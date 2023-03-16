#(c) 2023 David Shadoff
import sys

trailflash = False

if (len(sys.argv) == 3):
    if (sys.argv[1] == '-mednafen'):
        trailflash = True
        filename = sys.argv[2]
    elif (sys.argv[2] == '-mednafen'):
        trailflash = True
        filename = sys.argv[1]
    else:
        print("Usage:")
        print("   mkflashboot.py [-medanfen] <filename>")
        print(" OR ")
        print("   mkflashboot.py <filename> [-medanfen]")
        exit
else:
    filename = sys.argv[1]


program = open(filename,'rb').read()

program = open(filename,'rb').read()
length = len(program)

rest1 = 4096 - 64
rest2 = (128 * 1024) - 4096 - length

#print("length = ", length)
#print("rest1 = ", length)
#print("rest2 = ", length)

outfile = filename + '.bootflash'
f = open(outfile, 'wb')

# Original boot information - identifying it as a FX-BMP type card:
#
d_bootseq1 = [0x24, 0x8A, 0xDF]
f.write(bytearray(d_bootseq1))

f.write(bytes('PCFXCard','UTF-8'))

d_bootseq2 = [0x80, 0x00, 0x01, 0x01, 0x00]
f.write(bytearray(d_bootseq2))
d_bootseq3 = [0x01, 0x40, 0x00, 0x00, 0x01, 0xf9, 0x03, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00]
f.write(bytearray(d_bootseq3))
d_bootseq4 = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
f.write(bytearray(d_bootseq4))

# Now, for Bootable section:
#
# Offset 0x28 Boot sequence:
f.write(bytes('PCFXBoot','UTF-8'))

# Backup memory source offset:
f.write(0x1000.to_bytes(4,'little'))

# RAM Destination:
f.write(0x8000.to_bytes(4,'little'))

# Transfer size (bytes):
f.write(length.to_bytes(4,'little'))
 
# Transfer address:
f.write(0x8000.to_bytes(4,'little'))

# fill the boot sector of the flash (4096 bytes)
#
filler=[0x00]
for fill in range(rest1):
    databytes=bytearray(filler)
    f.write(databytes)

# Now write the program itself
#
f.write(program)

# Now fill out the remainder of the FXBMP file size to satisfy Mednafen
# (if required)
#
if (trailflash == True):
    for fill in range(rest2):
        databytes=bytearray(filler)
        f.write(databytes)     # fill the boot sector of the flash (4096 bytes)

f.close()

