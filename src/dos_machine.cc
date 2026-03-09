#include "dos_machine.h"
#include <cstdio>
#include <cstring>

//=============================================================================
// Constructor
//=============================================================================

dos_machine::dos_machine(emu88_mem *memory, dos_io *io)
    : emu88(memory), io(io),
      video_mode(3), screen_cols(80), screen_rows(25),
      pic_imr(0xFF), pic_vector_base(0x08), pic_init_step(0),
      port_b(0), crtc_index(0),
      tick_cycle_mark(0), refresh_cycle_mark(0),
      speed_mode(SPEED_FULL), target_cps(0),
      banner_shown(false),
      waiting_for_key(false),
      kbd_poll_count(0),
      kbd_poll_start_cycle(0),
      kbd_cmd_pending(0),
      nic(nullptr), ne2000_base(0x300), ne2000_irq(3)
{
  memset(pit_counter, 0, sizeof(pit_counter));
  memset(pit_mode, 0, sizeof(pit_mode));
  memset(pit_access, 0, sizeof(pit_access));
  memset(pit_write_phase, 0, sizeof(pit_write_phase));
  memset(pit_read_phase, 0, sizeof(pit_read_phase));
  memset(pit_latch_pending, 0, sizeof(pit_latch_pending));
  memset(pit_latch_value, 0, sizeof(pit_latch_value));
  memset(crtc_regs, 0, sizeof(crtc_regs));
  memset(bios_entry, 0, sizeof(bios_entry));
  memset(vga_dac, 0, sizeof(vga_dac));
  dac_write_index = 0;
  dac_read_index = 0;
  dac_component = 0;
  dac_pel_mask = 0xFF;
}

dos_machine::~dos_machine() {
  delete nic;
}

//=============================================================================
// BDA helpers
//=============================================================================

void dos_machine::bda_w8(int off, uint8_t v) {
  mem->store_mem(0x400 + off, v);
}
void dos_machine::bda_w16(int off, uint16_t v) {
  mem->store_mem16(0x400 + off, v);
}
void dos_machine::bda_w32(int off, uint32_t v) {
  mem->store_mem16(0x400 + off, v & 0xFFFF);
  mem->store_mem16(0x400 + off + 2, (v >> 16) & 0xFFFF);
}
uint8_t dos_machine::bda_r8(int off) {
  return mem->fetch_mem(0x400 + off);
}
uint16_t dos_machine::bda_r16(int off) {
  return mem->fetch_mem16(0x400 + off);
}
uint32_t dos_machine::bda_r32(int off) {
  return mem->fetch_mem16(0x400 + off) |
         ((uint32_t)mem->fetch_mem16(0x400 + off + 2) << 16);
}

//=============================================================================
// Configuration
//=============================================================================

void dos_machine::configure(const Config &cfg) {
  config = cfg;
  set_speed(cfg.speed);
}

//=============================================================================
// Machine initialization
//=============================================================================

void dos_machine::init_machine() {
  reset();
  init_ivt();
  init_bda();
  install_bios_stubs();

  // Initialize PIC (8259A) - normally done by BIOS POST
  // ICW1→ICW2→ICW3→ICW4, then set IMR
  pic_vector_base = 0x08;  // IRQ 0-7 → INT 08h-0Fh
  pic_init_step = 0;
  pic_icw4_needed = false;
  pic_imr = 0xBC;  // Unmask IRQ0 (timer), IRQ1 (keyboard), IRQ6 (floppy)

  // Install mouse if enabled
  if (config.mouse_enabled && io->mouse_present()) {
    mouse.installed = true;
    mouse.x = 320;
    mouse.y = 100;
    mouse.buttons = 0;
    mouse.visible = false;
  }

  // Initialize NE2000 NIC if enabled
  if (config.ne2000_enabled) {
    if (!nic) nic = new ne2000();
    nic->reset();
    uint8_t mac[] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    nic->set_mac(mac);
    ne2000_base = config.ne2000_iobase;
    ne2000_irq = config.ne2000_irq;
    nic->on_transmit = [this](const uint8_t *data, int len) {
      io->net_send(data, len);
    };
  }
}

