import sys
import struct
import json


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.off = 0

    def read(self, n: int) -> bytes:
        if self.off + n > len(self.data):
            raise ValueError(f"read out of range: off={self.off}, need={n}, len={len(self.data)}")
        b = self.data[self.off:self.off + n]
        self.off += n
        return b

    def align(self, n: int):
        new_off = (self.off + (n - 1)) & ~(n - 1)
        if new_off > len(self.data):
            raise ValueError(f"align out of range: off={self.off}, align={n}, len={len(self.data)}")
        self.off = new_off

    def u8(self) -> int:
        return struct.unpack_from("<B", self.read(1))[0]

    def bool(self) -> bool:
        return self.u8() != 0

    def usize(self) -> int:
        return struct.unpack_from("<Q", self.read(8))[0]


def read_c_string(buf: bytes) -> str:
    n = buf.find(b"\x00")
    if n == -1:
        n = len(buf)
    return buf[:n].decode("utf-8", errors="replace")


def parse_svc_mapping(r: Reader):
    return {
        "vaddr": r.usize(),
        "page_num": r.usize(),
        "page_size": r.usize(),
    }


def parse_protocon_svc(r: Reader):
    start = r.off

    svc_init = r.bool()
    svc_idx = r.u8()
    svc_type = r.u8()
    channels = list(r.read(4))
    irqs = list(r.read(4))

    # align to 8 before mappings
    r.align(8)

    mappings = [parse_svc_mapping(r) for _ in range(4)]
    data_path_raw = r.read(64)

    obj = {
        "svc_init": svc_init,
        "svc_idx": svc_idx,
        "svc_type": svc_type,
        "channels": channels,
        "irqs": irqs,
        "mappings": mappings,
        "data_path": read_c_string(data_path_raw),
    }

    end = r.off
    size = end - start
    if size != 176:
        raise ValueError(f"ProtoConSvc parsed size mismatch: got {size}, expected 176")

    return obj


def parse_protocon_svcdb(r: Reader):
    start = r.off

    pd_idx = r.u8()
    svc_num = r.u8()

    # align to 8 before array
    r.align(8)

    array = [parse_protocon_svc(r) for _ in range(16)]

    obj = {
        "pd_idx": pd_idx,
        "svc_num": svc_num,
        "array": array,
    }

    end = r.off
    size = end - start
    if size != 2824:
        raise ValueError(f"ProtoConSvcDb parsed size mismatch: got {size}, expected 2824")

    return obj


def parse_svcdb(data: bytes):
    if len(data) != 45192:
        print(f"warning: file size is {len(data)}, expected 45192", file=sys.stderr)

    r = Reader(data)

    obj = {
        "len": r.usize(),
        "list": [parse_protocon_svcdb(r) for _ in range(16)],
    }

    if r.off != len(data):
        print(f"warning: not all bytes consumed: consumed={r.off}, total={len(data)}", file=sys.stderr)

    return obj


def trim_empty_entries(obj):
    db_len = obj["len"]
    if db_len <= len(obj["list"]):
        obj["list"] = obj["list"][:db_len]

    for entry in obj["list"]:
        svc_num = entry["svc_num"]
        if svc_num <= len(entry["array"]):
            entry["array"] = entry["array"][:svc_num]

    return obj


def main():
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} <file.svc> [--full]")
        sys.exit(1)

    path = sys.argv[1]
    show_full = len(sys.argv) >= 3 and sys.argv[2] == "--full"

    with open(path, "rb") as f:
        data = f.read()

    obj = parse_svcdb(data)

    if not show_full:
        obj = trim_empty_entries(obj)

    print(json.dumps(obj, indent=2))


if __name__ == "__main__":
    main()