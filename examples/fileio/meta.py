# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import struct
from random import randint
from dataclasses import dataclass
from typing import List, Tuple
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from importlib.metadata import version

assert version('sdfgen').split(".")[1] == "25", "Unexpected sdfgen version"

from sdfgen_helper import *
from config_structs import *

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel

@dataclass
class Board:
    name: str
    arch: SystemDescription.Arch
    paddr_top: int
    serial: str
    timer: str
    blk: str
    blk_partition: int


BOARDS: List[Board] = [
    Board(
        name="qemu_virt_aarch64",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x6_0000_000,
        serial="pl011@9000000",
        timer="timer",
        blk="virtio_mmio@a003e00",
        blk_partition=0,
    ),
    Board(
        name="maaxboard",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x7_0000_000,
        serial="soc@0/bus@30800000/serial@30860000",
        timer="soc@0/bus@30000000/timer@302d0000",
        blk="soc@0/bus@30800000/mmc@30b40000",
        blk_partition=3,
    ),
]

def fs_connection(pd1: SystemDescription.ProtectionDomain , pd2: SystemDescription.ProtectionDomain, queue_len: int):
    queue_name = "fs_command_queue_" + pd1.name + "_" + pd2.name
    #
    # sdf is cmdline arg
    # specify a memory region name "queue_name"
    #
    queue = MemoryRegion(sdf, queue_name, 0x8000)
    sdf.add_mr(queue)

    pd1_map = Map(queue, pd1.get_map_vaddr(queue), perms="rw")
    pd1.add_map(pd1_map)
    pd1_command_region = RegionResource(pd1_map.vaddr, 0x8000)

    pd2_map = Map(queue, pd2.get_map_vaddr(queue), perms="rw")
    pd2.add_map(pd2_map)
    pd2_command_region = RegionResource(pd2_map.vaddr, 0x8000)

    # ---- 0x8000 as fixed queue size from sdfgen lionsos.zig
    queue_name = "fs_completion_queue_" + pd1.name + "_" + pd2.name
    queue = MemoryRegion(sdf, queue_name, 0x8000)
    sdf.add_mr(queue)

    pd1_map = Map(queue, pd1.get_map_vaddr(queue), perms="rw")
    pd1.add_map(pd1_map)
    pd1_completion_region = RegionResource(pd1_map.vaddr, 0x8000)

    pd2_map = Map(queue, pd2.get_map_vaddr(queue), perms="rw")
    pd2.add_map(pd2_map)
    pd2_completion_region = RegionResource(pd2_map.vaddr, 0x8000)

    # ---- hardcoded for now
    queue_name = "fs_share_queue_" + pd1.name + "_" + pd2.name
    queue = MemoryRegion(sdf, queue_name, 1024 * 1024 * 64)
    sdf.add_mr(queue)

    pd1_map = Map(queue, pd1.get_map_vaddr(queue), perms="rw")
    pd1.add_map(pd1_map)
    pd1_share_region = RegionResource(pd1_map.vaddr, 1024 * 1024 * 64)

    pd2_map = Map(queue, pd2.get_map_vaddr(queue), perms="rw")
    pd2.add_map(pd2_map)
    pd2_share_region = RegionResource(pd2_map.vaddr, 1024 * 1024 * 64)

    ch = Channel(pd1, pd2)
    sdf.add_channel(ch)

    pd1_conn = FsConnectionResource(pd1_command_region, pd1_completion_region, pd1_share_region, queue_len, ch.pd_a_id)
    pd2_conn = FsConnectionResource(pd2_command_region, pd2_completion_region, pd2_share_region, queue_len, ch.pd_b_id)

    return [pd1_conn, pd2_conn]


