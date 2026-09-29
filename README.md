# GameboyEmulator

A lightweight 8-bit Game Boy hardware emulator written in C/C++ using SDL2. Designed to accurately emulate the Sharp LR35902 CPU architecture, memory mapping, background and sprite graphics rendering, and real time input handling.

---


<img width="463" height="445" alt="Screenshot 2026-09-29 030230" src="https://github.com/user-attachments/assets/c2fb6bf8-6575-4943-b83f-810192822fad" />

---



To launch a game, compile the source code and run the executable with your Game Boy ROM file (e.g., `tetris.gb`) as a command-line argument:

```bash
./GameboyEmulator tetris.gb

Key Controls
 * D-Pad (Up, Down, Left, Right): Arrow Keys
 * Button A: Z
 * Button B: X
 * Start: Enter
 * Select: Right Shift
✨ Features
 * LR35902 CPU Core: Accurate emulation of registers, flags, and core instruction execution loops.
 * PPU & Video Rendering: Support for background layer tilemaps, window rendering, and sprite composition using SDL2.
 * Input System: Responsive keyboard mappings for standard Game Boy hardware buttons.
 * Memory Management: Efficient ROM loading and memory-mapped I/O handling.
🛠️ Build & Setup
Prerequisites
 * C/C++ Compiler (gcc or g++)
 * SDL2 Development Library (configured under ./include and ./lib)
Building from Source
Windows (MinGW / PowerShell):
gcc main.c -I./include -L./lib -lSDL2main -lSDL2 -o GameboyEmulator.exe

Linux / macOS:
gcc main.c -o GameboyEmulator -lSDL2

📁 Project Structure
GameboyEmulator/
├── include/           # Header files and SDL2 dependency headers
├── lib/               # Static libraries (.a, .la)
├── main.c             # Core emulator implementation
├── tetris.gb          # Test ROM file
├── .gitignore         # Configured to exclude build binaries and log traces
└── README.md          # Project documentation

📜 License
This project is open-source and available under the MIT License.

---
