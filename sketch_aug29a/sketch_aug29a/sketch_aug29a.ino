#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <SoftwareSerial.h>

Adafruit_VL53L0X tof;

// =================================================
// ESP32 통신
// =================================================

// ESP32 TXD → Arduino D6
// ESP32 GND → Arduino GND
// Arduino D7은 연결하지 않음
SoftwareSerial esp32Serial(6, 7);

// =================================================
// 모터 및 리미트 스위치 핀
// =================================================

const byte dirPin  = 2;
const byte stepPin = 3;

const byte bottomLimitPin = 4;
const byte topLimitPin    = 5;

// =================================================
// 모터 방향
// 실제 방향이 반대면 HIGH와 LOW를 서로 교체
// =================================================

const byte UP_DIRECTION   = HIGH;
const byte DOWN_DIRECTION = LOW;

// =================================================
// 거리센서 설정
// =================================================

// 120cm 이내이면 사용자 감지
const int DETECT_DISTANCE_MM = 1200;

// 5mm보다 가까운 측정값 제외
const int MIN_DISTANCE_MM = 5;

// 5번 측정
const byte MEASUREMENT_COUNT = 5;

// 5번 중 3번 이상이면 감지
const byte REQUIRED_DETECTION_COUNT = 3;

// =================================================
// 모터 이동 설정
// =================================================

// 숫자가 작을수록 빠름
const unsigned int STEP_DELAY_US = 70;

// 실제 이동거리에 맞게 조정
const long STEPS_PER_CM = 2000;

// 자동 높이 조절 거리
const long UP_DISTANCE_CM = 10;
const long DOWN_DISTANCE_CM = 3;

// 현재 계산 위치
long currentPositionSteps = 0;

// 센서 측정 대기시간
const unsigned long WAIT_TIME_MS = 100;

// 하단 복귀 최대 시간
const unsigned long HOMING_TIMEOUT_MS = 30000;

// =================================================
// 동작 상태
// =================================================

enum KioskState
{
  WAITING_AT_BOTTOM,
  MOVING_UP,
  SEARCHING_DOWN,
  HOLDING_POSITION
};

KioskState state = WAITING_AT_BOTTOM;

// 결제 후 기존 사용자가 떠날 때까지 재상승 방지
bool waitingForUserToLeave = false;

// =================================================
// 초기 설정
// =================================================

void setup()
{
  // 컴퓨터 시리얼 모니터
  Serial.begin(115200);

  // ESP32와 Arduino 통신
  esp32Serial.begin(9600);

  pinMode(dirPin, OUTPUT);
  pinMode(stepPin, OUTPUT);

  // 리미트 스위치
  // COM → GND
  // 하단 NO → D4
  // 상단 NO → D5
  pinMode(bottomLimitPin, INPUT_PULLUP);
  pinMode(topLimitPin, INPUT_PULLUP);

  digitalWrite(stepPin, LOW);

  Serial.println();
  Serial.println(F("============================"));
  Serial.println(F("키오스크 모터 제어 시작"));
  Serial.println(F("============================"));
  Serial.println(F("ESP32 결제 완료 신호 H 대기"));
  Serial.println(F("시작 위치를 하단으로 이동"));

  // 시작할 때 하단 리미트까지 이동
  moveToBottomAtStartup();

  // 거리센서 시작
  Wire.begin();

  if (!tof.begin())
  {
    Serial.println(F("VL53L0X 연결 오류"));

    while (1)
    {
      delay(100);
    }
  }

  currentPositionSteps = 0;
  state = WAITING_AT_BOTTOM;

  Serial.println(F("하단 원점 설정 완료"));
  Serial.println(F("사용자 감지 시작"));
}

// =================================================
// 반복 동작
// =================================================

