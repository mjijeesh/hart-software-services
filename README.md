# Added NETBOOT into the auto booting sequence


# HSS Netboot Service 

## 1. Overview
The HSS Netboot Service is a robust, bare-metal TFTP/DHCP bootloader implementation for the PolarFire SoC Hart Software Services (HSS). It allows developers to dynamically download and execute binary payloads (`boot.bin`) directly into DDR memory over an Ethernet network, bypassing the need for eMMC, SD Card, or QSPI flash programming during iterative development.

### Core Features
* **Dynamic DHCP Discovery:** Automatically extracts the TFTP Server IP from BOOTP Next-Server (`siaddr`) or DHCP Option 66, falling back to a static Kconfig default if no server is advertised.
* **Network Switch Compatibility:** Includes a 3.0-second delay after link auto-negotiation to accommodate Spanning Tree Protocol (STP) listening/learning states on managed switches.
* **Active Retransmission:** Mitigates UDP packet loss by using active retransmission loops for DHCP Discovery, ARP Resolution, and TFTP Read Requests.
* **Cross-Subnet Routing:** Automatically resolves the default Gateway MAC address if the target TFTP server resides on a different subnet.
* **Fallback Static IP:** Seamlessly transitions to a static board IP if the DHCP server times out or is absent (direct peer-to-peer cable connection).

---

## 2. Hardware Profiles

The Netboot service abstracts hardware differences between PolarFire SoC development kits using a board configuration struct (`netboot_hw_config_t`).

### MPFS Discovery Kit
* **Ethernet Port:** J1
* **MAC Controller:** GEM0 (`g_mac0`)
* **PHY Chip:** VSC8221 (Single Port Gigabit)
* **PHY Address:** `11U`
* **Management Bus:** Direct to GEM0

### MPFS Icicle Kit
The Icicle Kit utilizes a dual-port VSC8662 PHY. The MDIO management bus for **both** ports is physically routed to GEM1.
* **Port J2 (Primary):** Data via GEM1 (`g_mac1`), MDIO via GEM1 (`g_mac1`). PHY Address `9U`.
* **Port J1 (Secondary):** Data via GEM0 (`g_mac0`), MDIO routed across to GEM1 (`g_mac1`). PHY Address `8U`.
  * *Note: Port J1 utilizes **Cross-MAC MDIO** routing. GEM0 handles the SGMII data path and DMA, while GEM1 acts as the `phy_controller` to execute MDIO register reads/writes.*

---

## 3. File Modification Manifest

The implementation required modifying the core HSS services, board initialization files, and the Microchip MSS Bare-Metal driver configuration.

### Core Services
* `services/netboot/netboot_service.c`
  * Implemented the standalone Netboot engine (Hardware Init, DHCP, ARP, TFTP).
  * Added weak attributes (`__attribute__((weak))`) to freestanding linker stubs (`HAL_disable_interrupts`, `strcpy`, etc.) to prevent duplicate symbol conflicts on boards that compile `hal_irq.o` (like the Icicle Kit).
  * Added dynamic ISR polling to support both `PLIC_mac0_int_IRQHandler` and `PLIC_mac1_int_IRQHandler`.
* `services/netboot/netboot_service.h`
  * Exposed `NETBOOT_Service_FetchImage()` and the U54 Hart dispatcher `netboot_jump()`.
* `services/netboot/netboot_board.h`
  * Defined `netboot_hw_config_t` to abstract MAC, PHY, clock masks, and cross-MAC MDIO controllers (`phy_controller`).
* `services/tinycli/tinycli_api.c`
  * Registered the `netboot` command and wired it to `NETBOOT_Service_FetchImage(0, ...)`.

### Board Profiles
* `boards/mpfs-disco-kit/board_eth.c`
  * Configured `s_disco_net_cfg` for GEM0 and VSC8221 PHY.
* `boards/mpfs-icicle-kit/board_eth.c`
  * Configured `s_icicle_net_cfg` for VSC8662. Included a `#define ICICLE_USE_PORT_J1` toggle to easily switch between Port J2 (Direct GEM1) and Port J1 (GEM0 Data + GEM1 MDIO).

### Build System & Driver Configuration (Crucial Linker Fix)
To prevent linker conflicts caused by compiling multiple unreferenced PHY drivers, the monolithic `mss_ethernet_mac_sw_cfg.h` was shadowed at the board level.
* `boards/mpfs-disco-kit/mss_ethernet_mac_sw_cfg.h`
  * Enables **only** `MSS_MAC_USE_PHY_VSC8221`. Sets HW ID to `MSS_MAC_DESIGN_DISCOVERY_GEM0`.
* `boards/mpfs-icicle-kit/mss_ethernet_mac_sw_cfg.h`
  * Enables **only** `MSS_MAC_USE_PHY_VSC8662`. Sets HW ID to `MSS_MAC_DESIGN_ICICLE_STD_GEM0`.
* `boards/<board>/Makefile`
  * Added `-include $(BOARD_DIR)/mss_ethernet_mac_sw_cfg.h` to `CFLAGS` to force the compiler to use the board-specific hardware definitions, bypassing the generic vendor file.

---

## 4. Expected Results

To initiate a network boot, interrupt the boot sequence to enter the TinyCLI, then type `netboot`.

