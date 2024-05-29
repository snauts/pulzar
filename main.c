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
static byte *map_y[192];

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

static void precalculate(void) {
    for (byte y = 0; y < 192; y++) {
	byte f = ((y & 7) << 3) | ((y >> 3) & 7) | (y & 0xC0);
	map_y[y] = (byte *) (0x4000 + (f << 5));
    }
}

static void draw_image(byte *img, byte x, byte y, byte w, byte h) {
    word i = 0;
    y = y << 3;
    h = h << 3;
    for (byte dy = y; dy < y + h; dy++) {
	byte *addr = map_y[dy] + x;
	for (byte dx = 0; dx < w; dx++) {
	    *addr++ = img[i++];
	}
    }

    for (byte dy = y; dy < y + h; dy += 8) {
	for (byte dx = x; dx < x + w; dx++) {
	    BYTE(0x5800 + (dy << 2) + dx) = img[i++];
	}
    }
}

static void draw_title(void) {
    draw_image(title, 4, 4, 24, 5);
}

void main(void) {
    SETUP_STACK();
    setup_system();
    wipe_screen();
    precalculate();
    draw_title();

    for (;;) { }
}
