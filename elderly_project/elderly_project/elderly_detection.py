from collections import deque
from pathlib import Path

import cv2
import numpy as np
from openvino import Core


# =========================================================
# 설정값
# =========================================================

# 0: 노트북 내장 카메라
# 1: 첫 번째 외장 웹캠
# 2: 두 번째 외장 웹캠
CAMERA_INDEX = 1

# 노인 전용 모드로 전환하는 추정 나이 기준
SENIOR_ENTER_AGE = 60

# 노인 전용 모드에서 일반 모드로 돌아가는 기준
SENIOR_EXIT_AGE = 50

# 최근 나이 추정값 저장 개수
HISTORY_SIZE = 20

# 최소 측정 횟수
MIN_SAMPLES = 5

# 몇 프레임마다 나이를 추정할지
AGE_CHECK_INTERVAL = 5

# 얼굴이 사라진 상태가 몇 프레임 지속되면 초기화할지
NO_FACE_RESET_FRAMES = 60


# =========================================================
# 모델 파일 경로
# =========================================================

BASE_DIR = Path(__file__).resolve().parent

MODEL_XML = (
    BASE_DIR
    / "models"
    / "age-gender-recognition-retail-0013.xml"
)

MODEL_BIN = (
    BASE_DIR
    / "models"
    / "age-gender-recognition-retail-0013.bin"
)


# =========================================================
# 키오스크 모드 전환 함수
# =========================================================

def activate_senior_mode():
    """노인 전용 키오스크 모드로 전환합니다."""

    print()
    print("=" * 50)
    print("노인 전용 키오스크 모드로 전환합니다.")
    print("큰 글씨, 큰 버튼, 단순 메뉴를 표시합니다.")
    print("=" * 50)
    print()


def activate_normal_mode():
    """일반 키오스크 모드로 전환합니다."""

    print()
    print("=" * 50)
    print("일반 키오스크 모드로 전환합니다.")
    print("=" * 50)
    print()


# =========================================================
# OpenVINO 나이 추정 모델 불러오기
# =========================================================

def load_age_model():
    """OpenVINO 나이 추정 모델을 불러옵니다."""

    if not MODEL_XML.exists():
        raise FileNotFoundError(
            f"XML 모델 파일이 없습니다:\n{MODEL_XML}"
        )

    if not MODEL_BIN.exists():
        raise FileNotFoundError(
            f"BIN 모델 파일이 없습니다:\n{MODEL_BIN}"
        )

    core = Core()

    model = core.read_model(
        model=str(MODEL_XML),
        weights=str(MODEL_BIN)
    )

    compiled_model = core.compile_model(
        model=model,
        device_name="CPU"
    )

    return compiled_model


# =========================================================
# 얼굴에서 나이 추정
# =========================================================

def estimate_age(face_image, compiled_model):
    """잘라낸 얼굴 이미지에서 나이를 추정합니다."""

    if face_image is None or face_image.size == 0:
        return None

    # 모델 입력 크기: 62 × 62
    resized_face = cv2.resize(
        face_image,
        (62, 62)
    )

    # 이미지 형태를 HWC에서 NCHW로 변경
    input_tensor = resized_face.transpose(2, 0, 1)

    input_tensor = np.expand_dims(
        input_tensor,
        axis=0
    ).astype(np.float32)

    results = compiled_model([input_tensor])

    # 출력 중 값이 하나인 결과가 나이 출력
    for output in results.values():
        values = np.asarray(output).reshape(-1)

        if values.size == 1:
            estimated_age = float(values[0]) * 100

            return max(
                0,
                min(estimated_age, 100)
            )

    return None


# =========================================================
# 외장 웹캠 열기
# =========================================================

