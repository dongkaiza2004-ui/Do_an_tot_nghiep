#pragma once

typedef enum {
    NAV_EVENT_NONE = 0,
    NAV_PREV,      // W - lùi 1 item
    NAV_NEXT,      // S - tiến 1 item
    NAV_DEC,       // A - giảm giá trị (slider/dropdown)
    NAV_INC,       // D - tăng giá trị (slider/dropdown)
    NAV_SELECT,    // Enter
    NAV_BACK,      // b / backspace
    NAV_CHAR,      // kem 1 chu so, chi phat sinh khi dang UART_MODE_DIGIT
} nav_event_t;

typedef struct {
    nav_event_t type;
    char ch;    // hop le khi type == NAV_CHAR
} nav_msg_t;
