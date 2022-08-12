#include "definitions.h"
#include "hw.h"
#include "pa2xxssp.h"

#if USESSTHW
NTSTATUS CCsAudioCatptSSTHW::dsp_select_lpclock(BOOL lp, BOOL waiti)
{
	UINT32 mask, reg, val;
	int ret;

	ExAcquireFastMutex(&clk_mutex);

	val = lp ? CATPT_CS_LPCS : 0;
	reg = catpt_readl_shim(this, CS1) & CATPT_CS_LPCS;
	CatPtPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "LPCS [0x%08lx] 0x%08x -> 0x%08x",
		CATPT_CS_LPCS, reg, val);

	if (reg == val) {
		ExReleaseFastMutex(&clk_mutex);
		return STATUS_SUCCESS;
	}

	if (waiti) {
		/* wait for DSP to signal WAIT state */
		ret = catpt_readl_poll_shim(this, ISD,
			CATPT_ISD_DCPWM, CATPT_ISD_DCPWM,
			500, 10000);
		if (ret) {
			DPF(D_ERROR, "await WAITI timeout\n");
			/* no signal - only high clock selection allowed */
			if (lp) {
				ExReleaseFastMutex(&clk_mutex);
				return STATUS_SUCCESS;
			}
		}
	}

	ret = catpt_readl_poll_shim(this, CLKCTL,
		0, CATPT_CLKCTL_CFCIP,
		500, 10000);
	if (ret)
		CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "clock change still in progress\n");

	/* default to DSP core & audio fabric high clock */
	val |= CATPT_CS_DCS_HIGH;
	mask = CATPT_CS_LPCS | CATPT_CS_DCS;
	catpt_updatel_shim(this, CS1, mask, val);

	ret = catpt_readl_poll_shim(this, CLKCTL,
		0, CATPT_CLKCTL_CFCIP,
		500, 10000);
	if (ret)
		CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "clock change still in progress\n");

	/* update PLL accordingly */
	catpt_updatel_pci_raw(this, this->spec->pll_shutdown_reg, this->spec->pll_shutdown_val, lp ? this->spec->pll_shutdown_val : 0);

	ExReleaseFastMutex(&clk_mutex);
	return STATUS_SUCCESS;
}

NTSTATUS CCsAudioCatptSSTHW::dsp_update_lpclock()
{
	if (this->outStream.prepared || this->inStream.prepared)
			return dsp_select_lpclock(false, true);

	return dsp_select_lpclock(true, true);
}

/* bring registers to their defaults as HW won't reset itself */
void CCsAudioCatptSSTHW::dsp_set_regs_defaults()
{
	int i;

	catpt_writel_shim(this, CS1, CATPT_CS_DEFAULT);
	catpt_writel_shim(this, ISC, CATPT_ISC_DEFAULT);
	catpt_writel_shim(this, ISD, CATPT_ISD_DEFAULT);
	catpt_writel_shim(this, IMC, CATPT_IMC_DEFAULT);
	catpt_writel_shim(this, IMD, CATPT_IMD_DEFAULT);
	catpt_writel_shim(this, IPCC, CATPT_IPCC_DEFAULT);
	catpt_writel_shim(this, IPCD, CATPT_IPCD_DEFAULT);
	catpt_writel_shim(this, CLKCTL, CATPT_CLKCTL_DEFAULT);
	catpt_writel_shim(this, CS2, CATPT_CS2_DEFAULT);
	catpt_writel_shim(this, LTRC, CATPT_LTRC_DEFAULT);
	catpt_writel_shim(this, HMDC, CATPT_HMDC_DEFAULT);

	for (i = 0; i < CATPT_SSP_COUNT; i++) {
		catpt_writel_ssp(this, i, SSCR0, CATPT_SSC0_DEFAULT);
		catpt_writel_ssp(this, i, SSCR1, CATPT_SSC1_DEFAULT);
		catpt_writel_ssp(this, i, SSSR, CATPT_SSS_DEFAULT);
		catpt_writel_ssp(this, i, SSITR, CATPT_SSIT_DEFAULT);
		catpt_writel_ssp(this, i, SSDR, CATPT_SSD_DEFAULT);
		catpt_writel_ssp(this, i, SSTO, CATPT_SSTO_DEFAULT);
		catpt_writel_ssp(this, i, SSPSP, CATPT_SSPSP_DEFAULT);
		catpt_writel_ssp(this, i, SSTSA, CATPT_SSTSA_DEFAULT);
		catpt_writel_ssp(this, i, SSRSA, CATPT_SSRSA_DEFAULT);
		catpt_writel_ssp(this, i, SSTSS, CATPT_SSTSS_DEFAULT);
		catpt_writel_ssp(this, i, SSCR2, CATPT_SSCR2_DEFAULT);
		catpt_writel_ssp(this, i, SSPSP2, CATPT_SSPSP2_DEFAULT);
	}
}

