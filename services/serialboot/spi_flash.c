#include "spi_flash.h"
#include "hss_types.h"
#include "hss_debug.h"
#include <string.h>

/* Low-level HAL hooks defined per-board in board_spi.c */
extern void bsp_spi_init(void);
extern void bsp_spi_select(void);
extern void bsp_spi_deselect(void);
extern void bsp_spi_transfer(const uint8_t *tx, uint16_t tx_len, uint8_t *rx, uint16_t rx_len);

#define CMD_READ_ARRAY        0x03
#define CMD_READ_JEDEC_ID     0x9F
#define CMD_WRITE_ENABLE      0x06
#define CMD_WRITE_STATUS1     0x01
#define CMD_PAGE_PROGRAM      0x02
#define CMD_READ_STATUS       0x05
#define CMD_ERASE_64K         0xD8
#define CMD_ENTER_4BYTE       0xB7

/* SST Family Unprotect Opcodes */
#define CMD_SST25_EWSR        0x50  /* Enable Write Status Register (SST25/SST25WF) */
#define CMD_SST26_ULBPR       0x98  /* Global Block-Protection Unlock (SST26) */

static struct spi_flash_info active_flash;

static void spi_write_enable(void)
{
    uint8_t cmd = CMD_WRITE_ENABLE;
    bsp_spi_select();
    bsp_spi_transfer(&cmd, 1, NULL, 0);
    bsp_spi_deselect();
}

static void spi_wait_ready(void)
{
    uint8_t cmd = CMD_READ_STATUS;
    uint8_t status = 0;
    do {
        bsp_spi_select();
        bsp_spi_transfer(&cmd, 1, &status, 1);
        bsp_spi_deselect();
    } while (status & 0x01); /* Bit 0: Write In Progress (WIP) */
}

void spi_flash_read_id(uint8_t *mfr, uint8_t *dev, uint8_t *cap)
{
    uint8_t cmd = CMD_READ_JEDEC_ID;
    uint8_t id[3] = {0};

    bsp_spi_select();
    bsp_spi_transfer(&cmd, 1, id, 3);
    bsp_spi_deselect();

    if (mfr) *mfr = id[0];
    if (dev) *dev = id[1];
    if (cap) *cap = id[2];
}

void spi_flash_global_unprotect(void)
{
    uint8_t mfr = active_flash.mfr_id;
    uint8_t dev_type = (active_flash.device_id >> 8) & 0xFF;

    /* 1. Microchip SST26 Series: Uses 0x06 followed by 0x98 (ULBPR) */
    if (mfr == 0xBF && dev_type == 0x26) {
        spi_write_enable();
        uint8_t cmd = CMD_SST26_ULBPR;
        bsp_spi_select();
        bsp_spi_transfer(&cmd, 1, NULL, 0);
        bsp_spi_deselect();
        spi_wait_ready();
    }
    /* 2. Microchip SST25VF (0xBF) & SST25WF (0x62): Requires EWSR (0x50) + WRSR (0x01, 0x00) */
    else if (mfr == 0xBF || mfr == 0x62) {
        uint8_t cmd_ewsr = CMD_SST25_EWSR;
        bsp_spi_select();
        bsp_spi_transfer(&cmd_ewsr, 1, NULL, 0);
        bsp_spi_deselect();

        uint8_t wrsr_buf[2] = { CMD_WRITE_STATUS1, 0x00 };
        bsp_spi_select();
        bsp_spi_transfer(wrsr_buf, 2, NULL, 0);
        bsp_spi_deselect();
        spi_wait_ready();
    }
    /* 3. Micron, Winbond, and Generic Flash: Standard WREN (0x06) + WRSR (0x01, 0x00) */
    else {
        uint8_t cmd[2] = { CMD_WRITE_STATUS1, 0x00 };
        spi_write_enable();
        bsp_spi_select();
        bsp_spi_transfer(cmd, 2, NULL, 0);
        bsp_spi_deselect();
        spi_wait_ready();
    }
}

