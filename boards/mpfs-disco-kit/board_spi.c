/*******************************************************************************
 * Board-specific SPI HAL for PolarFire SoC Discovery Kit (MSS SPI0)
 * Uses official Microchip MSS SPI driver in polled master mode
 *******************************************************************************/

#include "hss_types.h"

/* Undefine macros that collide with baremetal HAL bits.h */
#ifdef MIN
#undef MIN
#endif
#ifdef MAX
#undef MAX
#endif
#ifdef ARRAY_SIZE
#undef ARRAY_SIZE
#endif
#ifdef BIT
#undef BIT
#endif

#include "mss_spi.h"
#include "mpfs_hal/mss_hal.h"
#include <stdint.h>
#include <stddef.h>

/* Forward prototypes to satisfy -Wmissing-prototypes */
void bsp_spi_init(void);
void bsp_spi_select(void);
void bsp_spi_deselect(void);
void bsp_spi_transfer(const uint8_t *tx, uint16_t tx_len, uint8_t *rx, uint16_t rx_len);

void bsp_spi_init(void)
{
    (void)mss_config_clk_rst(MSS_PERIPH_SPI0, (uint8_t)1, PERIPHERAL_ON);

    MSS_SPI_init(&g_mss_spi0_lo);

    MSS_SPI_configure_master_mode(
        &g_mss_spi0_lo,
        MSS_SPI_SLAVE_0,
        MSS_SPI_MODE0,
        16u,
        MSS_SPI_BLOCK_TRANSFER_FRAME_SIZE,
        NULL
    );
}

void bsp_spi_select(void)
{
    MSS_SPI_set_slave_select(&g_mss_spi0_lo, MSS_SPI_SLAVE_0);
}

void bsp_spi_deselect(void)
{
    MSS_SPI_clear_slave_select(&g_mss_spi0_lo, MSS_SPI_SLAVE_0);
}

void bsp_spi_transfer(const uint8_t *tx, uint16_t tx_len, uint8_t *rx, uint16_t rx_len)
{
    MSS_SPI_transfer_block(&g_mss_spi0_lo, (uint8_t *)tx, tx_len, rx, rx_len);
}
