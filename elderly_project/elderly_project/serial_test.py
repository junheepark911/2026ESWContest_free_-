import time
import serial

# 시리얼 모니터 테스트에 성공했던 COM 번호로 변경
ESP32_PORT = "COM3"
BAUD_RATE = 115200

esp32 = serial.Serial()

try:
    esp32.port = ESP32_PORT
    esp32.baudrate = BAUD_RATE
    esp32.timeout = 1

    # 연결 시 불필요한 자동 리셋을 줄이기 위한 설정
    esp32.dtr = False
    esp32.rts = False

    esp32.open()

    print(f"ESP32 연결 성공: {ESP32_PORT}")
    print("LCD가 NORMAL MODE가 될 때까지 잠시 기다립니다.")

    time.sleep(3)

    esp32.reset_input_buffer()
    esp32.write(b"S")
    esp32.flush()

    print("ESP32로 S 전송 완료")

    time.sleep(2)

    while esp32.in_waiting:
        response = esp32.readline().decode(
            "utf-8", errors="replace"
        ).strip()

        if response:
            print("ESP32 응답:", response)

except serial.SerialException as error:
    print("시리얼 연결 오류:", error)

finally:
    if esp32.is_open:
        esp32.close()

    print("시리얼 포트를 닫았습니다.")