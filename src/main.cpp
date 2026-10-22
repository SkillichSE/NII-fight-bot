#include <Arduino.h>
#include <PS4Controller.h>

constexpr uint8_t MOTOR_LEFT_PWM  = 22;
constexpr uint8_t MOTOR_LEFT_DIR  = 23;
constexpr uint8_t MOTOR_RIGHT_PWM = 19;
constexpr uint8_t MOTOR_RIGHT_DIR = 18;
constexpr uint8_t LIFT_PWM        = 25;
constexpr uint8_t LIFT_DIR        = 26;

constexpr bool LEFT_INVERTED  = false;
constexpr bool RIGHT_INVERTED = false;
constexpr bool LIFT_INVERTED  = false;

constexpr uint32_t PWM_FREQ_HZ = 20000;
constexpr uint8_t  PWM_BITS    = 8;
constexpr int      PWM_MAX     = 255;

constexpr int STICK_DEADZONE   = 15;
constexpr int TRIGGER_DEADZONE = 20;

constexpr uint32_t LOOP_PERIOD_MS = 10;
constexpr int DRIVE_RAMP_STEP = 14;
constexpr int LIFT_RAMP_STEP  = 10;

const char* const PS4_MAC = "AA:BB:CC:DD:EE:FF";

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#define PWM_SETUP(pin, ch) ledcAttach(pin, PWM_FREQ_HZ, PWM_BITS)
#define PWM_WRITE(pin, ch, duty) ledcWrite(pin, duty)
#else
#define PWM_SETUP(pin, ch) do { ledcSetup(ch, PWM_FREQ_HZ, PWM_BITS); ledcAttachPin(pin, ch); } while (0)
#define PWM_WRITE(pin, ch, duty) ledcWrite(ch, duty)
#endif

struct Motor {
    uint8_t pwmPin;
    uint8_t dirPin;
    uint8_t channel;
    bool inverted;
    int current;
    int rampStep;
};

Motor leftMotor  = {MOTOR_LEFT_PWM,  MOTOR_LEFT_DIR,  0, LEFT_INVERTED,  0, DRIVE_RAMP_STEP};
Motor rightMotor = {MOTOR_RIGHT_PWM, MOTOR_RIGHT_DIR, 1, RIGHT_INVERTED, 0, DRIVE_RAMP_STEP};
Motor liftMotor  = {LIFT_PWM,        LIFT_DIR,        2, LIFT_INVERTED,  0, LIFT_RAMP_STEP};

void motorInit(const Motor& m) {
    pinMode(m.dirPin, OUTPUT);
    digitalWrite(m.dirPin, LOW);
    PWM_SETUP(m.pwmPin, m.channel);
    PWM_WRITE(m.pwmPin, m.channel, 0);
}

void motorWrite(Motor& m, int speed) {
    speed = constrain(speed, -PWM_MAX, PWM_MAX);
    m.current = speed;
    if (speed != 0) {
        digitalWrite(m.dirPin, ((speed > 0) != m.inverted) ? HIGH : LOW);
    }
    PWM_WRITE(m.pwmPin, m.channel, abs(speed));
}

void motorDrive(Motor& m, int target) {
    int delta = target - m.current;
    if (delta > m.rampStep) delta = m.rampStep;
    if (delta < -m.rampStep) delta = -m.rampStep;
    motorWrite(m, m.current + delta);
}

void stopAll() {
    motorWrite(leftMotor, 0);
    motorWrite(rightMotor, 0);
    motorWrite(liftMotor, 0);
}

int scaleAxis(int raw, int deadzone, int rawMax, int outMax) {
    raw = constrain(raw, -rawMax, rawMax);
    int mag = abs(raw);
    if (mag <= deadzone) return 0;
    int scaled = (mag - deadzone) * outMax / (rawMax - deadzone);
    return raw > 0 ? scaled : -scaled;
}

void setup() {
    Serial.begin(115200);

    motorInit(leftMotor);
    motorInit(rightMotor);
    motorInit(liftMotor);

    PS4.begin(PS4_MAC);
    Serial.println("Waiting for DualShock 4 connection...");
}

void loop() {
    static uint32_t lastTick = 0;
    static bool wasConnected = false;

    uint32_t now = millis();
    if (now - lastTick < LOOP_PERIOD_MS) return;
    lastTick = now;

    bool connected = PS4.isConnected();

    if (connected != wasConnected) {
        wasConnected = connected;
        Serial.println(connected ? "Controller connected" : "Controller disconnected, motors stopped");
    }

    if (!connected) {
        stopAll();
        return;
    }

    int throttle = scaleAxis(PS4.LStickY(), STICK_DEADZONE, 127, PWM_MAX);
    int steering = scaleAxis(PS4.RStickX(), STICK_DEADZONE, 127, PWM_MAX);

    int left  = throttle + steering;
    int right = throttle - steering;

    int peak = max(abs(left), abs(right));
    if (peak > PWM_MAX) {
        left  = left  * PWM_MAX / peak;
        right = right * PWM_MAX / peak;
    }

    motorDrive(leftMotor, left);
    motorDrive(rightMotor, right);

    int up   = PS4.R2Value();
    int down = PS4.L2Value();
    up   = up   > TRIGGER_DEADZONE ? up   : 0;
    down = down > TRIGGER_DEADZONE ? down : 0;

    motorDrive(liftMotor, constrain(up - down, -PWM_MAX, PWM_MAX));
}
