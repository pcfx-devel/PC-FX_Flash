# PC-FX_Flash

This is a design of a non-volatile backup memory board for the PC-FX. It's based on the
FX-BMP, but doesn't need batteries, and manages savegames by group, rather than by individual game.

## Design

I designed the board using the free version of EAGLE, which is limited to 2 layers and
100mm x 100mm board size.  I have included the project files as well as the gerber outputs.

There are two boards in the repository - one for the DIP version of the Flash memory, and one
for the PLCC version of the Flash memory.  In both cases, the chips need to be soldered directly
to the board, as there isn't sufficient clearance for socketing.

## Assembly

I have included gerber files, as well as BOM.csv and assembly.csv files, for use with
JLCPCB's SMD assembly service, if you are interested in taking advantage of that.

For the DIP version of the board, the assumption is that you would solder the chip yourself, although
some of the capacitors are 0402 size, which may not be easy to handle yourself...

For the PLCC version of the board, the files are also included for JLCPCB to assemble for you, but
the Flash memory devices aren't always in stock. However, as I write this, they are both available
and inexpensive at distributors like Mouser and Digikey, and this board is designed for easy handling
for somebody ready to assemble a simple surface-mount board. If this is your desire, I suggest getting
a stencil made, using solder paste made for stencil use, and heating with any of: reflow oven,
reflow hotplate, or hot air soldering station.

## Case

You can also find the design for a case/enclosure, courtesy of Jeff Chen (Twitter: @jeffqchen).
This is suitable for any 3D printer; I particularly liked the resin prints I ordered online.
The case is designed to accept M2-12 socket-head screws and nuts.

## Circuit Design Concepts

As the PC-FX uses a 5V bus, all chips need to be 5V-compliant.

In order to drive the chip-select (low), a 3-input OR gate is used, requiring the following
lines to ALL be low in order to trigger the chip select:
 1. Cartridge select (keys in on range 0xE8000000 - 0xEFFFFFFF)
 2. A25 (narrows the range to 0xE8000000 - 0xEBFFFFFF)
 3. A24 (narrows the range to 0xE8000000 - 0xE9FFFFFF)

**NOTE:**\
The address lines are numbered according to the chip's address lines, which are not
necessarily the same as the CPU's address lines. The second byte on the memory chip is
actually memory address 0xE8000002, so the memory chip's A0 is effectively the CPU's A1).

## Software

### Program Overview

The overall idea of this software is to store multiple "slots" of the entire internal savegame memory
into the Flash cart, and manage them. Since the Flash cart is 512KB and the internal savegame memory
is 32KB, this leaves room for roughly 12 slots, after deducting space for comments and the program
which manages all this.

When saving memory into a slot, you will be prompted for the date and a shhort comment (in future,
this comment may be expanded). This is to help jog your memory of when this save was made (or what
is important about the contents), for retrieval at a later date.

When examining the contents of the cart, you will be presented with the date and comment for each slot,
and will be able to list what games have saves inside of the area - but no fine-grained management
is either programmed or planned.

### Development Chain & Tools

This was written using a version of gcc for V810 processor, with 'pcfxtools' which assist in
building executables for PC-FX, and 'liberis' which is a library of functions targetting the PC-FX.

These can be found here:\
![https://github.com/jbrandwood/v810-gcc](https://github.com/jbrandwood/v810-gcc)\
![https://github.com/jbrandwood/pcfxtools](https://github.com/jbrandwood/pcfxtools)\
![https://github.com/jbrandwood/liberis](https://github.com/jbrandwood/liberis)

### Development Status
This code uses both 'C' and assembler code, and may also be used as a demonstration of how to program
using several of the facilities of the PC-FX. During the development of this management utility,
several issues have been discovered and fixed in liberis, and one compiler/linker issue remains
outstanding at this time... so this program should still be considered as 'beta'.

### Flashing the Cart

At the moment, this program is being tested via the pcfx_uploader development tool
![https://github.com/enthusi/pcfx_uploader](https://github.com/enthusi/pcfx_uploader), but
will soon be able to be programmed into the card as an auto-boot cartridge (once testing
has completed). I also plan to create utilities for programming cartridges and self-booting
images as part of this (but they don't exist as yet).

