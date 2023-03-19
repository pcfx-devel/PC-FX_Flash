# Flash_Programmer

This program will store data on a FX-Flash memory card.

On screen, the name of the code to be deployed will appear, along with some information about its size.

Two options appear below:
 - ERASE -> Erase enough of the start of Flash to deploy the data
 - WRITE -> Write the data.  Note that ERASE must take place prior to WRITE.

Currently, this code is set up to deploy the FX Megavault memory manager.

## Writing Other Data

In order to deploy arbitrary binary data, this program can be compiled to include whatever
is in the "payload" file at compile time - whether data, a program, or a FX-BMP bootable image.

The "payload_name" file should also be updated with a brief note of what the payload file
contains (up to 31 characters), to be displayed to the user at the time of flash programming.

This means that "programmer" must be recompiled whenever you wish to use new data for
programming into FX-Flash cards - but, as long as you have a PC-FX compiler environment, this
should be very straightforward: just rename your data as "payload", update the "payload_name" file,
and run:
```
make clean
make
```

## Release

The payload file currently contains a bootable image of the FX Megavault Backup Manager.

The files in the "Release" folder are as follows:
 - programmer_fxuploader_20230318 -> to be used with fxuploader to run in-memory
 - programmer.cue / programmer.bin -> CDROM image for programming a FX-Flash card with the Megavault software

