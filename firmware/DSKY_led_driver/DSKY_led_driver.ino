/*
Copyright (c) 2021, Riley Rainey

Portions based on code supplied by Ben Krasnow
see https://github.com/benkrasnow/DSKY_EL_replica

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <SPI.h>
#include <Arduino.h>
#include <Wire.h>
#include "wiring_private.h"

#include <SoftWire.h>
#include <AsyncDelay.h>

/*
 * DSKY LED Display Driver for V4 board
 * 
 * This board design includes six Texas Instruments LP5036 LED driver ICs, each capable of directly driving 36 LEDs.
 * LP5036 ICs are controlled via an I2C interface but only support four distinct I2C addresses. Since we're
 * using six chips, we group the LP5036 chips into two banks -- four on bank 0 and two on bank 1. We use a TI TCA9543A as an
 * I2C bank / channel selector.
 *
 * References: [1] TI TCA9543A I2C multiplexor data sheet https://www.ti.com/lit/ds/symlink/tca9543a.pdf?ts=1626710180580
 *             [2] https://www.ti.com/lit/ds/symlink/lp5036.pdf?ts=1613336395802&ref_url=https%253A%252F%252Fwww.ti.com%252Fproduct%252FLP5036
 */

 /*********************************************************************************************
  *  O P E R A T I O N   M O D E S
  * 
  *  Set the op_mode variable to one of the following values to select the desired operation mode.
  * 
  *  For testing, I prefer mode 1, which test each bank of digits and then illuminates each digit 
  *  in a predefined sequence.
  * 
  *  Mode 0 is normally what I set once testing is complete.
  * 
  **********************************************************************************************/

 int op_mode = 1; // 1 = boot in digit test mode: bank test; then illuminate one seven-segment 
                  //     digit at a time
                  // 2 = boot in segment test mode: illuminate one segment at a time sequentially,
                  //     and indicate the number via serial interface. Receiving any byte via serial
                  //     advances to next segment.
                  // 3 = boot in simulator mode.  Upon receiving a byte via USB serial interface,
                  //     cancel simulator mode and accept data via USB only.
                  // 0 = boot in normal operating mode -- the display will be driven by data received
                  //     via the USB serial interface.

/*
 * LED - Macro used to compress the addressing information for each discrete LED.
 * 
 * +--------------+-------------+--------------------------+
 * | bank(4-bits) | ic (2-bits) |   line (6-bits) [0..35]  |
 * +--------------+-------------+--------------------------+
 */

#define LED(bank,ic,line) (unsigned short)((line & 0x3F) | ((ic & 0x3) << 6) | ((bank & 0xF) << 8))

/*
 * An LED group is a set of discrete LEDs; it is identified with bank set to 0xf
 */
#define IS_GROUP  0xf
#define LED_GROUP(group) (unsigned short)(group | (IS_GROUP<<8))

/*
 * This value is used to mean no LED is assigned to this slot.
 */
#define EMPTY  LED_GROUP(63)

// Pin definitions (Arduino board definition largely derived from Trinket M0)

#define PIN_RED_LED        13
#define PIN_TCA9543A_RESET 7  // SAMD21 PA00

/**
 * I2C address for TCA9543A I2C Multiplexor IC
 */
#define TCAADDR 0x70

#define RESET_I2C_MUX_ON_BOOT 1

/**
 * I2C Addressing for the six LP5036 ICs
 * 
 * Mux Bank / I2C Address  / IC Name
 *      0        0x30          U2
 *      0        0x31          U4
 *      0        0x32          U3
 *      0        0x33          U5
 * 
 *      1        0x30          U8
 *      1        0x31          U7
 */

const char PACKET_START = '[';  //Uses this character to indicate start of packet when receiving data via USB serial link

/*
 * LED Group table
 * 
 * The segments of each digit have a single LED illuminating it.  For larger regions on the display, though, groups of LEDS are used.
 * The "group" table defines the discrete LEDs that comprise each group.
 */

#define LED_GROUP_SIZE 8
#define MAX_GROUPS     10

/*
 * List of Groups
 */
