/*
 * ============================================================
 *              AXOLOTL ADVENTURE  by AliceFriend
 * ============================================================
 *  For the original "Cheap Yellow Display" (ESP32-2432S028R)
 *  ILI9341 320x240 screen + XPT2046 touch + speaker + RGB LED
 *
 *  Libraries (install from Arduino Library Manager):
 *  (see README.md for full setup steps)
 *    - TFT_eSPI            by Bodmer  (copy TFT_eSPI_Setup/User_Setup.h into the library)
 *    - XPT2046_Touchscreen by Paul Stoffregen
 *  Board: "ESP32 Dev Module"
 *
 *  HOW TO PLAY
 *    Touch the TOP half of the screen    -> swim up
 *    Touch the BOTTOM half of the screen -> swim down
 *    Dodge rocks, seaweed and fish. Collect bubbles and hearts!
 * ============================================================
 */

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include "GameTypes.h"   // all game types live here

// ---------------- Hardware pins (original CYD) ----------------
#define XPT2046_IRQ   36
#define XPT2046_MOSI  32
#define XPT2046_MISO  39
#define XPT2046_CLK   25
#define XPT2046_CS    33
#define SPEAKER_PIN   26
#define LED_R_PIN     4
#define LED_G_PIN     16
#define LED_B_PIN     17
#define BACKLIGHT_PIN 21

// ---------------- Touch calibration ----------------
// If touches feel "off", set TOUCH_DEBUG to 1. A red dot shows where the
// CYD thinks you touched, plus the raw numbers. Adjust the MIN/MAX values.
#define TOUCH_X_MIN   200
#define TOUCH_X_MAX   3700
#define TOUCH_Y_MIN   240
#define TOUCH_Y_MAX   3800
#define TOUCH_FLIP_X  0     // set to 1 if left/right are reversed
#define TOUCH_FLIP_Y  0     // set to 1 if up/down are reversed
#define TOUCH_DEBUG   0

// ---------------- Game tuning ----------------
static const int   W        = 320;
static const int   H        = 240;
static const int   SAND_Y   = 214;   // where the sea floor starts
static const int   PLAYER_X = 72;    // axolotl stays at this x
static const float IMPULSE  = 110;   // instant push when you tap
static const float SWIM_ACC = 420;   // push while holding
static const float MAX_VY   = 165;   // max vertical speed

SPIClass            touchSPI(VSPI);
XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI            tft;
TFT_eSprite         fb(&tft);        // full-screen frame buffer = no flicker
Preferences         prefs;

// ---------------- Colors ----------------
constexpr uint16_t RGB(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
const uint16_t C_WHITE      = RGB(255, 255, 255);
const uint16_t C_BLACK      = 0;
const uint16_t C_SAND       = RGB(240, 210, 140);
const uint16_t C_SAND_DARK  = RGB(200, 165, 100);
const uint16_t C_PEBBLE     = RGB(165, 135, 95);
const uint16_t C_ROCK       = RGB(130, 130, 145);
const uint16_t C_ROCK_LIGHT = RGB(190, 190, 205);
const uint16_t C_ROCK_DARK  = RGB(80, 80, 100);
const uint16_t C_MOSS       = RGB(90, 185, 90);
const uint16_t C_WEED       = RGB(40, 195, 80);
const uint16_t C_WEED_DARK  = RGB(20, 140, 60);
const uint16_t C_HILL       = RGB(20, 90, 125);
const uint16_t C_BUBBLE     = RGB(200, 240, 255);
const uint16_t C_HEART      = RGB(255, 60, 90);
const uint16_t C_GOLD       = RGB(255, 215, 40);
const uint16_t C_PANEL      = RGB(20, 40, 95);
const uint16_t C_BTN_SHADOW = RGB(0, 35, 70);
const uint16_t C_GREEN_BTN  = RGB(40, 170, 80);
const uint16_t C_BLUE_BTN   = RGB(40, 110, 215);
const uint16_t C_PURPLE_BTN = RGB(150, 80, 205);
const uint16_t C_ORANGE_BTN = RGB(240, 130, 30);
const uint16_t C_GREY_BTN   = RGB(110, 110, 125);
const uint16_t WATER[6] = { RGB(120, 215, 240), RGB(90, 195, 230), RGB(60, 170, 220),
                            RGB(40, 145, 205),  RGB(30, 120, 185), RGB(20, 95, 165) };
const uint16_t FISH_COLORS[3] = { RGB(255, 140, 40), RGB(255, 220, 50), RGB(190, 110, 235) };

// ---------------- Skins ----------------
const Skin SKINS[] = {
  {"Pinky",    RGB(255, 160, 190), RGB(255, 215, 230), RGB(235, 70, 130),  C_BLACK,          0,  false},
  {"Goldie",   RGB(255, 205, 70),  RGB(255, 240, 170), RGB(255, 120, 30),  C_BLACK,          0,  false},
  {"Wild",     RGB(105, 120, 70),  RGB(160, 175, 110), RGB(75, 60, 35),    RGB(230, 190, 40), 0, false},
  {"Sky",      RGB(90, 150, 255),  RGB(175, 205, 255), RGB(40, 70, 200),   C_BLACK,          15, false},
  {"Minty",    RGB(110, 225, 160), RGB(200, 255, 220), RGB(30, 150, 90),   C_BLACK,          25, false},
  {"Midnight", RGB(90, 60, 165),   RGB(150, 120, 220), RGB(255, 120, 220), C_WHITE,          40, false},
  {"Rainbow",  0, 0, 0,                                                     C_BLACK,          60, true},
};
const int N_SKINS = sizeof(SKINS) / sizeof(SKINS[0]);

// ---------------- Levels ----------------
const Level LEVELS[3] = {
  {"Easy",   60, 115, 170, 240, 5, 0.5f},
  {"Normal", 78, 145, 140, 205, 3, 1.0f},
  {"Zoom!",  98, 180, 120, 175, 3, 1.3f},
};
const char* VOL_NAMES[3] = {"Off", "Quiet", "Loud"};

// ============================================================
//   SOUND ENGINE  (runs in its own task so timing stays exact)
// ============================================================

const Note SND_CLICK[]     = {{1800, 25}};
const Note SND_SELECT[]    = {{988, 60}, {1319, 120}};
const Note SND_UP[]        = {{700, 30}, {1050, 30}};
const Note SND_DOWN[]      = {{700, 30}, {480, 30}};
const Note SND_PASS[]      = {{2400, 12}};
const Note SND_BUBBLE[]    = {{1320, 40}, {1760, 70}};
const Note SND_HEART[]     = {{880, 60}, {1109, 60}, {1319, 60}, {1760, 140}};
const Note SND_HIT[]       = {{330, 70}, {247, 70}, {165, 140}};
const Note SND_BEEP[]      = {{880, 120}};
const Note SND_GO[]        = {{1319, 260}};
const Note SND_MILESTONE[] = {{1047, 70}, {1319, 70}, {1568, 70}, {2093, 160}};
const Note SND_OVER[]      = {{784, 160}, {659, 160}, {523, 160}, {392, 360}};
const Note SND_RECORD[]    = {{523, 100}, {659, 100}, {784, 100}, {1047, 180}, {0, 60}, {784, 100}, {1047, 360}};
const Note SND_START[]     = {{523, 90}, {659, 90}, {784, 90}, {1047, 200}};
const Note SND_LOCKED[]    = {{220, 90}, {0, 40}, {180, 140}};
const Note SND_SQUEAK[]    = {{1500, 40}, {2000, 40}, {1700, 60}};
#define SND(x) x, (uint8_t)(sizeof(x) / sizeof(x[0]))

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  #define SPK_CH SPEAKER_PIN      // core 3.x uses the pin number
#else
  #define SPK_CH 0                // core 2.x uses a channel number
#endif

volatile uint8_t  volume = 2;             // 0 off, 1 quiet, 2 loud
portMUX_TYPE      sndMux = portMUX_INITIALIZER_UNLOCKED;
const Note*       sndReqSeq = nullptr;
uint8_t           sndReqLen = 0;
volatile bool     sndBusy = false;
volatile uint8_t  sndBusyPrio = 0;

void speakerInit() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(SPEAKER_PIN, 2000, 8);
#else
  ledcSetup(SPK_CH, 2000, 8);
  ledcAttachPin(SPEAKER_PIN, SPK_CH);
#endif
  ledcWrite(SPK_CH, 0);
}

