PREFIX         = v810
LIBERIS        = $(HOME)/devel/liberis
V810GCC        = $(HOME)/devel/pcfx/bin/v810-gcc

ASFLAGS        = -a=$*.lst
# CFLAGS        += -I$(LIBERIS)/include/ -I$(V810GCC)/include/ -I$(V810GCC)/$(PREFIX)/include/ -O2 -Wall -std=gnu99 -mv810 -msda=256 -mprolog-function
CFLAGS        += -I$(LIBERIS)/include/ -I$(V810GCC)/include/ -I$(V810GCC)/$(PREFIX)/include/ -Wall -std=gnu99 -mv810 -msda=256 -mprolog-function
CPFLAGS       += -I$(LIBERIS)/include/ -I$(V810GCC)/include/ -I$(V810GCC)/$(PREFIX)/include/ -O2 -Wall -std=gnu++11 -fno-rtti -fno-exceptions -mv810 -msda=256 -mprolog-function
LDFLAGS       += -T$(LIBERIS)/ldscripts/v810.x -L$(LIBERIS)/ -L$(V810GCC)/lib/ -L$(V810GCC)/$(PREFIX)/lib/ -L$(V810GCC)/lib/gcc/$(PREFIX)/4.7.4/ $(V810GCC)/$(PREFIX)/lib/crt0.o

LIBS           = -leris -lc -lsim -lgcc

bank.cue: cdlink_bank.txt bank
	pcfx-cdlink cdlink_bank.txt bank

bank.flashboot: bank
	python3 mkflashboot.py bank

bank: bank.o font.o backup.o flashfuncs.o
	v810-ld $(LDFLAGS) bank.o backup.o flashfuncs.o font.o $(LIBS) --sort-common=descending -o bank.linked -Map bank.map
	v810-objcopy -O binary bank.linked bank

backup.o: backup.s
	v810-as $(ASFLAGS) backup.s -o backup.o

flashfuncs.o: flashfuncs.s
	v810-as $(ASFLAGS) flashfuncs.s -o flashfuncs.o

font.o: font.s
	v810-as $(ASFLAGS) font.s -o font.o

bank.o: bank.source
	v810-as $(ASFLAGS) bank.source -o bank.o

%.o: %.s
	v810-as $(ASFLAGS) $< -o $@

bank.source: bank.c
	v810-gcc $(CFLAGS) bank.c -S -o bank.source

%.elf: $(OBJECTS)
	v810-ld $(LDFLAGS) $(OBJECTS) $(LIBS) -o $@ -Map $*.map

%.bin: %.elf
	v810-objcopy -O binary $< $@

lbas.h:
	bincat out.bin lbas.h $(BIN_TARGET) $(ADD_FILES)

clean:
	rm -rf bank *.o *.source *.map *.lst *.linked lbas.h out.bin bank.bin bank.cue
