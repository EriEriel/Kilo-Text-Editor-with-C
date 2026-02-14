/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

//Set upper 3 bits of character(1 byte) to 0. mimic Ctrl press
#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/

//store editor state in E
struct editorConfig {
  int screenrows;
  int screencols;
  struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  //When C library function fail it will set global errno variable to indicate the error
  //perror() looks at the global errno and prints a descriptive error message for it.
  perror(s);
  //exit programe with status 1 (indicate failure)
  exit(1);
}

void disableRawmode(void) {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcgetattr");
  die("tcsetattr");
}

void enableRawMode(void) {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcsetattr");
  atexit(disableRawmode);
  //Turn ECHO mode off. ECHO feature causes each key type to be print terminal but it get in a way when try to carefully render UI in rawmode.
  struct termios raw = E.orig_termios;
  //Disable Ctrl-S and Ctrl-Q which is software flowcontrol, now both can read as 19 byte and 17 byte
  //Disable Ctrl-J however Ctrl-M still act weird and all other miscellaneous flags
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  //Disable all output processing "\n" and "\r\n"
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag &= ~(CS8);
  //Disable echo and canonical mode via bitwise operation so we can read input byte by byte 
  //ISIG here also disable Ctrl-C and Ctrl-Z
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  //VMIN is minimum number of byte before read() and VTIME is set maximum time to wait before read() set to 1 = 100 millisec
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  //TCSAFLUSH discard any unread input before applying the change to the terminal
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}
//return keypress
char editorReadkey(void) {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  return c;
}

//Ancient Unix terminal magic!! this getCursorPosition and store in pointer (*rows and *cols)
int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;
  // \x1b[6n -> ESC + 6n(report cursor position)
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
  //read 1 byte at time and store in buf. stop when detect R, buf is almost full or fail 
  while (i < sizeof(buf) -1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';
  //check if this is really ESC sequences, if not abort
  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  //parse the number in &buf[2] (skip ESC[) %d;%d -> rows;column
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

  return 0;

}

//get the size of terminal to store in struct data
int getWindowSize(int *rows, int *cols) {
  struct winsize ws;
  //ioctl() place the number of column wide and number of rows high the terminal is into given winsize struct if fail return -1
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    //fallback to get screensize by using command C(cursor forward) and B(cursor down) by 999 to ensure that it get to right corner of the screen
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
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

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  free(ab->b);
}

/*** output ***/
// draw ~ in the begining of the line by the size of window
void editorDrawRows(void) {
  int y; 
  for (y = 0; y < E.screenrows; y++) {
    write(STDOUT_FILENO, "~", 1);
    // draw ~ at last line
    if (y < E.screenrows - 1) {
      write(STDOUT_FILENO, "/r/n", 2);
    }
  }
}

void editorRefreshScreen(void) {
  // \x1b or 27 in decimal is a escape character. [ is combine so this is J command mostly form VT100 escape sequences
  write(STDOUT_FILENO, "\x1b[2J", 4);
  // 3 and 4 is bytes, this is H command to position the cursor to top-left corner.
  write(STDOUT_FILENO, "\x1b[H", 3);

  editorDrawRows();

  write(STDOUT_FILENO, "\x1b[H", 3);
}


/*** input ***/

void editorProcessKeypress(void) {
  char c = editorReadkey();

  switch (c) {
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;
  }
}

/*** init ***/

void initEditor(void) {
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(void) {
  enableRawMode();
  initEditor();
  //it read 1 byte from standard input the into variable c and compare to 1(which is 1 byte of char)
  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}

//currently on step 37 . working on process