void dos_machine::init_ivt() {
  // Clear IVT (256 vectors x 4 bytes = 1KB)
  for (int i = 0; i < 0x400; i++)
    mem->store_mem(i, 0);

  // Place BIOS trap stubs in ROM at F000:E000 + vector*4
  // Each stub: BIOS_TRAP_OPCODE, vector_number, IRET(0xCF)
  uint16_t rom_base = 0xE000;

  for (int vec = 0; vec < 256; vec++) {
    uint16_t entry = rom_base + vec * 4;
    uint32_t addr = BIOS_ROM_BASE + entry;
    mem->store_mem(addr,     BIOS_TRAP_OPCODE);
    mem->store_mem(addr + 1, (uint8_t)vec);
    mem->store_mem(addr + 2, 0xCF);  // IRET fallback
    bios_entry[vec] = entry;
  }

  // Set ALL IVT entries to point to BIOS ROM stubs (IRET fallbacks)
  // This prevents unhandled INTs from jumping to 0000:0000
  for (int vec = 0; vec < 256; vec++) {
    uint32_t ivt_addr = vec * 4;
    mem->store_mem16(ivt_addr,     bios_entry[vec]);
    mem->store_mem16(ivt_addr + 2, 0xF000);
  }

  // INT 1Eh -> Disk parameter table (set up in install_bios_stubs)
  // INT 1Fh -> Graphics char table (not needed for text modes)
}

void dos_machine::init_bda() {
  for (int i = 0; i < 256; i++)
    mem->store_mem(0x400 + i, 0);

  // Equipment word: bit0=floppy present, bits4-5=initial video mode
  // 00=EGA/VGA, 01=40x25 CGA, 10=80x25 CGA, 11=MDA/Hercules
  uint16_t equip = 0x0001;  // floppy present
  bool use_mda = (config.display == DISPLAY_MDA || config.display == DISPLAY_HERCULES);
  bool use_ega_vga = (config.display == DISPLAY_EGA || config.display == DISPLAY_VGA);
  if (use_mda) {
    equip |= 0x0030;  // bits 4-5 = 11 (MDA)
  } else if (use_ega_vga) {
    equip |= 0x0000;  // bits 4-5 = 00 (EGA/VGA)
  } else {
    equip |= 0x0020;  // bits 4-5 = 10 (80x25 CGA)
  }
  // Mouse: bit 2 (PS/2 pointing device)
  if (config.mouse_enabled)
    equip |= 0x0004;
  bda_w16(bda::EQUIPMENT, equip);

  // Conventional memory: 640KB
  bda_w16(bda::MEM_SIZE_KB, 640);

  // Video setup based on display adapter
  int init_mode = use_mda ? 7 : 3;
  bda_w8(bda::VIDEO_MODE, init_mode);
  bda_w16(bda::SCREEN_COLS, 80);
  bda_w16(bda::VIDEO_PAGE_SZ, 4096);
  bda_w16(bda::VIDEO_PAGE_OFF, 0);
  bda_w8(bda::ACTIVE_PAGE, 0);
  bda_w16(bda::CRTC_BASE, use_mda ? 0x3B4 : 0x3D4);
  bda_w8(bda::SCREEN_ROWS, 24);  // 25 rows - 1
  bda_w16(bda::CURSOR_SHAPE, 0x0607);

  // Keyboard buffer (empty circular buffer)
  bda_w16(bda::KBD_BUF_HEAD, 0x1E);
  bda_w16(bda::KBD_BUF_TAIL, 0x1E);
  bda_w16(bda::KBD_BUF_START, 0x1E);
  bda_w16(bda::KBD_BUF_END, 0x3E);

  // Hard disk count
  int nhdd = 0;
  for (int d = 0x80; d < 0x84; d++)
    if (io->disk_present(d)) nhdd++;
  bda_w8(bda::NUM_HDD, nhdd);
}

