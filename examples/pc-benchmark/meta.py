# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import struct
from random import randint
from dataclasses import dataclass
from typing import List, Tuple, Optional
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from importlib.metadata import version
from board import BOARDS

assert (
    version("sdfgen").split(".")[1] == "29" or version("sdfgen").split(".")[1] == "33"
), "Unexpected sdfgen version"

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel

def connect_protocon_with_monitor(
    monitor: SystemDescription.ProtectionDomain,
    pc: SystemDescription.ProtectionDomain,
    cid: int,
):
    name_prefix = monitor.name + "/" + pc.name + "/"

    container_elf = MemoryRegion(sdf, name_prefix + "container/elf", 0x800000)
    trampoline_elf = MemoryRegion(sdf, name_prefix + "trampoline/elf", 0x800000)
    trampoline_exec = MemoryRegion(sdf, name_prefix + "trampoline/exec", 0x800000)
    tsldr_exec = MemoryRegion(sdf, name_prefix + "tsldr/exec", 0x800000)
    tsldr_data = MemoryRegion(sdf, name_prefix + "tsldr/data", 0x1000)
    ossvc_data = MemoryRegion(sdf, name_prefix + "ossvc/data", 0x1000)
    tsldr_context = MemoryRegion(sdf, name_prefix + "tsldr/context", 0x1000)
    trampoline_args = MemoryRegion(sdf, name_prefix + "tsldr/trampoline/args", 0x1000)

    sdf.add_mr(container_elf)
    sdf.add_mr(trampoline_elf)
    sdf.add_mr(trampoline_exec)
    sdf.add_mr(tsldr_exec)
    sdf.add_mr(tsldr_data)
    sdf.add_mr(ossvc_data)
    sdf.add_mr(tsldr_context)
    sdf.add_mr(trampoline_args)

    monitor.add_map(
        Map(tsldr_context, 0x0FF40000 + cid * 0x1000, perms="rw", cached="true")
    )
    monitor.add_map(Map(ossvc_data, 0x0FF80000 + cid * 0x1000, perms="rw", cached="true"))
    monitor.add_map(Map(tsldr_data, 0x0FFC0000 + cid * 0x1000, perms="rw", cached="true"))
    monitor.add_map(Map(tsldr_exec, 0x10000000 + cid * 0x800000, perms="rw", cached="true"))
    monitor.add_map(
        Map(trampoline_elf, 0x30000000 + cid * 0x800000, perms="rw", cached="true")
    )
    monitor.add_map(
        Map(container_elf, 0x50000000 + cid * 0x800000, perms="rw", cached="true")
    )

    pc.add_map(Map(tsldr_exec, 0x0200000, perms="rwx", cached="true"))
    pc.add_map(Map(tsldr_data, 0x0A00000, perms="rw", cached="true"))
    pc.add_map(Map(ossvc_data, 0x0A01000, perms="rw", cached="true"))
    pc.add_map(Map(trampoline_args, 0x0A02000, perms="rw", cached="true"))
    pc.add_map(Map(tsldr_context, 0x0E00000, perms="rw", cached="true"))
    pc.add_map(Map(trampoline_elf, 0x1000000, perms="rwx", cached="true"))
    pc.add_map(Map(trampoline_exec, 0x1800000, perms="rwx", cached="true"))
    pc.add_map(Map(container_elf, 0x2000000, perms="rw", cached="true"))

    trampoline_stack = MemoryRegion(sdf, name_prefix + "trampoline/stack", 0x1000)
    container_stack = MemoryRegion(sdf, name_prefix + "container/stack", 0x1000)
    container_exec = MemoryRegion(sdf, name_prefix + "container/exec", 0x2000000)

    sdf.add_mr(trampoline_stack)
    sdf.add_mr(container_stack)
    sdf.add_mr(container_exec)

    pc.add_map(Map(trampoline_stack, 0x00FFFDFF000, perms="rw", cached="true"))
    pc.add_map(Map(container_stack, 0x00FFFBFF000, perms="rw", cached="true"))
    pc.add_map(Map(container_exec, 0x2800000, perms="rwx", cached="true"))

    client_monitor_rx_free = MemoryRegion(sdf, name_prefix + "rx/free", 0x3000)
    client_monitor_tx_free = MemoryRegion(sdf, name_prefix + "tx/free", 0x3000)
    client_monitor_rx_active = MemoryRegion(sdf, name_prefix + "rx/active", 0x3000)
    client_monitor_tx_active = MemoryRegion(sdf, name_prefix + "tx/active", 0x3000)
    client_monitor_rx_data = MemoryRegion(sdf, name_prefix + "rx/data", 0x100000)
    client_monitor_tx_data = MemoryRegion(sdf, name_prefix + "tx/data", 0x100000)

    sdf.add_mr(client_monitor_rx_free)
    sdf.add_mr(client_monitor_rx_active)
    sdf.add_mr(client_monitor_rx_data)
    sdf.add_mr(client_monitor_tx_free)
    sdf.add_mr(client_monitor_tx_active)
    sdf.add_mr(client_monitor_tx_data)

    pc.add_map(Map(client_monitor_rx_free, 0x04800000, perms="rw", cached="false"))
    pc.add_map(Map(client_monitor_tx_free, 0x04803000, perms="rw", cached="false"))
    pc.add_map(Map(client_monitor_rx_active, 0x04806000, perms="rw", cached="false"))
    pc.add_map(Map(client_monitor_tx_active, 0x04809000, perms="rw", cached="false"))
    pc.add_map(Map(client_monitor_rx_data, 0x0480C000, perms="rw", cached="false"))
    pc.add_map(Map(client_monitor_tx_data, 0x0490C000, perms="rw", cached="false"))

    monitor_queue_base = 0x80000000 + cid * 0x400000
    # monitor RX uses client's TX regions
    monitor.add_map(
        Map(
            client_monitor_tx_free,
            monitor_queue_base + 0x000000,
            perms="rw",
            cached="false",
        )
    )
    monitor.add_map(
        Map(
            client_monitor_tx_active,
            monitor_queue_base + 0x006000,
            perms="rw",
            cached="false",
        )
    )
    monitor.add_map(
        Map(
            client_monitor_tx_data,
            monitor_queue_base + 0x00C000,
            perms="rw",
            cached="false",
        )
    )

    # monitor TX uses client's RX regions
    monitor.add_map(
        Map(
            client_monitor_rx_free,
            monitor_queue_base + 0x003000,
            perms="rw",
            cached="false",
        )
    )
    monitor.add_map(
        Map(
            client_monitor_rx_active,
            monitor_queue_base + 0x009000,
            perms="rw",
            cached="false",
        )
    )
    monitor.add_map(
        Map(
            client_monitor_rx_data,
            monitor_queue_base + 0x10C000,
            perms="rw",
            cached="false",
        )
    )

    uk_boot_stack = MemoryRegion(sdf, name_prefix + "uk_boot_stack", (0x1000 * (1 << 4)))
    uk_boot_heap = MemoryRegion(sdf, name_prefix + "uk_boot_heap", (0x1000 * (1 << 10)))

    sdf.add_mr(uk_boot_stack)
    sdf.add_mr(uk_boot_heap)

    pc.add_map(Map(uk_boot_stack, 0xffff008000, perms="rw", cached="true"))
    pc.add_map(Map(uk_boot_heap, 0xffff018000, perms="rw", cached="true"))

    sdf.add_channel(Channel(a=monitor, b=pc, a_id=(24+cid), b_id=15, pp_b=True))


