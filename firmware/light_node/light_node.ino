#include <Arduino.h>
#include <util/atomic.h>
#include <util/delay.h>

#define SCL_CLOCK 100000L

#define MY_ADDRESS 0x28
#define TARGET_ADDRESS 0x25

volatile unsigned long timer2_overflow_count = 0;
const unsigned long TRANSMIT_INTERVAL = 61 * 3;

const int UBRR = 103;

void timer2_init(void) {
  TCCR2A = 0;
  TCCR2B = (1 << CS22) | (1 << CS21) | (1 << CS20); // 프리스케일러
  TIMSK2 |= (1 << TOIE2); // 오버플로우 인터럽트 활성화
}

ISR(TIMER2_OVF_vect) {
  timer2_overflow_count++;
}
int sensorValue;                // A0 핀의 값을 읽음 (0 ~ 1023)
int previousSensorValue;        // A0 핀 이전 값 저장.
uint16_t readADC(uint8_t channel) {
  ADMUX = (1 << REFS0) | (channel & 0x07);
  ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
  ADCSRA |= (1 << ADSC);
  while (ADCSRA & (1 << ADSC));  // ADSC 비트가 0이 될 때까지 기다림
  uint16_t result = ADCL;
  result |= ((uint16_t)ADCH << 8);

  return result;
}

// TWINT is cleared by writing one. All waits have a finite timeout.
bool twi_wait() {
  for (uint16_t i = 0; i < 20000; ++i) {
    if (TWCR & (1 << TWINT)) return true;
    _delay_us(10);
  }
  TWCR = 0; // Release SDA/SCL after a stalled transfer.
  TWCR = (1 << TWEN) | (1 << TWEA);
  return false;
}

bool twi_finish(bool success) {
  const uint8_t status = TWSR & 0xF8;
  if (status == 0x68) return false; // Arbitration lost, now addressed: poll RX next.
  if (status == 0x38) {
    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
    return false; // The winning master owns STOP.
  }
  TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN) | (1 << TWEA);
  for (uint16_t i = 0; i < 20000; ++i) {
    if (!(TWCR & (1 << TWSTO))) return success;
    _delay_us(10);
  }
  TWCR = 0;
  TWCR = (1 << TWEN) | (1 << TWEA);
  return false;
}

bool twi_master_transmit(unsigned char* data, uint8_t length) {
  // Preserve a received address/data event that the next loop must service.
  if (TWCR & (1 << TWINT)) return false;
  TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN) | (1 << TWEA);
  if (!twi_wait()) return false;
  if ((TWSR & 0xF8) != 0x08 && (TWSR & 0xF8) != 0x10) return twi_finish(false);
  TWDR = TARGET_ADDRESS << 1;
  TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
  if (!twi_wait()) return false;
  if ((TWSR & 0xF8) != 0x18) return twi_finish(false);
  for (uint8_t i = 0; i < length; ++i) {
    TWDR = data[i];
    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA);
    if (!twi_wait()) return false;
    if ((TWSR & 0xF8) != 0x28) return twi_finish(false);
  }
  return twi_finish(true);
}

void setup(){
  UART_init(UBRR); // 디버깅용 시리얼
  string_transmit_ln("Smart farming node initialized.");
  PORTC |= (1 << PC4) | (1 << PC5);
  TWSR = 0x00;
  TWBR = ((F_CPU / SCL_CLOCK) - 16) / 2;
  TWAR = (MY_ADDRESS << 1);
  TWCR = (1 << TWEN) | (1 << TWEA);

  timer2_init();
  SREG|= (1<<7);

  string_transmit("Node initialized. My address is 0x");
  send_number_hex(MY_ADDRESS);
  string_transmit("\n");
}