### Expected Output: MPFS Discovery Kit (VSC8221)
```text
>> netboot

[CLI] Initiating Netboot service...

================ [NETBOOT: HARDWARE INIT] ================
[NETBOOT] [1/5] Cycling GEM peripheral reset and clock...
[NETBOOT] [2/5] Configuring GEM MAC Core...
[NETBOOT]       Station MAC Address : 00:FC:00:12:34:58
[NETBOOT] [3/5] Probing Hardware Registers (PHY Addr: 11)...
[NETBOOT]       PHY ID 1 (Reg 0x02) : 0x000F
[NETBOOT]       PHY ID 2 (Reg 0x03) : 0xC551
[NETBOOT] [4/5] Priming DMA Descriptor Queues in Polled Mode...
[NETBOOT] [5/5] Waiting for Copper Auto-Negotiation...
[NETBOOT]       Link State          : UP
[NETBOOT]       Negotiated Speed    : 1000 Mbps (Gigabit)
[NETBOOT]       Duplex Mode         : Full Duplex
[NETBOOT] Allowing switch port to enter forwarding state (3.0s)...
================ [NETBOOT: DHCP NEGOTIATION] ================
[DHCP] Broadcasting DHCP Discover (Attempt 1/6)...
[DHCP] DHCP Offer Successfully Received!
[DHCP]   |- Assigned Board IP : 192.168.20.37
[DHCP]   |- Subnet Mask       : 255.255.255.0
[DHCP]   \- Gateway IP        : 192.168.20.1
=============================================================

[NETBOOT] Target TFTP Server : 192.168.20.94 (Host Configuration)
[NETBOOT] Sending ARP Request for 192.168.20.94 (Attempt 1/4)...
[NETBOOT] Hardware MAC Resolved: 24:41:8C:F1:9B:45
[NETBOOT] Requesting file 'boot.bin' via TFTP...

[NETBOOT] Transfer complete! Total: 34928 bytes
[NETBOOT] Quiescing GEM MAC...
[NETBOOT] MAC released.
[CLI] Transfer complete (34928 bytes). Dispatching harts to 0x0000000080000000...

[NETBOOT] Synchronizing caches and dispatching all U54 harts to 0x0000000080000000...
[NETBOOT] All U54 harts launched. Application active on UART1 (/dev/ttyUSB1).
[NETBOOT] Returning to HSS TinyCLI.
```

### Expected Output: MPFS Icicle Kit (VSC8662 / Port J1 / Cross-MAC MDIO)
```text
>> netboot

[CLI] Initiating Netboot service...

================ [NETBOOT: HARDWARE INIT] ================
[NETBOOT] [1/5] Cycling GEM peripheral reset and clock...
[NETBOOT] [2/5] Configuring GEM MAC Core...
[NETBOOT]       Station MAC Address : 00:04:A3:11:22:33
[NETBOOT] [3/5] Probing Hardware Registers (PHY Addr: 8)...
[NETBOOT]       PHY ID 1 (Reg 0x02) : 0x0007
[NETBOOT]       PHY ID 2 (Reg 0x03) : 0x0660
[NETBOOT] [4/5] Priming DMA Descriptor Queues in Polled Mode...
[NETBOOT] [5/5] Waiting for Copper Auto-Negotiation...
[NETBOOT]       Link State          : UP
[NETBOOT]       Negotiated Speed    : 1000 Mbps (Gigabit)
[NETBOOT]       Duplex Mode         : Full Duplex
[NETBOOT] Allowing switch port to enter forwarding state (3.0s)...
================ [NETBOOT: DHCP NEGOTIATION] ================
[DHCP] Broadcasting DHCP Discover (Attempt 1/6)...
[DHCP] DHCP Offer Successfully Received!
[DHCP]   |- Assigned Board IP : 192.168.20.75
[DHCP]   |- Subnet Mask       : 255.255.255.128
[DHCP]   \- Gateway IP        : 192.168.20.1
=============================================================

[NETBOOT] Target TFTP Server : 192.168.20.94 (Host Configuration)
[NETBOOT] Sending ARP Request for 192.168.20.94 (Attempt 1/4)...
[NETBOOT] Hardware MAC Resolved: 24:41:8C:F1:9B:45
[NETBOOT] Requesting file 'boot.bin' via TFTP...

[NETBOOT] Transfer complete! Total: 34928 bytes
[NETBOOT] Quiescing GEM MAC...
[NETBOOT] MAC released.
[CLI] Transfer complete (34928 bytes). Dispatching harts to 0x0000000080000000...

[NETBOOT] Synchronizing caches and dispatching all U54 harts to 0x0000000080000000...
[NETBOOT] All U54 harts launched. Application active on UART1 (/dev/ttyUSB1).
```




# Hart Software Services (HSS) Documentation: Icicle Kit & Discovery Kit

The Microchip Hart Software Services (HSS) serves as the zero-stage bootloader and system controller for PolarFire® SoC FPGAs. It executes on the E51 monitor core to configure clocks, train DDR memory, release application U54 harts, and load boot payloads (U-Boot, Linux, or bare-metal binaries).

This guide outlines the hardware-specific architectural differences, modifications required between the **PolarFire SoC Icicle Kit** and **PolarFire SoC Discovery Kit**, build and flashing procedures, and CLI runtime usage.

---

## 1. Hardware Overview & Configuration Differences

| Feature | PolarFire SoC Icicle Kit | PolarFire SoC Discovery Kit |
| :--- | :--- | :--- |
| **Target Device** | MPFS250T-FCVG484EES / MPFS250TS | MPFS095T-1FCSG325E |
| **HSS Target Identifier** | `mpfs-icicle-kit-es` (or `mpfs-icicle-kit`) | `mpfs-disco-kit` |
| **DDR Subsystem** | 2 GB LPDDR4 (32-bit data bus) | 1 GB DDR4 (16-bit / 32-bit compact bus) |
| **Primary Storage** | Onboard eMMC (default) & MicroSD slot | MicroSD card socket only (no onboard eMMC) |
| **UART Port / Bridge** | Silicon Labs CP2108 Quad USB-to-UART (Interface 0/1) | FTDI / On-board FlashPro5 USB-C (UART Interface 0) |
| **Hardware Config Source** | `boards/mpfs-icicle-kit-es/fpga_design_config` | `boards/mpfs-disco-kit/fpga_design_config` |

