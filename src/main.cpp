/*
I-V curve analyzer for low-power energies
Array of resistors and MUX through 15 of them, and 1 open circuit
Display on OLED screen.

Features:
* Real-time I-V curve display with adjustable scaling
* Maximum Power Point (MPP) tracking
* Maximum current and voltage measurements
* Interactive display with button control
* Serial data output mode

Hardware Setup:
* ESP32-S3 Mini board
* SSD1306 OLED display (128x64)
* 4051 Multiplexer for resistor array
* Two potentiometers for X/Y scaling
* Button for serial mode toggle
*/

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Adafruit_ThinkInk.h"
#include "hackteria_logo.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "config.h"
#include "board_config.h"
#include "IVserver.h"
#include <Adafruit_ADS1X15.h>
#include "ThingSpeak.h"
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>

// Display Configuration
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define SCREEN_ADDRESS 0x3C // OLED display I2C address

// Declaration for displays
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);
ThinkInk_290_Tricolor_Z10 eink(EPD_DC, EPD_RESET, EPD_CS, SRAM_CS, EPD_BUSY, &SPI);
Adafruit_ADS1115 ads;  // Create an ADS1115 instance

// Global IV Server instance
//IVserver server(80);  // Create an IV server on port 80

// ThingSpeak client
WiFiClient client;

// Define LED states
#define LED_IDLE      0  // Blue
#define LED_MEASURING 1  // Yellow
#define LED_SENDING   2  // Purple
#define LED_SUCCESS   3  // Green
#define LED_ERROR     4  // Red

// Initialize NeoPixel
Adafruit_NeoPixel pixel(1, PIN_RGBLED, NEO_RGB + NEO_KHZ800);

// Function declarations
void initOLED();
void initEink();
void drawOnEink();
void drawOnEinkBackground();
void drawBackground();
void drawIVline();
void showLogo_hackteria();
void showBootScreen();
void showCreditsScreen();
void setupNetwork();
void handleIVserver();
void setupIVserver();
int readMux(int channel);
void printMeasurements();
void drawDashedLine(int x0, int y0, int x1, int y1, uint16_t color, int dashLength, int gapLength);
void setupThingSpeak();
void sendToThingSpeak(float voc, float isc, float mpp, float ff);
void setupLED();
void setLEDState(uint8_t state);

// Pin assignments from board configuration
int s0 = PIN_MUX_S0;      // MUX control pin S0
int s1 = PIN_MUX_S1;      // MUX control pin S1
int s2 = PIN_MUX_S2;      // MUX control pin S2
int s3 = PIN_MUX_S3;      // MUX control pin S3
int SIG_pin = PIN_SIG;    // ADC input for voltage measurement
int POTY_pin = PIN_POT_Y; // ADC input for Y-axis scaling
int POTX_pin = PIN_POT_X; // ADC input for X-axis scaling
int BUTT_pin = PIN_BUTTON;// Digital input for mode button
int TPI_pin = PIN_TPI;    // Test Point Input control

// Hardware Variables
#ifdef MUX_ADG706
const int resistorCorrection = 64;  // Correction Value for ADG706
#endif
#ifdef MUX_4051
const int resistorCorrection = 25;  // For old Mux
#endif

// Measurement and display variables
int overlay = 0; // Display overlay state
int scaleY;      // Y-axis (current) scaling factor from potentiometer
int scaleX;      // X-axis (voltage) scaling factor from potentiometer
const int average = 1;           // Number of readings to average
const int delayLoop = 10;        // Main loop delay
const int delayAveraging = 4;    // Delay between ADC readings for averaging
const int delayMuxSwitch = 2;    // Delay after MUX switching for voltage stabilization
const int delayWebUpdate = 2000;  // Web server update delay in milliseconds

// Interface Control Variables
bool serialMode = 0;          // Serial output mode flag
bool buttonState = false;     // Current button state
bool lastButtonState = false; // Previous button state
unsigned long lastPressTime = 0;      // Last button press timestamp
const unsigned long recentThreshold = 2000; // Button press timeout (ms)

// Global Measurement Variables
float vocValues[20];   // Array to store voltage measurements
float icalValues[20];  // Array to store calculated currents
float powValues[20];   // Array to store power values
float prevVocValues[20];   // Array to store previous voltage measurements
float prevIcalValues[20];  // Array to store previous current measurements
float prevPowValues[20];   // Array to store previous power values
float maxPower = 0;    // Maximum power point tracking
float maxCurrent = 0;  // Maximum current tracking
float maxVoltage = 0;  // Maximum voltage tracking
float prevMaxPower = 0;    // Previous maximum power
float prevMaxCurrent = 0;  // Previous maximum current
float prevMaxVoltage = 0;  // Previous maximum voltage
bool hasPreviousMeasurement = false;  // Flag to track if we have a previous measurement
int mppIndex = 0;      // Index of maximum power point
int prevMppIndex = 0;  // Previous maximum power point index

// Resistor array values in ohms (measured through MUX)
int resistorValues[20]{
  1000000,
  19910,
  8200,
  4310,
  2230,
  1052,
  802,
  530,
  390,
  282,
  212,
  165,
  141,
  113,
  93,
  81,
  81,
  81,
  81,
  81
};

