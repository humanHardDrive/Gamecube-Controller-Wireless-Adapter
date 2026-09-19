/**
 * @file rnbd350_hw_stub.c
 * @brief STUB implementation of rnbd350_hw.h.
 *
 * This file contains NO real hardware access. It exists so the client
 * library (rnbd350.c) builds and runs on a dev machine / CI, and so the
 * layering between "what talks to silicon" and "what talks binary HCI" is
 * obvious and easy to swap out.
 *
 * It does two things:
 *   1. Logs every "hardware" operation to stderr, so you can see exactly
 *      what the client is asking the transport to do.
 *   2. Runs a tiny fake HCI controller: an H4 packet parser (mirroring the
 *      client's own) that recognizes the handful of standard HCI commands
 *      and ATT PDUs this client sends, and answers with the events/ACL data
 *      a real RNBD350 controller would, so the demo program has something
 *      to talk to.
 *
 * This stub always models an RNBD350 that has already completed its
 * one-time RN (ASCII console) -> HCI mode transition (DS50003684 §5.2.25,
 * §6) - it boots directly ready to parse H4 bytes, with no "$$$"/RN command
 * framing modeled at all. Provisioning a real module into HCI mode, or
 * reverting it via the HCI_VND_Mode_Record_Clear vendor command
 * (OGF 0x3F/OCF 0x0000, sub-opcode 0x08), is out of scope for this library
 * and must happen once, out-of-band, before this code runs against real
 * hardware. (This mirrors how the stub also doesn't model PDS bond
 * persistence across reset - it's a simplified model of the firmware, not
 * the firmware itself.)
 *
 * Replace every function body in this file with real UART/GPIO calls for
 * your target MCU. Nothing in rnbd350.h/.c needs to change.
 */

#include "rnbd350_hw.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ---- little-endian helpers (duplicated from rnbd350.c on purpose: the
 * stub is deliberately independent of the client's internals) ----------- */

static inline void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static inline uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* ---- H4 packet type bytes and the opcodes/PDUs this stub understands -- */

#define H4_TYPE_COMMAND 0x01u
#define H4_TYPE_ACL     0x02u
#define H4_TYPE_EVENT   0x04u

#define HCI_OP_DISCONNECT              0x0406u
#define HCI_OP_RESET                   0x0C03u
#define HCI_OP_READ_BD_ADDR            0x1009u
#define HCI_OP_LE_SET_SCAN_PARAMETERS  0x200Bu
#define HCI_OP_LE_SET_SCAN_ENABLE      0x200Cu
#define HCI_OP_LE_CREATE_CONNECTION    0x200Du

#define HCI_EVT_DISCONNECTION_COMPLETE 0x05u
#define HCI_EVT_COMMAND_COMPLETE       0x0Eu
#define HCI_EVT_COMMAND_STATUS         0x0Fu
#define HCI_EVT_LE_META                0x3Eu

#define HCI_LE_SUBEVT_CONNECTION_COMPLETE 0x01u
#define HCI_LE_SUBEVT_ADVERTISING_REPORT  0x02u

#define L2CAP_CID_ATT 0x0004u

#define ATT_OP_ERROR_RESPONSE        0x01u
#define ATT_OP_EXCHANGE_MTU_REQUEST  0x02u
#define ATT_OP_EXCHANGE_MTU_RESPONSE 0x03u
#define ATT_OP_READ_REQUEST          0x0Au
#define ATT_OP_READ_RESPONSE         0x0Bu
#define ATT_OP_WRITE_REQUEST         0x12u
#define ATT_OP_WRITE_RESPONSE        0x13u

#define ATT_ERR_ATTRIBUTE_NOT_FOUND  0x0Au

/* ---- fake controller state (simulation only, not part of the HCI) ----- */

#define HW_STUB_RX_QUEUE_SIZE 512
#define HW_STUB_BODY_CAP      64

enum { FAKE_RX_WAIT_TYPE = 0, FAKE_RX_WAIT_HEADER, FAKE_RX_WAIT_BODY, FAKE_RX_SKIP_BODY };

static struct {
    int      initialized;
    int      connected;
    int      scanning;
    uint16_t conn_handle;
    uint8_t  local_bd_addr[6]; /* wire order, fixed fake value */
    uint8_t  peer_mac[6];      /* wire order, fixed fake peer */
    uint8_t  peer_addr_type;
    uint16_t peer_rx_mtu;      /* what we report back on Exchange MTU */

