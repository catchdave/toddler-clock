
#include "toddler-clock.h"
#include <EEPROM.h>
#include <Wire.h> 
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"
#include "RTClib.h"
#include <NoDelay.h>
#include "ezButton.h"

Adafruit_7segment clockDisplay = Adafruit_7segment();
RTC_DS3231 rtc;

void printStatus();
void blinkLEDLights();

// Pins
const int pinPot             = A0;
const int pinModeButton      = 3;  // white button
const int pinStatusButton    = 8;  // red button The override button for the wake/sleep light (AKA Status Light)
const int pinTempButton      = 2; // orange button
const int pinStatusButtonLED = A1; // red led button. The LED for the statuslight button.
const int pinTempButtonLED   = A2; // orange led button
const int pinModeButtonLED   = A3; // white led button
const int pinRgbRed          = 9;
const int pinRgbGreen        = 10;
const int pinRgbBlue         = 11;
const int pinSetUp           = 4;
const int pinSetDown         = 5;
const int pinDstButton       = 13; // yellow switch

/** Unused hardware: red/green status light. The green light doesn't really shine through wood veneer well, so disabled for now
const int pinRedStatus       = 6;
const int pinGreenStatus     = 7;
**/

// Settings
const bool DEBUG                    = false; // Set to true to print debug stats to console every second
const int RESET_TIME_TO_COMPUTER    = false; // change to true to re-sync time to computer
const int LONG_PRESS_TIME           = 2500; // 2.5 secs
const int ADJUST_TIME_DEFAULT_SPEED = 300; // in milliseconds
const int MAX_SPEED                 = 25; // max speed for time change using up/down buttons (in 100ms units, so 25 = 2.5 secs)
const int DEFAULT_WAKE_LENGTH       = 60 * 60; // in seconds (3,600 = 1 hour)
const long MAX_OVERRIDE_TIME        = 30 * 60000; // in milliseconds (1.8M = 30 minutes)
const long MAX_TEMP_DISPLAY_TIME    = 2 * 60000; // in milliseconds (120K = 2 minutes)
const float DEFAULT_SLEEP_TIME      = 19.75; // 24-hr decimal time (e.g. 6:45pm is 18.75)
const float DEFAULT_WAKE_TIME       = 6.5; // 24-hr decimal time (e.g. 6:30am is 6.5)

// Non-setting Constants
const int ALARM_SLEEP               = 1;
const int ALARM_WAKE                = 2;
bool USE_24_HR_CLOCK                = false; // current state of 24-hour value 
bool USE_CELCIUS                    = false; // current state of celicius value

// Variables
int dimmerValue = 0;       // variable to store the value coming from the sensor
int dimmerRawValue = 0;
int CUR_BRIGHTNESS = 15;
bool colon = true;
StatusLight statusLight = OFF;
StatusLight statusLightOverride = OFF;
unsigned long modeButtonPressedTime, modeButtonReleasedTime = 0;
bool blinkLEDState = false;
int curAdjustSpeed = ADJUST_TIME_DEFAULT_SPEED;
int DST;
unsigned long millisSinceOverrideStart; // millis when override mode was last activated
unsigned long millisSinceTempStart;     // millis when temp display was last activated

// Object variables
ToddlerClockMode MODE;
DateTime timesetDateTime; // current DT object that is being modified
DateTime now;
DateTime wakeLightOffTime;
noDelay writeTimeToDisplay(500);
noDelay debugTimer(1000, printStatus);
noDelay blinkTimer(300, blinkLEDLights); // generic timer for blinking all LEDs
noDelay adjustTime(ADJUST_TIME_DEFAULT_SPEED);
ezButton buttonStatusRotate(pinStatusButton);
ezButton buttonMode(pinModeButton);
ezButton buttonTempShow(pinTempButton);
ezButton buttonSetUp(pinSetUp);
ezButton buttonSetDown(pinSetDown);
ezButton buttonDst(pinDstButton);

void setRgbColor(int red, int green, int blue)
{
  // Common anode
  red = 255 - red;
  green = 255 - green;
  blue = 255 - blue;
  analogWrite(pinRgbRed, red);
  analogWrite(pinRgbGreen, green);
  analogWrite(pinRgbBlue, blue);  
}

