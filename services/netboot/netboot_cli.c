#include "config.h"
#include "hss_types.h"
#include "hss_debug.h"
#include "services/netboot/netboot_service.h"
#include "hss_boot_service.h"

unsigned long strtoul_(const char *nptr);

#if IS_ENABLED(CONFIG_SERVICE_SPI_FLASH)
#include "services/spi/spi_flash.h"
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef CONFIG_NETBOOT_DEFAULT_FILENAME
#define CONFIG_NETBOOT_DEFAULT_FILENAME "boot.bin"
#endif

#ifndef CONFIG_NETBOOT_DDR_DEST_ADDR
#define CONFIG_NETBOOT_DDR_DEST_ADDR 0x80000000UL
#endif

#ifndef CONFIG_NETBOOT_MAX_PAYLOAD_SIZE
#define CONFIG_NETBOOT_MAX_PAYLOAD_SIZE (32u * 1024u * 1024u)
#endif

#ifndef CONFIG_SERVICE_BOOT_SPI_FLASH_OFFSET
#define CONFIG_SERVICE_BOOT_SPI_FLASH_OFFSET 0x400UL
#endif

static bool netboot_streq_nocase_(const char *a, const char *b)
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



static void netboot_print_usage_(void)
{
    mHSS_PUTS("Usage:\n"
              "  NETBOOT [server_ip] [filename]              - Fetch to DDR and boot\n"
#if IS_ENABLED(CONFIG_SERVICE_SPI_FLASH)
              "  NETBOOT SPI [offset] [server_ip] [filename] - Fetch and flash to SPI (default 0x400)\n"
#endif
#if IS_ENABLED(CONFIG_SERVICE_MMC)
              "  NETBOOT SD [server_ip] [filename]           - Fetch and write to SD Card\n"
              "  NETBOOT EMMC [server_ip] [filename]         - Fetch and write to eMMC\n"
#endif
              "\n");
}

static bool netboot_download_(uint32_t server_ip, const char *filename, uintptr_t dest_addr, size_t *rx_bytes)
{
    *rx_bytes = 0u;
    netboot_status_t status = NETBOOT_Service_FetchImage(server_ip, filename, dest_addr, rx_bytes);
    if (status != NETBOOT_STATUS_OK || *rx_bytes == 0u) {
        mHSS_PRINTF("[NETBOOT] ERROR: Image fetch failed (status=%d, bytes=%lu).\n",
                    (int)status, (unsigned long)(*rx_bytes));
        return false;
    }

    return true;
}

void HSS_NetBoot_CLI(size_t argc, char * const argv[])
{
    const uintptr_t ddr_dest = (uintptr_t)CONFIG_NETBOOT_DDR_DEST_ADDR;
    uint32_t server_ip = 0u;
    const char *filename = CONFIG_NETBOOT_DEFAULT_FILENAME;
    size_t rx_bytes = 0u;

    if (!argv || argc <= 1u) {
        if (netboot_download_(server_ip, filename, ddr_dest, &rx_bytes)) {
            (void)HSS_Boot_DecodeAndBootDDR(ddr_dest, rx_bytes);
        }
        return;
    }

    const char *subcmd = argv[1];

#if IS_ENABLED(CONFIG_SERVICE_SPI_FLASH)
    
    if (netboot_streq_nocase_(subcmd, "SPI")) {
        uint32_t spi_offset = (uint32_t)CONFIG_SERVICE_BOOT_SPI_FLASH_OFFSET;
        size_t arg_idx = 2u;

        if (argc > arg_idx && NETBOOT_ParseIP(argv[arg_idx]) == 0u) {
            spi_offset = (uint32_t)strtoul_(argv[arg_idx]);
            arg_idx++;
        }
        if (argc > arg_idx) {
            server_ip = NETBOOT_ParseIP(argv[arg_idx]);
            arg_idx++;
        }
        if (argc > arg_idx) {
            filename = argv[arg_idx];
        }

        if (netboot_download_(server_ip, filename, ddr_dest, &rx_bytes)) {
            (void)spi_flash_write_payload(spi_offset, ddr_dest, rx_bytes);
        }
        return;
    }
#endif

#if IS_ENABLED(CONFIG_SERVICE_MMC)
    if (netboot_streq_nocase_(subcmd, "SD")) {
        if (argc > 2u) {
            server_ip = NETBOOT_ParseIP(argv[2]);
        }
        if (argc > 3u) {
            filename = argv[3];
        }

        if (netboot_download_(server_ip, filename, ddr_dest, &rx_bytes)) {
            (void)NETBOOT_WritePayloadToStorage(ddr_dest, rx_bytes, NETBOOT_MEDIA_SDCARD);
        }
        return;
    }

    if (netboot_streq_nocase_(subcmd, "EMMC")) {
        if (argc > 2u) {
            server_ip = NETBOOT_ParseIP(argv[2]);
        }
        if (argc > 3u) {
            filename = argv[3];
        }

        if (netboot_download_(server_ip, filename, ddr_dest, &rx_bytes)) {
            (void)NETBOOT_WritePayloadToStorage(ddr_dest, rx_bytes, NETBOOT_MEDIA_EMMC);
        }
        return;
    }
#endif

    if (netboot_streq_nocase_(subcmd, "HELP") || netboot_streq_nocase_(subcmd, "?")) {
        netboot_print_usage_();
        return;
    }

    /* Allow `NETBOOT <server_ip> [filename]` directly */
    server_ip = NETBOOT_ParseIP(argv[1]);
    if (server_ip != 0u) {
        if (argc > 2u) {
            filename = argv[2];
        }
        if (netboot_download_(server_ip, filename, ddr_dest, &rx_bytes)) {
            (void)HSS_Boot_DecodeAndBootDDR(ddr_dest, rx_bytes);
        }
        return;
    }

    netboot_print_usage_();
}
