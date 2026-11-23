/*
 * s23pcieprobe.c - Samsung Galaxy S23 (dm1q / SM8550) Live PCIe0 + QCA6490 Kernel Probe v1.0.0.10
 *
 * Uses FILE_OPEN on a pre-allocated 64 KB C:\Users\Public\s23pcieprobe.log with FILE_WRITE_THROUGH
 * so every LogNL() writes directly to existing physical UFS blocks before the next instruction executes.
 *
 * Granular StageMask bits (HKLM\SYSTEM\CurrentControlSet\Services\s23pcieprobe\Parameters\StageMask):
 *   0x00 : Stage 0 (Read-Only Hardware Discovery: CMD_DB, APPS_RSC, SPMI, AOSS QMP, TLMM, TCSR, GCC)
 *   0x01 : Stage 1 (Pin cold hold, PMK8550 GPIO3 32kHz sleep clk, RPMh rails + cx/mx, AOSS QMP 11 frames, WLAN_EN)
 *   0x02 : Stage 2A (TCSR clkref + GCC GDSCs + AUX/RCHNG RCGs + GCC branch clock votes; NO GPLL0_USER_CTL write)
 *   0x04 : Stage 2B (Map and read-only probe of PARF 0x01C00000 and PHY 0x01C06000)
 *   0x08 : Stage 2C (BCR resets, PARF init, 115-triplet PHY init, PIPE clock enable & PCS_STATUS bit 6 == 0 poll)
 *   0x10 : Stage 2D (Map DBI 0x60000000, read RootId/LinkCap, release PERST#, DBI Gen2/EQ, LTSSM enable & L0 poll)
 *   0x20 : Stage 2E (iATU Slot 0 CFG0 setup & ECAM 0x60100000 QCA6490 0x110317CB readback)
 */

#include <stdint.h>
#include <stddef.h>

typedef int32_t NTSTATUS;
typedef uint16_t WCHAR;
typedef uint32_t ULONG;
typedef uint32_t *PULONG;
typedef uint8_t UCHAR;
typedef uint8_t BOOLEAN;
typedef void *PVOID;
typedef void *HANDLE;
typedef int64_t LONGLONG;

#define STATUS_SUCCESS                ((NTSTATUS)0x00000000L)
#define STATUS_UNSUCCESSFUL           ((NTSTATUS)0xC0000001L)
#define NT_SUCCESS(st)                (((NTSTATUS)(st)) >= 0)

#define OBJ_CASE_INSENSITIVE          0x00000040UL
#define OBJ_KERNEL_HANDLE             0x00000200UL
#define FILE_GENERIC_WRITE            0x00120116UL
#define FILE_SHARE_READ               0x00000001UL
#define FILE_SHARE_WRITE              0x00000002UL
#define FILE_OPEN                     0x00000001UL
#define FILE_OPEN_IF                  0x00000003UL
#define FILE_WRITE_THROUGH            0x00000002UL
#define FILE_SYNCHRONOUS_IO_NONALERT  0x00000020UL
#define FILE_ATTRIBUTE_NORMAL         0x00000080UL
#define KEY_READ                      0x00020019UL
#define KeyValuePartialInformation    2