void setup()
{
  Serial.begin(57600);
  Serial.println();
  Serial.println(F("-~- Toddler Clock Initializing -~-"));

  clockDisplay.begin(0x70);
  initializeRtc();
  now = rtc.now();

  pinMode(pinStatusButtonLED, OUTPUT); 
  pinMode(pinModeButtonLED, OUTPUT); 
  pinMode(pinTempButtonLED, OUTPUT);
  pinMode(pinRgbRed, OUTPUT);
  pinMode(pinRgbGreen, OUTPUT);
  pinMode(pinRgbBlue, OUTPUT);
  
  buttonStatusRotate.setDebounceTime(50);
  buttonMode.setDebounceTime(50);
  buttonTempShow.setDebounceTime(50);
  buttonSetUp.setDebounceTime(50);
  buttonSetDown.setDebounceTime(50);
  buttonDst.setDebounceTime(50);

  setRgbColor(0, 0, 0);
  initizalizeDST();

  showAlarmTimes();
  checkIfMissedAlarms();
  Serial.println(F("-~- Toddler Clock Initialization complete -~-"));
}

void setDaylightSavings(int newDstState)
{
  int secsChange;
  if (newDstState == 1) {
    secsChange = 3600;
    DST = 1; 
  }
  else {
    secsChange = -3600;
    DST = 0;
  }

  TimeSpan adjustment(secsChange);
  rtc.adjust(rtc.now() + adjustment);
  EEPROM.put(0, DST);

  checkIfMissedAlarms();
}

/**
 * Checks if events have timed out and need to be reset
 */
void checkEventTimeouts()
{
  unsigned long curMillis = millis();
 
  // Check if temperature display has exceeded MAX_TEMP_DISPLAY_TIME
  if (MODE.isTemperature() && (curMillis - millisSinceTempStart > MAX_TEMP_DISPLAY_TIME)) {
    millisSinceTempStart = 0;

    MODE.reset();
    digitalWrite(pinTempButtonLED, LOW);
  }

  // Check if override mode has exceeded MAX_OVERRIDE_TIME
  if (statusLightOverride != OFF && (curMillis - millisSinceOverrideStart > MAX_OVERRIDE_TIME)) {
    millisSinceOverrideStart = 0;

    printMessageWithTime(F("Turning off 'override' mode since 30 mins has elapsed"));
    turnOffStatusOverride(); // override has been on for max time, turn off override
  }
}

/*
 * Main arduino loop
 */
void loop()
{
  bool displayNeedsUpdating = false;
  
  // Update time display every 500ms
  if (writeTimeToDisplay.update()) {
    displayNeedsUpdating = true;
    checkEventTimeouts();
    checkAlarms();
  }

  if (dimmerPotMonitor()) { // DST button [green potentimeter]
    clockDisplay.writeDisplay();
  }

  if (DSTButtonMonitor()) { // DST button [yellow switch]
    displayNeedsUpdating = true;
  }

  if (temperatureButtonMonitor()) { // Temperature display button [orange LED button]
    displayNeedsUpdating = true;
  }

  if (statusButtonMonitor()) { // Status button (status rotate and set alarm) [red LED button]
    displayNeedsUpdating = true;
  }

  if (modeButtonMonitor()) { // Mode button (mode and set time) [white LED button]
    displayNeedsUpdating = true;
  }

  if (timeChangeMonitor(timesetDateTime)) { // SIDE-EFFECT: updates "timesetDateTime" variable
    displayNeedsUpdating = true;
  }

  // Update the display when needed
  if (displayNeedsUpdating) {
    updateDisplay(timesetDateTime); // SIDE-EFFECT: updates "timesetDateTime" variable
  }

  blinkTimer.update();
  if (DEBUG) debugTimer.update();
}
// -- end main loop 

/**
 * Updates the main 4-digit LED display
 */
void updateDisplay(DateTime &timesetDateTime)
{
  now = rtc.now();
  if (MODE.modeJustChanged()) {
    if (MODE.isSetTime()) { // time-set mode just activated
      timesetDateTime = now;
    }
    else if (MODE.isSetSleepAlarm()) {
      timesetDateTime = rtc.getAlarm(ALARM_SLEEP);
    }
    else if (MODE.isSetWakeAlarm()) {
      timesetDateTime = rtc.getAlarm(ALARM_WAKE);
    } 
  }
    
  if (MODE.isTemperature()) {
    updateTemperatureDisplay();
  } else {
    updateTimeDisplay(MODE.isInAnySet() ? timesetDateTime : now);
  }
}

