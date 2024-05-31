typedef signed char int8;
typedef unsigned char byte;
typedef unsigned short word;

#define DEBUG

#define ADDR(obj)	((word) (obj))
#define BYTE(addr)	(* (volatile byte *) (addr))
#define WORD(addr)	(* (volatile word *) (addr))
#define SIZE(array)	(sizeof(array) / sizeof(*(array)))

#define SETUP_STACK()	__asm__("ld sp, #0x7DFC")
#define SPACE_DOWN()	!(in_fe(0x7f) & 0x01)

#define IRQ_BASE	0x7e00

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
    byte top = (IRQ_BASE >> 8) - 1;
    word jmp_addr = (top << 8) | top;
    BYTE(jmp_addr + 0) = 0xc3;
    WORD(jmp_addr + 1) = ADDR(&interrupt);
    memset((byte *) IRQ_BASE, top, 0x101);
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

#ifdef DEBUG
static char to_hex(byte digit) {
    return (digit < 10) ? '0' + digit : 'A' + digit - 10;
}

static void put_num(word num, byte x, byte y, byte color) {
    char msg[] = "0000";
    for (byte i = 0; i < 4; i++) {
	msg[3 - i] = to_hex(num & 0xf);
	num = num >> 4;
    }
    put_str(msg, x, y, color);
}
#endif

static void draw_image(const byte *img, byte x, byte y, byte w, byte h) {
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

static void draw_tile(const byte *img, byte x, byte y, byte color) {
    word i = 0;
    y = y << 3;
    for (byte dy = y; dy < y + 8; dy++) {
	BYTE(map_y[dy] + x) = img[i++];
    }
    BYTE(0x5800 + (y << 2) + x) = color;
}

static const char * const intro[] = {
    "Tsk, tsk, yet again you have run",
    "into trouble with the galactic",
    "police, but this time you would",
    "rather die than go back to jail.",
    "Ahead of you lies PULZAR. Only",
    "a mad man might use its gravity",
    "to slingshot one's ship away.",
    "", "  Will you be able to escape?",
    "", "      Press SPACE to PLAY",
};

static void draw_title(void) {
    draw_image(title, 4, 3, 24, 5);
    for (byte i = 0; i < SIZE(intro); i++) {
	put_str(intro[i], 0, 10 + i, 0x42);
    }
    while (!SPACE_DOWN()) { }
}

static void draw_hud(void) {
    memset((byte *) 0x5800, 0x42, 0x300);

    draw_tile(edge + 0x00, 0x00, 0x17, 0x02);
    draw_tile(edge + 0x08, 0x17, 0x17, 0x02);
    draw_tile(edge + 0x10, 0x00, 0x00, 0x02);
    draw_tile(edge + 0x18, 0x17, 0x00, 0x02);

    for (byte i = 1; i < 23; i++) {
	draw_tile(edge + 0x40, i, 0x17, 0x02);
	draw_tile(edge + 0x48, i, 0x00, 0x02);
	draw_tile(edge + 0x50, 0x00, i, 0x02);
	draw_tile(edge + 0x58, 0x17, i, 0x02);
    }

    const byte *ptr = edge + 0x20;
    for (byte i = 0; i < 4; i++) {
	draw_tile(ptr, i + 1, 0x17, 0x02);
	ptr += 8;
    }

    draw_image(star, 9, 9, 6, 6);
}

static word pos;
static byte dir;
static byte key;

static void draw_ship_part(word i) {
    i = i & 0xfff;
    byte *ptr = (byte *) line_addr[i];
    byte data = line_data[i];
    data |= (data << 1);
    data |= (data >> 1);
    *ptr ^= data;
}

static void draw_whole_ship(void) {
    draw_ship_part(pos);
    draw_ship_part(pos + 1);
    draw_ship_part(pos + (dir ? 32 : -32));
}

static void control_ship(void) {
    byte now = SPACE_DOWN();
    if (now > key) dir = 1 - dir;
    if (dir) pos += 32; else pos -= 32;
    pos = pos;
    key = now;
}

static void draw_player(void) {
    draw_whole_ship();
    control_ship();
    draw_whole_ship();
}

#ifdef DEBUG
static void debug_lines(void) {
    const byte *data = line_data;
    const word *addr = line_addr;
    while (addr < line_addr + SIZE(line_addr)) {
	byte *ptr = (byte *) *(addr++);
	*ptr |= *(data++);
    }
}
#endif

static void init_vars(void) {
    key = SPACE_DOWN();
    pos = 24;
    dir = 1;
}

static void game_loop(void) {
    draw_whole_ship();
    for (;;) {
	wait_vblank();
	out_fe(0x02);
	draw_player();
	out_fe(0x00);
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

    init_vars();
    game_loop();
}
