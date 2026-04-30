#include <Servo.h>
#include <Stepper.h>

// -------------------- Pin definitions --------------------
// Buttons
const int BTN1_PIN = 2;
const int BTN2_PIN = 3;

// Stepper motor driver (ULN2003 inputs)
const int IN1_PIN = 4;
const int IN2_PIN = 5;
const int IN3_PIN = 6;
const int IN4_PIN = 7;

// Buzzer and Servo
const int BUZZER_PIN = 8;
const int SERVO_PIN  = 9;

// -------------------- Servo --------------------
Servo winnerServo;

// Servo angles for each player and neutral position
const int SERVO_P1_ANGLE = 105;  // Player 1 side
const int SERVO_P2_ANGLE = 15;   // Player 2 side
const int SERVO_CENTER   = 60;   // Neutral position

// -------------------- Stepper --------------------
const int STEPS_PER_REV = 2048;

// Stepper initialization (correct coil sequence for ULN2003)
Stepper tugStepper(STEPS_PER_REV, IN1_PIN, IN3_PIN, IN2_PIN, IN4_PIN);

// Movement per round (small shift left/right)
const int ROUND_STEPPER_MOVE = 64;

// -------------------- Game settings --------------------
const int TARGET_WINS = 3;                    // First to 3 wins
const unsigned long FALSE_START_LOCKOUT_MS = 150;
const unsigned long BUTTON_DEBOUNCE_MS = 25; // Debounce delay
const unsigned long BUZZER_DURATION_MS = 150;

// -------------------- Game state --------------------
// State machine controlling game flow
enum GameState {
  IDLE,
  WAITING_RANDOM_DELAY,
  WAITING_FOR_REACTION,
  MATCH_OVER
};

GameState gameState = IDLE;

// Player names
String player1Name = "Player1";
String player2Name = "Player2";

// Scores
int score1 = 0;
int score2 = 0;

// -------------------- Timing variables --------------------
unsigned long roundStartMs = 0;
unsigned long randomDelayMs = 0;
unsigned long buzzerOnMs = 0;

// Buzzer state
bool buzzerActive = false;

// -------------------- Debounced button structure --------------------
// Handles stable button reading (avoids noise)
struct DebouncedButton {
  int pin;
  bool lastReading;
  bool stableState;
  unsigned long lastChangeMs;
};

DebouncedButton btn1;
DebouncedButton btn2;

// -------------------- Button helpers --------------------
// Initialize button
void initButton(DebouncedButton &btn, int pin) {
  btn.pin = pin;
  pinMode(pin, INPUT);   // external pulldown required
  bool now = digitalRead(pin);
  btn.lastReading = now;
  btn.stableState = now;
  btn.lastChangeMs = 0;
}

// Sync button state after release
void syncButtonState(DebouncedButton &btn) {
  bool now = digitalRead(btn.pin);
  btn.lastReading = now;
  btn.stableState = now;
  btn.lastChangeMs = millis();
}

// Detect button press (with debounce)
bool readDebouncedButtonPress(DebouncedButton &btn) {
  bool reading = digitalRead(btn.pin);

  // Detect change
  if (reading != btn.lastReading) {
    btn.lastChangeMs = millis();
    btn.lastReading = reading;
  }

  // Check if stable for debounce time
  if ((millis() - btn.lastChangeMs) >= BUTTON_DEBOUNCE_MS) {
    if (reading != btn.stableState) {
      btn.stableState = reading;

      // Only trigger on press (HIGH)
      if (btn.stableState == HIGH) {
        return true;
      }
    }
  }

  return false;
}

// Wait until both buttons are released
void waitForButtonsReleased() {
  while (digitalRead(BTN1_PIN) == HIGH || digitalRead(BTN2_PIN) == HIGH) {
    delay(5);
  }

  syncButtonState(btn1);
  syncButtonState(btn2);
}

// -------------------- Helper functions --------------------

// Move servo to indicate winner
void pointServoToWinner(int winner) {
  if (winner == 1) {
    winnerServo.write(SERVO_P1_ANGLE);
  } else if (winner == 2) {
    winnerServo.write(SERVO_P2_ANGLE);
  } else {
    winnerServo.write(SERVO_CENTER); // reset to center
  }
}

// Move stepper slightly toward winner side
void moveStepperTowardWinner(int winner) {
  if (winner == 1) {
    tugStepper.step(-ROUND_STEPPER_MOVE);
  } else if (winner == 2) {
    tugStepper.step(ROUND_STEPPER_MOVE);
  }
}

// Full rotation at game end
void victorySpin() {
  tugStepper.step(STEPS_PER_REV);
}

