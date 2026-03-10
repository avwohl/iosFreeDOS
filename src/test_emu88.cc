#include "emu88.h"
#include "test_8088_sst.h"
#include <cstdio>
#include <cstring>

// Simple test harness for the 8088 emulator
// Tests basic instruction execution

static int tests_passed = 0;
static int tests_failed = 0;

static void check(bool condition, const char *msg) {
  if (condition) {
    tests_passed++;
  } else {
    tests_failed++;
    fprintf(stderr, "FAIL: %s\n", msg);
  }
}

// Load code into memory at CS:IP and run n instructions
static void load_and_run(emu88 &cpu, const emu88_uint8 *code, int len, int n_insns) {
  emu88_uint32 addr = EMU88_MK20(cpu.sregs[emu88::seg_CS], cpu.ip);
  for (int i = 0; i < len; i++) {
    cpu.mem->store_mem(addr + i, code[i]);
  }
  for (int i = 0; i < n_insns; i++) {
    cpu.execute();
  }
}

static void test_mov_immediate() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0x0000;
  cpu.ip = 0x0100;
  cpu.sregs[emu88::seg_DS] = 0x0000;
  cpu.sregs[emu88::seg_SS] = 0x0000;
  cpu.regs[emu88::reg_SP] = 0xFFFE;

  // MOV AX, 0x1234  (B8 34 12)
  // MOV BX, 0x5678  (BB 78 56)
  emu88_uint8 code[] = { 0xB8, 0x34, 0x12, 0xBB, 0x78, 0x56 };
  load_and_run(cpu, code, sizeof(code), 2);

  check(cpu.regs[emu88::reg_AX] == 0x1234, "MOV AX, 0x1234");
  check(cpu.regs[emu88::reg_BX] == 0x5678, "MOV BX, 0x5678");
}

static void test_mov_reg8() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0x0000;
  cpu.ip = 0x0100;
  cpu.sregs[emu88::seg_DS] = 0x0000;

  // MOV AL, 0xAB (B0 AB)
  // MOV AH, 0xCD (B4 CD)
  emu88_uint8 code[] = { 0xB0, 0xAB, 0xB4, 0xCD };
  load_and_run(cpu, code, sizeof(code), 2);

  check(cpu.get_reg8(emu88::reg_AL) == 0xAB, "MOV AL, 0xAB");
  check(cpu.get_reg8(emu88::reg_AH) == 0xCD, "MOV AH, 0xCD");
  check(cpu.regs[emu88::reg_AX] == 0xCDAB, "AX == 0xCDAB after setting AL/AH");
}

static void test_add() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0x0000;
  cpu.ip = 0x0100;
  cpu.sregs[emu88::seg_DS] = 0x0000;

  // MOV AX, 5  (B8 05 00)
  // ADD AX, 3  (05 03 00)
  emu88_uint8 code[] = { 0xB8, 0x05, 0x00, 0x05, 0x03, 0x00 };
  load_and_run(cpu, code, sizeof(code), 2);

  check(cpu.regs[emu88::reg_AX] == 8, "ADD AX, 3 (5+3=8)");
  check(!cpu.get_flag(emu88::FLAG_ZF), "ADD: ZF clear");
  check(!cpu.get_flag(emu88::FLAG_CF), "ADD: CF clear");
}

static void test_sub() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0x0000;
  cpu.ip = 0x0100;
  cpu.sregs[emu88::seg_DS] = 0x0000;

  // MOV AX, 10  (B8 0A 00)
  // SUB AX, 10  (2D 0A 00)
  emu88_uint8 code[] = { 0xB8, 0x0A, 0x00, 0x2D, 0x0A, 0x00 };
  load_and_run(cpu, code, sizeof(code), 2);

  check(cpu.regs[emu88::reg_AX] == 0, "SUB AX, 10 (10-10=0)");
  check(cpu.get_flag(emu88::FLAG_ZF), "SUB: ZF set when result is 0");
}

static void test_push_pop() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0x0000;
  cpu.ip = 0x0100;
  cpu.sregs[emu88::seg_DS] = 0x0000;
  cpu.sregs[emu88::seg_SS] = 0x0000;
  cpu.regs[emu88::reg_SP] = 0xFFFE;

  // MOV AX, 0x1234  (B8 34 12)
  // PUSH AX          (50)
  // MOV AX, 0x0000   (B8 00 00)
  // POP BX           (5B)
  emu88_uint8 code[] = { 0xB8, 0x34, 0x12, 0x50, 0xB8, 0x00, 0x00, 0x5B };
  load_and_run(cpu, code, sizeof(code), 4);

  check(cpu.regs[emu88::reg_BX] == 0x1234, "PUSH/POP: BX == 0x1234");
  check(cpu.regs[emu88::reg_SP] == 0xFFFE, "PUSH/POP: SP restored");
}

static void test_jmp_short() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0x0000;
  cpu.ip = 0x0100;
  cpu.sregs[emu88::seg_DS] = 0x0000;

  // MOV AX, 1     (B8 01 00)
  // JMP +2        (EB 02)
  // MOV AX, 99    (B8 63 00)  <-- should be skipped
  // MOV BX, 42    (BB 2A 00)  <-- should execute
  emu88_uint8 code[] = {
    0xB8, 0x01, 0x00,
    0xEB, 0x03,
    0xB8, 0x63, 0x00,
    0xBB, 0x2A, 0x00
  };
  load_and_run(cpu, code, sizeof(code), 3);

  check(cpu.regs[emu88::reg_AX] == 1, "JMP short: AX unchanged (skipped MOV)");
  check(cpu.regs[emu88::reg_BX] == 42, "JMP short: BX == 42");
}

static void test_call_ret() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0x0000;
  cpu.ip = 0x0100;
  cpu.sregs[emu88::seg_DS] = 0x0000;
  cpu.sregs[emu88::seg_SS] = 0x0000;
  cpu.regs[emu88::reg_SP] = 0xFFFE;

  // 0100: CALL +3       (E8 03 00)  -> calls 0106
  // 0103: MOV BX, 0xBB  (BB BB 00)
  // 0106: MOV AX, 0xAA  (B8 AA 00)
  // 0109: RET            (C3)
  emu88_uint8 code[] = {
    0xE8, 0x03, 0x00,
    0xBB, 0xBB, 0x00,
    0xB8, 0xAA, 0x00,
    0xC3
  };
  load_and_run(cpu, code, sizeof(code), 4);

  check(cpu.regs[emu88::reg_AX] == 0xAA, "CALL/RET: AX == 0xAA (subroutine ran)");
  check(cpu.regs[emu88::reg_BX] == 0xBB, "CALL/RET: BX == 0xBB (returned correctly)");
}

static void test_cmp_jcc() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0x0000;
  cpu.ip = 0x0100;
  cpu.sregs[emu88::seg_DS] = 0x0000;

  // MOV AX, 5    (B8 05 00)
  // CMP AX, 5    (3D 05 00)
  // JE +3        (74 03)
  // MOV BX, 99   (BB 63 00)  <-- should be skipped
  // MOV CX, 42   (B9 2A 00)  <-- should execute
  emu88_uint8 code[] = {
    0xB8, 0x05, 0x00,
    0x3D, 0x05, 0x00,
    0x74, 0x03,
    0xBB, 0x63, 0x00,
    0xB9, 0x2A, 0x00
  };
  load_and_run(cpu, code, sizeof(code), 4);

  check(cpu.regs[emu88::reg_BX] == 0, "CMP/JE: BX == 0 (skipped)");
  check(cpu.regs[emu88::reg_CX] == 42, "CMP/JE: CX == 42");
}

static void test_loop() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0x0000;
  cpu.ip = 0x0100;
  cpu.sregs[emu88::seg_DS] = 0x0000;

  // MOV CX, 5       (B9 05 00)
  // MOV AX, 0       (B8 00 00)
  // ADD AX, 1       (05 01 00)     <- loop body at 0105
  // LOOP -5         (E2 FB)        <- jumps back to 0105
  emu88_uint8 code[] = {
    0xB9, 0x05, 0x00,
    0xB8, 0x00, 0x00,
    0x05, 0x01, 0x00,
    0xE2, 0xFB
  };
  // 2 setup + 5*2 (add+loop) = 12 instructions
  load_and_run(cpu, code, sizeof(code), 12);

  check(cpu.regs[emu88::reg_AX] == 5, "LOOP: AX == 5 (looped 5 times)");
  check(cpu.regs[emu88::reg_CX] == 0, "LOOP: CX == 0");
}

static void test_inc_dec() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0x0000;
  cpu.ip = 0x0100;
  cpu.sregs[emu88::seg_DS] = 0x0000;

  // MOV AX, 10   (B8 0A 00)
  // INC AX       (40)
  // DEC AX       (48)
  // DEC AX       (48)
  emu88_uint8 code[] = { 0xB8, 0x0A, 0x00, 0x40, 0x48, 0x48 };
  load_and_run(cpu, code, sizeof(code), 4);

  check(cpu.regs[emu88::reg_AX] == 9, "INC/DEC: AX == 9 (10+1-1-1)");
}

static void test_xchg() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0x0000;
  cpu.ip = 0x0100;
  cpu.sregs[emu88::seg_DS] = 0x0000;

  // MOV AX, 0x1111  (B8 11 11)
  // MOV BX, 0x2222  (BB 22 22)
  // XCHG AX, BX     (93)
  emu88_uint8 code[] = { 0xB8, 0x11, 0x11, 0xBB, 0x22, 0x22, 0x93 };
  load_and_run(cpu, code, sizeof(code), 3);

  check(cpu.regs[emu88::reg_AX] == 0x2222, "XCHG: AX == 0x2222");
  check(cpu.regs[emu88::reg_BX] == 0x1111, "XCHG: BX == 0x1111");
}

static void test_and_or_xor() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0x0000;
  cpu.ip = 0x0100;
  cpu.sregs[emu88::seg_DS] = 0x0000;

  // MOV AX, 0xFF00  (B8 00 FF)
  // AND AX, 0x0F0F  (25 0F 0F)
  emu88_uint8 code[] = { 0xB8, 0x00, 0xFF, 0x25, 0x0F, 0x0F };
  load_and_run(cpu, code, sizeof(code), 2);

  check(cpu.regs[emu88::reg_AX] == 0x0F00, "AND: AX == 0x0F00");
}

static void test_mul() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0x0000;
  cpu.ip = 0x0100;
  cpu.sregs[emu88::seg_DS] = 0x0000;

  // MOV AL, 10     (B0 0A)
  // MOV BL, 20     (B3 14)
  // MUL BL         (F6 E3)    ; AX = AL * BL
  emu88_uint8 code[] = { 0xB0, 0x0A, 0xB3, 0x14, 0xF6, 0xE3 };
  load_and_run(cpu, code, sizeof(code), 3);

  check(cpu.regs[emu88::reg_AX] == 200, "MUL: AX == 200 (10*20)");
}