void loop()
{
  // -------------------------------------------------
  // 결제 완료 신호를 가장 먼저 확인
  // -------------------------------------------------

  if (paymentCommandReceived())
  {
    Serial.println();
    Serial.println(F("============================"));
    Serial.println(F("결제 완료 신호 H 수신"));
    Serial.println(F("하단 리미트까지 한 번에 하강"));
    Serial.println(F("============================"));

    // 하단 리미트까지 연속 하강
    moveToBottomAfterPayment();

    currentPositionSteps = 0;
    state = WAITING_AT_BOTTOM;

    // 결제한 사용자가 앞에 있어도 다시 상승하지 않음
    waitingForUserToLeave = true;

    Serial.println(F("결제 후 하단 복귀 동작 종료"));
    Serial.println(F("기존 사용자가 떠날 때까지 대기"));

    delay(100);
    return;
  }

  // 거리센서로 사용자 확인
  bool detected = detectUser();

  // -------------------------------------------------
  // 결제 후 기존 사용자가 떠날 때까지 대기
  // -------------------------------------------------

  if (waitingForUserToLeave)
  {
    if (detected)
    {
      Serial.println(F("기존 사용자가 아직 감지됨"));
      Serial.println(F("최하단에서 모터 정지"));
    }
    else
    {
      Serial.println(F("기존 사용자 이탈 확인"));
      Serial.println(F("새로운 사용자 감지 시작"));

      waitingForUserToLeave = false;
    }

    delay(WAIT_TIME_MS);
    return;
  }

  // -------------------------------------------------
  // 기존 자동 높이 조절 동작
  // -------------------------------------------------

  switch (state)
  {
    case WAITING_AT_BOTTOM:

      if (detected)
      {
        Serial.println(F("사용자 감지 → 10cm 상승"));

        state = MOVING_UP;

        if (!moveUpCm(UP_DISTANCE_CM))
        {
          state = HOLDING_POSITION;
        }
      }
      else
      {
        Serial.println(F("최하단에서 사용자 대기"));
      }

      break;

    case MOVING_UP:

      if (detected)
      {
        Serial.println(F("계속 감지됨 → 다시 10cm 상승"));

        if (!moveUpCm(UP_DISTANCE_CM))
        {
          Serial.println(F("상단 리미트 도착 → 상승 정지"));

          state = HOLDING_POSITION;
        }
      }
      else
      {
        Serial.println(F("사용자를 놓침 → 3cm 하강"));

        if (moveDownCm(DOWN_DISTANCE_CM))
        {
          state = SEARCHING_DOWN;
        }
        else
        {
          state = WAITING_AT_BOTTOM;
        }
      }

      break;

    case SEARCHING_DOWN:

      if (detected)
      {
        Serial.println(F("사용자 재감지 → 현재 위치 정지"));

        state = HOLDING_POSITION;
      }
      else
      {
        Serial.println(F("사용자 미감지 → 다시 3cm 하강"));

        if (!moveDownCm(DOWN_DISTANCE_CM))
        {
          Serial.println(F("하단 리미트 도착"));

          state = WAITING_AT_BOTTOM;
        }
      }

      break;

    case HOLDING_POSITION:

      if (detected)
      {
        Serial.println(F("사용자 감지 중 → 모터 정지"));
      }
      else
      {
        Serial.println(F("사용자 미감지 → 3cm 하강"));

        if (moveDownCm(DOWN_DISTANCE_CM))
        {
          state = SEARCHING_DOWN;
        }
        else
        {
          Serial.println(F("하단 리미트 도착"));

          state = WAITING_AT_BOTTOM;
        }
      }

      break;
  }

  delay(WAIT_TIME_MS);
}

// =================================================
// ESP32 결제 완료 명령 확인
// H 또는 h를 받으면 true
// 여러 번 전송된 H도 모두 읽어서 비움
// =================================================

bool paymentCommandReceived()
{
  bool received = false;

  while (esp32Serial.available() > 0)
  {
    char command = esp32Serial.read();

    Serial.print(F("ESP32 수신 문자: "));
    Serial.println(command);

    if (command == 'H' || command == 'h')
    {
      received = true;
    }
  }

  return received;
}

// =================================================
// 전원을 켰을 때 하단 리미트까지 이동
// =================================================

void moveToBottomAtStartup()
{
  if (bottomLimitPressed())
  {
    currentPositionSteps = 0;

    Serial.println(F("이미 하단 리미트 위치입니다."));
    return;
  }

  digitalWrite(dirPin, DOWN_DIRECTION);
  delayMicroseconds(500);

  unsigned long startTime = millis();

  while (!bottomLimitPressed())
  {
    makeStep();

    if (millis() - startTime >= HOMING_TIMEOUT_MS)
    {
      Serial.println(F("시작 원점 탐색 실패"));
      Serial.println(F("D4 결선과 모터 방향을 확인하세요."));

      while (1)
      {
        delay(100);
      }
    }
  }

  currentPositionSteps = 0;

  Serial.println(F("하단 리미트 감지 완료"));
  delay(100);
}

// =================================================
// 결제 완료 후 하단 리미트까지 연속 하강
// =================================================

