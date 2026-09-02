# Senior-Friendly-Kiosk

웹캠 기반 고령자 인식과 자동 높이 조절 기능을 결합한  
**고령자 친화형 적응형 키오스크 시스템**입니다.

사용자의 얼굴을 웹캠으로 인식하여 고령자로 판단되면
ESP32-S3 기반 키오스크에 **Simple Mode 전환 안내**를 제공하고,
거리센서와 스텝모터를 이용하여 사용자의 위치에 맞게
키오스크 높이를 자동으로 조절합니다.

일반 사용자는 기존 키오스크와 동일한 **Normal Mode**를 사용할 수 있으며,
고령자에게는 보다 단순하고 큰 UI를 제공하여
키오스크 사용의 접근성을 높이는 것을 목표로 합니다.

---

## 프로젝트 목적

기존 키오스크는 모든 사용자에게 동일한 화면 구성과 높이를 제공하기 때문에
고령자의 경우 작은 글씨와 복잡한 UI, 고정된 화면 높이로 인해
사용에 불편을 겪을 수 있습니다.

본 프로젝트에서는 다음 두 가지 기능을 통해 이러한 문제를 개선하고자 했습니다.

1. **웹캠 기반 고령자 자동 인식**
2. **사용자 위치에 따른 키오스크 높이 자동 조절**

이를 통해 사용자가 직접 접근성 기능을 설정하지 않아도
키오스크가 사용자의 특성을 감지하여 자동으로 적응할 수 있도록 구현했습니다.

---

## 주요 기능

### 1. 웹캠 기반 고령자 인식

- OpenCV를 이용한 실시간 얼굴 검출
- OpenVINO 기반 연령 추정
- 여러 번 측정한 연령값의 중앙값을 이용한 판정 안정화
- 60세 이상 사용자를 고령자로 판단
- 고령자 감지 시 ESP32-S3에 `S` 신호 전송
- 동일 사용자에게 신호가 반복 전송되는 현상 방지
- 사용자가 사라지면 다음 사용자를 위해 판정 초기화

---

### 2. 고령자용 Simple Mode

- 일반 사용자를 위한 Normal Mode 제공
- 고령자 감지 시 Simple Mode 전환 안내 팝업 표시
- 큰 버튼과 단순화된 화면 구성
- 메뉴 선택
- 수량 조절
- 장바구니
- 주문 금액 계산
- 결제 화면
- 주문 완료 화면

---

### 3. 키오스크 자동 높이 조절

- VL53L0X 거리센서를 이용한 사용자 감지
- 스텝모터를 이용한 키오스크 높이 조절
- 사용자 감지 시 단계적으로 상승
- 사용자를 놓친 경우 단계적으로 하강
- 상단 / 하단 리미트 스위치를 이용한 이동 제한
- 전원 인가 시 자동 하단 원점 설정
- 결제 완료 후 키오스크 자동 하단 복귀

---

### 4. 장치 간 Serial 통신

Python 프로그램이 ESP32-S3 및 Arduino UNO와 통신하며
고령자 인식과 결제 완료 상태를 각 장치에 전달합니다.

주요 통신 신호는 다음과 같습니다.

| 송신 | 수신 | 신호 | 기능 |
|---|---|---|---|
| Python | ESP32-S3 | `S` | 고령자 감지 / Simple Mode 팝업 요청 |
| ESP32-S3 | Python | `PAYMENT_COMPLETE` | 결제 완료 알림 |
| Python | Arduino UNO | `H` | 모터 하단 복귀 요청 |

---

## 전체 시스템 구성

```text
                    ┌───────────────┐
                    │    Webcam     │
                    └───────┬───────┘
                            ↓
                    ┌───────────────┐
                    │    Python     │
                    │ OpenCV        │
                    │ OpenVINO      │
                    └───────┬───────┘
                            │
                   고령자 감지 : S
                            ↓
                    ┌───────────────┐
                    │   ESP32-S3    │
                    │  Touch LCD 7  │
                    │               │
                    │ Normal Mode   │
                    │ Simple Mode   │
                    └───────┬───────┘
                            │
                  PAYMENT_COMPLETE
                            ↓
                    ┌───────────────┐
                    │    Python     │
                    └───────┬───────┘
                            │ H
                            ↓
                    ┌───────────────┐
                    │ Arduino UNO   │
                    └───────┬───────┘
                            │
            ┌───────────────┼───────────────┐
            ↓               ↓               ↓
        VL53L0X         Step Motor      Limit Switch
                            ↓
                  Kiosk Height Control
```

