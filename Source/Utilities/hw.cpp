/*++

Copyright (c)  Microsoft Corporation All Rights Reserved

Module Name:

    hw.cpp

Abstract:

    Implementation of Simple Audio Sample HW class. 
    Simple Audio Sample HW has an array for storing mixer and volume settings
    for the topology.
--*/
#include "definitions.h"
#include "hw.h"
#include "resource.h"

static NTSTATUS InterruptRoutine(PINTERRUPTSYNC InterruptSync,
    PVOID DynamicContext) {
    UNREFERENCED_PARAMETER(InterruptSync);
    CCsAudioCatptSSTHW* that = (CCsAudioCatptSSTHW*)DynamicContext;
    return that->dsp_irq_handler();
}

#if USESSTHW
static struct catpt_spec wpt_desc = {
    .core_id = 0x02,
    .host_dram_offset = 0x000000,
    .host_iram_offset = 0x0A0000,
    .host_shim_offset = 0x0FB000,
    .host_dma_offset = { 0x0FE000, 0x0FF000 },
    .host_ssp_offset = { 0x0FC000, 0x0FD000 },
    .dram_mask = WPT_VDRTCTL0_DSRAMPGE_MASK,
    .iram_mask = WPT_VDRTCTL0_ISRAMPGE_MASK,
    .d3srampgd_bit = WPT_VDRTCTL0_D3SRAMPGD,
    .d3pgd_bit = WPT_VDRTCTL0_D3PGD,
    .pll_shutdown_reg = CATPT_PCI_VDRTCTL2,
    .pll_shutdown_val = WPT_VDRTCTL2_APLLSE
};
#endif

//=============================================================================
// CCsAudioCatptSSTHW
//=============================================================================

//=============================================================================
#pragma code_seg("PAGE")
CCsAudioCatptSSTHW::CCsAudioCatptSSTHW(_In_  PRESOURCELIST           ResourceList)
: m_ulMux(0),
    m_bDevSpecific(FALSE),
    m_iDevSpecific(0),
    m_uiDevSpecific(0)
/*++

Routine Description:

    Constructor for CsAudioAcp3xHW. 

Arguments:

Return Value:

    void

--*/
{
    PAGED_CODE();

#if USESSTHW
    spec = &wpt_desc;

    PCM_PARTIAL_RESOURCE_DESCRIPTOR partialDescriptor = ResourceList->FindTranslatedEntry(CmResourceTypeMemory, 0);
    if (partialDescriptor) {
        m_BAR0.Base.Base = MmMapIoSpace(partialDescriptor->u.Memory.Start, partialDescriptor->u.Memory.Length, MmNonCached);
        m_BAR0.Len = partialDescriptor->u.Memory.Length;
    }
    else {
        m_BAR0.Base.Base = NULL;
        m_BAR0.Len = 0;
        return;
    }

    partialDescriptor = ResourceList->FindTranslatedEntry(CmResourceTypeMemory, 1);
    if (partialDescriptor) {
        m_BAR1.Base.Base = MmMapIoSpace(partialDescriptor->u.Memory.Start, partialDescriptor->u.Memory.Length, MmNonCached);
        m_BAR1.Len = partialDescriptor->u.Memory.Length;
}
    else {
        m_BAR1.Base.Base = NULL;
        m_BAR1.Len = 0;
        return;
    }

    partialDescriptor = ResourceList->FindTranslatedEntry(CmResourceTypeInterrupt, 0);
    if (partialDescriptor) {
        NTSTATUS status = PcNewInterruptSync(&this->m_InterruptSync, NULL,
            ResourceList, 0, InterruptSyncModeNormal);
        if (!NT_SUCCESS(status)){
            this->m_InterruptSync = NULL;
            return;
        }
    }
    else {
        this->m_InterruptSync = NULL;
        return;
    }

    this->fw_ready = false;
    ExInitializeFastMutex(&clk_mutex);

    ipc_init();

    sram_init(&this->dram, this->spec->host_dram_offset,
        catpt_dram_size(this));
    sram_init(&this->iram, this->spec->host_iram_offset,
        catpt_iram_size(this));
#else
    UNREFERENCED_PARAMETER(ResourceList);
#endif
    
    MixerReset();
} // CCsAudioCatptSSTHW
#pragma code_seg()

bool CCsAudioCatptSSTHW::ResourcesValidated() {
#if USESSTHW
    if (!m_BAR0.Base.Base)
        return false;
    if (!m_BAR1.Base.Base)
        return false;
    if (!this->m_InterruptSync)
        return false;
    NTSTATUS status = this->m_InterruptSync->RegisterServiceRoutine(InterruptRoutine, (PVOID)this, FALSE);
    if (!NT_SUCCESS(status)) {
        return false;
    }
    status = this->m_InterruptSync->Connect();
    if (!NT_SUCCESS(status)) {
        return false;
    }
#endif
    return true;
}

