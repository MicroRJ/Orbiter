## Old Personal Notes

These are old personal notes and comments I've extracted from the old code-base.
I transfered them here because they serve as a bit of as historical link between
this squeaky clean new repo and the private one.

They may or may not be accurate.
---

I never had NES, never programmed one, and probably never will, at least not directly,
so this is here for my own reference.

Also this particular emulator, is my first emulator ever... So this this has been a
journey experience...

https://www.nesdev.org/wiki/Init_code
http://www.6502.org/tutorials/6502opcodes.html
http://wilsonminesco.com

PPU Pattern Tables:
https://www.nesdev.org/wiki/PPU_pattern_tables

Characters or patterns:
These are 8x8 bit-maps, literally.
Each image is stored in two adjacent planes.
Each plane is 8 bytes, each byte contains 8 pixels at one bit per pixel.
The two planes are merged to create a palette index.

Nametables:
https://www.nesdev.org/wiki/PPU_nametables

A nametable is essentially a game map or level.
Each entry or cell in the table is a byte index into a character in the character
table (depending on the character base address).
A nametable has 30 rows of 32 tiles and 96 additional bytes for its attribute table,
making the effective size 1024 bytes.
Since each tile is 8x8, a nametable is one full screen or 256x240 pixels.

The NES system has 4 logical name tables but the chip only has enough VRAM (2KiB) for 2
physical nametables.

PPU Attribute Tables:
https://www.nesdev.org/wiki/PPU_attribute_tables

Attribute tables assign a palette to the tile region of its nametable.
Attribute tables are 64 bytes long, thus each byte corresponds to a 4x4 tile region in
the nametable and each pair of bits corresponds a 2x2 tile subregion.

Memory Mirroring:
https://www.nesdev.org/wiki/Mirroring

The "picture region" is 256x240 pixels with the "border region" extending 16 pixels left and 11
right and 2 down for 283x242 pixels in total.

The PPU performs 262 scanlines per frame, each scanline is 341 dots.

The term 'dot' refers to a PPU clock cycle, not every dot produces a visible pixel. Some
dots are used to prepare the subsequent scanline data.

THE VISIBLE SCANLINES (0-239)
These are the scanlines that actually produce pixels.
During, the PPU is busy fetching data, so the program should not access PPU memory, unless rendering is
turned off.

This is how the dots on these scanlines are used up:
+ DOT 0: Idle.
+ DOTS 1-256: This is the VISIBLE portion of the
scanline, e.i the portion used to draw pixels
on the screen. We fetch background and sprite
data and render pixels on the screen, one pixel
per dot.
There are 4 memory fetches per tile:
	- Nametable byte
	- Attribute table byte
	- Pattern table low
	- Pattern table high
Each fetch takes two PPU cycles to complete and thus
each tile takes 8 PPU cycles, or dots.
For this reason we can only afford one palette
attribute every 8 pixels.
In other words, this is the reason tiles are 8x8 pixels
and we can only switch the palette for a tile and not
individual pixels.
** Note that at the beginning of each scanline, the data
for the for the first two tiles is already loaded and
ready to be rendered.

Palettes:
	NES PPU has 28 total internal palette entries, each is 6 bits (up to 64 states each).
	And they are mapped from $3F00 to $3FFF. Only the low 5 bits are relevant for selecting
	one of the internal palette entries (0 - 31). These 32 logical addresses, are each
	mapped to a separate internal entry.
	$00, $04, $08  $0C
	$10, $14, $18, $1C, which map to the same internal entries as:

### Addressing Modes
I've always been perplexed by the term addressing mode, now that I think about it,
the addressing mode really just determines how the operand is retrieved, or addressed?
So if each instruction has a value, then the addressing mode is the manner by which
the value is retrieved...

For instance the operand may be within the instruction itself, so this is called
immediate addressing, because the manner in which you get the operand is immediate!
Absolute addressing, meaning the operand resides in the memory address specified,
and so on...

### Address Bus

For the electronics ignorants such as my self:
So chips like the CPU and PPU have what's called an address bus, for the CPU is 16 bits,
or 16 pins or lanes, each bit or lane corresponds to a physical pin or address line, A0 - A15.
It also has an 8 bit data bus.
Every cycle, the CPU will populate the address bus with the desired address, and it will populate
or read in data from the data bus depending on the mode.

Additional circuitry wired directly to some of the pins
on the address bus figures out which other devices to
enable, i.e how to map that particular combination of
address lines to some form of functionality, this technique is
called memory mapped IO.

* The CPU uses an additional pin to signal whether this is a read
or write operation.

Notes on mappers and cartridges and abstractions used for this emulator.
-----------------------------------------------
First of all, what even is a mapper?

A mapper is just another chip, with the primary function of mapping some address to cartridge
memory or some other form of functionality, ultimately it acts as a bridge between the
system and the cartridge.

So when the NES system request an address or writes to an address it goes through the
mapper.
The mapper then can do whatever it wants.

Not all cartridges have a mapper, but we can think of all cartridges as having a mapper.
Because really, a cartridge with no mapper chip still does some mapping.
