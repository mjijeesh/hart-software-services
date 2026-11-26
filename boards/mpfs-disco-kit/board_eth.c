#include "netboot_board.h"
#include "drivers/mss/mss_ethernet_mac/phy.h"
#include <string.h>

static void disco_get_mac_address(uint8_t *mac_buf)
{
    static const uint8_t s_disco_mac[6] = { 0x00, 0xFC, 0x00, 0x12, 0x34, 0x58 };
    memcpy(mac_buf, s_disco_mac, 6);
}

static const netboot_hw_config_t s_disco_net_cfg = {
    .mac_dev             = &g_mac0,
    .sysreg_clk_mask     = (uint32_t)(1U << 1),
    .sysreg_rst_mask     = (uint32_t)(1U << 1),

    .phy_addr            = 11U,                 /* VSC8221 MDIO address */
    .pcs_phy_addr        = 16U,
    .phy_type            = MSS_MAC_DEV_PHY_VSC8221,
    .phy_flags           = PHY_VSC8221_EEPROM_INIT,
    .interface_type      = TBI,

    .phy_init            = MSS_MAC_VSC8221_phy_init,
    .phy_set_link_speed  = MSS_MAC_VSC8221_phy_set_link_speed,
    .phy_autonegotiate   = MSS_MAC_VSC8221_phy_autonegotiate,
    .phy_mac_autonegotiate = MSS_MAC_VSC8221_mac_autonegotiate,
    .phy_get_link_status = MSS_MAC_VSC8221_phy_get_link_status,

    .get_mac_address     = disco_get_mac_address,
};

const netboot_hw_config_t *NETBOOT_Board_GetConfig(void)
{
    return &s_disco_net_cfg;
}