void speakerTone(uint16_t f) {
  if (f == 0 || volume == 0) { ledcWrite(SPK_CH, 0); return; }
  ledcChangeFrequency(SPK_CH, f, 8);
  ledcWrite(SPK_CH, volume == 1 ? 10 : 128);   // smaller duty = quieter
}

// prio: higher numbers can interrupt lower ones
void playSound(const Note* seq, uint8_t len, uint8_t prio = 1) {
  if (sndBusy && prio < sndBusyPrio) return;
  portENTER_CRITICAL(&sndMux);
  sndReqSeq = seq;
  sndReqLen = len;
  sndBusyPrio = prio;
  sndBusy = true;
  portEXIT_CRITICAL(&sndMux);
}

void soundTask(void*) {
  const Note* seq = nullptr;
  uint8_t len = 0, pos = 0;
  uint32_t noteEnd = 0;
  for (;;) {
    const Note* req = nullptr;
    uint8_t reqLen = 0;
    portENTER_CRITICAL(&sndMux);
    if (sndReqSeq) { req = sndReqSeq; reqLen = sndReqLen; sndReqSeq = nullptr; }
    portEXIT_CRITICAL(&sndMux);

    uint32_t now = millis();
    if (req) {
      seq = req; len = reqLen; pos = 0;
      speakerTone(seq[0].f);
      noteEnd = now + seq[0].ms;
    } else if (seq && (int32_t)(now - noteEnd) >= 0) {
      pos++;
      if (pos >= len) {
        seq = nullptr;
        speakerTone(0);
        sndBusy = false;
        sndBusyPrio = 0;
      } else {
        speakerTone(seq[pos].f);
        noteEnd = now + seq[pos].ms;
      }
    }
    vTaskDelay(1);
  }
}

// ============================================================
//   RGB LED  (active LOW on the CYD)
// ============================================================
uint32_t ledOffAt = 0;
void ledSet(bool r, bool g, bool b) {
  digitalWrite(LED_R_PIN, r ? LOW : HIGH);
  digitalWrite(LED_G_PIN, g ? LOW : HIGH);
  digitalWrite(LED_B_PIN, b ? LOW : HIGH);
}
void ledFlash(bool r, bool g, bool b, uint16_t ms) {
  ledSet(r, g, b);
  ledOffAt = millis() + ms;
  if (ledOffAt == 0) ledOffAt = 1;
}
void ledUpdate(uint32_t now) {
  if (ledOffAt && (int32_t)(now - ledOffAt) >= 0) { ledSet(0, 0, 0); ledOffAt = 0; }
}

// ============================================================
//   TOUCH
// ============================================================
bool tDown = false, tPressed = false, prevDown = false;
int tx = 0, ty = 0, rawX = 0, rawY = 0;
uint32_t lastTouchMs = 0, inputLockUntil = 0;

void lockInput(uint16_t ms) { inputLockUntil = millis() + ms; }

void readTouch(uint32_t now) {
  if (touch.touched()) {
    TS_Point p = touch.getPoint();
    rawX = p.x; rawY = p.y;
    int x = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, W - 1);
    int y = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, H - 1);
    if (TOUCH_FLIP_X) x = W - 1 - x;
    if (TOUCH_FLIP_Y) y = H - 1 - y;
    tx = constrain(x, 0, W - 1);
    ty = constrain(y, 0, H - 1);
    lastTouchMs = now;
  }
  // small grace period smooths out resistive-touch dropouts
  bool down = lastTouchMs != 0 && (now - lastTouchMs) < 70;
  tPressed = down && !prevDown && (int32_t)(now - inputLockUntil) >= 0;
  prevDown = down;
  tDown = down;
}

