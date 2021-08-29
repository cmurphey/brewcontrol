#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <OneWire.h> 
#include <DallasTemperature.h>
#include <SPI.h>
#include "Adafruit_MAX31855.h"
#include <avr/wdt.h>
#include <EEPROM.h>

// Amount of time to wait for flame in ms
const int flameWaitAmount = 10000;
const int minTempChangeInFForFlame = 5;
const int defaultTarTemp = 160;
const int defaultMode = 0;
const int largeInt = 9999;

// Serial pins 0/1

// LCD pins
// SDA - pin 20 on mega2560
// SCL - pin 21 on mega2560
// set the LCD address to 0x27 for a 20 chars and 4 line display
LiquidCrystal_I2C lcd(0x27, 20, 4);

// On/Off Toggle
const int switchPin1 = 2;
// Target Temp Inc
const int switchPin2 = 3;
// Target Temp Dec
const int switchPin3 = 4;
// Pot selector
const int switchPin4 = 5;

// SPI pins
const int maxCS1 = 52;
const int maxDO1 = 51;
const int maxCLK1 = 53;

const int maxCS2 = 49;
const int maxDO2 = 48;
const int maxCLK2 = 50;

Adafruit_MAX31855 thermocouple1(maxCLK1, maxCS1, maxDO1);
Adafruit_MAX31855 thermocouple2(maxCLK2, maxCS2, maxDO2);

const int valvePin1 = 38;
const int valvePin2 = 39;
const int ignitorPin = 40;

// TODO: Change these to other pins
const int tempSensorPin1 = A0;
const int tempSensorPin2 = A1;
const int tempSensorPin3 = A2;

int switchPin1Val = 0;
int switchPin2Val = 0;
int switchPin3Val = 0;
int switchPin4Val = 0;

// Per Pot
struct Pot {
  int curTemp;
  int tarTemp;
  int thermoTemp;
  // 0 = off, 1 = on, 2 = lighting, 3 = error
  int state;
  // 0 = off, 1 = on
  int mode;
  // 0 = open, 1 = closed
  int valve;
  // 0 = off, 1 = on
  int ignition;
  long ignitionStartTime;
  long ignitionStartTemp;
  long oldThermoTemp;
  DallasTemperature tempSensor;
  int valvePin;
  Adafruit_MAX31855 thermocouple;
};     

// Non-initialized value to will hold temp values
union tempSetting{
  uint8_t    byte[6];
  struct {
    uint16_t tarTemp1;
    uint16_t tarTemp2;
    uint16_t tarTemp3;
    uint16_t mode1;
    uint16_t mode2;
    uint16_t mode3;
    uint16_t potSelector;
    uint16_t chksum;
  } val ;
} config  __attribute__ ((section (".noinit")));

OneWire oneWire1(tempSensorPin1);
OneWire oneWire2(tempSensorPin2);
OneWire oneWire3(tempSensorPin3);
 
DallasTemperature tempSensor1(&oneWire1);
DallasTemperature tempSensor2(&oneWire2);
DallasTemperature tempSensor3(&oneWire3);

// Brew Kettle
Pot pot0 = {0, defaultTarTemp, -1, 0, defaultMode, 0, 0, 0, largeInt, 0, tempSensor1, valvePin1, thermocouple1};
// Hot Liquor Tank
Pot pot1 = {0, defaultTarTemp, -1, 0, defaultMode, 0, 0, 0, largeInt, 0, tempSensor2, valvePin2, thermocouple2};
// Mash Tun
Pot pot2 = {0, defaultTarTemp, -1, 0, defaultMode, 0, 0, 0, largeInt, 0, tempSensor3, -1, -1};

Pot pots[] = {pot0, pot1, pot2};

// 1 to 3
int potSelector = 0;
// Refresh LCD
int refresh = 1;
long refreshTimeMs = 0;
long thermoRecordTimeMs = 0;

