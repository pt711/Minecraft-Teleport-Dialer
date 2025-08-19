#include <Arduino.h>
#include "M5Dial.h"
#include <M5GFX.h>
#include <ESP32TimerInterrupt.h>
#include "USB.h"
#include "USBHIDKeyboard.h"

M5GFX lcd;
M5Canvas canvas(&lcd);
ESP32Timer ITimer(0);
USBHIDKeyboard Keyboard;

const int countValues[] = {
  0,
  1,2,3,4,5,6,7,8,9,
  10,20,30,40,50,60,70,80,90,
  100,200,300,400,500,600,700,800,900,
  1000,2000,3000,4000,5000,6000,7000,8000,9000,
  10000,20000,30000,40000,50000,60000,70000,80000,90000,
  99999
};
const int countLength = sizeof(countValues) / sizeof(countValues[0]);
int countIndex = 0;
int currentValue = 0;
int lastLevel = 0;
bool animateShift = false;
bool isMaxJitter = false;
float carry = 0;
volatile int encoderDelta = 0;
long lastEncoder = 0;
unsigned long lastDrawTime = 0;
const unsigned long drawInterval = 20;

uint16_t gaugeColors[] = {
  lcd.color565(0, 70, 255),    // rgb(0,70,255)
  lcd.color565(0, 100, 220),   // rgb(0,100,220)
  lcd.color565(0, 150, 220),   // rgb(0,150,220)
  lcd.color565(50, 200, 230),  // rgb(50,200,230)
  lcd.color565(80, 255, 255)   // rgb(80,255,255)
};

// Timer interrupt: update encoder value
bool IRAM_ATTR onTimer(void*) {
  M5Dial.update();
  long current = M5Dial.Encoder.read();
  encoderDelta += (current - lastEncoder);
  lastEncoder = current;
  return true;
}

// Initialize LCD and canvas
void setupDisplay() {
  canvas.setColorDepth(16);
  canvas.createSprite(lcd.width(), lcd.height());
  canvas.setTextDatum(MC_DATUM);
  canvas.setFont(&fonts::Font6);
}

// Update count value by dial rotation
void updateCount() {
  float notchDiff = encoderDelta / 1.0f;
  carry += notchDiff;
  encoderDelta = 0;

  int steps = int(carry);
  carry -= steps;

  bool changed = false;

  for (int i = 0; i < abs(steps); ++i) {
    if (steps > 0 && countIndex < countLength - 1) {
      countIndex++;
      changed = true;
    } else if (steps < 0 && countIndex > 0) {
      countIndex--;
      changed = true;
    }
  }

  // Always play sound when dial is rotated
  if (steps != 0) {
    M5Dial.Speaker.tone(800, 20);
  }

  currentValue = countValues[countIndex];
  int level = (currentValue == 0) ? 0 : int(log10(currentValue));
  animateShift = (level != lastLevel);

  // Play different sound on digit increase
  if (animateShift) {
    M5Dial.Speaker.tone(6000, 30);
  }

  // Play digit increase sound also when reaching 99999
  if (currentValue == 99999 && steps > 0) {
    M5Dial.Speaker.tone(6000, 10);
    delay(10);
    M5Dial.Speaker.tone(6000, 10);
    delay(10);
    M5Dial.Speaker.tone(6000, 10);
  }

  lastLevel = level;

  isMaxJitter = (countIndex == countLength - 1 && steps > 0);
}

// Draw text with outline (utility function)
void drawTextWithOutline(int x, int y, const String& text, float textSize, uint16_t mainColor, int outlineWidth = 2, uint16_t outlineColor = BLACK) {
  canvas.setTextSize(textSize);
  // Outline
  for (int dx = -outlineWidth; dx <= outlineWidth; dx++) {
    for (int dy = -outlineWidth; dy <= outlineWidth; dy++) {
      if (dx != 0 || dy != 0) {
        canvas.setTextColor(outlineColor);
        canvas.setCursor(x + dx, y + dy);
        canvas.println(text);
      }
    }
  }
  // Main text
  canvas.setTextColor(mainColor);
  canvas.setCursor(x, y);
  canvas.println(text);
}

// Wrapper for old drawOutlinedText
void drawOutlinedText(int cx, int cy, const String& text, float fontScale, uint16_t color) {
  canvas.setFont(&fonts::Font6);
  canvas.setTextSize(fontScale);
  int textW = canvas.textWidth(text);
  int textH = canvas.fontHeight() * fontScale;
  int x = cx - textW / 2;
  int y = cy - textH / 2;
  drawTextWithOutline(x, y, text, fontScale, color);
}

