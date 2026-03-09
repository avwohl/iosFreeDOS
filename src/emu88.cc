#include "emu88.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>

static emu88_trace dummy_trace;

void emu88_fatal(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");
}

//=============================================================================
// Constructor and initialization
//=============================================================================

emu88::emu88(emu88_mem *memory)
  : mem(memory),
    trace(&dummy_trace),
    debug(false),
    cycles(0),
    int_pending(false),
    int_vector(0),
    halted(false),
    seg_override(-1),
    rep_prefix(REP_NONE),
    op_size_32(false),
    addr_size_32(false),
    gdtr_base(0), gdtr_limit(0),
    idtr_base(0), idtr_limit(0x3FF),
    cr0(0) {
  memset(regs, 0, sizeof(regs));
  memset(regs_hi, 0, sizeof(regs_hi));
  memset(sregs, 0, sizeof(sregs));
  ip = 0;
  flags = 0x0002;  // bit 1 is always 1 on 8088
  eflags_hi = 0;
  setup_parity();
}

void emu88::setup_parity(void) {
  for (int i = 0; i < 256; i++) {
    int bits = 0;
    int v = i;
    while (v) {
      bits += v & 1;
      v >>= 1;
    }
    parity_table[i] = (bits & 1) ? 0 : 1;  // even parity = 1
  }
}

void emu88::reset(void) {
  memset(regs, 0, sizeof(regs));
  memset(regs_hi, 0, sizeof(regs_hi));
  memset(sregs, 0, sizeof(sregs));
  sregs[seg_CS] = 0xFFFF;  // 8088 starts at FFFF:0000
  ip = 0x0000;
  flags = 0x0002;
  eflags_hi = 0;
  halted = false;
  int_pending = false;
  cycles = 0;
  op_size_32 = false;
  addr_size_32 = false;
  gdtr_base = 0; gdtr_limit = 0;
  idtr_base = 0; idtr_limit = 0x3FF;
  cr0 = 0;
}

//=============================================================================
// I/O ports - virtual, override in subclass
//=============================================================================

void emu88::port_out(emu88_uint16 port, emu88_uint8 value) {
  (void)port;
  (void)value;
}

emu88_uint8 emu88::port_in(emu88_uint16 port) {
  (void)port;
  return 0xFF;
}

void emu88::port_out16(emu88_uint16 port, emu88_uint16 value) {
  port_out(port, value & 0xFF);
  port_out(port + 1, (value >> 8) & 0xFF);
}

emu88_uint16 emu88::port_in16(emu88_uint16 port) {
  return port_in(port) | (emu88_uint16(port_in(port + 1)) << 8);
}

//=============================================================================
// Interrupt support
//=============================================================================

void emu88::do_interrupt(emu88_uint8 vector) {
  push_word(flags);
  push_word(sregs[seg_CS]);
  push_word(ip);
  clear_flag(FLAG_IF);
  clear_flag(FLAG_TF);
  emu88_uint32 vec_addr = emu88_uint32(vector) * 4;
  ip = mem->fetch_mem16(vec_addr);
  sregs[seg_CS] = mem->fetch_mem16(vec_addr + 2);
}

void emu88::request_int(emu88_uint8 vector) {
  int_pending = true;
  int_vector = vector;
}

bool emu88::check_interrupts(void) {
  if (int_pending && get_flag(FLAG_IF)) {
    int_pending = false;
    halted = false;
    do_interrupt(int_vector);
    return true;
  }
  return false;
}

void emu88::halt_cpu(void) {
  halted = true;
}

void emu88::unimplemented_opcode(emu88_uint8 opcode) {
  emu88_fatal("Unimplemented opcode 0x%02X at %04X:%04X", opcode, sregs[seg_CS], ip - 1);
  halted = true;
}

//=============================================================================
// Register access: 8-bit
//=============================================================================

emu88_uint8 emu88::get_reg8(emu88_uint8 r) const {
  // 0=AL, 1=CL, 2=DL, 3=BL, 4=AH, 5=CH, 6=DH, 7=BH
  if (r < 4) {
    return regs[r] & 0xFF;
  }
  return (regs[r - 4] >> 8) & 0xFF;
}

void emu88::set_reg8(emu88_uint8 r, emu88_uint8 val) {
  if (r < 4) {
    regs[r] = (regs[r] & 0xFF00) | val;
  } else {
    regs[r - 4] = (regs[r - 4] & 0x00FF) | (emu88_uint16(val) << 8);
  }
}

//=============================================================================
// Memory access (segment-aware)
//=============================================================================

emu88_uint16 emu88::default_segment(void) const {
  if (seg_override >= 0)
    return sregs[seg_override];
  return sregs[seg_DS];
}

emu88_uint8 emu88::fetch_byte(emu88_uint16 seg, emu88_uint16 off) {
  return mem->fetch_mem(effective_address(seg, off));
}

void emu88::store_byte(emu88_uint16 seg, emu88_uint16 off, emu88_uint8 val) {
  mem->store_mem(effective_address(seg, off), val);
}

emu88_uint16 emu88::fetch_word(emu88_uint16 seg, emu88_uint16 off) {
  return mem->fetch_mem16(effective_address(seg, off));
}

void emu88::store_word(emu88_uint16 seg, emu88_uint16 off, emu88_uint16 val) {
  mem->store_mem16(effective_address(seg, off), val);
}

emu88_uint32 emu88::fetch_dword(emu88_uint16 seg, emu88_uint16 off) {
  return mem->fetch_mem32(effective_address(seg, off));
}

void emu88::store_dword(emu88_uint16 seg, emu88_uint16 off, emu88_uint32 val) {
  mem->store_mem32(effective_address(seg, off), val);
}

//=============================================================================
// Instruction stream
//=============================================================================

emu88_uint8 emu88::fetch_ip_byte(void) {
  emu88_uint8 val = fetch_byte(sregs[seg_CS], ip);
  ip++;
  return val;
}

emu88_uint16 emu88::fetch_ip_word(void) {
  emu88_uint8 lo = fetch_ip_byte();
  emu88_uint8 hi = fetch_ip_byte();
  return EMU88_MK16(lo, hi);
}

emu88_uint32 emu88::fetch_ip_dword(void) {
  emu88_uint16 lo = fetch_ip_word();
  emu88_uint16 hi = fetch_ip_word();
  return EMU88_MK32(lo, hi);
}

//=============================================================================
// Stack operations
//=============================================================================

void emu88::push_word(emu88_uint16 val) {
  regs[reg_SP] -= 2;
  store_word(sregs[seg_SS], regs[reg_SP], val);
}

emu88_uint16 emu88::pop_word(void) {
  emu88_uint16 val = fetch_word(sregs[seg_SS], regs[reg_SP]);
  regs[reg_SP] += 2;
  return val;
}

void emu88::push_dword(emu88_uint32 val) {
  regs[reg_SP] -= 4;
  store_dword(sregs[seg_SS], regs[reg_SP], val);
}

emu88_uint32 emu88::pop_dword(void) {
  emu88_uint32 val = fetch_dword(sregs[seg_SS], regs[reg_SP]);
  regs[reg_SP] += 4;
  return val;
}

//=============================================================================
// ModR/M decoding
//=============================================================================

emu88::modrm_result emu88::decode_modrm(emu88_uint8 modrm) {
  if (addr_size_32)
    return decode_modrm_32(modrm);

  modrm_result mr;
  mr.mod_field = (modrm >> 6) & 3;
  mr.reg_field = (modrm >> 3) & 7;
  mr.rm_field = modrm & 7;
  mr.is_register = (mr.mod_field == 3);
  mr.seg = 0;
  mr.offset = 0;

  if (mr.is_register) {
    return mr;
  }

  // Default segment depends on r/m and mod
  emu88_uint16 base_seg = sregs[seg_DS];
  emu88_uint16 off = 0;

  switch (mr.rm_field) {
  case 0: off = regs[reg_BX] + regs[reg_SI]; break;
  case 1: off = regs[reg_BX] + regs[reg_DI]; break;
  case 2: off = regs[reg_BP] + regs[reg_SI]; base_seg = sregs[seg_SS]; break;
  case 3: off = regs[reg_BP] + regs[reg_DI]; base_seg = sregs[seg_SS]; break;
  case 4: off = regs[reg_SI]; break;
  case 5: off = regs[reg_DI]; break;
  case 6:
    if (mr.mod_field == 0) {
      // direct address
      off = fetch_ip_word();
    } else {
      off = regs[reg_BP];
      base_seg = sregs[seg_SS];
    }
    break;
  case 7: off = regs[reg_BX]; break;
  }

  if (mr.mod_field == 1) {
    emu88_int8 disp8 = (emu88_int8)fetch_ip_byte();
    off += disp8;
  } else if (mr.mod_field == 2) {
    emu88_uint16 disp16 = fetch_ip_word();
    off += disp16;
  }

  // Apply segment override if active
  mr.seg = (seg_override >= 0) ? sregs[seg_override] : base_seg;
  mr.offset = off;
  return mr;
}

emu88_uint8 emu88::get_rm8(const modrm_result &mr) {
  if (mr.is_register)
    return get_reg8(mr.rm_field);
  return fetch_byte(mr.seg, mr.offset);
}

void emu88::set_rm8(const modrm_result &mr, emu88_uint8 val) {
  if (mr.is_register)
    set_reg8(mr.rm_field, val);
  else
    store_byte(mr.seg, mr.offset, val);
}

emu88_uint16 emu88::get_rm16(const modrm_result &mr) {
  if (mr.is_register)
    return regs[mr.rm_field];
  return fetch_word(mr.seg, mr.offset);
}

void emu88::set_rm16(const modrm_result &mr, emu88_uint16 val) {
  if (mr.is_register)
    regs[mr.rm_field] = val;
  else
    store_word(mr.seg, mr.offset, val);
}

emu88_uint32 emu88::get_rm32(const modrm_result &mr) {
  if (mr.is_register)
    return get_reg32(mr.rm_field);
  return fetch_dword(mr.seg, mr.offset);
}

void emu88::set_rm32(const modrm_result &mr, emu88_uint32 val) {
  if (mr.is_register)
    set_reg32(mr.rm_field, val);
  else
    store_dword(mr.seg, mr.offset, val);
}

//=============================================================================
// 32-bit addressing mode decoding (386+, for 0x67 prefix)
//=============================================================================

emu88::modrm_result emu88::decode_modrm_32(emu88_uint8 modrm) {
  modrm_result mr;
  mr.mod_field = (modrm >> 6) & 3;
  mr.reg_field = (modrm >> 3) & 7;
  mr.rm_field = modrm & 7;
  mr.is_register = (mr.mod_field == 3);
  mr.seg = 0;
  mr.offset = 0;

  if (mr.is_register)
    return mr;

  emu88_uint32 off = 0;
  emu88_uint16 base_seg = sregs[seg_DS];

  if (mr.rm_field == 4) {
    // SIB byte follows
    emu88_uint8 sib = fetch_ip_byte();
    emu88_uint8 scale = (sib >> 6) & 3;
    emu88_uint8 index = (sib >> 3) & 7;
    emu88_uint8 base = sib & 7;

    if (base == 5 && mr.mod_field == 0) {
      off = fetch_ip_dword();
    } else {
      off = get_reg32(base);
      if (base == 4 || base == 5) base_seg = sregs[seg_SS];
    }
    if (index != 4) {
      off += get_reg32(index) << scale;
    }
  } else if (mr.rm_field == 5 && mr.mod_field == 0) {
    off = fetch_ip_dword();
  } else {
    off = get_reg32(mr.rm_field);
    if (mr.rm_field == 4 || mr.rm_field == 5)
      base_seg = sregs[seg_SS];
  }

  if (mr.mod_field == 1) {
    off += (emu88_int32)(emu88_int8)fetch_ip_byte();
  } else if (mr.mod_field == 2) {
    off += fetch_ip_dword();
  }

  mr.seg = (seg_override >= 0) ? sregs[seg_override] : base_seg;
  mr.offset = off & 0xFFFF;  // real mode: wrap to 16-bit
  return mr;
}

//=============================================================================
// Flags computation
//=============================================================================

void emu88::set_flags_zsp8(emu88_uint8 val) {
  set_flag_val(FLAG_ZF, val == 0);
  set_flag_val(FLAG_SF, (val & 0x80) != 0);
  set_flag_val(FLAG_PF, parity_table[val]);
}

void emu88::set_flags_zsp16(emu88_uint16 val) {
  set_flag_val(FLAG_ZF, val == 0);
  set_flag_val(FLAG_SF, (val & 0x8000) != 0);
  set_flag_val(FLAG_PF, parity_table[val & 0xFF]);
}

void emu88::set_flags_add8(emu88_uint8 a, emu88_uint8 b, emu88_uint8 carry) {
  emu88_uint16 result = emu88_uint16(a) + emu88_uint16(b) + carry;
  emu88_uint8 r8 = result & 0xFF;
  set_flags_zsp8(r8);
  set_flag_val(FLAG_CF, result > 0xFF);
  set_flag_val(FLAG_AF, ((a ^ b ^ r8) & 0x10) != 0);
  set_flag_val(FLAG_OF, ((a ^ r8) & (b ^ r8) & 0x80) != 0);
}

void emu88::set_flags_add16(emu88_uint16 a, emu88_uint16 b, emu88_uint16 carry) {
  emu88_uint32 result = emu88_uint32(a) + emu88_uint32(b) + carry;
  emu88_uint16 r16 = result & 0xFFFF;
  set_flags_zsp16(r16);
  set_flag_val(FLAG_CF, result > 0xFFFF);
  set_flag_val(FLAG_AF, ((a ^ b ^ r16) & 0x10) != 0);
  set_flag_val(FLAG_OF, ((a ^ r16) & (b ^ r16) & 0x8000) != 0);
}

void emu88::set_flags_sub8(emu88_uint8 a, emu88_uint8 b, emu88_uint8 borrow) {
  emu88_uint16 result = emu88_uint16(a) - emu88_uint16(b) - borrow;
  emu88_uint8 r8 = result & 0xFF;
  set_flags_zsp8(r8);
  set_flag_val(FLAG_CF, result > 0xFF);
  set_flag_val(FLAG_AF, ((a ^ b ^ r8) & 0x10) != 0);
  set_flag_val(FLAG_OF, ((a ^ b) & (a ^ r8) & 0x80) != 0);
}

void emu88::set_flags_sub16(emu88_uint16 a, emu88_uint16 b, emu88_uint16 borrow) {
  emu88_uint32 result = emu88_uint32(a) - emu88_uint32(b) - borrow;
  emu88_uint16 r16 = result & 0xFFFF;
  set_flags_zsp16(r16);
  set_flag_val(FLAG_CF, result > 0xFFFF);
  set_flag_val(FLAG_AF, ((a ^ b ^ r16) & 0x10) != 0);
  set_flag_val(FLAG_OF, ((a ^ b) & (a ^ r16) & 0x8000) != 0);
}

