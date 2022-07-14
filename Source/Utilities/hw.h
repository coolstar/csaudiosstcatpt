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

#if USEACPHW
#include "acp_chip_offset_byte.h"
#include "acp3x.h"
#define BIT(nr) (1UL << (nr))

union baseaddr {
    PVOID Base;
    UINT8* baseptr;
};

typedef struct _PCI_BAR {
    union baseaddr Base;
    ULONG Len;
} PCI_BAR, * PPCI_BAR;
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

class CCsAudioAcp3xHW
{
public:
protected:
    LONG                        m_PeakMeterControls[MAX_TOPOLOGY_NODES];
    ULONG                       m_ulMux;            // Mux selection
    BOOL                        m_bDevSpecific;
    INT                         m_iDevSpecific;
    UINT                        m_uiDevSpecific;
#if USEACPHW
    PCI_BAR m_BAR0;
    UINT32 m_pme_en;

    INT bt_running_streams;
    INT sp_running_streams;

    UINT32 rv_read32(UINT32 reg);
    void rv_write32(UINT32 reg, UINT32 val);
#endif

private:
#if USEACPHW
    NTSTATUS acp3x_power_on();
    NTSTATUS acp3x_power_off();
    NTSTATUS acp3x_reset();
#endif
public:
    CCsAudioAcp3xHW(_In_  PRESOURCELIST           ResourceList);
    ~CCsAudioAcp3xHW();

    bool                        ResourcesValidated();

    NTSTATUS acp3x_init();
    NTSTATUS acp3x_deinit();

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
typedef CCsAudioAcp3xHW    *PCCsAudioAcp3xHW;

#endif  // _CSAUDIOACP3X_HW_H_
