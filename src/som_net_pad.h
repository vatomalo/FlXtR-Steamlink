/* Network player 2: a controller on another device of the same Tailscale
 * network drives SNES pad 2 (the girl, once the game gives pad 2 a hero).
 *
 * UDP port SOM_NET_PAD_PORT, 8-byte packets:
 *   'S' '2' version(1) flags(0) buttons(LE16, Libretro joypad id bits) seq(LE16)
 * Accepted only from Tailscale addresses (100.64.0.0/10) and localhost.
 * One player at a time: the first sender owns pad 2 until it is silent for
 * SOM_NET_PAD_OWNER_MS; a silent owner's buttons are released after
 * SOM_NET_PAD_RELEASE_MS, so a dropped phone never leaves a button held.
 * No I/O beyond the socket; times come from the caller. */
#ifndef FLXTR_SOM_NET_PAD_H
#define FLXTR_SOM_NET_PAD_H
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SOM_NET_PAD_PORT 43931u
#define SOM_NET_PAD_RELEASE_MS 500u
#define SOM_NET_PAD_OWNER_MS 3000u

typedef struct {
    int fd;
    uint32_t owner_ip;            /* host order; 0 = nobody */
    uint16_t owner_port;
    uint16_t buttons;
    uint16_t seq;
    uint64_t last_ms;
    int joined;                   /* set once per new owner (for the overlay) */
} SomNetPad;

static inline int som_net_pad_allowed(uint32_t ip) {
    return (ip & 0xFFC00000u) == 0x64400000u ||   /* 100.64.0.0/10 Tailscale */
           ip == 0x7F000001u;                      /* localhost */
}

static inline int som_net_pad_open(SomNetPad *p, uint16_t port) {
    memset(p, 0, sizeof(*p));
    p->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (p->fd < 0) return 0;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    const int flags = fcntl(p->fd, F_GETFL, 0);
    if (flags < 0 || fcntl(p->fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
        bind(p->fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        close(p->fd);
        p->fd = -1;
        return 0;
    }
    return 1;
}

static inline void som_net_pad_close(SomNetPad *p) {
    if (p->fd >= 0) close(p->fd);
    memset(p, 0, sizeof(*p));
    p->fd = -1;
}

/* One packet from (ip, port) at now_ms. Returns 1 when it was accepted. */
static inline int som_net_pad_feed(SomNetPad *p, const uint8_t *data, size_t length,
                                   uint32_t ip, uint16_t port, uint64_t now_ms) {
    if (length != 8 || data[0] != 'S' || data[1] != '2' || data[2] != 1 ||
        !som_net_pad_allowed(ip))
        return 0;
    const int owned = p->owner_ip && now_ms - p->last_ms < SOM_NET_PAD_OWNER_MS;
    if (owned && (ip != p->owner_ip || port != p->owner_port)) return 0;
    const uint16_t seq = (uint16_t)(data[6] | data[7] << 8);
    const uint16_t ahead = (uint16_t)(seq - p->seq);
    if (owned && (ahead == 0 || ahead > 0x8000u)) return 0;   /* repeated or reordered */
    if (!owned) { p->owner_ip = ip; p->owner_port = port; p->joined = 1; }
    p->seq = seq;
    /* B..R, never Select (a RetroArch hotkey on this setup). */
    p->buttons = (uint16_t)(data[4] | data[5] << 8) & 0x0FFBu;
    p->last_ms = now_ms;
    return 1;
}

/* Drain the socket. */
static inline void som_net_pad_poll(SomNetPad *p, uint64_t now_ms) {
    if (p->fd < 0) return;
    for (int i = 0; i < 64; i++) {
        uint8_t data[16];
        struct sockaddr_in from;
        socklen_t size = sizeof(from);
        const ssize_t n = recvfrom(p->fd, data, sizeof(data), 0, (struct sockaddr *)&from, &size);
        if (n < 0) break;
        som_net_pad_feed(p, data, (size_t)n, ntohl(from.sin_addr.s_addr),
                         ntohs(from.sin_port), now_ms);
    }
}

/* Buttons pad 2 should see now (0 when the owner went quiet). */
static inline uint16_t som_net_pad_buttons(const SomNetPad *p, uint64_t now_ms) {
    if (!p->owner_ip || now_ms - p->last_ms >= SOM_NET_PAD_RELEASE_MS) return 0;
    return p->buttons;
}

static inline int som_net_pad_active(const SomNetPad *p, uint64_t now_ms) {
    return p->owner_ip && now_ms - p->last_ms < SOM_NET_PAD_OWNER_MS;
}
#endif
