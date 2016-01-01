/*


  modifier keys and non-ascii (CTRL, ALT, ESC, etc.)
  https://www.arduino.cc/en/Reference/KeyboardModifiers
  
  Use Keyboard.press() to press and hold to combine modifier
  keys.
  
  Keyboard.write() vs. Keyboard.print()
  write('a') causes 'a' to print, print('a') causes
  the numeric code of 'a' to print.
  
  First KEY_RETURN have no effect.  but cause following
  keys to not print.
  
*/

void setup() {
  Keyboard.begin();
  delay(500); // delays are needed between each keypress
              // so that the receiving computer processes them.
  
  // select all text in current document and erase.
  Keyboard.press(KEY_LEFT_CTRL);
  Keyboard.press('a');
  delay(500);
  Keyboard.releaseAll();
  // delete the selected text
  Keyboard.write(KEY_BACKSPACE);
  delay(500);
  
  Keyboard.write(KEY_RETURN);
  delay(500);
  
  int c = '0';
  for (int i = 0; i < 10; i++) {
    Keyboard.write(c + i);
    delay(500);  // waits milliseconds
  }
  
  Keyboard.write(KEY_RETURN);
  
  c = 'a';
  for (int i = 0; i < 26; i++) {
    Keyboard.write(c + i);
    delay(500);  // waits milliseconds
  }
  Keyboard.end();
}
  
void loop() {
}
