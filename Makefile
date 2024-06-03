CFLAGS += -mz80 --nostdinc --nostdlib --no-std-crt0
CFLAGS += --code-loc $(CODE) --data-loc $(DATA)

ENTRY = grep _reset pulzar.map | cut -d " " -f 6

all:
	@echo "make zxs" - build .tap for ZX Spectrum
	@echo "make cpc" - build .dsk for Amstrad CPC
	@echo "make fuse" - build and run fuse
	@echo "make mame" - build and run mame

prg:
	gcc $(TYPE) -lm tga-dump.c -o tga-dump
	./tga-dump -b title.tga 10 11 14 > data.h
	./tga-dump -b edge.tga >> data.h
	./tga-dump -b star.tga 10 14 15 >> data.h
	./tga-dump -b circuit.tga 2 10 >> data.h
	./tga-dump -l >> data.h
	./tga-dump -g >> data.h
	@sdcc $(CFLAGS) $(TYPE) main.c -o pulzar.ihx
	hex2bin pulzar.ihx > /dev/null

zxs:
	CODE=0x8000 DATA=0xf000	TYPE=-DZXS make prg
	bin2tap -b -r $(shell printf "%d" 0x$$($(ENTRY))) pulzar.bin

cpc:
	iDSK -n pulzar.dsk
	CODE=0x1000 DATA=0x8000	TYPE=-DCPC make prg
	iDSK pulzar.dsk -f -t 1 -c 1000 -e $(shell $(ENTRY)) -i pulzar.bin

mame: cpc
	mame cpc664 \
		-window \
		-skip_gameinfo \
		-flop1 pulzar.dsk \
		-autoboot_delay 1 \
		-ab "RUN \"PULZAR.BIN\"\n"

fuse: zxs
	fuse --no-confirm-actions -g 2x pulzar.tap

clean:
	rm -f pulzar* data.h tga-dump
