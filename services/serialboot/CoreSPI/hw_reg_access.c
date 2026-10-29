#ifndef asm
#define asm __asm__
#endif

#include <stdint.h>
#include "hal/hw_reg_access.h"
#include "hal/cpu_types.h"

#ifndef addr_t
typedef uintptr_t addr_t;
#endif

/* ---------------- 32-bit Register Access ---------------- */

void HW_set_32bit_reg(addr_t reg_addr, uint32_t value)
{
    volatile uint32_t *p_reg = (volatile uint32_t *)reg_addr;
    *p_reg = value;
}

uint32_t HW_get_32bit_reg(addr_t reg_addr)
{
    volatile uint32_t *p_reg = (volatile uint32_t *)reg_addr;
    return *p_reg;
}

void HW_set_32bit_reg_field(addr_t reg_addr, int_fast8_t shift, uint32_t mask, uint32_t value)
{
    volatile uint32_t *p_reg = (volatile uint32_t *)reg_addr;
    uint32_t old_value = *p_reg;
    uint32_t new_value = (value << shift);
    *p_reg = (new_value & mask) | (old_value & ~mask);
}

uint32_t HW_get_32bit_reg_field(addr_t reg_addr, int_fast8_t shift, uint32_t mask)
{
    volatile uint32_t *p_reg = (volatile uint32_t *)reg_addr;
    return ((*p_reg) & mask) >> shift;
}

/* ---------------- 16-bit Register Access ---------------- */

void HW_set_16bit_reg(addr_t reg_addr, uint_fast16_t value)
{
    volatile uint16_t *p_reg = (volatile uint16_t *)reg_addr;
    *p_reg = (uint16_t)value;
}

uint16_t HW_get_16bit_reg(addr_t reg_addr)
{
    volatile uint16_t *p_reg = (volatile uint16_t *)reg_addr;
    return *p_reg;
}

void HW_set_16bit_reg_field(addr_t reg_addr, int_fast8_t shift, uint_fast16_t mask, uint_fast16_t value)
{
    volatile uint16_t *p_reg = (volatile uint16_t *)reg_addr;
    uint32_t old_value = *p_reg;
    uint32_t new_value = ((uint32_t)value << shift);
    *p_reg = (uint16_t)((new_value & mask) | (old_value & ~mask));
}

uint16_t HW_get_16bit_reg_field(addr_t reg_addr, int_fast8_t shift, uint_fast16_t mask)
{
    volatile uint16_t *p_reg = (volatile uint16_t *)reg_addr;
    return (uint16_t)(((*p_reg) & mask) >> shift);
}

/* ---------------- 8-bit Register Access ---------------- */

void HW_set_8bit_reg(addr_t reg_addr, uint_fast8_t value)
{
    volatile uint8_t *p_reg = (volatile uint8_t *)reg_addr;
    *p_reg = (uint8_t)value;
}

uint8_t HW_get_8bit_reg(addr_t reg_addr)
{
    volatile uint8_t *p_reg = (volatile uint8_t *)reg_addr;
    return *p_reg;
}

void HW_set_8bit_reg_field(addr_t reg_addr, int_fast8_t shift, uint_fast8_t mask, uint_fast8_t value)
{
    volatile uint8_t *p_reg = (volatile uint8_t *)reg_addr;
    uint32_t old_value = *p_reg;
    uint32_t new_value = ((uint32_t)value << shift);
    *p_reg = (uint8_t)((new_value & mask) | (old_value & ~mask));
}

uint8_t HW_get_8bit_reg_field(addr_t reg_addr, int_fast8_t shift, uint_fast8_t mask)
{
    volatile uint8_t *p_reg = (volatile uint8_t *)reg_addr;
    return (uint8_t)(((*p_reg) & mask) >> shift);
}