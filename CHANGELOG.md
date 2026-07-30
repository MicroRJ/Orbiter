
- Debugger no longer tracks per cartridge data in snapshots, only live runtime state.
- Audio is no longer cleared to zero when running muted, instead we run at regular CPU cycles per frame,
though we should have a dedicated mode that runs one entire PPU frame every frame.

- Removed weird nes_emulator_run function with would take vestigial ppu_cycles, now we just call
step directly.

- Breakpoint restoration and rewind now use the same snapshot ring buffer. Snapshots are no longer
created per cpu step on F10 singular step mode, instead the latest snapshot is unwound and the
debugger runs to the previous scheduler cycle.



