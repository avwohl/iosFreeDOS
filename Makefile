CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -O2 -g
SRCDIR = src
OBJDIR = obj

SOURCES = $(SRCDIR)/emu88.cc $(SRCDIR)/emu88_mem.cc
OBJECTS = $(OBJDIR)/emu88.o $(OBJDIR)/emu88_mem.o

TEST_SOURCES = $(SRCDIR)/test_emu88.cc
TEST_OBJECTS = $(OBJDIR)/test_emu88.o

all: test_emu88

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.cc | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

test_emu88: $(OBJECTS) $(TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -rf $(OBJDIR) test_emu88

.PHONY: all clean