---

## 2. Key HSS Modifications

When adapting or customizing HSS across the two boards, modifications center on three core subsystems:

### A. MSS Component & DDR Memory Timing Configuration
The HSS generates hardware initialization tables from Libero MSS XML exports placed in `boards/<BOARD>/fpga_design_config`:
* **Icicle Kit (`mpfs-icicle-kit-es`)**: Configured for LPDDR4 timings, IO bank voltages, and full MPFS250 peripheral mapping.
* **Discovery Kit (`mpfs-disco-kit`)**: Adjusted for the MPFS095 device package and DDR4 memory profile. Memory training parameters, PLL clock multipliers, and bank registers reflect the smaller pin-count FPGA.

### B. Boot Source & Storage Services (`Kconfig`)
* **Icicle Kit**: Default configurations enable both MMC/eMMC (`CONFIG_SERVICE_BOOT_MMC=y`) and SD card options. Payload loading prioritizes partition offsets on eMMC flash.
* **Discovery Kit**: Because onboard eMMC is not populated, MMC peripheral routing must target the MicroSD interface (`mmcblk0` on the physical SD bus), ensuring the boot service does not stall polling non-existent eMMC hardware.

### C. Console UART & CLI Output
* HSS prints debug logs and the interactive CLI via the Microchip MSS UART peripheral.
* Ensure the target UART controller instance matches the hardware schematic routing:
  * Icicle Kit routes MSS UART0 to Interface 0 (or UART1 depending on board revision).
  * Discovery Kit routes MSS UART0 directly through the single Type-C serial debug interface.

---

## 3. Environment Setup & Toolchain Requirements

Ensure the following tools are installed and present in your system `PATH`:
* **RISC-V Toolchain:** xPack GNU RISC-V Embedded GCC or Microchip SoftConsole v2022.2+
* **Microchip Tools:** Libero SoC / FlashPro Express (provides `fpgenprog`)
* **Python 3:** with Tkinter (`sudo apt install python3-tk`) for Kconfig menu operations

```bash
# Export toolchain and Libero paths (adjust paths according to your installation)
export SC_INSTALL_DIR=/opt/microchip/SoftConsole-v2022.2-RISC-V-747
export FPGENPROG=/opt/microchip/Libero_SoC_v2024.1/Libero/bin64/fpgenprog
export PATH=$PATH:$SC_INSTALL_DIR/riscv-unknown-elf-gcc/bin:$SC_INSTALL_DIR/python3/bin
export CROSS_COMPILE=riscv64-unknown-elf-
```

---

## 4. Build Instructions

### Building for the Icicle Kit
```bash
cd hart-software-services

# Clean previous build artifacts
make clean BOARD=mpfs-icicle-kit-es

# Load Icicle Kit default configuration
make BOARD=mpfs-icicle-kit-es defconfig

# (Optional) Open interactive menu to modify CLI features, boot timers, or services
make BOARD=mpfs-icicle-kit-es menuconfig

# Compile HSS binary and eNVM wrapper
make -j$(nproc) BOARD=mpfs-icicle-kit-es
```

### Building for the Discovery Kit
```bash
cd hart-software-services

# Clean previous build artifacts
make clean BOARD=mpfs-disco-kit

# Load Discovery Kit default configuration
make BOARD=mpfs-disco-kit defconfig

# (Optional) Open interactive menu
make BOARD=mpfs-disco-kit menuconfig

# Compile HSS binary and eNVM wrapper
make -j$(nproc) BOARD=mpfs-disco-kit
```

Build outputs will be generated in `build/`, including:
* `build/hss-envm.elf`: Raw ELF binary.
* `build/bootmode1/envm_wrapper_bm1.hex`: Formatted hex file packaged with the decompressor for direct programming into eNVM.

---

## 5. Programming HSS into eNVM

Connect your development kit via USB to the host computer.

### Option 1: Direct Command-Line Programming via `fpgenprog`
Ensure `FPGENPROG` is exported in your environment, then execute:

* **For Icicle Kit:**
  ```bash
  make BOARD=mpfs-icicle-kit-es program
  ```

* **For Discovery Kit:**
  ```bash
  make BOARD=mpfs-disco-kit program
  ```

### Option 2: Using FlashPro Express GUI
1. Open **FlashPro Express**.
2. Create a new job file or open your board’s base design job file.
3. Import the generated `.hex` file:
   `build/bootmode1/envm_wrapper_bm1.hex` into the eNVM client.
4. Set the programmer action to **PROGRAM** and click **RUN**.

---

## 6. How to Use & Interact with HSS

### Serial Terminal Connection
Open a terminal emulator (PuTTY, Minicom, Picocom) with the following parameters:
* **Baud Rate:** `115200`
* **Data Bits:** `8`
* **Parity:** `None`
* **Stop Bits:** `1`
* **Flow Control:** `None`

* **Icicle Kit:** Connect to the first or second enumerated USB serial port (e.g., `/dev/ttyUSB0` or `/dev/ttyUSB1`).
* **Discovery Kit:** Connect to the enumerated primary COM port (e.g., `/dev/ttyUSB0`).

### Accessing the Interactive HSS CLI
Power-cycle or assert the reset push-button on the board. During boot, HSS displays banner info and counts down:

```text
Hart Software Services (HSS) - version 0.99.xx
MPFS HAL version 2.x.xxx / DDR Driver version 0.x.xxx
Press <SPACE> or <CTRL+C> to enter interactive mode...
```

Press `<SPACE>` or `<CTRL+C>` before timeout expires to enter the prompt (`>> `).

