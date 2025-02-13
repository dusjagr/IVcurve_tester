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
#include "hackteria_logo.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "config.h"
#include "board_config.h"
#include "IVserver.h"

// Display Configuration
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define SCREEN_ADDRESS 0x3C // OLED display I2C address

// I2C Pin Definitions
#define SDA_PIN 8   // Default I2C SDA for S3-Mini
#define SCL_PIN 9   // Default I2C SCL for S3-Mini

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

// Global IV Server instance
//IVserver server(80);  // Create an IV server on port 80

// Function declarations
void initDisplay();
void showLogo_hackteria();
void drawBackground();
void drawIVline();
int readMux(int channel);
void setupNetwork();
void handleIVserver();
void showBootScreen();
void showCreditsScreen();

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

// Measurement and Display Variables
int sig = 0;     // Current ADC reading
int overlay = 0; // Display overlay state
int scaleY;      // Y-axis (current) scaling factor from potentiometer
int scaleX;      // X-axis (voltage) scaling factor from potentiometer
int average = 3; // Number of readings to average for noise reduction
int delayAveraging = 10;  // Delay between averaged readings (ms)
int delayLoop = 200;      // Delay between measurement cycles (ms)

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
float maxPower = 0;    // Maximum power point tracking
float maxCurrent = 0;  // Maximum current tracking
float maxVoltage = 0;  // Maximum voltage tracking
int mppIndex = 0;      // Index of maximum power point

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
  140,
  112,
  93,
  81,
  81,
  81,
  81,
  81
};

void setup() {
  Serial.begin(9600);
  delay(500);
  Serial.println("=========== Booting I-V Scanner ================");
  delay(200);
  Wire.begin(SDA_PIN, SCL_PIN);

  //put MUX into open circuit Channel 0
  pinMode(s0, OUTPUT);
  pinMode(s1, OUTPUT);
  pinMode(s2, OUTPUT);
  pinMode(s3, OUTPUT);
  pinMode(TPI_pin, OUTPUT);
  pinMode(BUTT_pin,INPUT_PULLUP);

  digitalWrite(s0, 0);
  digitalWrite(s1, 0);
  digitalWrite(s2, 0);
  digitalWrite(s3, 0);
  digitalWrite(TPI_pin, 0);

  Serial.println("    ");
  Serial.println("=========== Initializing OLED Screen ===========");
  initDisplay();
  delay(100);
  showLogo_hackteria();
  delay(1000);
  setupNetwork();
  setupIVserver();  // Initialize our IV curve web server
  showBootScreen();
  delay(1000);
  showCreditsScreen();
  delay(1000);
  display.clearDisplay();
  display.display();
  Serial.println("    ");
  Serial.println("=========== Starting I-V Measurement ===========");
  drawBackground();

}

