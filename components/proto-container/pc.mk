

PC_SRC_DIR := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
PC_LIBMICROKITCO_DIR := $(LIONSOS)/dep/libmicrokitco
PC_LIBTRUSTEDLO_DIR := $(LIONSOS)/dep/libtrustedlo

LIBGCC := $(shell aarch64-none-elf-gcc -print-libgcc-file-name)

LIBFSHELPER_SRC_DIR := $(PC_SRC_DIR)/fs
include $(LIBFSHELPER_SRC_DIR)/Makefile

PC_CLAGS := \
	-I$(CONTAINER_LIBC_INCLUDE) \
	-I$(PC_LIBMICROKITCO_DIR) \
	-I$(PC_LIBTRUSTEDLO_DIR)/include \
	-I$(PC_SRC_DIR)/config \
	-I$(PC_SRC_DIR) \
	-I$(LIBFSHELPER_SRC_DIR)/include

PC_LIBMICROKITCO_OBJ := libmicrokitco/libmicrokitco.a
PC_LIBMICROKITCO_OPT_PATH := $(PC_SRC_DIR)/config

PC_LIBTRUSTEDLO_OBJ := libtrustedlo/libtrustedlo.a

PC_CLIENT_OBJS := pc/client.o
PC_MONITOR_OBJS := pc/monitor.o pc/ossvc.o
PC_FRONTEND_OBJS :=	pc/frontend.o
PC_PROTOCON_OBJS := pc/protocon.o
PC_TRAMPOLINE_OBJS := pc/trampoline.o
PC_OBJS := \
	PC_FRONTEND_OBJS \
	PC_MONITOR_OBJS \
	PC_PROTOCON_OBJS \
	PC_TRAMPOLINE_OBJS \
	PC_CLIENT_OBJS

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

pc/$(PC_LIBTRUSTEDLO_OBJ): pc
	make -f $(PC_LIBTRUSTEDLO_DIR)/Makefile \
			LIBTRUSTEDLO_PATH=$(PC_LIBTRUSTEDLO_DIR) \
			TARGET=$(TARGET) \
			MICROKIT_SDK:=$(MICROKIT_SDK) \
			BUILD_DIR:=pc \
			MICROKIT_BOARD:=$(MICROKIT_BOARD) \
			MICROKIT_CONFIG:=$(MICROKIT_CONFIG) \
			CPU:=$(CPU) \
			CC:=$(TOOLCHAIN)-gcc \
			AR:=$(TOOLCHAIN)-ar

pc/%.o: CFLAGS += $(PC_CLAGS)
pc/%.o: $(PC_SRC_DIR)/%.c | pc
	$(CC) -c $(CFLAGS) $< -o $@

frontend.elf: LIBS += $(LIBGCC)
frontend.elf: LDFLAGS += -L$(BOARD_DIR)/lib \
							-L$(LIBFSHELPER_BUILD_DIR)
frontend.elf: $(PC_FRONTEND_OBJS) \
			  pc/$(PC_LIBMICROKITCO_OBJ) pc/$(PC_LIBTRUSTEDLO_OBJ) libsddf_util.a \
              $(LIBFSHELPER) $(CONTAINER_LIBC_LIB)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

monitor.elf: LIBS += $(LIBGCC)
monitor.elf: LDFLAGS += -L$(BOARD_DIR)/lib \
						-L$(LIBFSHELPER_BUILD_DIR)
monitor.elf: $(PC_MONITOR_OBJS) pc/$(PC_LIBMICROKITCO_OBJ) pc/$(PC_LIBTRUSTEDLO_OBJ) \
			 $(LIBFSHELPER) $(CONTAINER_LIBC_LIB)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

protocon.elf: LIBS += $(LIBGCC)
protocon.elf: LDFLAGS += -L$(BOARD_DIR)/lib
protocon.elf: $(PC_PROTOCON_OBJS) \
			  pc/$(PC_LIBTRUSTEDLO_OBJ)
	$(LD) $(LDFLAGS) \
		--defsym __sel4_ipc_buffer=0x100000 \
		--defsym loader_context=0xE00000 \
		$^ $(LIBS) -o $@

trampoline.elf: LIBS += $(LIBGCC)
trampoline.elf: LDFLAGS += -L$(BOARD_DIR)/lib
trampoline.elf: $(PC_TRAMPOLINE_OBJS) pc/$(PC_LIBTRUSTEDLO_OBJ)
	$(LD) $(LDFLAGS) -Ttext=0x1800000 $^ $(LIBS) -o $@


client.elf: LIBS += $(LIBGCC)
client.elf: LDFLAGS += -L$(BOARD_DIR)/lib
client.elf: $(PC_CLIENT_OBJS) libsddf_util.a pc/$(PC_LIBTRUSTEDLO_OBJ)
	$(LD) $(LDFLAGS) -Ttext=0x2800000 $^ $(LIBS) -o $@


-include $(PC_OBJS:.o=.d)
