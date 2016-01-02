/*******************************************************************************

  This Arduino Due program sets up an attached MP23017 to read 8 inputs, and 
  translates those inputs into USB Keyboard commands.

  modifier keys and non-ascii (CTRL, ALT, ESC, etc.)
  https://www.arduino.cc/en/Reference/KeyboardModifiers
  
  Use Keyboard.press() to press and hold to combine modifier
  keys.
  
  Keyboard.write() vs. Keyboard.print()
  write('a') causes 'a' to print, print('a') causes
  the numeric code of 'a' to print.
  
  Libraries:
  https://www.arduino.cc/en/Reference/MouseKeyboard
  https://www.arduino.cc/en/Reference/Serial
  https://www.arduino.cc/en/Reference/Wire

  Some code copied from
  http://www.gammon.com.au/forum/?id=10945
  http://tronixstuff.com/2011/08/26/tutorial-maximising-your-arduinos-io-ports/

*/
#include "Wire.h"

// MCP23017 registers (everything except direction defaults to 0)
#define IODIRA   0x00   // IO direction  (0 = output, 1 = input (Default))
#define IODIRB   0x01
#define IOPOLA   0x02   // IO polarity   (0 = normal, 1 = inverse)
#define IOPOLB   0x03
#define GPINTENA 0x04   // Interrupt on change (0 = disable, 1 = enable)
#define GPINTENB 0x05
#define DEFVALA  0x06   // Default comparison for interrupt on change (interrupts on opposite)
#define DEFVALB  0x07
#define INTCONA  0x08   // Interrupt control (0 = interrupt on change from previous, 1 = interrupt on change from DEFVAL)
#define INTCONB  0x09
#define IOCON    0x0A   // IO Configuration: bank/mirror/seqop/disslw/haen/odr/intpol/notimp
//#define IOCON 0x0B  // same as 0x0A
#define GPPUA    0x0C   // Pull-up resistor (0 = disabled, 1 = enabled)
#define GPPUB    0x0D

#define GPIOA    0x12   // Port value. Write to change, read to obtain value
#define GPIOB    0x13

#define I2C_RIT   0x20 // address of right hand I2C MCP23017 chip
#define I2C_LFT   0x21 // address of left hand I2C MCP23017 chip

#define ONBOARD_LED 13    // pin 13

#define MODE_LETTER 0
#define MODE_NUMBER 1
#define MODE_PUNCT  2

// globals
int mode = MODE_LETTER;

/* set register "reg" on expander (MCP23017) to "data"
 * for example, IO direction
 */
void expanderWriteBoth(const byte chip, const byte reg, const byte data ) 
{
  Wire.beginTransmission(chip);
  Wire.write(reg);
  Wire.write(data);  // port A
  Wire.write(data);  // port B
  Wire.endTransmission();
}

/* debugging */
void printByte(byte b) {
  char s[] = {
    (b & 0x80 ? '1' : '0'),
    (b & 0x40 ? '1' : '0'),
    (b & 0x20 ? '1' : '0'),
    (b & 0x10 ? '1' : '0'),
    (b & 0x08 ? '1' : '0'),
    (b & 0x04 ? '1' : '0'),
    (b & 0x02 ? '1' : '0'),
    (b & 0x01 ? '1' : '0')};
 
  Serial.print(s);
}

void printInputs(const int mode, const byte a, const byte b) {
  Serial.print("mode ");
  switch (mode) {
  case MODE_LETTER:
    Serial.print("LETTER");
    break;
  case MODE_NUMBER:
    Serial.print("NUMBER");
    break;
  case MODE_PUNCT:
    Serial.print("PUNCT");
    break;
  default:
    Serial.print("invalid:");
    Serial.print(mode); 
  }
  Serial.print(" ");
  printByte(a);
  Serial.print(" ");
  printByte(b);
  Serial.println();
}

/*
 Right hand keys: index and middle finger mapped to port A, ring and pinky 
 mapped to port B.
 
 pin  idx contact
 ----------------
 GPA7  7  middle tip
 GPA6  6  middle center
 GPA5  5  middle base
 GPA4  4  index tip
 GPA3  3  index center
 GPA2  2  index base
 GPA1  1  inner palm
 GPA0  0  unused
 
 GPB0  0  ring tip
 GPB1  1  ring center
 GPB2  2  ring base
 GPB3  3  pinky tip
 GPB4  4  pinky center
 GPB5  5  pinky base
 GPB6  6  outer palm
 GPB7  7  unused
 
 */
 
 /*
  14 contacts per hand.
  14 contacts * 2 hands * 3 modes - 1 mode contact = 83 keys.
  
  zareason keyboard has 86 keys.
  
  3 modes: 
    1) letter
    2) number, punctuation
    3) function
    
  14 contacts:
    3 middle, 3 index, inner palm, 3 ring, 3 pinky, outer palm
    
 */
