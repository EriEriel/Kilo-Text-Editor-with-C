
# Kilo Text Editor (Reimplementation in C)

A low-level reimplementation of the **Kilo** text editor written in C, built to gain hands-on experience with terminal programming, memory management, and systems-level design.

This project focuses on understanding *how terminal-based editors work internally* by rebuilding core functionality without external libraries.

---

##  Project Objectives

- Strengthen practical C programming skills
- Understand terminal I/O and raw mode input handling
- Learn memory-safe dynamic data structures
- Read and reason about a real-world C codebase
- Apply bitwise operations and system calls in context

---

##  Technical Skills Demonstrated

### Systems Programming
- POSIX APIs: `read`, `write`, `open`, `close`
- Terminal control using `termios`
- ANSI escape sequences for rendering and styling

### Memory Management
- Manual allocation with `malloc`, `realloc`, and `free`
- Safe buffer resizing and ownership tracking
- Avoiding memory leaks and dangling pointers

### Data Structures & Design
- Line-based text storage using custom structs
- Global editor state management
- Separation of editor logic, rendering, and input

### Performance & UX
- Buffered screen rendering to minimize terminal flicker
- Incremental screen refresh strategy
- Efficient cursor movement and scrolling logic

---

##  Features Implemented

- Raw terminal mode
- Cursor navigation and scrolling
- File open and save
- Text insertion and deletion
- Status bar and message bar
- Incremental search
- Syntax highlighting using bitmasks
- Graceful error handling and cleanup

---

##  What This Project Proves

- Ability to work close to the operating system
- Comfort reading and modifying non-trivial C programs
- Practical understanding of pointers, buffers, and flags
- Strong debugging skills in low-level environments

---

##  Notes

This is a learning-focused project inspired by the original Kilo editor.  
Not intended for production use.
