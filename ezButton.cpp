/*
 * Copyright (c) 2019, ArduinoGetStarted.com. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of the ArduinoGetStarted.com nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ARDUINOGETSTARTED.COM "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL ARDUINOGETSTARTED.COM BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "ezButton.h"

ezButton::ezButton(int pin) {
  init(pin, DEFAULT_LONG_PRESS_TIME);
}
ezButton::ezButton(int pin, int longPressTime) {
	init(pin, longPressTime);
}

void ezButton::init(int pin, int longPressTime) {
  btnPin = pin;
  debounceTime = 0;
  count = 0;
  countMode = COUNT_FALLING;
  btnLongPressTime = longPressTime;

  pinMode(btnPin, INPUT_PULLUP);

  previousSteadyState = digitalRead(btnPin);
  lastSteadyState = digitalRead(btnPin);
  lastFlickerableState = digitalRead(btnPin);

  lastDebounceTime = 0;

  // Long press state
  isLongPressDetected = false;
  isPressing = false;
  lastPressedTime = 0;
  longPressActionReady = false;
}

void ezButton::setDebounceTime(unsigned long time) {
	debounceTime = time;
}

int ezButton::getState(void) {
	return lastSteadyState;
}

int ezButton::getStateRaw(void) {
	return digitalRead(btnPin);
}

bool ezButton::isPressed(void) {
	if(previousSteadyState == HIGH && lastSteadyState == LOW)
		return true;
	else
		return false;
}

bool ezButton::isLongPress(bool checkLongPressReady) {
  return isLongPressDetected && (!checkLongPressReady || longPressActionReady); 
}

void ezButton::markLongPressAction(void) {
  longPressActionReady = false;
}

bool ezButton::isLongPressActionReady(void) {
  return longPressActionReady;
}

bool ezButton::isReleased(bool checkLongPressReady) {
	if(previousSteadyState == LOW && lastSteadyState == HIGH)
		return (!checkLongPressReady || longPressActionReady);
	else
		return false;
}

void ezButton::setCountMode(int mode) {
	countMode = mode;
}

unsigned long ezButton::getCount(void) {
	return count;
}

void ezButton::resetCount(void) {
	count = 0;
}

void ezButton::loop(void) {
	// read the state of the switch/button:
	currentState = digitalRead(btnPin);

	// check to see if you just pressed the button
	// (i.e. the input went from LOW to HIGH), and you've waited long enough
	// since the last press to ignore any noise:

	// If the switch/button changed, due to noise or pressing:
	if (currentState != lastFlickerableState) {
		// reset the debouncing timer
		lastDebounceTime = millis();
		// save the the last flickerable state
		lastFlickerableState = currentState;
	}

	if ((millis() - lastDebounceTime) >= debounceTime) {
		// whatever the reading is at, it's been there for longer than the debounce
		// delay, so take it as the actual current state:

		// save the the steady state
		previousSteadyState = lastSteadyState;
		lastSteadyState = currentState;
	}

	if(previousSteadyState != lastSteadyState){
		if(countMode == COUNT_BOTH)
			count++;
		else if(countMode == COUNT_FALLING){
			if(previousSteadyState == HIGH && lastSteadyState == LOW)
				count++;
		}
		else if(countMode == COUNT_RISING){
			if(previousSteadyState == LOW && lastSteadyState == HIGH)
				count++;
		}
	}

  if (isPressed()) {
    lastPressedTime = millis();
    isPressing = true;
    isLongPressDetected = false;
    longPressActionReady = true;
  }
  if (isReleased()) {
    isPressing = false;
  }

  if (isPressing && !isLongPressDetected && (millis() - lastPressedTime) > btnLongPressTime) {
    isLongPressDetected = true;
  }
}
