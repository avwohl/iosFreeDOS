#include "dos_machine.h"
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <vector>

class dos_io_debug : public dos_io {
public:
  int disk_fds[4] = {-1, -1, -1, -1};
  uint64_t disk_szs[4] = {};

  bool load_disk(int drive, const char *path) {
    int idx = -1;
    if (drive == 0) idx = 0;
    else if (drive == 1) idx = 1;
    else if (drive == 0x80) idx = 2;
    else if (drive == 0x81) idx = 3;
    if (idx < 0) return false;
    disk_fds[idx] = open(path, O_RDWR);
    if (disk_fds[idx] < 0) { perror(path); return false; }
    disk_szs[idx] = lseek(disk_fds[idx], 0, SEEK_END);
    lseek(disk_fds[idx], 0, SEEK_SET);
    return true;
  }

  int drive_idx(int drive) {
    if (drive == 0) return 0;
    if (drive == 1) return 1;
    if (drive == 0x80) return 2;
    if (drive == 0x81) return 3;
    return -1;
  }

  std::vector<uint8_t> last_vram;
  int last_cols = 80, last_rows = 25;

  void console_write(uint8_t ch) override { (void)ch; }
  bool console_has_input() override { return false; }
  int console_read() override { return -1; }
  void video_mode_changed(int mode, int cols, int rows) override {
    (void)mode;
    last_cols = cols;
    last_rows = rows;
  }
  void video_refresh(const uint8_t *vram, int cols, int rows) override {
    last_cols = cols;
    last_rows = rows;
    last_vram.assign(vram, vram + cols * rows * 2);
  }
  void video_set_cursor(int row, int col) override { (void)row; (void)col; }

  bool disk_present(int drive) override {
    int i = drive_idx(drive);
    return i >= 0 && disk_fds[i] >= 0;
  }
  size_t disk_read(int drive, uint64_t offset, uint8_t *buf, size_t count) override {
    int i = drive_idx(drive);
    if (i < 0 || disk_fds[i] < 0) return 0;
    if (lseek(disk_fds[i], offset, SEEK_SET) < 0) return 0;
    ssize_t n = ::read(disk_fds[i], buf, count);
    return n > 0 ? (size_t)n : 0;
  }
  size_t disk_write(int drive, uint64_t offset, const uint8_t* buf, size_t count) override {
    int i = drive_idx(drive);
    if (i < 0 || disk_fds[i] < 0) return 0;
    if (lseek(disk_fds[i], offset, SEEK_SET) < 0) return 0;
    ssize_t n = ::write(disk_fds[i], buf, count);
    return n > 0 ? (size_t)n : 0;
  }
  uint64_t disk_size(int drive) override {
    int i = drive_idx(drive);
    return (i >= 0) ? disk_szs[i] : 0;
  }
  void get_time(int &h, int &m, int &s, int &hs) override { h=12;m=0;s=0;hs=0; }
  void get_date(int &y, int &m, int &d, int &w) override { y=2026;m=3;d=8;w=0; }

  void dump_screen() {
    if (last_vram.empty()) return;
    fprintf(stderr, "=== Screen (%dx%d) ===\n", last_cols, last_rows);
    for (int r = 0; r < last_rows; r++) {
      std::string line;
      for (int c = 0; c < last_cols; c++) {
        uint8_t ch = last_vram[(r * last_cols + c) * 2];
        line += (ch >= 0x20 && ch < 0x7F) ? (char)ch : ' ';
      }
      while (!line.empty() && line.back() == ' ') line.pop_back();
      fprintf(stderr, "%s\n", line.c_str());
    }
    fprintf(stderr, "=== END ===\n");
  }

  void dump_vram_hex(int row_start, int row_end) {
    if (last_vram.empty()) return;
    if (row_end > last_rows) row_end = last_rows;
    for (int r = row_start; r < row_end; r++) {
      fprintf(stderr, "VRAM row %2d: ", r);
      for (int c = 0; c < last_cols && c < 40; c++) {
        uint8_t ch = last_vram[(r * last_cols + c) * 2];
        fprintf(stderr, "%02X ", ch);
      }
      fprintf(stderr, "\n");
    }
  }
};

class dos_machine_debug : public dos_machine {
public:
  bool trace_bios = false;

