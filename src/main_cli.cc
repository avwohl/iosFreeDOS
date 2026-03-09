#include "dos_machine.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <cstdlib>
#include <strings.h>
#include <mach/mach_time.h>

//=============================================================================
// CLI I/O implementation
//=============================================================================

class dos_io_cli : public dos_io {
public:
  static constexpr int MAX_DRIVES = 5;  // A, B, C, D, CD-ROM

  dos_io_cli() {
    for (int i = 0; i < MAX_DRIVES; i++) {
      disk_fd[i] = -1;
      disk_sz[i] = 0;
      disk_off[i] = 0;
      disk_secsize[i] = 512;
    }
  }

  ~dos_io_cli() {
    for (int i = 0; i < MAX_DRIVES; i++)
      if (disk_fd[i] >= 0) close(disk_fd[i]);
  }

  // Load a disk image. drive: 0=A, 1=B, 0x80=C, 0x81=D
  bool load_disk(int drive, const char *path) {
    int idx = drive_index(drive);
    if (idx < 0) return false;

    int fd = open(path, O_RDWR);
    if (fd < 0) {
      fd = open(path, O_RDONLY);
      if (fd < 0) {
        perror(path);
        return false;
      }
    }

    disk_fd[idx] = fd;
    disk_sz[idx] = lseek(fd, 0, SEEK_END);
    disk_off[idx] = 0;
    lseek(fd, 0, SEEK_SET);
    return true;
  }

  // Create a new blank disk image and mount it.
  // For floppies (drive 0,1): size_val is in KB
  // For hard disks (drive 0x80+): size_val is in MB
  bool create_disk(int drive, const char *path, uint64_t size_val) {
    int idx = drive_index(drive);
    if (idx < 0) return false;

    uint64_t size_bytes;
    if (drive < 2) {
      size_bytes = size_val * 1024;
    } else {
      size_bytes = size_val * 1024 * 1024;
    }

    // Don't overwrite existing files
    if (access(path, F_OK) == 0) {
      fprintf(stderr, "%s: file already exists (use lowercase flag to mount)\n", path);
      return false;
    }

    int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
      perror(path);
      return false;
    }

    // Extend file to desired size (sparse)
    if (ftruncate(fd, size_bytes) < 0) {
      perror("ftruncate");
      close(fd);
      unlink(path);
      return false;
    }

    disk_fd[idx] = fd;
    disk_sz[idx] = size_bytes;
    disk_off[idx] = 0;

    const char *drive_str;
    if (drive == 0) drive_str = "A:";
    else if (drive == 1) drive_str = "B:";
    else drive_str = "C:";

    if (drive < 2)
      fprintf(stderr, "Created %lluKB floppy image %s -> %s\n",
              (unsigned long long)size_val, path, drive_str);
    else
      fprintf(stderr, "Created %lluMB hard disk image %s -> %s\n",
              (unsigned long long)size_val, path, drive_str);

