#ifndef EMU88_H
#define EMU88_H

#include "emu88_mem.h"
#include "emu88_trace.h"

class emu88 {
public:
  // 8088 register indices for general-purpose registers
  enum RegIndex16 {
    reg_AX = 0, reg_CX = 1, reg_DX = 2, reg_BX = 3,
    reg_SP = 4, reg_BP = 5, reg_SI = 6, reg_DI = 7
  };

  // 8-bit register indices (as encoded in mod/rm)
  enum RegIndex8 {
    reg_AL = 0, reg_CL = 1, reg_DL = 2, reg_BL = 3,
    reg_AH = 4, reg_CH = 5, reg_DH = 6, reg_BH = 7
  };

  // Segment register indices
  enum SegIndex {
    seg_ES = 0, seg_CS = 1, seg_SS = 2, seg_DS = 3,
    seg_FS = 4, seg_GS = 5
  };

  // FLAGS bit positions
  enum FlagBits {
    FLAG_CF = 0x0001,   // Carry
    FLAG_PF = 0x0004,   // Parity
    FLAG_AF = 0x0010,   // Auxiliary carry
    FLAG_ZF = 0x0040,   // Zero
    FLAG_SF = 0x0080,   // Sign
    FLAG_TF = 0x0100,   // Trap
    FLAG_IF = 0x0200,   // Interrupt enable
    FLAG_DF = 0x0400,   // Direction
    FLAG_OF = 0x0800    // Overflow
  };

  // General-purpose registers (AX, CX, DX, BX, SP, BP, SI, DI)
  emu88_uint16 regs[8];

  // Upper 16 bits of 32-bit registers (386+: EAX=regs_hi[0]:regs[0], etc.)
  emu88_uint16 regs_hi[8];

  // Segment registers (ES, CS, SS, DS, FS, GS)
  emu88_uint16 sregs[6];

  // Instruction pointer
  emu88_uint16 ip;

  // Flags register
  emu88_uint16 flags;

  // Upper 16 bits of EFLAGS (386+)
  emu88_uint16 eflags_hi;

  // System registers (286+/386+)
  emu88_uint32 gdtr_base;
  emu88_uint16 gdtr_limit;
  emu88_uint32 idtr_base;
  emu88_uint16 idtr_limit;
  emu88_uint32 cr0;  // Machine Status Word (MSW) is low 16 bits

  // Memory and trace
  emu88_mem *mem;
  emu88_trace *trace;
  bool debug;

  // Cycle counter
  unsigned long long cycles;

  // Interrupt state
  bool int_pending;
  emu88_uint8 int_vector;
  bool halted;

  // Segment override state (per-instruction)
  int seg_override;       // -1 = none, 0-5 = seg index

  // REP prefix state
  enum RepType { REP_NONE, REP_REPZ, REP_REPNZ };
  RepType rep_prefix;

  // 386 operand/address size prefix state (per-instruction)
  bool op_size_32;        // true when 0x66 prefix active
  bool addr_size_32;      // true when 0x67 prefix active

  // Parity lookup table
  emu88_uint8 parity_table[256];

  // Constructor/destructor
  emu88(emu88_mem *memory);
  virtual ~emu88() = default;

  // I/O port operations - override in subclass
  virtual void port_out(emu88_uint16 port, emu88_uint8 value);
  virtual emu88_uint8 port_in(emu88_uint16 port);
  virtual void port_out16(emu88_uint16 port, emu88_uint16 value);
  virtual emu88_uint16 port_in16(emu88_uint16 port);

  // Interrupt support
  virtual void do_interrupt(emu88_uint8 vector);
  void request_int(emu88_uint8 vector);
  bool check_interrupts(void);
  virtual void halt_cpu(void);
  virtual void unimplemented_opcode(emu88_uint8 opcode);

  // Configuration
  virtual void set_debug(bool new_debug) { debug = new_debug; }
  virtual void set_trace(emu88_trace *new_trace) { trace = new_trace; }

  // Register access: 8-bit
  emu88_uint8 get_reg8(emu88_uint8 r) const;
  void set_reg8(emu88_uint8 r, emu88_uint8 val);

  // Register access: 16-bit
  emu88_uint16 get_reg16(emu88_uint8 r) const { return regs[r]; }
  void set_reg16(emu88_uint8 r, emu88_uint16 val) { regs[r] = val; }

