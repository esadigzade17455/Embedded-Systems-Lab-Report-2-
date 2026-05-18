#include <SPI.h>              // SPI communication library (used for RFID)
#include <MFRC522.h>          // RFID module library (RC522)
#include <Keypad.h>           // Keypad matrix library
#include <IRremote.hpp>       // IR receiver library

// ================= RFID CONFIG =================
#define RFID_SS_PIN 10        // RFID Slave Select pin  SCK=D13, MOSI=D11, MISO=D12,
#define RFID_RST_PIN 9        // RFID Reset pin. // RFID (RC522) → SDA(SS)=D10, RST=D9
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN); // Create RFID object

// ================= IR =================
#define IR_RECEIVE_PIN 2      // IR receiver signal pin

// ================= LEDS =================
#define GREEN_LED A2          // Green LED pin (UNLOCKED state)
#define RED_LED A3            // Red LED pin (LOCKED state)

// ================= KEYPAD =================
const byte ROWS = 4;          // Number of keypad rows
const byte COLS = 4;          // Number of keypad columns

// Key layout of keypad
char keys[ROWS][COLS] = {
  {'A','3','2','1'},
  {'B','6','5','4'},
  {'C','9','8','7'},
  {'D','#','0','*'}
};

// Row and column pin connections
byte rowPins[ROWS] = {3,4,5,6};
byte colPins[COLS] = {7,8,A0,A1};

// Create keypad object
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ================= STATE =================
enum State { UNLOCKED, LOCKED };   // System states
State state = UNLOCKED;            // Initial state is UNLOCKED

char lockCode[5] = "";             // Stored 4-digit lock code
char buffer[5] = "";               // Keypad input buffer
byte index = 0;                    // Keypad input index

char irBuffer[5];                  // IR input buffer
byte irIndex = 0;                  // IR input index

// ================= IR MAP =================
// Converts IR remote button codes into digits
char mapIR(uint8_t cmd) {
  switch(cmd) {
    case 0x16: return '0';
    case 0x0C: return '1';
    case 0x18: return '2';
    case 0x5E: return '3';
    case 0x08: return '4';
    case 0x1C: return '5';
    case 0x5A: return '6';
    case 0x42: return '7';
    case 0x52: return '8';
    case 0x4A: return '9';
    default: return '\0';   // invalid key
  }
}

// ================= LED CONTROL =================
// Turns LEDs ON based on system state
void updateLEDs() {
  if (state == UNLOCKED) {
    digitalWrite(GREEN_LED, HIGH); // unlocked = green ON
    digitalWrite(RED_LED, LOW);    // red OFF
  } else {
    digitalWrite(GREEN_LED, LOW);   // locked = green OFF
    digitalWrite(RED_LED, HIGH);    // red ON
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(9600);        // Serial communication start

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  SPI.begin();               // Start SPI for RFID
  rfid.PCD_Init();           // Initialize RFID module

  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK); // Start IR receiver

  Serial.println("SYS,READY");           // System ready message
  Serial.println("SYS,STATE=UNLOCKED");  // Initial state

  updateLEDs();   // Set initial LED state
}

// ================= LOOP =================
void loop() {
  handleKeypad();  // Check keypad input
  handleIR();      // Check IR input
  handleRFID();    // Check RFID card

  updateLEDs();    // Keep LEDs updated
}

// ================= KEYPAD ================= // cerniy krasniy beliy jeltiy siniy zeleniy oranjeviy cerniy 7
void handleKeypad() {
  char key = keypad.getKey();  // Read key press
  if (!key) return;            // If no key, exit

  if (state == LOCKED) return; // Ignore keypad if locked

  // If number pressed, store it
  if (key >= '0' && key <= '9') {
    if (index < 4) buffer[index++] = key;
  }

  // Reset input
  if (key == '*') {
    index = 0;
    memset(buffer, 0, sizeof(buffer));
  }

  // Confirm code entry
  if (key == '#') {
    if (index == 4) {
      buffer[4] = '\0';          // End string
      strcpy(lockCode, buffer);  // Save lock code

      index = 0;

      state = LOCKED;            // Lock system
      Serial.println("SYS,LOCKED");
    } else {
      index = 0;                 // invalid input reset
    }
  }
}

// ================= IR =================
void handleIR() {
  if (state != LOCKED) return;  // Only works when locked

  if (IrReceiver.decode()) {

    // Ignore repeat signals
    if (!(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT)) {

      char digit = mapIR(IrReceiver.decodedIRData.command);

      // If valid digit received
      if (digit >= '0' && digit <= '9') {

        if (irIndex < 4) {
          irBuffer[irIndex++] = digit;
        }

        // If 4 digits entered, check password
        if (irIndex == 4) {
          irBuffer[4] = '\0';

          // Compare IR code with stored lock code
          if (strcmp(irBuffer, lockCode) == 0) {
            state = UNLOCKED;
            Serial.println("SYS,UNLOCKED");
          }

          irIndex = 0;
          memset(irBuffer, 0, sizeof(irBuffer));
        }
      }
    }

    IrReceiver.resume(); // ready for next IR signal
  }
}

// ================= RFID =================
void handleRFID() {
  if (state != UNLOCKED) return; // RFID works only when unlocked

  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  String uid = ""; // store card UID

  // Convert UID bytes to HEX string
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }

  uid.toUpperCase(); // convert to uppercase

  Serial.print("TAG,");
  Serial.println(uid); // send UID to serial monitor

  rfid.PICC_HaltA(); // stop reading card
}