    return true;
  }

  // Load a boot image from an ISO file via El Torito.
  // Returns the drive number to boot from, or -1 on failure.
  int load_iso(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror(path); return -1; }

    uint8_t buf[2048];

    // Read the Boot Record Volume Descriptor at sector 17
    if (pread(fd, buf, 2048, 17 * 2048) != 2048) {
      fprintf(stderr, "%s: cannot read boot record volume descriptor\n", path);
      close(fd); return -1;
    }

    // Verify: byte 0 = 0 (boot record), bytes 1-5 = "CD001"
    if (buf[0] != 0 || memcmp(buf + 1, "CD001", 5) != 0) {
      fprintf(stderr, "%s: no El Torito boot record found\n", path);
      close(fd); return -1;
    }

    // Boot catalog sector number at offset 0x47 (little-endian 32-bit)
    uint32_t cat_sector = buf[0x47] | (buf[0x48] << 8) |
                          (buf[0x49] << 16) | (buf[0x4A] << 24);

    // Read boot catalog
    if (pread(fd, buf, 2048, (uint64_t)cat_sector * 2048) != 2048) {
      fprintf(stderr, "%s: cannot read boot catalog at sector %u\n",
              path, cat_sector);
      close(fd); return -1;
    }

    // Validation entry: byte 0 = 1, bytes 30-31 = 0x55 0xAA
    if (buf[0] != 1 || buf[30] != 0x55 || buf[31] != 0xAA) {
      fprintf(stderr, "%s: invalid boot catalog validation entry\n", path);
      close(fd); return -1;
    }

    // Default boot entry starts at offset 32
    uint8_t *entry = buf + 32;
    if (entry[0] != 0x88) {
      fprintf(stderr, "%s: boot entry not bootable (0x%02X)\n",
              path, entry[0]);
      close(fd); return -1;
    }

    uint8_t media_type = entry[1];
    uint16_t load_seg = entry[2] | (entry[3] << 8);
    uint16_t sector_count = entry[6] | (entry[7] << 8);
    uint32_t load_rba = entry[8] | (entry[9] << 8) |
                        (entry[10] << 16) | (entry[11] << 24);

    (void)load_seg;

    uint64_t img_offset = (uint64_t)load_rba * 2048;
    uint64_t img_size;
    int drive;

    int sec_size = 512;

    switch (media_type) {
    case 1: img_size = 1228800;  drive = 0; break;  // 1.2MB floppy
    case 2: img_size = 1474560;  drive = 0; break;  // 1.44MB floppy
    case 3: img_size = 2949120;  drive = 0; break;  // 2.88MB floppy
    case 4: // Hard disk emulation
      img_size = (uint64_t)sector_count * 512;
      if (img_size == 0) img_size = 1474560;
      drive = 0x80;
      break;
    case 0: // No emulation - boot from CD-ROM with INT 13h extensions
      // Store boot image info for loading, but present full ISO as CD-ROM
      iso_boot_lba = load_rba;
      iso_boot_count = sector_count;
      drive = 0xE0;
      img_offset = 0;  // Full ISO, not just boot image
      img_size = lseek(fd, 0, SEEK_END);
      sec_size = 2048;
      break;
    default:
      fprintf(stderr, "%s: unsupported boot media type %d\n",
              path, media_type);
      close(fd); return -1;
    }

    int idx = drive_index(drive);
    if (idx < 0) { close(fd); return -1; }

    // Check if this drive slot is already in use
    if (disk_fd[idx] >= 0) {
      if (drive == 0 && disk_fd[1] < 0) {
        drive = 1; idx = 1;
      } else {
        fprintf(stderr, "%s: drive slot already in use\n", path);
        close(fd); return -1;
      }
    }

    disk_fd[idx] = fd;
    disk_sz[idx] = img_size;
    disk_off[idx] = img_offset;
    disk_secsize[idx] = sec_size;

    const char *type_str;
    const char *drive_str;
    switch (media_type) {
    case 0: type_str = "no-emulation"; break;
    case 1: type_str = "1.2MB floppy"; break;
    case 2: type_str = "1.44MB floppy"; break;
    case 3: type_str = "2.88MB floppy"; break;
    case 4: type_str = "hard disk"; break;
    default: type_str = "unknown"; break;
    }
    if (drive < 2) drive_str = (drive == 0 ? "A:" : "B:");
    else if (drive < 0xE0) drive_str = "C:";
    else drive_str = "CD-ROM";

    fprintf(stderr, "ISO: %s boot image at sector %u -> %s\n",
            type_str, load_rba, drive_str);

    return drive;
  }

  // For no-emulation boot: load boot sectors to memory
  bool load_boot_image(emu88_mem *memory) {
    if (iso_boot_lba == 0) return false;
    int idx = drive_index(0xE0);
    if (idx < 0 || disk_fd[idx] < 0) return false;

    // Load boot sectors (each is 512 bytes as specified by El Torito)
    uint64_t offset = (uint64_t)iso_boot_lba * 2048;
    int bytes = iso_boot_count * 512;
    if (bytes > 32768) bytes = 32768;  // Cap at 32KB

    uint8_t buf[32768];
    ssize_t n = pread(disk_fd[idx], buf, bytes, offset);
    if (n <= 0) return false;

    for (int i = 0; i < n; i++)
      memory->store_mem(0x7C00 + i, buf[i]);

    return true;
  }

  uint32_t iso_boot_lba = 0;
  uint16_t iso_boot_count = 0;

  // --- Console ---
  void console_write(uint8_t ch) override {
    putchar(ch);
    fflush(stdout);
  }

  bool console_has_input() override {
    struct timeval tv = {0, 0};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0;
  }

  int console_read() override {
    if (!console_has_input()) return -1;
    unsigned char ch;
    if (read(STDIN_FILENO, &ch, 1) == 1) return ch;
    return -1;
  }

  // --- Video ---
  void video_mode_changed(int /*mode*/, int cols, int rows) override {
    printf("\033[2J\033[H");  // Clear screen
    fflush(stdout);
    cur_cols = cols;
    cur_rows = rows;
  }

  void video_refresh(const uint8_t *vram, int cols, int rows) override {
    // Move cursor home and redraw entire screen
    printf("\033[H");
    for (int r = 0; r < rows; r++) {
      for (int c = 0; c < cols; c++) {
        int off = (r * cols + c) * 2;
        uint8_t ch = vram[off];
        uint8_t attr = vram[off + 1];
        print_with_attr(ch, attr);
      }
      if (r < rows - 1) printf("\033[0m\n");
    }
    printf("\033[0m");
    fflush(stdout);
  }

  void video_refresh_gfx(const uint8_t *framebuf, int width, int height,
                          const uint8_t palette[][3]) override {
    // Render VGA mode 13h using ANSI true color and half-block characters
    // Each terminal row represents 2 pixel rows using '▀' (upper half block)
    printf("\033[H");
    for (int y = 0; y < height; y += 2) {
      for (int x = 0; x < width; x++) {
        uint8_t top_idx = framebuf[y * width + x];
        uint8_t bot_idx = (y + 1 < height) ? framebuf[(y + 1) * width + x] : 0;
        // Scale VGA DAC (0-63) to RGB (0-255)
        int tr = palette[top_idx][0] * 255 / 63;
        int tg = palette[top_idx][1] * 255 / 63;
        int tb = palette[top_idx][2] * 255 / 63;
        int br = palette[bot_idx][0] * 255 / 63;
        int bg = palette[bot_idx][1] * 255 / 63;
        int bb = palette[bot_idx][2] * 255 / 63;
        printf("\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm\xe2\x96\x80",
               tr, tg, tb, br, bg, bb);
      }
      printf("\033[0m\n");
    }
    fflush(stdout);
  }

  void video_set_cursor(int row, int col) override {
    printf("\033[%d;%dH", row + 1, col + 1);
    fflush(stdout);
  }

  // --- Disk ---
  bool disk_present(int drive) override {
    int idx = drive_index(drive);
    return idx >= 0 && disk_fd[idx] >= 0;
  }

  size_t disk_read(int drive, uint64_t offset, uint8_t *buf, size_t count) override {
    int idx = drive_index(drive);
    if (idx < 0 || disk_fd[idx] < 0) return 0;
    if (lseek(disk_fd[idx], offset + disk_off[idx], SEEK_SET) < 0) return 0;
    ssize_t n = ::read(disk_fd[idx], buf, count);
    return n > 0 ? (size_t)n : 0;
  }

  size_t disk_write(int drive, uint64_t offset, const uint8_t *buf, size_t count) override {
    int idx = drive_index(drive);
    if (idx < 0 || disk_fd[idx] < 0) return 0;
    if (lseek(disk_fd[idx], offset + disk_off[idx], SEEK_SET) < 0) return 0;
    ssize_t n = ::write(disk_fd[idx], buf, count);
    return n > 0 ? (size_t)n : 0;
  }

  uint64_t disk_size(int drive) override {
    int idx = drive_index(drive);
    if (idx < 0) return 0;
    return disk_sz[idx];
  }

  int disk_sector_size(int drive) override {
    int idx = drive_index(drive);
    if (idx < 0) return 512;
    return disk_secsize[idx];
  }

  // --- Host file transfer ---
  bool host_file_open_read(const char *path) override {
    if (host_read_fp) fclose(host_read_fp);
    host_read_fp = fopen(path, "rb");
    return host_read_fp != nullptr;
  }

  bool host_file_open_write(const char *path) override {
    if (host_write_fp) fclose(host_write_fp);
    host_write_fp = fopen(path, "wb");
    return host_write_fp != nullptr;
  }

  int host_file_read_byte() override {
    if (!host_read_fp) return -1;
    int ch = fgetc(host_read_fp);
    return (ch == EOF) ? -1 : ch;
  }

  bool host_file_write_byte(uint8_t byte) override {
    if (!host_write_fp) return false;
    return fputc(byte, host_write_fp) != EOF;
  }

  void host_file_close_read() override {
    if (host_read_fp) { fclose(host_read_fp); host_read_fp = nullptr; }
  }

  void host_file_close_write() override {
    if (host_write_fp) { fclose(host_write_fp); host_write_fp = nullptr; }
  }

  // --- Time ---
  void get_time(int &hour, int &min, int &sec, int &hundredths) override {
    time_t t = time(nullptr);
    struct tm *tm = localtime(&t);
    hour = tm->tm_hour;
    min = tm->tm_min;
    sec = tm->tm_sec;
    hundredths = 0;
  }

  void get_date(int &year, int &month, int &day, int &weekday) override {
    time_t t = time(nullptr);
    struct tm *tm = localtime(&t);
    year = tm->tm_year + 1900;
    month = tm->tm_mon + 1;
    day = tm->tm_mday;
    weekday = tm->tm_wday;
  }

