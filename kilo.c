/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h> //ASCII string conversion and checking.
#include <errno.h> //Define errno macro for reporting error conditions
#include <stdio.h> //Standard I/O operation
#include <stdlib.h>//dynamic memory management
#include <string.h>//used to mamipulate string array and memory blocks
#include <sys/ioctl.h>//system call to manipulate terminal and special file
#include <sys/types.h>//provide system data type
#include <termios.h>//provide std controlling, async communication port and terminal I/O
#include <unistd.h>//not part of stdlib in C. Provide POSIX(Portable Operation System Interface) operation system API

/*** defines ***/

#define KILO_VERSION "0.0.1"

//Set upper 3 bits of character(1 byte) to 0. mimic Ctrl press on terminal level
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
  //set int to 1000 because this out of range of any character, prevent conflict
  ARROW_LEFT  = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

/*** data ***/

//erow stands for "editor row", it store line of text as pointer to dynamically re-allocate character abd data length
typedef struct erow {
  int size;
  int rsize;
  char *chars;
  char *render;
} erow;
//store editor state in E
struct editorConfig {
  int cx, cy; //cursor position
  int rowoff; //row offset for vertical scrolling
  int coloff; //column offset for horizontal scrolling
  int screenrows;
  int screencols;
  int numrows; //amount of rows
  erow *row; //size of row in bytes
  struct termios orig_termios; //original terminal state
};

struct editorConfig E;

/*** terminal ***/

//print error message and exit program immediatly
void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4); //erase entire screen
  write(STDOUT_FILENO, "\x1b[H", 3);  //move cursor to top-left corner
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
//return key-press
 int editorReadKey(void) {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) { //while read(keyboard input in to buffer &c each by 1 byte) != 1
    if (nread == -1 && errno != EAGAIN) die("read");
  }

  if (c == '\x1b') {
    char seq[3];
    //Esc sequences key-press
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            //due to HOME_KEY and END_KEY can have different ESC sequences depend on system so we handle of the case here
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      } else {
      switch (seq[1]) {
        case 'A': return ARROW_UP;
        case 'B': return ARROW_DOWN;
        case 'C': return ARROW_RIGHT;
        case 'D': return ARROW_LEFT;
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }
  } else if (seq[0] == '0') {
      switch (seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
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

/*** row operation ***/

//free the old render buffer than allocate the new one, then copy each char to the render buffer
void editorUpdateRow(erow *row) {
  free(row->render);
  row->render = malloc(row->size + 1);

  int j;
  int idx = 0;
  for (j = 0; j < row->size; j++) {
    row->render[idx++] = row->chars[j];
  }
  //null terminate the string so C know where string end
  row->render[idx] = '\0';
  //Update the rsize
  row->rsize = idx;
}


//append the row and allocate space for the new row
void editorAppendRow(char *s, size_t len) {
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

  int at = E.numrows;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
}

/*** file I/O ***/
//take file name and open the file, if blank open blank file
void editorOpen(char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");


  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    //strip off newline carriage becuase erow = one line of text so no use for storing newline character
    while (linelen > 0 && (line[linelen - 1] == '\n' || 
                           line[linelen - 1] == '\r'))
      linelen--;
    editorAppendRow(line, linelen);
  } 
  free(line);
  fclose(fp);
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

//set value of E.rowoff by check if cursor moved outside of visible window
void editorScroll(void) {
  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.cx < E.coloff) {
    E.coloff = E.cx;
  }
  if (E.cx >= E.coloff + E.screencols) {
    E.coloff = E.cx - E.screencols + 1;
  }
}

// draw ~ in the begining of the line by the size of window
void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
      //if text buffer is empty display welcome message third way down of the scrren
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome), 
          "Kilo editor (Mod. by Eri_Eriel) -- version %s", KILO_VERSION);
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
    } else {
      int len = E.row[filerow].size - E.coloff;
      if (len < 0) len = 0;
      if (len > E.screencols) len = E.screencols;
      abAppend(ab, &E.row[filerow].chars[E.coloff], len);
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
  editorScroll();
  //initialize new abuf called ab
  struct abuf ab = ABUF_INIT;
  //append every thing to buffer
  //also hide cursor when repainting, prevent potential flickering problem
  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);
  
  char buf[32];
  //set cursor position
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, 
                                            (E.cx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6);
  //write only once
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}


/*** input ***/
//cursor movement + out of bound prevention
void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key)  {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      } else if (E.cy > 0) {
        E.cy--;
        E.cx = E.row[E.cy].size; //if user press <- at the begining of line, move cursor to the end of previos line
      }
      break;

    case ARROW_RIGHT:
      if (row && E.cx < row->size) { //limit cursor move to the end of line but not off the screen
        E.cx++;
      } else if (row && E.cx == row->size) {
        E.cy++;
        E.cx = 0;  //if user press -> at the end of line, move cursor to the begining of next line

      }
      break;
    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      if (E.cy < E.numrows) { //let cursor move past buttom of the screen but not past the bottom of the file
      E.cy++; 
      }
      break;
  }

  //prevent out of bound row access, invalid column position, and cursor always stay on valid text 
  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) { //check if curosr x position out of text
    E.cx = rowlen; //if so, set cursor x position to the end of text
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

    //PAGE_UP and PAGE_DOWN to the top and bottom of the screen, while HOME_KEY and END_KEY move cursor left and right edge of screen
    case HOME_KEY:
      E.cx = 0;
      break;

    case END_KEY:
      E.cx = E.screencols - 1;
      break;

    case PAGE_UP:
    case PAGE_DOWN:
      {
        int times = E.screenrows;
        while (times--)
          editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
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
  E.rowoff = 0; //scroll to the top of file by default
  E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  //only call editoropen() when argc != 1, so it can compile and run blank program correctly
  if (argc >= 2) {
    editorOpen(argv[1]);
  }
  //it read 1 byte from standard input the into variable c and compare to 1(which is 1 byte of char)
  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}

//currently on step 81. working on process
