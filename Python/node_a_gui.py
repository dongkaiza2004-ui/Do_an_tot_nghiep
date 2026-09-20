"""
Giao dien PC gui/nhan lenh UART cho Node A (theo dung uart_protocol.h).
Cai dat:  pip install pyserial
Chay:     python node_a_gui.py

Mac dinh mo COM3, 115200 baud - doi trong o "Cong" tren giao dien neu can.
"""
import struct
import threading
import queue
import time
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    raise SystemExit("Chua cai pyserial. Chay: pip install pyserial")


# ===========================================================================
# Giao thuc - khop 1:1 voi components/comm_uart/include/uart_protocol.h
# ===========================================================================
FRAME_START = 0xAA
FRAME_END = 0x55

CMD_SET_MODEL        = 0x01
CMD_SET_THRESHOLD    = 0x02
CMD_SET_BRIGHTNESS   = 0x03
CMD_SET_CONTRAST     = 0x04
CMD_SET_FLIP         = 0x05
CMD_SET_MIRROR       = 0x06
CMD_SHOW_FPS         = 0x07
CMD_ADD_USER_START   = 0x08
CMD_ADD_USER_CAPTURE = 0x09
CMD_ADD_USER_CANCEL  = 0x0A
CMD_SUB_USER         = 0x0B
CMD_LIST_USER_REQ    = 0x0C
CMD_START_STREAM     = 0x0D
CMD_STOP_STREAM      = 0x0E
CMD_CLEAR_ALL_USERS  = 0x0F

RSP_ACK              = 0x80
RSP_NACK             = 0x81
RSP_ADD_USER_RESULT  = 0x82
RSP_SUB_USER_RESULT  = 0x83
RSP_USER_LIST_ITEM   = 0x84
RSP_USER_LIST_END    = 0x85
RSP_FPS_REPORT       = 0x86
RSP_FACE_MATCHED     = 0x87
RSP_FACE_UNKNOWN     = 0x88
RSP_LIVENESS_FAIL    = 0x89

CMD_NAME = {
    CMD_SET_MODEL: "CMD_SET_MODEL", CMD_SET_THRESHOLD: "CMD_SET_THRESHOLD",
    CMD_SET_BRIGHTNESS: "CMD_SET_BRIGHTNESS", CMD_SET_CONTRAST: "CMD_SET_CONTRAST",
    CMD_SET_FLIP: "CMD_SET_FLIP", CMD_SET_MIRROR: "CMD_SET_MIRROR",
    CMD_SHOW_FPS: "CMD_SHOW_FPS", CMD_ADD_USER_START: "CMD_ADD_USER_START",
    CMD_ADD_USER_CAPTURE: "CMD_ADD_USER_CAPTURE", CMD_ADD_USER_CANCEL: "CMD_ADD_USER_CANCEL",
    CMD_SUB_USER: "CMD_SUB_USER", CMD_LIST_USER_REQ: "CMD_LIST_USER_REQ",
    CMD_START_STREAM: "CMD_START_STREAM", CMD_STOP_STREAM: "CMD_STOP_STREAM",
    CMD_CLEAR_ALL_USERS: "CMD_CLEAR_ALL_USERS",
    RSP_ACK: "RSP_ACK", RSP_NACK: "RSP_NACK", RSP_ADD_USER_RESULT: "RSP_ADD_USER_RESULT",
    RSP_SUB_USER_RESULT: "RSP_SUB_USER_RESULT", RSP_USER_LIST_ITEM: "RSP_USER_LIST_ITEM",
    RSP_USER_LIST_END: "RSP_USER_LIST_END", RSP_FPS_REPORT: "RSP_FPS_REPORT",
    RSP_FACE_MATCHED: "RSP_FACE_MATCHED", RSP_FACE_UNKNOWN: "RSP_FACE_UNKNOWN",
    RSP_LIVENESS_FAIL: "RSP_LIVENESS_FAIL",
}


def checksum(cmd: int, payload: bytes) -> int:
    c = cmd ^ len(payload)
    for b in payload:
        c ^= b
    return c & 0xFF


