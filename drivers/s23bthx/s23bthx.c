/*
 * s23bthx.c - Native Windows 11 ARM64 WDM BTHX Transport LowerFilter Driver
 * Target: Samsung Galaxy S23 (dm1q / SM-S911B, SM8550-AC) QCA6490 Bluetooth 5.3
 *
 * Device Stack (per phone's C:\Windows\INF\bth.inf [BthMini.NT]):
 *   Upper / Function Driver : BthMini.sys (+ bthport.sys class extension)
 *   LowerFilter             : s23bthx.sys (ROOT\S23BTHX)
 *
 * Hardware:
 *   - QUP2 Wrapper @ 0x008C0000 (QUP_HW_VER_REG = 0x30090000)
 *   - QUP2 SE6 (UART14) @ 0x00898000 (FIFO mode, txDepth=48, rxDepth=48)
 *   - TLMM @ 0x0F100000 (GPIO76..79 fn1 UART14, GPIO81 BT_EN, GPIO204 XO_CLK)
 *   - GCC @ 0x00100000 (SE6 RCG 7.3728 MHz -> 51.2 MHz = 3,200,000 baud)
 *   - Shared QCA6490 rails + PMK8550 GPIO3 32kHz + AOSS QMP wlan_pdc
 *   - Firmware files: \??\C:\Windows\System32\drivers\{hpbtfw21.tlv,hpnv21g.bin}
 *                     (with fallback to \??\C:\Users\Public\*)
 *
 * BthMini.sys Binary-Verified ABI:
 *   - Sends ALL IOCTL_BTHX_* via IRP_MJ_DEVICE_CONTROL (0x0E), METHOD_NEITHER:
 *       InputBuffer  = IrpSp->Parameters.DeviceIoControl.Type3InputBuffer
 *       OutputBuffer = Irp->UserBuffer
 *   - IOCTL_BTHX_GET_VERSION (0x00410403): OUT ULONG = 1, Info = 4
 *   - IOCTL_BTHX_SET_VERSION (0x00410407): IN  ULONG == 1, Info = 0
 *   - IOCTL_BTHX_QUERY_CAPABILITIES (0x0041040B): OUT 16B BTHX_CAPABILITIES, Info = 16
 *   - IOCTL_BTHX_WRITE_HCI (0x0041040F): IN BTHX_HCI_READ_WRITE_CONTEXT,
 *       OUT ULONG = ctx->DataLen, Info = 4 (sizeof(ULONG))
 *   - IOCTL_BTHX_READ_HCI (0x00410413): IN ULONG pktType (0x04 Event or 0x02 ACL),
 *       OUT BTHX_HCI_READ_WRITE_CONTEXT (Type, DataLen, Data[]), Info = 5 + DataLen
 *
 * Built with clang-18 --target=aarch64-pc-windows-msvc + lld-link (/DRIVER:WDM /TSAWARE:NO).
 */

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * Minimal Verified Windows 11 ARM64 WDM + BTHX ABI Definitions
 * ============================================================================ */

typedef int32_t  NTSTATUS;
typedef uint8_t  BOOLEAN, UCHAR, KIRQL;
typedef int8_t   CHAR;
typedef uint16_t USHORT, WCHAR;
typedef int16_t  CSHORT;
typedef uint32_t ULONG, *PULONG;
typedef int32_t  LONG;
typedef uint64_t ULONGLONG, ULONG_PTR, SIZE_T, KSPIN_LOCK;
typedef int64_t  LONGLONG;
typedef void     VOID, *PVOID, *HANDLE;
typedef const uint16_t *PCWSTR;

#define FALSE 0
#define TRUE  1

#define NT_SUCCESS(s)                   ((NTSTATUS)(s) >= 0)
#define STATUS_SUCCESS                  ((NTSTATUS)0x00000000L)
#define STATUS_PENDING                  ((NTSTATUS)0x00000103L)
#define STATUS_MORE_PROCESSING_REQUIRED ((NTSTATUS)0xC0000016L)
#define STATUS_UNSUCCESSFUL             ((NTSTATUS)0xC0000001L)
#define STATUS_INVALID_PARAMETER        ((NTSTATUS)0xC000000DL)
#define STATUS_NO_SUCH_DEVICE           ((NTSTATUS)0xC000000EL)
#define STATUS_INVALID_DEVICE_REQUEST   ((NTSTATUS)0xC0000010L)
#define STATUS_END_OF_FILE              ((NTSTATUS)0xC0000011L)
#define STATUS_BUFFER_TOO_SMALL         ((NTSTATUS)0xC0000023L)
#define STATUS_INSUFFICIENT_RESOURCES   ((NTSTATUS)0xC000009AL)
#define STATUS_DEVICE_NOT_READY         ((NTSTATUS)0xC00000A3L)
#define STATUS_NOT_SUPPORTED            ((NTSTATUS)0xC00000BBL)
#define STATUS_CANCELLED                ((NTSTATUS)0xC0000120L)

#define IRP_MJ_CREATE                   0x00
#define IRP_MJ_CLOSE                    0x02
#define IRP_MJ_CLEANUP                  0x12
#define IRP_MJ_DEVICE_CONTROL           0x0E
#define IRP_MJ_INTERNAL_DEVICE_CONTROL  0x0F
#define IRP_MJ_POWER                    0x16
#define IRP_MJ_SYSTEM_CONTROL           0x17
#define IRP_MJ_PNP                      0x1B
#define IRP_MJ_MAXIMUM_FUNCTION         0x1B

#define IRP_MN_START_DEVICE             0x00
#define IRP_MN_QUERY_REMOVE_DEVICE      0x01
#define IRP_MN_REMOVE_DEVICE            0x02
#define IRP_MN_CANCEL_REMOVE_DEVICE     0x03
#define IRP_MN_STOP_DEVICE              0x04
#define IRP_MN_QUERY_STOP_DEVICE        0x05
#define IRP_MN_CANCEL_STOP_DEVICE       0x06
#define IRP_MN_SURPRISE_REMOVAL         0x17

#define SL_PENDING_RETURNED             0x01
#define SL_INVOKE_ON_CANCEL             0x20
#define SL_INVOKE_ON_SUCCESS            0x40
#define SL_INVOKE_ON_ERROR              0x80

#define FILE_DEVICE_BLUETOOTH           0x00000041u
#define FILE_DEVICE_SECURE_OPEN         0x00000100u
#define DO_DEVICE_INITIALIZING          0x00000080u
#define DO_POWER_PAGABLE                0x00002000u
#define IO_NO_INCREMENT                 0

#define MmNonCached                     0u
#define KernelMode                      0
#define Executive                       0
#define NotificationEvent               0
#define SynchronizationEvent            1

#define OBJ_CASE_INSENSITIVE            0x00000040u
#define OBJ_KERNEL_HANDLE               0x00000200u
#define FILE_GENERIC_READ               0x00120089u
#define FILE_GENERIC_WRITE              0x00120116u
#define FILE_SHARE_READ                 0x00000001u
#define FILE_SHARE_WRITE                0x00000002u
#define FILE_OPEN                       0x00000001u
#define FILE_OPEN_IF                    0x00000003u
#define FILE_WRITE_THROUGH              0x00000002u
#define FILE_SYNCHRONOUS_IO_NONALERT    0x00000020u
#define FILE_ATTRIBUTE_NORMAL           0x00000080u

/* BTHX DDI constants from bthxddi.h */
#define BTHX_DDI_VERSION_1              0x00000001u
#define IOCTL_BTHX_GET_VERSION          0x00410403u
#define IOCTL_BTHX_SET_VERSION          0x00410407u
#define IOCTL_BTHX_QUERY_CAPABILITIES   0x0041040Bu
#define IOCTL_BTHX_WRITE_HCI            0x0041040Fu
#define IOCTL_BTHX_READ_HCI             0x00410413u

#define HciPacketCommand                0x01u
#define HciPacketAclData                0x02u
#define HciPacketEvent                  0x04u
#define ScoSupportNone                  0u
#define ScoSupportHCIBypass             2u

typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    ULONG  _pad;
    WCHAR *Buffer;
} UNICODE_STRING, *PUNICODE_STRING;
_Static_assert(sizeof(UNICODE_STRING) == 16, "UNICODE_STRING size");

typedef union _LARGE_INTEGER {
    struct { uint32_t LowPart; int32_t HighPart; };
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER, PHYSICAL_ADDRESS;

typedef struct _IO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        PVOID    Pointer;
    };
    ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;
_Static_assert(sizeof(IO_STATUS_BLOCK) == 16, "IO_STATUS_BLOCK size");