// Add global variable for button press detection
volatile bool buttonPressed = false;

void setup() {
  Serial.begin(115200);
  delay(500);  // Give more time for Serial to initialize
  //while (!Serial) { delay(10); }  // Wait for Serial to be ready
  Serial.println("=========== Initializing I-V Scanner ===========");
  Serial.println("");
  delay(100);

  // Initialize MUX control pins
  pinMode(s0, OUTPUT);
  pinMode(s1, OUTPUT);
  pinMode(s2, OUTPUT);
  pinMode(s3, OUTPUT);  // Added s3 pin initialization
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  
  // Initialize arrays to zero
  memset(vocValues, 0, sizeof(vocValues));
  memset(icalValues, 0, sizeof(icalValues));
  memset(powValues, 0, sizeof(powValues));
  memset(prevVocValues, 0, sizeof(prevVocValues));
  memset(prevIcalValues, 0, sizeof(prevIcalValues));
  memset(prevPowValues, 0, sizeof(prevPowValues));
  
  // Initialize OLED
  initOLED();

  // Initialize ADS1115
  #ifdef hasADS1115
  ads.begin(0x48);
  ads.setGain(GAIN_ONE);    // 1x gain   +/- 4.096V  1 bit = 0.125mV
  #endif

  // Initialize NeoPixel
  setupLED();
  setLEDState(LED_IDLE);

  // Show logo
  showLogo_hackteria();
  delay(1000);
  
  Serial.println("");
  Serial.println("=========== Connecting to Wi-fi ===========");

  // Initialize network and server
  setupNetwork();
  if (WiFi.status() == WL_CONNECTED) {
    setupIVserver();
    setupThingSpeak();
  }
  
  Serial.println("");
  Serial.println("=========== IV Server started ===========");
  Serial.println("");

  // Show bootscreen
  showBootScreen();
  delay(10);

  // Initialize E-ink
  #ifdef hasEink
  initEink();
  #endif
  
  // Show credits screen
  showCreditsScreen();
  delay(1000);
  
  Serial.println("");
  Serial.println("=========== Starting I-V Scanner ===========");
  Serial.println("");

  // Clear display and draw background
  oled.clearDisplay();
  drawBackground();
  oled.display();
}

void loop() {
  // Handle IV server requests
  if (WiFi.status() == WL_CONNECTED) {
    handleIVserver();
  }
  
  // Read scaling potentiometers
  scaleX = analogReadMilliVolts(POTX_pin);
  scaleY = analogReadMilliVolts(POTY_pin);
  digitalWrite(TPI_pin, 0);
  
  // Take continuous measurements
  digitalWrite(s0, 0);
  digitalWrite(s1, 0);
  digitalWrite(s2, 0);
  digitalWrite(s3, 0);
  digitalWrite(TPI_pin, 0);
  delay(50);

  // Reset max values for new measurement
  maxPower = 0;
  maxCurrent = 0;
  maxVoltage = 0;
  mppIndex = 0;

  // Complete full measurement cycle
  #ifdef BOARD_ESP32S3_MINI
  for (int i = 0; i < 16; i++) {
    vocValues[i] = readMux(i);  // Get voltage directly from return value
    icalValues[i] = vocValues[i] / (resistorValues[i] - resistorCorrection);  // Calculate current in mA
    powValues[i] = vocValues[i] * icalValues[i];  // Calculate power
    
    // Update maximum values
    if (powValues[i] > maxPower) {
      maxPower = powValues[i];
      mppIndex = i;
    }
    
    // Find absolute maximum current and voltage
    if (icalValues[i] > maxCurrent) {
      maxCurrent = icalValues[i];
    }
    if (vocValues[i] > maxVoltage) {
      maxVoltage = vocValues[i];
    }
    
    delay(delayAveraging);
  }
  #endif

#ifdef BOARD_ESP32C3_SUPER_MINI
  for (int i = 0; i < 14; i++) {
    vocValues[i] = readMux(i);  // Get voltage directly from return value
    icalValues[i] = vocValues[i] / (resistorValues[i] - resistorCorrection);  // Calculate current in mA
    powValues[i] = vocValues[i] * icalValues[i];  // Calculate power
    
    // Update maximum values
    if (powValues[i] > maxPower) {
      maxPower = powValues[i];
      mppIndex = i;
    }
    
    // Find absolute maximum current and voltage
    if (icalValues[i] > maxCurrent) {
      maxCurrent = icalValues[i];
    }
    if (vocValues[i] > maxVoltage) {
      maxVoltage = vocValues[i];
    }
    
    delay(delayAveraging);
  }
#endif

  // Update IV server with new measurement data
  static unsigned long lastWebUpdate = 0;
  if (millis() - lastWebUpdate >= delayWebUpdate) {
    IVData data;
    data.maxPower = maxPower;
    data.maxCurrent = icalValues[mppIndex];  // Use current at MPP
    data.maxVoltage = vocValues[mppIndex];   // Use voltage at MPP
    data.numPoints = 16;
    
    // Copy array values
    for(int i = 0; i < 16; i++) {
      data.vocValues[i] = vocValues[i];
      data.icalValues[i] = icalValues[i];
      data.powValues[i] = powValues[i];
    }
    if (WiFi.status() == WL_CONNECTED) {
      updateIVserverData(data);
    }
    lastWebUpdate = millis();
  }
  
  // Update OLED display continuously
  drawIVline();

  // Handle button press
  if (buttonPressed) {
    setLEDState(LED_MEASURING);
    Serial.println("Button pressed!");
    
    // First draw the e-ink display with current measurement in red and previous in black
    #ifdef hasEink
    drawOnEink();
    #endif
    printMeasurements();
    
    // Only after displaying, store current as previous for next time
    memcpy(prevVocValues, vocValues, sizeof(vocValues));
    memcpy(prevIcalValues, icalValues, sizeof(icalValues));
    memcpy(prevPowValues, powValues, sizeof(powValues));
    prevMaxPower = maxPower;
    prevMaxCurrent = maxCurrent;
    prevMaxVoltage = maxVoltage;
    prevMppIndex = mppIndex;
    hasPreviousMeasurement = true;

    // Send data to ThingSpeak if WiFi is connected
    if (WiFi.status() == WL_CONNECTED) {
      setLEDState(LED_SENDING);
      // Calculate values to send
      float voc = vocValues[0];          // Open circuit voltage in mV
      float isc = icalValues[15];        // Short circuit current in mA
      float mpp = maxPower;              // Maximum power in mW
      float ff = (maxPower / ((vocValues[0]/1000.0) * icalValues[15])) / 10;  // Fill factor (already calculated)
      
      // Send to ThingSpeak (values already in correct units)
      sendToThingSpeak(voc, isc, mpp, ff);
      setLEDState(LED_SUCCESS);
      delay(1000);  // Show success for 1 second
    } else {
      setLEDState(LED_ERROR);
      delay(1000);  // Show error for 1 second
    }
    
    setLEDState(LED_IDLE);
    buttonPressed = false;  // Reset flag
    delay(200);  // Debounce delay
  }
  
  delay(delayLoop);
}

