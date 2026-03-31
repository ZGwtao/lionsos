# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import struct
from random import randint
from dataclasses import dataclass
from typing import List, Tuple
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from importlib.metadata import version
from board import BOARDS

assert version('sdfgen').split(".")[1] == "23" or \
    version('sdfgen').split(".")[1] == "25" or \
    version('sdfgen').split(".")[1] == "29", "Unexpected sdfgen version"

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel

def connect_protocon_with_monitor(mpd: SystemDescription.ProtectionDomain, cpd: SystemDescription.ProtectionDomain, cid: int):

    name_prefix = mpd.name + "/" + cpd.name + "/"

    if version('sdfgen').split(".")[1] != "23":
        # one per container memory regions...
        container_elf = MemoryRegion(sdf, name_prefix + "container/elf", 0x800000)
        trampoline_elf = MemoryRegion(sdf, name_prefix + "trampoline/elf", 0x800000)
        trampoline_exec = MemoryRegion(sdf, name_prefix + "trampoline/exec", 0x800000)
        tsldr_exec = MemoryRegion(sdf, name_prefix + "tsldr/exec", 0x800000)
        tsldr_data = MemoryRegion(sdf, name_prefix + "tsldr/data", 0x1000)
        ossvc_data = MemoryRegion(sdf, name_prefix + "ossvc/data", 0x1000)
        tsldr_context = MemoryRegion(sdf, name_prefix + "tsldr/context", 0x1000)
    else:
        # one per container memory regions...
        container_elf = MemoryRegion(name_prefix + "container/elf", 0x800000)
        trampoline_elf = MemoryRegion(name_prefix + "trampoline/elf", 0x800000)
        trampoline_exec = MemoryRegion(name_prefix + "trampoline/exec", 0x800000)
        tsldr_exec = MemoryRegion(name_prefix + "tsldr/exec", 0x800000)
        tsldr_data = MemoryRegion(name_prefix + "tsldr/data", 0x1000)
        ossvc_data = MemoryRegion(name_prefix + "ossvc/data", 0x1000)
        tsldr_context = MemoryRegion(name_prefix + "tsldr/context", 0x1000)

    sdf.add_mr(container_elf)
    sdf.add_mr(trampoline_elf)
    sdf.add_mr(trampoline_exec)
    sdf.add_mr(tsldr_exec)
    sdf.add_mr(tsldr_data)
    sdf.add_mr(ossvc_data)
    sdf.add_mr(tsldr_context)

    mpd.add_map(Map(tsldr_context,  0x0ff40000 + cid * 0x1000,   perms="rw", cached="true"))
    mpd.add_map(Map(ossvc_data,     0x0ff80000 + cid * 0x1000,   perms="rw", cached="true"))
    mpd.add_map(Map(tsldr_data,     0x0ffc0000 + cid * 0x1000,   perms="rw", cached="true"))
    mpd.add_map(Map(tsldr_exec,     0x10000000 + cid * 0x800000, perms="rw", cached="true"))
    mpd.add_map(Map(trampoline_elf, 0x30000000 + cid * 0x800000, perms="rw", cached="true"))
    mpd.add_map(Map(container_elf,  0x50000000 + cid * 0x800000, perms="rw", cached="true"))

    cpd.add_map(Map(tsldr_exec,         0x0200000, perms="rwx", cached="true"))
    cpd.add_map(Map(tsldr_data,         0x0A00000, perms="rw", cached="true"))
    cpd.add_map(Map(ossvc_data,         0x0A01000, perms="rw", cached="true"))
    cpd.add_map(Map(tsldr_context,      0x0E00000, perms="rw", cached="true"))
    cpd.add_map(Map(trampoline_elf,     0x1000000, perms="rwx", cached="true"))
    cpd.add_map(Map(trampoline_exec,    0x1800000, perms="rwx", cached="true"))
    cpd.add_map(Map(container_elf,      0x2000000, perms="rw", cached="true"))

    if version('sdfgen').split(".")[1] != "23":
        trampoline_stack = MemoryRegion(sdf, name_prefix + "trampoline/stack", 0x1000)
        container_stack = MemoryRegion(sdf, name_prefix + "container/stack", 0x1000)
        container_exec = MemoryRegion(sdf, name_prefix + "container/exec", 0x2000000)
    else:
        trampoline_stack = MemoryRegion(name_prefix + "trampoline/stack", 0x1000)
        container_stack = MemoryRegion(name_prefix + "container/stack", 0x1000)
        container_exec = MemoryRegion(name_prefix + "container/exec", 0x2000000)

    sdf.add_mr(trampoline_stack)
    sdf.add_mr(container_stack)
    sdf.add_mr(container_exec)

    cpd.add_map(Map(trampoline_stack, 0x00FFFDFF000, perms="rw", cached="true"))
    cpd.add_map(Map(container_stack,  0x00FFFBFF000, perms="rw", cached="true"))
    cpd.add_map(Map(container_exec, 0x2800000, perms="rwx", cached="true"))

    sdf.add_channel(Channel(a=mpd, b=cpd, a_id=(24+cid), b_id=15, pp_b=True))


def connect_frontend_with_monitor(mpd: SystemDescription.ProtectionDomain, fpd: SystemDescription.ProtectionDomain):

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
    timer_node = dtb.node(board.timer)
    assert timer_node is not None

    serial_driver = ProtectionDomain("serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_virt_rx = ProtectionDomain("serial_virt_rx", "serial_virt_rx.elf", priority=99)
    serial_system = Sddf.Serial(sdf, serial_node, serial_driver, serial_virt_tx, virt_rx=serial_virt_rx)

    pc_bm_server = ProtectionDomain("bm_server", "bm_server.elf", priority=50, stack_size=0x10000)
    pc_bm_monitor = ProtectionDomain("bm_monitor", "bm_monitor.elf", priority=54, stack_size=0x10000, is_monitor=True)

    connect_frontend_with_monitor(pc_bm_monitor, pc_bm_server)

    serial_system.add_client(pc_bm_server)
    serial_system.add_client(pc_bm_monitor)

    protocon0 = ProtectionDomain("protocon0", priority=53)
    protocon1 = ProtectionDomain("protocon1", priority=53)
    protocon2 = ProtectionDomain("protocon2", priority=53)
    protocon3 = ProtectionDomain("protocon3", priority=53)

    _ = pc_bm_monitor.add_child_pd(protocon0, child_id=0)
    _ = pc_bm_monitor.add_child_pd(protocon1, child_id=1)
    _ = pc_bm_monitor.add_child_pd(protocon2, child_id=2)
    _ = pc_bm_monitor.add_child_pd(protocon3, child_id=3)

    connect_protocon_with_monitor(pc_bm_monitor, protocon0, 0)
    connect_protocon_with_monitor(pc_bm_monitor, protocon1, 1)
    connect_protocon_with_monitor(pc_bm_monitor, protocon2, 2)
    connect_protocon_with_monitor(pc_bm_monitor, protocon3, 3)

    serial_system.add_client(protocon0, optional=True)
    serial_system.add_client(protocon1, optional=True)
    serial_system.add_client(protocon2, optional=True)
    serial_system.add_client(protocon3, optional=True)

    pds = [
        serial_driver,
        serial_virt_tx,
        serial_virt_rx,
        pc_bm_server,
        pc_bm_monitor,
    ]
    for pd in pds:
        sdf.add_pd(pd)

    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)

    with open(f"{output_dir}/{sdf_path}", "w+") as f:
        f.write(sdf.render())

    assert sdf.generate_svc(output_dir)

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
