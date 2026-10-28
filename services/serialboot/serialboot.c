#ifndef asm
#define asm __asm__
#endif

/* 1. HSS foundational headers must come first */
#include "config.h"
#include "hss_types.h"
#include "hss_debug.h"

/* 2. Standard C runtime headers (omitting stdbool.h, stdio.h, and stdint.h) */
#include <string.h>

/* 3. Subsystem and hardware headers */
#include "sfl.h"
#include "serialboot.h"
#include "mss_uart.h"

#define MAX_FAILURES 256
#define SYS_CYCLE_PER_SEC 600000000UL /* PolarFire SoC CPU Frequency (~600-667MHz) */

#include "ssmb_ipi.h"

#ifndef PRV_M
#define PRV_M 3
#endif



/* Forward declarations */
static inline uint64_t get_cycles(void);
static inline bool uart_read_nonblock(uint8_t *ch);
static inline void uart_write_byte(uint8_t ch);
static inline void uart_write_str(const char *str);
static uint16_t sfl_crc16(const unsigned char *buffer, size_t len);
static int boot_load_max_size(uintptr_t addr, size_t *max_size);
static uint32_t get_uint32(const unsigned char *data);
/* To: */
static void serialboot_jump(uintptr_t addr);
static int check_ack(void);

/* ----------------- Hardware Abstraction Wrappers ----------------- */

static inline uint64_t get_cycles(void)
{
    uint64_t c;
    __asm__ __volatile__("rdcycle %0" : "=r"(c));
    return c;
}

static inline bool uart_read_nonblock(uint8_t *ch)
{
    return (MSS_UART_get_rx(&g_mss_uart0_lo, ch, 1) > 0);
}

static inline void uart_write_byte(uint8_t ch)
{
    MSS_UART_polled_tx(&g_mss_uart0_lo, &ch, 1);
}

static inline void uart_write_str(const char *str)
{
    MSS_UART_polled_tx_string(&g_mss_uart0_lo, (const uint8_t *)str);
}

static uint16_t sfl_crc16(const unsigned char *buffer, size_t len)
{
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)buffer[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static int boot_load_max_size(uintptr_t addr, size_t *max_size)
{
    /* LIM Scratchpad (0x08000000 - 0x08200000) */
    if (addr >= 0x08000000UL && addr < 0x08200000UL) {
        *max_size = 0x08200000UL - addr;
        return 1;
    }
    /* DDR Cached Range (0x80000000 - 0xC0000000) */
    if (addr >= 0x80000000UL && addr < 0xC0000000UL) {
        *max_size = 0xC0000000UL - addr;
        return 1;
    }
    /* DDR Non-Cached Range (0xC0000000 - 0xE0000000) */
    if (addr >= 0xC0000000UL && addr < 0xE0000000UL) {
        *max_size = 0xE0000000UL - addr;
        return 1;
    }

    mHSS_PRINTF("Error: Address 0x%08lx outside permissible RAM\n", addr);
    return 0;
}

static uint32_t get_uint32(const unsigned char *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8)  |
            (uint32_t)data[3];
}

/* ----------------- Launch Routine ----------------- */
#include "ssmb_ipi.h"

#ifndef PRV_M
#define PRV_M 3
#endif

static TxId_t g_serialboot_txid = 1u;

static void serialboot_jump(uintptr_t addr)
{
    mHSS_PRINTF("\n[SERIALBOOT] Synchronizing caches and dispatching all U54 harts to 0x%016lx...\n", addr);

    /* 1. Flush write buffers and instruction cache[cite: 5] */
    __asm__ __volatile__("fence rw, rw" ::: "memory");
    __asm__ __volatile__("fence.i" ::: "memory");

    /* 2. Dispatch GOTO to ALL U54 cores (Harts 1 to 4) in Machine Mode (PRV_M)[cite: 5, 9] */
    for (enum HSSHartId hart = HSS_HART_U54_1; hart <= HSS_HART_U54_4; hart++) {
        IPI_Send(hart,
                 IPI_MSG_GOTO,
                 g_serialboot_txid++,
                 PRV_M,
                 (void *)addr,
                 NULL);
    }

    /* 3. Small hardware delay for all cores to unpark[cite: 5] */
    for (volatile int i = 0; i < 200000; i++);

    mHSS_PRINTF("[SERIALBOOT] All U54 harts launched. Application active on UART1 (/dev/ttyUSB1).\n");
    mHSS_PRINTF("[SERIALBOOT] Returning to HSS TinyCLI.\n\n");
}



