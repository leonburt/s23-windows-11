# Qualcomm Kailua (`SM8550`) Windows BSP — Complete Subsystem Driver & ACPI Inventory

**Audited Repository (Read-Only):** `C:\Users\Mahan\Documents\windows-on-android\s23_dm1q\repos\windows_silicon_qcom_kailua\`  
**Reference ACPI Tables Audited (Read-Only):**
- Active S23 `dm1q` DSDT: `/home/mahan/mu_s23/Mu-Silicium/Silicium-ACPI/Platforms/Samsung/dm1q/DSDT.aml` (`3,841 B`)
- Qualcomm Kailua MTP DSDT: `repos/Silicium-ACPI/Silicon/Qualcomm/Kailua/Decompiled/DSDT_MTP.dsl` (`61,683 B` AML)
- S23 Live Device Tree: `/home/mahan/mu_s23/s23_live.dts`

---

## 1. Executive Finding: Critical `_HID` Namespace Shift & Major Missing Subsystems

Auditing every file in `windows_silicon_qcom_kailua\Drivers\` and `Extensions\` reveals **two decisive findings**:

1. **Only 21 INF Packages Are Present (19 Core Drivers + 2 Extensions) — Almost All High-Level Subsystems Are Missing:**
   - The repository contains **NO** Qualcomm PEP (`qcpep`), **NO** GLINK/IPC Router (`qcglink`/`qcipc`), **NO** 5G Modem/X70 stack (`qcmbb`/`qcrmnet`/`qcipa`/`qcqmi`/`qcrfs`/`qcamss`), **NO** Adreno GPU driver (`qcdx`), **NO** Audio/LPASS/WCD/WSA drivers, **NO** Bluetooth/UART drivers, **NO** Sensors/SEE drivers, **NO** Camera/ISP drivers, and **NO** Thermal/TSENS drivers.
   - It ships **only** the foundational Qualcomm Kailua SOC plumbing layer (`ABD`, `SCM`, `SecApp`, `GPIO`, `I2C`, `SPI`, `IOMMU`, `PIL` + `PILFilter` + `SecureKernel`, `PMIC Apps`/`GLINK`/`GPIO`, `ADC`, `LLCC SysCache`, `TFTP`, `SOCPartition`, `CrashDumpInjector`, `ResetProtectionEnabler`, and `USB-C UCSI`).
2. **Critical `_HID` Mismatch (`QCOM5Exx` vs `QCOM0Cxx`/`QCOM06xx`):**
   - `Silicium-ACPI/Silicon/Qualcomm/Kailua/Decompiled/DSDT_MTP.dsl` uses the **`QCOM0Cxx` / `QCOM06xx` / `QCOM04xx`** ACPI ID scheme.
   - Every single driver in `windows_silicon_qcom_kailua` (`DriverVer = 08/27/2023, 1.0.3832.1600..1.0.3835.9600`) binds to the **`QCOM5Exx` / `QCOM05DD` / `QCOM0427`** ACPI ID scheme (matching Qualcomm's `8550` Windows BSP namespace)!
   - **Rule for S23 `dm1q` ACPI integration:** To load any `windows_silicon_qcom_kailua` driver without breaking its signed `.cat` catalog, the S23 ACPI SSDT MUST expose the exact `QCOM5Exx` (or `QCOM05DD`) ID as `_HID` (or `_CID`, plus `Alias(\_SB.PSUB, _SUB)` with `MTP08550` where extension INFs like `qciommuext8550.inf` match `SUBSYS_MTP08550`).

---

## 2. Current `dm1q/DSDT.aml` Baseline (`Mu-Silicium`)

Decompiling `/home/mahan/mu_s23/Mu-Silicium/Silicium-ACPI/Platforms/Samsung/dm1q/DSDT.aml` (`3,841 B`) shows that S23 currently exposes **ONLY 4 ACPI device families**:

| Device | `_HID` | Status on S23 Windows | Purpose |
|---|---|---|---|
| `SYSM` / `CLS0..2` / `CPU0..7` | `ACPI0010` / `ACPI0007` | **Active** | 8 ARM64 Kryo cores + `_LPI` idle states (gated by `PEPI = Zero`) |
| `UFS0` (`DEV0`) | `QCOM24A5` | **Active** | Internal UFS 4.0 storage (`_CCA = 0`) |
| `UUPC` | `PNP0A05` | **Active** | USB `_UPC` port capability container |
| `URS0` (`USB0` + `UFN0`) | `QCOM0C8B` | **Active** | USB-C dual-role controller (`GenericUSBFn` PC-Link works) |

**Result:** **0 of 21** `windows_silicon_qcom_kailua` drivers are currently declared in `dm1q/DSDT.aml`.

---

## 3. Exhaustive Audit of All 21 Packages in `windows_silicon_qcom_kailua\`

| # | Subsystem | Relative Path | Service | Version | Binary Payloads & Sizes | INF `_HID` (`kailua` BSP) | `DSDT_MTP.dsl` Device & `_HID` | Required Dependencies (`_DEP` / Runtime) | Present in `dm1q` DSDT? |
|---:|---|---|---|---|---|---|---|---|---|
| 1 | **ACPI / Bus** | `Drivers\SOC\ACPI\qcabd.inf` | `qcABD` | `1.0.3832.1600` | `qcabd.sys` (`49,896 B`) | `ACPI\QCOM0427` | `ABD` (`QCOM0427`) | Standalone ACPI Bridge Driver (SMEM/RPMh helper) | No |
| 2 | **GPIO / TLMM** | `Drivers\SOC\GPIO\qcgpio8550.inf` | `qcgpio` | `1.0.3832.1600` | `qcgpio.sys` (`71,488 B`) | `ACPI\QCOM5E0C` | `GIO0` (`QCOM0C0C`) | `PEP0` (optional on minimal), `0x0F100000` TLMM | No |
| 3 | **PIL / Hexagon** | `Drivers\SOC\HexagonLoader\PIL\qcpil.inf` | `qcPILC` | `1.0.3832.1600` | `qcpil.sys` (`438,080 B`) | `ACPI\QCOM5EE0` | `PILC` (`QCOM06E0`) | `SCM0`, `MMU0`, `RPEN`, `QCSK`, `TFTP`/`SOCP`, `PEP0` | No |
| 4 | **PIL / Filter Ext** | `Extensions\HexagonLoader\PILFilter\qcpilfilterext.inf` | `qcPILFC` | `1.0.3832.1600` | `QCPilFilter.sys` (`377,568 B`) | `ACPI\QCOM5EE0` *(UpperFilter)* | `PILC` (`QCOM06E0`) | Attaches above `qcPILC` for subsystem ELF/PAS filtering | No |
| 5 | **PIL / SecureKernel** | `Drivers\SOC\HexagonLoader\SecureKernel\QcSkExt8550.inf` | *(null, `PlatformExecute`)* | `1.0.3834.5900` | `QcSkExt8550.exe` (`187,528 B`) | `ACPI\QCOM5EAC` | `QCSK` (`QCOM0CAC`) | VBS/Secure Kernel (`async secure`) — **blocked on non-VBS / Gunyah** | No |
| 6 | **I2C (QUPv3)** | `Drivers\SOC\I2C\qci2c8550.inf` | `qci2c` | `1.0.3832.1600` | `qci2c8550.sys` (`88,296 B`) | `ACPI\QCOM5E10` | `I2C1` (`QCOM0C10`, `_CID QCOMFFEA`) | `PEP0` (clocks) + `QUPv3` GENI SE MMIO (`0x00800000+`) | No |
| 7 | **SPI (QUPv3)** | `Drivers\SOC\SPI\qcspi8550.inf` | `qcspi` | `1.0.3832.1600` | `qcspi8550.sys` (`102,224 B`), `BSRC_SPI.bin` (`53,556 B`), `BSRC_QSPI.bin` (`55,788 B`) | `ACPI\QCOM5E0E` | `SPI5` (`QCOM0C0E`) | `PEP0` (clocks) + `QUPv3` GENI SE MMIO | No |
| 8 | **IOMMU / SMMU** | `Drivers\SOC\IOMMU\qciommu.inf` | `qciommu` | `1.0.3832.1600` | `qciommu.sys` (`584,408 B`) | `ACPI\QCOM5E8F` | `IMM0`/`IMM1` (`QCOM068F`) + `MMU0`/`MMU1` (`QCOM0C09`) | `PEP0`, `IORT.aml`, SMMU-500 (`0x15000000`, `0x03DA0000`) | No |
| 9 | **IOMMU Ext** | `Extensions\SOC\IOMMU\qciommuext8550.inf` | *(reg ext)* | `1.0.3832.1600` | *(INF registry config only)* | `ACPI\VEN_QCOM&DEV_5E8F&SUBSYS_{MTP,IDP,IDPS,CRD}08550` | `IMM0`/`IMM1` (`SUBSYS=MTP08550`) | Configures `ARMv8L` + `SMMU500` + `EnableIOCoherentPTW=1` | No |
| 10 | **PMIC Apps** | `Drivers\SOC\PMIC\Apps\QcPmicApps8550.inf` | `qcpmicapps` | `1.0.3832.1600` | `qcpmicapps8550.sys` (`284,904 B`) | `ACPI\QCOM5E2C` | `PMAP` (`QCOM0C2C`) | `PMIC` (`QCOM0C2B`, missing!), `SPMI` (`QCOM0C0B`, missing!) | No |
| 11 | **PMIC GLINK** | `Drivers\SOC\PMIC\GLINK\QcPmicGlink8550.inf` | `qcpmicglink` | `1.0.3835.9600` | `qcpmicglink8550.sys` (`288,064 B`) | `ACPI\QCOM5E8E` | `PMGK` (`QCOM0C8E`) | `ADSP` (`PMIC_RTR_ADSP_APPS`) + `GLNK` (`QCOM0C84`, missing!) | No |
| 12 | **PMIC GPIO** | `Drivers\SOC\PMIC\GPIO\qcpmicgpio8550.inf` | `qcpmgpio` | `1.0.3832.1600` | `qcpmicgpio8550.sys` (`242,912 B`) | `ACPI\QCOM5E2D` | `PM01` (`QCOM0C2D`) | `PMIC` (`QCOM0C2B`, missing!) + `SPMI` | No |
| 13 | **Provisioning / UFS Partitions** | `Drivers\SOC\Provisioning\QcSOCPartition.inf` | `QcSOCPartition` | `1.0.3832.1600` | `QcSOCPartition.sys` (`359,656 B`) | `ACPI\QCOM5EDD` | `SOCP` (`QCOM06DD`) | Direct UFS partition reader (`modemst1`/`2`/`fsg`); **blocked by `SanPolicy=4` if UFS LUN offline** | No |
| 14 | **Reset / CrashDump** | `Drivers\SOC\ResetPower\CrashDumpInjector\qccdi8550.inf` | `QCCDI` | `1.0.3832.1600` | `qccdi8550.sys` (`390,376 B`) | `ACPI\QCOM5E2F` | `CDI` (`QCOM0C2F`) | `PILC`, `RPEN` | No |
| 15 | **Reset / Protection** | `Drivers\SOC\ResetPower\ResetProtectionEnabler\qcrpen.inf` | `QCRPEN` | `1.0.3832.1600` | `qcrpen.sys` (`289,504 B`) | `ACPI\QCOM5EE1` | `RPEN` (`QCOM06E1`) | Standalone SSR / reset-protection broker (dependency of `PILC`/`ADSP`/`AMSS`/`IPA`) | No |
| 16 | **System Cache (LLCC)** | `Drivers\SOC\System\CacheController\qcsyscache8550.inf` | `qcsyscache` | `1.0.3835.9600` | `qcsyscache8550.sys` (`240,448 B`), `SysCache_Profiler.dll` (`26,432 B`) | `ACPI\QCOM5E83` | `LLC` (`QCOM0C83`) | `PEP0`, LLCC registers (`0x09200000+`) | No |
| 17 | **TrustZone / SCM** | `Drivers\SOC\System\SCM\qcscm.inf` | `qcscm` | `1.0.3832.1600` | `qcscm.sys` (`360,272 B`), `SCMF.bin` (`269 B`) | `ACPI\QCOM05DD` | `SCM0` (`QCOM04DD`) | ARM64 `SMC #0` interface into Qualcomm TZ (`QTEE`) underneath Gunyah EL2 | No |
| 18 | **Secure App / QTEE** | `Drivers\SOC\System\SecApp\qcsecapp.inf` | `qcsecapp` | `1.0.3832.1600` | `qcsecapp.sys` (`310,608 B`) | `ACPI\QCOM5EE4` | `TLOG` (`QCOM0CE4`) / `TREE` (`QCOM04DE`) | `SCM0`, Windows QTEE TA binaries | No |
| 19 | **Transports / TFTP** | `Drivers\SOC\Transports\QcTftpKmdf.inf` | `QcTftpKmdf` | `1.0.3832.1600` | `QcTftpKmdf.sys` (`291,136 B`) | `ACPI\QCOM5EDC` | `TFTP` (`QCOM06DC`) | `IPC0` (`QCOM0C0D`, missing!) + `GLNK` (`QCOM0C84`, missing!) — serves MPSS/ADSP RFS files | No |
| 20 | **Voltage/Current ADC** | `Drivers\SOC\VoltageCurrentMonitoring\qcadc8550.inf` | `qcADC` | `1.0.3832.1600` | `qcadc8550.sys` (`306,496 B`), `ADC1.bin` (`9,639 B`) | `ACPI\QCOM5E11` | `ADC1` (`QCOM0C11`) | `PMIC` (`QCOM0C2B`) / `SPMI` (`QCOM0C0B`) — PMK8550 VADC telemetry | No |
| 21 | **USB-C / UCSI** | `Drivers\USB\UCSI\qcusbcucsi8550.inf` | `QcUsbC` | `1.0.3832.1600` | `qcusbcucsi8550.sys` (`344,296 B`), `UCS0.bin` (`22,256 B`) | `ACPI\QCOM5EA4` | *(USB Type-C UCSI; conflicts with S23 Samsung PDIC!)* | `PMGK` (`PMIC GLINK`) + `ADSP` | No |

