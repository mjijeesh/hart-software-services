#include "netboot_board.h"
#include <string.h>

static void icicle_get_mac_address(uint8_t *mac_buf)
{
    /* Factory MAC default for Icicle Kit */
    static const uint8_t s_icicle_mac[6] = { 0x00, 0x04, 0xA3, 0x11, 0x22, 0x33 };
    memcpy(mac_buf, s_icicle_mac, 6);
}

static const netboot_hw_config_t s_icicle_net_cfg = {
    .mac_dev             = &g_mac1,             /* GEM1 (RJ45 Port J2) */
    .sysreg_clk_mask     = (uint32_t)(1U << 2), /* GEM1 peripheral clock bit */
    .sysreg_rst_mask     = (uint32_t)(1U << 2), /* GEM1 peripheral reset bit */

    .phy_addr            = 9U,                  /* VSC8662 Port 1 MDIO address */
    .pcs_phy_addr        = 16U,                 /* SGMII PCS address */
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