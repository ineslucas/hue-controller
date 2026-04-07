#include <EncoderStepCounter.h>
 
// encoder pins:
const int pin1 = 2;
const int pin2 = 3;
// Create encoder instance:
EncoderStepCounter encoder(pin1, pin2);
 
// encoder previous position:
int oldPosition = 0;
 
void setup() {
  Serial.begin(9600);
  // Initialize encoder
  encoder.begin();
}
 
void loop() {
  // if you're not using interrupts, you need this in the loop:
  encoder.tick();
  // read encoder position:
  int position = encoder.getPosition();
 
  // if there's been a change, print it:
  if (position != oldPosition) {
    Serial.println(position);
    oldPosition = position;
  }
}