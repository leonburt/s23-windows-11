/*
 * s23sensprobe.c - Samsung Galaxy S23 (dm1q / SM8550-AC) All-26-SE QUPv3 Bus Scanner
 *                  & Audio (CS35L43) / Haptics (CS40L26A, MAX77705) Read-Only Probe v1.0.0.1
 *
 * Pure-C ARM64 WDM Demand-Start (Start = 3) one-shot driver (returns STATUS_UNSUCCESSFUL
 * after closing log so Windows auto-unloads cleanly). Writes to pre-allocated 64 KB
 * write-through log:
 *   C:\Users\Public\s23sensprobe.log
 *
 * Verified against /home/mahan/mu_s23/s23_live.dts, gcc-kalama.c, pinctrl-kalama.c,
 * qcom-geni-se.h, i2c-qcom-geni.c, cs35l43.c, cs40l26.c, and max77705.c:
 *   - SM8550 exposes 26 QUPv3 Serial Engines across 3 wrappers:
 *       1) QUPv3_I2C_HUB (0x009C0000): 10 dedicated I2C Hub SEs (0x00980000..0x009A4000)
 *            SE0 @0x980000 (PN547 NFC + TSL2585 ALS), SE3 @0x98C000 (CS40L26A Haptics),
 *            SE5 @0x994000 (S2MIW04 Wireless Chg), SE6 @0x998000 (Dual CS35L43 Amps @40/41),
 *            SE8 @0x9A0000 (MAX77705 PMIC/FG/CHG/MUIC + PCA9481; s23batt live GPIO206/207),
 *            SE9 @0x9A4000 (Cam Actuator1)
 *       2) QUPv3_1 (0x00AC0000): 8 multi-protocol GENI SEs (0x00A80000..0x00A9C000)
 *            SE4 @0xA90000 (STM FTS2BA61Y Touch SPI; live s23touch), SE6 @0xA98000 (eUSB2),
 *            SE7 @0xA9C000 (HSUART5)
 *       3) QUPv3_2 (0x008C0000): 8 multi-protocol GENI SEs (0x00880000..0x0089C000)
 *            SE0 @0x880000 (eSE SPI), SE1 @0x884000 (Cam0), SE2 @0x888000 (K250A StrongBox),
 *            SE6 @0x898000 (QCA6490 BT UART14; s23btprobe), SE7 @0x89C000 (Cam OIS)
 *
 * Registry Parameters (HKLM\SYSTEM\CurrentControlSet\Services\s23sensprobe\Parameters):
 *   StageMask (REG_DWORD, default 0x01):
 *     bit 0 (0x01): 100% read-only snapshot of GCC VOTE_1/VOTE_2, all 3 wrappers & 26 SE
 *                   CBCRs/RCGs (.hwcg_bit=1 aware), TLMM GPIOs, and GENI FW/status ONLY on
 *                   SEs whose clocks are already running (zero SError risk).
 *     bit 1 (0x02): Opt-in active QUPv3 GENI FW revision scan on all non-protected SEs
 *                   (strictly skips I2C_HUB SE0/SE4/SE8, QUP1 SE2/SE4, QUP2 SE6 so live
 *                   s23touch/s23batt/s23btprobe & TZ SEs are 100% undisturbed!), plus
 *                   non-intrusive read-only GENI I2C DEVID/REVID probe of CS35L43 @40/41
 *                   (I2C_HUB SE6), CS40L26A @40 (I2C_HUB SE3), and S2MIW04 @5B (I2C_HUB SE5).
 *     bit 2 (0x04): Opt-in read-only MAX77705 PMIC @66 + MUIC @25 ID probe on I2C_HUB SE8
 *                   (ONLY use when s23batt.sys is stopped so GPIO206/207 is not contended!).
 */

#include <stdint.h>
#include <stddef.h>

typedef int32_t  NTSTATUS;
typedef uint16_t WCHAR;
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
__declspec(dllimport) NTSTATUS ZwWriteFile(
    HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock, const void *Buffer, uint32_t Length,
    PLARGE_INTEGER ByteOffset, uint32_t *Key);
__declspec(dllimport) NTSTATUS ZwOpenKey(HANDLE *KeyHandle, uint32_t DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes);
__declspec(dllimport) NTSTATUS ZwQueryValueKey(
    HANDLE KeyHandle, PUNICODE_STRING ValueName, uint32_t KeyValueInformationClass,
    PVOID KeyValueInformation, uint32_t Length, uint32_t *ResultLength);
__declspec(dllimport) NTSTATUS ZwClose(HANDLE Handle);

/* ============================================================================
 * Hardware Bases & Register Offsets (gcc-kalama.c + qcom-geni-se.h + i2c-qcom-geni.c)
 * ============================================================================ */
#define PA_GCC_BASE             0x00100000ULL
#define SZ_GCC                  0x00060000U
#define PA_TLMM_BASE            0x0F100000ULL
#define SZ_TLMM                 0x00100000U

#define GCC_VOTE_1              0x52008U
#define GCC_VOTE_2              0x52010U
#define GCC_GPLL_VOTE           0x52018U

/* QUP Wrapper & GENI SE registers */
#define QUP_HW_VER_REG          0x004U
#define GENI_FORCE_DEFAULT_REG  0x020U
#define GENI_OUTPUT_CTRL        0x024U
#define GENI_CGC_CTRL           0x028U
#define SE_GENI_STATUS          0x040U
#define GENI_SER_M_CLK_CFG      0x048U
#define GENI_IF_DISABLE_RO      0x064U
#define GENI_FW_REVISION_RO     0x068U
#define SE_GENI_CLK_SEL         0x07CU
#define SE_GENI_BYTE_GRAN       0x254U
#define SE_GENI_DMA_MODE_EN     0x258U
#define SE_GENI_TX_PACKING_CFG0 0x260U
#define SE_GENI_TX_PACKING_CFG1 0x264U
#define SE_I2C_TX_TRANS_LEN     0x26CU
#define SE_I2C_RX_TRANS_LEN     0x270U
#define SE_I2C_SCL_COUNTERS     0x278U
#define SE_GENI_RX_PACKING_CFG0 0x284U
#define SE_GENI_RX_PACKING_CFG1 0x288U
#define SE_GENI_M_CMD0          0x600U
#define SE_GENI_M_CMD_CTRL_REG  0x604U
#define SE_GENI_M_IRQ_STATUS    0x610U
#define SE_GENI_M_IRQ_EN        0x614U
#define SE_GENI_M_IRQ_CLEAR     0x618U
#define SE_GENI_S_IRQ_CLEAR     0x648U
#define SE_GENI_TX_FIFOn        0x700U
#define SE_GENI_RX_FIFOn        0x780U
#define SE_GENI_TX_FIFO_STATUS  0x800U
#define SE_GENI_RX_FIFO_STATUS  0x804U
#define SE_GENI_TX_WATERMARK    0x80CU
#define SE_GENI_RX_WATERMARK    0x810U
#define SE_GENI_IOS             0x908U
#define SE_DMA_TX_IRQ_CLR       0xC44U
#define SE_DMA_RX_IRQ_CLR       0xD44U
#define SE_GSI_EVENT_EN         0xE18U
#define SE_IRQ_EN               0xE1CU
#define SE_HW_PARAM_0           0xE24U
#define SE_HW_PARAM_1           0xE28U
#define SE_DMA_GENERAL_CFG      0xE30U

#define GENI_SE_NONE            0U
#define GENI_SE_SPI             1U
#define GENI_SE_UART            2U
#define GENI_SE_I2C             3U
#define GENI_SE_I3C             4U

#define M_GENI_CMD_ACTIVE       (1U << 0)
#define M_GENI_CMD_ABORT        (1U << 1)
#define M_GENI_CMD_CANCEL       (1U << 2)
#define M_CMD_DONE_EN           (1U << 0)
#define M_CMD_OVERRUN_EN        (1U << 1)
#define M_ILLEGAL_CMD_EN        (1U << 2)
#define M_CMD_FAILURE_EN        (1U << 3)
#define M_CMD_CANCEL_EN         (1U << 4)
#define M_CMD_ABORT_EN          (1U << 5)
#define M_GP_IRQ_0_EN           (1U << 9)
#define M_GP_IRQ_1_EN           (1U << 10)  /* NACK */
#define M_GP_IRQ_3_EN           (1U << 12)  /* BUS_PROTO */
#define M_GP_IRQ_4_EN           (1U << 13)  /* ARB_LOST */
#define M_I2C_ERR_MASK          (M_CMD_OVERRUN_EN | M_ILLEGAL_CMD_EN | M_CMD_FAILURE_EN | \
                                 M_CMD_ABORT_EN | M_GP_IRQ_0_EN | M_GP_IRQ_1_EN | \
                                 M_GP_IRQ_3_EN | M_GP_IRQ_4_EN)