def connect_frontend_with_monitor(
    monitor: SystemDescription.ProtectionDomain,
    orchestrator: SystemDescription.ProtectionDomain
):
    name_prefix = monitor.name + "/" + orchestrator.name + "/"

    ext_trampoline_elf = MemoryRegion(sdf, name_prefix + "trampoline", 0x800000)
    ext_protocon_elf = MemoryRegion(sdf, name_prefix + "protocon", 0x800000)
    ext_client_elf = MemoryRegion(sdf, name_prefix + "client", 0x800000)

    sdf.add_mr(ext_trampoline_elf)
    sdf.add_mr(ext_protocon_elf)
    sdf.add_mr(ext_client_elf)

    monitor.add_map(Map(ext_protocon_elf, 0x6000000, perms="rw", cached="true"))
    monitor.add_map(Map(ext_trampoline_elf, 0x6800000, perms="rw", cached="true"))
    monitor.add_map(Map(ext_client_elf, 0x7000000, perms="rw", cached="true"))

    orchestrator.add_map(Map(ext_trampoline_elf, 0x6000000, perms="rw", cached="true"))
    orchestrator.add_map(Map(ext_protocon_elf, 0x4000000, perms="rw", cached="true"))
    orchestrator.add_map(Map(ext_client_elf, 0xB000000, perms="rw", cached="true"))

    sdf.add_channel(Channel(a=monitor, b=orchestrator, a_id=50, b_id=1, pp_b=True))
    sdf.add_channel(Channel(a=monitor, b=orchestrator, a_id=15, b_id=30))


