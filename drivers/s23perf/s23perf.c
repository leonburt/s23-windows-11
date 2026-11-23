/*
 * s23perf.c - Samsung Galaxy S23 (dm1q / SM8550-AC) EPSS CPUfreq & TSENS v2 41-Sensor Driver v1.0.0.1
 *
 * Pure-C ARM64 WDM Demand-Start (Start = 3) driver with pre-allocated 64 KB write-through log:
 *   C:\Users\Public\s23perf.log
 *
 * Hardware map verified against S23 live DTS (/home/mahan/mu_s23/s23_live.dts) & S23 kernel:
 *   - qcom,cpufreq-epss (qcom-cpufreq-hw.c epss_soc_data):
 *       Domain 0 (Silver, CPUs 0..2): 0x17D91000 (0x1000)
 *       Domain 1 (Gold,   CPUs 3..6): 0x17D92000 (0x1000)
 *       Domain 2 (Prime,  CPU 7, up to 3.36 GHz SM8550-AC): 0x17D93000 (0x1000)
 *       Offsets (EPSS, NOT legacy OSM 0x110/0x920!):
 *         REG_ENABLE       = 0x000
 *         REG_DOMAIN_STATE = 0x020  (active HW/LMh L_VAL in bits [7:0])
 *         REG_FREQ_LUT     = 0x100  (4-byte rows, up to 40 entries; [31:30]=src, [18:16]=cores, [7:0]=lval)
 *         REG_VOLT_LUT     = 0x200  (4-byte rows, up to 40 entries; [11:0]=mV)
 *         REG_INTR_CLEAR   = 0x308
 *         REG_INTR_STATUS  = 0x30C  (bit 2 = LMh throttle IRQ)
 *         REG_PERF_STATE   = 0x320  (SW vote LUT index)
 *         REG_CYCLE_CNTR   = 0x3C4  (accumulative per-core counters at 0x3C4 + (core+1)*4)
 *   - qcom,tsens-v2 (tsens-v2.c + tsens.c):
 *       tsens0: TM = 0x0C271000, SROT = 0x0C222000 (16 sensors)
 *       tsens1: TM = 0x0C272000, SROT = 0x0C223000 (16 sensors)
 *       tsens2: TM = 0x0C273000, SROT = 0x0C224000 ( 9 sensors) -> 41 sensors total
 *       SROT: HW_VER (+0x00), CTRL (+0x04 bit 0 = TSENS_EN)
 *       TM:   Sn_STATUS (+0xA0 + 4*n: [11:0]=signed 12-bit deciCelsius, bit 21=VALID, [20:16]=flags),
 *             TRDY (+0xE4 bit 0)
 *
 * Registry Parameters (HKLM\SYSTEM\CurrentControlSet\Services\s23perf\Parameters):
 *   Mode       (REG_DWORD, default 0):
 *     0 = Strictly Read-Only telemetry dump (EPSS LUTs + votes + cycle deltas + 41 TSENS sensors)
 *     1 = Opt-In thermal-guarded EPSS performance vote (clamped strictly to valid LUT indices;
 *         immediately throttles all domains to index 0 if ANY valid TSENS sensor >= 75.0 C)
 *   TargetIdx0 / TargetIdx1 / TargetIdx2 (REG_DWORD, optional, used only in Mode=1;
 *     default 0xFFFFFFFF -> safe 75% non-boost LUT index; strictly clamped to [0, lut_count-1])
 *   ThrottleTempC (REG_DWORD, optional, default 75, clamped to [50, 75] C)
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
 * EPSS & TSENS v2 Constants
 * ============================================================================ */
#define EPSS_REG_ENABLE        0x000U
#define EPSS_REG_DOMAIN_STATE  0x020U
#define EPSS_REG_FREQ_LUT      0x100U
#define EPSS_REG_VOLT_LUT      0x200U
#define EPSS_REG_INTR_CLEAR    0x308U
#define EPSS_REG_INTR_STATUS   0x30CU
#define EPSS_REG_PERF_STATE    0x320U
#define EPSS_REG_CYCLE_CNTR    0x3C4U
#define EPSS_LUT_ROW_SIZE      4U
#define EPSS_LUT_MAX_ENTRIES   40U