bool inRect(int px, int py, int x, int y, int w, int h) {
  return px >= x && px < x + w && py >= y && py < y + h;
}
bool rectsOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}
float frand(float a, float b) { return a + (b - a) * (random(10001) / 10000.0f); }

// ============================================================
//   GAME STATE
// ============================================================
State state = ST_TITLE;

const int MAX_OBS = 8, MAX_PK = 6, MAX_SPARK = 28, MAX_TXT = 4, MAX_BG = 12;
Obstacle  obs[MAX_OBS];
Pickup    pks[MAX_PK];
Spark     sparks[MAX_SPARK];
FloatText ftxt[MAX_TXT];
BgBubble  bgb[MAX_BG];

float py = 110, vy = 0, invuln = 0, gameT = 0, scrollX = 0, spawnDist = 0, speed = 0;
float readyT = 0, overT = 0, wiggleT = 0;
int   score = 0, lives = 3, best = 0, level = 0, skin = 0, browse = 0, readyCount = -1;
bool  newRecord = false;
const char* overMsg = "";
const char* unlockedName = nullptr;
uint32_t lastMs = 0;

// ============================================================
//   DRAWING HELPERS
// ============================================================
uint16_t pastelHue(float h) {
  float r, g, b, x = h * 6.0f;
  int i = (int)x;
  float f = x - i;
  switch (i % 6) {
    case 0: r = 1; g = f; b = 0; break;
    case 1: r = 1 - f; g = 1; b = 0; break;
    case 2: r = 0; g = 1; b = f; break;
    case 3: r = 0; g = 1 - f; b = 1; break;
    case 4: r = f; g = 0; b = 1; break;
    default: r = 1; g = 0; b = 1 - f; break;
  }
  r = 0.35f + 0.65f * r; g = 0.35f + 0.65f * g; b = 0.35f + 0.65f * b;
  return RGB((uint8_t)(r * 255), (uint8_t)(g * 255), (uint8_t)(b * 255));
}

void shadowText(const char* s, int x, int y, int font, uint16_t col) {
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(C_BLACK);
  fb.drawString(s, x + 2, y + 2, font);
  fb.setTextColor(col);
  fb.drawString(s, x, y, font);
}

void drawButtonBase(int x, int y, int w, int h, uint16_t col) {
  fb.fillRoundRect(x, y + 3, w, h, 10, C_BTN_SHADOW);
  fb.fillRoundRect(x, y, w, h, 10, col);
  fb.drawRoundRect(x, y, w, h, 10, C_WHITE);
}

void drawButton(int x, int y, int w, int h, const char* label, uint16_t col) {
  drawButtonBase(x, y, w, h, col);
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(C_WHITE);
  fb.drawString(label, x + w / 2, y + h / 2 + 1, 4);
}

void drawButton2(int x, int y, int w, int h, const char* cap, const char* val, uint16_t col) {
  drawButtonBase(x, y, w, h, col);
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(RGB(225, 230, 255));
  fb.drawString(cap, x + w / 2, y + 11, 2);
  int f = fb.textWidth(val, 4) <= w - 10 ? 4 : 2;
  fb.setTextColor(C_WHITE);
  fb.drawString(val, x + w / 2, y + 31, f);
}

void drawArrowButton(int x, int y, int w, int h, bool left, uint16_t col) {
  drawButtonBase(x, y, w, h, col);
  int cx = x + w / 2, cy = y + h / 2;
  if (left) fb.fillTriangle(cx + 8, cy - 13, cx + 8, cy + 13, cx - 10, cy, C_WHITE);
  else      fb.fillTriangle(cx - 8, cy - 13, cx - 8, cy + 13, cx + 10, cy, C_WHITE);
}

void drawHeart(int x, int y, int s, uint16_t c) {
  fb.fillCircle(x - s, y, s, c);
  fb.fillCircle(x + s, y, s, c);
  fb.fillTriangle(x - 2 * s, y + 1, x + 2 * s, y + 1, x, y + 2 * s + 3, c);
}

// The star of the show! Facing right. s = scale (1 in game, 2-3 in menus)
void drawAxolotl(int cx, int cy, int s, float t, const Skin& sk, bool locked = false) {
  uint16_t body = sk.body, belly = sk.belly, gill = sk.gill, eye = sk.eye;
  if (sk.rainbow) {
    float h = fmodf(t * 0.3f, 1.0f);
    body  = pastelHue(h);
    belly = pastelHue(fmodf(h + 0.12f, 1.0f));
    gill  = pastelHue(fmodf(h + 0.5f, 1.0f));
  }
  if (locked) { body = RGB(60, 70, 95); belly = RGB(80, 90, 115); gill = RGB(45, 55, 80); eye = RGB(30, 35, 50); }

  float wig = sinf(t * 9.0f);
  float paddle = sinf(t * 8.0f);

  // tail
  int tipX = cx - 23 * s;
  int tipY = cy + (int)(wig * 4 * s);
  fb.fillTriangle(cx - 7 * s, cy - 4 * s, cx - 7 * s, cy + 5 * s, tipX, tipY, body);
  fb.fillTriangle(cx - 9 * s, cy - 1 * s, cx - 9 * s, cy + 2 * s, tipX + 4 * s, tipY, belly);

  // legs (paddling)
  int lp = (int)(paddle * 2 * s);
  fb.fillRoundRect(cx - 8 * s + lp, cy + 3 * s, 3 * s, 6 * s, s, body);
  fb.fillRoundRect(cx + 4 * s - lp, cy + 3 * s, 3 * s, 6 * s, s, body);

  // body + belly
  fb.fillEllipse(cx - 1 * s, cy + 1 * s, 11 * s, 6 * s, body);
  fb.fillEllipse(cx + 1 * s, cy + 4 * s, 8 * s, 2 * s, belly);

  // feathery gills
  const int8_t gb[3][2] = {{7, -6}, {6, -3}, {6, 0}};
  const int8_t gt[3][2] = {{4, -14}, {0, -11}, {-3, -6}};
  for (int k = 0; k < 3; k++) {
    int bx = cx + gb[k][0] * s, by = cy + gb[k][1] * s;
    int ex = cx + gt[k][0] * s + (int)(sinf(t * 6 + k) * s), ey = cy + gt[k][1] * s;
    for (int d = 0; d <= s; d++) fb.drawLine(bx + d, by, ex + d, ey, gill);
    fb.fillCircle(ex, ey, 2 * s, gill);
    fb.fillCircle((bx + ex) / 2, (by + ey) / 2, s, gill);
  }

  // head
  fb.fillEllipse(cx + 11 * s, cy - 1 * s, 9 * s, 7 * s, body);

  // rosy cheek
  if (!locked) fb.fillCircle(cx + 12 * s, cy + 2 * s, s + s / 2, RGB(255, 120, 160));

  // eye (blinks now and then)
  int ex = cx + 14 * s, ey = cy - 3 * s;
  bool blink = fmodf(t, 3.2f) < 0.12f;
  if (blink) {
    fb.fillRect(ex - 2 * s, ey, 4 * s, s < 2 ? 1 : s / 2 + 1, eye);
  } else {
    fb.fillCircle(ex, ey, 2 * s, eye);
    fb.fillCircle(ex + s, ey - s, s / 2, C_WHITE);
  }

  // big smile
  uint16_t mouth = locked ? RGB(40, 45, 60) : RGB(120, 40, 70);
  int th = s > 1 ? s / 2 + 1 : 1;
  for (int d = 0; d < th; d++) {
    fb.drawLine(cx + 14 * s, cy + 2 * s + d, cx + 16 * s, cy + 3 * s + d, mouth);
    fb.drawLine(cx + 16 * s, cy + 3 * s + d, cx + 19 * s, cy + 1 * s + d, mouth);
  }
}

