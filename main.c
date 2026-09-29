#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define SDL_MAIN_HANDLED
#include "include/SDL2/SDL.h"

/* ============================================================
   DISPLAY
   ============================================================ */

#define SCREEN_W 160
#define SCREEN_H 144

#define WINDOW_SCALE 4

#define CPU_HZ       4194304
#define FRAME_CYCLES 70224

/* ============================================================
   REGISTERS
   ============================================================ */

#define JOYP  0xFF00
#define SB    0xFF01
#define SC    0xFF02

#define DIV   0xFF04
#define TIMA  0xFF05
#define TMA   0xFF06
#define TAC   0xFF07

#define IFREG 0xFF0F

#define LCDC  0xFF40
#define STAT  0xFF41
#define SCY   0xFF42
#define SCX   0xFF43
#define LY    0xFF44
#define LYC   0xFF45

#define DMA   0xFF46

#define BGP   0xFF47
#define OBP0  0xFF48
#define OBP1  0xFF49

#define WY    0xFF4A
#define WX    0xFF4B

#define IE    0xFFFF

/* ============================================================
   FLAGS
   ============================================================ */

#define FLAG_Z 0x80
#define FLAG_N 0x40
#define FLAG_H 0x20
#define FLAG_C 0x10

/* ============================================================
   CPU
   ============================================================ */

typedef struct
{
    uint8_t A;
    uint8_t F;

    uint8_t B;
    uint8_t C;

    uint8_t D;
    uint8_t E;

    uint8_t H;
    uint8_t L;

    uint16_t SP;
    uint16_t PC;

    int IME;
    int halted;
    int stopped;

    int ime_delay;

} CPU;

static CPU cpu;

/* ============================================================
   MEMORY
   ============================================================ */

static uint8_t mem[0x10000];

static uint8_t *rom = NULL;
static size_t rom_size = 0;

static uint8_t eram[0x8000];

static int mbc_type = 0;

static int rom_bank = 1;
static int ram_bank = 0;

static int ram_enabled = 0;
static int banking_mode = 0;

/* ============================================================
   JOYPAD
   ============================================================ */

typedef struct
{
    int A;
    int B;
    int SELECT;
    int START;

    int RIGHT;
    int LEFT;
    int UP;
    int DOWN;

} Joypad;

static Joypad joypad =
{
    1, 1, 1, 1,
    1, 1, 1, 1
};

/* ============================================================
   VIDEO
   ============================================================ */

/*
    Game Boy style green palette.

    This avoids the plain black/white appearance.
*/

static const uint32_t gb_colors[4] =
{
    0xFFE0F8D0,
    0xFF88C070,
    0xFF346856,
    0xFF081820
};

static uint32_t framebuffer[
    SCREEN_W * SCREEN_H
];

/* ============================================================
   TIMING
   ============================================================ */

static int div_counter = 0;
static int timer_counter = 0;
static int ppu_counter = 0;

/* ============================================================
   WINDOW STATE
   ============================================================ */

static int window_line_counter = 0;

/* ============================================================
   REGISTER HELPERS
   ============================================================ */

static uint16_t BC(void)
{
    return (uint16_t)(
        ((uint16_t)cpu.B << 8) |
        cpu.C
    );
}

static uint16_t DE(void)
{
    return (uint16_t)(
        ((uint16_t)cpu.D << 8) |
        cpu.E
    );
}

static uint16_t HL(void)
{
    return (uint16_t)(
        ((uint16_t)cpu.H << 8) |
        cpu.L
    );
}

static uint16_t AF(void)
{
    return (uint16_t)(
        ((uint16_t)cpu.A << 8) |
        (cpu.F & 0xF0)
    );
}

static void set_BC(uint16_t v)
{
    cpu.B = (uint8_t)(v >> 8);
    cpu.C = (uint8_t)v;
}

static void set_DE(uint16_t v)
{
    cpu.D = (uint8_t)(v >> 8);
    cpu.E = (uint8_t)v;
}

static void set_HL(uint16_t v)
{
    cpu.H = (uint8_t)(v >> 8);
    cpu.L = (uint8_t)v;
}

static void set_AF(uint16_t v)
{
    cpu.A = (uint8_t)(v >> 8);
    cpu.F = (uint8_t)(v & 0xF0);
}

/* ============================================================
   MEMORY READ
   ============================================================ */

static uint8_t memory_read(uint16_t address)
{
    /* ROM BANK 0 */

    if (address < 0x4000)
    {
        if (rom != NULL &&
            address < rom_size)
        {
            return rom[address];
        }

        return 0xFF;
    }

    /* SWITCHABLE ROM */

    if (address < 0x8000)
    {
        if (rom == NULL ||
            rom_size == 0)
        {
            return 0xFF;
        }

        int bank = rom_bank;

        if (bank == 0)
            bank = 1;

        size_t index =
            (size_t)bank * 0x4000 +
            (address - 0x4000);

        if (index >= rom_size)
        {
            size_t banks =
                rom_size / 0x4000;

            if (banks > 1)
            {
                bank %= (int)banks;

                if (bank == 0)
                    bank = 1;

                index =
                    (size_t)bank * 0x4000 +
                    (address - 0x4000);
            }
        }

        if (index < rom_size)
            return rom[index];

        return 0xFF;
    }

    /* VRAM */

    if (address >= 0x8000 &&
        address < 0xA000)
    {
        return mem[address];
    }

    /* EXTERNAL RAM */

    if (address >= 0xA000 &&
        address < 0xC000)
    {
        if (!ram_enabled)
            return 0xFF;

        size_t index =
            (size_t)ram_bank * 0x2000 +
            (address - 0xA000);

        if (index < sizeof(eram))
            return eram[index];

        return 0xFF;
    }

    /* WRAM */

    if (address >= 0xC000 &&
        address < 0xE000)
    {
        return mem[address];
    }

    /* ECHO RAM */

    if (address >= 0xE000 &&
        address < 0xFE00)
    {
        return mem[address - 0x2000];
    }

    /* OAM */

    if (address >= 0xFE00 &&
        address < 0xFEA0)
    {
        return mem[address];
    }

    /* UNUSED */

    if (address >= 0xFEA0 &&
        address < 0xFF00)
    {
        return 0xFF;
    }

    /* JOYPAD */

    if (address == JOYP)
    {
        uint8_t select =
            mem[JOYP] & 0x30;

        uint8_t result = 0x0F;

        /*
            P14 = direction buttons
            P15 = A/B/Select/Start
        */

        if (!(select & 0x10))
        {
            if (!joypad.RIGHT)
                result &= (uint8_t)~0x01;

            if (!joypad.LEFT)
                result &= (uint8_t)~0x02;

            if (!joypad.UP)
                result &= (uint8_t)~0x04;

            if (!joypad.DOWN)
                result &= (uint8_t)~0x08;
        }

        if (!(select & 0x20))
        {
            if (!joypad.A)
                result &= (uint8_t)~0x01;

            if (!joypad.B)
                result &= (uint8_t)~0x02;

            if (!joypad.SELECT)
                result &= (uint8_t)~0x04;

            if (!joypad.START)
                result &= (uint8_t)~0x08;
        }

        return (uint8_t)(
            0xC0 |
            select |
            result
        );
    }

    return mem[address];
}

