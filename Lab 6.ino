#include <Servo.h>
#include <Stepper.h>

// -------------------- Pin definitions --------------------
const int BTN1_PIN = 2;
const int BTN2_PIN = 3;

const int IN1_PIN = 4;
const int IN2_PIN = 5;
const int IN3_PIN = 6;
const int IN4_PIN = 7;

const int BUZZER_PIN = 8;
const int SERVO_PIN  = 9;

// -------------------- Servo --------------------
Servo winnerServo;

// Your requested angles
const int SERVO_P1_ANGLE = 105;
const int SERVO_P2_ANGLE = 15;
const int SERVO_CENTER   = 60;

// -------------------- Stepper --------------------
const int STEPS_PER_REV = 2048;
Stepper tugStepper(STEPS_PER_REV, IN1_PIN, IN3_PIN, IN2_PIN, IN4_PIN);
const int ROUND_STEPPER_MOVE = 256;

// -------------------- Game settings --------------------
const int TARGET_WINS = 3;
const unsigned long FALSE_START_LOCKOUT_MS = 150;
const unsigned long BUTTON_DEBOUNCE_MS = 25;
const unsigned long BUZZER_DURATION_MS = 150;

// -------------------- Game state --------------------
enum GameState {
  IDLE,
  WAITING_RANDOM_DELAY,
  WAITING_FOR_REACTION,
  MATCH_OVER
};

GameState gameState = IDLE;

String player1Name = "Player1";
String player2Name = "Player2";

int score1 = 0;
int score2 = 0;

// Round timing
unsigned long roundStartMs = 0;
unsigned long randomDelayMs = 0;
unsigned long buzzerOnMs = 0;

// Buzzer state
bool buzzerActive = false;

// -------------------- Debounced button structure --------------------
// This version assumes:
// - button pin has external pulldown resistor to GND
// - not pressed = LOW
// - pressed = HIGH
struct DebouncedButton {
  int pin;
  bool lastReading;
  bool stableState;
  unsigned long lastChangeMs;
};

DebouncedButton btn1;
DebouncedButton btn2;

// -------------------- Button helpers --------------------
void initButton(DebouncedButton &btn, int pin) {
  btn.pin = pin;
  pinMode(pin, INPUT);   // external pulldown required
  bool now = digitalRead(pin);
  btn.lastReading = now;
  btn.stableState = now;
  btn.lastChangeMs = 0;
}

void syncButtonState(DebouncedButton &btn) {
  bool now = digitalRead(btn.pin);
  btn.lastReading = now;
  btn.stableState = now;
  btn.lastChangeMs = millis();
}

// Returns true only once, on the press edge
bool readDebouncedButtonPress(DebouncedButton &btn) {
  bool reading = digitalRead(btn.pin);

  if (reading != btn.lastReading) {
    btn.lastChangeMs = millis();
    btn.lastReading = reading;
  }

  if ((millis() - btn.lastChangeMs) >= BUTTON_DEBOUNCE_MS) {
    if (reading != btn.stableState) {
      btn.stableState = reading;

      // external pulldown: HIGH means pressed
      if (btn.stableState == HIGH) {
        return true;
      }
    }
  }

  return false;
}

void waitForButtonsReleased() {
  while (digitalRead(BTN1_PIN) == HIGH || digitalRead(BTN2_PIN) == HIGH) {
    delay(5);
  }

  syncButtonState(btn1);
  syncButtonState(btn2);
}

// -------------------- Helper functions --------------------
void pointServoToWinner(int winner) {
  if (winner == 1) {
    winnerServo.write(SERVO_P1_ANGLE);   // 105
  } else if (winner == 2) {
    winnerServo.write(SERVO_P2_ANGLE);   // 15
  } else {
    winnerServo.write(SERVO_CENTER);     // 60
  }
}

void moveStepperTowardWinner(int winner) {
  if (winner == 1) {
    tugStepper.step(-ROUND_STEPPER_MOVE);
  } else if (winner == 2) {
    tugStepper.step(ROUND_STEPPER_MOVE);
  }
}

void victorySpin() {
  tugStepper.step(STEPS_PER_REV);
}

void announceScore() {
  Serial.print("SCORE,");
  Serial.print(score1);
  Serial.print(",");
  Serial.println(score2);
}

void sendRoundResult(int winner, unsigned long p1Time, unsigned long p2Time, bool falseStart, int falseStarter) {
  // ROUND_RESULT,winner,p1Time,p2Time,falseStart,falseStarter,score1,score2
  Serial.print("ROUND_RESULT,");
  Serial.print(winner);
  Serial.print(",");
  Serial.print(p1Time);
  Serial.print(",");
  Serial.print(p2Time);
  Serial.print(",");
  Serial.print(falseStart ? 1 : 0);
  Serial.print(",");
  Serial.print(falseStarter);
  Serial.print(",");
  Serial.print(score1);
  Serial.print(",");
  Serial.println(score2);
}

void sendMatchWinner(int winner) {
  Serial.print("MATCH_WINNER,");
  if (winner == 1) {
    Serial.println(player1Name);
  } else {
    Serial.println(player2Name);
  }
}