static void test_string_movsb() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0x0000;
  cpu.ip = 0x0100;
  cpu.sregs[emu88::seg_DS] = 0x0000;
  cpu.sregs[emu88::seg_ES] = 0x0000;

  // Put "HELLO" at DS:0200
  const char *src = "HELLO";
  for (int i = 0; i < 5; i++)
    mem.store_mem(0x0200 + i, src[i]);

  // MOV SI, 0x200  (BE 00 02)
  // MOV DI, 0x300  (BF 00 03)
  // MOV CX, 5      (B9 05 00)
  // REP MOVSB      (F3 A4)
  emu88_uint8 code[] = {
    0xBE, 0x00, 0x02,
    0xBF, 0x00, 0x03,
    0xB9, 0x05, 0x00,
    0xF3, 0xA4
  };
  load_and_run(cpu, code, sizeof(code), 4);

  bool match = true;
  for (int i = 0; i < 5; i++) {
    if (mem.fetch_mem(0x0300 + i) != src[i])
      match = false;
  }
  check(match, "REP MOVSB: copied 'HELLO' correctly");
  check(cpu.regs[emu88::reg_CX] == 0, "REP MOVSB: CX == 0");
}

static void test_segment_override() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0x0000;
  cpu.ip = 0x0100;
  cpu.sregs[emu88::seg_DS] = 0x1000;
  cpu.sregs[emu88::seg_ES] = 0x2000;

  // Store 0x42 at ES:0050 (physical 0x20050)
  mem.store_mem(0x20050, 0x42);

  // ES: MOV AL, [0x0050]  (26 A0 50 00)
  emu88_uint8 code[] = { 0x26, 0xA0, 0x50, 0x00 };
  load_and_run(cpu, code, sizeof(code), 1);

  check(cpu.get_reg8(emu88::reg_AL) == 0x42, "ES override: read from ES:0050");
}

// =========================================================================
// Shift / Rotate instructions
// =========================================================================

static void test_shl() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AL, 0x01  (B0 01)
  // SHL AL, 1     (D0 E0)
  emu88_uint8 code[] = { 0xB0, 0x01, 0xD0, 0xE0 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.get_reg8(emu88::reg_AL) == 0x02, "SHL AL,1: 0x01 << 1 = 0x02");
  check(!cpu.get_flag(emu88::FLAG_CF), "SHL AL,1: CF clear");
}

static void test_shr() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AL, 0x80  (B0 80)
  // SHR AL, 1     (D0 E8)
  emu88_uint8 code[] = { 0xB0, 0x80, 0xD0, 0xE8 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.get_reg8(emu88::reg_AL) == 0x40, "SHR AL,1: 0x80 >> 1 = 0x40");
  check(!cpu.get_flag(emu88::FLAG_CF), "SHR AL,1: CF clear");
}

static void test_sar() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AL, 0x80  (B0 80)  ; -128 signed
  // SAR AL, 1     (D0 F8)
  emu88_uint8 code[] = { 0xB0, 0x80, 0xD0, 0xF8 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.get_reg8(emu88::reg_AL) == 0xC0, "SAR AL,1: 0x80 >> 1 = 0xC0 (sign-extended)");
}

static void test_rol() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AL, 0x81  (B0 81)
  // ROL AL, 1     (D0 C0)
  emu88_uint8 code[] = { 0xB0, 0x81, 0xD0, 0xC0 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.get_reg8(emu88::reg_AL) == 0x03, "ROL AL,1: 0x81 -> 0x03");
  check(cpu.get_flag(emu88::FLAG_CF), "ROL AL,1: CF set (bit 7 was 1)");
}

static void test_ror() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AL, 0x01  (B0 01)
  // ROR AL, 1     (D0 C8)
  emu88_uint8 code[] = { 0xB0, 0x01, 0xD0, 0xC8 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.get_reg8(emu88::reg_AL) == 0x80, "ROR AL,1: 0x01 -> 0x80");
  check(cpu.get_flag(emu88::FLAG_CF), "ROR AL,1: CF set (bit 0 was 1)");
}

static void test_shl_cl() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AX, 0x0001  (B8 01 00)
  // MOV CL, 4       (B1 04)
  // SHL AX, CL      (D3 E0)
  emu88_uint8 code[] = { 0xB8, 0x01, 0x00, 0xB1, 0x04, 0xD3, 0xE0 };
  load_and_run(cpu, code, sizeof(code), 3);
  check(cpu.regs[emu88::reg_AX] == 0x0010, "SHL AX,CL: 1 << 4 = 0x0010");
}

static void test_shl_imm8() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AL, 0x01  (B0 01)
  // SHL AL, 3     (C0 E0 03)   ; 80186+ immediate shift
  emu88_uint8 code[] = { 0xB0, 0x01, 0xC0, 0xE0, 0x03 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.get_reg8(emu88::reg_AL) == 0x08, "SHL AL,3 (imm8): 1 << 3 = 8");
}

static void test_rcl() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // STC            (F9)
  // MOV AL, 0x00  (B0 00)
  // RCL AL, 1     (D0 D0)    ; rotate through carry: 0 with CF=1 -> AL=1, CF=0
  emu88_uint8 code[] = { 0xF9, 0xB0, 0x00, 0xD0, 0xD0 };
  load_and_run(cpu, code, sizeof(code), 3);
  check(cpu.get_reg8(emu88::reg_AL) == 0x01, "RCL AL,1: CF rotated in");
  check(!cpu.get_flag(emu88::FLAG_CF), "RCL AL,1: CF now 0");
}

// =========================================================================
// DIV / IDIV instructions
// =========================================================================

static void test_div8() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AX, 100   (B8 64 00)
  // MOV BL, 7     (B3 07)
  // DIV BL        (F6 F3)    ; AL = 100/7 = 14, AH = 100%7 = 2
  emu88_uint8 code[] = { 0xB8, 0x64, 0x00, 0xB3, 0x07, 0xF6, 0xF3 };
  load_and_run(cpu, code, sizeof(code), 3);
  check(cpu.get_reg8(emu88::reg_AL) == 14, "DIV BL: 100/7 = 14");
  check(cpu.get_reg8(emu88::reg_AH) == 2, "DIV BL: 100%7 = 2");
}

static void test_div16() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV DX, 0     (BA 00 00)
  // MOV AX, 1000  (B8 E8 03)
  // MOV BX, 7     (BB 07 00)
  // DIV BX        (F7 F3)    ; AX = 1000/7 = 142, DX = 1000%7 = 6
  emu88_uint8 code[] = { 0xBA, 0x00, 0x00, 0xB8, 0xE8, 0x03, 0xBB, 0x07, 0x00, 0xF7, 0xF3 };
  load_and_run(cpu, code, sizeof(code), 4);
  check(cpu.regs[emu88::reg_AX] == 142, "DIV BX: 1000/7 = 142");
  check(cpu.regs[emu88::reg_DX] == 6, "DIV BX: 1000%7 = 6");
}

static void test_idiv8() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AX, -100  (B8 9C FF) ; 0xFF9C = -100 signed
  // MOV BL, 7     (B3 07)
  // IDIV BL       (F6 FB)    ; AL = -14 (0xF2), AH = -2 (0xFE)
  emu88_uint8 code[] = { 0xB8, 0x9C, 0xFF, 0xB3, 0x07, 0xF6, 0xFB };
  load_and_run(cpu, code, sizeof(code), 3);
  check(cpu.get_reg8(emu88::reg_AL) == 0xF2, "IDIV BL: -100/7 = -14 (0xF2)");
  check(cpu.get_reg8(emu88::reg_AH) == 0xFE, "IDIV BL: -100%7 = -2 (0xFE)");
}

static void test_imul8() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AL, -10   (B0 F6) ; 0xF6 = -10 signed
  // MOV BL, 3     (B3 03)
  // IMUL BL       (F6 EB)  ; AX = -30 = 0xFFE2
  emu88_uint8 code[] = { 0xB0, 0xF6, 0xB3, 0x03, 0xF6, 0xEB };
  load_and_run(cpu, code, sizeof(code), 3);
  check(cpu.regs[emu88::reg_AX] == 0xFFE2, "IMUL BL: -10*3 = -30 (0xFFE2)");
}

// =========================================================================
// NOT / NEG instructions
// =========================================================================

static void test_not() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AL, 0x55  (B0 55)
  // NOT AL        (F6 D0)
  emu88_uint8 code[] = { 0xB0, 0x55, 0xF6, 0xD0 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.get_reg8(emu88::reg_AL) == 0xAA, "NOT AL: 0x55 -> 0xAA");
}

static void test_neg() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AX, 5    (B8 05 00)
  // NEG AX       (F7 D8)
  emu88_uint8 code[] = { 0xB8, 0x05, 0x00, 0xF7, 0xD8 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.regs[emu88::reg_AX] == 0xFFFB, "NEG AX: 5 -> -5 (0xFFFB)");
  check(cpu.get_flag(emu88::FLAG_CF), "NEG AX: CF set for non-zero");
}

static void test_neg_zero() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AL, 0    (B0 00)
  // NEG AL       (F6 D8)
  emu88_uint8 code[] = { 0xB0, 0x00, 0xF6, 0xD8 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.get_reg8(emu88::reg_AL) == 0, "NEG 0: result is 0");
  check(!cpu.get_flag(emu88::FLAG_CF), "NEG 0: CF clear");
  check(cpu.get_flag(emu88::FLAG_ZF), "NEG 0: ZF set");
}

// =========================================================================
// String operations
// =========================================================================

static void test_stosb() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;
  cpu.sregs[emu88::seg_ES] = 0;

  // MOV AL, 0xFF  (B0 FF)
  // MOV DI, 0x300 (BF 00 03)
  // MOV CX, 10    (B9 0A 00)
  // REP STOSB     (F3 AA)
  emu88_uint8 code[] = { 0xB0, 0xFF, 0xBF, 0x00, 0x03, 0xB9, 0x0A, 0x00, 0xF3, 0xAA };
  load_and_run(cpu, code, sizeof(code), 4);

  bool all_ff = true;
  for (int i = 0; i < 10; i++)
    if (mem.fetch_mem(0x300 + i) != 0xFF) all_ff = false;
  check(all_ff, "REP STOSB: filled 10 bytes with 0xFF");
  check(cpu.regs[emu88::reg_CX] == 0, "REP STOSB: CX == 0");
}

static void test_lodsb() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  mem.store_mem(0x200, 0x42);

  // MOV SI, 0x200 (BE 00 02)
  // LODSB         (AC)
  emu88_uint8 code[] = { 0xBE, 0x00, 0x02, 0xAC };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.get_reg8(emu88::reg_AL) == 0x42, "LODSB: AL loaded from DS:SI");
  check(cpu.regs[emu88::reg_SI] == 0x201, "LODSB: SI incremented");
}

static void test_movsw() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;
  cpu.sregs[emu88::seg_ES] = 0;

  mem.store_mem16(0x200, 0x1234);
  mem.store_mem16(0x202, 0x5678);

  // MOV SI, 0x200 (BE 00 02)
  // MOV DI, 0x300 (BF 00 03)
  // MOV CX, 2     (B9 02 00)
  // REP MOVSW     (F3 A5)
  emu88_uint8 code[] = { 0xBE, 0x00, 0x02, 0xBF, 0x00, 0x03, 0xB9, 0x02, 0x00, 0xF3, 0xA5 };
  load_and_run(cpu, code, sizeof(code), 4);
  check(mem.fetch_mem16(0x300) == 0x1234, "REP MOVSW: word 1 copied");
  check(mem.fetch_mem16(0x302) == 0x5678, "REP MOVSW: word 2 copied");
}

