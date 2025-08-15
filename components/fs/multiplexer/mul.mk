
FS_MUL_SRC_DIR	:= $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
FS_MUL_LIBMICROKITCO_DIR := $(LIONSOS)/dep/libmicrokitco

FS_MUL_CFLAGS := \
	-I$(FAT_LIBC_INCLUDE) \
	-I$(FS_MUL_LIBMICROKITCO_DIR) \
	-I$(FS_MUL_SRC_DIR)/config

FS_MUL_OBJ	:= \
	multiplexer/mul.o

FS_MUL_LIBMICROKITCO_OBJ := libmicrokitco/libmicrokitco.a
FS_MUL_LIBMICROKITCO_OPT_PATH := $(FS_MUL_SRC_DIR)/config

multiplexer:
	mkdir -p multiplexer
multiplexer/$(FS_MUL_LIBMICROKITCO_OBJ): multiplexer
	make -f $(FS_MUL_LIBMICROKITCO_DIR)/Makefile \
			LIBMICROKITCO_PATH=$(FS_MUL_LIBMICROKITCO_DIR) \
			TARGET=$(TARGET) \
			MICROKIT_SDK:=$(MICROKIT_SDK) \
			BUILD_DIR:=multiplexer \
			MICROKIT_BOARD:=$(MICROKIT_BOARD) \
			MICROKIT_CONFIG:=$(MICROKIT_CONFIG) \
			CPU:=$(CPU) \
			LLVM:=1 \
			LIBMICROKITCO_OPT_PATH=$(FS_MUL_LIBMICROKITCO_OPT_PATH)


multiplexer/%.o: CFLAGS += $(FS_MUL_CFLAGS)
multiplexer/%.o: $(FS_MUL_SRC_DIR)/%.c $(FAT_LIBC_INCLUDE) | multiplexer
	$(CC) -c $(CFLAGS) $< -o $@

multiplexer.elf: $(FS_MUL_OBJ) multiplexer/$(FS_MUL_LIBMICROKITCO_OBJ) $(FAT_LIBC_LIB)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

-include $(FS_MUL_OBJ:.o=.d)
