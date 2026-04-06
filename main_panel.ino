#include <Wire.h>
#include <LiquidCrystal_PCF8574.h>
#include <Keypad.h>

#define Password_Length 9
#define PASSWORD_TIMEOUT_MS 10000

#define COIN_LOW_MIN_MS 20
#define COIN_LOW_MAX_MS 120
#define COIN_LOCKOUT_MS 1500
#define COIN_STARTUP_MS 1000

LiquidCrystal_PCF8574 lcd(0x27);

// Keypad
const byte ROWS = 4;
const byte COLS = 4;

char hexaKeys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

byte rowPins[ROWS] = {9, 8, 7, 6};
byte colPins[COLS] = {5, 4, 3, 10};
Keypad keypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

// Pins
const int COIN_PIN = 2;
const int SOLENOID_PIN = 11;
const int LED_PIN = 13;

// Password
char Data[Password_Length];
char Master[Password_Length] = "123456AB";
byte data_count = 0;

bool passwordMode = false;
unsigned long lastPasswordActivity = 0;

enum PasswordAction { PASS_NONE, PASS_DOOR, PASS_FREEVEND };
PasswordAction pendingPasswordAction = PASS_NONE;

// Vending state
uint8_t credits = 0;
bool freeVendArmed = false;
char selectedRow = '\0';
bool machineBusy = false;

// Coin state
bool coinLowSeen = false;
unsigned long coinLowStartMs = 0;
unsigned long coinLockoutUntil = 0;
unsigned long startupIgnoreUntil = 0;

void setup() {
  Serial.begin(9600); // D1 TX -> Motor D0 RX

  pinMode(COIN_PIN, INPUT_PULLUP);
  pinMode(SOLENOID_PIN, OUTPUT);
  digitalWrite(SOLENOID_PIN, LOW);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  keypad.addEventListener(keypadEvent);

  Wire.begin();
  lcd.begin(16, 2);
  lcd.setBacklight(255);
  lcd.clear();

  clearData();
  showMainScreen();

  startupIgnoreUntil = millis() + COIN_STARTUP_MS;
}

void loop() {
  detectCoinPulse();
  checkPasswordTimeout();

  char key = keypad.getKey();
  if (!key) return;

  if (passwordMode) processPasswordKey(key);
  else processVendingKey(key);
}

// ---------- Display ----------
void showMainScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Charlton Snacks");
  lcd.setCursor(0, 1);

  if (freeVendArmed) lcd.print("FREE VEND READY");
  else if (credits > 0) lcd.print("1 Credit: Select");
  else lcd.print("Insert coin...");
}

void showTempMessage(const char* msg, unsigned long ms = 800) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(msg);
  delay(ms);
}

void showSelectPrompt() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Select: A1-A4");
  lcd.setCursor(0, 1);
  lcd.print("B1-B2 C1-C2");
}

void showRowSelected(char row) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Row ");
  lcd.print(row);
  lcd.print(" selected");
  lcd.setCursor(0, 1);
  lcd.print("Pick number...");
}

// ---------- Coin ----------
void detectCoinPulse() {
  unsigned long now = millis();
  int s = digitalRead(COIN_PIN);

  bool acceptWindow = (!machineBusy && !passwordMode && credits == 0 && !freeVendArmed && selectedRow == '\0');

  if (!acceptWindow) {
    coinLowSeen = false;
    return;
  }

  if (now < startupIgnoreUntil) return;
  if (now < coinLockoutUntil) return;

  if (!coinLowSeen && s == LOW) {
    coinLowSeen = true;
    coinLowStartMs = now;
    return;
  }

  if (coinLowSeen && s == HIGH) {
    unsigned long lowDur = now - coinLowStartMs;
    coinLowSeen = false;

    if (lowDur >= COIN_LOW_MIN_MS && lowDur <= COIN_LOW_MAX_MS) {
      credits = 1;
      coinLockoutUntil = now + COIN_LOCKOUT_MS;

      digitalWrite(LED_PIN, HIGH);
      showTempMessage("1 Credit", 450);
      digitalWrite(LED_PIN, LOW);

      showSelectPrompt();
    }
  }
}

// ---------- Password ----------
void clearData() {
  for (byte i = 0; i < Password_Length; i++) Data[i] = 0;
  data_count = 0;
}

void startPasswordMode(PasswordAction action) {
  pendingPasswordAction = action;
  passwordMode = true;
  lastPasswordActivity = millis();
  clearData();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Enter Password:");
  lcd.setCursor(0, 1);
}

void exitPasswordMode() {
  passwordMode = false;
  pendingPasswordAction = PASS_NONE;
  clearData();
  selectedRow = '\0';
  showMainScreen();
}

void redrawPasswordLine() {
  lcd.setCursor(0, 1);
  for (byte i = 0; i < Password_Length - 1; i++) lcd.print(i < data_count ? '*' : ' ');
  lcd.setCursor(data_count, 1);
}