### Useful HSS CLI Commands
* `help`: Lists all supported commands.
* `boot`: Resumes booting the configured payload from storage.
* `mmc info`: Queries the attached eMMC (Icicle) or SD card (Icicle/Discovery) geometry and status.
* `mmc read <addr> <sector> <count>`: Reads raw blocks from card into RAM for diagnostic testing.
* `service`: Shows status of background state machines and active services.
* `diag`: Runs built-in hardware diagnostics (e.g., DDR memory pattern checks).

---

## 7. Packaging Payload Images (`hss-payload-generator`)

HSS boots secondary binaries (U-Boot, Linux kernel, or bare-metal images) using a formatted payload image generated with `hss-payload-generator`:

1. Create a `config.yaml` matching your binary layout:
   ```yaml
   set-name: 'PolarFire-SoC-Payload'
   hart-entry-points: {u54_1: '0x80000000', u54_2: '0x80000000', u54_3: '0x80000000', u54_4: '0x80000000'}
   payloads:
     u-boot.bin: {exec-addr: '0x80000000', owner-hart: u54_1, secondary-hart: [u54_2, u54_3, u54_4], priv-mode: prv-s}
   ```
2. Generate the binary payload:
   ```bash
   hss-payload-generator -c config.yaml payload.bin
   ```
  
3. Write `payload.bin` to the designated raw partition (typically GUID `21686148-6449-6E6F-744E-656564454649`) on your SD card or eMMC.


# HSS with SerialBoot and SPI Flash Boot support

brnach: flashboot4 : add a generic spi flash driver for different board , wip


This is a modified version of HSS with below features added.

*  Load  an application binary to DDR memory and execute.
*  Upload an application binary ( payload format ) ino  the SPI Flash.
*  Boot from SPI Flash ( System Controller SPI)

The HSS is tested on the ICICLE kit with the  modified standard reference design with CoreSPI Connected to the System Controller SPI interface.
CoreSPI base Adress is at 0x40000600
# Build instructions.

configure the build system to enable SerialBoot SFL Support under services.


Use the `menuconfig` command and select  `SerialBoot SFL Support` under services. 
Also enable the `System Controller SPi Boot Option`

`#/git/hart-software-services$ make BOARD=mpfs-icicle-kit menuconfig`

save the configuration into the `.config` file.

alternatevely copy the preconfigured config file from the `board/mpfs-icicle-kit/` folder

`#/git/hart-software-services$ cp boards/mpfs-icicle-kit/def_config_minimal_serialboot_payload   .config `

### Build the HSS

`#/git/hart-software-services$  make BOARD=mpfs-icicle-kit clean all`

```
CC        build/thirdparty/opensbi/lib/sbi/sbi_hfence.o
 CC        build/services/opensbi/opensbi_ecall_exts.o
 CPP       boards/mpfs-icicle-kit/hss-l2scratch.ld
 LD        hss-l2scratch.elf
 NM        hss-l2scratch.sym
 BIN       hss-l2scratch.bin
   text	   data	    bss	    dec	    hex	filename
 133108	  27584	 149008	 309700	  4b9c4	build/hss-l2scratch.elf
 COMPRESS  build/hss-l2scratch.bin
 CC        build/envm-wrapper/envm-wrapper_crt.o
 CC        build/envm-wrapper/envm-wrapper_validate_crc.o
 CC        build/envm-wrapper/envm-wrapper_stubs.o
 CC        build/modules/misc/stack_guard.o
 LD        hss-envm-wrapper.elf
 NM        hss-envm-wrapper.sym
 BIN       hss-envm-wrapper.bin
 HEX       hss-envm-wrapper.hex
16:28:15 INFO  - mpfsBootmodeProgrammer v3.7 started.
16:28:15 INFO  - "/home/jijeesh/git/hart-software-services/build/bootmode1" is the output folder and the previous contents of this folder will be deleted.
16:28:15 INFO  - Selected boot mode "1 - non-secure boot from eNVM" and working in directory "/home/jijeesh/git/hart-software-services/build".
16:28:15 INFO  - Generating BIN file...
16:28:15 INFO  - Generating header...
16:28:15 INFO  - Generating HEX file...
16:28:15 INFO  - Preparing for bitstream generation...
16:28:15 INFO  - Generating bitstream...
16:28:23 INFO  - Programming/verifying the target skipped because --dryrun was specified.
16:28:23 INFO  - mpfsBootmodeProgrammer completed successfully.
   text	   data	    bss	    dec	    hex	filename
  89332	    416	 149632	 239380	  3a714	build/hss-envm-wrapper.elf

```

### Program the target Board eNVM

Once the build process is over , program the hss bootloader program into the target board envm.

`#/git/hart-software-services$  make BOARD=mpfs-icicle-kit program`

```
INFO: Linux detected
INFO: Linux Desktop detected
mpfs-icicle-kit selected
INFO: Using xPack GNU RISC-V Embedded GCC (riscv-none-elf 15.2.0)
INFO: NOTICE: enabling -flto (which means stack protection is disabled)
INFO: Expected mpfsBootmodeProgrammer.jar version v3.6 but found v3.7
INFO: This version of the HSS relies on SoftConsole v2021.3 or later
INFO: Using xPack GNU RISC-V Embedded GCC (riscv-none-elf 15.2.0)
16:29:15 INFO  - mpfsBootmodeProgrammer v3.7 started.
16:29:15 INFO  - "/home/jijeesh/git/hart-software-services/build/bootmode1" is the output folder and the previous contents of this folder will be deleted.
16:29:15 INFO  - Selected boot mode "1 - non-secure boot from eNVM" and working in directory "/home/jijeesh/git/hart-software-services/build".
16:29:15 INFO  - Generating BIN file...
16:29:15 INFO  - Generating header...
16:29:15 INFO  - Generating HEX file...
16:29:15 INFO  - Preparing for bitstream generation...
16:29:15 INFO  - Generating bitstream...
16:29:23 INFO  - Programming the target...
16:29:34 INFO  - mpfsBootmodeProgrammer completed successfully.
```