#define XO_KHZ                 19200U   /* 19.2 MHz XO */
#define ALT_CLK_KHZ            300000U  /* GPLL0 600 MHz / CLK_HW_DIV(2) */

#define TSENS_SROT_HW_VER      0x000U
#define TSENS_SROT_CTRL        0x004U
#define TSENS_TM_SN_STATUS     0x0A0U
#define TSENS_TM_TRDY          0x0E4U
#define TSENS_STATUS_VALID     (1U << 21)

#define MAX_THERMAL_GUARD_DC   750      /* 75.0 C hard ceiling in deciCelsius */

typedef struct {
    const char *name;
    uint64_t    pa;
    uint32_t    core_count;
    uint32_t    first_cpu;
} EPSS_DOMAIN_DESC;

typedef struct {
    const char *name;
    uint64_t    tm_pa;
    uint64_t    srot_pa;
    uint32_t    num_sensors;
} TSENS_BANK_DESC;

static const EPSS_DOMAIN_DESC g_epss_domains[3] = {
    { "Domain0-Silver(CPU0..2)", 0x17D91000ULL, 3U, 0U },
    { "Domain1-Gold(CPU3..6)",   0x17D92000ULL, 4U, 3U },
    { "Domain2-Prime(CPU7)",     0x17D93000ULL, 1U, 7U },
};

static const TSENS_BANK_DESC g_tsens_banks[3] = {
    { "tsens0", 0x0C271000ULL, 0x0C222000ULL, 16U },
    { "tsens1", 0x0C272000ULL, 0x0C223000ULL, 16U },
    { "tsens2", 0x0C273000ULL, 0x0C224000ULL,  9U },
};

typedef struct {
    uint32_t freq_khz;
    uint32_t volt_mv;
    uint8_t  src;
    uint8_t  lval;
    uint8_t  cores;
} EPSS_LUT_ENTRY;

typedef struct {
    volatile uint8_t *base;
    uint32_t          enabled;
    uint32_t          lut_count;
    uint32_t          last_non_boost_idx;
    EPSS_LUT_ENTRY    lut[EPSS_LUT_MAX_ENTRIES];
} EPSS_DOMAIN_STATE;

typedef struct {
    volatile uint8_t *tm;
    volatile uint8_t *srot;
    uint32_t          hw_ver;
    uint32_t          ctrl;
    uint32_t          trdy;
} TSENS_BANK_STATE;

static EPSS_DOMAIN_STATE g_dom[3];
static TSENS_BANK_STATE  g_tsens[3];

/* ============================================================================
 * Helpers: MMIO, Barriers, Timing, Logging
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

static inline void mmio_w32(volatile uint8_t *base, uint32_t off, uint32_t v) {
    dmb_sy();
    *(volatile uint32_t *)(base + off) = v;
    dmb_sy();
}

static void SleepMs(uint32_t ms) {
    if (ms == 0U) return;
    LARGE_INTEGER li;
    li.QuadPart = -((int64_t)ms * 10000LL);
    KeDelayExecutionThread(0, 0, &li);
}

static HANDLE   g_log_handle = NULL;
static int64_t  g_log_off    = 0;
static char     g_log_buf[4096];
static uint32_t g_log_pos    = 0;

static void LogOpen(void) {
    UNICODE_STRING u;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    RtlInitUnicodeString(&u, L"\\??\\C:\\Users\\Public\\s23perf.log");
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

static void LogClose(void) {
    LogFlush();
    if (g_log_handle) {
        ZwClose(g_log_handle);
        g_log_handle = NULL;
    }
}

static void LogC(char c) {
    if (g_log_pos >= sizeof(g_log_buf) - 16U) LogFlush();
    if (g_log_pos < sizeof(g_log_buf)) g_log_buf[g_log_pos++] = c;
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
    if (v < 0) { LogC('-'); /* magnitude taken as unsigned below: -INT64_MIN is undefined behaviour */ }
    char tmp[24];
    int n = 0;
    uint64_t uv = (v < 0) ? (0ULL - (uint64_t)v) : (uint64_t)v;
    if (uv == 0U) tmp[n++] = '0';
    while (uv > 0U) { tmp[n++] = (char)('0' + (uv % 10U)); uv /= 10U; }
    while (n > 0) LogC(tmp[--n]);
}

