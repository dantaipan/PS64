/*
 * ftp - just enough passive-mode FTP to list folders on an Ultimate.
 * Blocking; call it from a worker thread.
 */
#ifndef FTP_H
#define FTP_H

#include <stddef.h>

typedef struct {
    char               name[128];
    unsigned long long size;
    int                is_dir;
} ftp_entry;

/*
 * List dir on host (anonymous login, or the Ultimate network password).
 * Entries are sorted folders first, then by name. Returns the number of
 * entries, or -1 with a message in err.
 */
int ftp_list(const char *host, const char *password, const char *dir,
             ftp_entry *out, int max, char *err, size_t errcap);

/* A logged-in connection for listing many folders in a row. */
typedef struct ftp_session ftp_session;
ftp_session *ftp_open(const char *host, const char *password, char *err, size_t errcap);
/* The same on another port (the PS5's own FTP server listens on 2121). */
ftp_session *ftp_open_port(const char *host, int port, const char *password, char *err, size_t errcap);
/* List one folder (sorted); returns the count or -1 with a message in err. */
int  ftp_list_dir(ftp_session *s, const char *dir, ftp_entry *out, int max,
                  char *err, size_t errcap);
void ftp_close(ftp_session *s);

/* Parse one Unix-style LIST line; returns 0 on success. */
int ftp_parse_list_line(const char *line, ftp_entry *e);

/* Parse a "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)" reply's port. */
int ftp_parse_pasv_port(const char *reply);

void ftp_sort(ftp_entry *e, int n);

#endif
