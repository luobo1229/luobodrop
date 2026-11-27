#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <winioctl.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../src/station-scan.h"

void VerifyWificxScan(void);
void VerifyWificxNetRings(void);

#define MT7922_MCU_TEST
#ifndef MAXULONG
#define MAXULONG 0xffffffffUL
#endif
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif
#define KernelMode 0
typedef LARGE_INTEGER PHYSICAL_ADDRESS;
#ifndef NT_SUCCESS
#define NT_SUCCESS(status) ((NTSTATUS)(status) >= 0)
#endif

typedef struct _MOCK_POLL_TIMER {
    LONGLONG DueTime;
    BOOLEAN Allocated;
    BOOLEAN Armed;
} *PEX_TIMER;
#define EX_TIMER_HIGH_RESOLUTION 1U
#define Executive 0
static struct _MOCK_POLL_TIMER mockPollTimer;

typedef void *WDFDMAENABLER;
typedef void *WDFCOMMONBUFFER;
#define WDF_NO_OBJECT_ATTRIBUTES NULL

static ULONG FakeRead(PULONG Register);
static void FakeWrite(PULONG Register, ULONG Value);
static ULONGLONG FakeTime(void);
static ULONGLONG FakePreciseTime(ULONGLONG *Counter);
static NTSTATUS FakeDelay(int Mode, BOOLEAN Alertable, PLARGE_INTEGER Interval);
static PEX_TIMER FakeAllocateTimer(void *Callback, void *Context, ULONG Attributes);
static BOOLEAN FakeSetTimer(PEX_TIMER Timer, LONGLONG DueTime, LONGLONG Period, void *Parameters);
static NTSTATUS FakeWaitOnTimer(PEX_TIMER Timer, int Reason, int Mode, BOOLEAN Alertable, PLARGE_INTEGER Timeout);
static BOOLEAN FakeDeleteTimer(PEX_TIMER Timer, BOOLEAN Cancel, BOOLEAN Wait, void *Parameters);
static NTSTATUS FakeAllocate(WDFDMAENABLER Enabler, SIZE_T Bytes, PVOID Attr,
    WDFCOMMONBUFFER *Buffer);
static PHYSICAL_ADDRESS FakeBusAddress(WDFCOMMONBUFFER Buffer);
static SIZE_T FakeLength(WDFCOMMONBUFFER Buffer);
static void FakeDelete(WDFCOMMONBUFFER Buffer);

#undef READ_REGISTER_ULONG
#undef WRITE_REGISTER_ULONG
#define READ_REGISTER_ULONG FakeRead
#define WRITE_REGISTER_ULONG FakeWrite
#define KeMemoryBarrier MemoryBarrier
#define KeQueryInterruptTime FakeTime
#define KeQueryInterruptTimePrecise FakePreciseTime
#define KeDelayExecutionThread FakeDelay
#define POOL_FLAG_NON_PAGED 0
#define ExAllocatePool2(Flags, Bytes, Tag) calloc(1, (Bytes))
#define ExFreePoolWithTag(Buffer, Tag) free(Buffer)
#define ExAllocateTimer FakeAllocateTimer
#define ExSetTimer FakeSetTimer
#define KeWaitForSingleObject FakeWaitOnTimer
#define ExDeleteTimer FakeDeleteTimer
#define WdfCommonBufferCreate FakeAllocate
#define WdfCommonBufferGetAlignedVirtualAddress(buffer) (buffer)
#define WdfCommonBufferGetAlignedLogicalAddress FakeBusAddress
#define WdfCommonBufferGetLength FakeLength
#define WdfObjectDelete FakeDelete

#define MT7922_RX_QUEUE_TEST
#include "../src/rx-queue.c"
#include "../src/mcu.c"

#define MOCK_BUS_BASE 0x03000000
#define REGISTER_BYTES 0x100000

typedef enum _SCENARIO {
    NORMAL, PATCH_PRESENT, STALE_RING, LIVE_DMA, LOCKED_RING, RESET_IGNORED,
    SCHEDULER_WRITE_IGNORED, TOP_OWN_TIMEOUT, DOORBELL_IGNORED,
    TX_TIMEOUT, RX_TIMEOUT, RELEASE_FAILED, STUCK_DMA,
    FULL_BOOT, BOOT_PATCH_PRESENT, BOOT_SCATTER_TIMEOUT, BOOT_NOT_READY, BOOT_CORRUPT,
    BOOT_FIXED_READY, BOOT_ROM_REJECT_START, BOOT_ROM_REJECT_PATCH, BOOT_ROM_SHORT,
    BOOT_CAP_ROM_RING, BOOT_CAP_TIMEOUT, BOOT_CAP_MALFORMED, BOOT_CAP_NO_PHY,
    RECEIVE_AWDL, RECEIVE_EMPTY, RECEIVE_REJECTED,
    TRANSMIT_ACK, TRANSMIT_NO_ACK, TRANSMIT_NO_REPORT, TRANSMIT_NO_PEER, TRANSMIT_RETRY,
} SCENARIO;

static struct {
    PUCHAR Registers;
    PUCHAR CommonBuffer;
    SIZE_T BufferBytes;
    SCENARIO Scenario;
    ULONGLONG Counter;
    ULONGLONG NextProbeFrameTime;
    ULONG SelectedBank;
    ULONG ResetValue;
    ULONG FirmwareState;
    ULONG RxNext;
    ULONG RuntimeRxNext;
    ULONG DataRxNext;
    UCHAR ListenChannel;
    UCHAR TransmitChannel;
    USHORT TxRateCode;
    BOOLEAN Wide80;
    BOOLEAN AdvertiseVht;
    BOOLEAN Watching;
    BOOLEAN PublishService;
    BOOLEAN Bridge;
    ULONG LinkSubmitted;
    ULONG LinkReceived;
    ULONG PingBeforeLink;
    BOOLEAN LinkReady;
    BOOLEAN SnifferEnabled;
    BOOLEAN StationSnifferEnabled;
    ULONG StationSnifferCommands;
    UCHAR EepromHwType;
    ULONG EepromReads;
    UCHAR StationCountry[2];
    ULONG StationClcLength;
    UCHAR StationClcCountry[2];
    UCHAR StationClcType[2];
    UCHAR StationClcFirst;
    UCHAR LegacyReply[32];
    ULONG LegacyReplyBytes;
    BOOLEAN InjectedOnChannel;
    ULONG Commands;
    ULONG DmaEnables;
    ULONG DmaResets;
    ULONG ResetCount;
    const UCHAR *FactoryImage;
    SIZE_T ImageBytes;
    ULONG Region;
    ULONG RegionRemaining;
    ULONG ImageOffset;
    ULONG ScatterPackets;
    BOOLEAN PatchFinished;
    BOOLEAN DmaWasEnabled;
    BOOLEAN TransmitAwdl;
    BOOLEAN StationMode;
    BOOLEAN RejectStationRlm;
    BOOLEAN RejectStationPse;
    uint8_t StationRadioBatches;
    uint8_t StationChannelSwitches;
    BOOLEAN RejectPort;
    BOOLEAN RejectStationAuth;
    BOOLEAN RejectStationAssoc;
    BOOLEAN RejectStationKey;
    ULONG StationKeyCommands;
    ULONG StationPortSecureCommands;
    ULONG StationKeysRemoved;
    ULONG StationPeersReset;
    ULONG StationDeauthSent;
    bool StationAssociationPending;
    ULONG StationPeerCommands;
    ULONG StationGroupCommands;
    BOOLEAN OmitStationRelease;
    BOOLEAN DelayStationReleaseToGroupKey;
    ULONG StationMgmtSent;
    ULONG StationSharedAdverts;
    ULONG StationSharedEchoes;
    ULONG StationSharedExtraEchoes;
    bool OmitSharedUnicastReport;
    bool HoldSharedRelease;
    bool QueueSharedSynAck;
    ULONG SharedSynAcks;
    ULONG StationSharedServiceAdverts;
    ULONG StationSharedAnnouncements;
    bool StationSharedQueueAnnouncement;
    bool StationSharedSlot;
    bool CombineStationAndSharedRelease;
    ULONG StationEapolSent;
    ULONG ProtectedStationEapolSent;
    ULONG StationArpSent;
    USHORT HeldStationToken;
    ULONG StationBssWrites;
    ULONG StationScans;
    ULONG ActionFrames;
    ULONG SyncPsfSeen;
    ULONG SyncMifSeen;
    ULONG ServiceMifSeen;
    ULONG DataSeen;
    ULONG MgmtSeen;
    ULONG PingSeen;
    ULONG TcpSynSeen;
    ULONG TcpResetSeen;
    ULONG TcpSequence;
    ULONG MdnsSeen;
    BOOLEAN ReceivingLink;
    BOOLEAN LateDiscovery;
    BOOLEAN RecoveryEchoReceived;
    ULONG RecoveryEchoSeen;
    ULONG ReceiverAnnouncementsSeen;
    ULONG DirectMdnsSeen;
    ULONGLONG FirstSyncAt;
    ULONG AddbaSeen;
    ULONG RxBaWtbl;
    ULONG RxBaSta;
    BOOLEAN AddbaInjected;
    BOOLEAN DelayedProbe;
    ULONG HeldUnicastOffset;
    ULONG BroadcastDuringUnicast;
    ULONGLONG HeldUnicastAt;
    UCHAR HeldUnicast[512];
} sim;

static ULONG Offset(PULONG registerAddress)
{
    SIZE_T offset = (SIZE_T)((PUCHAR)registerAddress - sim.Registers);
    assert(offset < REGISTER_BYTES && (offset & 3) == 0);
    return (ULONG)offset;
}

static ULONG ReadStored(ULONG offset)
{
    return *(PULONG)(sim.Registers + offset);
}

static void Store(ULONG offset, ULONG value)
{
    *(PULONG)(sim.Registers + offset) = value;
}

static ULONG FakeRead(PULONG registerAddress)
{
    ULONG offset = Offset(registerAddress);
    if (offset == HIF_REMAP) return sim.SelectedBank;
    if (offset == HIF_WINDOW + WFSYS_RESET && sim.SelectedBank == WFSYS_BANK) {
        return sim.ResetValue;
    }
    if (offset == HIF_WINDOW + CONNECTION_FIRMWARE && sim.SelectedBank == CONNECTION_BANK) {
        if (sim.Scenario == BOOT_FIXED_READY && sim.FirmwareState == 3)
            return 0;
        return sim.FirmwareState;
    }
    if (offset == 0xe00f0) {
        return sim.FirmwareState;
    }
    return ReadStored(offset);
}

static void PutUint32(PUCHAR bytes, ULONG value)
{
    memcpy(bytes, &value, sizeof(value));
}

static ULONG WriteCapabilityTlv(PUCHAR buffer, ULONG Tag, const void *Value, ULONG Length)
{
    PutUint32(buffer, Tag);
    PutUint32(buffer + 4, Length);
    memcpy(buffer + 8, Value, Length);
    return 8 + Length;
}

static ULONG WriteCapabilities(PUCHAR response)
{
    static const UCHAR unknown[] = {0x12, 0x34, 0x56};
    static const UCHAR phy[] = {1, 1, 1, 3, 2, 0, 0, 0, 0, 0, 3, 1};
    const UCHAR sixGhz = 1;
    ULONG length = 40;
    response[36] = 4;
    length += WriteCapabilityTlv(response + length, 0x2000, unknown, sizeof(unknown));
    if (sim.Scenario == BOOT_CAP_NO_PHY) {
        response[36] = 3;
    } else {
        length += WriteCapabilityTlv(response + length, 0x08, phy, sizeof(phy));
    }
    static const UCHAR address[] = {0x02, 0x44, 0x33, 0x22, 0x11, 0x01};
    length += WriteCapabilityTlv(response + length, 0x07, address, sizeof(address));
    length += WriteCapabilityTlv(response + length, 0x18, &sixGhz, sizeof(sixGhz));
    if (sim.Scenario == BOOT_CAP_MALFORMED) {
        PutUint32(response + length - 5, 0xfffffff0);
    }
    return length;
}

static void PostMcuEvent(ULONG command, UCHAR sequence, ULONG status)
{
    BOOLEAN unified = (command & 0x10000U) != 0;
    BOOLEAN runtime = (command == 0x8a && sim.Scenario != BOOT_CAP_ROM_RING)
        || (command & 0xff) == 0xed || unified;
    ULONG reg = runtime ? RX_RUNTIME_RING : RX_MCU_RING;
    ULONG offset = runtime ? RX_RUNTIME_OFFSET : RX_RING_OFFSET;
    ULONG payload = runtime ? RX_RUNTIME_PAYLOAD : RX_PAYLOAD_OFFSET;
    ULONG *next = runtime ? &sim.RuntimeRxNext : &sim.RxNext;

    assert(ReadStored(reg + RING_BASE) == MOCK_BUS_BASE + offset);
    assert(ReadStored(reg + RING_COUNT) == RX_DESCRIPTOR_COUNT);
    DMA_DESCRIPTOR *rx = (DMA_DESCRIPTOR *)(sim.CommonBuffer + offset);
    ULONG slot = *next;
    assert(slot != ReadStored(reg + RING_CPU));
    assert(rx[slot].FirstBuffer == MOCK_BUS_BASE + payload + slot * RX_BUFFER_BYTES);
    assert(rx[slot].Control == RX_BUFFER_BYTES << 16);
    PUCHAR response = sim.CommonBuffer + payload + slot * RX_BUFFER_BYTES;
    ZeroMemory(response, RX_BUFFER_BYTES);
    response[28] = command == 0x10 ? 4 : (command & 0xff) == 0xed ? 0xed : 1;
    response[29] = sequence;
    response[32] = (UCHAR)status;
    ULONG bytes = command == 0x8a ? WriteCapabilities(response) : 36;
    if (unified) {
        response[30] = 2;
        response[36] = (UCHAR)command;
        PutUint32(response + 40, status);
        bytes = 44;
    }
    if (command == 0xc0) {
        PutUint32(response + 36, sim.RejectStationPse ? 0 : 0x820c8000U);
        bytes = 44;
    }
    if (command == 0x02 && sim.Scenario == BOOT_ROM_SHORT) {
        bytes = 32;
    }
    if (sim.LegacyReplyBytes != 0) {
        assert(bytes == 36 && sim.LegacyReplyBytes <= sizeof(sim.LegacyReply));
        memcpy(response + bytes, sim.LegacyReply, sim.LegacyReplyBytes);
        bytes += sim.LegacyReplyBytes;
        sim.LegacyReplyBytes = 0;
    }
    rx[slot].Control = 0x80000000UL | (bytes << 16);
    *next = (slot + 1) % RX_DESCRIPTOR_COUNT;
    Store(reg + RING_DEVICE, *next);
}

static void PostRuntimeStationRelease(USHORT Token)
{
    const ULONG slot = sim.RuntimeRxNext;
    DMA_DESCRIPTOR *rx = (DMA_DESCRIPTOR *)(sim.CommonBuffer + RX_RUNTIME_OFFSET);
    assert(slot != ReadStored(RX_RUNTIME_RING + RING_CPU));
    assert(rx[slot].FirstBuffer == MOCK_BUS_BASE + RX_RUNTIME_PAYLOAD + slot * RX_BUFFER_BYTES);
    PUCHAR record = sim.CommonBuffer + RX_RUNTIME_PAYLOAD + slot * RX_BUFFER_BYTES;
    ZeroMemory(record, 12);
    PutUint32(record, 0x3001000c);
    PutUint32(record + 8, (ULONG)Token << 16 | 1U);
    rx[slot].Control = (12U << 16) | 0xc0000000U;
    sim.RuntimeRxNext = (slot + 1) % RX_DESCRIPTOR_COUNT;
    Store(RX_RUNTIME_RING + RING_DEVICE, sim.RuntimeRxNext);
}

static UCHAR RespondToCommand(const MCU_COMMAND *packet, ULONG length)
{
    static const ULONG targets[] = {
        0x00900000, 0x0091b000, 0x02015c00, 0x00404400, 0x0210a000, 0xe0200000,
    };
    const ULONG factorySizes[] = {137472, 341904, 290768, 15312, 8144, 96080};
    const ULONG referenceSizes[] = {137472, 341904, 292816, 15312, 8144, 97232};
    const ULONG *sizes = sim.ImageBytes == MT7922_REFERENCE_IMAGE_BYTES ? referenceSizes : factorySizes;
    assert(sim.RegionRemaining == 0);

    switch (packet->Command) {
    case 0xed:
        assert((sim.Scenario == RECEIVE_AWDL || sim.Scenario == RECEIVE_EMPTY ||
            sim.Scenario == RECEIVE_REJECTED || sim.TransmitAwdl || sim.StationMode) && length == 68);
        assert(packet->ExtensionAck == 1);
        if (packet->Extension == 0x01) {
            /* EFUSE_ACCESS query: 16-byte block; HW type lives at 0x55b. */
            assert(sim.StationMode && packet->SetQuery == 0 && packet->Arguments[0] == 0x550);
            ++sim.EepromReads;
            const ULONG address = 0x550;
            memcpy(sim.LegacyReply, &address, 4);
            sim.LegacyReply[4] = 1;
            memset(sim.LegacyReply + 8, 0, 16);
            sim.LegacyReply[8 + 0x0b] = sim.EepromHwType;
            sim.LegacyReplyBytes = 24;
            return 0;
        }
        if (sim.Commands == 11) {
            assert(packet->Extension == 0x21 && packet->Arguments[0] == 0x100);
        } else {
            assert(sim.Commands == 12 && packet->Extension == 0x46 && packet->Arguments[0] == 1);
        }
        return 0;
    case 0x10:
        assert(length == 68);
        if (packet->Arguments[0] == 1) {
            assert(sim.Commands == 0);
            if (sim.Scenario == PATCH_PRESENT || sim.Scenario == BOOT_PATCH_PRESENT) {
                sim.Region = 1;
                sim.ImageOffset = 137472;
                return 1;
            }
            return 2;
        }
        assert(packet->Arguments[0] == 0 && sim.Commands > 0);
        assert(sim.FactoryImage == NULL || sim.PatchFinished || sim.Scenario == BOOT_ROM_REJECT_PATCH);
        return sim.Scenario == RELEASE_FAILED ? 0 : 3;
    case 0x05:
    case 0x01:
        assert(sim.FactoryImage != NULL && length == 76 && sim.Region < RTL_NUMBER_OF(targets));
        assert(packet->Command == (sim.Region == 0 ? 0x05 : 0x01));
        assert(packet->Arguments[0] == targets[sim.Region]);
        assert(packet->Arguments[1] == sizes[sim.Region]);
        assert(packet->Arguments[2] == 0x80000009);
        if (sim.Scenario == BOOT_ROM_REJECT_PATCH) {
            assert(packet->Command == 0x05);
            return 1;
        }
        sim.RegionRemaining = sizes[sim.Region++];
        return 0;
    case 0x07:
        assert(length == 68 && packet->Arguments[0] == 0 && sim.ImageOffset == 137472);
        sim.PatchFinished = TRUE;
        return 0;
    case 0x02:
        assert(length == 72 && packet->Arguments[0] == 1 && packet->Arguments[1] == 0x0091b000);
        assert(sim.Region == RTL_NUMBER_OF(targets) && sim.ImageOffset == sim.ImageBytes);
        if (sim.Scenario == BOOT_ROM_REJECT_START) {
            return 3;
        }
        if (sim.Scenario != BOOT_NOT_READY && sim.Scenario != BOOT_ROM_SHORT) {
            sim.FirmwareState = 3;
        }
        return 0;
    case 0x8a:
        assert(length == 64 && sim.FirmwareState == 3);
        return 0;
    default:
        assert(!"Unexpected command ID");
        return 0;
    }
}

static ULONG RespondToStationRec(const MCU_UNIFIED_COMMAND *Packet, ULONG Length)
{
    static const UCHAR ap[] = {2, 0x21, 0x22, 0x23, 0x24, 0x25};
    const UCHAR *data = Packet->Payload;
    assert(data[0] == 0 && data[4] == 1 && data[6] == 0);
    if (data[8] == 0x11) {
        assert(Length == 48 + MT7922_CCMP_COMMAND_BYTES && data[2] == 1);
        const bool group = data[1] == 19;
        assert(data[1] == (group ? 19 : 1) && data[5] == (group ? 14 : 0));
        if (data[12] == 1) {
            assert(data[10] == 8);
            for (unsigned i = 13; i < MT7922_CCMP_COMMAND_BYTES; ++i) {
                assert(data[i] == 0);
            }
            ++sim.StationKeysRemoved;
            return 0;
        }
        assert(data[10] == 44 && data[12] == 0 && data[13] == 1 && data[16] == 5 &&
            data[17] == 36 && data[18] == (group ? 2 : 0) && data[19] == 16);
        for (unsigned i = 20; i < MT7922_CCMP_COMMAND_BYTES; ++i) {
            assert(data[i] == (i < 36 ? (group ? 0x62 : 0x31) : 0));
        }
        ++sim.StationKeyCommands;
        if (group && sim.DelayStationReleaseToGroupKey) {
            assert(sim.HeldStationToken != 0);
            PostRuntimeStationRelease(sim.HeldStationToken);
            sim.DelayStationReleaseToGroupKey = FALSE;
            sim.HeldStationToken = 0;
        }
        return sim.RejectStationKey ? STATUS_NOT_SUPPORTED : 0;
    }
    if (Length == 76) {
        assert(data[1] == 1 && data[2] == 1 && data[5] == 0);
        assert(data[8] == 0 && data[10] == 20 && ReadLittleEndian32(data + 12) == 0x00010002);
        assert(data[16] == 2 && data[17] == 0 && data[18] == 7 && data[26] == 1);
        assert(memcmp(data + 20, ap, sizeof(ap)) == 0 && sim.StationKeyCommands == 2);
        ++sim.StationPortSecureCommands;
        return 0;
    }
    if (Length == 88 || Length == 68) {
        const bool group = Length == 68;
        const unsigned wtbl = group ? 8 : 28;
        assert(data[1] == (group ? 19 : 1) && data[2] == (group ? 1 : 2) &&
            data[5] == (group ? 14 : 0));
        if (!group) {
            assert(data[8] == 0 && data[10] == 20 && data[16] == 0 && data[18] == 7);
            assert(memcmp(data + 20, ap, sizeof(ap)) == 0 && data[26] == 1);
        }
        assert(data[wtbl] == 13 && data[wtbl + 2] == 12 &&
            data[wtbl + 4] == data[1] && data[wtbl + 5] == 1 && data[wtbl + 6] == 0);
        ++sim.StationPeersReset;
        return 0;
    }
    if (data[8] == 13) {
        assert(Length == 108 && data[1] == 19 && data[2] == 1 && data[5] == 14 && data[10] == 52);
        assert(data[12] == 19 && data[13] == 1 && data[14] == 3);
        assert(memcmp(data + 24, ap, sizeof(ap)) == 0);
        assert(data[30] == 14 && data[56] == 1 && data[57] == 0 && data[58] == 1);
        ++sim.StationGroupCommands;
        return 0;
    }
    assert(Length == 168 && data[1] == 1 && data[2] == 5 && data[5] == 0);
    assert(data[8] == 0 && data[10] == 20 && ReadLittleEndian32(data + 12) == 0x00010002);
    assert(data[16] == 1 && data[17] == 0 && memcmp(data + 20, ap, sizeof(ap)) == 0);
    assert((data[18] == 0 && data[26] == 3 && data[64] == 0) ||
        (data[18] == 7 && data[26] == 1 && data[64] == 2));
    assert(data[105] == 1 && data[106] == 1 && data[107] == 1);
    assert(data[116] == 1 && data[117] == 0 && data[118] == 1);
    ++sim.StationPeerCommands;
    return 0;
}

static ULONG RespondToStaRec(const MCU_UNIFIED_COMMAND *Packet, ULONG Length)
{
    static const UCHAR phone[] = {0x7e, 0, 0, 0, 0, 1};
    const UCHAR *data = Packet->Payload;
    assert(Packet->Option == 7);
    assert(Packet->Length == Length - 32 && Packet->PacketId == 0xa0);
    if (Length == 96 || Length == 68) {
        assert(data[0] == 0 && data[1] == 1 && data[2] == 1 && data[3] == 0);
        assert(data[4] == 1 && data[5] == 0 && data[6] == 0 && data[7] == 0);
        if (Length == 96) {
            assert(data[8] == 13 && data[10] == 40);
            assert(data[12] == 1 && data[13] == 2 && data[14] == 1);
            assert(data[20] == 8 && data[22] == 28 && data[24] == 0 && data[25] == 2);
            assert(data[32] == 64 && data[33] == 0 && memcmp(data + 34, phone, 6) == 0);
            assert(data[40] == 0 && data[41] == 0 && data[42] == 1);
            ++sim.RxBaWtbl;
            return 0;
        }
        assert(sim.RxBaWtbl == 1 && data[8] == 6 && data[10] == 12);
        assert(data[12] == 0 && data[13] == 2 && data[14] == 0 && data[15] == 1);
        assert(data[16] == 0x23 && data[17] == 0x01 && data[18] == 64 && data[19] == 0);
        ++sim.RxBaSta;
        return sim.Scenario == TRANSMIT_RETRY && !sim.Bridge ? 0xc00000bbUL : 0;
    }
    assert(Length == 168);
    assert(data[0] == 0 && data[1] == 1 && data[2] == 5 && data[3] == 0);
    assert(data[4] == 1 && data[5] == 0 && data[6] == 0);
    /* STA_REC_BASIC: INFRA_STA, PORT_SECURE, newly added. */
    assert(data[8] == 0 && data[10] == 20 && data[11] == 0);
    assert(data[12] == 1 && data[13] == 0 && data[14] == 1 && data[15] == 0);
    assert(data[16] == 2 && data[17] == 1 && data[18] == 1 && data[19] == 0);
    assert(memcmp(data + 20, phone, 6) == 0);
    assert(data[26] == 3 && data[27] == 0);
    /* STA_REC_RA: legacy rate set. */
    assert(data[28] == 1 && data[30] == 16 && data[31] == 0);
    assert(data[32] == (sim.TransmitChannel > 14 ? 0xc0 : 0xcf) && data[33] == 0x3f);
    /* STA_REC_PHY: HR/DSSS and ERP. */
    assert(data[44] == 0x15 && data[46] == 12 && data[47] == 0);
    if (sim.TransmitChannel > 14) {
        assert(data[48] == 0x40 && data[49] == 0x05 && data[50] == 8);
    } else {
        assert(data[48] == 0xff && data[49] == 0x0f && data[50] == 3);
    }
    /* STA_REC_STATE: associated. */
    assert(data[56] == 7 && data[58] == 12 && data[59] == 0 && data[64] == 2);
    /* STA_REC_WTBL: reset-and-set with generic, rx and header translation. */
    assert(data[68] == 13 && data[70] == 52 && data[71] == 0);
    assert(data[72] == 1 && data[73] == 1 && data[74] == 3 &&
        data[75] == 0 && data[76] == 0);
    assert(data[80] == 0 && data[82] == 20 && data[83] == 0);
    assert(memcmp(data + 84, phone, 6) == 0);
    assert(data[90] == 0 && data[93] == 1 && data[96] == 1);
    assert(data[100] == 1 && data[102] == 12);
    assert(data[106] == 1 && data[107] == 1);
    assert(data[112] == 6 && data[114] == 8);
    assert(data[117] == 1 && data[118] == 1);
    return 0;
}

