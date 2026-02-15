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

#define KILO_VERSION "0.0.1"

//Set upper 3 bits of character(1 byte) to 0. mimic Ctrl press
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
  //set int to 1000 because this out of range of any character, prevent conflict
  ARROW_LEFT  = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN
};

/*** data ***/

//store editor state in E
struct editorConfig {
  int cx, cy; //cursor position
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
 int editorReadKey(void) {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }

  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    if (seq[0] == '[') {
      switch (seq[1]) {
        case 'A': return ARROW_UP;
        case 'B': return ARROW_DOWN;
        case 'C': return ARROW_RIGHT;
        case 'D': return ARROW_LEFT;
      }
    }

    return '\x1b';
  } else {
  return c;
  }
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
//previously,we redraw the line with many small write() but now we want to write() only once so first we built buffer
//create dynamic string
struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

// append s to abuf and then use realloc() to re-allocate memory to hold new string
void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL) return;
  //copy the string s ,and update the pointer and length to abuf new values
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}
// deallocates the dynamic memory used by an abuf
void abFree(struct abuf *ab) {
  free(ab->b);
}

/*** output ***/
// draw ~ in the begining of the line by the size of window
void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    //welcome message third way down of the scrren 
    if (y == E.screenrows / 3) {
      char welcome[80];
      int welcomelen = snprintf(welcome, sizeof(welcome), 
        "Kilo editor -- version %s", KILO_VERSION);
      if (welcomelen > E.screencols) welcomelen = E.screencols;
      //print welcome message at center except for ~ at start of the line
      int padding = (E.screencols - welcomelen) / 2;
      if (padding) {
        abAppend(ab, "~", 1);
        padding--;
      }
      while (padding--) abAppend(ab, " ", 1);
      abAppend(ab, welcome, welcomelen);
    } else {
    abAppend(ab, "~", 1);
    // draw ~ at last line
    }
    //only clear one line at a time as it redrew them
    //K command(Erase in Line) O is defualt argument
    abAppend(ab, "\x1b[K", 3);
    if (y < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

void editorRefreshScreen(void) {
  //initialize new abuf called ab
  struct abuf ab = ABUF_INIT;
  //append every thing to buffer
  //also hide cursor when repainting, prevent potential flickering problem
  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);
  
  char buf[32];
  //set cursor position
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6);
  //write only once
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}


/*** input ***/
//cursor movement
void editorMoveCursor(int key) {
  switch (key)  {
    case ARROW_LEFT:
      E.cx--;
      break;
    case ARROW_RIGHT:
      E.cx++;
      break;
    case ARROW_UP:
      E.cy--;
      break;
    case ARROW_DOWN:
      E.cy++;
      break;
  }
}

void editorProcessKeypress(void) {
  int c = editorReadKey();
  //Ctrl-Q to exist
  switch (c) {
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;
  //cursor movement
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;
  }
}

/*** init ***/

void initEditor(void) {
  // initialize cursor position at 0,0 (top-left corner)
  E.cx = 0;
  E.cy = 0;
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

//currently on step 48. working on process