/* ============================================================
   MEMORY WRITE
   ============================================================ */

static void memory_write(
    uint16_t address,
    uint8_t value)
{
    /* RAM ENABLE */

    if (address < 0x2000)
    {
        if (mbc_type == 1)
        {
            ram_enabled =
                ((value & 0x0F) == 0x0A);
        }

        return;
    }

    /* ROM BANK */

    if (address >= 0x2000 &&
        address < 0x4000)
    {
        if (mbc_type == 1)
        {
            int bank =
                value & 0x1F;

            if (bank == 0)
                bank = 1;

            rom_bank =
                (rom_bank & 0x60) |
                bank;
        }

        return;
    }

    /* RAM BANK / UPPER ROM */

    if (address >= 0x4000 &&
        address < 0x6000)
    {
        if (mbc_type == 1)
        {
            if (banking_mode == 0)
            {
                rom_bank =
                    (rom_bank & 0x1F) |
                    ((value & 0x03) << 5);
            }
            else
            {
                ram_bank =
                    value & 0x03;
            }
        }

        return;
    }

    /* BANKING MODE */

    if (address >= 0x6000 &&
        address < 0x8000)
    {
        if (mbc_type == 1)
            banking_mode = value & 1;

        return;
    }

    /* VRAM */

    if (address >= 0x8000 &&
        address < 0xA000)
    {
        mem[address] = value;
        return;
    }

    /* EXTERNAL RAM */

    if (address >= 0xA000 &&
        address < 0xC000)
    {
        if (ram_enabled)
        {
            size_t index =
                (size_t)ram_bank * 0x2000 +
                (address - 0xA000);

            if (index < sizeof(eram))
                eram[index] = value;
        }

        return;
    }

    /* WRAM */

    if (address >= 0xC000 &&
        address < 0xE000)
    {
        mem[address] = value;
        return;
    }

    /* ECHO */

    if (address >= 0xE000 &&
        address < 0xFE00)
    {
        mem[address] = value;
        mem[address - 0x2000] = value;
        return;
    }

    /* OAM */

    if (address >= 0xFE00 &&
        address < 0xFEA0)
    {
        mem[address] = value;
        return;
    }

    /* UNUSED */

    if (address >= 0xFEA0 &&
        address < 0xFF00)
        return;

    /* JOYP */

    if (address == JOYP)
    {
        uint8_t old_value =
            mem[JOYP];

        mem[JOYP] =
            (uint8_t)(
                0xC0 |
                (value & 0x30)
            );

        /*
            If the selected line changes to low,
            request joypad interrupt.
        */

        uint8_t old_lines =
            old_value & 0x0F;

        uint8_t new_lines =
            memory_read(JOYP) & 0x0F;

        if ((old_lines & ~new_lines) != 0)
            mem[IFREG] |= 0x10;

        return;
    }

    /* DIV */

    if (address == DIV)
    {
        mem[DIV] = 0;
        div_counter = 0;
        return;
    }

    /* TAC */

    if (address == TAC)
    {
        mem[TAC] =
            (uint8_t)(value & 0x07);
        return;
    }

    /* LY */

    if (address == LY)
    {
        return;
    }

    /* DMA */

    if (address == DMA)
    {
        mem[DMA] = value;

        uint16_t source =
            (uint16_t)value << 8;

        for (int i = 0; i < 160; i++)
        {
            mem[0xFE00 + i] =
                memory_read(
                    (uint16_t)(
                        source + i
                    )
                );
        }

        return;
    }

    mem[address] = value;
}

/* ============================================================
   FETCH
   ============================================================ */

static uint8_t fetch8(void)
{
    uint8_t value =
        memory_read(cpu.PC);

    cpu.PC++;

    return value;
}

static uint16_t fetch16(void)
{
    uint8_t lo = fetch8();
    uint8_t hi = fetch8();

    return (uint16_t)(
        ((uint16_t)hi << 8) |
        lo
    );
}

/* ============================================================
   STACK
   ============================================================ */

static void push16(uint16_t value)
{
    cpu.SP--;

    memory_write(
        cpu.SP,
        (uint8_t)(value >> 8)
    );

    cpu.SP--;

    memory_write(
        cpu.SP,
        (uint8_t)value
    );
}

static uint16_t pop16(void)
{
    uint8_t lo =
        memory_read(cpu.SP++);

    uint8_t hi =
        memory_read(cpu.SP++);

    return (uint16_t)(
        ((uint16_t)hi << 8) |
        lo
    );
}

/* ============================================================
   ALU
   ============================================================ */

static uint8_t alu_add(
    uint8_t a,
    uint8_t b,
    int carry)
{
    uint16_t result =
        (uint16_t)a +
        b +
        carry;

    uint8_t r =
        (uint8_t)result;

    cpu.F = 0;

    if (r == 0)
        cpu.F |= FLAG_Z;

    if (((a & 0x0F) +
         (b & 0x0F) +
         carry) > 0x0F)
    {
        cpu.F |= FLAG_H;
    }

    if (result > 0xFF)
        cpu.F |= FLAG_C;

    return r;
}

static uint8_t alu_sub(
    uint8_t a,
    uint8_t b,
    int carry)
{
    int result =
        (int)a -
        (int)b -
        carry;

    uint8_t r =
        (uint8_t)result;

    cpu.F = FLAG_N;

    if (r == 0)
        cpu.F |= FLAG_Z;

    if ((a & 0x0F) <
        ((b & 0x0F) + carry))
    {
        cpu.F |= FLAG_H;
    }

    if (result < 0)
        cpu.F |= FLAG_C;

    return r;
}

static uint8_t inc8(uint8_t v)
{
    uint8_t old_c =
        cpu.F & FLAG_C;

    uint8_t r =
        (uint8_t)(v + 1);

    cpu.F = old_c;

    if (r == 0)
        cpu.F |= FLAG_Z;

    if ((v & 0x0F) == 0x0F)
        cpu.F |= FLAG_H;

    return r;
}

static uint8_t dec8(uint8_t v)
{
    uint8_t old_c =
        cpu.F & FLAG_C;

    uint8_t r =
        (uint8_t)(v - 1);

    cpu.F =
        old_c | FLAG_N;

    if (r == 0)
        cpu.F |= FLAG_Z;

    if ((v & 0x0F) == 0)
        cpu.F |= FLAG_H;

    return r;
}

static uint16_t add16(
    uint16_t a,
    uint16_t b)
{
    uint32_t result =
        (uint32_t)a +
        (uint32_t)b;

    uint8_t old_z =
        cpu.F & FLAG_Z;

    cpu.F = old_z;

    if (((a & 0x0FFF) +
         (b & 0x0FFF)) > 0x0FFF)
    {
        cpu.F |= FLAG_H;
    }

    if (result > 0xFFFF)
        cpu.F |= FLAG_C;

    return (uint16_t)result;
}

/* ============================================================
   DAA
   ============================================================ */

