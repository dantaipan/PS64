#include "discover.h"

#include "c64api.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define BATCH      64
#define CONNECT_MS 600

struct discover {
    pthread_t       thread;
    pthread_mutex_t lock;
    char            near[16];
    char            password[64];
    int             have_password;
    int             quit, done, progress;
    c64_device      found[DISCOVER_MAX];
    int             count;
};

int discover_parse_info(const char *body, c64_device *dev)
{
    if (c64api_json_string(body, "product", dev->product, sizeof(dev->product)) != 0)
        return -1;
    c64api_json_string(body, "hostname", dev->hostname, sizeof(dev->hostname));
    c64api_json_string(body, "firmware_version", dev->firmware, sizeof(dev->firmware));
    return strstr(dev->product, "Ultimate") ? 0 : -1;
}

static void add(discover *d, const c64_device *dev)
{
    pthread_mutex_lock(&d->lock);
    int dup = 0;
    for (int i = 0; i < d->count; i++)
        dup |= !strcmp(d->found[i].ip, dev->ip);
    if (!dup && d->count < DISCOVER_MAX)
        d->found[d->count++] = *dev;
    pthread_mutex_unlock(&d->lock);
}

static int should_quit(discover *d)
{
    pthread_mutex_lock(&d->lock);
    int q = d->quit;
    pthread_mutex_unlock(&d->lock);
    return q;
}

/* Probe one host that has port 80 open. */
static void probe(discover *d, const char *ip)
{
    c64api api = { .host = ip, .password = d->have_password ? d->password : NULL };
    char body[1024];
    size_t got = 0;
    int status = c64api_request(&api, "GET", "/v1/info", NULL, NULL, 0, body, sizeof(body) - 1, &got);
    body[got] = 0;
    c64_device dev;
    memset(&dev, 0, sizeof(dev));
    snprintf(dev.ip, sizeof(dev.ip), "%s", ip);
    if (status == 200 && discover_parse_info(body, &dev) == 0) {
        add(d, &dev);
    } else if (status == 403) {
        dev.locked = 1;
        snprintf(dev.product, sizeof(dev.product), "Password protected");
        add(d, &dev);
    }
}

/* Connect to port 80 on hosts first..first+n-1 of the /24 at once. */
static void scan_batch(discover *d, uint32_t net, int first, int n)
{
    int fds[BATCH];
    struct pollfd pfd[BATCH];
    for (int i = 0; i < n; i++) {
        fds[i] = socket(AF_INET, SOCK_STREAM, 0);
        pfd[i].fd = -1;
        pfd[i].events = POLLOUT;
        pfd[i].revents = 0;
        if (fds[i] < 0)
            continue;
        fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL) | O_NONBLOCK);
        struct sockaddr_in a = {0};
        a.sin_family = AF_INET;
        a.sin_port = htons(80);
        a.sin_addr.s_addr = htonl(net | (uint32_t)(first + i));
        int rc = connect(fds[i], (struct sockaddr *)&a, sizeof(a));
        if (rc == 0 || errno == EINPROGRESS)
            pfd[i].fd = fds[i];
    }

    /* Wait on the clock, not on poll() calls: refused connections make poll
     * return at once, which would otherwise cut the window short. */
    int open_hosts[BATCH], nopen = 0, pending = n;
    struct timespec t0, t;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &t);
        long elapsed = (t.tv_sec - t0.tv_sec) * 1000L + (t.tv_nsec - t0.tv_nsec) / 1000000L;
        if (elapsed >= CONNECT_MS || pending == 0)
            break;
        int r = poll(pfd, (nfds_t)n, (int)(CONNECT_MS - elapsed));
        if (r <= 0)
            continue;
        for (int i = 0; i < n; i++) {
            if (pfd[i].fd < 0 || !pfd[i].revents)
                continue;
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(pfd[i].fd, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err == 0 && (pfd[i].revents & POLLOUT))
                open_hosts[nopen++] = first + i;
            pfd[i].fd = -1;     /* done with this host either way */
            pending--;
        }
    }
    for (int i = 0; i < n; i++)
        if (fds[i] >= 0)
            close(fds[i]);

    for (int i = 0; i < nopen && !should_quit(d); i++) {
        struct in_addr ia = { htonl(net | (uint32_t)open_hosts[i]) };
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ia, ip, sizeof(ip));
        probe(d, ip);
    }
}

static uint32_t net24(const char *ip)
{
    struct in_addr a;
    if (inet_pton(AF_INET, ip, &a) != 1)
        return 0;
    return ntohl(a.s_addr) & 0xFFFFFF00u;
}

static void *worker(void *arg)
{
    discover *d = arg;
    uint32_t nets[2];
    int nnets = 0;
    char me[INET_ADDRSTRLEN];
    /* A UDP "connect" picks the outgoing interface without sending anything. */
    if (c64api_local_ip("8.8.8.8", me, sizeof(me)) == 0 && net24(me))
        nets[nnets++] = net24(me);
    if (d->near[0] && net24(d->near) && (nnets == 0 || net24(d->near) != nets[0]))
        nets[nnets++] = net24(d->near);

    int total = nnets * 254, scanned = 0;
    for (int k = 0; k < nnets && !should_quit(d); k++) {
        for (int first = 1; first <= 254 && !should_quit(d); first += BATCH) {
            int n = first + BATCH - 1 > 254 ? 255 - first : BATCH;
            scan_batch(d, nets[k], first, n);
            scanned += n;
            pthread_mutex_lock(&d->lock);
            d->progress = total ? scanned * 100 / total : 100;
            pthread_mutex_unlock(&d->lock);
        }
    }
    pthread_mutex_lock(&d->lock);
    d->done = 1;
    d->progress = 100;
    pthread_mutex_unlock(&d->lock);
    return NULL;
}

discover *discover_start(const char *also_near, const char *password)
{
    discover *d = calloc(1, sizeof(*d));
    if (!d)
        return NULL;
    if (also_near)
        snprintf(d->near, sizeof(d->near), "%s", also_near);
    if (password) {
        snprintf(d->password, sizeof(d->password), "%s", password);
        d->have_password = 1;
    }
    pthread_mutex_init(&d->lock, NULL);
    if (pthread_create(&d->thread, NULL, worker, d) != 0) {
        pthread_mutex_destroy(&d->lock);
        free(d);
        return NULL;
    }
    return d;
}

int discover_poll(discover *d, c64_device *out, int *count, int *progress)
{
    pthread_mutex_lock(&d->lock);
    memcpy(out, d->found, sizeof(d->found[0]) * (size_t)d->count);
    *count = d->count;
    *progress = d->progress;
    int done = d->done;
    pthread_mutex_unlock(&d->lock);
    return done;
}

void discover_stop(discover *d)
{
    if (!d)
        return;
    pthread_mutex_lock(&d->lock);
    d->quit = 1;
    pthread_mutex_unlock(&d->lock);
    pthread_join(d->thread, NULL);
    pthread_mutex_destroy(&d->lock);
    free(d);
}
