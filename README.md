# Windows 11 ARM64 on the Samsung Galaxy S23 (`dm1q` / `SM-S911B`)

Native **Windows 11 Pro 24H2 ARM64 (`10.0.26100`)** on the **Samsung Galaxy S23** (Snapdragon 8 Gen 2 for Galaxy, `SM8550-AC`, "Kailua").
This repository contains the ARM64 kernel drivers, UEFI DXE patches, ACPI SSDTs, kernel research probes and PC-side tooling used to bring the phone up.

> [!CAUTION]
> **Hardware & OS safety rules — read before touching a device**
> 1. **Never write to `sdb` or `sde`** — they are power-on write-protected (`ro=1`). Windows lives on `sda43` (`/dev/block/by-name/win`), the UEFI boot image on `sda25`.
> 2. **Keep `SanPolicy = 4`** (`HKLM\SYSTEM\ControlSet001\Services\partmgr\Parameters`) on the phone so the Android UFS LUNs stay offline in Windows.
> 3. **Keep `testsigning on`** (`bcdedit`). All `s23*.sys` drivers are test-signed (`CN=S23TouchTest2020`); the *Test Mode* watermark is expected.
> 4. **Never call `Set-Date`** in live scripts on the phone. The UEFI RTC starts at `2024-04-01`; jumping the clock forward triggers the display idle-off timer (black screen).
> 5. **Avoid WHEA `0x124` SError aborts** — never touch TZ-reserved QUP/TLMM pins (`GPIO36..39`, `50..51`, `56..59`, `64..65`; `QUP1_SE2`, `QUP2_SE0`, `QUP2_SE2`), non-APPS SMEM partitions, KGSL `0x03D00000` while `GPUCC_GX_GDSCR` bit 31 is 0, GMU `0x03D6A000` while `GPUCC_CX_GDSCR` is collapsed, or PIL DDR carve-outs before checking `IMEM ImageLoadInfo (0x146AA94C)`.

---

## Status

| Area | Driver / Path | Status | Highlights |
|---|---|---|---|
| **Display & GPU** (Adreno 740) | [`drivers/s23gpu/`](drivers/s23gpu/) | ✅ Working | WDDM 2.0 display + 3D KMD, `1080×2340 @ 120 Hz` with high-resolution VSync, native brightness & Night Light, Mesa Freedreno D3D11 (`d3d10umd`) and **OpenGL 4.6** WGL ICD (ARM64 + x64), live **GPU utilization** and **TSENS GPU temperature** in Task Manager. |
| **Battery & fast charging** | [`drivers/s23batt/`](drivers/s23batt/) | ✅ Working | Battery miniclass driver for the Maxim **MAX77705** fuel gauge (`@0x36`) and charger (`@0x69`): 3.0 A input limit, 2.5 A CC fast charge, 4.4 V float, watchdog handling, abnormal-off recovery, calibrated battery temperature. |
| **Touchscreen** | [`drivers/s23touch/`](drivers/s23touch/) | ✅ Deployed | HID multitouch minidriver for the STM **FTS2BA61Y** on QUP1 SE4 GENI SPI (`0x00A90000`, IRQ `gpio25`). |
| **Physical keys** | [`drivers/s23keys/`](drivers/s23keys/) | ✅ Deployed | HID minidriver for Power / Volume Up / Volume Down, read-only via the SPMI PMIC arbiter observer. |
| **Bluetooth 5.3 + BLE** | [`drivers/s23bthx/`](drivers/s23bthx/) | ✅ Working | `BthMini` extensible-transport lower filter for the **QCA6490** on `UART14` (QUP2 SE6, `3.2 Mbaud`, IBS), Classic + BLE, A2DP-sized ACL buffers. |
| **USB-C PC link** | [`s23link.py`](s23link.py) | ✅ Working | WinUSB remote shell and file transfer between PC and phone (`ping`, `ver`, `exec`, `put`, `get`, `reboot`). |
| **Audio** (CS35L43 amps, CS40L26A haptics) | [`audio_sensors_research/`](audio_sensors_research/) | 🚧 In active development | Amps and haptics probed live, ACPI SSDT and architecture doc in progress. |
| **Wi-Fi 6E** (QCA6490 PCIe, `PCI\VEN_17CB&DEV_1103`) | [`wifi_research/`](wifi_research/), [`drivers/s23pcieprobe/`](drivers/s23pcieprobe/) | ✅ Working | Live with internet: UEFI PCIe0 bring-up + `qcwlan8280.sys` + the S23 board data file `bdwlan.elf`. `wifi_research/` holds the bring-up and install docs (UEFI DXE, ACPI SSDT, package builder); `s23pcieprobe` is the diagnostic probe. |
| **5G modem / PIL / CPU perf** | [`modem_5g_research/`](modem_5g_research/) ([index](modem_5g_research/README.md)), [`drivers/s23pilprobe/`](drivers/s23pilprobe/), [`drivers/s23perf/`](drivers/s23perf/) | 🔬 Research | Autonomous bring-up blueprint, SMEM/IMEM/PIL telemetry, inert subsystem SSDTs, EPSS CPUfreq and TSENS probes. |
| **Native USB-C** (dual-role, PD 3.0, DP Alt-Mode) | [`usb_research/`](usb_research/) | 🔬 Research | Specification for native USB-C on Windows: MAX77705 OPCODE mailbox, UCM/URS driver split, DWC3/eUSB2/QMP PHY transitions, DP Alt-Mode via WDDM. |
| **UEFI display fix** | [`display_research/`](display_research/) | ✅ Working | `S23DisplayFixDxe` (DISP_CC gating, DSI0 wake, tear-counter heal) flashed in the UEFI image. |