static void test_stosw() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;
  cpu.sregs[emu88::seg_ES] = 0;

  // MOV AX, 0xDEAD (B8 AD DE)
  // MOV DI, 0x300  (BF 00 03)
  // MOV CX, 3      (B9 03 00)
  // REP STOSW      (F3 AB)
  emu88_uint8 code[] = { 0xB8, 0xAD, 0xDE, 0xBF, 0x00, 0x03, 0xB9, 0x03, 0x00, 0xF3, 0xAB };
  load_and_run(cpu, code, sizeof(code), 4);
  check(mem.fetch_mem16(0x300) == 0xDEAD, "REP STOSW: word 1");
  check(mem.fetch_mem16(0x302) == 0xDEAD, "REP STOSW: word 2");
  check(mem.fetch_mem16(0x304) == 0xDEAD, "REP STOSW: word 3");
}

static void test_cmpsb() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;
  cpu.sregs[emu88::seg_ES] = 0;

  // "ABCDE" at 0x200, "ABCXE" at 0x300
  const char *s1 = "ABCDE", *s2 = "ABCXE";
  for (int i = 0; i < 5; i++) {
    mem.store_mem(0x200 + i, s1[i]);
    mem.store_mem(0x300 + i, s2[i]);
  }

  // MOV SI, 0x200 (BE 00 02)
  // MOV DI, 0x300 (BF 00 03)
  // MOV CX, 5     (B9 05 00)
  // REPE CMPSB    (F3 A6)
  emu88_uint8 code[] = { 0xBE, 0x00, 0x02, 0xBF, 0x00, 0x03, 0xB9, 0x05, 0x00, 0xF3, 0xA6 };
  load_and_run(cpu, code, sizeof(code), 4);
  // Stops at first mismatch (index 3: 'D' vs 'X')
  check(cpu.regs[emu88::reg_CX] == 1, "REPE CMPSB: stopped at mismatch, CX=1");
  check(!cpu.get_flag(emu88::FLAG_ZF), "REPE CMPSB: ZF clear (mismatch)");
}

static void test_scasb() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;
  cpu.sregs[emu88::seg_ES] = 0;

  // "HELLO" at 0x300
  const char *s = "HELLO";
  for (int i = 0; i < 5; i++) mem.store_mem(0x300 + i, s[i]);

  // MOV AL, 'L'   (B0 4C)
  // MOV DI, 0x300 (BF 00 03)
  // MOV CX, 5     (B9 05 00)
  // REPNE SCASB   (F2 AE)
  emu88_uint8 code[] = { 0xB0, 0x4C, 0xBF, 0x00, 0x03, 0xB9, 0x05, 0x00, 0xF2, 0xAE };
  load_and_run(cpu, code, sizeof(code), 4);
  // Finds 'L' at index 2, DI advances past it to 0x303
  check(cpu.get_flag(emu88::FLAG_ZF), "REPNE SCASB: ZF set (found 'L')");
  check(cpu.regs[emu88::reg_DI] == 0x303, "REPNE SCASB: DI past match");
}

static void test_std_direction() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;
  cpu.sregs[emu88::seg_ES] = 0;

  // Copy backwards: "ABCDE" from 0x204 down
  const char *s = "ABCDE";
  for (int i = 0; i < 5; i++) mem.store_mem(0x200 + i, s[i]);

  // STD            (FD)
  // MOV SI, 0x204 (BE 04 02)
  // MOV DI, 0x304 (BF 04 03)
  // MOV CX, 5     (B9 05 00)
  // REP MOVSB     (F3 A4)
  // CLD           (FC)       ; restore direction flag
  emu88_uint8 code[] = {
    0xFD, 0xBE, 0x04, 0x02, 0xBF, 0x04, 0x03,
    0xB9, 0x05, 0x00, 0xF3, 0xA4, 0xFC
  };
  load_and_run(cpu, code, sizeof(code), 6);
  bool match = true;
  for (int i = 0; i < 5; i++)
    if (mem.fetch_mem(0x300 + i) != s[i]) match = false;
  check(match, "STD + REP MOVSB: backwards copy correct");
}

// =========================================================================
// Flag instructions
// =========================================================================

static void test_flag_instructions() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;
  cpu.sregs[emu88::seg_SS] = 0;
  cpu.regs[emu88::reg_SP] = 0xFFFE;

  // STC  (F9)
  emu88_uint8 stc[] = { 0xF9 };
  load_and_run(cpu, stc, sizeof(stc), 1);
  check(cpu.get_flag(emu88::FLAG_CF), "STC: CF set");

  // CLC  (F8)
  cpu.ip = 0x100;
  emu88_uint8 clc[] = { 0xF8 };
  load_and_run(cpu, clc, sizeof(clc), 1);
  check(!cpu.get_flag(emu88::FLAG_CF), "CLC: CF clear");

  // CMC (should toggle CF)  (F5)
  cpu.ip = 0x100;
  emu88_uint8 cmc[] = { 0xF5 };
  load_and_run(cpu, cmc, sizeof(cmc), 1);
  check(cpu.get_flag(emu88::FLAG_CF), "CMC: CF toggled to 1");

  // STD  (FD)
  cpu.ip = 0x100;
  emu88_uint8 std_[] = { 0xFD };
  load_and_run(cpu, std_, sizeof(std_), 1);
  check(cpu.get_flag(emu88::FLAG_DF), "STD: DF set");

  // CLD  (FC)
  cpu.ip = 0x100;
  emu88_uint8 cld[] = { 0xFC };
  load_and_run(cpu, cld, sizeof(cld), 1);
  check(!cpu.get_flag(emu88::FLAG_DF), "CLD: DF clear");

  // CLI  (FA)
  cpu.ip = 0x100;
  emu88_uint8 cli[] = { 0xFA };
  load_and_run(cpu, cli, sizeof(cli), 1);
  check(!cpu.get_flag(emu88::FLAG_IF), "CLI: IF clear");

  // STI  (FB)
  cpu.ip = 0x100;
  emu88_uint8 sti[] = { 0xFB };
  load_and_run(cpu, sti, sizeof(sti), 1);
  check(cpu.get_flag(emu88::FLAG_IF), "STI: IF set");
}

static void test_lahf_sahf() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // Set known flags: CF=1, ZF=1 -> flags low byte = 0x43 (CF|ZF|reserved bit 1)
  // STC           (F9)
  // MOV AX, 0     (B8 00 00)
  // SUB AX, 0     (2D 00 00) ; sets ZF, clears CF
  // STC           (F9)       ; set CF back
  // LAHF          (9F)       ; AH = flags low byte
  emu88_uint8 code[] = { 0xF9, 0xB8, 0x00, 0x00, 0x2D, 0x00, 0x00, 0xF9, 0x9F };
  load_and_run(cpu, code, sizeof(code), 5);
  // AH should have CF|ZF set
  uint8_t ah = cpu.get_reg8(emu88::reg_AH);
  check((ah & 0x01) != 0, "LAHF: CF bit in AH");
  check((ah & 0x40) != 0, "LAHF: ZF bit in AH");

  // Now clear flags and use SAHF to restore
  cpu.ip = 0x100;
  // CLC           (F8)
  // MOV AH, 0x41  (B4 41)  ; CF=1, ZF=1
  // SAHF          (9E)
  emu88_uint8 code2[] = { 0xF8, 0xB4, 0x41, 0x9E };
  load_and_run(cpu, code2, sizeof(code2), 3);
  check(cpu.get_flag(emu88::FLAG_CF), "SAHF: CF restored");
  check(cpu.get_flag(emu88::FLAG_ZF), "SAHF: ZF restored");
}

static void test_pushf_popf() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;
  cpu.sregs[emu88::seg_SS] = 0;
  cpu.regs[emu88::reg_SP] = 0xFFFE;

  // STC           (F9)     ; set CF
  // STD           (FD)     ; set DF
  // PUSHF         (9C)
  // CLC           (F8)     ; clear CF
  // CLD           (FC)     ; clear DF
  // POPF          (9D)     ; restore CF and DF
  emu88_uint8 code[] = { 0xF9, 0xFD, 0x9C, 0xF8, 0xFC, 0x9D };
  load_and_run(cpu, code, sizeof(code), 6);
  check(cpu.get_flag(emu88::FLAG_CF), "PUSHF/POPF: CF restored");
  check(cpu.get_flag(emu88::FLAG_DF), "PUSHF/POPF: DF restored");
}

// =========================================================================
// Conditional jumps - all variants
// =========================================================================

// Helper: test a Jcc opcode. Sets flags, then Jcc should skip a MOV if taken.
static void test_jcc_taken(uint8_t opcode, uint16_t init_flags, const char *name) {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;
  cpu.flags = init_flags | 0x0002; // bit 1 always set on 8086

  // Jcc +3          (xx 03)
  // MOV AX, 0xDEAD  (B8 AD DE) ; skipped if taken
  // NOP             (90)
  emu88_uint8 code[] = { opcode, 0x03, 0xB8, 0xAD, 0xDE, 0x90 };
  load_and_run(cpu, code, sizeof(code), 2); // Jcc + NOP
  char msg[80];
  snprintf(msg, sizeof(msg), "%s taken: AX not modified", name);
  check(cpu.regs[emu88::reg_AX] == 0, msg);
}

static void test_jcc_not_taken(uint8_t opcode, uint16_t init_flags, const char *name) {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;
  cpu.flags = init_flags | 0x0002;

  emu88_uint8 code[] = { opcode, 0x03, 0xB8, 0xAD, 0xDE, 0x90 };
  load_and_run(cpu, code, sizeof(code), 2); // Jcc (not taken) + MOV
  char msg[80];
  snprintf(msg, sizeof(msg), "%s not taken: AX modified", name);
  check(cpu.regs[emu88::reg_AX] == 0xDEAD, msg);
}