    /* H4 parser for bytes arriving from the "host" (the client). */
    struct {
        int      state;
        uint8_t  pkt_type;
        uint8_t  hdr[4];
        size_t   hdr_len, hdr_want;
        uint8_t  body[HW_STUB_BODY_CAP];
        size_t   body_len, body_want;
    } rx;

    uint8_t rx_queue[HW_STUB_RX_QUEUE_SIZE]; /* bytes queued for the host to read */
    size_t  rx_head;
    size_t  rx_tail;
} s_fake;

static void fake_reset(void)
{
    memset(&s_fake, 0, sizeof(s_fake));
    s_fake.initialized = 1;
    s_fake.conn_handle = 0x0040;
    s_fake.peer_rx_mtu = 247;

    static const uint8_t local_addr[6] = { 0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33 };
    memcpy(s_fake.local_bd_addr, local_addr, 6);

    static const uint8_t peer_addr[6] = { 0x55, 0x44, 0x33, 0x22, 0x11, 0x00 };
    memcpy(s_fake.peer_mac, peer_addr, 6);
    s_fake.peer_addr_type = 0; /* public */
}

/* Push raw bytes into the simulated controller's TX queue (i.e. what the
 * "host" will read back via rnbd350_hw_uart_read). */
static void fake_queue_bytes(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        if (s_fake.rx_tail - s_fake.rx_head >= HW_STUB_RX_QUEUE_SIZE) {
            break; /* queue full; drop rather than corrupt (stub only) */
        }
        s_fake.rx_queue[s_fake.rx_tail % HW_STUB_RX_QUEUE_SIZE] = data[i];
        s_fake.rx_tail++;
    }
}

static void fake_queue_command_complete(uint16_t opcode, const uint8_t *ret, uint8_t ret_len)
{
    uint8_t pkt[3 + 3 + 8]; /* H4 hdr(1) + event hdr(2) + num_pkts(1)+opcode(2) + ret */
    pkt[0] = H4_TYPE_EVENT;
    pkt[1] = HCI_EVT_COMMAND_COMPLETE;
    pkt[2] = (uint8_t)(3u + ret_len);
    pkt[3] = 0x01; /* Num_HCI_Command_Packets */
    put_le16(&pkt[4], opcode);
    if (ret_len > 0) {
        memcpy(&pkt[6], ret, ret_len);
    }
    fake_queue_bytes(pkt, (size_t)6u + ret_len);
}

static void fake_queue_command_status(uint16_t opcode, uint8_t status)
{
    uint8_t pkt[3 + 4];
    pkt[0] = H4_TYPE_EVENT;
    pkt[1] = HCI_EVT_COMMAND_STATUS;
    pkt[2] = 0x04;
    pkt[3] = status;
    pkt[4] = 0x01; /* Num_HCI_Command_Packets */
    put_le16(&pkt[5], opcode);
    fake_queue_bytes(pkt, sizeof(pkt));
}

static void fake_queue_le_meta(uint8_t subevent, const uint8_t *params, uint8_t params_len)
{
    uint8_t pkt[3 + 1 + 32];
    pkt[0] = H4_TYPE_EVENT;
    pkt[1] = HCI_EVT_LE_META;
    pkt[2] = (uint8_t)(1u + params_len);
    pkt[3] = subevent;
    if (params_len > 0) {
        memcpy(&pkt[4], params, params_len);
    }
    fake_queue_bytes(pkt, (size_t)4u + params_len);
}

static void fake_queue_disconnection_complete(uint16_t handle, uint8_t reason)
{
    uint8_t pkt[3 + 4];
    pkt[0] = H4_TYPE_EVENT;
    pkt[1] = HCI_EVT_DISCONNECTION_COMPLETE;
    pkt[2] = 0x04;
    pkt[3] = 0x00; /* Status: success */
    put_le16(&pkt[4], handle);
    pkt[6] = reason;
    fake_queue_bytes(pkt, sizeof(pkt));
}

static void fake_queue_acl_att(uint16_t handle, const uint8_t *pdu, uint16_t pdu_len)
{
    uint8_t pkt[9 + 32];
    uint16_t l2cap_len = pdu_len;
    uint16_t total_len = (uint16_t)(4u + l2cap_len);
    uint16_t handle_flags = (uint16_t)(0x2000u | (handle & 0x0FFFu));

    pkt[0] = H4_TYPE_ACL;
    put_le16(&pkt[1], handle_flags);
    put_le16(&pkt[3], total_len);
    put_le16(&pkt[5], l2cap_len);
    put_le16(&pkt[7], L2CAP_CID_ATT);
    memcpy(&pkt[9], pdu, pdu_len);

    fake_queue_bytes(pkt, (size_t)9u + pdu_len);
}

