#ifndef MSS_ETHERNET_MAC_SW_CFG_H_
#define MSS_ETHERNET_MAC_SW_CFG_H_

/* Inline assembly mapping for C11 mode */
#ifndef asm
#define asm __asm__
#endif

/* Target platform is always PolarFire SoC (G5 SOC) */
#ifndef TARGET_G5_SOC
#define TARGET_G5_SOC                   1
#endif

#define MSS_MAC_64_BIT_ADDRESS_MODE     1
#define MSS_MAC_SIMPLE_TX_QUEUE         1
#define MSS_MAC_TIME_STAMPED_MODE       0

/* Driver metadata */
#define CORE_VENDOR                     "Microsemi"
#define CORE_LIBRARY                    "Firmware"
#define CORE_NAME                       "PolarFire_SoC_MSS_Ethernet_MAC_Driver"
#define CORE_VERSION                    "1.8.104"

/* Must be 4 on PolarFire SoC to prevent out-of-bounds pointer faults in MSS_MAC_init */
#define MSS_MAC_QUEUE_COUNT             (4)
#define MSS_MAC_RX_RING_SIZE            (4U)
#define MSS_MAC_TX_RING_SIZE            (4U)

/* Supported PHY interface types */
#define NULL_PHY                        (0x0001U)
#define GMII                            (0x0002U)
#define TBI                             (0x0004U)
#define GMII_SGMII                      (0x0008U)
#define SGMII                           (0x0080U)

/* PolarFire SoC uses TBI/SGMII interface for GEM0 */
#define MSS_MAC_PHY_INTERFACE           TBI

/* Supported PHY model IDs */
#define MSS_MAC_DEV_PHY_NULL            (0x0001U)
#define MSS_MAC_DEV_PHY_VSC8662         (0x0020U) /* Icicle Kit */
#define MSS_MAC_DEV_PHY_VSC8221         (0x0080U) /* Discovery Kit */

/* Dummy IDs to satisfy dependent headers */
#define MSS_MAC_DEV_PHY_VSC8575         (0x0002U)
#define MSS_MAC_DEV_PHY_VSC8541         (0x0004U)
#define MSS_MAC_DEV_PHY_DP83867         (0x0008U)
#define MSS_MAC_DEV_PHY_VSC8575_LITE    (0x0010U)
#define MSS_MAC_DEV_PHY_RTL8211         (0x0040U)

/* Supported boards: Discovery Kit & Icicle Kit */
#define MSS_MAC_DESIGN_ICICLE_STD_GEM0  (22)
#define MSS_MAC_DESIGN_DISCOVERY_GEM0   (28)

#if defined(TARGET_ICICLE_KIT)
#define MSS_MAC_HW_PLATFORM             MSS_MAC_DESIGN_ICICLE_STD_GEM0
#else
#define MSS_MAC_HW_PLATFORM             MSS_MAC_DESIGN_DISCOVERY_GEM0
#endif

/* Include Discovery Kit (VSC8221), Icicle Kit (VSC8662), and NULL fallback */
#define MSS_MAC_PHYS                    (MSS_MAC_DEV_PHY_NULL | MSS_MAC_DEV_PHY_VSC8221 | MSS_MAC_DEV_PHY_VSC8662)

#define MSS_MAC_USE_PHY_NULL            1
#define MSS_MAC_USE_PHY_VSC8221         1
#define MSS_MAC_USE_PHY_VSC8662         1

#define MSS_MAC_USE_PHY_VSC8541         0
#define MSS_MAC_USE_PHY_VSC8575         0
#define MSS_MAC_USE_PHY_VSC8575_LITE    0
#define MSS_MAC_USE_PHY_DP83867         0
#define MSS_MAC_USE_PHY_RTL8211         0

/* Memory regions & screener register limits */
#define MSS_MAC_MEM_DDR                 (0)
#define MSS_MAC_MEM_FIC0                (1)
#define MSS_MAC_MEM_FIC1                (2)
#define MSS_MAC_MEM_CRYPTO              (3)

#define MSS_MAC_TYPE_1_SCREENERS        (4U)
#define MSS_MAC_TYPE_2_SCREENERS        (4U)
#define MSS_MAC_TYPE_2_ETHERTYPES       (4U)
#define MSS_MAC_TYPE_2_COMPARERS        (12U)
#define MSS_MAC_EMAC_TYPE_2_SCREENERS   (2U)
#define MSS_MAC_EMAC_TYPE_2_COMPARERS   (6U)

#define MICROSEMI__FIRMWARE__POLARFIRE_SOC_MSS_ETHERNET_MAC_DRIVER__1_8_104_CONFIGURATION_HEADER
#define MSS_ETHERNET_MAC_USER_CONFIG_H

#endif /* MSS_ETHERNET_MAC_SW_CFG_H_ */