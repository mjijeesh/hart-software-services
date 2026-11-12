# Hart Software Services (HSS)

The Microchip Hart Software Services (HSS) serves as the zero-stage bootloader and system controller for PolarFire® SoC FPGAs. It executes on the E51 monitor core to configure clocks, train DDR memory, release application U54 harts, and load boot payloads (U-Boot, Linux, or bare-metal binaries).

On PolarFire SoC, HSS is comprised of two portions:
1. A superloop monitor running on the E51 minion processor, which receives requests from individual U54 application processors to perform services on their behalf.
2. A Machine-Mode software interrupt trap handler, which allows the E51 to send messages to the U54s and coordinate cross-hart events.

---

## 1. Latest Updates & Architectural Enhancements

* **L2 Cache-to-DMA Coherency (`init/hss_boot_init.c`):**
  * Centralized hardware cache flushing inside `HSS_Storage_WriteBlock()`. 
  * Automatically flushes dirty L2 cache lines from `SEG0_0` (`0x80000000`) and `SEG0_1` to physical DDR before initiating ADMA/SDMA peripheral writes. This ensures payloads downloaded via TFTP (`NETBOOT`) or UART (`SERIALBOOT`) are written intact to eMMC/SD Cards without stale memory corruption.
* **Transport-Agnostic Payload Decoder (`services/boot/hss_boot_service.c`):**
  * Extracted `HSS_Boot_DecodeAndBootDDR()` into the core boot service so `NETBOOT` and `SERIALBOOT` share a single payload loader with zero cross-service dependency.
  * Automatically detects structured HSS Boot Images (`0xb007c0de` and compressed `0xc08b8355`) versus raw flat binaries across both Ethernet TFTP and UART SFL transfers.
  * Safely stages/decompresses container headers to `0x1010000000` (256 MiB into DDR-Hi) before unpacking segments into `0x80000000`, preventing chunk-table overwrites while remaining within physical RAM.
* **Board-Abstracted SFL UART Framing (`bsp_serialboot_uart_tx`):**
  * Added a weak BSP hook in `services/serialboot/` with board-specific overrides in `boards/<board>/hss_uart_init.c` (`MMUART0` on Icicle Kit, `MMUART1` on Discovery Kit).
  * Transmits raw SFL magic frames (`sL5DdSMmkekro\n`) and single-byte ACKs without `\r\n` newline expansion.
* **Unified Multi-Board SPI Flash Driver (`services/spi/spi_flash.c`):**
  * Supports FPGA fabric **CoreSPI** (Icicle Kit, Micron MT25Q) and hardened **MSS SPI** (Discovery Kit, Microchip SST25WF) under `CONFIG_SERVICE_SPI_FLASH`, adding `FLASHID` and `FLASHBOOT` CLI commands.
* **Detailed TinyCLI Help Output (`services/tinycli/tinycli_api.c`):**
  * Updated `display_help_()` to format top-level and subcommand help tables with aligned command names and full usage descriptions.

---

## 2. Automated Boot Cascade Architecture

When HSS boots without a manual CLI override, `HSS_BootInit()` evaluates boot media sequentially and falls back to Ethernet TFTP staging if all local non-volatile media fail to provide a valid payload:

```text
[Power-On / Cold Reset]
         │
         ▼
┌─────────────────────────────────────────┐
│ 1. Local MMC (SD Card / eMMC)           │
│    Checks GPT boot partition or LBA 0   │──(Valid 0xb007c0de)──► Stage to DDR & Boot U54s
└────────┬────────────────────────────────┘
         │ (No media / Invalid magic)
         ▼
┌─────────────────────────────────────────┐
│ 2. Onboard SPI Flash (+0x400)           │
│    Reads via System Controller SPI      │──(Valid 0xb007c0de)──► Stage to DDR & Boot U54s
└────────┬────────────────────────────────┘
         │ (Erased / Invalid magic)
         ▼
┌─────────────────────────────────────────┐
│ 3. Automatic Netboot Fallback (TFTP)    │
│    DHCP Lease ─► ARP ─► TFTP Download   │──(Transfer OK)───────► Decode & Boot U54s
└─────────────────────────────────────────┘
```

**Note:** HSS prioritizes MMC/SD over SPI Flash, ensuring updates flashed to removable media take precedence over factory images in SPI flash.

