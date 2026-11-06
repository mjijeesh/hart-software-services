#ifndef NETBOOT_SERVICE_H
#define NETBOOT_SERVICE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    NETBOOT_STATUS_OK = 0,
    NETBOOT_STATUS_LINK_DOWN,
    NETBOOT_STATUS_ARP_TIMEOUT,
    NETBOOT_STATUS_TFTP_ERROR,
    NETBOOT_STATUS_INVALID_IMAGE
} netboot_status_t;

/* Public Service API */
netboot_status_t NETBOOT_Service_FetchImage(uint32_t server_ip,
                                            const char *filename,
                                            uintptr_t dest_addr,
                                            size_t *bytes_read);

void NETBOOT_Service_Deinit(void);

/* Payload jump dispatcher */
//void serialboot_jump(uintptr_t addr);

#endif /* NETBOOT_SERVICE_H */