# Host tests. `make -C tests/host run` builds every tests/host/test_*.c that
# exists (other agents add files in parallel; the list is never hardcoded) and
# fails if any binary exits non-zero.
#
# Each test_*.c is self-contained: assert-based, no framework, links src/.

CC       ?= gcc
CXX      ?= g++
CFLAGS   ?= -std=c11 -Wall -Wextra -Werror -O2
CXXFLAGS ?= -std=c++11 -Wall -Wextra -Werror -O2
CPPFLAGS ?=
LDFLAGS  ?=
LDLIBS   ?= -lm

ROOT   := $(abspath $(CURDIR)/../..)
SRCDIR := $(ROOT)/src
OBJDIR := $(CURDIR)/build
SRC_C  := $(wildcard $(SRCDIR)/*.c)
SRC_CC := $(wildcard $(SRCDIR)/*.cpp)
HDRS   := $(wildcard $(SRCDIR)/*.h)
OBJS   := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRC_C)) \
          $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SRC_CC))
TESTS  := $(sort $(wildcard test_*.c))
BINS   := $(TESTS:.c=.bin)

CFLAGS += -I$(SRCDIR)
CXXFLAGS += -I$(SRCDIR)

.PHONY: all build run clean list

all: build

list:
	@echo "sources: $(notdir $(SRC_C) $(SRC_CC))"
	@echo "tests:   $(notdir $(TESTS))"

build: $(BINS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(HDRS)
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp $(HDRS)
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

%.bin: %.c $(OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(OBJS) $(LDFLAGS) $(LDLIBS) -o $@

run: build
	@fail=0; \
	cd $(ROOT) && for t in $(BINS); do \
	  printf '%-28s ' "$$t"; \
	  if $(CURDIR)/$$t; then echo "ok"; else echo "FAIL"; fail=1; fi; \
	done; \
	if [ $$fail -ne 0 ]; then echo "TESTS FAILED"; exit 1; fi; \
	echo "all tests passed"

clean:
	rm -rf $(OBJDIR) *.bin