static ULONG RespondToSniffer(const MCU_UNIFIED_COMMAND *Packet, ULONG Length)
{
    const UCHAR *data = Packet->Payload;
    assert(Packet->Option == 7);
    assert(Packet->Length == Length - 32 && Packet->PacketId == 0xa0);
    if (sim.StationMode) {
        /* The station observer never leaves the associated 20 MHz channel. */
        assert(Packet->Command == 0x24);
        ++sim.StationSnifferCommands;
        if (Length == 60) {
            assert(data[0] == 0 && data[4] == 0 && data[6] == 8 && data[9] == 0);
            sim.StationSnifferEnabled = data[8] != 0;
            return 0;
        }
        assert(Length == 68 && sim.StationSnifferEnabled);
        assert(data[0] == 0 && data[4] == 1 && data[6] == 16 && data[8] == 0 && data[9] == 0);
        assert(data[10] == 2 && data[11] == 0 && data[12] == 149 && data[13] == 0);
        assert(data[14] == 149 && data[15] == 0 && data[16] == 1);
        return 0;
    }
    if (Packet->Command == 1) {
        static const UCHAR local[] = {0x02, 0x44, 0x33, 0x22, 0x11, 0x01};
        assert(sim.TransmitAwdl && sim.Commands == 13 && Length == 64);
        assert(data[0] == 0 && data[4] == 0 && data[6] == 12 && data[8] == 1);
        assert(memcmp(data + 10, local, sizeof(local)) == 0);
        return 0;
    }
    static const UCHAR station[] = {0x02, 0x44, 0x33, 0x22, 0x11, 0x01};
    if (Packet->Command == 2) {
        if (Length == 84) {
            assert(sim.TransmitAwdl && sim.Commands == 14);
            assert(data[0] == 0 && data[4] == 0 && data[6] == 32 && data[8] == 1);
            assert(data[12] == 2 && data[14] == 1 && data[16] == 1);
            assert(memcmp(data + 18, station, 6) == 0);
            return 0;
        }
        assert(sim.TransmitAwdl && sim.Commands == 15 && Length == 68);
        assert(data[0] == 0 && data[4] == 2 && data[6] == 16);
        assert(data[8] == sim.TransmitChannel && data[9] == (sim.Wide80 ? 155 : sim.TransmitChannel));
        assert(data[10] == 0 && data[11] == (sim.Wide80 ? 2 : 0));
        assert(data[12] == 2 && data[13] == 3 && data[14] == 1);
        assert(data[15] == (sim.Wide80 ? 4 : 0) && data[16] == (sim.Wide80 ? 1 : 0));
        assert(data[17] == (sim.TransmitChannel > 14 ? 1 : 0));
        return 0;
    }
    assert(Packet->Command == 0x24);
    const ULONG enableIndex = sim.TransmitAwdl ? 16U : 13U;
    if (Length == 60) {
        assert(data[0] == 0 && data[4] == 0 && data[6] == 8);
        assert(data[8] == (sim.Commands == enableIndex));
        sim.SnifferEnabled = data[8] != 0;
        return 0;
    }

    assert(Length == 68 && sim.SnifferEnabled);
    assert(data[0] == 0 && data[4] == 1 && data[6] == 16);
    static const UCHAR surveyChannels[] = {6, 44, 149};
    const ULONG channelIndex = sim.Commands - enableIndex - 1;
    assert(channelIndex < RTL_NUMBER_OF(surveyChannels));
    const UCHAR channel = sim.Watching ? 149 : sim.TransmitAwdl ? sim.TransmitChannel : surveyChannels[channelIndex];
    assert(data[10] == (channel == 6 ? 1 : 2) && data[11] == (sim.Wide80 ? 1 : 0));
    assert(data[12] == channel && data[14] == (sim.Wide80 ? 155 : channel));
    assert(data[13] == (sim.Wide80 ? 1 : 0) && data[15] == 0 && data[16] == 1);
    if (sim.Scenario == RECEIVE_REJECTED || (channel == 149 && sim.Scenario == RECEIVE_EMPTY)) {
        return 0xc00000bb;
    }
    sim.ListenChannel = data[12];
    sim.InjectedOnChannel = FALSE;
    return 0;
}

static ULONG RespondToStationPort(const MCU_UNIFIED_COMMAND *Packet, ULONG Length)
{
    const UCHAR *data = Packet->Payload;
    assert(Packet->Option == 7 && Packet->Length == Length - 32);
    if (Packet->Command == MCU_DEV_INFO) {
        const UCHAR device[] = {0, 0, 0, 0, 0, 0, 12, 0, 1, 0, 2, 0x44, 0x33, 0x22, 0x11, 1};
        assert(Length == 48 + sizeof(device));
        assert(memcmp(data, device, sizeof(device)) == 0);
        return 0;
    }

    assert(Packet->Command == MCU_BSS_INFO);
    ++sim.StationBssWrites;
    const UCHAR idle[] = {
        [6] = 32, [8] = 1, [12] = 1, [14] = 1, [16] = 1, [24] = 19, [30] = 19,
        [35] = 0
    };
    const UCHAR ap[] = {
        [6] = 32, [8] = 1, [12] = 1, [14] = 1, [16] = sim.StationAssociationPending ? 0 : 1,
        [18] = 2, 0x21, 0x22,
        0x23, 0x24, 0x25, 19, 0, 100, 0, 2, 1, 19, 0, 8, 0, 0, 0, 15, 0, 8, 0, sim.StationAssociationPending ? 0 : 1,
        [43] = 0
    };
    const UCHAR rlm[] = {
        0, 0, 0, 0, 2, 0, 16, 0, 149, 149, 0, 0, 2, 3, 1, 0, 0, 1, 0, 0
    };
    if (Length == 48 + sizeof(idle)) {
        assert(memcmp(data, idle, sizeof(idle)) == 0);
        return sim.RejectPort ? STATUS_NOT_SUPPORTED : 0;
    }
    if (Length == 48 + sizeof(ap)) {
        assert(memcmp(data, ap, sizeof(ap)) == 0);
        return 0;
    }
    assert(Length == 48 + sizeof(rlm) && memcmp(data, rlm, sizeof(rlm)) == 0);
    return sim.RejectStationRlm ? STATUS_NOT_SUPPORTED : 0;
}

static void OnTx(ULONG producer)
{
    if ((ReadStored(DMA_GLOBAL) & DMA_ENABLE) == 0) return;
    sim.LegacyReplyBytes = 0;
    assert(ReadStored(0xd6004) & 0x10000000);
    assert(!(ReadStored(DMA_GLOBAL_EXT) & DMA_TX_SCHEDULING));
    assert(ReadStored(PCIE_MAC_PM) & PCIE_MAC_PM_L0S_DIS);
    if (sim.Scenario == TX_TIMEOUT) return;

    ULONG consumer = ReadStored(TX_MCU_RING + RING_DEVICE);
    assert(producer == (consumer + 1) % TX_DESCRIPTOR_COUNT);
    assert(ReadStored(TX_MCU_RING + RING_BASE) == MOCK_BUS_BASE);
    assert(ReadStored(TX_MCU_RING + RING_COUNT) == TX_DESCRIPTOR_COUNT);
    DMA_DESCRIPTOR *tx = (DMA_DESCRIPTOR *)sim.CommonBuffer;
    assert(tx[consumer].FirstBuffer == MOCK_BUS_BASE + TX_PAYLOAD_OFFSET);
    ULONG length = (tx[consumer].Control & DESCRIPTOR_LEN0) >> DESCRIPTOR_LEN_SHIFT;
    assert(length >= 60 &&
        length <= (sim.StationMode ? MT7922_STATION_POWER_BYTES + MT7922_SCAN_TX_HEADER_BYTES : 224) &&
        (tx[consumer].Control & DESCRIPTOR_LAST0));
    const MCU_COMMAND *packet = (const MCU_COMMAND *)(sim.CommonBuffer + TX_PAYLOAD_OFFSET);
    assert(packet->Hardware[0] == (0x41000000 | length));
    assert(packet->Hardware[1] == 0x80010000);
    assert(packet->Sequence == sim.Commands % 15 + 1);
    ULONG commandId;
    ULONG reply;
    bool expectReply = true;
    const UCHAR *body = (const UCHAR *)packet + MT7922_SCAN_TX_HEADER_BYTES;
    if (sim.StationMode && packet->PortQueue == MCU_WM_PORT_QUEUE &&
        packet->Command == MT7922_SCAN_START_COMMAND) {
        assert(length == MT7922_SCAN_TX_BYTES && packet->SetQuery == MCU_SET_QUERY_SET);
        assert(body[158] == 4 && body[159] != 0); /* Explicit channel set. */
        assert(body[160] == 2 && body[161] == (sim.StationScans == 0 ? 36 : 149));
        assert(body[159] == (sim.StationScans == 0 ? 9 : 1));
        ++sim.StationScans;
        commandId = MT7922_SCAN_START_COMMAND;
        reply = 0;
        expectReply = false;
    } else if (sim.StationMode && packet->PortQueue == MCU_WM_PORT_QUEUE &&
        (packet->Command == 0x0f || packet->Command == 0x5c || packet->Command == 0x5d ||
         packet->Command == 0xc0 ||
         (packet->Command == 0xed && (packet->Extension == 0x4e || packet->Extension == 8)))) {
        commandId = packet->Command | ((ULONG)packet->Extension << 8);
        reply = 0;
        assert(packet->SetQuery == (packet->Command == 0xc0 ? 0 : MCU_SET_QUERY_SET));
        switch (packet->Command) {
        case 0x0f:
            assert(length == 148 && body[0] == sim.StationCountry[0] &&
                body[1] == sim.StationCountry[1] &&
                body[4] == 1 && body[5] == 1 && body[6] == 1 && body[9] == 9);
            assert(body[12] == 36 && body[76] == 165);
            expectReply = false;
            break;
        case 0x5c: {
            const ULONG payload = length - MT7922_SCAN_TX_HEADER_BYTES;
            assert(payload > MT7922_CLC_REQUEST_BYTES && body[0] == 1 &&
                body[4] == 0 && body[5] == 1);
            assert(((ULONG)body[2] | ((ULONG)body[3] << 8)) == payload);
            sim.StationClcLength = payload;
            memcpy(sim.StationClcCountry, body + 8, 2);
            memcpy(sim.StationClcType, body + 10, 2);
            sim.StationClcFirst = body[MT7922_CLC_REQUEST_BYTES];
            expectReply = false;
            break;
        }
        case 0x5d: {
            const bool last = sim.StationRadioBatches++ != 0;
            assert(body[4] == (last ? 1 : 8) && body[5] == 2 && body[6] == last &&
                body[8] == sim.StationCountry[0] && body[9] == sim.StationCountry[1] &&
                length == 108U + 162U * body[4]);
            assert(body[44] == (last ? 165 : 36) && body[45] == (last ? 20 : 10) &&
                body[205] == body[45]);
            expectReply = false;
            break;
        }
        case 0xc0:
            assert(length == 72 && packet->Arguments[0] == 0x820c8000U && packet->Arguments[1] == 0);
            break;
        default: {
            const bool receivePath = packet->Extension == 0x4e;
            assert(length == 140 && packet->ExtensionAck == 1);
            assert(body[0] == (receivePath ? 36 : 149) && body[0] == body[1]);
            assert(body[2] == 0 && body[3] == 2 && body[4] == (receivePath ? 3 : 2) && body[10] == 1);
            if (!receivePath) {
                ++sim.StationChannelSwitches;
            }
            break;
        }
        }
    } else if (sim.StationMode && (packet->PortQueue == MCU_DEV_INFO || packet->PortQueue == MCU_BSS_INFO)) {
        commandId = MCU_UNIFIED_FLAG | packet->PortQueue;
        reply = RespondToStationPort((const MCU_UNIFIED_COMMAND *)packet, length);
    } else if (packet->PortQueue == 0x24 || packet->PortQueue == 1 ||
        packet->PortQueue == 2 || packet->PortQueue == 3) {
        commandId = 0x10000U | packet->PortQueue;
        if (packet->PortQueue == MCU_STA_REC) {
            const MCU_UNIFIED_COMMAND *unified = (const MCU_UNIFIED_COMMAND *)packet;
            reply = sim.StationMode ? RespondToStationRec(unified, length) : RespondToStaRec(unified, length);
        } else {
            reply = RespondToSniffer((const MCU_UNIFIED_COMMAND *)packet, length);
        }
    } else {
        assert(packet->Length == length - 32 && packet->PortQueue == 0x8000);
        assert(packet->PacketId == 0xa0);
        assert(packet->SetQuery == (packet->Command == 0x8a ||
            (packet->Command == 0xed && packet->Extension != 0x01) ? 1 : 3) ||
            (packet->Command == 0xed && packet->Extension == 0x01 && packet->SetQuery == 0));
        commandId = packet->Command | ((ULONG)packet->Extension << 8);
        reply = RespondToCommand(packet, length);
    }
    ++sim.Commands;
    tx[consumer].Control |= DESCRIPTOR_DONE;
    Store(DMA_HOST_INT_STATUS, ReadStored(DMA_HOST_INT_STATUS) | (1U << 27));
    Store(TX_MCU_RING + RING_DEVICE, producer);
    if (expectReply && sim.Scenario != RX_TIMEOUT &&
        !(sim.Scenario == BOOT_CAP_TIMEOUT && packet->Command == 0x8a)) {
        PostMcuEvent(commandId, packet->Sequence, reply);
    }
}

static void OnScatter(ULONG producer)
{
    if ((ReadStored(DMA_GLOBAL) & DMA_ENABLE) == 0) return;
    assert(sim.FactoryImage != NULL && sim.RegionRemaining != 0);
    ULONG consumer = ReadStored(TX_FWDL_RING + RING_DEVICE);
    assert(producer == (consumer + 1) % FWDL_DESCRIPTOR_COUNT);
    assert(ReadStored(TX_FWDL_RING + RING_BASE) == MOCK_BUS_BASE + FWDL_RING_OFFSET);
    assert(ReadStored(TX_FWDL_RING + RING_COUNT) == FWDL_DESCRIPTOR_COUNT);

    DMA_DESCRIPTOR *tx = (DMA_DESCRIPTOR *)(sim.CommonBuffer + FWDL_RING_OFFSET);
    assert(tx[consumer].FirstBuffer == MOCK_BUS_BASE + TX_PAYLOAD_OFFSET);
    ULONG length = sim.RegionRemaining < 4096 ? sim.RegionRemaining : 4096;
    assert(tx[consumer].Control == ((length << 16) | 0x40000000));
    assert(memcmp(sim.CommonBuffer + TX_PAYLOAD_OFFSET, sim.FactoryImage + sim.ImageOffset, length) == 0);
    if (sim.Scenario == BOOT_SCATTER_TIMEOUT && sim.ScatterPackets == 5) return;

    sim.RegionRemaining -= length;
    sim.ImageOffset += length;
    ++sim.ScatterPackets;
    tx[consumer].Control |= DESCRIPTOR_DONE;
    Store(TX_FWDL_RING + RING_DEVICE, producer);
}

static void PostDataDescriptor(const UCHAR *Packet, ULONG Length)
{
    const ULONG slot = sim.DataRxNext;
    const ULONG payload = DATA_PAYLOAD_OFFSET + slot * RX_BUFFER_BYTES;
    DMA_DESCRIPTOR *descriptors = (DMA_DESCRIPTOR *)(sim.CommonBuffer + DATA_RING_OFFSET);

    assert(ReadStored(RX_DATA_RING + RING_BASE) == MOCK_BUS_BASE + DATA_RING_OFFSET);
    assert(ReadStored(RX_DATA_RING + RING_COUNT) == DATA_DESCRIPTOR_COUNT);
    assert(slot != ReadStored(RX_DATA_RING + RING_CPU));
    assert(descriptors[slot].FirstBuffer == MOCK_BUS_BASE + payload);
    memcpy(sim.CommonBuffer + payload, Packet, Length);
    descriptors[slot].Control = (Length << 16) | 0xc0000000U;
    sim.DataRxNext = (slot + 1) % DATA_DESCRIPTOR_COUNT;
    Store(RX_DATA_RING + RING_DEVICE, sim.DataRxNext);
}

static void PostIcmp6Reply(void)
{
    static const UCHAR snap[] = {0xaa, 0xaa, 0x03, 0x00, 0x17, 0xf2, 0x08, 0x00};
    static const UCHAR bssid[] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    static const UCHAR phone[] = {0x7e, 0, 0, 0, 0, 1};
    static const UCHAR station[] = {0x02, 0x44, 0x33, 0x22, 0x11, 0x01};
    UCHAR frame[88] = {0};
    frame[0] = 0x08;
    memcpy(frame + 4, station, 6);
    memcpy(frame + 10, phone, 6);
    memcpy(frame + 16, bssid, 6);
    memcpy(frame + 24, snap, 8);
    frame[32] = 0x03;
    frame[33] = 0x04;
    frame[38] = 0x86;
    frame[39] = 0xdd;
    frame[40] = 0x60;
    frame[45] = 8;
    frame[46] = 58;
    frame[47] = 64;
    LinkLocalFromMac(phone, frame + 48);
    LinkLocalFromMac(station, frame + 64);
    frame[80] = 129;
    frame[84] = AWDL_ECHO_ID >> 8;
    frame[85] = AWDL_ECHO_ID & 0xffU;
    frame[86] = (UCHAR)(sim.PingSeen >> 8);
    frame[87] = (UCHAR)sim.PingSeen;
    const USHORT checksum = Ipv6TransportChecksum(frame + 48, frame + 64, 58, frame + 80, 8);
    frame[82] = (UCHAR)(checksum >> 8);
    frame[83] = (UCHAR)checksum;
    if (sim.Wide80) {
        enum { RxdBytes = 24, HeaderBytes = 26, ChipPadding = 2, SubHeaderBytes = 14,
            WrapperBytes = 8, PayloadBytes = sizeof(frame) - 24 };
        UCHAR split[RxdBytes + HeaderBytes + ChipPadding + SubHeaderBytes + WrapperBytes + PayloadBytes] = {0};
        PutUint32(split, (2U << 27) | sizeof(split));
        PutUint32(split + 8, 1U << 25); /* Untranslated frame, translation-error bit set. */
        split[16] = 3; /* Hardware marks this as the first A-MSDU subframe. */
        UCHAR *mac = split + RxdBytes;
        memcpy(mac, frame, 24);
        mac[0] = 0x88;
        mac[24] = 0x80;
        mac[26] = 0xff; /* Two firmware bytes to skip. */
        mac[27] = 0xff;
        UCHAR *sub = mac + HeaderBytes + ChipPadding;
        memcpy(sub, station, 6);
        memcpy(sub + 6, phone, 6);
        sub[13] = WrapperBytes + PayloadBytes;
        UCHAR *wrapped = sub + SubHeaderBytes;
        wrapped[0] = wrapped[1] = 0xaa;
        wrapped[2] = 0x03;
        wrapped[7] = PayloadBytes;
        memcpy(wrapped + WrapperBytes, frame + 24, PayloadBytes);

        size_t ipBytes = 0;
        BOOLEAN isWrapped = FALSE;
        assert(AwdlMsduToIp(wrapped, WrapperBytes + PayloadBytes, &ipBytes, &isWrapped) == wrapped + 24);
        assert(isWrapped && ipBytes == PayloadBytes - 16);
        assert(AwdlMsduToIp(wrapped, WrapperBytes + PayloadBytes - 1, &ipBytes, &isWrapped) == NULL);
        PostDataDescriptor(split, sizeof(split));
        return;
    }
    UCHAR record[24 + sizeof(frame)] = {0};
    PutUint32(record, (2U << 27) | sizeof(record));
    memcpy(record + 24, frame, sizeof(frame));
    PostDataDescriptor(record, sizeof(record));
}

static void ReplyToTcpProbe(const UCHAR *Ip)
{
    static const UCHAR peer[] = {0x7e, 0, 0, 0, 0, 1};
    static const UCHAR station[] = {0x02, 0x44, 0x33, 0x22, 0x11, 0x01};
    const UCHAR *outgoing = Ip + 40;
    const ULONG sequence = ReadBigEndian32(outgoing + 4);
    assert(ReadBigEndian16(outgoing + 2) == 8770);
    assert(Ipv6TransportChecksum(Ip + 8, Ip + 24, 6, outgoing, 20) == 0);
    if (outgoing[13] == AWDL_TCP_RST) {
        assert(sim.TcpSynSeen == 2 && sim.TcpResetSeen == 0 && sequence == sim.TcpSequence + 1);
        ++sim.TcpResetSeen;
        return;
    }
    assert(outgoing[13] == AWDL_TCP_SYN);
    ++sim.TcpSynSeen;
    if (sim.TcpSynSeen == 1) {
        sim.TcpSequence = sequence;
    }
    assert(sequence == sim.TcpSequence);

    UCHAR reply[60] = {0};
    reply[0] = 0x60;
    reply[5] = 20;
    reply[6] = 6;
    reply[7] = 64;
    memcpy(reply + 8, Ip + 24, 16);
    memcpy(reply + 24, Ip + 8, 16);
    UCHAR *tcp = reply + 40;
    memcpy(tcp, outgoing + 2, 2);
    memcpy(tcp + 2, outgoing, 2);
    tcp[7] = 100;
    /* The first reply acknowledges the wrong sequence, forcing the timed retry. */
    const ULONG ack = sequence + (sim.TcpSynSeen == 1 ? 2U : 1U);
    tcp[8] = (UCHAR)(ack >> 24);
    tcp[9] = (UCHAR)(ack >> 16);
    tcp[10] = (UCHAR)(ack >> 8);
    tcp[11] = (UCHAR)ack;
    tcp[12] = 5U << 4;
    tcp[13] = AWDL_TCP_SYN | AWDL_TCP_ACK;
    const USHORT sum = Ipv6TransportChecksum(reply + 8, reply + 24, 6, tcp, 20);
    tcp[16] = (UCHAR)(sum >> 8);
    tcp[17] = (UCHAR)sum;

    UCHAR record[24 + 40 + sizeof(reply)] = {0};
    assert(AwdlBuildIpFrame(record + 24, sizeof(record) - 24, station, peer,
        reply, sizeof(reply), 1) == sizeof(record) - 24);
    PutUint32(record, (2U << 27) | sizeof(record));
    PostDataDescriptor(record, sizeof(record));
}

static void PostMdnsReply(UCHAR Sender)
{
    const UCHAR phone[] = {Sender, 0, 0, 0, 0, 1};
    static const UCHAR station[] = {0x02, 0x44, 0x33, 0x22, 0x11, 0x01};
    static const UCHAR answer[] = {0xc0, 0x0c, 0, 12, 0, 1, 0, 0, 0, 120,
        0, 4, 1, 'x', 0xc0, 0x0c};
    enum { QueryEnd = DNS_HEADER_BYTES + sizeof(AirdropDnsName) + 4 };
    UCHAR ip[40 + 8 + QueryEnd + sizeof(answer)] = {0};
    assert(BuildMdnsQuery(ip, sizeof(ip), phone, NULL) == 40 + 8 + QueryEnd);
    LinkLocalFromMac(station, ip + 24);
    ip[5] = sizeof(ip) - 40;
    UCHAR *udp = ip + 40;
    udp[5] = ip[5];
    udp[6] = udp[7] = 0;
    UCHAR *dns = udp + 8;
    dns[2] = 0x84; /* Authoritative response. */
    dns[7] = 1;
    memcpy(dns + QueryEnd, answer, sizeof(answer));
    const USHORT sum = Ipv6TransportChecksum(ip + 8, ip + 24, 17, udp, ip[5]);
    udp[6] = (UCHAR)(sum >> 8);
    udp[7] = (UCHAR)sum;

    MT7922_MCU_PROBE_INFO parsed = {0};
    CountMdnsResponse(&parsed, udp, ip[5], TRUE, ip + 8);
    assert(parsed.MdnsResponses == 1 && parsed.MdnsAirDropAnswers == 1);
    dns[QueryEnd + 1] = QueryEnd; /* Reject a self-referencing compression pointer. */
    CountMdnsResponse(&parsed, udp, ip[5], TRUE, ip + 8);
    assert(parsed.MdnsResponses == 1 && parsed.MdnsAirDropAnswers == 1);
    dns[QueryEnd + 1] = 0x0c;

    CountMdnsResponse(&parsed, udp, ip[5], TRUE, ip + 8);
    assert(parsed.MdnsSampleCount == 1); /* A second identical response is not stored. */
    const MT7922_AWDL_MDNS_SAMPLE *sample = &parsed.MdnsSamples[0];
    assert(sample->Length == (ULONG)ip[5] - 8);
    assert(memcmp(sample->SourceIpv6, ip + 8, sizeof(sample->SourceIpv6)) == 0);
    assert(memcmp(sample->Bytes, dns, sample->Length) == 0);

    UCHAR record[24 + 40 + sizeof(ip)] = {0};
    assert(AwdlBuildIpFrame(record + 24, sizeof(record) - 24, station, phone,
        ip, sizeof(ip), 1) == sizeof(record) - 24);
    PutUint32(record, (2U << 27) | sizeof(record));
    PostDataDescriptor(record, sizeof(record));
    if (Sender == 0x8e) {
        PutUint32(record + 4, (1U << 27)); /* Firmware reports an FCS error. */
        PostDataDescriptor(record, sizeof(record));
        PutUint32(record + 4, 0);
        PutUint32(record + 8, (1U << 13) | (1U << 23)); /* Translated, A-MSDU error. */
        PostDataDescriptor(record, sizeof(record));
        PutUint32(record + 8, 0);
        PutUint32(record + 4, 1U << 15); /* Group 5 without group 3. */
        PostDataDescriptor(record, sizeof(record));
        PutUint32(record + 4, 0);
        PutUint32(record, (2U << 27) | (sizeof(record) + 1));
        PostDataDescriptor(record, sizeof(record));
    }
}

static void PostAddbaRequest(UCHAR Sender, UCHAR Token)
{
    static const UCHAR bssid[] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    static const UCHAR phone[] = {0x7e, 0, 0, 0, 0, 1};
    static const UCHAR station[] = {0x02, 0x44, 0x33, 0x22, 0x11, 0x01};
    UCHAR frame[33] = {0};
    frame[0] = 0xd0;
    memcpy(frame + 4, station, 6);
    memcpy(frame + 10, phone, 6);
    frame[10] = Sender;
    memcpy(frame + 16, bssid, 6);
    frame[24] = 3;
    frame[25] = 0;
    frame[26] = Token;
    frame[27] = 0x03;
    frame[28] = 0x10;
    frame[31] = 0x30;
    frame[32] = 0x12;
    UCHAR record[24 + sizeof(frame)] = {0};
    PutUint32(record, (2U << 27) | sizeof(record));
    memcpy(record + 24, frame, sizeof(frame));
    PostDataDescriptor(record, sizeof(record));
}

static void InjectAwdl(UCHAR Sender);

static const UCHAR TestAirDropLabel[] = "123456789abc";

