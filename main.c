typedef signed char int8;
typedef unsigned char byte;
typedef unsigned short word;

#define ADDR(obj)	((word) (obj))
#define BYTE(addr)	(* (volatile byte *) (addr))
#define WORD(addr)	(* (volatile word *) (addr))
#define SIZE(array)	(sizeof(array) / sizeof(*(array)))

#include "data.h"

void main(void) {
    __asm__("ld sp, #0xFDFC");

    for (;;) {
    }
}
