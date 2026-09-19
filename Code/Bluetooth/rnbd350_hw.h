/**
 * @file rnbd350_hw.h
 * @brief Hardware Communication Interface (HCI) boundary for the Microchip
 *        RNBD350 Bluetooth LE module.
 *
 * This header defines the ONLY surface the RNBD350 client (rnbd350.c) uses
 * to touch real hardware: byte-level UART I/O and the two control lines
 * (RESET, UART mode switch / wake). Everything above this line is portable
 * C with no MCU dependency.
 *
 * To port to a real board, implement every function declared here against
 * your MCU's HAL (e.g. STM32 HAL UART + GPIO, an RTOS UART driver, etc.) in
 * place of rnbd350_hw_stub.c. Nothing in rnbd350.c needs to change.
 */

#ifndef RNBD350_HW_H
#define RNBD350_HW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Logic level for GPIO-style control lines. */
typedef enum {
    RNBD350_HW_LOW  = 0,
    RNBD350_HW_HIGH = 1
} rnbd350_hw_level_t;

/** Result codes returned by the hardware layer. */
typedef enum {
    RNBD350_HW_OK        = 0,
    RNBD350_HW_ERR_IO    = -1, /**< Transport-level failure (UART fault, etc.) */
    RNBD350_HW_ERR_TIMEOUT = -2
} rnbd350_hw_status_t;

/**
 * @brief One-time hardware bring-up: configure the UART peripheral
 *        (115200 8N1, per the RNBD350 default UART settings) and the
 *        RESET / mode-switch GPIOs.
 *
 * @return RNBD350_HW_OK on success.
 */
rnbd350_hw_status_t rnbd350_hw_init(void);

/**
 * @brief Tear down whatever rnbd350_hw_init() set up (optional; useful for
 *        tests or power-down sequences).
 */
void rnbd350_hw_deinit(void);

/**
 * @brief Blocking write of raw bytes to the module's UART RX pin.
 *
 * @param data Bytes to transmit.
 * @param len  Number of bytes in @p data.
 * @return Number of bytes actually written, or a negative rnbd350_hw_status_t
 *         on error.
 */
int rnbd350_hw_uart_write(const uint8_t *data, size_t len);

/**
 * @brief Non-blocking-ish read of whatever bytes are currently available
 *        from the module's UART TX pin.
 *
 * Implementations are expected to be polled frequently by the client's
 * receive loop; a real implementation would typically be backed by an
 * interrupt- or DMA-fed ring buffer.
 *
 * @param buf     Destination buffer.
 * @param buf_len Capacity of @p buf.
 * @return Number of bytes copied into @p buf (0 if none available), or a
 *         negative rnbd350_hw_status_t on error.
 */
int rnbd350_hw_uart_read(uint8_t *buf, size_t buf_len);

/**
 * @brief Drive the module's hardware RESET line.
 *
 * @param level RNBD350_HW_LOW asserts reset (module held in reset on most
 *              reference designs), RNBD350_HW_HIGH releases it.
 */
void rnbd350_hw_set_reset(rnbd350_hw_level_t level);

/**
 * @brief Drive the UART_RX_IND / mode-switch style wake line (PB9 on the
 *        reference design) used to wake the module from low-power mode
 *        before sending UART data.
 */
void rnbd350_hw_set_wake(rnbd350_hw_level_t level);

/**
 * @brief Millisecond delay, used for reset pulse widths and wake timing
 *        (the module requires >= 25 ms of wake setup before UART data,
 *        per the datasheet timing diagram).
 */
void rnbd350_hw_delay_ms(uint32_t ms);

/**
 * @brief Monotonic millisecond tick counter, used by the client for
 *        command-response timeouts.
 */
uint32_t rnbd350_hw_millis(void);

#ifdef __cplusplus
}
#endif

#endif /* RNBD350_HW_H */