# Loading Application Binary to DDR over UART.

After programming the  new hss reset the board and connect a termianl program to the uart port.
Use the provided `mpfs-terminal.py` python application for supporting the serialboot function.
The script is supported in both linux and windows.

copy your application binary(.bin)  which is build to run from ddr memory to the current folder.

To use the python script for just as termianl application use the below comamnd.

`#/git/hart-software-services$ python3 mpfs-terminal.py  /dev/ttyUSB0`

if serialboot is needed , below command format can be used. `build/ddr/pfsoc_bios.bin` is the target baremetal binary file


`#/git/hart-software-services$ python3 mpfs-terminal.py  /dev/ttyUSB0 --kernel build/ddr/pfsoc_bios.bin`

Once the termianl is opened, reset the board  and interrupt  the autobooting from spi/emmc/sdcard if enabled.

Use the `HELP` command to see available commands. 

We should see additional  commands `SERIALBOOT FLASHID FLASHBOOT` 

type the `serialboot` command to start downlaoding the bianry file into the ddr memory and start executing from there.


```
[MPFS-TERM] Opened /dev/ttyUSB0 at 115200 baud. (Press Ctrl+C to exit)
[MPFS-TERM] Type 'serialboot' on prompt to trigger download.

[4.274079] CLI boot interrupt timeout
[4.279044] loop 397315 took 606529217 ticks (max 606529217 ticks)
[4.286969] Initializing Boot Image ...
[4.292029] Trying to get boot image via SPI ...
[4.297949] Preparing to copy from SPI Flash +0x400 ...
[4.304537] Attempting to read image header (1632 bytes) ...
[4.312523] magic is 04078793 vs expected b007c0de or c08b8355
[4.320066] Failed to get boot image, cannot decompress
[4.326940] Trying to get boot image via MMC ...
[4.332860] Attempting to select SDCARD ... Failed
[4.390044] Attempting to select eMMC ... Failed
>> help

Supported commands:
BOOT RESET HELP VERSION UPTIME DEBUG MEMTEST QSPI EMMC MMC SDCARD PAYLOAD SPI SNVM RESUME SERIALBOOT FLASHID FLASHBOOT 
>> serialboot


Ready for SFL transfer. Connect host script...
sL5DdSMmkekro

[MPFS-TERM] Magic trigger matched! Replying with handshake ACK...
[MPFS-TERM] Uploading build/ddr/pfsoc_bios.bin (32576 bytes) -> 0x80000000...
|====================| 100.0% (32576/32576 B)
[MPFS-TERM] Transfer Complete (9.8 KB/s).
[MPFS-TERM] Sending JUMP Command to boot application...
[MPFS-TERM] Terminal active. Continuing console output...


[SERIALBOOT] Synchronizing caches and dispatching all U54 harts to 0x0000000080000000...
[SERIALBOOT] All U54 harts launched. Application active on UART1 (/dev/ttyUSB1).
[SERIALBOOT] Returning to HSS TinyCLI.

[14.432526] u54 State Change:  [Running] [Running] [Running] [Running]
[14.441979] loop 1463774 took 2018520057 ticks (max 2018520057 ticks)
>> [20.817499] loop 2500000 took 3621 ticks (max 2018520057 ticks)



```

Now check your application terminal if any and verify that the program is running from other harts.

Note : dont use the payload bianry format for this .


# Downloading a binary Payload into the SPI flash and boot

This version of hss also provide support for the micron spi flash driver and we can downlaod a payload binary into the spi flash.


### build the payload binary.

We need to build the payload binary using the `hss-payload-generator` tools.

build the `hss-payload-generator` tool using the original instructions.


`#/git/hart-software-services$/tools/hss-payload-generator$ make clean all`

copy your test application  `app.elf`to the working directory.

generate the payload binary using the below command. an example `baremetal.yaml` file is provided  in the test folder.

 modify to match your file name. here  output file is `payload.bin`

`./hss-payload-generator  -c test/baremetal.yaml  payload.bin`

```
Hart Software Service formatted boot image generator v0.99.53
Copyright (c) 2021-2026 Microchip FPGA Embedded Systems Solutions.

NOTICE: test/pfsoc_bios.elf: ignoring >>exec-addr=0x80000000<< as payload is an ELF file

Set-name is >>PolarFire-SoC-HSS::pfsoc_bios<<
Output filename is >>payload.bin<<

```

### Load the Payload binary using mpfs-terminal

Use the mpfs-terminal python app to load the `payload.bin` to target spi flash memory.
use the `--flash` option to specify the target location is spiflash and not ddr.

`#/git/hart-software-services$ python3 mpfs-terminal.py  /dev/ttyUSB0 --kernel tools/hss-payload-generator/baremetal.bin --flash`

by default the target spi flash adress  offset is 0x20000 . to match this to your hss boot offset for spi, you can also specify the target addr using the   `--kernel-adr` option

eg: `--kernel-adr 0x400 `


Use the `FLASHID` command to verify the spi flash interface is enabled and proper.
`serialboot` command with --flash option will upload the payload.bin file to SPI flash memory.

once the upload of the file is  completed, we can use the  `spi` command to change the boot media, followed by `boot` command.


