#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fake_node_a.py — Gia lap camera (Node A) de test Node B qua UART.

Script nay:
  - Mo cong UART noi voi Node B (dung dung baud 115200 nhu firmware).
  - Doc va giai ma cac khung CMD_* ma Node B gui xuong (theo dung
    uart_protocol.h), in ra man hinh.
  - Cho ban go lenh de gui nguoc lai cac khung RSP_* gia lap (nhu the
    camera thuc su vua nhan dien xong), de test access_gateway.cpp ma
    chua can dau day camera thuc.

Cai dat:
    pip install pyserial

Chay:
    python3 fake_node_a.py /dev/ttyUSB0
    python3 fake_node_a.py COM5 --baud 115200

Trong luc chay, go "help" de xem danh sach lenh.
"""

import sys
import struct
import threading
import time
import argparse

try:
    import serial
except ImportError:
    print("Chua co pyserial. Cai bang: pip install pyserial")
    sys.exit(1)


# ==========================================================================
# Hang so giao thuc - PHAI khop chinh xac voi uart_protocol.h
# ==========================================================================

FRAME_START = 0xAA
FRAME_END = 0x55
MAX_PAYLOAD = 64

CMD_SET_MODEL = 0x01
CMD_SET_THRESHOLD = 0x02
CMD_SET_BRIGHTNESS = 0x03
CMD_SET_CONTRAST = 0x04
CMD_SET_FLIP = 0x05
CMD_SET_MIRROR = 0x06
CMD_SHOW_FPS = 0x07
CMD_ADD_USER_START = 0x08
CMD_ADD_USER_CAPTURE = 0x09
CMD_ADD_USER_CANCEL = 0x0A
CMD_SUB_USER = 0x0B
CMD_LIST_USER_REQ = 0x0C
CMD_START_STREAM = 0x0D
CMD_STOP_STREAM = 0x0E
CMD_CLEAR_ALL_USERS = 0x0F
CMD_ENABLE_LIVENESS_CHECK = 0x10

RSP_ACK = 0x80
RSP_NACK = 0x81
RSP_ADD_USER_RESULT = 0x82
RSP_SUB_USER_RESULT = 0x83
RSP_USER_LIST_ITEM = 0x84
RSP_USER_LIST_END = 0x85
RSP_FPS_REPORT = 0x86
RSP_FACE_MATCHED = 0x87
RSP_FACE_UNKNOWN = 0x88
RSP_LIVENESS_FAIL = 0x89

CMD_NAMES = {
    CMD_SET_MODEL: "CMD_SET_MODEL",
    CMD_SET_THRESHOLD: "CMD_SET_THRESHOLD",
    CMD_SET_BRIGHTNESS: "CMD_SET_BRIGHTNESS",
    CMD_SET_CONTRAST: "CMD_SET_CONTRAST",
    CMD_SET_FLIP: "CMD_SET_FLIP",
    CMD_SET_MIRROR: "CMD_SET_MIRROR",
    CMD_SHOW_FPS: "CMD_SHOW_FPS",
    CMD_ADD_USER_START: "CMD_ADD_USER_START",
    CMD_ADD_USER_CAPTURE: "CMD_ADD_USER_CAPTURE",
    CMD_ADD_USER_CANCEL: "CMD_ADD_USER_CANCEL",
    CMD_SUB_USER: "CMD_SUB_USER",
    CMD_LIST_USER_REQ: "CMD_LIST_USER_REQ",
    CMD_START_STREAM: "CMD_START_STREAM",
    CMD_STOP_STREAM: "CMD_STOP_STREAM",
    CMD_CLEAR_ALL_USERS: "CMD_CLEAR_ALL_USERS",
    CMD_ENABLE_LIVENESS_CHECK: "CMD_ENABLE_LIVENESS_CHECK",
}


def checksum(cmd: int, length: int, payload: bytes) -> int:
    """Giong het uart_calc_checksum() trong uart_protocol.h: XOR cmd, len, payload."""
    c = cmd ^ length
    for b in payload:
        c ^= b
    return c & 0xFF


def build_frame(cmd: int, payload: bytes = b"") -> bytes:
    length = len(payload)
    if length > MAX_PAYLOAD:
        raise ValueError(f"Payload qua dai ({length} > {MAX_PAYLOAD})")
    cs = checksum(cmd, length, payload)
    return bytes([FRAME_START, cmd, length]) + payload + bytes([cs, FRAME_END])


def decode_cstr(raw: bytes) -> str:
    """Cat theo byte 0x00 dau tien (char name[24] kieu C-string) va decode ASCII."""
    idx = raw.find(b"\x00")
    if idx >= 0:
        raw = raw[:idx]
    return raw.decode("ascii", errors="replace")


# ==========================================================================
# Giai ma payload cua tung lenh CMD_* (Node B gui xuong) de in cho de doc
# ==========================================================================

def describe_cmd_payload(cmd: int, payload: bytes) -> str:
    try:
        if cmd == CMD_SET_MODEL:
            (model_id,) = struct.unpack("<B", payload)
            return f"model_id={model_id}"
        if cmd == CMD_SET_THRESHOLD:
            (threshold,) = struct.unpack("<f", payload)
            return f"threshold={threshold:.3f}"
        if cmd in (CMD_SET_BRIGHTNESS, CMD_SET_CONTRAST):
            (value,) = struct.unpack("<b", payload)
            return f"value={value}"
        if cmd in (CMD_SET_FLIP, CMD_SET_MIRROR, CMD_SHOW_FPS, CMD_ENABLE_LIVENESS_CHECK):
            (enable,) = struct.unpack("<B", payload)
            return f"enable={bool(enable)}"
        if cmd == CMD_ADD_USER_START:
            user_id, name_raw = struct.unpack("<H24s", payload)
            return f"user_id={user_id} name='{decode_cstr(name_raw)}'"
        if cmd == CMD_SUB_USER:
            (user_id,) = struct.unpack("<H", payload)
            return f"user_id={user_id}"
        if cmd in (CMD_ADD_USER_CAPTURE, CMD_ADD_USER_CANCEL, CMD_LIST_USER_REQ,
                   CMD_START_STREAM, CMD_STOP_STREAM, CMD_CLEAR_ALL_USERS):
            return "(khong payload)"
    except struct.error as e:
        return f"[LOI GIAI MA: {e}] raw={payload.hex()}"
    return f"raw={payload.hex()}"


# ==========================================================================
# May trang thai doc UART - GIONG HET logic ben comm_uart.c cua Node B
# ==========================================================================

WAIT_START, WAIT_CMD, WAIT_LEN, WAIT_PAYLOAD, WAIT_CHECKSUM, WAIT_END = range(6)


class FrameParser:
    def __init__(self, on_frame):
        self.state = WAIT_START
        self.cmd = 0
        self.length = 0
        self.payload = bytearray()
        self.on_frame = on_frame

    def feed_byte(self, b: int):
        if self.state == WAIT_START:
            if b == FRAME_START:
                self.state = WAIT_CMD

        elif self.state == WAIT_CMD:
            self.cmd = b
            self.state = WAIT_LEN

        elif self.state == WAIT_LEN:
            self.length = b
            self.payload = bytearray()
            if self.length > MAX_PAYLOAD:
                print(f"[CANH BAO] LEN qua lon ({self.length}), huy khung")
                self.state = WAIT_START
            elif self.length == 0:
                self.state = WAIT_CHECKSUM
            else:
                self.state = WAIT_PAYLOAD

        elif self.state == WAIT_PAYLOAD:
            self.payload.append(b)
            if len(self.payload) >= self.length:
                self.state = WAIT_CHECKSUM

        elif self.state == WAIT_CHECKSUM:
            expected = checksum(self.cmd, self.length, bytes(self.payload))
            if b != expected:
                print(f"[CANH BAO] Sai checksum (cmd=0x{self.cmd:02X}, "
                      f"nhan={b:02X}, ky vong={expected:02X}), huy khung")
                self.state = WAIT_START
            else:
                self.state = WAIT_END

        elif self.state == WAIT_END:
            if b == FRAME_END:
                self.on_frame(self.cmd, bytes(self.payload))
            else:
                print(f"[CANH BAO] Thieu byte END (cmd=0x{self.cmd:02X}), huy khung")
            self.state = WAIT_START


# ==========================================================================
# Luong doc nen
# ==========================================================================

def reader_thread(ser: serial.Serial, stop_event: threading.Event):
    def on_frame(cmd, payload):
        name = CMD_NAMES.get(cmd, f"0x{cmd:02X}")
        detail = describe_cmd_payload(cmd, payload)
        ts = time.strftime("%H:%M:%S")
        print(f"\n[{ts}] <-- Node B gui: {name} | {detail}")
        print("> ", end="", flush=True)

    parser = FrameParser(on_frame)

    while not stop_event.is_set():
        try:
            data = ser.read(1)
        except serial.SerialException as e:
            print(f"\n[LOI] Mat ket noi serial: {e}")
            break
        if data:
            parser.feed_byte(data[0])


# ==========================================================================
# Cac lenh go tay de gui RSP_* gia lap
# ==========================================================================

def cmd_matched(ser, args):
    event_id, user_id, similarity = int(args[0]), int(args[1]), float(args[2])
    payload = struct.pack("<IHf", event_id, user_id, similarity)
    ser.write(build_frame(RSP_FACE_MATCHED, payload))
    print(f"--> Da gui RSP_FACE_MATCHED event_id={event_id} user_id={user_id} similarity={similarity}")


def cmd_unknown(ser, args):
    event_id = int(args[0])
    payload = struct.pack("<I", event_id)
    ser.write(build_frame(RSP_FACE_UNKNOWN, payload))
    print(f"--> Da gui RSP_FACE_UNKNOWN event_id={event_id}")


def cmd_liveness(ser, args):
    event_id = int(args[0])
    payload = struct.pack("<I", event_id)
    ser.write(build_frame(RSP_LIVENESS_FAIL, payload))
    print(f"--> Da gui RSP_LIVENESS_FAIL event_id={event_id}")


def cmd_ack(ser, args):
    original_cmd = int(args[0], 0)  # ho tro nhap "0x09" hoac "9"
    payload = bytes([original_cmd])
    ser.write(build_frame(RSP_ACK, payload))
    print(f"--> Da gui RSP_ACK cho cmd=0x{original_cmd:02X}")


def cmd_nack(ser, args):
    original_cmd = int(args[0], 0)
    error_code = int(args[1], 0)
    payload = bytes([original_cmd, error_code])
    ser.write(build_frame(RSP_NACK, payload))
    print(f"--> Da gui RSP_NACK cho cmd=0x{original_cmd:02X} error=0x{error_code:02X}")


def cmd_adduser(ser, args):
    success = int(args[0])
    user_id = int(args[1])
    name = args[2] if len(args) > 2 else "User"
    name_bytes = name.encode("ascii")[:23].ljust(24, b"\x00")
    payload = struct.pack("<BH24s", success, user_id, name_bytes)
    ser.write(build_frame(RSP_ADD_USER_RESULT, payload))
    print(f"--> Da gui RSP_ADD_USER_RESULT success={success} user_id={user_id} name='{name}'")


def cmd_subuser(ser, args):
    success = int(args[0])
    user_id = int(args[1])
    payload = struct.pack("<BH", success, user_id)
    ser.write(build_frame(RSP_SUB_USER_RESULT, payload))
    print(f"--> Da gui RSP_SUB_USER_RESULT success={success} user_id={user_id}")


def cmd_listitem(ser, args):
    user_id = int(args[0])
    name = args[1] if len(args) > 1 else "User"
    is_last = int(args[2]) if len(args) > 2 else 0
    name_bytes = name.encode("ascii")[:23].ljust(24, b"\x00")
    payload = struct.pack("<H24sB", user_id, name_bytes, is_last)
    ser.write(build_frame(RSP_USER_LIST_ITEM, payload))
    print(f"--> Da gui RSP_USER_LIST_ITEM user_id={user_id} name='{name}' is_last={is_last}")


def cmd_listend(ser, args):
    ser.write(build_frame(RSP_USER_LIST_END, b""))
    print("--> Da gui RSP_USER_LIST_END")


def cmd_fps(ser, args):
    fps = float(args[0])
    payload = struct.pack("<f", fps)
    ser.write(build_frame(RSP_FPS_REPORT, payload))
    print(f"--> Da gui RSP_FPS_REPORT fps={fps}")


def print_help():
    print("""
