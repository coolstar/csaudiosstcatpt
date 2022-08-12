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
#define USESSTHW 1

//
// Helper macros
//

#define DEBUG_LEVEL_ERROR   1
#define DEBUG_LEVEL_INFO    2
#define DEBUG_LEVEL_VERBOSE 3

#define CatptDebugLevel 100;
#define CatptDebugCatagories (DBG_INIT || DBG_PNP || DBG_IOCTL)

#define DBG_INIT  1
#define DBG_PNP   2
#define DBG_IOCTL 4

#if 0
#define CatPtPrint(dbglevel, dbgcatagory, fmt, ...) {          \
    if (CatptDebugLevel >= dbglevel &&                         \
        (CatptDebugCatagories && dbgcatagory))                 \
		    {                                                           \
        DbgPrint(DRIVERNAME);                                   \
        DbgPrint(fmt, __VA_ARGS__);                             \
		    }                                                           \
}
#else
#define CatPtPrint(dbglevel, fmt, ...) {                       \
}
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
    UINT64 start;
    UINT64 end;
    int flags;
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

#define CATPT_IPC_TIMEOUT_MS	300

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
    struct catpt_stream_template* templ;
    struct catpt_stream_info info;
    PRESOURCE persistent;

    PVOID pageTable;

    UINT32 byteCount;
    PMDL pMDL;
    IPortWaveRTStream* waveRtStream;

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

    RESOURCE dram;
    RESOURCE iram;
    PRESOURCE scratch;

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

    struct catpt_mixer_stream_info mixer;

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
    void sram_free(PRESOURCE sram);
    PRESOURCE catpt_request_region(PRESOURCE root, size_t size);

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
    NTSTATUS ipc_send_msg(struct catpt_ipc_msg request,
        struct catpt_ipc_msg* reply, int timeout);
    NTSTATUS ipc_wait_completion(int timeout);
    void dsp_send_tx(const struct catpt_ipc_msg* tx);
    void dsp_copy_rx(UINT32 header);
    void dsp_notify_stream(union catpt_notify_msg msg);
    void dsp_process_response(UINT32 header);
    //IPC methods

    //PCM private methods
    NTSTATUS catpt_arm_stream_templates();
    struct catpt_stream* catpt_stream_find(UINT8 stream_hw_id);
    NTSTATUS set_dsp_vol(UINT8 stream_id, LONG* ctlvol);
    void stream_update_position(struct catpt_stream* stream, struct catpt_notify_position* pos);

    //messages private methods
    NTSTATUS ipc_alloc_stream(enum catpt_path_id path_id, enum catpt_stream_type type,
        struct catpt_audio_format* afmt, struct catpt_ring_info* rinfo, UINT8 num_modules,
        struct catpt_module_entry* modules, PRESOURCE persistent, struct catpt_stream_info* sinfo);
    NTSTATUS ipc_free_stream(UINT8 stream_hw_id);
    NTSTATUS ipc_set_device_format(struct catpt_ssp_device_format* devfmt);
    NTSTATUS ipc_set_volume(UINT8 stream_hw_id,
        UINT32 channel, UINT32 volume,
        UINT32 curve_duration,
        enum catpt_audio_curve_type curve_type);
    NTSTATUS ipc_set_write_pos(UINT8 stream_hw_id,
        UINT32 pos, bool eob, bool ll);
    NTSTATUS ipc_reset_stream(UINT8 stream_hw_id);
    NTSTATUS ipc_pause_stream(UINT8 stream_hw_id);
    NTSTATUS ipc_resume_stream(UINT8 stream_hw_id);
public:
    NTSTATUS dsp_irq_handler();
#endif

public:
    CCsAudioCatptSSTHW(_In_  PRESOURCELIST           ResourceList);
    ~CCsAudioCatptSSTHW();

    bool                        ResourcesValidated();
    NTSTATUS sst_init();
    NTSTATUS sst_deinit();

    NTSTATUS sst_program_dma(eDeviceType deviceType, UINT32 byteCount, PMDL mdl, IPortWaveRTStream* stream);
    NTSTATUS sst_play(eDeviceType deviceType);
    NTSTATUS sst_stop(eDeviceType deviceType);
    void force_stop(catpt_stream* stream);
    NTSTATUS sst_current_position(eDeviceType deviceType, UINT32* linkPos, UINT64* linearPos);
    
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