def generate(sdf_path: str, output_dir: str, dtb: DeviceTree):
    serial_node = dtb.node(board.serial)
    assert serial_node is not None
    blk_node = dtb.node(board.blk)
    assert blk_node is not None
    timer_node = dtb.node(board.timer)
    assert timer_node is not None

    timer_driver = ProtectionDomain("timer_driver", "timer_driver.elf", priority=254)
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)

    serial_driver = ProtectionDomain("serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_virt_rx = ProtectionDomain("serial_virt_rx", "serial_virt_rx.elf", priority=99)
    serial_system = Sddf.Serial(sdf, serial_node, serial_driver, serial_virt_tx, virt_rx=serial_virt_rx)

    blk_driver = ProtectionDomain("blk_driver", "blk_driver.elf", priority=200)
    blk_virt = ProtectionDomain("blk_virt", "blk_virt.elf", priority=199, stack_size=0x2000)
    blk_system = Sddf.Blk(sdf, blk_node, blk_driver, blk_virt)

    micropython = ProtectionDomain("micropython", "micropython.elf", priority=1)

    serial_system.add_client(micropython)
    timer_system.add_client(micropython)

    fatfs1 = ProtectionDomain("fatfs1", "fat1.elf", priority=96)
    fatfs2 = ProtectionDomain("fatfs2", "fat2.elf", priority=96)

    # fs1 = LionsOs.FileSystem.Fat(
    #     sdf,
    #     fatfs1,
    #     micropython,
    #     blk=blk_system,
    #     partition=0
    # )

    # queue len = 512 as per lionsos.zig
    fs_client_server_chann = fs_connection(micropython, fatfs1, 512)
    # pd1 is client, pd2 is server
    fs_excl_client_config = FsClientConfig(
        [],
        fs_client_server_chann[0]
    )
    fs_server_config = FsServerConfig(
        [],
        fs_client_server_chann[1]
    )
    blk_system.add_client(fatfs1, partition=0)

    stack1 = MemoryRegion(sdf, "fat1_stack1", 0x40_000)
    stack2 = MemoryRegion(sdf, "fat1_stack2", 0x40_000)
    stack3 = MemoryRegion(sdf, "fat1_stack3", 0x40_000)
    stack4 = MemoryRegion(sdf, "fat1_stack4", 0x40_000)
    sdf.add_mr(stack1)
    sdf.add_mr(stack2)
    sdf.add_mr(stack3)
    sdf.add_mr(stack4)
    fatfs1.add_map(Map(stack1, 0xA0_000_000, perms="rw")) #, setvar_vaddr="worker_thread_stack_one"))
    fatfs1.add_map(Map(stack2, 0xB0_000_000, perms="rw")) #, setvar_vaddr="worker_thread_stack_two"))
    fatfs1.add_map(Map(stack3, 0xC0_000_000, perms="rw")) #, setvar_vaddr="worker_thread_stack_three"))
    fatfs1.add_map(Map(stack4, 0xD0_000_000, perms="rw")) #, setvar_vaddr="worker_thread_stack_four"))

    fs2 = LionsOs.FileSystem.Fat(
         sdf,
         fatfs2,
         micropython,
         blk=blk_system,
         partition=1
    )

    if board.name == "maaxboard":
        timer_system.add_client(blk_driver)

    pds = [
        serial_driver,
        serial_virt_tx,
        serial_virt_rx,
        micropython,
        fatfs1,
        fatfs2,
        timer_driver,
        blk_driver,
        blk_virt,
    ]
    for pd in pds:
        sdf.add_pd(pd)

    # assert fs1.connect()
    # assert fs1.serialise_config(output_dir)
    assert fs2.connect()
    assert fs2.serialise_config(output_dir)
    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)
    assert blk_system.connect()
    assert blk_system.serialise_config(output_dir)

    data_path = output_dir + "/fs_client_micropython_fatfs1.data"
    with open(data_path, "wb+") as f:
        f.write(fs_excl_client_config.serialise())
    update_elf_section(obj_copy, micropython.elf, fs_excl_client_config.section_name, data_path)

    data_path = output_dir + "/fs_server_fatfs1.data"
    with open(data_path, "wb+") as f:
        f.write(fs_server_config.serialise())
    update_elf_section(obj_copy, fatfs1.elf, fs_server_config.section_name, data_path)

    with open(f"{output_dir}/{sdf_path}", "w+") as f:
        f.write(sdf.render())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--dtb", required=True)
    parser.add_argument("--sddf", required=True)
    parser.add_argument("--board", required=True, choices=[b.name for b in BOARDS])
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)
    parser.add_argument("--objcopy", required=True)

    args = parser.parse_args()

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)

    global obj_copy
    obj_copy = args.objcopy

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    generate(args.sdf, args.output, dtb)