Cac lenh co the go (cach nhau boi dau cach):

  matched <event_id> <user_id> <similarity>   Gui RSP_FACE_MATCHED
                                                vd: matched 1 1 0.95
  unknown <event_id>                           Gui RSP_FACE_UNKNOWN
                                                vd: unknown 2
  liveness <event_id>                          Gui RSP_LIVENESS_FAIL
                                                vd: liveness 3

  ack <cmd_hex>                                Gui RSP_ACK cho 1 cmd
                                                vd: ack 0x03
  nack <cmd_hex> <error_hex>                   Gui RSP_NACK
                                                vd: nack 0x08 0x01

  adduser <0|1> <user_id> <name>               Gui RSP_ADD_USER_RESULT
                                                vd: adduser 1 1 "Nguyen Van A"
  subuser <0|1> <user_id>                      Gui RSP_SUB_USER_RESULT
                                                vd: subuser 1 1

  listitem <user_id> <name> <is_last 0|1>      Gui 1 muc RSP_USER_LIST_ITEM
                                                vd: listitem 1 "User A" 1
  listend                                      Gui RSP_USER_LIST_END

  fps <value>                                  Gui RSP_FPS_REPORT
                                                vd: fps 12.5

  help                                          In lai huong dan nay
  quit / exit                                   Thoat
