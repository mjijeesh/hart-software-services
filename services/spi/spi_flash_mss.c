#include "config.h"
#include "hss_types.h"
#include "spi_flash.h"

#include "mpfs_hal/common/mss_peripherals.h"
#include "mpfs_hal/common/mss_sysreg.h"
#include "drivers/mss/mss_spi/mss_spi.h"

#if CONFIG_SPI_FLASH_MSS_SPI_INSTANCE == 1
#define ACTIVE_MSS_SPI      (&g_mss_spi1_lo)
#define ACTIVE_MSS_PERIPH   MSS_PERIPH_SPI1
#else
#define ACTIVE_MSS_SPI      (&g_mss_spi0_lo)
#define ACTIVE_MSS_PERIPH   MSS_PERIPH_SPI0
#endif

static const mss_spi_slave_t s_slave_map[4] = {
    MSS_SPI_SLAVE_0,
    MSS_SPI_SLAVE_1,
    MSS_SPI_SLAVE_2,
    MSS_SPI_SLAVE_3
};

#define ACTIVE_MSS_SLAVE    (s_slave_map[CONFIG_SPI_FLASH_MSS_SPI_SLAVE & 0x3])

void bsp_spi_init(void)
{
    (void)mss_config_clk_rst(ACTIVE_MSS_PERIPH, (uint8_t)MPFS_HAL_FIRST_HART, PERIPHERAL_ON);

    MSS_SPI_init(ACTIVE_MSS_SPI);
    MSS_SPI_configure_master_mode(ACTIVE_MSS_SPI,
                                  ACTIVE_MSS_SLAVE,
                                  MSS_SPI_MODE0,
                                  (uint32_t)CONFIG_SPI_FLASH_MSS_SPI_CLK_DIV,
                                  MSS_SPI_BLOCK_TRANSFER_FRAME_SIZE,
                                  NULL);
}

void bsp_spi_select(void)
{
    MSS_SPI_set_slave_select(ACTIVE_MSS_SPI, ACTIVE_MSS_SLAVE);
}

void bsp_spi_deselect(void)
{
    MSS_SPI_clear_slave_select(ACTIVE_MSS_SPI, ACTIVE_MSS_SLAVE);
}

void bsp_spi_transfer(const uint8_t *tx, uint16_t tx_len, uint8_t *rx, uint16_t rx_len)
{
    uint8_t dummy = 0xFFU;

    /*
     * MSS_SPI_transfer_block() clocks out tx_len command/address bytes first,
     * then clocks in rx_len response bytes while holding slave select active.
     */
    MSS_SPI_transfer_block(ACTIVE_MSS_SPI,
                           tx ? tx : &dummy,
                           tx ? tx_len : 0U,
                           rx,
                           rx ? rx_len : 0U);
}