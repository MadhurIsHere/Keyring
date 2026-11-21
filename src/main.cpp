#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_sleep.h>
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "bitmap_shreya.h"

// ==========================================
// OLED & PIN CONFIGURATION (128x64)
// ==========================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Buttons (Reversed as requested)
#define BUTTON_POWER 5  // Button 1: Left button (Menu / Navigation / Deep Sleep)
#define BUTTON_MODE  4  // Button 2: Right button (Action / Select / Jump / Smiles)

#define I2C_SDA 8
#define I2C_SCL 9

// ==========================================
// WI-FI & WEATHER CONFIGURATION
// ==========================================
// You can enter your Wi-Fi details here (or phone hotspot):
const char* WIFI_SSID = "Phone";
const char* WIFI_PASS = "unlimited";

// Default coordinates (New Delhi / India: Lat 28.61, Lon 77.23, UTC+5:30)
// Easily change coordinates for your city
float CITY_LAT = 28.61;
float CITY_LON = 77.23;
long TIME_ZONE_OFFSET = 19800; // 5.5 hours * 3600 = 19800 for IST

Preferences prefs;

// Forward declarations
void initDino();
void updateDino();
void initFlappy();
void updateFlappy();
void initSnake();
void updateSnake();
void drawMenu();

// ==========================================
// SYSTEM STATES
// ==========================================
enum SystemState {
  STATE_IDLE_PHOTO,   // Shreya's photo idle state (Default)
  STATE_MESSAGES,     // 30+ Random Messages / Fortune Cookie with Blinking Faces
  STATE_ALBUM,        // 3-Photo Memory Album
  STATE_CLOCK,        // Live Wi-Fi Clock & Weather
  STATE_MENU,         // 3 Games Arcade Menu
  STATE_DINO,         // Game 1: Dino Run (Shreya Avatar)
  STATE_FLAPPY,       // Game 2: Flappy Balloon (Dodging Candles)
  STATE_SNAKE,        // Game 3: Classic Snake
  STATE_SECRET,       // Hidden easter egg
  STATE_GAMEOVER
};

SystemState currentState = STATE_IDLE_PHOTO;
SystemState gameBeforeGameOver = STATE_DINO;

// Photo album index (0 to 8: all 9 photos)
int photoAlbumIndex = 0;
int homePhotoIndex = 0;

const int TOTAL_ALBUM_PHOTOS = 10;

// Home photo mode: 0..9 = fixed photo, 10 = Photo of the Day
const int HOME_MODE_DAILY = TOTAL_ALBUM_PHOTOS;
long lastKnownDay = -1;

// Slideshow
bool slideshowOn = false;
unsigned long lastSlideTime = 0;
const unsigned long SLIDE_INTERVAL = 4500;

// Long-press tracking for B2
bool modeHold1Fired = false;
bool modeHold3Fired = false;
int holdProgress = 0;

// Easter egg
int secretPage = 0;
unsigned long secretStart = 0;
const int TYPE_MS = 45;

const unsigned char* albumBitmaps[TOTAL_ALBUM_PHOTOS] = {
  bitmap_shreya_128x64,
  bitmap_selfie_128x64,
  bitmap_dog_128x64,
  bitmap_scuba_128x64,
  bitmap_beach_128x64,
  bitmap_deer_128x64,
  bitmap_cafe_128x64,
  bitmap_mountain_128x64,
  bitmap_fireworks_128x64,
  bitmap_festive_128x64
};

const unsigned char* homeBitmaps[TOTAL_ALBUM_PHOTOS] = {
  bitmap_shreya_64x64,
  bitmap_selfie_64x64,
  bitmap_dog_64x64,
  bitmap_scuba_64x64,
  bitmap_beach_64x64,
  bitmap_deer_64x64,
  bitmap_cafe_64x64,
  bitmap_mountain_64x64,
  bitmap_fireworks_64x64,
  bitmap_festive_64x64
};

const char* photoTitles[TOTAL_ALBUM_PHOTOS] = {
  "Little Shreya",
  "Holi Selfie",
  "Cute Puppy",
  "Scuba Diving",
  "Beach Waves",
  "Deer Park",
  "Cafe Friends",
  "Mountain View",
  "Fireworks Smile",
  "Festive Friends"
};

// Button state tracking
bool powerState = HIGH, lastPowerState = HIGH;
bool modeState = HIGH, lastModeState = HIGH;
unsigned long powerPressTime = 0;
unsigned long modePressTime = 0;
bool powerLongHandled = false;
bool modeLongHandled = false;

// Arcade Menu
const char* menuItems[] = {
  "1. Shreya Run",
  "2. Flappy Balloon",
  "3. Classic Snake",
  "4. Live Clock...",
  "5. Photo Album (10)",
  "6. Home Pic Mode",
  "< Back to Home"
};
const int TOTAL_MENU_ITEMS = 7;
int menuCursor = 0;

// High Scores (Saved to ESP32 Flash NVS)
int currentScore = 0;
int bestDino = 0;
int bestFlappy = 0;
int bestSnake = 0;

void loadHighScores() {
  prefs.begin("shreya", true);
  bestDino   = prefs.getInt("dino", 0);
  bestFlappy = prefs.getInt("flappy", 0);
  bestSnake  = prefs.getInt("snake", 0);
  homePhotoIndex = prefs.getInt("homePic", HOME_MODE_DAILY);  // default: Photo of the Day
  lastKnownDay   = prefs.getLong("lastDay", -1);
  if (homePhotoIndex < 0 || homePhotoIndex > TOTAL_ALBUM_PHOTOS) homePhotoIndex = HOME_MODE_DAILY;
  prefs.end();
}

void saveScore(const char* key, int val) {
  prefs.begin("shreya", false);
  prefs.putInt(key, val);
  prefs.end();
}

// ==========================================
// PHOTO OF THE DAY
// ==========================================
int getHomeIndex() {
  if (homePhotoIndex != HOME_MODE_DAILY) return homePhotoIndex;

  time_t now = time(nullptr);
  if (now > 1700000000L) {  // clock has been synced via NTP
    long today = (long)((now + TIME_ZONE_OFFSET) / 86400L);
    if (today != lastKnownDay) {
      lastKnownDay = today;
      prefs.begin("shreya", false);
      prefs.putLong("lastDay", lastKnownDay);
      prefs.end();
    }
  }
  if (lastKnownDay < 0) return 0;
  return (int)(lastKnownDay % TOTAL_ALBUM_PHOTOS);
}

// ==========================================
// SLIDESHOW HELPERS
// ==========================================
void drawBitmapColumns(const unsigned char* bmp, int x0, int x1) {
  for (int x = x0; x < x1; x++) {
    for (int y = 0; y < 64; y++) {
      bool on = bmp[y * 16 + (x >> 3)] & (0x80 >> (x & 7));
      display.drawPixel(x, y, on ? SSD1306_WHITE : SSD1306_BLACK);
    }
  }
}

