/*++

Copyright (c) Microsoft Corporation All Rights Reserved

Module Name:

    hw.h

Abstract:

    Declaration of Simple Audio Sample HW class. 
    Simple Audio Sample HW has an array for storing mixer and volume settings
    for the topology.
--*/

#ifndef _CSAUDIOACP3X_HW_H_
#define _CSAUDIOACP3X_HW_H_

#define USEACPHW 0
#define USESSTHW 1

#if USEACPHW
#include "acp_chip_offset_byte.h"
#include "acp3x.h"
#define BIT(nr) (1UL << (nr))
#endif

#if USESSTHW
#include "bitops.h"

union baseaddr {
    PVOID Base;
    UINT8* baseptr;
};

typedef struct _PCI_BAR {
    union baseaddr Base;
    ULONG Len;
} PCI_BAR, * PPCI_BAR;

typedef struct _RESOURCE {
    UINT32 start;
    UINT32 end;
    struct _RESOURCE* parent, * sibling, * child;
} RESOURCE, *PRESOURCE;

enum PCI_POWER {
    PCI_D0,
    PCI_D1,
    PCI_D3,
    PCI_D3hot,
    PCI_D3cold
};
#define PCI_PM_CTRL		4	/* PM control and status register */
#define  PCI_PM_CTRL_STATE_MASK	0x0003	/* Current power state (D0 to D3) */

#include "messages.h"
#include "firmware.h"
#include "registers.h"
#include "dw_dma.h"

/*
* Either engine 0 or 1 can be used for image loading.
* Align with Intel Windows driver equivalent and stick to engine 1.
*/
#define CATPT_DMA_DEVID		1
#define CATPT_DMA_DSP_ADDR_MASK	GENMASK(31, 20)

struct catpt_ipc_msg {
    union {
        UINT32 header;
        union catpt_global_msg rsp;
    };
    void* data;
    size_t size;
};

struct catpt_module_type {
    bool loaded;
    UINT32 entry_point;
    UINT32 persistent_size;
    UINT32 scratch_size;
    /* DRAM, initial module state */
    UINT32 state_offset;
    UINT32 state_size;
};

struct catpt_spec {
    UINT8 core_id;
    UINT32 host_dram_offset;
    UINT32 host_iram_offset;
    UINT32 host_shim_offset;
    UINT32 host_dma_offset[CATPT_DMA_COUNT];
    UINT32 host_ssp_offset[CATPT_SSP_COUNT];
    UINT32 dram_mask;
    UINT32 iram_mask;
    UINT32 d3srampgd_bit;
    UINT32 d3pgd_bit;
    UINT32 pll_shutdown_reg;
    UINT32 pll_shutdown_val;
};

struct catpt_stream {
    BOOL allocated;
    BOOL prepared;
};
#endif

//=============================================================================
// Defines
//=============================================================================
// BUGBUG we should dynamically allocate this...
#define MAX_TOPOLOGY_NODES      20

//=============================================================================
// Classes
//=============================================================================
///////////////////////////////////////////////////////////////////////////////
// CCsAudioAcp3xHW
// This class represents virtual Simple Audio Sample HW. An array representing volume
// registers and mute registers.

class CCsAudioCatptSSTHW
{
public:
protected:
    LONG                        m_PeakMeterControls[MAX_TOPOLOGY_NODES];
    ULONG                       m_ulMux;            // Mux selection
    BOOL                        m_bDevSpecific;
    INT                         m_iDevSpecific;
    UINT                        m_uiDevSpecific;
#if USESSTHW
    PCI_BAR m_BAR0;
    PCI_BAR m_BAR1;

    PINTERRUPTSYNC m_InterruptSync;
    PWORK_QUEUE_ITEM m_WorkQueueItem;

    RESOURCE dram;
    RESOURCE iram;
    //PRESOURCE scratch;

    DwDMA* dmac;
#endif

#if USEACPHW
    UINT32 m_pme_en;

    INT bt_running_streams;
    INT sp_running_streams;
#endif

private:
#if USESSTHW
    const struct catpt_spec* spec;
    catpt_stream outStream;
    catpt_stream inStream;
    FAST_MUTEX clk_mutex;