void setup() {
  uint16_t sum;
  Serial.begin(9600);
  
  pinMode(switchPin1, INPUT);
  pinMode(switchPin2, INPUT);
  pinMode(switchPin3, INPUT);
  pinMode(switchPin4, INPUT);
  
  pinMode(tempSensorPin1, INPUT);
  pinMode(tempSensorPin2, INPUT);
  pinMode(tempSensorPin3, INPUT);

  pinMode(valvePin1, OUTPUT);
  pinMode(valvePin2, OUTPUT);
  pinMode(ignitorPin, OUTPUT);

  digitalWrite(valvePin1, HIGH);
  digitalWrite(valvePin2, HIGH);
  digitalWrite(ignitorPin, HIGH);
  
  // Wait for MAX chip to stabilize
  delay(500);

  tempSensor1.begin();
  tempSensor2.begin();
  tempSensor3.begin();

  while (!Serial) delay(1); // wait for Serial on Leonardo/Zero, etc
  
  Serial.print("Initializing thermocouples...");
  if (!thermocouple1.begin()) {
    Serial.println("Error when calling begin on thermocouple 1.");
  }
  if (!thermocouple2.begin()) {
    Serial.println("Error when calling begin on thermocouple 2.");
  }
  Serial.println("done.");

  initializeLCD();

  lcd.setCursor( 0, 0 );
  lcd.print("    Starting up...    ");
  
  // prints title with ending line break
  sum = getchksum();
  printMemoryValues();

  if (sum != config.val.chksum) {
    config.val.chksum = sum;
    Serial.println("chksum is incorrect, setting defaults");
    setMemoryValues(true);
  } else {
    Serial.println("chksum is correct, extracting old tarTemp values");
  }

  // Set beginning pot target temps
  (&pots[0])->tarTemp = config.val.tarTemp1;
  (&pots[1])->tarTemp = config.val.tarTemp2;
  (&pots[2])->tarTemp = config.val.tarTemp3;
  (&pots[0])->mode = config.val.mode1;
  (&pots[1])->mode = config.val.mode2;
  (&pots[2])->mode = config.val.mode3;
  potSelector = config.val.potSelector;

  // Enable watch dog to restart if the program hangs
  wdt_enable(WDTO_1S);
}

void initializeLCD() {
  lcd.init();  //initialize the lcd
  lcd.backlight();  //open the backlight 
}

uint16_t getchksum() {
  int sum = 0;
  for (int position = 0; position < (sizeof(config) - sizeof(config.val.chksum)); position++) {
    sum = sum + config.byte[position];
  }
  return sum;
}

void setMemoryValues(boolean setDefault) {
  if (setDefault) {
    config.val.tarTemp1 = defaultTarTemp;
    config.val.tarTemp2 = defaultTarTemp;
    config.val.tarTemp3 = defaultTarTemp;
    config.val.mode1 = defaultMode;
    config.val.mode2 = defaultMode;
    config.val.mode3 = defaultMode;
    config.val.potSelector = 0;
  } else {
    config.val.tarTemp1 = (&pots[0])->tarTemp;
    config.val.tarTemp2 = (&pots[1])->tarTemp;
    config.val.tarTemp3 = (&pots[2])->tarTemp;
    config.val.mode1 = (&pots[0])->mode;
    config.val.mode2 = (&pots[1])->mode;
    config.val.mode3 = (&pots[2])->mode;
    config.val.potSelector = potSelector;
  }
  config.val.chksum = getchksum();
}

void printMemoryValues() {
  Serial.print(" tarTemp1 = 0x"); 
  Serial.print(config.val.tarTemp1, HEX);
  Serial.print(", tarTemp2 = 0x"); 
  Serial.print(config.val.tarTemp2, HEX);
  Serial.print(", tarTemp3 = 0x"); 
  Serial.print(config.val.tarTemp3, HEX);
  Serial.print(", sum = 0x"); 
  Serial.print(getchksum(), HEX);
  Serial.print(", chksum = 0x"); 
  Serial.println(config.val.chksum, HEX);
}

