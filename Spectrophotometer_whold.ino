#include <Wire.h>
#include <Adafruit_MCP4725.h>
#include <LiquidCrystal_I2C.h>

// --- I2C Device Initialization ---
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_MCP4725 dac;

// --- Pin Definitions ---
const int SENSOR_PIN = A0;
const int CALIBRATE_BUTTON_PIN = 2;
const int HOLD_BUTTON_PIN = 3;

// --- Global Settings ---
const int LED_BRIGHTNESS = 4095; // DAC brightness (0-4095)
float V_blank = 0.0;             // Stores the calibration voltage

// --- State Machine ---
enum OperatingMode {
  MODE_STANDALONE,
  MODE_STREAMING
};
OperatingMode currentMode = MODE_STANDALONE;

// --- NEW: Global variables for Hold Function ---
bool isHoldActive = false;      // Tracks if we are in "hold" mode
bool holdButtonWasUp = true;    // For button state detection (debounce)
float held_OD = 0.0;            // Stores the held OD value
float held_V_sample = 0.0;      // Stores the held V_sample value
// --- End of new variables ---


void setup() {
  // Start Serial at a high baud rate for fast streaming
  Serial.begin(115200); 
  
  // Initialize I2C devices
  lcd.init();
  lcd.backlight();
  dac.begin(0x62); // 0x62 is the default DAC address
  
  // Set up our buttons
  pinMode(CALIBRATE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(HOLD_BUTTON_PIN, INPUT_PULLUP);
  
  // Set the LED to our default brightness
  dac.setVoltage(LED_BRIGHTNESS, false);

  // Initial LCD message
  lcd.print("Standalone Mode");
  lcd.setCursor(0, 1);
  lcd.print("Press to Calib.");
}

void loop() {
  // 1. Always check for commands from the PC
  checkSerialForCommands();

  // 2. Run code based on the current mode
  if (currentMode == MODE_STANDALONE) {
    runStandaloneMode();
  } else { // currentMode == MODE_STREAMING
    runStreamingMode();
  }
}

/**
 * Checks for incoming Serial data to switch modes.
 * 'S' = Start Streaming Mode
 * 'X' = Exit streaming and return to Standalone Mode
 */
void checkSerialForCommands() {
  if (Serial.available() > 0) {
    char command = Serial.read();
    
    if (command == 'S') {
      currentMode = MODE_STREAMING;
      lcd.clear();
      lcd.print("PC Control Mode");
      lcd.setCursor(0, 1);
      lcd.print("Streaming...");
      
    } else if (command == 'X') {
      currentMode = MODE_STANDALONE;
      dac.setVoltage(LED_BRIGHTNESS, false);
      lcd.clear();
      lcd.print("Standalone Mode");
      lcd.setCursor(0, 1);
      lcd.print("Press to Calib.");
    }
  }
}

/**
 * This function runs ONLY when in MODE_STANDALONE
 * --- MODIFIED with HOLD logic ---
 */
void runStandaloneMode() {
  
  // --- 1. Check for Hold Button Press (Edge Detection) ---
  bool holdButtonIsDown = (digitalRead(HOLD_BUTTON_PIN) == LOW);
  if (holdButtonIsDown && holdButtonWasUp) {
    // Button was just pressed
    isHoldActive = !isHoldActive; // Toggle the hold state
    delay(50); // Simple debounce
  }
  holdButtonWasUp = !holdButtonIsDown; // Update the button's last state

  // --- 2. Check for Calibrate Button Press ---
  if (digitalRead(CALIBRATE_BUTTON_PIN) == LOW) {
    lcd.clear();
    lcd.print("Calibrating...");
    delay(500); // Simple debounce
    
    float readingSum = 0;
    for (int i = 0; i < 20; i++) {
      readingSum += analogRead(SENSOR_PIN);
      delay(10);
    }
    float avgReading = readingSum / 20.0;
    
    V_blank = (avgReading / 1023.0) * 5.0;
    
    lcd.clear();
    lcd.print("Calib. OK!");
    lcd.setCursor(0, 1);
    lcd.print("V: " + String(V_blank, 3));
    delay(2000);
    lcd.clear();
    lcd.print("Insert Sample...");
    
    // When we re-calibrate, we should un-hold
    isHoldActive = false; 
    held_OD = 0.0;
    held_V_sample = 0.0;
  }

  // --- 3. Main Display Logic ---
  if (V_blank > 0.0) {
    
    // --- 4. Update readings ONLY if NOT holding ---
    if (!isHoldActive) {
      // Take a stable reading
      float readingSum = 0;
      for (int i = 0; i < 20; i++) {
        readingSum += analogRead(SENSOR_PIN);
        delay(10);
      }
      float avgReading = readingSum / 20.0;
      float V_sample = (avgReading / 1023.0) * 5.0;

      // Safety checks
      if (V_sample > V_blank) V_sample = V_blank;
      if (V_sample <= 0) V_sample = 0.00001; 

      // Calculation (using your hard-coded formula)
      float opticalDensity = 0.8789 * (-log10(V_sample)) + 0.3209;

      // Store these new values in the "held" variables
      held_OD = opticalDensity;
      held_V_sample = V_sample;
    }

    // --- 5. Display the (possibly held) values ---
    lcd.setCursor(0, 0);
    lcd.print("OD: ");
    lcd.print(held_OD, 3);
    lcd.print("   "); // Clear old data

    lcd.setCursor(0, 1);
    lcd.print("V_samp: ");
    lcd.print(held_V_sample, 3);
    lcd.print("   "); // Clear old data

    // --- 6. Show "H" if holding ---
    if (isHoldActive) {
      lcd.setCursor(15, 0); // Top-right corner
      lcd.print("H");
    } else {
      lcd.setCursor(15, 0);
      lcd.print(" "); // Clear the 'H'
    }
  }
}


/**
 * This function runs ONLY when in MODE_STREAMING
 */
void runStreamingMode() {
  // Just read the raw value and send it over Serial
  int rawValue = analogRead(SENSOR_PIN);
  if (rawValue<0.01) rawValue = 0.01;
  Serial.println(rawValue);
  
  // Wait 50ms to match the Python app's 20Hz refresh rate
  delay(50);
}