#include <windows.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "../src/tx.h"

#undef WRITE_REGISTER_ULONG
#define WRITE_REGISTER_ULONG(Register, Value) (*(Register) = (Value))
#define KeMemoryBarrier MemoryBarrier
#define MT7922_RX_QUEUE_TEST
#include "../src/rx-queue.c"

#define FRAME_CAPACITY 4096U
#define DMA_CAPACITY (FRAME_CAPACITY + 160U)
#define SIM_RING_COUNT 8U
#define SIM_DESCRIPTOR_BUS 0x4000U
#define SIM_BUFFER_BUS 0x8000U

typedef struct _SIM_RX_QUEUE {
    ULONG Registers[4];
    DMA_DESCRIPTOR Descriptors[SIM_RING_COUNT];
    uint8_t Buffers[SIM_RING_COUNT * RX_BUFFER_BYTES];
    ULONG Device;
    MT7922_RX_RING Ring;
} SIM_RX_QUEUE;

static void InitializeQueue(SIM_RX_QUEUE *Queue)
{
    memset(Queue, 0, sizeof(*Queue));
    Queue->Ring.Registers = (PUCHAR)Queue->Registers;
    Queue->Ring.Descriptors = Queue->Descriptors;
    Queue->Ring.Buffers = Queue->Buffers;
    Queue->Ring.DescriptorAddress = SIM_DESCRIPTOR_BUS;
    Queue->Ring.BufferAddress = SIM_BUFFER_BUS;
    Queue->Ring.Count = SIM_RING_COUNT;
    Queue->Descriptors[SIM_RING_COUNT - 1].Control = DESCRIPTOR_DONE;
    Mt7922ProgramRxRing(&Queue->Ring);

    assert(Queue->Registers[0] == SIM_DESCRIPTOR_BUS);
    assert(Queue->Registers[1] == SIM_RING_COUNT);
    assert(Queue->Registers[2] == SIM_RING_COUNT - 1);
    assert(Queue->Descriptors[SIM_RING_COUNT - 1].Control == 0);
}

static BOOLEAN CompleteDescriptor(SIM_RX_QUEUE *Queue, const uint8_t *Data,
    size_t DataBytes, ULONG Completion)
{
    ULONG slot = Queue->Device;
    DMA_DESCRIPTOR *descriptor = &Queue->Descriptors[slot];
    if (slot == Queue->Registers[2]) {
        return FALSE;
    }

    assert(DataBytes <= RX_BUFFER_BYTES);
    assert(descriptor->Control == RX_BUFFER_BYTES << DESCRIPTOR_LEN_SHIFT);
    assert(descriptor->FirstBuffer == SIM_BUFFER_BUS + slot * RX_BUFFER_BYTES);
    memcpy(Queue->Buffers + slot * RX_BUFFER_BYTES, Data, DataBytes);
    MemoryBarrier();
    descriptor->Control = Completion | DESCRIPTOR_DONE;
    Queue->Device = (slot + 1) % SIM_RING_COUNT;
    Queue->Registers[3] = Queue->Device;
    return TRUE;
}

static BOOLEAN CompleteFrame(SIM_RX_QUEUE *Queue, const uint8_t *Data, size_t Length)
{
    return CompleteDescriptor(Queue, Data, Length,
        ((ULONG)Length << DESCRIPTOR_LEN_SHIFT) | DESCRIPTOR_LAST0);
}

typedef struct _DESCRIPTOR_LAYOUT {
    uint32_t Groups;
    uint32_t PadWords;
    size_t HeaderBytes;
} DESCRIPTOR_LAYOUT;

static const DESCRIPTOR_LAYOUT layouts[] = {
    { 0, 0, 24 },
    { 1U << 11, 1, 42 },
    { 1U << 12, 2, 36 },
    { 1U << 13, 3, 38 },
    { 1U << 14, 0, 40 },
    { (1U << 13) | (1U << 15), 1, 106 },
    { 0xf800U, 3, 150 },
};

static uint32_t ReadLe32(const uint8_t *Bytes)
{
    return (uint32_t)Bytes[0] | (uint32_t)Bytes[1] << 8
        | (uint32_t)Bytes[2] << 16 | (uint32_t)Bytes[3] << 24;
}

static void WriteLe32(uint8_t *Bytes, uint32_t Value)
{
    Bytes[0] = (uint8_t)Value;
    Bytes[1] = (uint8_t)(Value >> 8);
    Bytes[2] = (uint8_t)(Value >> 16);
    Bytes[3] = (uint8_t)(Value >> 24);
}

static size_t WrapRx(uint8_t *Dma, const uint8_t *Frame, size_t FrameBytes,
    const DESCRIPTOR_LAYOUT *Layout)
{
    size_t used = Layout->HeaderBytes + FrameBytes;
    assert(used <= DMA_CAPACITY);

    memset(Dma, 0xa5, used);
    memset(Dma, 0, 24);
    WriteLe32(Dma, (2U << 27) | (uint32_t)used);
    WriteLe32(Dma + 4, Layout->Groups);
    WriteLe32(Dma + 8, Layout->PadWords << 14);
    memcpy(Dma + Layout->HeaderBytes, Frame, FrameBytes);
    return used;
}

