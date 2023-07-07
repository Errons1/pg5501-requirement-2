#include <Arduino.h>
#include <SPI.h>

// TFT ST7789 library
#include <Adafruit_ST7789.h>

// Clock ZS-042 library
#include <Wire.h>
#include <RtcDS3231.h>

// RFID RC522 library
#include <MFRC522.h>

// IRremote / sensor HW-477 v0.2 library
#include <IRremote.h>
#include <IRReceive.hpp>

// I2C protocol
int8_t scl = A5;        // Serial Clock
int8_t sda = A4;        // Serial Data

// SPI protocol
int8_t sclk = 13;       // Serial Clock
int8_t miso = 12;       // Master In Slave Out
int8_t mosi = 11;       // Master Out Slave In
int8_t ss1 = 10;        // Slave Select Adafruit 240x135 Color TFT LCD ST7789

// Setup for ST7789
int8_t dc = 7; // TFT SPI data or command selector pin
Adafruit_ST7789 tft = Adafruit_ST7789(ss1, dc, -1);

// Setup for ZS-042
RtcDS3231<TwoWire> rtc(Wire);

// Setup for RFID RC522 Module
//int8_t irq = 5; // interrupt pin, alerts when an RFID tag is in the vicinity.
int8_t rst = 6; // reset and power-down
MFRC522 mfrc522(ss2, rst);

// Setup IRremote / sensor HW-477 v0.2
int8_t irData = 5; // input stream of IR-remote date

// Setup HW-416-B Motion Sensor
int8_t motionData = 4;

// Setup SY-M213 Sound Sensor
int8_t soundDataDigital = 3;
int8_t soundDataAnalog = A0;


void setup() {
    Serial.begin(9600);
    SPIClass::begin();

//    Config of TFT screen ST7789
    tft.init(135, 240);
    tft.setRotation(1);
    tft.fillScreen(ST77XX_BLACK);

//    Config of clock (DS3231) on chip ZS-042
    rtc.Begin();
    RtcDateTime dateTime = RtcDateTime(__DATE__, __TIME__);
    rtc.SetDateTime(dateTime);
    rtc.Enable32kHzPin(false);
    rtc.SetSquareWavePin(DS3231SquareWavePin_ModeNone);

//    Config RFID RC522 Module
    mfrc522.PCD_Init();
//    mfrc522.PCD_DumpVersionToSerial();

//    Config IRremote sensor HW-477 v0.2
    IrReceiver.begin(irData);

//    Config HC-SR Motion Sensor
    pinMode(motionData, INPUT);

//    Config SY-M213 Sound Sensor
    pinMode(soundDataDigital, INPUT);
    pinMode(soundDataAnalog, INPUT);

//    To make system to catch up before running state starts
    delay(500);
}

//  int values for IR remote control
//  if ir reader starts sending 0 means it lost streams of data and cant read anymore
int inputIr = 0;
int valuePower = 69;
int valueReset = 71;
int valuePrevious = 68;
int valueSelect = 64;
int valueNext = 67;
int value0 = 22;
int value1 = 12;
int value2 = 24;
int value3 = 94;
int value4 = 8;
int value5 = 28;
int value6 = 90;
int value7 = 66;
int value8 = 82;
int value9 = 74;

int adminPassword[] = {value0, value0, value0, value0};
byte adminCard[] = { 0xA1, 0x8E, 0x0F, 0x1D};

MFRC522::Uid emptyUid = {0};
int inputPassword[4] = {0};
int inputPasswordIndex = 0;

//    current system states
int currentState = 0;
int screenState = 10;

//    states system can be in
int stateOffline = -1;
int stateIdle = 0;
int stateOn = 1;
int stateAlarm = 2;

//    old millis for preventing using delay()
unsigned long timeNow = 0;
unsigned long timeOldIr = 0;
unsigned long timeOldAlarm = 0;

void loginAndTurnOffAlarmPICC(const byte *buffer, byte bufferSize);
void loginAndTurnOffAlarmCode();
void printStatusOffline();
void printStatusIdle();
void printStatusOn();
void printStatusAlarm();