def generate(
    sdf_path: str,
    output_dir: str,
    dtb: Optional[DeviceTree],
):
    serial_node = None
    timer_node = None
    if dtb is not None:
        serial_node = dtb.node(board.serial)
        assert serial_node is not None
        timer_node = dtb.node(board.timer)
        assert timer_node is not None

    timer_driver = ProtectionDomain(
        "timer_driver", "timer_driver.elf", priority=101
    )
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)

    if board.arch == SystemDescription.Arch.X86_64:
        hpet_irq = SystemDescription.IrqMsi(
            pci_bus=0, pci_device=0, pci_func=0, vector=0, handle=0, id=0
        )
        timer_driver.add_irq(hpet_irq)

        hpet_regs = SystemDescription.MemoryRegion(
            sdf, "hpet_regs", 0x1000, paddr=0xFED00000
        )
        hpet_regs_map = SystemDescription.Map(
            hpet_regs, 0x5000_0000, "rw", cached=False
        )
        timer_driver.add_map(hpet_regs_map)
        sdf.add_mr(hpet_regs)

    serial_driver = ProtectionDomain("serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_virt_rx = ProtectionDomain("serial_virt_rx", "serial_virt_rx.elf", priority=99)
    serial_system = Sddf.Serial(sdf, serial_node, serial_driver, serial_virt_tx, virt_rx=serial_virt_rx)

    if board.arch == SystemDescription.Arch.X86_64:
        serial_port = SystemDescription.IoPort(0x3f8, 8, 0)
        serial_driver.add_ioport(serial_port)

    pc_bm_server = ProtectionDomain("bm_server", "bm_server.elf", priority=50, stack_size=0x10000)
    pc_bm_monitor = ProtectionDomain("bm_monitor", "bm_monitor.elf", priority=54, stack_size=0x10000, is_monitor=True)

    connect_frontend_with_monitor(pc_bm_monitor, pc_bm_server)

    serial_system.add_client(pc_bm_server)
    serial_system.add_client(pc_bm_monitor)

    timer_system.add_client(pc_bm_server)

    protocon0 = ProtectionDomain("protocon0", priority=53, stack_size=0x1000)
    protocon1 = ProtectionDomain("protocon1", priority=53, stack_size=0x1000)
    protocon2 = ProtectionDomain("protocon2", priority=53, stack_size=0x1000)
    protocon3 = ProtectionDomain("protocon3", priority=53, stack_size=0x1000)

    pc_bm_monitor.add_child_pd(protocon0, child_id=0)
    pc_bm_monitor.add_child_pd(protocon1, child_id=1)
    pc_bm_monitor.add_child_pd(protocon2, child_id=2)
    pc_bm_monitor.add_child_pd(protocon3, child_id=3)

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
        timer_driver,
    ]
    for pd in pds:
        sdf.add_pd(pd)

    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)
    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)

    with open(f"{output_dir}/{sdf_path}", "w+") as f:
        f.write(sdf.render())

    assert sdf.generate_svc(output_dir)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--dtb", required=False)
    parser.add_argument("--sddf", required=True)
    parser.add_argument("--board", required=True, choices=[b.name for b in BOARDS])
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)

    args = parser.parse_args()

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)

    dtb = None
    if board.arch != SystemDescription.Arch.X86_64:
        with open(args.dtb, "rb") as f:
            dtb = DeviceTree(f.read())

    generate(args.sdf, args.output, dtb)