/**
 * Blink LED lights
 */
void blinkLEDLights()
{
  blinkLEDState = !blinkLEDState;
    
  if (MODE.isSetTime()) {
    digitalWrite(pinModeButtonLED, blinkLEDState ? HIGH : LOW);
    if (blinkLEDState) {
      setRgbColor(148, 0, 211); // purple
    }
  }
  else if (MODE.isSetSleepAlarm()) {
    if (blinkLEDState) {
      setRgbColor(255, 0, 0); // red
    }
  }
  else if (MODE.isSetWakeAlarm()) {
    if (blinkLEDState) {
      setRgbColor(0, 255, 0); // green
    }
  }
  if (MODE.isSetAnyAlarm()) {
    digitalWrite(pinStatusButtonLED, blinkLEDState ? HIGH : LOW);
  }
  if (MODE.isInAnySet()) {
    if (!blinkLEDState) {
      setRgbColor(0, 0, 0);
    }
  }
}


int extractDigit(int V, int P)
{
  unsigned int pow [] = { 1, 10, 100, 1000, 10000 } ;
  return abs(V) / pow[P] % 10;
}

/**
 * Updates the display to show the current temperature in either C or F.
 */
void updateTemperatureDisplay()
{
  float celcius = rtc.getTemperature();
  float temperatureVal;
  millisSinceTempStart = millis();

  clockDisplay.clear();
    
  if (USE_CELCIUS) {
    temperatureVal = celcius;
    clockDisplay.writeDigitNum(4, 0xc);
  }
  else {
    temperatureVal = (celcius * 9/5) + 32;
    clockDisplay.writeDigitNum(4, 0xf);
  }

  // Since we have only 4 digits, temp display depends on sign and number of digits in value being displayed
  if (temperatureVal < 0) {
    clockDisplay.writeDigitRaw(0, 0x40);
  }
  else if (temperatureVal >= 100) {
    clockDisplay.writeDigitNum(0, extractDigit(temperatureVal, 2));
  }
  clockDisplay.writeDigitNum(1, extractDigit(temperatureVal, 1));
  clockDisplay.writeDigitNum(3, extractDigit(temperatureVal, 0));
  clockDisplay.writeDigitRaw(2, 0x10);
    
  clockDisplay.writeDisplay();
}

/**
 * Updates the display to show time
 */
void updateTimeDisplay(DateTime timeToDisplay)
{
  uint8_t colonBitMask = 0x00;
  int decimalTime = getDecimalTime(timeToDisplay, USE_24_HR_CLOCK);

  colon = !colon;

  bool setttingUpOrDown = (buttonSetUp.getState() == LOW) || (buttonSetDown.getState() == LOW);
  
  if (colon || MODE.isInAnySet()) {
    colonBitMask |= 0x02;
  }
  if (MODE.isInAnySet() && !setttingUpOrDown) {
    clockDisplay.blinkRate(1);
  }
  else {
    clockDisplay.blinkRate(0);
  }
  if (!USE_24_HR_CLOCK) {
    if (timeToDisplay.isPM()) {
      colonBitMask |= 0x08; // PM (lower dot)
    } else {
      colonBitMask |= 0x04; // AM (upper dot)
    }
  }

  // Print time
  clockDisplay.print(decimalTime);
  
  // Add zero padding when in 24 hour mode and it's midnight.
  if (USE_24_HR_CLOCK && decimalTime < 100) {
    clockDisplay.writeDigitNum(1, 0); // Pad hour 0.
    if (decimalTime < 10) { // Also pad when the 10's minute is 0 and should be padded.
      clockDisplay.writeDigitNum(3, 0);
    }
  }
  clockDisplay.writeDigitRaw(2, colonBitMask);

  clockDisplay.writeDisplay();
}

/** 
 *  Up/down button adjustments for modifying time or alarms
 *  @return [bool] - Returns true if the display needs updating
 */
