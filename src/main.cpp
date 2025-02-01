/*
I-V curve analyzer for low-power energies
Array of resistors and MUX through 15 of them, and 1 open circuit
Display on OLED screen.

ToDo:
* add button and knob for interface control
* Send data through the web

*/

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "hackteria_logo.h"

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define SCREEN_ADDRESS 0x3C // OLED display I2C address

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

// Function declarations
void initDisplay();
void showLogo_hackteria();
void drawBackground();
void drawIVline();
void drawIVline1();
int readMux(int channel);

//Mux control pins
int s0 = 4;     // Changed for S3-Mini
int s1 = 5;     // Changed for S3-Mini
int s2 = 6;     // Changed for S3-Mini
int s3 = 7;     // Changed for S3-Mini
int SIG_pin = 1;    // Changed for S3-Mini (ADC)
int POTY_pin = 2;   // Changed for S3-Mini (ADC)
int POTX_pin = 3;   // Changed for S3-Mini (ADC)
int BUTT_pin = 0;   // Changed for S3-Mini
int TPI_pin = 8;    // Changed for S3-Mini
int sig = 0;
int overlay = 0;
int scaleY;
int scaleX;
int average = 3;
int delayAveraging = 10;
bool serialMode = 0;
bool buttonState = false; // Current button state
bool lastButtonState = false; // Previous button state

unsigned long lastPressTime = 0; // Timestamp of the last button press
const unsigned long recentThreshold = 200; // Time in ms to consider as "recent"

#define SDA_PIN 9    // Changed for S3-Mini
#define SCL_PIN 10   // Changed for S3-Mini

int vocValues[20];
int icalValues[20];
int powValues[20];

// Resistors as measured through the MUX. 
//Should re-measure and average a bit.

int resistorValues5V[20]{
11636,
7420,
4830,
3194,
1892,
977,
761,
516,
386,
283,
218,
177,
155,
129,
110,
102,
50,
50,
50,
50
};

int resistorValues[20]{
10317,
6856,
4723,
3211,
1878,
972,
764,
516,
383,
278,
212,
165,
142,
114,
95,
83,
83,
83,
83,
83
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
  display.clearDisplay(); // Make sure the display is cleared
  display.display();
  Serial.println("    ");
  Serial.println("=========== Starting I-V Measurement ===========");
  drawBackground();
  
}

void loop() {
  // read the interface
  scaleX = analogReadMilliVolts(POTX_pin);
  scaleY = analogReadMilliVolts(POTY_pin);
  digitalWrite(TPI_pin, 0);
  // go through the 16 MUX channels and read analog value
  // there is a voltage divider before the A0 pin to half the voltage to keep it below 3.3V
  float maxPower = 0;
  int mppIndex = 0;
  
  for(int i = 19; i >= 0; i --){
    int sig = readMux(i);

    // just kinda calibrated this from other multimeter measurement.
    //int voc = (sig * 2900) / 4095 * 2; 
    int voc = sig * 2;
    vocValues[i] = voc;
    float ical = voc * 1000 / resistorValues[19-i];
    float powi = voc * (voc * 1000 / resistorValues[19-i]);
    icalValues[i] = ical;
    powValues[i] = powi / 1000;
    
    // Track maximum power point
    if (powValues[i] > maxPower) {
      maxPower = powValues[i];
      mppIndex = i;
    }

    display.fillRect(102, 48, 26, 16, SSD1306_BLACK);
    display.setCursor(104, 48);
    display.println("Voc:");
    display.setCursor(104, 56);
    display.println(voc);
    display.display();
  }

  // Display MPP values
  display.fillRect(0, 0, 100, 32, SSD1306_BLACK);  // Clear top-left area
  display.setTextSize(1);
  
  display.setCursor(0, 0);
  display.print("MPP: ");
  display.print(maxPower, 1);
  display.println("mW");
  
  display.setCursor(0, 10);
  display.print("Vmpp: ");
  display.print(vocValues[mppIndex]);
  display.println("mV");
  
  display.setCursor(0, 20);
  display.print("Impp: ");
  display.print(icalValues[mppIndex], 1);
  display.println("uA");
  
  display.display();

  // type out to serial, if mode is selected through button press once
  if (serialMode) {
    Serial.println("    ");
    Serial.println("=========== V meas===========");
    
    for(int i = 19; i >= 0; i --){
      Serial.println(vocValues[i]);
    }  
    
    Serial.println("    ");
    Serial.println("=========== I cal ===========");
    for(int i = 19; i >= 0; i --){
      Serial.println(icalValues[i]);
    }

    Serial.println("=========== POW2 ===========");
    for(int i = 19; i >= 0; i --){
      Serial.println(powValues[i]);
    }

    Serial.println("=========== MPP Details ===========");
    Serial.print("Maximum Power: ");
    Serial.print(maxPower, 1);
    Serial.println(" mW");
    Serial.print("MPP Voltage: ");
    Serial.print(vocValues[mppIndex]);
    Serial.println(" mV");
    Serial.print("MPP Current: ");
    Serial.print(icalValues[mppIndex], 1);
    Serial.println(" uA");
    Serial.print("Resistor at MPP: ");
    Serial.print(resistorValues[19-mppIndex]);
    Serial.println(" ohm");
    Serial.println("================================");
    
    delay(10);
    serialMode = 0;
  }

  //put MUX into open circuit Channel 0
  digitalWrite(s0, 0);
  digitalWrite(s1, 0);
  digitalWrite(s2, 0);
  digitalWrite(s3, 0);
  digitalWrite(TPI_pin, 0);
  
  /*
  digitalWrite(TPI_pin, 1);
  int TPI_val = analogReadMilliVolts(SIG_pin);
  Serial.println("    ");
  Serial.println(TPI_val * 2);
  */
  //Display curve on OLED
  overlay = overlay + 1;
  if (overlay == 1){
    
    display.clearDisplay();
    drawBackground();
    overlay = 0;
  }
  drawIVline();
  delay(10);

}

