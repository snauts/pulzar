#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>

// #define DEBUG

static char *file_name;
static int color_index = 1;
static unsigned char inkmap[256];
static unsigned char colors[256];

struct Header {
    unsigned char id;
    unsigned char color_type;
    unsigned char image_type;
    unsigned char color_map[5];
    unsigned short x, y, w, h;
    unsigned char depth;
    unsigned char desc;
};

#ifdef DEBUG
static void hexdump(unsigned char *buf, int size) {
    for (int i = 0; i < size; i++) {
	fprintf(stderr, "%02x ", buf[i]);
	if ((i & 0xf) == 0xf) {
	    fprintf(stderr, "\n");
	}
    }
    if ((size & 0xf) != 0x0) fprintf(stderr, "\n");
}
#endif

static void remove_extension(char *src, char *dst) {
    for (int i = 0; i < strlen(src); i++) {
	if (src[i] == '.') {
	    dst[i] = 0;
	    return;
	}
	else if (src[i] == '/') {
	    dst[i] = '_';
	}
	else {
	    dst[i] = src[i];
	}
    }
}

static unsigned char consume_pixels(unsigned char *buf, unsigned char on) {
    unsigned char ret = 0;
    for (int i = 0; i < 8; i++) {
	ret = ret << 1;
	ret |= (buf[i] == on) ? 1 : 0;
    }
    return ret;
}

static void add_color(unsigned char pixel) {
    if (pixel > 0 && inkmap[pixel] == 0) {
	inkmap[pixel] = colors[color_index++];
    }
}

static unsigned short encode_pixel(unsigned char a, unsigned char b) {
    return a > b ? (b << 8) | a : (a << 8) | b;
}

static unsigned short on_pixel(unsigned char *buf, int i, int w) {
    unsigned char pixel = buf[i];
    for (int y = 0; y < 8; y++) {
	for (int x = 0; x < 8; x++) {
	    unsigned char next = buf[i + x];
	    if (next != pixel) {
		add_color(next);
		add_color(pixel);
		return encode_pixel(next, pixel);
	    }
	}
	i += w;
    }
    add_color(pixel);
    return pixel == 0 ? 0x1 : pixel;
}

static int ink_index(struct Header *header, int i) {
    return (i / header->w / 8) * (header->w / 8) + i % header->w / 8;
}

static unsigned char encode_ink(unsigned short colors) {
    unsigned char f = inkmap[colors & 0xff];
    unsigned char b = inkmap[colors >> 8];
    unsigned char l = (f > 7 || b > 7) ? 0x40 : 0x00;
    return l | (f & 7) | ((b & 7) << 3);
}

static int has_any_color(void) {
    for (int i = 0; i < 256; i++) {
	if (colors[i] != 0) return 1;
    }
    return 0;
}

static void dump_buffer(void *ptr, int size, int step) {
    for (int i = 0; i < size; i++) {
	if (step == 1) {
	    printf(" 0x%02x,", * (unsigned char *) ptr);
	}
	else {
	    printf(" 0x%04x,", * (unsigned short *) ptr);
	}
	if ((i & 7) == 7) printf("\n");
	ptr += step;
    }
    if ((size & 7) != 0) printf("\n");
}

static void save_bitmap(struct Header *header, unsigned char *buf, int size) {
    int j = 0;
    char name[256];
    int attribute_size = size / 64;
    unsigned short on[attribute_size];
    remove_extension(file_name, name);
    printf("const byte %s[] = {\n", name);
    for (int i = 0; i < size; i += 8) {
	if (i / header->w % 8 == 0) {
	    on[j++] = on_pixel(buf, i, header->w);
	}
	unsigned char pixel = on[ink_index(header, i)] & 0xff;
	printf(" 0x%02x,", consume_pixels(buf + i, pixel));
	if ((i % 64) == 56) printf("\n");
    }
    if (has_any_color()) {
	unsigned char buf[attribute_size];
	printf(" /* %s attributes */\n", name);
	for (int i = 0; i < attribute_size; i++) {
	    buf[i] = encode_ink(on[i]);
	}
	dump_buffer(buf, attribute_size, 1);
    }
    printf("};\n");
}