#define GROUP_COMP_ACTY 0
#define GROUP_PROG_LAMP 1
#define GROUP_VERB_LAMP 2
#define GROUP_NOUN_LAMP 3
#define GROUP_R1_BAR    4
#define GROUP_R2_BAR    5
#define GROUP_R3_BAR    6
#define GROUP_R1_PLUS   7
#define GROUP_R2_PLUS   8
#define GROUP_R3_PLUS   9

/*
 * Group to discrete LED mapping
 */

const uint16_t group[MAX_GROUPS][LED_GROUP_SIZE] =
  { 
    { LED(0,3,28), LED(0,3,29), LED(0,3,30), LED(0,3,31), LED(0,3,32), LED(0,3,33), LED(0,3,34), LED(0,3,35) },
    { LED(1,0,14), LED(1,0,15), LED(1,0,16), LED(1,0,17), EMPTY, EMPTY, EMPTY, EMPTY },
    { LED(1,0,18), LED(1,0,19), LED(1,0,20), LED(1,0,21), EMPTY, EMPTY, EMPTY, EMPTY },
    { LED(1,0,22), LED(1,0,23), LED(1,0,24), LED(1,0,25), EMPTY, EMPTY, EMPTY, EMPTY },
    { LED(1,1,0), LED(1,1,1), LED(1,1,2), LED(1,1,3), EMPTY, EMPTY, EMPTY, EMPTY },
    { LED(1,1,4), LED(1,1,5), LED(1,1,6), LED(1,1,7), EMPTY, EMPTY, EMPTY, EMPTY },
    { LED(1,1,8), LED(1,1,9), LED(1,1,10), LED(1,1,11), EMPTY, EMPTY, EMPTY, EMPTY },
    { LED(1,0,26), LED(1,0,28), EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY },
    { LED(1,0,29), LED(1,0,31), EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY },
    { LED(1,0,32), LED(1,0,34), EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY }
  };

/*
 * Ben Krasnow's original driver used this table to map logical DSKY display digits to the actual EL Driver lines to
 * be driven to illuminate each segment.  We do something similar here, although this table references the address
 * of both single LEDs and in some cases groups of LEDs that are illuminated as a single unit.
 */

#define TOTAL_DISP_CHAR 25
#define NUM_SEGMENTS 7
#define NUM_PIXELS (TOTAL_DISP_CHAR*NUM_SEGMENTS)

