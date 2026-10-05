#include "core/powerSave.h"

#ifdef FP
#undef FP
#endif

/***************************************************************************************
** Function name: _setup_gpio()
***************************************************************************************/

#ifdef XPOWERS_CHIP_BQ25896
#include <Wire.h>
#include <XPowersLib.h>
XPowersPPM PPM;
#endif

// ============================================================================
// KY-040 ENCODER — QUADRATURE STATE TABLE DECODER
// ============================================================================
// Index = (prev_DT << 3) | (prev_CLK << 2) | (curr_DT << 1) | curr_CLK
// +1 = CW, -1 = CCW, 0 = illegal transition (bounce / both pins changed)
//
// FIX: read from an ISR -> must be in RAM, not flash-mapped rodata.
// (Flash cache is disabled during NVS/LittleFS writes; accessing this
// table from the ISR at that moment hard-faults the ESP32.)
static DRAM_ATTR const int8_t ENCODER_STATES[16] = {
    0,  1, -1,  0,
   -1,  0,  0,  1,
    1,  0,  0, -1,
    0, -1,  1,  0
};

// One KY-040 detent = one full quadrature cycle = 4 transitions.
// (Set to 2 only if your encoder is a half-cycle-per-detent type.)
#define ENC_COUNTS_PER_DETENT 4

static portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;

volatile int     encoderPos     = 0;   // committed detents
volatile int     lastEncoderPos = 0;   // last count consumed by InputHandler
volatile int8_t  encoderDelta   = 0;   // transitions since last commit
volatile uint8_t encoderState   = 0;   // last (DT << 1) | CLK

// ============================================================================
// ISR — attached to CHANGE of CLK and DT. NO time filter on purpose:
// the state table IS the debouncer. Every bounce is either an illegal
// transition (0) or a reversal that cancels the previous count.
// ============================================================================
void IRAM_ATTR handleEncoderISR() {
    uint8_t curr = ((uint8_t)digitalRead(ENC_DT) << 1) | (uint8_t)digitalRead(ENC_CLK);

    portENTER_CRITICAL_ISR(&encoderMux);

    uint8_t prev = encoderState;
    if (curr != prev) {                    // edge vanished before we looked -> glitch
        encoderState = curr;

        int8_t d = ENCODER_STATES[(prev << 2) | curr];
        if (d != 0) {
            encoderDelta += d;

            // FIX: commit one UI step per mechanical detent, not per raw
            // transition (4 raw counts = 1 click). Remainder is carried,
            // so a rare missed edge never turns into a wrong count.
            if (encoderDelta >= ENC_COUNTS_PER_DETENT) {
                encoderPos++;
                encoderDelta -= ENC_COUNTS_PER_DETENT;
            } else if (encoderDelta <= -ENC_COUNTS_PER_DETENT) {
                encoderPos--;
                encoderDelta += ENC_COUNTS_PER_DETENT;
            }
        }
    }

    portEXIT_CRITICAL_ISR(&encoderMux);
}

// ============================================================================
// SETUP
// ============================================================================

void _setup_gpio() {
    // --- KY-040 Encoder Pins ---
    pinMode(ENC_CLK, INPUT_PULLUP);
    pinMode(ENC_DT,  INPUT_PULLUP);
    pinMode(ENC_SW,  INPUT_PULLUP);
    delay(10);                             // let pull-ups settle

    encoderState = (digitalRead(ENC_DT) << 1) | digitalRead(ENC_CLK);

    // FIX: same decoder on both pins; removed the empty ENC_SW CHANGE ISR
    // (the switch is debounced by polling in InputHandler).
    attachInterrupt(digitalPinToInterrupt(ENC_CLK), handleEncoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC_DT),  handleEncoderISR, CHANGE);

    // --- RF Module SPI CS (keep for compile compatibility) ---
    pinMode(CC1101_SS_PIN, OUTPUT);
    pinMode(NRF24_SS_PIN, OUTPUT);
    digitalWrite(CC1101_SS_PIN, HIGH);
    digitalWrite(NRF24_SS_PIN, HIGH);

    // --- I2C & PMU ---
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
    static bool swLongFired = false;       // FIX: one-shot long press

    if (millis() - tm < 30 && !LongPress) return;
    tm = millis();

    // --- Encoder Rotation ---
    int currentPos;
    portENTER_CRITICAL(&encoderMux);
    currentPos = encoderPos;
    portEXIT_CRITICAL(&encoderMux);

    int delta = currentPos - lastEncoderPos;
    if (delta != 0) {
        lastEncoderPos = currentPos;       // always sync (wake or not)

        if (!wakeUpScreen()) AnyKeyPress = true;
        else return;                       // this turn only woke the screen

        if (delta > 0) {
            NextPress = true; DownPress = true; NextPagePress = true;
        } else {
            PrevPress = true; UpPress = true; PrevPagePress = true;
        }
    }

    // --- Encoder Button (SW), active LOW ---
    // 30 ms polling interval + 50 ms minimum press = adequate debounce.
    bool swCurrent = !digitalRead(ENC_SW);

    if (swCurrent && !swWasPressed) {                 // press
        swPressTime = millis();
        swWasPressed = true;
        swLongFired  = false;
        if (!wakeUpScreen()) AnyKeyPress = true;
        else return;
    }

    if (swCurrent && swWasPressed && !swLongFired) {  // held
        if (millis() - swPressTime > 800) {
            // FIX: fire EscPress exactly once (old code re-armed it every
            // iteration, and the 30 ms gate is bypassed while LongPress).
            swLongFired = true;
            LongPress = true;
            EscPress  = true;
        }
    }

    if (!swCurrent && swWasPressed) {                 // release
        if (!swLongFired && millis() - swPressTime > 50) {
            SelPress = true;
        }
        swWasPressed = false;
        LongPress = false;
    }

    // --- Legacy button polling (for code that reads raw pins) ---
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
    detachInterrupt(digitalPinToInterrupt(ENC_CLK));
    detachInterrupt(digitalPinToInterrupt(ENC_DT));
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
