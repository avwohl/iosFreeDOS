CXX = g++
APP_VERSION := $(shell grep 'MARKETING_VERSION:' project.yml | head -1 | sed 's/.*: *"\{0,1\}\([^"]*\)"\{0,1\}/\1/')
APP_BUILD := $(shell grep 'CURRENT_PROJECT_VERSION:' project.yml | head -1 | sed 's/.*: *\([0-9]*\)/\1/')
CXXFLAGS = -Wall -Wextra -std=c++17 -O2 -g -DIOSFREEDOS_VERSION='"v$(APP_VERSION) ($(APP_BUILD))"'
NASM = nasm
SRCDIR = src
OBJDIR = obj

# Core CPU emulator
CORE_SOURCES = $(SRCDIR)/emu88.cc $(SRCDIR)/emu88_mem.cc
CORE_OBJECTS = $(OBJDIR)/emu88.o $(OBJDIR)/emu88_mem.o

# DOS machine layer
DOS_SOURCES = $(SRCDIR)/dos_machine.cc $(SRCDIR)/dos_bios.cc $(SRCDIR)/ne2000.cc
DOS_OBJECTS = $(OBJDIR)/dos_machine.o $(OBJDIR)/dos_bios.o $(OBJDIR)/ne2000.o

# Test
TEST_SOURCES = $(SRCDIR)/test_emu88.cc
TEST_OBJECTS = $(OBJDIR)/test_emu88.o

# CLI harness
CLI_SOURCES = $(SRCDIR)/main_cli.cc
CLI_OBJECTS = $(OBJDIR)/main_cli.o

# DOS COM programs (file transfer)
DOS_PROGS = dos/r.com dos/w.com

# Debug boot harness
DEBUG_SOURCES = $(SRCDIR)/debug_boot.cc
DEBUG_OBJECTS = $(OBJDIR)/debug_boot.o

all: test_emu88 freedos_cli debug_boot $(DOS_PROGS)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.cc | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

freedos_cli: $(CORE_OBJECTS) $(DOS_OBJECTS) $(CLI_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

test_emu88: $(CORE_OBJECTS) $(TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

debug_boot: $(CORE_OBJECTS) $(DOS_OBJECTS) $(DEBUG_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

dos/r.com: dos/r.asm
	$(NASM) -f bin -o $@ $<

dos/w.com: dos/w.asm
	$(NASM) -f bin -o $@ $<

clean:
	rm -rf $(OBJDIR) test_emu88 freedos_cli $(DOS_PROGS)

.PHONY: all clean
