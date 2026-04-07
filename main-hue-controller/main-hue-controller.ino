/* 
   Uses ArduinoHttpClient library to control Philips Hue
   For more on Hue developer API see http://developer.meethue.com

  To control a light, the Hue expects a HTTP PUT request to:
  http://hue.hub.address/api/hueUserName/lights/lightNumber/state

  The body of the PUT request looks like this:
  {"on": true} or {"on":false}

  This example shows how to concatenate Strings to assemble the
  PUT request and the body of the request.

  Code sources included in the GitHub repo and documentation.
*/

#include <SPI.h>
#include <WiFiNINA.h> // Library for the Nano 33 IoT
#include <ArduinoHttpClient.h>
#include "arduino_secrets.h"
#include <EncoderStepCounter.h>
#include <Adafruit_NeoPixel.h>

///////please enter your sensitive data in the Secret tab/arduino_secrets.h
/////// WiFi Settings ///////
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

int status = WL_IDLE_STATUS;      // the WiFi radio's status

char hueHubIP[] = SECRET_HUE_IP;        // IP address of the HUE bridge
String hueUserName = SECRET_HUE_USER;   // hue bridge username
int hueValue = 0; // 0–65535
int hueLight = 7;

// Make a WiFiClient instance and a HttpClient instance:
WiFiClient wifi;
HttpClient httpClient = HttpClient(wifi, hueHubIP);

// ENCODER
const int pin1 = 2;
const int pin2 = 3;
EncoderStepCounter encoder(pin1, pin2); // Create encoder instance:
int oldPosition = 0; // encoder previous position:

// LED STRIP
#define LED_PIN     6
#define LED_COUNT  8 // How many NeoPixels are attached to the Arduino?
// NeoPixel brightness, 0 (min) to 255 (max)
#define BRIGHTNESS 50 // Set BRIGHTNESS to about 1/5 (max = 255)
#define DELAYVAL 500 // Time (in milliseconds) to pause between pixels

// Declare our NeoPixel strip object:
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
// Argument 1 = Number of pixels in NeoPixel strip
// Argument 2 = Arduino pin number (most are valid)
// Argument 3 = Pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
//   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)

// Ceiling Fan Switch
const int fanSwitchPin = A7;
int lastSwitchState = LOW;

// FSR
const int fsrPin = A0;
int lastBri = -1;

bool lightOn = true;

void setup() {
  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  while (!Serial); // wait for serial port to connect.

  // attempt to connect to WiFi network:
  while ( status != WL_CONNECTED) {
    // Serial.print("Attempting to connect to WPA SSID: ");
    // Serial.println(ssid);
    status = WiFi.begin(ssid, pass); // Connect to WPA/WPA2 network
  }

  // you're connected now, so print out the data:
  Serial.print("You're connected to the network IP = ");
  IPAddress ip = WiFi.localIP();
  Serial.println(ip);

  pinMode(fanSwitchPin, INPUT_PULLDOWN);

  encoder.begin(); // Initialize encoder

  // LED STRIP
  strip.begin();           // INITIALIZE NeoPixel strip object (REQUIRED)
  strip.show();            // Turn OFF all pixels ASAP
  strip.setBrightness(BRIGHTNESS);
}

void loop() {
  // Fan Switch
  int switchState = digitalRead(fanSwitchPin);
  if (switchState != lastSwitchState) {
    delay(20);                                  // crude debounce
    lightOn = !getLightOn(hueLight);            // flip whatever the bridge currently says
    sendRequest(hueLight, "on", lightOn ? "true" : "false");
    lastSwitchState = switchState;
  }

  // strip.clear(); // Set all pixel colors to 'off' // DELETE
  encoder.tick(); // if you're not using interrupts, you need this in the loop:
  int position = encoder.getPosition(); // read encoder position:
 
  // if there's been a change, print it:
  if (position != oldPosition) {
    hueValue += (position - oldPosition) * 1000;   // 1000 = sensitivity
    hueValue = (hueValue + 65536) % 65536;         // wrap around color wheel
    oldPosition = position;

    sendRequest(hueLight, "hue", String(hueValue));

    Serial.println(position);
  }

  // HUE
  // sendRequest(8, "on", "true");   // turn light number 8 on
  // delay(2000);                    // wait 2 seconds
  // sendRequest(8, "on", "false");  // turn light off
  // delay(2000);                    // wait 2 seconds

  LEDStrip();

  int fsrRaw = analogRead(fsrPin);                  // 0–1023
  int bri = map(fsrRaw, 0, 1023, 254, 0);           // Hue bri range
  if (abs(bri - lastBri) > 3) {                     // deadband: ignore jitter
    sendRequest(hueLight, "bri", String(bri));
    lastBri = bri;
  }
}

void LEDStrip(){
  float lit = lightOn ? (lastBri / 254.0) * LED_COUNT : 0;

  if (lit < 0) lit = 0;                       // handles initial lastBri = -1

  for (int i = 0; i < LED_COUNT; i++) {
    float frac = lit - i;                     // >=1 full, <=0 off, between = partial
    if (frac > 1) frac = 1;
    if (frac < 0) frac = 0;
    uint8_t val = frac * 255;
    strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(hueValue, 255, val)));
  }

  // The first NeoPixel in a strand is #0, second is 1, all the way up
  // to the count of pixels minus one.
  // for(int i=0; i<LED_COUNT; i++) { // For each pixel...

  //   // pixels.Color() takes RGB values, from 0,0,0 up to 255,255,255
  //   // Here we're using a moderately bright green color:
  //   // strip.setPixelColor(i, strip.Color(0, 150, 0));
  //   strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(hueValue)));

    

  //   // delay(DELAYVAL); // Pause before next pass through loop
  // }
  strip.show();   // Send the updated pixel colors to the hardware.
}

void sendRequest(int light, String cmd, String value) {
  // make a String for the HTTP request path:
  String request = "/api/" + hueUserName;
  request += "/lights/";
  request += light;
  request += "/state/";

  String contentType = "application/json";

  // make a string for the JSON command:
  String hueCmd = "{\"" + cmd;
  hueCmd += "\":";
  hueCmd += value;
  hueCmd += "}";
  // see what you assembled to send:
  Serial.print("PUT request to server: ");
  Serial.println(request);
  Serial.print("JSON command to server: ");

  // make the PUT request to the hub:
  httpClient.put(request, contentType, hueCmd);
  
  // read the status code and body of the response
  int statusCode = httpClient.responseStatusCode();
  String response = httpClient.responseBody();

  Serial.println(hueCmd);
  Serial.print("Status code from server: ");
  Serial.println(statusCode);
  Serial.print("Server response: ");
  Serial.println(response);
  Serial.println();
}

bool getLightOn(int light) {
    String request = "/api/" + hueUserName + "/lights/" + light;
    httpClient.get(request);
    httpClient.responseStatusCode();          // must be called
    String response = httpClient.responseBody();
    return response.indexOf("\"on\":true") >= 0;
  }