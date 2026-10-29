/***************************************************************************//**
 * Micron 1Gb MT25QL01GBBB SPI Flash Driver Implementation for MPFS
 *******************************************************************************/

#ifndef asm
#define asm __asm__
#endif

/* 1. HSS foundational headers first */
#include "config.h"
#include "hss_types.h"
#include "hss_debug.h"

/* 2. Standard C headers (avoiding stdbool.h to prevent macro collisions) */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* 3. Driver headers */
#include "micron1gflash.h"
#include "mpfs_hal/mss_hal.h"



/* Micron MT25QL01GBBB Command Opcodes */
#define READ_ARRAY_OPCODE           0x03
#define DEVICE_ID_READ              0x9F
#define WRITE_ENABLE_CMD            0x06
#define WRITE_DISABLE_CMD           0x04
#define PROGRAM_PAGE_CMD            0x02
#define WRITE_STATUS1_OPCODE        0x01
#define DIE_256MB_ERASE_OPCODE      0xC4
#define ERASE_4K_BLOCK_OPCODE       0x20
#define ERASE_64K_BLOCK_OPCODE      0xD8
#define READ_STATUS                 0x05
#define ADDRESS_MODE_4BYTE          0xB7
#define READ_FLAG_STATUS_REGISTER   0x70

/* Select CoreSPI driver or MSS_SPI Driver based on Board Design */
#if defined(FLASH_USE_CORE_SPI)
static spi_instance_t g_flash_core_spi;
#define SPI_SLAVE                   0

#elif defined(FLASH_USE_MSS_SPI1)
#define SPI_FLASH_PERIPH            MSS_PERIPH_SPI1
#define SPI_INSTANCE                (&g_mss_spi1_lo)
#define SPI_SLAVE                   MSS_SPI_SLAVE_0

#else /* Default: FLASH_USE_MSS_SPI0 */
#define SPI_FLASH_PERIPH            MSS_PERIPH_SPI0
#define SPI_INSTANCE                (&g_mss_spi0_lo)
#define SPI_SLAVE                   MSS_SPI_SLAVE_0
#endif

/*******************************************************************************
 * Hardware Abstraction Layer (HAL) Helpers
 *******************************************************************************/
static void hal_spi_init(void)
{
#if defined(FLASH_USE_CORE_SPI)
    SPI_init(&g_flash_core_spi, FLASH_CORESPI_BASE_ADDR, 32);
    SPI_configure_master_mode(&g_flash_core_spi);
#else
    (void) mss_config_clk_rst(SPI_FLASH_PERIPH, (uint8_t)1, PERIPHERAL_ON);
    MSS_SPI_init(SPI_INSTANCE);
    MSS_SPI_configure_master_mode(
        SPI_INSTANCE,
        SPI_SLAVE,
        MSS_SPI_MODE0,
        8u,
        MSS_SPI_BLOCK_TRANSFER_FRAME_SIZE,
        NULL
    );
#endif
}

static void hal_spi_select(void)
{
#if defined(FLASH_USE_CORE_SPI)
    SPI_set_slave_select(&g_flash_core_spi, SPI_SLAVE);
#else
    MSS_SPI_set_slave_select(SPI_INSTANCE, SPI_SLAVE);
#endif
}

static void hal_spi_deselect(void)
{
#if defined(FLASH_USE_CORE_SPI)
    SPI_clear_slave_select(&g_flash_core_spi, SPI_SLAVE);
#else
    MSS_SPI_clear_slave_select(SPI_INSTANCE, SPI_SLAVE);
#endif
}

static void hal_spi_transfer(const uint8_t *tx, uint16_t tx_len, uint8_t *rx, uint16_t rx_len)
{
#if defined(FLASH_USE_CORE_SPI)
    SPI_transfer_block(&g_flash_core_spi, (uint8_t *)tx, tx_len, rx, rx_len);
#else
    MSS_SPI_transfer_block(SPI_INSTANCE, (uint8_t *)tx, tx_len, rx, rx_len);
#endif
}