static void daa(void)
{
    uint8_t correction = 0;
    int carry =
        (cpu.F & FLAG_C) != 0;

    if (!(cpu.F & FLAG_N))
    {
        if ((cpu.F & FLAG_H) ||
            (cpu.A & 0x0F) > 9)
        {
            correction |= 0x06;
        }

        if (carry || cpu.A > 0x99)
        {
            correction |= 0x60;
            carry = 1;
        }

        cpu.A =
            (uint8_t)(
                cpu.A + correction
            );
    }
    else
    {
        if (cpu.F & FLAG_H)
            correction |= 0x06;

        if (carry)
            correction |= 0x60;

        cpu.A =
            (uint8_t)(
                cpu.A - correction
            );
    }

    cpu.F &= FLAG_N;

    if (cpu.A == 0)
        cpu.F |= FLAG_Z;

    if (carry)
        cpu.F |= FLAG_C;
}

/* ============================================================
   CONDITION
   ============================================================ */

static int condition_true(int condition)
{
    switch (condition)
    {
        case 0:
            return !(cpu.F & FLAG_Z);

        case 1:
            return (cpu.F & FLAG_Z) != 0;

        case 2:
            return !(cpu.F & FLAG_C);

        case 3:
            return (cpu.F & FLAG_C) != 0;
    }

    return 0;
}

/* ============================================================
   8-BIT REGISTER ACCESS
   ============================================================ */

static uint8_t read_r(int r)
{
    switch (r)
    {
        case 0: return cpu.B;
        case 1: return cpu.C;
        case 2: return cpu.D;
        case 3: return cpu.E;
        case 4: return cpu.H;
        case 5: return cpu.L;
        case 6: return memory_read(HL());
        case 7: return cpu.A;
    }

    return 0xFF;
}

static void write_r(
    int r,
    uint8_t value)
{
    switch (r)
    {
        case 0: cpu.B = value; break;
        case 1: cpu.C = value; break;
        case 2: cpu.D = value; break;
        case 3: cpu.E = value; break;
        case 4: cpu.H = value; break;
        case 5: cpu.L = value; break;
        case 6: memory_write(HL(), value); break;
        case 7: cpu.A = value; break;
    }
}

/* ============================================================
   CB PREFIX
   ============================================================ */

static int cb_execute(uint8_t op)
{
    int group =
        op >> 6;

    int bit =
        (op >> 3) & 7;

    int r =
        op & 7;

    uint8_t value =
        read_r(r);

    if (group == 0)
    {
        int operation =
            (op >> 3) & 7;

        uint8_t result =
            value;

        int carry = 0;

        switch (operation)
        {
            case 0:
                carry =
                    (value >> 7) & 1;

                result =
                    (uint8_t)(
                        (value << 1) |
                        carry
                    );
                break;

            case 1:
                carry =
                    value & 1;

                result =
                    (uint8_t)(
                        (value >> 1) |
                        (carry << 7)
                    );
                break;

            case 2:
            {
                int old_c =
                    (cpu.F & FLAG_C) != 0;

                carry =
                    (value >> 7) & 1;

                result =
                    (uint8_t)(
                        (value << 1) |
                        old_c
                    );
                break;
            }

            case 3:
            {
                int old_c =
                    (cpu.F & FLAG_C) != 0;

                carry =
                    value & 1;

                result =
                    (uint8_t)(
                        (value >> 1) |
                        (old_c << 7)
                    );
                break;
            }

            case 4:
                carry =
                    (value >> 7) & 1;

                result =
                    (uint8_t)(
                        value << 1
                    );
                break;

            case 5:
                carry =
                    value & 1;

                result =
                    (uint8_t)(
                        (value >> 1) |
                        (value & 0x80)
                    );
                break;

            case 6:
                result =
                    (uint8_t)(
                        (value << 4) |
                        (value >> 4)
                    );

                carry = 0;
                break;

            case 7:
                carry =
                    value & 1;

                result =
                    (uint8_t)(
                        value >> 1
                    );
                break;
        }

        cpu.F = 0;

        if (result == 0)
            cpu.F |= FLAG_Z;

        if (carry)
            cpu.F |= FLAG_C;

        write_r(r, result);

        return r == 6 ? 16 : 8;
    }

    if (group == 1)
    {
        cpu.F =
            (uint8_t)(
                (cpu.F & FLAG_C) |
                FLAG_H
            );

        if (!(value & (1 << bit)))
            cpu.F |= FLAG_Z;

        return r == 6 ? 12 : 8;
    }

    if (group == 2)
    {
        value =
            (uint8_t)(
                value &
                ~(1 << bit)
            );

        write_r(r, value);

        return r == 6 ? 16 : 8;
    }

    value =
        (uint8_t)(
            value |
            (1 << bit)
        );

    write_r(r, value);

    return r == 6 ? 16 : 8;
}

/* ============================================================
   CPU
   ============================================================ */