""")


COMMANDS = {
    "matched": cmd_matched,
    "unknown": cmd_unknown,
    "liveness": cmd_liveness,
    "ack": cmd_ack,
    "nack": cmd_nack,
    "adduser": cmd_adduser,
    "subuser": cmd_subuser,
    "listitem": cmd_listitem,
    "listend": cmd_listend,
    "fps": cmd_fps,
}


def parse_command_line(line: str):
    """Tach theo dau cach nhung giu nguyen phan trong dau nhay kep (cho ten co dau cach)."""
    parts = []
    current = ""
    in_quotes = False
    for ch in line:
        if ch == '"':
            in_quotes = not in_quotes
        elif ch == " " and not in_quotes:
            if current:
                parts.append(current)
                current = ""
        else:
            current += ch
    if current:
        parts.append(current)
    return parts


def main():
    ap = argparse.ArgumentParser(description="Gia lap Node A (camera) de test UART cua Node B")
    ap.add_argument("port", help="Cong serial, vd /dev/ttyUSB0 hoac COM5")
    ap.add_argument("--baud", type=int, default=115200, help="Baud rate (mac dinh 115200)")
    args = ap.parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.2)
    except serial.SerialException as e:
        print(f"Khong mo duoc cong {args.port}: {e}")
        sys.exit(1)

    print(f"Da mo {args.port} @ {args.baud} baud. Dang lang nghe CMD_* tu Node B...")
    print_help()

    stop_event = threading.Event()
    t = threading.Thread(target=reader_thread, args=(ser, stop_event), daemon=True)
    t.start()

    try:
        while True:
            line = input("> ").strip()
            if not line:
                continue
            parts = parse_command_line(line)
            keyword = parts[0].lower()

            if keyword in ("quit", "exit"):
                break
            if keyword == "help":
                print_help()
                continue

            handler = COMMANDS.get(keyword)
            if not handler:
                print(f"Lenh khong hop le: '{keyword}'. Go 'help' de xem danh sach.")
                continue

            try:
                handler(ser, parts[1:])
            except (IndexError, ValueError) as e:
                print(f"Sai cu phap lenh '{keyword}': {e}. Go 'help' de xem vi du.")

    except KeyboardInterrupt:
        pass
    finally:
        stop_event.set()
        ser.close()
        print("\nDa dong cong serial. Tam biet.")


if __name__ == "__main__":
    main()