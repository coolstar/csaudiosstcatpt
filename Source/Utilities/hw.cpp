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

//=============================================================================
// CCsAudioAcp3xHW
//=============================================================================

//=============================================================================
#pragma code_seg("PAGE")
CCsAudioAcp3xHW::CCsAudioAcp3xHW(_In_  PRESOURCELIST           ResourceList)
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

#if USEACPHW
    PCM_PARTIAL_RESOURCE_DESCRIPTOR partialDescriptor = (ResourceList->FindTranslatedEntry(CmResourceTypeMemory, 0));
    if (partialDescriptor) {
        m_BAR0.Base.Base = MmMapIoSpace(partialDescriptor->u.Memory.Start, partialDescriptor->u.Memory.Length, MmNonCached);
        m_BAR0.Len = partialDescriptor->u.Memory.Length;
    }
    else {
        m_BAR0.Base.Base = NULL;
        m_BAR0.Len = 0;
        return;
    }

    m_pme_en = rv_read32(mmACP_PME_EN);
#else
    UNREFERENCED_PARAMETER(ResourceList);
#endif
    
    MixerReset();
} // CsAudioAcp3xHW
#pragma code_seg()

bool CCsAudioAcp3xHW::ResourcesValidated() {
#if USEACPHW
    if (!m_BAR0.Base.Base)
        return false;
#endif
    return true;
}

CCsAudioAcp3xHW::~CCsAudioAcp3xHW() {
#if USEACPHW
    if (m_BAR0.Base.Base)
        MmUnmapIoSpace(m_BAR0.Base.Base, m_BAR0.Len);
#endif
}

#if USEACPHW
static UINT32 read32(PVOID addr) {
    UINT32 ret = *(UINT32*)addr;
    //DbgPrint("Read from %p: 0x%x\n", addr, ret);
    return ret;
}

static void write32(PVOID addr, UINT32 data) {
    *(UINT32*)addr = data;
    //DbgPrint("Write to %p: 0x%x\n", addr, data);
}

UINT32 CCsAudioAcp3xHW::rv_read32(UINT32 reg)
{
    return read32(m_BAR0.Base.baseptr + reg - ACP3x_PHY_BASE_ADDRESS);
}

void CCsAudioAcp3xHW::rv_write32(UINT32 reg, UINT32 val)
{
    write32(m_BAR0.Base.baseptr + reg - ACP3x_PHY_BASE_ADDRESS, val);
}

NTSTATUS CCsAudioAcp3xHW::acp3x_power_on() {
    UINT32 val;
    int timeout;

    val = rv_read32(mmACP_PGFSM_STATUS);
    if (val == 0)
        return STATUS_SUCCESS;

    if (!((val & ACP_PGFSM_STATUS_MASK) ==
        ACP_POWER_ON_IN_PROGRESS))
        rv_write32(mmACP_PGFSM_CONTROL, ACP_PGFSM_CNTL_POWER_ON_MASK);

    timeout = 0;
    while (++timeout < 500) {
        val = rv_read32(mmACP_PGFSM_STATUS);
        if (!val) {
            /* ACP power On clears PME_EN.
             * Restore the value to its prior state
             */
            rv_write32(mmACP_PME_EN, m_pme_en);
            return STATUS_SUCCESS;
        }

        LARGE_INTEGER Interval;
        Interval.QuadPart = -10;
        KeDelayExecutionThread(KernelMode, false, &Interval); //udelay(1)
    }
    return STATUS_TIMEOUT;
}

NTSTATUS CCsAudioAcp3xHW::acp3x_power_off() {
    UINT32 val;
    int timeout;

    rv_write32(mmACP_PGFSM_CONTROL, ACP_PGFSM_CNTL_POWER_OFF_MASK);
    timeout = 0;
    while (++timeout < 500) {
        val = rv_read32(mmACP_PGFSM_STATUS);
        if ((val & ACP_PGFSM_STATUS_MASK) == ACP_POWERED_OFF)
            return STATUS_SUCCESS;

        LARGE_INTEGER Interval;
        Interval.QuadPart = -10;
        KeDelayExecutionThread(KernelMode, false, &Interval); //udelay(1)
    }
    return STATUS_TIMEOUT;
}