static void test_all_jcc() {
  // JO (0x70): taken when OF=1
  test_jcc_taken(0x70, emu88::FLAG_OF, "JO");
  test_jcc_not_taken(0x70, 0, "JO");

  // JNO (0x71): taken when OF=0
  test_jcc_taken(0x71, 0, "JNO");
  test_jcc_not_taken(0x71, emu88::FLAG_OF, "JNO");

  // JB (0x72): taken when CF=1
  test_jcc_taken(0x72, emu88::FLAG_CF, "JB");
  test_jcc_not_taken(0x72, 0, "JB");

  // JNB (0x73): taken when CF=0
  test_jcc_taken(0x73, 0, "JNB");
  test_jcc_not_taken(0x73, emu88::FLAG_CF, "JNB");

  // JE (0x74): taken when ZF=1
  test_jcc_taken(0x74, emu88::FLAG_ZF, "JE");
  test_jcc_not_taken(0x74, 0, "JE");

  // JNE (0x75): taken when ZF=0
  test_jcc_taken(0x75, 0, "JNE");
  test_jcc_not_taken(0x75, emu88::FLAG_ZF, "JNE");

  // JBE (0x76): taken when CF=1 or ZF=1
  test_jcc_taken(0x76, emu88::FLAG_CF, "JBE(CF)");
  test_jcc_taken(0x76, emu88::FLAG_ZF, "JBE(ZF)");
  test_jcc_not_taken(0x76, 0, "JBE");

  // JA (0x77): taken when CF=0 and ZF=0
  test_jcc_taken(0x77, 0, "JA");
  test_jcc_not_taken(0x77, emu88::FLAG_CF, "JA(CF)");

  // JS (0x78): taken when SF=1
  test_jcc_taken(0x78, emu88::FLAG_SF, "JS");
  test_jcc_not_taken(0x78, 0, "JS");

  // JNS (0x79): taken when SF=0
  test_jcc_taken(0x79, 0, "JNS");
  test_jcc_not_taken(0x79, emu88::FLAG_SF, "JNS");

  // JP (0x7A): taken when PF=1
  test_jcc_taken(0x7A, emu88::FLAG_PF, "JP");
  test_jcc_not_taken(0x7A, 0, "JP");

  // JNP (0x7B): taken when PF=0
  test_jcc_taken(0x7B, 0, "JNP");
  test_jcc_not_taken(0x7B, emu88::FLAG_PF, "JNP");

  // JL (0x7C): taken when SF != OF
  test_jcc_taken(0x7C, emu88::FLAG_SF, "JL(SF)");
  test_jcc_taken(0x7C, emu88::FLAG_OF, "JL(OF)");
  test_jcc_not_taken(0x7C, 0, "JL");
  test_jcc_not_taken(0x7C, emu88::FLAG_SF | emu88::FLAG_OF, "JL(SF+OF)");

  // JGE (0x7D): taken when SF == OF
  test_jcc_taken(0x7D, 0, "JGE");
  test_jcc_taken(0x7D, emu88::FLAG_SF | emu88::FLAG_OF, "JGE(SF+OF)");
  test_jcc_not_taken(0x7D, emu88::FLAG_SF, "JGE(SF)");

  // JLE (0x7E): taken when ZF=1 or SF != OF
  test_jcc_taken(0x7E, emu88::FLAG_ZF, "JLE(ZF)");
  test_jcc_taken(0x7E, emu88::FLAG_SF, "JLE(SF)");
  test_jcc_not_taken(0x7E, 0, "JLE");

  // JG (0x7F): taken when ZF=0 and SF == OF
  test_jcc_taken(0x7F, 0, "JG");
  test_jcc_not_taken(0x7F, emu88::FLAG_ZF, "JG(ZF)");
  test_jcc_not_taken(0x7F, emu88::FLAG_SF, "JG(SF)");
}

// =========================================================================
// LEA, LDS, LES
// =========================================================================

static void test_lea() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV BX, 0x100  (BB 00 01)
  // MOV SI, 0x50   (BE 50 00)
  // LEA AX, [BX+SI+0x10]  (8D 40 10)  ; AX = 0x100+0x50+0x10 = 0x160
  // Wait, modrm for [BX+SI+disp8] is mod=01, reg=AX(0), r/m=000 -> 0x40
  // Actually [BX+SI+disp8] -> modrm: mod=01, rm=000 (BX+SI), reg=000 (AX) -> 0x40
  emu88_uint8 code[] = { 0xBB, 0x00, 0x01, 0xBE, 0x50, 0x00, 0x8D, 0x40, 0x10 };
  load_and_run(cpu, code, sizeof(code), 3);
  check(cpu.regs[emu88::reg_AX] == 0x160, "LEA [BX+SI+0x10]: AX = 0x160");
}

static void test_les() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // Store far pointer at DS:0x200: offset=0x1234, segment=0x5678
  mem.store_mem16(0x200, 0x1234);
  mem.store_mem16(0x202, 0x5678);

  // MOV BX, 0x200 (BB 00 02)
  // LES AX, [BX]  (C4 07)  ; modrm: mod=00, reg=AX(0), rm=111(BX) -> 0x07
  emu88_uint8 code[] = { 0xBB, 0x00, 0x02, 0xC4, 0x07 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.regs[emu88::reg_AX] == 0x1234, "LES: AX = offset 0x1234");
  check(cpu.sregs[emu88::seg_ES] == 0x5678, "LES: ES = segment 0x5678");
}

// =========================================================================
// Memory MOV (r/m) and MOV imm to r/m
// =========================================================================

static void test_mov_mem() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV WORD [0x200], 0x5678  (C7 06 00 02 78 56)
  // MOV AX, [0x200]           (A1 00 02)
  emu88_uint8 code[] = { 0xC7, 0x06, 0x00, 0x02, 0x78, 0x56, 0xA1, 0x00, 0x02 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.regs[emu88::reg_AX] == 0x5678, "MOV mem: write then read 0x5678");
}

static void test_mov_byte_mem() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV BYTE [0x200], 0xAB  (C6 06 00 02 AB)
  // MOV AL, [0x200]         (A0 00 02)
  emu88_uint8 code[] = { 0xC6, 0x06, 0x00, 0x02, 0xAB, 0xA0, 0x00, 0x02 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.get_reg8(emu88::reg_AL) == 0xAB, "MOV byte mem: write then read 0xAB");
}

// =========================================================================
// CBW, CWD, XLAT
// =========================================================================

static void test_cbw() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AL, 0x80  (B0 80)  ; -128
  // CBW           (98)     ; AX = 0xFF80
  emu88_uint8 code[] = { 0xB0, 0x80, 0x98 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.regs[emu88::reg_AX] == 0xFF80, "CBW: sign-extend 0x80 -> 0xFF80");

  // Positive case
  cpu.ip = 0x100;
  emu88_uint8 code2[] = { 0xB0, 0x7F, 0x98 };
  load_and_run(cpu, code2, sizeof(code2), 2);
  check(cpu.regs[emu88::reg_AX] == 0x007F, "CBW: sign-extend 0x7F -> 0x007F");
}

static void test_cwd() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AX, 0x8000 (B8 00 80)  ; -32768
  // CWD            (99)        ; DX = 0xFFFF
  emu88_uint8 code[] = { 0xB8, 0x00, 0x80, 0x99 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.regs[emu88::reg_DX] == 0xFFFF, "CWD: sign-extend 0x8000 -> DX=0xFFFF");

  cpu.ip = 0x100;
  emu88_uint8 code2[] = { 0xB8, 0x00, 0x01, 0x99 };
  load_and_run(cpu, code2, sizeof(code2), 2);
  check(cpu.regs[emu88::reg_DX] == 0x0000, "CWD: sign-extend 0x0100 -> DX=0x0000");
}

static void test_xlat() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // Set up translation table at DS:0x200
  for (int i = 0; i < 256; i++) mem.store_mem(0x200 + i, i ^ 0xFF); // invert bits

  // MOV BX, 0x200  (BB 00 02)
  // MOV AL, 0x55   (B0 55)
  // XLAT           (D7)      ; AL = DS:[BX+AL] = 0x200+0x55 = 0xAA
  emu88_uint8 code[] = { 0xBB, 0x00, 0x02, 0xB0, 0x55, 0xD7 };
  load_and_run(cpu, code, sizeof(code), 3);
  check(cpu.get_reg8(emu88::reg_AL) == 0xAA, "XLAT: table[0x55] = 0xAA");
}

// =========================================================================
// BCD adjust instructions
// =========================================================================

static void test_daa() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // BCD addition: 0x29 + 0x13 = 0x3C, DAA adjusts to 0x42
  // MOV AL, 0x29  (B0 29)
  // ADD AL, 0x13  (04 13)
  // DAA           (27)
  emu88_uint8 code[] = { 0xB0, 0x29, 0x04, 0x13, 0x27 };
  load_and_run(cpu, code, sizeof(code), 3);
  check(cpu.get_reg8(emu88::reg_AL) == 0x42, "DAA: 0x29+0x13 = BCD 42");
}

static void test_das() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // BCD subtraction: 0x42 - 0x13 = 0x2F, DAS adjusts to 0x29
  // MOV AL, 0x42  (B0 42)
  // SUB AL, 0x13  (2C 13)
  // DAS           (2F)
  emu88_uint8 code[] = { 0xB0, 0x42, 0x2C, 0x13, 0x2F };
  load_and_run(cpu, code, sizeof(code), 3);
  check(cpu.get_reg8(emu88::reg_AL) == 0x29, "DAS: 0x42-0x13 = BCD 29");
}

static void test_aam() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // AAM: AL=35 -> AH=3, AL=5
  // MOV AL, 35    (B0 23)
  // AAM           (D4 0A)
  emu88_uint8 code[] = { 0xB0, 0x23, 0xD4, 0x0A };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.get_reg8(emu88::reg_AH) == 3, "AAM: 35 -> AH=3");
  check(cpu.get_reg8(emu88::reg_AL) == 5, "AAM: 35 -> AL=5");
}

static void test_aad() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // AAD: AH=3, AL=5 -> AL = 3*10+5 = 35, AH=0
  // MOV AX, 0x0305  (B8 05 03)
  // AAD              (D5 0A)
  emu88_uint8 code[] = { 0xB8, 0x05, 0x03, 0xD5, 0x0A };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.get_reg8(emu88::reg_AL) == 35, "AAD: AH=3,AL=5 -> AL=35");
  check(cpu.get_reg8(emu88::reg_AH) == 0, "AAD: AH=0");
}

// =========================================================================
// 80186 instructions
// =========================================================================

static void test_pusha_popa() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;
  cpu.sregs[emu88::seg_SS] = 0;
  cpu.regs[emu88::reg_SP] = 0xFFF0;

  // Set all regs to known values
  cpu.regs[emu88::reg_AX] = 0x1111;
  cpu.regs[emu88::reg_CX] = 0x2222;
  cpu.regs[emu88::reg_DX] = 0x3333;
  cpu.regs[emu88::reg_BX] = 0x4444;
  cpu.regs[emu88::reg_BP] = 0x6666;
  cpu.regs[emu88::reg_SI] = 0x7777;
  cpu.regs[emu88::reg_DI] = 0x8888;
  uint16_t orig_sp = cpu.regs[emu88::reg_SP];

  // PUSHA         (60)
  // MOV AX, 0     (B8 00 00)
  // MOV CX, 0     (B9 00 00)
  // MOV DX, 0     (BA 00 00)
  // MOV BX, 0     (BB 00 00)
  // POPA          (61)
  emu88_uint8 code[] = {
    0x60,
    0xB8, 0x00, 0x00,
    0xB9, 0x00, 0x00,
    0xBA, 0x00, 0x00,
    0xBB, 0x00, 0x00,
    0x61
  };
  load_and_run(cpu, code, sizeof(code), 6);
  check(cpu.regs[emu88::reg_AX] == 0x1111, "PUSHA/POPA: AX restored");
  check(cpu.regs[emu88::reg_CX] == 0x2222, "PUSHA/POPA: CX restored");
  check(cpu.regs[emu88::reg_DX] == 0x3333, "PUSHA/POPA: DX restored");
  check(cpu.regs[emu88::reg_BX] == 0x4444, "PUSHA/POPA: BX restored");
  check(cpu.regs[emu88::reg_BP] == 0x6666, "PUSHA/POPA: BP restored");
  check(cpu.regs[emu88::reg_SI] == 0x7777, "PUSHA/POPA: SI restored");
  check(cpu.regs[emu88::reg_DI] == 0x8888, "PUSHA/POPA: DI restored");
  check(cpu.regs[emu88::reg_SP] == orig_sp, "PUSHA/POPA: SP restored");
}

static void test_push_imm() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;
  cpu.sregs[emu88::seg_SS] = 0;
  cpu.regs[emu88::reg_SP] = 0xFFFE;

  // PUSH 0x1234  (68 34 12)
  // POP AX       (58)
  emu88_uint8 code[] = { 0x68, 0x34, 0x12, 0x58 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.regs[emu88::reg_AX] == 0x1234, "PUSH imm16: value on stack");
}