  // Register access: 32-bit (386+) — combines regs_hi:regs
  emu88_uint32 get_reg32(emu88_uint8 r) const {
    return EMU88_MK32(regs[r], regs_hi[r]);
  }
  void set_reg32(emu88_uint8 r, emu88_uint32 val) {
    regs[r] = val & 0xFFFF;
    regs_hi[r] = (val >> 16) & 0xFFFF;
  }

  // EFLAGS access (386+)
  emu88_uint32 get_eflags() const { return EMU88_MK32(flags, eflags_hi); }
  void set_eflags(emu88_uint32 val) { flags = val & 0xFFFF; eflags_hi = (val >> 16) & 0xFFFF; }

  // Operand size helpers
  bool operand_32() const { return op_size_32; }
  bool address_32() const { return addr_size_32; }

  // Segment register access
  emu88_uint16 get_sreg(emu88_uint8 s) const { return sregs[s]; }
  void set_sreg(emu88_uint8 s, emu88_uint16 val) { sregs[s] = val; }

  // Flags helpers
  bool get_flag(emu88_uint16 f) const { return (flags & f) != 0; }
  void set_flag(emu88_uint16 f) { flags |= f; }
  void clear_flag(emu88_uint16 f) { flags &= ~f; }
  void set_flag_val(emu88_uint16 f, bool val) {
    if (val) flags |= f; else flags &= ~f;
  }

  // Flag computation — 8-bit and 16-bit (8088)
  void set_flags_zsp8(emu88_uint8 val);
  void set_flags_zsp16(emu88_uint16 val);
  void set_flags_add8(emu88_uint8 a, emu88_uint8 b, emu88_uint8 carry);
  void set_flags_add16(emu88_uint16 a, emu88_uint16 b, emu88_uint16 carry);
  void set_flags_sub8(emu88_uint8 a, emu88_uint8 b, emu88_uint8 borrow);
  void set_flags_sub16(emu88_uint16 a, emu88_uint16 b, emu88_uint16 borrow);
  void set_flags_logic8(emu88_uint8 result);
  void set_flags_logic16(emu88_uint16 result);

  // Flag computation — 32-bit (386+)
  void set_flags_zsp32(emu88_uint32 val);
  void set_flags_add32(emu88_uint32 a, emu88_uint32 b, emu88_uint32 carry);
  void set_flags_sub32(emu88_uint32 a, emu88_uint32 b, emu88_uint32 borrow);
  void set_flags_logic32(emu88_uint32 result);

  // Memory access (segment-aware)
  emu88_uint32 effective_address(emu88_uint16 seg, emu88_uint16 off) const {
    return EMU88_MK20(seg, off);
  }
  emu88_uint16 default_segment(void) const;
  emu88_uint8 fetch_byte(emu88_uint16 seg, emu88_uint16 off);
  void store_byte(emu88_uint16 seg, emu88_uint16 off, emu88_uint8 val);
  emu88_uint16 fetch_word(emu88_uint16 seg, emu88_uint16 off);
  void store_word(emu88_uint16 seg, emu88_uint16 off, emu88_uint16 val);

  // Memory access: 32-bit (386+)
  emu88_uint32 fetch_dword(emu88_uint16 seg, emu88_uint16 off);
  void store_dword(emu88_uint16 seg, emu88_uint16 off, emu88_uint32 val);

  // Instruction stream
  emu88_uint8 fetch_ip_byte(void);
  emu88_uint16 fetch_ip_word(void);
  emu88_uint32 fetch_ip_dword(void);

  // Stack operations
  void push_word(emu88_uint16 val);
  emu88_uint16 pop_word(void);
  void push_dword(emu88_uint32 val);
  emu88_uint32 pop_dword(void);

  // ModR/M decoding
  struct modrm_result {
    emu88_uint16 seg;       // segment for memory operand
    emu88_uint32 offset;    // offset for memory operand (32-bit for 386+ addressing)
    emu88_uint8 reg_field;  // reg field (bits 5-3)
    emu88_uint8 rm_field;   // r/m field (bits 2-0)
    emu88_uint8 mod_field;  // mod field (bits 7-6)
    bool is_register;       // true if r/m refers to register, not memory
  };
  modrm_result decode_modrm(emu88_uint8 modrm);
  modrm_result decode_modrm_32(emu88_uint8 modrm);  // 386+ 32-bit addressing with SIB