## **3\. Boot & Flash via SerialBoot (UART SFL)**

The SERIALBOOT service downloads payloads over the console UART using the Serial Flash Loader (SFL) protocol (mpfs-terminal.py). Payloads can be decoded and executed directly in DDR or permanently flashed to eMMC, SD Cards, QSPI, or SPI Flash.

### **SerialBoot & SPI Flash Command Reference**

| Command | Target Media | Description |
| :---- | :---- | :---- |
| serialboot \[addr\] | DDR RAM | Receives binary via UART into DDR (default 0x80000000), decodes payload, and boots U54 cores. |
| serialboot emmc | Onboard eMMC | Receives payload to DDR, writes to the eMMC GPT boot partition (or LBA 0), and verifies. |
| serialboot sd | microSD Card | Receives payload to DDR, writes to the SD Card GPT boot partition (or LBA 0), and verifies. |
| serialboot spi \[offset\] | SPI Flash | Receives payload to DDR, preserves 0x000–0x3FF prefix, erases, programs at 0x400, and verifies. |
| serialboot qspi | QSPI Flash | Receives payload and flashes to MSS QSPI flash (for Discovery Kit / supported boards). |
| flashid | SPI Flash | Reads and prints the JEDEC ID of the connected SPI Flash (CoreSPI or MSS SPI). |
| flashboot \[offset\] | SPI Flash | Reads an HSS payload image from SPI Flash (default 0x400) into DDR, decodes, and boots U54s. |

### **Usage with mpfs-terminal.py**

Bash

```
# Load and decode/boot payload in DDR (type 'serialboot' at HSS prompt)
python3 mpfs-terminal.py /dev/ttyUSB0 --kernel payload.bin

# Flash payload to SPI Flash at offset 0x400 (type 'serialboot' or 'serialboot spi')
python3 mpfs-terminal.py /dev/ttyUSB0 --kernel payload.bin --flash --kernel-adr 0x400
```

## **4\. Boot & Flash via Netboot (Ethernet / TFTP)**

The NETBOOT service downloads payloads over Ethernet via DHCP and TFTP and passes downloaded images through HSS\_Boot\_DecodeAndBootDDR(), supporting both hss-payload-generator images and raw flat binaries.

| Command | Description |
| :---- | :---- |
| netboot \[ip\] \[file\] \[addr\] | Downloads TFTP payload to DDR (0x80000000), decodes container if present, and boots U54 cores. |
| netboot emmc \[ip\] \[file\] | Downloads payload via TFTP, pushes cached data to physical RAM, and flashes to onboard eMMC. |
| netboot sd \[ip\] \[file\] | Downloads payload via TFTP, pushes cached data to physical RAM, and flashes to microSD Card. |
| netboot spi \[ip\] \[file\] \[offset\] | Downloads payload via TFTP and flashes to SPI Flash (default offset 0x400). |
| netboot qspi \[ip\] \[file\] | Downloads payload via TFTP and flashes to MSS QSPI Flash. |

*(All arguments in brackets are optional; omitted parameters default to DHCP auto-discovery, boot.bin, DDR 0x80000000, and SPI offset 0x400.)*

### **Expected Output: netboot sd**

Plaintext

```
>> netboot sd
[CLI] Netboot -> SD Card programming initiated...
================ [NETBOOT: HARDWARE INIT] ================
...
[NETBOOT] Requesting file 'boot.bin' via TFTP...
[NETBOOT] Transfer complete! Total: 36864 bytes
[CLI] Downloaded 36864 bytes to 0x80000000. Programming storage...
[FLASH] Selecting SD Card interface...
[FLASH] Writing 36864 bytes (aligned: 36864) to SD Card offset 0x0...
[FLASH] SUCCESS: Payload verified on SD Card! Reset board to boot.
```

## **5\. Detailed TinyCLI Help Output**

Running help (or entering an incomplete subcommand such as serialboot help or netboot help) prints an aligned command table with descriptions:

Plaintext

