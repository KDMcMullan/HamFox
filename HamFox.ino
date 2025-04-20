/****************************************************************************
 *
 * HamFox
 *
 * A Timer-triggered Voice transmitter for Amateur Radio Foxhunts
 *
 * Ken McMullan 2E0UMK
 *
/****************************************************************************
 *
 * History
 *
 * This is a rehash of a program I wrote a while ago to receive DTMF
 * sequences from a Ham radio, and retransmit those as MQTT packets. One of
 * the incidental features of same was the ability to periodically transmit 
 * the owner's callsign. It's that feature which has been isolted here. This
 * is a standalone program to broadcast a voice message periodically, such
 * that the transmitter can be sought, by qppropriately equipped individuals, 
 * as part of a ham radio "fox hunt". 
 *
 * Taking advantage of another core function of teh predeessor, this "fox"
 * can be remotely controlled by sending it DTMF sequences. 
 *
 * Initially, the arduino will braodcast in the voice of a 1980's Speak 'n'
 * Spell speech synthesiser, thanks to
 * https://github.com/earlephilhower/ESP8266Audio but the library supports
 * the playing of sampled audio files: an obvious evolution.
 *
 * v0.90.01 20250420
 * First pass. Removed all the uneccessary code for MQTT (which I'll
 * probably regret), added the necessities for regular retransmit, and a
 * few of the basic DTMD commands. Tested the regular transmit. The DTMF
 * receive code is not tested, but I think it's a hardware fault. 
 *
/****************************************************************************
 *
 * To Do
 *
 * Periodically transmit a message. Suggestion is "This is <callsign> slash
 * Foxtrot Hotel. This frequency is in use for a fox hunt" x2. This would be
 * followed by a number of repititions (8x) of "Find the fox" to make the
 * total transmit time up to about 30 seconds. The message would terminate
 * with "Over to you."
 * 
 * This process would restart every 90 seconds.
 *
 * Prposed controllable features are:
 *  - count of repetitions of the secondary message
 *  - the total time (in seconds) between the start of each broadcast
 *  - zero for "off"
 *  
 * Other proposed commands:
 *  - command to query the above.
 *  - command to instigae one of a number of other
 *
 * 
 * 
 * 
/****************************************************************************
 *
 * Notes:
 *
 * Don't forget that the connected radio must have the volume up. A setting
 * between 1/2 and 3/4 is suggested.
 *
/****************************************************************************/

#include "AudioOutputI2SNoDAC.h"
#include "ESP8266SAM.h"

#include "kTimer.h"

// Complete Pin Ref: https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/

#define pQ1 15   // D8, bit 0 input (boot fails if high)
#define pQ2 13   // D7, bit 1 input (no special requirements)
#define pQ3 4    // D2, bit 2 input (no special requirements)
#define pQ4 14   // D5, bit 3 input (no special requirements)
#define pSTQ 16  // D0, steering input (high at boot. high output?)
#define pPTT 12  // D6, push-to-talk via optocoupler (chosen for pull-down)

// #define pAO  5         // Analog out = GPIO 5 = pin D1 on D1 mini

#define pD1 5  // reserved for AO
#define pD3 0  // (Avoid this: needs to be zero during flash)
#define pD4 2  // (Avoid this: it's the built-in LED)

#define pLED LED_BUILTIN  // onboard LED

#define pAn 36  // from https://www.upesy.com/blogs/tutorials/measure-voltage-on-esp32-with-adc-with-arduino-code

// Tx GPIO1 debug output at boot, fails if pulled low
// Rx GPIO3

//--------------------------------------
// Factory Default Configs
//--------------------------------------

#define DTMFlen    8   // max length of user DTMF input
#define voiceLen 100   // max length (characters) of voice response


//--------------------------------------
// Configuration Values
//--------------------------------------

#define flashTime       250  // ms of half period of flash (250ms = 2Hz)
#define charTime       3000  // ms between DTMF characters before timeout (3 sec)
#define autoCallTime  60000  // ms between callsign auto send (1 min)


//--------------------------------------
// DTMF Comand Strings
//--------------------------------------

#define cmdStart "001"  // DTMF sequence to start the fox
#define cmdStop  "000"  // DTMF sequence to stop the fox

//--------------------------------------
// Language
//--------------------------------------

