#ifndef NETBOOT_BOARD_H
#define NETBOOT_BOARD_H

#ifndef asm
#define asm __asm__
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef int PLIC_IRQn_Type;

#include "drivers/mss/mss_ethernet_mac/mss_ethernet_mac_sw_cfg.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_registers.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_mac_regs.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_mac.h"
#include "drivers/mss/mss_ethernet_mac/phy.h"

typedef struct {
    /* MAC Controller and Clock/Reset Controls */
    mss_mac_instance_t         *mac_dev;
    mss_mac_instance_t         *phy_controller;   /* NULL if same MAC; &g_mac1 for Icicle J1 */
    uint32_t                    sysreg_clk_mask;   /* (1U << 1) | (1U << 2) */
    uint32_t                    sysreg_rst_mask;   /* (1U << 1) | (1U << 2) */

    /* PHY & Interface Configuration */
    uint8_t                     phy_addr;          /* 8 for J1; 9 for J2 */
    uint8_t                     pcs_phy_addr;      /* 16 */
    uint32_t                    phy_type;          /* MSS_MAC_DEV_PHY_VSC8662 */
    uint32_t                    phy_flags;
    uint32_t                    interface_type;    /* TBI */

    /* Driver Typedefs */
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