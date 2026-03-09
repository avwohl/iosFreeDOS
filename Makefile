CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -O2 -g
SRCDIR = src
OBJDIR = obj

# Core CPU emulator
CORE_SOURCES = $(SRCDIR)/emu88.cc $(SRCDIR)/emu88_mem.cc
CORE_OBJECTS = $(OBJDIR)/emu88.o $(OBJDIR)/emu88_mem.o

# DOS machine layer
DOS_SOURCES = $(SRCDIR)/dos_machine.cc $(SRCDIR)/dos_bios.cc
DOS_OBJECTS = $(OBJDIR)/dos_machine.o $(OBJDIR)/dos_bios.o

# Test
TEST_SOURCES = $(SRCDIR)/test_emu88.cc
TEST_OBJECTS = $(OBJDIR)/test_emu88.o

# CLI harness
CLI_SOURCES = $(SRCDIR)/main_cli.cc
CLI_OBJECTS = $(OBJDIR)/main_cli.o

all: test_emu88 dosemu_cli

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.cc | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

dosemu_cli: $(CORE_OBJECTS) $(DOS_OBJECTS) $(CLI_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

test_emu88: $(CORE_OBJECTS) $(TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -rf $(OBJDIR) test_emu88 dosemu_cli

.PHONY: all clean