void dos_machine::install_bios_stubs() {
  // BIOS date at F000:FFF5
  const char *date = "03/08/26";
  for (int i = 0; i < 8; i++)
    mem->store_mem(BIOS_ROM_BASE + 0xFFF5 + i, date[i]);

  // Model ID at F000:FFFE (0xFF = IBM PC)
  mem->store_mem(BIOS_ROM_BASE + 0xFFFE, 0xFF);

  // Reset vector at F000:FFF0 -> JMP FAR to bootstrap trap
  uint16_t boot_entry = bios_entry[0x19];  // INT 19h entry
  uint32_t reset = BIOS_ROM_BASE + 0xFFF0;
  mem->store_mem(reset,     0xEA);         // JMP FAR
  mem->store_mem16(reset+1, boot_entry);   // offset
  mem->store_mem16(reset+3, 0xF000);       // segment

  // System configuration table at F000:E6F5 (for INT 15h AH=C0h)
  uint32_t sct = BIOS_ROM_BASE + 0xE6F5;
  uint8_t sct_data[] = {
    0x08, 0x00,  // Table length (8 bytes following)
    0xFF,        // Model ID (0xFF = IBM PC)
    0x00,        // Submodel (0x00)
    0x01,        // BIOS revision level
    0x74,        // Feature byte 1: DMA ch3, cascade int, RTC, kbd intercept
    0x00,        // Feature byte 2
    0x00,        // Feature byte 3
    0x00, 0x00   // Reserved
  };
  for (int i = 0; i < 10; i++)
    mem->store_mem(sct + i, sct_data[i]);

  // Disk parameter table at F000:EFC7 (for INT 1Eh)
  uint32_t dpt = BIOS_ROM_BASE + 0xEFC7;
  uint8_t dpt_data[] = {
    0xDF, 0x02, 0x25, 0x02, 18, 0x1B, 0xFF, 0x54, 0xF6, 0x0F, 0x08
  };
  for (int i = 0; i < 11; i++)
    mem->store_mem(dpt + i, dpt_data[i]);
  mem->store_mem16(0x1E * 4,     0xEFC7);
  mem->store_mem16(0x1E * 4 + 2, 0xF000);

  // XMS entry point at F000:EFD8 - called via FAR CALL by XMS clients
  // Uses BIOS trap opcode (0xF1) with vector 0xFE (reserved for XMS dispatch)
  // Then RETF to return to caller
  xms_entry_off = 0xEFD8;
  uint32_t xms_addr = BIOS_ROM_BASE + xms_entry_off;
  mem->store_mem(xms_addr,     BIOS_TRAP_OPCODE);  // 0xF1
  mem->store_mem(xms_addr + 1, 0xFE);              // XMS vector marker
  mem->store_mem(xms_addr + 2, 0xCB);              // RETF

  // Initialize XMS handles
  for (int i = 0; i < XMS_MAX_HANDLES; i++) {
    xms_handles[i].allocated = false;
    xms_handles[i].base = 0;
    xms_handles[i].size_kb = 0;
    xms_handles[i].lock_count = 0;
  }
}

//=============================================================================
// Boot
//=============================================================================

bool dos_machine::boot(int drive) {
  init_machine();

  if (!io->disk_present(drive)) {
    fprintf(stderr, "Boot drive 0x%02X not present\n", drive);
    return false;
  }

  // Read boot sector to 0000:7C00
  uint8_t sector[512];
  size_t n = io->disk_read(drive, 0, sector, 512);
  if (n < 512) {
    fprintf(stderr, "Failed to read boot sector\n");
    return false;
  }
  if (sector[510] != 0x55 || sector[511] != 0xAA)
    fprintf(stderr, "Warning: boot sector missing 55AA signature\n");

  for (int i = 0; i < 512; i++)
    mem->store_mem(BOOT_LOAD_ADDR + i, sector[i]);

  // Set CPU state for boot
  sregs[seg_CS] = 0x0000;
  ip = 0x7C00;
  sregs[seg_DS] = 0x0000;
  sregs[seg_ES] = 0x0000;
  sregs[seg_SS] = 0x0000;
  regs[reg_SP] = 0x7C00;
  regs[reg_DX] = drive;   // DL = boot drive
  halted = false;

  bool use_mda = (config.display == DISPLAY_MDA || config.display == DISPLAY_HERCULES);
  int init_mode = use_mda ? 7 : 3;
  video_set_mode(init_mode);
  banner_shown = false;

  io->video_mode_changed(init_mode, 80, 25);
  return true;
}

//=============================================================================
// Run loop
//=============================================================================