const uint16_t seg_lookup[TOTAL_DISP_CHAR][NUM_SEGMENTS] =
    {     // a, b, c, d, e, f, g
        { LED(0,3,0), LED(0,3,1), LED(0,3,2), LED(0,3,3), LED(0,3,4), LED(0,3,5), LED(0,3,6) },
        { LED(0,3,7), LED(0,3,8), LED(0,3,9), LED(0,3,10), LED(0,3,11), LED(0,3,12), LED(0,3,13) }, 
        { LED(0,3,14), LED(0,3,15), LED(0,3,16), LED(0,3,17), LED(0,3,18), LED(0,3,19), LED(0,3,20) }, 
        { LED(0,3,21), LED(0,3,22), LED(0,3,23), LED(0,3,24), LED(0,3,25), LED(0,3,26), LED(0,3,27) }, 
        { LED(1,0,0), LED(1,0,1), LED(1,0,2), LED(1,0,3), LED(1,0,4), LED(1,0,5), LED(1,0,6) },
        { LED(1,0,7), LED(1,0,8), LED(1,0,9), LED(1,0,10), LED(1,0,11), LED(1,0,12), LED(1,0,13) }, 
        // R1 (upper) Sign Digits
        { EMPTY, LED_GROUP(GROUP_R1_PLUS), EMPTY, EMPTY, EMPTY, EMPTY, LED(1,0,27) },
        //{158,98,158,158,158,158,104},        //6 upper plus/minus  (it's not efficient to use a whole byte for just plus/minus, but creates character consistency and more readable code. The seven-seg decoder function accepts '+' and '-'  
        // R1 (upper) 5 digits
        { LED(0,0,0), LED(0,0,1), LED(0,0,2), LED(0,0,3), LED(0,0,4), LED(0,0,5), LED(0,0,6) },
        { LED(0,0,7), LED(0,0,8), LED(0,0,9), LED(0,0,10), LED(0,0,11), LED(0,0,12), LED(0,0,13) },
        { LED(0,0,14), LED(0,0,15), LED(0,0,16), LED(0,0,17), LED(0,0,18), LED(0,0,19), LED(0,0,20) }, 
        { LED(0,0,21), LED(0,0,22), LED(0,0,23), LED(0,0,24), LED(0,0,25), LED(0,0,26), LED(0,0,27) }, 
        { LED(0,0,28), LED(0,0,29), LED(0,0,30), LED(0,0,31), LED(0,0,32), LED(0,0,33), LED(0,0,34) }, 
        //{158,88,158,158,158,158,89},  //12   middle  plus/minus
        { EMPTY, LED_GROUP(GROUP_R2_PLUS), EMPTY, EMPTY, EMPTY, EMPTY, LED(1,0,30) },
        // R2 (middle) digits
        { LED(0,1,0), LED(0,1,1), LED(0,1,2), LED(0,1,3), LED(0,1,4), LED(0,1,5), LED(0,1,6) },
        { LED(0,1,7), LED(0,1,8), LED(0,1,9), LED(0,1,10), LED(0,1,11), LED(0,1,12), LED(0,1,13) },
        { LED(0,1,14), LED(0,1,15), LED(0,1,16), LED(0,1,17), LED(0,1,18), LED(0,1,19), LED(0,1,20) }, 
        { LED(0,1,21), LED(0,1,22), LED(0,1,23), LED(0,1,24), LED(0,1,25), LED(0,1,26), LED(0,1,27) }, 
        { LED(0,1,28), LED(0,1,29), LED(0,1,30), LED(0,1,31), LED(0,1,32), LED(0,1,33), LED(0,1,34) },
        { EMPTY, LED_GROUP(GROUP_R3_PLUS), EMPTY, EMPTY, EMPTY, EMPTY, LED(1,0,33) }, 
        //{158,29,158,158,158,158,28},         //18  lower  plus /minus
        // R3 (lower) digits
        { LED(0,2,0), LED(0,2,1), LED(0,2,2), LED(0,2,3), LED(0,2,4), LED(0,2,5), LED(0,2,6) },
        { LED(0,2,7), LED(0,2,8), LED(0,2,9), LED(0,2,10), LED(0,2,11), LED(0,2,12), LED(0,2,13) },
        { LED(0,2,14), LED(0,2,15), LED(0,2,16), LED(0,2,17), LED(0,2,18), LED(0,2,19), LED(0,2,20) }, 
        { LED(0,2,21), LED(0,2,22), LED(0,2,23), LED(0,2,24), LED(0,2,25), LED(0,2,26), LED(0,2,27) }, 
        { LED(0,2,28), LED(0,2,29), LED(0,2,30), LED(0,2,31), LED(0,2,32), LED(0,2,33), LED(0,2,34) },  
        { LED_GROUP(GROUP_COMP_ACTY), 
          LED_GROUP(GROUP_PROG_LAMP), 
          LED_GROUP(GROUP_VERB_LAMP), 
          LED_GROUP(GROUP_NOUN_LAMP), 
          LED_GROUP(GROUP_R1_BAR), 
          LED_GROUP(GROUP_R2_BAR), 
          LED_GROUP(GROUP_R3_BAR) }           
        //{166,129,167,138,109,75,164}  //24    special character for boxes and bars on display.  top-to-bottom, left-to-right:  a=COMP ACTY, b=PROG, c=VERB, d=NOUN, e=upper line, f=middle line, g=lower line
    };

/*
 * LP5036 Register Map 
 * accesed via I2C interface
 * see https://www.ti.com/lit/ds/symlink/lp5036.pdf?ts=1613336395802&ref_url=https%253A%252F%252Fwww.ti.com%252Fproduct%252FLP5036
 */