private:
  int disk_fd[MAX_DRIVES];
  uint64_t disk_sz[MAX_DRIVES];
  uint64_t disk_off[MAX_DRIVES];   // byte offset within file (for ISO boot images)
  int disk_secsize[MAX_DRIVES];    // sector size (512 for floppy/HDD, 2048 for CD)
  int cur_cols = 80, cur_rows = 25;
  FILE *host_read_fp = nullptr;
  FILE *host_write_fp = nullptr;

  int drive_index(int drive) {
    if (drive >= 0 && drive < 2) return drive;       // A, B
    if (drive >= 0x80 && drive < 0x82) return drive - 0x80 + 2;  // C, D
    if (drive == 0xE0) return 4;  // CD-ROM
    return -1;
  }

  // CGA color -> ANSI color index
  static int cga_to_ansi(int cga) {
    static const int map[] = {0,4,2,6,1,5,3,7};
    return map[cga & 7];
  }

  void print_with_attr(uint8_t ch, uint8_t attr) {
    int fg = attr & 0x0F;
    int bg = (attr >> 4) & 0x07;

    int ansi_fg = cga_to_ansi(fg & 7);
    int ansi_bg = cga_to_ansi(bg);
    bool bright = (fg & 8) != 0;

    printf("\033[%d;%d;%dm%c",
           bright ? 1 : 0,
           30 + ansi_fg,
           40 + ansi_bg,
           (ch >= 0x20 && ch < 0x7F) ? ch : ' ');
  }
};

