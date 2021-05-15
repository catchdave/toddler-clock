
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

// Pins
const int pinPot             = A0;
const int pinModeButton      = 3;  // white button
const int pinRedStatus       = 6;
const int pinGreenStatus     = 7;
const int pinStatusButton    = 8;  // red button
const int pinTempButton      = 2; // orange button
const int pinStatusButtonLED = A1; // red led button
const int pinTempButtonLED   = A2; // orange led button
const int pinModeButtonLED   = A3; // white led button
const int pinRgbRed          = 9;
const int pinRgbGreen        = 10;
const int pinRgbBlue         = 11;
const int pinSetUp           = 4;
const int pinSetDown         = 5;
const int pinDstButton       = 13; // yellow switch

// Constants
const int RESET_TIME_TO_COMPUTER    = false; // change to true to re-sync time to computer
const int LONG_PRESS_TIME           = 2500; // 2.5 secs
const int ADJUST_TIME_DEFAULT_SPEED = 300;
const int MAX_SPEED                 = 25; // max speed for time change using up/down buttons
const int DEFAULT_WAKE_LENGTH       = 60 * 60; // in seconds
const float DEFAULT_SLEEP_TIME      = 19; // 24-hr decimal time (e.g. 6:45pm is 18.75)
const float DEFAULT_WAKE_TIME       = 6.5; // 24-hr decimal time (e.g. 6:30am is 6.5)
const int ALARM_SLEEP               = 1;
const int ALARM_WAKE                = 2;

// Variables
int dimmerValue = 0;       // variable to store the value coming from the sensor
int dimmerRawValue = 0;
int CUR_BRIGHTNESS = 15;
bool colon = true;
StatusLight statusLight = OFF;
StatusLightOverride statusLightOverride = NO_OVERRIDE;
unsigned long modeButtonPressedTime, modeButtonReleasedTime = 0;
bool blinkLEDState = false;
int curAdjustSpeed = ADJUST_TIME_DEFAULT_SPEED;

// Object variables
DateTime timesetDateTime; // current DT object that is being modified
DateTime now;
DateTime wakeLightOffTime;
noDelay writeTimeToDisplay(500);
noDelay debugConsole(1000, printStatus);
noDelay blinkLEDLights(300); // generic timer for blinking all LEDs
noDelay adjustTime(ADJUST_TIME_DEFAULT_SPEED);
ezButton buttonStatusRotate(pinStatusButton);
ezButton buttonMode(pinModeButton);
ezButton buttonTempShow(pinTempButton);
ezButton buttonSetUp(pinSetUp);
ezButton buttonSetDown(pinSetDown);
ezButton buttonDst(pinDstButton);

// Settings
bool USE_24_HR_CLOCK       = false;
bool USE_CELCIUS           = false;
int  DST;
ToddlerClockMode MODE;

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
  Serial.println("-~- Toddler Clock Initializing -~-");

  clockDisplay.begin(0x70);
  initializeRtc();
  now = rtc.now();

  pinMode(pinRedStatus, OUTPUT); 
  pinMode(pinGreenStatus, OUTPUT); 
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
  Serial.println("-~- Toddler Clock Initialization complete -~-");
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