/*******************************************************************************
 * Private Helpers
 *******************************************************************************/
static void wait_program_or_erase_controller_ready(void)
{
    uint8_t ready_bit = 0;
    uint8_t command = READ_FLAG_STATUS_REGISTER;

    do {
        hal_spi_select();
        hal_spi_transfer(&command, 1, &ready_bit, 1);
        hal_spi_deselect();
        ready_bit = ready_bit & 0x80;
    } while (ready_bit == 0);
}

static void write_enable(void)
{
    uint8_t cmd_buffer = WRITE_ENABLE_CMD;
    hal_spi_select();
    hal_spi_transfer(&cmd_buffer, 1, NULL, 0);
    hal_spi_deselect();
}

static void enter_4byte_address_mode(void)
{
    uint8_t cmd_buffer = ADDRESS_MODE_4BYTE;
    hal_spi_select();
    hal_spi_transfer(&cmd_buffer, 1, NULL, 0);
    hal_spi_deselect();
}

static void write_cmd_data(const uint8_t *cmd_buffer, uint16_t cmd_byte_size,
                            const uint8_t *data_buffer, uint16_t data_byte_size)
{
    uint8_t tx_buffer[520];
    uint16_t transfer_size = cmd_byte_size + data_byte_size;

    if (transfer_size > sizeof(tx_buffer)) return;

    memcpy(tx_buffer, cmd_buffer, cmd_byte_size);
    memcpy(&tx_buffer[cmd_byte_size], data_buffer, data_byte_size);

    hal_spi_transfer(tx_buffer, transfer_size, NULL, 0);
}

/*******************************************************************************
 * Public Driver API
 *******************************************************************************/
void FLASH_init(void)
{
    hal_spi_init();
}

void FLASH_read_device_id(uint8_t *manufacturer_id, uint8_t *device_id, uint8_t *capacity_id)
{
    uint8_t read_device_id_cmd = DEVICE_ID_READ;
    uint8_t read_buffer[3] = {0};

    hal_spi_select();
    hal_spi_transfer(&read_device_id_cmd, 1, read_buffer, sizeof(read_buffer));
    hal_spi_deselect();

    if (manufacturer_id) *manufacturer_id = read_buffer[0];
    if (device_id)       *device_id       = read_buffer[1];
    if (capacity_id)     *capacity_id     = read_buffer[2];
}

void FLASH_read(uint32_t address, uint8_t *rx_buffer, size_t size_in_bytes)
{
    uint8_t cmd_buffer[5];
    size_t bytes_read = 0;

    write_enable();
    enter_4byte_address_mode();

    /* Chunk reads into max 32 KB blocks to prevent 16-bit uint16_t length overflow */
    while (bytes_read < size_in_bytes)
    {
        uint32_t current_addr = address + (uint32_t)bytes_read;
        size_t chunk_size = size_in_bytes - bytes_read;
        if (chunk_size > 32768) {
            chunk_size = 32768;
        }

        cmd_buffer[0] = READ_ARRAY_OPCODE;
        cmd_buffer[1] = (uint8_t)((current_addr >> 24) & 0xFF);
        cmd_buffer[2] = (uint8_t)((current_addr >> 16) & 0xFF);
        cmd_buffer[3] = (uint8_t)((current_addr >> 8) & 0xFF);
        cmd_buffer[4] = (uint8_t)(current_addr & 0xFF);

        hal_spi_select();
        hal_spi_transfer(cmd_buffer, 5, &rx_buffer[bytes_read], (uint16_t)chunk_size);
        hal_spi_deselect();

        bytes_read += chunk_size;
    }
}

void FLASH_global_unprotect(void)
{
    uint8_t cmd_buffer[2];
    write_enable();
    cmd_buffer[0] = WRITE_STATUS1_OPCODE;
    cmd_buffer[1] = 0x00;

    hal_spi_select();
    hal_spi_transfer(cmd_buffer, 2, NULL, 0);
    hal_spi_deselect();
    wait_program_or_erase_controller_ready();
}