//=============================================================================
// ASCII -> scan code mapping
//=============================================================================

static uint8_t ascii_to_scancode(uint8_t ascii) {
  if (ascii >= 'a' && ascii <= 'z') {
    static const uint8_t sc[] = {
      0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,
      0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1F,0x14,
      0x16,0x2F,0x11,0x2D,0x15,0x2C
    };
    return sc[ascii - 'a'];
  }
  if (ascii >= 'A' && ascii <= 'Z')
    return ascii_to_scancode(ascii - 'A' + 'a');
  if (ascii >= '1' && ascii <= '9') return 0x02 + (ascii - '1');
  if (ascii == '0') return 0x0B;

  switch (ascii) {
    case '\r': case '\n': return 0x1C;
    case '\t': return 0x0F;
    case 0x1B: return 0x01;  // Escape
    case ' ':  return 0x39;
    case '-':  return 0x0C;
    case '=':  return 0x0D;
    case '[':  return 0x1A;
    case ']':  return 0x1B;
    case '\\': return 0x2B;
    case ';':  return 0x27;
    case '\'': return 0x28;
    case '`':  return 0x29;
    case ',':  return 0x33;
    case '.':  return 0x34;
    case '/':  return 0x35;
    case 0x08: return 0x0E;  // Backspace
    default:   return 0x00;
  }
}