void CCsAudioCatptSSTHW::dsp_set_srampge(PRESOURCE sram,
	unsigned long mask, unsigned long newVal)
{
	unsigned long oldVal;
	UINT32 off = (UINT32)sram->start;

	oldVal = catpt_readl_pci(this, VDRTCTL0) & mask;
	CatPtPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "SRAMPGE [0x%08lx] 0x%08lx -> 0x%08lx",
		mask, oldVal, newVal);

	if (oldVal == newVal)
		return;

	catpt_updatel_pci(this, VDRTCTL0, mask, newVal);
	/* wait for SRAM power gating to propagate */
	udelay(60);

	/*
	 * Dummy read as the very first access after block enable
	 * to prevent byte loss in future operations.
	 */
	for (int bit = __ffs(mask); bit <= fls(mask); bit++) {
		if ((newVal >> bit) & 1) {
			continue;
		}

		UINT8 buf[4];

		/* newly enabled: new bit=0 while old bit=1 */
		if ((oldVal >> bit) & 1) {
			CatPtPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "sanitize block %ld: off 0x%08x\n",
				bit - __ffs(mask), off);
			memcpy(buf, this->lpe_ba + off, sizeof(buf));
		}
		off += CATPT_MEMBLOCK_SIZE;
	}
}

void CCsAudioCatptSSTHW::dsp_update_srampge(PRESOURCE sram,
	unsigned long mask)
{
	PRESOURCE resVal;
	unsigned long newVal = 0;

	/* flag all busy blocks */
	for (resVal = sram->child; resVal; resVal = resVal->sibling) {
		UINT32 h, l;

		h = (UINT32)((resVal->end - sram->start) / CATPT_MEMBLOCK_SIZE);
		l = (UINT32)((resVal->start - sram->start) / CATPT_MEMBLOCK_SIZE);
		newVal |= GENMASK(h, l);
	}

	/* offset value given mask's start and invert it as ON=b0 */
	newVal = ~(newVal << __ffs(mask)) & mask;

	/* disable core clock gating */
	catpt_updatel_pci(this, VDRTCTL2, CATPT_VDRTCTL2_DCLCGE, 0);

	dsp_set_srampge(sram, mask, newVal);

	/* enable core clock gating */
	catpt_updatel_pci(this, VDRTCTL2, CATPT_VDRTCTL2_DCLCGE,
		CATPT_VDRTCTL2_DCLCGE);
}

NTSTATUS CCsAudioCatptSSTHW::dsp_stall(BOOL stall)
{
	UINT32 val;

	val = stall ? CATPT_CS_STALL : 0;
	catpt_updatel_shim(this, CS1, CATPT_CS_STALL, val);

	return catpt_readl_poll_shim(this, CS1,
		val, CATPT_CS_STALL,
		500, 10000);
}

NTSTATUS CCsAudioCatptSSTHW::dsp_reset(BOOL reset)
{
	UINT32 val;

	val = reset ? CATPT_CS_RST : 0;
	catpt_updatel_shim(this, CS1, CATPT_CS_RST, val);

	return catpt_readl_poll_shim(this, CS1,
		val, CATPT_CS_RST,
		500, 10000);
}

