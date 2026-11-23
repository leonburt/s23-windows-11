/*
 * s23pilprobe.c - Samsung Galaxy S23 (dm1q / SM8550-AC) SMEM v12, CMD_DB,
 *                 IMEM PIL Info, GCC & PIL Carveout Read-Only Probe Driver v1.0.0.2
 *
 * Pure-C ARM64 WDM Demand-Start (Start = 3) one-shot driver (returns STATUS_UNSUCCESSFUL
 * after closing log so Windows auto-unloads cleanly). Writes to pre-allocated 64 KB
 * write-through log:
 *   C:\Users\Public\s23pilprobe.log
 *
 * Strictly Read-Only by default (StageMask = 0x01): ZERO MMIO writes, ZERO unclocked
 * subsystem MMIO accesses, and granular Gunyah/XPU isolation across stages.
 *
 * Registry Parameters (HKLM\SYSTEM\CurrentControlSet\Services\s23pilprobe\Parameters):
 *   StageMask (REG_DWORD, default 0x01):
 *     bit 0 (0x01): CMD_DB (0x81C60000) + safe verified GCC clock registers
 *     bit 1 (0x02): IMEM PIL ImageLoadInfo (0x146AA94C, 200 B = 10x20 B)
 *     bit 2 (0x04): SMEM v12 (0x81D00000, APPS/GLOBAL partitions only unless bit 5 set)
 *     bit 3 (0x08): PIL DDR first-4KB header probe gated by RegionMask (default 0)
 *     bit 4 (0x10): Extended GCC branch CBCRs (0x52000, 0x38004 BOOT_ROM, CAM/VID/UFS)
 *     bit 5 (0x20): Walk non-APPS SMEM partitions inside ProbeSmem
 *   RegionMask (REG_DWORD, default 0x00000000): bitmask into g_pil_regions[] for Stage 0x08
 *   AllowUnsafe (REG_DWORD, default 0x00): allow XPU/SYS_MEM/IMEM-active PIL DDR regions
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
#define FILE_OVERWRITE_IF            0x00000005UL
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
 * Physical Map & Protocol Constants (Verified against s23_live.dts + msm-kernel)
 * ============================================================================ */
#define PA_GCC_BASE             0x00100000ULL
#define SZ_GCC                  0x001F5000U
#define PA_IMEM_COOKIE_BASE     0x146AA000ULL
#define SZ_IMEM_COOKIE          0x00001000U
#define OFF_IMEM_PIL_INFO       0x0000094CU
#define IMEM_PIL_INFO_SLOTS     10U
#define IMEM_PIL_ENTRY_SIZE     20U

#define PA_CMD_DB_BASE          0x81C60000ULL
#define SZ_CMD_DB               0x00020000U
#define PA_SMEM_BASE            0x81D00000ULL
#define SZ_SMEM                 0x00200000U
#define SZ_SMEM_PTABLE_TAIL     0x00001000U

#define SMEM_ITEM_COUNT_MAX     512U
#define SMEM_MASTER_SBL_VER_IDX 7U
#define SMEM_GLOBAL_HOST        0xFFFEU
#define SMEM_PRIVATE_CANARY     0xA5A5U

#define SMEM_ITEM_SOCINFO       137U
#define SMEM_ITEM_IMAGE_VERS    469U
#define SMEM_ITEM_GLINK_DESC    478U
#define SMEM_ITEM_GLINK_FIFO0   479U
#define SMEM_ITEM_GLINK_FIFO1   480U

#define SMP2P_MAGIC             0x504D5324U  /* "$SMP" */
#define SMP2P_MAX_ENTRY         16U

/* Forced .reloc anchor so lld-link always emits .reloc */
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
static void * const g_RelocAnchor = (void *)(uintptr_t)&DriverEntry;
static volatile uintptr_t g_RelocSink;

/* ============================================================================
 * Alignment-Safe Non-Cached Memory Accessors & Helpers
 * ============================================================================ */
static inline void dmb_sy(void)
{
    __asm__ volatile("dmb sy" ::: "memory");
}

static inline uint16_t rd16(const volatile uint8_t *base, uint32_t off)
{
    if ((off & 1U) == 0U) {
        uint16_t v = *(const volatile uint16_t *)(base + off);
        dmb_sy();
        return v;
    }
    uint16_t lo = base[off];
    uint16_t hi = base[off + 1U];
    dmb_sy();
    return (uint16_t)(lo | (hi << 8));
}

