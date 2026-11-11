#ifndef asm
#define asm __asm__
#endif

#include "config.h"
#include "hss_types.h"
#include "hss_debug.h"
#include <string.h>

typedef int PLIC_IRQn_Type;

/* HAL and System Registers */
#include "mpfs_hal/common/mss_peripherals.h"
#include "mpfs_hal/common/mss_sysreg.h"

/* Driver headers */
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_mac_sw_cfg.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_registers.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_mac_regs.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_mac.h"
#include "drivers/mss/mss_ethernet_mac/phy.h"
#include "hal/hal.h"
#include "netboot_service.h"
#include "services/ipi_poll/ipi_poll_service.h"
#include "sbi/riscv_encoding.h"

#ifndef PRV_M
#define PRV_M 3
#endif

extern uint8_t PLIC_mac0_int_IRQHandler(void);

#define MAC0_DEV (&g_mac0)
#define TFTP_PORT 69

/* -------------------------------------------------------------------------- */
/* Freestanding Linker Stubs & Prototypes                                     */
/* -------------------------------------------------------------------------- */

uint32_t __bswapsi2(uint32_t u);

psr_t HAL_disable_interrupts(void)
{
    return 0U;
}

void HAL_restore_interrupts(psr_t saved_psr)
{
    (void)saved_psr;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++) != '\0') {
    }
    return dest;
}

uint32_t __bswapsi2(uint32_t u)
{
    return (((u & 0x000000FFUL) << 24) |
            ((u & 0x0000FF00UL) << 8)  |
            ((u & 0x00FF0000UL) >> 8)  |
            ((u & 0xFF000000UL) >> 24));
}

static inline uint32_t swap32(uint32_t v)
{
    return (((v & 0x000000FFUL) << 24) |
            ((v & 0x0000FF00UL) << 8)  |
            ((v & 0x00FF0000UL) >> 8)  |
            ((v & 0xFF000000UL) >> 24));
}

static uint32_t s_netboot_txid = 0x1000;

void netboot_jump(uintptr_t addr)
{
    mHSS_PRINTF("\n[NETBOOT] Synchronizing caches and dispatching all U54 harts to 0x%016lx...\n", (unsigned long)addr);

    __asm__ __volatile__("fence rw, rw" ::: "memory");
    __asm__ __volatile__("fence.i" ::: "memory");

    for (enum HSSHartId hart = HSS_HART_U54_1; hart <= HSS_HART_U54_4; hart++) {
        IPI_Send(hart,
                 IPI_MSG_GOTO,
                 s_netboot_txid++,
                 PRV_M,
                 (void *)addr,
                 NULL);
    }

    for (volatile int i = 0; i < 200000; i++);

    mHSS_PRINTF("[NETBOOT] All U54 harts launched. Application active on UART1 (/dev/ttyUSB1).\n");
    mHSS_PRINTF("[NETBOOT] Returning to HSS TinyCLI.\n\n");
}

/* -------------------------------------------------------------------------- */
/* Hardware State & Distinct Ring Buffers                                     */
/* -------------------------------------------------------------------------- */

static uint8_t s_mac_addr[6] = { 0x00, 0xFC, 0x00, 0x12, 0x34, 0x58 };
static uint8_t s_board_ip[4] = { 192, 168, 20, 50 }; /* Matches host subnet */
static uint8_t s_server_mac[6];
static bool s_driver_active = false;

static uint8_t s_rx_buffer[MSS_MAC_RX_RING_SIZE][1536] __attribute__((aligned(16)));
static uint8_t s_tx_buffer[1536] __attribute__((aligned(16)));

static volatile bool s_rx_packet_ready = false;
static volatile uint32_t s_rx_packet_length = 0;
static uint8_t *s_active_rx_ptr = NULL;

static void mac_rx_callback(void *this_mac,
                            uint32_t queue_no,
                            uint8_t *p_rx_data,
                            uint32_t rx_length,
                            mss_mac_rx_desc_t *p_rx_desc,
                            void *p_user_data)
{
    (void)this_mac;
    (void)queue_no;
    (void)p_rx_desc;
    (void)p_user_data;

    s_active_rx_ptr = p_rx_data;
    s_rx_packet_length = rx_length;
    s_rx_packet_ready = true;
}