static int cpu_step(void)
{
    if (cpu.stopped)
        return 4;

    if (cpu.halted)
        return 4;

    uint16_t old_pc =
        cpu.PC;

    uint8_t op =
        fetch8();

    /* ========================================================
       CB
       ======================================================== */

    if (op == 0xCB)
    {
        return cb_execute(fetch8());
    }

    /* ========================================================
       NOP
       ======================================================== */

    if (op == 0x00)
        return 4;

    /* ========================================================
       HALT
       ======================================================== */

    if (op == 0x76)
    {
        cpu.halted = 1;
        return 4;
    }

    /* ========================================================
       STOP
       ======================================================== */

    if (op == 0x10)
    {
        fetch8();
        cpu.stopped = 1;
        return 4;
    }

    /* ========================================================
       LD r,r
       ======================================================== */

    if (op >= 0x40 &&
        op <= 0x7F)
    {
        int dst =
            (op >> 3) & 7;

        int src =
            op & 7;

        write_r(
            dst,
            read_r(src)
        );

        return
            (dst == 6 ||
             src == 6)
            ? 8
            : 4;
    }

    /* ========================================================
       INC / DEC r
       ======================================================== */

    if ((op & 0xC7) == 0x04)
    {
        int r =
            (op >> 3) & 7;

        write_r(
            r,
            inc8(read_r(r))
        );

        return r == 6 ? 12 : 4;
    }

    if ((op & 0xC7) == 0x05)
    {
        int r =
            (op >> 3) & 7;

        write_r(
            r,
            dec8(read_r(r))
        );

        return r == 6 ? 12 : 4;
    }

    /* ========================================================
       LD r,n
       ======================================================== */

    if ((op & 0xC7) == 0x06)
    {
        int r =
            (op >> 3) & 7;

        write_r(
            r,
            fetch8()
        );

        return r == 6 ? 12 : 8;
    }

    /* ========================================================
       ALU A,r
       ======================================================== */

    if (op >= 0x80 &&
        op <= 0xBF)
    {
        int operation =
            (op >> 3) & 7;

        int r =
            op & 7;

        uint8_t value =
            read_r(r);

        switch (operation)
        {
            case 0:
                cpu.A =
                    alu_add(
                        cpu.A,
                        value,
                        0
                    );
                break;

            case 1:
                cpu.A =
                    alu_add(
                        cpu.A,
                        value,
                        (cpu.F & FLAG_C) != 0
                    );
                break;

            case 2:
                cpu.A =
                    alu_sub(
                        cpu.A,
                        value,
                        0
                    );
                break;

            case 3:
                cpu.A =
                    alu_sub(
                        cpu.A,
                        value,
                        (cpu.F & FLAG_C) != 0
                    );
                break;

            case 4:
                cpu.A &= value;

                cpu.F =
                    FLAG_H |
                    (cpu.A == 0
                     ? FLAG_Z : 0);
                break;

            case 5:
                cpu.A ^= value;

                cpu.F =
                    cpu.A == 0
                    ? FLAG_Z
                    : 0;
                break;

            case 6:
                cpu.A |= value;

                cpu.F =
                    cpu.A == 0
                    ? FLAG_Z
                    : 0;
                break;

            case 7:
                (void)alu_sub(
                    cpu.A,
                    value,
                    0
                );
                break;
        }

        return r == 6 ? 8 : 4;
    }

    /* ========================================================
       16-BIT IMMEDIATE
       ======================================================== */

    switch (op)
    {
        case 0x01:
            set_BC(fetch16());
            return 12;

        case 0x11:
            set_DE(fetch16());
            return 12;

        case 0x21:
            set_HL(fetch16());
            return 12;

        case 0x31:
            cpu.SP = fetch16();
            return 12;
    }

    /* ========================================================
       16-BIT INC
       ======================================================== */

    switch (op)
    {
        case 0x03:
            set_BC(BC() + 1);
            return 8;

        case 0x13:
            set_DE(DE() + 1);
            return 8;

        case 0x23:
            set_HL(HL() + 1);
            return 8;

        case 0x33:
            cpu.SP++;
            return 8;
    }

    /* ========================================================
       16-BIT DEC
       ======================================================== */

    switch (op)
    {
        case 0x0B:
            set_BC(BC() - 1);
            return 8;

        case 0x1B:
            set_DE(DE() - 1);
            return 8;

        case 0x2B:
            set_HL(HL() - 1);
            return 8;

        case 0x3B:
            cpu.SP--;
            return 8;
    }

    /* ========================================================
       ADD HL,rr
       ======================================================== */

    switch (op)
    {
        case 0x09:
            set_HL(add16(HL(), BC()));
            return 8;

        case 0x19:
            set_HL(add16(HL(), DE()));
            return 8;

        case 0x29:
            set_HL(add16(HL(), HL()));
            return 8;

        case 0x39:
            set_HL(add16(HL(), cpu.SP));
            return 8;
    }

    /* ========================================================
       LD (BC/DE),A
       ======================================================== */

    switch (op)
    {
        case 0x02:
            memory_write(BC(), cpu.A);
            return 8;

        case 0x12:
            memory_write(DE(), cpu.A);
            return 8;

        case 0x0A:
            cpu.A = memory_read(BC());
            return 8;

        case 0x1A:
            cpu.A = memory_read(DE());
            return 8;
    }

    /* ========================================================
       HL+
       ======================================================== */

    switch (op)
    {
        case 0x22:
        {
            uint16_t a = HL();

            memory_write(a, cpu.A);

            set_HL(
                (uint16_t)(a + 1)
            );

            return 8;
        }

        case 0x2A:
        {
            uint16_t a = HL();

            cpu.A =
                memory_read(a);

            set_HL(
                (uint16_t)(a + 1)
            );

            return 8;
        }

        case 0x32:
        {
            uint16_t a = HL();

            memory_write(a, cpu.A);

            set_HL(
                (uint16_t)(a - 1)
            );

            return 8;
        }

        case 0x3A:
        {
            uint16_t a = HL();

            cpu.A =
                memory_read(a);

            set_HL(
                (uint16_t)(a - 1)
            );

            return 8;
        }
    }

    /* ========================================================
       ROTATE A
       ======================================================== */

    switch (op)
    {
        case 0x07:
        {
            int c =
                (cpu.A >> 7) & 1;

            cpu.A =
                (uint8_t)(
                    (cpu.A << 1) |
                    c
                );

            cpu.F =
                c ? FLAG_C : 0;

            return 4;
        }

        case 0x0F:
        {
            int c =
                cpu.A & 1;

            cpu.A =
                (uint8_t)(
                    (cpu.A >> 1) |
                    (c << 7)
                );

            cpu.F =
                c ? FLAG_C : 0;

            return 4;
        }

        case 0x17:
        {
            int old_c =
                (cpu.F & FLAG_C) != 0;

            int c =
                (cpu.A >> 7) & 1;

            cpu.A =
                (uint8_t)(
                    (cpu.A << 1) |
                    old_c
                );

            cpu.F =
                c ? FLAG_C : 0;

            return 4;
        }

        case 0x1F:
        {
            int old_c =
                (cpu.F & FLAG_C) != 0;

            int c =
                cpu.A & 1;

            cpu.A =
                (uint8_t)(
                    (cpu.A >> 1) |
                    (old_c << 7)
                );

            cpu.F =
                c ? FLAG_C : 0;

            return 4;
        }
    }

    /* ========================================================
       DAA
       ======================================================== */

    if (op == 0x27)
    {
        daa();
        return 4;
    }

    /* ========================================================
       CPL
       ======================================================== */

    if (op == 0x2F)
    {
        cpu.A =
            (uint8_t)~cpu.A;

        cpu.F |=
            FLAG_N | FLAG_H;

        return 4;
    }

    /* ========================================================
       SCF
       ======================================================== */

    if (op == 0x37)
    {
        cpu.F &=
            (uint8_t)~(FLAG_N | FLAG_H);

        cpu.F |= FLAG_C;

        return 4;
    }

    /* ========================================================
       CCF
       ======================================================== */

    if (op == 0x3F)
    {
        cpu.F &=
            (uint8_t)~(FLAG_N | FLAG_H);

        cpu.F ^=
            FLAG_C;

        return 4;
    }

    /* ========================================================
       JP
       ======================================================== */

    if (op == 0xC3)
    {
        cpu.PC = fetch16();
        return 16;
    }

    if (op == 0xE9)
    {
        cpu.PC = HL();
        return 4;
    }

    if (op == 0xC2 ||
        op == 0xCA ||
        op == 0xD2 ||
        op == 0xDA)
    {
        int condition =
            (op >> 3) & 3;

        uint16_t address =
            fetch16();

        if (condition_true(condition))
        {
            cpu.PC = address;
            return 16;
        }

        return 12;
    }

    /* ========================================================
       JR
       ======================================================== */

    if (op == 0x18)
    {
        int8_t offset =
            (int8_t)fetch8();

        cpu.PC =
            (uint16_t)(
                cpu.PC + offset
            );

        return 12;
    }

    if (op == 0x20 ||
        op == 0x28 ||
        op == 0x30 ||
        op == 0x38)
    {
        int condition =
            (op >> 3) & 3;

        int8_t offset =
            (int8_t)fetch8();

        if (condition_true(condition))
        {
            cpu.PC =
                (uint16_t)(
                    cpu.PC + offset
                );

            return 12;
        }

        return 8;
    }

    /* ========================================================
       CALL
       ======================================================== */

    if (op == 0xCD)
    {
        uint16_t address =
            fetch16();

        push16(cpu.PC);

        cpu.PC = address;

        return 24;
    }

    if (op == 0xC4 ||
        op == 0xCC ||
        op == 0xD4 ||
        op == 0xDC)
    {
        int condition =
            (op >> 3) & 3;

        uint16_t address =
            fetch16();

        if (condition_true(condition))
        {
            push16(cpu.PC);

            cpu.PC = address;

            return 24;
        }

        return 12;
    }

    /* ========================================================
       RET
       ======================================================== */

    if (op == 0xC9)
    {
        cpu.PC = pop16();
        return 16;
    }

    if (op == 0xD9)
    {
        cpu.PC = pop16();

        cpu.IME = 1;

        return 16;
    }

    if (op == 0xC0 ||
        op == 0xC8 ||
        op == 0xD0 ||
        op == 0xD8)
    {
        int condition =
            (op >> 3) & 3;

        if (condition_true(condition))
        {
            cpu.PC = pop16();
            return 20;
        }

        return 8;
    }

    /* ========================================================
       RST
       ======================================================== */

    switch (op)
    {
        case 0xC7:
            push16(cpu.PC);
            cpu.PC = 0x00;
            return 16;

        case 0xCF:
            push16(cpu.PC);
            cpu.PC = 0x08;
            return 16;

        case 0xD7:
            push16(cpu.PC);
            cpu.PC = 0x10;
            return 16;

        case 0xDF:
            push16(cpu.PC);
            cpu.PC = 0x18;
            return 16;

        case 0xE7:
            push16(cpu.PC);
            cpu.PC = 0x20;
            return 16;

        case 0xEF:
            push16(cpu.PC);
            cpu.PC = 0x28;
            return 16;

        case 0xF7:
            push16(cpu.PC);
            cpu.PC = 0x30;
            return 16;

        case 0xFF:
            push16(cpu.PC);
            cpu.PC = 0x38;
            return 16;
    }

    /* ========================================================
       PUSH
       ======================================================== */

    switch (op)
    {
        case 0xC5:
            push16(BC());
            return 16;

        case 0xD5:
            push16(DE());
            return 16;

        case 0xE5:
            push16(HL());
            return 16;

        case 0xF5:
            push16(AF());
            return 16;
    }

    /* ========================================================
       POP
       ======================================================== */

    switch (op)
    {
        case 0xC1:
            set_BC(pop16());
            return 12;

        case 0xD1:
            set_DE(pop16());
            return 12;

        case 0xE1:
            set_HL(pop16());
            return 12;

        case 0xF1:
            set_AF(pop16());
            return 12;
    }

    /* ========================================================
       DI
       ======================================================== */

    if (op == 0xF3)
    {
        cpu.IME = 0;
        cpu.ime_delay = 0;
        return 4;
    }

    /* ========================================================
       EI
       ======================================================== */

    if (op == 0xFB)
    {
        cpu.ime_delay = 2;
        return 4;
    }

    /* ========================================================
       IMMEDIATE ALU
       ======================================================== */

    switch (op)
    {
        case 0xC6:
            cpu.A =
                alu_add(
                    cpu.A,
                    fetch8(),
                    0
                );
            return 8;

        case 0xCE:
        {
            int carry =
                (cpu.F & FLAG_C) != 0;

            cpu.A =
                alu_add(
                    cpu.A,
                    fetch8(),
                    carry
                );

            return 8;
        }

        case 0xD6:
            cpu.A =
                alu_sub(
                    cpu.A,
                    fetch8(),
                    0
                );
            return 8;

        case 0xDE:
        {
            int carry =
                (cpu.F & FLAG_C) != 0;

            cpu.A =
                alu_sub(
                    cpu.A,
                    fetch8(),
                    carry
                );

            return 8;
        }

        case 0xE6:
            cpu.A &= fetch8();

            cpu.F =
                FLAG_H |
                (cpu.A == 0
                 ? FLAG_Z : 0);

            return 8;

        case 0xEE:
            cpu.A ^= fetch8();

            cpu.F =
                cpu.A == 0
                ? FLAG_Z
                : 0;

            return 8;

        case 0xF6:
            cpu.A |= fetch8();

            cpu.F =
                cpu.A == 0
                ? FLAG_Z
                : 0;

            return 8;

        case 0xFE:
        {
            uint8_t v =
                fetch8();

            (void)alu_sub(
                cpu.A,
                v,
                0
            );

            return 8;
        }
    }

    /* ========================================================
       LD (a16),SP
       ======================================================== */

    if (op == 0x08)
    {
        uint16_t address =
            fetch16();

        memory_write(
            address,
            (uint8_t)cpu.SP
        );

        memory_write(
            address + 1,
            (uint8_t)(cpu.SP >> 8)
        );

        return 20;
    }

    /* ========================================================
       LD A,(a16)
       LD (a16),A
       ======================================================== */

    if (op == 0xEA)
    {
        uint16_t address =
            fetch16();

        memory_write(
            address,
            cpu.A
        );

        return 16;
    }

    if (op == 0xFA)
    {
        uint16_t address =
            fetch16();

        cpu.A =
            memory_read(address);

        return 16;
    }

    /* ========================================================
       LDH
       ======================================================== */

    if (op == 0xE0)
    {
        uint8_t n =
            fetch8();

        memory_write(
            (uint16_t)(
                0xFF00 + n
            ),
            cpu.A
        );

        return 12;
    }

    if (op == 0xF0)
    {
        uint8_t n =
            fetch8();

        cpu.A =
            memory_read(
                (uint16_t)(
                    0xFF00 + n
                )
            );

        return 12;
    }

    if (op == 0xE2)
    {
        memory_write(
            (uint16_t)(
                0xFF00 + cpu.C
            ),
            cpu.A
        );

        return 8;
    }

    if (op == 0xF2)
    {
        cpu.A =
            memory_read(
                (uint16_t)(
                    0xFF00 + cpu.C
                )
            );

        return 8;
    }

    /* ========================================================
       ADD SP,e8
       ======================================================== */

    if (op == 0xE8)
    {
        int8_t n =
            (int8_t)fetch8();

        uint16_t old =
            cpu.SP;

        uint16_t result =
            (uint16_t)(
                cpu.SP + n
            );

        cpu.F = 0;

        if (((old & 0x0F) +
             ((uint16_t)n & 0x0F)) >
            0x0F)
        {
            cpu.F |= FLAG_H;
        }

        if (((old & 0xFF) +
             ((uint16_t)n & 0xFF)) >
            0xFF)
        {
            cpu.F |= FLAG_C;
        }

        cpu.SP = result;

        return 16;
    }

    /* ========================================================
       LD HL,SP+e8
       ======================================================== */

    if (op == 0xF8)
    {
        int8_t n =
            (int8_t)fetch8();

        uint16_t old =
            cpu.SP;

        uint16_t result =
            (uint16_t)(
                cpu.SP + n
            );

        cpu.F = 0;

        if (((old & 0x0F) +
             ((uint16_t)n & 0x0F)) >
            0x0F)
        {
            cpu.F |= FLAG_H;
        }

        if (((old & 0xFF) +
             ((uint16_t)n & 0xFF)) >
            0xFF)
        {
            cpu.F |= FLAG_C;
        }

        set_HL(result);

        return 12;
    }

    /* ========================================================
       LD SP,HL
       ======================================================== */

    if (op == 0xF9)
    {
        cpu.SP = HL();
        return 8;
    }

    /* ========================================================
       LD A,(FF00+C)
       ======================================================== */

    if (op == 0xF2)
    {
        cpu.A =
            memory_read(
                (uint16_t)(
                    0xFF00 + cpu.C
                )
            );

        return 8;
    }

    /* ========================================================
       UNKNOWN OPCODE
       ======================================================== */

    /*
       Do NOT immediately kill the emulator.

       This makes debugging much easier.
    */

    fprintf(
        stderr,
        "WARNING: Unsupported opcode "
        "%02X at PC %04X\n",
        op,
        old_pc
    );

    /*
       Treat it as NOP so the emulator continues.
    */

    return 4;
}