void spi_flash_init(void)
{
    bsp_spi_init();

    uint8_t mfr = 0, dev = 0, cap = 0;
    spi_flash_read_id(&mfr, &dev, &cap);

    active_flash.mfr_id = mfr;
    active_flash.device_id = ((uint16_t)dev << 8) | cap;
    active_flash.page_size = 256;
    active_flash.erase_size = 0x10000; /* 64KB */

    switch (mfr) {
    case 0x20: /* Micron (Icicle Kit) */
        active_flash.name = "Micron MT25Q";
        active_flash.needs_4byte_addr = true;
        break;
    case 0xEF: /* Winbond (Video Kit) */
        active_flash.name = "Winbond W25Q";
        active_flash.needs_4byte_addr = (cap >= 0x19); /* >= 256Mbit needs 4-byte */
        break;
    case 0xBF: /* Microchip / SST 3.3V */
        if (dev == 0x26) {
            active_flash.name = "Microchip SST26";
        } else {
            active_flash.name = "Microchip SST25VF";
        }
        active_flash.needs_4byte_addr = false;
        break;
    case 0x62: /* Microchip / SST 1.8V Low-Voltage (Discovery Kit mikroBUS) */
        active_flash.name = "Microchip SST25WF";
        active_flash.needs_4byte_addr = false;
        break;
    default:
        active_flash.name = "Generic SPI Flash";
        active_flash.needs_4byte_addr = false;
        break;
    }

    if (active_flash.needs_4byte_addr) {
        uint8_t cmd = CMD_ENTER_4BYTE;
        spi_write_enable();
        bsp_spi_select();
        bsp_spi_transfer(&cmd, 1, NULL, 0);
        bsp_spi_deselect();
    }

    /* Unlock block protection so erase/write operations work immediately */
    spi_flash_global_unprotect();
}

void spi_flash_read(uint32_t address, uint8_t *rx_buf, size_t len)
{
    uint8_t cmd[5];
    size_t bytes_read = 0;

    while (bytes_read < len) {
        uint32_t cur = address + bytes_read;
        size_t chunk = (len - bytes_read > 32768) ? 32768 : (len - bytes_read);

        cmd[0] = CMD_READ_ARRAY;
        if (active_flash.needs_4byte_addr) {
            cmd[1] = (cur >> 24) & 0xFF;
            cmd[2] = (cur >> 16) & 0xFF;
            cmd[3] = (cur >> 8) & 0xFF;
            cmd[4] = cur & 0xFF;
            bsp_spi_select();
            bsp_spi_transfer(cmd, 5, &rx_buf[bytes_read], chunk);
        } else {
            cmd[1] = (cur >> 16) & 0xFF;
            cmd[2] = (cur >> 8) & 0xFF;
            cmd[3] = cur & 0xFF;
            bsp_spi_select();
            bsp_spi_transfer(cmd, 4, &rx_buf[bytes_read], chunk);
        }
        bsp_spi_deselect();
        bytes_read += chunk;
    }
}

void spi_flash_erase_range(uint32_t address, size_t count)
{
    uint32_t block = address & ~(0xFFFFUL);
    while (block < address + count) {
        uint8_t cmd[5];
        cmd[0] = CMD_ERASE_64K;
        spi_write_enable();

        if (active_flash.needs_4byte_addr) {
            cmd[1] = (block >> 24) & 0xFF;
            cmd[2] = (block >> 16) & 0xFF;
            cmd[3] = (block >> 8) & 0xFF;
            cmd[4] = block & 0xFF;
            bsp_spi_select();
            bsp_spi_transfer(cmd, 5, NULL, 0);
        } else {
            cmd[1] = (block >> 16) & 0xFF;
            cmd[2] = (block >> 8) & 0xFF;
            cmd[3] = block & 0xFF;
            bsp_spi_select();
            bsp_spi_transfer(cmd, 4, NULL, 0);
        }
        bsp_spi_deselect();
        spi_wait_ready();
        block += 0x10000UL;
    }
}

void spi_flash_program(uint32_t address, const uint8_t *tx_buf, size_t len)
{
    uint32_t idx = 0, cur = address;
    while (idx < len) {
        uint32_t page_rem = 256 - (cur & 0xFF);
        size_t chunk = (len - idx < page_rem) ? (len - idx) : page_rem;
        uint8_t tx[261];

        spi_write_enable();
        tx[0] = CMD_PAGE_PROGRAM;
        uint8_t hdr_len = 0;

        if (active_flash.needs_4byte_addr) {
            tx[1] = (cur >> 24) & 0xFF;
            tx[2] = (cur >> 16) & 0xFF;
            tx[3] = (cur >> 8) & 0xFF;
            tx[4] = cur & 0xFF;
            hdr_len = 5;
        } else {
            tx[1] = (cur >> 16) & 0xFF;
            tx[2] = (cur >> 8) & 0xFF;
            tx[3] = cur & 0xFF;
            hdr_len = 4;
        }

        memcpy(&tx[hdr_len], &tx_buf[idx], chunk);
        bsp_spi_select();
        bsp_spi_transfer(tx, hdr_len + chunk, NULL, 0);
        bsp_spi_deselect();
        spi_wait_ready();

        cur += chunk;
        idx += chunk;
    }
}

const struct spi_flash_info *spi_flash_get_info(void)
{
    return &active_flash;
}