static void CheckRxErrors(void)
{
    uint8_t dma[DMA_CAPACITY];
    const uint8_t frame[2] = { 0xd0, 0 };
    MT7922_RX_FRAME view;
    size_t used = WrapRx(dma, frame, sizeof(frame), &layouts[0]);

    assert(Mt7922DecodeRx(dma, 3, &view) == Mt7922RxMalformed);
    assert(Mt7922DecodeRx(dma, used - 1, &view) == Mt7922RxMalformed);
    assert(Mt7922DecodeRx(dma, used + 1, &view) == Mt7922RxRawFrame);
    assert(view.Length == sizeof(frame));

    WriteLe32(dma, (7U << 27) | (uint32_t)used);
    assert(Mt7922DecodeRx(dma, used, &view) == Mt7922RxIgnored);
    WriteLe32(dma, (2U << 27) | 23U);
    assert(Mt7922DecodeRx(dma, used, &view) == Mt7922RxMalformed);
    WriteLe32(dma, (2U << 27) | 24U);
    assert(Mt7922DecodeRx(dma, used, &view) == Mt7922RxMalformed);
    WriteLe32(dma, (2U << 27) | (uint32_t)used);

    WriteLe32(dma + 4, 1U << 27);
    assert(Mt7922DecodeRx(dma, used, &view) == Mt7922RxIgnored);
    WriteLe32(dma + 4, 1U << 15);
    assert(Mt7922DecodeRx(dma, used, &view) == Mt7922RxMalformed);
    WriteLe32(dma + 4, 0xf800U);
    assert(Mt7922DecodeRx(dma, used, &view) == Mt7922RxMalformed);
    WriteLe32(dma + 4, 0);

    WriteLe32(dma + 8, 1U << 13);
    assert(Mt7922DecodeRx(dma, used, &view) == Mt7922RxIgnored);
    WriteLe32(dma + 8, 1U << 25);
    assert(Mt7922DecodeRx(dma, used, &view) == Mt7922RxRawFrame && view.HeaderTranslationError);
    WriteLe32(dma + 8, (1U << 25) | (1U << 13));
    assert(Mt7922DecodeRx(dma, used, &view) == Mt7922RxIgnored && view.HeaderTranslationError);
    WriteLe32(dma + 8, 1U << 24);
    assert(Mt7922DecodeRx(dma, used, &view) == Mt7922RxIgnored);
    WriteLe32(dma + 8, 3U << 14);
    assert(Mt7922DecodeRx(dma, used, &view) == Mt7922RxMalformed);
}

static void CheckRxPhy(void)
{
    const uint8_t frame[2] = {0xd0, 0};
    const DESCRIPTOR_LAYOUT layout = {(1U << 12) | (1U << 13), 0, 40};
    uint8_t record[DMA_CAPACITY];
    MT7922_RX_FRAME view;
    const size_t used = WrapRx(record, frame, sizeof(frame), &layout);
    WriteLe32(record + 24, 0x12345678);
    WriteLe32(record + 32, (4U << 24) | (2U << 12));
    assert(Mt7922DecodeRx(record, used, &view) == Mt7922RxRawFrame);
    assert(view.PhyPresent && view.PhyMode == 4 && view.PhyWidth == 2);
    assert(view.Ampdu && view.AmpduTimestamp == 0x12345678);

    WriteLe32(record + 8, 1U << 30);
    assert(Mt7922DecodeRx(record, used, &view) == Mt7922RxRawFrame);
    assert(view.PhyPresent && !view.Ampdu);
    assert(Mt7922DecodeRx(record, used - 1, &view) == Mt7922RxMalformed);
    assert(!view.PhyPresent && !view.Ampdu);
}

static void CheckActionErrors(void)
{
    uint8_t frame[45] = {
        0xd0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x7e, 0, 0, 0, 0, 1, 0x00, 0x25, 0x00, 0xff, 0x94, 0x73, 0, 0,
        0x7f, 0, 0x17, 0xf2, 8, 0x10, 3, 0, 0x78, 0x56, 0x34, 0x12,
        4, 3, 2, 1, 0xfd, 2, 0, 0x44, 0x55
    };
    uint8_t ordered[49] = { 0 };
    uint8_t padded[47] = { 0 };
    AWDL_ACTION action;

    assert(AwdlDecodeAction(frame, sizeof(frame), &action) == AwdlMif);
    assert(action.Sender == frame + 10 && action.Elements == frame + 40);
    assert(action.ElementBytes == 5 && action.Version == 0x10);
    assert(action.PhysicalTx == 0x12345678 && action.TargetTx == 0x01020304);
    assert(AwdlDecodeAction(frame, 1, &action) == AwdlMalformed);
    assert(AwdlDecodeAction(frame, 24, &action) == AwdlMalformed);
    assert(AwdlDecodeAction(frame, 28, &action) == AwdlMalformed);
    assert(AwdlDecodeAction(frame, 39, &action) == AwdlMalformed);
    assert(AwdlDecodeAction(frame, 42, &action) == AwdlMalformed);
    assert(AwdlDecodeAction(frame, 44, &action) == AwdlMalformed);

    memcpy(ordered, frame, 24);
    memcpy(ordered + 28, frame + 24, sizeof(frame) - 24);
    ordered[1] = 0x80;
    assert(AwdlDecodeAction(ordered, sizeof(ordered), &action) == AwdlMif);

    memcpy(padded, frame, sizeof(frame));
    assert(AwdlDecodeAction(padded, sizeof(padded), &action) == AwdlMif);
    assert(action.ElementBytes == sizeof(frame) - 40);
    padded[46] = 1;
    assert(AwdlDecodeAction(padded, sizeof(padded), &action) == AwdlMalformed);

    frame[1] = 0x40;
    assert(AwdlDecodeAction(frame, sizeof(frame), &action) == AwdlOther);
    frame[1] = 4;
    assert(AwdlDecodeAction(frame, sizeof(frame), &action) == AwdlOther);
    frame[1] = 0;
    frame[22] = 1;
    assert(AwdlDecodeAction(frame, sizeof(frame), &action) == AwdlOther);
    frame[22] = 0;
    frame[16] ^= 1;
    assert(AwdlDecodeAction(frame, sizeof(frame), &action) == AwdlOther);
    frame[16] ^= 1;
    frame[27] ^= 1;
    assert(AwdlDecodeAction(frame, sizeof(frame), &action) == AwdlOther);
    frame[27] ^= 1;
    frame[28] = 7;
    assert(AwdlDecodeAction(frame, sizeof(frame), &action) == AwdlOther);
    frame[28] = 8;
    frame[30] = 255;
    assert(AwdlDecodeAction(frame, sizeof(frame), &action) == AwdlOther);
}

