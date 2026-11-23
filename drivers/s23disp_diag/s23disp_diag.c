/*
 * s23disp_diag.c - Samsung Galaxy S23 (dm1q / SM8550-AC) Live MDSS/DSI0 Display Diagnostic & Wake Driver v1.0.0.2
 *
 * Pre-allocated 64 KB write-through log: C:\Users\Public\s23disp_diag.log
 * Registry parameter: HKLM\SYSTEM\CurrentControlSet\Services\s23disp_diag\Parameters\ActionMask (REG_DWORD)
 *   0x00 (Default): Strictly read-only diagnostic dump of TLMM panel GPIOs, DISP_CC GDSC/BCR/CBCRs,
 *                   and (ONLY if full 5-register G116Gate passes!) MDSS_MDP CTL_0 / INTF_1 / PP_0 / DSC_0 (1.2) / DSI0_CTRL
 *                   sampled twice 100 ms apart (including INTF_TEAR_INT_COUNT_VAL +0x298 & AR_STATUS +0x2C0).
 *   0x01          : Safe Auto-Refresh re-arm (0x0AE36284=1, 0x0AE362B4=0x80000002) + CTL_0_START ONLY if
 *                   currently unarmed or stalled (set bit 0x08 to force-kick even if active).
 *   0x02          : Send Samsung XBL S6E3FAC max-brightness / Display-On DCS sequence via QCOM DSI v2.7 TPG DMA FIFO
 *                   (zero DDR DMA, bypasses apps-smmu SID 0x1c00!). Runs BEFORE Action 0x01 with Auto-Refresh paused.
 *   0x04          : Used with 0x02 — prepend DCS 0x11 (Sleep Out) + 120 ms wait before brightness & 0x29 Display On.
 *   0x08          : Used with 0x01 — force Auto-Refresh write + CTL_0_START kick even if already active.
 *   0x10          : Read-only DSI0 BTA DCS readback (0x04 RDDID, 0x0A POWER, 0x0E SIG, 0x52 DBV, 0x54 CTRL) + PM8550 GPIO11.
 *   0x20          : Send S6E3FAC Wake using MIPI_DSI_DCS_LONG_WRITE (0x39) + 0x15 + F1/FC unlock (and BTA verify).
 *
 * CRITICAL AXI SAFETY GATE (Full SM8550 G116Gate):
 *   Never touches 0x0AE00000..0x0AEFFFFF unless ALL 5 DISP_CC (0x0AF00000) conditions hold:
 *     1. CORE_BCR     (+0x8000) bit 0 == 0
 *     2. MDP_CLK_CBCR (+0x800C) (val & 0x80000001) == 0x1 (CLK_OFF=0, CLK_ENABLE=1)
 *     3. AHB_CLK_CBCR (+0x80A4) (val & 0x80000001) == 0x1 (CLK_OFF=0, CLK_ENABLE=1)
 *     4. CORE_GDSCR   (+0x9000) (val & 0x80000007) == 0x80000000 (PWR_ON=1, SW/HW collapse=0)
 *     5. CFG_GDSCR    (+0x9004) (val & 0x00018000) == 0x00010000
 */

#include <stdint.h>
#include <stddef.h>

typedef int32_t NTSTATUS;
typedef uint16_t WCHAR;
typedef uint8_t BOOLEAN;
typedef void *PVOID;
typedef void *HANDLE;

#define STATUS_SUCCESS                ((NTSTATUS)0x00000000L)
#define STATUS_UNSUCCESSFUL           ((NTSTATUS)0xC0000001L)
#define NT_SUCCESS(st)                (((NTSTATUS)(st)) >= 0)

#define OBJ_CASE_INSENSITIVE          0x00000040UL
#define OBJ_KERNEL_HANDLE             0x00000200UL
#define FILE_GENERIC_WRITE            0x00120116UL
#define FILE_SHARE_READ               0x00000001UL
#define FILE_SHARE_WRITE              0x00000002UL
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
typedef struct _DRIVER_OBJECT DRIVER_OBJECT, *PDRIVER_OBJECT;

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
    if (ms == 0) return;
    LARGE_INTEGER li;
    li.QuadPart = -((int64_t)ms * 10000LL);
    KeDelayExecutionThread(0, 0, &li);
}

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

/* ============================================================================
 * Write-Through Kernel Logger (C:\Users\Public\s23disp_diag.log)
 * ============================================================================ */
static HANDLE g_log = NULL;
static int64_t g_log_off = 0;
static char g_buf[4096];
static uint32_t g_pos = 0;

static void LogOpen(void) {
    UNICODE_STRING u;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    RtlInitUnicodeString(&u, L"\\??\\C:\\Users\\Public\\s23disp_diag.log");
    InitObjAttr(&oa, &u, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE);
    g_log_off = 0;
    g_pos = 0;
    if (!NT_SUCCESS(ZwCreateFile(
            &g_log, FILE_GENERIC_WRITE, &oa, &iosb, NULL, FILE_ATTRIBUTE_NORMAL,
            FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN_IF,
            FILE_WRITE_THROUGH | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0))) {
        g_log = NULL;
    }
}

static void LogFlush(void) {
    if (!g_log || g_pos == 0) {
        g_pos = 0;
        return;
    }
    if (g_log_off + (int64_t)g_pos > 65000) {
        g_pos = 0;
        return;
    }
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER off;
    off.QuadPart = g_log_off;
    ZwWriteFile(g_log, NULL, NULL, NULL, &iosb, g_buf, g_pos, &off, NULL);
    g_log_off += g_pos;
    g_pos = 0;
}

static void LogClose(void) {
    LogFlush();
    if (g_log) {
        ZwClose(g_log);
        g_log = NULL;
    }
}

