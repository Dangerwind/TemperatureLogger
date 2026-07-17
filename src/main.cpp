#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <hardware/rtc.h>
#include <pico/util/datetime.h>

// Пины дисплея
#define TFT_DC    8
#define TFT_CS    9
#define TFT_SCLK  10
#define TFT_MOSI  11
#define TFT_MISO  12
#define TFT_RST   13
#define TFT_BL    25

// Пины тачскрина
#define TOUCH_SDA  6
#define TOUCH_SCL  7
#define TOUCH_INT  5
#define TOUCH_RST  13
#define CST816S_ADDRESS 0x15

// Пин для датчика температуры DS18B20 и батареи
#define ONE_WIRE_BUS 16
#define BAT_ADC_PIN 29 

Adafruit_GC9A01A tft = Adafruit_GC9A01A(&SPI1, TFT_DC, TFT_CS, TFT_RST);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Состояния приложения
enum AppState { SCREEN_SELECT_INTERVAL, SCREEN_TEMPERATURE_MONITOR, SCREEN_DATA_REVIEW, SCREEN_CONFIRM_EXIT, SCREEN_HARDWARE_ERROR };
AppState currentScreen = SCREEN_SELECT_INTERVAL;

const char* intervals[] = {"0.5s", "1s", "5s", "30s", "1m", "5m", "10m", "30m", "60m", "2h", "6h"};
const unsigned long intervalMs[] = {500, 1000, 5000, 30000, 60000, 300000, 600000, 1800000, 3600000, 7200000, 21600000};
const int numIntervals = 11; 
int currentIndex = 1;        
unsigned long activeIntervalMs = 1000; 

datetime_t rtcStartTime;
uint32_t startTotalSeconds = 0;

const int drumY = 55;        
const int drumW = 240;       
const int drumH = 40;        
const int labelY = 125;      
const int buttonX = 70;      
const int buttonY = 165;
const int buttonW = 100;
const int buttonH = 40;

GFXcanvas16 canvas(drumW, drumH);

const int graphX = 30;         
const int graphY = 65;         
const int graphW = 180;        
const int graphH = 120;        

// Ограничение буфера до 10 000 измерений
const int maxHistory = 10000;     
float tempLog[maxHistory];         
uint32_t timeLog[maxHistory];      
int totalSavedPoints = 0;          

int viewScrollOffset = 0;          
const int maxPointsOnScreen = 180; 

unsigned long lastGraphUpdate = 0;
unsigned long lastClockUpdate = 0; 
unsigned long touchStartTime = 0; 
unsigned long lastBatUpdate = 0;   

bool isTouching = false;
int startX = 0;
int startY = 0;
int lastTouchX = 0;
int totalDragX = 0;
bool buttonPressedState = false;

// Флаг защиты: запрещает клики на экране меню, пока палец после удержания не убрали
bool confirmScreenTouchLocked = false; 

const int yesBtnX = 35;
const int yesBtnY = 135;
const int yesBtnW = 75;
const int yesBtnH = 40;

const int noBtnX = 130;
const int noBtnY = 135;
const int noBtnW = 75;
const int noBtnH = 40;

// Опрос тачскрина
bool getTouchCoordinates(int &x, int &y) {
    Wire1.beginTransmission(CST816S_ADDRESS);
    Wire1.write(0x02);
    if (Wire1.endTransmission() != 0) return false;
    Wire1.requestFrom(CST816S_ADDRESS, 5);
    if (Wire1.available() < 5) return false;
    uint8_t touchPoints = Wire1.read();
    uint8_t xHigh = Wire1.read();
    uint8_t xLow  = Wire1.read();
    uint8_t yHigh = Wire1.read();
    uint8_t yLow  = Wire1.read();
    if (touchPoints == 0) return false;
    x = ((xHigh & 0x0F) << 8) | xLow;
    y = ((yHigh & 0x0F) << 8) | yLow;
    return true;
}

// Замер заряда батареи
int getBatteryPercent() {
    long sum = 0;
    for(int i = 0; i < 10; i++) sum += analogRead(BAT_ADC_PIN);
    float rawVoltage = (sum / 10.0f) * 3.3f / 4095.0f;
    float vBat = rawVoltage * 2.0f;
    if (vBat >= 4.15f) return 100;
    if (vBat <= 3.40f) return 0;
    int percent = (int)((vBat - 3.40f) / (4.15f - 3.40f) * 100.0f);
    return constrain(percent, 0, 100);
}

