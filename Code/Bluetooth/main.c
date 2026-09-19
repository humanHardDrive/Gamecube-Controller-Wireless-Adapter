/**
 * @file main.c
 * @brief Demo of the RNBD350 HCI client running against the hardware stub.
 *
 * This exercises: init (includes the mandatory HCI_Reset) -> read the local
 * BD_ADDR -> scan for a peer -> connect to whichever device the scan finds
 * (this also negotiates the ATT MTU) -> GATT read/write by handle ->
 * disconnect. No peer address is hardcoded anywhere in this file; it always
 * comes from the module itself (a scan report). Swap rnbd350_hw_stub.c for
 * a real MCU transport and this file is unchanged.
 *
 * Pairing/bonding is not implemented by this client yet (see CLAUDE.md).
 */

#include "rnbd350.h"

#include <stdio.h>
#include <string.h>

/* Context threaded through the event callback so it can hand discovered
 * peers back to main()'s flow. */
typedef struct {
    bool              have_peer;
    rnbd350_bd_addr_t peer;
} demo_ctx_t;

static void on_event(const rnbd350_event_t *evt, void *ctx_ptr)
{
    demo_ctx_t *ctx = (demo_ctx_t *)ctx_ptr;

    switch (evt->type) {
        case RNBD350_EVT_SCAN_REPORT:
            printf("[event] scan report: name=\"%s\" rssi=%d\n", evt->scan.name, evt->scan.rssi);
            if (!ctx->have_peer) {
                /* Take the first device the scan finds as the connect
                 * target, instead of a hardcoded address. */
                ctx->peer = evt->scan.addr;
                ctx->have_peer = true;
            }
            break;
        case RNBD350_EVT_CONNECTED:
            printf("[event] connected, handle=0x%04X\n", evt->conn_handle);
            break;
        case RNBD350_EVT_CONNECT_FAILED:
            printf("[event] connect failed, HCI status=0x%02X\n", evt->hci_status);
            break;
        case RNBD350_EVT_DISCONNECTED:
            printf("[event] disconnected, handle=0x%04X, reason=0x%02X\n",
                   evt->conn_handle, evt->disconnect_reason);
            break;
        default:
            break;
    }
}

int main(void)
{
    rnbd350_client_t client;
    demo_ctx_t ctx = {0};
    rnbd350_status_t st;

    st = rnbd350_init(&client);
    printf("init: %s\n", rnbd350_status_str(st));
    if (st != RNBD350_OK) {
        fprintf(stderr, "init failed: %s\n", rnbd350_status_str(st));
        return 1;
    }
    rnbd350_set_event_callback(&client, on_event, &ctx);

    rnbd350_bd_addr_t local_addr;
    st = rnbd350_get_bd_addr(&client, &local_addr);
    if (st == RNBD350_OK) {
        printf("local BD_ADDR: %02X:%02X:%02X:%02X:%02X:%02X\n",
               local_addr.mac[5], local_addr.mac[4], local_addr.mac[3],
               local_addr.mac[2], local_addr.mac[1], local_addr.mac[0]);
    } else {
        printf("get_bd_addr: %s\n", rnbd350_status_str(st));
    }

    /* --- Discover a peer by scanning, rather than a hardcoded address --- */
    st = rnbd350_scan_start(&client);
    printf("scan_start: %s\n", rnbd350_status_str(st));
    rnbd350_poll(&client); /* pick up scan report event(s) */

    st = rnbd350_scan_stop(&client);
    printf("scan_stop: %s\n", rnbd350_status_str(st));

    if (!ctx.have_peer) {
        fprintf(stderr, "no peer discovered during scan; nothing to connect to\n");
        goto deinit;
    }

    /* --- Connect to the discovered peer --- */
    st = rnbd350_connect(&client, &ctx.peer);
    printf("connect: %s\n", rnbd350_status_str(st));
    rnbd350_poll(&client); /* pick up the CONNECTED event (ATT MTU exchange happens inside this call) */

    if (!rnbd350_is_connected(&client)) {
        goto deinit;
    }
    printf("negotiated ATT MTU: %u\n", (unsigned)rnbd350_att_mtu(&client));

    /* --- Use the link --- */
    uint8_t value[8];
    size_t value_len = 0;
    st = rnbd350_gatt_read_handle(&client, 0x001A, value, sizeof(value), &value_len);
    printf("gatt_read_handle(0x001A): %s, %zu byte(s)\n", rnbd350_status_str(st), value_len);

    uint8_t new_value[] = {0x64};
    st = rnbd350_gatt_write_handle(&client, 0x001A, new_value, sizeof(new_value));
    printf("gatt_write_handle(0x001A): %s\n", rnbd350_status_str(st));

    st = rnbd350_disconnect(&client);
    printf("disconnect: %s\n", rnbd350_status_str(st));
    rnbd350_poll(&client); /* pick up the DISCONNECTED event */

deinit:
    rnbd350_deinit(&client);
    return 0;
}
