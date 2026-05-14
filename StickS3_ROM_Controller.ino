/* StickS3 NES Controller
 * IMU task pinned to core 0, ESP-NOW + buttons on core 1
 * Static splash screen — display never touched after setup()
 *
 * NES bit layout (active LOW — bit clear = pressed):
 *   bit 0 = up,  bit 1 = down,  bit 2 = left,  bit 3 = right
 *   bit 4 = select,  bit 5 = start,  bit 6 = A,  bit 7 = B
 */

#include <M5Unified.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Embedded NES controller splash image (240x135 PNG, PROGMEM)
#include "nes_splash.h"

static uint8_t broadcastAddr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Shared tilt state — written by IMU task, read by main loop
// volatile so compiler doesn't cache it
static volatile uint32_t imu_bits = 0xFFFFFFFF;  // active LOW, default = no input
static volatile bool imu_calibrated = false;

// Button timing
static uint32_t lastBPressTime  = 0;
static uint32_t bHoldStartTime  = 0;
static bool     startPulseActive = false;
static uint32_t startPulseUntil  = 0;

// ── IMU task (core 0) ─────────────────────────────────────────────────────────
// Runs independently — never touches ESP-NOW or display
static void imuTask(void *arg)
{
    float base_ax = 0.0f, base_ay = 0.0f;
    int   calCount = 0;

    Serial.println("IMU task started on core 0");

    for (;;) {
        float ax = 0, ay = 0, az = 0;
        if (M5.Imu.getAccel(&ax, &ay, &az)) {

            // Calibrate on first 20 samples
            if (calCount < 20) {
                base_ax += ax;
                base_ay += ay;
                calCount++;
                if (calCount == 20) {
                    base_ax /= 20.0f;
                    base_ay /= 20.0f;
                    imu_calibrated = true;
                    Serial.printf("IMU calibrated: base_ax=%.3f base_ay=%.3f\n", base_ax, base_ay);
                }
                vTaskDelay(pdMS_TO_TICKS(16));
                continue;
            }

            float dx = ax - base_ax;
            float dy = ay - base_ay;
            if (fabs(dx) < 0.05f) dx = 0;
            if (fabs(dy) < 0.05f) dy = 0;

            uint32_t bits = 0xFFFFFFFF;
            if      (dx < -0.10f) bits ^= (1 << 3);  // RIGHT
            else if (dx >  0.10f) bits ^= (1 << 2);  // LEFT
            if (dy >  0.15f)      bits ^= (1 << 1);  // DOWN
            if (dy < -0.25f)      bits ^= (1 << 0);  // UP
            if (dx >  0.25f)      bits ^= (1 << 0);  // UP

            imu_bits = bits;
        }
        vTaskDelay(pdMS_TO_TICKS(16));  // ~60 Hz
    }
}

// ── ESP-NOW ───────────────────────────────────────────────────────────────────
void espnow_init()
{
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed!");
        return;
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastAddr, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
    Serial.println("ESP-NOW ready");
}

// ── Splash — drawn ONCE from embedded PNG, never touched again ────────────────
static void drawSplash(int battPct)
{
    // Draw the embedded 240x135 PNG flush to the top-left corner.
    M5.Display.drawPng(nes_splash_png, nes_splash_len, 0, 0, 240, 135);

    // ── Battery overlay (bottom-right corner, color-coded) ────────────────────
    uint16_t batColor = (battPct > 50) ? 0x07E0    // green
                      : (battPct > 20) ? 0xFFE0    // yellow
                      :                  0xF800;   // red
    // Black backing rectangle for legibility
    M5.Display.fillRect(155, 115, 82, 16, 0x0000);
    // Colored text on top
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(batColor);
    M5.Display.setCursor(158, 118);
    M5.Display.printf("BAT %d%%", battPct);
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);

    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);  // landscape 240x135

    // Read battery ONCE before ESP-NOW starts — safe here, single task
    int battPct = M5.Power.getBatteryLevel();
    Serial.printf("Battery: %d%%\n", battPct);

    drawSplash(battPct);  // only display call ever

    espnow_init();

    // Spin up IMU task on core 0 with a healthy stack
    xTaskCreatePinnedToCore(
        imuTask,
        "imu",
        4096,   // stack size
        NULL,
        1,      // priority
        NULL,
        0       // core 0
    );

    Serial.println("Setup complete");
}

// ── Loop (core 1) ─────────────────────────────────────────────────────────────
// Reads buttons + imu_bits, broadcasts via ESP-NOW
void loop()
{
    M5.update();

    uint32_t now = millis();
    bool aPressed = M5.BtnA.isPressed();
    bool bPressed = M5.BtnB.isPressed();

    // Start: double-tap BtnB
    if (M5.BtnB.wasPressed()) {
        if ((now - lastBPressTime) < 350) {
            startPulseActive = true;
            startPulseUntil  = now + 300;
            lastBPressTime   = 0;
        } else {
            lastBPressTime  = now;
            bHoldStartTime  = now;
        }
    }

    // Merge button bits with imu_bits from the other task
    // Start with IMU state (or neutral if not calibrated yet)
    uint32_t value = imu_calibrated ? imu_bits : 0xFFFFFFFF;

    if (startPulseActive) {
        if (now < startPulseUntil) {
            value ^= (1 << 5);  // START
        } else {
            startPulseActive = false;
        }
    } else {
        if (aPressed) value ^= (1 << 6);  // A
        if (bPressed && (now - bHoldStartTime) > 500) {
            value ^= (1 << 4);             // SELECT
        } else if (bPressed) {
            value ^= (1 << 7);             // B
        }
    }

    esp_now_send(broadcastAddr, (uint8_t *)&value, sizeof(value));

    delay(16);  // ~60 Hz
}
