#include "config.h"
#include "hss_types.h"
#include "hss_debug.h"
#include "ymodem.h"
#include "hss_boot_service.h"

unsigned long strtoul_(const char *nptr);

#if IS_ENABLED(CONFIG_SERVICE_SPI_FLASH)
#include "services/spi/spi_flash.h"
#endif

#if IS_ENABLED(CONFIG_SERVICE_MMC)
#include "mmc_service.h"
extern void HSS_BootSelectSDCARD(void);
extern void HSS_BootSelectEMMC(void);
#endif

#if IS_ENABLED(CONFIG_SERVICE_QSPI)
#include "qspi_service.h"
extern void HSS_BootSelectQSPI(void);
#endif

#if IS_ENABLED(CONFIG_SERVICE_MMC) || IS_ENABLED(CONFIG_SERVICE_QSPI)
#include "gpt.h"
extern struct HSS_Storage *HSS_BootGetActiveStorage(void);
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef CONFIG_YMODEM_DDR_DEST_ADDR
#define CONFIG_YMODEM_DDR_DEST_ADDR 0x80000000UL
#endif

#ifndef CONFIG_YMODEM_MAX_PAYLOAD_SIZE
#define CONFIG_YMODEM_MAX_PAYLOAD_SIZE (32u * 1024u * 1024u)
#endif

#ifndef CONFIG_SERVICE_BOOT_SPI_FLASH_OFFSET
#define CONFIG_SERVICE_BOOT_SPI_FLASH_OFFSET 0x400UL
#endif

extern void hss_loader_ymodem_loop(void);



static bool ymodem_streq_nocase_(const char *a, const char *b)
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



static void ymodem_print_usage_(void)
{
    mHSS_PUTS("Usage:\n"
              "  YMODEM                     - Run default YMODEM loader loop\n"
#if IS_ENABLED(CONFIG_SERVICE_SPI_FLASH)
              "  YMODEM SPI [offset]        - Receive via YMODEM and flash to SPI (default 0x400)\n"
#endif
#if IS_ENABLED(CONFIG_SERVICE_MMC)
              "  YMODEM SD                  - Receive via YMODEM and write to SD Card\n"
              "  YMODEM EMMC                - Receive via YMODEM and write to eMMC\n"
#endif
#if IS_ENABLED(CONFIG_SERVICE_QSPI)
              "  YMODEM QSPI                - Receive via YMODEM and write to QSPI Flash\n"
#endif
              "\n");
}

#if IS_ENABLED(CONFIG_SERVICE_MMC) || IS_ENABLED(CONFIG_SERVICE_QSPI)
static bool ymodem_write_block_media_(const char *target_name,
                                    uintptr_t ddr_addr,
                                    size_t byte_count,
                                    bool is_mmc)
{
    struct HSS_BootImage *pImg = (struct HSS_BootImage *)ddr_addr;
    if (!HSS_Boot_VerifyMagic(pImg)) {
        mHSS_PRINTF("[YMODEM] ERROR: Invalid HSS Boot Image magic at 0x%08lx!\n",
                    (unsigned long)ddr_addr);
        return false;
    }

    struct HSS_Storage *pStorage = HSS_BootGetActiveStorage();
    if (!pStorage) {
        mHSS_PRINTF("[YMODEM] ERROR: No active storage driver for %s.\n", target_name);
        return false;
    }

    if (pStorage->init && !pStorage->init()) {
        mHSS_PRINTF("[YMODEM] ERROR: Failed to initialize %s.\n", target_name);
        return false;
    }

    uint32_t blockSize = 512U, eraseSize = 512U, blockCount = 0U;
    if (pStorage->getInfo) {
        pStorage->getInfo(&blockSize, &eraseSize, &blockCount);
    }
    if (blockSize == 0U) {
        blockSize = 512U;
    }

    size_t write_offset = 0U;
#if IS_ENABLED(CONFIG_SERVICE_MMC) && IS_ENABLED(CONFIG_SERVICE_BOOT_MMC_USE_GPT)
    if (is_mmc) {
        HSS_GPT_t gpt;
        gpt.lbaSize = blockSize;
        GPT_Init(&gpt, pStorage);
        if (GPT_ReadHeader(&gpt)) {
            size_t partIdx = 0U;
            HSS_GPT_PartitionEntry_t const *pEntry = NULL;
            size_t lbaOffset = 0U;
            if (GPT_FindBootSectorIndex(&gpt, &partIdx, &pEntry) &&
                GPT_PartitionIdToLBAOffset(&gpt, partIdx, &lbaOffset)) {
                write_offset = lbaOffset * (size_t)blockSize;
            }
        }
    }
#else
    (void)is_mmc;
#endif

    size_t aligned_len = (byte_count + (size_t)blockSize - 1U) & ~((size_t)blockSize - 1U);
    if (aligned_len > byte_count) {
        memset((uint8_t *)(ddr_addr + byte_count), 0, aligned_len - byte_count);
    }

    if (!pStorage->writeBlock || !pStorage->writeBlock(write_offset, (void *)ddr_addr, aligned_len)) {
        mHSS_PRINTF("[YMODEM] ERROR: Write failed on %s at offset 0x%lx!\n",
                    target_name, (unsigned long)write_offset);
        return false;
    }

    mHSS_PRINTF("[YMODEM] SUCCESS: Wrote %lu bytes to %s @ 0x%lx.\n",
                (unsigned long)byte_count, target_name, (unsigned long)write_offset);
    return true;
}
#endif