void drawBackground() {
  for (int i = 0; i < 6; i++) {
    int y0 = i * SAND_Y / 6, y1 = (i + 1) * SAND_Y / 6;
    fb.fillRect(0, y0, W, y1 - y0, WATER[i]);
  }
  // far-away hills (slow parallax)
  float s = scrollX * 0.25f;
  for (int x = 0; x < W; x += 4) {
    int h = 16 + (int)(9 * sinf((x + s) * 0.025f) + 5 * sinf((x + s) * 0.071f));
    fb.fillRect(x, SAND_Y - h, 4, h, C_HILL);
  }
  // rising background bubbles
  for (int i = 0; i < MAX_BG; i++) fb.drawCircle((int)bgb[i].x, (int)bgb[i].y, bgb[i].r, C_BUBBLE);
  // sandy floor
  fb.fillRect(0, SAND_Y, W, H - SAND_Y, C_SAND);
  fb.fillRect(0, SAND_Y, W, 3, C_SAND_DARK);
  for (int i = 0; i < 12; i++) {
    float m = fmodf(i * 47.0f - scrollX, 360.0f);
    if (m < 0) m += 360.0f;
    fb.fillCircle((int)m - 20, SAND_Y + 7 + (i * 7) % 16, 2 + (i % 3), C_PEBBLE);
  }
}

void drawRock(const Obstacle& o) {
  int x = (int)o.x, w = o.w, h = o.h, r = w / 4;
  if (o.type == OB_ROCK) {
    int top = SAND_Y - h;
    fb.fillRoundRect(x, top + r, w, h - r + 4, 6, C_ROCK);
    fb.fillCircle(x + w / 3, top + r, r, C_ROCK);
    fb.fillCircle(x + 2 * w / 3, top + r + 3, r, C_ROCK);
    fb.fillCircle(x + w / 3 - r / 3, top + r - r / 3, r / 3 + 1, C_ROCK_LIGHT);
    fb.fillCircle(x + 2 * w / 3, top + h / 2 + 4, 3, C_ROCK_DARK);
    fb.fillCircle(x + w / 4, top + h * 3 / 4, 2, C_ROCK_DARK);
    fb.fillCircle(x + w / 3 + 2, top + 2, 3, C_MOSS);
    fb.fillCircle(x + w / 3 + 7, top + 3, 2, C_MOSS);
  } else {  // hanging rock from the top
    int bottom = h;
    fb.fillRoundRect(x, -6, w, bottom - r + 6, 6, C_ROCK);
    fb.fillCircle(x + w / 3, bottom - r, r, C_ROCK);
    fb.fillCircle(x + 2 * w / 3, bottom - r - 3, r, C_ROCK);
    fb.fillCircle(x + w / 4, bottom / 3, r / 3 + 1, C_ROCK_LIGHT);
    fb.fillCircle(x + 2 * w / 3, bottom / 2, 3, C_ROCK_DARK);
  }
}

void drawWeed(const Obstacle& o) {
  int base = SAND_Y + 2;
  for (int strand = 0; strand < 2; strand++) {
    int sx = (int)o.x + strand * 8;
    int sh = strand == 0 ? o.h : o.h * 6 / 10;
    for (int yy = 0; yy < sh; yy += 5) {
      float sway = sinf(gameT * 2.5f + o.phase + strand + yy * 0.06f) * (yy * 0.07f);
      int r = 5 - (yy * 3) / sh;
      uint16_t c = ((yy / 5) % 2) ? C_WEED : C_WEED_DARK;
      fb.fillCircle(sx + (int)sway, base - yy, r, c);
      if (yy % 20 == 10) fb.fillEllipse(sx + (int)sway + (strand ? 5 : -5), base - yy, 4, 2, C_WEED);
    }
  }
}

void drawFish(const Obstacle& o) {
  int x = (int)o.x, y = (int)o.y;
  int w = (int)(sinf(gameT * 12 + o.phase) * 3);
  fb.fillTriangle(x + 9, y, x + 19, y - 7 + w, x + 19, y + 7 + w, o.col);
  fb.fillEllipse(x, y, 12, 7, o.col);
  fb.fillTriangle(x - 2, y - 6, x + 7, y - 6, x + 3, y - 11, o.col);
  fb.drawFastVLine(x + 3, y - 5, 11, C_WHITE);
  fb.drawFastVLine(x + 4, y - 5, 11, C_WHITE);
  fb.fillCircle(x - 6, y - 2, 3, C_WHITE);
  fb.fillCircle(x - 7, y - 2, 1, C_BLACK);
  fb.drawLine(x - 11, y + 2, x - 8, y + 3, C_BLACK);   // little smile
}

