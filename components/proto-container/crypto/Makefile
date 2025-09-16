
ifeq ($(strip $(BUILD_DIR)),)
$(error BUILD_DIR must be specified)
endif

TOOLCHAIN ?= aarch64-none-elf
CPU ?= cortex-a53

CC ?= $(TOOLCHAIN)-gcc
AR ?= $(TOOLCHAIN)-ar

BOARD_DIR ?= $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)


CRYPTO_OBJS :=  \
	fe.o        \
	ge.o        \
	key_exchange.o \
	keypair.o   \
	sc.o        \
	sha512.o    \
	sign.o      \
	verify.o    \
	add_scalar.o \
	tweetnacl.o

LIBCRYPTO_SRC_DIR ?= $(CURDIR)
LIBCRYPTO_BUILD_DIR := $(BUILD_DIR)/libcrypto
LIBCRYPTO_OBJS := $(addprefix $(LIBCRYPTO_BUILD_DIR)/,$(CRYPTO_OBJS))
LIBCRYPTO := $(LIBCRYPTO_BUILD_DIR)/libcrypto.a

LIBCRYPTO_CFLAGS ?= -mcpu=$(CPU) -mstrict-align -nostdlib -ffreestanding -g -O3 \
          			-Wall -Wno-unused-function -Werror \
          			-I$(BOARD_DIR)/include

all: libcrypto_dir $(LIBCRYPTO)

.PHONY: libcrypto_dir
libcrypto_dir: 
	$(info $(shell mkdir -p $(BUILD_DIR)/libcrypto))

$(LIBCRYPTO_OBJS): $(LIBCRYPTO_BUILD_DIR)/%.o : $(LIBCRYPTO_SRC_DIR)/%.c libcrypto_dir
	$(CC) -c $(LIBCRYPTO_CFLAGS) $< -o $@

$(LIBCRYPTO): $(LIBCRYPTO_OBJS)
	$(AR) rcs $@ $^