NTSTATUS CCsAudioAcp3xHW::acp3x_reset() {
    UINT32 val;
    int timeout;
    rv_write32(mmACP_SOFT_RESET, 1);

    timeout = 0;
    while (++timeout < 500) {
        val = rv_read32(mmACP_SOFT_RESET);
        if (val & ACP3x_SOFT_RESET__SoftResetAudDone_MASK)
            break;
    }
    rv_write32(mmACP_SOFT_RESET, 0);
    timeout = 0;
    while (++timeout < 500) {
        val = rv_read32(mmACP_SOFT_RESET);
        if (!val)
            return STATUS_SUCCESS;
    }
    return STATUS_TIMEOUT;
}
#endif

NTSTATUS CCsAudioAcp3xHW::acp3x_init() {
#if USEACPHW
    bt_running_streams = 0;
    sp_running_streams = 0;

    NTSTATUS status = acp3x_power_on();
    if (!NT_SUCCESS(status)) {
        return status;
    }
    status = acp3x_reset();
    return status;
#else
    return STATUS_SUCCESS;
#endif
}

NTSTATUS CCsAudioAcp3xHW::acp3x_deinit() {
#if USEACPHW
    bt_running_streams = 0;
    sp_running_streams = 0;

    NTSTATUS status = acp3x_reset();
    if (!NT_SUCCESS(status)) {
        return status;
    }
    status = acp3x_power_off();
    return status;
#else
    return STATUS_SUCCESS;
#endif
}

NTSTATUS CCsAudioAcp3xHW::acp3x_hw_params(eDeviceType deviceType) {
#if USEACPHW
    UINT32 xfer_resolution = 0x02; //s16le

    UINT32 reg_val;
    switch (deviceType) {
    case eSpeakerDevice:
        reg_val = mmACP_BTTDM_ITER;
        break;
    case eHeadphoneDevice:
        reg_val = mmACP_I2STDM_ITER;
        break;
    case eMicArrayDevice1:
        reg_val = mmACP_BTTDM_IRER;
        break;
    case eMicJackDevice:
        reg_val = mmACP_I2STDM_IRER;
        break;
    default:
        DPF(D_ERROR, "Unknown device type");
        return STATUS_INVALID_PARAMETER;
    }

    UINT32 val = rv_read32(reg_val);
    val &= ACP3x_ITER_IRER_SAMP_LEN_MASK;
    val = val | (xfer_resolution << 3);
    rv_write32(reg_val, val);
#else
    UNREFERENCED_PARAMETER(deviceType);
#endif
    return STATUS_SUCCESS;
}

