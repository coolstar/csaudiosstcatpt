#pragma once
#include "definitions.h"

#define DW_DMA_MAX_NR_MASTERS	4
#define DW_DMA_MAX_NR_CHANNELS	8
#define DW_DMA_MIN_BURST	1
#define DW_DMA_MAX_BURST	256

/*
 * Redefine this macro to handle differences between 32- and 64-bit
 * addressing, big vs. little endian, etc.
 */
#define DW_REG(name)		UINT32 name; UINT32 __pad_##name

 /* Hardware register definitions. */
struct dw_dma_chan_regs {
	DW_REG(SAR);		/* Source Address Register */
	DW_REG(DAR);		/* Destination Address Register */
	DW_REG(LLP);		/* Linked List Pointer */
	UINT32	CTL_LO;		/* Control Register Low */
	UINT32	CTL_HI;		/* Control Register High */
	DW_REG(SSTAT);
	DW_REG(DSTAT);
	DW_REG(SSTATAR);
	DW_REG(DSTATAR);
	UINT32	CFG_LO;		/* Configuration Register Low */
	UINT32	CFG_HI;		/* Configuration Register High */
	DW_REG(SGR);
	DW_REG(DSR);
};

struct dw_dma_irq_regs {
	DW_REG(XFER);
	DW_REG(BLOCK);
	DW_REG(SRC_TRAN);
	DW_REG(DST_TRAN);
	DW_REG(ERROR);
};

struct dw_dma_regs {
	/* per-channel registers */
	struct dw_dma_chan_regs	CHAN[DW_DMA_MAX_NR_CHANNELS];

	/* irq handling */
	struct dw_dma_irq_regs	RAW;		/* r */
	struct dw_dma_irq_regs	STATUS;		/* r (raw & mask) */
	struct dw_dma_irq_regs	MASK;		/* rw (set = irq enabled) */
	struct dw_dma_irq_regs	CLEAR;		/* w (ack, affects "raw") */

	DW_REG(STATUS_INT);			/* r */

	/* software handshaking */
	DW_REG(REQ_SRC);
	DW_REG(REQ_DST);
	DW_REG(SGL_REQ_SRC);
	DW_REG(SGL_REQ_DST);
	DW_REG(LAST_SRC);
	DW_REG(LAST_DST);

	/* miscellaneous */
	DW_REG(CFG);
	DW_REG(CH_EN);
	DW_REG(ID);
	DW_REG(TEST);

	/* iDMA 32-bit support */
	DW_REG(CLASS_PRIORITY0);
	DW_REG(CLASS_PRIORITY1);

	/* optional encoded params, 0x3c8..0x3f7 */
	UINT32	dw___reserved;

	/* per-channel configuration registers */
	UINT32	DWC_PARAMS[DW_DMA_MAX_NR_CHANNELS];
	UINT32	MULTI_BLK_TYPE;
	UINT32	MAX_BLK_SIZE;

	/* top-level parameters */
	UINT32	DW_PARAMS;

	/* component ID */
	UINT32	COMP_TYPE;
	UINT32	COMP_VERSION;

	/* iDMA 32-bit support */
	DW_REG(FIFO_PARTITION0);
	DW_REG(FIFO_PARTITION1);

	DW_REG(SAI_ERR);
	DW_REG(GLOBAL_CFG);
};

/* Bitfields in DW_PARAMS */
#define DW_PARAMS_NR_CHAN	8		/* number of channels */
#define DW_PARAMS_NR_MASTER	11		/* number of AHB masters */
#define DW_PARAMS_DATA_WIDTH(n)	(15 + 2 * (n))
#define DW_PARAMS_DATA_WIDTH1	15		/* master 1 data width */
#define DW_PARAMS_DATA_WIDTH2	17		/* master 2 data width */
#define DW_PARAMS_DATA_WIDTH3	19		/* master 3 data width */
#define DW_PARAMS_DATA_WIDTH4	21		/* master 4 data width */
#define DW_PARAMS_EN		28		/* encoded parameters */

/* Bitfields in DWC_PARAMS */
#define DWC_PARAMS_MBLK_EN	11		/* multi block transfer */
#define DWC_PARAMS_HC_LLP	13		/* set LLP register to zero */
#define DWC_PARAMS_MSIZE	16		/* max group transaction size */

/* bursts size */
enum dw_dma_msize {
	DW_DMA_MSIZE_1,
	DW_DMA_MSIZE_4,
	DW_DMA_MSIZE_8,
	DW_DMA_MSIZE_16,
	DW_DMA_MSIZE_32,
	DW_DMA_MSIZE_64,
	DW_DMA_MSIZE_128,
	DW_DMA_MSIZE_256,
};