  dos_machine_debug(emu88_mem *m, dos_io *io) : dos_machine(m, io) {}

  void unimplemented_opcode(emu88_uint8 opcode) override {
    if (opcode != 0xF1) {
      fprintf(stderr, "[UNDEF] opcode=0x%02X at %04X:%04X\n",
              opcode, sregs[seg_CS], ip - 1);
    }
    dos_machine::unimplemented_opcode(opcode);
  }

  void do_interrupt(emu88_uint8 vector) override {
    if (trace_bios) {
      uint8_t ah = get_reg8(reg_AH);
      uint8_t al = get_reg8(reg_AL);
      switch (vector) {
        case 0x10:
          fprintf(stderr, "[INT10] AH=%02X AL=%02X BH=%02X BL=%02X CX=%04X DX=%04X\n",
                  ah, al, get_reg8(reg_BH), get_reg8(reg_BL), regs[reg_CX], regs[reg_DX]);
          break;
        case 0x13:
          fprintf(stderr, "[INT13] AH=%02X DL=%02X CX=%04X\n", ah, get_reg8(reg_DL), regs[reg_CX]);
          break;
        case 0x15:
          fprintf(stderr, "[INT15] AH=%02X AL=%02X\n", ah, al);
          break;
        case 0x16:
          fprintf(stderr, "[INT16] AH=%02X\n", ah);
          break;
        case 0x21:
          fprintf(stderr, "[INT21] AH=%02X AL=%02X\n", ah, al);
          break;
        default:
          if (vector != 0x08 && vector != 0x1C && vector != 0x1A)
            fprintf(stderr, "[INT%02X] AH=%02X\n", vector, ah);
          break;
      }
    }
    dos_machine::do_interrupt(vector);
  }
};

