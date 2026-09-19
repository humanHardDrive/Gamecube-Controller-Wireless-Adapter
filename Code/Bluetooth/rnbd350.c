/**
 * @file rnbd350.c
 * @brief Implementation of the RNBD350 client API declared in rnbd350.h.
 *
 * All hardware access goes through rnbd350_hw.h. This file speaks H4 UART
 * framing (HCI Command/ACL Data/Event packets) plus the small slice of
 * standard Bluetooth HCI/ATT needed for scan/connect/GATT-by-handle; it is
 * portable to any MCU that has an rnbd350_hw.h implementation. No string
 * formatting or hex-ASCII codec is used anywhere in this file - every
 * multi-byte field is encoded/decoded with the put_le16/get_le16 helpers
 * below, deliberately not via `packed` struct overlays (alignment/strict-
 * aliasing hazards on arbitrary embedded targets).
 */

#include "rnbd350.h"
#include "rnbd350_hw.h"

#include <string.h>

#define RNBD350_DEFAULT_TIMEOUT_MS 1000u

/* H4 packet type byte. The client only ever receives Event and ACL Data
 * packets from the controller (Command packets are host->controller only). */
#define H4_TYPE_COMMAND 0x01u
#define H4_TYPE_ACL     0x02u
#define H4_TYPE_EVENT   0x04u

/* HCI command opcodes = (OGF << 10) | OCF, per the Bluetooth Core Spec. */
#define HCI_OP_DISCONNECT              0x0406u /* OGF 0x01 / OCF 0x0006 */
#define HCI_OP_RESET                   0x0C03u /* OGF 0x03 / OCF 0x0003 */
#define HCI_OP_READ_BD_ADDR            0x1009u /* OGF 0x04 / OCF 0x0009 */
#define HCI_OP_LE_SET_SCAN_PARAMETERS  0x200Bu /* OGF 0x08 / OCF 0x000B */
#define HCI_OP_LE_SET_SCAN_ENABLE      0x200Cu /* OGF 0x08 / OCF 0x000C */
#define HCI_OP_LE_CREATE_CONNECTION    0x200Du /* OGF 0x08 / OCF 0x000D */

/* HCI event codes. */
#define HCI_EVT_DISCONNECTION_COMPLETE 0x05u
#define HCI_EVT_COMMAND_COMPLETE       0x0Eu
#define HCI_EVT_COMMAND_STATUS         0x0Fu
#define HCI_EVT_LE_META                0x3Eu

/* LE Meta Event subevent codes. */
#define HCI_LE_SUBEVT_CONNECTION_COMPLETE  0x01u
#define HCI_LE_SUBEVT_ADVERTISING_REPORT   0x02u

/* Fixed L2CAP channel IDs used over LE ACL links. Only ATT is handled here;
 * SMP (0x0006) is dropped - pairing is not implemented by this client yet. */
#define L2CAP_CID_ATT 0x0004u

/* ATT protocol opcodes. */
#define ATT_OP_ERROR_RESPONSE          0x01u
#define ATT_OP_EXCHANGE_MTU_REQUEST    0x02u
#define ATT_OP_EXCHANGE_MTU_RESPONSE   0x03u
#define ATT_OP_READ_REQUEST            0x0Au
#define ATT_OP_READ_RESPONSE           0x0Bu
#define ATT_OP_WRITE_REQUEST           0x12u
#define ATT_OP_WRITE_RESPONSE          0x13u
#define ATT_OP_HANDLE_VALUE_NOTIFICATION 0x1Bu

/* rnbd350_client_t.rx.state values. */
enum {
    RX_WAIT_TYPE = 0,
    RX_WAIT_HEADER,
    RX_WAIT_BODY,
    RX_SKIP_BODY /* body_want exceeds our buffer: drain and drop the packet */
};

/* ------------------------------------------------------------------ */
/* Forward declarations (definitions follow in dependency order below) */
/* ------------------------------------------------------------------ */

static inline void     put_le16(uint8_t *p, uint16_t v);
static inline uint16_t get_le16(const uint8_t *p);

static void hci_rx_feed_byte(rnbd350_client_t *client, uint8_t b);
static void hci_rx_pump(rnbd350_client_t *client);
static void hci_rx_dispatch_packet(rnbd350_client_t *client);
static void hci_dispatch_event(rnbd350_client_t *client, uint8_t event_code, const uint8_t *p, uint8_t len);
static void hci_dispatch_le_meta(rnbd350_client_t *client, const uint8_t *p, uint8_t len);
static void hci_dispatch_acl(rnbd350_client_t *client, uint16_t handle, const uint8_t *l2cap, uint16_t len);

