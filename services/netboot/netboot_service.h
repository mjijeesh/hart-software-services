#ifndef NETBOOT_SERVICE_H
#define NETBOOT_SERVICE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    NETBOOT_STATUS_OK = 0,
    NETBOOT_STATUS_LINK_DOWN,
    NETBOOT_STATUS_ARP_TIMEOUT,
    NETBOOT_STATUS_TFTP_ERROR
} netboot_status_t;

typedef enum {
    NETBOOT_MEDIA_SDCARD = 0,
    NETBOOT_MEDIA_EMMC,
    NETBOOT_MEDIA_QSPI
} netboot_media_t;

netboot_status_t NETBOOT_Service_FetchImage(uint32_t server_ip,
                                            const char *filename,
                                            uintptr_t dest_addr,
                                            size_t *bytes_read);

void NETBOOT_Service_Deinit(void);
void netboot_jump(uintptr_t addr);

uint32_t NETBOOT_ParseIP(const char *str);
/* Writes a staged HSS payload from DDR to SD Card, eMMC, or QSPI Flash */
bool NETBOOT_WritePayloadToStorage(uintptr_t ddr_addr, size_t byte_count, netboot_media_t media);

#endif /* NETBOOT_SERVICE_H */