int timeSinceLastAdjust;
bool timeWasModified = false;
bool timeChangeMonitor(DateTime &timesetDateTime) {
  if (!MODE.isInAnySet()) {
    return false;
  }
  
  buttonSetUp.loop();
  buttonSetDown.loop();

  // button released - set the new time
  if (buttonSetUp.isReleased() || buttonSetDown.isReleased()) {
    adjustTime.setdelay(ADJUST_TIME_DEFAULT_SPEED);
    curAdjustSpeed = ADJUST_TIME_DEFAULT_SPEED;

    if (timeWasModified) {
      if (MODE.isSetTime()) {
        rtc.adjust(timesetDateTime);
      }
      else if (MODE.isSetSleepAlarm()) {
        setSleepAlarm(timesetDateTime);
      }
      else if (MODE.isSetWakeAlarm()) {
        setWakeAlarm(timesetDateTime);
      }
      timeWasModified = false;
    }
  }

  bool setUp = (buttonSetUp.getState() == LOW);
  bool setDown = (buttonSetDown.getState() == LOW);

  if (adjustTime.update() && (setUp || setDown)) {
    TimeSpan adjustment(setUp ? 60 : -60);
    timesetDateTime = timesetDateTime + adjustment;

    if (curAdjustSpeed > MAX_SPEED && millis() - timeSinceLastAdjust > 1000) {
      if (curAdjustSpeed > 100) {
        curAdjustSpeed -= 100;
      } else if (curAdjustSpeed == 100) {
        curAdjustSpeed = MAX_SPEED;
      }
      adjustTime.setdelay(curAdjustSpeed);

      timeSinceLastAdjust = millis();
      timeWasModified = true;
    }
    
    return true;
  }

  return false;
}

/**
 * Monitors the analog pot (green potentimeter)
 */
bool dimmerPotMonitor()
{
  dimmerRawValue = analogRead(pinPot);
  dimmerValue = map(dimmerRawValue, 0, 1023, 0, 16);
  if (dimmerValue != CUR_BRIGHTNESS) {
    setBrightness(dimmerValue);
    return true;
  }

  return false;
}

/**
 * Monitors the status of DST (yellow) switch, and responds to a press
 */
bool DSTButtonMonitor()
{
  buttonDst.loop();

  if (buttonDst.isPressed()) {
    setDaylightSavings(0);
    return true;
  }
  if (buttonDst.isReleased()) {
    setDaylightSavings(1);
    return true;
  }

  return false;
}

/**
 * Monitors the status of temperature (orange) button, and responds to a press
 */
bool temperatureButtonMonitor()
{
  buttonTempShow.loop();

  if (buttonTempShow.isPressed()) {
    printMessageWithTime(F("Temp (orange) button pressed"));
    if (MODE.isClock()) {
      MODE.setTemperature();
    } else if (MODE.isTemperature()) {
      MODE.reset();
    }

    digitalWrite(pinTempButtonLED, MODE.isTemperature() ? HIGH : LOW);
    return true;
  }

  return false;
}

/**
 * Monitors the status of the status (red) button, and responds to a press
 */
bool statusButtonMonitor()
{
  buttonStatusRotate.loop();

  if (buttonStatusRotate.isLongPress(true) && MODE.isClock()) {
    MODE.setSetSleepAlarm();
    buttonStatusRotate.markLongPressAction();
    return true;
  }

  if (buttonStatusRotate.isReleased(true)) {
    buttonStatusRotate.markLongPressAction();
    if (MODE.isSetSleepAlarm()) {
      MODE.setSetWakeAlarm();
    }
    else if (MODE.isSetWakeAlarm()) {
      MODE.reset();
      
      digitalWrite(pinStatusButtonLED, LOW);
      checkIfMissedAlarms();
    }
    else if (MODE.isClock()) {
      statusRotate();
    }
    return true;
  }

  return false;
}

/** 
 * Monitors the status of the mode (white) button, and responds to a press
 *  @return [bool] - Returns true if the display needs updating
 */
bool modeButtonMonitor()
{  
  buttonMode.loop();

  if (buttonMode.isLongPress(true) && MODE.isClock()) {
    MODE.setSetTime();
    buttonMode.markLongPressAction();
    return true;
  }

  if (buttonMode.isReleased(true)) {
    buttonMode.markLongPressAction();
    if (MODE.isSetTime()) {
      MODE.reset();
      
      digitalWrite(pinModeButtonLED, LOW);
      checkIfMissedAlarms();
    }
    else if (MODE.isTemperature()) {
      USE_CELCIUS = !USE_CELCIUS;
    }
    else if (MODE.isClock()) {
      USE_24_HR_CLOCK = !USE_24_HR_CLOCK;
    }

    return true;
  }

  return false;
}