static rnbd350_status_t hci_send_command(rnbd350_client_t *client, uint16_t opcode,
                                          const uint8_t *params, uint8_t param_len);
static rnbd350_status_t hci_wait_command_done(rnbd350_client_t *client, uint32_t timeout_ms);
static rnbd350_status_t hci_run_command(rnbd350_client_t *client, uint16_t opcode,
                                         const uint8_t *params, uint8_t param_len);

static rnbd350_status_t acl_send_att(rnbd350_client_t *client, const uint8_t *pdu, uint16_t pdu_len);
static rnbd350_status_t att_run_request(rnbd350_client_t *client, uint8_t req_opcode,
                                         const uint8_t *pdu, uint16_t pdu_len, uint32_t timeout_ms);
static void exchange_mtu(rnbd350_client_t *client);

/* ------------------------------------------------------------------ */
/* Little-endian encode/decode helpers                                 */
/* ------------------------------------------------------------------ */

static inline void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static inline uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* ------------------------------------------------------------------ */
/* H4 receive state machine                                             */
/* ------------------------------------------------------------------ */

/* Feed one received byte through the H4 framing state machine. On a fully
 * assembled Event or ACL Data packet, dispatches it and resets to
 * RX_WAIT_TYPE *before* dispatching, so that a dispatch handler is free to
 * pump more bytes (see exchange_mtu()) without corrupting in-progress
 * framing state. */
static void hci_rx_feed_byte(rnbd350_client_t *client, uint8_t b)
{
    switch (client->rx.state) {
    case RX_WAIT_TYPE:
        client->rx.pkt_type = b;
        client->rx.hdr_len = 0;
        if (b == H4_TYPE_EVENT) {
            client->rx.hdr_want = 2;
        } else if (b == H4_TYPE_ACL) {
            client->rx.hdr_want = 4;
        } else {
            return; /* unrecognized type byte: drop, stay resynced */
        }
        client->rx.state = RX_WAIT_HEADER;
        break;

    case RX_WAIT_HEADER:
        client->rx.hdr[client->rx.hdr_len++] = b;
        if (client->rx.hdr_len < client->rx.hdr_want) {
            break;
        }
        if (client->rx.pkt_type == H4_TYPE_EVENT) {
            client->rx.body_want = client->rx.hdr[1]; /* param_len */
        } else {
            client->rx.body_want = get_le16(&client->rx.hdr[2]); /* ACL total_len */
        }
        client->rx.body_len = 0;
        if (client->rx.body_want == 0) {
            client->rx.state = RX_WAIT_TYPE;
            hci_rx_dispatch_packet(client);
        } else if (client->rx.body_want > sizeof(client->rx.body)) {
            client->rx.state = RX_SKIP_BODY;
        } else {
            client->rx.state = RX_WAIT_BODY;
        }
        break;

    case RX_WAIT_BODY:
        client->rx.body[client->rx.body_len++] = b;
        if (client->rx.body_len == client->rx.body_want) {
            client->rx.state = RX_WAIT_TYPE;
            hci_rx_dispatch_packet(client);
        }
        break;

    case RX_SKIP_BODY:
        client->rx.body_len++;
        if (client->rx.body_len == client->rx.body_want) {
            client->rx.state = RX_WAIT_TYPE;
        }
        break;

    default:
        client->rx.state = RX_WAIT_TYPE;
        break;
    }
}

static void hci_rx_pump(rnbd350_client_t *client)
{
    uint8_t chunk[32];
    int n;
    while ((n = rnbd350_hw_uart_read(chunk, sizeof(chunk))) > 0) {
        for (int i = 0; i < n; ++i) {
            hci_rx_feed_byte(client, chunk[i]);
        }
    }
}

/* client->rx.hdr/body hold a just-completed packet; rx.state has already
 * been reset to RX_WAIT_TYPE by the caller (see hci_rx_feed_byte). */
