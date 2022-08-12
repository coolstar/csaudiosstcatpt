#include "definitions.h"
#include "hw.h"
#include "messages.h"
#include "resource.h"
#include <stddef.h>

#include <pshpack1.h>
struct catpt_alloc_stream_input {
	enum catpt_path_id path_id :8;
	enum catpt_stream_type stream_type :8;
	enum catpt_format_id format_id :8;
	//UINT8 reserved; (for GCC/clang only)
	struct catpt_audio_format input_format;
	struct catpt_ring_info ring_info;
	UINT8 num_entries;
	/* flex array with entries here */
	struct catpt_memory_info persistent_mem;
	struct catpt_memory_info scratch_mem;
	UINT32 num_notifications; /* obsolete */
};
#include <poppack.h>

NTSTATUS CCsAudioCatptSSTHW::ipc_alloc_stream(
	enum catpt_path_id path_id,
	enum catpt_stream_type type,
	struct catpt_audio_format* afmt,
	struct catpt_ring_info* rinfo,
	UINT8 num_modules,
	struct catpt_module_entry* mods,
	PRESOURCE persistent,
	struct catpt_stream_info* sinfo
) {
	union catpt_global_msg msg = CATPT_GLOBAL_MSG(ALLOCATE_STREAM);
	struct catpt_alloc_stream_input input;
	struct catpt_ipc_msg request, reply;
	size_t size, arrsz;
	UINT8* payload;
	UINT32 off;
	NTSTATUS status;

	off = offsetof(struct catpt_alloc_stream_input, persistent_mem);
	arrsz = sizeof(*mods) * num_modules;
	size = sizeof(input) + arrsz;

	payload = (UINT8 *)ExAllocatePool2(POOL_FLAG_NON_PAGED, size, CSAUDIOCATPTSST_POOLTAG);
	if (!payload) {
		return STATUS_NO_MEMORY;
	}

	RtlZeroMemory(&input, sizeof(input));
	input.path_id = path_id;
	input.stream_type = type;
	input.format_id = CATPT_FORMAT_PCM;
	input.input_format = *afmt;
	input.ring_info = *rinfo;
	input.num_entries = num_modules;
	input.persistent_mem.offset = (UINT32)catpt_to_dsp_offset(persistent->start);
	input.persistent_mem.size = (UINT32)resource_size(persistent);
	if (scratch) {
		input.scratch_mem.offset = (UINT32)catpt_to_dsp_offset(scratch->start);
		input.scratch_mem.size = (UINT32)resource_size(scratch);
	}

	/* re-arrange the input: account for flex array 'entries' */
	memcpy(payload, &input, sizeof(input));
	memmove(payload + off + arrsz, payload + off, sizeof(input) - off);
	memcpy(payload + off, mods, arrsz);

	request.header = msg.val;
	request.size = size;
	request.data = payload;
	reply.size = sizeof(*sinfo);
	reply.data = sinfo;

	status = ipc_send_msg(request, &reply, CATPT_IPC_TIMEOUT_MS);
	if (!NT_SUCCESS(status)) {
		CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "alloc stream type %d failed 0x%x\n", type, status);
	}
	ExFreePoolWithTag(payload, CSAUDIOCATPTSST_POOLTAG);
	return status;
}

NTSTATUS CCsAudioCatptSSTHW::ipc_free_stream(UINT8 stream_hw_id)
{
	union catpt_global_msg msg = CATPT_GLOBAL_MSG(FREE_STREAM);
	struct catpt_ipc_msg request;
	NTSTATUS status;

	request.header = msg.val;
	request.size = sizeof(stream_hw_id);
	request.data = &stream_hw_id;

	status = ipc_send_msg(request, NULL, CATPT_IPC_TIMEOUT_MS);
	if (!NT_SUCCESS(status)) {
		CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "free stream %d failed: %d\n",
			stream_hw_id, status);
	}

	return status;
}

NTSTATUS CCsAudioCatptSSTHW::ipc_set_device_format(struct catpt_ssp_device_format* devfmt)
{
	union catpt_global_msg msg = CATPT_GLOBAL_MSG(SET_DEVICE_FORMATS);
	struct catpt_ipc_msg request;
	NTSTATUS status;

	request.header = msg.val;
	request.size = sizeof(*devfmt);
	request.data = devfmt;

	status = ipc_send_msg(request, NULL, CATPT_IPC_TIMEOUT_MS);
	if (!NT_SUCCESS(status)) {
		CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "set device format failed: %d\n", status);
	}

	return status;
}