static void CheckServiceMif(const UCHAR *frame, ULONG frameBytes)
{
    const UCHAR *tlv = frame + 40;
    const UCHAR *end = frame + frameBytes;
    ULONG records = 0, arpa = 0;
    while (end - tlv >= 3) {
        const UCHAR kind = tlv[0];
        const USHORT bytes = (USHORT)(tlv[1] | tlv[2] << 8);
        tlv += 3;
        assert(end - tlv >= bytes);
        if (kind == 16) {
            assert(bytes == 16 && tlv[1] == AWDL_AIRDROP_ID_BYTES);
            assert(memcmp(tlv + 2, TestAirDropLabel, AWDL_AIRDROP_ID_BYTES) == 0);
            assert(tlv[14] == 0xc0 && tlv[15] == 0x0c);
            ++arpa;
        } else if (kind == 2) {
            const UCHAR *name = tlv + 2;
            const ULONG nameBytes = (ULONG)(tlv[0] | tlv[1] << 8) - 1U;
            assert((records == 0 && nameBytes == 2) || (records < 3 && nameBytes == 15));
            assert(name[nameBytes - 2] == 0xc0 && name[nameBytes - 1] == 0x07);
            if (records != 0) {
                assert(name[0] == AWDL_AIRDROP_ID_BYTES);
                assert(memcmp(name + 1, TestAirDropLabel, AWDL_AIRDROP_ID_BYTES) == 0);
            }
            const UCHAR *payload = name + nameBytes + 5;
            const ULONG valueBytes = name[nameBytes + 1] | (ULONG)name[nameBytes + 2] << 8;
            assert(payload + valueBytes == tlv + bytes);
            switch (name[nameBytes]) {
            case 12:
                assert(records == 0 && valueBytes == 15);
                assert(payload[0] == AWDL_AIRDROP_ID_BYTES &&
                    memcmp(payload + 1, TestAirDropLabel, AWDL_AIRDROP_ID_BYTES) == 0);
                assert(payload[13] == 0xc0 && payload[14] == 0x07);
                break;
            case 33:
                assert(records == 1 && valueBytes == 21 && ReadBigEndian16(payload + 4) == 8771);
                assert(payload[6] == AWDL_AIRDROP_ID_BYTES &&
                    memcmp(payload + 7, TestAirDropLabel, AWDL_AIRDROP_ID_BYTES) == 0);
                assert(payload[19] == 0xc0 && payload[20] == 0x0c);
                break;
            case 16:
                assert(records == 2 && valueBytes == 11 && payload[0] == 10);
                assert(memcmp(payload + 1, "flags=1019", 10) == 0);
                break;
            default:
                assert(0);
            }
            ++records;
        }
        tlv += bytes;
    }
    assert(tlv == end && arpa == 1 && records == 3);
}

static void PostStationRelease(USHORT Token)
{
    UCHAR released[12] = {0};
    PutUint32(released, 0x3001000c);
    PutUint32(released + 8, (ULONG)Token << 16 | 1U);
    PostDataDescriptor(released, sizeof(released));
}

static void PostStationEapol(bool WrongBssid, bool Protected, uint8_t Message)
{
    UCHAR record[] = {
        62, 0, 0, 0x10, /* RXD 0 */
        [24] = 0x08, 0x02,
        [28] = 2, 0x44, 0x33, 0x22, 0x11, 1,
        [34] = 2, 0x21, 0x22, 0x23, 0x24, 0x25,
        [40] = 2, 0x21, 0x22, 0x23, 0x24, 0x25,
        [48] = 0xaa, 0xaa, 0x03, 0, 0, 0, 0x88, 0x8e, 2, 3, 0, 2, 0, Message
    };
    assert(sizeof(record) == record[0]);
    if (WrongBssid) {
        record[39] ^= 1;
    }
    if (Protected) {
        record[25] |= 0x40;
    }
    PostDataDescriptor(record, sizeof(record));
}

static void PostStationCcmpEapol(uint64_t Pn, uint32_t SecurityBits, uint8_t Message)
{
    UCHAR record[] = {
        78, 0, 0, 0x10, /* RXD 0 */
        [40] = 0x08, 0x42,
        [44] = 2, 0x44, 0x33, 0x22, 0x11, 1,
        [50] = 2, 0x21, 0x22, 0x23, 0x24, 0x25,
        [56] = 2, 0x21, 0x22, 0x23, 0x24, 0x25,
        [64] = 0xaa, 0xaa, 0x03, 0, 0, 0, 0x88, 0x8e, 2, 3, 0, 2, 0, Message
    };
    assert(sizeof(record) == record[0]);
    PutUint32(record + 4, SecurityBits | (1U << 11)); /* Group 1. */
    for (unsigned i = 0; i < 6; ++i) {
        record[24 + i] = (uint8_t)(Pn >> (8U * i));
    }
    PostDataDescriptor(record, sizeof(record));
}

static void PostStationCcmpData(uint64_t Pn, uint32_t SecurityBits, bool Group, bool Protected)
{
    UCHAR record[100] = {
        sizeof(record), 0, 0, 0x10, /* RXD 0 with 24-byte base. */
        [40] = 0x08, 0x42,
        [44] = 2, 0x44, 0x33, 0x22, 0x11, 1,
        [50] = 2, 0x21, 0x22, 0x23, 0x24, 0x25,
        [56] = 2, 0x31, 0x32, 0x33, 0x34, 0x35,
        [64] = 0xaa, 0xaa, 0x03, 0, 0, 0, 0x08, 0x06,
        [72] = 0, 1, 8, 0, 6, 4, 0, 1
    };
    if (Group) {
        memset(record + 44, 0xff, 6);
    }
    if (!Protected) {
        record[41] &= ~0x40U;
    }
    PutUint32(record + 4, SecurityBits | (1U << 11));
    for (unsigned i = 0; i < 6; ++i) {
        record[24 + i] = (uint8_t)(Pn >> (8U * i));
    }
    PostDataDescriptor(record, sizeof(record));
}

static void FinishStationDataTx(ULONG Producer, DMA_DESCRIPTOR *Descriptor,
    const ULONG *Txwi, const UCHAR *Frame)
{
    const UCHAR bssid[] = {2, 0x21, 0x22, 0x23, 0x24, 0x25};
    const UCHAR local[] = {2, 0x44, 0x33, 0x22, 0x11, 1};
    const UCHAR body[] = {0xaa,0xaa,0x03,0,0,0,0x88,0x8e,2,3,0,2,0,2};
    const bool protectedFrame = Frame[1] == 0x41;
    assert((Frame[1] == 1 || protectedFrame) && (Txwi[2] & 0x3fU) == 0x20);
    assert(((Txwi[3] & 2U) != 0) == protectedFrame);
    assert(memcmp(Frame + 4, bssid, 6) == 0 && memcmp(Frame + 10, local, 6) == 0);
    if (Frame[30] == 0x08 && Frame[31] == 0x06) {
        const uint8_t broadcast[] = {255, 255, 255, 255, 255, 255};
        assert(protectedFrame && (Txwi[12 / 4] & 1U) == 0 && (Txwi[1] & 0x3ffU) == 1);
        assert((Txwi[11] & 0xfffU) == 60 && memcmp(Frame + 16, broadcast, 6) == 0);
        ++sim.StationArpSent;
    } else {
        assert((Txwi[11] & 0xfffU) == 38 && memcmp(Frame + 16, bssid, 6) == 0);
        assert(memcmp(Frame + 24, body, sizeof(body)) == 0);
        ++sim.StationEapolSent;
        sim.ProtectedStationEapolSent += protectedFrame;
    }
    Descriptor->Control |= DESCRIPTOR_DONE;
    Store(TX_ACTION_RING + RING_DEVICE, Producer);
    const USHORT token = (USHORT)(Txwi[8] & 0x7fffU);
    if (sim.DelayStationReleaseToGroupKey) {
        sim.HeldStationToken = token;
    } else if (sim.CombineStationAndSharedRelease) {
        UCHAR released[16] = {0};
        PutUint32(released, 0x30020010);
        PutUint32(released + 8, STATION_SHARED_TOKEN << 16 | 1U);
        PutUint32(released + 12, (ULONG)token << 16 | 1U);
        PostDataDescriptor(released, sizeof(released));
        sim.CombineStationAndSharedRelease = false;
    } else {
        PostStationRelease(token);
    }
}

static void FinishStationTx(ULONG Producer)
{
    static const UCHAR ap[] = {2, 0x21, 0x22, 0x23, 0x24, 0x25};
    static const UCHAR local[] = {2, 0x44, 0x33, 0x22, 0x11, 1};
    static const UCHAR associationBody[] = {
        0x11, 0, 3, 0, 0, 3, 'L', 'A', 'N', 1, 3, 0x8c, 0x98, 0xb0
    };
    DMA_DESCRIPTOR *tx = (DMA_DESCRIPTOR *)(sim.CommonBuffer + ACTION_RING_OFFSET);
    const ULONG slot = (Producer + TX_ACTION_COUNT - 1) % TX_ACTION_COUNT;
    const ULONG *txwi = (const ULONG *)(sim.CommonBuffer + ACTION_TXWI_OFFSET);
    const UCHAR *frame = sim.CommonBuffer + ACTION_FRAME_OFFSET;
    if (tx[slot].FirstBuffer == MOCK_BUS_BASE + UNICAST_TX_OFFSET) {
        txwi = (const ULONG *)(sim.CommonBuffer + UNICAST_TX_OFFSET);
        frame = sim.CommonBuffer + UNICAST_TX_OFFSET + MT7922_TXWI_BYTES;
        AWDL_ACTION advert;
        assert(txwi[10] == MOCK_BUS_BASE + UNICAST_TX_OFFSET + MT7922_TXWI_BYTES);
        assert((txwi[8] & 0x7fffU) == STATION_SHARED_TOKEN && STATION_SHARED_TOKEN < STATION_TOKEN_COUNT);
        assert((txwi[6] >> 16) == MT7922_OFDM_6_MBPS && memcmp(frame + 10, local, 6) == 0);
        const bool data = frame[0] == 0x08;
        bool multicast = false;
        bool failSynAck = false;
        if (data) {
            size_t bytes = 0;
            BOOLEAN isWrapped = FALSE;
            const UCHAR *ip = AwdlMsduToIp(frame + 24, (txwi[11] & 0xfffU) - 24, &bytes, &isWrapped);
            assert(ip != NULL);
            multicast = AwdlIsMdnsMulticast(ip, bytes) != 0;
            if (multicast) {
                assert(memcmp(frame + 4, AwdlMdnsMac, 6) == 0 && (txwi[3] & 1U) != 0);
                ++sim.StationSharedAnnouncements;
            } else if (ip[6] == 6) {
                assert(bytes == 60 && ReadBigEndian16(ip + 40) == 8771 &&
                    ip[53] == (AWDL_TCP_SYN | AWDL_TCP_ACK));
                failSynAck = ++sim.SharedSynAcks == 1;
            } else {
                assert(bytes == 56 && ip[6] == 58 && ip[40] == 128);
                assert(ReadBigEndian16(ip + 44) == AWDL_ECHO_ID && (txwi[3] & 1U) == 0);
                assert(Ipv6TransportChecksum(ip + 8, ip + 24, 58, ip + 40, 16) == 0);
                sim.PingSeen = ReadBigEndian16(ip + 46) - 1;
                PostIcmp6Reply(); /* Older, otherwise valid reply should not satisfy the session. */
                ++sim.PingSeen;
                ++sim.StationSharedEchoes;
            }
        } else {
            const ULONG bytes = txwi[11] & 0xfffU;
            assert(AwdlDecodeAction(frame, bytes, &advert) == AwdlMif);
            for (size_t offset = 0; offset + 3 <= advert.ElementBytes;) {
                const UCHAR *element = advert.Elements + offset;
                const size_t size = element[1] | ((size_t)element[2] << 8);
                if (element[0] == 2) {
                    CheckServiceMif(frame, bytes);
                    ++sim.StationSharedServiceAdverts;
                    break;
                }
                offset += size + 3;
            }
            ++sim.StationSharedAdverts;
        }
        tx[slot].Control |= DESCRIPTOR_DONE;
        Store(TX_ACTION_RING + RING_DEVICE, Producer);
        const ULONG sequence = (ULONG)(frame[22] | (frame[23] << 8)) >> 4;
        UCHAR status[40] = {0};
        PutUint32(status, sizeof(status));
        PutUint32(status + 8, MT7922_OFDM_6_MBPS | (failSynAck ? 1U << 16 : 0));
        PutUint32(status + 12, sequence << 20);
        PutUint32(status + 20, STATION_SHARED_PACKET_ID << 24);
        if (!multicast && !(data && sim.OmitSharedUnicastReport)) {
            PostDataDescriptor(status, sizeof(status));
        }
        if (data) {
            if (!sim.HoldSharedRelease) {
                PostStationRelease(STATION_SHARED_TOKEN);
            }
        } else {
            /* The MIF token remains owned until the next ordinary frame. */
            sim.CombineStationAndSharedRelease = true;
        }
        return;
    }
    if (frame[0] == 0x08) {
        FinishStationDataTx(Producer, &tx[slot], txwi, frame);
        return;
    }
    const bool association = frame[0] == 0;
    const bool deauth = frame[0] == 0xc0;
    assert(association || deauth || frame[0] == 0xb0);
    assert(tx[slot].FirstBuffer == MOCK_BUS_BASE + ACTION_TXWI_OFFSET &&
        tx[slot].Control == (64U << 16 | DESCRIPTOR_LAST0));
    assert((txwi[0] >> 25) == 3 && ((txwi[1] >> 11) & 31) == 12 && (txwi[1] & 0x3ff) == 1);
    assert((txwi[2] & 0x3fU) == (association ? 0U : deauth ? 12U : 11U));
    assert(txwi[10] == MOCK_BUS_BASE + ACTION_FRAME_OFFSET);
    assert(memcmp(frame + 4, ap, 6) == 0 && memcmp(frame + 10, local, 6) == 0 &&
        memcmp(frame + 16, ap, 6) == 0);
    if (deauth) {
        assert((txwi[11] & 0xfffU) == 26 && frame[24] == 3 && frame[25] == 0);
        ++sim.StationDeauthSent;
    } else if (association) {
        assert((txwi[11] & 0xfffU) == 24 + sizeof(associationBody));
        assert(memcmp(frame + 24, associationBody, sizeof(associationBody)) == 0);
    } else {
        static const UCHAR auth[] = {0, 0, 1, 0, 0, 0};
        assert((txwi[11] & 0xfffU) == 30 && memcmp(frame + 24, auth, sizeof(auth)) == 0);
    }
    ++sim.StationMgmtSent;
    tx[slot].Control |= DESCRIPTOR_DONE;
    Store(TX_ACTION_RING + RING_DEVICE, Producer);
    const USHORT token = (USHORT)(txwi[8] & 0x7fffU);
    if (!sim.OmitStationRelease) {
        PostStationRelease(token);
    } else {
        sim.HeldStationToken = token;
    }

    if (deauth) {
        sim.StationAssociationPending = false;
        return;
    }

    UCHAR rx[54] = {sizeof(rx), 0, 0, 0x10};
    UCHAR *reply = rx + 24;
    reply[0] = association ? 0x10 : 0xb0;
    memcpy(reply + 4, local, 6);
    memcpy(reply + 10, ap, 6);
    memcpy(reply + 16, ap, 6);
    if (association) {
        reply[24] = 0x11;
        reply[26] = sim.RejectStationAssoc ? 17 : 0;
        reply[28] = 7;
        reply[29] = 0xc0;
    } else {
        reply[26] = 2;
        reply[28] = sim.RejectStationAuth ? 13 : 0;
    }

    /* A response from another BSSID must not complete the exchange. */
    reply[16] ^= 0x10;
    PostDataDescriptor(rx, sizeof(rx));
    reply[16] ^= 0x10;
    PostDataDescriptor(rx, sizeof(rx));
    if (association && !sim.RejectStationAssoc) {
        sim.StationAssociationPending = true;
        PostStationEapol(false, false, 1); /* Firmware delivered M1 before WDI publication. */
    }
}

