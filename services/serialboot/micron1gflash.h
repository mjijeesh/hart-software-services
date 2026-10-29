/***************************************************************************//**
 * Micron 1Gb MT25QL01GBBB SPI Flash Driver for Microchip PolarFire SoC (MPFS)
 * Modular Driver: Supports both FPGA CoreSPI and Hard MSS_SPI
 *******************************************************************************/

#ifndef MICRON1GFLASH_H_
#define MICRON1GFLASH_H_

#ifndef asm
#define asm __asm__
#endif

#include <stdint.h>
#include <stddef.h>

// icicle kit has micron flash connected to CoreSPI, so use that driver by default
#define FLASH_USE_CORE_SPI

#if defined(FLASH_USE_CORE_SPI)
#include "core_spi.h"
#ifndef FLASH_CORESPI_BASE_ADDR
#define FLASH_CORESPI_BASE_ADDR   0x40000000UL
#endif
#else
#include "mss_spi.h"
#endif


#define DIE_ERASE_0_256MB     0
#define DIE_ERASE_256MB_512MB 1
#define DIE_ERASE_512MB_768MB 2
#define DIE_ERASE_768MB_1GB   3

#define ERASE_4K_BLOCK        0
#define ERASE_64K_BLOCK       1

#ifdef __cplusplus
extern "C" {
#endif

void FLASH_init(void);
void FLASH_read_device_id(uint8_t *manufacturer_id, uint8_t *device_id, uint8_t *capacity_id);
void FLASH_read(uint32_t address, uint8_t *rx_buffer, size_t size_in_bytes);
void FLASH_program(uint32_t address, uint8_t *write_buffer, size_t size_in_bytes);
void FLASH_erase_64k_block(uint32_t address);
void FLASH_erase_range(uint32_t address, size_t count);
void FLASH_global_unprotect(void);
void FLASH_die_256MB_erase(uint8_t die_number);

#ifdef __cplusplus
}
#endif

#endif /* MICRON1GFLASH_H_ */