static void fake_queue_att_error(uint16_t handle, uint8_t req_opcode, uint16_t att_handle, uint8_t error_code)
{
    uint8_t resp[5];
    resp[0] = ATT_OP_ERROR_RESPONSE;
    resp[1] = req_opcode;
    put_le16(&resp[2], att_handle);
    resp[4] = error_code;
    fake_queue_acl_att(handle, resp, sizeof(resp));
}

/* Emits one LE Advertising Report for the fixed fake peer, AD data carrying
 * just a Complete Local Name (type 0x09), matching the peer the demo
 * expects to discover. */
static void fake_queue_scan_report(void)
{
    static const char name[] = "FakeSensor";
    uint8_t ad[1 + (sizeof(name) - 1)];
    ad[0] = (uint8_t)sizeof(name); /* AD structure length: type(1) + name */
    ad[1] = 0x09; /* Complete Local Name */
    memcpy(&ad[2], name, sizeof(name) - 1);
    uint8_t ad_len = (uint8_t)sizeof(ad);

    uint8_t params[1 + 1 + 1 + 6 + 1 + sizeof(ad) + 1];
    size_t i = 0;
    params[i++] = 0x01;              /* Num_Reports */
    params[i++] = 0x00;              /* Event_Type[0]: ADV_IND */
    params[i++] = s_fake.peer_addr_type; /* Address_Type[0] */
    memcpy(&params[i], s_fake.peer_mac, 6); i += 6; /* Address[0] */
    params[i++] = ad_len;             /* Length_Data[0] */
    memcpy(&params[i], ad, ad_len); i += ad_len; /* Data[0] */
    params[i++] = (uint8_t)(int8_t)-42; /* RSSI[0] */

    fake_queue_le_meta(HCI_LE_SUBEVT_ADVERTISING_REPORT, params, (uint8_t)i);
}

static void fake_handle_command(uint16_t opcode, const uint8_t *params, uint8_t param_len)
{
    switch (opcode) {
    case HCI_OP_RESET: {
        fake_reset();
        uint8_t ret[1] = { 0x00 };
        fake_queue_command_complete(opcode, ret, sizeof(ret));
        break;
    }
    case HCI_OP_READ_BD_ADDR: {
        uint8_t ret[7];
        ret[0] = 0x00;
        memcpy(&ret[1], s_fake.local_bd_addr, 6);
        fake_queue_command_complete(opcode, ret, sizeof(ret));
        break;
    }
    case HCI_OP_LE_SET_SCAN_PARAMETERS: {
        uint8_t ret[1] = { 0x00 };
        fake_queue_command_complete(opcode, ret, sizeof(ret));
        break;
    }
    case HCI_OP_LE_SET_SCAN_ENABLE: {
        uint8_t ret[1] = { 0x00 };
        fake_queue_command_complete(opcode, ret, sizeof(ret));
        s_fake.scanning = (param_len >= 1 && params[0] == 0x01) ? 1 : 0;
        if (s_fake.scanning) {
            fake_queue_scan_report();
        }
        break;
    }
    case HCI_OP_LE_CREATE_CONNECTION: {
        if (param_len < 25) {
            fake_queue_command_status(opcode, 0x01 /* Unknown HCI Command / bad params */);
            break;
        }
        uint8_t peer_addr_type = params[5];
        memcpy(s_fake.peer_mac, &params[6], 6);
        s_fake.peer_addr_type = peer_addr_type;
        s_fake.connected = 1;

        fake_queue_command_status(opcode, 0x00);

        uint8_t cc[18];
        cc[0] = 0x00; /* Status */
        put_le16(&cc[1], s_fake.conn_handle);
        cc[3] = 0x00; /* Role: central */
        cc[4] = peer_addr_type;
        memcpy(&cc[5], s_fake.peer_mac, 6);
        put_le16(&cc[11], 0x0018); /* Conn_Interval */
        put_le16(&cc[13], 0x0000); /* Conn_Latency */
        put_le16(&cc[15], 0x01F4); /* Supervision_Timeout */
        cc[17] = 0x00;             /* Master_Clock_Accuracy */
        fake_queue_le_meta(HCI_LE_SUBEVT_CONNECTION_COMPLETE, cc, sizeof(cc));
        break;
    }
    case HCI_OP_DISCONNECT: {
        if (param_len < 3) {
            fake_queue_command_status(opcode, 0x01);
            break;
        }
        uint16_t handle = get_le16(&params[0]);
        uint8_t reason = params[2];
        s_fake.connected = 0;
        fake_queue_command_status(opcode, 0x00);
        fake_queue_disconnection_complete(handle, reason);
        break;
    }
    default: {
        /* Real controllers reject an unsupported opcode this way. */
        uint8_t ret[1] = { 0x01 }; /* Unknown HCI Command */
        fake_queue_command_complete(opcode, ret, sizeof(ret));
        break;
    }
    }
}

