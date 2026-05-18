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

// -------------------- Servo configuration --------------------
Servo winnerServo;

// Servo angles define physical direction of winner indication
const int SERVO_P1_ANGLE = 105;
const int SERVO_P2_ANGLE = 15;
const int SERVO_CENTER   = 60;

// -------------------- Stepper configuration --------------------
const int STEPS_PER_REV = 2048;
Stepper tugStepper(STEPS_PER_REV, IN1_PIN, IN3_PIN, IN2_PIN, IN4_PIN);

// Small movement per round to simulate tug effect
const int ROUND_STEPPER_MOVE = 256;

// -------------------- Game parameters --------------------
const int TARGET_WINS = 3;

// Timing constraints (important for game fairness and stability)
const unsigned long FALSE_START_LOCKOUT_MS = 150;  // prevents double triggering
const unsigned long BUTTON_DEBOUNCE_MS = 25;       // removes button noise
const unsigned long BUZZER_DURATION_MS = 150;      // short reaction signal

// -------------------- Game state machine --------------------
// FSM controls full game flow (idle → wait → reaction → result)
enum GameState {
  IDLE,
  WAITING_RANDOM_DELAY,
  WAITING_FOR_REACTION,
  MATCH_OVER
};

GameState gameState = IDLE;

// Player names (can be updated via Serial interface)
String player1Name = "Player1";
String player2Name = "Player2";

// Score tracking
int score1 = 0;
int score2 = 0;

// Timing variables for reaction measurement
unsigned long roundStartMs = 0;
unsigned long randomDelayMs = 0;
unsigned long buzzerOnMs = 0;

// Tracks buzzer state to allow timed auto-disable
bool buzzerActive = false;

// -------------------- Debounced button structure --------------------
// Used to prevent false triggering caused by mechanical bounce
struct DebouncedButton {
  int pin;
  bool lastReading;
  bool stableState;
  unsigned long lastChangeMs;
};

DebouncedButton btn1;
DebouncedButton btn2;

// -------------------- BUTTON HANDLING --------------------

// Initialize button with default state
void initButton(DebouncedButton &btn, int pin) {
  btn.pin = pin;
  pinMode(pin, INPUT);

  bool now = digitalRead(pin);
  btn.lastReading = now;
  btn.stableState = now;
  btn.lastChangeMs = 0;
}

// Synchronizes button state after round/reset (prevents false triggers)
void syncButtonState(DebouncedButton &btn) {
  bool now = digitalRead(btn.pin);
  btn.lastReading = now;
  btn.stableState = now;
  btn.lastChangeMs = millis();
}

// Debounced button press detection (returns TRUE only once per press)
// IMPORTANT: ensures no multiple triggers from a single press
bool readDebouncedButtonPress(DebouncedButton &btn) {
  bool reading = digitalRead(btn.pin);

  if (reading != btn.lastReading) {
    btn.lastChangeMs = millis();   // reset debounce timer
    btn.lastReading = reading;
  }

  // only accept stable input after debounce delay
  if ((millis() - btn.lastChangeMs) >= BUTTON_DEBOUNCE_MS) {
    if (reading != btn.stableState) {
      btn.stableState = reading;

      // external pulldown logic: HIGH = pressed
      if (btn.stableState == HIGH) {
        return true;  // valid press event detected
      }
    }
  }

  return false;
}

// Blocks game until both buttons are released
// IMPORTANT: prevents accidental carry-over inputs between rounds
void waitForButtonsReleased() {
  while (digitalRead(BTN1_PIN) == HIGH || digitalRead(BTN2_PIN) == HIGH) {
    delay(5);
  }

  syncButtonState(btn1);
  syncButtonState(btn2);
}

// -------------------- ACTUATOR CONTROL --------------------

// Moves servo to indicate winner direction visually
void pointServoToWinner(int winner) {
  if (winner == 1) winnerServo.write(SERVO_P1_ANGLE);
  else if (winner == 2) winnerServo.write(SERVO_P2_ANGLE);
  else winnerServo.write(SERVO_CENTER);
}

// Stepper movement simulates tug-of-war physical shift
void moveStepperTowardWinner(int winner) {
  if (winner == 1) tugStepper.step(-ROUND_STEPPER_MOVE);
  else if (winner == 2) tugStepper.step(ROUND_STEPPER_MOVE);
}

// Full rotation used for match victory celebration
void victorySpin() {
  tugStepper.step(STEPS_PER_REV);
}

// -------------------- SERIAL OUTPUT --------------------

// Sends current score to external UI system
void announceScore() {
  Serial.print("SCORE,");
  Serial.print(score1);
  Serial.print(",");
  Serial.println(score2);
}

// Sends detailed round result for analytics/UI
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

