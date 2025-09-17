
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

FSHELPER_OBJS :=  \
	fs_helpers.o

LIBFSHELPER_SRC_DIR ?= $(CURDIR)
LIBFSHELPER_BUILD_DIR := $(BUILD_DIR)/libfshelper
LIBFSHELPER_OBJS := $(addprefix $(LIBFSHELPER_BUILD_DIR)/,$(FSHELPER_OBJS))
LIBFSHELPER := $(LIBFSHELPER_BUILD_DIR)/libfshelper.a

LIBFSHELPER_CFLAGS ?= -mcpu=$(CPU) -mstrict-align -nostdlib -ffreestanding -g -O3 \
          			-Wall -Wno-unused-function -Werror \
          			-I$(BOARD_DIR)/include \
					-I$(LIBFSHELPER_SRC_DIR)/include \
					-I$(LIONSOS)/include \
					-I$(SDDF)/include

all: $(LIBFSHELPER)

$(LIBFSHELPER_BUILD_DIR):
	mkdir -p $@

$(LIBFSHELPER_OBJS): $(LIBFSHELPER_BUILD_DIR)/%.o : $(LIBFSHELPER_SRC_DIR)/%.c | $(LIBFSHELPER_BUILD_DIR)
	$(CC) -c $(LIBFSHELPER_CFLAGS) $< -o $@

$(LIBFSHELPER): $(LIBFSHELPER_OBJS)
	$(AR) rcs $@ $^
