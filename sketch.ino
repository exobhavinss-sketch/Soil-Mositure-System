/* ==============================================================================
 * Project: Smart Irrigation Control System using Arduino Uno
 * Platform: Wokwi Simulator & Physical Arduino Uno Hardware
 * Course: Computer Architecture & Microcontrollers
 * 
 * Description:
 * An automated, production-grade irrigation controller simulating real-world 
 * closed-loop soil moisture regulation. Reads simulated soil moisture via an 
 * analog potentiometer, classifies moisture states (DRY, MOIST, WET), drives 
 * a relay and status LED for pump actuation, provides a transient audible alert,
 * and maintains continuous, flicker-free LCD and Serial telemetry.
 * 
 * Pin Configuration:
 * - Soil Sensor (Potentiometer) : Analog Pin A0
 * - LCD 16x2 (I2C)             : SDA -> A4, SCL -> A5 (I2C Address: 0x27)
 * - Relay Module (Pump Control): Digital Pin 7 (Active HIGH)
 * - Water Pump Indicator LED   : Digital Pin 8 (via 220 Ohm resistor)
 * - Active Buzzer (Alert)      : Digital Pin 9
 * ============================================================================== */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// -----------------------------------------------------------------------------
// HARDWARE PIN ASSIGNMENTS
// -----------------------------------------------------------------------------
constexpr uint8_t PIN_SOIL_SENSOR = A0;  // Analog input from simulated soil sensor
constexpr uint8_t PIN_RELAY       = 7;   // Digital output to pump relay module
constexpr uint8_t PIN_PUMP_LED    = 8;   // Digital output to water pump indicator LED
constexpr uint8_t PIN_BUZZER      = 9;   // Digital output to active alarm buzzer

// -----------------------------------------------------------------------------
// SYSTEM THRESHOLDS & LOGIC CONSTANTS
// -----------------------------------------------------------------------------
constexpr uint8_t THRESHOLD_DRY_MAX   = 30;  // 0% - 30%  : DRY (Irrigation Required)
constexpr uint8_t THRESHOLD_MOIST_MAX = 70;  // 31% - 70% : MOIST (Optimal Moisture)
                                             // 71% - 100%: WET (Excess Moisture)

// Relay logic state definitions
constexpr uint8_t RELAY_ACTIVE   = HIGH;
constexpr uint8_t RELAY_INACTIVE = LOW;

// Actuator timing constants (non-blocking)
constexpr unsigned long SERIAL_LOG_INTERVAL_MS = 1000; // Update serial every 1 second
constexpr unsigned long BUZZER_BEEP_DURATION_MS = 250;  // 250ms short alert chirp
constexpr unsigned int  BUZZER_TONE_FREQ_HZ     = 1000; // 1 kHz audible alert frequency

// -----------------------------------------------------------------------------
// SOIL MOISTURE STATES
// -----------------------------------------------------------------------------
enum SoilState {
  SOIL_DRY,
  SOIL_MOIST,
  SOIL_WET
};

// -----------------------------------------------------------------------------
// GLOBAL OBJECTS & STATE VARIABLES
// -----------------------------------------------------------------------------
// LCD I2C address 0x27 with 16 columns and 2 rows
LiquidCrystal_I2C lcd(0x27, 16, 2);

SoilState currentState      = SOIL_MOIST;
SoilState previousState     = SOIL_MOIST;
int currentMoisturePercent  = 0;
bool isPumpRunning          = false;
bool isBuzzerActive         = false;

unsigned long buzzerStartTime   = 0;
unsigned long lastSerialLogTime = 0;

// -----------------------------------------------------------------------------
// FUNCTION PROTOTYPES
// -----------------------------------------------------------------------------
void configureGPIOPins();
void displaySplashScreen();
int readMoisturePercentage();
SoilState evaluateSoilState(int percentage);
void controlActuators(SoilState state);
void handleBuzzerAlert(SoilState newState);
void updateBuzzerTiming();
void updateLCD(int percentage, SoilState state);
void printSerialTelemetry(int percentage, SoilState state, bool pumpActive);