void loop() {
  // Handle IV server requests
  handleIVserver();
  
  // Read scaling potentiometers
  scaleX = analogReadMilliVolts(POTX_pin);
  scaleY = analogReadMilliVolts(POTY_pin);
  digitalWrite(TPI_pin, 0);
  
  // Reset maximum values at the start of each measurement cycle
  maxPower = 0;
  maxCurrent = 0;
  maxVoltage = 0;
  
  // Measure through all resistors
  digitalWrite(s0, 0);
  digitalWrite(s1, 0);
  digitalWrite(s2, 0);
  digitalWrite(s3, 0);
  digitalWrite(TPI_pin, 0);
  delay(50);

  for (int i = 0; i < 16; i++) {
    readMux(i);
    vocValues[i] = sig;  // Convert ADC reading to voltage
    icalValues[i] = vocValues[i] / resistorValues[i];  // Calculate current in mA
    powValues[i] = vocValues[i] * icalValues[i];  // Calculate power
    
    // Update maximum values
    if (powValues[i] > maxPower) {
      maxPower = powValues[i];
      mppIndex = i;
      maxCurrent = icalValues[i];  // Current at MPP
      maxVoltage = vocValues[i];   // Voltage at MPP
    }
    
    delay(delayAveraging);
  }
  
  // Update IV server with new measurement data
  IVData data;
  data.maxPower = maxPower;
  data.maxCurrent = maxCurrent;
  data.maxVoltage = maxVoltage;
  data.vocValues = vocValues;
  data.icalValues = icalValues;
  data.powValues = powValues;
  data.numPoints = 16;  // number of measurement points
  updateIVserverData(data);
  
  // Draw the I-V curve on OLED
  drawIVline();
  
  // type out to serial, if mode is selected through button press once
  if (serialMode) {
    Serial.println("    ");
    Serial.println("=========== I calculated ===========");
    for(int i = 0; i <=15; i ++){
    Serial.println(icalValues[i]);
    }

    Serial.println("=========== Power calculated ===========");
    for(int i = 0; i <=15; i ++){
    Serial.println(powValues[i]);
    }
    Serial.println("    ");
    Serial.println("=========== V meas===========");
    for(int i = 0; i <=15; i ++){
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
    serialMode = 0;
  }

  delay(delayLoop);
}

int readMux(int channel){
  int controlPin[] = {TPI_pin, s0, s1, s2, s3};

  //rearranged the Channel 0 to the end according to soldered circuit
  int muxChannel[20][5]={
    {0,0,0,0,0}, //channel 0
    {0,1,0,0,0}, //channel 1
    {0,0,1,0,0}, //channel 2
    {0,1,1,0,0}, //channel 3
    {0,0,0,1,0}, //channel 4
    {0,1,0,1,0}, //channel 5
    {0,0,1,1,0}, //channel 6
    {0,1,1,1,0}, //channel 7
    {0,0,0,0,1}, //channel 8
    {0,1,0,0,1}, //channel 9
    {0,0,1,0,1}, //channel 10
    {0,1,1,0,1}, //channel 11
    {0,0,0,1,1}, //channel 12
    {0,1,0,1,1}, //channel 13
    {0,0,1,1,1}, //channel 14
    {0,1,1,1,1}, //channel 15
    {0,1,0,0,0}, //channel TPI
    {0,1,0,0,0}, //channel TPI
    {0,1,0,0,0}, //channel TPI
    {0,1,0,0,0} //channel TPI
  };

  //loop through the 4 sig + TPI channel
  for(int i = 0; i < 5; i ++){
    digitalWrite(controlPin[i], muxChannel[channel][i]);   
  }

  int prev = 0;
  int val = 0;
 
  //read the value at the SIG pin and average it a couple of times.
  for(int i = 1; i <= average; i ++){
  //val = analogRead(SIG_pin);
  val = analogReadMilliVolts(SIG_pin)*2; // there is a voltage divider
  
  // Read the button state
  buttonState = digitalRead(BUTT_pin) == LOW; // LOW means pressed

  // Check if the button state changed and is pressed
  if (buttonState && !lastButtonState) {
    lastPressTime = millis(); // Update the last press time
    Serial.println("Button pressed!");
  }

  // Check if the button has been pressed recently
  if (millis() - lastPressTime <= recentThreshold) {
    serialMode = 1;
  } else {
    //serialMode = 0;
  }

  lastButtonState = buttonState; // Update last button state
  
  val = prev + val;
  delay(delayAveraging);
  prev = val;
  }

  //return the value
  sig = val / average;
  return sig;
}

void initDisplay()
{  /* SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
     } 
     */
    display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS, false, false);
    display.clearDisplay();
    display.display();
}

void drawIVline()
{
  display.clearDisplay();  // Clear the display buffer
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
    int x1 = 1 + (int)(vocValues[i] * scaleX_auto);     // Start at x=1
    int y1 = 42 - (int)(icalValues[i] * scaleY_auto);
    int x2 = 1 + (int)(vocValues[i-1] * scaleX_auto);   // Start at x=1
    int y2 = 42 - (int)(icalValues[i-1] * scaleY_auto);
    
    // Ensure coordinates are within display bounds
    x1 = constrain(x1, 1, 90);    // Exactly 1-90 pixels for x
    y1 = constrain(y1, 0, 44);    // 0-44 for y
    x2 = constrain(x2, 1, 90);
    y2 = constrain(y2, 0, 44);
    
    display.drawLine(x1, y1, x2, y2, WHITE);
    display.drawRect(x2-1, y2-1, 3, 3, WHITE);
  }
  
  // Draw maximum power point marker
  int mpx = 1 + (int)(vocValues[mppIndex] * scaleX_auto);  // Start at x=1
  int mpy = 42 - (int)(icalValues[mppIndex] * scaleY_auto);
  mpx = constrain(mpx, 3, 88);    // Keep MPP marker within graph area
  mpy = constrain(mpy, 2, 42);
  display.fillRect(mpx-2, mpy-2, 5, 5, WHITE);
  
  display.display();
}