NTSTATUS CCsAudioCatptSSTHW::dsp_power_down()
{
	UINT32 mask, val;

	/* disable core clock gating */
	catpt_updatel_pci(this, VDRTCTL2, CATPT_VDRTCTL2_DCLCGE, 0);

	dsp_reset(true);
	/* set 24Mhz clock for both SSPs */
	catpt_updatel_shim(this, CS1, CATPT_CS_SBCS(0) | CATPT_CS_SBCS(1),
		CATPT_CS_SBCS(0) | CATPT_CS_SBCS(1));
	dsp_select_lpclock(true, false);
	/* disable MCLK */
	catpt_updatel_shim(this, CLKCTL, CATPT_CLKCTL_SMOS, 0);

	dsp_set_regs_defaults();

	/* switch clock gating */
	mask = CATPT_VDRTCTL2_CGEALL & (~CATPT_VDRTCTL2_DCLCGE);
	val = mask & (~CATPT_VDRTCTL2_DTCGE);
	catpt_updatel_pci(this, VDRTCTL2, mask, val);
	/* enable DTCGE separatelly */
	catpt_updatel_pci(this, VDRTCTL2, CATPT_VDRTCTL2_DTCGE,
		CATPT_VDRTCTL2_DTCGE);

	/* SRAM power gating all */
	dsp_set_srampge(&this->dram, this->spec->dram_mask,
		this->spec->dram_mask);
	dsp_set_srampge(&this->iram, this->spec->iram_mask,
		this->spec->iram_mask);
	mask = this->spec->d3srampgd_bit | this->spec->d3pgd_bit;
	catpt_updatel_pci(this, VDRTCTL0, mask, this->spec->d3pgd_bit);

	catpt_updatel_pci(this, PMCS, PCI_PM_CTRL_STATE_MASK, PCI_D3hot);
	/* give hw time to drop off */
	udelay(50);

	/* enable core clock gating */
	catpt_updatel_pci(this, VDRTCTL2, CATPT_VDRTCTL2_DCLCGE,
		CATPT_VDRTCTL2_DCLCGE);

	udelay(50);

	return STATUS_SUCCESS;
}

NTSTATUS CCsAudioCatptSSTHW::dsp_power_up() {
    UINT32 mask, val;

    /* disable core clock gating */
    catpt_updatel_pci(this, VDRTCTL2, CATPT_VDRTCTL2_DCLCGE, 0);

    /* switch clock gating */
    mask = CATPT_VDRTCTL2_CGEALL & (~CATPT_VDRTCTL2_DCLCGE);
    val = mask & (~CATPT_VDRTCTL2_DTCGE);
    catpt_updatel_pci(this, VDRTCTL2, mask, val);

    catpt_updatel_pci(this, PMCS, PCI_PM_CTRL_STATE_MASK, PCI_D0);

    /* SRAM power gating none */
    mask = this->spec->d3srampgd_bit | this->spec->d3pgd_bit;
    catpt_updatel_pci(this, VDRTCTL0, mask, mask);
    dsp_set_srampge(&this->dram, this->spec->dram_mask, 0);
    dsp_set_srampge(&this->iram, this->spec->iram_mask, 0);

    dsp_set_regs_defaults();

    /* restore MCLK */
    catpt_updatel_shim(this, CLKCTL, CATPT_CLKCTL_SMOS, CATPT_CLKCTL_SMOS);
    dsp_select_lpclock(false, false);
    /* set 24Mhz clock for both SSPs */
    catpt_updatel_shim(this, CS1, CATPT_CS_SBCS(0) | CATPT_CS_SBCS(1),
        CATPT_CS_SBCS(0) | CATPT_CS_SBCS(1));
    dsp_reset(false);

    /* enable core clock gating */
    catpt_updatel_pci(this, VDRTCTL2, CATPT_VDRTCTL2_DCLCGE,
        CATPT_VDRTCTL2_DCLCGE);

    /* generate int deassert msg to fix inversed int logic */
    catpt_updatel_shim(this, IMC, CATPT_IMC_IPCDB | CATPT_IMC_IPCCD, 0);

    return STATUS_SUCCESS;
}
#endif