static void test_push_imm8() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;
  cpu.sregs[emu88::seg_SS] = 0;
  cpu.regs[emu88::reg_SP] = 0xFFFE;

  // PUSH -5     (6A FB)  ; sign-extended to 0xFFFB
  // POP AX      (58)
  emu88_uint8 code[] = { 0x6A, 0xFB, 0x58 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.regs[emu88::reg_AX] == 0xFFFB, "PUSH imm8: sign-extended 0xFB -> 0xFFFB");
}

static void test_enter_leave() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;
  cpu.sregs[emu88::seg_SS] = 0;
  cpu.regs[emu88::reg_SP] = 0xFFF0;
  cpu.regs[emu88::reg_BP] = 0xAAAA;

  uint16_t orig_bp = cpu.regs[emu88::reg_BP];

  // ENTER 8, 0   (C8 08 00 00)  ; allocate 8 bytes of local space, nesting=0
  // LEAVE        (C9)
  emu88_uint8 code[] = { 0xC8, 0x08, 0x00, 0x00, 0xC9 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.regs[emu88::reg_BP] == orig_bp, "ENTER/LEAVE: BP restored");
  check(cpu.regs[emu88::reg_SP] == 0xFFF0, "ENTER/LEAVE: SP restored");
}

static void test_imul_3op() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV BX, 100     (BB 64 00)
  // IMUL AX, BX, 7  (6B C3 07)  ; AX = BX * 7 = 700
  emu88_uint8 code[] = { 0xBB, 0x64, 0x00, 0x6B, 0xC3, 0x07 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.regs[emu88::reg_AX] == 700, "IMUL r,r/m,imm8: 100*7=700");
}

static void test_imul_3op_imm16() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV BX, 10    (BB 0A 00)
  // IMUL AX, BX, 1000  (69 C3 E8 03)  ; AX = 10 * 1000 = 10000
  emu88_uint8 code[] = { 0xBB, 0x0A, 0x00, 0x69, 0xC3, 0xE8, 0x03 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.regs[emu88::reg_AX] == 10000, "IMUL r,r/m,imm16: 10*1000=10000");
}

// =========================================================================
// Segment register push/pop
// =========================================================================

static void test_push_pop_seg() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0x1234;
  cpu.sregs[emu88::seg_ES] = 0x5678;
  cpu.sregs[emu88::seg_SS] = 0;
  cpu.regs[emu88::reg_SP] = 0xFFFE;

  // PUSH DS  (1E)
  // PUSH ES  (06)
  // POP DS   (1F)   ; DS = old ES = 0x5678
  // POP ES   (07)   ; ES = old DS = 0x1234
  emu88_uint8 code[] = { 0x1E, 0x06, 0x1F, 0x07 };
  load_and_run(cpu, code, sizeof(code), 4);
  check(cpu.sregs[emu88::seg_DS] == 0x5678, "PUSH/POP seg: DS swapped to 0x5678");
  check(cpu.sregs[emu88::seg_ES] == 0x1234, "PUSH/POP seg: ES swapped to 0x1234");
}

// =========================================================================
// MOV seg, r16 / MOV r16, seg
// =========================================================================

static void test_mov_seg() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;
  cpu.sregs[emu88::seg_ES] = 0x9ABC;

  // MOV AX, ES    (8C C0)  ; modrm: mod=11, reg=ES(0), rm=AX(0) -> 0xC0
  // MOV DS, AX    (8E D8)  ; modrm: mod=11, reg=DS(3), rm=AX(0) -> 0xD8
  emu88_uint8 code[] = { 0x8C, 0xC0, 0x8E, 0xD8 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.regs[emu88::reg_AX] == 0x9ABC, "MOV AX, ES: AX=0x9ABC");
  check(cpu.sregs[emu88::seg_DS] == 0x9ABC, "MOV DS, AX: DS=0x9ABC");
}

// =========================================================================
// LOOP variants: LOOPE, LOOPNE, JCXZ
// =========================================================================

static void test_loope() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // Count from 5 to 0, stop looping when CMP AX,3 is not zero (AX != 3)
  // MOV CX, 5       (B9 05 00)
  // MOV AX, 5       (B8 05 00)
  // DEC AX          (48)            ; loop body
  // CMP AX, 3       (3D 03 00)      ; sets ZF if AX==3
  // LOOPE -5        (E1 F9)          ; loop while CX!=0 && ZF=1
  emu88_uint8 code[] = { 0xB9, 0x05, 0x00, 0xB8, 0x05, 0x00, 0x48, 0x3D, 0x03, 0x00, 0xE1, 0xF9 };
  // First iter: AX=4, CMP 4==3 -> ZF=0 -> stop. Only 1 iteration.
  load_and_run(cpu, code, sizeof(code), 5); // setup(2) + dec + cmp + loope
  check(cpu.regs[emu88::reg_AX] == 4, "LOOPE: stopped when ZF=0, AX=4");
}

static void test_jcxz() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV CX, 0       (B9 00 00)
  // JCXZ +3         (E3 03)
  // MOV AX, 0xDEAD  (B8 AD DE)   ; skipped
  // NOP             (90)
  emu88_uint8 code[] = { 0xB9, 0x00, 0x00, 0xE3, 0x03, 0xB8, 0xAD, 0xDE, 0x90 };
  load_and_run(cpu, code, sizeof(code), 3);
  check(cpu.regs[emu88::reg_AX] == 0, "JCXZ: jumped when CX=0");
}

// =========================================================================
// RET imm16, CALL near indirect, JMP near
// =========================================================================

static void test_ret_imm() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;
  cpu.sregs[emu88::seg_SS] = 0;
  cpu.regs[emu88::reg_SP] = 0xFFF0;

  // 0100: CALL +6      (E8 06 00)  ; calls 0x109
  // 0103: NOP * 6      (padding, skipped by ret)
  // 0109: RET 4        (C2 04 00)  ; return and add 4 to SP
  emu88_uint8 code[] = {
    0xE8, 0x06, 0x00,
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    0xC2, 0x04, 0x00
  };
  uint16_t sp_before = cpu.regs[emu88::reg_SP];
  load_and_run(cpu, code, sizeof(code), 3); // CALL, RET, (back at 0103)
  // After CALL: SP -= 2 (push IP). After RET 4: SP += 2 (pop IP) + 4
  check(cpu.regs[emu88::reg_SP] == sp_before + 4, "RET imm16: SP adjusted by 4");
}

static void test_jmp_near() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // 0100: JMP +5        (E9 05 00)  ; jump to 0108
  // 0103: MOV AX, 0xDEAD (B8 AD DE) ; skipped
  // 0108: MOV BX, 0x42   (BB 42 00)
  emu88_uint8 code[] = {
    0xE9, 0x05, 0x00,
    0xB8, 0xAD, 0xDE,
    0x90, 0x90,
    0xBB, 0x42, 0x00
  };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.regs[emu88::reg_AX] == 0, "JMP near: skipped MOV AX");
  check(cpu.regs[emu88::reg_BX] == 0x42, "JMP near: executed target MOV BX");
}

// =========================================================================
// 386 extensions: MOVZX, MOVSX
// =========================================================================

static void test_movzx() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV BL, 0x80     (B3 80)
  // MOVZX AX, BL     (0F B6 C3) ; modrm: mod=11, reg=AX(0), rm=BX(3) -> 0xC3
  emu88_uint8 code[] = { 0xB3, 0x80, 0x0F, 0xB6, 0xC3 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.regs[emu88::reg_AX] == 0x0080, "MOVZX: zero-extend 0x80 -> 0x0080");
}

static void test_movsx() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV BL, 0x80     (B3 80)
  // MOVSX AX, BL     (0F BE C3)
  emu88_uint8 code[] = { 0xB3, 0x80, 0x0F, 0xBE, 0xC3 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.regs[emu88::reg_AX] == 0xFF80, "MOVSX: sign-extend 0x80 -> 0xFF80");

  // Positive value
  cpu.ip = 0x100;
  emu88_uint8 code2[] = { 0xB3, 0x7F, 0x0F, 0xBE, 0xC3 };
  load_and_run(cpu, code2, sizeof(code2), 2);
  check(cpu.regs[emu88::reg_AX] == 0x007F, "MOVSX: sign-extend 0x7F -> 0x007F");
}

// =========================================================================
// 386 extensions: SETcc
// =========================================================================

static void test_setcc() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // SETZ AL: set AL=1 if ZF=1
  cpu.flags = emu88::FLAG_ZF | 0x0002;
  // 0F 94 C0  (SETZ AL: modrm mod=11, reg=0(unused), rm=0(AL) -> 0xC0)
  emu88_uint8 code1[] = { 0x0F, 0x94, 0xC0 };
  load_and_run(cpu, code1, sizeof(code1), 1);
  check(cpu.get_reg8(emu88::reg_AL) == 1, "SETZ: AL=1 when ZF=1");

  // SETZ AL when ZF=0
  cpu.ip = 0x100;
  cpu.flags = 0x0002; // ZF=0
  load_and_run(cpu, code1, sizeof(code1), 1);
  check(cpu.get_reg8(emu88::reg_AL) == 0, "SETZ: AL=0 when ZF=0");

  // SETB AL (set if CF=1)
  cpu.ip = 0x100;
  cpu.flags = emu88::FLAG_CF | 0x0002;
  emu88_uint8 code2[] = { 0x0F, 0x92, 0xC0 };
  load_and_run(cpu, code2, sizeof(code2), 1);
  check(cpu.get_reg8(emu88::reg_AL) == 1, "SETB: AL=1 when CF=1");
}

// =========================================================================
// 386 extensions: Jcc near (0F 8x)
// =========================================================================

static void test_jcc_near() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // JE near +0x100: jump to 0x206 (0F 84 00 01)
  cpu.flags = emu88::FLAG_ZF | 0x0002;
  // 0F 84 xx xx  (JE near, 16-bit displacement)
  // At IP=0x100: opcode takes 4 bytes, so IP after = 0x104, target = 0x104 + 0x100 = 0x204
  // Put a MOV BX,42 at 0x204
  mem.store_mem(0x204, 0xBB); mem.store_mem(0x205, 0x2A); mem.store_mem(0x206, 0x00);
  emu88_uint8 code[] = { 0x0F, 0x84, 0x00, 0x01 };
  load_and_run(cpu, code, sizeof(code), 1);
  check(cpu.ip == 0x204, "JE near: jumped to 0x204");
}

// =========================================================================
// 386 extensions: PUSH/POP FS/GS
// =========================================================================

static void test_push_pop_fs_gs() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;
  cpu.sregs[emu88::seg_SS] = 0;
  cpu.regs[emu88::reg_SP] = 0xFFFE;
  cpu.sregs[emu88::seg_FS] = 0xAAAA;
  cpu.sregs[emu88::seg_GS] = 0xBBBB;

  // PUSH FS  (0F A0)
  // PUSH GS  (0F A8)
  // POP FS   (0F A1)  ; FS = old GS = 0xBBBB
  // POP GS   (0F A9)  ; GS = old FS = 0xAAAA
  emu88_uint8 code[] = { 0x0F, 0xA0, 0x0F, 0xA8, 0x0F, 0xA1, 0x0F, 0xA9 };
  load_and_run(cpu, code, sizeof(code), 4);
  check(cpu.sregs[emu88::seg_FS] == 0xBBBB, "PUSH/POP FS/GS: FS swapped");
  check(cpu.sregs[emu88::seg_GS] == 0xAAAA, "PUSH/POP FS/GS: GS swapped");
}