void drawBackground()
{  
  display.clearDisplay();
  // Draw main frame for I-V curve (100x44 pixels)
  display.drawRect(0, 0, 100, 44, SSD1306_WHITE);
  
  // Draw grid lines - vertical (5 sections)
  for (int i = 0; i < 100; i += 20) {  
    for (int j = 0; j < 44; j += 2) {   
      if (i > 0) display.drawPixel(i, j, WHITE);  // Dotted line pattern
    }
  }
  
  // Draw grid lines - horizontal (4 sections)
  for (int i = 0; i < 44; i += 11) {    
    for (int j = 0; j < 100; j += 2) {  
      if (i > 0) display.drawPixel(j, i, WHITE);  // Dotted line pattern
    }
  }

  // Draw right-side information panel
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(104, 0);
  display.println("I-V");
  // Display short circuit current (Isc)
  display.setCursor(104, 12);
  display.println("Isc:");
  display.setCursor(104, 20);
  display.print(icalValues[15], 1);  // Index 15 for short circuit current
  
  // Display open circuit voltage (Voc)
  display.setCursor(104, 30);
  display.println("Voc:");
  display.setCursor(104, 38);
  display.print(vocValues[0]/1000.0, 2);  // Index 0 for open circuit voltage, convert to V

  // Display voltage and current at MPP
  display.setTextSize(1);
  display.setCursor(0, 47);
  display.print("MPP: ");
  display.print(maxPower/1000, 1);  // Actual measured MPP
  display.println("mW");
  
  display.setCursor(0, 56);
  display.print("V:");
  display.print(vocValues[mppIndex], 0);  // Voltage at MPP
  display.print("mV  I:");
  display.print(icalValues[mppIndex], 2);  // Current at MPP
  display.print("mA");
  
  display.display();
}

void showLogo_hackteria(){
  display.clearDisplay(); // Make sure the display is cleared
  display.drawBitmap(0, 0, hackteria_logo, 128, 64, WHITE);  
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(11,0);
  display.println("HACKTERIA");
  // Update the display
  display.display();
  delay(30);
}

void showBootScreen() {
  display.clearDisplay();
  
  // Draw a frame
  display.drawRect(0, 0, 128, 64, WHITE);
  
  // Title
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(10, 4);
  display.println("I-V CURVE");
  display.setCursor(15, 22);
  display.println("SCANNER");
  
  // Smaller text at bottom - centered
  display.setTextSize(1);
  // "Solar Cell Analyzer" is 17 characters * 6 pixels = 102 pixels
  display.setCursor((128 - 102)/2 - 4, 45);  // Shifted 4 pixels left
  display.println("Solar Cell Analyzer");
  // "Version 1.0 2024" is 15 characters * 6 pixels = 90 pixels
  display.setCursor((128 - 90)/2 - 4, 55);   // Shifted 4 pixels left
  display.println("Version 1.0 2025");
  
  display.display();
}

void showCreditsScreen() {
  display.clearDisplay();
  
  // Draw a frame
  display.drawRect(0, 0, 128, 64, WHITE);
  
  // Main credit in large text
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(6, 16);  // Moved left from center position
  display.println("by dusjagr");
  
  // Organization name in small text, centered in three lines
  display.setTextSize(1);
  // "Center for" is 10 chars * 6 pixels = 60 pixels
  display.setCursor((128 - 60)/2, 38);
  display.println("Center for");
  // "Alternative" is 11 chars * 6 pixels = 66 pixels
  display.setCursor((128 - 66)/2, 46);
  display.println("Alternative");
  // "Coconut Research" is 15 chars * 6 pixels = 90 pixels
  display.setCursor((128 - 90)/2, 54);
  display.println("Coconut Research");
  
  display.display();
}

void setupNetwork() {
  // Connect to WiFi
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);
  
  WiFi.mode(WIFI_STA);
  
  // Handle both open and password-protected networks
  if (strlen(WIFI_PASSWORD) > 0) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  } else {
    WiFi.begin(WIFI_SSID);  // For open networks
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
  display.print("Connecting");
  display.display();
  
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    display.print(".");
    display.display();
  }
  
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(WIFI_SSID);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Initialize mDNS
  if (MDNS.begin("ivcurve")) {
    Serial.println("mDNS responder started");
    Serial.println("You can access the server at: http://ivcurve.local");
  }
  
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Connected!");
  display.println(WiFi.localIP());
  display.println("ivcurve.local");
  display.display();
  delay(2000);
}