#define DEVICE_CONFIG0      0x00
#define DEVICE_CONFIG1      0x01
#define LED_CONFIG0         0x02
#define LED_CONFIG1         0x03
#define BANK_BRIGHTNESS     0x04
#define LED_BRIGHTNESS_BASE 0x08
#define OUT_COLOR_BASE      0x14
#define RESET               0x38

#define CHIP_EN             0b01000000  // bit on DEVICE_CONFIG0

#define BANK_COUNT          12  // count of LED banks on one IC

#define LED_LINE_COUNT      36  // count of distinct lines on one IC

/*
 * Brightness value sent to the LP5036 chips to illuminate an LED (range 0x00..0xff)
 * On the V2 through V4 board, 0xff corresponds to a 20mA current
 */
unsigned char global_brightness = 0x20;

unsigned char led_brightness = 0x20;

int test_on_ms = 300;
int test_off_ms = 50;

/*
 * Silkscreen label for each LP5036 IC on the physical board
 */
const char * ic_names[] = { "U2", "U4", "U3", "U5", "U8", "U7", "UNK1", "UNK2" };

//TwoWire myWire(&sercom1, PIN_WIRE_SCL, PIN_WIRE_SDA);

SoftWire myWire(SDA, SCL);
char swTxBuffer[64];
char swRxBuffer[16];

void setup() 
{

  pinMode(PIN_TCA9543A_RESET, OUTPUT);
  digitalWrite(PIN_TCA9543A_RESET, HIGH);

  // Red LED on to indicate initialization has started
  pinMode(PIN_RED_LED, OUTPUT);
  digitalWrite(PIN_RED_LED, HIGH);

  // wait max 10 seconds for Serial to connect in any test mode
  uint32_t start_ms = millis();
  uint32_t wait_ms = op_mode == 0 ? 120000 : 10000;
  while (!Serial && (millis()-start_ms < wait_ms)) delay(10);
  delay(500);

  // Red LED off to indicate we have a USB serial connection
  digitalWrite(PIN_RED_LED, LOW);

  // Start I2C
  //myWire.begin();

  myWire.setTxBuffer(swTxBuffer, sizeof(swTxBuffer));
  myWire.setRxBuffer(swRxBuffer, sizeof(swRxBuffer));
  myWire.setClock( 100000 ); // normal mode (100K)
  myWire.setTimeout( 500 );
  myWire.begin();

  // Start USB Serial
  Serial.begin(115200);
  Serial.println("\nLED Driver Ready\n");

  /*
   * Force a hardware reset on the I2C multiplexor?
   */

  if ( RESET_I2C_MUX_ON_BOOT == 1 ) {
    delay(1);
    digitalWrite(PIN_TCA9543A_RESET, LOW);
    delay(1); // datasheet says 500ns minimum
    digitalWrite(PIN_TCA9543A_RESET, HIGH);
    delay(1); // datasheet says "0ns"
  }

  verifyTCAMux();

  // Enable LP5036 ICs
  configureDriverIC(0,0);
  configureDriverIC(0,1);
  configureDriverIC(0,2);
  configureDriverIC(0,3);
  configureDriverIC(1,0);
  configureDriverIC(1,1);

  /*
   * These next two are present on a V4 board
   * however trying to initialize them will help debug
   * PCB issues.
   */
  configureDriverIC(1,2);
  configureDriverIC(1,3);

  delay(50);

  verifyDriverIC(0,0);
  verifyDriverIC(0,1);
  verifyDriverIC(0,2);
  verifyDriverIC(0,3);
  verifyDriverIC(1,0);
  verifyDriverIC(1,1);

  /*
   * These next are not be present on a V4 board
   * however trying to initialize them will help debug
   * PCB issues.
   */
  verifyDriverIC(1,2);
  verifyDriverIC(1,3);
  
}

void verifyTCAMux()
{
  int verified = 0;
  
  selectI2CChannel (0);

  myWire.requestFrom(TCAADDR, 1);

  while (myWire.available()) {
    char c = myWire.read();
    if ((c & 3) == 1) {
      verified |= 1;
    }
  }

  selectI2CChannel (1);

  myWire.requestFrom(TCAADDR, 1);

  while (myWire.available()) {
    char c = myWire.read();
    if ((c & 3) == 2) {
      verified |= 2;
    }
  }

  Serial.print("USB Mux IC test ");
  Serial.print((verified == 3) ? "passed " : "failed ");
  Serial.println();
}