void drawPickups() {
  for (int i = 0; i < MAX_PK; i++) {
    Pickup& p = pks[i];
    if (!p.on) continue;
    int x = (int)p.x, y = (int)(p.y + sinf(gameT * 3 + p.phase) * 3);
    if (p.type == PK_BUBBLE) {
      fb.fillCircle(x, y, 8, C_BUBBLE);
      fb.fillCircle(x, y, 6, RGB(100, 195, 240));
      fb.fillCircle(x - 3, y - 3, 2, C_WHITE);
    } else {
      fb.fillCircle(x, y, 10, C_WHITE);
      drawHeart(x, y - 2, 4, C_HEART);
    }
  }
}

void drawObstacles() {
  for (int i = 0; i < MAX_OBS; i++) {
    Obstacle& o = obs[i];
    if (!o.on) continue;
    if (o.type == OB_ROCK || o.type == OB_ROCK_TOP) drawRock(o);
    else if (o.type == OB_WEED) drawWeed(o);
    else drawFish(o);
  }
}

void drawPlayer() {
  if (invuln > 0 && ((int)(invuln * 12)) % 2 == 0) return;   // blink after a bump
  int bob = (state == ST_READY) ? (int)(sinf(gameT * 3) * 3) : 0;
  drawAxolotl(PLAYER_X, (int)py + bob, 1, gameT, SKINS[skin]);
}

void drawSparksAndText() {
  for (int i = 0; i < MAX_SPARK; i++)
    if (sparks[i].on) fb.fillCircle((int)sparks[i].x, (int)sparks[i].y, sparks[i].life > 0.3f ? 2 : 1, sparks[i].c);
  for (int i = 0; i < MAX_TXT; i++)
    if (ftxt[i].on) shadowText(ftxt[i].s, (int)ftxt[i].x, (int)ftxt[i].y, 4, ftxt[i].c);
}

void drawHUD(bool showPause) {
  for (int i = 0; i < lives; i++) drawHeart(14 + i * 20, 12, 4, C_HEART);
  char buf[12];
  snprintf(buf, sizeof(buf), "%d", score);
  shadowText(buf, W / 2, 18, 4, C_WHITE);
  if (showPause) {
    fb.fillRoundRect(W - 34, 4, 28, 26, 6, RGB(0, 60, 115));
    fb.fillRect(W - 26, 10, 5, 14, C_WHITE);
    fb.fillRect(W - 17, 10, 5, 14, C_WHITE);
  }
}

void drawGame() {
  drawBackground();
  drawPickups();
  drawObstacles();
  drawPlayer();
}

// ============================================================
//   EFFECTS
// ============================================================
void burst(float x, float y, uint16_t c, int n) {
  for (int i = 0; i < MAX_SPARK && n > 0; i++) {
    if (sparks[i].on) continue;
    sparks[i] = {true, x, y, frand(-70, 70), frand(-90, 30), frand(0.4f, 0.9f), c};
    n--;
  }
}

void addText(const char* s, float x, float y, uint16_t c) {
  for (int i = 0; i < MAX_TXT; i++) {
    if (ftxt[i].on) continue;
    ftxt[i].on = true;
    strncpy(ftxt[i].s, s, sizeof(ftxt[i].s) - 1);
    ftxt[i].s[sizeof(ftxt[i].s) - 1] = 0;
    ftxt[i].x = x; ftxt[i].y = y; ftxt[i].life = 1.2f; ftxt[i].c = c;
    return;
  }
}

void initBg() {
  for (int i = 0; i < MAX_BG; i++)
    bgb[i] = {frand(0, W), frand(0, SAND_Y), frand(15, 35), (uint8_t)random(1, 4)};
}

void updateCommon(float dt, float worldSpd) {
  for (int i = 0; i < MAX_BG; i++) {
    BgBubble& b = bgb[i];
    b.y -= b.spd * dt;
    b.x -= worldSpd * 0.3f * dt;
    if (b.y < -5 || b.x < -5) { b.y = SAND_Y + frand(0, 20); b.x = frand(0, W + 40); }
  }
  for (int i = 0; i < MAX_SPARK; i++) {
    Spark& s = sparks[i];
    if (!s.on) continue;
    s.x += (s.vx - worldSpd * 0.5f) * dt;
    s.y += s.vy * dt;
    s.vy += 60 * dt;
    s.life -= dt;
    if (s.life <= 0) s.on = false;
  }
  for (int i = 0; i < MAX_TXT; i++) {
    if (!ftxt[i].on) continue;
    ftxt[i].y -= 22 * dt;
    ftxt[i].life -= dt;
    if (ftxt[i].life <= 0) ftxt[i].on = false;
  }
}

// ============================================================
//   GAME LOGIC
// ============================================================
bool skinUnlocked(int i) { return best >= SKINS[i].unlockScore; }

void goReady() { state = ST_READY; readyT = 0; readyCount = -1; }

void startGame() {
  for (int i = 0; i < MAX_OBS; i++) obs[i].on = false;
  for (int i = 0; i < MAX_PK; i++) pks[i].on = false;
  for (int i = 0; i < MAX_SPARK; i++) sparks[i].on = false;
  for (int i = 0; i < MAX_TXT; i++) ftxt[i].on = false;
  py = SAND_Y / 2; vy = 0; score = 0; invuln = 0;
  lives = LEVELS[level].lives;
  speed = LEVELS[level].v0;
  spawnDist = 120;
  goReady();
}

void addScore(int n) {
  int before = score;
  score += n;
  if (score / 10 > before / 10) {
    static const char* praise[] = {"Awesome!", "Super!", "Wow!", "Amazing!", "Splashy!", "Keep going!"};
    addText(praise[random(6)], W / 2, 70, C_GOLD);
    playSound(SND(SND_MILESTONE), 2);
    ledFlash(0, 0, 1, 400);
  }
}