static void CheckChannelSequence(void)
{
    uint8_t bytes[81] = {
        0xd0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x7e, 0, 0, 0, 0, 1, 0x00, 0x25, 0x00, 0xff, 0x94, 0x73, 0, 0,
        0x7f, 0, 0x17, 0xf2, 8, 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        18, 38, 0, 15, 3, 0, 3, 0xff, 0xff, 44, 128
    };
    AWDL_ACTION action;

    assert(AwdlDecodeAction(bytes, sizeof(bytes), &action) == AwdlPsf);
    assert(action.Sequence.Count == 16 && action.Sequence.Channel[0] == 44);
    assert(action.Sequence.OperatingClass[0] == 128);
    assert(action.Sequence.Channel[15] == 0 && action.Sync.Master == NULL);

    bytes[44] = 1;
    bytes[49] = 0x24;
    bytes[50] = 149;
    assert(AwdlDecodeAction(bytes, sizeof(bytes), &action) == AwdlPsf);
    assert(action.Sequence.Channel[0] == 149 && action.Sequence.OperatingClass[0] == 0);

    bytes[41] = 22;
    bytes[44] = 0;
    bytes[49] = 6;
    assert(AwdlDecodeAction(bytes, 65, &action) == AwdlPsf);
    assert(action.Sequence.Channel[0] == 6 && action.Sequence.OperatingClass[0] == 0);
    assert(AwdlDecodeAction(bytes, 64, &action) == AwdlMalformed);
    bytes[44] = 2;
    assert(AwdlDecodeAction(bytes, 65, &action) == AwdlMalformed);
    bytes[44] = 0;
    bytes[43] = 14;
    assert(AwdlDecodeAction(bytes, 65, &action) == AwdlMalformed);

    bytes[40] = 4;
    assert(AwdlDecodeAction(bytes, 65, &action) == AwdlMalformed);
}

/* The pinned classic-PCAP fixtures have little-endian headers and Radiotap link type. */
static size_t ReadRadiotap(const uint8_t *Packet, size_t Length, uint8_t *Flags)
{
    size_t headerBytes, offset;
    uint32_t first, present;

    assert(Length >= 8 && Packet[0] == 0);
    headerBytes = (size_t)Packet[2] | (size_t)Packet[3] << 8;
    assert(headerBytes >= 8 && headerBytes <= Length);

    first = present = ReadLe32(Packet + 4);
    offset = 8;
    while (present & 0x80000000U) {
        assert(offset + 4 <= headerBytes);
        present = ReadLe32(Packet + offset);
        offset += 4;
    }
    *Flags = 0;
    if (first & 2U) {
        if (first & 1U)
            offset = ((offset + 7U) & ~(size_t)7U) + 8U;
        assert(offset < headerBytes);
        *Flags = Packet[offset];
    }

    return headerBytes;
}

/* Expected digests fold the same numeric fields exported by TShark for each
   frame, including the final 16 channel numbers and operating classes. */
static uint32_t FoldSchedule(uint32_t Digest, unsigned FrameNumber,
    AWDL_ACTION_KIND Kind, const AWDL_ACTION *Action)
{
    const uint32_t fields[] = {
        FrameNumber, Kind == AwdlPsf ? 0U : 3U, Action->Sync.NextChannel,
        Action->Sync.WindowTu, Action->Sync.UntilNextSlotTu,
        Action->Sync.NextWindowSequence, Action->Sync.WindowsPerSlot,
        Action->Sequence.Encoding
    };

    assert(Action->Sync.Master != NULL && Action->Sequence.Count == AWDL_CHANNEL_SLOTS);
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
        Digest = (Digest ^ fields[i]) * 16777619U;
    for (size_t i = 0; i < AWDL_CHANNEL_SLOTS; ++i)
        Digest = (Digest ^ Action->Sequence.Channel[i]) * 16777619U;
    for (size_t i = 0; i < AWDL_CHANNEL_SLOTS; ++i)
        Digest = (Digest ^ Action->Sequence.OperatingClass[i]) * 16777619U;
    return Digest;
}