static void hci_rx_dispatch_packet(rnbd350_client_t *client)
{
    if (client->rx.pkt_type == H4_TYPE_EVENT) {
        uint8_t event_code = client->rx.hdr[0];
        hci_dispatch_event(client, event_code, client->rx.body, (uint8_t)client->rx.body_len);
    } else if (client->rx.pkt_type == H4_TYPE_ACL) {
        uint16_t handle_flags = get_le16(&client->rx.hdr[0]);
        uint16_t handle = (uint16_t)(handle_flags & 0x0FFFu);
        hci_dispatch_acl(client, handle, client->rx.body, (uint16_t)client->rx.body_len);
    }
}

/* ------------------------------------------------------------------ */
/* HCI event dispatch                                                   */
/* ------------------------------------------------------------------ */

static void hci_dispatch_event(rnbd350_client_t *client, uint8_t event_code, const uint8_t *p, uint8_t len)
{
    switch (event_code) {
    case HCI_EVT_COMMAND_COMPLETE: {
        if (len < 3) return;
        uint16_t opcode = get_le16(&p[1]);
        if (client->cmd_pending && opcode == client->cmd_pending_opcode) {
            client->cmd_pending = false;
            size_t ret_len = (size_t)len - 3u;
            if (ret_len > sizeof(client->cmd_return_params)) {
                ret_len = sizeof(client->cmd_return_params);
            }
            memcpy(client->cmd_return_params, &p[3], ret_len);
            client->cmd_return_len = ret_len;
            client->cmd_status = (ret_len > 0) ? client->cmd_return_params[0] : 0;
        }
        break;
    }
    case HCI_EVT_COMMAND_STATUS: {
        if (len < 4) return;
        uint8_t status = p[0];
        uint16_t opcode = get_le16(&p[2]);
        if (client->cmd_pending && opcode == client->cmd_pending_opcode) {
            client->cmd_pending = false;
            client->cmd_status = status;
            client->cmd_return_len = 0;
        }
        break;
    }
    case HCI_EVT_DISCONNECTION_COMPLETE: {
        if (len < 4) return;
        uint8_t status = p[0];
        uint16_t handle = get_le16(&p[1]);
        uint8_t reason = p[3];
        if (status == 0) {
            client->connected = false;
            rnbd350_event_t evt;
            memset(&evt, 0, sizeof(evt));
            evt.type = RNBD350_EVT_DISCONNECTED;
            evt.conn_handle = handle;
            evt.disconnect_reason = reason;
            if (client->event_cb) {
                client->event_cb(&evt, client->event_cb_ctx);
            }
        }
        break;
    }
    case HCI_EVT_LE_META:
        hci_dispatch_le_meta(client, p, len);
        break;
    default:
        break;
    }
}