static void fake_handle_acl(uint16_t handle, const uint8_t *l2cap, uint16_t len)
{
    if (len < 4) return;
    uint16_t l2cap_len = get_le16(&l2cap[0]);
    uint16_t cid = get_le16(&l2cap[2]);
    if (cid != L2CAP_CID_ATT) {
        return; /* SMP (0x0006) etc: correctly out of scope, never sent by phase-1 client */
    }
    if ((uint32_t)4 + l2cap_len > len) return;

    const uint8_t *pdu = &l2cap[4];
    uint16_t pdu_len = l2cap_len;
    if (pdu_len == 0) return;
    uint8_t opcode = pdu[0];

    switch (opcode) {
    case ATT_OP_EXCHANGE_MTU_REQUEST: {
        uint8_t resp[3];
        resp[0] = ATT_OP_EXCHANGE_MTU_RESPONSE;
        put_le16(&resp[1], s_fake.peer_rx_mtu);
        fake_queue_acl_att(handle, resp, sizeof(resp));
        break;
    }
    case ATT_OP_READ_REQUEST: {
        if (pdu_len < 3) return;
        uint16_t att_handle = get_le16(&pdu[1]);
        if (att_handle == 0x001A && s_fake.connected) {
            uint8_t resp[2] = { ATT_OP_READ_RESPONSE, 0x64 }; /* fake characteristic value */
            fake_queue_acl_att(handle, resp, sizeof(resp));
        } else {
            fake_queue_att_error(handle, ATT_OP_READ_REQUEST, att_handle, ATT_ERR_ATTRIBUTE_NOT_FOUND);
        }
        break;
    }
    case ATT_OP_WRITE_REQUEST: {
        if (pdu_len < 3) return;
        uint16_t att_handle = get_le16(&pdu[1]);
        if (s_fake.connected) {
            uint8_t resp[1] = { ATT_OP_WRITE_RESPONSE };
            fake_queue_acl_att(handle, resp, sizeof(resp));
        } else {
            fake_queue_att_error(handle, ATT_OP_WRITE_REQUEST, att_handle, ATT_ERR_ATTRIBUTE_NOT_FOUND);
        }
        break;
    }
    default:
        break; /* unsupported ATT opcode: silently dropped, not exercised by this client */
    }
}

static void fake_dispatch_packet(void)
{
    if (s_fake.rx.pkt_type == H4_TYPE_COMMAND) {
        uint16_t opcode = get_le16(&s_fake.rx.hdr[0]);
        fake_handle_command(opcode, s_fake.rx.body, (uint8_t)s_fake.rx.body_len);
    } else if (s_fake.rx.pkt_type == H4_TYPE_ACL) {
        uint16_t handle_flags = get_le16(&s_fake.rx.hdr[0]);
        uint16_t handle = (uint16_t)(handle_flags & 0x0FFFu);
        fake_handle_acl(handle, s_fake.rx.body, (uint16_t)s_fake.rx.body_len);
    }
}

/* H4 parser for bytes arriving from the "host" - the mirror image of
 * hci_rx_feed_byte() in rnbd350.c, one packet type set earlier (Command
 * instead of Event) since this side receives what the client sends. */