static inline uint32_t rd32(const volatile uint8_t *base, uint32_t off)
{
    if ((off & 3U) == 0U) {
        dmb_sy();
        uint32_t v = *(const volatile uint32_t *)(base + off);
        dmb_sy();
        return v;
    }
    uint32_t b0 = base[off];
    uint32_t b1 = base[off + 1U];
    uint32_t b2 = base[off + 2U];
    uint32_t b3 = base[off + 3U];
    dmb_sy();
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

static void copy_from_io(uint8_t *dst, const volatile uint8_t *src, uint32_t len)
{
    for (uint32_t i = 0U; i < len; i++) {
        dst[i] = src[i];
    }
    dmb_sy();
}

static inline uint32_t align_up(uint32_t v, uint32_t a)
{
    if (a <= 1U) return v;
    return (v + (a - 1U)) & ~(a - 1U);
}

static int str_eq_ci_prefix(const char *s, const char *pfx)
{
    while (*pfx) {
        char a = *s++;
        char b = *pfx++;
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return 0;
    }
    return 1;
}

static int str_contains_ci(const char *hay, const char *ndl)
{
    for (uint32_t i = 0U; hay[i] != '\0'; i++) {
        if (str_eq_ci_prefix(hay + i, ndl)) return 1;
    }
    return 0;
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

/* ============================================================================
 * Pre-Allocated 64 KB Write-Through Logger (C:\Users\Public\s23pilprobe.log)
 * ============================================================================ */
static HANDLE   g_log_handle = NULL;
static int64_t  g_log_off    = 0;
static char     g_log_buf[4096];
static uint32_t g_log_pos    = 0;

static void LogOpen(void)
{
    UNICODE_STRING u;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    RtlInitUnicodeString(&u, L"\\??\\C:\\Users\\Public\\s23pilprobe.log");
    InitObjAttr(&oa, &u, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE);
    g_log_off = 0;
    g_log_pos = 0;
    if (!NT_SUCCESS(ZwCreateFile(
            &g_log_handle, FILE_GENERIC_WRITE, &oa, &iosb, NULL, FILE_ATTRIBUTE_NORMAL,
            FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OVERWRITE_IF,
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

static void LogSanitizedStr(const uint8_t *buf, uint32_t max_len)
{
    for (uint32_t i = 0U; i < max_len; i++) {
        uint8_t c = buf[i];
        if (c == 0U) break;
        if (c >= 0x20U && c <= 0x7EU) LogC((char)c);
        else LogC('.');
    }
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

static void LogHexBytes(const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0U; i < n; i++) {
        if (i > 0U) LogC(' ');
        LogHex(b[i], 2);
    }
}

static uint32_t ReadRegDword(const WCHAR *val_name, uint32_t def_val)
{
    UNICODE_STRING uk, uv;
    OBJECT_ATTRIBUTES oa;
    HANDLE hk = NULL;
    uint32_t val = def_val;

    RtlInitUnicodeString(&uk, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\s23pilprobe\\Parameters");
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
 * 1. IMEM PIL ImageLoadInfo Parser (0x146AA94C, 10 x 20 B)
 * ============================================================================ */
typedef struct {
    char     name[9];
    uint64_t base;
    uint32_t size;
} IMEM_PIL_SLOT;

static IMEM_PIL_SLOT g_imem_pil[IMEM_PIL_INFO_SLOTS];
static uint32_t      g_imem_pil_cnt = 0U;

static void ProbeImemPilInfo(void)
{
    LogS("=== [1/5] IMEM PIL ImageLoadInfo (0x146AA94C, 200 B) ==="); LogNL();
    PHYSICAL_ADDRESS pa;
    pa.QuadPart = (int64_t)PA_IMEM_COOKIE_BASE;
    volatile uint8_t *imem = (volatile uint8_t *)MmMapIoSpace(pa, SZ_IMEM_COOKIE, MmNonCached);
    if (!imem) {
        LogS("  [ERR] MmMapIoSpace(IMEM 0x146AA000) failed"); LogNL();
        return;
    }
    g_imem_pil_cnt = 0U;
    for (uint32_t i = 0U; i < IMEM_PIL_INFO_SLOTS; i++) {
        uint32_t off = OFF_IMEM_PIL_INFO + i * IMEM_PIL_ENTRY_SIZE;
        uint8_t name[8];
        copy_from_io(name, imem + off, 8U);
        uint32_t blo = rd32(imem, off + 8U);
        uint32_t bhi = rd32(imem, off + 12U);
        uint32_t sz  = rd32(imem, off + 16U);
        uint64_t base = ((uint64_t)bhi << 32) | (uint64_t)blo;
        if (name[0] != 0U || base != 0ULL || sz != 0U) {
            IMEM_PIL_SLOT *sl = &g_imem_pil[g_imem_pil_cnt++];
            for (uint32_t k = 0U; k < 8U; k++) sl->name[k] = (char)name[k];
            sl->name[8] = '\0';
            sl->base = base;
            sl->size = sz;
            LogS("  Slot["); LogDec(i); LogS("]: name='");
            LogSanitizedStr(name, 8U);
            LogS("' base=0x"); LogHex(base, 16);
            LogS(" size=0x"); LogHex(sz, 8); LogNL();
        }
    }
    if (g_imem_pil_cnt == 0U) {
        LogS("  (no pre-boot PIL entries recorded in IMEM 0x146AA94C)"); LogNL();
    }
    MmUnmapIoSpace((PVOID)imem, SZ_IMEM_COOKIE);
}

static int IsRegionInImemPil(uint64_t pa)
{
    for (uint32_t i = 0U; i < g_imem_pil_cnt; i++) {
        uint64_t b = g_imem_pil[i].base;
        uint64_t e = b + (uint64_t)g_imem_pil[i].size;
        if (g_imem_pil[i].size > 0U && pa >= b && pa < e) {
            return 1;
        }
    }
    return 0;
}

/* ============================================================================
 * 2. Qualcomm SMEM v12 Parser (0x81D00000, 2 MiB)
 * ============================================================================ */
static const char *SmemHostName(uint16_t h)
{
    switch (h) {
        case 0U:      return "APPS";
        case 1U:      return "MPSS";
        case 2U:      return "ADSP";
        case 3U:      return "SNS";
        case 4U:      return "WCN";
        case 5U:      return "CDSP";
        case 6U:      return "TZ";
        case 7U:      return "RPM";
        case 8U:      return "SPSS";
        case 9U:      return "HYP";
        case 10U:     return "NPU";
        case 12U:     return "AOP";
        case 13U:     return "DEBUG";
        case 15U:     return "WPSS";
        case 0xFFFEU: return "GLOBAL";
        default:      return "HOST";
    }
}

static const char *SocinfoPlatName(uint32_t p)
{
    switch (p) {
        case 1U:  return "Surf";
        case 8U:  return "MTP";
        case 11U: return "QRD";
        case 21U: return "RCM";
        case 31U: return "HDK";
        case 34U: return "IDP";
        default:  return "OEM/Other";
    }
}

static const char *SocinfoFcName(uint32_t fc)
{
    switch (fc) {
        case 1U: return "AA";
        case 2U: return "AB";
        case 3U: return "AC";
        case 4U: return "AD";
        case 5U: return "AE";
        case 6U: return "AF";
        case 7U: return "AG";
        case 8U: return "AH";
        default: return "?";
    }
}

static const char *ImageTableSlotName(uint32_t idx)
{
    switch (idx) {
        case 0U:  return "BOOT/XBL";
        case 1U:  return "TZ";
        case 2U:  return "TZ_DEV";
        case 3U:  return "RPM/AOP";
        case 4U:  return "HYP";
        case 9U:  return "UEFI";
        case 10U: return "APPS";
        case 11U: return "MPSS";
        case 12U: return "ADSP";
        case 13U: return "CNSS";
        case 14U: return "VIDEO";
        case 15U: return "CDSP";
        case 16U: return "SLPI";
        case 17U: return "SPSS";
        default:  return "IMG";
    }
}

static void DecodeSocinfo(const volatile uint8_t *base, uint32_t off, uint32_t sz)
{
    LogS("  --- SMEM Item 137: SOCINFO (off=0x"); LogHex(off, 6);
    LogS(" sz="); LogDec(sz); LogS(") ---"); LogNL();
    if (sz < 44U || off + sz > SZ_SMEM) {
        LogS("    [WARN] SOCINFO truncated"); LogNL();
        return;
    }
    uint32_t fmt = rd32(base, off + 0x00U);
    uint32_t id  = rd32(base, off + 0x04U);
    uint32_t ver = rd32(base, off + 0x08U);
    uint8_t build_id[32];
    copy_from_io(build_id, base + off + 0x0CU, 32U);

    LogS("    fmt="); LogDec((fmt >> 16) & 0xFFFFU); LogC('.'); LogDec(fmt & 0xFFFFU);
    LogS(" soc_id="); LogDec(id);
    LogS(" ver="); LogDec((ver >> 16) & 0xFFFFU); LogC('.'); LogDec(ver & 0xFFFFU);
    LogS(" build_id='"); LogSanitizedStr(build_id, 32U); LogS("'"); LogNL();

    if (sz >= 0x44U) {
        uint32_t raw_id  = rd32(base, off + 0x2CU);
        uint32_t raw_ver = rd32(base, off + 0x30U);
        uint32_t hw_plat = rd32(base, off + 0x34U);
        uint32_t plat_v  = rd32(base, off + 0x38U);
        uint32_t subtype = rd32(base, off + 0x40U);
        LogS("    raw_id=0x"); LogHex(raw_id, 8);
        LogS(" raw_ver=0x"); LogHex(raw_ver, 8);
        LogS(" hw_plat="); LogDec(hw_plat); LogS("("); LogS(SocinfoPlatName(hw_plat)); LogS(")");
        LogS(" plat_ver=0x"); LogHex(plat_v, 8);
        LogS(" subtype="); LogDec(subtype); LogNL();
    }
    if (sz >= 0x64U) {
        uint32_t pm0  = rd32(base, off + 0x44U);
        uint32_t pr0  = rd32(base, off + 0x48U);
        uint32_t pm1  = rd32(base, off + 0x4CU);
        uint32_t pr1  = rd32(base, off + 0x50U);
        uint32_t pm2  = rd32(base, off + 0x54U);
        uint32_t pr2  = rd32(base, off + 0x58U);
        uint32_t fnd  = rd32(base, off + 0x5CU);
        uint32_t ser  = rd32(base, off + 0x60U);
        LogS("    foundry="); LogDec(fnd);
        LogS(" serial=0x"); LogHex(ser, 8);
        LogS(" pmic0="); LogDec(pm0 & 0xFFFFU); LogS("(rev 0x"); LogHex(pr0, 4); LogS(")");
        LogS(" pmic1="); LogDec(pm1 & 0xFFFFU); LogS("(rev 0x"); LogHex(pr1, 4); LogS(")");
        LogS(" pmic2="); LogDec(pm2 & 0xFFFFU); LogS("(rev 0x"); LogHex(pr2, 4); LogS(")"); LogNL();
    }
    if (sz >= 0x9CU) {
        uint32_t num_pm = rd32(base, off + 0x64U);
        uint32_t fam    = rd32(base, off + 0x6CU);
        uint32_t nprod  = rd32(base, off + 0x78U);
        uint8_t chip_id[32];
        copy_from_io(chip_id, base + off + 0x7CU, 32U);
        LogS("    num_pmics="); LogDec(num_pm);
        LogS(" chip_family=0x"); LogHex(fam, 4);
        LogS(" nproduct_id=0x"); LogHex(nprod, 8);
        LogS(" chip_id='"); LogSanitizedStr(chip_id, 32U); LogS("'"); LogNL();
    }
    if (sz >= 0xBCU) {
        uint32_t nmodem = rd32(base, off + 0xACU);
        uint32_t fcode  = rd32(base, off + 0xB0U);
        uint32_t pcode  = rd32(base, off + 0xB4U);
        LogS("    nmodem_supported="); LogDec(nmodem);
        LogS(" feature_code="); LogDec(fcode); LogS("("); LogS(SocinfoFcName(fcode)); LogS(")");
        LogS(" pcode="); LogDec(pcode); LogNL();
    }
}

static void DecodeImageVersionTable(const volatile uint8_t *base, uint32_t off, uint32_t sz)
{
    LogS("  --- SMEM Item 469: IMAGE_VERSION_TABLE (off=0x"); LogHex(off, 6);
    LogS(" sz="); LogDec(sz); LogS(") ---"); LogNL();
    uint32_t slots = sz / 128U;
    if (slots > 32U) slots = 32U;
    uint32_t populated = 0U;
    for (uint32_t i = 0U; i < slots; i++) {
        uint32_t eo = off + i * 128U;
        if (eo + 128U > SZ_SMEM) break;
        uint8_t blk[128];
        copy_from_io(blk, base + eo, 128U);
        if (blk[0] == 0U && blk[75] == 0U && blk[95] == 0U) continue;
        populated++;
        LogS("    [");
        if (i < 10U) LogC(' ');
        LogDec(i); LogS(" "); LogS(ImageTableSlotName(i)); LogS("] name='");
        LogSanitizedStr(blk, 75U);
        LogS("' var='");
        LogSanitizedStr(blk + 75U, 20U);
        LogS("' oem='");
        LogSanitizedStr(blk + 95U, 33U);
        LogS("'"); LogNL();
    }
    LogS("    Total populated firmware image version slots: "); LogDec(populated); LogNL();
}

static void DecodeSmp2pItem(const volatile uint8_t *base, uint32_t off, uint32_t sz,
                            uint16_t h0, uint16_t h1, uint32_t item_id)
{
    if (sz < 20U || off + sz > SZ_SMEM) return;
    uint32_t magic = rd32(base, off + 0U);
    if (magic != SMP2P_MAGIC) return;
    uint32_t vf    = rd32(base, off + 4U);
    uint16_t lpid  = rd16(base, off + 8U);
    uint16_t rpid  = rd16(base, off + 10U);
    uint16_t total = rd16(base, off + 12U);
    uint16_t valid = rd16(base, off + 14U);
    uint32_t flags = rd32(base, off + 16U);

    LogS("  [SMP2P] item="); LogDec(item_id);
    LogS(" part="); LogS(SmemHostName(h0)); LogC(':'); LogS(SmemHostName(h1));
    LogS(" ver="); LogDec(vf & 0xFFU);
    LogS(" feat=0x"); LogHex((vf >> 8) & 0xFFFFFFU, 6);
    LogS(" lpid="); LogDec(lpid); LogS("->rpid="); LogDec(rpid);
    LogS(" entries="); LogDec(valid); LogC('/'); LogDec(total);
    LogS(" flags=0x"); LogHex(flags, 8); LogNL();

    uint32_t n = valid;
    if (n > SMP2P_MAX_ENTRY) n = SMP2P_MAX_ENTRY;
    for (uint32_t i = 0U; i < n; i++) {
        uint32_t eo = off + 20U + i * 20U;
        if (eo + 20U > off + sz || eo + 20U > SZ_SMEM) break;
        uint8_t ename[16];
        copy_from_io(ename, base + eo, 16U);
        uint32_t eval = rd32(base, eo + 16U);
        LogS("    entry["); LogDec(i); LogS("] '");
        LogSanitizedStr(ename, 16U);
        LogS("' = 0x"); LogHex(eval, 8); LogNL();
    }
}

static void DecodeGlinkItem(const volatile uint8_t *base, uint32_t off, uint32_t sz,
                            uint16_t h0, uint16_t h1, uint32_t item_id)
{
    if (off + sz > SZ_SMEM) return;
    LogS("  [GLINK] item="); LogDec(item_id);
    LogS(" part="); LogS(SmemHostName(h0)); LogC(':'); LogS(SmemHostName(h1));
    LogS(" sz="); LogDec(sz);
    if (item_id == SMEM_ITEM_GLINK_DESC && sz >= 16U) {
        uint32_t tx_tail = rd32(base, off + 0U);
        uint32_t tx_head = rd32(base, off + 4U);
        uint32_t rx_tail = rd32(base, off + 8U);
        uint32_t rx_head = rd32(base, off + 12U);
        LogS(" DESC tx(tail=0x"); LogHex(tx_tail, 8);
        LogS(" head=0x"); LogHex(tx_head, 8);
        LogS(") rx(tail=0x"); LogHex(rx_tail, 8);
        LogS(" head=0x"); LogHex(rx_head, 8); LogS(")");
    } else if ((item_id == SMEM_ITEM_GLINK_FIFO0 || item_id == SMEM_ITEM_GLINK_FIFO1) && sz >= 16U) {
        uint8_t prv[16];
        copy_from_io(prv, base + off, 16U);
        LogS(item_id == SMEM_ITEM_GLINK_FIFO0 ? " TX_FIFO0[0..15]=" : " RX_FIFO1[0..15]=");
        LogHexBytes(prv, 16U);
    }
    LogNL();
}

typedef struct {
    uint32_t off;
    uint32_t sz;
    uint16_t h0;
    uint16_t h1;
    uint16_t id;
} SMEM_SPECIAL_HIT;

static SMEM_SPECIAL_HIT g_smp2p_hits[24];
static uint32_t         g_smp2p_cnt = 0U;
static SMEM_SPECIAL_HIT g_glink_hits[24];
static uint32_t         g_glink_cnt = 0U;
static SMEM_SPECIAL_HIT g_socinfo_hit;
static SMEM_SPECIAL_HIT g_imgver_hit;
static int              g_has_socinfo = 0;
static int              g_has_imgver  = 0;

static void RecordSpecialSmemItem(uint16_t h0, uint16_t h1, uint32_t id, uint32_t off, uint32_t sz)
{
    if (id == SMEM_ITEM_SOCINFO && !g_has_socinfo) {
        g_socinfo_hit.off = off; g_socinfo_hit.sz = sz;
        g_socinfo_hit.h0 = h0; g_socinfo_hit.h1 = h1; g_socinfo_hit.id = (uint16_t)id;
        g_has_socinfo = 1;
    } else if (id == SMEM_ITEM_IMAGE_VERS && !g_has_imgver) {
        g_imgver_hit.off = off; g_imgver_hit.sz = sz;
        g_imgver_hit.h0 = h0; g_imgver_hit.h1 = h1; g_imgver_hit.id = (uint16_t)id;
        g_has_imgver = 1;
    } else if ((id == 94U || (id >= 427U && id <= 445U)) && g_smp2p_cnt < 24U) {
        SMEM_SPECIAL_HIT *h = &g_smp2p_hits[g_smp2p_cnt++];
        h->off = off; h->sz = sz; h->h0 = h0; h->h1 = h1; h->id = (uint16_t)id;
    } else if (id >= 478U && id <= 485U && g_glink_cnt < 24U) {
        SMEM_SPECIAL_HIT *h = &g_glink_hits[g_glink_cnt++];
        h->off = off; h->sz = sz; h->h0 = h0; h->h1 = h1; h->id = (uint16_t)id;
    }
}

static void WalkSmemPartition(const volatile uint8_t *smem, uint32_t part_off, uint32_t part_sz,
                              uint16_t exp_h0, uint16_t exp_h1, uint32_t cacheline, uint32_t walk_foreign)
{
    if (part_off == 0U || part_sz < 64U || part_off >= SZ_SMEM || part_sz > SZ_SMEM - part_off) {
        return;
    }
    int is_apps_or_global = (exp_h0 == 0U || exp_h1 == 0U ||
                             (exp_h0 == SMEM_GLOBAL_HOST && exp_h1 == SMEM_GLOBAL_HOST));
    if (!is_apps_or_global && walk_foreign == 0U) {
        LogS("  Partition "); LogS(SmemHostName(exp_h0)); LogS("("); LogDec(exp_h0); LogS("):");
        LogS(SmemHostName(exp_h1)); LogS("("); LogDec(exp_h1); LogS(")");
        LogS(" off=0x"); LogHex(part_off, 6);
        LogS(" sz=0x"); LogHex(part_sz, 6);
        LogS(" -> SKIPPED (non-APPS remote partition; StageMask|=0x20 to walk)"); LogNL();
        return;
    }
    uint8_t magic[4];
    copy_from_io(magic, smem + part_off, 4U);
    if (magic[0] != '$' || magic[1] != 'P' || magic[2] != 'R' || magic[3] != 'T') {
        LogS("  Part off=0x"); LogHex(part_off, 6);
        LogS(" ("); LogS(SmemHostName(exp_h0)); LogC(':'); LogS(SmemHostName(exp_h1));
        LogS("): bad magic "); LogHexBytes(magic, 4U); LogNL();
        return;
    }
    uint16_t h0       = rd16(smem, part_off + 4U);
    uint16_t h1       = rd16(smem, part_off + 6U);
    uint32_t psz      = rd32(smem, part_off + 8U);
    uint32_t free_unc = rd32(smem, part_off + 12U);
    uint32_t free_cac = rd32(smem, part_off + 16U);
    if (psz > part_sz) psz = part_sz;

    LogS("  Partition "); LogS(SmemHostName(h0)); LogS("("); LogDec(h0); LogS("):");
    LogS(SmemHostName(h1)); LogS("("); LogDec(h1); LogS(")");
    LogS(" off=0x"); LogHex(part_off, 6);
    LogS(" sz=0x"); LogHex(psz, 6);
    LogS(" free_unc=0x"); LogHex(free_unc, 6);
    LogS(" free_cac=0x"); LogHex(free_cac, 6);
    LogS(" cl="); LogDec(cacheline); LogNL();

    /* Uncached heap: walk forward from part_off + 32 to part_off + free_unc */
    uint32_t pos = 32U;
    uint32_t lim = free_unc;
    if (lim > psz) lim = psz;
    uint32_t u_cnt = 0U;
    while (pos + 16U <= lim && u_cnt < 512U) {
        uint16_t canary   = rd16(smem, part_off + pos + 0U);
        if (canary != SMEM_PRIVATE_CANARY) {
            LogS("    [U] canary mismatch @+0x"); LogHex(pos, 6);
            LogS(" got=0x"); LogHex(canary, 4); LogNL();
            break;
        }
        uint16_t item     = rd16(smem, part_off + pos + 2U);
        uint32_t esz      = rd32(smem, part_off + pos + 4U);
        uint16_t pad_data = rd16(smem, part_off + pos + 8U);
        uint16_t pad_hdr  = rd16(smem, part_off + pos + 10U);
        uint32_t data_rel = pos + 16U + (uint32_t)pad_hdr;
        uint32_t next_pos = data_rel + esz;
        if (esz > psz || next_pos <= pos || next_pos > lim) {
            LogS("    [U] bounds stop @+0x"); LogHex(pos, 6);
            LogS(" item="); LogDec(item); LogS(" esz=0x"); LogHex(esz, 8); LogNL();
            break;
        }
        uint32_t pay_sz = (esz >= pad_data) ? (esz - pad_data) : esz;
        uint32_t abs_off = part_off + data_rel;
        uint8_t prv[8];
        uint32_t pn = (pay_sz < 8U) ? pay_sz : 8U;
        copy_from_io(prv, smem + abs_off, pn);

        LogS("    [U] id="); LogDec(item);
        LogS(" sz="); LogDec(pay_sz);
        LogS(" off=0x"); LogHex(abs_off, 6);
        LogS(" prv="); LogHexBytes(prv, pn); LogNL();

        RecordSpecialSmemItem(h0, h1, item, abs_off, pay_sz);
        pos = next_pos;
        u_cnt++;
    }

    /* Cached heap: walk backward from psz - ALIGN(16, cl) down to free_cac */
    uint32_t cl = (cacheline >= 16U && cacheline <= 1024U) ? cacheline : 64U;
    uint32_t hdr_stride = align_up(16U, cl);
    uint32_t c_cnt = 0U;
    if (free_cac < psz && psz >= hdr_stride && free_cac >= lim) {
        uint32_t cpos = psz - hdr_stride;
        while (cpos > free_cac && c_cnt < 512U) {
            uint16_t canary   = rd16(smem, part_off + cpos + 0U);
            if (canary != SMEM_PRIVATE_CANARY) {
                LogS("    [C] canary mismatch @+0x"); LogHex(cpos, 6);
                LogS(" got=0x"); LogHex(canary, 4); LogNL();
                break;
            }
            uint16_t item     = rd16(smem, part_off + cpos + 2U);
            uint32_t esz      = rd32(smem, part_off + cpos + 4U);
            uint16_t pad_data = rd16(smem, part_off + cpos + 8U);
            if (esz == 0U || esz > cpos || cpos - esz < free_cac) {
                LogS("    [C] bounds stop @+0x"); LogHex(cpos, 6);
                LogS(" item="); LogDec(item); LogS(" esz=0x"); LogHex(esz, 8); LogNL();
                break;
            }
            uint32_t data_rel = cpos - esz;
            uint32_t pay_sz = (esz >= pad_data) ? (esz - pad_data) : esz;
            uint32_t abs_off = part_off + data_rel;
            uint8_t prv[8];
            uint32_t pn = (pay_sz < 8U) ? pay_sz : 8U;
            copy_from_io(prv, smem + abs_off, pn);

            LogS("    [C] id="); LogDec(item);
            LogS(" sz="); LogDec(pay_sz);
            LogS(" off=0x"); LogHex(abs_off, 6);
            LogS(" prv="); LogHexBytes(prv, pn); LogNL();

            RecordSpecialSmemItem(h0, h1, item, abs_off, pay_sz);
            c_cnt++;
            if (data_rel < hdr_stride) break;
            cpos = data_rel - hdr_stride;
        }
    }
    LogS("    Summary: uncached="); LogDec(u_cnt); LogS(" cached="); LogDec(c_cnt); LogNL();
}

static void ProbeSmem(uint32_t stage_mask)
{
    LogS("=== [2/5] Qualcomm SMEM v12 Inspection (0x81D00000, 2 MiB) ==="); LogNL();
    PHYSICAL_ADDRESS pa;
    pa.QuadPart = (int64_t)PA_SMEM_BASE;
    volatile uint8_t *smem = (volatile uint8_t *)MmMapIoSpace(pa, SZ_SMEM, MmNonCached);
    if (!smem) {
        LogS("  [ERR] MmMapIoSpace(SMEM 0x81D00000) failed"); LogNL();
        return;
    }

    g_smp2p_cnt = 0U;
    g_glink_cnt = 0U;
    g_has_socinfo = 0;
    g_has_imgver  = 0;

    uint32_t sbl_ver     = rd32(smem, 0x40U + SMEM_MASTER_SBL_VER_IDX * 4U);
    uint32_t initialized = rd32(smem, 0xC0U);
    uint32_t free_offset = rd32(smem, 0xC4U);
    uint32_t available   = rd32(smem, 0xC8U);
    uint32_t reserved    = rd32(smem, 0xCCU);

    LogS("  Header: SBL_VER[7]=0x"); LogHex(sbl_ver, 8);
    LogS(" (SMEM_MAJOR="); LogDec(sbl_ver >> 16); LogS(")");
    LogS(" init="); LogDec(initialized);
    LogS(" free_off=0x"); LogHex(free_offset, 8);
    LogS(" avail=0x"); LogHex(available, 8);
    LogS(" rsvd=0x"); LogHex(reserved, 8); LogNL();

    /* Non-zero subsystem versions in header->version[0..31] */
    LogS("  Non-zero version[]:");
    for (uint32_t i = 0U; i < 32U; i++) {
        uint32_t v = rd32(smem, 0x40U + i * 4U);
        if (v != 0U) {
            LogS(" ["); LogDec(i); LogS("]=0x"); LogHex(v, 8);
        }
    }
    LogNL();

    /* Legacy global TOC (header->toc[0..511] at +0xD0, 16 B each) */
    uint32_t legacy_cnt = 0U;
    for (uint32_t i = 0U; i < SMEM_ITEM_COUNT_MAX; i++) {
        uint32_t eo = 0xD0U + i * 16U;
        uint32_t alloc = rd32(smem, eo + 0U);
        if (alloc == 0U) continue;
        uint32_t ioff  = rd32(smem, eo + 4U);
        uint32_t isz   = rd32(smem, eo + 8U);
        uint32_t aux   = rd32(smem, eo + 12U) & 0xFFFFFFFCU;
        legacy_cnt++;
        LogS("  [GLOBAL_TOC] id="); LogDec(i);
        LogS(" off=0x"); LogHex(ioff, 6);
        LogS(" sz="); LogDec(isz);
        if (aux != 0U) { LogS(" aux=0x"); LogHex(aux, 8); }
        if (aux == 0U && ioff < SZ_SMEM && isz > 0U && isz <= SZ_SMEM - ioff) {
            uint8_t prv[8];
            uint32_t pn = (isz < 8U) ? isz : 8U;
            copy_from_io(prv, smem + ioff, pn);
            LogS(" prv="); LogHexBytes(prv, pn);
            RecordSpecialSmemItem(SMEM_GLOBAL_HOST, SMEM_GLOBAL_HOST, i, ioff, isz);
        }
        LogNL();
    }
    LogS("  Legacy global TOC allocated count: "); LogDec(legacy_cnt); LogNL();

    /* Partition Table ($TOC) at SMEM_BASE + 2 MiB - 4 KiB */
    uint32_t pt_off = SZ_SMEM - SZ_SMEM_PTABLE_TAIL;
    uint8_t pt_magic[4];
    copy_from_io(pt_magic, smem + pt_off, 4U);
    if (pt_magic[0] == '$' && pt_magic[1] == 'T' && pt_magic[2] == 'O' && pt_magic[3] == 'C') {
        uint32_t pt_ver  = rd32(smem, pt_off + 4U);
        uint32_t pt_num  = rd32(smem, pt_off + 8U);
        LogS("  $TOC found @0x"); LogHex(pt_off, 6);
        LogS(": ver="); LogDec(pt_ver);
        LogS(" num_entries="); LogDec(pt_num); LogNL();

        uint32_t max_e = (SZ_SMEM_PTABLE_TAIL - 32U) / 48U;
        if (pt_num > max_e) pt_num = max_e;

        /* Check struct smem_info ("SIII") immediately after ptable->entry[pt_num] */
        uint32_t info_off = pt_off + 32U + pt_num * 48U;
        if (info_off + 18U <= SZ_SMEM) {
            uint8_t im[4];
            copy_from_io(im, smem + info_off, 4U);
            if (im[0] == 'S' && im[1] == 'I' && im[2] == 'I' && im[3] == 'I') {
                uint32_t isz   = rd32(smem, info_off + 4U);
                uint32_t ibase = rd32(smem, info_off + 8U);
                uint16_t nitem = rd16(smem, info_off + 16U);
                LogS("  SIII smem_info: base=0x"); LogHex(ibase, 8);
                LogS(" size=0x"); LogHex(isz, 8);
                LogS(" num_items="); LogDec(nitem); LogNL();
            }
        }

        for (uint32_t i = 0U; i < pt_num; i++) {
            uint32_t eo   = pt_off + 32U + i * 48U;
            uint32_t poff = rd32(smem, eo + 0U);
            uint32_t psz  = rd32(smem, eo + 4U);
            uint16_t h0   = rd16(smem, eo + 12U);
            uint16_t h1   = rd16(smem, eo + 14U);
            uint32_t cl   = rd32(smem, eo + 16U);
            if (poff == 0U || psz == 0U) continue;
            WalkSmemPartition(smem, poff, psz, h0, h1, cl, (stage_mask & 0x20U));
        }
    } else {
        LogS("  [WARN] $TOC magic not found @0x"); LogHex(pt_off, 6);
        LogS(" bytes="); LogHexBytes(pt_magic, 4U); LogNL();
    }

    /* Decode discovered high-value SMEM items */
    if (g_has_socinfo) {
        DecodeSocinfo(smem, g_socinfo_hit.off, g_socinfo_hit.sz);
    } else {
        LogS("  [WARN] SMEM Item 137 (SOCINFO) not found"); LogNL();
    }
    if (g_has_imgver) {
        DecodeImageVersionTable(smem, g_imgver_hit.off, g_imgver_hit.sz);
    } else {
        LogS("  [WARN] SMEM Item 469 (IMAGE_VERSION_TABLE) not found"); LogNL();
    }

    LogS("  --- SMP2P Ring Inspection ("); LogDec(g_smp2p_cnt); LogS(" candidate items) ---"); LogNL();
    for (uint32_t i = 0U; i < g_smp2p_cnt; i++) {
        DecodeSmp2pItem(smem, g_smp2p_hits[i].off, g_smp2p_hits[i].sz,
                        g_smp2p_hits[i].h0, g_smp2p_hits[i].h1, g_smp2p_hits[i].id);
    }

    LogS("  --- GLink SMEM Native Transport Inspection ("); LogDec(g_glink_cnt); LogS(" items) ---"); LogNL();
    for (uint32_t i = 0U; i < g_glink_cnt; i++) {
        DecodeGlinkItem(smem, g_glink_hits[i].off, g_glink_hits[i].sz,
                        g_glink_hits[i].h0, g_glink_hits[i].h1, g_glink_hits[i].id);
    }

    MmUnmapIoSpace((PVOID)smem, SZ_SMEM);
}

/* ============================================================================
 * 3. CMD_DB Parser (0x81C60000, 128 KiB)
 * ============================================================================ */
static const char *CmdDbSlvName(uint16_t slv)
{
    switch (slv) {
        case 3U: return "ARC";
        case 4U: return "VRM";
        case 5U: return "BCM";
        default: return "SLV";
    }
}

static int IsInterestingCmdDbVrm(const char *id)
{
    static const char * const keys[] = {
        "mss", "nsp", "lpass", "ipa", "cx", "mx", "ebi", "xo", "clk",
        "vrm", "smpg", "smpe", "ldob", "ldoe", "wlan", "bt", "rf"
    };
    for (uint32_t i = 0U; i < (uint32_t)(sizeof(keys) / sizeof(keys[0])); i++) {
        if (str_contains_ci(id, keys[i])) return 1;
    }
    return 0;
}

static void ProbeCmdDb(void)
{
    LogS("=== [3/5] CMD_DB Modem/DSP/IPA Resource Dump (0x81C60000, 128 KiB) ==="); LogNL();
    PHYSICAL_ADDRESS pa;
    pa.QuadPart = (int64_t)PA_CMD_DB_BASE;
    volatile uint8_t *cdb = (volatile uint8_t *)MmMapIoSpace(pa, SZ_CMD_DB, MmNonCached);
    if (!cdb) {
        LogS("  [ERR] MmMapIoSpace(CMD_DB 0x81C60000) failed"); LogNL();
        return;
    }

    uint32_t ver = rd32(cdb, 0U);
    uint8_t magic[4];
    copy_from_io(magic, cdb + 4U, 4U);
    LogS("  Header: ver=0x"); LogHex(ver, 8);
    LogS(" magic="); LogHexBytes(magic, 4U); LogNL();

    if (magic[0] != 0xDBU || magic[1] != 0x30U || magic[2] != 0x03U || magic[3] != 0x0CU) {
        LogS("  [ERR] CMD_DB magic mismatch (expected DB 30 03 0C)"); LogNL();
        MmUnmapIoSpace((PVOID)cdb, SZ_CMD_DB);
        return;
    }

    /* struct cmd_db_header: 4(ver)+4(magic)+8*16(rsc_hdr)+4(csum)+4(rsvd) = 144 (0x90) */
    const uint32_t data_base = 8U + 8U * 16U + 8U;

    for (uint32_t s = 0U; s < 8U; s++) {
        uint32_t rh = 8U + s * 16U;
        uint16_t slv_id   = rd16(cdb, rh + 0U);
        uint16_t hdr_off  = rd16(cdb, rh + 2U);
        uint16_t dat_off  = rd16(cdb, rh + 4U);
        uint16_t cnt      = rd16(cdb, rh + 6U);
        uint16_t rver     = rd16(cdb, rh + 8U);
        if (slv_id == 0U || cnt == 0U) continue;

        LogS("  Table["); LogDec(s); LogS("] "); LogS(CmdDbSlvName(slv_id));
        LogS("(slv="); LogDec(slv_id); LogS(") cnt="); LogDec(cnt);
        LogS(" ver=0x"); LogHex(rver, 4);
        LogS(" hdr_off=0x"); LogHex(hdr_off, 4);
        LogS(" dat_off=0x"); LogHex(dat_off, 4); LogNL();

        uint32_t printed = 0U;
        for (uint32_t i = 0U; i < cnt; i++) {
            uint32_t eo = data_base + (uint32_t)hdr_off + i * 24U;
            if (eo + 24U > SZ_CMD_DB) break;
            char id[9];
            uint8_t raw_id[8];
            copy_from_io(raw_id, cdb + eo, 8U);
            for (uint32_t k = 0U; k < 8U; k++) {
                id[k] = (raw_id[k] >= 0x20U && raw_id[k] <= 0x7EU) ? (char)raw_id[k] : '\0';
            }
            id[8] = '\0';
            uint32_t raddr = rd32(cdb, eo + 16U);
            uint16_t rlen  = rd16(cdb, eo + 20U);
            uint16_t roff  = rd16(cdb, eo + 22U);

            /* Dump ALL ARC(3) and ALL BCM(5), and filtered VRM(4) entries */
            int show = (slv_id == 3U || slv_id == 5U) || IsInterestingCmdDbVrm(id);
            if (!show) continue;
            printed++;

            LogS("    "); LogS(CmdDbSlvName(slv_id)); LogS(" '"); LogS(id);
            LogS("' addr=0x"); LogHex(raddr, 5);
            LogS(" len="); LogDec(rlen);
            uint32_t aux_abs = data_base + (uint32_t)dat_off + (uint32_t)roff;
            if (rlen > 0U && aux_abs < SZ_CMD_DB && rlen <= SZ_CMD_DB - aux_abs) {
                uint8_t aux[8];
                uint32_t an = (rlen < 8U) ? rlen : 8U;
                copy_from_io(aux, cdb + aux_abs, an);
                LogS(" aux="); LogHexBytes(aux, an);
            }
            LogNL();
        }
        LogS("    (printed "); LogDec(printed); LogC('/'); LogDec(cnt); LogS(" entries)"); LogNL();
    }

    MmUnmapIoSpace((PVOID)cdb, SZ_CMD_DB);
}

/* ============================================================================
 * 4. GCC Read-Only Clock Status Snapshot (0x00100000, 0x1F5000)
 * ============================================================================ */
typedef struct {
    const char *name;
    uint32_t    off;
    uint8_t     hwcg;
    uint8_t     ext_only; /* 1 = requires StageMask & 0x10 */
} GCC_REG_ITEM;

static const GCC_REG_ITEM g_gcc_regs[] = {
    { "GPLL0_MODE",               0x00000U, 0U, 0U },
    { "GPLL4_MODE",               0x04000U, 0U, 0U },
    { "GPLL7_MODE",               0x07000U, 0U, 0U },
    { "GPLL9_MODE",               0x09000U, 0U, 0U },
    { "APCS_CLK_BR_ENA_VOTE_0",   0x52000U, 0U, 1U },
    { "APCS_CLK_BR_ENA_VOTE_1",   0x52008U, 0U, 0U },
    { "APCS_CLK_BR_ENA_VOTE_2",   0x52010U, 0U, 0U },
    { "APCS_GPLL_ENA_VOTE",       0x52018U, 0U, 0U },
    { "GCC_BOOT_ROM_AHB_CBCR",    0x38004U, 1U, 1U },
    { "GCC_CFG_NOC_PCIE_ANOC",    0x10028U, 1U, 0U },
    { "GCC_CNOC_PCIE_SF_AXI",     0x10030U, 1U, 0U },
    { "GCC_AGGRE_NOC_PCIE_AXI",   0x1003CU, 1U, 0U },
    { "GCC_AGGRE_UFS_PHY_AXI",    0x770E4U, 1U, 1U },
    { "GCC_AGGRE_USB3_PRIM_AXI",  0x3908CU, 1U, 1U },
    { "GCC_CFG_NOC_USB3_AXI",     0x39088U, 1U, 1U },
    { "GCC_GPU_CFG_AHB_CBCR",     0x71004U, 1U, 0U },
    { "GCC_GPU_MEMNOC_GFX_CBCR",  0x71010U, 0U, 0U },
    { "GCC_DDRSS_GPU_AXI_CBCR",   0x71154U, 0U, 0U },
    { "GCC_DISP_AHB_CBCR",        0x27004U, 1U, 0U },
    { "GCC_DISP_HF_AXI_CBCR",     0x2700CU, 1U, 0U },
    { "GCC_CAMERA_AHB_CBCR",      0x26004U, 1U, 1U },
    { "GCC_VIDEO_AHB_CBCR",       0x32004U, 1U, 1U },
    { "GCC_QUPV3_I2C_S_AHB",      0x23140U, 1U, 0U },
    { "GCC_QUPV3_I2C_CORE",       0x23144U, 0U, 0U },
    { "GCC_QUPV3_WRAP1_M_AHB",    0x2327CU, 1U, 0U },
    { "GCC_QUPV3_WRAP1_S_AHB",    0x23280U, 1U, 0U },
    { "GCC_QUPV3_WRAP1_CORE",     0x23284U, 0U, 0U },
    { "GCC_QUPV3_WRAP2_M_AHB",    0x233CCU, 1U, 0U },
    { "GCC_QUPV3_WRAP2_S_AHB",    0x233D0U, 1U, 0U },
    { "GCC_QUPV3_WRAP2_CORE",     0x233D4U, 0U, 0U },
};

static void ProbeGccSubsysClocks(uint32_t stage_mask)
{
    LogS("=== [4/5] GCC Read-Only Clock Status (0x00100000, 0x1F5000) ==="); LogNL();
    LogS("  NOTE: On SM8550 (gcc-kalama.c + clk-rpmh.c), MSS/LPASS/CDSP/IPA clocks are"); LogNL();
    LogS("        RPMh ARC/BCM resources (xo.lvl + IP0), NOT legacy GCC branch CBCRs."); LogNL();

    PHYSICAL_ADDRESS pa;
    pa.QuadPart = (int64_t)PA_GCC_BASE;
    volatile uint8_t *gcc = (volatile uint8_t *)MmMapIoSpace(pa, SZ_GCC, MmNonCached);
    if (!gcc) {
        LogS("  [ERR] MmMapIoSpace(GCC 0x00100000) failed"); LogNL();
        return;
    }

    uint32_t cnt = (uint32_t)(sizeof(g_gcc_regs) / sizeof(g_gcc_regs[0]));
    for (uint32_t i = 0U; i < cnt; i++) {
        if (g_gcc_regs[i].ext_only && ((stage_mask & 0x10U) == 0U)) {
            LogS("  "); LogS(g_gcc_regs[i].name); LogS(" (+0x"); LogHex(g_gcc_regs[i].off, 5);
            LogS(") = SKIPPED (StageMask|=0x10)"); LogNL();
            continue;
        }
        uint32_t v = rd32(gcc, g_gcc_regs[i].off);
        LogS("  "); LogS(g_gcc_regs[i].name);
        LogS(" (+0x"); LogHex(g_gcc_regs[i].off, 5);
        LogS(") = 0x"); LogHex(v, 8);
        if (g_gcc_regs[i].hwcg) {
            LogS((v & 0x80000000U) ? ((v & 2U) ? " [HWCG]" : " [OFF]") : " [ON]");
        } else if (g_gcc_regs[i].off >= 0x10000U && g_gcc_regs[i].off != 0x52000U &&
                   g_gcc_regs[i].off != 0x52008U && g_gcc_regs[i].off != 0x52010U &&
                   g_gcc_regs[i].off != 0x52018U) {
            LogS((v & 0x80000000U) ? " [CLK_OFF]" : " [CLK_ON]");
        }
        LogNL();
    }

    MmUnmapIoSpace((PVOID)gcc, SZ_GCC);
}

/* ============================================================================
 * 5. PIL Reserved & ADSP MHI DDR First-4KB Header Probe (Gunyah-Safe)
 * ============================================================================ */
typedef struct {
    const char *name;
    uint64_t    pa;
    uint32_t    total_sz;
    uint8_t     xpu_risk; /* 1 = skip unless AllowUnsafe=1 to prevent Gunyah/XPU Data Abort */
} PIL_DDR_REGION;

static const PIL_DDR_REGION g_pil_regions[] = {
    { "chipinfo_region",     0x81CF4000ULL, 0x00001000U, 1U }, /* MemoryMapLib: SYS_MEM WRITE_BACK */
    { "adsp_mhi_region",     0x81F00000ULL, 0x00020000U, 1U }, /* MemoryMapLib: SYS_MEM WRITE_BACK */
    { "pil_reserved_base",   0x89800000ULL, 0x01000000U, 1U }, /* Not in s23_live.dts reg; TZ/RKP risk */
    { "mpss_region",         0x8A800000ULL, 0x10800000U, 0U },
    { "q6_mpss_dtb_region",  0x9B000000ULL, 0x00080000U, 0U },
    { "ipa_fw_region",       0x9B080000ULL, 0x00010000U, 1U }, /* IPA XPU risk */
    { "ipa_gsi_region",      0x9B090000ULL, 0x0000A000U, 1U }, /* IPA XPU risk */
    { "spss_region",         0x9B100000ULL, 0x00180000U, 1U },
    { "spu_tz_shared_mem",   0x9B280000ULL, 0x00060000U, 1U },
    { "cdsp_region",         0x9C900000ULL, 0x02000000U, 0U },
    { "q6_cdsp_dtb_region",  0x9E900000ULL, 0x00080000U, 0U },
    { "q6_adsp_dtb_region",  0x9E980000ULL, 0x00080000U, 0U },
    { "adspslpi_region",     0x9EA00000ULL, 0x059B4000U, 0U },
};

static void ProbePilDdrHeaders(uint32_t region_mask, uint32_t allow_unsafe)
{
    LogS("=== [5/5] PIL Reserved DDR Header Probe (RegionMask=0x");
    LogHex(region_mask, 4); LogS(" AllowUnsafe="); LogDec(allow_unsafe); LogS(") ==="); LogNL();
    uint32_t cnt = (uint32_t)(sizeof(g_pil_regions) / sizeof(g_pil_regions[0]));

    for (uint32_t i = 0U; i < cnt; i++) {
        const PIL_DDR_REGION *r = &g_pil_regions[i];
        int in_imem = IsRegionInImemPil(r->pa);

        LogS("  ["); LogDec(i); LogS("] "); LogS(r->name);
        LogS(" @0x"); LogHex(r->pa, 8);
        LogS(" (sz=0x"); LogHex(r->total_sz, 8); LogS("): ");

        if ((region_mask & (1U << i)) == 0U) {
            LogS("SKIPPED (bit "); LogDec(i); LogS(" unset in RegionMask)"); LogNL();
            continue;
        }
        if ((r->xpu_risk || in_imem) && allow_unsafe == 0U) {
            LogS("SKIPPED (");
            if (in_imem) LogS("ACTIVE_IN_IMEM_PIL");
            if (in_imem && r->xpu_risk) LogS("+");
            if (r->xpu_risk) LogS("XPU_OR_SYSMEM_GUARD");
            LogS("; set AllowUnsafe=1 to force)"); LogNL();
            continue;
        }

        PHYSICAL_ADDRESS pa;
        pa.QuadPart = (int64_t)r->pa;
        volatile uint8_t *p = (volatile uint8_t *)MmMapIoSpace(pa, 0x1000U, MmNonCached);
        if (!p) {
            LogS("MmMapIoSpace FAILED"); LogNL();
            continue;
        }

        uint32_t w0 = rd32(p, 0x00U);
        uint32_t w1 = rd32(p, 0x04U);
        uint32_t w2 = rd32(p, 0x08U);
        uint32_t w3 = rd32(p, 0x0CU);
        uint32_t nz = 0U;
        for (uint32_t off = 0U; off < 0x1000U; off += 4U) {
            if (rd32(p, off) != 0U) nz++;
        }

        const char *tag = "EMPTY";
        if (w0 == 0x464C457FU)      tag = "ELF_HEADER(\\x7FELF)";
        else if (w0 == 0xEDFE0DD0U) tag = "FDT_BLOB(0xD00DFEED)";
        else if (nz > 0U)           tag = "NONZERO_DATA";

        LogS(tag);
        LogS(" nz_dw="); LogDec(nz); LogS("/1024");
        LogS(" hdr="); LogHex(w0, 8); LogC(' '); LogHex(w1, 8);
        LogC(' '); LogHex(w2, 8); LogC(' '); LogHex(w3, 8); LogNL();

        MmUnmapIoSpace((PVOID)p, 0x1000U);
    }
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
    uint32_t stage_mask   = ReadRegDword(L"StageMask", 0x01U);
    uint32_t region_mask  = ReadRegDword(L"RegionMask", 0x00U);
    uint32_t allow_unsafe = ReadRegDword(L"AllowUnsafe", 0x00U);
    LogS("=== s23pilprobe.sys v1.0.0.2 started (StageMask=0x");
    LogHex(stage_mask, 2);
    LogS(" RegionMask=0x"); LogHex(region_mask, 4);
    LogS(" AllowUnsafe="); LogDec(allow_unsafe);
    LogS(") ==="); LogNL();

    if (stage_mask & 0x01U) {
        ProbeCmdDb();
        ProbeGccSubsysClocks(stage_mask);
    }
    if (stage_mask & 0x02U) {
        ProbeImemPilInfo();
    }
    if (stage_mask & 0x04U) {
        ProbeSmem(stage_mask);
    }
    if (stage_mask & 0x08U) {
        ProbePilDdrHeaders(region_mask, allow_unsafe);
    }

    LogS("=== s23pilprobe.sys completed cleanly (log_bytes=");
    LogDec(g_log_off + (int64_t)g_log_pos);
    LogS(") ==="); LogNL();
    LogClose();

    KeStallExecutionProcessor(1U);
    {
        LARGE_INTEGER li;
        li.QuadPart = -10000LL; /* 1 ms settle at PASSIVE_LEVEL */
        KeDelayExecutionThread(0, 0, &li);
    }

    /* One-shot Demand-Start probe: return STATUS_UNSUCCESSFUL so SCM unloads cleanly */
    return STATUS_UNSUCCESSFUL;
}