void stopBuzzerIfNeeded() {
  if (buzzerActive && (millis() - buzzerOnMs >= BUZZER_DURATION_MS)) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerActive = false;
  }
}

void startBuzzer() {
  buzzerOnMs = millis();
  buzzerActive = true;
  digitalWrite(BUZZER_PIN, HIGH);
  Serial.println("BUZZER_ON");
}

void resetRoundState() {
  waitForButtonsReleased();

  roundStartMs = millis();
  randomDelayMs = random(1000, 5001); // 1 to 5 seconds
  buzzerOnMs = 0;
  buzzerActive = false;
  digitalWrite(BUZZER_PIN, LOW);

  gameState = WAITING_RANDOM_DELAY;

  Serial.print("ROUND_START,");
  Serial.println(randomDelayMs);
}

void handleRoundWin(int winner, unsigned long p1Time, unsigned long p2Time, bool falseStart = false, int falseStarter = 0) {
  if (winner == 1) {
    score1++;
  } else {
    score2++;
  }

  pointServoToWinner(winner);
  delay(200);
  moveStepperTowardWinner(winner);

  sendRoundResult(winner, p1Time, p2Time, falseStart, falseStarter);

  waitForButtonsReleased();

  if (score1 >= TARGET_WINS || score2 >= TARGET_WINS) {
    int matchWinner = (score1 >= TARGET_WINS) ? 1 : 2;
    victorySpin();
    sendMatchWinner(matchWinner);
    gameState = MATCH_OVER;
  } else {
    delay(500);
    pointServoToWinner(0);   // goes back to 60
    resetRoundState();
  }
}

void startMatch(String p1, String p2) {
  player1Name = p1;
  player2Name = p2;
  score1 = 0;
  score2 = 0;

  pointServoToWinner(0);   // initial position = 60

  Serial.print("MATCH_STARTED,");
  Serial.print(player1Name);
  Serial.print(",");
  Serial.println(player2Name);

  announceScore();
  resetRoundState();
}

void parseSerialCommand(String cmd) {
  cmd.trim();

  if (cmd.startsWith("START,")) {
    int firstComma = cmd.indexOf(',');
    int secondComma = cmd.indexOf(',', firstComma + 1);

    if (secondComma > 0) {
      String p1 = cmd.substring(firstComma + 1, secondComma);
      String p2 = cmd.substring(secondComma + 1);

      p1.trim();
      p2.trim();

      if (p1.length() == 0) p1 = "Player1";
      if (p2.length() == 0) p2 = "Player2";

      startMatch(p1, p2);
    }
  }

  if (cmd == "RESET") {
    score1 = 0;
    score2 = 0;
    pointServoToWinner(0);   // back to 60
    gameState = IDLE;
    buzzerActive = false;
    digitalWrite(BUZZER_PIN, LOW);
    waitForButtonsReleased();
    Serial.println("RESET_DONE");
  }
}

// -------------------- Setup --------------------
void setup() {
  initButton(btn1, BTN1_PIN);
  initButton(btn2, BTN2_PIN);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  winnerServo.attach(SERVO_PIN);
  winnerServo.write(SERVO_CENTER);   // initial position = 60

  tugStepper.setSpeed(12);

  Serial.begin(115200);
  randomSeed(analogRead(A0));

  Serial.println("READY");
}

// -------------------- Main loop --------------------
void loop() {
  // GUI commands
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    parseSerialCommand(cmd);
  }

  stopBuzzerIfNeeded();

  bool btn1Pressed = readDebouncedButtonPress(btn1);
  bool btn2Pressed = readDebouncedButtonPress(btn2);

  switch (gameState) {
    case IDLE:
      break;

    case WAITING_RANDOM_DELAY: {
      // False start
      if (btn1Pressed) {
        handleRoundWin(2, 0, 0, true, 1);
        delay(FALSE_START_LOCKOUT_MS);
        return;
      }

      if (btn2Pressed) {
        handleRoundWin(1, 0, 0, true, 2);
        delay(FALSE_START_LOCKOUT_MS);
        return;
      }

      if (millis() - roundStartMs >= randomDelayMs) {
        startBuzzer();
        gameState = WAITING_FOR_REACTION;
      }
      break;
    }

    case WAITING_FOR_REACTION: {
      if (btn1Pressed && btn2Pressed) {
        unsigned long t = millis() - buzzerOnMs;
        handleRoundWin(1, t, t, false, 0);   // tie rule
        delay(FALSE_START_LOCKOUT_MS);
        return;
      }

      if (btn1Pressed) {
        unsigned long p1Time = millis() - buzzerOnMs;
        handleRoundWin(1, p1Time, 0, false, 0);
        delay(FALSE_START_LOCKOUT_MS);
        return;
      }

      if (btn2Pressed) {
        unsigned long p2Time = millis() - buzzerOnMs;
        handleRoundWin(2, 0, p2Time, false, 0);
        delay(FALSE_START_LOCKOUT_MS);
        return;
      }

      break;
    }

    case MATCH_OVER:
      break;
  }
}
