#ifndef HSS_SERIALBOOT_H
#define HSS_SERIALBOOT_H

#include "hss_types.h"

typedef enum {
    SERIALBOOT_TARGET_DDR = 0,
    SERIALBOOT_TARGET_SPI,
    SERIALBOOT_TARGET_EMMC,
    SERIALBOOT_TARGET_SDCARD,
    SERIALBOOT_TARGET_QSPI
} serialboot_target_t;

int do_serialboot(serialboot_target_t target, uintptr_t addr_or_offset);
void do_flash_read_id(void);
void do_flashboot(uint32_t spi_offset);

/* Board-specific raw UART transmit (no CRLF translation) */
void bsp_serialboot_uart_tx(const uint8_t *data, size_t len);

/* Auto-detects HSS_BootImage (0xb007c0de / compressed) vs raw flat binary and boots U54s */
//bool HSS_Boot_DecodeAndBootDDR(uintptr_t ddr_addr, size_t byte_count);

#endif /* HSS_SERIALBOOT_H */