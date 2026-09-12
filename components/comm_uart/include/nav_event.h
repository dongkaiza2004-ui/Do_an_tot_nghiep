#pragma once

typedef enum {
    NAV_EVENT_NONE = 0,
    NAV_PREV, NAV_NEXT, NAV_DEC, NAV_INC, NAV_SELECT, NAV_BACK,
    NAV_CHAR,   // kem 1 ky tu, chi phat sinh khi dang UART_MODE_TEXT
} nav_event_t;

typedef struct {
    nav_event_t type;
    char ch;
} nav_msg_t;