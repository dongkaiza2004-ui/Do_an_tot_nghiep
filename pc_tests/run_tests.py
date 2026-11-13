#!/usr/bin/env python3
"""
run_tests.py - Chay TOAN BO test tren PC (ca Node A va Node B) bang 1 lenh:

    python3 pc_tests/run_tests.py

Script tu lam het:
  1. Lay ma nguon THAT tu nhanh nodeA / nodeB bang `git show` (khong can checkout).
  2. Bien dich cac file .c that tren PC (ESP-IDF duoc gia lap bang thu muc stubs/),
     kem AddressSanitizer + UBSan de bat loi bo nho.
  3. Chay cac bai test theo dung thu tu phu thuoc (B xuat lenh -> A doc; A gui anh -> B ghep),
     in ket qua, ghi bao cao build/report.md.
Yeu cau: Linux / macOS / WSL2, gcc (hoac clang), python3, git.
Ma thoat: 0 = khong co test nao FAIL, 1 = co FAIL hoac loi bien dich.
"""
import argparse, difflib, os, pathlib, re, subprocess, sys, datetime

HERE = pathlib.Path(__file__).resolve().parent

# ten file -> duong dan trong repo
B_FILES = {
    "comm_uart.c": "components/comm_uart/src/comm_uart.c",
    "comm_uart.h": "components/comm_uart/include/comm_uart.h",
    "uart_protocol.h": "components/protocol/include/uart_protocol.h",
    "espnow_frame.h": "components/protocol/include/espnow_frame.h",
    "comm_espnow.c": "components/comm_espnow/src/comm_espnow.c",
    "comm_espnow.h": "components/comm_espnow/include/comm_espnow.h",
    "sd_storage.c": "components/sd_storage/src/sd_storage.c",
    "sd_storage.h": "components/sd_storage/include/sd_storage.h",
    "access_events.c": "components/access_events/src/access_events.c",
    "access_events.h": "components/access_events/include/access_events.h",
}
A_FILES = {
    "comm_uart.c": "components/comm_uart/src/comm_uart.c",
    "comm_uart.h": "components/comm_uart/include/comm_uart.h",
    "uart_protocol.h": "components/comm_uart/include/uart_protocol.h",
    "comm_espnow.c": "components/comm_espnow/src/comm_espnow.c",
    "comm_espnow.h": "components/comm_espnow/include/comm_espnow.h",
    "espnow_frame.h": "components/comm_espnow/include/espnow_frame.h",
    "face_engine.h": "components/face_engine/include/face_engine.h",
    "app_state.h": "components/app_state/include/app_state.h",
    "app_state.c": "components/app_state/src/app_state.c",
    "cam_driver.h": "components/cam_driver/include/cam_driver.h",
}
A_EVENT_ID = "components/face_engine/include/event_id.h"   # co sau khi ban tich hop ban sua

USE_COLOR = sys.stdout.isatty()
def c(text, code): return f"\033[{code}m{text}\033[0m" if USE_COLOR else text
OK, BAD, WARN, DIM = (lambda t: c(t, 32)), (lambda t: c(t, 31)), (lambda t: c(t, 33)), (lambda t: c(t, 2))


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


class Sources:
    """Doc file nguon: thu muc chi dinh -> git ref -> cay lam viec hien tai."""
    def __init__(self, repo, node, refs, directory):
        self.repo, self.node, self.refs, self.dir = repo, node, refs, directory
        self.origin = {}

    def read(self, path, required=True):
        tried = []
        if self.dir:
            f = pathlib.Path(self.dir) / path
            tried.append(str(f))
            if f.is_file():
                self.origin[path] = f"thu muc {self.dir}"
                return f.read_text(encoding="utf-8")
        for ref in self.refs:
            r = run(["git", "-C", str(self.repo), "show", f"{ref}:{path}"])
            tried.append(f"{ref}:{path}")
            if r.returncode == 0:
                self.origin[path] = f"git {ref}"
                return r.stdout
        f = pathlib.Path(self.repo) / path
        tried.append(str(f) + " (cay lam viec)")
        if f.is_file():
            self.origin[path] = "cay lam viec hien tai"
            return f.read_text(encoding="utf-8")
        if required:
            sys.exit(f"[LOI] Khong tim thay '{path}' cho {self.node}.\n  Da thu: " + "\n          ".join(tried) +
                     "\n  Goi y: chay `git fetch --all` de co origin/nodeA, origin/nodeB; hoac dung --src-a/--src-b.")
        return None


