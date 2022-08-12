#include <ntddk.h>
#include "dw_dma.h"
#include "bitops.h"
#include "hw.h"

// Pool tag used for DMA allocations
#define DWDMA_POOLTAG               'MDWD'  

DwDMA::DwDMA(void* regs) {
	this->regs = regs;

	this->pdata = NULL;
	this->chan = NULL;
	this->all_chan_mask = 0;
}

NTSTATUS DwDMA::init() {
	NTSTATUS status;

	this->pdata = (struct dw_dma_platform_data*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*this->pdata), DWDMA_POOLTAG);
	if (!this->pdata) {
		return STATUS_NO_MEMORY;
	}

	UINT32 dw_params = dma_readl(this, DW_PARAMS);
	CatPtPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "DW_PARMS: 0x%08x\n", dw_params);

	BOOL autocfg = FALSE;
	autocfg = dw_params >> DW_PARAMS_EN & 1;
	if (!autocfg){
		status = STATUS_INVALID_PARAMETER;
		goto err_pdata;
	}

	/* Get hardware configuration parameters */
	pdata->nr_channels = (dw_params >> DW_PARAMS_NR_CHAN & 7) + 1;
	pdata->nr_masters = (dw_params >> DW_PARAMS_NR_MASTER & 3) + 1;
	for (unsigned int i = 0; i < pdata->nr_masters; i++) {
		pdata->data_width[i] =
			4 << (dw_params >> DW_PARAMS_DATA_WIDTH(i) & 3);
	}
	pdata->block_size = dma_readl(this, MAX_BLK_SIZE);

	/* Fill platform data with the default values */
	pdata->chan_allocation_order = CHAN_ALLOCATION_ASCENDING;
	pdata->chan_priority = CHAN_PRIORITY_ASCENDING;

	//pdata done

	this->chan = (struct dw_dma_chan*)ExAllocatePool2(POOL_FLAG_NON_PAGED, pdata->nr_channels * sizeof(*this->chan), DWDMA_POOLTAG);

	if (!this->chan) {
		status = STATUS_NO_MEMORY;
		goto err_pdata;
	}

	/* Calculate all channel mask before DMA setup */
	this->all_chan_mask = (1 << pdata->nr_channels) - 1;

	/* Force dma off, just in case */
	this->disable();

	for (UINT32 i = 0; i < pdata->nr_channels; i++) {
		struct dw_dma_chan* dwc = &this->chan[i];

		/* 7 is highest priority & 0 is lowest. */
		if (pdata->chan_priority == CHAN_PRIORITY_ASCENDING)
			dwc->priority = (UINT8)(pdata->nr_channels - i - 1);
		else
			dwc->priority = (UINT8)i;

		dwc->ch_regs = &__dw_regs(this)->CHAN[i];

		ExInitializeFastMutex(&dwc->lock);

		dwc->mask = 1 << i;

		channel_clear_bit(this, CH_EN, dwc->mask);

		dwc->direction = DMA_TRANS_NONE;

		/* Hardware configuration */
		if (autocfg) {
			unsigned int r = DW_DMA_MAX_NR_CHANNELS - i - 1;
			void* addr = &__dw_regs(this)->DWC_PARAMS[r];
			unsigned int dwc_params = readl(addr);

			CatPtPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "DWC_PARAMS[%d]: 0x%08x\n", i,
				dwc_params);

			/*
			 * Decode maximum block size for given channel. The
			 * stored 4 bit value represents blocks from 0x00 for 3
			 * up to 0x0a for 4095.
			 */
			dwc->block_size =
				(4 << ((pdata->block_size >> 4 * i) & 0xf)) - 1;

			/*
			 * According to the DW DMA databook the true scatter-
			 * gether LLPs aren't available if either multi-block
			 * config is disabled (CHx_MULTI_BLK_EN == 0) or the
			 * LLP register is hard-coded to zeros
			 * (CHx_HC_LLP == 1).
			 */
			dwc->nollp =
				(dwc_params >> DWC_PARAMS_MBLK_EN & 0x1) == 0 ||
				(dwc_params >> DWC_PARAMS_HC_LLP & 0x1) == 1;
			dwc->max_burst =
				(0x4 << (dwc_params >> DWC_PARAMS_MSIZE & 0x7));
		}
		else {
			dwc->block_size = pdata->block_size;
			dwc->nollp = !pdata->multi_block[i];
			dwc->max_burst = pdata->max_burst[i] ? 0 : DW_DMA_MAX_BURST;
		}
	}

	/* Clear all interrupts on all channels. */
	dma_writel(this, CLEAR.XFER, this->all_chan_mask);
	dma_writel(this, CLEAR.BLOCK, this->all_chan_mask);
	dma_writel(this, CLEAR.SRC_TRAN, this->all_chan_mask);
	dma_writel(this, CLEAR.DST_TRAN, this->all_chan_mask);
	dma_writel(this, CLEAR.ERROR, this->all_chan_mask);

	status = STATUS_SUCCESS;