typedef struct _VERIFY_ACTION {
    const AWDL_ACTION *Expected;
    AWDL_ACTION_KIND Kind;
    unsigned FrameNumber;
    unsigned Calls;
} VERIFY_ACTION;

static void VerifyAction(void *Context, const AWDL_ACTION *Replay)
{
    VERIFY_ACTION *verify = Context;
    const AWDL_ACTION *original = verify->Expected;
    assert(verify->Kind == AwdlPsf || verify->Kind == AwdlMif);
    assert(memcmp(Replay->Sender, original->Sender, 6) == 0);
    assert(Replay->Version == original->Version);
    assert(Replay->PhysicalTx == original->PhysicalTx);
    assert(Replay->TargetTx == original->TargetTx);
    assert(Replay->ElementBytes == original->ElementBytes);
    assert(FoldSchedule(0, verify->FrameNumber, verify->Kind, Replay)
        == FoldSchedule(0, verify->FrameNumber, verify->Kind, original));
    assert(memcmp(Replay->Elements, original->Elements, Replay->ElementBytes) == 0);
    verify->Calls++;
}

static void CountAction(void *Context, const AWDL_ACTION *Action)
{
    unsigned *calls = Context;
    assert(Action->Version == 0x10);
    (*calls)++;
}

static void CheckQueueBoundaries(void)
{
    const uint8_t frame[40] = {
        0xd0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x7e, 0, 0, 0, 0, 1, 0x00, 0x25, 0x00, 0xff, 0x94, 0x73, 0, 0,
        0x7f, 0, 0x17, 0xf2, 8, 0x10, 3, 0
    };
    SIM_RX_QUEUE queue;
    uint8_t data[DMA_CAPACITY];
    unsigned calls = 0;
    size_t used = WrapRx(data, frame, sizeof(frame), &layouts[0]);
    ULONG length = (ULONG)used << DESCRIPTOR_LEN_SHIFT;
    InitializeQueue(&queue);

    assert(Mt7922PollAwdlRx(&queue.Ring, CountAction, &calls) == Mt7922AwdlRxEmpty);
    for (unsigned i = 0; i < SIM_RING_COUNT - 1; i++)
        assert(CompleteFrame(&queue, data, used));
    assert(!CompleteFrame(&queue, data, used));

    for (unsigned i = 0; i < SIM_RING_COUNT - 1; i++)
        assert(Mt7922PollAwdlRx(&queue.Ring, CountAction, &calls) == Mt7922AwdlRxMif);
    assert(calls == SIM_RING_COUNT - 1);
    assert(Mt7922PollAwdlRx(&queue.Ring, CountAction, &calls) == Mt7922AwdlRxEmpty);

    /* The first part lands in the final slot. The continuation wraps to zero
       after a poll that sees no completed descriptor. */
    assert(CompleteDescriptor(&queue, data, used, length));
    assert(Mt7922PollAwdlRx(&queue.Ring, CountAction, &calls) == Mt7922AwdlRxSkipped);
    assert(queue.Ring.Consumer == 0);
    assert(Mt7922PollAwdlRx(&queue.Ring, CountAction, &calls) == Mt7922AwdlRxEmpty);
    assert(CompleteFrame(&queue, data, used));
    assert(Mt7922PollAwdlRx(&queue.Ring, CountAction, &calls) == Mt7922AwdlRxSkipped);

    assert(CompleteDescriptor(&queue, data, used, length | DESCRIPTOR_LAST0 | (1U << 14)));
    assert(Mt7922PollAwdlRx(&queue.Ring, CountAction, &calls) == Mt7922AwdlRxSkipped);
    assert(CompleteDescriptor(&queue, data, used, DESCRIPTOR_LAST0));
    assert(Mt7922PollAwdlRx(&queue.Ring, CountAction, &calls) == Mt7922AwdlRxInvalid);
    assert(CompleteDescriptor(&queue, data, used,
        ((RX_BUFFER_BYTES + 1) << DESCRIPTOR_LEN_SHIFT) | DESCRIPTOR_LAST0));
    assert(Mt7922PollAwdlRx(&queue.Ring, CountAction, &calls) == Mt7922AwdlRxInvalid);

    assert(CompleteFrame(&queue, data, used));
    assert(Mt7922PollAwdlRx(&queue.Ring, CountAction, &calls) == Mt7922AwdlRxMif);
    assert(calls == SIM_RING_COUNT);
    assert(Mt7922PollAwdlRx(&queue.Ring, CountAction, &calls) == Mt7922AwdlRxEmpty);
}

static void CheckPureUploadAck(void)
{
    uint8_t ip[73] = {0x60};
    ip[5] = 32;
    ip[6] = 6;
    ip[24] = 0xfe;
    ip[40] = 8771 >> 8;
    ip[41] = (uint8_t)(8771 & 0xff);
    ip[52] = 0x80; /* TCP header with timestamp options. */
    ip[53] = 0x10;
    assert(AwdlIsPureTcpAck(ip, 72, 8771));
    assert(!AwdlIsPureTcpAck(ip, 72, 8770));
    assert(!AwdlIsPureTcpAck(ip, 73, 8771));
    assert(!AwdlIsPureTcpAck(ip, 59, 8771));
    for (uint8_t flag = 1; flag <= 8; flag <<= 1) {
        ip[53] = (uint8_t)(0x10 | flag);
        assert(!AwdlIsPureTcpAck(ip, 72, 8771));
    }
    ip[53] = 0x10;
    ip[24] = 0xff;
    assert(!AwdlIsPureTcpAck(ip, 72, 8771));
}

