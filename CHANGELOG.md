[CHANGED]
	The emulator module no longer dictates serialization nor cartridge loading.
	It is now a pure executioner.
	It has a single setup procedure, that merely all it does is copy memory and reset its devices.

	Orb is now the unified representation for both, disk saves and the runtime model.
	Each orb contains a game and its saves along with metadata.
	iNES files are converted into orbs, and saved to disk as orbs.

	The debugger is no longer a complete proxy for the emulator, it now only handles the debug execution path,
	introspection and unwind semantics.

	The emulator no longer stores nor manages a circular trace buffer.
	The entire bookkeeping system is gone now.
	Now the debugger simply passes a trace buffer to the run function, the emulator fills it with trace entries.
	Entries no longer store the scheduler step, instead each trace corresponds to one scheduler step.
	Traces are now unpacked, since packing didn't seem to make much of a difference last time, thought we could
	revisit later.

[TODO]
-- UPCOMING REFACTOR OF DOOM --

A) the emulator itself doesn't get to pick what it serializes or not, that's the job of the
thing that manages saves.
	At the moment the emulator module itself determines what a save is and what it should serialize,
	clearly this isn't its job, because it simply doesn't have enough context to know what matters.
	The meta-descriptors and serialization should belong under orb, which is the module responsible
	for saving / restoring state.
	It knows what it wants from the emulator.
	Eventually if we every support other systems we'd simply have different targets.

B) the emulator doesn't need different entry points for loading state, there's only one entry
point needed.
	At the moment the emulator has a few boot entry points, loading from a cartridge, loading from
	a serial save, loading from a direct save. This is simply ridiculous.
	There should be one configuration entry point that doesn't care about whether it came from a
	cartridge, a saved state or a copy.
	And an additional function for reseting the emulator. Reset happens once a cartridge is loaded.

	That the emulator copies everything into its internal block is a bit of a weird thing now, but
	that's not something that affects the grand architecture. And it may prove useful later.

C) ines file format should not even exist outside of orb.
	At the moment the ines cartridge descriptor acts as the intermediary between nes and orbs.
	Instead, the ines decoder should be used as a temporary importer, ines -> orb.

	There should be one internal representation of what an executable / state is and management
	of that is driven by the orb system.

D) There's no need for a separate emulator and machine state, serialization and snapshotting already
	mixes the both and selectively filters what it wants. Keeping them separate was never really useful.


Plan of action here:
	Start with the easy mechanical stuff.

	-) Remove the machine state and just put everything under NES_Emulator.
		This will immediately cleanup and simplify bunch of stuff in the nes module.

	-) The meta descriptors will also have be be updated and put under 'orb' since that's
		the only thing that cares and knows about selective state serialization.

	-) Remove ines entirely from 'nes\' and put under 'orb\' and have a singular entry point for
		booting up the emulator which takes an emulator config.

	-) The orb module will automatically detect whether the file is an 'ines' file and convert it
		to a runtime orb.
		Orbs act as the single representation of game state / save for the entire application & library.



In summary:
	ORB takes care of everything to do with managing saves / state.
	NES only runs the emulator.
	DBG only handles execution, breakpoints and unwind semantics.


END
---


---
We still have this proxy model:

debugger -> emulator
		   -> publisher -> emulator

But it isn't really the right model.

What we need is something more straightforward.

We just need a process that runs the emulator and handles breakpoints in realtime.

Real-time running works on a separate thread

The emulation thread handles:
	Running the emulator with breakpoints / unwind semantics / captures and send
	updates back to the frontend.

	Whenever the app actually needs to talk to the emulator.

	We stop the thread and do whatever it is we want.









- Debugger no longer tracks per cartridge data in snapshots, only live runtime state.
- Audio is no longer cleared to zero when running muted, instead we run at regular CPU cycles per frame,
though we should have a dedicated mode that runs one entire PPU frame every frame.

- Removed weird nes_emulator_run function with would take vestigial ppu_cycles, now we just call
step directly.

- Breakpoint restoration and rewind now use the same snapshot ring buffer. Snapshots are no longer
created per cpu step on F10 singular step mode, instead the latest snapshot is unwound and the
debugger runs to the previous scheduler cycle.