```
[MPFS-TERM] Opened /dev/ttyUSB0 at 115200 baud. (Press Ctrl+C to exit)
[MPFS-TERM] Type 'serialboot' on prompt to trigger download.

>>help

Supported commands:
BOOT RESET HELP VERSION UPTIME DEBUG MEMTEST QSPI EMMC MMC SDCARD PAYLOAD SPI SNVM RESUME SERIALBOOT FLASHID FLASHBOOT 

[2849.361699] loop 448149533 took 4286259205 ticks (max 4286259205 ticks)
>> FLASHID

[SPI_FLASH] JEDEC ID:
 Manufacturer : 0x20 (Micron expected: 0x20)
 Device Type  : 0xBA
 Capacity ID  : 0x21

>>serialboot

[2842.218024] Command SERIALBOOT may cause problems post boot.



Ready for SFL transfer. Connect host script...
sL5DdSMmkekro

[MPFS-TERM] Magic trigger matched! Replying with handshake ACK...
[MPFS-TERM] Requesting SPI Flash erase/prep (67328 bytes) at Offset 0x00020000...
[MPFS-TERM] Uploading tools/hss-payload-generator/payload.bin (67328 bytes) -> 0x00020000...
|====================| 100.0% (67328/67328 B)
[MPFS-TERM] Transfer Complete (9.6 KB/s).
[MPFS-TERM] Sending DONE command...
[MPFS-TERM] Terminal active. Continuing console output...


[FLASHWRITE] SPI Flash Direct Upload Complete!
 Target Flash Addr  : 0x00020000
 Total Written Size : 67328 bytes

[2849.361699] loop 448149533 took 4286259205 ticks (max 4286259205 ticks)

Supported commands:
BOOT RESET HELP VERSION UPTIME DEBUG MEMTEST QSPI EMMC MMC SDCARD PAYLOAD SPI SNVM RESUME SERIALBOOT FLASHID FLASHBOOT 
>> spi

[7.651236] Selecting SPI Flash as boot source ...
>> boot

[9.612612] Initializing Boot Image ...
[9.617672] Preparing to copy from SPI Flash +0x20000 ...
[9.624451] Attempting to read image header (1632 bytes) ...
[9.632437] Copying 67328 bytes to 0x103fc00000
[9.677955] SPI: Boot Image registered ...
[9.683406] Boot image set name: "PolarFire-SoC-HSS::pfsoc_bios"
[9.690853] boot_service(u54_1):: BOOT_FLAG_SKIP_OPENSBI found
[9.698396] boot_service(u54_1) :: [Init] -> [SetupPMP]
[9.705271] boot_service(u54_2):: BOOT_FLAG_SKIP_OPENSBI found
[9.712814] boot_service(u54_2) :: [Init] -> [SetupPMP]
[9.719688] boot_service(u54_3):: BOOT_FLAG_SKIP_OPENSBI found
[9.727231] boot_service(u54_3) :: [Init] -> [SetupPMP]
[9.734106] boot_service(u54_4):: BOOT_FLAG_SKIP_OPENSBI found
[9.741649] boot_service(u54_4) :: [Init] -> [SetupPMP]
>> [9.748810] boot_service(u54_1) :: [SetupPMP] -> [SetupPMPComplete]
[9.756830] boot_service(u54_2) :: [SetupPMP] -> [SetupPMPComplete]
[9.764850] boot_service(u54_3) :: [SetupPMP] -> [SetupPMPComplete]
[9.772871] boot_service(u54_4) :: [SetupPMP] -> [SetupPMPComplete]
[9.780891] u54 State Change:  [Booting] [Booting] [Booting] [Booting]
[9.790248] boot_service(u54_1) :: [SetupPMPComplete] -> [ZeroInit]
[9.798268] boot_service(u54_2) :: [SetupPMPComplete] -> [ZeroInit]
[9.806289] boot_service(u54_3) :: [SetupPMPComplete] -> [ZeroInit]
[9.814309] boot_service(u54_4) :: [SetupPMPComplete] -> [ZeroInit]
[9.823058] boot_service(u54_1) :: [ZeroInit] -> [Download]
[9.830315] boot_service(u54_2) :: [ZeroInit] -> [Download]
[9.837571] boot_service(u54_3) :: [ZeroInit] -> [Download]
[9.844828] boot_service(u54_4) :: [ZeroInit] -> [Download]
[9.852084] boot_service(u54_1)::Processing boot image: "baremetal+pfsoc_bios"
[9.860869] boot_service(u54_2) :: [Download] -> [Complete]
[9.868125] boot_service(u54_3) :: [Download] -> [Complete]
[9.875382] boot_service(u54_4) :: [Download] -> [Complete]
[9.884409] boot_service(u54_1) :: [Download] -> [OpenSBIInit]
[9.893176] boot_service(u54_1) :: [OpenSBIInit] -> [Wait]
[9.912775] u54 State Change:  [Booting] [Running] [Running] [Running]
[9.922410] boot_service(u54_1) :: [Wait] -> [Complete]
[9.933164] u54 State Change:  [Running] [Running] [Running] [Running]
[9.942521] boot_service(u54_1) :: [Complete] -> [Idle]
[9.949396] boot_service(u54_2) :: [Complete] -> [Idle]
[9.956270] boot_service(u54_3) :: [Complete] -> [Idle]
[9.963145] boot_service(u54_4) :: [Complete] -> [Idle]
[17.67100] loop 2500000 took 3722 ticks (max 394341623 ticks)



```


30th October 2026


# Hart Software Services

This is the Hart Software Services (HSS) code.

On PolarFire SoC, this is comprised of two portions:

- A superloop monitor running on the E51 minion processor, which receives requests from the
   individual U54 application processors to perform certain services on their behalf;

- A Machine-Mode software interrupt trap handler, which allows the E51 to send messages to the U54s,
   and request them to perform certain functions for it related to rebooting a U54.

The HSS performs boot and system monitoring functions for PolarFire SoC.

The HSS is compressed (DEFLATE) and stored in eNVM.  On power-up, a small decompressor decompressor wrapper inflates the HSS from eNVM flash to L2-Scratchpad memory and starts the HSS.