int readMux(int channel) {
  int sig = 0;
  float multiplier = 0.125F; // 4.096V range = 0.125mV per bit
  int16_t adc0;
  
  // Set the channel on MUX
  digitalWrite(s0, bitRead(channel, 0));
  digitalWrite(s1, bitRead(channel, 1));
  digitalWrite(s2, bitRead(channel, 2));
  digitalWrite(s3, bitRead(channel, 3));
  delay(delayMuxSwitch);  // Delay for voltage stabilization after switching
  
  // Take multiple readings and average
  for(int i = 0; i < average; i++) {
    // Check for button press during measurement
    if (digitalRead(PIN_BUTTON) == LOW) {  // Button pressed
      buttonPressed = true;
    }


    #ifdef hasADS1115
    adc0 = ads.readADC_SingleEnded(0);
    sig += adc0 * multiplier; // read from ADC1115
    #endif
    #ifdef hasESPADC
    sig += analogReadMilliVolts(SIG_pin);  // Multiply by 2 to account for voltage divider
    #endif
    
    delay(delayAveraging);
  }
  
  return sig / average;
}

void printMeasurements() {
  Serial.println("    ");
  Serial.println("=========== I calculated ===========");
  for(int i = 0; i < 16; i ++){
    Serial.println(icalValues[i]);
  }

  Serial.println("=========== Power calculated ===========");
  for(int i = 0; i < 16; i ++){
    Serial.println(powValues[i]);
  }
  Serial.println("    ");
  Serial.println("=========== V meas===========");
  for(int i = 0; i < 16; i ++){
    Serial.println(vocValues[i]);
  }  

  Serial.println("=========== MPP ===========");
  Serial.print("MPP Index: ");
  Serial.println(mppIndex);
  Serial.print("Voltage at MPP: ");
  Serial.print(vocValues[mppIndex]);
  Serial.println(" mV");
  Serial.print("Current at MPP: ");
  Serial.print(icalValues[mppIndex]);
  Serial.println(" mA");
  Serial.print("Power at MPP: ");
  Serial.print(powValues[mppIndex]);
  Serial.println(" uW");
  Serial.println("=========== DONE ===========");
  delay(10);
}

void initOLED() {
  // Initialize OLED with I2C
  Serial.println("Initializing OLED display");
  Wire.begin(SDA_PIN, SCL_PIN);
  if(!oled.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS, false, false)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);  // Don't proceed, loop forever
  }
  oled.clearDisplay();
  oled.display();
  delay(50);
  Serial.println("OLED display initialized");
}

void initEink() {
  // Initialize E-ink with proper delays
  Serial.println("Initializing e-ink display");
  delay(100);
  eink.begin(THINKINK_TRICOLOR);
  delay(100);
  
  // Clear the buffer
  eink.clearBuffer();
  
  // Draw the initial background (grid and labels only)
  drawOnEinkBackground();
  
  Serial.println("E-ink display initialized");
}