static unsigned short pixel_addr(int x, int y) {
    int f = ((y & 7) << 3) | ((y >> 3) & 7) | (y & 0xc0);
    return 0x4000 + (f << 5) + (x >> 3);
}

static unsigned char pixel_data(int x) {
    return (1 << (7 - (x & 7)));
}

unsigned char line_data[0x10000];
unsigned short line_addr[0x10000];

static void save_lines(void) {
    int size = 0;
    for (int angle = 0; angle < 128; angle++) {
	float a = 2 * M_PI * (angle / 128.0);
	for (int step = 24; step < 24 + 64; step += 2) {
	    int x = roundf(95.5 + sin(a) * (step + 1.0));
	    int y = roundf(95.5 + cos(a) * (step + 1.0));
	    line_addr[size] = pixel_addr(x, y);
	    line_data[size] = pixel_data(x);
	    size++;
	}
    }
    printf("const word line_addr[] = {\n");
    dump_buffer(line_addr, size, 2);
    printf("};\n");
    printf("const byte line_data[] = {\n");
    dump_buffer(line_data, size, 1);
    printf("};\n");
}

unsigned char unfold[128][512];
unsigned char level[sizeof(unfold)];

static int get_diff(unsigned char *diff, unsigned y, int height) {
    int i, n = 0;
    y = y % height;
    for (int x = 0; x < 128; x++) {
	i = (y + height - 1) % height;
	if (unfold[x][y] != unfold[x][i]) diff[n++] = x;
    }
    return n;
}

static int get_line(unsigned char *diff, int y) {
    int n = 0;
    for (int x = 0; x < 128; x++) {
	if (unfold[x][y]) diff[n++] = x;
    }
    return n;
}

static void save_diff(unsigned char *diff, int amount, int *index, int wait) {
    if (wait >= 0) {
	level[(*index)++] = wait;
    }
    level[(*index)++] = amount;
    for (int i = 0; i < amount; i++) {
	level[(*index)++] = diff[i];
    }
}

static int serialize(int height) {
    int amount;
    int wait = 1;
    int index = 0;
    unsigned char diff[128];
#ifdef DEBUG
    for (int y = 0; y < height; y++) {
	for (int x = 0; x < 128; x++) {
	    fprintf(stderr, "%d", unfold[x][y]);
	}
	fprintf(stderr, "\n");
    }
#endif
    save_diff(diff, get_line(diff, 0), &index, -1);
    for (int y = 1; y <= height; y++) {
	amount = get_diff(diff, y, height);
	if (amount > 0) {
	    save_diff(diff, amount, &index, wait);
	    wait = 1;
	}
	else {
	    wait++;
	}
    }
    level[index++] = 0;
    return index;
}

static void save_buffer(char *name, int (*fill)(void)) {
    memset(unfold, 0, sizeof(unfold));
    int size = serialize(fill());
    fprintf(stderr, "LEVEL:%s SIZE:%d\n", name, size);
    printf("const byte %s[] = {\n", name);
    dump_buffer(level, size, 1);
    printf("};\n");
}

static unsigned char squiggly_interval(int x, int n) {
    return  (4 + n < x && x <  8 + n)
	|| (46 + n < x && x < 50 + n)
	|| (88 + n < x && x < 92 + n);
}

static int squiggly(void) {
    for (int y = 0; y < 32; y++) {
	for (int x = 0; x < 128; x++) {
	    int offset = roundf(4.0 * sin(2 * M_PI * y / 32.0));
	    unfold[x][y] = squiggly_interval(x, offset);
	}
    }
    return 32;
}

static int diamonds(void) {
    float q = 10.0;
    for (unsigned y = 1; y < 251; y += 5) {
	unsigned x = roundf(q);
	for (unsigned i = 0; i < 3; i++) {
	    unfold[x % 128][y + i] = 1;
	    unfold[(x - 1 + i) % 128][y + 1] = 1;
	}
	q = q + (128.0 * 1.618033);
    }
    return 255;
}