def build_frame(cmd: int, payload: bytes = b"") -> bytes:
    return bytes([FRAME_START, cmd, len(payload)]) + payload + \
           bytes([checksum(cmd, payload), FRAME_END])


def hx(data: bytes) -> str:
    return " ".join(f"{b:02X}" for b in data)


def pack_name(name: str, size: int = 24) -> bytes:
    return name.encode("utf-8")[: size - 1].ljust(size, b"\x00")


def unpack_name(data: bytes) -> str:
    return data.split(b"\x00", 1)[0].decode("utf-8", errors="ignore")


def decode_payload(cmd: int, payload: bytes) -> str:
    """Giai ma payload thanh chuoi de doc, khop tung struct trong uart_protocol.h."""
    try:
        if cmd == RSP_ACK:
            return "OK"
        if cmd == RSP_NACK:
            return f"loi (raw={hx(payload)})" if payload else "lenh bi tu choi"
        if cmd == RSP_ADD_USER_RESULT:
            success, user_id = struct.unpack_from("<BH", payload, 0)
            name = unpack_name(payload[3:3 + 24])
            return f"success={bool(success)} user_id={user_id} name='{name}'"
        if cmd == RSP_SUB_USER_RESULT:
            success, user_id = struct.unpack_from("<BH", payload, 0)
            return f"success={bool(success)} user_id={user_id}"
        if cmd == RSP_USER_LIST_ITEM:
            user_id, = struct.unpack_from("<H", payload, 0)
            name = unpack_name(payload[2:2 + 24])
            is_last = payload[26]
            return f"user_id={user_id} name='{name}' is_last={bool(is_last)}"
        if cmd == RSP_USER_LIST_END:
            return "(het danh sach)"
        if cmd == RSP_FPS_REPORT:
            fps, = struct.unpack_from("<f", payload, 0)
            return f"fps={fps:.1f}"
        if cmd == RSP_FACE_MATCHED:
            event_id, user_id, sim = struct.unpack_from("<IHf", payload, 0)
            return f"event_id={event_id} user_id={user_id} similarity={sim:.3f}"
        if cmd == RSP_FACE_UNKNOWN or cmd == RSP_LIVENESS_FAIL:
            event_id, = struct.unpack_from("<I", payload, 0)
            return f"event_id={event_id}"
    except struct.error as e:
        return f"[loi giai ma: {e}] raw={hx(payload)}"
    return f"raw={hx(payload)}" if payload else "(khong payload)"


# ===========================================================================
# Bo phan tich khung nhan - CUNG mot may trang thai voi comm_uart.c ben firmware
# ===========================================================================
class FrameParser:
    WAIT_START, WAIT_CMD, WAIT_LEN, WAIT_PAYLOAD, WAIT_CHECKSUM, WAIT_END = range(6)

    def __init__(self):
        self.state = self.WAIT_START
        self.cmd = 0
        self.length = 0
        self.payload = bytearray()

    def feed(self, byte: int):
        """Nap 1 byte, tra ve (cmd, payload:bytes) neu vua hoan tat 1 khung hop le, khong thi None."""
        if self.state == self.WAIT_START:
            if byte == FRAME_START:
                self.state = self.WAIT_CMD
        elif self.state == self.WAIT_CMD:
            self.cmd = byte
            self.state = self.WAIT_LEN
        elif self.state == self.WAIT_LEN:
            self.length = byte
            self.payload = bytearray()
            self.state = self.WAIT_PAYLOAD if self.length > 0 else self.WAIT_CHECKSUM
        elif self.state == self.WAIT_PAYLOAD:
            self.payload.append(byte)
            if len(self.payload) >= self.length:
                self.state = self.WAIT_CHECKSUM
        elif self.state == self.WAIT_CHECKSUM:
            expect = checksum(self.cmd, bytes(self.payload))
            self._checksum_ok = (byte == expect)
            self._checksum_got = byte
            self.state = self.WAIT_END
        elif self.state == self.WAIT_END:
            self.state = self.WAIT_START
            if byte == FRAME_END:
                cmd, payload = self.cmd, bytes(self.payload)
                ok = self._checksum_ok
                return (cmd, payload, ok)
            # sai byte END -> bo khung nay, khong tra gi ca
        return None


