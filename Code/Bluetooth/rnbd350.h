/**
 * @file rnbd350.h
 * @brief Client-facing API for the Microchip RNBD350 Bluetooth LE module's
 *        binary HCI transport.
 *
 * This is a minimal, synchronous client for the subset of the standard
 * Bluetooth HCI/ATT protocol needed to: reset the controller, scan,
 * connect/disconnect, and perform basic GATT client characteristic
 * read/write by handle. It sits directly on top of rnbd350_hw.h and knows
 * nothing about the physical transport.
 *
 * Protocol notes:
 *   - The module must already be in HCI mode (RNBD350 User Guide,
 *     DS50003684, section 6; firmware v1.1+). In HCI mode the module speaks
 *     H4 UART framing (packet type byte, then Command/ACL Data/Event
 *     packets) and no longer accepts the ASCII "RN" command console.
 *     Provisioning that one-time, PDS-persisted mode transition is a
 *     manufacturing/bring-up step, not something this library does.
 *   - Section 6 of DS50003684 only documents Microchip's *vendor* HCI
 *     commands (DFU, sleep, UART config, ...). Scanning, connecting, and
 *     GATT access use the standard Bluetooth Core Specification HCI/ATT
 *     command set, which is not RNBD-specific and so isn't repeated there.
 *   - Only one HCI command may be outstanding at a time, and only one ATT
 *     request may be outstanding at a time (the latter is an ATT protocol
 *     rule, not just a simplification here).
 *   - rnbd350_bd_addr_t::mac is in **wire order** (the byte order HCI
 *     already puts addresses on the air in), not conventional
 *     colon-notation MSB-first order.
 *   - Pairing/bonding (SMP) is not implemented by this client yet; see
 *     CLAUDE.md for what a follow-on phase would need to add.
 */

#ifndef RNBD350_H
#define RNBD350_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Types                                                                */
/* ------------------------------------------------------------------ */

/** Client-offered ATT MTU (bytes). Also sizes the ATT response buffer. */
#define RNBD350_ATT_RX_MTU 64u

/**
 * Largest GATT value this client will write in one request. Chosen to fit
 * a Write_Request within the default (pre-negotiation) ATT_MTU of 23 bytes
 * (23 - 3 byte header = 20). rnbd350_gatt_write_handle() rejects larger
 * writes outright rather than fragmenting them (see CLAUDE.md).
 */
#define RNBD350_GATT_MAX_VALUE_LEN 20u

/** Client-visible result of an operation. */
typedef enum {
    RNBD350_OK = 0,
    RNBD350_ERR_TIMEOUT,       /**< No response within the configured timeout. */
    RNBD350_ERR_PROTOCOL,      /**< Malformed/unexpected HCI or ATT framing. */
    RNBD350_ERR_IO,            /**< Underlying hardware/transport failure. */
    RNBD350_ERR_INVALID_ARG,
    RNBD350_ERR_NOT_CONNECTED,
    RNBD350_ERR_BUSY,          /**< A command/request is already in flight. */
    RNBD350_ERR_HCI,           /**< Controller returned a nonzero HCI status;
                                     see rnbd350_last_hci_status(). */
    RNBD350_ERR_ATT            /**< Peer sent an ATT Error_Response; see
                                     rnbd350_last_att_error(). */
} rnbd350_status_t;

/** Address type, matching the HCI wire values directly (no translation). */
typedef enum {
    RNBD350_ADDR_PUBLIC = 0,
    RNBD350_ADDR_RANDOM = 1
} rnbd350_addr_type_t;

/** A Bluetooth device address: 6-byte MAC in HCI wire order (LSO-first),
 *  plus its address type. */
typedef struct {
    uint8_t              mac[6];
    rnbd350_addr_type_t  type;
} rnbd350_bd_addr_t;

/** One entry from an LE Advertising Report. */
typedef struct {
    rnbd350_bd_addr_t addr;
    char              name[32]; /**< From AD type 0x09/0x08; "" if absent. */
    int8_t            rssi;
} rnbd350_scan_result_t;

