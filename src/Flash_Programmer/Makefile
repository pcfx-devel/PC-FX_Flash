PREFIX         = v810
LIBERIS        = $(HOME)/devel/liberis
V810GCC        = $(HOME)/devel/pcfx/bin/v810-gcc

ASFLAGS        = -a=$*.lst
# CFLAGS        += -I$(LIBERIS)/include/ -I$(V810GCC)/include/ -I$(V810GCC)/$(PREFIX)/include/ -O2 -Wall -std=gnu99 -mv810 -msda=256 -mprolog-function
CFLAGS        += -I$(LIBERIS)/include/ -I$(V810GCC)/include/ -I$(V810GCC)/$(PREFIX)/include/ -Wall -std=gnu99 -mv810 -msda=256 -mprolog-function
CPFLAGS       += -I$(LIBERIS)/include/ -I$(V810GCC)/include/ -I$(V810GCC)/$(PREFIX)/include/ -O2 -Wall -std=gnu++11 -fno-rtti -fno-exceptions -mv810 -msda=256 -mprolog-function
LDFLAGS       += -T$(LIBERIS)/ldscripts/v810.x -L$(LIBERIS)/ -L$(V810GCC)/lib/ -L$(V810GCC)/$(PREFIX)/lib/ -L$(V810GCC)/lib/gcc/$(PREFIX)/4.7.4/ $(V810GCC)/$(PREFIX)/lib/crt0.o

LIBS           = -leris -lc -lsim -lgcc

programmer.cue: cdlink_programmer.txt programmer
	pcfx-cdlink cdlink_programmer.txt programmer

programmer: programmer.o font.o backup.o flashfuncs.o payload.o payload_name.o
	v810-ld $(LDFLAGS) programmer.o backup.o flashfuncs.o font.o payload.o payload_name.o $(LIBS) -o programmer.linked -Map programmer.map
	v810-objcopy -O binary programmer.linked programmer

payload.o: payload
	v810-objcopy -I binary -O elf32-v810 -B v810 payload payload.o

payload_name.o: payload_name
	v810-objcopy -I binary -O elf32-v810 -B v810 payload_name payload_name.o

backup.o: backup.s
	v810-as $(ASFLAGS) backup.s -o backup.o

flashfuncs.o: flashfuncs.s
	v810-as $(ASFLAGS) flashfuncs.s -o flashfuncs.o

font.o: font.s
	v810-as $(ASFLAGS) font.s -o font.o

programmer.o: programmer.source
	v810-as $(ASFLAGS) programmer.source -o programmer.o

%.o: %.s
	v810-as $(ASFLAGS) $< -o $@

programmer.source: programmer.c
	v810-gcc $(CFLAGS) programmer.c -S -o programmer.source

%.elf: $(OBJECTS)
	v810-ld $(LDFLAGS) $(OBJECTS) $(LIBS) -o $@ -Map $*.map

%.bin: %.elf
	v810-objcopy -O binary $< $@

lbas.h:
	bincat out.bin lbas.h $(BIN_TARGET) $(ADD_FILES)

clean:
	rm -rf programmer *.o *.source *.map *.lst *.linked lbas.h out.bin programmer.bin programmer.cue