err_pdata:
	return status;
}

DwDMA::~DwDMA() {
	this->disable();

	for (UINT32 i = 0; i < pdata->nr_channels; i++) {
		struct dw_dma_chan* dwc = &this->chan[i];
		channel_clear_bit(this, CH_EN, dwc->mask);
	}

	if (this->chan)
		ExFreePoolWithTag(this->chan, DWDMA_POOLTAG);
	if (this->pdata)
		ExFreePoolWithTag(this->pdata, DWDMA_POOLTAG);
}

void DwDMA::disable() {
	dma_writel(this, CFG, 0);

	channel_clear_bit(this, MASK.XFER, this->all_chan_mask);
	channel_clear_bit(this, MASK.BLOCK, this->all_chan_mask);
	channel_clear_bit(this, MASK.SRC_TRAN, this->all_chan_mask);
	channel_clear_bit(this, MASK.DST_TRAN, this->all_chan_mask);
	channel_clear_bit(this, MASK.ERROR, this->all_chan_mask);

	while (dma_readl(this, CFG) & DW_CFG_DMA_EN) {
		LARGE_INTEGER Interval;
		Interval.QuadPart = -10 * 1000;
		KeDelayExecutionThread(KernelMode, false, &Interval);
	}
}

void DwDMA::enable() {
	dma_writel(this, CFG, DW_CFG_DMA_EN);
}

UINT32 DwDMA::readl(PVOID addr) {
	UINT32 ret = *(UINT32*)addr;
	//DbgPrint("Read from %p: 0x%x\n", addr, ret);
	return ret;
}

void DwDMA::writel(UINT32 data, PVOID addr) {
	*(UINT32*)addr = data;
	//DbgPrint("Write to %p: 0x%x\n", addr, data);
}

UINT32 DwDMA::bytes2block(struct dw_dma_chan* dwc, UINT32 bytes, unsigned int width, UINT32* len) {
	UINT32 block;

	if ((bytes >> width) > dwc->block_size) {
		block = dwc->block_size;
		*len = (UINT32)dwc->block_size << width;
	}
	else {
		block = bytes >> width;
		*len = bytes;
	}

	return block;
}