void dos_machine::set_speed(SpeedMode mode) {
  speed_mode = mode;
  // CPS values for 386+ speeds are inflated ~3-4x to compensate for
  // the 8088-based cycle table (8088 instructions cost ~3x more cycles
  // than 386 instructions, so we need higher CPS to match wall-clock speed).
  switch (mode) {
    case SPEED_FULL:       target_cps = 0; break;
    case SPEED_PC_4_77:    target_cps = 4770000; break;
    case SPEED_AT_8:       target_cps = 8000000; break;
    case SPEED_386SX_16:   target_cps = 48000000; break;
    case SPEED_386DX_33:   target_cps = 100000000; break;
    case SPEED_486DX2_66:  target_cps = 260000000; break;
  }
}

bool dos_machine::run_batch(int count) {
  waiting_for_key = false;
  kbd_poll_count = 0;

  for (int i = 0; i < count; i++) {
    if (waiting_for_key) {
      // Program is genuinely idle at a keyboard prompt (passed both
      // poll count AND emulated time thresholds). Yield to host.

      // Fast-forward cycles to deliver a timer tick so guest timeouts
      // still work (e.g. FreeDOS F5/F8 boot prompt). Same approach
      // as HLT idle handling below.
      cycles = tick_cycle_mark + CYCLES_PER_TICK;
      tick_cycle_mark = cycles;
      uint32_t ticks = bda_r32(bda::TIMER_COUNT) + 1;
      if (ticks >= 0x1800B0) {
        ticks = 0;
        bda_w8(bda::TIMER_ROLLOVER, 1);
      }
      bda_w32(bda::TIMER_COUNT, ticks);
      if (get_flag(FLAG_IF) && !(pic_imr & 0x01))
        request_int(pic_vector_base);

      if (video_mode == 0x13) {
        io->video_refresh_gfx(mem->get_mem() + VGA_VRAM_BASE, 320, 200, vga_dac);
      } else {
        uint32_t base = vram_base();
        io->video_refresh(mem->get_mem() + base, screen_cols, screen_rows);
        int page = bda_r8(bda::ACTIVE_PAGE);
        uint16_t pos = bda_r16(bda::CURSOR_POS + page * 2);
        io->video_set_cursor((pos >> 8) & 0xFF, pos & 0xFF);
      }
      return true;
    }

    // Handle HLT: fast-forward to next timer tick and deliver interrupt,
    // then exit batch so the host can yield CPU time (DOSBox-style idle)
    if (halted) {
      if (!get_flag(FLAG_IF)) return false;  // HLT with IF=0 is permanent halt
      cycles = tick_cycle_mark + CYCLES_PER_TICK;
    } else {
      execute();
    }

    // Timer tick (cycle-based: 18.2 Hz at 4.77 MHz)
    if (cycles - tick_cycle_mark >= CYCLES_PER_TICK) {
      tick_cycle_mark = cycles;
      uint32_t ticks = bda_r32(bda::TIMER_COUNT) + 1;
      if (ticks >= 0x1800B0) {
        ticks = 0;
        bda_w8(bda::TIMER_ROLLOVER, 1);
      }
      bda_w32(bda::TIMER_COUNT, ticks);

      if (get_flag(FLAG_IF) && !(pic_imr & 0x01))
        request_int(pic_vector_base);

      // After delivering the timer tick that wakes from HLT, exit the
      // batch so the host run loop can yield (saves battery on mobile)
      if (halted) {
        check_interrupts();  // deliver the tick interrupt to un-halt
        break;
      }
    }

    // Video refresh (cycle-based: ~30 Hz)
    if (cycles - refresh_cycle_mark >= CYCLES_PER_REFRESH) {
      refresh_cycle_mark = cycles;
      if (video_mode == 0x13) {
        // VGA mode 13h: send 320x200 framebuffer + palette
        io->video_refresh_gfx(mem->get_mem() + VGA_VRAM_BASE, 320, 200, vga_dac);
      } else {
        uint32_t base = vram_base();
        io->video_refresh(mem->get_mem() + base, screen_cols, screen_rows);

        int page = bda_r8(bda::ACTIVE_PAGE);
        uint16_t pos = bda_r16(bda::CURSOR_POS + page * 2);
        io->video_set_cursor((pos >> 8) & 0xFF, pos & 0xFF);
      }
    }

    // NE2000: poll for incoming packets and deliver IRQ (every 1024 insns)
    if (nic && (i & 0x3FF) == 0) {
      if (io->net_available()) {
        uint8_t pkt[1600];
        int len = io->net_receive(pkt, sizeof(pkt));
        if (len > 0) nic->receive(pkt, len);
      }
      if (nic->irq_active() && get_flag(FLAG_IF) &&
          !(pic_imr & (1 << ne2000_irq)))
        request_int(pic_vector_base + ne2000_irq);
    }

    check_interrupts();
  }
  // HLT with IF=1 is just "waiting for interrupt" (idle), not a dead halt
  return !halted || get_flag(FLAG_IF);
}