NTSTATUS CCsAudioAcp3xHW::acp3x_program_dma(eDeviceType deviceType, PMDL mdl, IPortWaveRTStream *stream) {
#if USEACPHW
    int pageCount = stream->GetPhysicalPagesCount(mdl);
    if (pageCount < 1) {
        return STATUS_NO_MEMORY;
    }

    UINT32 val;
    UINT32 reg_dma_size;
    UINT32 acp_fifo_addr;
    UINT32 reg_fifo_addr;
    UINT32 reg_fifo_size;

    UINT32 ring_buf_addr;
    UINT32 window_start_addr;

    switch (deviceType) {
    case eSpeakerDevice:
        val = ACP_SRAM_BT_PB_PTE_OFFSET;
        reg_dma_size = mmACP_BT_TX_DMA_SIZE;
        acp_fifo_addr = ACP_SRAM_PTE_OFFSET +
            BT_PB_FIFO_ADDR_OFFSET;
        reg_fifo_addr = mmACP_BT_TX_FIFOADDR;
        reg_fifo_size = mmACP_BT_TX_FIFOSIZE;

        ring_buf_addr = mmACP_BT_TX_RINGBUFADDR;
        window_start_addr = I2S_BT_TX_MEM_WINDOW_START;
        break;
    case eHeadphoneDevice:
        val = ACP_SRAM_SP_PB_PTE_OFFSET;
        reg_dma_size = mmACP_I2S_TX_DMA_SIZE;
        acp_fifo_addr = ACP_SRAM_PTE_OFFSET +
            SP_PB_FIFO_ADDR_OFFSET;
        reg_fifo_addr = mmACP_I2S_TX_FIFOADDR;
        reg_fifo_size = mmACP_I2S_TX_FIFOSIZE;

        ring_buf_addr = mmACP_I2S_TX_RINGBUFADDR;
        window_start_addr = I2S_SP_TX_MEM_WINDOW_START;
        break;
    case eMicArrayDevice1:
        val = ACP_SRAM_BT_CP_PTE_OFFSET;
        reg_dma_size = mmACP_BT_RX_DMA_SIZE;
        acp_fifo_addr = ACP_SRAM_PTE_OFFSET +
            BT_CAPT_FIFO_ADDR_OFFSET;
        reg_fifo_addr = mmACP_BT_RX_FIFOADDR;
        reg_fifo_size = mmACP_BT_RX_FIFOSIZE;

        ring_buf_addr = mmACP_BT_RX_RINGBUFADDR;
        window_start_addr = I2S_BT_RX_MEM_WINDOW_START;
        break;
    case eMicJackDevice:
        val = ACP_SRAM_SP_CP_PTE_OFFSET;
        reg_dma_size = mmACP_I2S_RX_DMA_SIZE;
        acp_fifo_addr = ACP_SRAM_PTE_OFFSET +
            SP_CAPT_FIFO_ADDR_OFFSET;
        reg_fifo_addr = mmACP_I2S_RX_FIFOADDR;
        reg_fifo_size = mmACP_I2S_RX_FIFOSIZE;

        ring_buf_addr = mmACP_I2S_RX_RINGBUFADDR;
        window_start_addr = I2S_SP_RX_MEM_WINDOW_START;
        break;
    default:
        DPF(D_ERROR, "Unknown device type");
        return STATUS_INVALID_PARAMETER;
    }

    /* Group Enable */
    rv_write32(mmACPAXI2AXI_ATU_BASE_ADDR_GRP_1, ACP_SRAM_PTE_OFFSET | BIT(31));
    rv_write32(mmACPAXI2AXI_ATU_PAGE_SIZE_GRP_1, PAGE_SIZE_4K_ENABLE);

    for (int i = 0; i < pageCount; i++) {
        PHYSICAL_ADDRESS address = stream->GetPhysicalPageAddress(mdl, i);
        UINT32 low = address.LowPart;
        UINT32 high = address.HighPart;

        rv_write32(mmACP_SCRATCH_REG_0 + val, low);
        high |= BIT(31);
        rv_write32(mmACP_SCRATCH_REG_0 + val + 4, high);

        val += 8;
    }

    rv_write32(ring_buf_addr, window_start_addr);
    rv_write32(reg_dma_size, DMA_SIZE);
    rv_write32(reg_fifo_addr, acp_fifo_addr);
    rv_write32(reg_fifo_size, FIFO_SIZE);
    rv_write32(mmACP_EXTERNAL_INTR_CNTL, BIT(I2S_RX_THRESHOLD) | BIT(BT_RX_THRESHOLD)
        | BIT(I2S_TX_THRESHOLD) | BIT(BT_TX_THRESHOLD));
#else
    UNREFERENCED_PARAMETER(deviceType);
    UNREFERENCED_PARAMETER(stream);
    UNREFERENCED_PARAMETER(mdl);
#endif
    return STATUS_SUCCESS;
}