static void LogDeciC(int32_t dC) {
    if (dC < 0) {
        LogC('-');
        dC = -dC;
    }
    LogDec(dC / 10);
    LogC('.');
    LogDec(dC % 10);
}

static void LogNL(void) {
    LogC('\r');
    LogC('\n');
    LogFlush();
}

/* ============================================================================
 * Registry Parameter Reader
 * ============================================================================ */
static uint32_t ReadRegDword(const WCHAR *val_name, uint32_t def_val) {
    UNICODE_STRING uk, uv;
    OBJECT_ATTRIBUTES oa;
    HANDLE hk = NULL;
    uint32_t val = def_val;

    RtlInitUnicodeString(&uk, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\s23perf\\Parameters");
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
 * TSENS v2 41-Sensor Reader (SROT + TM)
 * ============================================================================ */
static int SampleAllTsens(int verbose, int32_t *max_dC_out, uint32_t *valid_cnt_out, uint32_t *hot_bank_out, uint32_t *hot_sid_out) {
    int32_t  max_dC = -9999;
    int32_t  min_dC =  9999;
    uint32_t valid_cnt = 0U;
    uint32_t hot_b = 0U, hot_s = 0U;

    if (verbose) {
        LogS("=== TSENS v2 41-Sensor Die Thermal Snapshot ==="); LogNL();
    }

    for (uint32_t b = 0U; b < 3U; b++) {
        if (!g_tsens[b].tm || !g_tsens[b].srot) {
            if (verbose) {
                LogS("  "); LogS(g_tsens_banks[b].name); LogS(": UNMAPPED"); LogNL();
            }
            continue;
        }
        g_tsens[b].hw_ver = mmio_r32(g_tsens[b].srot, TSENS_SROT_HW_VER);
        g_tsens[b].ctrl   = mmio_r32(g_tsens[b].srot, TSENS_SROT_CTRL);
        g_tsens[b].trdy   = mmio_r32(g_tsens[b].tm,   TSENS_TM_TRDY);

        if (verbose) {
            LogS("  "); LogS(g_tsens_banks[b].name);
            LogS(" (TM=0x"); LogHex(g_tsens_banks[b].tm_pa, 8);
            LogS(" SROT=0x"); LogHex(g_tsens_banks[b].srot_pa, 8);
            LogS("): VER=0x"); LogHex(g_tsens[b].hw_ver, 8);
            LogS(" CTRL=0x"); LogHex(g_tsens[b].ctrl, 8);
            LogS(" TRDY=0x"); LogHex(g_tsens[b].trdy, 8);
            LogNL();
        }

        if (g_tsens[b].ctrl == 0xFFFFFFFFU || (g_tsens[b].ctrl & 1U) == 0U) {
            if (verbose) {
                LogS("    [WARN] TSENS_EN==0 or unreadable; skipping bank."); LogNL();
            }
            continue;
        }

        for (uint32_t s = 0U; s < g_tsens_banks[b].num_sensors; s++) {
            uint32_t raw = 0U;
            int valid = 0;
            for (uint32_t tries = 0U; tries < 25U; tries++) {
                raw = mmio_r32(g_tsens[b].tm, TSENS_TM_SN_STATUS + 4U * s);
                if (raw != 0xFFFFFFFFU && (raw & TSENS_STATUS_VALID) != 0U) {
                    valid = 1;
                    break;
                }
                KeStallExecutionProcessor(1U);
            }
            if (!valid) {
                if (verbose) {
                    LogS("    S"); LogDec(s); LogS(": INVALID (raw=0x"); LogHex(raw, 8); LogS(")"); LogNL();
                }
                continue;
            }
            /* 12-bit signed two's complement deciCelsius (tsens.c: sign_extend32(temp, 11) * 100 mC) */
            uint32_t u12 = raw & 0xFFFU;
            int32_t dC = (u12 & 0x800U) ? (int32_t)(u12 | 0xFFFFF000U) : (int32_t)u12;
            uint32_t flags = (raw >> 16) & 0x1FU;

            /* Sanity filter: valid silicon range -40.0 C .. +150.0 C */
            if (dC >= -400 && dC <= 1500) {
                valid_cnt++;
                if (dC > max_dC) {
                    max_dC = dC;
                    hot_b = b;
                    hot_s = s;
                }
                if (dC < min_dC) {
                    min_dC = dC;
                }
            }
            if (verbose) {
                LogS("    "); LogS(g_tsens_banks[b].name); LogS("[");
                if (s < 10U) LogC(' ');
                LogDec(s); LogS("]: "); LogDeciC(dC); LogS(" C");
                if (flags) {
                    LogS(" flags=0x"); LogHex(flags, 2);
                }
                LogS(" (raw=0x"); LogHex(raw, 8); LogS(")"); LogNL();
            }
        }
    }

    if (max_dC_out)    *max_dC_out    = max_dC;
    if (valid_cnt_out) *valid_cnt_out = valid_cnt;
    if (hot_bank_out)  *hot_bank_out  = hot_b;
    if (hot_sid_out)   *hot_sid_out   = hot_s;

    if (verbose) {
        LogS("  TSENS Summary: valid="); LogDec(valid_cnt); LogS("/41");
        if (valid_cnt > 0U) {
            LogS(" min="); LogDeciC(min_dC); LogS(" C");
            LogS(" max="); LogDeciC(max_dC); LogS(" C (");
            LogS(g_tsens_banks[hot_b].name); LogS("["); LogDec(hot_s); LogS("])");
        }
        LogNL();
    }
    return (valid_cnt > 0U);
}

/* ============================================================================
 * EPSS LUT Decoding & Telemetry (qcom,cpufreq-epss)
 * ============================================================================ */
static void ReadEpssDomain(uint32_t d, int dump_lut) {
    EPSS_DOMAIN_STATE *st = &g_dom[d];
    st->enabled = 0U;
    st->lut_count = 0U;
    st->last_non_boost_idx = 0U;

    if (!st->base) {
        LogS("  "); LogS(g_epss_domains[d].name); LogS(": UNMAPPED"); LogNL();
        return;
    }

    uint32_t en   = mmio_r32(st->base, EPSS_REG_ENABLE);
    uint32_t dst  = mmio_r32(st->base, EPSS_REG_DOMAIN_STATE);
    uint32_t perf = mmio_r32(st->base, EPSS_REG_PERF_STATE);
    uint32_t ist  = mmio_r32(st->base, EPSS_REG_INTR_STATUS);
    st->enabled = (en != 0xFFFFFFFFU) && ((en & 1U) != 0U);

    LogS("--- "); LogS(g_epss_domains[d].name);
    LogS(" @ 0x"); LogHex(g_epss_domains[d].pa, 8); LogS(" ---"); LogNL();
    LogS("  ENABLE(0x000)=0x"); LogHex(en, 8);
    LogS(" DOMAIN_STATE(0x020)=0x"); LogHex(dst, 8);
    LogS(" (HW="); LogDec(((uint64_t)(dst & 0xFFU) * XO_KHZ) / 1000U); LogS(" MHz)");
    LogS(" PERF_STATE(0x320)="); LogDec(perf);
    LogS(" INTR_ST(0x30C)=0x"); LogHex(ist, 8); LogNL();

    if (!st->enabled) {
        LogS("  [WARN] EPSS domain disabled or unreadable."); LogNL();
        return;
    }

    uint32_t prev_freq = 0U;
    uint8_t  max_cc = 0U;
    for (uint32_t i = 0U; i < EPSS_LUT_MAX_ENTRIES; i++) {
        uint32_t fraw = mmio_r32(st->base, EPSS_REG_FREQ_LUT + i * EPSS_LUT_ROW_SIZE);
        uint32_t vraw = mmio_r32(st->base, EPSS_REG_VOLT_LUT + i * EPSS_LUT_ROW_SIZE);
        uint8_t  src  = (uint8_t)((fraw >> 30) & 3U);
        uint8_t  cc   = (uint8_t)((fraw >> 16) & 7U);
        uint8_t  lval = (uint8_t)(fraw & 0xFFU);
        uint32_t mv   = vraw & 0xFFFU;
        uint32_t freq_khz = src ? ((uint32_t)lval * XO_KHZ) : ALT_CLK_KHZ;

        if (i == 0U) max_cc = cc;
        /* End-of-table rule from qcom-cpufreq-hw.c L308: two identical consecutive freqs */
        if (i > 0U && freq_khz == prev_freq) {
            break;
        }
        st->lut[i].freq_khz = freq_khz;
        st->lut[i].volt_mv  = mv;
        st->lut[i].src      = src;
        st->lut[i].lval     = lval;
        st->lut[i].cores    = cc;
        st->lut_count       = i + 1U;
        if (cc >= max_cc) {
            st->last_non_boost_idx = i;
        }
        prev_freq = freq_khz;

        if (dump_lut) {
            LogS("    LUT[");
            if (i < 10U) LogC(' ');
            LogDec(i);
            LogS("]: "); LogDec(freq_khz / 1000U); LogS(".");
            LogDec((freq_khz % 1000U) / 100U); LogS(" MHz (src=");
            LogDec(src); LogS(" L="); LogDec(lval);
            LogS(") "); LogDec(mv); LogS(" mV cores="); LogDec(cc);
            if (i == perf) LogS("  <-- CUR_VOTE");
            LogNL();
        }
    }

    LogS("  Decoded LUT entries="); LogDec(st->lut_count);
    if (st->lut_count > 0U) {
        LogS(" range="); LogDec(st->lut[0].freq_khz / 1000U);
        LogS(".."); LogDec(st->lut[st->lut_count - 1U].freq_khz / 1000U); LogS(" MHz");
        LogS(" (non-boost max idx="); LogDec(st->last_non_boost_idx); LogS(")");
    }
    LogNL();
}

static void MeasureCycleCounters(uint32_t window_ms) {
    uint32_t c0[3][5];
    uint32_t c1[3][5];

    for (uint32_t d = 0U; d < 3U; d++) {
        for (uint32_t k = 0U; k < 5U; k++) {
            c0[d][k] = 0U;
            c1[d][k] = 0U;
        }
        if (!g_dom[d].base || !g_dom[d].enabled) continue;
        uint32_t n = g_epss_domains[d].core_count;
        for (uint32_t k = 0U; k <= n; k++) {
            c0[d][k] = mmio_r32(g_dom[d].base, EPSS_REG_CYCLE_CNTR + k * 4U);
        }
    }

    SleepMs(window_ms);

    LogS("=== EPSS Cycle Counter Deltas ("); LogDec(window_ms); LogS(" ms window) ==="); LogNL();
    for (uint32_t d = 0U; d < 3U; d++) {
        if (!g_dom[d].base || !g_dom[d].enabled) continue;
        uint32_t n = g_epss_domains[d].core_count;
        LogS("  "); LogS(g_epss_domains[d].name); LogS(":");
        for (uint32_t k = 0U; k <= n; k++) {
            c1[d][k] = mmio_r32(g_dom[d].base, EPSS_REG_CYCLE_CNTR + k * 4U);
            uint32_t delta = c1[d][k] - c0[d][k];
            if (k == 0U) {
                LogS(" base=0x"); LogHex(delta, 8);
            } else {
                LogS(" cpu"); LogDec(g_epss_domains[d].first_cpu + (k - 1U));
                LogS("="); LogDec(delta);
            }
        }
        LogNL();
    }
}

static void ThrottleAllToMin(const char *reason) {
    LogS("  [THERMAL GUARD] Throttling all EPSS domains to LUT[0]! Reason: ");
    LogS(reason); LogNL();
    for (uint32_t d = 0U; d < 3U; d++) {
        if (g_dom[d].base && g_dom[d].enabled && g_dom[d].lut_count > 0U) {
            mmio_w32(g_dom[d].base, EPSS_REG_PERF_STATE, 0U);
            LogS("    "); LogS(g_epss_domains[d].name);
            LogS(" -> PERF_STATE=0 ("); LogDec(g_dom[d].lut[0].freq_khz / 1000U); LogS(" MHz)"); LogNL();
        }
    }
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
    uint32_t mode = ReadRegDword(L"Mode", 0U); /* SAFE DEFAULT: Mode = 0 (100% Read-Only) */
    LogS("=== s23perf.sys v1.0.0.1 started (Mode="); LogDec(mode); LogS(") ==="); LogNL();

    /* Map 3 EPSS domains (0x17D91000..0x17D93000) */
    for (uint32_t d = 0U; d < 3U; d++) {
        PHYSICAL_ADDRESS pa;
        pa.QuadPart = (int64_t)g_epss_domains[d].pa;
        g_dom[d].base = (volatile uint8_t *)MmMapIoSpace(pa, 0x1000U, MmNonCached);
    }

    /* Map 3 TSENS v2 banks (TM 0x0C271000..3000 + SROT 0x0C222000..4000) */
    for (uint32_t b = 0U; b < 3U; b++) {
        PHYSICAL_ADDRESS pa;
        pa.QuadPart = (int64_t)g_tsens_banks[b].tm_pa;
        g_tsens[b].tm = (volatile uint8_t *)MmMapIoSpace(pa, 0x1000U, MmNonCached);
        pa.QuadPart = (int64_t)g_tsens_banks[b].srot_pa;
        g_tsens[b].srot = (volatile uint8_t *)MmMapIoSpace(pa, 0x1000U, MmNonCached);
    }

    /* 1. Read-Only EPSS LUT + current vote decode */
    LogS("=== EPSS CPUfreq Domains (qcom,cpufreq-epss) ==="); LogNL();
    for (uint32_t d = 0U; d < 3U; d++) {
        ReadEpssDomain(d, 1);
    }
    MeasureCycleCounters(50U);

    /* 2. Read-Only 41-sensor TSENS v2 dump */
    int32_t  max_dC = 0;
    uint32_t valid_cnt = 0U, hot_b = 0U, hot_s = 0U;
    int tsens_ok = SampleAllTsens(1, &max_dC, &valid_cnt, &hot_b, &hot_s);

    /* 3. Opt-In Mode = 1: Thermal-Guarded EPSS Performance Vote */
    if (mode == 1U) {
        uint32_t thresh_c = ReadRegDword(L"ThrottleTempC", 75U);
        if (thresh_c < 50U) thresh_c = 50U;
        if (thresh_c > 75U) thresh_c = 75U;
        int32_t thresh_dC = (int32_t)(thresh_c * 10U);

        LogS("=== MODE 1: Thermal-Guarded EPSS Vote (guard >= ");
        LogDeciC(thresh_dC); LogS(" C) ==="); LogNL();

        if (!tsens_ok || valid_cnt == 0U) {
            ThrottleAllToMin("No valid TSENS sensors available (fail-safe)");
        } else if (max_dC >= thresh_dC) {
            LogS("  Pre-vote max temp "); LogDeciC(max_dC); LogS(" C >= guard!"); LogNL();
            ThrottleAllToMin("Pre-vote die temperature >= guard ceiling");
        } else {
            static const WCHAR * const reg_names[3] = { L"TargetIdx0", L"TargetIdx1", L"TargetIdx2" };
            for (uint32_t d = 0U; d < 3U; d++) {
                if (!g_dom[d].base || !g_dom[d].enabled || g_dom[d].lut_count == 0U) continue;
                uint32_t req = ReadRegDword(reg_names[d], 0xFFFFFFFFU);
                uint32_t idx;
                if (req == 0xFFFFFFFFU) {
                    /* Safe default target: ~75% of non-boost LUT ceiling */
                    idx = (g_dom[d].last_non_boost_idx * 3U) / 4U;
                } else {
                    idx = req;
                }
                /* Strictly clamp to hardware LUT bounds [0, lut_count - 1] */
                if (idx >= g_dom[d].lut_count) {
                    idx = g_dom[d].lut_count - 1U;
                }
                mmio_w32(g_dom[d].base, EPSS_REG_PERF_STATE, idx);
                LogS("  Voted "); LogS(g_epss_domains[d].name);
                LogS(" -> LUT["); LogDec(idx); LogS("] (");
                LogDec(g_dom[d].lut[idx].freq_khz / 1000U); LogS(" MHz, ");
                LogDec(g_dom[d].lut[idx].volt_mv); LogS(" mV)"); LogNL();
            }

            /* Thermal dwell monitor (5 x 100 ms): immediately throttle down if >= thresh_dC */
            int tripped = 0;
            for (uint32_t step = 0U; step < 5U; step++) {
                SleepMs(100U);
                int32_t  cur_max = 0;
                uint32_t cur_cnt = 0U, cb = 0U, cs = 0U;
                if (!SampleAllTsens(0, &cur_max, &cur_cnt, &cb, &cs) || cur_cnt == 0U || cur_max >= thresh_dC) {
                    LogS("  [THERMAL TRIP] step="); LogDec(step);
                    LogS(" max="); LogDeciC(cur_max); LogS(" C (");
                    LogS(g_tsens_banks[cb].name); LogS("["); LogDec(cs); LogS("])"); LogNL();
                    ThrottleAllToMin("Post-vote die temperature >= guard ceiling");
                    tripped = 1;
                    break;
                }
            }

            LogS("  Post-vote EPSS state (thermal_tripped="); LogDec(tripped); LogS("):"); LogNL();
            for (uint32_t d = 0U; d < 3U; d++) {
                if (!g_dom[d].base || !g_dom[d].enabled) continue;
                uint32_t dst  = mmio_r32(g_dom[d].base, EPSS_REG_DOMAIN_STATE);
                uint32_t perf = mmio_r32(g_dom[d].base, EPSS_REG_PERF_STATE);
                LogS("    "); LogS(g_epss_domains[d].name);
                LogS(": PERF_STATE="); LogDec(perf);
                LogS(" DOMAIN_STATE=0x"); LogHex(dst, 8);
                LogS(" (HW="); LogDec(((uint64_t)(dst & 0xFFU) * XO_KHZ) / 1000U); LogS(" MHz)"); LogNL();
            }
            MeasureCycleCounters(50U);
            (void)SampleAllTsens(0, &max_dC, &valid_cnt, &hot_b, &hot_s);
            LogS("  Final TSENS max="); LogDeciC(max_dC); LogS(" C (valid="); LogDec(valid_cnt); LogS("/41)"); LogNL();
        }
    }

    LogS("=== s23perf.sys completed cleanly ==="); LogNL();

    for (uint32_t b = 0U; b < 3U; b++) {
        if (g_tsens[b].srot) { MmUnmapIoSpace((PVOID)g_tsens[b].srot, 0x1000U); g_tsens[b].srot = NULL; }
        if (g_tsens[b].tm)   { MmUnmapIoSpace((PVOID)g_tsens[b].tm,   0x1000U); g_tsens[b].tm   = NULL; }
    }
    for (uint32_t d = 0U; d < 3U; d++) {
        if (g_dom[d].base)   { MmUnmapIoSpace((PVOID)g_dom[d].base,   0x1000U); g_dom[d].base   = NULL; }
    }

    LogClose();
    return STATUS_UNSUCCESSFUL;
}
