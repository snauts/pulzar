#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>

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

static void hexdump(unsigned char *buf, int size) {
    for (int i = 0; i < size; i++) {
	fprintf(stderr, "%02x ", buf[i]);
	if ((i & 0xf) == 0xf) {
	    fprintf(stderr, "\n");
	}
    }
    if ((size & 0xf) != 0x0) fprintf(stderr, "\n");
}

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
    int n = 0;
    y = y % height;
    for (int x = 0; x < 128; x++) {
	if (unfold[x][y] != unfold[x][(y - 1) % height]) diff[n++] = x;
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
    if (wait > 1) save_diff(diff, amount, &index, wait);
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
    float q = 1.0;
    for (unsigned y = 1; y < 250; y += 5) {
	unsigned x = roundf(q);
	for (unsigned i = 0; i < 3; i++) {
	    unfold[x % 128][y + i] = 1;
	    unfold[(x - 1 + i) % 128][y + 1] = 1;
	}
	q = q + 128.0 * 1.618033;
    }
    return 250;
}

static void save_game(void) {
    save_buffer("squiggly", &squiggly);
    save_buffer("diamonds", &diamonds);
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