  // Get/set operand from modrm — 8-bit and 16-bit (8088)
  emu88_uint8 get_rm8(const modrm_result &mr);
  void set_rm8(const modrm_result &mr, emu88_uint8 val);
  emu88_uint16 get_rm16(const modrm_result &mr);
  void set_rm16(const modrm_result &mr, emu88_uint16 val);

  // Get/set operand from modrm — 32-bit (386+)
  emu88_uint32 get_rm32(const modrm_result &mr);
  void set_rm32(const modrm_result &mr, emu88_uint32 val);

  // String operations support
  emu88_uint16 string_src_seg(void) const;

  // Execute one instruction
  virtual void execute(void);

  // Debug
  virtual void debug_dump_regs(const char *label);

  // Initialization
  void setup_parity(void);
  void reset(void);

private:
  // ALU helpers — 8-bit and 16-bit (8088)
  emu88_uint8 alu_add8(emu88_uint8 a, emu88_uint8 b, emu88_uint8 carry);
  emu88_uint16 alu_add16(emu88_uint16 a, emu88_uint16 b, emu88_uint16 carry);
  emu88_uint8 alu_sub8(emu88_uint8 a, emu88_uint8 b, emu88_uint8 borrow);
  emu88_uint16 alu_sub16(emu88_uint16 a, emu88_uint16 b, emu88_uint16 borrow);
  emu88_uint8 alu_inc8(emu88_uint8 val);
  emu88_uint8 alu_dec8(emu88_uint8 val);
  emu88_uint16 alu_inc16(emu88_uint16 val);
  emu88_uint16 alu_dec16(emu88_uint16 val);

  // ALU helpers — 32-bit (386+)
  emu88_uint32 alu_add32(emu88_uint32 a, emu88_uint32 b, emu88_uint32 carry);
  emu88_uint32 alu_sub32(emu88_uint32 a, emu88_uint32 b, emu88_uint32 borrow);
  emu88_uint32 alu_inc32(emu88_uint32 val);
  emu88_uint32 alu_dec32(emu88_uint32 val);

  // Group instruction helpers
  void execute_alu_rm8_r8(emu88_uint8 op);
  void execute_alu_rm16_r16(emu88_uint8 op);
  void execute_alu_r8_rm8(emu88_uint8 op);
  void execute_alu_r16_rm16(emu88_uint8 op);
  void execute_alu_al_imm8(emu88_uint8 op);
  void execute_alu_ax_imm16(emu88_uint8 op);
  void execute_grp1_rm8(const modrm_result &mr, emu88_uint8 imm);
  void execute_grp1_rm16(const modrm_result &mr, emu88_uint16 imm);
  void execute_grp2_rm8(const modrm_result &mr, emu88_uint8 count);
  void execute_grp2_rm16(const modrm_result &mr, emu88_uint8 count);
  void execute_grp3_rm8(emu88_uint8 modrm);
  void execute_grp3_rm16(emu88_uint8 modrm);
  void execute_grp4_rm8(emu88_uint8 modrm);
  void execute_grp5_rm16(emu88_uint8 modrm);

  // ALU operation dispatch (ADD=0, OR=1, ADC=2, SBB=3, AND=4, SUB=5, XOR=6, CMP=7)
  emu88_uint8 do_alu8(emu88_uint8 op, emu88_uint8 a, emu88_uint8 b);
  emu88_uint16 do_alu16(emu88_uint8 op, emu88_uint16 a, emu88_uint16 b);
  emu88_uint32 do_alu32(emu88_uint8 op, emu88_uint32 a, emu88_uint32 b);

  // Shift/rotate helpers
  emu88_uint8 do_shift8(emu88_uint8 op, emu88_uint8 val, emu88_uint8 count);
  emu88_uint16 do_shift16(emu88_uint8 op, emu88_uint16 val, emu88_uint8 count);
  emu88_uint32 do_shift32(emu88_uint8 op, emu88_uint32 val, emu88_uint8 count);

  // Group instruction helpers — 32-bit (386+)
  void execute_grp1_rm32(const modrm_result &mr, emu88_uint32 imm);
  void execute_grp2_rm32(const modrm_result &mr, emu88_uint8 count);
  void execute_grp3_rm32(emu88_uint8 modrm_byte);
  void execute_grp5_rm32(emu88_uint8 modrm_byte);

  // String instruction helpers
  void execute_string_op(emu88_uint8 opcode);
};

#endif // EMU88_H
