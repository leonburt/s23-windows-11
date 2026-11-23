/*
 * ddi.c - WDDM 2.0 Full Display (1080x2340 @ 120Hz) + 3D DDI Layer for s23gpu.sys
 */
#include "s23gpu.h"

#define S23_QAITYPE_NODEPERFDATA         24u
#define S23_QAITYPE_ADAPTERPERFDATA      25u
#define S23_QAITYPE_ADAPTERPERFDATA_CAPS 26u
#define S23_QAITYPE_GPUVERSION           27u

#pragma pack(push, 1)
typedef struct _S23_NODE_PERFDATA {
    ULONGLONG Frequency;
    ULONGLONG MaxFrequency;
    ULONGLONG MaxFrequencyOC;
    ULONG     Voltage;
    ULONG     VoltageMax;
    ULONG     VoltageMaxOC;
} S23_NODE_PERFDATA;

typedef struct _S23_ADAPTER_PERFDATA {
    ULONGLONG MemoryFrequency;
    ULONGLONG MaxMemoryFrequency;
    ULONGLONG MaxMemoryFrequencyOC;
    ULONGLONG MemoryBandwidth;
    ULONGLONG PCIEBandwidth;
    ULONG     FanRPM;
    ULONG     Power;
    ULONG     Temperature;
    UCHAR     PowerStateOverride;
} S23_ADAPTER_PERFDATA;

typedef struct _S23_ADAPTER_PERFDATACAPS {
    ULONGLONG MaxMemoryBandwidth;
    ULONGLONG MaxPCIEBandwidth;
    ULONG     MaxFanRPM;
    ULONG     TemperatureMax;
    ULONG     TemperatureWarning;
} S23_ADAPTER_PERFDATACAPS;

typedef struct _S23_GPUVERSION {
    WCHAR BiosVersion[32];
    WCHAR GpuArchitecture[32];
} S23_GPUVERSION;
#pragma pack(pop)

C_ASSERT(sizeof(S23_NODE_PERFDATA) == 36);
C_ASSERT(sizeof(S23_ADAPTER_PERFDATA) == 53);
C_ASSERT(sizeof(S23_ADAPTER_PERFDATACAPS) == 28);
C_ASSERT(sizeof(S23_GPUVERSION) == 128);

typedef struct _NOTIFY_CTX {
    S23GPU_ADAPTER *Adapter;
    ULONG           Fence;
    BOOLEAN         IsPreempt;
} NOTIFY_CTX;

static S23GPU_ADAPTER *g_Adapter = NULL;

#define GART_PAGES ((ULONG)(S23GPU_APERTURE_SIZE / PAGE_SIZE))
static PPFN_NUMBER g_Gart = NULL;

static BOOLEAN NotifyRoutine(PVOID Ctx)
{
    NOTIFY_CTX *n = (NOTIFY_CTX *)Ctx;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA d;

    RtlZeroMemory(&d, sizeof(d));
    if (n->IsPreempt) {
        d.InterruptType = DXGK_INTERRUPT_DMA_PREEMPTED;
        d.DmaPreempted.PreemptionFenceId = n->Fence;
        d.DmaPreempted.NodeOrdinal = 0;
        d.DmaPreempted.EngineOrdinal = 0;
        d.DmaPreempted.LastCompletedFenceId = n->Adapter->LastCompletedFence;
        n->Adapter->Dxgk.DxgkCbNotifyInterrupt(n->Adapter->Dxgk.DeviceHandle, &d);
        n->Adapter->Dxgk.DxgkCbQueueDpc(n->Adapter->Dxgk.DeviceHandle);
        return TRUE;
    }
    if (n->Fence != 0u) {
        d.InterruptType = DXGK_INTERRUPT_DMA_COMPLETED;
        d.DmaCompleted.SubmissionFenceId = n->Fence;
        d.DmaCompleted.NodeOrdinal = 0;
        d.DmaCompleted.EngineOrdinal = 0;
        n->Adapter->Dxgk.DxgkCbNotifyInterrupt(n->Adapter->Dxgk.DeviceHandle, &d);
        n->Adapter->LastCompletedFence = n->Fence;
        n->Adapter->Dxgk.DxgkCbQueueDpc(n->Adapter->Dxgk.DeviceHandle);
    }
    return TRUE;
}

static VOID CompleteFenceEx(S23GPU_ADAPTER *A, ULONG Fence, BOOLEAN IsPreempt)
{
    NOTIFY_CTX n;
    BOOLEAN ret = FALSE;
    NTSTATUS st;
    static volatile LONG errLog = 0;

    n.Adapter = A;
    n.Fence = Fence;
    n.IsPreempt = IsPreempt;
    st = A->Dxgk.DxgkCbSynchronizeExecution(A->Dxgk.DeviceHandle, NotifyRoutine, &n, 0, &ret);
    if (!NT_SUCCESS(st) && KeGetCurrentIrql() == PASSIVE_LEVEL &&
        InterlockedIncrement(&errLog) <= 10) {
        LogPrint("CompleteFence %u (preempt=%u): SynchronizeExecution -> %08x ret=%u\n",
                 Fence, IsPreempt ? 1u : 0u, st, ret);
    }
}

static VOID CompleteFence(S23GPU_ADAPTER *A, ULONG Fence)
{
    CompleteFenceEx(A, Fence, FALSE);
}

static VOID PreemptFence(S23GPU_ADAPTER *A, ULONG Fence)
{
    CompleteFenceEx(A, Fence, TRUE);
}

static VOID FenceDone(PVOID Ctx, ULONG Fence, BOOLEAN IsPreempt)
{
    CompleteFenceEx((S23GPU_ADAPTER *)Ctx, Fence, IsPreempt);
}

static NTSTATUS TgAddDevice(const PDEVICE_OBJECT Pdo, PVOID *Ctx)
{
    S23GPU_ADAPTER *a = (S23GPU_ADAPTER *)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*a), S23GPU_POOL_TAG);

    if (a == NULL) {
        return STATUS_NO_MEMORY;
    }
    a->Pdo = Pdo;
    *Ctx = a;
    g_Adapter = a;
    LogPrint("AddDevice pdo=%p\n", Pdo);
    return STATUS_SUCCESS;
}

static NTSTATUS TgStartDevice(const PVOID Ctx, PDXGK_START_INFO StartInfo, PDXGKRNL_INTERFACE Dxgk,
                              PULONG NumSources, PULONG NumChildren)
{
    S23GPU_ADAPTER *a = (S23GPU_ADAPTER *)Ctx;
    NTSTATUS st;

    UNREFERENCED_PARAMETER(StartInfo);
    RtlCopyMemory(&a->Dxgk, Dxgk, sizeof(a->Dxgk));
    st = EngStart(FenceDone, a);
    if (!NT_SUCCESS(st)) {
        LogPrint("StartDevice: EngStart failed %08x\n", st);
        return st;
    }
    st = DispStart(&a->Dxgk);
    if (!NT_SUCCESS(st)) {
        LogPrint("StartDevice: DispStart failed %08x (falling back to NumSources=0)\n", st);
        *NumSources = 0;
        *NumChildren = 0;
    } else {
        *NumSources = 1;
        *NumChildren = 1;
    }
    a->Started = TRUE;
    LogPrint("StartDevice: s23gpu Full Display+3D adapter ready (NumSources=%u NumChildren=%u)\n",
             *NumSources, *NumChildren);
    return STATUS_SUCCESS;
}

