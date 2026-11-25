# Custom Hart Software Services (HSS)

This is a custom version of Microchip's **Hart Software Services (HSS)** for PolarFire® SoC FPGAs, adding support for:

1. **Serialboot** (SFL payload transfer over UART to DDR or non-volatile storage)
2. **SPI Flash Memory Programming & Boot** (writing and booting payloads via SPI flash)
3. **Netboot** (downloading and booting/flashing payloads over Ethernet via DHCP and TFTP)

## 1. Architecture Overview

HSS acts as the zero-stage bootloader and system controller on PolarFire SoC. Running on the E51 monitor core, it configures clocks, trains DDR memory, manages boot media and storage services, and releases U54 application harts to run U-Boot, Linux, or bare-metal payloads.

On PolarFire SoC, HSS consists of two main parts:

1. A superloop monitor running on the E51 minion processor that services requests from U54 application cores.
2. A Machine-Mode software interrupt trap handler that lets the E51 message U54 harts and coordinate cross-hart events.

---

## 2. Automated Boot Cascade Architecture

When HSS boots without a manual CLI override, `HSS_BootInit()` evaluates boot media sequentially and falls back to Ethernet TFTP staging if local non-volatile media do not yield a valid payload:

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

**Note:** HSS prioritizes MMC/SD over SPI Flash so images flashed to removable media take precedence over SPI flash payloads.

---

## 3. HSS TinyCLI Help Menu

Use the `help` command at the TinyCLI prompt to list available commands:

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
  QSPI           - Select boot via QSPI.
  EMMC           - Select boot via eMMC.
  MMC            - Select boot via SDCARD/eMMC.
  SDCARD         - Select boot via SDCARD.
  PAYLOAD        - Select boot via payload.
  SPI            - Select boot via SPI.
  SNVM           - Select boot via sNVM.
  RESUME         - Resume after suspending
  ECC            - Dump ECC stats.
  SERIALBOOT     - Receive binary over SFL and run/flash: SERIALBOOT [sd|emmc|spi|qspi] [addr/offset]
  FLASHID        - Read SPI Flash JEDEC ID: FLASHID
  FLASHBOOT      - Boot image from SPI Flash: FLASHBOOT <spi_offset>
  NETBOOT        - Download and run/flash payload via TFTP: NETBOOT [sd|emmc|spi|qspi] [server_ip] [filename] [addr/offset]

>>
```

You can also pass `help` to subcommand-capable commands such as `serialboot` or `netboot`:

```text
>> serialboot help

Supported subcommands:
  SD             - Receive binary over SFL and flash to SD Card: SERIALBOOT SD
  EMMC           - Receive binary over SFL and flash to eMMC: SERIALBOOT EMMC
  SPI            - Receive binary over SFL and flash to SPI Flash: SERIALBOOT SPI [offset]

Or run without subcommand to load directly to DDR and execute:
  SERIALBOOT [load_addr]
```

---

## 4. Serialboot

Serialboot downloads a payload image into DDR memory over UART using the SFL protocol. Once transferred, the image can either execute directly from DDR or be written to non-volatile storage (SD card, eMMC, QSPI, or SPI flash).

Use `tools/mpfs-terminal.py` to handle the host-side SFL transfer:

* `/dev/ttyUSB0` / `COM9`: HSS UART port on Linux or Windows
* `--kernel`: Target application payload binary to upload (omitting `--kernel` runs `mpfs-terminal.py` as a plain UART terminal)

### 4.1 Download and Execute from DDR

Build your target application for `ddr-release` (targeting DDR base address `0x80000000` by default) and start `mpfs-terminal.py`:

Linux:

```sh
python tools/mpfs-terminal.py /dev/ttyUSB0 --kernel build/mpfs-disco-kit/ddr-release/mpfs-net-app-payload.bin
```

Windows:

```sh
python tools/mpfs-terminal.py COM9 --kernel build/mpfs-disco-kit/ddr-release/mpfs-net-app-payload.bin
```

Reset the board, press a key in the terminal to halt auto-boot and enter TinyCLI, then run `serialboot`:

```text
>> serialboot


[SERIALBOOT] Staging buffer @ 0x0080000000. Waiting for SFL host...
sL5DdSMmkekro

