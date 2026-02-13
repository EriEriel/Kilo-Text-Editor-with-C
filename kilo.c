/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** data ***/

//store original terminal attribute
struct termios orig_termios;

/*** terminal ***/

void die(const char *s) {
  //When C library function fail it will set global errno variable to indicate the error
  //perror() looks at the global errno and prints a descriptive error message for it.
  perror(s);
  //exit programe with status 1 (indicate failure)
  exit(1);
}

void disableRawmode(void) {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) die("tcgetattr");
  die("tcsetattr");
}

void enableRawMode(void) {
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcsetattr");
  atexit(disableRawmode);
  //Turn ECHO mode off. ECHO feature causes each key type to be print terminal but it get in a way when try to carefully render UI in rawmode.
  struct termios raw = orig_termios;
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

/*** init ***/

int main(void) {
  enableRawMode();

  //it read 1 byte from standard input the into variable c and compare to 1(which is 1 byte of char)
  while (1) {
    char c = '\0';
    if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");
  //iscntrl is from ctype.h and test whether a character is control character or not (control character is nonprintable)
  // ASCII 0-31 and 127 is control character
    if (iscntrl(c)) {
      printf("%d\r\n", c);
    } else {
      printf("%d('%c')\r\n", c, c); // print both character and ASCII values
    }
    if (c == 'q') break;
  }
  return 0;
}

//working on process, Continue on step 20.
