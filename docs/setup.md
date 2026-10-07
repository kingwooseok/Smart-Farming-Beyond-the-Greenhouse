# 배선과 실행

## 준비물

- Arduino Uno(ATmega328P, 16 MHz) 4대
- CdS 조도센서와 분압 저항
- Grove 토양수분센서
- L9110S 모터 드라이버와 3~5 V DC 워터펌프
- 물탱크, 호스, 화분, 브레드보드, 점퍼선
- SDA·SCL 외부 풀업 저항, 보드 전원, 펌프 사양에 맞는 전원

## 연결

| 연결 대상 | Uno 핀 | 연결 내용 |
|---|---|---|
| 모든 노드 | A4 / PC4 | 공통 SDA |
| 모든 노드 | A5 / PC5 | 공통 SCL |
| 모든 노드 | GND | 공통 접지 |
| 조도 노드 | A0 | 밝을수록 전압이 높아지는 CdS 분압 출력 |
| 토양수분 노드 | A0 | Grove 센서의 아날로그 출력 |
| 토양수분 노드 | D11 / PB3 | 측정 구간 표시용 출력 |
| 관수 노드 | D6 / PD6 / OC0A | L9110S의 PWM 입력 |
| 관수 노드 | D5 / PD5 | L9110S의 반대 방향 입력, LOW 유지 |
| L9110S | 모터 출력 | 워터펌프 |

![전체 회로도](../assets/circuit.png)

SDA·SCL에는 공통 5 V 기준 외부 풀업을 연결합니다. 짧은 브레드보드 배선에서는 각 선당 4.7 kΩ을 시작값으로 사용할 수 있습니다. 모듈에 이미 장착된 풀업이 있으면 병렬 합성값을 반영합니다. 펌프 전원은 모터 드라이버로 공급하고 MCU와 접지를 공유합니다.

## Arduino IDE

1. Arduino AVR Boards에서 **Arduino Uno**를 선택합니다.
2. `firmware/light_node/light_node.ino`를 열고 조도 노드 보드에 업로드합니다.
3. `soil_node`, `controller`, `pump_node`도 같은 방법으로 각각의 보드에 업로드합니다.
4. 시리얼 모니터는 **9,600 bps**로 설정합니다.
5. 밝은 환경에서 마른 흙을 감지하게 두고, 관수 명령과 펌프 동작을 확인합니다.
6. 센서 가림, 젖은 흙으로 이동, 흙의 양 변경 순서로 [시연 조건](demo.md)을 재현합니다.

각 스케치에 노드 주소가 설정되어 있습니다. 센서는 제어기 `0x25`로, 제어기는 관수 노드 `0x21`로 전송합니다.

## Arduino CLI

```bash
arduino-cli core install arduino:avr@1.8.6
for node in light_node soil_node controller pump_node; do
  arduino-cli compile --fqbn arduino:avr:uno "firmware/$node"
done
```

```bash
# 해당 노드가 연결된 포트를 지정한다.
arduino-cli upload --fqbn arduino:avr:uno --port /dev/ttyACM0 firmware/controller
```

## AVR 도구로 빌드

기존 Arduino AVR core와 AVR-GCC를 사용해 네 스케치를 컴파일·링크하고 제어식의 경계값을 검사할 수 있습니다.

```bash
python3 tools/build.py \
  --avr-bin /path/to/avr-gcc/bin \
  --arduino-core /path/to/arduino/avr/1.8.6/cores/arduino \
  --variant /path/to/arduino/avr/1.8.6/variants/standard
```

결과 ELF·HEX는 `build/`에 생성됩니다. `tools/build.py`는 Arduino core의 초기화·Timer0 구현을 링크하며, 원본 자료인 `archive/`는 빌드 대상에서 제외합니다.

## 제어 파라미터

| 설정 | 위치 | 기본값 |
|---|---|---|
| ADC 정규화 기준 | `soil_node.ino` | 660 |
| 조도 판정 | `light_node.ino` | 야간 500 미만, 주간 800 초과, 변화량 300, 경계 700 |
| 목표·트리거 | `controller/control_math.h` | 47.8 / 37.87 |
| 초기 PWM | `controller.ino` | 150 |
| 건조·습윤 보정 이득 | `controller/control_math.h` | 0.40 / 0.80 |
| 관수 주기·후속 보정 | `controller.ino` | `61 × 15` / `61 × 5` ticks |
| 센서값 유효 시간 | `controller.ino` | `61 × 10` ticks |
| 펌프 구동 시간 | `pump_node.ino` | 183 ticks |

공통 시간 기준은 Timer2 오버플로우 1회 = 16.384 ms입니다.