//=============================================================================
// Interrupt dispatch - BIOS trapping
//=============================================================================

void dos_machine::dispatch_bios(uint8_t vector) {
  // Reset keyboard poll counter on interrupts that indicate program activity.
  // Exclude timer (08/1C), time (1A), keyboard (16), and video (10).
  // Video is excluded because vask-style polling loops interleave INT 16h AH=01
  // (check key) with INT 10h AH=02 (cursor blink) - resetting on INT 10h would
  // prevent the poll counter from ever reaching the threshold.
  if (vector != 0x08 && vector != 0x1C && vector != 0x1A && vector != 0x16
      && vector != 0x10)
    kbd_poll_count = 0;

  switch (vector) {
    case 0x08: bios_int08h(); break;
    case 0x10: bios_int10h(); break;
    case 0x11: bios_int11h(); break;
    case 0x12: bios_int12h(); break;
    case 0x13: bios_int13h(); break;
    case 0x14: bios_int14h(); break;
    case 0x15: bios_int15h(); break;
    case 0x16: bios_int16h(); break;
    case 0x17: bios_int17h(); break;
    case 0x19: bios_int19h(); break;
    case 0x1A: bios_int1ah(); break;
    case 0x1C: break;  // User timer hook - default is no-op
    case 0x2F: bios_int2fh(); break;
    case 0x33: bios_int33h(); break;
    case 0xE0: bios_int_e0h(); break;  // Host file services
    default: break;
  }
}

void dos_machine::do_interrupt(emu88_uint8 vector) {
  // Check if IVT still points to our BIOS stub (not hooked by DOS)
  uint32_t ivt = vector * 4;
  uint16_t off = mem->fetch_mem16(ivt);
  uint16_t seg = mem->fetch_mem16(ivt + 2);

  if (seg == 0xF000 && off == bios_entry[vector]) {
    // Fast path: trap directly, no push/jump
    dispatch_bios(vector);
    if (waiting_for_key) {
      // Rewind IP to re-execute this INT instruction next batch.
      // IP currently points past the INT xx bytes (2 bytes).
      ip -= 2;
    }
    return;
  }

  // Hooked by DOS/TSR - let normal interrupt flow happen.
  // Our ROM stub will catch it via unimplemented_opcode when the
  // chain reaches our entry point.
  // Reset keyboard poll counter since non-BIOS interrupt = program activity
  // (exclude timer, time, and video - see dispatch_bios comment)
  if (vector != 0x08 && vector != 0x1C && vector != 0x1A && vector != 0x10)
    kbd_poll_count = 0;
  emu88::do_interrupt(vector);
}

//=============================================================================
// Unimplemented opcode - catches BIOS ROM trap stubs
//=============================================================================

void dos_machine::unimplemented_opcode(emu88_uint8 opcode) {
  if (opcode == BIOS_TRAP_OPCODE) {
    uint8_t vector = fetch_ip_byte();
    if (vector == 0xFE) {
      // XMS entry point - reached via FAR CALL, not INT
      // Stack has IP, CS (no FLAGS) - RETF follows in ROM
      xms_dispatch();
      return;
    }
    // Normal BIOS trap stub (reached via interrupt chain)
    dispatch_bios(vector);
    if (waiting_for_key) {
      // Don't IRET — rewind IP to the start of this F1 xx stub so the
      // trap re-fires on the next batch.  The stack (return address from
      // the INT/CALL that got us here) stays intact; when a key finally
      // arrives, the handler sets AX and waiting_for_key stays false,
      // so we fall through to the IRET below and return to the caller.
      ip -= 2;
      return;
    }
    // IRET: pop IP, CS, FLAGS (they were pushed by the INT instruction)
    ip = pop_word();
    sregs[seg_CS] = pop_word();
    flags = pop_word();
    return;
  }
  emu88::unimplemented_opcode(opcode);
}

