# Hart Software Services (HSS)

The Microchip Hart Software Services (HSS) serves as the zero-stage bootloader and system controller for PolarFire® SoC FPGAs. It executes on the E51 monitor core to configure clocks, train DDR memory, release application U54 harts, and load boot payloads (U-Boot, Linux, or bare-metal binaries).

On PolarFire SoC, HSS is comprised of two portions:
1. A superloop monitor running on the E51 minion processor, which receives requests from individual U54 application processors to perform services on their behalf.
2. A Machine-Mode software interrupt trap handler, which allows the E51 to send messages to the U54s and coordinate cross-hart events.

---

## 1. Latest Updates & Architectural Enhancements

* **Transport-Agnostic Payload Decoder (`services/boot/hss_boot_service.c`):**
  * Extracted `HSS_Boot_DecodeAndBootDDR()` into the core boot service so `NETBOOT` and `SERIALBOOT` share a single payload loader with zero cross-service dependency.
  * Automatically detects structured HSS Boot Images (`0xb007c0de` and compressed `0xc08b8355`) versus raw flat binaries across both Ethernet TFTP and UART SFL transfers.
  * Stages/decompresses container headers to `0x1010000000` (256 MiB into DDR-Hi) before unpacking segments into `0x80000000`, preventing chunk-table overwrites while remaining within physical RAM on both the 1 GiB Discovery Kit (`0x1000000000`–`0x1037800000`) and 2 GiB Icicle Kit.
* **Board-Abstracted SFL UART Framing (`bsp_serialboot_uart_tx`):**
  * Added a weak BSP hook in `services/serialboot/` with board-specific overrides in `boards/<board>/hss_uart_init.c` (`MMUART0` on Icicle Kit, `MMUART1` on Discovery Kit).
  * Transmits raw SFL magic frames (`sL5DdSMmkekro\n`) and single-byte ACKs (`'K'`, `'C'`, `'E'`) without `\r\n` newline expansion.
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

> **Note:** HSS prioritizes MMC/SD over SPI Flash, ensuring updates flashed to removable media take precedence over factory images in SPI flash.

---

## 3. Boot & Flash via SerialBoot (UART SFL)

The `SERIALBOOT` service downloads payloads over the console UART using the Serial Flash Loader (SFL) protocol (`mpfs-terminal.py`). Payloads can be decoded and executed directly in DDR or permanently flashed to eMMC, SD Cards, QSPI, or SPI Flash.

### SerialBoot & SPI Flash Command Reference

| Command | Target Media | Description |
| :--- | :--- | :--- |
| `serialboot [addr]` | DDR RAM | Receives binary via UART into DDR (default `0x80000000`), decodes payload, and boots U54 cores. |
| `serialboot emmc` | Onboard eMMC | Receives payload to DDR, writes to the eMMC GPT boot partition (or LBA 0), and verifies. |
| `serialboot sd` | microSD Card | Receives payload to DDR, writes to the SD Card GPT boot partition (or LBA 0), and verifies. |
| `serialboot spi [offset]` | SPI Flash | Receives payload to DDR, preserves `0x000`–`0x3FF` prefix, erases, programs at `0x400`, and verifies. |
| `serialboot qspi` | QSPI Flash | Receives payload and flashes to MSS QSPI flash (for Discovery Kit / supported boards). |
| `flashid` | SPI Flash | Reads and prints the JEDEC ID of the connected SPI Flash (`CoreSPI` or `MSS SPI`). |
| `flashboot [offset]` | SPI Flash | Reads an HSS payload image from SPI Flash (default `0x400`) into DDR, decodes, and boots U54s. |

### Usage with `mpfs-terminal.py`

```bash
# Load and decode/boot payload in DDR (type 'serialboot' at HSS prompt)
python3 mpfs-terminal.py /dev/ttyUSB0 --kernel payload.bin

# Flash payload to SPI Flash at offset 0x400 (type 'serialboot' or 'serialboot spi')
python3 mpfs-terminal.py /dev/ttyUSB0 --kernel payload.bin --flash --kernel-adr 0x400
```