// NB the following are in something of a phonetic spelling as the English
// (UK) spellings of words did not always seem to pronouce well.

const char* MsgOpen =  "This is 2 echo 0 uniform mike, keelo, slash foxtrot hotel.";
const char* MsgUse =   "This frequency, is in yous, for a fox hunnt.";
const char* MsgFox =   "Find the fox.";
const char* MsgOver =  "Over to, you.";
const char* MsgQSL =   "ku ess ell";                    // No QSL
const char* MsgNoQSL = "no ku ess ell";                 // No QSL


//--------------------------------------
// Timer Objects
//--------------------------------------

kTimer TimerFlash(flashTime);
kTimer TimerChar(charTime);
kTimer TimerAutoCall(autoCallTime);


//--------------------------------------
// Other
//--------------------------------------

unsigned char newCh;                  // DTMF character
unsigned char DTMFpos;                // index into DTMF string
bool relayOpen = false;               // flag that the relay is open
bool OnLED;                           // Flashing LED status
bool foxRunning = true;               // Running at powerup by default
bool parse = false;                   // there is a received message to parse
char DTMFstr[DTMFlen + 1];            // read DTMF string including nul

char VoiceMsg[voiceLen];  // string to be spoken

char* conv = (char*)malloc(20);  // globally allocated space for string conversions

unsigned int CntQSL = 0;    // count of DTMFs in open mode
unsigned int CntNoQSL = 0;  // count of DTMFs in closed mode

enum DTMFmode { DTMF_IDLE,
                DTMF_RX,
                DTMF_WAIT,
                DTMF_FULL,
                DTMF_END };
enum DTMFmode DTMFmode = DTMF_IDLE;

enum LEDMode { LED_OFF,
               LED_ON,
               LED_FLASH };
enum LEDMode LEDmode = LED_OFF;

// These are the characters we store when the given numbers are received by
// the MT8870 module. The order seems odd, but the fact that 0 = D, for
// comes directly from the data sheet.
char numStr[] = "D1234567890*#ABC";


//--------------------------------------
// globals derived from libraries
//--------------------------------------

AudioOutputI2SNoDAC* out = NULL;


//--------------------------------------
// Press or release PTT.
//--------------------------------------

void PTT(bool Tx = true) {

  if (Tx) {
    digitalWrite(pPTT, HIGH);   // Turn PTT on
    digitalWrite(pLED, LOW);  // Turn on LED (no point using LEDmode as the voice holds the process loop)
    delay(500);                // Let the radio spin up!
  } else {
    digitalWrite(pPTT, LOW);
    digitalWrite(pLED, LOW);

  } // if Tx
  
}  // PTT


//--------------------------------------
// Say the supplied string.
//--------------------------------------

void vocalise(const char* phrase) {

  ESP8266SAM* sam = new ESP8266SAM;
  sam->Say(out, phrase);
  delete sam;

  delay(100);

}  // vocalise


//--------------------------------------
// Press PTT, pause, say supplied string then release.
//--------------------------------------

void transmit(const char* phrase) {

  PTT(true);

  vocalise(phrase);
  
  PTT(false);
  
}  // transmit


//--------------------------------------
// Convert integer to string
//--------------------------------------

char* int_str(int num) {
  sprintf(conv, "%d", num);
  return conv;
}  // int_str


//--------------------------------------
// main arduino setup fuction called once
//--------------------------------------

void setup() {

  // This must come before the pinmode for pQ1.
  // Something to do with the way the registers mess about with things.
  out = new AudioOutputI2SNoDAC();  // No DAC implementation uses Serial port Rx pin

  pinMode(pQ1, INPUT);  // DTMF decoder pins
  pinMode(pQ2, INPUT);
  pinMode(pQ3, INPUT);
  pinMode(pQ4, INPUT);
  pinMode(pSTQ, INPUT);

  pinMode(pPTT, OUTPUT);
  pinMode(pLED, OUTPUT);

  digitalWrite(pPTT, LOW);  // Turn PTT off

  Serial.begin(9600);
  delay(100);

  Serial.print("Powered Up.");
  transmit("Fox powerd up.");  // debug

}  // setup

//--------------------------------------
// main arduino loop fuction called continuously
//--------------------------------------