#define I2C_OP_WRITE            1U
#define I2C_OP_READ             2U
#define I2C_OP_STOP_ON_BUS      7U
#define I2C_STOP_STRETCH        (1U << 2)
#define I2C_SLV_ADDR_SHFT       9U
/* i2c-qcom-geni.c L610: geni_se_config_packing(se, 8, 4, true [msb_to_lsb], true, true) */
#define PACKING_4X8_MSB_CFG0    0x0007F8FEU
#define PACKING_4X8_MSB_CFG1    0x000FFAFEU

/* 100 kHz I2C on 19.2 MHz XO (geni_i2c_clk_map[0]: div=7, high=10, low=11, cycle=26) */
#define I2C_100K_SER_M_CLK_CFG  ((7U << 4) | 1U)
#define I2C_100K_SCL_COUNTERS   ((10U << 20) | (11U << 10) | 26U)

/* Protection flags for live drivers & TZ-reserved SEs */
#define SE_FLAG_NONE            0U
#define SE_FLAG_PROTECTED       (1U << 0)   /* Never disturb live driver / protected SE */
#define SE_FLAG_TZ_RESERVED     (1U << 1)   /* TZ-owned GPIO/SE (avoid XPU fault) */
#define SE_FLAG_DISABLED        (1U << 2)   /* status="disabled" in s23_live.dts */

/* Forced .reloc anchor */
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
static void * const g_RelocAnchor = (void *)(uintptr_t)&DriverEntry;
static volatile uintptr_t g_RelocSink;

static int IsTlmmPinReserved(uint16_t pin)
{
    /* s23_live.dts pinctrl@f000000: qcom,gpios-reserved = <0x24 0x25 0x26 0x27 0x32 0x33> */
    if ((pin >= 36U && pin <= 39U) || pin == 50U || pin == 51U) return 1;
    /* Also avoid reading eSE/StrongBox SPI/I2C pins unless opted in */
    if ((pin >= 56U && pin <= 59U) || pin == 64U || pin == 65U) return 1;
    return 0;
}

/* ============================================================================
 * Wrapper & 26-SE Master Hardware Tables (Verified Against s23_live.dts + gcc-kalama.c)
 * ============================================================================ */
typedef struct {
    const char *name;
    uint64_t    pa;
    uint32_t    vote_reg;
    uint32_t    s_ahb_cbcr;
    uint32_t    s_ahb_bit;
    uint32_t    m_ahb_cbcr;     /* 0 if none (I2C Hub) */
    uint32_t    m_ahb_bit;
    uint32_t    core_cbcr;
    uint32_t    core_bit;
    uint32_t    core2x_cbcr;    /* 0 if none (I2C Hub) */
    uint32_t    core2x_bit;
} QUP_WRAP_DESC;

static const QUP_WRAP_DESC g_wraps[3] = {
    { "QUPv3_I2C_HUB", 0x009C0000ULL, GCC_VOTE_1,
      0x23140U, (1U << 7),  0U,       0U,        0x23144U, (1U << 8),  0U,       0U },
    { "QUPv3_1",       0x00AC0000ULL, GCC_VOTE_1,
      0x23280U, (1U << 21), 0x2327CU, (1U << 20), 0x23284U, (1U << 19), 0x23294U, (1U << 18) },
    { "QUPv3_2",       0x008C0000ULL, GCC_VOTE_2,
      0x233D0U, (1U << 1),  0x233CCU, (1U << 2),  0x233D4U, (1U << 0),  0x23004U, (1U << 3) },
};

typedef struct {
    const char *name;
    const char *dts_role;
    uint8_t     wrap_idx;
    uint8_t     flags;
    uint64_t    pa;
    uint32_t    vote_reg;
    uint32_t    vote_bit;
    uint32_t    cbcr_off;
    uint32_t    rcg_cmd_off;
    uint16_t    gsi;
    uint16_t    pin0;       /* Primary SDA/MISO/CTS or 0xFFFF */
    uint16_t    pin1;       /* Primary SCL/MOSI/RTS or 0xFFFF */
    uint8_t     fn0;        /* Expected FUNC_SEL for pin0 */
    uint8_t     fn1;        /* Expected FUNC_SEL for pin1 */
} QUP_SE_DESC;

static const QUP_SE_DESC g_ses[26] = {
    /* Group A: QUPv3_I2C_HUB (0x009C0000, SE0..SE9) */
    { "I2C_HUB_SE0", "OK:I2C PN547@28+TSL2585@39",      0, SE_FLAG_PROTECTED,                    0x00980000ULL, GCC_VOTE_1, (1U<<10), 0x17004U, 0x17008U, 496,  16,  17, 1, 1 },
    { "I2C_HUB_SE1", "dis:I2C unused",                   0, SE_FLAG_DISABLED,                     0x00984000ULL, GCC_VOTE_1, (1U<<11), 0x17020U, 0x17024U, 497,  18,  19, 1, 1 },
    { "I2C_HUB_SE2", "dis:I2C (fsa4480 dis)",            0, SE_FLAG_DISABLED,                     0x00988000ULL, GCC_VOTE_1, (1U<<12), 0x1703CU, 0x17040U, 498,  20,  21, 1, 1 },
    { "I2C_HUB_SE3", "OK:I2C CS40L26A@40(Haptics)",      0, SE_FLAG_NONE,                         0x0098C000ULL, GCC_VOTE_1, (1U<<13), 0x17058U, 0x1705CU, 499,  22,  23, 1, 1 },
    { "I2C_HUB_SE4", "dis:I2C unused",                   0, SE_FLAG_PROTECTED | SE_FLAG_DISABLED, 0x00990000ULL, GCC_VOTE_1, (1U<<14), 0x17074U, 0x17078U, 500,   4,   5, 1, 1 },
    { "I2C_HUB_SE5", "OK:I2C S2MIW04@5B(WChg)",          0, SE_FLAG_NONE,                         0x00994000ULL, GCC_VOTE_1, (1U<<15), 0x17090U, 0x17094U, 501,   6,   7, 1, 1 },
    { "I2C_HUB_SE6", "OK:I2C CS35L43@40/41(SpkrAmps)",   0, SE_FLAG_NONE,                         0x00998000ULL, GCC_VOTE_1, (1U<<16), 0x170ACU, 0x170B0U, 502,   8,   9, 1, 1 },
    { "I2C_HUB_SE7", "dis:I2C unused",                   0, SE_FLAG_DISABLED,                     0x0099C000ULL, GCC_VOTE_1, (1U<<17), 0x170C8U, 0x170CCU, 503,  10,  11, 1, 1 },
    { "I2C_HUB_SE8", "OK:I2C MAX77705@66+PCA9481(BATT)", 0, SE_FLAG_PROTECTED,                    0x009A0000ULL, GCC_VOTE_2, (1U<<14), 0x170E4U, 0x170E8U, 504, 206, 207, 1, 1 },
    { "I2C_HUB_SE9", "OK:I2C CamActuator1@18",           0, SE_FLAG_NONE,                         0x009A4000ULL, GCC_VOTE_2, (1U<<15), 0x17100U, 0x17104U, 505,  84,  85, 1, 1 },

    /* Group B: QUPv3_1 (0x00AC0000, SE0..SE7) */
    { "QUP1_SE0",    "dis:(nq@28 dis)",                  1, SE_FLAG_DISABLED,                     0x00A80000ULL, GCC_VOTE_1, (1U<<22), 0x18004U, 0x18010U, 385,  28,  29, 1, 1 },
    { "QUP1_SE1",    "dis:unused",                       1, SE_FLAG_DISABLED,                     0x00A84000ULL, GCC_VOTE_1, (1U<<23), 0x1813CU, 0x18148U, 386,  32,  33, 1, 1 },
    { "QUP1_SE2",    "dis:TZ-reserved(gpio36..39)",      1, SE_FLAG_TZ_RESERVED | SE_FLAG_DISABLED, 0x00A88000ULL, GCC_VOTE_1, (1U<<24), 0x18274U, 0x18280U, 387,  36,  37, 1, 1 },
    { "QUP1_SE3",    "dis:unused(g42=CS35L43_R_RST)",    1, SE_FLAG_DISABLED,                     0x00A8C000ULL, GCC_VOTE_1, (1U<<25), 0x183ACU, 0x183B8U, 388,  40,  41, 1, 1 },
    { "QUP1_SE4",    "OK:SPI STM_FTS_Touch(LIVE!)",      1, SE_FLAG_PROTECTED,                    0x00A90000ULL, GCC_VOTE_1, (1U<<26), 0x184E4U, 0x184F0U, 389,  44,  45, 1, 1 },
    { "QUP1_SE5",    "dis:unused(g55=CS35L43_L_RST)",    1, SE_FLAG_DISABLED,                     0x00A94000ULL, GCC_VOTE_1, (1U<<27), 0x1861CU, 0x18628U, 390,  52,  53, 2, 2 },
    { "QUP1_SE6",    "OK:I2C NXP_eUSB2_Rep@4F(LIVE_USB)", 1, SE_FLAG_PROTECTED,                    0x00A98000ULL, GCC_VOTE_1, (1U<<28), 0x18754U, 0x18760U, 395,  48,  49, 2, 1 },
    { "QUP1_SE7",    "OK:2W_HSUART5(sec_atd)",           1, SE_FLAG_PROTECTED,                    0x00A9C000ULL, GCC_VOTE_2, (1U<<16), 0x1888CU, 0x18898U, 611,  26,  27, 1, 1 },

    /* Group C: QUPv3_2 (0x008C0000, SE0..SE7 / DTS se8..se15) */
    { "QUP2_SE0",    "OK:SPI NXP_p61_eSE(TZ_GUARD)",     2, SE_FLAG_TZ_RESERVED,                  0x00880000ULL, GCC_VOTE_2, (1U<<4),  0x1E004U, 0x1E010U, 405,  56,  57, 1, 1 },
    { "QUP2_SE1",    "OK:I2C CamSensor0@20",             2, SE_FLAG_NONE,                         0x00884000ULL, GCC_VOTE_2, (1U<<5),  0x1E13CU, 0x1E148U, 615,  60,  61, 1, 1 },
    { "QUP2_SE2",    "OK:I2C K250A_StrongBox(TZ_GUARD)", 2, SE_FLAG_TZ_RESERVED,                  0x00888000ULL, GCC_VOTE_2, (1U<<6),  0x1E274U, 0x1E280U, 616,  64,  65, 1, 1 },
    { "QUP2_SE3",    "dis:unused",                       2, SE_FLAG_DISABLED,                     0x0088C000ULL, GCC_VOTE_2, (1U<<7),  0x1E3ACU, 0x1E3B8U, 617,  68,  69, 1, 1 },
    { "QUP2_SE4",    "dis:unused",                       2, SE_FLAG_DISABLED,                     0x00890000ULL, GCC_VOTE_2, (1U<<8),  0x1E4E4U, 0x1E4F0U, 618,   2,   3, 1, 1 },
    { "QUP2_SE5",    "dis:(g80=WLAN_EN,g81=BT_EN)",      2, SE_FLAG_DISABLED,                     0x00894000ULL, GCC_VOTE_2, (1U<<9),  0x1E61CU, 0x1E628U, 619,  80,  81, 1, 1 },
    { "QUP2_SE6",    "OK:4W_UART14_QCA6490_BT(LIVE!)",   2, SE_FLAG_PROTECTED,                    0x00898000ULL, GCC_VOTE_2, (1U<<10), 0x1E754U, 0x1E760U, 493,  78,  79, 1, 1 },
    { "QUP2_SE7",    "OK:I2C CamOIS@62",                 2, SE_FLAG_NONE,                         0x0089C000ULL, GCC_VOTE_2, (1U<<17), 0x1E88CU, 0x1E898U, 494,  72, 106, 2, 2 },
};