int getDecimalTime(DateTime timeToConvert, bool use24HrClock)
{
  int hours = use24HrClock ? timeToConvert.hour() : timeToConvert.twelveHour();
  int minutes = timeToConvert.minute();
  int decimalTime = hours * 100 + minutes;

  return decimalTime;
}

void setBrightness(int value) {
  CUR_BRIGHTNESS = value;
  clockDisplay.setBrightness(value);
}

/**
 * Manual override of wake/sleep visual light ("statusLight")
 */
void statusRotate()
{
  printMessageWithTime(F("Override (red) button pressed"));
  
  digitalWrite(pinStatusButtonLED, HIGH);
  millisSinceOverrideStart = millis();

  if (statusLightOverride == OFF && (getAlarmStatus() == OFF || getAlarmStatus() == WAKE)) {
    printMessageWithTime(F("Override to SLEEP"));
    statusLightOverride = SLEEP;
    statusSleep();
  }
  else if ((statusLightOverride == OFF && getAlarmStatus() == SLEEP) || statusLightOverride == SLEEP) {
    printMessageWithTime(F("Override to WAKE"));
    statusLightOverride = WAKE;
    statusWake();
  }
  else {
    turnOffStatusOverride();
  }
  
}

void turnOffStatusOverride()
{
    printMessageWithTime(F("Override OFF"));
    statusLightOverride = OFF; // stop overriding
    digitalWrite(pinStatusButtonLED, LOW);
    checkIfMissedAlarms();
}

void statusSleep()
{
  statusLight = SLEEP; 
  setRgbColor(255, 0, 0);

  printMessageWithTime(F("Setting to status SLEEP"));
}

void statusWake()
{
  statusLight = WAKE;
  setRgbColor(0, 255, 0);

  printMessageWithTime(F("Setting to status WAKE"));
}

void statusOff()
{
  statusLight = OFF;
  setRgbColor(0, 0, 0);

  printMessageWithTime(F("Setting to status OFF"));
}

/**
 * Checks if alarms have been fired and adjusts wake/sleep light if needed.
 * This check is currently run every 0.5 second.
 */
void checkAlarms()
{
  if (statusLightOverride != OFF) {
    return; // alarm status lights have been overridden - do nothing
  }
  
  if (rtc.alarmFired(ALARM_SLEEP)) {
    printMessageWithTime(F("Sleep alarm fired"));
    if (getAlarmStatus() != SLEEP) statusSleep();
    rtc.clearAlarm(ALARM_SLEEP);
    
    Serial.println(F("Clearing Sleep alarm"));
  }

  if (rtc.alarmFired(ALARM_WAKE)) {
    if (getAlarmStatus() != WAKE) statusWake();

    int now_t = getDecimalTime(now, true);
    int wake_t = getDecimalTime(wakeLightOffTime, true);
    if (now_t >= wake_t) {
      rtc.clearAlarm(ALARM_WAKE);
      Serial.println(F("Clearing Wake alarm"));
      statusOff();
    }
  }
}

void printTimePart(DateTime dt)
{
    Serial.print(dt.hour(), DEC);
    Serial.print(':');
    if (dt.minute() < 10) Serial.print("0");
    Serial.print(dt.minute(), DEC);
}

void printMessageWithTime(String s)
{
  Serial.print("[");
  printTimePart(now);
  Serial.println("] " + s);
}

void printStatus() {
    DateTime now = rtc.now();

    Serial.print(F("Current time: "));
    Serial.print(now.year(), DEC);
    Serial.print('/');
    Serial.print(now.month(), DEC);
    Serial.print('/');
    Serial.print(now.day(), DEC);
    Serial.print(F(" @ "));
    printTimePart(now);
    Serial.print(F(" DST = "));
    Serial.println(DST);

    Serial.print(F("Sleep time: "));
    Serial.print(rtc.getAlarm(ALARM_SLEEP).timestamp());
    Serial.print(F(" | Wake time: "));
    Serial.print(rtc.getAlarm(ALARM_WAKE).timestamp());
    Serial.print(F(" | Wake-off time: "));
    Serial.print(wakeLightOffTime.timestamp());
    
    Serial.println();
    Serial.println();
}