static void hci_dispatch_le_meta(rnbd350_client_t *client, const uint8_t *p, uint8_t len)
{
    if (len < 1) return;
    uint8_t subevent = p[0];
    const uint8_t *sp = p + 1;
    uint8_t slen = (uint8_t)(len - 1);

    if (subevent == HCI_LE_SUBEVT_CONNECTION_COMPLETE) {
        if (slen < 18) return;

        uint8_t status = sp[0];
        uint16_t handle = get_le16(&sp[1]);
        uint8_t peer_addr_type = sp[4];
        uint8_t peer_mac[6];
        memcpy(peer_mac, &sp[5], 6);

        if (status != 0) {
            rnbd350_event_t evt;
            memset(&evt, 0, sizeof(evt));
            evt.type = RNBD350_EVT_CONNECT_FAILED;
            evt.hci_status = status;
            if (client->event_cb) {
                client->event_cb(&evt, client->event_cb_ctx);
            }
            return;
        }

        client->connected = true;
        client->conn_handle = handle;
        client->att_mtu = 23; /* ATT spec default until Exchange MTU completes */

        /* Documented, one-time exception to rnbd350_poll()'s non-blocking
         * contract: negotiate MTU now so RNBD350_EVT_CONNECTED always
         * reports a valid rnbd350_att_mtu(). This may pump the receive path
         * re-entrantly, which is why peer_mac/handle were copied out of
         * client->rx.body above before calling it - that buffer is reused
         * for whatever packet arrives during the nested pump. */
        exchange_mtu(client);

        rnbd350_event_t evt;
        memset(&evt, 0, sizeof(evt));
        evt.type = RNBD350_EVT_CONNECTED;
        evt.conn_handle = handle;
        evt.scan.addr.type = (peer_addr_type == 0) ? RNBD350_ADDR_PUBLIC : RNBD350_ADDR_RANDOM;
        memcpy(evt.scan.addr.mac, peer_mac, 6);
        if (client->event_cb) {
            client->event_cb(&evt, client->event_cb_ctx);
        }
        return;
    }

    if (subevent == HCI_LE_SUBEVT_ADVERTISING_REPORT) {
        if (slen < 1) return;
        uint8_t num_reports = sp[0];

        const uint8_t *event_type_arr = sp + 1;
        const uint8_t *addr_type_arr  = event_type_arr + num_reports;
        const uint8_t *addr_arr       = addr_type_arr + num_reports;
        const uint8_t *len_data_arr   = addr_arr + (size_t)num_reports * 6u;
        const uint8_t *data_cursor    = len_data_arr + num_reports;

        size_t total_data = 0;
        for (uint8_t i = 0; i < num_reports; ++i) {
            total_data += len_data_arr[i];
        }
        const uint8_t *rssi_arr = data_cursor + total_data;

        const uint8_t *d = data_cursor;
        for (uint8_t i = 0; i < num_reports; ++i) {
            uint8_t ad_len = len_data_arr[i];

            rnbd350_scan_result_t scan;
            memset(&scan, 0, sizeof(scan));
            scan.addr.type = (addr_type_arr[i] == 0) ? RNBD350_ADDR_PUBLIC : RNBD350_ADDR_RANDOM;
            memcpy(scan.addr.mac, &addr_arr[(size_t)i * 6u], 6);
            scan.rssi = (int8_t)rssi_arr[i];

            /* Walk AD structures [len][type][data...] for the Complete
             * (0x09) or Shortened (0x08) Local Name. */
            const uint8_t *ad = d;
            size_t remaining = ad_len;
            while (remaining >= 2) {
                uint8_t s_len = ad[0];
                if (s_len == 0 || (size_t)s_len > remaining - 1) break;
                uint8_t ad_type = ad[1];
                if (ad_type == 0x09 || ad_type == 0x08) {
                    size_t name_len = (size_t)s_len - 1u;
                    if (name_len > sizeof(scan.name) - 1u) {
                        name_len = sizeof(scan.name) - 1u;
                    }
                    memcpy(scan.name, &ad[2], name_len);
                    scan.name[name_len] = '\0';
                    if (ad_type == 0x09) break; /* prefer the complete name */
                }
                ad += (size_t)s_len + 1u;
                remaining -= (size_t)s_len + 1u;
            }

            rnbd350_event_t evt;
            memset(&evt, 0, sizeof(evt));
            evt.type = RNBD350_EVT_SCAN_REPORT;
            evt.scan = scan;
            if (client->event_cb) {
                client->event_cb(&evt, client->event_cb_ctx);
            }

            d += ad_len;
        }
        return;
    }
}

static void hci_dispatch_acl(rnbd350_client_t *client, uint16_t handle, const uint8_t *l2cap, uint16_t len)
{
    (void)handle; /* single-link client: no need to demux by conn handle yet */

    if (len < 4) return;
    uint16_t l2cap_len = get_le16(&l2cap[0]);
    uint16_t cid = get_le16(&l2cap[2]);
    if (cid != L2CAP_CID_ATT) {
        return; /* SMP etc: out of scope, phase 2 */
    }
    if ((uint32_t)4 + l2cap_len > len) {
        return; /* malformed */
    }

    const uint8_t *pdu = &l2cap[4];
    uint16_t pdu_len = l2cap_len;
    if (pdu_len == 0) return;

    uint8_t opcode = pdu[0];

    if (opcode == ATT_OP_ERROR_RESPONSE) {
        if (client->att_pending) {
            client->att_pending = false;
            size_t copy_len = (pdu_len > sizeof(client->att_resp)) ? sizeof(client->att_resp) : pdu_len;
            memcpy(client->att_resp, pdu, copy_len);
            client->att_resp_len = copy_len;
            client->last_att_error = (pdu_len >= 5) ? pdu[4] : 0;
        }
        return;
    }

    if (opcode == ATT_OP_HANDLE_VALUE_NOTIFICATION) {
        return; /* no subscription API in phase 1: unsolicited, dropped */
    }

    uint8_t expected = 0;
    switch (client->att_pending_opcode) {
        case ATT_OP_EXCHANGE_MTU_REQUEST: expected = ATT_OP_EXCHANGE_MTU_RESPONSE; break;
        case ATT_OP_READ_REQUEST:         expected = ATT_OP_READ_RESPONSE;         break;
        case ATT_OP_WRITE_REQUEST:        expected = ATT_OP_WRITE_RESPONSE;        break;
        default: break;
    }
    if (client->att_pending && opcode == expected) {
        client->att_pending = false;
        size_t copy_len = (pdu_len > sizeof(client->att_resp)) ? sizeof(client->att_resp) : pdu_len;
        memcpy(client->att_resp, pdu, copy_len);
        client->att_resp_len = copy_len;
    }
}