void drawOnEink() {
  eink.powerUp();    // Power up the display
  delay(100);        // Wait for power up
  
  eink.clearBuffer();
  delay(50);         // Wait after clear
  
  // Draw frame for I-V curve (160x100 pixels)
  eink.drawRect(20, 10, 160, 100, EPD_BLACK);
  
  // Draw grid lines - vertical (8 sections of 20 pixels each)
  for (int i = 0; i < 160; i += 20) {  
    for (int j = 10; j < 110; j += 2) {   
      if (i > 0) eink.drawPixel(i + 20, j, EPD_BLACK);  
    }
  }
  
  // Draw grid lines - horizontal (10 sections)
  for (int i = 0; i < 100; i += 20) {    
    for (int j = 20; j < 180; j += 2) {  
      if (i > 0) eink.drawPixel(j, i + 10, EPD_BLACK);  
    }
  }

  // Draw X-axis label with proper rotation
  eink.setTextSize(1);
  eink.setTextColor(EPD_RED);
  eink.setCursor(70, 116);
  eink.print("Voltage (V)");
  
  // Draw Y-axis label with proper rotation
  uint8_t oldRotation = eink.getRotation();
  eink.setRotation(3);  
  eink.setCursor(30, 6);  
  eink.print("Current (mA)");
  eink.setRotation(oldRotation);  
  
  // Calculate scaling factors ONCE using max values from both measurements
  float scaleX_auto = 160.0 / (max(maxVoltage, hasPreviousMeasurement ? prevMaxVoltage : 0) * 1.1);  
  float scaleY_auto = 100.0 / (max(maxCurrent, hasPreviousMeasurement ? prevMaxCurrent : 0) * 1.1);   
  float scaleY_power = 47.5 / (max(maxPower, hasPreviousMeasurement ? prevMaxPower : 0) * 1.1);    
  
  // Calculate maximum values for axis labels
  float maxVoltage = 160.0 / scaleX_auto;  // 160 pixels is full scale X
  float maxCurrent = 100.0 / scaleY_auto;  // 100 pixels is full scale Y
  
  // Format the values with 2 decimal places
  char voltageStr[10];
  char currentStr[10];
  snprintf(voltageStr, sizeof(voltageStr), "%.1fV", maxVoltage/1000);  // Format voltage with unit
  snprintf(currentStr, sizeof(currentStr), "%.1fmA", maxCurrent);  // Convert to mA and format with 2 decimals
  
  // Draw axis max values
  eink.setTextSize(1);
  eink.setTextColor(EPD_BLACK);
  
  // Voltage max value (X-axis) - at the end of X axis
  eink.setCursor(170, 116);  // Position just right of the graph, aligned with axis
  eink.print(voltageStr);
  
  // Current max value (Y-axis) - at the top of Y axis
  eink.setCursor(8, 2);  // Position just left of the graph, aligned with top
  eink.print(currentStr);
  
  // Only plot previous measurement if we have one
  if (hasPreviousMeasurement) {
    // First draw the previous measurement in red (without dots)
    for(int i = 0; i < 16; i++) {
      int x = 20 + (int)(prevVocValues[i] * scaleX_auto);
      int y = 110 - (int)(prevIcalValues[i] * scaleY_auto);
      x = constrain(x, 20, 180);
      y = constrain(y, 10, 110);
      if (i == 0) {
        eink.drawPixel(x, y, EPD_RED);
      } else {
        int x_prev = 20 + (int)(prevVocValues[i-1] * scaleX_auto);
        int y_prev = 110 - (int)(prevIcalValues[i-1] * scaleY_auto);
        x_prev = constrain(x_prev, 20, 180);
        y_prev = constrain(y_prev, 10, 110);
        eink.drawLine(x_prev, y_prev, x, y, EPD_RED);
      }
    }
  }

  // Then plot the current measurement in red with dots
  for(int i = 0; i < 16; i++) {
    int x = 20 + (int)(vocValues[i] * scaleX_auto);
    int y = 110 - (int)(icalValues[i] * scaleY_auto);
    x = constrain(x, 20, 180);
    y = constrain(y, 10, 110);
    if (i == 0) {
      eink.drawPixel(x, y, EPD_RED);
    } else {
      int x_prev = 20 + (int)(vocValues[i-1] * scaleX_auto);
      int y_prev = 110 - (int)(icalValues[i-1] * scaleY_auto);
      x_prev = constrain(x_prev, 20, 180);
      y_prev = constrain(y_prev, 10, 110);
      eink.drawLine(x_prev, y_prev, x, y, EPD_RED);
    }
    // Add dots at each measurement point
    eink.fillCircle(x, y, 2, EPD_RED);
  }

  // Add MPP dot on the I-V curve (current measurement)
  if (maxPower > 0 && mppIndex >= 0 && mppIndex < 16) {
    int mpx = 20 + (int)(vocValues[mppIndex] * scaleX_auto);
    int mpy = 110 - (int)(icalValues[mppIndex] * scaleY_auto);
    mpx = constrain(mpx, 20, 180);
    mpy = constrain(mpy, 10, 110);
    eink.fillCircle(mpx, mpy, 4, EPD_RED);  
  }
  
  // Draw previous power curve in black
  if (hasPreviousMeasurement) {
    // Draw from high V to low V (index 0 to 15)
    for(int i = 15; i > 0; i--) {  
      float prev_power1 = prevVocValues[i] * prevIcalValues[i];
      float prev_power2 = prevVocValues[i-1] * prevIcalValues[i-1];
      
      int x1 = 20 + (int)(prevVocValues[i] * scaleX_auto);
      int y1 = 110 - (int)(prev_power1 * scaleY_power);
      int x2 = 20 + (int)(prevVocValues[i-1] * scaleX_auto);
      int y2 = 110 - (int)(prev_power2 * scaleY_power);
      
      x1 = constrain(x1, 20, 180);
      y1 = constrain(y1, 10, 110);
      x2 = constrain(x2, 20, 180);
      y2 = constrain(y2, 10, 110);
      
      drawDashedLine(x1, y1, x2, y2, EPD_BLACK, 2, 2);  
    }
    
    // Connect lowest V point (index 15) to origin for previous curve
    float prev_last_power = prevVocValues[15] * prevIcalValues[15];
    int x1 = 20 + (int)(prevVocValues[15] * scaleX_auto);
    int y1 = 110 - (int)(prev_last_power * scaleY_power);
    x1 = constrain(x1, 20, 180);
    y1 = constrain(y1, 10, 110);
    drawDashedLine(x1, y1, 20, 110, EPD_BLACK, 2, 2);  
  }

  // Draw power curve (current measurement in black)
  // Draw from high V to low V (index 0 to 15)
  for(int i = 15; i > 0; i--) {  
    float power1 = vocValues[i] * icalValues[i];
    float power2 = vocValues[i-1] * icalValues[i-1];
    
    int x1 = 20 + (int)(vocValues[i] * scaleX_auto);
    int y1 = 110 - (int)(power1 * scaleY_power);
    int x2 = 20 + (int)(vocValues[i-1] * scaleX_auto);
    int y2 = 110 - (int)(power2 * scaleY_power);
    
    x1 = constrain(x1, 20, 180);
    y1 = constrain(y1, 10, 110);
    x2 = constrain(x2, 20, 180);
    y2 = constrain(y2, 10, 110);
    
    eink.drawLine(x1, y1, x2, y2, EPD_BLACK);  
  }

  // Connect lowest V point (index 15) to origin for current curve
  float last_power = vocValues[15] * icalValues[15];
  int x1 = 20 + (int)(vocValues[15] * scaleX_auto);
  int y1 = 110 - (int)(last_power * scaleY_power);
  x1 = constrain(x1, 20, 180);
  y1 = constrain(y1, 10, 110);
  eink.drawLine(x1, y1, 20, 110, EPD_BLACK);  

  // Add MPP dot for current power curve
  if (maxPower > 0 && mppIndex >= 0 && mppIndex < 16) {
    float mpp_power = vocValues[mppIndex] * icalValues[mppIndex];
    int mpx = 20 + (int)(vocValues[mppIndex] * scaleX_auto);
    int mpy = 110 - (int)(mpp_power * scaleY_power);
    mpx = constrain(mpx, 20, 180);
    mpy = constrain(mpy, 10, 110);
    eink.fillCircle(mpx, mpy, 2, EPD_BLACK);
    
  }
  
  // Add measurements text
  eink.setTextColor(EPD_RED);
  eink.setCursor(184, 40);
  eink.print("MPP:");
  eink.setTextSize(3);
  
  // Format MPP value based on its range
  float mppValue = maxPower/1000;  // Convert to mW
  if (mppValue < 10) {
    // Small values: show 0.00 format
    eink.print(mppValue, 2);
  } else if (mppValue < 100) {
    // Medium values: show 00.0 format
    eink.print(mppValue, 1);
  } else {
    // Large values: show 000 format
    eink.print((int)mppValue);
  }
  
  eink.setTextSize(1);
  eink.setCursor(280, 40);
  eink.print("mW");
  
  eink.setTextSize(1);
  eink.setTextColor(EPD_BLACK);
  eink.setCursor(184, 70);
  eink.print("Isc:");
  eink.print(icalValues[15], 2);
  eink.print("mA");
  
  eink.setCursor(184, 82);
  eink.print("Voc:");
  eink.print(vocValues[0]/1000.0, 2);
  eink.print("V");
  
  // Calculate and display Fill Factor (FF)
  float theoreticalMax = (vocValues[0]/1000.0) * icalValues[15];  // Voc * Isc
  float fillFactor = (maxPower / theoreticalMax) / 10;  // Convert to percentage
  
  eink.setCursor(184, 94);
  eink.setTextColor(EPD_BLACK);
  eink.print("FF :");
  eink.print((int)fillFactor);
  eink.print("%");
  
  // Add title in red
  eink.setTextSize(2);
  eink.setTextColor(EPD_RED);
  eink.setCursor(184, 6);
  eink.print("I-V Curve");
  
  // Add note below title
  eink.setTextSize(1);
  eink.setTextColor(EPD_BLACK);
  eink.setCursor(190, 25);
  eink.print("button to update");
  
  // Draw hackteria logo in bottom right corner
  eink.drawBitmap(296 - HACKTERIA_LOGO_WIDTH + 42 - 2,    // X position (2 pixels from right edge)
                  128 - HACKTERIA_LOGO_HEIGHT - 2,     // Y position (2 pixels from bottom edge)
                  hackteria_logo, HACKTERIA_LOGO_WIDTH, HACKTERIA_LOGO_HEIGHT, EPD_RED);
  
  // Give the display time to prepare for update
  delay(100);
  
  // Do a full refresh for better color quality
  eink.display();
  delay(2000);       // Wait for the display to complete its refresh
  
  eink.powerDown(); // Power down the display
  delay(1000);       // Wait for power down to complete
}

