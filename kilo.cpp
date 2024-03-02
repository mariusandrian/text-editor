/*** includes ***/

#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <iostream>

using namespace std;

/*** data ***/

struct termios orig_termios;

/*** terminal ***/

// TODO: change to std::string_view eventually.
void die(const char *s) {
  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
    die("tcsetattr");
  }
}

void enableRawMode() {
  // Get attribute and put in struct.
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
    die("tcgetattr");
  }
  atexit(disableRawMode);

  struct termios raw = orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);  // Processes \n to \r\n.
  raw.c_oflag &= ~(OPOST);
  // Set char size to 8 bits per byte.
  raw.c_cflag |= (CS8);
  // Bitwise NOT on ECHO(fourth bit), and then bitwise AND to flip only the
  // 4th bit while the rest stays the same.
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

  // How many bytes of input until read returns. cc is 'control characters'.
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1; //In tenths of a second.
  
  // Set terminal attr back. TCSAFLUSH will wait for all output to be done and
  // discards any input that is not read.
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    die("tcsetattr");
  }
}

/*** init ***/

int main() {
  enableRawMode();

  while(1) {
    char c = '\0';
    read(STDIN_FILENO, &c, 1);
    if (!iscntrl(c)) {
      cout << c << "\r\n";
    } else {
      printf("%d ('%c')\r\n", c, c); 
    }
    if (c == 'q') break;
  }
  
  return 0;
}