CCsAudioCatptSSTHW::~CCsAudioCatptSSTHW() {
#if USESSTHW
    if (this->ipc_rx.data){
        ExFreePoolWithTag(this->ipc_rx.data, CSAUDIOCATPTSST_POOLTAG);
        this->ipc_rx.data = NULL;
    }

    force_stop(&this->outStream);
    force_stop(&this->inStream);

    if (this->m_InterruptSync) {
        this->m_InterruptSync->Disconnect();
        this->m_InterruptSync->Release();
        this->m_InterruptSync = NULL;
    }

    if (m_BAR0.Base.Base)
        MmUnmapIoSpace(m_BAR0.Base.Base, m_BAR0.Len);
    if (m_BAR1.Base.Base)
        MmUnmapIoSpace(m_BAR1.Base.Base, m_BAR1.Len);
#endif
}

#if USESSTHW
void CCsAudioCatptSSTHW::udelay(ULONG usec) {
    LARGE_INTEGER Interval;
    Interval.QuadPart = -10 * usec;
    KeDelayExecutionThread(KernelMode, false, &Interval);
}

UINT32 CCsAudioCatptSSTHW::readl(PVOID addr) {
    UINT32 ret = *(UINT32*)addr;
    //DbgPrint("Read from %p: 0x%x\n", addr, ret);
    return ret;
}

void CCsAudioCatptSSTHW::writel(UINT32 data, PVOID addr) {
    *(UINT32*)addr = data;
    //DbgPrint("Write to %p: 0x%x\n", addr, data);
}

NTSTATUS CCsAudioCatptSSTHW::readl_poll_timeout(PVOID addr, UINT32 val, UINT32 mask, ULONG sleep_us, ULONG timeout_us) {
    UINT32 reg;
    LARGE_INTEGER StartTime;
    KeQuerySystemTimePrecise(&StartTime);
    for (;;) {
        reg = readl(addr);
        LARGE_INTEGER CurrentTime;
        KeQuerySystemTimePrecise(&CurrentTime);
        if ((reg & mask) == val || ((CurrentTime.QuadPart - StartTime.QuadPart) / 10) > timeout_us)
            break; \
            if (sleep_us)\
                udelay(sleep_us); \
    }
    return (reg & mask) == val ? STATUS_SUCCESS : STATUS_IO_TIMEOUT;
}
#endif

NTSTATUS CCsAudioCatptSSTHW::sst_init() {
#if USESSTHW
    NTSTATUS status = dsp_power_up();
    if (!NT_SUCCESS(status)) {
        return status;
    }

    this->dmac = new (POOL_FLAG_NON_PAGED, CSAUDIOCATPTSST_POOLTAG)DwDMA(this->lpe_ba + this->spec->host_dma_offset[CATPT_DMA_DEVID]);
    status = this->dmac->init();
    if (!NT_SUCCESS(status)) {
        return status;
    }

    {
        status = catpt_boot_firmware(FALSE);
        if (!NT_SUCCESS(status)) {
            return status;
        }

        /* restrict FW Core dump area */
        __request_region(&this->dram, 0, 0x200, 0);
        /* restrict entire area following BASE_FW - highest offset in DRAM */
        PRESOURCE res;
        for (res = this->dram.child; res->sibling; res = res->sibling)
            ;
        __request_region(&this->dram, res->end + 1,
            this->dram.end - res->end, 0);

        {
            //get mixer info
            union catpt_global_msg msg = CATPT_GLOBAL_MSG(GET_MIXER_STREAM_INFO);
            struct catpt_ipc_msg request = { {0} }, reply;

            request.header = msg.val;
            reply.size = sizeof(this->mixer);
            reply.data = &this->mixer;

            status = ipc_send_msg(request, &reply, CATPT_IPC_TIMEOUT_MS);
            if (!NT_SUCCESS(status)) {
                DPF(D_ERROR, "Failed to get mixer info!\n");
                return status;
            }
        }

        status = catpt_arm_stream_templates();
        if (!NT_SUCCESS(status)) {
            DPF(D_ERROR, "arm templates failed\n");
            return status;
        }

        /* update dram pg for scratch and restricted regions */
        dsp_update_srampge(&this->dram, this->spec->dram_mask);

        {
            //Set device fmt
            struct catpt_ssp_device_format devfmt;
            devfmt.channels = 2;
            devfmt.iface = CATPT_SSP_IFACE_0;
            devfmt.mclk = CATPT_MCLK_FREQ_24_MHZ;
            devfmt.mode = CATPT_SSP_MODE_I2S_PROVIDER;
            devfmt.clock_divider = 9;
            status = ipc_set_device_format(&devfmt);
            if (!NT_SUCCESS(status)) {
                DPF(D_ERROR, "set device fmt failed\n");
                return status;
            }
        }

        {
            //Set mixer volume
            LONG volMax[CATPT_CHANNELS_MAX] = {0x1e, 0x1e, 0x1e, 0x1e};
            status = set_dsp_vol((UINT8)this->mixer.mixer_hw_id, volMax);
            if (!NT_SUCCESS(status)) {
                DPF(D_ERROR, "set mixer vol failed\n");
                return status;
            }
        }

        {
            //Check if streams need to be resumed
            if (this->outStream.allocated) {
                this->outStream.allocated = false;
                CatPtPrint(DEBUG_LEVEL_VERBOSE, DBG_PNP, "Reprogramming stream %d\n", eSpeakerDevice);
                sst_program_dma(eSpeakerDevice, this->outStream.byteCount, this->outStream.pMDL, this->outStream.waveRtStream);
                sst_play(eSpeakerDevice);
            }

            if (this->inStream.allocated) {
                this->inStream.allocated = false;
                CatPtPrint(DEBUG_LEVEL_VERBOSE, DBG_PNP, "Reprogramming stream %d\n", eMicJackDevice);
                sst_program_dma(eMicJackDevice, this->inStream.byteCount, this->inStream.pMDL, this->inStream.waveRtStream);
                sst_play(eMicJackDevice);
            }
        }
    }

    return status;
#else
    

    return STATUS_SUCCESS;
#endif
}