static void CheckTransmitFormat(void)
{
    uint8_t frame[40] = {
        0xd0, 0, 0, 0, 0x02, 0, 0, 0, 0, 2,
        0x02, 0, 0, 0, 0, 1, 0x00, 0x25, 0x00, 0xff, 0x94, 0x73, 0x30, 0x12,
        0x7f, 0, 0x17, 0xf2, 8, 0x10, 0, 0
    };
    uint8_t txwi[MT7922_TXWI_BYTES];
    MT7922_AWDL_TX request = {0x01234560, 0x102, 0x17, 0x43, 2, 1, MT7922_OFDM_6_MBPS};

    assert(Mt7922BuildAwdlTxwi(txwi, &request, frame, sizeof(frame)));
    const uint32_t expected[] = {
        0x02000048, 0x82026102, 0x8000200d, 0x91237800,
        0, 0x00000443, 0x004b0004, 0x000d0000,
        0x00008017, 0, 0x01234560, 0x00008028, 0, 0, 0, 0
    };
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        assert(ReadLe32(txwi + i * 4U) == expected[i]);
    }
    frame[4] = 0xff;
    assert(Mt7922BuildAwdlTxwi(txwi, &request, frame, sizeof(frame)));
    assert(ReadLe32(txwi + 8) == 0x8000240d);
    assert(ReadLe32(txwi + 12) == 0x91237801);

    frame[27] ^= 1;
    assert(!Mt7922BuildAwdlTxwi(txwi, &request, frame, sizeof(frame)));
    frame[27] ^= 1;
    assert(!Mt7922BuildAwdlTxwi(txwi, &request, frame, 39));
    request.FrameAddress = 0xfffffff0U;
    assert(!Mt7922BuildAwdlTxwi(txwi, &request, frame, sizeof(frame)));
    request.FrameAddress = 0x01234560;
    request.PacketId = 2;
    assert(!Mt7922BuildAwdlTxwi(txwi, &request, frame, sizeof(frame)));

    request.PacketId = 0x43;
    frame[4] = 0x02;
    frame[0] = 0x08;
    assert(Mt7922BuildAwdlTxwi(txwi, &request, frame, sizeof(frame)));
    assert(ReadLe32(txwi + 8) == 0x80002020);
    assert(ReadLe32(txwi + 28) == 0x00200000);
    frame[0] = 0x88;
    frame[24] = 5;
    assert(Mt7922BuildAwdlTxwi(txwi, &request, frame, sizeof(frame)));
    assert(ReadLe32(txwi + 4) == 0x82526902);
    assert(ReadLe32(txwi + 8) == 0x80002028);
    assert(ReadLe32(txwi + 28) == 0x00280000);

    request.RateCode = MT7922_OFDM_12_MBPS;
    assert(Mt7922BuildAwdlTxwi(txwi, &request, frame, sizeof(frame)));
    assert(ReadLe32(txwi + 24) == 0x004a0004);
    request.RateCode = MT7922_OFDM_24_MBPS;
    assert(Mt7922BuildAwdlTxwi(txwi, &request, frame, sizeof(frame)));
    assert(ReadLe32(txwi + 24) == 0x00490004);
    request.RateCode = MT7922_OFDM_54_MBPS;
    assert(Mt7922BuildAwdlTxwi(txwi, &request, frame, sizeof(frame)));
    assert(ReadLe32(txwi + 24) == 0x004c0004);
    request.RateCode = 0;
    assert(!Mt7922BuildAwdlTxwi(txwi, &request, frame, sizeof(frame)));
}

static void CheckConnac3TransmitFormat(void)
{
    uint8_t frame[40] = {
        0xd0, 0, 0, 0, 0x02, 0, 0, 0, 0, 2,
        0x02, 0, 0, 0, 0, 1, 0x00, 0x25, 0x00, 0xff, 0x94, 0x73, 0x30, 0x12,
        0x7f, 0, 0x17, 0xf2, 8, 0x10, 0, 0
    };
    uint8_t txwi[MT7922_TXWI_BYTES];
    MT7922_AWDL_TX request = {
        0x01234560, 0x102, 0x17, 0x43, 2, 1, MT7922_OFDM_6_MBPS, 0, 0, 0x7925
    };

    assert(Mt7922BuildAwdlTxwi(txwi, &request, frame, sizeof(frame)));
    const uint32_t expected[] = {
        0x20000048, 0x840c8102, 0x0000000d, 0x91237800,
        0, 0x00000443, 0x00120014, 0,
        0x00008017, 0, 0x01234560, 0x00008028, 0, 0, 0, 0
    };
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        assert(ReadLe32(txwi + i * 4U) == expected[i]);
    }

    frame[4] = 0xff;
    assert(Mt7922BuildAwdlTxwi(txwi, &request, frame, sizeof(frame)));
    assert(ReadLe32(txwi + 12) == 0x91237811);

    frame[4] = 0x02;
    frame[0] = 0x88;
    frame[24] = 5;
    request.RateCode = MT7922_OFDM_12_MBPS;
    assert(Mt7922BuildAwdlTxwi(txwi, &request, frame, sizeof(frame)));
    assert(ReadLe32(txwi + 4) == 0x84ad8102);
    assert(ReadLe32(txwi + 8) == 0x00000028);
    assert(ReadLe32(txwi + 24) == 0x00140014);

    request.RateCode = 0;
    request.Ampdu = 1;
    assert(Mt7922BuildAwdlTxwi(txwi, &request, frame, sizeof(frame)));
    assert(ReadLe32(txwi + 4) == 0x04ad8102);
    assert(ReadLe32(txwi + 12) == 0x81237800);
    assert(ReadLe32(txwi + 24) == 0x00000014);

    request.Ampdu = 0;
    request.RateCode = 0x1234;
    assert(!Mt7922BuildAwdlTxwi(txwi, &request, frame, sizeof(frame)));
    request.RateCode = MT7922_OFDM_6_MBPS;
    request.ChipId = 0x7927;
    assert(!Mt7922BuildAwdlTxwi(txwi, &request, frame, sizeof(frame)));
}

