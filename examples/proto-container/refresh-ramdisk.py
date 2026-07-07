#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys

COPY_TABLE = [
    ("trampoline.elf", 1),
    ("protocon.elf", 1),
    ("client_timeout.elf", 1),
    ("client_faulting.elf", 1),
    ("client_echo.elf", 1),
    ("client_looping.elf", 1),
    ("unikraft.elf", 1),
    ("serial_client_protocon0.data", 2),
    ("serial_client_protocon1.data", 2),
    ("serial_client_protocon2.data", 2),
    ("serial_client_protocon3.data", 2),
    ("timer_client_protocon0.data", 2),
    ("timer_client_protocon1.data", 2),
    ("timer_client_protocon2.data", 2),
    ("timer_client_protocon3.data", 2),
]


def main() -> int:
    script_dir = Path(__file__).resolve().parent
    copy_script = script_dir / "copy2ramdisk.sh"

    if not copy_script.is_file():
        print(f"Error: cannot find {copy_script}", file=sys.stderr)
        return 1

    for file_path, partition in COPY_TABLE:
        source = Path(file_path)

        if not source.is_file():
            print(f"Error: source file does not exist: {source}", file=sys.stderr)
            return 1

        print(f"Copying {source} to partition {partition}")

        try:
            subprocess.run(
                [str(copy_script), str(source), str(partition)],
                check=True,
            )
        except subprocess.CalledProcessError as error:
            print(
                f"Error: failed to copy {source} "
                f"to partition {partition}, exit code {error.returncode}",
                file=sys.stderr,
            )
            return error.returncode

    print("All files copied successfully.")
    subprocess.run(
        [str(script_dir / "listramdisk.sh")],
        check=True,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())