int readMux(int channel){
  int controlPin[] = {TPI_pin, s0, s1, s2, s3};

  //rearranged the Channel 0 to the end according to soldered circuit
  int muxChannel[20][5]={
    {0,1,0,0,0}, //channel TPI
    {0,1,0,0,0}, //channel TPI
    {0,1,0,0,0}, //channel TPI
    {0,1,0,0,0}, //channel TPI
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
    {0,0,0,0,0}  //channel 0
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
  val = analogReadMilliVolts(SIG_pin);
  
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
  return val / average;
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
{ //display.clearDisplay();
  //drawBackground();
  int scale_Y = (scaleY+100) / 2;
  int scale_X = (scaleX+200) / 50;
  for(int i = 19; i > 0; i --){
    display.drawLine(vocValues[i]/scale_X,64-icalValues[i]/scale_Y,vocValues[i-1]/scale_X,64-icalValues[i-1]/scale_Y,WHITE);
    display.drawRect(vocValues[i-1]/scale_X-1, 64-icalValues[i-1]/scale_Y-1, 3, 3, WHITE);
  }
  display.drawLine(vocValues[0]/scale_X,64-icalValues[0]/scale_Y,0,64-icalValues[0]/scale_Y,WHITE);
  display.drawRect(0, 64-icalValues[0]/scale_Y-1, 3, 3, WHITE);
  display.display();
}

void drawIVline1()
{
  display.drawLine(5,5,25,6,WHITE);
  display.drawLine(25,6,47,10,WHITE);
  display.drawLine(25,6,47,10,WHITE);
  display.drawLine(47,10,55,14,WHITE);
  display.drawLine(55,14,60,20,WHITE);
  display.drawLine(60,20,67,30,WHITE);
  display.drawLine(67,30,73,40,WHITE);
  display.drawLine(73,40,78,60,WHITE);
  display.display();
}

void drawBackground()
{  display.drawRect(0, 0, 100, 64, SSD1306_WHITE);
  for (int i = 2; i < 100; i = i + 5) {
        display.drawPixel(i, 24, WHITE);
        display.drawPixel(i, 44, WHITE);
        display.drawPixel(i, 4, WHITE);

        display.drawPixel(20, i, WHITE);
        display.drawPixel(40, i, WHITE);
        display.drawPixel(60, i, WHITE);
        display.drawPixel(80, i, WHITE);
    }
  display.setTextSize(1);
  display.setCursor(104, 0);
  display.println("I-V");
  display.setCursor(104, 12);
  display.println("Isc:");
  display.setCursor(104, 20);
  display.println(icalValues[0]/1000);
  display.setCursor(104, 30);
  display.println("MPP:");
  display.setCursor(104, 38);
  display.println(powValues[6]);
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