/* ----------------- SFL Reception Engine ----------------- */

static int check_ack(void)
{
    int recognized = 0;
    static const char ack_str[SFL_MAGIC_LEN] = SFL_MAGIC_ACK;
    uint64_t timeout_cycles = SYS_CYCLE_PER_SEC / 4; /* 250ms */
    uint64_t start_cycles = get_cycles();
    uint8_t c;

    while ((get_cycles() - start_cycles) < timeout_cycles) {
        if (uart_read_nonblock(&c)) {
            if (c == 'Q' || c == '\e') return -1;
            if (c == ack_str[recognized]) {
                recognized++;
                if (recognized == SFL_MAGIC_LEN) return 0;
            } else {
                recognized = (c == ack_str[0]) ? 1 : 0;
            }
        }
    }
    return 1; /* Timeout */
}

int do_serialboot(void)
{
    struct sfl_frame frame;
    int failures = 0;

    mHSS_PRINTF("\nReady for SFL transfer. Connect host script...\n");

    /* 1. Transmit Magic Request to terminal host */
    uart_write_str(SFL_MAGIC_REQ);

    /* 2. Wait for ACK */
    int ack = check_ack();
    if (ack != 0) {
        mHSS_PRINTF("No response or cancelled. Aborting.\n");
        return 1;
    }

    /* 3. Packet receive loop */
    while (1) {
        int idx = 0;
        int timed_out = 1;
        uint64_t byte_timeout = SYS_CYCLE_PER_SEC / 4;
        uint64_t start = get_cycles();

        while ((get_cycles() - start) < byte_timeout) {
            uint8_t rx;
            if (uart_read_nonblock(&rx)) {
                start = get_cycles();
                if (idx == 0) frame.payload_length = rx;
                if (idx == 1) frame.crc[0] = rx;
                if (idx == 2) frame.crc[1] = rx;
                if (idx == 3) frame.cmd = rx;
                if (idx >= 4) {
                    frame.payload[idx - 4] = rx;
                }

                if (idx == (frame.payload_length + 4 - 1)) {
                    timed_out = 0;
                    break;
                }
                idx++;
            }
        }

        if (timed_out) {
            uart_write_byte(SFL_ACK_ERROR);
            if (++failures >= MAX_FAILURES) return 1;
            continue;
        }

        /* Verify CRC16 */
        uint16_t rx_crc = ((uint16_t)frame.crc[0] << 8) | (uint16_t)frame.crc[1];
        uint16_t calc_crc = sfl_crc16(&frame.cmd, frame.payload_length + 1);

        if (calc_crc != rx_crc) {
            uart_write_byte(SFL_ACK_CRCERROR);
            if (++failures >= MAX_FAILURES) return 1;
            continue;
        }

        /* Process SFL Opcode */
        switch (frame.cmd) {
        case SFL_CMD_ABORT:
            uart_write_byte(SFL_ACK_SUCCESS);
            return 1;

        case SFL_CMD_LOAD: {
            if (frame.payload_length < 4) {
                uart_write_byte(SFL_ACK_ERROR);
                break;
            }

            failures = 0;
            uint32_t chunk_len = frame.payload_length - 4;
            uintptr_t dest_addr = (uintptr_t)get_uint32(&frame.payload[0]);
            size_t max_size;

            if (!boot_load_max_size(dest_addr, &max_size) || (chunk_len > max_size)) {
                uart_write_byte(SFL_ACK_ERROR);
                return 1;
            }

            memcpy((void *)dest_addr, &frame.payload[4], chunk_len);
            uart_write_byte(SFL_ACK_SUCCESS);
            break;
        }

        case SFL_CMD_JUMP: {
            uart_write_byte(SFL_ACK_SUCCESS);
            uintptr_t entry_point = (uintptr_t)get_uint32(&frame.payload[0]);
            serialboot_jump(entry_point);
            return 0;
        }

        default:
            uart_write_byte(SFL_ACK_UNKNOWN);
            break;
        }
    }
    return 0;
}