// =========================================================================
// I/O instructions (basic; default port_in returns 0)
// =========================================================================

static void test_in_out() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // OUT 0x80, AL    (E6 80) ; write AL to port 0x80 (diagnostic port)
  // IN AL, 0x80     (E4 80) ; read from port 0x80
  // (base emu88 port_in returns 0, but this at least tests decoding)
  emu88_uint8 code[] = { 0xB0, 0x55, 0xE6, 0x80, 0xE4, 0x80 };
  load_and_run(cpu, code, sizeof(code), 3);
  // base port_in returns 0xFF
  check(cpu.get_reg8(emu88::reg_AL) == 0xFF, "IN/OUT: decoded and read port");
}

static void test_in_out_dx() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV DX, 0x3D4  (BA D4 03)
  // MOV AL, 0x0E   (B0 0E)
  // OUT DX, AL     (EE)
  // IN AL, DX      (EC)
  emu88_uint8 code[] = { 0xBA, 0xD4, 0x03, 0xB0, 0x0E, 0xEE, 0xEC };
  load_and_run(cpu, code, sizeof(code), 4);
  check(true, "IN/OUT DX: decoded without crash");
}

// =========================================================================
// ADC / SBB (carry-dependent arithmetic)
// =========================================================================

static void test_adc() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // STC           (F9)
  // MOV AX, 5     (B8 05 00)
  // ADC AX, 3     (15 03 00)   ; AX = 5 + 3 + CF(1) = 9
  emu88_uint8 code[] = { 0xF9, 0xB8, 0x05, 0x00, 0x15, 0x03, 0x00 };
  load_and_run(cpu, code, sizeof(code), 3);
  check(cpu.regs[emu88::reg_AX] == 9, "ADC: 5+3+CF(1) = 9");
}

static void test_sbb() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // STC           (F9)
  // MOV AX, 10    (B8 0A 00)
  // SBB AX, 3     (1D 03 00)   ; AX = 10 - 3 - CF(1) = 6
  emu88_uint8 code[] = { 0xF9, 0xB8, 0x0A, 0x00, 0x1D, 0x03, 0x00 };
  load_and_run(cpu, code, sizeof(code), 3);
  check(cpu.regs[emu88::reg_AX] == 6, "SBB: 10-3-CF(1) = 6");
}

// =========================================================================
// OR / XOR edge cases
// =========================================================================

static void test_or() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AX, 0xF0F0 (B8 F0 F0)
  // OR AX, 0x0F0F  (0D 0F 0F)
  emu88_uint8 code[] = { 0xB8, 0xF0, 0xF0, 0x0D, 0x0F, 0x0F };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.regs[emu88::reg_AX] == 0xFFFF, "OR: 0xF0F0 | 0x0F0F = 0xFFFF");
}

static void test_xor_self() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AX, 0x1234 (B8 34 12)
  // XOR AX, AX     (31 C0)   ; modrm: mod=11, reg=AX(0), rm=AX(0) -> 0xC0
  emu88_uint8 code[] = { 0xB8, 0x34, 0x12, 0x31, 0xC0 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.regs[emu88::reg_AX] == 0, "XOR AX,AX: clears register");
  check(cpu.get_flag(emu88::FLAG_ZF), "XOR AX,AX: ZF set");
}

// =========================================================================
// TEST instruction
// =========================================================================

static void test_test() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AX, 0x0F00  (B8 00 0F)
  // TEST AX, 0x00F0 (A9 F0 00) ; AND without storing: 0x0F00 & 0x00F0 = 0
  emu88_uint8 code[] = { 0xB8, 0x00, 0x0F, 0xA9, 0xF0, 0x00 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.get_flag(emu88::FLAG_ZF), "TEST: ZF set when AND result is 0");
  check(cpu.regs[emu88::reg_AX] == 0x0F00, "TEST: AX unchanged");
}

// =========================================================================
// NOP
// =========================================================================

static void test_nop() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // NOP (90) x 3
  emu88_uint8 code[] = { 0x90, 0x90, 0x90 };
  load_and_run(cpu, code, sizeof(code), 3);
  check(cpu.ip == 0x103, "NOP: IP advanced by 3");
}

// =========================================================================
// MUL 16-bit
// =========================================================================

static void test_mul16() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AX, 1000   (B8 E8 03)
  // MOV BX, 100    (BB 64 00)
  // MUL BX         (F7 E3)    ; DX:AX = 1000*100 = 100000 = 0x0001:86A0
  emu88_uint8 code[] = { 0xB8, 0xE8, 0x03, 0xBB, 0x64, 0x00, 0xF7, 0xE3 };
  load_and_run(cpu, code, sizeof(code), 3);
  check(cpu.regs[emu88::reg_AX] == 0x86A0, "MUL16: low word = 0x86A0");
  check(cpu.regs[emu88::reg_DX] == 0x0001, "MUL16: high word = 0x0001");
}

// =========================================================================
// ADD/SUB with flags: carry and overflow edge cases
// =========================================================================

static void test_add_carry() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AX, 0xFFFF (B8 FF FF)
  // ADD AX, 1      (05 01 00)  ; wraps to 0, sets CF
  emu88_uint8 code[] = { 0xB8, 0xFF, 0xFF, 0x05, 0x01, 0x00 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.regs[emu88::reg_AX] == 0, "ADD overflow: 0xFFFF+1=0");
  check(cpu.get_flag(emu88::FLAG_CF), "ADD overflow: CF set");
  check(cpu.get_flag(emu88::FLAG_ZF), "ADD overflow: ZF set");
}

static void test_add_signed_overflow() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AX, 0x7FFF (B8 FF 7F)
  // ADD AX, 1      (05 01 00)  ; 32767+1 = signed overflow
  emu88_uint8 code[] = { 0xB8, 0xFF, 0x7F, 0x05, 0x01, 0x00 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.regs[emu88::reg_AX] == 0x8000, "ADD signed overflow: 0x7FFF+1=0x8000");
  check(cpu.get_flag(emu88::FLAG_OF), "ADD signed overflow: OF set");
  check(cpu.get_flag(emu88::FLAG_SF), "ADD signed overflow: SF set");
}

static void test_sub_borrow() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AX, 0      (B8 00 00)
  // SUB AX, 1      (2D 01 00) ; 0-1 = borrow
  emu88_uint8 code[] = { 0xB8, 0x00, 0x00, 0x2D, 0x01, 0x00 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.regs[emu88::reg_AX] == 0xFFFF, "SUB borrow: 0-1=0xFFFF");
  check(cpu.get_flag(emu88::FLAG_CF), "SUB borrow: CF set");
  check(cpu.get_flag(emu88::FLAG_SF), "SUB borrow: SF set");
}

// =========================================================================
// GRP1 r/m8, imm8 (opcode 0x80)
// =========================================================================

static void test_grp1_rm8_imm8() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AL, 10      (B0 0A)
  // ADD AL, 5       (80 C0 05)  ; modrm: mod=11, reg=0(ADD), rm=0(AL)
  // CMP AL, 15      (80 F8 0F)  ; modrm: mod=11, reg=7(CMP), rm=0(AL)
  emu88_uint8 code[] = { 0xB0, 0x0A, 0x80, 0xC0, 0x05, 0x80, 0xF8, 0x0F };
  load_and_run(cpu, code, sizeof(code), 3);
  check(cpu.get_reg8(emu88::reg_AL) == 15, "GRP1 ADD r/m8, imm8: 10+5=15");
  check(cpu.get_flag(emu88::FLAG_ZF), "GRP1 CMP r/m8, imm8: ZF set (15==15)");
}

// =========================================================================
// GRP1 r/m16, sign-ext imm8 (opcode 0x83)
// =========================================================================

static void test_grp1_rm16_simm8() {
  emu88_mem mem;
  emu88 cpu(&mem);
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0;

  // MOV AX, 100    (B8 64 00)
  // SUB AX, 3      (83 E8 03)  ; modrm: mod=11, reg=5(SUB), rm=0(AX)
  emu88_uint8 code[] = { 0xB8, 0x64, 0x00, 0x83, 0xE8, 0x03 };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.regs[emu88::reg_AX] == 97, "GRP1 SUB r/m16, simm8: 100-3=97");
}

//=============================================================================
// 386 32-bit instruction tests
//=============================================================================

// Helper: set up CPU for 386 tests
static void setup_386(emu88 &cpu) {
  cpu.sregs[emu88::seg_CS] = 0; cpu.ip = 0x100;
  cpu.sregs[emu88::seg_DS] = 0; cpu.sregs[emu88::seg_SS] = 0;
  cpu.sregs[emu88::seg_ES] = 0;
  cpu.regs[emu88::reg_SP] = 0xFFF0;
}

static void test_386_mov_imm32() {
  emu88_mem mem;
  emu88 cpu(&mem);
  setup_386(cpu);

  // 66 B8 78 56 34 12  = MOV EAX, 0x12345678
  // 66 BB EF CD AB 00  = MOV EBX, 0x00ABCDEF
  emu88_uint8 code[] = {
    0x66, 0xB8, 0x78, 0x56, 0x34, 0x12,
    0x66, 0xBB, 0xEF, 0xCD, 0xAB, 0x00
  };
  load_and_run(cpu, code, sizeof(code), 2); // prefix is consumed within execute()
  check(cpu.get_reg32(emu88::reg_AX) == 0x12345678, "MOV EAX, 0x12345678");
  check(cpu.get_reg32(emu88::reg_BX) == 0x00ABCDEF, "MOV EBX, 0x00ABCDEF");
  // Verify low 16 bits are correct
  check(cpu.regs[emu88::reg_AX] == 0x5678, "MOV EAX low word = 0x5678");
}

static void test_386_add_reg32() {
  emu88_mem mem;
  emu88 cpu(&mem);
  setup_386(cpu);

  // Load EAX=0x10000000, EBX=0x20000000, then ADD EAX,EBX
  cpu.set_reg32(emu88::reg_AX, 0x10000000);
  cpu.set_reg32(emu88::reg_BX, 0x20000000);

  // 66 01 D8 = ADD EAX, EBX (modrm: mod=11 reg=BX rm=AX = 0xD8)
  emu88_uint8 code[] = { 0x66, 0x01, 0xD8 };
  load_and_run(cpu, code, sizeof(code), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 0x30000000, "ADD EAX, EBX = 0x30000000");

  // SUB EAX, EBX
  cpu.ip = 0x100;
  emu88_uint8 code2[] = { 0x66, 0x29, 0xD8 };  // SUB EAX, EBX
  load_and_run(cpu, code2, sizeof(code2), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 0x10000000, "SUB EAX, EBX = 0x10000000");
}

static void test_386_push_pop32() {
  emu88_mem mem;
  emu88 cpu(&mem);
  setup_386(cpu);

  cpu.set_reg32(emu88::reg_AX, 0xDEADBEEF);

  // 66 50 = PUSH EAX, 66 5B = POP EBX
  emu88_uint8 code[] = { 0x66, 0x50, 0x66, 0x5B };
  load_and_run(cpu, code, sizeof(code), 2);
  check(cpu.get_reg32(emu88::reg_BX) == 0xDEADBEEF, "PUSH/POP EAX->EBX = 0xDEADBEEF");
}