void wipeToPhoto(int newIndex) {
  const unsigned char* bmp = albumBitmaps[newIndex];
  for (int x = 0; x < 128; x += 16) {
    drawBitmapColumns(bmp, x, x + 16);
    display.display();
    delay(15);
  }
}

void showToast(const char* t) {
  int w = strlen(t) * 6 + 10;
  int x = (128 - w) / 2;
  display.fillRect(x, 20, w, 18, SSD1306_BLACK);
  display.drawRect(x, 20, w, 18, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(x + 5, 25);
  display.print(t);
  display.display();
  delay(700);
}

// ==========================================
// EASTER EGG: SECRET MESSAGE
// Max 4 lines per page, max 21 chars per line, NO emojis.
// ==========================================
const char* secretPages[] = {
  "Psst, Shreya...\nyou found the\nsecret screen!",
  "Every memory\nwe made together\nis gold.",
  "You make every\nroom brighter\njust by showing up!",
  "Happy Birthday!\n\n- With Love"
};
const int TOTAL_SECRET_PAGES = 4;

void startSecret() {
  currentState = STATE_SECRET;
  secretPage = 0;
  secretStart = millis();
}

void drawSecretPage() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(16, 2);
  display.print("~ just for you ~");
  display.drawFastHLine(0, 11, 128, SSD1306_WHITE);

  const char* s = secretPages[secretPage];
  int len = strlen(s);
  int reveal = (millis() - secretStart) / TYPE_MS;

  int y = 16;
  display.setCursor(4, y);
  for (int i = 0; i < len && i < reveal; i++) {
    if (s[i] == '\n') {
      y += 10;
      display.setCursor(4, y);
    } else {
      display.print(s[i]);
    }
  }

  bool done = reveal >= len;
  display.fillRect(0, 52, 128, 12, SSD1306_BLACK);
  display.drawFastHLine(0, 52, 128, SSD1306_WHITE);
  display.setCursor(4, 54);
  display.print("B1:Home");
  display.setCursor(74, 54);
  if (!done) display.print("B2:Skip");
  else if (secretPage == TOTAL_SECRET_PAGES - 1) display.print("B2:Close");
  else display.print("B2:Next");
  display.display();
}

// ==========================================
// 25 SHORT, NON-ROMANTIC PERSONALIZED MESSAGES
// ==========================================
const char* shreyaMessages[] = {
  "You're pure magic,\nShreya! :)",
  "Keep shining,\nyou rock!",
  "Remember: you can\ndo anything!",
  "Happy Birthday to\nmy favorite buddy!",
  "Smile! It suits\nyou best :)",
  "You're certified\nadorable trouble!",
  "If you were a veg,\na cute-cumber!",
  "May all wishes\ncome true today!",
  "You bring so much\nsunshine! :)",
  "Keep doing\namazing things!",
  "Take a breath:\nyou've got this!",
  "Daily reminder:\nYou are awesome!",
  "Life is better\nwith birthday cake",
  "You have the\nbest smile ever!",
  "Hope your day is\nsuper wonderful!",
  "Stay awesome,\nstay you! :)",
  "High fives and\nbig cheers! :)",
  "You deserve all\nhappiness today!",
  "Eat extra cake,\nDoctor's orders!",
  "You light up every\nroom you enter!",
  "Keep your head up,\nyour crown shines!",
  "Never forget:\nYou rock always!",
  "Sending infinite\nsmiles today! :)",
  "Best adventure\nbuddy ever!",
  "You make the world\na happier place!"
};
const int TOTAL_MESSAGES = 25;
int currentMessageIndex = 0;
unsigned long lastMessageInteraction = 0;

// Blinking face tracking
bool faceBlinking = false;
unsigned long lastFaceBlink = 0;
unsigned long faceBlinkStart = 0;

// ==========================================
// WEATHER DATA
// ==========================================
int currentWeatherCode = 0; // 0=Sunny, 1-3=Clouds, 51+=Rain
float currentTemp = 24.0;
bool weatherUpdated = false;
unsigned long lastWeatherFetch = 0;

void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.setTimeout(4000);
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(CITY_LAT, 2) +
               "&longitude=" + String(CITY_LON, 2) + "&current=temperature_2m,weather_code";
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    int tempIdx = payload.indexOf("\"temperature_2m\":");
    if (tempIdx != -1) {
      currentTemp = payload.substring(tempIdx + 17, payload.indexOf(",", tempIdx)).toFloat();
    }
    int codeIdx = payload.indexOf("\"weather_code\":");
    if (codeIdx != -1) {
      currentWeatherCode = payload.substring(codeIdx + 15, payload.indexOf("}", codeIdx)).toInt();
    }
    weatherUpdated = true;
    Serial.printf("[Weather] Temp: %.1f C, Code: %d\n", currentTemp, currentWeatherCode);
  } else {
    Serial.printf("[Weather] HTTP GET failed, code: %d\n", httpCode);
  }
  http.end();
}