/* ------------------------------------------------------------------ */
/* HCI command layer                                                    */
/* ------------------------------------------------------------------ */

static rnbd350_status_t hci_send_command(rnbd350_client_t *client, uint16_t opcode,
                                          const uint8_t *params, uint8_t param_len)
{
    if (client->cmd_pending) {
        return RNBD350_ERR_BUSY;
    }

    uint8_t pkt[1 + 2 + 1 + 32];
    if (param_len > 32) {
        return RNBD350_ERR_INVALID_ARG;
    }
    pkt[0] = H4_TYPE_COMMAND;
    put_le16(&pkt[1], opcode);
    pkt[3] = param_len;
    if (param_len > 0) {
        memcpy(&pkt[4], params, param_len);
    }

    size_t pkt_len = 4u + param_len;
    int written = rnbd350_hw_uart_write(pkt, pkt_len);
    if (written < 0 || (size_t)written != pkt_len) {
        return RNBD350_ERR_IO;
    }

    client->cmd_pending = true;
    client->cmd_pending_opcode = opcode;
    client->cmd_return_len = 0;
    return RNBD350_OK;
}

static rnbd350_status_t hci_wait_command_done(rnbd350_client_t *client, uint32_t timeout_ms)
{
    uint32_t start = rnbd350_hw_millis();
    while (client->cmd_pending) {
        hci_rx_pump(client);
        if (!client->cmd_pending) {
            break;
        }
        if ((rnbd350_hw_millis() - start) >= timeout_ms) {
            client->cmd_pending = false;
            return RNBD350_ERR_TIMEOUT;
        }
    }
    return RNBD350_OK;
}

static rnbd350_status_t hci_run_command(rnbd350_client_t *client, uint16_t opcode,
                                         const uint8_t *params, uint8_t param_len)
{
    rnbd350_status_t st = hci_send_command(client, opcode, params, param_len);
    if (st != RNBD350_OK) {
        return st;
    }
    st = hci_wait_command_done(client, client->response_timeout_ms);
    if (st != RNBD350_OK) {
        return st;
    }
    if (client->cmd_status != 0) {
        client->last_hci_status = client->cmd_status;
        return RNBD350_ERR_HCI;
    }
    return RNBD350_OK;
}

/* ------------------------------------------------------------------ */
/* ATT layer                                                            */
/* ------------------------------------------------------------------ */

static rnbd350_status_t acl_send_att(rnbd350_client_t *client, const uint8_t *pdu, uint16_t pdu_len)
{
    if (pdu_len > RNBD350_ATT_RX_MTU) {
        return RNBD350_ERR_INVALID_ARG;
    }

    uint8_t pkt[9 + RNBD350_ATT_RX_MTU]; /* H4 type(1) + ACL hdr(4) + L2CAP hdr(4) + pdu */
    uint16_t l2cap_len = pdu_len;
    uint16_t total_len = (uint16_t)(4u + l2cap_len); /* L2CAP header + pdu */
    uint16_t handle_flags = (uint16_t)(0x2000u | (client->conn_handle & 0x0FFFu)); /* PB=first, BC=point-to-point */

    pkt[0] = H4_TYPE_ACL;
    put_le16(&pkt[1], handle_flags);
    put_le16(&pkt[3], total_len);
    put_le16(&pkt[5], l2cap_len);
    put_le16(&pkt[7], L2CAP_CID_ATT);
    if (pdu_len > 0) {
        memcpy(&pkt[9], pdu, pdu_len);
    }

    size_t pkt_len = 9u + pdu_len;
    int written = rnbd350_hw_uart_write(pkt, pkt_len);
    if (written < 0 || (size_t)written != pkt_len) {
        return RNBD350_ERR_IO;
    }
    return RNBD350_OK;
}

