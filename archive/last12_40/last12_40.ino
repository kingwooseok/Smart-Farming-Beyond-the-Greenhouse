#define F_CPU 16000000UL
#define SCL_CLOCK 100000L

#define MY_ADDRESS 0x28
//주소는 각자 노드마다 다르게해야함! #define으로 설정하면 될듯
#define TARGET_ADDRESS 0x21 // 보내는곳

const unsigned long TRANSMIT_INTERVAL = 1111;
unsigned long previousMillis = 0;

volatile uint8_t received_sender_id;     // 수신된 '보낸 주소'를 저장할 변수
volatile bool expecting_sender_id = true; // '보낸 주소'를 기다리는 중인지 상태 플래그

int sensorValue; // A0 핀의 값을 읽음 (0 ~ 1023)
// --- 마스터 역할 수행 함수 ---
// 1바이트가 아닌, 여러 바이트(배열)를 전송하도록 수정됨
bool twi_master_transmit(unsigned char* data, uint8_t length) {

    // 1. START Condition 전송
    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
    if ((TWSR & 0xF8) != 0x08) return false; // START 실패

    // 2. Target 주소와 쓰기 모드(W=0) 전송
    TWDR = (TARGET_ADDRESS << 1) | 0;
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));

    // 3. 주소 전송 후 상태 확인
    switch (TWSR & 0xF8) {
        case 0x18: // SLA+W 전송 성공, ACK 받음
            break; // 계속 진행
        case 0x20: // SLA+W 전송 성공, NACK 받음
            TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN); // STOP
            return false;
        case 0x38: // 중재 손실 (Arbitration Lost)
            TWCR = (1 << TWINT) | (1 << TWEN);
            return false;
        default:
            return false; // 기타 에러
    }
    
    // 4. 데이터 바이트들을 순차적으로 전송 (수정된 부분)
    for (uint8_t i = 0; i < length; i++) {
        TWDR = data[i]; // 배열의 i번째 데이터 로드
        TWCR = (1 << TWINT) | (1 << TWEN); // 전송 시작
        while (!(TWCR & (1 << TWINT))); // 완료 대기

        // 데이터 전송 실패(NACK) 시
        if ((TWSR & 0xF8) != 0x28) {
            TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN); // STOP
            return false;
        }
    }

    // 5. 모든 데이터 전송 완료 후 STOP Condition 전송
    TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
    return true; // 전송 성공
}

////레지스터 단위의 analog read 를 하는 함수
uint16_t readADC(uint8_t channel) {
  // 채널 선택 (A0 ~ A5 → 0~5)
  ADMUX = (1 << REFS0) | (channel & 0x07);
  // REFS0 = 1 → AVcc를 기준 전압으로 선택
  // REFS1 = 0 → 외부 AREF 핀 사용 안 함

  // ADC 활성화 + 프리스케일러 설정 (ADC Enable + Prescaler 128)
  ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
  // 프리스케일러 128 → 16MHz / 128 = 125kHz (ADC 클럭 권장 범위)

  // 변환 시작
  ADCSRA |= (1 << ADSC);

  // 변환 완료 대기
  while (ADCSRA & (1 << ADSC));  // ADSC 비트가 0이 될 때까지 기다림

  // 결과 읽기 (하위 바이트 먼저)
  uint16_t result = ADCL;
  result |= ((uint16_t)ADCH << 8);

  return result;
}

void setup(){
  Serial.begin(9600); // 디버깅용 시리얼
  Serial.println("Multi-Slave Polling Master Initialized.");

   // 풀업 저항 활성화 (모든 노드가 활성화해도 괜찮음)
    PORTC |= (1 << PC4) | (1 << PC5);
    
    // SCL 주파수 설정 (100kHz)
    TWSR = 0x00;
    TWBR = ((F_CPU / SCL_CLOCK) - 16) / 2;
    
    // 자신의 슬레이브 주소 설정
    TWAR = (MY_ADDRESS << 1);
    
    // TWI 활성화 및 ACK 응답 활성화 (슬레이브 대기 모드로 시작)
    TWCR = (1 << TWEN) | (1 << TWEA);

    Serial.print("Node initialized. My address is 0x");
    Serial.println(MY_ADDRESS, HEX);

}

void loop(){
  unsigned long current_count;

unsigned long currentMillis = millis();

    if (currentMillis - previousMillis >= TRANSMIT_INTERVAL) {
    previousMillis = currentMillis;

      sensorValue = readADC(0);
    
    // 센서 값을 0~100 범위로 스케일링
    unsigned char scaledValue = (sensorValue*100.0)/1024.0;
    
    // 시리얼 모니터에 전송할 데이터 출력
    Serial.print("Master: Attempting to send [ID, Value] -> [0x");
    Serial.print(MY_ADDRESS, HEX);
    Serial.print(", ");
    Serial.print(scaledValue);
    Serial.println("]");
    
    // 2바이트 패킷 생성
    unsigned char data_packet[2];
    data_packet[0] = MY_ADDRESS;  // Byte 1: 보낸 사람 (나)의 주소
    data_packet[1] = scaledValue; // Byte 2: 실제 데이터
    
    // 수정된 함수를 호출하여 2바이트 패킷 전송
    if (twi_master_transmit(data_packet, 2)) {
        Serial.println("Master: Send success!");
    } else {
        Serial.println("Master: Send failed (Bus busy, NACK, or Arbitration Lost).");
    }
    
    // 마스터 역할 후, 슬레이브 수신 상태로 복귀
    TWAR = (MY_ADDRESS << 1);
    TWCR = (1 << TWINT) | (1 << TWEA) | (1 << TWEN);
}
}


    