NTSTATUS DwDMA::transfer_dma(UINT32 dest, UINT32 src, UINT32 len) {
	this->enable();
	struct dw_dma_chan* dwc = NULL;
	for (UINT32 i = 0; i < this->pdata->nr_channels; i++) {
		if (dma_readl(this, CH_EN) & this->chan[i].mask) {
			DPF(D_ERROR, "Channel not idle!\n");
		}
		else {
			dwc = &this->chan[i];
			break;
		}
	}

	if (!dwc) {
		return STATUS_RESOURCE_IN_USE;
	}

	UINT8 m_master = 0;

	UINT8 lms = DWC_LLP_LMS(m_master);

	UINT32 data_width = this->pdata->data_width[m_master];

	UINT32 src_width, dst_width;
	src_width = dst_width = __ffs(data_width | src | dest | len);

	dwc->direction = DMA_MEM_TO_MEM;

	UINT32 ctllo;
	UINT32 ctlhi;

	{
		//prepare ctllo
		UINT8 smsize = (dwc->direction == DMA_DEV_TO_MEM) ? 3 : 0; //src_maxburst (DMA_DEV_MEM_TO_MEM) [16 -> 3]
		UINT8 dmsize = (dwc->direction == DMA_MEM_TO_DEV) ? 3 : 0; //dst_maxburst (DMA_DEV_MEM_TO_DEV) [16 -> 3]
		UINT8 p_master = 0;
		UINT8 dms = (dwc->direction == DMA_MEM_TO_DEV) ? p_master : m_master;
		UINT8 sms = (dwc->direction == DMA_DEV_TO_MEM) ? p_master : m_master;

		ctllo = DWC_CTLL_LLP_D_EN | DWC_CTLL_LLP_S_EN |
			DWC_CTLL_DST_MSIZE(dmsize) | DWC_CTLL_SRC_MSIZE(smsize) |
			DWC_CTLL_DMS(dms) | DWC_CTLL_SMS(sms);
	}
	ctllo = ctllo | DWC_CTLL_DST_WIDTH(dst_width)
		| DWC_CTLL_SRC_WIDTH(src_width)
		| DWC_CTLL_DST_INC
		| DWC_CTLL_SRC_INC
		| DWC_CTLL_FC_M2M;

	NTSTATUS status;

	PHYSICAL_ADDRESS maxAddr;
	maxAddr.QuadPart = MAXULONG32;
	UINT8* vaddr;
	vaddr = (UINT8 *)MmAllocateContiguousMemory(0x1000, maxAddr);
	if (!vaddr) {
		status = STATUS_NO_MEMORY;
		goto err;
	}

	RtlZeroMemory(vaddr, 0x1000);

	UINT8* curVaddr;
	curVaddr = vaddr;

	struct dw_lli* prev;
	prev = NULL;

	UINT32 xfer_count, offset;
	for (offset = 0; offset < len; offset += xfer_count) {
		ctlhi = this->bytes2block(dwc, len - offset, src_width, &xfer_count);

		//write sar, dar, ctllo, ctlhi
		struct dw_lli* cur = (struct dw_lli*)curVaddr;
		if (curVaddr + sizeof(struct dw_lli) > vaddr + 0x1000) {
			status = STATUS_NO_MEMORY;
			DPF(D_ERROR, "Unable to get lli entry\n");
			goto err;
		}

		cur->sar = (UINT32)(src + offset);
		cur->dar = (UINT32)(dest + offset);
		cur->ctllo = ctllo;
		cur->ctlhi = ctlhi;
		cur->llp = 0;

		if (prev) {
			PHYSICAL_ADDRESS paddr;
			paddr = MmGetPhysicalAddress(cur);

			prev->llp = paddr.LowPart | lms;
		}
		prev = cur;

		curVaddr += sizeof(struct dw_lli);
	}

	if (prev) {
		prev->ctllo &= ~(DWC_CTLL_LLP_D_EN | DWC_CTLL_LLP_S_EN);
	}

	PHYSICAL_ADDRESS paddr;
	paddr = MmGetPhysicalAddress(vaddr);

	//Begin start transfer
	{
		//Initialize channel

		UINT32 cfghi = is_slave_direction(dwc->direction) ? 0 : DWC_CFGH_FIFO_MODE;
		UINT32 cfglo = DWC_CFGL_CH_PRIOR(dwc->priority);
		BOOL hs_polarity = FALSE;

		cfghi |= DWC_CFGH_DST_PER(0); //dwc->dws.dst_id
		cfghi |= DWC_CFGH_SRC_PER(0); //dwc->dws.src_id
		cfghi |= DWC_CFGH_PROTCTL(this->pdata->protctl);

		/* Set polarity of handshake interface */
		cfglo |= hs_polarity ? DWC_CFGL_HS_DST_POL | DWC_CFGL_HS_SRC_POL : 0;

		channel_writel(dwc, CFG_LO, cfglo);
		channel_writel(dwc, CFG_HI, cfghi);
	}

	/* Enable interrupts */
	channel_set_bit(this, MASK.XFER, dwc->mask);
	channel_set_bit(this, MASK.ERROR, dwc->mask);

	channel_writel(dwc, LLP, paddr.LowPart | lms);
	channel_writel(dwc, CTL_LO, DWC_CTLL_LLP_D_EN | DWC_CTLL_LLP_S_EN);
	channel_writel(dwc, CTL_HI, 0);
	channel_set_bit(this, CH_EN, dwc->mask);

	LARGE_INTEGER Start;
	KeQuerySystemTimePrecise(&Start);

	//Check status
	while (true) {
		LARGE_INTEGER Cur;
		KeQuerySystemTimePrecise(&Cur);

		if (Cur.QuadPart - Start.QuadPart > 10 * 1000 * 1000 * 1) {
			CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "Timed out!\n");
			CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "Regs; SAR: 0x%x DAR: 0x%x LLP: 0x%x CTL: 0x%x:%08x\n",
				channel_readl(dwc, SAR),
				channel_readl(dwc, DAR),
				channel_readl(dwc, LLP),
				channel_readl(dwc, CTL_HI),
				channel_readl(dwc, CTL_LO));
			status = STATUS_TIMEOUT;
			break;
		}

		UINT32 status_xfer = dma_readl(this, RAW.XFER);
		if (status_xfer & dwc->mask) {
			dma_writel(this, CLEAR.XFER, dwc->mask);
			if (dma_readl(this, CH_EN) & dwc->mask) {
				DPF(D_ERROR, "Channel not idle???\n");
				status = STATUS_INTERNAL_ERROR;
			}
			else {
				status = STATUS_SUCCESS;
			}
			break;
		}

		LARGE_INTEGER Interval;
		Interval.QuadPart = -10 * 10;
		KeDelayExecutionThread(KernelMode, false, &Interval);
	}

err:
	if (vaddr) {
		MmFreeContiguousMemory(vaddr);
	}
	this->disable();
	return status;
}