/* ============================================================
   TIMER
   ============================================================ */

static void timer_step(int cycles)
{
    div_counter += cycles;

    while (div_counter >= 256)
    {
        div_counter -= 256;

        mem[DIV]++;
    }

    if (!(mem[TAC] & 0x04))
        return;

    int frequency;

    switch (mem[TAC] & 3)
    {
        case 0:
            frequency = 1024;
            break;

        case 1:
            frequency = 16;
            break;

        case 2:
            frequency = 64;
            break;

        default:
            frequency = 256;
            break;
    }

    timer_counter += cycles;

    while (timer_counter >= frequency)
    {
        timer_counter -= frequency;

        if (mem[TIMA] == 0xFF)
        {
            mem[TIMA] = mem[TMA];

            mem[IFREG] |= 0x04;
        }
        else
        {
            mem[TIMA]++;
        }
    }
}

/* ============================================================
   TILE PIXEL
   ============================================================ */

static int tile_pixel(
    int tile_number,
    int x,
    int y)
{
    uint16_t address =
        (uint16_t)(
            0x8000 +
            tile_number * 16 +
            y * 2
        );

    uint8_t lo =
        memory_read(address);

    uint8_t hi =
        memory_read(
            address + 1
        );

    int bit =
        7 - x;

    return
        ((lo >> bit) & 1) |
        (((hi >> bit) & 1) << 1);
}