static void OnActionTx(ULONG Producer)
{
    if ((ReadStored(DMA_GLOBAL) & DMA_ENABLE) == 0) {
        return;
    }
    if (sim.StationMode) {
        FinishStationTx(Producer);
        return;
    }
    assert(sim.TransmitAwdl && sim.ListenChannel == sim.TransmitChannel && Producer < TX_ACTION_COUNT);
    DMA_DESCRIPTOR *tx = (DMA_DESCRIPTOR *)(sim.CommonBuffer + ACTION_RING_OFFSET);
    ULONG slot = (Producer + TX_ACTION_COUNT - 1) % TX_ACTION_COUNT;
    const ULONG txwiOffset = tx[slot].FirstBuffer - MOCK_BUS_BASE;
    assert(txwiOffset == ACTION_TXWI_OFFSET ||
        (txwiOffset >= UNICAST_TX_OFFSET && txwiOffset < sim.BufferBytes &&
            (txwiOffset - UNICAST_TX_OFFSET) % UNICAST_TX_STRIDE == 0));
    assert(tx[slot].Control == (64U << 16 | DESCRIPTOR_LAST0));
    if (sim.HeldUnicastOffset != 0) {
        assert(memcmp(sim.CommonBuffer + sim.HeldUnicastOffset, sim.HeldUnicast,
            sizeof(sim.HeldUnicast)) == 0);
        if (txwiOffset == ACTION_TXWI_OFFSET && sim.Counter - sim.HeldUnicastAt < 3000000ULL) {
            ++sim.BroadcastDuringUnicast;
        }
    }

    const UCHAR *frame = sim.CommonBuffer + txwiOffset + MT7922_TXWI_BYTES;
    static const UCHAR broadcast[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    static const UCHAR phone[] = {0x7e, 0, 0, 0, 0, 1};
    static const UCHAR station[] = {0x02, 0x44, 0x33, 0x22, 0x11, 0x01};
    const ULONG *txwi = (const ULONG *)(sim.CommonBuffer + txwiOffset);
    assert(txwi[6] == ((ULONG)sim.TxRateCode << 16 | 4U));
    const ULONG reportedRate = 0x8000U | sim.TxRateCode;

    const ULONG probes = sim.Scenario == TRANSMIT_ACK || sim.Scenario == TRANSMIT_NO_REPORT
        ? 1U : 3U;
    UCHAR report[40] = {0};
    UCHAR released[12] = {0};
    BOOLEAN delivered;
    BOOLEAN withholdCompletion = FALSE;
    if (frame[0] == 0x08 && (frame[4] & 1U) != 0) {
        const UCHAR *ip = frame + 40;
        const UCHAR *udp = ip + 40;
        const UCHAR *dns = udp + 8;
        const UCHAR *query = dns + DNS_HEADER_BYTES + sizeof(AirdropDnsName);
        const USHORT udpBytes = ReadBigEndian16(udp + 4);
        assert(txwiOffset == ACTION_TXWI_OFFSET && memcmp(frame + 4, AwdlMdnsMac, 6) == 0);
        assert((txwi[2] & (1U << 10)) != 0 && (txwi[3] & 1U) != 0);
        assert(ip[0] == 0x60 && ip[6] == 17 && ip[7] == 255);
        assert(memcmp(ip + 24, AwdlMdnsAddress, 16) == 0);
        assert(ReadBigEndian16(ip + 4) == udpBytes && ReadBigEndian16(udp) == AWDL_MDNS_PORT);
        assert(ReadBigEndian16(udp + 2) == AWDL_MDNS_PORT && dns[5] == 1);
        assert(Ipv6TransportChecksum(ip + 8, ip + 24, 17, udp, udpBytes) == 0);
        assert(memcmp(dns + DNS_HEADER_BYTES, AirdropDnsName, sizeof(AirdropDnsName)) == 0);
        assert(ReadBigEndian16(query) == 12 && ReadBigEndian16(query + 2) == 0x8001);
        if (sim.Wide80 && !sim.Bridge && sim.MdnsSeen == 0) {
            assert(sim.Counter - sim.FirstSyncAt >= 600ULL * 10000);
            assert(sim.DirectMdnsSeen == 0);
        }
        ++sim.MdnsSeen;
        assert((ULONG)(frame[22] >> 4 | (ULONG)frame[23] << 4) == sim.DataSeen + sim.MdnsSeen);
        if (sim.ReceivingLink && sim.LinkReady) {
            ++sim.ReceiverAnnouncementsSeen;
        } else if (sim.Scenario == TRANSMIT_ACK || (sim.PublishService && !sim.LateDiscovery)) {
            assert(sim.MdnsSeen == 1);
            PostMdnsReply(0x7e);
        }
        delivered = TRUE;
    } else if (frame[0] == 0x08) {
        assert(memcmp(frame + 4, phone, 6) == 0);
        assert(memcmp(frame + 10, station, 6) == 0);
        assert((txwi[2] & (1U << 10)) == 0); /* Unicast data requests an ACK. */
        assert((txwi[3] & 1U) == 0);
        ++sim.DataSeen;
        assert(((txwi[2] >> 4) & 3U) == 2 && ((txwi[7] >> 20) & 3U) == 2);
        assert((ULONG)(frame[22] >> 4 | (ULONG)frame[23] << 4) == sim.DataSeen + sim.MdnsSeen);
        assert(((txwi[3] >> 16) & 0xfffU) == sim.DataSeen + sim.MdnsSeen);
        if (sim.Scenario == TRANSMIT_ACK && sim.DataSeen == 1) {
            /* A later RX frame arrives while the driver is waiting for this TX result. */
            sim.Counter += 10000;
            InjectAwdl(0x7e);
            InjectAwdl(0x8e);
            PostAddbaRequest(0x8e, 0x5a);
            assert(txwiOffset >= UNICAST_TX_OFFSET);
            sim.HeldUnicastOffset = txwiOffset;
            sim.HeldUnicastAt = sim.Counter;
            memcpy(sim.HeldUnicast, sim.CommonBuffer + txwiOffset, sizeof(sim.HeldUnicast));
            withholdCompletion = TRUE;
        }
        delivered = TRUE;
        static const UCHAR snap[] = {0xaa, 0xaa, 0x03, 0x00, 0x17, 0xf2, 0x08, 0x00};
        if (memcmp(frame + 24, snap, 8) == 0) {
            const UCHAR *ip = frame + 40;
            if (ip[6] == 17) {
                UCHAR dest[16];
                LinkLocalFromMac(phone, dest);
                assert(memcmp(ip + 24, dest, sizeof(dest)) == 0);
                const UCHAR *udp = ip + 40;
                const USHORT udpBytes = ReadBigEndian16(udp + 4);
                assert(ReadBigEndian16(udp) == AWDL_MDNS_PORT &&
                    ReadBigEndian16(udp + 2) == AWDL_MDNS_PORT);
                assert(Ipv6TransportChecksum(ip + 8, ip + 24, 17, udp, udpBytes) == 0);
                ++sim.DirectMdnsSeen;
                if (sim.Scenario == TRANSMIT_NO_REPORT) {
                    assert(sim.DirectMdnsSeen == 1);
                    PostMdnsReply(0x8e);
                    PostMdnsReply(0x7e);
                }
            } else if (ip[6] == 6) {
                assert(sim.Wide80 && sim.PublishService && sim.MdnsSeen != 0 && sim.PingSeen != 0);
                ReplyToTcpProbe(ip);
            } else {
                assert(ip[6] == 58);
                if (sim.LinkReady && sim.LateDiscovery && ReadBigEndian16(ip + 4) == 16) {
                    ++sim.RecoveryEchoSeen;
                } else if (sim.LinkReady) {
                    assert(ReadBigEndian16(ip + 4) == MT7922_AWDL_LINK_MTU - 40);
                    assert(ip[MT7922_AWDL_LINK_MTU - 1] == (UCHAR)(sim.PingSeen - sim.PingBeforeLink + 1));
                    assert(Ipv6TransportChecksum(ip + 8, ip + 24, 58, ip + 40,
                        MT7922_AWDL_LINK_MTU - 40) == 0);
                    assert(txwiOffset == UNICAST_TX_OFFSET +
                        ((sim.DataSeen + sim.AddbaSeen - 1) % MT7922_AWDL_DATA_PROBES) * UNICAST_TX_STRIDE);
                }
                ++sim.PingSeen;
                if (!withholdCompletion && (!sim.LateDiscovery || sim.LinkReady)) {
                    PostIcmp6Reply();
                }
            }
        }
        if (!withholdCompletion) {
            PutUint32(report, sizeof(report));
            PutUint32(report + 8, reportedRate);
            PutUint32(report + 16, MT7922_AWDL_DATA_WCID << 16);
            PutUint32(report + 20, (txwi[5] & 0xffU) << 24);
            PostDataDescriptor(report, sizeof(report));
        }
    } else if (frame[0] == 0xd0 && (frame[4] & 1) == 0 && frame[24] == 3) {
        assert(frame[25] == 1 && memcmp(frame + 4, phone, 6) == 0);
        assert(sim.RxBaSta == 1 && (sim.Scenario != TRANSMIT_RETRY || sim.Bridge));
        assert(frame[26] == 0x5aU + (sim.Bridge ? 0U : sim.AddbaSeen));
        assert(sim.Bridge || sim.AddbaSeen <= sim.DataSeen);
        assert(frame[27] == 0 && frame[28] == 0);
        assert(frame[29] == 0x02 && frame[30] == 0x10);
        assert(frame[31] == 0 && frame[32] == 0);
        ++sim.AddbaSeen;
        if (sim.Scenario == TRANSMIT_ACK && sim.AddbaSeen <= AWDL_ADDBA_MAX_RESPONSES) {
            PostAddbaRequest(0x7e, (UCHAR)(0x5aU + sim.AddbaSeen));
        }
        delivered = !sim.Bridge;
        PutUint32(report, sizeof(report));
        PutUint32(report + 8, reportedRate);
        PutUint32(report + 16, MT7922_AWDL_DATA_WCID << 16);
        PutUint32(report + 20, (txwi[5] & 0xffU) << 24);
        PostDataDescriptor(report, sizeof(report));
    } else if (frame[0] == 0xd0 && (frame[4] & 1) == 0) {
        assert(memcmp(frame + 4, phone, 6) == 0);
        assert(memcmp(frame + 10, station, 6) == 0);
        assert((txwi[2] & (1U << 10)) == 0); /* Unicast management requests an ACK. */
        assert((txwi[3] & 1U) == 0);
        AWDL_ACTION action;
        assert(AwdlDecodeAction(frame, 273, &action) == AwdlPsf);
        assert(action.DeviceClass == MT7922_AWDL_DEVICE_CLASS);
        assert(action.Sync.UntilNextSlotTu >= 1 && action.Sync.UntilNextSlotTu <= 64);
        ++sim.MgmtSeen;
        delivered = TRUE;
        PutUint32(report, sizeof(report));
        PutUint32(report + 8, reportedRate);
        PutUint32(report + 16, MT7922_AWDL_DATA_WCID << 16);
        PutUint32(report + 20, (txwi[5] & 0xffU) << 24);
        PostDataDescriptor(report, sizeof(report));
    } else if (sim.ActionFrames < probes) {
        assert(memcmp(frame + 4, broadcast, 6) == 0);
        assert((txwi[2] & (1U << 10)) != 0); /* TXD2 multicast. */
        assert((txwi[3] & 1U) != 0); /* TXD3 no-ACK. */
        assert(memcmp(frame + 10, station, 6) == 0);
        assert(frame[22] == Producer << 4);
        AWDL_ACTION action;
        assert(AwdlDecodeAction(frame, 273, &action) == AwdlPsf);
        assert(action.Sync.WindowTu == 16 && action.Sync.UntilNextSlotTu == 26);
        assert(action.Sync.NextChannel == sim.TransmitChannel);
        assert(action.Sequence.Count == 16 && action.Sequence.Channel[1] == sim.TransmitChannel);
        assert(action.Sequence.OperatingClass[1] == (sim.TransmitChannel > 14 ? 128 : 81));
        assert(action.Sequence.Channel[15] == (sim.TransmitChannel == 149 ? 0 : sim.TransmitChannel));
        delivered = sim.Scenario == TRANSMIT_ACK || sim.Scenario == TRANSMIT_NO_REPORT ||
            (sim.Scenario == TRANSMIT_RETRY && Producer == 3);
        PutUint32(report, sizeof(report));
        PutUint32(report + 8, reportedRate | (delivered ? 0 : 0x00010000U));
        PutUint32(report + 20, (txwi[5] & 0xffU) << 24);
        if (sim.Scenario != TRANSMIT_NO_REPORT) {
            PostDataDescriptor(report, sizeof(report));
        }
    } else {
        assert(memcmp(frame + 4, broadcast, 6) == 0);
        assert((txwi[2] & (1U << 10)) != 0); /* TXD2 multicast. */
        assert((txwi[3] & 1U) != 0); /* TXD3 no-ACK. */
        const UCHAR subtype = frame[30];
        assert(subtype == 0 || subtype == 3);
        const ULONG frameBytes = txwi[11] & 0xfffU;
        const ULONG vhtBytes = sim.AdvertiseVht ? 17 : 0;
        if (sim.AdvertiseVht) {
            static const UCHAR vht[] = {17, 14, 0, 191, 12, 0x20, 0, 0x80, 0x03,
                0xfa, 0xff, 0, 0, 0xfa, 0xff, 0, 0};
            assert(frameBytes >= sizeof(vht));
            assert(memcmp(frame + frameBytes - sizeof(vht), vht, sizeof(vht)) == 0);
        }
        if (sim.ReceivingLink && subtype == 3 && sim.ReceiverAnnouncementsSeen != 0) {
            CheckServiceMif(frame, frameBytes);
            ++sim.ServiceMifSeen;
        } else {
            assert(frameBytes == (subtype == 3 ? 289U : 273U) + vhtBytes);
        }
        AWDL_ACTION action;
        assert(AwdlDecodeAction(frame, frameBytes, &action) ==
            (subtype == 0 ? AwdlPsf : AwdlMif));
        assert(action.DeviceClass == MT7922_AWDL_DEVICE_CLASS);
        assert(action.Sync.UntilNextSlotTu >= 1 && action.Sync.UntilNextSlotTu <= 64);
        assert(action.Sync.NextChannel == sim.TransmitChannel);
        const ULONG untilSlot = action.Sync.UntilNextSlotTu;
        assert((frame[58] | (ULONG)frame[59] << 8) == (untilSlot > 48 ? untilSlot - 48 : 0));
        assert(action.Sequence.Channel[1] == sim.TransmitChannel &&
            action.Sequence.OperatingClass[1] == (sim.TransmitChannel > 14 ? 128 : 81));
        if (sim.TransmitChannel == 149) {
            assert(action.Sync.MasterChannel == 6);
            assert(action.Election.Present && action.Election.MasterCounter == 0x246U);
            assert(action.Election.MasterMetric == 0x20dU && action.Election.Distance == 3);
            /* The second address of the Election v2 TLV is the sync source. */
            const UCHAR *election2 = frame + 40 + 76 + 24 + 44 + 3;
            assert(memcmp(election2 + 6, phone, 6) == 0);
            assert(ReadLittleEndian32(election2 + 24) == 60);
            for (ULONG i = 0; i < AWDL_CHANNEL_SLOTS; ++i) {
                assert(action.Sequence.Channel[i] == (i == 1 || i == 9 ? 149 : 0));
            }
        } else {
            assert(action.Sequence.Channel[0] == sim.TransmitChannel);
        }
        const ULONG sequence = frame[22] >> 4 | (ULONG)frame[23] << 4;
        assert(sequence == MT7922_AWDL_TX_MAX_ATTEMPTS + 1U +
            sim.SyncPsfSeen + sim.SyncMifSeen + sim.AddbaSeen);
        assert(((txwi[3] >> 16) & 0xfffU) == sequence);
        if (sim.FirstSyncAt == 0) {
            sim.FirstSyncAt = sim.Counter;
        }
        if (subtype == 0) {
            ++sim.SyncPsfSeen;
        } else {
            ++sim.SyncMifSeen;
        }
        delivered = TRUE;
    }

    tx[slot].Control |= DESCRIPTOR_DONE;
    Store(TX_ACTION_RING + RING_DEVICE, Producer);
    if (!withholdCompletion) {
        const ULONG token = txwi[8] & 0x7fffU;
        PutUint32(released, 0x3001000c);
        PutUint32(released + 8, token << 16 | (delivered ? 0x0001U : 0x200fU));
        PostDataDescriptor(released, sizeof(released));
    }
    const BOOLEAN wasProbe = sim.ActionFrames < probes;
    ++sim.ActionFrames;
    sim.InjectedOnChannel = FALSE;
    sim.NextProbeFrameTime = sim.Counter +
        (wasProbe && sim.Scenario == TRANSMIT_NO_REPORT ? 1010ULL : 20ULL) * 10000;
}

static void FakeWrite(PULONG registerAddress, ULONG value)
{
    ULONG offset = Offset(registerAddress);
    if (offset == HIF_REMAP) {
        sim.SelectedBank = value;
    } else if (offset == HIF_WINDOW + WFSYS_RESET && sim.SelectedBank == WFSYS_BANK) {
        sim.ResetValue = value;
        if (value & WFSYS_RESET_RELEASE) ++sim.ResetCount;
    } else if (offset == HIF_WINDOW + CONNECTION_OWN && sim.SelectedBank == CONNECTION_BANK) {
        Store(offset, 0);
    } else if (offset == HIF_WINDOW + TOP_HOST_LPCR && sim.SelectedBank == TOP_HOST_BANK) {
        assert(value == HOST_OWN_REQUEST);
        Store(offset, sim.Scenario == TOP_OWN_TIMEOUT ? TOP_HOST_FW_OWN : 0);
    } else if (offset == 0xd6004 && sim.Scenario == SCHEDULER_WRITE_IGNORED) {
        assert(value & 0x10000000);
    } else if (offset == DMA_RESET) {
        if (sim.Scenario != RESET_IGNORED) {
            if ((value & DMA_RESET_LOGIC) == DMA_RESET_LOGIC) {
                ++sim.DmaResets;
            }
            Store(offset, value);
        }
    } else if (offset == TX_RING_START + RING_COUNT && sim.Scenario == LOCKED_RING) {
        assert(value == 0 && sim.DmaResets == 1);
    } else if (offset == DMA_GLOBAL) {
        if ((value & DMA_ENABLE) != 0) {
            assert(sim.DmaResets == 1);
            const DMA_DESCRIPTOR *controlTx = (DMA_DESCRIPTOR *)sim.CommonBuffer;
            const DMA_DESCRIPTOR *downloadTx = (DMA_DESCRIPTOR *)(sim.CommonBuffer + FWDL_RING_OFFSET);
            for (ULONG i = 0; i < TX_DESCRIPTOR_COUNT; ++i)
                assert(controlTx[i].Control == DESCRIPTOR_DONE);
            for (ULONG i = 0; i < FWDL_DESCRIPTOR_COUNT; ++i)
                assert(downloadTx[i].Control == DESCRIPTOR_DONE);
            assert(ReadStored(TX_RING_START + RING_COUNT) ==
                (sim.TransmitAwdl || sim.StationMode ? TX_ACTION_COUNT : 0U));
            assert(ReadStored(TX_RING_START + RING_BASE) ==
                (sim.TransmitAwdl || sim.StationMode ? MOCK_BUS_BASE + ACTION_RING_OFFSET : 0U));
            assert(ReadStored(TX_RING_START + RING_CPU) == 0);
            assert(ReadStored(RX_DATA_RING + RING_COUNT) ==
                (sim.BufferBytes >= CAPTURE_BUFFER_BYTES ? (ULONG)DATA_DESCRIPTOR_COUNT : 0UL));
            ++sim.DmaEnables;
            sim.DmaWasEnabled = TRUE;
        } else if (sim.Scenario == STUCK_DMA && sim.DmaWasEnabled) {
            value |= DMA_BUSY;
        }
        Store(offset, value);
    } else if (offset == TX_ACTION_RING + RING_CPU) {
        Store(offset, value);
        OnActionTx(value);
    } else if (offset == TX_MCU_RING + RING_CPU) {
        if (sim.Scenario == DOORBELL_IGNORED && (ReadStored(DMA_GLOBAL) & DMA_ENABLE))
            return;
        Store(offset, value);
        OnTx(value);
    } else if (offset == TX_FWDL_RING + RING_CPU) {
        Store(offset, value);
        OnScatter(value);
    } else {
        Store(offset, value);
    }
}

static void InjectAwdl(UCHAR Sender)
{
    UCHAR frame[147] = {
        0xd0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x7e, 0, 0, 0, 0, 1, 0x00, 0x25, 0x00, 0xff, 0x94, 0x73, 0, 0,
        0x7f, 0, 0x17, 0xf2, 8, 0x10, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        21, 2, 0, 0x34, 2, 4, 31, 0
    };
    frame[10] = Sender;
    frame[48] = sim.ListenChannel;
    frame[49] = 26;
    frame[53] = 16;
    frame[69] = 0x7e;
    frame[74] = 1;
    frame[75] = 4;
    /* Channel sequence TLV, simple encoding, 16 advertised slots. */
    frame[79] = 18;
    frame[80] = 22;
    frame[82] = 15;
    frame[85] = 3;
    frame[86] = frame[87] = 0xff;
    frame[88] = 6;
    frame[89] = 149;
    frame[90] = 44;
    frame[97] = 149;
    if (sim.StationSharedSlot || (sim.Bridge && (!sim.PublishService || sim.LinkReady))) {
        frame[77] = 4;
    } else if (sim.Wide80 && sim.FirstSyncAt != 0 && sim.MdnsSeen == 0) {
        /* Start off channel, then offer only the current slot once both queries are due. */
        frame[77] = sim.Counter - sim.FirstSyncAt < 700ULL * 10000 ? 8 : 4;
    }
    PutUint32(frame + 32, (ULONG)(sim.Counter / 10));
    if (sim.ListenChannel == 44 && !sim.TransmitAwdl) {
        frame[30] = 0; /* PSF from a Mac. */
        frame[44] = 1;
    }

    ULONG frameBytes = 104;
    if (sim.TransmitAwdl && sim.ListenChannel == 149) {
        frame[51] = 6; /* The master's channel can differ from our tuned channel. */
        frame[104] = 24;
        frame[105] = 40;
        memcpy(frame + 107, frame + 69, 6);
        PutUint32(frame + 119, 0x246U);
        PutUint32(frame + 123, 2);
        PutUint32(frame + 127, 0x20dU);
        frameBytes = sizeof(frame);
    }
    UCHAR record[24 + sizeof(frame)] = {0};
    PutUint32(record, (2U << 27) | (24U + frameBytes));
    memcpy(record + 24, frame, frameBytes);
    PostDataDescriptor(record, 24U + frameBytes);
    sim.InjectedOnChannel = TRUE;
}

static void PostWatchTcp(UCHAR sender, UCHAR receiver, UCHAR flags, ULONG payload)
{
    UCHAR source[6] = {sender, 0, 0, 0, 0, 1};
    UCHAR dest[6] = {receiver, 0, 0, 0, 0, 1};
    UCHAR ip[61] = {0};
    assert(payload <= 1);
    ip[0] = 0x60;
    ip[5] = (UCHAR)(20U + payload);
    ip[6] = 6;
    ip[7] = 64;
    LinkLocalFromMac(source, ip + 8);
    LinkLocalFromMac(dest, ip + 24);
    ip[52] = 0x50;
    ip[53] = flags;
    const USHORT checksum = Ipv6TransportChecksum(ip + 8, ip + 24, 6, ip + 40, 20U + payload);
    ip[56] = (UCHAR)(checksum >> 8);
    ip[57] = (UCHAR)checksum;
    UCHAR record[24 + 40 + sizeof(ip)] = {0};
    const ULONG bytes = (ULONG)AwdlBuildIpFrame(record + 24, sizeof(record) - 24,
        dest, source, ip, 60U + payload, 1) + 24;
    PutUint32(record, (2U << 27) | bytes);
    PostDataDescriptor(record, bytes);
}

static ULONGLONG FakeTime(void) { return sim.Counter; }

static ULONGLONG FakePreciseTime(ULONGLONG *Counter)
{
    *Counter = sim.Counter;
    return sim.Counter;
}

static NTSTATUS FakeDelay(int mode, BOOLEAN alertable, PLARGE_INTEGER interval)
{
    assert(mode == 0 && !alertable && interval->QuadPart < 0);
    sim.Counter += (ULONGLONG)(-interval->QuadPart);
    if (sim.Bridge && sim.PublishService && sim.TxRateCode == MT7922_OFDM_12_MBPS &&
        sim.SyncMifSeen == 1 && !sim.DelayedProbe) {
        /* Discovering a peer before the old cutoff does not make its first
           usable data window arrive before 15 seconds. */
        sim.Counter += 16000ULL * 10000;
        sim.DelayedProbe = TRUE;
    }
    if (sim.Scenario == TRANSMIT_ACK && sim.DataSeen == 1 && !sim.DelayedProbe) {
        sim.Counter += 100ULL * 10000;
        sim.DelayedProbe = TRUE;
    }
    if ((sim.Scenario == RECEIVE_AWDL || sim.Scenario == TRANSMIT_ACK ||
            sim.Scenario == TRANSMIT_NO_ACK || sim.Scenario == TRANSMIT_NO_REPORT ||
            sim.Scenario == TRANSMIT_RETRY) && sim.SnifferEnabled &&
        sim.ListenChannel != 0 && !sim.InjectedOnChannel &&
        sim.Counter >= sim.NextProbeFrameTime && ReadStored(RX_DATA_RING + RING_COUNT) != 0) {
        if (sim.Wide80 && sim.ActionFrames == 0) {
            InjectAwdl(0x8e);
        }
        InjectAwdl(0x7e);
        if (sim.Watching) {
            PostWatchTcp(0x7e, 0x8e, AWDL_TCP_SYN, 0);
            PostWatchTcp(0x8e, 0x7e, AWDL_TCP_SYN | AWDL_TCP_ACK, 0);
            PostWatchTcp(0x7e, 0x8e, AWDL_TCP_ACK, 1);
            PostWatchTcp(0x7e, 0x9e, AWDL_TCP_SYN, 0);
        }
    }
    if (sim.TransmitAwdl && sim.SnifferEnabled && !sim.AddbaInjected &&
        (sim.Bridge ? sim.FirstSyncAt != 0 : sim.Counter > 2000000ULL) &&
        ReadStored(RX_DATA_RING + RING_COUNT) != 0) {
        PostAddbaRequest(0x7e, 0x5a);
        sim.AddbaInjected = TRUE;
    }
    return STATUS_SUCCESS;
}

static PEX_TIMER FakeAllocateTimer(void *callback, void *context, ULONG attributes)
{
    assert(callback == NULL && context == NULL && attributes == EX_TIMER_HIGH_RESOLUTION);
    assert(!mockPollTimer.Allocated && !mockPollTimer.Armed);
    mockPollTimer.Allocated = TRUE;
    return &mockPollTimer;
}

static BOOLEAN FakeSetTimer(PEX_TIMER timer, LONGLONG dueTime, LONGLONG period, void *parameters)
{
    assert(timer == &mockPollTimer && timer->Allocated && !timer->Armed);
    assert((dueTime == -10000 || dueTime == -2500) && period == 0 && parameters == NULL);
    timer->DueTime = dueTime;
    timer->Armed = TRUE;
    return FALSE;
}

static NTSTATUS FakeWaitOnTimer(PEX_TIMER timer, int reason, int mode, BOOLEAN alertable, PLARGE_INTEGER timeout)
{
    assert(timer == &mockPollTimer && timer->Allocated && timer->Armed);
    assert(reason == Executive && timeout == NULL);
    timer->Armed = FALSE;
    LARGE_INTEGER interval;
    interval.QuadPart = timer->DueTime;
    return FakeDelay(mode, alertable, &interval);
}

static BOOLEAN FakeDeleteTimer(PEX_TIMER timer, BOOLEAN cancel, BOOLEAN wait, void *parameters)
{
    assert(timer == &mockPollTimer && timer->Allocated && cancel && wait && parameters == NULL);
    timer->Allocated = FALSE;
    timer->Armed = FALSE;
    return FALSE;
}

static NTSTATUS FakeAllocate(WDFDMAENABLER enabler, SIZE_T bytes, PVOID attr,
    WDFCOMMONBUFFER *buffer)
{
    assert((enabler == NULL || (sim.StationMode && enabler == &sim)) && attr == NULL && sim.CommonBuffer == NULL);
    sim.CommonBuffer = calloc(1, bytes);
    assert(sim.CommonBuffer != NULL);
    sim.BufferBytes = bytes;
    *buffer = sim.CommonBuffer;
    return STATUS_SUCCESS;
}

static PHYSICAL_ADDRESS FakeBusAddress(WDFCOMMONBUFFER buffer)
{
    assert(buffer == sim.CommonBuffer);
    PHYSICAL_ADDRESS address;
    address.QuadPart = MOCK_BUS_BASE;
    return address;
}

static SIZE_T FakeLength(WDFCOMMONBUFFER buffer)
{
    assert(buffer == sim.CommonBuffer);
    return sim.BufferBytes;
}

static void FakeDelete(WDFCOMMONBUFFER buffer)
{
    assert(buffer == sim.CommonBuffer);
    assert((ReadStored(DMA_GLOBAL) & (DMA_ENABLE | DMA_BUSY)) == 0
        || (!sim.DmaWasEnabled && ReadStored(TX_MCU_RING + RING_BASE) != MOCK_BUS_BASE));
    free(buffer);
    sim.CommonBuffer = NULL;
}

#define LINK_PACKETS (MT7922_AWDL_DATA_PROBES + 8U)

static BOOLEAN TestLinkStopped(void *context)
{
    assert(context == &sim);
    return sim.LinkReceived == LINK_PACKETS && (!sim.ReceivingLink || sim.ServiceMifSeen != 0);
}

static void TestLinkReady(void *context, const UCHAR *local, const UCHAR *peer)
{
    assert(context == &sim && !sim.LinkReady);
    assert(local[0] == 0xfe && local[1] == 0x80 && local[15] == 1);
    assert(peer[0] == 0xfe && peer[1] == 0x80 && peer[15] == 1);
    assert(local[9] == 0x44 && peer[9] == 0);
    if (sim.PublishService && !sim.LateDiscovery) {
        assert(sim.MdnsSeen == 1 && sim.PingSeen > 0 && sim.TcpSynSeen == 2 && sim.TcpResetSeen == 1);
    }
    sim.PingBeforeLink = sim.PingSeen;
    sim.LinkReady = TRUE;
    if (sim.ReceivingLink && sim.PublishService) {
        /* An upload starting after the old two-minute limit must still run. */
        sim.Counter += (AWDL_LINK_MAX_MS + 1000ULL) * 10000ULL;
    }
}

static void TestLinkReceive(void *context, const UCHAR *packet, ULONG length)
{
    assert(context == &sim && sim.LinkReady && length == 48 && packet[40] == 129);
    if (sim.LateDiscovery && !sim.RecoveryEchoReceived) {
        assert(sim.RecoveryEchoSeen == 1);
        sim.RecoveryEchoReceived = TRUE;
        ++sim.PingBeforeLink;
        return;
    }
    assert(ReadBigEndian16(packet + 46) == sim.PingBeforeLink + sim.LinkReceived + 1);
    ++sim.LinkReceived;
}

static ULONG TestLinkSend(void *context, UCHAR *packet, USHORT *rateCode, ULONGLONG *enqueuedAt)
{
    *rateCode = 0;
    *enqueuedAt = sim.Counter;
    assert(context == &sim && sim.LinkReady);
    if (sim.LinkSubmitted == LINK_PACKETS || (sim.LateDiscovery && !sim.RecoveryEchoReceived) ||
        (!sim.PublishService && sim.AddbaSeen < AWDL_ADDBA_MAX_RESPONSES)) {
        return 0;
    }
    static const UCHAR phone[] = {0x7e, 0, 0, 0, 0, 1};
    static const UCHAR station[] = {0x02, 0x44, 0x33, 0x22, 0x11, 0x01};
    if (sim.ReceivingLink && sim.ReceiverAnnouncementsSeen == 0) {
        const ULONG bytes = BuildMdnsQuery(packet, MT7922_AWDL_LINK_MTU, station, NULL);
        assert(AwdlIsMdnsMulticast(packet, bytes));
        return bytes;
    }
    ++sim.LinkSubmitted;
    ZeroMemory(packet, MT7922_AWDL_LINK_MTU);
    assert(BuildIcmp6Echo(packet, MT7922_AWDL_LINK_MTU, station, phone,
        (USHORT)sim.LinkSubmitted) == 56);
    packet[4] = (MT7922_AWDL_LINK_MTU - 40) >> 8;
    packet[5] = (UCHAR)((MT7922_AWDL_LINK_MTU - 40) & 0xffU);
    packet[MT7922_AWDL_LINK_MTU - 1] = (UCHAR)sim.LinkSubmitted;
    packet[42] = packet[43] = 0;
    const USHORT checksum = Ipv6TransportChecksum(packet + 8, packet + 24, 58,
        packet + 40, MT7922_AWDL_LINK_MTU - 40);
    packet[42] = (UCHAR)(checksum >> 8);
    packet[43] = (UCHAR)checksum;
    return MT7922_AWDL_LINK_MTU;
}

static void VerifyImageScenarioWithOptions(SCENARIO scenario, NTSTATUS expected, ULONG expectedStage,
    const UCHAR *image, SIZE_T imageBytes, BOOLEAN wide80, BOOLEAN publishService, BOOLEAN bridge, USHORT rateCode,
    MT7922_AWDL_WATCH *watch, BOOLEAN receivingLink, BOOLEAN lateDiscovery, BOOLEAN advertiseVht)
{
    ZeroMemory(&sim, sizeof(sim));
    sim.Registers = calloc(1, REGISTER_BYTES);
    assert(sim.Registers != NULL);
    sim.Scenario = scenario;
    sim.Wide80 = wide80;
    sim.AdvertiseVht = advertiseVht;
    sim.Watching = watch != NULL;
    sim.PublishService = publishService;
    sim.Bridge = bridge;
    sim.ReceivingLink = receivingLink;
    sim.LateDiscovery = lateDiscovery;
    sim.TxRateCode = rateCode;
    sim.SelectedBank = 0x7001;
    sim.ResetValue = 0x11;
    sim.FirmwareState = 1;
    sim.FactoryImage = image;
    sim.ImageBytes = imageBytes;
    Store(MCU_FIRMWARE_MODE, 3);
    Store(DMA_RESET, DMA_RESET_LOGIC);
    Store(DMA_GLOBAL, 0x1010b870);
    Store(DMA_GLOBAL_EXT, DMA_TX_SCHEDULING);
    Store(PCIE_MAC_PM, 0x10);
    Store(HIF_WINDOW + TOP_HOST_LPCR, TOP_HOST_FW_OWN);
    if (scenario == STALE_RING || scenario == LOCKED_RING || scenario == LIVE_DMA ||
        scenario == RESET_IGNORED) {
        Store(TX_RING_START + RING_BASE, 0x02e00000);
        Store(TX_RING_START + RING_COUNT, 0x200);
        Store(TX_RING_START + RING_CPU, 72);
    }
    if (scenario == STALE_RING) {
        const ULONG rxDataRing = RX_RING_START + 2 * RING_STRIDE;
        Store(rxDataRing + RING_BASE, 0x02f00000);
        Store(rxDataRing + RING_COUNT, 0x600);
        Store(rxDataRing + RING_CPU, 201);
    }
    if (scenario == LIVE_DMA) {
        Store(DMA_GLOBAL, ReadStored(DMA_GLOBAL) | DMA_ENABLE | DMA_BUSY);
    }

    MT7922_MCU_PROBE_INFO result = {0};
    WDFCOMMONBUFFER retained = NULL;
    sim.TransmitAwdl = scenario == TRANSMIT_ACK || scenario == TRANSMIT_NO_ACK ||
        scenario == TRANSMIT_NO_REPORT || scenario == TRANSMIT_NO_PEER || scenario == TRANSMIT_RETRY;
    BOOLEAN receive = scenario == RECEIVE_AWDL || scenario == RECEIVE_EMPTY || scenario == RECEIVE_REJECTED ||
        sim.TransmitAwdl;
    sim.TransmitChannel = !sim.TransmitAwdl ? 0 :
        scenario == TRANSMIT_NO_REPORT ? 44 : scenario == TRANSMIT_RETRY ? 149 : 6;
    static const UCHAR targetPeer[MT7922_AWDL_PEER_BYTES] = {0x7e, 0, 0, 0, 0, 1};
    MT7922_AWDL_LINK link = {
        .Context = &sim, .Stopped = TestLinkStopped, .Ready = TestLinkReady,
        .Receive = TestLinkReceive, .TakeSend = TestLinkSend
    };
    link.AnnounceMdns = receivingLink;
    link.AdvertiseVht = advertiseVht;
    link.ServiceLabel = receivingLink ? TestAirDropLabel : NULL;
    link.ReceivePort = 8771;
    assert(Mt7922RunMcuProbe(sim.Registers, 0x7922, NULL, &retained, image, imageBytes,
        receive, sim.TransmitChannel, wide80, wide80 && watch == NULL ? targetPeer : NULL,
        wide80 && watch == NULL && (!bridge || publishService) ? 8770 : 0,
        rateCode, bridge ? &link : NULL, watch, &result) == expected);
    if (watch != NULL) {
        assert(result.Stage == MT7922_MCU_COMPLETE && result.CleanupStatus == 0);
        assert(sim.ActionFrames == 0 && !sim.SnifferEnabled && retained == NULL && sim.CommonBuffer == NULL);
        const MT7922_AWDL_WATCH_DIRECTION *first = &watch->Info.From[0];
        const MT7922_AWDL_WATCH_DIRECTION *second = &watch->Info.From[1];
        assert(first->SyncFrames == 1 && second->SyncFrames == 1);
        assert(first->DataFrames == 2 && first->Ipv6Packets == 2);
        assert(first->TcpSyn == 1 && first->TcpSynAck == 0 && first->TcpPayload == 1);
        assert(second->DataFrames == 1 && second->Ipv6Packets == 1);
        assert(second->TcpSyn == 0 && second->TcpSynAck == 1 && second->TcpPayload == 0);
        free(sim.Registers);
        return;
    }
    if (bridge) {
        assert(result.Stage == MT7922_MCU_COMPLETE && result.CleanupStatus == 0);
        assert(sim.LinkReady && sim.LinkSubmitted == LINK_PACKETS && sim.LinkReceived == LINK_PACKETS);
        assert(link.Submitted == LINK_PACKETS && link.Acked == LINK_PACKETS && link.NoAck == 0);
        assert(link.ReportRate == rateCode && result.TxReportRate == rateCode);
        assert(retained == NULL && sim.CommonBuffer == NULL);
        assert(result.AddbaResponses == AWDL_ADDBA_MAX_RESPONSES && result.AddbaAcked == 0);
        if (publishService) {
            assert(link.SharedMifReleased > 0 && link.SharedMifReleased <= result.SyncMifSent);
            if (rateCode == MT7922_OFDM_12_MBPS) {
                assert(sim.DelayedProbe);
            }
            assert(result.PingReplies > 0);
            if (lateDiscovery) {
                assert(link.RecoveryEchoSent == 1 && link.RecoveryEchoAcked == 1 && sim.RecoveryEchoReceived);
                assert(result.MdnsAirDropAnswers == 0 && result.TcpSynSent == 0);
            } else {
                assert(link.RecoveryEchoSent == 0);
                assert(result.MdnsAirDropAnswers == 1);
                assert(result.TcpSynAck == 1 && result.TcpSynSent == 2 && result.TcpResetSent == 1);
            }
        } else {
            assert(result.MdnsQueriesSent == 0 && result.DataProbeSent == 0);
        }
        assert(sim.MdnsSeen == result.MdnsQueriesSent + sim.ReceiverAnnouncementsSeen);
        assert(sim.ReceiverAnnouncementsSeen == (receivingLink ? 1UL : 0UL));
        assert(link.MdnsAnnounced == sim.ReceiverAnnouncementsSeen);
        assert(!receivingLink || (link.SharedMifReleased > 0 && sim.ServiceMifSeen > 0));
        assert(link.ServiceMifsReleased == sim.ServiceMifSeen);
        assert(link.VhtAdverts == (advertiseVht ? result.SyncReleased : 0));
        free(sim.Registers);
        return;
    }
    assert(result.Stage == expectedStage);
    assert(sim.SelectedBank == 0x7001);
    BOOLEAN refused = scenario == LIVE_DMA || scenario == LOCKED_RING ||
        scenario == RESET_IGNORED || scenario == SCHEDULER_WRITE_IGNORED ||
        scenario == BOOT_CORRUPT;
    assert(sim.DmaEnables == (refused ? 0UL : 1UL));

    if (scenario == NORMAL) {
        assert(result.GetReply == 2 && result.ReleaseReply == 3 && sim.Commands == 2);
        assert(result.LastTxRing == TX_MCU_RING && result.LastTxHostIndex == 2);
        assert((result.LastTxDescriptorControl & DESCRIPTOR_DONE) != 0);
        assert(result.DmaInterruptStatus != 0);
    }
    if (scenario == TX_TIMEOUT || scenario == DOORBELL_IGNORED) {
        assert(result.LastTxRing == TX_MCU_RING && sim.Commands == 0);
        assert(result.LastTxDescriptorControl == 0x40440000);
        assert(result.DmaInterruptStatus == 0);
        if (scenario == TX_TIMEOUT) {
            assert(result.LastTxHostIndex == 1 && result.BlockedRegister == TX_MCU_RING + RING_DEVICE);
        } else {
            assert(result.LastTxHostIndex == 0 && result.BlockedRegister == TX_MCU_RING + RING_CPU);
        }
        assert(result.BlockedValue == 0);
    }
    if (scenario == SCHEDULER_WRITE_IGNORED) {
        assert(result.BlockedRegister == 0xd6004 && result.BlockedValue == 0);
    }
    if (scenario == TOP_OWN_TIMEOUT) {
        assert(result.BlockedRegister == 0x18060010 && result.BlockedValue == TOP_HOST_FW_OWN);
        assert(sim.Commands == 0);
    }
    if (scenario == PATCH_PRESENT) {
        assert(result.GetReply == 1 && result.ReleaseReply == 0xffffffffUL && sim.Commands == 1);
    }
    if (scenario == TX_TIMEOUT || scenario == RX_TIMEOUT || scenario == RELEASE_FAILED) {
        assert(sim.ResetCount == 2);
    }
    if (scenario == STALE_RING) {
        assert(result.BlockedRegister == 0 && result.BlockedValue == 0);
        assert(result.GetReply == 2 && result.ReleaseReply == 3 && sim.Commands == 2);
        assert(ReadStored(RX_RING_START + 2 * RING_STRIDE + RING_BASE) == 0);
        assert(ReadStored(RX_RING_START + 2 * RING_STRIDE + RING_CPU) == 0);
    }
    if (scenario == LOCKED_RING) {
        assert(result.BlockedRegister == TX_RING_START + RING_COUNT && result.BlockedValue == 0x200);
        assert(sim.Commands == 0 && sim.DmaResets == 1);
    }
    if (scenario == LIVE_DMA || scenario == RESET_IGNORED) {
        assert(result.BlockedRegister == (ULONG)(scenario == LIVE_DMA ? DMA_GLOBAL : DMA_RESET));
        assert(ReadStored(TX_RING_START + RING_COUNT) == 0x200);
        assert(sim.Commands == 0 && sim.DmaResets == 0);
        if (scenario == LIVE_DMA) {
            assert(sim.ResetCount == 0);
        }
    }
    if (scenario == BOOT_ROM_REJECT_PATCH) {
        assert(sim.Commands == 3 && sim.ScatterPackets == 0);
        assert(result.GetReply == 2 && result.ReleaseReply == 3);
        assert(result.LastCommand == 0x10 && result.RomResultCommand == 0x05);
        assert(result.RomResultStatus == 1 && result.RomResultBytes == 36);
    }
    if (scenario == FULL_BOOT || scenario == BOOT_NOT_READY || scenario == BOOT_CAP_ROM_RING ||
        scenario == BOOT_CAP_TIMEOUT || scenario == BOOT_CAP_MALFORMED || scenario == BOOT_CAP_NO_PHY ||
        scenario == BOOT_FIXED_READY || scenario == BOOT_ROM_REJECT_START || scenario == BOOT_ROM_SHORT) {
        const BOOLEAN rejected = scenario == BOOT_ROM_REJECT_START || scenario == BOOT_ROM_SHORT;
        const BOOLEAN noRuntime = rejected || scenario == BOOT_NOT_READY;
        assert(sim.Commands == (noRuntime ? 10UL : 11UL));
        assert(sim.ScatterPackets == 219 && result.DownloadPackets == 219);
        assert(result.DownloadDonePackets == 219);
        assert(result.DownloadBytes == MT7922_FACTORY_IMAGE_BYTES && result.DownloadDmaIndex == 91);
        assert(sim.ResetCount == 2);
        assert(result.RxDmaIndex == (scenario == BOOT_CAP_ROM_RING ? 3UL : 2UL));
        assert(result.RuntimeRxDmaIndex ==
            (noRuntime || scenario == BOOT_CAP_TIMEOUT || scenario == BOOT_CAP_ROM_RING ? 0UL : 1UL));
        assert(result.RomResultCommand == 0x02);
        assert(result.RomResultBytes == (scenario == BOOT_ROM_SHORT ? 32UL : 36UL));
        if (rejected) {
            assert(result.RomResultStatus == (scenario == BOOT_ROM_SHORT ? MAXULONG : 3UL));
            assert(result.FirmwareReady == 0 && result.FirmwareReadyRemapped == MAXULONG);
        } else {
            assert(result.RomResultStatus == 0);
            assert(result.FirmwareReady == (scenario == BOOT_NOT_READY ? 1UL : 3UL));
            assert(result.FirmwareReadyRemapped == (scenario == BOOT_FIXED_READY ? 0UL : result.FirmwareReady));
        }
        if (scenario == BOOT_NOT_READY) {
            assert(result.BlockedRegister == 0xe00f0 && result.BlockedValue == 1);
        }
    }
    if (scenario == FULL_BOOT || scenario == BOOT_CAP_ROM_RING || scenario == BOOT_PATCH_PRESENT ||
        scenario == BOOT_FIXED_READY || receive) {
        assert(result.NicBands == 7 && result.NicSpatialStreams == 2);
        assert(result.NicBandwidthCode == 3 && result.NicPhyFlags == 7);
    } else {
        assert(result.NicBands == 0 && result.NicSpatialStreams == 0 && result.NicPhyFlags == 0);
    }
    if (scenario == BOOT_PATCH_PRESENT) {
        assert(sim.Commands == 8 && sim.ScatterPackets == 185 && result.DownloadBytes == 752208);
        assert(result.FirmwareReady == 3 && sim.ResetCount == 2);
    }
    if (scenario == BOOT_SCATTER_TIMEOUT) {
        assert(sim.Commands == 2 && result.DownloadPackets == 5 && sim.ResetCount == 2);
        assert(result.ReleaseReply == MAXULONG);
    }
    if (receive) {
        assert(result.FirmwareReady == 3 && sim.ResetCount == 2);
        const BOOLEAN reference = imageBytes == MT7922_REFERENCE_IMAGE_BYTES;
        assert(result.DownloadBytes == imageBytes);
        assert(result.DownloadPackets == (reference ? 220UL : 219UL));
        assert(ReadStored(MCU_FIRMWARE_MODE) == (reference ? 0UL : 3UL));
        if (sim.TransmitAwdl) {
            assert(ReadStored(TX_ACTION_RING + RING_BASE) == 0);
            assert(ReadStored(TX_ACTION_RING + RING_COUNT) == 0);
            if (scenario == TRANSMIT_NO_PEER) {
                assert(result.TxPeerFound == 0 && result.DataTxDmaIndex == 0);
                assert(result.TxReportSeen == 0 && result.TxReleaseSeen == 0);
            } else {
                const BOOLEAN delivered = scenario == TRANSMIT_ACK ||
                    scenario == TRANSMIT_NO_REPORT || scenario == TRANSMIT_RETRY;
                const ULONG probes = scenario == TRANSMIT_NO_ACK ? 3U
                    : scenario == TRANSMIT_RETRY ? 3U : 1U;
                const ULONG totalFrames = probes + result.SyncPsfSent + result.SyncMifSent
                    + result.DataProbeSent + result.MgmtProbeSent + result.AddbaResponses + result.MdnsQueriesSent;
                assert(result.TxPeerFound == 1);
                assert(result.DataTxDmaIndex == totalFrames % TX_ACTION_COUNT);
                assert(result.TxProbes == probes && result.TxReleaseSeen == 1);
                assert(result.TxReleaseStatus == (delivered ? 0UL : 1UL));
                assert(result.TxAttempts == (delivered ? 1UL : 15UL));
                assert(result.TxRxTypeCount[6] != 0 && result.TxReleaseSampleBytes != 0);
                if (scenario == TRANSMIT_NO_REPORT) {
                    assert(result.TxReportSeen == 0 && result.TxProbe[0].TxStatusRecords == 0);
                } else {
                    assert(result.TxReportSeen == 1 && result.TxReportRate == 0x4b);
                    assert(result.TxReportAckErrors == (scenario == TRANSMIT_NO_ACK ? 1UL : 0UL));
                }
                if (delivered) {
                    assert(result.TxStatusSampleBytes >= 8 && result.TxRxTypeCount[0] != 0);
                }
                if (scenario == TRANSMIT_RETRY) {
                    assert(result.TxProbe[0].AckErrors == 1 && result.TxProbe[1].AckErrors == 1);
                    assert(result.TxProbe[2].AckErrors == 0 && result.TxProbe[2].TxStatusRecords == 1);
                }
                assert(result.SelfFrames == 0 && result.ControlFrames == 0 && result.AcksToUs == 0);
                assert(result.PeerMentions == (sim.Wide80 ? result.PingReplies : 0));
                if (delivered) {
                    assert(result.SyncPsfSent > 0 && result.SyncMifSent > 0);
                    assert(result.SyncFailed == 0);
                    assert(result.SyncReleased == result.SyncPsfSent + result.SyncMifSent);
                    assert(result.SyncPeerFrames > 0);
                    assert(sim.SyncPsfSeen == result.SyncPsfSent);
                    assert(sim.SyncMifSeen == result.SyncMifSent);
                    const ULONG timedOut = scenario == TRANSMIT_ACK ? 1U : 0U;
                    assert(result.UnicastReleaseTimeouts == timedOut);
                    if (timedOut != 0) {
                        assert(result.ProbeWindowsMissed >= 1 && sim.BroadcastDuringUnicast != 0);
                    } else {
                        assert(result.ProbeWindowsMissed == 0);
                    }
                    const ULONG reservedTcp = wide80 && !publishService ? 2U : 0U;
                    assert(result.DataProbeSent + result.AddbaResponses == MT7922_AWDL_DATA_PROBES - reservedTcp);
                    assert(result.DataProbeAcked == result.DataProbeSent - timedOut);
                    assert(result.DataProbeFailed == timedOut);
                    assert(result.DataProbeAckErrors == 0);
                    assert(result.DataProbeReleaseStatus == 0);
                    assert(result.DataPeer[0] == 0x7e);
                    assert(sim.DataSeen == result.DataProbeSent);
                    for (ULONG i = 0; i < result.DataProbeSent; ++i) {
                        const ULONG offset = result.DataProbeOffset[i];
                        assert(offset == 6 || offset == 22 || offset == 38 || offset == 54);
                        assert(result.DataProbeResult[i] == (i >= timedOut ? 1U : 0U));
                    }
                    assert(result.MgmtProbeSent == 0 && sim.MgmtSeen == 0);
                    assert(result.PingSent + result.MdnsDirectQueries + result.TcpSynSent +
                        result.TcpResetSent == result.DataProbeSent);
                    assert(result.TcpSynSent == sim.TcpSynSeen && result.TcpResetSent == sim.TcpResetSeen);
                    assert(result.TcpRefused == 0);
                    if (publishService) {
                        assert(result.TcpSynSent == 2 && result.TcpSynAck == 1 && result.TcpResetSent == 1);
                    } else {
                        assert(result.TcpSynSent == 0 && result.TcpSynAck == 0 && result.TcpResetSent == 0);
                    }
                    assert(result.MdnsDirectQueries == sim.DirectMdnsSeen);
                    assert(result.MdnsDirectAcked == sim.DirectMdnsSeen);
                    assert(sim.PingSeen == result.PingSent);
                    assert(result.PingReplies == result.PingSent - timedOut);
                    assert(result.PeerAddressed >= result.PingReplies);
                    assert(result.PeerAddr[0][0] == 0x7e);
                    assert(result.PeerFrames[0] != 0 && result.DataPeerChanges == 0);
                    if (scenario == TRANSMIT_ACK) {
                        assert(result.PeerCount == 2 && result.PeerAddr[1][0] == 0x8e);
                        assert(result.PeerFrames[1] == 1);
                    } else {
                        assert(result.PeerCount == 1);
                    }
                    for (ULONG i = 0; i < result.DataProbeSent; ++i) {
                        assert(result.ProbeSubmitUs[i] >= result.ProbePlanUs[i]);
                        assert(result.ProbeSubmitUs[i] - result.ProbePlanUs[i] <=
                            AWDL_PROBE_MAX_LATE_TICKS / 10);
                        const ULONG syncMs = wide80 ? MT7922_AWDL_TCP_SYNC_MS : MT7922_AWDL_SYNC_MS;
                        assert(result.ProbePeerDeltaUs[i] <= syncMs * 1000U);
                    }
                    assert(result.PeerSamples == 3);
                    const ULONG addbaCount = scenario == TRANSMIT_ACK ? AWDL_ADDBA_MAX_RESPONSES
                        : scenario == TRANSMIT_RETRY ? 0U : 1U;
                    const ULONG addbaRequests = scenario == TRANSMIT_ACK ? addbaCount + 1U : 1U;
                    assert(result.AddbaRequests == addbaRequests);
                    assert(result.RxBaAttempts == 1 && sim.RxBaWtbl == 1 && sim.RxBaSta == 1);
                    assert(result.RxBaConfigured == (scenario == TRANSMIT_RETRY ? 0U : 1U));
                    assert(result.RxBaLastStatus ==
                        (scenario == TRANSMIT_RETRY ? (ULONG)STATUS_DEVICE_PROTOCOL_ERROR : 0));
                    assert(result.RxBaFirmwareReply == (scenario == TRANSMIT_RETRY ? 0xc00000bbUL : 0));
                    assert(result.AddbaResponses == addbaCount && result.AddbaAcked == addbaCount);
                    assert(result.AddbaPeerAddr[0] == 0x7e);
                    assert(result.PeerQos == (sim.Wide80 ? result.PingReplies : 0));
                    assert(sim.AddbaSeen == addbaCount);
                    assert(result.AwdlRxDataSplit == (sim.Wide80 ? result.PingReplies : 0));
                    assert(result.WrappedIpv6Packets == result.AwdlRxDataSplit);
                    assert(result.NormalRxRawTranslationError == result.AwdlRxDataSplit);
                    assert(result.Ipv6PacketsToUs == result.PingReplies + result.MdnsResponses +
                        result.MdnsOtherResponses + sim.TcpSynSeen);
                    if (sim.Wide80) {
                        assert(result.AwdlRxHeaders[1].AmsduPart == 3 &&
                            result.AwdlRxHeaders[1].SampleBytes == MT7922_AWDL_RX_HEADER_BYTES);
                        assert(result.AwdlRxHeaders[1].Bytes[42] == 0xaa);
                    }
                    assert(result.PingLastTtl == 64);
                    assert(result.MdnsQueriesSent == sim.MdnsSeen &&
                        result.MdnsQueriesReleased == sim.MdnsSeen);
                    assert(result.MdnsCurrentWindowQueries == (sim.Wide80 ? 1U : 0U));
                    assert(result.MdnsSampleCount == result.MdnsResponses);
                    if (result.MdnsSampleCount != 0) {
                        assert(result.MdnsSamples[0].SourceIpv6[8] == (0x7e ^ 2));
                    }
                    assert(result.PeerIpv6Frames == result.PingReplies + result.MdnsResponses + sim.TcpSynSeen);
                    assert(result.AwdlRxDataSelected == result.PeerIpv6Frames);
                    assert(result.AwdlRxDataOther == result.MdnsOtherResponses);
                    assert(result.AwdlRxDataToUs == result.AwdlRxDataSelected + result.AwdlRxDataOther);
                    const ULONG skipped = scenario == TRANSMIT_NO_REPORT ? 1U : 0U;
                    assert(result.NormalRxSkipped == 4 * skipped);
                    assert(result.NormalRxBadLength == skipped && result.NormalRxBadGroups == skipped);
                    assert(result.NormalRxFcsErrors == skipped && result.NormalRxTranslated == skipped &&
                        result.NormalRxAmsduErrors == skipped);
                    if (scenario == TRANSMIT_ACK || publishService) {
                        assert(sim.MdnsSeen == 1 && sim.DirectMdnsSeen == 0);
                        assert(result.MdnsResponses == 1 && result.MdnsAirDropAnswers == 1);
                    } else if (scenario == TRANSMIT_NO_REPORT) {
                        assert(sim.DirectMdnsSeen == 1);
                        assert(result.MdnsResponses == 1 && result.MdnsAirDropAnswers == 1);
                        assert(result.MdnsOtherResponses == 1 && result.MdnsOtherAirDropAnswers == 1);
                    } else {
                        assert(sim.MdnsSeen == AWDL_MDNS_MAX_QUERIES &&
                            sim.DirectMdnsSeen == AWDL_MDNS_MAX_QUERIES);
                        assert(result.MdnsResponses == 0 && result.MdnsAirDropAnswers == 0);
                        assert(result.MdnsOtherResponses == 0 && result.MdnsOtherAirDropAnswers == 0);
                        if (scenario == TRANSMIT_RETRY) {
                            BOOLEAN directOutsideLastWindow = FALSE;
                            for (ULONG i = 0; i < result.DataProbeSent; ++i) {
                                if ((result.MdnsDirectProbeMask & (1U << i)) != 0 &&
                                    result.DataProbeOffset[i] != 54) {
                                    directOutsideLastWindow = TRUE;
                                }
                            }
                            assert(directOutsideLastWindow);
                        }
                    }
                } else {
                    assert(result.SyncPsfSent == 0 && result.SyncMifSent == 0);
                    assert(result.DataProbeSent == 0 && result.MgmtProbeSent == 0);
                    assert(result.PingSent == 0 && result.PingReplies == 0);
                    assert(result.PeerAddressed == 0);
                }
            }
        } else if (scenario == RECEIVE_REJECTED) {
            assert(result.MonitorReply == 0xc00000bb && sim.Commands == 15);
            assert(result.DataRxDmaIndex == 0);
        } else {
            assert(result.MonitorReply == 0 && sim.Commands == 18 && !sim.SnifferEnabled);
            assert(result.Capture[0].Channel == 6 && result.Capture[1].Channel == 44);
            assert(result.Capture149.Channel == 149);
            const BOOLEAN heard = scenario == RECEIVE_AWDL;
            assert(result.Capture149Status == (heard ? 0 : (ULONG)STATUS_DEVICE_PROTOCOL_ERROR));
            assert(result.Capture149FirmwareReply == (heard ? 0 : 0xc00000bbUL));
            assert(sim.Counter >= (ULONGLONG)MT7922_AWDL_PROBE_DWELL_MS * (heard ? 30000U : 20000U));
            for (ULONG i = 0; i < MT7922_AWDL_PROBE_CHANNELS; ++i) {
                assert(result.Capture[i].Descriptors == (scenario == RECEIVE_AWDL ? 1UL : 0UL));
                assert(result.Capture[i].Invalid == 0);
            }
            assert(result.Capture[0].Mif == (scenario == RECEIVE_AWDL ? 1UL : 0UL));
            assert(result.Capture[0].Ios == result.Capture[0].Mif);
            assert(result.Capture[1].Psf == (scenario == RECEIVE_AWDL ? 1UL : 0UL));
            assert(result.Capture[1].Ios == 0);
            assert(result.Capture149.Descriptors == (heard ? 1UL : 0UL));
            assert(result.Capture149.Ios == (heard ? 1UL : 0UL));
            assert(result.DataRxDmaIndex == (heard ? 3UL : 0UL));
            assert(result.SurveyPeerCount == (heard ? 1UL : 0UL));
            if (heard) {
                const MT7922_AWDL_SURVEY_PEER *peer = &result.SurveyPeers[0];
                assert(peer->Address[0] == 0x7e && peer->Received[0] == 1 &&
                    peer->Received[1] == 0 && peer->Received[2] == 1);
                assert(peer->LastChannel == 149 && peer->NextChannel == 149 && peer->SlotCount == 16);
                assert(peer->Slots[0] == 6 && peer->Slots[1] == 149 && peer->Slots[2] == 44);
            }
        }
        assert(ReadStored(RX_DATA_RING + RING_COUNT) == 0);
        assert(ReadStored(RX_DATA_RING + RING_BASE) == 0);
    }
    if (scenario == BOOT_CORRUPT) {
        assert(ReadStored(MCU_FIRMWARE_MODE) == 3);
        assert(sim.ResetCount == 0 && sim.Commands == 0 && sim.ScatterPackets == 0);
    }
    if (scenario == STUCK_DMA) {
        assert(retained == sim.CommonBuffer && retained != NULL);
        assert(result.CleanupStatus == (ULONG)STATUS_IO_TIMEOUT);
        Store(DMA_GLOBAL, 0); /* Model PnP stopping the bus before release. */
        FakeDelete(retained);
    } else {
        assert(ReadStored(PCIE_MAC_PM) == 0x10);
        assert(result.CleanupStatus == 0 && retained == NULL && sim.CommonBuffer == NULL);
        assert(ReadStored(TX_MCU_RING + RING_COUNT) == 0);
        assert(ReadStored(TX_FWDL_RING + RING_COUNT) == 0);
        assert(ReadStored(RX_MCU_RING + RING_COUNT) == 0);
        assert(ReadStored(RX_RUNTIME_RING + RING_COUNT) == 0);
    }
    free(sim.Registers);
}

static void VerifyImageScenario(SCENARIO scenario, NTSTATUS expected, ULONG expectedStage,
    const UCHAR *image, SIZE_T imageBytes)
{
    VerifyImageScenarioWithOptions(scenario, expected, expectedStage, image, imageBytes, FALSE, FALSE, FALSE, MT7922_OFDM_6_MBPS, NULL, FALSE, FALSE, FALSE);
}

static void VerifyScenario(SCENARIO scenario, NTSTATUS expected, ULONG expectedStage,
    const UCHAR *image)
{
    VerifyImageScenario(scenario, expected, expectedStage, image, image == NULL ? 0 : MT7922_FACTORY_IMAGE_BYTES);
}

static PUCHAR ReadImage(const wchar_t *Path, DWORD ExpectedBytes)
{
    HANDLE file = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(file != INVALID_HANDLE_VALUE);
    LARGE_INTEGER bytes;
    assert(GetFileSizeEx(file, &bytes) && bytes.QuadPart == ExpectedBytes);
    PUCHAR image = malloc(ExpectedBytes);
    assert(image != NULL);
    DWORD bytesRead = 0;
    assert(ReadFile(file, image, ExpectedBytes, &bytesRead, NULL));
    assert(bytesRead == ExpectedBytes);
    CloseHandle(file);
    return image;
}

static void VerifyAdvertTiming(void)
{
    static const UCHAR local[] = {2, 0, 0, 0, 0, 1};
    static const UCHAR master[] = {2, 0, 0, 0, 0, 2};
    AWDL_SYNC_REF ref = {0};
    ref.LocalAtRx = 1000 * TU_TICKS;
    ref.NextWindowSequence = 0xffff;
    ref.UntilNextSlotTu = 12;
    AWDL_ACTION advert = {0};
    advert.Sender = local;
    advert.Sync.Master = master;
    advert.Sync.WindowTu = 16;
    advert.Sync.WindowsPerSlot = 4;

    SetAdvertTiming(&ref, ref.LocalAtRx, &advert.Sync);
    assert(advert.Sync.UntilNextSlotTu == 12 && advert.Sync.NextWindowSequence == 0xffff);
    UCHAR frame[MT7922_PROBE_PSF_CAPACITY];
    assert(AwdlBuildAdvert(frame, sizeof(frame), local, &advert, 0, 0, 1, 6) == 273);
    assert(frame[43 + 15] == 0 && frame[43 + 16] == 0);

    SetAdvertTiming(&ref, ref.LocalAtRx + 12 * TU_TICKS, &advert.Sync);
    assert(advert.Sync.UntilNextSlotTu == 64 && advert.Sync.NextWindowSequence == 0);
    assert(AwdlBuildAdvert(frame, sizeof(frame), local, &advert, 0, 0, 1, 6) == 273);
    assert(frame[43 + 15] == 16 && frame[43 + 16] == 0);
    SetAdvertTiming(&ref, ref.LocalAtRx + 31 * TU_TICKS, &advert.Sync);
    assert(advert.Sync.UntilNextSlotTu == 45 && advert.Sync.NextWindowSequence == 1);

    ref.LocalAtRx += 31 * TU_TICKS;
    ref.NextWindowSequence = 1;
    ref.UntilNextSlotTu = 45;
    SetAdvertTiming(&ref, ref.LocalAtRx + 2 * TU_TICKS, &advert.Sync);
    assert(advert.Sync.UntilNextSlotTu == 43 && advert.Sync.NextWindowSequence == 1);
}

static void CountReceivedUploadPacket(void *context, const UCHAR *packet, ULONG length)
{
    ULONG *received = context;
    assert(packet[6] == 6 && (length == 60 || length == 61));
    ++*received;
}

static void RecordReorderedPacket(void *context, const UCHAR *packet, ULONG length)
{
    UCHAR **next = context;
    assert(length == 61);
    *(*next)++ = packet[60];
}

static void VerifyReceiveBaReordering(void)
{
    static const UCHAR station[] = {2, 0, 0, 0, 0, 1};
    static const UCHAR phone[] = {0x7e, 0, 0, 0, 0, 1};
    static const USHORT sequences[] = {0, 4095, 0, 2, 4};
    UCHAR received[5] = {0};
    UCHAR *next = received;
    MT7922_AWDL_LINK link = {0};
    link.ReorderRequested = TRUE;
    link.Receive = RecordReorderedPacket;
    link.Context = &next;
    link.AnnounceMdns = TRUE;
    link.ReceivePort = 8771;

    AWDL_SYNC_REF ref = {0};
    ref.Link = &link;
    ref.LinkOnline = TRUE;
    ref.Addba.StartingSequence = 4095;
    ref.Addba.Params = (16U << 6) | (5U << 2) | 2U;
    memcpy(ref.Peer, phone, sizeof(phone));
    MT7922_MCU_PROBE_INFO result = {0};
    memcpy(result.NicMac, station, sizeof(station));

    UCHAR ip[61] = {0x60, 0, 0, 0, 0, 21, 6, 64};
    LinkLocalFromMac(phone, ip + 8);
    LinkLocalFromMac(station, ip + 24);
    ip[42] = (UCHAR)(8771 >> 8);
    ip[43] = (UCHAR)(8771 & 0xff);
    ip[52] = 0x50;
    ip[53] = AWDL_TCP_ACK;

    StartBaReorder(&result, &ref);
    for (ULONG i = 0; i < RTL_NUMBER_OF(sequences); ++i) {
        UCHAR wire[103];
        ip[60] = (UCHAR)(i + 1);
        MT7922_RX_FRAME frame = {0};
        frame.Bytes = wire;
        frame.Length = AwdlBuildIpFrame(wire, sizeof(wire), station, phone, ip, sizeof(ip), sequences[i]);
        assert(frame.Length == 101);
        memmove(wire + 26, wire + 24, frame.Length - 24);
        wire[0] = 0x88;
        wire[24] = 5;
        wire[25] = 0;
        frame.Length += 2;
        sim.Counter = (i + 1) * 10000ULL;
        CheckIpv6Reply(&result, &frame, &ref);
        if (i == 0) {
            assert(next == received);
            StartBaReorder(&result, &ref); /* A retried response preserves the window. */
        } else if (i == 1) {
            assert(next == received + 2 && received[0] == 2 && received[1] == 1);
            assert(ref.LocalAtUploadData == sim.Counter);
        }
    }
    assert(next == received + 2);
    UCHAR bar[20] = {0x84};
    memcpy(bar + 4, station, sizeof(station));
    memcpy(bar + 10, phone, sizeof(phone));
    bar[16] = 4;
    bar[17] = 5 << 4;
    bar[18] = 2 << 4;
    MT7922_RX_FRAME barFrame = {0};
    barFrame.Bytes = bar;
    barFrame.Length = sizeof(bar);
    HandleBaReorderControl(&result, &barFrame, &ref);
    assert(next == received + 3 && received[2] == 4);
    const ULONGLONG lastArrival = sim.Counter;
    PollBaReorder(&result, &ref, lastArrival + 10ULL * BA_REORDER_TIMEOUT_US);
    assert(next == received + 4 && received[3] == 5);
    assert(ref.LocalAtUploadData == lastArrival);
    StopBaReorder(&result, &ref);
    assert(!link.Reorder.active && link.Reorder.stored == 0);
    assert(link.BaReorderSessions == 1 && link.BaReorderDirect == 1 && link.BaReorderReleased == 3);
    assert(link.BaReorderDuplicates == 1 && link.BaReorderHoles == 2);
    assert(link.BaReorderBars == 1 && link.BaReorderTimeouts == 1);
}

static void VerifyUploadReplyTiming(void)
{
    static const UCHAR station[] = {2, 0, 0, 0, 0, 1};
    static const UCHAR phone[] = {0x7e, 0, 0, 0, 0, 1};
    ULONG received = 0;
    MT7922_AWDL_LINK link = {0};
    link.Context = &received;
    link.Receive = CountReceivedUploadPacket;
    link.AnnounceMdns = TRUE;
    link.ReceivePort = 8771;

    AWDL_SYNC_REF ref = {0};
    ref.Link = &link;
    ref.LinkOnline = TRUE;
    ref.LocalAtRx = 1000 * TU_TICKS;
    ref.UntilNextSlotTu = 44;
    ref.NextWindowSequence = 4;
    ref.Channel = 149;
    ref.Sequence.Count = AWDL_CHANNEL_SLOTS;
    ref.Sequence.Channel[1] = 149;
    memcpy(ref.Peer, phone, sizeof(phone));
    assert(!CanReplyAfterUploadData(&ref, ref.LocalAtRx + 8 * TU_TICKS));

    MT7922_MCU_PROBE_INFO result = {0};
    memcpy(result.NicMac, station, sizeof(station));
    UCHAR ip[61] = {0x60, 0, 0, 0, 0, 20, 6, 64};
    LinkLocalFromMac(phone, ip + 8);
    LinkLocalFromMac(station, ip + 24);
    ip[42] = (UCHAR)(8771 >> 8);
    ip[43] = (UCHAR)(8771 & 0xff);
    ip[52] = 0x50;
    ip[53] = AWDL_TCP_ACK;

    UCHAR wire[101];
    MT7922_RX_FRAME frame = {0};
    frame.Bytes = wire;
    sim.Counter = ref.LocalAtRx + 7 * TU_TICKS;
    frame.Length = AwdlBuildIpFrame(wire, sizeof(wire), station, phone, ip, 60, 1);
    CheckIpv6Reply(&result, &frame, &ref);
    assert(received == 1 && ref.LocalAtUploadData == 0);

    ip[5] = 21;
    frame.Length = AwdlBuildIpFrame(wire, sizeof(wire), station, phone, ip, sizeof(ip), 2);
    CheckIpv6Reply(&result, &frame, &ref);
    assert(received == 2 && ref.LocalAtUploadData == sim.Counter);

    const ULONGLONG afterSync = ref.LocalAtRx + 8 * TU_TICKS;
    assert(!IsCurrentSharedWindow(&ref, afterSync));
    assert(CanReplyAfterUploadData(&ref, afterSync));
    assert(!CanReplyAfterUploadData(&ref, ref.LocalAtRx + 20 * TU_TICKS));

    ref.LocalAtUploadData = ref.LocalAtRx + 40 * TU_TICKS;
    assert(CanReplyAfterUploadData(&ref, ref.LocalAtRx + 41 * TU_TICKS));
    assert(!CanReplyAfterUploadData(&ref, ref.LocalAtRx + 42 * TU_TICKS));
    ref.Sequence.Channel[2] = 149;
    assert(!CanReplyAfterUploadData(&ref, ref.LocalAtRx + 48 * TU_TICKS));
    ref.LocalAtUploadData = ref.LocalAtRx + 47 * TU_TICKS;
    ref.Sequence.Channel[2] = 6;
    assert(!CanReplyAfterUploadData(&ref, ref.LocalAtRx + 48 * TU_TICKS));
    ref.Sequence.Channel[2] = 149;
    assert(CanReplyAfterUploadData(&ref, ref.LocalAtRx + 48 * TU_TICKS));
    assert(!CanReplyAfterUploadData(&ref, ref.LocalAtRx + 2 * PSF_INTERVAL_TICKS + 1));

    MT7922_MCU_STATION shared = {0};
    shared.Ap.Channel = 149;
    shared.SharedPeerChannels = ref.Sequence;
    shared.SharedPeerWindowSequence = ref.NextWindowSequence;
    shared.SharedPeerUntilNextSlotTu = ref.UntilNextSlotTu;
    shared.SharedPeerSyncAt = ref.LocalAtRx;
    assert(IsStationPeerChannelWindow(&shared, afterSync));
    assert(IsStationPeerChannelWindow(&shared, ref.LocalAtRx + 20 * TU_TICKS));
    assert(!IsStationPeerChannelWindow(&shared, ref.LocalAtRx + 42 * TU_TICKS));
    assert(!IsStationPeerChannelWindow(&shared, ref.LocalAtRx + 46 * TU_TICKS));
    assert(IsStationPeerChannelWindow(&shared, ref.LocalAtRx + 47 * TU_TICKS));
    shared.SharedPeerChannels.Channel[2] = 6;
    assert(!IsStationPeerChannelWindow(&shared, ref.LocalAtRx + 48 * TU_TICKS));
    shared.SharedPeerChannels.Channel[1] = 6;
    assert(!IsStationPeerChannelWindow(&shared, afterSync));
    memset(shared.SharedPeerChannels.Channel, 149, sizeof(shared.SharedPeerChannels.Channel));
    assert(!IsStationPeerChannelWindow(&shared, ref.LocalAtRx + 2 * PSF_INTERVAL_TICKS + 1));

    MCU_RINGS rings = {0};
    rings.AwdlPollTimer = ExAllocateTimer(NULL, NULL, EX_TIMER_HIGH_RESOLUTION);
    sim.Counter = ref.LocalAtUploadData + 1501ULL * 10000;
    WaitForAwdlPoll(&rings, &ref);
    assert(link.UploadFastPolls == 0);
    ref.LocalAtUploadData = sim.Counter;
    WaitForAwdlPoll(&rings, &ref);
    assert(link.UploadFastPolls == 1 && link.UploadLatePolls == 0);
    sim.Counter += 1501ULL * 10000;
    WaitForAwdlPoll(&rings, &ref);
    assert(link.UploadFastPolls == 1);
    link.ShortPollRequested = TRUE;
    link.ShortPollActive = 1;
    ref.LocalAtUploadData = sim.Counter;
    WaitForAwdlPoll(&rings, &ref);
    assert(mockPollTimer.DueTime == -2500 && link.UploadShortPolls == 1);
    assert(link.UploadShortPollTimeUs == 250);
    sim.Counter = ref.LocalAtUploadData + 13ULL * TU_TICKS;
    WaitForAwdlPoll(&rings, &ref);
    assert(mockPollTimer.DueTime == -10000 && link.UploadShortPolls == 1);
    link.ShortPollActive = 0;
    ref.LocalAtUploadData = sim.Counter;
    WaitForAwdlPoll(&rings, &ref);
    assert(mockPollTimer.DueTime == -10000 && link.UploadShortPolls == 1);
    ExDeleteTimer(rings.AwdlPollTimer, TRUE, TRUE, NULL);
}

static void VerifyFastAckReport(void)
{
    MT7922_AWDL_LINK link = {0};
    AWDL_SYNC_REF ref = {0};
    MT7922_MCU_PROBE_INFO result = {0};
    ref.Link = &link;
    ref.Unicast.Active = TRUE;
    ref.Unicast.IsTunnelData = TRUE;
    ref.Unicast.Request.RateCode = MT7922_OFDM_24_MBPS;
    ref.Unicast.Completion.ReleaseSeen = 1;
    ref.Unicast.Completion.ReportSeen = 1;
    ref.Unicast.Completion.ReportRate = MT7922_OFDM_24_MBPS;
    FinishUnicast(&ref, &result, 1);
    assert(link.FastAckAcked == 1 && link.FastAckNoAck == 0 && link.FastAckReportRate == MT7922_OFDM_24_MBPS);

    ref.Unicast.Active = TRUE;
    ref.Unicast.Request.RateCode = MT7922_OFDM_54_MBPS;
    ref.Unicast.Completion.ReportRate = MT7922_OFDM_54_MBPS;
    FinishUnicast(&ref, &result, 2);
    assert(link.FastAckAcked == 2 && link.FastAckNoAck == 0 && link.FastAckReportRate == MT7922_OFDM_54_MBPS);

    ref.Unicast.Active = TRUE;
    ref.Unicast.Completion.AckErrors = 1;
    FinishUnicast(&ref, &result, 3);
    assert(link.FastAckAcked == 2 && link.FastAckNoAck == 1);
}

static void VerifyStationScan(void)
{
    uint8_t request[MT7922_SCAN_REQUEST_BYTES];
    uint8_t expected[MT7922_SCAN_REQUEST_BYTES] = {
        0x87, 2, 1, 4, 1, 2, 0x20, 1, 3, 0, 0, 0, 'L', 'A', 'N'
    };
    const MT7922_SCAN_SSID ssids[] = {{0, {0}}, {3, {'L', 'A', 'N'}}};
    MT7922_SCAN_CHANNEL channels[33];
    for (size_t index = 0; index < 32; ++index) {
        channels[index] = (MT7922_SCAN_CHANNEL){2, (uint8_t)(36 + index * 4)};
        expected[160 + index * 2] = channels[index].Band;
        expected[161 + index * 2] = channels[index].Number;
    }
    channels[32] = (MT7922_SCAN_CHANNEL){3, 5};
    uint8_t informationElements[260] = {0x7f, 1, 0x01};
    const MT7922_SCAN_PARAMETERS active = {
        7, 1, 2, true, ssids, 2, channels, 33, informationElements, sizeof(informationElements), NULL, 0, 0
    };
    expected[158] = 4;
    expected[159] = 32;
    expected[224] = 4;
    expected[225] = 1;
    memcpy(expected + 226, informationElements, sizeof(informationElements));
    expected[826] = 1;
    expected[830] = 3;
    expected[831] = 5;
    memset(expected + 1110, 0xff, 6);
    expected[1185] = 1;
    assert(Mt7922BuildScanRequest(request, sizeof(request), &active));
    assert(memcmp(request, expected, sizeof(request)) == 0);

    MT7922_SCAN_PARAMETERS invalid = active;
    invalid.ChannelCount = MT7922_SCAN_MAX_CHANNELS + 1;
    assert(!Mt7922BuildScanRequest(request, sizeof(request), &invalid));
    assert(!Mt7922BuildScanRequest(request, sizeof(request) - 1, &active));
    assert(memcmp(request, expected, sizeof(request)) == 0);
    const MT7922_SCAN_PARAMETERS passive = {.Sequence = 9};
    assert(Mt7922BuildScanRequest(request, sizeof(request), &passive));
    assert(request[0] == 9 && request[2] == 0 && request[3] == 1 && request[5] == 0 &&
        request[158] == 0 && request[159] == 0 && request[826] == 0 && request[1185] == 0);

    uint8_t cancel[MT7922_SCAN_CANCEL_BYTES];
    assert(Mt7922BuildScanCancel(cancel, sizeof(cancel), 7));
    assert(cancel[0] == 7 && cancel[1] == 0 && cancel[2] == 0 && cancel[3] == 0);
    assert(!Mt7922BuildScanCancel(cancel, sizeof(cancel), 0x87));
    const uint8_t event[20] = {0x87};
    assert(Mt7922IsScanDone(0x0d, event, sizeof(event), 7, 1));
    assert(!Mt7922IsScanDone(0x0d, event, sizeof(event), 7, 0));
    assert(!Mt7922IsScanDone(0x0d, event, sizeof(event), 8, 1));
    assert(!Mt7922IsScanDone(0x0d, event, sizeof(event) - 1, 7, 1));
    assert(!Mt7922IsScanDone(0x23, event, sizeof(event), 7, 1));
}

static void VerifyMt7925ScanBuilders(void)
{
    const MT7922_SCAN_SSID ssids[] = {{0, {0}}, {3, {'L', 'A', 'N'}}};
    const MT7922_SCAN_CHANNEL channels[] = {{2, 36}, {2, 149}};
    const uint8_t bssid[] = {2, 0x11, 0x22, 0x33, 0x44, 0x55};
    const uint8_t ies[] = {0xdd, 1, 0xaa};
    const MT7922_SCAN_PARAMETERS parameters = {
        7, 1, 2, true, ssids, RTL_NUMBER_OF(ssids), channels, RTL_NUMBER_OF(channels),
        ies, sizeof(ies), bssid, 40, 600
    };
    uint8_t connac2[MT7922_SCAN_REQUEST_BYTES];
    assert(Mt7922BuildScanRequest(connac2, sizeof(connac2), &parameters));
    const uint8_t randomMac[] = {2, 0xaa, 0xbb, 0xcc, 0xdd, 0xee};
    memcpy(connac2 + 1116, randomMac, sizeof(randomMac));

    uint8_t unified[MT7925_SCAN_REQUEST_MAX_BYTES];
    const size_t bytes = Mt7925BuildScanRequest(unified, sizeof(unified), connac2, sizeof(connac2));
    assert(bytes == 567 && unified[0] == 0x87 && unified[1] == 2 &&
        unified[2] == 0 && unified[3] == 0);
    assert(unified[4] == 1 && unified[5] == 0 && unified[6] == 20 && unified[7] == 0 &&
        unified[8] == 1 && unified[9] == 2 && unified[10] == 0x20 &&
        unified[12] == 40 && unified[14] == 40 && unified[16] == 0x58 && unified[17] == 0x02);

    const size_t ssid = 24;
    assert(unified[ssid] == 10 && unified[ssid + 1] == 0 &&
        unified[ssid + 2] == 0x70 && unified[ssid + 3] == 1 &&
        unified[ssid + 4] == 4 && unified[ssid + 5] == 1 &&
        unified[ssid + 8] == 3 && unified[ssid + 12] == 'L' &&
        unified[ssid + 13] == 'A' && unified[ssid + 14] == 'N');
    const size_t bssidTlv = ssid + 368;
    assert(unified[bssidTlv] == 11 && unified[bssidTlv + 1] == 0 &&
        unified[bssidTlv + 2] == 16 && unified[bssidTlv + 3] == 0 &&
        memcmp(unified + bssidTlv + 4, bssid, sizeof(bssid)) == 0);
    const size_t channelTlv = bssidTlv + 16;
    assert(unified[channelTlv] == 12 && unified[channelTlv + 1] == 0 &&
        unified[channelTlv + 2] == 136 && unified[channelTlv + 3] == 0 &&
        unified[channelTlv + 4] == 4 && unified[channelTlv + 5] == 2 &&
        unified[channelTlv + 8] == 2 && unified[channelTlv + 9] == 36 &&
        unified[channelTlv + 10] == 2 && unified[channelTlv + 11] == 149);
    const size_t miscTlv = channelTlv + 136;
    assert(unified[miscTlv] == 14 && unified[miscTlv + 1] == 0 &&
        unified[miscTlv + 2] == 12 && unified[miscTlv + 3] == 0 &&
        memcmp(unified + miscTlv + 4, randomMac, sizeof(randomMac)) == 0);
    const size_t ieTlv = miscTlv + 12;
    assert(unified[ieTlv] == 13 && unified[ieTlv + 1] == 0 &&
        unified[ieTlv + 2] == 11 && unified[ieTlv + 3] == 0 &&
        unified[ieTlv + 4] == 3 && unified[ieTlv + 6] == 2 &&
        memcmp(unified + ieTlv + 8, ies, sizeof(ies)) == 0);
    assert(Mt7925BuildScanRequest(unified, bytes - 1, connac2, sizeof(connac2)) == 0);
    connac2[160] = 1; /* MT7925 scan requests in this profile are 5 GHz only. */
    assert(Mt7925BuildScanRequest(unified, sizeof(unified), connac2, sizeof(connac2)) == 0);

    uint8_t cancel[MT7925_SCAN_CANCEL_BYTES];
    assert(Mt7925BuildScanCancel(cancel, sizeof(cancel), 0x87, 2) == sizeof(cancel));
    assert(cancel[0] == 0x87 && cancel[1] == 2 && cancel[2] == 0 && cancel[3] == 0 &&
        cancel[4] == 2 && cancel[5] == 0 && cancel[6] == 8 && cancel[7] == 0 &&
        cancel[8] == 0 && cancel[9] == 0 && cancel[10] == 0 && cancel[11] == 0);
    assert(Mt7925BuildScanCancel(cancel, sizeof(cancel) - 1, 0x87, 2) == 0);
}

static void VerifyStationObserverBuilders(void)
{
    uint8_t enable[MT7922_OBSERVER_ENABLE_BYTES];
    Mt7922BuildStationSnifferEnable(enable, true);
    static const uint8_t expectedEnable[MT7922_OBSERVER_ENABLE_BYTES] = {
        0, 0, 0, 0, 0, 0, 8, 0, 1, 0, 0, 0
    };
    assert(memcmp(enable, expectedEnable, sizeof(enable)) == 0);
    Mt7922BuildStationSnifferEnable(enable, false);
    assert(enable[8] == 0);

    uint8_t config[MT7922_OBSERVER_CONFIG_BYTES];
    assert(Mt7922BuildStationSnifferConfig(config, 149, 2, true));
    static const uint8_t expectedConfig[MT7922_OBSERVER_CONFIG_BYTES] = {
        0, 0, 0, 0, 1, 0, 16, 0, 0, 0, 2, 0, 149, 0, 149, 0, 1, 0, 0, 0
    };
    assert(memcmp(config, expectedConfig, sizeof(config)) == 0);
    assert(Mt7922BuildStationSnifferConfig(config, 6, 1, false) && config[10] == 1 &&
        config[11] == 0 && config[12] == 6 && config[13] == 0 && config[14] == 6 && config[16] == 0);
    assert(!Mt7922BuildStationSnifferConfig(config, 0, 2, true));
    assert(!Mt7922BuildStationSnifferConfig(config, 149, 0, true));
    assert(!Mt7922BuildStationSnifferConfig(config, 149, 4, true));

    MT7922_STATION_OBSERVER observer = {0};
    const MT7922_RX_FRAME empty = {0};
    assert(Mt7922ObserverIngest(&observer, Mt7922RxMalformed, &empty) == Mt7922ObserveIgnored);
    const uint8_t local[6] = {2, 0x44, 0x33, 0x22, 0x11, 1};
    Mt7922ObserverStart(&observer, 149, 2, local);
    assert(observer.Running && observer.Channel == 149 && observer.Band == 2 &&
        memcmp(observer.Local, local, sizeof(local)) == 0);
    Mt7922ObserverStop(&observer);
    assert(!observer.Running);
}

static void VerifyMt7925StationRadioBuilders(void)
{
    static const uint8_t channels[] = {36, 40, 44, 48, 149, 153, 157, 161, 165};
    MT7922_STATION_RADIO_PROFILE profile = {
        .Country = {'C', 'N'}, .ChannelCount = (uint8_t)RTL_NUMBER_OF(channels)
    };
    for (size_t i = 0; i < RTL_NUMBER_OF(channels); ++i) {
        profile.Channels[i] = (MT7922_STATION_RADIO_CHANNEL){channels[i], 40};
    }

    uint8_t domain[MT7925_STATION_DOMAIN_HEADER + MT7925_STATION_DOMAIN_ENTRY * MT7922_STATION_RADIO_CHANNELS];
    assert(Mt7925BuildStationDomain(domain, &profile) == 88);
    assert(domain[0] == 'C' && domain[1] == 'N' && domain[4] == 0 && domain[5] == 2 && domain[6] == 0);
    assert(domain[8] == 2 && domain[9] == 0 && domain[10] == 80 && domain[11] == 0);
    assert(domain[12] == 0 && domain[13] == (uint8_t)RTL_NUMBER_OF(channels) && domain[14] == 0);
    for (size_t i = 0; i < RTL_NUMBER_OF(channels); ++i) {
        const size_t offset = MT7925_STATION_DOMAIN_HEADER + MT7925_STATION_DOMAIN_ENTRY * i;
        assert(domain[offset] == channels[i] && domain[offset + 1] == 0);
        for (size_t j = 2; j < MT7925_STATION_DOMAIN_ENTRY; ++j) {
            assert(domain[offset + j] == 0);
        }
    }
    assert(Mt7925BuildStationDomain(NULL, &profile) == 0);

    uint8_t power[MT7925_STATION_POWER_BYTES];
    assert(Mt7925BuildStationPower(power, &profile, 0) == MT7925_STATION_POWER_BYTES);
    assert(power[4] == 1 && power[6] == 0x76 && power[7] == 0x05);
    assert(power[12] == 3 && power[13] == 2 && power[14] == 0 &&
        power[16] == 'C' && power[17] == 'N');
    assert(power[MT7925_STATION_POWER_HEADER] == 36 && power[MT7925_STATION_POWER_HEADER + 1] == 40);
    assert(power[MT7925_STATION_POWER_HEADER + MT7925_STATION_POWER_ENTRY] == 40 &&
        power[MT7925_STATION_POWER_HEADER + MT7925_STATION_POWER_ENTRY + 1] == 40);
    assert(Mt7925BuildStationPower(power, &profile, 6) == MT7925_STATION_POWER_BYTES &&
        power[12] == 3 && power[14] == 1 &&
        power[MT7925_STATION_POWER_HEADER] == 157 &&
        power[MT7925_STATION_POWER_HEADER + 2U * MT7925_STATION_POWER_ENTRY] == 165);
    assert(Mt7925BuildStationPower(power, &profile, RTL_NUMBER_OF(channels)) == 0);
}

static void VerifyVhtRxStats(void)
{
    MT7922_AWDL_LINK link = {0};
    MT7922_RX_FRAME frame = {0};
    CountPeerRxRadio(&link, &frame);
    frame.PhyPresent = 1;
    frame.PhyMode = 4;
    frame.PhyWidth = 2;
    frame.Ampdu = 1;
    CountPeerRxRadio(&link, &frame);
    CountPeerRxRadio(&link, &frame);
    frame.AmpduTimestamp = 123;
    CountPeerRxRadio(&link, &frame);
    frame.PhyMode = 2;
    frame.Ampdu = 0;
    CountPeerRxRadio(&link, &frame);
    assert(link.RxPhyVht == 3 && link.RxPhyVht80 == 3 && link.RxPhyHt == 1 && link.RxPhyUnknown == 1);
    assert(link.RxAmpduFrames == 3 && link.RxAmpduBursts == 2 && link.RxAmpduMax == 2);
}

static void VerifyPeerSelection(void)
{
    static const UCHAR local[] = {2, 0, 0, 0, 0, 1};
    static const UCHAR peer[] = {2, 0, 0, 0, 0, 2};
    MT7922_AWDL_RX_COUNTS capture = {0};
    capture.Channel = 149;
    AWDL_PROBE_FRAME probe = {0};
    probe.Source = local;
    probe.Capture = &capture;
    probe.PreferMifUntil = 1200ULL * 10000;
    sim.Counter = 0;

    AWDL_ACTION action = {0};
    action.DeviceClass = 2;
    action.Sender = peer;
    action.Sync.Master = peer;
    action.Sync.WindowTu = 16;
    action.Sync.WindowsPerSlot = 4;
    action.Sync.UntilNextSlotTu = 26;

    action.Kind = AwdlPsf;
    BuildIosProbe(&probe, &action);
    assert(probe.SeenIos && !probe.PeerSelected);
    action.Kind = AwdlMif;
    BuildIosProbe(&probe, &action);
    assert(probe.PeerSelected && memcmp(probe.Peer, peer, sizeof(peer)) == 0);

    probe.Length = 0;
    probe.PeerSelected = FALSE;
    action.Kind = AwdlPsf;
    sim.Counter = probe.PreferMifUntil;
    BuildIosProbe(&probe, &action);
    assert(probe.PeerSelected);
}

typedef struct {
    unsigned Records;
    unsigned Events;
} STATION_POLL_TEST;

static void TestStationReceive(void *Context, const uint8_t *Record, size_t Bytes)
{
    STATION_POLL_TEST *seen = Context;
    assert(Bytes == 24 && Record[0] == Bytes);
    ++seen->Records;
}

static bool TestStationScanEvent(void *Context, const uint8_t *Record, size_t Bytes)
{
    STATION_POLL_TEST *seen = Context;
    if (!Mt7922MatchScanRecord(Record, Bytes, 1, 0)) {
        return false;
    }
    assert(seen->Records == 1); /* Data reaches the consumer before scan completion. */
    ++seen->Events;
    return true;
}

typedef struct {
    unsigned Delivered;
    bool Blocked;
} EAPOL_TEST;

static bool TestEapolReceive(void *Context, const uint8_t *Frame, size_t Bytes, bool Eapol)
{
    EAPOL_TEST *eapol = Context;
    const uint8_t local[] = {2, 0x44, 0x33, 0x22, 0x11, 1};
    const uint8_t ap[] = {2, 0x21, 0x22, 0x23, 0x24, 0x25};
    assert(Eapol && Bytes == 38 && Frame[0] == 0x08 && Frame[1] == 0x02);
    assert(memcmp(Frame + 4, local, 6) == 0 && memcmp(Frame + 10, ap, 6) == 0);
    const uint8_t expected[] = {1, 3, 4, 5};
    assert(eapol->Delivered < sizeof(expected));
    assert(Frame[30] == 0x88 && Frame[31] == 0x8e && Frame[37] == expected[eapol->Delivered]);
    if (eapol->Blocked) {
        return false;
    }
    ++eapol->Delivered;
    return true;
}

static bool TestArpReceive(void *Context, const uint8_t *Frame, size_t Bytes, bool Eapol)
{
    unsigned *delivered = Context;
    const uint8_t local[] = {2, 0x44, 0x33, 0x22, 0x11, 1};
    const uint8_t sender[] = {2, 0x31, 0x32, 0x33, 0x34, 0x35};
    assert(!Eapol && Bytes == 60 && Frame[0] == 0x08 && Frame[1] == 0x02);
    assert(Frame[30] == 0x08 && Frame[31] == 0x06 && memcmp(Frame + 16, sender, 6) == 0);
    if ((*delivered)++ == 0) {
        assert(memcmp(Frame + 4, local, 6) == 0);
    } else {
        const uint8_t broadcast[] = {255, 255, 255, 255, 255, 255};
        assert(memcmp(Frame + 4, broadcast, 6) == 0);
    }
    return true;
}

static size_t AppendClcRule(UCHAR *Buffer, size_t Offset, const char Alpha2[2],
    const char Type[2], const UCHAR *Data, size_t Bytes)
{
    memcpy(Buffer + Offset, Alpha2, 2);
    memcpy(Buffer + Offset + 2, Type, 2);
    Buffer[Offset + 4] = (UCHAR)Bytes;
    Buffer[Offset + 5] = (UCHAR)(Bytes >> 8);
    memcpy(Buffer + Offset + 6, Data, Bytes);
    return Offset + 6 + Bytes;
}

static void AppendClcEntry(UCHAR *Buffer, size_t Offset, UCHAR EntryType, UCHAR Count, size_t End)
{
    PutUint32(Buffer + Offset, (ULONG)(End - Offset));
    Buffer[Offset + 4] = 0; /* POWER */
    Buffer[Offset + 5] = 1; /* version */
    Buffer[Offset + 6] = Count;
    Buffer[Offset + 7] = EntryType;
}

static void VerifyStationClcSelection(void)
{
    static const UCHAR dataA[] = {0xaa};
    static const UCHAR dataB[] = {0xbb, 0xcc};
    static const UCHAR dataC[] = {0xdd, 0xee, 0xff};
    UCHAR region[128] = {0};
    size_t pos = 16;
    pos = AppendClcRule(region, pos, "US", "0-", dataA, sizeof(dataA));
    pos = AppendClcRule(region, pos, "00", "0-", dataB, sizeof(dataB));
    AppendClcEntry(region, 0, 0, 2, pos);
    const size_t firstEnd = pos;
    const size_t second = pos;
    pos = second + 16;
    pos = AppendClcRule(region, pos, "US", "0C", dataC, sizeof(dataC));
    AppendClcEntry(region, second, 1, 1, pos);

    static const uint8_t us[2] = {'U', 'S'};
    static const uint8_t cn[2] = {'C', 'N'};
    MT7922_CLC_RULE rule;
    assert(Mt7922ClcSelectPower(region, pos, 0, us, &rule) && rule.DataBytes == 1 &&
        rule.Data[0] == 0xaa && memcmp(rule.Type, "0-", 2) == 0);
    assert(Mt7922ClcSelectPower(region, pos, 1, us, &rule) && rule.DataBytes == 3 &&
        memcmp(rule.Data, dataC, 3) == 0 && memcmp(rule.Type, "0C", 2) == 0);
    /* No CN rule: the "00" rule inside the matching entry is used. */
    assert(Mt7922ClcSelectPower(region, pos, 0, cn, &rule) && rule.DataBytes == 2 &&
        memcmp(rule.Data, dataB, 2) == 0);
    /* The type-1 entry advertises US only, so CN has no fallback there. */
    assert(!Mt7922ClcSelectPower(region, pos, 1, cn, &rule));
    /* Encapsulation 1 is absent from the truncated type-0 region. */
    assert(!Mt7922ClcSelectPower(region, firstEnd, 1, us, &rule));
    UCHAR broken[sizeof(region)];
    memcpy(broken, region, sizeof(broken));
    PutUint32(broken, 0x1000);
    assert(!Mt7922ClcSelectPower(broken, sizeof(broken), 0, us, &rule));
    assert(!Mt7922ClcSelectPower(region, firstEnd - 1, 0, us, &rule));

    UCHAR command[MT7922_CLC_REQUEST_BYTES + 8];
    assert(Mt7922ClcSelectPower(region, pos, 1, us, &rule));
    const size_t bytes = Mt7922BuildStationClc(command, sizeof(command), &rule);
    assert(bytes == MT7922_CLC_REQUEST_BYTES + 3);
    assert(command[0] == 1 && command[2] == (UCHAR)bytes && command[3] == (UCHAR)(bytes >> 8) &&
        command[4] == 0 && command[5] == 1 && memcmp(command + 8, "US", 2) == 0 &&
        memcmp(command + 10, "0C", 2) == 0 &&
        memcmp(command + MT7922_CLC_REQUEST_BYTES, dataC, 3) == 0);
    assert(Mt7922BuildStationClc(command, MT7922_CLC_REQUEST_BYTES + 2, &rule) == 0);
    MT7922_CLC_RULE oversize = rule;
    oversize.DataBytes = MT7922_CLC_RULE_MAX + 1;
    assert(Mt7922BuildStationClc(command, sizeof(command), &oversize) == 0);
}

static void VerifyStationClcBoot(const UCHAR *Image, SIZE_T Bytes, bool MissingRule)
{
    ZeroMemory(&sim, sizeof(sim));
    sim.Registers = calloc(1, REGISTER_BYTES);
    assert(sim.Registers != NULL);
    sim.Scenario = FULL_BOOT;
    sim.StationMode = TRUE;
    sim.FactoryImage = Image;
    sim.ImageBytes = Bytes;
    sim.SelectedBank = 0x7001;
    sim.ResetValue = 0x11;
    sim.FirmwareState = 1;
    sim.StationCountry[0] = 'C';
    sim.StationCountry[1] = 'N';
    sim.EepromHwType = 1;
    Store(MCU_FIRMWARE_MODE, 3);
    Store(DMA_RESET, DMA_RESET_LOGIC);
    Store(DMA_GLOBAL, 0x1010b870);
    Store(DMA_GLOBAL_EXT, DMA_TX_SCHEDULING);
    Store(PCIE_MAC_PM, 0x10);
    Store(HIF_WINDOW + TOP_HOST_LPCR, TOP_HOST_FW_OWN);

    const MT7922_STATION_RADIO_PROFILE radio = {
        .Country = {'C', 'N'}, .ChannelCount = 9,
        .Channels = {{36, 10}, {40, 10}, {44, 10}, {48, 10},
            {149, 20}, {153, 20}, {157, 20}, {161, 20}, {165, 20}}
    };
    static const UCHAR encap0[] = {0xaa};
    static const UCHAR encap1[] = {0xbb, 0xcc, 0xdd};
    static const UCHAR other[] = {0x55};
    UCHAR clc[96] = {0};
    size_t pos = 16;
    if (MissingRule) {
        /* Matching entry exists but carries no CN or "00" rule. */
        pos = AppendClcRule(clc, pos, "US", "0-", other, sizeof(other));
        AppendClcEntry(clc, 0, 0, 1, pos);
    } else {
        pos = AppendClcRule(clc, pos, "CN", "0-", encap0, sizeof(encap0));
        AppendClcEntry(clc, 0, 0, 1, pos);
        const size_t second = pos;
        pos = second + 16;
        pos = AppendClcRule(clc, pos, "CN", "0C", encap1, sizeof(encap1));
        AppendClcEntry(clc, second, 1, 1, pos);
    }

    MT7922_MCU_STATION station = {0};
    const NTSTATUS status = Mt7922McuStartStation(&station, sim.Registers, MT7922_CHIP_ID,
        &sim, Image, Bytes,
        19, &radio, clc, pos);
    assert(sim.EepromReads == 1);
    if (MissingRule) {
        assert(status == STATUS_DEVICE_CONFIGURATION_ERROR);
        assert(sim.StationClcLength == 0 && sim.StationRadioBatches == 0);
        assert(!station.Ready && station.Scan.Rings == NULL && sim.CommonBuffer == NULL);
    } else {
        assert(status == STATUS_SUCCESS && station.Ready);
        assert(sim.StationClcLength == MT7922_CLC_REQUEST_BYTES + 3);
        assert(memcmp(sim.StationClcCountry, "CN", 2) == 0 &&
            memcmp(sim.StationClcType, "0C", 2) == 0 && sim.StationClcFirst == 0xbb);
        assert(sim.StationRadioBatches == 2);
        assert(Mt7922McuStopStation(&station) == STATUS_SUCCESS);
    }
    free(sim.Registers);
    sim.Registers = NULL;
}

static void TestSharedLinkReceive(void *context, const UCHAR *packet, ULONG length)
{
    assert(context == &sim && length == 48 && packet[6] == 58 && packet[40] == 129);
    ++sim.LinkReceived;
}

static ULONG TestSharedLinkTakeSend(void *context, UCHAR *packet, USHORT *rate, ULONGLONG *enqueuedAt)
{
    *enqueuedAt = sim.Counter;
    assert(context == &sim && sim.LinkReady && *rate == 0);
    static const UCHAR local[6] = {2, 0x44, 0x33, 0x22, 0x11, 1};
    static const UCHAR peer[6] = {0x7e, 0, 0, 0, 0, 1};
    if (sim.StationSharedQueueAnnouncement) {
        sim.StationSharedQueueAnnouncement = false;
        return BuildMdnsQuery(packet, MT7922_AWDL_LINK_MTU, local, NULL);
    }
    if (sim.QueueSharedSynAck) {
        sim.QueueSharedSynAck = false;
        memset(packet, 0, 60);
        packet[0] = 0x60;
        packet[5] = 20;
        packet[6] = 6;
        packet[7] = 64;
        LinkLocalFromMac(local, packet + 8);
        LinkLocalFromMac(peer, packet + 24);
        packet[40] = (UCHAR)(8771 >> 8);
        packet[41] = (UCHAR)(8771 & 0xff);
        packet[42] = (UCHAR)(60511 >> 8);
        packet[43] = (UCHAR)(60511 & 0xff);
        packet[52] = 0x50;
        packet[53] = AWDL_TCP_SYN | AWDL_TCP_ACK;
        return 60;
    }
    if (sim.StationSharedEchoes > 1) {
        if (sim.StationSharedExtraEchoes == 0) {
            return 0;
        }
        --sim.StationSharedExtraEchoes;
    }
    return BuildIcmp6Echo(packet, MT7922_AWDL_LINK_MTU, local, peer, 27);
}

static void VerifyStationLifecycle(const UCHAR *Image, SIZE_T Bytes,
    bool RejectPort, bool RejectGroupKey, bool RejectPse)
{
    ZeroMemory(&sim, sizeof(sim));
    sim.Registers = calloc(1, REGISTER_BYTES);
    assert(sim.Registers != NULL);
    sim.Scenario = FULL_BOOT;
    sim.StationMode = TRUE;
    sim.RejectPort = RejectPort;
    sim.StationCountry[0] = 'Z';
    sim.StationCountry[1] = 'Z';
    sim.FactoryImage = Image;
    sim.ImageBytes = Bytes;
    sim.SelectedBank = 0x7001;
    sim.ResetValue = 0x11;
    sim.FirmwareState = 1;
    Store(MCU_FIRMWARE_MODE, 3);
    Store(DMA_RESET, DMA_RESET_LOGIC);
    Store(DMA_GLOBAL, 0x1010b870);
    Store(DMA_GLOBAL_EXT, DMA_TX_SCHEDULING);
    Store(PCIE_MAC_PM, 0x10);
    Store(HIF_WINDOW + TOP_HOST_LPCR, TOP_HOST_FW_OWN);

    const MT7922_STATION_RADIO_PROFILE radio = {
        .Country = {'Z', 'Z'}, .ChannelCount = 9,
        .Channels = {{36, 10}, {40, 10}, {44, 10}, {48, 10},
            {149, 20}, {153, 20}, {157, 20}, {161, 20}, {165, 20}}
    };
    MT7922_MCU_STATION station = {0};
    assert(Mt7922McuStartStation(&station, sim.Registers, MT7922_CHIP_ID,
        &sim, Image, Bytes, 19, NULL, NULL, 0) == STATUS_INVALID_PARAMETER);
    MT7922_STATION_RADIO_PROFILE invalid = radio;
    invalid.Channels[4].Number = 52;
    assert(Mt7922McuStartStation(&station, sim.Registers, MT7922_CHIP_ID,
        &sim, Image, Bytes, 19, &invalid, NULL, 0) == STATUS_INVALID_PARAMETER);
    assert(sim.DmaEnables == 0);
    sim.RejectStationPse = RejectPse;
    assert(Mt7922McuStartStation(&station, sim.Registers, MT7922_CHIP_ID,
        &sim, Image, Bytes, 19, &radio, NULL, 0) ==
        ((RejectPort || RejectPse) ? STATUS_DEVICE_PROTOCOL_ERROR : STATUS_SUCCESS));
    assert(sim.DmaEnables == 1 && sim.ResetCount == 1 &&
        sim.StationBssWrites == (RejectPse ? 0U : 1U) && sim.StationRadioBatches == (RejectPse ? 1U : 2U));
    if (RejectPort || RejectPse) {
        assert(!station.Ready && station.Scan.Rings == NULL && sim.CommonBuffer == NULL);
        free(sim.Registers);
        return;
    }
    assert(station.Ready && station.Scan.Rings != NULL && sim.CommonBuffer != NULL);
    assert(!sim.SnifferEnabled && ReadStored(TX_ACTION_RING + RING_COUNT) == TX_ACTION_COUNT);
    assert(Mt7922McuStartStation(&station, sim.Registers, MT7922_CHIP_ID,
        &sim, Image, Bytes, 19, &radio, NULL, 0) == STATUS_DEVICE_BUSY);

    const MT7922_STATION_AP ap = {
        .Bssid = {2, 0x21, 0x22, 0x23, 0x24, 0x25}, .Channel = 149, .Band = 2,
        .DtimPeriod = 2, .BeaconInterval = 100, .Qos = true
    };
    MT7922_STATION_AP disallowed = ap;
    disallowed.Channel = 52;
    assert(Mt7922McuPrepareStationAp(&station, &disallowed) == STATUS_NOT_SUPPORTED);
    assert(sim.StationChannelSwitches == 0);
    assert(Mt7922McuPrepareStationAp(&station, &ap) == STATUS_SUCCESS && sim.StationChannelSwitches == 1);
    sim.RejectStationRlm = TRUE;
    assert(Mt7922McuPrepareStationAp(&station, &ap) == STATUS_DEVICE_PROTOCOL_ERROR);
    assert(station.Ready && sim.StationBssWrites == 6); /* Restored the disconnected BSS. */
    assert(sim.DmaEnables == 1 && sim.ResetCount == 1);

    STATION_POLL_TEST seen = {0};
    Mt7922McuSetScanReceiver(station.Scan.Rings, &seen, TestStationScanEvent, NULL);
    const MT7922_SCAN_PARAMETERS scan = {.Sequence = 1};
    UCHAR payload[MT7922_SCAN_REQUEST_BYTES];
    assert(Mt7922BuildScanRequest(payload, sizeof(payload), &scan));
    assert(Mt7922McuSubmitStationScan(&station.Scan, MT7922_SCAN_START_COMMAND,
        payload, sizeof(payload)) == STATUS_SUCCESS && sim.StationScans == 1);
    const uint8_t data[] = {24, [23] = 0};
    const uint8_t done[] = {56, 0, 0, 7U << 3, [28] = MT7922_SCAN_DONE_EVENT, [36] = 1, [55] = 0};
    PostDataDescriptor(data, sizeof(data));
    PostDataDescriptor(done, sizeof(done));
    Mt7922McuPollStation(&station, TestStationReceive, &seen);
    assert(seen.Events == 1 && seen.Records == 1);

    const MT7922_SCAN_CHANNEL requested[] = {{1, 6}, {2, 52}, {2, 149}};
    MT7922_SCAN_PARAMETERS limited = scan;
    limited.Channels = requested;
    limited.ChannelCount = RTL_NUMBER_OF(requested) - 1;
    assert(Mt7922BuildScanRequest(payload, sizeof(payload), &limited));
    assert(Mt7922McuSubmitStationScan(&station.Scan, MT7922_SCAN_START_COMMAND,
        payload, sizeof(payload)) == STATUS_NOT_SUPPORTED && sim.StationScans == 1);
    limited.ChannelCount = RTL_NUMBER_OF(requested);
    assert(Mt7922BuildScanRequest(payload, sizeof(payload), &limited));
    assert(Mt7922McuSubmitStationScan(&station.Scan, MT7922_SCAN_START_COMMAND,
        payload, sizeof(payload)) == STATUS_SUCCESS && sim.StationScans == 2);

    const UCHAR elements[] = {0, 3, 'L', 'A', 'N', 1, 3, 0x8c, 0x98, 0xb0};
    MT7922_STATION_JOIN_REQUEST join = {elements, sizeof(elements), 0x11, 3, 1};
    MT7922_STATION_JOIN_RESULT joined;
    assert(station.EapolRxPrivacy == Mt7922PrivacyClearUntilKey);
    assert(Mt7922McuExchangeStationJoin(&station, &join, &joined, NULL, NULL) == STATUS_DEVICE_NOT_READY);
    sim.RejectStationRlm = FALSE;
    assert(Mt7922McuPrepareStationAp(&station, &ap) == STATUS_SUCCESS);

    --join.InformationElementBytes;
    assert(Mt7922McuExchangeStationJoin(&station, &join, &joined, NULL, NULL) == STATUS_INVALID_PARAMETER);
    ++join.InformationElementBytes;
    assert(sim.StationMgmtSent == 0);

    sim.RejectStationAuth = TRUE;
    assert(Mt7922McuExchangeStationJoin(&station, &join, &joined, NULL, NULL) == STATUS_CONNECTION_REFUSED);
    assert(sim.StationMgmtSent == 1 && joined.Authentication.StatusCode == 13);
    sim.RejectStationAuth = FALSE;
    sim.RejectStationAssoc = TRUE;
    assert(Mt7922McuExchangeStationJoin(&station, &join, &joined, NULL, NULL) == STATUS_CONNECTION_REFUSED);
    assert(sim.StationMgmtSent == 3 && joined.Association.StatusCode == 17);
    sim.RejectStationAssoc = FALSE;

    sim.OmitStationRelease = TRUE;
    assert(Mt7922McuExchangeStationJoin(&station, &join, &joined, NULL, NULL) == STATUS_IO_TIMEOUT);
    assert(sim.StationMgmtSent == 4 && sim.HeldStationToken != 0);
    assert(Mt7922McuExchangeStationJoin(&station, &join, &joined, NULL, NULL) == STATUS_DEVICE_BUSY);
    PostStationRelease(sim.HeldStationToken);
    Mt7922McuPollStation(&station, NULL, NULL);
    sim.OmitStationRelease = FALSE;
    assert(Mt7922McuExchangeStationJoin(&station, &join, &joined, NULL, NULL) == STATUS_SUCCESS);
    assert(sim.StationMgmtSent == 6 && joined.Association.AssociationId == 7);
    assert(joined.Association.BodyBytes == 6 && joined.Authentication.BodyBytes == 6);
    assert(station.AssociationId == 7 && station.PeerWcid == 1 && sim.StationGroupCommands == 1);
    assert(sim.StationPeerCommands == 5);
    assert(sim.DmaEnables == 1 && sim.ResetCount == 1);

    EAPOL_TEST eapol = {0};
    assert(station.PendingFrameBytes == 38 && station.ConnectDebug.Eapol.RxEarly != 0);
    Mt7922McuPollStationData(&station, TestEapolReceive, &eapol);
    assert(eapol.Delivered == 0);
    station.WdiAssociationReported = true;
    eapol.Blocked = true;
    Mt7922McuPollStationData(&station, TestEapolReceive, &eapol);
    assert(station.PendingFrameBytes == 38 && eapol.Delivered == 0);
    eapol.Blocked = false;
    Mt7922McuPollStationData(&station, TestEapolReceive, &eapol);
    assert(station.PendingFrameBytes == 0 && eapol.Delivered == 1);

    UCHAR m2[] = {
        2, 0x21, 0x22, 0x23, 0x24, 0x25, 2, 0x44, 0x33, 0x22, 0x11, 1,
        0x88, 0x8e, 2, 3, 0, 2, 0, 2
    };
    assert(Mt7922McuSubmitStationScan(&station.Scan, MT7922_SCAN_START_COMMAND,
        payload, sizeof(payload)) == STATUS_DEVICE_BUSY && sim.StationScans == 2);
    assert(Mt7922McuSendStationData(&station, m2, sizeof(m2), Mt7922PrivacyRequireKey) ==
        STATUS_INVALID_DEVICE_STATE);
    assert(Mt7922McuSendStationData(&station, m2, sizeof(m2) - 1, Mt7922PrivacyClearUntilKey) == STATUS_INVALID_PARAMETER);
    uint8_t hostM2[] = {
        0x88, 0x01, 0, 0,
        2, 0x21, 0x22, 0x23, 0x24, 0x25, 2, 0x44, 0x33, 0x22, 0x11, 1,
        2, 0x21, 0x22, 0x23, 0x24, 0x25, 0, 0, 0, 0,
        0xaa, 0xaa, 0x03, 0, 0, 0, 0x88, 0x8e, 2, 3, 0, 2, 0, 2
    };
    assert(Mt7922ConvertStationTxData(hostM2, sizeof(hostM2) - 1, station.Info.NicMac, ap.Bssid) == 0);
    assert(Mt7922ConvertStationTxData(hostM2, sizeof(hostM2), station.Info.NicMac, ap.Bssid) == sizeof(m2));
    assert(memcmp(hostM2, m2, sizeof(m2)) == 0);
    assert(Mt7922McuSendStationData(&station, hostM2, sizeof(m2), Mt7922PrivacyClearUntilKey) == STATUS_SUCCESS);
    assert(Mt7922McuSendStationData(&station, m2, sizeof(m2), Mt7922PrivacyClearUntilKey) == STATUS_DEVICE_BUSY);
    PostStationEapol(true, false, 99);
    PostStationEapol(false, true, 99);
    PostStationEapol(false, false, 3);
    Mt7922McuPollStationData(&station, TestEapolReceive, &eapol);
    assert(eapol.Delivered == 2 && sim.StationEapolSent == 1);

    UCHAR key[MT7922_CCMP_KEY_BYTES];
    memset(key, 0x31, sizeof(key));
    assert(Mt7922McuInstallStationCcmp(&station, false, 0, key, 0) == STATUS_SUCCESS);
    sim.DelayStationReleaseToGroupKey = TRUE;
    assert(Mt7922McuSendStationData(&station, m2, sizeof(m2), Mt7922PrivacyClearUntilKey) == STATUS_SUCCESS);
    assert(sim.StationKeyCommands == 1 && sim.StationEapolSent == 2 && sim.ProtectedStationEapolSent == 1);

    const uint32_t ccmp = (1U << 31) | (4U << 16) | 1U; /* SECURITY_DONE, CCMP, peer WCID. */
    const uint32_t policyDrops = station.ConnectDebug.Eapol.RxPolicyDrop;
    PostStationEapol(false, false, 99); /* No plaintext after PTK. */
    PostStationCcmpEapol(0x100, ccmp ^ (1U << 31), 99);
    PostStationCcmpEapol(0x100, ccmp | (1U << 25), 99); /* ICV error. */
    PostStationCcmpEapol(0x100, ccmp ^ 2U, 99); /* Wrong WCID. */
    PostStationCcmpEapol(0x100, ccmp | (1U << 21), 99); /* Wrong key index. */
    PostStationCcmpEapol(0x100, ccmp, 4);
    Mt7922McuPollStationData(&station, TestEapolReceive, &eapol);
    assert(eapol.Delivered == 3 && station.PairwiseRxPn[0] == 0x100);
    assert(station.ConnectDebug.Eapol.RxPolicyDrop == policyDrops + 1);
    assert(station.ConnectDebug.Eapol.TxNeedKey == 1 && station.ConnectDebug.Eapol.TxSubmitted == 2);

    PostStationCcmpEapol(0x100, ccmp, 99);
    PostStationCcmpEapol(0xff, ccmp, 99);
    PostStationCcmpEapol(0x101, ccmp, 5);
    PostStationCcmpData(0x102, ccmp, false, true); /* Both keys are required for ordinary data. */
    UCHAR arp[42] = {
        255, 255, 255, 255, 255, 255, 2, 0x44, 0x33, 0x22, 0x11, 1,
        0x08, 0x06, 0, 1, 8, 0, 6, 4, 0, 1
    };
    assert(Mt7922McuSendStationData(&station, arp, sizeof(arp), Mt7922PrivacyRequireKey) ==
        STATUS_INVALID_DEVICE_STATE);
    Mt7922McuPollStationData(&station, TestEapolReceive, &eapol);
    assert(eapol.Delivered == 4 && station.PairwiseRxPn[0] == 0x101);
    const UCHAR zeroCommand[48 + MT7922_CCMP_COMMAND_BYTES] = {0};
    assert(memcmp(sim.CommonBuffer + TX_PAYLOAD_OFFSET, zeroCommand, sizeof(zeroCommand)) == 0);
    memset(key, 0x62, sizeof(key));
    sim.RejectStationKey = RejectGroupKey;
    assert(Mt7922McuInstallStationCcmp(&station, true, 2, key, 0x30) ==
        (RejectGroupKey ? STATUS_DEVICE_PROTOCOL_ERROR : STATUS_SUCCESS));
    /* A successful GTK install is followed by a non-key port command in the same buffer. */
    const size_t portCommandBytes = RejectGroupKey ? 0 : 76;
    assert(memcmp(sim.CommonBuffer + TX_PAYLOAD_OFFSET + portCommandBytes,
        zeroCommand + portCommandBytes, sizeof(zeroCommand) - portCommandBytes) == 0);
    assert(station.KeyStateInvalid == RejectGroupKey && station.GroupKeyInstalled != RejectGroupKey);
    assert(sim.StationKeyCommands == 2);
    assert(station.ConnectDebug.Data.TxReleaseDuringCommand == 1);
    assert((BOOLEAN)(sim.StationPortSecureCommands != 0) == (BOOLEAN)!RejectGroupKey);
    assert(station.FirmwarePortSecured == (BOOLEAN)!RejectGroupKey &&
        station.ConnectDebug.Data.PortSecured == (ULONG)!RejectGroupKey);
    if (!RejectGroupKey) {
        assert(Mt7922McuSendStationData(&station, arp, sizeof(arp), Mt7922PrivacyRequireKey) == STATUS_SUCCESS);
        assert(sim.StationArpSent == 1);
        unsigned receivedArp = 0;
        const uint32_t groupCcmp = ccmp | (2U << 21);
        PostStationCcmpData(0x102, ccmp, false, false); /* Unprotected. */
        PostStationCcmpData(0x102, ccmp, false, true);
        PostStationCcmpData(0x102, ccmp, false, true); /* Pairwise replay. */
        PostStationCcmpData(0x30, groupCcmp, true, true); /* At the installed replay floor. */
        PostStationCcmpData(0x31, groupCcmp ^ (1U << 21), true, true); /* Wrong GTK index. */
        PostStationCcmpData(0x31, groupCcmp, true, true);
        PostStationCcmpData(0x31, groupCcmp, true, true); /* Group replay. */
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        assert(station.ConnectDebug.Data.TxCompleted == 1 && station.ConnectDebug.Data.TxCompletionErrors == 0);
        assert(receivedArp == 2 && station.PairwiseRxPn[0] == 0x102 &&
            station.GroupRxPn[0] == 0x31 && station.GroupRxPn[15] == 0x30);
        /* Ordinary station RX can feed the observer while still delivering encrypted ARP. */
        assert(station.Observer.Running && station.Observer.Channel == ap.Channel &&
            station.ObserverDeadline != 0 && station.ConnectDebug.Observation.Channel == ap.Channel);
        assert(sim.StationSnifferCommands == 0 && !sim.StationSnifferEnabled);
        sim.ListenChannel = ap.Channel;
        InjectAwdl(0x7e);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        assert(station.Observer.Mif == 1 && receivedArp == 2 && station.FirmwarePortSecured);
        station.ObserverDeadline = KeQueryInterruptTime();
        InjectAwdl(0x7e);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        assert(!station.Observer.Running && station.ObserverDeadline == 0 && station.Observer.Mif == 1);

        static const MT7922_SHARED_ADVERT_REQUEST advert = {.RequestId = 7, .Peer = {0x7e, 0, 0, 0, 0, 1}};
        assert(Mt7922McuArmSharedAdvert(&station, &advert) == STATUS_SUCCESS);
        assert(Mt7922McuArmSharedAdvert(&station, &advert) == STATUS_DEVICE_BUSY);
        InjectAwdl(advert.Peer[0]); /* Its advertised current slot is not 149. */
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(sim.StationSharedAdverts == 0);
        sim.StationSharedSlot = true;
        InjectAwdl(0x7c); /* An unrelated peer cannot start the transmission. */
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(sim.StationSharedAdverts == 0);
        InjectAwdl(advert.Peer[0]);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, true); /* An ordinary host frame takes priority. */
        assert(sim.StationSharedAdverts == 0);
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(sim.StationSharedAdverts == 1 && station.ConnectDebug.SharedAdvert.Submitted == 1);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        assert(station.ConnectDebug.SharedAdvert.Released == 0 &&
            station.ConnectDebug.SharedAdvert.DmaFetched == 1 &&
            station.ConnectDebug.SharedAdvert.TxReportSeen == 1 &&
            station.ConnectDebug.SharedAdvert.TxRateCode == MT7922_OFDM_6_MBPS);
        assert(Mt7922McuSendStationData(&station, arp, sizeof(arp), Mt7922PrivacyRequireKey) == STATUS_SUCCESS);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(station.ConnectDebug.SharedAdvert.Released == 1 &&
            station.ConnectDebug.SharedAdvert.Status == STATUS_SUCCESS &&
            station.ConnectDebug.Data.TxCompleted == 2 && sim.StationArpSent == 2);
        assert(sim.StationSnifferCommands == 0 && station.FirmwarePortSecured);

        MT7922_SHARED_ADVERT_REQUEST echo = advert;
        echo.RequestId = 8;
        echo.Echo = 1;
        assert(Mt7922McuArmSharedAdvert(&station, &echo) == STATUS_SUCCESS);
        InjectAwdl(echo.Peer[0]);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(sim.StationSharedAdverts == 2 && sim.StationSharedEchoes == 0);
        assert(Mt7922McuSendStationData(&station, arp, sizeof(arp), Mt7922PrivacyRequireKey) == STATUS_SUCCESS);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(station.ConnectDebug.SharedAdvert.Status == STATUS_SUCCESS &&
            station.ConnectDebug.SharedEcho.Status == STATUS_PENDING);
        InjectAwdl(echo.Peer[0]);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, false);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(sim.StationSharedEchoes == 1 && station.ConnectDebug.SharedEcho.Released == 1 &&
            station.ConnectDebug.SharedEcho.Status == STATUS_PENDING && station.ConnectDebug.SharedEchoReplies == 0);
        PostIcmp6Reply();
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(station.ConnectDebug.SharedEcho.Status == STATUS_SUCCESS && station.ConnectDebug.SharedEchoReplies == 1);
        assert(station.ConnectDebug.Data.TxCompleted == 3 && station.FirmwarePortSecured);

        MT7922_AWDL_LINK runtime = {
            .Context = &sim, .Stopped = TestLinkStopped, .Ready = TestLinkReady,
            .Receive = TestSharedLinkReceive, .TakeSend = TestSharedLinkTakeSend,
            .AnnounceMdns = TRUE, .ServiceLabel = TestAirDropLabel, .ReceivePort = 8771
        };
        assert(Mt7922McuSharedAvailable(&station));
        Mt7922McuAttachSharedLink(&station, &runtime, echo.Peer);
        assert(Mt7922McuArmSharedAdvert(&station, &echo) == STATUS_DEVICE_BUSY);
        InjectAwdl(echo.Peer[0]);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(sim.StationSharedAdverts == 3 && sim.StationSharedServiceAdverts == 0 && !sim.LinkReady);
        assert(Mt7922McuSendStationData(&station, arp, sizeof(arp), Mt7922PrivacyRequireKey) == STATUS_SUCCESS);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(sim.LinkReady && runtime.SharedMifReleased == 1 && runtime.Submitted == 1);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(sim.LinkReceived == 1 && runtime.Acked == 1 && runtime.NoAck == 0 &&
            station.ConnectDebug.Data.TxCompleted == 4);
        sim.StationSharedQueueAnnouncement = true;
        InjectAwdl(echo.Peer[0]);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(sim.StationSharedAnnouncements == 1 && runtime.MdnsAnnounced == 0);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(runtime.MdnsAnnounced == 1 && runtime.NoAck == 0);
        sim.Counter += (AWDL_SHARED_MIF_INTERVAL_MS + 1ULL) * 10000ULL;
        InjectAwdl(echo.Peer[0]);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(sim.StationSharedServiceAdverts == 1 && runtime.ServiceMifsReleased == 0);
        sim.CombineStationAndSharedRelease = false;
        PostStationRelease(STATION_SHARED_TOKEN);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(runtime.ServiceMifsReleased == 1 && runtime.SharedMifReleased == 2);

        /* A projected slot opens without a fresh sync or TCP packet.
           Station traffic still takes priority, and TX-free permits the next reply. */
        sim.StationSharedExtraEchoes = 2;
        sim.OmitSharedUnicastReport = true;
        sim.Counter += 8 * TU_TICKS;
        station.SharedLinkDataUntil = 0;
        assert(IsStationPeerChannelWindow(&station, AwdlHostTime()));
        Mt7922McuServiceSharedAdvert(&station, true);
        assert(sim.StationSharedExtraEchoes == 2 && runtime.Submitted == 2);
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(sim.StationSharedExtraEchoes == 1 && runtime.Submitted == 3 &&
            runtime.UploadWindowReplies == 1);
        sim.OmitSharedUnicastReport = false;
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(sim.StationSharedExtraEchoes == 0 && runtime.Submitted == 4 &&
            runtime.SharedTxStatusMissing == 1 && runtime.NoAck == 0);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(runtime.Acked == 2 && runtime.NoAck == 0 && runtime.SharedTxStatusMissing == 1);

        sim.QueueSharedSynAck = true;
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(sim.SharedSynAcks == 1 && runtime.SharedSynAckSubmitted == 1);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(sim.SharedSynAcks == 1 && runtime.SharedSynAckFailed == 1);
        sim.Counter += 4 * TU_TICKS;
        InjectAwdl(echo.Peer[0]);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, true);
        assert(sim.SharedSynAcks == 1 && runtime.SharedSynAckRetries == 0);
        Mt7922McuServiceSharedAdvert(&station, false);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(sim.SharedSynAcks == 2 && runtime.SharedSynAckSubmitted == 2 &&
            runtime.SharedSynAckAcked == 1 && runtime.SharedSynAckRetries == 1);

        sim.HoldSharedRelease = true;
        sim.QueueSharedSynAck = true;
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(sim.SharedSynAcks == 3 && !station.SharedLinkTx.Released);
        sim.Counter += 10000001ULL;
        Mt7922McuServiceSharedAdvert(&station, false);
        assert(station.SharedLinkStatus == STATUS_IO_TIMEOUT &&
            !Mt7922McuSharedAvailable(&station) && station.FirmwarePortSecured);
        assert(Mt7922McuSendStationData(&station, arp, sizeof(arp), Mt7922PrivacyRequireKey) == STATUS_SUCCESS);
        sim.HoldSharedRelease = false;
        PostStationRelease(STATION_SHARED_TOKEN);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        Mt7922McuDetachSharedLink(&station);
        assert(Mt7922McuSharedAvailable(&station) && station.FirmwarePortSecured);
        sim.StationSharedSlot = false;

        /* The separate firmware observer remains an explicit entry point. */
        assert(Mt7922McuStartStationObserver(&station, 44) == STATUS_INVALID_DEVICE_STATE);
        assert(sim.StationSnifferCommands == 0 && !sim.StationSnifferEnabled);
        assert(Mt7922McuStartStationObserver(&station, ap.Channel) == STATUS_SUCCESS);
        assert(sim.StationSnifferEnabled && sim.StationSnifferCommands == 2 &&
            station.Observer.Running && station.Observer.Channel == ap.Channel);
        assert(Mt7922McuStartStationObserver(&station, ap.Channel) == STATUS_DEVICE_BUSY &&
            sim.StationSnifferCommands == 2);

        static const uint8_t observerPeer[6] = {0x7e, 0, 0, 0, 0, 1};
        sim.ListenChannel = ap.Channel;
        InjectAwdl(observerPeer[0]);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        assert(station.Observer.Mif == 1 && station.Observer.Psf == 0 &&
            station.Observer.PeerCount == 1);
        MT7922_OBSERVER_PEER *observed = &station.Observer.Peers[0];
        assert(memcmp(observed->Address, observerPeer, sizeof(observerPeer)) == 0 &&
            observed->Mif == 1 && observed->Ios);
        assert(memcmp(observed->Master, observerPeer, sizeof(observerPeer)) == 0 &&
            observed->NextChannel == ap.Channel && observed->WindowsPerSlot == 4);
        assert(observed->SlotCount == 16 && observed->Slots[1] == ap.Channel &&
            observed->Slots[9] == ap.Channel);

        sim.ListenChannel = 44;
        InjectAwdl(observerPeer[0]);
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        assert(station.Observer.Psf == 1 && station.Observer.Mif == 1 &&
            observed->Psf == 1 && !observed->Ios);

        static const uint8_t awdlBssid[6] = {0, 0x25, 0, 0xff, 0x94, 0x73};
        UCHAR dataRecord[24 + 24] = {0};
        PutUint32(dataRecord, (2U << 27) | sizeof(dataRecord));
        UCHAR *awdlData = dataRecord + 24;
        awdlData[0] = 0x08;
        memcpy(awdlData + 4, station.Info.NicMac, 6);
        memcpy(awdlData + 10, observerPeer, sizeof(observerPeer));
        memcpy(awdlData + 16, awdlBssid, sizeof(awdlBssid));
        PostDataDescriptor(dataRecord, sizeof(dataRecord));
        awdlData[4] ^= 0x03; /* The next copy is not addressed to us. */
        PostDataDescriptor(dataRecord, sizeof(dataRecord));
        Mt7922McuPollStationData(&station, TestArpReceive, &receivedArp);
        assert(station.Observer.Data == 2 && station.Observer.DataToUs == 1 &&
            observed->Data == 2);

        assert(Mt7922McuDisconnectStation(&station, 3) == STATUS_SUCCESS);
        assert(!station.Observer.Running && !sim.StationSnifferEnabled &&
            sim.StationSnifferCommands == 3);
        assert(sim.StationDeauthSent == 1 && sim.StationKeysRemoved == 2 && sim.StationPeersReset == 2);
        assert(station.Ready && station.AssociationId == 0 && !station.Scan.Associated &&
            !station.WdiAssociationReported && !station.PairwiseKeyInstalled && !station.GroupKeyInstalled &&
            station.PairwiseRxPn[0] == 0 && station.GroupRxPn[15] == 0 && station.Ap.Band == 0);
        assert(sim.DmaEnables == 1 && sim.ResetCount == 1);
        assert(Mt7922McuSendStationData(&station, arp, sizeof(arp), Mt7922PrivacyRequireKey) ==
            STATUS_INVALID_DEVICE_STATE);

        assert(Mt7922McuPrepareStationAp(&station, &ap) == STATUS_SUCCESS);
        assert(Mt7922McuExchangeStationJoin(&station, &join, &joined, NULL, NULL) == STATUS_SUCCESS);
        station.WdiAssociationReported = true;
        station.PendingFrameBytes = 0; /* This test exercises loss of the new association. */
        UCHAR peerFrame[50] = {sizeof(peerFrame), 0, 0, 0x10, [24] = 0xa0, [48] = 4};
        memcpy(peerFrame + 28, station.Info.NicMac, 6);
        memcpy(peerFrame + 34, ap.Bssid, 6);
        memcpy(peerFrame + 40, ap.Bssid, 6);
        peerFrame[34] ^= 0x10;
        PostDataDescriptor(peerFrame, sizeof(peerFrame));
        Mt7922McuPollStationData(&station, TestEapolReceive, &eapol);
        assert(station.PeerDisconnectFrame[0] == 0);
        peerFrame[34] ^= 0x10;
        PostDataDescriptor(peerFrame, sizeof(peerFrame));
        Mt7922McuPollStationData(&station, TestEapolReceive, &eapol);
        assert(station.PeerDisconnectFrame[0] == 0xa0 && station.PeerDisconnectFrame[24] == 4);
        assert(Mt7922McuDisconnectStation(&station, 0) == STATUS_SUCCESS);
        assert(sim.StationDeauthSent == 1 && sim.StationKeysRemoved == 2 && sim.StationPeersReset == 4 &&
            sim.ResetCount == 1 && !station.Scan.Associated);
    }

    sim.Scenario = STUCK_DMA;
    assert(Mt7922McuStopStation(&station) == STATUS_IO_TIMEOUT);
    assert(!station.Ready && station.Scan.Rings != NULL && sim.CommonBuffer != NULL);
    sim.Scenario = FULL_BOOT;
    Store(DMA_GLOBAL, ReadStored(DMA_GLOBAL) & ~DMA_BUSY);
    assert(Mt7922McuStopStation(&station) == STATUS_SUCCESS);
    assert(station.Scan.Rings == NULL && sim.CommonBuffer == NULL &&
        sim.ResetCount == (RejectGroupKey ? 2U : 1U));
    assert(Mt7922McuStopStation(&station) == STATUS_SUCCESS);
    free(sim.Registers);
}