void showAlarmTimes()
{
  const int show_alarm_time = 3000;
  
  Serial.println(F("Visual display of alarms"));
  updateTimeDisplay(rtc.getAlarm(ALARM_SLEEP));
  statusSleep();
  delay(show_alarm_time);
  
  updateTimeDisplay(rtc.getAlarm(ALARM_WAKE));
  statusWake();
  delay(show_alarm_time);

  statusOff();
  Serial.println(F("Visual display complete."));
}

void initializeRtc()
{
  bool resetTime = RESET_TIME_TO_COMPUTER;
  if ( !rtc.begin() ) {
    Serial.println(F("Couldn't find RTC"));
    Serial.flush();
    abort();
  }

  if (rtc.lostPower()) {
    Serial.println(F("RTC lost power, let's set the time!"));
    resetTime = true;
  }

  if (resetTime) {
    Serial.println(F("Resetting time to computer time."));
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    
    setSleepAlarm(getDateTimeFromDecimal(DEFAULT_SLEEP_TIME));
    setWakeAlarm(getDateTimeFromDecimal(DEFAULT_WAKE_TIME));
  }
  else {
    // Set just wake time when not restting alarms & time
    setWakeOffTime(rtc.getAlarm(ALARM_WAKE));
  }

  printStatus();
}

void initizalizeDST()
{
  DST = EEPROM.get(0, DST);
  if (DST != 0 && DST != 1) {
    DST = buttonDst.getStateRaw();
    EEPROM.put(0, DST);
    Serial.println(F("DST state stored in EEPROM in unknown state. Resetting to state of button"));
  }

  // if the button changed when powered down, update state
  if (DST != buttonDst.getStateRaw()) { 
    DST = buttonDst.getStateRaw();
    setDaylightSavings(DST);
    Serial.println(F("DST button state does not match value stored in EEPROM--updating"));
  }
}

void setSleepAlarm(DateTime dt)
{
  rtc.setAlarm1(dt, DS3231_A1_Hour);
}

void setWakeAlarm(DateTime dt)
{
  setWakeOffTime(dt);
  
  rtc.setAlarm2(dt, DS3231_A2_Hour);
}

void setWakeOffTime(DateTime dt)
{
  TimeSpan ts(DEFAULT_WAKE_LENGTH);
  wakeLightOffTime = dt + ts;
}

void checkIfMissedAlarms()
{
  switch (getAlarmStatus()) {
    case SLEEP:
      statusSleep();
      Serial.println(F("Assuming missed sleep alarm. Setting Sleep."));
      if (rtc.alarmFired(ALARM_WAKE)) {
        rtc.clearAlarm(ALARM_WAKE);
        Serial.println(F("Clearing previous WAKE alarm too."));
      }
      break;
    case WAKE:
      statusWake();
      Serial.println(F("Assuming missed wake alarm. Setting Wake."));
      if (rtc.alarmFired(ALARM_SLEEP)) {
        rtc.clearAlarm(ALARM_SLEEP);
        Serial.println(F("Clearing previous SLEEP alarm too."));
      }
      break;
     default:
      statusOff();
      Serial.println(F("Assuming missed off time. Setting status light off."));
  }
}

StatusLight getAlarmStatus()
{
  int i_sleep = getDecimalTime(rtc.getAlarm(ALARM_SLEEP), true);
  int i_wake = getDecimalTime(rtc.getAlarm(ALARM_WAKE), true);
  int i_wake_off = getDecimalTime(wakeLightOffTime, true);
  int i_now = getDecimalTime(now, true);
  
  if (i_now < i_wake || i_now >= i_sleep) {
    return SLEEP;
  }
  else if (i_now >= i_wake && i_now < i_wake_off) {
    return WAKE;
  }
  else {
    return OFF;
  }
}

DateTime getDateTimeFromDecimal(float decimalTime)
{
  int hours = (int) decimalTime;
  int mins = 60 * (decimalTime - hours);
  return DateTime(2020, 1, 1, hours, mins, 0);
}
