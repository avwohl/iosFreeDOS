#ifndef DOS_MACHINE_H
#define DOS_MACHINE_H

#include "emu88.h"
#include "dos_io.h"

// PC memory map
static constexpr uint32_t MDA_VRAM_BASE  = 0xB0000;
static constexpr uint32_t CGA_VRAM_BASE  = 0xB8000;
static constexpr uint32_t BIOS_ROM_BASE  = 0xF0000;
static constexpr uint32_t BOOT_LOAD_ADDR = 0x07C00;
static constexpr uint16_t BDA_SEG        = 0x0040;

// BIOS trap opcode (0xF1 is undefined on all x86, falls to unimplemented_opcode)
static constexpr uint8_t BIOS_TRAP_OPCODE = 0xF1;

// BIOS Data Area offsets (from 0040:0000)
namespace bda {
  constexpr int EQUIPMENT     = 0x10;
  constexpr int MEM_SIZE_KB   = 0x13;
  constexpr int KBD_FLAGS1    = 0x17;
  constexpr int KBD_FLAGS2    = 0x18;
  constexpr int KBD_BUF_HEAD  = 0x1A;
  constexpr int KBD_BUF_TAIL  = 0x1C;
  constexpr int KBD_BUFFER    = 0x1E;
  constexpr int VIDEO_MODE    = 0x49;
  constexpr int SCREEN_COLS   = 0x4A;
  constexpr int VIDEO_PAGE_SZ = 0x4C;
  constexpr int VIDEO_PAGE_OFF= 0x4E;
  constexpr int CURSOR_POS    = 0x50;  // 8 pages x 2 bytes
  constexpr int CURSOR_SHAPE  = 0x60;
  constexpr int ACTIVE_PAGE   = 0x62;
  constexpr int CRTC_BASE     = 0x63;
  constexpr int TIMER_COUNT   = 0x6C;
  constexpr int TIMER_ROLLOVER= 0x70;
  constexpr int NUM_HDD       = 0x75;
  constexpr int KBD_BUF_START = 0x80;
  constexpr int KBD_BUF_END   = 0x82;
  constexpr int SCREEN_ROWS   = 0x84;
}

class dos_machine : public emu88 {
public:
  // Speed modes
  enum SpeedMode {
    SPEED_FULL = 0,    // No throttling
    SPEED_PC_4_77 = 1, // IBM PC 4.77 MHz (4,770,000 cycles/sec)
    SPEED_AT_8 = 2,    // IBM AT 8 MHz (8,000,000 cycles/sec)
    SPEED_TURBO = 3    // 386/25 MHz (25,000,000 cycles/sec)
  };

  // Display adapter
  enum DisplayAdapter {
    DISPLAY_CGA = 0,
    DISPLAY_MDA = 1,
    DISPLAY_HERCULES = 2,
    DISPLAY_CGA_MDA = 3   // Dual: both adapters active
  };

  // Machine configuration
  struct Config {
    DisplayAdapter display = DISPLAY_CGA;
    bool mouse_enabled = true;
    bool speaker_enabled = true;
    // Sound card type: 0=none, 1=Adlib, 2=SoundBlaster (future)
    int sound_card = 0;
    bool cdrom_enabled = true;
  };

  dos_machine(emu88_mem *memory, dos_io *io);

  // Machine lifecycle
  void configure(const Config &cfg);
  void init_machine();
  bool boot(int drive = 0);
  bool run_batch(int count = 10000);

  // Config access
  const Config &get_config() const { return config; }

  // Speed control
  void set_speed(SpeedMode mode);
  SpeedMode get_speed() const { return speed_mode; }

  // CPU overrides
  void do_interrupt(emu88_uint8 vector) override;
  void port_out(emu88_uint16 port, emu88_uint8 value) override;
  emu88_uint8 port_in(emu88_uint16 port) override;
  void unimplemented_opcode(emu88_uint8 opcode) override;

  // Keyboard input from host
  void queue_key(uint8_t ascii, uint8_t scancode);

  // Check if waiting for keyboard input (for host yield)
  bool is_waiting_for_key() const { return waiting_for_key; }

private:
  dos_io *io;

  // Video state
  int video_mode;
  int screen_cols;
  int screen_rows;

