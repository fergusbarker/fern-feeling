/*------------
MIDI_PsychoGalvanometer v021
Accepts pulse inputs from a Galvanic Conductance sensor
consisting of a 555 timer set as an astablemultivibrator and two electrodes.

TODO:
- Pattern generator?
- Move to MIDI library
-------------*/

#include <LEDFader.h> //manage LEDs without delay() jgillick/arduino-LEDFader https://github.com/jgillick/arduino-LEDFader.git
#include <MIDI.h> // midi test

//**********************************************************************************************************
// SCALES
//******************************
//set scaled values, sorted array, first element scale length
int scaleMajor[]  = {7,1, 3, 5, 6, 8, 10, 12};
int scaleDiaMinor[]  = {7,1, 3, 4, 6, 8, 9, 11};
int scaleIndian[]  = {7,1, 2, 2, 5, 6, 9, 11};
int scaleMinor[]  = {7,1, 3, 4, 6, 8, 9, 11};
int scaleChrom[] = {12,1,2,3,4,5,6,7,8,9,10,11,12};
int minorPent[] = {5,1,4,6,8,11};
int majorPent[] = {5,1,3,5,8,10};
int testNote[] = {2,1,2};

int root = 0; //initialize for root

int currentScale[] = {testNote, scaleDiaMinor, testNote, scaleMinor, testNote, minorPent, testNote};
int selectedScale = 0; // start at 0 the major scale
int scaleLength = 7; // length of currentScale,  easier than using arraylist for something so static

int *scaleSelect = currentScale[0]; //initialize scaling
//*******************************

// VARIABLES
// BUTTONS
const byte interruptPin = INT0; //galvanometer input
const byte knobPin = A0; //knob analog input
const int buttonPin = 8; // pin for button inputs BUTTON TO COME
int buttonState = 0; // read button
int lastButtonState = LOW; // for debounce

// TEMP BUTTON
const int LEDPin = 7;

// SAMPLE ARRAY
const byte samplesize = 10; //set sample array size
const byte analysize = samplesize - 1;  //trim for analysis array

// GENERAL BITS
const byte polyphony = 5; //above 8 notes may run out of ram
byte channel = 1;  //setting channel to 11 or 12 often helps simply computer midi routing setups
int noteMin = 36; //C2  - keyboard note minimum
int noteMax = 96; //C7  - keyboard note maximum
byte controlNumber = 80; //set to mappable control, low values may interfere with other soft synth controls!!
byte controlVoltage = 1; //output PWM CV on controlLED, pin 17, PB3, digital 11 *lowpass filter

// DEBOUNCE STUFF
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;    // the debounce time; increase if the output flickers
int ledState = HIGH;

// TIME
volatile unsigned long microseconds; //sampling timer
volatile byte index = 0;
volatile unsigned long samples[samplesize];
unsigned long previousMillis = 0;
unsigned long currentMillis = 1;

// Checks rate of change, essentially density?
// KNOB VALUES
float threshold = 2.3;  //change threshold multiplier
float threshMin = 1.61; //scaling threshold min
float threshMax = 3.71; //scaling threshold max
float knobMin = 1;
float knobMax = 1023;

// LEDS
#define LED_NUM 6
LEDFader leds[LED_NUM] = { // 6 LEDs (perhaps 2 RGB LEDs)
  LEDFader(3),
  LEDFader(5),
  LEDFader(6),
  LEDFader(9),
  LEDFader(10),
  LEDFader(11)  //Control Voltage output or controlLED
};
int ledNums[LED_NUM] = {3,5,6,9,10,11};
byte controlLED = 5; //array index of control LED (CV out)
byte noteLEDs = 1;  //performs lightshow set at noteOn event


// MIDI FORMAT
// Create a new type that stores midi and initialise as noteArray
typedef struct _MIDImessage { //build structure for Note and Control MIDImessages
  unsigned int type;
  int value;
  int velocity;
  long duration;
  long period;
  int channel;
}
MIDImessage;
MIDImessage noteArray[polyphony]; //manage MIDImessage data as an array with size polyphony
int noteIndex = 0;
MIDImessage controlMessage; //manage MIDImessage data for Control Message (CV out)