---

## 전체 동작 과정

### 사용자 접근

```text
사용자 접근
   ↓
웹캠 얼굴 검출
   ↓
연령 추정
   ↓
고령자 여부 판단
```

일반 사용자로 판단되면 기존 Normal Mode를 유지합니다.

```text
일반 사용자
   ↓
Normal Mode
```

고령자로 판단되면 ESP32-S3에 `S` 신호를 전송합니다.

```text
60세 이상 감지
   ↓
Python
   ↓
S
   ↓
ESP32-S3
   ↓
Simple Mode 전환 안내
```

---

### 높이 조절

Arduino UNO는 VL53L0X 거리센서를 이용하여
사용자의 위치를 감지합니다.

```text
VL53L0X 거리 측정
   ↓
사용자 감지
   ↓
10cm 상승
   ↓
다시 거리 측정
```

사용자가 감지되지 않으면 다음과 같이 동작합니다.

```text
사용자 미감지
   ↓
3cm 하강
   ↓
다시 거리 측정
```

이를 반복하여 사용자의 위치에 맞는 키오스크 높이를 탐색합니다.

---

### 주문 과정

```text
Home
   ↓
매장 / 포장 선택
   ↓
메뉴 카테고리 선택
   ↓
음료 선택
   ↓
수량 선택
   ↓
장바구니
   ↓
결제
   ↓
주문 완료
```

---

### 결제 완료

결제가 완료되면 ESP32-S3가 Python 프로그램에
다음 메시지를 전송합니다.

```text
PAYMENT_COMPLETE
```

Python 프로그램은 이를 확인하여
Arduino UNO에 `H` 명령을 전달합니다.

```text
ESP32-S3
   ↓
PAYMENT_COMPLETE
   ↓
Python
   ↓
H
   ↓
Arduino UNO
   ↓
스텝모터 하강
   ↓
하단 리미트 스위치 감지
   ↓
모터 정지
```

키오스크는 최하단 위치로 복귀한 후
다음 사용자를 기다립니다.

---

## 고령자 판단 기준

현재 고령자 판단 기준은 다음과 같습니다.

```python
SENIOR_ENTER_AGE = 60
SENIOR_EXIT_AGE = 50
```

- `SENIOR_ENTER_AGE` : 추정 연령이 60세 이상이면 고령자로 판단
- `SENIOR_EXIT_AGE` : 추정 연령이 50세 이하로 내려가면 기존 고령자 판정 상태 해제

두 값 사이에 차이를 두어
연령 추정값이 60세 부근에서 흔들리더라도
Simple Mode 요청 신호가 반복적으로 발생하는 현상을 줄였습니다.

또한 얼굴이 일정 시간 동안 검출되지 않으면
기존 사용자의 판정 정보를 초기화하고
다음 사용자를 새롭게 인식합니다.

---

## 프로젝트 구성

```text
Senior-Friendly-Kiosk/
│
├── Cafe_Kiosk_Menu_Images/
│   ├── Cafe_Kiosk_Menu_Images.ino
│   ├── drink_images.h
│   ├── esp_panel_board_custom_conf.h
│   ├── lvgl_v8_port.cpp
│   ├── lvgl_v8_port.h
│   
│
├── elderly_project/
│   ├── elderly_detection_to_esp32_final.py
│   ├── serial_test.py
│   │
│   ├── models/
│   │   ├── age-gender-recognition-retail-0013.xml
│   │   └── age-gender-recognition-retail-0013.bin
│   
│   
│
├── sketch_aug29a/
│   ├── sketch_aug29a.ino
│  

```

---

## 폴더별 역할

### Cafe_Kiosk_Menu_Images

ESP32-S3 Touch LCD에서 실행되는
카페 키오스크 UI 프로그램입니다.

다음 기능을 담당합니다.