### Expected Output: Discovery Kit SPI Probe & DDR Boot

```text
>> flashid

[SPI-FLASH] JEDEC ID: MFR=0x62, DEV=0x06, CAP=0x13 (Microchip SST25WF)
>> serialboot

[SERIALBOOT] Staging buffer @ 0x0080000000. Waiting for SFL host...
[MPFS-TERM] Magic trigger matched! Replying with handshake ACK...
[MPFS-TERM] Uploading payload.bin (32576 bytes) -> 0x80000000...
|====================| 100.0% (32576/32576 B)

[SERIALBOOT] Transfer complete (32576 bytes received, host base: 0x80000000).
[PAYLOAD] Detected HSS Boot Image header (magic: 0xb007c0de).
[PAYLOAD] Set Name : "PolarFire-SoC-HSS::pfsoc_bios"
[PAYLOAD] Image Len: 32576 bytes
[PAYLOAD]   Chunk[0]: Hart=1  Load=0x0080000000  Exec=0x0080000000  Size=30944 bytes
[PAYLOAD] Registering boot image and dispatching U54 harts...
```

### Expected Output: Flashing to SPI (`serialboot spi`)

```text
>> serialboot spi

[SERIALBOOT] Staging buffer @ 0x0080000000. Waiting for SFL host...
[MPFS-TERM] Magic trigger matched! Replying with handshake ACK...
[MPFS-TERM] Uploading payload.bin (32576 bytes) -> 0x00000400...
|====================| 100.0% (32576/32576 B)

[SERIALBOOT] Transfer complete (32576 bytes received, host base: 0x400).
[SPI-FLASH] Detected Micron MT25Q (MFR: 0x20, DEV: 0xBB21)
[SPI-FLASH] Backing up 1024 prefix bytes at 0x0...
[SPI-FLASH] Erasing 64 KiB sectors covering 0x400 (32576 bytes)...
[SPI-FLASH] Programming 32576 bytes to 0x400...
[SPI-FLASH] SUCCESS: Payload verified in SPI Flash @ 0x400! Reset board to boot.
```

---

## 4. Boot & Flash via Netboot (Ethernet / TFTP)

The `NETBOOT` service downloads payloads over Ethernet via DHCP and TFTP and passes downloaded images through `HSS_Boot_DecodeAndBootDDR()`, supporting both `hss-payload-generator` images and raw flat binaries.

| Command | Description |
| :--- | :--- |
| `netboot [ip] [file] [addr]` | Downloads TFTP payload to DDR (`0x80000000`), decodes container if present, and boots U54 cores. |
| `netboot emmc [ip] [file]` | Downloads payload via TFTP and flashes to onboard eMMC. |
| `netboot sd [ip] [file]` | Downloads payload via TFTP and flashes to microSD Card. |
| `netboot spi [ip] [file] [offset]` | Downloads payload via TFTP and flashes to SPI Flash (default offset `0x400`). |
| `netboot qspi [ip] [file]` | Downloads payload via TFTP and flashes to MSS QSPI Flash. |

*(All arguments in brackets are optional; omitted parameters default to DHCP auto-discovery, `boot.bin`, DDR `0x80000000`, and SPI offset `0x400`.)*

### Expected Output: `netboot`

```text
>> netboot

[CLI] Initiating Netboot service...
================ [NETBOOT: HARDWARE INIT] ================
[NETBOOT] [1/5] Cycling GEM peripheral reset and clock...
[NETBOOT] [2/5] Configuring GEM MAC Core...
[NETBOOT]       Station MAC Address : 00:04:A3:11:22:34
[NETBOOT] [5/5] Waiting for Copper Auto-Negotiation...
[NETBOOT]       Link State          : UP (1000 Mbps Full Duplex)
================ [NETBOOT: DHCP NEGOTIATION] ================
[DHCP] DHCP Offer Successfully Received!
[DHCP]   |- Assigned Board IP : 192.168.20.76
[DHCP]   \- Gateway IP        : 192.168.20.1
=============================================================
[NETBOOT] Target TFTP Server : 192.168.20.94
[NETBOOT] Hardware MAC Resolved: 24:41:8C:F1:9B:45
[NETBOOT] Requesting file 'boot.bin' via TFTP...
[NETBOOT] Transfer complete! Total: 36864 bytes
[CLI] Transfer complete (36864 bytes). Decoding/booting from 0x0000000080000000...
[PAYLOAD] Detected HSS Boot Image header (magic: 0xb007c0de).
[PAYLOAD] Registering boot image and dispatching U54 harts...
```