void spawnObstacle(int gap) {
  int slot = -1;
  for (int i = 0; i < MAX_OBS; i++) if (!obs[i].on) { slot = i; break; }
  if (slot < 0) return;
  Obstacle& o = obs[slot];
  o.on = true; o.passed = false; o.phase = frand(0, 6.28f); o.spd = 0; o.y = 0; o.col = 0;

  int r = random(100);
  if (r < 30) {
    o.type = OB_ROCK; o.w = random(34, 53); o.h = random(28, 71); o.x = W + 10;
  } else if (r < 55) {
    o.type = OB_WEED; o.w = 16; o.h = random(60, 116); o.x = W + 20;
  } else if (r < 82 || score < 4) {
    o.type = OB_FISH; o.w = 24; o.h = 14; o.x = W + 24;
    o.y = constrain(py + frand(-35, 35), 28.0f, (float)(SAND_Y - 24));  // fish aim near you
    o.spd = frand(12, 40) * LEVELS[level].fishSpeed;
    o.col = FISH_COLORS[random(3)];
  } else {
    o.type = OB_ROCK_TOP; o.w = random(34, 51); o.h = random(30, 66); o.x = W + 10;
  }

  // place a bubble (or sometimes a heart) in the open water after it
  if (random(100) < 65) {
    for (int i = 0; i < MAX_PK; i++) {
      if (pks[i].on) continue;
      Pickup& p = pks[i];
      p.on = true;
      p.x = o.x + gap / 2.0f;
      p.y = frand(35, SAND_Y - 30);
      p.phase = frand(0, 6.28f);
      p.type = (lives < LEVELS[level].lives && random(100) < 12) ? PK_HEART : PK_BUBBLE;
      break;
    }
  }
}

void obHitbox(const Obstacle& o, int& x, int& y, int& w, int& h) {
  switch (o.type) {
    case OB_ROCK:     x = (int)o.x + 3; y = SAND_Y - o.h + 3; w = o.w - 6; h = o.h; break;
    case OB_ROCK_TOP: x = (int)o.x + 3; y = 0; w = o.w - 6; h = o.h - 3; break;
    case OB_WEED:     x = (int)o.x - 6; y = SAND_Y - o.h + 4; w = 14; h = o.h; break;
    default:          x = (int)o.x - 10; y = (int)o.y - 5; w = 22; h = 10; break;
  }
}

void gameOver() {
  state = ST_OVER;
  overT = 0;
  invuln = 0;
  unlockedName = nullptr;
  newRecord = score > best;
  if (newRecord) {
    for (int i = 0; i < N_SKINS; i++)
      if (SKINS[i].unlockScore > best && SKINS[i].unlockScore <= score) unlockedName = SKINS[i].name;
    best = score;
    prefs.putInt("best", best);
    playSound(SND(SND_RECORD), 4);
    ledFlash(1, 1, 0, 800);
  } else {
    playSound(SND(SND_OVER), 4);
  }
  static const char* msgs[] = {"Great swim!", "Nice try!", "So close!", "Well done!", "Good job!"};
  overMsg = newRecord ? "New Record!" : msgs[random(5)];
  lockInput(900);   // stops accidental taps on the buttons
}

void hurt() {
  lives--;
  invuln = 1.8f;
  vy = 0;
  burst(PLAYER_X, py, C_HEART, 10);
  ledFlash(1, 0, 0, 300);
  if (lives <= 0) {
    gameOver();
  } else {
    playSound(SND(SND_HIT), 3);
    addText("Ouch!", PLAYER_X + 35, py - 20, C_WHITE);
  }
}

// ============================================================
//   SCREEN UPDATES
// ============================================================
void updateTitle(float dt) {
  scrollX += 35 * dt;
  if (!tPressed) return;
  if (inRect(tx, ty, 95, 122, 130, 44)) {
    playSound(SND(SND_CLICK));
    startGame();
  } else if (inRect(tx, ty, 5, 178, 100, 48)) {
    playSound(SND(SND_CLICK));
    browse = skin;
    state = ST_SKINS;
    lockInput(250);
  } else if (inRect(tx, ty, 110, 178, 100, 48)) {
    level = (level + 1) % 3;
    prefs.putUChar("lvl", level);
    playSound(SND(SND_CLICK));
  } else if (inRect(tx, ty, 215, 178, 100, 48)) {
    volume = (volume + 1) % 3;
    prefs.putUChar("vol", volume);
    playSound(SND(SND_SELECT));
  } else if (inRect(tx, ty, 110, 66, 100, 56)) {
    wiggleT = 0.6f;
    playSound(SND(SND_SQUEAK));
  }
}

void drawTitle() {
  drawBackground();
  shadowText("Axolotl Adventure", W / 2, 28, 4, C_GOLD);
  shadowText("by AliceFriend", W / 2, 52, 2, C_WHITE);
  int wig = wiggleT > 0 ? (int)(sinf(gameT * 30) * 4) : 0;
  drawAxolotl(160, 96 + (int)(sinf(gameT * 2) * 4) + wig, 2, gameT, SKINS[skin]);

  char buf[12];
  snprintf(buf, sizeof(buf), "%d", best);
  shadowText("Best", 45, 84, 2, C_WHITE);
  shadowText(buf, 45, 104, 4, C_GOLD);

  drawButton(95, 122, 130, 44, "PLAY", C_GREEN_BTN);
  drawButton2(5, 178, 100, 48, "SKIN", SKINS[skin].name, C_PURPLE_BTN);
  drawButton2(110, 178, 100, 48, "LEVEL", LEVELS[level].name, C_ORANGE_BTN);
  drawButton2(215, 178, 100, 48, "SOUND", VOL_NAMES[volume], C_BLUE_BTN);
}