static rnbd350_status_t att_run_request(rnbd350_client_t *client, uint8_t req_opcode,
                                         const uint8_t *pdu, uint16_t pdu_len, uint32_t timeout_ms)
{
    if (client->att_pending) {
        return RNBD350_ERR_BUSY;
    }

    rnbd350_status_t st = acl_send_att(client, pdu, pdu_len);
    if (st != RNBD350_OK) {
        return st;
    }

    client->att_pending = true;
    client->att_pending_opcode = req_opcode;
    client->att_resp_len = 0;

    uint32_t start = rnbd350_hw_millis();
    while (client->att_pending) {
        hci_rx_pump(client);
        if (!client->att_pending) {
            break;
        }
        if ((rnbd350_hw_millis() - start) >= timeout_ms) {
            client->att_pending = false;
            return RNBD350_ERR_TIMEOUT;
        }
    }

    if (client->att_resp_len > 0 && client->att_resp[0] == ATT_OP_ERROR_RESPONSE) {
        return RNBD350_ERR_ATT;
    }
    return RNBD350_OK;
}

/* Internal only: negotiate ATT MTU for the link that was just established.
 * Not fatal on failure/timeout - client->att_mtu simply stays at the spec
 * default (23), already set by the caller before invoking this. */
static void exchange_mtu(rnbd350_client_t *client)
{
    uint8_t pdu[3];
    pdu[0] = ATT_OP_EXCHANGE_MTU_REQUEST;
    put_le16(&pdu[1], RNBD350_ATT_RX_MTU);

    rnbd350_status_t st = att_run_request(client, ATT_OP_EXCHANGE_MTU_REQUEST, pdu, sizeof(pdu),
                                           client->response_timeout_ms);
    if (st == RNBD350_OK && client->att_resp_len >= 3 && client->att_resp[0] == ATT_OP_EXCHANGE_MTU_RESPONSE) {
        uint16_t server_mtu = get_le16(&client->att_resp[1]);
        client->att_mtu = (server_mtu < RNBD350_ATT_RX_MTU) ? server_mtu : RNBD350_ATT_RX_MTU;
    }
}

/* ------------------------------------------------------------------ */
/* Public API: lifecycle                                                */
/* ------------------------------------------------------------------ */

rnbd350_status_t rnbd350_init(rnbd350_client_t *client)
{
    if (!client) {
        return RNBD350_ERR_INVALID_ARG;
    }
    memset(client, 0, sizeof(*client));
    client->response_timeout_ms = RNBD350_DEFAULT_TIMEOUT_MS;
    client->att_mtu = 23; /* ATT spec default until a link negotiates higher */

    if (rnbd350_hw_init() != RNBD350_HW_OK) {
        return RNBD350_ERR_IO;
    }

    /* Hold reset briefly then release, matching typical bring-up. */
    rnbd350_hw_set_reset(RNBD350_HW_LOW);
    rnbd350_hw_delay_ms(10);
    rnbd350_hw_set_reset(RNBD350_HW_HIGH);
    rnbd350_hw_set_wake(RNBD350_HW_HIGH);
    rnbd350_hw_delay_ms(25); /* module wake/boot settling time */

    client->initialized = true;

    /* HCI_Reset is the mandatory first command after controller boot; it
     * also doubles as the direct replacement for the old ASCII client's
     * rnbd350_enter_command_mode() as a "prove the link is up" step. */
    rnbd350_status_t st = hci_run_command(client, HCI_OP_RESET, NULL, 0);
    if (st != RNBD350_OK) {
        client->initialized = false;
        return st;
    }
    return RNBD350_OK;
}

void rnbd350_deinit(rnbd350_client_t *client)
{
    if (!client || !client->initialized) {
        return;
    }
    rnbd350_hw_deinit();
    client->initialized = false;
}

void rnbd350_set_event_callback(rnbd350_client_t *client, rnbd350_event_cb_t cb, void *user_ctx)
{
    if (!client) return;
    client->event_cb = cb;
    client->event_cb_ctx = user_ctx;
}

void rnbd350_set_response_timeout(rnbd350_client_t *client, uint32_t timeout_ms)
{
    if (!client) return;
    client->response_timeout_ms = timeout_ms;
}

void rnbd350_poll(rnbd350_client_t *client)
{
    if (!client || !client->initialized) return;
    hci_rx_pump(client);
}

