# Cafe Kiosk Portrait V9 - VL53L0X Legacy I2C

이 버전은 V8에서 발생한 `driver_ng is not allowed to be used with this old driver` 문제를 피하기 위해
`driver/i2c_master.h`, `Wire.h`, `Adafruit_VL53L0X`를 모두 사용하지 않습니다.

- 외부 I2C 커넥터: SDA=GPIO8, SCL=GPIO9
- 보드 라이브러리가 이미 설치한 I2C_NUM_0 legacy driver를 그대로 사용
- 화면을 먼저 띄운 뒤 VL53L0X 초기화
- 센서 실패해도 화면은 유지
- 15cm 이하 감지 시 Simple Mode 팝업

연결:
VL53L0X VIN/VCC -> I2C VCC
VL53L0X GND -> I2C GND
VL53L0X SDA -> I2C SDA
VL53L0X SCL -> I2C SCL

주의:
V8 폴더는 쓰지 말고 이 V9 폴더 전체를 새로 열어 업로드하세요.

## V10 메뉴 이미지 적용본

이 폴더에서는 기존 `[Cup]`, `[Drink]`, `[Drink Image]` 문구를 실제 음료 이미지로 교체했습니다.
프로그램 저장 공간 초과를 막기 위해 원본 데이터는 메뉴당 128×128 RGB565로 최적화했습니다.

- `drink_images.h`: 16개 메뉴 이미지가 RGB565 형식으로 저장된 파일
- 일반 메뉴 목록: 약 70×70 크기로 표시
- 장바구니: 약 70×70 크기로 표시
- 수량 선택 화면: 160×160 크기로 표시
- 심플 모드: 약 80×80 크기로 표시
- 심플 모드 상세 화면: 약 120×120 크기로 표시

메뉴 이미지 순서는 `menuItems[]` 순서와 동일하므로 메뉴 배열의 순서를 바꾸면
`drinkImages[]` 순서도 함께 바꿔야 합니다.

Arduino IDE에서는 `Cafe_Kiosk_Menu_Images` 폴더 전체를 같은 위치에 둔 상태로
`Cafe_Kiosk_Menu_Images.ino`를 열어 업로드하세요. `drink_images.h`를 다른 폴더로
옮기면 컴파일할 때 파일을 찾지 못합니다.

이미지 데이터는 약 0.5MB의 플래시를 사용합니다. 그래도 업로드 용량 오류가 발생하면
ESP32-S3 보드의 Partition Scheme을 큰 APP 영역을 제공하는 설정으로 변경하세요.
