#include "definitions.h"
#include "hw.h"

static void memcpy_io(PVOID dst, PVOID src, size_t sz) {
	for (size_t i = 0; i < sz; i++) {
		((PUINT8)dst)[i] = ((PUINT8)src)[i];
	}
}

void CCsAudioCatptSSTHW::ipc_init() {
	this->ipc_ready = false;
	this->ipc_done = false;
	this->ipc_busy = false;
}

NTSTATUS CCsAudioCatptSSTHW::ipc_arm(struct catpt_fw_ready* config)
{
	/*
	 * Both tx and rx are put into and received from outbox. Inbox is
	 * only used for notifications where payload size is known upfront,
	 * thus no separate buffer is allocated for it.
	 */
	this->ipc_rx.data = ExAllocatePool2(POOL_FLAG_NON_PAGED, config->outbox_size, CSAUDIOCATPTSST_POOLTAG);
	if (!this->ipc_rx.data)
		return STATUS_NO_MEMORY;

	memcpy_io(&ipc_config, config, sizeof(*config));
	this->ipc_ready = true;

	return STATUS_SUCCESS;
}

NTSTATUS CCsAudioCatptSSTHW::ipc_send_msg(struct catpt_ipc_msg request,
	struct catpt_ipc_msg* reply, int timeout) {
	if (!this->ipc_ready) {
		return STATUS_NO_SUCH_DEVICE;
	}

	if (request.size > ipc_config.outbox_size || (reply && reply->size > ipc_config.outbox_size)) {
		return STATUS_BUFFER_OVERFLOW;
	}

	//msg init
	{
		ipc_rx.header = 0;
		ipc_rx.size = reply ? reply->size : 0;

		this->ipc_done = false;
		this->ipc_busy = true;
	}

	dsp_send_tx(&request);

	NTSTATUS status;
	status = ipc_wait_completion(timeout);
	if (!NT_SUCCESS(status)) {
		DPF(D_ERROR, "IPC Failed!!!\n");
		this->ipc_ready = false;
		return status;
	}

	int ret = ipc_rx.rsp.status;
	if (reply) {
		reply->header = ipc_rx.header;
		if (!ret && reply->data) {
			memcpy_io(reply->data, ipc_rx.data, reply->size);
		}
	}

	if (ret) {
		CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "SST returned %d\n", ret);
		return STATUS_INVALID_DEVICE_STATE;
	}
	return status;
}

NTSTATUS CCsAudioCatptSSTHW::ipc_wait_completion(int timeout) {
	LARGE_INTEGER StartTime;
	KeQuerySystemTimePrecise(&StartTime);
	while (!this->ipc_done) {
		LARGE_INTEGER CurrentTime;
		KeQuerySystemTimePrecise(&CurrentTime);

		if (((CurrentTime.QuadPart - StartTime.QuadPart) / (10 * 1000)) >= timeout) {
			DPF(D_ERROR, "Timed out waiting for transmit IPC\n");
			return STATUS_IO_TIMEOUT;
		}

		LARGE_INTEGER Interval;
		Interval.QuadPart = -10 * 1000;
		KeDelayExecutionThread(KernelMode, false, &Interval);
	}

	if (ipc_rx.rsp.status != CATPT_REPLY_PENDING)
		return STATUS_SUCCESS;

	KeQuerySystemTimePrecise(&StartTime);
	while (this->ipc_busy) {
		LARGE_INTEGER CurrentTime;
		KeQuerySystemTimePrecise(&CurrentTime);

		if (((CurrentTime.QuadPart - StartTime.QuadPart) / (10 * 1000)) >= timeout) {
			DPF(D_ERROR, "Timed out waiting for receive IPC\n");
			return STATUS_IO_TIMEOUT;
		}

		LARGE_INTEGER Interval;
		Interval.QuadPart = -10 * 1000;
		KeDelayExecutionThread(KernelMode, false, &Interval);
	}
	return STATUS_SUCCESS;
}

void CCsAudioCatptSSTHW::dsp_send_tx(const struct catpt_ipc_msg* tx) {
	UINT32 header = tx->header | CATPT_IPCC_BUSY;

	memcpy_io(catpt_outbox_addr(this), tx->data, tx->size);

	catpt_writel_shim(this, IPCC, header);
}

