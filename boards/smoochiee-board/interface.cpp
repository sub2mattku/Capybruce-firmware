#include "core/powerSave.h"

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/

#ifdef XPOWERS_CHIP_BQ25896
#include <Wire.h>
#include <XPowersLib.h>
XPowersPPM PPM;
#endif

// ============================================================================
// KY-040 ENCODER STATE
// ============================================================================
volatile int encoderPos = 0;
volatile int lastEncoderPos = 0;
volatile uint8_t lastCLK = 0;

// ============================================================================
// INTERRUPT SERVICE ROUTINES
// ============================================================================

void IRAM_ATTR onEncoderCLK() {
    uint8_t clk = digitalRead(ENC_CLK);
    uint8_t dt = digitalRead(ENC_DT);
    
    if (clk == HIGH && lastCLK == LOW) {
        if (dt == HIGH) {
            encoderPos--;
        } else {
            encoderPos++;
        }
    }
    lastCLK = clk;
}

void IRAM_ATTR onEncoderSW() {
    // Handled in polling loop for debounce
}

// ============================================================================
// SETUP
// ============================================================================

void _setup_gpio() {
    pinMode(ENC_CLK, INPUT_PULLUP);
    pinMode(ENC_DT, INPUT_PULLUP);
    pinMode(ENC_SW, INPUT_PULLUP);
    
    lastCLK = digitalRead(ENC_CLK);
    
    attachInterrupt(digitalPinToInterrupt(ENC_CLK), onEncoderCLK, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC_SW), onEncoderSW, CHANGE);

    pinMode(CC1101_SS_PIN, OUTPUT);
    pinMode(NRF24_SS_PIN, OUTPUT);
    digitalWrite(CC1101_SS_PIN, HIGH);
    digitalWrite(NRF24_SS_PIN, HIGH);

    bruceConfigPins.rfModule = CC1101_SPI_MODULE;
    bruceConfigPins.irRx = RXLED;
    Wire.setPins(GROVE_SDA, GROVE_SCL);
    
    bool pmu_ret = false;
    Wire.begin(GROVE_SDA, GROVE_SCL);
    pmu_ret = PPM.init(Wire, GROVE_SDA, GROVE_SCL, BQ25896_SLAVE_ADDRESS);
    if (pmu_ret) {
        PPM.setSysPowerDownVoltage(3300);
        PPM.setInputCurrentLimit(3250);
        Serial.printf("getInputCurrentLimit: %d mA\n", PPM.getInputCurrentLimit());
        PPM.disableCurrentLimitPin();
        PPM.setChargeTargetVoltage(4208);
        PPM.setPrechargeCurr(64);
        PPM.setChargerConstantCurr(832);
        PPM.getChargerConstantCurr();
        Serial.printf("getChargerConstantCurr: %d mA\n", PPM.getChargerConstantCurr());
        PPM.enableMeasure(PowersBQ25896::CONTINUOUS);
        PPM.disableOTG();
        PPM.enableCharge();
    }
}

bool isCharging() {
    return PPM.isCharging();
}

int getBattery() {
    int voltage = PPM.getBattVoltage();
    int percent = (voltage - 3300) * 100 / (float)(4150 - 3350);

    if (percent < 0) return 1;
    if (percent > 100) percent = 100;

    if (PPM.isCharging() && percent >= 97) {
        PPM.disableBatLoad();
        percent = 95;
    }

    if (PPM.isChargeDone()) { percent = 100; }

    return percent;
}

/*********************************************************************
** Function: setBrightness
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    if (brightval == 0) {
        analogWrite(TFT_BL, brightval);
    } else {
        int bl = MINBRIGHT + round(((255 - MINBRIGHT) * brightval / 100));
        analogWrite(TFT_BL, bl);
    }
}

/*********************************************************************
** Function: InputHandler
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = 0;
    static unsigned long swPressTime = 0;
    static bool swWasPressed = false;
    
    if (millis() - tm < 50 && !LongPress) return;
    tm = millis();

    // --- Encoder Rotation ---
    noInterrupts();
    int currentPos = encoderPos;
    interrupts();
    
    int delta = currentPos - lastEncoderPos;
    
    if (delta != 0) {
        if (!wakeUpScreen()) AnyKeyPress = true;
        else {
            lastEncoderPos = currentPos;
            return;
        }
        
        if (delta > 0) {
            NextPress = true;
            DownPress = true;
            NextPagePress = true;
        } else {
            PrevPress = true;
            UpPress = true;
            PrevPagePress = true;
        }
        
        lastEncoderPos = currentPos;
    }

    // --- Encoder Button (SW) ---
    bool swCurrent = !digitalRead(ENC_SW);
    
    if (swCurrent && !swWasPressed) {
        swPressTime = millis();
        swWasPressed = true;
        if (!wakeUpScreen()) AnyKeyPress = true;
        else return;
    }
    
    if (swCurrent && swWasPressed) {
        if (millis() - swPressTime > 800) {
            EscPress = true;
            SelPress = false;
            LongPress = true;
        }
    }
    
    if (!swCurrent && swWasPressed) {
        if (millis() - swPressTime < 800 && millis() - swPressTime > 50) {
            SelPress = true;
        }
        swWasPressed = false;
        LongPress = false;
    }

    // --- Legacy button polling ---
    bool _l = digitalRead(L_BTN);
    bool _r = digitalRead(R_BTN);
    bool _s = digitalRead(SEL_BTN);

    if (!_s) {
        tm = millis();
        if (!wakeUpScreen()) AnyKeyPress = true;
        else return;
    }
}

/*********************************************************************
** Function: powerOff
**********************************************************************/
void powerOff() {
    esp_sleep_enable_ext0_wakeup((gpio_num_t)ENC_SW, BTN_ACT);
    esp_deep_sleep_start();
}

/*********************************************************************
** Function: checkReboot
**********************************************************************/
void checkReboot() {
    int countDown = 0;
    
    if (digitalRead(ENC_SW) == BTN_ACT) {
        uint32_t time_count = millis();
        while (digitalRead(ENC_SW) == BTN_ACT) {
            if (millis() - time_count > 500) {
                if (countDown == 0) {
                    int textWidth = tft.textWidth("PWR OFF IN 3/3", 1);
                    tft.fillRect(tftWidth / 2 - textWidth / 2, 7, textWidth, 18, bruceConfig.bgColor);
                }
                tft.setTextSize(1);
                tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
                countDown = (millis() - time_count) / 1000 + 1;
                if (countDown < 4)
                    tft.drawCentreString("PWR OFF IN " + String(countDown) + "/3", tftWidth / 2, 12, 1);
                else {
                    tft.fillScreen(bruceConfig.bgColor);
                    while (digitalRead(ENC_SW) == BTN_ACT);
                    delay(200);
                    powerOff();
                }
                delay(10);
            }
        }
        
        delay(30);
        if (millis() - time_count > 500) {
            tft.fillRect(60, 12, tftWidth - 60, tft.fontHeight(1), bruceConfig.bgColor);
            drawStatusBar();
        }
    }
}
