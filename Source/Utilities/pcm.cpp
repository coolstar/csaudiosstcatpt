#include <ntddk.h>
#include "definitions.h"
#include "hw.h"
#include "messages.h"
#include "resource.h"

struct catpt_stream_template {
	enum catpt_path_id path_id;
	enum catpt_stream_type type;
	UINT32 persistent_size;
	UINT8 num_entries;
	struct catpt_module_entry entries[1];
};

static struct catpt_stream_template system_pb = {
	.path_id = CATPT_PATH_SSP0_OUT,
	.type = CATPT_STRM_TYPE_SYSTEM,
	.num_entries = 1,
	.entries = {{ CATPT_MODID_PCM_SYSTEM, 0 }},
};

static struct catpt_stream_template system_cp = {
	.path_id = CATPT_PATH_SSP0_IN,
	.type = CATPT_STRM_TYPE_CAPTURE,
	.num_entries = 1,
	.entries = {{ CATPT_MODID_PCM_CAPTURE, 0 }},
};

static struct catpt_stream_template offload_pb = {
	.path_id = CATPT_PATH_SSP0_OUT,
	.type = CATPT_STRM_TYPE_RENDER,
	.num_entries = 1,
	.entries = {{ CATPT_MODID_PCM, 0 }},
};

static struct catpt_stream_template loopback_cp = {
	.path_id = CATPT_PATH_SSP0_OUT,
	.type = CATPT_STRM_TYPE_LOOPBACK,
	.num_entries = 1,
	.entries = {{ CATPT_MODID_PCM_REFERENCE, 0 }},
};

static struct catpt_stream_template bluetooth_pb = {
	.path_id = CATPT_PATH_SSP1_OUT,
	.type = CATPT_STRM_TYPE_BLUETOOTH_RENDER,
	.num_entries = 1,
	.entries = {{ CATPT_MODID_BLUETOOTH_RENDER, 0 }},
};

static struct catpt_stream_template bluetooth_cp = {
	.path_id = CATPT_PATH_SSP1_IN,
	.type = CATPT_STRM_TYPE_BLUETOOTH_CAPTURE,
	.num_entries = 1,
	.entries = {{ CATPT_MODID_BLUETOOTH_CAPTURE, 0 }},
};

static struct catpt_stream_template* catpt_topology[] = {
	/*[CATPT_STRM_TYPE_RENDER] =*/ &offload_pb,
	/*[CATPT_STRM_TYPE_SYSTEM] =*/ &system_pb,
	/*[CATPT_STRM_TYPE_CAPTURE] =*/ &system_cp,
	/*[CATPT_STRM_TYPE_LOOPBACK] =*/ &loopback_cp,
	/*[CATPT_STRM_TYPE_BLUETOOTH_RENDER] =*/ &bluetooth_pb,
	/*[CATPT_STRM_TYPE_BLUETOOTH_CAPTURE] =*/ &bluetooth_cp,
};

NTSTATUS CCsAudioCatptSSTHW::catpt_arm_stream_templates()
{
	PRESOURCE res;
	UINT32 scratch_size = 0;
	int i, j;

	for (i = 0; i < sizeof(catpt_topology) / sizeof(struct catpt_stream_template *); i++) {
		struct catpt_stream_template* templ;
		struct catpt_module_entry* entry;
		struct catpt_module_type* type;

		templ = catpt_topology[i];
		templ->persistent_size = 0;

		for (j = 0; j < templ->num_entries; j++) {
			entry = &templ->entries[j];
			type = &this->modules[entry->module_id];

			if (!type->loaded)
				return STATUS_NOT_FOUND;

			entry->entry_point = type->entry_point;
			templ->persistent_size += type->persistent_size;
			if (type->scratch_size > scratch_size)
				scratch_size = type->scratch_size;
		}
	}

	if (scratch_size) {
		/* allocate single scratch area for all modules */
		res = catpt_request_region(&this->dram, scratch_size);
		if (!res)
			return STATUS_DEVICE_BUSY;
		this->scratch = res;
	}

	return 0;
}

struct catpt_stream* CCsAudioCatptSSTHW::catpt_stream_find(UINT8 stream_hw_id)
{
	if (this->outStream.info.stream_hw_id == stream_hw_id) {
		return &this->outStream;
	}
	if (this->inStream.info.stream_hw_id == stream_hw_id) {
		return &this->inStream;
	}
	return NULL;
}

