typedef signed char int8;
typedef unsigned char byte;
typedef unsigned short word;

#define DEBUG

#define ADDR(obj)	((word) (obj))
#define BYTE(addr)	(* (volatile byte *) (addr))
#define WORD(addr)	(* (volatile word *) (addr))
#define SIZE(array)	(sizeof(array) / sizeof(*(array)))

#ifdef ZXS
#define STREAK		10
#define is_vsync()	vblank
#define EDGE(x)		(edge + x)
#define SPACE_DOWN()	!(in_fe(0x7f) & 0x01)
#define SETUP_STACK()	__asm__("ld sp, #0xFDFC")
#define IRQ_BASE	0xfe00
#define TILE_SIZE	8
#endif

#ifdef CPC
#define STREAK		20
#define EDGE(x)		(edge + (x << 1))
#define SPACE_DOWN()	!(cpc_keys() & 0x80)
#define SETUP_STACK()	__asm__("ld sp, #0x95FC")
#define IRQ_BASE	0x9600
#define TILE_SIZE	16
#endif

#define LINE(x)		(byte *) (line_addr[x])

#include "data.h"

static volatile byte vblank;
static byte *map_y[192];

static word counter;
static byte level;
static byte flash;
static int8 lives;
static byte done;

static word pos;
static byte dir;
static byte key;
static byte clr;
static byte die;

static word ray[256];
static byte head, tail;

void reset(void);
static void (*emit_field)(void);

static byte wait;
static byte repeat;
static const byte *wrap;
static const byte *start;
static const byte *current;

struct Level {
    void (*fn)(void);
    const char *msg;
};

