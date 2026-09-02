# Elderly Detection

Python과 웹캠을 이용하여 사용자의 얼굴을 검출하고,
OpenVINO 기반 연령 추정 모델을 통해 고령자 여부를 판단하는 프로그램입니다.

고령자로 판단되면 ESP32-S3 키오스크에 `S` 신호를 전송하여
고령자를 위한 **Simple Mode 전환 안내 팝업**을 실행합니다.

또한 ESP32-S3에서 결제 완료 메시지를 수신하면
Arduino UNO에 키오스크 모터 하단 복귀 명령을 전달합니다.

---

## 주요 기능

- USB 웹캠을 이용한 실시간 영상 입력
- OpenCV 기반 얼굴 검출
- OpenVINO 기반 연령 추정
- 여러 번의 연령 추정값을 이용한 판정 안정화
- 60세 이상 감지 시 고령자로 판단
- 고령자 감지 시 ESP32-S3에 `S` 신호 전송
- 동일 사용자에게 `S` 신호가 반복 전송되는 현상 방지
- 사용자가 사라지면 다음 사용자를 위한 판정 초기화
- ESP32-S3와 Serial 통신
- Arduino UNO와 Serial 통신
- 결제 완료 후 Arduino에 모터 하단 복귀 명령 전달

---

## 동작 과정

웹캠으로 사용자의 얼굴을 검출하고,
검출된 얼굴 영역을 OpenVINO 연령 추정 모델에 전달합니다.

```text
사용자
   ↓
Webcam
   ↓
OpenCV
   ↓
Face Detection
   ↓
OpenVINO
   ↓
Age Estimation
   ↓
Senior Detection
```

고령자로 판단되면 ESP32-S3에 `S` 신호를 전송합니다.

```text
Senior Detection
   ↓
S
   ↓
ESP32-S3
   ↓
Simple Mode Popup
```

---

## 고령자 판단 기준

현재 고령자 판단 기준은 다음과 같습니다.

```python
SENIOR_ENTER_AGE = 60
SENIOR_EXIT_AGE = 50
```

- `SENIOR_ENTER_AGE` : 추정 연령이 60세 이상이면 고령자로 판단
- `SENIOR_EXIT_AGE` : 추정 연령이 50세 이하로 내려가면 기존 고령자 판정 상태를 해제

두 기준값 사이에 차이를 두어 연령 추정값이 60세 부근에서 흔들리더라도
Simple Mode 신호가 반복적으로 전송되는 현상을 줄이도록 구성했습니다.

또한 얼굴이 일정 시간 동안 검출되지 않으면
기존 연령 데이터와 판정 상태가 자동으로 초기화되어
다음 사용자를 새롭게 판정합니다.

---

## 판정 안정화

웹캠 기반 연령 추정값은 촬영 환경이나 얼굴 각도에 따라
매번 조금씩 달라질 수 있습니다.

따라서 한 번의 추정값만으로 고령자를 판단하지 않고,
여러 번 측정한 값을 이용합니다.

```python
AGE_HISTORY_SIZE = 20
MIN_AGE_SAMPLES = 5
AGE_ESTIMATE_INTERVAL = 5
NO_FACE_RESET_FRAMES = 60
```

- `AGE_HISTORY_SIZE` : 최근 연령 추정값을 최대 20개까지 저장
- `MIN_AGE_SAMPLES` : 최소 5개의 연령값이 수집된 후 판정 시작
- `AGE_ESTIMATE_INTERVAL` : 5프레임마다 한 번씩 연령 추정
- `NO_FACE_RESET_FRAMES` : 얼굴이 일정 시간 검출되지 않으면 이전 사용자 정보 초기화

저장된 연령값의 중앙값을 사용하여
순간적인 연령 추정 오차를 줄이도록 구성했습니다.

---

## Serial Communication

ESP32-S3와 Arduino UNO 모두 다음 통신 속도를 사용합니다.

```text
115200 baud
```

현재 기본 포트 설정은 다음과 같습니다.

```python
ESP32_PORT = "COM8"
ARDUINO_PORT = "COM3"
CAMERA_INDEX = 1
```

COM 포트 번호와 웹캠 번호는
사용하는 컴퓨터의 연결 환경에 따라 변경할 수 있습니다.

### Python → ESP32-S3

고령자로 판단되면 Python 프로그램이 다음 신호를 전송합니다.

