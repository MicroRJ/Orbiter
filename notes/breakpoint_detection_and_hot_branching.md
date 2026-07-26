
Breakpoint detection is easy because it's simple, but it's hard
because it's stupid simple.

We need to detect breaks on execution / reads / writes.

The idea is:

	We create a map over the entire state. What we do is this:

	For every read / write / exec, check that map as well, if
	a trap is set, set a signal and remember the current
	scheduler cycle.

	The nice thing is that it would work symmetrically for literally
	everything ... because everything lives in the same memory block.

	Problem is, every freaking byte is more than twice as expensive.

	So, what else?

	I think the only real 'solution' is to just compile the emulator
	twice, it's small, it won't matter, once for trap mode, once for
	release mode.

Guess on hot branching:

Another thing that perhaps isn't helping is that in the very beginning
of time, I chose to do this:

	cpu_mem(access)
	{
		if (access.mode == MAP) {
		}
		else if (access.mode == WRITE) {
		}
		else {
		}
	}

As supposed to this:
	cpu_mem_map(access)
	{
	}

	cpu_mem_read(access)
	{
	}

	cpu_mem_write(access)
	{
	}

Every single byte is several function calls, multiple branches,
additional returns, additional arguments ...

The intriguing question is, how much is this adding?

The reason for having one read/write/map function is so that I can
reuse the same routing code.

But I think we can do something about this that might also not require
doing much ...

So we keep the generic access thing, but we keep it private.

However, we only expose the 3 read / write / map prongs.

Then we wire the read / write / map ports to the shared access port.

Then we force the compiler to inline it.

Essentially, the compiler gets rid of the branching, and the additional
stuff for read / write.

A) Has the compiler already done this? B) is this insignificant? C) or all of the above?


For 1,000 Frames:
Zelda: 28,015 CPU reads + 373 writes/frame; 103,914 PPU reads/frame; 3.343 ms median.
Pac-Man: 24,600 CPU reads + 116 writes/frame; 95,348 PPU reads/frame; 2.908 ms median.
Dragon Quest: 22,802 CPU reads + 1,650 writes/frame; 95,412 PPU reads/frame; 2.844 ms median.
Effective throughput is roughly:
CPU bus: 8.5–8.6 million accesses/sec
PPU bus: 31–33 million accesses/sec

Mappers:
Zelda: 27,222 CPU reads and 42,709 PPU reads - about 53% of all bus accesses.
Pac-Man: 19,552 CPU reads and 33,968 PPU reads - about 45%.
Dragon Quest: 18,451 CPU reads and 33,971 PPU reads - about 44%.


Come on, what are your guesses people? Will we go down?

