
BM_SRC_DIR := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
BM_LIBMICROKITCO_DIR := $(LIBMICROKITCO_PATH)
BM_LIBTRUSTEDLO_DIR := $(LIONSOS)/dep/libtrustedlo
BM_UNIKRAFT_DIR := $(LIONSOS)/dep/unikraft
BM_CATALOG_CORE_DIR := $(LIONSOS)/dep/catalog-core

BM_UK_APPLICATION ?= c-hello
BM_UK_PAYLOAD_ELF ?= $(BM_UK_APPLICATION)_default-arm64

BM_UK_CONFIG ?= uk-carrels-arm.config
BM_UK_CONFIG_SRC := $(BM_SRC_DIR)/$(BM_UK_CONFIG)

BM_UK_APP_DIR := $(BM_CATALOG_CORE_DIR)/$(BM_UK_APPLICATION)
BM_UK_BUILD_DIR := $(BUILD_DIR)/uk
BM_UK_BUILT_ELF := $(BM_UK_BUILD_DIR)/$(BM_UK_PAYLOAD_ELF)
BM_UK_CONFIGURED := $(BM_UK_BUILD_DIR)/.configured

BM_CLAGS := \
	-I$(PC_BENCHMARK_LIBC_INCLUDE) \
	-I$(BM_SRC_DIR) \
	-I$(BM_SRC_DIR)/config \
	-I$(BM_LIBTRUSTEDLO_DIR)/include \
	-I$(BM_LIBMICROKITCO_DIR)


LIBMICROKITCO_CFLAGS_pcbench := ${BM_CLAGS}
BM_LIBMICROKITCO_OBJ := libmicrokitco_pcbench.a


BM_LIBTRUSTEDLO_OBJ := libtrustedlo/libtrustedlo.a

PROGRAM_PATCH := pcbench/programs.o
BM_PAYLOAD_OBJS := pcbench/bm_payload.o
BM_MONITOR_OBJS := pcbench/bm_monitor.o pcbench/ossvc.o
BM_SERVER_OBJS := pcbench/bm_server.o
BM_OBJS := \
	$(BM_PAYLOAD_OBJS) \
	$(BM_MONITOR_OBJS) \
	$(BM_SERVER_OBJS)

pcbench:
	mkdir -p pcbench


pcbench/$(BM_LIBTRUSTEDLO_OBJ): pcbench
	make -f $(BM_LIBTRUSTEDLO_DIR)/Makefile \
			LIBTRUSTEDLO_PATH=$(BM_LIBTRUSTEDLO_DIR) \
			TARGET=$(TARGET) \
			MICROKIT_SDK:=$(MICROKIT_SDK) \
			BUILD_DIR:=pcbench \
			MICROKIT_BOARD:=$(MICROKIT_BOARD) \
			MICROKIT_CONFIG:=$(MICROKIT_CONFIG) \
			CPU:=$(CPU) \
			LLVM:=1


pcbench/%.o: CFLAGS := $(BM_CLAGS) \
			 		$(CFLAGS)
pcbench/%.o: $(BM_SRC_DIR)/%.c | pcbench
	$(CC) -c $(CFLAGS) $< -o $@

bm_server.elf: LDFLAGS += -L$(BOARD_DIR)/lib
bm_server.elf: $(BM_SERVER_OBJS) $(PROGRAM_PATCH) \
			  $(BM_LIBMICROKITCO_OBJ) pcbench/$(BM_LIBTRUSTEDLO_OBJ) libsddf_util.a \
              $(PC_BENCHMARK_LIBC_LIB)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@


bm_monitor.elf: LDFLAGS += -L$(BOARD_DIR)/lib
bm_monitor.elf: $(BM_MONITOR_OBJS) \
			  $(BM_LIBMICROKITCO_OBJ) pcbench/$(BM_LIBTRUSTEDLO_OBJ) libsddf_util.a \
              $(PC_BENCHMARK_LIBC_LIB)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@


PROTOCON_ELF := protocon.elf
TRAMPOLINE_ELF := trampoline.elf
BM_PAYLOAD_ELF := bm_payload.elf

$(BM_PAYLOAD_ELF): LDFLAGS += -L$(BOARD_DIR)/lib
$(BM_PAYLOAD_ELF): $(BM_PAYLOAD_OBJS) libsddf_util.a pcbench/$(BM_LIBTRUSTEDLO_OBJ)
	$(LD) $(LDFLAGS) -Ttext=0x2800000 $^ $(LIBS) -o $@


.PHONY: uk-build
uk-build: $(BM_UK_CONFIGURED) | pcbench
	$(MAKE) -C $(BM_UK_APP_DIR) \
		UK_ROOT=$(BM_UNIKRAFT_DIR) \
		UK_APP=$(BM_UK_APP_DIR) \
		UK_BUILD=$(BM_UK_BUILD_DIR) \
		-j$$(nproc)
	cp $(BM_UK_BUILT_ELF) pcbench/$(BM_UK_PAYLOAD_ELF)


$(BM_UK_CONFIGURED): $(BM_UK_CONFIG_SRC)
	$(MAKE) -C $(BM_UK_APP_DIR) \
		UK_ROOT=$(BM_UNIKRAFT_DIR) \
		UK_APP=$(BM_UK_APP_DIR) \
		UK_BUILD=$(BM_UK_BUILD_DIR) \
		distclean
	$(MAKE) -C $(BM_UK_APP_DIR) \
		UK_ROOT=$(BM_UNIKRAFT_DIR) \
		UK_APP=$(BM_UK_APP_DIR) \
		UK_BUILD=$(BM_UK_BUILD_DIR) \
		UK_DEFCONFIG=$(BM_UK_CONFIG_SRC) \
		defconfig
	mkdir -p $(BM_UK_BUILD_DIR)
	touch $@


# TARGET_PAYLOAD := $(BM_PAYLOAD_ELF)
TARGET_PAYLOAD := pcbench/$(BM_UK_PAYLOAD_ELF)

$(PROGRAM_PATCH): $(PROTOCON_ELF) $(TRAMPOLINE_ELF) uk-build
	cp $(BM_SRC_DIR)/package_program.S .
	$(CC) -c $(CFLAGS) \
		-DBM_PROTOCON_PATH=\"$(PROTOCON_ELF)\" \
		-DBM_TRAMPOLINE_PATH=\"$(TRAMPOLINE_ELF)\" \
		-DBM_PAYLOAD_PATH=\"$(TARGET_PAYLOAD)\" \
		package_program.S -o $@


-include $(BM_OBJS:.o=.d)