typedef union _LARGE_INTEGER {
    struct { uint32_t LowPart; int32_t HighPart; };
    int64_t QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER, PHYSICAL_ADDRESS;

typedef struct _UNICODE_STRING {
    uint16_t Length;
    uint16_t MaximumLength;
    WCHAR *Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _OBJECT_ATTRIBUTES {
    uint32_t Length;
    uint32_t Pad0;
    HANDLE RootDirectory;
    PUNICODE_STRING ObjectName;
    uint32_t Attributes;
    uint32_t Pad1;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

typedef struct _IO_STATUS_BLOCK {
    union { NTSTATUS Status; PVOID Pointer; };
    uintptr_t Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

typedef struct _KEY_VALUE_PARTIAL_INFORMATION {
    uint32_t TitleIndex;
    uint32_t Type;
    uint32_t DataLength;
    uint8_t Data[16];
} KEY_VALUE_PARTIAL_INFORMATION;

struct _DRIVER_OBJECT;
typedef void (*PDRIVER_UNLOAD)(struct _DRIVER_OBJECT *DriverObject);

typedef struct _DRIVER_OBJECT {
    int16_t Type;
    int16_t Size;
    uint32_t Pad0;
    PVOID DeviceObject;
    uint32_t Flags;
    uint32_t Pad1;
    PVOID DriverStart;
    uint32_t DriverSize;
    uint32_t Pad2;
    PVOID DriverSection;
    PVOID DriverExtension;
    UNICODE_STRING DriverName;
    PUNICODE_STRING HardwareDatabase;
    PVOID FastIoDispatch;
    PVOID DriverInit;
    PVOID DriverStartIo;
    PDRIVER_UNLOAD DriverUnload;
    PVOID MajorFunction[28];
} DRIVER_OBJECT, *PDRIVER_OBJECT;

__declspec(dllimport) PVOID MmMapIoSpace(PHYSICAL_ADDRESS PhysicalAddress, size_t NumberOfBytes, uint32_t CacheType);
__declspec(dllimport) void MmUnmapIoSpace(PVOID BaseAddress, size_t NumberOfBytes);
__declspec(dllimport) void KeStallExecutionProcessor(uint32_t MicroSeconds);
__declspec(dllimport) NTSTATUS KeDelayExecutionThread(int8_t WaitMode, BOOLEAN Alertable, PLARGE_INTEGER Interval);
__declspec(dllimport) void RtlInitUnicodeString(PUNICODE_STRING DestinationString, const WCHAR *SourceString);
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

static inline void mmio_rmw32(volatile uint8_t *base, uint32_t off, uint32_t clear_bits, uint32_t set_bits) {
    uint32_t v = mmio_r32(base, off);
    mmio_w32(base, off, (v & ~clear_bits) | set_bits);
}

static void SleepMs(uint32_t ms) {
    if (ms == 0) return;
    LARGE_INTEGER li;
    li.QuadPart = -((int64_t)ms * 10000LL);
    KeDelayExecutionThread(0, 0, &li);
}

static void StallUs(uint32_t us) {
    while (us > 1000) {
        KeStallExecutionProcessor(1000);
        us -= 1000;
    }
    if (us > 0) {
        KeStallExecutionProcessor(us);
    }
}

/* ============================================================================
 * Pre-Allocated 64 KB Write-Through Kernel Log (C:\Users\Public\s23pcieprobe.log)
 * ============================================================================ */
static HANDLE g_log_handle = NULL;
static int64_t g_log_off = 0;
static char g_log_buf[4096];
static uint32_t g_log_pos = 0;

static void LogOpen(void) {
    UNICODE_STRING u;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    RtlInitUnicodeString(&u, L"\\??\\C:\\Users\\Public\\s23pcieprobe.log");
    InitObjAttr(&oa, &u, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE);
    g_log_off = 0;
    g_log_pos = 0;
    /* Use FILE_OPEN_IF (without truncating!) so pre-allocated 64KB blocks are overwritten in-place */
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
    if (g_log_off + (int64_t)g_log_pos > 65000) {
        g_log_pos = 0;
        return;
    }
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER off;
    off.QuadPart = g_log_off;
    ZwWriteFile(g_log_handle, NULL, NULL, NULL, &iosb, g_log_buf, g_log_pos, &off, NULL);
    g_log_off += g_log_pos;
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
    if (g_log_pos >= sizeof(g_log_buf) - 16) {
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
        LogC(hex[(v >> i) & 0xF]);
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
        tmp[n++] = (char)('0' + (uv % 10));
        uv /= 10;
    }
    while (n > 0) LogC(tmp[--n]);
}

static void LogNL(void) {
    LogC('\r');
    LogC('\n');
    LogFlush();
}

static void LogStep(const char *msg) {
    LogS("[STEP] ");
    LogS(msg);
    LogNL();
}

/* ============================================================================
 * Registry Parameter Reader (StageMask)
 * ============================================================================ */
static uint32_t ReadStageMask(void) {
    UNICODE_STRING uk, uv;
    OBJECT_ATTRIBUTES oa;
    HANDLE hk = NULL;
    uint32_t val = 0;

    RtlInitUnicodeString(&uk, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\s23pcieprobe\\Parameters");
    InitObjAttr(&oa, &uk, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE);
    if (!NT_SUCCESS(ZwOpenKey(&hk, KEY_READ, &oa))) {
        return 0;
    }
    RtlInitUnicodeString(&uv, L"StageMask");
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
 * CMD_DB Parser (0x81C60000) & APPS_RSC DRV2 TCS0 (0x17A20000)
 * ============================================================================ */
static volatile uint8_t *g_cmddb = NULL;
static volatile uint8_t *g_rsc   = NULL;

static uint32_t CmdDbLookup(const char *name) {
    if (!g_cmddb) return 0;
    if (g_cmddb[4] != 0xDB || g_cmddb[5] != 0x30 || g_cmddb[6] != 0x03 || g_cmddb[7] != 0x0C) {
        return 0;
    }
    uint32_t nlen = 0;
    while (name[nlen] && nlen < 8) nlen++;

    for (uint32_t i = 0; i < 8; i++) {
        const volatile uint8_t *r = g_cmddb + 8 + 16 * i;
        uint16_t slv_id  = *(const volatile uint16_t *)(r + 0);
        uint16_t hdr_off = *(const volatile uint16_t *)(r + 2);
        uint16_t cnt     = *(const volatile uint16_t *)(r + 6);
        if (slv_id == 0) break;
        if ((uint32_t)144U + (uint32_t)hdr_off + (uint32_t)cnt * 24U > 0x20000U) break;
        const volatile uint8_t *e = g_cmddb + 144 + hdr_off;
        for (uint32_t j = 0; j < cnt; j++, e += 24) {
            int match = 1;
            for (uint32_t k = 0; k < 8; k++) {
                uint8_t ch = (k < nlen) ? (uint8_t)name[k] : 0;
                if (e[k] != ch) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                return *(const volatile uint32_t *)(e + 16);
            }
        }
    }
    return 0;
}

#define RSC_TCS_OFF      0xD00U
#define V3_CTRL          0x24U
#define V3_STATUS        0x28U
#define V3_CMDEN         0x2CU
#define V3_MSGID         0x34U
#define V3_ADDR          0x38U
#define V3_DATA          0x3CU
#define V3_CMDST         0x40U
#define AMC_ENABLE       (1U << 16)
#define AMC_TRIGGER      (1U << 24)

static inline uint32_t TcsOff(uint32_t id, uint32_t reg) {
    return RSC_TCS_OFF + 672U * id + reg;
}
static inline uint32_t TcsCmdOff(uint32_t id, uint32_t cmd, uint32_t reg) {
    return TcsOff(id, reg) + 24U * cmd;
}

static int RpmhWriteTcs0(uint32_t addr, uint32_t data) {
    if (!g_rsc || addr == 0) return 0;
    const uint32_t id = 0;

    /* Wait up to 10 ms for TCS0 controller to report idle (V3_STATUS bit 0 == 1) */
    uint32_t idle_us = 0;
    for (; idle_us < 10000U; idle_us++) {
        if (mmio_r32(g_rsc, TcsOff(id, V3_STATUS)) & 1U) break;
        KeStallExecutionProcessor(1);
    }
    if ((mmio_r32(g_rsc, TcsOff(id, V3_STATUS)) & 1U) == 0) {
        LogS("  RPMh[0x"); LogHex(addr, 5); LogS("] ABORT: TCS0 busy!"); LogNL();
        return 0;
    }

    uint32_t sv_cmden = mmio_r32(g_rsc, TcsOff(id, V3_CMDEN));
    uint32_t sv_msg0  = mmio_r32(g_rsc, TcsCmdOff(id, 0, V3_MSGID));
    uint32_t sv_addr0 = mmio_r32(g_rsc, TcsCmdOff(id, 0, V3_ADDR));
    uint32_t sv_data0 = mmio_r32(g_rsc, TcsCmdOff(id, 0, V3_DATA));

    mmio_w32(g_rsc, RSC_TCS_OFF + 0x08, 1U << id);
    mmio_w32(g_rsc, TcsOff(id, V3_CMDEN), 0);
    mmio_w32(g_rsc, TcsCmdOff(id, 0, V3_MSGID), 8U | 0x100U | 0x10000U);
    mmio_w32(g_rsc, TcsCmdOff(id, 0, V3_ADDR), addr);
    mmio_w32(g_rsc, TcsCmdOff(id, 0, V3_DATA), data);
    mmio_w32(g_rsc, TcsOff(id, V3_CMDEN), 1);

    uint32_t en = mmio_r32(g_rsc, TcsOff(id, V3_CTRL));
    en &= ~AMC_TRIGGER; mmio_w32(g_rsc, TcsOff(id, V3_CTRL), en); (void)mmio_r32(g_rsc, TcsOff(id, V3_CTRL));
    en &= ~AMC_ENABLE;  mmio_w32(g_rsc, TcsOff(id, V3_CTRL), en); (void)mmio_r32(g_rsc, TcsOff(id, V3_CTRL));
    en |= AMC_ENABLE;   mmio_w32(g_rsc, TcsOff(id, V3_CTRL), en); (void)mmio_r32(g_rsc, TcsOff(id, V3_CTRL));
    en |= AMC_TRIGGER;  mmio_w32(g_rsc, TcsOff(id, V3_CTRL), en);

    uint32_t st = 0, wait_us = 0;
    for (wait_us = 0; wait_us < 100000; wait_us++) {
        st = mmio_r32(g_rsc, RSC_TCS_OFF + 0x04);
        if (st & (1U << id)) break;
        KeStallExecutionProcessor(1);
    }
    uint32_t cmdst = mmio_r32(g_rsc, TcsCmdOff(id, 0, V3_CMDST));
    int ok = (st & (1U << id)) != 0;

    if (ok && (mmio_r32(g_rsc, TcsOff(id, V3_STATUS)) & 1U)) {
        mmio_w32(g_rsc, TcsOff(id, V3_CMDEN), 0);
        mmio_w32(g_rsc, TcsCmdOff(id, 0, V3_MSGID), sv_msg0);
        mmio_w32(g_rsc, TcsCmdOff(id, 0, V3_ADDR),  sv_addr0);
        mmio_w32(g_rsc, TcsCmdOff(id, 0, V3_DATA),  sv_data0);
        mmio_w32(g_rsc, TcsOff(id, V3_CMDEN), sv_cmden);
    }
    mmio_w32(g_rsc, RSC_TCS_OFF + 0x08, 1U << id);

    LogS("  RPMh[0x"); LogHex(addr, 5); LogS("] <- 0x"); LogHex(data, 4);
    LogS(" : irq=0x"); LogHex(st, 2); LogS(" cmdst=0x"); LogHex(cmdst, 2);
    LogS(" ("); LogDec(wait_us); LogS(" us)"); LogNL();
    return ok;
}

static void VoteVrmRail(const char *name, uint32_t mv, int set_mode, uint32_t mode) {
    uint32_t a = CmdDbLookup(name);
    LogS("Rail "); LogS(name); LogS(" cmd_db=0x"); LogHex(a, 5);
    LogS(" target="); LogDec(mv); LogS(" mV"); LogNL();
    if (a == 0) return;
    RpmhWriteTcs0(a + 0x0, mv);
    if (set_mode) {
        RpmhWriteTcs0(a + 0x8, mode);
    }
    RpmhWriteTcs0(a + 0x4, 1);
}

/* ============================================================================
 * SPMI PMIC Arbiter v7 (0x0C400000 / 0x0C42D000 / 0x0C440000 / 0x0C500000)
 * ============================================================================ */
static volatile uint8_t *g_spmi_core   = NULL;
static volatile uint8_t *g_spmi_cnfg   = NULL;
static volatile uint8_t *g_spmi_obsrvr = NULL;
static volatile uint8_t *g_spmi_chnls  = NULL;
static uint16_t g_spmi_apid[4096];
static uint8_t  g_spmi_owner[4096];

static int SpmiReadByte(uint32_t sid, uint32_t addr16, uint8_t *out_val) {
    if (!g_spmi_obsrvr) return 0;
    uint32_t ppid = ((sid & 0xFU) << 8) | ((addr16 >> 8) & 0xFFU);
    uint32_t apid_plus1 = g_spmi_apid[ppid & 0xFFFU];
    if (apid_plus1 == 0) return 0;
    uint32_t apid = apid_plus1 - 1;
    uint32_t off = 0x20U * apid;
    if (off + 0x20U > 0x80000U) return 0;

    uint32_t cmd = (1U << 27) | ((addr16 & 0xFFU) << 4) | 0U;
    mmio_w32(g_spmi_obsrvr, off + 0x00, cmd);
    uint32_t st = 0;
    for (uint32_t i = 0; i < 1000; i++) {
        st = mmio_r32(g_spmi_obsrvr, off + 0x08);
        if (st & 1U) break;
        KeStallExecutionProcessor(1);
    }
    if (!(st & 1U) || (st & 0xEU)) return 0;
    uint32_t d0 = mmio_r32(g_spmi_obsrvr, off + 0x18);
    *out_val = (uint8_t)(d0 & 0xFFU);
    return 1;
}

static int SpmiWriteByte(uint32_t sid, uint32_t addr16, uint8_t val) {
    if (!g_spmi_chnls) return 0;
    uint32_t ppid = ((sid & 0xFU) << 8) | ((addr16 >> 8) & 0xFFU);
    uint32_t apid_plus1 = g_spmi_apid[ppid & 0xFFFU];
    if (apid_plus1 == 0 || g_spmi_owner[ppid & 0xFFFU] != 0) return 0;
    uint32_t apid = apid_plus1 - 1;
    uint32_t off = 0x1000U * apid;
    if (off + 0x20U > 0x400000U) return 0;

    mmio_w32(g_spmi_chnls, off + 0x10, val);
    mmio_w32(g_spmi_chnls, off + 0x00, (0U << 27) | ((addr16 & 0xFFU) << 4) | 0U);
    uint32_t st = 0;
    for (uint32_t i = 0; i < 1000; i++) {
        st = mmio_r32(g_spmi_chnls, off + 0x08);
        if (st & 1U) break;
        KeStallExecutionProcessor(1);
    }
    return ((st & 1U) != 0) && ((st & 0xEU) == 0);
}

/* ============================================================================
 * AOSS QMP (0x0C300000) + IPCC MPROC (0x00408000)
 * ============================================================================ */
static volatile uint8_t *g_qmp  = NULL;
static volatile uint8_t *g_ipcc = NULL;
static uint32_t g_qmp_off = 0;
static uint32_t g_qmp_sz  = 0;

static const char * const g_s23_pdc_msgs[11] = {
    "{class: wlan_pdc, ss: rf, res: s4e.v, upval: 966}",
    "{class: wlan_pdc, ss: rf, res: s4e.v, dwnval: 615}",
    "{class: wlan_pdc, ss: rf, res: s4g.v, upval: 1350}",
    "{class: wlan_pdc, ss: rf, res: s4g.v, dwnval: 945}",
    "{class: wlan_pdc, ss: rf, res: s6g.v, upval: 1900}",
    "{class: wlan_pdc, ss: rf, res: s6g.v, dwnval: 1825}",
    "{class: wlan_pdc, ss: rf, res: s2g.m, enable: 1}",
    "{class: wlan_pdc, ss: rf, res: s2g.v, enable: 1}",
    "{class: wlan_pdc, ss: rf, res: s2g.v, upval: 1012}",
    "{class: wlan_pdc, ss: rf, res: s2g.v, dwnval: 800}",
    "{class: wlan_pdc, ss: bb, res: pdc, enable: 0}"
};

static void QmpKick(void) {
    if (g_ipcc) {
        mmio_w32(g_ipcc, 0x0C, 0);
    }
}

static int QmpWaitEq(uint32_t off, uint32_t want, uint32_t timeout_ms) {
    for (uint32_t i = 0; i < timeout_ms * 10; i++) {
        if (mmio_r32(g_qmp, off) == want) return 1;
        KeStallExecutionProcessor(100);
    }
    return mmio_r32(g_qmp, off) == want;
}

static int QmpOpenIfNeeded(void) {
    if (!g_qmp || !g_ipcc) return 0;
    uint32_t magic = mmio_r32(g_qmp, 0x00);
    uint32_t ver   = mmio_r32(g_qmp, 0x04);
    g_qmp_sz  = mmio_r32(g_qmp, 0x34);
    g_qmp_off = mmio_r32(g_qmp, 0x38);
    if (magic != 0x4D41494CU || ver != 1U || (g_qmp_off & 3U) != 0 || g_qmp_off < 0x3CU || g_qmp_off + 68U > 0x400U) {
        LogS("  QMP invalid header!"); LogNL();
        return 0;
    }
    if (mmio_r32(g_qmp, 0x24) == 0xFFFFU &&
        mmio_r32(g_qmp, 0x30) == 0xFFFFU &&
        mmio_r32(g_qmp, 0x14) == 0xFFFFU) {
        LogS("  QMP link already UP (offset=0x"); LogHex(g_qmp_off, 3); LogS(")"); LogNL();
        return 1;
    }
    mmio_w32(g_qmp, 0x10, mmio_r32(g_qmp, 0x0C));
    mmio_w32(g_qmp, 0x24, 0xFFFFU);
    QmpKick();
    int a = QmpWaitEq(0x28, 0xFFFFU, 500);
    mmio_w32(g_qmp, 0x2C, 0xFFFFU);
    QmpKick();
    int b = QmpWaitEq(0x14, 0xFFFFU, 500);
    mmio_w32(g_qmp, 0x18, 0xFFFFU);
    QmpKick();
    int c = QmpWaitEq(0x30, 0xFFFFU, 500);
    LogS("  QMP handshake: a="); LogDec(a); LogS(" b="); LogDec(b); LogS(" c="); LogDec(c); LogNL();
    return a && b && c;
}

static int QmpSendMsg(const char *msg) {
    if (!QmpWaitEq(g_qmp_off, 0, 100)) {
        LogS("  QMP busy before send: "); LogS(msg); LogNL();
        return 0;
    }
    uint8_t buf[64];
    for (uint32_t i = 0; i < 64; i++) buf[i] = 0;
    for (uint32_t i = 0; i < 63 && msg[i]; i++) buf[i] = (uint8_t)msg[i];

    for (uint32_t i = 0; i < 64; i += 4) {
        uint32_t w = (uint32_t)buf[i] | ((uint32_t)buf[i + 1] << 8) |
                     ((uint32_t)buf[i + 2] << 16) | ((uint32_t)buf[i + 3] << 24);
        mmio_w32(g_qmp, g_qmp_off + 4 + i, w);
    }
    mmio_w32(g_qmp, g_qmp_off, 64);
    (void)mmio_r32(g_qmp, g_qmp_off);
    QmpKick();
    int ok = QmpWaitEq(g_qmp_off, 0, 500);
    LogS("  QMP send ["); LogS(msg); LogS("] -> "); LogS(ok ? "OK" : "TIMEOUT"); LogNL();
    return ok;
}

/* ============================================================================
 * S23 PCIe0 QMP PHY Init Sequence (115 triplets from s23_live.dtb)
 * ============================================================================ */
static const struct { uint16_t off; uint8_t val; uint8_t delay_us; } g_s23_pcie0_phy_seq[] = {
    { 0x0240, 0x03, 0 }, { 0x00c0, 0x01, 0 }, { 0x00cc, 0x62, 0 }, { 0x00d0, 0x02, 0 },
    { 0x0060, 0xf8, 0 }, { 0x0064, 0x01, 0 }, { 0x0000, 0x93, 0 }, { 0x0004, 0x01, 0 },
    { 0x00e0, 0x90, 0 }, { 0x00e4, 0x82, 0 }, { 0x00f4, 0x07, 0 }, { 0x0070, 0x02, 0 },
    { 0x0010, 0x02, 0 }, { 0x0074, 0x16, 0 }, { 0x0014, 0x16, 0 }, { 0x0078, 0x36, 0 },
    { 0x0018, 0x36, 0 }, { 0x0110, 0x08, 0 }, { 0x00bc, 0x0a, 0 }, { 0x0120, 0x42, 0 },
    { 0x0080, 0x04, 0 }, { 0x0084, 0x0d, 0 }, { 0x0020, 0x0a, 0 }, { 0x0024, 0x1a, 0 },
    { 0x0088, 0x41, 0 }, { 0x0028, 0x34, 0 }, { 0x0090, 0xab, 0 }, { 0x0094, 0xaa, 0 },
    { 0x0098, 0x01, 0 }, { 0x0030, 0x55, 0 }, { 0x0034, 0x55, 0 }, { 0x0038, 0x01, 0 },
    { 0x0140, 0x14, 0 }, { 0x0164, 0x34, 0 }, { 0x003c, 0x01, 0 }, { 0x001c, 0x04, 0 },
    { 0x0174, 0x16, 0 }, { 0x01bc, 0x0f, 0 }, { 0x0170, 0xa0, 0 }, { 0x11a4, 0x38, 0 },
    { 0x10dc, 0x11, 0 }, { 0x1160, 0xbf, 0 }, { 0x1164, 0xbf, 0 }, { 0x1168, 0xb7, 0 },
    { 0x116c, 0xea, 0 }, { 0x115c, 0x3f, 0 }, { 0x1174, 0x5c, 0 }, { 0x1178, 0x9c, 0 },
    { 0x117c, 0x1a, 0 }, { 0x1180, 0x89, 0 }, { 0x1170, 0xdc, 0 }, { 0x1188, 0x94, 0 },
    { 0x118c, 0x5b, 0 }, { 0x1190, 0x1a, 0 }, { 0x1194, 0x89, 0 }, { 0x10cc, 0x00, 0 },
    { 0x1008, 0x09, 0 }, { 0x1014, 0x05, 0 }, { 0x104c, 0x08, 0 }, { 0x1050, 0x08, 0 },
    { 0x10d8, 0x0f, 0 }, { 0x1118, 0x1c, 0 }, { 0x10f8, 0x07, 0 }, { 0x11f8, 0x08, 0 },
    { 0x1600, 0x00, 0 }, { 0x0e84, 0x15, 0 }, { 0x0e90, 0x3f, 0 }, { 0x0ee4, 0x02, 0 },
    { 0x0e40, 0x06, 0 }, { 0x0e3c, 0x18, 0 }, { 0x19a4, 0x38, 0 }, { 0x18dc, 0x11, 0 },
    { 0x1960, 0xbf, 0 }, { 0x1964, 0xbf, 0 }, { 0x1968, 0xb7, 0 }, { 0x196c, 0xea, 0 },
    { 0x195c, 0x3f, 0 }, { 0x1974, 0x5c, 0 }, { 0x1978, 0x9c, 0 }, { 0x197c, 0x1a, 0 },
    { 0x1980, 0x89, 0 }, { 0x1970, 0xdc, 0 }, { 0x1988, 0x94, 0 }, { 0x198c, 0x5b, 0 },
    { 0x1990, 0x1a, 0 }, { 0x1994, 0x89, 0 }, { 0x18cc, 0x00, 0 }, { 0x1808, 0x09, 0 },
    { 0x1814, 0x05, 0 }, { 0x184c, 0x08, 0 }, { 0x1850, 0x08, 0 }, { 0x18d8, 0x0f, 0 },
    { 0x1918, 0x1c, 0 }, { 0x18f8, 0x07, 0 }, { 0x19f8, 0x08, 0 }, { 0x1684, 0x15, 0 },
    { 0x1690, 0x3f, 0 }, { 0x16e4, 0x02, 0 }, { 0x1640, 0x06, 0 }, { 0x163c, 0x18, 0 },
    { 0x02dc, 0x05, 0 }, { 0x0388, 0x77, 0 }, { 0x0398, 0x0b, 0 }, { 0x06a4, 0x1e, 0 },
    { 0x06f4, 0x27, 0 }, { 0x03e0, 0x0f, 0 }, { 0x060c, 0x1d, 0 }, { 0x0614, 0x07, 0 },
    { 0x0620, 0xc1, 0 }, { 0x0694, 0x00, 0 }, { 0x03d0, 0x8c, 0 }, { 0x0368, 0x17, 0 },
    { 0x0370, 0x2e, 0 }, { 0x0200, 0x00, 0 }, { 0x0244, 0x03, 0 }
};

static int PollReg(volatile uint8_t *base, uint32_t off, uint32_t mask, uint32_t want, uint32_t timeout_us) {
    for (uint32_t i = 0; i < timeout_us; i += 10) {
        if ((mmio_r32(base, off) & mask) == want) return 1;
        KeStallExecutionProcessor(10);
    }
    return (mmio_r32(base, off) & mask) == want;
}

static void SetTlmmGpio(volatile uint8_t *tlmm, uint32_t pin, uint32_t cfg, int out_val) {
    uint32_t off = pin * 0x1000U;
    if (out_val >= 0) {
        mmio_w32(tlmm, off + 0x04, out_val ? 2U : 0U);
    }
    mmio_w32(tlmm, off + 0x00, cfg);
    LogS("  GPIO"); LogDec(pin);
    LogS(" set cfg=0x"); LogHex(mmio_r32(tlmm, off + 0x00), 4);
    LogS(" io=0x"); LogHex(mmio_r32(tlmm, off + 0x04), 2);
    LogNL();
}

static PVOID MapPhys(uint64_t phys, size_t sz, const char *name) {
    PHYSICAL_ADDRESS pa;
    pa.QuadPart = (int64_t)phys;
    PVOID p = MmMapIoSpace(pa, sz, 0 /* MmNonCached */);
    LogS("Map "); LogS(name); LogS(" @ 0x"); LogHex(phys, 8);
    LogS(" (0x"); LogHex(sz, 6); LogS(") -> ");
    if (p) { LogS("OK"); } else { LogS("FAILED!"); }
    LogNL();
    return p;
}

/* ============================================================================
 * DriverEntry
 * ============================================================================ */
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    (void)DriverObject;
    (void)RegistryPath;

    LogOpen();
    uint32_t stage_mask = ReadStageMask();
    LogS("=== s23pcieprobe.sys v1.0.0.13 started (StageMask=0x");
    LogHex(stage_mask, 2);
    LogS(") ===");
    LogNL();

    volatile uint8_t *tlmm  = (volatile uint8_t *)MapPhys(0x0F100000ULL, 0x100000, "TLMM");
    volatile uint8_t *gcc   = (volatile uint8_t *)MapPhys(0x00100000ULL, 0x1F5000, "GCC");
    volatile uint8_t *tcsr  = (volatile uint8_t *)MapPhys(0x01FD5000ULL, 0x001000, "TCSR");
    g_cmddb       = (volatile uint8_t *)MapPhys(0x81C60000ULL, 0x020000, "CMD_DB");
    g_rsc         = (volatile uint8_t *)MapPhys(0x17A20000ULL, 0x002000, "APPS_RSC_DRV2");
    g_spmi_core   = (volatile uint8_t *)MapPhys(0x0C400000ULL, 0x004000, "SPMI_CORE");
    g_spmi_cnfg   = (volatile uint8_t *)MapPhys(0x0C42D000ULL, 0x004000, "SPMI_CNFG");
    g_spmi_obsrvr = (volatile uint8_t *)MapPhys(0x0C440000ULL, 0x080000, "SPMI_OBSRVR");
    g_spmi_chnls  = (volatile uint8_t *)MapPhys(0x0C500000ULL, 0x400000, "SPMI_CHNLS");
    g_qmp         = (volatile uint8_t *)MapPhys(0x0C300000ULL, 0x001000, "AOSS_QMP");
    g_ipcc        = (volatile uint8_t *)MapPhys(0x00408000ULL, 0x001000, "IPCC_MPROC");

    if (!tlmm || !gcc || !tcsr || !g_cmddb || !g_rsc || !g_spmi_core || !g_spmi_cnfg || !g_spmi_obsrvr || !g_qmp || !g_ipcc) {
        LogS("FATAL: Required base mapping failed!"); LogNL();
        goto cleanup;
    }

    /* ------------------------------------------------------------------------
     * STAGE 0: Read-Only Discovery & Pre-Flight
     * ------------------------------------------------------------------------ */
    LogS("=== STAGE 0: Read-Only Hardware Discovery ==="); LogNL();

    uint32_t spmi_ver = mmio_r32(g_spmi_core, 0x00);
    for (uint32_t i = 0; i < 4096; i++) {
        g_spmi_apid[i] = 0;
        g_spmi_owner[i] = 0xFF;
    }
    if ((spmi_ver & 0xF0000000U) == 0x70000000U) {
        for (uint32_t apid = 0; apid < 1024; apid++) {
            uint32_t v = mmio_r32(g_spmi_core, 0x2000U + 4U * apid);
            if (v == 0) continue;
            uint32_t ppid = (v >> 8) & 0xFFFU;
            uint32_t owner = mmio_r32(g_spmi_cnfg, 4U * apid) & 0x7U;
            if (g_spmi_apid[ppid] == 0 || owner == 0) {
                g_spmi_apid[ppid] = (uint16_t)(apid + 1);
                g_spmi_owner[ppid] = (uint8_t)owner;
            }
        }
    }

    static const uint16_t gpio3_regs[] = { 0x04, 0x05, 0x08, 0x40, 0x41, 0x42, 0x44, 0x45, 0x46 };
    for (uint32_t i = 0; i < sizeof(gpio3_regs) / sizeof(gpio3_regs[0]); i++) {
        uint8_t rv = 0xEE;
        int ok = SpmiReadByte(0, 0xBA00U + gpio3_regs[i], &rv);
        LogS("  PMK8550_GPIO3[0xBA"); LogHex(gpio3_regs[i], 2); LogS("] = ");
        if (ok) { LogS("0x"); LogHex(rv, 2); } else { LogS("READ_FAIL"); }
        LogNL();
    }

    static const uint32_t probe_pins[] = { 80, 81, 82, 83, 94, 95, 96, 204 };
    LogS("TLMM Pins (Pre):"); LogNL();
    for (uint32_t i = 0; i < sizeof(probe_pins) / sizeof(probe_pins[0]); i++) {
        uint32_t p = probe_pins[i];
        uint32_t off = p * 0x1000U;
        LogS("  GPIO"); LogDec(p);
        LogS(": cfg=0x"); LogHex(mmio_r32(tlmm, off + 0x00), 8);
        LogS(" io=0x"); LogHex(mmio_r32(tlmm, off + 0x04), 8);
        LogS(" intr=0x"); LogHex(mmio_r32(tlmm, off + 0x08), 8);
        LogNL();
    }

    LogS("TCSR_PCIE_0_CLKREF (0x01FD5100) = 0x"); LogHex(mmio_r32(tcsr, 0x100), 8); LogNL();
    LogS("GCC GPLL0_MODE(0x00000)=0x"); LogHex(mmio_r32(gcc, 0x00000), 8);
    LogS(" GPLL0_USER_CTL(0x00004)=0x"); LogHex(mmio_r32(gcc, 0x00004), 8);
    LogS(" GPLL_VOTE(0x52018)=0x"); LogHex(mmio_r32(gcc, 0x52018), 8); LogNL();
    LogS("GCC VOTE(0x52000)=0x"); LogHex(mmio_r32(gcc, 0x52000), 8);
    LogS(" VOTE_1(0x52008)=0x"); LogHex(mmio_r32(gcc, 0x52008), 8);
    LogS(" COLLAPSE(0x52020/24)=0x"); LogHex(mmio_r32(gcc, 0x52020), 8);
    LogS("/0x"); LogHex(mmio_r32(gcc, 0x52024), 8); LogNL();
    LogS("GCC PCIE_0_GDSC(0x6B004)=0x"); LogHex(mmio_r32(gcc, 0x6B004), 8);
    LogS(" PHY_GDSC(0x6C000)=0x"); LogHex(mmio_r32(gcc, 0x6C000), 8);
    LogS(" PIPE_MUX(0x6B070)=0x"); LogHex(mmio_r32(gcc, 0x6B070), 8); LogNL();
    LogS("GCC NoC CBCRs: cfg_anoc(0x10028)=0x"); LogHex(mmio_r32(gcc, 0x10028), 8);
    LogS(" cnoc_sf(0x10030)=0x"); LogHex(mmio_r32(gcc, 0x10030), 8);
    LogS(" aggre(0x1003C)=0x"); LogHex(mmio_r32(gcc, 0x1003C), 8);
    LogS(" ddrss(0x1004C)=0x"); LogHex(mmio_r32(gcc, 0x1004C), 8);
    LogS(" qmip(0x6B018)=0x"); LogHex(mmio_r32(gcc, 0x6B018), 8); LogNL();

    /* Dump CMD_DB ARC (3), VRM (4), BCM (5) entries */
    LogS("CMD_DB All Entries:"); LogNL();
    if (g_cmddb[4] == 0xDB && g_cmddb[5] == 0x30 && g_cmddb[6] == 0x03 && g_cmddb[7] == 0x0C) {
        for (uint32_t i = 0; i < 8; i++) {
            const volatile uint8_t *r = g_cmddb + 8 + 16 * i;
            uint16_t slv_id  = *(const volatile uint16_t *)(r + 0);
            uint16_t hdr_off = *(const volatile uint16_t *)(r + 2);
            uint16_t cnt     = *(const volatile uint16_t *)(r + 6);
            if (slv_id == 0) break;
            if ((uint32_t)144U + (uint32_t)hdr_off + (uint32_t)cnt * 24U > 0x20000U) break;
            const volatile uint8_t *e = g_cmddb + 144 + hdr_off;
            for (uint32_t j = 0; j < cnt; j++, e += 24) {
                LogS("  SLV"); LogDec(slv_id); LogS("[");
                for (uint32_t k = 0; k < 8 && e[k]; k++) LogC((char)e[k]);
                LogS("] = 0x"); LogHex(*(const volatile uint32_t *)(e + 16), 5);
                LogNL();
            }
        }
    } else {
        LogS("  CMD_DB invalid magic!"); LogNL();
    }

    /* Scan SPMI SID 0 (PMK8550) & SID 1 (PM8550) CLK (TYPE=0x06) and GPIO (TYPE=0x10) peripherals */
    LogS("SPMI SID 0..1 CLK/GPIO Peripherals:"); LogNL();
    for (uint32_t ppid = 0; ppid < 0x200; ppid++) {
        if (g_spmi_apid[ppid] == 0) continue;
        uint8_t sid = (uint8_t)(ppid >> 8);
        uint16_t base = (uint16_t)((ppid & 0xFFU) << 8);
        uint8_t ptype = 0, psub = 0, pst = 0, pen = 0, p40 = 0, p44 = 0;
        if (!SpmiReadByte(sid, base + 0x04, &ptype)) continue;
        if (ptype != 0x06 && ptype != 0x10) continue;
        (void)SpmiReadByte(sid, base + 0x05, &psub);
        (void)SpmiReadByte(sid, base + 0x08, &pst);
        (void)SpmiReadByte(sid, base + 0x40, &p40);
        (void)SpmiReadByte(sid, base + 0x44, &p44);
        (void)SpmiReadByte(sid, base + 0x46, &pen);
        LogS("  PPID=0x"); LogHex(ppid, 3);
        LogS(" (owner="); LogDec(g_spmi_owner[ppid]);
        LogS(") type=0x"); LogHex(ptype, 2);
        LogS(" sub=0x"); LogHex(psub, 2);
        LogS(" st=0x"); LogHex(pst, 2);
        LogS(" r40=0x"); LogHex(p40, 2);
        LogS(" r44=0x"); LogHex(p44, 2);
        LogS(" en46=0x"); LogHex(pen, 2);
        LogNL();
    }

    /* Dump APPS_RSC_DRV2 TCS 0..6 snapshot */
    LogS("APPS_RSC_DRV2 TCS Snapshot:"); LogNL();
    for (uint32_t t = 0; t < 7; t++) {
        uint32_t cmden = mmio_r32(g_rsc, TcsOff(t, V3_CMDEN));
        uint32_t ctrl  = mmio_r32(g_rsc, TcsOff(t, V3_CTRL));
        uint32_t stat  = mmio_r32(g_rsc, TcsOff(t, V3_STATUS));
        LogS("  TCS"); LogDec(t); LogS(": ctrl=0x"); LogHex(ctrl, 8);
        LogS(" stat=0x"); LogHex(stat, 8); LogS(" cmden=0x"); LogHex(cmden, 4); LogNL();
        for (uint32_t c = 0; c < 16; c++) {
            uint32_t ca = mmio_r32(g_rsc, TcsCmdOff(t, c, V3_ADDR));
            uint32_t cd = mmio_r32(g_rsc, TcsCmdOff(t, c, V3_DATA));
            if ((cmden & (1U << c)) || ca != 0) {
                LogS("    cmd["); LogDec(c); LogS("]: addr=0x"); LogHex(ca, 5);
                LogS(" data=0x"); LogHex(cd, 8); LogNL();
            }
        }
    }

    if ((stage_mask & 1U) == 0) {
        LogS("StageMask bit 0 not set: Stage 0 complete."); LogNL();
        goto cleanup;
    }

    /* ------------------------------------------------------------------------
     * STAGE 1 (0x01): Pin Hold, PMK8550 GPIO3 32kHz Sleep Clock, RPMh Rails, QMP PDC, WLAN_EN
     * ------------------------------------------------------------------------ */
    LogStep("STAGE 1: Cold pin hold, PMK8550 GPIO3 32kHz, RPMh rails, AOSS QMP wlan_pdc, WLAN_EN");

    SetTlmmGpio(tlmm, 94, 0x0201U, 0);
    SetTlmmGpio(tlmm, 95, 0x0007U, -1);
    SetTlmmGpio(tlmm, 96, 0x0003U, -1);
    /* GPIO204 has EGPIO_PRESENT (BIT 11 = 0x0800): MUST set EGPIO_ENABLE (BIT 12 = 0x1000, MSM_APPS_OWNER)
     * per pinctrl-msm.c or physical pad remains owned by remote/LPASS and ignores APPS TLMM OUT! */
    SetTlmmGpio(tlmm, 204, 0x1201U, 0);
    /* Match s23_live.dts bt_en_sleep (0x605) and cnss_wlan_en_sleep (0x607): 2mA pull-down, output-low */
    SetTlmmGpio(tlmm, 81, 0x0201U, 0);
    SetTlmmGpio(tlmm, 80, 0x0201U, 0);

    /* If PCIe0 GDSC / PHY GDSC were left ON by an earlier run, tear down PARF/PHY and power-cycle GDSCs to COLD state */
    if ((mmio_r32(gcc, 0x6B004) & 0x80000000U) != 0) {
        LogStep("STAGE 1.0: Cold teardown of active PCIe0 controller/PHY/GDSCs");
        /* Ensure gcc_pcie_0_cfg_ahb_clk (0x52008 bit 2 / CBCR 0x6B038 bit 31 == 0) is active before PARF/PHY MMIO */
        if ((mmio_r32(gcc, 0x6B038) & 0x80000000U) != 0) {
            mmio_rmw32(gcc, 0x52008, 0U, (1U << 2));
            for (uint32_t w = 0; w < 200; w++) {
                if ((mmio_r32(gcc, 0x6B038) & 0x80000000U) == 0) break;
                KeStallExecutionProcessor(10);
            }
        }
        if ((mmio_r32(gcc, 0x6B038) & 0x80000000U) == 0) {
            volatile uint8_t *parf_td = (volatile uint8_t *)MapPhys(0x01C00000ULL, 0x3000, "PARF_TD");
            volatile uint8_t *phy_td  = (volatile uint8_t *)MapPhys(0x01C06000ULL, 0x2000, "PHY_TD");
            if (parf_td && phy_td) {
                mmio_w32(parf_td, 0x1B0, 0U);
                if ((mmio_r32(gcc, 0x6C000) & 0x80000000U) != 0) {
                    mmio_w32(phy_td, 0x244, 0U);
                    mmio_w32(phy_td, 0x200, 1U);
                    mmio_w32(phy_td, 0x240, 0U);
                }
                mmio_rmw32(parf_td, 0x040, 0U, 1U);
                mmio_rmw32(parf_td, 0x2C4, 0U, 1U);
            }
            if (phy_td)  MmUnmapIoSpace((PVOID)phy_td,  0x2000);
            if (parf_td) MmUnmapIoSpace((PVOID)parf_td, 0x3000);
        } else {
            LogS("  STAGE 1.0: cfg_ahb_clk still OFF (0x6B038=0x");
            LogHex(mmio_r32(gcc, 0x6B038), 8);
            LogS("), skipping PARF/PHY MMIO teardown"); LogNL();
        }

        /* Disable pipe_clk, switch pipe_clk_src mux to XO (2), pulse canonical DTS BCRs (0x6B000, 0x6C01C), collapse GDSCs */
        mmio_rmw32(gcc, 0x52008, (1U << 4), 0U);
        StallUs(200);
        mmio_rmw32(gcc, 0x6B070, 3U, 2U);
        StallUs(200);
        mmio_rmw32(gcc, 0x6B000, 1U, 1U);
        mmio_rmw32(gcc, 0x6C01C, 1U, 1U);
        StallUs(1000);
        mmio_rmw32(gcc, 0x52008, 0x2FU, 0U);
        mmio_rmw32(gcc, 0x52000, (1U << 22), 0U);
        mmio_rmw32(gcc, 0x6C000, 0U, 1U);
        mmio_rmw32(gcc, 0x6B004, 0U, 1U);
        mmio_rmw32(gcc, 0x52020, 0U, 0x9U);
        mmio_rmw32(gcc, 0x52024, 0U, 0x9U);
        StallUs(2000);
        mmio_rmw32(gcc, 0x6B000, 1U, 0U);
        mmio_rmw32(gcc, 0x6C01C, 1U, 0U);
        LogS("  After teardown: GDSC_CORE(0x6B004)=0x"); LogHex(mmio_r32(gcc, 0x6B004), 8);
        LogS(" GDSC_PHY(0x6C000)=0x"); LogHex(mmio_r32(gcc, 0x6C000), 8);
        LogS(" PIPE_MUX(0x6B070)=0x"); LogHex(mmio_r32(gcc, 0x6B070), 8);
        LogNL();
    }

    /* Cold power-off QCA6490 dedicated rails (never disable ldob15!; smpg5 is unconnected on dm1q) so QCA6490 PMU resets its bootstrap latch */
    static const char *wlan_off_rails[] = { "smpg4", "smpg6", "smpe4", "smpg2" };
    for (uint32_t i = 0; i < sizeof(wlan_off_rails) / sizeof(wlan_off_rails[0]); i++) {
        uint32_t ra = CmdDbLookup(wlan_off_rails[i]);
        if (ra) RpmhWriteTcs0(ra + 0x04U, 0U);
    }
    /* 150 ms delay for cold rail discharge + BT_EN=0/WLAN_EN=0 PMU sequencing */
    SleepMs(150);
    LogS("After cold power-off: GPIO82(SW_CTRL)_IO=0x"); LogHex(mmio_r32(tlmm, 82U * 0x1000U + 0x04), 2);
    LogS(" GPIO95(CLKREQ)_IO=0x"); LogHex(mmio_r32(tlmm, 95U * 0x1000U + 0x04), 2);
    LogNL();

    uint8_t g3_en = 0, g3_sub = 0, g3_mode = 0, g3_src = 0;
    (void)SpmiReadByte(0, 0xBA05, &g3_sub);
    (void)SpmiReadByte(0, 0xBA40, &g3_mode);
    (void)SpmiReadByte(0, 0xBA44, &g3_src);
    if (SpmiReadByte(0, 0xBA46, &g3_en)) {
        if (g_spmi_owner[0x0BA] == 0 && ((g3_en & 0x80U) == 0 || g3_mode != 0x01U || g3_src != 0x02U)) {
            LogS("  Programming PMK8550 GPIO3 32kHz sleep clock..."); LogNL();
            SpmiWriteByte(0, 0xBA40, 0x01);
            SpmiWriteByte(0, 0xBA41, 0x00);
            SpmiWriteByte(0, 0xBA42, 0x05);
            SpmiWriteByte(0, 0xBA44, 0x02);
            SpmiWriteByte(0, 0xBA45, 0x03);
            SpmiWriteByte(0, 0xBA46, 0x80);
        }
        uint8_t r40 = 0, r41 = 0, r42 = 0, r44 = 0, r45 = 0, r46 = 0, r08 = 0;
        (void)SpmiReadByte(0, 0xBA08, &r08);
        (void)SpmiReadByte(0, 0xBA40, &r40);
        (void)SpmiReadByte(0, 0xBA41, &r41);
        (void)SpmiReadByte(0, 0xBA42, &r42);
        (void)SpmiReadByte(0, 0xBA44, &r44);
        (void)SpmiReadByte(0, 0xBA45, &r45);
        (void)SpmiReadByte(0, 0xBA46, &r46);
        LogS("  PMK8550_GPIO3 post-write: st08=0x"); LogHex(r08, 2);
        LogS(" r40=0x"); LogHex(r40, 2);
        LogS(" r41=0x"); LogHex(r41, 2);
        LogS(" r42=0x"); LogHex(r42, 2);
        LogS(" r44=0x"); LogHex(r44, 2);
        LogS(" r45=0x"); LogHex(r45, 2);
        LogS(" r46=0x"); LogHex(r46, 2);
        LogNL();
    }

    uint32_t xo_addr = CmdDbLookup("xo.lvl");
    if (xo_addr) RpmhWriteTcs0(xo_addr, 3);
    uint32_t cx_addr = CmdDbLookup("cx.lvl");
    if (cx_addr) RpmhWriteTcs0(cx_addr, 3);
    uint32_t mx_addr = CmdDbLookup("mx.lvl");
    if (mx_addr) RpmhWriteTcs0(mx_addr, 3);

    /* Enable PMK8550 RF/LN_BB XO clock buffers via RPMh VRM (addr + 0x04 = 1) */
    static const char *vrm_clks[] = { "clka1", "clka2", "clka3", "clka4", "clka6", "clka7", "clka8", "rclkha1" };
    for (uint32_t i = 0; i < sizeof(vrm_clks) / sizeof(vrm_clks[0]); i++) {
        uint32_t ca = CmdDbLookup(vrm_clks[i]);
        if (ca) RpmhWriteTcs0(ca + 0x04U, 1U);
    }
    for (uint32_t ppid = 0x054; ppid <= 0x05B; ppid++) {
        uint8_t cst = 0, cen = 0;
        (void)SpmiReadByte(0, (uint16_t)(ppid << 8) + 0x08, &cst);
        (void)SpmiReadByte(0, (uint16_t)(ppid << 8) + 0x46, &cen);
        LogS("  PMK8550_CLK[0x"); LogHex(ppid, 3); LogS("]: st=0x"); LogHex(cst, 2);
        LogS(" en46=0x"); LogHex(cen, 2); LogNL();
    }

    /* Restore high DDR/LLCC bandwidth on MC0/SH0 for Display, and vote CN0 (CNOC) + SN7 (PCIe ANOC) */
    uint32_t mc0_addr = CmdDbLookup("MC0");
    if (mc0_addr) RpmhWriteTcs0(mc0_addr, 0x62CDD764U);
    uint32_t sh0_addr = CmdDbLookup("SH0");
    if (sh0_addr) RpmhWriteTcs0(sh0_addr, 0x62CDD764U);
    uint32_t cn0_addr = CmdDbLookup("CN0");
    if (cn0_addr) RpmhWriteTcs0(cn0_addr, 0x60004004U);
    uint32_t sn7_addr = CmdDbLookup("SN7");
    if (sn7_addr) RpmhWriteTcs0(sn7_addr, 0x60004004U);

    VoteVrmRail("ldoe1",  880,  1, 7);
    VoteVrmRail("ldoe3",  1200, 1, 7);
    VoteVrmRail("ldob15", 1800, 1, 7);
    VoteVrmRail("smpg2",  1012, 1, 7);
    VoteVrmRail("smpe4",  966,  1, 7);
    VoteVrmRail("smpg6",  1900, 1, 7);
    VoteVrmRail("smpg4",  1350, 1, 7);
    /* Note: vdd-wlan-supply (smpg5) is intentionally omitted — empty in s23_live.dts (unconnected on dm1q) */
    /* Also trigger vrm.wlan (0x46808) if present */
    uint32_t vrm_wlan = CmdDbLookup("vrm.wlan");
    if (vrm_wlan) RpmhWriteTcs0(vrm_wlan, 1U);

    /* Scan all SPMI peripherals on SID 1..10 (PPID 0x100..0xAFF) after VRM votes */
    LogS("SPMI SID 1..10 All Peripherals (Post-Vote):"); LogNL();
    for (uint32_t ppid = 0x100; ppid < 0xB00; ppid++) {
        if (g_spmi_apid[ppid] == 0) continue;
        uint8_t sid = (uint8_t)(ppid >> 8);
        uint16_t base = (uint16_t)((ppid & 0xFFU) << 8);
        uint8_t ptype = 0, psub = 0, pst = 0, vlb = 0, vub = 0, pmode = 0, pen = 0;
        if (!SpmiReadByte(sid, base + 0x04, &ptype)) continue;
        if (ptype == 0x10 || ptype == 0x06) continue;
        (void)SpmiReadByte(sid, base + 0x05, &psub);
        (void)SpmiReadByte(sid, base + 0x08, &pst);
        (void)SpmiReadByte(sid, base + 0x40, &vlb);
        (void)SpmiReadByte(sid, base + 0x41, &vub);
        (void)SpmiReadByte(sid, base + 0x45, &pmode);
        (void)SpmiReadByte(sid, base + 0x46, &pen);
        uint32_t mv = ((uint32_t)vub << 8) | (uint32_t)vlb;
        LogS("  PPID=0x"); LogHex(ppid, 3);
        LogS(" (own="); LogDec(g_spmi_owner[ppid]);
        LogS(") t=0x"); LogHex(ptype, 2);
        LogS(" sub=0x"); LogHex(psub, 2);
        LogS(" st=0x"); LogHex(pst, 2);
        LogS(" en=0x"); LogHex(pen, 2);
        LogS(" mode=0x"); LogHex(pmode, 2);
        LogS(" v="); LogDec(mv); LogS("mV");
        LogNL();
    }

    if (QmpOpenIfNeeded()) {
        for (uint32_t i = 0; i < 11; i++) {
            QmpSendMsg(g_s23_pdc_msgs[i]);
        }
    }

    mmio_w32(tlmm, 82U * 0x1000U + 0x08, 0x7C2U);
    mmio_w32(tlmm, 96U * 0x1000U + 0x08, 0x7C2U);
    /* Temporarily set GPIO95 to FUNC_SEL=0 (0x0003) to sample true physical pad level before & after WLAN_EN */
    mmio_w32(tlmm, 95U * 0x1000U + 0x00, 0x0003U);
    SleepMs(10);
    LogS("Before WLAN_EN (GPIO95 func0): GPIO82_IO=0x"); LogHex(mmio_r32(tlmm, 82U * 0x1000U + 0x04), 2);
    LogS(" GPIO95_IO=0x"); LogHex(mmio_r32(tlmm, 95U * 0x1000U + 0x04), 2);
    LogNL();

    /* QCA6490 XO_CLK_GPIO (GPIO204) bootstrap latch sequence around WLAN_EN (GPIO80) (cnss2/power.c:965-1035).
     * CRITICAL: GPIO204 has EGPIO_PRESENT (BIT 11 = 0x0800), so EGPIO_ENABLE (BIT 12 = 0x1000) MUST be set
     * for APPS TLMM to own and drive the physical pad HIGH (io=0x03)! */
    SetTlmmGpio(tlmm, 204, 0x13C3U, 1);
    StallUs(200);
    LogS("  GPIO204 after 200us settle: cfg=0x"); LogHex(mmio_r32(tlmm, 204U * 0x1000U + 0x00), 4);
    LogS(" io=0x"); LogHex(mmio_r32(tlmm, 204U * 0x1000U + 0x04), 2); LogNL();
    SetTlmmGpio(tlmm, 80, 0x03C3U, 1);
    SleepMs(6);
    SetTlmmGpio(tlmm, 204, 0x1201U, 0);
    SleepMs(50);
    LogS("After WLAN_EN (GPIO95 func0): GPIO80_IO=0x"); LogHex(mmio_r32(tlmm, 80U * 0x1000U + 0x04), 2);
    LogS(" GPIO82(SW_CTRL)_IO=0x"); LogHex(mmio_r32(tlmm, 82U * 0x1000U + 0x04), 2);
    LogS(" GPIO95(CLKREQ_PAD)_IO=0x"); LogHex(mmio_r32(tlmm, 95U * 0x1000U + 0x04), 2);
    LogS(" GPIO96(WAKE)_IO=0x"); LogHex(mmio_r32(tlmm, 96U * 0x1000U + 0x04), 2);
    LogNL();
    /* Restore GPIO95 to FUNC_SEL=1 (pcie0_clk_req_n, 0x0007) */
    SetTlmmGpio(tlmm, 95, 0x0007U, -1);

    if ((stage_mask & 2U) == 0) {
        LogS("StageMask bit 1 (0x02) not set: stopping after Stage 1."); LogNL();
        goto cleanup;
    }

    /* ------------------------------------------------------------------------
     * STAGE 2A (0x02): TCSR Clkref + GCC GDSCs + AUX/RCHNG RCGs + Branch Votes
     * ------------------------------------------------------------------------ */
    LogStep("STAGE 2A.1: TCSR_PCIE_0_CLKREF (0x01FD5100 |= 1)");
    mmio_rmw32(tcsr, 0x100, 0, 1U);
    LogS("  TCSR_PCIE_0_CLKREF = 0x"); LogHex(mmio_r32(tcsr, 0x100), 8); LogNL();

    LogStep("STAGE 2A.2: Clear GDSC collapse votes (0x52020/0x52024 &= ~0x9) & enable GDSCs");
    mmio_rmw32(gcc, 0x52020, 0x9U, 0);
    mmio_rmw32(gcc, 0x52024, 0x9U, 0);
    mmio_rmw32(gcc, 0x6B004, 1U, 0);
    int gdsc_core_ok = PollReg(gcc, 0x6B004, 0x80000000U, 0x80000000U, 20000);
    mmio_rmw32(gcc, 0x6C000, 1U, 0);
    int gdsc_phy_ok  = PollReg(gcc, 0x6C000, 0x80000000U, 0x80000000U, 20000);
    LogS("  GDSCs: core=0x"); LogHex(mmio_r32(gcc, 0x6B004), 8); LogS(" (ok="); LogDec(gdsc_core_ok);
    LogS(") phy=0x"); LogHex(mmio_r32(gcc, 0x6C000), 8); LogS(" (ok="); LogDec(gdsc_phy_ok); LogS(")"); LogNL();
    if (!gdsc_core_ok || !gdsc_phy_ok) {
        LogS("ABORT: PCIe0 GDSCs did not power on!"); LogNL();
        goto cleanup;
    }

    /* Switch pipe_clk_src mux (0x6B070=0) right after GDSCs power on (exact pci-msm.c msm_pcie_clk_init / G289Driver.c order) */
    mmio_rmw32(gcc, 0x6B070, 3U, 0U);
    LogS("  PIPE_MUX(0x6B070) switched to ext(0): 0x"); LogHex(mmio_r32(gcc, 0x6B070), 8); LogNL();

    LogStep("STAGE 2A.3: Configure AUX RCG (0x6B074/0x6B078) and PHY_RCHNG RCG (0x6B058/0x6B05C)");
    /* Note: Do NOT touch GPLL0_USER_CTL (0x00100004)! */
    mmio_rmw32(gcc, 0x6B074, 0, 2U);
    mmio_w32(gcc, 0x6B078, 0x001U);
    mmio_rmw32(gcc, 0x6B074, 0, 1U);
    int aux_rcg_ok = PollReg(gcc, 0x6B074, 1U, 0, 5000);

    mmio_rmw32(gcc, 0x6B058, 0, 2U);
    mmio_w32(gcc, 0x6B05C, 0x00100605U);
    mmio_rmw32(gcc, 0x6B058, 0, 1U);
    int rchng_rcg_ok = PollReg(gcc, 0x6B058, 1U, 0, 5000);
    mmio_rmw32(gcc, 0x6B058, 2U, 0);
    LogS("  RCGs: aux_ok="); LogDec(aux_rcg_ok);
    LogS(" (cmd=0x"); LogHex(mmio_r32(gcc, 0x6B074), 8); LogS(" cfg=0x"); LogHex(mmio_r32(gcc, 0x6B078), 8);
    LogS(") rchng_ok="); LogDec(rchng_rcg_ok);
    LogS(" (cmd=0x"); LogHex(mmio_r32(gcc, 0x6B058), 8); LogS(" cfg=0x"); LogHex(mmio_r32(gcc, 0x6B05C), 8); LogS(")");
    LogNL();

    LogStep("STAGE 2A.4: Enable PCIe0 + NoC branch clock votes (0x52008 &= ~pipe, |= 0x6F, 0x52000 |= qmip|aggre|ddrss|cfg_anoc|rchng)");
    mmio_rmw32(gcc, 0x52008, (1U << 4), 0x6FU);
    mmio_rmw32(gcc, 0x52000, 0, (1U << 11) | (1U << 12) | (1U << 19) | (1U << 20) | (1U << 22));
    StallUs(1000);

    static const uint32_t prepipe_cbcrs[] = {
        0x6B01C, 0x6B020, 0x6B02C, 0x6B038, 0x6B03C, 0x6B054,
        0x10028, 0x10030, 0x1003C, 0x1004C
    };
    int clocks_ok = 1;
    for (uint32_t i = 0; i < sizeof(prepipe_cbcrs) / sizeof(prepipe_cbcrs[0]); i++) {
        int ok = PollReg(gcc, prepipe_cbcrs[i], 0x80000000U, 0, 10000);
        LogS("  CBCR+0x"); LogHex(prepipe_cbcrs[i], 5); LogS(" = 0x");
        LogHex(mmio_r32(gcc, prepipe_cbcrs[i]), 8); LogS(" ok="); LogDec(ok); LogNL();
        if (!ok) clocks_ok = 0;
    }
    if (!clocks_ok) {
        LogS("ABORT: Pre-PIPE branch clocks did not turn on!"); LogNL();
        goto cleanup;
    }

    if ((stage_mask & 4U) == 0) {
        LogS("StageMask bit 2 (0x04) not set: stopping after Stage 2A."); LogNL();
        goto cleanup;
    }

    /* ------------------------------------------------------------------------
     * STAGE 2B (0x04): Map and Read-Only Probe of PARF (0x01C00000) & PHY (0x01C06000)
     * ------------------------------------------------------------------------ */
    LogStep("STAGE 2B.1: Map PARF (0x01C00000) and PHY (0x01C06000)");
    volatile uint8_t *parf = (volatile uint8_t *)MapPhys(0x01C00000ULL, 0x3000, "PCIE0_PARF");
    volatile uint8_t *phy  = (volatile uint8_t *)MapPhys(0x01C06000ULL, 0x2000, "PCIE0_PHY");
    if (!parf || !phy) {
        if (parf) MmUnmapIoSpace((PVOID)parf, 0x3000);
        if (phy)  MmUnmapIoSpace((PVOID)phy, 0x2000);
        goto cleanup;
    }

    LogStep("STAGE 2B.2: Read PARF+0x00 (SYS_CTRL)");
    uint32_t parf_sys = mmio_r32(parf, 0x00);
    LogS("  PARF_SYS_CTRL(0x00) = 0x"); LogHex(parf_sys, 8); LogNL();

    LogStep("STAGE 2B.3: Read PARF+0x20, +0x40, +0x1B0, +0x234");
    LogS("  PM_CTRL(0x20)=0x"); LogHex(mmio_r32(parf, 0x20), 8);
    LogS(" PHY_CTRL(0x40)=0x"); LogHex(mmio_r32(parf, 0x40), 8);
    LogS(" LTSSM(0x1B0)=0x"); LogHex(mmio_r32(parf, 0x1B0), 8);
    LogS(" SID_OFFSET(0x234)=0x"); LogHex(mmio_r32(parf, 0x234), 8);
    LogNL();

    LogStep("STAGE 2B.4: Read PHY+0x214 (PCS_STATUS) and PHY+0x240 (PWRDN)");
    LogS("  PHY PCS_STATUS(0x214)=0x"); LogHex(mmio_r32(phy, 0x214), 8);
    LogS(" PWRDN(0x240)=0x"); LogHex(mmio_r32(phy, 0x240), 8);
    LogNL();

    if ((stage_mask & 8U) == 0) {
        LogS("StageMask bit 3 (0x08) not set: stopping after Stage 2B."); LogNL();
        MmUnmapIoSpace((PVOID)phy, 0x2000);
        MmUnmapIoSpace((PVOID)parf, 0x3000);
        goto cleanup;
    }

    /* ------------------------------------------------------------------------
     * STAGE 2C (0x08): Pipe Mux, BCR Resets, PARF Init, 115-Triplet PHY, PIPE Clock & PCS_STATUS Poll
     * ------------------------------------------------------------------------ */
    LogStep("STAGE 2C.1: Ensure pipe_clk_src mux (0x6B070=0) & pulse canonical BCR resets (0x6B000, 0x6C01C)");
    mmio_rmw32(gcc, 0x6B070, 3U, 0U);
    mmio_rmw32(gcc, 0x6B000, 1U, 1U);
    StallUs(1000);
    mmio_rmw32(gcc, 0x6B000, 1U, 0U);
    StallUs(1000);
    mmio_rmw32(gcc, 0x6C01C, 1U, 1U);
    StallUs(1000);
    mmio_rmw32(gcc, 0x6C01C, 1U, 0U);
    StallUs(1000);
    LogS("  After BCR resets: PARF_LTSSM(0x1B0)=0x"); LogHex(mmio_r32(parf, 0x1B0), 8);
    LogS(" PHY_PCS_STATUS(0x214)=0x"); LogHex(mmio_r32(phy, 0x214), 8);
    LogS(" PWRDN(0x240)=0x"); LogHex(mmio_r32(phy, 0x240), 8);
    LogNL();

    LogStep("STAGE 2C.2: Program PARF registers (exact pci-msm.c / g289_rc0.c sequence)");
    mmio_w32(parf, 0x1000, 4U);
    mmio_rmw32(parf, 0x0020, (1U << 5), 0U);
    mmio_rmw32(parf, 0x0040, 1U, 0U);
    mmio_w32(parf, 0x0350, 0U);
    mmio_w32(parf, 0x0000, 0x365EU);
    mmio_rmw32(parf, 0x0174, 0U, (1U << 4));
    mmio_w32(parf, 0x022C, 0U);
    mmio_w32(parf, 0x0508, 0U);
    mmio_w32(parf, 0x0358, 0x04000000U);
    mmio_rmw32(parf, 0x01A8, 0U, (1U << 31));
    LogS("  PARF configured."); LogNL();

    LogStep("STAGE 2C.3: Program 115-triplet QMP PHY table (0x01C06000)");
    const uint32_t phy_cnt = (uint32_t)(sizeof(g_s23_pcie0_phy_seq) / sizeof(g_s23_pcie0_phy_seq[0]));
    for (uint32_t i = 0; i < phy_cnt; i++) {
        mmio_w32(phy, g_s23_pcie0_phy_seq[i].off, g_s23_pcie0_phy_seq[i].val);
        if (g_s23_pcie0_phy_seq[i].delay_us) {
            StallUs(g_s23_pcie0_phy_seq[i].delay_us);
        }
    }
    StallUs(1000);
    LogS("  PHY 115 triplets written: +0x200=0x"); LogHex(mmio_r32(phy, 0x200), 2);
    LogS(" +0x240=0x"); LogHex(mmio_r32(phy, 0x240), 2);
    LogS(" +0x244=0x"); LogHex(mmio_r32(phy, 0x244), 2); LogNL();

    LogStep("STAGE 2C.4: Enable gcc_pcie_0_pipe_clk (0x52008 |= 1<<4) & poll PCS_STATUS (0x214 bit 6 == 0)");
    mmio_rmw32(gcc, 0x52008, 0, (1U << 4));
    int phy_ready = 0, pipe_ready = 0;
    for (uint32_t poll = 0; poll < 50; poll++) {
        uint32_t pcs_st = mmio_r32(phy, 0x214);
        uint32_t pipe_cbcr = mmio_r32(gcc, 0x6B048);
        if ((pcs_st & (1U << 6)) == 0) phy_ready = 1;
        if ((pipe_cbcr & 0x80000000U) == 0) pipe_ready = 1;
        if (phy_ready && pipe_ready) break;
        StallUs(1000);
    }
    LogS("  PHY/PIPE poll: PCS_STATUS(0x214)=0x"); LogHex(mmio_r32(phy, 0x214), 8);
    LogS(" PIPE_CBCR(0x6B048)=0x"); LogHex(mmio_r32(gcc, 0x6B048), 8);
    LogS(" phy_ready="); LogDec(phy_ready); LogS(" pipe_ready="); LogDec(pipe_ready); LogNL();

    if (!phy_ready || !pipe_ready) {
        LogS("ABORT: PHY or PIPE clock not ready! Refusing to touch DBI 0x60000000."); LogNL();
        MmUnmapIoSpace((PVOID)phy, 0x2000);
        MmUnmapIoSpace((PVOID)parf, 0x3000);
        goto cleanup;
    }

    if ((stage_mask & 0x10U) == 0) {
        LogS("StageMask bit 4 (0x10) not set: stopping after Stage 2C."); LogNL();
        MmUnmapIoSpace((PVOID)phy, 0x2000);
        MmUnmapIoSpace((PVOID)parf, 0x3000);
        goto cleanup;
    }

    /* ------------------------------------------------------------------------
     * STAGE 2D (0x10): Map DBI (0x60000000), Read RootId, Release PERST#, Train Link to L0
     * ------------------------------------------------------------------------ */
    SleepMs(10);
    LogStep("STAGE 2D.1: Map DBI (0x60000000, 0x2000)");
    volatile uint8_t *dbi = (volatile uint8_t *)MapPhys(0x60000000ULL, 0x2000, "PCIE0_DBI_ELBI_ATU");
    if (!dbi) {
        MmUnmapIoSpace((PVOID)phy, 0x2000);
        MmUnmapIoSpace((PVOID)parf, 0x3000);
        goto cleanup;
    }

    LogStep("STAGE 2D.2: Read DBI+0x7C (RootLinkCap) and DBI+0x00 (RootId)");
    uint32_t root_link_cap = mmio_r32(dbi, 0x7C);
    uint32_t root_id       = mmio_r32(dbi, 0x00);
    uint32_t root_class    = mmio_r32(dbi, 0x08);
    LogS("  DBI Root Port: ID=0x"); LogHex(root_id, 8);
    LogS(" Class=0x"); LogHex(root_class, 8);
    LogS(" LinkCap=0x"); LogHex(root_link_cap, 8);
    LogNL();

    if ((root_id & 0xFFFFU) != 0x17CBU) {
        LogS("ABORT: Unexpected DBI Root Vendor ID!"); LogNL();
        MmUnmapIoSpace((PVOID)dbi, 0x2000);
        MmUnmapIoSpace((PVOID)phy, 0x2000);
        MmUnmapIoSpace((PVOID)parf, 0x3000);
        goto cleanup;
    }

    LogStep("STAGE 2D.3: Release PERST# (GPIO94 HIGH) & configure DBI (exact pci-msm.c / g289_rc0.c sequence)");
    mmio_rmw32(parf, 0x2C4, 1U, 0U);
    mmio_rmw32(parf, 0x2C4, 0U, (1U << 8));
    mmio_rmw32(dbi, 0x714, 0U, (1U << 5));

    SetTlmmGpio(tlmm, 94, 0x0201U, 1);
    SleepMs(2);

    /* Exact g289_rc0.c / pci-msm.c sequence: keep reset DBI+0x710 & DBI+0x7C, set DBI+0x80C NUM_OF_LANES=1 */
    mmio_rmw32(dbi, 0x80C, 0x1F00U, 0x0100U);

    uint32_t max_gen = root_link_cap & 0xFU;
    for (uint32_t gen = 3; gen <= max_gen && gen <= 4; gen++) {
        mmio_rmw32(dbi, 0x890, (3U << 24), (gen - 3U) << 24);
        mmio_rmw32(dbi, 0x8A8, 0xFFFF00U, 0U);
        mmio_rmw32(dbi, 0x890, 1U, 0U);
        mmio_rmw32(dbi, 0x8AC, 0x1FU, 0U);
    }
    mmio_rmw32(dbi, 0x8BC, 0U, 1U);
    if (max_gen >= 3) mmio_w32(dbi, 0x154, 0x55555555U);
    if (max_gen >= 4) mmio_w32(dbi, 0x188, 0x55555555U);
    mmio_rmw32(dbi, 0x8BC, 1U, 0U);
    mmio_rmw32(dbi, 0x0A0, 0xFU, 2U); /* s23_live.dts qcom,target-link-speed = 2 (trains Gen1 first then Gen2) */
    mmio_rmw32(dbi, 0x078, 0x7000U, 0x5000U);

    LogStep("STAGE 2D.4: Conditional FLUSH_MODE clear, enable LTSSM (PARF+0x1B0 |= 1<<8) & poll Link Up");
    if ((mmio_r32(parf, 0x1B0) & (1U << 11)) != 0) {
        LogS("  FLUSH_MODE(BIT11) set in PARF_LTSSM; pulsing SW_CLR_FLUSH_MODE(BIT10)..."); LogNL();
        mmio_rmw32(parf, 0x1B0, 0U, (1U << 10));
        (void)PollReg(parf, 0x1B0, (1U << 11), 0U, 5000);
        mmio_rmw32(parf, 0x1B0, (1U << 10), 0U);
    }
    mmio_rmw32(parf, 0x1B0, (1U << 10), (1U << 8)); /* LTSSM_EN cleanly with SW_CLR_FLUSH_MODE=0 */

    int link_up = 0;
    uint32_t elbi_st = 0, link_st = 0, ltssm_st = 0, dbg0 = 0, dbg1 = 0;
    uint64_t seen_states_mask = 0;
    /* High-frequency 50us sampling over first 30ms (600 polls) to catch any brief DETECT_ACT (0x01) / POLLING (0x02) transitions, then 1ms for 200ms */
    for (uint32_t p = 0; p < 800; p++) {
        if (p < 600) {
            KeStallExecutionProcessor(50);
        } else {
            SleepMs(1);
        }
        elbi_st  = mmio_r32(dbi, 0xF28);
        link_st  = mmio_r32(dbi, 0x080);
        ltssm_st = mmio_r32(parf, 0x1B0);
        dbg0     = mmio_r32(dbi, 0x728);
        dbg1     = mmio_r32(dbi, 0x72C);
        seen_states_mask |= (1ULL << (ltssm_st & 0x3FU));
        seen_states_mask |= (1ULL << ((elbi_st >> 12) & 0x3FU));
        seen_states_mask |= (1ULL << (dbg0 & 0x3FU));
        if ((elbi_st & 0x400U) && (link_st & (1U << 29))) {
            link_up = 1;
            LogS("  LINK UP (pass 1) at poll "); LogDec(p + 1); LogNL();
            break;
        }
    }
    LogS("  Link Status (pass 1): link_up="); LogDec(link_up);
    LogS(" seen_states=0x"); LogHex(seen_states_mask, 16);
    LogS(" LTSSM=0x"); LogHex(ltssm_st, 8); LogS(" (state=0x"); LogHex(ltssm_st & 0x3FU, 2);
    LogS(") ELBI+8=0x"); LogHex(elbi_st, 8);
    LogS(" DBI+0x80=0x"); LogHex(link_st, 8);
    LogS(" DBG0=0x"); LogHex(dbg0, 8);
    LogS(" DBG1=0x"); LogHex(dbg1, 8);
    LogNL();
    LogS("  PARF Debug: PM_STTS(0x24)=0x"); LogHex(mmio_r32(parf, 0x24), 8);
    LogS(" PM_STTS_1(0x28)=0x"); LogHex(mmio_r32(parf, 0x28), 8);
    LogS(" INT_ST(0x224)=0x"); LogHex(mmio_r32(parf, 0x224), 8);
    LogS(" STATUS(0x230)=0x"); LogHex(mmio_r32(parf, 0x230), 8);
    LogS(" CLKREQ_OVR(0x2B0)=0x"); LogHex(mmio_r32(parf, 0x2B0), 8);
    LogS(" CORE_ERR(0x3C0)=0x"); LogHex(mmio_r32(parf, 0x3C0), 8);
    LogNL();

    /* Sample GPIO95 physical pad after PERST# release */
    mmio_w32(tlmm, 95U * 0x1000U + 0x00, 0x0003U);
    StallUs(50);
    uint32_t g95_pad = mmio_r32(tlmm, 95U * 0x1000U + 0x04);
    mmio_w32(tlmm, 95U * 0x1000U + 0x00, 0x0007U);
    LogS("  After PERST# release: GPIO95(CLKREQ_PAD)_IO=0x"); LogHex(g95_pad, 2);
    LogS(" GPIO82(SW_CTRL)_IO=0x"); LogHex(mmio_r32(tlmm, 82U * 0x1000U + 0x04), 2);
    LogNL();

    if (!link_up) {
        LogStep("STAGE 2D.5: Fallback pass 2 (x1 PORT_LINK_MODE + TLS=1 + PERST# pulse + CLKREQ_OVR=0x02)");
        mmio_rmw32(parf, 0x1B0, (1U << 8) | (1U << 10), 0U);
        SetTlmmGpio(tlmm, 94, 0x0201U, 0);
        SleepMs(10);
        mmio_rmw32(dbi, 0x710, 0x3F0000U, 0x010000U);
        mmio_rmw32(dbi, 0x80C, 0x1F00U, 0x0100U);
        mmio_rmw32(dbi, 0x8BC, 0U, 1U);
        mmio_rmw32(dbi, 0x7C,  0x3F0U, 0x10U);
        mmio_rmw32(dbi, 0x8BC, 1U, 0U);
        mmio_rmw32(dbi, 0x0A0, 0xFU, 1U);
        mmio_w32(parf, 0x2B0, 0x02U);
        SetTlmmGpio(tlmm, 94, 0x0201U, 1);
        SleepMs(12);
        mmio_rmw32(parf, 0x1B0, (1U << 10), (1U << 8));

        uint64_t seen2 = 0;
        for (uint32_t p = 0; p < 600; p++) {
            if (p < 400) {
                KeStallExecutionProcessor(50);
            } else {
                SleepMs(1);
            }
            elbi_st  = mmio_r32(dbi, 0xF28);
            link_st  = mmio_r32(dbi, 0x080);
            ltssm_st = mmio_r32(parf, 0x1B0);
            dbg0     = mmio_r32(dbi, 0x728);
            dbg1     = mmio_r32(dbi, 0x72C);
            seen2 |= (1ULL << (ltssm_st & 0x3FU));
            seen2 |= (1ULL << ((elbi_st >> 12) & 0x3FU));
            seen2 |= (1ULL << (dbg0 & 0x3FU));
            if ((elbi_st & 0x400U) && (link_st & (1U << 29))) {
                link_up = 1;
                LogS("  LINK UP (pass 2) at poll "); LogDec(p + 1); LogNL();
                break;
            }
        }
        LogS("  Link Status (pass 2): link_up="); LogDec(link_up);
        LogS(" seen_states=0x"); LogHex(seen2, 16);
        LogS(" LTSSM=0x"); LogHex(ltssm_st, 8); LogS(" (state=0x"); LogHex(ltssm_st & 0x3FU, 2);
        LogS(") ELBI+8=0x"); LogHex(elbi_st, 8);
        LogS(" DBI+0x80=0x"); LogHex(link_st, 8);
        LogS(" DBG0=0x"); LogHex(dbg0, 8);
        LogS(" DBG1=0x"); LogHex(dbg1, 8);
        LogS(" PM_STTS(0x24)=0x"); LogHex(mmio_r32(parf, 0x24), 8);
        LogS(" CLKREQ_OVR(0x2B0)=0x"); LogHex(mmio_r32(parf, 0x2B0), 8);
        LogNL();
        if (!link_up) {
            LogS("  DBI Cfg: 0x710=0x"); LogHex(mmio_r32(dbi, 0x710), 8);
            LogS(" 0x80C=0x"); LogHex(mmio_r32(dbi, 0x80C), 8);
            LogS(" 0x7C=0x"); LogHex(mmio_r32(dbi, 0x7C), 8);
            LogS(" 0xA0=0x"); LogHex(mmio_r32(dbi, 0xA0), 8); LogNL();
            static const uint16_t phy_dbg_offs[] = {
                0x1CC, 0x1D0, 0x1D4, 0x1D8, 0x1DC, 0x1E0, 0x1E4, 0x1F8,
                0xED0, 0x16D0, 0xEDC, 0x16DC, 0x11E0, 0x19E0,
                0xA00, 0x1200, 0xA04, 0x1204, 0xA08, 0x1208, 0xA0C, 0x120C,
                0xA10, 0x1210, 0xA14, 0x1214, 0xA18, 0x1218, 0xC20, 0x1420,
                0x214, 0x218, 0x21C, 0x220, 0x224, 0x228, 0x22C, 0x230, 0x234, 0x238, 0x23C,
                0x600, 0x604
            };
            LogS("  Full S23 PHY Debug Regs:"); LogNL();
            for (uint32_t k = 0; k < sizeof(phy_dbg_offs) / sizeof(phy_dbg_offs[0]); k++) {
                if ((k & 7U) == 0) LogS("   ");
                LogS(" 0x"); LogHex(phy_dbg_offs[k], 3); LogS("=0x"); LogHex(mmio_r32(phy, phy_dbg_offs[k]), 2);
                if ((k & 7U) == 7U || k + 1 == sizeof(phy_dbg_offs) / sizeof(phy_dbg_offs[0])) LogNL();
            }
        }
    }

    if (link_up) {
        uint32_t ls = mmio_r32(dbi, 0x080);
        LogS("  Negotiated Link: Gen"); LogDec((ls >> 16) & 0xFU);
        LogS(" x"); LogDec((ls >> 20) & 0x3FU);
        LogS(" (DBI+0x80=0x"); LogHex(ls, 8); LogS(")"); LogNL();
    }

    if (!link_up || (stage_mask & 0x20U) == 0) {
        LogS("Stopping after Stage 2D."); LogNL();
        MmUnmapIoSpace((PVOID)dbi, 0x2000);
        MmUnmapIoSpace((PVOID)phy, 0x2000);
        MmUnmapIoSpace((PVOID)parf, 0x3000);
        goto cleanup;
    }

    /* ------------------------------------------------------------------------
     * STAGE 2E (0x20): Root Bus Numbers + iATU Outbound Slot 0 CFG0 & ECAM (0x60100000) Poll
     * ------------------------------------------------------------------------ */
    LogStep("STAGE 2E.1: Set Root Bus 0/1/1 (DBI+0x18) & Configure iATU Slot 0 (0x60001000) for CFG0 at 0x60100000");
    uint32_t rp_bus = (mmio_r32(dbi, 0x18) & 0xFF000000U) | 0x00010100U;
    mmio_w32(dbi, 0x18, rp_bus);
    mmio_w32(dbi, 0x1004, 0U);
    mmio_w32(dbi, 0x1000, 4U);
    mmio_w32(dbi, 0x1008, 0x60100000U);
    mmio_w32(dbi, 0x100C, 0U);
    mmio_w32(dbi, 0x1010, 0x60100FFFU);
    mmio_w32(dbi, 0x1014, 0x01000000U);
    mmio_w32(dbi, 0x1018, 0U);
    mmio_w32(dbi, 0x1004, 0x80000000U);

    uint32_t atu_en = mmio_r32(dbi, 0x1004);
    LogS("  iATU Slot 0 CTRL2 = 0x"); LogHex(atu_en, 8);
    LogS(" RP_BUS(0x18)=0x"); LogHex(mmio_r32(dbi, 0x18), 8); LogNL();
    if ((atu_en & 0x80000000U) != 0) {
        mmio_rmw32(dbi, 0x70C, 0U, (1U << 15));
        mmio_w32(dbi, 0xB40, 20U);

        LogStep("STAGE 2E.2: Map ECAM (0x60100000) and poll Endpoint Config Space (up to 1000 ms)");
        volatile uint8_t *ecam = (volatile uint8_t *)MapPhys(0x60100000ULL, 0x10000, "PCIE0_ECAM");
        if (ecam) {
            uint32_t ep_id = 0xFFFFFFFFU;
            uint32_t ep_polls = 0;
            for (; ep_polls < 1000; ep_polls++) {
                ep_id = mmio_r32(ecam, 0x00);
                if (ep_id != 0U && ep_id != 0xFFFFFFFFU) break;
                SleepMs(1);
            }
            LogS("  [0x00] Vendor/Device ID = 0x"); LogHex(ep_id, 8);
            LogS(" (polls="); LogDec(ep_polls < 1000U ? ep_polls + 1U : 1000U); LogS(")"); LogNL();
            if (ep_id != 0U && ep_id != 0xFFFFFFFFU) {
                LogS("  [0x04] Command/Status   = 0x"); LogHex(mmio_r32(ecam, 0x04), 8); LogNL();
                LogS("  [0x08] Class/Revision   = 0x"); LogHex(mmio_r32(ecam, 0x08), 8); LogNL();
                LogS("  [0x10] BAR0             = 0x"); LogHex(mmio_r32(ecam, 0x10), 8); LogNL();
                LogS("  [0x14] BAR1             = 0x"); LogHex(mmio_r32(ecam, 0x14), 8); LogNL();
                LogS("  [0x2C] Subsystem ID     = 0x"); LogHex(mmio_r32(ecam, 0x2C), 8); LogNL();
            } else {
                LogS("  ECAM endpoint did not respond within 1000 ms (skipping 0x04..0x2C reads)"); LogNL();
            }
            MmUnmapIoSpace((PVOID)ecam, 0x10000);
        }
    }

    /* ------------------------------------------------------------------------
     * STAGE 2F (0x40): G401 3-Slot iATU Bus-1 ECAM Isolation + PARF SID + BAR0 (0x60300000) MHI/BHI Probe
     * ------------------------------------------------------------------------ */
    if ((stage_mask & 0x40U) != 0) {
        LogStep("STAGE 2F.1: Configure G401 3-Slot iATU Bus-1 ECAM Transport & Hidden Root Port");
        mmio_rmw32(dbi, 0x18, 0x00FFFFFFU, 0x00010100U);
        mmio_w32(dbi, 0x20, 0x63F06030U);
        mmio_rmw32(dbi, 0x04, 0xFFFF0000U, 0x00000006U); /* MSE | BME on RP */
        mmio_rmw32(dbi, 0x8BC, 0U, 1U);
        mmio_rmw32(dbi, 0x03C, 0x0000FF00U, 0x00000100U); /* Interrupt Pin = 1 (INTA) */
        mmio_rmw32(dbi, 0x8BC, 1U, 0U);

        /* Slot 0 (+0x1000): CFG0 + CFG_SHIFT for Bus 1 Dev 0 Fn 0..7 (0x60100000..0x60107FFF) */
        mmio_w32(dbi, 0x1004, 0U);
        mmio_w32(dbi, 0x1000, 4U);
        mmio_w32(dbi, 0x1008, 0x60100000U);
        mmio_w32(dbi, 0x100C, 0U);
        mmio_w32(dbi, 0x1010, 0x60107FFFU);
        mmio_w32(dbi, 0x1014, 0U);
        mmio_w32(dbi, 0x1018, 0U);
        mmio_w32(dbi, 0x1004, (1U << 31) | (1U << 28));

        /* Slot 1 (+0x1200): CFG1 absent-bus sink for Dev 1..31 (0x60108000..0x601FFFFF -> Bus 0xFF) */
        mmio_w32(dbi, 0x1204, 0U);
        mmio_w32(dbi, 0x1200, 5U);
        mmio_w32(dbi, 0x1208, 0x60108000U);
        mmio_w32(dbi, 0x120C, 0U);
        mmio_w32(dbi, 0x1210, 0x601FFFFFU);
        mmio_w32(dbi, 0x1214, 0xFF000000U);
        mmio_w32(dbi, 0x1218, 0U);
        mmio_w32(dbi, 0x1204, (1U << 31));

        /* Slot 2 (+0x1400): Identity MEM32 window (0x60300000..0x63FFFFFF -> PCI 0x60300000) */
        mmio_w32(dbi, 0x1404, 0U);
        mmio_w32(dbi, 0x1400, 0U);
        mmio_w32(dbi, 0x1408, 0x60300000U);
        mmio_w32(dbi, 0x140C, 0U);
        mmio_w32(dbi, 0x1410, 0x63FFFFFFU);
        mmio_w32(dbi, 0x1414, 0x60300000U);
        mmio_w32(dbi, 0x1418, 0U);
        mmio_w32(dbi, 0x1404, (1U << 31));

        LogS("  iATU CTRL2: S0=0x"); LogHex(mmio_r32(dbi, 0x1004), 8);
        LogS(" S1=0x"); LogHex(mmio_r32(dbi, 0x1204), 8);
        LogS(" S2=0x"); LogHex(mmio_r32(dbi, 0x1404), 8); LogNL();

        /* PARF BDF-to-SID CRC8 table (0x01C02000..0x01C023FC, RID 0x0100 -> CRC8=23 -> 0x01000100) */
        for (uint32_t idx = 0; idx < 256U; idx++) {
            mmio_w32(parf, 0x2000U + 4U * idx, 0U);
        }
        uint8_t crc = 0;
        uint16_t bdf = 0x0100U;
        for (int b = 15; b >= 0; b--) {
            uint8_t bit = (uint8_t)(((bdf >> b) & 1U) ^ ((crc >> 7) & 1U));
            crc = (uint8_t)((crc << 1) ^ (bit ? 0x07U : 0x00U));
        }
        mmio_w32(parf, 0x2000U + 4U * (uint32_t)crc, 0x01000100U);
        mmio_rmw32(parf, 0x2C00U, 1U, 0U);
        LogS("  PARF BDF_TO_SID["); LogDec(crc); LogS("]=0x"); LogHex(mmio_r32(parf, 0x2000U + 4U * (uint32_t)crc), 8);
        LogS(" CFG(0x2C00)=0x"); LogHex(mmio_r32(parf, 0x2C00U), 8); LogNL();

        /* Map full 1 MB Bus-1 ECAM (0x60100000..0x601FFFFF) and verify Df=0..255 isolation + BAR0 */
        volatile uint8_t *ecam1m = (volatile uint8_t *)MapPhys(0x60100000ULL, 0x100000, "PCIE0_ECAM_1M");
        if (ecam1m) {
            uint32_t df0_id = mmio_r32(ecam1m, 0x00);
            uint32_t bad_df = 0;
            for (uint32_t df = 1U; df < 256U; df++) {
                if (mmio_r32(ecam1m, df << 12) != 0xFFFFFFFFU) {
                    bad_df++;
                }
            }
            LogS("  Bus-1 ECAM Df=0..255 scan: Df0=0x"); LogHex(df0_id, 8);
            LogS(" non_ff_ghosts="); LogDec(bad_df); LogNL();

            /* Size BAR0 (2 MB aligned: mask_lo=0xFFE00004) and assign 0x60400000 temporarily to read QCA6490 MHI/BHI registers */
            uint32_t orig_cmd  = mmio_r32(ecam1m, 0x04);
            uint32_t orig_bar0 = mmio_r32(ecam1m, 0x10);
            uint32_t orig_bar1 = mmio_r32(ecam1m, 0x14);
            mmio_w32(ecam1m, 0x04, orig_cmd & ~0x7U);
            mmio_w32(ecam1m, 0x10, 0xFFFFFFFFU);
            mmio_w32(ecam1m, 0x14, 0xFFFFFFFFU);
            uint32_t sz_lo = mmio_r32(ecam1m, 0x10);
            uint32_t sz_hi = mmio_r32(ecam1m, 0x14);
            mmio_w32(ecam1m, 0x10, 0x60400004U);
            mmio_w32(ecam1m, 0x14, 0x00000000U);
            mmio_w32(ecam1m, 0x04, (orig_cmd & 0xFFFFU) | 0x0002U); /* MSE = 1, BME = 0 */

            LogS("  BAR0 sizing: mask_lo=0x"); LogHex(sz_lo, 8);
            LogS(" mask_hi=0x"); LogHex(sz_hi, 8);
            LogS(" assigned=0x"); LogHex(mmio_r32(ecam1m, 0x10), 8); LogNL();

            volatile uint8_t *bar0 = (volatile uint8_t *)MapPhys(0x60400000ULL, 0x10000, "QCA6490_BAR0");
            if (bar0) {
                /* Standard Qualcomm MHI / BHI registers inside BAR0:
                 * MHI_REGLEN=0x100, MHI_VER=0x108, MHI_CFG=0x110, MHI_CTRL=0x138, MHI_STATUS=0x148,
                 * BHI_OFFSET=0x124 (BHI_VERSION=+0x00, BHI_IMGTXDB=+0x18, BHI_STATUS=+0x28, BHI_EE=+0x30, BHI_ERRCODE=+0x38) */
                LogS("  QCA6490 BAR0 MHI/BHI Registers:"); LogNL();
                LogS("    [0x0000] PCIE_SOC_ID = 0x"); LogHex(mmio_r32(bar0, 0x0000), 8);
                LogS(" [0x0100] MHI_REGLEN = 0x"); LogHex(mmio_r32(bar0, 0x0100), 8);
                LogS(" [0x0108] MHI_VER = 0x"); LogHex(mmio_r32(bar0, 0x0108), 8);
                LogS(" [0x0124] BHI_OFF = 0x"); LogHex(mmio_r32(bar0, 0x0124), 8);
                LogS(" [0x0148] MHI_ST  = 0x"); LogHex(mmio_r32(bar0, 0x0148), 8); LogNL();
                uint32_t bhi_off = mmio_r32(bar0, 0x0124);
                if (bhi_off >= 0x100U && bhi_off <= 0x8000U && (bhi_off & 3U) == 0U) {
                    LogS("    [BHI+0x00] BHI_VER = 0x"); LogHex(mmio_r32(bar0, bhi_off + 0x00), 8);
                    LogS(" [BHI+0x28] STATUS = 0x"); LogHex(mmio_r32(bar0, bhi_off + 0x28), 8);
                    LogS(" [BHI+0x30] BHI_EE = 0x"); LogHex(mmio_r32(bar0, bhi_off + 0x30), 8);
                    LogS(" [BHI+0x38] ERRCOD = 0x"); LogHex(mmio_r32(bar0, bhi_off + 0x38), 8);
                    LogS(" [BHI+0x40] SERNUM = 0x"); LogHex(mmio_r32(bar0, bhi_off + 0x40), 8);
                    LogNL();
                }
                MmUnmapIoSpace((PVOID)bar0, 0x10000);
            }

            /* Restore BAR0 and Command register */
            mmio_w32(ecam1m, 0x04, orig_cmd & ~0x7U);
            mmio_w32(ecam1m, 0x10, orig_bar0);
            mmio_w32(ecam1m, 0x14, orig_bar1);
            mmio_w32(ecam1m, 0x04, orig_cmd & 0xFFFFU);
            MmUnmapIoSpace((PVOID)ecam1m, 0x100000);
        }
    }

    /* ------------------------------------------------------------------------
     * STAGE 2G (0x80): APPS_SMMU (0x15000000) Geometry & Stage-1 Identity Pass-Through for SID 0x1400/0x1401
     * ------------------------------------------------------------------------ */
    if ((stage_mask & 0x80U) != 0) {
        LogStep("STAGE 2G.1: Map APPS_SMMU (0x15000000, 0x100000) & Read Geometry (CR0, ID0, ID1, ID2, GFSR)");
        volatile uint8_t *smmu = (volatile uint8_t *)MapPhys(0x15000000ULL, 0x100000, "APPS_SMMU");
        if (smmu) {
            uint32_t cr0  = mmio_r32(smmu, 0x00);
            uint32_t id0  = mmio_r32(smmu, 0x20);
            uint32_t id1  = mmio_r32(smmu, 0x24);
            uint32_t id2  = mmio_r32(smmu, 0x28);
            uint32_t gfsr = mmio_r32(smmu, 0x48);
            uint32_t num_smr = id0 & 0xFFU;
            uint32_t num_cb  = id1 & 0xFFU;
            uint32_t pg_sz   = (id1 & (1U << 31)) ? 0x10000U : 0x1000U;
            uint32_t pages   = 1U << (((id1 >> 28) & 7U) + 1U);
            uint32_t cb_base_off = pages * pg_sz;

            LogS("  SMMU CR0=0x"); LogHex(cr0, 8);
            LogS(" ID0=0x"); LogHex(id0, 8);
            LogS(" ID1=0x"); LogHex(id1, 8);
            LogS(" ID2=0x"); LogHex(id2, 8);
            LogS(" GFSR=0x"); LogHex(gfsr, 8); LogNL();
            LogS("  SMMU Geometry: NumSmr="); LogDec(num_smr);
            LogS(" NumCb="); LogDec(num_cb);
            LogS(" PgSz=0x"); LogHex(pg_sz, 5);
            LogS(" Pages="); LogDec(pages);
            LogS(" CbBaseOff=0x"); LogHex(cb_base_off, 6); LogNL();

            if (num_smr > 0U && num_smr <= 128U && num_cb >= 4U && cb_base_off + num_cb * pg_sz <= 0x100000U) {
                uint32_t active_smrs = 0, free_slots[2] = { 0xFFFFFFFFU, 0xFFFFFFFFU }, ns = 0;
                uint32_t existing_1400 = 0xFFFFFFFFU, existing_1401 = 0xFFFFFFFFU;
                uint8_t used_cb[256];
                for (uint32_t i = 0; i < 256U; i++) used_cb[i] = 0;

                for (uint32_t n = 0; n < num_smr; n++) {
                    uint32_t smr  = mmio_r32(smmu, 0x800U + 4U * n);
                    uint32_t s2cr = mmio_r32(smmu, 0xC00U + 4U * n);
                    if (smr != 0U) {
                        active_smrs++;
                        LogS("    SMR["); LogDec(n); LogS("]=0x"); LogHex(smr, 8);
                        LogS(" S2CR=0x"); LogHex(s2cr, 8); LogNL();
                        if ((smr & 0x80007FFFU) == 0x80001400U) existing_1400 = n;
                        if ((smr & 0x80007FFFU) == 0x80001401U) existing_1401 = n;
                    }
                    if (((s2cr >> 16) & 3U) == 0U && (s2cr & 0xFFU) < num_cb) {
                        used_cb[s2cr & 0xFFU] = 1;
                    }
                    if (smr == 0U && s2cr == 0x00020000U && ns < 2U) {
                        free_slots[ns++] = n;
                    }
                }

                uint32_t free_cbs[2] = { 0xFFFFFFFFU, 0xFFFFFFFFU }, nb = 0;
                for (uint32_t cb = 1U; cb < num_cb && nb < 2U; cb++) {
                    if (used_cb[cb]) continue;
                    uint32_t cba   = cb_base_off + cb * pg_sz;
                    uint32_t sctlr = mmio_r32(smmu, cba + 0x00U);
                    uint32_t actlr = mmio_r32(smmu, cba + 0x04U);
                    uint32_t fsr   = mmio_r32(smmu, cba + 0x58U);
                    uint32_t cbar  = mmio_r32(smmu, pg_sz + 4U * cb);
                    uint32_t cba2r = mmio_r32(smmu, pg_sz + 0x800U + 4U * cb);
                    if ((sctlr & 0x61U) == 0U && actlr == 3U && fsr == 0U && cbar == 0U && cba2r == 0U) {
                        free_cbs[nb++] = cb;
                        LogS("    Free CB["); LogDec(cb); LogS("]: sctlr=0x"); LogHex(sctlr, 8);
                        LogS(" actlr=0x"); LogHex(actlr, 8); LogNL();
                    }
                }
                LogS("  SMMU Scan: active_smrs="); LogDec(active_smrs);
                LogS(" free_slots=("); LogDec(free_slots[0]); LogS(","); LogDec(free_slots[1]);
                LogS(") free_cbs=("); LogDec(free_cbs[0]); LogS(","); LogDec(free_cbs[1]);
                LogS(") existing_1400="); LogDec(existing_1400);
                LogS(" existing_1401="); LogDec(existing_1401); LogNL();

                /* Program Stage-1 Coherent Identity Pass-Through for SID 0x1401 & 0x1400 if not yet present */
                if ((cr0 & 1U) == 0U && gfsr == 0U && existing_1400 == 0xFFFFFFFFU && existing_1401 == 0xFFFFFFFFU && ns == 2U && nb == 2U) {
                    static const uint32_t target_sids[2] = { 0x1401U, 0x1400U };
                    for (uint32_t k = 0; k < 2U; k++) {
                        uint32_t slot = free_slots[k];
                        uint32_t cb   = free_cbs[k];
                        uint32_t cba  = cb_base_off + cb * pg_sz;
                        mmio_w32(smmu, cba + 0x00U, 0U);
                        mmio_w32(smmu, pg_sz + 0x800U + 4U * cb, 1U);         /* CBA2R = VA64 */
                        mmio_w32(smmu, pg_sz + 4U * cb, 0x0001F300U);         /* CBAR */
                        mmio_w32(smmu, cba + 0x10U, 0x00000060U);             /* TCR2 = TBI1|TBI0 */
                        mmio_w32(smmu, cba + 0x20U, 0U); mmio_w32(smmu, cba + 0x24U, 0U);
                        mmio_w32(smmu, cba + 0x28U, 0U); mmio_w32(smmu, cba + 0x2CU, 0U);
                        mmio_w32(smmu, cba + 0x30U, 0x80A000A0U);             /* TCR */
                        mmio_w32(smmu, cba + 0x34U, 0U); mmio_w32(smmu, cba + 0x38U, 0U); mmio_w32(smmu, cba + 0x3CU, 0U);
                        mmio_w32(smmu, cba + 0x00U, 0x0A5F00E0U);             /* SCTLR (M=0 coherent identity) */
                        mmio_w32(smmu, 0xC00U + 4U * slot, cb);               /* S2CR = TRANSLATION -> cb */
                        mmio_w32(smmu, 0x800U + 4U * slot, 0x80000000U | target_sids[k]);
                    }
                    mmio_w32(smmu, 0x70U, 0U);
                    int sync_ok = PollReg(smmu, 0x74U, 1U, 0U, 100000U);
                    uint32_t gfsr_after = mmio_r32(smmu, 0x48U);
                    LogS("  SMMU Identity Programmed: sync_ok="); LogDec(sync_ok);
                    LogS(" GFSR_after=0x"); LogHex(gfsr_after, 8);
                    LogS(" SMR["); LogDec(free_slots[0]); LogS("]=0x"); LogHex(mmio_r32(smmu, 0x800U + 4U * free_slots[0]), 8);
                    LogS(" SMR["); LogDec(free_slots[1]); LogS("]=0x"); LogHex(mmio_r32(smmu, 0x800U + 4U * free_slots[1]), 8);
                    LogNL();
                }
            }
            MmUnmapIoSpace((PVOID)smmu, 0x100000);
        }
    }

    MmUnmapIoSpace((PVOID)dbi, 0x2000);
    MmUnmapIoSpace((PVOID)phy, 0x2000);
    MmUnmapIoSpace((PVOID)parf, 0x3000);

cleanup:
    if (g_ipcc)        { MmUnmapIoSpace((PVOID)g_ipcc, 0x001000); g_ipcc = NULL; }
    if (g_qmp)         { MmUnmapIoSpace((PVOID)g_qmp, 0x001000); g_qmp = NULL; }
    if (g_spmi_chnls)  { MmUnmapIoSpace((PVOID)g_spmi_chnls, 0x400000); g_spmi_chnls = NULL; }
    if (g_spmi_obsrvr) { MmUnmapIoSpace((PVOID)g_spmi_obsrvr, 0x080000); g_spmi_obsrvr = NULL; }
    if (g_spmi_cnfg)   { MmUnmapIoSpace((PVOID)g_spmi_cnfg, 0x004000); g_spmi_cnfg = NULL; }
    if (g_spmi_core)   { MmUnmapIoSpace((PVOID)g_spmi_core, 0x004000); g_spmi_core = NULL; }
    if (g_rsc)         { MmUnmapIoSpace((PVOID)g_rsc, 0x002000); g_rsc = NULL; }
    if (g_cmddb)       { MmUnmapIoSpace((PVOID)g_cmddb, 0x020000); g_cmddb = NULL; }
    if (tcsr)          MmUnmapIoSpace((PVOID)tcsr, 0x001000);
    if (gcc)           MmUnmapIoSpace((PVOID)gcc, 0x1F5000);
    if (tlmm)          MmUnmapIoSpace((PVOID)tlmm, 0x100000);

    LogS("=== s23pcieprobe.sys v1.0.0.13 finished ==="); LogNL();
    LogClose();
    return STATUS_UNSUCCESSFUL;
}
