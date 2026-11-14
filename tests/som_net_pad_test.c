/* Network player 2: who may join, ownership, stale packets, release. */
#include <assert.h>
#include <stdio.h>
#include "../src/som_net_pad.h"

static void packet(uint8_t out[8], uint16_t buttons, uint16_t seq) {
    out[0] = 'S'; out[1] = '2'; out[2] = 1; out[3] = 0;
    out[4] = (uint8_t)buttons; out[5] = (uint8_t)(buttons >> 8);
    out[6] = (uint8_t)seq; out[7] = (uint8_t)(seq >> 8);
}

int main(void) {
    SomNetPad p;
    memset(&p, 0, sizeof(p)); p.fd = -1;
    uint8_t d[8];
    const uint32_t phone = 0x64451207u;      /* 100.69.18.7 */
    const uint32_t other = 0x64451208u;
    packet(d, 1u << 0, 1);
    assert(!som_net_pad_feed(&p, d, 8, 0xC0A80105u, 5000, 0));    /* LAN 192.168: no */
    assert(som_net_pad_allowed(0x7F000001u) && !som_net_pad_allowed(0x64800001u));
    assert(som_net_pad_feed(&p, d, 8, phone, 5000, 10) && p.joined);
    assert(som_net_pad_buttons(&p, 20) == 1u);
    packet(d, 1u << 8, 2);
    assert(!som_net_pad_feed(&p, d, 8, other, 6000, 30));          /* pad 2 taken */
    packet(d, 1u << 4, 1);
    assert(!som_net_pad_feed(&p, d, 8, phone, 5000, 40));          /* stale seq */
    packet(d, (1u << 2) | (1u << 9), 3);
    assert(som_net_pad_feed(&p, d, 8, phone, 5000, 50));
    assert(som_net_pad_buttons(&p, 60) == (1u << 9));               /* no Select */
    assert(som_net_pad_buttons(&p, 50 + SOM_NET_PAD_RELEASE_MS) == 0); /* went quiet */
    assert(som_net_pad_active(&p, 50 + SOM_NET_PAD_RELEASE_MS));
    p.joined = 0;
    packet(d, 1u << 8, 9);
    assert(som_net_pad_feed(&p, d, 8, other, 6000, 50 + SOM_NET_PAD_OWNER_MS));
    assert(p.owner_ip == other && p.joined);                       /* free again */
    d[0] = 'X';
    assert(!som_net_pad_feed(&p, d, 8, other, 6000, 4000));
    assert(!som_net_pad_feed(&p, d, 7, other, 6000, 4000));
    /* A real socket on a free port: bind, drain empty, close. */
    SomNetPad live;
    assert(som_net_pad_open(&live, 0));
    som_net_pad_poll(&live, 0);
    assert(!som_net_pad_active(&live, 0));
    som_net_pad_close(&live);
    puts("PASS: network player 2 pad");
    return 0;
}