void checkPasswordTimeout() {
  if (!passwordMode) return;
  if (millis() - lastPasswordActivity >= PASSWORD_TIMEOUT_MS) {
    showTempMessage("Password timeout", 700);
    exitPasswordMode();
  }
}

void unlockDoor() {
  machineBusy = true;
  digitalWrite(SOLENOID_PIN, HIGH);
  delay(350);
  digitalWrite(SOLENOID_PIN, LOW);
  machineBusy = false;

  coinLockoutUntil = millis() + COIN_LOCKOUT_MS;
}
void processPasswordKey(char key) {
  lastPasswordActivity = millis();

  if (key == '#') {
    showTempMessage("Cancelled", 500);
    exitPasswordMode();
    return;
  }

  if (key == '*') {
    if (data_count > 0) {
      data_count--;
      Data[data_count] = '\0';
      redrawPasswordLine();
    }
    return;
  }

  if (!((key >= '0' && key <= '9') || (key >= 'A' && key <= 'D'))) return;

  if (data_count < Password_Length - 1) {
    Data[data_count++] = key;
    Data[data_count] = '\0';
    redrawPasswordLine();
  }

  if (data_count == Password_Length - 1) {
    if (!strcmp(Data, Master)) {
      if (pendingPasswordAction == PASS_DOOR) {
        showTempMessage("Door Unlocked", 700);
        unlockDoor();
      } else if (pendingPasswordAction == PASS_FREEVEND) {
        freeVendArmed = true;
        credits = 0;
        showTempMessage("Free Vend ON", 700);
      }
    } else {
      showTempMessage("Incorrect", 800);
    }
    exitPasswordMode();
  }
}

// ---------- Serial helpers ----------
void clearSerialInput() {
  while (Serial.available() > 0) Serial.read();
}

bool waitForMotorAck(const String &code, unsigned long timeoutMs = 6000) {
  unsigned long start = millis();
  String line = "";

  while (millis() - start < timeoutMs) {
    while (Serial.available() > 0) {
      char c = (char)Serial.read();
      if (c == '\r') continue;

      if (c == '\n') {
        line.trim();
        line.toUpperCase();

        if (line.length() > 0) {
          if (line == ("OK " + code)) return true;
          if (line.startsWith("ERR")) return false;
        }
        line = "";
      } else {
        line += c;
      }
    }
  }

  return false;
}

bool sendVendCommand(char row, char num) {
  String code = String(row) + String(num); // e.g. A4
  Serial.print("VEND_");
  Serial.println(code);
  delay(150); // send settle
  return true; // skip ACK for now
}

// ---------- Vending ----------
bool canVendNow() {
  return (credits > 0) || freeVendArmed;
}

bool isValidSelection(char row, char num) {
  if (row == 'A' && (num >= '1' && num <= '4')) return true;
  if (row == 'B' && (num == '1' || num == '2')) return true;
  if (row == 'C' && (num == '1' || num == '2')) return true;
  return false;
}

void completeVendConsumption() {
  if (freeVendArmed) freeVendArmed = false;
  else if (credits > 0) credits = 0;

  selectedRow = '\0';
  coinLockoutUntil = millis() + COIN_LOCKOUT_MS;
  showMainScreen();
}

void processVendingKey(char key) {
  if (!canVendNow() || machineBusy) return;

  if (selectedRow == '\0') {
    if (key == 'A' || key == 'B' || key == 'C') {
      selectedRow = key;
      showRowSelected(selectedRow);
      return;
    }
    if (key == '#') {
      selectedRow = '\0';
      showSelectPrompt();
      return;
    }
    return;
  }

  if (key >= '0' && key <= '9') {
    if (isValidSelection(selectedRow, key)) {
      machineBusy = true;

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Vending ");
      lcd.print(selectedRow);
      lcd.print(key);

      bool ok = sendVendCommand(selectedRow, key);

      if (ok) {
        showTempMessage("Vend OK", 500);
        completeVendConsumption();
      } else {
        showTempMessage("Vend Failed", 900);
        selectedRow = '\0'; // keep credit for retry
        showSelectPrompt();
      }

      machineBusy = false;
    } else {
      showTempMessage("Invalid option", 700);
      showRowSelected(selectedRow);
    }
    return;
  }

  if (key == '#') {
    selectedRow = '\0';
    showSelectPrompt();
  }
}

// ---------- Keypad hold hooks ----------
void keypadEvent(KeypadEvent key) {
  switch (keypad.getState()) {
    case HOLD:
      if (!passwordMode && key == '#') {
        startPasswordMode(PASS_FREEVEND);
      } else if (!passwordMode && key == 'D') {
        startPasswordMode(PASS_DOOR);
      }
      break;
    default:
      break;
  }
}
