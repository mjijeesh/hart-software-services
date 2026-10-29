#ifndef asm
#define asm __asm__
#endif

#include "config.h"
#include "hss_types.h"
#include "hss_debug.h"
#include <string.h>

#include "sfl.h"
#include "serialboot.h"
#include "micron1gflash.h"
#include "mss_uart.h"
#include "ssmb_ipi.h"

#ifndef PRV_M
#define PRV_M 3
#endif

#define MAX_FAILURES 256
#define SYS_CYCLE_PER_SEC 600000000UL
#define FLASH_DEFAULT_ADDR 0x00100000UL

/* SFL Extended Protocol Opcodes (from boot.c) */
#ifndef SFL_CMD_FLASH
#define SFL_CMD_FLASH 0x04
#endif
#ifndef SFL_CMD_DONE
#define SFL_CMD_DONE  0x05
#endif

static TxId_t g_serialboot_txid = 1u;

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
    if (addr >= 0x08000000UL && addr < 0x08200000UL) {
        *max_size = 0x08200000UL - addr;
        return 1;
    }
    if (addr >= 0x80000000UL && addr < 0xC0000000UL) {
        *max_size = 0xC0000000UL - addr;
        return 1;
    }
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

static void serialboot_jump(uintptr_t addr)
{
    mHSS_PRINTF("\n[SERIALBOOT] Synchronizing caches and dispatching all U54 harts to 0x%016lx...\n", addr);

    __asm__ __volatile__("fence rw, rw" ::: "memory");
    __asm__ __volatile__("fence.i" ::: "memory");

    for (enum HSSHartId hart = HSS_HART_U54_1; hart <= HSS_HART_U54_4; hart++) {
        IPI_Send(hart,
                 IPI_MSG_GOTO,
                 g_serialboot_txid++,
                 PRV_M,
                 (void *)addr,
                 NULL);
    }

    for (volatile int i = 0; i < 200000; i++);

    mHSS_PRINTF("[SERIALBOOT] All U54 harts launched. Application active on UART1 (/dev/ttyUSB1).\n");
    mHSS_PRINTF("[SERIALBOOT] Returning to HSS TinyCLI.\n\n");
}

static int check_ack(void)
{
    int recognized = 0;
    static const char ack_str[SFL_MAGIC_LEN] = SFL_MAGIC_ACK;
    uint64_t timeout_cycles = SYS_CYCLE_PER_SEC / 4;
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
    return 1;
}

/* ----------------- SFL State Machine ----------------- */

int do_serialboot(void)
{
    struct sfl_frame frame;
    int failures = 0;
    int is_flash_mode = 0;
    uint32_t spi_flash_address = FLASH_DEFAULT_ADDR;
    uint32_t spi_flash_start_address = 0;
    uint32_t total_loaded_size = 0;

    mHSS_PRINTF("\nReady for SFL transfer. Connect host script...\n");
    uart_write_str(SFL_MAGIC_REQ);

    if (check_ack() != 0) {
        mHSS_PRINTF("No response or cancelled. Aborting.\n");
        return 1;
    }

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

        uint16_t rx_crc = ((uint16_t)frame.crc[0] << 8) | (uint16_t)frame.crc[1];
        uint16_t calc_crc = sfl_crc16(&frame.cmd, frame.payload_length + 1);

        if (calc_crc != rx_crc) {
            uart_write_byte(SFL_ACK_CRCERROR);
            if (++failures >= MAX_FAILURES) return 1;
            continue;
        }

        switch (frame.cmd) {
        case SFL_CMD_ABORT:
            uart_write_byte(SFL_ACK_SUCCESS);
            return 1;

        case SFL_CMD_FLASH: {
            is_flash_mode = 1;
            spi_flash_address = FLASH_DEFAULT_ADDR;

            if (frame.payload_length >= 4) {
                uint32_t total_image_size = get_uint32(&frame.payload[0]);

                if (frame.payload_length >= 8) {
                    spi_flash_address = get_uint32(&frame.payload[4]);
                }
                spi_flash_start_address = spi_flash_address;

                FLASH_init();
                FLASH_global_unprotect();
                FLASH_erase_range(spi_flash_address, total_image_size);
                uart_write_byte(SFL_ACK_SUCCESS);
            } else {
                uart_write_byte(SFL_ACK_ERROR);
            }
            break;
        }

        case SFL_CMD_LOAD: {
            if (frame.payload_length < 4) {
                uart_write_byte(SFL_ACK_ERROR);
                break;
            }

            failures = 0;
            uint32_t load_size = frame.payload_length - 4;

            if (is_flash_mode) {
                /* Stream chunk directly to Micron SPI Flash */
                FLASH_program(spi_flash_address, &frame.payload[4], load_size);
                spi_flash_address += load_size;
                total_loaded_size += load_size;
                uart_write_byte(SFL_ACK_SUCCESS);
            } else {
                /* Direct RAM Download */
                uintptr_t dest_addr = (uintptr_t)get_uint32(&frame.payload[0]);
                size_t max_size;

                if (!boot_load_max_size(dest_addr, &max_size) || (load_size > max_size)) {
                    uart_write_byte(SFL_ACK_ERROR);
                    return 1;
                }

                memcpy((void *)dest_addr, &frame.payload[4], load_size);
                total_loaded_size += load_size;
                uart_write_byte(SFL_ACK_SUCCESS);
            }
            break;
        }

        case SFL_CMD_DONE: {
            uart_write_byte(SFL_ACK_SUCCESS);
            mHSS_PRINTF("\n[FLASHWRITE] SPI Flash Direct Upload Complete!\n");
            mHSS_PRINTF(" Target Flash Addr  : 0x%08X\n", (unsigned int)spi_flash_start_address);
            mHSS_PRINTF(" Total Written Size : %u bytes\n\n", (unsigned int)total_loaded_size);
            return 0;
        }

        case SFL_CMD_JUMP: {
            uart_write_byte(SFL_ACK_SUCCESS);

            if (is_flash_mode) {
                mHSS_PRINTF("\n[INFO] Flash programming finished. Returning to prompt.\n");
                return 0;
            }

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

/* ----------------- Manual CLI Flash Handlers ----------------- */

void do_flash_read_id(void)
{
    uint8_t mfg = 0, dev = 0, cap = 0;
    FLASH_init();
    FLASH_read_device_id(&mfg, &dev, &cap);
    mHSS_PRINTF("\n[SPI_FLASH] JEDEC ID:\n");
    mHSS_PRINTF(" Manufacturer : 0x%02X (Micron expected: 0x20)\n", mfg);
    mHSS_PRINTF(" Device Type  : 0x%02X\n", dev);
    mHSS_PRINTF(" Capacity ID  : 0x%02X\n\n", cap);
}

void do_flash_write(uint32_t spi_offset, uintptr_t ram_addr, uint32_t size)
{
    mHSS_PRINTF("\n[FLASH_WRITE] Initializing SPI Flash...\n");
    FLASH_init();
    FLASH_global_unprotect();

    mHSS_PRINTF("[FLASH_WRITE] Erasing range 0x%08X to 0x%08X (%u bytes)...\n",
                spi_offset, spi_offset + size, size);
    FLASH_erase_range(spi_offset, size);

    mHSS_PRINTF("[FLASH_WRITE] Programming from DDR 0x%016lx to Flash 0x%08X...\n",
                ram_addr, spi_offset);
    FLASH_program(spi_offset, (uint8_t *)ram_addr, size);

    mHSS_PRINTF("[FLASH_WRITE] Programming complete.\n\n");
}