// Draw arc-shaped gauge
void drawFilledArc(int cx, int cy, float r_inner, float r_outer,
                   float startAngle, float endAngle, float skewY, uint16_t color) {
  for (float angle = startAngle; angle < endAngle; angle += 0.5) {
    float rad = radians(angle);
    float rad2 = radians(angle + 0.25);
    float x1 = cx + cos(rad) * r_inner;
    float y1 = cy + sin(rad) * r_inner * skewY;
    float x2 = cx + cos(rad) * r_outer;
    float y2 = cy + sin(rad) * r_outer * skewY;
    canvas.drawLine(x1, y1, x2, y2, color);

    float x3 = cx + cos(rad2) * r_inner;
    float y3 = cy + sin(rad2) * r_inner * skewY;
    float x4 = cx + cos(rad2) * r_outer;
    float y4 = cy + sin(rad2) * r_outer * skewY;
    canvas.drawLine(x3, y3, x4, y4, color);
  }
}

// Draw gauge and count value
void drawGaugeAndCount(int val, int verticalOffset = 0, float numberScale = 1.0f, int numberYOffset = 0) {
  const int globalYOffset = 20;
  int cx = lcd.width() / 2;
  int cy = lcd.height() / 2 + verticalOffset + globalYOffset;

  canvas.fillScreen(BLACK);

  int level = (val == 0) ? 0 : int(log10(val));
  int progressPercent = 0;

  if (val == 99999) {
    progressPercent = 100;
  } else if (val > 0) {
    int base = pow(10, level);
    progressPercent = (val * 100) / (base * 10);
    progressPercent = constrain(progressPercent, 0, 100);
  }

  int ringCount = level + 1;

  // Draw gauge in normal size
  for (int i = 0; i < ringCount; ++i) {
    int cyOffset = cy + (ringCount - 1 - i) * 30;
    int angle = 360;
    if (i == ringCount - 1) {
      angle = map(progressPercent, 0, 100, 0, 360);
    }
    drawFilledArc(cx, cyOffset, 80, 100, -90, -90 + angle, 0.25, gaugeColors[min(i, 4)]);
  }

  // Draw Teleport label
  String teleportText = "Teleport";
  canvas.setFont(&fonts::Font4);
  float teleportTextSize = 0.6 + 0.2 * ringCount;
  int teleportW = canvas.textWidth(teleportText);
  int teleportX = cx - teleportW / 2;
  int teleportY = cy - 55 - level * 10;
  drawTextWithOutline(teleportX, teleportY, teleportText, teleportTextSize, gaugeColors[min(level, 4)], 2, BLACK);

  // Draw number (with scale)
  float fontScale = (0.6 + 0.2 * ringCount) * numberScale;
  int textYOffset = level * 8 + numberYOffset;
  drawOutlinedText(cx, cy - 10 + textYOffset, String(val), fontScale, gaugeColors[min(level, 4)]);

  // Draw Blocks label
  String blocksText = "Blocks";
  int blocksW = canvas.textWidth(blocksText);
  int blocksX = cx - blocksW / 2;
  int blocksY = cy + 0 + level * 2;
  float blocksTextSize = 0.6 + 0.2 * ringCount;
  canvas.setFont(&fonts::Font4);
  drawTextWithOutline(blocksX, blocksY, blocksText, blocksTextSize, gaugeColors[min(level, 4)], 2, BLACK);

  canvas.pushSprite(&lcd, 0, 0);
}

// Digit increase animation
void animateGaugeShift(int val, int direction) {
  drawGaugeAndCount(val, -18, 1.1f);
  for (int offset = -18; offset <= 0; offset += 9) {
    drawGaugeAndCount(val, offset, 1.0f);
    delay(2);
  }
}

// Max value reached animation
void animateMaxJitter(int val) {
  drawGaugeAndCount(val, -5, 1.02f);
  M5Dial.Speaker.tone(7000, 100);
  delay(10);
  drawGaugeAndCount(val, 0, 1.0f);
}

// Pin definitions for G1/G2 buttons
const int G1_PIN = 1;
const int G2_PIN = 2;

// Pin definition for built-in button
const int BUILTIN_BTN_PIN = 42;

