# StickS3_ROM_Controller
StickS3 is a wireless controller built for the M5StickS3 made to work seamlessly with the CoreS3 ROM Launcher. THIS ONLY WORKS WITH THE CS3 RUNNING THE ROM LAUNCHER.  The two devices communicate over ESP-NOW — no WiFi, no Bluetooth, no wires.

A portable NES emulator running on the M5Stack CoreS3 with a wireless controller built from the M5StickS3. Just pick up the StickS3 and play.

Hardware
RoleDeviceEmulator / Display-- M5Stack CoreS3, Wireless Controller-- M5StickS3

How It Works
The CoreS3 runs Nofrendo, a cycle-accurate NES emulator, and renders at the NES native resolution (256×240) centered on its 320×240 ILI9342 display. ROMs are loaded directly from a microSD card.
The StickS3 acts as a virtual NES controller. It reads its two physical buttons and IMU accelerometer, packs the state into a standard NES button byte (active LOW, 8-bit layout), and broadcasts it via ESP-NOW at ~60Hz. The CoreS3 receives the packet and feeds it into Nofrendo's input system exactly as if it were a real controller being polled over serial.
Because the controller state maps to generic event_joypad1_* events inside Nofrendo, any NES ROM loaded on the CoreS3 works with the StickS3 automatically — no per-game configuration needed.

Controller Mapping
StickS3 D-PAD is basically your classic tilt steering. Btn A is A. Btn B is B. Long Press B= Select. Double Press B+ Start.

IMU calibration happens automatically on boot (first 20 samples averaged). Hold the StickS3 flat in your natural playing position before powering on or rebooting for best results.

*****ONLY PLAYER 1 IS WORKING*****

Notes

Sound is disabled in this build. The audio pipeline code is present but stubbed out — a future build may enable it once a stable output method for CoreS3 is confirmed.

The CoreS3 display renders with 32px black bars on each side to center the 256px-wide NES frame on the 320px-wide screen. No scaling — pure 1:1 pixels vertically and horizontally.
The StickS3 sends a packet every 16ms regardless of input changes. This doubles as a heartbeat — the CoreS3 will release all buttons automatically if no packet arrives within 500ms (e.g. StickS3 powered off or out of range), so games don't get stuck with a button held.

ESP-NOW range is typically 50–100m line of sight indoors, so you won't be running out of range anytime soon.

