#include "emu88_mem.h"
#include <cstring>

#define MEM_SIZE (0x100000)  // 1MB for 8088

emu88_mem::emu88_mem() : dat(0) {
  dat = new emu88_uint8[MEM_SIZE];
  memset(dat, 0, MEM_SIZE);
}

emu88_mem::~emu88_mem() {
  delete[] dat;
  dat = 0;
}

emu88_uint8 emu88_mem::fetch_mem(emu88_uint32 addr) {
  return dat[addr & 0xFFFFF];
}

void emu88_mem::store_mem(emu88_uint32 addr, emu88_uint8 abyte) {
  dat[addr & 0xFFFFF] = abyte;
}

emu88_uint16 emu88_mem::fetch_mem16(emu88_uint32 addr) {
  return fetch_mem(addr) | (emu88_uint16(fetch_mem(addr + 1)) << 8);
}

void emu88_mem::store_mem16(emu88_uint32 addr, emu88_uint16 aword) {
  store_mem(addr, aword & 0xFF);
  store_mem(addr + 1, (aword >> 8) & 0xFF);
}