static uint16_t checksum(const void *data, size_t len)
{
    const uint8_t *ptr = (const uint8_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += ((uint16_t)ptr[0] << 8) | ptr[1];
        ptr += 2;
        len -= 2;
    }
    if (len) sum += ((uint16_t)ptr[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/* -------------------------------------------------------------------------- */
/* Hardware Initialization Sequence                                           */
/* -------------------------------------------------------------------------- */

static bool netboot_hw_init(void)
{
    mss_mac_cfg_t mac_config;

    mHSS_PRINTF("\n================ [NETBOOT: HARDWARE INIT] ================\n");

    /* Step 1: Clock Enable and Full Hardware Reset Pulse */
    mHSS_PRINTF("[NETBOOT] [1/5] Cycling GEM0 peripheral reset and clock...\n");
    SYSREG->SUBBLK_CLOCK_CR |= (uint32_t)(1U << 1);
    SYSREG->SOFT_RESET_CR   |= (uint32_t)(1U << 1); /* Assert reset */
    for (volatile int d = 0; d < 10000; d++);
    SYSREG->SOFT_RESET_CR   &= (uint32_t)~(1U << 1); /* Release reset */

    /* Step 2: MAC Driver Configuration */
    mHSS_PRINTF("[NETBOOT] [2/5] Configuring GEM0 MAC Core...\n");
    mHSS_PRINTF("[NETBOOT]       Station MAC Address : %02X:%02X:%02X:%02X:%02X:%02X\n",
                s_mac_addr[0], s_mac_addr[1], s_mac_addr[2],
                s_mac_addr[3], s_mac_addr[4], s_mac_addr[5]);

    MSS_MAC_cfg_struct_def_init(&mac_config);

    mac_config.speed_duplex_select   = MSS_MAC_ANEG_ALL_SPEEDS;
    mac_config.tsu_clock_select      = 1U;
    mac_config.phy_addr              = 11U;
    mac_config.phy_type              = MSS_MAC_DEV_PHY_VSC8221;
    mac_config.phy_flags             = PHY_VSC8221_EEPROM_INIT;
    mac_config.pcs_phy_addr          = 16U;
    mac_config.interface_type        = TBI;

    mac_config.phy_init              = MSS_MAC_VSC8221_phy_init;
    mac_config.phy_set_link_speed    = MSS_MAC_VSC8221_phy_set_link_speed;
    mac_config.phy_autonegotiate     = MSS_MAC_VSC8221_phy_autonegotiate;
    mac_config.phy_mac_autonegotiate = MSS_MAC_VSC8221_mac_autonegotiate;
    mac_config.phy_get_link_status   = MSS_MAC_VSC8221_phy_get_link_status;
    memcpy(mac_config.mac_addr, s_mac_addr, 6);

    MSS_MAC_init(MAC0_DEV, &mac_config);

    MAC0_DEV->mac_q_int[0] = 0;
    MAC0_DEV->mac_q_int[1] = 0;
    MAC0_DEV->mac_q_int[2] = 0;
    MAC0_DEV->mac_q_int[3] = 0;

    MAC0_DEV->mac_base->NETWORK_CONFIG |= GEM_COPY_ALL_FRAMES;

    MAC0_DEV->mac_base->NETWORK_CONTROL &= ~GEM_ENABLE_TRANSMIT;
    MAC0_DEV->mac_base->TX_Q_SEG_ALLOC_Q0TO3 = 2;
    MAC0_DEV->mac_base->NETWORK_CONTROL |= GEM_ENABLE_TRANSMIT;

    /* Step 3: Verify Hardware PHY Identification Registers */
    uint16_t phy_id1 = MSS_MAC_read_phy_reg(MAC0_DEV, 11U, MII_PHYSID1);
    uint16_t phy_id2 = MSS_MAC_read_phy_reg(MAC0_DEV, 11U, MII_PHYSID2);
    mHSS_PRINTF("[NETBOOT] [3/5] Probing VSC8221 Hardware Registers...\n");
    mHSS_PRINTF("[NETBOOT]       PHY ID 1 (Reg 0x02) : 0x%04X\n", phy_id1);
    mHSS_PRINTF("[NETBOOT]       PHY ID 2 (Reg 0x03) : 0x%04X\n", phy_id2);

    /* Step 4: Prime RX Ring Buffers in Polled Mode */
    mHSS_PRINTF("[NETBOOT] [4/5] Priming DMA Descriptor Queues in Polled Mode...\n");
    MSS_MAC_set_rx_callback(MAC0_DEV, 0, mac_rx_callback);
    for (uint32_t i = 0; i < MSS_MAC_RX_RING_SIZE; ++i) {
        if (i != (MSS_MAC_RX_RING_SIZE - 1)) {
            MSS_MAC_receive_pkt(MAC0_DEV, 0, s_rx_buffer[i], NULL, MSS_MAC_INT_DISABLE);
        } else {
            MSS_MAC_receive_pkt(MAC0_DEV, 0, s_rx_buffer[i], NULL, MSS_MAC_INT_ARM);
        }
    }

    /* Step 5: Wait for Link Auto-Negotiation */
    mHSS_PRINTF("[NETBOOT] [5/5] Waiting for Copper Auto-Negotiation...");

    mss_mac_speed_t speed = MSS_MAC_1000MBPS;
    uint8_t duplex = 1;
    uint32_t retries = 50;
    bool link_ok = false;

    while (retries--) {
        if (MSS_MAC_get_link_status(MAC0_DEV, &speed, &duplex) != 0) {
            link_ok = true;
            break;
        }
        MSS_MAC_delay(MAC0_DEV, 100000);
        mHSS_PRINTF(".");
    }
    mHSS_PRINTF("\n");

    if (!link_ok) {
        mHSS_PRINTF("[NETBOOT] ERROR: Link timeout! Please verify Ethernet cable.\n");
        mHSS_PRINTF("==========================================================\n\n");
        return false;
    }

    mHSS_PRINTF("[NETBOOT]       Link State          : UP\n");
    mHSS_PRINTF("[NETBOOT]       Negotiated Speed    : %s\n",
                (speed == MSS_MAC_1000MBPS) ? "1000 Mbps (Gigabit)" :
                (speed == MSS_MAC_100MBPS)  ? "100 Mbps (Fast Ethernet)" : "10 Mbps");
    mHSS_PRINTF("[NETBOOT]       Duplex Mode         : %s\n", duplex ? "Full Duplex" : "Half Duplex");

    /* Critical: Allow switch port forwarding / STP learning time */
    mHSS_PRINTF("[NETBOOT] Allowing switch port to enter forwarding state (1.5s)...\n");
    MSS_MAC_delay(MAC0_DEV, 1500000);

    mHSS_PRINTF("==========================================================\n\n");

    s_driver_active = true;
    return true;
}

void NETBOOT_Service_Deinit(void)
{
    if (!s_driver_active) return;

    mHSS_PRINTF("[NETBOOT] Quiescing GEM0 MAC...\n");

    if (g_mac0.mac_base) {
        g_mac0.mac_base->NETWORK_CONTROL = 0;
        g_mac0.mac_base->INT_DISABLE = 0xFFFFFFFFUL;
    }

    s_driver_active = false;
    mHSS_PRINTF("[NETBOOT] MAC released.\n");
}

/* -------------------------------------------------------------------------- */
/* Dynamic DHCP Client with Active Retransmission                             */
/* -------------------------------------------------------------------------- */

static bool acquire_dhcp_lease(uint32_t *discovered_server_ip)
{
    uint8_t frame[512] = {0};
    uint32_t xid = 0x3903F326;

    mHSS_PRINTF("================ [NETBOOT: DHCP NEGOTIATION] ================\n");

    memset(&frame[0], 0xFF, 6);
    memcpy(&frame[6], s_mac_addr, 6);
    frame[12] = 0x08; frame[13] = 0x00;

    uint16_t dhcp_payload_len = 256;
    uint16_t udp_len = 8 + dhcp_payload_len;
    uint16_t ip_len  = 20 + udp_len;
    uint32_t total_len = 14 + ip_len;

    frame[14] = 0x45; frame[15] = 0x00;
    frame[16] = (uint8_t)(ip_len >> 8); frame[17] = (uint8_t)(ip_len & 0xFF);
    frame[18] = 0x00; frame[19] = 0x01;
    frame[20] = 0x00; frame[21] = 0x00;
    frame[22] = 128;  frame[23] = 17;
    memset(&frame[26], 0x00, 4);
    memset(&frame[30], 0xFF, 4);

    uint16_t ip_cksum = checksum(&frame[14], 20);
    frame[24] = (uint8_t)(ip_cksum >> 8);
    frame[25] = (uint8_t)(ip_cksum & 0xFF);

    frame[34] = 0x00; frame[35] = 68;
    frame[36] = 0x00; frame[37] = 67;
    frame[38] = (uint8_t)(udp_len >> 8); frame[39] = (uint8_t)(udp_len & 0xFF);

    uint8_t *dhcp = &frame[42];
    dhcp[0] = 0x01;
    dhcp[1] = 0x01;
    dhcp[2] = 0x06;
    dhcp[3] = 0x00;
    dhcp[4] = (uint8_t)((xid >> 24) & 0xFF);
    dhcp[5] = (uint8_t)((xid >> 16) & 0xFF);
    dhcp[6] = (uint8_t)((xid >> 8) & 0xFF);
    dhcp[7] = (uint8_t)(xid & 0xFF);
    dhcp[10] = 0x80;
    memcpy(&dhcp[28], s_mac_addr, 6);

    dhcp[236] = 0x63; dhcp[237] = 0x82; dhcp[238] = 0x53; dhcp[239] = 0x63;
    dhcp[240] = 53; dhcp[241] = 1; dhcp[242] = 1;
    dhcp[243] = 55; dhcp[244] = 3; dhcp[245] = 1; dhcp[246] = 3; dhcp[247] = 6;
    dhcp[248] = 12; dhcp[249] = 5; memcpy(&dhcp[250], "Polar", 5);
    dhcp[255] = 255;

    /* Up to 4 retransmission attempts (1 second wait per attempt) */
    for (uint32_t attempt = 1; attempt <= 4; attempt++) {
        mHSS_PRINTF("[DHCP] Broadcasting DHCP Discover (Attempt %u/4)...\n", attempt);
        MSS_MAC_send_pkt(MAC0_DEV, 0, frame, total_len, NULL);

        for (uint32_t t = 0; t < 200 && MAC0_DEV->queue[0].nb_available_tx_desc < MSS_MAC_TX_RING_SIZE; ++t) {
            PLIC_mac0_int_IRQHandler();
            MSS_MAC_delay(MAC0_DEV, 20);
        }

        for (uint32_t ms = 0; ms < 1000; ++ms) {
            PLIC_mac0_int_IRQHandler();

            if (s_rx_packet_ready) {
                s_rx_packet_ready = false;
                uint8_t *rx = s_active_rx_ptr;
                uint32_t len = s_rx_packet_length;

                if (len >= 282 && rx[12] == 0x08 && rx[13] == 0x00 && rx[23] == 17) {
                    uint16_t dst_p = ((uint16_t)rx[36] << 8) | rx[37];
                    if (dst_p == 68) {
                        uint8_t *rx_dhcp = &rx[42];
                        uint32_t rx_xid = ((uint32_t)rx_dhcp[4] << 24) |
                                          ((uint32_t)rx_dhcp[5] << 16) |
                                          ((uint32_t)rx_dhcp[6] << 8)  |
                                          (uint32_t)rx_dhcp[7];

                        if (rx_xid == xid && rx_dhcp[0] == 0x02) {
                            memcpy(s_board_ip, &rx_dhcp[16], 4);
                            uint32_t srv_ip = ((uint32_t)rx_dhcp[20] << 24) |
                                              ((uint32_t)rx_dhcp[21] << 16) |
                                              ((uint32_t)rx_dhcp[22] << 8)  |
                                              (uint32_t)rx_dhcp[23];

                            if (srv_ip == 0) {
                                uint8_t *opt = &rx_dhcp[240];
                                while (*opt != 255 && (opt - rx_dhcp) < (int)len) {
                                    if (*opt == 54 && *(opt + 1) == 4) {
                                        srv_ip = ((uint32_t)opt[2] << 24) |
                                                 ((uint32_t)opt[3] << 16) |
                                                 ((uint32_t)opt[4] << 8)  |
                                                 (uint32_t)opt[5];
                                        break;
                                    }
                                    opt += (2 + *(opt + 1));
                                }
                            }

                            mHSS_PRINTF("[DHCP] DHCP Offer Successfully Received!\n");
                            mHSS_PRINTF("[DHCP]   |- Assigned Board IP : %u.%u.%u.%u\n",
                                        s_board_ip[0], s_board_ip[1], s_board_ip[2], s_board_ip[3]);
                            mHSS_PRINTF("[DHCP]   \\- Gateway/Server IP : %u.%u.%u.%u\n",
                                        (srv_ip >> 24) & 0xFF, (srv_ip >> 16) & 0xFF,
                                        (srv_ip >> 8) & 0xFF, srv_ip & 0xFF);
                            mHSS_PRINTF("=============================================================\n\n");

                            if (discovered_server_ip && srv_ip != 0) {
                                *discovered_server_ip = srv_ip;
                            }
                            MSS_MAC_receive_pkt(MAC0_DEV, 0, rx, NULL, MSS_MAC_INT_ENABLE);
                            return true;
                        }
                    }
                }
                MSS_MAC_receive_pkt(MAC0_DEV, 0, rx, NULL, MSS_MAC_INT_ENABLE);
            }
            MSS_MAC_delay(MAC0_DEV, 1000);
        }
    }

    mHSS_PRINTF("[DHCP] WARNING: Timeout waiting for lease. Using fallback IP: %u.%u.%u.%u\n",
                s_board_ip[0], s_board_ip[1], s_board_ip[2], s_board_ip[3]);
    mHSS_PRINTF("=============================================================\n\n");
    return false;
}

/* -------------------------------------------------------------------------- */
/* ARP Resolution with Active Retransmission                                  */
/* -------------------------------------------------------------------------- */

static bool resolve_arp(uint32_t target_ip)
{
    uint8_t arp_pkt[60];
    memset(arp_pkt, 0, sizeof(arp_pkt));

    memset(&arp_pkt[0], 0xFF, 6);
    memcpy(&arp_pkt[6], s_mac_addr, 6);
    arp_pkt[12] = 0x08; arp_pkt[13] = 0x06;
    arp_pkt[14] = 0x00; arp_pkt[15] = 0x01;
    arp_pkt[16] = 0x08; arp_pkt[17] = 0x00;
    arp_pkt[18] = 0x06; arp_pkt[19] = 0x04;
    arp_pkt[20] = 0x00; arp_pkt[21] = 0x01;

    memcpy(&arp_pkt[22], s_mac_addr, 6);
    memcpy(&arp_pkt[28], s_board_ip, 4);
    memset(&arp_pkt[32], 0x00, 6);
    uint32_t t_ip = swap32(target_ip);
    memcpy(&arp_pkt[38], &t_ip, 4);

    /* Up to 4 retransmission attempts (750ms wait per attempt) */
    for (uint32_t attempt = 1; attempt <= 4; attempt++) {
        mHSS_PRINTF("[NETBOOT] Sending ARP Request for %u.%u.%u.%u (Attempt %u/4)...\n",
                    (target_ip >> 24) & 0xFF, (target_ip >> 16) & 0xFF,
                    (target_ip >> 8) & 0xFF, target_ip & 0xFF, attempt);

        MSS_MAC_send_pkt(MAC0_DEV, 0, arp_pkt, sizeof(arp_pkt), NULL);

        for (uint32_t t = 0; t < 200 && MAC0_DEV->queue[0].nb_available_tx_desc < MSS_MAC_TX_RING_SIZE; ++t) {
            PLIC_mac0_int_IRQHandler();
            MSS_MAC_delay(MAC0_DEV, 20);
        }

        for (uint32_t ms = 0; ms < 750; ++ms) {
            PLIC_mac0_int_IRQHandler();

            if (s_rx_packet_ready) {
                s_rx_packet_ready = false;
                uint8_t *rx = s_active_rx_ptr;

                if (s_rx_packet_length >= 42 && rx[12] == 0x08 && rx[13] == 0x06) {
                    if (rx[21] == 0x02) {
                        memcpy(s_server_mac, &rx[6], 6);
                        mHSS_PRINTF("[NETBOOT] Server MAC Resolved: %02X:%02X:%02X:%02X:%02X:%02X\n",
                                    s_server_mac[0], s_server_mac[1], s_server_mac[2],
                                    s_server_mac[3], s_server_mac[4], s_server_mac[5]);
                        MSS_MAC_receive_pkt(MAC0_DEV, 0, rx, NULL, MSS_MAC_INT_ENABLE);
                        return true;
                    }
                }
                MSS_MAC_receive_pkt(MAC0_DEV, 0, rx, NULL, MSS_MAC_INT_ENABLE);
            }
            MSS_MAC_delay(MAC0_DEV, 1000);
        }
    }
    return false;
}

netboot_status_t NETBOOT_Service_FetchImage(uint32_t server_ip,
                                            const char *filename,
                                            uintptr_t dest_addr,
                                            size_t *bytes_read)
{
    if (!netboot_hw_init()) {
        NETBOOT_Service_Deinit();
        return NETBOOT_STATUS_LINK_DOWN;
    }

    uint32_t dhcp_server_ip = 0;
    acquire_dhcp_lease(&dhcp_server_ip);

    server_ip = 0xC0A8145EUL; /* 192.168.20.94 */
    mHSS_PRINTF("[NETBOOT] Target TFTP Server : 192.168.20.94 (Ubuntu Host)\n");

    if (!resolve_arp(server_ip)) {
        mHSS_PRINTF("[NETBOOT] ERROR: TFTP server MAC resolution timed out.\n");
        NETBOOT_Service_Deinit();
        return NETBOOT_STATUS_ARP_TIMEOUT;
    }

    uint8_t *tx = s_tx_buffer;
    memset(tx, 0, sizeof(s_tx_buffer));

    memcpy(tx, s_server_mac, 6);
    memcpy(tx + 6, s_mac_addr, 6);
    tx[12] = 0x08; tx[13] = 0x00;

    tx[14] = 0x45; tx[15] = 0x00;
    tx[18] = 0x12; tx[19] = 0x34;
    tx[20] = 0x00; tx[21] = 0x00;
    tx[22] = 64;   tx[23] = 17;
    memcpy(tx + 26, s_board_ip, 4);
    uint32_t dst_ip = swap32(server_ip);
    memcpy(tx + 30, &dst_ip, 4);

    uint16_t src_port = 50000;
    uint16_t dst_port = TFTP_PORT;
    uint8_t *tftp_req = tx + 42;
    *tftp_req++ = 0x00; *tftp_req++ = 0x01;
    strcpy((char *)tftp_req, filename);
    tftp_req += strlen(filename) + 1;
    strcpy((char *)tftp_req, "octet");
    tftp_req += strlen("octet") + 1;

    uint16_t udp_len = (uint16_t)(tftp_req - (tx + 34));
    uint16_t ip_len  = udp_len + 20;

    tx[16] = (ip_len >> 8); tx[17] = (ip_len & 0xFF);
    tx[24] = 0; tx[25] = 0;
    uint16_t ip_csum = checksum(tx + 14, 20);
    tx[24] = (ip_csum >> 8); tx[25] = (ip_csum & 0xFF);

    tx[34] = (src_port >> 8); tx[35] = (src_port & 0xFF);
    tx[36] = (dst_port >> 8); tx[37] = (dst_port & 0xFF);
    tx[38] = (udp_len >> 8);  tx[39] = (udp_len & 0xFF);
    tx[40] = 0; tx[41] = 0;

    uint32_t rrq_frame_len = (uint32_t)(14 + ip_len);
    if (rrq_frame_len < 60) rrq_frame_len = 60;

    uint16_t expected_block = 1;
    size_t total = 0;
    uint8_t *target = (uint8_t *)dest_addr;

    mHSS_PRINTF("[NETBOOT] Requesting file '%s' via TFTP...\n", filename);

    MSS_MAC_send_pkt(MAC0_DEV, 0, tx, rrq_frame_len, NULL);
    MAC0_DEV->mac_base->NETWORK_CONTROL |= GEM_ENABLE_RECEIVE;

    uint32_t rrq_retries = 5;
    uint32_t poll_count = 0;

    while (1) {
        PLIC_mac0_int_IRQHandler();

        if (s_rx_packet_ready) {
            s_rx_packet_ready = false;
            uint8_t *rx = s_active_rx_ptr;
            uint32_t rx_len = s_rx_packet_length;

            if (rx_len > 46 && rx[12] == 0x08 && rx[13] == 0x00 && rx[23] == 17) {
                uint16_t pkt_dst_port = ((uint16_t)rx[36] << 8) | rx[37];

                if (pkt_dst_port == src_port && memcmp(&rx[30], s_board_ip, 4) == 0) {
                    dst_port = ((uint16_t)rx[34] << 8) | rx[35];

                    uint8_t *tftp = rx + 42;
                    uint16_t opcode = ((uint16_t)tftp[0] << 8) | tftp[1];

                    if (opcode == 3) {
                        uint16_t block = ((uint16_t)tftp[2] << 8) | tftp[3];
                        uint16_t ulen = ((uint16_t)rx[38] << 8) | rx[39];
                        size_t dlen = (ulen >= 12) ? (ulen - 12) : 0;

                        if (block == expected_block) {
                            memcpy(target, tftp + 4, dlen);
                            target += dlen;
                            total += dlen;

                            memset(tx, 0, 60);
                            memcpy(tx, s_server_mac, 6);
                            memcpy(tx + 6, s_mac_addr, 6);
                            tx[12] = 0x08; tx[13] = 0x00;

                            tx[14] = 0x45; tx[15] = 0x00;
                            tx[16] = 0x00; tx[17] = 32;
                            tx[18] = 0x22; tx[19] = 0x11;
                            tx[22] = 64;   tx[23] = 17;
                            memcpy(tx + 26, s_board_ip, 4);
                            memcpy(tx + 30, &dst_ip, 4);

                            uint16_t csum = checksum(tx + 14, 20);
                            tx[24] = (csum >> 8); tx[25] = (csum & 0xFF);

                            tx[34] = (src_port >> 8); tx[35] = (src_port & 0xFF);
                            tx[36] = (dst_port >> 8); tx[37] = (dst_port & 0xFF);
                            tx[38] = 0x00; tx[39] = 12;

                            tx[42] = 0x00; tx[43] = 0x04;
                            tx[44] = (block >> 8); tx[45] = (block & 0xFF);

                            MSS_MAC_send_pkt(MAC0_DEV, 0, tx, 60, NULL);
                            expected_block++;
                            poll_count = 0;

                            if ((block % 100) == 0) {
                                mHSS_PRINTF("#");
                            }

                            if (dlen < 512) {
                                mHSS_PRINTF("\n[NETBOOT] Transfer complete! Total: %lu bytes\n", (unsigned long)total);
                                *bytes_read = total;
                                NETBOOT_Service_Deinit();
                                return NETBOOT_STATUS_OK;
                            }
                        }
                    } else if (opcode == 5) {
                        uint16_t err_code = ((uint16_t)tftp[2] << 8) | tftp[3];
                        char *err_msg = (char *)&tftp[4];
                        mHSS_PRINTF("\n[NETBOOT] TFTP Error (Code %u): %s\n", err_code, err_msg);
                        NETBOOT_Service_Deinit();
                        return NETBOOT_STATUS_TFTP_ERROR;
                    }
                }
            }
            MSS_MAC_receive_pkt(MAC0_DEV, 0, rx, NULL, MSS_MAC_INT_ENABLE);
        }

        MSS_MAC_delay(MAC0_DEV, 1000);
        poll_count++;

        if (expected_block == 1 && poll_count >= 1000) {
            poll_count = 0;
            if (rrq_retries > 0) {
                mHSS_PRINTF("[NETBOOT] Retrying TFTP Read Request (%u attempts left)...\n", rrq_retries);
                MSS_MAC_send_pkt(MAC0_DEV, 0, tx, rrq_frame_len, NULL);
                rrq_retries--;
            } else {
                mHSS_PRINTF("\n[NETBOOT] ERROR: TFTP server not responding.\n");
                NETBOOT_Service_Deinit();
                return NETBOOT_STATUS_TFTP_ERROR;
            }
        }
    }
}