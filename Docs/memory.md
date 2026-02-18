# Memory map in RAM (1 byte = 8 bits)

Virtual Address Space (per process)
┌─────────────────────────────┐  high addresses
│            Stack            │  grows ↓
│                             │
├─────────────────────────────┤
│      Memory-mapped region   │  (shared libs, mmap)
├─────────────────────────────┤
│            Heap             │  grows ↑
├─────────────────────────────┤
│   BSS / Data (globals)      │
├─────────────────────────────┤
│            Code             │
└─────────────────────────────┘  low addresses

Ram memory address is divined into difference address as above.
memory address is a hexadecimal(base 16 system) 

 Example of memory address : 
	Stack: 0x7ffeefbff5ac
  Heap:  0x55555556a2a0
  Code:  0x401146

0x indicate that this is base 16

	On X86_64 linux-system stack begin at 7fff… and go down(get increase in number) while heap begin at 55… and go up(get decrease in number)

	when heap and stack collide that mean it’s out of memory.

## Stack VS Heap in memory

Stack is a short live, small fixed size data and automatically free. it usually use for local scope variable or Function call. it faster than heap and manage automatically by LIFO(Last in First out)

Heap is long live, large/flexible dynamic size data and need to manage manually or with GC(garbage collector) it use for global scope variable, large structure and objects.
<stdio.h>

snprintf() use to write format string character into array buffer with specific maximum length. if return value equal or greater than the the set size, the output has been truncated.
int snprintf(char *buffer, size_t n, const char *format, ...);



<stdlib.h>

malloc() function in C - stand for memory allocate, malloc() take size of data as parameter( 4, sizeof(int), sizeof(*p) ) and it return generic pointer (void *) to the starting byte of that data in heap, also this pointer usually stay in stack layer of memory.

realloc() function in C - stand for re-allocate, it take pointer return by malloc, calloc, or realloc and argument and new size of data in bytes void *realloc(void *ptr, size_t new_size); , and it move that data to new address also return pointer to the start of that new address or NULL if failure (old block still exist)


va_list - store state to rea d variable arguments, when need to iterate over …argument

va_start() - initialize va_list

va_end() - clean up va_list

vsnprintf() - format string use va_list

time() - get current time in epoch seconds