```
>> help
Supported commands:
  YMODEM         - Run YMODEM utility to download an image to DDR.
  BOOT           - Quit TinyCLI and return to regular boot process.
  RESET          - Reset PolarFire SoC.
  HELP           - Display command summary / command help information.
  ...
  SERIALBOOT     - Receive binary over SFL and run/flash: SERIALBOOT [sd|emmc|spi|qspi] [addr/offset]
  FLASHID        - Read SPI Flash JEDEC ID: FLASHID
  FLASHBOOT      - Boot image from SPI Flash: FLASHBOOT <spi_offset>
  NETBOOT        - Download and run/flash payload via TFTP: NETBOOT [sd|emmc|spi|qspi] [server_ip] [filename] [addr/offset]
```

## **6\. Payload Preparation Requirements**

All structured boot operations (flashing to non-volatile storage or multi-hart ELF segment loading) require an image packaged with hss-payload-generator. Raw flat binaries can only be executed directly from DDR.

YAML

```
# baremetal.yaml
set-name: 'PolarFire-SoC-Payload'
hart-entry-points: {u54_1: '0x80000000'}
payloads:
  app.elf: {exec-addr: '0x80000000', owner-hart: u54_1, priv-mode: prv-m}
```

Generate payload.bin:

Bash

```
./tools/hss-payload-generator/hss-payload-generator -c baremetal.yaml payload.bin
```

## **7\. Hardware Configuration Differences & Build Instructions**

| Feature | PolarFire SoC Icicle Kit | PolarFire SoC Discovery Kit |
| :---- | :---- | :---- |
| **HSS Target** | mpfs-icicle-kit / mpfs-icicle-kit-es | mpfs-disco-kit |
| **DDR Subsystem** | 2 GB LPDDR4 (32-bit data bus) | 1 GB DDR4 (DDR-Lo 32 MiB \+ DDR-Hi 888 MiB) |
| **DDR Target Addr** | 0x103fc00000 | **0x1020000000** (Must fit in 888 MiB bounds) |
| **Console UART** | MMUART0 (g\_mss\_uart0\_lo) | MMUART1 (g\_mss\_uart1\_lo) |
| **Primary Storage** | Onboard eMMC & MicroSD | MicroSD only (no onboard eMMC) |
| **SPI Flash Path** | CoreSPI (FPGA Fabric, Micron MT25Q) | MSS SPI (Hardened Peripheral, Microchip SST25WF) |
| **Ethernet PHY** | VSC8662 (Dual-Port / Cross-MAC MDIO) | VSC8221 (Single Port, GEM0) |
| **Embedded DTB** | Supported (CONFIG\_PROVIDE\_DTB=y) | Disabled (\# CONFIG\_PROVIDE\_DTB is not set) |

### **Toolchain Setup**

Bash

```
export PATH=/opt/xpack-riscv-none-elf-gcc-15.2.0-1/bin:\$PATH
export FPGENPROG=/opt/microchip/Libero_SoC_v2024.1/Libero/bin64/fpgenprog
```

### **Compiling & Programming eNVM**

Bash

```
cd hart-software-services

# 1. Load board default configuration (always run defconfig when switching boards)
make BOARD=mpfs-icicle-kit defconfig   # or BOARD=mpfs-disco-kit

# 2. (Optional) Enable SerialBoot, Netboot, and SPI Flash drivers via menuconfig
make BOARD=mpfs-icicle-kit menuconfig

# 3. Clean, build, and program target eNVM
make -j\$(nproc) BOARD=mpfs-icicle-kit clean all program
```

**Tip when switching boards:** Save board-specific .config files (e.g., boards/mpfs-disco-kit/def\_config\_serial\_netboot) so switching targets preserves board-specific options such as CONFIG\_PROVIDE\_DTB, target staging addresses, and CONFIG\_SPI\_FLASH\_MSS\_SPI vs. CONFIG\_SPI\_FLASH\_CORESPI.

## **8\. Contributions & Source Layout**

Pull requests must include a per-commit sign-off stating agreement to the [Developer Certificate of Origin](https://developercertificate.org/).

* application/ \- Main functions, U54 IPI handlers, and CRT startup  
* boards/ \- Board-specific initializations (board\_eth.c, board\_spi.c, hss\_uart\_init.c) and Libero XML mappings  
* envm-wrapper/ \- Decompressor routines to inflate HSS to L2-Scratchpad  
* modules/ \- Cryptography, compression, and Secure Software Message Bus (SSMB)  
* services/ \- HSS state machines (boot, spi, mmc, qspi, netboot, serialboot, tinycli)  
* thirdparty/ \- OpenSBI, Miniz, Libecc, Kconfiglib