---

## 4. Subsystem Gap Matrix: What Is Missing from `windows_silicon_qcom_kailua`

Cross-referencing `DSDT_MTP.dsl` against the 21 packages above exposes every missing Qualcomm Windows driver required for full phone functionality:

| Subsystem | Required ACPI Devices (`DSDT_MTP.dsl` `_HID`) | In `windows_silicon_qcom_kailua`? | Notes for S23 `dm1q` |
|---|---|---|---|
| **PEP / Power / Clocks** | `PEP0` (`QCOM0C17`), `SPMI` (`QCOM0C0B`), `PMIC` (`QCOM0C2B`) | **MISSING** | Largest foundational gap — required by `ADSP`, `AMSS`, `IPA`, `GPU0`, `I2C`, `SPI`, `MMU0/1`. Without `qcpep`, custom drivers must vote clocks/RPMh directly via MMIO + `APPS_RSC` TCS0 (as done in `s23touch`, `s23batt`, `s23pcieprobe`, `s23gpuprobe`). |
| **GLINK / IPC Router / IPCC** | `IPCC` (`QCOM06C2`), `IPC0` (`QCOM0C0D`), `GLNK` (`QCOM0C84`), `PDSR` (`QCOM06DF`) | **MISSING** | Mandatory transport for all Hexagon DSPs (`ADSP`, `CDSP`, `MPSS`), `TFTP`, `PMIC GLINK`, and `QMI`. |
| **PIL / Subsystems (`ADSP`/`CDSP`/`MPSS`)** | `PILC` (`QCOM06E0`/`5EE0` — **present**), `ADSP` (`QCOM0C1B`), `AMSS` (`QCOM0C1C`), `NSP0` (`QCOM0CB0`), `SPSS` (`QCOM0C8D`), `RFS0` (`QCOM0C15`), `QSM` (`QCOM0C1E`), `SSDD` (`QCOM0C20`) | **PARTIAL** (`qcpil` + `QCPilFilter` + `QcTftpKmdf` + `QcSOCPartition` present; `qcadsp`/`qcamss`/`qcrfs`/`qcpdsr`/`qcssdd` **MISSING**) | Even with `qcpil.sys` present, it cannot boot `AMSS`/`ADSP` without `qcpep` + `qcglink` + `qcipc` + `qcrfs` + subsystem clients. |
| **X70 5G Modem / QMI / MBB / IPA / RmNet** | `AMSS` (`QCOM0C1C`), `IPA` (`QCOM0C6A`), `RMNT` (`QCOM0C95`), `MBRG` (`QCOM0C07`), `RMAT` (`QCOM0C08`), `DPLB` (`QCOM0C70`), `QDIG` (`QCOM0C13`) | **MISSING** (`qcmbb`, `qcrmnet`, `qcipa`, `qcqmi`, `qcmbrg` all absent) | See `S23_5G_MODEM_BRINGUP.md` for full feasibility & hard-blocker analysis. |
| **GPU / Display** | `GPU0` (`QCOM0C36`) | **MISSING** (`qcdx` absent; shipping Gen7 `qcdx` rejects `SM8550`) | Use open Path 1 (`s23gpuprobe` -> `G1287`/`G779` + Mesa `G739`). |
| **Audio / LPASS / WCD / WSA** | `ADCM` / `AUCD` / `ACXS` (under `ADSP`) | **MISSING** | S23 uses `WCD938x` + **dual Cirrus Logic `CS35L43`** speaker amps on I2C `0x40`/`0x41` (NOT Qualcomm `WSA883x`), requiring custom amp init in addition to ADSP/LPASS! |
| **Bluetooth / UART** | `UR15` (`QCOM0C16`), `BTH0` (`QCOM0C6B`), `BTAT` (`QCOM0D04`) | **MISSING** (`qcuart`/`qcbt` absent) | S23 BT is QCA6490 over `qupv3_se14_4uart @ 0x00898000` (`GPIO81 BT_EN`), and S23 BT firmware (`hpbtfw21.tlv`, `hpnv21.bin`) is **already on disk** — high-value open-driver candidate! |
| **Sensors / SEE** | `SEN2` (`QCOM0693`), `HPS0` (`QCOM06D9`) | **MISSING** | Depends on `ADSP`/SLPI (`adspslpi_region@9ea00000`) + `GLNK` + `QMI`. |
| **Thermal / CPUfreq** | `PEP0` + TSENS (`0x0C271000..0x0C273000`) + EPSS (`0x17D91000..0x17D93000`) | **MISSING** | Direct MMIO read/write to `EPSS` & `TSENS` is completely independent of `qcpep` — huge quick win! |
| **Camera / ISP / Video** | `CAMP`/`CAMS`/`CAMF`/`CAMI`/`CAMT`/`VFE0`/`JPGE`/`EVA0`/`AVS0` | **MISSING** | Complex MIPI CSI + Samsung `s2mpb02` camera PMIC + ISP firmware; lowest priority. |