NTSTATUS CCsAudioCatptSSTHW::sst_deinit() {
#if USESSTHW
    if (this->dmac) {
        delete this->dmac;
        this->dmac = NULL;
    }

    NTSTATUS status = dsp_power_down();
    if (!NT_SUCCESS(status)) {
        return status;
    }

    this->outStream.persistent = NULL;
    this->inStream.persistent = NULL;

    sram_free(&this->iram);
    sram_free(&this->dram);
    return status;
#else
    return STATUS_SUCCESS;
#endif
}

NTSTATUS CCsAudioCatptSSTHW::sst_play(eDeviceType deviceType) {
#if USESSTHW
    UINT8 stream_id;
    NTSTATUS status;

    CatPtPrint(DEBUG_LEVEL_INFO, DBG_IOCTL, "Playing stream %d\n", deviceType);

    catpt_stream* stream;

    switch (deviceType) {
    case eSpeakerDevice:
        stream = &this->outStream;
        break;
    case eMicJackDevice:
        stream = &this->inStream;
        break;
    default:
        DPF(D_ERROR, "Unknown device type");
        return STATUS_INVALID_PARAMETER;
    }

    if (!stream->allocated) {
        return STATUS_INVALID_PARAMETER;
    }
    stream_id = (UINT8)stream->info.stream_hw_id;

    status = ipc_reset_stream(stream_id);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    status = ipc_pause_stream(stream_id);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    stream->prepared = true;
    dsp_update_lpclock();

    status = ipc_resume_stream(stream_id);
    return status;

#else
    UNREFERENCED_PARAMETER(deviceType);
    return STATUS_SUCCESS;
#endif
}

NTSTATUS CCsAudioCatptSSTHW::sst_stop(eDeviceType deviceType) {
#if USESSTHW
    NTSTATUS status;

    catpt_stream* stream;

    CatPtPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "Stopping stream %d\n", deviceType);

    switch (deviceType) {
    case eSpeakerDevice:
        stream = &this->outStream;
        break;
    case eMicJackDevice:
        stream = &this->inStream;
        break;
    default:
        DPF(D_ERROR, "Unknown device type");
        return STATUS_INVALID_PARAMETER;
    }

    if (!stream->allocated) {
        force_stop(stream);
        
        dsp_update_lpclock();

        return STATUS_SUCCESS;
    }

    UINT8 stream_id = (UINT8)stream->info.stream_hw_id;
    status = ipc_pause_stream(stream_id);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    stream->prepared = false;
    dsp_update_lpclock();

    status = ipc_free_stream(stream_id);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    force_stop(stream);
    return status;
    
#else
    UNREFERENCED_PARAMETER(deviceType);
    return STATUS_SUCCESS;
#endif
}

void CCsAudioCatptSSTHW::force_stop(catpt_stream* stream) {
    if (stream->pageTable) {
        MmFreeContiguousMemory(stream->pageTable);
        stream->pageTable = NULL;
    }
    stream->allocated = false;

    if (stream->persistent) {
        release_resource(stream->persistent);
        stream->persistent = NULL;
    }

    stream->prepared = false;

    dsp_update_srampge(&this->dram, this->spec->dram_mask);
}