// Sends final match winner
void sendMatchWinner(int winner) {
  Serial.print("MATCH_WINNER,");
  Serial.println(winner == 1 ? player1Name : player2Name);
}

// -------------------- BUZZER CONTROL --------------------

// Automatically turns off buzzer after fixed duration
void stopBuzzerIfNeeded() {
  if (buzzerActive && (millis() - buzzerOnMs >= BUZZER_DURATION_MS)) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerActive = false;
  }
}

// Starts reaction signal (critical timing reference point)
void startBuzzer() {
  buzzerOnMs = millis();  // used as reaction baseline
  buzzerActive = true;
  digitalWrite(BUZZER_PIN, HIGH);
  Serial.println("BUZZER_ON");
}

// -------------------- ROUND CONTROL --------------------

// Prepares a new round and generates random delay
// IMPORTANT: ensures unpredictability in reaction test
void resetRoundState() {
  waitForButtonsReleased();

  roundStartMs = millis();
  randomDelayMs = random(1000, 5001); // random reaction start delay

  buzzerOnMs = 0;
  buzzerActive = false;
  digitalWrite(BUZZER_PIN, LOW);

  gameState = WAITING_RANDOM_DELAY;

  Serial.print("ROUND_START,");
  Serial.println(randomDelayMs);
}

// Handles end-of-round logic (win / false start / scoring)
void handleRoundWin(int winner, unsigned long p1Time, unsigned long p2Time, bool falseStart = false, int falseStarter = 0) {

  // update score
  if (winner == 1) score1++;
  else score2++;

  // visual + physical feedback for winner
  pointServoToWinner(winner);
  delay(200);
  moveStepperTowardWinner(winner);

  sendRoundResult(winner, p1Time, p2Time, falseStart, falseStarter);

  waitForButtonsReleased();

  // check if match is finished
  if (score1 >= TARGET_WINS || score2 >= TARGET_WINS) {
    int matchWinner = (score1 >= TARGET_WINS) ? 1 : 2;

    victorySpin();
    sendMatchWinner(matchWinner);

    gameState = MATCH_OVER;
  } else {
    delay(500);
    pointServoToWinner(0); // reset to center position
    resetRoundState();
  }
}

// -------------------- MATCH CONTROL --------------------

// Starts match and resets all states
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

// Parses external commands (UI or PC control)
// Supports: START,p1,p2 and RESET
void parseSerialCommand(String cmd) {
  cmd.trim();

  if (cmd.startsWith("START,")) {
    int firstComma = cmd.indexOf(',');
    int secondComma = cmd.indexOf(',', firstComma + 1);

    String p1 = cmd.substring(firstComma + 1, secondComma);
    String p2 = cmd.substring(secondComma + 1);

    p1.trim();
    p2.trim();

    if (p1.length() == 0) p1 = "Player1";
    if (p2.length() == 0) p2 = "Player2";

    startMatch(p1, p2);
  }

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

// -------------------- SETUP --------------------
void setup() {
  initButton(btn1, BTN1_PIN);
  initButton(btn2, BTN2_PIN);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  winnerServo.attach(SERVO_PIN);
  winnerServo.write(SERVO_CENTER);

  tugStepper.setSpeed(12);

  Serial.begin(115200);
  randomSeed(analogRead(A0));

  Serial.println("READY");
}

// -------------------- MAIN LOOP --------------------
void loop() {

  // handle external commands from PC/UI
  if (Serial.available()) {
    parseSerialCommand(Serial.readStringUntil('\n'));
  }

  stopBuzzerIfNeeded();

  bool btn1Pressed = readDebouncedButtonPress(btn1);
  bool btn2Pressed = readDebouncedButtonPress(btn2);

  // -------------------- STATE MACHINE CORE --------------------
  switch (gameState) {

    case IDLE:
      break;

    case WAITING_RANDOM_DELAY:

      // FALSE START HANDLING (press before buzzer)
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

      // trigger buzzer after random delay
      if (millis() - roundStartMs >= randomDelayMs) {
        startBuzzer();
        gameState = WAITING_FOR_REACTION;
      }
      break;

    case WAITING_FOR_REACTION:

      // simultaneous press = tie rule
      if (btn1Pressed && btn2Pressed) {
        unsigned long t = millis() - buzzerOnMs;
        handleRoundWin(1, t, t, false, 0);
        delay(FALSE_START_LOCKOUT_MS);
        return;
      }

      if (btn1Pressed) {
        handleRoundWin(1, millis() - buzzerOnMs, 0, false, 0);
        delay(FALSE_START_LOCKOUT_MS);
        return;
      }

      if (btn2Pressed) {
        handleRoundWin(2, 0, millis() - buzzerOnMs, false, 0);
        delay(FALSE_START_LOCKOUT_MS);
        return;
      }

      break;

    case MATCH_OVER:
      break;
  }
}
