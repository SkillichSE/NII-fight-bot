#include <Arduino.h>
#include <Bluepad32.h>

// Pins: each motor has one PWM pin (speed) and one DIR pin (direction)
#define L_PWM   25
#define L_DIR   26
#define R_PWM   27
#define R_DIR   14
#define F_PWM   32
#define F_DIR   33
// Pins 34 and 35 are input-only, fine for an encoder
#define ENC_A   34
#define ENC_B   35
#define BUZZER  23
#define LED     2

// Stick idles around 4, so small values are ignored
const int  DEADZONE         = 40;
// Set to true if that motor spins the wrong way
const bool INVERT_LEFT      = false;
const bool INVERT_RIGHT     = false;
const bool INVERT_FLIP      = false;
// Set to true if encoder count goes down when the flipper lifts
const bool ENC_INVERT       = false;
// Encoder ticks from bottom to top, tune to the real flipper travel
const long FLIP_UP_TICKS    = 300;
const int  FLIP_SPEED       = 255;
// Max time (ms) for one raise or lower, safety limit
const uint32_t FLIP_TIMEOUT = 2000;
// No encoder change for this long (ms) means the flipper has stopped
const uint32_t STALL_MS     = 200;
// No gamepad data for this long (ms) means connection lost, stop everything
const uint32_t DATA_TIMEOUT = 300;

// Encoder position. Zero = flipper down. Start with the flipper lowered
volatile long encCount = 0;

void IRAM_ATTR encISR() {
  int a = digitalRead(ENC_A);
  int b = digitalRead(ENC_B);
  long step = (a == b) ? 1 : -1;
  if (ENC_INVERT) step = -step;
  encCount += step;
}

long getEnc() { return encCount; }

// v is -255..255, sign sets direction
void setMotor(int pwmPin, int dirPin, int v, bool invert) {
  if (invert) v = -v;
  v = constrain(v, -255, 255);
  digitalWrite(dirPin, v >= 0 ? HIGH : LOW);
  ledcWrite(pwmPin, abs(v));
}

// thr = forward/back, turn = left/right, both -512..512
// Left and right sides get different power to steer
void drive(int thr, int turn) {
  int l = constrain(thr + turn, -512, 512);
  int r = constrain(thr - turn, -512, 512);
  setMotor(L_PWM, L_DIR, l * 255 / 512, INVERT_LEFT);
  setMotor(R_PWM, R_DIR, r * 255 / 512, INVERT_RIGHT);
}

int applyDeadzone(int v) { return abs(v) < DEADZONE ? 0 : v; }

// One X press: raise, then lower right away
// Stops by target ticks, by encoder stall, or by timeout
enum FlipState { F_IDLE, F_RAISE, F_LOWER };
FlipState fstate = F_IDLE;
uint32_t fStart = 0, lastEncMs = 0;
long lastEnc = 0;

void flipperStart() {
  if (fstate != F_IDLE) return;
  fstate = F_RAISE;
  fStart = millis();
  lastEnc = getEnc();
  lastEncMs = fStart;
}

void flipperStop() {
  setMotor(F_PWM, F_DIR, 0, INVERT_FLIP);
  fstate = F_IDLE;
}

void flipperUpdate() {
  uint32_t now = millis();
  long pos = getEnc();
  if (pos != lastEnc) { lastEnc = pos; lastEncMs = now; }

  bool stalled = (now - fStart > 150) && (now - lastEncMs > STALL_MS);
  bool timeout = (now - fStart > FLIP_TIMEOUT);

  switch (fstate) {
    case F_IDLE:
      setMotor(F_PWM, F_DIR, 0, INVERT_FLIP);
      break;

    case F_RAISE:
      setMotor(F_PWM, F_DIR, FLIP_SPEED, INVERT_FLIP);
      if (pos >= FLIP_UP_TICKS || stalled || timeout) {
        fstate = F_LOWER;
        fStart = now;
        lastEnc = pos;
        lastEncMs = now;
      }
      break;

    case F_LOWER:
      setMotor(F_PWM, F_DIR, -FLIP_SPEED, INVERT_FLIP);
      if (pos <= 0 || stalled || timeout) {
        if (stalled) encCount = 0;  // hit the bottom, reset zero
        flipperStop();
      }
      break;
  }
}

// Two short buzzer + LED pulses, same timing as the rumble
bool fbActive = false;
uint32_t fbStart = 0;

void feedbackStart() { fbActive = true; fbStart = millis(); }

void feedbackUpdate() {
  bool on = false;
  if (fbActive) {
    uint32_t t = millis() - fbStart;
    on = (t < 250) || (t >= 500 && t < 750);
    if (t >= 750) fbActive = false;
  }
  digitalWrite(BUZZER, on ? HIGH : LOW);
  digitalWrite(LED, on ? HIGH : LOW);
}

// Only one gamepad is used
ControllerPtr gamepad = nullptr;
bool rumbleDone = false;
bool prevX = false;
uint32_t lastDataMs = 0;

void stopAll() {
  drive(0, 0);
  flipperStop();
}

void onConnectedController(ControllerPtr ctl) {
  if (gamepad == nullptr) {
    Serial.println("Gamepad connected");
    gamepad = ctl;
    rumbleDone = false;
    prevX = false;
    lastDataMs = millis();
  } else {
    ctl->disconnect();
  }
}

// Gamepad lost: stop all motors
void onDisconnectedController(ControllerPtr ctl) {
  if (gamepad == ctl) {
    Serial.println("Gamepad disconnected, stopping");
    gamepad = nullptr;
    stopAll();
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(L_DIR, OUTPUT);
  pinMode(R_DIR, OUTPUT);
  pinMode(F_DIR, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED, OUTPUT);
  ledcAttach(L_PWM, 20000, 8);
  ledcAttach(R_PWM, 20000, 8);
  ledcAttach(F_PWM, 20000, 8);
  stopAll();

  pinMode(ENC_A, INPUT);
  pinMode(ENC_B, INPUT);
  attachInterrupt(digitalPinToInterrupt(ENC_A), encISR, CHANGE);

  BP32.setup(&onConnectedController, &onDisconnectedController);
  BP32.enableVirtualDevice(false);

  Serial.println("Waiting for gamepad (SHARE + PS)...");
}

void loop() {
  BP32.update();
  feedbackUpdate();

  bool alive = gamepad && gamepad->isConnected();
  if (alive && gamepad->hasData()) lastDataMs = millis();

  // Failsafe: no gamepad or no data, stop everything
  if (!alive || millis() - lastDataMs > DATA_TIMEOUT) {
    stopAll();
    delay(5);
    return;
  }

  if (!rumbleDone) {
    gamepad->playDualRumble(0,   250, 0xC0, 0xC0);
    gamepad->playDualRumble(500, 250, 0xC0, 0xC0);
    feedbackStart();
    rumbleDone = true;
  }

  // Left stick. Up is negative Y, so it is flipped
  int thr  = -applyDeadzone(gamepad->axisY());
  int turn =  applyDeadzone(gamepad->axisX());
  drive(thr, turn);

  // a() is the X (Cross) button on DualShock 4
  bool x = gamepad->a();
  if (x && !prevX) flipperStart();
  prevX = x;
  flipperUpdate();

  delay(5);
}