#define SPC ' '     // SPACE
#define BS '\b'     // BACKSPACE
#define MDS 129     // mode switch
// TODO: BKS, ENTER, TAB, SHIFT
// ESC, F1-F12, PageUp, PageDwn, arrow (up, down, right, left),
// CTRL, ALT, DEL
int right_hand_keys[][14] = {
  {SPC, 'h', 'u', 'a',         // inner palm, index finger
        'j', 'p', 'e',         // middle finger
        'i', 'q', 'k',         // ring finter
        'o', 'r', 'l', MDS},   // pinky, outer palm
  {'1', '*', '%', '2', 
        '*', '@', '3',
        '4', '#', '*',
        '5', '^', '*', MDS},
  {',', '}', ':', '.',
        '[', '/', '-',
        '"', '*', ']',
        '_', '!', '\\', MDS}
};
int left_hand_keys[][14] = {
  {'r', 'b', 'c', 's',
        'w', 'd', 't',
        'm', 'f', 'x', 
        'n', 'g', 'y', 'z'},
   {'6', '*' ,'`', '7',
         '*', '~', '8',
         '*', '*', '9',
         '*', '*', '0', MDS},
   {'\'', '+', '?', ')',
         '|', '$', '&', '<', MDS}
};

/*
 closing a contact selects a bit in a port's byte register.  Convert the
 selected bit to a index into the key array. return 0-7 for selections.
 return -1 for no selection.
 
 We ignore multiple selections and return the index of the lowest contact 
 selected.
 */
int toKeyIndex(const byte b) {
  if (b & 0x01) {         // GPx0
    return 0;
  } else if (b & 0x02) {  // GPx1
    return 1;
  } else if (b & 0x04) {  // GPx2
    return 2;
  } else if (b & 0x08) {  // GPx3
    return 3;
  } else if (b & 0x10) {  // GPx4
    return 4;
  } else if (b & 0x20) {  // GPx5
    return 5;
  } else if (b & 0x40) {  // GPx6
    return 6;
  } else if (b & 0x80) {  // GPx7
    return 7;
  } else {
    return -1; 
  }  
}

/* Map tacdata code from MCP23017 I/O register A to keyboard character. */
int tac2keyA(const byte a) {
  const int idx = toKeyIndex(a) - 1; // Port A 0x01 not used on glove
  if (idx >= 0) {
    const int c = right_hand_keys[mode][idx];
    if (c == MDS) {
      mode = (mode + 1) % 3;
      return -1;
    } else {
      return c;
    }
  } else {
    return -1; 
  }
}

/* Map tacdata code from MCP23017 I/O register B to keyboard character. */
int tac2keyB(const byte b) {
  const int idx = toKeyIndex(b) + 7; // Port A 0x01 not used on glove
  if (idx >= 7) {
    const int c = right_hand_keys[mode][idx];
    if (c == MDS) {
      mode = (mode + 1) % 3;
      return -1;
    } else {
      return c;
    }
  } else {
    return -1; 
  }
}

void sendKey(const int c) {
  if (c > 0) {
    Keyboard.write(c);
  }  
}

byte readRegister(const int chip, const byte reg) {
  Wire.beginTransmission(chip); // chip address
  Wire.write(reg); // set MCP23017 memory pointer to GPIOA or GPIOB address
  Wire.endTransmission();
  Wire.requestFrom(chip, 1); // request one byte of data from MCP20317
  return Wire.read();
}

/* the setup function runs once when you press reset or power the board */
void setup() {
  // initialize digital pin 13 as an output for status.
  pinMode(ONBOARD_LED, OUTPUT);
  
  // begin communicating on serial port 0.  the arduino ide can monitor
  // the port over USB.  go to menu "Tools > Serial Monitor" to open
  // the monitor.
  Serial.begin(9600); // https://www.arduino.cc/en/Reference/Serial
  Wire.begin(); // wake up I2C bus
 
  expanderWriteBoth(I2C_RIT, GPPUA, 0xFF);   // enable pull-up resistor for switch - both ports
  expanderWriteBoth(I2C_RIT, IOPOLA, 0xFF);  // invert polarity of signal - both ports 
 
  Keyboard.begin();
  
  delay(500); // delays are needed between each key press
              // so that the receiving computer processes them.
              
  Serial.println("Waiting for input...");
}
  
void loop() {
   // show status on LED
  digitalWrite(ONBOARD_LED, HIGH);
  
  byte inputsA = readRegister(I2C_RIT, GPIOA);            
  byte inputsB = readRegister(I2C_RIT, GPIOB);
 
  printInputs(mode, inputsA, inputsB);
  
  sendKey(tac2keyA(inputsA));    // send USB Keyboard key
  sendKey(tac2keyB(inputsB));
               
  digitalWrite(ONBOARD_LED, LOW); 
  delay(200); // for debounce
}