void emu88::set_flags_logic8(emu88_uint8 result) {
  set_flags_zsp8(result);
  clear_flag(FLAG_CF);
  clear_flag(FLAG_OF);
  clear_flag(FLAG_AF);
}

void emu88::set_flags_logic16(emu88_uint16 result) {
  set_flags_zsp16(result);
  clear_flag(FLAG_CF);
  clear_flag(FLAG_OF);
  clear_flag(FLAG_AF);
}

//--- 32-bit flag computation (386+) ---

void emu88::set_flags_zsp32(emu88_uint32 val) {
  set_flag_val(FLAG_ZF, val == 0);
  set_flag_val(FLAG_SF, (val & 0x80000000) != 0);
  set_flag_val(FLAG_PF, parity_table[val & 0xFF]);
}

void emu88::set_flags_add32(emu88_uint32 a, emu88_uint32 b, emu88_uint32 carry) {
  emu88_uint64 result = emu88_uint64(a) + emu88_uint64(b) + carry;
  emu88_uint32 r32 = (emu88_uint32)result;
  set_flags_zsp32(r32);
  set_flag_val(FLAG_CF, result > 0xFFFFFFFF);
  set_flag_val(FLAG_AF, ((a ^ b ^ r32) & 0x10) != 0);
  set_flag_val(FLAG_OF, ((a ^ r32) & (b ^ r32) & 0x80000000) != 0);
}

void emu88::set_flags_sub32(emu88_uint32 a, emu88_uint32 b, emu88_uint32 borrow) {
  emu88_uint64 result = emu88_uint64(a) - emu88_uint64(b) - borrow;
  emu88_uint32 r32 = (emu88_uint32)result;
  set_flags_zsp32(r32);
  set_flag_val(FLAG_CF, result > 0xFFFFFFFF);
  set_flag_val(FLAG_AF, ((a ^ b ^ r32) & 0x10) != 0);
  set_flag_val(FLAG_OF, ((a ^ b) & (a ^ r32) & 0x80000000) != 0);
}

void emu88::set_flags_logic32(emu88_uint32 result) {
  set_flags_zsp32(result);
  clear_flag(FLAG_CF);
  clear_flag(FLAG_OF);
  clear_flag(FLAG_AF);
}

//=============================================================================
// ALU helpers
//=============================================================================

emu88_uint8 emu88::alu_add8(emu88_uint8 a, emu88_uint8 b, emu88_uint8 carry) {
  set_flags_add8(a, b, carry);
  return (a + b + carry) & 0xFF;
}

emu88_uint16 emu88::alu_add16(emu88_uint16 a, emu88_uint16 b, emu88_uint16 carry) {
  set_flags_add16(a, b, carry);
  return (a + b + carry) & 0xFFFF;
}

emu88_uint8 emu88::alu_sub8(emu88_uint8 a, emu88_uint8 b, emu88_uint8 borrow) {
  set_flags_sub8(a, b, borrow);
  return (a - b - borrow) & 0xFF;
}

emu88_uint16 emu88::alu_sub16(emu88_uint16 a, emu88_uint16 b, emu88_uint16 borrow) {
  set_flags_sub16(a, b, borrow);
  return (a - b - borrow) & 0xFFFF;
}

emu88_uint8 emu88::alu_inc8(emu88_uint8 val) {
  emu88_uint8 result = val + 1;
  set_flags_zsp8(result);
  set_flag_val(FLAG_AF, (val & 0x0F) == 0x0F);
  set_flag_val(FLAG_OF, val == 0x7F);
  // CF not affected by INC
  return result;
}

emu88_uint8 emu88::alu_dec8(emu88_uint8 val) {
  emu88_uint8 result = val - 1;
  set_flags_zsp8(result);
  set_flag_val(FLAG_AF, (val & 0x0F) == 0x00);
  set_flag_val(FLAG_OF, val == 0x80);
  // CF not affected by DEC
  return result;
}

emu88_uint16 emu88::alu_inc16(emu88_uint16 val) {
  return val + 1;
  // INC r16 does not affect any flags on 8088
}

emu88_uint16 emu88::alu_dec16(emu88_uint16 val) {
  return val - 1;
  // DEC r16 does not affect any flags on 8088
}

//--- 32-bit ALU helpers (386+) ---

emu88_uint32 emu88::alu_add32(emu88_uint32 a, emu88_uint32 b, emu88_uint32 carry) {
  set_flags_add32(a, b, carry);
  return (emu88_uint32)(emu88_uint64(a) + b + carry);
}

emu88_uint32 emu88::alu_sub32(emu88_uint32 a, emu88_uint32 b, emu88_uint32 borrow) {
  set_flags_sub32(a, b, borrow);
  return (emu88_uint32)(emu88_uint64(a) - b - borrow);
}

emu88_uint32 emu88::alu_inc32(emu88_uint32 val) {
  emu88_uint32 result = val + 1;
  set_flags_zsp32(result);
  set_flag_val(FLAG_AF, (val & 0x0F) == 0x0F);
  set_flag_val(FLAG_OF, val == 0x7FFFFFFF);
  return result;
}

emu88_uint32 emu88::alu_dec32(emu88_uint32 val) {
  emu88_uint32 result = val - 1;
  set_flags_zsp32(result);
  set_flag_val(FLAG_AF, (val & 0x0F) == 0x00);
  set_flag_val(FLAG_OF, val == 0x80000000);
  return result;
}

//=============================================================================
// ALU dispatch (op: ADD=0, OR=1, ADC=2, SBB=3, AND=4, SUB=5, XOR=6, CMP=7)
//=============================================================================

emu88_uint8 emu88::do_alu8(emu88_uint8 op, emu88_uint8 a, emu88_uint8 b) {
  switch (op) {
  case 0: return alu_add8(a, b, 0);
  case 1: { emu88_uint8 r = a | b; set_flags_logic8(r); return r; }
  case 2: return alu_add8(a, b, get_flag(FLAG_CF) ? 1 : 0);
  case 3: return alu_sub8(a, b, get_flag(FLAG_CF) ? 1 : 0);
  case 4: { emu88_uint8 r = a & b; set_flags_logic8(r); return r; }
  case 5: return alu_sub8(a, b, 0);
  case 6: { emu88_uint8 r = a ^ b; set_flags_logic8(r); return r; }
  case 7: alu_sub8(a, b, 0); return a;  // CMP: flags only
  }
  return a;
}

emu88_uint16 emu88::do_alu16(emu88_uint8 op, emu88_uint16 a, emu88_uint16 b) {
  switch (op) {
  case 0: return alu_add16(a, b, 0);
  case 1: { emu88_uint16 r = a | b; set_flags_logic16(r); return r; }
  case 2: return alu_add16(a, b, get_flag(FLAG_CF) ? 1 : 0);
  case 3: return alu_sub16(a, b, get_flag(FLAG_CF) ? 1 : 0);
  case 4: { emu88_uint16 r = a & b; set_flags_logic16(r); return r; }
  case 5: return alu_sub16(a, b, 0);
  case 6: { emu88_uint16 r = a ^ b; set_flags_logic16(r); return r; }
  case 7: alu_sub16(a, b, 0); return a;
  }
  return a;
}

emu88_uint32 emu88::do_alu32(emu88_uint8 op, emu88_uint32 a, emu88_uint32 b) {
  switch (op) {
  case 0: return alu_add32(a, b, 0);
  case 1: { emu88_uint32 r = a | b; set_flags_logic32(r); return r; }
  case 2: return alu_add32(a, b, get_flag(FLAG_CF) ? 1 : 0);
  case 3: return alu_sub32(a, b, get_flag(FLAG_CF) ? 1 : 0);
  case 4: { emu88_uint32 r = a & b; set_flags_logic32(r); return r; }
  case 5: return alu_sub32(a, b, 0);
  case 6: { emu88_uint32 r = a ^ b; set_flags_logic32(r); return r; }
  case 7: alu_sub32(a, b, 0); return a;
  }
  return a;
}

//=============================================================================
// Shift/rotate operations
//=============================================================================

emu88_uint8 emu88::do_shift8(emu88_uint8 op, emu88_uint8 val, emu88_uint8 count) {
  count &= 31;  // 286+ masks shift count to 5 bits
  if (count == 0) return val;
  emu88_uint8 result = val;
  emu88_uint8 cf;
  for (emu88_uint8 i = 0; i < count; i++) {
    switch (op) {
    case 0: // ROL
      cf = (result >> 7) & 1;
      result = (result << 1) | cf;
      set_flag_val(FLAG_CF, cf);
      break;
    case 1: // ROR
      cf = result & 1;
      result = (result >> 1) | (cf << 7);
      set_flag_val(FLAG_CF, cf);
      break;
    case 2: // RCL
      cf = (result >> 7) & 1;
      result = (result << 1) | (get_flag(FLAG_CF) ? 1 : 0);
      set_flag_val(FLAG_CF, cf);
      break;
    case 3: // RCR
      cf = result & 1;
      result = (result >> 1) | (get_flag(FLAG_CF) ? 0x80 : 0);
      set_flag_val(FLAG_CF, cf);
      break;
    case 4: // SHL/SAL
      cf = (result >> 7) & 1;
      result <<= 1;
      set_flag_val(FLAG_CF, cf);
      break;
    case 5: // SHR
      cf = result & 1;
      result >>= 1;
      set_flag_val(FLAG_CF, cf);
      break;
    case 6: // (undefined, acts like SHL on 8088)
      cf = (result >> 7) & 1;
      result <<= 1;
      set_flag_val(FLAG_CF, cf);
      break;
    case 7: // SAR
      cf = result & 1;
      result = (result >> 1) | (result & 0x80);
      set_flag_val(FLAG_CF, cf);
      break;
    }
  }
  if (count == 1) {
    // OF set only for single-bit shifts
    switch (op) {
    case 0: case 2: case 4: case 6: // left shifts/rotates
      set_flag_val(FLAG_OF, ((result >> 7) & 1) != get_flag(FLAG_CF));
      break;
    case 1: case 3: // right rotates
      set_flag_val(FLAG_OF, ((result >> 7) ^ ((result >> 6) & 1)) != 0);
      break;
    case 5: case 7: // right shifts
      set_flag_val(FLAG_OF, (op == 5) ? ((val & 0x80) != 0) : false);
      break;
    }
  }
  if (op >= 4) {
    set_flags_zsp8(result);
  }
  return result;
}

emu88_uint16 emu88::do_shift16(emu88_uint8 op, emu88_uint16 val, emu88_uint8 count) {
  count &= 31;  // 286+ masks shift count to 5 bits
  if (count == 0) return val;
  emu88_uint16 result = val;
  emu88_uint8 cf;
  for (emu88_uint8 i = 0; i < count; i++) {
    switch (op) {
    case 0: // ROL
      cf = (result >> 15) & 1;
      result = (result << 1) | cf;
      set_flag_val(FLAG_CF, cf);
      break;
    case 1: // ROR
      cf = result & 1;
      result = (result >> 1) | (emu88_uint16(cf) << 15);
      set_flag_val(FLAG_CF, cf);
      break;
    case 2: // RCL
      cf = (result >> 15) & 1;
      result = (result << 1) | (get_flag(FLAG_CF) ? 1 : 0);
      set_flag_val(FLAG_CF, cf);
      break;
    case 3: // RCR
      cf = result & 1;
      result = (result >> 1) | (get_flag(FLAG_CF) ? 0x8000 : 0);
      set_flag_val(FLAG_CF, cf);
      break;
    case 4: // SHL
      cf = (result >> 15) & 1;
      result <<= 1;
      set_flag_val(FLAG_CF, cf);
      break;
    case 5: // SHR
      cf = result & 1;
      result >>= 1;
      set_flag_val(FLAG_CF, cf);
      break;
    case 6: // (undefined)
      cf = (result >> 15) & 1;
      result <<= 1;
      set_flag_val(FLAG_CF, cf);
      break;
    case 7: // SAR
      cf = result & 1;
      result = (result >> 1) | (result & 0x8000);
      set_flag_val(FLAG_CF, cf);
      break;
    }
  }
  if (count == 1) {
    switch (op) {
    case 0: case 2: case 4: case 6:
      set_flag_val(FLAG_OF, ((result >> 15) & 1) != get_flag(FLAG_CF));
      break;
    case 1: case 3:
      set_flag_val(FLAG_OF, ((result >> 15) ^ ((result >> 14) & 1)) != 0);
      break;
    case 5: case 7:
      set_flag_val(FLAG_OF, (op == 5) ? ((val & 0x8000) != 0) : false);
      break;
    }
  }
  if (op >= 4) {
    set_flags_zsp16(result);
  }
  return result;
}

emu88_uint32 emu88::do_shift32(emu88_uint8 op, emu88_uint32 val, emu88_uint8 count) {
  count &= 31;  // 386 masks shift count to 5 bits
  if (count == 0) return val;
  emu88_uint32 result = val;
  emu88_uint8 cf;
  for (emu88_uint8 i = 0; i < count; i++) {
    switch (op) {
    case 0: // ROL
      cf = (result >> 31) & 1;
      result = (result << 1) | cf;
      set_flag_val(FLAG_CF, cf);
      break;
    case 1: // ROR
      cf = result & 1;
      result = (result >> 1) | (emu88_uint32(cf) << 31);
      set_flag_val(FLAG_CF, cf);
      break;
    case 2: // RCL
      cf = (result >> 31) & 1;
      result = (result << 1) | (get_flag(FLAG_CF) ? 1 : 0);
      set_flag_val(FLAG_CF, cf);
      break;
    case 3: // RCR
      cf = result & 1;
      result = (result >> 1) | (get_flag(FLAG_CF) ? 0x80000000u : 0);
      set_flag_val(FLAG_CF, cf);
      break;
    case 4: case 6: // SHL
      cf = (result >> 31) & 1;
      result <<= 1;
      set_flag_val(FLAG_CF, cf);
      break;
    case 5: // SHR
      cf = result & 1;
      result >>= 1;
      set_flag_val(FLAG_CF, cf);
      break;
    case 7: // SAR
      cf = result & 1;
      result = (result >> 1) | (result & 0x80000000u);
      set_flag_val(FLAG_CF, cf);
      break;
    }
  }
  if (count == 1) {
    switch (op) {
    case 0: case 2: case 4: case 6:
      set_flag_val(FLAG_OF, ((result >> 31) & 1) != (emu88_uint32)get_flag(FLAG_CF));
      break;
    case 1: case 3:
      set_flag_val(FLAG_OF, ((result >> 31) ^ ((result >> 30) & 1)) != 0);
      break;
    case 5: case 7:
      set_flag_val(FLAG_OF, (op == 5) ? ((val & 0x80000000u) != 0) : false);
      break;
    }
  }
  if (op >= 4) {
    set_flags_zsp32(result);
  }
  return result;
}

//=============================================================================
// Group instruction helpers
//=============================================================================