```text
S
```

| 명령 | 기능 |
|---|---|
| `S` | Simple Mode 전환 팝업 요청 |

### ESP32-S3 → Python

키오스크에서 결제가 완료되면
ESP32-S3가 다음 메시지를 전송합니다.

```text
PAYMENT_COMPLETE
```

### Python → Arduino UNO

Python이 `PAYMENT_COMPLETE` 메시지를 확인하면
Arduino UNO에 다음 명령을 전달합니다.

```text
H
```

| 명령 | 기능 |
|---|---|
| `H` | 키오스크 모터 하단 복귀 요청 |

결제 완료 후의 전체 통신 과정은 다음과 같습니다.

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
Motor Return
```

---

## 사용 라이브러리

- OpenCV
- OpenVINO
- NumPy
- PySerial

필요한 라이브러리는 다음 명령으로 설치할 수 있습니다.

```bash
pip install opencv-python openvino numpy pyserial
```

---

## 파일 구성

```text
elderly_project/
│
├── elderly_detection_to_esp32_final.py
├── serial_test.py
│
├── models/
│   ├── age-gender-recognition-retail-0013.xml
│   └── age-gender-recognition-retail-0013.bin
│
└── README.md
```

### elderly_detection_to_esp32_final.py

고령자 인식 시스템의 메인 Python 프로그램입니다.

다음 기능을 담당합니다.

- USB 웹캠 연결 및 실시간 영상 입력
- OpenCV를 이용한 얼굴 검출
- OpenVINO 모델을 이용한 연령 추정
- 여러 연령 추정값의 중앙값을 이용한 판정 안정화
- 60세 이상 사용자를 고령자로 판단
- 고령자 감지 시 ESP32-S3에 `S` 신호 전송
- 동일 사용자에게 신호가 반복 전송되는 현상 방지
- 사용자가 사라지면 이전 판정 정보 초기화
- ESP32-S3와 Serial 통신
- Arduino UNO와 Serial 통신
- ESP32-S3에서 `PAYMENT_COMPLETE` 메시지 수신
- 결제 완료 후 Arduino UNO에 `H` 명령 전달

즉, 웹캠 기반 고령자 인식과 ESP32-S3 및 Arduino UNO 사이의
통신을 담당하는 핵심 프로그램입니다.

---

### serial_test.py

Python과 ESP32-S3 사이의 Serial 통신이
정상적으로 이루어지는지 확인하기 위한 테스트 프로그램입니다.

ESP32-S3에 `S` 신호를 직접 전송하여
Simple Mode 전환 팝업이 정상적으로 실행되는지 확인할 때 사용합니다.

```text
Python
   ↓
S
   ↓
ESP32-S3
   ↓
Simple Mode Popup
```

메인 프로그램을 실행하기 전에
Serial 포트와 통신 상태를 확인할 때 사용할 수 있습니다.

---

### models/

OpenVINO를 이용한 연령 추정에 필요한
AI 모델 파일이 저장되어 있는 폴더입니다.

사용 모델은 다음과 같습니다.

```text
age-gender-recognition-retail-0013
```

#### age-gender-recognition-retail-0013.xml

OpenVINO 모델의 구조를 정의하는 파일입니다.

신경망의 레이어 구성과 입력 및 출력 정보 등
모델 실행에 필요한 구조 정보가 저장되어 있습니다.

#### age-gender-recognition-retail-0013.bin

OpenVINO 모델이 학습한 가중치 데이터가 저장되어 있는 파일입니다.

`.xml` 파일과 `.bin` 파일은 함께 사용되며,
둘 중 하나라도 없으면 연령 추정 모델을 정상적으로 불러올 수 없습니다.

```text
XML + BIN
    ↓
OpenVINO
    ↓
Age Estimation
```

---

## 전체 프로젝트에서의 역할

이 폴더는 전체 **Senior-Friendly-Kiosk** 프로젝트에서
웹캠 기반 고령자 인식과 장치 간 통신을 담당합니다.

```text
Webcam
   ↓
Python
   ↓
Elderly Detection
   ↓
ESP32-S3 Kiosk UI
```

또한 결제가 완료되면 Arduino 모터 제어 프로그램으로
하단 복귀 명령을 전달합니다.

```text
ESP32-S3
   ↓
Python
   ↓
Arduino UNO
   ↓
Motor Control
```
