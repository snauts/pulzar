typedef signed char int8;
typedef unsigned char byte;
typedef unsigned short word;

#define ADDR(obj)	((word) (obj))
#define BYTE(addr)	(* (volatile byte *) (addr))
#define WORD(addr)	(* (volatile word *) (addr))
#define SIZE(array)	(sizeof(array) / sizeof(*(array)))

#define SETUP_STACK()	__asm__("ld sp, #0xFDFC")

#define IRQ_BASE	0xfe00

#include "data.h"

static volatile byte vblank;
static void interrupt(void) __naked {
    __asm__("di");
    __asm__("push af");
    __asm__("ld a, #1");
    __asm__("ld (_vblank), a");
    __asm__("pop af");
    __asm__("ei");
    __asm__("reti");
}

static void __sdcc_call_hl(void) __naked {
    __asm__("jp (hl)");
}

static void setup_irq(byte base) {
    __asm__("di");
    __asm__("ld i, a"); base;
    __asm__("im 2");
    __asm__("ei");
}

static void memset(byte *ptr, byte data, word len) {
    while (len-- > 0) { *ptr++ = data; }
}

static void setup_system(void) {
    BYTE(IRQ_BASE - 3) = 0xc3;
    WORD(IRQ_BASE - 2) = ADDR(&interrupt);
    memset((byte *) IRQ_BASE, (byte) ((IRQ_BASE >> 8) - 1), 0x101);
    setup_irq(IRQ_BASE >> 8);
}

static void wipe_screen(void) {
    memset((byte *) 0x4000, 0x00, 0x1B00);
}

void main(void) {
    SETUP_STACK();
    setup_system();
    wipe_screen();

    for (;;) {
    }
}