// -----------------------------------------------------------------------------
// GPIO PIN CONFIGURATION & INITIALIZATION
// -----------------------------------------------------------------------------
/**
 * @brief Configures input/output modes and sets safe initial default states for all GPIO pins.
 * 
 * =============================================================================
 * ATmega328P Microcontroller Architecture & Register Mapping:
 * =============================================================================
 * - Soil Sensor (Potentiometer) : Analog Pin A0 -> PORTC Pin 0 (PC0 / ADC0)
 *   * Direction: INPUT (High-Impedance / Tri-state)
 *   * Bare-metal equivalent: DDRC &= ~(1 << DDC0);
 * 
 * - Relay Module (Pump Control) : Digital Pin 7  -> PORTD Pin 7 (PD7)
 *   * Direction: OUTPUT (Driven LOW for safe inactive boot state)
 *   * Bare-metal equivalent: DDRD |= (1 << DDD7); PORTD &= ~(1 << PORTD7);
 * 
 * - Water Pump Indicator LED    : Digital Pin 8  -> PORTB Pin 0 (PB0)
 *   * Direction: OUTPUT (Driven LOW / OFF)
 *   * Bare-metal equivalent: DDRB |= (1 << DDB0); PORTB &= ~(1 << PORTB0);
 * 
 * - Active Buzzer (Alert)       : Digital Pin 9  -> PORTB Pin 1 (PB1)
 *   * Direction: OUTPUT (Driven LOW / OFF)
 *   * Bare-metal equivalent: DDRB |= (1 << DDB1); PORTB &= ~(1 << PORTB1);
 * 
 * - I2C Bus (LCD Display)       : Pins A4 (SDA), A5 (SCL) -> PC4 (SDA) / PC5 (SCL)
 *   * Managed by hardware Two-Wire Interface (TWI) via Wire.h / lcd.init()
 * =============================================================================
 */
void configureGPIOPins() {
  // 1. Configure Pin Data Direction
  pinMode(PIN_SOIL_SENSOR, INPUT);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_PUMP_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  // 2. Set safe initial default output states (all actuators deactivated)
  digitalWrite(PIN_RELAY, RELAY_INACTIVE);
  digitalWrite(PIN_PUMP_LED, LOW);
  digitalWrite(PIN_BUZZER, LOW);
  noTone(PIN_BUZZER);

  Serial.println(F("[GPIO] Pins configured successfully:"));
  Serial.println(F("  - PIN A0 (PC0): INPUT  [Soil Sensor]"));
  Serial.println(F("  - PIN 7  (PD7): OUTPUT [Relay Control, Initial: OFF]"));
  Serial.println(F("  - PIN 8  (PB0): OUTPUT [Pump LED,      Initial: OFF]"));
  Serial.println(F("  - PIN 9  (PB1): OUTPUT [Buzzer,        Initial: OFF]"));
}

// -----------------------------------------------------------------------------
// ARDUINO SETUP
// -----------------------------------------------------------------------------
void setup() {
  // Initialize Serial Communication for debugging and monitoring
  Serial.begin(9600);
  Serial.println(F("=================================================="));
  Serial.println(F("    SMART IRRIGATION CONTROL SYSTEM INITIALIZING  "));
  Serial.println(F("=================================================="));

  // Configure and initialize all GPIO pins
  configureGPIOPins();

  // Initialize I2C LCD Display
  lcd.init();
  lcd.backlight();
  lcd.clear();

  // Show required startup splash screen for 2.0 seconds
  displaySplashScreen();

  // Clear splash screen
  lcd.clear();

  // Initialize baseline reading and refresh display
  currentMoisturePercent = readMoisturePercentage();
  currentState           = evaluateSoilState(currentMoisturePercent);
  previousState          = currentState;
  controlActuators(currentState);
  updateLCD(currentMoisturePercent, currentState);

  Serial.println(F("[SYSTEM READY] Commencing real-time telemetry monitoring...\n"));
  printSerialTelemetry(currentMoisturePercent, currentState, isPumpRunning);
  lastSerialLogTime = millis();
}

// -----------------------------------------------------------------------------
// MAIN SYSTEM LOOP
// -----------------------------------------------------------------------------
void loop() {
  unsigned long currentMillis = millis();

  // 1. Acquire sensor telemetry and convert to percentage
  currentMoisturePercent = readMoisturePercentage();

  // 2. Classify soil moisture state
  currentState = evaluateSoilState(currentMoisturePercent);

  // 3. Trigger buzzer alert upon transition into DRY state
  handleBuzzerAlert(currentState);

  // 4. Update non-blocking buzzer turn-off timing
  updateBuzzerTiming();

  // 5. Actuate Relay and Water Pump Indicator LED
  controlActuators(currentState);

  // 6. Refresh LCD with flicker-free formatted telemetry
  updateLCD(currentMoisturePercent, currentState);

  // 7. Output telemetry to Serial Monitor approximately every 1 second
  if (currentMillis - lastSerialLogTime >= SERIAL_LOG_INTERVAL_MS) {
    lastSerialLogTime = currentMillis;
    printSerialTelemetry(currentMoisturePercent, currentState, isPumpRunning);
  }

  // Preserve state for edge-triggered alert detection
  previousState = currentState;

  // Short processing yield to ensure rock-solid simulation loop
  delay(50);
}

// -----------------------------------------------------------------------------
// SPLASH SCREEN DISPLAY
// -----------------------------------------------------------------------------
void displaySplashScreen() {
  lcd.setCursor(0, 0);
  lcd.print(F("     SMART      "));
  lcd.setCursor(0, 1);
  lcd.print(F("   IRRIGATION   "));

  Serial.println(F("[DISPLAY] Showing splash screen..."));
  delay(2000); // 2-second splash screen as required
}

