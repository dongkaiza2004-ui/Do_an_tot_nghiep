// time_sync.h
//
// Dong bo gio he thong qua SNTP khi Wi-Fi co IP. Can thiet de lich su ghi len
// SD co moc thoi gian that (esp_timer_get_time() chi tinh tu luc boot).

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Goi sau esp_matter::start(). Tu bat SNTP khi (hoac neu da) co IP.
void time_sync_start(void);

// true neu gio he thong da duoc dong bo (khac gia tri mac dinh 1970).
bool time_sync_is_valid(void);

#ifdef __cplusplus
}
#endif