Driver versions are tracked in each driver's `.inf` (`DriverVer`) and in the commit history (`git log --oneline`).

---

## Repository layout

```text
.
├── s23link.py                 PC ⇄ phone USB-C control link (WinUSB)
├── drivers/
│   ├── s23gpu/                Adreno 740 WDDM 2.0 KMD + UMD/test tools (MSVC build.ps1)
│   ├── s23batt/               MAX77705 battery & charger miniclass driver
│   ├── s23touch/              FTS2BA61Y HID multitouch
│   ├── s23keys/               Power / volume keys HID
│   ├── s23bthx/               QCA6490 UART Bluetooth transport
│   ├── s23gpuprobe/           GPU research probe + verify_pe.py (PE/import verifier)
│   └── s23*probe/, s23perf/, s23disp_diag/, s23acl*/   research probes & recovery helpers
├── display_research/          UEFI display DXE + display black-screen fix write-up
├── gpu_research/              Adreno 740 architecture notes, GPU SSDT, firmware tooling
├── bt_research/               QCA6490 UART14 bring-up notes + SSDT
├── wifi_research/             QCA6490 PCIe Wi-Fi 6E bring-up & install (working)
├── audio_sensors_research/    audio / haptics / sensors bus map
├── modem_5g_research/         5G modem blueprint, PIL/subsystem notes, SSDTs (index: README.md)
├── usb_research/              native USB-C spec + MAX77705 OPCODE mailbox reference (host-tested)
├── docs/archive/              historical bring-up reports & handoff notes
└── scripts/legacy/            one-off bring-up scripts kept for reference
```

---

## Building the drivers

All drivers target **Windows ARM64 kernel mode** and only import standard kernel exports (`ntoskrnl.exe`, plus class drivers such as `hidclass.sys`, `battc.sys`, `dxgkrnl.sys` where needed).

### Prerequisites

- **WSL** (for `build_wsl.py` drivers): `clang-18`, `lld` (`lld-link`), `llvm-dlltool`, `binutils-aarch64-linux-gnu` (`objdump`), Python 3; `acpica-tools` (`iasl`) for SSDTs.
- **Windows** (for `s23gpu` and signing): Visual Studio 2022 with the ARM64 MSVC toolset, Windows SDK/WDK `10.0.26100`, PowerShell, Python 3, and the test-signing certificate `CN=S23TouchTest2020` in `Cert:\CurrentUser\My`.