// Отображение заряда батареи
void displayBattery() {
    tft.setTextSize(1);
    tft.setTextColor(0x5AEB, GC9A01A_BLACK);
    String batStr = "Bat: " + String(getBatteryPercent()) + "%";
    int strW = batStr.length() * 6;
    tft.fillRect(100, 218, 40, 10, GC9A01A_BLACK);
    tft.setCursor(120 - (strW / 2), 218);
    tft.print(batStr);
}

void drawDrumItemToCanvas(int index, int offsetX, uint16_t color) {
    int charWidth = 24; 
    int strLen = strlen(intervals[index]) * charWidth;
    int baseX = 120 - (strLen / 2) + offsetX;
    if ((baseX + strLen) < 0 || baseX > 240) return;
    canvas.setTextSize(4);
    canvas.setTextColor(color);
    canvas.setCursor(baseX, 5); 
    canvas.print(intervals[index]);
}

void drawDrum(int scrollOffset) {
    canvas.fillScreen(GC9A01A_BLACK);
    int prev1 = (currentIndex - 1 + numIntervals) % numIntervals;
    int prev2 = (currentIndex - 2 + numIntervals) % numIntervals;
    int next1 = (currentIndex + 1) % numIntervals;
    int next2 = (currentIndex + 2) % numIntervals;
    int step = 90;
    drawDrumItemToCanvas(prev2, scrollOffset - (step * 2), 0x5AEB); 
    drawDrumItemToCanvas(prev1, scrollOffset - step, 0x5AEB);       
    drawDrumItemToCanvas(currentIndex, scrollOffset, GC9A01A_GREEN); 
    drawDrumItemToCanvas(next1, scrollOffset + step, 0x5AEB);       
    drawDrumItemToCanvas(next2, scrollOffset + (step * 2), 0x5AEB); 
    tft.drawRGBBitmap(0, drumY, canvas.getBuffer(), drumW, drumH);
}
void drawOKButton(bool pressed) {
    if (pressed) {
        tft.fillRoundRect(buttonX, buttonY, buttonW, buttonH, 8, GC9A01A_WHITE);
        tft.setTextColor(GC9A01A_BLACK);
    } else {
        tft.fillRoundRect(buttonX, buttonY, buttonW, buttonH, 8, GC9A01A_BLACK);
        tft.drawRoundRect(buttonX, buttonY, buttonW, buttonH, 8, GC9A01A_WHITE);
        tft.setTextColor(GC9A01A_WHITE);
    }
    tft.setTextSize(2);
    tft.setCursor(buttonX + 38, buttonY + 12);
    tft.print("OK");
}

void drawConfirmButtons(bool yesPressed, bool noPressed) {
    if (yesPressed) {
        tft.fillRoundRect(yesBtnX, yesBtnY, yesBtnW, yesBtnH, 8, GC9A01A_WHITE);
        tft.setTextColor(GC9A01A_BLACK);
    } else {
        tft.fillRoundRect(yesBtnX, yesBtnY, yesBtnW, yesBtnH, 8, GC9A01A_BLACK);
        tft.drawRoundRect(yesBtnX, yesBtnY, yesBtnW, yesBtnH, 8, GC9A01A_WHITE);
        tft.setTextColor(GC9A01A_WHITE);
    }
    tft.setTextSize(2);
    tft.setCursor(yesBtnX + 20, yesBtnY + 12);
    tft.print("YES");

    if (noPressed) {
        tft.fillRoundRect(noBtnX, noBtnY, noBtnW, noBtnH, 8, GC9A01A_WHITE);
        tft.setTextColor(GC9A01A_BLACK);
    } else {
        tft.fillRoundRect(noBtnX, noBtnY, noBtnW, noBtnH, 8, GC9A01A_BLACK);
        tft.drawRoundRect(noBtnX, noBtnY, noBtnW, noBtnH, 8, GC9A01A_WHITE);
        tft.setTextColor(GC9A01A_WHITE);
    }
    tft.setCursor(noBtnX + 26, noBtnY + 12);
    tft.print("NO");
}

