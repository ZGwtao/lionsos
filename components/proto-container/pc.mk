

PC_SRC_DIR := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
PC_LIBMICROKITCO_DIR := $(LIBMICROKITCO_PATH)
PC_LIBTRUSTEDLO_DIR := $(LIONSOS)/dep/libtrustedlo

PC_CLAGS := \
	-I$(CONTAINER_LIBC_INCLUDE) \
	-I$(PC_SRC_DIR)/config \
	-I$(PC_SRC_DIR) \
	-I$(PC_LIBTRUSTEDLO_DIR)/include \
	-I$(PC_LIBMICROKITCO_DIR)

LIBMICROKITCO_CFLAGS_pc := ${PC_CLAGS}
PC_LIBMICROKITCO_OBJ := libmicrokitco_pc.a

PC_LIBTRUSTEDLO_OBJ := libtrustedlo/libtrustedlo.a

PC_CLIENT_OBJS := pc/client.o
PC_MONITOR_OBJS := pc/monitor.o pc/ossvc.o pc/pico_vfs.o
PC_FRONTEND_OBJS :=	pc/frontend.o pc/pico_vfs.o
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

#pc/$(PC_LIBMICROKITCO_OBJ): pc
#	make -f $(PC_LIBMICROKITCO_DIR)/Makefile \
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
			LLVM:=1


pc/%.o: CFLAGS := $(PC_CLAGS) \
			 		$(CFLAGS)
pc/%.o: $(PC_SRC_DIR)/%.c | pc
	$(CC) -c $(CFLAGS) $< -o $@


frontend.elf: LDFLAGS += -L$(BOARD_DIR)/lib
frontend.elf: $(PC_FRONTEND_OBJS) \
			  $(PC_LIBMICROKITCO_OBJ) pc/$(PC_LIBTRUSTEDLO_OBJ) libsddf_util.a \
              $(CONTAINER_LIBC_LIB)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

monitor.elf: LDFLAGS += -L$(BOARD_DIR)/lib
monitor.elf: $(PC_MONITOR_OBJS) pc/$(PC_LIBTRUSTEDLO_OBJ) $(PC_LIBMICROKITCO_OBJ) \
			 $(CONTAINER_LIBC_LIB)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

protocon.elf: LDFLAGS += -L$(BOARD_DIR)/lib
protocon.elf: $(PC_PROTOCON_OBJS) \
			  pc/$(PC_LIBTRUSTEDLO_OBJ)
	$(LD) $(LDFLAGS) \
		--defsym __sel4_ipc_buffer=0x100000 \
		--defsym loader_context=0xE00000 \
		$^ $(LIBS) -o $@

trampoline.elf: LDFLAGS += -L$(BOARD_DIR)/lib
trampoline.elf: $(PC_TRAMPOLINE_OBJS) pc/$(PC_LIBTRUSTEDLO_OBJ)
	$(LD) $(LDFLAGS) -Ttext=0x1800000 $^ $(LIBS) -o $@

client.elf: LDFLAGS += -L$(BOARD_DIR)/lib
client.elf: $(PC_CLIENT_OBJS) libsddf_util.a pc/$(PC_LIBTRUSTEDLO_OBJ)
	$(LD) $(LDFLAGS) -Ttext=0x2800000 $^ $(LIBS) -o $@


-include $(PC_OBJS:.o=.d)