/* Bitfields in LLP */
#define DWC_LLP_LMS(x)		((x) & 3)	/* list master select */
#define DWC_LLP_LOC(x)		((x) & ~3)	/* next lli */

/* Bitfields in CTL_LO */
#define DWC_CTLL_INT_EN		(1 << 0)	/* irqs enabled? */
#define DWC_CTLL_DST_WIDTH(n)	((n)<<1)	/* bytes per element */
#define DWC_CTLL_SRC_WIDTH(n)	((n)<<4)
#define DWC_CTLL_DST_INC	(0<<7)		/* DAR update/not */
#define DWC_CTLL_DST_DEC	(1<<7)
#define DWC_CTLL_DST_FIX	(2<<7)
#define DWC_CTLL_SRC_INC	(0<<9)		/* SAR update/not */
#define DWC_CTLL_SRC_DEC	(1<<9)
#define DWC_CTLL_SRC_FIX	(2<<9)
#define DWC_CTLL_DST_MSIZE(n)	((n)<<11)	/* burst, #elements */
#define DWC_CTLL_SRC_MSIZE(n)	((n)<<14)
#define DWC_CTLL_S_GATH_EN	(1 << 17)	/* src gather, !FIX */
#define DWC_CTLL_D_SCAT_EN	(1 << 18)	/* dst scatter, !FIX */
#define DWC_CTLL_FC(n)		((n) << 20)
#define DWC_CTLL_FC_M2M		(0 << 20)	/* mem-to-mem */
#define DWC_CTLL_FC_M2P		(1 << 20)	/* mem-to-periph */
#define DWC_CTLL_FC_P2M		(2 << 20)	/* periph-to-mem */
#define DWC_CTLL_FC_P2P		(3 << 20)	/* periph-to-periph */
/* plus 4 transfer types for peripheral-as-flow-controller */
#define DWC_CTLL_DMS(n)		((n)<<23)	/* dst master select */
#define DWC_CTLL_SMS(n)		((n)<<25)	/* src master select */
#define DWC_CTLL_LLP_D_EN	(1 << 27)	/* dest block chain */
#define DWC_CTLL_LLP_S_EN	(1 << 28)	/* src block chain */

/* Bitfields in CTL_HI */
#define DWC_CTLH_BLOCK_TS_MASK	GENMASK(11, 0)
#define DWC_CTLH_BLOCK_TS(x)	((x) & DWC_CTLH_BLOCK_TS_MASK)
#define DWC_CTLH_DONE		(1 << 12)

/* Bitfields in CFG_LO */
#define DWC_CFGL_CH_PRIOR_MASK	(0x7 << 5)	/* priority mask */
#define DWC_CFGL_CH_PRIOR(x)	((x) << 5)	/* priority */
#define DWC_CFGL_CH_SUSP	(1 << 8)	/* pause xfer */
#define DWC_CFGL_FIFO_EMPTY	(1 << 9)	/* pause xfer */
#define DWC_CFGL_HS_DST		(1 << 10)	/* handshake w/dst */
#define DWC_CFGL_HS_SRC		(1 << 11)	/* handshake w/src */
#define DWC_CFGL_LOCK_CH_XFER	(0 << 12)	/* scope of LOCK_CH */
#define DWC_CFGL_LOCK_CH_BLOCK	(1 << 12)
#define DWC_CFGL_LOCK_CH_XACT	(2 << 12)
#define DWC_CFGL_LOCK_BUS_XFER	(0 << 14)	/* scope of LOCK_BUS */
#define DWC_CFGL_LOCK_BUS_BLOCK	(1 << 14)
#define DWC_CFGL_LOCK_BUS_XACT	(2 << 14)
#define DWC_CFGL_LOCK_CH	(1 << 15)	/* channel lockout */
#define DWC_CFGL_LOCK_BUS	(1 << 16)	/* busmaster lockout */
#define DWC_CFGL_HS_DST_POL	(1 << 18)	/* dst handshake active low */
#define DWC_CFGL_HS_SRC_POL	(1 << 19)	/* src handshake active low */
#define DWC_CFGL_MAX_BURST(x)	((x) << 20)
#define DWC_CFGL_RELOAD_SAR	(1 << 30)
#define DWC_CFGL_RELOAD_DAR	(1 << 31)