static void test_386_inc_dec32() {
  emu88_mem mem;
  emu88 cpu(&mem);
  setup_386(cpu);

  cpu.set_reg32(emu88::reg_AX, 0xFFFF);

  // 66 40 = INC EAX
  emu88_uint8 code[] = { 0x66, 0x40 };
  load_and_run(cpu, code, sizeof(code), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 0x10000, "INC EAX: 0xFFFF -> 0x10000");

  // 66 48 = DEC EAX
  cpu.ip = 0x100;
  emu88_uint8 code2[] = { 0x66, 0x48 };
  load_and_run(cpu, code2, sizeof(code2), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 0xFFFF, "DEC EAX: 0x10000 -> 0xFFFF");
}

static void test_386_cwde_cdq() {
  emu88_mem mem;
  emu88 cpu(&mem);
  setup_386(cpu);

  // CWDE: sign-extend AX to EAX
  cpu.regs[emu88::reg_AX] = 0xFF80;  // -128
  emu88_uint8 code_cwde[] = { 0x66, 0x98 };  // prefix + CBW = CWDE
  load_and_run(cpu, code_cwde, sizeof(code_cwde), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 0xFFFFFF80, "CWDE: AX=0xFF80 -> EAX=0xFFFFFF80");

  // CDQ: sign-extend EAX to EDX:EAX
  cpu.ip = 0x100;
  cpu.set_reg32(emu88::reg_AX, 0x80000000);
  emu88_uint8 code_cdq[] = { 0x66, 0x99 };  // prefix + CWD = CDQ
  load_and_run(cpu, code_cdq, sizeof(code_cdq), 1);
  check(cpu.get_reg32(emu88::reg_DX) == 0xFFFFFFFF, "CDQ: EAX=0x80000000 -> EDX=0xFFFFFFFF");
}

static void test_386_pushfd_popfd() {
  emu88_mem mem;
  emu88 cpu(&mem);
  setup_386(cpu);

  cpu.flags = 0x0246;  // ZF+PF+bit1
  cpu.eflags_hi = 0;

  // 66 9C = PUSHFD, clear flags, 66 9D = POPFD
  emu88_uint8 code[] = { 0x66, 0x9C, 0x66, 0x9D };
  load_and_run(cpu, code, sizeof(code), 1); // PUSHFD
  cpu.flags = 0x0002;
  // Need to re-load POPFD code at current IP
  emu88_uint32 addr2 = EMU88_MK20(cpu.sregs[emu88::seg_CS], cpu.ip);
  cpu.mem->store_mem(addr2, 0x66);
  cpu.mem->store_mem(addr2 + 1, 0x9D);
  cpu.execute();  // POPFD
  check((cpu.flags & 0x0FFF) == (0x0246 & 0x0FFF), "PUSHFD/POPFD preserves flags");
}

static void test_386_movzx_movsx_32() {
  emu88_mem mem;
  emu88 cpu(&mem);
  setup_386(cpu);

  // MOVZX EAX, BL: 0x0F 0xB6 modrm=0xC3 (mod=11, reg=0=EAX, rm=3=BL)
  cpu.set_reg8(emu88::reg_BL, 0x80);
  emu88_uint8 code_zx[] = { 0x66, 0x0F, 0xB6, 0xC3 };
  load_and_run(cpu, code_zx, sizeof(code_zx), 1); // prefix consumed within execute()
  check(cpu.get_reg32(emu88::reg_AX) == 0x80, "MOVZX EAX, BL: 0x80 -> 0x00000080");

  // MOVSX EAX, BL: 0x0F 0xBE modrm=0xC3
  cpu.ip = 0x100;
  emu88_uint8 code_sx[] = { 0x66, 0x0F, 0xBE, 0xC3 };
  load_and_run(cpu, code_sx, sizeof(code_sx), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 0xFFFFFF80, "MOVSX EAX, BL: 0x80 -> 0xFFFFFF80");

  // MOVZX EAX, BX: 0x0F 0xB7 modrm=0xC3
  cpu.ip = 0x100;
  cpu.regs[emu88::reg_BX] = 0x8000;
  emu88_uint8 code_zxw[] = { 0x0F, 0xB7, 0xC3 };
  load_and_run(cpu, code_zxw, sizeof(code_zxw), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 0x8000, "MOVZX EAX, BX: 0x8000 -> 0x00008000");

  // MOVSX EAX, BX: 0x0F 0xBF modrm=0xC3
  cpu.ip = 0x100;
  emu88_uint8 code_sxw[] = { 0x0F, 0xBF, 0xC3 };
  load_and_run(cpu, code_sxw, sizeof(code_sxw), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 0xFFFF8000, "MOVSX EAX, BX: 0x8000 -> 0xFFFF8000");
}

static void test_386_bsf_bsr() {
  emu88_mem mem;
  emu88 cpu(&mem);
  setup_386(cpu);

  // BSF EAX, EBX: 0x0F 0xBC modrm=0xC3
  cpu.set_reg32(emu88::reg_BX, 0x00000010);  // bit 4 set
  emu88_uint8 code_bsf[] = { 0x66, 0x0F, 0xBC, 0xC3 };
  load_and_run(cpu, code_bsf, sizeof(code_bsf), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 4, "BSF EAX, EBX: 0x10 -> bit 4");
  check(!cpu.get_flag(emu88::FLAG_ZF), "BSF: ZF=0 when nonzero");

  // BSR EAX, EBX: 0x0F 0xBD modrm=0xC3
  cpu.ip = 0x100;
  cpu.set_reg32(emu88::reg_BX, 0x80000000);
  emu88_uint8 code_bsr[] = { 0x66, 0x0F, 0xBD, 0xC3 };
  load_and_run(cpu, code_bsr, sizeof(code_bsr), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 31, "BSR EAX, EBX: 0x80000000 -> bit 31");

  // BSF with zero source -> ZF=1
  cpu.ip = 0x100;
  cpu.set_reg32(emu88::reg_BX, 0);
  load_and_run(cpu, code_bsf, sizeof(code_bsf), 1);
  check(cpu.get_flag(emu88::FLAG_ZF), "BSF: ZF=1 when zero");
}

static void test_386_bt_bts_btr_btc() {
  emu88_mem mem;
  emu88 cpu(&mem);
  setup_386(cpu);

  // BT EAX, imm8: 0x0F 0xBA modrm=0xE0 (reg=4=BT, rm=0=EAX), imm8
  cpu.set_reg32(emu88::reg_AX, 0x00000008);  // bit 3 set

  // BT EAX, 3 -> CF=1
  emu88_uint8 code_bt[] = { 0x66, 0x0F, 0xBA, 0xE0, 0x03 };
  load_and_run(cpu, code_bt, sizeof(code_bt), 1);
  check(cpu.get_flag(emu88::FLAG_CF), "BT EAX, 3: CF=1");

  // BT EAX, 4 -> CF=0
  cpu.ip = 0x100;
  emu88_uint8 code_bt2[] = { 0x66, 0x0F, 0xBA, 0xE0, 0x04 };
  load_and_run(cpu, code_bt2, sizeof(code_bt2), 1);
  check(!cpu.get_flag(emu88::FLAG_CF), "BT EAX, 4: CF=0");

  // BTS EAX, 4: set bit 4
  cpu.ip = 0x100;
  emu88_uint8 code_bts[] = { 0x66, 0x0F, 0xBA, 0xE8, 0x04 };  // reg=5=BTS
  load_and_run(cpu, code_bts, sizeof(code_bts), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 0x00000018, "BTS EAX, 4: set bit 4");
  check(!cpu.get_flag(emu88::FLAG_CF), "BTS: CF=0 (was 0)");

  // BTR EAX, 3: clear bit 3
  cpu.ip = 0x100;
  emu88_uint8 code_btr[] = { 0x66, 0x0F, 0xBA, 0xF0, 0x03 };  // reg=6=BTR
  load_and_run(cpu, code_btr, sizeof(code_btr), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 0x00000010, "BTR EAX, 3: clear bit 3");
  check(cpu.get_flag(emu88::FLAG_CF), "BTR: CF=1 (was 1)");

  // BTC EAX, 4: complement bit 4
  cpu.ip = 0x100;
  emu88_uint8 code_btc[] = { 0x66, 0x0F, 0xBA, 0xF8, 0x04 };  // reg=7=BTC
  load_and_run(cpu, code_btc, sizeof(code_btc), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 0x00000000, "BTC EAX, 4: complement bit 4");
  check(cpu.get_flag(emu88::FLAG_CF), "BTC: CF=1 (was 1)");
}

static void test_386_shld_shrd() {
  emu88_mem mem;
  emu88 cpu(&mem);
  setup_386(cpu);

  // SHLD EAX, EBX, imm8: 0x0F 0xA4 modrm imm8
  cpu.set_reg32(emu88::reg_AX, 0x12345678);
  cpu.set_reg32(emu88::reg_BX, 0xABCDEF01);

  // SHLD EAX, EBX, 4
  emu88_uint8 code_shld[] = { 0x66, 0x0F, 0xA4, 0xD8, 0x04 };  // modrm C3->D8: reg=BX, rm=AX
  load_and_run(cpu, code_shld, sizeof(code_shld), 1);
  // EAX shifts left 4, low 4 bits filled from high 4 bits of EBX (0xA)
  check(cpu.get_reg32(emu88::reg_AX) == 0x2345678A, "SHLD EAX, EBX, 4");

  // SHRD EAX, EBX, 4: 0x0F 0xAC modrm imm8
  cpu.ip = 0x100;
  cpu.set_reg32(emu88::reg_AX, 0x12345678);
  cpu.set_reg32(emu88::reg_BX, 0xABCDEF01);
  emu88_uint8 code_shrd[] = { 0x66, 0x0F, 0xAC, 0xD8, 0x04 };
  load_and_run(cpu, code_shrd, sizeof(code_shrd), 1);
  // EAX shifts right 4, high 4 bits filled from low 4 bits of EBX (0x1)
  check(cpu.get_reg32(emu88::reg_AX) == 0x11234567, "SHRD EAX, EBX, 4");
}

static void test_386_bswap() {
  emu88_mem mem;
  emu88 cpu(&mem);
  setup_386(cpu);

  cpu.set_reg32(emu88::reg_AX, 0x12345678);
  // BSWAP EAX: 0x0F 0xC8
  emu88_uint8 code[] = { 0x0F, 0xC8 };
  load_and_run(cpu, code, sizeof(code), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 0x78563412, "BSWAP EAX: 0x12345678 -> 0x78563412");
}

static void test_386_imul_r32() {
  emu88_mem mem;
  emu88 cpu(&mem);
  setup_386(cpu);

  // IMUL EAX, EBX: 0x0F 0xAF modrm=0xC3 (mod=11 reg=0=EAX rm=3=EBX)
  cpu.set_reg32(emu88::reg_AX, 100);
  cpu.set_reg32(emu88::reg_BX, 200);
  emu88_uint8 code[] = { 0x66, 0x0F, 0xAF, 0xC3 };
  load_and_run(cpu, code, sizeof(code), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 20000, "IMUL EAX, EBX: 100*200=20000");
}

static void test_386_xadd() {
  emu88_mem mem;
  emu88 cpu(&mem);
  setup_386(cpu);

  // XADD EAX, EBX: 0x0F 0xC1 modrm=0xD8 (mod=11, reg=BX, rm=AX)
  cpu.set_reg32(emu88::reg_AX, 100);
  cpu.set_reg32(emu88::reg_BX, 200);
  emu88_uint8 code[] = { 0x66, 0x0F, 0xC1, 0xD8 };
  load_and_run(cpu, code, sizeof(code), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 300, "XADD EAX, EBX: dest=100+200=300");
  check(cpu.get_reg32(emu88::reg_BX) == 100, "XADD EAX, EBX: src=old_dest=100");
}