def open_camera():
    """설정한 번호의 웹캠을 엽니다."""

    print(f"{CAMERA_INDEX}번 카메라 연결을 시도합니다.")

    camera = cv2.VideoCapture(
        CAMERA_INDEX,
        cv2.CAP_DSHOW
    )

    # DirectShow로 열리지 않으면 기본 방식으로 재시도
    if not camera.isOpened():
        camera.release()

        camera = cv2.VideoCapture(
            CAMERA_INDEX
        )

    if not camera.isOpened():
        raise RuntimeError(
            f"{CAMERA_INDEX}번 카메라를 열 수 없습니다."
        )

    # 해상도 설정
    camera.set(
        cv2.CAP_PROP_FRAME_WIDTH,
        640
    )

    camera.set(
        cv2.CAP_PROP_FRAME_HEIGHT,
        480
    )

    # 카메라 버퍼를 줄여 화면 지연 최소화
    camera.set(
        cv2.CAP_PROP_BUFFERSIZE,
        1
    )

    print(f"{CAMERA_INDEX}번 카메라가 연결되었습니다.")

    return camera


# =========================================================
# 메인 프로그램
# =========================================================

def main():
    try:
        age_model = load_age_model()
        print("나이 추정 모델을 정상적으로 불러왔습니다.")

    except Exception as error:
        print("나이 추정 모델을 불러오지 못했습니다.")
        print(error)
        return

    # OpenCV 기본 얼굴 검출기
    face_model_path = (
        cv2.data.haarcascades
        + "haarcascade_frontalface_default.xml"
    )

    face_detector = cv2.CascadeClassifier(
        face_model_path
    )

    if face_detector.empty():
        print("얼굴 검출 모델을 불러오지 못했습니다.")
        return

    try:
        camera = open_camera()

    except RuntimeError as error:
        print(error)
        print()
        print("외장 웹캠이 1번이 아닐 수 있습니다.")
        print("코드의 CAMERA_INDEX를 2로 바꿔보세요.")
        return

    # 최근 나이 결과 저장
    age_history = deque(
        maxlen=HISTORY_SIZE
    )

    frame_count = 0
    no_face_count = 0

    smoothed_age = None

    # None, normal, senior 중 하나
    kiosk_mode = None

    print()
    print("프로그램이 실행되었습니다.")
    print(f"사용 중인 카메라 번호: {CAMERA_INDEX}")
    print(f"노인 전용 모드 기준: {SENIOR_ENTER_AGE}세 이상")
    print("종료하려면 웹캠 창을 클릭한 후 Q를 누르세요.")
    print()

    try:
        while True:
            success, frame = camera.read()

            if not success:
                print("웹캠 영상을 읽지 못했습니다.")
                break

            frame_count += 1

            # 화면을 거울처럼 좌우 반전
            frame = cv2.flip(
                frame,
                1
            )

            # 얼굴 검출용 흑백 영상
            gray = cv2.cvtColor(
                frame,
                cv2.COLOR_BGR2GRAY
            )

            # 조명 차이 보정
            gray = cv2.equalizeHist(
                gray
            )

            faces = face_detector.detectMultiScale(
                gray,
                scaleFactor=1.1,
                minNeighbors=6,
                minSize=(100, 100)
            )

            # =================================================
            # 얼굴이 없는 경우
            # =================================================

            if len(faces) == 0:
                no_face_count += 1

                cv2.putText(
                    frame,
                    "NO FACE",
                    (20, 40),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.8,
                    (0, 255, 255),
                    2,
                    cv2.LINE_AA
                )

                # 일정 시간 동안 얼굴이 보이지 않으면 초기화
                if no_face_count >= NO_FACE_RESET_FRAMES:
                    if kiosk_mode == "senior":
                        activate_normal_mode()

                    age_history.clear()
                    smoothed_age = None
                    kiosk_mode = None
                    no_face_count = 0

            # =================================================
            # 얼굴이 있는 경우
            # =================================================

            else:
                no_face_count = 0

                # 여러 얼굴 중 가장 큰 얼굴 하나를 선택
                x, y, width, height = max(
                    faces,
                    key=lambda face: face[2] * face[3]
                )

                padding = 15

                x1 = max(
                    0,
                    x - padding
                )

                y1 = max(
                    0,
                    y - padding
                )

                x2 = min(
                    frame.shape[1],
                    x + width + padding
                )

                y2 = min(
                    frame.shape[0],
                    y + height + padding
                )

                face_image = frame[
                    y1:y2,
                    x1:x2
                ]

                # 일정한 프레임 간격으로만 나이를 추정
                if frame_count % AGE_CHECK_INTERVAL == 0:
                    estimated_age = estimate_age(
                        face_image,
                        age_model
                    )

                    if estimated_age is not None:
                        age_history.append(
                            estimated_age
                        )

                        # 튀는 값의 영향을 줄이기 위해 중앙값 사용
                        smoothed_age = float(
                            np.median(age_history)
                        )

                        # 최소 측정 횟수 이후에만 판정
                        if len(age_history) >= MIN_SAMPLES:
                            previous_mode = kiosk_mode

                            # 최초 모드 판정
                            if kiosk_mode is None:
                                if smoothed_age >= SENIOR_ENTER_AGE:
                                    kiosk_mode = "senior"
                                else:
                                    kiosk_mode = "normal"

                            # 일반 모드에서 노인 모드로 전환
                            elif (
                                kiosk_mode == "normal"
                                and smoothed_age >= SENIOR_ENTER_AGE
                            ):
                                kiosk_mode = "senior"

                            # 노인 모드에서 일반 모드로 복귀
                            elif (
                                kiosk_mode == "senior"
                                and smoothed_age <= SENIOR_EXIT_AGE
                            ):
                                kiosk_mode = "normal"

                            # 모드가 실제로 변경됐을 때만 실행
                            if kiosk_mode != previous_mode:
                                if kiosk_mode == "senior":
                                    activate_senior_mode()

                                elif kiosk_mode == "normal":
                                    activate_normal_mode()

                # =================================================
                # 화면에 결과 표시
                # =================================================

                if len(age_history) < MIN_SAMPLES:
                    label = (
                        f"MEASURING "
                        f"{len(age_history)}/{MIN_SAMPLES}"
                    )

                    box_color = (
                        0,
                        255,
                        255
                    )

                elif kiosk_mode == "senior":
                    label = (
                        f"SENIOR MODE / EST: "
                        f"{smoothed_age:.0f}"
                    )

                    # 빨간색
                    box_color = (
                        0,
                        0,
                        255
                    )

                else:
                    label = (
                        f"NORMAL MODE / EST: "
                        f"{smoothed_age:.0f}"
                    )

                    # 초록색
                    box_color = (
                        0,
                        255,
                        0
                    )

                cv2.rectangle(
                    frame,
                    (x1, y1),
                    (x2, y2),
                    box_color,
                    3
                )

                cv2.putText(
                    frame,
                    label,
                    (x1, max(30, y1 - 10)),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.65,
                    box_color,
                    2,
                    cv2.LINE_AA
                )

                sample_label = (
                    f"Samples: "
                    f"{len(age_history)}/{HISTORY_SIZE}"
                )

                cv2.putText(
                    frame,
                    sample_label,
                    (20, 40),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.65,
                    (255, 255, 255),
                    2,
                    cv2.LINE_AA
                )

                camera_label = (
                    f"Camera: {CAMERA_INDEX}"
                )

                cv2.putText(
                    frame,
                    camera_label,
                    (20, 70),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.65,
                    (255, 255, 255),
                    2,
                    cv2.LINE_AA
                )

            cv2.imshow(
                "Senior Kiosk Detection",
                frame
            )

            key = cv2.waitKey(1) & 0xFF

            if key == ord("q"):
                break

    except KeyboardInterrupt:
        print("프로그램을 종료합니다.")

    finally:
        camera.release()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()