/** Categories of asynchronous events the module can emit. */
typedef enum {
    RNBD350_EVT_SCAN_REPORT,
    RNBD350_EVT_CONNECTED,
    RNBD350_EVT_CONNECT_FAILED, /**< LE_Connection_Complete with status != 0. */
    RNBD350_EVT_DISCONNECTED
} rnbd350_event_type_t;

/** Payload for an asynchronous event delivered to the client. */
typedef struct {
    rnbd350_event_type_t  type;
    uint16_t               conn_handle;       /**< CONNECTED / DISCONNECTED. */
    uint8_t                hci_status;        /**< CONNECT_FAILED. */
    uint8_t                disconnect_reason; /**< DISCONNECTED. */
    rnbd350_scan_result_t  scan;              /**< SCAN_REPORT; .addr also
                                                    valid for CONNECTED. */
} rnbd350_event_t;

/**
 * @brief Callback invoked for every asynchronous event parsed out of the
 *        module's HCI event/ACL stream (connect/disconnect, scan reports).
 *
 * Called from rnbd350_poll(); not from an interrupt context.
 */
typedef void (*rnbd350_event_cb_t)(const rnbd350_event_t *event, void *user_ctx);

/** Client instance. Opaque to callers; allocate with rnbd350_init(). */
typedef struct {
    bool     initialized;
    bool     connected;
    uint16_t conn_handle;
    uint16_t att_mtu;              /**< Negotiated MTU; RNBD350_ATT_RX_MTU
                                         (well, the spec default of 23) until
                                         Exchange MTU completes for a link. */
    uint32_t response_timeout_ms;

    rnbd350_event_cb_t  event_cb;
    void               *event_cb_ctx;

    /* Outstanding HCI command state. */
    bool     cmd_pending;
    uint16_t cmd_pending_opcode;
    uint8_t  cmd_status;
    uint8_t  cmd_return_params[8]; /**< Only Read_BD_ADDR needs >1 byte. */
    size_t   cmd_return_len;

    /* Outstanding ATT request state. */
    bool     att_pending;
    uint8_t  att_pending_opcode;
    uint8_t  att_resp[RNBD350_ATT_RX_MTU];
    size_t   att_resp_len;

    uint8_t  last_hci_status;
    uint8_t  last_att_error;

    /* H4 receive state machine. */
    struct {
        int     state;      /**< RX_IDLE / RX_HDR / RX_BODY, see rnbd350.c. */
        uint8_t pkt_type;
        uint8_t hdr[4];
        size_t  hdr_len, hdr_want;
        uint8_t body[RNBD350_ATT_RX_MTU + 8]; /**< Covers ACL(L2CAP+ATT) and
                                                    the phase-1 event set. */
        size_t  body_len, body_want;
    } rx;
} rnbd350_client_t;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                            */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the client, bring up the underlying hardware transport
 *        (rnbd350_hw_init()), and issue the mandatory post-boot HCI_Reset.
 *
 * @param client Client instance to initialize (caller-owned storage).
 * @return RNBD350_OK, RNBD350_ERR_IO if the transport failed to init, or
 *         RNBD350_ERR_TIMEOUT/RNBD350_ERR_HCI if HCI_Reset didn't complete.
 */
rnbd350_status_t rnbd350_init(rnbd350_client_t *client);

/** Release hardware resources acquired by rnbd350_init(). */
void rnbd350_deinit(rnbd350_client_t *client);

/**
 * @brief Register the callback used to deliver asynchronous events.
 *
 * @param client    Initialized client.
 * @param cb        Callback, or NULL to stop receiving events.
 * @param user_ctx  Opaque pointer passed back to @p cb.
 */
void rnbd350_set_event_callback(rnbd350_client_t *client, rnbd350_event_cb_t cb, void *user_ctx);

/**
 * @brief Set how long a command/request may wait for its completion before
 *        RNBD350_ERR_TIMEOUT is returned.
 */
void rnbd350_set_response_timeout(rnbd350_client_t *client, uint32_t timeout_ms);

/**
 * @brief Pump the receive path: drains available UART bytes, assembles H4
 *        packets, and dispatches any asynchronous events found.
 *
 * Must be called periodically (e.g. from the main loop or a polling task)
 * even when no command is outstanding, so events are not missed.
 *
 * Note: this is non-blocking with one documented exception. Immediately
 * after a successful LE_Connection_Complete, rnbd350_poll() performs the
 * ATT Exchange MTU handshake synchronously (bounded by
 * response_timeout_ms) before delivering RNBD350_EVT_CONNECTED, so that
 * every connected callback sees a valid rnbd350_att_mtu().
 */