static NTSTATUS TgStopDevice(const PVOID Ctx)
{
    S23GPU_ADAPTER *a = (S23GPU_ADAPTER *)Ctx;

    LogPrint("StopDevice\n");
    if (a != NULL) {
        a->Started = FALSE;
    }
    DispStop();
    EngStop();
    HwHaltForCleanup();
    MsmCleanup();
    HwStop();
    if (g_Gart != NULL) {
        ExFreePoolWithTag(g_Gart, S23GPU_POOL_TAG);
        g_Gart = NULL;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS TgRemoveDevice(const PVOID Ctx)
{
    LogPrint("RemoveDevice\n");
    if (Ctx != NULL) {
        ExFreePoolWithTag(Ctx, S23GPU_POOL_TAG);
    }
    g_Adapter = NULL;
    return STATUS_SUCCESS;
}

static NTSTATUS TgDispatchIoRequest(const PVOID Ctx, ULONG Src, PVIDEO_REQUEST_PACKET Vrp)
{
    UNREFERENCED_PARAMETER(Ctx);
    UNREFERENCED_PARAMETER(Src);
    UNREFERENCED_PARAMETER(Vrp);
    return STATUS_NOT_SUPPORTED;
}

static BOOLEAN TgInterruptRoutine(const PVOID Ctx, ULONG Msg)
{
    UNREFERENCED_PARAMETER(Ctx);
    UNREFERENCED_PARAMETER(Msg);
    return FALSE;
}

static VOID TgDpcRoutine(const PVOID Ctx)
{
    S23GPU_ADAPTER *a = (S23GPU_ADAPTER *)Ctx;
    a->Dxgk.DxgkCbNotifyDpc(a->Dxgk.DeviceHandle);
}

static NTSTATUS TgSetPowerState(const PVOID Ctx, ULONG Uid, DEVICE_POWER_STATE Ps, POWER_ACTION Action)
{
    UNREFERENCED_PARAMETER(Ctx);
    UNREFERENCED_PARAMETER(Uid);
    UNREFERENCED_PARAMETER(Ps);
    UNREFERENCED_PARAMETER(Action);
    return STATUS_SUCCESS;
}

static VOID TgResetDevice(const PVOID Ctx)
{
    UNREFERENCED_PARAMETER(Ctx);
}

static VOID TgUnload(VOID)
{
    LogPrint("DriverUnload\n");
    LogClose();
}

static NTSTATUS TgQueryInterface(const PVOID Ctx, PQUERY_INTERFACE Qi)
{
    return DispQueryInterface(Ctx, Qi);
}

static NTSTATUS QueryAdapterInfoInner(const S23GPU_ADAPTER *A, const DXGKARG_QUERYADAPTERINFO *Q)
{
    switch (Q->Type) {
    case DXGKQAITYPE_DRIVERCAPS: {
        DXGK_DRIVERCAPS *c = (DXGK_DRIVERCAPS *)Q->pOutputData;
        if ((SIZE_T)Q->OutputDataSize < FIELD_OFFSET(DXGK_DRIVERCAPS, SupportSmoothRotation)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(c, Q->OutputDataSize);
        c->WDDMVersion = DXGKDDI_WDDMv2;
        c->HighestAcceptableAddress.QuadPart = S23GPU_SMMU_PA_LIMIT;
        c->MaxAllocationListSlotId = 16;
        c->SchedulingCaps.MultiEngineAware = 1;
        c->SchedulingCaps.PreemptionAware = 1;
        c->SchedulingCaps.CancelCommandAware = 1;
        c->PreemptionCaps.GraphicsPreemptionGranularity = D3DKMDT_GRAPHICS_PREEMPTION_DMA_BUFFER_BOUNDARY;
        c->PreemptionCaps.ComputePreemptionGranularity = D3DKMDT_COMPUTE_PREEMPTION_DMA_BUFFER_BOUNDARY;
        c->SupportDirectFlip = 1;
        c->GpuEngineTopology.NbAsymetricProcessingNodes = 1;
        c->SupportNonVGA = TRUE;
        c->GammaRampCaps.Gamma_Rgb256x3x16 = 1;
        c->FlipCaps.FlipOnVSyncMmIo = 1;
        c->MaxQueuedFlipOnVSync = 1;
        c->MemoryManagementCaps.SectionBackedPrimary = 1;
        c->MemoryManagementCaps.VirtualAddressingSupported = 0;
        c->MemoryManagementCaps.GpuMmuSupported = 0;
        c->MemoryManagementCaps.IoMmuSupported = 0;
        c->MemoryManagementCaps.PagingNode = 0;
        c->PresentationCaps.NoScreenToScreenBlt = 1;
        c->PresentationCaps.NoOverlapScreenBlt = 1;
        c->PresentationCaps.AlignmentShift = 2;
        c->PresentationCaps.MaxTextureWidthShift = 2;
        c->PresentationCaps.MaxTextureHeightShift = 2;
        if ((SIZE_T)Q->OutputDataSize >= sizeof(DXGK_DRIVERCAPS)) {
            c->HybridIntegrated = FALSE;
            c->InternalGpuVirtualAddressRangeStart = 0;
            c->InternalGpuVirtualAddressRangeEnd = 0;
        }
        return STATUS_SUCCESS;
    }
    case DXGKQAITYPE_QUERYSEGMENT3: {
        DXGK_QUERYSEGMENTOUT3 *s = (DXGK_QUERYSEGMENTOUT3 *)Q->pOutputData;
        if (Q->OutputDataSize < sizeof(*s)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        if (s->pSegmentDescriptor == NULL) {
            s->NbSegment = 1;
        } else {
            DXGK_SEGMENTDESCRIPTOR3 *d = s->pSegmentDescriptor;
            RtlZeroMemory(&d[0], sizeof(d[0]));
            s->PagingBufferPrivateDataSize = 16 * sizeof(S23GPU_CMD);
            s->PagingBufferSegmentId = 1;
            s->PagingBufferSize = 16 * PAGE_SIZE;
            d[0].BaseAddress.QuadPart = S23GPU_APERTURE_BASE;
            d[0].Size = S23GPU_APERTURE_SIZE;
            d[0].CommitLimit = S23GPU_APERTURE_SIZE;
            d[0].Flags.Aperture = TRUE;
            d[0].Flags.CacheCoherent = TRUE;
            d[0].Flags.DirectFlip = TRUE;
        }
        return STATUS_SUCCESS;
    }
    case DXGKQAITYPE_QUERYSEGMENT4: {
        DXGK_QUERYSEGMENTOUT4 *s = (DXGK_QUERYSEGMENTOUT4 *)Q->pOutputData;
        if (Q->OutputDataSize < sizeof(*s)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        LogPrint("QueryAdapterInfo QUERYSEGMENT4 pDesc=%p inStride=%llu sizeof=%llu\n",
                 s->pSegmentDescriptor, (ULONGLONG)s->SegmentDescriptorStride,
                 (ULONGLONG)sizeof(DXGK_SEGMENTDESCRIPTOR4));
        s->NbSegment = 1;
        if (s->pSegmentDescriptor == NULL || s->SegmentDescriptorStride < sizeof(DXGK_SEGMENTDESCRIPTOR4)) {
            s->SegmentDescriptorStride = sizeof(DXGK_SEGMENTDESCRIPTOR4);
        }
        s->PagingBufferSegmentId = 1;
        s->PagingBufferSize = 16 * PAGE_SIZE;
        s->PagingBufferPrivateDataSize = 16 * sizeof(S23GPU_CMD);
        if (s->pSegmentDescriptor != NULL) {
            DXGK_SEGMENTDESCRIPTOR4 *d = (DXGK_SEGMENTDESCRIPTOR4 *)s->pSegmentDescriptor;
            RtlZeroMemory(&d[0], sizeof(d[0]));
            d[0].BaseAddress.QuadPart = S23GPU_APERTURE_BASE;
            d[0].Size = S23GPU_APERTURE_SIZE;
            d[0].CommitLimit = S23GPU_APERTURE_SIZE;
            d[0].Flags.Aperture = TRUE;
            d[0].Flags.CacheCoherent = TRUE;
            d[0].Flags.DirectFlip = TRUE;
        }
        return STATUS_SUCCESS;
    }
    case DXGKQAITYPE_PHYSICALADAPTERCAPS: {
        DXGK_PHYSICALADAPTERCAPS *p = (DXGK_PHYSICALADAPTERCAPS *)Q->pOutputData;
        SIZE_T need = FIELD_OFFSET(DXGK_PHYSICALADAPTERCAPS, Flags) + sizeof(p->Flags);
        if (Q->OutputDataSize < need) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        LogPrint("QueryAdapterInfo PHYSICALADAPTERCAPS inHandle=%p size=%u need=%u\n",
                 p->DxgkPhysicalAdapterHandle, Q->OutputDataSize, (ULONG)need);
        if (p->DxgkPhysicalAdapterHandle == NULL && A != NULL) {
            p->DxgkPhysicalAdapterHandle = A->Dxgk.DeviceHandle;
        }
        p->NumExecutionNodes = 1;
        p->PagingNodeIndex = 0;
        p->Flags.Value = 0;
        return STATUS_SUCCESS;
    }
    case DXGKQAITYPE_GPUMMUCAPS: {
        DXGK_GPUMMUCAPS *g = (DXGK_GPUMMUCAPS *)Q->pOutputData;
        if (Q->OutputDataSize < sizeof(*g)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(g, Q->OutputDataSize);
        g->ReadOnlyMemorySupported = 1;
        g->NoExecuteMemorySupported = 1;
        g->ZeroInPteSupported = 1;
        g->CacheCoherentMemorySupported = 1;
        g->PageTableUpdateMode = DXGK_PAGETABLEUPDATE_CPU_VIRTUAL;
        g->VirtualAddressBitCount = 39;
        g->LeafPageTableSizeFor64KPagesInBytes = 0;
        g->PageTableLevelCount = 3;
        return STATUS_SUCCESS;
    }
    case DXGKQAITYPE_PAGETABLELEVELDESC: {
        const DXGK_QUERYPAGETABLELEVELDESCIN *in = (const DXGK_QUERYPAGETABLELEVELDESCIN *)Q->pInputData;
        DXGK_PAGE_TABLE_LEVEL_DESC *d = (DXGK_PAGE_TABLE_LEVEL_DESC *)Q->pOutputData;
        if (in == NULL || Q->InputDataSize < sizeof(*in) || in->LevelIndex >= 3u) {
            return STATUS_INVALID_PARAMETER;
        }
        if (Q->OutputDataSize < sizeof(*d)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(d, Q->OutputDataSize);
        d->PageTableIndexBitCount = 9;
        d->PageTableSegmentId = 0;
        d->PagingProcessPageTableSegmentId = 0;
        d->PageTableSizeInBytes = PAGE_SIZE;
        d->PageTableAlignmentInBytes = PAGE_SIZE;
        return STATUS_SUCCESS;
    }
    case DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION:
        if (Q->OutputDataSize < sizeof(DXGK_DISPLAY_DRIVERCAPS_EXTENSION)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(Q->pOutputData, Q->OutputDataSize);
        return STATUS_SUCCESS;
    case S23_QAITYPE_NODEPERFDATA: {
        S23_NODE_PERFDATA *np = (S23_NODE_PERFDATA *)Q->pOutputData;
        if (Q->OutputDataSize < sizeof(*np)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(np, Q->OutputDataSize);
        np->Frequency = S23GPU_MAX_FREQ_HZ;
        np->MaxFrequency = S23GPU_MAX_FREQ_HZ;
        np->MaxFrequencyOC = S23GPU_MAX_FREQ_HZ;
        return STATUS_SUCCESS;
    }
    case S23_QAITYPE_ADAPTERPERFDATA: {
        S23_ADAPTER_PERFDATA *ap = (S23_ADAPTER_PERFDATA *)Q->pOutputData;
        if (Q->OutputDataSize < sizeof(*ap)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(ap, Q->OutputDataSize);
        ap->MemoryFrequency = 3200000000ULL;
        ap->MaxMemoryFrequency = 3200000000ULL;
        ap->MaxMemoryFrequencyOC = 3200000000ULL;

        ULONG temp = HwReadTsens();
        if (temp > 0 && temp < 1500) {
            ap->Temperature = temp; /* The WDDM schema expects 0.1C / deciCelsius for DXGKQAITYPE_ADAPTERPERFDATA Temperature */
        }
        return STATUS_SUCCESS;
    }
    case S23_QAITYPE_ADAPTERPERFDATA_CAPS:
        if (Q->OutputDataSize < sizeof(S23_ADAPTER_PERFDATACAPS)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(Q->pOutputData, Q->OutputDataSize);
        ((S23_ADAPTER_PERFDATACAPS *)Q->pOutputData)->TemperatureMax = 120;
        ((S23_ADAPTER_PERFDATACAPS *)Q->pOutputData)->TemperatureWarning = 95;
        return STATUS_SUCCESS;
    case S23_QAITYPE_GPUVERSION: {
        S23_GPUVERSION *gv = (S23_GPUVERSION *)Q->pOutputData;
        if (Q->OutputDataSize < sizeof(*gv)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        RtlZeroMemory(gv, Q->OutputDataSize);
        RtlStringCbCopyW(gv->BiosVersion, sizeof(gv->BiosVersion), L"A740.4001.0018");
        RtlStringCbCopyW(gv->GpuArchitecture, sizeof(gv->GpuArchitecture), L"Adreno 740 (GEN7_2_1)");
        return STATUS_SUCCESS;
    }
    case 6: /* DXGKQAITYPE_NUMPOWERCOMPONENTS */
        if (Q->OutputDataSize < sizeof(UINT)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        *(UINT *)Q->pOutputData = 1;
        return STATUS_SUCCESS;
    case 7: { /* DXGKQAITYPE_POWERCOMPONENTINFO */
        if (Q->OutputDataSize < sizeof(DXGK_POWER_RUNTIME_COMPONENT) * 1) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        DXGK_POWER_RUNTIME_COMPONENT *pc = (DXGK_POWER_RUNTIME_COMPONENT *)Q->pOutputData;
        RtlZeroMemory(pc, sizeof(DXGK_POWER_RUNTIME_COMPONENT) * 1);
        pc[0].StateCount = 1;
        pc[0].ComponentMapping.ComponentType = DXGK_POWER_COMPONENT_OTHER; /* Temperature */
        pc[0].Flags.Value = 0;
        pc[0].Flags.ActiveInD3 = 1;
        RtlStringCbCopyA((LPSTR)pc[0].ComponentName, sizeof(pc[0].ComponentName), "GPU_TSENS");
        return STATUS_SUCCESS;
    }
    case DXGKQAITYPE_HISTORYBUFFERPRECISION: {
        DXGKARG_HISTORYBUFFERPRECISION *h = (DXGKARG_HISTORYBUFFERPRECISION *)Q->pOutputData;
        if (Q->OutputDataSize < sizeof(*h)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        h->PrecisionBits = 64;
        return STATUS_SUCCESS;
    }
    case DXGKQAITYPE_UMDRIVERPRIVATE:
        if (Q->OutputDataSize < sizeof(ULONG)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        *(PULONG)Q->pOutputData = TOPAZGPU_ESC_MAGIC;
        return STATUS_SUCCESS;
    default:
        return STATUS_NOT_SUPPORTED;
    }
}

static NTSTATUS APIENTRY TgQueryAdapterInfo(const HANDLE hAdapter, const DXGKARG_QUERYADAPTERINFO *Q)
{
    NTSTATUS st;
    static ULONG qaLog = 0;

    st = QueryAdapterInfoInner((const S23GPU_ADAPTER *)hAdapter, Q);
    if ((Q->Type != 24u && Q->Type != 25u) || ++qaLog <= 40u) {
        LogPrint("QueryAdapterInfo type=%u in=%u out=%u -> %08x\n", Q->Type, Q->InputDataSize, Q->OutputDataSize, st);
    }
    return st;
}

static NTSTATUS APIENTRY TgEscape(const HANDLE hAdapter, const DXGKARG_ESCAPE *E)
{
    UNREFERENCED_PARAMETER(hAdapter);
    if (E == NULL || E->PrivateDriverDataSize < sizeof(struct topazgpu_escape) || E->pPrivateDriverData == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    return MsmEscape((struct topazgpu_escape *)E->pPrivateDriverData, E->hDevice);
}

static NTSTATUS APIENTRY TgCreateDevice(const HANDLE hAdapter, DXGKARG_CREATEDEVICE *A)
{
    S23GPU_DEVICE *d = (S23GPU_DEVICE *)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*d), S23GPU_POOL_TAG);

    if (d == NULL) {
        return STATUS_NO_MEMORY;
    }
    d->Adapter = (S23GPU_ADAPTER *)hAdapter;
    d->hRtDevice = A->hDevice;
    d->CtxRestoreDone = FALSE;
    A->hDevice = d;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgDestroyDevice(const HANDLE hDevice)
{
    MsmReleaseOwner(hDevice);
    if (hDevice != NULL) {
        ExFreePoolWithTag(hDevice, S23GPU_POOL_TAG);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgCreateContext(const HANDLE hDevice, DXGKARG_CREATECONTEXT *A)
{
    S23GPU_CONTEXT *c = (S23GPU_CONTEXT *)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*c), S23GPU_POOL_TAG);

    if (c == NULL) {
        return STATUS_NO_MEMORY;
    }
    c->Device = (S23GPU_DEVICE *)hDevice;
    A->hContext = c;
    RtlZeroMemory(&A->ContextInfo, sizeof(A->ContextInfo));
    A->ContextInfo.DmaBufferSize = 16 * PAGE_SIZE;
    A->ContextInfo.DmaBufferPrivateDataSize = 4 * sizeof(S23GPU_CMD);
    A->ContextInfo.DmaBufferSegmentSet = A->Flags.VirtualAddressing ? 0u : 1u;
    A->ContextInfo.AllocationListSize = 64;
    A->ContextInfo.PatchLocationListSize = 64;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgDestroyContext(const HANDLE hContext)
{
    if (hContext != NULL) {
        ExFreePoolWithTag(hContext, S23GPU_POOL_TAG);
    }
    return STATUS_SUCCESS;
}

/* Aperture page table ("GART") for allocations mapped by VidMM with hAllocation == NULL */
static VOID GartMap(SIZE_T OffsetInPages, PMDL Mdl, ULONG MdlOffset, SIZE_T Pages)
{
    PPFN_NUMBER pfn;
    SIZE_T i;

    if (g_Gart == NULL) {
        g_Gart = (PPFN_NUMBER)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                              (SIZE_T)GART_PAGES * sizeof(PFN_NUMBER),
                                              S23GPU_POOL_TAG);
        if (g_Gart == NULL) {
            return;
        }
    }
    if (Mdl == NULL || OffsetInPages + Pages > GART_PAGES) {
        return;
    }
    pfn = MmGetMdlPfnArray(Mdl);
    for (i = 0; i < Pages; i++) {
        g_Gart[OffsetInPages + i] = pfn[MdlOffset + i];
    }
}

static VOID GartUnmap(SIZE_T OffsetInPages, SIZE_T Pages)
{
    if (g_Gart != NULL && OffsetInPages + Pages <= GART_PAGES) {
        RtlZeroMemory(&g_Gart[OffsetInPages], Pages * sizeof(PFN_NUMBER));
    }
}

PVOID GartMapVa(ULONGLONG Addr, SIZE_T Bytes, PMDL *Mdl)
{
    SIZE_T first, pages, i;
    PMDL m;
    PPFN_NUMBER pfn;
    PVOID va;

    *Mdl = NULL;
    if (g_Gart == NULL || Addr < S23GPU_APERTURE_BASE || (Addr & (PAGE_SIZE - 1)) != 0 || Bytes == 0) {
        return NULL;
    }
    first = (SIZE_T)((Addr - S23GPU_APERTURE_BASE) / PAGE_SIZE);
    pages = ROUND_TO_PAGES(Bytes) / PAGE_SIZE;
    if (first + pages > GART_PAGES) {
        return NULL;
    }
    m = (PMDL)ExAllocatePool2(POOL_FLAG_NON_PAGED, MmSizeOfMdl(NULL, pages * PAGE_SIZE), S23GPU_POOL_TAG);
    if (m == NULL) {
        return NULL;
    }
    MmInitializeMdl(m, NULL, pages * PAGE_SIZE);
    pfn = MmGetMdlPfnArray(m);
    for (i = 0; i < pages; i++) {
        pfn[i] = g_Gart[first + i];
        if (pfn[i] == 0) {
            ExFreePoolWithTag(m, S23GPU_POOL_TAG);
            return NULL;
        }
    }
    m->MdlFlags |= MDL_PAGES_LOCKED;
    va = MmMapLockedPagesSpecifyCache(m, KernelMode, MmCached, NULL, FALSE,
                                      NormalPagePriority | MdlMappingNoExecute);
    if (va == NULL) {
        ExFreePoolWithTag(m, S23GPU_POOL_TAG);
        return NULL;
    }
    *Mdl = m;
    return va;
}

VOID GartUnmapVa(PVOID Va, PMDL Mdl)
{
    if (Va != NULL && Mdl != NULL) {
        MmUnmapLockedPages(Va, Mdl);
        ExFreePoolWithTag(Mdl, S23GPU_POOL_TAG);
    }
}

PUCHAR AllocPixels(S23GPU_ALLOCATION *Al)
{
    if (Al == NULL) {
        return NULL;
    }
    if (Al->Bo != NULL) {
        return Al->Bo->KernelVa != NULL ? (PUCHAR)Al->Bo->KernelVa + Al->Desc.bo_offset : NULL;
    }
    return (PUCHAR)Al->ApVa;
}

static VOID AllocUnmapAperture(S23GPU_ALLOCATION *Al)
{
    if (Al == NULL || Al->ApVa == NULL) {
        return;
    }
    DispScanoutLock();
    if (Al->ApVa != NULL) {
        MmUnmapLockedPages(Al->ApVa, Al->ApMdl);
        if (Al->ApPartial != NULL) {
            IoFreeMdl(Al->ApPartial);
        }
        Al->ApVa = NULL;
        Al->ApMdl = NULL;
        Al->ApPartial = NULL;
        Al->ApBytes = 0;
    }
    DispScanoutUnlock();
}

static VOID AllocMapAperture(S23GPU_ALLOCATION *Al, PMDL Mdl, ULONG MdlOffset, SIZE_T Pages)
{
    PMDL m = Mdl;
    PVOID va;

    if (Al == NULL || Mdl == NULL || Al->Bo != NULL || Pages == 0u) {
        return;
    }
    AllocUnmapAperture(Al);
    if (MdlOffset != 0u) {
        PUCHAR baseVa = (PUCHAR)MmGetMdlVirtualAddress(Mdl) + (SIZE_T)MdlOffset * PAGE_SIZE;
        m = IoAllocateMdl(baseVa, (ULONG)(Pages * PAGE_SIZE), FALSE, FALSE, NULL);
        if (m == NULL) {
            return;
        }
        IoBuildPartialMdl(Mdl, m, baseVa, (ULONG)(Pages * PAGE_SIZE));
        Al->ApPartial = m;
    }
    va = MmMapLockedPagesSpecifyCache(m, KernelMode, MmCached, NULL, FALSE,
                                      NormalPagePriority | MdlMappingNoExecute);
    if (va == NULL) {
        if (Al->ApPartial != NULL) {
            IoFreeMdl(Al->ApPartial);
            Al->ApPartial = NULL;
        }
        return;
    }
    Al->ApMdl = m;
    Al->ApVa = va;
    Al->ApBytes = Pages * PAGE_SIZE;
}

static VOID AllocRelease(S23GPU_ALLOCATION *Al)
{
    if (Al == NULL || InterlockedDecrement(&Al->Refs) != 0) {
        return;
    }
    AllocUnmapAperture(Al);
    if (Al->Bo != NULL) {
        MsmBoRelease(Al->Bo);
    }
    ExFreePoolWithTag(Al, S23GPU_POOL_TAG);
}

static const struct topazgpu_alloc *FindAllocDesc(const VOID *Priv, UINT Size)
{
    const struct topazgpu_alloc *d = (const struct topazgpu_alloc *)Priv;
    return (d != NULL && Size >= sizeof(*d) && d->magic == TOPAZGPU_ALLOC_MAGIC) ? d : NULL;
}

static NTSTATUS APIENTRY TgCreateAllocation(const HANDLE hAdapter, DXGKARG_CREATEALLOCATION *A)
{
    ULONG i;
    static ULONG logCount = 0;

    UNREFERENCED_PARAMETER(hAdapter);
    for (i = 0; i < A->NumAllocations; i++) {
        DXGK_ALLOCATIONINFO *ai = &A->pAllocationInfo[i];
        const struct topazgpu_alloc *d = FindAllocDesc(ai->pPrivateDriverData, ai->PrivateDriverDataSize);
        S23GPU_ALLOCATION *al;

        if (d == NULL) {
            d = FindAllocDesc(A->pPrivateDriverData, A->PrivateDriverDataSize);
        }
        al = (S23GPU_ALLOCATION *)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*al), S23GPU_POOL_TAG);
        if (al == NULL) {
            return STATUS_NO_MEMORY;
        }
        al->Refs = 1;
        if (d != NULL) {
            al->Desc = *d;
        } else {
            al->Desc.magic = TOPAZGPU_ALLOC_MAGIC;
            al->Desc.size = PAGE_SIZE;
        }
        if (al->Desc.size == 0) {
            al->Desc.size = al->Desc.pitch * al->Desc.height;
        }
        al->Size = ROUND_TO_PAGES(al->Desc.size != 0 ? al->Desc.size : PAGE_SIZE);

        /* GDI texture surfaces (window redirection) get a BO so DWM on the UMD can sample them */
        if (al->Desc.bo == 0 && al->Desc.kind == TOPAZGPU_ALLOC_GDI &&
            al->Desc.reserved[0] == D3DKMDT_GDISURFACE_TEXTURE &&
            d != NULL && ai->pPrivateDriverData != NULL &&
            ai->PrivateDriverDataSize >= sizeof(*d)) {
            al->Bo = MsmBoCreate(al->Size);
            if (al->Bo != NULL) {
                al->Desc.bo = al->Bo->Handle;
                al->Desc.bo_offset = 0;
                ((struct topazgpu_alloc *)ai->pPrivateDriverData)->bo = al->Desc.bo;
            }
        } else if (al->Desc.bo != 0) {
            al->Bo = MsmBoAcquire(al->Desc.bo);
            /* 64-bit sum: bo_offset and size are 32-bit values from the (user-mode) allocation private
             * data; a 32-bit sum can wrap and let AllocPixels() point far outside the BO. */
            if (al->Bo == NULL ||
                (ULONGLONG)al->Bo->Size < (ULONGLONG)al->Desc.bo_offset + (ULONGLONG)al->Desc.size) {
                if (al->Bo != NULL) {
                    MsmBoRelease(al->Bo);
                }
                ExFreePoolWithTag(al, S23GPU_POOL_TAG);
                return STATUS_INVALID_PARAMETER;
            }
        }
        ai->hAllocation = al;
        ai->Alignment = 0;
        ai->Size = al->Size;
        ai->PitchAlignedSize = 0;
        ai->HintedBank.Value = 0;
        ai->PreferredSegment.Value = 0;
        ai->PreferredSegment.SegmentId0 = 1;
        ai->SupportedReadSegmentSet = 1;
        ai->SupportedWriteSegmentSet = 1;
        ai->EvictionSegmentSet = 1;
        ai->MaximumRenamingListLength = 0;
        ai->AllocationPriority = D3DDDI_ALLOCATIONPRIORITY_NORMAL;
        ai->Flags.Value = 0;
        ai->Flags.CpuVisible = 1;
        if (++logCount <= 30) {
            LogPrint("CreateAllocation %p: kind=%u %ux%u pitch=%u fmt=%u bo=%u sz=%u%s\n",
                     al, al->Desc.kind, al->Desc.width, al->Desc.height, al->Desc.pitch,
                     al->Desc.format, al->Desc.bo, (ULONG)al->Size,
                     (al->Desc.flags & TOPAZGPU_ALLOC_F_PRIMARY) ? " PRIMARY" : "");
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgDestroyAllocation(const HANDLE hAdapter, const DXGKARG_DESTROYALLOCATION *A)
{
    ULONG i;

    UNREFERENCED_PARAMETER(hAdapter);
    for (i = 0; i < A->NumAllocations; i++) {
        S23GPU_ALLOCATION *al = (S23GPU_ALLOCATION *)A->pAllocationList[i];
        DispAllocDestroyed(al);
        AllocRelease(al);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgDescribeAllocation(const HANDLE hAdapter, DXGKARG_DESCRIBEALLOCATION *A)
{
    S23GPU_ALLOCATION *al = (S23GPU_ALLOCATION *)A->hAllocation;

    UNREFERENCED_PARAMETER(hAdapter);
    A->Width = al->Desc.width;
    A->Height = al->Desc.height;
    A->Format = al->Desc.format != 0 ? (D3DDDIFORMAT)al->Desc.format : D3DDDIFMT_X8R8G8B8;
    A->MultisampleMethod.NumSamples = 0;
    A->MultisampleMethod.NumQualityLevels = 0;
    A->RefreshRate.Numerator = 120;
    A->RefreshRate.Denominator = 1;
    A->PrivateDriverFormatAttribute = 0;
    A->Rotation = D3DDDI_ROTATION_IDENTITY;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgGetStandardAllocationDriverData(const HANDLE hAdapter,
                                                           DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA *A)
{
    struct topazgpu_alloc d;

    UNREFERENCED_PARAMETER(hAdapter);
    RtlZeroMemory(&d, sizeof(d));
    d.magic = TOPAZGPU_ALLOC_MAGIC;
    d.kind = A->StandardAllocationType;
    switch (A->StandardAllocationType) {
    case D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE:
        d.width = A->pCreateSharedPrimarySurfaceData->Width;
        d.height = A->pCreateSharedPrimarySurfaceData->Height;
        d.format = A->pCreateSharedPrimarySurfaceData->Format;
        d.vidpn = A->pCreateSharedPrimarySurfaceData->VidPnSourceId;
        d.flags = TOPAZGPU_ALLOC_F_PRIMARY;
        break;
    case D3DKMDT_STANDARDALLOCATION_SHADOWSURFACE:
        d.width = A->pCreateShadowSurfaceData->Width;
        d.height = A->pCreateShadowSurfaceData->Height;
        d.format = A->pCreateShadowSurfaceData->Format;
        A->pCreateShadowSurfaceData->Pitch = d.width * 4;
        break;
    case D3DKMDT_STANDARDALLOCATION_STAGINGSURFACE:
        d.width = A->pCreateStagingSurfaceData->Width;
        d.height = A->pCreateStagingSurfaceData->Height;
        d.format = D3DDDIFMT_X8R8G8B8;
        A->pCreateStagingSurfaceData->Pitch = d.width * 4;
        break;
    case D3DKMDT_STANDARDALLOCATION_GDISURFACE:
        d.width = A->pCreateGdiSurfaceData->Width;
        d.height = A->pCreateGdiSurfaceData->Height;
        d.format = A->pCreateGdiSurfaceData->Format;
        d.reserved[0] = A->pCreateGdiSurfaceData->Type;
        A->pCreateGdiSurfaceData->Pitch = d.width * 4;
        break;
    default:
        return STATUS_NOT_SUPPORTED;
    }
    d.pitch = d.width * 4;
    d.size = d.pitch * d.height;
    if (A->pAllocationPrivateDriverData != NULL) {
        RtlCopyMemory(A->pAllocationPrivateDriverData, &d, sizeof(d));
    }
    A->AllocationPrivateDriverDataSize = sizeof(d);
    A->ResourcePrivateDriverDataSize = 0;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgOpenAllocation(const HANDLE hDevice, const DXGKARG_OPENALLOCATION *A)
{
    ULONG i;
    S23GPU_DEVICE *d = (S23GPU_DEVICE *)hDevice;

    for (i = 0; i < A->NumAllocations; i++) {
        DXGKARGCB_GETHANDLEDATA g;
        g.hObject = A->pOpenAllocation[i].hAllocation;
        g.Type = DXGK_HANDLE_ALLOCATION;
        g.Flags.Value = 0;
        A->pOpenAllocation[i].hDeviceSpecificAllocation = d->Adapter->Dxgk.DxgkCbGetHandleData(&g);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgCloseAllocation(const HANDLE hDevice, const DXGKARG_CLOSEALLOCATION *A)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(A);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgRender(const HANDLE hContext, DXGKARG_RENDER *A)
{
    UNREFERENCED_PARAMETER(hContext);
    if (A == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (A->pDmaBuffer == NULL || A->DmaSize < A->CommandLength) {
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    }
    A->pDmaBuffer = (PUCHAR)A->pDmaBuffer + A->CommandLength;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgPresent(const HANDLE hContext, DXGKARG_PRESENT *A)
{
    S23GPU_CMD *c = (S23GPU_CMD *)A->pDmaBufferPrivateData;
    const DXGK_ALLOCATIONLIST *src = A->pAllocationList ? &A->pAllocationList[DXGK_PRESENT_SOURCE_INDEX] : NULL;
    const DXGK_ALLOCATIONLIST *dst = A->pAllocationList ? &A->pAllocationList[DXGK_PRESENT_DESTINATION_INDEX] : NULL;
    ULONG i;

    UNREFERENCED_PARAMETER(hContext);
    if (A->Flags.Flip) {
        return STATUS_SUCCESS;
    }
    if (A->DmaSize < 4u || A->DmaBufferPrivateDataSize < sizeof(S23GPU_CMD) || c == NULL || A->pDmaBuffer == NULL) {
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    }
    RtlZeroMemory(c, sizeof(*c));
    c->Op = S23GPU_CMD_NOP;
    if ((A->Flags.Blt || A->Flags.ColorFill) && dst != NULL && dst->hDeviceSpecificAllocation != NULL) {
        c->Op = A->Flags.Blt ? S23GPU_CMD_BLT : S23GPU_CMD_FILL;
        c->Src = src ? (S23GPU_ALLOCATION *)src->hDeviceSpecificAllocation : NULL;
        c->Dst = (S23GPU_ALLOCATION *)dst->hDeviceSpecificAllocation;
        c->SrcRect = A->SrcRect;
        c->DstRect = A->DstRect;
        c->Color = A->Color;
        if (A->SubRectCnt > 0u && A->SubRectCnt <= S23GPU_CMD_MAX_RECTS && A->pDstSubRects != NULL) {
            c->NumRects = A->SubRectCnt;
            for (i = 0; i < A->SubRectCnt; i++) {
                c->Rects[i] = A->pDstSubRects[i];
            }
        } else if (A->SubRectCnt > S23GPU_CMD_MAX_RECTS && A->pDstSubRects != NULL) {
            RECT b = A->pDstSubRects[0];
            for (i = 1; i < A->SubRectCnt; i++) {
                if (A->pDstSubRects[i].left < b.left)     b.left = A->pDstSubRects[i].left;
                if (A->pDstSubRects[i].top < b.top)       b.top = A->pDstSubRects[i].top;
                if (A->pDstSubRects[i].right > b.right)   b.right = A->pDstSubRects[i].right;
                if (A->pDstSubRects[i].bottom > b.bottom) b.bottom = A->pDstSubRects[i].bottom;
            }
            c->NumRects = 1u;
            c->Rects[0] = b;
        }
        if (c->Op == S23GPU_CMD_BLT && c->Src == NULL) {
            c->Op = S23GPU_CMD_NOP;
        }
    }
    if (c->Op != S23GPU_CMD_NOP && A->pPatchLocationListOut != NULL && A->PatchLocationListOutSize >= 2u) {
        D3DDDI_PATCHLOCATIONLIST *pl = A->pPatchLocationListOut;
        RtlZeroMemory(pl, 2u * sizeof(*pl));
        pl[0].AllocationIndex = (c->Op == S23GPU_CMD_FILL) ? DXGK_PRESENT_DESTINATION_INDEX : DXGK_PRESENT_SOURCE_INDEX;
        pl[0].SlotId = 1u;
        pl[0].DriverId = 1u;
        if (c->Op == S23GPU_CMD_FILL) {
            A->pPatchLocationListOut = pl + 1;
        } else {
            pl[1].AllocationIndex = DXGK_PRESENT_DESTINATION_INDEX;
            pl[1].SlotId = 2u;
            pl[1].DriverId = 2u;
            A->pPatchLocationListOut = pl + 2;
        }
    }
    *(PULONG)A->pDmaBuffer = c->Op;
    A->pDmaBuffer = (PUCHAR)A->pDmaBuffer + 4;
    A->pDmaBufferPrivateData = c + 1;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgPatch(const HANDLE hAdapter, const DXGKARG_PATCH *A)
{
    ULONG off;

    UNREFERENCED_PARAMETER(hAdapter);
    if (A->pDmaBufferPrivateData == NULL || A->pAllocationList == NULL) {
        return STATUS_SUCCESS;
    }
    for (off = A->DmaBufferPrivateDataSubmissionStartOffset;
         off + sizeof(S23GPU_CMD) <= A->DmaBufferPrivateDataSubmissionEndOffset;
         off += (ULONG)sizeof(S23GPU_CMD)) {
        S23GPU_CMD *c = (S23GPU_CMD *)((PUCHAR)A->pDmaBufferPrivateData + off);
        if (c->Op != S23GPU_CMD_BLT && c->Op != S23GPU_CMD_FILL) {
            continue;
        }
        if (A->AllocationListSize > DXGK_PRESENT_DESTINATION_INDEX) {
            c->SrcSeg = A->pAllocationList[DXGK_PRESENT_SOURCE_INDEX].SegmentId;
            c->SrcAddr = (ULONGLONG)A->pAllocationList[DXGK_PRESENT_SOURCE_INDEX].PhysicalAddress.QuadPart;
            c->DstSeg = A->pAllocationList[DXGK_PRESENT_DESTINATION_INDEX].SegmentId;
            c->DstAddr = (ULONGLONG)A->pAllocationList[DXGK_PRESENT_DESTINATION_INDEX].PhysicalAddress.QuadPart;
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgSubmitCommand(const HANDLE hAdapter, const DXGKARG_SUBMITCOMMAND *A)
{
    S23GPU_ADAPTER *a = (S23GPU_ADAPTER *)hAdapter;
    const UCHAR *cmds = NULL;
    ULONG bytes = 0;
    BOOLEAN isPaging = (BOOLEAN)(A->Flags.Paging || A->Flags.ContextSwitch ||
                                 A->Flags.NullRendering || A->Flags.Flip);

    if (!A->Flags.NullRendering &&
        A->pDmaBufferPrivateData != NULL &&
        A->DmaBufferPrivateDataSubmissionEndOffset > A->DmaBufferPrivateDataSubmissionStartOffset &&
        A->DmaBufferPrivateDataSubmissionEndOffset <= A->DmaBufferPrivateDataSize) {
        cmds = (const UCHAR *)A->pDmaBufferPrivateData + A->DmaBufferPrivateDataSubmissionStartOffset;
        bytes = A->DmaBufferPrivateDataSubmissionEndOffset - A->DmaBufferPrivateDataSubmissionStartOffset;
    }
    if (A->SubmissionFenceId != 0u) {
        a->LastSubmittedFence = A->SubmissionFenceId;
    }
    if (!NT_SUCCESS(EngSubmit(cmds, bytes, A->SubmissionFenceId, isPaging))) {
        CompleteFence(a, A->SubmissionFenceId);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgPreemptCommand(const HANDLE hAdapter, const DXGKARG_PREEMPTCOMMAND *A)
{
    S23GPU_ADAPTER *a = (S23GPU_ADAPTER *)hAdapter;

    if (!NT_SUCCESS(EngPreempt(A->PreemptionFenceId))) {
        PreemptFence(a, A->PreemptionFenceId);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgBuildPagingBuffer(const HANDLE hAdapter, DXGKARG_BUILDPAGINGBUFFER *A)
{
    S23GPU_CMD *c;

    UNREFERENCED_PARAMETER(hAdapter);
    switch (A->Operation) {
    case DXGK_OPERATION_MAP_APERTURE_SEGMENT:
        GartMap(A->MapApertureSegment.OffsetInPages,
                A->MapApertureSegment.pMdl,
                A->MapApertureSegment.MdlOffset,
                A->MapApertureSegment.NumberOfPages);
        AllocMapAperture((S23GPU_ALLOCATION *)A->MapApertureSegment.hAllocation,
                         A->MapApertureSegment.pMdl,
                         A->MapApertureSegment.MdlOffset,
                         A->MapApertureSegment.NumberOfPages);
        break;
    case DXGK_OPERATION_UNMAP_APERTURE_SEGMENT:
        GartUnmap(A->UnmapApertureSegment.OffsetInPages,
                  A->UnmapApertureSegment.NumberOfPages);
        AllocUnmapAperture((S23GPU_ALLOCATION *)A->UnmapApertureSegment.hAllocation);
        break;
    case DXGK_OPERATION_FILL:
        if (A->Fill.hAllocation == NULL) {
            break;
        }
        if (A->DmaSize < 4u || A->DmaBufferPrivateDataSize < sizeof(S23GPU_CMD) ||
            A->pDmaBuffer == NULL || A->pDmaBufferPrivateData == NULL) {
            return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        }
        c = (S23GPU_CMD *)A->pDmaBufferPrivateData;
        RtlZeroMemory(c, sizeof(*c));
        c->Op = S23GPU_CMD_PG_FILL;
        c->Dst = (S23GPU_ALLOCATION *)A->Fill.hAllocation;
        c->Bytes = A->Fill.FillSize;
        c->Color = A->Fill.FillPattern;
        *(PULONG)A->pDmaBuffer = c->Op;
        A->pDmaBuffer = (PUCHAR)A->pDmaBuffer + 4;
        A->pDmaBufferPrivateData = c + 1;
        break;
    case DXGK_OPERATION_VIRTUAL_FILL:
        if (A->FillVirtual.hAllocation == NULL) {
            break;
        }
        if (A->DmaSize < 4u || A->DmaBufferPrivateDataSize < sizeof(S23GPU_CMD) ||
            A->pDmaBuffer == NULL || A->pDmaBufferPrivateData == NULL) {
            return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        }
        c = (S23GPU_CMD *)A->pDmaBufferPrivateData;
        RtlZeroMemory(c, sizeof(*c));
        c->Op = S23GPU_CMD_PG_FILL;
        c->Dst = (S23GPU_ALLOCATION *)A->FillVirtual.hAllocation;
        c->Bytes = (SIZE_T)A->FillVirtual.FillSizeInBytes;
        c->Color = A->FillVirtual.FillPattern;
        *(PULONG)A->pDmaBuffer = c->Op;
        A->pDmaBuffer = (PUCHAR)A->pDmaBuffer + 4;
        A->pDmaBufferPrivateData = c + 1;
        break;
    case DXGK_OPERATION_UPDATE_PAGE_TABLE:
    case DXGK_OPERATION_FLUSH_TLB:
    case DXGK_OPERATION_COPY_PAGE_TABLE_ENTRIES:
    case DXGK_OPERATION_UPDATE_CONTEXT_ALLOCATION:
    case DXGK_OPERATION_NOTIFY_RESIDENCY:
        break;
    default:
        break;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgResetFromTimeout(const HANDLE hAdapter)
{
    UNREFERENCED_PARAMETER(hAdapter);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgRestartFromTimeout(const HANDLE hAdapter)
{
    UNREFERENCED_PARAMETER(hAdapter);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgCollectDbgInfo(const HANDLE hAdapter, const DXGKARG_COLLECTDBGINFO *A)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(A);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgQueryCurrentFence(const HANDLE hAdapter, DXGKARG_QUERYCURRENTFENCE *A)
{
    S23GPU_ADAPTER *a = (S23GPU_ADAPTER *)hAdapter;
    A->CurrentFence = a->LastCompletedFence;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgControlInterrupt(const HANDLE hAdapter, const DXGK_INTERRUPT_TYPE Type, BOOLEAN Enable)
{
    static ULONG ctlLog = 0;

    UNREFERENCED_PARAMETER(hAdapter);
    if (++ctlLog <= 40 || Type != DXGK_INTERRUPT_CRTC_VSYNC) {
        LogPrint("ControlInterrupt: type=%u enable=%u\n", Type, Enable);
    }
    if (Type == DXGK_INTERRUPT_CRTC_VSYNC) {
        DispVsyncEnable(Enable);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgGetNodeMetadata(const HANDLE hAdapter, UINT NodeOrdinal, DXGKARG_GETNODEMETADATA *A)
{
    static ULONG nmLog = 0;

    UNREFERENCED_PARAMETER(hAdapter);
    if (NodeOrdinal >= 1u) {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(A, sizeof(*A));
    A->EngineType = DXGK_ENGINE_TYPE_3D;
    RtlStringCbCopyW(A->FriendlyName, sizeof(A->FriendlyName), L"3D");
    if (++nmLog <= 8u) {
        LogPrint("GetNodeMetadata node=%u -> 3D\n", NodeOrdinal);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgCancelCommand(const HANDLE hAdapter, const DXGKARG_CANCELCOMMAND *A)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(A);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgQueryEngineStatus(const HANDLE hAdapter, DXGKARG_QUERYENGINESTATUS *A)
{
    UNREFERENCED_PARAMETER(hAdapter);
    A->EngineStatus.Responsive = 1;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgResetEngine(const HANDLE hAdapter, DXGKARG_RESETENGINE *A)
{
    S23GPU_ADAPTER *a = (S23GPU_ADAPTER *)hAdapter;
    if (A != NULL) {
        A->LastAbortedFenceId = (a != NULL) ? a->LastCompletedFence : 0u;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgCalibrateGpuClock(const HANDLE hAdapter, UINT32 NodeOrdinal, UINT32 EngineOrdinal,
                                             DXGKARG_CALIBRATEGPUCLOCK *Clock)
{
    LARGE_INTEGER freq = {0}, cpu;

    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(NodeOrdinal);
    UNREFERENCED_PARAMETER(EngineOrdinal);
    if (Clock == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    cpu = KeQueryPerformanceCounter(&freq);
    Clock->GpuFrequency = HwReady() ? 19200000ULL : (ULONGLONG)(freq.QuadPart > 0 ? freq.QuadPart : 19200000LL);
    Clock->GpuClockCounter = HwReady() ? HwTimestamp() : (ULONGLONG)cpu.QuadPart;
    Clock->CpuClockCounter = (ULONGLONG)cpu.QuadPart;
    return STATUS_SUCCESS;
}

static VOID APIENTRY TgSetStablePowerState(const HANDLE hAdapter,
                                           const DXGKARG_SETSTABLEPOWERSTATE *A)
{
    static ULONG spsLog = 0;

    UNREFERENCED_PARAMETER(hAdapter);
    if (++spsLog <= 16u) {
        LogPrint("SetStablePowerState enabled=%u\n", A ? (ULONG)A->Enabled : 0u);
    }
}

/* Display DDIs wired to disp.c (1 VidPN source, 1 target: S23 1080x2340 @ 120 Hz panel) */
static NTSTATUS TgQueryChildRelations(const PVOID Ctx, PDXGK_CHILD_DESCRIPTOR Rel, ULONG Size)
{
    UNREFERENCED_PARAMETER(Ctx);
    return DispQueryChildRelations(Rel, Size);
}

static NTSTATUS TgQueryChildStatus(const PVOID Ctx, PDXGK_CHILD_STATUS St, BOOLEAN NonDestructive)
{
    UNREFERENCED_PARAMETER(Ctx);
    UNREFERENCED_PARAMETER(NonDestructive);
    return DispQueryChildStatus(St);
}

static NTSTATUS TgQueryDeviceDescriptor(const PVOID Ctx, ULONG Uid, PDXGK_DEVICE_DESCRIPTOR Desc)
{
    UNREFERENCED_PARAMETER(Ctx);
    return DispQueryDeviceDescriptor(Uid, Desc);
}

static NTSTATUS APIENTRY TgIsSupportedVidPn(const HANDLE hAdapter, DXGKARG_ISSUPPORTEDVIDPN *A)
{
    static ULONG supLog = 0;

    UNREFERENCED_PARAMETER(hAdapter);
    A->IsVidPnSupported = TRUE;
    if (++supLog <= 20) {
        LogPrint("IsSupportedVidPn %p -> TRUE\n", A->hDesiredVidPn);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgRecommendFunctionalVidPn(const HANDLE hAdapter,
                                                    const DXGKARG_RECOMMENDFUNCTIONALVIDPN *const A)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(A);
    return STATUS_GRAPHICS_NO_RECOMMENDED_FUNCTIONAL_VIDPN;
}

static NTSTATUS APIENTRY TgRecommendVidPnTopology(const HANDLE hAdapter,
                                                  const DXGKARG_RECOMMENDVIDPNTOPOLOGY *const A)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(A);
    return STATUS_GRAPHICS_NO_RECOMMENDED_VIDPN_TOPOLOGY;
}

static NTSTATUS APIENTRY TgRecommendMonitorModes(const HANDLE hAdapter,
                                                 const DXGKARG_RECOMMENDMONITORMODES *const A)
{
    UNREFERENCED_PARAMETER(hAdapter);
    LogPrint("RecommendMonitorModes target=%u\n", A->VideoPresentTargetId);
    return DispRecommendMonitorModes(A);
}

static NTSTATUS APIENTRY TgEnumVidPnCofuncModality(const HANDLE hAdapter,
                                                   const DXGKARG_ENUMVIDPNCOFUNCMODALITY *const A)
{
    static ULONG enumLog = 0;

    if (++enumLog <= 20) {
        LogPrint("EnumVidPnCofuncModality pivot=%u\n", A->EnumPivotType);
    }
    return DispEnumCofuncModality(&((S23GPU_ADAPTER *)hAdapter)->Dxgk, A);
}

static NTSTATUS APIENTRY TgCommitVidPn(const HANDLE hAdapter, const DXGKARG_COMMITVIDPN *const A)
{
    return DispCommitVidPn(&((S23GPU_ADAPTER *)hAdapter)->Dxgk, A);
}

static NTSTATUS APIENTRY TgSetVidPnSourceAddress(const HANDLE hAdapter,
                                                 const DXGKARG_SETVIDPNSOURCEADDRESS *A)
{
    UNREFERENCED_PARAMETER(hAdapter);
    return DispSetSourceAddress(A);
}

static NTSTATUS APIENTRY TgSetVidPnSourceVisibility(const HANDLE hAdapter,
                                                    const DXGKARG_SETVIDPNSOURCEVISIBILITY *A)
{
    UNREFERENCED_PARAMETER(hAdapter);
    return DispSetVisibility(A);
}

static NTSTATUS APIENTRY TgUpdateActiveVidPnPresentPath(const HANDLE hAdapter,
                                                        const DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *const A)
{
    UNREFERENCED_PARAMETER(hAdapter);
    return DispUpdateActiveVidPnPresentPath(A);
}

static NTSTATUS APIENTRY TgGetScanLine(const HANDLE hAdapter, DXGKARG_GETSCANLINE *A)
{
    UNREFERENCED_PARAMETER(hAdapter);
    return DispGetScanLine(A);
}

static NTSTATUS APIENTRY TgStopCapture(const HANDLE hAdapter, const DXGKARG_STOPCAPTURE *A)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(A);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgSetPalette(const HANDLE hAdapter, const DXGKARG_SETPALETTE *A)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(A);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgSetPointerPosition(const HANDLE hAdapter, const DXGKARG_SETPOINTERPOSITION *A)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(A);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgSetPointerShape(const HANDLE hAdapter, const DXGKARG_SETPOINTERSHAPE *A)
{
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(A);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY TgQueryVidPnHWCapability(const HANDLE hAdapter, DXGKARG_QUERYVIDPNHWCAPABILITY *A)
{
    UNREFERENCED_PARAMETER(hAdapter);
    RtlZeroMemory(&A->VidPnHWCaps, sizeof(A->VidPnHWCaps));
    return STATUS_SUCCESS;
}

static NTSTATUS TgStopDeviceAndReleasePostDisplayOwnership(PVOID Ctx, D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                                           PDXGK_DISPLAY_INFORMATION Info)
{
    UNREFERENCED_PARAMETER(Ctx);
    UNREFERENCED_PARAMETER(TargetId);
    DispGetPostInfo(Info);
    DispStop();
    EngStop();
    return STATUS_SUCCESS;
}

static NTSTATUS TgSystemDisplayEnable(PVOID Ctx, D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                      PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags, UINT *Width, UINT *Height,
                                      D3DDDIFORMAT *Format)
{
    DXGK_DISPLAY_INFORMATION i;

    UNREFERENCED_PARAMETER(Ctx);
    UNREFERENCED_PARAMETER(TargetId);
    UNREFERENCED_PARAMETER(Flags);
    DispGetPostInfo(&i);
    *Width = i.Width;
    *Height = i.Height;
    *Format = D3DDDIFMT_X8R8G8B8;
    return STATUS_SUCCESS;
}

static VOID TgSystemDisplayWrite(PVOID Ctx, PVOID Src, UINT W, UINT H, UINT Stride, UINT X, UINT Y)
{
    UNREFERENCED_PARAMETER(Ctx);
    DispSystemWrite(Src, W, H, Stride, X, Y);
}

DRIVER_INITIALIZE DriverEntry;

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    DRIVER_INITIALIZATION_DATA d;
    NTSTATUS st;

    LogOpen();
    LogPrint("==== s23gpu.sys v1.0.2.3 WDDM 2.0 Full Display (1080x2340 @ 120Hz HW + 0.5ms TimerRes + RowDelta Scanout + Preempt/GPU Engine Util + OpenGL 4.6 ICD + Brightness + Night Light) + Adreno 740 KMD ====\n");
    MsmInit();

    RtlZeroMemory(&d, sizeof(d));
    d.Version = DXGKDDI_INTERFACE_VERSION_WDDM2_0;
    d.DxgkDdiAddDevice = TgAddDevice;
    d.DxgkDdiStartDevice = TgStartDevice;
    d.DxgkDdiStopDevice = TgStopDevice;
    d.DxgkDdiRemoveDevice = TgRemoveDevice;
    d.DxgkDdiDispatchIoRequest = TgDispatchIoRequest;
    d.DxgkDdiInterruptRoutine = TgInterruptRoutine;
    d.DxgkDdiDpcRoutine = TgDpcRoutine;
    d.DxgkDdiQueryChildRelations = TgQueryChildRelations;
    d.DxgkDdiQueryChildStatus = TgQueryChildStatus;
    d.DxgkDdiQueryDeviceDescriptor = TgQueryDeviceDescriptor;
    d.DxgkDdiIsSupportedVidPn = TgIsSupportedVidPn;
    d.DxgkDdiRecommendFunctionalVidPn = TgRecommendFunctionalVidPn;
    d.DxgkDdiEnumVidPnCofuncModality = TgEnumVidPnCofuncModality;
    d.DxgkDdiSetVidPnSourceAddress = TgSetVidPnSourceAddress;
    d.DxgkDdiSetVidPnSourceVisibility = TgSetVidPnSourceVisibility;
    d.DxgkDdiCommitVidPn = TgCommitVidPn;
    d.DxgkDdiUpdateActiveVidPnPresentPath = TgUpdateActiveVidPnPresentPath;
    d.DxgkDdiRecommendMonitorModes = TgRecommendMonitorModes;
    d.DxgkDdiRecommendVidPnTopology = TgRecommendVidPnTopology;
    d.DxgkDdiGetScanLine = TgGetScanLine;
    d.DxgkDdiStopCapture = TgStopCapture;
    d.DxgkDdiSetPalette = TgSetPalette;
    d.DxgkDdiSetPointerPosition = TgSetPointerPosition;
    d.DxgkDdiSetPointerShape = TgSetPointerShape;
    d.DxgkDdiQueryVidPnHWCapability = TgQueryVidPnHWCapability;
    d.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = TgStopDeviceAndReleasePostDisplayOwnership;
    d.DxgkDdiSystemDisplayEnable = TgSystemDisplayEnable;
    d.DxgkDdiSystemDisplayWrite = TgSystemDisplayWrite;
    d.DxgkDdiSetPowerState = TgSetPowerState;
    d.DxgkDdiResetDevice = TgResetDevice;
    d.DxgkDdiUnload = TgUnload;
    d.DxgkDdiQueryInterface = TgQueryInterface;
    d.DxgkDdiQueryAdapterInfo = TgQueryAdapterInfo;
    d.DxgkDdiEscape = TgEscape;
    d.DxgkDdiCreateDevice = TgCreateDevice;
    d.DxgkDdiDestroyDevice = TgDestroyDevice;
    d.DxgkDdiCreateContext = TgCreateContext;
    d.DxgkDdiDestroyContext = TgDestroyContext;
    d.DxgkDdiCreateAllocation = TgCreateAllocation;
    d.DxgkDdiDestroyAllocation = TgDestroyAllocation;
    d.DxgkDdiDescribeAllocation = TgDescribeAllocation;
    d.DxgkDdiGetStandardAllocationDriverData = TgGetStandardAllocationDriverData;
    d.DxgkDdiOpenAllocation = TgOpenAllocation;
    d.DxgkDdiCloseAllocation = TgCloseAllocation;
    d.DxgkDdiRender = TgRender;
    d.DxgkDdiPresent = TgPresent;
    d.DxgkDdiPatch = TgPatch;
    d.DxgkDdiSubmitCommand = TgSubmitCommand;
    d.DxgkDdiPreemptCommand = TgPreemptCommand;
    d.DxgkDdiBuildPagingBuffer = TgBuildPagingBuffer;
    d.DxgkDdiResetFromTimeout = TgResetFromTimeout;
    d.DxgkDdiRestartFromTimeout = TgRestartFromTimeout;
    d.DxgkDdiCollectDbgInfo = TgCollectDbgInfo;
    d.DxgkDdiQueryCurrentFence = TgQueryCurrentFence;
    d.DxgkDdiControlInterrupt = TgControlInterrupt;
    d.DxgkDdiGetNodeMetadata = TgGetNodeMetadata;
    d.DxgkDdiCancelCommand = TgCancelCommand;
    d.DxgkDdiQueryEngineStatus = TgQueryEngineStatus;
    d.DxgkDdiResetEngine = TgResetEngine;
    d.DxgkDdiCalibrateGpuClock = TgCalibrateGpuClock;
    d.DxgkDdiSetStablePowerState = TgSetStablePowerState;

    st = DxgkInitialize(DriverObject, RegistryPath, &d);
    LogPrint("DxgkInitialize -> %08x\n", st);
    return st;
}
