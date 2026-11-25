#include "config.h"
#include "hss_types.h"
#include "hss_debug.h"
#include "services/serialboot/serialboot.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef CONFIG_SERVICE_BOOT_SPI_FLASH_OFFSET
#define CONFIG_SERVICE_BOOT_SPI_FLASH_OFFSET 0x400UL
#endif

#ifndef CONFIG_SERIALBOOT_DDR_DEFAULT_ADDR
#define CONFIG_SERIALBOOT_DDR_DEFAULT_ADDR 0x80000000UL
#endif

static bool serialboot_streq_nocase_(const char *a, const char *b)
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

static unsigned long serialboot_strtoul_(const char *nptr)
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

static void serialboot_print_usage_(void)
{
    mHSS_PUTS("Usage:\n"
              "  SERIALBOOT [load_addr]                 - Load over SFL to DDR and run\n"
#if IS_ENABLED(CONFIG_SERVICE_MMC)
              "  SERIALBOOT SD                          - Flash SFL image to SD Card\n"
              "  SERIALBOOT EMMC                        - Flash SFL image to eMMC\n"
#endif
#if IS_ENABLED(CONFIG_SERVICE_QSPI)
              "  SERIALBOOT QSPI                        - Flash SFL image to QSPI Flash\n"
#endif
#if IS_ENABLED(CONFIG_SERVICE_SPI_FLASH) || IS_ENABLED(CONFIG_SERVICE_SPI) || defined(CONFIG_SERVICE_SPI)
              "  SERIALBOOT SPI [offset]                - Flash SFL image to SPI Flash (default 0x400)\n"
#endif
              "\n");
}

void HSS_SerialBoot_CLI(size_t argc, char * const argv[])
{
    if (!argv || argc <= 1u) {
        do_serialboot(SERIALBOOT_TARGET_DDR, (uintptr_t)CONFIG_SERIALBOOT_DDR_DEFAULT_ADDR);
        return;
    }

    const char *arg1 = argv[1];

#if IS_ENABLED(CONFIG_SERVICE_MMC)
    if (serialboot_streq_nocase_(arg1, "SD")) {
        do_serialboot(SERIALBOOT_TARGET_SDCARD, 0U);
        return;
    }

    if (serialboot_streq_nocase_(arg1, "EMMC")) {
        do_serialboot(SERIALBOOT_TARGET_EMMC, 0U);
        return;
    }
#endif

#if IS_ENABLED(CONFIG_SERVICE_QSPI)
    if (serialboot_streq_nocase_(arg1, "QSPI")) {
        do_serialboot(SERIALBOOT_TARGET_QSPI, 0U);
        return;
    }
#endif

#if IS_ENABLED(CONFIG_SERVICE_SPI_FLASH) || IS_ENABLED(CONFIG_SERVICE_SPI) || defined(CONFIG_SERVICE_SPI)
    if (serialboot_streq_nocase_(arg1, "SPI")) {
        uint32_t offset = (uint32_t)CONFIG_SERVICE_BOOT_SPI_FLASH_OFFSET;
        if (argc > 2u) {
            offset = (uint32_t)serialboot_strtoul_(argv[2]);
        }
        do_serialboot(SERIALBOOT_TARGET_SPI, (uintptr_t)offset);
        return;
    }
#endif

    if (serialboot_streq_nocase_(arg1, "HELP") || arg1[0] < '0' || arg1[0] > '9') {
        serialboot_print_usage_();
        return;
    }

    do_serialboot(SERIALBOOT_TARGET_DDR, (uintptr_t)serialboot_strtoul_(arg1));
}