#include <pshpack1.h>
struct catpt_set_volume_input {
	UINT32 channel;
	UINT32 target_volume;
	UINT64 curve_duration;
	UINT32 curve_type;
};
#include <poppack.h>

NTSTATUS CCsAudioCatptSSTHW::ipc_set_volume(UINT8 stream_hw_id,
	UINT32 channel, UINT32 volume,
	UINT32 curve_duration,
	enum catpt_audio_curve_type curve_type)
{
	union catpt_stream_msg msg = CATPT_STAGE_MSG(SET_VOLUME);
	struct catpt_ipc_msg request;
	struct catpt_set_volume_input input;
	NTSTATUS status;

	msg.stream_hw_id = stream_hw_id;
	input.channel = channel;
	input.target_volume = volume;
	input.curve_duration = curve_duration;
	input.curve_type = curve_type;

	request.header = msg.val;
	request.size = sizeof(input);
	request.data = &input;

	status = ipc_send_msg(request, NULL, CATPT_IPC_TIMEOUT_MS);
	if (!NT_SUCCESS(status)) {
		CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "set stream %d volume failed: 0x%x\n",
		stream_hw_id, status);
	}

	return status;
}

#include <pshpack1.h>
struct catpt_set_write_pos_input {
	UINT32 new_write_pos;
	bool end_of_buffer;
	bool low_latency;
};
#include <poppack.h>

NTSTATUS CCsAudioCatptSSTHW::ipc_set_write_pos(UINT8 stream_hw_id,
	UINT32 pos, bool eob, bool ll)
{
	union catpt_stream_msg msg = CATPT_STAGE_MSG(SET_WRITE_POSITION);
	struct catpt_ipc_msg request;
	struct catpt_set_write_pos_input input;
	NTSTATUS status;

	msg.stream_hw_id = stream_hw_id;
	input.new_write_pos = pos;
	input.end_of_buffer = eob;
	input.low_latency = ll;

	request.header = msg.val;
	request.size = sizeof(input);
	request.data = &input;

	status = ipc_send_msg(request, NULL, CATPT_IPC_TIMEOUT_MS);
	if (!NT_SUCCESS(status)) {
		CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "set stream %d write pos failed: 0x%x\n",
			stream_hw_id, status);
	}

	return status;
}

NTSTATUS CCsAudioCatptSSTHW::ipc_reset_stream(UINT8 stream_hw_id)
{
	union catpt_stream_msg msg = CATPT_STREAM_MSG(RESET_STREAM);
	struct catpt_ipc_msg request = { {0} };
	NTSTATUS status;

	msg.stream_hw_id = stream_hw_id;
	request.header = msg.val;

	status = ipc_send_msg(request, NULL, CATPT_IPC_TIMEOUT_MS);
	if (!NT_SUCCESS(status)) {
		CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "reset stream %d failed : 0x%x\n",
			stream_hw_id, status);
	}

	return status;
}

NTSTATUS CCsAudioCatptSSTHW::ipc_pause_stream(UINT8 stream_hw_id)
{
	union catpt_stream_msg msg = CATPT_STREAM_MSG(PAUSE_STREAM);
	struct catpt_ipc_msg request = { {0} };
	NTSTATUS status;

	msg.stream_hw_id = stream_hw_id;
	request.header = msg.val;

	status = ipc_send_msg(request, NULL, CATPT_IPC_TIMEOUT_MS);
	if (!NT_SUCCESS(status)) {
		CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "pause stream %d failed : 0x%x\n",
			stream_hw_id, status);
	}

	return status;
}

NTSTATUS CCsAudioCatptSSTHW::ipc_resume_stream(UINT8 stream_hw_id)
{
	union catpt_stream_msg msg = CATPT_STREAM_MSG(RESUME_STREAM);
	struct catpt_ipc_msg request = { {0} };
	NTSTATUS status;

	msg.stream_hw_id = stream_hw_id;
	request.header = msg.val;

	status = ipc_send_msg(request, NULL, CATPT_IPC_TIMEOUT_MS);
	if (!NT_SUCCESS(status)) {
		CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "resume stream %d failed : 0x%x\n",
			stream_hw_id, status);
	}

	return status;
}