static void CheckTransmitReports(void)
{
    uint8_t data[72] = {0};
    MT7922_AWDL_TX request = {0x01234560, 0x102, 0x17, 0x43, 2, 1, MT7922_OFDM_6_MBPS};
    MT7922_TX_REPORT report;
    MT7922_TX_RELEASE release;

    WriteLe32(data, sizeof(data));
    WriteLe32(data + 8, 0x4b);
    WriteLe32(data + 16, 0x01030000);
    WriteLe32(data + 20, 0x43000000);
    WriteLe32(data + 40, 0x804b);
    WriteLe32(data + 44, 0x12300000);
    WriteLe32(data + 48, 0x01020000);
    WriteLe32(data + 52, 0x43000000);
    assert(Mt7922ReadTxReport(data, sizeof(data), &request, &report) == Mt7922TxReportMatched);
    assert(report.RateCode == 0x4b && report.Sequence == 0x123 && report.AckErrors == 0);
    WriteLe32(data + 40, 0x0001804b);
    assert(Mt7922ReadTxReport(data, sizeof(data), &request, &report) == Mt7922TxReportMatched);
    assert(report.AckErrors == 1);
    WriteLe32(data + 40, 0x0100804b);
    assert(Mt7922ReadTxReport(data, sizeof(data), &request, &report) == Mt7922TxReportUnmatched);
    assert(Mt7922ReadTxReport(data, sizeof(data) - 1, &request, &report) == Mt7922TxReportMalformed);

    WriteLe32(data, 0x30020014);
    WriteLe32(data + 8, 0x80408000);
    WriteLe32(data + 12, 0x00180001);
    WriteLe32(data + 16, 0x00174002);
    assert(Mt7922ReadTxReport(data, 20, &request, &report) == Mt7922TxReportUnmatched);
    assert(Mt7922ReadTxRelease(data, 20, request.Token, &release) == Mt7922TxReportMatched);
    assert(release.Attempts == 2 && release.Status == 2);
    assert(Mt7922ReadTxRelease(data, 16, request.Token, &release) == Mt7922TxReportMalformed);
    WriteLe32(data + 12, 0x00170001);
    assert(Mt7922ReadTxRelease(data, 20, request.Token, &release) == Mt7922TxReportMalformed);
}

