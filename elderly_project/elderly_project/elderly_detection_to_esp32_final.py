from __future__ import annotations

import time
from collections import deque
from pathlib import Path

import cv2
import numpy as np
import serial
from openvino import Core


# =========================================================
# 사용자 설정
# =========================================================

# Arduino 시리얼 모니터 테스트에 성공한 포트
ESP32_PORT = "COM8"
ARDUINO_PORT = "COM3"  # 장치 관리자에서 Arduino UNO 포트에 맞게 변경
BAUD_RATE = 115200

# ZIO C960 외장 웹캠
# 외장 웹캠이 안 열리면 1을 2로 바꿔보세요.
CAMERA_INDEX = 1

# 고령자 판단 기준
SENIOR_ENTER_AGE = 60
SENIOR_EXIT_AGE = 50

# 판정 안정화 설정
AGE_HISTORY_SIZE = 20
MIN_AGE_SAMPLES = 5
AGE_ESTIMATE_INTERVAL = 5
NO_FACE_RESET_FRAMES = 60

PROJECT_DIR = Path(__file__).resolve().parent
MODEL_XML = PROJECT_DIR / "models" / "age-gender-recognition-retail-0013.xml"
MODEL_BIN = PROJECT_DIR / "models" / "age-gender-recognition-retail-0013.bin"


def open_esp32() -> serial.Serial:
    port = serial.Serial()
    port.port = ESP32_PORT
    port.baudrate = BAUD_RATE
    port.timeout = 1
    port.dtr = False
    port.rts = False
    port.open()

    time.sleep(2)
    port.reset_input_buffer()

    print(f"[OK] ESP32 연결: {ESP32_PORT} / {BAUD_RATE}")
    return port


def open_arduino() -> serial.Serial:
    port = serial.Serial()
    port.port = ARDUINO_PORT
    port.baudrate = BAUD_RATE
    port.timeout = 0.05
    port.open()

    time.sleep(2)
    port.reset_input_buffer()

    print(f"[OK] Arduino 연결: {ARDUINO_PORT} / {BAUD_RATE}")
    return port


def relay_payment_command(
    esp32: serial.Serial,
    arduino: serial.Serial,
) -> None:
    """ESP32의 결제 완료 메시지를 Arduino 하단 복귀 명령으로 중계."""
    while esp32.in_waiting > 0:
        message = esp32.readline().decode("utf-8", errors="ignore").strip()

        if not message:
            continue

        print(f"[ESP32] {message}")

        if message == "PAYMENT_COMPLETE":
            arduino.write(b"H\n")
            arduino.flush()
            print("[RELAY] 결제 완료 -> Arduino 하단 복귀 H 전송")


def open_camera(index: int) -> cv2.VideoCapture:
    camera = cv2.VideoCapture(index, cv2.CAP_DSHOW)

    if not camera.isOpened():
        camera.release()
        camera = cv2.VideoCapture(index)

    if not camera.isOpened():
        raise RuntimeError(
            f"{index}번 카메라를 열 수 없습니다. "
            "CAMERA_INDEX를 0, 1, 2 중 다른 값으로 바꿔보세요."
        )

    camera.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
    camera.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
    camera.set(cv2.CAP_PROP_BUFFERSIZE, 1)

    print(f"[OK] 웹캠 연결: {index}번")
    return camera


def load_age_model():
    if not MODEL_XML.exists() or not MODEL_BIN.exists():
        raise FileNotFoundError(
            "나이 추정 모델 파일을 찾을 수 없습니다.\n"
            f"확인 위치:\n{MODEL_XML}\n{MODEL_BIN}"
        )

    core = Core()
    model = core.read_model(model=str(MODEL_XML), weights=str(MODEL_BIN))
    compiled_model = core.compile_model(model=model, device_name="CPU")

    input_layer = compiled_model.input(0)

    age_output = None
    for output in compiled_model.outputs:
        shape = [int(v) for v in output.shape]
        if int(np.prod(shape)) == 1:
            age_output = output
            break

    if age_output is None:
        raise RuntimeError("모델에서 나이 출력 레이어를 찾지 못했습니다.")

    print("[OK] OpenVINO 나이 추정 모델 로드")
    return compiled_model, input_layer, age_output


def estimate_age(
    face_bgr: np.ndarray,
    compiled_model,
    input_layer,
    age_output,
) -> float:
    face = cv2.resize(face_bgr, (62, 62))
    blob = face.transpose(2, 0, 1)
    blob = np.expand_dims(blob, axis=0).astype(np.float32)

    result = compiled_model({input_layer: blob})
    age_value = float(np.ravel(result[age_output])[0])

    return age_value * 100.0


