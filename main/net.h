#pragma once
#include "esp_err.h"
/* Verbindet mit dem in menuconfig hinterlegten WLAN und wartet auf eine IP. */
esp_err_t net_connect(void);
