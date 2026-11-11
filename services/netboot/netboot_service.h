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

netboot_status_t NETBOOT_Service_FetchImage(uint32_t server_ip,
                                            const char *filename,
                                            uintptr_t dest_addr,
                                            size_t *bytes_read);

void NETBOOT_Service_Deinit(void);

/* Self-contained U54 Hart Dispatcher */
void netboot_jump(uintptr_t addr);

#endif /* NETBOOT_SERVICE_H */