void moveToBottomAfterPayment()
{
  // 이미 하단 리미트가 눌려 있으면 움직이지 않음
  if (bottomLimitPressed())
  {
    currentPositionSteps = 0;

    Serial.println(F("이미 하단 리미트 위치입니다."));
    return;
  }

  // 모터를 하강 방향으로 설정
  digitalWrite(dirPin, DOWN_DIRECTION);
  delayMicroseconds(500);

  unsigned long startTime = millis();

  // 하단 리미트가 눌릴 때까지 계속 스텝 출력
  while (!bottomLimitPressed())
  {
    makeStep();

    if (currentPositionSteps > 0)
    {
      currentPositionSteps--;
    }

    // 스위치 또는 배선 고장 시 무한 하강 방지
    if (millis() - startTime >= HOMING_TIMEOUT_MS)
    {
      Serial.println(F("결제 후 하단 복귀 시간 초과"));
      Serial.println(F("하단 리미트와 모터 방향 확인 필요"));
      return;
    }
  }

  currentPositionSteps = 0;

  Serial.println(F("하단 리미트 감지"));
  Serial.println(F("모터 즉시 정지"));

  delay(100);
}

// =================================================
// 사용자 감지
// =================================================

bool detectUser()
{
  byte detectedCount = 0;

  Serial.println(F("----------------------------"));

  for (byte i = 0; i < MEASUREMENT_COUNT; i++)
  {
    VL53L0X_RangingMeasurementData_t measure;

    tof.rangingTest(&measure, false);

    if (measure.RangeStatus != 4)
    {
      int distance = measure.RangeMilliMeter;

      Serial.print(F("측정 "));
      Serial.print(i + 1);
      Serial.print(F(": "));
      Serial.print(distance);
      Serial.println(F(" mm"));

      if (distance >= MIN_DISTANCE_MM &&
          distance <= DETECT_DISTANCE_MM)
      {
        detectedCount++;
      }
    }
    else
    {
      Serial.print(F("측정 "));
      Serial.print(i + 1);
      Serial.println(F(": 미감지"));
    }

    delay(50);
  }

  Serial.print(F("감지 횟수: "));
  Serial.print(detectedCount);
  Serial.print(F("/"));
  Serial.println(MEASUREMENT_COUNT);

  if (detectedCount >= REQUIRED_DETECTION_COUNT)
  {
    Serial.println(F("거리센서 판정: 사용자 감지"));
    return true;
  }

  Serial.println(F("거리센서 판정: 사용자 미감지"));
  return false;
}

// =================================================
// 지정한 거리만큼 상승
// =================================================

bool moveUpCm(long distanceCm)
{
  if (topLimitPressed())
  {
    Serial.println(F("상단 리미트 눌림 → 상승 금지"));
    return false;
  }

  long requestedSteps = STEPS_PER_CM * distanceCm;

  digitalWrite(dirPin, UP_DIRECTION);
  delayMicroseconds(500);

  for (long i = 0; i < requestedSteps; i++)
  {
    if (topLimitPressed())
    {
      Serial.println(F("상단 리미트 감지 → 즉시 정지"));
      return false;
    }

    makeStep();
    currentPositionSteps++;
  }

  Serial.print(F("상승 완료, 계산 높이: "));
  printCurrentHeight();

  return true;
}

// =================================================
// 지정한 거리만큼 하강
// =================================================

bool moveDownCm(long distanceCm)
{
  if (bottomLimitPressed())
  {
    currentPositionSteps = 0;

    Serial.println(F("하단 리미트 눌림 → 하강 금지"));
    return false;
  }

  long requestedSteps = STEPS_PER_CM * distanceCm;

  digitalWrite(dirPin, DOWN_DIRECTION);
  delayMicroseconds(500);

  for (long i = 0; i < requestedSteps; i++)
  {
    if (bottomLimitPressed())
    {
      currentPositionSteps = 0;

      Serial.println(F("하단 리미트 감지 → 즉시 정지"));
      return false;
    }

    makeStep();

    if (currentPositionSteps > 0)
    {
      currentPositionSteps--;
    }
  }

  Serial.print(F("하강 완료, 계산 높이: "));
  printCurrentHeight();

  return true;
}

// =================================================
// 스텝모터 펄스 출력
// =================================================

void makeStep()
{
  digitalWrite(stepPin, HIGH);
  delayMicroseconds(STEP_DELAY_US);

  digitalWrite(stepPin, LOW);
  delayMicroseconds(STEP_DELAY_US);
}

// =================================================
// 리미트 스위치 확인
// INPUT_PULLUP이므로 눌리면 LOW
// =================================================

bool bottomLimitPressed()
{
  return digitalRead(bottomLimitPin) == LOW;
}

bool topLimitPressed()
{
  return digitalRead(topLimitPin) == LOW;
}

// =================================================
// 현재 높이 출력
// =================================================

void printCurrentHeight()
{
  float heightCm =
    (float)currentPositionSteps / STEPS_PER_CM;

  Serial.print(heightCm, 1);
  Serial.println(F(" cm"));
}