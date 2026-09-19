"""
Chuyen (user_id, ten) sang chuoi hex de dan thang vao Hercules (che do HEX)
gui cho Node A theo giao thuc trong uart_protocol.h.

Cach dung:
    python id_name_to_hex.py <user_id> "<ten>"

Vi du:
    python id_name_to_hex.py 1 "Dong"

Ket qua in ra 2 dong hex, dan lan luot tung dong vao o Send cua Hercules
(nho tick HEX truoc khi dan):
    1) Khung CMD_ADD_USER_START  -> gui truoc, cho ACK, roi dua mat vao khung hinh
    2) Khung CMD_ADD_USER_CAPTURE -> gui sau khi mat da o trong khung hinh
"""
import sys
import struct

FRAME_START = 0xAA
FRAME_END   = 0x55

CMD_ADD_USER_START   = 0x08
CMD_ADD_USER_CAPTURE = 0x09
CMD_ADD_USER_CANCEL  = 0x0A
CMD_SUB_USER         = 0x0B
CMD_LIST_USER_REQ    = 0x0C

NAME_LEN = 24  # phai khop voi payload_add_user_start_t.name[24] trong uart_protocol.h


def checksum(cmd: int, payload: bytes) -> int:
    c = cmd ^ len(payload)
    for b in payload:
        c ^= b
    return c & 0xFF


def build_frame(cmd: int, payload: bytes = b"") -> bytes:
    return bytes([FRAME_START, cmd, len(payload)]) + payload + \
           bytes([checksum(cmd, payload), FRAME_END])


def to_hex(data: bytes) -> str:
    return " ".join(f"{b:02X}" for b in data)


def add_user_start_frame(user_id: int, name: str) -> bytes:
    if user_id < 0 or user_id > 0xFFFF:
        raise ValueError("user_id phai trong khoang 0..65535")
    name_bytes = name.encode("utf-8")
    if len(name_bytes) > NAME_LEN - 1:
        print(f"[Canh bao] Ten dai hon {NAME_LEN - 1} byte se bi cat bot.")
    name_bytes = name_bytes[:NAME_LEN - 1].ljust(NAME_LEN, b"\x00")
    payload = struct.pack("<H", user_id) + name_bytes
    return build_frame(CMD_ADD_USER_START, payload)


def main():
    if len(sys.argv) < 3:
        print('Dung: python id_name_to_hex.py <user_id> "<ten>"')
        print('Vi du: python id_name_to_hex.py 1 "Dong"')
        sys.exit(1)

    user_id = int(sys.argv[1])
    name = sys.argv[2]

    start_frame = add_user_start_frame(user_id, name)
    capture_frame = build_frame(CMD_ADD_USER_CAPTURE)
    cancel_frame = build_frame(CMD_ADD_USER_CANCEL)

    print(f"user_id = {user_id}, ten = \"{name}\"")
    print()
    print("Buoc 1) CMD_ADD_USER_START - gui truoc, cho ACK (80 00 80 55):")
    print("  ", to_hex(start_frame))
    print()
    print("Buoc 2) CMD_ADD_USER_CAPTURE - gui khi mat da o trong khung hinh:")
    print("  ", to_hex(capture_frame))
    print()
    print("(Neu muon huy giua chung) CMD_ADD_USER_CANCEL:")
    print("  ", to_hex(cancel_frame))


if __name__ == "__main__":
    main()