void configureDriverIC(int bank, int ic)
{
  int ic_addr = 0x30 | ic;
  selectI2CChannel (bank);
  myWire.beginTransmission(ic_addr);
  myWire.write((unsigned char) DEVICE_CONFIG0 );
  myWire.write((unsigned char) CHIP_EN );
  myWire.endTransmission();

  // Set Banks 0-7 to independent control mode
  myWire.beginTransmission(ic_addr);
  myWire.write((unsigned char) LED_CONFIG0);
  myWire.write(0x00);                 
  myWire.endTransmission();

  // Set banks 8-11 to independent control mode
  myWire.beginTransmission(ic_addr);
  myWire.write((unsigned char) LED_CONFIG1);
  myWire.write(0x00);                 
  myWire.endTransmission();

  // Global Bank Brightness (0-255)
  myWire.beginTransmission(ic_addr);
  myWire.write((unsigned char) BANK_BRIGHTNESS);
  myWire.write(global_brightness);          // was F0       
  myWire.endTransmission();

  // All LEDs "on"
  int i;
  for(i=0; i<LED_LINE_COUNT; ++i) {
    myWire.beginTransmission(ic_addr);
    myWire.write((unsigned char) OUT_COLOR_BASE+i);
    myWire.write(global_brightness);                 
    myWire.endTransmission();
  }
  delay(1000);

  // All LEDs "off"
  for(i=0; i<LED_LINE_COUNT; ++i) {
    myWire.beginTransmission(ic_addr);
    myWire.write((unsigned char) OUT_COLOR_BASE+i);
    myWire.write(0);                 
    myWire.endTransmission();
  }
}

void verifyDriverIC(int bank, int ic)
{
  int ic_addr = 0x30 | ic;
  selectI2CChannel ( bank );
  
  myWire.beginTransmission(ic_addr);
  myWire.write((unsigned char) DEVICE_CONFIG0 );
  myWire.endTransmission();

  myWire.requestFrom(ic_addr, 1);

  while (myWire.available()) {
    char c = myWire.read();
    Serial.print("LP5036 ");
    Serial.print(ic_names[ic + bank*4]);
    Serial.print(" ");
    Serial.print(c & CHIP_EN ? "enabled" : "not enabled");
    Serial.println();
  }

}

void setLEDState( uint16_t lamp, boolean state) {
 
  int bank = lamp >> 8;
  int ic = (lamp >> 6) & 0x3;
  int line = lamp & 0x3f;

  // LED Group?
  if (bank == IS_GROUP) {
    
    // Not "EMPTY" group? activate all LEDs in the group
    if (line != 0x3f) {
      for (int i=0; i<LED_GROUP_SIZE; ++i) {
        uint16_t led = group[line][i];
        if (led != EMPTY) {
          setLEDState( led, state );
        }
      }
    }
  }
  else {
    selectI2CChannel( bank );
    int ic_addr = 0x30 | ic;
    myWire.beginTransmission(ic_addr);
    myWire.write((unsigned char) OUT_COLOR_BASE+line);
    myWire.write(state ? led_brightness : 0);                 
    myWire.endTransmission();
  }
}

