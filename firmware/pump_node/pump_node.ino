#include <Arduino.h>
#include <util/atomic.h>
#include <util/delay.h>

#define SCL_CLOCK 100000L

#define MY_ADDRESS 0x21

volatile bool pwm_is_active = false;
volatile unsigned long timer2_overflow_count = 0;
const unsigned long PUMP_OFF_COUNT = 183;

void setup(){
  UART_init(103); // 디버깅용 시리얼
  string_transmit_ln("Smart farming node initialized.");

  PORTD &= ~((1 << PD6) | (1 << PD5));
  DDRD |= 0x60; // 워터펌프: 6번핀(PD6), 5번핀(PD5)
  // Fast PWM, non-inverting output
  TCCR0A = (1 << COM0A1) | (1 << WGM01) | (1 << WGM00);
  // Prescaler 64 (976Hz)
  TCCR0B = (1 << CS01)|(1 << CS00);

  OCR0A = 0; // 초기 워터펌프 PWM 0으로 시작
  TCCR0A &= ~(1 << COM0A1); // OC0A(PD6) 핀 연결 해제 (꺼놓고 시작)
  TCCR2A = 0;
  TCCR2B = 0; // timer2 꺼둔 채로 시작
  TIMSK2 |= (1 << TOIE2); // Overflow 인터럽트 활성화
  PORTC |= (1 << PC4) | (1 << PC5);
  TWSR = 0x00;
  TWBR = ((F_CPU / SCL_CLOCK) - 16) / 2;
  TWAR = (MY_ADDRESS << 1);
  TWCR = (1 << TWEN) | (1 << TWEA);
  string_transmit("Node initialized. My address is 0x");
  send_hex(MY_ADDRESS);
  string_transmit("\n");
}
ISR(TIMER2_OVF_vect) {
  if(pwm_is_active){
    timer2_overflow_count++;
    if (timer2_overflow_count >= PUMP_OFF_COUNT){ // 시간 지나면 여기서 끔
      TCCR0A &= ~(1 << COM0A1); // 워터펌프 핀연결 해제
      TCCR2B = 0; // 타이머2 분주비 0 -> 타이머 정지
      PORTD &= ~((1 << PD6) | (1 << PD5));
      pwm_is_active = false;
    }
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
void send_hex(uint8_t num) {
  char buf[10];
  itoa(num, buf, 16);
  string_transmit(buf);
}

// Commit exactly two data bytes at STOP/repeated START.
void twi_slave_listen() {
  static uint8_t count = 0;
  static uint8_t sender = 0;
  static uint8_t value = 0;
  if (!(TWCR & (1 << TWINT))) return;
  const uint8_t status = TWSR & 0xF8;
  switch (status) {
    case 0x60:
    case 0x68:
    count = 0;
    break;
    case 0x80:
    if (count == 0) sender = TWDR;
    else if (count == 1) value = TWDR;
    if (count < 3) ++count;
    break;
    case 0xA0: {
      const bool valid = count == 2;
      count = 0;
      // Release clock stretching before processing the complete frame.
      TWCR = (1 << TWINT) | (1 << TWEA) | (1 << TWEN);
      if (!valid) return;

      if (sender == 0x25 && value > 0 && value <= 254 && !pwm_is_active) {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
          timer2_overflow_count = 0;
          TCNT2 = 0;
          TIFR2 = (1 << TOV2);
          OCR0A = value;
          PORTD &= ~(1 << PD5);
          pwm_is_active = true;
          TCCR0A |= (1 << COM0A1);
          TCCR2B = (1 << CS22) | (1 << CS21) | (1 << CS20);
        }
      }

      return;
    }
    case 0x00:
    count = 0;
    TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN) | (1 << TWEA);
    return;
    default:
    count = 0;
    break;
  }
  // ACK both the sender ID and the value, including the first data byte.
  TWCR = (1 << TWINT) | (1 << TWEA) | (1 << TWEN);
}

void loop(){
  twi_slave_listen();
}