void emu88::execute_grp1_rm8(const modrm_result &mr, emu88_uint8 imm) {
  emu88_uint8 op = mr.reg_field;
  emu88_uint8 val = get_rm8(mr);
  emu88_uint8 result = do_alu8(op, val, imm);
  if (op != 7) // CMP doesn't store result
    set_rm8(mr, result);
}

void emu88::execute_grp1_rm16(const modrm_result &mr, emu88_uint16 imm) {
  emu88_uint8 op = mr.reg_field;
  emu88_uint16 val = get_rm16(mr);
  emu88_uint16 result = do_alu16(op, val, imm);
  if (op != 7)
    set_rm16(mr, result);
}

void emu88::execute_grp2_rm8(const modrm_result &mr, emu88_uint8 count) {
  emu88_uint8 val = get_rm8(mr);
  emu88_uint8 result = do_shift8(mr.reg_field, val, count);
  set_rm8(mr, result);
}

void emu88::execute_grp2_rm16(const modrm_result &mr, emu88_uint8 count) {
  emu88_uint16 val = get_rm16(mr);
  emu88_uint16 result = do_shift16(mr.reg_field, val, count);
  set_rm16(mr, result);
}

void emu88::execute_grp3_rm8(emu88_uint8 modrm_byte) {
  modrm_result mr = decode_modrm(modrm_byte);
  emu88_uint8 val = get_rm8(mr);
  switch (mr.reg_field) {
  case 0: case 1: { // TEST r/m8, imm8
    emu88_uint8 imm = fetch_ip_byte();
    set_flags_logic8(val & imm);
    break;
  }
  case 2: // NOT r/m8
    set_rm8(mr, ~val);
    break;
  case 3: { // NEG r/m8
    set_flags_sub8(0, val, 0);
    set_rm8(mr, (~val) + 1);
    set_flag_val(FLAG_CF, val != 0);
    break;
  }
  case 4: { // MUL r/m8 (70-77 cycles)
    emu88_uint16 result = emu88_uint16(regs[reg_AX] & 0xFF) * val;
    regs[reg_AX] = result;
    bool of_cf = (result & 0xFF00) != 0;
    set_flag_val(FLAG_CF, of_cf);
    set_flag_val(FLAG_OF, of_cf);
    cycles += 54;  // 70 total - 16 from base
    break;
  }
  case 5: { // IMUL r/m8 (80-98 cycles)
    emu88_int16 result = emu88_int16(emu88_int8(regs[reg_AX] & 0xFF)) * emu88_int8(val);
    regs[reg_AX] = emu88_uint16(result);
    bool of_cf = (result < -128 || result > 127);
    set_flag_val(FLAG_CF, of_cf);
    set_flag_val(FLAG_OF, of_cf);
    cycles += 64;  // 80 total
    break;
  }
  case 6: { // DIV r/m8 (80-90 cycles)
    if (val == 0) {
      do_interrupt(0); // divide by zero
      return;
    }
    emu88_uint16 dividend = regs[reg_AX];
    emu88_uint16 quotient = dividend / val;
    if (quotient > 0xFF) {
      do_interrupt(0);
      return;
    }
    emu88_uint8 remainder = dividend % val;
    set_reg8(reg_AL, quotient & 0xFF);
    set_reg8(reg_AH, remainder);
    cycles += 64;  // 80 total
    break;
  }
  case 7: { // IDIV r/m8 (101-112 cycles)
    if (val == 0) {
      do_interrupt(0);
      return;
    }
    emu88_int16 dividend = emu88_int16(regs[reg_AX]);
    emu88_int16 divisor = emu88_int8(val);
    emu88_int16 quotient = dividend / divisor;
    if (quotient > 127 || quotient < -128) {
      do_interrupt(0);
      return;
    }
    emu88_int8 remainder = dividend % divisor;
    set_reg8(reg_AL, emu88_uint8(quotient));
    set_reg8(reg_AH, emu88_uint8(remainder));
    cycles += 85;  // 101 total
    break;
  }
  }
}

void emu88::execute_grp3_rm16(emu88_uint8 modrm_byte) {
  modrm_result mr = decode_modrm(modrm_byte);
  emu88_uint16 val = get_rm16(mr);
  switch (mr.reg_field) {
  case 0: case 1: { // TEST r/m16, imm16
    emu88_uint16 imm = fetch_ip_word();
    set_flags_logic16(val & imm);
    break;
  }
  case 2: // NOT r/m16
    set_rm16(mr, ~val);
    break;
  case 3: { // NEG r/m16
    set_flags_sub16(0, val, 0);
    set_rm16(mr, (~val) + 1);
    set_flag_val(FLAG_CF, val != 0);
    break;
  }
  case 4: { // MUL r/m16 (118-133 cycles)
    emu88_uint32 result = emu88_uint32(regs[reg_AX]) * val;
    regs[reg_AX] = result & 0xFFFF;
    regs[reg_DX] = (result >> 16) & 0xFFFF;
    bool of_cf = regs[reg_DX] != 0;
    set_flag_val(FLAG_CF, of_cf);
    set_flag_val(FLAG_OF, of_cf);
    cycles += 105;  // 118 total approx
    break;
  }
  case 5: { // IMUL r/m16 (128-154 cycles)
    emu88_int32 result = emu88_int32(emu88_int16(regs[reg_AX])) * emu88_int16(val);
    regs[reg_AX] = emu88_uint16(result);
    regs[reg_DX] = emu88_uint16(result >> 16);
    bool of_cf = (result < -32768 || result > 32767);
    set_flag_val(FLAG_CF, of_cf);
    set_flag_val(FLAG_OF, of_cf);
    cycles += 118;  // 128 total approx
    break;
  }
  case 6: { // DIV r/m16 (144-162 cycles)
    if (val == 0) {
      do_interrupt(0);
      return;
    }
    emu88_uint32 dividend = (emu88_uint32(regs[reg_DX]) << 16) | regs[reg_AX];
    emu88_uint32 quotient = dividend / val;
    if (quotient > 0xFFFF) {
      do_interrupt(0);
      return;
    }
    emu88_uint16 remainder = dividend % val;
    regs[reg_AX] = quotient & 0xFFFF;
    regs[reg_DX] = remainder;
    cycles += 130;  // 144 total approx
    break;
  }
  case 7: { // IDIV r/m16 (165-184 cycles)
    if (val == 0) {
      do_interrupt(0);
      return;
    }
    emu88_int32 dividend = emu88_int32((emu88_uint32(regs[reg_DX]) << 16) | regs[reg_AX]);
    emu88_int32 divisor = emu88_int16(val);
    emu88_int32 quotient = dividend / divisor;
    if (quotient > 32767 || quotient < -32768) {
      do_interrupt(0);
      return;
    }
    emu88_int16 remainder = dividend % divisor;
    regs[reg_AX] = emu88_uint16(quotient);
    regs[reg_DX] = emu88_uint16(remainder);
    cycles += 150;  // 165 total approx
    break;
  }
  }
}

void emu88::execute_grp4_rm8(emu88_uint8 modrm_byte) {
  modrm_result mr = decode_modrm(modrm_byte);
  emu88_uint8 val = get_rm8(mr);
  switch (mr.reg_field) {
  case 0: // INC r/m8
    set_rm8(mr, alu_inc8(val));
    break;
  case 1: // DEC r/m8
    set_rm8(mr, alu_dec8(val));
    break;
  default:
    do_interrupt(6);  // #UD - Invalid Opcode
    break;
  }
}

void emu88::execute_grp5_rm16(emu88_uint8 modrm_byte) {
  modrm_result mr = decode_modrm(modrm_byte);
  switch (mr.reg_field) {
  case 0: { // INC r/m16
    emu88_uint16 val = get_rm16(mr);
    emu88_uint16 result = val + 1;
    set_flags_zsp16(result);
    set_flag_val(FLAG_AF, (val & 0x0F) == 0x0F);
    set_flag_val(FLAG_OF, val == 0x7FFF);
    set_rm16(mr, result);
    break;
  }
  case 1: { // DEC r/m16
    emu88_uint16 val = get_rm16(mr);
    emu88_uint16 result = val - 1;
    set_flags_zsp16(result);
    set_flag_val(FLAG_AF, (val & 0x0F) == 0x00);
    set_flag_val(FLAG_OF, val == 0x8000);
    set_rm16(mr, result);
    break;
  }
  case 2: { // CALL r/m16 (near indirect)
    emu88_uint16 target = get_rm16(mr);
    push_word(ip);
    ip = target;
    break;
  }
  case 3: { // CALL m16:16 (far indirect)
    emu88_uint16 off = fetch_word(mr.seg, mr.offset);
    emu88_uint16 seg = fetch_word(mr.seg, mr.offset + 2);
    push_word(sregs[seg_CS]);
    push_word(ip);
    sregs[seg_CS] = seg;
    ip = off;
    break;
  }
  case 4: { // JMP r/m16 (near indirect)
    ip = get_rm16(mr);
    break;
  }
  case 5: { // JMP m16:16 (far indirect)
    emu88_uint16 off = fetch_word(mr.seg, mr.offset);
    emu88_uint16 seg = fetch_word(mr.seg, mr.offset + 2);
    sregs[seg_CS] = seg;
    ip = off;
    break;
  }
  case 6: { // PUSH r/m16
    push_word(get_rm16(mr));
    break;
  }
  default:
    unimplemented_opcode(0xFF);
    break;
  }
}

//=============================================================================
// 32-bit group instruction helpers (386+)
//=============================================================================

void emu88::execute_grp1_rm32(const modrm_result &mr, emu88_uint32 imm) {
  emu88_uint8 op = mr.reg_field;
  emu88_uint32 val = get_rm32(mr);
  emu88_uint32 result = do_alu32(op, val, imm);
  if (op != 7) set_rm32(mr, result);
}

void emu88::execute_grp2_rm32(const modrm_result &mr, emu88_uint8 count) {
  emu88_uint32 val = get_rm32(mr);
  emu88_uint32 result = do_shift32(mr.reg_field, val, count);
  set_rm32(mr, result);
}

void emu88::execute_grp3_rm32(emu88_uint8 modrm_byte) {
  modrm_result mr = decode_modrm(modrm_byte);
  emu88_uint32 val = get_rm32(mr);
  switch (mr.reg_field) {
  case 0: case 1: { // TEST r/m32, imm32
    emu88_uint32 imm = fetch_ip_dword();
    set_flags_logic32(val & imm);
    break;
  }
  case 2: // NOT r/m32
    set_rm32(mr, ~val);
    break;
  case 3: { // NEG r/m32
    set_flags_sub32(0, val, 0);
    set_rm32(mr, (~val) + 1);
    set_flag_val(FLAG_CF, val != 0);
    break;
  }
  case 4: { // MUL r/m32 — EDX:EAX = EAX * r/m32
    emu88_uint64 result = emu88_uint64(get_reg32(reg_AX)) * val;
    set_reg32(reg_AX, (emu88_uint32)result);
    set_reg32(reg_DX, (emu88_uint32)(result >> 32));
    bool of_cf = get_reg32(reg_DX) != 0;
    set_flag_val(FLAG_CF, of_cf);
    set_flag_val(FLAG_OF, of_cf);
    break;
  }
  case 5: { // IMUL r/m32
    emu88_int64 result = emu88_int64(emu88_int32(get_reg32(reg_AX))) * emu88_int32(val);
    set_reg32(reg_AX, (emu88_uint32)result);
    set_reg32(reg_DX, (emu88_uint32)(result >> 32));
    bool of_cf = (result < -(emu88_int64)0x80000000LL || result > 0x7FFFFFFFLL);
    set_flag_val(FLAG_CF, of_cf);
    set_flag_val(FLAG_OF, of_cf);
    break;
  }
  case 6: { // DIV r/m32
    if (val == 0) { do_interrupt(0); return; }
    emu88_uint64 dividend = (emu88_uint64(get_reg32(reg_DX)) << 32) | get_reg32(reg_AX);
    emu88_uint64 quotient = dividend / val;
    if (quotient > 0xFFFFFFFF) { do_interrupt(0); return; }
    emu88_uint32 remainder = (emu88_uint32)(dividend % val);
    set_reg32(reg_AX, (emu88_uint32)quotient);
    set_reg32(reg_DX, remainder);
    break;
  }
  case 7: { // IDIV r/m32
    if (val == 0) { do_interrupt(0); return; }
    emu88_int64 dividend = (emu88_int64)((emu88_uint64(get_reg32(reg_DX)) << 32) | get_reg32(reg_AX));
    emu88_int64 divisor = emu88_int32(val);
    emu88_int64 quotient = dividend / divisor;
    if (quotient > 0x7FFFFFFFLL || quotient < -(emu88_int64)0x80000000LL) { do_interrupt(0); return; }
    emu88_int32 remainder = (emu88_int32)(dividend % divisor);
    set_reg32(reg_AX, (emu88_uint32)quotient);
    set_reg32(reg_DX, (emu88_uint32)remainder);
    break;
  }
  }
}

void emu88::execute_grp5_rm32(emu88_uint8 modrm_byte) {
  modrm_result mr = decode_modrm(modrm_byte);
  switch (mr.reg_field) {
  case 0: { // INC r/m32
    emu88_uint32 val = get_rm32(mr);
    set_rm32(mr, alu_inc32(val));
    break;
  }
  case 1: { // DEC r/m32
    emu88_uint32 val = get_rm32(mr);
    set_rm32(mr, alu_dec32(val));
    break;
  }
  case 2: { // CALL r/m32 (near indirect) — in real mode, only uses low 16 bits
    emu88_uint32 target = get_rm32(mr);
    push_word(ip);
    ip = target & 0xFFFF;
    break;
  }
  case 4: { // JMP r/m32 (near indirect)
    ip = get_rm32(mr) & 0xFFFF;
    break;
  }
  case 6: { // PUSH r/m32
    push_dword(get_rm32(mr));
    break;
  }
  default:
    execute_grp5_rm16(modrm_byte);  // fall back to 16-bit
    break;
  }
}

//=============================================================================
// String operations
//=============================================================================

emu88_uint16 emu88::string_src_seg(void) const {
  if (seg_override >= 0)
    return sregs[seg_override];
  return sregs[seg_DS];
}

