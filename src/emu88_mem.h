#ifndef EMU88_MEM_H
#define EMU88_MEM_H

#include "emu88_types.h"

// Memory for the 8088: 1MB address space (20-bit addressing)
// Override fetch_mem/store_mem in subclass for memory-mapped I/O, ROM regions, etc.

class emu88_mem {
  emu88_uint8 *dat;
public:
  emu88_mem();
  virtual ~emu88_mem();

  virtual emu88_uint8 *get_mem(void) {
    return dat;
  }

  virtual emu88_uint8 fetch_mem(emu88_uint32 addr);
  virtual void store_mem(emu88_uint32 addr, emu88_uint8 abyte);

  virtual emu88_uint16 fetch_mem16(emu88_uint32 addr);
  virtual void store_mem16(emu88_uint32 addr, emu88_uint16 aword);
};

#endif // EMU88_MEM_H