def send_senior_command(esp32: serial.Serial) -> None:
    esp32.write(b"S")
    esp32.flush()
    print("[SEND] ESP32로 S 전송 → Simple Mode 팝업 요청")


def main() -> None:
    esp32 = None
    arduino = None
    camera = None

    try:
        esp32 = open_esp32()
        arduino = open_arduino()
        compiled_model, input_layer, age_output = load_age_model()
        camera = open_camera(CAMERA_INDEX)

        cascade_path = cv2.data.haarcascades + "haarcascade_frontalface_default.xml"
        face_detector = cv2.CascadeClassifier(cascade_path)

        if face_detector.empty():
            raise RuntimeError("OpenCV 얼굴 검출기를 불러오지 못했습니다.")

        age_history: deque[float] = deque(maxlen=AGE_HISTORY_SIZE)
        frame_count = 0
        no_face_frames = 0
        senior_command_sent = False
        smoothed_age: float | None = None

        print()
        print("웹캠 인식을 시작합니다.")
        print("고령자로 판정되면 ESP32에 S를 한 번 전송합니다.")
        print("종료하려면 웹캠 창에서 Q를 누르세요.")
        print()

        while True:
            # 결제 완료 메시지는 영상 처리와 동시에 계속 확인
            relay_payment_command(esp32, arduino)

            ok, frame = camera.read()

            if not ok:
                print("[WARN] 웹캠 프레임을 읽지 못했습니다.")
                time.sleep(0.05)
                continue

            frame_count += 1
            frame = cv2.flip(frame, 1)

            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            gray = cv2.equalizeHist(gray)

            faces = face_detector.detectMultiScale(
                gray,
                scaleFactor=1.1,
                minNeighbors=5,
                minSize=(100, 100),
            )

            if len(faces) == 0:
                no_face_frames += 1

                if no_face_frames >= NO_FACE_RESET_FRAMES:
                    age_history.clear()
                    smoothed_age = None
                    senior_command_sent = False
                    no_face_frames = 0
                    print("[RESET] 얼굴이 없어 다음 사용자를 기다립니다.")

            else:
                no_face_frames = 0

                x, y, w, h = max(faces, key=lambda item: item[2] * item[3])

                pad_x = int(w * 0.15)
                pad_y = int(h * 0.15)

                x1 = max(0, x - pad_x)
                y1 = max(0, y - pad_y)
                x2 = min(frame.shape[1], x + w + pad_x)
                y2 = min(frame.shape[0], y + h + pad_y)

                face_crop = frame[y1:y2, x1:x2]

                if frame_count % AGE_ESTIMATE_INTERVAL == 0 and face_crop.size > 0:
                    estimated_age = estimate_age(
                        face_crop,
                        compiled_model,
                        input_layer,
                        age_output,
                    )
                    age_history.append(estimated_age)

                    if len(age_history) >= MIN_AGE_SAMPLES:
                        smoothed_age = float(np.median(age_history))

                        if (
                            smoothed_age >= SENIOR_ENTER_AGE
                            and not senior_command_sent
                        ):
                            send_senior_command(esp32)
                            senior_command_sent = True

                        elif smoothed_age <= SENIOR_EXIT_AGE:
                            senior_command_sent = False

                if smoothed_age is None:
                    status_text = "Measuring age..."
                    box_color = (0, 255, 255)
                elif smoothed_age >= SENIOR_ENTER_AGE:
                    status_text = f"SENIOR / age {smoothed_age:.1f}"
                    box_color = (0, 0, 255)
                else:
                    status_text = f"NORMAL / age {smoothed_age:.1f}"
                    box_color = (0, 255, 0)

                cv2.rectangle(frame, (x, y), (x + w, y + h), box_color, 3)
                cv2.putText(
                    frame,
                    status_text,
                    (x, max(35, y - 12)),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.9,
                    box_color,
                    2,
                    cv2.LINE_AA,
                )

            cv2.putText(
                frame,
                "Q: quit",
                (20, 40),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.8,
                (255, 255, 255),
                2,
                cv2.LINE_AA,
            )

            cv2.imshow("Elderly Detection to ESP32", frame)

            if cv2.waitKey(1) & 0xFF in (ord("q"), ord("Q")):
                break

    except serial.SerialException as error:
        print()
        print("[ERROR] ESP32 시리얼 연결 실패")
        print(error)
        print("Arduino 시리얼 모니터가 열려 있으면 닫고 다시 실행하세요.")

    except Exception as error:
        print()
        print("[ERROR]", error)

    finally:
        if camera is not None:
            camera.release()

        cv2.destroyAllWindows()

        if esp32 is not None and esp32.is_open:
            esp32.close()

        if arduino is not None and arduino.is_open:
            arduino.close()

        print("프로그램을 종료했습니다.")


if __name__ == "__main__":
    main()