[MPFS-TERM] Magic trigger matched! Replying with handshake ACK...
[MPFS-TERM] Uploading build/mpfs-disco-kit/ddr-release/mpfs-net-app-payload.bin (85936 bytes) -> 0x80000000...
|====================| 100.0% (85936/85936 B)
[MPFS-TERM] Transfer Complete (7.7 KB/s).
[MPFS-TERM] Sending JUMP Command to boot application...
[MPFS-TERM] Terminal active. Continuing console output...


[SERIALBOOT] Transfer complete (85936 bytes received, host base: 0x80000000).
[PAYLOAD] Detected HSS Boot Image header (magic: 0xb007c0de).
[PAYLOAD] Set Name : "PolarFire-SoC-HSS::mpfs-net-app"
[PAYLOAD] Image Len: 85936 bytes
[PAYLOAD]   Chunk[0]: Hart=1  Load=0x0000000790  Exec=0x0080000000  Size=80272 bytes
[PAYLOAD]   Chunk[1]: Hart=1  Load=0x0000014120  Exec=0x0080013990  Size=32 bytes
[PAYLOAD]   Chunk[2]: Hart=1  Load=0x0000014140  Exec=0x00800139b0  Size=3696 bytes
[PAYLOAD] Registering boot image and dispatching U54 harts...
```

### 4.2 Download and Write to SPI Flash

Launch `mpfs-terminal.py` with your payload binary, halt boot at the TinyCLI prompt, and run `serialboot spi` to program the default SPI flash boot offset (`0x400`):

```sh
python tools/mpfs-terminal.py /dev/ttyUSB0 --kernel build/mpfs-disco-kit/ddr-release/mpfs-net-app-payload.bin
```

```text
>> serialboot spi

[SERIALBOOT] Staging buffer @ 0x0080000000. Waiting for SFL host...
sL5DdSMmkekro

[MPFS-TERM] Magic trigger matched! Replying with handshake ACK...
[MPFS-TERM] Uploading build/mpfs-disco-kit/ddr-release/mpfs-net-app-payload.bin (85936 bytes) -> 0x80000000...
|====================| 100.0% (85936/85936 B)
[MPFS-TERM] Transfer Complete (7.7 KB/s).
[MPFS-TERM] Sending JUMP Command to boot application...
[MPFS-TERM] Terminal active. Continuing console output...


[SERIALBOOT] Transfer complete (85936 bytes received, host base: 0x80000000).
[SPI-FLASH] Detected Microchip SST25WF (MFR: 0x62, DEV: 0x0613)
[SPI-FLASH] Backing up 1024 prefix bytes at 0x0...
[SPI-FLASH] Erasing 64 KiB sectors covering 0x400 (85936 bytes)...
[SPI-FLASH] Programming 85936 bytes to 0x400...
[SPI-FLASH] SUCCESS: Payload verified in SPI Flash @ 0x400! Reset board to boot.
```

To flash to a custom SPI offset (for example, `0x1000`), pass the offset argument:

```text
>> serialboot spi 0x1000

[SERIALBOOT] Staging buffer @ 0x0080000000. Waiting for SFL host...
sL5DdSMmkekro

[MPFS-TERM] Magic trigger matched! Replying with handshake ACK...
[MPFS-TERM] Uploading build/mpfs-disco-kit/ddr-release/mpfs-net-app-payload.bin (85936 bytes) -> 0x80000000...
|====================| 100.0% (85936/85936 B)
[MPFS-TERM] Transfer Complete (7.7 KB/s).
[MPFS-TERM] Sending JUMP Command to boot application...
[MPFS-TERM] Terminal active. Continuing console output...


[SERIALBOOT] Transfer complete (85936 bytes received, host base: 0x80000000).
[SPI-FLASH] Detected Microchip SST25WF (MFR: 0x62, DEV: 0x0613)
[SPI-FLASH] Backing up 4096 prefix bytes at 0x0...
[SPI-FLASH] Erasing 64 KiB sectors covering 0x1000 (85936 bytes)...
[SPI-FLASH] Programming 85936 bytes to 0x1000...
[SPI-FLASH] SUCCESS: Payload verified in SPI Flash @ 0x1000! 
Reset board to boot.
```

Automatic booting checks the default offset (`0x400`). To boot a payload stored at a custom offset without resetting, run `flashboot <offset>`:

```text
>> flashboot 0x1000


