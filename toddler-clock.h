enum StatusLight { 
  SLEEP,
  WAKE,
  OFF
};

struct ToddlerClockMode
{
  private:
    enum Modes {
      M_CLOCK           = 0x00,
      M_TEMPERATURE     = 0x04,

      M_SET_ANY         = 0x01,
      M_SET_TIME        = 0x08,
      
      M_SET_ALARM       = 0x02,
      M_SET_ALARM_SLEEP = 0x16,
      M_SET_ALARM_WAKE  = 0x32
    };
    int mode = M_CLOCK;
    int prevMode = M_CLOCK;

  public:
    bool isClock(void);
    bool isTemperature(void);
    bool isSetTime(void);
    bool isSetAnyAlarm(void);
    bool isSetWakeAlarm(void);
    bool isSetSleepAlarm(void);
    bool isInAnySet(void);
    bool modeJustChanged(void);

    int getMode(void);

    void reset(void);
    void setTemperature(void);
    void setSetTime(void);
    void setSetWakeAlarm(void);
    void setSetSleepAlarm(void);
};

int ToddlerClockMode::getMode(void) {
  return mode;
}

bool ToddlerClockMode::modeJustChanged(void)
{
  bool changed = (prevMode != mode);
  prevMode = mode;
  return changed;
}

bool ToddlerClockMode::isClock(void) {
  return mode == M_CLOCK;
}

bool ToddlerClockMode::isTemperature(void) {
  return mode == M_TEMPERATURE;
}

bool ToddlerClockMode::isSetTime(void) {
  return (mode & M_SET_TIME) == M_SET_TIME;
}

bool ToddlerClockMode::isSetAnyAlarm(void) {
  return (mode & M_SET_ALARM) == M_SET_ALARM;
}

bool ToddlerClockMode::isInAnySet(void) {
  return (mode & M_SET_ANY) == M_SET_ANY;
}

bool ToddlerClockMode::isSetWakeAlarm(void) {
  return (mode & M_SET_ALARM_WAKE) == M_SET_ALARM_WAKE;
}

bool ToddlerClockMode::isSetSleepAlarm(void) {
  return (mode & M_SET_ALARM_SLEEP) == M_SET_ALARM_SLEEP;
}

void ToddlerClockMode::reset(void) {
  prevMode = mode;
  mode = M_CLOCK;
}

void ToddlerClockMode::setTemperature(void) {
  prevMode = mode;
  mode = M_TEMPERATURE;
}

void ToddlerClockMode::setSetTime(void) {
  prevMode = mode;
  mode = M_SET_TIME | M_SET_ANY;
}

void ToddlerClockMode::setSetWakeAlarm(void) {
  prevMode = mode;
  mode = M_SET_ALARM_WAKE | M_SET_ALARM | M_SET_ANY;
}

void ToddlerClockMode::setSetSleepAlarm(void) {
  prevMode = mode;
  mode = M_SET_ALARM_SLEEP | M_SET_ALARM | M_SET_ANY;
}
