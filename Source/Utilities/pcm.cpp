#include "definitions.h"
#include "hw.h"
#include "messages.h"

struct catpt_stream_template {
	enum catpt_path_id path_id;
	enum catpt_stream_type type;
	UINT32 persistent_size;
	UINT8 num_entries;
	struct catpt_module_entry entries[];
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