static void LogC(char c) {
    if (g_pos >= sizeof(g_buf) - 16) LogFlush();
    if (g_pos < sizeof(g_buf)) g_buf[g_pos++] = c;
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
    if (v < 0) { LogC('-'); /* magnitude taken as unsigned below: -INT64_MIN is undefined behaviour */ }
    char tmp[24];
    int n = 0;
    uint64_t uv = (v < 0) ? (0ULL - (uint64_t)v) : (uint64_t)v;
    if (uv == 0) tmp[n++] = '0';
    while (uv > 0) { tmp[n++] = (char)('0' + (uv % 10)); uv /= 10; }
    while (n > 0) LogC(tmp[--n]);
}

static void LogNL(void) {
    LogC('\r');
    LogC('\n');
    LogFlush();
}

static uint32_t ReadActionMask(void) {
    UNICODE_STRING uk, uv;
    OBJECT_ATTRIBUTES oa;
    HANDLE hk = NULL;
    uint32_t val = 0;
    RtlInitUnicodeString(&uk, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\s23disp_diag\\Parameters");
    InitObjAttr(&oa, &uk, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE);
    if (!NT_SUCCESS(ZwOpenKey(&hk, KEY_READ, &oa))) return 0;
    RtlInitUnicodeString(&uv, L"ActionMask");
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

static PVOID MapPhys(uint64_t phys, size_t sz, const char *name) {
    PHYSICAL_ADDRESS pa;
    pa.QuadPart = (int64_t)phys;
    PVOID p = MmMapIoSpace(pa, sz, 0 /* MmNonCached */);
    LogS("Map "); LogS(name); LogS(" @ 0x"); LogHex(phys, 8);
    LogS(" (0x"); LogHex(sz, 6); LogS(") -> ");
    LogS(p ? "OK" : "FAILED"); LogNL();
    return p;
}

/* ============================================================================
 * QCOM DSI v2.7 TPG DMA FIFO Command Helper (Zero DDR DMA / Zero SMMU Fault)
 * Verified against S23 kernel dsi_ctrl_hw_cmn.c:913-978 & 1141-1204:
 *   Short write (dtype 0x05): [b0=cmd, b1=0x00, b2=0x05, b3=0x80(LAST)] (4 B)
 *   Long write  (dtype 0x29): [b0=wc_lo, b1=wc_hi, b2=0x29, b3=0xC0(LAST|LONG)] + payload + 0xFF pad (4B aligned)
 * ============================================================================ */
static int Dsi0WaitIdle(volatile uint8_t *dsi0, const char *label) {
    for (uint32_t i = 0; i < 60; i++) {
        if ((mmio_r32(dsi0, 0x08) & 0x07U) == 0) return 1;
        if (i < 10) {
            KeStallExecutionProcessor(20);
        } else {
            SleepMs(1);
        }
    }
    LogS("  DSI0 busy before "); LogS(label);
    LogS(" DSI_STATUS=0x"); LogHex(mmio_r32(dsi0, 0x08), 8); LogNL();
    return 0;
}

static int Dsi0SendTpgFifoPkt(volatile uint8_t *dsi0, const uint32_t *dwords, uint32_t n_dwords, const char *label) {
    if (!Dsi0WaitIdle(dsi0, label)) return 0;

    uint32_t saved_dma_ctrl = mmio_r32(dsi0, 0x3C);

    /* Reset TPG FIFO cleanly before loading */
    mmio_w32(dsi0, 0x15C, 0U);
    mmio_w32(dsi0, 0x1EC, 1U);
    KeStallExecutionProcessor(2);
    mmio_w32(dsi0, 0x1EC, 0U);

    /* Enable CMD_DMA_TPG_EN (BIT1) | TPG_DMA_FIFO_MODE (BIT2) | custom pattern (3 << 16) */
    mmio_w32(dsi0, 0x15C, (1U << 1) | (1U << 2) | (3U << 16));

    /* Push command DWORDs; hardware requires an extra zero DWORD if DWORD count is odd */
    for (uint32_t i = 0; i < n_dwords; i++) {
        mmio_w32(dsi0, 0x17C, dwords[i]);
    }
    if (n_dwords & 1U) {
        mmio_w32(dsi0, 0x17C, 0U);
    }

    /* Configure DSI_COMMAND_MODE_DMA_CTRL (+0x3C): embedded mode (BIT28) + LPM (BIT26), no broadcast/wc_sel/pkt_type */
    uint32_t dma_ctrl = saved_dma_ctrl;
    dma_ctrl &= ~((1U << 31) | (1U << 30) | (1U << 29) | (1U << 24));
    dma_ctrl |= (1U << 28) | (1U << 26);
    mmio_w32(dsi0, 0x3C, dma_ctrl);

    /* Clear only CMD_MODE_DMA_DONE (BIT0) in DSI_INT_CTRL (+0x110), preserving enable bits & BIT24/BIT28 */
    uint32_t int_ctrl = mmio_r32(dsi0, 0x110);
    mmio_w32(dsi0, 0x110, (int_ctrl & 0xAAEEAAFEU) | 1U);

    uint32_t byte_len = n_dwords * 4U;
    mmio_w32(dsi0, 0x4C, byte_len);
    mmio_w32(dsi0, 0x90, 1U); /* DSI_CMD_MODE_DMA_SW_TRIGGER */

    int done = 0;
    uint32_t final_int = 0;
    for (uint32_t i = 0; i < 75; i++) {
        final_int = mmio_r32(dsi0, 0x110);
        if (final_int & 1U) {
            mmio_w32(dsi0, 0x110, (final_int & 0xAAEEAAFEU) | 1U);
            done = 1;
            break;
        }
        if (i < 25) {
            KeStallExecutionProcessor(20);
        } else {
            SleepMs(1);
        }
    }

    /* Disable TPG FIFO and restore DSI_COMMAND_MODE_DMA_CTRL */
    mmio_w32(dsi0, 0x15C, 0U);
    mmio_w32(dsi0, 0x1EC, 1U);
    KeStallExecutionProcessor(2);
    mmio_w32(dsi0, 0x1EC, 0U);
    mmio_w32(dsi0, 0x3C, saved_dma_ctrl);

    LogS("  DSI0 TPG-FIFO ["); LogS(label); LogS("] len="); LogDec(byte_len);
    LogS(" -> "); LogS(done ? "DONE" : "TIMEOUT");
    if (final_int & (1U << 24)) LogS(" (DSI_ERROR!)");
    LogS(" INT=0x"); LogHex(final_int, 8);
    LogS(" ST=0x"); LogHex(mmio_r32(dsi0, 0x08), 8); LogNL();
    return done;
}

static void Dsi0ReadDcsBta(volatile uint8_t *dsi0, uint8_t reg, uint32_t max_ret, const char *label) {
    if (!Dsi0WaitIdle(dsi0, label)) return;

    /* Clear previous readback registers */
    mmio_w32(dsi0, 0x1D4, 1U);
    KeStallExecutionProcessor(5);
    mmio_w32(dsi0, 0x1D4, 0U);
    KeStallExecutionProcessor(5);

    /* 1. SET_MAXIMUM_RETURN_PACKET_SIZE (0x37) short write */
    uint32_t set_max[1] = { 0x80370000U | (max_ret & 0xFFFFU) };
    (void)Dsi0SendTpgFifoPkt(dsi0, set_max, 1, "MAX_RET_SZ(0x37)");

    /* 2. MIPI_DSI_DCS_READ (0x06) with BTA (BIT5) | LAST (BIT7) = 0xA0 */
    uint32_t saved_dma_ctrl = mmio_r32(dsi0, 0x3C);
    mmio_w32(dsi0, 0x15C, 0U);
    mmio_w32(dsi0, 0x1EC, 1U);
    KeStallExecutionProcessor(2);
    mmio_w32(dsi0, 0x1EC, 0U);
    mmio_w32(dsi0, 0x15C, (1U << 1) | (1U << 2) | (3U << 16));
    mmio_w32(dsi0, 0x17C, 0xA0060000U | (uint32_t)reg);
    mmio_w32(dsi0, 0x17C, 0U);

    uint32_t dma_ctrl = saved_dma_ctrl;
    dma_ctrl &= ~((1U << 31) | (1U << 30) | (1U << 29) | (1U << 24));
    dma_ctrl |= (1U << 28) | (1U << 26);
    mmio_w32(dsi0, 0x3C, dma_ctrl);

    uint32_t int_ctrl = mmio_r32(dsi0, 0x110);
    mmio_w32(dsi0, 0x110, (int_ctrl & 0xAAEEAAFEU) | 1U | (1U << 20));
    mmio_w32(dsi0, 0x4C, 4U);
    mmio_w32(dsi0, 0x90, 1U);

    int done = 0;
    uint32_t final_int = 0;
    for (uint32_t i = 0; i < 100; i++) {
        final_int = mmio_r32(dsi0, 0x110);
        if ((final_int & 1U) && (final_int & (1U << 20))) {
            done = 1;
            break;
        }
        SleepMs(1);
    }
    mmio_w32(dsi0, 0x110, (final_int & 0xAAEEAAFEU) | 1U | (1U << 20));

    uint32_t rdbk_ctrl = mmio_r32(dsi0, 0x1D4);
    uint32_t rd_cnt = (rdbk_ctrl >> 16) & 0xFFFFU;
    uint32_t d0 = mmio_r32(dsi0, 0x6C);
    uint32_t d1 = mmio_r32(dsi0, 0x70);
    uint32_t dt0 = mmio_r32(dsi0, 0x7C);

    mmio_w32(dsi0, 0x15C, 0U);
    mmio_w32(dsi0, 0x1EC, 1U);
    KeStallExecutionProcessor(2);
    mmio_w32(dsi0, 0x1EC, 0U);
    mmio_w32(dsi0, 0x3C, saved_dma_ctrl);

    LogS("  DSI0 BTA ["); LogS(label); LogS(" reg=0x"); LogHex(reg, 2);
    LogS("]: done="); LogDec(done);
    LogS(" INT=0x"); LogHex(final_int, 8);
    LogS(" RDBK_CTRL=0x"); LogHex(rdbk_ctrl, 8);
    LogS(" (cnt="); LogDec(rd_cnt);
    LogS(") DT0=0x"); LogHex(dt0, 8);
    LogS(" D0=0x"); LogHex(d0, 8);
    LogS(" D1=0x"); LogHex(d1, 8); LogNL();
}

static void DumpSpmiPm8550Gpio11(void) {
    volatile uint8_t *core = (volatile uint8_t *)MapPhys(0x0C400000ULL, 0x4000U, "SPMI_CORE");
    volatile uint8_t *cnfg = (volatile uint8_t *)MapPhys(0x0C42D000ULL, 0x4000U, "SPMI_CNFG");
    volatile uint8_t *obs  = (volatile uint8_t *)MapPhys(0x0C440000ULL, 0x80000U, "SPMI_OBS");
    if (core && cnfg && obs) {
        uint32_t cap = mmio_r32(core, 0x04U);
        uint32_t max_ch = cap & 0xFFFU;
        if (max_ch > 1024U) max_ch = 1024U;
        uint32_t apid = 0xFFFFFFFFU;
        uint32_t owner = 0xFFU;
        for (uint32_t i = 0; i < max_ch; i++) {
            uint32_t ch = mmio_r32(cnfg, 0x0800U + i * 4U);
            uint32_t ppid = (ch >> 8) & 0xFFFU;
            if (ppid == 0x192U) { /* PM8550 SID=1 GPIO11 (0x9200) */
                apid = i;
                owner = mmio_r32(cnfg, 0x2400U + i * 4U) & 0xFFU;
                break;
            }
        }
        LogS("  SPMI PM8550 GPIO11 (PPID 0x192 display_panel_avdd): APID=0x");
        LogHex(apid, 3); LogS(" Owner="); LogDec(owner);
        if (apid != 0xFFFFFFFFU) {
            static const uint8_t regs[6] = { 0x08U, 0x40U, 0x41U, 0x42U, 0x45U, 0x46U };
            LogS(" Reg[08,40,41,42,45,46]=");
            for (uint32_t r = 0; r < 6U; r++) {
                uint32_t cmd = (0x39U << 24) | ((uint32_t)regs[r] << 16);
                uint32_t ch_off = apid * 0x1000U;
                mmio_w32(obs, ch_off + 0x00U, cmd);
                uint8_t val = 0;
                int ok = 0;
                for (uint32_t t = 0; t < 500U; t++) {
                    if (mmio_r32(obs, ch_off + 0x08U) & 1U) {
                        val = (uint8_t)(mmio_r32(obs, ch_off + 0x18U) & 0xFFU);
                        ok = 1;
                        break;
                    }
                    KeStallExecutionProcessor(1U);
                }
                if (r > 0) LogC('/');
                if (ok) LogHex(val, 2); else LogS("??");
            }
        }
        LogNL();
    }
    if (obs)  MmUnmapIoSpace((PVOID)obs, 0x80000U);
    if (cnfg) MmUnmapIoSpace((PVOID)cnfg, 0x4000U);
    if (core) MmUnmapIoSpace((PVOID)core, 0x4000U);
}

typedef struct {
    const char *name;
    uint32_t pin;
} GPIO_ITEM;

static const GPIO_ITEM g_panel_gpios[] = {
    { "panel_vci(3.0V)",  20  },
    { "panel_vdd3(1.8V)", 21  },
    { "TE_VSYNC",         86  },
    { "UB_CON_DET",       108 },
    { "Reset_N",          125 },
    { "ESD_IRQ1",         186 },
    { "panel_vddr(1.3V)", 187 },
};

/* Force a real .reloc section for /DYNAMICBASE ARM64 WDM driver loading */
static const void * const gRelocAnchors[] = {
    (const void *)g_panel_gpios,
    (const void *)LogOpen,
};

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    (void)DriverObject;
    (void)RegistryPath;
    (void)gRelocAnchors;

    LogOpen();
    uint32_t action = ReadActionMask();
    LogS("=== s23disp_diag.sys v1.0.0.4 started (ActionMask=0x");
    LogHex(action, 2); LogS(") ==="); LogNL();

    volatile uint8_t *tlmm   = (volatile uint8_t *)MapPhys(0x0F100000ULL, 0x100000, "TLMM");
    volatile uint8_t *dispcc = (volatile uint8_t *)MapPhys(0x0AF00000ULL, 0x020000, "DISP_CC");
    volatile uint8_t *mdss   = NULL;

    if (!tlmm || !dispcc) {
        LogS("FATAL: TLMM or DISP_CC map failed!"); LogNL();
        goto cleanup;
    }

    /* 1. Dump Panel TLMM GPIOs */
    LogS("--- Panel TLMM GPIOs ---"); LogNL();
    for (uint32_t i = 0; i < sizeof(g_panel_gpios) / sizeof(g_panel_gpios[0]); i++) {
        uint32_t off = g_panel_gpios[i].pin * 0x1000U;
        LogS("  GPIO"); LogDec(g_panel_gpios[i].pin);
        LogS(" ["); LogS(g_panel_gpios[i].name); LogS("]: cfg=0x");
        LogHex(mmio_r32(tlmm, off + 0x00), 8);
        LogS(" io=0x"); LogHex(mmio_r32(tlmm, off + 0x04), 8); LogNL();
    }

    /* 2. Full 5-register SM8550 G116Gate in DISP_CC (0x0AF00000) */
    LogS("--- DISP_CC Power & Clock Gate (Full G116Gate) ---"); LogNL();
    uint32_t core_bcr  = mmio_r32(dispcc, 0x8000);
    uint32_t gdscr     = mmio_r32(dispcc, 0x9000);
    uint32_t gdsc_cfg  = mmio_r32(dispcc, 0x9004);
    uint32_t ahb_cbcr  = mmio_r32(dispcc, 0x80A4);
    uint32_t mdp_cbcr  = mmio_r32(dispcc, 0x800C);
    uint32_t pclk_cbcr = mmio_r32(dispcc, 0x8004);
    uint32_t pclk1_cb  = mmio_r32(dispcc, 0x8008);
    uint32_t esc_cbcr  = mmio_r32(dispcc, 0x8038);
    uint32_t byte_cbcr = mmio_r32(dispcc, 0x8028);
    uint32_t bintf_cb  = mmio_r32(dispcc, 0x802C);

    LogS("  CORE_BCR(0x8000)=0x"); LogHex(core_bcr, 8);
    LogS(" MDSS_CORE_GDSCR(0x9000)=0x"); LogHex(gdscr, 8);
    LogS(" CFG(0x9004)=0x"); LogHex(gdsc_cfg, 8); LogNL();
    LogS("  AHB(0x80A4)=0x"); LogHex(ahb_cbcr, 8);
    LogS(" MDP(0x800C)=0x"); LogHex(mdp_cbcr, 8);
    LogS(" PCLK0(0x8004)=0x"); LogHex(pclk_cbcr, 8);
    LogS(" PCLK1(0x8008)=0x"); LogHex(pclk1_cb, 8); LogNL();
    LogS("  ESC0(0x8038)=0x"); LogHex(esc_cbcr, 8);
    LogS(" BYTE0(0x8028)=0x"); LogHex(byte_cbcr, 8);
    LogS(" BYTE0_INTF(0x802C)=0x"); LogHex(bintf_cb, 8); LogNL();

    int gate_ok = ((core_bcr & 1U) == 0) &&
                  ((mdp_cbcr & 0x80000001U) == 1U) &&
                  ((ahb_cbcr & 0x80000001U) == 1U) &&
                  ((gdscr & 0x80000007U) == 0x80000000U) &&
                  ((gdsc_cfg & 0x18000U) == 0x10000U);
    LogS("  G116Gate = "); LogS(gate_ok ? "OPEN (SAFE TO READ MDSS)" : "CLOSED (MDSS OFF/UNSAFE - ABORTING MMIO!)"); LogNL();
    if (!gate_ok) {
        goto cleanup;
    }

    /* 3. Safe Read-Only Dump of MDSS (0x0AE00000, 0x100000) */
    mdss = (volatile uint8_t *)MapPhys(0x0AE00000ULL, 0x100000, "MDSS");
    if (!mdss) goto cleanup;

    uint32_t hw_ver = mmio_r32(mdss, 0x00000);
    uint32_t hw_irq = mmio_r32(mdss, 0x00010);
    LogS("--- MDSS_MDP / CTL_0 / INTF_1 / PP_0 / DSC_0(1.2) / DSI0_CTRL (Sample 1) ---"); LogNL();
    LogS("  MDSS_HW_VERSION=0x"); LogHex(hw_ver, 8);
    LogS(" HW_INTR_STATUS=0x"); LogHex(hw_irq, 8); LogNL();

    /* CTL_0 @ +0x16000 (CTL_STATUS is +0x64 per sde_hw_ctl.c) */
    LogS("  CTL_0: TOP(0x14)=0x"); LogHex(mmio_r32(mdss, 0x16014), 8);
    LogS(" FLUSH(0x18)=0x"); LogHex(mmio_r32(mdss, 0x16018), 8);
    LogS(" STATUS(0x64)=0x"); LogHex(mmio_r32(mdss, 0x16064), 8);
    LogS(" ACT_CFG(0x00)=0x"); LogHex(mmio_r32(mdss, 0x16000), 8);
    LogS(" INTF_ACT(0xF4)=0x"); LogHex(mmio_r32(mdss, 0x160F4), 8);
    LogS(" DSC_ACT(0xE8)=0x"); LogHex(mmio_r32(mdss, 0x160E8), 8); LogNL();

    /* Scan SSPP VIG0..3 (+0x05000..+0x0B000) and DMA0..3 (+0x25000..+0x2B000) for active scanout */
    {
        static const struct { uint32_t off; const char *nm; } sspps[] = {
            { 0x05000, "VIG0" }, { 0x07000, "VIG1" }, { 0x09000, "VIG2" }, { 0x0B000, "VIG3" },
            { 0x25000, "DMA0" }, { 0x27000, "DMA1" }, { 0x29000, "DMA2" }, { 0x2B000, "DMA3" },
        };
        LogS("--- Active MDP SSPP Scanout Sources ---"); LogNL();
        for (uint32_t i = 0; i < sizeof(sspps)/sizeof(sspps[0]); i++) {
            uint32_t sz = mmio_r32(mdss, sspps[i].off + 0x00);
            uint32_t a0 = mmio_r32(mdss, sspps[i].off + 0x14);
            uint32_t st = mmio_r32(mdss, sspps[i].off + 0x24);
            uint32_t fm = mmio_r32(mdss, sspps[i].off + 0x30);
            if (sz != 0 || a0 != 0) {
                LogS("  "); LogS(sspps[i].nm);
                LogS(": SRC_SZ=0x"); LogHex(sz, 8);
                LogS(" SRC0_ADDR=0x"); LogHex(a0, 8);
                LogS(" STRIDE=0x"); LogHex(st, 8);
                LogS(" FMT=0x"); LogHex(fm, 8); LogNL();
            }
        }
    }

    /* Sample UEFI Display Reserved framebuffer at 0xB8000000 (top, mid, taskbar) */
    {
        volatile uint8_t *fb = (volatile uint8_t *)MapPhys(0xB8000000ULL, 0x00A00000, "FB_B8000000");
        if (fb) {
            uint32_t nz = 0;
            for (uint32_t off = 0; off < 1080U * 2340U * 4U; off += 4096U) {
                if (mmio_r32(fb, off) != 0) nz++;
            }
            LogS("  FB 0xB8000000: nonZeroPages="); LogDec(nz);
            LogS(" px[0]=0x"); LogHex(mmio_r32(fb, 0), 8);
            LogS(" px[mid]=0x"); LogHex(mmio_r32(fb, (1170U * 1080U + 540U) * 4U), 8);
            LogS(" px[bot]=0x"); LogHex(mmio_r32(fb, (2320U * 1080U + 540U) * 4U), 8); LogNL();
            MmUnmapIoSpace((PVOID)fb, 0x00A00000);
        }
    }

    /* INTF_1 @ +0x36000 */
    uint32_t flc_en = mmio_r32(mdss, 0x360A8);
    uint32_t fc1    = mmio_r32(mdss, 0x360AC);
    uint32_t lc1    = mmio_r32(mdss, 0x360B0);
    uint32_t ur1    = mmio_r32(mdss, 0x36268);
    uint32_t ist1   = mmio_r32(mdss, 0x3626C);
    uint32_t te_en  = mmio_r32(mdss, 0x36284);
    uint32_t te_vs  = mmio_r32(mdss, 0x36288);
    uint32_t te_ic1 = mmio_r32(mdss, 0x36298);
    uint32_t te_lc1 = mmio_r32(mdss, 0x362B0);
    uint32_t ar_cfg = mmio_r32(mdss, 0x362B4);
    uint32_t ar_st1 = mmio_r32(mdss, 0x362C0);

    LogS("  INTF_1: FLC_EN(0xA8)=0x"); LogHex(flc_en, 8);
    LogS(" FRAME_CNT(0xAC)=0x"); LogHex(fc1, 8);
    LogS(" LINE_CNT(0xB0)=0x"); LogHex(lc1, 8);
    LogS(" UNDERRUN=0x"); LogHex(ur1, 8);
    LogS(" STATUS=0x"); LogHex(ist1, 8); LogNL();
    LogS("  INTF_1 TE: EN(0x284)=0x"); LogHex(te_en, 8);
    LogS(" SYNC_VSYNC(0x288)=0x"); LogHex(te_vs, 8);
    LogS(" HEIGHT(0x28C)=0x"); LogHex(mmio_r32(mdss, 0x3628C), 8);
    LogS(" WRCNT(0x290)=0x"); LogHex(mmio_r32(mdss, 0x36290), 8);
    LogS(" INIT(0x294)=0x"); LogHex(mmio_r32(mdss, 0x36294), 8); LogNL();
    LogS("  INTF_1 TE2: INT_CNT(0x298)=0x"); LogHex(te_ic1, 8);
    LogS(" THRESH(0x29C)=0x"); LogHex(mmio_r32(mdss, 0x3629C), 8);
    LogS(" START_POS(0x2A0)=0x"); LogHex(mmio_r32(mdss, 0x362A0), 8);
    LogS(" OUT_LN(0x2AC)=0x"); LogHex(mmio_r32(mdss, 0x362AC), 8);
    LogS(" TE_LINE(0x2B0)=0x"); LogHex(te_lc1, 8); LogNL();
    LogS("  INTF_1 AR: CFG(0x2B4)=0x"); LogHex(ar_cfg, 8);
    LogS(" STATUS(0x2C0)=0x"); LogHex(ar_st1, 8); LogNL();

    /* PP_0 @ +0x6A000 (4-byte block), DSC 1.2 @ +0x81000 (CMN +0x0, ENC_0 +0x100, CTL_0 +0xF00) */
    LogS("  PP_0(+0x00)=0x"); LogHex(mmio_r32(mdss, 0x6A000), 8);
    LogS("  DSC_0: CMN(0x00)=0x"); LogHex(mmio_r32(mdss, 0x81000), 8);
    LogS(" ENC0_ST(0x104)=0x"); LogHex(mmio_r32(mdss, 0x81104), 8);
    LogS(" ENC0_CFG(0x130)=0x"); LogHex(mmio_r32(mdss, 0x81130), 8);
    LogS(" CTL0(0xF00/F04)=0x"); LogHex(mmio_r32(mdss, 0x81F00), 8);
    LogS("/0x"); LogHex(mmio_r32(mdss, 0x81F04), 8); LogNL();

    /* DSI0_CTRL @ +0x94000 */
    volatile uint8_t *dsi0 = mdss + 0x94000;
    uint32_t dsi_ctrl = mmio_r32(dsi0, 0x04);
    LogS("  DSI0: CTRL(0x04)=0x"); LogHex(dsi_ctrl, 8);
    LogS(" STATUS(0x08)=0x"); LogHex(mmio_r32(dsi0, 0x08), 8);
    LogS(" FIFO(0x0C)=0x"); LogHex(mmio_r32(dsi0, 0x0C), 8);
    LogS(" DMA_CTRL(0x3C)=0x"); LogHex(mmio_r32(dsi0, 0x3C), 8); LogNL();
    LogS("  DSI0: TRIG(0x84)=0x"); LogHex(mmio_r32(dsi0, 0x84), 8);
    LogS(" LANE_ST(0xA8)=0x"); LogHex(mmio_r32(dsi0, 0xA8), 8);
    LogS(" INT_CTRL(0x110)=0x"); LogHex(mmio_r32(dsi0, 0x110), 8);
    LogS(" CLK_CTRL(0x11C)=0x"); LogHex(mmio_r32(dsi0, 0x11C), 8);
    LogS(" DSC_CMD(0x2A8)=0x"); LogHex(mmio_r32(dsi0, 0x2A8), 8); LogNL();

    /* Sample 2 after 100 ms to prove whether TE / Auto-Refresh are ticking */
    SleepMs(100);
    uint32_t fc2    = mmio_r32(mdss, 0x360AC);
    uint32_t te_ic2 = mmio_r32(mdss, 0x36298);
    uint32_t te_lc2 = mmio_r32(mdss, 0x362B0);
    uint32_t ar_st2 = mmio_r32(mdss, 0x362C0);
    LogS("--- 100 ms Delta Check ---"); LogNL();
    LogS("  FRAME_CNT: 0x"); LogHex(fc1, 8); LogS(" -> 0x"); LogHex(fc2, 8);
    LogS(" (delta="); LogDec((int64_t)(fc2 - fc1)); LogS(")"); LogNL();
    LogS("  INT_CNT:   0x"); LogHex(te_ic1, 8); LogS(" -> 0x"); LogHex(te_ic2, 8); LogNL();
    LogS("  TE_LINE:   0x"); LogHex(te_lc1, 8); LogS(" -> 0x"); LogHex(te_lc2, 8); LogNL();
    LogS("  AR_STATUS: 0x"); LogHex(ar_st1, 8); LogS(" -> 0x"); LogHex(ar_st2, 8); LogNL();

    /* Opt-in Action 0x02 (runs BEFORE Action 0x01 to avoid MDP/DMA interleave contention):
     * Send S6E3FAC Wake / Max Brightness via DSI0 TPG DMA FIFO (zero DDR DMA, no SMMU fault). */
    if (action & 0x02U) {
        LogS("--- ACTION 0x02: Sending S6E3FAC Brightness/Wake DCS over DSI0 TPG-FIFO ---"); LogNL();
        if ((esc_cbcr & 0x80000001U) != 1U || (dsi_ctrl & 0x05U) != 0x05U) {
            LogS("  SKIP: DSI0 CTRL/CMD_MODE or ESC0(0x8038) clock not active!"); LogNL();
        } else {
            /* Pause Auto-Refresh during DCS transmission so MDP and DMA do not collide */
            uint32_t prev_ar = mmio_r32(mdss, 0x362B4);
            if (prev_ar & 0x80000000U) {
                mmio_w32(mdss, 0x362B4, prev_ar & ~0x80000000U);
                SleepMs(20);
            }

            /* Embedded QCOM DSI packet DWORDs (little-endian):
             *   Long write:  b0=wc_lo, b1=wc_hi, b2=0x29, b3=0xC0 (LAST|LONG), payload padded with 0xFF
             *   Short write: b0=cmd,   b1=0x00,  b2=0x05, b3=0x80 (LAST) */
            static const uint32_t p_unlock9f[2] = { 0xC0290003U, 0xFFA5A59FU }; /* 9F A5 A5 */
            static const uint32_t p_unlockf0[2] = { 0xC0290003U, 0xFF5A5AF0U }; /* F0 5A 5A */
            static const uint32_t p_slpout[1]   = { 0x80050011U };              /* 11 (Sleep Out) */
            static const uint32_t p_wrctrld[2]  = { 0xC0290002U, 0xFFFF2853U }; /* 53 28 */
            static const uint32_t p_dbvmax[2]   = { 0xC0290003U, 0xFFFF0751U }; /* 51 07 FF */
            static const uint32_t p_latchf7[2]  = { 0xC0290002U, 0xFFFF0FF7U }; /* F7 0F */
            static const uint32_t p_dispon[1]   = { 0x80050029U };              /* 29 (Display On) */
            static const uint32_t p_lockf0[2]   = { 0xC0290003U, 0xFFA5A5F0U }; /* F0 A5 A5 */
            static const uint32_t p_lock9f[2]   = { 0xC0290003U, 0xFF5A5A9FU }; /* 9F 5A 5A */

            Dsi0SendTpgFifoPkt(dsi0, p_unlock9f, 2, "9F A5 A5 (unlock L3)");
            Dsi0SendTpgFifoPkt(dsi0, p_unlockf0, 2, "F0 5A 5A (unlock L2)");
            if (action & 0x04U) {
                Dsi0SendTpgFifoPkt(dsi0, p_slpout, 1, "11 (Sleep Out)");
                SleepMs(120);
            }
            Dsi0SendTpgFifoPkt(dsi0, p_wrctrld,  2, "53 28 (WRCTRLD)");
            Dsi0SendTpgFifoPkt(dsi0, p_dbvmax,   2, "51 07 FF (DBV max)");
            Dsi0SendTpgFifoPkt(dsi0, p_latchf7,  2, "F7 0F (update latch)");
            Dsi0SendTpgFifoPkt(dsi0, p_dispon,   1, "29 (Display On)");
            Dsi0SendTpgFifoPkt(dsi0, p_lockf0,   2, "F0 A5 A5 (lock L2)");
            Dsi0SendTpgFifoPkt(dsi0, p_lock9f,   2, "9F 5A 5A (lock L3)");

            /* Restore Auto-Refresh if it was previously armed and Action 0x01 won't re-arm it */
            if ((prev_ar & 0x80000000U) && !(action & 0x01U)) {
                mmio_w32(mdss, 0x362B4, prev_ar);
                LogS("  Restored prior AR_CFG=0x"); LogHex(prev_ar, 8); LogNL();
            }
        }
    }

    /* Opt-in Action 0x10: Read-only DSI0 BTA DCS readback + PM8550 GPIO11 (AVDD) */
    if (action & 0x10U) {
        LogS("--- ACTION 0x10: Panel DSI0 BTA Readback & PM8550 GPIO11 ---"); LogNL();
        DumpSpmiPm8550Gpio11();
        if ((esc_cbcr & 0x80000001U) == 1U && (dsi_ctrl & 0x05U) == 0x05U) {
            uint32_t prev_ar = mmio_r32(mdss, 0x362B4);
            if (prev_ar & 0x80000000U) {
                mmio_w32(mdss, 0x362B4, prev_ar & ~0x80000000U);
                SleepMs(20);
            }
            Dsi0ReadDcsBta(dsi0, 0x04U, 4U, "RDDID");
            Dsi0ReadDcsBta(dsi0, 0x0AU, 2U, "POWER_MODE");
            Dsi0ReadDcsBta(dsi0, 0x0EU, 2U, "SIGNAL_MODE");
            Dsi0ReadDcsBta(dsi0, 0x52U, 2U, "BRIGHTNESS");
            Dsi0ReadDcsBta(dsi0, 0x54U, 2U, "CTRL_DISP");
            Dsi0ReadDcsBta(dsi0, 0x05U, 2U, "ERR_CNT");
            if ((prev_ar & 0x80000000U) && !(action & 0x20U) && !(action & 0x01U)) {
                mmio_w32(mdss, 0x362B4, prev_ar);
            }
        }
    }

    /* Opt-in Action 0x20: S6E3FAC Wake using MIPI_DSI_DCS_LONG_WRITE (0x39) + 0x15 + F1/FC unlock */
    if (action & 0x20U) {
        LogS("--- ACTION 0x20: S6E3FAC Wake (DT=0x39 DCS Long Write + F1/FC + BTA Verify) ---"); LogNL();
        if ((esc_cbcr & 0x80000001U) != 1U || (dsi_ctrl & 0x05U) != 0x05U) {
            LogS("  SKIP: DSI0 CTRL/CMD_MODE or ESC0(0x8038) clock not active!"); LogNL();
        } else {
            uint32_t prev_ar = mmio_r32(mdss, 0x362B4);
            if (prev_ar & 0x80000000U) {
                mmio_w32(mdss, 0x362B4, prev_ar & ~0x80000000U);
                SleepMs(20);
            }
            /* Note: DT=0x39 = MIPI_DSI_DCS_LONG_WRITE, DT=0x15 = DCS_SHORT_WRITE_1P, DT=0x05 = DCS_SHORT_WRITE_0P */
            static const uint32_t p_u9f[2]  = { 0xC0390003U, 0xFFA5A59FU }; /* 9F A5 A5 */
            static const uint32_t p_uf0[2]  = { 0xC0390003U, 0xFF5A5AF0U }; /* F0 5A 5A */
            static const uint32_t p_uf1[2]  = { 0xC0390003U, 0xFF5A5AF1U }; /* F1 5A 5A */
            static const uint32_t p_ufc[2]  = { 0xC0390003U, 0xFF5A5AFCU }; /* FC 5A 5A */
            static const uint32_t p_slp[1]  = { 0x80050011U };              /* 11 (Sleep Out) */
            static const uint32_t p_te[1]   = { 0x80150035U };              /* 35 00 (TE ON) */
            static const uint32_t p_ctl[1]  = { 0x80152053U };              /* 53 20 (BCTRL on, dimming off) */
            static const uint32_t p_dbv[2]  = { 0xC0390003U, 0xFFFF0751U }; /* 51 07 FF (DBV max) */
            static const uint32_t p_f7[1]   = { 0x80150FF7U };              /* F7 0F (update latch) */
            static const uint32_t p_on[1]   = { 0x80050029U };              /* 29 (Display On) */
            static const uint32_t p_lfc[2]  = { 0xC0390003U, 0xFFA5A5FCU }; /* FC A5 A5 */
            static const uint32_t p_lf1[2]  = { 0xC0390003U, 0xFFA5A5F1U }; /* F1 A5 A5 */
            static const uint32_t p_lf0[2]  = { 0xC0390003U, 0xFFA5A5F0U }; /* F0 A5 A5 */
            static const uint32_t p_l9f[2]  = { 0xC0390003U, 0xFF5A5A9FU }; /* 9F 5A 5A */

            Dsi0SendTpgFifoPkt(dsi0, p_u9f, 2, "0x39 9F A5 A5");
            Dsi0SendTpgFifoPkt(dsi0, p_uf0, 2, "0x39 F0 5A 5A");
            Dsi0SendTpgFifoPkt(dsi0, p_uf1, 2, "0x39 F1 5A 5A");
            Dsi0SendTpgFifoPkt(dsi0, p_ufc, 2, "0x39 FC 5A 5A");
            if (action & 0x04U) {
                Dsi0SendTpgFifoPkt(dsi0, p_slp, 1, "0x05 11 (Sleep Out)");
                SleepMs(120);
            }
            Dsi0SendTpgFifoPkt(dsi0, p_te,  1, "0x15 35 00 (TE ON)");
            Dsi0SendTpgFifoPkt(dsi0, p_ctl, 1, "0x15 53 20 (WRCTRLD)");
            Dsi0SendTpgFifoPkt(dsi0, p_dbv, 2, "0x39 51 07 FF (DBV max)");
            Dsi0SendTpgFifoPkt(dsi0, p_f7,  1, "0x15 F7 0F (latch)");
            Dsi0SendTpgFifoPkt(dsi0, p_on,  1, "0x05 29 (Display On)");
            SleepMs(20);
            Dsi0SendTpgFifoPkt(dsi0, p_lfc, 2, "0x39 FC A5 A5");
            Dsi0SendTpgFifoPkt(dsi0, p_lf1, 2, "0x39 F1 A5 A5");
            Dsi0SendTpgFifoPkt(dsi0, p_lf0, 2, "0x39 F0 A5 A5");
            Dsi0SendTpgFifoPkt(dsi0, p_l9f, 2, "0x39 9F 5A 5A");

            Dsi0ReadDcsBta(dsi0, 0x0AU, 2U, "POST_POWER_MODE");
            Dsi0ReadDcsBta(dsi0, 0x52U, 2U, "POST_BRIGHTNESS");
            Dsi0ReadDcsBta(dsi0, 0x54U, 2U, "POST_CTRL_DISP");

            if ((prev_ar & 0x80000000U) && !(action & 0x01U)) {
                mmio_w32(mdss, 0x362B4, prev_ar);
                LogS("  Restored prior AR_CFG=0x"); LogHex(prev_ar, 8); LogNL();
            }
        }
    }

    /* Opt-in Action 0x01: Safe Auto-Refresh (60 Hz) re-arm + CTL_0_START ONLY if unarmed/stalled or forced (0x08) */
    if (action & 0x01U) {
        LogS("--- ACTION 0x01: Checking Auto-Refresh & CTL_0_START ---"); LogNL();
        int ar_running = ((te_en & 1U) != 0) && ((ar_cfg & 0x80000000U) != 0) &&
                         ((ar_st2 != ar_st1) || (te_ic2 != te_ic1));
        if ((dsi_ctrl & 0x05U) != 0x05U || (te_vs & (1U << 19)) == 0) {
            LogS("  SKIP: DSI0_CTRL(0x04) or SYNC_VSYNC(0x288 bit 19) not configured!"); LogNL();
        } else if (ar_running && !(action & 0x08U) && !(action & 0x02U)) {
            LogS("  SKIP: Auto-Refresh already active & ticking (set ActionMask|=0x08 to force kick)."); LogNL();
        } else {
            mmio_w32(mdss, 0x36284, 1U);
            mmio_w32(mdss, 0x362B4, 0x80000002U);
            /* Only kick CTL_0_START if Auto-Refresh wasn't already ticking or explicitly forced */
            if (!ar_running || (action & 0x08U)) {
                mmio_w32(mdss, 0x1601C, 1U);
            }
            SleepMs(100);
            uint32_t fc3    = mmio_r32(mdss, 0x360AC);
            uint32_t te_ic3 = mmio_r32(mdss, 0x36298);
            uint32_t ar_st3 = mmio_r32(mdss, 0x362C0);
            LogS("  Post-kick (+100ms): FRAME_CNT=0x"); LogHex(fc3, 8);
            LogS(" INT_CNT=0x"); LogHex(te_ic3, 8);
            LogS(" AR_CFG=0x"); LogHex(mmio_r32(mdss, 0x362B4), 8);
            LogS(" AR_ST=0x"); LogHex(ar_st3, 8); LogNL();
        }
    }

cleanup:
    if (mdss)   MmUnmapIoSpace((PVOID)mdss,   0x100000);
    if (dispcc) MmUnmapIoSpace((PVOID)dispcc, 0x020000);
    if (tlmm)   MmUnmapIoSpace((PVOID)tlmm,   0x100000);

    LogS("=== s23disp_diag.sys v1.0.0.2 finished ==="); LogNL();
    LogClose();
    return STATUS_UNSUCCESSFUL;
}