typedef struct _OBJECT_ATTRIBUTES {
    ULONG           Length;
    ULONG           _pad0;
    HANDLE          RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG           Attributes;
    ULONG           _pad1;
    PVOID           SecurityDescriptor;
    PVOID           SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;
_Static_assert(sizeof(OBJECT_ATTRIBUTES) == 48, "OBJECT_ATTRIBUTES size");

typedef struct __attribute__((aligned(8))) _KEVENT {
    ULONGLONG Opaque[4];
} KEVENT, *PKEVENT;

typedef struct __attribute__((aligned(8))) _IO_REMOVE_LOCK {
    ULONGLONG Opaque[4];
} IO_REMOVE_LOCK, *PIO_REMOVE_LOCK;

typedef struct _BTHX_CAPABILITIES {
    ULONG   MaxAclTransferInSize;
    ULONG   ScoSupport;
    ULONG   MaxScoChannels;
    BOOLEAN IsDeviceIdleCapable;
    BOOLEAN IsDeviceWakeCapable;
    UCHAR   _pad[2];
} BTHX_CAPABILITIES, *PBTHX_CAPABILITIES;
_Static_assert(sizeof(BTHX_CAPABILITIES) == 16, "BTHX_CAPABILITIES size");

#pragma pack(push, 1)
typedef struct _BTHX_HCI_READ_WRITE_CONTEXT {
    ULONG DataLen;
    UCHAR Type;
    UCHAR Data[1];
} BTHX_HCI_READ_WRITE_CONTEXT, *PBTHX_HCI_READ_WRITE_CONTEXT;
#pragma pack(pop)
_Static_assert(sizeof(BTHX_HCI_READ_WRITE_CONTEXT) == 6, "BTHX_HCI_READ_WRITE_CONTEXT size");
_Static_assert(offsetof(BTHX_HCI_READ_WRITE_CONTEXT, Type) == 4, "BTHX context Type off");
_Static_assert(offsetof(BTHX_HCI_READ_WRITE_CONTEXT, Data) == 5, "BTHX context Data off");

typedef struct _DEVICE_OBJECT DEVICE_OBJECT, *PDEVICE_OBJECT;
typedef struct _DRIVER_OBJECT DRIVER_OBJECT, *PDRIVER_OBJECT;
typedef struct _IRP IRP, *PIRP;

typedef NTSTATUS (*PDRIVER_DISPATCH)(PDEVICE_OBJECT, PIRP);
typedef NTSTATUS (*PDRIVER_ADD_DEVICE)(PDRIVER_OBJECT, PDEVICE_OBJECT);
typedef VOID     (*PDRIVER_UNLOAD)(PDRIVER_OBJECT);
typedef VOID     (*PDRIVER_CANCEL)(PDEVICE_OBJECT, PIRP);
typedef NTSTATUS (*PIO_COMPLETION_ROUTINE)(PDEVICE_OBJECT, PIRP, PVOID);
typedef VOID     (*PKSTART_ROUTINE)(PVOID);

typedef struct _DRIVER_EXTENSION {
    PDRIVER_OBJECT     DriverObject;
    PDRIVER_ADD_DEVICE AddDevice;
    ULONG              Count;
    ULONG              _pad;
    UNICODE_STRING     ServiceKeyName;
} DRIVER_EXTENSION, *PDRIVER_EXTENSION;

struct _DRIVER_OBJECT {
    CSHORT            Type;
    CSHORT            Size;
    ULONG             _pad0;
    PDEVICE_OBJECT    DeviceObject;
    ULONG             Flags;
    ULONG             _pad1;
    PVOID             DriverStart;
    ULONG             DriverSize;
    ULONG             _pad2;
    PVOID             DriverSection;
    PDRIVER_EXTENSION DriverExtension;
    UNICODE_STRING    DriverName;
    PUNICODE_STRING   HardwareDatabase;
    PVOID             FastIoDispatch;
    PVOID             DriverInit;
    PVOID             DriverStartIo;
    PDRIVER_UNLOAD    DriverUnload;
    PDRIVER_DISPATCH  MajorFunction[IRP_MJ_MAXIMUM_FUNCTION + 1];
};
_Static_assert(offsetof(DRIVER_OBJECT, DriverExtension) == 0x30, "DriverExtension off");
_Static_assert(offsetof(DRIVER_OBJECT, DriverUnload) == 0x68, "DriverUnload off");
_Static_assert(offsetof(DRIVER_OBJECT, MajorFunction) == 0x70, "MajorFunction off");

struct _DEVICE_OBJECT {
    UCHAR _pad0[0x30];
    ULONG Flags;
    ULONG Characteristics;
    PVOID Vpb;
    PVOID DeviceExtension;
};
_Static_assert(offsetof(DEVICE_OBJECT, Flags) == 0x30, "DO.Flags off");
_Static_assert(offsetof(DEVICE_OBJECT, DeviceExtension) == 0x40, "DO.DeviceExtension off");

typedef struct _IO_STACK_LOCATION {
    UCHAR  MajorFunction;
    UCHAR  MinorFunction;
    UCHAR  Flags;
    UCHAR  Control;
    ULONG  _pad0;
    union {
        struct {
            ULONG OutputBufferLength;
            ULONG _pad1;
            ULONG InputBufferLength;
            ULONG _pad2;
            ULONG IoControlCode;
            ULONG _pad3;
            PVOID Type3InputBuffer;
        } DeviceIoControl;
        UCHAR RawParams[32];
    } Parameters;
    PDEVICE_OBJECT         DeviceObject;
    PVOID                  FileObject;
    PIO_COMPLETION_ROUTINE CompletionRoutine;
    PVOID                  Context;
} IO_STACK_LOCATION, *PIO_STACK_LOCATION;
_Static_assert(sizeof(IO_STACK_LOCATION) == 0x48, "IO_STACK_LOCATION size");
_Static_assert(offsetof(IO_STACK_LOCATION, Parameters.DeviceIoControl.Type3InputBuffer) == 0x20, "Type3InputBuffer off");

struct _IRP {
    UCHAR              _pad0[0x18];
    PVOID              SystemBuffer;
    UCHAR              _pad1[0x10];
    IO_STATUS_BLOCK    IoStatus;
    CHAR               RequestorMode;
    BOOLEAN            PendingReturned;
    CHAR               StackCount;
    CHAR               CurrentLocation;
    BOOLEAN            Cancel;
    KIRQL              CancelIrql;
    UCHAR              _pad2[0x22];
    PDRIVER_CANCEL     CancelRoutine;
    PVOID              UserBuffer;
    UCHAR              _pad3[0x40];
    PIO_STACK_LOCATION CurrentStackLocation;
};
_Static_assert(offsetof(IRP, IoStatus) == 0x30, "IRP.IoStatus off");
_Static_assert(offsetof(IRP, Cancel) == 0x44, "IRP.Cancel off");
_Static_assert(offsetof(IRP, CancelRoutine) == 0x68, "IRP.CancelRoutine off");
_Static_assert(offsetof(IRP, UserBuffer) == 0x70, "IRP.UserBuffer off");
_Static_assert(offsetof(IRP, CurrentStackLocation) == 0xB8, "IRP.CurrentStackLocation off");

/* Kernel Imports (strictly verified against phone's ntoskrnl.exe) */
__declspec(dllimport) PVOID    MmMapIoSpace(PHYSICAL_ADDRESS PhysicalAddress, SIZE_T NumberOfBytes, ULONG CacheType);
__declspec(dllimport) VOID     MmUnmapIoSpace(PVOID BaseAddress, SIZE_T NumberOfBytes);
__declspec(dllimport) NTSTATUS PsCreateSystemThread(HANDLE *ThreadHandle, ULONG DesiredAccess, POBJECT_ATTRIBUTES ObjAttr, HANDLE ProcessHandle, PVOID ClientId, PKSTART_ROUTINE StartRoutine, PVOID StartContext);
__declspec(dllimport) NTSTATUS PsTerminateSystemThread(NTSTATUS ExitStatus);
__declspec(dllimport) NTSTATUS ObReferenceObjectByHandle(HANDLE Handle, ULONG DesiredAccess, PVOID ObjectType, CHAR AccessMode, PVOID *Object, PVOID HandleInfo);
__declspec(dllimport) VOID     ObfDereferenceObject(PVOID Object);
__declspec(dllimport) NTSTATUS KeWaitForSingleObject(PVOID Object, ULONG WaitReason, CHAR WaitMode, BOOLEAN Alertable, PLARGE_INTEGER Timeout);
__declspec(dllimport) NTSTATUS KeDelayExecutionThread(CHAR WaitMode, BOOLEAN Alertable, PLARGE_INTEGER Interval);
__declspec(dllimport) VOID     KeStallExecutionProcessor(ULONG MicroSeconds);
__declspec(dllimport) VOID     KeInitializeSpinLock(KSPIN_LOCK *SpinLock);
__declspec(dllimport) KIRQL    KeAcquireSpinLockRaiseToDpc(KSPIN_LOCK *SpinLock);
__declspec(dllimport) VOID     KeReleaseSpinLock(KSPIN_LOCK *SpinLock, KIRQL NewIrql);
__declspec(dllimport) VOID     KeInitializeEvent(PKEVENT Event, ULONG Type, BOOLEAN State);
__declspec(dllimport) LONG     KeSetEvent(PKEVENT Event, LONG Increment, BOOLEAN Wait);
__declspec(dllimport) NTSTATUS IoCreateDevice(PDRIVER_OBJECT DriverObject, ULONG DeviceExtensionSize, PUNICODE_STRING DeviceName, ULONG DeviceType, ULONG DeviceCharacteristics, BOOLEAN Exclusive, PDEVICE_OBJECT *DeviceObject);
__declspec(dllimport) VOID     IoDeleteDevice(PDEVICE_OBJECT DeviceObject);
__declspec(dllimport) PDEVICE_OBJECT IoAttachDeviceToDeviceStack(PDEVICE_OBJECT SourceDevice, PDEVICE_OBJECT TargetDevice);
__declspec(dllimport) VOID     IoDetachDevice(PDEVICE_OBJECT TargetDevice);
__declspec(dllimport) VOID     IoInitializeRemoveLockEx(PIO_REMOVE_LOCK Lock, ULONG AllocateTag, ULONG MaxLockedMinutes, ULONG HighWatermark, ULONG RemlockSize);
__declspec(dllimport) NTSTATUS IoAcquireRemoveLockEx(PIO_REMOVE_LOCK Lock, PVOID Tag, const char *File, ULONG Line, ULONG RemlockSize);
__declspec(dllimport) VOID     IoReleaseRemoveLockEx(PIO_REMOVE_LOCK Lock, PVOID Tag, ULONG RemlockSize);
__declspec(dllimport) VOID     IoReleaseRemoveLockAndWaitEx(PIO_REMOVE_LOCK Lock, PVOID Tag, ULONG RemlockSize);
__declspec(dllimport) NTSTATUS IofCallDriver(PDEVICE_OBJECT DeviceObject, PIRP Irp);
__declspec(dllimport) VOID     IofCompleteRequest(PIRP Irp, CHAR PriorityBoost);
__declspec(dllimport) VOID     IoAcquireCancelSpinLock(KIRQL *Irql);
__declspec(dllimport) VOID     IoReleaseCancelSpinLock(KIRQL Irql);
__declspec(dllimport) NTSTATUS PoCallDriver(PDEVICE_OBJECT DeviceObject, PIRP Irp);
__declspec(dllimport) VOID     PoStartNextPowerIrp(PIRP Irp);
__declspec(dllimport) NTSTATUS ZwCreateFile(HANDLE *FileHandle, ULONG DesiredAccess, POBJECT_ATTRIBUTES ObjAttr, PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocSize, ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength);
__declspec(dllimport) NTSTATUS ZwReadFile(HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset, ULONG *Key);
__declspec(dllimport) NTSTATUS ZwWriteFile(HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, const void *Buffer, ULONG Length, PLARGE_INTEGER ByteOffset, ULONG *Key);
__declspec(dllimport) NTSTATUS ZwClose(HANDLE Handle);
__declspec(dllimport) VOID     RtlInitUnicodeString(PUNICODE_STRING Dest, PCWSTR Source);

/* Inline WDM helpers */
static inline PIO_STACK_LOCATION IoGetCurrentIrpStackLocation(PIRP Irp) {
    return Irp->CurrentStackLocation;
}
static inline PIO_STACK_LOCATION IoGetNextIrpStackLocation(PIRP Irp) {
    return Irp->CurrentStackLocation - 1;
}
static inline VOID IoSkipCurrentIrpStackLocation(PIRP Irp) {
    Irp->CurrentLocation++;
    Irp->CurrentStackLocation++;
}
static inline VOID IoMarkIrpPending(PIRP Irp) {
    IoGetCurrentIrpStackLocation(Irp)->Control |= SL_PENDING_RETURNED;
}
static inline VOID MemCopy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}
static inline VOID MemZero(void *dst, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) d[i] = 0;
}
static inline VOID IoCopyCurrentIrpStackLocationToNext(PIRP Irp) {
    PIO_STACK_LOCATION cur = IoGetCurrentIrpStackLocation(Irp);
    PIO_STACK_LOCATION nxt = IoGetNextIrpStackLocation(Irp);
    MemCopy(nxt, cur, offsetof(IO_STACK_LOCATION, DeviceObject));
    nxt->Control = 0;
}
static inline VOID IoSetCompletionRoutine(PIRP Irp, PIO_COMPLETION_ROUTINE fn, PVOID ctx) {
    PIO_STACK_LOCATION nxt = IoGetNextIrpStackLocation(Irp);
    nxt->CompletionRoutine = fn;
    nxt->Context = ctx;
    nxt->Control = SL_INVOKE_ON_SUCCESS | SL_INVOKE_ON_ERROR | SL_INVOKE_ON_CANCEL;
}
static inline PDRIVER_CANCEL IoSetCancelRoutine(PIRP Irp, PDRIVER_CANCEL fn) {
    return __atomic_exchange_n(&Irp->CancelRoutine, fn, __ATOMIC_SEQ_CST);
}

/* ============================================================================
 * S23 Hardware Physical Addresses & Register Constants (from s23btprobe v1.0.0.3)
 * ============================================================================ */
#define PA_GCC_BASE             0x00100000ULL
#define SZ_GCC                  0x00060000U
#define PA_IPCC_BASE            0x00408000ULL
#define SZ_IPCC                 0x00001000U
#define PA_QUP2_SE6_BASE        0x00898000ULL
#define SZ_QUP2_SE6             0x00004000U
#define PA_QUP2_WRAP_BASE       0x008C0000ULL
#define SZ_QUP2_WRAP            0x00001000U
#define PA_QMP_BASE             0x0C300000ULL
#define SZ_QMP                  0x00001000U
#define PA_SPMI_CORE_BASE       0x0C400000ULL
#define SZ_SPMI_CORE            0x00004000U
#define PA_SPMI_CNFG_BASE       0x0C42D000ULL
#define SZ_SPMI_CNFG            0x00004000U
#define PA_SPMI_OBSRVR_BASE     0x0C440000ULL
#define SZ_SPMI_OBSRVR          0x00080000U
#define PA_SPMI_CHNLS_BASE      0x0C500000ULL
#define SZ_SPMI_CHNLS           0x00400000U
#define PA_TLMM_BASE            0x0F100000ULL
#define SZ_TLMM                 0x00100000U
#define PA_RSC_DRV2_BASE        0x17A20000ULL
#define SZ_RSC_DRV2             0x00002000U
#define PA_CMD_DB_BASE          0x81C60000ULL
#define SZ_CMD_DB               0x00020000U

#define GCC_GPLL0_MODE          0x00000U
#define GCC_QUP2_S6_CBCR        0x1E754U
#define GCC_QUP2_S6_CMD_RCGR    0x1E760U
#define GCC_QUP2_S6_CFG_RCGR    0x1E764U
#define GCC_QUP2_S6_M           0x1E768U
#define GCC_QUP2_S6_N           0x1E76CU
#define GCC_QUP2_S6_D           0x1E770U
#define GCC_QUP2_CORE_2X_CBCR   0x23004U
#define GCC_QUP2_M_AHB_CBCR     0x233CCU
#define GCC_QUP2_S_AHB_CBCR     0x233D0U
#define GCC_QUP2_CORE_CBCR      0x233D4U
#define GCC_APCS_CLK_VOTE_2     0x52010U
#define GCC_APCS_GPLL_VOTE      0x52018U

#define QUP2_VOTE_CORE          (1U << 0)
#define QUP2_VOTE_S_AHB         (1U << 1)
#define QUP2_VOTE_M_AHB         (1U << 2)
#define QUP2_VOTE_CORE_2X       (1U << 3)
#define QUP2_VOTE_S6            (1U << 10)
#define QUP2_VOTE_ALL           (QUP2_VOTE_CORE | QUP2_VOTE_S_AHB | QUP2_VOTE_M_AHB | QUP2_VOTE_CORE_2X | QUP2_VOTE_S6)

#define SE6_RCG_CFG_7P3728M     0x00102601U
#define SE6_RCG_M_7P3728M       0x00000180U
#define SE6_RCG_N_7P3728M       0x0000C476U
#define SE6_RCG_D_7P3728M       0x0000C2F6U

#define SE6_RCG_CFG_51P2M       0x00102601U
#define SE6_RCG_M_51P2M         0x00000040U
#define SE6_RCG_N_51P2M         0x0000FEC8U
#define SE6_RCG_D_51P2M         0x0000FE88U

#define QUP_HW_VER_REG          0x004U
#define QUP_SE_VERSION_2_5      0x20050000U

#define GENI_FORCE_DEFAULT_REG  0x020U
#define GENI_OUTPUT_CTRL        0x024U
#define GENI_CGC_CTRL           0x028U
#define SE_GENI_STATUS          0x040U
#define GENI_SER_M_CLK_CFG      0x048U
#define GENI_SER_S_CLK_CFG      0x04CU
#define GENI_IF_DISABLE_RO      0x064U
#define GENI_FW_REVISION_RO     0x068U
#define SE_GENI_CLK_SEL         0x07CU
#define SE_UART_LOOPBACK_CFG    0x22CU
#define SE_GENI_BYTE_GRAN       0x254U
#define SE_GENI_DMA_MODE_EN     0x258U
#define SE_UART_TX_TRANS_CFG    0x25CU
#define SE_GENI_TX_PACKING_CFG0 0x260U
#define SE_GENI_TX_PACKING_CFG1 0x264U
#define SE_UART_TX_WORD_LEN     0x268U
#define SE_UART_TX_STOP_BIT_LEN 0x26CU
#define SE_UART_TX_TRANS_LEN    0x270U
#define SE_UART_RX_TRANS_CFG    0x280U
#define SE_GENI_RX_PACKING_CFG0 0x284U
#define SE_GENI_RX_PACKING_CFG1 0x288U
#define SE_UART_RX_WORD_LEN     0x28CU
#define SE_UART_RX_STALE_CNT    0x294U
#define SE_UART_TX_PARITY_CFG   0x2A4U
#define SE_UART_RX_PARITY_CFG   0x2A8U
#define SE_UART_MANUAL_RFR      0x2ACU
#define SE_GENI_M_CMD0          0x600U
#define SE_GENI_M_CMD_CTRL_REG  0x604U
#define SE_GENI_M_IRQ_STATUS    0x610U
#define SE_GENI_M_IRQ_EN        0x614U
#define SE_GENI_M_IRQ_CLEAR     0x618U
#define SE_GENI_S_CMD0          0x630U
#define SE_GENI_S_CMD_CTRL_REG  0x634U
#define SE_GENI_S_IRQ_STATUS    0x640U
#define SE_GENI_S_IRQ_EN        0x644U
#define SE_GENI_S_IRQ_CLEAR     0x648U
#define SE_GENI_TX_FIFOn        0x700U
#define SE_GENI_RX_FIFOn        0x780U
#define SE_GENI_TX_FIFO_STATUS  0x800U
#define SE_GENI_RX_FIFO_STATUS  0x804U
#define SE_GENI_RX_WATERMARK    0x810U
#define SE_GENI_RX_RFR_WM       0x814U
#define SE_GENI_IOS             0x908U
#define SE_DMA_TX_IRQ_CLR       0xC44U
#define SE_DMA_RX_IRQ_CLR       0xD44U
#define SE_GSI_EVENT_EN         0xE18U
#define SE_IRQ_EN               0xE1CU
#define SE_HW_PARAM_0           0xE24U
#define SE_HW_PARAM_1           0xE28U
#define SE_DMA_GENERAL_CFG      0xE30U

#define GENI_SE_UART            2U
#define M_GENI_CMD_ACTIVE       (1U << 0)
#define S_GENI_CMD_ACTIVE       (1U << 12)
#define M_GENI_CMD_ABORT        (1U << 1)
#define M_GENI_CMD_CANCEL       (1U << 2)
#define S_GENI_CMD_ABORT        (1U << 1)
#define S_GENI_CMD_CANCEL       (1U << 2)
#define M_CMD_DONE_EN           (1U << 0)
#define M_CMD_CANCEL_EN         (1U << 4)
#define M_CMD_ABORT_EN          (1U << 5)
#define S_CMD_CANCEL_EN         (1U << 4)
#define S_CMD_ABORT_EN          (1U << 5)
#define UART_CTS_MASK           (1U << 1)
#define UART_MANUAL_RFR_EN      (1U << 31)
#define UART_RFR_NOT_READY      (1U << 1)
#define UART_START_TX           (1U << 27)
#define UART_START_READ         (1U << 27)
#define PACKING_4X8_LSB_CFG0    0x0004380EU
#define PACKING_4X8_LSB_CFG1    0x000C3E0EU