void loop() {

  switch (DTMFmode) {

    case DTMF_IDLE:
      if (digitalRead(pSTQ)) {  // STQ has gone high
        DTMFpos = 0;           // reset string pointer
        LEDmode = LED_FLASH;   // indicates receiving
        DTMFmode = DTMF_RX;
        Serial.print("Rx: ");
      }
      break;  // DTMF_IDLE

    case DTMF_RX:
      if (!digitalRead(pSTQ)) {  // wait for STQ to go low
        TimerChar.reset();

        // convert the IO bits into a decimal number then into a string
        newCh = (digitalRead(pQ1) | (digitalRead(pQ2) << 1) | (digitalRead(pQ3) << 2) | (digitalRead(pQ4) << 3));
        DTMFstr[DTMFpos] = numStr[newCh];  // look it up and store it

        Serial.print(DTMFstr[DTMFpos]);

        DTMFpos++;  // increment string pointer

        if (DTMFpos >= DTMFlen) {  // is input buffer full?
          DTMFmode = DTMF_FULL;
        } else {
          DTMFmode = DTMF_WAIT;
        }

      }       // if STQ went low
      break;  // DTMF_RX

    case DTMF_WAIT:
      if (TimerChar.expired()) {  // inter-character time expired
        LEDmode = LED_OFF;
        DTMFmode = DTMF_END;
        Serial.print(" TIMEOUT WITH: ");
      } else {
        if (digitalRead(pSTQ)) { DTMFmode = DTMF_RX; }  // STQ has gone high
      }
      break;  // DTMF_WAIT

    case DTMF_FULL:
      Serial.print(" BUFFER FULL WITH: ");
      DTMFmode = DTMF_END;
      break;  // DTMF_FULL

    case DTMF_END:
      DTMFstr[DTMFpos] = 0;  // terminator
      Serial.println(DTMFstr);
      parse = true;
      DTMFmode = DTMF_IDLE;
      LEDmode = LED_OFF;  // indicates waiting
      break;              // DTMF_END

  }  // switch DTMFmode

  if (parse) {
    parse = false;  // mark as parsed
    Serial.print("PARSING... ");
    if (!strcmp(DTMFstr, cmdStart)) {  // DTMF sequence for "start" received
      CntQSL += 1;
      TimerAutoCall.reset();           // reset the open timer
      foxRunning = true;               // fox isrunning
      Serial.println("Fox running");
      transmit(MsgQSL);                // Tx acknowledgement (actually maybe we won't?)

    } else if (!strcmp(DTMFstr, cmdStop)) {  // DTMF sequence for "stop" received
      CntQSL += 1;
      foxRunning = false;              // fox is stopped
      Serial.println("Fox stopped");
      transmit(MsgQSL);                // Tx acknowledgement (actually maybe we won't?)

    } else {  // some other DTMF sequence received

      // the thing we do with messages goes here
      CntNoQSL += 1;
      Serial.print("MESSAGE: ");
      Serial.print(DTMFstr);

      transmit(MsgNoQSL);

    }  // switch DTMFstr

  }  // if parse

  if (TimerAutoCall.expired()) {  // check last time fox transmitted
    TimerAutoCall.reset();        // don't need to check until it expires again
    if (foxRunning) {
      Serial.print("Sending Channel in use... ");
      PTT();
      vocalise(MsgOpen);
      vocalise(MsgUse);
      delay(1000);
      vocalise(MsgFox);
      delay(1000);
      vocalise(MsgFox);
      delay(1000);
      vocalise(MsgFox);
      delay(1000);
      vocalise(MsgOver);
      PTT(false);
      Serial.println("SENT");
    } else {                // fox not running
      Serial.println("Autocall, with fox not running.");
    }  
  }                           // callsign time has expired

  if (TimerFlash.expired()) {  // is it time to toggle the flash?
    TimerFlash.reset();
    switch (LEDmode) {  // refresh LED (ESP8266 LED is active Low, so high = off)
      case LED_OFF:
        digitalWrite(pLED, HIGH);
        break;
      case LED_ON:
        digitalWrite(pLED, LOW);
        break;
      case LED_FLASH:
        OnLED = !OnLED;
        digitalWrite(pLED, OnLED);
        break;
    }  // LEDmode
  }    // if FlashStart

}  // loop
