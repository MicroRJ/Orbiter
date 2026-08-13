# Orbiter

Orbiter is an experimental NES emulator and debugger for Windows. It combines
game playback with live disassembly, CPU and PPU inspection, execution
activity, profiling, rewind, screenshots, and video capture.

This is an early prerelease. Keep a copy of any library data you care about.

## Getting started

1. Extract the complete archive to a writable folder. Do not run it directly
   from the ZIP or install it under a read-only directory.
2. Run `Orbiter.exe`.
3. Press `Ctrl+O` to import an iNES `.nes` file.
4. Press `F5` to run or pause the game. Press `Tab` to show or hide the game
   library.

Orbiter does not include game ROMs. Only use ROM images you are legally
permitted to use.

## Input

- D-pad: arrow keys or `WASD`
- A / B: `Z` / `X`
- Start / Select: `C` / `V`
- Run or pause: `F5`
- Step one instruction: `F10`
- Rewind / advance timeline: `Ctrl+Left` / `Ctrl+Right`
- Save / restore the active resume point: `Ctrl+S` / `Ctrl+L`
- Toggle library: `Tab`
- Import ROM: `Ctrl+O`
- Toggle fullscreen: `F11` or `Alt+Enter`
- Mute: `M`

The active game is also saved when switching games and when Orbiter exits
normally.

## Compatibility

- Windows 8 or newer, x64
- iNES cartridges using mapper 0 (NROM), 1 (MMC1), 2 (UxROM), or 9 (MMC2)
- Trainers and four-screen mirroring are not currently supported

Unsupported or malformed cartridges are rejected rather than partially
loaded. Emulator compatibility is still a work in progress.

## Local data

Orbiter stores its library, saves, configuration, and logs in the `data`
directory beside the executable. The distributed archive contains none of
those user files. The prerelease executable is unsigned, so Windows may show
a security warning on first launch.

Orbiter is licensed under the MIT License. See `THIRD_PARTY_NOTICES.md` for
the software and assets it uses.
