# Elderly Detection

웹캠 영상을 이용하여 사용자의 얼굴을 검출하고,
OpenVINO 기반 연령 추정 모델을 통해 고령자 여부를 판단하는 Python 프로그램입니다.

고령자로 판단되면 ESP32-S3 키오스크에 `S` 신호를 전송하여
Simple Mode 전환 안내를 표시합니다.

또한 ESP32에서 결제 완료 메시지를 수신하면
Arduino에 모터 하단 복귀 명령을 전달합니다.

---

## 주요 기능

- USB 웹캠 영상 입력
- OpenCV 기반 얼굴 검출
- OpenVINO 기반 연령 추정
- 여러 번의 연령 추정값을 이용한 판정 안정화
- 고령자 감지 시 ESP32에 `S` 신호 전송
- 얼굴이 사라지면 다음 사용자를 위한 판정 초기화
- ESP32와 Serial 통신
- Arduino와 Serial 통신
- 결제 완료 후 Arduino에 모터 복귀 명령 전달

---

## 동작 과정

```text
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
   ↓
ESP32-S3

---

## 사용 라이브러리

- OpenCV
- OpenVINO
- NumPy
- PySerial
