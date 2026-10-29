#ifndef SERIALBOOT_H
#define SERIALBOOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

int do_serialboot(void);
void do_flash_write(uint32_t spi_offset, uintptr_t ram_addr, uint32_t size);
void do_flash_read_id(void);

#ifdef __cplusplus
}
#endif

#endif