void syncWiFiWithFeedback(bool forceScanIfFail = true) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(6, 4);
  display.print("CONNECTING WI-FI...");
  display.drawFastHLine(0, 15, 128, SSD1306_WHITE);
  display.setCursor(4, 20);
  display.print("SSID: ");
  display.print(WIFI_SSID);
  display.setCursor(4, 32);
  display.print("Connecting");
  display.display();

  Serial.println("[WiFi] Starting connection attempt...");
  Serial.printf("[WiFi] Target SSID: '%s'\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setTxPower(WIFI_POWER_17dBm);
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }

  unsigned long start = millis();
  int dots = 0;
  bool ok = false;
  while (millis() - start < 10000) {
    if (WiFi.status() == WL_CONNECTED) {
      ok = true;
      break;
    }
    dots = (dots + 1) % 4;
    display.fillRect(68, 32, 30, 8, SSD1306_BLACK);
    display.setCursor(68, 32);
    for (int d = 0; d < dots; d++) display.print(".");
    
    int bar = map(millis() - start, 0, 10000, 4, 124);
    display.drawRect(4, 45, 120, 6, SSD1306_WHITE);
    display.fillRect(6, 46, max(0, bar - 4), 4, SSD1306_WHITE);
    display.display();
    delay(350);
  }

  if (ok) {
    Serial.println("[WiFi] Connected successfully!");
    Serial.printf("[WiFi] IP Address: %s\n", WiFi.localIP().toString().c_str());

    display.fillRect(0, 16, 128, 48, SSD1306_BLACK);
    display.setCursor(4, 20);
    display.print("Status: CONNECTED!");
    display.setCursor(4, 34);
    display.print("Syncing Time & NTP...");
    display.display();

    configTime(TIME_ZONE_OFFSET, 0, "pool.ntp.org", "time.google.com");
    struct tm tinfo;
    for (int i = 0; i < 20; i++) {
      if (getLocalTime(&tinfo, 100)) break;
      delay(100);
    }
    display.setCursor(4, 46);
    display.print("Fetching Weather...");
    display.display();
    fetchWeather();
    delay(700);
  } else if (forceScanIfFail) {
    Serial.println("[WiFi] Connection failed. Scanning 2.4GHz networks...");
    display.fillRect(0, 16, 128, 48, SSD1306_BLACK);
    display.setCursor(4, 20);
    display.print("Scanning 2.4GHz...");
    display.display();

    int n = WiFi.scanNetworks();
    bool found = false;
    Serial.printf("[WiFi] Scan finished: found %d networks:\n", n);
    for (int i = 0; i < n; i++) {
      Serial.printf("  [%d] SSID: '%s' | RSSI: %d dBm\n", i, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
      if (WiFi.SSID(i) == WIFI_SSID) {
        found = true;
      }
    }

    display.fillRect(0, 16, 128, 48, SSD1306_BLACK);
    if (!found) {
      display.setCursor(4, 18);
      display.print("SSID NOT FOUND!");
      display.setCursor(4, 30);
      display.print("Hotspot must be 2.4G");
      display.setCursor(4, 42);
      display.print("Enable: Max Compat");
      display.setCursor(4, 54);
      display.print("B1:Menu  B2:Retry");
    } else {
      display.setCursor(4, 18);
      display.print("HOTSPOT DETECTED!");
      display.setCursor(4, 30);
      display.print("Pass/Auth Rejected");
      display.setCursor(4, 42);
      display.print("Check password!");
      display.setCursor(4, 54);
      display.print("B1:Menu  B2:Retry");
    }
    display.display();

    unsigned long waitStart = millis();
    while (millis() - waitStart < 4000) {
      if (digitalRead(BUTTON_POWER) == LOW || digitalRead(BUTTON_MODE) == LOW) break;
      delay(50);
    }
  }
}

// ==========================================
// DEEP SLEEP ROUTINE
// ==========================================
void enterDeepSleep() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Cute sleeping chibi face ( - ω - )
  // Cat ears on top
  display.fillTriangle(34, 12, 44, 12, 39, 4, SSD1306_WHITE);
  display.fillTriangle(84, 12, 94, 12, 89, 4, SSD1306_WHITE);
  display.drawPixel(39, 8, SSD1306_BLACK);
  display.drawPixel(89, 8, SSD1306_BLACK);

  display.drawFastHLine(36, 18, 14, SSD1306_WHITE);
  display.drawFastHLine(78, 18, 14, SSD1306_WHITE);
  // Blushing cheeks
  display.drawLine(40, 24, 43, 21, SSD1306_WHITE);
  display.drawLine(42, 24, 45, 21, SSD1306_WHITE);
  display.drawLine(83, 24, 86, 21, SSD1306_WHITE);
  display.drawLine(85, 24, 88, 21, SSD1306_WHITE);
  // Cute cat mouth ( ω )
  display.drawLine(60, 22, 62, 24, SSD1306_WHITE);
  display.drawLine(62, 24, 64, 22, SSD1306_WHITE);
  display.drawLine(64, 22, 66, 24, SSD1306_WHITE);
  display.drawLine(66, 24, 68, 22, SSD1306_WHITE);

  display.setCursor(32, 38);
  display.print("SEE YOU AI");
  display.setCursor(54, 50);
  display.print("Zzz...");
  display.display();
  delay(1200);

  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);

  while (digitalRead(BUTTON_MODE) == LOW || digitalRead(BUTTON_POWER) == LOW) {
    delay(10);
  }
  delay(120);

  uint64_t wakeupPinMask = (1ULL << BUTTON_MODE) | (1ULL << BUTTON_POWER);
  esp_deep_sleep_enable_gpio_wakeup(wakeupPinMask, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
}

// ==========================================
// ANIMATED BLINKING FACES (KAWAII / CHIBI)
// ==========================================
void drawBlinkingCuteFace(int centerX, int centerY, int faceType) {
  unsigned long now = millis();
  if (!faceBlinking && (now - lastFaceBlink > 2800)) {
    faceBlinking = true;
    faceBlinkStart = now;
    lastFaceBlink = now;
  }
  if (faceBlinking && (now - faceBlinkStart > 180)) {
    faceBlinking = false;
  }

  // 1. Cute Cat / Bear Ears on top
  display.fillTriangle(centerX - 18, centerY - 6, centerX - 10, centerY - 6, centerX - 14, centerY - 12, SSD1306_WHITE);
  display.fillTriangle(centerX + 10, centerY - 6, centerX + 18, centerY - 6, centerX + 14, centerY - 12, SSD1306_WHITE);
  display.drawPixel(centerX - 14, centerY - 8, SSD1306_BLACK);
  display.drawPixel(centerX + 14, centerY - 8, SSD1306_BLACK);

  // 2. Cute blushing cheeks right under the eyes ( // flush marks )
  display.drawLine(centerX - 17, centerY + 4, centerX - 15, centerY + 2, SSD1306_WHITE);
  display.drawLine(centerX - 14, centerY + 4, centerX - 12, centerY + 2, SSD1306_WHITE);
  display.drawLine(centerX + 12, centerY + 4, centerX + 14, centerY + 2, SSD1306_WHITE);
  display.drawLine(centerX + 15, centerY + 4, centerX + 17, centerY + 2, SSD1306_WHITE);

  if (faceBlinking) {
    // Closed happy blinking curves ^ ^
    display.drawLine(centerX - 17, centerY + 1, centerX - 13, centerY - 2, SSD1306_WHITE);
    display.drawLine(centerX - 13, centerY - 2, centerX - 9, centerY + 1, SSD1306_WHITE);
    display.drawLine(centerX + 9, centerY + 1, centerX + 13, centerY - 2, SSD1306_WHITE);
    display.drawLine(centerX + 13, centerY - 2, centerX + 17, centerY + 1, SSD1306_WHITE);
  } else {
    if (faceType % 3 == 0) {
      // 1. Big Sparkly Chibi Anime Eyes ( ◕ ω ◕ )
      for (int ex : {centerX - 13, centerX + 13}) {
        display.fillCircle(ex, centerY, 5, SSD1306_WHITE);
        display.drawPixel(ex - 2, centerY - 2, SSD1306_BLACK);
        display.drawPixel(ex - 1, centerY - 2, SSD1306_BLACK);
        display.drawPixel(ex - 2, centerY - 1, SSD1306_BLACK);
        display.drawPixel(ex + 2, centerY + 2, SSD1306_BLACK);
      }
    } else if (faceType % 3 == 1) {
      // 2. Loving Heart Eyes ( ♡ ω ♡ )
      int hx1 = centerX - 14, hx2 = centerX + 14;
      display.fillCircle(hx1 - 3, centerY - 2, 3, SSD1306_WHITE);
      display.fillCircle(hx1 + 3, centerY - 2, 3, SSD1306_WHITE);
      display.fillTriangle(hx1 - 6, centerY, hx1 + 6, centerY, hx1, centerY + 6, SSD1306_WHITE);

      display.fillCircle(hx2 - 3, centerY - 2, 3, SSD1306_WHITE);
      display.fillCircle(hx2 + 3, centerY - 2, 3, SSD1306_WHITE);
      display.fillTriangle(hx2 - 6, centerY, hx2 + 6, centerY, hx2, centerY + 6, SSD1306_WHITE);
    } else {
      // 3. Playful Starry Wink ( > ‿ ◕ )
      display.drawLine(centerX - 17, centerY - 2, centerX - 11, centerY, SSD1306_WHITE);
      display.drawLine(centerX - 17, centerY + 2, centerX - 11, centerY, SSD1306_WHITE);

      display.fillCircle(centerX + 13, centerY, 5, SSD1306_WHITE);
      display.drawPixel(centerX + 11, centerY - 2, SSD1306_BLACK);
      display.drawPixel(centerX + 12, centerY - 2, SSD1306_BLACK);
      display.drawPixel(centerX + 11, centerY - 1, SSD1306_BLACK);
      display.drawPixel(centerX + 15, centerY + 2, SSD1306_BLACK);

      // Star sparkle next to wink
      display.drawPixel(centerX - 21, centerY - 2, SSD1306_WHITE);
      display.drawLine(centerX - 22, centerY - 2, centerX - 20, centerY - 2, SSD1306_WHITE);
      display.drawLine(centerX - 21, centerY - 3, centerX - 21, centerY - 1, SSD1306_WHITE);
    }
  }

  // 3. Adorable kitten mouth ( ω )
  display.drawLine(centerX - 4, centerY + 3, centerX - 2, centerY + 5, SSD1306_WHITE);
  display.drawLine(centerX - 2, centerY + 5, centerX, centerY + 3, SSD1306_WHITE);
  display.drawLine(centerX, centerY + 3, centerX + 2, centerY + 5, SSD1306_WHITE);
  display.drawLine(centerX + 2, centerY + 5, centerX + 4, centerY + 3, SSD1306_WHITE);
}

