# Cafe Kiosk Menu UI

ESP32-S3 Touch LCD를 이용하여 제작한
고령자 친화형 카페 키오스크 UI입니다.

일반 사용자를 위한 Normal Mode와
고령자를 위한 Simple Mode를 제공하며,
Python 기반 고령자 인식 프로그램과 Serial 통신을 통해
화면 모드를 전환합니다.

---

## 주요 기능

- ESP32-S3 7인치 터치 LCD 기반 키오스크 UI
- LVGL 기반 화면 구성
- 카페 메뉴 선택
- 메뉴별 음료 이미지 표시
- 수량 선택
- 장바구니 기능
- 주문 금액 계산
- 결제 화면
- 주문 완료 화면
- Normal Mode / Simple Mode 지원
- Python 고령자 인식 프로그램과 Serial 통신
- 결제 완료 신호 전송

---

## 메뉴 구성

총 4개의 메뉴 카테고리를 제공합니다.

- Coffee
- Ade
- Tea
- Shake

각 카테고리 안에는 여러 음료가 등록되어 있으며,
전체 음료 항목은 총 16개입니다.

메뉴의 이름, 가격, 카테고리 등의 정보는
메인 코드의 메뉴 데이터에 저장되어 있습니다.

---

## 메뉴 이미지

각 음료 이미지는 `drink_images.h` 파일에 저장되어 있습니다.

ESP32에서 표시할 수 있도록
RGB565 형식의 이미지 데이터로 변환하여 사용합니다.

---

## Normal Mode

일반적인 카페 키오스크 주문 과정을 제공합니다.

```text
Home
 ↓
Menu
 ↓
Menu Detail
 ↓
Cart
 ↓
Payment
 ↓
Order Complete
 ↓
Home