void DSKY_set_char(int char_position, char input_char, int br)
{

    if( input_char & 0x80)  //most significant bit is one, so use the following seven bits to set the segments directly
      {
          setLEDState(seg_lookup[char_position][0], (input_char & 0x01));  //a
          setLEDState(seg_lookup[char_position][1], (input_char & 0x02));  //b
          setLEDState(seg_lookup[char_position][2], (input_char & 0x04));  //c
          setLEDState(seg_lookup[char_position][3], (input_char & 0x08));  //d
          setLEDState(seg_lookup[char_position][4], (input_char & 0x10));  //e
          setLEDState(seg_lookup[char_position][5], (input_char & 0x20));  //f
          setLEDState(seg_lookup[char_position][6], (input_char & 0x40));  //g
      }

    else  //If we received a standard ASCII character, parse it as a readable character
      {
        switch(toupper(input_char)) 
          {
             case ' ':
              setLEDState(seg_lookup[char_position][0], false);  //a
              setLEDState(seg_lookup[char_position][1], false);  //b
              setLEDState(seg_lookup[char_position][2], false);  //c
              setLEDState(seg_lookup[char_position][3], false);  //d
              setLEDState(seg_lookup[char_position][4], false);  //e
              setLEDState(seg_lookup[char_position][5], false);  //f
              setLEDState(seg_lookup[char_position][6], false);  //g
             break;
             
            case '0':
              setLEDState(seg_lookup[char_position][0], true);  //a
              setLEDState(seg_lookup[char_position][1], true);  //b
              setLEDState(seg_lookup[char_position][2], true);  //c
              setLEDState(seg_lookup[char_position][3], true);  //d
              setLEDState(seg_lookup[char_position][4], true);  //e
              setLEDState(seg_lookup[char_position][5], true);  //f
              setLEDState(seg_lookup[char_position][6], false); //g
             break;
    
            case '1':
              setLEDState(seg_lookup[char_position][0], false);  //a
              setLEDState(seg_lookup[char_position][1], true);   //b
              setLEDState(seg_lookup[char_position][2], true);   //c
              setLEDState(seg_lookup[char_position][3], false);  //d
              setLEDState(seg_lookup[char_position][4], false);  //e
              setLEDState(seg_lookup[char_position][5], false);  //f
              setLEDState(seg_lookup[char_position][6], false);  //g
             break;
    
            case '2':
              setLEDState(seg_lookup[char_position][0], true);  //a
              setLEDState(seg_lookup[char_position][1], true);  //b
              setLEDState(seg_lookup[char_position][2], false); //c
              setLEDState(seg_lookup[char_position][3], true);  //d
              setLEDState(seg_lookup[char_position][4], true);  //e
              setLEDState(seg_lookup[char_position][5], false); //f
              setLEDState(seg_lookup[char_position][6], true);  //g
             break;
    
    
             case '3':
              setLEDState(seg_lookup[char_position][0], true);  //a
              setLEDState(seg_lookup[char_position][1], true);  //b
              setLEDState(seg_lookup[char_position][2], true);  //c
              setLEDState(seg_lookup[char_position][3], true);  //d
              setLEDState(seg_lookup[char_position][4], false); //e
              setLEDState(seg_lookup[char_position][5], false); //f
              setLEDState(seg_lookup[char_position][6], true);  //g
             break;
    
             case '4':
              setLEDState(seg_lookup[char_position][0], false); //a
              setLEDState(seg_lookup[char_position][1], true);  //b
              setLEDState(seg_lookup[char_position][2], true);  //c
              setLEDState(seg_lookup[char_position][3], false); //d
              setLEDState(seg_lookup[char_position][4], false); //e
              setLEDState(seg_lookup[char_position][5], true);  //f
              setLEDState(seg_lookup[char_position][6], true);  //g
             break;
    
             case '5':
              setLEDState(seg_lookup[char_position][0], true);  //a
              setLEDState(seg_lookup[char_position][1], false); //b
              setLEDState(seg_lookup[char_position][2], true);  //c
              setLEDState(seg_lookup[char_position][3], true);  //d
              setLEDState(seg_lookup[char_position][4], false); //e
              setLEDState(seg_lookup[char_position][5], true);  //f
              setLEDState(seg_lookup[char_position][6], true);  //g
             break;
    
             case '6':
              setLEDState(seg_lookup[char_position][0], true);  //a
              setLEDState(seg_lookup[char_position][1], false); //b
              setLEDState(seg_lookup[char_position][2], true);  //c
              setLEDState(seg_lookup[char_position][3], true);  //d
              setLEDState(seg_lookup[char_position][4], true);  //e
              setLEDState(seg_lookup[char_position][5], true);  //f
              setLEDState(seg_lookup[char_position][6], true);  //g
             break;
    
             case '7':
              setLEDState(seg_lookup[char_position][0], true);   //a
              setLEDState(seg_lookup[char_position][1], true);   //b
              setLEDState(seg_lookup[char_position][2], true);   //c
              setLEDState(seg_lookup[char_position][3], false);  //d
              setLEDState(seg_lookup[char_position][4], false);  //e
              setLEDState(seg_lookup[char_position][5], false);  //f
              setLEDState(seg_lookup[char_position][6], false);  //g
             break;
    
             case '8':
              setLEDState(seg_lookup[char_position][0], true);  //a
              setLEDState(seg_lookup[char_position][1], true);  //b
              setLEDState(seg_lookup[char_position][2], true);  //c
              setLEDState(seg_lookup[char_position][3], true);  //d
              setLEDState(seg_lookup[char_position][4], true);  //e
              setLEDState(seg_lookup[char_position][5], true);  //f
              setLEDState(seg_lookup[char_position][6], true);  //g
             break;
             
            case '9':
              setLEDState(seg_lookup[char_position][0], true);   //a
              setLEDState(seg_lookup[char_position][1], true);   //b
              setLEDState(seg_lookup[char_position][2], true);   //c
              setLEDState(seg_lookup[char_position][3], false);  //d
              setLEDState(seg_lookup[char_position][4], false);  //e
              setLEDState(seg_lookup[char_position][5], true);   //f
              setLEDState(seg_lookup[char_position][6], true);   //g
             break;
    
            case '+':
              setLEDState(seg_lookup[char_position][0], false);  //a
              setLEDState(seg_lookup[char_position][1], true);   //b
              setLEDState(seg_lookup[char_position][2], false);  //c
              setLEDState(seg_lookup[char_position][3], false);  //d
              setLEDState(seg_lookup[char_position][4], false);  //e
              setLEDState(seg_lookup[char_position][5], false);  //f
              setLEDState(seg_lookup[char_position][6], true);   //g
             break;
    
            case '-':
              setLEDState(seg_lookup[char_position][0], false);  //a
              setLEDState(seg_lookup[char_position][1], false);  //b
              setLEDState(seg_lookup[char_position][2], false);  //c
              setLEDState(seg_lookup[char_position][3], false);  //d
              setLEDState(seg_lookup[char_position][4], false);  //e
              setLEDState(seg_lookup[char_position][5], false);  //f
              setLEDState(seg_lookup[char_position][6], true);   //g
             break;
    
           case 'a':  //pretty arbitrary.  This switch case could be extended to show lots of combinations other than numbers
              setLEDState(seg_lookup[char_position][0], true);  //a
              setLEDState(seg_lookup[char_position][1], true);  //b
              setLEDState(seg_lookup[char_position][2], true);  //c
              setLEDState(seg_lookup[char_position][3], true);  //d
              setLEDState(seg_lookup[char_position][4], true);  //e
              setLEDState(seg_lookup[char_position][5], true);  //f
              setLEDState(seg_lookup[char_position][6], true);  //g
             break;
          }
     }
      
}

