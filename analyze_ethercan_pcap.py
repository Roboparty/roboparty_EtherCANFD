#!/usr/bin/env python3
"""Streaming analyser for EtherCANFD gs_usb captures.

The supplied ``ethercan.pcap`` is a Linux usbmon capture (DLT 220), not an
Ethernet EtherCAT capture.  EtherCANFD uses the gs_usb host frame layout:

    echo_id:u32, can_id:u32, can_dlc:u8, channel:u8, flags:u8,
    reserved:u8, data[64]

Only the Python standard library is required.  The file is processed one
pcap record at a time, so the 700 MB sample does not need to fit in memory.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import struct
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Iterator, Optional


GS_CAN_FLAG_OVERFLOW = 0x01
GS_CAN_FLAG_FD = 0x02
GS_CAN_FLAG_BRS = 0x04
GS_CAN_FLAG_ESI = 0x08
CAN_EFF_FLAG = 0x80000000
CAN_RTR_FLAG = 0x40000000
CAN_ERR_FLAG = 0x20000000
DLC_LENGTH = (0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64)


@dataclass(slots=True)
class Frame:
    timestamp: float
    urb_type: str
    endpoint: int
    direction: str
    echo_id: int
    can_id_raw: int
    can_id: int
    frame_kind: str
    dlc: int
    data: bytes
    channel: int
    flags: int


def _pcap_header(f: BinaryIO) -> tuple[str, float, int]:
    h = f.read(24)
    if len(h) != 24:
        raise ValueError("文件不是有效的 pcap（全局头不足 24 字节）")
    magic = h[:4]
    if magic == b"\xd4\xc3\xb2\xa1":
        endian, resolution = "<", 1e-6
    elif magic == b"\xa1\xb2\xc3\xd4":
        endian, resolution = ">", 1e-6
    elif magic == b"M\xC3\xB2\xA1":
        endian, resolution = "<", 1e-9
    elif magic == b"\xa1\xb2\xc3M":
        endian, resolution = ">", 1e-9
    else:
        raise ValueError("仅支持 pcap（不支持 pcapng 或未知文件格式）")
    network = struct.unpack_from(endian + "I", h, 20)[0]
    return endian, resolution, network


def _frames_from_usb_payload(payload: bytes, timestamp: float, urb_type: str,
                             endpoint: int) -> Iterator[Frame]:
    """Decode one usbmon bulk payload; a URB may contain multiple frames."""
    direction = "RX" if endpoint & 0x80 else "TX"
    offset = 0
    while offset + 12 <= len(payload):
        echo_id, can_id_raw = struct.unpack_from("<II", payload, offset)
        dlc, channel, flags, _reserved = struct.unpack_from("<BBBB", payload, offset + 8)
        is_fd = bool(flags & GS_CAN_FLAG_FD)
        stride = 76 if is_fd else 20
        if offset + stride > len(payload):
            break
        if dlc >= len(DLC_LENGTH):
            # Invalid DLC cannot identify a reliable frame boundary.
            break
        data_len = DLC_LENGTH[dlc] if is_fd else min(dlc, 8)
        data = payload[offset + 12:offset + 12 + data_len]
        if can_id_raw & CAN_ERR_FLAG:
            frame_kind = "ERROR"
            can_id = can_id_raw & 0x1FFFFFFF
        elif can_id_raw & CAN_EFF_FLAG:
            frame_kind = "EXTENDED"
            can_id = can_id_raw & 0x1FFFFFFF
        else:
            frame_kind = "RTR" if can_id_raw & CAN_RTR_FLAG else "STANDARD"
            can_id = can_id_raw & 0x7FF
        yield Frame(timestamp, urb_type, endpoint, direction, echo_id, can_id_raw,
                    can_id, frame_kind, dlc, data, channel, flags)
        offset += stride


def iter_frames(path: Path, *, max_frames: Optional[int] = None) -> Iterator[Frame]:
    with path.open("rb") as f:
        endian, resolution, network = _pcap_header(f)
        # Linux usbmon cooked link type is 220.  Keep parsing permissive because
        # some capture tools write the same 64-byte records with a generic type.
        if network not in (220, 189, 0):
            print(f"警告：pcap 链路类型为 {network}，按 usbmon 64 字节头尝试解析", file=sys.stderr)
        count = 0
        while True:
            ph = f.read(16)
            if not ph:
                return
            if len(ph) != 16:
                raise ValueError("pcap 数据包头不完整")
            sec, frac, incl_len, _orig_len = struct.unpack(endian + "IIII", ph)
            packet = f.read(incl_len)
            if len(packet) != incl_len:
                raise ValueError("pcap 数据包内容不完整")
            # usbmon header is 64 bytes. xfer type and endpoint are at offsets
            # 9 and 10 respectively (see Linux Documentation/usb/usbmon.txt).
            if len(packet) < 64 or packet[9] != 3:  # bulk transfer only
                continue
            payload = packet[64:]
            if not payload:
                continue
            timestamp = sec + frac * resolution
            for frame in _frames_from_usb_payload(payload, timestamp,
                                                  chr(packet[8]), packet[10]):
                yield frame
                count += 1
                if max_frames is not None and count >= max_frames:
                    return


def analyse(path: Path, csv_path: Optional[Path] = None,
            max_frames: Optional[int] = None) -> dict:
    counters = {
        "frames": 0, "tx": 0, "rx": 0, "error": 0,
        "fd": 0, "brs": 0, "overflow": 0, "esi": 0,
    }
    channels: Counter[int] = Counter()
    ids: Counter[tuple[str, int]] = Counter()
    flag_counts: Counter[int] = Counter()
    dlcs: Counter[int] = Counter()
    first = last = None
    data_bytes = 0
    gaps: list[float] = []
    writer = None
    out_file = None
    if csv_path:
        out_file = csv_path.open("w", newline="", encoding="utf-8")
        writer = csv.writer(out_file)
        writer.writerow(("timestamp", "direction", "channel", "can_id", "kind",
                         "echo_id", "dlc", "length", "fd", "brs", "flags", "data"))
    try:
        for frame in iter_frames(path, max_frames=max_frames):
            counters["frames"] += 1
            counters[frame.direction.lower()] += 1
            if frame.frame_kind == "ERROR":
                counters["error"] += 1
            if frame.flags & GS_CAN_FLAG_FD:
                counters["fd"] += 1
            if frame.flags & GS_CAN_FLAG_BRS:
                counters["brs"] += 1
            if frame.flags & GS_CAN_FLAG_OVERFLOW:
                counters["overflow"] += 1
            if frame.flags & GS_CAN_FLAG_ESI:
                counters["esi"] += 1
            channels[frame.channel] += 1
            ids[(frame.frame_kind, frame.can_id)] += 1
            flag_counts[frame.flags] += 1
            dlcs[frame.dlc] += 1
            data_bytes += len(frame.data)
            if first is None:
                first = frame.timestamp
            elif last is not None:
                gaps.append(frame.timestamp - last)
            last = frame.timestamp
            if writer:
                writer.writerow((f"{frame.timestamp:.6f}", frame.direction, frame.channel,
                                 f"0x{frame.can_id:X}", frame.frame_kind,
                                 f"0x{frame.echo_id:X}", frame.dlc, len(frame.data),
                                 int(bool(frame.flags & GS_CAN_FLAG_FD)),
                                 int(bool(frame.flags & GS_CAN_FLAG_BRS)),
                                 f"0x{frame.flags:02X}", frame.data.hex()))
    finally:
        if out_file:
            out_file.close()
    duration = (last - first) if first is not None and last is not None else 0.0
    result = {
        "input": str(path),
        "frames": counters["frames"],
        "tx_frames": counters["tx"], "rx_frames": counters["rx"],
        "error_frames": counters["error"], "fd_frames": counters["fd"],
        "brs_frames": counters["brs"], "overflow_flag_frames": counters["overflow"],
        "esi_flag_frames": counters["esi"], "data_bytes": data_bytes,
        "first_timestamp": first, "last_timestamp": last,
        "duration_s": duration,
        "frames_per_second": counters["frames"] / duration if duration > 0 else 0.0,
        "payload_bytes_per_second": data_bytes / duration if duration > 0 else 0.0,
        "channels": dict(sorted(channels.items())),
        "dlc_counts": dict(sorted(dlcs.items())),
        "flag_counts": {f"0x{k:02X}": v for k, v in sorted(flag_counts.items())},
        "top_ids": [
            {"kind": kind, "can_id": f"0x{can_id:X}", "count": n}
            for (kind, can_id), n in ids.most_common()
        ],
    }
    if gaps:
        result["inter_frame_gap_us"] = {
            "min": min(gaps) * 1e6, "mean": sum(gaps) / len(gaps) * 1e6,
            "p99": sorted(gaps)[min(len(gaps) - 1, math.floor(len(gaps) * 0.99))] * 1e6,
            "max": max(gaps) * 1e6,
        }
    return result


def main() -> int:
    # Windows consoles are often cp1252/cp936 while the report contains
    # Chinese labels.  UTF-8 keeps normal CLI use from failing with a
    # UnicodeEncodeError; callers can still redirect output normally.
    if hasattr(sys.stdout, "reconfigure"):
        try:
            sys.stdout.reconfigure(encoding="utf-8")
            sys.stderr.reconfigure(encoding="utf-8")
        except (OSError, ValueError):
            pass
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("pcap", type=Path, help="usbmon pcap 文件")
    ap.add_argument("--csv", type=Path, help="导出逐帧 CSV（大文件会很大）")
    ap.add_argument("--json", action="store_true", help="以 JSON 输出完整统计")
    ap.add_argument("--top", type=int, default=20, help="显示前 N 个 CAN ID（默认 20）")
    ap.add_argument("--max-frames", type=int, help="只解析前 N 帧，用于快速抽样")
    args = ap.parse_args()
    try:
        result = analyse(args.pcap, args.csv, args.max_frames)
    except (OSError, ValueError) as exc:
        ap.error(str(exc))
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0
    print(f"文件: {result['input']}")
    print(f"帧数: {result['frames']:,}  TX: {result['tx_frames']:,}  RX: {result['rx_frames']:,}  错误: {result['error_frames']:,}")
    print(f"FD: {result['fd_frames']:,}  BRS: {result['brs_frames']:,}  时长: {result['duration_s']:.6f} s")
    print(f"速率: {result['frames_per_second']:.2f} 帧/s, payload {result['payload_bytes_per_second']:.2f} B/s")
    print("通道:", ", ".join(f"ch{k}={v:,}" for k, v in result["channels"].items()) or "无")
    print("CAN ID（按出现次数）:")
    for item in result["top_ids"][:max(0, args.top)]:
        print(f"  {item['kind']:8s} {item['can_id']:>10s}  {item['count']:,}")
    if args.csv:
        print(f"CSV: {args.csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