static void fake_feed_byte(uint8_t b)
{
    switch (s_fake.rx.state) {
    case FAKE_RX_WAIT_TYPE:
        s_fake.rx.pkt_type = b;
        s_fake.rx.hdr_len = 0;
        if (b == H4_TYPE_COMMAND) {
            s_fake.rx.hdr_want = 3; /* opcode(2) + param_len(1) */
        } else if (b == H4_TYPE_ACL) {
            s_fake.rx.hdr_want = 4; /* handle_flags(2) + total_len(2) */
        } else {
            return; /* unrecognized type byte: drop, stay resynced */
        }
        s_fake.rx.state = FAKE_RX_WAIT_HEADER;
        break;

    case FAKE_RX_WAIT_HEADER:
        s_fake.rx.hdr[s_fake.rx.hdr_len++] = b;
        if (s_fake.rx.hdr_len < s_fake.rx.hdr_want) break;
        if (s_fake.rx.pkt_type == H4_TYPE_COMMAND) {
            s_fake.rx.body_want = s_fake.rx.hdr[2]; /* param_len */
        } else {
            s_fake.rx.body_want = get_le16(&s_fake.rx.hdr[2]); /* ACL total_len */
        }
        s_fake.rx.body_len = 0;
        if (s_fake.rx.body_want == 0) {
            s_fake.rx.state = FAKE_RX_WAIT_TYPE;
            fake_dispatch_packet();
        } else if (s_fake.rx.body_want > sizeof(s_fake.rx.body)) {
            s_fake.rx.state = FAKE_RX_SKIP_BODY;
        } else {
            s_fake.rx.state = FAKE_RX_WAIT_BODY;
        }
        break;

    case FAKE_RX_WAIT_BODY:
        s_fake.rx.body[s_fake.rx.body_len++] = b;
        if (s_fake.rx.body_len == s_fake.rx.body_want) {
            s_fake.rx.state = FAKE_RX_WAIT_TYPE;
            fake_dispatch_packet();
        }
        break;

    case FAKE_RX_SKIP_BODY:
        s_fake.rx.body_len++;
        if (s_fake.rx.body_len == s_fake.rx.body_want) {
            s_fake.rx.state = FAKE_RX_WAIT_TYPE;
        }
        break;

    default:
        s_fake.rx.state = FAKE_RX_WAIT_TYPE;
        break;
    }
}

/* ---- rnbd350_hw.h implementation -------------------------------------- */

rnbd350_hw_status_t rnbd350_hw_init(void)
{
    fprintf(stderr, "[hw-stub] init: configuring UART 115200 8N1, RESET/WAKE GPIOs (stub)\n");
    fake_reset();
    return RNBD350_HW_OK;
}

void rnbd350_hw_deinit(void)
{
    fprintf(stderr, "[hw-stub] deinit\n");
    s_fake.initialized = 0;
}

int rnbd350_hw_uart_write(const uint8_t *data, size_t len)
{
    if (!s_fake.initialized) {
        return RNBD350_HW_ERR_IO;
    }

    fprintf(stderr, "[hw-stub] TX %zu byte(s):", len);
    for (size_t i = 0; i < len; ++i) {
        fprintf(stderr, " %02X", data[i]);
    }
    fprintf(stderr, "\n");

    /* Feed the simulated controller so it can produce a response. In a real
     * transport this write would instead go out the physical UART TX pin
     * to the RNBD350's RX pin. */
    for (size_t i = 0; i < len; ++i) {
        fake_feed_byte(data[i]);
    }

    return (int)len;
}

int rnbd350_hw_uart_read(uint8_t *buf, size_t buf_len)
{
    if (!s_fake.initialized) {
        return RNBD350_HW_ERR_IO;
    }

    size_t available = s_fake.rx_tail - s_fake.rx_head;
    size_t n = available < buf_len ? available : buf_len;

    for (size_t i = 0; i < n; ++i) {
        buf[i] = s_fake.rx_queue[s_fake.rx_head % HW_STUB_RX_QUEUE_SIZE];
        s_fake.rx_head++;
    }

    if (n > 0) {
        fprintf(stderr, "[hw-stub] RX %zu byte(s):", n);
        for (size_t i = 0; i < n; ++i) {
            fprintf(stderr, " %02X", buf[i]);
        }
        fprintf(stderr, "\n");
    }

    return (int)n;
}

void rnbd350_hw_set_reset(rnbd350_hw_level_t level)
{
    fprintf(stderr, "[hw-stub] RESET pin -> %s\n", level == RNBD350_HW_LOW ? "LOW (asserted)" : "HIGH (released)");
    if (level == RNBD350_HW_HIGH) {
        /* Real module would boot straight into HCI mode (see file header
         * comment); the stub just resets its fake state. */
        fake_reset();
    }
}

void rnbd350_hw_set_wake(rnbd350_hw_level_t level)
{
    fprintf(stderr, "[hw-stub] WAKE (UART_RX_IND) pin -> %s\n", level == RNBD350_HW_LOW ? "LOW" : "HIGH");
}

void rnbd350_hw_delay_ms(uint32_t ms)
{
    /* Stub: no real delay in the demo so it runs instantly; a firmware
     * port would call its RTOS/HAL delay here. */
    (void)ms;
}

uint32_t rnbd350_hw_millis(void)
{
    return (uint32_t)((clock() * 1000ULL) / CLOCKS_PER_SEC);
}