static void interrupt(void) __naked {
#ifdef ZXS
    __asm__("di");
    __asm__("push af");
    __asm__("ld a, #1");
    __asm__("ld (_vblank), a");
    __asm__("pop af");
    __asm__("ei");
#endif
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

#ifdef CPC
static word mul80(word x) {
    return (x << 6) + (x << 4);
}

static void gate_array(byte reg) {
    __asm__("ld bc, #0x7f00");
    __asm__("out (c), a"); reg;
}

static byte is_vsync(void) __naked {
    __asm__("ld b, #0xf5");
    __asm__("in a, (c)");
    __asm__("and a, #1");
    __asm__("ret");
}

static byte cpc_psg(byte reg, byte val) __naked {
    __asm__("ld b, #0xf4");
    __asm__("ld c, a"); reg;
    __asm__("out (c), c");
    __asm__("ld bc, #0xf6c0");
    __asm__("out (c), c");
    __asm__("ld bc, #0xf600");
    __asm__("out (c), c");
    __asm__("ld bc, #0xf680");
    __asm__("out (c), c");
    __asm__("ld b, #0xf4");
    __asm__("ld c, l"); val;
    __asm__("out (c), c");
    __asm__("ld bc, #0xf600");
    __asm__("out (c), c");
    __asm__("ret");
}

static byte cpc_keys(void) __naked {
    __asm__("ld bc, #0xf782");
    __asm__("out (c), c");
    __asm__("ld bc, #0xf40e");
    __asm__("out (c), c");
    __asm__("ld bc, #0xf6c0");
    __asm__("out (c), c");
    __asm__("ld bc, #0xf600");
    __asm__("out (c), c");
    __asm__("ld bc, #0xf792");
    __asm__("out (c), c");
    __asm__("ld bc, #0xf645");
    __asm__("out (c), c");
    __asm__("ld b, #0xf4");
    __asm__("in a, (c)");
    __asm__("ld bc, #0xf782");
    __asm__("out (c), c");
    __asm__("ld bc, #0xf600");
    __asm__("out (c), c");
    __asm__("ret");
}

static const byte pal1[] = {
    0x9D, 0x10, 0x54, 0, 0x54, 1, 0x4D, 2, 0x4C, 3, 0x4A
};

static const byte pal2[] = {
    0x9D, 0x10, 0x54, 0, 0x54, 1, 0x4A, 2, 0x4C, 3, 0x4B
};

static void init_gate_array(const byte *ptr, byte size) {
    for (byte i = 0; i < size; i++) gate_array(ptr[i]);
}

static void palette(byte num) {
    num;
    switch (num) {
    case 1:
	init_gate_array(pal1, SIZE(pal1));
	break;
    case 2:
	init_gate_array(pal2, SIZE(pal2));
	break;
    }
}
#endif

static void delay_vblank(word ticks) {
    for (word i = 0; i < ticks; i++) { if (is_vsync()) break; }
}

#ifdef ZXS
static void crash_sound(void) {
    if (die && die < 8) {
	word pitch = 8 + (die << 3);
	out_fe(0x10);
	delay_vblank(pitch);
	out_fe(0x0);
	delay_vblank(pitch);
    }
}

static void level_sound(void) {
    static const byte jerk_color[] = { 10, 0, 14, 15 };
    if (flash) {
	byte color = jerk_color[flash & 3];
	out_fe(0x10 | color);
	delay_vblank(flash);
	out_fe(0x0 | color);
	delay_vblank(flash);
    }
}
#endif

#ifdef CPC
static void crash_sound(void) {
    if (die) {
	if (die < 8) {
	    if (die == 1) cpc_psg(8, 0x0F);
	    word pitch = 225 + (die << 6);
	    cpc_psg(0, pitch & 0xff);
	    cpc_psg(1, pitch >> 8);
	}
	else {
	    cpc_psg(8, 0x10);
	}
    }
}

static void level_sound(void) {
    static const byte jerk_color[] = { 0x4A, 0x54, 0x4B, 0x4C };

    if (flash) {
	gate_array(0x10);
	gate_array(jerk_color[flash & 3]);

	if (flash == 32) {
	    cpc_psg(8, 0x0F);
	}
	if (flash > 1) {
	    word pitch = 225 - ((32 - flash) << 2);
	    cpc_psg(0, pitch & 0xff);
	    cpc_psg(1, pitch >> 8);
	}
	else {
	    cpc_psg(8, 0x10);
	}
    }
}
#endif

static void wait_vblank(void) {
#ifdef CPC
    crash_sound();
    level_sound();
#endif
    while (!is_vsync()) {
#ifdef ZXS
	crash_sound();
	level_sound();
#endif
    }
    vblank = 0;
}

static void memset(byte *ptr, byte data, word len) {
    while (len-- > 0) { *ptr++ = data; }
}

static void setup_system(void) {
    byte top = (byte) ((IRQ_BASE >> 8) - 1);
    word jmp_addr = (top << 8) | top;
    BYTE(jmp_addr + 0) = 0xc3;
    WORD(jmp_addr + 1) = ADDR(&interrupt);
    memset((byte *) IRQ_BASE, top, 0x101);
    setup_irq(IRQ_BASE >> 8);

#ifdef CPC
    __asm__("ld bc, #0xbc0c");
    __asm__("out (c), c");
    __asm__("ld bc, #0xbd33");
    __asm__("out (c), c");

    __asm__("ld bc, #0xbc0d");
    __asm__("out (c), c");
    __asm__("ld bc, #0xbdd4");
    __asm__("out (c), c");

    cpc_psg(7, 0xB8);
    cpc_psg(8, 0x00);
#endif
}

static void clear_screen(void) {
#ifdef ZXS
    memset((byte *) 0x5800, 0x00, 0x300);
    memset((byte *) 0x4000, 0x00, 0x1800);
    out_fe(0);
#endif
#ifdef CPC
    memset((byte *) 0xC000, 0x00, 0x4000);
    palette(1);
#endif
}

static void precalculate(void) {
    for (byte y = 0; y < 192; y++) {
#ifdef ZXS
	byte f = ((y & 7) << 3) | ((y >> 3) & 7) | (y & 0xC0);
	map_y[y] = (byte *) (0x4000 + (f << 5));
#endif
#ifdef CPC
	word f = ((y & 7) << 11) | mul80(y >> 3);
	map_y[y] = (byte *) (0xC000 + f);
#endif
    }
}

static void put_char(char symbol, byte x, byte y, byte color) {
#ifdef ZXS
    y = y << 3;
    byte *addr = (byte *) 0x3C00 + (symbol << 3);
    for (byte i = 0; i < 8; i++) {
	map_y[y + i][x] = *addr++;
    }
    BYTE(0x5800 + (y << 2) + x) = color;
#endif
#ifdef CPC
    color;
    x = x << 1;
    y = y << 3;
    word glyph = symbol - 0x20;
    const byte *addr = font_cpc + (glyph << 4);
    for (byte i = 0; i < 8; i++) {
	byte *ptr = map_y[y++] + x;
	*ptr++ = *addr++;
	*ptr++ = *addr++;
    }
#endif
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
#ifdef CPC
    w = w << 1;
    x = x << 1;
#endif
    for (byte dy = y; dy < y + h; dy++) {
	byte *addr = map_y[dy] + x;
	for (byte dx = 0; dx < w; dx++) {
	    *addr++ = img[i++];
	}
    }
#ifdef ZXS
    for (byte dy = y; dy < y + h; dy += 8) {
	for (byte dx = x; dx < x + w; dx++) {
	    byte attribute = img[i++];
	    if (attribute != 0) {
		BYTE(0x5800 + (dy << 2) + dx) = attribute;
	    }
	}
    }
#endif
}

static void draw_tile(const byte *img, byte x, byte y, byte color) {
    word i = 0;
    y = y << 3;
#ifdef CPC
    x = x << 1;
    color;
#endif
    for (byte dy = y; dy < y + 8; dy++) {
	BYTE(map_y[dy] + x + 0) = img[i++];
#ifdef CPC
	BYTE(map_y[dy] + x + 1) = img[i++];
#endif
    }
#ifdef ZXS
    BYTE(0x5800 + (y << 2) + x) = color;
#endif
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

static void wait_space(void) {
    while (!SPACE_DOWN()) { }
}

static void draw_title(void) {
    draw_image(title, 4, 3, 24, 5);
    for (byte i = 0; i < SIZE(intro); i++) {
	put_str(intro[i], 0, 10 + i, 0x42);
    }
    wait_space();
}

static const char * const lose[] = {
    "All of us are cosmic dust, but",
    "some more literally than others.",
};

static void game_over(void) {
    put_str("GAME OVER", 11, 10, 0x42);
    for (byte i = 0; i < SIZE(lose); i++) {
	put_str(lose[i], 0, 12 + i, 0x42);
    }
    wait_space();
}

static void life_sprite(byte offset, byte pos) {
    draw_tile(EDGE(offset), 0x16 - pos, 0x17, 0x02);
}

#ifdef ZXS
static byte *attribute_addr(byte x, byte y) {
    return (byte *) 0x5800 + ((y & ~7) << 2) + x;
}
#endif

static void flip_V(byte x1, byte y1, byte x2, byte y2, byte w, byte h) {
#ifdef CPC
    w = w << 1;
    x1 = x1 << 1;
    x2 = x2 << 1;
#endif
    for (byte j = 0; j < h; j++) {
	word z1 = y1 + j;
	word z2 = y2 + h - j - 1;
	byte *p_src = (byte *) map_y[z1] + x1;
	byte *p_dst = (byte *) map_y[z2] + x2;
#ifdef ZXS
	byte *c_src = attribute_addr(x1, z1);
	byte *c_dst = attribute_addr(x2, z2);
#endif
	for (byte i = 0; i < w; i++) {
	    p_dst[i] = p_src[i];
#ifdef ZXS
	    c_dst[i] = c_src[i];
#endif
	}
    }
}

static byte flip_bits(byte source) {
    byte result = 0;
    for (byte i = 0; i < 8; i++) {
	result = result << 1;
	result |= source & 1;
	source = source >> 1;
    }
#ifdef CPC
    result = (result >> 4) | (result << 4);
#endif
    return result;
}

static void flip_H(byte x1, byte y1, byte x2, byte y2, byte w, byte h) {
#ifdef CPC
    w = w << 1;
    x1 = x1 << 1;
    x2 = x2 << 1;
#endif
    for (byte j = 0; j < h; j++) {
	word z1 = y1 + j;
	word z2 = y2 + j;
	byte *p_src = (byte *) map_y[z1] + x1;
	byte *p_dst = (byte *) map_y[z2] + x2;
#ifdef ZXS
	byte *c_src = attribute_addr(x1, z1);
	byte *c_dst = attribute_addr(x2, z2);
#endif
	for (byte i = 0; i < w; i++) {
	    byte offset = w - i - 1;
	    p_dst[i] = flip_bits(p_src[offset]);
#ifdef ZXS
	    c_dst[i] = c_src[offset];
#endif
	}
    }
}

static void draw_level_tab(void);
static void draw_hud(void) {
#ifdef ZXS
    memset((byte *) 0x5800, 0x42, 0x300);
#endif
#ifdef CPC
    palette(2);
#endif

    draw_tile(EDGE(0x00), 0x00, 0x17, 0x02);
    draw_tile(EDGE(0x08), 0x17, 0x17, 0x02);
    draw_tile(EDGE(0x10), 0x00, 0x00, 0x02);
    draw_tile(EDGE(0x18), 0x17, 0x00, 0x02);

    for (byte i = 1; i < 23; i++) {
	draw_tile(EDGE(0x40), i, 0x17, 0x02);
	draw_tile(EDGE(0x48), i, 0x00, 0x02);
	draw_tile(EDGE(0x50), 0x00, i, 0x02);
	draw_tile(EDGE(0x58), 0x17, i, 0x02);
    }

    const byte *ptr = EDGE(0x20);
    for (byte i = 0; i < 4; i++) {
	draw_tile(ptr, i + 1, 0x17, 0x02);
	ptr += TILE_SIZE;
    }

    for (int8 i = 0; i < lives; i++) {
	life_sprite(0x60, i);
    }

    draw_image(circuit, 1, 1, 8, 8);
    flip_V(0x01, 0x08, 0x01, 0x78, 0x08, 0x40);
    flip_H(0x01, 0x08, 0x0f, 0x08, 0x08, 0x40);
    flip_V(0x0f, 0x08, 0x0f, 0x78, 0x08, 0x40);
    draw_image(star, 9, 9, 6,  6);
    draw_level_tab();
}

static void take_life(void) {
    if (--lives >= 0) life_sprite(0x40, lives);
}

static inline byte check_collision(byte prev, byte data) {
    byte mask = prev & data;
    return clr ? mask != data : mask;
}

static void draw_ship_part(word i) {
    i = i & 0xfff;
    byte prev = *LINE(i);
    byte data = line_data[i];
    data |= dir ? (data << 1) : (data >> 1);
#ifdef CPC
    data = data & 0xf;
    data = data | (data << 4);
#endif
    if (check_collision(prev, data)) die = 1;
    *LINE(i) = prev ^ data;
}

static void draw_whole_ship(byte clear_ship) {
    clr = clear_ship;
    draw_ship_part(pos - 1);
    draw_ship_part(pos + 1);
    draw_ship_part(pos + (dir ? 32 : -32));
}

static void move_ship(byte speed) {
    if (dir) pos += speed; else pos -= speed;
}

static void control_ship(void) {
    byte now = SPACE_DOWN();
    if (now > key) dir = 1 - dir;
    move_ship(32);
    key = now;
}

static void draw_scrap(word i) {
    i = i & 0xfff;
    byte prev = *LINE(i);
    byte data = line_data[i];
#ifdef CPC
    data = data << 4;
#endif
    *LINE(i) = prev ^ data;
}

static void draw_debris(byte time) {
    if (time < 8) {
	byte spread = time >> (1 + (time & 1));
	draw_scrap(pos - spread);
	draw_scrap(pos + spread);
	draw_scrap(pos);
    }
}

static inline void death_clean_up(void) {
    if (die) draw_whole_ship(1);
}

static void draw_player(void) {
    if (die == 0) {
	draw_whole_ship(1);
	control_ship();
	draw_whole_ship(0);
	death_clean_up();
    }
    else {
	crash_sound();
	if (counter & 1) {
	    draw_debris(die >> 1);
	    move_ship(32);
	}
	die++;
    }
}

static void draw_field(void) {
    byte i = tail;
    while (i != head) {
	word r = ray[i++]++;
	*LINE(r) ^= line_data[r];
	if ((r & 0x1f) == 0x1f) tail++;
    }
}

static void load_level(void);
static void advance_level(void) {
    level++;
    done = 0;
    load_level();
    counter = 0xffff;
}

static void next_field(void) {
    if (flash > 0) {
	flash = flash - 1;
	if (flash == 0) {
	    advance_level();
	}
    }
    else if (!die && done) {
	flash = 32;
    }
}

static void emit_emptiness(void) {
    if (tail == head) done = 1;
}

static void push_whirlpool(word i) {
    for (word j = 0; j <= 0x800; j += 0x800) {
	ray[head++] = (i + j) & 0xfff;
    }
}

static void emit_whirlpool(word next, int8 dir) {
    const byte w = 10;
    word i = next << 5;
    if (counter == 0) {
	for (byte n = 0; n < w; n++) {
	    push_whirlpool(i);
	    i = i + dir;
	}
    }
    else if (counter < 256) {
	push_whirlpool(i + w * dir);
	push_whirlpool(i);
    }
    else {
	for (byte n = 0; n < w; n++) {
	    i = i + dir;
	    push_whirlpool(i);
	}
	emit_field = &emit_emptiness;
    }
}

static void emit_whirler(void) {
    emit_whirlpool(counter, -32);
}

static void emit_reverse(void) {
    emit_whirlpool(-counter, 32);
}

#ifdef ZXS
#define G3	1069	// 196.0Hz
#define B3	1347	// 246.9Hz
#define C4	1427	// 261.6Hz
#define D4	1602	// 293.7Hz
#define E4	1798	// 329.6Hz
#define F4	1905	// 349.2Hz
#define G4	2138	// 392.0Hz
#define A4	2400	// 440.0Hz

#define L2	4
#define L4	2
#endif
#ifdef CPC
#define G3	318
#define B3	253
#define C4	239
#define D4	213
#define E4	190
#define F4	179
#define G4	159
#define A4	142

#define L2	32
#define L4	16
#endif

static const word music[] = {
    C4, L4, C4, L4, G4, L4, G4, L4, A4, L4, A4, L4, G4, L2,
    F4, L4, F4, L4, E4, L4, E4, L4, D4, L4, D4, L4, C4, L2,
    G4, L4, G4, L4, F4, L4, F4, L4, E4, L4, E4, L4, D4, L2,
    G4, L4, G4, L4, F4, L4, F4, L4, E4, L4, E4, L4, D4, L2,
    C4, L4, C4, L4, G4, L4, G4, L4, A4, L4, A4, L4, G4, L2,
    F4, L4, F4, L4, E4, L4, E4, L4, D4, L4, D4, L4, C4, L2,
    0, 0
};

static const word chord[] = {
    C4, L2, E4, L2, F4, L2, E4, L2, D4, L2, C4, L2, G3, L4, B3, L4, C4, L2,
    C4, L2, F4, L2, C4, L2, G4, L2, C4, L2, F4, L2, C4, L2, G4, L2,
    C4, L2, E4, L2, F4, L2, E4, L2, D4, L2, C4, L2, G3, L4, B3, L4, C4, L2,
    0, 0
};

static byte is_vblank_start(void) {
#ifdef ZXS
    byte ret = vblank;
    if (ret) vblank = 0;
    return ret;
#endif
#ifdef CPC
    byte new = is_vsync();
    byte old = vblank;
    vblank = new;
    return new && !old;
#endif
}

static void delay(word loops) {
    for (word i = 0; i < loops; i++) { }
}

static const char * const outro[] = {
    " Unbelievable! You did it.",
    "You crazy son of a Belgium,",
    "        You did it!",
};

static void reverse(void) {
    word *addr = (word *) (word) line_addr;
    byte *data = (byte *) (word) line_data;
    for (word y = 0; y < 0x1000; y += 32) {
	for (word x = 0; x < 16; x++) {
	    word i = y + x;
	    word j = y + 31 - x;
	    word tmp_addr = addr[i];
	    byte tmp_data = data[i];
	    addr[i] = addr[j];
	    data[i] = data[j];
	    addr[j] = tmp_addr;
	    data[j] = tmp_data;
	}
    }
}

struct Channel {
    const word *tune;
    byte duration;
    word period;
    byte volume;
    byte number;
};

#ifdef CPC
static void play_note(struct Channel *channel) {
    byte number = channel->number;
    word period = channel->period;
    if (number == 0) period <<= 1;

    if (period > 0) {
	byte offset = (number << 1);
	cpc_psg(0 + offset, period & 0xff);
	cpc_psg(1 + offset, period >> 8);
    }

    cpc_psg(8 + number, channel->volume);
}
#endif

static byte music_done;
static void next_note(struct Channel *channel) {
    const word *tune = channel->tune;
    if (tune[1] == 0) {
	music_done = 1;
	return;
    }
    channel->period = tune[0];
    channel->duration = 0;
    channel->volume = 0xf;
#ifdef CPC
    play_note(channel);
#endif
}

static void advance_channel(struct Channel *channel) {
    byte duration = ++channel->duration;
    byte length = channel->tune[1];
    byte number = channel->number;
    byte volume = channel->volume;
    byte decay = length >> 1;

    if (duration >= decay && volume > 0) {
#ifdef CPC
	channel->volume >>= 1;
	play_note(channel);
#else
	channel->volume = 0;
#endif
    }
    if (duration >= length) {
	channel->tune += 2;
	next_note(channel);
    }
}

static void beeper(struct Channel *channel) {
    channel;

#ifdef ZXS
    byte v0 = channel[0].volume;
    word p0 = channel[0].period >> 1;
    word c0 = 0;

    byte v1 = channel[1].volume;
    word p1 = channel[1].period;
    word c1 = 0;

    __asm__("di");
    for (word i = 0; i < 2048 / L4; i++) {
	c0 += p0;
	out_fe(c0 >= 32768 && v0 > 0 ? 0x10 : 0x00);
	c1 += p1;
	out_fe(c1 >= 32768 && v1 > 0 ? 0x10 : 0x00);
    }
    __asm__("ei");
    vblank = 1;
#endif
}

static void finish_game(void) {
    clear_screen();
    put_str("GAME COMPLETE", 9, 12, 0x42);
    draw_image(title, 4, 3, 24, 5);

    for (byte i = 0; i < SIZE(outro); i++) {
	put_str(outro[i], 2, 17 + i, 0x42);
    }

    static struct Channel channels[2];
    for (byte i = 0; i < SIZE(channels); i++) {
	struct Channel *ptr = channels + i;
	ptr->tune = (i == 0 ? chord : music);
	ptr->number = i;
	next_note(ptr);
    }

    music_done = 0;
    while (!SPACE_DOWN()) {
	if (!music_done) {
	    beeper(channels);

	    if (is_vblank_start()) {
		advance_channel(channels + 0);
		advance_channel(channels + 1);
	    }
	}
    }
#ifdef CPC
    for (byte i = 0; i < 3; i++) cpc_psg(8 + i, 0);
#endif
    reset();
}

static byte launch_position(void) {
    return (pos & 0x01f) < 8 && (pos & 0xfff) < 256;
}

static void hyperspace_sound(word j, byte clear) {
    word pitch = (clear ? 160 : 320) - (j << 4);

#ifdef ZXS
    while (!vblank) {
	out_fe(0x10);
	delay_vblank(pitch);
	out_fe(0x0);
	delay_vblank(pitch);
    }
    vblank = 0;
#endif
#ifdef CPC
    pitch <<= 1;
    if (j & 1) wait_vblank();
    if (j == 0) cpc_psg(8, 0x0F);
    cpc_psg(0, pitch & 0xff);
    cpc_psg(1, pitch >> 8);
#endif
}

static void hyperspace_streaks(word *lines, byte clear) {
    for (word j = 0; j < STREAK; j++) {
	hyperspace_sound(j, clear);
	for (byte i = 0; i < 3; i++) {
	    word addr = lines[i];
	    if (j > STREAK - i - 1) break;
	    if (dir) addr += j; else addr -= j;
	    BYTE(addr) = clear ? 0 : 0xff;
	}
    }
}

static word addr_of(word i) {
    return line_addr[i & 0xfff];
}

static void emit_slinger(void) {
    byte faster = 0;
    byte close = 0;
    byte speed = 32;
    while (!launch_position()) {
	wait_vblank();
	draw_whole_ship(1);
	move_ship(speed);
	if (!faster) speed += 32;
	if (!close) pos--;
	draw_whole_ship(0);
	faster += 4;
	close += 16;
    }
    word lines[3];
    lines[0] = addr_of(pos + (dir ? 32 : -32));
    lines[1] = addr_of(pos - 1);
    lines[2] = addr_of(pos + 1);
    hyperspace_streaks(lines, 0);
    hyperspace_streaks(lines, 1);
#ifdef CPC
    cpc_psg(8, 0x10);
#endif
    for (byte i = 0; i < 50; i++) {
	while (!is_vblank_start()) { }
    }
    reverse();
    finish_game();
}

static void update_field(void) {
    byte amount = *(current++);
    for (byte i = 0; i < amount; i++) {
	word emit = *(current++);
	ray[head++] = emit << 5;
    }
}

static void emit_cleanup(void) {
    current = start;
    update_field();
    emit_field = &emit_emptiness;
}

static void emit_generated(void) {
    wait--;
    if (wait == 0) {
	update_field();
	wait = *(current++);
	if (wait == 0) {
	    if (repeat > 1) {
		repeat--;
		current = wrap;
		wait = *(current++);
	    }
	    else {
		emit_field = &emit_cleanup;
	    }
	}
    }
}

static void load_generated(byte n, const byte *ptr) {
    repeat = n;
    start = ptr;
    current = ptr;
    update_field();
    wrap = current;
    wait = *(current++);
    emit_field = &emit_generated;
}

static void emit_squigle(void) {
    load_generated(8, squiggly);
}

static void emit_diamond(void) {
    load_generated(1, diamonds);
}

static void emit_rings(void) {
    load_generated(7, rings);
}

static void emit_gamma(void) {
    load_generated(8, gamma);
}

static void emit_curve(void) {
    load_generated(2, curve);
}

static void emit_twinkle(void) {
    load_generated(3, twinkle);
}

static void emit_number(void) {
    load_generated(1, number);
}

static void emit_bubbles(void) {
    load_generated(1, bubbles);
}

static void emit_solaris(void) {
    load_generated(2, solaris);
}

static void emit_radiate(void) {
    load_generated(2, radiate);
}

static const struct Level level_list[] = {
    { &emit_whirler, "WHIRLER" },
    { &emit_reverse, "REVERSE" },
    { &emit_squigle, "WAVEING" },
    { &emit_solaris, "*SOLAR*" },
    { &emit_radiate, "RADIATE" },
    { &emit_curve,   "&CURVES" },
    { &emit_diamond, "DIAMOND" },
    { &emit_number,  "NUMBER?" },
    { &emit_rings,   "(RINGS)" },
    { &emit_bubbles, "BUBBLES" },
    { &emit_twinkle, "TWINKLE" },
    { &emit_gamma,   "/GAMMA/" },
    { &emit_slinger, "SLING->" },
};

static byte text_pos(byte i) {
    return ((24 - SIZE(level_list)) >> 1) + i;
}

static void draw_level_tab(void) {
    byte y1 = text_pos(0) - 1;
    byte y2 = text_pos(SIZE(level_list));
    draw_tile(EDGE(0x18), 0x1F, y1, 0x02);
    draw_tile(EDGE(0x08), 0x1F, y2, 0x02);

    for (byte i = 24; i < 31; i++) {
	draw_tile(EDGE(0x68), i, y1, 0x02);
	draw_tile(EDGE(0x68), i, y2, 0x02);
    }
    for (byte i = y1 + 1; i < y2; i++) {
	draw_tile(EDGE(0x70), 0x1F, i, 0x02);
    }

    flip_V(1,  48, 24, (y1 << 3) - 24, 2, 24);
    flip_V(1, 120, 24, (y2 << 3) + 8, 2, 24);
}

static void load_level(void) {
    if (level < SIZE(level_list)) {
	put_str(level_list[level].msg, 24, text_pos(level), 0x02);
	emit_field = level_list[level].fn;
    }
}

static void clear_field(void) {
    for (byte y = 0; y < 32; y++) {
	for (word x = 0; x < 0x1000; x += 32) {
	    byte data = line_data[x + y];
#ifdef CPC
	    data = data | (data << 4);
#endif
	    *LINE(x + y) &= ~data;
	}
    }
}

static void init_variables(void) {
    head = tail = 0;
    counter = 0;
    flash = 0;
    done = 0;
    pos = 28;
    dir = 1;
    key = 1;
    die = 0;
}

static void reset_variables(void) {
    level = 0;
    lives = 5;
}

static void game_loop(void) {
    load_level();
    init_variables();
    draw_whole_ship(0);
    while (die < 32) {
	wait_vblank();
	draw_player();
	emit_field();
	draw_field();
	next_field();
	counter++;
    }
    clear_field();
}

void reset(void) {
    SETUP_STACK();
    setup_system();
    clear_screen();
    precalculate();

    draw_title();
    clear_screen();
    reset_variables();
    draw_hud();

    while (lives >= 0) {
	game_loop();
	take_life();
    }
    clear_screen();
    game_over();
    reset();
}