void loop()
{
  bool displayNeedsUpdating = false;
  int prevMode = MODE.getMode();
  
  // Update time display every 500ms
  if (writeTimeToDisplay.update()) {
    displayNeedsUpdating = true;
    checkAlarms();
  }

  // Read and update dimmer value
  dimmerRawValue = analogRead(pinPot);
  dimmerValue = map(dimmerRawValue, 0, 1023, 0, 16);
  if (dimmerValue != CUR_BRIGHTNESS) {
    setBrightness(dimmerValue);
    clockDisplay.writeDisplay();
  }

  buttonDst.loop();
  if (buttonDst.isPressed()) {
    setDaylightSavings(0);
  }
  if (buttonDst.isReleased()) {
    setDaylightSavings(1);
  }

  // Tempreature display button [orange LED button]
  buttonTempShow.loop();
  if (buttonTempShow.isPressed()) {
    if (MODE.isClock()) {
      MODE.setTemperature();
    } else if (MODE.isTemperature()) {
      MODE.reset();
    }

    digitalWrite(pinTempButtonLED, MODE.isTemperature() ? HIGH : LOW);
    displayNeedsUpdating = true;
  }

  // Status button (status rotate and set alarm) [red LED button]
  if (statusButtonMonitor()) {
    displayNeedsUpdating = true;
  }

  // Mode button (mode and set time) [white LED button]
  if (modeButtonMonitor()) {
    displayNeedsUpdating = true;
  }

  if (timeChangeMonitor(timesetDateTime)) {
    displayNeedsUpdating = true;
  }

  if (displayNeedsUpdating) {
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
    
    updateClockDisplay(MODE.isInAnySet() ? timesetDateTime : now);
  }

  // blink any LEDs that need blnking
  if (blinkLEDLights.update()) {
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
  
  //debugConsole.update();
}
// -- end main loop 


int extractDigit(int V, int P)
{
  unsigned int pow [] = { 1, 10, 100, 1000, 10000 } ;
  return abs(V) / pow[P] % 10;
}

void updateClockDisplay(DateTime timeToDisplay)
{
  // Display Temp in fahrenheit or celicuis depending on mode
  if (MODE.isTemperature()) {
    float celcius = rtc.getTemperature();
    float temperatureVal;
    clockDisplay.clear();
    
    if (USE_CELCIUS) {
      temperatureVal = celcius;
      clockDisplay.writeDigitNum(4, 0xc);
    }
    else {
      temperatureVal = (celcius * 9/5) + 32;
      clockDisplay.writeDigitNum(4, 0xf);
    }

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
    return;
  }

  uint8_t colonBitMask = 0x00;
  int decimalTime = getDecimalTime(timeToDisplay, USE_24_HR_CLOCK);
  colon = !colon;
  
  if (colon || MODE.isInAnySet()) {
    colonBitMask |= 0x02;
  }
  if (MODE.isInAnySet()) {
    clockDisplay.blinkRate(2);
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
      //setRgbColor(0, 0, 0);
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
//      setRgbColor(0, 0, 0);
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

void statusRotate() {
  digitalWrite(pinStatusButtonLED, HIGH);
  if (statusLightOverride == NO_OVERRIDE) {
    statusLightOverride = SLEEP_OVERRIDE;
    statusSleep();
  }
  else if (statusLightOverride == SLEEP_OVERRIDE) {
    statusLightOverride = WAKE_OVERRIDE;
    statusWake();
  }
  else if (statusLightOverride == WAKE_OVERRIDE) {
    statusLightOverride = OFF_OVERRIDE;
    statusOff();
  }
  else {
    statusLightOverride = NO_OVERRIDE; // stop overriding
    digitalWrite(pinStatusButtonLED, LOW);
    checkIfMissedAlarms();
  }
  
}

void statusSleep() {
  statusLight = SLEEP; 
  digitalWrite(pinRedStatus, HIGH);
  digitalWrite(pinGreenStatus, LOW);
  setRgbColor(255, 0, 0);
}

void statusWake() {
  statusLight = WAKE;
  digitalWrite(pinGreenStatus, HIGH);
  digitalWrite(pinRedStatus, LOW);
  setRgbColor(0, 255, 0);
}

void statusOff() {
  statusLight = OFF;
  digitalWrite(pinGreenStatus, LOW);
  digitalWrite(pinRedStatus, LOW);
  setRgbColor(0, 0, 0);
}

void checkAlarms()
{
  if (statusLightOverride != NO_OVERRIDE) {
    return; // alarm status lights have been overridden
  }
  
  if (rtc.alarmFired(ALARM_SLEEP)) {
    statusSleep();
    rtc.clearAlarm(ALARM_SLEEP);
  }

  if (rtc.alarmFired(ALARM_WAKE)) {
    statusWake();

    if (now >= wakeLightOffTime) {
      rtc.clearAlarm(ALARM_WAKE);
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

void printStatus() {
    DateTime now = rtc.now();

    Serial.print("Current time: ");
    Serial.print(now.year(), DEC);
    Serial.print('/');
    Serial.print(now.month(), DEC);
    Serial.print('/');
    Serial.print(now.day(), DEC);
    Serial.print(" @ ");
    printTimePart(now);
    Serial.print(" DST = ");
    Serial.println(DST);

    Serial.print("Sleep time: ");
    printTimePart(rtc.getAlarm(ALARM_SLEEP));
    Serial.print(" | Wake time: ");
    printTimePart(rtc.getAlarm(ALARM_WAKE));
    Serial.print(" | Wake-off time: ");
    printTimePart(wakeLightOffTime);
    
    Serial.println();
    Serial.println();
}

void showAlarmTimes()
{
  const int show_alarm_time = 3000;
  
  Serial.println("Visual display of alarms");
  updateClockDisplay(rtc.getAlarm(ALARM_SLEEP));
  statusSleep();
  delay(show_alarm_time);
  
  updateClockDisplay(rtc.getAlarm(ALARM_WAKE));
  statusWake();
  delay(show_alarm_time);

  statusOff();
  Serial.println("Visual display complete.");
}

void initializeRtc()
{
  bool resetTime = RESET_TIME_TO_COMPUTER;
  if ( !rtc.begin() ) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    abort();
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, let's set the time!");
    resetTime = true;
  }

  if (resetTime) {
    Serial.println("Resetting time to computer time.");
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
    Serial.println("DST state stored in EEPROM in unknown state. Resetting to state of button");
  }

  // if the button changed when powered down, update state
  if (DST != buttonDst.getStateRaw()) { 
    DST = buttonDst.getStateRaw();
    setDaylightSavings(DST);
    Serial.println("DST button state does not match value stored in EEPROM--updating");
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
      Serial.println("Assuming missed sleep alarm. Setting Sleep.");
      break;
    case WAKE:
      statusWake();
      Serial.println("Assuming missed wake alarm. Setting Wake.");
      break;
     default:
      statusOff();
      Serial.println("Assuming missed off time. Setting status light off.");
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