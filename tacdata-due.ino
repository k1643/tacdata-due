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
#include "Keyboard.h"

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

#define I2C_RIT   0x27 // address of right hand I2C MCP23017 chip
#define I2C_LFT   0x20 // address of left hand I2C MCP23017 chip

#define ONBOARD_LED 13    // pin 13

#define MODE_LETTER 0
#define MODE_PUNCT  1
#define MODE_NUMBER 2

#define HAND_RIGHT  0
#define HAND_LEFT   1

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

char* modeName(const int mode) {
  switch (mode) {
  case MODE_LETTER:
    return "LETTER";
    break;
  case MODE_PUNCT:
    return "PUNCT";
    break;
  case MODE_NUMBER:
    return "NUMBER";
    break;
  default:
    return "invalid:";
  }
}

void printInputs(const byte hand, const int mode, const byte a, const byte b) {
  if (a == 0 and b == 0) {
    return;  
  }
  if (hand == HAND_RIGHT) {
    Serial.print("Right ");
  } else if (hand == HAND_LEFT) {
    Serial.print("Left ");
  } else {
    Serial.print("Hand Unknown ");
  }
  Serial.print("mode ");
  Serial.print(modeName(mode));
  Serial.print(" ");
  printByte(a);
  Serial.print(" ");
  printByte(b);
  Serial.println();
}

/*
 map glove contact indexes to key codes.  keyboard code constants at
 https://www.arduino.cc/en/Reference/KeyboardModifiers
 */
#define SPC ' '                  // SPACE
#define MDS (KEY_UP_ARROW+1)     // mode switch   
int left_hand_keys[][16] = {
  {-1, 'v', 'b', 'c', 's', 'w', 'd', 't', 'm', 'f', 'x', 'n', 'g', 'y', 'z', MDS},
  {-1,  KEY_TAB, '[', '/', '(', ']', '*', ')', '"', '!', '\\', '\'', '?', '|', '&', MDS},
  {-1, '6', KEY_LEFT_ALT, '~', '7', KEY_ESC, '`', '8', '9', KEY_UP_ARROW, KEY_DOWN_ARROW, '0', KEY_INSERT, KEY_LEFT_GUI, SPC, MDS}
};
int right_hand_keys[][16] = {
  {-1, MDS, 'l', 'r', 'o', 'k', 'q', 'i', 'e', 'p', 'j', KEY_RETURN, 'a', 'u', 'h', SPC},
  {-1, MDS, '>', '=', '+', '$', '_', '-', '.', ':', '}', KEY_LEFT_SHIFT, ',', ';', '{', KEY_BACKSPACE},
  {-1, MDS, KEY_RIGHT_ARROW, '@', '5', KEY_LEFT_ARROW, '%', '4', '3', '>', KEY_DELETE, '*', '2', '<',  KEY_LEFT_CTRL, '1'}
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

int toKeyIndexByPort(const byte b, const byte port) {
  int idx = toKeyIndex(b);
  if (idx < 0) {
    return idx;  // indicates no key selected  
  }
  if (port == GPIOB) {
    idx += 8;
  }
  return idx;
}

/* Map tacdata code from MCP23017 I/O register to keyboard character. 

   reg: port GPIOA or GPIOB
   v:   port's register value
*/
int touch2key(const byte hand, const byte port, const byte v, int* keys) {
  int idx = toKeyIndexByPort(v, port);
  if (idx < 0) {
    return -1;
  }
  if (idx >= 0) {
    Serial.print("mode ");
    Serial.print(modeName(mode));
    Serial.print(" index ");
    Serial.print(idx);
    const int c = keys[idx];
    if (c == MDS) { // mode switch
      mode = (mode + 1) % 3;
      Serial.print("change to mode ");
      Serial.println(modeName(mode));
      return -1;
    } else if (c > KEY_UP_ARROW) {
      Serial.print(" Error key:");
      Serial.println(c);
      return -1;
    } else {
      Serial.print(" key:");
      Serial.print(c);
      if (c >= 32 && c <= 126) {
        Serial.print(" char ");
        Serial.write(c);
      }
      Serial.println();
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

byte readRegister(const byte chip, const byte reg) {
  // Begin a transmission to the I2C slave device with the given address
  Wire.beginTransmission(chip);   // chip address.
  Wire.write(reg);               // set MCP23017 memory pointer to GPIOA or GPIOB address
  const byte status = Wire.endTransmission();        // transmit write() bytes
  if (status != 0) {
    // error.  assume no chip connected.
    return 0;  
  }
  Wire.requestFrom(chip, 1); // request one byte of data from MCP20317
  return Wire.read();
}

void contactsRight() {
  byte inputsA = readRegister(I2C_RIT, GPIOA);            
  byte inputsB = readRegister(I2C_RIT, GPIOB);
 
  printInputs(HAND_RIGHT, mode, inputsA, inputsB);
  
  // send USB Keyboard key
  sendKey(touch2key(HAND_RIGHT, GPIOA, inputsA, right_hand_keys[mode]));
  sendKey(touch2key(HAND_RIGHT, GPIOB, inputsB, right_hand_keys[mode]));
}

void contactsLeft() {
  byte inputsA = readRegister(I2C_LFT, GPIOA);            
  byte inputsB = readRegister(I2C_LFT, GPIOB);
 
  printInputs(HAND_LEFT, mode, inputsA, inputsB);
  
  // send USB Keyboard key
  sendKey(touch2key(HAND_LEFT, GPIOA, inputsA, left_hand_keys[mode]));
  sendKey(touch2key(HAND_LEFT, GPIOB, inputsB, left_hand_keys[mode]));
}

/* the setup function runs once when you press reset or power the board */
void setup() {
  // initialize digital pin 13 as an output for status.
  pinMode(ONBOARD_LED, OUTPUT);
  
  // begin communicating on serial port 0.  the arduino ide can monitor
  // the port over USB.  go to menu "Tools > Serial Monitor" to open
  // the monitor.
  Serial.begin(9600); // https://www.arduino.cc/en/Reference/Serial
  Wire.begin(); // wake up I2C bus as master
 
  expanderWriteBoth(I2C_RIT, GPPUA, 0xFF);   // enable pull-up resistor for switch - both ports
  expanderWriteBoth(I2C_RIT, IOPOLA, 0xFF);  // invert polarity of signal - both ports
  
  expanderWriteBoth(I2C_LFT, GPPUA, 0xFF);
  expanderWriteBoth(I2C_LFT, IOPOLA, 0xFF); 
 
  mode = MODE_LETTER;
  
  Keyboard.begin();
  
  delay(500); // delays are needed between each key press
              // so that the receiving computer processes them.
              
  Serial.println("Waiting for input...");
}
  
void loop() {
   // show status on LED
  digitalWrite(ONBOARD_LED, HIGH);
  
  contactsRight();
  contactsLeft();
                 
  digitalWrite(ONBOARD_LED, LOW); 
  delay(200); // for debounce
}
