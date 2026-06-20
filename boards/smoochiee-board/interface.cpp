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
// KY-040 ENCODER STATE TABLE (Full Quadrature Debounce)
// ============================================================================
// Valid state transitions only — ignores contact bounce and invalid states
// State = (old_DT << 3 | old_CLK << 2 | new_DT << 1 | new_CLK)
// Output: -1 (CCW), 0 (invalid/bounce), +1 (CW)

const int8_t ENCODER_STATES[] = {
    0,  0,  0,  0,   // 0000 to 0011: invalid
    0,  0,  1,  0,   // 0100: no change, 0101: invalid, 0110: CW, 0111: invalid
    0, -1,  0,  0,   // 1000: no change, 1001: CCW, 1010: invalid, 1011: invalid
    0,  0,  0,  0    // 1100 to 1111: invalid
};

volatile int encoderPos = 0;
volatile int lastEncoderPos = 0;
volatile uint8_t encoderState = 0;

// ============================================================================
// INTERRUPT SERVICE ROUTINES
// ============================================================================

void IRAM_ATTR onEncoderCLK() {
    // Read current pin states
    uint8_t dt = digitalRead(ENC_DT);
    uint8_t clk = digitalRead(ENC_CLK);
    
    // Build 4-bit state: (prev_DT << 3 | prev_CLK << 2 | curr_DT << 1 | curr_CLK)
    // encoderState holds the previous 2 bits from last call
    uint8_t newState = ((encoderState & 0x03) << 2) | (dt << 1) | clk;
    
    // Look up valid transition
    int8_t delta = ENCODER_STATES[newState & 0x0F];
    encoderPos += delta;
    
    // Save current state for next interrupt
    encoderState = newState & 0x03;
}

void IRAM_ATTR onEncoderDT() {
    // Optional: can use both interrupts for higher resolution
    // For now, CLK interrupt with state table is sufficient
}

void IRAM_ATTR onEncoderSW() {
    // Handled in polling loop for debounce
}

// ============================================================================
// SETUP
// ============================================================================

void _setup_gpio() {
    // --- KY-040 Encoder Pins ---
    pinMode(ENC_CLK, INPUT_PULLUP);
    pinMode(ENC_DT, INPUT_PULLUP);
    pinMode(ENC_SW, INPUT_PULLUP);
    
    // Initialize encoder state before attaching interrupts
    encoderState = (digitalRead(ENC_DT) << 1) | digitalRead(ENC_CLK);
    
    // Attach interrupts
    attachInterrupt(digitalPinToInterrupt(ENC_CLK), onEncoderCLK, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC_DT), onEncoderDT, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC_SW), onEncoderSW, CHANGE);

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
    
    // Relaxed polling — state table handles the heavy lifting
    if (millis() - tm < 30 && !LongPress) return;
    tm = millis();

    // --- Process Encoder Rotation ---
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
            // Clockwise
            NextPress = true;
            DownPress = true;
            NextPagePress = true;
        } else {
            // Counter-clockwise
            PrevPress = true;
            UpPress = true;
            PrevPagePress = true;
        }
        
        lastEncoderPos = currentPos;
    }

    // --- Process Encoder Button (SW) ---
    bool swCurrent = !digitalRead(ENC_SW); // Active LOW
    
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
    esp_sleep_enable_ext0_wakeup((gpio_num_t)ENC_SW, BTN_ACT);
    esp_deep_sleep_start();
}

/*********************************************************************
** Function: checkReboot
**********************************************************************/
void checkReboot() {
    int countDown = 0;
    
    // Long press encoder SW to power off
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
