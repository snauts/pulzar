typedef signed char int8;
typedef unsigned char byte;
typedef unsigned short word;

#define ADDR(obj)	((word) (obj))
#define BYTE(addr)	(* (volatile byte *) (addr))
#define WORD(addr)	(* (volatile word *) (addr))
#define SIZE(array)	(sizeof(array) / sizeof(*(array)))

#define SETUP_STACK()	__asm__("ld sp, #0xFDFC")
#define SPACE_DOWN()	!(in_fe(0x7f) & 0x01)

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

static void out_fe(byte data) {
    __asm__("out (#0xfe), a"); data;
}

static byte in_fe(byte a) __naked {
    __asm__("in a, (#0xfe)"); a;
    __asm__("ret");
}

static void wait_vblank(void) {
    while (!vblank) { }
    vblank = 0;
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
    memset((byte *) 0x5800, 0x00, 0x300);
    memset((byte *) 0x4000, 0x00, 0x1800);
    out_fe(0);
}

static void precalculate(void) {
    for (byte y = 0; y < 192; y++) {
	byte f = ((y & 7) << 3) | ((y >> 3) & 7) | (y & 0xC0);
	map_y[y] = (byte *) (0x4000 + (f << 5));
    }
}

static void put_char(char symbol, byte x, byte y, byte color) {
    y = y << 3;
    byte *addr = (byte *) 0x3C00 + (symbol << 3);
    for (byte i = 0; i < 8; i++) {
	map_y[y + i][x] = *addr++;
    }
    BYTE(0x5800 + (y << 2) + x) = color;
}

static void put_str(const char *msg, byte x, byte y, byte color) {
    while (*msg != 0) {
	put_char(*(msg++), x++, y, color);
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

static void draw_tile(byte *img, byte x, byte y, byte color) {
    word i = 0;
    y = y << 3;
    for (byte dy = y; dy < y + 8; dy++) {
	BYTE(map_y[dy] + x) = img[i++];
    }
    BYTE(0x5800 + (y << 2) + x) = color;
}

static const char * const intro[] = {
    "Tsk, tsk, once again you have",
    "trouble with galactic police,",
    "but this time you would rather",
    "die than go back to the prison.",
    "Ahead of you lies PULZAR. Only",
    "mad man might use it's gravity",
    "to slingshot ones ship away.",
    "", " Will you be able to escape?",
    "", "     Press SPACE to start",
};

static void draw_title(void) {
    draw_image(title, 4, 3, 24, 5);
    for (byte i = 0; i < SIZE(intro); i++) {
	put_str(intro[i], 1, 10 + i, 0x42);
    }
    while (!SPACE_DOWN()) { }
}

static void draw_hud(void) {
    draw_tile(edge + 0x00, 0x00, 0x17, 0x10);
    draw_tile(edge + 0x08, 0x17, 0x17, 0x10);
    draw_tile(edge + 0x10, 0x00, 0x00, 0x10);
    draw_tile(edge + 0x18, 0x17, 0x00, 0x10);

    byte *ptr = edge + 0x20;
    for (byte i = 0; i < 4; i++) {
	draw_tile(ptr, i + 1, 0x17, 0x02);
	ptr += 8;
    }
}

void main(void) {
    SETUP_STACK();
    setup_system();
    wipe_screen();
    precalculate();

    draw_title();
    wipe_screen();
    draw_hud();

    for (;;) { }
}