### WSL / Clang drivers (`s23batt`, `s23touch`, `s23keys`, `s23bthx`, probes)

```powershell
# 1. compile + link the ARM64 .sys inside WSL (writes drivers/<name>/out/)
wsl python3 ./drivers/s23bthx/build_wsl.py

# 2. sign the driver and create the catalog on Windows (writes drivers/<name>/pkg/)
powershell -ExecutionPolicy Bypass -File .\drivers\s23bthx\sign_and_cat.ps1

# 3. verify the signed binary (ARM64, PE32+, .reloc, checksum, Authenticode, imports)
python .\drivers\s23gpuprobe\verify_pe.py .\drivers\s23bthx\pkg\s23bthx.sys <path-to-phone-ntoskrnl.exe>
```

### MSVC driver (`s23gpu`)

```powershell
powershell -ExecutionPolicy Bypass -File .\drivers\s23gpu\build.ps1
```

`build.ps1` compiles the KMD with `Hostx64\arm64` `cl.exe`/`link.exe`, builds the user-mode test tools, pulls the Mesa UMD/WGL DLLs from the WSL Mesa build, signs everything and runs `verify_pe.py`.

> [!NOTE]
> Always verify and deploy the **signed** `pkg\*.sys` (never the unsigned `out\*.sys`). When compiling by hand, use `Hostx64\arm64`, never `Hostx64\x64`.

### ACPI SSDTs

```bash
iasl -tc gpu_research/s23_gpu_bringup/s23_gpu_acpi.asl
```

---

## Using `s23link.py`

With the phone running Windows and connected over USB-C:

```powershell
python s23link.py ping                       # round-trip test
python s23link.py exec "whoami && hostname"  # run a command as SYSTEM on the phone
python s23link.py exec -t 300 "pnputil /enum-drivers"
python s23link.py put .\drivers\s23batt\pkg\s23batt.sys C:\drv\s23batt.sys
python s23link.py get C:\Users\Public\s23keys.log .\s23keys.log
python s23link.py reboot
python s23link.py wait                       # block until the phone answers again
```

Set `RM9_WINUSB=1` to force the inbox WinUSB transport.

---

## Firmware (not included)

Proprietary Samsung/Qualcomm firmware, partition images, logs and all compiled binaries are excluded via [`.gitignore`](.gitignore). Extract firmware from **your own** device in Lineage Recovery using strictly read-only mounts:

- GPU: [`gpu_research/s23_gpu_bringup/fw/pull_s23_gpu_fw.py`](gpu_research/s23_gpu_bringup/fw/pull_s23_gpu_fw.py) (+ [`extract_and_verify_s23_gpu_fw.py`](gpu_research/s23_gpu_bringup/fw/extract_and_verify_s23_gpu_fw.py))
- Wi-Fi / Bluetooth: [`wifi_research/pull_boot24_and_wifi_fw.py`](wifi_research/pull_boot24_and_wifi_fw.py), [`QCA6490_DRIVER_AND_FW_GUIDE.md`](wifi_research/qca6490_bringup/QCA6490_DRIVER_AND_FW_GUIDE.md)
- Modem, DSPs, audio/haptics: [`modem_5g_research/pull_s23_5g_and_subsys_fw.py`](modem_5g_research/pull_s23_5g_and_subsys_fw.py)

---

## Contributing

- Open pull requests against `master`; keep each PR small and focused.
- Never commit binaries or firmware (`.sys`, `.exe`, `.dll`, `.obj`, `.pdb`, `.bin`, `.mbn`, `.img`, …).
- Kernel code: keep stack frames small (the ARM64 kernel stack is 24 KB — no `__chkstk` in any `.sys`), no CRT/user-mode dependencies, and do not change register offsets, clock/GDSC sequences or power votes without hardware validation.
- Historical reports live in [`docs/archive/`](docs/archive/), one-off bring-up scripts in [`scripts/legacy/`](scripts/legacy/).