// Initialization
void setup() {
  auto cfg = M5.config();
  M5Dial.begin(cfg, true, false);
  lcd.begin();
  Serial.begin(115200);
  setupDisplay();
  M5Dial.Speaker.tone(8000, 20);

  // Initialize G1/G2/built-in buttons
  pinMode(G1_PIN, INPUT_PULLUP);
  pinMode(G2_PIN, INPUT_PULLUP);
  pinMode(BUILTIN_BTN_PIN, INPUT_PULLUP); // Built-in button init

  Keyboard.begin();
  USB.begin();

  if (!ITimer.attachInterruptInterval(1000, onTimer)) {
    Serial.println("Timer attach failed");
  }
}

// Draw HOME screen (G1)
void drawHomeScreen(int x, int y, int z) {
  canvas.fillScreen(BLACK);

  // HOME title
  canvas.setFont(&fonts::Font4);
  canvas.setTextSize(2.0);
  int homeW = canvas.textWidth("HOME");
  int homeX = lcd.width() / 2 - homeW / 2;
  int homeY = 80;
  canvas.setTextColor(WHITE);
  canvas.setCursor(homeX, homeY);
  canvas.println("HOME");

  // Display coordinates
  canvas.setFont(&fonts::Font4);
  canvas.setTextSize(1.0);

  int coordY = 140;
  int coordCenterX = lcd.width() / 2;
  int coordOffsetX = 25;
  canvas.setCursor(coordCenterX - coordOffsetX, coordY);
  canvas.printf("X: %d", x);
  canvas.setCursor(coordCenterX - coordOffsetX, coordY + 30);
  canvas.printf("Y: %d", y);
  canvas.setCursor(coordCenterX - coordOffsetX, coordY + 60);
  canvas.printf("Z: %d", z);
  canvas.pushSprite(&lcd, 0, 0);
}

// Draw G2 screen (show Done when sent)
void drawG2Screen(bool fromHome) {
  canvas.fillScreen(BLACK);

  uint16_t cyberBlue = canvas.color565(0, 0, 255);
  int cx = lcd.width() / 2;
  int cy = lcd.height() / 2;

  // Double ring (dashed, cyber blue)
  int r1 = 70;
  int r2 = 90;
  int dashLen = 18;
  int gapLen = 12;
  int totalLen = dashLen + gapLen;
  int numDashes = 360 / totalLen;
  static int dashOffset = 0;
  dashOffset = (dashOffset + 8) % 360;

  // Draw outer ring
  for (int i = 0; i < numDashes; ++i) {
    float startAngle = i * totalLen + dashOffset;
    float endAngle = startAngle + dashLen;
    for (float a = startAngle; a < endAngle; a += 2) {
      float rad = radians(a);
      int x1 = cx + cos(rad) * r2;
      int y1 = cy + sin(rad) * r2;
      for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
          canvas.drawPixel(x1 + dx, y1 + dy, cyberBlue);
        }
      }
    }
  }
  // Draw inner ring
  for (int i = 0; i < numDashes; ++i) {
    float startAngle = i * totalLen - dashOffset;
    float endAngle = startAngle + dashLen;
    for (float a = startAngle; a < endAngle; a += 2) {
      float rad = radians(a);
      int x1 = cx + cos(rad) * r1;
      int y1 = cy + sin(rad) * r1;
      for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
          canvas.drawPixel(x1 + dx, y1 + dy, cyberBlue);
        }
      }
    }
  }

  // Teleport label (center, cyber blue, black outline)
  canvas.setFont(&fonts::Font4);
  float teleportTextSize = 2.0;
  String teleportText = "Teleport";
  int teleportW = canvas.textWidth(teleportText);
  int teleportX = cx - teleportW / 2;
  int teleportY = cy;
  // Gradient glow (outer to inner)
  for (int glow = 8; glow >= 1; glow--) {
    uint8_t r = 0;
    uint8_t g = map(glow, 8, 1, 40, 0);
    uint8_t b = map(glow, 8, 1, 80, 255);
    uint16_t glowColor = canvas.color565(r, g, b);
    float angleStep = PI / 8;
    int offset = glow;
    for (float a = 0; a < 2 * PI; a += angleStep) {
      int dx = round(cos(a) * offset);
      int dy = round(sin(a) * offset);
      drawTextWithOutline(teleportX + dx, teleportY + dy, teleportText, teleportTextSize, glowColor, 0, glowColor);
    }
  }
  // Draw black outline and main text
  drawTextWithOutline(teleportX, teleportY, teleportText, teleportTextSize, cyberBlue, 2, BLACK);

  canvas.pushSprite(&lcd, 0, 0);
}

