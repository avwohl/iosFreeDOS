// 8088 SingleStepTests runner — auto-downloads MOO files from GitHub
// https://github.com/SingleStepTests/8088 (v2_binary/)
//
// Returns: number of failures (0 = all pass)

#include "emu88.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <dirent.h>
#include <algorithm>
#include <sys/stat.h>

// ---- MOO binary parser (same as 286 runner) ----

struct SSTRegs {
  uint16_t bitmask;
  uint16_t values[14]; // ax,bx,cx,dx,cs,ss,ds,es,sp,bp,si,di,ip,flags
};

static const char* SST_REG_NAMES[] = {
  "ax","bx","cx","dx","cs","ss","ds","es","sp","bp","si","di","ip","flags"
};

struct SSTRamEntry { uint32_t addr; uint8_t value; };

struct SSTCpuState {
  SSTRegs regs;
  std::vector<SSTRamEntry> ram;
};

struct SSTTestCase {
  uint32_t idx;
  std::string name;
  std::vector<uint8_t> bytes;
  SSTCpuState initial;
  SSTCpuState final_state;
};

static SSTRegs sst_decode_regs(const uint8_t* data, int len, int& off) {
  SSTRegs r{};
  r.bitmask = *(uint16_t*)(data + off); off += 2;
  for (int i = 0; i < 14; i++) {
    if (r.bitmask & (1 << i)) {
      r.values[i] = *(uint16_t*)(data + off); off += 2;
    }
  }
  return r;
}

static std::vector<SSTRamEntry> sst_decode_ram(const uint8_t* data, int len, int& off) {
  uint32_t count = *(uint32_t*)(data + off); off += 4;
  std::vector<SSTRamEntry> ram(count);
  for (uint32_t i = 0; i < count; i++) {
    ram[i].addr = *(uint32_t*)(data + off); off += 4;
    ram[i].value = data[off]; off += 1;
  }
  return ram;
}

static SSTCpuState sst_decode_cpu_state(const uint8_t* data, int length, int off) {
  SSTCpuState s{};
  int end = off + length;
  while (off < end) {
    char tag[5] = {}; memcpy(tag, data + off, 4); off += 4;
    uint32_t sublen = *(uint32_t*)(data + off); off += 4;
    if (strcmp(tag, "REGS") == 0) {
      int roff = off;
      s.regs = sst_decode_regs(data, sublen, roff);
    } else if (strcmp(tag, "RAM ") == 0) {
      int roff = off;
      s.ram = sst_decode_ram(data, sublen, roff);
    }
    off += sublen;
  }
  return s;
}

static std::vector<SSTTestCase> sst_parse_moo(const uint8_t* data, size_t size) {
  std::vector<SSTTestCase> tests;
  if (size < 8 || memcmp(data, "MOO ", 4) != 0) return tests;
  int off = 4;
  uint32_t hlen = *(uint32_t*)(data + off); off += 4;
  off += hlen;

  while ((size_t)off + 8 <= size) {
    char tag[5] = {}; memcpy(tag, data + off, 4); off += 4;
    uint32_t length = *(uint32_t*)(data + off); off += 4;
    if (strcmp(tag, "TEST") == 0) {
      SSTTestCase tc{};
      tc.idx = *(uint32_t*)(data + off);
      int poff = off + 4;
      int tend = off + length;
      while (poff < tend) {
        char stag[5] = {}; memcpy(stag, data + poff, 4); poff += 4;
        uint32_t slen = *(uint32_t*)(data + poff); poff += 4;
        if (strcmp(stag, "NAME") == 0) {
          uint32_t nl = *(uint32_t*)(data + poff);
          tc.name = std::string((char*)(data + poff + 4), nl);
        } else if (strcmp(stag, "BYTS") == 0) {
          uint32_t cnt = *(uint32_t*)(data + poff);
          tc.bytes.assign(data + poff + 4, data + poff + 4 + cnt);
        } else if (strcmp(stag, "INIT") == 0) {
          tc.initial = sst_decode_cpu_state(data, slen, poff);
        } else if (strcmp(stag, "FINA") == 0) {
          tc.final_state = sst_decode_cpu_state(data, slen, poff);
        }
        poff += slen;
      }
      tests.push_back(std::move(tc));
    }
    off += length;
  }
  return tests;
}

// ---- Test CPU subclass ----

class sst_test_cpu : public emu88 {
public:
  sst_test_cpu(emu88_mem *memory) : emu88(memory) {}
  uint8_t exception_vector = 0xFF;
  bool got_exception = false;

