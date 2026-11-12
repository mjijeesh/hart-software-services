/*
 * MPFS HSS Embedded Software - Serial Boot & Multi-Media Flash Loader
 *
 * Compatible with mpfs-terminal.py SFL protocol:
 *   - Magic Req : "sL5DdSMmkekro\n" (14 bytes)
 *   - Magic Ack : "z6IHG7cYDID6o\n" (14 bytes)
 *   - Replies   : 'K' (success), 'C' (CRC error), 'U' (unknown), 'E' (error)
 *   - Commands  : ABORT (0x00), LOAD (0x01), JUMP (0x02), FLASH (0x03), DONE (0x04)
 */

#include "config.h"
#include "hss_types.h"
#include "hss_debug.h"
#include <string.h>

#include "sbi/riscv_encoding.h"
#include "drivers/mss/mss_mmuart/mss_uart.h"
#include "serialboot.h"
#include "hss_boot_service.h"
#include "hss_boot_init.h"
#include "gpt.h"
#include "uart_helper.h"

#if IS_ENABLED(CONFIG_COMPRESSION)
#include "hss_decompress.h"
#endif

#if IS_ENABLED(CONFIG_SERVICE_SPI_FLASH)
#include "services/spi/spi_flash.h"
#endif

#ifndef PRV_M
#define PRV_M 3U
#endif

#ifndef CONFIG_SERVICE_BOOT_DDR_TARGET_ADDR
#define CONFIG_SERVICE_BOOT_DDR_TARGET_ADDR 0x103fc00000ULL
#endif

#ifndef CONFIG_SERVICE_BOOT_SPI_FLASH_OFFSET
#define CONFIG_SERVICE_BOOT_SPI_FLASH_OFFSET 0x400UL
#endif

#define SERIALBOOT_DEFAULT_DDR_ADDR 0x80000000UL
#define SERIALBOOT_MAX_PAYLOAD_SIZE (64UL * 1024UL * 1024UL) /* 64 MiB safety limit */

/* SFL Protocol Constants matching mpfs-terminal.py */
#define SFL_MAGIC_LEN       14U
#define SFL_MAGIC_REQ       "sL5DdSMmkekro\n"
#define SFL_MAGIC_ACK       "z6IHG7cYDID6o\n"

#define SFL_CMD_ABORT       0x00U
#define SFL_CMD_LOAD        0x01U
#define SFL_CMD_JUMP        0x02U
#define SFL_CMD_FLASH       0x03U
#define SFL_CMD_DONE        0x04U

#define SFL_ACK_SUCCESS     'K'
#define SFL_ACK_CRCERROR    'C'
#define SFL_ACK_UNKNOWN     'U'
#define SFL_ACK_ERROR       'E'

