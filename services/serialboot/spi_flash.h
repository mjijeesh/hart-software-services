#ifndef SPI_FLASH_H
#define SPI_FLASH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct spi_flash_info {
    const char *name;
    uint8_t mfr_id;
    uint16_t device_id;
    uint32_t total_size;
    uint32_t page_size;
    uint32_t erase_size;
    bool needs_4byte_addr;
};

void spi_flash_init(void);
void spi_flash_read_id(uint8_t *mfr, uint8_t *dev, uint8_t *cap);
void spi_flash_read(uint32_t address, uint8_t *rx_buf, size_t len);
void spi_flash_erase_range(uint32_t address, size_t len);
void spi_flash_program(uint32_t address, const uint8_t *tx_buf, size_t len);
void spi_flash_global_unprotect(void);
const struct spi_flash_info *spi_flash_get_info(void);

#endif