void drawOnEinkBackground() {
  eink.powerUp();    // Power up the display
  delay(100);        // Wait for power up
  
  eink.clearBuffer();
  delay(50);         // Wait after clear
  
  // Draw frame for I-V curve (160x100 pixels)
  eink.drawRect(20, 10, 160, 100, EPD_BLACK);
  
  // Draw grid lines - vertical (8 sections of 20 pixels each)
  for (int i = 0; i < 160; i += 20) {  
    for (int j = 10; j < 110; j += 2) {   
      if (i > 0) eink.drawPixel(i + 20, j, EPD_BLACK);  
    }
  }
  
  // Draw grid lines - horizontal (10 sections)
  for (int i = 0; i < 100; i += 20) {    
    for (int j = 20; j < 180; j += 2) {  
      if (i > 0) eink.drawPixel(j, i + 10, EPD_BLACK);  
    }
  }

  // Draw X-axis label with proper rotation
  eink.setTextSize(1);
  eink.setTextColor(EPD_RED);
  eink.setCursor(70, 116);
  eink.print("Voltage (V)");
  
  // Draw Y-axis label with proper rotation
  uint8_t oldRotation = eink.getRotation();
  eink.setRotation(3);  
  eink.setCursor(30, 6);  
  eink.print("Current (mA)");
  eink.setRotation(oldRotation);  
  
  // Add title in red
  eink.setTextSize(2);
  eink.setTextColor(EPD_RED);
  eink.setCursor(184, 6);
  eink.print("I-V Curve");
  
  // Add note below title
  eink.setTextSize(1);
  eink.setTextColor(EPD_BLACK);
  eink.setCursor(190, 25);
  eink.print("button to update");

  // Draw hackteria logo in bottom right corner
  eink.drawBitmap(296 - HACKTERIA_LOGO_WIDTH + 40 - 2,    // X position (2 pixels from right edge)
                  128 - HACKTERIA_LOGO_HEIGHT - 2,     // Y position (2 pixels from bottom edge)
                  hackteria_logo, HACKTERIA_LOGO_WIDTH, HACKTERIA_LOGO_HEIGHT, EPD_RED);
  
  // Give the display time to prepare for update
  delay(100);
  
  // Do a full refresh for better color quality
  eink.display();
  delay(2000);       // Wait for the display to complete its refresh
  
  eink.powerDown(); // Power down the display
  delay(100);       // Wait for power down to complete
}

