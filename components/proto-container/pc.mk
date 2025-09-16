

PC_SRC_DIR := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
PC_LIBMICROKITCO_DIR := $(LIONSOS)/dep/libmicrokitco

LIBGCC := $(shell aarch64-none-elf-gcc -print-libgcc-file-name)

LIBCRYPTO_SRC_DIR := $(PC_SRC_DIR)/crypto
LIBEXTELF_SRC_DIR := $(PC_SRC_DIR)/extelf
LIBTSLDR_SRC_DIR  := $(PC_SRC_DIR)/trustedloader
include $(LIBCRYPTO_SRC_DIR)/Makefile
include $(LIBEXTELF_SRC_DIR)/Makefile
include $(LIBTSLDR_SRC_DIR)/Makefile

PC_CLAGS := \
	-I$(CONTAINER_LIBC_INCLUDE) \
	-I$(PC_LIBMICROKITCO_DIR) \
	-I$(PC_SRC_DIR)/config \
	-I$(LIBCRYPTO_SRC_DIR)/include \
	-I$(LIBEXTELF_SRC_DIR)/include \
	-I$(LIBTSLDR_SRC_DIR)/include

PC_LIBMICROKITCO_OBJ := libmicrokitco/libmicrokitco.a
PC_LIBMICROKITCO_OPT_PATH := $(PC_SRC_DIR)/config


PC_OBJS := \
	pc/monitor.o

pc:
	mkdir -p pc

pc/$(PC_LIBMICROKITCO_OBJ): pc
	make -f $(PC_LIBMICROKITCO_DIR)/Makefile \
			LIBMICROKITCO_PATH=$(PC_LIBMICROKITCO_DIR) \
			TARGET=$(TARGET) \
			MICROKIT_SDK:=$(MICROKIT_SDK) \
			BUILD_DIR:=pc \
			MICROKIT_BOARD:=$(MICROKIT_BOARD) \
			MICROKIT_CONFIG:=$(MICROKIT_CONFIG) \
			CPU:=$(CPU) \
			LLVM:=1 \
			LIBMICROKITCO_OPT_PATH=$(PC_LIBMICROKITCO_OPT_PATH)

pc/%.o: CFLAGS += $(PC_CLAGS)
pc/%.o: $(PC_SRC_DIR)/%.c $(CONTAINER_LIBC_INCLUDE) | pc
	$(CC) -c $(CFLAGS) $< -o $@

monitor.elf: LIBS += $(LIBGCC)
monitor.elf: LDFLAGS += -L$(BOARD_DIR)/lib -L$(LIBCRYPTO_BUILD_DIR) -L$(LIBEXTELF_BUILD_DIR) -L$(LIBTSLDR_BUILD_DIR)
monitor.elf: $(PC_OBJS) pc/$(PC_LIBMICROKITCO_OBJ) $(CONTAINER_LIBC_LIB) ${LIBEXTELF} ${LIBCRYPTO} $(LIBTSLDR)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

-include $(PC_OBJS:.o=.d)
