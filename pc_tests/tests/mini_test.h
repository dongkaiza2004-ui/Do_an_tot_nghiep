// mini_test.h - khung test toi gian. Moi test in 1 dong "RESULT|suite|STATUS|ten|mo ta"
// de run_tests.py gom ket qua. STATUS: PASS | FAIL | REPRO | NOREPRO
#pragma once
#include <stdio.h>
#include <string.h>

static int mt_fail, mt_total, mt_failed;
static int mt_bug_seen __attribute__((unused));
static const char *mt_suite = "";

#define SUITE(s) (mt_suite = (s))
#define CHECK(cond) do { if (!(cond)) { mt_fail = 1; \
    printf("DETAIL|%s|%s:%d: CHECK that bai: %s\n", mt_suite, __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_MSG(cond, ...) do { if (!(cond)) { mt_fail = 1; \
    printf("DETAIL|%s|%s:%d: ", mt_suite, __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define INFO(...) do { printf("INFO|%s|", mt_suite); printf(__VA_ARGS__); printf("\n"); } while (0)

// Test thuong: phai PASS.
#define RUN(fn, desc) do { mt_fail = 0; fn(); mt_total++; \
    if (mt_fail) { mt_failed++; printf("RESULT|%s|FAIL|%s|%s\n", mt_suite, #fn, desc); } \
    else printf("RESULT|%s|PASS|%s|%s\n", mt_suite, #fn, desc); } while (0)

// Test "tai hien loi": test tu dat mt_bug_seen = 1 khi THAY loi tren hanh vi cu.
// REPRO   = loi duoc tai hien dung nhu ky vong (khong tinh la that bai).
// NOREPRO = khong thay loi nua (vi du da sua) - van khong tinh la that bai, chi bao.
#define RUN_REPRO(fn, desc) do { mt_fail = 0; mt_bug_seen = 0; fn(); mt_total++; \
    if (mt_fail) { mt_failed++; printf("RESULT|%s|FAIL|%s|%s\n", mt_suite, #fn, desc); } \
    else printf("RESULT|%s|%s|%s|%s\n", mt_suite, mt_bug_seen ? "REPRO" : "NOREPRO", #fn, desc); } while (0)

static inline int mt_exit_code(void) { fflush(stdout); return mt_failed ? 1 : 0; }