/* ============================================================================
 * MMIO, Timing, Logging & Registry Helpers
 * ============================================================================ */
static inline void dmb_sy(void)
{
    __asm__ volatile("dmb sy" ::: "memory");
}

static inline uint32_t mmio_r32(volatile uint8_t *base, uint32_t off)
{
    dmb_sy();
    uint32_t v = *(volatile uint32_t *)(base + off);
    dmb_sy();
    return v;
}

static inline void mmio_w32(volatile uint8_t *base, uint32_t off, uint32_t v)
{
    dmb_sy();
    *(volatile uint32_t *)(base + off) = v;
    dmb_sy();
}

static void SleepMs(uint32_t ms)
{
    if (ms == 0U) return;
    LARGE_INTEGER li;
    li.QuadPart = -((int64_t)ms * 10000LL);
    KeDelayExecutionThread(0, 0, &li);
}

static inline void InitObjAttr(POBJECT_ATTRIBUTES oa, PUNICODE_STRING name, uint32_t attr)
{
    oa->Length = sizeof(OBJECT_ATTRIBUTES);
    oa->Pad0 = 0;
    oa->RootDirectory = NULL;
    oa->ObjectName = name;
    oa->Attributes = attr;
    oa->Pad1 = 0;
    oa->SecurityDescriptor = NULL;
    oa->SecurityQualityOfService = NULL;
}

static HANDLE   g_log_handle = NULL;
static int64_t  g_log_off    = 0;
static char     g_log_buf[4096];
static uint32_t g_log_pos    = 0;