void HSS_YModem_CLI(size_t argc, char * const argv[])
{
    if (!argv || argc <= 1u) {
        hss_loader_ymodem_loop();
        return;
    }

    const char *subcmd = argv[1];

#if IS_ENABLED(CONFIG_SERVICE_SPI_FLASH)
    if (ymodem_streq_nocase_(subcmd, "SPI")) {
        uint32_t spi_offset = (uint32_t)CONFIG_SERVICE_BOOT_SPI_FLASH_OFFSET;
        if (argc > 2u) {
            spi_offset = (uint32_t)strtoul_(argv[2]);
        }

        uint8_t * const ddr_buf = (uint8_t *)CONFIG_YMODEM_DDR_DEST_ADDR;
        mHSS_PRINTF("\n[YMODEM] Ready to receive payload into 0x%08lx and flash at SPI offset 0x%x...\n",
                    (unsigned long)CONFIG_YMODEM_DDR_DEST_ADDR, spi_offset);

        const size_t rx_bytes = ymodem_receive(ddr_buf, CONFIG_YMODEM_MAX_PAYLOAD_SIZE);
        if (rx_bytes == 0u) {
            mHSS_PRINTF("[YMODEM] Transfer failed or aborted; SPI flash untouched.\n");
            return;
        }

        (void)spi_flash_write_payload(spi_offset, (uintptr_t)ddr_buf, rx_bytes);
        return;
    }
#endif

#if IS_ENABLED(CONFIG_SERVICE_MMC)
    if (ymodem_streq_nocase_(subcmd, "SD")) {
        uint8_t * const ddr_buf = (uint8_t *)CONFIG_YMODEM_DDR_DEST_ADDR;
        HSS_BootSelectSDCARD();
        const size_t rx_bytes = ymodem_receive(ddr_buf, CONFIG_YMODEM_MAX_PAYLOAD_SIZE);
        if (rx_bytes > 0u) {
            (void)ymodem_write_block_media_("SD Card", (uintptr_t)ddr_buf, rx_bytes, true);
        }
        return;
    }

    if (ymodem_streq_nocase_(subcmd, "EMMC")) {
        uint8_t * const ddr_buf = (uint8_t *)CONFIG_YMODEM_DDR_DEST_ADDR;
        HSS_BootSelectEMMC();
        const size_t rx_bytes = ymodem_receive(ddr_buf, CONFIG_YMODEM_MAX_PAYLOAD_SIZE);
        if (rx_bytes > 0u) {
            (void)ymodem_write_block_media_("eMMC", (uintptr_t)ddr_buf, rx_bytes, true);
        }
        return;
    }
#endif

#if IS_ENABLED(CONFIG_SERVICE_QSPI)
    if (ymodem_streq_nocase_(subcmd, "QSPI")) {
        uint8_t * const ddr_buf = (uint8_t *)CONFIG_YMODEM_DDR_DEST_ADDR;
        HSS_BootSelectQSPI();
        const size_t rx_bytes = ymodem_receive(ddr_buf, CONFIG_YMODEM_MAX_PAYLOAD_SIZE);
        if (rx_bytes > 0u) {
            (void)ymodem_write_block_media_("QSPI Flash", (uintptr_t)ddr_buf, rx_bytes, false);
        }
        return;
    }
#endif

    ymodem_print_usage_();
}