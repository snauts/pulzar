#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

static char *file_name;

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

static unsigned char on_pixel(unsigned char *buf, int i, int w) {
    unsigned char pixel = buf[i];
    for (int y = 0; y < 8; y++) {
	for (int x = 0; x < 8; x++) {
	    unsigned char next = buf[i + x];
	    if (next != pixel) {
		return next > pixel ? next : pixel;
	    }
	}
	i += w;
    }
    return pixel >= 0x80 ? pixel : 0x80;
}

static void save_bitmap(struct Header *header, unsigned char *buf, int size) {
    char name[256];
    unsigned char on[header->w / 8];
    remove_extension(file_name, name);
    printf("const byte %s[%d] = {\n", name, size / 8);
    for (int i = 0; i < size; i += 8) {
	if (i / header->w % 8 == 0) {
	    on[i % header->w / 8] = on_pixel(buf, i, header->w);
	}
	printf(" 0x%02x,", consume_pixels(buf + i, on[i % header->w / 8]));
	if ((i % 64) == 56) printf("\n");
    }
    printf("};\n");
}

int main(int argc, char **argv) {
    if (argc != 3) {
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
	save_bitmap(&header, buf, size);
	break;
    }

    return 0;
}
