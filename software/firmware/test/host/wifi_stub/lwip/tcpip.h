#pragma once
#include "dns.h"
inline err_t tcpip_try_callback(void (*cb)(void *), void *arg) { cb(arg); return ERR_OK; }
