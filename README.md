# GameboyEmulator

A lightweight 8-bit Game Boy hardware emulator written in C++ using SDL2. Designed to accurately emulate the Sharp LR35902 CPU architecture, memory mapping, background/sprite graphics rendering, and real-time input handling.


## 🎮 How to Play

To launch a game, compile the source code and run the executable with your Game Boy ROM file (e.g., `tetris.gb`) as a command-line argument:

```bash
./GameboyEmulator tetris.gb

Controls
 * D-Pad (Up, Down, Left, Right): Arrow Keys
 * Button A: Z
 * Button B: X
 * Start: Enter
 * Select: Right Shift

<img width="463" height="445" alt="Screenshot 2026-09-29 030230" src="https://github.com/user-attachments/assets/e70f14ce-90ee-4e9f-8982-8f263b03a712" />

Figure 1: GameboyEmulator executing tetris.gb with real-time SDL2 frame rendering.
✨ Features
 * LR35902 CPU Core: Accurate emulation of registers, flags, and the core instruction set.
 * PPU & Video Rendering: Background layer tilemaps, window rendering, and sprite handling.
 * Input System: Responsive controls mapped to standard keyboard inputs via SDL2.
 * Memory & ROM Support: ROM loading and basic memory bank management.
🛠️ Build & Setup
Requirements
 * C/C++ Compiler (gcc or g++)
 * SDL2 Library (included under ./include and ./lib)
Building from Source
Windows (MinGW / PowerShell):
gcc main.c -I./include -L./lib -lSDL2main -lSDL2 -o GameboyEmulator.exe

Linux / macOS:
gcc main.c -o GameboyEmulator -lSDL2

📁 Project Structure
GameboyEmulator/
├── include/           # Header files and SDL2 dependencies
├── lib/               # Static libraries (.a, .la)
├── main.c             # Core emulator implementation
├── tetris.gb          # Test ROM
├── .gitignore         # Ignores large trace logs and build binaries
└── README.md          # Project documentation

📜 License
This project is open-source and available under the MIT License.