//=============================================================================
// Terminal raw mode
//=============================================================================

static struct termios orig_termios;
static bool raw_mode = false;

static void disable_raw_mode() {
  if (raw_mode) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    raw_mode = false;
    printf("\033[0m\033[?25h\n");  // Reset attrs, show cursor
  }
}

static void enable_raw_mode() {
  tcgetattr(STDIN_FILENO, &orig_termios);
  atexit(disable_raw_mode);

  struct termios raw = orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  raw_mode = true;
}

//=============================================================================
// Main
//=============================================================================

// Check if a file looks like an ISO 9660 image
static bool is_iso_file(const char *path) {
  // Check by extension
  const char *dot = strrchr(path, '.');
  if (dot && (strcasecmp(dot, ".iso") == 0))
    return true;

  // Check for ISO 9660 signature at sector 16
  int fd = open(path, O_RDONLY);
  if (fd < 0) return false;
  uint8_t buf[6];
  bool is_iso = (pread(fd, buf, 5, 16 * 2048 + 1) == 5 &&
                 memcmp(buf, "CD001", 5) == 0);
  close(fd);
  return is_iso;
}

static void usage(const char *prog) {
  fprintf(stderr, "Usage: %s [-a disk.img] [-c disk.img] [disk.img | file.iso]\n", prog);
  fprintf(stderr, "\n  Mount existing images:\n");
  fprintf(stderr, "  -a FILE       Floppy A: disk image\n");
  fprintf(stderr, "  -b FILE       Floppy B: disk image\n");
  fprintf(stderr, "  -c FILE       Hard disk C: image\n");
  fprintf(stderr, "  -d FILE       CD-ROM: ISO image (El Torito boot)\n");
  fprintf(stderr, "  FILE          Auto-detect: .iso as CD-ROM, otherwise floppy A:\n");
  fprintf(stderr, "\n  Create new blank images:\n");
  fprintf(stderr, "  -A FILE SIZE  Create blank floppy A: (SIZE in KB: 360,720,1200,1440,2880)\n");
  fprintf(stderr, "  -B FILE SIZE  Create blank floppy B: (SIZE in KB)\n");
  fprintf(stderr, "  -C FILE SIZE  Create blank hard disk C: (SIZE in MB)\n");
  fprintf(stderr, "\n  Boot control:\n");
  fprintf(stderr, "  -boot DRIVE   Boot from: a, c, or cd (default: auto-detect)\n");
  fprintf(stderr, "\n  Speed control:\n");
  fprintf(stderr, "  -s MODE       Speed: full, pc, at, 386sx, 386dx, 486dx2\n");
  fprintf(stderr, "\n  Hardware:\n");
  fprintf(stderr, "  -net          Enable NE2000 NIC (IRQ3, base 0x300)\n");
}

