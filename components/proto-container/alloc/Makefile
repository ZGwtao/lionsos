
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

CC := $(TOOLCHAIN)-gcc
AR := $(TOOLCHAIN)-ar

BOARD_DIR ?= $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)

KRMALLOC_OBJS :=  \
	k_r_malloc.o

LIBKRMALLOC_SRC_DIR ?= $(CURDIR)
LIBKRMALLOC_BUILD_DIR := $(BUILD_DIR)/libkrmalloc
LIBKRMALLOC_OBJS := $(addprefix $(LIBKRMALLOC_BUILD_DIR)/,$(KRMALLOC_OBJS))
LIBKRMALLOC := $(LIBKRMALLOC_BUILD_DIR)/libkrmalloc.a

LIBKRMALLOC_CFLAGS ?= -mcpu=$(CPU) -mstrict-align -nostdlib -ffreestanding -g -O3 \
          			-Wall -Wno-unused-function -Werror \
          			-I$(BOARD_DIR)/include \
					-I$(LIBKRMALLOC_SRC_DIR)/include

all: $(LIBKRMALLOC)

$(LIBKRMALLOC_BUILD_DIR):
	mkdir -p $@

$(LIBKRMALLOC_OBJS): $(LIBKRMALLOC_BUILD_DIR)/%.o : $(LIBKRMALLOC_SRC_DIR)/%.c | $(LIBKRMALLOC_BUILD_DIR)
	$(CC) -c $(LIBKRMALLOC_CFLAGS) $< -o $@

$(LIBKRMALLOC): $(LIBKRMALLOC_OBJS)
	$(AR) rcs $@ $^