NTSTATUS CCsAudioAcp3xHW::acp3x_play(eDeviceType deviceType, UINT32 byteCount) {
#if USEACPHW
    UINT32 water_val;
    UINT32 reg_val;
    UINT32 ier_val;
    UINT32 buf_reg;

    switch (deviceType) {
    case eSpeakerDevice:
        water_val =
            mmACP_BT_TX_INTR_WATERMARK_SIZE;
        reg_val = mmACP_BTTDM_ITER;
        ier_val = mmACP_BTTDM_IER;
        buf_reg = mmACP_BT_TX_RINGBUFSIZE;

        bt_running_streams++;
        break;
    case eHeadphoneDevice:
        water_val =
            mmACP_I2S_TX_INTR_WATERMARK_SIZE;
        reg_val = mmACP_I2STDM_ITER;
        ier_val = mmACP_I2STDM_IER;
        buf_reg = mmACP_I2S_TX_RINGBUFSIZE;

        sp_running_streams++;
        break;
    case eMicArrayDevice1:
        water_val =
            mmACP_BT_RX_INTR_WATERMARK_SIZE;
        reg_val = mmACP_BTTDM_IRER;
        ier_val = mmACP_BTTDM_IER;
        buf_reg = mmACP_BT_RX_RINGBUFSIZE;

        bt_running_streams++;
        break;
    case eMicJackDevice:
        water_val =
            mmACP_I2S_RX_INTR_WATERMARK_SIZE;
        reg_val = mmACP_I2STDM_IRER;
        ier_val = mmACP_I2STDM_IER;
        buf_reg = mmACP_I2S_RX_RINGBUFSIZE;

        sp_running_streams++;
        break;
    default:
        DPF(D_ERROR, "Unknown device type");
        return STATUS_INVALID_PARAMETER;
    }

    rv_write32(water_val, 160);
    rv_write32(buf_reg, byteCount);

    UINT32 val = rv_read32(reg_val);
    val = val | BIT(0);
    rv_write32(reg_val, val);
    rv_write32(ier_val, 1);

    rv_write32(mmACP_EXTERNAL_INTR_ENB, 1); //Enable interrupts
#else
    UNREFERENCED_PARAMETER(deviceType);
    UNREFERENCED_PARAMETER(byteCount);
#endif
return STATUS_SUCCESS;
}

NTSTATUS CCsAudioAcp3xHW::acp3x_stop(eDeviceType deviceType) {
#if USEACPHW
    UINT32 reg_val;
    UINT32 ier_val;

    INT running_streams = 0;

    switch (deviceType) {
    case eSpeakerDevice:
        reg_val = mmACP_BTTDM_ITER;
        ier_val = mmACP_BTTDM_IER;

        bt_running_streams++;
        running_streams = bt_running_streams;
        break;
    case eHeadphoneDevice:
        reg_val = mmACP_I2STDM_ITER;
        ier_val = mmACP_I2STDM_IER;

        sp_running_streams++;
        running_streams = sp_running_streams;
        break;
    case eMicArrayDevice1:
        reg_val = mmACP_BTTDM_IRER;
        ier_val = mmACP_BTTDM_IER;

        bt_running_streams++;
        running_streams = bt_running_streams;
        break;
    case eMicJackDevice:
        reg_val = mmACP_I2STDM_IRER;
        ier_val = mmACP_I2STDM_IER;

        sp_running_streams++;
        running_streams = sp_running_streams;
        break;
    default:
        DPF(D_ERROR, "Unknown device type");
        return STATUS_INVALID_PARAMETER;
    }

    UINT32 val = rv_read32(reg_val);
    val = val & ~BIT(0);
    rv_write32(reg_val, val);
    if (running_streams < 1)
        rv_write32(ier_val, 0);
#else
    UNREFERENCED_PARAMETER(deviceType);
#endif
    return STATUS_SUCCESS;
}