/* Bitfields in CFG_HI */
#define DWC_CFGH_FCMODE		(1 << 0)
#define DWC_CFGH_FIFO_MODE	(1 << 1)
#define DWC_CFGH_PROTCTL(x)	((x) << 2)
#define DWC_CFGH_PROTCTL_DATA	(0 << 2)	/* data access - always set */
#define DWC_CFGH_PROTCTL_PRIV	(1 << 2)	/* privileged -> AHB HPROT[1] */
#define DWC_CFGH_PROTCTL_BUFFER	(2 << 2)	/* bufferable -> AHB HPROT[2] */
#define DWC_CFGH_PROTCTL_CACHE	(4 << 2)	/* cacheable  -> AHB HPROT[3] */
#define DWC_CFGH_DS_UPD_EN	(1 << 5)
#define DWC_CFGH_SS_UPD_EN	(1 << 6)
#define DWC_CFGH_SRC_PER(x)	((x) << 7)
#define DWC_CFGH_DST_PER(x)	((x) << 11)

/* Bitfields in SGR */
#define DWC_SGR_SGI(x)		((x) << 0)
#define DWC_SGR_SGC(x)		((x) << 20)

/* Bitfields in DSR */
#define DWC_DSR_DSI(x)		((x) << 0)
#define DWC_DSR_DSC(x)		((x) << 20)

/* Bitfields in CFG */
#define DW_CFG_DMA_EN		(1 << 0)

/* iDMA 32-bit support */

/* bursts size */
enum idma32_msize {
	IDMA32_MSIZE_1,
	IDMA32_MSIZE_2,
	IDMA32_MSIZE_4,
	IDMA32_MSIZE_8,
	IDMA32_MSIZE_16,
	IDMA32_MSIZE_32,
};

/* Bitfields in CTL_HI */
#define IDMA32C_CTLH_BLOCK_TS_MASK	GENMASK(16, 0)
#define IDMA32C_CTLH_BLOCK_TS(x)	((x) & IDMA32C_CTLH_BLOCK_TS_MASK)
#define IDMA32C_CTLH_DONE		(1 << 17)

/* Bitfields in CFG_LO */
#define IDMA32C_CFGL_DST_BURST_ALIGN	(1 << 0)	/* dst burst align */
#define IDMA32C_CFGL_SRC_BURST_ALIGN	(1 << 1)	/* src burst align */
#define IDMA32C_CFGL_CH_DRAIN		(1 << 10)	/* drain FIFO */
#define IDMA32C_CFGL_DST_OPT_BL		(1 << 20)	/* optimize dst burst length */
#define IDMA32C_CFGL_SRC_OPT_BL		(1 << 21)	/* optimize src burst length */

/* Bitfields in CFG_HI */
#define IDMA32C_CFGH_SRC_PER(x)		((x) << 0)
#define IDMA32C_CFGH_DST_PER(x)		((x) << 4)
#define IDMA32C_CFGH_RD_ISSUE_THD(x)	((x) << 8)
#define IDMA32C_CFGH_RW_ISSUE_THD(x)	((x) << 18)
#define IDMA32C_CFGH_SRC_PER_EXT(x)	((x) << 28)	/* src peripheral extension */
#define IDMA32C_CFGH_DST_PER_EXT(x)	((x) << 30)	/* dst peripheral extension */

/* Bitfields in FIFO_PARTITION */
#define IDMA32C_FP_PSIZE_CH0(x)		((x) << 0)
#define IDMA32C_FP_PSIZE_CH1(x)		((x) << 13)
#define IDMA32C_FP_UPDATE		(1 << 26)

enum dw_dmac_flags {
	DW_DMA_IS_CYCLIC = 0,
	DW_DMA_IS_SOFT_LLP = 1,
	DW_DMA_IS_PAUSED = 2,
	DW_DMA_IS_INITIALIZED = 3,
};

/**
 * enum dma_transfer_direction - dma transfer mode and direction indicator
 * @DMA_MEM_TO_MEM: Async/Memcpy mode
 * @DMA_MEM_TO_DEV: Slave mode & From Memory to Device
 * @DMA_DEV_TO_MEM: Slave mode & From Device to Memory
 * @DMA_DEV_TO_DEV: Slave mode & From Device to Device
 */
enum dma_transfer_direction {
	DMA_MEM_TO_MEM,
	DMA_MEM_TO_DEV,
	DMA_DEV_TO_MEM,
	DMA_DEV_TO_DEV,
	DMA_TRANS_NONE,
};

static inline bool is_slave_direction(enum dma_transfer_direction direction)
{
	return (direction == DMA_MEM_TO_DEV) || (direction == DMA_DEV_TO_MEM);
}

struct dw_dma_chan {
	struct dw_dma_chan_regs* ch_regs;
	UINT8				mask;
	UINT8				priority;
	enum dma_transfer_direction	direction;

#if 0
	/* software emulation of the LLP transfers */
	struct list_head* tx_node_active;
#endif

	FAST_MUTEX lock;

