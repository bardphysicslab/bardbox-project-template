#pragma once
#include <sys/select.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <string>
enum { ESP_TLS_INIT, ESP_TLS_CONNECTING, ESP_TLS_HANDSHAKE, ESP_TLS_FAIL, ESP_TLS_DONE };
#define ESP_TLS_ERR_SSL_WANT_READ -2
#define ESP_TLS_ERR_SSL_WANT_WRITE -3
struct esp_tls_cfg_t {
 bool non_block, skip_common_name;
 int timeout_ms;
 const unsigned char *cacert_buf;
 unsigned cacert_bytes;
 const char *common_name;
};
typedef void *esp_tls_error_handle_t;
struct esp_tls_t { int conn_state=ESP_TLS_INIT, sockfd=1, calls=0; fd_set rset,wset; esp_tls_error_handle_t error_handle=nullptr; };
inline int esp_tls_get_and_clear_last_error(esp_tls_error_handle_t, int *code, int *flags) { *code=0x2700; *flags=0x4; return 0; }
extern int tlsCreated, tlsDestroyed, tlsPendingChecks;
extern std::string tlsSent;
extern size_t tlsReadAt;
inline esp_tls_t *esp_tls_init() { ++tlsCreated;return new esp_tls_t; }
inline void esp_tls_conn_destroy(esp_tls_t *p) { ++tlsDestroyed;delete p; }
inline int esp_tls_conn_new_async(const char *ip, int, int port, const esp_tls_cfg_t *cfg, esp_tls_t *tls) {
 assert(cfg->non_block && cfg->timeout_ms>0 && cfg->timeout_ms<=1);
 assert(!cfg->skip_common_name && !strcmp(cfg->common_name,"receiver.invalid"));
 assert(cfg->cacert_bytes>1 && cfg->cacert_buf && !strcmp(ip,"203.0.113.1") && port==443);
 if (tls->conn_state==ESP_TLS_CONNECTING) {
   assert(FD_ISSET(tls->sockfd,&tls->rset) && FD_ISSET(tls->sockfd,&tls->wset));
   ++tlsPendingChecks;
 }
 // First two pending calls consume descriptor sets just like select().
 if (++tls->calls<=2) { tls->conn_state=ESP_TLS_CONNECTING;FD_ZERO(&tls->rset);FD_ZERO(&tls->wset);return 0; }
 if (tls->calls==3) {tls->conn_state=ESP_TLS_HANDSHAKE;return 0;}
 tls->conn_state=ESP_TLS_DONE;return 1;
}
inline ssize_t esp_tls_conn_write(esp_tls_t *, const void *data, size_t n) {
 tlsSent.append(static_cast<const char *>(data),n);return n;
}
inline ssize_t esp_tls_conn_read(esp_tls_t *, void *data, size_t cap) {
 const std::string response="HTTP/1.1 204 No Content\r\n\r\n";
 size_t n=response.size()-tlsReadAt; if(n>cap)n=cap;
 memcpy(data,response.data()+tlsReadAt,n);tlsReadAt+=n;return n;
}
