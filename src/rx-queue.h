#pragma once

#include "rx.h"

#define RX_BUFFER_BYTES 2048U
#define DESCRIPTOR_DONE 0x80000000U
#define DESCRIPTOR_LAST0 0x40000000U
#define DESCRIPTOR_LEN0 0x3fff0000U
#define DESCRIPTOR_LEN_SHIFT 16U
#define DESCRIPTOR_DROP (1U << 14)
#define DESCRIPTOR_TO_HOST_A (1U << 12)

typedef struct _DMA_DESCRIPTOR {
    ULONG FirstBuffer;
    volatile ULONG Control;
    ULONG SecondBuffer;
    ULONG Info;
} DMA_DESCRIPTOR;

typedef struct _MT7922_RX_RING {
    PUCHAR Registers;
    DMA_DESCRIPTOR *Descriptors;
    PUCHAR Buffers;
    ULONG DescriptorAddress;
    ULONG BufferAddress;
    ULONG Count;
    ULONG Consumer;
    ULONG Producer;
    USHORT ChipId;
    BOOLEAN DiscardContinuation;
} MT7922_RX_RING;

typedef enum _MT7922_AWDL_RX_STATUS {
    Mt7922AwdlRxEmpty,
    Mt7922AwdlRxSkipped,
    Mt7922AwdlRxInvalid,
    Mt7922AwdlRxPsf,
    Mt7922AwdlRxMif
} MT7922_AWDL_RX_STATUS;

/* Called synchronously before the DMA buffer is recycled. Copy any required bytes. */
typedef void (*MT7922_AWDL_VISITOR)(void *Context, const AWDL_ACTION *Action);

/* The caller supplies disjoint DMA memory, Count >= 2, and serializes access.
   Program the ring while DMA is disabled. */
void Mt7922ProgramRxRing(MT7922_RX_RING *Ring);
BOOLEAN Mt7922PeekRxRing(const MT7922_RX_RING *Ring, const UCHAR **Data, ULONG *Control);
void Mt7922RecycleRxRing(MT7922_RX_RING *Ring);
MT7922_AWDL_RX_STATUS Mt7922PollAwdlRx(MT7922_RX_RING *Ring,
    MT7922_AWDL_VISITOR Visitor, void *Context);
