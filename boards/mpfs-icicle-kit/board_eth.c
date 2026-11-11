#include "netboot_board.h"
#include <string.h>

/* Set to 1 for Port J1 (GEM0), or 0 for Port J2 (GEM1) */
#define ICICLE_USE_PORT_J1 0

static void icicle_get_mac_address(uint8_t *mac_buf)
{
#if ICICLE_USE_PORT_J1
    static const uint8_t s_icicle_mac[6] = { 0x00, 0x04, 0xA3, 0x11, 0x22, 0x33 };
#else
    static const uint8_t s_icicle_mac[6] = { 0x00, 0x04, 0xA3, 0x11, 0x22, 0x34 };
#endif
    memcpy(mac_buf, s_icicle_mac, 6);
}

static const netboot_hw_config_t s_icicle_net_cfg = {
#if ICICLE_USE_PORT_J1
    /* Port J1: GEM0 Data + GEM1 MDIO */
    .mac_dev             = &g_mac0,
    .phy_controller      = &g_mac1,             /* Route MDIO management to GEM1 */
    .sysreg_clk_mask     = (uint32_t)((1U << 1) | (1U << 2)), /* Must clock both GEMs */
    .sysreg_rst_mask     = (uint32_t)((1U << 1) | (1U << 2)), /* Reset both GEMs */
    .phy_addr            = 8U,                  /* VSC8662 Channel 0 MDIO address */
#else
    /* Port J2: GEM1 Direct */
    .mac_dev             = &g_mac1,
    .phy_controller      = NULL,
    .sysreg_clk_mask     = (uint32_t)(1U << 2),
    .sysreg_rst_mask     = (uint32_t)(1U << 2),
    .phy_addr            = 9U,                  /* VSC8662 Channel 1 MDIO address */
#endif

    .pcs_phy_addr        = 16U,
    .phy_type            = MSS_MAC_DEV_PHY_VSC8662,
    .phy_flags           = 0U,
    .interface_type      = TBI,

    .phy_init            = MSS_MAC_VSC8662_phy_init,
    .phy_set_link_speed  = MSS_MAC_VSC8662_phy_set_link_speed,
    .phy_autonegotiate   = MSS_MAC_VSC8662_phy_autonegotiate,
    .phy_mac_autonegotiate = MSS_MAC_VSC8662_mac_autonegotiate,
    .phy_get_link_status = MSS_MAC_VSC8662_phy_get_link_status,

    .get_mac_address     = icicle_get_mac_address,
};

const netboot_hw_config_t *NETBOOT_Board_GetConfig(void)
{
    return &s_icicle_net_cfg;
}