NTSTATUS CCsAudioCatptSSTHW::sst_program_dma(eDeviceType deviceType, UINT32 byteCount, PMDL mdl, IPortWaveRTStream* waveStream) {
#if USESSTHW
	NTSTATUS status;

	catpt_stream* stream;

	CatPtPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "Programming stream %d\n", deviceType);

	switch (deviceType) {
	case eSpeakerDevice:
		stream = &this->outStream;
		stream->templ = &system_pb;
		break;
	case eMicJackDevice:
		stream = &this->inStream;
		stream->templ = &system_cp;
		break;
	default:
		CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "Unknown device type");
		return STATUS_INVALID_PARAMETER;
	}

	if (stream->allocated) {
		CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "%s: Already have stream for %d\n", __func__, deviceType);
		return STATUS_INVALID_PARAMETER;
	}

	LONG volMax[CATPT_CHANNELS_MAX] = { 0, 0, 0, 0 };

	int pageCount = waveStream->GetPhysicalPagesCount(mdl);
	if (pageCount < 1) {
		return STATUS_NO_MEMORY;
	}

	PHYSICAL_ADDRESS highAddr;
	highAddr.QuadPart = MAXULONG;

	UINT8* pageTable = (UINT8*)stream->pageTable;
	if (!pageTable) {
		stream->pageTable = MmAllocateContiguousMemory(PAGE_SIZE, highAddr);
		pageTable = (UINT8*)stream->pageTable;
	}
	if (!pageTable) {
		return STATUS_NO_MEMORY;
	}

	RtlZeroMemory(pageTable, PAGE_SIZE);
	for (int i = 0; i < pageCount; i++) {
		PHYSICAL_ADDRESS address = waveStream->GetPhysicalPageAddress(mdl, i);
		LONGLONG addrVal = address.QuadPart;

		UINT32 pfn = (UINT32)(addrVal >> 12);
		UINT32 offset = ((i << 2) + i) >> 1;

		UINT32* page_table = (UINT32*)(pageTable + offset);
		if (i & 1)
			*page_table |= (pfn << 4);
		else
			*page_table |= pfn;
	}

	PHYSICAL_ADDRESS pageTableAddr = MmGetPhysicalAddress(pageTable);

	if (!stream->persistent) {
		stream->persistent = catpt_request_region(&this->dram, stream->templ->persistent_size);
		dsp_update_srampge(&this->dram, this->spec->dram_mask);
	}

	struct catpt_audio_format afmt;
	RtlZeroMemory(&afmt, sizeof(afmt));
	afmt.sample_rate = 48000;
	afmt.bit_depth = 16;
	afmt.valid_bit_depth = 16;
	afmt.num_channels = 2;
	afmt.channel_config = CATPT_CHANNEL_CONFIG_STEREO;
	afmt.channel_map = GENMASK(31, 8) | CATPT_CHANNEL_LEFT
		| (CATPT_CHANNEL_RIGHT << 4);
	afmt.interleaving = CATPT_INTERLEAVING_PER_CHANNEL;

	PHYSICAL_ADDRESS firstPage = waveStream->GetPhysicalPageAddress(mdl, 0);

	struct catpt_ring_info rinfo;
	RtlZeroMemory(&rinfo, sizeof(rinfo));
	rinfo.page_table_addr = pageTableAddr.LowPart; //TODO: figure out page table addr
	rinfo.num_pages = pageCount;
	rinfo.size = byteCount;
	rinfo.offset = 0;
	rinfo.ring_first_page_pfn = (firstPage.LowPart >> 12);

	CatPtPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "Buffer Size: %d, Pages: %d\n", rinfo.size, rinfo.num_pages);

	status = ipc_alloc_stream(
		stream->templ->path_id,
		stream->templ->type,
		&afmt, &rinfo,
		stream->templ->num_entries,
		stream->templ->entries,
		stream->persistent,
		&stream->info
	);

	if (!NT_SUCCESS(status)) {
		CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "Failed to alloc stream: 0x%x\n", status);

		force_stop(stream);
		return status;
	}

	stream->byteCount = byteCount;
	stream->pMDL = mdl;
	stream->waveRtStream = waveStream;
	stream->allocated = true;

	NTSTATUS volStatus;
	volStatus = set_dsp_vol((UINT8)this->outStream.info.stream_hw_id, volMax);
	if (!NT_SUCCESS(volStatus)) {
		CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "Failed to set stream volume 0x%x\n", volStatus);
		//Don't fail here
	}
#else
	UNREFERENCED_PARAMETER(deviceType);
	UNREFERENCED_PARAMETER(stream);
	UNREFERENCED_PARAMETER(mdl);
#endif
	return STATUS_SUCCESS;
}

#define DSP_VOLUME_MAX		INT32_MAX /* 0db */
#define DSP_VOLUME_STEP_MAX	30
static UINT32 ctlvol_to_dspvol(UINT32 value)
{
	if (value > DSP_VOLUME_STEP_MAX)
		value = 0;
	return DSP_VOLUME_MAX >> (DSP_VOLUME_STEP_MAX - value);
}

NTSTATUS CCsAudioCatptSSTHW::set_dsp_vol(UINT8 stream_id, LONG* ctlvol) {
	UINT32 dspvol;
	int i;
	NTSTATUS status;

	for (i = 1; i < CATPT_CHANNELS_MAX; i++)
		if (ctlvol[i] != ctlvol[0])
			break;

	if (i == CATPT_CHANNELS_MAX) {
		dspvol = ctlvol_to_dspvol(ctlvol[0]);

		status = ipc_set_volume(stream_id,
			CATPT_ALL_CHANNELS_MASK, dspvol,
			0, CATPT_AUDIO_CURVE_NONE);
	}
	else {
		for (i = 0; i < CATPT_CHANNELS_MAX; i++) {
			dspvol = ctlvol_to_dspvol(ctlvol[i]);

			status = ipc_set_volume(stream_id,
				i, dspvol,
				0, CATPT_AUDIO_CURVE_NONE);
			if (!NT_SUCCESS(status))
				break;
		}
	}

	return status;
}