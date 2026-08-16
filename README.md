# Orbiter v0.1

Orbiter is an immersive NES emulator, introspector, and debugger for Windows,
focused on providing a bad-ass debugging experience.

We don't just want to debug the machine. We want to see it breathe, hear its
heartbeat, and feel its tender pulses.

![Orbiter](gifs/orbiter_capture_004.gif)

The emulator and debugger grew together, so introspection is not an attachment:
it is the point. Orbiter combines game playback with live disassembly, CPU and
PPU inspection, execution-flow visualization, profiling, rewind, screenshots,
video capture, and a persistent game library.

This is an early prerelease. Emulator compatibility is still a work in progress,
and you should keep a copy of any library data you care about.

## Getting started

1. Extract the complete archive to a writable folder. Do not run Orbiter
   directly from the ZIP or install it under a read-only directory.
2. Run `Orbiter.exe`.
3. Press `Ctrl+O` to import an iNES `.nes` file.
4. Press `F5` to run or pause the game.
5. Press `Tab` to open or close the game library.

Orbiter does not include game ROMs. Only use ROM images you are legally
permitted to use.

## Key bindings

### Controller

| Input | Key |
| --- | --- |
| D-pad | Arrow keys or `WASD` |
| A | `Z` |
| B | `X` |
| Start | `C` |
| Select | `V` |

### Emulation and debugging

| Action | Key |
| --- | --- |
| Run or pause | `F5` |
| Step one instruction | `F10` (hold to repeat) |
| Rewind / advance timeline | `Ctrl+Left` / `Ctrl+Right` |
| Reset | `Ctrl+R` |
| Save / restore the active resume point | `Ctrl+S` / `Ctrl+L` |
| Dump the current program | `Ctrl+K` |

### Views and panels

| Action | Key |
| --- | --- |
| Toggle game library | `Tab` |
| Import ROM | `Ctrl+O` |
| Video / Program / CPU | `1` / `2` / `3` |
| Profiler / Execution Flow / CHR Map | `4` / `5` / `6` |
| Split the focused panel | `Ctrl+H` / `Ctrl+V` |
| Close the focused panel | `Ctrl+Q` |
| Increase / decrease UI font size | `Ctrl++` / `Ctrl+-` |
| Reset UI font size | `Ctrl+0` |

### Display, audio, and capture

| Action | Key |
| --- | --- |
| Toggle fullscreen | `F11` or `Alt+Enter` |
| Toggle game-view fullscreen | `F` |
| Leave game-view fullscreen | `Esc` |
| Toggle CRT effect | `F7` |
| Mute | `M` |
| Raise / lower volume | `Ctrl+Up` / `Ctrl+Down` |
| Take game screenshot | `F8` |
| Start / stop game GIF capture | `Shift+F8` |
| Take application screenshot | `F9` |
| Toggle UI debug bounds | `F6` |

Screenshots and game captures are written to the `data` directory. The active game
is also saved when switching games and when Orbiter exits normally.

## Compatibility

- Windows 8 or newer, x64
- iNES cartridges using mapper 0 (NROM), 1 (MMC1), 2 (UxROM), or 9 (MMC2)
- Trainers and four-screen mirroring are not currently supported

Unsupported or malformed cartridges are rejected rather than partially loaded.

## Building

Orbiter currently builds with Clang on Windows. From an x64 Developer Command
Prompt, run:

```bat
build.bat
```

## Local data

Orbiter stores its library, saves, configuration, logs, screenshots, and
captures in the `data` directory beside the executable. The distributed archive
contains none of those user files. The prerelease executable is unsigned, so
Windows may show a security warning on first launch.

Orbiter is licensed under the MIT License. See `THIRD_PARTY_NOTICES.md` for the
software and assets it uses.