static uint16_t sfl_crc16_(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0x0000U;
    for (size_t i = 0U; i < len; i++) {
        crc ^= (uint16_t)buf[i] << 8;
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            if (crc & 0x8000U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static uint8_t uart_read_byte_blocking_(void)
{
    uint8_t ch = 0U;
    while (!uart_getchar(&ch, -1, false)) {
        /* Wait for next byte */
    }
    return ch;
}

static void uart_send_raw_bytes_(const void *data, size_t len)
{
    MSS_UART_polled_tx(&g_mss_uart0_lo, (const uint8_t *)data, (uint32_t)len);
}

static void uart_send_reply_(char reply)
{
    uint8_t ch = (uint8_t)reply;
    uart_send_raw_bytes_(&ch, 1U);
}

static void serialboot_print_payload_chunks_(const struct HSS_BootImage *pImg)
{
    mHSS_PRINTF("[PAYLOAD] Set Name : \"%s\"\n", pImg->set_name);
    mHSS_PRINTF("[PAYLOAD] Image Len: %lu bytes\n", (unsigned long)pImg->bootImageLength);

    if (pImg->chunkTableOffset == 0U) {
        return;
    }

    const struct HSS_BootChunkDesc *pChunk =
        (const struct HSS_BootChunkDesc *)((uintptr_t)pImg + pImg->chunkTableOffset);

    size_t idx = 0U;
    while (pChunk->size != 0U) {
        mHSS_PRINTF("[PAYLOAD]   Chunk[%lu]: Hart=%d  Load=0x%010lx  Exec=0x%010lx  Size=%lu bytes\n",
                    (unsigned long)idx,
                    (int)pChunk->owner,
                    (unsigned long)pChunk->loadAddr,
                    (unsigned long)pChunk->execAddr,
                    (unsigned long)pChunk->size);
        pChunk++;
        idx++;
    }
}

static void serialboot_jump_raw_u54_(uintptr_t entry_addr)
{
    mHSS_PRINTF("[SERIALBOOT] Flushing caches and dispatching U54 harts to 0x%010lx...\n",
                (unsigned long)entry_addr);

    __asm__ __volatile__("fence rw, rw" ::: "memory");
    __asm__ __volatile__("fence.i" ::: "memory");

    for (enum HSSHartId hartid = HSS_HART_U54_1; hartid <= HSS_HART_U54_4; hartid++) {
        (void)IPI_Send(hartid, IPI_MSG_GOTO, 0U, PRV_M, (void const *)entry_addr, NULL);
    }
}

bool HSS_Boot_DecodeAndBootDDR(uintptr_t ddr_addr, size_t byte_count)
{
    struct HSS_BootImage *pImg = (struct HSS_BootImage *)ddr_addr;

    /* 1. Structured HSS Boot Image (0xb007c0de or compressed 0xc08b8355) */
    if (HSS_Boot_VerifyMagic(pImg)) {
        mHSS_PRINTF("[PAYLOAD] Detected HSS Boot Image header (magic: 0x%08x).\n", pImg->magic);

        /*
         * Relocate (or decompress) the HSS_BootImage container to upper cached DDR
         * (0x103fc00000) so unpacking chunks into 0x80000000 does not overwrite the
         * chunk descriptor table while HSS_BootHarts() copies segments.
         */
        uintptr_t safe_container_addr = (uintptr_t)CONFIG_SERVICE_BOOT_DDR_TARGET_ADDR;
        struct HSS_BootImage *pStagedImg = (struct HSS_BootImage *)safe_container_addr;

        if (pImg->magic == mHSS_COMPRESSED_MAGIC) {
#if IS_ENABLED(CONFIG_COMPRESSION)
            mHSS_PRINTF("[PAYLOAD] Decompressing payload to 0x%010lx...\n",
                        (unsigned long)safe_container_addr);
            HSS_Decompress((void *)pImg, (void *)pStagedImg);
            if (pStagedImg->magic != mHSS_BOOT_MAGIC) {
                mHSS_PRINTF("[PAYLOAD] ERROR: Decompressed header magic invalid (0x%08x)!\n",
                            pStagedImg->magic);
                return false;
            }
#else
            mHSS_PRINTF("[PAYLOAD] ERROR: Compressed image received but CONFIG_COMPRESSION is disabled.\n");
            return false;
#endif
        } else {
            size_t copy_len = (pImg->bootImageLength > byte_count)
                              ? (size_t)pImg->bootImageLength
                              : byte_count;
            if (ddr_addr != safe_container_addr) {
                memcpy((void *)pStagedImg, (const void *)pImg, copy_len);
            }
        }

        serialboot_print_payload_chunks_(pStagedImg);

        mHSS_PRINTF("[PAYLOAD] Registering boot image and dispatching U54 harts...\n");
        HSS_Register_Boot_Image(pStagedImg);
        HSS_BootHarts();
        return true;
    }

    /* 2. Raw flat binary without HSS header -> jump U54 harts directly to ddr_addr */
    mHSS_PRINTF("[PAYLOAD] No HSS header magic found; treating as raw binary @ 0x%010lx.\n",
                (unsigned long)ddr_addr);
    serialboot_jump_raw_u54_(ddr_addr);
    return true;
}

#if IS_ENABLED(CONFIG_SERVICE_MMC) || IS_ENABLED(CONFIG_SERVICE_QSPI)
extern struct HSS_Storage *HSS_BootGetActiveStorage(void);

static bool serialboot_write_block_storage_(serialboot_target_t target,
                                            uintptr_t ddr_addr,
                                            size_t byte_count)
{
    struct HSS_BootImage *pImg = (struct HSS_BootImage *)ddr_addr;
    if (!HSS_Boot_VerifyMagic(pImg)) {
        mHSS_PRINTF("[FLASH] ERROR: Invalid HSS Boot Image magic at 0x%08lx!\n",
                    (unsigned long)ddr_addr);
        mHSS_PRINTF("[FLASH] Image must be packaged with hss-payload-generator (magic 0xb007c0de).\n");
        return false;
    }

    const char *media_name = "Storage";
    switch (target) {
#if IS_ENABLED(CONFIG_SERVICE_MMC)
        case SERIALBOOT_TARGET_EMMC:
            media_name = "eMMC";
            mHSS_PRINTF("[FLASH] Selecting eMMC interface...\n");
            HSS_BootSelectEMMC();
            break;

        case SERIALBOOT_TARGET_SDCARD:
            media_name = "SD Card";
            mHSS_PRINTF("[FLASH] Selecting SD Card interface...\n");
            HSS_BootSelectSDCARD();
            break;
#endif
#if IS_ENABLED(CONFIG_SERVICE_QSPI)
        case SERIALBOOT_TARGET_QSPI:
            media_name = "QSPI Flash";
            mHSS_PRINTF("[FLASH] Selecting QSPI Flash interface...\n");
            HSS_BootSelectQSPI();
            break;
#endif
        default:
            return false;
    }

    struct HSS_Storage *pStorage = HSS_BootGetActiveStorage();
    if (!pStorage) {
        mHSS_PRINTF("[FLASH] ERROR: No active storage driver available for %s.\n", media_name);
        return false;
    }

    if (pStorage->init && !pStorage->init()) {
        mHSS_PRINTF("[FLASH] ERROR: Failed to initialize %s.\n", media_name);
        return false;
    }

    uint32_t blockSize = 512U;
    uint32_t eraseSize = 512U;
    uint32_t blockCount = 0U;
    if (pStorage->getInfo) {
        pStorage->getInfo(&blockSize, &eraseSize, &blockCount);
    }
    if (blockSize == 0U) {
        blockSize = 512U;
    }

    size_t write_offset = 0U;
#if IS_ENABLED(CONFIG_SERVICE_MMC) && IS_ENABLED(CONFIG_SERVICE_BOOT_MMC_USE_GPT)
    if (target == SERIALBOOT_TARGET_EMMC || target == SERIALBOOT_TARGET_SDCARD) {
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
                mHSS_PRINTF("[FLASH] Found GPT boot partition %lu at offset 0x%lx.\n",
                            (unsigned long)partIdx, (unsigned long)write_offset);
            }
        }
    }
#endif

    size_t aligned_len = (byte_count + (size_t)blockSize - 1U) & ~((size_t)blockSize - 1U);
    if (aligned_len > byte_count) {
        memset((uint8_t *)(ddr_addr + byte_count), 0, aligned_len - byte_count);
    }

    mHSS_PRINTF("[FLASH] Writing %lu bytes (aligned: %lu) to %s offset 0x%lx...\n",
                (unsigned long)byte_count,
                (unsigned long)aligned_len,
                media_name,
                (unsigned long)write_offset);

    if (!pStorage->writeBlock || !pStorage->writeBlock(write_offset, (void *)ddr_addr, aligned_len)) {
        mHSS_PRINTF("[FLASH] ERROR: Write failed on %s!\n", media_name);
        return false;
    }

    /* Read back header to verify write integrity */
    struct HSS_BootImage verifyHdr;
    memset(&verifyHdr, 0, sizeof(verifyHdr));
    if (pStorage->readBlock && pStorage->readBlock(&verifyHdr, write_offset, sizeof(verifyHdr)) &&
        HSS_Boot_VerifyMagic(&verifyHdr)) {
        mHSS_PRINTF("[FLASH] SUCCESS: Payload verified on %s! Reset board to boot.\n", media_name);
        return true;
    }

    mHSS_PRINTF("[FLASH] ERROR: Read-back verification failed on %s!\n", media_name);
    return false;
}
#endif /* IS_ENABLED(CONFIG_SERVICE_MMC) || IS_ENABLED(CONFIG_SERVICE_QSPI) */

static void serialboot_handle_received_image_(serialboot_target_t target,
                                              uintptr_t ddr_dest,
                                              size_t bytes_received,
                                              uintptr_t addr_or_offset)
{
    switch (target) {
        case SERIALBOOT_TARGET_SPI:
#if IS_ENABLED(CONFIG_SERVICE_SPI_FLASH)
            (void)spi_flash_write_payload((uint32_t)addr_or_offset, ddr_dest, bytes_received);
#else
            mHSS_PRINTF("[SERIALBOOT] ERROR: SPI Flash support (CONFIG_SERVICE_SPI_FLASH) is disabled.\n");
#endif
            break;

#if IS_ENABLED(CONFIG_SERVICE_MMC)
        case SERIALBOOT_TARGET_EMMC:
        case SERIALBOOT_TARGET_SDCARD:
            (void)serialboot_write_block_storage_(target, ddr_dest, bytes_received);
            break;
#endif

#if IS_ENABLED(CONFIG_SERVICE_QSPI)
        case SERIALBOOT_TARGET_QSPI:
            (void)serialboot_write_block_storage_(target, ddr_dest, bytes_received);
            break;
#endif

        case SERIALBOOT_TARGET_DDR:
        default:
            (void)HSS_Boot_DecodeAndBootDDR(ddr_dest, bytes_received);
            break;
    }
}

int do_serialboot(serialboot_target_t target, uintptr_t addr_or_offset)
{
    uintptr_t ddr_dest = (target == SERIALBOOT_TARGET_DDR && addr_or_offset != 0U)
                         ? addr_or_offset
                         : (uintptr_t)SERIALBOOT_DEFAULT_DDR_ADDR;

    mHSS_PRINTF("[SERIALBOOT] Staging buffer @ 0x%010lx. Waiting for SFL host...\n",
                (unsigned long)ddr_dest);

    /* Send exact 14-byte SFL magic trigger ("sL5DdSMmkekro\n") without CRLF expansion */
    uart_send_raw_bytes_(SFL_MAGIC_REQ, SFL_MAGIC_LEN);

    /* Wait for exact 14-byte SFL magic ACK ("z6IHG7cYDID6o\n") from mpfs-terminal.py */
    const char *ack_str = SFL_MAGIC_ACK;
    size_t matched = 0U;
    while (matched < SFL_MAGIC_LEN) {
        uint8_t ch = uart_read_byte_blocking_();
        if (ch == (uint8_t)ack_str[matched]) {
            matched++;
        } else if (ch == (uint8_t)ack_str[0]) {
            matched = 1U;
        } else {
            matched = 0U;
        }
    }

    uint8_t frame_buf[260];
    bool has_base_addr = false;
    uint32_t base_host_addr = 0U;
    bool host_requested_flash = false;
    bool no_boot_only = false;
    size_t bytes_received = 0U;
    bool transfer_done = false;
    bool aborted = false;

    while (!transfer_done && !aborted) {
        uint8_t payload_len = uart_read_byte_blocking_();
        uint8_t crc_hi      = uart_read_byte_blocking_();
        uint8_t crc_lo      = uart_read_byte_blocking_();
        uint16_t rx_crc     = (uint16_t)(((uint16_t)crc_hi << 8) | (uint16_t)crc_lo);

        /* Frame CRC covers cmd (1 byte) + payload (payload_len bytes) */
        size_t body_len = (size_t)payload_len + 1U;
        for (size_t i = 0U; i < body_len; i++) {
            frame_buf[i] = uart_read_byte_blocking_();
        }

        uint16_t calc_crc = sfl_crc16_(frame_buf, body_len);
        if (calc_crc != rx_crc) {
            uart_send_reply_(SFL_ACK_CRCERROR);
            continue;
        }

        uint8_t cmd = frame_buf[0];
        const uint8_t *payload = &frame_buf[1];

        switch (cmd) {
            case SFL_CMD_FLASH:
                /* mpfs-terminal.py --flash sends 4B file_size + 4B target flash offset */
                if (payload_len < 8U) {
                    uart_send_reply_(SFL_ACK_ERROR);
                    aborted = true;
                } else {
                    host_requested_flash = true;
                    base_host_addr = ((uint32_t)payload[4] << 24) |
                                     ((uint32_t)payload[5] << 16) |
                                     ((uint32_t)payload[6] << 8)  |
                                      (uint32_t)payload[7];
                    has_base_addr = true;
                    uart_send_reply_(SFL_ACK_SUCCESS);
                }
                break;

            case SFL_CMD_LOAD:
                if (payload_len < 4U) {
                    uart_send_reply_(SFL_ACK_ERROR);
                    aborted = true;
                } else {
                    uint32_t pkt_addr = ((uint32_t)payload[0] << 24) |
                                        ((uint32_t)payload[1] << 16) |
                                        ((uint32_t)payload[2] << 8)  |
                                         (uint32_t)payload[3];
                    size_t data_len = (size_t)payload_len - 4U;

                    if (!has_base_addr) {
                        base_host_addr = pkt_addr;
                        has_base_addr = true;
                    }

                    size_t rel_offset = (pkt_addr >= base_host_addr)
                                        ? (size_t)(pkt_addr - base_host_addr)
                                        : (size_t)pkt_addr;

                    if ((rel_offset + data_len) > SERIALBOOT_MAX_PAYLOAD_SIZE) {
                        uart_send_reply_(SFL_ACK_ERROR);
                        aborted = true;
                    } else {
                        memcpy((uint8_t *)(ddr_dest + rel_offset), &payload[4], data_len);
                        if ((rel_offset + data_len) > bytes_received) {
                            bytes_received = rel_offset + data_len;
                        }
                        uart_send_reply_(SFL_ACK_SUCCESS);
                    }
                }
                break;

            case SFL_CMD_DONE:
            case SFL_CMD_JUMP:
                uart_send_reply_(SFL_ACK_SUCCESS);
                transfer_done = true;
                break;

            case SFL_CMD_ABORT:
                uart_send_reply_(SFL_ACK_SUCCESS);
                if (bytes_received > 0U) {
                    /* Sent by mpfs-terminal.py --no-boot after full transfer */
                    no_boot_only = true;
                    transfer_done = true;
                } else {
                    aborted = true;
                }
                break;

            default:
                uart_send_reply_(SFL_ACK_UNKNOWN);
                break;
        }
    }

    if (aborted || bytes_received == 0U) {
        mHSS_PRINTF("\n[SERIALBOOT] Transfer aborted or 0 bytes received.\n");
        return -1;
    }

    mHSS_PRINTF("\n[SERIALBOOT] Transfer complete (%lu bytes received, host base: 0x%x).\n",
                (unsigned long)bytes_received, base_host_addr);

    if (no_boot_only) {
        mHSS_PRINTF("[SERIALBOOT] --no-boot requested: payload staged in DDR @ 0x%010lx.\n",
                    (unsigned long)ddr_dest);
        return 0;
    }

    /*
     * If plain 'serialboot' was typed while host passed '--flash --kernel-adr <offset>'
     * (SFL_CMD_FLASH or base_host_addr < 0x80000000), route automatically to SPI flash.
     */
    if (target == SERIALBOOT_TARGET_DDR &&
        (host_requested_flash || (has_base_addr && base_host_addr < SERIALBOOT_DEFAULT_DDR_ADDR))) {
        target = SERIALBOOT_TARGET_SPI;
        addr_or_offset = (uintptr_t)base_host_addr;
    }

    serialboot_handle_received_image_(target, ddr_dest, bytes_received, addr_or_offset);
    return 0;
}

void do_flash_read_id(void)
{
#if IS_ENABLED(CONFIG_SERVICE_SPI_FLASH)
    spi_flash_init();

    uint8_t mfr = 0U, dev = 0U, cap = 0U;
    spi_flash_read_id(&mfr, &dev, &cap);
    const struct spi_flash_info *info = spi_flash_get_info();

    mHSS_PRINTF("[SPI-FLASH] JEDEC ID: MFR=0x%02X, DEV=0x%02X, CAP=0x%02X (%s)\n",
                mfr, dev, cap, info ? info->name : "Unknown");
#else
    mHSS_PRINTF("[SPI-FLASH] ERROR: CONFIG_SERVICE_SPI_FLASH is not enabled in this build.\n");
#endif
}

void do_flashboot(uint32_t spi_offset)
{
#if IS_ENABLED(CONFIG_SERVICE_SPI_FLASH)
    spi_flash_init();
    const struct spi_flash_info *info = spi_flash_get_info();
    if (!info || info->mfr_id == 0x00U || info->mfr_id == 0xFFU) {
        mHSS_PRINTF("[FLASHBOOT] ERROR: No SPI Flash detected (JEDEC ID: 0x%02X).\n",
                    info ? info->mfr_id : 0xFFU);
        return;
    }

    struct HSS_BootImage hdr;
    memset(&hdr, 0, sizeof(hdr));
    spi_flash_read(spi_offset, (uint8_t *)&hdr, sizeof(hdr));

    if (!HSS_Boot_VerifyMagic(&hdr)) {
        mHSS_PRINTF("[FLASHBOOT] ERROR: Invalid HSS Boot Image magic at SPI offset 0x%x!\n",
                    spi_offset);
        return;
    }

    uintptr_t ddr_dest = (uintptr_t)SERIALBOOT_DEFAULT_DDR_ADDR;
    size_t img_len = (size_t)hdr.bootImageLength;
    if (img_len < sizeof(hdr) || img_len > SERIALBOOT_MAX_PAYLOAD_SIZE) {
        mHSS_PRINTF("[FLASHBOOT] ERROR: Invalid bootImageLength (%lu bytes).\n",
                    (unsigned long)img_len);
        return;
    }

    mHSS_PRINTF("[FLASHBOOT] Reading %lu bytes from SPI @ 0x%x to DDR @ 0x%010lx...\n",
                (unsigned long)img_len, spi_offset, (unsigned long)ddr_dest);
    spi_flash_read(spi_offset, (uint8_t *)ddr_dest, img_len);

    (void)HSS_Boot_DecodeAndBootDDR(ddr_dest, img_len);
#else
    (void)spi_offset;
    mHSS_PRINTF("[FLASHBOOT] ERROR: CONFIG_SERVICE_SPI_FLASH is not enabled in this build.\n");
#endif
}