/* ============================================================
   BACKGROUND
   ============================================================ */

static void render_background(
    int line,
    uint8_t *bg_priority)
{
    uint8_t lcdc =
        memory_read(LCDC);

    if (!(lcdc & 0x01))
    {
        for (int x = 0;
             x < SCREEN_W;
             x++)
        {
            framebuffer[
                line * SCREEN_W + x
            ] =
                gb_colors[0];

            bg_priority[x] = 0;
        }

        return;
    }

    uint8_t scx =
        memory_read(SCX);

    uint8_t scy =
        memory_read(SCY);

    int world_y =
        (scy + line) & 0xFF;

    int tile_y =
        world_y / 8;

    int pixel_y =
        world_y & 7;

    uint16_t map_base =
        (lcdc & 0x08)
        ? 0x9C00
        : 0x9800;

    int unsigned_tiles =
        (lcdc & 0x10) != 0;

    uint8_t palette =
        memory_read(BGP);

    for (int x = 0;
         x < SCREEN_W;
         x++)
    {
        int world_x =
            (scx + x) & 0xFF;

        int tile_x =
            world_x / 8;

        int pixel_x =
            world_x & 7;

        uint16_t address =
            (uint16_t)(
                map_base +
                tile_y * 32 +
                tile_x
            );

        uint8_t tile =
            memory_read(address);

        int tile_number;

        if (unsigned_tiles)
        {
            tile_number = tile;
        }
        else
        {
            tile_number =
                (int8_t)tile + 256;

            tile_number -= 128;
        }

        int color =
            tile_pixel(
                tile_number,
                pixel_x,
                pixel_y
            );

        int shade =
            (palette >>
             (color * 2)) & 3;

        framebuffer[
            line * SCREEN_W + x
        ] =
            gb_colors[shade];

        bg_priority[x] =
            color != 0;
    }
}

/* ============================================================
   WINDOW
   ============================================================ */

static void render_window(
    int line,
    uint8_t *bg_priority)
{
    uint8_t lcdc =
        memory_read(LCDC);

    if (!(lcdc & 0x20))
        return;

    int wy =
        memory_read(WY);

    int wx =
        memory_read(WX) - 7;

    if (line < wy)
        return;

    uint16_t map_base =
        (lcdc & 0x40)
        ? 0x9C00
        : 0x9800;

    int unsigned_tiles =
        (lcdc & 0x10) != 0;

    uint8_t palette =
        memory_read(BGP);

    int window_y =
        window_line_counter;

    for (int x = 0;
         x < SCREEN_W;
         x++)
    {
        if (x < wx)
            continue;

        int window_x =
            x - wx;

        int tile_x =
            window_x / 8;

        int pixel_x =
            window_x & 7;

        int tile_y =
            window_y / 8;

        int pixel_y =
            window_y & 7;

        uint16_t address =
            (uint16_t)(
                map_base +
                tile_y * 32 +
                tile_x
            );

        uint8_t tile =
            memory_read(address);

        int tile_number;

        if (unsigned_tiles)
            tile_number = tile;
        else
            tile_number =
                (int8_t)tile + 256 - 128;

        int color =
            tile_pixel(
                tile_number,
                pixel_x,
                pixel_y
            );

        int shade =
            (palette >>
             (color * 2)) & 3;

        framebuffer[
            line * SCREEN_W + x
        ] =
            gb_colors[shade];

        bg_priority[x] =
            color != 0;
    }

    window_line_counter++;
}

/* ============================================================
   SPRITES
   ============================================================ */

static void render_sprites(
    int line,
    uint8_t *bg_priority)
{
    uint8_t lcdc =
        memory_read(LCDC);

    if (!(lcdc & 0x02))
        return;

    int height =
        (lcdc & 0x04)
        ? 16
        : 8;

    int visible[10];
    int visible_count = 0;

    /*
       Find sprites on current scanline.
    */

    for (int i = 0;
         i < 40;
         i++)
    {
        int y =
            (int)memory_read(
                0xFE00 + i * 4
            ) - 16;

        if (line >= y &&
            line < y + height)
        {
            if (visible_count < 10)
            {
                visible[
                    visible_count++
                ] = i;
            }
        }
    }

    /*
       Draw later OAM entries first,
       so earlier OAM entries remain on top.
    */

    for (int n = visible_count - 1;
         n >= 0;
         n--)
    {
        int sprite =
            visible[n];

        uint16_t address =
            (uint16_t)(
                0xFE00 +
                sprite * 4
            );

        int y =
            (int)memory_read(address) - 16;

        int x =
            (int)memory_read(
                address + 1
            ) - 8;

        int tile =
            memory_read(
                address + 2
            );

        uint8_t flags =
            memory_read(
                address + 3
            );

        int row =
            line - y;

        if (flags & 0x40)
        {
            row =
                height - 1 - row;
        }

        if (height == 16)
            tile &= 0xFE;

        if (row >= 8)
            tile++;

        uint16_t tile_address =
            (uint16_t)(
                0x8000 +
                tile * 16 +
                row * 2
            );

        uint8_t lo =
            memory_read(
                tile_address
            );

        uint8_t hi =
            memory_read(
                tile_address + 1
            );

        uint8_t palette =
            (flags & 0x10)
            ? memory_read(OBP1)
            : memory_read(OBP0);

        for (int px = 0;
             px < 8;
             px++)
        {
            int sx =
                x + px;

            if (sx < 0 ||
                sx >= SCREEN_W)
                continue;

            int bit;

            if (flags & 0x20)
                bit = px;
            else
                bit = 7 - px;

            int color =
                ((lo >> bit) & 1) |
                (((hi >> bit) & 1) << 1);

            if (color == 0)
                continue;

            /*
               OBJ priority.
            */

            if ((flags & 0x80) &&
                bg_priority[sx])
            {
                continue;
            }

            int shade =
                (palette >>
                 (color * 2)) & 3;

            framebuffer[
                line * SCREEN_W + sx
            ] =
                gb_colors[shade];
        }
    }
}