void CCsAudioCatptSSTHW::dsp_copy_rx(UINT32 header)
{
	this->ipc_rx.header = header;
	if (this->ipc_rx.rsp.status != CATPT_REPLY_SUCCESS)
		return;

	memcpy_io(this->ipc_rx.data, catpt_outbox_addr(this), this->ipc_rx.size);
}

void CCsAudioCatptSSTHW::dsp_notify_stream(union catpt_notify_msg msg) {
	catpt_stream *stream = catpt_stream_find(msg.stream_hw_id);
	if (!stream) {
		CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "notify %d for non-existent stream %d\n", msg.notify_reason, msg.stream_hw_id);
		return;
	}

	struct catpt_notify_position pos;
	struct catpt_notify_glitch glitch;

	switch (msg.notify_reason) {
	case CATPT_NOTIFY_POSITION_CHANGED:
		memcpy_io(&pos, catpt_inbox_addr(this), sizeof(pos));
		break;

	case CATPT_NOTIFY_GLITCH_OCCURRED:
		memcpy_io(&glitch, catpt_inbox_addr(this), sizeof(glitch));

		CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "glitch %d at pos: 0x%08llx, wp: 0x%08x\n",
			glitch.type, glitch.presentation_pos,
			glitch.write_pos);
		break;

	default:
		CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "unknown notification: %d received\n",
			msg.notify_reason);
		break;
	}
}

void CCsAudioCatptSSTHW::dsp_process_response(UINT32 header)
{
	union catpt_notify_msg msg = CATPT_MSG(header);

	if (msg.fw_ready) {
		struct catpt_fw_ready config;
		/* to fit 32b header original address is shifted right by 3 */
		UINT32 off = msg.mailbox_address << 3;

		memcpy_io(&config, this->lpe_ba + off, sizeof(config));

		ipc_arm(&config);
		this->fw_ready = true;
		return;
	}

	switch (msg.global_msg_type) {
	case CATPT_GLB_REQUEST_CORE_DUMP:
		DPF(D_ERROR, "ADSP device coredump received\n");
		this->ipc_ready = false;
		//catpt_coredump();
		/* TODO: attempt recovery */
		break;

	case CATPT_GLB_STREAM_MESSAGE:
		switch (msg.stream_msg_type) {
		case CATPT_STRM_NOTIFICATION:
			dsp_notify_stream(msg);
			break;
		default:
			dsp_copy_rx(header);
			/* signal completion of delayed reply */
			this->ipc_busy = FALSE;
			break;
		}
		break;

	default:
		CatPtPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "unknown response: %d received\n",
			msg.global_msg_type);
		break;
	}
}

NTSTATUS CCsAudioCatptSSTHW::dsp_irq_handler() {
	NTSTATUS status = STATUS_INVALID_PARAMETER;
	UINT32 isc, ipcc;
	isc = catpt_readl_shim(this, ISC);

	/* immediate reply */
	if (isc & CATPT_ISC_IPCCD) {
		/* mask host DONE interrupt */
		catpt_updatel_shim(this, IMC, CATPT_IMC_IPCCD, CATPT_IMC_IPCCD);

		ipcc = catpt_readl_shim(this, IPCC);
		dsp_copy_rx(ipcc);

		this->ipc_done = true;

		/* tell DSP processing is completed */
		catpt_updatel_shim(this, IPCC, CATPT_IPCC_DONE, 0);
		/* unmask host DONE interrupt */
		catpt_updatel_shim(this, IMC, CATPT_IMC_IPCCD, 0);
		status = STATUS_SUCCESS;
	}

	/* delayed reply or notification */
	if (isc & CATPT_ISC_IPCDB) {
		/* mask dsp BUSY interrupt */
		catpt_updatel_shim(this, IMC, CATPT_IMC_IPCDB, CATPT_IMC_IPCDB);
		
		{ //from thread in linux
			UINT32 ipcd;

			ipcd = catpt_readl_shim(this, IPCD);

			/* ensure there is delayed reply or notification to process */
			if ((ipcd & CATPT_IPCD_BUSY)) {
				dsp_process_response(ipcd);


				/* tell DSP processing is completed */
				catpt_updatel_shim(this, IPCD, CATPT_IPCD_BUSY | CATPT_IPCD_DONE,
					CATPT_IPCD_DONE);
			}

			/* unmask dsp BUSY interrupt */
			catpt_updatel_shim(this, IMC, CATPT_IMC_IPCDB, 0);
		}

		status = STATUS_SUCCESS;
	}

	return status;
}