// ==========================================
// 1. IDLE PHOTO SCREEN
// ==========================================
void drawIdlePhotoScreen() {
  display.clearDisplay();

  // Full 64x64 photo, nothing covering it
  display.drawBitmap(0, 0, homeBitmaps[getHomeIndex()], 64, 64, SSD1306_WHITE);
  display.drawFastVLine(65, 0, 64, SSD1306_WHITE);

  // Pulsing heart (top of right column)
  bool beat = ((millis() / 450) % 2 == 0);
  int hx = 96, hy = 6;
  if (beat) {
    display.fillCircle(hx - 3, hy, 3, SSD1306_WHITE);
    display.fillCircle(hx + 3, hy, 3, SSD1306_WHITE);
    display.fillTriangle(hx - 6, hy + 2, hx + 6, hy + 2, hx, hy + 7, SSD1306_WHITE);
  } else {
    display.fillCircle(hx - 2, hy, 2, SSD1306_WHITE);
    display.fillCircle(hx + 2, hy, 2, SSD1306_WHITE);
    display.fillTriangle(hx - 4, hy + 1, hx + 4, hy + 1, hx, hy + 5, SSD1306_WHITE);
  }

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(80, 18);
  display.print("HEY");
  display.setCursor(72, 28);
  display.print("SHREYA!");

  if (holdProgress > 0) {
    // Easter-egg hold bar, bottom of right column
    display.setCursor(72, 42);
    display.print("Hold...");
    display.drawRect(68, 54, 56, 7, SSD1306_WHITE);
    display.fillRect(68, 54, (holdProgress * 56) / 120, 7, SSD1306_WHITE);
  } else {
    // Button hints in the right column
    display.setCursor(70, 43);
    display.print("B1:Menu");
    display.setCursor(70, 54);
    display.print("B2:Smiles");
  }

  display.display();
}

// ==========================================
// 2. FORTUNE COOKIE / MESSAGE CARD
// ==========================================
void drawMessageCard() {
  display.clearDisplay();

  // Top: Animated blinking face
  drawBlinkingCuteFace(64, 11, currentMessageIndex);

  // Message body
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Print multi-line message centered
  String msg = shreyaMessages[currentMessageIndex];
  int line1End = msg.indexOf('\n');
  if (line1End != -1) {
    String l1 = msg.substring(0, line1End);
    String l2 = msg.substring(line1End + 1);
    int x1 = max(2, (128 - (int)l1.length() * 6) / 2);
    int x2 = max(2, (128 - (int)l2.length() * 6) / 2);
    display.setCursor(x1, 26);
    display.print(l1);
    display.setCursor(x2, 38);
    display.print(l2);
  } else {
    int x = max(2, (128 - (int)msg.length() * 6) / 2);
    display.setCursor(x, 32);
    display.print(msg);
  }

  // Bottom prompt bar: Button 1 on LEFT, Button 2 on RIGHT
  display.fillRect(0, 52, 128, 12, SSD1306_BLACK);
  display.drawFastHLine(0, 52, 128, SSD1306_WHITE);
  display.setCursor(4, 54);
  display.print("B1:Menu");
  display.setCursor(80, 54);
  display.print("B2:Next");

  display.display();
}

// ==========================================
// 3. PHOTO ALBUM (ALL 9 FULL PHOTOS)
// ==========================================
void drawPhotoAlbum() {
  display.clearDisplay();
  display.drawBitmap(0, 0, albumBitmaps[photoAlbumIndex], 128, 64, SSD1306_WHITE);

  // Brief hint bar overlaid on the photo
  display.fillRect(0, 52, 128, 12, SSD1306_BLACK);
  display.drawFastHLine(0, 52, 128, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 54);
  display.print("B1:Menu");
  if (slideshowOn) {
    display.setCursor(49, 54);
    display.print(">");
  }
  display.setCursor(55, 54);
  display.print(String(photoAlbumIndex + 1) + "/" + String(TOTAL_ALBUM_PHOTOS));
  display.setCursor(80, 54);
  display.print("B2:Next");

  display.display();
}

