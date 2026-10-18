/*
 * c64api - minimal blocking HTTP client for the Ultimate REST API.
 * Plain BSD sockets, so it builds for macOS and the PS5 payload alike.
 */
#ifndef C64API_H
#define C64API_H

#include <stddef.h>

typedef struct {
    const char *host;       /* IP address or host name */
    const char *password;   /* X-Password header, or NULL */
    int         port;       /* 0 = 80 */
    const char *client_id;  /* client-id header (Assembly64), or NULL */
    int         timeout_s;  /* receive timeout, 0 = 3 s */
} c64api;

/*
 * Send one request and read the response body into resp (may be NULL).
 * Returns the HTTP status, or -1 if the C64 couldn't be reached.
 */
int c64api_request(const c64api *api, const char *method, const char *path,
                   const char *content_type, const void *body, size_t body_len,
                   void *resp, size_t resp_cap, size_t *resp_len);

/*
 * Same, but returns the whole body malloc'd (NUL-terminated; caller frees)
 * and optionally the Content-Type. Returns the HTTP status or -1.
 */
int c64api_request_alloc(const c64api *api, const char *method, const char *path,
                         const char *content_type, const void *body, size_t body_len,
                         char **resp, size_t *resp_len, char *resp_type, size_t type_cap);

/* Split "http://host[:port]/base" into parts; returns 0 on success. */
int c64api_parse_url(const char *url, char *host, size_t host_cap, int *port,
                     char *base, size_t base_cap);

/*
 * Copy the string value of "key" from a flat JSON body (as the Ultimate
 * returns them) into out. Returns 0 if found.
 */
int c64api_json_string(const char *body, const char *key, char *out, size_t cap);

/* URL-encode a path for a query parameter ('/' kept). */
void c64api_url_encode(const char *in, char *out, size_t cap);

/* TCP connect with a timeout and 3 s send/receive timeouts; returns the socket or -1. */
int c64api_connect(const char *host, int port, int timeout_ms);

/* Find the local IP address used to reach host (no packets are sent). */
int c64api_local_ip(const char *host, char *out, size_t outlen);

/* Start the "video" or "audio" stream towards this machine; 0 on success. */
int c64api_start_stream(const c64api *api, const char *stream, int port, int default_port);
void c64api_stop_stream(const c64api *api, const char *stream);

#endif