  void port_out(emu88_uint16, emu88_uint8) override {}
  emu88_uint8 port_in(emu88_uint16) override { return 0xFF; }
  void port_out16(emu88_uint16, emu88_uint16) override {}
  emu88_uint16 port_in16(emu88_uint16) override { return 0xFFFF; }

  void do_interrupt(emu88_uint8 vector) override {
    got_exception = true;
    exception_vector = vector;
    emu88::do_interrupt(vector);
  }

  void unimplemented_opcode(emu88_uint8 opcode) override {
    got_exception = true;
    exception_vector = 6;
    halted = true;
  }

  void halt_cpu() override {
    halted = true;
  }
};

// ---- Reg helpers ----

static void sst_set_reg(sst_test_cpu& cpu, int idx, uint16_t val) {
  switch(idx) {
    case 0: cpu.regs[emu88::reg_AX] = val; break;
    case 1: cpu.regs[emu88::reg_BX] = val; break;
    case 2: cpu.regs[emu88::reg_CX] = val; break;
    case 3: cpu.regs[emu88::reg_DX] = val; break;
    case 4: cpu.sregs[emu88::seg_CS] = val;
            cpu.seg_cache[emu88::seg_CS].base = (uint32_t)val << 4; break;
    case 5: cpu.sregs[emu88::seg_SS] = val;
            cpu.seg_cache[emu88::seg_SS].base = (uint32_t)val << 4; break;
    case 6: cpu.sregs[emu88::seg_DS] = val;
            cpu.seg_cache[emu88::seg_DS].base = (uint32_t)val << 4; break;
    case 7: cpu.sregs[emu88::seg_ES] = val;
            cpu.seg_cache[emu88::seg_ES].base = (uint32_t)val << 4; break;
    case 8: cpu.regs[emu88::reg_SP] = val; break;
    case 9: cpu.regs[emu88::reg_BP] = val; break;
    case 10: cpu.regs[emu88::reg_SI] = val; break;
    case 11: cpu.regs[emu88::reg_DI] = val; break;
    case 12: cpu.ip = val; break;
    case 13: cpu.flags = val; break;
  }
}

static uint16_t sst_get_reg(sst_test_cpu& cpu, int idx) {
  switch(idx) {
    case 0: return cpu.regs[emu88::reg_AX];
    case 1: return cpu.regs[emu88::reg_BX];
    case 2: return cpu.regs[emu88::reg_CX];
    case 3: return cpu.regs[emu88::reg_DX];
    case 4: return cpu.sregs[emu88::seg_CS];
    case 5: return cpu.sregs[emu88::seg_SS];
    case 6: return cpu.sregs[emu88::seg_DS];
    case 7: return cpu.sregs[emu88::seg_ES];
    case 8: return cpu.regs[emu88::reg_SP];
    case 9: return cpu.regs[emu88::reg_BP];
    case 10: return cpu.regs[emu88::reg_SI];
    case 11: return cpu.regs[emu88::reg_DI];
    case 12: return (uint16_t)cpu.ip;
    case 13: return cpu.flags;
    default: return 0;
  }
}

// ---- File I/O ----

static std::vector<uint8_t> sst_read_file(const std::string& path) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return {};
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> buf(sz);
  fread(buf.data(), 1, sz, f);
  fclose(f);
  return buf;
}