// ==========================================
// 4. LIVE REAL-WORLD CLOCK & WEATHER
// ==========================================
void drawClockWeather() {
  display.clearDisplay();

  // Header
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Time
  struct tm timeinfo;
  bool gotTime = getLocalTime(&timeinfo, 50);

  if (gotTime) {
    char timeStr[10];
    strftime(timeStr, sizeof(timeStr), "%I:%M %p", &timeinfo);
    display.setTextSize(2);
    display.setCursor(18, 6);
    display.print(timeStr);

    char dateStr[20];
    strftime(dateStr, sizeof(dateStr), "%a, %d %b", &timeinfo);
    display.setTextSize(1);
    display.setCursor(30, 26);
    display.print(dateStr);
  } else {
    // Offline / running timer
    unsigned long secs = millis() / 1000;
    int mins = (secs / 60) % 60;
    int hrs = (secs / 3600) % 24;
    display.setTextSize(2);
    display.setCursor(24, 6);
    if (hrs < 10) display.print("0");
    display.print(hrs);
    display.print(":");
    if (mins < 10) display.print("0");
    display.print(mins);

    display.setTextSize(1);
    display.setCursor(24, 26);
    display.print("(Offline Clock)");
  }

  // Weather & Animated Buddy Accessories at bottom
  display.drawFastHLine(0, 36, 128, SSD1306_WHITE);

  // Left: Buddy Face
  int bx = 16, by = 40;
  display.fillRoundRect(bx, by, 10, 9, 2, SSD1306_WHITE);
  display.fillRoundRect(bx + 12, by, 10, 9, 2, SSD1306_WHITE);
  display.fillCircle(bx + 5, by + 4, 2, SSD1306_BLACK);
  display.fillCircle(bx + 17, by + 4, 2, SSD1306_BLACK);

  // Sunglasses if Sunny / Clear (Code 0, 1)
  if (currentWeatherCode <= 2) {
    display.fillRect(bx - 1, by + 1, 24, 4, SSD1306_BLACK);
    display.drawRect(bx - 1, by + 1, 24, 4, SSD1306_WHITE);
    display.drawFastHLine(bx + 9, by + 3, 4, SSD1306_WHITE); // bridge
  }
  // Umbrella if Rain (Code >= 50)
  else if (currentWeatherCode >= 50) {
    display.drawCircleHelper(bx + 26, by, 7, 1 | 2, SSD1306_WHITE);
    display.drawFastVLine(bx + 26, by, 9, SSD1306_WHITE); // handle
  }

  // Right: Temperature and status
  display.setTextSize(1);
  display.setCursor(44, 40);
  if (WiFi.status() == WL_CONNECTED) {
    if (weatherUpdated) {
      display.print((int)currentTemp);
      display.print("C ");
      if (currentWeatherCode <= 2) display.print("Sunny!");
      else if (currentWeatherCode >= 50) display.print("Rainy!");
      else display.print("Cloudy");
    } else {
      display.print("Syncing...");
    }
  } else {
    display.print("WiFi: Offline");
  }

  // Bottom prompt bar: Button 1 on LEFT, Button 2 on RIGHT
  display.fillRect(0, 52, 128, 12, SSD1306_BLACK);
  display.drawFastHLine(0, 52, 128, SSD1306_WHITE);
  display.setCursor(4, 54);
  display.print("B1:Menu");
  display.setCursor(64, 54);
  display.print("B2:Connect");

  display.display();
}

// ==========================================
// ARCADE MENU
// ==========================================
void drawMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(10, 2);
  display.print("=== SHREYA'S ARCADE ===");
  display.drawFastHLine(0, 11, 128, SSD1306_WHITE);

  int startIdx = max(0, min(menuCursor - 1, TOTAL_MENU_ITEMS - 4));
  for (int i = 0; i < 4 && (startIdx + i) < TOTAL_MENU_ITEMS; i++) {
    int idx = startIdx + i;
    int y = 15 + i * 10;
    if (idx == menuCursor) {
      display.fillRect(2, y - 1, 124, 9, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(6, y);
      display.print("> ");
      display.print(menuItems[idx]);
      display.setTextColor(SSD1306_WHITE);
    } else {
      display.setCursor(14, y);
      display.print(menuItems[idx]);
    }
  }

  // Bottom prompt bar: Button 1 on LEFT, Button 2 on RIGHT
  display.fillRect(0, 53, 128, 11, SSD1306_BLACK);
  display.drawFastHLine(0, 53, 128, SSD1306_WHITE);
  display.setCursor(4, 55);
  display.print("B1:Next");
  display.setCursor(72, 55);
  display.print("B2:Select");
  display.display();
}

// ==========================================
// GAME OVER SCREEN
// ==========================================
void showGameOver(int score, int best) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(35, 4);
  display.print("GAME OVER");

  display.setCursor(20, 20);
  display.print("SCORE: ");
  display.print(score);

  display.setCursor(20, 32);
  display.print("BEST:  ");
  display.print(best);

  // Cute face
  display.drawRoundRect(92, 22, 10, 12, 3, SSD1306_WHITE);
  display.drawRoundRect(105, 22, 10, 12, 3, SSD1306_WHITE);
  if (score >= best && score > 0) {
    display.fillCircle(96, 27, 2, SSD1306_WHITE);
    display.fillCircle(109, 27, 2, SSD1306_WHITE);
  } else {
    display.fillCircle(97, 28, 2, SSD1306_WHITE);
    display.fillCircle(110, 28, 2, SSD1306_WHITE);
  }

  // Bottom prompt bar: Button 1 on LEFT, Button 2 on RIGHT
  display.fillRect(0, 48, 128, 16, SSD1306_BLACK);
  display.drawFastHLine(0, 49, 128, SSD1306_WHITE);
  display.setCursor(4, 53);
  display.print("B1:Menu");
  display.setCursor(64, 53);
  display.print("B2:Restart");
  display.display();
}

// ==========================================
// GAME 1: SHREYA RUN (AVATAR RUNNER)
// ==========================================
float dinoY = 44;
float dinoVY = 0;
bool dinoJumping = false;
int cactusX1 = 130, cactusW1 = 6, cactusH1 = 12;
int cactusX2 = 200, cactusW2 = 8, cactusH2 = 14;
float dinoSpeed = 3.5;

void initDino() {
  dinoY = 44; dinoVY = 0; dinoJumping = false;
  cactusX1 = 130; cactusX2 = 205; dinoSpeed = 3.5; currentScore = 0;
}

void drawShreyaRunner(int x, int y, bool jumping, int frame) {
  // Hair + Head
  display.fillRoundRect(x + 1, y, 7, 6, 2, SSD1306_WHITE);
  display.drawPixel(x + 5, y + 2, SSD1306_BLACK); // Eye
  display.fillRect(x - 2, y + 1, 3, 3, SSD1306_WHITE); // Ponytail puff

  // Dress
  display.fillTriangle(x + 4, y + 5, x + 1, y + 10, x + 7, y + 10, SSD1306_WHITE);

  // Running legs
  if (jumping) {
    display.drawLine(x + 2, y + 10, x, y + 13, SSD1306_WHITE);
    display.drawLine(x + 5, y + 10, x + 7, y + 12, SSD1306_WHITE);
  } else {
    if (frame == 0) {
      display.drawLine(x + 2, y + 10, x + 1, y + 13, SSD1306_WHITE);
      display.drawLine(x + 5, y + 10, x + 6, y + 12, SSD1306_WHITE);
    } else {
      display.drawLine(x + 2, y + 10, x + 3, y + 12, SSD1306_WHITE);
      display.drawLine(x + 5, y + 10, x + 4, y + 13, SSD1306_WHITE);
    }
  }
}

