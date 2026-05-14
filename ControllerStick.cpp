#include <Arduino.h>
#include <M5Unified.h>

// --- Calibration + smoothing state ---
static float base_ax = 0.0f;
static float base_ay = 0.0f;
static bool calibrated = false;
static float smooth_ax = 0.0f;
static float smooth_ay = 0.0f;
static uint32_t lastBPressTime = 0;
static uint32_t bHoldStartTime = 0;

static bool startPulseActive = false;
static uint32_t startPulseUntil = 0;

// Declared in main .ino — sends packed state via ESP-NOW broadcast
extern "C" void espnow_send_controller(uint32_t state);

extern "C" void controller_init()
{
    Serial.println("controller_init ran");
}

extern "C" uint32_t controller_read_input()
{
    M5.update();

    uint32_t value = 0xFFFFFFFF;
    uint32_t now = millis();

    bool aPressed = M5.BtnA.isPressed();
    bool bPressed = M5.BtnB.isPressed();

    // NES bit layout (active LOW — bit clear = pressed):
    // bit 0 = up
    // bit 1 = down
    // bit 2 = left
    // bit 3 = right
    // bit 4 = select
    // bit 5 = start
    // bit 6 = A
    // bit 7 = B

    // Detect double-press on BtnB → START
    if (M5.BtnB.wasPressed()) {
        if ((now - lastBPressTime) < 350) {
            Serial.println("BtnB double-press -> START");
            startPulseActive = true;
            startPulseUntil = now + 300;
            lastBPressTime = 0;
        } else {
            lastBPressTime = now;
            bHoldStartTime = now;
        }
    }

    if (startPulseActive) {
        if (now < startPulseUntil) {
            value ^= (1 << 5);  // START
        } else {
            startPulseActive = false;
        }
    } else {
        // BtnA = NES A (jump)
        if (aPressed) {
            value ^= (1 << 6);
        }

        // Long-hold BtnB = SELECT
        if (bPressed && (now - bHoldStartTime) > 500) {
            Serial.println("BtnB hold -> SELECT");
            value ^= (1 << 4);  // SELECT
        }
        // Normal BtnB = NES B (run/fire)
        else if (bPressed) {
            value ^= (1 << 7);  // B
        }
    }

    // Tilt controls
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    if (M5.Imu.getAccel(&ax, &ay, &az)) {

        static int calCount = 0;
        if (calCount < 20) {
            base_ax += ax;
            base_ay += ay;
            calCount++;
            if (calCount == 20) {
                base_ax /= 20.0f;
                base_ay /= 20.0f;
                calibrated = true;
                Serial.println("IMU calibrated (averaged)");
            }
            // Send uncalibrated state (neutral) while calibrating
            espnow_send_controller(value);
            return value;
        }

        float dx = ax - base_ax;
        float dy = ay - base_ay;

        // Deadzone
        if (fabs(dx) < 0.05f) dx = 0;
        if (fabs(dy) < 0.05f) dy = 0;

        // LEFT / RIGHT
        if (dx < -0.10f) {
            value ^= (1 << 3);  // RIGHT
        } else if (dx > 0.10f) {
            value ^= (1 << 2);  // LEFT
        }

        // CROUCH
        if (dy > 0.15f) {
            value ^= (1 << 1);  // DOWN
        }

        // UP
        if (dy < -0.25f) {
            value ^= (1 << 0);  // UP
        }

        if (dx > 0.25f) {
            value ^= (1 << 0);  // UP (tilt back)
        }
    }

    // ── Broadcast controller state via ESP-NOW ──────────────────────────
    espnow_send_controller(value);
    // ────────────────────────────────────────────────────────────────────

    return value;
}
