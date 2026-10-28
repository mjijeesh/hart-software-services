#!/usr/bin/env python3

import sys
import os
import time
import signal
import serial
import threading
import argparse

# Project Branding / Log Prefix --------------------------------------------------------------------
LOG_PREFIX = "[MPFS-TERM]"

# Platform Console Setup ---------------------------------------------------------------------------

if sys.platform == "win32":
    import ctypes
    import msvcrt

    try:
        ctypes.windll.winmm.timeBeginPeriod(1)  # Force 1 ms timer precision on Windows
    except Exception:
        pass

    class Console:
        def configure(self):
            kernel32 = ctypes.windll.kernel32
            kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)

        def unconfigure(self):
            pass

        def getkey(self):
            return msvcrt.getch()

else:
    import termios
    class Console:
        def __init__(self):
            self.fd = sys.stdin.fileno()
            self.default_settings = termios.tcgetattr(self.fd)

        def configure(self):
            settings = termios.tcgetattr(self.fd)
            settings[3] = settings[3] & ~termios.ICANON & ~termios.ECHO
            settings[6][termios.VMIN] = 1
            settings[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, settings)

        def unconfigure(self):
            termios.tcsetattr(self.fd, termios.TCSAFLUSH, self.default_settings)

        def getkey(self):
            return os.read(self.fd, 1)

# SFL Protocol Constants (Matching boot.c) ---------------------------------------------------------

sfl_magic_req = b"sL5DdSMmkekro\n"
sfl_magic_ack = b"z6IHG7cYDID6o\n"

# Protocol Commands
sfl_cmd_abort = 0x00
sfl_cmd_load  = 0x01
sfl_cmd_jump  = 0x02
sfl_cmd_flash = 0x03
sfl_cmd_done  = 0x04

# Protocol Replies
sfl_ack_success  = b"K"
sfl_ack_crcerror = b"C"
sfl_ack_unknown  = b"U"
sfl_ack_error    = b"E"

# CRC16 Implementation -----------------------------------------------------------------------------

crc16_table = [
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
]

def crc16(data: bytes) -> int:
    crc = 0
    for d in data:
        crc = crc16_table[((crc >> 8) ^ d) & 0xff] ^ (crc << 8)
    return crc & 0xffff

class SFLFrame:
    def __init__(self, cmd: int, payload: bytes):
        self.cmd = cmd
        self.payload = payload

    def encode(self) -> bytes:
        payload_len = len(self.payload)
        crc = crc16(bytes([self.cmd]) + self.payload)
        
        packet = bytes([payload_len])
        packet += crc.to_bytes(2, "big")
        packet += bytes([self.cmd])
        packet += self.payload
        return packet

# Loader Engine ------------------------------------------------------------------------------------