void updateSkins(float dt) {
  scrollX += 20 * dt;
  if (!tPressed) return;
  bool un = skinUnlocked(browse);
  if (inRect(tx, ty, 8, 72, 50, 56)) {
    browse = (browse + N_SKINS - 1) % N_SKINS;
    playSound(SND(SND_CLICK));
  } else if (inRect(tx, ty, 262, 72, 50, 56)) {
    browse = (browse + 1) % N_SKINS;
    playSound(SND(SND_CLICK));
  } else if (inRect(tx, ty, 90, 55, 140, 80)) {
    if (un) { wiggleT = 0.6f; playSound(SND(SND_SQUEAK)); }
    else playSound(SND(SND_LOCKED));
  } else if (inRect(tx, ty, 110, 190, 100, 40)) {
    if (un) {
      skin = browse;
      prefs.putUChar("skin", skin);
      playSound(SND(SND_SELECT));
    } else {
      playSound(SND(SND_CLICK));
    }
    state = ST_TITLE;
    lockInput(250);
  }
}

void drawSkins() {
  drawBackground();
  shadowText("Pick Your Axolotl", W / 2, 22, 4, C_GOLD);
  bool un = skinUnlocked(browse);
  int wig = wiggleT > 0 ? (int)(sinf(gameT * 30) * 5) : 0;
  drawAxolotl(160, 100 + wig, 3, gameT, SKINS[browse], !un);

  if (!un) {  // padlock
    for (int r = 6; r <= 9; r++) fb.drawCircle(160, 94, r, C_GOLD);
    fb.fillRoundRect(147, 96, 26, 20, 4, C_GOLD);
    fb.fillCircle(160, 104, 3, C_BLACK);
    fb.fillRect(159, 104, 3, 7, C_BLACK);
  }

  drawArrowButton(8, 72, 50, 56, true, C_BLUE_BTN);
  drawArrowButton(262, 72, 50, 56, false, C_BLUE_BTN);
  shadowText(SKINS[browse].name, W / 2, 150, 4, C_WHITE);

  char buf[32];
  if (!un) {
    snprintf(buf, sizeof(buf), "Score %d to unlock!", SKINS[browse].unlockScore);
    shadowText(buf, W / 2, 174, 2, C_GOLD);
  } else if (browse == skin) {
    shadowText("This is you!", W / 2, 174, 2, C_GOLD);
  } else {
    shadowText("Tap OK to choose", W / 2, 174, 2, C_WHITE);
  }
  drawButton(110, 190, 100, 40, un ? "OK" : "Back", un ? C_GREEN_BTN : C_GREY_BTN);
}

void updateReady(float dt) {
  readyT += dt;
  int c = (int)(readyT / 0.55f);
  if (c != readyCount) {
    readyCount = c;
    if (c < 3) playSound(SND(SND_BEEP), 2);
    else if (c == 3) playSound(SND(SND_GO), 2);
  }
  if (readyT > 0.55f * 4) state = ST_PLAY;
}

void drawReady() {
  drawGame();
  drawHUD(false);
  for (int x = 0; x < W; x += 16) fb.drawFastHLine(x, H / 2, 8, C_WHITE);
  fb.fillTriangle(285, 40, 272, 58, 298, 58, C_WHITE);
  shadowText("Swim up", 285, 72, 2, C_WHITE);
  shadowText("Swim down", 280, 168, 2, C_WHITE);
  fb.fillTriangle(285, 200, 272, 182, 298, 182, C_WHITE);

  if (readyCount < 3) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", 3 - readyCount);
    fb.setTextDatum(MC_DATUM);
    fb.setTextColor(C_BLACK);
    fb.drawString(buf, 172, 92, 7);
    fb.setTextColor(C_GOLD);
    fb.drawString(buf, 170, 90, 7);
  } else {
    shadowText("Swim!", 170, 90, 4, C_GOLD);
  }
}

void updatePlay(float dt) {
  const Level& L = LEVELS[level];
  bool onPause = inRect(tx, ty, W - 44, 0, 44, 38);
  if (tPressed && onPause) {
    state = ST_PAUSE;
    playSound(SND(SND_CLICK));
    return;
  }

  // --- controls: top half = up, bottom half = down ---
  if (tDown && !onPause) {
    bool up = ty < H / 2;
    if (tPressed) {
      vy = up ? -IMPULSE : IMPULSE;
      if (up) playSound(SND(SND_UP), 0);
      else    playSound(SND(SND_DOWN), 0);
    }
    vy += (up ? -SWIM_ACC : SWIM_ACC) * dt;
  } else {
    vy -= vy * fminf(1.0f, 3.0f * dt);   // gently glide to a stop
  }
  vy = constrain(vy, -MAX_VY, MAX_VY);
  py += vy * dt;
  if (py < 18)          { py = 18;          if (vy < 0) vy = 0; }
  if (py > SAND_Y - 12) { py = SAND_Y - 12; if (vy > 0) vy = 0; }

  // --- world scroll & spawning ---
  speed = fminf(L.vMax, L.v0 + score * 1.6f);
  float dx = speed * dt;
  scrollX += dx;
  spawnDist -= dx;
  if (spawnDist <= 0) {
    int gap = random(L.gapMin, L.gapMax + 1);
    spawnObstacle(gap);
    spawnDist = gap;
  }

  // forgiving hitbox (smaller than the drawing)
  int hx = PLAYER_X - 10, hy = (int)py - 6, hw = 26, hh = 13;

  for (int i = 0; i < MAX_OBS; i++) {
    Obstacle& o = obs[i];
    if (!o.on) continue;
    o.x -= dx;
    if (o.type == OB_FISH) {
      o.x -= o.spd * dt;
      o.y += cosf(gameT * 2.5f + o.phase) * 14 * dt;
    }
    int bx, by, bw, bh;
    obHitbox(o, bx, by, bw, bh);
    if (!o.passed && bx + bw < hx) {
      o.passed = true;
      addScore(1);
      playSound(SND(SND_PASS), 0);
    }
    if (bx + bw < -30) { o.on = false; continue; }
    if (invuln <= 0 && rectsOverlap(hx, hy, hw, hh, bx, by, bw, bh)) {
      hurt();
      if (state != ST_PLAY) return;
    }
  }

  for (int i = 0; i < MAX_PK; i++) {
    Pickup& p = pks[i];
    if (!p.on) continue;
    p.x -= dx;
    if (p.x < -20) { p.on = false; continue; }
    float ddx = p.x - (PLAYER_X + 4), ddy = p.y - py;
    if (ddx * ddx + ddy * ddy < 18 * 18) {
      p.on = false;
      if (p.type == PK_BUBBLE) {
        burst(p.x, p.y, C_BUBBLE, 8);
        playSound(SND(SND_BUBBLE), 1);
        ledFlash(0, 1, 1, 120);
        addScore(1);
      } else {
        if (lives < L.lives) lives++;
        burst(p.x, p.y, C_HEART, 12);
        addText("+1 Heart!", p.x, p.y - 15, C_HEART);
        playSound(SND(SND_HEART), 2);
        ledFlash(1, 0, 1, 400);
      }
    }
  }

  if (invuln > 0) invuln -= dt;
}

