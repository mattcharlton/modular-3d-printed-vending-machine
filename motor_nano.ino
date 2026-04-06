/*
  Vending Motor Nano
  Receives: VEND_A1 ... VEND_C2 on Serial (RX D0)
  Replies: BUSY <code>, OK <code>, ERR <reason>
*/

#define STEP_PIN 3

// 12 enable pins (DRV8825 EN is active LOW)
const uint8_t EN_PINS[12] = {
  2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, A0
};

const int STEPS_PER_VEND = 3500; // your calibrated value
const int PULSE_US = 1200;
const int ENABLE_SETTLE_MS = 100;

String inLine;

void setup() {
  Serial.begin(9600);

  pinMode(STEP_PIN, OUTPUT);
  digitalWrite(STEP_PIN, LOW);

  for (uint8_t i = 0; i < 12; i++) {
    pinMode(EN_PINS[i], OUTPUT);
    digitalWrite(EN_PINS[i], HIGH); // disable all
  }

  Serial.println("MOTOR_CTRL_READY");
}

void loop() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    if (c == '\r') continue;

    if (c == '\n') {
      inLine.trim();
      inLine.toUpperCase();
      if (inLine.length() > 0) processCommand(inLine);
      inLine = "";
    } else {
      inLine += c;
      if (inLine.length() > 64) {
        inLine = "";
        Serial.println("ERR LINE_TOO_LONG");
      }
    }
  }
}

void processCommand(const String &cmd) {
  if (cmd == "PING") {
    Serial.println("PONG");
    return;
  }

  if (cmd.startsWith("VEND_")) {
    String code = cmd.substring(5);
    Serial.print("BUSY ");
    Serial.println(code);

    bool ok = handleVendCode(code);

    if (ok) {
      Serial.print("OK ");
      Serial.println(code);
    } else {
      Serial.print("ERR BAD_SELECTION ");
      Serial.println(code);
    }
    return;
  }

  Serial.println("ERR UNKNOWN_CMD");
}

void disableAll() {
  for (uint8_t i = 0; i < 12; i++) digitalWrite(EN_PINS[i], HIGH);
}

void enableOne(uint8_t idx) {
  digitalWrite(EN_PINS[idx], LOW);
}

void enableTwo(uint8_t a, uint8_t b) {
  digitalWrite(EN_PINS[a], LOW);
  digitalWrite(EN_PINS[b], LOW);
}

void pulseSteps(int steps) {
  for (int i = 0; i < steps; i++) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(PULSE_US);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(PULSE_US);
  }
}

void vendSingle(uint8_t m) {
  disableAll();
  delay(2);
  enableOne(m);
  delay(ENABLE_SETTLE_MS);
  pulseSteps(STEPS_PER_VEND);
  digitalWrite(EN_PINS[m], HIGH);
}

void vendDouble(uint8_t m1, uint8_t m2) {
  disableAll();
  delay(2);
  enableTwo(m1, m2);
  delay(ENABLE_SETTLE_MS);
  pulseSteps(STEPS_PER_VEND);
  digitalWrite(EN_PINS[m1], HIGH);
  digitalWrite(EN_PINS[m2], HIGH);
}

// Mapping:
// A1->M1, A2->M2, A3->M3, A4->M4
// B1->M5+M6, B2->M7+M8
// C1->M9+M10, C2->M11+M12
bool handleVendCode(const String &code) {
  if (code == "A1") { vendSingle(0); return true; }
  if (code == "A2") { vendSingle(1); return true; }
  if (code == "A3") { vendSingle(2); return true; }
  if (code == "A4") { vendSingle(3); return true; }

  if (code == "B1") { vendDouble(4, 5); return true; }
  if (code == "B2") { vendDouble(6, 7); return true; }

  if (code == "C1") { vendDouble(8, 9); return true; }
  if (code == "C2") { vendDouble(10, 11); return true; }

  return false;
}
