[TODO]

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