## Contributions

Pull requests to this repository must include a per-commit sign-off made by a contributor stating that they agree to the terms published at https://developercertificate.org/ for that particular contribution.

## Source

Source code is found under the `hart-software-services` folder.

    hart-software-services
    ├── application (main function, crt startup, init function)
    ├── baremetal
    │   ├── drivers (local modules)
    │   └── polarfire-soc-bare-metal-library (subtree)
    ├── boards
    │   ├── mpfs-icicle-kit-es (Microchip Icicle Kit)
    │   ├── aries-m100pfsevp (Aries M100PFEVPS Kit)
    │   └── custom-board-design (template for new boards)
    ├── envm-wrapper (helper routines to inflate the HSS to L2-Scratchpad)
    ├── include
    ├── init (system initialization)
    ├── modules
    │   ├── compression
    │   ├── crypto
    │   ├── debug (helper routines for function profiling)
    │   ├── misc (miscellaneous routines)
    │   └── ssmb (secure software message bus)
    │        └── ipi
    ├── services (software service state machines)
    │   ├── beu
    │   ├── boot
    │   ├── crypto
    │   ├── ddr
    │   ├── goto
    │   ├── ipi_poll
    │   ├── mmc
    │   ├── opensbi
    │   ├── powermode
    │   ├── qspi
    │   ├── scrub
    │   ├── sgdma
    │   ├── spi
    │   ├── tinycli
    │   ├── uart
    │   ├── usbdmsc
    │   │    └── flash_drive
    │   ├── wdog
    │   └── ymodem
    └── thirdparty
        ├── Kconfiglib (a Kconfig implementation in Python)
        ├── libecc (library for elliptic curves based cryptography (ECC))
        ├── miniz (fast lossless compression library)
        └── opensbi (RISC-V OpenSBI)

## Building

The build is configured using the Kconfig system of selecting build options.

The Hart Software Services includes the Kconfig parsing infrastructure directly as a third-party tool invoked by the build system.

Both Linux and Windows are supported by Kconfiglib.

The HSS currently support PolarFire SoC-based icicle kit (mpfs-icicle-kit-es) as a board build target.

### Compiler Toolchain

The HSS now relies on xPack GNU RISC-V Embedded GCC, a binary distribution of GNU RISC-V Embedded GCC toolchain.

SoftConsole v2021.3 or later can still be used to build the HSS, but you'll need to set `CONFIG_CC_USE_SOFTCONSOLE` in your toplevel `.config` file. Note that this support for the older SoftConsole GCC toolchain is considered deprecated and will be removed from a future release of the HSS.

You can get xPack from https://xpack-dev-tools.github.io/riscv-none-elf-gcc-xpack/

Untar the release somewhere convenient (e.g. /opt) and add it to your PATH:

    $ export PATH=/opt/xpack-riscv-none-elf-gcc-15.2.0-1/bin:$PATH

### Building on Linux

The HSS needs tkinter installed for the KConfiglib guiconfig tool. To install this on Ubuntu/Debian:

    $ sudo apt install python3-tk

First, ensure that the environment variable `$SC_INSTALL_DIR` is set to the location of SoftConsole on your system. For example, assuming that SoftConsole is installed at  `/home/$USER/Microchip/SoftConsole-v2022.2`:

    $ export SC_INSTALL_DIR=/home/$USER/Microchip/SoftConsole-v2022.2

For building on Linux from the command line, configure the path appropriately. For example, the following will add the SoftConsole provided Python and RISC-V compiler toolchain to the path:

    $ export PATH=$PATH:$SC_INSTALL_DIR/python3/bin:$SC_INSTALL_DIR/riscv-unknown-elf-gcc/bin

Next, set the environment variable `$FPGENPROG` to the location of the fpgenprog tool installed by Libero -- this is used to program the eNVM ('make program') and also to generate hex files suitable for programming by Libero, or inclusion in the fabric design with Libero. For example, assuming that Libero is installed at `/usr/local/microchip/Libero_SoC_v2025.2/`:

    $ export FPGENPROG=/usr/local/microchip/Libero_SoC_v2025.2/Libero_SoC/Designer/bin64/fpgenprog

Note: Older versions of Libero are (typically) installed at `/usr/local/microsemi/<Libero_Version>`.

To make these persistent upon logging in, these three environment variables - `$SC_INSTALL_DIR`, `$FPGENPROG` and the additions to `$PATH` - should be updated in your user shell initialization file (for example, `/home/$USER/.bashrc` for those running the bash shell).

You can enter an interactive Kconfiglib configuration selection by running `make BOARD=mpfs-icicle-kit-es config`. This will generate a `.config` file (which is used to configure the Make build system) and a `config.h` header file (which is used to configure the source code):

    $ make BOARD=mpfs-icicle-kit-es config

Alternatively, copy the default config for your board. For example:

    $ cp boards/mpfs-icicle-kit-es/def_config .config

or just use the defconfig target to do the copy:

    $ make BOARD=mpfs-icicle-kit-es defconfig

Once configured, to build, run `make`:

    $ make BOARD=mpfs-icicle-kit-es

In the `build` subdirectory, the standard build will create `hss-envm.elf` and various binary formats (`hss-envm.hex` and `hss-envm.bin`).  Also generated are `output-envm.map`, which is a mapfile for the build, and  `hss-envm.sym`, which is a list of symbols.  (The name `build` is required by SoftConsole for programming purposes. In earlier releases, this was called `Default`.)

Once built, program the HSS to the board:

    $ make program

For programming HSS to PolarFire SoC Video Kit:

    $ make program BOARD=mpfs-video-kit

A variety of alternative build options can be seen by running `make help`:

    $ make help

