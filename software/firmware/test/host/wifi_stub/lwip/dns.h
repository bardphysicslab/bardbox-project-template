#pragma once
#include <stdio.h>
#include <stdint.h>
typedef int err_t;
#define ERR_OK 0
#define ERR_INPROGRESS -5
struct ip_addr_t { uint32_t value; };
inline bool ip_addr_isany(const ip_addr_t *p) { return !p->value; }
inline char *ipaddr_ntoa_r(const ip_addr_t *, char *s, int n) { snprintf(s,n,"203.0.113.1"); return s; }
typedef void (*dns_cb)(const char *,const ip_addr_t *,void *);
extern dns_cb pendingDns;
extern void *pendingDnsArg;
inline err_t dns_gethostbyname(const char *, ip_addr_t *, dns_cb cb, void *arg) {
 pendingDns=cb; pendingDnsArg=arg; return ERR_INPROGRESS;
}