void drawDashedLine(int x0, int y0, int x1, int y1, uint16_t color, int dashLength, int gapLength) {
  int dx = abs(x1 - x0);
  int dy = abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1;
  int sy = y0 < y1 ? 1 : -1;
  
  // Total length of the line
  float lineLength = sqrt(dx * dx + dy * dy);
  if (lineLength < 1) {  // If line is too short, just draw a single pixel
    eink.drawPixel(x0, y0, color);
    return;
  }
  
  // Number of segments (dash + gap)
  int totalSegmentLength = dashLength + gapLength;
  int segments = max(1, (int)(lineLength / totalSegmentLength));
  
  // Draw each dash
  for (int i = 0; i < segments; i++) {
    float startPercent = (float)(i * totalSegmentLength) / lineLength;
    float endPercent = min((float)(i * totalSegmentLength + dashLength) / lineLength, 1.0f);
    
    if (startPercent >= 1.0f) break;  // Stop if we've gone past the end
    
    int xStart = x0 + (int)((x1 - x0) * startPercent);
    int yStart = y0 + (int)((y1 - y0) * startPercent);
    int xEnd = x0 + (int)((x1 - x0) * endPercent);
    int yEnd = y0 + (int)((y1 - y0) * endPercent);
    
    // Ensure we don't draw outside the line endpoints
    if (sx > 0) {
      xEnd = min(xEnd, x1);
    } else {
      xEnd = max(xEnd, x1);
    }
    if (sy > 0) {
      yEnd = min(yEnd, y1);
    } else {
      yEnd = max(yEnd, y1);
    }
    
    eink.drawLine(xStart, yStart, xEnd, yEnd, color);
  }
}