void rnbd350_poll(rnbd350_client_t *client);

/**
 * @brief Read the controller's local Bluetooth device address
 *        (HCI_Read_BD_ADDR). Mainly useful as a cheap way to prove the
 *        transport is alive.
 */
rnbd350_status_t rnbd350_get_bd_addr(rnbd350_client_t *client, rnbd350_bd_addr_t *out_addr);

/* ------------------------------------------------------------------ */
/* GAP: scanning, connecting                                           */
/* ------------------------------------------------------------------ */

/**
 * @brief Start BLE scanning (LE_Set_Scan_Parameters + LE_Set_Scan_Enable).
 *        Results are delivered via the event callback as
 *        RNBD350_EVT_SCAN_REPORT events as they arrive.
 */
rnbd350_status_t rnbd350_scan_start(rnbd350_client_t *client);

/** Stop BLE scanning (LE_Set_Scan_Enable, disabled). */
rnbd350_status_t rnbd350_scan_stop(rnbd350_client_t *client);

/**
 * @brief Connect to a specific device by address (LE_Create_Connection).
 *
 * This issues the command and returns once the controller acknowledges the
 * connection attempt (Command_Status, status 0); the resulting
 * RNBD350_EVT_CONNECTED / RNBD350_EVT_CONNECT_FAILED is reported
 * asynchronously via the event callback once LE_Connection_Complete
 * arrives, matching the module's own "ack now, resolve later" behavior.
 */
rnbd350_status_t rnbd350_connect(rnbd350_client_t *client, const rnbd350_bd_addr_t *addr);

/** Disconnect the active link (HCI_Disconnect). */
rnbd350_status_t rnbd350_disconnect(rnbd350_client_t *client);

/** @return true if the client believes a BLE link is currently up. */
bool rnbd350_is_connected(const rnbd350_client_t *client);

/* ------------------------------------------------------------------ */
/* GATT client: characteristic access by handle                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Read a remote characteristic value by handle (ATT Read_Request).
 *
 * @param client      Initialized, connected client.
 * @param handle      16-bit characteristic value handle (from prior service
 *                    discovery / the peer's GATT table).
 * @param out_buf     Destination for the characteristic bytes.
 * @param out_buf_len Capacity of @p out_buf.
 * @param out_len     Set to the number of bytes actually written to
 *                    @p out_buf.
 */
rnbd350_status_t rnbd350_gatt_read_handle(rnbd350_client_t *client,
                                           uint16_t handle,
                                           uint8_t *out_buf,
                                           size_t out_buf_len,
                                           size_t *out_len);

/**
 * @brief Write a remote characteristic value by handle (ATT Write_Request).
 *
 * @param client Initialized, connected client.
 * @param handle 16-bit characteristic value handle.
 * @param data   Bytes to write.
 * @param len    Number of bytes in @p data; must be <=
 *               RNBD350_GATT_MAX_VALUE_LEN and fit the negotiated MTU, or
 *               RNBD350_ERR_INVALID_ARG is returned (no fragmentation).
 */
rnbd350_status_t rnbd350_gatt_write_handle(rnbd350_client_t *client,
                                            uint16_t handle,
                                            const uint8_t *data,
                                            size_t len);

/* ------------------------------------------------------------------ */
/* Misc / diagnostics                                                   */
/* ------------------------------------------------------------------ */

/** Negotiated ATT MTU for the active link (spec default 23 if not yet
 *  negotiated / not connected). */
uint16_t rnbd350_att_mtu(const rnbd350_client_t *client);

/** HCI status byte from the most recent failed command (RNBD350_ERR_HCI). */
uint8_t rnbd350_last_hci_status(const rnbd350_client_t *client);

/** ATT error code from the most recent Error_Response (RNBD350_ERR_ATT). */
uint8_t rnbd350_last_att_error(const rnbd350_client_t *client);

/** Human-readable name for a rnbd350_status_t, for logging. */
const char *rnbd350_status_str(rnbd350_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* RNBD350_H */
