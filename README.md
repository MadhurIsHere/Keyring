# Keyring

An interactive Birthday & Memory Keychain firmware built for the ESP32-C3 SuperMini and a 128×64 I2C SSD1306 OLED display.

## ✨ Features
* **Custom Mini-Games:**
  * **Shreya Run:** Mini pixel avatar runner jumping over obstacles.
  * **Flappy Balloon:** Floating birthday balloon dodging flickering candles.
  * **Classic Snake:** Arcade snake with high-score tracking stored in NVS flash.
* **Live Wi-Fi Clock & Weather Engine:**
  * NTP real-time clock synchronization.
  * Live temperature and animated buddy accessories (sunglasses for sunny weather, umbrella for rain).
  * Auto-reconnect and 2.4 GHz live network diagnostics.
* **Photo Memory Album:**
  * High-contrast 128×64 edge-to-edge monochrome photo album.
  * Changeable idle home screen photo persisted to EEPROM/NVS.
* **Fortune Cookie Message Bank:**
  * 25 personalized, cheerful, and motivational messages.
  * Animated kawaii chibi blinking faces with blush marks and star winks.
* **Power Management:**
  * Deep sleep mode with low power consumption.
  * One-second hold power off with sleep animation.
  * Instant hardware wake on button press with greeting.

## 🛠️ Hardware Specifications
* **MCU:** ESP32-C3 SuperMini (RISC-V, 4MB Flash)
* **Display:** 0.96" I2C OLED (SSD1306, 128×64)
  * `SDA` -> GPIO 8
  * `SCL` -> GPIO 9
* **Controls:**
  * `Button 1 (Left)` -> GPIO 5 (Pull-up, System / Menu / Navigation / Sleep)
  * `Button 2 (Right)` -> GPIO 4 (Pull-up, Action / Select / Jump / Smiles)

## 🚀 Building & Flashing
This project is built using [PlatformIO](https://platformio.org/).

```bash
# Build and upload
pio run -t upload
```