void updatePause() {
  if (tPressed) { playSound(SND(SND_CLICK)); goReady(); }
}

void drawPause() {
  drawGame();
  drawHUD(false);
  fb.fillRoundRect(60, 70, 200, 100, 14, C_PANEL);
  fb.drawRoundRect(60, 70, 200, 100, 14, C_WHITE);
  shadowText("Paused", W / 2, 100, 4, C_GOLD);
  shadowText("Tap to keep swimming!", W / 2, 140, 2, C_WHITE);
}

void updateOver(float dt) {
  overT += dt;
  if (newRecord && overT < 3.0f && random(100) < 30)
    burst(frand(50, 270), frand(40, 110), pastelHue(frand(0, 1)), 5);   // confetti!
  if (!tPressed) return;
  if (inRect(tx, ty, 55, 165, 100, 40)) {
    playSound(SND(SND_CLICK));
    startGame();
  } else if (inRect(tx, ty, 165, 165, 100, 40)) {
    playSound(SND(SND_CLICK));
    state = ST_TITLE;
    lockInput(250);
  }
}

void drawOver() {
  drawGame();
  fb.fillRoundRect(40, 30, 240, 185, 14, C_PANEL);
  fb.drawRoundRect(40, 30, 240, 185, 14, C_WHITE);
  shadowText(overMsg, W / 2, 52, 4, newRecord ? C_GOLD : C_WHITE);

  char buf[32];
  snprintf(buf, sizeof(buf), "%d", score);
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(C_WHITE);
  fb.drawString(buf, W / 2, 98, 7);

  snprintf(buf, sizeof(buf), "Best: %d", best);
  shadowText(buf, W / 2, 132, 2, C_WHITE);
  if (unlockedName) {
    snprintf(buf, sizeof(buf), "New skin: %s!", unlockedName);
    shadowText(buf, W / 2, 150, 2, C_GOLD);
  }
  drawButton(55, 165, 100, 40, "Again", C_GREEN_BTN);
  drawButton(165, 165, 100, 40, "Menu", C_BLUE_BTN);
}

// ============================================================
//   SETUP & LOOP
// ============================================================
void setup() {
  Serial.begin(115200);
  pinMode(LED_R_PIN, OUTPUT);
  pinMode(LED_G_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);
  ledSet(0, 0, 0);
  randomSeed(esp_random());

  tft.init();
  tft.setRotation(1);            // landscape; try 3 if the picture is upside down
  tft.fillScreen(TFT_BLACK);
  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touch.begin(touchSPI);
  touch.setRotation(1);

  // 8-bit colour frame buffer (a 16-bit one won't fit in the ESP32's RAM)
  fb.setColorDepth(8);
  if (!fb.createSprite(W, H)) {
    tft.setTextColor(TFT_RED);
    tft.drawString("Not enough memory!", 10, 10, 2);
    while (true) delay(1000);
  }

  prefs.begin("axolotl", false);
  best   = prefs.getInt("best", 0);
  skin   = prefs.getUChar("skin", 0);
  volume = prefs.getUChar("vol", 2);
  level  = prefs.getUChar("lvl", 0);
  if (skin >= N_SKINS || !skinUnlocked(skin)) skin = 0;
  if (volume > 2) volume = 2;
  if (level > 2) level = 0;

  speakerInit();
  xTaskCreatePinnedToCore(soundTask, "sound", 3072, nullptr, 1, nullptr, 0);

  initBg();
  playSound(SND(SND_START), 2);
  ledFlash(1, 0, 1, 500);
  lastMs = millis();
}

void loop() {
  uint32_t now = millis();
  float dt = (now - lastMs) / 1000.0f;
  lastMs = now;
  if (dt > 0.05f) dt = 0.05f;

  readTouch(now);
  ledUpdate(now);
  gameT += dt;
  if (wiggleT > 0) wiggleT -= dt;

  switch (state) {
    case ST_TITLE: updateTitle(dt);  break;
    case ST_SKINS: updateSkins(dt);  break;
    case ST_READY: updateReady(dt);  break;
    case ST_PLAY:  updatePlay(dt);   break;
    case ST_PAUSE: updatePause();    break;
    case ST_OVER:  updateOver(dt);   break;
  }

  float worldSpd = (state == ST_PLAY) ? speed : (state == ST_TITLE ? 35.0f : 0.0f);
  updateCommon(dt, worldSpd);

  switch (state) {
    case ST_TITLE: drawTitle(); break;
    case ST_SKINS: drawSkins(); break;
    case ST_READY: drawReady(); break;
    case ST_PLAY:  drawGame(); drawHUD(true); break;
    case ST_PAUSE: drawPause(); break;
    case ST_OVER:  drawOver();  break;
  }
  drawSparksAndText();

  if (TOUCH_DEBUG && tDown) {
    fb.fillCircle(tx, ty, 4, TFT_RED);
    char b[32];
    snprintf(b, sizeof(b), "raw %d,%d", rawX, rawY);
    fb.setTextDatum(BL_DATUM);
    fb.setTextColor(TFT_RED);
    fb.drawString(b, 4, H - 2, 2);
  }

  fb.pushSprite(0, 0);
}
