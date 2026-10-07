#include <Arduino.h>
#include "control_math.h"
#include <util/atomic.h>
#include <util/delay.h>

#define SCL_CLOCK 100000L

#define SOIL_ADDRESS 0x20
#define LIGHT_ADDRESS 0x28
#define MY_ADDRESS 0x25
#define TARGET_ADDRESS 0x21

#define FC control::target
#define TRIGGER control::trigger

volatile unsigned long timer2_overflow_count = 0;
unsigned long cycle_start_count = 0;
volatile unsigned long current_count = 0;

const unsigned long PWM_INTERVAL = 61 * 15;
const unsigned long UPDATE_INTERVAL = 61 * 5;

volatile uint8_t is_night = 0;

volatile bool night_block = true;
volatile bool soil_block = true;

volatile float err = 0.0f;
volatile float pwm = 150.0f;

volatile uint16_t hum_data;
volatile uint8_t light_data = 1;
bool soil_received = false;
bool light_received = false;
bool update_pending = false;
unsigned long soil_received_at = 0;
unsigned long light_received_at = 0;
unsigned long soil_sequence = 0;
unsigned long watered_sequence = 0;
const unsigned long SENSOR_STALE_COUNT = 61 * 10;

ISR(TIMER2_OVF_vect) {
  timer2_overflow_count++;
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

void send_number(float num) {
  char buf[10];
  dtostrf(num, 0, 2, buf);
  string_transmit(buf);
}

void send_hex(uint8_t num) {
  char buf[10];
  itoa(num, buf, 16);
  string_transmit(buf);
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
  UART_init(103);

  TCCR2A = 0;
  TCCR2B = (1 << CS22) | (1 << CS21) | (1 << CS20);
  TIMSK2 |= (1 << TOIE2);

  PORTC |= (1 << PC4) | (1 << PC5);

  TWSR = 0x00;
  TWBR = ((F_CPU / SCL_CLOCK) - 16) / 2;

  TWAR = (MY_ADDRESS << 1);

  TWCR = (1 << TWEN) | (1 << TWEA);

  string_transmit("Node initialized. My address is 0x");
  send_hex(MY_ADDRESS);
  string_transmit("\n");
  string_transmit_ln("----------------------------------");
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

      if (sender == SOIL_ADDRESS && value <= 100) {
        hum_data = value;
        soil_received = true;
        ++soil_sequence;
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { soil_received_at = timer2_overflow_count; }
      } else if (sender == LIGHT_ADDRESS && value <= 1) {
        light_data = value;
        light_received = true;
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { light_received_at = timer2_overflow_count; }
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

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { current_count = timer2_overflow_count; }

  if(control::due(current_count, cycle_start_count, PWM_INTERVAL)){
    cycle_start_count = current_count;
    update_pending = false;
    is_night = light_data;

    if (is_night == 1) {
      night_block = true;
      string_transmit_ln("Light : night");
    }
    else{
      night_block = false;
      string_transmit_ln("Light : day");
    }

    string_transmit("Soil Humidity : ");
    send_number(hum_data);
    string_transmit("\n");

    if (hum_data > TRIGGER) {
      soil_block = true;
      string_transmit_ln("too wet");
      string_transmit_ln("----------------------------------");
    }
    else {
      soil_block = false;
    }

    if (soil_received && light_received &&
    current_count - soil_received_at < SENSOR_STALE_COUNT &&
    current_count - light_received_at < SENSOR_STALE_COUNT &&
    control::should_water(is_night, hum_data)){
      unsigned char send_data[2] = {0};
      send_data[0] = MY_ADDRESS;
      send_data[1] = (unsigned char)pwm;
      update_pending = twi_master_transmit(send_data, 2);
      if (update_pending) {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { cycle_start_count = timer2_overflow_count; }
        current_count = cycle_start_count;
        watered_sequence = soil_sequence;
      }

      string_transmit(update_pending ? "PWM sent: " : "PWM send failed: ");
      send_number(pwm);
      string_transmit("\n");
      string_transmit_ln("----------------------------------");
    }
    else {
      string_transmit_ln("Do not watering");
      string_transmit_ln("----------------------------------");
    }
  }

  if(update_pending && control::due(current_count, cycle_start_count, UPDATE_INTERVAL) &&
  soil_sequence != watered_sequence && current_count - soil_received_at < SENSOR_STALE_COUNT){
    if (update_pending){
      update_pending = false;
      err = FC - hum_data;
      string_transmit("error : ");
      send_number(err);
      string_transmit("\n");

      pwm = control::next_pwm(pwm, hum_data);
      string_transmit("NEXT PWM : ");
      send_number(pwm);
      string_transmit("\n");

      night_block = true;
      soil_block = true;

      string_transmit_ln("----------------------------------");
    }
  }
}