//**********************************************************************************************************


//////////////////////////////////////////////////////// MAIN LOOP AND SETUP

// INITIATE
void setup()
{
  pinMode(knobPin, INPUT);
  pinMode(buttonPin, INPUT);
  // TEMP BUTTON
  pinMode(LEDPin, OUTPUT);
  Serial.begin(31250);  //initialize at MIDI rate
  controlMessage.value = 0;  //begin CV at 0
  //MIDIpanic(); //dont panic, unless you are sure it is nessisary
  if(noteLEDs) bootLightshow(); //a light show to display on system boot
  attachInterrupt(interruptPin, sample, RISING);  //begin sampling from interrupt
}

// MAIN LOOP
void loop()
{
  currentMillis = millis();   //manage time
  checkKnob(); //check knob value
  if(index >= samplesize)  { analyzeSample(); }  //if samples array full, also checked in analyzeSample(), call sample analysis
  checkNote();  //turn off expired notes
  checkControl();  //update control value
  checkButton();  // WIP
  checkLED();  //LED management without delay()
  previousMillis = currentMillis;   //manage time
}

//////////////////////////////////////////////////////// MAIN LOOP AND SETUP


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////// MIDI
// NOTE VALUES FOR MIDI
void setControl(int type, int value, int velocity, long duration)
{
  controlMessage.type = type;
  controlMessage.value = value;
  controlMessage.velocity = velocity;
  controlMessage.period = duration;
  controlMessage.duration = currentMillis + duration; //schedule for update cycle
}

// CHANGE VALUES FOR MIDI BASED ON LIKE A SLIDING RATE? CV STUFF TOO (UNSURE)
void checkControl()
{
  //need to make this a smooth slide transition, using high precision
  //distance is current minus goal
  signed int distance =  controlMessage.velocity - controlMessage.value;
  //if still sliding
  if(distance != 0) {
    //check timing
    if(currentMillis>controlMessage.duration) { //and duration expired
        controlMessage.duration = currentMillis + controlMessage.period; //extend duration
        //update value
       if(distance > 0) { controlMessage.value += 1; } else { controlMessage.value -=1; }

       //send MIDI control message after ramp duration expires, on each increment
       midiSerial(176, channel, controlMessage.type, controlMessage.value);

        // THIS CONFUSES ME
        //send out control voltage message on pin 17, PB3, digital 11
        if(controlVoltage) {
          if (distance > 0) {
             rampUp(controlLED, map(controlMessage.value, 0, 127, 0 , 255), 5);
          }
          else
          {
            rampDown(controlLED, map(controlMessage.value, 0, 127, 0 , 255), 5);
          }
        }
    }
  }
}

// NOTES AND POLYPHONY AND I NEED TO LEARN HOW MIDI WORKS (UNSURE)
void checkNote()
{
  for (int i = 0;i<polyphony;i++) {
    if(noteArray[i].velocity) {
      if (noteArray[i].duration <= currentMillis) {
        //send noteOff for all notes with expired duration
        midiSerial(144, channel, noteArray[i].value, 0);
        noteArray[i].velocity = 0;
        rampDown(i, 0, 225);
      }
    }
  }

}

// THIS ISN'T USED BUT GOOD TO HAVE
/*
void MIDIpanic()
{
  //120 - all sound off
  //123 - All Notes off
 // midiSerial(21, panicChannel, 123, 0); //123 kill all notes

  //brute force all notes Off
  for(byte i=1;i<128;i++) {
    delay(1); //don't choke on note offs!
    midiSerial(144, channel, i, 0); //clear notes on main channel

  }


}
*/