  // PIC state
  uint8_t pic_imr;
  uint8_t pic_vector_base;
  int pic_init_step;
  bool pic_icw4_needed;

  // PIT state
  uint16_t pit_counter[3];
  uint8_t pit_mode[3];
  uint8_t pit_access[3];
  uint8_t pit_write_phase[3];
  uint8_t pit_read_phase[3];
  bool pit_latch_pending[3];
  uint16_t pit_latch_value[3];
  uint8_t port_b;  // Port 0x61

  // CGA/MDA CRTC state
  uint8_t crtc_index;
  uint8_t crtc_regs[256];

  // Timer / refresh counters (cycle-based)
  unsigned long long tick_cycle_mark;
  unsigned long long refresh_cycle_mark;

  // 8088 @ 4.77MHz: timer tick fires at 18.2 Hz = every ~262,187 cycles
  // Video refresh at ~30 Hz = every ~159,000 cycles
  static constexpr uint32_t CYCLES_PER_TICK    = 262187;
  static constexpr uint32_t CYCLES_PER_REFRESH = 159000;

  // Speed control
  SpeedMode speed_mode;
  uint32_t target_cps;  // Target cycles per second (0 = unlimited)

  // Keyboard wait state
  bool waiting_for_key;
  int kbd_poll_count;  // Consecutive AH=01 no-key responses

  // How many consecutive AH=01 "no key" polls before yielding.
  // Must be high enough to avoid triggering during DOS Ctrl-C checks
  // during file I/O (~5-20 per batch), but low enough to catch tight
  // polling loops at prompts (~5000+ per batch).
  static constexpr int KBD_POLL_THRESHOLD = 500;

  // Keyboard controller command state (for A20 gate)
  uint8_t kbd_cmd_pending;

  // Machine config
  Config config;

  // Mouse state (INT 33h driver)
  struct {
    int x = 320, y = 100;      // Virtual coords (0-639, 0-199)
    int buttons = 0;            // Bit 0=left, 1=right, 2=middle
    bool visible = false;
    bool installed = false;
    int min_x = 0, max_x = 639;
    int min_y = 0, max_y = 199;
    int sensitivity_x = 8, sensitivity_y = 16;
    // Button press/release counters
    int press_count[3] = {};
    int release_count[3] = {};
    int press_x[3] = {}, press_y[3] = {};
    int release_x[3] = {}, release_y[3] = {};
    // User callback
    uint16_t handler_mask = 0;
    uint16_t handler_seg = 0;
    uint16_t handler_off = 0;
  } mouse;

  // BIOS ROM entry points (offset within F000 segment)
  uint16_t bios_entry[256];

  // BDA helpers
  void bda_w8(int off, uint8_t v);
  void bda_w16(int off, uint16_t v);
  void bda_w32(int off, uint32_t v);
  uint8_t bda_r8(int off);
  uint16_t bda_r16(int off);
  uint32_t bda_r32(int off);

  // BIOS interrupt handlers (in dos_bios.cc)
  void bios_int08h();   // Timer tick
  void bios_int10h();   // Video services
  void bios_int11h();   // Equipment list
  void bios_int12h();   // Memory size
  void bios_int13h();   // Disk services
  void bios_int14h();   // Serial port
  void bios_int15h();   // System services
  void bios_int16h();   // Keyboard
  void bios_int17h();   // Printer
  void bios_int19h();   // Bootstrap loader
  void bios_int1ah();   // Time/date
  void bios_int33h();   // Mouse driver

  // BIOS trap dispatch
  void dispatch_bios(uint8_t vector);

  // Video helpers (in dos_bios.cc)
  void video_set_mode(int mode);
  void video_tty(uint8_t ch);
  void video_write_char(uint8_t ch, uint8_t attr, int count);
  void video_scroll(int dir, int top, int left, int bottom, int right,
                    int lines, uint8_t attr);
  uint32_t vram_base() const;
  void cursor_advance();

  // Disk helpers (in dos_bios.cc)
  struct disk_geom { int heads, cyls, spt, sector_size; };
  disk_geom get_geometry(int drive);

  // Init helpers
  void init_ivt();
  void init_bda();
  void install_bios_stubs();
};

#endif // DOS_MACHINE_H