---

## 5. Detailed TinyCLI Help Output

Running `help` (or entering an incomplete subcommand such as `serialboot help` or `netboot help`) prints an aligned command table with descriptions:

```text
>> help
Supported commands:
  YMODEM         - Run YMODEM utility to download an image to DDR.
  BOOT           - Quit TinyCLI and return to regular boot process.
  RESET          - Reset PolarFire SoC.
  HELP           - Display command summary / command help information.
  VERSION        - Display system version information.
  UPTIME         - Display uptime information.
  DEBUG          - Display debug information.
  MEMTEST        - Full DDR memory test.
  ...
  SERIALBOOT     - Receive binary over SFL and run/flash: SERIALBOOT [sd|emmc|spi|qspi] [addr/offset]
  FLASHID        - Read SPI Flash JEDEC ID: FLASHID
  FLASHBOOT      - Boot image from SPI Flash: FLASHBOOT <spi_offset>
  NETBOOT        - Download and run/flash payload via TFTP: NETBOOT [sd|emmc|spi|qspi] [server_ip] [filename] [addr/offset]
```

---

## 6. Payload Preparation Requirements

All structured boot operations (flashing to non-volatile storage or multi-hart ELF segment loading) require an image packaged with `hss-payload-generator`. Raw flat binaries can only be executed directly from DDR.

```yaml
# baremetal.yaml
set-name: 'PolarFire-SoC-Payload'
hart-entry-points: {u54_1: '0x80000000'}
payloads:
  app.elf: {exec-addr: '0x80000000', owner-hart: u54_1, priv-mode: prv-m}
```

Generate `payload.bin`:

```bash
./tools/hss-payload-generator/hss-payload-generator -c baremetal.yaml payload.bin
```

---

## 7. Hardware Configuration Differences & Build Instructions

| Feature | PolarFire SoC Icicle Kit | PolarFire SoC Discovery Kit |
| :--- | :--- | :--- |
| **HSS Target** | `mpfs-icicle-kit` / `mpfs-icicle-kit-es` | `mpfs-disco-kit` |
| **DDR Subsystem** | 2 GB LPDDR4 (32-bit data bus) | 1 GB DDR4 (`DDR-Lo` 32 MiB + `DDR-Hi` 888 MiB) |
| **Console UART** | `MMUART0` (`g_mss_uart0_lo`) | `MMUART1` (`g_mss_uart1_lo`) |
| **Primary Storage** | Onboard eMMC & MicroSD | MicroSD only (no onboard eMMC) |
| **SPI Flash Path** | CoreSPI (FPGA Fabric, Micron MT25Q) | MSS SPI (Hardened Peripheral, Microchip SST25WF) |
| **Ethernet PHY** | VSC8662 (Dual-Port / Cross-MAC MDIO) | VSC8221 (Single Port, GEM0) |
| **Embedded DTB** | Supported (`CONFIG_PROVIDE_DTB=y`) | Disabled (`# CONFIG_PROVIDE_DTB is not set`) |

### Toolchain Setup

```bash
export PATH=/opt/xpack-riscv-none-elf-gcc-15.2.0-1/bin:$PATH
export FPGENPROG=/opt/microchip/Libero_SoC_v2024.1/Libero/bin64/fpgenprog
```

### Compiling & Programming eNVM