#define QCA_BAUDRATE_3200000    0x11U
#define QCA_SKIP_EVT_NONE       0U
#define QCA_SKIP_EVT_VSE        1U
#define QCA_SKIP_EVT_VSE_CC     3U
#define TLV_TYPE_PATCH          1U
#define TLV_TYPE_NVM            2U
#define EDL_PATCH_TLV_REQ_CMD   0x1EU
#define EDL_TAG_ID_BDADDR       2U
#define EDL_TAG_ID_HCI          17U
#define EDL_TAG_ID_DEEP_SLEEP   27U

#define GPIO_UART14_CTS         76U
#define GPIO_UART14_RTS         77U
#define GPIO_UART14_TX          78U
#define GPIO_UART14_RX          79U
#define GPIO_WLAN_EN            80U
#define GPIO_BT_EN              81U
#define GPIO_SW_CTRL            82U
#define GPIO_XO_CLK             204U
#define PPID_PMK8550_GPIO3      0x0BAU

/* ============================================================================
 * Bounded 64 KB Write-Through Logger + Static .bss Scratch Buffers
 * ============================================================================ */
#define LOG_CAP 65536U
static char     g_LogBuf[LOG_CAP];
static uint32_t g_LogLen = 0;

#define FW_BUF_CAP 184320U
static uint8_t  g_FwBuf[FW_BUF_CAP];

static void LogChar(char c) {
    if (g_LogLen + 1U < LOG_CAP) g_LogBuf[g_LogLen++] = c;
}
static void LogStr(const char *s) {
    while (*s) LogChar(*s++);
}
static void LogHex32(uint32_t v) {
    static const char hx[] = "0123456789ABCDEF";
    LogStr("0x");
    for (int i = 28; i >= 0; i -= 4) LogChar(hx[(v >> i) & 0xFU]);
}
static void LogHex8(uint8_t v) {
    static const char hx[] = "0123456789ABCDEF";
    LogChar(hx[(v >> 4) & 0xFU]);
    LogChar(hx[v & 0xFU]);
}
static void LogU32(uint32_t v) {
    char tmp[16];
    int n = 0;
    if (v == 0U) { LogChar('0'); return; }
    while (v > 0U && n < 15) { tmp[n++] = (char)('0' + (v % 10U)); v /= 10U; }
    while (n > 0) LogChar(tmp[--n]);
}

static void FlushLogFile(void) {
    static const WCHAR kLogPath[] = L"\\??\\C:\\Users\\Public\\s23bthx.log";
    UNICODE_STRING us;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE hFile = NULL;
    LARGE_INTEGER allocSz, zeroOff;

    RtlInitUnicodeString(&us, kLogPath);
    MemZero(&oa, sizeof(oa));
    oa.Length = sizeof(oa);
    oa.ObjectName = &us;
    oa.Attributes = OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE;

    allocSz.QuadPart = (LONGLONG)LOG_CAP;
    zeroOff.QuadPart = 0;
    if (!NT_SUCCESS(ZwCreateFile(&hFile, FILE_GENERIC_WRITE, &oa, &iosb, &allocSz,
                                 FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 FILE_OPEN_IF, FILE_WRITE_THROUGH | FILE_SYNCHRONOUS_IO_NONALERT,
                                 NULL, 0))) {
        return;
    }
    for (uint32_t p = g_LogLen; p < LOG_CAP; p++) g_LogBuf[p] = ' ';
    (void)ZwWriteFile(hFile, NULL, NULL, NULL, &iosb, g_LogBuf, LOG_CAP, &zeroOff, NULL);
    (void)ZwClose(hFile);
}

/* ============================================================================
 * Device Extension, Packet Ring Buffers, and IRP Queues
 * ============================================================================ */
#define RING_SLOTS      64U
#define MAX_PKT_BYTES   1280U
#define MAX_PENDING_IRP 32U

typedef struct _HCI_PKT_SLOT {
    uint16_t Len;
    uint8_t  Data[MAX_PKT_BYTES];
} HCI_PKT_SLOT;

typedef struct _HCI_PKT_RING {
    HCI_PKT_SLOT Slots[RING_SLOTS];
    uint32_t     Head;
    uint32_t     Tail;
    uint32_t     Count;
} HCI_PKT_RING;

typedef struct _IRP_QUEUE {
    PIRP     Irps[MAX_PENDING_IRP];
    uint32_t Count;
} IRP_QUEUE;

typedef enum _RX_PARSE_STATE {
    RX_STATE_IDLE = 0,
    RX_STATE_HDR  = 1,
    RX_STATE_BODY = 2
} RX_PARSE_STATE;

typedef struct _S23BTHX_EXT {
    PDEVICE_OBJECT    Fdo;
    PDEVICE_OBJECT    Pdo;
    PDEVICE_OBJECT    LowerDevice;
    IO_REMOVE_LOCK    RemoveLock;

    volatile uint8_t *Gcc;
    volatile uint8_t *Tlmm;
    volatile uint8_t *QupWrap;
    volatile uint8_t *Se;
    uint32_t          TxDepth;
    uint32_t          QupVer;

    BOOLEAN           HwReady;
    volatile LONG     Stopping;

    KSPIN_LOCK        IoLock;       /* protects TxQueue, EvtQueue, AclQueue, EvtRing, AclRing */
    IRP_QUEUE         TxQueue;
    IRP_QUEUE         EvtQueue;
    IRP_QUEUE         AclQueue;
    HCI_PKT_RING      EvtRing;
    HCI_PKT_RING      AclRing;

    KEVENT            TxEvent;
    KEVENT            StopEvent;
    KEVENT            UartTxMutex;  /* SynchronizationEvent serializing UART TX */
    PVOID             TxThreadObj;
    PVOID             RxThreadObj;

    RX_PARSE_STATE    RxState;
    uint8_t           RxPktType;
    uint32_t          RxGot;
    uint32_t          RxNeed;
    uint8_t           RxPktBuf[MAX_PKT_BYTES];
    uint8_t           TxScratch[1280];

    uint32_t          LogIoctlCount;
    uint32_t          LogTxCount;
    uint32_t          LogRxCount;
} S23BTHX_EXT, *PS23BTHX_EXT;

static PS23BTHX_EXT g_ActiveExt = NULL;

/* ============================================================================
 * Low-Level MMIO & Delay Helpers
 * ============================================================================ */
static inline uint32_t Rd32(const volatile uint8_t *base, uint32_t off) {
    uint32_t v;
    __asm__ volatile("dmb sy" ::: "memory");
    v = *(const volatile uint32_t *)(base + off);
    __asm__ volatile("dmb sy" ::: "memory");
    return v;
}
static inline void Wr32(volatile uint8_t *base, uint32_t off, uint32_t val) {
    __asm__ volatile("dmb sy" ::: "memory");
    *(volatile uint32_t *)(base + off) = val;
    __asm__ volatile("dmb sy" ::: "memory");
}
static volatile uint8_t *MapPhys(uint64_t pa, size_t sz) {
    PHYSICAL_ADDRESS p;
    p.QuadPart = (LONGLONG)pa;
    return (volatile uint8_t *)MmMapIoSpace(p, sz, MmNonCached);
}
static void SleepMs(uint32_t ms) {
    LARGE_INTEGER interval;
    interval.QuadPart = -10000LL * (LONGLONG)ms;
    (void)KeDelayExecutionThread(KernelMode, FALSE, &interval);
}
static BOOLEAN PollMaskUs(const volatile uint8_t *base, uint32_t off, uint32_t mask, uint32_t want, uint32_t timeoutUs) {
    for (uint32_t t = 0; t < timeoutUs; t += 10U) {
        if ((Rd32(base, off) & mask) == want) return TRUE;
        KeStallExecutionProcessor(10U);
    }
    return ((Rd32(base, off) & mask) == want) ? TRUE : FALSE;
}
static BOOLEAN PollMaskMs(const volatile uint8_t *base, uint32_t off, uint32_t mask, uint32_t want, uint32_t timeoutMs) {
    for (uint32_t i = 0; i < 200U; i++) {
        if ((Rd32(base, off) & mask) == want) return TRUE;
        KeStallExecutionProcessor(5U);
    }
    for (uint32_t ms = 0; ms < timeoutMs; ms++) {
        if ((Rd32(base, off) & mask) == want) return TRUE;
        SleepMs(1U);
    }
    return ((Rd32(base, off) & mask) == want) ? TRUE : FALSE;
}
static BOOLEAN PollTxDoneFast(const volatile uint8_t *se, uint32_t timeoutMs) {
    /* Busy-poll up to 4.5 ms (~1440 B @ 3.2 Mbps) to avoid 15.6 ms OS timer-tick stalls on A2DP frames */
    for (uint32_t i = 0; i < 450U; i++) {
        if ((Rd32(se, SE_GENI_M_IRQ_STATUS) & M_CMD_DONE_EN) == M_CMD_DONE_EN) return TRUE;
        KeStallExecutionProcessor(10U);
    }
    for (uint32_t ms = 0; ms < timeoutMs; ms++) {
        if ((Rd32(se, SE_GENI_M_IRQ_STATUS) & M_CMD_DONE_EN) == M_CMD_DONE_EN) return TRUE;
        SleepMs(1U);
    }
    return ((Rd32(se, SE_GENI_M_IRQ_STATUS) & M_CMD_DONE_EN) == M_CMD_DONE_EN) ? TRUE : FALSE;
}

/* ============================================================================
 * File Loader (with DriverStore/System32 -> Public fallback)
 * ============================================================================ */