rnbd350_status_t rnbd350_get_bd_addr(rnbd350_client_t *client, rnbd350_bd_addr_t *out_addr)
{
    if (!client || !client->initialized || !out_addr) {
        return RNBD350_ERR_INVALID_ARG;
    }

    rnbd350_status_t st = hci_run_command(client, HCI_OP_READ_BD_ADDR, NULL, 0);
    if (st != RNBD350_OK) {
        return st;
    }
    if (client->cmd_return_len < 7) {
        return RNBD350_ERR_PROTOCOL;
    }
    memcpy(out_addr->mac, &client->cmd_return_params[1], 6);
    out_addr->type = RNBD350_ADDR_PUBLIC; /* local address: type field is not meaningful here */
    return RNBD350_OK;
}

/* ------------------------------------------------------------------ */
/* Public API: GAP                                                      */
/* ------------------------------------------------------------------ */

rnbd350_status_t rnbd350_scan_start(rnbd350_client_t *client)
{
    if (!client || !client->initialized) return RNBD350_ERR_INVALID_ARG;

    uint8_t scan_params[7];
    scan_params[0] = 0x01; /* Scan_Type: active */
    put_le16(&scan_params[1], 0x0060); /* Scan_Interval */
    put_le16(&scan_params[3], 0x0030); /* Scan_Window */
    scan_params[5] = 0x00; /* Own_Address_Type: public */
    scan_params[6] = 0x00; /* Filter_Policy: accept all advertisements */

    rnbd350_status_t st = hci_run_command(client, HCI_OP_LE_SET_SCAN_PARAMETERS,
                                           scan_params, sizeof(scan_params));
    if (st != RNBD350_OK) return st;

    uint8_t scan_enable[2] = { 0x01, 0x00 }; /* enable, no duplicate filtering */
    return hci_run_command(client, HCI_OP_LE_SET_SCAN_ENABLE, scan_enable, sizeof(scan_enable));
}

rnbd350_status_t rnbd350_scan_stop(rnbd350_client_t *client)
{
    if (!client || !client->initialized) return RNBD350_ERR_INVALID_ARG;

    uint8_t scan_enable[2] = { 0x00, 0x00 };
    return hci_run_command(client, HCI_OP_LE_SET_SCAN_ENABLE, scan_enable, sizeof(scan_enable));
}

rnbd350_status_t rnbd350_connect(rnbd350_client_t *client, const rnbd350_bd_addr_t *addr)
{
    if (!client || !client->initialized || !addr) return RNBD350_ERR_INVALID_ARG;

    /* Fixed phase-1 connection parameters (not yet caller-configurable). */
    uint8_t p[25];
    put_le16(&p[0], 0x0010);     /* Scan_Interval */
    put_le16(&p[2], 0x0010);     /* Scan_Window */
    p[4] = 0x00;                 /* Initiator_Filter_Policy: use Peer_Address */
    p[5] = (uint8_t)addr->type;  /* Peer_Address_Type */
    memcpy(&p[6], addr->mac, 6); /* Peer_Address */
    p[12] = 0x00;                /* Own_Address_Type: public */
    put_le16(&p[13], 0x0018);    /* Conn_Interval_Min */
    put_le16(&p[15], 0x0028);    /* Conn_Interval_Max */
    put_le16(&p[17], 0x0000);    /* Conn_Latency */
    put_le16(&p[19], 0x01F4);    /* Supervision_Timeout */
    put_le16(&p[21], 0x0000);    /* Min_CE_Length */
    put_le16(&p[23], 0x0000);    /* Max_CE_Length */

    /* Ack is Command_Status ("connection attempt started"), not
     * Command_Complete; hci_run_command() handles both transparently.
     * RNBD350_EVT_CONNECTED/CONNECT_FAILED follow asynchronously via
     * LE_Connection_Complete, delivered through rnbd350_poll(). */
    return hci_run_command(client, HCI_OP_LE_CREATE_CONNECTION, p, sizeof(p));
}

rnbd350_status_t rnbd350_disconnect(rnbd350_client_t *client)
{
    if (!client || !client->initialized) return RNBD350_ERR_INVALID_ARG;
    if (!client->connected) return RNBD350_ERR_NOT_CONNECTED;

    uint8_t p[3];
    put_le16(&p[0], client->conn_handle);
    p[2] = 0x13; /* Reason: Remote User Terminated Connection */

    /* Ack is Command_Status; RNBD350_EVT_DISCONNECTED follows asynchronously
     * via Disconnection_Complete, delivered through rnbd350_poll(). */
    return hci_run_command(client, HCI_OP_DISCONNECT, p, sizeof(p));
}