```bash
cd hart-software-services

# 1. Load board default configuration (always run defconfig when switching boards)
make BOARD=mpfs-icicle-kit defconfig   # or BOARD=mpfs-disco-kit

# 2. (Optional) Enable SerialBoot, Netboot, and SPI Flash drivers via menuconfig
make BOARD=mpfs-icicle-kit menuconfig

# 3. Clean, build, and program target eNVM
make -j$(nproc) BOARD=mpfs-icicle-kit clean all program
```

> **Tip when switching boards:** Save board-specific `.config` files (e.g., `boards/mpfs-disco-kit/def_config_serial_netboot`) so switching targets preserves board-specific options such as `CONFIG_PROVIDE_DTB` and `CONFIG_SPI_FLASH_MSS_SPI` vs. `CONFIG_SPI_FLASH_CORESPI`.

---

## 8. Contributions & Source Layout

Pull requests must include a per-commit sign-off stating agreement to the [Developer Certificate of Origin](https://developercertificate.org/).

* `application/` - Main functions, U54 IPI handlers, and CRT startup
* `boards/` - Board-specific initializations (`board_eth.c`, `board_spi.c`, `hss_uart_init.c`) and Libero XML mappings
* `envm-wrapper/` - Decompressor routines to inflate HSS to L2-Scratchpad
* `modules/` - Cryptography, compression, and Secure Software Message Bus (SSMB)
* `services/` - HSS state machines (`boot`, `spi`, `mmc`, `qspi`, `netboot`, `serialboot`, `tinycli`)
* `thirdparty/` - OpenSBI, Miniz, Libecc, Kconfiglib

# **Hart Software Services (HSS)**

The Microchip Hart Software Services (HSS) serves as the zero-stage bootloader and system controller for PolarFire® SoC FPGAs. It executes on the E51 monitor core to configure clocks, train DDR memory, release application U54 harts, and load boot payloads (U-Boot, Linux, or bare-metal binaries). 

On PolarFire SoC, this is comprised of two portions:

1\. A superloop monitor running on the E51 minion processor, which receives requests from the individual U54 application processors to perform services on their behalf.

2\. A Machine-Mode software interrupt trap handler, which allows the E51 to send messages to the U54s and coordinate cross-hart events.

## 

## **1\. Automated Boot Cascade Architecture**

When HSS boots without a manual CLI override, \`HSS\_BootInit()\` evaluates boot media sequentially. It falls back to Ethernet TFTP staging if all local non-volatile media fail to provide a valid payload:

```
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
│    DHCP Lease ─► ARP ─► TFTP Download   │──(Transfer OK)───────► Stage to DDR & Boot U54s
└─────────────────────────────────────────┘
```

**Note:** HSS prioritizes MMC/SD over SPI Flash, ensuring updates flashed to removable media take precedence over factory images in SPI flash.

## **2\. Boot & Flash via SerialBoot (UART SFL)**

The SERIALBOOT service allows developers to download payloads over the console UART using the Serial Flash Loader (SFL) protocol. Using the mpfs-terminal.py host script, payloads can be executed directly in DDR or permanently flashed to eMMC, SD Cards, or SPI Flash.

### **SerialBoot Command Reference**

From the TinyCLI prompt (\>\>), use the following commands:

| Command | Target Media | Description |
| :---- | :---- | :---- |
| serialboot \[addr\] | DDR RAM | Receives binary via UART into DDR (default 0x80000000), decodes payload, and boots U54 cores. |
| serialboot emmc | Onboard eMMC | Receives payload to DDR, writes to the eMMC GPT boot partition (or LBA 0), and verifies. |
| serialboot sd | microSD Card | Receives payload to DDR, writes to the SD Card GPT boot partition, and verifies. |
| serialboot spi \[offset\] | SPI Flash | Receives payload to DDR, preserves 0x000–0x3FF prefix, erases, programs at 0x400, and verifies. |
| serialboot qspi | QSPI Flash | Receives payload and flashes to MSS QSPI flash (for Discovery Kit / supported boards). |
| flashid | SPI Flash | Diagnostics: Reads and prints the JEDEC ID of the connected SPI Flash. |

### **Usage with mpfs-terminal.py**

Start the host script on your development machine. The script will wait for the board to request the file:

Bash

```
# To load and boot from DDR (Plain 'serialboot' command)
python3 mpfs-terminal.py /dev/ttyUSB0 --kernel payload.bin

# To flash to SPI Flash (Use 'serialboot spi' or trigger auto-flash with flags)
python3 mpfs-terminal.py /dev/ttyUSB0 --kernel payload.bin --flash --kernel-adr 0x400
```

### **Expected Output: Flashing to SPI (serialboot spi)**

Plaintext

```
>> serialboot spi

[SERIALBOOT] Staging buffer @ 0x0080000000. Waiting for SFL host...
[MPFS-TERM] Magic trigger matched! Replying with handshake ACK...
[MPFS-TERM] Uploading payload.bin (32576 bytes) -> 0x00020000...
|====================| 100.0% (32576/32576 B)
[MPFS-TERM] Transfer Complete (9.8 KB/s).
[MPFS-TERM] Terminal active. Continuing console output...

[SERIALBOOT] Transfer complete (32576 bytes received, host base: 0x0).
[SPI-FLASH] Detected Micron MT25Q (MFR: 0x20, DEV: 0xBB21)
[SPI-FLASH] Backing up 1024 prefix bytes at 0x0...
[SPI-FLASH] Erasing 64 KiB sectors covering 0x400 (32576 bytes)...
[SPI-FLASH] Programming 32576 bytes to 0x400...
[SPI-FLASH] SUCCESS: Payload verified in SPI Flash @ 0x400! Reset board to boot.
```

### **Expected Output: Booting to DDR (serialboot)**

Plaintext

```
>> serialboot

[SERIALBOOT] Staging buffer @ 0x0080000000. Waiting for SFL host...
[MPFS-TERM] Magic trigger matched! Replying with handshake ACK...
[MPFS-TERM] Uploading payload.bin (32576 bytes) -> 0x80000000...
|====================| 100.0% (32576/32576 B)

[SERIALBOOT] Transfer complete (32576 bytes received).
[PAYLOAD] Detected HSS Boot Image header (magic: 0xb007c0de).
[PAYLOAD] Set Name : "PolarFire-SoC-HSS::pfsoc_bios"
[PAYLOAD] Image Len: 32576 bytes
[PAYLOAD]   Chunk[0]: Hart=1  Load=0x0080000000  Exec=0x0080000000  Size=30944 bytes
[PAYLOAD] Registering boot image and dispatching U54 harts...
[SERIALBOOT] Synchronizing caches and dispatching all U54 harts...
[SERIALBOOT] All U54 harts launched. Application active on UART1.
```

## **3\. Boot & Flash via Netboot (Ethernet/TFTP)**

The NETBOOT service allows developers to download payloads dynamically over a network via DHCP and TFTP.

| Command | Description |
| :---- | :---- |
| netboot \[ip\] \[file\] \[addr\] | Downloads TFTP payload to DDR (0x80000000) and boots U54 cores. |
| netboot emmc \[ip\] \[file\] | Downloads payload via TFTP and flashes to onboard eMMC. |
| netboot sd \[ip\] \[file\] | Downloads payload via TFTP and flashes to microSD Card. |
| netboot spi \[ip\] \[file\] | Downloads payload via TFTP and flashes to CoreSPI Flash. |

*(All arguments in brackets are optional; omitted parameters default to DHCP auto-discovery, boot.bin, and DDR 0x80000000.)*

### **Expected Output: netboot**

Plaintext

```
>> netboot

[CLI] Initiating Netboot service...
================ [NETBOOT: HARDWARE INIT] ================
[NETBOOT] [1/5] Cycling GEM peripheral reset and clock...
[NETBOOT] [2/5] Configuring GEM MAC Core...
[NETBOOT]       Station MAC Address : 00:04:A3:11:22:34
[NETBOOT] [5/5] Waiting for Copper Auto-Negotiation...
[NETBOOT]       Link State          : UP (1000 Mbps Full Duplex)
================ [NETBOOT: DHCP NEGOTIATION] ================
[DHCP] DHCP Offer Successfully Received!
[DHCP]   |- Assigned Board IP : 192.168.20.76
[DHCP]   \- Gateway IP        : 192.168.20.1
=============================================================
[NETBOOT] Target TFTP Server : 192.168.20.94
[NETBOOT] Hardware MAC Resolved: 24:41:8C:F1:9B:45
[NETBOOT] Requesting file 'boot.bin' via TFTP...
[NETBOOT] Transfer complete! Total: 36864 bytes
[CLI] Transfer complete (36864 bytes). Dispatching harts to 0x0000000080000000...
```

## **4\. Payload Preparation Requirements**

All structured boot operations (flashing to storage or decoding multiple ELF segments) require an image packaged with hss-payload-generator. Raw flat binaries can only be booted directly from DDR.

Generate a payload image (payload.bin) matching your configuration:

YAML

```
# baremetal.yaml
set-name: 'PolarFire-SoC-Payload'
hart-entry-points: {u54_1: '0x80000000'}
payloads:
  app.elf: {exec-addr: '0x80000000', owner-hart: u54_1, priv-mode: prv-m}
```

Build the payload:

Bash

```
./tools/hss-payload-generator/hss-payload-generator -c baremetal.yaml payload.bin
```

## **5\. Hardware Configuration Differences**

| Feature | PolarFire SoC Icicle Kit | PolarFire SoC Discovery Kit |
| :---- | :---- | :---- |
| **HSS Target** | mpfs-icicle-kit-es | mpfs-disco-kit |
| **DDR Subsystem** | 2 GB LPDDR4 (32-bit data bus) | 1 GB DDR4 (16-bit/32-bit compact bus) |
| **Primary Storage** | Onboard eMMC & MicroSD | MicroSD only (no onboard eMMC) |
| **SPI Flash Path** | CoreSPI (FPGA Fabric) | MSS SPI0/SPI1 (Hardened Peripherals) |
| **Ethernet PHY** | VSC8662 (Dual-Port / Cross-MAC MDIO) | VSC8221 (Single Port) |

## **6\. Build Instructions**

### **Toolchain Setup**

The HSS relies on the xPack GNU RISC-V Embedded GCC.

Bash

```
# Export toolchain and Libero paths
export PATH=/opt/xpack-riscv-none-elf-gcc-15.2.0-1/bin:\$PATH
export FPGENPROG=/opt/microchip/Libero_SoC_v2024.1/Libero/bin64/fpgenprog
```

### **Compiling HSS**

Bash

```
cd hart-software-services

# Clean previous build artifacts
make BOARD=mpfs-icicle-kit clean

# Load board default configuration
make BOARD=mpfs-icicle-kit defconfig

# (Optional) Open interactive menu to customize services
make BOARD=mpfs-icicle-kit menuconfig

# Compile HSS binary and eNVM wrapper
make BOARD=mpfs-icicle-kit
```

Build outputs will be generated in build/, including hss-envm-wrapper.hex.

### **Programming eNVM**

Ensure your development kit is connected via USB, then execute:

Bash

```
make BOARD=mpfs-icicle-kit program
```

*(Alternatively, import build/bootmode1/envm\_wrapper\_bm1.hex into FlashPro Express and program it manually).*

## **7\. Contributions & Source Layout**

Pull requests must include a per-commit sign-off stating agreement to the [Developer Certificate of Origin](https://developercertificate.org/).

### **Directory Structure**

* application/ \- Main functions and CRT startup  
* boards/ \- Board-specific initializations and Libero XML mappings  
* envm-wrapper/ \- Decompressor routines to inflate HSS to L2-Scratchpad  
* modules/ \- Cryptography, compression, and Secure Software Message Bus (SSMB)  
* services/ \- HSS state machines (Boot, SPI, MMC, Netboot, SerialBoot, TinyCLI)  
* thirdparty/ \- OpenSBI, Miniz, Libecc, Kconfiglib

