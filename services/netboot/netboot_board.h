#ifndef NETBOOT_BOARD_H
#define NETBOOT_BOARD_H

#ifndef asm
#define asm __asm__
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Freestanding type stub for MAC interrupt array */
typedef int PLIC_IRQn_Type;

/* Driver prerequisite headers in exact dependency order */
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_mac_sw_cfg.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_registers.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_mac_regs.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_mac.h"
#include "drivers/mss/mss_ethernet_mac/phy.h"

typedef struct {
    /* MAC Controller and Clock/Reset Controls */
    mss_mac_instance_t         *mac_dev;
    uint32_t                    sysreg_clk_mask;   /* (1U << 1) for GEM0 */
    uint32_t                    sysreg_rst_mask;   /* (1U << 1) for GEM0 */

    /* PHY & Interface Configuration */
    uint8_t                     phy_addr;          /* 11 for Discovery Kit */
    uint8_t                     pcs_phy_addr;      /* 16 */
    uint32_t                    phy_type;          /* MSS_MAC_DEV_PHY_VSC8221 */
    uint32_t                    phy_flags;         /* PHY_VSC8221_EEPROM_INIT */
    uint32_t                    interface_type;    /* TBI */

    /* Driver Typedefs from mss_ethernet_mac_types.h */
    mss_mac_phy_init_t          phy_init;
    mss_mac_phy_set_speed_t     phy_set_link_speed;
    mss_mac_phy_autonegotiate_t phy_autonegotiate;
    mss_mac_phy_autonegotiate_t phy_mac_autonegotiate;
    mss_mac_phy_get_link_status_t phy_get_link_status;

    /* Station MAC Address Provider */
    void (*get_mac_address)(uint8_t *mac_buf);
} netboot_hw_config_t;

const netboot_hw_config_t *NETBOOT_Board_GetConfig(void);

#endif /* NETBOOT_BOARD_H */