void loop(){

  int aboveValue;                 // 700 이하면 1.
  int diffValue;                  // ADC 값의 차이가 클 때 1.
  unsigned char scaledValue;      // 최종적으로 보내는 0(day) or 1(night) 값.
  static int a = 1;                          // scaledValue 지정을 위한 임시변수.

  int dayOrNight = 700;           // 밤인가 낮인가. aboveValue와 같이 쓰임.
  int absNight = 500;             // 무조건 밤인 ADC값.
  int absDay = 800;               // 무조건 낮인 ADC값.
  int compareSensorValue = 300;   // previousSensorValue와 sensorValue의 차이가 심한 기준

  unsigned long ticks;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { ticks = timer2_overflow_count; }
  if (ticks >= TRANSMIT_INTERVAL) {

    static bool first_sample = true;
    previousSensorValue = sensorValue;
    sensorValue = readADC(0);
    if (first_sample) {
      a = sensorValue <= dayOrNight;
      first_sample = false;
    }
    if (sensorValue > dayOrNight) {
      aboveValue = 0; // Day
    }
    else {
      aboveValue = 1; //Night
    }
    if (abs(sensorValue - previousSensorValue) > compareSensorValue) {
      diffValue = 1;  // 차이가 큼.
    }
    else {
      diffValue = 0;  // 차이가 작음.
    }
    if ( (sensorValue < absNight) || ( diffValue && aboveValue ) ) {
      a = 1;

    }
    else if ( (sensorValue > absDay) || ( diffValue && !aboveValue ) )
    {
      a = 0;

    }

    scaledValue = a;
    string_transmit("Master: Attempting to send [ID, Value] -> [0x");
    send_number_hex(MY_ADDRESS);
    string_transmit(", ");
    send_number(scaledValue);
    string_transmit("]\nLight Flux: ");
    send_number(sensorValue);
    string_transmit("\n");
    unsigned char data_packet[2];
    data_packet[0] = MY_ADDRESS;  // Byte 1: 보낸 사람 (나)의 주소
    data_packet[1] = scaledValue; // Byte 2: 실제 데이터
    if (scaledValue) {
      string_transmit_ln("night");
    } else {
      string_transmit_ln("day");
    }
    if (twi_master_transmit(data_packet, 2)) {
      string_transmit("Master: Send success!\n");
    } else {
      string_transmit("Master: Send failed (Bus busy, NACK, or Arbitration Lost).\n");
    }
    string_transmit("\n");
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { timer2_overflow_count = 0; }
  }
}
void UART_init(unsigned int ubrr) {

  //set baud rate
  UBRR0H = (unsigned char)(ubrr >> 8);
  UBRR0L = (unsigned char)ubrr;

  //ENABLE reciever and transmitter
  UCSR0B = (1 << RXEN0) | (1 << TXEN0); //송신과 수신 모두 활성화

  //set frame format: 8 data bits, 1stop bit
  UCSR0C = (1 << UCSZ01) | (1 << UCSZ00); //UCSR0C레지스터는 통신 모드 (비동기/동기), 패리티 설정, 정지 비트 수, 데이터 비트 수 등 프레임 형식을 설정
}

void UART_transmit(unsigned char data) {
  //Wait for empty transmit buffer
  while (!(UCSR0A & (1 << UDRE0))); //UDRE0 비트가 1일 때까지 기다리고

  //put data into buffer, sends the data
  UDR0 = data; //UDR0는 USART I/O 레지스터
}
void string_transmit_ln(const char data []) {
  for (int i = 0; data[i] != '\0'; i ++) {
    UART_transmit(data[i]);
  }
  UART_transmit('\n'); // 줄바꿈
}
void string_transmit(const char data []) {
  for (int i = 0; data[i] != '\0'; i ++) {
    UART_transmit(data[i]);
  }
}

void send_number(uint16_t num) {
  char buf[10];
  itoa(num, buf, 10);  // 10진수 문자열로 변환
  string_transmit(buf);
}

void send_number_hex(uint8_t num) {
  char buf[4];
  itoa(num, buf, 16);  // 16진수 문자열로 변환
  string_transmit(buf);
}