int main(int argc, char *argv[]) {
  dos_io_cli io;

  const char *floppy_a = nullptr;
  const char *floppy_b = nullptr;
  const char *hdd_c = nullptr;
  const char *cdrom = nullptr;
  int iso_boot_drive = -1;
  int force_boot_drive = -1;  // -1 = auto-detect
  dos_machine::SpeedMode speed = dos_machine::SPEED_FULL;
  bool enable_net = false;

  // New disk creation specs (file, size)
  struct new_disk { const char *path; uint64_t size; };
  new_disk new_a = {nullptr, 0};
  new_disk new_b = {nullptr, 0};
  new_disk new_c = {nullptr, 0};

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-a") == 0 && i + 1 < argc) {
      floppy_a = argv[++i];
    } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
      floppy_b = argv[++i];
    } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
      hdd_c = argv[++i];
    } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
      cdrom = argv[++i];
    } else if (strcmp(argv[i], "-A") == 0 && i + 2 < argc) {
      new_a.path = argv[++i];
      new_a.size = strtoull(argv[++i], nullptr, 10);
    } else if (strcmp(argv[i], "-B") == 0 && i + 2 < argc) {
      new_b.path = argv[++i];
      new_b.size = strtoull(argv[++i], nullptr, 10);
    } else if (strcmp(argv[i], "-C") == 0 && i + 2 < argc) {
      new_c.path = argv[++i];
      new_c.size = strtoull(argv[++i], nullptr, 10);
    } else if (strcmp(argv[i], "-boot") == 0 && i + 1 < argc) {
      const char *drv = argv[++i];
      if (strcasecmp(drv, "a") == 0) force_boot_drive = 0;
      else if (strcasecmp(drv, "c") == 0) force_boot_drive = 0x80;
      else if (strcasecmp(drv, "cd") == 0) force_boot_drive = 0xE0;
      else { fprintf(stderr, "Unknown boot drive: %s (use a, c, or cd)\n", drv); return 1; }
    } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
      const char *mode = argv[++i];
      if (strcasecmp(mode, "full") == 0) speed = dos_machine::SPEED_FULL;
      else if (strcasecmp(mode, "pc") == 0) speed = dos_machine::SPEED_PC_4_77;
      else if (strcasecmp(mode, "at") == 0) speed = dos_machine::SPEED_AT_8;
      else if (strcasecmp(mode, "386sx") == 0) speed = dos_machine::SPEED_386SX_16;
      else if (strcasecmp(mode, "386dx") == 0) speed = dos_machine::SPEED_386DX_33;
      else if (strcasecmp(mode, "486dx2") == 0) speed = dos_machine::SPEED_486DX2_66;
      else { fprintf(stderr, "Unknown speed: %s\n", mode); return 1; }
    } else if (strcmp(argv[i], "-net") == 0) {
      enable_net = true;
    } else if (argv[i][0] != '-') {
      // Auto-detect: ISO files go to CD-ROM, others to floppy A
      if (is_iso_file(argv[i])) {
        if (!cdrom) cdrom = argv[i];
      } else {
        if (!floppy_a) floppy_a = argv[i];
        else if (!hdd_c) hdd_c = argv[i];
      }
    } else {
      usage(argv[0]);
      return 1;
    }
  }

  if (!floppy_a && !hdd_c && !cdrom && !new_a.path && !new_c.path) {
    usage(argv[0]);
    return 1;
  }

  if (floppy_a && !io.load_disk(0, floppy_a)) return 1;
  if (floppy_b && !io.load_disk(1, floppy_b)) return 1;
  if (hdd_c && !io.load_disk(0x80, hdd_c)) return 1;
  if (new_a.path && !io.create_disk(0, new_a.path, new_a.size)) return 1;
  if (new_b.path && !io.create_disk(1, new_b.path, new_b.size)) return 1;
  if (new_c.path && !io.create_disk(0x80, new_c.path, new_c.size)) return 1;
  if (cdrom) {
    iso_boot_drive = io.load_iso(cdrom);
    if (iso_boot_drive < 0) return 1;
  }

  emu88_mem mem(0x1000000);  // 16MB: 1MB conventional + 15MB extended
  dos_machine machine(&mem, &io);
  machine.set_speed(speed);

  if (enable_net) {
    dos_machine::Config cfg = machine.get_config();
    cfg.ne2000_enabled = true;
    machine.configure(cfg);
  }

  int boot_drive;
  if (force_boot_drive >= 0) {
    boot_drive = force_boot_drive;
    // For CD boot with -boot cd, use El Torito if ISO was loaded
    if (boot_drive == 0xE0 && iso_boot_drive < 0) {
      fprintf(stderr, "No CD-ROM ISO loaded for CD boot\n");
      return 1;
    }
    if (boot_drive == 0xE0) boot_drive = iso_boot_drive;
  } else if (iso_boot_drive >= 0) {
    boot_drive = iso_boot_drive;
  } else if (floppy_a || new_a.path) {
    boot_drive = 0;
  } else {
    boot_drive = 0x80;
  }

  if (boot_drive == 0xE0) {
    // No-emulation CD-ROM boot: load boot image directly, skip normal boot
    machine.init_machine();
    if (!io.load_boot_image(machine.mem)) {
      fprintf(stderr, "Failed to load CD-ROM boot image\n");
      return 1;
    }
    // Set up registers for boot: DL = boot drive
    machine.set_reg8(emu88::reg_DL, 0xE0);
    machine.ip = 0x7C00;
    machine.sregs[emu88::seg_CS] = 0;
  } else {
    if (!machine.boot(boot_drive)) return 1;
  }

  enable_raw_mode();

  // Throttling state
  mach_timebase_info_data_t timebase;
  mach_timebase_info(&timebase);
  uint64_t wall_start = mach_absolute_time();
  unsigned long long cycle_start = machine.cycles;

  while (true) {
    // Pump keyboard input
    int ch;
    while ((ch = io.console_read()) >= 0) {
      if (ch == 0x03) {  // Ctrl-C = quit
        disable_raw_mode();
        printf("Emulator terminated.\n");
        return 0;
      }
      uint8_t ascii = (uint8_t)ch;
      if (ascii == '\n') ascii = '\r';  // Unix LF -> DOS CR
      uint8_t scan = ascii_to_scancode(ascii);
      machine.queue_key(ascii, scan);
    }

    if (!machine.run_batch(50000)) break;

    // Speed throttling
    uint32_t cps = 0;
    switch (machine.get_speed()) {
      case dos_machine::SPEED_FULL:      cps = 0; break;
      case dos_machine::SPEED_PC_4_77:   cps = 4770000; break;
      case dos_machine::SPEED_AT_8:      cps = 8000000; break;
      case dos_machine::SPEED_386SX_16:  cps = 48000000; break;
      case dos_machine::SPEED_386DX_33:  cps = 100000000; break;
      case dos_machine::SPEED_486DX2_66: cps = 260000000; break;
    }

    if (cps > 0) {
      unsigned long long elapsed_cycles = machine.cycles - cycle_start;
      uint64_t wall_now = mach_absolute_time();
      uint64_t wall_elapsed_ns = (wall_now - wall_start) * timebase.numer / timebase.denom;
      uint64_t target_ns = (uint64_t)elapsed_cycles * 1000000000ULL / cps;

      if (target_ns > wall_elapsed_ns) {
        uint64_t sleep_ns = target_ns - wall_elapsed_ns;
        if (sleep_ns > 100000) {  // Only sleep if > 0.1ms
          usleep((unsigned)(sleep_ns / 1000));
        }
      }
    } else if (machine.is_waiting_for_key()) {
      usleep(1000);  // 1ms yield when idle
    }
  }

  disable_raw_mode();
  return 0;
}
