CFLAGS += -mz80 --nostdinc --nostdlib --no-std-crt0
CFLAGS += --code-loc 0x8000 --data-loc 0xf000

ENTRY = grep _main pulzar.map | cut -d " " -f 6

all:
	gcc $(TYPE) tga-dump.c -o tga-dump
	./tga-dump -b title.tga 2 3 6 > data.h
	@sdcc $(CFLAGS) main.c -o pulzar.ihx
	hex2bin pulzar.ihx > /dev/null
	bin2tap -b -r $(shell printf "%d" 0x$$($(ENTRY))) pulzar.bin

fuse: all
	fuse --no-confirm-actions -g 2x pulzar.tap

clean:
	rm -f pulzar* data.h tga-dump
