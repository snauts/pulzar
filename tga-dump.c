#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

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

static unsigned short on_pixel(unsigned char *buf, int i, int w) {
    unsigned char pixel = buf[i];
    for (int y = 0; y < 8; y++) {
	for (int x = 0; x < 8; x++) {
	    unsigned char next = buf[i + x];
	    if (next != pixel) {
		add_color(next);
		add_color(pixel);
		return (next << 8) | pixel;
	    }
	}
	i += w;
    }
    add_color(pixel);
    return pixel;
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

static void save_bitmap(struct Header *header, unsigned char *buf, int size) {
    int j = 0;
    char name[256];
    unsigned short on[size / 64];
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
	printf(" /* %s attributes */\n", name);
	for (int i = 0; i < size / 64; i++) {
	    printf(" 0x%02x,", encode_ink(on[i]));
	    if ((i % 8) == 7) printf("\n");
	}
    }
    printf("};\n");
}

int main(int argc, char **argv) {
    if (argc < 3) {
	printf("USAGE: tga-dump [option] file.tga\n");
	printf("  -b   save bitmap zx\n");
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
