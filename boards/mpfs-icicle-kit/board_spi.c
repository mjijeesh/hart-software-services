/*******************************************************************************
 * Board-specific SPI HAL binding for PolarFire SoC Icicle Kit (CoreSPI)
 *******************************************************************************/

#include "hss_types.h"
#include "core_spi.h"
#include <stdint.h>
#include <stddef.h>

#ifndef FLASH_CORESPI_BASE_ADDR
#define FLASH_CORESPI_BASE_ADDR 0x40000600UL
#endif

#define CORESPI_SLAVE_ID 0

static spi_instance_t g_core_spi;

/* Forward function prototypes to satisfy -Wmissing-prototypes */
void bsp_spi_init(void);
void bsp_spi_select(void);
void bsp_spi_deselect(void);
void bsp_spi_transfer(const uint8_t *tx, uint16_t tx_len, uint8_t *rx, uint16_t rx_len);

void bsp_spi_init(void)
{
    SPI_init(&g_core_spi, FLASH_CORESPI_BASE_ADDR, 32);
    SPI_configure_master_mode(&g_core_spi);
}

void bsp_spi_select(void)
{
    SPI_set_slave_select(&g_core_spi, CORESPI_SLAVE_ID);
}

void bsp_spi_deselect(void)
{
    SPI_clear_slave_select(&g_core_spi, CORESPI_SLAVE_ID);
}

void bsp_spi_transfer(const uint8_t *tx, uint16_t tx_len, uint8_t *rx, uint16_t rx_len)
{
    SPI_transfer_block(&g_core_spi, (uint8_t *)tx, tx_len, rx, rx_len);
}