bool rnbd350_is_connected(const rnbd350_client_t *client)
{
    return client && client->connected;
}

/* ------------------------------------------------------------------ */
/* Public API: GATT client                                              */
/* ------------------------------------------------------------------ */

rnbd350_status_t rnbd350_gatt_read_handle(rnbd350_client_t *client,
                                           uint16_t handle,
                                           uint8_t *out_buf,
                                           size_t out_buf_len,
                                           size_t *out_len)
{
    if (!client || !client->initialized || !out_buf || !out_len) {
        return RNBD350_ERR_INVALID_ARG;
    }
    if (!client->connected) {
        return RNBD350_ERR_NOT_CONNECTED;
    }

    uint8_t pdu[3];
    pdu[0] = ATT_OP_READ_REQUEST;
    put_le16(&pdu[1], handle);

    rnbd350_status_t st = att_run_request(client, ATT_OP_READ_REQUEST, pdu, sizeof(pdu),
                                           client->response_timeout_ms);
    if (st != RNBD350_OK) {
        return st;
    }
    if (client->att_resp_len < 1 || client->att_resp[0] != ATT_OP_READ_RESPONSE) {
        return RNBD350_ERR_PROTOCOL;
    }

    size_t value_len = client->att_resp_len - 1u;
    if (value_len > out_buf_len) {
        value_len = out_buf_len;
    }
    memcpy(out_buf, &client->att_resp[1], value_len);
    *out_len = value_len;
    return RNBD350_OK;
}

rnbd350_status_t rnbd350_gatt_write_handle(rnbd350_client_t *client,
                                            uint16_t handle,
                                            const uint8_t *data,
                                            size_t len)
{
    if (!client || !client->initialized || (!data && len > 0)) {
        return RNBD350_ERR_INVALID_ARG;
    }
    if (!client->connected) {
        return RNBD350_ERR_NOT_CONNECTED;
    }
    /* No fragmentation support (phase 1): reject values that wouldn't fit a
     * single Write_Request under the negotiated MTU, rather than truncate. */
    if (len > RNBD350_GATT_MAX_VALUE_LEN || len + 3u > client->att_mtu) {
        return RNBD350_ERR_INVALID_ARG;
    }

    uint8_t pdu[3 + RNBD350_GATT_MAX_VALUE_LEN];
    pdu[0] = ATT_OP_WRITE_REQUEST;
    put_le16(&pdu[1], handle);
    if (len > 0) {
        memcpy(&pdu[3], data, len);
    }

    rnbd350_status_t st = att_run_request(client, ATT_OP_WRITE_REQUEST, pdu, (uint16_t)(3u + len),
                                           client->response_timeout_ms);
    if (st != RNBD350_OK) {
        return st;
    }
    return (client->att_resp_len >= 1 && client->att_resp[0] == ATT_OP_WRITE_RESPONSE)
               ? RNBD350_OK : RNBD350_ERR_PROTOCOL;
}

/* ------------------------------------------------------------------ */
/* Public API: misc / diagnostics                                       */
/* ------------------------------------------------------------------ */

uint16_t rnbd350_att_mtu(const rnbd350_client_t *client)
{
    return client ? client->att_mtu : 23;
}

uint8_t rnbd350_last_hci_status(const rnbd350_client_t *client)
{
    return client ? client->last_hci_status : 0;
}

uint8_t rnbd350_last_att_error(const rnbd350_client_t *client)
{
    return client ? client->last_att_error : 0;
}

const char *rnbd350_status_str(rnbd350_status_t status)
{
    switch (status) {
        case RNBD350_OK:                return "OK";
        case RNBD350_ERR_TIMEOUT:       return "timeout";
        case RNBD350_ERR_PROTOCOL:      return "protocol error";
        case RNBD350_ERR_IO:            return "I/O error";
        case RNBD350_ERR_INVALID_ARG:   return "invalid argument";
        case RNBD350_ERR_NOT_CONNECTED: return "not connected";
        case RNBD350_ERR_BUSY:          return "busy";
        case RNBD350_ERR_HCI:           return "controller error";
        case RNBD350_ERR_ATT:           return "ATT error";
        default:                        return "unknown";
    }
}
