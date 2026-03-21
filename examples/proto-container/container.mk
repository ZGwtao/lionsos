#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
IMAGES := \
	timer_driver.elf \
	monitor.elf \
	frontend.elf \
	fat.elf \
	client.elf \
	trampoline.elf \
	protocon.elf \
	serial_driver.elf \
	serial_virt_rx.elf \
	serial_virt_tx.elf \
	blk_virt.elf \
	blk_driver.elf

SUPPORTED_BOARDS:= \
	qemu_virt_aarch64 \
	maaxboard

TOOLCHAIN ?= clang
CP ?= cp
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
SDDF := $(LIONSOS)/dep/sddf
LIBMICROKITCO_PATH := $(LIONSOS)/dep/libmicrokitco
SYSTEM_FILE := container.system
IMAGE_FILE := container.img
REPORT_FILE := report.txt


all: ${IMAGE_FILE}

include ${SDDF}/tools/make/board/common.mk

METAPROGRAM := $(CONTAINER_DIR)/meta.py
FAT := $(LIONSOS)/components/fs/fat

CFLAGS += \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit \
	-I$(LIBMICROKITCO_PATH)

include $(LIONSOS)/lib/libc/libc.mk

LDFLAGS := -L$(BOARD_DIR)/lib -L$(LIONS_LIBC)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a -lc

BLK_DRIVER := $(SDDF)/drivers/blk/${BLK_DRIV_DIR}
BLK_COMPONENTS := $(SDDF)/blk/components

SDDF_LIBC_INCLUDE := $(LIONS_LIBC)/include
include ${SDDF}/util/util.mk
include ${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk
include ${SDDF}/drivers/serial/${UART_DRIV_DIR}/serial_driver.mk
include ${SDDF}/serial/components/serial_components.mk
include ${SDDF}/libco/libco.mk
include ${BLK_DRIVER}/blk_driver.mk
include ${BLK_COMPONENTS}/blk_components.mk


%.py: ${CONTAINER_DIR}/%.py
	cp $< $@

FAT_LIBC_LIB := $(LIONS_LIBC)/lib/libc.a
FAT_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIONSOS)/components/fs/fat/fat.mk

CONTAINER_LIBC_LIB := $(LIONS_LIBC)/lib/libc.a
CONTAINER_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIONSOS)/components/proto-container/pc.mk

LIBMICROKITCO_LIBC_INCLUDE := $(LIONS_LIBC)/include
include $(LIBMICROKITCO_PATH)/libmicrokitco.mk


${IMAGES}: $(LIONS_LIBC)/lib/libc.a libsddf_util_debug.a


FORCE:


$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	$(CP) fat.elf frontend_fs.elf
	$(CP) fat.elf monitor_fs.elf
	$(CP) fat.elf protocon0_fs.elf
	$(CP) fat.elf protocon1_fs.elf
	PYTHONPATH=${SDDF}/tools/meta:$$PYTHONPATH $(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE)
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .serial_virt_rx_config=serial_virt_rx.data serial_virt_rx.elf
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_frontend.data frontend.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_frontend.data frontend.elf
	$(OBJCOPY) --update-section .fs_client_config=fs_client_container_monitor.data monitor.elf
	$(OBJCOPY) --update-section .device_resources=blk_driver_device_resources.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_driver_config=blk_driver.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_virt_config=blk_virt.data blk_virt.elf
	$(OBJCOPY) --update-section .blk_client_config=blk_client_frontend_fs.data frontend_fs.elf
	$(OBJCOPY) --update-section .fs_server_config=fs_server_frontend_fs.data frontend_fs.elf
	$(OBJCOPY) --update-section .blk_client_config=blk_client_monitor_fs.data monitor_fs.elf
	$(OBJCOPY) --update-section .fs_server_config=fs_server_monitor_fs.data monitor_fs.elf
	$(OBJCOPY) --update-section .blk_client_config=blk_client_protocon0_fs.data protocon0_fs.elf
	$(OBJCOPY) --update-section .fs_server_config=fs_server_protocon0_fs.data protocon0_fs.elf
	$(OBJCOPY) --update-section .blk_client_config=blk_client_protocon1_fs.data protocon1_fs.elf
	$(OBJCOPY) --update-section .fs_server_config=fs_server_protocon1_fs.data protocon1_fs.elf

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