// Coordinate values
int coordX = 0;
int coordY = 80;
int coordZ = 0;

// Screen mode management
int altScreenMode = 0; // 0:通常, 1:G1, 2:G2, 3:Setting
bool g2FromHome = false; // G2遷移元フラグ

// Edit mode management
enum EditMode { EDIT_X, EDIT_Y, EDIT_Z, EDIT_DONE };
EditMode editMode = EDIT_X;

// Draw Setting screen
void drawSettingScreen() {
  canvas.fillScreen(BLACK);
  canvas.setFont(&fonts::Font4);
  canvas.setTextSize(1.2);

  // 1st line: Set
  int x1 = lcd.width() / 2 - canvas.textWidth("Set") / 2;
  canvas.setTextColor(WHITE);
  canvas.setCursor(x1, 60);
  canvas.println("Set");

  // 2nd line: HOME XYZ
  int x2 = lcd.width() / 2 - canvas.textWidth("HOME XYZ") / 2;
  canvas.setCursor(x2, 90);
  canvas.println("HOME XYZ");

  canvas.setFont(&fonts::Font4);
  canvas.setTextSize(1.0);
  int cx = lcd.width() / 2, y = 140, off = 25;

  // Blue while editing, white when done
  canvas.setTextColor((editMode == EDIT_X && editMode != EDIT_DONE) ? BLUE : WHITE);
  canvas.setCursor(cx - off, y);
  canvas.printf("X: %d", coordX);
  canvas.setTextColor((editMode == EDIT_Y && editMode != EDIT_DONE) ? BLUE : WHITE);
  canvas.setCursor(cx - off, y + 30);
  canvas.printf("Y: %d", coordY);
  canvas.setTextColor((editMode == EDIT_Z && editMode != EDIT_DONE) ? BLUE : WHITE);
  canvas.setCursor(cx - off, y + 60);
  canvas.printf("Z: %d", coordZ);
  canvas.pushSprite(&lcd, 0, 0);
}

// Command sending state management
bool isSendingCmd = false;
char sendCmd[32];
int sendCmdPos = 0;
unsigned long lastSendTime = 0;
unsigned long sendInterval = 100;

// Debounce variables
unsigned long lastG1PressTime = 0;
unsigned long lastG2PressTime = 0;
const unsigned long debounceDelay = 50; // 50ms

