#!/usr/bin/env python3

from __future__ import annotations

import argparse
import importlib.util
import re
from pathlib import Path
from types import ModuleType
from typing import Any


UINT64_MAX = (1 << 64) - 1
VALID_REGION_NAME = re.compile(r"^[A-Z][A-Z0-9_]*$")


def load_config(config_path: Path) -> ModuleType:
    config_path = config_path.resolve()

    if not config_path.is_file():
        raise FileNotFoundError(
            f"monitor VM layout config does not exist: {config_path}"
        )

    spec = importlib.util.spec_from_file_location(
        "monitor_vm_layout_config",
        config_path,
    )

    if spec is None or spec.loader is None:
        raise RuntimeError(
            f"cannot load monitor VM layout config: {config_path}"
        )

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_config(
    config_module: ModuleType,
) -> tuple[int, list[dict[str, Any]]]:
    if not hasattr(config_module, "PAGE_SIZE"):
        raise ValueError("config must define PAGE_SIZE")

    if not hasattr(config_module, "VM_REGIONS"):
        raise ValueError("config must define VM_REGIONS")

    page_size = config_module.PAGE_SIZE
    vm_regions = config_module.VM_REGIONS

    if not isinstance(page_size, int):
        raise TypeError("PAGE_SIZE must be an integer")

    if page_size <= 0:
        raise ValueError("PAGE_SIZE must be positive")

    if page_size & (page_size - 1):
        raise ValueError("PAGE_SIZE must be a power of two")

    if not isinstance(vm_regions, list):
        raise TypeError("VM_REGIONS must be a list")

    return page_size, vm_regions


def validate_regions(
    page_size: int,
    vm_regions: list[dict[str, Any]],
) -> None:
    names: set[str] = set()

    for index, region in enumerate(vm_regions):
        if not isinstance(region, dict):
            raise TypeError(
                f"VM_REGIONS[{index}] must be a dictionary"
            )

        required_keys = {"name", "base", "size"}
        missing_keys = required_keys - region.keys()
        if missing_keys:
            missing = ", ".join(sorted(missing_keys))
            raise ValueError(
                f"VM_REGIONS[{index}] is missing: {missing}"
            )

        unexpected_keys = region.keys() - required_keys
        if unexpected_keys:
            unexpected = ", ".join(sorted(unexpected_keys))
            raise ValueError(
                f"VM_REGIONS[{index}] has unexpected keys: {unexpected}"
            )

        name = region["name"]
        base = region["base"]
        size = region["size"]

        if not isinstance(name, str):
            raise TypeError(
                f"VM_REGIONS[{index}].name must be a string"
            )

        if not VALID_REGION_NAME.fullmatch(name):
            raise ValueError(
                f"{name!r}: region name must match "
                "[A-Z][A-Z0-9_]*"
            )

        if name in names:
            raise ValueError(f"duplicate region name: {name}")
        names.add(name)

        if not isinstance(base, int):
            raise TypeError(f"{name}: base must be an integer")

        if not isinstance(size, int):
            raise TypeError(f"{name}: size must be an integer")

        if base < 0:
            raise ValueError(f"{name}: base must be non-negative")

        if size <= 0:
            raise ValueError(f"{name}: size must be positive")

        if base > UINT64_MAX:
            raise ValueError(f"{name}: base exceeds uint64_t")

        if size > UINT64_MAX:
            raise ValueError(f"{name}: size exceeds uint64_t")

        if base % page_size != 0:
            raise ValueError(
                f"{name}: base 0x{base:x} is not page aligned"
            )

        if size % page_size != 0:
            raise ValueError(
                f"{name}: size 0x{size:x} is not page aligned"
            )

        if base + size > UINT64_MAX:
            raise ValueError(
                f"{name}: address range overflows uint64_t"
            )

    sorted_regions = sorted(
        vm_regions,
        key=lambda region: region["base"],
    )

    for previous, current in zip(
        sorted_regions,
        sorted_regions[1:],
    ):
        previous_start = previous["base"]
        previous_end = previous_start + previous["size"]
        current_start = current["base"]
        current_end = current_start + current["size"]

        if previous_end > current_start:
            raise ValueError(
                f"overlap: {previous['name']} "
                f"[0x{previous_start:x}, 0x{previous_end:x}) and "
                f"{current['name']} "
                f"[0x{current_start:x}, 0x{current_end:x})"
            )


def generate_header(
    page_size: int,
    vm_regions: list[dict[str, Any]],
    config_path: Path,
) -> str:
    lines = [
        "/*",
        " * Auto-generated file. Do not edit manually.",
        f" * Source configuration: {config_path.name}",
        " */",
        "",
        "#ifndef MONITOR_VM_LAYOUT_H",
        "#define MONITOR_VM_LAYOUT_H",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "#define MONITOR_VM_PAGE_SIZE \\",
        f"    ((size_t)UINT64_C(0x{page_size:x}))",
        "",
        "typedef struct monitor_vm_region {",
        "    uintptr_t base;",
        "    size_t size;",
        "} monitor_vm_region_t;",
        "",
    ]

    for region in vm_regions:
        name = region["name"]
        base = region["base"]
        size = region["size"]
        end = base + size

        lines.extend(
            [
                f"#define MONITOR_VM_{name}_BASE \\",
                f"    ((uintptr_t)UINT64_C(0x{base:x}))",
                "",
                f"#define MONITOR_VM_{name}_SIZE \\",
                f"    ((size_t)UINT64_C(0x{size:x}))",
                "",
                f"#define MONITOR_VM_{name}_END \\",
                f"    ((uintptr_t)UINT64_C(0x{end:x}))",
                "",
            ]
        )

    lines.append("typedef struct monitor_vm_layout {")

    for region in vm_regions:
        field_name = region["name"].lower()
        lines.append(f"    monitor_vm_region_t {field_name};")

    lines.extend(
        [
            "} monitor_vm_layout_t;",
            "",
            "static const monitor_vm_layout_t monitor_vm_layout = {",
        ]
    )

    for region in vm_regions:
        name = region["name"]
        field_name = name.lower()

        lines.extend(
            [
                f"    .{field_name} = {{",
                f"        .base = MONITOR_VM_{name}_BASE,",
                f"        .size = MONITOR_VM_{name}_SIZE,",
                "    },",
            ]
        )

    lines.extend(
        [
            "};",
            "",
            "static inline uintptr_t monitor_vm_region_base(",
            "    const monitor_vm_region_t *region,",
            "    size_t cid",
            ")",
            "{",
            "    return region->base + cid * region->size;",
            "}",
            "",
            "#endif /* MONITOR_VM_LAYOUT_H */",
            "",
        ]
    )

    return "\n".join(lines)


def write_if_changed(output_path: Path, content: str) -> None:
    if output_path.exists():
        old_content = output_path.read_text(encoding="utf-8")
        if old_content == content:
            return

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(content, encoding="utf-8")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate the monitor VM layout C header."
    )

    parser.add_argument(
        "--config",
        type=Path,
        required=True,
        help="path to monitor_vm_layout.py",
    )

    parser.add_argument(
        "--header-output",
        "--output",
        dest="header_output",
        type=Path,
        required=True,
        help="path to the generated monitor_vm_layout.h",
    )

    return parser.parse_args()


def main() -> None:
    args = parse_arguments()

    config_module = load_config(args.config)
    page_size, vm_regions = read_config(config_module)
    validate_regions(page_size, vm_regions)

    header = generate_header(
        page_size=page_size,
        vm_regions=vm_regions,
        config_path=args.config,
    )

    write_if_changed(args.header_output, header)


if __name__ == "__main__":
    main()
