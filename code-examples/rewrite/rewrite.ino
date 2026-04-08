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

 // Wi-Fi + bridge
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
int status = WL_IDLE_STATUS;      // the WiFi radio's status
char hueHubIP[] = SECRET_HUE_IP;        // IP address of the HUE bridge
String hueUserName = SECRET_HUE_USER;   // hue bridge username
WiFiClient wifi; // Make a WiFiClient instance
HttpClient httpClient = HttpClient(wifi, hueHubIP); // HttpClient instance

const int hueLight = 4;   // same light # currently being controlled

// Local state — Arduino is the source of truth
int  hueValue = 0;        // 0–65535, seeded from GET
int  satValue = 254;      // 0–254,   seeded from GET
int  briValue = 200;      // 1–254,   seeded from GET
bool lightOn  = true;

// Encoder
const int pin1 = 2;
const int pin2 = 3;
EncoderStepCounter encoder(pin1, pin2);
int oldPosition = 0;

// LED strip
#define LED_PIN    6
#define LED_COUNT  8
#define BRIGHTNESS 50
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// FSR (brightness)
const int fsrPin = A0;
int lastSentBri = -1;
unsigned long lastBriSent = 0;

// Fan switch (on/off)
const int fanSwitchPin = A7;
int lastSwitchState = LOW;

// Rate-limit PUTs (shared interval for hue and bri)
unsigned long lastHueSent = 0;
int           lastSentHue = -1;
const unsigned long SEND_INTERVAL = 150; // ms — ~6–7 PUTs/sec ceiling

void setup() {
  Serial.begin(9600);
  while (!Serial);

  // Connect to WiFi
  while (status != WL_CONNECTED) {
    status = WiFi.begin(ssid, pass);
  }
  Serial.print("You're connected to the network IP = ");
  IPAddress ip = WiFi.localIP();
  Serial.println(ip);

  encoder.begin();
  pinMode(fanSwitchPin, INPUT_PULLDOWN);

  strip.begin();
  strip.show();
  strip.setBrightness(BRIGHTNESS);

  // Seed local state from the bridge — exactly once
  fetchLightState(hueLight);

  // Force bulb sat to 254 so the strip (rendered at full sat) matches the bulb
  sendPutRequest(hueLight, "sat", "254");
  satValue = 254;
}

void loop() {
  encoder.tick();
  int position = encoder.getPosition();

  // 1. Encoder → local hueValue
  if (position != oldPosition) {
    hueValue += (position - oldPosition) * 1000; // sensitivity
    hueValue = (hueValue + 65536) % 65536;       // wrap around the color wheel
    oldPosition = position;
  }

  // 2. FSR → local briValue (deadband to ignore jitter)
  int fsrRaw = analogRead(fsrPin);              // 0–1023
  int bri = map(fsrRaw, 0, 1023, 254, 1);       // Hue bri range (1–254)
  if (abs(bri - briValue) > 3) {
    briValue = bri;
  }

  // 3. Fan switch → toggle lightOn locally on edge
  int switchState = digitalRead(fanSwitchPin);
  if (switchState != lastSwitchState) {
    lightOn = !lightOn;
    sendPutRequest(hueLight, "on", lightOn ? "true" : "false");
    lastSwitchState = switchState;
  }

  // 4. Always paint the strip from local state — no HTTP in this path
  LEDStrip();

  // 5. Push hue to bridge only when changed AND interval elapsed
  if (hueValue != lastSentHue && millis() - lastHueSent > SEND_INTERVAL) {
    sendPutRequest(hueLight, "hue", String(hueValue));
    lastSentHue = hueValue;
    lastHueSent = millis();
  }

  // 6. Push bri to bridge only when changed AND interval elapsed
  if (briValue != lastSentBri && millis() - lastBriSent > SEND_INTERVAL) {
    sendPutRequest(hueLight, "bri", String(briValue));
    lastSentBri = briValue;
    lastBriSent = millis();
  }
}

void LEDStrip() {
  float lit = lightOn ? (briValue / 254.0) * LED_COUNT : 0;
  if (lit < 0) lit = 0;

  for (int i = 0; i < LED_COUNT; i++) {
    float frac = lit - i;
    if (frac > 1) frac = 1;
    if (frac < 0) frac = 0;
    uint8_t val = frac * 255;
    strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(hueValue, 255, val)));
  }
  strip.show();
}

// HTTP PUT to /api/<user>/lights/<n>/state/ with a JSON body
void sendPutRequest(int light, String cmd, String value) {
  String request = "/api/" + hueUserName;
  request += "/lights/";
  request += light;
  request += "/state/";

  String contentType = "application/json";

  String hueCmd = "{\"" + cmd;
  hueCmd += "\":";
  hueCmd += value;
  hueCmd += "}";

  Serial.print("PUT ");
  Serial.print(request);
  Serial.print("  body: ");
  Serial.println(hueCmd);

  httpClient.put(request, contentType, hueCmd);
  int statusCode = httpClient.responseStatusCode();
  String response = httpClient.responseBody();
  Serial.print("  status: ");
  Serial.println(statusCode);
}

// One-shot GET to seed local state from the bridge
void fetchLightState(int light) {
  httpClient.get("/api/" + hueUserName + "/lights/" + light);
  httpClient.responseStatusCode();
  String response = httpClient.responseBody();

  lightOn = response.indexOf("\"on\":true") != -1;

  int hueIdx = response.indexOf("\"hue\":");
  if (hueIdx != -1) hueValue = response.substring(hueIdx + 6).toInt();

  int briIdx = response.indexOf("\"bri\":");
  if (briIdx != -1) briValue = response.substring(briIdx + 6).toInt();

  int satIdx = response.indexOf("\"sat\":");
  if (satIdx != -1) satValue = response.substring(satIdx + 6).toInt();
}