void loop() {
  static bool prevG1Pressed = false;
  static bool prevG2Pressed = false;
  static bool prevBtnPressed = false;
  static int lastEncoderValue = 0;
  bool g1Pressed = digitalRead(G1_PIN) == LOW;
  bool g2Pressed = digitalRead(G2_PIN) == LOW;
  bool btnPressed = digitalRead(BUILTIN_BTN_PIN) == LOW;

  unsigned long now = millis();

  // G1 button: toggle HOME screen (debounced)
  if (g1Pressed && !prevG1Pressed && (now - lastG1PressTime > debounceDelay)) {
    altScreenMode = (altScreenMode == 1) ? 0 : 1;
    M5Dial.Speaker.tone(1200, 60);
    lastG1PressTime = now;
  }
  prevG1Pressed = g1Pressed;

  // G2 button: determine source and send USB (debounced)
  static int prevAltScreenMode = 0;

  if (g2Pressed && !prevG2Pressed && (now - lastG2PressTime > debounceDelay)) {
  // Determine previous screen mode when G2 pressed
    if (prevAltScreenMode == 1) {
        g2FromHome = true;
        snprintf(sendCmd, sizeof(sendCmd), "/tp @s %d %d %d\n", coordX, coordY, coordZ);
    } else {
        g2FromHome = false;
        snprintf(sendCmd, sizeof(sendCmd), "/tp @s ^0 ^0 ^%d\n", currentValue);
    }
    altScreenMode = (altScreenMode == 2) ? 0 : 2;
    isSendingCmd = true;
    sendCmdPos = 0;
    lastSendTime = millis();
    lastG2PressTime = now;
  }
  prevG2Pressed = g2Pressed;
  prevAltScreenMode = altScreenMode;

  // Asynchronous USB keyboard send
  if (isSendingCmd && millis() - lastSendTime > sendInterval) {
    if (sendCmdPos == 0) {
  Keyboard.write('/'); // Slash
      sendCmdPos++;
      lastSendTime = millis();
    sendInterval = 400; // Wait 400ms after slash
  // Teleport start sound (pitch up)
      for (int i = 0; i < 6; i++) {
        M5Dial.Speaker.tone(800 + i * 200, 30);
        delay(30);
      }
    } else {
      char c = sendCmd[sendCmdPos];
      if (c) {
        if (c == '@') {
          Keyboard.write(0x40); // ASCII code for @
        } else if (c == '^') {
          Keyboard.write(0x5E); // ASCII code for ^
        } else {
          Keyboard.write(c);
        }
        sendCmdPos++;
        lastSendTime = millis();
  sendInterval = 100; // 100ms after 2nd char
      } else {
  // Show Done (white, no outline)
        canvas.fillScreen(BLACK);
        int cx = lcd.width() / 2;
        int cy = lcd.height() / 2;
        canvas.setFont(&fonts::Font4);
        float doneTextSize = 2.0;
        String doneText = "Done";
        int doneW = canvas.textWidth(doneText);
        int doneX = cx - doneW / 2;
        int doneY = cy;
        drawTextWithOutline(doneX, doneY, doneText, doneTextSize, WHITE, 0, WHITE);
        canvas.pushSprite(&lcd, 0, 0);
  // Teleport complete sound (beep)
        M5Dial.Speaker.tone(1800, 80);
        delay(80);
        M5Dial.Speaker.tone(2500, 80);
        delay(600);
        isSendingCmd = false;
        altScreenMode = 0;
      }
    }
  }

  // Built-in button: toggle Setting screen
  if (btnPressed && !prevBtnPressed) {
    if (altScreenMode == 3) {
  // Setting screen: edit mode progress
      if (editMode == EDIT_X) {
  editMode = EDIT_Y;
  M5Dial.Speaker.tone(1200, 60); // Normal sound
      } else if (editMode == EDIT_Y) {
  editMode = EDIT_Z;
  M5Dial.Speaker.tone(1200, 60); // Normal sound
      } else if (editMode == EDIT_Z) {
  editMode = EDIT_DONE;
  M5Dial.Speaker.tone(2000, 80); // Beep sound
  delay(80);
  M5Dial.Speaker.tone(3000, 80);
      } else if (editMode == EDIT_DONE) {
  altScreenMode = 0; // Return to normal screen, reset
  editMode = EDIT_X;
  M5Dial.Speaker.tone(1200, 60); // Normal sound
      }
    } else {
  altScreenMode = (altScreenMode == 3) ? 0 : 3;
  editMode = EDIT_X; // X edit after entering Setting screen
  M5Dial.Speaker.tone(1200, 60);
    }
  }
  prevBtnPressed = btnPressed;

  // Setting screen: edit coordinates by dial
  if (altScreenMode == 3 && editMode != EDIT_DONE) {
    int encoderValue = M5Dial.Encoder.read();
    int diff = encoderValue - lastEncoderValue;
    lastEncoderValue = encoderValue;
    if (diff != 0) {
      if (editMode == EDIT_X) {
        coordX += diff;
      } else if (editMode == EDIT_Y) {
        coordY += diff;
      } else if (editMode == EDIT_Z) {
        coordZ += diff;
      }
    }
  encoderDelta = 0; // Reset dial value
  } else if (altScreenMode != 3) {
  lastEncoderValue = M5Dial.Encoder.read(); // Reset value in normal screen
  }

  if (millis() - lastDrawTime > drawInterval) {
    lastDrawTime = millis();

    if (altScreenMode == 1) {
  // HOME screen
      drawHomeScreen(coordX, coordY, coordZ);
    } else if (altScreenMode == 2) {
  // G2 screen
      drawG2Screen(g2FromHome);
    } else if (altScreenMode == 3) {
  // Setting screen
      drawSettingScreen();
    } else {
  // Normal screen
      updateCount();
      if (animateShift) {
        int direction = (lastLevel < int(log10(currentValue))) ? 1 : -1;
        animateGaugeShift(currentValue, direction);
        animateShift = false;
      } else if (isMaxJitter) {
        animateMaxJitter(currentValue);
      } else {
        drawGaugeAndCount(currentValue);
      }
    }
  }
}
