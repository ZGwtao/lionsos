
ifndef MICROKIT_SDK
$(error MICROKIT_SDK is not set)
endif

ifndef BUILD_DIR 
$(error BUILD_DIR is not set)
endif

ifndef MICROKIT_BOARD 
$(error MICROKIT_BOARD is not set)
endif

ifndef MICROKIT_CONFIG 
$(error MICROKIT_CONFIG is not set)
endif
TOOLCHAIN ?= aarch64-none-elf
CPU ?= cortex-a53

CC ?= $(TOOLCHAIN)-gcc
AR ?= $(TOOLCHAIN)-ar

BOARD_DIR ?= $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)

TSLDR_OBJS :=  \
	trustedloader.o \
	acg.o

LIBEXTELF_SRC_DIR ?= $(CURDIR)/../extelf

LIBTSLDR_SRC_DIR ?= $(CURDIR)
LIBTSLDR_BUILD_DIR := $(BUILD_DIR)/libtsldr
LIBTSLDR_OBJS := $(addprefix $(LIBTSLDR_BUILD_DIR)/,$(TSLDR_OBJS))
LIBTSLDR := $(LIBTSLDR_BUILD_DIR)/libtsldr.a

LIBTSLDR_CFLAGS ?= -mcpu=$(CPU) -mstrict-align -nostdlib -ffreestanding -g -O3 \
          			-Wall -Wno-unused-function -Werror \
          			-I$(BOARD_DIR)/include \
					-I$(LIBTSLDR_SRC_DIR)/include \
					-I$(LIBEXTELF_SRC_DIR)/include

all: $(LIBTSLDR)

# Build directory as a real target
$(LIBTSLDR_BUILD_DIR):
	mkdir -p $@

# Object rules depend on the directory, not a phony
$(LIBTSLDR_BUILD_DIR)/%.o: $(LIBTSLDR_SRC_DIR)/%.c | $(LIBTSLDR_BUILD_DIR)
	$(CC) -c $(LIBTSLDR_CFLAGS) $< -o $@

# Archive rule
$(LIBTSLDR): $(LIBTSLDR_OBJS)
	$(AR) rcs $@ $^
