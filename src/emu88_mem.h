#ifndef EMU88_MEM_H
#define EMU88_MEM_H

#include "emu88_types.h"

// Memory: 1MB conventional + extended memory (up to 16MB)
// A20 gate controls whether address bit 20 is masked (8086 wraparound)

class emu88_mem {
  emu88_uint8 *dat;
  emu88_uint32 mem_size;
  bool a20_enabled;
public:
  emu88_mem(emu88_uint32 size = 0x100000);  // default 1MB
  virtual ~emu88_mem();

  virtual emu88_uint8 *get_mem(void) { return dat; }
  emu88_uint32 get_mem_size(void) const { return mem_size; }

  // A20 gate control
  void set_a20(bool enabled) { a20_enabled = enabled; }
  bool get_a20() const { return a20_enabled; }

  // Address masking: if A20 disabled, bit 20 is forced to 0
  emu88_uint32 mask_addr(emu88_uint32 addr) const {
    if (!a20_enabled) addr &= 0xFFFFF;  // 20-bit wrap
    return addr < mem_size ? addr : addr % mem_size;
  }

  virtual emu88_uint8 fetch_mem(emu88_uint32 addr);
  virtual void store_mem(emu88_uint32 addr, emu88_uint8 abyte);

  virtual emu88_uint16 fetch_mem16(emu88_uint32 addr);
  virtual void store_mem16(emu88_uint32 addr, emu88_uint16 aword);

  virtual emu88_uint32 fetch_mem32(emu88_uint32 addr);
  virtual void store_mem32(emu88_uint32 addr, emu88_uint32 adword);
};

#endif // EMU88_MEM_H