/* ============================================================
   PPU
   ============================================================ */

static void ppu_step(int cycles)
{
    ppu_counter += cycles;

    while (ppu_counter >= 456)
    {
        ppu_counter -= 456;

        int line =
            mem[LY];

        if (line < 144)
        {
            uint8_t priority[
                SCREEN_W
            ];

            render_background(
                line,
                priority
            );

            render_window(
                line,
                priority
            );

            render_sprites(
                line,
                priority
            );
        }

        line++;

        if (line == 144)
        {
            mem[LY] = 144;

            /*
               VBlank interrupt.
            */

            mem[IFREG] |= 0x01;

            window_line_counter = 0;
        }
        else if (line > 153)
        {
            line = 0;

            mem[LY] = 0;

            window_line_counter = 0;
        }
        else
        {
            mem[LY] =
                (uint8_t)line;
        }

        /*
           LY == LYC.
        */

        if (mem[LY] == mem[LYC])
            mem[STAT] |= 0x04;
        else
            mem[STAT] &=
                (uint8_t)~0x04;
    }
}

/* ============================================================
   INTERRUPTS
   ============================================================ */

static int interrupt_step(void)
{
    uint8_t enabled =
        memory_read(IE);

    uint8_t requested =
        memory_read(IFREG);

    uint8_t pending =
        enabled &
        requested &
        0x1F;

    if (!pending)
        return 0;

    /*
       Interrupt wakes HALT.
    */

    cpu.halted = 0;

    if (!cpu.IME)
        return 0;

    for (int i = 0; i < 5; i++)
    {
        if (pending & (1 << i))
        {
            uint16_t vector =
                (uint16_t)(
                    0x40 +
                    i * 8
                );

            cpu.IME = 0;

            mem[IFREG] &=
                (uint8_t)~(1 << i);

            push16(cpu.PC);

            cpu.PC =
                vector;

            return 20;
        }
    }

    return 0;
}

/* ============================================================
   KEYBOARD
   ============================================================ */

static void update_keyboard(void)
{
    const Uint8 *keys =
        SDL_GetKeyboardState(NULL);

    int old_a =
        joypad.A;

    int old_b =
        joypad.B;

    int old_select =
        joypad.SELECT;

    int old_start =
        joypad.START;

    int old_right =
        joypad.RIGHT;

    int old_left =
        joypad.LEFT;

    int old_up =
        joypad.UP;

    int old_down =
        joypad.DOWN;

    /*
       Active LOW:

       0 = pressed
       1 = released
    */

    joypad.A =
        keys[SDL_SCANCODE_Z]
        ? 0 : 1;

    joypad.B =
        keys[SDL_SCANCODE_X]
        ? 0 : 1;

    joypad.START =
        keys[SDL_SCANCODE_RETURN]
        ? 0 : 1;

    /*
       Backspace is SELECT.

       Space is also SELECT as a backup.
    */

    joypad.SELECT =
        (keys[SDL_SCANCODE_BACKSPACE] ||
         keys[SDL_SCANCODE_SPACE])
        ? 0 : 1;

    joypad.RIGHT =
        keys[SDL_SCANCODE_RIGHT]
        ? 0 : 1;

    joypad.LEFT =
        keys[SDL_SCANCODE_LEFT]
        ? 0 : 1;

    joypad.UP =
        keys[SDL_SCANCODE_UP]
        ? 0 : 1;

    joypad.DOWN =
        keys[SDL_SCANCODE_DOWN]
        ? 0 : 1;

    /*
       Request joypad interrupt when a key
       changes from released to pressed.
    */

    if ((old_a == 1 &&
         joypad.A == 0) ||

        (old_b == 1 &&
         joypad.B == 0) ||

        (old_select == 1 &&
         joypad.SELECT == 0) ||

        (old_start == 1 &&
         joypad.START == 0) ||

        (old_right == 1 &&
         joypad.RIGHT == 0) ||

        (old_left == 1 &&
         joypad.LEFT == 0) ||

        (old_up == 1 &&
         joypad.UP == 0) ||

        (old_down == 1 &&
         joypad.DOWN == 0))
    {
        mem[IFREG] |= 0x10;
    }
}

/* ============================================================
   ROM LOADING
   ============================================================ */

static int load_rom(
    const char *filename)
{
    FILE *f =
        fopen(filename, "rb");

    if (!f)
    {
        fprintf(
            stderr,
            "\nERROR: Cannot open ROM:\n%s\n\n",
            filename
        );

        return 0;
    }

    fseek(
        f,
        0,
        SEEK_END
    );

    long size =
        ftell(f);

    fseek(
        f,
        0,
        SEEK_SET
    );

    if (size <= 0)
    {
        fclose(f);

        fprintf(
            stderr,
            "ERROR: ROM is empty.\n"
        );

        return 0;
    }

    rom_size =
        (size_t)size;

    rom =
        (uint8_t *)malloc(
            rom_size
        );

    if (!rom)
    {
        fclose(f);

        fprintf(
            stderr,
            "ERROR: Memory allocation failed.\n"
        );

        return 0;
    }

    size_t read =
        fread(
            rom,
            1,
            rom_size,
            f
        );

    fclose(f);

    if (read != rom_size)
    {
        free(rom);
        rom = NULL;

        fprintf(
            stderr,
            "ERROR: Could not read ROM.\n"
        );

        return 0;
    }

    /*
       Cartridge type.
    */

    if (rom_size > 0x147)
    {
        uint8_t type =
            rom[0x147];

        switch (type)
        {
            case 0x00:
                mbc_type = 0;
                break;

            case 0x01:
            case 0x02:
            case 0x03:
                mbc_type = 1;
                break;

            default:
                /*
                   Many simple cartridges still
                   work without banking.
                */
                mbc_type = 0;

                printf(
                    "Cartridge type %02X "
                    "not fully implemented.\n",
                    type
                );

                break;
        }
    }

    rom_bank = 1;
    ram_bank = 0;
    ram_enabled = 0;
    banking_mode = 0;

    return 1;
}

/* ============================================================
   EMULATOR INITIALIZATION
   ============================================================ */

