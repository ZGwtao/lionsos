# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import struct
from random import randint
from dataclasses import dataclass
from typing import List, Tuple
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from importlib.metadata import version

assert version('sdfgen').split(".")[1] == "23" or \
    version('sdfgen').split(".")[1] == "25" or \
    version('sdfgen').split(".")[1] == "29", "Unexpected sdfgen version"

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

def frontend_connect(mpd: SystemDescription.ProtectionDomain, fpd: SystemDescription.ProtectionDomain):

    name_prefix = mpd.name + "/" + fpd.name + "/"

    if version('sdfgen').split(".")[1] != "23":
        ext_trampoline_elf = MemoryRegion(sdf, name_prefix + "trampoline", 0x800000)
        ext_protocon_elf = MemoryRegion(sdf, name_prefix + "protocon", 0x800000)
        ext_client_elf = MemoryRegion(sdf, name_prefix + "client", 0x800000)
    else:
        ext_trampoline_elf = MemoryRegion(name_prefix + "trampoline", 0x800000)
        ext_protocon_elf = MemoryRegion(name_prefix + "protocon", 0x800000)
        ext_client_elf = MemoryRegion(name_prefix + "client", 0x800000)

    sdf.add_mr(ext_trampoline_elf)
    sdf.add_mr(ext_protocon_elf)
    sdf.add_mr(ext_client_elf)

    mpd.add_map(Map(ext_protocon_elf,   0x6000000, perms="rw", cached="true"))
    mpd.add_map(Map(ext_trampoline_elf, 0x6800000, perms="rw", cached="true"))
    mpd.add_map(Map(ext_client_elf,     0x7000000, perms="rw", cached="true"))

    fpd.add_map(Map(ext_trampoline_elf, 0x6000000, perms="rw", cached="true"))
    fpd.add_map(Map(ext_protocon_elf, 0x4000000, perms="rw", cached="true"))
    fpd.add_map(Map(ext_client_elf, 0xB000000, perms="rw", cached="true"))

    sdf.add_channel(Channel(a=mpd, b=fpd, a_id=50, b_id=1, pp_b=True))
    sdf.add_channel(Channel(a=mpd, b=fpd, a_id=15, b_id=30))


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

    pd_frontend = ProtectionDomain("frontend", "frontend.elf", priority=50, stack_size=0x10000)
    pd_monitor = ProtectionDomain("container_monitor", "monitor.elf", priority=54, stack_size=0x10000)
    # pd_frontend = ProtectionDomain("frontend", "frontend.elf", priority=50)

    frontend_connect(pd_monitor, pd_frontend)


    fatfs = ProtectionDomain("fatfs", "fat.elf", priority=96)
    frontend_fs = LionsOs.FileSystem.Fat(sdf, fatfs, pd_frontend, blk=blk_system, partition=0)
    pd_fs_monitor = ProtectionDomain("monitor_fs", "monitor_fs.elf", priority=96)
    monitor_fs = LionsOs.FileSystem.Fat(sdf, pd_fs_monitor, pd_monitor, blk=blk_system, partition=1)


    serial_system.add_client(pd_frontend)

    if board.name =="maaxboard":
        timer_system.add_client(blk_driver)

    pds = [
        serial_driver,
        serial_virt_tx,
        serial_virt_rx,
        pd_frontend,
        fatfs,
        timer_driver,
        blk_driver,
        blk_virt,
        pd_monitor,
        pd_fs_monitor
    ]
    for pd in pds:
        sdf.add_pd(pd)

    assert frontend_fs.connect()
    assert frontend_fs.serialise_config(output_dir)
    assert monitor_fs.connect()
    assert monitor_fs.serialise_config(output_dir)
    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)
    assert blk_system.connect()
    assert blk_system.serialise_config(output_dir)

    with open(f"{output_dir}/{sdf_path}", "w+") as f:
        f.write(sdf.render())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--dtb", required=True)
    parser.add_argument("--sddf", required=True)
    parser.add_argument("--board", required=True, choices=[b.name for b in BOARDS])
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)

    args = parser.parse_args()

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    generate(args.sdf, args.output, dtb)
