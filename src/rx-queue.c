#ifndef MT7922_RX_QUEUE_TEST
#include <ntddk.h>
#endif
#include "rx-queue.h"

#define RX_RING_BASE 0
#define RX_RING_COUNT 4
#define RX_RING_CPU_INDEX 8

C_ASSERT(sizeof(DMA_DESCRIPTOR) == 16);

static void ArmReceiveBuffer(MT7922_RX_RING *Ring, ULONG Index)
{
    DMA_DESCRIPTOR *descriptor = &Ring->Descriptors[Index];

    descriptor->FirstBuffer = Ring->BufferAddress + Index * RX_BUFFER_BYTES;
    descriptor->SecondBuffer = 0;
    descriptor->Info = 0;
    descriptor->Control = RX_BUFFER_BYTES << DESCRIPTOR_LEN_SHIFT;
}

static void PublishProducer(const MT7922_RX_RING *Ring)
{
    KeMemoryBarrier();
    WRITE_REGISTER_ULONG((PULONG)(Ring->Registers + RX_RING_CPU_INDEX), Ring->Producer);
}

void Mt7922ProgramRxRing(MT7922_RX_RING *Ring)
{
    Ring->Consumer = 0;
    Ring->Producer = Ring->Count - 1;
    Ring->DiscardContinuation = FALSE;
    for (ULONG index = 0; index < Ring->Producer; ++index) {
        ArmReceiveBuffer(Ring, index);
    }
    /* Clear a stale completion in the slot reserved for the next refill. */
    Ring->Descriptors[Ring->Producer].Control = 0;

    KeMemoryBarrier();
    WRITE_REGISTER_ULONG((PULONG)(Ring->Registers + RX_RING_BASE), Ring->DescriptorAddress);
    WRITE_REGISTER_ULONG((PULONG)(Ring->Registers + RX_RING_COUNT), Ring->Count);
    PublishProducer(Ring);
}

BOOLEAN Mt7922PeekRxRing(const MT7922_RX_RING *Ring, const UCHAR **Data, ULONG *Control)
{
    ULONG control = Ring->Descriptors[Ring->Consumer].Control;
    if ((control & DESCRIPTOR_DONE) == 0) {
        return FALSE;
    }

    KeMemoryBarrier();
    *Data = Ring->Buffers + Ring->Consumer * RX_BUFFER_BYTES;
    *Control = control;
    return TRUE;
}

void Mt7922RecycleRxRing(MT7922_RX_RING *Ring)
{
    Ring->Consumer = (Ring->Consumer + 1) % Ring->Count;
    ArmReceiveBuffer(Ring, Ring->Producer);
    Ring->Producer = (Ring->Producer + 1) % Ring->Count;
    PublishProducer(Ring);
}

MT7922_AWDL_RX_STATUS Mt7922PollAwdlRx(MT7922_RX_RING *Ring,
    MT7922_AWDL_VISITOR Visitor, void *Context)
{
    const UCHAR *data;
    ULONG control;
    MT7922_AWDL_RX_STATUS result = Mt7922AwdlRxSkipped;

    if (!Mt7922PeekRxRing(Ring, &data, &control)) {
        return Mt7922AwdlRxEmpty;
    }

    const BOOLEAN last = (control & DESCRIPTOR_LAST0) != 0;
    const ULONG length = (control & DESCRIPTOR_LEN0) >> DESCRIPTOR_LEN_SHIFT;
    /* Drain the entire chain before trying to decode another standalone frame. */
    if (!Ring->DiscardContinuation && last
        && (control & (DESCRIPTOR_DROP | DESCRIPTOR_TO_HOST_A)) == 0) {
        MT7922_RX_FRAME frame = {0};
        MT7922_RX_STATUS status = length > RX_BUFFER_BYTES
            ? Mt7922RxMalformed : Mt792xDecodeRx(Ring->ChipId, data, length, &frame);
        if (status == Mt7922RxMalformed) {
            result = Mt7922AwdlRxInvalid;
        } else if (status == Mt7922RxRawFrame) {
            AWDL_ACTION action;
            AWDL_ACTION_KIND kind = AwdlDecodeAction(frame.Bytes, frame.Length, &action);
            if (kind == AwdlMalformed) {
                result = Mt7922AwdlRxInvalid;
            } else if (kind == AwdlPsf || kind == AwdlMif) {
                result = kind == AwdlPsf ? Mt7922AwdlRxPsf : Mt7922AwdlRxMif;
                Visitor(Context, &action);
            }
        }
    }

    Ring->DiscardContinuation = !last;
    Mt7922RecycleRxRing(Ring);
    return result;
}