// MORE MIDI SETUP (BAD FORMATTING)
void midiSerial(int type, int channel, int data1, int data2) {

  cli(); //kill interrupts, probably unnessisary
  //  Note type = 144
  //  Control type = 176
  // remove MSBs on data
	data1 &= 0x7F;  //number
	data2 &= 0x7F;  //velocity

	byte statusbyte = (type | ((channel-1) & 0x0F));

	Serial.write(statusbyte);
	Serial.write(data1);
	Serial.write(data2);
  sei(); //enable interrupts
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////// MIDI


//////////////////////////////////////////////////////// KNOB

// READ KNOB
void checkKnob() {
  //float knobValue
  threshold = analogRead(knobPin);
  //set threshold to knobValue mapping
  threshold = mapfloat(threshold, knobMin, knobMax, threshMin, threshMax);

}

// FOR KNOB
//provide float map function
float mapfloat(float x, float in_min, float in_max, float out_min, float out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
//////////////////////////////////////////////////////// KNOB

//////////////////////////////////////////////////////// LED LED LED
// LED RAMPS
void rampUp(int ledPin, int value, int time) {
LEDFader *led = &leds[ledPin];
// led->set_value(0);
  led->fade(value, time);
}

void rampDown(int ledPin, int value, int time) {
  LEDFader *led = &leds[ledPin];
 // led->set_value(255); //turn on
  led->fade(value, time); //fade out
}


// LED CHECK (DID I REMOVE THIS BEFORE?)
void checkLED(){
//iterate through LED array and call update
 for (byte i = 0; i < LED_NUM; i++) {
    LEDFader *led = &leds[i];
    led->update();
 }
}


// LIGHTS ON BOOT UP
void bootLightshow(){
 //light show to be displayed on boot
  for (byte i = 5; i > 0; i--) {
    LEDFader *led = &leds[i-1];
//    led->set_value(200); //set to max

    led->fade(200, 150); //fade up
    while(led->is_fading()) checkLED();


    led->fade(0,150+i*17);  //fade down
    while(led->is_fading()) checkLED();
   //move to next LED
  }
}
//////////////////////////////////////////////////////// LED LED LED


//////////////////////////////////////////////////////// BUTTON OPERATIONS
// THE MAIN UPCOMING AFFAIR
void checkButton() {

  int reading = digitalRead(buttonPin);

  // debounce
  // check for presses too fast
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  // act if enough time between debounce
  if ((millis() - lastDebounceTime) > debounceDelay) {
    // check for change
    if (reading != buttonState) {
      buttonState = reading;

      // if read high, increment root note
      if (buttonState == HIGH) {
        /*
        root++;
        // reloop for octave
        if (root == 12) {
          root = 0;
        }
        */
        changeScale();
        // temporary function for LED testing
        if ((root % 2) == 0) {
          ledState = !ledState;
        }
      }
    }
  }

  digitalWrite(LEDPin, ledState);

  /// TEMPORARY TEST FUNCTION FOR BUTTON
  /*
  if (buttonState == HIGH) {
    digitalWrite(LEDPin, HIGH);
  } else {
    digitalWrite(LEDPin, LOW);
  }
  */

  lastButtonState = reading;
}

// SCALE CHANGE
void changeScale() {
  selectedScale++;
  if (selectedScale == scaleLength)
  {
    selectedScale = 0;
  }
  *scaleSelect = currentScale[selectedScale];
}

// OCTAVE RANGE
void changeRange() {
  bool rangeUp = false; // increasing or decreasing range?
  bool bottomTop = false; // effecting the top or bottom end?
  if (rangeUp == false) {
    // decreasing range at the bottom
    if (bottomTop == false) {
      noteMin = noteMin + 12;
    }
    // decreasing range at the top
    else {
      noteMax = noteMin - 12;
    }
  }
  else {
    // increasing range at the bottom
    if (bottomTop == false) {
      noteMin = noteMin - 12;
    }
    // increasing range at the top
    else {
      noteMax = noteMin + 12;
    }
  }
}

//////////////////////////////////////////////////////// BUTTON OPERATIONS


//////////////////////////////////////////////////////// TRIGGER CODE

int triggerStore()
{
  // sends a trigger on new midi notes

}

//////////////////////////////////////////////////////// TRIGGER CODE
////////////////////////////////////////////////////////////////////////////////////////////////////////////// GALVANOMETER AND ANALYSIS
// FOR READING THE GALVANOMETER
// interrupt timing sample array
void sample()
{
  if(index < samplesize) {
    samples[index] = micros() - microseconds;
    microseconds = samples[index] + microseconds; //rebuild micros() value w/o recalling
    //micros() is very slow
    //try a higher precision counter
    //samples[index] = ((timer0_overflow_count << 8) + TCNT0) - microseconds;
    index += 1;
  }
}


// THE MAIN WORK, THE MATH I MUST LEARN
void analyzeSample()
{
  //eating up memory, one long at a time!
  unsigned long averg = 0;
  unsigned long maxim = 0;
  unsigned long minim = 100000;
  float stdevi = 0;
  unsigned long delta = 0;
  byte change = 0;

  if (index = samplesize) { //array is full
    unsigned long sampanalysis[analysize];
    for (byte i=0; i<analysize; i++){
      //skip first element in the array
      sampanalysis[i] = samples[i+1];  //load analysis table (due to volitle)
      //manual calculation
      if(sampanalysis[i] > maxim) { maxim = sampanalysis[i]; }
      if(sampanalysis[i] < minim) { minim = sampanalysis[i]; }
      averg += sampanalysis[i];
      stdevi += sampanalysis[i] * sampanalysis[i];  //prep stdevi
    }

    //manual calculation
    averg = averg/analysize;
    stdevi = sqrt(stdevi / analysize - averg * averg); //calculate stdevu
    if (stdevi < 1) { stdevi = 1.0; } //min stdevi of 1
    delta = maxim - minim;

    //**********perform change detection
    if (delta > (stdevi * threshold)){
      change = 1;
    }
    //*********

    if(change){
       // set note and control vector
       int dur = 150+(map(delta%127,1,127,100,2500)); //length of note
       int ramp = 3 + (dur%100) ; //control slide rate, min 25 (or 3 ;)

       //set scaling, root key, note
       int setnote = map(averg%127,1,127,noteMin,noteMax);  //derive note, min and max note
       setnote = scaleNote(setnote, scaleSelect, root);  //scale the note
       // setnote = setnote + root; // (apply root?)
       setNote(setnote, 100, dur, channel);

       //derive control parameters and set
       setControl(controlNumber, controlMessage.value, delta%127, ramp); //set the ramp rate for the control
     }
     //reset array for next sample
    index = 0;
  }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////// GALVANOMETER AND ANALYSIS

////////////////////////////////////////////////////////////////////////////////////////////////////////////// SCALING AND NOTES


// IF IN ARRAY SET NEW NOTE (UNSURE)
void setNote(int value, int velocity, long duration, int notechannel)
{
  //find available note in array (velocity = 0);
  for(int i=0;i<polyphony;i++){
    if(!noteArray[i].velocity){
      //if velocity is 0, replace note in array
      noteArray[i].type = 0;
      noteArray[i].value = value;
      noteArray[i].velocity = velocity;
      noteArray[i].duration = currentMillis + duration;
      noteArray[i].channel = notechannel;

      midiSerial(144, channel, value, velocity);

      if(noteLEDs){
          for(byte j=0; j<(LED_NUM-1); j++) {   //find available LED and set
            if(!leds[j].is_fading()) { rampUp(i, 255, duration);  break; }
          }
      }

      break;
    }
  }
}


// FINDING THE RIGHT NOTES IN THE SCALE
int scaleSearch(int note, int scale[], int scalesize) {
 for(byte i=1; i<scalesize; i++)
 {
  if(note == scale[i])
  {
    return note;
  }
  else
  {
    if(note < scale[i])
    {
      return scale[i];
    }
  } //highest scale value less than or equal to note
  //otherwise continue search
 }
 //didn't find note and didn't pass note value, uh oh!
 return 6;//give arbitrary value rather than fail
}

// ACTUALLY OUTPUTING A MIDI NOTE
int scaleNote(int note, int scale[], int root)
{
  //input note mod 12 for scaling, note/12 octave
  //search array for nearest note, return scaled*octave
  int scaled = note%12;
  // int octave = note/12;
  int octave = 3;
  int scalesize = (scale[0]);
  //search entire array and return closest scaled note
  scaled = scaleSearch(scaled, scale, scalesize);
  scaled = (scaled + (12 * octave)) + root; //apply octave and root
  return scaled;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////// SCALING AND NOTES