//=============================================================================
// Port I/O
//=============================================================================

void dos_machine::port_out(emu88_uint16 port, emu88_uint8 value) {
  switch (port) {
    // --- PIC (8259A) Master ---
    case 0x20:
      if (value & 0x10) {
        pic_init_step = 1;  // ICW1
        pic_icw4_needed = (value & 0x01);
      }
      // else: OCW (EOI etc.)
      break;
    case 0x21:
      if (pic_init_step == 1) {
        pic_vector_base = value; pic_init_step = 2;  // ICW2
      } else if (pic_init_step == 2) {
        pic_init_step = pic_icw4_needed ? 3 : 0;  // ICW3, then ICW4 if needed
      } else if (pic_init_step == 3) {
        pic_init_step = 0;  // ICW4
      } else {
        pic_imr = value;     // OCW1
      }
      break;

    // --- PIT (8253) ---
    case 0x40: case 0x41: case 0x42: {
      int ch = port - 0x40;
      if (pit_access[ch] == 3) {
        if (pit_write_phase[ch] == 0) {
          pit_counter[ch] = (pit_counter[ch] & 0xFF00) | value;
          pit_write_phase[ch] = 1;
        } else {
          pit_counter[ch] = (pit_counter[ch] & 0x00FF) | ((uint16_t)value << 8);
          pit_write_phase[ch] = 0;
        }
      } else if (pit_access[ch] == 1) {
        pit_counter[ch] = value;
      } else if (pit_access[ch] == 2) {
        pit_counter[ch] = (uint16_t)value << 8;
      }
      break;
    }
    case 0x43: {
      int ch = (value >> 6) & 3;
      if (ch == 3) break;
      int access = (value >> 4) & 3;
      if (access == 0) {
        pit_latch_value[ch] = pit_counter[ch];
        pit_latch_pending[ch] = true;
        pit_read_phase[ch] = 0;
      } else {
        pit_access[ch] = access;
        pit_mode[ch] = (value >> 1) & 7;
        pit_write_phase[ch] = 0;
        pit_read_phase[ch] = 0;
      }
      break;
    }

    // --- Keyboard controller / A20 ---
    case 0x60:
      // Keyboard data port - also used for A20 control
      if (kbd_cmd_pending == 0xD1) {  // Write output port
        bool a20 = (value & 0x02) != 0;
        mem->set_a20(a20);
        kbd_cmd_pending = 0;
      }
      break;
    case 0x61: port_b = value; break;
    case 0x64:
      // Keyboard command port
      if (value == 0xD1) {
        kbd_cmd_pending = 0xD1;  // Next byte to port 0x60 = output port
      } else if (value == 0xDD) {
        mem->set_a20(false);  // Disable A20
      } else if (value == 0xDF) {
        mem->set_a20(true);   // Enable A20
      } else if (value == 0xD0) {
        kbd_cmd_pending = 0xD0;  // Read output port (handled in port_in 0x60)
      }
      break;

    // --- Fast A20 gate (port 0x92) ---
    case 0x92:
      mem->set_a20((value & 0x02) != 0);
      break;

    // --- CGA CRTC ---
    case 0x3D4: case 0x3B4:
      crtc_index = value;
      break;
    case 0x3D5: case 0x3B5:
      crtc_regs[crtc_index] = value;
      break;
    case 0x3D8: case 0x3B8: break;  // Mode control
    case 0x3D9: break;               // Color select

    // --- VGA DAC ---
    case 0x3C6: dac_pel_mask = value; break;
    case 0x3C7: dac_read_index = value; dac_component = 0; break;
    case 0x3C8: dac_write_index = value; dac_component = 0; break;
    case 0x3C9:
      vga_dac[dac_write_index][dac_component] = value & 0x3F;
      dac_component++;
      if (dac_component >= 3) {
        dac_component = 0;
        dac_write_index++;
      }
      break;

    default:
      // NE2000 NIC (32 ports at ne2000_base)
      if (nic && port >= ne2000_base && port < ne2000_base + 0x20) {
        nic->iowrite(port - ne2000_base, value);
        return;
      }
      break;
  }
}

