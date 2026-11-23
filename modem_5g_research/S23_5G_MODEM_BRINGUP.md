# Samsung Galaxy S23 (`dm1q` / `SM-S911B`, `SM8550-AC`) — Snapdragon X70 5G Modem & Kailua Core Subsystem Bring-Up Blueprint

**Target Device:** Samsung Galaxy S23 (`dm1q` / `SM-S911B`, `S911BXXS8DYI3`, `SM8550-AC` + integrated Snapdragon X70 5G RF Modem)  
**Target OS:** Windows 11 Pro 24H2 ARM64 (`10.0.26100.4349`)  
**Primary Sources Verified On Disk:**
- `/home/mahan/mu_s23/s23_live.dts` (`remoteproc-mss@04080000`, `qcom,ipa@3e00000`, `qcom,ipcc@408000`, `qcom,glink`, `qcom,smp2p-modem`, `reserved-memory`)
- `/home/mahan/s23/src/kernel_platform/msm-kernel/drivers/pinctrl/qcom/pinctrl-kalama.c` (UIM0/UIM1 SIM pinmux)
- `repos/windows_silicon_qcom_kailua/` & `repos/Silicium-ACPI/Silicon/Qualcomm/Kailua/Decompiled/DSDT_MTP.dsl`
- `backups/` (read-only S23 NV partition audit) vs `..\device_firmware\` / `..\modem_fix\` (verified Galaxy A52 4G `SM7125` — NOT S23!)

---

## 1. Exact S23 (`dm1q`) Snapdragon X70 5G Modem Hardware Map (`s23_live.dts`)

### 1.1 Remoteproc / PAS / IPC / Interrupt Resources

| Subsystem Block | Physical Base & Size | DTS Compatible / ID | GIC Interrupts (`SPI` -> `GSI = SPI+32`) | Power / Clocks / IPC |
|---|---|---|---|---|
| **MPSS (`remoteproc-mss@04080000`)** | `0x04080000` (`0x10000`) | `qcom,kalama-modem-pas` (PAS ID = `4`) | Watchdog `SPI 0x108` (`264`) -> **`GSI 296` (`0x128`)**; SMP2P (`fatal`, `handover`, `ready`, `stop-ack`, `shutdown-ack`) | `xo`, `cx.lvl (0x30000)` @ `0x180`, `mss.lvl (0x30060)` @ `0x140`, AOSS QMP, `rproc_ddr`/`crypto_ddr` ICC |
| **IPCC (`qcom,ipcc@408000`)** | `0x00408000` (`0x1000`) | `qcom,ipcc` | MPROC client `0x02` (`mpss_smem`), `0x06` (`cdsp_smem`), `0x03` (`adsp_smem`) | Shared doorbell fabric for GLINK SMEM edges |
| **GLINK MPSS Edge** | SMEM (`0x81D00000`, `2 MiB`) | `qcom,glink` (`remote-pid = 1`, `label = "mpss"`) | Via `IPCC` client `0x02` | Channels: `IPCRTR` (QRTR/QMI), `DS` (`at_mdm0`), `DATA40_CNTL` (`smdcntl8`), `DATA1/4/11`, `QMC_DMA_LINE`, `QMC_CMA_LINE`, `glink_bridge` |
| **IPA v5.0 (`qcom,ipa@3e00000`)** | **IPA `0x03E00000` (`0x84000`)**<br>**GSI `0x03E04000` (`0xFC000`)**<br>*(NOT `0x01E00000`!)* | `qcom,ipa` (`ipa-hw-ver = 0x18` = IPA 5.0, PAS ID = `0x0F`, `ipa_fws`) | `ipa-irq`: `SPI 0x28E` (`654`) -> **`GSI 686` (`0x2AE`)**<br>`gsi-irq`: `SPI 0x1B0` (`432`) -> **`GSI 464` (`0x1D0`)** | `apps_smmu@15000000` SIDs: **`0x4A0`** (`ipa_smmu_ap`, `dma-coherent`, additional map `0x146A8000`), **`0x4A1`** (`wlan`), **`0x4A2`** (`uc`), **`0x4A4`** (`11ad`) |
| **UIM0 / SIM1 (`pinctrl-kalama.c`)** | TLMM (`0x0F100000`) | Modem-owned UIM0 | Modem-internal UIM interrupt | `GPIO130` (`uim0_data`), `GPIO131` (`uim0_clk`), `GPIO132` (`uim0_reset`), `GPIO27` (`uim0_present`), PMIC `ldob9` |
| **UIM1 / SIM2 / eSIM** | TLMM (`0x0F100000`) | Modem-owned UIM1 | Modem-internal UIM interrupt | `GPIO134` (`uim1_data`), `GPIO135` (`uim1_clk`), `GPIO136` (`uim1_reset`), `GPIO26` (`uim1_present`), PMIC `ldob10` |

> [!IMPORTANT]
> **Address Correction Confirmed Against Both `s23_live.dts` and `DSDT_MTP.dsl`:** On Snapdragon 8 Gen 2 (`SM8550` / Kailua), IPA moved from legacy `0x01E00000` (SM8150–SM8450) to **`0x03E00000`** (`0x03E00000..0x03E84000` IPA + `0x03E04000..0x03F00000` GSI, total `2 MiB` block). Both `s23_live.dts` (`qcom,ipa@3e00000`) and `Kailua/Decompiled/DSDT_MTP.dsl` (`Device (IPA)`) agree down to the exact byte and GIC GSIs (`0x2AE` and `0x1D0`).

### 1.2 S23 Modem & Subsystem Reserved Memory Carveouts (`s23_live.dts` lines `94..206`)

All of these lie inside `Mu-dm1q`'s `PIL Reserved` (`0x89800000`, `0x19280000`) and high carveout regions (`no-map`):

| DTS Region Node | Physical Base | Size | Purpose |
|---|---:|---:|---|
| `mpss_region@89800000` | `0x8A800000` | `0x10800000` (**264 MiB**) | Hexagon Q6 MPSS modem code/data ELF load region |
| `q6_mpss_dtb_region@9b000000` | `0x9B000000` | `0x00080000` (**512 KiB**) | MPSS device-tree blob (`modem_dtb`) authenticated by PAS |
| `ipa_fw_region@9b080000` | `0x9B080000` | `0x00010000` (**64 KiB**) | IPA microcontroller firmware (`ipa_fws`) |
| `ipa_gsi_region@9b090000` | `0x9B090000` | `0x0000A000` (**40 KiB**) | IPA GSI firmware (`ipa_gsi_fw`) |
| `spu_modem_shared_mem@9b2e0000` | `0x9B2E0000` | `0x00020000` (**128 KiB**) | SPU <-> Modem secure shared memory |
| `mpss_dsm_region@d4d00000` | `0xD4D00000` | `0x03300000` (**51 MiB**) | MPSS Dynamic Shared Memory (DSM / packet pool) — **NOTE:** currently NOT carved out separately in `dm1q` `MemoryMapLib.c` (`DXE Heap` ends at `0xC7CC0000`, `CPUCP FW` starts at `0xD8140000`, leaving `0xD4D00000..0xD8000000` unmapped; must be explicitly reserved `MEM_RES` before ever booting MPSS!) |
| `adspslpi_region@9ea00000` | `0x9EA00000` | `0x059B4000` (~89.7 MiB) | ADSP / SLPI Hexagon DSP (`q6_adsp_dtb_region@9e980000`) |
| `cdsp_region@9c900000` | `0x9C900000` | `0x02000000` (**32 MiB**) | CDSP Hexagon NPU (`q6_cdsp_dtb_region@9e900000`) |

### 1.3 Modem Firmware & EFS/NV Files Audited On Disk (Read-Only)

| Path | Verified Origin & Contents | Usable for S23 `dm1q`? |
|---|---|---|
| `s23_dm1q\backups\efs.img` (`20 MiB`), `sec_efs.img` (`20 MiB`) | **S23 `dm1q` (`RFCW90N0N7X`)** factory EFS & Samsung secure EFS backups (`read-only!`) | **YES** (Samsung RIL metadata / carrier CSC; **NEVER modify**) |
| `s23_dm1q\backups\modemst1.img` (`3 MiB`), `modemst2.img` (`3 MiB`), `fsg.img` (`3 MiB`), `fsc.img` (`4 KiB`) | **S23 `dm1q`** hardware-AES-encrypted Qualcomm EFS1/EFS2/FSG/FSC partitions (`IMGEFS1`/`2`/`G`) containing IMEI & RF calibration | **YES** (critical factory NV; **NEVER write from experimental Windows drivers!**) |
| `..\device_firmware\firmware_modem\` (`modem.mdt` + `modem.b00..b29`), `ipa_fws.*` | **Galaxy A52 4G (`SM-A525F` / `SM7125`)**, confirmed via `..\modem_fix\MODEM_FORENSIC_REPORT.md` | **NO — WRONG SOC/DEVICE!** |
| **Missing S23 Modem Firmware on PC** | S23's `modem` partition (`NON-HLOS` / `/vendor/firmware_mnt/image/modem.*`, `modem_dtb.*`, `ipa_fws.*`, `mcfg_sw`) is NOT dumped to PC yet. | Pull read-only from S23 Android/Recovery if/when PIL bring-up begins. |

---

## 2. Required Windows Mobile Broadband (MBB) Stack vs. Hard Blockers on Samsung `dm1q`

### 2.1 Full Windows Mobile Broadband Dependency Chain

To bring up cellular 5G/LTE data & SIM in Windows 11 ARM64 (`NetAdapterCx` / `MBBCx`), **every link** in this 8-layer chain must work:

1. **`qcscm` (`ACPI\QCOM05DD`)** — ARM64 `SMC #0` bridge to Qualcomm TrustZone (`QTEE`) for PAS ELF metadata authentication, memory-region protection (`hyp_assign`), and PAS reset release. *(Present in `windows_silicon_qcom_kailua`.)*
2. **`qcpep` (`ACPI\QCOM0C17`)** — Power Engine Plugin owning RPMh (`cx.lvl`, `mss.lvl`, `xo`), interconnects (`rproc_ddr`/`crypto_ddr`), and IPA/MPSS clocks. *(**MISSING** from `windows_silicon_qcom_kailua`!)*
3. **`qcipc` / `qcglink` / `qcipcc` (`QCOM06C2` / `QCOM0C0D` / `QCOM0C84`)** — Shared-memory (`0x81D00000`) + `IPCC (0x00408000)` transport hosting `IPCRTR` (QRTR) and SMD/GLINK control channels. *(**MISSING** from `windows_silicon_qcom_kailua`!)*
4. **`qcpil` + `QCPilFilter` + `qcrpen` + `QcTftpKmdf` + `QcSOCPartition` + `RFS0`** — Loads `modem.mdt`/`modem.b*` into `0x8A800000`, authenticates via `PAS ID 4`, serves Remote File System (RFS) requests for `modemst1`/`modemst2`/`fsg` (`mcfg` / EFS / NV). *(Partial: `qcpil`/`QCPilFilter`/`qcrpen`/`QcTftpKmdf`/`QcSOCPartition` present; `qcrfs`/`qcamss`/`qcpdsr`/`qcssdd` **MISSING**!)*
5. **`qcamss` (`ACPI\QCOM0C1C`)** — Modem subsystem driver owning WDOG `GSI 296` and SMP2P ready/fatal handshakes. *(**MISSING**.)*
6. **`qcipa` (`ACPI\QCOM0C6A`)** — IPA v5.0 (`0x03E00000`) + GSI hardware packet offload + `ipa_fws` (`PAS ID 0x0F`) + `apps_smmu` SIDs `0x4A0..0x4A4`. *(**MISSING**.)*
7. **`qcqmi` + `qcrmnet` (`QCOM0C95`) + `qcmbrg` (`QCOM0C07`)** — QMI service client (`DMS`, `NAS`, `WDS`, `UIM`, `PDS`) + RmNet virtual Ethernet/IP data plane over IPA. *(**MISSING**.)*
8. **`qcmbb` (`MBBCx`) + Windows WWAN AutoConfig (`WwanSvc`)** — Translates Windows MBIM OIDs into QMI `UIM`/`NAS`/`WDS` commands for SIM unlock, APN attach, and 5G NR/LTE bearer management. *(**MISSING**.)*

