#include <ntddk.h>
#include "definitions.h"
#include "hw.h"
#include "resource.h"

/* FW load (200ms) plus operational delays */
#define FW_READY_TIMEOUT_MS	250

#define FW_SIGNATURE		"$SST"
#define FW_SIGNATURE_SIZE	4

#include <pshpack1.h>
struct catpt_fw_hdr {
	char signature[FW_SIGNATURE_SIZE];
	UINT32 file_size;
	UINT32 modules;
	UINT32 file_format;
	UINT32 reserved[4];
};

struct catpt_fw_mod_hdr {
	char signature[FW_SIGNATURE_SIZE];
	UINT32 mod_size;
	UINT32 blocks;
	UINT16 slot;
	UINT16 module_id;
	UINT32 entry_point;
	UINT32 persistent_size;
	UINT32 scratch_size;
};

enum catpt_ram_type {
	CATPT_RAM_TYPE_IRAM = 1,
	CATPT_RAM_TYPE_DRAM = 2,
	/* DRAM with module's initial state */
	CATPT_RAM_TYPE_INSTANCE = 3,
};

struct catpt_fw_block_hdr {
	UINT32 ram_type;
	UINT32 size;
	UINT32 ram_offset;
	UINT32 rsvd;
};
#include <poppack.h>

void CCsAudioCatptSSTHW::sram_init(PRESOURCE sram, UINT32 start, UINT32 size) {
	sram->start = start;
	sram->end = start + size - 1;
}

void CCsAudioCatptSSTHW::sram_free(PRESOURCE sram) {
	PRESOURCE res, save;

	for (res = sram->child; res;) {
		save = res->sibling;
		release_resource(res);
		ExFreePoolWithTag(res, CSAUDIOCATPTSST_POOLTAG);
		res = save;
	}
}

PRESOURCE CCsAudioCatptSSTHW::catpt_request_region(PRESOURCE root, size_t size)
{
	PRESOURCE res = root->child;
	size_t addr = root->start;

	for (;;) {
		if (res->start - addr >= size)
			break;
		addr = res->end + 1;
		res = res->sibling;
		if (!res)
			return NULL;
	}

	return __request_region(root, addr, size, 0);
}

NTSTATUS CCsAudioCatptSSTHW::catpt_load_block(PHYSICAL_ADDRESS pAddr, struct catpt_fw_block_hdr* blk, bool alloc)
{
	NTSTATUS status;
	PRESOURCE sram;

	switch (blk->ram_type) {
	case CATPT_RAM_TYPE_IRAM:
		sram = &this->iram;
		break;
	default:
		sram = &this->dram;
		break;
	}

	UINT32 dst_addr = (UINT32)(sram->start + blk->ram_offset);
	//TODO: mark region as busy

	if (alloc) {
		PRESOURCE res = __request_region(sram, dst_addr, blk->size, 0);
		if (!res) {
			return STATUS_DEVICE_BUSY;
		}
	}

	dst_addr |= CATPT_DMA_DSP_ADDR_MASK;

	/* advance to data area */
	pAddr.QuadPart += sizeof(*blk);

	//TODO: dma_memcpy_todsp
	status = this->dmac->transfer_dma(dst_addr, pAddr.LowPart, blk->size);

	if (!NT_SUCCESS(status)) {
		__release_region(sram, dst_addr, blk->size);
	}

	return status;
}

NTSTATUS CCsAudioCatptSSTHW::catpt_load_module(PHYSICAL_ADDRESS paddr, struct catpt_fw_mod_hdr* mod)
{
	struct catpt_module_type* type;
	UINT32 offset = sizeof(*mod);

	type = &this->modules[mod->module_id];

	for (UINT32 i = 0; i < mod->blocks; i++) {
		struct catpt_fw_block_hdr* blk;
		NTSTATUS status;

		blk = (struct catpt_fw_block_hdr*)((UINT8*)mod + offset);

		PHYSICAL_ADDRESS blockPaddr;
		blockPaddr.QuadPart = paddr.QuadPart + offset;
		status = catpt_load_block(blockPaddr, blk, true);
		if (!NT_SUCCESS(status)) {
			CatPtPrint(DEBUG_LEVEL_ERROR, DBG_PNP, "load block failed: 0x%x\n", status);
			return status;
		}
		/*
		 * Save state window coordinates - these will be
		 * used to capture module state on D0 exit.
		 */
		if (blk->ram_type == CATPT_RAM_TYPE_INSTANCE) {
			type->state_offset = blk->ram_offset;
			type->state_size = blk->size;
		}

		offset += sizeof(*blk) + blk->size;
	}

	/* init module type static info */
	type->loaded = true;
	/* DSP expects address from module header substracted by 4 */
	type->entry_point = mod->entry_point - 4;
	type->persistent_size = mod->persistent_size;
	type->scratch_size = mod->scratch_size;

	return STATUS_SUCCESS;
}