void FLASH_erase_64k_block(uint32_t address)
{
    uint8_t cmd_buffer[5];

    write_enable();
    enter_4byte_address_mode();
    write_enable();

    cmd_buffer[0] = ERASE_64K_BLOCK_OPCODE;
    cmd_buffer[1] = (uint8_t)((address >> 24) & 0xFF);
    cmd_buffer[2] = (uint8_t)((address >> 16) & 0xFF);
    cmd_buffer[3] = (uint8_t)((address >> 8) & 0xFF);
    cmd_buffer[4] = (uint8_t)(address & 0xFF);

    hal_spi_select();
    hal_spi_transfer(cmd_buffer, 5, NULL, 0);
    hal_spi_deselect();
    wait_program_or_erase_controller_ready();
}

void FLASH_erase_range(uint32_t address, size_t count)
{
    if (count == 0) return;

    uint32_t start_addr = address;
    uint32_t end_addr   = address + (uint32_t)count;
    uint32_t block_addr = start_addr & ~(0xFFFFUL);

    while (block_addr < end_addr) {
        FLASH_erase_64k_block(block_addr);
        block_addr += 0x10000UL;
    }
}

void FLASH_die_256MB_erase(uint8_t die_number)
{
    uint8_t cmd_buffer[5];
    uint32_t die_address = ((uint32_t)die_number) * 0x02000000UL;

    write_enable();
    enter_4byte_address_mode();
    write_enable();

    cmd_buffer[0] = DIE_256MB_ERASE_OPCODE;
    cmd_buffer[1] = (uint8_t)((die_address >> 24) & 0xFF);
    cmd_buffer[2] = (uint8_t)((die_address >> 16) & 0xFF);
    cmd_buffer[3] = (uint8_t)((die_address >> 8) & 0xFF);
    cmd_buffer[4] = (uint8_t)(die_address & 0xFF);

    hal_spi_select();
    hal_spi_transfer(cmd_buffer, 5, NULL, 0);
    hal_spi_deselect();
    wait_program_or_erase_controller_ready();
}

void FLASH_program(uint32_t address, uint8_t *write_buffer, size_t size_in_bytes)
{
    uint8_t cmd_buffer[5];
    uint32_t in_buffer_idx = 0;
    uint32_t target_addr = address;

    write_enable();
    enter_4byte_address_mode();

    while (in_buffer_idx < size_in_bytes)
    {
        uint32_t nb_bytes_to_write = 0x100 - (target_addr & 0xFF);
        uint32_t size_left = size_in_bytes - in_buffer_idx;
        
        if (size_left < nb_bytes_to_write)
        {
            nb_bytes_to_write = size_left;
        }

        write_enable();
        enter_4byte_address_mode();
        write_enable();

        cmd_buffer[0] = PROGRAM_PAGE_CMD;
        cmd_buffer[1] = (uint8_t)((target_addr >> 24) & 0xFF);
        cmd_buffer[2] = (uint8_t)((target_addr >> 16) & 0xFF);
        cmd_buffer[3] = (uint8_t)((target_addr >> 8) & 0xFF);
        cmd_buffer[4] = (uint8_t)(target_addr & 0xFF);

        hal_spi_select();
        write_cmd_data(cmd_buffer, sizeof(cmd_buffer), &write_buffer[in_buffer_idx], (uint16_t)nb_bytes_to_write);
        hal_spi_deselect();
        wait_program_or_erase_controller_ready();

        target_addr += nb_bytes_to_write;
        in_buffer_idx += nb_bytes_to_write;
    }

    hal_spi_select();
    cmd_buffer[0] = WRITE_DISABLE_CMD;
    hal_spi_transfer(cmd_buffer, 1, NULL, 0);
    hal_spi_deselect();
}