static void emulator_init(void)
{
    memset(
        &cpu,
        0,
        sizeof(cpu)
    );

    memset(
        mem,
        0,
        sizeof(mem)
    );

    memset(
        eram,
        0,
        sizeof(eram)
    );

    /*
       DMG post-boot CPU registers.
    */

    cpu.A = 0x01;
    cpu.F = 0xB0;

    cpu.B = 0x00;
    cpu.C = 0x13;

    cpu.D = 0x00;
    cpu.E = 0xD8;

    cpu.H = 0x01;
    cpu.L = 0x4D;

    cpu.SP = 0xFFFE;
    cpu.PC = 0x0100;

    cpu.IME = 0;
    cpu.halted = 0;
    cpu.stopped = 0;
    cpu.ime_delay = 0;

    /*
       Game Boy registers.
    */

    mem[JOYP] = 0x30;

    mem[SB] = 0;
    mem[SC] = 0;

    mem[DIV] = 0;
    mem[TIMA] = 0;
    mem[TMA] = 0;
    mem[TAC] = 0;

    mem[IFREG] = 0xE1;

    /*
       LCD enabled,
       BG enabled,
       tile data at 8000,
       BG map 9800.
    */

    mem[LCDC] = 0x91;

    mem[STAT] = 0;

    mem[SCY] = 0;
    mem[SCX] = 0;

    mem[LY] = 0;
    mem[LYC] = 0;

    /*
       Game Boy palettes.

       E4 means:

       color 0 -> shade 0
       color 1 -> shade 1
       color 2 -> shade 2
       color 3 -> shade 3
    */

    mem[BGP] = 0xE4;
    mem[OBP0] = 0xE4;
    mem[OBP1] = 0xE4;

    mem[WY] = 0;
    mem[WX] = 7;

    mem[IE] = 0;

    div_counter = 0;
    timer_counter = 0;
    ppu_counter = 0;

    window_line_counter = 0;

    /*
       Controller released.
    */

    joypad.A = 1;
    joypad.B = 1;
    joypad.SELECT = 1;
    joypad.START = 1;

    joypad.RIGHT = 1;
    joypad.LEFT = 1;
    joypad.UP = 1;
    joypad.DOWN = 1;

    /*
       Initial screen.
    */

    for (int i = 0;
         i < SCREEN_W * SCREEN_H;
         i++)
    {
        framebuffer[i] =
            gb_colors[0];
    }
}

/* ============================================================
   MAIN
   ============================================================ */

int main(
    int argc,
    char **argv)
{
    const char *rom_filename =
        "tetris.gb";

    if (argc >= 2)
        rom_filename = argv[1];

    printf(
        "=====================================\n"
        "       GAME BOY EMULATOR\n"
        "=====================================\n"
        "ROM: %s\n\n",
        rom_filename
    );

    if (!load_rom(rom_filename))
        return 1;

    printf(
        "ROM size: %zu bytes\n",
        rom_size
    );

    printf(
        "MBC type: %d\n",
        mbc_type
    );

    emulator_init();

    if (SDL_Init(
            SDL_INIT_VIDEO |
            SDL_INIT_EVENTS) != 0)
    {
        fprintf(
            stderr,
            "SDL initialization failed:\n%s\n",
            SDL_GetError()
        );

        free(rom);

        return 1;
    }

    SDL_SetHint(
        SDL_HINT_RENDER_SCALE_QUALITY,
        "0"
    );

    SDL_Window *window =
        SDL_CreateWindow(
            "Game Boy - Tetris",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            SCREEN_W * WINDOW_SCALE,
            SCREEN_H * WINDOW_SCALE,
            SDL_WINDOW_SHOWN
        );

    if (!window)
    {
        fprintf(
            stderr,
            "Window creation failed:\n%s\n",
            SDL_GetError()
        );

        SDL_Quit();
        free(rom);

        return 1;
    }

    SDL_Renderer *renderer =
        SDL_CreateRenderer(
            window,
            -1,
            SDL_RENDERER_ACCELERATED |
            SDL_RENDERER_PRESENTVSYNC
        );

    if (!renderer)
    {
        renderer =
            SDL_CreateRenderer(
                window,
                -1,
                SDL_RENDERER_SOFTWARE
            );
    }

    if (!renderer)
    {
        fprintf(
            stderr,
            "Renderer creation failed:\n%s\n",
            SDL_GetError()
        );

        SDL_DestroyWindow(window);
        SDL_Quit();
        free(rom);

        return 1;
    }

    SDL_Texture *texture =
        SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,
            SCREEN_W,
            SCREEN_H
        );

    if (!texture)
    {
        fprintf(
            stderr,
            "Texture creation failed:\n%s\n",
            SDL_GetError()
        );

        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        free(rom);

        return 1;
    }

    SDL_RenderSetLogicalSize(
        renderer,
        SCREEN_W,
        SCREEN_H
    );

    int running = 1;

    uint32_t last_frame =
        SDL_GetTicks();

    /*
       Main emulator loop.
    */

    while (running)
    {
        /* ====================================================
           EVENTS
           ==================================================== */

        SDL_Event event;

        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                running = 0;
            }

            if (event.type ==
                SDL_KEYDOWN)
            {
                if (event.key.keysym.sym ==
                    SDLK_ESCAPE)
                {
                    running = 0;
                }
            }
        }

        /*
           IMPORTANT:
           Poll the keyboard continuously.
        */

        SDL_PumpEvents();

        update_keyboard();

        const Uint8 *keys =
            SDL_GetKeyboardState(NULL);

        if (keys[SDL_SCANCODE_ESCAPE])
        {
            running = 0;
            break;
        }

        /* ====================================================
           RUN ONE GAME BOY FRAME
           ==================================================== */

        int frame_cycles = 0;

        while (frame_cycles <
               FRAME_CYCLES &&
               running)
        {
            /*
               Check interrupts first.
            */

            int interrupt_cycles =
                interrupt_step();

            if (interrupt_cycles > 0)
            {
                frame_cycles +=
                    interrupt_cycles;

                timer_step(
                    interrupt_cycles
                );

                ppu_step(
                    interrupt_cycles
                );

                continue;
            }

            int cycles =
                cpu_step();

            if (cycles <= 0)
                cycles = 4;

            frame_cycles += cycles;

            timer_step(cycles);

            ppu_step(cycles);

            /*
               EI becomes active after
               the following instruction.
            */

            if (cpu.ime_delay > 0)
            {
                cpu.ime_delay--;

                if (cpu.ime_delay == 0)
                {
                    cpu.IME = 1;
                }
            }

            /*
               Keep keyboard responsive even
               while the CPU is busy.
            */

            if ((frame_cycles & 0x0FFF) == 0)
            {
                SDL_PumpEvents();
                update_keyboard();
            }
        }

        /* ====================================================
           DRAW
           ==================================================== */

        SDL_UpdateTexture(
            texture,
            NULL,
            framebuffer,
            SCREEN_W *
            sizeof(uint32_t)
        );

        SDL_RenderClear(renderer);

        SDL_RenderCopy(
            renderer,
            texture,
            NULL,
            NULL
        );

        SDL_RenderPresent(renderer);

        /* ====================================================
           FRAME LIMIT
           ==================================================== */

        uint32_t now =
            SDL_GetTicks();

        uint32_t elapsed =
            now - last_frame;

        /*
           Approximately 60 FPS.

           16 ms per frame.
        */

        if (elapsed < 16)
        {
            SDL_Delay(
                (uint32_t)(
                    16 - elapsed
                )
            );
        }

        last_frame =
            SDL_GetTicks();
    }

    /* ========================================================
       CLEANUP
       ======================================================== */

    SDL_DestroyTexture(texture);

    SDL_DestroyRenderer(renderer);

    SDL_DestroyWindow(window);

    SDL_Quit();

    free(rom);

    printf(
        "\nGame Boy emulator closed.\n"
    );

    return 0;
}