void emu88::execute_string_op(emu88_uint8 opcode) {
  emu88_int16 dir = get_flag(FLAG_DF) ? -1 : 1;

  auto do_one = [&]() {
    switch (opcode) {
    case 0x6C: { // INSB (80186+)
      store_byte(sregs[seg_ES], regs[reg_DI], port_in(regs[reg_DX]));
      regs[reg_DI] += dir;
      break;
    }
    case 0x6D: { // INSW / INSD (80186+)
      if (op_size_32) {
        emu88_uint32 val = port_in16(regs[reg_DX]) | (emu88_uint32(port_in16(regs[reg_DX])) << 16);
        store_dword(sregs[seg_ES], regs[reg_DI], val);
        regs[reg_DI] += dir * 4;
      } else {
        emu88_uint16 val = port_in(regs[reg_DX]) | ((emu88_uint16)port_in(regs[reg_DX]) << 8);
        store_word(sregs[seg_ES], regs[reg_DI], val);
        regs[reg_DI] += dir * 2;
      }
      break;
    }
    case 0x6E: { // OUTSB (80186+)
      port_out(regs[reg_DX], fetch_byte(string_src_seg(), regs[reg_SI]));
      regs[reg_SI] += dir;
      break;
    }
    case 0x6F: { // OUTSW / OUTSD (80186+)
      if (op_size_32) {
        emu88_uint32 val = fetch_dword(string_src_seg(), regs[reg_SI]);
        port_out16(regs[reg_DX], val & 0xFFFF);
        port_out16(regs[reg_DX], (val >> 16) & 0xFFFF);
        regs[reg_SI] += dir * 4;
      } else {
        emu88_uint16 val = fetch_word(string_src_seg(), regs[reg_SI]);
        port_out(regs[reg_DX], val & 0xFF);
        port_out(regs[reg_DX], (val >> 8) & 0xFF);
        regs[reg_SI] += dir * 2;
      }
      break;
    }
    case 0xA4: { // MOVSB
      emu88_uint8 val = fetch_byte(string_src_seg(), regs[reg_SI]);
      store_byte(sregs[seg_ES], regs[reg_DI], val);
      regs[reg_SI] += dir;
      regs[reg_DI] += dir;
      break;
    }
    case 0xA5: { // MOVSW / MOVSD
      if (op_size_32) {
        emu88_uint32 val = fetch_dword(string_src_seg(), regs[reg_SI]);
        store_dword(sregs[seg_ES], regs[reg_DI], val);
        regs[reg_SI] += dir * 4;
        regs[reg_DI] += dir * 4;
      } else {
        emu88_uint16 val = fetch_word(string_src_seg(), regs[reg_SI]);
        store_word(sregs[seg_ES], regs[reg_DI], val);
        regs[reg_SI] += dir * 2;
        regs[reg_DI] += dir * 2;
      }
      break;
    }
    case 0xA6: { // CMPSB
      emu88_uint8 src = fetch_byte(string_src_seg(), regs[reg_SI]);
      emu88_uint8 dst = fetch_byte(sregs[seg_ES], regs[reg_DI]);
      alu_sub8(src, dst, 0);
      regs[reg_SI] += dir;
      regs[reg_DI] += dir;
      break;
    }
    case 0xA7: { // CMPSW / CMPSD
      if (op_size_32) {
        emu88_uint32 src = fetch_dword(string_src_seg(), regs[reg_SI]);
        emu88_uint32 dst = fetch_dword(sregs[seg_ES], regs[reg_DI]);
        alu_sub32(src, dst, 0);
        regs[reg_SI] += dir * 4;
        regs[reg_DI] += dir * 4;
      } else {
        emu88_uint16 src = fetch_word(string_src_seg(), regs[reg_SI]);
        emu88_uint16 dst = fetch_word(sregs[seg_ES], regs[reg_DI]);
        alu_sub16(src, dst, 0);
        regs[reg_SI] += dir * 2;
        regs[reg_DI] += dir * 2;
      }
      break;
    }
    case 0xAA: { // STOSB
      store_byte(sregs[seg_ES], regs[reg_DI], regs[reg_AX] & 0xFF);
      regs[reg_DI] += dir;
      break;
    }
    case 0xAB: { // STOSW / STOSD
      if (op_size_32) {
        store_dword(sregs[seg_ES], regs[reg_DI], get_reg32(reg_AX));
        regs[reg_DI] += dir * 4;
      } else {
        store_word(sregs[seg_ES], regs[reg_DI], regs[reg_AX]);
        regs[reg_DI] += dir * 2;
      }
      break;
    }
    case 0xAC: { // LODSB
      set_reg8(reg_AL, fetch_byte(string_src_seg(), regs[reg_SI]));
      regs[reg_SI] += dir;
      break;
    }
    case 0xAD: { // LODSW / LODSD
      if (op_size_32) {
        set_reg32(reg_AX, fetch_dword(string_src_seg(), regs[reg_SI]));
        regs[reg_SI] += dir * 4;
      } else {
        regs[reg_AX] = fetch_word(string_src_seg(), regs[reg_SI]);
        regs[reg_SI] += dir * 2;
      }
      break;
    }
    case 0xAE: { // SCASB
      emu88_uint8 val = fetch_byte(sregs[seg_ES], regs[reg_DI]);
      alu_sub8(regs[reg_AX] & 0xFF, val, 0);
      regs[reg_DI] += dir;
      break;
    }
    case 0xAF: { // SCASW / SCASD
      if (op_size_32) {
        emu88_uint32 val = fetch_dword(sregs[seg_ES], regs[reg_DI]);
        alu_sub32(get_reg32(reg_AX), val, 0);
        regs[reg_DI] += dir * 4;
      } else {
        emu88_uint16 val = fetch_word(sregs[seg_ES], regs[reg_DI]);
        alu_sub16(regs[reg_AX], val, 0);
        regs[reg_DI] += dir * 2;
      }
      break;
    }
    }
  };

  if (rep_prefix == REP_NONE) {
    do_one();
  } else {
    // REP string ops: ~17 cycles per iteration on 8088
    while (regs[reg_CX] != 0) {
      do_one();
      regs[reg_CX]--;
      cycles += 17;
      // For CMPS/SCAS, check termination condition
      if (opcode == 0xA6 || opcode == 0xA7 || opcode == 0xAE || opcode == 0xAF) {
        if (rep_prefix == REP_REPZ && !get_flag(FLAG_ZF))
          break;
        if (rep_prefix == REP_REPNZ && get_flag(FLAG_ZF))
          break;
      }
    }
  }
}

//=============================================================================
// Debug
//=============================================================================

void emu88::debug_dump_regs(const char *label) {
  (void)label;
}

//=============================================================================
// Main instruction execution
//=============================================================================

// Approximate 8088 cycle counts per opcode.
// Memory operands cost more but this uses averages.
// Variable-cost instructions (MUL, DIV, REP string) are adjusted in handlers.
static const uint8_t base_cycles[256] = {
  // 0x00-0x07: ALU r/m,r (16), PUSH ES (14)
  16, 16, 10, 10,  4,  4, 14, 12,
  // 0x08-0x0F: OR r/m,r (16), PUSH CS (14), 0x0F prefix (4)
  16, 16, 10, 10,  4,  4, 14,  4,
  // 0x10-0x17: ADC r/m,r (16), PUSH SS (14)
  16, 16, 10, 10,  4,  4, 14, 12,
  // 0x18-0x1F: SBB r/m,r (16), POP DS (12)
  16, 16, 10, 10,  4,  4, 14, 12,
  // 0x20-0x27: AND r/m,r (16), DAA (4)
  16, 16, 10, 10,  4,  4,  2,  4,
  // 0x28-0x2F: SUB r/m,r (16), DAS (4)
  16, 16, 10, 10,  4,  4,  2,  4,
  // 0x30-0x37: XOR r/m,r (16), AAA (8)
  16, 16, 10, 10,  4,  4,  2,  8,
  // 0x38-0x3F: CMP r/m,r (16), AAS (8)
  16, 16, 10, 10,  4,  4,  2,  8,
  // 0x40-0x47: INC r16 (2)
   2,  2,  2,  2,  2,  2,  2,  2,
  // 0x48-0x4F: DEC r16 (2)
   2,  2,  2,  2,  2,  2,  2,  2,
  // 0x50-0x57: PUSH r16 (11)
  11, 11, 11, 11, 11, 11, 11, 11,
  // 0x58-0x5F: POP r16 (12)
  12, 12, 12, 12, 12, 12, 12, 12,
  // 0x60-0x67: 186+ PUSHA/POPA/BOUND/ARPL/FS/GS/OpSz/AdSz
   4,  4,  4,  4,  4,  4,  4,  4,
  // 0x68-0x6F: 186+ PUSH imm/IMUL/PUSH/IMUL/INS/OUTS
   4,  4,  4,  4,  4,  4,  4,  4,
  // 0x70-0x7F: Jcc short (taken=16, not-taken=4, use average 10)
  10, 10, 10, 10, 10, 10, 10, 10,
  10, 10, 10, 10, 10, 10, 10, 10,
  // 0x80-0x83: GRP1 r/m, imm (17 mem, 4 reg, avg 10)
  10, 10, 10, 10,
  // 0x84-0x87: TEST/XCHG r/m,r (13 avg)
  13, 13, 17, 17,
  // 0x88-0x8B: MOV r/m,r or r,r/m (10 mem, 2 reg, avg 6)
  10, 10,  8,  8,
  // 0x8C-0x8F: MOV r/m,sreg / LEA / MOV sreg,r/m / POP r/m
  10,  2, 10, 17,
  // 0x90-0x97: NOP (3), XCHG AX,r (3)
   3,  3,  3,  3,  3,  3,  3,  3,
  // 0x98-0x9F: CBW(2) CWD(5) CALL far(28) WAIT(4) PUSHF(10) POPF(8) SAHF(4) LAHF(4)
   2,  5, 28,  4, 10,  8,  4,  4,
  // 0xA0-0xA3: MOV AL/AX,[addr] / MOV [addr],AL/AX (10)
  10, 10, 10, 10,
  // 0xA4-0xA7: MOVS/CMPS (18 per iteration, REP adjusted separately)
  18, 18, 22, 22,
  // 0xA8-0xAB: TEST AL/AX,imm (4), STOS (11)
   4,  4, 11, 11,
  // 0xAC-0xAF: LODS (12), SCAS (15)
  12, 12, 15, 15,
  // 0xB0-0xB7: MOV r8, imm8 (4)
   4,  4,  4,  4,  4,  4,  4,  4,
  // 0xB8-0xBF: MOV r16, imm16 (4)
   4,  4,  4,  4,  4,  4,  4,  4,
  // 0xC0-0xC3: GRP2 r/m,imm8(186+)/RET/RET
   8, 12, 16, 16,
  // 0xC4-0xC7: LES(16) LDS(16) MOV r/m,imm(10)
  16, 16, 10, 10,
  // 0xC8-0xCF: 186+/186+/RETF(26)/RETF(25)/INT3(52)/INT(51)/INTO/IRET(32)
   4,  4, 26, 25, 52, 51, 53, 32,
  // 0xD0-0xD3: GRP2 r/m,1 (8 avg) / GRP2 r/m,CL (12 avg)
   8,  8, 12, 12,
  // 0xD4-0xD7: AAM(83) AAD(60) SALC(4) XLAT(11)
  83, 60,  4, 11,
  // 0xD8-0xDF: ESC/FPU (2, no FPU)
   2,  2,  2,  2,  2,  2,  2,  2,
  // 0xE0-0xE3: LOOPNZ(19) LOOPZ(18) LOOP(17) JCXZ(18)
  19, 18, 17, 18,
  // 0xE4-0xE7: IN(10) IN(10) OUT(10) OUT(10)
  10, 10, 10, 10,
  // 0xE8-0xEB: CALL near(19) JMP near(15) JMP far(15) JMP short(15)
  19, 15, 15, 15,
  // 0xEC-0xEF: IN DX(8) IN DX(8) OUT DX(8) OUT DX(8)
   8,  8,  8,  8,
  // 0xF0-0xF3: LOCK(2) undef(4) REPNZ(2) REPZ(2)
   2,  4,  2,  2,
  // 0xF4-0xF7: HLT(2) CMC(2) GRP3 r/m8(varies) GRP3 r/m16(varies)
   2,  2, 16, 20,
  // 0xF8-0xFF: CLC(2) STC(2) CLI(2) STI(2) CLD(2) STD(2) GRP4(varies) GRP5(varies)
   2,  2,  2,  2,  2,  2, 15, 15
};