static void VerifyUnitTests(void)
{
    VerifyAdvertTiming();
    VerifyUploadReplyTiming();
    VerifyReceiveBaReordering();
    VerifyVhtRxStats();
    VerifyStationScan();
    VerifyMt7925ScanBuilders();
    VerifyStationObserverBuilders();
    VerifyMt7925StationRadioBuilders();
    VerifyStationClcSelection();
    VerifyWificxScan();
    VerifyWificxNetRings();
    VerifyFastAckReport();
    VerifyPeerSelection();
}

int wmain(int argc, wchar_t **argv)
{
    if (argc == 2 && wcscmp(argv[1], L"--unit-only") == 0) {
        VerifyUnitTests();
        puts("Firmware-independent MCU, scan, WiFiCx, and ring checks passed.");
        return 0;
    }
    assert(argc == 3);
    VerifyUnitTests();
    PUCHAR image = ReadImage(argv[1], MT7922_FACTORY_IMAGE_BYTES);
    PUCHAR reference = ReadImage(argv[2], MT7922_REFERENCE_IMAGE_BYTES);

    VerifyStationClcBoot(reference, MT7922_REFERENCE_IMAGE_BYTES, false);
    VerifyStationClcBoot(reference, MT7922_REFERENCE_IMAGE_BYTES, true);
    VerifyStationLifecycle(reference, MT7922_REFERENCE_IMAGE_BYTES, false, false, false);
    VerifyStationLifecycle(reference, MT7922_REFERENCE_IMAGE_BYTES, false, true, false);
    VerifyStationLifecycle(reference, MT7922_REFERENCE_IMAGE_BYTES, true, false, false);
    VerifyStationLifecycle(reference, MT7922_REFERENCE_IMAGE_BYTES, false, false, true);
    VerifyScenario(NORMAL, STATUS_SUCCESS, MT7922_MCU_COMPLETE, NULL);
    VerifyScenario(PATCH_PRESENT, STATUS_SUCCESS, MT7922_MCU_COMPLETE, NULL);
    VerifyScenario(STALE_RING, STATUS_SUCCESS, MT7922_MCU_COMPLETE, NULL);
    VerifyScenario(LIVE_DMA, STATUS_DEVICE_BUSY, MT7922_MCU_RINGS, NULL);
    VerifyScenario(LOCKED_RING, STATUS_DEVICE_HARDWARE_ERROR, MT7922_MCU_RINGS, NULL);
    VerifyScenario(RESET_IGNORED, STATUS_IO_TIMEOUT, MT7922_MCU_RINGS, NULL);
    VerifyScenario(SCHEDULER_WRITE_IGNORED, STATUS_DEVICE_HARDWARE_ERROR, MT7922_MCU_RINGS, NULL);
    VerifyScenario(TOP_OWN_TIMEOUT, STATUS_IO_TIMEOUT, MT7922_MCU_RINGS, NULL);
    VerifyScenario(DOORBELL_IGNORED, STATUS_DEVICE_HARDWARE_ERROR, MT7922_MCU_GET, NULL);
    VerifyScenario(TX_TIMEOUT, STATUS_IO_TIMEOUT, MT7922_MCU_GET, NULL);
    VerifyScenario(RX_TIMEOUT, STATUS_IO_TIMEOUT, MT7922_MCU_GET, NULL);
    VerifyScenario(RELEASE_FAILED, STATUS_DEVICE_PROTOCOL_ERROR, MT7922_MCU_RELEASE, NULL);
    VerifyScenario(STUCK_DMA, STATUS_IO_TIMEOUT, MT7922_MCU_RELEASE, NULL);
    VerifyScenario(FULL_BOOT, STATUS_SUCCESS, MT7922_MCU_COMPLETE, image);
    VerifyScenario(BOOT_PATCH_PRESENT, STATUS_SUCCESS, MT7922_MCU_COMPLETE, image);
    VerifyScenario(BOOT_SCATTER_TIMEOUT, STATUS_IO_TIMEOUT, MT7922_MCU_PATCH, image);
    VerifyScenario(BOOT_NOT_READY, STATUS_IO_TIMEOUT, MT7922_MCU_START, image);
    VerifyScenario(BOOT_FIXED_READY, STATUS_SUCCESS, MT7922_MCU_COMPLETE, image);
    VerifyScenario(BOOT_ROM_REJECT_START, STATUS_DEVICE_PROTOCOL_ERROR, MT7922_MCU_START, image);
    VerifyScenario(BOOT_ROM_REJECT_PATCH, STATUS_DEVICE_PROTOCOL_ERROR, MT7922_MCU_PATCH, image);
    VerifyScenario(BOOT_ROM_SHORT, STATUS_DEVICE_PROTOCOL_ERROR, MT7922_MCU_START, image);
    VerifyScenario(RECEIVE_AWDL, STATUS_SUCCESS, MT7922_MCU_COMPLETE, image);
    VerifyScenario(RECEIVE_EMPTY, STATUS_SUCCESS, MT7922_MCU_COMPLETE, image);
    VerifyScenario(RECEIVE_REJECTED, STATUS_DEVICE_PROTOCOL_ERROR, MT7922_MCU_RECEIVE_CHANNEL, image);
    VerifyScenario(BOOT_CAP_ROM_RING, STATUS_SUCCESS, MT7922_MCU_COMPLETE, image);
    VerifyScenario(BOOT_CAP_TIMEOUT, STATUS_IO_TIMEOUT, MT7922_MCU_CAPABILITIES, image);
    VerifyScenario(BOOT_CAP_MALFORMED, STATUS_DEVICE_PROTOCOL_ERROR, MT7922_MCU_CAPABILITIES, image);
    VerifyScenario(BOOT_CAP_NO_PHY, STATUS_DEVICE_PROTOCOL_ERROR, MT7922_MCU_CAPABILITIES, image);
    image[0] ^= 1;
    VerifyScenario(BOOT_CORRUPT, STATUS_DATA_ERROR, MT7922_MCU_VALIDATE_IMAGE, image);
    free(image);
    VerifyImageScenario(RECEIVE_AWDL, STATUS_SUCCESS, MT7922_MCU_COMPLETE,
        reference, MT7922_REFERENCE_IMAGE_BYTES);
    VerifyImageScenario(TRANSMIT_ACK, STATUS_SUCCESS, MT7922_MCU_COMPLETE,
        reference, MT7922_REFERENCE_IMAGE_BYTES);
    VerifyImageScenario(TRANSMIT_NO_ACK, STATUS_DEVICE_PROTOCOL_ERROR, MT7922_MCU_TX_REPORT,
        reference, MT7922_REFERENCE_IMAGE_BYTES);
    VerifyImageScenario(TRANSMIT_NO_REPORT, STATUS_SUCCESS, MT7922_MCU_COMPLETE,
        reference, MT7922_REFERENCE_IMAGE_BYTES);
    VerifyImageScenario(TRANSMIT_RETRY, STATUS_SUCCESS, MT7922_MCU_COMPLETE,
        reference, MT7922_REFERENCE_IMAGE_BYTES);
    VerifyImageScenarioWithOptions(TRANSMIT_RETRY, STATUS_SUCCESS, MT7922_MCU_COMPLETE,
        reference, MT7922_REFERENCE_IMAGE_BYTES, TRUE, FALSE, FALSE, MT7922_OFDM_6_MBPS, NULL, FALSE, FALSE, FALSE);
    VerifyImageScenarioWithOptions(TRANSMIT_RETRY, STATUS_SUCCESS, MT7922_MCU_COMPLETE,
        reference, MT7922_REFERENCE_IMAGE_BYTES, TRUE, TRUE, FALSE, MT7922_OFDM_6_MBPS, NULL, FALSE, FALSE, FALSE);
    VerifyImageScenarioWithOptions(TRANSMIT_RETRY, STATUS_SUCCESS, MT7922_MCU_COMPLETE,
        reference, MT7922_REFERENCE_IMAGE_BYTES, TRUE, FALSE, TRUE, MT7922_OFDM_6_MBPS, NULL, FALSE, FALSE, FALSE);
    VerifyImageScenarioWithOptions(TRANSMIT_RETRY, STATUS_SUCCESS, MT7922_MCU_COMPLETE,
        reference, MT7922_REFERENCE_IMAGE_BYTES, TRUE, TRUE, TRUE, MT7922_OFDM_6_MBPS, NULL, FALSE, FALSE, FALSE);
    VerifyImageScenarioWithOptions(TRANSMIT_RETRY, STATUS_SUCCESS, MT7922_MCU_COMPLETE,
        reference, MT7922_REFERENCE_IMAGE_BYTES, TRUE, TRUE, TRUE, MT7922_OFDM_12_MBPS, NULL, FALSE, FALSE, FALSE);
    VerifyImageScenarioWithOptions(TRANSMIT_RETRY, STATUS_SUCCESS, MT7922_MCU_COMPLETE,
        reference, MT7922_REFERENCE_IMAGE_BYTES, TRUE, FALSE, TRUE, MT7922_OFDM_6_MBPS, NULL, TRUE, FALSE, FALSE);
    VerifyImageScenarioWithOptions(TRANSMIT_RETRY, STATUS_SUCCESS, MT7922_MCU_COMPLETE,
        reference, MT7922_REFERENCE_IMAGE_BYTES, TRUE, TRUE, TRUE, MT7922_OFDM_6_MBPS, NULL, TRUE, FALSE, FALSE);
    VerifyImageScenarioWithOptions(TRANSMIT_RETRY, STATUS_SUCCESS, MT7922_MCU_COMPLETE,
        reference, MT7922_REFERENCE_IMAGE_BYTES, TRUE, TRUE, TRUE, MT7922_OFDM_6_MBPS, NULL, TRUE, TRUE, FALSE);
    VerifyImageScenarioWithOptions(TRANSMIT_RETRY, STATUS_SUCCESS, MT7922_MCU_COMPLETE,
        reference, MT7922_REFERENCE_IMAGE_BYTES, TRUE, TRUE, TRUE, MT7922_OFDM_6_MBPS, NULL, TRUE, FALSE, TRUE);
    static const UCHAR watchPeers[MT7922_AWDL_WATCH_PEER_BYTES] = {
        0x7e, 0, 0, 0, 0, 1, 0x8e, 0, 0, 0, 0, 1
    };
    MT7922_AWDL_WATCH watch = {watchPeers};
    VerifyImageScenarioWithOptions(RECEIVE_AWDL, STATUS_SUCCESS, MT7922_MCU_COMPLETE,
        reference, MT7922_REFERENCE_IMAGE_BYTES, TRUE, FALSE, FALSE, MT7922_OFDM_6_MBPS, &watch, FALSE, FALSE, FALSE);
    VerifyImageScenario(TRANSMIT_NO_PEER, STATUS_IO_TIMEOUT, MT7922_MCU_TX_FIND_PEER,
        reference, MT7922_REFERENCE_IMAGE_BYTES);
    reference[0] ^= 1;
    VerifyImageScenario(BOOT_CORRUPT, STATUS_DATA_ERROR, MT7922_MCU_VALIDATE_IMAGE,
        reference, MT7922_REFERENCE_IMAGE_BYTES);
    free(reference);
    puts("MCU/DMA simulations passed, including WiFiCx connect, NetAdapterCx station rings, CCMP EAPOL and data, and PulseIt Block Ack.");
    return 0;
}