NTSTATUS CCsAudioCatptSSTHW::sst_current_position(eDeviceType deviceType, UINT32 *linkPos, UINT64 *linearPos) {
#if USESSTHW
    UINT32 regaddr;
    catpt_stream* stream;

    switch (deviceType) {
    case eSpeakerDevice:
        stream = &this->outStream;
        break;
    case eMicJackDevice:
        stream = &this->inStream;
        break;
    default:
        DPF(D_ERROR, "Unknown device type");
        return STATUS_INVALID_PARAMETER;
    }

    if (!stream->allocated) {
        return STATUS_INVALID_PARAMETER;
    }

    regaddr = stream->info.read_pos_regaddr;

    UINT32 pos;
    memcpy(&pos, this->lpe_ba + regaddr, sizeof(pos));

    if (linkPos)
        *linkPos = pos;
    if (linearPos)
        *linearPos = pos;
#else
    UNREFERENCED_PARAMETER(deviceType);
    UNREFERENCED_PARAMETER(linkPos);
    UNREFERENCED_PARAMETER(linearPos);
#endif
    return STATUS_SUCCESS;
}

//=============================================================================
BOOL
CCsAudioCatptSSTHW::bGetDevSpecific()
/*++

Routine Description:

  Gets the HW (!) Device Specific info

Arguments:

  N/A

Return Value:

  True or False (in this example).

--*/
{
    return m_bDevSpecific;
} // bGetDevSpecific

//=============================================================================
void
CCsAudioCatptSSTHW::bSetDevSpecific
(
    _In_  BOOL                bDevSpecific
)
/*++

Routine Description:

  Sets the HW (!) Device Specific info

Arguments:

  fDevSpecific - true or false for this example.

Return Value:

    void

--*/
{
    m_bDevSpecific = bDevSpecific;
} // bSetDevSpecific

//=============================================================================
INT
CCsAudioCatptSSTHW::iGetDevSpecific()
/*++

Routine Description:

  Gets the HW (!) Device Specific info

Arguments:

  N/A

Return Value:

  int (in this example).

--*/
{
    return m_iDevSpecific;
} // iGetDevSpecific

//=============================================================================
void
CCsAudioCatptSSTHW::iSetDevSpecific
(
    _In_  INT                 iDevSpecific
)
/*++

Routine Description:

  Sets the HW (!) Device Specific info

Arguments:

  fDevSpecific - true or false for this example.

Return Value:

    void

--*/
{
    m_iDevSpecific = iDevSpecific;
} // iSetDevSpecific

//=============================================================================
UINT
CCsAudioCatptSSTHW::uiGetDevSpecific()
/*++

Routine Description:

  Gets the HW (!) Device Specific info

Arguments:

  N/A

Return Value:

  UINT (in this example).

--*/
{
    return m_uiDevSpecific;
} // uiGetDevSpecific

//=============================================================================
void
CCsAudioCatptSSTHW::uiSetDevSpecific
(
    _In_  UINT                uiDevSpecific
)
/*++

Routine Description:

  Sets the HW (!) Device Specific info

Arguments:

  uiDevSpecific - int for this example.

Return Value:

    void

--*/
{
    m_uiDevSpecific = uiDevSpecific;
} // uiSetDevSpecific

//=============================================================================
ULONG                       
CCsAudioCatptSSTHW::GetMixerMux()
/*++

Routine Description:

  Return the current mux selection

Arguments:

Return Value:

  ULONG

--*/
{
    return m_ulMux;
} // GetMixerMux

//=============================================================================
LONG
CCsAudioCatptSSTHW::GetMixerPeakMeter
(   
    _In_  ULONG                   ulNode,
    _In_  ULONG                   ulChannel
)
/*++

Routine Description:

  Gets the HW (!) peak meter for Simple Audio Sample.

Arguments:

  ulNode - topology node id

  ulChannel - which channel are we reading?

Return Value:

  LONG - sample peak meter level

--*/
{
    UNREFERENCED_PARAMETER(ulChannel);

    if (ulNode < MAX_TOPOLOGY_NODES)
    {
        return m_PeakMeterControls[ulNode];
    }

    return 0;
} // GetMixerVolume

//=============================================================================
#pragma code_seg("PAGE")
void 
CCsAudioCatptSSTHW::MixerReset()
/*++

Routine Description:

  Resets the mixer registers.

Arguments:

Return Value:

    void

--*/
{
    PAGED_CODE();

    for (ULONG i=0; i<MAX_TOPOLOGY_NODES; ++i)
    {
        m_PeakMeterControls[i] = PEAKMETER_SIGNED_MAXIMUM/2;
    }
    
    // BUGBUG change this depending on the topology
    m_ulMux = 2;
} // MixerReset
#pragma code_seg()

//=============================================================================
void                        
CCsAudioCatptSSTHW::SetMixerMux
(
    _In_  ULONG                   ulNode
)
/*++

Routine Description:

  Sets the HW (!) mux selection

Arguments:

  ulNode - topology node id

Return Value:

    void

--*/
{
    m_ulMux = ulNode;
} // SetMixMux
