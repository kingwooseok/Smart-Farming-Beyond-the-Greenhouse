# 소스 정리 내역

## 자료 구성

`firmware/`에는 조도·토양수분·중앙 제어기·관수 노드의 스케치가 있습니다. `archive/`에는 초기 TWI 송신 스케치를 보관했습니다.

| 소스 | 역할 |
|---|---|
| [`light_node`](../firmware/light_node/light_node.ino) | 조도 측정과 주야간 판정 전송 |
| [`soil_node`](../firmware/soil_node/soil_node.ino) | 토양수분 측정과 수분 지표 전송 |
| [`controller`](../firmware/controller/controller.ino) | 관수 조건 판단과 PWM 보정 |
| [`pump_node`](../firmware/pump_node/pump_node.ino) | PWM 명령 수신과 펌프 구동 |

아래 변경은 저장소의 소스 정리 과정에서 반영했습니다.

## 반영한 수정

| 항목 | 적용 내용 |
|---|---|
| 문자열·주석·함수 호출 | 줄바꿈으로 분리된 문자열·주석 복구, 잘못된 UART 함수 호출 수정 |
| 조도 경계 구간 | 최초 판정값을 초기화하고 이후에는 이전 판정 유지 |
| 수분 지표 | ADC 정규화 결과를 0~100 범위로 제한 |
| 첫 수신 바이트 | 송신 ID 수신 뒤에도 TWINT를 해제하고 다음 바이트 ACK 준비 |
| 프레임 처리 | 송신 ID·값 두 바이트와 프레임 종료를 확인한 뒤 데이터 반영 |
| TWI 대기 | 완료·STOP 대기에 유한 timeout 적용, 중재 상실 시 승자에게 STOP 제어 유지 |
| 타이머 공유 변수 | ISR과 메인 루프가 공유하는 32비트 카운터의 읽기·초기화를 atomic block으로 처리 |
| 보정 예약 | `==` 비교를 `>=`로 바꾸고 성공한 관수 명령에 대해 1회만 보정 |
| 시작·수신 지연 | 양쪽 센서값을 받은 뒤 관수를 허용하고 오래된 값으로 새 관수를 시작하지 않도록 처리 |
| 펌프 출력 | D5를 LOW로 고정하고 정지 시 PWM 핀을 LOW로 복귀 |
| 제어 계산 | 실제 PWM 갱신 함수를 분리해 경계값 검사를 적용 |

## 검증

- ATmega328P / 16 MHz, AVR-GCC 7.3.0, Arduino AVR core 1.8.6으로 네 스케치 컴파일·링크.
- `tests/control_checks.cpp`: 주야간·수분 트리거, 건조·습윤 보정, 목표값 유지, PWM 상하한, 지연된 루프, 32비트 카운터 wraparound를 compile-time assertion으로 검사.
- 하드웨어 동작 기록: [5개 시연](demo.md). 정리된 소스의 검증 범위는 빌드와 제어 로직 검사입니다.