[FLASHBOOT] Reading 85936 bytes from SPI @ 0x1000 to DDR @ 0x0080000000...
[PAYLOAD] Detected HSS Boot Image header (magic: 0xb007c0de).
[PAYLOAD] Set Name : "PolarFire-SoC-HSS::mpfs-net-app"
[PAYLOAD] Image Len: 85936 bytes
[PAYLOAD]   Chunk[0]: Hart=1  Load=0x0000000790  Exec=0x0080000000  Size=80272 bytes
[PAYLOAD]   Chunk[1]: Hart=1  Load=0x0000014120  Exec=0x0080013990  Size=32 bytes
[PAYLOAD]   Chunk[2]: Hart=1  Load=0x0000014140  Exec=0x00800139b0  Size=3696 bytes
[PAYLOAD] Registering boot image and dispatching U54 harts...
[33.909924] Boot image set name: "PolarFire-SoC-HSS::mpfs-net-app"
[33.917658] healthmon_service :: [init] -> [monitoring]
[33.924628] boot_service(u54_1):: BOOT_FLAG_SKIP_OPENSBI found
[33.932267] boot_service(u54_1) :: [Init] -> [SetupPMP]
[33.939237] boot_service(u54_2):: BOOT_FLAG_SKIP_OPENSBI found
[33.946875] boot_service(u54_2) :: [Init] -> [SetupPMP]
[33.953845] boot_service(u54_3):: BOOT_FLAG_SKIP_OPENSBI found
[33.961484] boot_service(u54_3) :: [Init] -> [SetupPMP]
[33.968454] boot_service(u54_4):: BOOT_FLAG_SKIP_OPENSBI found
[33.976092] boot_service(u54_4) :: [Init] -> [SetupPMP]
```

### 4.3 Download and Write to SD Card or eMMC

Use `serialboot sd` or `serialboot emmc` to stage a payload into DDR and write it to removable or onboard MMC storage:

```text
>> serialboot sd

[SERIALBOOT] Staging buffer @ 0x0080000000. Waiting for SFL host...
sL5DdSMmkekro

[MPFS-TERM] Magic trigger matched! Replying with handshake ACK...
[MPFS-TERM] Uploading build/mpfs-disco-kit/ddr-release/mpfs-net-app-payload.bin (85936 bytes) -> 0x80000000...
|====================| 100.0% (85936/85936 B)
[MPFS-TERM] Transfer Complete (7.7 KB/s).
[MPFS-TERM] Sending JUMP Command to boot application...
[MPFS-TERM] Terminal active. Continuing console output...


[SERIALBOOT] Transfer complete (85936 bytes received, host base: 0x80000000).
[FLASH] Selecting SD Card interface...
[329.198838] Selecting SDCARD as boot source ...
[329.204853] Attempting to select SDCARD ... Passed
[329.379295] Attempting to select SDCARD ... Passed
[329.408608] GPT signature not as expected
[FLASH] Writing 85936 bytes (aligned: 86016) to SD Card offset 0x0...
[FLASH] SUCCESS: Payload verified on SD Card! 
Reset board to boot.
```

---

## 5. Using YMODEM with SPI Flash Support

You can also transfer a payload into DDR over UART via YMODEM and write it directly to SPI flash (or other supported targets). Commands automatically use the SPI flash backend configured in `CONFIG_SPI_FLASH_BACKEND`:

1. Connect a serial terminal (such as Tera Term) to the HSS UART console.
2. Reset the board and press a key to stop auto-boot and enter `TinyCLI`.
3. Run `ymodem spi` (or `ymodem spi 0x1000` for a custom offset):

```text
>> ymodem spi
```

4. In Tera Term, select **File -> Transfer -> YMODEM -> Send...** and choose your payload file (for example, `build/mpfs-icicle-kit/ddr-release/mpfs-netapp-dualport-payload.bin`).

Once the transfer completes, HSS programs and verifies the image in SPI flash:

```text
>> ymodem spi

[YMODEM] Ready to receive payload into 0x80000000 and flash at SPI offset 0x400...
CCCCCCCCC