// -----------------------------------------------------------------------------
// SENSOR ACQUISITION & PERCENTAGE CONVERSION
// -----------------------------------------------------------------------------
int readMoisturePercentage() {
  // Read analog value (0 - 1023)
  int rawAnalog = analogRead(PIN_SOIL_SENSOR);
  
  // Map analog range (0–1023) to soil moisture percentage (0%–100%)
  // Potentiometer Left = 0% (Dry), Middle = ~50% (Moist), Right = 100% (Wet)
  int percentage = map(rawAnalog, 0, 1023, 0, 100);
  percentage = constrain(percentage, 0, 100);

  return percentage;
}

// -----------------------------------------------------------------------------
// SOIL STATE EVALUATION
// -----------------------------------------------------------------------------
SoilState evaluateSoilState(int percentage) {
  if (percentage <= THRESHOLD_DRY_MAX) {
    return SOIL_DRY;
  } else if (percentage <= THRESHOLD_MOIST_MAX) {
    return SOIL_MOIST;
  } else {
    return SOIL_WET;
  }
}

// -----------------------------------------------------------------------------
// ACTUATOR CONTROL (RELAY & PUMP STATUS LED)
// -----------------------------------------------------------------------------
void controlActuators(SoilState state) {
  if (state == SOIL_DRY) {
    // Soil is Dry: Energize relay and illuminate pump LED
    digitalWrite(PIN_RELAY, RELAY_ACTIVE);
    digitalWrite(PIN_PUMP_LED, HIGH);
    isPumpRunning = true;
  } else {
    // Soil is Moist or Wet: De-energize relay and turn off pump LED
    digitalWrite(PIN_RELAY, RELAY_INACTIVE);
    digitalWrite(PIN_PUMP_LED, LOW);
    isPumpRunning = false;
  }
}

// -----------------------------------------------------------------------------
// BUZZER ALERT LOGIC (Edge-triggered, avoids continuous annoying buzzing)
// -----------------------------------------------------------------------------
void handleBuzzerAlert(SoilState newState) {
  // Trigger a crisp audible alert only when transitioning into the DRY state
  if (newState == SOIL_DRY && previousState != SOIL_DRY) {
    tone(PIN_BUZZER, BUZZER_TONE_FREQ_HZ);
    isBuzzerActive = true;
    buzzerStartTime = millis();
    Serial.println(F("[ALERT] Soil is DRY! Pump started, buzzer alert triggered."));
  }
}

// -----------------------------------------------------------------------------
// NON-BLOCKING BUZZER TIMING
// -----------------------------------------------------------------------------
void updateBuzzerTiming() {
  if (isBuzzerActive) {
    if (millis() - buzzerStartTime >= BUZZER_BEEP_DURATION_MS) {
      noTone(PIN_BUZZER);
      digitalWrite(PIN_BUZZER, LOW);
      isBuzzerActive = false;
    }
  }
}

// -----------------------------------------------------------------------------
// FLICKER-FREE LCD TELEMETRY UPDATE
// -----------------------------------------------------------------------------
void updateLCD(int percentage, SoilState state) {
  // Line 1: Moisture: XX%
  // Using fixed buffer to avoid lcd.clear() flicker
  char line1[17];
  snprintf(line1, sizeof(line1), "Moisture: %3d%%  ", percentage);
  lcd.setCursor(0, 0);
  lcd.print(line1);

  // Line 2: Status: DRY / Status: MOIST / Status: WET
  lcd.setCursor(0, 1);
  switch (state) {
    case SOIL_DRY:
      lcd.print(F("Status: DRY     "));
      break;
    case SOIL_MOIST:
      lcd.print(F("Status: MOIST   "));
      break;
    case SOIL_WET:
      lcd.print(F("Status: WET     "));
      break;
  }
}

// -----------------------------------------------------------------------------
// SERIAL TELEMETRY LOGGING (Matches exact required specification)
// -----------------------------------------------------------------------------
void printSerialTelemetry(int percentage, SoilState state, bool pumpActive) {
  Serial.print(F("Moisture : "));
  Serial.print(percentage);
  Serial.println(F("%"));

  Serial.print(F("Status   : "));
  switch (state) {
    case SOIL_DRY:
      Serial.println(F("DRY"));
      break;
    case SOIL_MOIST:
      Serial.println(F("MOIST"));
      break;
    case SOIL_WET:
      Serial.println(F("WET"));
      break;
  }

  Serial.print(F("Relay    : "));
  Serial.println(pumpActive ? F("ON") : F("OFF"));

  Serial.print(F("LED      : "));
  Serial.println(pumpActive ? F("ON") : F("OFF"));

  Serial.println(F("--------------------"));
}