static void ReplayCapture(const wchar_t *Path, unsigned ExpectedFrames,
    unsigned ExpectedPsf, unsigned ExpectedMif, uint32_t ExpectedSchedule)
{
    FILE *file = NULL;
    uint8_t global[24], record[16], packet[FRAME_CAPACITY], dma[DMA_CAPACITY];
    unsigned frames = 0, psf = 0, mif = 0, probeFrames = 0, electedFrames = 0;
    uint32_t schedule = 2166136261U;
    size_t read;
    SIM_RX_QUEUE queue;
    InitializeQueue(&queue);

    assert(_wfopen_s(&file, Path, L"rb") == 0 && file != NULL);
    assert(fread(global, 1, sizeof(global), file) == sizeof(global));
    assert(ReadLe32(global) == 0xa1b2c3d4U && ReadLe32(global + 20) == 127U);

    while ((read = fread(record, 1, sizeof(record), file)) != 0) {
        uint32_t captureBytes = ReadLe32(record + 8);
        uint8_t flags;
        size_t start, frameBytes;
        AWDL_ACTION original;
        AWDL_ACTION_KIND expected;

        assert(read == sizeof(record));
        assert(captureBytes > 0 && captureBytes <= sizeof(packet));
        assert(captureBytes == ReadLe32(record + 12));
        assert(fread(packet, 1, captureBytes, file) == captureBytes);
        frames++;

        start = ReadRadiotap(packet, captureBytes, &flags);
        if (flags & 0x40)
            continue;
        frameBytes = captureBytes - start;
        if (flags & 0x10) {
            assert(frameBytes >= 4);
            frameBytes -= 4;
        }

        expected = AwdlDecodeAction(packet + start, frameBytes, &original);
        assert(expected != AwdlMalformed);
        if (expected == AwdlPsf)
            psf++;
        if (expected == AwdlMif)
            mif++;
        if (expected == AwdlPsf || expected == AwdlMif) {
            schedule = FoldSchedule(schedule, frames, expected, &original);
            const MT7922_AWDL_TX request = {SIM_BUFFER_BUS, 0, 3, 3, 0, 1, MT7922_OFDM_6_MBPS};
            uint8_t txwi[MT7922_TXWI_BYTES];
            assert(Mt7922BuildAwdlTxwi(txwi, &request, packet + start, frameBytes));
            assert(ReadLe32(txwi) == (0x02000000U | (uint32_t)(frameBytes + 32U)));
            assert(ReadLe32(txwi + 24) == 0x004b0004);
            assert(ReadLe32(txwi + 40) == SIM_BUFFER_BUS);
            assert(ReadLe32(txwi + 44) == (0x8000U | (uint32_t)frameBytes));
            if (original.DeviceClass == 2 && original.Sync.UntilNextSlotTu > 0 &&
                original.Sync.UntilNextSlotTu <= 64) {
                const uint8_t local[] = {0x02, 0, 0, 0, 0, 1};
                uint8_t probe[MT7922_PROBE_PSF_CAPACITY];
                size_t probeBytes = AwdlBuildProbePsf(probe, sizeof(probe), local,
                    &original, original.PhysicalTx, 6);
                AWDL_ACTION decoded;
                assert(probeBytes == 273);
                assert(AwdlDecodeAction(probe, probeBytes, &decoded) == AwdlPsf);
                static const uint8_t broadcast[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
                assert(memcmp(probe + 4, broadcast, 6) == 0);
                assert(memcmp(decoded.Sender, local, 6) == 0);
                assert(decoded.Sync.UntilNextSlotTu == original.Sync.UntilNextSlotTu);
                assert(decoded.Sequence.Channel[15] == 6 && decoded.DeviceClass == 0);
                if (original.Election.Present && original.Election.MasterMetric > 60 &&
                    original.Election.MasterCounter != 0 && original.Sync.MasterChannel != 0) {
                    const size_t advertBytes = AwdlBuildAdvert(probe, sizeof(probe), local,
                        &original, original.PhysicalTx, 1, 1, 6);
                    assert(AwdlDecodeAction(probe, advertBytes, &decoded) == AwdlMif);
                    assert(decoded.Election.Present);
                    assert(decoded.Election.MasterCounter == original.Election.MasterCounter);
                    assert(decoded.Election.MasterMetric == original.Election.MasterMetric);
                    assert(decoded.Election.Distance == original.Election.Distance + 1);
                    assert(decoded.Sync.MasterChannel == original.Sync.MasterChannel);
                    ++electedFrames;
                }
                ++probeFrames;
            }
        }

        for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++) {
            VERIFY_ACTION verify = { &original, expected, frames, 0 };
            MT7922_RX_FRAME view;
            MT7922_AWDL_RX_STATUS status;
            size_t used = WrapRx(dma, packet + start, frameBytes, &layouts[i]);

            assert(Mt7922DecodeRx(dma, used, &view) == Mt7922RxRawFrame);
            assert(view.Length == frameBytes);
            assert(memcmp(view.Bytes, packet + start, frameBytes) == 0);
            assert(CompleteFrame(&queue, dma, used));
            status = Mt7922PollAwdlRx(&queue.Ring, VerifyAction, &verify);
            switch (expected) {
            case AwdlPsf:
                assert(status == Mt7922AwdlRxPsf && verify.Calls == 1);
                break;
            case AwdlMif:
                assert(status == Mt7922AwdlRxMif && verify.Calls == 1);
                break;
            default:
                assert(status == Mt7922AwdlRxSkipped && verify.Calls == 0);
                break;
            }
            assert(Mt7922PollAwdlRx(&queue.Ring, VerifyAction, &verify) == Mt7922AwdlRxEmpty);
        }
    }

    assert(!ferror(file) && feof(file));
    fclose(file);
    assert(frames == ExpectedFrames && psf == ExpectedPsf && mif == ExpectedMif);
    assert(schedule == ExpectedSchedule && probeFrames > 0 && electedFrames > 0);
    const wchar_t *name = wcsrchr(Path, L'\\');
    wprintf(L"%ls\n  frames=%u PSF=%u MIF=%u queue-replays=%zu schedule=%08x probe-frames=%u\n",
        name != NULL ? name + 1 : Path, frames, psf, mif,
        frames * (sizeof(layouts) / sizeof(layouts[0])), schedule, probeFrames);
}

static void CheckPeerPhyCapabilities(void)
{
    const uint8_t psf[] = {
        0xd0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x7e, 0, 0, 0, 0, 1, 0, 0x25, 0, 0xff, 0x94, 0x73, 0, 0,
        0x7f, 0, 0x17, 0xf2, 8, 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        7, 9, 0, 0, 0, 0x6f, 0, 0x1f, 0xff, 0xff, 0, 0,
        17, 14, 0, 191, 12, 0x32, 0, 0x80, 3, 0xfa, 0xff, 0, 0, 0xfa, 0xff, 0, 0
    };
    AWDL_ACTION action;
    assert(AwdlDecodeAction(psf, sizeof(psf), &action) == AwdlPsf);
    assert(action.Phy.HtPresent && action.Phy.HtCapabilities == 0x006f && action.Phy.HtAmpdu == 0x1f);
    assert(action.Phy.HtRxMcs[0] == 0xff && action.Phy.HtRxMcs[1] == 0xff);
    assert(action.Phy.VhtPresent && action.Phy.VhtCapabilities == 0x03800032);
    assert(action.Phy.VhtRxMcs == 0xfffa && action.Phy.VhtTxMcs == 0xfffa);
}