void updateDino() {
  if (digitalRead(BUTTON_MODE) == LOW && !dinoJumping) {
    dinoVY = -6.2; dinoJumping = true;
  }
  dinoY += dinoVY; dinoVY += 0.55;
  if (dinoY >= 44) { dinoY = 44; dinoVY = 0; dinoJumping = false; }

  cactusX1 -= (int)dinoSpeed; cactusX2 -= (int)dinoSpeed;
  if (cactusX1 < -10) { cactusX1 = 130 + random(0, 40); cactusH1 = random(10, 16); currentScore++; }
  if (cactusX2 < -10) { cactusX2 = cactusX1 + 70 + random(20, 60); cactusH2 = random(10, 16); currentScore++; }
  dinoSpeed = 3.5 + (currentScore / 15.0);

  int dinoBoxL = 16, dinoBoxR = 26, dinoBoxT = (int)dinoY, dinoBoxB = (int)dinoY + 14;
  bool hit1 = !(dinoBoxR < cactusX1 || dinoBoxL > cactusX1 + cactusW1 || dinoBoxB < 56 - cactusH1);
  bool hit2 = !(dinoBoxR < cactusX2 || dinoBoxL > cactusX2 + cactusW2 || dinoBoxB < 56 - cactusH2);

  if (hit1 || hit2) {
    if (currentScore > bestDino) { bestDino = currentScore; saveScore("dino", bestDino); }
    gameBeforeGameOver = STATE_DINO; currentState = STATE_GAMEOVER; return;
  }

  display.clearDisplay();
  display.drawFastHLine(0, 56, 128, SSD1306_WHITE);

  // Draw custom Shreya Runner!
  int runFrame = ((millis() / 80) % 2);
  drawShreyaRunner(16, (int)dinoY, dinoJumping, runFrame);

  display.fillRect(cactusX1, 56 - cactusH1, cactusW1, cactusH1, SSD1306_WHITE);
  display.fillRect(cactusX2, 56 - cactusH2, cactusW2, cactusH2, SSD1306_WHITE);

  display.setTextSize(1); display.setTextColor(SSD1306_WHITE); display.setCursor(75, 4);
  display.print("SCR:"); display.print(currentScore);
  display.display();
}

// ==========================================
// GAME 2: FLAPPY BALLOON (DODGING CANDLES)
// ==========================================
float flappyY = 28; float flappyVY = 0;
int candleX[2]; int candleGapY[2]; const int candleW = 12; const int gapH = 26;

void initFlappy() {
  flappyY = 28; flappyVY = 0;
  candleX[0] = 130; candleGapY[0] = random(12, 32);
  candleX[1] = 130 + 75; candleGapY[1] = random(12, 32); currentScore = 0;
}

void updateFlappy() {
  if (digitalRead(BUTTON_MODE) == LOW) flappyVY = -3.2;
  flappyY += flappyVY; flappyVY += 0.38;

  for (int i = 0; i < 2; i++) {
    candleX[i] -= 2;
    if (candleX[i] + candleW == 24) currentScore++;
    if (candleX[i] < -candleW) { candleX[i] = 130; candleGapY[i] = random(10, 32); }
  }

  if (flappyY < 0 || flappyY > 54) {
    if (currentScore > bestFlappy) { bestFlappy = currentScore; saveScore("flappy", bestFlappy); }
    gameBeforeGameOver = STATE_FLAPPY; currentState = STATE_GAMEOVER; return;
  }

  for (int i = 0; i < 2; i++) {
    if (24 + 10 > candleX[i] && 24 < candleX[i] + candleW) {
      if (flappyY < candleGapY[i] || flappyY + 12 > candleGapY[i] + gapH) {
        if (currentScore > bestFlappy) { bestFlappy = currentScore; saveScore("flappy", bestFlappy); }
        gameBeforeGameOver = STATE_FLAPPY; currentState = STATE_GAMEOVER; return;
      }
    }
  }

  display.clearDisplay();

  // Draw Birthday Candles with Flickering Flames!
  for (int i = 0; i < 2; i++) {
    // Top candle
    display.fillRect(candleX[i], 0, candleW, candleGapY[i] - 4, SSD1306_WHITE);
    display.drawLine(candleX[i] + candleW/2, candleGapY[i] - 4, candleX[i] + candleW/2, candleGapY[i] - 2, SSD1306_WHITE);
    // Flickering flame top
    if ((millis() / 100) % 2 == 0) {
      display.fillCircle(candleX[i] + candleW/2, candleGapY[i] - 1, 2, SSD1306_WHITE);
    } else {
      display.drawPixel(candleX[i] + candleW/2, candleGapY[i] - 1, SSD1306_WHITE);
    }

    // Bottom candle
    int by = candleGapY[i] + gapH;
    display.fillRect(candleX[i], by + 4, candleW, 64 - (by + 4), SSD1306_WHITE);
    display.drawLine(candleX[i] + candleW/2, by + 2, candleX[i] + candleW/2, by + 4, SSD1306_WHITE);
    // Flickering flame bottom
    if ((millis() / 120) % 2 == 0) {
      display.fillCircle(candleX[i] + candleW/2, by + 1, 2, SSD1306_WHITE);
    } else {
      display.drawPixel(candleX[i] + candleW/2, by + 1, SSD1306_WHITE);
    }
  }

  // Draw Flying Birthday Balloon with Shreya hanging on!
  int bx = 24, by = (int)flappyY;
  display.fillCircle(bx + 4, by + 4, 4, SSD1306_WHITE); // Balloon
  display.drawPixel(bx + 3, by + 2, SSD1306_BLACK);     // Highlight
  display.drawFastVLine(bx + 4, by + 8, 4, SSD1306_WHITE); // String
  // Mini Shreya head holding string
  display.fillCircle(bx + 4, by + 13, 2, SSD1306_WHITE);

  display.setTextSize(1); display.setCursor(4, 4); display.print("SCR:"); display.print(currentScore);
  display.display();
}

// ==========================================
// GAME 3: CLASSIC SNAKE
// ==========================================
int snakeX[64]; int snakeY[64]; int snakeLen = 4; int snakeDir = 0; int foodX = 40, foodY = 30; unsigned long lastSnakeTick = 0;

