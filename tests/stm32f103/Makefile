# STM32F103 build gate (Cortex-M3, no FPU, soft-float).
# `make -C tests/stm32f103 run` cross-compiles every src/*.c + test_stm32f103.c
# into a thumb ELF, links it (newlib-nano + libm) and checks .text/.data sizes.
# It does not flash anything — it is a compile/link/static gate. Run with
#   make FZ_DOUBLE=1 -C tests/stm32f103 run
# to also verify the 64-bit fz_real_t build (software double on M3).

# override with e.g. CC=/path/to/arm-none-eabi-gcc or set ARM_GCC
ARM_GCC ?= $(shell command -v arm-none-eabi-gcc 2>/dev/null)
ifeq ($(ARM_GCC),)
ARM_GCC := /run/media/ranggawbp/558cd5a9-730a-4d9d-ba58-874c89a20ccf/tools/acdata/packages/STMicroelectronics/tools/xpack-arm-none-eabi-gcc/14.2.1-1.1/bin/arm-none-eabi-gcc
endif
ARM_SIZE := $(dir $(ARM_GCC))arm-none-eabi-size

CC       := $(ARM_GCC)
CPU      := -mcpu=cortex-m3 -mthumb
CFLAGS   ?= -std=c11 -Wall -Wextra -Werror -O2 -ffunction-sections -fdata-sections $(CPU)
CPPFLAGS ?=
ifeq ($(FZ_DOUBLE),1)
CPPFLAGS += -DFZ_USE_DOUBLE
endif
LDFLAGS  ?= $(CPU) -Wl,--gc-sections --specs=nano.specs --specs=rdimon.specs
LDLIBS   ?= -lm

ROOT   := $(abspath $(CURDIR)/../..)
SRCDIR := $(ROOT)/src
OBJDIR := $(CURDIR)/build
SRC_C  := $(wildcard $(SRCDIR)/*.c)
HDRS   := $(wildcard $(SRCDIR)/*.h)
OBJS   := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRC_C)) \
          $(OBJDIR)/test_stm32f103.o
ELF    := $(CURDIR)/fz_stm32f103.elf

CFLAGS += -I$(SRCDIR)

.PHONY: all build run clean size

all: build

build: $(ELF)

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(HDRS)
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJDIR)/test_stm32f103.o: test_stm32f103.c $(HDRS)
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(ELF): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) $(LDLIBS) -o $@

run: build
	@echo "== STM32F103 (Cortex-M3) build OK =="
	@echo "== sizes (text=flash, data+bss=RAM) =="
	@$(ARM_SIZE) $(ELF)

size: $(ELF)
	@$(ARM_SIZE) $(ELF)

clean:
	rm -rf $(OBJDIR) $(ELF)