class MPFSTerminal:
    def __init__(self, port, baudrate, kernel, kernel_adr="0x80000000", flash_mode=False, no_boot=False, auto_serialboot=False):
        self.port_name = port
        self.baudrate = baudrate
        self.kernel_path = kernel
        self.flash_mode = flash_mode
        self.no_boot = no_boot
        self.auto_serialboot = auto_serialboot

        # AUTO-FIX: Switch default RAM address (0x80000000) to default SPI Flash offset (0x00020000) when --flash is enabled
        if self.flash_mode and (kernel_adr == "0x80000000" or kernel_adr == 0x80000000):
            kernel_adr = "0x00020000"

        self.kernel_adr = int(kernel_adr, 16) if isinstance(kernel_adr, str) else kernel_adr

        self.ser = None
        self.console = Console()
        self.running = False
        self.uploading = False
        self.magic_buffer = bytes(len(sfl_magic_req))

        self.chunk_data_size = 60 if sys.platform == "win32" else 251
        self.inter_frame_delay = 0.001 if sys.platform == "win32" else 0.0

    def open(self):
        self.ser = serial.Serial(self.port_name, self.baudrate, timeout=0.2, write_timeout=1.0)
        self.running = True

    def write_frame(self, frame: SFLFrame, timeout=10.0) -> bool:
        data = frame.encode()
        self.ser.write(data)
        self.ser.flush()

        start = time.time()
        while time.time() - start < timeout:
            reply = self.ser.read(1)
            if reply == sfl_ack_success:
                return True
            elif reply == sfl_ack_crcerror:
                print(f"\n{LOG_PREFIX} CRC Error, retrying frame...")
                return False
            elif reply == sfl_ack_error:
                print(f"\n{LOG_PREFIX} Device reported frame error.")
                return False
            elif reply == sfl_ack_unknown:
                print(f"\n{LOG_PREFIX} Device reported unknown command.")
                return False
        return False

    def execute_upload(self):
        self.uploading = True
        print(f"\n{LOG_PREFIX} Magic trigger matched! Replying with handshake ACK...")

        self.ser.write(sfl_magic_ack)
        self.ser.flush()

        time.sleep(0.05)
        while self.ser.in_waiting:
            self.ser.read(self.ser.in_waiting)

        file_size = os.path.getsize(self.kernel_path)

        try:
            if self.flash_mode:
                print(f"{LOG_PREFIX} Requesting SPI Flash erase/prep ({file_size} bytes) at Offset 0x{self.kernel_adr:08X}...")
                flash_payload = file_size.to_bytes(4, "big") + self.kernel_adr.to_bytes(4, "big")
                flash_frame = SFLFrame(sfl_cmd_flash, flash_payload)

                if not self.write_frame(flash_frame, timeout=30.0):
                    print(f"{LOG_PREFIX} [ERROR] Flash initialization failed.")
                    self.uploading = False
                    return

            print(f"{LOG_PREFIX} Uploading {self.kernel_path} ({file_size} bytes) -> 0x{self.kernel_adr:08X}...")
            
            with open(self.kernel_path, "rb") as f:
                current_addr = self.kernel_adr
                bytes_sent = 0
                start_time = time.time()

                while bytes_sent < file_size:
                    chunk = f.read(self.chunk_data_size)
                    payload = current_addr.to_bytes(4, "big") + chunk
                    load_frame = SFLFrame(sfl_cmd_load, payload)

                    retries = 5
                    success = False
                    while retries > 0:
                        if self.write_frame(load_frame, timeout=2.0):
                            success = True
                            break
                        retries -= 1
                        time.sleep(0.01)

                    if not success:
                        print(f"\n{LOG_PREFIX} [ERROR] Transmission aborted due to persistent errors.")
                        self.uploading = False
                        return

                    bytes_sent += len(chunk)
                    current_addr += len(chunk)

                    if self.inter_frame_delay > 0:
                        time.sleep(self.inter_frame_delay)

                    progress = (bytes_sent / file_size) * 100
                    sys.stdout.write(f"\r|{'=' * int(progress // 5):<20}| {progress:.1f}% ({bytes_sent}/{file_size} B)")
                    sys.stdout.flush()

            elapsed = time.time() - start_time
            print(f"\n{LOG_PREFIX} Transfer Complete ({file_size / (elapsed * 1024):.1f} KB/s).")

            if self.flash_mode or self.no_boot:
                cmd = sfl_cmd_done if self.flash_mode else sfl_cmd_abort
                print(f"{LOG_PREFIX} Sending {'DONE' if self.flash_mode else 'ABORT'} command...")
                self.write_frame(SFLFrame(cmd, self.kernel_adr.to_bytes(4, "big")))
            else:
                print(f"{LOG_PREFIX} Sending JUMP Command to boot application...")
                self.write_frame(SFLFrame(sfl_cmd_jump, self.kernel_adr.to_bytes(4, "big")))

        except Exception as e:
            print(f"\n{LOG_PREFIX} [ERROR] Exception during serialboot: {e}")

        self.uploading = False
        print(f"{LOG_PREFIX} Terminal active. Continuing console output...\n")

    def reader_thread(self):
        while self.running:
            if self.uploading:
                time.sleep(0.05)
                continue

            try:
                c = self.ser.read(1)
                if c:
                    sys.stdout.buffer.write(c)
                    sys.stdout.flush()

                    self.magic_buffer = self.magic_buffer[1:] + c
                    if self.magic_buffer == sfl_magic_req:
                        if not self.kernel_path:
                            print(f"\n{LOG_PREFIX} Board requested serialboot, but no kernel file was specified (--kernel <file>). Ignoring upload request.")
                        elif not os.path.exists(self.kernel_path):
                            print(f"\n{LOG_PREFIX} [ERROR] Kernel file '{self.kernel_path}' not found. Ignoring upload request.")
                        else:
                            self.execute_upload()

            except serial.SerialException:
                print(f"\n{LOG_PREFIX} Serial port disconnected.")
                self.running = False
                break

    def writer_thread(self):
        if self.auto_serialboot:
            if not self.kernel_path or not os.path.exists(self.kernel_path):
                print(f"\n{LOG_PREFIX} [WARNING] --serial-boot flag ignored because no valid --kernel file was specified.")
            else:
                time.sleep(0.3)
                self.ser.write(b"serialboot\n")

        while self.running:
            if self.uploading:
                time.sleep(0.05)
                continue

            try:
                key = self.console.getkey()
                if key == b"\x03":  # Ctrl+C
                    self.running = False
                    break
                elif key == b"\r" or key == b"\n":
                    self.ser.write(b"\n")
                else:
                    self.ser.write(key)
            except Exception:
                break

    def start(self):
        self.open()
        self.console.configure()

        t_read = threading.Thread(target=self.reader_thread, daemon=True)
        t_write = threading.Thread(target=self.writer_thread, daemon=True)
        t_read.start()
        t_write.start()

        print(f"{LOG_PREFIX} Opened {self.port_name} at {self.baudrate} baud. (Press Ctrl+C to exit)")
        print(f"{LOG_PREFIX} Type 'serialboot' on prompt to trigger download.\n")

        while self.running:
            time.sleep(0.2)

        self.console.unconfigure()
        self.ser.close()

