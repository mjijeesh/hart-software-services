# HSS Release Notes — SerialBoot, SPI Flash, YMODEM & Netboot

## Verified Targets
- Microchip PolarFire SoC Icicle Kit (`BOARD=mpfs-icicle-kit`, CoreSPI / board BSP)
- Microchip PolarFire SoC Discovery Kit (`BOARD=mpfs-disco-kit`, MSS SPI)

## Features & Architecture
- **Service Separation:** System Controller SPI boot remains in `services/spi/` (`CONFIG_SERVICE_SPI`), while external SPI NOR flash driver, storage provider, and CLI live in `services/spiflash/` (`CONFIG_SERVICE_SPI_FLASH`).
- **SPI Flash (`SPIFLASH`):** JEDEC ID detection, sector erase/program/read, payload flashing, and boot support.
- **SerialBoot (`SERIALBOOT`):** Binary transfer over SFL with run and storage flashing support.
- **YMODEM (`YMODEM`):** Image transfer with SPI flash, SD card, and eMMC support.
- **Netboot (`NETBOOT`):** TFTP/DHCP image fetch with DDR boot and SD, SPI flash, and eMMC flashing support.