static int rings(void) {
    float q = 1.0;
    for (unsigned x = 0; x < 32; x++) {
	for (unsigned y = 30; y < 32; y++) {
	    unfold[x +  0][y] = 1;
	    unfold[x + 64][y] = 1;
	}
	for (unsigned y = 62; y < 64; y++) {
	    unfold[x + 32][y] = 1;
	    unfold[x + 96][y] = 1;
	}
    }
    return 64;
}

static void gamma_ray(int x, int y) {
    for (unsigned i = 0; i < 8; i++) {
	unfold[x][y + i] = 1;
    }
}

static int gamma_rain(void) {
    int offset = 0;
    for (unsigned y = 0; y < 32; y += 16) {
	for (unsigned x = 0; x < 128; x += 32) {
	    gamma_ray((x + offset + 8) % 128, y + 1);
	}
	offset = 16 - offset;
    }
    return 32;
}

static int curve(void) {
    int offset = 0;
    for (unsigned y = 0; y < 128; y++) {
	for (unsigned x = 0; x < 128; x++) {
	    float offset = 16.0 * sin(2 * M_PI * y / 64.0);
	    unfold[x][y] = roundf(40.0 - offset) < x
		&& x < roundf(88.0 + offset);
	}
	unsigned char buf[128];
	for (unsigned x = 0; x < 128; x++) {
	    buf[x] = unfold[(x + y) % 128][y];
	}
	for (unsigned x = 0; x < 128; x++) {
	    unfold[x][y] = buf[x];
	}
    }
    return 128;
}

static int twinkle_lines(int center, int offset, int y) {
    for (int i = center - offset; i < center + offset; i++) {
	unfold[i % 128][y] = 1;
    }
}

static void line(int x1, int y1, int x2, int y2) {
    float dx = (x2 - x1);
    float dy = (y2 - y1);
    float len = sqrtf(dx * dx + dy * dy);
    if (len > 0.0) {
	dx = dx / len;
	dy = dy / len;
	for (float i = 0; i <= len; i += 0.5) {
	    int px = roundf(x1 + dx * i);
	    int py = roundf(y1 + dy * i);
	    unfold[(px + 128) % 128][(py + 512) % 512] = 1;
	}
    }
}

static void circle(int x, int y, int r1, int r2) {
    for (float i = 0; i <= 2 * M_PI; i += 0.01) {
	int dx = roundf((float) r1 * sin(i));
	int dy = roundf((float) r2 * cos(i));
	line(x, y, x + dx, y + dy);
    }
}

static void little_star(int x, int y) {
    for (int i = 0; i < 5; i++) {
	float angle = 2 * M_PI * i / 5.0  + M_PI / 2;
	float dx = roundf(6 * sin(angle));
	float dy = roundf(6 * cos(angle));
	for (int a = -1; a <= 1; a++) {
	    for (int b = -1; b <= 1; b++) {
		line(x + a, y + b, x + dx, y + dy);
	    }
	}
    }
}

static int twinkle(void) {
    int offset = 0;
    const int size = 32;
    for (unsigned y = 0; y < 3 * size; y++) {
	if (y < size) {
	    twinkle_lines(size, y, y + 1);
	}
	else if (y < 2 * size) {
	    twinkle_lines(size, 2 * size - y - 1, y + 1);
	    twinkle_lines(128 - size, y - size, y + 1);
	}
	else if (y < 3 * size) {
	    twinkle_lines(128 - size, 3 * size - y - 1, y + 1);
	}
    }
    little_star(32 + 16, 80);
    little_star(96 + 16, 16);
    return 3 * size + 2;
}

static int four[64] = {
    0,0,0,1,1,1,0,0,
    0,0,1,1,1,1,0,0,
    0,1,1,0,1,1,0,0,
    1,1,0,0,1,1,0,0,
    1,1,1,1,1,1,1,0,
    0,0,0,0,1,1,0,0,
    0,0,0,0,1,1,0,0,
    0,0,0,0,0,0,0,0,
};

static int two[64] = {
    0,1,1,1,1,1,0,0,
    1,1,0,0,0,1,1,0,
    0,0,0,0,1,1,1,0,
    0,0,0,1,1,1,0,0,
    0,0,1,1,1,0,0,0,
    0,1,1,1,0,0,0,0,
    1,1,1,1,1,1,1,0,
    0,0,0,0,0,0,0,0,
};

