# ESC sequences use in Kilo source code

\x1b -> ESC 
[ -> sequences for next input

- ESC[2J = erase entire screen
- ESC[H = move cursor to the top-left corner

## POSIX stream

In Unix-like-system everything is treat as a file (keyboard input, terminal output etc.) Each file indentified by file descriptor (value of small int)

- ```STDIN_FILENO```-> 0    standard input
- ```STDOUT_FILENO```-> 1    standard output
- ```STDERR_FILENO```-> 2    standard error message

### termios

```c_iflag``` stand for Input Flags

```BRKINT``` Break condition,"A ""BREAK"" condition will no longer send a SIGINT (like Ctrl-C) to the program."
```ICRNL``` Carriage Return to Newline,The terminal stops translating \r (Carriage Return) into \n (Newline).
```INPCK``` Input Parity Check,"Enables input parity checking. Disabling it is a relic of older hardware communication but is standard for ""raw"" mode."
```ISTRIP``` Strip Bit,Stops the terminal from stripping the 8th bit of each input byte.
```IXON``` Software Flow Control,"The big one. It disables Ctrl-S (stop output) and Ctrl-Q (resume output), allowing you to use those keys as normal data input."
