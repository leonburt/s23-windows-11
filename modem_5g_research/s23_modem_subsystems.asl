/*
 * s23_modem_subsystems.asl - Samsung Galaxy S23 (dm1q / SM8550-AC) Kailua SOC & X70 Modem ACPI Stubs
 *
 * Verified against /home/mahan/mu_s23/s23_live.dts, Kailua/Decompiled/DSDT_MTP.dsl,
 * and windows_silicon_qcom_kailua/Drivers/ INFs.
 *
 * CRITICAL _HID/_CID NOTE:
 *   windows_silicon_qcom_kailua INFs (08/27/2023) bind to QCOM5Exx / QCOM05DD / QCOM0427,
 *   whereas DSDT_MTP.dsl uses QCOM0Cxx / QCOM06xx / QCOM04DD. Where a signed Kailua BSP
 *   driver exists on disk, _HID is set to the exact Kailua BSP INF ID and _CID carries
 *   the DSDT_MTP ID for forward compatibility.
 *
 * SAFETY DEFAULT:
 *   ALL devices return _STA = 0 (disabled) by default so this SSDT is 100% inert until
 *   each device is intentionally enabled (0x0F) alongside a tested driver.
 *
 * Compile check: iasl -tc s23_modem_subsystems.asl  (0 Errors, 0 Warnings)
 */
DefinitionBlock ("s23_modem_subsystems.aml", "SSDT", 2, "SAMSNG", "S23MDMS0", 0x00000001)
{
    Scope (\_SB)
    {
        /* Phase 1: Qualcomm ACPI Bridge Driver (windows_silicon_qcom_kailua: qcabd.inf) */
        Device (ABD)
        {
            Name (_HID, "QCOM0427")
            Name (_UID, Zero)
            Method (_STA, 0, NotSerialized) { Return (Zero) }
        }

        /* Phase 1: Qualcomm SCM / TrustZone Bridge (windows_silicon_qcom_kailua: qcscm.inf) */
        Device (SCM0)
        {
            Name (_HID, "QCOM05DD")
            Name (_CID, "QCOM04DD")
            Name (_UID, Zero)
            Method (_STA, 0, NotSerialized) { Return (Zero) }
        }

        /* Phase 1: Reset Protection Enabler (windows_silicon_qcom_kailua: qcrpen.inf) */
        Device (RPEN)
        {
            Name (_HID, "QCOM5EE1")
            Name (_CID, "QCOM06E1")
            Name (_UID, Zero)
            Method (_STA, 0, NotSerialized) { Return (Zero) }
        }

        /* Phase 3: Inter-Processor Communication Controller (s23_live.dts: qcom,ipcc@408000) */
        Device (IPCC)
        {
            Name (_HID, "QCOM06C2")
            Name (_UID, Zero)
            Method (_STA, 0, NotSerialized) { Return (Zero) }
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x00408000, 0x00001000)
            })
        }

        /* Phase 3: GLINK Shared-Memory Transport (s23_live.dts: SMEM 0x81D00000 + IPCC) */
        Device (GLNK)
        {
            Name (_HID, "QCOM0C84")
            Name (_UID, Zero)
            Name (_DEP, Package () { \_SB.IPCC })
            Method (_STA, 0, NotSerialized) { Return (Zero) }
        }

        /* Phase 3: IPC Router / QRTR Fabric */
        Device (IPC0)
        {
            Name (_HID, "QCOM0C0D")
            Name (_UID, Zero)
            Name (_DEP, Package () { \_SB.GLNK })
            Method (_STA, 0, NotSerialized) { Return (Zero) }
        }

        /* Phase 4: Peripheral Image Loader (windows_silicon_qcom_kailua: qcpil.inf + qcpilfilterext.inf) */
        Device (PILC)
        {
            Name (_HID, "QCOM5EE0")
            Name (_CID, "QCOM06E0")
            Name (_UID, Zero)
            Name (_DEP, Package () { \_SB.SCM0, \_SB.RPEN })
            Method (_STA, 0, NotSerialized) { Return (Zero) }
        }

        /* Phase 4: Qualcomm TFTP RFS Server (windows_silicon_qcom_kailua: QcTftpKmdf.inf) */
        Device (TFTP)
        {
            Name (_HID, "QCOM5EDC")
            Name (_CID, "QCOM06DC")
            Name (_UID, Zero)
            Name (_DEP, Package () { \_SB.GLNK, \_SB.IPC0 })
            Method (_STA, 0, NotSerialized) { Return (Zero) }
        }

        /* Phase 4: SOC Partition Provisioning (windows_silicon_qcom_kailua: QcSOCPartition.inf)
         * WARNING: Keep disabled while SanPolicy=4 / live UFS modemst1/2 protection is active! */
        Device (SOCP)
        {
            Name (_HID, "QCOM5EDD")
            Name (_CID, "QCOM06DD")
            Name (_UID, Zero)
            Method (_STA, 0, NotSerialized) { Return (Zero) }
        }

        /* Phase 4: Snapdragon X70 MPSS Subsystem (s23_live.dts: remoteproc-mss@04080000)
         *   PAS ID = 4, mpss_region = 0x8A800000 (264 MiB), q6_mpss_dtb = 0x9B000000 (512 KiB),
         *   mpss_dsm_region = 0xD4D00000 (51 MiB), WDOG SPI 0x108 (264) -> GSI 296 (0x128). */
        Device (AMSS)
        {
            Name (_HID, "QCOM0C1C")
            Name (_UID, Zero)
            Name (_CCA, Zero)
            Name (_DEP, Package () { \_SB.PILC, \_SB.RPEN, \_SB.GLNK, \_SB.IPC0, \_SB.TFTP })
            Method (_STA, 0, NotSerialized) { Return (Zero) }
            Name (_CRS, ResourceTemplate ()
            {
                Interrupt (ResourceConsumer, Edge, ActiveHigh, Exclusive, ,, WDOG) { 296 }
            })
        }

        /* Phase 5: IPA v5.0 + GSI Offload Engine (s23_live.dts: qcom,ipa@3e00000, PAS ID = 0x0F)
         *   IPA regs = 0x03E00000 (0x84000), GSI regs = 0x03E04000 (0xFC000),
         *   ipa-irq SPI 0x28E (654) -> GSI 686 (0x2AE),
         *   gsi-irq SPI 0x1B0 (432) -> GSI 464 (0x1D0),
         *   apps_smmu@15000000 SIDs 0x4A0 (AP), 0x4A1 (WLAN), 0x4A2 (uC), 0x4A4 (11ad). */
        Device (IPA)
        {
            Name (_HID, "QCOM0C6A")
            Name (_UID, Zero)
            Name (_CCA, One)
            Name (_DEP, Package () { \_SB.RPEN, \_SB.GLNK, \_SB.IPC0 })
            Method (_STA, 0, NotSerialized) { Return (Zero) }
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x03E00000, 0x00084000, IPAR)
                Interrupt (ResourceConsumer, Edge, ActiveHigh, Exclusive, ,, IPAI) { 686 }
                Memory32Fixed (ReadWrite, 0x03E04000, 0x000FC000, GSIR)
                Interrupt (ResourceConsumer, Edge, ActiveHigh, Exclusive, ,, GSII) { 464 }
            })
        }
    }
}