void loop() {

  wdt_reset();
  
  // Iterate through pots
  for (int potNum = 0; potNum < 3; potNum++) {

    Serial.print("Pot: ");
    Serial.print(potNum);
    
    Pot* pot = &pots[potNum];
    
    // Get current temp
    pot->tempSensor.requestTemperatures();
    int newTemp = pot->tempSensor.getTempFByIndex(0);
    
    Serial.print(" Temp: ");
    Serial.print(newTemp);
    
    if (newTemp != pot->curTemp) {
      pot->curTemp = newTemp;
      refresh = 1;
    }

    // Skip pot if it doesn't have a burner
    if (pot->valvePin == -1) {
      Serial.println();
      continue;
    }

    // Read thermocouple
    int termocoupleTemp = pot->thermocouple.readFahrenheit();

    Serial.print(" ThermoC temp: ");
    Serial.print(termocoupleTemp);

    if (termocoupleTemp != pot->thermoTemp && termocoupleTemp > 50) {
      pot->thermoTemp = termocoupleTemp;
      refresh = 1;
    }

    // Keep track of the older value of thermocouple to compare
    if (millis() - thermoRecordTimeMs > 2000 || pot->oldThermoTemp == 0) {
      thermoRecordTimeMs = millis();
      pot->oldThermoTemp = pot->thermoTemp;
    }

    // Pot needs heat and heating mode is on
    if (pot->curTemp < pot->tarTemp && pot->mode == 1) {

      // Determine change in thermocouple temp since starting lighting
      int themoTempChange = pot->thermoTemp - pot->ignitionStartTemp;
      Serial.print(" themoTempChange: ");
      Serial.print(themoTempChange);
      Serial.print(" thermoTemp: ");
      Serial.print(pot->thermoTemp);
      Serial.print(" oldThermoTemp: ");
      Serial.print(pot->oldThermoTemp);

      int maxDropTemp = 5;
      if (pot->thermoTemp > 100) {
        maxDropTemp = pot->thermoTemp / 5;
      }

      bool tempIncreasedWhileLighting = pot->state == 2 && themoTempChange >= minTempChangeInFForFlame;
      bool tempDidNotDropWhileOn = pot->state == 1 && pot->thermoTemp >= (pot->oldThermoTemp - maxDropTemp);
      if (!tempDidNotDropWhileOn) {
        Serial.print(" Detected drop in thermo temp "); 
      }

      // Ignore thremo drop for now
      if (pot->state == 1) {
        tempDidNotDropWhileOn = 1;
      }

      // Flame is lit - lighting and saw the change in temp or lit and did not see a decrease
      if (tempIncreasedWhileLighting || tempDidNotDropWhileOn) {

        // If lighting state, then turn off ignition
        if (pot->state == 2) {
          pot->state = 1;
          pot->valve = 1;
          pot->ignition = 0;
          pot->ignitionStartTemp = largeInt;
          refresh = 1;
          Serial.print("Pot: ");
          Serial.print(potNum);
          Serial.println(" Lit!");
        } 

        // If state is not on, then error out
        else if  (pot->state != 1) {
          pot->state = 3;
          pot->valve = 0;
          pot->ignition = 0;
          pot->ignitionStartTemp = largeInt;
          refresh = 1;
          Serial.print("Pot: ");
          Serial.print(potNum);
          Serial.println(" Error, lit but wrong state!"); 
        }
      } 
      
      // Start the lighting process
      else {

        // Require an extra degree to light
        if (pot->state == 0 && (pot->curTemp + 1 < pot->tarTemp)) {
          pot->state = 2;
          pot->valve = 1;
          pot->ignition = 1;
          pot->ignitionStartTime = millis();
          pot->ignitionStartTemp = pot->thermoTemp;
          refresh = 1;
          Serial.print("Pot: ");
          Serial.print(potNum);
          Serial.println(" Igniting!");
        } 

        // Was lit, but no longer
        else if (pot->state == 1) {
          pot->state = 3;
          pot->valve = 0;
          pot->ignition = 0;
          pot->ignitionStartTemp = largeInt;
          refresh = 1;
          Serial.print("Pot: ");
          Serial.print(potNum);
          Serial.println(" Error, no longer lit!");
        }
      
        // Check if we're still lighting and error if too long a wait
        else if (pot->state == 2) {
          if (millis() - pot->ignitionStartTime > flameWaitAmount) {
            pot->state = 3;
            pot->valve = 0;
            pot->ignition = 0;
            pot->ignitionStartTemp = largeInt;
            refresh = 1;
            Serial.print("Pot: ");
            Serial.print(potNum);
            Serial.println(" Error, could not light!");
          }
        }
      }
    } 
    
    // Pot is hot enough or heating mode is off
    else if ((pot->curTemp >= pot->tarTemp || pot->mode == 0) && pot->state != 0) {
      pot->state = 0;
      pot->valve = 0;
      pot->ignition = 0;
      pot->ignitionStartTemp = largeInt;
      refresh = 1;
      Serial.print("Pot: ");
      Serial.print(potNum);
      Serial.println(" Off!");
    }
    Serial.println();
  }

  // Update output pins - ignition is the result of all pots
  int ignitionOn = 0;
  for (int potNum = 0; potNum < 3; potNum++) {
    Pot* pot = &pots[potNum];
    if (pot->ignition == 1) {
      ignitionOn = 1;
    }
    if (pot->valve == 1) {
      Serial.print("Pot: ");
      Serial.print(potNum);
      Serial.println(" Valve high");
      digitalWrite(pot->valvePin, LOW);
    } else {
      digitalWrite(pot->valvePin, HIGH);
    }
  }
  if (ignitionOn == 1) {
    digitalWrite(ignitorPin, LOW);
  } else {
    digitalWrite(ignitorPin, HIGH);
  }

  // Check buttons
  int switch1Val = digitalRead(switchPin1);
  int switch2Val = digitalRead(switchPin2);
  int switch3Val = digitalRead(switchPin3);
  int switch4Val = digitalRead(switchPin4);

  // Restart display
  if (switch4Val == HIGH && switch1Val == HIGH) {
    // This will trigger watch dog to fire
    delay(1000);
  }

  // Pot selector
  if (switch4Val == HIGH && switchPin4Val == 0) {
    switchPin4Val = 1;
    int newPot = potSelector + 1;
    if (newPot > 2) {
      newPot = 0;
    }
    potSelector = newPot;
    refresh = 1;
  } else if (switch4Val == LOW) {
    switchPin4Val = 0;
  }

  Pot* selectedPot = &pots[potSelector];  

  // Mode selector
  if (switch1Val == HIGH && switchPin1Val == 0) {
    switchPin1Val = 1;
    if (selectedPot->mode == 0) {
      selectedPot->mode = 1;
    } else {
      selectedPot->mode = 0;
    }
    refresh = 1;
  } else if(switch1Val == LOW) {
    switchPin1Val = 0;  
  }

  // Increase target temp
  if (switch2Val == HIGH && switchPin2Val == 0) {
    switchPin2Val = 1;
    selectedPot->tarTemp++;
    refresh = 1;
  } else if (switch2Val == LOW) {
    switchPin2Val = 0;
  }

  // Decrease target temp
  if (switch3Val == HIGH && switchPin3Val == 0) {
    switchPin3Val = 1;
    selectedPot->tarTemp--;
    refresh = 1;
  } else if (switch3Val == LOW) {
    switchPin3Val = 0;
  }

  // Refresh once a second anyway
//  if (refresh == 0 && millis() - refreshTimeMs > 1000) {
//    refreshTimeMs = millis();
//    refresh = 1;
//  }

  // Display
  if (refresh == 1) {

    // Save current target temps
    setMemoryValues(false);
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Pot ");
    lcd.print(potSelector);
    lcd.print(" Mode ");
    lcd.print(selectedPot->mode);
    lcd.print(" State ");
    lcd.print(selectedPot->state);

    lcd.setCursor(0, 1);
    lcd.print("Current Temp: ");
    lcd.print(selectedPot->curTemp);
    
    lcd.setCursor(0, 2);
    lcd.print("Target  Temp: ");
    lcd.print(selectedPot->tarTemp);
    
    lcd.setCursor(0, 3);
    lcd.print("ThermoC Temp: ");
    lcd.print(selectedPot->thermoTemp);
  }
  refresh = 0;
}
