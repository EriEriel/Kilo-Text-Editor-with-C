/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h> //ASCII string conversion and checking.
#include <errno.h> //Define errno macro for reporting error conditions
#include <fcntl.h>
#include <stdio.h> //Standard I/O operation
#include <stdlib.h>//dynamic memory management
#include <stdarg.h> //
#include <string.h>//used to mamipulate string array and memory blocks
#include <sys/ioctl.h> //system call to manipulate terminal and special file
#include <sys/types.h> //provide system data type
#include <termios.h> //provide std controlling, async communication port and terminal I/O
#include <time.h> //
#include <unistd.h>//not part of stdlib in C. Provide POSIX(Portable Operation System Interface) operation system API

/*** defines ***/

#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3
//Set upper 3 bits of character(1 byte) to 0. mimic Ctrl press on terminal level
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
  //set int to 1000 because this out of range of any character, prevent conflict
  BACKSPACE = 127,
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
  int rx; //index into render. if no tab character rx = cx, else rx > cx
  int rowoff; //row offset for vertical scrolling
  int coloff; //column offset for horizontal scrolling
  int screenrows;
  int screencols;
  int numrows; //amount of rows
  erow *row; //size of row in bytes
  int dirty; //flag for non-empty text buffer
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  struct termios orig_termios; //original terminal state
};

struct editorConfig E;

/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);

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

int editorRowCxtoRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP); //find how many column to the left of the next tab stop 
    rx++;
  }
  return rx;
}

//free the old render buffer than allocate the new one, then copy each char to the render buffer
void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;
  //because one character in chars[] can produce many characters in render (ex. \tA -> A) so we need J and idx serperately
  free(row->render);
  row->render = malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1); //maximum memory that need to render row. tabs is 8 char so here we simply multiply 7 by tabs + 1(row->size = 1)

  //to display tabs/space correctly
  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  //null terminate the string so C know where string end
  row->render[idx] = '\0';
  //Update the rsize
  row->rsize = idx;
}

//to the new row
void editorInsetRow(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows) return;

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
  E.dirty++;
}

//free buffer
void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
}

void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows) return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1)); //slid array to the left by 1 to close the gap
  E.numrows--;
  E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  row->chars = realloc(row->chars, row->size + 2); //add 2 for the null byte(\0)
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
}

void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}

/*** editor operations ***/

void editorInsertChar(int c) {
  if (E.cy == E.numrows) {
    editorInsetRow(E.numrows, "", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
  E.dirty++;
}

void editorDelChar(void) {
  if (E.cy == E.numrows) return;
  if (E.cx == 0 && E.cy == 0) return;

  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  } else {
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    editorDelRow(E.cy);
    E.cy--;
  }
}

void editorInsertNewLine(void) {
  if (E.cx ==0) {
    editorInsetRow(E.cy, "", 0);
  } else {
    erow *row = &E.row[E.cy];
    editorInsetRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;
}



/*** file I/O ***/

//turn every rows in to one big text buffer
char *editorRowToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++)
    totlen += E.row[j].size + 1;
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }

  return buf;
}

//take file name and open the file, if blank open blank file
void editorOpen(char *filename) {
  //set filename when open file
  free(E.filename);
  E.filename = strdup(filename); //strdup() from <string.h> copy the given string and allocate the required memory, assume you are free()


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
    editorInsetRow(E.numrows, line, linelen);
  } 
  free(line);
  fclose(fp);
  E.dirty = 0;
}

void editorSave(void) {
  if (E.filename == NULL) return;

  int len;
  char *buf = editorRowToString(&len);

  int fd = open(E.filename, O_RDWR | O_CREAT, 0644); //0644 is standard permission for a text file. owner can read and write, while every one else can only read
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        E.dirty = 0;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }

  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
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
  E.rx = E.cx;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxtoRx(&E.row[E.cy], E.cx);
  }

  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
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
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0) len = 0;
      if (len > E.screencols) len = E.screencols;
      abAppend(ab, &E.row[filerow].render[E.coloff], len);
    }
    //only clear one line at a time as it redrew them
    //K command(Erase in Line) O is defualt argument
    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
    }
  
}

void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4); //invert color to make it standout
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", 
    E.filename ? E.filename : "[No Name]", E.numrows, //use snprintf() to set the amount of line and file name 
    E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
    E.cy + 1, E.numrows);
  if (len > E.screencols) len = E.screencols;
  abAppend(ab, status, len);
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen); //show line number to the right of the screen
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3); //ESC sequences for text format. 0 clear arttribute
  abAppend(ab, "\r\n", 2);
}

//drawmessage bar at bottom of the screen
void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
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
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  char buf[32];
  //set cursor position
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, 
                                            (E.rx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6);
  //write only once
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
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
  static int quit_times = KILO_QUIT_TIMES;

  int c = editorReadKey();
  //Ctrl-Q to exist
  switch (c) {
    case '\r': //Enter to the new line
      editorInsertNewLine();
      break;

    case CTRL_KEY('q'):
      if (E.dirty && quit_times > 0) {
        editorSetStatusMessage("WARNING!!! File has unsaved changes. "
          "Press Ctrl-Q %d more times to quit.", quit_times);
        quit_times--;
        return;
      }
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;

    case CTRL_KEY('s'):
      editorSave();
      break;

    //PAGE_UP and PAGE_DOWN to the top and bottom of the screen, while HOME_KEY and END_KEY move cursor left and right edge of screen
    case HOME_KEY:
      E.cx = 0;
      break;

    case END_KEY: //bring to the end of line, if no current line E.cx = 0
      if (E.cy < E.numrows)
        E.cx = E.row[E.cy].size;
      break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
      if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
      editorDelChar();
      break;

    case PAGE_UP:
    case PAGE_DOWN:
      {
        if (c == PAGE_UP) {
          E.cy = E.rowoff;
        } else if (c == PAGE_DOWN) {
          E.cy = E.rowoff + E.screenrows - 1;
          if (E.cy > E.numrows) E.cy = E.numrows; // Prevent out of bound
        }

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

    case CTRL_KEY('l'):
    case '\x1b':
      break;

    default:
      editorInsertChar(c);
    break;
  }

  quit_times = KILO_QUIT_TIMES;
}

/*** init ***/

void initEditor(void) {
  // initialize cursor position at 0,0 (top-left corner)
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0; //scroll to the top of file by default
  E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.dirty = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
  E.screenrows -= 2; //make space in final line for status bar, with this editorDrawRows() will not draw line at bottom of screen
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  //only call editoropen() when argc != 1, so it can compile and run blank program correctly
  if (argc >= 2) {
    editorOpen(argv[1]);
  }
 
  editorSetStatusMessage("HELP: Ctrl-Q = quit | Ctrl-Q = quit");

  //it read 1 byte from standard input the into variable c and compare to 1(which is 1 byte of char)
  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}

//currently on step 98. working on process