static float interpolate(float a, float b, float q) {
    return a * q + b * (1.0 - q);
}

static void rotate_ccw(int *ptr, int w, int h) {
    int tmp[w * h];
    for (int y = 0; y < h; y++) {
	for (int x = 0; x < w; x++) {
	    tmp[x * h + y] = ptr[y * w + (w - x - 1)];
	}
    }
    memcpy(ptr, tmp, sizeof(tmp));
}

static void flip_horizontal(int *ptr, int w, int h) {
    for (int y = 0; y < h; y++) {
	for (int x = 0; x < w / 2; x++) {
	    int left = y * w + x;
	    int right = y * w + (w - x - 1);
	    int tmp = ptr[left];
	    ptr[left] = ptr[right];
	    ptr[right] = tmp;
	}
    }
}

static void scale(int *buf, int x, int y, int w, int h, int scale) {
    for (int dy = 0; dy < h - 1; dy++) {
	for (int dx = 0; dx < w - 1; dx++) {
	    int p1 = buf[(dy + 0) * w + (dx + 0)];
	    int p2 = buf[(dy + 0) * w + (dx + 1)];
	    int p3 = buf[(dy + 1) * w + (dx + 0)];
	    int p4 = buf[(dy + 1) * w + (dx + 1)];
	    for (int sy = 0; sy < scale; sy++) {
		for (int sx = 0; sx < scale; sx++) {
		    int fx = x + dx * scale + sx;
		    int fy = y + dy * scale + sy;
		    float qx = (float) sx / (float) (scale - 1);
		    float qy = (float) sy / (float) (scale - 1);
		    float a = interpolate(p1, p2, 1.0 - qx);
		    float b = interpolate(p3, p4, 1.0 - qx);
		    unfold[fx][fy] = interpolate(a, b, (1.0 - qy)) > 0.5;
		}
	    }
	}
    }
}

static int number(void) {
    flip_horizontal(four, 8, 8);
    rotate_ccw(four, 8, 8);
    scale(four, 80, 1, 8, 8, 4);

    rotate_ccw(two, 8, 8);
    flip_horizontal(two, 8, 8);
    scale(two,  16, 1, 8, 8, 4);
    return 40;
}

static int bubbles(void) {
    float q = 0.0;
    const int size = 8;
    for (unsigned y = size + 1; y < size * 20; y += (size + 1)) {
	q = q + (128.0 * 1.618033);
	unsigned x = roundf(q);
	circle(x, y, 6, 8);
    }
    return 21 * size;
}

static void save_game(void) {
    save_buffer("squiggly", &squiggly);
    save_buffer("diamonds", &diamonds);
    save_buffer("rings", &rings);
    save_buffer("gamma", &gamma_rain);
    save_buffer("curve", &curve);
    save_buffer("twinkle", &twinkle);
    save_buffer("number", &number);
    save_buffer("bubbles", &bubbles);
}

int main(int argc, char **argv) {
    if (argc < 2) {
	printf("USAGE: tga-dump [option] file.tga\n");
	printf("  -b   save bitmap zx\n");
	printf("  -l   save line data\n");
	printf("  -g   save game data\n");
	return 0;
    }

    switch (argv[1][1]) {
    case 'l':
	save_lines();
	return 0;
    case 'g':
	save_game();
	return 0;
    }

    file_name = argv[2];
    int fd = open(file_name, O_RDONLY);
    if (fd < 0) {
	printf("ERROR: unable to open %s\n", file_name);
	return -ENOENT;
    }

    struct Header header;
    read(fd, &header, sizeof(header));

    if (header.image_type != 3 || header.depth != 8) {
	printf("ERROR: not a grayscale 8-bit TGA file\n");
	close(fd);
	return 0;
    }

    int size = header.w * header.h;
    unsigned char buf[size];
    read(fd, buf, size);
    close(fd);

    switch (argv[1][1]) {
    case 'b':
	for (int i = 3; i < argc; i++) {
	    colors[i - 2] = atoi(argv[i]);
	}
	memset(inkmap, 0, sizeof(inkmap));
	save_bitmap(&header, buf, size);
	break;
    }

    return 0;
}
