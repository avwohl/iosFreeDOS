#include "emu88.h"
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

int main() {
  printf("Running 8088 emulator tests...\n");

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

  printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
  return tests_failed > 0 ? 1 : 0;
}