def strip_c(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return [re.sub(r"\s+", " ", ln).strip() for ln in text.splitlines() if ln.strip()]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", help="thu muc repo git (mac dinh: repo chua script nay)")
    ap.add_argument("--ref-a", default="origin/nodeA"); ap.add_argument("--ref-b", default="origin/nodeB")
    ap.add_argument("--src-a", help="thu muc ma nguon Node A (bo qua git)")
    ap.add_argument("--src-b", help="thu muc ma nguon Node B (bo qua git)")
    ap.add_argument("--cc", default=os.environ.get("CC", "gcc"))
    ap.add_argument("--no-sanitize", action="store_true", help="tat AddressSanitizer/UBSan")
    ap.add_argument("-v", "--verbose", action="store_true", help="in ca cac dong ghi chu (INFO) cua test dat")
    args = ap.parse_args()

    if os.name == "nt":
        sys.exit("[LOI] Chay trong WSL2 / Linux / macOS (bo test dung POSIX: system/dirent/setjmp).")

    if args.repo:
        repo = pathlib.Path(args.repo).resolve()
    else:
        r = run(["git", "-C", str(HERE), "rev-parse", "--show-toplevel"])
        repo = pathlib.Path(r.stdout.strip()) if r.returncode == 0 else pathlib.Path.cwd()

    build = HERE / "build"
    if build.exists():
        run(["rm", "-rf", str(build)])
    for d in ("uart", "sd", "a", "b_espnow", "ev", "eid"):
        (build / "src" / d).mkdir(parents=True)

    srcB = Sources(repo, "Node B", [args.ref_b, "nodeB"], args.src_b)
    srcA = Sources(repo, "Node A", [args.ref_a, "nodeA"], args.src_a)
    results, notes, build_errors, skipped = [], [], [], []

    print(c("== 1/4  Lay ma nguon ==", 1))
    B = {k: srcB.read(v) for k, v in B_FILES.items()}
    A = {k: srcA.read(v, required=False) for k, v in A_FILES.items()}
    have_a = all(v is not None for v in A.values())
    print(f"   repo: {repo}")
    print(f"   Node B: {srcB.origin[B_FILES['sd_storage.c']]}")
    if have_a:
        print(f"   Node A: {srcA.origin[A_FILES['comm_uart.c']]}")
    else:
        miss = [k for k, v in A.items() if v is None]
        print(WARN(f"   Node A: KHONG DOC DUOC ({', '.join(miss)}) -> bo qua cac test Node A"))
        notes.append("Khong doc duoc ma nguon Node A: cac test Node A bi BO QUA (chay `git fetch --all`).")

    # event_id.h: uu tien ban da tich hop trong nodeA, neu chua co thi dung ban dong goi kem
    eid_text = srcA.read(A_EVENT_ID, required=False)
    if eid_text is not None:
        eid_origin = f"nodeA ({srcA.origin[A_EVENT_ID]}) - BAN DA TICH HOP"
    else:
        eid_text = (HERE / "event_id.h").read_text(encoding="utf-8")
        eid_origin = "ban dong goi kem trong pc_tests/ (chua tich hop vao nodeA)"
    print(f"   event_id.h: {eid_origin}")
    (build / "src/eid/event_id.h").write_text(eid_text, encoding="utf-8")

    S = build / "src"
    for name in ("comm_uart.c", "comm_uart.h", "uart_protocol.h", "espnow_frame.h"):
        (S / "uart" / name).write_text(B[name], encoding="utf-8")
    for name in ("comm_espnow.c", "comm_espnow.h", "espnow_frame.h"):
        (S / "b_espnow" / name).write_text(B[name], encoding="utf-8")
    # Ban sao sd_storage.h: CHI doi 2 hang so (duong dan the, so anh toi da). Logic khong dong vao.
    hdr, n1 = re.subn(r'(#define\s+SD_STORAGE_MOUNT_POINT\s+)"[^"]*"', r'\1"sdtest"', B["sd_storage.h"])
    hdr, n2 = re.subn(r'(#define\s+SD_STORAGE_MAX_IMAGES\s+)\d+', r'\g<1>5', hdr)
    if n1 != 1 or n2 != 1:
        sys.exit("[LOI] Khong va duoc sd_storage.h (doi ten macro SD_STORAGE_MOUNT_POINT / SD_STORAGE_MAX_IMAGES?).")
    for d in ("sd", "ev"):
        (S / d / "sd_storage.h").write_text(hdr, encoding="utf-8")
        (S / d / "sd_storage.c").write_text(B["sd_storage.c"], encoding="utf-8")
        (S / d / "access_events.h").write_text(B["access_events.h"], encoding="utf-8")
    (S / "ev" / "access_events.c").write_text(B["access_events.c"], encoding="utf-8")
    if have_a:
        for name, text in A.items():
            (S / "a" / name).write_text(text, encoding="utf-8")

    # ---- Bien dich ---------------------------------------------------------------
    print(c("\n== 2/4  Bien dich tren PC ==", 1))
    san = [] if args.no_sanitize else ["-fsanitize=address,undefined", "-fno-sanitize-recover=undefined"]
    base = [args.cc, "-std=gnu11", "-g", "-O0", "-Wall", "-Wextra", "-Wno-unused-parameter"]
    inc = lambda *d: [f"-I{x}" for x in d]
    stubs, tests = HERE / "stubs", HERE / "tests"
    T, ST = lambda n: str(tests / n), str(stubs / "stub_impl.c")
    # (ten, thuoc Node, nguon, include, co bien dich them, thu vien) - THU TU = THU TU CHAY
    jobs = [
        ("test_event_id", "A", [T("test_event_id.c")], inc(stubs, S / "eid", tests), [], []),
        ("test_uart", "B", [T("test_uart.c"), str(S / "uart/comm_uart.c"), ST], inc(stubs, S / "uart", tests), [], []),
        ("test_sd_storage", "B", [T("test_sd_storage.c"), str(S / "sd/sd_storage.c"), ST], inc(stubs, S / "sd", S / "eid", tests), [], []),
        ("test_b_events", "B", [T("test_b_events.c"), str(S / "ev/access_events.c"), str(S / "ev/sd_storage.c"), ST],
         inc(stubs, S / "ev", tests), [], ["-lm"]),
        ("test_a_uart", "A", [T("test_a_uart.c"), str(S / "a/comm_uart.c"), ST], inc(stubs, S / "a", tests), [], ["-lm"]),
        ("test_a_espnow", "A", [T("test_a_espnow.c"), str(S / "a/comm_espnow.c"), ST], inc(stubs, S / "a", tests),
         ["-DSTUB_TRACK_FREE"], []),
        ("test_b_espnow", "B", [T("test_b_espnow.c"), str(S / "b_espnow/comm_espnow.c"), ST], inc(stubs, S / "b_espnow", tests), [], []),
        ("test_a_appstate", "A", [T("test_a_appstate.c"), str(S / "a/app_state.c"), ST], inc(stubs, S / "a", tests), [], ["-lm"]),
    ]
    built = []
    for name, node, srcs, incs, extra, libs in jobs:
        if node == "A" and not have_a and name != "test_event_id":
            skipped.append(name); print(f"   {WARN('[bo qua]')} {name} (khong co ma nguon Node A)"); continue
        out = str(build / name)
        r = run(base + san + extra + srcs + incs + ["-o", out] + libs)
        if r.returncode != 0 and san:
            r2 = run(base + extra + srcs + incs + ["-o", out] + libs)
            if r2.returncode == 0:
                notes.append("May khong ho tro sanitizer -> bien dich khong co ASan/UBSan.")
                san = []; r = r2
        if r.returncode != 0:
            print(f"   {BAD('[LOI BIEN DICH]')} {name}\n{r.stderr[-1800:]}")
            build_errors.append(name)
        else:
            print(f"   {OK('[ok]')} {name}" + ("" if san else DIM("  (khong sanitizer)")))
            built.append(name)

    # ---- Chay ----------------------------------------------------------------------
    print(c("\n== 3/4  Chay test ==", 1))
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:allocator_may_return_null=1", UBSAN_OPTIONS="print_stacktrace=1")
    for name in built:
        try:
            r = run(["./" + name], cwd=build, timeout=300, env=env)
        except subprocess.TimeoutExpired:
            build_errors.append(name + " (timeout)"); print(f"   {BAD('[TIMEOUT]')} {name}"); continue
        got, pend_i, pend_d = 0, [], []
        for line in r.stdout.splitlines():
            p = line.split("|")
            if p[0] == "RESULT" and len(p) >= 5:
                results.append(dict(suite=p[1], status=p[2], name=p[3], desc="|".join(p[4:]), infos=pend_i, details=pend_d, binary=name))
                pend_i, pend_d = [], []; got += 1
            elif p[0] == "INFO" and len(p) >= 3: pend_i.append("|".join(p[2:]).strip())
            elif p[0] == "DETAIL" and len(p) >= 3: pend_d.append("|".join(p[2:]).strip())
        if r.returncode not in (0, 1) or got == 0:          # crash / sanitizer / khong in ket qua nao
            results.append(dict(suite=name, status="FAIL", name="(crash/sanitizer)", infos=[], details=[], binary=name,
                                desc="tien trinh thoat ma %d. %s" % (r.returncode, r.stderr.strip()[-700:])))
        elif r.returncode == 1 and not any(x["status"] == "FAIL" and x["binary"] == name for x in results):
            results.append(dict(suite=name, status="FAIL", name="(thoat ma 1)", infos=[], details=[], binary=name,
                                desc="tien trinh bao loi: " + r.stderr.strip()[-500:]))
        elif r.returncode == 0 and "Sanitizer" in r.stderr:
            results.append(dict(suite=name, status="FAIL", name="(sanitizer)", infos=[], details=[], binary=name,
                                desc="Sanitizer bao loi: " + r.stderr.strip()[-700:]))

    # ---- Hop dong giao tiep giua 2 node ------------------------------------------------
    if have_a:
        for fname in ("uart_protocol.h", "espnow_frame.h"):
            suite = "contract_A_vs_B"
            a, b = strip_c(A[fname]), strip_c(B[fname])
            if a == b:
                results.append(dict(suite=suite, status="PASS", name=f"{fname}_same", infos=[], details=[], binary="-",
                                    desc=f"{fname}: Node A va Node B GIONG HET (bo qua chu thich/khoang trang)"))
            else:
                results.append(dict(suite=suite, status="FAIL", name=f"{fname}_same", infos=[], binary="-",
                                    details=list(difflib.unified_diff(a, b, "nodeA", "nodeB", lineterm="", n=1)),
                                    desc=f"{fname}: Node A va Node B KHAC NHAU -> 2 ben hieu khung khac nhau!"))

    # ---- Tong ket -----------------------------------------------------------------------
    print(c("\n== 4/4  Ket qua ==", 1))
    tag = {"PASS": OK("[ PASS ]"), "FAIL": BAD("[ FAIL ]"), "REPRO": WARN("[ REPRO]"), "NOREPRO": WARN("[NOREPRO]")}
    suites = []
    for r in results:
        if r["suite"] not in suites: suites.append(r["suite"])
    for s in suites:
        print(c(f"\n  {s}", 1))
        for r in [x for x in results if x["suite"] == s]:
            print(f"   {tag[r['status']]} {r['desc']}")
            show = args.verbose or r["status"] in ("REPRO", "NOREPRO", "FAIL")
            if show:
                for t in r["infos"]: print(DIM(f"            i  {t}"))
            for t in r["details"]: print(BAD(f"            ! {t}"))

    cnt = lambda st: sum(1 for r in results if r["status"] == st)
    print("\n" + "-" * 72)
    print(f"  PASS: {cnt('PASS')}   FAIL: {cnt('FAIL')}   REPRO (han che/loi tai hien dung ky vong): {cnt('REPRO')}   NOREPRO: {cnt('NOREPRO')}")
    if cnt("REPRO"):
        print(WARN("  REPRO = test CHUNG MINH han che/loi ton tai tren code hien tai. Day la ket qua mong doi, khong phai that bai."))
    for n in notes: print(WARN("  luu y: " + n))
    for s in skipped: print(WARN(f"  bo qua: {s}"))
    for e in build_errors: print(BAD(f"  LOI: {e}"))
    failed = cnt("FAIL") > 0 or bool(build_errors)
    print("  " + (BAD("KET LUAN: CO LOI - xem cac dong [ FAIL ] o tren") if failed else OK("KET LUAN: TAT CA DAT")))

    with open(build / "report.md", "w", encoding="utf-8") as f:
        f.write(f"# Bao cao test tren PC\n\nThoi gian: {datetime.datetime.now():%Y-%m-%d %H:%M}\n\n")
        f.write(f"- Node B: {srcB.origin[B_FILES['sd_storage.c']]}\n- Node A: {srcA.origin.get(A_FILES['comm_uart.c'], 'KHONG DOC DUOC')}\n")
        f.write(f"- event_id.h: {eid_origin}\n- Sanitizer: {'co (ASan+UBSan)' if san else 'khong'}\n\n")
        f.write(f"**PASS {cnt('PASS')} / FAIL {cnt('FAIL')} / REPRO {cnt('REPRO')} / NOREPRO {cnt('NOREPRO')}**\n\n")
        rep = [r for r in results if r["status"] in ("REPRO", "NOREPRO")]
        if rep:
            f.write("## Han che / loi da tai hien tren code hien tai\n\n")
            for r in rep:
                f.write(f"- **[{r['status']}] {r['desc']}**\n")
                for t in r["infos"]: f.write(f"  - {t}\n")
            f.write("\n")
        f.write("## Toan bo test\n\n| Nhom | Ket qua | Noi dung test |\n|---|---|---|\n")
        for r in results: f.write(f"| {r['suite']} | {r['status']} | {r['desc']} |\n")
    print(DIM(f"  bao cao: {build / 'report.md'}"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