void emu88::execute(void) {
  seg_override = -1;
  rep_prefix = REP_NONE;
  op_size_32 = false;
  addr_size_32 = false;

  // Handle prefix bytes
  bool prefix_done = false;
  while (!prefix_done) {
    emu88_uint8 prefix = fetch_byte(sregs[seg_CS], ip);
    switch (prefix) {
    case 0x26: seg_override = seg_ES; ip++; break;
    case 0x2E: seg_override = seg_CS; ip++; break;
    case 0x36: seg_override = seg_SS; ip++; break;
    case 0x3E: seg_override = seg_DS; ip++; break;
    case 0x64: seg_override = seg_FS; ip++; break;
    case 0x65: seg_override = seg_GS; ip++; break;
    case 0x66: op_size_32 = true; ip++; break;
    case 0x67: addr_size_32 = true; ip++; break;
    case 0xF0: ip++; break;  // LOCK prefix (ignored for emulation)
    case 0xF2: rep_prefix = REP_REPNZ; ip++; break;
    case 0xF3: rep_prefix = REP_REPZ; ip++; break;
    default: prefix_done = true; break;
    }
  }

  emu88_uint8 opcode = fetch_ip_byte();
  cycles += base_cycles[opcode];

  switch (opcode) {
  //--- ALU: op r/m8, r8 ---
  case 0x00: case 0x08: case 0x10: case 0x18:
  case 0x20: case 0x28: case 0x30: case 0x38: {
    emu88_uint8 op = (opcode >> 3) & 7;
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    emu88_uint8 val = get_rm8(mr);
    emu88_uint8 reg = get_reg8(mr.reg_field);
    emu88_uint8 result = do_alu8(op, val, reg);
    if (op != 7) set_rm8(mr, result);
    break;
  }

  //--- ALU: op r/m16, r16 ---
  case 0x01: case 0x09: case 0x11: case 0x19:
  case 0x21: case 0x29: case 0x31: case 0x39: {
    emu88_uint8 op = (opcode >> 3) & 7;
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    if (op_size_32) {
      emu88_uint32 val = get_rm32(mr);
      emu88_uint32 reg = get_reg32(mr.reg_field);
      emu88_uint32 result = do_alu32(op, val, reg);
      if (op != 7) set_rm32(mr, result);
    } else {
      emu88_uint16 val = get_rm16(mr);
      emu88_uint16 reg = regs[mr.reg_field];
      emu88_uint16 result = do_alu16(op, val, reg);
      if (op != 7) set_rm16(mr, result);
    }
    break;
  }

  //--- ALU: op r8, r/m8 ---
  case 0x02: case 0x0A: case 0x12: case 0x1A:
  case 0x22: case 0x2A: case 0x32: case 0x3A: {
    emu88_uint8 op = (opcode >> 3) & 7;
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    emu88_uint8 reg = get_reg8(mr.reg_field);
    emu88_uint8 val = get_rm8(mr);
    emu88_uint8 result = do_alu8(op, reg, val);
    if (op != 7) set_reg8(mr.reg_field, result);
    break;
  }

  //--- ALU: op r16, r/m16 ---
  case 0x03: case 0x0B: case 0x13: case 0x1B:
  case 0x23: case 0x2B: case 0x33: case 0x3B: {
    emu88_uint8 op = (opcode >> 3) & 7;
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    if (op_size_32) {
      emu88_uint32 reg = get_reg32(mr.reg_field);
      emu88_uint32 val = get_rm32(mr);
      emu88_uint32 result = do_alu32(op, reg, val);
      if (op != 7) set_reg32(mr.reg_field, result);
    } else {
      emu88_uint16 reg = regs[mr.reg_field];
      emu88_uint16 val = get_rm16(mr);
      emu88_uint16 result = do_alu16(op, reg, val);
      if (op != 7) regs[mr.reg_field] = result;
    }
    break;
  }

  //--- ALU: op AL, imm8 ---
  case 0x04: case 0x0C: case 0x14: case 0x1C:
  case 0x24: case 0x2C: case 0x34: case 0x3C: {
    emu88_uint8 op = (opcode >> 3) & 7;
    emu88_uint8 imm = fetch_ip_byte();
    emu88_uint8 al = get_reg8(reg_AL);
    emu88_uint8 result = do_alu8(op, al, imm);
    if (op != 7) set_reg8(reg_AL, result);
    break;
  }

  //--- ALU: op AX, imm16 ---
  case 0x05: case 0x0D: case 0x15: case 0x1D:
  case 0x25: case 0x2D: case 0x35: case 0x3D: {
    emu88_uint8 op = (opcode >> 3) & 7;
    if (op_size_32) {
      emu88_uint32 imm = fetch_ip_dword();
      emu88_uint32 result = do_alu32(op, get_reg32(reg_AX), imm);
      if (op != 7) set_reg32(reg_AX, result);
    } else {
      emu88_uint16 imm = fetch_ip_word();
      emu88_uint16 result = do_alu16(op, regs[reg_AX], imm);
      if (op != 7) regs[reg_AX] = result;
    }
    break;
  }

  //--- PUSH segment ---
  case 0x06: push_word(sregs[seg_ES]); break;
  case 0x0E: push_word(sregs[seg_CS]); break;
  case 0x16: push_word(sregs[seg_SS]); break;
  case 0x1E: push_word(sregs[seg_DS]); break;

  //--- POP segment ---
  case 0x07: sregs[seg_ES] = pop_word(); break;
  case 0x17: sregs[seg_SS] = pop_word(); break;
  case 0x1F: sregs[seg_DS] = pop_word(); break;
  // 0x0F: POP CS is not valid on 8088 (undefined behavior)

  //--- DAA ---
  case 0x27: {
    emu88_uint8 al = get_reg8(reg_AL);
    emu88_uint8 old_al = al;
    bool old_cf = get_flag(FLAG_CF);
    clear_flag(FLAG_CF);
    if ((al & 0x0F) > 9 || get_flag(FLAG_AF)) {
      al += 6;
      set_flag_val(FLAG_CF, old_cf || (al < old_al));
      set_flag(FLAG_AF);
    } else {
      clear_flag(FLAG_AF);
    }
    if (old_al > 0x99 || old_cf) {
      al += 0x60;
      set_flag(FLAG_CF);
    }
    set_reg8(reg_AL, al);
    set_flags_zsp8(al);
    break;
  }

  //--- DAS ---
  case 0x2F: {
    emu88_uint8 al = get_reg8(reg_AL);
    emu88_uint8 old_al = al;
    bool old_cf = get_flag(FLAG_CF);
    clear_flag(FLAG_CF);
    if ((al & 0x0F) > 9 || get_flag(FLAG_AF)) {
      al -= 6;
      set_flag_val(FLAG_CF, old_cf || (old_al < 6));
      set_flag(FLAG_AF);
    } else {
      clear_flag(FLAG_AF);
    }
    if (old_al > 0x99 || old_cf) {
      al -= 0x60;
      set_flag(FLAG_CF);
    }
    set_reg8(reg_AL, al);
    set_flags_zsp8(al);
    break;
  }

  //--- AAA ---
  case 0x37: {
    emu88_uint8 al = get_reg8(reg_AL);
    if ((al & 0x0F) > 9 || get_flag(FLAG_AF)) {
      set_reg8(reg_AL, (al + 6) & 0x0F);
      set_reg8(reg_AH, get_reg8(reg_AH) + 1);
      set_flag(FLAG_AF);
      set_flag(FLAG_CF);
    } else {
      set_reg8(reg_AL, al & 0x0F);
      clear_flag(FLAG_AF);
      clear_flag(FLAG_CF);
    }
    break;
  }

  //--- AAS ---
  case 0x3F: {
    emu88_uint8 al = get_reg8(reg_AL);
    if ((al & 0x0F) > 9 || get_flag(FLAG_AF)) {
      set_reg8(reg_AL, (al - 6) & 0x0F);
      set_reg8(reg_AH, get_reg8(reg_AH) - 1);
      set_flag(FLAG_AF);
      set_flag(FLAG_CF);
    } else {
      set_reg8(reg_AL, al & 0x0F);
      clear_flag(FLAG_AF);
      clear_flag(FLAG_CF);
    }
    break;
  }

  //--- INC r16 (0x40-0x47) ---
  case 0x40: case 0x41: case 0x42: case 0x43:
  case 0x44: case 0x45: case 0x46: case 0x47: {
    emu88_uint8 r = opcode & 7;
    if (op_size_32) {
      emu88_uint32 val = get_reg32(r);
      emu88_uint32 result = alu_inc32(val);
      set_reg32(r, result);
    } else {
      emu88_uint16 val = regs[r];
      emu88_uint16 result = val + 1;
      set_flags_zsp16(result);
      set_flag_val(FLAG_AF, (val & 0x0F) == 0x0F);
      set_flag_val(FLAG_OF, val == 0x7FFF);
      regs[r] = result;
    }
    break;
  }

  //--- DEC r16 (0x48-0x4F) ---
  case 0x48: case 0x49: case 0x4A: case 0x4B:
  case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
    emu88_uint8 r = opcode & 7;
    if (op_size_32) {
      emu88_uint32 val = get_reg32(r);
      emu88_uint32 result = alu_dec32(val);
      set_reg32(r, result);
    } else {
      emu88_uint16 val = regs[r];
      emu88_uint16 result = val - 1;
      set_flags_zsp16(result);
      set_flag_val(FLAG_AF, (val & 0x0F) == 0x00);
      set_flag_val(FLAG_OF, val == 0x8000);
      regs[r] = result;
    }
    break;
  }

  //--- PUSH r16 (0x50-0x57) ---
  case 0x50: case 0x51: case 0x52: case 0x53:
  case 0x54: case 0x55: case 0x56: case 0x57:
    if (op_size_32) push_dword(get_reg32(opcode & 7));
    else push_word(regs[opcode & 7]);
    break;

  //--- POP r16 (0x58-0x5F) ---
  case 0x58: case 0x59: case 0x5A: case 0x5B:
  case 0x5C: case 0x5D: case 0x5E: case 0x5F:
    if (op_size_32) set_reg32(opcode & 7, pop_dword());
    else regs[opcode & 7] = pop_word();
    break;

  //--- Conditional jumps (0x70-0x7F) ---
  case 0x70: { // JO
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    if (get_flag(FLAG_OF)) ip += disp;
    break;
  }
  case 0x71: { // JNO
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    if (!get_flag(FLAG_OF)) ip += disp;
    break;
  }
  case 0x72: { // JB/JNAE/JC
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    if (get_flag(FLAG_CF)) ip += disp;
    break;
  }
  case 0x73: { // JNB/JAE/JNC
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    if (!get_flag(FLAG_CF)) ip += disp;
    break;
  }
  case 0x74: { // JE/JZ
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    if (get_flag(FLAG_ZF)) ip += disp;
    break;
  }
  case 0x75: { // JNE/JNZ
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    if (!get_flag(FLAG_ZF)) ip += disp;
    break;
  }
  case 0x76: { // JBE/JNA
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    if (get_flag(FLAG_CF) || get_flag(FLAG_ZF)) ip += disp;
    break;
  }
  case 0x77: { // JNBE/JA
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    if (!get_flag(FLAG_CF) && !get_flag(FLAG_ZF)) ip += disp;
    break;
  }
  case 0x78: { // JS
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    if (get_flag(FLAG_SF)) ip += disp;
    break;
  }
  case 0x79: { // JNS
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    if (!get_flag(FLAG_SF)) ip += disp;
    break;
  }
  case 0x7A: { // JP/JPE
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    if (get_flag(FLAG_PF)) ip += disp;
    break;
  }
  case 0x7B: { // JNP/JPO
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    if (!get_flag(FLAG_PF)) ip += disp;
    break;
  }
  case 0x7C: { // JL/JNGE
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    if (get_flag(FLAG_SF) != get_flag(FLAG_OF)) ip += disp;
    break;
  }
  case 0x7D: { // JNL/JGE
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    if (get_flag(FLAG_SF) == get_flag(FLAG_OF)) ip += disp;
    break;
  }
  case 0x7E: { // JLE/JNG
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    if (get_flag(FLAG_ZF) || (get_flag(FLAG_SF) != get_flag(FLAG_OF))) ip += disp;
    break;
  }
  case 0x7F: { // JNLE/JG
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    if (!get_flag(FLAG_ZF) && (get_flag(FLAG_SF) == get_flag(FLAG_OF))) ip += disp;
    break;
  }

  //--- GRP1: ALU r/m8, imm8 ---
  case 0x80: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    execute_grp1_rm8(mr, fetch_ip_byte());
    break;
  }

  //--- GRP1: ALU r/m16, imm16 ---
  case 0x81: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    if (op_size_32) execute_grp1_rm32(mr, fetch_ip_dword());
    else execute_grp1_rm16(mr, fetch_ip_word());
    break;
  }

  //--- GRP1: ALU r/m8, imm8 (duplicate of 0x80) ---
  case 0x82: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    execute_grp1_rm8(mr, fetch_ip_byte());
    break;
  }

  //--- GRP1: ALU r/m16, sign-extended imm8 ---
  case 0x83: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    emu88_int8 imm8 = (emu88_int8)fetch_ip_byte();
    if (op_size_32) execute_grp1_rm32(mr, emu88_uint32(emu88_int32(imm8)));
    else execute_grp1_rm16(mr, emu88_uint16(emu88_int16(imm8)));
    break;
  }

  //--- TEST r/m8, r8 ---
  case 0x84: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    set_flags_logic8(get_rm8(mr) & get_reg8(mr.reg_field));
    break;
  }

  //--- TEST r/m16, r16 ---
  case 0x85: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    if (op_size_32) set_flags_logic32(get_rm32(mr) & get_reg32(mr.reg_field));
    else set_flags_logic16(get_rm16(mr) & regs[mr.reg_field]);
    break;
  }

  //--- XCHG r/m8, r8 ---
  case 0x86: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    emu88_uint8 a = get_rm8(mr);
    emu88_uint8 b = get_reg8(mr.reg_field);
    set_rm8(mr, b);
    set_reg8(mr.reg_field, a);
    break;
  }

  //--- XCHG r/m16, r16 ---
  case 0x87: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    if (op_size_32) {
      emu88_uint32 a = get_rm32(mr);
      emu88_uint32 b = get_reg32(mr.reg_field);
      set_rm32(mr, b);
      set_reg32(mr.reg_field, a);
    } else {
      emu88_uint16 a = get_rm16(mr);
      emu88_uint16 b = regs[mr.reg_field];
      set_rm16(mr, b);
      regs[mr.reg_field] = a;
    }
    break;
  }

  //--- MOV r/m8, r8 ---
  case 0x88: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    set_rm8(mr, get_reg8(mr.reg_field));
    break;
  }

  //--- MOV r/m16, r16 ---
  case 0x89: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    if (op_size_32) set_rm32(mr, get_reg32(mr.reg_field));
    else set_rm16(mr, regs[mr.reg_field]);
    break;
  }

  //--- MOV r8, r/m8 ---
  case 0x8A: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    set_reg8(mr.reg_field, get_rm8(mr));
    break;
  }

  //--- MOV r16, r/m16 ---
  case 0x8B: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    if (op_size_32) set_reg32(mr.reg_field, get_rm32(mr));
    else regs[mr.reg_field] = get_rm16(mr);
    break;
  }

  //--- MOV r/m16, sreg ---
  case 0x8C: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    int sreg_idx = mr.reg_field & 7;
    if (sreg_idx >= 6) sreg_idx = 0;
    set_rm16(mr, sregs[sreg_idx]);
    break;
  }

  //--- LEA r16, m ---
  case 0x8D: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    if (op_size_32) set_reg32(mr.reg_field, mr.offset);
    else regs[mr.reg_field] = mr.offset;
    break;
  }

  //--- MOV sreg, r/m16 ---
  case 0x8E: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    int sreg_idx = mr.reg_field & 7;
    if (sreg_idx >= 6) sreg_idx = 0;
    sregs[sreg_idx] = get_rm16(mr);
    break;
  }

  //--- POP r/m16 ---
  case 0x8F: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    if (op_size_32) set_rm32(mr, pop_dword());
    else set_rm16(mr, pop_word());
    break;
  }

  //--- NOP (XCHG AX, AX) ---
  case 0x90:
    break;

  //--- XCHG AX, r16 (0x91-0x97) ---
  case 0x91: case 0x92: case 0x93:
  case 0x94: case 0x95: case 0x96: case 0x97: {
    emu88_uint8 r = opcode & 7;
    if (op_size_32) {
      emu88_uint32 tmp = get_reg32(reg_AX);
      set_reg32(reg_AX, get_reg32(r));
      set_reg32(r, tmp);
    } else {
      emu88_uint16 tmp = regs[reg_AX];
      regs[reg_AX] = regs[r];
      regs[r] = tmp;
    }
    break;
  }

  //--- CBW / CWDE ---
  case 0x98:
    if (op_size_32) // CWDE: sign-extend AX to EAX
      set_reg32(reg_AX, emu88_uint32(emu88_int32(emu88_int16(regs[reg_AX]))));
    else // CBW: sign-extend AL to AX
      regs[reg_AX] = emu88_uint16(emu88_int16(emu88_int8(regs[reg_AX] & 0xFF)));
    break;

  //--- CWD / CDQ ---
  case 0x99:
    if (op_size_32) // CDQ: sign-extend EAX to EDX:EAX
      set_reg32(reg_DX, (get_reg32(reg_AX) & 0x80000000) ? 0xFFFFFFFF : 0x00000000);
    else // CWD: sign-extend AX to DX:AX
      regs[reg_DX] = (regs[reg_AX] & 0x8000) ? 0xFFFF : 0x0000;
    break;

  //--- CALL far ptr16:16 ---
  case 0x9A: {
    emu88_uint16 off = fetch_ip_word();
    emu88_uint16 seg = fetch_ip_word();
    push_word(sregs[seg_CS]);
    push_word(ip);
    sregs[seg_CS] = seg;
    ip = off;
    break;
  }

  //--- WAIT ---
  case 0x9B:
    break;  // no FPU, just continue

  //--- PUSHF / PUSHFD ---
  case 0x9C:
    if (op_size_32) push_dword(get_eflags() | 0x0002);
    else push_word((flags & 0x7FFF) | 0x0002);  // 386: bit 15=0, bit 1=1, preserve IOPL/NT
    break;

  //--- POPF / POPFD ---
  case 0x9D:
    if (op_size_32) set_eflags((pop_dword() & 0x003FFFFF) | 0x0002);  // mask reserved bits
    else flags = (pop_word() & 0x7FD7) | 0x0002;  // 386: allow IOPL/NT, clear bits 15/5/3
    break;

  //--- SAHF ---
  case 0x9E:
    flags = (flags & 0xFF00) | get_reg8(reg_AH);
    break;

  //--- LAHF ---
  case 0x9F:
    set_reg8(reg_AH, flags & 0xFF);
    break;

  //--- MOV AL, [addr16] ---
  case 0xA0: {
    emu88_uint16 addr = fetch_ip_word();
    set_reg8(reg_AL, fetch_byte(default_segment(), addr));
    break;
  }

  //--- MOV AX, [addr16] ---
  case 0xA1: {
    emu88_uint16 addr = fetch_ip_word();
    if (op_size_32) set_reg32(reg_AX, fetch_dword(default_segment(), addr));
    else regs[reg_AX] = fetch_word(default_segment(), addr);
    break;
  }

  //--- MOV [addr16], AL ---
  case 0xA2: {
    emu88_uint16 addr = fetch_ip_word();
    store_byte(default_segment(), addr, get_reg8(reg_AL));
    break;
  }

  //--- MOV [addr16], AX ---
  case 0xA3: {
    emu88_uint16 addr = fetch_ip_word();
    if (op_size_32) store_dword(default_segment(), addr, get_reg32(reg_AX));
    else store_word(default_segment(), addr, regs[reg_AX]);
    break;
  }

  //--- String operations ---
  case 0x6C: case 0x6D: case 0x6E: case 0x6F:  // INS/OUTS (80186+)
  case 0xA4: case 0xA5: case 0xA6: case 0xA7:
  case 0xAA: case 0xAB: case 0xAC: case 0xAD:
  case 0xAE: case 0xAF:
    execute_string_op(opcode);
    break;

  //--- TEST AL, imm8 ---
  case 0xA8: {
    emu88_uint8 imm = fetch_ip_byte();
    set_flags_logic8(get_reg8(reg_AL) & imm);
    break;
  }

  //--- TEST AX, imm16 ---
  case 0xA9: {
    if (op_size_32) {
      emu88_uint32 imm = fetch_ip_dword();
      set_flags_logic32(get_reg32(reg_AX) & imm);
    } else {
      emu88_uint16 imm = fetch_ip_word();
      set_flags_logic16(regs[reg_AX] & imm);
    }
    break;
  }

  //--- MOV r8, imm8 (0xB0-0xB7) ---
  case 0xB0: case 0xB1: case 0xB2: case 0xB3:
  case 0xB4: case 0xB5: case 0xB6: case 0xB7:
    set_reg8(opcode & 7, fetch_ip_byte());
    break;

  //--- MOV r16, imm16 (0xB8-0xBF) ---
  case 0xB8: case 0xB9: case 0xBA: case 0xBB:
  case 0xBC: case 0xBD: case 0xBE: case 0xBF:
    if (op_size_32) set_reg32(opcode & 7, fetch_ip_dword());
    else regs[opcode & 7] = fetch_ip_word();
    break;

  //--- GRP2: shift/rotate r/m8, imm8 (80186+, treated as 1 on 8088) ---
  case 0xC0: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    emu88_uint8 count = fetch_ip_byte();
    execute_grp2_rm8(mr, count);
    break;
  }

  //--- GRP2: shift/rotate r/m16, imm8 (80186+, treated as 1 on 8088) ---
  case 0xC1: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    emu88_uint8 count = fetch_ip_byte();
    if (op_size_32) execute_grp2_rm32(mr, count);
    else execute_grp2_rm16(mr, count);
    break;
  }

  //--- RET near imm16 ---
  case 0xC2: {
    emu88_uint16 pop_count = fetch_ip_word();
    ip = pop_word();
    regs[reg_SP] += pop_count;
    break;
  }

  //--- RET near ---
  case 0xC3:
    ip = pop_word();
    break;

  //--- LES r16, m16:16 / LES r32, m16:32 ---
  case 0xC4: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    if (op_size_32) {
      set_reg32(mr.reg_field, fetch_dword(mr.seg, mr.offset));
      sregs[seg_ES] = fetch_word(mr.seg, mr.offset + 4);
    } else {
      regs[mr.reg_field] = fetch_word(mr.seg, mr.offset);
      sregs[seg_ES] = fetch_word(mr.seg, mr.offset + 2);
    }
    break;
  }

  //--- LDS r16, m16:16 / LDS r32, m16:32 ---
  case 0xC5: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    if (op_size_32) {
      set_reg32(mr.reg_field, fetch_dword(mr.seg, mr.offset));
      sregs[seg_DS] = fetch_word(mr.seg, mr.offset + 4);
    } else {
      regs[mr.reg_field] = fetch_word(mr.seg, mr.offset);
      sregs[seg_DS] = fetch_word(mr.seg, mr.offset + 2);
    }
    break;
  }

  //--- MOV r/m8, imm8 ---
  case 0xC6: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    set_rm8(mr, fetch_ip_byte());
    break;
  }

  //--- MOV r/m16, imm16 ---
  case 0xC7: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    if (op_size_32) set_rm32(mr, fetch_ip_dword());
    else set_rm16(mr, fetch_ip_word());
    break;
  }

  //--- RETF imm16 ---
  case 0xCA: {
    emu88_uint16 pop_count = fetch_ip_word();
    ip = pop_word();
    sregs[seg_CS] = pop_word();
    regs[reg_SP] += pop_count;
    break;
  }

  //--- RETF ---
  case 0xCB:
    ip = pop_word();
    sregs[seg_CS] = pop_word();
    break;

  //--- INT 3 ---
  case 0xCC:
    do_interrupt(3);
    break;

  //--- INT imm8 ---
  case 0xCD:
    do_interrupt(fetch_ip_byte());
    break;

  //--- INTO ---
  case 0xCE:
    if (get_flag(FLAG_OF))
      do_interrupt(4);
    break;

  //--- IRET / IRETD ---
  case 0xCF:
    if (op_size_32) {
      ip = pop_dword() & 0xFFFF;  // in real mode, IP is still 16-bit
      sregs[seg_CS] = pop_dword() & 0xFFFF;
      set_eflags((pop_dword() & 0x003FFFFF) | 0x0002);
    } else {
      ip = pop_word();
      sregs[seg_CS] = pop_word();
      flags = (pop_word() & 0x7FD7) | 0x0002;  // 386: allow IOPL/NT
    }
    break;

  //--- GRP2: shift/rotate r/m8, 1 ---
  case 0xD0: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    execute_grp2_rm8(mr, 1);
    break;
  }

  //--- GRP2: shift/rotate r/m16, 1 ---
  case 0xD1: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    if (op_size_32) execute_grp2_rm32(mr, 1);
    else execute_grp2_rm16(mr, 1);
    break;
  }

  //--- GRP2: shift/rotate r/m8, CL ---
  case 0xD2: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    execute_grp2_rm8(mr, get_reg8(reg_CL));
    break;
  }

  //--- GRP2: shift/rotate r/m16, CL ---
  case 0xD3: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    if (op_size_32) execute_grp2_rm32(mr, get_reg8(reg_CL));
    else execute_grp2_rm16(mr, get_reg8(reg_CL));
    break;
  }

  //--- AAM ---
  case 0xD4: {
    emu88_uint8 base = fetch_ip_byte();  // usually 0x0A
    if (base == 0) {
      do_interrupt(0);
      break;
    }
    emu88_uint8 al = get_reg8(reg_AL);
    set_reg8(reg_AH, al / base);
    set_reg8(reg_AL, al % base);
    set_flags_zsp8(get_reg8(reg_AL));
    break;
  }

  //--- AAD ---
  case 0xD5: {
    emu88_uint8 base = fetch_ip_byte();  // usually 0x0A
    emu88_uint8 al = get_reg8(reg_AL);
    emu88_uint8 ah = get_reg8(reg_AH);
    set_reg8(reg_AL, (ah * base + al) & 0xFF);
    set_reg8(reg_AH, 0);
    set_flags_zsp8(get_reg8(reg_AL));
    break;
  }

  //--- XLAT ---
  case 0xD7: {
    emu88_uint16 addr = regs[reg_BX] + get_reg8(reg_AL);
    set_reg8(reg_AL, fetch_byte(default_segment(), addr));
    break;
  }

  //--- ESC (FPU escape, 0xD8-0xDF) ---
  case 0xD8: case 0xD9: case 0xDA: case 0xDB:
  case 0xDC: case 0xDD: case 0xDE: case 0xDF: {
    // No FPU - just consume the modrm byte and any displacement
    emu88_uint8 modrm = fetch_ip_byte();
    decode_modrm(modrm);  // consume displacement bytes
    break;
  }

  //--- LOOPNZ/LOOPNE ---
  case 0xE0: {
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    regs[reg_CX]--;
    if (regs[reg_CX] != 0 && !get_flag(FLAG_ZF))
      ip += disp;
    break;
  }

  //--- LOOPZ/LOOPE ---
  case 0xE1: {
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    regs[reg_CX]--;
    if (regs[reg_CX] != 0 && get_flag(FLAG_ZF))
      ip += disp;
    break;
  }

  //--- LOOP ---
  case 0xE2: {
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    regs[reg_CX]--;
    if (regs[reg_CX] != 0)
      ip += disp;
    break;
  }

  //--- JCXZ ---
  case 0xE3: {
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    if (regs[reg_CX] == 0)
      ip += disp;
    break;
  }

  //--- IN AL, imm8 ---
  case 0xE4:
    set_reg8(reg_AL, port_in(fetch_ip_byte()));
    break;

  //--- IN AX, imm8 ---
  case 0xE5: {
    emu88_uint8 port = fetch_ip_byte();
    if (op_size_32) set_reg32(reg_AX, port_in16(port) | (emu88_uint32(port_in16(port + 2)) << 16));
    else regs[reg_AX] = port_in16(port);
    break;
  }

  //--- OUT imm8, AL ---
  case 0xE6:
    port_out(fetch_ip_byte(), get_reg8(reg_AL));
    break;

  //--- OUT imm8, AX ---
  case 0xE7: {
    emu88_uint8 port = fetch_ip_byte();
    if (op_size_32) {
      emu88_uint32 val = get_reg32(reg_AX);
      port_out16(port, val & 0xFFFF);
      port_out16(port + 2, (val >> 16) & 0xFFFF);
    } else {
      port_out16(port, regs[reg_AX]);
    }
    break;
  }

  //--- CALL near rel16 ---
  case 0xE8: {
    emu88_int16 disp = (emu88_int16)fetch_ip_word();
    push_word(ip);
    ip += disp;
    break;
  }

  //--- JMP near rel16 ---
  case 0xE9: {
    emu88_int16 disp = (emu88_int16)fetch_ip_word();
    ip += disp;
    break;
  }

  //--- JMP far ptr16:16 ---
  case 0xEA: {
    emu88_uint16 off = fetch_ip_word();
    emu88_uint16 seg = fetch_ip_word();
    sregs[seg_CS] = seg;
    ip = off;
    break;
  }

  //--- JMP short rel8 ---
  case 0xEB: {
    emu88_int8 disp = (emu88_int8)fetch_ip_byte();
    ip += disp;
    break;
  }

  //--- IN AL, DX ---
  case 0xEC:
    set_reg8(reg_AL, port_in(regs[reg_DX]));
    break;

  //--- IN AX, DX ---
  case 0xED:
    if (op_size_32) {
      emu88_uint16 port = regs[reg_DX];
      set_reg32(reg_AX, port_in16(port) | (emu88_uint32(port_in16(port + 2)) << 16));
    } else {
      regs[reg_AX] = port_in16(regs[reg_DX]);
    }
    break;

  //--- OUT DX, AL ---
  case 0xEE:
    port_out(regs[reg_DX], get_reg8(reg_AL));
    break;

  //--- OUT DX, AX ---
  case 0xEF:
    if (op_size_32) {
      emu88_uint16 port = regs[reg_DX];
      emu88_uint32 val = get_reg32(reg_AX);
      port_out16(port, val & 0xFFFF);
      port_out16(port + 2, (val >> 16) & 0xFFFF);
    } else {
      port_out16(regs[reg_DX], regs[reg_AX]);
    }
    break;

  //--- HLT ---
  case 0xF4:
    halt_cpu();
    break;

  //--- CMC ---
  case 0xF5:
    set_flag_val(FLAG_CF, !get_flag(FLAG_CF));
    break;

  //--- GRP3: r/m8 ---
  case 0xF6: {
    emu88_uint8 modrm = fetch_ip_byte();
    execute_grp3_rm8(modrm);
    break;
  }

  //--- GRP3: r/m16 ---
  case 0xF7: {
    emu88_uint8 modrm = fetch_ip_byte();
    if (op_size_32) execute_grp3_rm32(modrm);
    else execute_grp3_rm16(modrm);
    break;
  }

  //--- CLC ---
  case 0xF8: clear_flag(FLAG_CF); break;
  //--- STC ---
  case 0xF9: set_flag(FLAG_CF); break;
  //--- CLI ---
  case 0xFA: clear_flag(FLAG_IF); break;
  //--- STI ---
  case 0xFB: set_flag(FLAG_IF); break;
  //--- CLD ---
  case 0xFC: clear_flag(FLAG_DF); break;
  //--- STD ---
  case 0xFD: set_flag(FLAG_DF); break;

  //--- GRP4: INC/DEC r/m8 ---
  case 0xFE: {
    emu88_uint8 modrm = fetch_ip_byte();
    execute_grp4_rm8(modrm);
    break;
  }

  //--- GRP5: misc r/m16 ---
  case 0xFF: {
    emu88_uint8 modrm = fetch_ip_byte();
    if (op_size_32) execute_grp5_rm32(modrm);
    else execute_grp5_rm16(modrm);
    break;
  }

  //--- 80186+ instructions ---

  //--- PUSHA / PUSHAD (80186+) ---
  case 0x60: {
    if (op_size_32) {
      emu88_uint32 tmp_esp = get_reg32(reg_SP);
      push_dword(get_reg32(reg_AX));
      push_dword(get_reg32(reg_CX));
      push_dword(get_reg32(reg_DX));
      push_dword(get_reg32(reg_BX));
      push_dword(tmp_esp);
      push_dword(get_reg32(reg_BP));
      push_dword(get_reg32(reg_SI));
      push_dword(get_reg32(reg_DI));
    } else {
      emu88_uint16 tmp_sp = regs[reg_SP];
      push_word(regs[reg_AX]);
      push_word(regs[reg_CX]);
      push_word(regs[reg_DX]);
      push_word(regs[reg_BX]);
      push_word(tmp_sp);
      push_word(regs[reg_BP]);
      push_word(regs[reg_SI]);
      push_word(regs[reg_DI]);
    }
    break;
  }

  //--- POPA / POPAD (80186+) ---
  case 0x61: {
    if (op_size_32) {
      set_reg32(reg_DI, pop_dword());
      set_reg32(reg_SI, pop_dword());
      set_reg32(reg_BP, pop_dword());
      pop_dword();  // skip ESP
      set_reg32(reg_BX, pop_dword());
      set_reg32(reg_DX, pop_dword());
      set_reg32(reg_CX, pop_dword());
      set_reg32(reg_AX, pop_dword());
    } else {
      regs[reg_DI] = pop_word();
      regs[reg_SI] = pop_word();
      regs[reg_BP] = pop_word();
      pop_word();  // skip SP
      regs[reg_BX] = pop_word();
      regs[reg_DX] = pop_word();
      regs[reg_CX] = pop_word();
      regs[reg_AX] = pop_word();
    }
    break;
  }

  //--- BOUND (80186+) ---
  case 0x62: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    emu88_int16 idx = (emu88_int16)regs[mr.reg_field];
    emu88_int16 lo = (emu88_int16)get_rm16(mr);
    emu88_int16 hi = (emu88_int16)fetch_word(mr.seg, mr.offset + 2);
    if (idx < lo || idx > hi) do_interrupt(5);
    break;
  }

  //--- PUSH imm16 (80186+) ---
  case 0x68:
    if (op_size_32) push_dword(fetch_ip_dword());
    else push_word(fetch_ip_word());
    break;

  //--- IMUL r16, r/m16, imm16 (80186+) ---
  case 0x69: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    if (op_size_32) {
      emu88_int32 src = (emu88_int32)get_rm32(mr);
      emu88_int32 imm = (emu88_int32)fetch_ip_dword();
      emu88_int64 result = (emu88_int64)src * (emu88_int64)imm;
      set_reg32(mr.reg_field, (emu88_uint32)result);
      set_flag_val(FLAG_CF, result != (emu88_int32)result);
      set_flag_val(FLAG_OF, result != (emu88_int32)result);
    } else {
      emu88_int16 src = (emu88_int16)get_rm16(mr);
      emu88_int16 imm = (emu88_int16)fetch_ip_word();
      emu88_int32 result = (emu88_int32)src * (emu88_int32)imm;
      regs[mr.reg_field] = (emu88_uint16)result;
      set_flag_val(FLAG_CF, result != (emu88_int16)result);
      set_flag_val(FLAG_OF, result != (emu88_int16)result);
    }
    break;
  }

  //--- PUSH imm8 (sign-extended) (80186+) ---
  case 0x6A: {
    emu88_int8 imm = (emu88_int8)fetch_ip_byte();
    if (op_size_32) push_dword((emu88_uint32)(emu88_int32)imm);
    else push_word((emu88_uint16)(emu88_int16)imm);
    break;
  }

  //--- IMUL r16, r/m16, imm8 (80186+) ---
  case 0x6B: {
    emu88_uint8 modrm = fetch_ip_byte();
    modrm_result mr = decode_modrm(modrm);
    emu88_int8 imm = (emu88_int8)fetch_ip_byte();
    if (op_size_32) {
      emu88_int32 src = (emu88_int32)get_rm32(mr);
      emu88_int64 result = (emu88_int64)src * (emu88_int64)imm;
      set_reg32(mr.reg_field, (emu88_uint32)result);
      set_flag_val(FLAG_CF, result != (emu88_int32)result);
      set_flag_val(FLAG_OF, result != (emu88_int32)result);
    } else {
      emu88_int16 src = (emu88_int16)get_rm16(mr);
      emu88_int32 result = (emu88_int32)src * (emu88_int32)imm;
      regs[mr.reg_field] = (emu88_uint16)result;
      set_flag_val(FLAG_CF, result != (emu88_int16)result);
      set_flag_val(FLAG_OF, result != (emu88_int16)result);
    }
    break;
  }

  //--- ENTER (80186+) ---
  case 0xC8: {
    emu88_uint16 alloc_size = fetch_ip_word();
    emu88_uint8 nesting = fetch_ip_byte();
    push_word(regs[reg_BP]);
    emu88_uint16 frame_ptr = regs[reg_SP];
    if (nesting > 0) {
      for (int i = 1; i < nesting; i++) {
        regs[reg_BP] -= 2;
        push_word(fetch_word(sregs[seg_SS], regs[reg_BP]));
      }
      push_word(frame_ptr);
    }
    regs[reg_BP] = frame_ptr;
    regs[reg_SP] -= alloc_size;
    break;
  }

  //--- LEAVE (80186+) ---
  case 0xC9:
    regs[reg_SP] = regs[reg_BP];
    regs[reg_BP] = pop_word();
    break;

  //--- 0x0F two-byte opcode prefix (286+) ---
  case 0x0F: {
    emu88_uint8 op2 = fetch_ip_byte();
    switch (op2) {

    // SGDT/SIDT/LGDT/LIDT/SMSW/LMSW (0x0F 0x01)
    case 0x01: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      switch (mr.reg_field) {
      case 0: // SGDT: store 6 bytes (limit:base) to memory
        store_word(mr.seg, mr.offset, gdtr_limit);
        store_word(mr.seg, mr.offset + 2, gdtr_base & 0xFFFF);
        store_byte(mr.seg, mr.offset + 4, (gdtr_base >> 16) & 0xFF);
        store_byte(mr.seg, mr.offset + 5, (gdtr_base >> 24) & 0xFF);
        break;
      case 1: // SIDT: store 6 bytes (limit:base) to memory
        store_word(mr.seg, mr.offset, idtr_limit);
        store_word(mr.seg, mr.offset + 2, idtr_base & 0xFFFF);
        store_byte(mr.seg, mr.offset + 4, (idtr_base >> 16) & 0xFF);
        store_byte(mr.seg, mr.offset + 5, (idtr_base >> 24) & 0xFF);
        break;
      case 2: // LGDT: load 6 bytes from memory
        gdtr_limit = fetch_word(mr.seg, mr.offset);
        gdtr_base = fetch_word(mr.seg, mr.offset + 2) |
                    (emu88_uint32(fetch_byte(mr.seg, mr.offset + 4)) << 16) |
                    (emu88_uint32(fetch_byte(mr.seg, mr.offset + 5)) << 24);
        break;
      case 3: // LIDT: load 6 bytes from memory
        idtr_limit = fetch_word(mr.seg, mr.offset);
        idtr_base = fetch_word(mr.seg, mr.offset + 2) |
                    (emu88_uint32(fetch_byte(mr.seg, mr.offset + 4)) << 16) |
                    (emu88_uint32(fetch_byte(mr.seg, mr.offset + 5)) << 24);
        break;
      case 4: // SMSW: store CR0 low 16 bits
        if (mr.is_register) regs[mr.rm_field] = cr0 & 0xFFFF;
        else store_word(mr.seg, mr.offset, cr0 & 0xFFFF);
        break;
      case 6: // LMSW: load CR0 low 16 bits (PE bit can be set but not cleared)
        {
          emu88_uint16 val = mr.is_register ? regs[mr.rm_field] : get_rm16(mr);
          cr0 = (cr0 & 0xFFFF0000) | val;
          // In real mode emulation, we just store it but don't actually switch to protected mode
        }
        break;
      default:
        break;
      }
      break;
    }

    // MOV r32, CRn (0x0F 0x20)
    case 0x20: {
      emu88_uint8 modrm = fetch_ip_byte();
      emu88_uint8 cr = (modrm >> 3) & 7;
      emu88_uint8 r = modrm & 7;
      switch (cr) {
      case 0: set_reg32(r, cr0); break;
      default: set_reg32(r, 0); break;  // CR2/CR3/CR4: return 0
      }
      break;
    }

    // MOV CRn, r32 (0x0F 0x22)
    case 0x22: {
      emu88_uint8 modrm = fetch_ip_byte();
      emu88_uint8 cr = (modrm >> 3) & 7;
      emu88_uint8 r = modrm & 7;
      switch (cr) {
      case 0: cr0 = get_reg32(r); break;
      default: break;  // ignore writes to other CRs
      }
      break;
    }

    // CLTS (0x0F 0x06) - Clear Task-Switched flag in CR0
    case 0x06:
      cr0 &= ~0x08;
      break;

    // WBINVD (0x0F 0x09) - Write-back and invalidate cache (NOP for emulator)
    case 0x09:
      break;

    // INVD (0x0F 0x08) - Invalidate cache (NOP for emulator)
    case 0x08:
      break;

    // PUSH FS (0x0F 0xA0)
    case 0xA0: push_word(sregs[seg_FS]); break;
    // POP FS (0x0F 0xA1)
    case 0xA1: sregs[seg_FS] = pop_word(); break;
    // PUSH GS (0x0F 0xA8)
    case 0xA8: push_word(sregs[seg_GS]); break;
    // POP GS (0x0F 0xA9)
    case 0xA9: sregs[seg_GS] = pop_word(); break;
    // MOVZX r16/r32, r/m8 (0x0F 0xB6)
    case 0xB6: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      if (op_size_32) set_reg32(mr.reg_field, get_rm8(mr));
      else regs[mr.reg_field] = get_rm8(mr);
      break;
    }
    // MOVZX r32, r/m16 (0x0F 0xB7)
    case 0xB7: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      set_reg32(mr.reg_field, get_rm16(mr));
      break;
    }
    // MOVSX r16/r32, r/m8 (0x0F 0xBE)
    case 0xBE: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      emu88_int8 val = (emu88_int8)get_rm8(mr);
      if (op_size_32) set_reg32(mr.reg_field, (emu88_uint32)(emu88_int32)val);
      else regs[mr.reg_field] = (emu88_uint16)(emu88_int16)val;
      break;
    }
    // MOVSX r32, r/m16 (0x0F 0xBF)
    case 0xBF: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      emu88_int16 val = (emu88_int16)get_rm16(mr);
      set_reg32(mr.reg_field, (emu88_uint32)(emu88_int32)val);
      break;
    }
    // SETcc (0x0F 0x90-0x9F)
    case 0x90: case 0x91: case 0x92: case 0x93:
    case 0x94: case 0x95: case 0x96: case 0x97:
    case 0x98: case 0x99: case 0x9A: case 0x9B:
    case 0x9C: case 0x9D: case 0x9E: case 0x9F: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      bool cond = false;
      switch (op2 & 0x0F) {
        case 0x0: cond = get_flag(FLAG_OF); break;   // SETO
        case 0x1: cond = !get_flag(FLAG_OF); break;  // SETNO
        case 0x2: cond = get_flag(FLAG_CF); break;   // SETB
        case 0x3: cond = !get_flag(FLAG_CF); break;  // SETNB
        case 0x4: cond = get_flag(FLAG_ZF); break;   // SETZ
        case 0x5: cond = !get_flag(FLAG_ZF); break;  // SETNZ
        case 0x6: cond = get_flag(FLAG_CF) || get_flag(FLAG_ZF); break; // SETBE
        case 0x7: cond = !get_flag(FLAG_CF) && !get_flag(FLAG_ZF); break; // SETA
        case 0x8: cond = get_flag(FLAG_SF); break;   // SETS
        case 0x9: cond = !get_flag(FLAG_SF); break;  // SETNS
        case 0xA: cond = get_flag(FLAG_PF); break;   // SETP
        case 0xB: cond = !get_flag(FLAG_PF); break;  // SETNP
        case 0xC: cond = get_flag(FLAG_SF) != get_flag(FLAG_OF); break; // SETL
        case 0xD: cond = get_flag(FLAG_SF) == get_flag(FLAG_OF); break; // SETGE
        case 0xE: cond = get_flag(FLAG_ZF) || (get_flag(FLAG_SF) != get_flag(FLAG_OF)); break; // SETLE
        case 0xF: cond = !get_flag(FLAG_ZF) && (get_flag(FLAG_SF) == get_flag(FLAG_OF)); break; // SETG
      }
      set_rm8(mr, cond ? 1 : 0);
      break;
    }
    // Jcc near (0x0F 0x80-0x8F) - 386+ two-byte conditional jumps
    case 0x80: case 0x81: case 0x82: case 0x83:
    case 0x84: case 0x85: case 0x86: case 0x87:
    case 0x88: case 0x89: case 0x8A: case 0x8B:
    case 0x8C: case 0x8D: case 0x8E: case 0x8F: {
      emu88_int16 disp = (emu88_int16)fetch_ip_word();
      bool cond = false;
      switch (op2 & 0x0F) {
        case 0x0: cond = get_flag(FLAG_OF); break;
        case 0x1: cond = !get_flag(FLAG_OF); break;
        case 0x2: cond = get_flag(FLAG_CF); break;
        case 0x3: cond = !get_flag(FLAG_CF); break;
        case 0x4: cond = get_flag(FLAG_ZF); break;
        case 0x5: cond = !get_flag(FLAG_ZF); break;
        case 0x6: cond = get_flag(FLAG_CF) || get_flag(FLAG_ZF); break;
        case 0x7: cond = !get_flag(FLAG_CF) && !get_flag(FLAG_ZF); break;
        case 0x8: cond = get_flag(FLAG_SF); break;
        case 0x9: cond = !get_flag(FLAG_SF); break;
        case 0xA: cond = get_flag(FLAG_PF); break;
        case 0xB: cond = !get_flag(FLAG_PF); break;
        case 0xC: cond = get_flag(FLAG_SF) != get_flag(FLAG_OF); break;
        case 0xD: cond = get_flag(FLAG_SF) == get_flag(FLAG_OF); break;
        case 0xE: cond = get_flag(FLAG_ZF) || (get_flag(FLAG_SF) != get_flag(FLAG_OF)); break;
        case 0xF: cond = !get_flag(FLAG_ZF) && (get_flag(FLAG_SF) == get_flag(FLAG_OF)); break;
      }
      if (cond) ip += disp;
      break;
    }
    // MOV sreg (0x0F extended segment ops) - handle FS/GS load/store
    case 0xB2: { // LSS r16, m16:16 / LSS r32, m16:32
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      if (op_size_32) {
        set_reg32(mr.reg_field, fetch_dword(mr.seg, mr.offset));
        sregs[seg_SS] = fetch_word(mr.seg, mr.offset + 4);
      } else {
        regs[mr.reg_field] = get_rm16(mr);
        sregs[seg_SS] = fetch_word(mr.seg, mr.offset + 2);
      }
      break;
    }
    case 0xB4: { // LFS r16, m16:16 / LFS r32, m16:32
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      if (op_size_32) {
        set_reg32(mr.reg_field, fetch_dword(mr.seg, mr.offset));
        sregs[seg_FS] = fetch_word(mr.seg, mr.offset + 4);
      } else {
        regs[mr.reg_field] = get_rm16(mr);
        sregs[seg_FS] = fetch_word(mr.seg, mr.offset + 2);
      }
      break;
    }
    case 0xB5: { // LGS r16, m16:16 / LGS r32, m16:32
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      if (op_size_32) {
        set_reg32(mr.reg_field, fetch_dword(mr.seg, mr.offset));
        sregs[seg_GS] = fetch_word(mr.seg, mr.offset + 4);
      } else {
        regs[mr.reg_field] = get_rm16(mr);
        sregs[seg_GS] = fetch_word(mr.seg, mr.offset + 2);
      }
      break;
    }
    // IMUL r16, r/m16 (0x0F 0xAF)
    case 0xAF: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      if (op_size_32) {
        emu88_int32 a = (emu88_int32)get_reg32(mr.reg_field);
        emu88_int32 b = (emu88_int32)get_rm32(mr);
        emu88_int64 result = (emu88_int64)a * (emu88_int64)b;
        set_reg32(mr.reg_field, (emu88_uint32)result);
        set_flag_val(FLAG_CF, result != (emu88_int32)result);
        set_flag_val(FLAG_OF, result != (emu88_int32)result);
      } else {
        emu88_int16 a = (emu88_int16)regs[mr.reg_field];
        emu88_int16 b = (emu88_int16)get_rm16(mr);
        emu88_int32 result = (emu88_int32)a * (emu88_int32)b;
        regs[mr.reg_field] = (emu88_uint16)result;
        set_flag_val(FLAG_CF, result != (emu88_int16)result);
        set_flag_val(FLAG_OF, result != (emu88_int16)result);
      }
      break;
    }

    // SHLD r/m16, r16, imm8 (0x0F 0xA4)
    case 0xA4: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      emu88_uint8 count = fetch_ip_byte() & 0x1F;
      if (count) {
        if (op_size_32) {
          emu88_uint32 dst = get_rm32(mr);
          emu88_uint32 src = get_reg32(mr.reg_field);
          emu88_uint64 tmp = ((emu88_uint64)dst << 32) | src;
          tmp <<= count;
          emu88_uint32 result = (tmp >> 32) & 0xFFFFFFFF;
          set_rm32(mr, result);
          set_flags_zsp32(result);
          set_flag_val(FLAG_CF, (dst >> (32 - count)) & 1);
        } else {
          emu88_uint16 dst = get_rm16(mr);
          emu88_uint16 src = regs[mr.reg_field];
          emu88_uint32 tmp = ((emu88_uint32)dst << 16) | src;
          tmp <<= count;
          emu88_uint16 result = (tmp >> 16) & 0xFFFF;
          set_rm16(mr, result);
          set_flags_zsp16(result);
          set_flag_val(FLAG_CF, (dst >> (16 - count)) & 1);
        }
      }
      break;
    }

    // SHLD r/m16, r16, CL (0x0F 0xA5)
    case 0xA5: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      emu88_uint8 count = get_reg8(reg_CL) & 0x1F;
      if (count) {
        if (op_size_32) {
          emu88_uint32 dst = get_rm32(mr);
          emu88_uint32 src = get_reg32(mr.reg_field);
          emu88_uint64 tmp = ((emu88_uint64)dst << 32) | src;
          tmp <<= count;
          emu88_uint32 result = (tmp >> 32) & 0xFFFFFFFF;
          set_rm32(mr, result);
          set_flags_zsp32(result);
          set_flag_val(FLAG_CF, (dst >> (32 - count)) & 1);
        } else {
          emu88_uint16 dst = get_rm16(mr);
          emu88_uint16 src = regs[mr.reg_field];
          emu88_uint32 tmp = ((emu88_uint32)dst << 16) | src;
          tmp <<= count;
          emu88_uint16 result = (tmp >> 16) & 0xFFFF;
          set_rm16(mr, result);
          set_flags_zsp16(result);
          set_flag_val(FLAG_CF, (dst >> (16 - count)) & 1);
        }
      }
      break;
    }

    // SHRD r/m16, r16, imm8 (0x0F 0xAC)
    case 0xAC: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      emu88_uint8 count = fetch_ip_byte() & 0x1F;
      if (count) {
        if (op_size_32) {
          emu88_uint32 dst = get_rm32(mr);
          emu88_uint32 src = get_reg32(mr.reg_field);
          emu88_uint64 tmp = ((emu88_uint64)src << 32) | dst;
          tmp >>= count;
          emu88_uint32 result = tmp & 0xFFFFFFFF;
          set_rm32(mr, result);
          set_flags_zsp32(result);
          set_flag_val(FLAG_CF, (dst >> (count - 1)) & 1);
        } else {
          emu88_uint16 dst = get_rm16(mr);
          emu88_uint16 src = regs[mr.reg_field];
          emu88_uint32 tmp = ((emu88_uint32)src << 16) | dst;
          tmp >>= count;
          emu88_uint16 result = tmp & 0xFFFF;
          set_rm16(mr, result);
          set_flags_zsp16(result);
          set_flag_val(FLAG_CF, (dst >> (count - 1)) & 1);
        }
      }
      break;
    }

    // SHRD r/m16, r16, CL (0x0F 0xAD)
    case 0xAD: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      emu88_uint8 count = get_reg8(reg_CL) & 0x1F;
      if (count) {
        if (op_size_32) {
          emu88_uint32 dst = get_rm32(mr);
          emu88_uint32 src = get_reg32(mr.reg_field);
          emu88_uint64 tmp = ((emu88_uint64)src << 32) | dst;
          tmp >>= count;
          emu88_uint32 result = tmp & 0xFFFFFFFF;
          set_rm32(mr, result);
          set_flags_zsp32(result);
          set_flag_val(FLAG_CF, (dst >> (count - 1)) & 1);
        } else {
          emu88_uint16 dst = get_rm16(mr);
          emu88_uint16 src = regs[mr.reg_field];
          emu88_uint32 tmp = ((emu88_uint32)src << 16) | dst;
          tmp >>= count;
          emu88_uint16 result = tmp & 0xFFFF;
          set_rm16(mr, result);
          set_flags_zsp16(result);
          set_flag_val(FLAG_CF, (dst >> (count - 1)) & 1);
        }
      }
      break;
    }

    // BT r/m16, r16 (0x0F 0xA3)
    case 0xA3: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      if (op_size_32) {
        emu88_uint32 val = get_rm32(mr);
        emu88_uint8 bit = get_reg32(mr.reg_field) & 31;
        set_flag_val(FLAG_CF, (val >> bit) & 1);
      } else {
        emu88_uint16 val = get_rm16(mr);
        emu88_uint8 bit = regs[mr.reg_field] & 15;
        set_flag_val(FLAG_CF, (val >> bit) & 1);
      }
      break;
    }

    // BTS r/m16, r16 (0x0F 0xAB)
    case 0xAB: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      if (op_size_32) {
        emu88_uint32 val = get_rm32(mr);
        emu88_uint8 bit = get_reg32(mr.reg_field) & 31;
        set_flag_val(FLAG_CF, (val >> bit) & 1);
        set_rm32(mr, val | (1u << bit));
      } else {
        emu88_uint16 val = get_rm16(mr);
        emu88_uint8 bit = regs[mr.reg_field] & 15;
        set_flag_val(FLAG_CF, (val >> bit) & 1);
        set_rm16(mr, val | (1u << bit));
      }
      break;
    }

    // BTR r/m16, r16 (0x0F 0xB3)
    case 0xB3: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      if (op_size_32) {
        emu88_uint32 val = get_rm32(mr);
        emu88_uint8 bit = get_reg32(mr.reg_field) & 31;
        set_flag_val(FLAG_CF, (val >> bit) & 1);
        set_rm32(mr, val & ~(1u << bit));
      } else {
        emu88_uint16 val = get_rm16(mr);
        emu88_uint8 bit = regs[mr.reg_field] & 15;
        set_flag_val(FLAG_CF, (val >> bit) & 1);
        set_rm16(mr, val & ~(emu88_uint16(1) << bit));
      }
      break;
    }

    // BTC r/m16, r16 (0x0F 0xBB)
    case 0xBB: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      if (op_size_32) {
        emu88_uint32 val = get_rm32(mr);
        emu88_uint8 bit = get_reg32(mr.reg_field) & 31;
        set_flag_val(FLAG_CF, (val >> bit) & 1);
        set_rm32(mr, val ^ (1u << bit));
      } else {
        emu88_uint16 val = get_rm16(mr);
        emu88_uint8 bit = regs[mr.reg_field] & 15;
        set_flag_val(FLAG_CF, (val >> bit) & 1);
        set_rm16(mr, val ^ (emu88_uint16(1) << bit));
      }
      break;
    }

    // BT/BTS/BTR/BTC r/m16, imm8 (0x0F 0xBA)
    case 0xBA: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      emu88_uint8 bit = fetch_ip_byte();
      emu88_uint8 op_bt = mr.reg_field;  // 4=BT, 5=BTS, 6=BTR, 7=BTC
      if (op_size_32) {
        emu88_uint32 val = get_rm32(mr);
        bit &= 31;
        set_flag_val(FLAG_CF, (val >> bit) & 1);
        if (op_bt == 5) set_rm32(mr, val | (1u << bit));
        else if (op_bt == 6) set_rm32(mr, val & ~(1u << bit));
        else if (op_bt == 7) set_rm32(mr, val ^ (1u << bit));
      } else {
        emu88_uint16 val = get_rm16(mr);
        bit &= 15;
        set_flag_val(FLAG_CF, (val >> bit) & 1);
        if (op_bt == 5) set_rm16(mr, val | (emu88_uint16(1) << bit));
        else if (op_bt == 6) set_rm16(mr, val & ~(emu88_uint16(1) << bit));
        else if (op_bt == 7) set_rm16(mr, val ^ (emu88_uint16(1) << bit));
      }
      break;
    }

    // BSF r16, r/m16 (0x0F 0xBC)
    case 0xBC: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      if (op_size_32) {
        emu88_uint32 val = get_rm32(mr);
        if (val == 0) { set_flag(FLAG_ZF); }
        else {
          clear_flag(FLAG_ZF);
          emu88_uint8 bit = 0;
          while (!(val & (1u << bit))) bit++;
          set_reg32(mr.reg_field, bit);
        }
      } else {
        emu88_uint16 val = get_rm16(mr);
        if (val == 0) { set_flag(FLAG_ZF); }
        else {
          clear_flag(FLAG_ZF);
          emu88_uint8 bit = 0;
          while (!(val & (1u << bit))) bit++;
          regs[mr.reg_field] = bit;
        }
      }
      break;
    }

    // BSR r16, r/m16 (0x0F 0xBD)
    case 0xBD: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      if (op_size_32) {
        emu88_uint32 val = get_rm32(mr);
        if (val == 0) { set_flag(FLAG_ZF); }
        else {
          clear_flag(FLAG_ZF);
          emu88_uint8 bit = 31;
          while (!(val & (1u << bit))) bit--;
          set_reg32(mr.reg_field, bit);
        }
      } else {
        emu88_uint16 val = get_rm16(mr);
        if (val == 0) { set_flag(FLAG_ZF); }
        else {
          clear_flag(FLAG_ZF);
          emu88_uint8 bit = 15;
          while (!(val & (1u << bit))) bit--;
          regs[mr.reg_field] = bit;
        }
      }
      break;
    }

    // BSWAP r32 (0x0F 0xC8-0xCF)
    case 0xC8: case 0xC9: case 0xCA: case 0xCB:
    case 0xCC: case 0xCD: case 0xCE: case 0xCF: {
      emu88_uint8 r = op2 & 7;
      emu88_uint32 val = get_reg32(r);
      set_reg32(r, ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) |
                    ((val >> 8) & 0xFF00) | ((val >> 24) & 0xFF));
      break;
    }

    // CMPXCHG r/m8, r8 (0x0F 0xB0)
    case 0xB0: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      emu88_uint8 dst = get_rm8(mr);
      emu88_uint8 al = get_reg8(reg_AL);
      emu88_uint8 tmp = al - dst;
      set_flags_sub8(al, dst, 0);
      if (al == dst) {
        set_flag(FLAG_ZF);
        set_rm8(mr, get_reg8(mr.reg_field));
      } else {
        clear_flag(FLAG_ZF);
        set_reg8(reg_AL, dst);
      }
      break;
    }

    // CMPXCHG r/m16, r16 (0x0F 0xB1)
    case 0xB1: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      if (op_size_32) {
        emu88_uint32 dst = get_rm32(mr);
        emu88_uint32 eax = get_reg32(reg_AX);
        set_flags_sub32(eax, dst, 0);
        if (eax == dst) {
          set_flag(FLAG_ZF);
          set_rm32(mr, get_reg32(mr.reg_field));
        } else {
          clear_flag(FLAG_ZF);
          set_reg32(reg_AX, dst);
        }
      } else {
        emu88_uint16 dst = get_rm16(mr);
        emu88_uint16 ax = regs[reg_AX];
        set_flags_sub16(ax, dst, 0);
        if (ax == dst) {
          set_flag(FLAG_ZF);
          set_rm16(mr, regs[mr.reg_field]);
        } else {
          clear_flag(FLAG_ZF);
          regs[reg_AX] = dst;
        }
      }
      break;
    }

    // XADD r/m8, r8 (0x0F 0xC0)
    case 0xC0: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      emu88_uint8 dst = get_rm8(mr);
      emu88_uint8 src = get_reg8(mr.reg_field);
      emu88_uint8 result = alu_add8(dst, src, 0);
      set_reg8(mr.reg_field, dst);
      set_rm8(mr, result);
      break;
    }

    // XADD r/m16, r16 (0x0F 0xC1)
    case 0xC1: {
      emu88_uint8 modrm = fetch_ip_byte();
      modrm_result mr = decode_modrm(modrm);
      if (op_size_32) {
        emu88_uint32 dst = get_rm32(mr);
        emu88_uint32 src = get_reg32(mr.reg_field);
        emu88_uint32 result = alu_add32(dst, src, 0);
        set_reg32(mr.reg_field, dst);
        set_rm32(mr, result);
      } else {
        emu88_uint16 dst = get_rm16(mr);
        emu88_uint16 src = regs[mr.reg_field];
        emu88_uint16 result = alu_add16(dst, src, 0);
        regs[mr.reg_field] = dst;
        set_rm16(mr, result);
      }
      break;
    }

    default:
      emu88_fatal("Unimplemented 0x0F opcode: 0x%02X at %04X:%04X", op2, sregs[seg_CS], ip - 2);
      halted = true;
      break;
    }
    break;
  }

  case 0xD6: // SALC (undocumented: set AL to 0xFF if CF, else 0x00)
    set_reg8(reg_AL, get_flag(FLAG_CF) ? 0xFF : 0x00);
    break;

  default:
    unimplemented_opcode(opcode);
    break;
  }
}
