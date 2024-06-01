typedef signed char int8;
typedef unsigned char byte;
typedef unsigned short word;

#define DEBUG

#define ADDR(obj)	((word) (obj))
#define BYTE(addr)	(* (volatile byte *) (addr))
#define WORD(addr)	(* (volatile word *) (addr))
#define SIZE(array)	(sizeof(array) / sizeof(*(array)))

#define SETUP_STACK()	__asm__("ld sp, #0xFDFC")
#define SPACE_DOWN()	!(in_fe(0x7f) & 0x01)

#define LINE(x)		(byte *) (line_addr[x])

#define IRQ_BASE	0xfe00

#include "data.h"

static volatile byte vblank;
static byte *map_y[192];

static word counter;
static byte level;
static byte flash;
static int8 lives;

static word pos;
static byte dir;
static byte key;
static byte clr;
static byte die;

static word ray[256];
static byte r_head, r_tail;

static word wipe[256];
static byte w_head, w_tail;

void reset(void);
static void (*emit_field)(void);

struct Level {
    void (*fn)(void);
    const char *msg;
};

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

static void vblank_delay(word ticks) {
    for (word i = 0; i < ticks; i++) { if (vblank) break; }
}

static void crash_sound(void) {
    if (die && die < 8) {
	word pitch = 8 + (die << 3);
	out_fe(0x10);
	vblank_delay(pitch);
	out_fe(0x0);
	vblank_delay(pitch);
    }
}

static void level_sound(void) {
    static const byte jerk_color[] = { 10, 0, 14, 15 };
    if (flash) {
	byte color = jerk_color[flash & 3];
	out_fe(0x10 | color);
	vblank_delay(flash);
	out_fe(0x0 | color);
	vblank_delay(flash);
    }
}

static void wait_vblank(void) {
    while (!vblank) {
	crash_sound();
	level_sound();
    }
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

static void clear_screen(void) {
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
    draw_tile(edge + offset, 0x16 - pos, 0x17, 0x02);
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

    for (int8 i = 0; i < lives; i++) {
	life_sprite(0x60, i);
    }

    draw_image(star, 9, 9, 6, 6);
}

static void take_life(void) {
    if (--lives >= 0) life_sprite(0x40, lives);
}

static void draw_ship_part(word i) {
    i = i & 0xfff;
    byte prev = *LINE(i);
    byte data = line_data[i];
    data |= dir ? (data << 1) : (data >> 1);
    if (clr == !(prev & data)) die = 1;
    *LINE(i) = prev ^ data;
}

static void draw_whole_ship(byte clear_ship) {
    clr = clear_ship;
    draw_ship_part(pos - 1);
    draw_ship_part(pos + 1);
    draw_ship_part(pos + (dir ? 32 : -32));
}

static void move_ship(void) {
    if (dir) pos += 32; else pos -= 32;
}

static void control_ship(void) {
    byte now = SPACE_DOWN();
    if (now > key) dir = 1 - dir;
    move_ship();
    pos = pos;
    key = now;
}

static void draw_scrap(word i) {
    i = i & 0xfff;
    byte prev = *LINE(i);
    byte data = line_data[i];
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
	if (counter & 1) {
	    draw_debris(die >> 1);
	    move_ship();
	}
	die++;
    }
}

static void draw_field(void) {
    byte i = r_tail;
    while (i != r_head) {
	word r = ray[i++]++;
	*LINE(r) ^= line_data[r];
	if ((r & 0x1f) == 0x1f) r_tail++;
    }
}

static void load_level(void);
static void next_field(void) {
    if (flash > 0) {
	flash = flash - 1;
	if (flash == 0) {
	    level++;
	    load_level();
	}
    }
    else if (r_tail == r_head && !die) {
	flash = 32;
    }
}

static void push_wipe(word i) {
    ray[r_head++] = i;
    wipe[w_head++] = i;
}

static void pop_wipe(void) {
    ray[r_head++] = wipe[w_tail++];
}

static inline byte empty_wipe (void) {
    return w_head == w_tail;
}

static void init_wipe(void) {
    if (counter == 0) {
	w_head = 0;
	w_tail = 0;
    }
}

static void emit_whirlpool(word dir) {
    init_wipe();
    word i = (dir & 0x7f) << 5;
    if (counter > 8 && !empty_wipe()) {
	pop_wipe();
	pop_wipe();
    }
    if (counter < 256) {
	push_wipe(i);
	push_wipe((i + 0x800) & 0xfff);
    }
    else {
	while (!empty_wipe()) {
	    pop_wipe();
	}
    }
}

static void emit_whirler(void) {
    emit_whirlpool(counter);
}

static void emit_reverse(void) {
    emit_whirlpool(-counter);
}

#define C4	169	// 261.6Hz
#define D4	146	// 293.7Hz
#define E4	129	// 329.6Hz
#define F4	124	// 349.2Hz
#define G4	109	// 392.0Hz
#define A4	96	// 440.0Hz

#define L2  40
#define L4  20

static const byte music[] = {
    C4, L4, C4, L4, G4, L4, G4, L4, A4, L4, A4, L4, G4, L2,
    F4, L4, F4, L4, E4, L4, E4, L4, D4, L4, D4, L4, C4, L2,
    G4, L4, G4, L4, F4, L4, F4, L4, E4, L4, E4, L4, D4, L2,
    G4, L4, G4, L4, F4, L4, F4, L4, E4, L4, E4, L4, D4, L2,
    C4, L4, C4, L4, G4, L4, G4, L4, A4, L4, A4, L4, G4, L2,
    F4, L4, F4, L4, E4, L4, E4, L4, D4, L4, D4, L4, C4, L2,
    0, 0
};

static byte is_vblank_start(void) {
    byte ret = vblank;
    if (ret) vblank = 0;
    return ret;
}

static void delay(word loops) {
    for (word i = 0; i < loops; i++) { }
}

static void finish_game(void) {
    const byte *tune = music;

    word offset = 0;
    byte duration = 0;
    word period = tune[0];

    clear_screen();
    put_str("GAME COMPLETE", 9, 12, 0x42);

    while (!SPACE_DOWN()) {
	if (period > offset) {
	    out_fe(0x10);
	    delay(period - offset);
	    out_fe(0x00);
	    delay(period + offset);
	}
	if (is_vblank_start()) {
	    duration++;
	    if (duration >= tune[1] >> 3) {
		offset += period >> 4;
	    }
	    if (duration >= tune[1]) {
		tune += 2;
		if (tune[1] == 0) {
		    wait_space();
		    break;
		}
		period = *tune;
		duration = 0;
		offset = 0;
	    }
	}
    }
    reset();
}

static const struct Level level_list[] = {
    { &emit_whirler, "/WHIRLER" },
    { &emit_reverse, "REVERSER" },
};

static void load_level(void) {
    counter = 0;
    if (level < SIZE(level_list)) {
	put_str(level_list[level].msg, 24, level, 0x02);
	emit_field = level_list[level].fn;
    }
    else {
	finish_game();
    }
}

static void clear_field(void) {
    for (byte y = 0; y < 32; y++) {
	for (word x = 0; x < 0x1000; x += 32) {
	    *LINE(x + y) &= ~line_data[x + y];
	}
    }
}

static void init_variables(void) {
    r_head = r_tail = 0;
    flash = 0;
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
