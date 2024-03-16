/*** includes ***/

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctype.h>
#include <errno.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

using namespace std;

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)

enum class ARROW : int { UP = 1000, DOWN = 1001, LEFT = 1002, RIGHT = 1003 };

/*** data ***/

struct editorConfig {
  int cx;
  int cy;
  int screenrows;
  int screencols;
  struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/

// TODO: change to std::string_view eventually.
void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
    die("tcsetattr");
  }
}

void enableRawMode() {
  // Get attribute and put in struct.
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
    die("tcgetattr");
  }
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;
  raw.c_iflag &=
      ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); // Processes \n to \r\n.
  raw.c_oflag &= ~(OPOST);
  // Set char size to 8 bits per byte.
  raw.c_cflag |= (CS8);
  // Bitwise NOT on ECHO(fourth bit), and then bitwise AND to flip only the
  // 4th bit while the rest stays the same.
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

  // How many bytes of input until read returns. cc is 'control characters'.
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1; // In tenths of a second.

  // Set terminal attr back. TCSAFLUSH will wait for all output to be done and
  // discards any input that is not read.
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    die("tcsetattr");
  }
}

int editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  if (c == '\x1b') {
    char seq[3];

    // Handle char wih only escape seq or only 1 char afterwards.
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';

    if (seq[0] == '[') {
      switch (seq[1]) {
      case 'A':
        return static_cast<int>(ARROW::UP);
      case 'B':
        return static_cast<int>(ARROW::DOWN);
      case 'C':
        return static_cast<int>(ARROW::RIGHT);
      case 'D':
        return static_cast<int>(ARROW::LEFT);
      }
    }
    return '\x1b';
  } else {
    return c;
  }
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }

  buf[i] = '\0';
  // // Skip the escape sequence \x1b by printing from 1st index.
  printf("\r\n&buf[1]: '%s'\r\n", &buf[1]);
  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;

  editorReadKey();

  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** append buffer ***/

struct abuf {
  char *b;
  int len;
};

// Represent empty buffer. Serves as constructor for abuf?
// TODO: replace with actual class constructor.
#define ABUF_INIT                                                              \
  { nullptr, 0 }
#define KILO_VERSION "0.0.1"

// TODO: change to just use std::vector.
void abAppend(struct abuf *ab, const char *s, int len) {
  char *newChar = (char *)realloc(ab->b, ab->len + len);

  if (newChar == nullptr) {
    return;
  }
  memcpy(&newChar[ab->len], s, len);
  ab->b = newChar;
  ab->len += len;
}

void abFree(struct abuf *ab) { free(ab->b); }

/*** output ***/
void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    if (y == E.screenrows / 3) {
      char welcome[80];
      int welcomeLen = snprintf(welcome, sizeof(welcome),
                                "Kilo editor -- version %s", KILO_VERSION);
      if (welcomeLen > E.screencols)
        welcomeLen = E.screencols;
      int padding = (E.screencols - welcomeLen) / 2;
      if (padding) {
        abAppend(ab, "~", 1);
        padding--;
      }
      while (padding--)
        abAppend(ab, " ", 1);
      abAppend(ab, welcome, welcomeLen);
    } else {
      abAppend(ab, "~", 1);
    }

    // Clear line after the cursor.
    abAppend(ab, "\x1b[K", 3);
    if (y < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

void editorRefreshScreen() {
  struct abuf ab = ABUF_INIT;

  // Turn off cursor. (Reset mode)
  abAppend(&ab, "\x1b[?25l", 6);
  // Clear screen.
  // abAppend(&ab, "\x1b[2J", 4);

  // Position cursor at top-left corner.
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  abAppend(&ab, buf, strlen(buf));

  // Position cursor at top-left corner after drawing.
  // abAppend(&ab, "\x1b[H", 3);

  // Turn on cursor. (Set mode)
  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);

  abFree(&ab);
}

/*** input ***/
void editorMoveCursor(int key) {
  switch (key) {
    case static_cast<int>(ARROW::LEFT):
    E.cx--;
    break;
  case static_cast<int>(ARROW::RIGHT):
    E.cx++;
    break;
  case static_cast<int>(ARROW::UP):
    E.cy--;
    break;
  case static_cast<int>(ARROW::DOWN):
    E.cy++;
    break;
  }
}

void editorProcessKeypress() {
  int c = editorReadKey();
  switch (c) {
  case CTRL_KEY('q'):
    exit(0);
    break;

  case static_cast<int>(ARROW::UP):
  case static_cast<int>(ARROW::DOWN):
  case static_cast<int>(ARROW::LEFT):
  case static_cast<int>(ARROW::RIGHT):
    editorMoveCursor(c);
    break;
  }
}

/*** init ***/

void initEditor() {
  E.cx = 0;
  E.cy = 0;
  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
}

int main() {
  enableRawMode();
  initEditor();

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