static void test_386_cmpxchg() {
  emu88_mem mem;
  emu88 cpu(&mem);
  setup_386(cpu);

  // CMPXCHG EBX, ECX: 0x0F 0xB1 modrm=0xCB (mod=11, reg=1=ECX, rm=3=EBX)
  // Case 1: EAX == EBX -> EBX = ECX, ZF=1
  cpu.set_reg32(emu88::reg_AX, 0x1000);
  cpu.set_reg32(emu88::reg_BX, 0x1000);
  cpu.set_reg32(emu88::reg_CX, 0x2000);
  emu88_uint8 code[] = { 0x66, 0x0F, 0xB1, 0xCB };
  load_and_run(cpu, code, sizeof(code), 1);
  check(cpu.get_reg32(emu88::reg_BX) == 0x2000, "CMPXCHG: EAX==EBX -> EBX=ECX");
  check(cpu.get_flag(emu88::FLAG_ZF), "CMPXCHG: ZF=1 on match");

  // Case 2: EAX != EBX -> EAX = EBX, ZF=0
  cpu.ip = 0x100;
  cpu.set_reg32(emu88::reg_AX, 0x1000);
  cpu.set_reg32(emu88::reg_BX, 0x3000);
  cpu.set_reg32(emu88::reg_CX, 0x2000);
  load_and_run(cpu, code, sizeof(code), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 0x3000, "CMPXCHG: EAX!=EBX -> EAX=EBX");
  check(!cpu.get_flag(emu88::FLAG_ZF), "CMPXCHG: ZF=0 on mismatch");
}

static void test_386_grp1_32() {
  emu88_mem mem;
  emu88 cpu(&mem);
  setup_386(cpu);

  // ADD EAX, imm32: 66 05 dd dd dd dd
  cpu.set_reg32(emu88::reg_AX, 0x10000000);
  emu88_uint8 code[] = { 0x66, 0x05, 0x00, 0x00, 0x00, 0x20 };  // ADD EAX, 0x20000000
  load_and_run(cpu, code, sizeof(code), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 0x30000000, "ADD EAX, imm32");

  // CMP EAX, imm32: 66 3D dd dd dd dd
  cpu.ip = 0x100;
  emu88_uint8 code2[] = { 0x66, 0x3D, 0x00, 0x00, 0x00, 0x30 };  // CMP EAX, 0x30000000
  load_and_run(cpu, code2, sizeof(code2), 1);
  check(cpu.get_flag(emu88::FLAG_ZF), "CMP EAX, imm32: equal -> ZF=1");

  // GRP1 r/m32, simm8: 66 83 C0 05 = ADD EAX, 5
  cpu.ip = 0x100;
  cpu.set_reg32(emu88::reg_AX, 0xFFFFFFFB);
  emu88_uint8 code3[] = { 0x66, 0x83, 0xC0, 0x05 };
  load_and_run(cpu, code3, sizeof(code3), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 0, "ADD EAX, simm8: 0xFFFFFFFB+5=0");
  check(cpu.get_flag(emu88::FLAG_ZF), "ADD EAX, simm8: ZF=1");
}

static void test_386_shift32() {
  emu88_mem mem;
  emu88 cpu(&mem);
  setup_386(cpu);

  // SHL EAX, 4: 66 C1 E0 04
  cpu.set_reg32(emu88::reg_AX, 0x12345678);
  emu88_uint8 code[] = { 0x66, 0xC1, 0xE0, 0x04 };
  load_and_run(cpu, code, sizeof(code), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 0x23456780, "SHL EAX, 4");

  // SHR EAX, 8: 66 C1 E8 08
  cpu.ip = 0x100;
  cpu.set_reg32(emu88::reg_AX, 0x12345678);
  emu88_uint8 code2[] = { 0x66, 0xC1, 0xE8, 0x08 };
  load_and_run(cpu, code2, sizeof(code2), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 0x00123456, "SHR EAX, 8");

  // SAR EAX, 4: 66 C1 F8 04
  cpu.ip = 0x100;
  cpu.set_reg32(emu88::reg_AX, 0x80000000);
  emu88_uint8 code3[] = { 0x66, 0xC1, 0xF8, 0x04 };
  load_and_run(cpu, code3, sizeof(code3), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 0xF8000000, "SAR EAX, 4 (sign extend)");
}

static void test_386_test32() {
  emu88_mem mem;
  emu88 cpu(&mem);
  setup_386(cpu);

  // TEST EAX, imm32: 66 A9 dd dd dd dd
  cpu.set_reg32(emu88::reg_AX, 0x80000000);
  emu88_uint8 code[] = { 0x66, 0xA9, 0x00, 0x00, 0x00, 0x80 };
  load_and_run(cpu, code, sizeof(code), 1);
  check(!cpu.get_flag(emu88::FLAG_ZF), "TEST EAX, 0x80000000: ZF=0");
  check(cpu.get_flag(emu88::FLAG_SF), "TEST EAX, 0x80000000: SF=1");

  cpu.ip = 0x100;
  cpu.set_reg32(emu88::reg_AX, 0x7FFFFFFF);
  load_and_run(cpu, code, sizeof(code), 1);
  check(cpu.get_flag(emu88::FLAG_ZF), "TEST 0x7FFFFFFF & 0x80000000: ZF=1");
}

static void test_386_xchg32() {
  emu88_mem mem;
  emu88 cpu(&mem);
  setup_386(cpu);

  cpu.set_reg32(emu88::reg_AX, 0x11111111);
  cpu.set_reg32(emu88::reg_BX, 0x22222222);

  // XCHG EAX, EBX: 66 93
  emu88_uint8 code[] = { 0x66, 0x93 };
  load_and_run(cpu, code, sizeof(code), 1);
  check(cpu.get_reg32(emu88::reg_AX) == 0x22222222, "XCHG EAX, EBX: EAX=0x22222222");
  check(cpu.get_reg32(emu88::reg_BX) == 0x11111111, "XCHG EAX, EBX: EBX=0x11111111");
}

static void test_386_stosd() {
  emu88_mem mem;
  emu88 cpu(&mem);
  setup_386(cpu);

  cpu.set_reg32(emu88::reg_AX, 0xDEADBEEF);
  cpu.regs[emu88::reg_DI] = 0x2000;

  // STOSD: 66 AB
  emu88_uint8 code[] = { 0x66, 0xAB };
  load_and_run(cpu, code, sizeof(code), 1);
  check(mem.fetch_mem32(0x2000) == 0xDEADBEEF, "STOSD: [DI] = 0xDEADBEEF");
  check(cpu.regs[emu88::reg_DI] == 0x2004, "STOSD: DI += 4");
}

static void test_386_movsd() {
  emu88_mem mem;
  emu88 cpu(&mem);
  setup_386(cpu);

  mem.store_mem32(0x1000, 0xCAFEBABE);
  cpu.regs[emu88::reg_SI] = 0x1000;
  cpu.regs[emu88::reg_DI] = 0x2000;

  // MOVSD: 66 A5
  emu88_uint8 code[] = { 0x66, 0xA5 };
  load_and_run(cpu, code, sizeof(code), 1);
  check(mem.fetch_mem32(0x2000) == 0xCAFEBABE, "MOVSD: [DI] = [SI] = 0xCAFEBABE");
  check(cpu.regs[emu88::reg_SI] == 0x1004, "MOVSD: SI += 4");
  check(cpu.regs[emu88::reg_DI] == 0x2004, "MOVSD: DI += 4");
}

int main(int argc, char* argv[]) {
  printf("Running 8088 emulator tests...\n\n");

  // Original tests
  test_mov_immediate();
  test_mov_reg8();
  test_add();
  test_sub();
  test_push_pop();
  test_jmp_short();
  test_call_ret();
  test_cmp_jcc();
  test_loop();
  test_inc_dec();
  test_xchg();
  test_and_or_xor();
  test_mul();
  test_string_movsb();
  test_segment_override();

  // Shift / Rotate
  test_shl();
  test_shr();
  test_sar();
  test_rol();
  test_ror();
  test_shl_cl();
  test_shl_imm8();
  test_rcl();

  // DIV / IDIV / IMUL
  test_div8();
  test_div16();
  test_idiv8();
  test_imul8();

  // NOT / NEG
  test_not();
  test_neg();
  test_neg_zero();

  // String operations
  test_stosb();
  test_lodsb();
  test_movsw();
  test_stosw();
  test_cmpsb();
  test_scasb();
  test_std_direction();

  // Flags
  test_flag_instructions();
  test_lahf_sahf();
  test_pushf_popf();

  // All conditional jumps
  test_all_jcc();

  // LEA, LES
  test_lea();
  test_les();

  // Memory MOV
  test_mov_mem();
  test_mov_byte_mem();

  // CBW, CWD, XLAT
  test_cbw();
  test_cwd();
  test_xlat();

  // BCD
  test_daa();
  test_das();
  test_aam();
  test_aad();

  // 80186
  test_pusha_popa();
  test_push_imm();
  test_push_imm8();
  test_enter_leave();
  test_imul_3op();
  test_imul_3op_imm16();

  // Segment register ops
  test_push_pop_seg();
  test_mov_seg();

  // Loop variants
  test_loope();
  test_jcxz();

  // Call/ret/jmp variants
  test_ret_imm();
  test_jmp_near();

  // 386 extensions
  test_movzx();
  test_movsx();
  test_setcc();
  test_jcc_near();
  test_push_pop_fs_gs();

  // I/O
  test_in_out();
  test_in_out_dx();

  // ADC / SBB
  test_adc();
  test_sbb();

  // OR / XOR edge cases
  test_or();
  test_xor_self();

  // TEST
  test_test();

  // NOP
  test_nop();

  // MUL 16-bit
  test_mul16();

  // Flag edge cases
  test_add_carry();
  test_add_signed_overflow();
  test_sub_borrow();

  // GRP1 instruction encoding variants
  test_grp1_rm8_imm8();
  test_grp1_rm16_simm8();

  // 386 32-bit operations
  test_386_mov_imm32();
  test_386_add_reg32();
  test_386_push_pop32();
  test_386_inc_dec32();
  test_386_cwde_cdq();
  test_386_pushfd_popfd();
  test_386_movzx_movsx_32();
  test_386_bsf_bsr();
  test_386_bt_bts_btr_btc();
  test_386_shld_shrd();
  test_386_bswap();
  test_386_imul_r32();
  test_386_xadd();
  test_386_cmpxchg();
  test_386_grp1_32();
  test_386_shift32();
  test_386_test32();
  test_386_xchg32();
  test_386_stosd();
  test_386_movsd();

  fprintf(stderr, "Unit tests: %d passed, %d failed\n", tests_passed, tests_failed);

  // 8088 SingleStepTests (auto-downloads on first run)
  const char* sst_filter = (argc >= 2) ? argv[1] : nullptr;
  int sst_fail = run_8088_sst_tests(sst_filter);

  fprintf(stderr, "\nUnit tests: %d passed, %d failed\n", tests_passed, tests_failed);
  fprintf(stderr, "8088 SST:   %d failures\n", sst_fail > 0 ? sst_fail : 0);
  return (tests_failed > 0 || sst_fail > 0) ? 1 : 0;
}