emu88_uint8 dos_machine::port_in(emu88_uint16 port) {
  switch (port) {
    // --- PIC ---
    case 0x20: return 0;
    case 0x21: return pic_imr;

    // --- PIT ---
    case 0x40: case 0x41: case 0x42: {
      int ch = port - 0x40;
      uint16_t val = pit_latch_pending[ch] ? pit_latch_value[ch] : pit_counter[ch];
      if (pit_access[ch] == 3) {
        if (pit_read_phase[ch] == 0) {
          pit_read_phase[ch] = 1;
          return val & 0xFF;
        } else {
          pit_read_phase[ch] = 0;
          pit_latch_pending[ch] = false;
          return (val >> 8) & 0xFF;
        }
      } else if (pit_access[ch] == 1) {
        pit_latch_pending[ch] = false;
        return val & 0xFF;
      } else {
        pit_latch_pending[ch] = false;
        return (val >> 8) & 0xFF;
      }
    }

    // --- Keyboard controller ---
    case 0x60:
      if (kbd_cmd_pending == 0xD0) {
        // Read output port: bit 1 = A20 status
        kbd_cmd_pending = 0;
        return mem->get_a20() ? 0x02 : 0x00;
      }
      return 0;
    case 0x61: return port_b;
    case 0x64: return 0x14;  // Input buffer empty, self-test passed

    // --- Fast A20 gate ---
    case 0x92: return mem->get_a20() ? 0x02 : 0x00;

    // --- CGA status ---
    case 0x3DA: {
      static uint8_t toggle = 0;
      toggle ^= 0x09;  // retrace bits toggle
      return toggle;
    }
    // --- MDA status ---
    case 0x3BA: {
      static uint8_t mtoggle = 0;
      mtoggle ^= 0x09;
      return mtoggle;
    }

    // --- VGA DAC ---
    case 0x3C6: return dac_pel_mask;
    case 0x3C7: return 0x03;  // DAC state: read mode
    case 0x3C9: {
      uint8_t val = vga_dac[dac_read_index][dac_component] & 0x3F;
      dac_component++;
      if (dac_component >= 3) {
        dac_component = 0;
        dac_read_index++;
      }
      return val;
    }

    // --- CRTC ---
    case 0x3D5: case 0x3B5:
      return crtc_regs[crtc_index];

    default:
      // NE2000 NIC (32 ports at ne2000_base)
      if (nic && port >= ne2000_base && port < ne2000_base + 0x20)
        return nic->ioread(port - ne2000_base);
      return 0xFF;
  }
}

//=============================================================================
// 16-bit Port I/O (for NE2000 data port)
//=============================================================================

void dos_machine::port_out16(emu88_uint16 port, emu88_uint16 value) {
  if (nic && port >= ne2000_base && port < ne2000_base + 0x20) {
    nic->iowrite16(port - ne2000_base, value);
    return;
  }
  // Default: two byte writes
  port_out(port, value & 0xFF);
  port_out(port + 1, (value >> 8) & 0xFF);
}

emu88_uint16 dos_machine::port_in16(emu88_uint16 port) {
  if (nic && port >= ne2000_base && port < ne2000_base + 0x20)
    return nic->ioread16(port - ne2000_base);
  // Default: two byte reads
  return port_in(port) | ((uint16_t)port_in(port + 1) << 8);
}

//=============================================================================
// Keyboard input from host
//=============================================================================

void dos_machine::queue_key(uint8_t ascii, uint8_t scancode) {
  uint16_t head = bda_r16(bda::KBD_BUF_HEAD);
  uint16_t tail = bda_r16(bda::KBD_BUF_TAIL);
  uint16_t buf_end = bda_r16(bda::KBD_BUF_END);
  uint16_t buf_start = bda_r16(bda::KBD_BUF_START);

  uint16_t next_tail = tail + 2;
  if (next_tail >= buf_end) next_tail = buf_start;
  if (next_tail == head) return;  // Buffer full

  // Store: low byte = ASCII, high byte = scancode
  mem->store_mem(0x400 + tail,     ascii);
  mem->store_mem(0x400 + tail + 1, scancode);
  bda_w16(bda::KBD_BUF_TAIL, next_tail);

  waiting_for_key = false;
  kbd_poll_count = 0;
  halted = false;  // Wake from HLT so CPU can process the key
}