void initSnake() {
  snakeLen = 4; snakeDir = 0;
  for (int i = 0; i < snakeLen; i++) { snakeX[i] = 40 - i * 4; snakeY[i] = 32; }
  foodX = (random(2, 28) * 4); foodY = (random(2, 14) * 4); currentScore = 0;
}

void updateSnake() {
  static bool lastPBtn = HIGH, lastMBtn = HIGH;
  bool pBtn = digitalRead(BUTTON_POWER), mBtn = digitalRead(BUTTON_MODE);
  if (pBtn == LOW && lastPBtn == HIGH) snakeDir = (snakeDir + 3) % 4; // Turn Left
  if (mBtn == LOW && lastMBtn == HIGH) snakeDir = (snakeDir + 1) % 4; // Turn Right
  lastPBtn = pBtn; lastMBtn = mBtn;

  if (millis() - lastSnakeTick > 100) {
    for (int i = snakeLen - 1; i > 0; i--) { snakeX[i] = snakeX[i - 1]; snakeY[i] = snakeY[i - 1]; }
    if (snakeDir == 0) snakeX[0] += 4; else if (snakeDir == 1) snakeY[0] += 4;
    else if (snakeDir == 2) snakeX[0] -= 4; else if (snakeDir == 3) snakeY[0] -= 4;

    if (snakeX[0] < 0 || snakeX[0] >= 128 || snakeY[0] < 0 || snakeY[0] >= 64) {
      if (currentScore > bestSnake) { bestSnake = currentScore; saveScore("snake", bestSnake); }
      gameBeforeGameOver = STATE_SNAKE; currentState = STATE_GAMEOVER; return;
    }
    for (int i = 1; i < snakeLen; i++) {
      if (snakeX[0] == snakeX[i] && snakeY[0] == snakeY[i]) {
        if (currentScore > bestSnake) { bestSnake = currentScore; saveScore("snake", bestSnake); }
        gameBeforeGameOver = STATE_SNAKE; currentState = STATE_GAMEOVER; return;
      }
    }
    if (abs(snakeX[0] - foodX) < 4 && abs(snakeY[0] - foodY) < 4) {
      if (snakeLen < 60) snakeLen++;
      currentScore += 10;
      foodX = (random(2, 28) * 4); foodY = (random(2, 14) * 4);
    }
    lastSnakeTick = millis();
  }

  display.clearDisplay();
  display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  display.fillRect(foodX, foodY, 3, 3, SSD1306_WHITE);
  for (int i = 0; i < snakeLen; i++) display.fillRect(snakeX[i], snakeY[i], 3, 3, SSD1306_WHITE);
  display.setTextSize(1); display.setCursor(4, 2); display.print("S:"); display.print(currentScore);
  display.display();
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_POWER, INPUT_PULLUP);
  pinMode(BUTTON_MODE,  INPUT_PULLUP);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not detected!");
    while (true);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  loadHighScores();

  // Try Wi-Fi in background (Non-blocking)
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setTxPower(WIFI_POWER_17dBm);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTime(TIME_ZONE_OFFSET, 0, "pool.ntp.org", "time.google.com");

  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
  if (wakeupReason == ESP_SLEEP_WAKEUP_GPIO) {
    display.clearDisplay();
    drawBlinkingCuteFace(64, 20, 0);
    display.setTextSize(1);
    display.setCursor(40, 42);
    display.print("HELLO AI");
    display.display();
    delay(600);
  } else {
    display.clearDisplay();
    drawBlinkingCuteFace(64, 18, 0);
    display.setTextSize(2);
    display.setCursor(18, 38);
    display.println("HELLO AI");
    display.display();
    delay(1200);
  }

  currentState = STATE_IDLE_PHOTO;
  drawIdlePhotoScreen();
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  bool pState = digitalRead(BUTTON_POWER); // Button 1 (GPIO 5, LEFT)
  bool mState = digitalRead(BUTTON_MODE);  // Button 2 (GPIO 4, RIGHT)

  // Periodically fetch weather if Wi-Fi connected, or retry Wi-Fi every 45s if disconnected
  static unsigned long lastWiFiRetry = 0;
  if (WiFi.status() == WL_CONNECTED) {
    if (millis() - lastWeatherFetch > 600000 || !weatherUpdated) {
      fetchWeather();
      lastWeatherFetch = millis();
    }
  } else {
    if (millis() - lastWiFiRetry > 45000) {
      lastWiFiRetry = millis();
      WiFi.reconnect();
    }
  }

  // --- BUTTON 1: Deep Sleep on Hold (1 sec) ---
  if (pState == LOW && lastPowerState == HIGH) {
    powerPressTime = millis();
    powerLongHandled = false;
  }
  if (pState == LOW && !powerLongHandled && (millis() - powerPressTime > 1000)) {
    powerLongHandled = true;
    enterDeepSleep();
    return;
  }

  // --- SHORT CLICK DETECTION ---
  bool pClicked = false;
  if (pState == HIGH && lastPowerState == LOW) {
    unsigned long pDur = millis() - powerPressTime;
    if (!powerLongHandled && pDur > 30 && pDur < 1000) {
      pClicked = true;
    }
  }

  bool mClicked = false;
  if (mState == LOW && lastModeState == HIGH) {
    modePressTime = millis();
    modeHold1Fired = false;
    modeHold3Fired = false;
  }
  if (mState == HIGH && lastModeState == LOW) {
    unsigned long mDur = millis() - modePressTime;
    if (mDur > 30 && mDur < 800) {
      mClicked = true;
    }
  }

  // B2 long-press detection (1 s and 3 s)
  bool mLong1 = false, mLong3 = false;
  unsigned long mHeld = (mState == LOW) ? (millis() - modePressTime) : 0;
  if (mState == LOW) {
    if (!modeHold1Fired && mHeld > 1000) { modeHold1Fired = true; mLong1 = true; }
    if (!modeHold3Fired && mHeld > 3000) { modeHold3Fired = true; mLong3 = true; }
  }

  lastPowerState = pState;
  lastModeState = mState;

  // ==========================================
  // STATE ROUTING
  // ==========================================
  switch (currentState) {
    case STATE_IDLE_PHOTO: {
      if (pClicked) {
        holdProgress = 0;
        currentState = STATE_MENU;
        menuCursor = 0;
        drawMenu();
        return;
      }
      if (mLong3) {
        // Easter egg unlocked!
        holdProgress = 0;
        startSecret();
        drawSecretPage();
        return;
      }
      if (mClicked) {
        holdProgress = 0;
        currentState = STATE_MESSAGES;
        currentMessageIndex = random(0, TOTAL_MESSAGES);
        lastMessageInteraction = millis();
        drawMessageCard();
        return;
      }

      // Progress bar appears after a 0.7 s hold, fills until 3 s
      if (mState == LOW && mHeld > 700) {
        long hp = constrain((long)mHeld, 700L, 3000L);
        holdProgress = map(hp, 700, 3000, 2, 120);
      } else {
        holdProgress = 0;
      }

      static unsigned long lastPulse = 0;
      unsigned long interval = holdProgress ? 80 : 450;
      if (millis() - lastPulse > interval) {
        drawIdlePhotoScreen();
        lastPulse = millis();
      }
      break;
    }

    case STATE_MESSAGES: {
      if (pClicked) {
        // Button 1 (Left): Return to Menu
        currentState = STATE_MENU;
        menuCursor = 0;
        drawMenu();
        return;
      }
      if (mClicked) {
        // Button 2 (Right): Roll Next Fortune Cookie Message!
        currentMessageIndex = (currentMessageIndex + random(1, TOTAL_MESSAGES)) % TOTAL_MESSAGES;
        lastMessageInteraction = millis();
        drawMessageCard();
        return;
      }

      // Keep animated face blinking smoothly
      drawMessageCard();
      // Auto-return to Shreya's Photo after 18 seconds of idle
      if (millis() - lastMessageInteraction > 18000) {
        currentState = STATE_IDLE_PHOTO;
        drawIdlePhotoScreen();
      }
      delay(40);
      break;
    }

    case STATE_ALBUM: {
      if (pClicked) {
        slideshowOn = false;
        currentState = STATE_MENU;
        menuCursor = 0;
        drawMenu();
        return;
      }
      if (mLong1) {
        // Hold B2 for 1 s: toggle slideshow
        slideshowOn = !slideshowOn;
        lastSlideTime = millis();
        showToast(slideshowOn ? "SLIDESHOW ON" : "SLIDESHOW OFF");
        drawPhotoAlbum();
        return;
      }
      if (mClicked) {
        photoAlbumIndex = (photoAlbumIndex + 1) % TOTAL_ALBUM_PHOTOS;
        lastSlideTime = millis();
        wipeToPhoto(photoAlbumIndex);
        drawPhotoAlbum();
        return;
      }
      if (slideshowOn && millis() - lastSlideTime > SLIDE_INTERVAL) {
        photoAlbumIndex = (photoAlbumIndex + 1) % TOTAL_ALBUM_PHOTOS;
        lastSlideTime = millis();
        wipeToPhoto(photoAlbumIndex);
        drawPhotoAlbum();
      }
      break;
    }

    case STATE_CLOCK: {
      if (pClicked) {
        // Button 1 (Left): Return to Menu
        currentState = STATE_MENU;
        menuCursor = 0;
        drawMenu();
        return;
      }
      if (mClicked) {
        // Button 2 (Right): Trigger interactive Wi-Fi sync with diagnostic feedback
        syncWiFiWithFeedback(true);
        drawClockWeather();
        return;
      }

      drawClockWeather();
      delay(400);
      break;
    }

    case STATE_MENU: {
      if (pClicked) {
        // Button 1 (Left): Scroll to Next Menu Item
        menuCursor = (menuCursor + 1) % TOTAL_MENU_ITEMS;
        drawMenu();
        return;
      }
      if (mClicked) {
        // Button 2 (Right): Select Highlighted Item
        switch (menuCursor) {
          case 0: initDino();   currentState = STATE_DINO;   break;
          case 1: initFlappy(); currentState = STATE_FLAPPY; break;
          case 2: initSnake();  currentState = STATE_SNAKE;  break;
          case 3: 
            currentState = STATE_CLOCK;
            if (WiFi.status() != WL_CONNECTED) {
              syncWiFiWithFeedback(true);
            }
            drawClockWeather();
            break;
          case 4: 
            photoAlbumIndex = 0;
            slideshowOn = false;
            currentState = STATE_ALBUM;
            drawPhotoAlbum();
            break;
          case 5:
            // Cycle: photo 1..9, then Photo of the Day, then back to photo 1
            homePhotoIndex = (homePhotoIndex + 1) % (TOTAL_ALBUM_PHOTOS + 1);
            prefs.begin("shreya", false);
            prefs.putInt("homePic", homePhotoIndex);
            prefs.end();
            display.clearDisplay();
            display.drawRect(4, 12, 120, 40, SSD1306_WHITE);
            display.fillRect(5, 13, 118, 38, SSD1306_BLACK);
            display.setTextSize(1);
            display.setTextColor(SSD1306_WHITE);
            display.setCursor(14, 20);
            display.print("Home Pic Mode:");
            display.setCursor(16, 32);
            if (homePhotoIndex == HOME_MODE_DAILY) {
              display.print("Photo of the Day");
              display.setCursor(16, 43);
              display.print("(new one daily)");
            } else {
              display.print(photoTitles[homePhotoIndex]);
            }
            display.display();
            delay(1200);
            drawMenu();
            break;
          case 6: 
            currentState = STATE_IDLE_PHOTO;
            drawIdlePhotoScreen();
            break;
        }
        return;
      }
      break;
    }

    case STATE_SECRET: {
      if (pClicked) {
        currentState = STATE_IDLE_PHOTO;
        holdProgress = 0;
        drawIdlePhotoScreen();
        return;
      }
      if (mClicked) {
        int len = strlen(secretPages[secretPage]);
        if ((int)((millis() - secretStart) / TYPE_MS) < len) {
          // still typing: skip to full text
          secretStart = millis() - (unsigned long)len * TYPE_MS;
        } else if (++secretPage >= TOTAL_SECRET_PAGES) {
          currentState = STATE_IDLE_PHOTO;
          holdProgress = 0;
          drawIdlePhotoScreen();
          return;
        } else {
          secretStart = millis();
        }
      }
      drawSecretPage();
      delay(40);
      break;
    }

    case STATE_DINO:
      updateDino();
      delay(25);
      break;

    case STATE_FLAPPY:
      updateFlappy();
      delay(30);
      break;

    case STATE_SNAKE:
      updateSnake();
      delay(20);
      break;

    case STATE_GAMEOVER: {
      int best = 0;
      if (gameBeforeGameOver == STATE_DINO) best = bestDino;
      else if (gameBeforeGameOver == STATE_FLAPPY) best = bestFlappy;
      else if (gameBeforeGameOver == STATE_SNAKE) best = bestSnake;

      showGameOver(currentScore, best);

      if (pClicked) {
        // Button 1 (Left): Return to Menu
        currentState = STATE_MENU;
        menuCursor = 0;
        drawMenu();
        return;
      }
      if (mClicked) {
        // Button 2 (Right): Replay current game
        if (gameBeforeGameOver == STATE_DINO)        { initDino();   currentState = STATE_DINO; }
        else if (gameBeforeGameOver == STATE_FLAPPY) { initFlappy(); currentState = STATE_FLAPPY; }
        else if (gameBeforeGameOver == STATE_SNAKE)  { initSnake();  currentState = STATE_SNAKE; }
        return;
      }
      break;
    }
  }
}