void drawIVline()
{
  oled.clearDisplay();  // Clear the display buffer
  drawBackground();       // Redraw the background
  
  // Find maximum values for auto-scaling
  float maxV = 0.001;  // Small non-zero value to prevent division by zero
  float maxI = 0.001;  // Small non-zero value to prevent division by zero
  
  for(int i = 0; i < 16; i++) {
    if(vocValues[i] > maxV) maxV = vocValues[i];
    if(icalValues[i] > maxI) maxI = icalValues[i];
  }
  
  // Calculate scaling factors (pixels per volt/amp)
  float scaleX_auto = 89.0 / maxV;   // Scale to exactly 89 pixels (1 to 90)
  float scaleY_auto = 38.0 / maxI;   // 40 pixels for y-axis
  
  // Draw the I-V curve points and lines
  for(int i = 15; i > 0; i--) {
    int x1 = 1 + (int)(vocValues[i-1] * scaleX_auto);     // Start at x=1
    int y1 = 42 - (int)(icalValues[i-1] * scaleY_auto);
    int x2 = 1 + (int)(vocValues[i] * scaleX_auto);   // Start at x=1
    int y2 = 42 - (int)(icalValues[i] * scaleY_auto);
    
    // Ensure coordinates are within display bounds
    x1 = constrain(x1, 1, 90);    // Exactly 1-90 pixels for x
    y1 = constrain(y1, 0, 44);    // 0-44 for y
    x2 = constrain(x2, 1, 90);
    y2 = constrain(y2, 0, 44);
    
    oled.drawLine(x1, y1, x2, y2, WHITE);
    oled.drawRect(x2-1, y2-1, 3, 3, WHITE);
  }
  
  // Draw maximum power point marker
  int mpx = 1 + (int)(vocValues[mppIndex] * scaleX_auto);  // Start at x=1
  int mpy = 42 - (int)(icalValues[mppIndex] * scaleY_auto);
  mpx = constrain(mpx, 3, 88);    // Keep MPP marker within graph area
  mpy = constrain(mpy, 2, 42);
  oled.fillRect(mpx-2, mpy-2, 5, 5, WHITE);
  
  oled.display();
}

void drawBackground()
{  
  oled.clearDisplay();
  // Draw main frame for I-V curve (100x44 pixels)
  oled.drawRect(0, 0, 100, 44, WHITE);
  
  // Draw grid lines - vertical (5 sections)
  for (int i = 0; i < 100; i += 20) {  
    for (int j = 0; j < 44; j += 2) {   
      if (i > 0) oled.drawPixel(i, j, WHITE);  // Dotted line pattern
    }
  }
  
  // Draw grid lines - horizontal (4 sections)
  for (int i = 0; i < 44; i += 11) {    
    for (int j = 0; j < 100; j += 2) {  
      if (i > 0) oled.drawPixel(j, i, WHITE);  // Dotted line pattern
    }
  }

  // Draw right-side information panel
  oled.setTextSize(1);
  oled.setTextColor(WHITE);
  oled.setCursor(104, 0);
  oled.println("I-V");
  // Display short circuit current (Isc)
  oled.setCursor(104, 12);
  oled.println("Isc:");
  oled.setCursor(104, 20);
  
  // Format Isc value based on its range
  float iscValue = icalValues[15];  // Index 15 for short circuit current
  if (iscValue < 10) {
    // Small values: show 0.00 format
    oled.print(iscValue, 2);
  } else if (iscValue < 100) {
    // Medium values: show 00.0 format
    oled.print(iscValue, 1);
  } else {
    // Large values: show 000 format
    oled.print((int)iscValue);
  }
  
  // Display open circuit voltage (Voc)
  oled.setCursor(104, 30);
  oled.println("Voc:");
  oled.setCursor(104, 38);
  oled.print(vocValues[0]/1000.0, 2);  // Index 0 for open circuit voltage, convert to V

  // Display voltage and current at MPP
  oled.setTextSize(1);
  oled.setCursor(0, 56);
  oled.print("MPP: ");
  oled.print(maxPower/1000, 2);  // Actual measured MPP
  oled.print("mW FF:");
  oled.print((int)((maxPower / ((vocValues[0]/1000.0) * icalValues[15])) / 10));
  oled.println("%");
  
  oled.setCursor(0, 47);
  oled.print("V:");
  oled.print(vocValues[mppIndex]/1000, 2);  // Voltage at MPP
  oled.print("V  I:");
  oled.print(icalValues[mppIndex], 2);  // Current at MPP
  oled.print("mA");
  
  oled.display();
}

void showLogo_hackteria(){
  oled.clearDisplay(); // Make sure the display is cleared
  oled.drawBitmap(0, 0, hackteria_logo, 128, 64, WHITE);  
  oled.setTextSize(2);
  oled.setTextColor(WHITE);
  oled.setCursor(11,0);
  oled.println("HACKTERIA");
  // Update the display
  oled.display();
  delay(30);
}

void showBootScreen() {
  oled.clearDisplay();
  
  // Draw a frame
  oled.drawRect(0, 0, 128, 64, WHITE);
  
  // Title
  oled.setTextSize(2);
  oled.setTextColor(WHITE);
  oled.setCursor(10, 4);
  oled.println("I-V CURVE");
  oled.setCursor(15, 22);
  oled.println("SCANNER");
  
  // Smaller text at bottom - centered
  oled.setTextSize(1);
  // "Solar Cell Analyzer" is 17 characters * 6 pixels = 102 pixels
  oled.setCursor((128 - 102)/2 - 4, 45);  // Shifted 4 pixels left
  oled.println("Solar Cell Analyzer");
  // "Version 1.0 2024" is 15 characters * 6 pixels = 90 pixels
  oled.setCursor((128 - 90)/2 - 4, 55);   // Shifted 4 pixels left
  oled.println("Version 1.0 2025");
  
  oled.display();
}