    void udelay(ULONG usec);
    UINT32 readl(PVOID reg);
    void writel(UINT32 val, PVOID reg);
    NTSTATUS readl_poll_timeout(PVOID reg, UINT32 val, UINT32 mask, ULONG sleep_us, ULONG timeout_us);

    //DSP Private methods
    NTSTATUS dsp_select_lpclock(BOOL lp, BOOL waiti);
    NTSTATUS dsp_update_lpclock();
    void dsp_set_regs_defaults();
    void dsp_set_srampge(PRESOURCE sram, unsigned long mask, unsigned long newVal);
    void dsp_update_srampge(PRESOURCE sram, unsigned long mask);
    NTSTATUS dsp_stall(BOOL stall);
    NTSTATUS dsp_reset(BOOL reset);

    //DSP methods
    NTSTATUS dsp_power_down();
    NTSTATUS dsp_power_up();

    //loader vars
    struct catpt_module_type modules[CATPT_MODULE_COUNT];
    BOOL fw_ready;

    //loader private methods
    void sram_init(PRESOURCE sram, UINT32 start, UINT32 size);
    NTSTATUS catpt_load_block(PHYSICAL_ADDRESS pAddr, struct catpt_fw_block_hdr* blk, bool alloc);
    NTSTATUS catpt_load_module(PHYSICAL_ADDRESS paddr, struct catpt_fw_mod_hdr* mod);
    NTSTATUS catpt_load_firmware(PHYSICAL_ADDRESS paddr, struct catpt_fw_hdr* fw);
    NTSTATUS catpt_load_image(PCWSTR path, BOOL restore);

    //loader methods
    NTSTATUS catpt_boot_firmware(BOOL restore);

    //IPC vars
    struct catpt_ipc_msg ipc_rx;
    struct catpt_fw_ready ipc_config;
    BOOL ipc_ready;

    BOOL ipc_done;
    BOOL ipc_busy;

    //IPC private methods
    void ipc_init();
    NTSTATUS ipc_arm(struct catpt_fw_ready* config);
    void dsp_copy_rx(UINT32 header);
    void dsp_process_response(UINT32 header);
    //IPC methods
   
public:
    void dsp_irq_thread();
    NTSTATUS dsp_irq_handler();
#endif

public:
    CCsAudioCatptSSTHW(_In_  PRESOURCELIST           ResourceList);
    ~CCsAudioCatptSSTHW();

    bool                        ResourcesValidated();
    NTSTATUS sst_init();
    NTSTATUS sst_deinit();

    NTSTATUS acp3x_hw_params(eDeviceType deviceType);
    NTSTATUS acp3x_program_dma(eDeviceType deviceType, PMDL mdl, IPortWaveRTStream* stream);
    NTSTATUS acp3x_play(eDeviceType deviceType, UINT32 byteCount);
    NTSTATUS acp3x_stop(eDeviceType deviceType);
    NTSTATUS acp3x_current_position(eDeviceType deviceType, UINT32* linkPos, UINT64* linearPos);
    NTSTATUS acp3x_set_position(eDeviceType deviceType, UINT32 linkPos, UINT64 linearPos);
    
    void                        MixerReset();
    BOOL                        bGetDevSpecific();
    void                        bSetDevSpecific
    (
        _In_  BOOL                bDevSpecific
    );
    INT                         iGetDevSpecific();
    void                        iSetDevSpecific
    (
        _In_  INT                 iDevSpecific
    );
    UINT                        uiGetDevSpecific();
    void                        uiSetDevSpecific
    (
        _In_  UINT                uiDevSpecific
    );
    ULONG                       GetMixerMux();
    void                        SetMixerMux
    (
        _In_  ULONG               ulNode
    );
    
    LONG                        GetMixerPeakMeter
    (   
        _In_  ULONG               ulNode,
        _In_  ULONG               ulChannel
    );

protected:
private:
};
typedef CCsAudioCatptSSTHW *PCCsAudioCatptSSTHW;

#endif  // _CSAUDIOACP3X_HW_H_
