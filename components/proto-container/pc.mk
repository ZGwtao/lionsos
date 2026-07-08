

PC_SRC_DIR := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
PC_LIBMICROKITCO_DIR := $(LIBMICROKITCO_PATH)
PC_LIBTRUSTEDLO_DIR := $(LIONSOS)/dep/libtrustedlo

# ===================== unikraft variables ==========================

BM_UNIKRAFT_DIR := $(LIONSOS)/dep/unikraft
BM_CATALOG_CORE_DIR := $(LIONSOS)/dep/catalog-core

BM_UK_APPLICATION ?= c-hello
BM_UK_PAYLOAD_ELF ?= $(BM_UK_APPLICATION)_default-arm64

BM_UK_CONFIG ?= uk-carrels-arm.config
BM_UK_CONFIG_SRC := $(PC_SRC_DIR)/$(BM_UK_CONFIG)

BM_UK_APP_DIR := $(BM_CATALOG_CORE_DIR)/$(BM_UK_APPLICATION)
BM_UK_BUILD_DIR := $(BUILD_DIR)/uk
BM_UK_BUILT_ELF := $(BM_UK_BUILD_DIR)/$(BM_UK_PAYLOAD_ELF)
BM_UK_CONFIGURED := $(BM_UK_BUILD_DIR)/.configured

BM_UK_MAKE_ARGS := \
	UK_ROOT=$(BM_UNIKRAFT_DIR) \
	UK_APP=$(BM_UK_APP_DIR) \
	UK_BUILD=$(BM_UK_BUILD_DIR) \
	SDDF=$(SDDF) \
	LIONSOS=$(LIONSOS) \
	LIBMICROKITCO_PATH=$(LIBMICROKITCO_PATH) \
	MICROKIT_SDK=$(MICROKIT_SDK) \
	MICROKIT_BOARD=$(MICROKIT_BOARD) \
	MICROKIT_CONFIG=$(MICROKIT_CONFIG) \
	BOARD_DIR=$(BOARD_DIR) \
	SDDF_UTIL_LIB=$(abspath libsddf_util.a)

# ===================== unikraft variables ==========================

PC_CLAGS := \
	-I$(CONTAINER_LIBC_INCLUDE) \
	-I$(PC_SRC_DIR)/config \
	-I$(PC_SRC_DIR) \
	-I$(PC_LIBTRUSTEDLO_DIR)/include \
	-I$(PC_LIBMICROKITCO_DIR)

LIBMICROKITCO_CFLAGS_pc := ${PC_CLAGS}
PC_LIBMICROKITCO_OBJ := libmicrokitco_pc.a

PC_LIBTRUSTEDLO_OBJ := libtrustedlo/libtrustedlo.a

PC_ECHO_CLIENT_OBJS := pc/client_echo.o
PC_FAULTING_CLIENT_OBJS := pc/client_faulting.o
PC_LOOPING_CLIENT_OBJS := pc/client_looping.o
PC_TIMEOUT_CLIENT_OBJS := pc/client_timeout.o
PC_MONITOR_OBJS := pc/monitor.o pc/ossvc.o pc/pico_vfs.o
PC_FRONTEND_OBJS :=	pc/frontend.o pc/pico_vfs.o
PC_PROTOCON_OBJS := pc/protocon.o
PC_TRAMPOLINE_OBJS :=
PC_OBJS := \
	PC_FRONTEND_OBJS \
	PC_MONITOR_OBJS \
	PC_PROTOCON_OBJS \
	PC_TRAMPOLINE_OBJS \
	PC_ECHO_CLIENT_OBJS \
	PC_FAULTING_CLIENT_OBJS \
	PC_LOOPING_CLIENT_OBJS \
	PC_TIMEOUT_CLIENT_OBJS

pc:
	mkdir -p pc

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

# ===================== unikraft variables ==========================

.PHONY: uk-build
uk-build: $(BM_UK_CONFIGURED) libsddf_util.a | pc
	$(MAKE) -C $(BM_UK_APP_DIR) \
		$(BM_UK_MAKE_ARGS) \
		-j$$(nproc)
	cp $(BM_UK_BUILT_ELF) pc/$(BM_UK_PAYLOAD_ELF)


$(BM_UK_CONFIGURED): $(BM_UK_CONFIG_SRC)
	$(MAKE) -C $(BM_UK_APP_DIR) \
		$(BM_UK_MAKE_ARGS) \
		distclean
	$(MAKE) -C $(BM_UK_APP_DIR) \
		$(BM_UK_MAKE_ARGS) \
		UK_DEFCONFIG=$(BM_UK_CONFIG_SRC) \
		defconfig
	mkdir -p $(BM_UK_BUILD_DIR)
	touch $@

# ===================== unikraft variables ==========================

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

protocon.elf:
	cp $(BUILD_DIR)/pc/libtrustedlo/loader.elf $@

trampoline.elf:
	cp $(BUILD_DIR)/pc/libtrustedlo/trampoline.elf $@

client_echo.elf: LDFLAGS += -L$(BOARD_DIR)/lib
client_echo.elf: $(PC_ECHO_CLIENT_OBJS) libsddf_util.a pc/$(PC_LIBTRUSTEDLO_OBJ)
	$(LD) $(LDFLAGS) -Ttext=0x2800000 $^ $(LIBS) -o $@


client_looping.elf: LDFLAGS += -L$(BOARD_DIR)/lib
client_looping.elf: $(PC_LOOPING_CLIENT_OBJS) libsddf_util.a pc/$(PC_LIBTRUSTEDLO_OBJ)
	$(LD) $(LDFLAGS) -Ttext=0x2800000 $^ $(LIBS) -o $@


client_faulting.elf: LDFLAGS += -L$(BOARD_DIR)/lib
client_faulting.elf: $(PC_FAULTING_CLIENT_OBJS) libsddf_util.a pc/$(PC_LIBTRUSTEDLO_OBJ)
	$(LD) $(LDFLAGS) -Ttext=0x2800000 $^ $(LIBS) -o $@

client_timeout.elf: LDFLAGS += -L$(BOARD_DIR)/lib
client_timeout.elf: $(PC_TIMEOUT_CLIENT_OBJS) libsddf_util.a pc/$(PC_LIBTRUSTEDLO_OBJ)
	$(LD) $(LDFLAGS) -Ttext=0x2800000 $^ $(LIBS) -o $@

unikraft.elf: uk-build
	cp $(BUILD_DIR)/pc/$(BM_UK_PAYLOAD_ELF) $(BUILD_DIR)/unikraft.elf

-include $(PC_OBJS:.o=.d)