- Normal Mode
- Simple Mode
- 메뉴 화면
- 메뉴 이미지
- 메뉴 상세 화면
- 수량 선택
- 장바구니
- 결제
- 주문 완료
- Python과 Serial 통신

---

### elderly_project

웹캠 기반 고령자 인식과
각 장치 사이의 통신을 담당하는 Python 프로그램입니다.

다음 기능을 담당합니다.

- 웹캠 영상 입력
- OpenCV 얼굴 검출
- OpenVINO 연령 추정
- 고령자 판정
- ESP32-S3에 `S` 신호 전송
- ESP32-S3의 결제 완료 메시지 수신
- Arduino UNO에 모터 복귀 명령 전달

---

### sketch_aug29a

Arduino UNO를 이용한
키오스크 자동 높이 조절 프로그램입니다.

다음 기능을 담당합니다.

- VL53L0X 거리센서 측정
- 사용자 감지
- 스텝모터 상승 / 하강
- 상단 / 하단 리미트 스위치 감지
- 초기 원점 설정
- 결제 완료 후 자동 하단 복귀

---

## Hardware

프로젝트에서 사용하는 주요 하드웨어는 다음과 같습니다.

- ESP32-S3 Touch LCD 7
- Arduino UNO
- ZIO C960 USB Webcam
- VL53L0X ToF Distance Sensor
- Step Motor
- Motor Driver
- Upper Limit Switch
- Lower Limit Switch
- PC / Laptop

---

## Software

### Python

- Python
- OpenCV
- OpenVINO
- NumPy
- PySerial

### ESP32-S3

- Arduino IDE
- ESP32 Arduino Core
- LVGL v8
- ESP32 Display Panel Library

### Arduino UNO

- Arduino IDE
- Wire
- Adafruit VL53L0X
- SoftwareSerial

---

## Python 라이브러리 설치

필요한 Python 라이브러리는 다음 명령으로 설치할 수 있습니다.

```bash
pip install opencv-python openvino numpy pyserial
```

---

## 실행 전 설정

Python 프로그램을 실행하기 전에
ESP32-S3와 Arduino UNO의 COM 포트 및 웹캠 번호를 확인해야 합니다.

```python
ESP32_PORT = "COM8"
ARDUINO_PORT = "COM3"
BAUD_RATE = 115200

CAMERA_INDEX = 1
```

COM 포트와 카메라 번호는
사용하는 컴퓨터의 연결 환경에 따라 변경될 수 있습니다.

---

## 실행 순서

### 1. ESP32-S3 프로그램 업로드

`Cafe_Kiosk_Menu_Images` 폴더의 메인 프로그램을
ESP32-S3 Touch LCD에 업로드합니다.

```text
Cafe_Kiosk_Menu_Images.ino
```

### 2. Arduino UNO 프로그램 업로드

`sketch_aug29a` 폴더의 프로그램을
Arduino UNO에 업로드합니다.

```text
sketch_aug29a.ino
```

### 3. 하드웨어 연결 확인

- ESP32-S3 연결
- Arduino UNO 연결
- 웹캠 연결
- VL53L0X 연결
- 모터 드라이버 연결
- 리미트 스위치 연결

### 4. Python 프로그램 실행

CMD 또는 터미널에서 `elderly_project` 폴더로 이동한 후
다음 프로그램을 실행합니다.

```bash
python elderly_detection_to_esp32_final.py
```

프로그램이 실행되면 웹캠을 통해
사용자의 얼굴과 연령을 실시간으로 분석합니다.

프로그램 종료는 웹캠 화면에서 `Q` 키를 누릅니다.

---

## 프로젝트 특징

기존 키오스크가 모든 사용자에게
동일한 UI와 동일한 화면 높이를 제공하는 것과 달리,
본 프로젝트는 사용자의 특성과 위치를 감지하여
키오스크 환경을 자동으로 변경합니다.

```text
기존 키오스크
→ 사용자가 키오스크에 맞춰야 함

Senior-Friendly-Kiosk
→ 키오스크가 사용자에게 맞춰짐
```

즉, 단순히 고령자 전용 화면을 제공하는 것을 넘어
**사용자 인식 + UI 적응 + 물리적 높이 조절**을 하나의 시스템으로 결합한 것이
본 프로젝트의 주요 특징입니다.
