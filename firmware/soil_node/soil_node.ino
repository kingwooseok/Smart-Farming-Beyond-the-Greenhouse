#include <Arduino.h>
#include <util/atomic.h>
#include <util/delay.h>

#define SCL_CLOCK 100000L

#define MY_ADDRESS 0x20

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

uint16_t readADC(uint8_t channel) {
  DDRB |= 0x08;
  PORTB |= 0x08;

  uint16_t result=0;
  ADMUX = (1 << REFS0) | (channel & 0x07);
  ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
  ADCSRA |= (1 << ADSC);
  while (ADCSRA & (1 << ADSC))
  ;  // ADSC 비트가 0이 될 때까지 기다림
  result = ADCL;
  result |= ((uint16_t)ADCH << 8);
  PORTB &= ~(0x08);

  return result;
}

void UART_init(unsigned int ubrr) {

  UBRR0H = (unsigned char)(ubrr >> 8);
  UBRR0L = (unsigned char)ubrr;

  UCSR0B = (1 << RXEN0) | (1 << TXEN0);

  UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

void UART_transmit(unsigned char data) {
  while (!(UCSR0A & (1 << UDRE0)));
  UDR0 = data;
}

void string_transmit_ln(const char data []) {
  for (int i = 0; data[i] != '\0'; i ++) {
    UART_transmit(data[i]);
  }
  UART_transmit('\n');
}

void string_transmit(const char data []) {
  for (int i = 0; data[i] != '\0'; i ++) {
    UART_transmit(data[i]);
  }
}

void send_number(uint16_t num) {
  char buf[10];
  itoa(num, buf, 10);
  string_transmit(buf);
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
  UART_init(UBRR);
  string_transmit_ln("Smart farming node initialized.");

  PORTC |= (1 << PC4) | (1 << PC5);

  TWSR = 0x00;
  TWBR = ((F_CPU / SCL_CLOCK) - 16) / 2;

  TWAR = (MY_ADDRESS << 1);

  TWCR = (1 << TWEN) | (1 << TWEA);

  timer2_init();

  string_transmit_ln("Node initialized. My address is 0x20");

}

void loop(){

  unsigned long ticks;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { ticks = timer2_overflow_count; }
  if (ticks >= TRANSMIT_INTERVAL) {
    int sensorValue = readADC(0);
    int scaledValue = (100UL * sensorValue) / 660UL;
    if (scaledValue > 100) scaledValue = 100;

    string_transmit("Master: Attempting to send [ID, Value] -> [0x20, ");
    send_number(scaledValue);
    string_transmit_ln("]");
    string_transmit("Humidity: ");
    send_number(scaledValue);
    string_transmit("\n");
    unsigned char data_packet[2];
    data_packet[0] = MY_ADDRESS;   // Byte 1: 보낸 사람 (나)의 주소
    data_packet[1] = scaledValue;  // Byte 2: 실제 데이터
    if (twi_master_transmit(data_packet, 2)) {
      string_transmit_ln("Master: Send success!");
      string_transmit("\n");
    } else {
      string_transmit_ln("Master: Send failed (Bus busy, NACK, or Arbitration Lost).");
    }
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { timer2_overflow_count = 0; }
  }
}
