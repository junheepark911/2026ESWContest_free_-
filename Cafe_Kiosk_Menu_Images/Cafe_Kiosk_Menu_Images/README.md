# Cafe Kiosk Menu UI

ESP32-S3 Touch LCD 7을 이용하여 제작한
**고령자 친화형 카페 키오스크 사용자 인터페이스**입니다.

LVGL 기반으로 메뉴 선택, 수량 조절, 장바구니, 결제 등의
일반적인 키오스크 주문 기능을 구현하였으며,
일반 사용자를 위한 **Normal Mode**와
고령자를 위한 **Simple Mode**를 제공합니다.

Python 기반 고령자 인식 프로그램과 Serial 통신하여
고령자로 판단된 경우 Simple Mode 전환 안내를 표시합니다.

---

## 주요 기능

- ESP32-S3 7인치 터치 LCD 기반 키오스크 UI
- 480 × 800 세로형 화면 구성
- LVGL v8 기반 터치 인터페이스
- 매장 이용 / 포장 주문 선택
- 메뉴 카테고리 선택
- 음료 메뉴 이미지 표시
- 메뉴 상세 화면
- 수량 증가 / 감소
- 장바구니 기능
- 주문 금액 자동 계산
- 결제 방법 선택
- 결제 완료 화면
- Normal Mode 지원
- Simple Mode 지원
- Python 고령자 인식 프로그램과 Serial 통신
- 결제 완료 후 외부 제어 시스템과 연동

---

## 동작 과정

키오스크는 전원이 켜지면
기본적으로 **Normal Mode**로 시작합니다.

```text
전원 ON
   ↓
ESP32-S3
   ↓
Normal Mode
   ↓
매장 / 포장 선택
   ↓
메뉴 선택
   ↓
수량 선택
   ↓
장바구니
   ↓
결제
   ↓
주문 완료
```

결제가 완료되면 주문 완료 화면을 표시한 후
다시 초기 화면으로 복귀합니다.

---

## 화면 구성

현재 키오스크 UI는 다음 해상도를 기준으로 제작되었습니다.

```cpp
static const int SCREEN_W = 480;
static const int SCREEN_H = 800;
```

따라서 **480 × 800 세로형 터치 화면**을 사용합니다.

화면은 LVGL을 이용하여 버튼, 텍스트,
메뉴 카드 및 장바구니 등의 UI 요소를 구성합니다.

---

## 메뉴 구성

총 4개의 메뉴 카테고리를 제공합니다.

```text
Coffee
Ade
Tea
Shake
```



---

## 메뉴 이미지

음료 이미지는 `drink_images.h` 파일에 저장되어 있습니다.

이미지는 ESP32-S3의 LCD에서 직접 출력할 수 있도록
임베디드 환경에 맞는 이미지 데이터 형태로 변환하여 사용합니다.

```text
drink_images.h
   ↓
Menu Image Data
   ↓
ESP32-S3
   ↓
LCD 화면 출력
```

메뉴 화면 및 메뉴 상세 화면에서
각 음료에 해당하는 이미지를 표시합니다.

---

## Normal Mode

Normal Mode는 일반적인 카페 키오스크 주문 방식을 제공합니다.

```text
Normal Mode
   ↓
Eat In / Take Out
   ↓
Menu Category
   ↓
Menu
   ↓
Quantity
   ↓
Cart
   ↓
Payment
   ↓
Order Complete
```

사용자는 Coffee, Ade, Tea, Shake 중 원하는 카테고리를 선택한 뒤
음료와 수량을 선택할 수 있습니다.

선택한 메뉴는 장바구니에 저장되며
전체 주문 금액이 자동으로 계산됩니다.

---

## Simple Mode

Simple Mode는 고령자가 보다 쉽게 키오스크를 사용할 수 있도록
일반 모드보다 단순한 화면 구성과 큰 버튼을 제공하는 모드입니다.

Python 프로그램이 웹캠을 통해 사용자를 고령자로 판단하면
ESP32-S3에 다음 신호를 전송합니다.

```text
S
```

ESP32-S3가 `S` 신호를 수신하면
Simple Mode 전환 확인 팝업을 표시합니다.

```text
Webcam
   ↓
Python
   ↓
고령자 판정
   ↓
S
   ↓
ESP32-S3
   ↓
Simple Mode Popup
```

사용자가 팝업에서 전환을 선택하면
Simple Mode 주문 화면으로 이동합니다.

---

## Serial Communication

Python 프로그램과 ESP32-S3는
USB Serial 통신을 이용하여 데이터를 주고받습니다.

통신 속도는 다음과 같습니다.

```text
115200 baud
```

### Python → ESP32-S3

| 명령 | 기능 |
|---|---|
| `S` | Simple Mode 전환 안내 팝업 표시 |
| `N` | Normal Mode로 복귀 |

고령자가 감지되면 Python 프로그램에서 `S` 신호를 전송하여
키오스크가 Simple Mode 전환 여부를 사용자에게 안내합니다.

---

## 결제 기능

