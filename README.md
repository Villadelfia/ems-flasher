The EMS flasher is a simple command line flasher for the 64 Mbit EMS USB
flash cart for Game Boy.

This software was written by Mike Ryan <mikeryan@lacklustre.net> and modified by
Randy Thiemann <uselinuxnow@gmail.com>.

I added some filesize checks to ensure that you don't destroy your cartridge by
accidentally writing a huge file.

For more information, see the web site at:
http://lacklustre.net/gb/ems/

# BUILDING

    $ make

On Mac OS X, prior to building you must install pkgconfig and libusb:

    $ brew install pkgconfig
    $ brew install libusb

# RUNNING

The software has three major modes of operation:

* Write file to cart
* Read file from cart
* Read title of ROM on cart

Pass the --help flag to see all the options.

# EXAMPLES

## Rom
### Write the ROM to the cart in bank 1.
    $ ./ems-flasher --write rom.gb

### Write the ROM to the cart in bank 2.
    $ ./ems-flasher --bank 2 --write rom.gb

### Read the ROM in bank 1 to file.
    $ ./ems-flasher --read rom.gb

### Read the ROM in bank 2 to file.
    $ ./ems-flasher --bank 2 --read rom.gb

## Save
### Write the SAVE to the cart.
    $ ./ems-flasher --write save.sav

### Read the SAVE to file.
    $ ./ems-flasher --read save.sav

## Miscellaneous
### Print out the titles of both roms.
    $ ./ems-flasher --title

Note that you can force the target location by passing --rom or --save, 
otherwise the program will automatically read or write from sram if the filename
ends in .sav.