Received 85936 bytes from mpfs-net-app-payload.bin (CRC32 is 0x0638C289)
[SPI-FLASH] Detected Microchip SST25WF (MFR: 0x62, DEV: 0x0613)
[SPI-FLASH] Backing up 1024 prefix bytes at 0x0...
[SPI-FLASH] Erasing 64 KiB sectors covering 0x400 (85936 bytes)...
[SPI-FLASH] Programming 85936 bytes to 0x400...
[SPI-FLASH] SUCCESS: Payload verified in SPI Flash @ 0x400! 
Reset board to boot.
```

Reset the board to boot from SPI flash, or run `flashboot` to boot immediately.

---

## 6. Boot and Flash via Netboot (Ethernet / TFTP)

The `NETBOOT` service fetches images over Ethernet using DHCP and TFTP and passes them through `HSS_Boot_DecodeAndBootDDR()`, supporting both structured `hss-payload-generator` images and raw flat binaries:

| Command | Description |
| :--- | :--- |
| `netboot [ip] [file] [addr]` | Downloads a TFTP image to DDR (`0x80000000`), decodes the container if present, and boots U54 harts. |
| `netboot emmc [ip] [file]` | Downloads an image via TFTP, flushes cached data to physical RAM, and flashes onboard eMMC. |
| `netboot sd [ip] [file]` | Downloads an image via TFTP, flushes cached data to physical RAM, and flashes a microSD card. |
| `netboot spi [ip] [file] [offset]` | Downloads an image via TFTP and flashes SPI flash (default offset `0x400`). |
| `netboot qspi [ip] [file]` | Downloads an image via TFTP and flashes MSS QSPI flash. |

All arguments in brackets are optional and default to DHCP auto-discovery, `boot.bin`, DDR address `0x80000000`, and SPI offset `0x400`.

### Example Output: `netboot sd`

```text
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

---

## 7. Payload Preparation Requirements

Flashing to non-volatile storage or loading multi-hart ELF segments requires an image packaged with `hss-payload-generator`. Raw flat binaries can only be executed directly from DDR.

Example `baremetal.yaml`:

```yaml
# baremetal.yaml
set-name: 'PolarFire-SoC-Payload'
hart-entry-points: {u54_1: '0x80000000'}
payloads:
  app.elf: {exec-addr: '0x80000000', owner-hart: u54_1, priv-mode: prv-m}
```

Generate `payload.bin`:

```sh
./tools/hss-payload-generator/hss-payload-generator -c baremetal.yaml payload.bin
```

---

## 8. Hardware Differences and Build Instructions

| Feature | PolarFire SoC Icicle Kit | PolarFire SoC Discovery Kit |
| :--- | :--- | :--- |
| **HSS Target** | `mpfs-icicle-kit` / `mpfs-icicle-kit-es` | `mpfs-disco-kit` |
| **DDR Subsystem** | 2 GB LPDDR4 (32-bit data bus) | 1 GB DDR4 (DDR-Lo 32 MiB + DDR-Hi 888 MiB) |
| **DDR Target Addr** | `0x103fc00000` | `0x1020000000` (must fit within 888 MiB bounds) |
| **Console UART** | MMUART0 (`g_mss_uart0_lo`) | MMUART1 (`g_mss_uart1_lo`) |
| **Primary Storage** | Onboard eMMC & MicroSD | MicroSD only (no onboard eMMC) |
| **SPI Flash Path** | CoreSPI (FPGA Fabric, Micron MT25Q) | MSS SPI (Hardened Peripheral, Microchip SST25WF) |
| **Ethernet PHY** | VSC8662 (Dual-Port / Cross-MAC MDIO) | VSC8221 (Single Port, GEM0) |
| **Embedded DTB** | Supported (`CONFIG_PROVIDE_DTB=y`) | Disabled (`# CONFIG_PROVIDE_DTB is not set`) |

### Toolchain Setup

```sh
export PATH=/opt/xpack-riscv-none-elf-gcc-15.2.0-1/bin:$PATH
export FPGENPROG=/opt/microchip/Libero_SoC_v2024.1/Libero/bin64/fpgenprog
```

### Compiling and Programming eNVM

```sh
cd hart-software-services

# 1. Load board default configuration (run defconfig whenever switching boards)
make BOARD=mpfs-icicle-kit defconfig   # or BOARD=mpfs-disco-kit

# 2. (Optional) Enable Serialboot, Netboot, and SPI Flash drivers via menuconfig
make BOARD=mpfs-icicle-kit menuconfig

# 3. Clean, build, and program target eNVM
make -j$(nproc) BOARD=mpfs-icicle-kit clean all program
```

**Tip when switching boards:** Save board-specific `.config` files (for example, `boards/mpfs-disco-kit/def_config_serial_netboot`) so switching targets preserves board-specific settings such as `CONFIG_PROVIDE_DTB`, staging addresses, and `CONFIG_SPI_FLASH_MSS_SPI` vs. `CONFIG_SPI_FLASH_CORESPI`.