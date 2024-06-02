CFLAGS += -mz80 --nostdinc --nostdlib --no-std-crt0
CFLAGS += --code-loc 0x8000 --data-loc 0xf000

ENTRY = grep _reset pulzar.map | cut -d " " -f 6

all:
	gcc -lm tga-dump.c -o tga-dump
	./tga-dump -b title.tga 10 11 14 > data.h
	./tga-dump -b edge.tga >> data.h
	./tga-dump -b star.tga 10 14 15 >> data.h
	./tga-dump -b circuit.tga 2 10 >> data.h
	./tga-dump -l >> data.h
	@sdcc $(CFLAGS) main.c -o pulzar.ihx
	hex2bin pulzar.ihx > /dev/null
	@make tap

tap:
	bin2tap -b -r $(shell printf "%d" 0x$$($(ENTRY))) pulzar.bin

fuse: all
	fuse --no-confirm-actions -g 2x pulzar.tap

clean:
	rm -f pulzar* data.h tga-dump
