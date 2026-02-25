
# Kilo Editor – Reference Notes

This document rewrites and cleans up two core references used while building **kilo**: ANSI escape sequences / POSIX streams, and Linux memory layout. The goal is clarity, correctness, and quick review while reading the source code.

---

## 1. ANSI Escape Sequences in Kilo

ANSI escape sequences are special byte sequences sent to the terminal to control cursor movement, screen clearing, and text formatting.

### Basic Structure

```
\x1b   -> ESC (escape character)
[      -> Control Sequence Introducer (CSI)
```

Most terminal control commands start with `ESC [` followed by parameters and a final command character.

### Common Sequences Used in Kilo

| Sequence | Meaning                                       |
| -------- | --------------------------------------------- |
| `ESC[K`  | Clear from cursor to end of line              |
| `ESC[2J` | Clear entire screen                           |
| `ESC[nC` | Move cursor **right** by `n` columns          |
| `ESC[nD` | Move cursor **left** by `n` columns           |
| `ESC[H`  | Move cursor to top-left corner (row 1, col 1) |
| `ESC[m`  | Set text attributes (formatting/colors)       |

> Note: Cursor movement commands use numbers (`n`) as parameters.

### Text Attributes (`ESC[m`)

Multiple attributes can be combined using semicolons:

```
ESC[1;4;7m
```

Common attributes:

| Code | Effect               |
| ---- | -------------------- |
| `0`  | Reset all attributes |
| `1`  | Bold                 |
| `4`  | Underline            |
| `5`  | Blink                |
| `7`  | Inverted colors      |

### Text Colors

Color is also set using `ESC[<code>m`.

| Code | Color                            |
| ---- | -------------------------------- |
| `30` | Black                            |
| `31` | Red                              |
| `32` | Green                            |
| `33` | Yellow                           |
| `34` | Blue                             |
| `35` | Magenta                          |
| `36` | Cyan                             |
| `37` | White                            |
| `39` | Default (reset foreground color) |

> Always reset colors with `ESC[39m` to avoid color bleeding.

---

## 2. POSIX Streams and File Descriptors

On Unix-like systems, **everything is treated as a file**—including keyboard input and terminal output.

Each open file is identified by a small integer called a **file descriptor**.

| Name            | Descriptor | Description                |
| --------------- | ---------- | -------------------------- |
| `STDIN_FILENO`  | `0`        | Standard input (keyboard)  |
| `STDOUT_FILENO` | `1`        | Standard output (terminal) |
| `STDERR_FILENO` | `2`        | Standard error output      |

Kilo writes directly to `STDOUT` and reads raw input from `STDIN`.

---

## 3. `termios` and Raw Mode

`termios` controls how the terminal processes input.

### Input Flags (`c_iflag`)

These flags are commonly disabled when enabling **raw mode**:

| Flag     | Purpose                                        |
| -------- | ---------------------------------------------- |
| `BRKINT` | Prevents BREAK condition from sending `SIGINT` |
| `ICRNL`  | Stops translating `\r` (CR) into `\n` (NL)     |
| `INPCK`  | Disables input parity checking                 |
| `ISTRIP` | Prevents stripping the 8th bit                 |
| `IXON`   | Disables Ctrl-S / Ctrl-Q flow control          |

Disabling these gives the program full control over key input.

---

## 4. Memory Layout in RAM

Each process runs in its own **virtual address space**.

```
High addresses
┌─────────────────────────────┐
│            Stack            │  grows ↓
├─────────────────────────────┤
│      Memory-mapped region   │  (shared libs, mmap)
├─────────────────────────────┤
│            Heap             │  grows ↑
├─────────────────────────────┤
│   BSS / Data (globals)      │
├─────────────────────────────┤
│            Code             │
└─────────────────────────────┘
Low addresses
```

* Memory addresses are written in **hexadecimal (base 16)**
* `0x` prefix indicates hex

### Example Addresses

```
Stack: 0x7ffeefbff5ac
Heap:  0x55555556a2a0
Code:  0x401146
```

On **x86_64 Linux**:

* Stack starts near `0x7fff...` and grows **downward**
* Heap starts near `0x5555...` and grows **upward**

If stack and heap collide → **out of memory**.

---

## 5. Stack vs Heap

### Stack

* Short-lived data
* Fixed size
* Automatically managed
* Used for local variables and function calls
* Very fast (LIFO: Last In, First Out)

### Heap

* Long-lived data
* Dynamic size
* Manually managed (or via GC)
* Used for large structures and buffers
* Slower but flexible

---

## 6. Common C Functions Used in Kilo

