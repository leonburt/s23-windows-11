/*
 * s23gpuprobe.c - Samsung Galaxy S23 (dm1q / SM8550-AC) Adreno 740 Kernel Probe v1.0.0.7
 *
 * Pure-C ARM64 WDM Demand-Start (Start = 3) diagnostic & opt-in bring-up driver.
 * Uses FILE_OPEN_IF on pre-allocated 64 KB C:\Users\Public\s23gpuprobe.log with
 * FILE_WRITE_THROUGH | FILE_SYNCHRONOUS_IO_NONALERT so every LogNL() flushes
 * directly to UFS before the next MMIO instruction executes.
 *
 * Registry control:
 *   HKLM\SYSTEM\CurrentControlSet\Services\s23gpuprobe\Parameters\StageMask (REG_DWORD)
 *   Safe default if missing: StageMask = 0x01 (read-only telemetry ONLY!)
 *
 * StageMask bits:
 *   0x01  : Stage 1 (Safe Read-Only Telemetry - DEFAULT):
 *           - CMD_DB (0x81C60000), ChipInfo (0x81CF4000), GCC (0x00100000), AXI-gated GPUCC/GMU/KGSL
 *   0x02  : Stage 2 (Opt-In Clock/Power Bring-Up & Live Adreno 740 ID/Status Read):
 *           - APPS_RSC DRV2 TCS0 RPMh ARC votes (cx, mx, mxc, gfx)
 *           - GCC_GPU_CFG_AHB + GPLL0 votes (BIT15|BIT16) + CX_GDSC + SMMU/MEMNOC/DVM/DDRSS
 *           - GPUCC HUB/FF/AHB/DEMET/CXO + PLL0 standby + PLL1 locked + GMU 220 MHz
 *   0x04  : Stage 3 (Opt-In Non-Destructive Scratch R/W & GX Power-Collapse Recovery Test)
 *   0x08  : Stage 4 (Read-Only GMU ITCM/DTCM, CX_MISC, RSCC, GMU_PDC Inspection)
 *   0x10  : Stage 5A (Read-Only ARM64 SCM IS_CALL_AVAIL & AXI-Gated KGSL-SMMU Telemetry)
 *   0x20  : Stage 5B (Opt-In KGSL-SMMU CB2 + GMU CM3 + HFI + OOB GX Wake)
 *   0x40  : Stage 6A (Opt-In KGSL-SMMU CB0 SID0/SID1 + QTB ECATS + A740 HwInit + Ring0/SQE Load)
 *   0x80  : Stage 6B (Opt-In CP_SQE_CNTL=1 + CP_ME_INIT fence=1 + CP_MEM_WRITE fence=2)
 *   0x100 : Stage 6C (Opt-In SCM CP_SMMU_APERTURE + ZAP PAS_ID=13 + CP_SET_SECURE_MODE(0) fence=3)
 */

#include <stdint.h>
#include <stddef.h>

typedef int32_t  NTSTATUS;
typedef uint16_t WCHAR;
typedef uint32_t ULONG;
typedef uint8_t  BOOLEAN;
typedef void    *PVOID;
typedef void    *HANDLE;

#define STATUS_SUCCESS               ((NTSTATUS)0x00000000L)
#define STATUS_UNSUCCESSFUL          ((NTSTATUS)0xC0000001L)
#define NT_SUCCESS(st)               (((NTSTATUS)(st)) >= 0)

#define OBJ_CASE_INSENSITIVE         0x00000040UL
#define OBJ_KERNEL_HANDLE            0x00000200UL
#define FILE_GENERIC_WRITE           0x00120116UL
#define FILE_SHARE_READ              0x00000001UL
#define FILE_SHARE_WRITE             0x00000002UL
#define FILE_OPEN_IF                 0x00000003UL
#define FILE_WRITE_THROUGH           0x00000002UL
#define FILE_SYNCHRONOUS_IO_NONALERT 0x00000020UL
#define FILE_ATTRIBUTE_NORMAL        0x00000080UL
#define KEY_READ                     0x00020019UL
#define KeyValuePartialInformation   2U
#define MmNonCached                  0U