	/* these other elements are all protected by lock */
	unsigned long		flags;
#if 0
	struct list_head	active_list;
	struct list_head	queue;
#endif

	unsigned int		descs_allocated;

	/* hardware configuration */
	unsigned int		block_size;
	bool			nollp;
	UINT32			max_burst;

#if 0
	/* custom slave configuration */
	struct dw_dma_slave	dws;

	/* configuration passed via .device_config */
	struct dma_slave_config dma_sconfig;
#endif
};

/**
 * struct dw_dma_platform_data - Controller configuration parameters
 * @nr_masters: Number of AHB masters supported by the controller
 * @nr_channels: Number of channels supported by hardware (max 8)
 * @chan_allocation_order: Allocate channels starting from 0 or 7
 * @chan_priority: Set channel priority increasing from 0 to 7 or 7 to 0.
 * @block_size: Maximum block size supported by the controller
 * @data_width: Maximum data width supported by hardware per AHB master
 *		(in bytes, power of 2)
 * @multi_block: Multi block transfers supported by hardware per channel.
 * @max_burst: Maximum value of burst transaction size supported by hardware
 *	       per channel (in units of CTL.SRC_TR_WIDTH/CTL.DST_TR_WIDTH).
 * @protctl: Protection control signals setting per channel.
 * @quirks: Optional platform quirks.
 */
struct dw_dma_platform_data {
	UINT32		nr_masters;
	UINT32		nr_channels;
#define CHAN_ALLOCATION_ASCENDING	0	/* zero to seven */
#define CHAN_ALLOCATION_DESCENDING	1	/* seven to zero */
	UINT32		chan_allocation_order;
#define CHAN_PRIORITY_ASCENDING		0	/* chan0 highest */
#define CHAN_PRIORITY_DESCENDING	1	/* chan7 highest */
	UINT32		chan_priority;
	UINT32		block_size;
	UINT32		data_width[DW_DMA_MAX_NR_MASTERS];
	UINT32		multi_block[DW_DMA_MAX_NR_CHANNELS];
	UINT32		max_burst[DW_DMA_MAX_NR_CHANNELS];
#define CHAN_PROTCTL_PRIVILEGED		BIT(0)
#define CHAN_PROTCTL_BUFFERABLE		BIT(1)
#define CHAN_PROTCTL_CACHEABLE		BIT(2)
#define CHAN_PROTCTL_MASK		GENMASK(2, 0)
	UINT32		protctl;
#define DW_DMA_QUIRK_XBAR_PRESENT	BIT(0)
	UINT32		quirks;
};

/* LLI == Linked List Item; a.k.a. DMA block descriptor */
struct dw_lli {
	/* values that are not changed by hardware */
	UINT32 sar;
	UINT32 dar;
	UINT32 llp; //chain to next lli
	UINT32 ctllo;
	/* values that may get written back: */
	UINT32 ctlhi;
	/* sstat and dstat can snapshot peripheral register state.
	 * silicon config may discard either or both...
	 */
	UINT32 sstat;
	UINT32 dstat;
};

class DwDMA {
public:
	void* regs;

	DwDMA(void* regs);
	~DwDMA();

	NTSTATUS init();
private:
	struct dw_dma_chan* chan;

	/* channels */
	UINT8			all_chan_mask;

	struct dw_dma_platform_data* pdata;

	void disable();
	void enable();

	UINT32 bytes2block(struct dw_dma_chan* dwc, UINT32 bytes, unsigned int width, UINT32* len);

	UINT32 readl(PVOID reg);
	void writel(UINT32 val, PVOID reg);

public:
	NTSTATUS transfer_dma(UINT32 dest, UINT32 src, UINT32 len);
};
typedef DwDMA* PDwDMA;

static inline struct dw_dma_regs* __dw_regs(DwDMA* dw)
{
	return (struct dw_dma_regs *)dw->regs;
}

static inline struct dw_dma_chan_regs* __dwc_regs(struct dw_dma_chan* dwc)
{
	return dwc->ch_regs;
}

#define dma_readl(dw, name) \
	readl(&(__dw_regs(dw)->name))
#define dma_writel(dw, name, val) \
	writel((val), &(__dw_regs(dw)->name))

#define channel_set_bit(dw, reg, mask) \
	dma_writel(dw, reg, ((mask) << 8) | (mask))
#define channel_clear_bit(dw, reg, mask) \
	dma_writel(dw, reg, ((mask) << 8) | 0)

#define channel_readl(dwc, name) \
	readl(&(__dwc_regs(dwc)->name))
#define channel_writel(dwc, name, val) \
	writel((val), &(__dwc_regs(dwc)->name))