static BOOLEAN ReadFileToBuf(const WCHAR *ntPath, uint8_t *dst, uint32_t maxBytes, uint32_t *outBytes) {
    UNICODE_STRING us;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;
    HANDLE hFile = NULL;

    *outBytes = 0;
    RtlInitUnicodeString(&us, ntPath);
    MemZero(&oa, sizeof(oa));
    oa.Length = sizeof(oa);
    oa.ObjectName = &us;
    oa.Attributes = OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE;

    NTSTATUS st = ZwCreateFile(&hFile, FILE_GENERIC_READ, &oa, &iosb, NULL,
                               FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (!NT_SUCCESS(st)) return FALSE;

    uint32_t total = 0;
    while (total < maxBytes) {
        uint32_t chunk = maxBytes - total;
        if (chunk > 32768U) chunk = 32768U;
        LARGE_INTEGER off;
        off.QuadPart = (LONGLONG)total;
        iosb.Status = 0;
        iosb.Information = 0;
        st = ZwReadFile(hFile, NULL, NULL, NULL, &iosb, dst + total, chunk, &off, NULL);
        if (st == STATUS_END_OF_FILE) break;
        if (!NT_SUCCESS(st) || iosb.Information == 0) {
            (void)ZwClose(hFile);
            return FALSE;
        }
        total += (uint32_t)iosb.Information;
    }
    (void)ZwClose(hFile);
    *outBytes = total;
    return (total > 0U) ? TRUE : FALSE;
}

static BOOLEAN LoadFwWithFallback(const WCHAR *p1, const WCHAR *p2, uint8_t *dst, uint32_t cap, uint32_t *outLen) {
    if (ReadFileToBuf(p1, dst, cap, outLen)) return TRUE;
    return ReadFileToBuf(p2, dst, cap, outLen);
}

/* ============================================================================
 * SPMI / CMD_DB / RPMh / AOSS QMP Standalone Rail Bring-Up (only if WLAN off)
 * ============================================================================ */
static BOOLEAN SpmiFindApid(const volatile uint8_t *core, const volatile uint8_t *cnfg,
                            uint32_t targetPpid, uint32_t *apidOut, uint8_t *ownerOut) {
    if ((Rd32(core, 0x00U) & 0xF0000000U) != 0x70000000U) return FALSE;
    BOOLEAN found = FALSE;
    uint32_t bestApid = 0;
    uint8_t bestOwner = 0xFFU;
    for (uint32_t apid = 0; apid < 1024U; apid++) {
        uint32_t v = Rd32(core, 0x2000U + 4U * apid);
        if (v == 0U || ((v >> 8) & 0xFFFU) != targetPpid) continue;
        uint8_t owner = (uint8_t)(Rd32(cnfg, 4U * apid) & 0x7U);
        if (!found || owner == 0U) {
            found = TRUE; bestApid = apid; bestOwner = owner;
            if (owner == 0U) break;
        }
    }
    if (found) { *apidOut = bestApid; *ownerOut = bestOwner; }
    return found;
}

static BOOLEAN SpmiObsReadByte(volatile uint8_t *obs, uint32_t apid, uint8_t regOff, uint8_t *valOut) {
    uint32_t off = apid * 0x20U;
    if (off + 0x20U > SZ_SPMI_OBSRVR) return FALSE;
    Wr32(obs, off + 0x00U, (1U << 27) | ((uint32_t)regOff << 4));
    uint32_t st = 0;
    for (uint32_t i = 0; i < 1000U; i++) {
        st = Rd32(obs, off + 0x08U);
        if ((st & 1U) != 0U) break;
        KeStallExecutionProcessor(1U);
    }
    if ((st & 1U) == 0U || (st & 0xEU) != 0U) return FALSE;
    *valOut = (uint8_t)(Rd32(obs, off + 0x18U) & 0xFFU);
    return TRUE;
}

static BOOLEAN SpmiChnlWriteByte(volatile uint8_t *chnls, uint32_t apid, uint8_t owner, uint8_t regOff, uint8_t val) {
    if (owner != 0U) return FALSE;
    uint32_t off = apid * 0x1000U;
    if (off + 0x20U > SZ_SPMI_CHNLS) return FALSE;
    Wr32(chnls, off + 0x10U, (uint32_t)val);
    Wr32(chnls, off + 0x00U, ((uint32_t)regOff << 4));
    uint32_t st = 0;
    for (uint32_t i = 0; i < 1000U; i++) {
        st = Rd32(chnls, off + 0x08U);
        if ((st & 1U) != 0U) break;
        KeStallExecutionProcessor(1U);
    }
    return ((st & 1U) != 0U && (st & 0xEU) == 0U) ? TRUE : FALSE;
}

static uint32_t CmdDbLookup(const volatile uint8_t *db, const char *name) {
    if (!db || db[4] != 0xDB || db[5] != 0x30 || db[6] != 0x03 || db[7] != 0x0C) return 0;
    uint32_t nlen = 0;
    while (name[nlen] != 0 && nlen < 8U) nlen++;
    for (uint32_t i = 0; i < 8U; i++) {
        const volatile uint8_t *r = db + 8U + 16U * i;
        uint16_t slvId  = *(const volatile uint16_t *)(r + 0);
        uint16_t hdrOff = *(const volatile uint16_t *)(r + 2);
        uint16_t cnt    = *(const volatile uint16_t *)(r + 6);
        if (slvId == 0U || 144U + (uint32_t)hdrOff + 24U * (uint32_t)cnt > SZ_CMD_DB) break;
        const volatile uint8_t *e = db + 144U + hdrOff;
        for (uint32_t j = 0; j < cnt; j++, e += 24U) {
            BOOLEAN match = TRUE;
            for (uint32_t k = 0; k < 8U; k++) {
                uint8_t ch = (k < nlen) ? (uint8_t)name[k] : 0U;
                if (e[k] != ch) { match = FALSE; break; }
            }
            if (match) return *(const volatile uint32_t *)(e + 16);
        }
    }
    return 0;
}

static BOOLEAN RpmhWriteTcs0(volatile uint8_t *rscDrv2, uint32_t addr, uint32_t data) {
    if (!rscDrv2 || addr == 0U) return FALSE;
    volatile uint8_t *tcs = rscDrv2 + 0xD00U;
    if (!PollMaskMs(tcs, 0x28U, 1U, 1U, 10U)) return FALSE;

    uint32_t svIrqEn = Rd32(tcs, 0x00U);
    uint32_t svCmdEn = Rd32(tcs, 0x2CU);
    uint32_t svMsg0  = Rd32(tcs, 0x34U);
    uint32_t svAddr0 = Rd32(tcs, 0x38U);
    uint32_t svData0 = Rd32(tcs, 0x3CU);

    Wr32(tcs, 0x08U, 1U);
    if ((svIrqEn & 1U) == 0U) Wr32(tcs, 0x00U, svIrqEn | 1U);

    Wr32(tcs, 0x2CU, 0U);
    Wr32(tcs, 0x34U, 8U | 0x100U | 0x10000U);
    Wr32(tcs, 0x38U, addr);
    Wr32(tcs, 0x3CU, data);
    Wr32(tcs, 0x2CU, 1U);

    uint32_t ctrl = Rd32(tcs, 0x24U);
    ctrl &= ~(1U << 24); Wr32(tcs, 0x24U, ctrl); (void)Rd32(tcs, 0x24U);
    ctrl &= ~(1U << 16); Wr32(tcs, 0x24U, ctrl); (void)Rd32(tcs, 0x24U);
    ctrl |=  (1U << 16); Wr32(tcs, 0x24U, ctrl); (void)Rd32(tcs, 0x24U);
    ctrl |=  (1U << 24); Wr32(tcs, 0x24U, ctrl);

    BOOLEAN ok   = PollMaskMs(tcs, 0x04U, 1U, 1U, 100U);
    BOOLEAN idle = PollMaskMs(tcs, 0x28U, 1U, 1U, 10U);
    if (idle) {
        Wr32(tcs, 0x2CU, 0U);
        Wr32(tcs, 0x34U, svMsg0);
        Wr32(tcs, 0x38U, svAddr0);
        Wr32(tcs, 0x3CU, svData0);
        Wr32(tcs, 0x2CU, svCmdEn);
    }
    Wr32(tcs, 0x08U, 1U);
    if (svIrqEn != Rd32(tcs, 0x00U)) Wr32(tcs, 0x00U, svIrqEn);
    idle = PollMaskMs(tcs, 0x28U, 1U, 1U, 10U);
    return (ok && idle) ? TRUE : FALSE;
}

static void VoteRailNpm(const volatile uint8_t *db, volatile uint8_t *rsc, const char *name, uint32_t mv) {
    uint32_t a = CmdDbLookup(db, name);
    if (a == 0U) return;
    (void)RpmhWriteTcs0(rsc, a + 0x0U, mv);
    (void)RpmhWriteTcs0(rsc, a + 0x8U, 7U);
    (void)RpmhWriteTcs0(rsc, a + 0x4U, 1U);
}

static void EnsureQca6490SharedRails(void) {
    volatile uint8_t *spmiCore = MapPhys(PA_SPMI_CORE_BASE, SZ_SPMI_CORE);
    volatile uint8_t *spmiCnfg = MapPhys(PA_SPMI_CNFG_BASE, SZ_SPMI_CNFG);
    volatile uint8_t *spmiObs  = MapPhys(PA_SPMI_OBSRVR_BASE, SZ_SPMI_OBSRVR);
    volatile uint8_t *db       = MapPhys(PA_CMD_DB_BASE, SZ_CMD_DB);

    uint32_t apid = 0;
    uint8_t owner = 0xFFU;
    if (spmiCore && spmiCnfg && spmiObs &&
        SpmiFindApid(spmiCore, spmiCnfg, PPID_PMK8550_GPIO3, &apid, &owner) && owner == 0U) {
        uint8_t mode = 0, src = 0, en = 0;
        (void)SpmiObsReadByte(spmiObs, apid, 0x40U, &mode);
        (void)SpmiObsReadByte(spmiObs, apid, 0x44U, &src);
        (void)SpmiObsReadByte(spmiObs, apid, 0x46U, &en);
        if ((en & 0x80U) == 0U || mode != 0x01U || src != 0x02U) {
            volatile uint8_t *chnls = MapPhys(PA_SPMI_CHNLS_BASE, SZ_SPMI_CHNLS);
            if (chnls) {
                (void)SpmiChnlWriteByte(chnls, apid, owner, 0x40U, 0x01U);
                (void)SpmiChnlWriteByte(chnls, apid, owner, 0x41U, 0x00U);
                (void)SpmiChnlWriteByte(chnls, apid, owner, 0x42U, 0x05U);
                (void)SpmiChnlWriteByte(chnls, apid, owner, 0x44U, 0x02U);
                (void)SpmiChnlWriteByte(chnls, apid, owner, 0x45U, 0x03U);
                (void)SpmiChnlWriteByte(chnls, apid, owner, 0x46U, 0x80U);
                MmUnmapIoSpace((PVOID)chnls, SZ_SPMI_CHNLS);
                LogStr("  PMK8550 GPIO3 32kHz sleep clock enabled\r\n");
            }
        }
    }

    volatile uint8_t *rsc = MapPhys(PA_RSC_DRV2_BASE, SZ_RSC_DRV2);
    if (rsc && db) {
        static const char * const arcNames[] = { "xo.lvl", "cx.lvl", "mx.lvl" };
        for (uint32_t i = 0; i < 3U; i++) {
            uint32_t a = CmdDbLookup(db, arcNames[i]);
            if (a != 0U) (void)RpmhWriteTcs0(rsc, a, 3U);
        }
        static const char * const clkNames[] = { "clka1","clka2","clka3","clka4","clka6","clka7","clka8","rclkha1" };
        for (uint32_t i = 0; i < 8U; i++) {
            uint32_t a = CmdDbLookup(db, clkNames[i]);
            if (a != 0U) (void)RpmhWriteTcs0(rsc, a + 0x4U, 1U);
        }
        VoteRailNpm(db, rsc, "ldob15", 1800U);
        VoteRailNpm(db, rsc, "smpg2",  1012U);
        VoteRailNpm(db, rsc, "smpe4",  966U);
        VoteRailNpm(db, rsc, "smpg6",  1900U);
        VoteRailNpm(db, rsc, "smpg4",  1350U);
        SleepMs(10U);
        LogStr("  QCA6490 VRM rails voted\r\n");
    }
    if (rsc) MmUnmapIoSpace((PVOID)rsc, SZ_RSC_DRV2);

    volatile uint8_t *qmp  = MapPhys(PA_QMP_BASE, SZ_QMP);
    volatile uint8_t *ipcc = MapPhys(PA_IPCC_BASE, SZ_IPCC);
    if (qmp && ipcc && Rd32(qmp, 0x00U) == 0x4D41494CU && Rd32(qmp, 0x04U) == 1U) {
        uint32_t off = Rd32(qmp, 0x38U);
        if (off >= 0x3CU && off + 68U <= 0x400U) {
            BOOLEAN ready = TRUE;
            if (Rd32(qmp, 0x24U) != 0xFFFFU) {
                Wr32(qmp, 0x10U, Rd32(qmp, 0x0CU));
                Wr32(qmp, 0x24U, 0xFFFFU); Wr32(ipcc, 0x0CU, 0U);
                ready = PollMaskMs(qmp, 0x28U, 0xFFFFFFFFU, 0xFFFFU, 500U);
                if (ready) { Wr32(qmp, 0x2CU, 0xFFFFU); Wr32(ipcc, 0x0CU, 0U); ready = PollMaskMs(qmp, 0x14U, 0xFFFFFFFFU, 0xFFFFU, 500U); }
                if (ready) { Wr32(qmp, 0x18U, 0xFFFFU); Wr32(ipcc, 0x0CU, 0U); ready = PollMaskMs(qmp, 0x30U, 0xFFFFFFFFU, 0xFFFFU, 500U); }
            }
            if (ready) {
                static const char * const frames[10] = {
                    "{class: wlan_pdc, ss: rf, res: s4e.v, upval: 966}",
                    "{class: wlan_pdc, ss: rf, res: s4e.v, dwnval: 615}",
                    "{class: wlan_pdc, ss: rf, res: s4g.v, upval: 1350}",
                    "{class: wlan_pdc, ss: rf, res: s4g.v, dwnval: 945}",
                    "{class: wlan_pdc, ss: rf, res: s6g.v, upval: 1900}",
                    "{class: wlan_pdc, ss: rf, res: s6g.v, dwnval: 1825}",
                    "{class: wlan_pdc, ss: rf, res: s2g.m, enable: 1}",
                    "{class: wlan_pdc, ss: rf, res: s2g.v, enable: 1}",
                    "{class: wlan_pdc, ss: rf, res: s2g.v, upval: 1012}",
                    "{class: wlan_pdc, ss: rf, res: s2g.v, dwnval: 515}"
                };
                uint32_t sent = 0;
                for (uint32_t f = 0; f < 10U; f++) {
                    if (!PollMaskMs(qmp, off, 0xFFFFFFFFU, 0U, 100U)) break;
                    uint8_t buf[64];
                    MemZero(buf, sizeof(buf));
                    for (uint32_t i = 0; i < 63U && frames[f][i] != 0; i++) buf[i] = (uint8_t)frames[f][i];
                    for (uint32_t i = 0; i < 64U; i += 4U) {
                        uint32_t w = (uint32_t)buf[i] | ((uint32_t)buf[i+1U] << 8) | ((uint32_t)buf[i+2U] << 16) | ((uint32_t)buf[i+3U] << 24);
                        Wr32(qmp, off + 4U + i, w);
                    }
                    Wr32(qmp, off, 64U);
                    (void)Rd32(qmp, off);
                    Wr32(ipcc, 0x0CU, 0U);
                    if (!PollMaskMs(qmp, off, 0xFFFFFFFFU, 0U, 500U)) break;
                    sent++;
                }
                LogStr("  AOSS QMP wlan_pdc frames="); LogU32(sent); LogStr("/10\r\n");
            }
        }
    }
    if (ipcc) MmUnmapIoSpace((PVOID)ipcc, SZ_IPCC);
    if (qmp)  MmUnmapIoSpace((PVOID)qmp, SZ_QMP);
    if (db)   MmUnmapIoSpace((PVOID)db, SZ_CMD_DB);
    if (spmiObs)  MmUnmapIoSpace((PVOID)spmiObs, SZ_SPMI_OBSRVR);
    if (spmiCnfg) MmUnmapIoSpace((PVOID)spmiCnfg, SZ_SPMI_CNFG);
    if (spmiCore) MmUnmapIoSpace((PVOID)spmiCore, SZ_SPMI_CORE);
}

/* ============================================================================
 * TLMM, Clock & GENI UART14 Core
 * ============================================================================ */
static void SetTlmmPin(volatile uint8_t *tlmm, uint32_t pin, uint32_t cfg, int32_t outVal) {
    uint32_t off = pin * 0x1000U;
    if (outVal >= 0) Wr32(tlmm, off + 0x04U, outVal ? 2U : 0U);
    Wr32(tlmm, off + 0x00U, cfg);
}

static BOOLEAN IsQup2SAhbReady(uint32_t vote2, uint32_t sAhbCbcr) {
    if ((sAhbCbcr & 0x80000000U) == 0U) return TRUE;
    return (((vote2 & QUP2_VOTE_S_AHB) != 0U) && ((sAhbCbcr & 2U) != 0U)) ? TRUE : FALSE;
}

static BOOLEAN ProgramSe6Rcg(volatile uint8_t *gcc, uint32_t cfg, uint32_t m, uint32_t n, uint32_t d) {
    Wr32(gcc, GCC_QUP2_S6_M, m);
    Wr32(gcc, GCC_QUP2_S6_N, n);
    Wr32(gcc, GCC_QUP2_S6_D, d);
    Wr32(gcc, GCC_QUP2_S6_CFG_RCGR, cfg);
    Wr32(gcc, GCC_QUP2_S6_CMD_RCGR, Rd32(gcc, GCC_QUP2_S6_CMD_RCGR) | 1U);
    BOOLEAN ok = PollMaskMs(gcc, GCC_QUP2_S6_CMD_RCGR, 1U, 0U, 10U);
    if (!ok) {
        Wr32(gcc, GCC_QUP2_S6_CMD_RCGR, 3U);
        ok = PollMaskMs(gcc, GCC_QUP2_S6_CMD_RCGR, 1U, 0U, 10U);
        Wr32(gcc, GCC_QUP2_S6_CMD_RCGR, Rd32(gcc, GCC_QUP2_S6_CMD_RCGR) & ~2U);
    }
    return ok;
}

static void GeniStopRx(volatile uint8_t *se) {
    if ((Rd32(se, SE_GENI_STATUS) & S_GENI_CMD_ACTIVE) == 0U) return;
    Wr32(se, SE_GENI_S_CMD_CTRL_REG, S_GENI_CMD_CANCEL);
    if (!PollMaskUs(se, SE_GENI_S_IRQ_STATUS, S_CMD_CANCEL_EN, S_CMD_CANCEL_EN, 5000U)) {
        Wr32(se, SE_GENI_S_CMD_CTRL_REG, S_GENI_CMD_ABORT);
        (void)PollMaskUs(se, SE_GENI_S_IRQ_STATUS, S_CMD_ABORT_EN, S_CMD_ABORT_EN, 5000U);
    }
    Wr32(se, SE_GENI_S_IRQ_CLEAR, 0xFFFFFFFFU);
}

static void GeniStopTx(volatile uint8_t *se) {
    if ((Rd32(se, SE_GENI_STATUS) & M_GENI_CMD_ACTIVE) == 0U) return;
    Wr32(se, SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_CANCEL);
    if (!PollMaskUs(se, SE_GENI_M_IRQ_STATUS, M_CMD_CANCEL_EN, M_CMD_CANCEL_EN, 5000U)) {
        Wr32(se, SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
        (void)PollMaskUs(se, SE_GENI_M_IRQ_STATUS, M_CMD_ABORT_EN, M_CMD_ABORT_EN, 5000U);
    }
    Wr32(se, SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFU);
}

static BOOLEAN GeniWaitTxFifoSpace(volatile uint8_t *se, uint32_t txDepth, uint32_t timeoutMs) {
    uint32_t limit = (txDepth > 4U) ? (txDepth - 2U) : 4U;
    for (uint32_t i = 0; i < 200U; i++) {
        if ((Rd32(se, SE_GENI_TX_FIFO_STATUS) & 0x0FFFFFFFU) < limit) return TRUE;
        KeStallExecutionProcessor(5U);
    }
    for (uint32_t ms = 0; ms < timeoutMs; ms++) {
        if ((Rd32(se, SE_GENI_TX_FIFO_STATUS) & 0x0FFFFFFFU) < limit) return TRUE;
        SleepMs(1U);
    }
    return ((Rd32(se, SE_GENI_TX_FIFO_STATUS) & 0x0FFFFFFFU) < limit) ? TRUE : FALSE;
}

static BOOLEAN GeniTxRaw(PS23BTHX_EXT ext, const uint8_t *pkt, uint32_t len) {
    volatile uint8_t *se = ext->Se;
    if (!se || len == 0U) return FALSE;

    KeWaitForSingleObject(&ext->UartTxMutex, Executive, KernelMode, FALSE, NULL);
    /* Wait for CTS_IN == 0 up to 200ms */
    for (uint32_t w = 0; w < 200U; w++) {
        if ((Rd32(se, SE_GENI_IOS) & 2U) == 0U) break;
        if (w < 20U) KeStallExecutionProcessor(50U); else SleepMs(1U);
    }

    GeniStopTx(se);
    Wr32(se, SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFU);
    Wr32(se, SE_UART_TX_TRANS_LEN, len);
    Wr32(se, SE_GENI_M_CMD0, UART_START_TX);

    for (uint32_t i = 0; i < len; i += 4U) {
        if (!GeniWaitTxFifoSpace(se, ext->TxDepth, 500U)) {
            Wr32(se, SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
            (void)PollMaskUs(se, SE_GENI_M_IRQ_STATUS, M_CMD_ABORT_EN, M_CMD_ABORT_EN, 5000U);
            Wr32(se, SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFU);
            KeSetEvent(&ext->UartTxMutex, IO_NO_INCREMENT, FALSE);
            return FALSE;
        }
        uint32_t w = 0;
        for (uint32_t b = 0; b < 4U && (i + b) < len; b++) {
            w |= ((uint32_t)pkt[i + b]) << (b * 8U);
        }
        Wr32(se, SE_GENI_TX_FIFOn, w);
    }

    BOOLEAN done = PollTxDoneFast(se, 1000U);
    uint32_t mIrq = Rd32(se, SE_GENI_M_IRQ_STATUS);
    if (!done) {
        Wr32(se, SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
        (void)PollMaskUs(se, SE_GENI_M_IRQ_STATUS, M_CMD_ABORT_EN, M_CMD_ABORT_EN, 5000U);
        Wr32(se, SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFU);
        KeSetEvent(&ext->UartTxMutex, IO_NO_INCREMENT, FALSE);
        return FALSE;
    }
    Wr32(se, SE_GENI_M_IRQ_CLEAR, M_CMD_DONE_EN | mIrq);
    KeSetEvent(&ext->UartTxMutex, IO_NO_INCREMENT, FALSE);
    return TRUE;
}

static uint32_t GeniRxDrainFilterIbsBoot(volatile uint8_t *se, uint8_t *out, uint32_t maxOut, uint32_t curLen, BOOLEAN *sawWakeInd) {
    uint32_t sIrq = Rd32(se, SE_GENI_S_IRQ_STATUS);
    uint32_t st = Rd32(se, SE_GENI_RX_FIFO_STATUS);
    uint32_t wc = st & 0x01FFFFFFU;
    if (wc == 0U) {
        if (sIrq != 0U) Wr32(se, SE_GENI_S_IRQ_CLEAR, sIrq);
        return curLen;
    }
    uint32_t last = (st >> 31) & 1U;
    uint32_t lastValid = (st >> 28) & 7U;
    uint32_t totalBytes = 4U * (wc - 1U) + ((last && lastValid) ? lastValid : 4U);
    uint32_t rem = totalBytes;
    for (uint32_t w = 0; w < wc; w++) {
        uint32_t word = Rd32(se, SE_GENI_RX_FIFOn);
        uint32_t n = (rem >= 4U) ? 4U : rem;
        for (uint32_t b = 0; b < n; b++) {
            uint8_t ch = (uint8_t)((word >> (b * 8U)) & 0xFFU);
            if (curLen == 0U && (ch == 0xFDU || ch == 0xFCU || ch == 0xFEU)) {
                if (ch == 0xFDU && sawWakeInd) *sawWakeInd = TRUE;
                continue;
            }
            if (curLen < maxOut) out[curLen++] = ch;
        }
        rem -= n;
    }
    Wr32(se, SE_GENI_S_IRQ_CLEAR, 0xFFFFFFFFU);
    return curLen;
}

static uint32_t GeniRxHciEventBoot(PS23BTHX_EXT ext, uint8_t *out, uint32_t maxOut, uint32_t timeoutMs) {
    volatile uint8_t *se = ext->Se;
    uint32_t len = 0, quietMs = 0;
    BOOLEAN acked = FALSE;
    for (uint32_t elapsed = 0; elapsed < timeoutMs; elapsed++) {
        uint32_t before = len;
        BOOLEAN wakeInd = FALSE;
        len = GeniRxDrainFilterIbsBoot(se, out, maxOut, len, &wakeInd);
        if (wakeInd && !acked) {
            static const uint8_t kWakeAck[1] = { 0xFCU };
            (void)GeniTxRaw(ext, kWakeAck, 1U);
            acked = TRUE;
        }
        if (len >= 3U && out[0] == 0x04U && len >= (3U + (uint32_t)out[2])) {
            len = GeniRxDrainFilterIbsBoot(se, out, maxOut, len, NULL);
            break;
        }
        if (len > before) {
            quietMs = 0;
            if (len >= maxOut) break;
        } else if (len > 0U) {
            if (++quietMs >= 40U) break;
        }
        SleepMs(1U);
    }
    return len;
}

static void GeniFlushAllRxBoot(volatile uint8_t *se) {
    uint8_t tmp[128];
    for (uint32_t pass = 0; pass < 32U; pass++) {
        if ((Rd32(se, SE_GENI_RX_FIFO_STATUS) & 0x01FFFFFFU) == 0U) break;
        (void)GeniRxDrainFilterIbsBoot(se, tmp, sizeof(tmp), 0U, NULL);
    }
}

static BOOLEAN PatchNvmTagsInMemory(uint8_t *buf, uint32_t fileLen, uint8_t targetQcaBaud) {
    if (fileLen < 4U) return FALSE;
    uint8_t tlvType = buf[0];
    uint32_t tlvLen = (uint32_t)buf[1] | ((uint32_t)buf[2] << 8) | ((uint32_t)buf[3] << 16);
    uint32_t off = 4U;
    if (tlvType == 4U && fileLen >= 8U) {
        tlvType = buf[4];
        tlvLen  = (uint32_t)buf[5] | ((uint32_t)buf[6] << 8) | ((uint32_t)buf[7] << 16);
        off = 8U;
    }
    if (tlvType != TLV_TYPE_NVM || off + tlvLen > fileLen) return FALSE;

    uint32_t end = off + tlvLen;
    while (off + 12U <= end) {
        uint16_t tagId  = (uint16_t)buf[off] | ((uint16_t)buf[off + 1U] << 8);
        uint16_t tagLen = (uint16_t)buf[off + 2U] | ((uint16_t)buf[off + 3U] << 8);
        if (off + 12U + (uint32_t)tagLen > end) return FALSE;
        uint8_t *td = buf + off + 12U;

        if (tagId == EDL_TAG_ID_BDADDR && tagLen == 6U) {
            BOOLEAN isPh = (td[0] == 0xADU && td[1] == 0x5AU && td[2] == 0U && td[3] == 0U && td[4] == 0U && td[5] == 0U);
            BOOLEAN isZr = (td[0] == 0U && td[1] == 0U && td[2] == 0U && td[3] == 0U && td[4] == 0U && td[5] == 0U);
            if (isPh || isZr) {
                static const uint8_t kS23Mac[6] = { 0x90U, 0x64U, 0x23U, 0x53U, 0x23U, 0x02U };
                MemCopy(td, kS23Mac, 6);
            }
        } else if (tagId == EDL_TAG_ID_HCI && tagLen >= 3U) {
            td[0] = (uint8_t)(td[0] & ~0x80U); /* IBS=0 */
            td[1] = targetQcaBaud;
        } else if (tagId == EDL_TAG_ID_DEEP_SLEEP && tagLen >= 1U) {
            td[0] = (uint8_t)(td[0] & ~0x01U); /* SLEEP=0 */
        }
        off += 12U + (uint32_t)tagLen;
    }
    return TRUE;
}

static BOOLEAN StreamTlvToQca6490(PS23BTHX_EXT ext, const char *label, const uint8_t *data,
                                  uint32_t totalLen, uint8_t dnldMode) {
    uint8_t pkt[256];
    uint8_t rx[64];
    uint32_t offset = 0, segIdx = 0;
    const uint32_t chunkSize = 240U;

    while (offset < totalLen) {
        uint32_t remain = totalLen - offset;
        uint32_t seg = (remain > chunkSize) ? chunkSize : remain;
        BOOLEAN isLast = (offset + seg >= totalLen) ? TRUE : FALSE;

        pkt[0] = 0x01U; pkt[1] = 0x00U; pkt[2] = 0xFCU;
        pkt[3] = (uint8_t)(seg + 2U);
        pkt[4] = EDL_PATCH_TLV_REQ_CMD;
        pkt[5] = (uint8_t)seg;
        MemCopy(pkt + 6, data + offset, seg);

        if (!GeniTxRaw(ext, pkt, seg + 6U)) {
            LogStr("  [ERR] TLV TX fail "); LogStr(label); LogStr(" seg="); LogU32(segIdx); LogStr("\r\n");
            return FALSE;
        }

        BOOLEAN waitAck = (isLast || (dnldMode != QCA_SKIP_EVT_VSE && dnldMode != QCA_SKIP_EVT_VSE_CC)) ? TRUE : FALSE;
        if (waitAck) {
            uint32_t rxLen = GeniRxHciEventBoot(ext, rx, sizeof(rx), isLast ? 2000U : 500U);
            BOOLEAN ok = ((rxLen >= 8U && rx[0] == 0x04U && rx[1] == 0x0EU && rx[4] == 0x00U && rx[5] == 0xFCU && rx[6] == 0x00U) ||
                          (rxLen >= 6U && rx[0] == 0x04U && rx[1] == 0xFFU && rx[3] == 0x00U)) ? TRUE : FALSE;
            if (!ok) {
                LogStr("  [ERR] TLV ACK fail "); LogStr(label); LogStr(" seg="); LogU32(segIdx);
                LogStr(" rxLen="); LogU32(rxLen); LogStr("\r\n");
                return FALSE;
            }
        } else {
            uint32_t early = GeniRxDrainFilterIbsBoot(ext->Se, rx, sizeof(rx), 0U, NULL);
            if (early >= 7U && rx[0] == 0x04U && rx[1] == 0x0EU && rx[6] != 0x00U) {
                LogStr("  [ERR] TLV early error "); LogStr(label); LogStr(" status="); LogHex8(rx[6]); LogStr("\r\n");
                return FALSE;
            }
        }
        offset += seg;
        segIdx++;
    }
    LogStr("  [OK] Streamed "); LogStr(label); LogStr(" ("); LogU32(segIdx); LogStr(" chunks)\r\n");
    return TRUE;
}

static NTSTATUS InitQca6490Hardware(PS23BTHX_EXT ext) {
    LogStr("InitQca6490Hardware starting...\r\n");
    ext->Gcc     = MapPhys(PA_GCC_BASE, SZ_GCC);
    ext->Tlmm    = MapPhys(PA_TLMM_BASE, SZ_TLMM);
    ext->QupWrap = MapPhys(PA_QUP2_WRAP_BASE, SZ_QUP2_WRAP);
    ext->Se      = MapPhys(PA_QUP2_SE6_BASE, SZ_QUP2_SE6);
    if (!ext->Gcc || !ext->Tlmm || !ext->QupWrap || !ext->Se) {
        LogStr("[ERR] MmMapIoSpace failed\r\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    uint32_t wlanIn = Rd32(ext->Tlmm, GPIO_WLAN_EN * 0x1000U + 0x04U) & 3U;
    uint32_t swCtrl = Rd32(ext->Tlmm, GPIO_SW_CTRL * 0x1000U + 0x04U) & 1U;
    if (wlanIn == 0U && swCtrl == 0U) {
        EnsureQca6490SharedRails();
    } else {
        LogStr("  Shared QCA6490 rails already live (WLAN_EN="); LogU32(wlanIn);
        LogStr(" SW_CTRL="); LogU32(swCtrl); LogStr(")\r\n");
    }

    /* Enable GPLL0 + QUP2 clocks + 7.3728 MHz SE6 RCG (AXI safety gate before QUP2 MMIO) */
    Wr32(ext->Gcc, GCC_APCS_GPLL_VOTE, Rd32(ext->Gcc, GCC_APCS_GPLL_VOTE) | 1U);
    (void)PollMaskMs(ext->Gcc, GCC_GPLL0_MODE, (1U << 30) | (1U << 31), (1U << 30) | (1U << 31), 10U);
    Wr32(ext->Gcc, GCC_APCS_CLK_VOTE_2, Rd32(ext->Gcc, GCC_APCS_CLK_VOTE_2) | QUP2_VOTE_ALL);
    (void)ProgramSe6Rcg(ext->Gcc, SE6_RCG_CFG_7P3728M, SE6_RCG_M_7P3728M, SE6_RCG_N_7P3728M, SE6_RCG_D_7P3728M);

    BOOLEAN mOk  = PollMaskMs(ext->Gcc, GCC_QUP2_M_AHB_CBCR, 0x80000000U, 0U, 10U);
    BOOLEAN sOk  = PollMaskMs(ext->Gcc, GCC_QUP2_S_AHB_CBCR, 0x80000000U, 0U, 2U)
                || IsQup2SAhbReady(Rd32(ext->Gcc, GCC_APCS_CLK_VOTE_2), Rd32(ext->Gcc, GCC_QUP2_S_AHB_CBCR));
    BOOLEAN s6Ok = PollMaskMs(ext->Gcc, GCC_QUP2_S6_CBCR,    0x80000000U, 0U, 10U);
    if (!mOk || !sOk || !s6Ok) {
        LogStr("[ERR] QUP2 AHB/SE6 clock gate closed!\r\n");
        FlushLogFile();
        return STATUS_UNSUCCESSFUL;
    }

    volatile uint8_t *se = ext->Se;
    ext->QupVer = Rd32(ext->QupWrap, QUP_HW_VER_REG);
    uint32_t fwRev = Rd32(se, GENI_FW_REVISION_RO);
    uint32_t proto = (fwRev >> 8) & 0xFFU;
    uint32_t ifDis = Rd32(se, GENI_IF_DISABLE_RO);
    uint32_t hw0   = Rd32(se, SE_HW_PARAM_0);
    uint32_t hw1   = Rd32(se, SE_HW_PARAM_1);
    ext->TxDepth   = (ext->QupVer >= QUP_SE_VERSION_2_5) ? ((hw0 >> 16) & 0xFFU) : ((hw0 >> 16) & 0x3FU);
    uint32_t rxDepth = (ext->QupVer >= QUP_SE_VERSION_2_5) ? ((hw1 >> 16) & 0xFFU) : ((hw1 >> 16) & 0x3FU);
    if (ext->TxDepth < 8U || ext->TxDepth > 256U) ext->TxDepth = 48U;
    if (rxDepth < 8U || rxDepth > 256U) rxDepth = 48U;
    uint32_t rfrWm = (rxDepth > 4U) ? (rxDepth - 2U) : 2U;

    if (proto != GENI_SE_UART || (ifDis & 1U) != 0U) {
        LogStr("[ERR] SE14 not UART or FIFO disabled proto="); LogU32(proto); LogStr("\r\n");
        FlushLogFile();
        return STATUS_UNSUCCESSFUL;
    }

    /* TLMM UART14 pins */
    SetTlmmPin(ext->Tlmm, GPIO_UART14_CTS, 0x0004U, -1);
    SetTlmmPin(ext->Tlmm, GPIO_UART14_RTS, 0x0005U, -1);
    SetTlmmPin(ext->Tlmm, GPIO_UART14_TX,  0x0007U, -1);
    SetTlmmPin(ext->Tlmm, GPIO_UART14_RX,  0x0007U, -1);

    /* Init GENI UART14 @ 115200 8N1 FIFO */
    GeniStopRx(se);
    GeniStopTx(se);
    Wr32(se, SE_GSI_EVENT_EN, 0U);
    Wr32(se, SE_IRQ_EN, 0U);
    Wr32(se, SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFFU);
    Wr32(se, SE_GENI_S_IRQ_CLEAR, 0xFFFFFFFFU);
    Wr32(se, SE_DMA_TX_IRQ_CLR, 0xFFFFFFFFU);
    Wr32(se, SE_DMA_RX_IRQ_CLR, 0xFFFFFFFFU);
    Wr32(se, GENI_CGC_CTRL, Rd32(se, GENI_CGC_CTRL) | 0x7FU);
    Wr32(se, SE_DMA_GENERAL_CFG, Rd32(se, SE_DMA_GENERAL_CFG) | 0x0FU);
    Wr32(se, GENI_OUTPUT_CTRL, 0x7FU);
    Wr32(se, GENI_FORCE_DEFAULT_REG, 1U);
    Wr32(se, SE_GENI_DMA_MODE_EN, 0U);
    Wr32(se, SE_GENI_CLK_SEL, 0U);
    Wr32(se, SE_GENI_TX_PACKING_CFG0, PACKING_4X8_LSB_CFG0);
    Wr32(se, SE_GENI_TX_PACKING_CFG1, PACKING_4X8_LSB_CFG1);
    Wr32(se, SE_GENI_RX_PACKING_CFG0, PACKING_4X8_LSB_CFG0);
    Wr32(se, SE_GENI_RX_PACKING_CFG1, PACKING_4X8_LSB_CFG1);
    Wr32(se, SE_GENI_BYTE_GRAN, 0U);
    Wr32(se, SE_GENI_RX_WATERMARK, 2U);
    Wr32(se, SE_GENI_RX_RFR_WM, rfrWm);
    Wr32(se, SE_UART_MANUAL_RFR, 0U);
    Wr32(se, SE_UART_LOOPBACK_CFG, 0U);
    Wr32(se, SE_UART_TX_TRANS_CFG, 0U);
    Wr32(se, SE_UART_TX_PARITY_CFG, 0U);
    Wr32(se, SE_UART_RX_TRANS_CFG, 0U);
    Wr32(se, SE_UART_RX_PARITY_CFG, 0U);
    Wr32(se, SE_UART_TX_WORD_LEN, 8U);
    Wr32(se, SE_UART_RX_WORD_LEN, 8U);
    Wr32(se, SE_UART_TX_STOP_BIT_LEN, 0U);
    Wr32(se, SE_UART_RX_STALE_CNT, 160U);

    uint32_t oversamp = (ext->QupVer >= QUP_SE_VERSION_2_5) ? 16U : 32U;
    uint32_t serClk = ((7372800U / (115200U * oversamp)) << 4) | 1U;
    Wr32(se, GENI_SER_M_CLK_CFG, serClk);
    Wr32(se, GENI_SER_S_CLK_CFG, serClk);
    Wr32(se, SE_GENI_M_IRQ_EN, 0x0000003FU);
    Wr32(se, SE_GENI_S_IRQ_EN, 0x0C00003FU);
    Wr32(se, SE_GENI_S_CMD0, UART_START_READ);

    /* Pulse BT_EN (GPIO81) + XO_CLK (GPIO204, EGPIO_ENABLE) */
    SetTlmmPin(ext->Tlmm, GPIO_BT_EN,  0x0201U, 0);
    SetTlmmPin(ext->Tlmm, GPIO_XO_CLK, 0x1201U, 0);
    SleepMs(50U);
    SetTlmmPin(ext->Tlmm, GPIO_XO_CLK, 0x13C3U, 1);
    KeStallExecutionProcessor(200U);
    SetTlmmPin(ext->Tlmm, GPIO_BT_EN,  0x0201U, 1);
    SleepMs(5U);
    SetTlmmPin(ext->Tlmm, GPIO_XO_CLK, 0x1201U, 0);
    SleepMs(150U);
    GeniFlushAllRxBoot(se);

    /* Verify ROM responds to HCI_Reset + EDL_PATCH_VER_REQ */
    uint8_t rx[64];
    static const uint8_t kHciReset[4]    = { 0x01U, 0x03U, 0x0CU, 0x00U };
    static const uint8_t kEdlPatchVer[5] = { 0x01U, 0x00U, 0xFCU, 0x01U, 0x19U };
    (void)GeniTxRaw(ext, kHciReset, sizeof(kHciReset));
    uint32_t rn = GeniRxHciEventBoot(ext, rx, sizeof(rx), 400U);
    LogStr("  ROM HCI_Reset rx="); LogU32(rn); LogStr("\r\n");
    GeniFlushAllRxBoot(se);
    (void)GeniTxRaw(ext, kEdlPatchVer, sizeof(kEdlPatchVer));
    rn = GeniRxHciEventBoot(ext, rx, sizeof(rx), 400U);
    LogStr("  EDL_PATCH_VER rx="); LogU32(rn); LogStr("\r\n");
    if (rn < 7U) {
        LogStr("[ERR] QCA6490 ROM did not respond!\r\n");
        FlushLogFile();
        return STATUS_DEVICE_NOT_READY;
    }

    /* Switch to 3,200,000 baud */
    Wr32(se, SE_UART_MANUAL_RFR, UART_MANUAL_RFR_EN | UART_RFR_NOT_READY);
    Wr32(se, SE_UART_TX_TRANS_CFG, UART_CTS_MASK);
    static const uint8_t kSetBaud[5] = { 0x01U, 0x48U, 0xFCU, 0x01U, QCA_BAUDRATE_3200000 };
    if (!GeniTxRaw(ext, kSetBaud, sizeof(kSetBaud))) {
        LogStr("[ERR] EDL_SET_BAUDRATE TX failed\r\n");
        FlushLogFile();
        return STATUS_UNSUCCESSFUL;
    }
    SleepMs(10U);
    GeniStopRx(se);
    if (!ProgramSe6Rcg(ext->Gcc, SE6_RCG_CFG_51P2M, SE6_RCG_M_51P2M, SE6_RCG_N_51P2M, SE6_RCG_D_51P2M)) {
        LogStr("[ERR] 51.2 MHz RCG failed\r\n");
        FlushLogFile();
        return STATUS_UNSUCCESSFUL;
    }
    Wr32(se, GENI_SER_M_CLK_CFG, 0x11U);
    Wr32(se, GENI_SER_S_CLK_CFG, 0x11U);
    Wr32(se, SE_GENI_S_CMD0, UART_START_READ);
    Wr32(se, SE_UART_TX_TRANS_CFG, 0U);
    Wr32(se, SE_UART_MANUAL_RFR, 0U);
    SleepMs(20U);
    GeniFlushAllRxBoot(se);
    LogStr("  Switched UART14 to 3,200,000 baud\r\n");

    /* Download hpbtfw21.tlv */
    uint32_t patchLen = 0;
    if (!LoadFwWithFallback(L"\\??\\C:\\Windows\\System32\\drivers\\hpbtfw21.tlv",
                            L"\\??\\C:\\Users\\Public\\hpbtfw21.tlv",
                            g_FwBuf, FW_BUF_CAP, &patchLen) ||
        patchLen < 32U || g_FwBuf[0] != TLV_TYPE_PATCH) {
        LogStr("[ERR] Failed to load hpbtfw21.tlv\r\n");
        FlushLogFile();
        return STATUS_UNSUCCESSFUL;
    }
    uint8_t dnldMode = g_FwBuf[14];
    if (!StreamTlvToQca6490(ext, "hpbtfw21.tlv", g_FwBuf, patchLen, dnldMode)) {
        FlushLogFile();
        return STATUS_UNSUCCESSFUL;
    }
    SleepMs(10U);

    /* Download hpnv21g.bin */
    uint32_t nvmLen = 0;
    if (!LoadFwWithFallback(L"\\??\\C:\\Windows\\System32\\drivers\\hpnv21g.bin",
                            L"\\??\\C:\\Users\\Public\\hpnv21g.bin",
                            g_FwBuf, FW_BUF_CAP, &nvmLen) ||
        !PatchNvmTagsInMemory(g_FwBuf, nvmLen, QCA_BAUDRATE_3200000)) {
        LogStr("[ERR] Failed to load/patch hpnv21g.bin\r\n");
        FlushLogFile();
        return STATUS_UNSUCCESSFUL;
    }
    if (!StreamTlvToQca6490(ext, "hpnv21g.bin", g_FwBuf, nvmLen, QCA_SKIP_EVT_NONE)) {
        FlushLogFile();
        return STATUS_UNSUCCESSFUL;
    }

    /* Post-download activation: QCA_DISABLE_LOGGING + HCI_Reset */
    static const uint8_t kDisableLog[6] = { 0x01U, 0x17U, 0xFCU, 0x02U, 0x14U, 0x00U };
    GeniFlushAllRxBoot(se);
    (void)GeniTxRaw(ext, kDisableLog, sizeof(kDisableLog));
    (void)GeniRxHciEventBoot(ext, rx, sizeof(rx), 400U);
    GeniFlushAllRxBoot(se);
    (void)GeniTxRaw(ext, kHciReset, sizeof(kHciReset));
    rn = GeniRxHciEventBoot(ext, rx, sizeof(rx), 1500U);
    LogStr("  Post-DL HCI_Reset rx="); LogU32(rn);
    if (rn >= 7U && rx[0] == 0x04U && rx[1] == 0x0EU && rx[6] == 0x00U) {
        LogStr(" -> BT 5.3 FW ACTIVE!\r\n");
    } else {
        LogStr(" -> FAIL!\r\n");
        FlushLogFile();
        return STATUS_UNSUCCESSFUL;
    }
    GeniFlushAllRxBoot(se);
    FlushLogFile();
    return STATUS_SUCCESS;
}

/* ============================================================================
 * Cancel-Safe Queue & Packet Ring Helpers
 * ============================================================================ */
static VOID QueuePush(IRP_QUEUE *q, PIRP irp) {
    if (q->Count < MAX_PENDING_IRP) q->Irps[q->Count++] = irp;
}

static PIRP QueuePop(IRP_QUEUE *q) {
    if (q->Count == 0U) return NULL;
    PIRP irp = q->Irps[0];
    for (uint32_t i = 1U; i < q->Count; i++) q->Irps[i - 1U] = q->Irps[i];
    q->Irps[--q->Count] = NULL;
    return irp;
}

static BOOLEAN QueueRemove(IRP_QUEUE *q, PIRP target) {
    for (uint32_t i = 0; i < q->Count; i++) {
        if (q->Irps[i] == target) {
            for (uint32_t j = i + 1U; j < q->Count; j++) q->Irps[j - 1U] = q->Irps[j];
            q->Irps[--q->Count] = NULL;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOLEAN RingPush(HCI_PKT_RING *r, const uint8_t *data, uint32_t len) {
    if (len > MAX_PKT_BYTES) return FALSE;
    if (r->Count >= RING_SLOTS) {
        /* Drop oldest slot on overflow to keep newest events flowing */
        r->Head = (r->Head + 1U) % RING_SLOTS;
        r->Count--;
    }
    HCI_PKT_SLOT *s = &r->Slots[r->Tail];
    s->Len = (uint16_t)len;
    MemCopy(s->Data, data, len);
    r->Tail = (r->Tail + 1U) % RING_SLOTS;
    r->Count++;
    return TRUE;
}

static BOOLEAN RingPop(HCI_PKT_RING *r, uint8_t *out, uint32_t cap, uint32_t *lenOut) {
    if (r->Count == 0U) return FALSE;
    HCI_PKT_SLOT *s = &r->Slots[r->Head];
    if (s->Len > cap) return FALSE;
    *lenOut = s->Len;
    MemCopy(out, s->Data, s->Len);
    r->Head = (r->Head + 1U) % RING_SLOTS;
    r->Count--;
    return TRUE;
}

static VOID S23BthxCancelIrp(PDEVICE_OBJECT devObj, PIRP irp) {
    (void)devObj;
    IoReleaseCancelSpinLock(irp->CancelIrql);
    PS23BTHX_EXT ext = g_ActiveExt;
    if (ext) {
        KIRQL oldIrql = KeAcquireSpinLockRaiseToDpc(&ext->IoLock);
        (void)(QueueRemove(&ext->TxQueue, irp) ||
               QueueRemove(&ext->EvtQueue, irp) ||
               QueueRemove(&ext->AclQueue, irp));
        KeReleaseSpinLock(&ext->IoLock, oldIrql);
    }
    /* The I/O manager calls this routine only after atomically taking the cancel routine out of the IRP,
     * so this routine owns the IRP and must complete it even if it is no longer queued: the TX thread,
     * DeliverRxPacket and DrainAndCancelQueue pop the IRP first and, when IoSetCancelRoutine(irp, NULL)
     * returns NULL, leave it to us.  Completing only "removed" IRPs lost those IRPs forever. */
    irp->IoStatus.Status = STATUS_CANCELLED;
    irp->IoStatus.Information = 0;
    IofCompleteRequest(irp, IO_NO_INCREMENT);
}

static VOID CompleteReadIrpWithPacket(PIRP irp, UCHAR pktType, const uint8_t *pkt, uint32_t pktLen) {
    PIO_STACK_LOCATION isl = IoGetCurrentIrpStackLocation(irp);
    ULONG outLen = isl->Parameters.DeviceIoControl.OutputBufferLength;
    PBTHX_HCI_READ_WRITE_CONTEXT outCtx = (PBTHX_HCI_READ_WRITE_CONTEXT)irp->UserBuffer;
    if (!outCtx || outLen < 5U + pktLen) {
        irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
        irp->IoStatus.Information = 0;
        IofCompleteRequest(irp, IO_NO_INCREMENT);
        return;
    }
    outCtx->Type = pktType;
    outCtx->DataLen = pktLen;
    if (pktLen > 0U) MemCopy(outCtx->Data, pkt, pktLen);
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 5U + (ULONG_PTR)pktLen;
    IofCompleteRequest(irp, IO_NO_INCREMENT);
}

/* ============================================================================
 * TX & RX Kernel Worker Threads
 * ============================================================================ */
static VOID S23BthxTxThread(PVOID ctx) {
    PS23BTHX_EXT ext = (PS23BTHX_EXT)ctx;

    for (;;) {
        if (ext->Stopping) break;

        for (;;) {
            KIRQL oldIrql = KeAcquireSpinLockRaiseToDpc(&ext->IoLock);
            PIRP irp = NULL;
            while (ext->TxQueue.Count > 0U) {
                PIRP cand = QueuePop(&ext->TxQueue);
                if (IoSetCancelRoutine(cand, NULL) != NULL) {
                    irp = cand;
                    break;
                }
                /* Cancel routine already claimed it */
            }
            KeReleaseSpinLock(&ext->IoLock, oldIrql);

            if (!irp) break;

            PIO_STACK_LOCATION isl = IoGetCurrentIrpStackLocation(irp);
            PBTHX_HCI_READ_WRITE_CONTEXT inCtx = (PBTHX_HCI_READ_WRITE_CONTEXT)isl->Parameters.DeviceIoControl.Type3InputBuffer;
            ULONG inLen  = isl->Parameters.DeviceIoControl.InputBufferLength;
            ULONG outLen = isl->Parameters.DeviceIoControl.OutputBufferLength;
            PULONG outBytes = (PULONG)irp->UserBuffer;

            NTSTATUS st = STATUS_INVALID_PARAMETER;
            ULONG info = 0;
            if (inCtx && inLen >= 5U && outBytes && outLen >= 4U &&
                inCtx->DataLen > 0U && inCtx->DataLen <= (sizeof(ext->TxScratch) - 1U) &&
                inLen >= (5U + inCtx->DataLen)) {
                uint32_t dlen = inCtx->DataLen;
                ext->TxScratch[0] = inCtx->Type;
                MemCopy(ext->TxScratch + 1, inCtx->Data, dlen);
                /* BT 5.3 (Core Spec 4.1+) deprecates Simultaneous_LE_Host: QCA6490 rejects 0x01 with 0x11 */
                if (inCtx->Type == HciPacketCommand && dlen == 5U &&
                    ext->TxScratch[1] == 0x6DU && ext->TxScratch[2] == 0x0CU &&
                    ext->TxScratch[3] == 0x02U && ext->TxScratch[5] != 0x00U) {
                    ext->TxScratch[5] = 0x00U;
                    LogStr("  [QUIRK] Write_LE_Host_Support(0x0C6D): Simultaneous_LE_Host 1->0\r\n");
                }
                if (ext->LogTxCount < 80U) {
                    ext->LogTxCount++;
                    LogStr("  TX type="); LogHex8(inCtx->Type);
                    LogStr(" len="); LogU32(dlen);
                    if (dlen >= 2U) {
                        LogStr(" op="); LogHex8(ext->TxScratch[2]); LogHex8(ext->TxScratch[1]);
                    }
                    LogStr("\r\n");
                    FlushLogFile();
                }
                if (GeniTxRaw(ext, ext->TxScratch, 1U + dlen)) {
                    *outBytes = dlen;
                    st = STATUS_SUCCESS;
                    info = sizeof(ULONG);
                } else {
                    st = STATUS_UNSUCCESSFUL;
                }
            }
            irp->IoStatus.Status = st;
            irp->IoStatus.Information = info;
            IofCompleteRequest(irp, IO_NO_INCREMENT);
        }

        if (ext->Stopping) break;
        /* Wait on TxEvent (synchronization event) for 50ms or StopEvent */
        LARGE_INTEGER to;
        to.QuadPart = -500000LL;
        (void)KeWaitForSingleObject(&ext->TxEvent, Executive, KernelMode, FALSE, &to);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID DeliverRxPacket(PS23BTHX_EXT ext, UCHAR pktType, const uint8_t *pkt, uint32_t pktLen) {
    if (ext->LogRxCount < 80U) {
        ext->LogRxCount++;
        LogStr("  RX type="); LogHex8(pktType);
        LogStr(" len="); LogU32(pktLen);
        if (pktLen >= 1U) { LogStr(" b0="); LogHex8(pkt[0]); }
        if (pktType == HciPacketEvent && pktLen >= 6U && pkt[0] == 0x0EU) {
            LogStr(" cc_op="); LogHex8(pkt[4]); LogHex8(pkt[3]);
            LogStr(" st="); LogHex8(pkt[5]);
        }
        LogStr("\r\n");
        FlushLogFile();
    }

    KIRQL oldIrql = KeAcquireSpinLockRaiseToDpc(&ext->IoLock);
    IRP_QUEUE *q = (pktType == HciPacketEvent) ? &ext->EvtQueue : &ext->AclQueue;
    PIRP irp = NULL;
    while (q->Count > 0U) {
        PIRP cand = QueuePop(q);
        if (IoSetCancelRoutine(cand, NULL) != NULL) {
            irp = cand;
            break;
        }
    }
    if (!irp) {
        HCI_PKT_RING *r = (pktType == HciPacketEvent) ? &ext->EvtRing : &ext->AclRing;
        (void)RingPush(r, pkt, pktLen);
    }
    KeReleaseSpinLock(&ext->IoLock, oldIrql);

    if (irp) {
        CompleteReadIrpWithPacket(irp, pktType, pkt, pktLen);
    }
}

static VOID ProcessRxByte(PS23BTHX_EXT ext, uint8_t ch) {
    switch (ext->RxState) {
    case RX_STATE_IDLE:
        if (ch == 0xFDU) {
            static const uint8_t kWakeAck[1] = { 0xFCU };
            (void)GeniTxRaw(ext, kWakeAck, 1U);
            return;
        }
        if (ch == 0xFCU || ch == 0xFEU) {
            return;
        }
        if (ch == HciPacketEvent) {
            ext->RxPktType = HciPacketEvent;
            ext->RxGot = 0;
            ext->RxNeed = 2U;
            ext->RxState = RX_STATE_HDR;
        } else if (ch == HciPacketAclData) {
            ext->RxPktType = HciPacketAclData;
            ext->RxGot = 0;
            ext->RxNeed = 4U;
            ext->RxState = RX_STATE_HDR;
        }
        break;

    case RX_STATE_HDR:
        ext->RxPktBuf[ext->RxGot++] = ch;
        if (ext->RxGot >= ext->RxNeed) {
            uint32_t payload = (ext->RxPktType == HciPacketEvent)
                ? (uint32_t)ext->RxPktBuf[1]
                : ((uint32_t)ext->RxPktBuf[2] | ((uint32_t)ext->RxPktBuf[3] << 8));
            uint32_t total = ext->RxNeed + payload;
            if (total > MAX_PKT_BYTES) {
                ext->RxState = RX_STATE_IDLE;
                ext->RxGot = 0;
            } else if (payload == 0U) {
                DeliverRxPacket(ext, ext->RxPktType, ext->RxPktBuf, total);
                ext->RxState = RX_STATE_IDLE;
                ext->RxGot = 0;
            } else {
                ext->RxNeed = total;
                ext->RxState = RX_STATE_BODY;
            }
        }
        break;

    case RX_STATE_BODY:
        ext->RxPktBuf[ext->RxGot++] = ch;
        if (ext->RxGot >= ext->RxNeed) {
            DeliverRxPacket(ext, ext->RxPktType, ext->RxPktBuf, ext->RxNeed);
            ext->RxState = RX_STATE_IDLE;
            ext->RxGot = 0;
        }
        break;
    }
}

static VOID S23BthxRxThread(PVOID ctx) {
    PS23BTHX_EXT ext = (PS23BTHX_EXT)ctx;
    volatile uint8_t *se = ext->Se;
    uint32_t idleSpin = 0;

    while (!ext->Stopping) {
        uint32_t sIrq = Rd32(se, SE_GENI_S_IRQ_STATUS);
        uint32_t st = Rd32(se, SE_GENI_RX_FIFO_STATUS);
        uint32_t wc = st & 0x01FFFFFFU;
        if (wc == 0U) {
            if (sIrq != 0U) Wr32(se, SE_GENI_S_IRQ_CLEAR, sIrq);
            if (idleSpin > 0U) {
                idleSpin--;
                KeStallExecutionProcessor(10U);
            } else {
                SleepMs(1U);
            }
            continue;
        }
        idleSpin = 50U;

        uint32_t last = (st >> 31) & 1U;
        uint32_t lastValid = (st >> 28) & 7U;
        uint32_t totalBytes = 4U * (wc - 1U) + ((last && lastValid) ? lastValid : 4U);
        uint32_t rem = totalBytes;
        for (uint32_t w = 0; w < wc && !ext->Stopping; w++) {
            uint32_t word = Rd32(se, SE_GENI_RX_FIFOn);
            uint32_t n = (rem >= 4U) ? 4U : rem;
            for (uint32_t b = 0; b < n; b++) {
                ProcessRxByte(ext, (uint8_t)((word >> (b * 8U)) & 0xFFU));
            }
            rem -= n;
        }
        Wr32(se, SE_GENI_S_IRQ_CLEAR, 0xFFFFFFFFU);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

/* ============================================================================
 * IOCTL_BTHX_* Dispatch (METHOD_NEITHER via DEVICE_CONTROL & INTERNAL_DEVICE_CONTROL)
 * ============================================================================ */
static NTSTATUS S23BthxDispatchIoctl(PDEVICE_OBJECT fdo, PIRP irp) {
    PS23BTHX_EXT ext = (PS23BTHX_EXT)fdo->DeviceExtension;
    NTSTATUS rl = IoAcquireRemoveLockEx(&ext->RemoveLock, irp, "s23bthx", 1, sizeof(IO_REMOVE_LOCK));
    if (!NT_SUCCESS(rl)) {
        irp->IoStatus.Status = rl;
        irp->IoStatus.Information = 0;
        IofCompleteRequest(irp, IO_NO_INCREMENT);
        return rl;
    }

    PIO_STACK_LOCATION isl = IoGetCurrentIrpStackLocation(irp);
    ULONG code   = isl->Parameters.DeviceIoControl.IoControlCode;
    ULONG inLen  = isl->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = isl->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID inBuf  = isl->Parameters.DeviceIoControl.Type3InputBuffer;
    PVOID outBuf = irp->UserBuffer;

    if (ext->LogIoctlCount < 20U) {
        ext->LogIoctlCount++;
        LogStr("IOCTL mj="); LogHex8(isl->MajorFunction);
        LogStr(" code="); LogHex32(code);
        LogStr(" in="); LogU32(inLen);
        LogStr(" out="); LogU32(outLen);
        LogStr("\r\n");
        FlushLogFile();
    }

    switch (code) {
    case IOCTL_BTHX_GET_VERSION: {
        if (!outBuf || outLen < sizeof(ULONG)) {
            irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
            irp->IoStatus.Information = 0;
            IofCompleteRequest(irp, IO_NO_INCREMENT);
            IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
            return STATUS_BUFFER_TOO_SMALL;
        }
        *(PULONG)outBuf = BTHX_DDI_VERSION_1;
        irp->IoStatus.Status = STATUS_SUCCESS;
        irp->IoStatus.Information = sizeof(ULONG);
        IofCompleteRequest(irp, IO_NO_INCREMENT);
        IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
        return STATUS_SUCCESS;
    }

    case IOCTL_BTHX_SET_VERSION: {
        NTSTATUS st = STATUS_SUCCESS;
        if (!inBuf || inLen < sizeof(ULONG) || *(PULONG)inBuf != BTHX_DDI_VERSION_1) {
            st = STATUS_INVALID_PARAMETER;
        }
        irp->IoStatus.Status = st;
        irp->IoStatus.Information = 0;
        IofCompleteRequest(irp, IO_NO_INCREMENT);
        IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
        return st;
    }

    case IOCTL_BTHX_QUERY_CAPABILITIES: {
        if (!outBuf || outLen < sizeof(BTHX_CAPABILITIES)) {
            irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
            irp->IoStatus.Information = 0;
            IofCompleteRequest(irp, IO_NO_INCREMENT);
            IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
            return STATUS_BUFFER_TOO_SMALL;
        }
        PBTHX_CAPABILITIES caps = (PBTHX_CAPABILITIES)outBuf;
        MemZero(caps, sizeof(*caps));
        caps->MaxAclTransferInSize = 1025U;
        caps->ScoSupport           = ScoSupportHCIBypass;
        caps->MaxScoChannels       = 1U;
        caps->IsDeviceIdleCapable  = FALSE;
        caps->IsDeviceWakeCapable  = FALSE;
        irp->IoStatus.Status = STATUS_SUCCESS;
        irp->IoStatus.Information = sizeof(BTHX_CAPABILITIES);
        IofCompleteRequest(irp, IO_NO_INCREMENT);
        IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
        return STATUS_SUCCESS;
    }

    case IOCTL_BTHX_WRITE_HCI: {
        if (!ext->HwReady || ext->Stopping) {
            irp->IoStatus.Status = STATUS_NO_SUCH_DEVICE;
            irp->IoStatus.Information = 0;
            IofCompleteRequest(irp, IO_NO_INCREMENT);
            IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
            return STATUS_NO_SUCH_DEVICE;
        }
        KIRQL oldIrql = KeAcquireSpinLockRaiseToDpc(&ext->IoLock);
        /* Re-check Stopping under IoLock (like READ_HCI): S23BthxStopHw sets it before draining the
         * queues under this lock, so an IRP queued after the drain would never be completed. */
        if (ext->TxQueue.Count >= MAX_PENDING_IRP || ext->Stopping) {
            NTSTATUS rej = ext->Stopping ? STATUS_NO_SUCH_DEVICE : STATUS_INSUFFICIENT_RESOURCES;
            KeReleaseSpinLock(&ext->IoLock, oldIrql);
            irp->IoStatus.Status = rej;
            irp->IoStatus.Information = 0;
            IofCompleteRequest(irp, IO_NO_INCREMENT);
            IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
            return rej;
        }
        IoMarkIrpPending(irp);
        IoSetCancelRoutine(irp, S23BthxCancelIrp);
        if (irp->Cancel && IoSetCancelRoutine(irp, NULL) != NULL) {
            KeReleaseSpinLock(&ext->IoLock, oldIrql);
            irp->IoStatus.Status = STATUS_CANCELLED;
            irp->IoStatus.Information = 0;
            IofCompleteRequest(irp, IO_NO_INCREMENT);
            IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
            return STATUS_PENDING;   /* IoMarkIrpPending was called: must return STATUS_PENDING */
        }
        QueuePush(&ext->TxQueue, irp);
        KeReleaseSpinLock(&ext->IoLock, oldIrql);
        KeSetEvent(&ext->TxEvent, IO_NO_INCREMENT, FALSE);
        IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
        return STATUS_PENDING;
    }

    case IOCTL_BTHX_READ_HCI: {
        if (!inBuf || inLen < sizeof(ULONG) || !outBuf || outLen < 5U) {
            irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
            irp->IoStatus.Information = 0;
            IofCompleteRequest(irp, IO_NO_INCREMENT);
            IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
            return STATUS_INVALID_PARAMETER;
        }
        ULONG pktType = *(PULONG)inBuf;
        if (pktType != HciPacketEvent && pktType != HciPacketAclData) {
            irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
            irp->IoStatus.Information = 0;
            IofCompleteRequest(irp, IO_NO_INCREMENT);
            IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
            return STATUS_INVALID_PARAMETER;
        }

        uint8_t pktCopy[MAX_PKT_BYTES];
        uint32_t pktLen = 0;
        BOOLEAN havePkt = FALSE;

        KIRQL oldIrql = KeAcquireSpinLockRaiseToDpc(&ext->IoLock);
        HCI_PKT_RING *r = (pktType == HciPacketEvent) ? &ext->EvtRing : &ext->AclRing;
        if (r->Count > 0U && RingPop(r, pktCopy, sizeof(pktCopy), &pktLen)) {
            havePkt = TRUE;
        } else {
            IRP_QUEUE *q = (pktType == HciPacketEvent) ? &ext->EvtQueue : &ext->AclQueue;
            if (q->Count >= MAX_PENDING_IRP || ext->Stopping) {
                KeReleaseSpinLock(&ext->IoLock, oldIrql);
                irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                irp->IoStatus.Information = 0;
                IofCompleteRequest(irp, IO_NO_INCREMENT);
                IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            IoMarkIrpPending(irp);
            IoSetCancelRoutine(irp, S23BthxCancelIrp);
            if (irp->Cancel && IoSetCancelRoutine(irp, NULL) != NULL) {
                KeReleaseSpinLock(&ext->IoLock, oldIrql);
                irp->IoStatus.Status = STATUS_CANCELLED;
                irp->IoStatus.Information = 0;
                IofCompleteRequest(irp, IO_NO_INCREMENT);
                IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
                return STATUS_PENDING;   /* IoMarkIrpPending was called: must return STATUS_PENDING */
            }
            QueuePush(q, irp);
            KeReleaseSpinLock(&ext->IoLock, oldIrql);
            IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
            return STATUS_PENDING;
        }
        KeReleaseSpinLock(&ext->IoLock, oldIrql);

        if (havePkt) {
            CompleteReadIrpWithPacket(irp, (UCHAR)pktType, pktCopy, pktLen);
            NTSTATUS cst = irp->IoStatus.Status;
            IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
            return cst;
        }
        break;
    }

    default:
        break;
    }

    IoSkipCurrentIrpStackLocation(irp);
    NTSTATUS st = IofCallDriver(ext->LowerDevice, irp);
    IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
    return st;
}

/* ============================================================================
 * PnP, Power, Create/Close/PassThrough & Unload
 * ============================================================================ */
static NTSTATUS S23SyncComp(PDEVICE_OBJECT dev, PIRP irp, PVOID ctx) {
    (void)dev; (void)irp;
    KeSetEvent((PKEVENT)ctx, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS S23SendIrpSync(PDEVICE_OBJECT target, PIRP irp) {
    KEVENT ev;
    KeInitializeEvent(&ev, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(irp);
    IoSetCompletionRoutine(irp, S23SyncComp, &ev);
    NTSTATUS st = IofCallDriver(target, irp);
    if (st == STATUS_PENDING) {
        KeWaitForSingleObject(&ev, Executive, KernelMode, FALSE, NULL);
        st = irp->IoStatus.Status;
    }
    return st;
}

static VOID DrainAndCancelQueue(PS23BTHX_EXT ext, IRP_QUEUE *q) {
    for (;;) {
        KIRQL oldIrql = KeAcquireSpinLockRaiseToDpc(&ext->IoLock);
        PIRP irp = QueuePop(q);
        if (irp && IoSetCancelRoutine(irp, NULL) == NULL) {
            irp = NULL;
        }
        KeReleaseSpinLock(&ext->IoLock, oldIrql);
        if (!irp) {
            if (q->Count == 0U) break;
            continue;
        }
        irp->IoStatus.Status = STATUS_CANCELLED;
        irp->IoStatus.Information = 0;
        IofCompleteRequest(irp, IO_NO_INCREMENT);
    }
}

static VOID S23BthxStopHw(PS23BTHX_EXT ext) {
    ext->Stopping = 1;
    ext->HwReady = FALSE;
    KeSetEvent(&ext->StopEvent, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&ext->TxEvent, IO_NO_INCREMENT, FALSE);

    if (ext->TxThreadObj) {
        KeWaitForSingleObject(ext->TxThreadObj, Executive, KernelMode, FALSE, NULL);
        ObfDereferenceObject(ext->TxThreadObj);
        ext->TxThreadObj = NULL;
    }
    if (ext->RxThreadObj) {
        KeWaitForSingleObject(ext->RxThreadObj, Executive, KernelMode, FALSE, NULL);
        ObfDereferenceObject(ext->RxThreadObj);
        ext->RxThreadObj = NULL;
    }

    DrainAndCancelQueue(ext, &ext->TxQueue);
    DrainAndCancelQueue(ext, &ext->EvtQueue);
    DrainAndCancelQueue(ext, &ext->AclQueue);

    if (ext->Se) {
        GeniStopRx(ext->Se);
        GeniStopTx(ext->Se);
        MmUnmapIoSpace((PVOID)ext->Se, SZ_QUP2_SE6);
        ext->Se = NULL;
    }
    if (ext->QupWrap) { MmUnmapIoSpace((PVOID)ext->QupWrap, SZ_QUP2_WRAP); ext->QupWrap = NULL; }
    if (ext->Tlmm)    { MmUnmapIoSpace((PVOID)ext->Tlmm, SZ_TLMM); ext->Tlmm = NULL; }
    if (ext->Gcc)     { MmUnmapIoSpace((PVOID)ext->Gcc, SZ_GCC); ext->Gcc = NULL; }
}

static NTSTATUS SpawnWorker(PKSTART_ROUTINE fn, PS23BTHX_EXT ext, PVOID *objOut) {
    OBJECT_ATTRIBUTES oa;
    MemZero(&oa, sizeof(oa));
    oa.Length = sizeof(oa);
    oa.Attributes = OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE;
    HANDLE hThread = NULL;
    NTSTATUS st = PsCreateSystemThread(&hThread, 0x1FFFFFu, &oa, NULL, NULL, fn, ext);
    if (!NT_SUCCESS(st) || !hThread) return st;
    NTSTATUS ost = ObReferenceObjectByHandle(hThread, 0x1FFFFFu, NULL, KernelMode, objOut, NULL);
    ZwClose(hThread);
    return ost;
}

static NTSTATUS S23BthxPnp(PDEVICE_OBJECT fdo, PIRP irp) {
    PS23BTHX_EXT ext = (PS23BTHX_EXT)fdo->DeviceExtension;
    NTSTATUS rl = IoAcquireRemoveLockEx(&ext->RemoveLock, irp, "s23bthx", 1, sizeof(IO_REMOVE_LOCK));
    if (!NT_SUCCESS(rl)) {
        irp->IoStatus.Status = rl;
        irp->IoStatus.Information = 0;
        IofCompleteRequest(irp, IO_NO_INCREMENT);
        return rl;
    }

    UCHAR mn = IoGetCurrentIrpStackLocation(irp)->MinorFunction;
    if (mn == IRP_MN_START_DEVICE) {
        NTSTATUS st = S23SendIrpSync(ext->LowerDevice, irp);
        if (NT_SUCCESS(st) && !ext->HwReady) {
            ext->Stopping = 0;
            st = InitQca6490Hardware(ext);
            if (NT_SUCCESS(st)) {
                ext->HwReady = TRUE;
                NTSTATUS t1 = SpawnWorker(S23BthxTxThread, ext, &ext->TxThreadObj);
                NTSTATUS t2 = SpawnWorker(S23BthxRxThread, ext, &ext->RxThreadObj);
                if (!NT_SUCCESS(t1) || !NT_SUCCESS(t2)) {
                    LogStr("[ERR] Failed to spawn TX/RX worker threads\r\n");
                    S23BthxStopHw(ext);
                    st = STATUS_INSUFFICIENT_RESOURCES;
                } else {
                    LogStr("s23bthx START_DEVICE complete (TX/RX workers active)\r\n");
                }
                FlushLogFile();
            }
        }
        irp->IoStatus.Status = st;
        IofCompleteRequest(irp, IO_NO_INCREMENT);
        IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
        return st;
    }

    if (mn == IRP_MN_STOP_DEVICE || mn == IRP_MN_SURPRISE_REMOVAL) {
        S23BthxStopHw(ext);
        irp->IoStatus.Status = STATUS_SUCCESS;
        IoSkipCurrentIrpStackLocation(irp);
        NTSTATUS st = IofCallDriver(ext->LowerDevice, irp);
        IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
        return st;
    }

    if (mn == IRP_MN_REMOVE_DEVICE) {
        IoReleaseRemoveLockAndWaitEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
        S23BthxStopHw(ext);
        if (g_ActiveExt == ext) g_ActiveExt = NULL;
        irp->IoStatus.Status = STATUS_SUCCESS;
        IoSkipCurrentIrpStackLocation(irp);
        NTSTATUS st = IofCallDriver(ext->LowerDevice, irp);
        IoDetachDevice(ext->LowerDevice);
        IoDeleteDevice(fdo);
        LogStr("s23bthx REMOVE_DEVICE done\r\n");
        FlushLogFile();
        return st;
    }

    if (mn == IRP_MN_QUERY_STOP_DEVICE || mn == IRP_MN_QUERY_REMOVE_DEVICE ||
        mn == IRP_MN_CANCEL_STOP_DEVICE || mn == IRP_MN_CANCEL_REMOVE_DEVICE) {
        irp->IoStatus.Status = STATUS_SUCCESS;
    }

    IoSkipCurrentIrpStackLocation(irp);
    NTSTATUS st = IofCallDriver(ext->LowerDevice, irp);
    IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
    return st;
}

static NTSTATUS S23BthxPower(PDEVICE_OBJECT fdo, PIRP irp) {
    PS23BTHX_EXT ext = (PS23BTHX_EXT)fdo->DeviceExtension;
    NTSTATUS st = IoAcquireRemoveLockEx(&ext->RemoveLock, irp, "s23bthx", 1, sizeof(IO_REMOVE_LOCK));
    if (!NT_SUCCESS(st)) {
        PoStartNextPowerIrp(irp);
        irp->IoStatus.Status = st;
        irp->IoStatus.Information = 0;
        IofCompleteRequest(irp, IO_NO_INCREMENT);
        return st;
    }
    PoStartNextPowerIrp(irp);
    IoSkipCurrentIrpStackLocation(irp);
    st = PoCallDriver(ext->LowerDevice, irp);
    IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
    return st;
}

static NTSTATUS S23BthxPassDown(PDEVICE_OBJECT fdo, PIRP irp) {
    PS23BTHX_EXT ext = (PS23BTHX_EXT)fdo->DeviceExtension;
    NTSTATUS rl = IoAcquireRemoveLockEx(&ext->RemoveLock, irp, "s23bthx", 1, sizeof(IO_REMOVE_LOCK));
    if (!NT_SUCCESS(rl)) {
        irp->IoStatus.Status = rl;
        irp->IoStatus.Information = 0;
        IofCompleteRequest(irp, IO_NO_INCREMENT);
        return rl;
    }
    IoSkipCurrentIrpStackLocation(irp);
    NTSTATUS st = IofCallDriver(ext->LowerDevice, irp);
    IoReleaseRemoveLockEx(&ext->RemoveLock, irp, sizeof(IO_REMOVE_LOCK));
    return st;
}

static NTSTATUS S23BthxAddDevice(PDRIVER_OBJECT drv, PDEVICE_OBJECT pdo) {
    PDEVICE_OBJECT fiDo = NULL;
    NTSTATUS st = IoCreateDevice(drv, sizeof(S23BTHX_EXT), NULL,
                                 FILE_DEVICE_BLUETOOTH, FILE_DEVICE_SECURE_OPEN,
                                 FALSE, &fiDo);
    if (!NT_SUCCESS(st) || !fiDo) {
        LogStr("[ERR] IoCreateDevice failed\r\n");
        FlushLogFile();
        return st;
    }

    PS23BTHX_EXT ext = (PS23BTHX_EXT)fiDo->DeviceExtension;
    MemZero(ext, sizeof(*ext));
    ext->Fdo = fiDo;
    ext->Pdo = pdo;
    IoInitializeRemoveLockEx(&ext->RemoveLock, 0x33324254u, 0, 0, sizeof(IO_REMOVE_LOCK));
    KeInitializeSpinLock(&ext->IoLock);
    KeInitializeEvent(&ext->TxEvent,     SynchronizationEvent, FALSE);
    KeInitializeEvent(&ext->StopEvent,   NotificationEvent,    FALSE);
    KeInitializeEvent(&ext->UartTxMutex, SynchronizationEvent, TRUE);

    ext->LowerDevice = IoAttachDeviceToDeviceStack(fiDo, pdo);
    if (!ext->LowerDevice) {
        IoDeleteDevice(fiDo);
        return STATUS_NO_SUCH_DEVICE;
    }

    /* Mirror lower device buffering & power flags cleanly */
    fiDo->Flags |= (ext->LowerDevice->Flags & (DO_POWER_PAGABLE)) | 0;
    fiDo->Flags &= ~DO_DEVICE_INITIALIZING;
    g_ActiveExt = ext;
    LogStr("s23bthx AddDevice OK\r\n");
    FlushLogFile();
    return STATUS_SUCCESS;
}

static VOID S23BthxUnload(PDRIVER_OBJECT drv) {
    (void)drv;
    LogStr("s23bthx DriverUnload\r\n");
    FlushLogFile();
}

/* Forced .reloc anchors for ARM64 /DYNAMICBASE */
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
PVOID gRelocAnchors[2] = { (PVOID)&DriverEntry, (PVOID)&S23BthxUnload };
volatile ULONG gRelocTouch = 1u;

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    (void)RegistryPath;
    LogStr("=== s23bthx.sys v1.0.0.5 (ARM64 WDM BTHX LowerFilter) DriverEntry ===\r\n");
    if (!gRelocAnchors[0] || !gRelocAnchors[1] || gRelocTouch == 0U) {
        return STATUS_UNSUCCESSFUL;
    }
    FlushLogFile();

    DriverObject->MajorFunction[IRP_MJ_CREATE]                  = S23BthxPassDown;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]                   = S23BthxPassDown;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP]                 = S23BthxPassDown;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL]          = S23BthxDispatchIoctl;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = S23BthxDispatchIoctl;
    DriverObject->MajorFunction[IRP_MJ_POWER]                   = S23BthxPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL]          = S23BthxPassDown;
    DriverObject->MajorFunction[IRP_MJ_PNP]                     = S23BthxPnp;
    DriverObject->DriverExtension->AddDevice                    = S23BthxAddDevice;
    DriverObject->DriverUnload                                  = S23BthxUnload;
    return STATUS_SUCCESS;
}