// Send score to GUI
void announceScore() {
  Serial.print("SCORE,");
  Serial.print(score1);
  Serial.print(",");
  Serial.println(score2);
}

// Send round result to GUI
void sendRoundResult(int winner, unsigned long p1Time, unsigned long p2Time, bool falseStart, int falseStarter) {
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

// Send final winner
void sendMatchWinner(int winner) {
  Serial.print("MATCH_WINNER,");
  if (winner == 1) {
    Serial.println(player1Name);
  } else {
    Serial.println(player2Name);
  }
}

// Stop buzzer after short duration
void stopBuzzerIfNeeded() {
  if (buzzerActive && (millis() - buzzerOnMs >= BUZZER_DURATION_MS)) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerActive = false;
  }
}

// Start buzzer
void startBuzzer() {
  buzzerOnMs = millis();
  buzzerActive = true;
  digitalWrite(BUZZER_PIN, HIGH);
  Serial.println("BUZZER_ON");
}

// Reset and prepare new round
void resetRoundState() {
  waitForButtonsReleased();

  roundStartMs = millis();
  randomDelayMs = random(1000, 5001); // random delay 1–5 sec

  buzzerOnMs = 0;
  buzzerActive = false;
  digitalWrite(BUZZER_PIN, LOW);

  gameState = WAITING_RANDOM_DELAY;

  Serial.print("ROUND_START,");
  Serial.println(randomDelayMs);
}

// Handle result of a round
void handleRoundWin(int winner, unsigned long p1Time, unsigned long p2Time, bool falseStart = false, int falseStarter = 0) {

  // Update score
  if (winner == 1) score1++;
  else score2++;

  // Show winner
  pointServoToWinner(winner);
  delay(200);

  // Move stepper toward winner
  moveStepperTowardWinner(winner);

  // Send result to GUI
  sendRoundResult(winner, p1Time, p2Time, falseStart, falseStarter);

  waitForButtonsReleased();

  // Check if match is over
  if (score1 >= TARGET_WINS || score2 >= TARGET_WINS) {
    int matchWinner = (score1 >= TARGET_WINS) ? 1 : 2;

    victorySpin();
    sendMatchWinner(matchWinner);

    gameState = MATCH_OVER;
  } else {
    delay(500);
    pointServoToWinner(0);   // return to center
    resetRoundState();
  }
}

// Start a new match
void startMatch(String p1, String p2) {
  player1Name = p1;
  player2Name = p2;
  score1 = 0;
  score2 = 0;

  pointServoToWinner(0);

  Serial.print("MATCH_STARTED,");
  Serial.print(player1Name);
  Serial.print(",");
  Serial.println(player2Name);

  announceScore();
  resetRoundState();
}

// Handle commands from GUI
void parseSerialCommand(String cmd) {
  cmd.trim();

  // Start match command
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

  // Reset command
  if (cmd == "RESET") {
    score1 = 0;
    score2 = 0;
    pointServoToWinner(0);
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
  winnerServo.write(SERVO_CENTER);

  tugStepper.setSpeed(12);

  Serial.begin(115200);
  randomSeed(analogRead(A0)); // random seed

  Serial.println("READY");
}

// -------------------- Main loop --------------------
void loop() {

  // Read commands from GUI
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    parseSerialCommand(cmd);
  }

  stopBuzzerIfNeeded();

  // Read buttons
  bool btn1Pressed = readDebouncedButtonPress(btn1);
  bool btn2Pressed = readDebouncedButtonPress(btn2);

  switch (gameState) {

    case IDLE:
      break;

    case WAITING_RANDOM_DELAY:

      // False start detection
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

      // Wait until delay passes, then start buzzer
      if (millis() - roundStartMs >= randomDelayMs) {
        startBuzzer();
        gameState = WAITING_FOR_REACTION;
      }
      break;

    case WAITING_FOR_REACTION:

      // Both pressed (tie case)
      if (btn1Pressed && btn2Pressed) {
        unsigned long t = millis() - buzzerOnMs;
        handleRoundWin(1, t, t, false, 0);
        delay(FALSE_START_LOCKOUT_MS);
        return;
      }

      // Player 1 wins
      if (btn1Pressed) {
        unsigned long p1Time = millis() - buzzerOnMs;
        handleRoundWin(1, p1Time, 0, false, 0);
        delay(FALSE_START_LOCKOUT_MS);
        return;
      }

      // Player 2 wins
      if (btn2Pressed) {
        unsigned long p2Time = millis() - buzzerOnMs;
        handleRoundWin(2, 0, p2Time, false, 0);
        delay(FALSE_START_LOCKOUT_MS);
        return;
      }

      break;

    case MATCH_OVER:
      break;
  }
}