void showCreditsScreen() {
  oled.clearDisplay();
  
  // Draw a frame
  oled.drawRect(0, 0, 128, 64, WHITE);
  
  // Main credit in large text
  oled.setTextSize(2);
  oled.setTextColor(WHITE);
  oled.setCursor(6, 16);  // Moved left from center position
  oled.println("by dusjagr");
  
  // Organization name in small text, centered in three lines
  oled.setTextSize(1);
  // "Center for" is 10 chars * 6 pixels = 60 pixels
  oled.setCursor((128 - 60)/2, 38);
  oled.println("Center for");
  // "Alternative" is 11 chars * 6 pixels = 66 pixels
  oled.setCursor((128 - 66)/2, 46);
  oled.println("Alternative");
  // "Coconut Research" is 15 chars * 6 pixels = 90 pixels
  oled.setCursor((128 - 90)/2, 54);
  oled.println("Coconut Research");
  
  oled.display();
}

void setupNetwork() {
  const int MAX_ATTEMPTS_PER_NETWORK = 3;    // Only try 3 times per network
  const int WIFI_DELAY = 3333;        // ~3.3 seconds between attempts
  bool wifiConnected = false;
  
  Serial.println();
  Serial.println("Starting WiFi connection attempts");
  
  // Try each network in sequence
  for (int network = 0; network < WIFI_NETWORK_COUNT && !wifiConnected; network++) {
    int attempts = 0;
    
    Serial.printf("\nTrying network: %s\n", WIFI_NETWORKS[network].ssid);
    
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setCursor(0,0);
    oled.printf("Trying WiFi:\n%s", WIFI_NETWORKS[network].ssid);
    oled.display();
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_NETWORKS[network].ssid, WIFI_NETWORKS[network].password);
    
    // Try to connect to this network
    while (WiFi.status() != WL_CONNECTED && attempts < MAX_ATTEMPTS_PER_NETWORK) {
      delay(WIFI_DELAY);
      Serial.print(".");
      oled.print(".");
      oled.display();
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      Serial.printf("\nConnected to %s\n", WIFI_NETWORKS[network].ssid);
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      
      // Initialize mDNS if connected
      if (MDNS.begin("ivcurve")) {
        Serial.println("mDNS responder started");
        Serial.println("You can access the server at: http://ivcurve.local");
      }
      
      oled.clearDisplay();
      oled.setCursor(0,0);
      oled.println("Connected!");
      oled.println(WiFi.localIP());
      oled.println("ivcurve.local");
      oled.display();
      delay(2000);
    }
  }
  
  if (!wifiConnected) {
    Serial.println("\nFailed to connect to any network - continuing without WiFi");
    oled.clearDisplay();
    oled.setCursor(0,0);
    oled.println("No WiFi");
    oled.println("Running offline");
    oled.display();
    delay(2000);
  }
}

void setupThingSpeak() {
  ThingSpeak.begin(client);
}

// Function to create array string (comma separated values)
String createArrayString(float values[], int count, int decimals = 3) {
  String result = "";
  for(int i = 0; i < count; i++) {
    if (i > 0) result += ",";
    if (decimals == 0) {
      result += String((int)round(values[i]));  // Cast to int to remove decimal point
    } else {
      result += String(values[i], decimals);
    }
  }
  return result;
}

void sendToThingSpeak(float voc, float isc, float mpp, float ff) {
  // Set the main measurement fields
  ThingSpeak.setField(1, voc);    // Open Circuit Voltage
  ThingSpeak.setField(2, isc);    // Short Circuit Current
  ThingSpeak.setField(3, mpp);    // Maximum Power Point
  ThingSpeak.setField(4, ff);     // Fill Factor
  
  // Create voltage array string (field 5)
  String voltageStr = createArrayString(vocValues, 16, 0);  // No decimals for voltage
  if (voltageStr.length() <= 255) {
    ThingSpeak.setField(5, voltageStr);
  } else {
    Serial.println("Voltage array too long!");
  }
  
  // Create current array string (field 6)
  String currentStr = createArrayString(icalValues, 16, 2);  // 2 decimals for current
  if (currentStr.length() <= 255) {
    ThingSpeak.setField(6, currentStr);
  } else {
    Serial.println("Current array too long!");
  }
  
  // Write to ThingSpeak
  int status = ThingSpeak.writeFields(THINGSPEAK_CHANNEL_ID, THINGSPEAK_API_KEY);
  
  if (status == 200) {
    Serial.println("ThingSpeak update successful");
    Serial.println("Voltage array sent:");
    Serial.println(voltageStr);
    Serial.println("Current array sent:");
    Serial.println(currentStr);
  } else {
    Serial.printf("ThingSpeak update failed, status: %d\n", status);
  }
}

void setupLED() {
  pixel.begin();
  pixel.setBrightness(25);  // Set to 50% brightness
  pixel.show();
}

void setLEDState(uint8_t state) {
  switch(state) {
    case LED_SENDING:
      pixel.setPixelColor(0, pixel.Color(0, 0, 255));  // Blue
      break;
    case LED_MEASURING:
      pixel.setPixelColor(0, pixel.Color(148, 128, 0));  // Yellow
      break;
    case LED_IDLE:
      pixel.setPixelColor(0, pixel.Color(128, 0, 128));  // Purple
      break;
    case LED_SUCCESS:
      pixel.setPixelColor(0, pixel.Color(0, 255, 0));  // Green
      break;
    case LED_ERROR:
      pixel.setPixelColor(0, pixel.Color(255, 0, 0));  // Red
      break;
  }
  pixel.show();
}