### `<string.h>`

#### `memset()`

Fills a block of memory with a single byte value.

```c
void *memset(void *ptr, int value, size_t num);
```

* Writes `num` bytes starting at `ptr`
* Each byte is set to `(unsigned char)value`
* Returns the original pointer `ptr`

**Important details:**

* `value` is applied **byte-by-byte**, not as an `int`
* Commonly used to initialize buffers or reset memory
* Very fast; often optimized at the hardware level

**Typical Kilo usage:**

```c
memset(row->hl, HL_NORMAL, row->rsize);
```

This sets every byte in the `hl` array to `HL_NORMAL`, effectively initializing syntax highlighting for the row.

**Common pitfalls:**

* Do **not** use `memset()` to initialize non-zero integers or structs with non-byte fields
* Safe values are usually `0`, `-1`, or small enums

---

### `<stdio.h>`

#### `snprintf()`

Writes formatted output into a buffer with a size limit.

```c
int snprintf(char *buf, size_t size, const char *fmt, ...);
```

* Prevents buffer overflow
* If return value ≥ `size`, output was truncated

---

### `<stdlib.h>`

#### `malloc()`

Allocates memory on the heap.

```c
void *malloc(size_t size);
```

* Returns a `void *` to heap memory
* Pointer itself usually lives on the stack

#### `realloc()`

Resizes an existing heap allocation.

```c
void *realloc(void *ptr, size_t new_size);
```

* May move memory to a new address
* Returns new pointer or `NULL`
* Old block remains valid if allocation fails

---

## 7. `memset()` vs `memcpy()`

Both `memset()` and `memcpy()` operate on **raw memory**, but they serve very different purposes.

### Function Comparison

| Function   | Purpose                                  | Writes                         |
| ---------- | ---------------------------------------- | ------------------------------ |
| `memset()` | Initialize or reset memory               | Repeated **single byte** value |
| `memcpy()` | Copy memory from one location to another | Exact byte-for-byte copy       |

### Signatures

```c
void *memset(void *ptr, int value, size_t num);
void *memcpy(void *dest, const void *src, size_t num);
```

### When to Use `memset()`

Use `memset()` when:

* Initializing buffers
* Clearing memory (`0`-fill)
* Setting arrays of bytes or enums

Example:

```c
memset(buf, 0, sizeof(buf));
```

### When to Use `memcpy()`

Use `memcpy()` when:

* Copying structs
* Copying buffers
* Duplicating data already initialized

Example:

```c
memcpy(dest, src, len);
```

> ⚠️ `memcpy()` **does not handle overlapping memory**. Use `memmove()` if overlap is possible.

---

## 8. Why `memset(struct, 0)` Usually Works

It is very common in C to see:

```c
memset(&my_struct, 0, sizeof(my_struct));
```

This **usually works** because:

* Zero bits represent:

  * `0` for integers
  * `NULL` for pointers (on modern systems)
  * `false` for booleans
* Most C structs are plain data (POD-style)

### When It Is Safe

`memset(struct, 0)` is safe when the struct contains:

* Integers
* Pointers
* Plain arrays
* Simple enums starting at `0`

This is why Kilo frequently uses zero-initialization for editor state.

### When It Breaks

`memset()` **does NOT guarantee correctness** if the struct contains:

* Floating-point values (not guaranteed all-bits-zero = `0.0`)
* Bitfields with non-zero defaults
* Enums where `0` is not a valid value
* Platform-dependent representations
* Objects requiring construction or invariants

Bad example:

```c
struct Bad {
  float x;        // not guaranteed
  int mode;       // enum where 0 is invalid
};
```

### Rule of Thumb

✔ Safe:

```c
memset(&s, 0, sizeof(s));
```

when the struct is **plain data and zero is meaningful**.

❌ Unsafe:

* Complex invariants
* Non-trivial defaults
* Anything pretending to be C++-like

---

## 9. Variadic Functions

Used heavily in formatting functions.

| Macro / Function | Purpose                        |
| ---------------- | ------------------------------ |
| `va_list`        | Stores variable argument state |
| `va_start()`     | Initializes argument list      |
| `va_end()`       | Cleans up                      |
| `vsnprintf()`    | Formats string using `va_list` |

---

## 8. Time Utility

```c
time_t time(time_t *t);
```

Returns current time in **epoch seconds**.

---

### Why This Matters for Kilo

Understanding terminal control + memory layout explains:

* Why raw mode is required
* Why `abuf` exists
* Why heap allocation dominates editor state
* Why ANSI escape sequences are written manually

Keep this doc nearby while reading the source code.
