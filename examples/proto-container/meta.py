# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import os
import sys
import shutil
import subprocess
import argparse
import struct
import importlib.util
from pathlib import Path
from random import randint
from dataclasses import dataclass
from typing import List, Tuple
from sdfgen import SystemDescription, Sddf, DeviceTree, LionsOs
from importlib.metadata import version

sys.path.append(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../tools/meta")
)
from board import BOARDS

assert (
    version("sdfgen").split(".")[1] == "29" or version("sdfgen").split(".")[1] == "33"
), "Unexpected sdfgen version"

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel


def load_vm_layout(path: str, module_name: str) -> dict[str, dict[str, int]]:
    layout_path = Path(path).resolve()
    if not layout_path.is_file():
        raise FileNotFoundError(f"VM layout does not exist: {layout_path}")

    spec = importlib.util.spec_from_file_location(module_name, layout_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load VM layout: {layout_path}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    regions = getattr(module, "VM_REGIONS", None)
    if not isinstance(regions, list):
        raise TypeError("VM layout must define VM_REGIONS as a list")

    result: dict[str, dict[str, int]] = {}
    for index, region in enumerate(regions):
        if not isinstance(region, dict):
            raise TypeError(f"VM_REGIONS[{index}] must be a dictionary")
        try:
            name = region["name"]
            base = region["base"]
            size = region["size"]
        except KeyError as error:
            raise ValueError(
                f"VM_REGIONS[{index}] is missing {error.args[0]!r}"
            ) from error
        if not isinstance(name, str) or not isinstance(base, int) or not isinstance(size, int):
            raise TypeError(
                f"VM_REGIONS[{index}] must contain string name and integer base/size"
            )
        if name in result:
            raise ValueError(f"Duplicate VM region: {name}")
        result[name] = {"base": base, "size": size}

    return result


def vm_region(name: str) -> dict[str, int]:
    try:
        return vm_layout[name]
    except KeyError as error:
        raise KeyError(f"Required container VM region is missing: {name}") from error


def monitor_vm_region(name: str) -> dict[str, int]:
    try:
        return monitor_vm_layout[name]
    except KeyError as error:
        raise KeyError(f"Required monitor VM region is missing: {name}") from error


def monitor_region_base(name: str, cid: int) -> int:
    region = monitor_vm_region(name)
    return region["base"] + cid * region["size"]


def connect_protocon_with_monitor(
    monitor: SystemDescription.ProtectionDomain,
    pc: SystemDescription.ProtectionDomain,
    cid: int,
):
    name_prefix = monitor.name + "/" + pc.name + "/"

    container_elf = MemoryRegion(sdf, name_prefix + "container/elf", vm_region("CONTAINER_IMAGE")["size"])
    trampoline_elf = MemoryRegion(sdf, name_prefix + "trampoline/elf", vm_region("TRAMPOLINE_IMAGE")["size"])
    trampoline_exec = MemoryRegion(sdf, name_prefix + "trampoline/exec", vm_region("TRAMPOLINE_PROGRAM")["size"])
    tsldr_exec = MemoryRegion(sdf, name_prefix + "tsldr/exec", vm_region("LOADER_PROGRAM")["size"])
    tsldr_data = MemoryRegion(sdf, name_prefix + "tsldr/data", vm_region("LOADER_METADATA")["size"])
    ossvc_data = MemoryRegion(sdf, name_prefix + "ossvc/data", vm_region("OSSVC_METADATA")["size"])
    tsldr_context = MemoryRegion(sdf, name_prefix + "tsldr/context", vm_region("LOADER_CONTEXT")["size"])
    trampoline_args = MemoryRegion(sdf, name_prefix + "tsldr/trampoline/args", vm_region("TRAMPOLINE_ARGS")["size"])

    sdf.add_mr(container_elf)
    sdf.add_mr(trampoline_elf)
    sdf.add_mr(trampoline_exec)
    sdf.add_mr(tsldr_exec)
    sdf.add_mr(tsldr_data)
    sdf.add_mr(ossvc_data)
    sdf.add_mr(tsldr_context)
    sdf.add_mr(trampoline_args)

    monitor.add_map(Map(tsldr_context, monitor_region_base("LOADER_CONTEXT", cid), perms="rw", cached="true"))
    monitor.add_map(Map(ossvc_data, monitor_region_base("OSSVC_METADATA", cid), perms="rw", cached="true"))
    monitor.add_map(Map(tsldr_data, monitor_region_base("LOADER_METADATA", cid), perms="rw", cached="true"))
    monitor.add_map(Map(tsldr_exec, monitor_region_base("LOADER_PROGRAM", cid), perms="rw", cached="true"))
    monitor.add_map(Map(trampoline_elf, monitor_region_base("TRAMPOLINE_IMAGE", cid), perms="rw", cached="true"))
    monitor.add_map(Map(container_elf, monitor_region_base("CONTAINER_IMAGE", cid), perms="rw", cached="true"))

    pc.add_map(Map(tsldr_exec, vm_region("LOADER_PROGRAM")["base"], perms="rwx", cached="true"))
    pc.add_map(Map(tsldr_data, vm_region("LOADER_METADATA")["base"], perms="rw", cached="true"))
    pc.add_map(Map(ossvc_data, vm_region("OSSVC_METADATA")["base"], perms="rw", cached="true"))
    pc.add_map(Map(trampoline_args, vm_region("TRAMPOLINE_ARGS")["base"], perms="rw", cached="true"))
    pc.add_map(Map(tsldr_context, vm_region("LOADER_CONTEXT")["base"], perms="rw", cached="true"))
    pc.add_map(Map(trampoline_elf, vm_region("TRAMPOLINE_IMAGE")["base"], perms="rwx", cached="true"))
    pc.add_map(Map(trampoline_exec, vm_region("TRAMPOLINE_PROGRAM")["base"], perms="rwx", cached="true"))
    pc.add_map(Map(container_elf, vm_region("CONTAINER_IMAGE")["base"], perms="rw", cached="true"))

    trampoline_stack = MemoryRegion(sdf, name_prefix + "trampoline/stack", vm_region("TRAMPOLINE_STACK")["size"])
    container_stack = MemoryRegion(sdf, name_prefix + "container/stack", vm_region("CONTAINER_STACK")["size"])
    container_exec = MemoryRegion(sdf, name_prefix + "container/exec", vm_region("CONTAINER_PROGRAM")["size"])

    sdf.add_mr(trampoline_stack)
    sdf.add_mr(container_stack)
    sdf.add_mr(container_exec)

    pc.add_map(Map(trampoline_stack, vm_region("TRAMPOLINE_STACK")["base"], perms="rw", cached="true"))
    pc.add_map(Map(container_stack, vm_region("CONTAINER_STACK")["base"], perms="rw", cached="true"))
    pc.add_map(Map(container_exec, vm_region("CONTAINER_PROGRAM")["base"], perms="rwx", cached="true"))

    sdf.add_channel(Channel(a=monitor, b=pc, a_id=(24 + cid), b_id=15, pp_b=True))
    sdf.add_channel(Channel(a=monitor, b=pc, a_id=(40 + cid), b_id=16))

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

    pc.add_map(Map(uk_boot_stack, 0xff008000, perms="rw", cached="true"))
    pc.add_map(Map(uk_boot_heap, 0xff018000, perms="rw", cached="true"))

def connect_orchestrator_with_monitor(
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

    sdf.add_channel(Channel(a=monitor, b=orchestrator, a_id=23, b_id=1, pp_b=True))
    sdf.add_channel(Channel(a=monitor, b=orchestrator, a_id=15, b_id=30))


# Adds ".elf" to elf strings
def copy_elf(source_elf: str, new_elf: str, elf_number=None):
    source_elf += ".elf"
    if elf_number != None:
        new_elf += str(elf_number)
    new_elf += ".elf"
    assert os.path.isfile(source_elf)
    return shutil.copyfile(source_elf, new_elf)


# Assumes elf string has ".elf" suffix, and ".data" to data string
def update_elf_section(
    elf_name: str, section_name: str, data_name: str, data_number=None
):
    assert os.path.isfile(elf_name)
    if data_number != None:
        data_name += str(data_number)
    data_name += ".data"
    assert os.path.isfile(data_name)
    assert (
        subprocess.run(
            [
                obj_copy,
                "--update-section",
                "." + section_name + "=" + data_name,
                elf_name,
            ]
        ).returncode
        == 0
    )


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
    serial_virt_tx = ProtectionDomain(
        "serial_virt_tx", "serial_virt_tx.elf", priority=99
    )
    serial_virt_rx = ProtectionDomain(
        "serial_virt_rx", "serial_virt_rx.elf", priority=99
    )
    serial_system = Sddf.Serial(
        sdf, serial_node, serial_driver, serial_virt_tx, virt_rx=serial_virt_rx
    )

    blk_driver = ProtectionDomain("blk_driver", "blk_driver.elf", priority=200)
    blk_virt = ProtectionDomain(
        "blk_virt", "blk_virt.elf", priority=199, stack_size=0x2000
    )
    blk_system = Sddf.Blk(sdf, blk_node, blk_driver, blk_virt)

    pd_orchestrator = ProtectionDomain(
        "orchestrator", "orchestrator.elf", priority=60, stack_size=0x10000
    )
    # noted that the 'is_monitor' feature is enabled for container monitor, which needs sdfgen support
    pd_monitor = ProtectionDomain(
        "container_monitor",
        "monitor.elf",
        priority=64,
        stack_size=0x10000,
        is_monitor=True,
    )

    connect_orchestrator_with_monitor(pd_monitor, pd_orchestrator)

    serial_system.add_client(pd_orchestrator)
    serial_system.add_client(pd_monitor)

    if board.name == "maaxboard":
        timer_system.add_client(blk_driver)

    protocon0 = ProtectionDomain("protocon0", priority=53)
    protocon1 = ProtectionDomain("protocon1", priority=53)
    protocon2 = ProtectionDomain("protocon2", priority=53)
    protocon3 = ProtectionDomain("protocon3", priority=53)

    pd_monitor.add_child_pd(protocon0, child_id=0)
    pd_monitor.add_child_pd(protocon1, child_id=1)
    pd_monitor.add_child_pd(protocon2, child_id=2)
    pd_monitor.add_child_pd(protocon3, child_id=3)

    connect_protocon_with_monitor(pd_monitor, protocon0, 0)
    connect_protocon_with_monitor(pd_monitor, protocon1, 1)
    connect_protocon_with_monitor(pd_monitor, protocon2, 2)
    connect_protocon_with_monitor(pd_monitor, protocon3, 3)

    pd_fs_orchestrator = ProtectionDomain("orchestrator_fs", "orchestrator_fs.elf", priority=96)
    pd_fs_monitor = ProtectionDomain("monitor_fs", "monitor_fs.elf", priority=96)
    pd_fs_sp0 = ProtectionDomain("protocon0_fs", "protocon0_fs.elf", priority=96)
    pd_fs_sp1 = ProtectionDomain("protocon1_fs", "protocon1_fs.elf", priority=96)

    orchestrator_fs = LionsOs.FileSystem.Fat(
        sdf, pd_fs_orchestrator, pd_orchestrator, blk=blk_system, partition=0
    )
    monitor_fs = LionsOs.FileSystem.Fat(
        sdf, pd_fs_monitor, pd_monitor, blk=blk_system, partition=1
    )
    protocon0_fs = LionsOs.FileSystem.Fat(
        sdf, pd_fs_sp0, protocon0, blk=blk_system, partition=2
    )
    protocon1_fs = LionsOs.FileSystem.Fat(
        sdf, pd_fs_sp1, protocon1, blk=blk_system, partition=3
    )

    serial_system.add_client(protocon0, optional=True)
    serial_system.add_client(protocon1, optional=True)
    serial_system.add_client(protocon2, optional=True)
    serial_system.add_client(protocon3, optional=True)

    timer_system.add_client(protocon0, optional=True)
    timer_system.add_client(protocon1, optional=True)
    timer_system.add_client(protocon2, optional=True)
    timer_system.add_client(protocon3, optional=True)

    pds = [
        serial_driver,
        serial_virt_tx,
        serial_virt_rx,
        pd_orchestrator,
        pd_fs_orchestrator,
        timer_driver,
        blk_driver,
        blk_virt,
        pd_monitor,
        pd_fs_monitor,
        pd_fs_sp0,
        pd_fs_sp1,
    ]
    for pd in pds:
        sdf.add_pd(pd)

    assert protocon0_fs.connect(optional=True)
    assert protocon0_fs.serialise_config(output_dir)
    assert protocon1_fs.connect(optional=True)
    assert protocon1_fs.serialise_config(output_dir)
    assert orchestrator_fs.connect()
    assert orchestrator_fs.serialise_config(output_dir)
    assert monitor_fs.connect()
    assert monitor_fs.serialise_config(output_dir)
    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)
    assert blk_system.connect()
    assert blk_system.serialise_config(output_dir)

    copy_elf("fat", "orchestrator_fs", None)
    copy_elf("fat", "monitor_fs", None)
    copy_elf("fat", "protocon0_fs", None)
    copy_elf("fat", "protocon1_fs", None)

    update_elf_section(
        "orchestrator_fs.elf", "blk_client_config", "blk_client_orchestrator_fs"
    )
    update_elf_section(
        "orchestrator_fs.elf", "fs_server_config", "fs_server_orchestrator_fs"
    )

    update_elf_section(
        "monitor_fs.elf", "blk_client_config", "blk_client_monitor_fs"
    )
    update_elf_section(
        "monitor_fs.elf", "fs_server_config", "fs_server_monitor_fs"
    )

    update_elf_section(
        "protocon0_fs.elf", "blk_client_config", "blk_client_protocon0_fs"
    )
    update_elf_section(
        "protocon0_fs.elf", "fs_server_config", "fs_server_protocon0_fs"
    )

    update_elf_section(
        "protocon1_fs.elf", "blk_client_config", "blk_client_protocon1_fs"
    )
    update_elf_section(
        "protocon1_fs.elf", "fs_server_config", "fs_server_protocon1_fs"
    )

    with open(f"{output_dir}/{sdf_path}", "w+") as f:
        f.write(sdf.render())

    assert sdf.generate_svc(output_dir)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--dtb", required=True)
    parser.add_argument("--sddf", required=True)
    parser.add_argument("--board", required=True, choices=[b.name for b in BOARDS])
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument(
        "--vm-layout",
        required=True,
        help="path to libtrustedlo config/vm_layout.py",
    )
    parser.add_argument(
        "--monitor-vm-layout",
        required=True,
        help="path to monitor config/vm_layout.py",
    )

    args = parser.parse_args()

    global vm_layout
    vm_layout = load_vm_layout(args.vm_layout, "libtrustedlo_vm_layout")

    global monitor_vm_layout
    monitor_vm_layout = load_vm_layout(
        args.monitor_vm_layout, "monitor_vm_layout"
    )

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)

    global obj_copy
    obj_copy = args.objcopy

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    generate(args.sdf, args.output, dtb)