typedef union _LARGE_INTEGER {
    struct { uint32_t LowPart; int32_t HighPart; };
    int64_t QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER, PHYSICAL_ADDRESS;

typedef struct _UNICODE_STRING {
    uint16_t Length;
    uint16_t MaximumLength;
    WCHAR   *Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _OBJECT_ATTRIBUTES {
    uint32_t        Length;
    uint32_t        Pad0;
    HANDLE          RootDirectory;
    PUNICODE_STRING ObjectName;
    uint32_t        Attributes;
    uint32_t        Pad1;
    PVOID           SecurityDescriptor;
    PVOID           SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

typedef struct _IO_STATUS_BLOCK {
    union { NTSTATUS Status; PVOID Pointer; };
    uintptr_t Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

typedef struct _KEY_VALUE_PARTIAL_INFORMATION {
    uint32_t TitleIndex;
    uint32_t Type;
    uint32_t DataLength;
    uint8_t  Data[16];
} KEY_VALUE_PARTIAL_INFORMATION;

struct _DRIVER_OBJECT;
typedef void (*PDRIVER_UNLOAD)(struct _DRIVER_OBJECT *DriverObject);

typedef struct _DRIVER_OBJECT {
    int16_t         Type;
    int16_t         Size;
    uint32_t        Pad0;
    PVOID           DeviceObject;
    uint32_t        Flags;
    uint32_t        Pad1;
    PVOID           DriverStart;
    uint32_t        DriverSize;
    uint32_t        Pad2;
    PVOID           DriverSection;
    PVOID           DriverExtension;
    UNICODE_STRING  DriverName;
    PUNICODE_STRING HardwareDatabase;
    PVOID           FastIoDispatch;
    PVOID           DriverInit;
    PVOID           DriverStartIo;
    PDRIVER_UNLOAD  DriverUnload;
    PVOID           MajorFunction[28];
} DRIVER_OBJECT, *PDRIVER_OBJECT;

__declspec(dllimport) PVOID    MmMapIoSpace(PHYSICAL_ADDRESS PhysicalAddress, size_t NumberOfBytes, uint32_t CacheType);
__declspec(dllimport) void     MmUnmapIoSpace(PVOID BaseAddress, size_t NumberOfBytes);
__declspec(dllimport) void     KeStallExecutionProcessor(uint32_t MicroSeconds);
__declspec(dllimport) NTSTATUS KeDelayExecutionThread(int8_t WaitMode, BOOLEAN Alertable, PLARGE_INTEGER Interval);
__declspec(dllimport) void     RtlInitUnicodeString(PUNICODE_STRING DestinationString, const WCHAR *SourceString);
__declspec(dllimport) NTSTATUS ZwCreateFile(
    HANDLE *FileHandle, uint32_t DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
    PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, uint32_t FileAttributes,
    uint32_t ShareAccess, uint32_t CreateDisposition, uint32_t CreateOptions,
    PVOID EaBuffer, uint32_t EaLength);
__declspec(dllimport) NTSTATUS ZwReadFile(
    HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, uint32_t Length,
    PLARGE_INTEGER ByteOffset, uint32_t *Key);
__declspec(dllimport) NTSTATUS ZwWriteFile(
    HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock, const void *Buffer, uint32_t Length,
    PLARGE_INTEGER ByteOffset, uint32_t *Key);
__declspec(dllimport) NTSTATUS ZwOpenKey(HANDLE *KeyHandle, uint32_t DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes);
__declspec(dllimport) NTSTATUS ZwQueryValueKey(
    HANDLE KeyHandle, PUNICODE_STRING ValueName, uint32_t KeyValueInformationClass,
    PVOID KeyValueInformation, uint32_t Length, uint32_t *ResultLength);
__declspec(dllimport) NTSTATUS ZwClose(HANDLE Handle);
__declspec(dllimport) PVOID    MmAllocateContiguousMemorySpecifyCache(
    size_t NumberOfBytes,
    PHYSICAL_ADDRESS LowestAcceptableAddress,
    PHYSICAL_ADDRESS HighestAcceptableAddress,
    PHYSICAL_ADDRESS BoundaryAddressMultiple,
    uint32_t CacheType);
__declspec(dllimport) void     MmFreeContiguousMemorySpecifyCache(
    PVOID BaseAddress, size_t NumberOfBytes, uint32_t CacheType);
__declspec(dllimport) PHYSICAL_ADDRESS MmGetPhysicalAddress(PVOID BaseAddress);

#ifndef FILE_GENERIC_READ
#define FILE_GENERIC_READ 0x00120089U
#endif
#ifndef FILE_OPEN
#define FILE_OPEN 0x00000001U
#endif
#ifndef STATUS_END_OF_FILE
#define STATUS_END_OF_FILE ((NTSTATUS)0xC0000011L)
#endif

/* ============================================================================
 * Physical Bases & Verified Register Offsets (S23 dm1q / SM8550-AC)
 * ============================================================================ */
#define PA_GCC            0x00100000ULL
#define SZ_GCC            0x001F5000UL

#define PA_KGSL           0x03D00000ULL
#define SZ_KGSL           0x00040000UL

#define PA_RSCC           0x03D50000ULL  /* G482_RSCC / s23_live.dts reg-names="rscc" */
#define SZ_RSCC           0x00010000UL

#define PA_GMU_CORE       0x03D6A000ULL  /* KGSL + 0x1A800*4; covers ITCM/DTCM/CM3/HFI/GENERAL/GMU_AO */
#define SZ_GMU_CORE       0x00026000UL   /* 0x03D6A000..0x03D8FFFF */

#define PA_GPUCC          0x03D90000ULL
#define SZ_GPUCC          0x0000A000UL

#define PA_CX_MISC        0x03D9E000ULL  /* G482_GMU_HIGH+0x4000 / s23_live.dts reg-names="cx_misc" */
#define SZ_CX_MISC        0x00001000UL

#define PA_KGSL_SMMU      0x03DA0000ULL  /* s23_live.dts kgsl-smmu@3da0000 */
#define SZ_KGSL_SMMU      0x00040000UL

#define PA_GPU_QTB        0x03DE8000ULL  /* s23_live.dts gpu_qtb@3de8000 */
#define SZ_GPU_QTB        0x00001000UL

#define PA_GMU_PDC        0x0B280000ULL  /* G482_PDC / s23_live.dts reg-names="gmu_pdc" */
#define SZ_GMU_PDC        0x00010000UL

#define PA_APPS_RSC_DRV2  0x17A20000ULL
#define SZ_APPS_RSC_DRV2  0x00002000UL

#define PA_CMD_DB         0x81C60000ULL
#define SZ_CMD_DB         0x00020000UL

#define PA_CHIPINFO       0x81CF4000ULL
#define SZ_CHIPINFO       0x00001000UL

#define PA_ZAP_REGION     0x9B09A000ULL  /* s23_live.dts gpu_micro_code_region@9b09a000 */
#define SZ_ZAP_REGION     0x00002000UL

/* GCC offsets (gcc-kalama.c) */
#define GCC_GPLL0_MODE             0x00000U
#define GCC_GPLL0_L_VAL            0x00004U
#define GCC_GPLL0_USER_CTL         0x00010U
#define GCC_APCS_CLK_BR_ENA_VOTE   0x52000U  /* BIT(15) = gcc_gpu_gpll0_clk_src */
#define GCC_APCS_GPLL_ENA_VOTE     0x52018U  /* BIT(0)  = GPLL0 */
#define GCC_GPU_BCR                0x71000U
#define GCC_GPU_CFG_AHB_CBCR       0x71004U
#define GCC_QMIP_GPU_AHB_CBCR      0x71008U
#define GCC_GPU_MEMNOC_GFX_CBCR    0x71010U
#define GCC_GPU_SNOC_DVM_GFX_CBCR  0x71018U
#define GCC_DDRSS_GPU_AXI_CBCR     0x71154U

/* GPUCC offsets (gpucc-kalama.c + s23_live.dts) */
#define GPUCC_PLL0_BASE            0x0000U
#define GPUCC_PLL1_BASE            0x1000U
#define PLL_OFF_MODE               0x00U
#define PLL_OFF_OPMODE             0x04U
#define PLL_OFF_STATE              0x08U
#define PLL_OFF_STATUS             0x0CU
#define PLL_OFF_L_VAL              0x10U
#define PLL_OFF_ALPHA_VAL          0x14U
#define PLL_OFF_USER_CTL           0x18U
#define PLL_OFF_USER_CTL_U         0x1CU
#define PLL_OFF_CONFIG_CTL         0x20U
#define PLL_OFF_CONFIG_CTL_U       0x24U
#define PLL_OFF_CONFIG_CTL_U1      0x28U
#define PLL_OFF_TEST_CTL           0x2CU
#define PLL_OFF_TEST_CTL_U         0x30U
#define PLL_OFF_TEST_CTL_U1        0x34U
#define PLL_OFF_TEST_CTL_U2        0x38U

#define GPUCC_HLOS1_VOTE_SMMU_CBCR 0x7000U
#define GPUCC_XO_BCR               0x9000U
#define GPUCC_CXO_AON_CBCR         0x9004U
#define GPUCC_FREQ_MEAS_CBCR       0x9008U
#define GPUCC_DEMET_CBCR           0x900CU
#define GPUCC_GX_BCR               0x9058U
#define GPUCC_GX_GDSCR             0x905CU
#define GPUCC_GX_CFG_GDSCR         0x9060U
#define GPUCC_GX_GFX3D_CBCR        0x90A8U
#define GPUCC_GX_VSENSE_CBCR       0x90B0U
#define GPUCC_GX_GMU_CBCR          0x90BCU
#define GPUCC_GX_FF_CBCR           0x90C0U
#define GPUCC_GX_GFX3D_RDVM_CBCR   0x90C8U
#define GPUCC_CX_BCR               0x9104U
#define GPUCC_CX_GDSCR             0x9108U
#define GPUCC_AHB_CBCR             0x911CU
#define GPUCC_CRC_AHB_CBCR         0x9120U
#define GPUCC_SLEEP_CBCR           0x9134U
#define GPUCC_CX_GMU_CBCR          0x913CU
#define GPUCC_CXO_CBCR             0x9144U
#define GPUCC_HUB_CX_INT_CBCR      0x9148U
#define GPUCC_CX_FF_CBCR           0x914CU
#define GPUCC_MEMNOC_GFX_CBCR      0x9150U
#define GPUCC_GFX3D_AON_BCR        0x9198U
#define GPUCC_MND1X_0_CBCR         0x9288U
#define GPUCC_MND1X_1_CBCR         0x928CU
#define GPUCC_GMU_BCR              0x9314U
#define GPUCC_GMU_RCG_CMD          0x9318U
#define GPUCC_GMU_RCG_CFG          0x931CU
#define GPUCC_ACD_BCR              0x9358U
#define GPUCC_HUB_AON_CBCR         0x93E8U
#define GPUCC_HUB_RCG_CMD          0x93ECU
#define GPUCC_HUB_RCG_CFG          0x93F0U
#define GPUCC_FF_RCG_CMD           0x9474U
#define GPUCC_FF_RCG_CFG           0x9478U
#define GPUCC_GX_DOMAIN_MISC3      0x9504U
#define GPUCC_CX_GDSC_HW_CTRL      0x953CU

/* GMU_CORE (0x03D6A000) byte offsets = (gen7_dword - 0x1A800) * 4 */
#define GMU_ITCM_OFF               0x03000U  /* 0x03D6D000 (0x1B400), 16 KB */
#define GMU_DTCM_OFF               0x07000U  /* 0x03D71000 (0x1C400), 16 KB */
#define GMU_DTCM_VERSION_OFF       0x0AFE0U  /* 0x03D74FE0 (G190_DTCM_VERSION) */
#define GMU_ICACHE_CONFIG          0x13000U  /* 0x03D7D000 */
#define GMU_DCACHE_CONFIG          0x13004U  /* 0x03D7D004 */
#define GMU_SYS_BUS_CONFIG         0x1303CU  /* 0x03D7D03C */
#define GMU_CM3_SYSRESET           0x14000U  /* 0x03D7E000 (0x1F800) */
#define GMU_CM3_BOOT_CONFIG        0x14004U  /* 0x03D7E004 (0x1F801) */
#define GMU_CM3_FW_BUSY            0x14068U  /* 0x03D7E068 (0x1F81A) */
#define GMU_CM3_FW_INIT_RESULT     0x14070U  /* 0x03D7E070 (0x1F81C) */
#define GMU_CM3_CFG                0x140B4U  /* 0x03D7E0B4 (0x1F82D) */
#define GMU_ALWAYS_ON_CNT_L        0x14220U  /* 0x03D7E220 (0x1F888) */
#define GMU_ALWAYS_ON_CNT_H        0x14224U  /* 0x03D7E224 (0x1F889) */
#define GMU_GFX_PWR_CLK_STATUS     0x14340U  /* 0x03D7E340 (0x1F8D0) */
#define GMU_RPMH_POWER_STATE       0x143B0U  /* 0x03D7E3B0 (0x1F8EC) */
#define GMU_RPMH_HYST_CTRL         0x143C0U  /* 0x03D7E3C0 */
#define GMU_RPMH_CTRL              0x143C4U  /* 0x03D7E3C4 */
#define GMU_HFI_CTRL_STATUS        0x14600U  /* 0x03D7E600 (0x1F980) */
#define GMU_HFI_QTBL_INFO          0x14610U  /* 0x03D7E610 (0x1F984) */
#define GMU_HFI_QTBL_ADDR          0x14614U  /* 0x03D7E614 (0x1F985) */
#define GMU_HFI_INIT               0x14618U  /* 0x03D7E618 (0x1F986) */
#define GMU_G2H_INTR_CLEAR         0x14644U  /* 0x03D7E644 (0x1F991) */
#define GMU_G2H_INTR_INFO          0x14648U  /* 0x03D7E648 (0x1F992) */
#define GMU_G2H_INTR_MASK          0x1464CU  /* 0x03D7E64C (0x1F993) */
#define GMU_H2G_SET                0x14650U  /* 0x03D7E650 (0x1F994) */
#define GMU_GENERAL_0              0x14714U  /* 0x03D7E714 (0x1F9C5) */
#define GMU_GENERAL_1              0x14718U  /* 0x03D7E718 (0x1F9C6) */
#define GMU_GENERAL_8              0x14734U  /* 0x03D7E734 (log IOVA) */
#define GMU_GENERAL_9              0x14738U  /* 0x03D7E738 */
#define GMU_GENERAL_10             0x1473CU  /* 0x03D7E73C (0x1F9CF) */
#define GMU_AO_HOST_INTR_CLR       0x24C10U  /* 0x03D8EC10 (0x23B04) */
#define GMU_AO_HOST_INTR_STATUS    0x24C14U  /* 0x03D8EC14 (0x23B05) */
#define GMU_AO_HOST_INTR_MASK      0x24C18U  /* 0x03D8EC18 (0x23B06) */
#define GMU_RSCC_CONTROL_REQ       0x24C1CU  /* 0x03D8EC1C (0x23B07) */
#define GMU_RSCC_CONTROL_ACK       0x24C20U  /* 0x03D8EC20 (0x23B08) */
#define GMU_AO_GPU_CX_BUSY_STATUS  0x24C30U  /* 0x03D8EC30 (0x23B0C) */
#define GMU_AO_GPU_CX_BUSY_STATUS2 0x24C34U  /* 0x03D8EC34 (0x23B0D) */
#define GMU_AO_AHB_FENCE_CTRL      0x24C40U  /* 0x03D8EC40 (0x23B10) */
#define GMU_AO_AHB_FENCE_RANGE_0   0x24C44U  /* 0x03D8EC44 (0x23B11) */
#define GMU_AO_AHB_FENCE_STATUS    0x24C4CU  /* 0x03D8EC4C (0x23B13) */
#define GMU_AO_SPARE_CNTL          0x24C58U  /* 0x03D8EC58 (0x23B16) */

/* CX_MISC (0x03D9E000), G482_RSCC (0x03D50000), GMU_PDC (0x0B280000) */
#define CX_MISC_TCM_RET_CNTL       0x00E4U   /* 0x03D9E0E4 (GEN7_GPU_CX_MISC_TCM_RET_CNTL) */

#define RSCC_RSC_STATUS0_DRV0      0x0010U   /* 0x03D50010 */
#define RSCC_PDC_SEQ_START_ADDR    0x0020U   /* 0x03D50020 */
#define RSCC_PDC_MATCH_VALUE_LO    0x0024U   /* 0x03D50024 */
#define RSCC_PDC_MATCH_VALUE_HI    0x0028U   /* 0x03D50028 */
#define RSCC_PDC_SLAVE_ID_DRV0     0x002CU   /* 0x03D5002C */
#define RSCC_OVERRIDE_START_ADDR   0x0400U   /* 0x03D50400 */
#define RSCC_SEQ_BUSY_DRV0         0x0404U   /* 0x03D50404 */
#define RSCC_SEQ_MEM_0_DRV0        0x0550U   /* 0x03D50550..0x03D50560 (GEN7_2_0_RSCC_SEQ_MEM_0_DRV0) */
#define RSCC_TCS0_DRV0_STATUS      0x0D28U   /* 0x03D50D28 */
#define RSCC_TCS1_DRV0_STATUS      0x0FC8U   /* 0x03D50FC8 */
#define RSCC_TCS2_DRV0_STATUS      0x1268U   /* 0x03D51268 */
#define RSCC_TCS3_DRV0_STATUS      0x1508U   /* 0x03D51508 */

#define PDC_GPU_STATUS_1024        0x1024U   /* 0x0B281024 */
#define PDC_GPU_ENABLE_PDC         0x4500U   /* 0x0B284500 */
#define PDC_GPU_MATCH_VALUE_LO     0x4510U   /* 0x0B284510 */
#define PDC_GPU_MATCH_VALUE_HI     0x4514U   /* 0x0B284514 */
#define PDC_GPU_SEQ_START_ADDR     0x4520U   /* 0x0B284520 */

/* KGSL (0x03D00000) byte offsets = gen7_dword * 4 (GX-domain) */
#define KGSL_RBBM_GBIF_HALT        (0x00016U * 4U)  /* 0x00058 */
#define KGSL_RBBM_GBIF_HALT_ACK    (0x00017U * 4U)  /* 0x0005C */
#define KGSL_RBBM_SP_HYST_CNT      (0x00042U * 4U)  /* 0x00108 (safe R/W scratch when idle) */
#define KGSL_RBBM_RAC_THRESH_CNT   (0x00044U * 4U)  /* 0x00110 (safe R/W scratch when idle) */
#define KGSL_RBBM_CLOCK_CNTL       (0x000AEU * 4U)  /* 0x002B8 */
#define KGSL_RBBM_INT_0_STATUS     (0x00201U * 4U)  /* 0x00804 */
#define KGSL_RBBM_STATUS           (0x00210U * 4U)  /* 0x00840 */
#define KGSL_RBBM_STATUS3          (0x00213U * 4U)  /* 0x0084C */
#define KGSL_CP_RB_RPTR            (0x00806U * 4U)  /* 0x02018 */
#define KGSL_CP_RB_WPTR            (0x00807U * 4U)  /* 0x0201C */
#define KGSL_CP_SQE_CNTL           (0x00808U * 4U)  /* 0x02020 */
#define KGSL_GBIF_SCACHE_CNTL0     (0x03C01U * 4U)  /* 0x0F004 */
#define KGSL_GBIF_HALT             (0x03C45U * 4U)  /* 0x0F114 */
#define KGSL_GBIF_HALT_ACK         (0x03C46U * 4U)  /* 0x0F118 */
#define KGSL_SECVID_TRUST_CNTL     (0x0F400U * 4U)  /* 0x3D000 */
#define KGSL_SECVID_TSB_CNTL       (0x0F803U * 4U)  /* 0x3E00C */

/* ============================================================================
 * Helpers: MMIO, Barriers, Timing
 * ============================================================================ */
static inline void InitObjAttr(POBJECT_ATTRIBUTES oa, PUNICODE_STRING name, uint32_t attr) {
    oa->Length = sizeof(OBJECT_ATTRIBUTES);
    oa->Pad0 = 0;
    oa->RootDirectory = NULL;
    oa->ObjectName = name;
    oa->Attributes = attr;
    oa->Pad1 = 0;
    oa->SecurityDescriptor = NULL;
    oa->SecurityQualityOfService = NULL;
}

static inline void dmb_sy(void) {
    __asm__ volatile("dmb sy" ::: "memory");
}

static inline uint32_t mmio_r32(volatile uint8_t *base, uint32_t off) {
    dmb_sy();
    uint32_t v = *(volatile uint32_t *)(base + off);
    dmb_sy();
    return v;
}

static inline uint64_t mmio_r64(volatile uint8_t *base, uint32_t off) {
    dmb_sy();
    uint64_t v = *(volatile uint64_t *)(base + off);
    dmb_sy();
    return v;
}

static inline void mmio_w32(volatile uint8_t *base, uint32_t off, uint32_t v) {
    dmb_sy();
    *(volatile uint32_t *)(base + off) = v;
    dmb_sy();
}

static inline void mmio_w64(volatile uint8_t *base, uint32_t off, uint64_t v) {
    dmb_sy();
    *(volatile uint64_t *)(base + off) = v;
    dmb_sy();
}

static void SleepMs(uint32_t ms) {
    if (ms == 0) return;
    LARGE_INTEGER li;
    li.QuadPart = -((int64_t)ms * 10000LL);
    KeDelayExecutionThread(0, 0, &li);
}

static void StallUs(uint32_t us) {
    while (us > 1000U) {
        KeStallExecutionProcessor(1000U);
        us -= 1000U;
    }
    if (us > 0U) {
        KeStallExecutionProcessor(us);
    }
}

/* ============================================================================
 * Pre-Allocated 64 KB Write-Through Log (C:\Users\Public\s23gpuprobe.log)
 * ============================================================================ */
static HANDLE   g_log_handle = NULL;
static int64_t  g_log_off    = 0;
static char     g_log_buf[4096];
static uint32_t g_log_pos    = 0;

static void LogOpen(void) {
    UNICODE_STRING u;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    RtlInitUnicodeString(&u, L"\\??\\C:\\Users\\Public\\s23gpuprobe.log");
    InitObjAttr(&oa, &u, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE);
    g_log_off = 0;
    g_log_pos = 0;
    if (!NT_SUCCESS(ZwCreateFile(
            &g_log_handle, FILE_GENERIC_WRITE, &oa, &iosb, NULL, FILE_ATTRIBUTE_NORMAL,
            FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN_IF,
            FILE_WRITE_THROUGH | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0))) {
        g_log_handle = NULL;
    }
}

static void LogFlush(void) {
    if (!g_log_handle || g_log_pos == 0) {
        g_log_pos = 0;
        return;
    }
    if (g_log_off + (int64_t)g_log_pos > 65000LL) {
        g_log_pos = 0;
        return;
    }
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER off;
    off.QuadPart = g_log_off;
    ZwWriteFile(g_log_handle, NULL, NULL, NULL, &iosb, g_log_buf, g_log_pos, &off, NULL);
    g_log_off += (int64_t)g_log_pos;
    g_log_pos = 0;
}

static void LogClose(void) {
    LogFlush();
    if (g_log_handle) {
        ZwClose(g_log_handle);
        g_log_handle = NULL;
    }
}

static void LogC(char c) {
    if (g_log_pos >= sizeof(g_log_buf) - 16U) {
        LogFlush();
    }
    if (g_log_pos < sizeof(g_log_buf)) {
        g_log_buf[g_log_pos++] = c;
    }
}

static void LogS(const char *s) {
    while (*s) LogC(*s++);
}

static void LogHex(uint64_t v, int digits) {
    static const char hex[] = "0123456789ABCDEF";
    for (int i = (digits - 1) * 4; i >= 0; i -= 4) {
        LogC(hex[(v >> i) & 0xFU]);
    }
}

static void LogDec(int64_t v) {
    if (v < 0) {
        LogC('-');
        /* magnitude taken as unsigned below: -INT64_MIN is undefined behaviour */
    }
    char tmp[24];
    int n = 0;
    uint64_t uv = (v < 0) ? (0ULL - (uint64_t)v) : (uint64_t)v;
    if (uv == 0) tmp[n++] = '0';
    while (uv > 0) {
        tmp[n++] = (char)('0' + (uv % 10U));
        uv /= 10U;
    }
    while (n > 0) LogC(tmp[--n]);
}

static void LogNL(void) {
    LogC('\r');
    LogC('\n');
    LogFlush();
}

static void LogReg(const char *name, uint32_t pa, uint32_t val) {
    LogS("  ");
    LogS(name);
    LogS(" (0x");
    LogHex(pa, 8);
    LogS(") = 0x");
    LogHex(val, 8);
    LogNL();
}

/* ============================================================================
 * Registry Reader (Safe Default StageMask = 0x01 Read-Only!)
 * ============================================================================ */
static uint32_t ReadStageMask(void) {
    UNICODE_STRING uk, uv;
    OBJECT_ATTRIBUTES oa;
    HANDLE hk = NULL;
    uint32_t val = 0x01U; /* SAFE DEFAULT: Read-Only Telemetry */

    RtlInitUnicodeString(&uk, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\s23gpuprobe\\Parameters");
    InitObjAttr(&oa, &uk, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE);
    if (!NT_SUCCESS(ZwOpenKey(&hk, KEY_READ, &oa))) {
        return val;
    }
    RtlInitUnicodeString(&uv, L"StageMask");
    KEY_VALUE_PARTIAL_INFORMATION kv;
    uint32_t rlen = 0;
    if (NT_SUCCESS(ZwQueryValueKey(hk, &uv, KeyValuePartialInformation, &kv, sizeof(kv), &rlen))) {
        if (kv.DataLength >= 4U) {
            val = *(uint32_t *)kv.Data;
        }
    }
    ZwClose(hk);
    return val;
}

/* ============================================================================
 * CMD_DB Parser (0x81C60000) & APPS_RSC DRV2 TCS0 (0x17A20000)
 * ============================================================================ */
static volatile uint8_t *g_cmddb    = NULL;
static volatile uint8_t *g_chipinfo = NULL;
static volatile uint8_t *g_rsc      = NULL;
static volatile uint8_t *g_gcc      = NULL;
static volatile uint8_t *g_gpucc    = NULL;
static volatile uint8_t *g_gmu      = NULL;
static volatile uint8_t *g_kgsl     = NULL;

typedef struct {
    uint32_t addr;
    uint16_t slv_id;
    uint16_t aux_len;
    uint16_t vlvls[16];
    uint32_t num_vlvls;
} CMD_DB_ARC_INFO;

static int CmdDbLookupArc(const char *name, CMD_DB_ARC_INFO *out) {
    out->addr = 0;
    out->slv_id = 0;
    out->aux_len = 0;
    out->num_vlvls = 0;
    if (!g_cmddb) return 0;
    if (g_cmddb[4] != 0xDBU || g_cmddb[5] != 0x30U || g_cmddb[6] != 0x03U || g_cmddb[7] != 0x0CU) {
        return 0;
    }
    uint32_t nlen = 0;
    while (name[nlen] && nlen < 8U) nlen++;

    for (uint32_t i = 0; i < 8U; i++) {
        const volatile uint8_t *r = g_cmddb + 8U + 16U * i;
        uint16_t slv_id   = *(const volatile uint16_t *)(r + 0);
        uint16_t hdr_off  = *(const volatile uint16_t *)(r + 2);
        uint16_t data_off = *(const volatile uint16_t *)(r + 4);
        uint16_t cnt      = *(const volatile uint16_t *)(r + 6);
        if (slv_id == 0U) break;
        if (144U + (uint32_t)hdr_off + 24U * (uint32_t)cnt > SZ_CMD_DB) continue;

        const volatile uint8_t *e = g_cmddb + 144U + hdr_off;
        for (uint32_t j = 0; j < cnt; j++, e += 24U) {
            int match = 1;
            for (uint32_t k = 0; k < 8U; k++) {
                uint8_t ch = (k < nlen) ? (uint8_t)name[k] : 0U;
                if (e[k] != ch) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                out->addr    = *(const volatile uint32_t *)(e + 16);
                out->aux_len = *(const volatile uint16_t *)(e + 20);
                uint16_t aoff = *(const volatile uint16_t *)(e + 22);
                out->slv_id  = slv_id;

                uint32_t abs_off = 144U + (uint32_t)data_off + (uint32_t)aoff;
                if (out->aux_len >= 2U && abs_off + out->aux_len <= SZ_CMD_DB) {
                    uint32_t max_u16 = out->aux_len >> 1;
                    if (max_u16 > 16U) max_u16 = 16U;
                    const volatile uint16_t *aux = (const volatile uint16_t *)(g_cmddb + abs_off);
                    uint32_t n = 0;
                    for (uint32_t idx = 0; idx < max_u16; idx++) {
                        uint16_t lv = aux[idx];
                        out->vlvls[idx] = lv;
                        if (idx > 0U && out->vlvls[idx - 1U] != 0U && lv == 0U) {
                            break;
                        }
                        n = idx + 1U;
                    }
                    out->num_vlvls = n;
                }
                return 1;
            }
        }
    }
    return 0;
}

static int FindArcLevelIndex(const CMD_DB_ARC_INFO *arc, uint16_t min_vlvl, uint32_t *idx_out, uint16_t *vlvl_out) {
    if (!arc || arc->addr == 0U || arc->num_vlvls == 0U) return 0;
    for (uint32_t i = 0; i < arc->num_vlvls; i++) {
        if (arc->vlvls[i] >= min_vlvl) {
            *idx_out = i;
            *vlvl_out = arc->vlvls[i];
            return 1;
        }
    }
    /* Fallback to highest available non-zero level */
    for (int i = (int)arc->num_vlvls - 1; i >= 0; i--) {
        if (arc->vlvls[i] != 0U) {
            *idx_out = (uint32_t)i;
            *vlvl_out = arc->vlvls[i];
            return 1;
        }
    }
    return 0;
}

#define RSC_TCS_OFF  0xD00U
#define V3_IRQ_EN    0x00U
#define V3_IRQ_ST    0x04U
#define V3_IRQ_CLR   0x08U
#define V3_CTRL      0x24U
#define V3_STATUS    0x28U
#define V3_CMDEN     0x2CU
#define V3_MSGID     0x34U
#define V3_ADDR      0x38U
#define V3_DATA      0x3CU
#define V3_CMDST     0x40U
#define AMC_ENABLE   (1U << 16)
#define AMC_TRIGGER  (1U << 24)

static inline uint32_t TcsOff(uint32_t id, uint32_t reg) {
    return RSC_TCS_OFF + 672U * id + reg;
}

static inline uint32_t TcsCmdOff(uint32_t id, uint32_t cmd, uint32_t reg) {
    return TcsOff(id, reg) + 24U * cmd;
}

/* Hybrid PASSIVE_LEVEL poll: fast 1 us stalls up to 200 us, then 1 ms sleeps up to max_ms */
static int WaitTcsBitHybrid(uint32_t reg_off, uint32_t mask, uint32_t want, uint32_t max_ms, uint32_t *waited_us_out) {
    for (uint32_t us = 0U; us < 200U; us++) {
        if ((mmio_r32(g_rsc, reg_off) & mask) == want) {
            if (waited_us_out) *waited_us_out = us;
            return 1;
        }
        KeStallExecutionProcessor(1U);
    }
    for (uint32_t ms = 0U; ms < max_ms; ms++) {
        SleepMs(1U);
        if ((mmio_r32(g_rsc, reg_off) & mask) == want) {
            if (waited_us_out) *waited_us_out = 200U + (ms + 1U) * 1000U;
            return 1;
        }
    }
    if (waited_us_out) *waited_us_out = 200U + max_ms * 1000U;
    return (mmio_r32(g_rsc, reg_off) & mask) == want;
}

static int RpmhWriteTcs0(const char *label, uint32_t addr, uint32_t data) {
    if (!g_rsc || addr == 0U) return 0;
    const uint32_t id = 0U;

    /* 1. Wait for TCS0_STATUS (+0x28 bit 0 == 1) idle before touching TCS0 */
    if (!WaitTcsBitHybrid(TcsOff(id, V3_STATUS), 1U, 1U, 10U, NULL)) {
        LogS("  RPMh["); LogS(label); LogS(" @ 0x"); LogHex(addr, 5);
        LogS("] ABORT: TCS0 busy (STATUS=0x"); LogHex(mmio_r32(g_rsc, TcsOff(id, V3_STATUS)), 8);
        LogS(")"); LogNL();
        return 0;
    }

    /* 2. Save IRQ_EN, CMDEN, and slot 0..3 command registers */
    uint32_t sv_irqen = mmio_r32(g_rsc, RSC_TCS_OFF + V3_IRQ_EN);
    uint32_t sv_cmden = mmio_r32(g_rsc, TcsOff(id, V3_CMDEN));
    uint32_t sv_msg[4], sv_addr[4], sv_data[4];
    for (uint32_t i = 0; i < 4U; i++) {
        sv_msg[i]  = mmio_r32(g_rsc, TcsCmdOff(id, i, V3_MSGID));
        sv_addr[i] = mmio_r32(g_rsc, TcsCmdOff(id, i, V3_ADDR));
        sv_data[i] = mmio_r32(g_rsc, TcsCmdOff(id, i, V3_DATA));
    }

    mmio_w32(g_rsc, RSC_TCS_OFF + V3_IRQ_CLR, 1U << id);
    if ((sv_irqen & (1U << id)) == 0U) {
        mmio_w32(g_rsc, RSC_TCS_OFF + V3_IRQ_EN, sv_irqen | (1U << id));
    }

    mmio_w32(g_rsc, TcsOff(id, V3_CMDEN), 0U);
    mmio_w32(g_rsc, TcsCmdOff(id, 0U, V3_MSGID), 8U | 0x100U | 0x10000U);
    mmio_w32(g_rsc, TcsCmdOff(id, 0U, V3_ADDR), addr);
    mmio_w32(g_rsc, TcsCmdOff(id, 0U, V3_DATA), data);
    mmio_w32(g_rsc, TcsOff(id, V3_CMDEN), 1U);

    uint32_t en = mmio_r32(g_rsc, TcsOff(id, V3_CTRL));
    en &= ~AMC_TRIGGER; mmio_w32(g_rsc, TcsOff(id, V3_CTRL), en); (void)mmio_r32(g_rsc, TcsOff(id, V3_CTRL));
    en &= ~AMC_ENABLE;  mmio_w32(g_rsc, TcsOff(id, V3_CTRL), en); (void)mmio_r32(g_rsc, TcsOff(id, V3_CTRL));
    en |= AMC_ENABLE;   mmio_w32(g_rsc, TcsOff(id, V3_CTRL), en); (void)mmio_r32(g_rsc, TcsOff(id, V3_CTRL));
    en |= AMC_TRIGGER;  mmio_w32(g_rsc, TcsOff(id, V3_CTRL), en);

    uint32_t wait_us = 0U;
    int irq_ok = WaitTcsBitHybrid(RSC_TCS_OFF + V3_IRQ_ST, 1U << id, 1U << id, 100U, &wait_us);
    uint32_t st    = mmio_r32(g_rsc, RSC_TCS_OFF + V3_IRQ_ST);
    uint32_t cmdst = mmio_r32(g_rsc, TcsCmdOff(id, 0U, V3_CMDST));
    int idle_ok    = WaitTcsBitHybrid(TcsOff(id, V3_STATUS), 1U, 1U, 10U, NULL);

    /* 3. Restore sv_msg/sv_addr/sv_data BEFORE restoring V3_CMDEN, only when TCS0 is idle */
    if (idle_ok) {
        mmio_w32(g_rsc, TcsOff(id, V3_CMDEN), 0U);
        for (uint32_t i = 0; i < 4U; i++) {
            mmio_w32(g_rsc, TcsCmdOff(id, i, V3_MSGID), sv_msg[i]);
            mmio_w32(g_rsc, TcsCmdOff(id, i, V3_ADDR),  sv_addr[i]);
            mmio_w32(g_rsc, TcsCmdOff(id, i, V3_DATA),  sv_data[i]);
        }
        mmio_w32(g_rsc, TcsOff(id, V3_CMDEN), sv_cmden);
    }
    mmio_w32(g_rsc, RSC_TCS_OFF + V3_IRQ_CLR, 1U << id);
    if (mmio_r32(g_rsc, RSC_TCS_OFF + V3_IRQ_EN) != sv_irqen) {
        mmio_w32(g_rsc, RSC_TCS_OFF + V3_IRQ_EN, sv_irqen);
    }

    LogS("  RPMh["); LogS(label); LogS(" @ 0x"); LogHex(addr, 5);
    LogS("] <- 0x"); LogHex(data, 4);
    LogS(" : irq=0x"); LogHex(st, 2);
    LogS(" cmdst=0x"); LogHex(cmdst, 2);
    LogS(" idle="); LogDec(idle_ok);
    LogS(" ("); LogDec(wait_us); LogS(" us)"); LogNL();
    return irq_ok && idle_ok;
}

/* ============================================================================
 * Strict 3-Tier AXI Safety Gates (Prevents Synchronous External Abort / SError)
 * ============================================================================ */
static int GpuccGateOpen(void) {
    if (!g_gcc) return 0;
    uint32_t bcr = mmio_r32(g_gcc, GCC_GPU_BCR);
    uint32_t cfg = mmio_r32(g_gcc, GCC_GPU_CFG_AHB_CBCR);
    if (bcr == 0xFFFFFFFFU || cfg == 0xFFFFFFFFU) return 0;
    if (bcr & 1U) return 0;
    /* Require CFG_AHB enabled AND not halted (or HWCG active with enable set) */
    if ((cfg & 1U) == 0U) return 0;
    if ((cfg & 0x80000000U) != 0U && (cfg & 2U) == 0U) return 0;
    return 1;
}

static int GpuCxAxiGateOpen(uint32_t *cx_gdscr_out, uint32_t *ahb_out) {
    if (!GpuccGateOpen() || !g_gpucc) return 0;
    uint32_t cx_bcr     = mmio_r32(g_gpucc, GPUCC_CX_BCR);
    uint32_t cx_gds     = mmio_r32(g_gpucc, GPUCC_CX_GDSCR);
    uint32_t cx_hw      = mmio_r32(g_gpucc, GPUCC_CX_GDSC_HW_CTRL);
    uint32_t ahb        = mmio_r32(g_gpucc, GPUCC_AHB_CBCR);
    uint32_t hub_aon    = mmio_r32(g_gpucc, GPUCC_HUB_AON_CBCR);
    uint32_t hub_cx_int = mmio_r32(g_gpucc, GPUCC_HUB_CX_INT_CBCR);
    uint32_t cx_ff      = mmio_r32(g_gpucc, GPUCC_CX_FF_CBCR);
    uint32_t memnoc_gfx = mmio_r32(g_gpucc, GPUCC_MEMNOC_GFX_CBCR);
    uint32_t cxo        = mmio_r32(g_gpucc, GPUCC_CXO_CBCR);
    uint32_t cx_gmu     = mmio_r32(g_gpucc, GPUCC_CX_GMU_CBCR);
    if (cx_gdscr_out) *cx_gdscr_out = cx_gds;
    if (ahb_out)      *ahb_out      = ahb;
    if (cx_bcr == 0xFFFFFFFFU || cx_gds == 0xFFFFFFFFU || ahb == 0xFFFFFFFFU) return 0;
    if (cx_bcr & 1U) return 0;
    /* Per G290GpuPower.c (G530 StockAhbRawReady & G721 Flags=64 ahb-not-sticky):
     * On SM8550, 0x03D9911C (gpu_cc_ahb_clk) has non-sticky bit 0 and is ready when
     * ((ahb >> 28) == 0 || (ahb >> 28) == 2) AND CX_GDSCR + CX_HW_CTRL + HUB/CX/GMU branches are ON. */
    int cx_on  = ((cx_gds & 0x80000001U) == 0x80000000U) &&
                 ((cx_hw != 0xFFFFFFFFU) && ((cx_hw & 0x80000000U) != 0U));
    int ahb_ok = (ahb != 0xFFFFFFFFU) && (((ahb >> 28) == 0U) || ((ahb >> 28) == 2U));
    int leaves_on = ((hub_aon    & 0x80000001U) == 1U) &&
                    ((hub_cx_int & 0x80000001U) == 1U) &&
                    ((cx_ff      & 0x80000001U) == 1U) &&
                    ((memnoc_gfx & 0x80000001U) == 1U) &&
                    ((cxo        & 0x80000001U) == 1U) &&
                    ((cx_gmu     & 0x80000001U) == 1U);
    return (cx_on && ahb_ok && leaves_on) ? 1 : 0;
}

static int GpuGxAxiGateOpen(uint32_t *gx_gdscr_out, uint32_t *gx_cfg_out) {
    if (!GpuCxAxiGateOpen(NULL, NULL)) return 0;
    uint32_t gx_bcr = mmio_r32(g_gpucc, GPUCC_GX_BCR);
    uint32_t gx_gds = mmio_r32(g_gpucc, GPUCC_GX_GDSCR);
    uint32_t gx_cfg = mmio_r32(g_gpucc, GPUCC_GX_CFG_GDSCR);
    if (gx_gdscr_out) *gx_gdscr_out = gx_gds;
    if (gx_cfg_out)   *gx_cfg_out   = gx_cfg;
    if (gx_bcr == 0xFFFFFFFFU || gx_gds == 0xFFFFFFFFU || gx_cfg == 0xFFFFFFFFU) return 0;
    if (gx_bcr & 1U) return 0;
    /* GX_GDSCR PWR_ON(bit31)==1, SW_COLLAPSE(bit0)==0, CFG PWR_DOWN_COMPLETE(bit15)==0 */
    return ((gx_gds & 0x80000001U) == 0x80000000U) && ((gx_cfg & (1U << 15)) == 0U);
}

/* ============================================================================
 * Clock / PLL / RCG / Branch Helpers (Verified vs gpucc-kalama.c & G736r2)
 * ============================================================================ */
static int PollReg(volatile uint8_t *base, uint32_t off, uint32_t mask, uint32_t expected, uint32_t timeout_us) {
    for (uint32_t t = 0; t <= timeout_us; t += 5U) {
        uint32_t v = mmio_r32(base, off);
        if (v == 0xFFFFFFFFU) return 0;
        if ((v & mask) == expected) return 1;
        StallUs(5U);
    }
    return 0;
}

static int EnableBranch(volatile uint8_t *base, uint32_t off, const char *name, int allow_hwcg, int delay_only) {
    uint32_t v = mmio_r32(base, off);
    if (v == 0xFFFFFFFFU) {
        LogS("  [ERR] "); LogS(name); LogS(" read 0xFFFFFFFF"); LogNL();
        return 0;
    }
    if ((v & 1U) == 0U) {
        mmio_w32(base, off, v | 1U);
    }
    if (delay_only) {
        StallUs(15U);
        v = mmio_r32(base, off);
        LogS("  CLK "); LogS(name); LogS(" -> 0x"); LogHex(v, 8); LogS(" (delay)"); LogNL();
        return (v != 0xFFFFFFFFU);
    }
    for (uint32_t t = 0; t <= 500U; t += 5U) {
        v = mmio_r32(base, off);
        if (v == 0xFFFFFFFFU) return 0;
        if ((v & 1U) && (((v & 0x80000000U) == 0U) || (allow_hwcg && (v & 2U)))) {
            LogS("  CLK "); LogS(name); LogS(" -> 0x"); LogHex(v, 8); LogS(" (ON)"); LogNL();
            return 1;
        }
        StallUs(5U);
    }
    LogS("  [WARN] CLK "); LogS(name); LogS(" timeout val=0x"); LogHex(v, 8); LogNL();
    return 0;
}

static int ProgramRcg(volatile uint8_t *base, uint32_t cmd_off, uint32_t cfg_val, const char *name) {
    uint32_t cmd = mmio_r32(base, cmd_off);
    if (cmd == 0xFFFFFFFFU) return 0;
    int forced = 0;
    if ((cmd & 2U) == 0U) {
        mmio_w32(base, cmd_off, cmd | 2U);
        forced = 1;
    }
    if (!PollReg(base, cmd_off, 0x80000000U, 0U, 500U)) {
        LogS("  [WARN] RCG "); LogS(name); LogS(" ROOT_OFF timeout cmd=0x");
        LogHex(mmio_r32(base, cmd_off), 8); LogNL();
        return 0;
    }
    uint32_t cfg = mmio_r32(base, cmd_off + 4U);
    uint32_t new_cfg = (cfg & ~0x0010371FU) | cfg_val;
    if (new_cfg != cfg) {
        mmio_w32(base, cmd_off + 4U, new_cfg);
    }
    cmd = mmio_r32(base, cmd_off);
    mmio_w32(base, cmd_off, cmd | 1U);
    int ok = PollReg(base, cmd_off, 1U, 0U, 500U);
    if (forced) {
        cmd = mmio_r32(base, cmd_off);
        mmio_w32(base, cmd_off, cmd & ~2U);
    }
    LogS("  RCG "); LogS(name);
    LogS(" cmd=0x"); LogHex(mmio_r32(base, cmd_off), 8);
    LogS(" cfg=0x"); LogHex(mmio_r32(base, cmd_off + 4U), 8);
    LogS(ok ? " (OK)" : " (TIMEOUT)"); LogNL();
    return ok;
}

static int ConfigureLucidOlePllStandby(volatile uint8_t *gpucc, uint32_t pll_off, uint32_t l_val, uint32_t alpha_val, const char *name) {
    uint32_t mode = mmio_r32(gpucc, pll_off + PLL_OFF_MODE);
    if (mode == 0xFFFFFFFFU) return 0;

    /* Leave PLL configured in standby (MODE=0, OPMODE=0, USER_CTL=0) so GMU owns it */
    mmio_w32(gpucc, pll_off + PLL_OFF_MODE,     0U);
    mmio_w32(gpucc, pll_off + PLL_OFF_OPMODE,   0U);
    mmio_w32(gpucc, pll_off + PLL_OFF_USER_CTL, 0U);
    StallUs(2U);

    mmio_w32(gpucc, pll_off + PLL_OFF_L_VAL,         0x44440000U | (l_val & 0xFFFFU));
    mmio_w32(gpucc, pll_off + PLL_OFF_ALPHA_VAL,     alpha_val & 0xFFFFU);
    mmio_w32(gpucc, pll_off + PLL_OFF_CONFIG_CTL,    0x20485699U);
    mmio_w32(gpucc, pll_off + PLL_OFF_CONFIG_CTL_U,  0x00182261U);
    mmio_w32(gpucc, pll_off + PLL_OFF_CONFIG_CTL_U1, 0x82AA299CU);
    mmio_w32(gpucc, pll_off + PLL_OFF_USER_CTL_U,    0x00000005U);
    mmio_w32(gpucc, pll_off + PLL_OFF_TEST_CTL_U,    0x00000003U);
    mmio_w32(gpucc, pll_off + PLL_OFF_TEST_CTL_U1,   0x00009000U);
    mmio_w32(gpucc, pll_off + PLL_OFF_TEST_CTL_U2,   0x00000034U);
    dmb_sy();

    LogS("  PLL "); LogS(name);
    LogS(" standby-configured mode=0x"); LogHex(mmio_r32(gpucc, pll_off + PLL_OFF_MODE), 8);
    LogS(" op=0x"); LogHex(mmio_r32(gpucc, pll_off + PLL_OFF_OPMODE), 2);
    LogS(" user=0x"); LogHex(mmio_r32(gpucc, pll_off + PLL_OFF_USER_CTL), 8); LogNL();
    return 1;
}

static int ProgramLucidOlePll(volatile uint8_t *gpucc, uint32_t pll_off, uint32_t l_val, uint32_t alpha_val, const char *name) {
    uint32_t mode = mmio_r32(gpucc, pll_off + PLL_OFF_MODE);
    uint32_t op   = mmio_r32(gpucc, pll_off + PLL_OFF_OPMODE);
    uint32_t cur_l = mmio_r32(gpucc, pll_off + PLL_OFF_L_VAL);
    uint32_t cur_a = mmio_r32(gpucc, pll_off + PLL_OFF_ALPHA_VAL);
    uint32_t user  = mmio_r32(gpucc, pll_off + PLL_OFF_USER_CTL);

    if (mode != 0xFFFFFFFFU &&
        (mode & 0x80000005U) == 0x80000005U && op == 1U && (user & 1U) &&
        (cur_l & 0xFFFFU) == (l_val & 0xFFFFU) && (cur_a & 0xFFFFU) == (alpha_val & 0xFFFFU)) {
        LogS("  PLL "); LogS(name); LogS(" already locked (mode=0x");
        LogHex(mode, 8); LogS(")"); LogNL();
        return 1;
    }

    /* Lucid OLE safe sequence: clear OUTCTRL and enter STANDBY BEFORE modifying L/ALPHA/CFG/TEST */
    if (mode == 0xFFFFFFFFU) return 0;
    mmio_w32(gpucc, pll_off + PLL_OFF_MODE, mode & ~1U);  /* OUTCTRL off */
    mmio_w32(gpucc, pll_off + PLL_OFF_OPMODE, 0U);        /* STANDBY */
    StallUs(2U);

    /* Exact gpucc-kalama.c clk_lucid_ole_pll_configure + enable sequence */
    mmio_w32(gpucc, pll_off + PLL_OFF_L_VAL,         0x44440000U | (l_val & 0xFFFFU));
    mmio_w32(gpucc, pll_off + PLL_OFF_ALPHA_VAL,     alpha_val & 0xFFFFU);
    mmio_w32(gpucc, pll_off + PLL_OFF_CONFIG_CTL,    0x20485699U);
    mmio_w32(gpucc, pll_off + PLL_OFF_CONFIG_CTL_U,  0x00182261U);
    mmio_w32(gpucc, pll_off + PLL_OFF_CONFIG_CTL_U1, 0x82AA299CU);
    mmio_w32(gpucc, pll_off + PLL_OFF_USER_CTL_U,    0x00000005U);
    mmio_w32(gpucc, pll_off + PLL_OFF_TEST_CTL_U,    0x00000003U);
    mmio_w32(gpucc, pll_off + PLL_OFF_TEST_CTL_U1,   0x00009000U);
    mmio_w32(gpucc, pll_off + PLL_OFF_TEST_CTL_U2,   0x00000034U);

    mode = mmio_r32(gpucc, pll_off + PLL_OFF_MODE);
    if (mode == 0xFFFFFFFFU) return 0;
    mmio_w32(gpucc, pll_off + PLL_OFF_MODE, mode | 4U);   /* RESET_N high */
    mmio_w32(gpucc, pll_off + PLL_OFF_OPMODE, 1U);        /* RUN */

    if (!PollReg(gpucc, pll_off + PLL_OFF_MODE, 0x80000000U, 0x80000000U, 1500U)) {
        LogS("  [WARN] PLL "); LogS(name); LogS(" LOCK timeout mode=0x");
        LogHex(mmio_r32(gpucc, pll_off + PLL_OFF_MODE), 8); LogNL();
        return 0;
    }
    user = mmio_r32(gpucc, pll_off + PLL_OFF_USER_CTL);
    mmio_w32(gpucc, pll_off + PLL_OFF_USER_CTL, user | 1U);
    mode = mmio_r32(gpucc, pll_off + PLL_OFF_MODE);
    mmio_w32(gpucc, pll_off + PLL_OFF_MODE, mode | 1U);   /* OUTCTRL on */

    mode = mmio_r32(gpucc, pll_off + PLL_OFF_MODE);
    LogS("  PLL "); LogS(name); LogS(" LOCKED mode=0x"); LogHex(mode, 8);
    LogS(" op=0x"); LogHex(mmio_r32(gpucc, pll_off + PLL_OFF_OPMODE), 2);
    LogS(" user=0x"); LogHex(mmio_r32(gpucc, pll_off + PLL_OFF_USER_CTL), 8); LogNL();
    return ((mode & 0x80000005U) == 0x80000005U);
}

/* ============================================================================
 * Telemetry Dumpers
 * ============================================================================ */
static void DumpGpuccTelemetry(void) {
    LogS("--- GPUCC (0x03D90000) Telemetry ---"); LogNL();
    if (!GpuccGateOpen()) {
        LogS("  [GATE CLOSED] GCC_GPU_BCR/CFG_AHB not active; skipping 0x03D90000 read (SError guard)."); LogNL();
        return;
    }
    if (!g_gpucc) return;
    LogReg("GPUCC_PLL0_MODE",      0x03D90000U, mmio_r32(g_gpucc, GPUCC_PLL0_BASE + PLL_OFF_MODE));
    LogReg("GPUCC_PLL0_OPMODE",    0x03D90004U, mmio_r32(g_gpucc, GPUCC_PLL0_BASE + PLL_OFF_OPMODE));
    LogReg("GPUCC_PLL0_L_VAL",     0x03D90010U, mmio_r32(g_gpucc, GPUCC_PLL0_BASE + PLL_OFF_L_VAL));
    LogReg("GPUCC_PLL1_MODE",      0x03D91000U, mmio_r32(g_gpucc, GPUCC_PLL1_BASE + PLL_OFF_MODE));
    LogReg("GPUCC_PLL1_OPMODE",    0x03D91004U, mmio_r32(g_gpucc, GPUCC_PLL1_BASE + PLL_OFF_OPMODE));
    LogReg("GPUCC_PLL1_L_VAL",     0x03D91010U, mmio_r32(g_gpucc, GPUCC_PLL1_BASE + PLL_OFF_L_VAL));
    LogReg("GPUCC_HLOS1_SMMU",     0x03D97000U, mmio_r32(g_gpucc, GPUCC_HLOS1_VOTE_SMMU_CBCR));
    LogReg("GPUCC_CXO_AON",        0x03D99004U, mmio_r32(g_gpucc, GPUCC_CXO_AON_CBCR));
    LogReg("GPUCC_GX_BCR",         0x03D99058U, mmio_r32(g_gpucc, GPUCC_GX_BCR));
    LogReg("GPUCC_GX_GDSCR",       0x03D9905CU, mmio_r32(g_gpucc, GPUCC_GX_GDSCR));
    LogReg("GPUCC_GX_CFG_GDSCR",   0x03D99060U, mmio_r32(g_gpucc, GPUCC_GX_CFG_GDSCR));
    LogReg("GPUCC_GX_GFX3D_CBCR",  0x03D990A8U, mmio_r32(g_gpucc, GPUCC_GX_GFX3D_CBCR));
    LogReg("GPUCC_GX_GMU_CBCR",    0x03D990BCU, mmio_r32(g_gpucc, GPUCC_GX_GMU_CBCR));
    LogReg("GPUCC_GX_FF_CBCR",     0x03D990C0U, mmio_r32(g_gpucc, GPUCC_GX_FF_CBCR));
    LogReg("GPUCC_CX_BCR",         0x03D99104U, mmio_r32(g_gpucc, GPUCC_CX_BCR));
    LogReg("GPUCC_CX_GDSCR",       0x03D99108U, mmio_r32(g_gpucc, GPUCC_CX_GDSCR));
    LogReg("GPUCC_CX_HW_CTRL",     0x03D9953CU, mmio_r32(g_gpucc, GPUCC_CX_GDSC_HW_CTRL));
    LogReg("GPUCC_AHB_CBCR",       0x03D9911CU, mmio_r32(g_gpucc, GPUCC_AHB_CBCR));
    LogReg("GPUCC_CRC_AHB_CBCR",   0x03D99120U, mmio_r32(g_gpucc, GPUCC_CRC_AHB_CBCR));
    LogReg("GPUCC_CX_GMU_CBCR",    0x03D9913CU, mmio_r32(g_gpucc, GPUCC_CX_GMU_CBCR));
    LogReg("GPUCC_CXO_CBCR",       0x03D99144U, mmio_r32(g_gpucc, GPUCC_CXO_CBCR));
    LogReg("GPUCC_HUB_CX_INT",     0x03D99148U, mmio_r32(g_gpucc, GPUCC_HUB_CX_INT_CBCR));
    LogReg("GPUCC_CX_FF_CBCR",     0x03D9914CU, mmio_r32(g_gpucc, GPUCC_CX_FF_CBCR));
    LogReg("GPUCC_MEMNOC_GFX",     0x03D99150U, mmio_r32(g_gpucc, GPUCC_MEMNOC_GFX_CBCR));
    LogReg("GPUCC_GMU_RCG_CMD",    0x03D99318U, mmio_r32(g_gpucc, GPUCC_GMU_RCG_CMD));
    LogReg("GPUCC_GMU_RCG_CFG",    0x03D9931CU, mmio_r32(g_gpucc, GPUCC_GMU_RCG_CFG));
    LogReg("GPUCC_HUB_AON_CBCR",   0x03D993E8U, mmio_r32(g_gpucc, GPUCC_HUB_AON_CBCR));
    LogReg("GPUCC_HUB_RCG_CMD",    0x03D993ECU, mmio_r32(g_gpucc, GPUCC_HUB_RCG_CMD));
    LogReg("GPUCC_HUB_RCG_CFG",    0x03D993F0U, mmio_r32(g_gpucc, GPUCC_HUB_RCG_CFG));
    LogReg("GPUCC_FF_RCG_CMD",     0x03D99474U, mmio_r32(g_gpucc, GPUCC_FF_RCG_CMD));
    LogReg("GPUCC_FF_RCG_CFG",     0x03D99478U, mmio_r32(g_gpucc, GPUCC_FF_RCG_CFG));
    LogReg("GPUCC_GX_MISC3",       0x03D99504U, mmio_r32(g_gpucc, GPUCC_GX_DOMAIN_MISC3));
}

static void ProbeGmuAndKgslIfGatedOpen(void) {
    uint32_t cx_gds = 0, ahb = 0, gx_gds = 0, gx_cfg = 0;

    if (!GpuCxAxiGateOpen(&cx_gds, &ahb)) {
        LogS("--- GMU/KGSL AXI GATE CLOSED (CX_GDSCR=0x"); LogHex(cx_gds, 8);
        LogS(" AHB=0x"); LogHex(ahb, 8);
        LogS("): Refusing 0x03D6A000 / 0x03D00000 access (SError prevented). ---"); LogNL();
        return;
    }

    if (!g_gmu) {
        PHYSICAL_ADDRESS pa;
        pa.QuadPart = (int64_t)PA_GMU_CORE;
        g_gmu = (volatile uint8_t *)MmMapIoSpace(pa, SZ_GMU_CORE, MmNonCached);
    }
    if (g_gmu) {
        LogS("--- GMU_CX / GMU_AO Live Telemetry (CX Gate OPEN) ---"); LogNL();
        LogReg("GMU_CM3_SYSRESET",      0x03D7E000U, mmio_r32(g_gmu, GMU_CM3_SYSRESET));
        LogReg("GMU_CM3_BOOT_CONFIG",   0x03D7E004U, mmio_r32(g_gmu, GMU_CM3_BOOT_CONFIG));
        LogReg("GMU_CM3_FW_BUSY",       0x03D7E068U, mmio_r32(g_gmu, GMU_CM3_FW_BUSY));
        LogReg("GMU_CM3_FW_INIT_RES",   0x03D7E070U, mmio_r32(g_gmu, GMU_CM3_FW_INIT_RESULT));
        LogReg("GMU_CM3_CFG",           0x03D7E0B4U, mmio_r32(g_gmu, GMU_CM3_CFG));
        LogReg("GMU_AO_CNT_LO",         0x03D7E220U, mmio_r32(g_gmu, GMU_ALWAYS_ON_CNT_L));
        LogReg("GMU_AO_CNT_HI",         0x03D7E224U, mmio_r32(g_gmu, GMU_ALWAYS_ON_CNT_H));
        LogReg("GMU_GFX_PWR_CLK_ST",    0x03D7E340U, mmio_r32(g_gmu, GMU_GFX_PWR_CLK_STATUS));
        LogReg("GMU_RPMH_PWR_STATE",    0x03D7E3B0U, mmio_r32(g_gmu, GMU_RPMH_POWER_STATE));
        LogReg("GMU_HFI_CTRL_STATUS",   0x03D7E600U, mmio_r32(g_gmu, GMU_HFI_CTRL_STATUS));
        LogReg("GMU_G2H_INTR_INFO",     0x03D7E648U, mmio_r32(g_gmu, GMU_G2H_INTR_INFO));
        LogReg("GMU_GENERAL_0",         0x03D7E714U, mmio_r32(g_gmu, GMU_GENERAL_0));
        LogReg("GMU_GENERAL_10",        0x03D7E73CU, mmio_r32(g_gmu, GMU_GENERAL_10));
        LogReg("GMU_AO_HOST_INTR_ST",   0x03D8EC14U, mmio_r32(g_gmu, GMU_AO_HOST_INTR_STATUS));
        LogReg("GMU_AO_CX_BUSY_ST",     0x03D8EC30U, mmio_r32(g_gmu, GMU_AO_GPU_CX_BUSY_STATUS));
        LogReg("GMU_AO_CX_BUSY_ST2",    0x03D8EC34U, mmio_r32(g_gmu, GMU_AO_GPU_CX_BUSY_STATUS2));
        LogReg("GMU_AO_AHB_FENCE_CTL",  0x03D8EC40U, mmio_r32(g_gmu, GMU_AO_AHB_FENCE_CTRL));
        LogReg("GMU_AO_AHB_FENCE_ST",   0x03D8EC4CU, mmio_r32(g_gmu, GMU_AO_AHB_FENCE_STATUS));
        LogReg("GMU_AO_SPARE_CNTL",     0x03D8EC58U, mmio_r32(g_gmu, GMU_AO_SPARE_CNTL));
    }

    if (!GpuGxAxiGateOpen(&gx_gds, &gx_cfg)) {
        LogS("--- KGSL GX GATE CLOSED (GX_GDSCR=0x"); LogHex(gx_gds, 8);
        LogS(" GX_CFG=0x"); LogHex(gx_cfg, 8);
        LogS("): Refusing 0x03D00000 RBBM/GBIF read (SError prevented). ---"); LogNL();
        return;
    }

    if (!g_kgsl) {
        PHYSICAL_ADDRESS pa;
        pa.QuadPart = (int64_t)PA_KGSL;
        g_kgsl = (volatile uint8_t *)MmMapIoSpace(pa, SZ_KGSL, MmNonCached);
    }
    if (g_kgsl) {
        LogS("--- Adreno 740 KGSL (0x03D00000) Live Telemetry (CX+GX Gates OPEN) ---"); LogNL();
        LogS("  [NOTE] Adreno Gen7 identifies ChipID via SoCInfo/ChipInfo (0x07020100 = GEN7_2_1), not legacy RBBM_CHIP_ID."); LogNL();
        LogReg("RBBM_STATUS",           0x03D00840U, mmio_r32(g_kgsl, KGSL_RBBM_STATUS));
        LogReg("RBBM_STATUS3",          0x03D0084CU, mmio_r32(g_kgsl, KGSL_RBBM_STATUS3));
        LogReg("RBBM_INT_0_STATUS",     0x03D00804U, mmio_r32(g_kgsl, KGSL_RBBM_INT_0_STATUS));
        LogReg("RBBM_GBIF_HALT",        0x03D00058U, mmio_r32(g_kgsl, KGSL_RBBM_GBIF_HALT));
        LogReg("RBBM_GBIF_HALT_ACK",    0x03D0005CU, mmio_r32(g_kgsl, KGSL_RBBM_GBIF_HALT_ACK));
        LogReg("RBBM_SP_HYST_CNT",      0x03D00108U, mmio_r32(g_kgsl, KGSL_RBBM_SP_HYST_CNT));
        LogReg("RBBM_RAC_THRESH_CNT",   0x03D00110U, mmio_r32(g_kgsl, KGSL_RBBM_RAC_THRESH_CNT));
        LogReg("RBBM_CLOCK_CNTL",       0x03D002B8U, mmio_r32(g_kgsl, KGSL_RBBM_CLOCK_CNTL));
        LogReg("CP_RB_RPTR",            0x03D02018U, mmio_r32(g_kgsl, KGSL_CP_RB_RPTR));
        LogReg("CP_RB_WPTR",            0x03D0201CU, mmio_r32(g_kgsl, KGSL_CP_RB_WPTR));
        LogReg("CP_SQE_CNTL",           0x03D02020U, mmio_r32(g_kgsl, KGSL_CP_SQE_CNTL));
        LogReg("GBIF_SCACHE_CNTL0",     0x03D0F004U, mmio_r32(g_kgsl, KGSL_GBIF_SCACHE_CNTL0));
        LogReg("GBIF_HALT",             0x03D0F114U, mmio_r32(g_kgsl, KGSL_GBIF_HALT));
        LogReg("GBIF_HALT_ACK",         0x03D0F118U, mmio_r32(g_kgsl, KGSL_GBIF_HALT_ACK));
        LogReg("SECVID_TRUST_CNTL",     0x03D3D000U, mmio_r32(g_kgsl, KGSL_SECVID_TRUST_CNTL));
        LogReg("SECVID_TSB_CNTL",       0x03D3E00CU, mmio_r32(g_kgsl, KGSL_SECVID_TSB_CNTL));
    }
}

/* ============================================================================
 * Stage 0x01: Safe Read-Only Telemetry (Default)
 * ============================================================================ */
static CMD_DB_ARC_INFO g_arc_cx, g_arc_mx, g_arc_mxc, g_arc_gfx;
static int g_arcs_loaded = 0;

static void EnsureArcRailsLoaded(void) {
    if (g_arcs_loaded) return;
    (void)CmdDbLookupArc("cx.lvl",  &g_arc_cx);
    (void)CmdDbLookupArc("mx.lvl",  &g_arc_mx);
    (void)CmdDbLookupArc("mxc.lvl", &g_arc_mxc);
    (void)CmdDbLookupArc("gfx.lvl", &g_arc_gfx);
    g_arcs_loaded = 1;
}

static void EnsureGmuAndKgslMapped(void) {
    if (!g_gmu && GpuCxAxiGateOpen(NULL, NULL)) {
        PHYSICAL_ADDRESS pa;
        pa.QuadPart = (int64_t)PA_GMU_CORE;
        g_gmu = (volatile uint8_t *)MmMapIoSpace(pa, SZ_GMU_CORE, MmNonCached);
    }
    if (!g_kgsl && GpuGxAxiGateOpen(NULL, NULL)) {
        PHYSICAL_ADDRESS pa;
        pa.QuadPart = (int64_t)PA_KGSL;
        g_kgsl = (volatile uint8_t *)MmMapIoSpace(pa, SZ_KGSL, MmNonCached);
    }
}

static void DumpArcInfo(const char *name, CMD_DB_ARC_INFO *dst) {
    CMD_DB_ARC_INFO tmp;
    CMD_DB_ARC_INFO *p = dst ? dst : &tmp;
    if (CmdDbLookupArc(name, p)) {
        LogS("  CMD_DB["); LogS(name); LogS("] = 0x"); LogHex(p->addr, 5);
        LogS(" slv="); LogDec(p->slv_id);
        LogS(" aux_len="); LogDec(p->aux_len);
        LogS(" vlvls["); LogDec(p->num_vlvls); LogS("]:");
        for (uint32_t i = 0; i < p->num_vlvls; i++) {
            LogS(" "); LogHex(p->vlvls[i], 3);
        }
        LogNL();
    } else {
        LogS("  CMD_DB["); LogS(name); LogS("] = NOT FOUND"); LogNL();
    }
}

typedef struct {
    const char      *name;
    CMD_DB_ARC_INFO *dst;
} ARC_TABLE_ENTRY;

static const ARC_TABLE_ENTRY g_arc_table[] = {
    { "cx.lvl",   &g_arc_cx  },
    { "mx.lvl",   &g_arc_mx  },
    { "mxc.lvl",  &g_arc_mxc },
    { "gfx.lvl",  &g_arc_gfx },
    { "gx.lvl",   NULL       },
    { "gmxc.lvl", NULL       },
    { "mmcx.lvl", NULL       },
    { "xo.lvl",   NULL       },
};

static void RunStage1ReadOnly(void) {
    LogS("=== STAGE 0x01: Safe Read-Only GPU Telemetry ==="); LogNL();

    LogS("--- CMD_DB (0x81C60000) GPU ARC Rails ---"); LogNL();
    for (uint32_t i = 0; i < (uint32_t)(sizeof(g_arc_table) / sizeof(g_arc_table[0])); i++) {
        DumpArcInfo(g_arc_table[i].name, g_arc_table[i].dst);
    }
    g_arcs_loaded = 1;

    if (g_chipinfo) {
        LogS("--- ChipInfo Region (0x81CF4000) ---"); LogNL();
        for (uint32_t off = 0; off < 32U; off += 4U) {
            LogReg("CHIPINFO", 0x81CF4000U + off, mmio_r32(g_chipinfo, off));
        }
    }

    if (g_gcc) {
        LogS("--- GCC (0x00100000) GPU Clocks & GPLL0 ---"); LogNL();
        LogReg("GCC_GPLL0_MODE",         0x00100000U, mmio_r32(g_gcc, GCC_GPLL0_MODE));
        LogReg("GCC_GPLL0_L_VAL",        0x00100004U, mmio_r32(g_gcc, GCC_GPLL0_L_VAL));
        LogReg("GCC_GPLL0_USER_CTL",     0x00100010U, mmio_r32(g_gcc, GCC_GPLL0_USER_CTL));
        LogReg("GCC_APCS_CLK_VOTE",      0x00152000U, mmio_r32(g_gcc, GCC_APCS_CLK_BR_ENA_VOTE));
        LogReg("GCC_APCS_GPLL_VOTE",     0x00152018U, mmio_r32(g_gcc, GCC_APCS_GPLL_ENA_VOTE));
        LogReg("GCC_GPU_BCR",            0x00171000U, mmio_r32(g_gcc, GCC_GPU_BCR));
        LogReg("GCC_GPU_CFG_AHB_CBCR",   0x00171004U, mmio_r32(g_gcc, GCC_GPU_CFG_AHB_CBCR));
        LogReg("GCC_QMIP_GPU_AHB_CBCR",  0x00171008U, mmio_r32(g_gcc, GCC_QMIP_GPU_AHB_CBCR));
        LogReg("GCC_GPU_MEMNOC_GFX",     0x00171010U, mmio_r32(g_gcc, GCC_GPU_MEMNOC_GFX_CBCR));
        LogReg("GCC_GPU_SNOC_DVM_GFX",   0x00171018U, mmio_r32(g_gcc, GCC_GPU_SNOC_DVM_GFX_CBCR));
        LogReg("GCC_DDRSS_GPU_AXI",      0x00171154U, mmio_r32(g_gcc, GCC_DDRSS_GPU_AXI_CBCR));
    }

    DumpGpuccTelemetry();
    ProbeGmuAndKgslIfGatedOpen();
}

/* ============================================================================
 * Stage 0x02: Opt-In Clock/Power Bring-Up & Live Adreno 740 Hardware Read
 * ============================================================================ */
static int RunStage2BringUp(void) {
    LogS("=== STAGE 0x02: Opt-In GPU Clock/Power Bring-Up ==="); LogNL();
    if (!g_gcc || !g_gpucc) {
        LogS("  [ERR] Missing GCC or GPUCC mapping."); LogNL();
        return 0;
    }

    /* Self-contained Stage 2: ensure CMD_DB ARC addresses/VLVL tables are loaded even if Stage 1 skipped */
    EnsureArcRailsLoaded();

    /* 1. Ensure GCC_GPU_BCR is not asserted (observe-only safety rule per G736r2) */
    uint32_t bcr = mmio_r32(g_gcc, GCC_GPU_BCR);
    if (bcr == 0xFFFFFFFFU || (bcr & 1U)) {
        LogS("  [ABORT] GCC_GPU_BCR asserted or unreadable (0x"); LogHex(bcr, 8); LogS(")"); LogNL();
        return 0;
    }

    /* 2. RPMh ARC votes via APPS_RSC DRV2 TCS0 */
    if (!g_rsc) {
        PHYSICAL_ADDRESS pa;
        pa.QuadPart = (int64_t)PA_APPS_RSC_DRV2;
        g_rsc = (volatile uint8_t *)MmMapIoSpace(pa, SZ_APPS_RSC_DRV2, MmNonCached);
    }
    int gfx_voted = 0;
    if (g_rsc) {
        uint32_t idx = 0; uint16_t vlvl = 0;
        if (FindArcLevelIndex(&g_arc_cx, 0x100U, &idx, &vlvl)) {
            LogS("  Voting cx.lvl idx="); LogDec(idx); LogS(" (vlvl=0x"); LogHex(vlvl, 3); LogS(")"); LogNL();
            RpmhWriteTcs0("cx.lvl", g_arc_cx.addr, idx);
        }
        if (FindArcLevelIndex(&g_arc_mx, 0x100U, &idx, &vlvl)) {
            LogS("  Voting mx.lvl idx="); LogDec(idx); LogS(" (vlvl=0x"); LogHex(vlvl, 3); LogS(")"); LogNL();
            RpmhWriteTcs0("mx.lvl", g_arc_mx.addr, idx);
        }
        if (FindArcLevelIndex(&g_arc_mxc, 0x100U, &idx, &vlvl)) {
            LogS("  Voting mxc.lvl idx="); LogDec(idx); LogS(" (vlvl=0x"); LogHex(vlvl, 3); LogS(")"); LogNL();
            RpmhWriteTcs0("mxc.lvl", g_arc_mxc.addr, idx);
        }
        if (FindArcLevelIndex(&g_arc_gfx, 0x034U, &idx, &vlvl)) {
            LogS("  Voting gfx.lvl idx="); LogDec(idx); LogS(" (vlvl=0x"); LogHex(vlvl, 3); LogS(")"); LogNL();
            gfx_voted = RpmhWriteTcs0("gfx.lvl", g_arc_gfx.addr, idx);
        }
        SleepMs(2U);
    } else {
        LogS("  [WARN] APPS_RSC_DRV2 map failed; proceeding with existing boot rail levels."); LogNL();
    }

    /* 3. Enable GCC_GPU_CFG_AHB & GPLL0 votes */
    if (!EnableBranch(g_gcc, GCC_GPU_CFG_AHB_CBCR, "gcc_gpu_cfg_ahb_clk", 1, 0)) {
        LogS("  [ABORT] gcc_gpu_cfg_ahb_clk failed to enable."); LogNL();
        return 0;
    }
    /* Vote GPLL0 + gcc_gpu_gpll0_clk_src (BIT 15 of 0x52000) */
    uint32_t gpll_vote = mmio_r32(g_gcc, GCC_APCS_GPLL_ENA_VOTE);
    if ((gpll_vote & 1U) == 0U) {
        mmio_w32(g_gcc, GCC_APCS_GPLL_ENA_VOTE, gpll_vote | 1U);
    }
    if (!PollReg(g_gcc, GCC_GPLL0_MODE, 0x80000000U, 0x80000000U, 500U)) {
        LogS("  [ABORT] GCC GPLL0 not locked!"); LogNL();
        return 0;
    }
    uint32_t br_vote = mmio_r32(g_gcc, GCC_APCS_CLK_BR_ENA_VOTE);
    if ((br_vote & 0x00018000U) != 0x00018000U) {
        mmio_w32(g_gcc, GCC_APCS_CLK_BR_ENA_VOTE, br_vote | 0x00018000U);
        StallUs(10U);
    }
    LogReg("GCC_APCS_CLK_VOTE_AFTER", 0x00152000U, mmio_r32(g_gcc, GCC_APCS_CLK_BR_ENA_VOTE));

    /* 4. Enable GPUCC_CX_GDSC (0x03D99108) */
    uint32_t cx_gds = mmio_r32(g_gpucc, GPUCC_CX_GDSCR);
    if (cx_gds == 0xFFFFFFFFU) return 0;
    if (cx_gds & 2U) {
        LogS("  [WARN] GPUCC_CX_GDSCR has HW_CONTROL bit1 set (0x"); LogHex(cx_gds, 8); LogS(")"); LogNL();
    }
    if (cx_gds & 1U) {
        mmio_w32(g_gpucc, GPUCC_CX_GDSCR, cx_gds & ~1U);
        StallUs(2U);
    }
    int cx_ok = PollReg(g_gpucc, GPUCC_CX_GDSC_HW_CTRL, 0x80000000U, 0x80000000U, 1500U);
    if (!cx_ok) {
        cx_ok = PollReg(g_gpucc, GPUCC_CX_GDSCR, 0x80000000U, 0x80000000U, 1500U);
    }
    LogReg("GPUCC_CX_GDSCR_AFTER",   0x03D99108U, mmio_r32(g_gpucc, GPUCC_CX_GDSCR));
    LogReg("GPUCC_CX_HW_CTRL_AFTER", 0x03D9953CU, mmio_r32(g_gpucc, GPUCC_CX_GDSC_HW_CTRL));
    if (!cx_ok) {
        LogS("  [ABORT] GPUCC_CX_GDSC failed to power on."); LogNL();
        return 0;
    }

    /* 5. SMMU/NoC clocks (exact G736r2 ClockOrder: SMMU -> MEMNOC -> DVM -> DDRSS) */
    EnableBranch(g_gpucc, GPUCC_HLOS1_VOTE_SMMU_CBCR, "gpu_cc_hlos1_vote_gpu_smmu", 0, 0);
    EnableBranch(g_gcc,   GCC_GPU_MEMNOC_GFX_CBCR,    "gcc_gpu_memnoc_gfx_clk",     1, 0);
    EnableBranch(g_gcc,   GCC_GPU_SNOC_DVM_GFX_CBCR,  "gcc_gpu_snoc_dvm_gfx_clk",   0, 1);
    EnableBranch(g_gcc,   GCC_DDRSS_GPU_AXI_CBCR,     "gcc_ddrss_gpu_axi_clk",      0, 1);

    /* 6. HUB (200 MHz = GPLL0/3: src=5, hid=5, HW_CLK_CTRL=BIT20), FF (200 MHz), AHB/CRC_AHB */
    ProgramRcg(g_gpucc, GPUCC_HUB_RCG_CMD, (5U << 8) | 5U | 0x00100000U, "gpu_cc_hub_clk_src");
    EnableBranch(g_gpucc, GPUCC_HUB_AON_CBCR,    "gpu_cc_hub_aon_clk",    0, 0);
    EnableBranch(g_gpucc, GPUCC_HUB_CX_INT_CBCR, "gpu_cc_hub_cx_int_clk", 0, 0);
    EnableBranch(g_gpucc, GPUCC_CRC_AHB_CBCR,    "gpu_cc_crc_ahb_clk",    0, 0);
    EnableBranch(g_gpucc, GPUCC_AHB_CBCR,        "gpu_cc_ahb_clk",        0, 1);
    ProgramRcg(g_gpucc, GPUCC_FF_RCG_CMD,  (5U << 8) | 5U | 0x00100000U, "gpu_cc_ff_clk_src");
    EnableBranch(g_gpucc, GPUCC_CX_FF_CBCR,      "gpu_cc_cx_ff_clk",      0, 0);
    EnableBranch(g_gpucc, GPUCC_MEMNOC_GFX_CBCR, "gpu_cc_memnoc_gfx_clk", 0, 0);

    /* 7. Configure GPU_CC_PLL0 (standby for GMU ownership) & lock GPU_CC_PLL1 (440 MHz -> 220 MHz GMU CX) */
    (void)ConfigureLucidOlePllStandby(g_gpucc, GPUCC_PLL0_BASE, 0x1EU, 0xBAAAU, "gpu_cc_pll0");
    int pll1_ok = ProgramLucidOlePll(g_gpucc, GPUCC_PLL1_BASE, 0x16U, 0xEAAAU, "gpu_cc_pll1");

    /* 8. GMU RCG (220 MHz = PLL1/2: src=3, hid=3, HW_CLK_CTRL) & GMU CX branches */
    if (pll1_ok) {
        ProgramRcg(g_gpucc, GPUCC_GMU_RCG_CMD, (3U << 8) | 3U | 0x00100000U, "gpu_cc_gmu_clk_src");
    } else {
        ProgramRcg(g_gpucc, GPUCC_GMU_RCG_CMD, 0x00100001U, "gpu_cc_gmu_clk_src(xo)");
    }
    EnableBranch(g_gpucc, GPUCC_CXO_AON_CBCR, "gpu_cc_cxo_aon_clk", 0, 0);
    EnableBranch(g_gpucc, GPUCC_DEMET_CBCR,   "gpu_cc_demet_clk",   0, 0);
    EnableBranch(g_gpucc, GPUCC_CXO_CBCR,     "gpu_cc_cxo_clk",     0, 0);
    EnableBranch(g_gpucc, GPUCC_CX_GMU_CBCR,  "gpu_cc_cx_gmu_clk",  0, 0);

    /* 9. Preserve GMU exclusive control over GX GDSC (SW_COLLAPSE=1 on GPUCC_GX_GDSCR) */
    (void)gfx_voted;
    {
        uint32_t gx_gds = mmio_r32(g_gpucc, GPUCC_GX_GDSCR);
        if (gx_gds != 0xFFFFFFFFU) {
            if ((gx_gds & 1U) == 0U) {
                mmio_w32(g_gpucc, GPUCC_GX_GDSCR, gx_gds | 1U);
                (void)PollReg(g_gpucc, GPUCC_GX_CFG_GDSCR, (1U << 15), (1U << 15), 3000U);
            }
            LogReg("GPUCC_GX_GDSCR_AFTER",     0x03D9905CU, mmio_r32(g_gpucc, GPUCC_GX_GDSCR));
            LogReg("GPUCC_GX_CFG_GDSCR_AFTER", 0x03D99060U, mmio_r32(g_gpucc, GPUCC_GX_CFG_GDSCR));
        }
    }

    SleepMs(2U);
    ProbeGmuAndKgslIfGatedOpen();
    return GpuCxAxiGateOpen(NULL, NULL);
}

/* ============================================================================
 * Stage 0x04: Opt-In Non-Destructive Scratch R/W & GX Power-Collapse Self-Test
 * ============================================================================ */
static void RunStage3ScratchAndGxTest(void) {
    LogS("=== STAGE 0x04: Opt-In Non-Destructive Scratch & GX Collapse Test ==="); LogNL();

    /* Self-contained Stage 3: populate CMD_DB arcs and map GMU/KGSL independently if gates are open */
    EnsureArcRailsLoaded();
    EnsureGmuAndKgslMapped();

    if (!GpuCxAxiGateOpen(NULL, NULL) || !g_gmu) {
        LogS("  [SKIP] CX gate closed or GMU unmapped; refusing Stage 0x04."); LogNL();
        return;
    }

    /* 1. CX-domain GMU_GENERAL_0 scratch test (ONLY while CM3 is held in reset!) */
    uint32_t cm3_rst = mmio_r32(g_gmu, GMU_CM3_SYSRESET);
    if (cm3_rst == 1U) {
        uint32_t orig = mmio_r32(g_gmu, GMU_GENERAL_0);
        mmio_w32(g_gmu, GMU_GENERAL_0, 0xA7405231U);
        uint32_t r1 = mmio_r32(g_gmu, GMU_GENERAL_0);
        mmio_w32(g_gmu, GMU_GENERAL_0, 0x58BFADCEU);
        uint32_t r2 = mmio_r32(g_gmu, GMU_GENERAL_0);
        mmio_w32(g_gmu, GMU_GENERAL_0, orig);
        uint32_t r3 = mmio_r32(g_gmu, GMU_GENERAL_0);
        LogS("  [CX SCRATCH] GMU_GENERAL_0: orig=0x"); LogHex(orig, 8);
        LogS(" r1=0x"); LogHex(r1, 8); LogS(" r2=0x"); LogHex(r2, 8);
        LogS(" restored=0x"); LogHex(r3, 8);
        LogS((r1 == 0xA7405231U && r2 == 0x58BFADCEU && r3 == orig) ? " -> PASS" : " -> MISMATCH"); LogNL();
    } else {
        LogS("  [SKIP] GMU CM3_SYSRESET=0x"); LogHex(cm3_rst, 8); LogS(" (not in reset); skipping GMU_GENERAL_0 write."); LogNL();
    }

    /* 2. GX-domain RBBM scratch test (ONLY while GX gate open & CP_SQE_CNTL == 0) */
    if (!GpuGxAxiGateOpen(NULL, NULL) || !g_kgsl) {
        LogS("  [SKIP] GX gate closed or KGSL unmapped; skipping GX scratch & collapse test."); LogNL();
        return;
    }
    uint32_t sqe = mmio_r32(g_kgsl, KGSL_CP_SQE_CNTL);
    if (sqe != 0U) {
        LogS("  [SKIP] CP_SQE_CNTL=0x"); LogHex(sqe, 8); LogS(" != 0; skipping GX register writes."); LogNL();
        return;
    }

    uint32_t orig_sp  = mmio_r32(g_kgsl, KGSL_RBBM_SP_HYST_CNT);
    uint32_t orig_rac = mmio_r32(g_kgsl, KGSL_RBBM_RAC_THRESH_CNT);
    mmio_w32(g_kgsl, KGSL_RBBM_SP_HYST_CNT, 0x00005A3CU);
    uint32_t sp1 = mmio_r32(g_kgsl, KGSL_RBBM_SP_HYST_CNT);
    mmio_w32(g_kgsl, KGSL_RBBM_RAC_THRESH_CNT, 0x0000A5C3U);
    uint32_t rac1 = mmio_r32(g_kgsl, KGSL_RBBM_RAC_THRESH_CNT);
    LogS("  [GX SCRATCH] RBBM_SP_HYST_CNT: orig=0x"); LogHex(orig_sp, 8);
    LogS(" w=0x00005A3C r=0x"); LogHex(sp1, 8);
    LogS(" | RBBM_RAC_THRESH_CNT: orig=0x"); LogHex(orig_rac, 8);
    LogS(" w=0x0000A5C3 r=0x"); LogHex(rac1, 8); LogNL();

    /* 3. GX Power-Collapse & Recovery Cycle */
    LogS("  [GX COLLAPSE] Setting GPUCC_GX_GDSCR SW_COLLAPSE=1..."); LogNL();
    uint32_t gx = mmio_r32(g_gpucc, GPUCC_GX_GDSCR);
    mmio_w32(g_gpucc, GPUCC_GX_GDSCR, gx | 1U);
    int down_ok = PollReg(g_gpucc, GPUCC_GX_CFG_GDSCR, (1U << 15), (1U << 15), 3000U);
    LogS("  [GX COLLAPSE] down="); LogS(down_ok ? "OK" : "TIMEOUT");
    LogS(" GDSCR=0x"); LogHex(mmio_r32(g_gpucc, GPUCC_GX_GDSCR), 8);
    LogS(" CFG=0x"); LogHex(mmio_r32(g_gpucc, GPUCC_GX_CFG_GDSCR), 8); LogNL();

    SleepMs(2U);

    LogS("  [GX RECOVER] Clearing GPUCC_GX_GDSCR SW_COLLAPSE=0..."); LogNL();
    gx = mmio_r32(g_gpucc, GPUCC_GX_GDSCR);
    mmio_w32(g_gpucc, GPUCC_GX_GDSCR, gx & ~1U);
    int up_ok  = PollReg(g_gpucc, GPUCC_GX_GDSCR, 0x80000000U, 0x80000000U, 3000U);
    int cfg_ok = PollReg(g_gpucc, GPUCC_GX_CFG_GDSCR, (1U << 15), 0U, 3000U);
    uint32_t cx_hw_post = mmio_r32(g_gpucc, GPUCC_CX_GDSC_HW_CTRL);
    uint32_t gx_misc3   = mmio_r32(g_gpucc, GPUCC_GX_DOMAIN_MISC3);
    LogS("  [GX RECOVER] up="); LogS((up_ok && cfg_ok) ? "OK" : "TIMEOUT");
    LogS(" GDSCR=0x"); LogHex(mmio_r32(g_gpucc, GPUCC_GX_GDSCR), 8);
    LogS(" CFG=0x"); LogHex(mmio_r32(g_gpucc, GPUCC_GX_CFG_GDSCR), 8);
    LogS(" CX_HW_CTRL=0x"); LogHex(cx_hw_post, 8);
    LogS(" GX_MISC3=0x"); LogHex(gx_misc3, 8); LogNL();

    /* Re-verify / re-enable GX branch CBCRs after GX collapse/recovery before any KGSL MMIO */
    int gx_gmu_ok = 0, gx_ff_ok = 0, gx_gfx_ok = 0;
    if (up_ok && cfg_ok && (cx_hw_post != 0xFFFFFFFFU) && ((cx_hw_post & 0x80000000U) != 0U)) {
        gx_gmu_ok = EnableBranch(g_gpucc, GPUCC_GX_GMU_CBCR,   "gpu_cc_gx_gmu_clk(post)",   0, 0);
        gx_ff_ok  = EnableBranch(g_gpucc, GPUCC_GX_FF_CBCR,    "gpu_cc_gx_ff_clk(post)",    0, 0);
        gx_gfx_ok = EnableBranch(g_gpucc, GPUCC_GX_GFX3D_CBCR, "gpu_cc_gx_gfx3d_clk(post)", 0, 0);
    }

    if (up_ok && cfg_ok && gx_gmu_ok && gx_ff_ok && gx_gfx_ok &&
        (cx_hw_post != 0xFFFFFFFFU) && ((cx_hw_post & 0x80000000U) != 0U) &&
        GpuGxAxiGateOpen(NULL, NULL)) {
        uint32_t sp_post  = mmio_r32(g_kgsl, KGSL_RBBM_SP_HYST_CNT);
        uint32_t rac_post = mmio_r32(g_kgsl, KGSL_RBBM_RAC_THRESH_CNT);
        uint32_t rbbm_st  = mmio_r32(g_kgsl, KGSL_RBBM_STATUS);
        /* Restore original values */
        mmio_w32(g_kgsl, KGSL_RBBM_SP_HYST_CNT, orig_sp);
        mmio_w32(g_kgsl, KGSL_RBBM_RAC_THRESH_CNT, orig_rac);
        LogS("  [GX POST-RECOVER] RBBM_STATUS=0x"); LogHex(rbbm_st, 8);
        LogS(" SP_HYST(post)=0x"); LogHex(sp_post, 8);
        LogS(" RAC_THRESH(post)=0x"); LogHex(rac_post, 8);
        LogS(" (originals restored)"); LogNL();
    } else {
        LogS("  [WARN] GX gate/branches/CX_HW_CTRL not ready after collapse cycle; skipping post-recover KGSL read."); LogNL();
    }
}

/* ============================================================================
 * Stage 0x08: Read-Only GMU ITCM/DTCM + CX_MISC + G482_RSCC + GMU_PDC + GPUCC GX/ACD/MEMNOC
 * ============================================================================ */
static void RunStage4GmuRsccPdcDump(void) {
    LogS("=== STAGE 0x08: Read-Only GMU ITCM/DTCM + RSCC + GMU_PDC + GPUCC GX/ACD/MEMNOC ==="); LogNL();

    /* 1. GPUCC GX / ACD / MEMNOC detailed state (safe whenever GPUCC gate is open) */
    if (GpuccGateOpen() && g_gpucc) {
        LogS("--- GPUCC GX / ACD / MEMNOC / Aux State (0x03D90000) ---"); LogNL();
        LogReg("GPUCC_GX_BCR",           0x03D99058U, mmio_r32(g_gpucc, GPUCC_GX_BCR));
        LogReg("GPUCC_GX_GDSCR",         0x03D9905CU, mmio_r32(g_gpucc, GPUCC_GX_GDSCR));
        LogReg("GPUCC_GX_CFG_GDSCR",     0x03D99060U, mmio_r32(g_gpucc, GPUCC_GX_CFG_GDSCR));
        LogReg("GPUCC_GX_GFX3D_CBCR",    0x03D990A8U, mmio_r32(g_gpucc, GPUCC_GX_GFX3D_CBCR));
        LogReg("GPUCC_GX_VSENSE_CBCR",   0x03D990B0U, mmio_r32(g_gpucc, GPUCC_GX_VSENSE_CBCR));
        LogReg("GPUCC_GX_GMU_CBCR",      0x03D990BCU, mmio_r32(g_gpucc, GPUCC_GX_GMU_CBCR));
        LogReg("GPUCC_GX_FF_CBCR",       0x03D990C0U, mmio_r32(g_gpucc, GPUCC_GX_FF_CBCR));
        LogReg("GPUCC_GX_GFX3D_RDVM",    0x03D990C8U, mmio_r32(g_gpucc, GPUCC_GX_GFX3D_RDVM_CBCR));
        LogReg("GPUCC_GX_DOMAIN_MISC3",  0x03D99504U, mmio_r32(g_gpucc, GPUCC_GX_DOMAIN_MISC3));
        LogReg("GPUCC_ACD_BCR",          0x03D99358U, mmio_r32(g_gpucc, GPUCC_ACD_BCR));
        LogReg("GPUCC_GFX3D_AON_BCR",    0x03D99198U, mmio_r32(g_gpucc, GPUCC_GFX3D_AON_BCR));
        LogReg("GPUCC_MND1X_0_CBCR",     0x03D99288U, mmio_r32(g_gpucc, GPUCC_MND1X_0_CBCR));
        LogReg("GPUCC_MND1X_1_CBCR",     0x03D9928CU, mmio_r32(g_gpucc, GPUCC_MND1X_1_CBCR));
        LogReg("GPUCC_SLEEP_CBCR",       0x03D99134U, mmio_r32(g_gpucc, GPUCC_SLEEP_CBCR));
        LogReg("GPUCC_FREQ_MEAS_CBCR",   0x03D99008U, mmio_r32(g_gpucc, GPUCC_FREQ_MEAS_CBCR));
        LogReg("GPUCC_DEMET_CBCR",       0x03D9900CU, mmio_r32(g_gpucc, GPUCC_DEMET_CBCR));
        LogReg("GPUCC_MEMNOC_GFX_CBCR",  0x03D99150U, mmio_r32(g_gpucc, GPUCC_MEMNOC_GFX_CBCR));
        if (g_gcc) {
            LogReg("GCC_GPU_MEMNOC_GFX",   0x00171010U, mmio_r32(g_gcc, GCC_GPU_MEMNOC_GFX_CBCR));
            LogReg("GCC_GPU_SNOC_DVM_GFX", 0x00171018U, mmio_r32(g_gcc, GCC_GPU_SNOC_DVM_GFX_CBCR));
            LogReg("GCC_DDRSS_GPU_AXI",    0x00171154U, mmio_r32(g_gcc, GCC_DDRSS_GPU_AXI_CBCR));
        }
    } else {
        LogS("  [SKIP] GPUCC gate closed; skipping GPUCC GX/ACD/MEMNOC dump."); LogNL();
    }

    /* 2. Strict CX AXI gate required before touching ITCM, DTCM, CX_MISC, RSCC, or GMU_PDC */
    uint32_t cx_gds = 0, ahb = 0;
    if (!GpuCxAxiGateOpen(&cx_gds, &ahb)) {
        LogS("  [SKIP] CX AXI gate closed (CX_GDSCR=0x"); LogHex(cx_gds, 8);
        LogS(" AHB=0x"); LogHex(ahb, 8);
        LogS("); refusing ITCM/DTCM/RSCC/PDC reads (SError prevented)."); LogNL();
        return;
    }

    EnsureGmuAndKgslMapped();

    /* 2a. GPU CX_MISC (0x03D9E000) */
    PHYSICAL_ADDRESS pa;
    pa.QuadPart = (int64_t)PA_CX_MISC;
    volatile uint8_t *cx_misc = (volatile uint8_t *)MmMapIoSpace(pa, SZ_CX_MISC, MmNonCached);
    if (cx_misc) {
        LogS("--- GPU CX_MISC (0x03D9E000, G482_GMU_HIGH+0x4000) ---"); LogNL();
        LogReg("CX_MISC_TCM_RET_CNTL", 0x03D9E0E4U, mmio_r32(cx_misc, CX_MISC_TCM_RET_CNTL));
        MmUnmapIoSpace((PVOID)cx_misc, SZ_CX_MISC);
    } else {
        LogS("  [WARN] Failed to map CX_MISC @ 0x03D9E000"); LogNL();
    }

    /* 2b. GMU ITCM (0x03D6D000, 16 KB) & DTCM (0x03D71000, 16 KB) + GMU Control/HFI/RSCC-bridge */
    if (g_gmu) {
        LogS("--- GMU ITCM (0x03D6D000) & DTCM (0x03D71000) Window Status ---"); LogNL();
        uint32_t itcm_nz = 0, itcm_ff = 0;
        for (uint32_t i = 0; i < 4096U; i++) {
            uint32_t v = mmio_r32(g_gmu, GMU_ITCM_OFF + i * 4U);
            if (v != 0U) itcm_nz++;
            if (v == 0xFFFFFFFFU) itcm_ff++;
        }
        LogS("  ITCM[0..3] (0x03D6D000): 0x"); LogHex(mmio_r32(g_gmu, GMU_ITCM_OFF + 0x00U), 8);
        LogS(" 0x"); LogHex(mmio_r32(g_gmu, GMU_ITCM_OFF + 0x04U), 8);
        LogS(" 0x"); LogHex(mmio_r32(g_gmu, GMU_ITCM_OFF + 0x08U), 8);
        LogS(" 0x"); LogHex(mmio_r32(g_gmu, GMU_ITCM_OFF + 0x0CU), 8);
        LogS(" | scan(4096w): nz="); LogDec(itcm_nz); LogS(" ff="); LogDec(itcm_ff); LogNL();

        uint32_t dtcm_nz = 0, dtcm_ff = 0;
        for (uint32_t i = 0; i < 4096U; i++) {
            uint32_t v = mmio_r32(g_gmu, GMU_DTCM_OFF + i * 4U);
            if (v != 0U) dtcm_nz++;
            if (v == 0xFFFFFFFFU) dtcm_ff++;
        }
        LogS("  DTCM[0..3] (0x03D71000): 0x"); LogHex(mmio_r32(g_gmu, GMU_DTCM_OFF + 0x00U), 8);
        LogS(" 0x"); LogHex(mmio_r32(g_gmu, GMU_DTCM_OFF + 0x04U), 8);
        LogS(" 0x"); LogHex(mmio_r32(g_gmu, GMU_DTCM_OFF + 0x08U), 8);
        LogS(" 0x"); LogHex(mmio_r32(g_gmu, GMU_DTCM_OFF + 0x0CU), 8);
        LogS(" | scan(4096w): nz="); LogDec(dtcm_nz); LogS(" ff="); LogDec(dtcm_ff); LogNL();
        LogReg("GMU_DTCM_VERSION",       0x03D74FE0U, mmio_r32(g_gmu, GMU_DTCM_VERSION_OFF));

        LogS("--- GMU Cache / HFI / AO RSCC-Bridge (0x03D6A000) ---"); LogNL();
        LogReg("GMU_ICACHE_CONFIG",      0x03D7D000U, mmio_r32(g_gmu, GMU_ICACHE_CONFIG));
        LogReg("GMU_DCACHE_CONFIG",      0x03D7D004U, mmio_r32(g_gmu, GMU_DCACHE_CONFIG));
        LogReg("GMU_SYS_BUS_CONFIG",     0x03D7D03CU, mmio_r32(g_gmu, GMU_SYS_BUS_CONFIG));
        LogReg("GMU_RPMH_HYST_CTRL",     0x03D7E3C0U, mmio_r32(g_gmu, GMU_RPMH_HYST_CTRL));
        LogReg("GMU_RPMH_CTRL",          0x03D7E3C4U, mmio_r32(g_gmu, GMU_RPMH_CTRL));
        LogReg("GMU_HFI_QTBL_INFO",      0x03D7E610U, mmio_r32(g_gmu, GMU_HFI_QTBL_INFO));
        LogReg("GMU_HFI_QTBL_ADDR",      0x03D7E614U, mmio_r32(g_gmu, GMU_HFI_QTBL_ADDR));
        LogReg("GMU_G2H_INTR_MASK",      0x03D7E64CU, mmio_r32(g_gmu, GMU_G2H_INTR_MASK));
        LogReg("GMU_GENERAL_8(LOG)",     0x03D7E734U, mmio_r32(g_gmu, GMU_GENERAL_8));
        LogReg("GMU_GENERAL_9",          0x03D7E738U, mmio_r32(g_gmu, GMU_GENERAL_9));
        LogReg("GMU_AO_HOST_INTR_MASK",  0x03D8EC18U, mmio_r32(g_gmu, GMU_AO_HOST_INTR_MASK));
        LogReg("GMU_RSCC_CONTROL_REQ",   0x03D8EC1CU, mmio_r32(g_gmu, GMU_RSCC_CONTROL_REQ));
        LogReg("GMU_RSCC_CONTROL_ACK",   0x03D8EC20U, mmio_r32(g_gmu, GMU_RSCC_CONTROL_ACK));
        LogReg("GMU_AO_AHB_FENCE_RNG0",  0x03D8EC44U, mmio_r32(g_gmu, GMU_AO_AHB_FENCE_RANGE_0));
    }

    /* 2c. G482_RSCC (0x03D50000, verified s23_live.dts reg-names="rscc") */
    pa.QuadPart = (int64_t)PA_RSCC;
    volatile uint8_t *rscc = (volatile uint8_t *)MmMapIoSpace(pa, SZ_RSCC, MmNonCached);
    if (rscc) {
        LogS("--- GPU G482_RSCC (0x03D50000, s23_live.dts rscc) ---"); LogNL();
        LogReg("RSCC_RSC_STATUS0_DRV0",  0x03D50010U, mmio_r32(rscc, RSCC_RSC_STATUS0_DRV0));
        LogReg("RSCC_PDC_SEQ_START",     0x03D50020U, mmio_r32(rscc, RSCC_PDC_SEQ_START_ADDR));
        LogReg("RSCC_PDC_MATCH_LO",      0x03D50024U, mmio_r32(rscc, RSCC_PDC_MATCH_VALUE_LO));
        LogReg("RSCC_PDC_MATCH_HI",      0x03D50028U, mmio_r32(rscc, RSCC_PDC_MATCH_VALUE_HI));
        LogReg("RSCC_PDC_SLAVE_ID_DRV0", 0x03D5002CU, mmio_r32(rscc, RSCC_PDC_SLAVE_ID_DRV0));
        LogReg("RSCC_OVERRIDE_START",    0x03D50400U, mmio_r32(rscc, RSCC_OVERRIDE_START_ADDR));
        LogReg("RSCC_SEQ_BUSY_DRV0",     0x03D50404U, mmio_r32(rscc, RSCC_SEQ_BUSY_DRV0));
        for (uint32_t i = 0; i < 5U; i++) {
            LogS("  RSCC_SEQ_MEM_"); LogDec(i);
            LogS("_DRV0 (0x"); LogHex(0x03D50550U + i * 4U, 8);
            LogS(") = 0x"); LogHex(mmio_r32(rscc, RSCC_SEQ_MEM_0_DRV0 + i * 4U), 8); LogNL();
        }
        LogReg("RSCC_TCS0_DRV0_STATUS",  0x03D50D28U, mmio_r32(rscc, RSCC_TCS0_DRV0_STATUS));
        LogReg("RSCC_TCS1_DRV0_STATUS",  0x03D50FC8U, mmio_r32(rscc, RSCC_TCS1_DRV0_STATUS));
        LogReg("RSCC_TCS2_DRV0_STATUS",  0x03D51268U, mmio_r32(rscc, RSCC_TCS2_DRV0_STATUS));
        LogReg("RSCC_TCS3_DRV0_STATUS",  0x03D51508U, mmio_r32(rscc, RSCC_TCS3_DRV0_STATUS));
        MmUnmapIoSpace((PVOID)rscc, SZ_RSCC);
    } else {
        LogS("  [WARN] Failed to map G482_RSCC @ 0x03D50000"); LogNL();
    }

    /* 2d. GMU_PDC (0x0B280000, verified s23_live.dts reg-names="gmu_pdc") */
    pa.QuadPart = (int64_t)PA_GMU_PDC;
    volatile uint8_t *pdc = (volatile uint8_t *)MmMapIoSpace(pa, SZ_GMU_PDC, MmNonCached);
    if (pdc) {
        LogS("--- GPU GMU_PDC (0x0B280000, G482_PDC) ---"); LogNL();
        LogReg("PDC_GPU_STATUS_1024",    0x0B281024U, mmio_r32(pdc, PDC_GPU_STATUS_1024));
        LogReg("PDC_GPU_ENABLE_PDC",     0x0B284500U, mmio_r32(pdc, PDC_GPU_ENABLE_PDC));
        LogReg("PDC_GPU_MATCH_VALUE_LO", 0x0B284510U, mmio_r32(pdc, PDC_GPU_MATCH_VALUE_LO));
        LogReg("PDC_GPU_MATCH_VALUE_HI", 0x0B284514U, mmio_r32(pdc, PDC_GPU_MATCH_VALUE_HI));
        LogReg("PDC_GPU_SEQ_START_ADDR", 0x0B284520U, mmio_r32(pdc, PDC_GPU_SEQ_START_ADDR));
        MmUnmapIoSpace((PVOID)pdc, SZ_GMU_PDC);
    } else {
        LogS("  [WARN] Failed to map GMU_PDC @ 0x0B280000"); LogNL();
    }
}

/* ============================================================================
 * Stage 0x10 (Stage 5A): Read-Only ARM64 SCM + AXI-Gated KGSL-SMMU Telemetry
 * ============================================================================ */
static int ScmCall64(
    uint64_t fid, uint64_t arginfo,
    uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
    uint64_t *r0_out, uint64_t *r1_out, uint64_t *r2_out, uint64_t *r3_out)
{
    uint64_t next_x0 = fid, x1 = arginfo, x2 = a0, x3 = a1, x4 = a2, x5 = a3, x6 = 0, x7 = 0;
    uint64_t last_r0 = ~0ULL;
    uint32_t busy = 0;
    for (uint32_t tries = 0; tries < 10000U; tries++) {
        register uint64_t rx0 __asm__("x0") = next_x0;
        register uint64_t rx1 __asm__("x1") = x1;
        register uint64_t rx2 __asm__("x2") = x2;
        register uint64_t rx3 __asm__("x3") = x3;
        register uint64_t rx4 __asm__("x4") = x4;
        register uint64_t rx5 __asm__("x5") = x5;
        register uint64_t rx6 __asm__("x6") = x6;
        register uint64_t rx7 __asm__("x7") = x7;
        __asm__ volatile(
            "smc #0"
            : "+r"(rx0), "+r"(rx1), "+r"(rx2), "+r"(rx3), "+r"(rx4), "+r"(rx5), "+r"(rx6), "+r"(rx7)
            :
            : "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "cc", "memory"
        );
        last_r0 = rx0;
        if (rx0 == 1ULL) { /* QCOM_SCM_INTERRUPTED */
            next_x0 = 1ULL;
            x1 = rx1; x2 = rx2; x3 = rx3; x4 = rx4; x5 = rx5; x6 = rx6; x7 = rx7;
            continue;
        }
        if (rx0 == (uint64_t)(int64_t)-12 && busy < 20U) { /* QCOM_SCM_V2_EBUSY */
            busy++;
            SleepMs(30U);
            next_x0 = fid;
            x1 = arginfo; x2 = a0; x3 = a1; x4 = a2; x5 = a3; x6 = 0ULL; x7 = 0ULL;
            continue;
        }
        if (r0_out) *r0_out = rx0;
        if (r1_out) *r1_out = rx1;
        if (r2_out) *r2_out = rx2;
        if (r3_out) *r3_out = rx3;
        return 1;
    }
    if (r0_out) *r0_out = last_r0;
    if (r1_out) *r1_out = 0;
    if (r2_out) *r2_out = 0;
    if (r3_out) *r3_out = 0;
    return 0;
}

static int ProbeScmCallAvail(const char *label, uint64_t target_fid) {
    uint64_t r0 = ~0ULL, r1 = 0, r2 = 0, r3 = 0;
    uint64_t used_fid = 0xC2000601ULL; /* QCOM_SCM_INFO_IS_CALL_AVAIL (FAST64, SiP) */
    (void)ScmCall64(used_fid, 1ULL, target_fid, 0, 0, 0, &r0, &r1, &r2, &r3);
    if (r0 == ~0ULL) {
        used_fid = 0x42000601ULL; /* STD64 fallback if FAST64 unknown */
        (void)ScmCall64(used_fid, 1ULL, target_fid, 0, 0, 0, &r0, &r1, &r2, &r3);
    }
    LogS("  SCM IS_CALL_AVAIL["); LogS(label);
    LogS(" fid=0x"); LogHex(target_fid, 8);
    LogS(" via 0x"); LogHex(used_fid, 8);
    LogS("]: r0=0x"); LogHex(r0, 16);
    LogS(" avail=0x"); LogHex(r1, 16);
    LogNL();
    return (r0 == 0ULL && r1 != 0ULL) ? 1 : 0;
}

static void RunStage5AScmAndSmmu(uint32_t mask) {
    LogS("=== STAGE 0x10 (Stage 5A): Read-Only ARM64 SCM + KGSL-SMMU Telemetry ==="); LogNL();

    ProbeScmCallAvail("SET_CP_POOL_SIZE",  0x02000C1BULL);
    ProbeScmCallAvail("PAS_INIT_IMAGE",    0x02000201ULL);
    ProbeScmCallAvail("PAS_MEM_SETUP",     0x02000202ULL);
    ProbeScmCallAvail("PAS_AUTH_AND_RST",  0x02000205ULL);
    ProbeScmCallAvail("PAS_SHUTDOWN",      0x02000206ULL);
    int pas_supp_avail = ProbeScmCallAvail("PAS_IS_SUPPORTED", 0x02000207ULL);

    if (pas_supp_avail) {
        uint64_t r0 = ~0ULL, r1 = 0, r2 = 0, r3 = 0;
        uint64_t fid = 0x42000207ULL; /* STD64 QCOM_SCM_PIL_PAS_IS_SUPPORTED */
        (void)ScmCall64(fid, 1ULL, 13ULL, 0, 0, 0, &r0, &r1, &r2, &r3);
        if (r0 == ~0ULL) {
            fid = 0xC2000207ULL;
            (void)ScmCall64(fid, 1ULL, 13ULL, 0, 0, 0, &r0, &r1, &r2, &r3);
        }
        LogS("  SCM PAS_IS_SUPPORTED(PAS_ID=13 GPU ZAP via 0x"); LogHex(fid, 8);
        LogS("): r0=0x"); LogHex(r0, 16);
        LogS(" r1=0x"); LogHex(r1, 16);
        LogS(" r2=0x"); LogHex(r2, 16); LogNL();
    }

    /* Strict AXI-gated KGSL-SMMU (0x03DA0000) read-only dump */
    uint32_t cx_gds = 0, ahb = 0;
    if (!GpuCxAxiGateOpen(&cx_gds, &ahb) || !g_gpucc) {
        LogS("  [SKIP] CX AXI gate closed (CX_GDSCR=0x"); LogHex(cx_gds, 8);
        LogS(" AHB=0x"); LogHex(ahb, 8);
        LogS("); refusing 0x03DA0000 KGSL-SMMU read (SError prevented)."); LogNL();
        return;
    }

    uint32_t smmu_cbcr = mmio_r32(g_gpucc, GPUCC_HLOS1_VOTE_SMMU_CBCR);
    if (smmu_cbcr != 0xFFFFFFFFU && ((smmu_cbcr & 0x80000000U) != 0U || (smmu_cbcr & 1U) == 0U)) {
        if (mask & 0x02U) {
            (void)EnableBranch(g_gpucc, GPUCC_HLOS1_VOTE_SMMU_CBCR, "gpu_cc_hlos1_vote_gpu_smmu", 0, 0);
            (void)PollReg(g_gpucc, GPUCC_HLOS1_VOTE_SMMU_CBCR, 0x80000001U, 0x00000001U, 5000U);
            smmu_cbcr = mmio_r32(g_gpucc, GPUCC_HLOS1_VOTE_SMMU_CBCR);
        }
    }
    LogReg("GPUCC_HLOS1_SMMU(5A)", 0x03D97000U, smmu_cbcr);
    if (smmu_cbcr == 0xFFFFFFFFU || (smmu_cbcr & 0x80000000U) != 0U || (smmu_cbcr & 1U) == 0U) {
        LogS("  [SKIP] GPUCC_HLOS1_VOTE_SMMU_CBCR CLK_OFF=1; refusing 0x03DA0000 KGSL-SMMU read (SError prevented)."); LogNL();
        return;
    }

    PHYSICAL_ADDRESS pa;
    pa.QuadPart = (int64_t)PA_KGSL_SMMU;
    volatile uint8_t *smmu = (volatile uint8_t *)MmMapIoSpace(pa, SZ_KGSL_SMMU, MmNonCached);
    if (!smmu) {
        LogS("  [WARN] Failed to map KGSL-SMMU @ 0x03DA0000"); LogNL();
        return;
    }
    LogS("--- KGSL-SMMU (0x03DA0000, QSMMU-v500) Read-Only Dump ---"); LogNL();
    LogReg("SMMU_sCR0",   0x03DA0000U, mmio_r32(smmu, 0x0000U));
    LogReg("SMMU_sACR",   0x03DA0010U, mmio_r32(smmu, 0x0010U));
    LogReg("SMMU_sIDR0",  0x03DA0020U, mmio_r32(smmu, 0x0020U));
    LogReg("SMMU_sIDR1",  0x03DA0024U, mmio_r32(smmu, 0x0024U));
    LogReg("SMMU_sIDR2",  0x03DA0028U, mmio_r32(smmu, 0x0028U));
    LogReg("SMMU_sIDR7",  0x03DA003CU, mmio_r32(smmu, 0x003CU));
    LogReg("SMMU_sGFSR",  0x03DA0048U, mmio_r32(smmu, 0x0048U));

    uint32_t scr0_scan = mmio_r32(smmu, 0x0000U);
    uint32_t valid_smr = 0U;
    int cold_fault_default = ((scr0_scan & (1U << 10)) != 0U) ? 1 : 0;
    for (uint32_t i = 0U; i < 24U; i++) {
        uint32_t smr  = mmio_r32(smmu, 0x0800U + 4U * i);
        uint32_t s2cr = mmio_r32(smmu, 0x0C00U + 4U * i);
        if (smr & 0x80000000U) valid_smr++;
        if (smr != 0U || s2cr != 0x00020000U) {
            cold_fault_default = 0;
            LogS("  SMMU_SLOT["); LogDec((int64_t)i);
            LogS("]: SMR=0x"); LogHex(smr, 8);
            LogS(" S2CR=0x"); LogHex(s2cr, 8); LogNL();
        }
    }
    LogS("  SMMU_ROUTES: valid_smr="); LogDec((int64_t)valid_smr);
    LogS(" cold_fault_default="); LogDec((int64_t)cold_fault_default); LogNL();

    MmUnmapIoSpace((PVOID)smmu, SZ_KGSL_SMMU);
}

/* ============================================================================
 * Stage 0x20 (Stage 5B): Opt-In KGSL-SMMU CB2 (SID 5) + GMU CM3 + HFI + OOB
 * ============================================================================ */
static uint8_t g_gmu_fw_buf[131072];

static uint16_t ReadLe16U(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t ReadLe32U(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t Pm4OddParity(uint32_t v) {
    return (0x9669U >> (0xFU & (v ^ (v >> 4) ^ (v >> 8) ^ (v >> 12) ^
            (v >> 16) ^ (v >> 20) ^ (v >> 24) ^ (v >> 28)))) & 1U;
}

static uint32_t Pm4Type4(uint32_t reg_dw, uint32_t cnt) {
    return 0x40000000U | (cnt & 0x7FU) | (Pm4OddParity(cnt) << 7) |
           ((reg_dw & 0x3FFFFU) << 8) | (Pm4OddParity(reg_dw) << 27);
}

static uint32_t Pm4Type7(uint32_t op, uint32_t cnt) {
    return 0x70000000U | (cnt & 0x7FFFU) | (Pm4OddParity(cnt) << 15) |
           ((op & 0x7FU) << 16) | (Pm4OddParity(op) << 23);
}

static int PollMmioBit0Clear(volatile uint8_t *base, uint32_t off, uint32_t max_us) {
    for (uint32_t t = 0U; t <= max_us; t += 5U) {
        uint32_t v = mmio_r32(base, off);
        if (v != 0xFFFFFFFFU && (v & 1U) == 0U) return 1;
        StallUs(5U);
    }
    return 0;
}

static int ReadFwFile(const WCHAR *nt_path, uint32_t *out_len) {
    UNICODE_STRING u;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE hf = NULL;
    RtlInitUnicodeString(&u, nt_path);
    InitObjAttr(&oa, &u, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE);
    if (!NT_SUCCESS(ZwCreateFile(
            &hf, FILE_GENERIC_READ, &oa, &iosb, NULL, FILE_ATTRIBUTE_NORMAL,
            FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN,
            FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0))) {
        return 0;
    }
    uint32_t total = 0;
    LARGE_INTEGER off;
    off.QuadPart = 0;
    while (total < sizeof(g_gmu_fw_buf)) {
        NTSTATUS st = ZwReadFile(hf, NULL, NULL, NULL, &iosb,
                                 g_gmu_fw_buf + total, (uint32_t)(sizeof(g_gmu_fw_buf) - total),
                                 &off, NULL);
        if (st == STATUS_END_OF_FILE || iosb.Information == 0) break;
        if (!NT_SUCCESS(st)) {
            ZwClose(hf);
            return 0;
        }
        total += (uint32_t)iosb.Information;
        off.QuadPart = (int64_t)total;
    }
    ZwClose(hf);
    *out_len = total;
    return (total >= 16U) ? 1 : 0;
}

static void ZeroBuf32(volatile uint32_t *p, uint32_t words) {
    for (uint32_t i = 0; i < words; i++) p[i] = 0U;
}

static int HfiSendAndWaitReply(
    volatile uint8_t *gmu,
    volatile uint32_t *hfi_va,
    uint32_t seq,
    uint32_t msg_id,
    const uint32_t *payload,
    uint32_t words,
    const char *label)
{
    volatile uint32_t *qh0   = hfi_va + 6U;
    volatile uint32_t *qh1   = hfi_va + 6U + 12U;
    volatile uint32_t *data0 = hfi_va + 1024U;
    volatile uint32_t *data1 = hfi_va + 2048U;

    dmb_sy();
    uint32_t read0  = qh0[10];
    uint32_t write0 = qh0[11];
    dmb_sy();

    if (read0 >= 1024U || write0 >= 1024U || (read0 & 3U) || (write0 & 3U)) {
        LogS("  [ERR] HFI "); LogS(label); LogS(" bad CMDQ indices"); LogNL();
        return 0;
    }
    uint32_t count  = words + 1U;
    uint32_t padded = (count + 3U) & ~3U;
    if (((read0 - write0 - 1U) & 1023U) < padded) {
        LogS("  [ERR] HFI "); LogS(label); LogS(" no CMDQ space"); LogNL();
        return 0;
    }

    uint32_t hdr = (seq << 20) | (count << 8) | (msg_id & 0xFFU);
    data0[write0] = hdr;
    for (uint32_t i = 0U; i < words; i++) {
        data0[(write0 + 1U + i) & 1023U] = payload[i];
    }
    for (uint32_t i = count; i < padded; i++) {
        data0[(write0 + i) & 1023U] = 0xFAFAFAFAU;
    }
    dmb_sy();
    qh0[11] = (write0 + padded) & 1023U;
    dmb_sy();
    mmio_w32(gmu, GMU_H2G_SET, 1U);

    for (uint32_t poll = 0U; poll < 4000U; poll++) {
        dmb_sy();
        uint32_t r1 = qh1[10];
        uint32_t w1 = qh1[11];
        dmb_sy();
        if (r1 >= 1024U || w1 >= 1024U || (r1 & 3U) || (w1 & 3U)) {
            LogS("  [ERR] HFI "); LogS(label); LogS(" corrupt MSGQ indices"); LogNL();
            return 0;
        }
        if (r1 == w1) {
            qh1[8] = 1U;
            dmb_sy();
            StallUs(250U);
            continue;
        }

        uint32_t rhdr = data1[r1];
        uint32_t rcnt = (rhdr >> 8) & 0xFFU;
        uint32_t rpad = (rcnt + 3U) & ~3U;
        if (rcnt < 3U || rcnt > 19U || rpad > ((w1 - r1) & 1023U)) {
            LogS("  [ERR] HFI "); LogS(label); LogS(" bad MSGQ packet hdr=0x");
            LogHex(rhdr, 8); LogNL();
            return 0;
        }

        uint32_t pkt[19];
        for (uint32_t i = 0U; i < rcnt; i++) {
            pkt[i] = data1[(r1 + i) & 1023U];
        }
        dmb_sy();
        qh1[10] = (r1 + rpad) & 1023U;
        dmb_sy();

        uint32_t rid   = rhdr & 0xFFU;
        uint32_t rtype = (rhdr >> 16) & 0xFU;
        if (rid == 100U) {
            LogS("  [ERR] HFI async error msg "); LogS(label);
            LogS(" code=0x"); LogHex(pkt[2], 8); LogNL();
            return 0;
        }
        if (rtype != 1U) {
            LogS("  [ERR] HFI unexpected pkt type="); LogDec((int64_t)rtype); LogNL();
            return 0;
        }
        if ((pkt[1] >> 20) != seq) {
            continue;
        }
        if ((rid != 126U && rid != msg_id) || pkt[1] != hdr) {
            LogS("  [ERR] HFI reply mismatch rid="); LogDec((int64_t)rid);
            LogS(" echo=0x"); LogHex(pkt[1], 8); LogNL();
            return 0;
        }
        LogS("  HFI["); LogS(label);
        LogS(" id="); LogDec((int64_t)msg_id);
        LogS(" seq="); LogDec((int64_t)seq);
        LogS("] reply hdr=0x"); LogHex(rhdr, 8);
        LogS(" err=0x"); LogHex(pkt[2], 8); LogNL();
        return (pkt[2] == 0U) ? 1 : 0;
    }

    LogS("  [WARN] HFI "); LogS(label); LogS(" reply timeout."); LogNL();
    return 0;
}

static void DumpGmuHfiAndLogState(
    volatile uint8_t *gmu,
    volatile uint8_t *rscc,
    volatile uint8_t *pdc,
    volatile uint8_t *smmu,
    volatile uint32_t *hfi_va,
    volatile uint32_t *log_va,
    const char *tag)
{
    LogS("--- GMU/HFI/RSCC Telemetry ["); LogS(tag); LogS("] ---"); LogNL();
    if (hfi_va) {
        dmb_sy();
        volatile uint32_t *qh0   = hfi_va + 6U;
        volatile uint32_t *qh1   = hfi_va + 6U + 12U;
        volatile uint32_t *qh2   = hfi_va + 6U + 24U;
        volatile uint32_t *data2 = hfi_va + 3072U;
        uint32_t r0 = qh0[10], w0 = qh0[11];
        uint32_t r1 = qh1[10], w1 = qh1[11];
        uint32_t r2 = qh2[10], w2 = qh2[11];
        LogS("  HFI CMDQ r="); LogDec((int64_t)r0); LogS(" w="); LogDec((int64_t)w0);
        LogS(" | MSGQ r="); LogDec((int64_t)r1); LogS(" w="); LogDec((int64_t)w1);
        LogS(" | DBGQ r="); LogDec((int64_t)r2); LogS(" w="); LogDec((int64_t)w2); LogNL();
        if (r2 != w2 && r2 < 1024U && w2 < 1024U) {
            uint32_t avail = (w2 - r2) & 1023U;
            uint32_t dump_n = (avail > 16U) ? 16U : avail;
            LogS("  HFI DBGQ["); LogDec((int64_t)dump_n); LogS("]:");
            for (uint32_t i = 0U; i < dump_n; i++) {
                LogS(" 0x"); LogHex(data2[(r2 + i) & 1023U], 8);
            }
            LogNL();
        }
    }

    if (gmu) {
        LogReg("GMU_GENERAL_8",           0x03D7E734U, mmio_r32(gmu, GMU_GENERAL_8));
        LogReg("GMU_GENERAL_9",           0x03D7E738U, mmio_r32(gmu, GMU_GENERAL_9));
    }

    if (log_va) {
        dmb_sy();
        uint32_t nonzero = 0U;
        for (uint32_t i = 0U; i < 4096U; i++) {
            if (log_va[i] != 0U) nonzero++;
        }
        LogS("  GMU_LOG(0x60002000) nonzero_dwords="); LogDec((int64_t)nonzero); LogNL();
        uint32_t printed = 0U;
        for (uint32_t i = 0U; i < 4096U && printed < 32U; i++) {
            uint32_t v = log_va[i];
            if (v != 0U) {
                LogS("    LOG[0x"); LogHex(i, 4); LogS("]=0x"); LogHex(v, 8); LogNL();
                printed++;
            }
        }
    }

    if (gmu) {
        LogReg("GMU_CM3_FW_INIT_RESULT",  0x03D7E070U, mmio_r32(gmu, GMU_CM3_FW_INIT_RESULT));
        LogReg("GMU_CM3_FW_BUSY",         0x03D7E068U, mmio_r32(gmu, GMU_CM3_FW_BUSY));
        LogReg("GMU_CM3_CFG",             0x03D7E0B4U, mmio_r32(gmu, GMU_CM3_CFG));
        LogReg("GMU_GFX_PWR_CLK_STATUS",  0x03D7E340U, mmio_r32(gmu, GMU_GFX_PWR_CLK_STATUS));
        LogReg("GMU_RPMH_POWER_STATE",    0x03D7E3B0U, mmio_r32(gmu, GMU_RPMH_POWER_STATE));
        LogReg("GMU_G2H_INTR_INFO",       0x03D7E648U, mmio_r32(gmu, GMU_G2H_INTR_INFO));
        LogReg("GMU_AO_HOST_INTR_STATUS", 0x03D8EC14U, mmio_r32(gmu, GMU_AO_HOST_INTR_STATUS));
        LogReg("GMU_AHB_FENCE_STATUS",    0x03D8EC4CU, mmio_r32(gmu, GMU_AO_AHB_FENCE_STATUS));
    }

    if (rscc) {
        LogReg("RSCC_RSC_STATUS0_DRV0",   0x03D50010U, mmio_r32(rscc, RSCC_RSC_STATUS0_DRV0));
        LogReg("RSCC_SEQ_BUSY_DRV0",      0x03D50404U, mmio_r32(rscc, RSCC_SEQ_BUSY_DRV0));
        LogReg("RSCC_TCS0_DRV0_STATUS",   0x03D50D28U, mmio_r32(rscc, RSCC_TCS0_DRV0_STATUS));
        LogReg("RSCC_TCS1_DRV0_STATUS",   0x03D50FC8U, mmio_r32(rscc, RSCC_TCS1_DRV0_STATUS));
        LogReg("RSCC_TCS2_DRV0_STATUS",   0x03D51268U, mmio_r32(rscc, RSCC_TCS2_DRV0_STATUS));
        LogReg("RSCC_TCS3_DRV0_STATUS",   0x03D51508U, mmio_r32(rscc, RSCC_TCS3_DRV0_STATUS));
    }

    if (pdc) {
        LogReg("PDC_GPU_ENABLE_PDC",      0x0B284500U, mmio_r32(pdc, PDC_GPU_ENABLE_PDC));
        LogReg("PDC_GPU_STATUS_1024",     0x0B281024U, mmio_r32(pdc, PDC_GPU_STATUS_1024));
    }

    if (g_gpucc) {
        LogReg("GPUCC_PLL0_MODE",         0x03D90000U, mmio_r32(g_gpucc, GPUCC_PLL0_BASE + PLL_OFF_MODE));
        LogReg("GPUCC_PLL0_OPMODE",       0x03D90004U, mmio_r32(g_gpucc, GPUCC_PLL0_BASE + PLL_OFF_OPMODE));
        LogReg("GPUCC_PLL0_L_VAL",        0x03D90010U, mmio_r32(g_gpucc, GPUCC_PLL0_BASE + PLL_OFF_L_VAL));
        LogReg("GPUCC_GX_GDSCR",          0x03D9905CU, mmio_r32(g_gpucc, GPUCC_GX_GDSCR));
        LogReg("GPUCC_GX_CFG_GDSCR",      0x03D99060U, mmio_r32(g_gpucc, GPUCC_GX_CFG_GDSCR));
    }

    if (smmu) {
        LogReg("SMMU_sGFSR",              0x03DA0048U, mmio_r32(smmu, 0x0048U));
        LogReg("CB2_FSR",                 0x03DC2058U, mmio_r32(smmu, 0x22058U));
        LogReg("CB2_FAR_LO",              0x03DC2060U, mmio_r32(smmu, 0x22060U));
        LogReg("CB2_FAR_HI",              0x03DC2064U, mmio_r32(smmu, 0x22064U));
    }
}

typedef struct {
    uint32_t iova;
    uint32_t pages;
    uint32_t data_page_off;
} GMU_EXTENT_SPEC;

static const GMU_EXTENT_SPEC g_gmu_extents[8] = {
    { 0x00004000U, 17U,  0U }, /* ICACHE  0x11000 B (data +0x00000) */
    { 0x00100000U,  9U, 17U }, /* PREALLOC 0x09000 B (data +0x11000) */
    { 0x00200000U, 19U, 26U }, /* PREALLOC 0x13000 B (data +0x1A000) */
    { 0x00300000U, 28U, 45U }, /* PREALLOC 0x1C000 B (data +0x2D000) */
    { 0x60000000U,  1U, 73U }, /* PERSIST  0x01000 B (data +0x49000) */
    { 0x60001000U,  1U, 74U }, /* PERSIST  0x01000 B (data +0x4A000) */
    { 0x60002000U,  4U, 75U }, /* LOG      0x04000 B (data +0x4B000) */
    { 0x60006000U,  4U, 79U }, /* HFI      0x04000 B (data +0x4F000) */
};

static uint32_t g_hfi_msg_buf[160];

/* ============================================================================
 * Stage 0x40 / 0x80 / 0x100 (Stage 6A/6B/6C): CB0 + QTB + HwInit + SQE + ZAP
 * ============================================================================ */
static const uint32_t kA740Gbif[5][2] = {
    { 0x3C03U, 0x00071620U }, { 0x3C04U, 0x00071620U },
    { 0x3C05U, 0x00071620U }, { 0x3C06U, 0x00071620U },
    { 0x0011U, 0x02120212U },
};

static const uint32_t kA740AoHwcg[3][2] = {
    { 0x23B09U, 0x00020202U },
    { 0x23B0AU, 0x00010111U },
    { 0x23B0BU, 0x00005555U },
};

static const uint32_t kA740Hwcg[54][2] = {
    { 0x00B0U, 0x02222222U }, { 0x00B4U, 0x22022222U }, { 0x00BCU, 0x003CF3CFU }, { 0x00B8U, 0x00000080U },
    { 0x00C0U, 0x22222220U }, { 0x00C4U, 0x22222222U }, { 0x00C8U, 0x22222222U }, { 0x00CCU, 0x00222222U },
    { 0x00E0U, 0x77777777U }, { 0x00E4U, 0x77777777U }, { 0x00E8U, 0x77777777U }, { 0x00ECU, 0x00077777U },
    { 0x00D0U, 0x11111111U }, { 0x00D4U, 0x11111111U }, { 0x00D8U, 0x11111111U }, { 0x00DCU, 0x00011111U },
    { 0x010BU, 0x22222222U }, { 0x010CU, 0x00222222U }, { 0x0110U, 0x00000444U }, { 0x010FU, 0x00000222U },
    { 0x00F0U, 0x22222222U }, { 0x00F4U, 0x01002222U }, { 0x00F8U, 0x00002220U }, { 0x0100U, 0x44000F00U },
    { 0x0104U, 0x25222022U }, { 0x0105U, 0x00555555U }, { 0x0106U, 0x00000011U }, { 0x0107U, 0x00440044U },
    { 0x0108U, 0x04222222U }, { 0x0286U, 0x00000222U }, { 0x0285U, 0x00222222U }, { 0x0114U, 0x02222223U },
    { 0x0111U, 0x00222222U }, { 0x0288U, 0x00222222U }, { 0x0287U, 0x00002222U }, { 0x010AU, 0x00000000U },
    { 0x0116U, 0x04104004U }, { 0x0113U, 0x00000000U }, { 0x0109U, 0x00000000U }, { 0x0115U, 0x00000200U },
    { 0x0112U, 0x00000000U }, { 0x011BU, 0x00002222U }, { 0x011CU, 0x00000000U }, { 0x011DU, 0x00000000U },
    { 0x0284U, 0x55555552U }, { 0x012FU, 0x00000000U }, { 0x0260U, 0x00000222U }, { 0x00AEU, 0x8AA8AA82U },
    { 0x0533U, 0x00000182U }, { 0x0044U, 0x00000000U }, { 0x0042U, 0x00000000U }, { 0x0118U, 0x00000222U },
    { 0x0119U, 0x00000111U }, { 0x011AU, 0x00000555U },
};

static const uint32_t kA740Protect[45][2] = {
    { 0x0850U, 0x13FC0000U }, { 0x0851U, 0x0160050BU }, { 0x0852U, 0x8000050EU },
    { 0x0853U, 0x80000510U }, { 0x0854U, 0x80000534U }, { 0x0855U, 0x027405FBU },
    { 0x0856U, 0x87A40699U }, { 0x0857U, 0x802008A0U }, { 0x0858U, 0x809008ABU },
    { 0x0859U, 0x05C008D0U }, { 0x085AU, 0x81340900U }, { 0x085BU, 0x82C8098DU },
    { 0x085CU, 0x86F80A41U }, { 0x085DU, 0x80040DF0U }, { 0x085EU, 0x80000E01U },
    { 0x085FU, 0x80200E07U }, { 0x0860U, 0x830C3C00U }, { 0x0861U, 0x7FFC3CC4U },
    { 0x0862U, 0x873C8630U }, { 0x0863U, 0x80008E00U }, { 0x0864U, 0x80008E08U },
    { 0x0865U, 0x807C8E50U }, { 0x0866U, 0x8A008E80U }, { 0x0867U, 0x876C9624U },
    { 0x0868U, 0x80009E40U }, { 0x0869U, 0x80349E64U }, { 0x086AU, 0x861C9E78U },
    { 0x086BU, 0x873CA630U }, { 0x086CU, 0x8000AE02U }, { 0x086DU, 0x803CAE50U },
    { 0x086EU, 0x800CAE66U }, { 0x086FU, 0x800CAE6FU }, { 0x0870U, 0x800CB604U },
    { 0x0871U, 0xBFFCEC00U }, { 0x0872U, 0x7FFCFC00U }, { 0x0873U, 0x814D8400U },
    { 0x0874U, 0x00118454U }, { 0x0875U, 0xFFFD8459U }, { 0x0876U, 0xFFFDA459U },
    { 0x0877U, 0xFFFDC459U }, { 0x0878U, 0x910DF400U }, { 0x0879U, 0x01EDF844U },
    { 0x087AU, 0x8001F860U }, { 0x087BU, 0x80A9F878U }, { 0x087FU, 0x8001F8C0U },
};

static const uint32_t kA740PreemptRegs[16] = {
    0x0E09U, 0x0E0AU, 0x0E07U, 0x0E08U, 0x0E0BU, 0x0E0CU, 0x0E0DU, 0x0E0EU,
    0x0E17U, 0x0E01U, 0x8E08U, 0x8E28U, 0x8602U, 0x8E50U, 0x0E3AU, 0x0E19U,
};

static inline void KgslW32(uint32_t dw, uint32_t v) { mmio_w32(g_kgsl, dw * 4U, v); }
static inline uint32_t KgslR32(uint32_t dw) { return mmio_r32(g_kgsl, dw * 4U); }
static inline void GmuDwW32(uint32_t dw, uint32_t v) { mmio_w32(g_gmu, (dw - 0x1A800U) * 4U, v); }
static inline uint32_t GmuDwR32(uint32_t dw) { return mmio_r32(g_gmu, (dw - 0x1A800U) * 4U); }

static uint32_t ZapPh(uint32_t phoff, uint32_t idx, uint32_t field) {
    return ReadLe32U(g_gmu_fw_buf + phoff + idx * 32U + field * 4U);
}

static void RunStage6GpuCp(uint32_t mask, volatile uint8_t *smmu) {
    LogS("=== STAGE 0x40 (Stage 6A): CB0 (SID0/1) + QTB ECATS + A740 HwInit + SQE ==="); LogNL();

    if (!g_kgsl || !g_gmu || !smmu) {
        LogS("  [ERR] Missing KGSL/GMU/SMMU mapping for Stage 6."); LogNL();
        return;
    }

    uint32_t sqe_len = 0;
    if (!ReadFwFile(L"\\??\\C:\\Users\\Public\\a740_sqe.fw", &sqe_len) || sqe_len < 8U || (sqe_len - 4U) > 0x13000U) {
        LogS("  [SKIP] Invalid or missing C:\\Users\\Public\\a740_sqe.fw (len="); LogDec((int64_t)sqe_len); LogS(")."); LogNL();
        return;
    }
    uint32_t sqe_payload_len = sqe_len - 4U;
    LogS("  Loaded C:\\Users\\Public\\a740_sqe.fw: "); LogDec((int64_t)sqe_len);
    LogS(" B (payload="); LogDec((int64_t)sqe_payload_len); LogS(" B)"); LogNL();

    const uint32_t cb0 = 0x20000U;
    if (mmio_r32(smmu, cb0 + 0x000U) & 1U) {
        LogS("  [SKIP] KGSL-SMMU CB0 SCTLR already active."); LogNL();
        return;
    }
    if ((mmio_r32(smmu, 0x0800U) & 0x80000000U) || (mmio_r32(smmu, 0x0804U) & 0x80000000U)) {
        LogS("  [SKIP] KGSL-SMMU SMR[0] or SMR[1] already valid."); LogNL();
        return;
    }

    const uint32_t kCb0Pages = 34U;
    PHYSICAL_ADDRESS lo, hi, bound;
    lo.QuadPart = 0LL;
    hi.QuadPart = 0x0000000FFFFFFFFFLL;
    bound.QuadPart = 0LL;
    volatile uint32_t *cb0_pool_va = (volatile uint32_t *)MmAllocateContiguousMemorySpecifyCache(
        kCb0Pages * 4096U, lo, hi, bound, MmNonCached);
    if (!cb0_pool_va) {
        LogS("  [ERR] Failed allocating 34-page (<64GB) CB0 pool."); LogNL();
        return;
    }
    uint64_t cb0_pa = (uint64_t)MmGetPhysicalAddress((PVOID)cb0_pool_va).QuadPart;
    if ((cb0_pa & 0xFFFULL) != 0ULL || cb0_pa + (uint64_t)kCb0Pages * 4096ULL > (1ULL << 36)) {
        LogS("  [ERR] CB0 pool PA out of 36-bit OAS or unaligned: 0x"); LogHex(cb0_pa, 16); LogNL();
        MmFreeContiguousMemorySpecifyCache((PVOID)cb0_pool_va, kCb0Pages * 4096U, MmNonCached);
        return;
    }
    ZeroBuf32(cb0_pool_va, kCb0Pages * 1024U);

    /* Copy SQE payload (minus 4-byte header) into pages 4..22 and verify readback */
    volatile uint8_t *sqe_bytes_va = (volatile uint8_t *)cb0_pool_va + 4U * 4096U;
    for (uint32_t i = 0U; i < sqe_payload_len; i++) {
        sqe_bytes_va[i] = g_gmu_fw_buf[4U + i];
    }
    dmb_sy();
    for (uint32_t i = 0U; i < sqe_payload_len; i++) {
        if (sqe_bytes_va[i] != g_gmu_fw_buf[4U + i]) {
            LogS("  [ERR] SQE copy mismatch at byte 0x"); LogHex(i, 5); LogNL();
            MmFreeContiguousMemorySpecifyCache((PVOID)cb0_pool_va, kCb0Pages * 4096U, MmNonCached);
            return;
        }
    }

    /* Populate 3-level 4 KiB page tables for CB0 (39-bit IAS) */
    volatile uint64_t *pt_l1   = (volatile uint64_t *)((volatile uint8_t *)cb0_pool_va + 0x0000U);
    volatile uint64_t *pt_l2   = (volatile uint64_t *)((volatile uint8_t *)cb0_pool_va + 0x1000U);
    volatile uint64_t *pt_l3_0 = (volatile uint64_t *)((volatile uint8_t *)cb0_pool_va + 0x2000U);
    volatile uint64_t *pt_l3_1 = (volatile uint64_t *)((volatile uint8_t *)cb0_pool_va + 0x3000U);

    pt_l1[0] = (cb0_pa + 0x1000ULL) | 3ULL;
    pt_l2[0] = (cb0_pa + 0x2000ULL) | 3ULL;
    pt_l2[1] = (cb0_pa + 0x3000ULL) | 3ULL;

    const uint64_t kAttrSqeRx = 0x0000000000000EC3ULL; /* nG=1 AF=1 SH=2(OSH) RO+X NC */
    const uint64_t kAttrGpuRw = 0x0060000000000E43ULL; /* UXN|PXN nG=1 AF=1 SH=2(OSH) RW NC */

    for (uint32_t p = 0U; p < 19U; p++) {
        pt_l3_0[0x100U + p] = (cb0_pa + (4ULL + (uint64_t)p) * 4096ULL) | kAttrSqeRx;
    }
    for (uint32_t p = 0U; p < 8U; p++) {
        pt_l3_1[p] = (cb0_pa + (23ULL + (uint64_t)p) * 4096ULL) | kAttrGpuRw;
    }
    pt_l3_1[0x100U] = (cb0_pa + 31ULL * 4096ULL) | kAttrGpuRw; /* Memptrs/Fence @ 0x00300000 */
    pt_l3_1[0x101U] = (cb0_pa + 32ULL * 4096ULL) | kAttrGpuRw; /* Pwrup reglist @ 0x00301000 */
    pt_l3_1[0x102U] = (cb0_pa + 33ULL * 4096ULL) | kAttrGpuRw; /* Scratch       @ 0x00302000 */
    dmb_sy();

    volatile uint32_t *ring_va    = (volatile uint32_t *)((volatile uint8_t *)cb0_pool_va + 23U * 4096U);
    volatile uint32_t *memptrs_va = (volatile uint32_t *)((volatile uint8_t *)cb0_pool_va + 31U * 4096U);
    volatile uint32_t *pwrup_va   = (volatile uint32_t *)((volatile uint8_t *)cb0_pool_va + 32U * 4096U);
    volatile uint32_t *scratch_va = (volatile uint32_t *)((volatile uint8_t *)cb0_pool_va + 33U * 4096U);

    LogS("  Allocated 34p CB0 pool @ PA=0x"); LogHex(cb0_pa, 12);
    LogS(" TTBR0=0x"); LogHex(cb0_pa | (1ULL << 48), 16); LogNL();

    int cb0_active = 0;
    int sqe_started = 0;
    int abort_sqe = 0;

    /* Program KGSL-SMMU Context Bank 0 (Bank=0, Asid=1, SID 0 + SID 1) */
    mmio_w32(smmu, 0x0C00U, 0x00020000U); /* S2CR[0] = FAULT */
    mmio_w32(smmu, 0x0C04U, 0x00020000U); /* S2CR[1] = FAULT */
    mmio_w32(smmu, cb0 + 0x000U, 0U);          /* CB0_SCTLR = 0 */
    mmio_w32(smmu, cb0 + 0x004U, 0x00000301U); /* CB0_ACTLR: CMTLB | PREFETCH_DEEP, CPRE=0 */
    mmio_w32(smmu, 0x1800U, 1U);               /* CBA2R[0] = VA64 */
    mmio_w32(smmu, 0x1000U, 0x0001F300U);      /* CBAR[0] = S1 trans, S2 bypass */
    mmio_w32(smmu, cb0 + 0x010U, 0x00038011U); /* CB0_TCR2 */
    mmio_w32(smmu, cb0 + 0x030U, 0x80803519U); /* CB0_TCR */
    mmio_w64(smmu, cb0 + 0x020U, cb0_pa | (1ULL << 48)); /* CB0_TTBR0 */
    mmio_w64(smmu, cb0 + 0x028U, (1ULL << 48));          /* CB0_TTBR1 */
    mmio_w32(smmu, cb0 + 0x038U, 0x0000FF44U); /* CB0_MAIR0 */
    mmio_w32(smmu, cb0 + 0x03CU, 0U);          /* CB0_MAIR1 */
    uint32_t cb0_fsr0 = mmio_r32(smmu, cb0 + 0x058U);
    mmio_w32(smmu, cb0 + 0x058U, cb0_fsr0 & 0xC00001FEU);

    mmio_w32(smmu, cb0 + 0x618U, 0U);          /* CB0_TLBIALL */
    mmio_w32(smmu, cb0 + 0x7F0U, 0xFFFFFFFFU); /* CB0_TLBSYNC */
    if (!PollMmioBit0Clear(smmu, cb0 + 0x7F4U, 1000U)) {
        LogS("  [ERR] SMMU CB0 TLBSYNC timeout."); LogNL();
        goto teardown_cb0;
    }

    mmio_w32(smmu, cb0 + 0x000U, 0x00001167U); /* CB0_SCTLR enable */
    mmio_w32(smmu, 0x0C00U, 0x00000000U);      /* S2CR[0] -> CB0 */
    mmio_w32(smmu, 0x0800U, 0x80000000U);      /* SMR[0]: VALID | SID=0 (gfx3d_user) */
    mmio_w32(smmu, 0x0C04U, 0x00000000U);      /* S2CR[1] -> CB0 */
    mmio_w32(smmu, 0x0804U, 0x80000001U);      /* SMR[1]: VALID | SID=1 (gfx3d_lpac) */
    dmb_sy();
    cb0_active = 1;

    LogReg("CB0_SCTLR(armed)",   0x03DC0000U, mmio_r32(smmu, cb0 + 0x000U));
    LogReg("CB0_ACTLR(armed)",   0x03DC0004U, mmio_r32(smmu, cb0 + 0x004U));
    LogReg("CB0_TCR(armed)",     0x03DC0030U, mmio_r32(smmu, cb0 + 0x030U));
    LogReg("CB0_TCR2(armed)",    0x03DC0010U, mmio_r32(smmu, cb0 + 0x010U));
    LogReg("CB0_TTBR0_LO",       0x03DC0020U, mmio_r32(smmu, cb0 + 0x020U));
    LogReg("CB0_TTBR0_HI",       0x03DC0024U, mmio_r32(smmu, cb0 + 0x024U));
    LogReg("CB0_FSR(armed)",     0x03DC0058U, mmio_r32(smmu, cb0 + 0x058U));
    LogReg("SMR[0](armed)",      0x03DA0800U, mmio_r32(smmu, 0x0800U));
    LogReg("SMR[1](armed)",      0x03DA0804U, mmio_r32(smmu, 0x0804U));
    LogReg("S2CR[0](armed)",     0x03DA0C00U, mmio_r32(smmu, 0x0C00U));
    LogReg("S2CR[1](armed)",     0x03DA0C04U, mmio_r32(smmu, 0x0C04U));
    LogReg("SMMU_sGFSR(cb0)",    0x03DA0048U, mmio_r32(smmu, 0x0048U));

    /* Optional Non-Destructive gpu_qtb@0x03DE8000 ECATS Hardware Walk Self-Test */
    PHYSICAL_ADDRESS qtb_pa;
    qtb_pa.QuadPart = (int64_t)PA_GPU_QTB;
    volatile uint8_t *qtb = (volatile uint8_t *)MmMapIoSpace(qtb_pa, SZ_GPU_QTB, MmNonCached);
    if (qtb) {
        uint64_t q_req  = mmio_r64(qtb, 0x410U);
        uint64_t q_trig = mmio_r64(qtb, 0x448U);
        if (q_req != ~0ULL && (q_req & 1ULL) == 0ULL && q_trig == 0ULL) {
            uint32_t sctlr_saved = mmio_r32(smmu, cb0 + 0x000U);
            mmio_w64(qtb, 0x410U, q_req | 1ULL);
            int halt_ok = 0;
            for (uint32_t t = 0U; t <= 200U; t += 5U) {
                if (mmio_r64(qtb, 0x418U) & 1ULL) { halt_ok = 1; break; }
                StallUs(5U);
            }
            if (!halt_ok) {
                LogS("  [WARN] QTB ECATS HALT_TIMEOUT (req=0x"); LogHex(q_req, 16);
                LogS(" ack=0x"); LogHex(mmio_r64(qtb, 0x418U), 16); LogS(")"); LogNL();
                mmio_w64(qtb, 0x410U, q_req);
            } else {
                mmio_w32(smmu, cb0 + 0x000U, sctlr_saved & ~0xC0U);
                mmio_w64(qtb, 0x430U, 0ULL);
                mmio_w64(qtb, 0x438U, 0x3408ULL);
                mmio_w64(qtb, 0x440U, 0x00100000ULL);
                mmio_w64(qtb, 0x448U, 1ULL);
                uint64_t q_status = 0ULL, q_out = 0ULL;
                int ecats_ok = 0;
                for (uint32_t t = 0U; t <= 200U; t += 5U) {
                    q_status = mmio_r64(qtb, 0x450U);
                    q_out    = mmio_r64(qtb, 0x458U);
                    if ((q_status | q_out) & 1ULL) { ecats_ok = 1; break; }
                    StallUs(5U);
                }
                uint32_t q_fsr = mmio_r32(smmu, cb0 + 0x058U) & ~0x600U;
                uint64_t expected_sqe_pa = cb0_pa + 4ULL * 4096ULL;
                uint64_t extracted = q_out >> 12;

                mmio_w64(qtb, 0x448U, 0ULL);
                mmio_w32(smmu, cb0 + 0x000U, sctlr_saved);
                mmio_w64(qtb, 0x410U, q_req);
                dmb_sy();

                const char *res_str = "TIMEOUT";
                if (ecats_ok) {
                    if ((q_out & 1ULL) != 0ULL || q_fsr != 0U) {
                        res_str = "FAULT";
                        abort_sqe = 1;
                    } else if (extracted == expected_sqe_pa) {
                        res_str = "MATCH";
                    } else {
                        res_str = "MISMATCH";
                        abort_sqe = 1;
                    }
                }
                LogS("  QTB ECATS: "); LogS(res_str);
                LogS(" status=0x"); LogHex(q_status, 16);
                LogS(" out=0x"); LogHex(q_out, 16);
                LogS(" extracted=0x"); LogHex(extracted, 12);
                LogS(" expected=0x"); LogHex(expected_sqe_pa, 12);
                LogS(" fsr=0x"); LogHex(q_fsr, 8); LogNL();
            }
        } else {
            LogS("  [INFO] QTB ECATS skipped (req=0x"); LogHex(q_req, 16);
            LogS(" trig=0x"); LogHex(q_trig, 16); LogS(")"); LogNL();
        }
        MmUnmapIoSpace((PVOID)qtb, SZ_GPU_QTB);
    }

    /* A740 gpuHwInit() */
    KgslW32(0x3C45U, 0U);
    uint32_t gbif_halt_post = KgslR32(0x3C45U);
    uint32_t gbif_ack_post  = KgslR32(0x3C46U);
    LogReg("GBIF_HALT(clr)",     0x03D0F114U, gbif_halt_post);
    LogReg("GBIF_HALT_ACK(clr)", 0x03D0F118U, gbif_ack_post);

    KgslW32(0x0016U, 0U);
    LogReg("RBBM_GBIF_HALT(clr)",     0x03D00058U, KgslR32(0x0016U));
    LogReg("RBBM_GBIF_HALT_ACK(clr)", 0x03D0005CU, KgslR32(0x0017U));
    LogReg("SECVID_TRUST_CNTL(pre)",  0x03D3D000U, KgslR32(0xF400U));

    KgslW32(0xF803U, 0U);
    KgslW32(0xF800U, 0U);
    KgslW32(0xF801U, 0U);
    KgslW32(0xF802U, 0U);

    for (uint32_t i = 0U; i < 3U; i++) {
        GmuDwW32(kA740AoHwcg[i][0], kA740AoHwcg[i][1]);
    }
    if (KgslR32(0x00AEU) != 0x8AA8AA82U) {
        for (uint32_t i = 0U; i < 54U; i++) {
            KgslW32(kA740Hwcg[i][0], kA740Hwcg[i][1]);
        }
        KgslW32(0x00AEU, 0x8AA8AA82U);
    }
    for (uint32_t i = 0U; i < 5U; i++) {
        KgslW32(kA740Gbif[i][0], kA740Gbif[i][1]);
    }

    KgslW32(0x0E3AU, 0x010240E0U); /* UCHE_GBIF_GX_CONFIG */
    KgslW32(0x050BU, 0xFFFFFFFFU); /* RBBM_PERFCTR_GPU_BUSY_MASKED */
    KgslW32(0x0E09U, 0xFFFFF000U); /* UCHE_TRAP_BASE_LO */
    KgslW32(0x0E0AU, 0x0001FFFFU); /* UCHE_TRAP_BASE_HI */
    KgslW32(0x0E07U, 0xFFFFF000U); /* UCHE_WRITE_THRU_BASE_LO */
    KgslW32(0x0E08U, 0x0001FFFFU); /* UCHE_WRITE_THRU_BASE_HI */
    KgslW32(0x0E0BU, 0x01000000U); /* UCHE_GMEM_RANGE_MIN_LO */
    KgslW32(0x0E0CU, 0U);          /* UCHE_GMEM_RANGE_MIN_HI */
    KgslW32(0x0E0DU, 0x012FFFFFU); /* UCHE_GMEM_RANGE_MAX_LO */
    KgslW32(0x0E0EU, 0U);          /* UCHE_GMEM_RANGE_MAX_HI */
    KgslW32(0x0E17U, 0x00800000U); /* UCHE_CACHE_WAYS */
    KgslW32(0x098DU, 1U);          /* CP_AHB_CNTL */
    KgslW32(0x0500U, 1U);          /* RBBM_PERFCTR_CNTL */
    KgslW32(0x001FU, 0x40CFFFFFU); /* RBBM_INTERFACE_HANG_INT_CNTL */
    KgslW32(0x0E19U, 0x00000081U); /* UCHE_CLIENT_PF */
    KgslW32(0x0E3CU, 0x0006690EU); /* UCHE_CMDQ_CONFIG */

    KgslW32(0x084FU, 0x0000000BU); /* CP_PROTECT_CNTL */
    for (uint32_t i = 0U; i < 45U; i++) {
        KgslW32(kA740Protect[i][0], kA740Protect[i][1]);
    }

    KgslW32(0x0844U, 0x6FU); /* CP_APRIV_CNTL */
    KgslW32(0x0AD0U, 0x0FU); /* CP_BV_APRIV_CNTL */
    KgslW32(0x0B31U, 0x0FU); /* CP_LPAC_APRIV_CNTL */
    KgslW32(0x0841U, 1U);    /* CP_CHICKEN_DBG */
    KgslW32(0x0ADAU, 1U);    /* CP_BV_CHICKEN_DBG */
    KgslW32(0x0B30U, 1U);    /* CP_LPAC_CHICKEN_DBG */

    KgslW32(0x8E28U, KgslR32(0x8E28U) | 0x800U); /* RB_CMP_DBG_ECO_CNTL */

    /* S23 UBWC 4.0 / HBB=16 (lo=3, hi=0, mal=32) */
    KgslW32(0x8E08U, 0x00000816U); /* RB_NC_MODE_CNTL */
    KgslW32(0xB604U, 0x00000006U); /* TPL1_NC_MODE_CNTL */
    KgslW32(0xAE02U, 0x00000026U); /* SP_NC_MODE_CNTL */
    for (uint32_t pipe = 1U; pipe <= 2U; pipe++) {
        KgslW32(0x0A00U, pipe << 12);
        KgslW32(0x8602U, 0x00000060U); /* GRAS_NC_MODE_CNTL */
    }
    KgslW32(0x0A00U, 0U);          /* CP_APERTURE_CNTL_HOST = PIPE_NONE */
    KgslW32(0x0E01U, 0x00600000U); /* UCHE_MODE_CNTL */
    KgslW32(0x0534U, 1U);          /* RBBM_NC_MODE_CNTL */

    GmuDwW32(0x23B0EU, 0xFF000000U); /* GMU_AO_GPU_CX_BUSY_MASK */
    GmuDwW32(0x1F841U, (GmuDwR32(0x1F841U) & ~0xFFU) | 0x20U);
    GmuDwW32(0x1F842U, 4U);
    GmuDwW32(0x1F840U, 1U);

    KgslW32(0x0038U, 0x33D283C2U); /* RBBM_INT_0_MASK */

    /* Ring 0 & SQE Base Setup */
    KgslW32(0x0800U, 0x00200000U); /* CP_RB_BASE_LO */
    KgslW32(0x0801U, 0U);          /* CP_RB_BASE_HI */
    KgslW32(0x0802U, 0x0000020CU); /* CP_RB_CNTL */
    KgslW32(0x0804U, 0x00300000U); /* CP_RB_RPTR_ADDR_LO */
    KgslW32(0x0805U, 0U);          /* CP_RB_RPTR_ADDR_HI */
    KgslW32(0x0A98U, 0x00300008U); /* CP_BV_RB_RPTR_ADDR_LO */
    KgslW32(0x0A99U, 0U);          /* CP_BV_RB_RPTR_ADDR_HI */
    KgslW32(0x0807U, 0U);          /* CP_RB_WPTR */
    KgslW32(0x0885U, 0U);          /* CP_SCRATCH_REG2 */
    KgslW32(0x0830U, 0x00100000U); /* CP_SQE_INSTR_BASE_LO */
    KgslW32(0x0831U, 0U);          /* CP_SQE_INSTR_BASE_HI */

    /* Build pwrup_reglist at 0x00301000 */
    pwrup_va[0] = 0U; pwrup_va[1] = 0U; pwrup_va[2] = 0U;
    pwrup_va[3] = (16U << 8); /* ifpc=0, preempt=16, dynamic=0 */
    for (uint32_t i = 0U; i < 16U; i++) {
        pwrup_va[4U + 2U * i] = kA740PreemptRegs[i];
        pwrup_va[5U + 2U * i] = KgslR32(kA740PreemptRegs[i]);
    }
    dmb_sy();

    uint32_t ahb_post     = mmio_r32(g_gmu, GMU_AO_AHB_FENCE_STATUS);
    uint32_t rbbm_post    = KgslR32(0x0210U);
    uint32_t clk_post     = KgslR32(0x00AEU);
    uint32_t sqe_lo_post  = KgslR32(0x0830U);
    uint32_t sqe_hi_post  = KgslR32(0x0831U);
    uint32_t rptr_post    = KgslR32(0x0806U);
    uint32_t sqe_ctl_post = KgslR32(0x0808U);
    uint32_t cb0_fsr_post = mmio_r32(smmu, cb0 + 0x058U);
    uint32_t sgfsr_post   = mmio_r32(smmu, 0x0048U);

    LogS("--- Stage 6A Post-HwInit Checkpoint ---"); LogNL();
    LogReg("GMU_AO_AHB_FENCE_ST(6A)", 0x03D8EC4CU, ahb_post);
    LogReg("RBBM_STATUS(6A)",         0x03D00840U, rbbm_post);
    LogReg("RBBM_CLOCK_CNTL(6A)",     0x03D002B8U, clk_post);
    LogReg("CP_SQE_INSTR_BASE_LO",    0x03D020C0U, sqe_lo_post);
    LogReg("CP_SQE_INSTR_BASE_HI",    0x03D020C4U, sqe_hi_post);
    LogReg("CP_RB_RPTR(6A)",          0x03D02018U, rptr_post);
    LogReg("CP_SQE_CNTL(6A)",         0x03D02020U, sqe_ctl_post);
    LogReg("CB0_FSR(6A)",             0x03DC0058U, cb0_fsr_post);
    LogReg("SMMU_sGFSR(6A)",          0x03DA0048U, sgfsr_post);

    int pre_ok = (ahb_post == 0U) && (gbif_halt_post == 0U) &&
                 ((cb0_fsr_post & ~0x600U) == 0U) && (sgfsr_post == 0U) &&
                 (sqe_lo_post == 0x00100000U) && (sqe_hi_post == 0U) &&
                 (rptr_post == 0U) && (sqe_ctl_post == 0U) && !abort_sqe;

    /* Stage 6B (mask & 0x80): Release CP_SQE_CNTL=1 & submit PM4 CP_ME_INIT + CP_MEM_WRITE */
    if ((mask & 0x80U) == 0U) {
        LogS("  Stage 0x80 not set; skipping CP_SQE_CNTL=1 (Stage 6A checkpoint complete)."); LogNL();
        goto teardown_cb0;
    }
    if (!pre_ok) {
        LogS("  [ABORT] Stage 6A pre-SQE verification failed (abort_sqe="); LogDec(abort_sqe);
        LogS("); refusing CP_SQE_CNTL=1."); LogNL();
        goto teardown_cb0;
    }

    LogS("=== STAGE 0x80 (Stage 6B): Releasing CP_SQE_CNTL=1 + PM4 Init/Scratch Fences ==="); LogNL();
    KgslW32(0x0808U, 1U);
    dmb_sy();
    sqe_started = 1;
    LogS("  CP_SQE_CNTL=1 returned: AHB=0x"); LogHex(mmio_r32(g_gmu, GMU_AO_AHB_FENCE_STATUS), 8);
    LogS(" RBBM_STATUS=0x"); LogHex(KgslR32(0x0210U), 8);
    LogS(" CB0_FSR=0x"); LogHex(mmio_r32(smmu, cb0 + 0x058U), 8); LogNL();

    ring_va[0]  = Pm4Type7(0x17U, 1U);
    ring_va[1]  = (1U << 27);
    ring_va[2]  = Pm4Type7(0x48U, 7U);
    ring_va[3]  = 0x0000094BU;
    ring_va[4]  = 3U;
    ring_va[5]  = 0x20000000U;
    ring_va[6]  = 2U;
    ring_va[7]  = 0x00301000U;
    ring_va[8]  = 0U;
    ring_va[9]  = (1U << 31);
    ring_va[10] = Pm4Type4(0x0885U, 1U);
    ring_va[11] = 1U;
    ring_va[12] = Pm4Type7(0x46U, 4U);
    ring_va[13] = 0x88000031U;
    ring_va[14] = 0x00300004U;
    ring_va[15] = 0U;
    ring_va[16] = 1U;
    dmb_sy();
    KgslW32(0x0807U, 17U);
    dmb_sy();
    LogS("  Posted WPTR=17: AHB=0x"); LogHex(mmio_r32(g_gmu, GMU_AO_AHB_FENCE_STATUS), 8); LogNL();

    uint32_t fence1 = 0U, polls1 = 0U;
    for (uint32_t i = 0U; i < 5000U; i++) {
        dmb_sy();
        fence1 = memptrs_va[1];
        polls1 = i + 1U;
        if (fence1 >= 1U) break;
        StallUs(100U);
    }
    LogS("  Fence1: val="); LogDec((int64_t)fence1);
    LogS(" polls="); LogDec((int64_t)polls1);
    LogS(" rptr_shadow=0x"); LogHex(memptrs_va[0], 8);
    LogS(" RPTR=0x"); LogHex(KgslR32(0x0806U), 8);
    LogS(" WPTR=0x"); LogHex(KgslR32(0x0807U), 8); LogNL();
    LogReg("RBBM_STATUS(f1)",       0x03D00840U, KgslR32(0x0210U));
    LogReg("RBBM_STATUS3(f1)",      0x03D0084CU, KgslR32(0x0213U));
    LogReg("RBBM_INT_0_STATUS(f1)", 0x03D00804U, KgslR32(0x0201U));
    LogReg("CP_HW_FAULT(f1)",       0x03D02084U, KgslR32(0x0821U));
    LogReg("CP_INT_STATUS(f1)",     0x03D0208CU, KgslR32(0x0823U));
    LogReg("CB0_FSR(f1)",           0x03DC0058U, mmio_r32(smmu, cb0 + 0x058U));
    LogReg("CB0_FAR_LO(f1)",        0x03DC0060U, mmio_r32(smmu, cb0 + 0x060U));
    LogReg("CB0_FAR_HI(f1)",        0x03DC0064U, mmio_r32(smmu, cb0 + 0x064U));
    LogReg("SMMU_sGFSR(f1)",        0x03DA0048U, mmio_r32(smmu, 0x0048U));

    if (fence1 != 1U) {
        LogS("  [WARN] Fence 1 did not complete."); LogNL();
        goto teardown_cb0;
    }

    /* Second batch: CP_MEM_WRITE(0x05E9A740) -> 0x00302000 + fence 2 */
    scratch_va[0] = 0U;
    dmb_sy();
    ring_va[17] = Pm4Type7(0x3DU, 3U);
    ring_va[18] = 0x00302000U;
    ring_va[19] = 0U;
    ring_va[20] = 0x05E9A740U;
    ring_va[21] = Pm4Type4(0x0885U, 1U);
    ring_va[22] = 2U;
    ring_va[23] = Pm4Type7(0x46U, 4U);
    ring_va[24] = 0x88000031U;
    ring_va[25] = 0x00300004U;
    ring_va[26] = 0U;
    ring_va[27] = 2U;
    dmb_sy();
    KgslW32(0x0807U, 28U);
    dmb_sy();

    uint32_t fence2 = 0U, polls2 = 0U;
    for (uint32_t i = 0U; i < 5000U; i++) {
        dmb_sy();
        fence2 = memptrs_va[1];
        polls2 = i + 1U;
        if (fence2 >= 2U) break;
        StallUs(100U);
    }
    dmb_sy();
    uint32_t scratch_val = scratch_va[0];
    LogS("  Fence2: val="); LogDec((int64_t)fence2);
    LogS(" polls="); LogDec((int64_t)polls2);
    LogS(" scratch=0x"); LogHex(scratch_val, 8);
    LogS(" RPTR=0x"); LogHex(KgslR32(0x0806U), 8);
    LogS(" RBBM_STATUS=0x"); LogHex(KgslR32(0x0210U), 8);
    LogS(" CB0_FSR=0x"); LogHex(mmio_r32(smmu, cb0 + 0x058U), 8); LogNL();

    if ((mask & 0x100U) == 0U || fence2 != 2U || scratch_val != 0x05E9A740U) {
        goto teardown_cb0;
    }

    /* Stage 6C (mask & 0x100): CP_SMMU_APERTURE + ZAP PAS_ID=13 + CP_SET_SECURE_MODE(0) */
    LogS("=== STAGE 0x100 (Stage 6C): CP_SMMU_APERTURE + ZAP PAS13 + Non-Secure ==="); LogNL();
    {
        uint64_t r0 = ~0ULL, r1 = 0ULL, r2 = 0ULL, r3 = 0ULL;
        (void)ScmCall64(0x42000C1BULL, 4ULL, 0xFFFF0000ULL, 0xFFFFFFFFULL, 0xFFFFFFFFULL, 0xFFFFFFFFULL, &r0, &r1, &r2, &r3);
        LogS("  SCM CP_SMMU_APERTURE_ID(CB0): r0=0x"); LogHex(r0, 16);
        LogS(" r1=0x"); LogHex(r1, 16); LogNL();

        uint32_t zap_len = 0U;
        if (!ReadFwFile(L"\\??\\C:\\Users\\Public\\a740_zap.mbn", &zap_len) || zap_len < 52U ||
            g_gmu_fw_buf[0] != 0x7FU || g_gmu_fw_buf[1] != 'E' ||
            g_gmu_fw_buf[2] != 'L'  || g_gmu_fw_buf[3] != 'F' || g_gmu_fw_buf[4] != 1U) {
            LogS("  [SKIP] Invalid or missing C:\\Users\\Public\\a740_zap.mbn (len="); LogDec((int64_t)zap_len); LogS(")."); LogNL();
            goto teardown_cb0;
        }
        LogS("  Loaded C:\\Users\\Public\\a740_zap.mbn: "); LogDec((int64_t)zap_len); LogS(" bytes"); LogNL();

        uint32_t phoff = ReadLe32U(g_gmu_fw_buf + 28U);
        uint32_t phent = ReadLe16U(g_gmu_fw_buf + 42U);
        uint32_t phnum = ReadLe16U(g_gmu_fw_buf + 44U);
        if (phent != 32U || phnum < 2U || phnum > 16U || (uint64_t)phoff + (uint64_t)phnum * 32ULL > zap_len) {
            LogS("  [ERR] ZAP ELF phdr bounds error."); LogNL();
            goto teardown_cb0;
        }
        if (ZapPh(phoff, 0U, 0U) == 1U) {
            LogS("  [ERR] ZAP phdr[0] is PT_LOAD."); LogNL();
            goto teardown_cb0;
        }
        uint32_t hash_idx = 0U;
        for (uint32_t i = 1U; i < phnum; i++) {
            if (((ZapPh(phoff, i, 6U) >> 24) & 7U) == 2U) {
                hash_idx = i;
                break;
            }
        }
        if (hash_idx == 0U) {
            LogS("  [ERR] ZAP hash segment not found."); LogNL();
            goto teardown_cb0;
        }
        uint64_t zlo = ~0ULL, zhi = 0ULL;
        int relocate = 0;
        for (uint32_t i = 0U; i < phnum; i++) {
            uint32_t ptype = ZapPh(phoff, i, 0U);
            uint32_t flags = ZapPh(phoff, i, 6U);
            uint32_t memsz = ZapPh(phoff, i, 5U);
            uint32_t paddr = ZapPh(phoff, i, 3U);
            if (ptype != 1U || ((flags >> 24) & 7U) == 2U || memsz == 0U) continue;
            if (flags & (1U << 27)) relocate = 1;
            if ((uint64_t)paddr < zlo) zlo = (uint64_t)paddr;
            uint64_t end = ((uint64_t)paddr + (uint64_t)memsz + 0xFFFULL) & ~0xFFFULL;
            if (end > zhi) zhi = end;
        }
        if (zlo >= zhi || (zhi - zlo) > SZ_ZAP_REGION) {
            LogS("  [ERR] ZAP region span invalid lo=0x"); LogHex(zlo, 8);
            LogS(" hi=0x"); LogHex(zhi, 8); LogNL();
            goto teardown_cb0;
        }
        uint32_t ehdr_bytes = ZapPh(phoff, 0U, 4U);
        uint32_t hash_bytes = ZapPh(phoff, hash_idx, 4U);
        uint32_t hash_off   = ZapPh(phoff, hash_idx, 1U);
        if (ehdr_bytes > zap_len || (uint64_t)hash_off + (uint64_t)hash_bytes > zap_len ||
            ehdr_bytes + hash_bytes > 0x4000U) {
            LogS("  [ERR] ZAP metadata bounds invalid."); LogNL();
            goto teardown_cb0;
        }

        uint32_t meta_pages = (ehdr_bytes + hash_bytes + 0xFFFU) >> 12;
        volatile uint8_t *meta_va = (volatile uint8_t *)MmAllocateContiguousMemorySpecifyCache(
            meta_pages * 4096U, lo, hi, bound, MmNonCached);
        if (!meta_va) {
            LogS("  [ERR] Failed allocating ZAP metadata buffer."); LogNL();
            goto teardown_cb0;
        }
        uint64_t meta_pa = (uint64_t)MmGetPhysicalAddress((PVOID)meta_va).QuadPart;
        ZeroBuf32((volatile uint32_t *)meta_va, meta_pages * 1024U);
        for (uint32_t i = 0U; i < ehdr_bytes; i++) meta_va[i] = g_gmu_fw_buf[i];
        for (uint32_t i = 0U; i < hash_bytes; i++) meta_va[ehdr_bytes + i] = g_gmu_fw_buf[hash_off + i];
        dmb_sy();

        (void)ScmCall64(0x42000201ULL, 2ULL | (2ULL << 6), 13ULL, meta_pa, 0ULL, 0ULL, &r0, &r1, NULL, NULL);
        LogS("  PAS_INIT_IMAGE(13): r0=0x"); LogHex(r0, 16); LogS(" r1=0x"); LogHex(r1, 16); LogNL();

        if (r0 == 0ULL && r1 == 0ULL && relocate) {
            (void)ScmCall64(0x42000202ULL, 3ULL, 13ULL, PA_ZAP_REGION, zhi - zlo, 0ULL, &r0, &r1, NULL, NULL);
            LogS("  PAS_MEM_SETUP(13):  r0=0x"); LogHex(r0, 16); LogS(" r1=0x"); LogHex(r1, 16); LogNL();
        }
        if (r0 == 0ULL && r1 == 0ULL) {
            PHYSICAL_ADDRESS zpa;
            zpa.QuadPart = (int64_t)PA_ZAP_REGION;
            volatile uint8_t *zreg = (volatile uint8_t *)MmMapIoSpace(zpa, SZ_ZAP_REGION, MmNonCached);
            if (!zreg) {
                LogS("  [ERR] Failed mapping PA_ZAP_REGION 0x9B09A000."); LogNL();
                MmFreeContiguousMemorySpecifyCache((PVOID)meta_va, meta_pages * 4096U, MmNonCached);
                goto teardown_cb0;
            }
            uint64_t zbase = relocate ? zlo : PA_ZAP_REGION;
            int seg_ok = 1;
            for (uint32_t i = 0U; i < phnum; i++) {
                uint32_t ptype = ZapPh(phoff, i, 0U);
                uint32_t flags = ZapPh(phoff, i, 6U);
                uint32_t memsz = ZapPh(phoff, i, 5U);
                if (ptype != 1U || ((flags >> 24) & 7U) == 2U || memsz == 0U) continue;
                uint64_t paddr  = (uint64_t)ZapPh(phoff, i, 3U);
                uint32_t filesz = ZapPh(phoff, i, 4U);
                uint32_t foff   = ZapPh(phoff, i, 1U);
                uint64_t at     = paddr - zbase;
                if (paddr < zbase || at + memsz > SZ_ZAP_REGION || filesz > memsz || (uint64_t)foff + filesz > zap_len) {
                    seg_ok = 0;
                    break;
                }
                for (uint32_t b = 0U; b < filesz; b++) zreg[at + b] = g_gmu_fw_buf[foff + b];
                for (uint32_t b = filesz; b < memsz; b++) zreg[at + b] = 0U;
            }
            dmb_sy();
            MmUnmapIoSpace((PVOID)zreg, SZ_ZAP_REGION);
            if (!seg_ok) {
                LogS("  [ERR] ZAP segment copy bounds failure."); LogNL();
                MmFreeContiguousMemorySpecifyCache((PVOID)meta_va, meta_pages * 4096U, MmNonCached);
                goto teardown_cb0;
            }
            (void)ScmCall64(0x42000205ULL, 1ULL, 13ULL, 0ULL, 0ULL, 0ULL, &r0, &r1, NULL, NULL);
            LogS("  PAS_AUTH_RESET(13): r0=0x"); LogHex(r0, 16); LogS(" r1=0x"); LogHex(r1, 16); LogNL();
        }

        MmFreeContiguousMemorySpecifyCache((PVOID)meta_va, meta_pages * 4096U, MmNonCached);

        if (r0 == 0ULL && r1 == 0ULL) {
            ring_va[28] = Pm4Type7(0x66U, 1U);
            ring_va[29] = 0U;
            ring_va[30] = Pm4Type4(0x0885U, 1U);
            ring_va[31] = 3U;
            ring_va[32] = Pm4Type7(0x46U, 4U);
            ring_va[33] = 0x88000031U;
            ring_va[34] = 0x00300004U;
            ring_va[35] = 0U;
            ring_va[36] = 3U;
            dmb_sy();
            KgslW32(0x0807U, 37U);
            dmb_sy();

            uint32_t fence3 = 0U, polls3 = 0U;
            for (uint32_t i = 0U; i < 5000U; i++) {
                dmb_sy();
                fence3 = memptrs_va[1];
                polls3 = i + 1U;
                if (fence3 >= 3U) break;
                StallUs(100U);
            }
            LogS("  Fence3(non-sec): val="); LogDec((int64_t)fence3);
            LogS(" polls="); LogDec((int64_t)polls3); LogNL();
            LogReg("SECVID_TRUST_CNTL(6C)", 0x03D3D000U, KgslR32(0xF400U));
            LogReg("RBBM_STATUS(6C)",       0x03D00840U, KgslR32(0x0210U));
            LogReg("CB0_FSR(6C)",           0x03DC0058U, mmio_r32(smmu, cb0 + 0x058U));
        }
    }

teardown_cb0:
    if (sqe_started) {
        KgslW32(0x0808U, 3U);
        dmb_sy();
        KgslW32(0x3C45U, 1U);
        for (uint32_t i = 0U; i < 500U && (KgslR32(0x3C46U) & 1U) == 0U; i++) StallUs(100U);
        KgslW32(0x3C45U, 3U);
        for (uint32_t i = 0U; i < 500U && (KgslR32(0x3C46U) & 2U) == 0U; i++) StallUs(100U);
        KgslW32(0x3C45U, 0U);
        KgslW32(0x0043U, 1U);
        (void)KgslR32(0x0043U);
        StallUs(150U);
        KgslW32(0x0043U, 0U);
        (void)KgslR32(0x0043U);
        LogReg("RBBM_STATUS(reset)",  0x03D00840U, KgslR32(0x0210U));
        LogReg("CP_SQE_CNTL(reset)",  0x03D02020U, KgslR32(0x0808U));
    }

    if (cb0_active) {
        mmio_w32(smmu, 0x0C00U, 0x00020000U);
        mmio_w32(smmu, 0x0C04U, 0x00020000U);
        mmio_w32(smmu, 0x0800U, 0U);
        mmio_w32(smmu, 0x0804U, 0U);
        mmio_w32(smmu, cb0 + 0x000U, 0U);
        mmio_w32(smmu, cb0 + 0x618U, 0U);
        mmio_w32(smmu, cb0 + 0x7F0U, 0xFFFFFFFFU);
        int cb0_tlb = PollMmioBit0Clear(smmu, cb0 + 0x7F4U, 1000U);
        dmb_sy();

        uint32_t sqe_now   = KgslR32(0x0808U);
        uint32_t cb0_sctlr = mmio_r32(smmu, cb0 + 0x000U);
        uint32_t cb0_tlb_st = mmio_r32(smmu, cb0 + 0x7F4U);
        LogS("  CB0 teardown: tlb_done="); LogDec((int64_t)cb0_tlb);
        LogS(" SQE="); LogHex(sqe_now, 8);
        LogS(" CB0_SCTLR="); LogHex(cb0_sctlr, 8);
        LogS(" TLBSTATUS="); LogHex(cb0_tlb_st, 8); LogNL();

        if ((sqe_started && sqe_now != 0U) || cb0_sctlr != 0U || (cb0_tlb_st & 1U) != 0U) {
            LogS("  [WARN] Unsafe to free CB0 pool (retained)."); LogNL();
            return;
        }
    }

    MmFreeContiguousMemorySpecifyCache((PVOID)cb0_pool_va, kCb0Pages * 4096U, MmNonCached);
}

static void RunStage5BGmuBoot(uint32_t mask) {
    LogS("=== STAGE 0x20 (Stage 5B): Opt-In KGSL-SMMU CB2 + GMU CM3 + HFI + OOB ==="); LogNL();

    uint32_t fw_len = 0;
    if (!ReadFwFile(L"\\??\\C:\\Users\\Public\\gmu_gen70200.bin", &fw_len)) {
        LogS("  [SKIP] C:\\Users\\Public\\gmu_gen70200.bin not found -- pull via Lineage Recovery pull_s23_gpu_fw.py first"); LogNL();
        return;
    }
    LogS("  Loaded C:\\Users\\Public\\gmu_gen70200.bin: "); LogDec((int64_t)fw_len); LogS(" bytes"); LogNL();

    uint32_t cx_gds = 0, ahb = 0;
    if (!GpuCxAxiGateOpen(&cx_gds, &ahb)) {
        LogS("  [SKIP] CX AXI gate closed (CX_GDSCR=0x"); LogHex(cx_gds, 8);
        LogS(" AHB=0x"); LogHex(ahb, 8); LogS("); aborting Stage 5B."); LogNL();
        return;
    }
    EnsureGmuAndKgslMapped();
    if (!g_gmu) {
        LogS("  [ERR] GMU mapping unavailable."); LogNL();
        return;
    }

    uint32_t cm3_rst = mmio_r32(g_gmu, GMU_CM3_SYSRESET);
    if (cm3_rst != 1U) {
        LogS("  [SKIP] GMU_CM3_SYSRESET=0x"); LogHex(cm3_rst, 8); LogS(" != 1 (not in cold reset)."); LogNL();
        return;
    }

    PHYSICAL_ADDRESS pa;
    pa.QuadPart = (int64_t)PA_RSCC;
    volatile uint8_t *rscc = (volatile uint8_t *)MmMapIoSpace(pa, SZ_RSCC, MmNonCached);
    pa.QuadPart = (int64_t)PA_GMU_PDC;
    volatile uint8_t *pdc = (volatile uint8_t *)MmMapIoSpace(pa, SZ_GMU_PDC, MmNonCached);
    pa.QuadPart = (int64_t)PA_CX_MISC;
    volatile uint8_t *cx_misc = (volatile uint8_t *)MmMapIoSpace(pa, SZ_CX_MISC, MmNonCached);
    pa.QuadPart = (int64_t)PA_KGSL_SMMU;
    volatile uint8_t *smmu = (volatile uint8_t *)MmMapIoSpace(pa, SZ_KGSL_SMMU, MmNonCached);
    if (!rscc || !pdc || !cx_misc || !smmu) {
        LogS("  [ERR] Failed mapping RSCC/PDC/CX_MISC/KGSL-SMMU."); LogNL();
        if (smmu)    MmUnmapIoSpace((PVOID)smmu, SZ_KGSL_SMMU);
        if (cx_misc) MmUnmapIoSpace((PVOID)cx_misc, SZ_CX_MISC);
        if (pdc)     MmUnmapIoSpace((PVOID)pdc, SZ_GMU_PDC);
        if (rscc)    MmUnmapIoSpace((PVOID)rscc, SZ_RSCC);
        return;
    }

    uint32_t smmu_cbcr = g_gpucc ? mmio_r32(g_gpucc, GPUCC_HLOS1_VOTE_SMMU_CBCR) : 0xFFFFFFFFU;
    if (smmu_cbcr == 0xFFFFFFFFU || (smmu_cbcr & 0x80000000U) != 0U || (smmu_cbcr & 1U) == 0U) {
        LogS("  [SKIP] GPUCC_HLOS1_SMMU closed (0x"); LogHex(smmu_cbcr, 8); LogS("); aborting Stage 5B."); LogNL();
        goto cleanup_maps;
    }

    uint32_t sid0 = mmio_r32(smmu, 0x0020U);
    uint32_t sid1 = mmio_r32(smmu, 0x0024U);
    uint32_t sid2 = mmio_r32(smmu, 0x0028U);
    if (sid0 != 0x4C017E18U || sid1 != 0x40000016U || sid2 != 0x00005511U) {
        LogS("  [SKIP] SMMU IDR mismatch."); LogNL();
        goto cleanup_maps;
    }
    if (mmio_r32(smmu, 0x20000U) & 1U) {
        LogS("  [SKIP] KGSL-SMMU CB0 SCTLR already active."); LogNL();
        goto cleanup_maps;
    }
    if (mmio_r32(smmu, 0x22000U) & 1U) {
        LogS("  [SKIP] KGSL-SMMU CB2 SCTLR already active."); LogNL();
        goto cleanup_maps;
    }
    if ((mmio_r32(smmu, 0x0800U) & 0x80000000U) ||
        (mmio_r32(smmu, 0x0804U) & 0x80000000U) ||
        (mmio_r32(smmu, 0x0808U) & 0x80000000U)) {
        LogS("  [SKIP] KGSL-SMMU SMR[0..2] already valid (0x"); LogHex(mmio_r32(smmu, 0x0800U), 8);
        LogS("/0x"); LogHex(mmio_r32(smmu, 0x0804U), 8);
        LogS("/0x"); LogHex(mmio_r32(smmu, 0x0808U), 8); LogS(")."); LogNL();
        goto cleanup_maps;
    }

    uint32_t rsc_busy = mmio_r32(rscc, RSCC_SEQ_BUSY_DRV0);
    if (rsc_busy != 0U) {
        LogS("  [SKIP] RSCC_SEQ_BUSY_DRV0=0x"); LogHex(rsc_busy, 8); LogS(" != 0."); LogNL();
        goto cleanup_maps;
    }

    /* Allocate 89-page (<64GB, 36-bit OAS) MmNonCached pool: Pages 0..5 PT, Pages 6..88 GMU VA */
    const uint32_t kPoolPages = 89U;
    PHYSICAL_ADDRESS lo, hi, bound;
    lo.QuadPart = 0LL;
    hi.QuadPart = 0x0000000FFFFFFFFFLL;
    bound.QuadPart = 0LL;
    volatile uint32_t *pool_va = (volatile uint32_t *)MmAllocateContiguousMemorySpecifyCache(
        kPoolPages * 4096U, lo, hi, bound, MmNonCached);
    if (!pool_va) {
        LogS("  [ERR] Failed allocating 89-page (<64GB) contiguous GMU pool."); LogNL();
        goto cleanup_maps;
    }
    uint64_t pool_pa = (uint64_t)MmGetPhysicalAddress((PVOID)pool_va).QuadPart;
    if ((pool_pa & 0xFFFULL) != 0ULL || pool_pa + (uint64_t)kPoolPages * 4096ULL > (1ULL << 36)) {
        LogS("  [ERR] GMU pool PA out of 36-bit OAS or unaligned: 0x"); LogHex(pool_pa, 16); LogNL();
        MmFreeContiguousMemorySpecifyCache((PVOID)pool_va, kPoolPages * 4096U, MmNonCached);
        goto cleanup_maps;
    }
    ZeroBuf32(pool_va, kPoolPages * 1024U);

    volatile uint64_t *pt_l1      = (volatile uint64_t *)((volatile uint8_t *)pool_va + 0x0000U);
    volatile uint64_t *pt_l2_low  = (volatile uint64_t *)((volatile uint8_t *)pool_va + 0x1000U);
    volatile uint64_t *pt_l2_high = (volatile uint64_t *)((volatile uint8_t *)pool_va + 0x2000U);
    volatile uint64_t *pt_l3_low0 = (volatile uint64_t *)((volatile uint8_t *)pool_va + 0x3000U);
    volatile uint64_t *pt_l3_low1 = (volatile uint64_t *)((volatile uint8_t *)pool_va + 0x4000U);
    volatile uint64_t *pt_l3_600  = (volatile uint64_t *)((volatile uint8_t *)pool_va + 0x5000U);

    pt_l1[0]       = (pool_pa + 0x1000ULL) | 3ULL;
    pt_l1[1]       = (pool_pa + 0x2000ULL) | 3ULL;
    pt_l2_low[0]   = (pool_pa + 0x3000ULL) | 3ULL;
    pt_l2_low[1]   = (pool_pa + 0x4000ULL) | 3ULL;
    pt_l2_high[256]= (pool_pa + 0x5000ULL) | 3ULL;

    const uint64_t leaf_attrs = 0x0000000000000E03ULL; /* nG=1 AF=1 SH=2(OS) AttrIdx=0(NC) RWX Priv */
    for (uint32_t e = 0U; e < 8U; e++) {
        uint32_t base_iova = g_gmu_extents[e].iova;
        uint32_t pages     = g_gmu_extents[e].pages;
        uint32_t dpo       = g_gmu_extents[e].data_page_off;
        for (uint32_t p = 0U; p < pages; p++) {
            uint64_t va = (uint64_t)base_iova + (uint64_t)p * 4096ULL;
            uint64_t leaf_pa = pool_pa + (6ULL + (uint64_t)(dpo + p)) * 4096ULL;
            uint32_t l3_idx = (uint32_t)((va >> 12) & 0x1FFU);
            volatile uint64_t *l3_tbl = NULL;
            if ((va >> 21) == 0ULL)      l3_tbl = pt_l3_low0;
            else if ((va >> 21) == 1ULL) l3_tbl = pt_l3_low1;
            else                         l3_tbl = pt_l3_600;
            l3_tbl[l3_idx] = leaf_pa | leaf_attrs;
        }
    }
    dmb_sy();

    volatile uint8_t *data_va = (volatile uint8_t *)pool_va + 0x6000U;
    volatile uint32_t *log_va = (volatile uint32_t *)(data_va + (75U * 4096U)); /* 0x60002000 */
    volatile uint32_t *hfi_va = (volatile uint32_t *)(data_va + (79U * 4096U)); /* 0x60006000 */

    LogS("  Allocated 89p GMU pool @ PA=0x"); LogHex(pool_pa, 12);
    LogS(" TTBR0=0x"); LogHex(pool_pa | (2ULL << 48), 16); LogNL();

    int smmu_active = 0;
    int oob_requested = 0;

    /* Load & verify all 15 GMU FW blocks: ITCM, DTCM, and Region 2 (ICACHE backing) */
    uint32_t off = 0, blk_idx = 0, itcm_blks = 0, dtcm_blks = 0, icache_blks = 0;
    while (off + 16U <= fw_len) {
        uint32_t addr = ReadLe32U(g_gmu_fw_buf + off);
        uint32_t blen = ReadLe32U(g_gmu_fw_buf + off + 4U);
        off += 16U;
        if (blen > fw_len - off || (addr & 3U) || (blen & 3U)) {
            LogS("  [ERR] Invalid GMU block header at #"); LogDec((int64_t)blk_idx); LogNL();
            goto cleanup_pool;
        }
        if (blen > 0U) {
            if (addr + blen <= 0x00004000U) {
                itcm_blks++;
                for (uint32_t w = 0; w < blen; w += 4U) {
                    mmio_w32(g_gmu, GMU_ITCM_OFF + addr + w, ReadLe32U(g_gmu_fw_buf + off + w));
                }
                for (uint32_t w = 0; w < blen; w += 4U) {
                    if (mmio_r32(g_gmu, GMU_ITCM_OFF + addr + w) != ReadLe32U(g_gmu_fw_buf + off + w)) {
                        LogS("  [ERR] ITCM verify mismatch at 0x"); LogHex(addr + w, 4); LogNL();
                        goto cleanup_pool;
                    }
                }
            } else if (addr >= 0x10004000U && (addr - 0x10004000U) + blen <= 0x00004000U) {
                dtcm_blks++;
                uint32_t dbase = addr - 0x10004000U;
                for (uint32_t w = 0; w < blen; w += 4U) {
                    mmio_w32(g_gmu, GMU_DTCM_OFF + dbase + w, ReadLe32U(g_gmu_fw_buf + off + w));
                }
                for (uint32_t w = 0; w < blen; w += 4U) {
                    if (mmio_r32(g_gmu, GMU_DTCM_OFF + dbase + w) != ReadLe32U(g_gmu_fw_buf + off + w)) {
                        LogS("  [ERR] DTCM verify mismatch at 0x"); LogHex(dbase + w, 4); LogNL();
                        goto cleanup_pool;
                    }
                }
            } else if (addr >= 0x00004000U && addr + blen <= 0x00015000U) {
                icache_blks++;
                uint32_t cbase = addr - 0x00004000U;
                volatile uint32_t *dst32 = (volatile uint32_t *)(data_va + cbase);
                for (uint32_t w = 0; w < blen; w += 4U) {
                    dst32[w >> 2] = ReadLe32U(g_gmu_fw_buf + off + w);
                }
                dmb_sy();
                for (uint32_t w = 0; w < blen; w += 4U) {
                    if (dst32[w >> 2] != ReadLe32U(g_gmu_fw_buf + off + w)) {
                        LogS("  [ERR] ICACHE DRAM verify mismatch at 0x"); LogHex(addr + w, 6); LogNL();
                        goto cleanup_pool;
                    }
                }
            } else {
                LogS("  [ERR] Unmapped GMU block addr=0x"); LogHex(addr, 8); LogNL();
                goto cleanup_pool;
            }
        }
        off += blen;
        blk_idx++;
    }
    dmb_sy();
    LogS("  Loaded GMU FW: total="); LogDec((int64_t)blk_idx);
    LogS(" ITCM="); LogDec((int64_t)itcm_blks);
    LogS(" ICACHE="); LogDec((int64_t)icache_blks);
    LogS(" DTCM="); LogDec((int64_t)dtcm_blks); LogNL();

    /* Initialize HFI Queue Table at 0x60006000 (hfi_va) */
    hfi_va[1] = 168U; hfi_va[2] = 6U; hfi_va[3] = 12U; hfi_va[4] = 3U; hfi_va[5] = 3U;
    for (uint32_t q = 0; q < 3U; q++) {
        volatile uint32_t *qh = hfi_va + 6U + 12U * q;
        qh[0] = 1U;
        qh[1] = 0x60006000U + (q + 1U) * 4096U;
        qh[2] = 0xA00U | q;
        qh[3] = 1024U;
        qh[6] = 1U; qh[7] = 1U; qh[8] = 1U;
    }
    dmb_sy();

    /* Program KGSL-SMMU Context Bank 2 (Bank=2, Asid=2, Slot=2, SID=5, CB=0x22000) */
    const uint32_t cb = 0x22000U;
    uint32_t s2cr2_init = mmio_r32(smmu, 0x0C08U);
    mmio_w32(smmu, 0x0C08U, (s2cr2_init & ~0x30000U) | 0x20000U);

    uint32_t nsacr = mmio_r32(smmu, 0x0010U);
    if (nsacr & (1U << 26)) {
        mmio_w32(smmu, 0x0010U, nsacr & ~(1U << 26));
    }
    mmio_w32(smmu, 0x0070U, 0xFFFFFFFFU);
    if (!PollMmioBit0Clear(smmu, 0x0074U, 1000U)) {
        LogS("  [ERR] SMMU global TLBSYNC timeout."); LogNL();
        goto teardown_smmu;
    }
    uint32_t nscr0 = mmio_r32(smmu, 0x0000U);
    mmio_w32(smmu, 0x0000U, (nscr0 & ~0x00C0FC37U) | 0x00C00406U);

    mmio_w32(smmu, cb + 0x000U, 0U);          /* CB2_SCTLR = 0 */
    mmio_w32(smmu, cb + 0x004U, 0x0000032BU); /* CB2_ACTLR */
    mmio_w32(smmu, cb + 0x610U, 2U);          /* CB2_TLBIASID */
    mmio_w32(smmu, cb + 0x7F0U, 0xFFFFFFFFU); /* CB2_TLBSYNC */
    if (!PollMmioBit0Clear(smmu, cb + 0x7F4U, 1000U)) {
        LogS("  [ERR] SMMU CB2 pre-init TLBSYNC timeout."); LogNL();
        goto teardown_smmu;
    }

    mmio_w32(smmu, 0x1808U, 1U);              /* CBA2R[2] = 64-bit */
    mmio_w32(smmu, 0x1008U, 0x0001F300U);     /* CBAR[2] */
    mmio_w32(smmu, cb + 0x010U, 0x00038071U); /* CB2_TCR2 */
    mmio_w32(smmu, cb + 0x030U, 0x00803519U); /* CB2_TCR */
    mmio_w64(smmu, cb + 0x020U, pool_pa | (2ULL << 48)); /* CB2_TTBR0 */
    mmio_w64(smmu, cb + 0x028U, (2ULL << 48));           /* CB2_TTBR1 */
    mmio_w32(smmu, cb + 0x038U, 0xF404FF44U); /* CB2_MAIR0 */
    mmio_w32(smmu, cb + 0x03CU, 0x0000EFE4U); /* CB2_MAIR1 */
    uint32_t fsr_pre = mmio_r32(smmu, cb + 0x058U);
    mmio_w32(smmu, cb + 0x058U, fsr_pre & 0xC00001FEU);

    mmio_w32(smmu, cb + 0x610U, 2U);
    mmio_w32(smmu, cb + 0x7F0U, 0xFFFFFFFFU);
    if (!PollMmioBit0Clear(smmu, cb + 0x7F4U, 1000U)) {
        LogS("  [ERR] SMMU CB2 post-TTBR TLBSYNC timeout."); LogNL();
        goto teardown_smmu;
    }

    mmio_w32(smmu, cb + 0x000U, 0x00C00067U); /* CB2_SCTLR enable */
    mmio_w32(smmu, 0x0808U, 0x80000005U);     /* SMR[2]: VALID | SID=5 */
    mmio_w32(smmu, 0x0C08U, 0x00000302U);     /* S2CR[2]: TRANS | PRIV=3 | CB=2 */
    dmb_sy();
    smmu_active = 1;

    LogReg("SMMU_sCR0(armed)",   0x03DA0000U, mmio_r32(smmu, 0x0000U));
    LogReg("CB2_SCTLR(armed)",   0x03DC2000U, mmio_r32(smmu, cb + 0x000U));
    LogReg("CB2_TTBR0_LO",       0x03DC2020U, mmio_r32(smmu, cb + 0x020U));
    LogReg("CB2_TTBR0_HI",       0x03DC2024U, mmio_r32(smmu, cb + 0x024U));
    LogReg("SMR[2](armed)",      0x03DA0808U, mmio_r32(smmu, 0x0808U));
    LogReg("S2CR[2](armed)",     0x03DA0C08U, mmio_r32(smmu, 0x0C08U));
    LogReg("CB2_FSR(armed)",     0x03DC2058U, mmio_r32(smmu, cb + 0x058U));

    /* Mask & clear both G2H and AO interrupt banks before configuring GMU core */
    mmio_w32(g_gmu, GMU_G2H_INTR_MASK, 0xFFFFFFFFU);
    mmio_w32(g_gmu, GMU_AO_HOST_INTR_MASK, 0x7FU);
    mmio_w32(g_gmu, GMU_G2H_INTR_CLEAR, 0xFFFFFFFFU);
    mmio_w32(g_gmu, GMU_AO_HOST_INTR_CLR, 0xFFFFFFFFU);

    /* Preboot GMU programming */
    mmio_w32(g_gmu, GMU_RPMH_CTRL, 1U);
    mmio_w32(g_gmu, GMU_RPMH_HYST_CTRL, 1U);
    mmio_w32(cx_misc, CX_MISC_TCM_RET_CNTL, 1U);
    mmio_w32(g_gmu, GMU_AO_AHB_FENCE_RANGE_0, 0x80C808A0U);
    mmio_w32(g_gmu, GMU_CM3_CFG, 0x4052U);
    mmio_w32(g_gmu, GMU_GENERAL_9, 0U);
    mmio_w32(g_gmu, GMU_GENERAL_10, 0x07020100U);
    mmio_w32(g_gmu, GMU_GENERAL_8, 0x60002003U); /* log IOVA 0x60002000, (16KB/4KB - 1)=3 */
    mmio_w32(g_gmu, GMU_SYS_BUS_CONFIG, 1U);
    mmio_w32(g_gmu, GMU_ICACHE_CONFIG, 1U);
    mmio_w32(g_gmu, GMU_DCACHE_CONFIG, 1U);

    uint32_t fw_ver = mmio_r32(g_gmu, GMU_DTCM_VERSION_OFF);
    LogReg("GMU_DTCM_VERSION(pre)", 0x03D74FE0U, fw_ver);

    /* Ensure GPUCC_PLL0 is standby-configured and GPUCC_GX_GDSCR has SW_COLLAPSE=1 for GMU ownership */
    if (g_gpucc) {
        (void)ConfigureLucidOlePllStandby(g_gpucc, GPUCC_PLL0_BASE, 0x1EU, 0xBAAAU, "gpu_cc_pll0(5B)");
        uint32_t gx_pre = mmio_r32(g_gpucc, GPUCC_GX_GDSCR);
        if (gx_pre != 0xFFFFFFFFU && (gx_pre & 1U) == 0U) {
            mmio_w32(g_gpucc, GPUCC_GX_GDSCR, gx_pre | 1U);
            (void)PollReg(g_gpucc, GPUCC_GX_CFG_GDSCR, (1U << 15), (1U << 15), 3000U);
        }
        LogReg("GPUCC_GX_GDSCR(pre-cm3)", 0x03D9905CU, mmio_r32(g_gpucc, GPUCC_GX_GDSCR));
    }

    mmio_w32(g_gmu, GMU_CM3_FW_INIT_RESULT, 0U);
    mmio_w32(g_gmu, GMU_CM3_BOOT_CONFIG, 2U);
    mmio_w32(g_gmu, GMU_HFI_QTBL_ADDR, 0x60006000U);
    mmio_w32(g_gmu, GMU_HFI_QTBL_INFO, 1U);
    dmb_sy();

    /* Release CM3 reset & poll FW_INIT_RESULT (500 ms max) */
    mmio_w32(g_gmu, GMU_CM3_SYSRESET, 0U);
    uint32_t rmask = (fw_ver <= 0x20010004U) ? 0xFFFFFFFFU : 0x1FFU;
    uint32_t rwant = (fw_ver <= 0x20010004U) ? 0xBABEFACEU : 0x100U;
    int fw_ok = 0;
    for (uint32_t ms = 0; ms < 500U; ms++) {
        uint32_t res = mmio_r32(g_gmu, GMU_CM3_FW_INIT_RESULT);
        if ((res & rmask) == rwant) {
            fw_ok = 1;
            break;
        }
        SleepMs(1U);
    }
    LogReg("GMU_CM3_FW_INIT_RES(post)", 0x03D7E070U, mmio_r32(g_gmu, GMU_CM3_FW_INIT_RESULT));
    LogReg("GMU_CM3_FW_BUSY(post)",     0x03D7E068U, mmio_r32(g_gmu, GMU_CM3_FW_BUSY));
    LogReg("GMU_G2H_INTR_INFO(post)",   0x03D7E648U, mmio_r32(g_gmu, GMU_G2H_INTR_INFO));
    LogReg("GMU_AO_HOST_INTR_ST(post)", 0x03D8EC14U, mmio_r32(g_gmu, GMU_AO_HOST_INTR_STATUS));
    LogReg("SMMU_sGFSR(post)",          0x03DA0048U, mmio_r32(smmu, 0x0048U));
    LogReg("CB2_FSR(post)",             0x03DC2058U, mmio_r32(smmu, cb + 0x058U));
    LogReg("CB2_FAR_LO(post)",          0x03DC2060U, mmio_r32(smmu, cb + 0x060U));
    LogReg("CB2_FAR_HI(post)",          0x03DC2064U, mmio_r32(smmu, cb + 0x064U));
    LogReg("CB2_FSYNR0(post)",          0x03DC2068U, mmio_r32(smmu, cb + 0x068U));

    if (!fw_ok) {
        LogS("  [WARN] GMU CM3 FW_INIT_RESULT timed out."); LogNL();
        goto stop_cm3;
    }

    /* Post-FW, pre-HFI: PDC + RSCC + S23 GEN7_2_1 20B ucode (g482_board.c order) */
    mmio_w32(pdc, PDC_GPU_SEQ_START_ADDR, 0U);
    mmio_w32(pdc, PDC_GPU_ENABLE_PDC, 0x80000001U);
    mmio_w32(rscc, RSCC_RSC_STATUS0_DRV0, 1U << 24);
    static const uint32_t kRscPairs[][2] = {
        { 0x002CU, 1U }, { 0x0038U, 0U }, { 0x0034U, 0U }, { 0x0040U, 0U },
        { 0x003CU, 0U }, { 0x0048U, 0x80000021U }, { 0x0044U, 0U }, { 0x0400U, 0U },
        { 0x0020U, 0x4520U }, { 0x0024U, 0x4510U }, { 0x0028U, 0x4514U }
    };
    for (uint32_t i = 0; i < (uint32_t)(sizeof(kRscPairs) / sizeof(kRscPairs[0])); i++) {
        mmio_w32(rscc, kRscPairs[i][0], kRscPairs[i][1]);
    }
    static const uint32_t kS23RscUcode[5] = {
        0xEAAAE5A0U, 0xE1A1EBABU, 0xA2E0A581U, 0xECAC82E2U, 0x0020EDADU
    };
    for (uint32_t i = 0; i < 5U; i++) {
        mmio_w32(rscc, RSCC_SEQ_MEM_0_DRV0 + i * 4U, kS23RscUcode[i]);
    }

    /* Start HFI */
    mmio_w32(g_gmu, GMU_HFI_INIT, 1U);
    int hfi_ok = 0;
    for (uint32_t ms = 0; ms < 200U; ms++) {
        if (mmio_r32(g_gmu, GMU_HFI_CTRL_STATUS) & 1U) {
            hfi_ok = 1;
            break;
        }
        SleepMs(1U);
    }
    LogReg("GMU_HFI_CTRL_STATUS(post)", 0x03D7E600U, mmio_r32(g_gmu, GMU_HFI_CTRL_STATUS));
    if (!hfi_ok) {
        LogS("  [WARN] GMU HFI_INIT timed out."); LogNL();
        goto stop_cm3;
    }

    /* Submit 4 G196Policy HFI startup messages + HFI 30 DCVS vote */
    static const uint32_t kGpuVotes[10] = {
        0U, 3408385U, 3408385U, 3670530U, 3932675U,
        4194820U, 5243653U, 8389382U, 9438215U, 12583944U
    };
    static const uint32_t kGpuKhz[10] = {
        0U, 124800U, 220000U, 295000U, 348000U,
        401000U, 475000U, 550000U, 615000U, 680000U
    };
    static const uint32_t kGmuVotes[3] = { 0U, 4194818U, 8389379U };
    static const uint32_t kGmuKhz[3]   = { 0U, 220000U,  550000U };
    static const uint32_t kDdrData[10][3] = {
        { 1073741824U, 1073741824U, 1073741824U },
        { 1610614770U, 1610614770U, 1610612744U },
        { 1610615593U, 1610615593U, 1610612744U },
        { 1610618520U, 1610618520U, 1610612744U },
        { 1610619090U, 1610619090U, 1610612744U },
        { 1610620518U, 1610620518U, 1610612744U },
        { 1610622914U, 1610622914U, 1610612744U },
        { 1610624592U, 1610624592U, 1610612744U },
        { 1610626448U, 1610626448U, 1610612744U },
        { 1610628450U, 1610628450U, 1610612744U },
    };

    for (uint32_t i = 0U; i < 58U; i++) g_hfi_msg_buf[i] = 0U;
    g_hfi_msg_buf[0] = 10U;
    g_hfi_msg_buf[1] = 3U;
    for (uint32_t i = 0U; i < 10U; i++) {
        g_hfi_msg_buf[2U + 3U * i] = kGpuVotes[i];
        g_hfi_msg_buf[3U + 3U * i] = 0xFFFFFFFFU;
        g_hfi_msg_buf[4U + 3U * i] = kGpuKhz[i];
    }
    for (uint32_t i = 0U; i < 3U; i++) {
        g_hfi_msg_buf[50U + 2U * i] = kGmuVotes[i];
        g_hfi_msg_buf[51U + 2U * i] = kGmuKhz[i];
    }
    if (!HfiSendAndWaitReply(g_gmu, hfi_va, 1U, 4U, g_hfi_msg_buf, 58U, "PERF_TBL")) {
        DumpGmuHfiAndLogState(g_gmu, rscc, pdc, smmu, hfi_va, log_va, "startup_fail");
        goto stop_cm3;
    }

    for (uint32_t i = 0U; i < 159U; i++) g_hfi_msg_buf[i] = 0U;
    g_hfi_msg_buf[0] = 10U; g_hfi_msg_buf[1] = 1U; g_hfi_msg_buf[2] = 3U;
    g_hfi_msg_buf[3] = 1U;  g_hfi_msg_buf[4] = 7U;
    g_hfi_msg_buf[5] = 327736U;
    g_hfi_msg_buf[11] = 1073741824U;
    g_hfi_msg_buf[17] = 1610612737U;
    g_hfi_msg_buf[23] = 327684U; g_hfi_msg_buf[24] = 327680U; g_hfi_msg_buf[25] = 327784U;
    for (uint32_t i = 0U; i < 10U; i++) {
        g_hfi_msg_buf[31U + 8U * i + 0U] = kDdrData[i][0];
        g_hfi_msg_buf[31U + 8U * i + 1U] = kDdrData[i][1];
        g_hfi_msg_buf[31U + 8U * i + 2U] = kDdrData[i][2];
    }
    if (!HfiSendAndWaitReply(g_gmu, hfi_va, 2U, 3U, g_hfi_msg_buf, 159U, "BW_TBL")) {
        DumpGmuHfiAndLogState(g_gmu, rscc, pdc, smmu, hfi_va, log_va, "startup_fail");
        goto stop_cm3;
    }

    g_hfi_msg_buf[0] = 0U;
    if (!HfiSendAndWaitReply(g_gmu, hfi_va, 3U, 14U, g_hfi_msg_buf, 1U, "CORE_START")) {
        DumpGmuHfiAndLogState(g_gmu, rscc, pdc, smmu, hfi_va, log_va, "startup_fail");
        goto stop_cm3;
    }
    if (!HfiSendAndWaitReply(g_gmu, hfi_va, 4U, 10U, g_hfi_msg_buf, 0U, "START")) {
        DumpGmuHfiAndLogState(g_gmu, rscc, pdc, smmu, hfi_va, log_va, "startup_fail");
        goto stop_cm3;
    }

    g_hfi_msg_buf[0] = 1U; g_hfi_msg_buf[1] = 1U; g_hfi_msg_buf[2] = 1U;
    int dcvs_ok = HfiSendAndWaitReply(g_gmu, hfi_va, 5U, 30U, g_hfi_msg_buf, 3U, "DCVS_VOTE");
    DumpGmuHfiAndLogState(g_gmu, rscc, pdc, smmu, hfi_va, log_va, dcvs_ok ? "post_dcvs_ok" : "post_dcvs_timeout");
    if (!dcvs_ok) {
        uint32_t g2h_now = mmio_r32(g_gmu, GMU_G2H_INTR_INFO);
        uint32_t ao_now  = mmio_r32(g_gmu, GMU_AO_HOST_INTR_STATUS);
        if (((g2h_now & 0x100U) != 0U) || ((ao_now & 0x79U) != 0U)) {
            LogS("  [WARN] CM3/AO fault detected after DCVS_VOTE timeout; skipping OOB_REQUEST."); LogNL();
            goto stop_cm3;
        }
    }

    /* Request OOB_REQUEST (BIT 30), wait OOB_ACK (BIT 31) up to 20 ms, clear ACK */
    oob_requested = 1;
    mmio_w32(g_gmu, GMU_H2G_SET, 1U << 30);
    int oob_ok = 0;
    for (uint32_t i = 0; i < 200U; i++) {
        if (mmio_r32(g_gmu, GMU_G2H_INTR_INFO) & (1U << 31)) {
            oob_ok = 1;
            break;
        }
        StallUs(100U);
    }
    LogReg("GMU_G2H_INTR_INFO(oob)",   0x03D7E648U, mmio_r32(g_gmu, GMU_G2H_INTR_INFO));
    if (oob_ok) {
        mmio_w32(g_gmu, GMU_G2H_INTR_CLEAR, 1U << 31);
    }
    uint32_t gfx_pwr_st = mmio_r32(g_gmu, GMU_GFX_PWR_CLK_STATUS);
    LogReg("GMU_GFX_PWR_CLK_ST(oob)",  0x03D7E340U, gfx_pwr_st);
    LogReg("GMU_RPMH_PWR_STATE(oob)",  0x03D7E3B0U, mmio_r32(g_gmu, GMU_RPMH_POWER_STATE));
    if (g_gpucc) {
        LogReg("GPUCC_PLL0_MODE(oob)",     0x03D90000U, mmio_r32(g_gpucc, GPUCC_PLL0_BASE + PLL_OFF_MODE));
        LogReg("GPUCC_GX_GDSCR(5B)",       0x03D9905CU, mmio_r32(g_gpucc, GPUCC_GX_GDSCR));
        LogReg("GPUCC_GX_CFG_GDSCR(5B)",   0x03D99060U, mmio_r32(g_gpucc, GPUCC_GX_CFG_GDSCR));
    }

    if (GpuGxAxiGateOpen(NULL, NULL) && ((gfx_pwr_st & 3U) == 0U)) {
        if (!g_kgsl) {
            pa.QuadPart = (int64_t)PA_KGSL;
            g_kgsl = (volatile uint8_t *)MmMapIoSpace(pa, SZ_KGSL, MmNonCached);
        }
        if (g_kgsl) {
            LogS("--- GX AXI GATE OPEN! Live Adreno 740 KGSL Telemetry ---"); LogNL();
            LogReg("KGSL_RBBM_STATUS(5B)",  0x03D00840U, mmio_r32(g_kgsl, KGSL_RBBM_STATUS));
            LogReg("KGSL_RBBM_STATUS3(5B)", 0x03D0084CU, mmio_r32(g_kgsl, KGSL_RBBM_STATUS3));
            LogReg("KGSL_GBIF_HALT(5B)",    0x03D0F114U, mmio_r32(g_kgsl, KGSL_GBIF_HALT));
            LogReg("KGSL_GBIF_HALT_ACK(5B)",0x03D0F118U, mmio_r32(g_kgsl, KGSL_GBIF_HALT_ACK));
            LogReg("KGSL_CP_SQE_CNTL(5B)",  0x03D02020U, mmio_r32(g_kgsl, KGSL_CP_SQE_CNTL));
            if (mask & 0x40U) {
                RunStage6GpuCp(mask, smmu);
            }
        }
    } else {
        LogS("  [INFO] GX gate/clock closed (PWR_CLK_ST=0x"); LogHex(gfx_pwr_st, 8);
        LogS("); skipping 0x03D00000 KGSL RBBM read."); LogNL();
    }

stop_cm3:
    if (oob_requested) {
        mmio_w32(g_gmu, GMU_H2G_SET, 1U << 31);
        StallUs(200U);
    }
    mmio_w32(g_gmu, GMU_CM3_SYSRESET, 1U);
    (void)PollReg(g_gmu, GMU_CM3_SYSRESET, 1U, 1U, 1000U);
    uint32_t rst_stopped = mmio_r32(g_gmu, GMU_CM3_SYSRESET);
    LogReg("GMU_CM3_SYSRESET(stopped)", 0x03D7E000U, rst_stopped);

teardown_smmu:
    if (smmu_active) {
        mmio_w32(smmu, 0x0C08U, 0x00020302U);
        mmio_w32(smmu, cb + 0x000U, 0U);
        mmio_w32(smmu, cb + 0x610U, 2U);
        mmio_w32(smmu, cb + 0x7F0U, 0xFFFFFFFFU);
        int tlb_done = PollMmioBit0Clear(smmu, cb + 0x7F4U, 1000U);
        mmio_w32(smmu, 0x0808U, 0U);
        mmio_w32(smmu, 0x0C08U, 0x00020000U);
        dmb_sy();

        uint32_t cm3_now = mmio_r32(g_gmu, GMU_CM3_SYSRESET);
        uint32_t sctlr_now = mmio_r32(smmu, cb + 0x000U);
        uint32_t tlb_st = mmio_r32(smmu, cb + 0x7F4U);
        LogS("  SMMU teardown: tlb_done="); LogDec((int64_t)tlb_done);
        LogS(" CM3_RST="); LogHex(cm3_now, 1);
        LogS(" CB2_SCTLR="); LogHex(sctlr_now, 8);
        LogS(" TLBSTATUS="); LogHex(tlb_st, 8); LogNL();

        if (cm3_now != 1U || sctlr_now != 0U || (tlb_st & 1U) != 0U) {
            LogS("  [WARN] Unsafe to free GMU pool (retained to avoid DMA-while-active)."); LogNL();
            goto cleanup_maps;
        }
    }

cleanup_pool:
    MmFreeContiguousMemorySpecifyCache((PVOID)pool_va, kPoolPages * 4096U, MmNonCached);

cleanup_maps:
    if (smmu)    MmUnmapIoSpace((PVOID)smmu, SZ_KGSL_SMMU);
    if (cx_misc) MmUnmapIoSpace((PVOID)cx_misc, SZ_CX_MISC);
    if (pdc)     MmUnmapIoSpace((PVOID)pdc, SZ_GMU_PDC);
    if (rscc)    MmUnmapIoSpace((PVOID)rscc, SZ_RSCC);
}

/* ============================================================================
 * DriverUnload & DriverEntry
 * ============================================================================ */
static void DriverUnload(PDRIVER_OBJECT DriverObject) {
    (void)DriverObject;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    (void)RegistryPath;
    if (DriverObject) {
        DriverObject->DriverUnload = DriverUnload;
    }

    LogOpen();
    uint32_t mask = ReadStageMask();
    LogS("=== s23gpuprobe.sys v1.0.0.7 started (StageMask=0x");
    LogHex(mask, 3);
    LogS(") ===");
    LogNL();

    PHYSICAL_ADDRESS pa;
    pa.QuadPart = (int64_t)PA_CMD_DB;
    g_cmddb = (volatile uint8_t *)MmMapIoSpace(pa, SZ_CMD_DB, MmNonCached);
    LogS("Map CMD_DB   @ 0x81C60000 -> "); LogS(g_cmddb ? "OK" : "FAIL"); LogNL();

    pa.QuadPart = (int64_t)PA_CHIPINFO;
    g_chipinfo = (volatile uint8_t *)MmMapIoSpace(pa, SZ_CHIPINFO, MmNonCached);
    LogS("Map CHIPINFO @ 0x81CF4000 -> "); LogS(g_chipinfo ? "OK" : "FAIL"); LogNL();

    pa.QuadPart = (int64_t)PA_GCC;
    g_gcc = (volatile uint8_t *)MmMapIoSpace(pa, SZ_GCC, MmNonCached);
    LogS("Map GCC      @ 0x00100000 -> "); LogS(g_gcc ? "OK" : "FAIL"); LogNL();

    pa.QuadPart = (int64_t)PA_GPUCC;
    g_gpucc = (volatile uint8_t *)MmMapIoSpace(pa, SZ_GPUCC, MmNonCached);
    LogS("Map GPUCC    @ 0x03D90000 -> "); LogS(g_gpucc ? "OK" : "FAIL"); LogNL();

    if (mask & 0x01U) {
        RunStage1ReadOnly();
    }
    if (mask & 0x02U) {
        int s2_ok = RunStage2BringUp();
        if (s2_ok && (mask & 0x04U)) {
            RunStage3ScratchAndGxTest();
        } else if (mask & 0x04U) {
            LogS("  [SKIP] Stage 0x04 requires Stage 0x02 CX gate open."); LogNL();
        }
    } else if (mask & 0x04U) {
        RunStage3ScratchAndGxTest();
    }
    if (mask & 0x08U) {
        RunStage4GmuRsccPdcDump();
    }
    if (mask & 0x10U) {
        RunStage5AScmAndSmmu(mask);
    }
    if (mask & 0x20U) {
        RunStage5BGmuBoot(mask);
    }

    LogS("=== s23gpuprobe.sys v1.0.0.7 completed cleanly ==="); LogNL();

    if (g_kgsl)     { MmUnmapIoSpace((PVOID)g_kgsl,     SZ_KGSL);          g_kgsl     = NULL; }
    if (g_gmu)      { MmUnmapIoSpace((PVOID)g_gmu,      SZ_GMU_CORE);      g_gmu      = NULL; }
    if (g_rsc)      { MmUnmapIoSpace((PVOID)g_rsc,      SZ_APPS_RSC_DRV2); g_rsc      = NULL; }
    if (g_gpucc)    { MmUnmapIoSpace((PVOID)g_gpucc,    SZ_GPUCC);         g_gpucc    = NULL; }
    if (g_gcc)      { MmUnmapIoSpace((PVOID)g_gcc,      SZ_GCC);           g_gcc      = NULL; }
    if (g_chipinfo) { MmUnmapIoSpace((PVOID)g_chipinfo, SZ_CHIPINFO);      g_chipinfo = NULL; }
    if (g_cmddb)    { MmUnmapIoSpace((PVOID)g_cmddb,    SZ_CMD_DB);        g_cmddb    = NULL; }

    LogClose();
    /* Return STATUS_UNSUCCESSFUL after logging so Demand-Start service auto-unloads cleanly */
    return STATUS_UNSUCCESSFUL;
}