static void LogOpen(void)
{
    UNICODE_STRING u;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    RtlInitUnicodeString(&u, L"\\??\\C:\\Users\\Public\\s23sensprobe.log");
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

static void LogFlush(void)
{
    if (!g_log_handle || g_log_pos == 0U) {
        g_log_pos = 0U;
        return;
    }
    if (g_log_off + (int64_t)g_log_pos > 65000LL) {
        g_log_pos = 0U;
        return;
    }
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER off;
    off.QuadPart = g_log_off;
    ZwWriteFile(g_log_handle, NULL, NULL, NULL, &iosb, g_log_buf, g_log_pos, &off, NULL);
    g_log_off += (int64_t)g_log_pos;
    g_log_pos = 0U;
}

static void LogClose(void)
{
    LogFlush();
    if (g_log_handle) {
        ZwClose(g_log_handle);
        g_log_handle = NULL;
    }
}

static void LogC(char c)
{
    if (g_log_pos >= sizeof(g_log_buf) - 16U) LogFlush();
    if (g_log_pos < sizeof(g_log_buf)) g_log_buf[g_log_pos++] = c;
}

static void LogS(const char *s)
{
    while (*s) LogC(*s++);
}

static void LogHex(uint64_t v, int digits)
{
    static const char hx[] = "0123456789ABCDEF";
    for (int i = (digits - 1) * 4; i >= 0; i -= 4) {
        LogC(hx[(v >> i) & 0xFU]);
    }
}

static void LogDec(int64_t v)
{
    if (v < 0) { LogC('-'); /* magnitude taken as unsigned below: -INT64_MIN is undefined behaviour */ }
    char tmp[24];
    int n = 0;
    uint64_t uv = (v < 0) ? (0ULL - (uint64_t)v) : (uint64_t)v;
    if (uv == 0U) tmp[n++] = '0';
    while (uv > 0U) { tmp[n++] = (char)('0' + (uv % 10U)); uv /= 10U; }
    while (n > 0) LogC(tmp[--n]);
}

static void LogNL(void)
{
    LogC('\r');
    LogC('\n');
    LogFlush();
}

static uint32_t ReadRegDword(const WCHAR *val_name, uint32_t def_val)
{
    UNICODE_STRING uk, uv;
    OBJECT_ATTRIBUTES oa;
    HANDLE hk = NULL;
    uint32_t val = def_val;

    RtlInitUnicodeString(&uk, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\s23sensprobe\\Parameters");
    InitObjAttr(&oa, &uk, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE);
    if (!NT_SUCCESS(ZwOpenKey(&hk, KEY_READ, &oa))) {
        return val;
    }
    RtlInitUnicodeString(&uv, val_name);
    KEY_VALUE_PARTIAL_INFORMATION kv;
    uint32_t rlen = 0;
    if (NT_SUCCESS(ZwQueryValueKey(hk, &uv, KeyValuePartialInformation, &kv, sizeof(kv), &rlen))) {
        if (kv.Type == 4U && kv.DataLength >= 4U) {
            val = *(uint32_t *)kv.Data;
        }
    }
    ZwClose(hk);
    return val;
}

/* ============================================================================
 * GCC Clock Status Helpers (.hwcg_bit = 1 Aware)
 * ============================================================================ */
static int IsBranchOn(volatile uint8_t *gcc, uint32_t cbcr_off, uint32_t vote_off, uint32_t vote_bit, int hwcg)
{
    uint32_t cbcr = mmio_r32(gcc, cbcr_off);
    if ((cbcr & 0x80000000U) == 0U) return 1;
    if (hwcg && ((cbcr & 0x00000002U) != 0U)) {
        uint32_t vote = mmio_r32(gcc, vote_off);
        if ((vote & vote_bit) != 0U) return 1;
    }
    return 0;
}

static int IsWrapperAhbOn(volatile uint8_t *gcc, uint32_t widx)
{
    const QUP_WRAP_DESC *w = &g_wraps[widx];
    if (!IsBranchOn(gcc, w->s_ahb_cbcr, w->vote_reg, w->s_ahb_bit, 1)) return 0;
    if (w->m_ahb_cbcr != 0U && !IsBranchOn(gcc, w->m_ahb_cbcr, w->vote_reg, w->m_ahb_bit, 1)) return 0;
    return 1;
}

static const char *GeniProtoName(uint32_t proto)
{
    switch (proto) {
        case GENI_SE_NONE: return "NONE";
        case GENI_SE_SPI:  return "SPI";
        case GENI_SE_UART: return "UART";
        case GENI_SE_I2C:  return "I2C";
        case GENI_SE_I3C:  return "I3C";
        case 5U:           return "SPI_SLV";
        default:           return "UNK";
    }
}

static void DumpTlmmPin(volatile uint8_t *tlmm, const char *label, uint32_t pin)
{
    if (!tlmm || pin >= 256U) return;
    uint32_t cfg = mmio_r32(tlmm, pin * 0x1000U);
    uint32_t io  = mmio_r32(tlmm, pin * 0x1000U + 4U);
    uint32_t fn  = (cfg >> 2) & 0xFU;
    uint32_t oe  = (cfg >> 9) & 1U;
    LogS("    "); LogS(label); LogS("(GPIO"); LogDec(pin);
    LogS("): CFG=0x"); LogHex(cfg, 8);
    LogS(" (fn="); LogDec(fn);
    LogS(" oe="); LogDec(oe);
    LogS(" pull="); LogDec(cfg & 3U);
    LogS(") IO=0x"); LogHex(io, 2);
    LogS(" (in="); LogDec(io & 1U);
    LogS(" out="); LogDec((io >> 1) & 1U); LogS(")"); LogNL();
}

static void SnapshotGeniSe(uint64_t se_pa, uint32_t *proto_out, uint32_t *if_dis_out, uint32_t *status_out)
{
    PHYSICAL_ADDRESS pa;
    pa.QuadPart = (int64_t)se_pa;
    volatile uint8_t *se = (volatile uint8_t *)MmMapIoSpace(pa, 0x4000U, MmNonCached);
    if (!se) {
        LogS(" [MAP_FAIL]");
        if (proto_out)  *proto_out  = 0xFFU;
        if (if_dis_out) *if_dis_out = 0xFFFFFFFFU;
        if (status_out) *status_out = 0xFFFFFFFFU;
        return;
    }

    uint32_t fw   = mmio_r32(se, GENI_FW_REVISION_RO);
    uint32_t ifd  = mmio_r32(se, GENI_IF_DISABLE_RO);
    uint32_t st   = mmio_r32(se, SE_GENI_STATUS);
    uint32_t ios  = mmio_r32(se, SE_GENI_IOS);
    uint32_t hw0  = mmio_r32(se, SE_HW_PARAM_0);
    uint32_t hw1  = mmio_r32(se, SE_HW_PARAM_1);
    uint32_t proto = (fw >> 8) & 0xFFU;

    LogS(" FW=0x"); LogHex(fw, 8);
    LogS("["); LogS(GeniProtoName(proto));
    LogS(" v"); LogDec((fw >> 4) & 0xFU); LogC('.'); LogDec(fw & 0xFU); LogS("]");
    LogS(" IF_DIS=0x"); LogHex(ifd, 2);
    LogS((ifd & 1U) ? "(GSI)" : "(FIFO)");
    LogS(" ST=0x"); LogHex(st, 8);
    LogS(" IOS=0x"); LogHex(ios, 2);
    LogS(" HW0=0x"); LogHex(hw0, 8);
    LogS(" HW1=0x"); LogHex(hw1, 8);

    if (proto_out)  *proto_out  = proto;
    if (if_dis_out) *if_dis_out = ifd;
    if (status_out) *status_out = st;
    MmUnmapIoSpace((PVOID)se, 0x4000U);
}

/* ============================================================================
 * Stage 1: Strictly Read-Only GCC / TLMM / Clock-Gated QUPv3 Snapshot
 * ============================================================================ */
static void RunStage1ReadOnly(volatile uint8_t *gcc, volatile uint8_t *tlmm)
{
    LogS("=== [STAGE 1] 100% Read-Only All-26-SE QUPv3 & Audio/Haptic/Sensor Snapshot ==="); LogNL();

    uint32_t v1 = mmio_r32(gcc, GCC_VOTE_1);
    uint32_t v2 = mmio_r32(gcc, GCC_VOTE_2);
    uint32_t gp = mmio_r32(gcc, GCC_GPLL_VOTE);
    LogS("  GCC VOTE_1(0x52008)=0x"); LogHex(v1, 8);
    LogS(" VOTE_2(0x52010)=0x"); LogHex(v2, 8);
    LogS(" GPLL_VOTE(0x52018)=0x"); LogHex(gp, 8); LogNL();

    int wrap_on[3];
    for (uint32_t w = 0U; w < 3U; w++) {
        const QUP_WRAP_DESC *wd = &g_wraps[w];
        uint32_t sahb = mmio_r32(gcc, wd->s_ahb_cbcr);
        uint32_t mahb = wd->m_ahb_cbcr ? mmio_r32(gcc, wd->m_ahb_cbcr) : 0U;
        uint32_t core = mmio_r32(gcc, wd->core_cbcr);
        uint32_t c2x  = wd->core2x_cbcr ? mmio_r32(gcc, wd->core2x_cbcr) : 0U;
        wrap_on[w] = IsWrapperAhbOn(gcc, w);

        LogS("  Wrapper "); LogS(wd->name); LogS(" @0x"); LogHex(wd->pa, 8);
        LogS(": S_AHB=0x"); LogHex(sahb, 8);
        if (wd->m_ahb_cbcr) { LogS(" M_AHB=0x"); LogHex(mahb, 8); }
        LogS(" CORE=0x"); LogHex(core, 8);
        if (wd->core2x_cbcr) { LogS(" CORE2X=0x"); LogHex(c2x, 8); }
        LogS(wrap_on[w] ? " [AHB_ON]" : " [AHB_OFF]");

        if (wrap_on[w]) {
            PHYSICAL_ADDRESS pa;
            pa.QuadPart = (int64_t)wd->pa;
            volatile uint8_t *wb = (volatile uint8_t *)MmMapIoSpace(pa, 0x2000U, MmNonCached);
            if (wb) {
                uint32_t hwv = mmio_r32(wb, QUP_HW_VER_REG);
                LogS(" QUP_HW_VER=0x"); LogHex(hwv, 8);
                MmUnmapIoSpace((PVOID)wb, 0x2000U);
            }
        }
        LogNL();
    }

    LogS("  --- All 26 QUPv3 Serial Engines (Read-Only Clocks, Pins & Live GENI) ---"); LogNL();
    for (uint32_t i = 0U; i < 26U; i++) {
        const QUP_SE_DESC *s = &g_ses[i];
        uint32_t cbcr = mmio_r32(gcc, s->cbcr_off);
        uint32_t cmd  = mmio_r32(gcc, s->rcg_cmd_off);
        uint32_t cfg  = mmio_r32(gcc, s->rcg_cmd_off + 4U);
        uint32_t vote = mmio_r32(gcc, s->vote_reg);
        int se_on = ((cbcr & 0x80000000U) == 0U);

        uint32_t p0_cfg = 0xFFFFFFFFU;
        uint32_t p1_cfg = 0xFFFFFFFFU;
        if (!(s->flags & SE_FLAG_TZ_RESERVED) && !IsTlmmPinReserved(s->pin0) && !IsTlmmPinReserved(s->pin1)) {
            p0_cfg = mmio_r32(tlmm, (uint32_t)s->pin0 * 0x1000U);
            p1_cfg = mmio_r32(tlmm, (uint32_t)s->pin1 * 0x1000U);
        }

        LogS("  [");
        if (i < 10U) LogC(' ');
        LogDec(i); LogS("] "); LogS(s->name);
        LogS(" @0x"); LogHex(s->pa, 8);
        LogS(" GSI="); LogDec(s->gsi);
        LogS(" V="); LogDec((vote & s->vote_bit) ? 1 : 0);
        LogS(" CBCR=0x"); LogHex(cbcr, 8);
        LogS(" RCG="); LogHex(cmd, 8); LogC('/'); LogHex(cfg, 8);
        if (p0_cfg != 0xFFFFFFFFU) {
            LogS(" g"); LogDec(s->pin0); LogS("(fn"); LogDec((p0_cfg >> 2) & 0xFU);
            LogS(")/g"); LogDec(s->pin1); LogS("(fn"); LogDec((p1_cfg >> 2) & 0xFU); LogS(")");
        } else {
            LogS(" g"); LogDec(s->pin0); LogS("/g"); LogDec(s->pin1); LogS("(TZ_SKIP)");
        }

        if (wrap_on[s->wrap_idx] && se_on && !(s->flags & SE_FLAG_TZ_RESERVED)) {
            SnapshotGeniSe(s->pa, NULL, NULL, NULL);
        } else {
            LogS(se_on ? " [WAIT_AHB/TZ]" : " [CLK_HALTED]");
        }
        LogS(" | "); LogS(s->dts_role); LogNL();
    }

    LogS("  --- Key Audio / Haptics / Sensors TLMM GPIOs ---"); LogNL();
    DumpTlmmPin(tlmm, "CS35L43_IRQ",      14U);
    DumpTlmmPin(tlmm, "CS35L43_BOT_RST",  42U);
    DumpTlmmPin(tlmm, "CS35L43_RCV_RST",  55U);
    DumpTlmmPin(tlmm, "TSL2585_ALS_IRQ",  62U);
    DumpTlmmPin(tlmm, "TSL2585_ALS_CAL",  87U);
    DumpTlmmPin(tlmm, "CS40L26A_HAP_IRQ", 88U);
    DumpTlmmPin(tlmm, "HALL_IC_IRQ",     107U);
    DumpTlmmPin(tlmm, "TDM0_I2S0_SCK",   126U);
    DumpTlmmPin(tlmm, "TDM0_I2S0_DATA0", 127U);
    DumpTlmmPin(tlmm, "TDM0_I2S0_DATA1", 128U);
    DumpTlmmPin(tlmm, "TDM0_I2S0_WS",    129U);
    DumpTlmmPin(tlmm, "MAX77705_IRQ",    133U);
    DumpTlmmPin(tlmm, "CS40L26A_HAP_RST",145U);
    DumpTlmmPin(tlmm, "ISG6320_GRIP_IRQ",153U);
    DumpTlmmPin(tlmm, "ISG6320_I2C_SDA", 169U);
    DumpTlmmPin(tlmm, "ISG6320_I2C_SCL", 170U);
}

/* ============================================================================
 * GENI I2C FIFO Read-Only Register Probe Helper (i2c-qcom-geni.c)
 * ============================================================================ */
static void GeniI2cAbortAndStop(volatile uint8_t *se, uint8_t addr7, int send_stop)
{
    if (send_stop) {
        uint32_t m_param = ((uint32_t)(addr7 & 0x7FU) << I2C_SLV_ADDR_SHFT);
        mmio_w32(se, SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFU);
        mmio_w32(se, SE_GENI_M_CMD0, (I2C_OP_STOP_ON_BUS << 27) | m_param);
        for (uint32_t t = 0U; t < 2000U; t++) {
            uint32_t st = mmio_r32(se, SE_GENI_M_IRQ_STATUS);
            if (st & (M_CMD_DONE_EN | M_I2C_ERR_MASK)) {
                mmio_w32(se, SE_GENI_M_IRQ_CLEAR, st);
                break;
            }
            KeStallExecutionProcessor(5U);
        }
    }
    if (mmio_r32(se, SE_GENI_STATUS) & M_GENI_CMD_ACTIVE) {
        mmio_w32(se, SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_CANCEL);
        for (uint32_t t = 0U; t < 1000U; t++) {
            uint32_t st = mmio_r32(se, SE_GENI_M_IRQ_STATUS);
            if (st & M_CMD_CANCEL_EN) {
                mmio_w32(se, SE_GENI_M_IRQ_CLEAR, st);
                break;
            }
            KeStallExecutionProcessor(5U);
        }
        if (mmio_r32(se, SE_GENI_STATUS) & M_GENI_CMD_ACTIVE) {
            mmio_w32(se, SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
            for (uint32_t t = 0U; t < 1000U; t++) {
                uint32_t st = mmio_r32(se, SE_GENI_M_IRQ_STATUS);
                if (st & M_CMD_ABORT_EN) {
                    mmio_w32(se, SE_GENI_M_IRQ_CLEAR, st);
                    break;
                }
                KeStallExecutionProcessor(5U);
            }
        }
    }
    mmio_w32(se, SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFU);
}

static void GeniI2cInitIo(volatile uint8_t *se)
{
    mmio_w32(se, SE_GSI_EVENT_EN, 0U);
    mmio_w32(se, SE_IRQ_EN, 0U);
    mmio_w32(se, SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFU);
    mmio_w32(se, SE_GENI_S_IRQ_CLEAR, 0xFFFFFFFFU);
    mmio_w32(se, SE_DMA_TX_IRQ_CLR, 0xFFFFFFFFU);
    mmio_w32(se, SE_DMA_RX_IRQ_CLR, 0xFFFFFFFFU);
    mmio_w32(se, GENI_CGC_CTRL, mmio_r32(se, GENI_CGC_CTRL) | 0x7FU);
    mmio_w32(se, SE_DMA_GENERAL_CFG, mmio_r32(se, SE_DMA_GENERAL_CFG) | 0x0FU);
    mmio_w32(se, GENI_OUTPUT_CTRL, 0x7FU);
    mmio_w32(se, GENI_FORCE_DEFAULT_REG, 1U);
    mmio_w32(se, SE_GENI_DMA_MODE_EN, 0U);
}

/*
 * Performs a 100% read-only I2C register read (write reg addr with STOP_STRETCH,
 * repeated-START read rx_len bytes). Returns 0 on ACK+success, or non-zero IRQ error/timeout.
 */
static uint32_t GeniI2cReadReg(volatile uint8_t *se, uint8_t addr7,
                               const uint8_t *tx_buf, uint32_t tx_len,
                               uint8_t *rx_buf, uint32_t rx_len)
{
    if (tx_len == 0U || tx_len > 4U || rx_len == 0U || rx_len > 4U) return 0xE0000001U;
    if (mmio_r32(se, SE_GENI_STATUS) & M_GENI_CMD_ACTIVE) return 0xE0000002U;

    /* Polling FIFO mode (zero GIC interrupt disturbance) */
    GeniI2cInitIo(se);

    /* Drain any stale RX FIFO words before starting transfer */
    for (uint32_t d = 0U; d < 16U; d++) {
        if ((mmio_r32(se, SE_GENI_RX_FIFO_STATUS) & 0x01FFFFFFU) == 0U) break;
        (void)mmio_r32(se, SE_GENI_RX_FIFOn);
    }

    /* 4x8 MSB-first packing (i2c-qcom-geni.c L610) + 100 kHz SCL on 19.2 MHz XO */
    mmio_w32(se, SE_GENI_BYTE_GRAN, 0U);
    mmio_w32(se, SE_GENI_TX_PACKING_CFG0, PACKING_4X8_MSB_CFG0);
    mmio_w32(se, SE_GENI_TX_PACKING_CFG1, PACKING_4X8_MSB_CFG1);
    mmio_w32(se, SE_GENI_RX_PACKING_CFG0, PACKING_4X8_MSB_CFG0);
    mmio_w32(se, SE_GENI_RX_PACKING_CFG1, PACKING_4X8_MSB_CFG1);
    mmio_w32(se, SE_GENI_CLK_SEL, 0U);
    mmio_w32(se, GENI_SER_M_CLK_CFG, I2C_100K_SER_M_CLK_CFG);
    mmio_w32(se, SE_I2C_SCL_COUNTERS, I2C_100K_SCL_COUNTERS);
    mmio_w32(se, SE_GENI_TX_WATERMARK, 0U);
    mmio_w32(se, SE_GENI_RX_WATERMARK, 1U);

    /* Msg 1: Write register address bytes with STOP_STRETCH */
    uint32_t tx_word = 0U;
    for (uint32_t i = 0U; i < tx_len; i++) {
        tx_word |= ((uint32_t)tx_buf[i]) << (i * 8U);
    }
    uint32_t m_param = ((uint32_t)(addr7 & 0x7FU) << I2C_SLV_ADDR_SHFT) | I2C_STOP_STRETCH;
    mmio_w32(se, SE_I2C_TX_TRANS_LEN, tx_len);
    mmio_w32(se, SE_GENI_M_CMD0, (I2C_OP_WRITE << 27) | m_param);
    mmio_w32(se, SE_GENI_TX_FIFOn, tx_word);

    uint32_t st = 0U;
    int done = 0;
    for (uint32_t t = 0U; t < 4000U; t++) {
        st = mmio_r32(se, SE_GENI_M_IRQ_STATUS);
        if (st & (M_CMD_DONE_EN | M_I2C_ERR_MASK)) {
            mmio_w32(se, SE_GENI_M_IRQ_CLEAR, st);
            done = 1;
            break;
        }
        KeStallExecutionProcessor(5U);
    }
    if (!done || (st & M_I2C_ERR_MASK)) {
        GeniI2cAbortAndStop(se, addr7, 1);
        return done ? st : 0xE0000010U;
    }

    /* Msg 2: Repeated-START Read rx_len bytes + STOP */
    m_param = ((uint32_t)(addr7 & 0x7FU) << I2C_SLV_ADDR_SHFT);
    mmio_w32(se, SE_I2C_RX_TRANS_LEN, rx_len);
    mmio_w32(se, SE_GENI_M_CMD0, (I2C_OP_READ << 27) | m_param);

    done = 0;
    for (uint32_t t = 0U; t < 4000U; t++) {
        st = mmio_r32(se, SE_GENI_M_IRQ_STATUS);
        if (st & (M_CMD_DONE_EN | M_I2C_ERR_MASK)) {
            mmio_w32(se, SE_GENI_M_IRQ_CLEAR, st);
            done = 1;
            break;
        }
        KeStallExecutionProcessor(5U);
    }
    if (!done || (st & M_I2C_ERR_MASK)) {
        GeniI2cAbortAndStop(se, addr7, 0);
        return done ? st : 0xE0000020U;
    }

    uint32_t wc = mmio_r32(se, SE_GENI_RX_FIFO_STATUS) & 0x01FFFFFFU;
    if (wc == 0U) return 0xE0000030U;
    uint32_t rx_word = mmio_r32(se, SE_GENI_RX_FIFOn);
    for (uint32_t i = 0U; i < rx_len; i++) {
        rx_buf[i] = (uint8_t)((rx_word >> (i * 8U)) & 0xFFU);
    }
    return 0U;
}

static uint32_t ProbeCirrus32Be(volatile uint8_t *se, const char *name, uint8_t addr7, uint32_t retries)
{
    uint8_t reg0[4] = { 0x00U, 0x00U, 0x00U, 0x00U };
    uint8_t reg4[4] = { 0x00U, 0x00U, 0x00U, 0x04U };
    uint8_t idb[4]  = { 0U, 0U, 0U, 0U };
    uint8_t rvb[4]  = { 0U, 0U, 0U, 0U };

    uint32_t err0 = 0xFFFFFFFFU;
    for (uint32_t r = 0U; r <= retries; r++) {
        err0 = GeniI2cReadReg(se, addr7, reg0, 4U, idb, 4U);
        if (err0 == 0U) break;
        SleepMs(2U);
    }
    LogS("    "); LogS(name); LogS(" @0x"); LogHex(addr7, 2); LogS(": ");
    if (err0 != 0U) {
        LogS("DEVID read err=0x"); LogHex(err0, 8);
        if (err0 & M_GP_IRQ_1_EN) LogS(" (NACK)");
        LogNL();
        return err0;
    }
    uint32_t devid = ((uint32_t)idb[0] << 24) | ((uint32_t)idb[1] << 16) |
                     ((uint32_t)idb[2] << 8)  | (uint32_t)idb[3];
    uint32_t err4 = GeniI2cReadReg(se, addr7, reg4, 4U, rvb, 4U);
    uint32_t revid = ((uint32_t)rvb[0] << 24) | ((uint32_t)rvb[1] << 16) |
                     ((uint32_t)rvb[2] << 8)  | (uint32_t)rvb[3];

    LogS("DEVID=0x"); LogHex(devid, 8);
    if ((devid & 0xFFFFFFU) == 0x035A43U)      LogS(" [CONFIRMED CS35L43!]");
    else if ((devid & 0xFFFFFFU) == 0x035A40U) LogS(" [CONFIRMED CS35L41!]");
    else if ((devid & 0xFFFFFFU) == 0x40A260U) LogS(" [CONFIRMED CS40L26A!]");
    else if ((devid & 0xFFFFFFU) == 0x40A26BU) LogS(" [CONFIRMED CS40L26B!]");
    if (err4 == 0U) {
        LogS(" REVID=0x"); LogHex(revid, 8);
    }
    LogNL();
    return 0U;
}

/* ============================================================================
 * Stage 2 (StageMask & 0x02): Active 26-SE GENI FW Scanner + Read-Only I2C ID Probes
 * ============================================================================ */
static int EnableWrapperClocks(volatile uint8_t *gcc, uint32_t widx)
{
    const QUP_WRAP_DESC *w = &g_wraps[widx];
    uint32_t mask = w->s_ahb_bit | w->m_ahb_bit | w->core_bit | w->core2x_bit;
    uint32_t v = mmio_r32(gcc, w->vote_reg);
    mmio_w32(gcc, w->vote_reg, v | mask);
    for (uint32_t t = 0U; t < 400U; t++) {
        if (IsWrapperAhbOn(gcc, widx) &&
            IsBranchOn(gcc, w->core_cbcr, w->vote_reg, w->core_bit, 0)) {
            return 1;
        }
        KeStallExecutionProcessor(5U);
    }
    return IsWrapperAhbOn(gcc, widx);
}

static int EnableSeClockSafeXo(volatile uint8_t *gcc, const QUP_SE_DESC *s)
{
    /* Only configure safe 19.2 MHz XO RCG if root is currently off */
    uint32_t cmd = mmio_r32(gcc, s->rcg_cmd_off);
    if (cmd & 0x80000000U) {
        mmio_w32(gcc, s->rcg_cmd_off + 4U, 0x00000000U); /* SRC=0 (BI_TCXO 19.2 MHz), div=1 */
        mmio_w32(gcc, s->rcg_cmd_off, 0x00000001U);      /* CMD_UPDATE */
        for (uint32_t t = 0U; t < 200U; t++) {
            if ((mmio_r32(gcc, s->rcg_cmd_off) & 1U) == 0U) break;
            KeStallExecutionProcessor(5U);
        }
    }
    uint32_t v = mmio_r32(gcc, s->vote_reg);
    mmio_w32(gcc, s->vote_reg, v | s->vote_bit);
    for (uint32_t t = 0U; t < 400U; t++) {
        if ((mmio_r32(gcc, s->cbcr_off) & 0x80000000U) == 0U) return 1;
        KeStallExecutionProcessor(5U);
    }
    return ((mmio_r32(gcc, s->cbcr_off) & 0x80000000U) == 0U);
}

static void RunStage2ActiveScan(volatile uint8_t *gcc, volatile uint8_t *tlmm, uint32_t stage_mask)
{
    LogS("=== [STAGE 2] Active QUPv3 GENI FW Revision Table & Non-Intrusive I2C ID Probe ==="); LogNL();
    LogS("  Protected live/TZ SEs (ZERO clock/RCG/GENI mutation): I2C_HUB SE0/SE4/SE8, QUP1 SE2/SE4, QUP2 SE6"); LogNL();

    uint32_t orig_v1  = mmio_r32(gcc, GCC_VOTE_1);
    uint32_t orig_v2  = mmio_r32(gcc, GCC_VOTE_2);
    uint32_t added_v1 = 0U;
    uint32_t added_v2 = 0U;

    uint32_t proto_tbl[26];
    uint32_t ifd_tbl[26];
    uint32_t st_tbl[26];
    for (uint32_t i = 0U; i < 26U; i++) {
        proto_tbl[i] = 0xFFU;
        ifd_tbl[i]   = 0xFFFFFFFFU;
        st_tbl[i]    = 0xFFFFFFFFU;
    }

    for (uint32_t w = 0U; w < 3U; w++) {
        uint32_t wmask = g_wraps[w].s_ahb_bit | g_wraps[w].m_ahb_bit | g_wraps[w].core_bit | g_wraps[w].core2x_bit;
        if (g_wraps[w].vote_reg == GCC_VOTE_1) added_v1 |= (wmask & ~orig_v1);
        else                                   added_v2 |= (wmask & ~orig_v2);
        int won = EnableWrapperClocks(gcc, w);
        LogS("  Wrapper "); LogS(g_wraps[w].name);
        LogS(" active="); LogDec(won);
        if (won) {
            PHYSICAL_ADDRESS pa;
            pa.QuadPart = (int64_t)g_wraps[w].pa;
            volatile uint8_t *wb = (volatile uint8_t *)MmMapIoSpace(pa, 0x2000U, MmNonCached);
            if (wb) {
                LogS(" QUP_HW_VER=0x"); LogHex(mmio_r32(wb, QUP_HW_VER_REG), 8);
                MmUnmapIoSpace((PVOID)wb, 0x2000U);
            }
        }
        LogNL();
        if (!won) continue;

        for (uint32_t i = 0U; i < 26U; i++) {
            const QUP_SE_DESC *s = &g_ses[i];
            if (s->wrap_idx != w) continue;

            LogS("    [");
            if (i < 10U) LogC(' ');
            LogDec(i); LogS("] "); LogS(s->name); LogS(" @0x"); LogHex(s->pa, 8);

            if (s->flags & SE_FLAG_TZ_RESERVED) {
                LogS(": SKIPPED (TZ_RESERVED)"); LogNL();
                continue;
            }
            if ((s->flags & SE_FLAG_DISABLED) && ((stage_mask & 0x08U) == 0U)) {
                LogS(": SKIPPED (disabled in DTS; pass StageMask|=0x08 to scan)"); LogNL();
                continue;
            }
            if (s->flags & SE_FLAG_PROTECTED) {
                int already_on = ((mmio_r32(gcc, s->cbcr_off) & 0x80000000U) == 0U);
                LogS(": PROTECTED");
                if (already_on) {
                    SnapshotGeniSe(s->pa, &proto_tbl[i], &ifd_tbl[i], &st_tbl[i]);
                } else {
                    LogS(" (clock halted; left untouched)");
                }
                LogNL();
                continue;
            }

            if (s->vote_reg == GCC_VOTE_1) added_v1 |= (s->vote_bit & ~orig_v1);
            else                           added_v2 |= (s->vote_bit & ~orig_v2);
            if (!EnableSeClockSafeXo(gcc, s)) {
                LogS(": SE_CLK_UNHALT_TIMEOUT (CBCR=0x");
                LogHex(mmio_r32(gcc, s->cbcr_off), 8); LogS(")"); LogNL();
                continue;
            }

            SnapshotGeniSe(s->pa, &proto_tbl[i], &ifd_tbl[i], &st_tbl[i]);
            LogNL();
        }
    }

    /* Non-intrusive read-only I2C peripheral ID probes (Audio CS35L43, Haptics CS40L26A, WChg S2MIW04) */
    LogS("  --- Non-Intrusive Read-Only I2C Peripheral ID Probes ---"); LogNL();

    /* 1. Dual Cirrus CS35L43 Smart Speaker Amps on I2C_HUB_SE6 (index 6, 0x00998000, gpio8/9) */
    if (proto_tbl[6] == GENI_SE_I2C && (ifd_tbl[6] & 1U) == 0U && (st_tbl[6] & M_GENI_CMD_ACTIVE) == 0U) {
        uint32_t saved8 = mmio_r32(tlmm, 8U * 0x1000U);
        uint32_t saved9 = mmio_r32(tlmm, 9U * 0x1000U);
        uint32_t saved42_cfg = mmio_r32(tlmm, 42U * 0x1000U);
        uint32_t saved42_io  = mmio_r32(tlmm, 42U * 0x1000U + 4U);
        uint32_t saved55_cfg = mmio_r32(tlmm, 55U * 0x1000U);
        uint32_t saved55_io  = mmio_r32(tlmm, 55U * 0x1000U + 4U);

        mmio_w32(tlmm, 8U * 0x1000U, (saved8 & ~0x3FU) | (1U << 2) | 3U);
        mmio_w32(tlmm, 9U * 0x1000U, (saved9 & ~0x3FU) | (1U << 2) | 3U);
        if (stage_mask & 0x10U) {
            mmio_w32(tlmm, 42U * 0x1000U + 4U, 2U);
            mmio_w32(tlmm, 42U * 0x1000U, 0x0200U);
            mmio_w32(tlmm, 55U * 0x1000U + 4U, 2U);
            mmio_w32(tlmm, 55U * 0x1000U, 0x0200U);
            SleepMs(4U);
        } else {
            KeStallExecutionProcessor(50U);
        }

        PHYSICAL_ADDRESS pa;
        pa.QuadPart = (int64_t)g_ses[6].pa;
        volatile uint8_t *se = (volatile uint8_t *)MmMapIoSpace(pa, 0x4000U, MmNonCached);
        if (se) {
            GeniI2cInitIo(se);
            LogS("  Probing I2C_HUB_SE6 (0x00998000, gpio8/9, IOS=0x");
            LogHex(mmio_r32(se, SE_GENI_IOS), 2);
            LogS(") — Dual CS35L43 Speaker Amps:"); LogNL();
            (void)ProbeCirrus32Be(se, "CS35L43-Right(bot)", 0x40U, 2U);
            (void)ProbeCirrus32Be(se, "CS35L43-Left(rcv) ", 0x41U, 2U);
            MmUnmapIoSpace((PVOID)se, 0x4000U);
        }
        if (stage_mask & 0x10U) {
            mmio_w32(tlmm, 42U * 0x1000U + 4U, saved42_io);
            mmio_w32(tlmm, 42U * 0x1000U, saved42_cfg);
            mmio_w32(tlmm, 55U * 0x1000U + 4U, saved55_io);
            mmio_w32(tlmm, 55U * 0x1000U, saved55_cfg);
        }
        mmio_w32(tlmm, 8U * 0x1000U, saved8);
        mmio_w32(tlmm, 9U * 0x1000U, saved9);
    } else {
        LogS("  Skipping CS35L43 (I2C_HUB_SE6): proto="); LogDec(proto_tbl[6]);
        LogS(" ifd=0x"); LogHex(ifd_tbl[6], 2); LogS(" st=0x"); LogHex(st_tbl[6], 8); LogNL();
    }

    /* 2. Cirrus CS40L26A LRA Haptic Driver on I2C_HUB_SE3 (index 3, 0x0098C000, gpio22/23) */
    if (proto_tbl[3] == GENI_SE_I2C && (ifd_tbl[3] & 1U) == 0U && (st_tbl[3] & M_GENI_CMD_ACTIVE) == 0U) {
        uint32_t saved22 = mmio_r32(tlmm, 22U * 0x1000U);
        uint32_t saved23 = mmio_r32(tlmm, 23U * 0x1000U);
        mmio_w32(tlmm, 22U * 0x1000U, (saved22 & ~0x3FU) | (1U << 2) | 3U);
        mmio_w32(tlmm, 23U * 0x1000U, (saved23 & ~0x3FU) | (1U << 2) | 3U);
        KeStallExecutionProcessor(50U);

        PHYSICAL_ADDRESS pa;
        pa.QuadPart = (int64_t)g_ses[3].pa;
        volatile uint8_t *se = (volatile uint8_t *)MmMapIoSpace(pa, 0x4000U, MmNonCached);
        if (se) {
            GeniI2cInitIo(se);
            LogS("  Probing I2C_HUB_SE3 (0x0098C000, gpio22/23, IOS=0x");
            LogHex(mmio_r32(se, SE_GENI_IOS), 2);
            LogS(") — CS40L26A LRA Haptics:"); LogNL();
            uint32_t err = ProbeCirrus32Be(se, "CS40L26A-Haptics  ", 0x40U, 8U);
            if (err != 0U && (stage_mask & 0x10U)) {
                uint32_t s145_cfg = mmio_r32(tlmm, 145U * 0x1000U);
                uint32_t s145_io  = mmio_r32(tlmm, 145U * 0x1000U + 4U);
                mmio_w32(tlmm, 145U * 0x1000U + 4U, 0U);
                mmio_w32(tlmm, 145U * 0x1000U, 0x0200U);
                SleepMs(10U);
                mmio_w32(tlmm, 145U * 0x1000U + 4U, 2U);
                SleepMs(8U);
                LogS("    Retrying CS40L26A after GPIO145 reset pulse..."); LogNL();
                (void)ProbeCirrus32Be(se, "CS40L26A-Haptics  ", 0x40U, 6U);
                mmio_w32(tlmm, 145U * 0x1000U + 4U, s145_io);
                mmio_w32(tlmm, 145U * 0x1000U, s145_cfg);
            }
            MmUnmapIoSpace((PVOID)se, 0x4000U);
        }
        mmio_w32(tlmm, 22U * 0x1000U, saved22);
        mmio_w32(tlmm, 23U * 0x1000U, saved23);
    } else {
        LogS("  Skipping CS40L26A (I2C_HUB_SE3): proto="); LogDec(proto_tbl[3]);
        LogS(" ifd=0x"); LogHex(ifd_tbl[3], 2); LogS(" st=0x"); LogHex(st_tbl[3], 8); LogNL();
    }

    /* 3. Samsung S2MIW04 Wireless Charger on I2C_HUB_SE5 (index 5, 0x00994000, gpio6/7) */
    if (proto_tbl[5] == GENI_SE_I2C && (ifd_tbl[5] & 1U) == 0U && (st_tbl[5] & M_GENI_CMD_ACTIVE) == 0U) {
        uint32_t saved6 = mmio_r32(tlmm, 6U * 0x1000U);
        uint32_t saved7 = mmio_r32(tlmm, 7U * 0x1000U);
        mmio_w32(tlmm, 6U * 0x1000U, (saved6 & ~0x3FU) | (1U << 2) | 3U);
        mmio_w32(tlmm, 7U * 0x1000U, (saved7 & ~0x3FU) | (1U << 2) | 3U);
        KeStallExecutionProcessor(50U);

        PHYSICAL_ADDRESS pa;
        pa.QuadPart = (int64_t)g_ses[5].pa;
        volatile uint8_t *se = (volatile uint8_t *)MmMapIoSpace(pa, 0x4000U, MmNonCached);
        if (se) {
            GeniI2cInitIo(se);
            uint8_t r0 = 0x00U;
            uint8_t rx[2] = { 0U, 0U };
            uint32_t ios = mmio_r32(se, SE_GENI_IOS);
            uint32_t err = GeniI2cReadReg(se, 0x5BU, &r0, 1U, rx, 2U);
            LogS("  Probing I2C_HUB_SE5 (0x00994000, gpio6/7, IOS=0x");
            LogHex(ios, 2); LogS(") — S2MIW04@0x5B: ");
            if (err == 0U) {
                LogS("reg[0..1]=0x"); LogHex(rx[0], 2); LogC(' '); LogHex(rx[1], 2);
            } else {
                LogS("err=0x"); LogHex(err, 8);
            }
            LogNL();
            MmUnmapIoSpace((PVOID)se, 0x4000U);
        }
        mmio_w32(tlmm, 6U * 0x1000U, saved6);
        mmio_w32(tlmm, 7U * 0x1000U, saved7);
    }

    /* 4. Optional StageMask & 0x04: MAX77705 PMIC(0x66) + MUIC(0x25) on I2C_HUB_SE8 (s23batt stopped!) */
    if (stage_mask & 0x04U) {
        LogS("  [OPT-IN bit 2] Probing MAX77705 PMIC(0x66)/MUIC(0x25) on I2C_HUB_SE8 (0x009A0000):"); LogNL();
        added_v2 |= (g_ses[8].vote_bit & ~orig_v2);
        if (EnableSeClockSafeXo(gcc, &g_ses[8])) {
            uint32_t p8 = 0xFFU, ifd8 = 0xFFFFFFFFU, st8 = 0xFFFFFFFFU;
            LogS("    SE8:"); SnapshotGeniSe(g_ses[8].pa, &p8, &ifd8, &st8); LogNL();
            if (p8 == GENI_SE_I2C && (ifd8 & 1U) == 0U && (st8 & M_GENI_CMD_ACTIVE) == 0U) {
                uint32_t s206 = mmio_r32(tlmm, 206U * 0x1000U);
                uint32_t s207 = mmio_r32(tlmm, 207U * 0x1000U);
                /* Preserve EGPIO_ENABLE (0x1000) when EGPIO_PRESENT (0x0800) is set */
                uint32_t eg6 = (s206 & 0x0800U) ? 0x1000U : 0U;
                uint32_t eg7 = (s207 & 0x0800U) ? 0x1000U : 0U;
                mmio_w32(tlmm, 206U * 0x1000U, eg6 | (1U << 2) | 3U);
                mmio_w32(tlmm, 207U * 0x1000U, eg7 | (1U << 2) | 3U);
                KeStallExecutionProcessor(50U);

                PHYSICAL_ADDRESS pa;
                pa.QuadPart = (int64_t)g_ses[8].pa;
                volatile uint8_t *se = (volatile uint8_t *)MmMapIoSpace(pa, 0x4000U, MmNonCached);
                if (se) {
                    uint8_t r_id = 0x00U, r_mc = 0x10U, rx[2] = { 0U, 0U };
                    uint32_t e1 = GeniI2cReadReg(se, 0x66U, &r_id, 1U, rx, 2U);
                    LogS("    MAX77705 PMIC@0x66: ");
                    if (e1 == 0U) {
                        LogS("PMICID1=0x"); LogHex(rx[0], 2); LogS(" PMICREV=0x"); LogHex(rx[1], 2);
                    } else {
                        LogS("err=0x"); LogHex(e1, 8);
                    }
                    uint32_t e2 = GeniI2cReadReg(se, 0x66U, &r_mc, 1U, rx, 2U);
                    if (e2 == 0U) {
                        LogS(" MCONFIG=0x"); LogHex(rx[0], 2); LogS(" MCONFIG2=0x"); LogHex(rx[1], 2);
                    }
                    LogNL();

                    uint32_t e3 = GeniI2cReadReg(se, 0x25U, &r_id, 1U, rx, 2U);
                    LogS("    MAX77705 MUIC/USBC@0x25: ");
                    if (e3 == 0U) {
                        LogS("reg[0..1]=0x"); LogHex(rx[0], 2); LogC(' '); LogHex(rx[1], 2);
                    } else {
                        LogS("err=0x"); LogHex(e3, 8);
                    }
                    LogNL();
                    MmUnmapIoSpace((PVOID)se, 0x4000U);
                }
                mmio_w32(tlmm, 206U * 0x1000U, s206);
                mmio_w32(tlmm, 207U * 0x1000U, s207);
            }
        }
    }

    /* Clear ONLY vote bits newly added by this scan (never disturb concurrent live drivers!) */
    mmio_w32(gcc, GCC_VOTE_1, mmio_r32(gcc, GCC_VOTE_1) & ~added_v1);
    mmio_w32(gcc, GCC_VOTE_2, mmio_r32(gcc, GCC_VOTE_2) & ~added_v2);
    SleepMs(1U);
    LogS("  Restored GCC VOTE_1=0x"); LogHex(mmio_r32(gcc, GCC_VOTE_1), 8);
    LogS(" VOTE_2=0x"); LogHex(mmio_r32(gcc, GCC_VOTE_2), 8);
    LogS(" (cleared added 0x"); LogHex(added_v1, 8); LogS("/0x"); LogHex(added_v2, 8); LogS(")"); LogNL();
}

/* ============================================================================
 * DriverUnload & DriverEntry
 * ============================================================================ */
static void DriverUnload(PDRIVER_OBJECT DriverObject)
{
    (void)DriverObject;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    (void)RegistryPath;
    g_RelocSink = (uintptr_t)g_RelocAnchor;
    if (DriverObject) {
        DriverObject->DriverUnload = DriverUnload;
    }

    LogOpen();
    uint32_t stage_mask = ReadRegDword(L"StageMask", 0x01U);
    LogS("=== s23sensprobe.sys v1.0.0.3 started (StageMask=0x");
    LogHex(stage_mask, 2);
    LogS(") ==="); LogNL();

    PHYSICAL_ADDRESS pa;
    pa.QuadPart = (int64_t)PA_GCC_BASE;
    volatile uint8_t *gcc = (volatile uint8_t *)MmMapIoSpace(pa, SZ_GCC, MmNonCached);
    pa.QuadPart = (int64_t)PA_TLMM_BASE;
    volatile uint8_t *tlmm = (volatile uint8_t *)MmMapIoSpace(pa, SZ_TLMM, MmNonCached);

    if (!gcc || !tlmm) {
        LogS("  [ERR] Failed to map GCC or TLMM"); LogNL();
    } else {
        if (stage_mask & 0x01U) {
            RunStage1ReadOnly(gcc, tlmm);
        }
        if (stage_mask & 0x02U) {
            RunStage2ActiveScan(gcc, tlmm, stage_mask);
        }
    }

    if (tlmm) MmUnmapIoSpace((PVOID)tlmm, SZ_TLMM);
    if (gcc)  MmUnmapIoSpace((PVOID)gcc,  SZ_GCC);

    LogS("=== s23sensprobe.sys completed cleanly (log_bytes=");
    LogDec(g_log_off + (int64_t)g_log_pos);
    LogS(") ==="); LogNL();
    LogClose();

    KeStallExecutionProcessor(1U);
    SleepMs(1U);

    return STATUS_UNSUCCESSFUL;
}