# ===========================================================================
# GUI
# ===========================================================================
class App:
    def __init__(self, root):
        self.root = root
        root.title("Node A - Cong cu dieu khien UART")
        root.geometry("880x640")

        self.ser = None
        self.rx_queue = queue.Queue()
        self.stop_flag = threading.Event()
        self.parser = FrameParser()

        self._build_ui()
        self.root.after(50, self._poll_queue)

    # ---------------- Ket noi ----------------
    def _build_ui(self):
        top = ttk.Frame(self.root, padding=6)
        top.pack(fill="x")

        ttk.Label(top, text="Cong:").pack(side="left")
        self.port_var = tk.StringVar(value="COM3")
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_combo = ttk.Combobox(top, textvariable=self.port_var, values=ports, width=10)
        self.port_combo.pack(side="left", padx=4)

        ttk.Label(top, text="Baud:").pack(side="left")
        self.baud_var = tk.StringVar(value="115200")
        ttk.Entry(top, textvariable=self.baud_var, width=8).pack(side="left", padx=4)

        self.connect_btn = ttk.Button(top, text="Ket noi", command=self._toggle_connect)
        self.connect_btn.pack(side="left", padx=8)

        self.status_var = tk.StringVar(value="Chua ket noi")
        ttk.Label(top, textvariable=self.status_var, foreground="red").pack(side="left", padx=8)

        body = ttk.Frame(self.root, padding=6)
        body.pack(fill="both", expand=True)

        left = ttk.Frame(body)
        left.pack(side="left", fill="y", padx=(0, 8))

        self._build_cmd_panel(left)

        right = ttk.Frame(body)
        right.pack(side="left", fill="both", expand=True)

        ttk.Label(right, text="Log (>> gui, << nhan):").pack(anchor="w")
        self.log = scrolledtext.ScrolledText(right, wrap="word", height=30, font=("Consolas", 9))
        self.log.pack(fill="both", expand=True)
        self.log.tag_config("tx", foreground="#0055aa")
        self.log.tag_config("rx", foreground="#008800")
        self.log.tag_config("err", foreground="#cc0000")

        ttk.Button(right, text="Xoa log", command=lambda: self.log.delete("1.0", "end")) \
            .pack(anchor="e", pady=4)

    def _build_cmd_panel(self, parent):
        nb = ttk.Notebook(parent, width=320)
        nb.pack(fill="y")

        # ---- Tab: Camera / model ----
        f1 = ttk.Frame(nb, padding=8)
        nb.add(f1, text="Camera")

        ttk.Label(f1, text="Model ID (0/1):").grid(row=0, column=0, sticky="w")
        self.model_var = tk.StringVar(value="0")
        ttk.Entry(f1, textvariable=self.model_var, width=6).grid(row=0, column=1)
        ttk.Button(f1, text="Set Model", command=self._send_set_model).grid(row=0, column=2, padx=4)

        ttk.Label(f1, text="Threshold (0..1):").grid(row=1, column=0, sticky="w")
        self.thresh_var = tk.StringVar(value="0.6")
        ttk.Entry(f1, textvariable=self.thresh_var, width=6).grid(row=1, column=1)
        ttk.Button(f1, text="Set Threshold", command=self._send_set_threshold).grid(row=1, column=2, padx=4)

        ttk.Label(f1, text="Brightness (-2..2):").grid(row=2, column=0, sticky="w")
        self.bright_var = tk.StringVar(value="0")
        ttk.Entry(f1, textvariable=self.bright_var, width=6).grid(row=2, column=1)
        ttk.Button(f1, text="Set Brightness", command=self._send_set_brightness).grid(row=2, column=2, padx=4)

        ttk.Label(f1, text="Contrast (-2..2):").grid(row=3, column=0, sticky="w")
        self.contrast_var = tk.StringVar(value="0")
        ttk.Entry(f1, textvariable=self.contrast_var, width=6).grid(row=3, column=1)
        ttk.Button(f1, text="Set Contrast", command=self._send_set_contrast).grid(row=3, column=2, padx=4)

        self.flip_var = tk.BooleanVar()
        ttk.Checkbutton(f1, text="Flip", variable=self.flip_var,
                         command=lambda: self._send_bool(CMD_SET_FLIP, self.flip_var.get())
                         ).grid(row=4, column=0, sticky="w", pady=6)

        self.mirror_var = tk.BooleanVar()
        ttk.Checkbutton(f1, text="Mirror", variable=self.mirror_var,
                         command=lambda: self._send_bool(CMD_SET_MIRROR, self.mirror_var.get())
                         ).grid(row=4, column=1, sticky="w", pady=6)

        self.fps_var = tk.BooleanVar()
        ttk.Checkbutton(f1, text="Show FPS", variable=self.fps_var,
                         command=lambda: self._send_bool(CMD_SHOW_FPS, self.fps_var.get())
                         ).grid(row=5, column=0, sticky="w")

        ttk.Separator(f1).grid(row=6, column=0, columnspan=3, sticky="ew", pady=8)
        ttk.Button(f1, text="Start Stream (du phong)",
                   command=lambda: self._send(CMD_START_STREAM)).grid(row=7, column=0, columnspan=2, sticky="ew", pady=2)
        ttk.Button(f1, text="Stop Stream (du phong)",
                   command=lambda: self._send(CMD_STOP_STREAM)).grid(row=8, column=0, columnspan=2, sticky="ew", pady=2)

        # ---- Tab: User ----
        f2 = ttk.Frame(nb, padding=8)
        nb.add(f2, text="User")

        ttk.Label(f2, text="User ID:").grid(row=0, column=0, sticky="w")
        self.uid_var = tk.StringVar(value="1")
        ttk.Entry(f2, textvariable=self.uid_var, width=8).grid(row=0, column=1)

        ttk.Label(f2, text="Ten:").grid(row=1, column=0, sticky="w")
        self.uname_var = tk.StringVar(value="Dong")
        ttk.Entry(f2, textvariable=self.uname_var, width=16).grid(row=1, column=1)

        ttk.Button(f2, text="1) Add User Start", command=self._send_add_user_start) \
            .grid(row=2, column=0, columnspan=2, sticky="ew", pady=(8, 2))
        ttk.Button(f2, text="2) Add User Capture (dua mat vao khung truoc)",
                   command=lambda: self._send(CMD_ADD_USER_CAPTURE)) \
            .grid(row=3, column=0, columnspan=2, sticky="ew", pady=2)
        ttk.Button(f2, text="Add User Cancel", command=lambda: self._send(CMD_ADD_USER_CANCEL)) \
            .grid(row=4, column=0, columnspan=2, sticky="ew", pady=2)

        ttk.Separator(f2).grid(row=5, column=0, columnspan=2, sticky="ew", pady=8)
        ttk.Button(f2, text="Xoa user (theo User ID o tren)", command=self._send_sub_user) \
            .grid(row=6, column=0, columnspan=2, sticky="ew", pady=2)
        ttk.Button(f2, text="Lay danh sach user", command=lambda: self._send(CMD_LIST_USER_REQ)) \
            .grid(row=7, column=0, columnspan=2, sticky="ew", pady=2)

        ttk.Separator(f2).grid(row=8, column=0, columnspan=2, sticky="ew", pady=8)
        clear_btn = ttk.Button(f2, text="XOA SACH TOAN BO USER", command=self._send_clear_all_users)
        clear_btn.grid(row=9, column=0, columnspan=2, sticky="ew", pady=2)

    # ---------------- Ket noi / ngat ----------------
    def _toggle_connect(self):
        if self.ser and self.ser.is_open:
            self.stop_flag.set()
            self.ser.close()
            self.ser = None
            self.status_var.set("Chua ket noi")
            self.connect_btn.config(text="Ket noi")
            return
        try:
            self.ser = serial.Serial(self.port_var.get(), int(self.baud_var.get()), timeout=0.2)
        except Exception as e:
            messagebox.showerror("Loi ket noi", str(e))
            return
        self.stop_flag.clear()
        self.parser = FrameParser()
        threading.Thread(target=self._reader_thread, daemon=True).start()
        self.status_var.set(f"Da ket noi {self.port_var.get()} @ {self.baud_var.get()}")
        self.connect_btn.config(text="Ngat ket noi")

    def _reader_thread(self):
        while not self.stop_flag.is_set():
            try:
                data = self.ser.read(256)
            except Exception as e:
                self.rx_queue.put(("err", f"Loi doc cong: {e}"))
                return
            for b in data:
                result = self.parser.feed(b)
                if result:
                    cmd, payload, ok = result
                    self.rx_queue.put(("rx", (cmd, payload, ok)))

    # ---------------- Gui frame ----------------
    def _write(self, frame: bytes):
        if not self.ser or not self.ser.is_open:
            messagebox.showwarning("Chua ket noi", "Hay ket noi cong COM truoc.")
            return
        self.ser.write(frame)
        cmd = frame[1]
        name = CMD_NAME.get(cmd, f"0x{cmd:02X}")
        self._log(f">> {name}: {hx(frame)}", "tx")

    def _send(self, cmd, payload=b""):
        self._write(build_frame(cmd, payload))

    def _send_bool(self, cmd, value: bool):
        self._send(cmd, bytes([1 if value else 0]))

    def _send_set_model(self):
        try:
            self._send(CMD_SET_MODEL, bytes([int(self.model_var.get())]))
        except ValueError:
            messagebox.showerror("Loi", "Model ID phai la so nguyen")

    def _send_set_threshold(self):
        try:
            self._send(CMD_SET_THRESHOLD, struct.pack("<f", float(self.thresh_var.get())))
        except ValueError:
            messagebox.showerror("Loi", "Threshold phai la so thuc")

    def _send_set_brightness(self):
        try:
            self._send(CMD_SET_BRIGHTNESS, struct.pack("<b", int(self.bright_var.get())))
        except ValueError:
            messagebox.showerror("Loi", "Brightness phai la so nguyen -2..2")

    def _send_set_contrast(self):
        try:
            self._send(CMD_SET_CONTRAST, struct.pack("<b", int(self.contrast_var.get())))
        except ValueError:
            messagebox.showerror("Loi", "Contrast phai la so nguyen -2..2")

    def _send_add_user_start(self):
        try:
            uid = int(self.uid_var.get())
        except ValueError:
            messagebox.showerror("Loi", "User ID phai la so nguyen")
            return
        payload = struct.pack("<H", uid) + pack_name(self.uname_var.get())
        self._send(CMD_ADD_USER_START, payload)

    def _send_clear_all_users(self):
        if messagebox.askyesno(
            "Xac nhan xoa sach",
            "Xoa SACH toan bo user + feature vector trong Node A?\n"
            "Khong the hoan tac. Tiep tuc?",
        ):
            self._send(CMD_CLEAR_ALL_USERS)

    def _send_sub_user(self):
        try:
            uid = int(self.uid_var.get())
        except ValueError:
            messagebox.showerror("Loi", "User ID phai la so nguyen")
            return
        self._send(CMD_SUB_USER, struct.pack("<H", uid))

    # ---------------- Nhan / hien thi ----------------
    def _poll_queue(self):
        try:
            while True:
                kind, data = self.rx_queue.get_nowait()
                if kind == "err":
                    self._log(data, "err")
                else:
                    cmd, payload, ok = data
                    name = CMD_NAME.get(cmd, f"0x{cmd:02X}")
                    decoded = decode_payload(cmd, payload)
                    tag = "rx" if ok else "err"
                    ck_note = "" if ok else "  [!] CHECKSUM SAI"
                    self._log(f"<< {name}: {decoded}{ck_note}   (raw payload={hx(payload)})", tag)
        except queue.Empty:
            pass
        self.root.after(50, self._poll_queue)

    def _log(self, text, tag):
        ts = time.strftime("%H:%M:%S")
        self.log.insert("end", f"[{ts}] {text}\n", tag)
        self.log.see("end")


if __name__ == "__main__":
    root = tk.Tk()
    App(root)
    root.mainloop()