NTSTATUS CCsAudioAcp3xHW::acp3x_current_position(eDeviceType deviceType, UINT32 *linkPos, UINT64 *linearPos) {
#if USEACPHW
    UINT32 link_reg;
    UINT32 linearHigh_reg;
    UINT32 linearLow_reg;

    switch (deviceType) {
    case eSpeakerDevice:
        link_reg = mmACP_BT_TX_LINKPOSITIONCNTR;
        linearHigh_reg = mmACP_BT_TX_LINEARPOSITIONCNTR_HIGH;
        linearLow_reg = mmACP_BT_TX_LINEARPOSITIONCNTR_LOW;
        break;
    case eHeadphoneDevice:
        link_reg = mmACP_I2S_TX_LINKPOSITIONCNTR;
        linearHigh_reg = mmACP_I2S_TX_LINEARPOSITIONCNTR_HIGH;
        linearLow_reg = mmACP_I2S_TX_LINEARPOSITIONCNTR_LOW;
        break;
    case eMicArrayDevice1:
        link_reg = mmACP_BT_RX_LINKPOSITIONCNTR;
        linearHigh_reg = mmACP_BT_RX_LINEARPOSITIONCNTR_HIGH;
        linearLow_reg = mmACP_BT_RX_LINEARPOSITIONCNTR_LOW;
        break;
    case eMicJackDevice:
        link_reg = mmACP_I2S_RX_LINKPOSITIONCNTR;
        linearHigh_reg = mmACP_I2S_RX_LINEARPOSITIONCNTR_HIGH;
        linearLow_reg = mmACP_I2S_RX_LINEARPOSITIONCNTR_LOW;
        break;
    default:
        DPF(D_ERROR, "Unknown device type");
        return STATUS_INVALID_PARAMETER;
    }

    UINT32 linkCtr = rv_read32(link_reg);
    UINT32 linearHigh = rv_read32(linearHigh_reg);
    UINT32 linearLow = rv_read32(linearLow_reg);

    if (linkPos)
        *linkPos = linkCtr;
    if (linearPos)
        *linearPos = ((UINT64)linearHigh << 32) | (UINT64)linearLow;
#else
    UNREFERENCED_PARAMETER(deviceType);
    UNREFERENCED_PARAMETER(linkPos);
    UNREFERENCED_PARAMETER(linearPos);
#endif
    return STATUS_SUCCESS;
}

NTSTATUS CCsAudioAcp3xHW::acp3x_set_position(eDeviceType deviceType, UINT32 linkPos, UINT64 linearPos) {
#if USEACPHW
    UINT32 link_reg;
    UINT32 linearHigh_reg;
    UINT32 linearLow_reg;

    switch (deviceType) {
    case eSpeakerDevice:
        link_reg = mmACP_BT_TX_LINKPOSITIONCNTR;
        linearHigh_reg = mmACP_BT_TX_LINEARPOSITIONCNTR_HIGH;
        linearLow_reg = mmACP_BT_TX_LINEARPOSITIONCNTR_LOW;
        break;
    case eHeadphoneDevice:
        link_reg = mmACP_I2S_TX_LINKPOSITIONCNTR;
        linearHigh_reg = mmACP_I2S_TX_LINEARPOSITIONCNTR_HIGH;
        linearLow_reg = mmACP_I2S_TX_LINEARPOSITIONCNTR_LOW;
        break;
    case eMicArrayDevice1:
        link_reg = mmACP_BT_RX_LINKPOSITIONCNTR;
        linearHigh_reg = mmACP_BT_RX_LINEARPOSITIONCNTR_HIGH;
        linearLow_reg = mmACP_BT_RX_LINEARPOSITIONCNTR_LOW;
        break;
    case eMicJackDevice:
        link_reg = mmACP_I2S_RX_LINKPOSITIONCNTR;
        linearHigh_reg = mmACP_I2S_RX_LINEARPOSITIONCNTR_HIGH;
        linearLow_reg = mmACP_I2S_RX_LINEARPOSITIONCNTR_LOW;
        break;
    default:
        DPF(D_ERROR, "Unknown device type");
        return STATUS_INVALID_PARAMETER;
    }

    rv_write32(link_reg, linkPos);
    rv_write32(linearHigh_reg, linearPos >> 32);
    rv_write32(linearLow_reg, linearPos & 0xffffffff);
#else
    UNREFERENCED_PARAMETER(deviceType);
    UNREFERENCED_PARAMETER(linkPos);
    UNREFERENCED_PARAMETER(linearPos);
#endif
    return STATUS_SUCCESS;
}

//=============================================================================
BOOL
CCsAudioAcp3xHW::bGetDevSpecific()
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
CCsAudioAcp3xHW::bSetDevSpecific
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
CCsAudioAcp3xHW::iGetDevSpecific()
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
CCsAudioAcp3xHW::iSetDevSpecific
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
CCsAudioAcp3xHW::uiGetDevSpecific()
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
CCsAudioAcp3xHW::uiSetDevSpecific
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
CCsAudioAcp3xHW::GetMixerMux()
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
CCsAudioAcp3xHW::GetMixerPeakMeter
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
CCsAudioAcp3xHW::MixerReset()
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
CCsAudioAcp3xHW::SetMixerMux
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
