#include "emu88_mem.h"
#include <cstring>
#include <cstdio>

emu88_mem::emu88_mem(emu88_uint32 size)
    : dat(nullptr), mem_size(size), a20_enabled(false) {
  dat = new emu88_uint8[mem_size];
  memset(dat, 0, mem_size);
}

emu88_mem::~emu88_mem() {
  delete[] dat;
  dat = nullptr;
}

emu88_uint8 emu88_mem::fetch_mem(emu88_uint32 addr) {
  emu88_uint32 masked = mask_addr(addr);
  if (masked >= mem_size) {
    fprintf(stderr, "[MEM] fetch_mem OOB: addr=0x%08X masked=0x%08X mem_size=0x%08X\n", addr, masked, mem_size);
    return 0xFF;
  }
  return dat[masked];
}

void emu88_mem::store_mem(emu88_uint32 addr, emu88_uint8 abyte) {
  emu88_uint32 masked = mask_addr(addr);
  if (masked >= mem_size) {
    fprintf(stderr, "[MEM] store_mem OOB: addr=0x%08X masked=0x%08X mem_size=0x%08X\n", addr, masked, mem_size);
    return;
  }
  dat[masked] = abyte;
}

emu88_uint16 emu88_mem::fetch_mem16(emu88_uint32 addr) {
  return fetch_mem(addr) | (emu88_uint16(fetch_mem(addr + 1)) << 8);
}

void emu88_mem::store_mem16(emu88_uint32 addr, emu88_uint16 aword) {
  store_mem(addr, aword & 0xFF);
  store_mem(addr + 1, (aword >> 8) & 0xFF);
}

emu88_uint32 emu88_mem::fetch_mem32(emu88_uint32 addr) {
  return fetch_mem16(addr) | (emu88_uint32(fetch_mem16(addr + 2)) << 16);
}

void emu88_mem::store_mem32(emu88_uint32 addr, emu88_uint32 adword) {
  store_mem16(addr, adword & 0xFFFF);
  store_mem16(addr + 2, (adword >> 16) & 0xFFFF);
}
