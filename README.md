# SPI Flash Support

use the defconfig_minimal_serialboot fiel  for mpfs-icle kit


$make BOARD=mpfs-icicle-kit DEFCONFIG=boards/mpfs-icicle-kit/def_config_minimal_serialboot

or 

$cp boards/mpfs-icicle-kit/def_config_minimal_serialboot .

alternatevely you can configure  using the menuconfig and enable serialboot SFL via CLI from the default ( defconfig)


$make BOARD=mpfs-icicle-kit menuconfig

$ make BOARD=mpfs-icicle-kit clean all


once the building is done, program the hex fiel into env using belw command

$ make BOARD=mpfs-icicle-kit program



Added the SPI flash Support via the System Controller SPI.
Now we can program the micron 1gB memory conencted to the System controlelr SPI with the same serialboot cmmand.

```
$python3 mpfs-terminal.py  /dev/ttyUSB0 --kernel build/ddr/pfsoc_bios.fbi --flash
```

use the command option `--flash` so taht serialboot can understand it is for spi flash. Also provide an 
`.fbi` file instead of `.bin` file 

use the `serialboot command from the hSS to start the downalod 

```
(litex-env) jijeesh@jijeesh-Latitude-5300:~/git/mpfs-bootloader/target-app2$ python3 mpfs-terminal.py  /dev/ttyUSB0 --kernel build/ddr/pfsoc_bios.bin  --no-boot
[MPFS-TERM] Opened /dev/ttyUSB0 at 115200 baud. (Press Ctrl+C to exit)
[MPFS-TERM] Type 'serialboot' on prompt to trigger download.

help

Supported commands:
BOOT RESET HELP VERSION UPTIME DEBUG MEMTEST QSPI EMMC MMC SDCARD PAYLOAD SPI SNVM RESUME SERIALBOOT FLASH_WRITE FLASH_ID 
>> FL[48.190253] loop 7500000 took 3588 ticks (max 606533091 ticks)
ASH

[49.685835] Unknown command >>FLASH<< (1 tokens)

>> flash_id


[SPI_FLASH] JEDEC ID:
 Manufacturer : 0x20 (Micron expected: 0x20)
 Device Type  : 0xBA
 Capacity ID  : 0x21

>

(litex-env) jijeesh@jijeesh-Latitude-5300:~/git/mpfs-bootloader/target-app2$ python3 mpfs-terminal.py  /dev/ttyUSB0 --kernel build/ddr/pfsoc_bios.fbi  --flash
[MPFS-TERM] Opened /dev/ttyUSB0 at 115200 baud. (Press Ctrl+C to exit)
[MPFS-TERM] Type 'serialboot' on prompt to trigger download.

>> help

Supported commands:
BOOT RESET HELP VERSION UPTIME DEBUG MEMTEST QSPI EMMC MMC SDCARD PAYLOAD SPI SNVM RESUME SERIALBOOT FLASH_WRITE FLASH_ID 
>> serialboot


Ready for SFL transfer. Connect host script...
sL5DdSMmkekro

[MPFS-TERM] Magic trigger matched! Replying with handshake ACK...
[MPFS-TERM] Requesting SPI Flash erase/prep (32584 bytes) at Offset 0x00020000...
[MPFS-TERM] Uploading build/ddr/pfsoc_bios.fbi (32584 bytes) -> 0x00020000...
|====================| 100.0% (32584/32584 B)
[MPFS-TERM] Transfer Complete (9.6 KB/s).
[MPFS-TERM] Sending DONE command...
[MPFS-TERM] Terminal active. Continuing console output...


[FLASHWRITE] SPI Flash Direct Upload Complete!
 Target Flash Addr  : 0x00020000
 Total Written Size : 32584 bytes

[361.994136] loop 57637434 took 2096632077 ticks (max 2096632077 ticks)


```

Now for booting fro mthe flash, use the `flashboot` command

```
Supported commands:
BOOT RESET HELP VERSION UPTIME DEBUG MEMTEST QSPI EMMC MMC SDCARD PAYLOAD SPI SNVM RESUME SERIALBOOT FLASH_WRITE FLASH_ID FLASHBOOT 
>>flashboot


[FLASHBOOT] Reading FBI header from SPI Flash offset 0x00020000...
[FLASHBOOT] FBI [Length, CRC32] detected:
  Payload Size : 32576 bytes (0x7F40)
  Payload CRC  : 0xE8C25A16
  Target DDR   : 0x0000000080000000
[FLASHBOOT] Copying 32576 bytes from SPI 0x00020008 to DDR 0x0000000080000000...
[FLASHBOOT] DDR First 4 Words: 0x00000717 0x20070713 0x30571073 0x305027F3
[FLASHBOOT] Launching application on U54 harts...

[SERIALBOOT] Synchronizing caches and dispatching all U54 harts to 0x0000000080000000...
[SERIALBOOT] All U54 harts launched. Application active on UART1 (/dev/ttyUSB1).
[SERIALBOOT] Returning to HSS TinyCLI.

[59.987425] u54 State Change:  [Running] [Running] [Running] [Running]


```

Note that before every boot command, the board need to be reset, we cannot boot into a u54 if it is already running.

use either the `RESET` command or a hardware reset before starting boot from spi flash.


29th October 2026



# SERIALBOOT

Added the serialboot service.
use the serialboot command to download a binary file to ddr and execute from ddr location 0x80000000.
The target-app used for the mpfs-bootloader can be used here as well.
use the mpfs-terminal.py python app for the terminal app.

start the terminal with the application file via the --kernel option 

```
$python3 mpfs-terminal.py  /dev/ttyUSB0 --kernel build/ddr/pfsoc_bios.bin

```
The mpfs-terminal.py can be used as a regular termianl without the --kernel option as well.



```
..
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
BOOT RESET HELP VERSION UPTIME DEBUG MEMTEST QSPI EMMC MMC SDCARD PAYLOAD SPI SNVM RESUME SERIALBOOT 
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
 