void drawConfirmScreen() {
    tft.fillScreen(GC9A01A_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(GC9A01A_WHITE, GC9A01A_BLACK);
    String qStr = "Restart measuring?";
    int qStrWidth = qStr.length() * 12; 
    tft.setCursor(120 - (qStrWidth / 2), 75); 
    tft.print(qStr);
    drawConfirmButtons(false, false);
    displayBattery(); 
}

void drawErrorScreen() {
    tft.fillScreen(GC9A01A_BLACK);
    tft.setTextSize(4);
    tft.setTextColor(GC9A01A_RED, GC9A01A_BLACK);
    String errStr = "ERROR";
    int errW = errStr.length() * 24; 
    tft.setCursor(120 - (errW / 2), 105);
    tft.print(errStr);
    displayBattery();
}

void drawStaticUI() {
    tft.setTextSize(2);
    tft.setTextColor(GC9A01A_WHITE, GC9A01A_BLACK);
    tft.setCursor(30, labelY + 10);
    tft.print("Select interval");
    drawOKButton(false);
}

void animateToTarget(int startOffset, int targetOffset) {
    int frames = 8; 
    for (int i = 1; i <= frames; i++) {
        int currentOffset = startOffset + (targetOffset - startOffset) * i / frames;
        drawDrum(currentOffset);
        delay(10); 
    }
}

void drawGraphGrid() {
    uint16_t gridColor = 0x31A6; 
    tft.drawRect(graphX, graphY, graphW, graphH, gridColor);
}

String formatTimeStr(uint32_t totalSec) {
    int hours = totalSec / 3600;
    int minutes = (totalSec % 3600) / 60;
    int seconds = totalSec % 60;
    return (hours < 10 ? "0" : "") + String(hours) + ":" +
           (minutes < 10 ? "0" : "") + String(minutes) + ":" +
           (seconds < 10 ? "0" : "") + String(seconds);
}

void renderGraphScreen(bool isReviewMode);

void initTemperatureScreen() {
    tft.fillScreen(GC9A01A_BLACK);
    drawGraphGrid();
    rtc_init();
    datetime_t t = {.year = 2026, .month = 1, .day = 1, .dotw = 4, .hour = 0, .min = 0, .sec = 0};
    rtc_set_datetime(&t);
    rtcStartTime = t;
    startTotalSeconds = 0;
    totalSavedPoints = 0; 
    viewScrollOffset = 0;

    sensors.requestTemperatures();
    float firstTemp = sensors.getTempCByIndex(0);
    
    if (firstTemp == DEVICE_DISCONNECTED_C) {
        currentScreen = SCREEN_HARDWARE_ERROR;
        drawErrorScreen();
    } else {
        tempLog[totalSavedPoints] = firstTemp;
        timeLog[totalSavedPoints] = 0; 
        totalSavedPoints++;
        lastGraphUpdate = millis();
        lastClockUpdate = millis(); 
        renderGraphScreen(false); 
    }
}

void renderGraphScreen(bool isReviewMode) {
    if (totalSavedPoints == 0) return;

    int centerPixelX = graphW / 2; 
    int startIdx = 0;
    int endIdx = totalSavedPoints;

    if (!isReviewMode) {
        if (totalSavedPoints > maxPointsOnScreen) {
            startIdx = totalSavedPoints - maxPointsOnScreen;
        }
        endIdx = totalSavedPoints;
    } else {
        startIdx = -centerPixelX + viewScrollOffset;
        endIdx = startIdx + maxPointsOnScreen;
    }

    float locMin = 999.0f;
    float locMax = -999.0f;
    bool hasVisiblePoints = false;

    for (int i = 0; i < maxPointsOnScreen; i++) {
        int arrayIdx = startIdx + i;
        if (arrayIdx >= 0 && arrayIdx < totalSavedPoints) {
            if (tempLog[arrayIdx] < locMin) locMin = tempLog[arrayIdx];
            if (tempLog[arrayIdx] > locMax) locMax = tempLog[arrayIdx];
            hasVisiblePoints = true;
        }
    }

    if (!hasVisiblePoints) {
        locMin = 20.0f; locMax = 22.0f;
    } else if (locMax == locMin) {
        locMax += 1.0f; locMin -= 1.0f;
    }

    float tempDiff = locMax - locMin;
    if (tempDiff < 2.0f) {
        float mid = (locMax + locMin) / 2.0f;
        locMax = mid + 1.0f; 
        locMin = mid - 1.0f;
    }

    tft.fillRect(graphX, graphY, graphW, graphH, GC9A01A_BLACK);
    drawGraphGrid();

    for (int i = 0; i < maxPointsOnScreen - 1; i++) {
        int arrayIdx1 = startIdx + i;
        int arrayIdx2 = startIdx + i + 1;

        if (arrayIdx1 >= 0 && arrayIdx1 < totalSavedPoints && arrayIdx2 >= 0 && arrayIdx2 < totalSavedPoints) {
            int x1 = graphX + i;
            int x2 = graphX + i + 1;
            int y1 = graphY + graphH - 2 - (int)((tempLog[arrayIdx1] - locMin) * (graphH - 4) / (locMax - locMin));
            int y2 = graphY + graphH - 2 - (int)((tempLog[arrayIdx2] - locMin) * (graphH - 4) / (locMax - locMin));
            tft.drawLine(x1, y1, x2, y2, GC9A01A_YELLOW);
        }
    }

    tft.setTextSize(2);
    tft.fillRect(40, 12, 160, 42, GC9A01A_BLACK); 

    if (!isReviewMode) {
        float currentTemp = tempLog[totalSavedPoints - 1];
        
        datetime_t t_now;
        rtc_get_datetime(&t_now);
        uint32_t currentSec = (t_now.hour * 3600) + (t_now.min * 60) + t_now.sec;
        uint32_t elapsedSec = currentSec - startTotalSeconds;

        tft.setTextColor(GC9A01A_GREEN);
        String curTempStr = String(currentTemp, 1) + " C";
        tft.setCursor(120 - ((curTempStr.length() * 12) / 2), 12);
        tft.print(curTempStr);

        tft.setTextColor(GC9A01A_WHITE);
        String timeStr = formatTimeStr(elapsedSec);
        tft.setCursor(120 - ((timeStr.length() * 12) / 2), 35);
        tft.print(timeStr);
    } else {
        tft.drawFastVLine(120, graphY + 1, graphH - 2, GC9A01A_WHITE);
        int targetDataIdx = startIdx + centerPixelX;
        
        if (targetDataIdx >= 0 && targetDataIdx < totalSavedPoints) {
            float reviewTemp = tempLog[targetDataIdx];
            uint32_t reviewTime = timeLog[targetDataIdx];

            tft.setTextColor(0x5A9F); 
            String revTempStr = String(reviewTemp, 1) + " C";
            tft.setCursor(120 - ((revTempStr.length() * 12) / 2), 12);
            tft.print(revTempStr);

            tft.setTextColor(GC9A01A_YELLOW);
            String revTimeStr = formatTimeStr(reviewTime);
            tft.setCursor(120 - ((revTimeStr.length() * 12) / 2), 35);
            tft.print(revTimeStr);
        } else {
            tft.setTextColor(0x5AEB);
            tft.setCursor(84, 22);
            tft.print("EMPTY");
        }
    }

    tft.setTextSize(1);
    tft.fillRect(30, 198, 180, 12, GC9A01A_BLACK); 
    tft.setTextColor(GC9A01A_RED);
    tft.setCursor(35, 198);
    tft.print("Max: "); tft.print(locMax, 1); tft.print("C");
    tft.setTextColor(0x5A9F); 
    tft.setCursor(140, 198);
    tft.print("Min: "); tft.print(locMin, 1); tft.print("C");
    displayBattery(); 
}
void setup() {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    analogReadResolution(12); // 12-битное разрешение для батареи (0-4095)

    SPI1.setTX(TFT_MOSI);
    SPI1.setSCK(TFT_SCLK);
    SPI1.setRX(TFT_MISO);

    tft.begin();
    tft.setRotation(0);
    tft.fillScreen(GC9A01A_BLACK);

    Wire1.setSDA(TOUCH_SDA);
    Wire1.setSCL(TOUCH_SCL);
    Wire1.begin();

    sensors.begin();

    drawStaticUI();
    drawDrum(0); 
    displayBattery(); 
}

void loop() {
    // Обновление батареи раз в 5 секунд на любом экране
    if (millis() - lastBatUpdate >= 5000) {
        lastBatUpdate = millis();
        displayBattery();
    }

    if (currentScreen == SCREEN_SELECT_INTERVAL) {
        int currentX = 0; int currentY = 0;
        if (getTouchCoordinates(currentX, currentY)) {
            if (!isTouching) {
                startX = currentX; startY = currentY; lastTouchX = currentX; totalDragX = 0;
                isTouching = true; buttonPressedState = false;
            } else {
                int dragDelta = currentX - lastTouchX;
                totalDragX += dragDelta; lastTouchX = currentX;
                if (abs(totalDragX) < 15) {
                    if (currentX >= buttonX && currentX <= (buttonX + buttonW) &&
                        currentY >= buttonY && currentY <= (buttonY + buttonH)) {
                        if (!buttonPressedState) { buttonPressedState = true; drawOKButton(true); }
                    } else {
                        if (buttonPressedState) { buttonPressedState = false; drawOKButton(false); }
                    }
                }
                if (abs(totalDragX) >= 15) {
                    if (buttonPressedState) { buttonPressedState = false; drawOKButton(false); }
                    if (abs(totalDragX) < 110) drawDrum(totalDragX);
                }
            }
        } else {
            if (isTouching) {
                if (abs(totalDragX) < 15) {
                    if (startX >= buttonX && startX <= (buttonX + buttonW) &&
                        startY >= buttonY && startY <= (buttonY + buttonH)) {
                        activeIntervalMs = intervalMs[currentIndex];
                        currentScreen = SCREEN_TEMPERATURE_MONITOR;
                        initTemperatureScreen();
                        isTouching = false; buttonPressedState = false;
                        return; 
                    }
                } 
                if (buttonPressedState) { buttonPressedState = false; drawOKButton(false); }
                if (abs(totalDragX) > 35) { 
                    if (totalDragX > 0) {
                        animateToTarget(totalDragX, 90);
                        currentIndex = (currentIndex - 1 + numIntervals) % numIntervals;
                    } else {
                        animateToTarget(totalDragX, -90);
                        currentIndex = (currentIndex + 1) % numIntervals;
                    }
                }
                drawDrum(0); isTouching = false;
            }
        }
    } 
    else if (currentScreen == SCREEN_TEMPERATURE_MONITOR) {
        int currentX = 0; int currentY = 0;

        // Нажатие 3 секунды для перехода в режим просмотра графиков
        if (getTouchCoordinates(currentX, currentY)) {
            if (touchStartTime == 0) {
                touchStartTime = millis(); 
            } else if (millis() - touchStartTime >= 3000) {
                touchStartTime = 0;
                currentScreen = SCREEN_DATA_REVIEW; 
                tft.fillScreen(GC9A01A_WHITE); delay(80); tft.fillScreen(GC9A01A_BLACK);
                viewScrollOffset = totalSavedPoints;
                renderGraphScreen(true);
                return;
            }
        } else {
            touchStartTime = 0; 
        }

        // Независимое обновление часов и текущей температуры каждую секунду
        if (millis() - lastClockUpdate >= 1000) {
            lastClockUpdate = millis();
            sensors.requestTemperatures();
            float testTemp = sensors.getTempCByIndex(0);
            if (testTemp == DEVICE_DISCONNECTED_C) {
                currentScreen = SCREEN_HARDWARE_ERROR;
                drawErrorScreen();
                return;
            }
            renderGraphScreen(false); 
        }

        // Логика записи данных по таймеру
        if (millis() - lastGraphUpdate >= activeIntervalMs) {
            lastGraphUpdate = millis();
            
            sensors.requestTemperatures();
            float currentTemp = sensors.getTempCByIndex(0);
            
            if (currentTemp == DEVICE_DISCONNECTED_C) {
                currentScreen = SCREEN_HARDWARE_ERROR; 
                drawErrorScreen();
                return;
            }

            if (totalSavedPoints < maxHistory) {
                datetime_t t_now; rtc_get_datetime(&t_now);
                uint32_t currentSec = (t_now.hour * 3600) + (t_now.min * 60) + t_now.sec;

                tempLog[totalSavedPoints] = currentTemp;
                timeLog[totalSavedPoints] = currentSec;
                totalSavedPoints++;
                renderGraphScreen(false); 
            } else {
                currentScreen = SCREEN_DATA_REVIEW;
                tft.fillScreen(GC9A01A_WHITE); delay(80); tft.fillScreen(GC9A01A_BLACK);
                viewScrollOffset = totalSavedPoints;
                renderGraphScreen(true);
                return;
            }
        }
    }
    else if (currentScreen == SCREEN_DATA_REVIEW) {
        int currentX = 0; int currentY = 0;

        if (getTouchCoordinates(currentX, currentY)) {
            if (!isTouching) {
                startX = currentX; lastTouchX = currentX;
                touchStartTime = millis(); 
                isTouching = true;
            } else {
                int dragDelta = currentX - lastTouchX;
                lastTouchX = currentX;

                // Адаптивный скроллинг
                if (abs(dragDelta) > 0) {
                    touchStartTime = millis(); 

                    float speedMultiplier = 1.0f;
                    int absDelta = abs(dragDelta);
                    
                    if (absDelta > 15) speedMultiplier = 2.6f;       
                    else if (absDelta > 6) speedMultiplier = 1.3f;   
                    else speedMultiplier = 0.4f;                     

                    viewScrollOffset -= (dragDelta > 0 ? (int)(absDelta * speedMultiplier) : -(int)(absDelta * speedMultiplier)); 
                    
                    int minScroll = 0;
                    int maxScroll = totalSavedPoints;
                    if (minScroll > maxScroll) minScroll = maxScroll; 
                    
                    if (viewScrollOffset < minScroll) viewScrollOffset = minScroll;
                    if (viewScrollOffset > maxScroll) viewScrollOffset = maxScroll;
                    
                    renderGraphScreen(true);
                }

                // Удержание пальца без движения больше 3 секунд -> выход на ЭКРАН ПОДТВЕРЖДЕНИЯ
                if (abs(currentX - startX) < 5 && (millis() - touchStartTime >= 3000)) {
                    currentScreen = SCREEN_CONFIRM_EXIT;
                    confirmScreenTouchLocked = true; // АКТИВИРУЕМ ЗАЩИТУ ОТ ЛОЖНОГО КЛИКА
                    drawConfirmScreen();
                    isTouching = false;
                    return;
                }
            }
        } else {
            isTouching = false;
        }
    }
    else if (currentScreen == SCREEN_CONFIRM_EXIT) {
        int currentX = 0; int currentY = 0;

        if (getTouchCoordinates(currentX, currentY)) {
            // Если экран заблокирован защитой, полностью игнорируем любые касания и удержания
            if (confirmScreenTouchLocked) {
                return; 
            }

            if (!isTouching) {
                startX = currentX; startY = currentY;
                isTouching = true;
            } else {
                bool isOverYes = (currentX >= yesBtnX && currentX <= (yesBtnX + yesBtnW) && currentY >= yesBtnY && currentY <= (yesBtnY + yesBtnH));
                bool isOverNo = (currentX >= noBtnX && currentX <= (noBtnX + noBtnW) && currentY >= noBtnY && currentY <= (noBtnY + noBtnH));
                drawConfirmButtons(isOverYes, isOverNo);
            }
        } else {
            // Если палец убрали с экрана — снимаем флаг защиты, теперь клики разрешены!
            if (confirmScreenTouchLocked) {
                confirmScreenTouchLocked = false;
                return;
            }

            if (isTouching) {
                bool isOverYes = (startX >= yesBtnX && startX <= (yesBtnX + yesBtnW) && startY >= yesBtnY && startY <= (yesBtnY + yesBtnH));
                bool isOverNo = (startX >= noBtnX && startX <= (noBtnX + noBtnW) && startY >= noBtnY && startY <= (noBtnY + noBtnH));

                if (isOverYes) {
                    currentScreen = SCREEN_SELECT_INTERVAL;
                    tft.fillScreen(GC9A01A_BLACK);
                    drawStaticUI();
                    drawDrum(0);
                } else if (isOverNo) {
                    currentScreen = SCREEN_DATA_REVIEW;
                    tft.fillScreen(GC9A01A_BLACK);
                    renderGraphScreen(true);
                } else {
                    drawConfirmButtons(false, false);
                }
                isTouching = false;
            }
        }
    }
    else if (currentScreen == SCREEN_HARDWARE_ERROR) {
        int currentX = 0; int currentY = 0;
        if (getTouchCoordinates(currentX, currentY)) {
            if (touchStartTime == 0) {
                touchStartTime = millis();
            } else if (millis() - touchStartTime >= 3000) {
                touchStartTime = 0;
                currentScreen = SCREEN_SELECT_INTERVAL;
                tft.fillScreen(GC9A01A_BLACK);
                drawStaticUI();
                drawDrum(0);
                return;
            }
        } else {
          touchStartTime = 0;
        }
      }
  delay(10);
}