void loop() {
    timeNow = millis();
    Serial.println("testtest");
//      Reading IR remote
//      After testing, to prevent doubles sending of value, a next read can only happen after 300ms
    if (IrReceiver.decode() && timeNow - timeOldIr >= 300) {
        IrReceiver.resume();
        inputIr = (int) IrReceiver.decodedIRData.command;
        timeOldIr = timeNow;
        Serial.println(inputIr);
    } else {
        inputIr = 0;
    }

//    Reset the loop if no new card present on the sensor/reader. This saves the entire process when idle.
    if (mfrc522.PICC_IsNewCardPresent()) {
//        Select one of the cards
        mfrc522.PICC_ReadCardSerial();
//        Halt PICC
        mfrc522.PICC_HaltA();
    } else {
        mfrc522.uid = emptyUid;
    }


//    ------------------- alarm state ----------------------
//    Do completely nothing, device is turned off
    if (currentState == stateOffline) {
        printStatusOffline();

//        Turn on the system
        if (inputIr == valuePower) {
            currentState = stateIdle;
        }
    }

//    Device is on and show menu on screen, but sensor won't trigger
    else if (currentState == stateIdle) {
        printStatusIdle();

//        Turn on alarm
        if (inputIr == valueSelect) {
            currentState = stateOn;
        }
//        Turn off the system
        else if (inputIr == valuePower) {
            currentState = stateOffline;
        }
    }

//    Device is on and alarm will trigger on sensor
    else if (currentState == stateOn) {
        printStatusOn();
        loginAndTurnOffAlarmCode();
        loginAndTurnOffAlarmPICC(mfrc522.uid.uidByte, mfrc522.uid.size);

//    Code for reading motion
        int motionValue = digitalRead(motionData);

//    Code for printing sound
        int soundValueAnalog = analogRead(soundDataAnalog);

        if (motionValue == 1 || soundValueAnalog >= 100) {
            currentState = stateAlarm;
        }
    }

//    Device is on and alarm is triggered
    else if (currentState == stateAlarm) {
//        Function that controls how the screen looks like
        printStatusAlarm();

//        Need to log in to turn of alarm
        loginAndTurnOffAlarmCode();
        loginAndTurnOffAlarmPICC(mfrc522.uid.uidByte, mfrc522.uid.size);
    }

}


void loginAndTurnOffAlarmPICC(const byte *buffer, byte bufferSize) {
    for (int i = 0; i < bufferSize; ++i) {
        if (buffer[i] != adminCard[i]) {
            break;
        }

        if (i == 3) {
            currentState = stateIdle;
        }
    }
}

void loginAndTurnOffAlarmCode() {
//        input needs to be numbers
    if (inputIr != 0 && inputPasswordIndex <= 3) {
        inputPassword[inputPasswordIndex++] = inputIr;
    }

//    if 4 numbers are used need to check with account
    else if (inputPasswordIndex >= 4) {

        for (int i = 0; i < 4; ++i) {
            if (adminPassword[i] != inputPassword[i]) {
                break;
            }

            if (i == 3) {
                currentState = stateIdle;
            }
        }

        inputPasswordIndex = 0;
    }
}

void printStatusOffline() {
    if (screenState != stateOffline) {
        tft.fillScreen(ST77XX_BLACK);
        screenState = stateOffline;
    }
}

void printStatusIdle() {
    if (screenState != stateIdle){
        tft.invertDisplay(true);
        tft.fillScreen(ST77XX_WHITE);
        tft.setTextColor(ST77XX_BLACK);
        tft.setTextSize(3);
        tft.setCursor(0, 50);
        tft.println("Status: Idle");
        screenState = stateIdle;
    }
}

void printStatusOn() {
    if (screenState != stateOn) {
        tft.invertDisplay(true);
        tft.fillScreen(ST77XX_WHITE);
        tft.setTextColor(ST77XX_BLACK);
        tft.setTextSize(3);
        tft.setCursor(0, 50);
        tft.print("Status: On");
        screenState = stateOn;
    }
}

void printStatusAlarm() {
    RtcDateTime now;
    char date[20];

//        Gives correct background and set what date is when alarm goes of.
    if (screenState != stateAlarm) {
        now = rtc.GetDateTime();
        snprintf_P(date, sizeof(date) / sizeof(date[0]), PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
                   now.Month(),
                   now.Day(),
                   now.Year(),
                   now.Hour(),
                   now.Minute(),
                   now.Second());

        tft.fillScreen(ST77XX_RED);
        tft.setTextColor(ST77XX_WHITE);
        tft.setTextSize(4);
        tft.setCursor(60, 25);
        tft.println("Alarm");
        tft.println(date);

        screenState = stateAlarm;
    }

    if (timeNow - timeOldAlarm >= 2000) {
        timeOldAlarm = timeNow;
        tft.invertDisplay(false);

    } else if (timeNow - timeOldAlarm >= 1000) {
        tft.invertDisplay(true);
    }
}