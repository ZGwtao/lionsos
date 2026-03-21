#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
IMAGES := \
	timer_driver.elf \
	bm_monitor.elf \
	bm_server.elf \
	bm_payload.elf \
	trampoline.elf \
	protocon.elf \
	serial_driver.elf \
	serial_virt_rx.elf \
	serial_virt_tx.elf

SUPPORTED_BOARDS:= \
	qemu_virt_aarch64 \
	maaxboard

TOOLCHAIN ?= clang
CP ?= cp
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
SDDF := $(LIONSOS)/dep/sddf
LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco
SYSTEM_FILE := pc_benchmark.system
IMAGE_FILE := pc_benchmark.img
REPORT_FILE := report.txt


all: ${IMAGE_FILE}

include ${SDDF}/tools/make/board/common.mk

METAPROGRAM := $(PC_BENCHMARK_DIR)/meta.py

CFLAGS += \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(LIBMICROKITCO_PATH)

include $(LIONSOS)/lib/libc/libc.mk

LDFLAGS := -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a -lc

SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include
include ${SDDF}/util/util.mk
include ${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk
include ${SDDF}/drivers/serial/${UART_DRIV_DIR}/serial_driver.mk
include ${SDDF}/serial/components/serial_components.mk
include ${SDDF}/libco/libco.mk


%.py: ${PC_BENCHMARK_DIR}/%.py
	cp $< $@

CONTAINER_LIBC_LIB := $(LIONS_LIBC)/lib/libc.a
CONTAINER_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIONSOS)/components/proto-container/pc.mk

PC_BENCHMARK_LIBC_LIB := $(LIONS_LIBC)/lib/libc.a
PC_BENCHMARK_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIONSOS)/components/pc-benchmark/pcbench.mk

LIBMICROKITCO_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIBMICROKITCO_PATH)/libmicrokitco.mk


${IMAGES}: $(LIONS_LIBC)/lib/libc.a libsddf_util_debug.a


FORCE:


$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	PYTHONPATH=${SDDF}/tools/meta:$$PYTHONPATH $(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE)
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .serial_virt_rx_config=serial_virt_rx.data serial_virt_rx.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_bm_server.data bm_server.elf
	$(OBJCOPY) --update-section .pl0_serial_config=serial_client_protocon0.data bm_monitor.elf
	$(OBJCOPY) --update-section .pl1_serial_config=serial_client_protocon1.data bm_monitor.elf
	$(OBJCOPY) --update-section .pl2_serial_config=serial_client_protocon2.data bm_monitor.elf
	$(OBJCOPY) --update-section .pl3_serial_config=serial_client_protocon3.data bm_monitor.elf

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) \
		--search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) 	\
		--config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

qemu_disk:
	$(LIONSOS)/dep/sddf/tools/mkvirtdisk $@ 4 512 16777216 GPT

qemu: ${IMAGE_FILE} qemu_disk
	$(QEMU) -machine virt,virtualization=on \
		-cpu cortex-a53 \
		-serial mon:stdio \
		-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
		-m size=2G \
		-nographic \
		-global virtio-mmio.force-legacy=false \
		-d guest_errors \
		-drive file=qemu_disk,if=none,format=raw,id=hd \
		-device virtio-blk-device,drive=hd

${SDDF}/tools/make/board/common.mk ${SDDF_MAKEFILES} ${LIONSOS}/dep/sddf/include &:
	cd $(LIONSOS); git submodule update --init dep/sddf