### 2.2 The 5 Hard Blockers on Samsung Galaxy S23 (`dm1q`)

1. **Missing Qualcomm Kailua Windows Modem/IPC/PEP Drivers (Blocker #1 — Absolute):**
   - Unlike Surface Duo (`SM8150`/`SM8350`) or Snapdragon X laptops (`SC8280XP`/`X1E80100`), Qualcomm never shipped a retail Windows 11 ARM64 device on `SM8550` (`Kailua`). Consequently, `windows_silicon_qcom_kailua` contains **zero** binaries for `qcpep`, `qcglink`, `qcipc`, `qcamss`, `qcipa`, `qcqmi`, `qcrmnet`, or `qcmbb`. Borrowing `SM8350`/`SC8280XP` binaries fails because `SM8550` changed IPA to v5.0 (`0x03E00000`), IPCC v2, and Kailua RPMh/cmd-db resource layouts.
2. **Gunyah EL2 Hypervisor + Qualcomm PAS/TZ `SCM` Signature Enforcement (Blocker #2 — Security/Hypervisor):**
   - On `dm1q`, Samsung's `S911BXXS8DYI3` TZ (`QTEE`) + Gunyah EL2 enforces strict `PAS` RSA/ECDSA signature verification against Samsung's OEM signing chain (`modem.mdt` + `modem_dtb` + `ipa_fws`). Only Samsung's stock signed S23 modem ELF will authenticate.
   - Worse, `PAS` memory handover calls `hyp_assign` (`SCM_SVC_MP`) to transfer `0x8A800000..0x9B07FFFF` (`264.5 MiB`) from HLOS VM to `VMID_MSS_MSA`. Any overlap with Windows's own memory map or Gunyah stage-2 page tables causes an instant EL2 hypervisor panic / `900E` crashdump.
   - `QcSkExt8550.exe` (`QCOM5EAC`) expects Windows VBS Secure Kernel (`async secure`), which is incompatible with booting Windows under Qualcomm Gunyah EL2.
3. **Samsung `modem.bin` / `sec_efs` / RFS / IMEI Protection (Blocker #3 — Safety-Critical):**
   - Samsung's stock MPSS firmware (`SM-S911B` One UI baseband) expects Samsung-specific RFS/EFS/NV items and `sec-ril` / `QCNv` handshakes over `glinkpkt` (`at_mdm0`, `smdcntl8`) in addition to standard QMI.
   - **High brick/IMEI-corruption risk:** If `QcSOCPartition`/`RFS0` writes corrupt sync records back to live UFS `modemst1`/`modemst2`, it can corrupt baseband NV sync counters or trigger `CP silent reset` loops even back in Android (though backups exist in `backups\`, live NV writes must NEVER be enabled during bring-up — always redirect RFS to RAM-backed read-only copies of `modemst1`/`modemst2`/`fsg`!).
4. **`SanPolicy = 4` UFS LUN Isolation (Blocker #4 — Operational):**
   - `s23_dm1q` intentionally sets `SanPolicy = 4` (`OfflineShared` / all extra LUNs offline) so Windows cannot accidentally corrupt Android's UFS LUNs (`sda`–`sdf`). Because `modemst1`/`modemst2`/`fsg`/`modem` live on Android LUNs (`sda`/`sde`), `QcSOCPartition` cannot open them while `SanPolicy = 4` keeps those disks offline — requiring either RAM-cached firmware files inside `C:\Windows\System32\drivers\` + a read-only RAM-backed RFS server, or selective per-partition mounting (risky!).
5. **Samsung Dual-SIM / eSIM (`UIM0`/`UIM1`) Carrier MCFG & IMS:**
   - Even if MPSS boots and registers on 5G/LTE via QMI, Samsung's `SM-S911B` baseband uses Samsung dynamic PLMN tables + carrier MCFG switching; voice calling (VoLTE/VoNR) requires IMS stack support absent in Windows `WwanSvc` (data-only MBIM is the realistic ceiling).

**Honest Feasibility Verdict:** Native X70 5G cellular on S23 Windows 11 is a **Tier-4 (Long-Term / Research-Only) milestone** blocked primarily by the absence of public Kailua `qcpep`/`qcglink`/`qcipa`/`qcmbb` Windows drivers. The **only realistic open path** (without leaked Qualcomm Kailua MTP BSP modem binaries) is either (a) porting Linux `qcom_q6v5_pas` + `glink_smem` + `qrtr` + `rmnet`/`ipa` into a custom open-source Windows KMDF/NetAdapterCx driver (multi-month effort), or (b) using USB/Wi-Fi tethering or a USB-C external WWAN dongle once USB host mode / QCA6490 Wi-Fi is working.

---

## 3. Recommended Foundational Bring-Up Order (If Pursued Later)

| Phase | Target Layer | ACPI Nodes Required | Safety Guardrails |
|---|---|---|---|
| **Phase 0** | Reserve `mpss_dsm_region` (`0xD4D00000`, `51 MiB`) in `MemoryMapLib.c` | None | Verify Windows boots cleanly with `0xD4D00000` carved out `MEM_RES`. |
| **Phase 1** | **`qcscm` (`ACPI\QCOM05DD`)** + **`qcabd` (`ACPI\QCOM0427`)** | `SCM0`, `ABD` | Already shipped in `windows_silicon_qcom_kailua`! Test read-only `SCM` info query first (zero hardware state mutation). |
| **Phase 2** | **Open RPMh/Clock Provider (replaces missing `qcpep`)** | Custom `S23P` or direct `APPS_RSC` TCS0 | Vote `cx.lvl`, `mss.lvl`, `xo` via verified `APPS_RSC DRV2 TCS0` (`0x17A20000`). |
| **Phase 3** | **Open `IPCC` + `GLINK-SMEM` + `QRTR` (`IPCRTR`)** | `IPCC`, `GLNK`, `IPC0` | Map `IPCC (0x00408000)` + `SMEM (0x81D00000)` read-only first; verify `ADSP`/`AOP` SMEM TOC headers before opening TX channels. |
| **Phase 4** | **`PIL` + `MPSS` (`PAS ID 4`) with RAM-Only RFS** | `PILC`, `RPEN`, `TFTP`, `RFS0`, `AMSS` | Pull S23 stock `modem.mdt`/`modem.b*` + `modem_dtb` into `C:\Windows\System32\drivers\`; serve **read-only copies** of `modemst1`/`2`/`fsg` from RAM so live UFS NV is 100% untouchable! |
| **Phase 5** | **`IPA v5.0 (0x03E00000)` + `QMI` + `MBBCx` (Data-Only)** | `IPA` | Requires `ipa_fws` (`PAS ID 0x0F`), `apps_smmu` SID `0x4A0`, and QMI `UIM`/`NAS`/`WDS` -> Windows `MBBCx` miniport. |

---

## 4. Quick Wins Discovered in Kailua & S23 Hardware (Ranked by Effort vs. Payoff)

While X70 5G is blocked by missing Qualcomm drivers, our Kailua + `s23_live.dts` audit uncovered **five much higher-ROI subsystem wins** for S23 Windows 11:

| Rank | Subsystem | Effort | Payoff | Concrete Technical Path on S23 (`dm1q`) |
|---:|---|---|---|---|
| **1** | **CPU Frequency Scaling (EPSS DVFS) & SoC Thermal Monitoring (TSENS)** | **Low** (1–2 days, pure KMDF/WDM like `s23batt`) | **HUGE** (prevents throttling / unlocks 3.36 GHz Prime core + real CPU temp reporting!) | `s23_live.dts` proves S23 exposes direct MMIO **EPSS hardware frequency domains** (`0x17D91000` Silver, `0x17D92000` Gold, `0x17D93000` Prime — standard `qcom-cpufreq-hw` LUT + `PERF_DESIRED` register, **zero `qcpep` needed!**) and **TSENS v2** (`0x0C271000` / `0x0C272000` / `0x0C273000` — 41 on-die temperature sensors readable directly via MMIO!). |
| **2** | **Bluetooth 5.3 over QCA6490 HSUART (`qupv3_se14_4uart`)** | **Medium** (1–2 weeks) | **HIGH** (wireless headphones, keyboards, mice — solves Audio & Input immediately without ADSP/LPASS!) | S23's QCA6490 Bluetooth shares the **exact same PMIC rails & PDC votes** already solved by `s23pcieprobe`! Control pin is `GPIO81` (`BT_EN`), UART is `qupv3_se14_4uart @ 0x00898000` (4-wire GENI HSUART, `SPI 0x1CD -> GSI 493`), and **S23's exact stock BT firmware (`hpbtfw21.tlv`, `hpnv21.bin`, `hpnv21g.bin`) is ALREADY ON DISK** in `wifi_research\s23_vendor_wifi_fw\`! A small KMDF `BthMini` / H4 UART transport driver (or `qcuart` + `BthH4` ACPI `BTH0`) unlocks Bluetooth audio + HID peripherals completely bypassing LPASS! |
| **3** | **Qualcomm `qcscm` (`ACPI\QCOM05DD`) + `qcgpio` (`QCOM5E0C`) + `qci2c` (`QCOM5E10`) + `qcspi` (`QCOM5E0E`)** | **Low–Medium** (3–5 days) | **MEDIUM–HIGH** (replaces ad-hoc MMIO with inbox-style signed Kailua bus drivers) | **Already present & Qualcomm-signed** in `windows_silicon_qcom_kailua\Drivers\SOC\`! Just add matching `QCOM05DD` / `QCOM5E0C` / `QCOM5E10` / `QCOM5E0E` ACPI devices (with S23's QUPv3 GENI SE bases `0x00880000+`/`0x00980000+`/`0x00A80000+` pre-clocked in UEFI). |
| **4** | **USB-C Host Mode (xHCI `USB0`) & PD Role Switching** | **Medium** (1 week) | **HIGH** (USB keyboards, Ethernet dongles, external drives, HDMI adapters) | `URS0` (`USB0` + `UFN0`) is already in `dm1q/DSDT.aml`. Switching USB-C from peripheral (`GenericUSBFn`) to host mode (`USB0` xHCI) via S23's I2C PDIC / `QUSB2` PHY gives instant plug-and-play USB Ethernet, USB Audio DACs, and USB hubs. |
| **5** | **Native Internal Speakers / Audio (LPASS + `WCD938x` + Dual Cirrus `CS35L43`)** | **Very High** (months) | **MEDIUM** (Bluetooth / USB-C audio is 10x easier) | S23 uses LPASS ADSP (`adspslpi_region@9ea00000`, `lpass-cdc` SoundWire `swr0..3`) + `WCD938x` (`GPIO108` reset) + **two Cirrus Logic `CS35L43` smart amps** on I2C `0x40`/`0x41` (`GPIO14`/`42`/`55`, custom Cirrus firmware). Because `windows_silicon_qcom_kailua` lacks `qcpep`, `qcglink`, `qcadsp`, and `qcaud`—AND Kailua MTP uses `WSA883x`, not Cirrus `CS35L43`—native speakers require ADSP PIL + GLINK + GPR/AudioReach + custom Cirrus I2C DSP bring-up. **Prioritize Bluetooth audio (#2) first!** |

---

## 5. Ready-to-Integrate ACPI Stubs (`modem_5g_research/s23_modem_subsystems.asl`)

A companion `iasl -tc`-verified ACPI SSDT (`modem_5g_research/s23_modem_subsystems.asl`, compiled to `s23_modem_subsystems.aml` & `s23_modem_subsystems.hex` with **0 Errors, 0 Warnings**) defines S23-exact ACPI stubs for:
- `ABD` (`QCOM0427`), `SCM0` (`QCOM05DD`, `_CID QCOM04DD`), `RPEN` (`QCOM5EE1`, `_CID QCOM06E1`), `PILC` (`QCOM5EE0`, `_CID QCOM06E0`), `TFTP` (`QCOM5EDC`, `_CID QCOM06DC`), `SOCP` (`QCOM5EDD`, `_CID QCOM06DD`), `IPCC` (`QCOM06C2` @ `0x00408000`), `GLNK` (`QCOM0C84`), `IPC0` (`QCOM0C0D`), `AMSS` (`QCOM0C1C`, WDOG `GSI 296`), and `IPA` (`QCOM0C6A` @ `0x03E00000` + `0x03E04000`, `GSI 686` & `464`).
- **Safety:** Every stub defaults to `Method (_STA) { Return (Zero) }` so including the table causes **zero behavioral change** until individual devices are explicitly flipped to `0x0F` one-by-one as matching drivers become available.