static void CheckTransmitAmsdu(void)
{
    const uint8_t dest[6] = {2, 3, 4, 5, 6, 7};
    const uint8_t source[6] = {8, 9, 10, 11, 12, 13};
    const uint8_t first[1500] = {0x60, 1};
    const uint8_t second[1500] = {0x60, 2};
    const uint8_t third[1500] = {0x60, 3};
    uint8_t frame[AWDL_TX_AMSDU3_FRAME_BYTES];
    uint8_t txwi[MT7922_TXWI_BYTES];
    const MT7922_AWDL_TX tx = {
        .FrameAddress = 0x8000, .Wcid = 2, .Token = 128, .PacketId = 191, .Queue = 1, .Ampdu = 1
    };

    assert(AwdlBuildAddbaRequest(frame, sizeof(frame), dest, source, 1, 64, 0, 32, 1) == 33);
    assert(frame[27] == 3 && frame[28] == 16);
    assert(AwdlBuildQosAmsduFrame(frame, AWDL_TX_AMSDU_FRAME_BYTES - 1, dest, source,
        first, sizeof(first), second, sizeof(second), NULL, 0, 0xfffe) == 0);
    size_t bytes = AwdlBuildQosAmsduFrame(frame, sizeof(frame), dest, source,
        first, sizeof(first), second, sizeof(second), NULL, 0, 0xfffe);
    assert(bytes == AWDL_TX_AMSDU_FRAME_BYTES);
    assert(frame[0] == 0x88 && frame[24] == 0x80);
    const uint8_t *a = frame + 26;
    const uint8_t *b = a + 1532;
    assert(memcmp(a, dest, 6) == 0 && memcmp(a + 6, source, 6) == 0);
    assert(memcmp(b, dest, 6) == 0 && memcmp(b + 6, source, 6) == 0);
    assert(a[12] == 5 && a[13] == 0xec && b[12] == 5 && b[13] == 0xec);
    assert(a[14] == 0xaa && b[14] == 0xaa);
    assert(a[24] == 0xfe && a[25] == 0xff && b[24] == 0xff && b[25] == 0xff);
    assert(memcmp(a + 30, first, sizeof(first)) == 0);
    assert(memcmp(b + 30, second, sizeof(second)) == 0);
    assert(a[1530] == 0 && a[1531] == 0);
    assert(Mt7922BuildAwdlTxwi(txwi, &tx, frame, bytes));
    assert(txwi[44] == 0x10 && txwi[45] == 0x8c);
    assert(txwi[46] == 0 && txwi[47] == 0);

    assert(AwdlBuildQosAmsduFrame(frame, sizeof(frame) - 1, dest, source,
        first, sizeof(first), second, sizeof(second), third, sizeof(third), 0xfffe) == 0);
    bytes = AwdlBuildQosAmsduFrame(frame, sizeof(frame), dest, source,
        first, sizeof(first), second, sizeof(second), third, sizeof(third), 0xfffe);
    assert(bytes == AWDL_TX_AMSDU3_FRAME_BYTES);
    const uint8_t *c = b + 1532;
    assert(b[1530] == 0 && b[1531] == 0 && c[24] == 0 && c[25] == 0);
    assert(memcmp(c + 30, third, sizeof(third)) == 0);
    assert(Mt7922BuildAwdlTxwi(txwi, &tx, frame, bytes));
    assert(txwi[0] == 0x2c && txwi[1] == 0x12);
    assert(txwi[44] == 0xc0 && txwi[45] == 0x0f);
    assert(txwi[46] == 0x4c && txwi[47] == 0x82);
    assert(txwi[48] == 0xc0 && txwi[49] == 0x8f && txwi[50] == 0 && txwi[51] == 0);
    frame[24] = 0;
    assert(!Mt7922BuildAwdlTxwi(txwi, &tx, frame, bytes));
}

int wmain(int argc, wchar_t **argv)
{
    if (argc != 4) {
        fwprintf(stderr, L"Usage: rx-test discovery.pcap receive.pcap send.pcap\n");
        return 2;
    }

    CheckRxErrors();
    CheckRxPhy();
    CheckActionErrors();
    CheckChannelSequence();
    CheckPeerPhyCapabilities();
    CheckQueueBoundaries();
    CheckPureUploadAck();
    CheckTransmitFormat();
    CheckConnac3TransmitFormat();
    CheckTransmitAmsdu();
    CheckTransmitReports();
    wprintf(L"RX queue, TX descriptors, ACK reports and TX token release checks passed.\n");
    ReplayCapture(argv[1], 1229, 599, 620, 0xd4535223U);
    ReplayCapture(argv[2], 5413, 679, 820, 0x3644fa04U);
    ReplayCapture(argv[3], 2731, 690, 1485, 0x07695990U);
    return 0;
}
