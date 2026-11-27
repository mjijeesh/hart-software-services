#include "config.h"
#include "hss_types.h"
#include "hss_debug.h"
#include "services/spiflash/spi_flash.h"
#include "hss_boot_service.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


void HSS_BootSelectSPI(void);

#ifndef CONFIG_SERVICE_BOOT_SPI_FLASH_OFFSET
#define CONFIG_SERVICE_BOOT_SPI_FLASH_OFFSET 0x400UL
#endif

#ifndef SPI_CLI_DEFAULT_DDR_ADDR
#define SPI_CLI_DEFAULT_DDR_ADDR 0x80000000UL
#endif

#ifndef SPI_CLI_MAX_PAYLOAD_SIZE
#define SPI_CLI_MAX_PAYLOAD_SIZE (64UL * 1024UL * 1024UL)
#endif

static bool spi_cli_streq_nocase_(const char *a, const char *b)
{
    if (!a || !b) {
        return false;
    }

    while (*a && *b) {
        char ca = *a;
        char cb = *b;

        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca + ('a' - 'A'));
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb + ('a' - 'A'));
        }

        if (ca != cb) {
            return false;
        }
        a++;
        b++;
    }

    return (*a == '\0') && (*b == '\0');
}

static unsigned long spi_cli_strtoul_(const char *nptr)
{
    int base = 10;

    if (!nptr) {
        return 0UL;
    }

    if ((nptr[0] == '0') && (nptr[1] == 'x' || nptr[1] == 'X')) {
        base = 16;
    }

    return strtoul(nptr, NULL, base);
}

static void spi_cli_print_usage_(void)
{
    mHSS_PUTS("Usage:\n"
              "  SPIFLASH                      - Select boot via SPI\n"
              "  SPIFLASH  ID                   - Read SPI Flash JEDEC ID and info\n"
              "  SPIFLASH  WRITE <addr> <off> <len> - Program DDR buffer to SPI Flash at offset\n"
              "  SPIFLASH  BOOT [offset]        - Boot payload from SPI Flash (default 0x400)\n"
              "\n");
}

static void spi_cli_id_(void)
{
    spi_flash_init();

    uint8_t mfr = 0U, dev = 0U, cap = 0U;
    spi_flash_read_id(&mfr, &dev, &cap);
    const struct spi_flash_info *info = spi_flash_get_info();

    if (info && info->mfr_id != 0x00U && info->mfr_id != 0xFFu) {
        mHSS_PRINTF("[SPI-FLASH] %s (JEDEC: MFR=0x%02X, DEV=0x%02X, CAP=0x%02X, 4-Byte Addr: %s)\n",
                    info->name, mfr, dev, cap, info->needs_4byte_addr ? "Yes" : "No");
    } else {
        mHSS_PRINTF("[SPI-FLASH] No SPI flash detected (JEDEC: 0x%02X).\n", mfr);
    }
}

static void spi_cli_write_(size_t argc, char * const argv[])
{
    if (argc < 5u) {
        mHSS_PUTS("Usage: SPI WRITE <ddr_addr> <spi_offset> <length_bytes>\n");
        return;
    }

    const uintptr_t src_addr  = (uintptr_t)spi_cli_strtoul_(argv[2]);
    const uint32_t spi_offset = (uint32_t)spi_cli_strtoul_(argv[3]);
    const size_t length       = (size_t)spi_cli_strtoul_(argv[4]);

    (void)spi_flash_write_payload(spi_offset, src_addr, length);
}

static void spi_cli_boot_(size_t argc, char * const argv[])
{
    uint32_t spi_offset = (uint32_t)CONFIG_SERVICE_BOOT_SPI_FLASH_OFFSET;
    if (argc >= 3u) {
        spi_offset = (uint32_t)spi_cli_strtoul_(argv[2]);
    }

    spi_flash_init();
    const struct spi_flash_info *info = spi_flash_get_info();
    if (!info || info->mfr_id == 0x00U || info->mfr_id == 0xFFU) {
        mHSS_PRINTF("[SPI-BOOT] ERROR: No SPI Flash detected (JEDEC ID: 0x%02X).\n",
                    info ? info->mfr_id : 0xFFU);
        return;
    }

    struct HSS_BootImage hdr;
    memset(&hdr, 0, sizeof(hdr));
    spi_flash_read(spi_offset, (uint8_t *)&hdr, sizeof(hdr));

    if (!HSS_Boot_VerifyMagic(&hdr)) {
        mHSS_PRINTF("[SPI-BOOT] ERROR: Invalid HSS Boot Image magic at SPI offset 0x%x!\n",
                    spi_offset);
        return;
    }

    uintptr_t ddr_dest = (uintptr_t)SPI_CLI_DEFAULT_DDR_ADDR;
    size_t img_len = (size_t)hdr.bootImageLength;
    if (img_len < sizeof(hdr) || img_len > SPI_CLI_MAX_PAYLOAD_SIZE) {
        mHSS_PRINTF("[SPI-BOOT] ERROR: Invalid bootImageLength (%lu bytes).\n",
                    (unsigned long)img_len);
        return;
    }

    mHSS_PRINTF("[SPI-BOOT] Reading %lu bytes from SPI @ 0x%x to DDR @ 0x%010lx...\n",
                (unsigned long)img_len, spi_offset, (unsigned long)ddr_dest);
    spi_flash_read(spi_offset, (uint8_t *)ddr_dest, img_len);

    (void)HSS_Boot_DecodeAndBootDDR(ddr_dest, img_len);
}

void HSS_SPIFLASH_CLI(size_t argc, char * const argv[])
{
 
    const char *subcmd = argv[1];

    if (spi_cli_streq_nocase_(subcmd, "ID")) {
        spi_cli_id_();
        return;
    }

    if (spi_cli_streq_nocase_(subcmd, "WRITE")) {
        spi_cli_write_(argc, argv);
        return;
    }

    if (spi_cli_streq_nocase_(subcmd, "BOOT")) {
        spi_cli_boot_(argc, argv);
        return;
    }

    spi_cli_print_usage_();
}