# CLI Parser ---------------------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="MPFS Discovery Kit Serialboot Terminal")
    parser.add_argument("port", help="Serial port name (e.g. COM6 or /dev/ttyUSB0)")
    parser.add_argument("--speed", default=115200, type=int, help="Serial baudrate")
    parser.add_argument("--kernel", default=None, help="Binary file path")
    parser.add_argument("--kernel-adr", default="0x80000000", help="Target memory address or Flash Offset (defaults: RAM 0x80000000, Flash 0x00020000)")
    parser.add_argument("--flash", action="store_true", help="Program SPI Flash instead of DDR RAM")
    parser.add_argument("--no-boot", action="store_true", help="Download to RAM without executing jump")
    parser.add_argument("--serial-boot", action="store_true", help="Automatically trigger 'serialboot' command on startup")

    args = parser.parse_args()

    # File Format Validation
    if args.kernel:
        ext = os.path.splitext(args.kernel)[1].lower()
        if args.flash and ext != ".fbi":
            print(f"{LOG_PREFIX} [ERROR] SPI Flash mode (--flash) requires a '.fbi' file. Received: '{ext}'")
            sys.exit(1)
        elif not args.flash and ext != ".bin":
            print(f"{LOG_PREFIX} [ERROR] Regular serialboot mode requires a '.bin' file. Received: '{ext}'")
            sys.exit(1)

    term = MPFSTerminal(
        port=args.port,
        baudrate=args.speed,
        kernel=args.kernel,
        kernel_adr=args.kernel_adr,
        flash_mode=args.flash,
        no_boot=args.no_boot,
        auto_serialboot=args.serial_boot
    )
    term.start()

if __name__ == "__main__":
    main()