사용자는 장바구니에서 주문 내용을 확인한 후
결제 화면으로 이동할 수 있습니다.

키오스크에서는 다음과 같은 결제 방식 UI를 제공합니다.

```text
Credit Card
Easy Pay
Cash
Coupon
```

결제 완료 후에는 주문 완료 화면이 표시됩니다.

```text
Payment
   ↓
Payment Complete
   ↓
Order Complete
   ↓
Home
```

전체 프로젝트에서는 결제 완료 상태를
Python 및 Arduino 모터 제어 시스템과 연동하여
키오스크를 초기 위치로 복귀시키는 데 사용합니다.

```text
ESP32-S3
   ↓
Payment Complete
   ↓
Python
   ↓
Arduino UNO
   ↓
Motor Return
```

---

## 거리센서 설정

코드에는 VL53L0X 거리센서를 사용할 수 있는 기능이 포함되어 있지만,
현재 프로젝트에서는 비활성화되어 있습니다.

```cpp
#define USE_VL53L0X_SENSOR 0
```

현재 Simple Mode 판단은 거리센서가 아니라
**노트북의 웹캠과 Python 고령자 인식 프로그램**을 이용합니다.

```text
Webcam
   ↓
Python
   ↓
Age Detection
   ↓
ESP32-S3
```

따라서 VL53L0X 거리센서는
현재 ESP32-S3의 Simple Mode 판정에는 사용하지 않습니다.

---

## 사용 라이브러리

- Arduino
- LVGL v8
- ESP32 Display Panel
- ESP32 Arduino Core

주요 헤더는 다음과 같습니다.

```cpp
#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
```

LVGL은 키오스크 화면 구성과
터치 버튼 이벤트 처리를 담당합니다.

ESP32 Display Panel 관련 라이브러리는
ESP32-S3 Touch LCD의 디스플레이와 터치 기능을 제어하는 데 사용합니다.

---

## 파일 구성

```text
Cafe_Kiosk_Menu_Images/
│
├── Cafe_Kiosk_Menu_Images.ino
├── drink_images.h
├── esp_panel_board_custom_conf.h
├── lvgl_v8_port.cpp
├── lvgl_v8_port.h
└── README.md
```

### Cafe_Kiosk_Menu_Images.ino

카페 키오스크 시스템의 메인 ESP32-S3 프로그램입니다.

다음 기능을 담당합니다.

- ESP32-S3 Touch LCD 초기화
- LVGL 기반 키오스크 화면 구성
- Normal Mode / Simple Mode 관리
- 매장 / 포장 선택
- 메뉴 카테고리 선택
- 메뉴 선택
- 수량 조절
- 장바구니 관리
- 주문 금액 계산
- 결제 화면
- 주문 완료 화면
- Python과 Serial 통신
- 고령자 감지 신호 수신 및 Simple Mode 전환

즉, 사용자가 직접 조작하는
키오스크 UI 전체 동작을 담당하는 핵심 프로그램입니다.

---

### drink_images.h

키오스크 메뉴 화면에서 사용하는
음료 이미지 데이터가 저장되어 있는 헤더 파일입니다.

```text
drink_images.h
   ↓
ESP32-S3
   ↓
LVGL
   ↓
음료 이미지 출력
```

메인 `.ino` 파일에서 해당 이미지 데이터를 불러와
각 메뉴에 맞는 이미지를 LCD 화면에 표시합니다.

---

### esp_panel_board_custom_conf.h

ESP32-S3 Touch LCD 7의
디스플레이 및 터치 하드웨어 설정을 담당하는 파일입니다.

보드에서 사용하는 LCD 크기와 인터페이스,
터치 관련 설정 등이 포함되어 있으며
ESP32 Display Panel 라이브러리에서 사용합니다.

---

### lvgl_v8_port.cpp

LVGL과 ESP32-S3 디스플레이를 연결하기 위한
포팅 기능을 구현한 파일입니다.

다음과 같은 역할을 담당합니다.

- LVGL 초기화
- LCD 화면 출력 연결
- 디스플레이 버퍼 처리
- 터치 입력 처리
- LVGL 화면 갱신

---

### lvgl_v8_port.h

`lvgl_v8_port.cpp`에서 구현된
LVGL 포팅 관련 함수와 설정을 선언하는 헤더 파일입니다.

메인 `.ino` 프로그램에서 해당 파일을 불러와
LVGL 기능을 사용할 수 있도록 연결합니다.

---

## 전체 프로젝트에서의 역할

이 폴더는 전체 **Senior-Friendly-Kiosk** 프로젝트에서
사용자가 직접 조작하는 키오스크 화면과
주문 및 결제 기능을 담당합니다.

```text
Webcam
   ↓
Python Elderly Detection
   ↓
S
   ↓
ESP32-S3
   ↓
Normal / Simple Mode
   ↓
Menu
   ↓
Cart
   ↓
Payment
```

즉, Python의 고령자 인식 결과를 전달받아
사용자에게 적합한 UI를 제공하고,
주문 및 결제 과정을 처리하는
ESP32-S3 기반 사용자 인터페이스 프로그램입니다.