NTSTATUS CCsAudioCatptSSTHW::catpt_load_firmware(PHYSICAL_ADDRESS paddr, struct catpt_fw_hdr* fw) {
	UINT32 offset = sizeof(*fw);

	for (UINT32 i = 0; i < fw->modules; i++) {
		struct catpt_fw_mod_hdr* mod;
		NTSTATUS status;

		mod = (struct catpt_fw_mod_hdr*)((UINT8*)fw + offset);
		if (strncmp(fw->signature, mod->signature,
			FW_SIGNATURE_SIZE)) {
			DPF(D_ERROR, "module signature mismatch\n");
			return STATUS_INVALID_PARAMETER;
		}

		if (mod->module_id > CATPT_MODID_LAST)
			return STATUS_INVALID_PARAMETER;

		PHYSICAL_ADDRESS modulePaddr;
		modulePaddr.QuadPart = paddr.QuadPart + offset;
		status = catpt_load_module(modulePaddr, mod);
		if (!NT_SUCCESS(status)) {
			CatPtPrint(DEBUG_LEVEL_ERROR, DBG_PNP, "load module failed: 0x%x\n", status);
			return status;
		}

		offset += sizeof(*mod) + mod->mod_size;
	}
	return STATUS_SUCCESS;
}

NTSTATUS CCsAudioCatptSSTHW::catpt_load_image(PCWSTR path, BOOL restore) {
	NTSTATUS status;
	struct firmware* img;
	struct catpt_fw_hdr* fw;

	const char* signature = FW_SIGNATURE;

	status = request_firmware((const struct firmware**)&img, path);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	fw = (struct catpt_fw_hdr*)img->data;
	if (strncmp(fw->signature, signature, FW_SIGNATURE_SIZE)) {
		status = STATUS_INVALID_PARAMETER;
		goto release_fw;
	}

	PHYSICAL_ADDRESS maxAddr;
	maxAddr.QuadPart = MAXULONG32;
	void* vaddr;
	vaddr = MmAllocateContiguousMemory(img->size, maxAddr);
	if (!vaddr) {
		status = STATUS_NO_MEMORY;
		goto release_fw;
	}
	memcpy(vaddr, img->data, img->size);

	PHYSICAL_ADDRESS paddr;
	paddr = MmGetPhysicalAddress(vaddr);

	fw = (struct catpt_fw_hdr*)vaddr;
	if (restore) {
		//TODO: restore
	}
	else {
		status = catpt_load_firmware(paddr, fw);
	}

	MmFreeContiguousMemory(vaddr);
release_fw:
	free_firmware(img);
	return status;
}

NTSTATUS CCsAudioCatptSSTHW::catpt_boot_firmware(BOOL restore) {
	NTSTATUS status;

	dsp_stall(true);

	status = catpt_load_image(L"\\SystemRoot\\system32\\DRIVERS\\IntcSST2.bin", restore);
	if (status) {
		CatPtPrint(DEBUG_LEVEL_ERROR, DBG_PNP, "Load binaries failed: 0x%x\n", status);
		return status;
	}

	this->fw_ready = FALSE;
	dsp_stall(false);

	LARGE_INTEGER StartTime;
	KeQuerySystemTimePrecise(&StartTime);
	while (this->fw_ready == FALSE) {
		LARGE_INTEGER CurrentTime;
		KeQuerySystemTimePrecise(&CurrentTime);

		if (((CurrentTime.QuadPart - StartTime.QuadPart) / (10 * 1000)) >= FW_READY_TIMEOUT_MS) {
			DPF(D_ERROR, "Firmware ready timeout\n");
			return STATUS_TIMEOUT;
		}
	}
	DPF(D_ERROR, "Firmware ready!!!\n");

	/* update sram pg & clock once done booting */
	dsp_update_srampge(&this->dram, this->spec->dram_mask);
	dsp_update_srampge(&this->iram, this->spec->iram_mask);

	return dsp_update_lpclock();
}