void DSKY_format_2dig(char * s, int intval)
  {
    if (intval < 0)
      {
        snprintf( s, 3, "00");
      }
    else if ( intval > 99)
      {
        snprintf( s, 3, "00");
      }
      
    else 
      {
        snprintf(s, 3, "%02d", intval);
      }
  }


void DSKY_format_5dig(char * s, int intval)
{
    if( intval > 99999)
      {
        intval = 99999;
      }
    else if( intval < -99999)
      {
        intval = -99999;
      }
      
    if(intval <0)
          {
            snprintf(s, 2, "-");
          }
    else
          {
            snprintf(s,2,"+");
          }
       
    snprintf(s+1,6,"%05d",abs(intval));
 }


void loop() {
  
  static int char_pos = 0;
  static int i, j;
  
  //only used for op_mode 3
  static unsigned long ProgTimer, NounTimer, VerbTimer, angTimer;
  static float tmpangle = 1.4;
  static int NounVal, VerbVal, ProgVal,TopVal, MidVal, BotVal;
  static int SpecVal = 8;
  static char tmpStr[] = "123456789123456789123456789";

  switch (op_mode) {
  case 1:

    for (i=0; i<TOTAL_DISP_CHAR; ++i) {
      const uint16_t *p;
      p = &seg_lookup[i][0];
      for (j=0; j<NUM_SEGMENTS; j++) {
        setLEDState(*(p+j), true);
      }

      int k = i-1;
      if (k < 0) {
        k = TOTAL_DISP_CHAR - 1;
      }
      p = &seg_lookup[k][0];
      for (j=0; j<NUM_SEGMENTS; j++) {
        setLEDState(*(p+j), false);
      }

      delay(1000);
    }
    break;

  case 2:
    
    for (i = 0; i<NUM_PIXELS; i++) {

      int m0, m1;
      int j = i-1;
      if (i == 0) {
        j = NUM_PIXELS - 1;
      }

      m0 = i % 7;
      m1 = i / 7;
      setLEDState( seg_lookup[m1][m0], true);

      m0 = j % 7;
      m1 = j / 7;
      setLEDState( seg_lookup[m1][m0], false);

      delay(300);
     
      //Serial.println(i);
      //while(!Serial.available());
      //Serial.read();
    }
    break;

  case 3:
    if (millis() - ProgTimer > 1000)
        {
          ProgVal = (ProgVal > 99) ? 0 : ProgVal + 5;
          ProgTimer = millis();
        }
        
    if (millis() - NounTimer > 500)
        {
          NounVal = (NounVal > 99) ? 0 : NounVal + 2;
          NounTimer = millis();
        }
      
    if (millis() - VerbTimer > 100)
        {
          VerbVal = (VerbVal > 99) ? 0 : VerbVal + 1;
           VerbTimer = millis();
        }
          
          
    if( millis() - angTimer > 70)
        {
          tmpangle = (tmpangle > 6.283) ? 0 : tmpangle + 0.001;
          angTimer = millis();
        }
        
      
     TopVal = (sin(tmpangle) * 100000.0);
     MidVal = (cos(tmpangle) * 100000.0);

    //TopVal = 55555;
    //MidVal = -12345;
    //BotVal = 0;

     DSKY_format_2dig(tmpStr, ProgVal);
     DSKY_format_2dig(tmpStr+2, VerbVal);
     DSKY_format_2dig(tmpStr+4, NounVal);
     DSKY_format_5dig(tmpStr+6, TopVal);
     DSKY_format_5dig(tmpStr+12, MidVal);
     DSKY_format_5dig(tmpStr+18, BotVal); 
     tmpStr[24] = '8';
          
      for(i = 0; i<TOTAL_DISP_CHAR;i++)
        {
          DSKY_set_char(i,tmpStr[i],global_brightness);
        }
    
    if (Serial.available())
      {
        op_mode = 0;
      }
    delay(10);
    break;

  case 0:

    /*
     * op_mode 0 is the normal mode for processing directives from 
     * the USB serial interface
     */
    while(Serial.available()) {
      
      char c = Serial.read();
      if (c == PACKET_START) {
        char_pos = 0;
      }
      else {  
        DSKY_set_char( char_pos,c, led_brightness );
        char_pos = (++char_pos == TOTAL_DISP_CHAR) ? 0 : char_pos;
        op_mode = 0;
      }
      
    }

    delay(2);
    break;
  }
}

/**
 * TCA I2C Channel Select
 * 
 * i - Select Channel 0 or 1
 * See reference [1], page 15
 */
static uint8_t curChannel = 255;

void selectI2CChannel(uint8_t i) {

  int result;
  
  if (i > 1) return;

  // Channel not what we already have previously selected? Change it
  
  if (i != curChannel) {
 
    myWire.beginTransmission(TCAADDR);

    // 0x01 == enable i2c channel 0
    // 0x02 == enable i2c channel 1
    
    myWire.write ((uint8_t) (1 << i));

    result = myWire.endTransmission();

    if (result != 0) {
      Serial.print("selectI2CChannel endTransmission; result = "); Serial.println(result);
    }
    else {
      curChannel = i;
    }
  }
}