int main(int argc, char *argv[]) {
  const char *img = "fd/freedos.img";
  bool trace = false;
  int boot_drive = 0;
  int max_batches = 5000;
  std::vector<std::pair<int,const char*>> extra_disks;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-t") == 0) { trace = true; continue; }
    if (strcmp(argv[i], "-n") == 0 && i+1 < argc) { max_batches = atoi(argv[++i]); continue; }
    if (strcmp(argv[i], "-c") == 0 && i+1 < argc) { extra_disks.push_back({0x80, argv[++i]}); continue; }
    if (strcmp(argv[i], "-b") == 0 && i+1 < argc) { extra_disks.push_back({1, argv[++i]}); continue; }
    if (strcmp(argv[i], "-hd") == 0) { boot_drive = 0x80; continue; }
    img = argv[i];
  }

  dos_io_debug io;
  if (!io.load_disk(boot_drive < 0x80 ? 0 : 0x80, img)) return 1;
  for (auto &[drv, path] : extra_disks)
    io.load_disk(drv, path);

  emu88_mem mem(0x1000000);
  dos_machine_debug machine(&mem, &io);
  machine.trace_bios = trace;
  machine.set_display(dos_machine::DISPLAY_VGA);

  if (!machine.boot(boot_drive)) return 1;

  // Feed keys for automation - queue one at a time when buffer is empty
  const char *auto_keys = getenv("AUTOKEYS");
  const char *next_key = auto_keys;

  auto map_scancode = [](uint8_t ascii) -> uint8_t {
    if (ascii >= 'a' && ascii <= 'z') {
      static const uint8_t scs[] = {0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C};
      return scs[ascii - 'a'];
    } else if (ascii >= 'A' && ascii <= 'Z') {
      static const uint8_t scs[] = {0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C};
      return scs[ascii - 'A'];
    } else if (ascii >= '0' && ascii <= '9') {
      static const uint8_t scs[] = {0x0B,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A};
      return scs[ascii - '0'];
    } else if (ascii == '\r') return 0x1C;
    else if (ascii == ' ') return 0x39;
    return 0;
  };

  // Track screen text stability to only inject keys when at a prompt (not boot).
  // queue_key wakes the CPU from HLT, so the program processes keys immediately.
  std::vector<uint8_t> prev_vram;
  int stable_count = 0;
  int idle_no_keys = 0;

  for (int batch = 0; batch < max_batches; batch++) {
    if (!machine.run_batch(100000)) break;

    // Compare screen text (skip attribute bytes)
    bool text_changed = false;
    if (prev_vram.size() != io.last_vram.size()) {
      text_changed = true;
    } else {
      for (size_t j = 0; j < io.last_vram.size(); j += 2) {
        if (io.last_vram[j] != prev_vram[j]) { text_changed = true; break; }
      }
    }
    if (text_changed) {
      if (trace && !prev_vram.empty()) {
        for (size_t j = 0; j < io.last_vram.size() && j < prev_vram.size(); j += 2) {
          if (io.last_vram[j] != prev_vram[j]) {
            int row = (j / 2) / 80, col = (j / 2) % 80;
            fprintf(stderr, "[BATCH %d] Text diff at row=%d col=%d: %02X->%02X (stable was %d)\n",
                    batch, row, col, prev_vram[j], io.last_vram[j], stable_count);
            break;
          }
        }
      }
      prev_vram = io.last_vram;
      stable_count = 0;
    } else {
      stable_count++;
    }

    // Inject key when screen text has been stable for 30+ batches (prompt shown)
    uint16_t head = mem.fetch_mem16(0x41A);
    uint16_t tail = mem.fetch_mem16(0x41C);
    if (trace && batch >= 330 && batch <= 500 && (batch <= 340 || batch % 10 == 0))
      fprintf(stderr, "[BATCH %d] stable=%d head=%04X tail=%04X halted=%d wfk=%d\n",
              batch, stable_count, head, tail, machine.halted, machine.is_waiting_for_key());
    if (trace && (stable_count == 149 || stable_count == 150))
      fprintf(stderr, "[BATCH %d] stable=%d -> checking injection\n", batch, stable_count);
    if (head == tail && stable_count >= 150 && next_key && *next_key) {
      // Inject all pending keys up to next newline (one "input line" at a time)
      while (next_key && *next_key) {
        char ch = *next_key++;
        uint8_t ascii = (ch == '\n') ? '\r' : (uint8_t)ch;
        if (trace)
          fprintf(stderr, "[BATCH %d] Injecting key '%c' (0x%02X)\n",
                  batch, (ascii >= 0x20 && ascii < 0x7F) ? ascii : '.', ascii);
        machine.queue_key(ascii, map_scancode(ascii));
        if (ch == '\n') break;  // Stop after newline to let program process
      }
      stable_count = 0;
      idle_no_keys = 0;
    } else if (head == tail && stable_count >= 100 && !(next_key && *next_key)) {
      idle_no_keys++;
      if (idle_no_keys > 20) break;
    }
  }

  // Show final machine state
  uint16_t final_cs = machine.sregs[dos_machine::seg_CS];
  uint16_t final_ip = machine.ip;
  fprintf(stderr, "Final CS:IP = %04X:%04X\n", final_cs, final_ip);
  uint32_t phys = (uint32_t)final_cs * 16 + final_ip;
  fprintf(stderr, "Code at CS:IP:");
  for (int i = -8; i < 24; i++) {
    if (i == 0) fprintf(stderr, " [");
    fprintf(stderr, " %02X", mem.fetch_mem(phys + i));
    if (i == 0) fprintf(stderr, "]");
  }
  fprintf(stderr, "\n");

  fprintf(stderr, "AX=%04X BX=%04X CX=%04X DX=%04X\n",
          machine.regs[dos_machine::reg_AX], machine.regs[dos_machine::reg_BX],
          machine.regs[dos_machine::reg_CX], machine.regs[dos_machine::reg_DX]);
  fprintf(stderr, "SI=%04X DI=%04X SP=%04X BP=%04X\n",
          machine.regs[dos_machine::reg_SI], machine.regs[dos_machine::reg_DI],
          machine.regs[dos_machine::reg_SP], machine.regs[dos_machine::reg_BP]);
  fprintf(stderr, "DS=%04X ES=%04X SS=%04X FLAGS=%04X\n",
          machine.sregs[dos_machine::seg_DS], machine.sregs[dos_machine::seg_ES],
          machine.sregs[dos_machine::seg_SS], machine.flags);
  fprintf(stderr, "IVT INT16h: %04X:%04X\n",
          mem.fetch_mem16(0x16 * 4 + 2), mem.fetch_mem16(0x16 * 4));
  fprintf(stderr, "BDA cursor: %04X\n", mem.fetch_mem16(0x450));
  io.dump_screen();
  return 0;
}