Verbose builds (which show each individual command) are possible by adding V=1 to the end of the make command. For example:

    $ make V=1

### Building on Windows

As with Linux, the HSS now by default relies on the xPack GNU RISC-V Embedded GCC toolchain. Please download it and add it to your path. (In this documentation, we will assume it has been downloaded and decompressed to `C:\xpack-riscv-none-elf-gcc-15.2.0-1\bin`).

The HSS also needs SoftConsole v2021.3 or later to build on Windows.

For more detailed build instructions, particular with regards to using SoftConsole on Windows, see https://mi-v-ecosystem.github.io/redirects/software-development_polarfire-soc-software-tool-flow

First, ensure that the `%SC_INSTALL_DIR%` environment variable is correctly set to the location of SoftConsole.  For example, if SoftConsole is installed in `C:\Microchip\SoftConsole-v2022.2-RISC-V-747`:

    C:\> set SC_INSTALL_DIR=c:\Microchip\SoftConsole-v2022.2-RISC-V-747

For building on Windows from the command line, you must configure the path appropriately to add (Micro) Python, GNU build tools, and the RISC-V compiler toolchain. For example:

    C:\> path C:\xpack-riscv-none-elf-gcc-15.2.0-1\bin;%SC_INSTALL_DIR%\build_tools\bin;%SC_INSTALL_DIR%\python3;%PATH%

Ensure the `%SC_INSTALL_DIR%` variable correctly matches your system.

Next, set the environment variable `%FPGENPROG%` to the location of the fpgenprog tool installed by Libero -- this is used to program the eNVM ('make program') and also to generate hex files suitable for programming by Libero, or inclusion in the fabric design with Libero. For example, assuming that Libero is installed at `C:\Microchip\Libero_SoC_v2022.2\Designer\bin64\fpgenprog.exe`:

    C:\> set FPGENPROG=C:\Microchip\Libero_SoC_v2025.2\Libero_SoC\Designer\bin64\fpgenprog.exe

To make these persistent, set these environment variables via the Control Panel or via the Settings App.

#### MicroPython

A MicroPython environment is provided bundled as part of SoftConsole. This includes enough Python support to enable building the HSS. However, it does not support targets `make menuconfig` and `make guiconfig`, as it is missing `windows-curses` (the console library needed for Kconfiglib) and `python3-tk` (the GUI library needed for Kconfiglib).  THese targets are only needed for editing the `.config` configuration.

To work around this, you will need to use an external Python install. This can be done either via the Microsoft Store (preferrred) or manually.

#### Python from Microsoft Store

To do install Python from the Microsoft Store, in a `CMD.EXE` console, type:

    C:\> python3

This should take you to the Microsoft Store to install Python3... Click install.

Make sure that the MicroPython tools are not in your path (e.g. do not have `%SC_INSTALL_DIR%\python3` in your path), and setup your environment as follows:

    C:\> path C:\xpack-riscv-none-elf-gcc-15.2.0-1\bin;%SC_INSTALL_DIR%\build_tools\bin;%PATH%

Back to the console (no need to restart it if installing Python from the Microsoft Store), and type:

    C:\work\hart-software-services> pip install windows-curses

At this point, windows-curses is now installed. Now `menuconfig` and `guiconfig` work as expected.

#### Python from `python.org`

If you are unable to install Python from the Microsoft Store (due to IT group policy or other enterprise restrictions), you can download it manually and install it from https://www.python.org/downloads/windows/

When you install, please be sure that `Add python.exe to PATH` is selected. Also, ensure that `pip` is being installed (click `Customize installation` and make sure `pip` is selected).  Then click 'Install Now'.  Once the install has completed, close and restart your command prompt. And then type:

    C:\work\hart-software-services> pip install windows-curses

Unlike installs from the Microsoft Store, the `python.org` installer does not add `python3.exe` as an alias of `python.exe`. As a result, if installing Python manually on Windows, you will need to invoke the HSS build commands with `PYTHON=python` as a suffix. For example:

    C>\work\hart-software-services> make menuconfig PYTHON=python
    C>\work\hart-software-services> make guiconfig PYTHON=python

### Debug

The `modules/debug/` subdirectory contains code to enable a number of debug features, including:

 * Logging all state machine transitions to the serial console (MMUART0);
 * Periodic logging of super loop timings to the serial console;
 * Logging of IPI statistics to the serial console;
 * Logging all IPI messages for MSC traces;
 * Function profiling.

### Function Profiling

Function profiling in the HSS can be enabled using the `CONFIG_DEBUG_PROFILING_SUPPORT` Kconfig option.

Function profiling allows capturing of the time spent in each C function (through the use of `__cyg_profile_func_enter` and `__cyg_profile_func_exit`. This information can be logged to the serial console through calling the `HSS_Profile_DumpAll()` function at an appropriate time, depending on what is being debugged, and by using the `debug profile` CLI command.

For more information, please refer to the [HSS Function Profiling documentation](https://mi-v-ecosystem.github.io/redirects/hart-software-services-profiling).

### Scrubbing for ECCs and Non-Coherent Memory.

The HSS includes a scrubbing service (running on the E51) to scrub ECC protected memories such as DDR, L2 Scratchpad, LIM for correctable single-bit errors in an effort to correct them before they propagate to uncorrectable double-bit errors. Note that by default, mpfs-linux is configured such that DDR accesses to/from peripherals such as PCIe, eNVM and USB are directed to uncached memory addresses; with cache synchronisation performed explicitly by the Linux kerne. Enabling scrubbing can result in interfering with this default mpfs-linux behavior, and so it is important to configure mpfs-linux to only used coherent memory when using scrubbing.

As a consequence, scrubbing is currently off by default in provided `def_config` files.

    # CONFIG_SERVICE_SCRUB is not set
 
