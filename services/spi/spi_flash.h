#ifndef HSS_SPI_FLASH_H
#define HSS_SPI_FLASH_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct spi_flash_info {
    const char *name;
    uint8_t mfr_id;
    uint16_t device_id;
    uint32_t page_size;
    uint32_t erase_size;
    bool needs_4byte_addr;
};

/* Low-level SPI controller hooks (implemented in board_spi.c or spi_flash_mss.c) */
void bsp_spi_init(void);
void bsp_spi_select(void);
void bsp_spi_deselect(void);
void bsp_spi_transfer(const uint8_t *tx, uint16_t tx_len, uint8_t *rx, uint16_t rx_len);

/* High-level SPI NOR flash API */
void spi_flash_init(void);
void spi_flash_read_id(uint8_t *mfr, uint8_t *dev, uint8_t *cap);
void spi_flash_global_unprotect(void);
void spi_flash_read(uint32_t address, uint8_t *rx_buf, size_t len);
void spi_flash_erase_range(uint32_t address, size_t count);
void spi_flash_program(uint32_t address, const uint8_t *tx_buf, size_t len);
const struct spi_flash_info *spi_flash_get_info(void);

/* Validates HSS magic, preserves <64 KiB sector prefix, erases, programs, and verifies */
bool spi_flash_write_payload(uint32_t spi_offset, uintptr_t ddr_addr, size_t byte_count);

#endif /* HSS_SPI_FLASH_H */