static bool dir_exists(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool file_exists(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// ---- Auto-download ----

static const char* SST_8088_CACHE_DIR = "/tmp/sst_8088_v2";
static const char* SST_8088_REPO = "https://raw.githubusercontent.com/SingleStepTests/8088/main/v2_binary";

static bool download_8088_tests() {
  if (dir_exists(SST_8088_CACHE_DIR)) {
    // Check if we have at least some MOO files
    DIR* d = opendir(SST_8088_CACHE_DIR);
    if (!d) return false;
    int count = 0;
    struct dirent* ent;
    while ((ent = readdir(d))) {
      std::string name = ent->d_name;
      if (name.size() >= 4 && name.substr(name.size()-4) == ".MOO") count++;
    }
    closedir(d);
    if (count >= 200) return true;  // Already downloaded
  }

  fprintf(stderr, "Downloading 8088 SingleStepTests to %s ...\n", SST_8088_CACHE_DIR);

  // Create cache dir
  std::string cmd = std::string("mkdir -p ") + SST_8088_CACHE_DIR;
  if (system(cmd.c_str()) != 0) return false;

  // Clone just the v2_binary directory using sparse checkout
  std::string tmpdir = std::string(SST_8088_CACHE_DIR) + "/_clone";
  cmd = "rm -rf " + tmpdir + " && "
        "git clone --depth 1 --filter=blob:none --sparse "
        "https://github.com/SingleStepTests/8088.git " + tmpdir + " 2>&1 && "
        "cd " + tmpdir + " && git sparse-checkout set v2_binary 2>&1";
  fprintf(stderr, "  Cloning repo (sparse)...\n");
  if (system(cmd.c_str()) != 0) {
    fprintf(stderr, "  Failed to clone. Trying direct download...\n");
    // Fallback: download individual files with curl
    // Generate list of expected MOO files
    std::vector<std::string> names;
    for (int i = 0; i < 256; i++) {
      char buf[16];
      snprintf(buf, sizeof(buf), "%02X", i);
      names.push_back(buf);
    }
    // Add sub-opcode variants
    const char* grp_opcodes[] = {
      "80", "81", "82", "83", "C0", "C1", "D0", "D1", "D2", "D3",
      "F6", "F7", "FE", "FF", nullptr
    };
    for (int g = 0; grp_opcodes[g]; g++) {
      for (int s = 0; s < 8; s++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%s.%d", grp_opcodes[g], s);
        names.push_back(buf);
      }
    }

    int downloaded = 0;
    for (const auto& name : names) {
      std::string url = std::string(SST_8088_REPO) + "/" + name + ".MOO.gz";
      std::string dest = std::string(SST_8088_CACHE_DIR) + "/" + name + ".MOO.gz";
      if (file_exists(dest)) { downloaded++; continue; }
      std::string dl = "curl -sfL -o '" + dest + "' '" + url + "' 2>/dev/null";
      if (system(dl.c_str()) == 0) downloaded++;
    }
    // Decompress all .gz files
    cmd = std::string("cd ") + SST_8088_CACHE_DIR + " && gunzip -kf *.MOO.gz 2>/dev/null";
    system(cmd.c_str());
    fprintf(stderr, "  Downloaded %d files\n", downloaded);
    return downloaded > 100;
  }

  // Move and decompress MOO files
  cmd = std::string("cp ") + tmpdir + "/v2_binary/*.MOO.gz " + SST_8088_CACHE_DIR + "/ 2>/dev/null && "
        "cd " + SST_8088_CACHE_DIR + " && gunzip -kf *.MOO.gz 2>/dev/null && "
        "rm -rf " + tmpdir;
  fprintf(stderr, "  Decompressing...\n");
  system(cmd.c_str());

  // Verify
  DIR* d = opendir(SST_8088_CACHE_DIR);
  if (!d) return false;
  int count = 0;
  struct dirent* ent;
  while ((ent = readdir(d))) {
    std::string name = ent->d_name;
    if (name.size() >= 4 && name.substr(name.size()-4) == ".MOO") count++;
  }
  closedir(d);
  fprintf(stderr, "  Got %d MOO files\n", count);
  return count > 100;
}

// ---- Test runner ----

static int sst_run_file(const std::string& filepath, const std::string& basename,
                        int& total_pass, int& total_fail) {
  auto data = sst_read_file(filepath);
  if (data.empty()) return 0;

  auto tests = sst_parse_moo(data.data(), data.size());
  if (tests.empty()) return 0;

  fprintf(stderr, "%s: %zu tests...", basename.c_str(), tests.size());

  emu88_mem memory(0x110000);  // 1MB + 64K wraparound
  int pass = 0, fail = 0;

  for (const auto& tc : tests) {
    memset(memory.get_mem(), 0, 0x110000);

    sst_test_cpu cpu(&memory);
    cpu.reset();
    cpu.init_seg_caches();

    // 8088: 20-bit address bus, no A20 gate — addresses wrap at 1MB
    memory.set_a20(false);

    // 8088 mode
    cpu.cpu_type = emu88::CPU_8088;
    cpu.lock_ud = false;  // 8088 has no #UD exception

    // Load initial state
    for (int i = 0; i < 14; i++) {
      if (tc.initial.regs.bitmask & (1 << i))
        sst_set_reg(cpu, i, tc.initial.regs.values[i]);
    }
    memset(cpu.regs_hi, 0, sizeof(cpu.regs_hi));

    for (const auto& re : tc.initial.ram) {
      if (re.addr < 0x110000)
        memory.get_mem()[re.addr] = re.value;
    }

    // Execute exactly one instruction
    cpu.execute();

    // Build expected state
    uint16_t expected[14];
    for (int i = 0; i < 14; i++)
      expected[i] = (tc.initial.regs.bitmask & (1 << i)) ? tc.initial.regs.values[i] : 0;
    for (int i = 0; i < 14; i++) {
      if (tc.final_state.regs.bitmask & (1 << i))
        expected[i] = tc.final_state.regs.values[i];
    }

    // 8088 flags mask: SF ZF AF PF CF + OF = 0x0FD5
    // (bits 1,3,5 undefined; bits 12-15 undefined on 8088)
    const uint16_t FLAGS_MASK = 0x0FD5;
    bool ok = true;
    std::string failures;

    for (int i = 0; i < 14; i++) {
      uint16_t got = sst_get_reg(cpu, i);
      uint16_t exp = expected[i];
      if (i == 13) { got &= FLAGS_MASK; exp &= FLAGS_MASK; }
      if (got != exp) {
        ok = false;
        char buf[128];
        snprintf(buf, sizeof(buf), "  %s: got=0x%04X expected=0x%04X\n",
                 SST_REG_NAMES[i], got, exp);
        failures += buf;
      }
    }

    // Check memory
    for (const auto& re : tc.final_state.ram) {
      if (re.addr < 0x110000) {
        uint8_t got = memory.get_mem()[re.addr];
        if (got != re.value) {
          ok = false;
          char buf[128];
          snprintf(buf, sizeof(buf), "  mem[0x%05X]: got=0x%02X expected=0x%02X\n",
                   re.addr, got, re.value);
          failures += buf;
        }
      }
    }

    // Check unchanged initial memory
    std::map<uint32_t, uint8_t> final_ram_map;
    for (const auto& re : tc.final_state.ram) final_ram_map[re.addr] = re.value;
    for (const auto& re : tc.initial.ram) {
      if (final_ram_map.count(re.addr)) continue;
      if (re.addr < 0x110000) {
        uint8_t got = memory.get_mem()[re.addr];
        if (got != re.value) {
          ok = false;
          char buf[128];
          snprintf(buf, sizeof(buf), "  mem[0x%05X]: got=0x%02X expected=0x%02X (unchanged)\n",
                   re.addr, got, re.value);
          failures += buf;
        }
      }
    }

    if (ok) {
      pass++;
    } else {
      fail++;
      if (fail <= 10) {
        fprintf(stderr, "\nFAIL %s #%u \"%s\" bytes=[", basename.c_str(), tc.idx, tc.name.c_str());
        for (size_t i = 0; i < tc.bytes.size(); i++) {
          if (i) fprintf(stderr, " ");
          fprintf(stderr, "%02X", tc.bytes[i]);
        }
        fprintf(stderr, "]\n%s", failures.c_str());
      }
    }
  }

  total_pass += pass;
  total_fail += fail;

  if (fail == 0) fprintf(stderr, " ALL PASS\n");
  else fprintf(stderr, " %d FAIL / %zu total\n", fail, tests.size());

  return (int)tests.size();
}

// ---- Public entry point ----

int run_8088_sst_tests(const char* filter) {
  if (!download_8088_tests()) {
    fprintf(stderr, "8088 SingleStepTests: SKIPPED (download failed)\n");
    return 0;
  }

  std::vector<std::string> files;
  DIR* d = opendir(SST_8088_CACHE_DIR);
  if (!d) { perror(SST_8088_CACHE_DIR); return -1; }
  struct dirent* ent;
  while ((ent = readdir(d))) {
    std::string name = ent->d_name;
    if (name.size() >= 4 && name.substr(name.size()-4) == ".MOO") {
      if (filter) {
        // Match filter against basename without .MOO extension
        std::string base = name.substr(0, name.size() - 4);
        if (base != filter) continue;
      }
      files.push_back(name);
    }
  }
  closedir(d);
  std::sort(files.begin(), files.end());

  fprintf(stderr, "\n8088 SingleStepTests: %zu MOO files\n", files.size());

  int total_pass = 0, total_fail = 0, total_tests = 0;

  for (const auto& name : files) {
    std::string path = std::string(SST_8088_CACHE_DIR) + "/" + name;
    total_tests += sst_run_file(path, name, total_pass, total_fail);
  }

  fprintf(stderr, "\n=== 8088 SST TOTAL: %d pass, %d fail out of %d tests ===\n",
          total_pass, total_fail, total_tests);

  return total_fail;
}
