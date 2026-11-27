#ifndef MT7922_MCU_TEST
#include <ntddk.h>
#include <wdf.h>
#endif
#include "probe.h"
#include "mcu.h"
#ifdef MT7922_STATION_PACKET_TRACE
#include "station-packet-trace.h"
#endif
#include "station-scan.h"
#include "rx-queue.h"
#include "tx.h"
#include <bcrypt.h>

/* MT7922 PCIe register and descriptor layout from mt76/mt7921 and mt792x_dma.c. */
#define HIF_REMAP           0xfe24c
#define HIF_WINDOW          0x40000
#define HIF_BANK_MASK       0xffff
#define MT7925_HIF_REMAP_L1 0x155024
#define MT7925_HIF_REMAP_MASK 0xffff0000
#define MT7925_HIF_WINDOW_L1 0x130000
#define MT7925_CONN_ON_WINDOW 0x0e0000
#define WFSYS_BANK          0x1800
#define WFSYS_RESET         0x0140
#define WFSYS_RESET_RELEASE 0x0001
#define WFSYS_INIT_DONE     0x0010
#define CONNECTION_BANK     0x7c06
#define CONNECTION_OWN      0x0010
#define CONNECTION_FIRMWARE 0x00f0
#define CONNECTION_BAR_BASE 0xe0000
#define CONNECTION_READY    0x0003
#define MT7925_WTBL_ITCR    0x343b0U
#define MT7925_WTBL_ITDR0   0x343b8U
#define MT7925_WTBL_ITDR1   0x343bcU
#define MT7925_WTBL_ITCR_WR (1U << 16)
#define MT7925_WTBL_ITCR_EXEC (1U << 31)
#define MT7925_WTBL_SPE_IDX_SEL (1U << 6)
#define MT7925_BASIC_RATES_TABLE 14U
#define HOST_OWN_REQUEST    0x0002
#define FIRMWARE_OWNS       0x0004
#define TOP_HOST_BANK       0x1806
#define TOP_HOST_LPCR       0x0010
#define TOP_HOST_FW_OWN     0x0001
#define DMASHDL_CONTROL     0xd6004
#define DMASHDL_BYPASS      0x10000000
/* mt792x_regs.h PLE AC-empty bitmap at mt7921 PCI BAR0's fixed UMAC offset. */
#define PLE_AC_QEMPTY(Ac)   (0x08500U + 0x40U * (Ac))
#define STATION_SHARED_AC   1U
/* mt7921 PCIe fixed mapping of MT_WTBL_LMAC_OFFS for WCIDs in group 0. */
#define LMAC_WTBL_OFFSET    0x38000U
#define LMAC_WTBL_STRIDE    0x100U

#define DMA_GLOBAL          0xd4208
#define DMA_GLOBAL_EXT      0xd42b0
#define DMA_RESET           0xd4100
#define DMA_RESET_LOGIC     0x0030
#define DMA_RESET_TX_INDEX  0xd420c
#define DMA_HOST_INT_STATUS 0xd4200
#define MCU_FIRMWARE_MODE   0x9f23c
#define DMA_HOST_INT_ENABLE 0xd4204
#define PCIE_INT_ENABLE     0x10188
#define PCIE_MAC_PM         0x10194
#define PCIE_MAC_PM_L0S_DIS 0x0100
#define DMA_DELAY_INTERRUPT 0xd42f0
#define DMA_ENABLE          0x00000005
#define DMA_BUSY            0x0000000a
#define DMA_EXTERNAL_ADDR   0x04000000
#define DMA_TX_SCHEDULING   0x00000040
#define DMA_RX_INFO_OMIT    0x08000000
#define DMA_LEGACY_MODE     0x18208000 /* TX/RX info omission and descriptor-chain flags */
#define DMA_ROM_CONFIG      0x5020b870

#define TX_RING_START       0xd4300
#define RX_RING_START       0xd4500
#define RING_STRIDE         0x10
#define RING_BASE           0x00
#define RING_COUNT          0x04
#define RING_CPU            0x08
#define RING_DEVICE         0x0c
#define TX_MCU_RING         (TX_RING_START + 17 * RING_STRIDE)
#define MT7925_TX_MCU_RING  (TX_RING_START + 15 * RING_STRIDE)
#define TX_ACTION_RING      TX_RING_START
#define TX_ACTION_PREFETCH  0xd4600
#define TX_ACTION_COUNT     64U
#define STATION_ACTION_COUNT 128U
#define TX_FWDL_RING        (TX_RING_START + 16 * RING_STRIDE)
#define RX_MCU_RING         RX_RING_START
#define RX_RUNTIME_RING     (RX_RING_START + 4 * RING_STRIDE)
#define RX_DATA_RING        (RX_RING_START + 2 * RING_STRIDE)
#define TX_MCU_PREFETCH     0xd4644
#define MT7925_TX_MCU_PREFETCH 0xd463c
#define TX_FWDL_PREFETCH    0xd4640
#define RX_MCU_PREFETCH     0xd4680
#define RX_RUNTIME_PREFETCH 0xd4690
#define RX_DATA_PREFETCH    0xd4688
#define TX_DESCRIPTOR_COUNT 256
#define FWDL_DESCRIPTOR_COUNT 128
#define RX_DESCRIPTOR_COUNT 8
#define DATA_DESCRIPTOR_COUNT 64
#define FWDL_RING_OFFSET    PAGE_SIZE
#define RX_RING_OFFSET      (2 * PAGE_SIZE)
#define RX_RUNTIME_OFFSET   (3 * PAGE_SIZE)
#define TX_PAYLOAD_OFFSET   (4 * PAGE_SIZE)
#define RX_PAYLOAD_OFFSET   (5 * PAGE_SIZE)
#define RX_RUNTIME_PAYLOAD  (RX_PAYLOAD_OFFSET + RX_DESCRIPTOR_COUNT * RX_BUFFER_BYTES)
#define COMMON_BUFFER_BYTES (RX_RUNTIME_PAYLOAD + RX_DESCRIPTOR_COUNT * RX_BUFFER_BYTES)
#define DATA_RING_OFFSET    COMMON_BUFFER_BYTES
#define DATA_PAYLOAD_OFFSET (DATA_RING_OFFSET + PAGE_SIZE)
#define CAPTURE_BUFFER_BYTES (DATA_PAYLOAD_OFFSET + DATA_DESCRIPTOR_COUNT * RX_BUFFER_BYTES)
#define ACTION_RING_OFFSET CAPTURE_BUFFER_BYTES
#define ACTION_TXWI_OFFSET (ACTION_RING_OFFSET + PAGE_SIZE)
#define ACTION_FRAME_OFFSET (ACTION_TXWI_OFFSET + MT7922_TXWI_BYTES)
#define UNICAST_TX_OFFSET   (ACTION_TXWI_OFFSET + PAGE_SIZE)
#define UNICAST_TX_STRIDE   2048U
#define STATION_SHARED_TX_STRIDE 8192U
#define AWDL_LINK_MAX_MS    120000U
#define AWDL_RECEIVE_LINK_MAX_MS (18U * 60U * 1000U)
#define AWDL_SHARED_MIF_INTERVAL_MS 500U
#define UNICAST_TOKEN_BASE  TX_ACTION_COUNT
#define UNICAST_PACKET_BASE 128U
#define ACTION_BUFFER_BYTES (UNICAST_TX_OFFSET + MT7922_AWDL_DATA_PROBES * UNICAST_TX_STRIDE)
#define ACTION_TOKEN 1U
#define ACTION_PACKET_ID 3U
/* mt76's MT7921 PCIe backend uses an 8192-token range for MT7922. */
#define STATION_TOKEN_COUNT 8192U
#define STATION_SHARED_TOKEN UNICAST_TOKEN_BASE
#define STATION_SHARED_PACKET_ID 255U
#define STATION_SHARED_ACK_SLOTS 31U
#define STATION_SHARED_UPLOAD_FAST_IN_FLIGHT 64U
#define STATION_SHARED_TX_SLOTS STATION_SHARED_UPLOAD_FAST_IN_FLIGHT
#define STATION_SHARED_UPLOAD_IN_FLIGHT 8U
#define STATION_SHARED_UPLOAD_AGGREGATE_IN_FLIGHT 16U
#define STATION_SHARED_UPLOAD_EARLY_IN_FLIGHT 2U
#define STATION_SHARED_UPLOAD_BA_WINDOW 64U
#define STATION_SHARED_UPLOAD_BA_IDLE_TU 4096U
#define STATION_SHARED_UPLOAD_ADDBA_WAIT_MS 1200U
#define STATION_SHARED_UPLOAD_ADDBA_ATTEMPTS 2U
#define STATION_SHARED_TX_TOKEN_BASE (STATION_SHARED_TOKEN + 1U)
#define STATION_SHARED_TX_PID_BASE (STATION_SHARED_PACKET_ID - STATION_SHARED_TX_SLOTS)
#define STATION_DATA_TX_TOKEN_BASE (STATION_SHARED_TX_TOKEN_BASE + STATION_SHARED_TX_SLOTS)
#define STATION_DATA_TX_TOKEN_END (STATION_DATA_TX_TOKEN_BASE + MT7922_STATION_DATA_TX_SLOTS)
#define STATION_DATA_TX_PID_BASE (STATION_SHARED_TX_PID_BASE - MT7922_STATION_DATA_TX_SLOTS)
#define STATION_DATA_TX_OFFSET (UNICAST_TX_OFFSET + (STATION_SHARED_TX_SLOTS + 1U) * STATION_SHARED_TX_STRIDE)
#define STATION_TX_BUFFER_BYTES (STATION_DATA_TX_OFFSET + MT7922_STATION_DATA_TX_SLOTS * UNICAST_TX_STRIDE)
#define STATION_SHARED_BA_WCID 2U /* AP and multicast keep their own WCIDs. */
#define STATION_AWDL_UPLOAD_PORT 1U
#define STATION_SHARED_ADVERT_TICKS (10ULL * 10000000ULL)

/* Connac2 legacy WM command, mt76_connac_mcu.h and mt76_connac_mcu.c. */
#define TXD0_COMMAND_FORMAT  (2u << 23)
#define TXD0_WM_QUEUE        (0x20u << 25)
#define TXD1_LONG_COMMAND    0x80010000
#define MCU_WM_PORT_QUEUE   0x8000
#define MCU_PACKET_ID       0xa0
#define MCU_SET_QUERY_NA    3
#define MCU_SET_QUERY_SET   1
#define MCU_TARGET_ADDRESS_LEN 0x01
#define MCU_FW_START        0x02
#define MCU_PATCH_START     0x05
#define MCU_PATCH_FINISH    0x07
#define MCU_PATCH_SEMAPHORE 0x10
#define MCU_NIC_CAPABILITIES 0x8a
#define MCU_EXTENDED        0xedU
#define MCU_EXT_EFUSE       0x21U
#define MCU_EXT_EFUSE_ACCESS 0x01U
#define MCU_EXT_MAC_INIT    0x46U
#define MCU_EXT_CHANNEL_SWITCH 0x08U
#define MCU_EXT_SET_RX_PATH 0x4eU
#define MCU_CE_CHANNEL_DOMAIN 0x0fU
#define MCU_CE_SET_CLC      0x5cU
#define MCU_CE_RATE_TX_POWER 0x5dU
#define MCU_EEPROM_BLOCK_BYTES 16U
#define MT_EE_HW_TYPE       0x55bU
#define MCU_CE_REG_READ 0xc0U
#define MCU_REG_PSE_BASE 0x820c8000U
#define MCU_SET_QUERY_READ 0U
#define MCU_UNIFIED_FLAG    0x10000U
#define MCU_DEV_INFO        MT7922_STATION_DEVICE_COMMAND
#define MCU_BSS_INFO        MT7922_STATION_BSS_COMMAND
#define MCU_SNIFFER         0x24U
#define MCU_STA_REC         0x03U
#define MT7922_CONN_STATE_DISCONNECT 0U
#define MT7922_CONN_STATE_CONNECT 1U
#define MT7922_CONN_STATE_PORT_SECURE 2U
#define MCU_UNI_SET_ACK     7U
#define MCU_UNI_SET_NO_ACK  6U
#define MCU_UNI_RESULT      1U
#define MCU_UNI_RESULT_BYTES 8U
#define MCU_UNI_CHIP_CONFIG 0x0eU
#define MCU_UNI_CHIP_CONFIG_NIC_CAPA 0x03U
#define MCU_UNI_EFUSE_CTRL 0x2dU
#define MCU_UNI_EFUSE_BUFFER_MODE 0x02U
#define MCU_UNI_SET_DOMAIN_INFO 0x15U
#define MCU_UNI_SET_POWER_LIMIT 0x2cU
#define MCU_UNI_SCAN_REQ 0x16U
#define MCU_NIC_CAP_MAC_ADDR 0x07U
#define MCU_NIC_CAP_PHY     0x08
#define MCU_NIC_CAP_6GHZ    0x18
#define MCU_RX_HEADER_BYTES 36
#define MCU_CONNAC3_RX_HEADER_BYTES 44
#define MCU_PATCH_BYTES     137472
#define MCU_DOWNLOAD_MODE   0x80000009
#define MCU_SEM_RELEASE     0
#define MCU_SEM_GET         1
#define MCU_PATCH_PRESENT   1
#define MCU_SEM_ACQUIRED    2
#define MCU_SEM_RELEASED    3
#define MCU_RX_EVENT_OFFSET 28
#define MCU_RX_SEQ_OFFSET   29
#define MCU_ROM_RESULT_EVENT 1
#define MCU_ROM_SEMAPHORE_EVENT 4
#define MCU_NO_RESPONSE     MAXULONG

typedef struct _MCU_COMMAND {
    ULONG Hardware[8];
    USHORT Length;
    USHORT PortQueue;
    UCHAR Command;
    UCHAR PacketId;
    UCHAR SetQuery;
    UCHAR Sequence;
    UCHAR Reserved0;
    UCHAR Extension;
    UCHAR Destination;
    UCHAR ExtensionAck;
    ULONG Reserved[5];
    ULONG Arguments[3];
} MCU_COMMAND;

typedef struct _MCU_UNIFIED_COMMAND {
    ULONG Hardware[8];
    USHORT Length;
    USHORT Command;
    UCHAR Reserved0;
    UCHAR PacketId;
    UCHAR Fragment;
    UCHAR Sequence;
    USHORT Checksum;
    UCHAR Destination;
    UCHAR Option;
    ULONG Reserved1;
    UCHAR Payload[1536];
} MCU_UNIFIED_COMMAND;

C_ASSERT(sizeof(((MCU_UNIFIED_COMMAND *)0)->Payload) >= MT7925_STATION_POWER_BYTES);

typedef struct _TX_RING {
    ULONG Registers;
    ULONG Offset;
    ULONG PayloadOffset;
    ULONG Count;
    ULONG Producer;
} TX_RING;

typedef struct _STATION_SHARED_ACK_TX {
    ULONGLONG SubmittedAt;
    MT7922_AWDL_ACK_TRACE_ENTRY *Trace;
    USHORT Sequence;
    USHORT Wcid;
    MT7922_TX_REPORT Report;
    UCHAR ReleaseStatus;
    bool Released;
    bool ReportSeen;
} STATION_SHARED_ACK_TX;

typedef struct {
    ULONGLONG SubmittedAt;
    ULONGLONG DmaObservedAt;
    ULONGLONG TraceDeadline;
    ULONG Descriptor;
#ifdef MT7922_STATION_PACKET_TRACE
    bool PacketTraced;
    MT7922_STATION_TCP_HEADER Packet;
#endif
} STATION_DATA_TX;

typedef struct _MCU_RINGS {
    PUCHAR Registers;
    ULONG ChipId;
    PUCHAR Buffer;
    ULONG DmaAddress;
    TX_RING ControlTx;
    TX_RING DownloadTx;
    TX_RING ActionTx;
    MT7922_RX_RING RomEvents;
    MT7922_RX_RING RuntimeEvents;
    MT7922_RX_RING DataRx;
    ULONG SavedPciePower;
    UCHAR Sequence;
    UCHAR AwdlTxChannel;
    USHORT AwdlTxRateCode;
    BOOLEAN AwdlWide80;
    const UCHAR *AwdlTarget;
    MT7922_AWDL_LINK *AwdlLink;
    PEX_TIMER AwdlPollTimer;
    MT7922_AWDL_WATCH *AwdlWatch;
    void *ScanContext;
    MT7922_SCAN_RECORD_HANDLER ScanEvent;
    MT7922_SCAN_RECORD_HANDLER ScanFrame;
    BOOLEAN RestorePciePower;
    BOOLEAN Programmed;
    BOOLEAN TransmitPending;
    bool StationStopping;
    bool StationTxPending;
    STATION_DATA_TX StationDataTx[MT7922_STATION_DATA_TX_SLOTS];
    ULONG StationDataInFlight;
    ULONG StationDataDmaCursor;
    ULONGLONG StationDataFastPollUntil;
    bool SharedAdvertReleasePending;
    bool SharedAdvertDmaPending;
    bool SharedTxWaitForEcho;
    uint32_t SharedAdvertRequestId;
    USHORT SharedAdvertSequence;
    UCHAR SharedAdvertQueue;
    MT7922_SHARED_ADVERT_DEBUG *SharedAdvertDebug;
    USHORT SharedAdvertWcid;
    STATION_SHARED_ACK_TX SharedAckTx[STATION_SHARED_TX_SLOTS];
    ULONG SharedAckInFlight;
    MT7922_AWDL_ACK_TRACE_ENTRY *SharedAckDmaTrace[STATION_ACTION_COUNT];
    ULONG SharedAckDmaCursor;
    ULONG SharedAckDmaPending;
    MT7922_AWDL_SYN_ACK_QUEUE_TIMING *SharedSynAckQueueTiming;
    ULONGLONG SharedSynAckQueueSubmittedAt;
    MT7922_STATION_DATA_DEBUG *StationDataDebug;
    MT7922_STATION_IO_DEBUG *StationIoDebug;
    const uint64_t *StationIoTraceDeadline;
    USHORT StationTxToken;
    USHORT StationSequence;
    UCHAR StationTxStatus;
} MCU_RINGS;

C_ASSERT(STATION_SHARED_UPLOAD_AGGREGATE_IN_FLIGHT <= STATION_SHARED_TX_SLOTS);
C_ASSERT(STATION_SHARED_ACK_SLOTS <= STATION_SHARED_TX_SLOTS);
C_ASSERT(STATION_DATA_TX_TOKEN_END <= STATION_TOKEN_COUNT);
C_ASSERT(MT7922_STATION_DATA_TX_SLOTS + STATION_SHARED_TX_SLOTS + 1U < STATION_ACTION_COUNT);
C_ASSERT(MT7922_STATION_RAW_MAX + MT7922_TXWI_BYTES <= UNICAST_TX_STRIDE);
C_ASSERT(MT7922_AWDL_MAX_MTU + 40U + MT7922_TXWI_BYTES <= UNICAST_TX_STRIDE);
C_ASSERT(AWDL_TX_AMSDU3_FRAME_BYTES + MT7922_TXWI_BYTES <= STATION_SHARED_TX_STRIDE);
C_ASSERT(sizeof(DMA_DESCRIPTOR) == 16);
C_ASSERT(TX_DESCRIPTOR_COUNT * sizeof(DMA_DESCRIPTOR) == PAGE_SIZE);
C_ASSERT(FWDL_DESCRIPTOR_COUNT * sizeof(DMA_DESCRIPTOR) <= PAGE_SIZE);
C_ASSERT(FIELD_OFFSET(MCU_COMMAND, Arguments) == MT7922_SCAN_TX_HEADER_BYTES);
C_ASSERT(sizeof(MCU_COMMAND) == 76);
C_ASSERT(FIELD_OFFSET(MCU_UNIFIED_COMMAND, Payload) == 48);
C_ASSERT(DATA_DESCRIPTOR_COUNT * sizeof(DMA_DESCRIPTOR) <= PAGE_SIZE);
C_ASSERT(STATION_ACTION_COUNT * sizeof(DMA_DESCRIPTOR) <= PAGE_SIZE);
C_ASSERT(MT7922_TXWI_BYTES + MT7922_PROBE_PSF_CAPACITY <= UNICAST_TX_STRIDE);
C_ASSERT(MT7922_TXWI_BYTES + MT7922_AWDL_SERVICE_MIF_CAPACITY <= PAGE_SIZE);
C_ASSERT(MCU_PATCH_BYTES + 341904 + 290768 + 15312 + 8144 + 96080
    == MT7922_FACTORY_IMAGE_BYTES);
C_ASSERT(MCU_PATCH_BYTES + 341904 + 292816 + 15312 + 8144 + 97232
    == MT7922_REFERENCE_IMAGE_BYTES);

#define MCU_MAX_PATCH_REGIONS 2U
#define MCU_MAX_RAM_REGIONS   5U
typedef struct _MCU_FIRMWARE {
    ULONG ChipId;
    ULONG Bytes;
    UCHAR Sha256[32];
    ULONG PatchRegionCount;
    ULONG PatchTargets[MCU_MAX_PATCH_REGIONS];
    ULONG PatchRegionBytes[MCU_MAX_PATCH_REGIONS];
    ULONG RamRegionCount;
    ULONG RamTargets[MCU_MAX_RAM_REGIONS];
    ULONG RamRegionBytes[MCU_MAX_RAM_REGIONS];
    ULONG EntryAddress;
} MCU_FIRMWARE;

/* Packed Connac2 download payloads. The MT7925 image comes from the
   user's backed-up 2025-12 vendor driver; it is kept separate from 7922. */
static const MCU_FIRMWARE acceptedFirmware[] = {
    {
        0x7922,
        MT7922_FACTORY_IMAGE_BYTES,
        {
          0x7b, 0xc2, 0x47, 0x09, 0x56, 0xee, 0x9e, 0x26,
          0xfa, 0x16, 0x94, 0xcf, 0x9c, 0xb0, 0x9e, 0xdd,
          0xff, 0xc3, 0x7d, 0xf1, 0x21, 0x6a, 0x8a, 0x95,
          0xa9, 0xff, 0xec, 0x13, 0xaf, 0x38, 0xdd, 0x2e
        },
        1,
        {0x00900000, 0},
        {MCU_PATCH_BYTES, 0},
        5,
        {0x0091b000, 0x02015c00, 0x00404400, 0x0210a000, 0xe0200000},
        {341904, 290768, 15312, 8144, 96080},
        0x0091b000
    },
    {
        0x7922,
        MT7922_REFERENCE_IMAGE_BYTES,
        {
          0x42, 0xfa, 0xfd, 0x30, 0x97, 0x75, 0x50, 0x4f,
          0xb7, 0xf3, 0x1b, 0xed, 0x63, 0x47, 0xa4, 0x30,
          0xf5, 0x67, 0x2f, 0x8f, 0x88, 0x93, 0x07, 0x8a,
          0xab, 0x06, 0x4c, 0xea, 0x52, 0xc6, 0x82, 0xc0
        },
        1,
        {0x00900000, 0},
        {MCU_PATCH_BYTES, 0},
        5,
        {0x0091b000, 0x02015c00, 0x00404400, 0x0210a000, 0xe0200000},
        {341904, 292816, 15312, 8144, 97232},
        0x0091b000
    },
    {
        0x7925,
        1276032,
        {
          0x5a, 0xe7, 0x64, 0xd3, 0x61, 0x59, 0x28, 0x15,
          0x23, 0x7e, 0x6b, 0x68, 0x83, 0x87, 0xb8, 0xf4,
          0xff, 0xd6, 0x14, 0x44, 0x39, 0x38, 0xf1, 0x42,
          0xab, 0x44, 0xce, 0x54, 0x16, 0xc1, 0x16, 0x1b
        },
        2,
        {0x00900000, 0xe0002800},
        {39872, 174016},
        4,
        {0x0090d000, 0x02212800, 0x00404000, 0xe002d000, 0},
        {77200, 382928, 32720, 569296, 0},
        0x0090d000
    }
};

C_ASSERT(39872 + 174016 + 77200 + 382928 + 32720 + 569296 == 1276032);

static ULONG ReadRegister(PUCHAR Window, ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Window + Offset));
}

static void WriteRegister(PUCHAR Window, ULONG Offset, ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Window + Offset), Value);
}

static void ProgramMt7925BasicRates(PUCHAR Window)
{
    /* MT7925 uses Connac3 fixed-rate table entries. These four entries cover
       the legacy OFDM rates used by LuoboDrop's AWDL management and data path.
       The BAR mapping is 0x820d0000 -> 0x30000; WTBL ITCR is at +0x43b0. */
    static const struct {
        ULONG Index;
        ULONG Rate;
    } rates[] = {
        {MT7925_BASIC_RATES_TABLE + 4U, 0x004bU}, /* OFDM 6 Mbps */
        {MT7925_BASIC_RATES_TABLE + 6U, 0x004aU}, /* OFDM 12 Mbps */
        {MT7925_BASIC_RATES_TABLE + 8U, 0x0049U}, /* OFDM 24 Mbps */
        {MT7925_BASIC_RATES_TABLE + 11U, 0x004cU} /* OFDM 54 Mbps */
    };

    for (ULONG i = 0; i < RTL_NUMBER_OF(rates); ++i) {
        WriteRegister(Window, MT7925_WTBL_ITDR0, rates[i].Rate);
        WriteRegister(Window, MT7925_WTBL_ITDR1, MT7925_WTBL_SPE_IDX_SEL);
        WriteRegister(Window, MT7925_WTBL_ITCR,
            MT7925_WTBL_ITCR_WR | MT7925_WTBL_ITCR_EXEC | rates[i].Index);
    }
}

static ULONG McuTxRingRegister(ULONG ChipId)
{
    return ChipId == 0x7925 ? MT7925_TX_MCU_RING : TX_MCU_RING;
}

static ULONG McuTxPrefetchRegister(ULONG ChipId)
{
    return ChipId == 0x7925 ? MT7925_TX_MCU_PREFETCH : TX_MCU_PREFETCH;
}

static void WaitMilliseconds(ULONG Milliseconds)
{
    LARGE_INTEGER interval;
    interval.QuadPart = -(LONGLONG)Milliseconds * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

static NTSTATUS WaitRegister(PUCHAR Window, ULONG Offset, ULONG Mask,
    ULONG Expected, ULONG Milliseconds, ULONG *Observed)
{
    ULONGLONG start = KeQueryInterruptTime();
    do {
        *Observed = ReadRegister(Window, Offset);
        if (*Observed == MAXULONG) {
            return STATUS_DEVICE_NOT_READY;
        }
        if ((*Observed & Mask) == Expected) {
            return STATUS_SUCCESS;
        }
        WaitMilliseconds(1);
    } while (KeQueryInterruptTime() - start < (ULONGLONG)Milliseconds * 10000);
    return STATUS_IO_TIMEOUT;
}

static ULONG RegisterBankWindow(ULONG ChipId, ULONG Bank)
{
    if (ChipId == 0x7925) {
        return Bank == CONNECTION_BANK ? MT7925_CONN_ON_WINDOW : MT7925_HIF_WINDOW_L1;
    }
    return HIF_WINDOW;
}

static ULONG RegisterRemapAddress(ULONG ChipId)
{
    return ChipId == 0x7925 ? MT7925_HIF_REMAP_L1 : HIF_REMAP;
}

static ULONG RegisterRemapMask(ULONG ChipId)
{
    return ChipId == 0x7925 ? MT7925_HIF_REMAP_MASK : HIF_BANK_MASK;
}

static NTSTATUS SelectBank(PUCHAR Window, ULONG ChipId, ULONG Bank, ULONG *Saved)
{
    const ULONG remap = RegisterRemapAddress(ChipId);
    const ULONG mask = RegisterRemapMask(ChipId);
    *Saved = ReadRegister(Window, remap);
    if (*Saved == MAXULONG) {
        return STATUS_DEVICE_NOT_READY;
    }
    /* MT7925 maps CONN_ON at a fixed BAR0 address; other banks use L1 remap. */
    if (ChipId == 0x7925 && Bank == CONNECTION_BANK) {
        return STATUS_SUCCESS;
    }
    const ULONG selected = ChipId == 0x7925
        ? ((*Saved & ~mask) | (Bank << 16))
        : ((*Saved & ~mask) | Bank);
    WriteRegister(Window, remap, selected);
    if ((ReadRegister(Window, remap) & mask) != (selected & mask)) {
        WriteRegister(Window, remap, *Saved);
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS RestoreBank(PUCHAR Window, ULONG ChipId, ULONG Saved, NTSTATUS Status)
{
    const ULONG remap = RegisterRemapAddress(ChipId);
    const ULONG mask = RegisterRemapMask(ChipId);
    WriteRegister(Window, remap, Saved);
    ULONG now = ReadRegister(Window, remap);
    if (now == MAXULONG || (now & mask) != (Saved & mask)) {
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    return Status;
}

static NTSTATUS ResetWireless(PUCHAR Window, ULONG ChipId, ULONG *ResetResult)
{
    if (ChipId == 0x7925) {
        /* MT7925 maps MT_WFSYS_SW_RST_B at BAR0 0x0f0140. */
        const ULONG resetRegister = 0x0f0140;
        ULONG initial = ReadRegister(Window, resetRegister);
        if (initial == MAXULONG) {
            return STATUS_DEVICE_NOT_READY;
        }
        WriteRegister(Window, resetRegister, initial & ~WFSYS_RESET_RELEASE);
        WaitMilliseconds(50);
        WriteRegister(Window, resetRegister, initial | WFSYS_RESET_RELEASE);
        return WaitRegister(Window, resetRegister, WFSYS_INIT_DONE,
            WFSYS_INIT_DONE, 500, ResetResult);
    }

    ULONG saved;
    NTSTATUS status = SelectBank(Window, ChipId, WFSYS_BANK, &saved);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    const ULONG bankWindow = RegisterBankWindow(ChipId, WFSYS_BANK);
    ULONG initial = ReadRegister(Window, bankWindow + WFSYS_RESET);
    if (initial == MAXULONG) {
        return RestoreBank(Window, ChipId, saved, STATUS_DEVICE_NOT_READY);
    }
    WriteRegister(Window, bankWindow + WFSYS_RESET, initial & ~WFSYS_RESET_RELEASE);
    WaitMilliseconds(50);
    WriteRegister(Window, bankWindow + WFSYS_RESET, initial | WFSYS_RESET_RELEASE);
    status = WaitRegister(Window, bankWindow + WFSYS_RESET, WFSYS_INIT_DONE,
        WFSYS_INIT_DONE, 500, ResetResult);
    return RestoreBank(Window, ChipId, saved, status);
}

static NTSTATUS ReadBootRomState(PUCHAR Window, ULONG ChipId, ULONG *FirmwareState)
{
    ULONG saved;
    NTSTATUS status = SelectBank(Window, ChipId, CONNECTION_BANK, &saved);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    const ULONG bankWindow = RegisterBankWindow(ChipId, CONNECTION_BANK);
    if (ReadRegister(Window, bankWindow + CONNECTION_OWN) == MAXULONG) {
        return RestoreBank(Window, ChipId, saved, STATUS_DEVICE_NOT_READY);
    }
    WriteRegister(Window, bankWindow + CONNECTION_OWN, HOST_OWN_REQUEST);
    ULONG owner;
    status = WaitRegister(Window, bankWindow + CONNECTION_OWN, FIRMWARE_OWNS, 0, 150, &owner);
    if (NT_SUCCESS(status)) {
        *FirmwareState = ReadRegister(Window, bankWindow + CONNECTION_FIRMWARE);
        if (*FirmwareState == MAXULONG || (*FirmwareState & 0x3) == 0x3) {
            status = STATUS_DEVICE_NOT_READY;
        }
    }
    return RestoreBank(Window, ChipId, saved, status);
}

static NTSTATUS ResetDmaLogic(PUCHAR Window, ULONG *Observed)
{
    const ULONG reset = ReadRegister(Window, DMA_RESET);
    *Observed = reset;
    if (reset == MAXULONG) {
        return STATUS_DEVICE_NOT_READY;
    }

    WriteRegister(Window, DMA_RESET, reset & ~DMA_RESET_LOGIC);
    NTSTATUS status = WaitRegister(Window, DMA_RESET, DMA_RESET_LOGIC, 0, 100, Observed);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WaitMilliseconds(1);
    WriteRegister(Window, DMA_RESET, reset | DMA_RESET_LOGIC);
    return WaitRegister(Window, DMA_RESET, DMA_RESET_LOGIC, DMA_RESET_LOGIC,
        100, Observed);
}

NTSTATUS Mt7922StopMcuDma(PUCHAR Window)
{
    ULONG previous = ReadRegister(Window, DMA_GLOBAL);
    if (previous == MAXULONG) {
        return STATUS_DEVICE_NOT_READY;
    }
    WriteRegister(Window, DMA_GLOBAL, previous & ~DMA_ENABLE);
    ULONG current;
    NTSTATUS status = WaitRegister(Window, DMA_GLOBAL, DMA_ENABLE | DMA_BUSY, 0, 100, &current);
    if (NT_SUCCESS(status)) {
        return status;
    }

    /* Force the DMA logic and scheduler to stop before the common buffer can be freed. */
    if (!NT_SUCCESS(ResetDmaLogic(Window, &current))) {
        return status;
    }
    return WaitRegister(Window, DMA_GLOBAL, DMA_ENABLE | DMA_BUSY, 0, 100, &current);
}

static void AssignRxRing(MCU_RINGS *Rings, MT7922_RX_RING *Ring, ULONG Registers,
    ULONG DescriptorOffset, ULONG BufferOffset, ULONG Count)
{
    Ring->Registers = Rings->Registers + Registers;
    Ring->Descriptors = (DMA_DESCRIPTOR *)(Rings->Buffer + DescriptorOffset);
    Ring->Buffers = Rings->Buffer + BufferOffset;
    Ring->DescriptorAddress = Rings->DmaAddress + DescriptorOffset;
    Ring->BufferAddress = Rings->DmaAddress + BufferOffset;
    Ring->Count = Count;
    Ring->ChipId = (USHORT)Rings->ChipId;
}

static NTSTATUS RequireIdleDma(PUCHAR Window, MT7922_MCU_PROBE_INFO *Result)
{
    ULONG state = ReadRegister(Window, DMA_GLOBAL);
    Result->DmaConfig = state;
    if (state == MAXULONG || (state & (DMA_ENABLE | DMA_BUSY)) != 0) {
        Result->BlockedRegister = DMA_GLOBAL;
        Result->BlockedValue = state;
        return state == MAXULONG ? STATUS_DEVICE_NOT_READY : STATUS_DEVICE_BUSY;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS CheckDmaRegister(PUCHAR Window, ULONG Register, ULONG Expected,
    MT7922_MCU_PROBE_INFO *Result)
{
    ULONG value = ReadRegister(Window, Register);
    if (value != Expected) {
        Result->BlockedRegister = Register;
        Result->BlockedValue = value;
        return value == MAXULONG ? STATUS_DEVICE_NOT_READY : STATUS_DEVICE_HARDWARE_ERROR;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS ClearDormantRings(PUCHAR Window, ULONG FirstRing, ULONG Count,
    MT7922_MCU_PROBE_INFO *Result)
{
    for (ULONG index = 0; index < Count; ++index) {
        ULONG ring = FirstRing + index * RING_STRIDE;
        ULONG previous = ReadRegister(Window, ring + RING_COUNT);
        if (previous == MAXULONG) {
            Result->BlockedRegister = ring + RING_COUNT;
            Result->BlockedValue = previous;
            return STATUS_DEVICE_NOT_READY;
        }
        if (previous == 0) {
            continue;
        }

        /* DMA is stopped and the descriptor engines have been reset.
           Retire the count before changing stale pointers from the previous driver. */
        WriteRegister(Window, ring + RING_COUNT, 0);
        NTSTATUS status = CheckDmaRegister(Window, ring + RING_COUNT, 0, Result);
        if (!NT_SUCCESS(status)) {
            return status;
        }
        WriteRegister(Window, ring + RING_CPU, 0);
        WriteRegister(Window, ring + RING_BASE, 0);
        status = CheckDmaRegister(Window, ring + RING_BASE, 0, Result);
        if (!NT_SUCCESS(status)) {
            return status;
        }
        status = CheckDmaRegister(Window, ring + RING_CPU, 0, Result);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS PrepareMcuTransport(MCU_RINGS *Rings, MT7922_MCU_PROBE_INFO *Result)
{
    PUCHAR window = Rings->Registers;
    ULONG saved, owner;
    const BOOLEAN mt7925 = Rings->ChipId == 0x7925;
    const ULONG bank = mt7925 ? CONNECTION_BANK : TOP_HOST_BANK;
    const ULONG ownerRegister = mt7925 ? CONNECTION_OWN : TOP_HOST_LPCR;
    const ULONG ownerMask = mt7925 ? FIRMWARE_OWNS : TOP_HOST_FW_OWN;
    NTSTATUS status = SelectBank(window, Rings->ChipId, bank, &saved);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    const ULONG bankWindow = RegisterBankWindow(Rings->ChipId, bank);
    WriteRegister(window, bankWindow + ownerRegister, HOST_OWN_REQUEST);
    status = WaitRegister(window, bankWindow + ownerRegister,
        ownerMask, 0, 500, &owner);
    if (!NT_SUCCESS(status)) {
        Result->BlockedRegister = mt7925 ? bankWindow + ownerRegister : 0x18060010;
        Result->BlockedValue = owner;
    }
    status = RestoreBank(window, Rings->ChipId, saved, status);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    ULONG power = ReadRegister(window, PCIE_MAC_PM);
    if (power == MAXULONG) {
        Result->BlockedRegister = PCIE_MAC_PM;
        Result->BlockedValue = power;
        return STATUS_DEVICE_NOT_READY;
    }
    if (!(power & PCIE_MAC_PM_L0S_DIS)) {
        Rings->SavedPciePower = power;
        Rings->RestorePciePower = TRUE;
        WriteRegister(window, PCIE_MAC_PM, power | PCIE_MAC_PM_L0S_DIS);
    }
    Result->PcieMacPower = ReadRegister(window, PCIE_MAC_PM);
    if (Result->PcieMacPower == MAXULONG || !(Result->PcieMacPower & PCIE_MAC_PM_L0S_DIS)) {
        Result->BlockedRegister = PCIE_MAC_PM;
        Result->BlockedValue = Result->PcieMacPower;
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    return STATUS_SUCCESS;
}

static void ProgramTxRing(MCU_RINGS *Rings, const TX_RING *Ring)
{
    DMA_DESCRIPTOR *descriptors = (DMA_DESCRIPTOR *)(Rings->Buffer + Ring->Offset);
    for (ULONG i = 0; i < Ring->Count; ++i) {
        descriptors[i].Control = DESCRIPTOR_DONE;
    }
    KeMemoryBarrier();
    WriteRegister(Rings->Registers, Ring->Registers + RING_CPU, 0);
    WriteRegister(Rings->Registers, Ring->Registers + RING_BASE, Rings->DmaAddress + Ring->Offset);
    WriteRegister(Rings->Registers, Ring->Registers + RING_COUNT, Ring->Count);
}

static NTSTATUS PrepareDma(MCU_RINGS *Rings, MT7922_MCU_PROBE_INFO *Result)
{
    PUCHAR window = Rings->Registers;
    NTSTATUS status = RequireIdleDma(window, Result);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WriteRegister(window, DMA_HOST_INT_ENABLE, 0);
    WriteRegister(window, PCIE_INT_ENABLE, 0);
    WriteRegister(window, DMA_GLOBAL, Result->DmaConfig & ~DMA_LEGACY_MODE);
    ULONG observed;
    status = ResetDmaLogic(window, &observed);
    if (!NT_SUCCESS(status)) {
        Result->BlockedRegister = DMA_RESET;
        Result->BlockedValue = observed;
        return status;
    }
    status = RequireIdleDma(window, Result);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = ClearDormantRings(window, TX_RING_START, 18, Result);
    if (NT_SUCCESS(status)) {
        status = ClearDormantRings(window, RX_RING_START, 6, Result);
    }
    if (!NT_SUCCESS(status)) {
        return status;
    }

    ULONG extension = ReadRegister(window, DMA_GLOBAL_EXT);
    if (extension == MAXULONG) {
        Result->BlockedRegister = DMA_GLOBAL_EXT;
        Result->BlockedValue = extension;
        return STATUS_DEVICE_NOT_READY;
    }
    WriteRegister(window, DMA_GLOBAL_EXT, extension & ~DMA_TX_SCHEDULING);
    Result->DmaGlobalExtension = ReadRegister(window, DMA_GLOBAL_EXT);
    if (Result->DmaGlobalExtension == MAXULONG ||
        (Result->DmaGlobalExtension & DMA_TX_SCHEDULING)) {
        Result->BlockedRegister = DMA_GLOBAL_EXT;
        Result->BlockedValue = Result->DmaGlobalExtension;
        return STATUS_DEVICE_HARDWARE_ERROR;
    }

    ULONG scheduler = ReadRegister(window, DMASHDL_CONTROL);
    if (scheduler == MAXULONG) {
        Result->BlockedRegister = DMASHDL_CONTROL;
        Result->BlockedValue = scheduler;
        return STATUS_DEVICE_NOT_READY;
    }
    WriteRegister(window, DMASHDL_CONTROL, scheduler | DMASHDL_BYPASS);
    Result->DmaScheduler = ReadRegister(window, DMASHDL_CONTROL);
    if (Result->DmaScheduler == MAXULONG || !(Result->DmaScheduler & DMASHDL_BYPASS)) {
        Result->BlockedRegister = DMASHDL_CONTROL;
        Result->BlockedValue = Result->DmaScheduler;
        return STATUS_DEVICE_HARDWARE_ERROR;
    }

    WriteRegister(window, DMA_DELAY_INTERRUPT, 0);
    WriteRegister(window, McuTxPrefetchRegister(Rings->ChipId), 0x03800004);
    WriteRegister(window, TX_FWDL_PREFETCH, 0x03400004);
    WriteRegister(window, RX_MCU_PREFETCH, 0x00000004);
    if (Rings->ChipId != 0x7925) {
        WriteRegister(window, RX_RUNTIME_PREFETCH, 0x00c00004);
    }
    WriteRegister(window, DMA_RESET_TX_INDEX, MAXULONG);
    Rings->Programmed = TRUE;
    ProgramTxRing(Rings, &Rings->ControlTx);
    ProgramTxRing(Rings, &Rings->DownloadTx);
    if (Rings->ActionTx.Count != 0) {
        WriteRegister(window, TX_ACTION_PREFETCH, 0x01400004);
        ProgramTxRing(Rings, &Rings->ActionTx);
    }
    Mt7922ProgramRxRing(&Rings->RomEvents);
    if (Rings->ChipId != 0x7925) {
        Mt7922ProgramRxRing(&Rings->RuntimeEvents);
    }
    if (Rings->DataRx.Count != 0) {
        WriteRegister(window, RX_DATA_PREFETCH, 0x00400004);
        Mt7922ProgramRxRing(&Rings->DataRx);
    }

    const struct {
        ULONG Base;
        ULONG Expected[4]; /* descriptor base, count, host index, device index */
        BOOLEAN Active;
    } programmed[] = {
        {McuTxRingRegister(Rings->ChipId), {Rings->DmaAddress, TX_DESCRIPTOR_COUNT, 0, 0}, TRUE},
        {TX_FWDL_RING, {Rings->DmaAddress + FWDL_RING_OFFSET, FWDL_DESCRIPTOR_COUNT, 0, 0}, TRUE},
        {RX_MCU_RING, {Rings->RomEvents.DescriptorAddress, RX_DESCRIPTOR_COUNT,
            Rings->RomEvents.Producer, 0}, TRUE},
        {RX_RUNTIME_RING, {Rings->RuntimeEvents.DescriptorAddress, RX_DESCRIPTOR_COUNT,
            Rings->RuntimeEvents.Producer, 0}, Rings->ChipId != 0x7925},
        {RX_DATA_RING, {Rings->DataRx.DescriptorAddress, Rings->DataRx.Count,
            Rings->DataRx.Producer, 0}, Rings->DataRx.Count != 0},
        {TX_ACTION_RING, {Rings->DmaAddress + ACTION_RING_OFFSET, Rings->ActionTx.Count, 0, 0},
            Rings->ActionTx.Count != 0}
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(programmed); ++i) {
        if (!programmed[i].Active) {
            continue;
        }
        for (ULONG field = 0; field < RTL_NUMBER_OF(programmed[i].Expected); ++field) {
            status = CheckDmaRegister(window, programmed[i].Base + field * sizeof(ULONG),
                programmed[i].Expected[field], Result);
            if (!NT_SUCCESS(status)) {
                return status;
            }
        }
    }
    status = RequireIdleDma(window, Result);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    WriteRegister(window, DMA_GLOBAL,
        (Result->DmaConfig & ~(DMA_EXTERNAL_ADDR | DMA_RX_INFO_OMIT)) | DMA_ROM_CONFIG | DMA_ENABLE);
    ULONG enabled = ReadRegister(window, DMA_GLOBAL);
    if (enabled == MAXULONG || (enabled & DMA_ENABLE) != DMA_ENABLE) {
        Result->BlockedRegister = DMA_GLOBAL;
        Result->BlockedValue = enabled;
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    return PrepareMcuTransport(Rings, Result);
}

static NTSTATUS PublishPackets(MCU_RINGS *Rings, TX_RING *Ring,
    MT7922_MCU_PROBE_INFO *Result, bool WaitForDma)
{
    const DMA_DESCRIPTOR *descriptors = (const DMA_DESCRIPTOR *)(Rings->Buffer + Ring->Offset);
    const ULONG last = Ring->Producer == 0 ? Ring->Count - 1 : Ring->Producer - 1;
    KeMemoryBarrier();
    Rings->TransmitPending = WaitForDma;
    PUCHAR window = Rings->Registers;
    ULONG hostIndex = Ring->Registers + RING_CPU;
    ULONG deviceIndex = Ring->Registers + RING_DEVICE;
    WriteRegister(window, hostIndex, Ring->Producer);
    Result->LastTxRing = Ring->Registers;
    Result->LastTxHostIndex = ReadRegister(window, hostIndex);

    NTSTATUS status;
    ULONG observed;
    if (Result->LastTxHostIndex != Ring->Producer) {
        Result->BlockedRegister = hostIndex;
        Result->BlockedValue = Result->LastTxHostIndex;
        status = STATUS_DEVICE_HARDWARE_ERROR;
    } else if (!WaitForDma) {
        status = STATUS_SUCCESS;
    } else {
        status = WaitRegister(window, deviceIndex, MAXULONG, Ring->Producer, 1000, &observed);
        if (NT_SUCCESS(status)) {
            Rings->TransmitPending = FALSE;
        } else {
            Result->BlockedRegister = deviceIndex;
            Result->BlockedValue = observed;
        }
    }
    KeMemoryBarrier();
    Result->LastTxDescriptorControl = descriptors[last].Control;
    Result->DmaInterruptStatus = ReadRegister(window, DMA_HOST_INT_STATUS);
    return status;
}

static NTSTATUS PostPacket(MCU_RINGS *Rings, TX_RING *Ring, const void *Packet, ULONG Length,
    MT7922_MCU_PROBE_INFO *Result, bool WaitForDma, bool DeferPublish)
{
    if (Length == 0 || Length > PAGE_SIZE) {
        return STATUS_INVALID_BUFFER_SIZE;
    }
    if (Rings->TransmitPending) {
        return STATUS_DEVICE_BUSY;
    }
    DMA_DESCRIPTOR *descriptors = (DMA_DESCRIPTOR *)(Rings->Buffer + Ring->Offset);
    DMA_DESCRIPTOR *descriptor = &descriptors[Ring->Producer];

    RtlCopyMemory(Rings->Buffer + Ring->PayloadOffset, Packet, Length);
    descriptor->FirstBuffer = Rings->DmaAddress + Ring->PayloadOffset;
    descriptor->SecondBuffer = 0;
    descriptor->Info = 0;
    descriptor->Control = (Length << DESCRIPTOR_LEN_SHIFT) | DESCRIPTOR_LAST0;
    Ring->Producer = (Ring->Producer + 1) % Ring->Count;
    return DeferPublish ? STATUS_SUCCESS : PublishPackets(Rings, Ring, Result, WaitForDma);
}

static NTSTATUS SendPacket(MCU_RINGS *Rings, TX_RING *Ring, const void *Packet, ULONG Length,
    MT7922_MCU_PROBE_INFO *Result)
{
    return PostPacket(Rings, Ring, Packet, Length, Result, true, false);
}

static ULONG ReadLittleEndian32(const UCHAR *Bytes)
{
    return (ULONG)Bytes[0] | ((ULONG)Bytes[1] << 8) |
        ((ULONG)Bytes[2] << 16) | ((ULONG)Bytes[3] << 24);
}

static USHORT ReadLittleEndian16(const UCHAR *Bytes)
{
    return (USHORT)((USHORT)Bytes[0] | ((USHORT)Bytes[1] << 8));
}

static ULONG McuRxHeaderBytes(ULONG ChipId)
{
    return ChipId == 0x7925 ? MCU_CONNAC3_RX_HEADER_BYTES : MCU_RX_HEADER_BYTES;
}

static ULONG McuRxEventOffset(ULONG ChipId)
{
    /* Connac RXD stores the event fields in its final eight header bytes. */
    return McuRxHeaderBytes(ChipId) - 8U;
}

static NTSTATUS ParseMt7925NicCapabilities(const UCHAR *Payload, ULONG Length,
    MT7922_MCU_PROBE_INFO *Result)
{
    enum { MT7925_PHY_CAP_BYTES = 13U, MT7925_TLV_HEADER_BYTES = 4U };
    if (Payload == NULL || Length < 4U) {
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }

    const ULONG elements = ReadLittleEndian16(Payload);
    ULONG remaining = Length - 4U;
    const UCHAR *cursor = Payload + 4U;
    if (elements > remaining / MT7925_TLV_HEADER_BYTES) {
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }

    ULONG bands = 0;
    ULONG streams = 0;
    ULONG width = 0;
    ULONG standards = 0;
    BOOLEAN foundMac = FALSE;
    BOOLEAN foundPhy = FALSE;
    UCHAR mac[sizeof(Result->NicMac)] = {0};

    for (ULONG i = 0; i < elements; ++i) {
        if (remaining < MT7925_TLV_HEADER_BYTES) {
            return STATUS_DEVICE_PROTOCOL_ERROR;
        }
        const USHORT tag = ReadLittleEndian16(cursor);
        const USHORT tlvBytes = ReadLittleEndian16(cursor + 2U);
        if (tlvBytes < MT7925_TLV_HEADER_BYTES || tlvBytes > remaining) {
            return STATUS_DEVICE_PROTOCOL_ERROR;
        }
        const UCHAR *data = cursor + MT7925_TLV_HEADER_BYTES;
        const ULONG dataBytes = (ULONG)tlvBytes - MT7925_TLV_HEADER_BYTES;

        if (tag == MCU_NIC_CAP_MAC_ADDR) {
            if (dataBytes < sizeof(mac)) {
                return STATUS_DEVICE_PROTOCOL_ERROR;
            }
            RtlCopyMemory(mac, data, sizeof(mac));
            foundMac = TRUE;
        } else if (tag == MCU_NIC_CAP_PHY) {
            if (dataBytes < MT7925_PHY_CAP_BYTES || data[4] == 0 || data[4] > 8) {
                return STATUS_DEVICE_PROTOCOL_ERROR;
            }
            bands |= (data[10] & 0x01U) ? MT7922_NIC_BAND_24GHZ : 0;
            bands |= (data[10] & 0x02U) ? MT7922_NIC_BAND_5GHZ : 0;
            streams = data[4];
            width = data[3];
            if (data[0]) standards |= MT7922_NIC_PHY_HT;
            if (data[1]) standards |= MT7922_NIC_PHY_VHT;
            if (data[11]) standards |= MT7922_NIC_PHY_HE;
            if (data[12]) standards |= MT7922_NIC_PHY_EHT;
            foundPhy = TRUE;
        } else if (tag == MCU_NIC_CAP_6GHZ) {
            if (dataBytes < 1U) {
                return STATUS_DEVICE_PROTOCOL_ERROR;
            }
            if (data[0]) bands |= MT7922_NIC_BAND_6GHZ;
        }

        cursor += tlvBytes;
        remaining -= tlvBytes;
    }

    UCHAR macBits = 0;
    for (ULONG i = 0; i < sizeof(mac); ++i) {
        macBits |= mac[i];
    }
    if (!foundMac || !foundPhy || macBits == 0 || (mac[0] & 1U) != 0 ||
        streams == 0 || bands == 0) {
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }

    RtlCopyMemory(Result->NicMac, mac, sizeof(mac));
    Result->NicBands = bands;
    Result->NicSpatialStreams = streams;
    Result->NicBandwidthCode = width;
    Result->NicPhyFlags = standards;
    return STATUS_SUCCESS;
}

static NTSTATUS ParseNicCapabilities(const UCHAR *Frame, ULONG Length, MT7922_MCU_PROBE_INFO *Result)
{
    if (Length < MCU_RX_HEADER_BYTES + 4) {
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }
    const UCHAR *payload = Frame + MCU_RX_HEADER_BYTES;
    ULONG elements = (ULONG)payload[0] | ((ULONG)payload[1] << 8);
    ULONG remaining = Length - MCU_RX_HEADER_BYTES - 4;
    payload += 4;
    if (elements > remaining / 8) {
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }

    ULONG bands = 0;
    ULONG streams = 0;
    ULONG width = 0;
    ULONG standards = 0;
    for (ULONG i = 0; i < elements; ++i) {
        if (remaining < 8) {
            return STATUS_DEVICE_PROTOCOL_ERROR;
        }
        ULONG type = ReadLittleEndian32(payload);
        ULONG bytes = ReadLittleEndian32(payload + 4);
        payload += 8;
        remaining -= 8;
        if (bytes > remaining) {
            return STATUS_DEVICE_PROTOCOL_ERROR;
        }

        if (type == MCU_NIC_CAP_PHY) {
            if (bytes < 12 || streams != 0 || payload[4] == 0 || payload[4] > 8) {
                return STATUS_DEVICE_PROTOCOL_ERROR;
            }
            bands |= payload[10] & (MT7922_NIC_BAND_24GHZ | MT7922_NIC_BAND_5GHZ);
            streams = payload[4];
            width = payload[3];
            if (payload[0]) standards |= MT7922_NIC_PHY_HT;
            if (payload[1]) standards |= MT7922_NIC_PHY_VHT;
            if (payload[11]) standards |= MT7922_NIC_PHY_HE;
        } else if (type == MCU_NIC_CAP_MAC_ADDR && bytes >= sizeof(Result->NicMac)) {
            RtlCopyMemory(Result->NicMac, payload, sizeof(Result->NicMac));
        } else if (type == MCU_NIC_CAP_6GHZ) {
            if (bytes == 0) {
                return STATUS_DEVICE_PROTOCOL_ERROR;
            }
            if (payload[0]) bands |= MT7922_NIC_BAND_6GHZ;
        }
        payload += bytes;
        remaining -= bytes;
    }
    if (streams == 0 || bands == 0) {
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }
    Result->NicBands = bands;
    Result->NicSpatialStreams = streams;
    Result->NicBandwidthCode = width;
    Result->NicPhyFlags = standards;
    return STATUS_SUCCESS;
}

void Mt7922McuSetScanReceiver(MCU_RINGS *Rings, void *Context,
    MT7922_SCAN_RECORD_HANDLER Event, MT7922_SCAN_RECORD_HANDLER Frame)
{
    Rings->ScanContext = Context;
    Rings->ScanEvent = Event;
    Rings->ScanFrame = Frame;
}

static NTSTATUS ExchangeUnifiedWait(MCU_RINGS *Rings, UCHAR CommandId, const UCHAR *Payload,
    ULONG PayloadBytes, MT7922_MCU_PROBE_INFO *Result, ULONG TimeoutMs, bool Sensitive);

int32_t Mt7922McuSubmitStationScan(void *Context, uint8_t Command,
    const uint8_t *Payload, size_t Bytes)
{
    MT7922_MCU_SCAN_TRANSPORT *transport = Context;
    MCU_RINGS *rings = transport->Rings;
    if (rings == NULL || !rings->Programmed || rings->DataRx.Count == 0 ||
        rings->StationStopping) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (transport->Radio == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (transport->Associated) {
        return STATUS_DEVICE_BUSY; /* Shared mode keeps the router channel until off-channel scan is validated. */
    }
    uint8_t scan[MT7922_SCAN_REQUEST_BYTES];
    if (Command == MT7922_SCAN_START_COMMAND) {
        if (Payload == NULL || Bytes != sizeof(scan) || !Mt7922LimitStationScan(scan, Payload, transport->Radio)) {
            return STATUS_NOT_SUPPORTED;
        }
        Payload = scan;
    }
    if (rings->ChipId == MT7925_CHIP_ID) {
        uint8_t unified[MT7925_SCAN_REQUEST_MAX_BYTES];
        size_t unifiedBytes;
        if (Command == MT7922_SCAN_START_COMMAND) {
            unifiedBytes = Mt7925BuildScanRequest(unified, sizeof(unified), Payload, Bytes);
        } else if (Command == MT7922_SCAN_CANCEL_COMMAND && transport->ScanActive &&
            Payload != NULL && Bytes == MT7922_SCAN_CANCEL_BYTES &&
            (Payload[0] & 0x7fU) == (transport->ScanSequence & 0x7fU)) {
            unifiedBytes = Mt7925BuildScanCancel(unified, sizeof(unified),
                transport->ScanSequence, transport->ScanBssIndex);
        } else {
            return STATUS_NOT_SUPPORTED;
        }
        if (unifiedBytes == 0) {
            return STATUS_INVALID_PARAMETER;
        }
        NTSTATUS status = ExchangeUnifiedWait(rings, MCU_UNI_SCAN_REQ, unified,
            (ULONG)unifiedBytes, transport->Result, 3000, false);
        if (NT_SUCCESS(status) && Command == MT7922_SCAN_START_COMMAND) {
            transport->ScanSequence = Payload[0];
            transport->ScanBssIndex = Payload[1];
            transport->ScanActive = true;
        } else if (NT_SUCCESS(status) && Command == MT7922_SCAN_CANCEL_COMMAND) {
            transport->ScanActive = false;
        }
        return status;
    }
    uint8_t packet[MT7922_SCAN_TX_BYTES];
    const uint8_t sequence = (uint8_t)(rings->Sequence % 15 + 1);
    const size_t length = Mt7922FrameScanCommand(packet, sizeof(packet), Command,
        sequence, Payload, Bytes);
    if (length == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    rings->Sequence = sequence;
    transport->Result->LastCommand = Command;
    return SendPacket(rings, &rings->ControlTx, packet, (ULONG)length, transport->Result);
}

static ULONGLONG AwdlHostTime(void);

static void ObserveSharedAckDma(MCU_RINGS *Rings, ULONG Device)
{
    TX_RING *ring = &Rings->ActionTx;
    if (Rings->SharedAckDmaPending == 0 || Device >= ring->Count ||
        Device == Rings->SharedAckDmaCursor) {
        return;
    }

    const ULONGLONG now = AwdlHostTime();
    /* The device index points to the next descriptor to be consumed. */
    do {
        const ULONG index = Rings->SharedAckDmaCursor;
        MT7922_AWDL_ACK_TRACE_ENTRY *trace = Rings->SharedAckDmaTrace[index];
        if (trace != NULL) {
            if (trace->TxStatusAt == 0 && trace->ReleasedAt == 0) {
                trace->DmaObservedAt = now;
            }
            Rings->SharedAckDmaTrace[index] = NULL;
            --Rings->SharedAckDmaPending;
        }
        Rings->SharedAckDmaCursor = (index + 1U) % ring->Count;
    } while (Rings->SharedAckDmaCursor != Device && Rings->SharedAckDmaPending != 0);
}

static bool StationDataTraceCurrent(const MCU_RINGS *Rings, const STATION_DATA_TX *Slot)
{
    return Slot->TraceDeadline != 0 && *Rings->StationIoTraceDeadline == Slot->TraceDeadline &&
        Rings->StationIoDebug->Active;
}

static void RecordStationDataDma(MCU_RINGS *Rings, STATION_DATA_TX *Slot, bool FromRelease)
{
    Slot->DmaObservedAt = AwdlHostTime();
    if (StationDataTraceCurrent(Rings, Slot)) {
        MT7922_STATION_IO_DEBUG *trace = Rings->StationIoDebug;
        const ULONG elapsed = (ULONG)((Slot->DmaObservedAt - Slot->SubmittedAt) / 10U);
        trace->TxMaxSubmitToDmaUs = max(trace->TxMaxSubmitToDmaUs, elapsed);
        trace->TxDmaObservedAtRelease += FromRelease;
    }
#ifdef MT7922_STATION_PACKET_TRACE
    if (Slot->PacketTraced) {
        /* A release proves DMA consumption if its cursor was not sampled in time. */
        Mt7922WriteStationPacketTrace(&Slot->Packet, Mt7922TraceTxDmaObserved, FromRelease);
    }
#endif
}

static void ObserveStationDataDma(MCU_RINGS *Rings, ULONG Device)
{
    TX_RING *ring = &Rings->ActionTx;
    if (Rings->StationDataInFlight == 0 || Device >= ring->Count || Device == Rings->StationDataDmaCursor) {
        return;
    }
    const ULONG cursor = Rings->StationDataDmaCursor;
    const ULONG advanced = (Device + ring->Count - cursor) % ring->Count;
    for (ULONG i = 0; i < MT7922_STATION_DATA_TX_SLOTS; ++i) {
        STATION_DATA_TX *slot = &Rings->StationDataTx[i];
        if (slot->SubmittedAt != 0 && slot->DmaObservedAt == 0 &&
            (slot->Descriptor + ring->Count - cursor) % ring->Count < advanced) {
            RecordStationDataDma(Rings, slot, false);
        }
    }
    Rings->StationDataDmaCursor = Device;
}

static void CheckStationSharedDma(MCU_RINGS *Rings)
{
    MT7922_AWDL_SYN_ACK_QUEUE_TIMING *timing = Rings->SharedSynAckQueueTiming;
    if (Rings->SharedAdvertDmaPending || Rings->SharedAckDmaPending != 0 || Rings->StationDataInFlight != 0) {
        const ULONG device = ReadRegister(Rings->Registers, Rings->ActionTx.Registers + RING_DEVICE);
        ObserveSharedAckDma(Rings, device);
        ObserveStationDataDma(Rings, device);
        if (Rings->SharedAdvertDmaPending && device == Rings->ActionTx.Producer) {
            Rings->SharedAdvertDmaPending = false;
            if (Rings->SharedAdvertDebug->RequestId == Rings->SharedAdvertRequestId) {
                Rings->SharedAdvertDebug->DmaFetched = 1;
            }
            if (timing != NULL) {
                timing->DmaObservedUs = (ULONG)((AwdlHostTime() - Rings->SharedSynAckQueueSubmittedAt) / 10U);
                timing->SamplesPresent |= MT7922_AWDL_SYN_ACK_DMA_OBSERVED;
            }
        }
    }
    if (timing == NULL || Rings->SharedAdvertDmaPending || Rings->SharedAdvertQueue != STATION_SHARED_AC ||
        (timing->SamplesPresent & 8U)) {
        return;
    }

    const ULONG elapsedUs = (ULONG)((AwdlHostTime() - Rings->SharedSynAckQueueSubmittedAt) / 10U);
    const ULONG dmaAgeUs = elapsedUs - timing->DmaObservedUs;
    const ULONG sample = dmaAgeUs >= 100000U ? 8U : dmaAgeUs >= 50000U ? 4U :
        dmaAgeUs >= 10000U ? 2U : 1U;
    if ((timing->SamplesPresent & sample) == 0) {
        /* Use the bitmap for the shared descriptor's WCID 0. No PLE query write is needed. */
        const ULONG empty = ReadRegister(Rings->Registers, PLE_AC_QEMPTY(STATION_SHARED_AC));
        timing->SamplesPresent |= sample;
        if ((empty & 1U) == 0) {
            timing->NonemptySamples |= sample;
        }
    }
}

static void RecordSharedAdvertTxReport(MCU_RINGS *Rings, const UCHAR *Data, ULONG Length)
{
    if (Rings->SharedAckInFlight != 0 && Length >= sizeof(ULONG) &&
        (ReadLittleEndian32(Data) >> 27) == MT7922_TX_STATUS_PACKET_TYPE) {
        for (ULONG i = 0; i < STATION_SHARED_TX_SLOTS; ++i) {
            STATION_SHARED_ACK_TX *slot = &Rings->SharedAckTx[i];
            if (slot->SubmittedAt == 0 || slot->ReportSeen) {
                continue;
            }
            const MT7922_AWDL_TX tx = {
                .PacketId = (UCHAR)(STATION_SHARED_TX_PID_BASE + i),
                .Wcid = slot->Wcid
            };
            MT7922_TX_REPORT report;
            if (Mt792xReadTxReport((USHORT)Rings->ChipId, Data, Length, &tx, &report) == Mt7922TxReportMatched &&
                report.Sequence == slot->Sequence) {
                slot->Report = report;
                slot->ReportSeen = true;
                if (slot->Trace != NULL) {
                    slot->Trace->TxStatusAt = AwdlHostTime();
                    slot->Trace->RateCode = report.RateCode;
                    slot->Trace->AckErrors = report.AckErrors;
                }
            }
        }
    }
    MT7922_SHARED_ADVERT_DEBUG *debug = Rings->SharedAdvertDebug;
    if (debug == NULL || !debug->Submitted || debug->TxReportSeen ||
        debug->RequestId != Rings->SharedAdvertRequestId) {
        return;
    }
    const MT7922_AWDL_TX tx = {.PacketId = STATION_SHARED_PACKET_ID, .Wcid = Rings->SharedAdvertWcid};
    MT7922_TX_REPORT report;
    if (Mt792xReadTxReport((USHORT)Rings->ChipId, Data, Length, &tx, &report) == Mt7922TxReportMatched &&
        report.Sequence == Rings->SharedAdvertSequence) {
        debug->TxReportSeen = 1;
        debug->TxRateCode = report.RateCode;
        debug->TxAckErrors = report.AckErrors;
    }
}

static bool ConsumeStationTxRelease(MCU_RINGS *Rings, const UCHAR *Data, ULONG Length)
{
    MT7922_TX_RELEASE release;
    bool sharedReleased = false;
    if (Rings->SharedAdvertReleasePending &&
        Mt792xReadTxRelease((USHORT)Rings->ChipId, Data, Length, STATION_SHARED_TOKEN,
            &release) == Mt7922TxReportMatched) {
        Rings->SharedAdvertReleasePending = false;
        Rings->SharedAdvertDmaPending = false;
        MT7922_AWDL_SYN_ACK_QUEUE_TIMING *timing = Rings->SharedSynAckQueueTiming;
        if (timing != NULL) {
            timing->ReleaseObservedUs =
                (ULONG)((AwdlHostTime() - Rings->SharedSynAckQueueSubmittedAt) / 10U);
            Rings->SharedSynAckQueueTiming = NULL;
        }
        MT7922_SHARED_ADVERT_DEBUG *debug = Rings->SharedAdvertDebug;
        if (debug->RequestId == Rings->SharedAdvertRequestId) {
            debug->DmaFetched = 1;
            debug->Released = 1;
            debug->ReleaseStatus = release.Status;
            if (debug->Status == STATUS_PENDING) {
                if (release.Status != 0) {
                    debug->Status = STATUS_DEVICE_PROTOCOL_ERROR;
                } else if (!Rings->SharedTxWaitForEcho) {
                    debug->Status = STATUS_SUCCESS;
                }
            }
        }
        sharedReleased = true;
    }
    if (Rings->SharedAckInFlight != 0 && Length >= sizeof(ULONG) &&
        (ReadLittleEndian32(Data) >> 27) == MT7922_TX_RELEASE_PACKET_TYPE) {
        for (ULONG i = 0; i < STATION_SHARED_TX_SLOTS; ++i) {
            STATION_SHARED_ACK_TX *slot = &Rings->SharedAckTx[i];
            if (slot->SubmittedAt != 0 && !slot->Released &&
                Mt792xReadTxRelease((USHORT)Rings->ChipId, Data, Length,
                    (USHORT)(STATION_SHARED_TX_TOKEN_BASE + i),
                    &release) == Mt7922TxReportMatched) {
                slot->Released = true;
                slot->ReleaseStatus = release.Status;
                if (slot->Trace != NULL) {
                    slot->Trace->ReleasedAt = AwdlHostTime();
                    slot->Trace->ReleaseStatus = release.Status;
                    slot->Trace->Attempts = release.Attempts;
                }
                sharedReleased = true;
            }
        }
    }
    if (Rings->StationDataInFlight != 0 && Length >= sizeof(ULONG) &&
        (ReadLittleEndian32(Data) >> 27) == MT7922_TX_RELEASE_PACKET_TYPE) {
        ObserveStationDataDma(Rings, ReadRegister(Rings->Registers, Rings->ActionTx.Registers + RING_DEVICE));
        for (ULONG i = 0; i < MT7922_STATION_DATA_TX_SLOTS; ++i) {
            STATION_DATA_TX *slot = &Rings->StationDataTx[i];
            if (slot->SubmittedAt == 0 || Mt792xReadTxRelease((USHORT)Rings->ChipId, Data, Length,
                    (USHORT)(STATION_DATA_TX_TOKEN_BASE + i), &release) != Mt7922TxReportMatched) {
                continue;
            }
            const bool independentlyObserved = slot->DmaObservedAt != 0;
            if (!independentlyObserved) {
                RecordStationDataDma(Rings, slot, true);
            }
            MT7922_STATION_DATA_DEBUG *debug = Rings->StationDataDebug;
            if (debug != NULL) {
                ++debug->TxCompleted;
                debug->TxCompletionErrors += release.Status != 0;
                debug->TxReleaseStatus = release.Status;
            }
            if (StationDataTraceCurrent(Rings, slot)) {
                MT7922_STATION_IO_DEBUG *trace = Rings->StationIoDebug;
                ++trace->TxReleased;
                if (independentlyObserved) {
                    const ULONG elapsed = (ULONG)((AwdlHostTime() - slot->DmaObservedAt) / 10U);
                    trace->TxMaxDmaToReleaseUs = max(trace->TxMaxDmaToReleaseUs, elapsed);
                    trace->TxReleaseOver50Ms += elapsed >= 50000U;
                }
            }
#ifdef MT7922_STATION_PACKET_TRACE
            if (slot->PacketTraced) {
                Mt7922WriteStationPacketTrace(&slot->Packet, Mt7922TraceTxFree, release.Status);
            }
#endif
            slot->SubmittedAt = 0;
            --Rings->StationDataInFlight;
            sharedReleased = true;
        }
    }
    if (Rings->StationTxPending &&
        Mt792xReadTxRelease((USHORT)Rings->ChipId, Data, Length,
            Rings->StationTxToken, &release) == Mt7922TxReportMatched) {
        Rings->StationTxPending = false;
        Rings->StationTxStatus = release.Status;
        return true;
    }
    return sharedReleased;
}

static BOOLEAN ConsumeMcuEvent(MCU_RINGS *Rings, MT7922_RX_RING *Ring, UCHAR Sequence, ULONG CommandId,
    ULONG *Reply, MT7922_MCU_PROBE_INFO *Result, NTSTATUS *Outcome)
{
    const UCHAR *data;
    ULONG control;
    if (!Mt7922PeekRxRing(Ring, &data, &control)) {
        return FALSE;
    }

    ULONG length = (control & DESCRIPTOR_LEN0) >> DESCRIPTOR_LEN_SHIFT;
    if (length <= RX_BUFFER_BYTES && (control & DESCRIPTOR_LAST0) != 0 &&
        (control & (DESCRIPTOR_DROP | DESCRIPTOR_TO_HOST_A)) == 0) {
        /* Like mt76, dispatch TX-free records independently of the MCU command reply. */
        if (!Ring->DiscardContinuation && length >= sizeof(ULONG)) {
            const ULONG packetType = ReadLittleEndian32(data) >> 27;
            if (packetType == MT7922_TX_RELEASE_PACKET_TYPE) {
                const ULONG completed = Rings->StationDataDebug != NULL
                    ? Rings->StationDataDebug->TxCompleted : 0;
                const bool stationPending = Rings->StationTxPending;
                ConsumeStationTxRelease(Rings, data, length);
                if (Rings->StationDataDebug != NULL) {
                    Rings->StationDataDebug->TxReleaseDuringCommand +=
                        Rings->StationDataDebug->TxCompleted != completed ||
                        (stationPending && !Rings->StationTxPending);
                }
                Mt7922RecycleRxRing(Ring);
                return TRUE;
            }
            if (packetType == MT7922_TX_STATUS_PACKET_TYPE) {
                RecordSharedAdvertTxReport(Rings, data, length);
            }
        }
        if (Rings->ScanEvent != NULL && Rings->ScanEvent(Rings->ScanContext, data, length)) {
            Mt7922RecycleRxRing(Ring);
            return TRUE;
        }
    }
    const ULONG rxHeaderBytes = McuRxHeaderBytes(Rings->ChipId);
    const ULONG eventOffset = McuRxEventOffset(Rings->ChipId);
    if (length > eventOffset + 1U && length <= RX_BUFFER_BYTES) {
        Result->EventId = data[eventOffset];
        Result->EventSequence = data[eventOffset + 1U];
        if (Result->EventSequence == Sequence) {
            if (CommandId == MCU_NIC_CAPABILITIES) {
                *Outcome = ParseNicCapabilities(data, length, Result);
            } else if (CommandId & MCU_UNIFIED_FLAG) {
                const UCHAR *payload = data + rxHeaderBytes;
                if (Rings->ChipId == 0x7925 &&
                    (UCHAR)CommandId == MCU_UNI_CHIP_CONFIG) {
                    *Reply = 0;
                    *Outcome = length >= rxHeaderBytes
                        ? ParseMt7925NicCapabilities(payload, length - rxHeaderBytes, Result)
                        : STATUS_DEVICE_PROTOCOL_ERROR;
                } else if (Result->EventId == MCU_UNI_RESULT &&
                    length >= rxHeaderBytes + MCU_UNI_RESULT_BYTES &&
                    payload[0] == (UCHAR)CommandId) {
                    Result->MonitorReply = ReadLittleEndian32(payload + 4);
                    *Reply = Result->MonitorReply;
                    *Outcome = *Reply == 0 ? STATUS_SUCCESS : STATUS_DEVICE_PROTOCOL_ERROR;
                } else {
                    *Outcome = STATUS_DEVICE_PROTOCOL_ERROR;
                }
            } else if (CommandId == MCU_CE_REG_READ) {
                const UCHAR *payload = data + rxHeaderBytes;
                if (length >= rxHeaderBytes + 8 &&
                    ReadLittleEndian32(payload) == MCU_REG_PSE_BASE) {
                    *Reply = ReadLittleEndian32(payload + 4);
                    *Outcome = STATUS_SUCCESS;
                } else {
                    *Outcome = STATUS_DEVICE_PROTOCOL_ERROR;
                }
            } else if ((UCHAR)CommandId == MCU_EXTENDED) {
                /* Extended runtime replies are matched by sequence, as in mt7921. */
                *Outcome = length < rxHeaderBytes
                    ? STATUS_DEVICE_PROTOCOL_ERROR : STATUS_SUCCESS;
                *Reply = 0;
                Result->ReplyPayloadBytes = 0;
                if (NT_SUCCESS(*Outcome)) {
                    ULONG bytes = length - rxHeaderBytes;
                    if (bytes > sizeof(Result->ReplyPayload)) {
                        bytes = sizeof(Result->ReplyPayload);
                    }
                    RtlCopyMemory(Result->ReplyPayload, data + rxHeaderBytes, bytes);
                    Result->ReplyPayloadBytes = bytes;
                }
            } else {
                const BOOLEAN semaphore = CommandId == MCU_PATCH_SEMAPHORE;
                ULONG expected = semaphore ? MCU_ROM_SEMAPHORE_EVENT : MCU_ROM_RESULT_EVENT;
                if (!semaphore) {
                    Result->RomResultCommand = CommandId;
                    Result->RomResultBytes = length;
                    Result->RomResultStatus = MCU_NO_RESPONSE;
                }
                const ULONG resultOffset = rxHeaderBytes - 4U;
                if (length <= resultOffset || Result->EventId != expected) {
                    *Outcome = STATUS_DEVICE_PROTOCOL_ERROR;
                } else {
                    *Reply = data[resultOffset];
                    if (!semaphore) {
                        Result->RomResultStatus = *Reply;
                    }
                    *Outcome = semaphore || *Reply == 0
                        ? STATUS_SUCCESS : STATUS_DEVICE_PROTOCOL_ERROR;
                }
            }
        }
    }

    Mt7922RecycleRxRing(Ring);
    return TRUE;
}

static NTSTATUS WaitForMcuReply(MCU_RINGS *Rings, UCHAR Sequence, ULONG CommandId,
    ULONG *Reply, MT7922_MCU_PROBE_INFO *Result, ULONG TimeoutMs)
{
    ULONGLONG start = KeQueryInterruptTime();
    do {
        NTSTATUS outcome = STATUS_PENDING;
        BOOLEAN romEvent = ConsumeMcuEvent(Rings, &Rings->RomEvents, Sequence,
            CommandId, Reply, Result, &outcome);
        if (outcome != STATUS_PENDING) {
            return outcome;
        }
        BOOLEAN runtimeEvent = FALSE;
        if (Rings->ChipId != 0x7925) {
            runtimeEvent = ConsumeMcuEvent(Rings, &Rings->RuntimeEvents, Sequence,
                CommandId, Reply, Result, &outcome);
            if (outcome != STATUS_PENDING) {
                return outcome;
            }
        }
        if (!romEvent && !runtimeEvent) {
            WaitMilliseconds(1);
        }
    } while (KeQueryInterruptTime() - start < (ULONGLONG)TimeoutMs * 10000);
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS ExchangeMcu(MCU_RINGS *Rings, ULONG CommandId, const ULONG *Words,
    ULONG WordCount, ULONG *Reply, MT7922_MCU_PROBE_INFO *Result)
{
    uint8_t packet[sizeof(MCU_COMMAND)];
    const UCHAR extension = (UCHAR)(CommandId >> 8);
    const UCHAR query = CommandId == MCU_NIC_CAPABILITIES || extension != 0 ?
        MCU_SET_QUERY_SET : MCU_SET_QUERY_NA;
    const UCHAR sequence = (UCHAR)(Rings->Sequence % 15 + 1);
    const size_t bytes = Mt7922FrameLegacyMcuCommand(packet, sizeof(packet), (UCHAR)CommandId,
        extension, query, sequence, (const uint8_t *)Words, (size_t)WordCount * sizeof(ULONG));
    if (bytes == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    Rings->Sequence = sequence;
    Result->LastCommand = CommandId;
    NTSTATUS status = SendPacket(Rings, &Rings->ControlTx, packet, (ULONG)bytes, Result);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    return WaitForMcuReply(Rings, sequence, CommandId, Reply, Result, 3000);
}

static NTSTATUS SendStationRadioCommand(MCU_RINGS *Rings, ULONG Command,
    uint8_t SetQuery, const uint8_t *Payload, size_t PayloadBytes, bool Wait,
    MT7922_MCU_PROBE_INFO *Result)
{
    uint8_t packet[MT7922_SCAN_TX_HEADER_BYTES + MT7922_STATION_POWER_BYTES];
    const UCHAR sequence = (UCHAR)(Rings->Sequence % 15 + 1);
    const size_t bytes = Mt7922FrameLegacyMcuCommand(packet, sizeof(packet), (uint8_t)Command,
        (uint8_t)(Command >> 8), SetQuery, sequence, Payload, PayloadBytes);
    if (bytes == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    Rings->Sequence = sequence;
    Result->LastCommand = Command;
    NTSTATUS status = SendPacket(Rings, &Rings->ControlTx, packet, (ULONG)bytes, Result);
    ULONG reply;
    return NT_SUCCESS(status) && Wait ? WaitForMcuReply(Rings, sequence, Command, &reply, Result, 3000) : status;
}

static NTSTATUS ExchangeUnifiedWait(MCU_RINGS *Rings, UCHAR CommandId, const UCHAR *Payload,
    ULONG PayloadBytes, MT7922_MCU_PROBE_INFO *Result, ULONG TimeoutMs, bool Sensitive)
{
    MCU_UNIFIED_COMMAND command = {0};
    if (PayloadBytes > sizeof(command.Payload)) {
        return STATUS_INVALID_PARAMETER;
    }
    const ULONG length = FIELD_OFFSET(MCU_UNIFIED_COMMAND, Payload) + PayloadBytes;
    command.Hardware[0] = length | TXD0_COMMAND_FORMAT | TXD0_WM_QUEUE;
    command.Hardware[1] = TXD1_LONG_COMMAND;
    command.Length = (USHORT)(length - sizeof(command.Hardware));
    command.Command = CommandId;
    command.PacketId = MCU_PACKET_ID;
    command.Option = Rings->ChipId == 0x7925 && CommandId == MCU_UNI_CHIP_CONFIG
        ? MCU_UNI_SET_NO_ACK : MCU_UNI_SET_ACK;
    Rings->Sequence = (UCHAR)(Rings->Sequence % 15 + 1);
    command.Sequence = Rings->Sequence;
    RtlCopyMemory(command.Payload, Payload, PayloadBytes);
    Result->LastCommand = MCU_UNIFIED_FLAG | CommandId;
    Result->MonitorReply = MCU_NO_RESPONSE;

    NTSTATUS status = SendPacket(Rings, &Rings->ControlTx, &command, length, Result);
    if (NT_SUCCESS(status)) {
        ULONG reply;
        status = WaitForMcuReply(Rings, command.Sequence, Result->LastCommand, &reply, Result, TimeoutMs);
        if (Sensitive) {
            RtlSecureZeroMemory(Rings->Buffer + Rings->ControlTx.PayloadOffset, length);
        }
    }
    if (Sensitive) {
        RtlSecureZeroMemory(&command, sizeof(command));
    }
    return status;
}

static NTSTATUS ExchangeUnified(MCU_RINGS *Rings, UCHAR CommandId, const UCHAR *Payload,
    ULONG PayloadBytes, MT7922_MCU_PROBE_INFO *Result)
{
    return ExchangeUnifiedWait(Rings, CommandId, Payload, PayloadBytes, Result, 3000, false);
}

static NTSTATUS DownloadRegion(MCU_RINGS *Rings, const UCHAR *Data, ULONG Target,
    ULONG Length, UCHAR Command, MT7922_MCU_PROBE_INFO *Result)
{
    const ULONG args[] = {Target, Length, MCU_DOWNLOAD_MODE};
    ULONG reply;
    NTSTATUS status = ExchangeMcu(Rings, Command, args, RTL_NUMBER_OF(args), &reply, Result);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    for (ULONG offset = 0; offset < Length;) {
        ULONG remaining = Length - offset;
        ULONG bytes = remaining < PAGE_SIZE ? remaining : PAGE_SIZE;
        status = SendPacket(Rings, &Rings->DownloadTx, Data + offset, bytes, Result);
        if (!NT_SUCCESS(status)) {
            return status;
        }
        ++Result->DownloadPackets;
        if (Result->LastTxDescriptorControl & DESCRIPTOR_DONE) {
            ++Result->DownloadDonePackets;
        }
        Result->DownloadBytes += bytes;
        offset += bytes;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS WaitForFirmwareReady(PUCHAR Window, ULONG ChipId, MT7922_MCU_PROBE_INFO *Result)
{
    /* MT7925 and MT7922 expose CONN_ON_MISC at the same fixed BAR0 address. */
    const ULONG bankWindow = RegisterBankWindow(ChipId, CONNECTION_BANK);
    const ULONG ready = bankWindow + CONNECTION_FIRMWARE;
    NTSTATUS status = WaitRegister(Window, ready, CONNECTION_READY, CONNECTION_READY,
        1500, &Result->FirmwareReady);
    if (!NT_SUCCESS(status)) {
        Result->BlockedRegister = ready;
        Result->BlockedValue = Result->FirmwareReady;
    }

    ULONG saved;
    if (NT_SUCCESS(SelectBank(Window, ChipId, CONNECTION_BANK, &saved))) {
        Result->FirmwareReadyRemapped = ReadRegister(Window, bankWindow + CONNECTION_FIRMWARE);
        return RestoreBank(Window, ChipId, saved, status);
    }
    return status;
}

static NTSTATUS RunCommands(MCU_RINGS *Rings, const UCHAR *Image,
    const MCU_FIRMWARE *Firmware, MT7922_MCU_PROBE_INFO *Result)
{
    ULONG operation = MCU_SEM_GET;
    Result->Stage = MT7922_MCU_GET;
    NTSTATUS status = ExchangeMcu(Rings, MCU_PATCH_SEMAPHORE, &operation, 1, &Result->GetReply, Result);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    if (Result->GetReply == MCU_SEM_ACQUIRED) {
        if (Image != NULL) {
            Result->Stage = MT7922_MCU_PATCH;
            ULONG offset = 0;
            for (ULONG index = 0; index < Firmware->PatchRegionCount; ++index) {
                const ULONG bytes = Firmware->PatchRegionBytes[index];
                status = DownloadRegion(Rings, Image + offset, Firmware->PatchTargets[index],
                    bytes, MCU_PATCH_START, Result);
                if (!NT_SUCCESS(status)) {
                    break;
                }
                offset += bytes;
            }
            if (NT_SUCCESS(status)) {
                ULONG reply;
                const ULONG checkCrc = 0;
                status = ExchangeMcu(Rings, MCU_PATCH_FINISH, &checkCrc, 1, &reply, Result);
            }
        }
        if (NT_SUCCESS(status)) {
            Result->Stage = MT7922_MCU_RELEASE;
        }
        operation = MCU_SEM_RELEASE;
        NTSTATUS release = ExchangeMcu(Rings, MCU_PATCH_SEMAPHORE,
            &operation, 1, &Result->ReleaseReply, Result);
        if (NT_SUCCESS(status)) {
            status = NT_SUCCESS(release) && Result->ReleaseReply != MCU_SEM_RELEASED
                ? STATUS_DEVICE_PROTOCOL_ERROR : release;
        }
    } else if (Result->GetReply != MCU_PATCH_PRESENT) {
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }

    if (!NT_SUCCESS(status) || Image == NULL) {
        return status;
    }

    Result->Stage = MT7922_MCU_RAM;
    ULONG offset = 0;
    for (ULONG index = 0; index < Firmware->PatchRegionCount; ++index) {
        offset += Firmware->PatchRegionBytes[index];
    }
    for (ULONG index = 0; index < Firmware->RamRegionCount; ++index) {
        const ULONG bytes = Firmware->RamRegionBytes[index];
        status = DownloadRegion(Rings, Image + offset, Firmware->RamTargets[index],
            bytes, MCU_TARGET_ADDRESS_LEN, Result);
        if (!NT_SUCCESS(status)) {
            return status;
        }
        offset += bytes;
    }

    Result->Stage = MT7922_MCU_START;
    const ULONG startArgs[] = {1, Firmware->EntryAddress};
    ULONG reply;
    status = ExchangeMcu(Rings, MCU_FW_START, startArgs, RTL_NUMBER_OF(startArgs), &reply, Result);
    if (NT_SUCCESS(status)) {
        status = WaitForFirmwareReady(Rings->Registers, Rings->ChipId, Result);
    }
    if (NT_SUCCESS(status)) {
        Result->Stage = MT7922_MCU_CAPABILITIES;
        if (Rings->ChipId == 0x7925) {
            const UCHAR request[8] = {
                0, 0, 0, 0,
                MCU_UNI_CHIP_CONFIG_NIC_CAPA, 0,
                4, 0
            };
            status = ExchangeUnifiedWait(Rings, MCU_UNI_CHIP_CONFIG, request,
                sizeof(request), Result, 3000, false);
        } else {
            status = ExchangeMcu(Rings, MCU_NIC_CAPABILITIES, NULL, 0, &reply, Result);
        }
    }
    return status;
}

static void CountIosAction(void *Context, const AWDL_ACTION *Action)
{
    MT7922_AWDL_RX_COUNTS *counts = Context;
    if (Action->DeviceClass == 2) {
        ++counts->Ios;
    }
}

static void CountAwdlRxRecord(MT7922_AWDL_RX_COUNTS *Counts, MT7922_AWDL_RX_STATUS Status)
{
    ++Counts->Descriptors;
    if (Status == Mt7922AwdlRxPsf) {
        ++Counts->Psf;
    } else if (Status == Mt7922AwdlRxMif) {
        ++Counts->Mif;
    } else if (Status == Mt7922AwdlRxInvalid) {
        ++Counts->Invalid;
    }
}

typedef struct _AWDL_SURVEY_CONTEXT {
    MT7922_MCU_PROBE_INFO *Result;
    MT7922_AWDL_RX_COUNTS *Counts;
    ULONG ChannelIndex;
} AWDL_SURVEY_CONTEXT;

C_ASSERT(MT7922_AWDL_SURVEY_SLOTS == AWDL_CHANNEL_SLOTS);

static void RecordSurveyAction(void *Context, const AWDL_ACTION *Action)
{
    AWDL_SURVEY_CONTEXT *survey = Context;
    CountIosAction(survey->Counts, Action);
    if (Action->DeviceClass != 2 || (Action->Sender[0] & 1U) != 0) {
        return;
    }

    MT7922_MCU_PROBE_INFO *result = survey->Result;
    ULONG index = 0;
    while (index < result->SurveyPeerCount &&
        RtlCompareMemory(result->SurveyPeers[index].Address, Action->Sender, 6) != 6) {
        ++index;
    }
    if (index >= RTL_NUMBER_OF(result->SurveyPeers)) {
        return;
    }
    MT7922_AWDL_SURVEY_PEER *peer = &result->SurveyPeers[index];
    if (index == result->SurveyPeerCount) {
        RtlCopyMemory(peer->Address, Action->Sender, 6);
        ++result->SurveyPeerCount;
    }
    ++peer->Received[survey->ChannelIndex];
    peer->LastChannel = (UCHAR)survey->Counts->Channel;
    if (Action->Sync.Master != NULL) {
        peer->NextChannel = Action->Sync.NextChannel;
    }
    if (Action->Sequence.Count == AWDL_CHANNEL_SLOTS) {
        peer->SlotCount = Action->Sequence.Count;
        RtlCopyMemory(peer->Slots, Action->Sequence.Channel, sizeof(peer->Slots));
    }
}

static void ListenForAwdl(MT7922_RX_RING *Ring, AWDL_SURVEY_CONTEXT *Survey)
{
    ULONGLONG start = KeQueryInterruptTime();
    do {
        for (ULONG i = 0; i < Ring->Count; ++i) {
            MT7922_AWDL_RX_STATUS status = Mt7922PollAwdlRx(Ring, RecordSurveyAction, Survey);
            if (status == Mt7922AwdlRxEmpty) {
                break;
            }
            CountAwdlRxRecord(Survey->Counts, status);
        }
        WaitMilliseconds(1);
    } while (KeQueryInterruptTime() - start < (ULONGLONG)MT7922_AWDL_PROBE_DWELL_MS * 10000);
}

#define TU_TICKS 10240ULL
#define SLOT_TICKS (64ULL * TU_TICKS)
#define AW_TICKS (16ULL * TU_TICKS)
#define PSF_INTERVAL_TICKS (110ULL * TU_TICKS)
#define MT7922_TX_SYNC_RELEASE_MS 200U
#define AWDL_PROBE_MAX_LATE_TICKS (8ULL * TU_TICKS)
#define AWDL_ADDBA_MAX_RESPONSES 8U
#define AWDL_RX_BA_REPLY_MS 150U
#define AWDL_MDNS_MAX_QUERIES 3U
#define AWDL_MDNS_PORT 5353U
#define AWDL_ECHO_ID 0x4d54U
#define AWDL_TCP_MAX_SYN 2U
#define AWDL_TCP_ACK 0x10U
#define AWDL_TCP_RST 0x04U
#define AWDL_TCP_SYN 0x02U
#define DNS_HEADER_BYTES 12U

static const UCHAR AwdlMdnsMac[6] = {0x33, 0x33, 0, 0, 0, 0xfb};
static const UCHAR AwdlMdnsAddress[16] = {0xff, 2, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0xfb};
static const UCHAR AirdropDnsName[] = {8, '_', 'a', 'i', 'r', 'd', 'r', 'o', 'p',
    4, '_', 't', 'c', 'p', 5, 'l', 'o', 'c', 'a', 'l', 0};

static ULONGLONG AwdlHostTime(void)
{
    ULONGLONG performanceCounter;
    return KeQueryInterruptTimePrecise(&performanceCounter);
}

typedef struct _AWDL_PROBE_FRAME {
    const UCHAR *Source;
    const UCHAR *Target;
    ULONGLONG PreferMifUntil;
    MT7922_AWDL_RX_COUNTS *Capture;
    AWDL_CHANNEL_SEQUENCE Sequence;
    AWDL_ELECTION Election;
    ULONG Length;
    ULONG PeerTimestamp;
    ULONGLONG PeerLocalTime;
    UCHAR Peer[6];
    UCHAR Master[6];
    USHORT UntilNextSlotTu;
    USHORT NextWindowSequence;
    UCHAR MasterChannel;
    BOOLEAN PeerSelected;
    BOOLEAN SeenIos;
    UCHAR Bytes[MT7922_PROBE_PSF_CAPACITY];
} AWDL_PROBE_FRAME;

typedef struct _AWDL_UNICAST_TX {
    MT7922_AWDL_TX Request;
    MT7922_TX_ATTEMPT_INFO Completion;
    ULONGLONG SubmittedAt;
    ULONGLONG ReleasedAt;
    ULONG DataIndex;
    UCHAR AddbaToken;
    BOOLEAN IsAddba;
    BOOLEAN IsTunnelData;
    BOOLEAN IsRecoveryEcho;
    BOOLEAN Active;
} AWDL_UNICAST_TX;

typedef struct _AWDL_SYNC_REF {
    AWDL_UNICAST_TX Unicast;
    MT7922_AWDL_LINK *Link;
    MT7922_AWDL_WATCH *Watch;
    ULONG LinkTxCount;
    BOOLEAN LinkTxFailed;
    BOOLEAN LinkOnline;
    AWDL_CHANNEL_SEQUENCE Sequence;
    AWDL_ELECTION Election;
    UCHAR Channel;
    UCHAR MasterChannel;
    UCHAR Peer[6];
    UCHAR Master[6];
    USHORT UntilNextSlotTu;
    USHORT NextWindowSequence;
    USHORT ProbeSlotSequence;
    USHORT EchoSequence;
    ULONG PhysicalTx;
    ULONGLONG LocalAtRx;
    ULONGLONG LocalAtUploadData;
    ULONGLONG ProbeTime;
    ULONG ProbeOffsetTu;
    ULONG ProbesScheduled;
    MT7922_AWDL_ADDBA Addba;
    BOOLEAN ProbeArmed;
} AWDL_SYNC_REF;

static BOOLEAN IsUnicastAddress(const UCHAR *Address)
{
    if (Address[0] & 1) {
        return FALSE;
    }
    for (ULONG i = 0; i < 6; ++i) {
        if (Address[i] != 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static void BuildIosProbe(void *Context, const AWDL_ACTION *Action)
{
    AWDL_PROBE_FRAME *probe = Context;
    CountIosAction(probe->Capture, Action);
    if (Action->DeviceClass != 2 || !IsUnicastAddress(Action->Sender) ||
        (probe->Target != NULL &&
            RtlCompareMemory(probe->Target, Action->Sender, MT7922_AWDL_PEER_BYTES) != MT7922_AWDL_PEER_BYTES)) {
        return;
    }
    probe->SeenIos = TRUE;
    if (probe->Length != 0 || Action->Sync.UntilNextSlotTu < 8 ||
        (!probe->PeerSelected && Action->Kind != AwdlMif && AwdlHostTime() < probe->PreferMifUntil) ||
        (probe->PeerSelected &&
            (RtlCompareMemory(probe->Peer, Action->Sender, sizeof(probe->Peer)) != sizeof(probe->Peer) ||
                probe->PeerTimestamp == Action->PhysicalTx))) {
        return;
    }

    probe->Length = (ULONG)AwdlBuildProbePsf(probe->Bytes, sizeof(probe->Bytes),
        probe->Source, Action, (ULONG)(AwdlHostTime() / 10), (UCHAR)probe->Capture->Channel);
    if (probe->Length != 0) {
        RtlCopyMemory(probe->Peer, Action->Sender, sizeof(probe->Peer));
        RtlCopyMemory(probe->Master, Action->Sync.Master, sizeof(probe->Master));
        probe->UntilNextSlotTu = Action->Sync.UntilNextSlotTu;
        probe->NextWindowSequence = Action->Sync.NextWindowSequence;
        probe->Sequence = Action->Sequence;
        probe->Election = Action->Election;
        probe->MasterChannel = Action->Sync.MasterChannel;
        probe->PeerTimestamp = Action->PhysicalTx;
        probe->PeerLocalTime = AwdlHostTime();
        probe->PeerSelected = TRUE;
    }
}

static BOOLEAN UpdateSyncRef(AWDL_SYNC_REF *Ref, const AWDL_ACTION *Action,
    MT7922_MCU_PROBE_INFO *Result)
{
    if (Action->DeviceClass != 2 || Action->Sync.Master == NULL ||
        Action->Sync.UntilNextSlotTu == 0 || Action->Sync.UntilNextSlotTu > 64 ||
        Action->Sync.WindowTu != 16 || Action->Sync.WindowsPerSlot != 4) {
        return FALSE;
    }
    BOOLEAN found = FALSE;
    ULONG slot = 0;
    for (ULONG i = 0; i < Result->PeerCount && i < RTL_NUMBER_OF(Result->PeerAddr); ++i) {
        if (RtlCompareMemory(Result->PeerAddr[i], Action->Sender, 6) == 6) {
            slot = i;
            found = TRUE;
            break;
        }
    }
    if (!found && Result->PeerCount < RTL_NUMBER_OF(Result->PeerAddr)) {
        slot = Result->PeerCount;
        RtlCopyMemory(Result->PeerAddr[slot], Action->Sender, 6);
        ++Result->PeerCount;
        found = TRUE;
    }
    if (found) {
        ++Result->PeerFrames[slot];
    }
    /* WCID registration remains bound to the device that started this run. */
    if (RtlCompareMemory(Ref->Peer, Action->Sender, 6) != 6) {
        return TRUE;
    }
    const BOOLEAN masterChanged =
        RtlCompareMemory(Ref->Master, Action->Sync.Master, sizeof(Ref->Master)) != sizeof(Ref->Master);
    RtlCopyMemory(Ref->Master, Action->Sync.Master, sizeof(Ref->Master));
    if (masterChanged || Action->Election.Present) {
        Ref->Election = Action->Election;
    }
    if (masterChanged || Action->Sync.MasterChannel != 0) {
        Ref->MasterChannel = Action->Sync.MasterChannel;
    }
    Ref->UntilNextSlotTu = Action->Sync.UntilNextSlotTu;
    Ref->NextWindowSequence = Action->Sync.NextWindowSequence;
    if (Action->Sequence.Count == AWDL_CHANNEL_SLOTS) {
        Ref->Sequence = Action->Sequence;
    }
    Ref->PhysicalTx = Action->PhysicalTx;
    Ref->LocalAtRx = AwdlHostTime();
    if (Ref->Link == NULL && Result->DataProbeSent + Result->AddbaResponses >= MT7922_AWDL_DATA_PROBES &&
        (Result->MdnsQueriesSent >= AWDL_MDNS_MAX_QUERIES || Result->MdnsAirDropAnswers != 0)) {
        return TRUE;
    }

    const USHORT nextSlot = (USHORT)((Action->Sync.NextWindowSequence & ~3U) + 4U);
    const ULONG nextIndex = (nextSlot / 4U) % AWDL_CHANNEL_SLOTS;
    const BOOLEAN nextOnChannel = Ref->Channel == 149 && Ref->Sequence.Count == AWDL_CHANNEL_SLOTS
        ? Ref->Sequence.Channel[nextIndex] == Ref->Channel
        : Action->Sync.NextChannel == Ref->Channel;
    if (Ref->ProbeArmed) {
        if (Ref->ProbeSlotSequence == nextSlot && Ref->LocalAtRx < Ref->ProbeTime) {
            if (!nextOnChannel) {
                Ref->ProbeArmed = FALSE;
            } else {
                const ULONGLONG updated = Ref->LocalAtRx
                    + ((ULONG)Action->Sync.UntilNextSlotTu + Ref->ProbeOffsetTu) * TU_TICKS;
                const LONGLONG correction = (LONGLONG)updated - (LONGLONG)Ref->ProbeTime;
                if (correction >= -(LONGLONG)AWDL_PROBE_MAX_LATE_TICKS &&
                    correction <= (LONGLONG)AWDL_PROBE_MAX_LATE_TICKS) {
                    Ref->ProbeTime = updated;
                }
            }
        }
        return TRUE;
    }

    /* The offset rotates even if a planned window expired before submission. */
    if (nextOnChannel) {
        static const UCHAR offsets[] = {6, 22, 38, 54};
        Ref->ProbeOffsetTu = offsets[Ref->ProbesScheduled % RTL_NUMBER_OF(offsets)];
        ++Ref->ProbesScheduled;
        Ref->ProbeArmed = TRUE;
        Ref->ProbeSlotSequence = nextSlot;
        Ref->ProbeTime = Ref->LocalAtRx
            + ((ULONG)Action->Sync.UntilNextSlotTu + Ref->ProbeOffsetTu) * TU_TICKS;
    }
    return TRUE;
}

static void RecordRxRecord(MT7922_MCU_PROBE_INFO *Result, const UCHAR *Data,
    ULONG Length, ULONG Type)
{
    if (Type < RTL_NUMBER_OF(Result->TxRxTypeCount)) {
        ++Result->TxRxTypeCount[Type];
    }
    ULONG *sampleBytes;
    ULONG *sample;
    if (Type == MT7922_TX_STATUS_PACKET_TYPE) {
        sampleBytes = &Result->TxStatusSampleBytes;
        sample = Result->TxStatusSample;
    } else if (Type == MT7922_TX_RELEASE_PACKET_TYPE) {
        sampleBytes = &Result->TxReleaseSampleBytes;
        sample = Result->TxReleaseSample;
    } else {
        return;
    }
    if (*sampleBytes != 0) {
        return;
    }
    *sampleBytes = Length;
    ULONG words = Length / sizeof(ULONG);
    if (words > MT7922_TX_SAMPLE_WORDS) {
        words = MT7922_TX_SAMPLE_WORDS;
    }
    for (ULONG i = 0; i < words; ++i) {
        sample[i] = ReadLittleEndian32(Data + i * sizeof(ULONG));
    }
}

static const UCHAR *AwdlMsduToIp(const UCHAR *Body, size_t BodyBytes,
    size_t *IpBytes, BOOLEAN *Wrapped)
{
    static const UCHAR snap[] = {0xaa, 0xaa, 0x03, 0x00, 0x17, 0xf2, 0x08, 0x00};
    static const UCHAR lengthSnap[] = {0xaa, 0xaa, 0x03, 0, 0, 0};
    enum { SnapBytes = 8, AwdlHeaderBytes = SnapBytes + 8, MinimumAwDlMsdu = AwdlHeaderBytes + 40 };
    *Wrapped = FALSE;
    if (BodyBytes < MinimumAwDlMsdu) {
        return NULL;
    }
    if (RtlCompareMemory(Body, lengthSnap, sizeof(lengthSnap)) == sizeof(lengthSnap)) {
        const size_t declared = ((size_t)Body[6] << 8) | Body[7];
        /* Apple includes the inner AWDL header in this length, in addition to IPv6. */
        if (declared < MinimumAwDlMsdu || declared > MT7922_AWDL_MAX_MTU + AwdlHeaderBytes ||
            declared > BodyBytes - SnapBytes) {
            return NULL;
        }
        Body += SnapBytes;
        BodyBytes = declared;
        *Wrapped = TRUE;
    }
    if (RtlCompareMemory(Body, snap, sizeof(snap)) != sizeof(snap)) {
        return NULL;
    }
    if (Body[8] != 0x03 || Body[9] != 0x04 || Body[14] != 0x86 || Body[15] != 0xdd) {
        return NULL;
    }
    *IpBytes = BodyBytes - AwdlHeaderBytes;
    return Body + AwdlHeaderBytes;
}

static USHORT ReadBigEndian16(const UCHAR *Bytes)
{
    return (USHORT)(((USHORT)Bytes[0] << 8) | Bytes[1]);
}

static USHORT Ipv6TransportChecksum(const UCHAR *Source, const UCHAR *Dest,
    UCHAR Protocol, const UCHAR *Payload, ULONG Bytes)
{
    ULONG sum = 0;
    for (ULONG i = 0; i < 16; i += 2) {
        sum += ((ULONG)Source[i] << 8) | Source[i + 1];
        sum += ((ULONG)Dest[i] << 8) | Dest[i + 1];
    }
    sum += (Bytes >> 16) + (Bytes & 0xffff) + Protocol;
    for (ULONG i = 0; i < Bytes; i += 2) {
        sum += ((ULONG)Payload[i] << 8) | (i + 1 < Bytes ? Payload[i + 1] : 0);
    }
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return (USHORT)~sum;
}

static BOOLEAN ReadMdnsName(const UCHAR *Message, size_t Length, size_t *Cursor,
    BOOLEAN *IsAirDrop)
{
    size_t position = *Cursor;
    size_t afterPointer = 0;
    size_t expanded = 0;
    BOOLEAN matches = TRUE;
    for (ULONG steps = 0; steps < 255 && position < Length; ++steps) {
        UCHAR labelBytes = Message[position++];
        if ((labelBytes & 0xc0U) == 0xc0U) {
            if (position >= Length) {
                return FALSE;
            }
            const size_t target = ((size_t)(labelBytes & 0x3fU) << 8) | Message[position++];
            if (target < DNS_HEADER_BYTES || target >= position - 2) {
                return FALSE;
            }
            if (afterPointer == 0) {
                afterPointer = position;
            }
            position = target;
            continue;
        }
        if (labelBytes > 63 || labelBytes > Length - position ||
            expanded + 1 + labelBytes > 255) {
            return FALSE;
        }
        matches = matches && expanded < sizeof(AirdropDnsName) &&
            AirdropDnsName[expanded] == labelBytes;
        ++expanded;
        if (labelBytes == 0) {
            *Cursor = afterPointer != 0 ? afterPointer : position;
            *IsAirDrop = matches && expanded == sizeof(AirdropDnsName);
            return TRUE;
        }
        for (ULONG i = 0; i < labelBytes; ++i) {
            UCHAR ch = Message[position++];
            if (ch >= 'A' && ch <= 'Z') {
                ch = (UCHAR)(ch + ('a' - 'A'));
            }
            matches = matches && expanded < sizeof(AirdropDnsName) && AirdropDnsName[expanded] == ch;
            ++expanded;
        }
    }
    return FALSE;
}

static void RememberMdnsResponse(MT7922_MCU_PROBE_INFO *Result, const UCHAR *Message,
    size_t Length, const UCHAR *Source)
{
    if (Length > MT7922_AWDL_MDNS_SAMPLE_BYTES || Result->MdnsSampleCount >= MT7922_AWDL_MDNS_SAMPLES) {
        return;
    }
    for (ULONG i = 0; i < Result->MdnsSampleCount; ++i) {
        const MT7922_AWDL_MDNS_SAMPLE *sample = &Result->MdnsSamples[i];
        if (sample->Length == Length && RtlCompareMemory(sample->SourceIpv6, Source, 16) == 16 &&
            RtlCompareMemory(sample->Bytes, Message, Length) == Length) {
            return;
        }
    }
    MT7922_AWDL_MDNS_SAMPLE *sample = &Result->MdnsSamples[Result->MdnsSampleCount++];
    sample->Length = (ULONG)Length;
    RtlCopyMemory(sample->SourceIpv6, Source, sizeof(sample->SourceIpv6));
    RtlCopyMemory(sample->Bytes, Message, Length);
}

static void CountMdnsResponse(MT7922_MCU_PROBE_INFO *Result, const UCHAR *Udp,
    size_t Length, BOOLEAN SelectedPeer, const UCHAR *Source)
{
    if (Length < 8 + DNS_HEADER_BYTES || ReadBigEndian16(Udp) != AWDL_MDNS_PORT ||
        ReadBigEndian16(Udp + 2) != AWDL_MDNS_PORT || ReadBigEndian16(Udp + 4) != Length) {
        return;
    }
    const UCHAR *message = Udp + 8;
    const size_t bytes = Length - 8;
    if (ReadBigEndian16(message) != 0 || (message[2] & 0xf8U) != 0x80U ||
        (message[3] & 0x0fU) != 0) {
        return;
    }
    size_t cursor = DNS_HEADER_BYTES;
    BOOLEAN isAirDrop;
    const ULONG questions = ReadBigEndian16(message + 4);
    for (ULONG i = 0; i < questions; ++i) {
        if (!ReadMdnsName(message, bytes, &cursor, &isAirDrop) || bytes - cursor < 4) {
            return;
        }
        cursor += 4;
    }
    ULONG airDropAnswers = 0;
    const ULONG records = (ULONG)ReadBigEndian16(message + 6) +
        ReadBigEndian16(message + 8) + ReadBigEndian16(message + 10);
    for (ULONG i = 0; i < records; ++i) {
        if (!ReadMdnsName(message, bytes, &cursor, &isAirDrop) || bytes - cursor < 10) {
            return;
        }
        const USHORT type = ReadBigEndian16(message + cursor);
        const USHORT recordClass = ReadBigEndian16(message + cursor + 2) & 0x7fffU;
        const size_t dataBytes = ReadBigEndian16(message + cursor + 8);
        cursor += 10;
        if (dataBytes > bytes - cursor) {
            return;
        }
        if (isAirDrop && type == 12 && recordClass == 1 && dataBytes > 1) {
            size_t instance = cursor;
            BOOLEAN unused;
            if (ReadMdnsName(message, bytes, &instance, &unused) && instance == cursor + dataBytes) {
                ++airDropAnswers;
            }
        }
        cursor += dataBytes;
    }
    if (SelectedPeer) {
        ++Result->MdnsResponses;
        Result->MdnsAirDropAnswers += airDropAnswers;
        if (records != 0) {
            RememberMdnsResponse(Result, message, bytes, Source);
        }
    } else {
        ++Result->MdnsOtherResponses;
        Result->MdnsOtherAirDropAnswers += airDropAnswers;
    }
}

static void LinkLocalFromMac(const UCHAR *Mac, UCHAR *Address);

static ULONG ReadBigEndian32(const UCHAR *Bytes)
{
    return ((ULONG)ReadBigEndian16(Bytes) << 16) | ReadBigEndian16(Bytes + 2);
}

static void CheckTcpProbeReply(MT7922_MCU_PROBE_INFO *Result, const UCHAR *Ip, size_t Payload)
{
    const UCHAR *tcp = Ip + 40;
    if (Result->TcpSynSent == 0 || Payload < 20 || Result->TcpRefused || Result->TcpSynAck ||
        ReadBigEndian16(tcp) != Result->TcpPort || ReadBigEndian16(tcp + 2) != Result->TcpSourcePort ||
        ReadBigEndian32(tcp + 8) != Result->TcpInitialSequence + 1U) {
        return;
    }
    const size_t headerBytes = (tcp[12] >> 4) * 4U;
    if (headerBytes < 20 || headerBytes > Payload ||
        Ipv6TransportChecksum(Ip + 8, Ip + 24, 6, tcp, (ULONG)Payload) != 0) {
        return;
    }
    UCHAR peer[16];
    LinkLocalFromMac(Result->DataPeer, peer);
    if (RtlCompareMemory(Ip + 8, peer, sizeof(peer)) != sizeof(peer)) {
        return;
    }
    const UCHAR flags = tcp[13] & (AWDL_TCP_ACK | AWDL_TCP_RST | AWDL_TCP_SYN);
    if (flags == (AWDL_TCP_ACK | AWDL_TCP_SYN)) {
        Result->TcpSynAck = 1;
    } else if (flags == (AWDL_TCP_ACK | AWDL_TCP_RST)) {
        Result->TcpRefused = 1;
    }
}

static void CountIpv6Reply(MT7922_MCU_PROBE_INFO *Result, const UCHAR *Body,
    size_t BodyBytes, BOOLEAN SelectedPeer, AWDL_SYNC_REF *Ref, MT7922_AWDL_WATCH_DIRECTION *Watch,
    ULONGLONG Arrival)
{
    size_t ipBytes;
    BOOLEAN wrapped;
    const UCHAR *ip = AwdlMsduToIp(Body, BodyBytes, &ipBytes, &wrapped);
    if (ip == NULL || (ip[0] >> 4) != 6) {
        return;
    }
    const size_t payload = ReadBigEndian16(ip + 4);
    if (payload > ipBytes - 40) {
        return;
    }
    if (Watch != NULL) {
        ++Watch->Ipv6Packets;
        if (ip[6] == 6 && payload >= 20) {
            const UCHAR *tcp = ip + 40;
            const size_t header = (tcp[12] >> 4) * 4U;
            if (header >= 20 && header <= payload &&
                Ipv6TransportChecksum(ip + 8, ip + 24, 6, tcp, (ULONG)payload) == 0) {
                const UCHAR flags = tcp[13] & (AWDL_TCP_SYN | AWDL_TCP_ACK);
                Watch->TcpSyn += flags == AWDL_TCP_SYN;
                Watch->TcpSynAck += flags == (AWDL_TCP_SYN | AWDL_TCP_ACK);
                Watch->TcpPayload += payload > header;
            }
        }
        return;
    }
    if (SelectedPeer) {
        ++Result->PeerIpv6Frames;
    }
    if (wrapped) {
        ++Result->WrappedIpv6Packets;
    }
    UCHAR local[16];
    LinkLocalFromMac(Result->NicMac, local);
    const BOOLEAN toUs = RtlCompareMemory(ip + 24, local, sizeof(local)) == sizeof(local);
    if (toUs) {
        ++Result->Ipv6PacketsToUs;
    }
    MT7922_AWDL_LINK *link = Ref->LinkOnline ? Ref->Link : NULL;
    const size_t packetBytes = payload + 40;
    if (link != NULL && SelectedPeer && packetBytes <= Mt7922AwdlLinkMtu(link) &&
        (toUs || (link->AnnounceMdns && AwdlIsMdnsMulticast(ip, packetBytes) &&
            Ipv6TransportChecksum(ip + 8, ip + 24, 17, ip + 40, (ULONG)payload) == 0))) {
        if (link->AnnounceMdns && toUs && ip[6] == 6 && payload >= 20 &&
            ReadBigEndian16(ip + 42) == link->ReceivePort) {
            const size_t tcpHeader = (ip[52] >> 4) * 4U;
            UCHAR peer[16];
            LinkLocalFromMac(Ref->Peer, peer);
            if (tcpHeader >= 20 && tcpHeader < payload &&
                RtlCompareMemory(ip + 8, peer, sizeof(peer)) == sizeof(peer)) {
                const ULONGLONG arrival = Arrival != 0 ? Arrival : AwdlHostTime();
                if (arrival > Ref->LocalAtUploadData) {
                    Ref->LocalAtUploadData = arrival;
                }
            }
        }
        link->Receive(link->Context, ip, (ULONG)packetBytes);
    }
    if (SelectedPeer && toUs && ip[6] == 6) {
        CheckTcpProbeReply(Result, ip, payload);
    } else if (SelectedPeer && toUs && payload >= 8 && ip[6] == 58 &&
        ip[40] == 129 && ip[41] == 0 && ReadBigEndian16(ip + 44) == AWDL_ECHO_ID) {
        UCHAR peer[16];
        LinkLocalFromMac(Result->DataPeer, peer);
        const USHORT sequence = ReadBigEndian16(ip + 46);
        const bool matches = Ref->EchoSequence != 0 ? sequence == Ref->EchoSequence
            : sequence != 0 && sequence <= Result->PingSent;
        if (matches && RtlCompareMemory(ip + 8, peer, sizeof(peer)) == sizeof(peer) &&
            Ipv6TransportChecksum(ip + 8, ip + 24, 58, ip + 40, (ULONG)payload) == 0) {
            ++Result->PingReplies;
            Result->PingLastTtl = ip[7];
        }
    } else if (ip[6] == 17 && payload >= 8 && ReadBigEndian16(ip + 46) != 0 &&
        Ipv6TransportChecksum(ip + 8, ip + 24, 17, ip + 40, (ULONG)payload) == 0) {
        CountMdnsResponse(Result, ip + 40, payload, SelectedPeer, ip + 8);
    }
}

#define CONNAC2_AMSDU_LAST 1U
#define CONNAC2_AMSDU_FIRST 3U

static void CountPeerRxRadio(MT7922_AWDL_LINK *Link, const MT7922_RX_FRAME *Frame)
{
    MT7922_AWDL_LINK *link = Link;
    if (!Frame->PhyPresent) {
        ++link->RxPhyUnknown;
    } else if (Frame->PhyMode <= 1) {
        ++link->RxPhyLegacy; /* CCK or OFDM. */
    } else if (Frame->PhyMode <= 3) {
        ++link->RxPhyHt;
    } else if (Frame->PhyMode == 4) {
        ++link->RxPhyVht;
        link->RxPhyVht80 += Frame->PhyWidth == 2;
    } else if (Frame->PhyMode >= 8 && Frame->PhyMode <= 11) {
        ++link->HeReport.Frames;
        link->HeReport.Frames80 += Frame->PhyWidth == 2;
        link->HeReport.Mcs10 += Frame->PhyMcs == 10;
        link->HeReport.Mcs11 += Frame->PhyMcs == 11;
    } else {
        ++link->RxPhyOther;
    }

    if (!Frame->Ampdu) {
        link->RxAmpduRun = 0;
        return;
    }
    ++link->RxAmpduFrames;
    if (link->RxAmpduRun == 0 || link->RxAmpduTimestamp != Frame->AmpduTimestamp) {
        ++link->RxAmpduBursts;
        link->RxAmpduTimestamp = Frame->AmpduTimestamp;
        link->RxAmpduRun = 0;
    }
    ++link->RxAmpduRun;
    if (link->RxAmpduRun > link->RxAmpduMax) {
        link->RxAmpduMax = link->RxAmpduRun;
    }
}

typedef struct {
    MT7922_MCU_PROBE_INFO *Result;
    AWDL_SYNC_REF *Ref;
} AWDL_BA_DELIVERY;

#define AMSDU_SUBFRAME_HEADER 14U
#define BA_PAYLOAD_HEADER (sizeof(ULONGLONG) + 1U)

static size_t AmsduSubframeBytes(const UCHAR *Body, size_t Length)
{
    if (Length < AMSDU_SUBFRAME_HEADER) {
        return 0;
    }
    const size_t bytes = AMSDU_SUBFRAME_HEADER + ((size_t)Body[12] << 8) + Body[13];
    return bytes > AMSDU_SUBFRAME_HEADER && bytes <= Length ? bytes : 0;
}

static size_t AmsduPaddedBytes(size_t Bytes)
{
    return (Bytes + 3U) & ~(size_t)3U;
}

static bool ValidAmsduBody(const UCHAR *Body, size_t Length)
{
    /* A single MSDU with a forged A-MSDU bit must not become an aggregate. */
    static const UCHAR snap[] = {0xaa, 0xaa, 3};
    if (Length < AMSDU_SUBFRAME_HEADER || RtlCompareMemory(Body, snap, sizeof(snap)) == sizeof(snap)) {
        return false;
    }
    for (;;) {
        const size_t bytes = AmsduSubframeBytes(Body, Length);
        if (bytes == 0 || bytes == Length) {
            return bytes != 0;
        }
        const size_t padded = AmsduPaddedBytes(bytes);
        if (padded >= Length) {
            return false;
        }
        Body += padded;
        Length -= padded;
    }
}

static ULONG DeliverAmsduBody(MT7922_MCU_PROBE_INFO *Result, const UCHAR *Body, size_t Length,
    BOOLEAN SelectedPeer, AWDL_SYNC_REF *Ref, MT7922_AWDL_WATCH_DIRECTION *Watch,
    ULONGLONG Arrival, BOOLEAN SingleSubframe)
{
    ULONG subframes = 0;
    while (Length >= AMSDU_SUBFRAME_HEADER) {
        const size_t bytes = AmsduSubframeBytes(Body, Length);
        if (bytes == 0) {
            break;
        }
        CountIpv6Reply(Result, Body + AMSDU_SUBFRAME_HEADER, bytes - AMSDU_SUBFRAME_HEADER,
            SelectedPeer, Ref, Watch, Arrival);
        ++subframes;
        const size_t padded = AmsduPaddedBytes(bytes);
        if (SingleSubframe || padded >= Length) {
            break;
        }
        Body += padded;
        Length -= padded;
    }
    return subframes;
}

static void DeliverReorderedBody(void *Context, const uint8_t *Bytes, int Length)
{
    AWDL_BA_DELIVERY *delivery = Context;
    ULONGLONG arrival;
    RtlCopyMemory(&arrival, Bytes, sizeof(arrival));
    const UCHAR *body = Bytes + BA_PAYLOAD_HEADER;
    const size_t bodyBytes = (size_t)Length - BA_PAYLOAD_HEADER;
    if (Bytes[sizeof(arrival)] != 0) {
        MT7922_AWDL_SHARED_AMSDU_REPORT *report = &delivery->Ref->Link->AmsduReport;
        ++report->Frames;
        report->Subframes += DeliverAmsduBody(delivery->Result, body, bodyBytes,
            TRUE, delivery->Ref, NULL, arrival, FALSE);
        return;
    }
    CountIpv6Reply(delivery->Result, body, bodyBytes, TRUE, delivery->Ref, NULL, arrival);
}

static void ReorderBody(MT7922_MCU_PROBE_INFO *Result, AWDL_SYNC_REF *Ref,
    const UCHAR *Body, size_t Length, USHORT Sequence, ULONGLONG Arrival, BOOLEAN Amsdu)
{
    MT7922_AWDL_LINK *link = Ref->Link;
    AWDL_BA_DELIVERY delivery = {Result, Ref};
    RtlCopyMemory(link->ReorderInput, &Arrival, sizeof(Arrival));
    link->ReorderInput[sizeof(Arrival)] = Amsdu;
    RtlCopyMemory(link->ReorderInput + BA_PAYLOAD_HEADER, Body, Length);
    ba_reorder_rx(&link->Reorder, Sequence, link->ReorderInput,
        (int)(BA_PAYLOAD_HEADER + Length), Arrival / 10U, DeliverReorderedBody, &delivery);
}

static void DiscardSplitAmsdu(MT7922_AWDL_LINK *Link)
{
    if (Link->AmsduBodyBytes != 0) {
        ++Link->AmsduReport.Discarded;
        Link->AmsduBodyBytes = 0;
    }
}

static void ReceiveSplitAmsdu(MT7922_MCU_PROBE_INFO *Result, AWDL_SYNC_REF *Ref,
    const UCHAR *Body, size_t Length, UCHAR Part, USHORT Sequence, ULONGLONG Now)
{
    MT7922_AWDL_LINK *link = Ref->Link;
    ++link->AmsduReport.SplitRecords;
    const size_t bytes = AmsduSubframeBytes(Body, Length);
    if (Part == CONNAC2_AMSDU_FIRST) {
        DiscardSplitAmsdu(link);
        link->AmsduSequence = Sequence;
        link->AmsduArrival = Now;
    } else if (link->AmsduBodyBytes == 0 || link->AmsduSequence != Sequence) {
        ++link->AmsduReport.Discarded;
        return;
    }
    const size_t offset = AmsduPaddedBytes(link->AmsduBodyBytes);
    if (bytes == 0 || offset > sizeof(link->AmsduBody) || bytes > sizeof(link->AmsduBody) - offset) {
        ++link->AmsduReport.Discarded;
        link->AmsduBodyBytes = 0;
        return;
    }
    if (offset != link->AmsduBodyBytes) {
        RtlZeroMemory(link->AmsduBody + link->AmsduBodyBytes, offset - link->AmsduBodyBytes);
    }
    RtlCopyMemory(link->AmsduBody + offset, Body, bytes);
    link->AmsduBodyBytes = offset + bytes;
    if (Part == CONNAC2_AMSDU_LAST) {
        if (ValidAmsduBody(link->AmsduBody, link->AmsduBodyBytes)) {
            ReorderBody(Result, Ref, link->AmsduBody, link->AmsduBodyBytes,
                Sequence, link->AmsduArrival, TRUE);
        } else {
            ++link->AmsduReport.Discarded;
        }
        link->AmsduBodyBytes = 0;
    }
}

static void CollectBaReorderStats(MT7922_AWDL_LINK *Link)
{
    Link->BaReorderDirect += Link->Reorder.in_order;
    Link->BaReorderReleased += Link->Reorder.reordered;
    Link->BaReorderDuplicates += Link->Reorder.duplicates;
    Link->BaReorderHoles += Link->Reorder.holes_skipped;
}

static void StopBaReorder(MT7922_MCU_PROBE_INFO *Result, AWDL_SYNC_REF *Ref)
{
    AWDL_BA_DELIVERY delivery = {Result, Ref};
    ba_reorder_stop(&Ref->Link->Reorder, DeliverReorderedBody, &delivery);
    CollectBaReorderStats(Ref->Link);
    DiscardSplitAmsdu(Ref->Link);
    Ref->Link->AmsduNegotiated = FALSE;
    Ref->Link->AmsduRxRecordBytes = 0;
}

static void StartBaReorder(MT7922_MCU_PROBE_INFO *Result, AWDL_SYNC_REF *Ref)
{
    MT7922_AWDL_LINK *link = Ref->Link;
    if (link == NULL || !link->ReorderRequested) {
        return;
    }
    const UCHAR tid = (Ref->Addba.Params >> 2) & 0xfU;
    const unsigned size = Ref->Addba.Params >> 6;
    const BOOLEAN amsdu = link->ReceiveAmsdu && (Ref->Addba.Params & 1U) != 0;
    if (link->Reorder.active) {
        if (link->ReorderTid == tid && link->ReorderToken == Ref->Addba.Token &&
            link->ReorderStart == Ref->Addba.StartingSequence && link->AmsduNegotiated == amsdu &&
            link->Reorder.size == (size != 0 && size <= BA_REORDER_SLOTS ? size : BA_REORDER_SLOTS)) {
            return;
        }
        StopBaReorder(Result, Ref);
    }
    ba_reorder_start(&link->Reorder, Ref->Addba.StartingSequence, size);
    link->ReorderTid = tid;
    link->ReorderToken = Ref->Addba.Token;
    link->ReorderStart = Ref->Addba.StartingSequence;
    ++link->BaReorderSessions;
    link->AmsduNegotiated = amsdu;
    link->AmsduReport.Negotiations += amsdu;
}

static bool HandleBaReorderControl(MT7922_MCU_PROBE_INFO *Result,
    const MT7922_RX_FRAME *Frame, AWDL_SYNC_REF *Ref)
{
    MT7922_AWDL_LINK *link = Ref->Link;
    const UCHAR *frame = Frame->Bytes;
    if (link == NULL || !link->Reorder.active || Frame->Length < 20 ||
        (frame[0] != 0x84 && frame[0] != 0xd0) || (frame[1] & 0x40U) != 0 ||
        RtlCompareMemory(frame + 4, Result->NicMac, 6) != 6 ||
        RtlCompareMemory(frame + 10, Ref->Peer, 6) != 6) {
        return false;
    }
    if (frame[0] == 0x84) {
        const USHORT control = (USHORT)(frame[16] | (frame[17] << 8));
        if ((control & 0x0006U) == 0x0004U && (control >> 12) == link->ReorderTid) {
            const USHORT start = (USHORT)((frame[18] | (frame[19] << 8)) >> 4);
            AWDL_BA_DELIVERY delivery = {Result, Ref};
            ba_reorder_bar(&link->Reorder, start, DeliverReorderedBody, &delivery);
            ++link->BaReorderBars;
        }
    } else if (Frame->Length >= 30 && frame[24] == 3 && frame[25] == 2) {
        const USHORT params = (USHORT)(frame[26] | (frame[27] << 8));
        if ((params & 0x0800U) != 0 && (params >> 12) == link->ReorderTid) {
            StopBaReorder(Result, Ref);
            return true;
        }
    }
    return false;
}

static bool ReadAddbaRequest(MT7922_MCU_PROBE_INFO *Result, const MT7922_RX_FRAME *Frame,
    const UCHAR *Peer, MT7922_AWDL_ADDBA *Addba)
{
    const UCHAR *bytes = Frame->Bytes;
    if (Frame->Length < 33 || bytes[0] != 0xd0 || (bytes[1] & 0xc3U) != 0 ||
        bytes[24] != 3 || bytes[25] != 0 || RtlCompareMemory(bytes + 4, Result->NicMac, 6) != 6 ||
        RtlCompareMemory(bytes + 10, Peer, 6) != 6) {
        return false;
    }
    ++Result->AddbaRequests;
    RtlCopyMemory(Result->AddbaPeerAddr, Peer, 6);
    const UCHAR token = bytes[26];
    const USHORT params = (USHORT)(bytes[27] | (bytes[28] << 8));
    const USHORT timeout = (USHORT)(bytes[29] | (bytes[30] << 8));
    const USHORT start = (USHORT)((bytes[31] | (bytes[32] << 8)) >> 4);
    if (Addba->Params != params || Addba->StartingSequence != start) {
        Addba->HardwareReady = FALSE;
    }
    if (!Addba->Seen || Addba->Token != token || Addba->Params != params ||
        Addba->Timeout != timeout || Addba->StartingSequence != start) {
        Addba->Attempts = 0;
        Addba->Seen = TRUE;
    }
    Addba->Token = token;
    Addba->Params = params;
    Addba->Timeout = timeout;
    Addba->StartingSequence = start;
    Addba->Active = TRUE;
    return true;
}

static void PollBaReorder(MT7922_MCU_PROBE_INFO *Result, AWDL_SYNC_REF *Ref, ULONGLONG Now)
{
    MT7922_AWDL_LINK *link = Ref->Link;
    if (link != NULL && link->AmsduBodyBytes != 0 && Now - link->AmsduArrival > 1000000ULL) {
        DiscardSplitAmsdu(link);
    }
    if (link != NULL && link->Reorder.active && link->Reorder.stored != 0) {
        AWDL_BA_DELIVERY delivery = {Result, Ref};
        const unsigned holes = link->Reorder.holes_skipped;
        ba_reorder_timeout(&link->Reorder, Now / 10U, DeliverReorderedBody, &delivery);
        link->BaReorderTimeouts += link->Reorder.holes_skipped != holes;
    }
}

static void CheckIpv6Reply(MT7922_MCU_PROBE_INFO *Result, const MT7922_RX_FRAME *Frame,
    AWDL_SYNC_REF *Ref)
{
    static const UCHAR bssid[] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    const UCHAR *Bytes = Frame->Bytes;
    const size_t Length = Frame->Length;
    if (Length < 24 || RtlCompareMemory(Bytes + 16, bssid, 6) != 6 ||
        RtlCompareMemory(Bytes + 10, Result->NicMac, 6) == 6) {
        return;
    }
    const USHORT fc = (USHORT)(Bytes[0] | (Bytes[1] << 8));
    if (((fc >> 2) & 3U) != 2U || (fc & 0x0300U) != 0) {
        return;
    }
    const ULONG subtype = (fc >> 4) & 0xfU;
    if (subtype != 0 && subtype != 8) {
        return; /* Only data and QoS data can carry the AWDL payload. */
    }
    MT7922_AWDL_WATCH_DIRECTION *watch = NULL;
    if (Ref->Watch != NULL) {
        const UCHAR *peers = Ref->Watch->Peers;
        for (ULONG index = 0; index < 2; ++index) {
            if (RtlCompareMemory(Bytes + 10, peers + index * 6, 6) == 6 &&
                RtlCompareMemory(Bytes + 4, peers + (1U - index) * 6, 6) == 6) {
                watch = &Ref->Watch->Info.From[index];
                ++watch->DataFrames;
                break;
            }
        }
        if (watch == NULL) {
            return;
        }
    }
    const BOOLEAN selectedPeer = RtlCompareMemory(Bytes + 10, Ref->Peer, 6) == 6;
    if (selectedPeer) {
        ++Result->AwdlRxDataSelected;
    } else {
        ++Result->AwdlRxDataOther;
    }
    const BOOLEAN split = Frame->AmsduPart != 0;
    const BOOLEAN toUs = RtlCompareMemory(Bytes + 4, Result->NicMac, 6) == 6;
    if (toUs) {
        ++Result->AwdlRxDataToUs;
        if (selectedPeer && Ref->Link != NULL && Ref->Link->AnnounceMdns &&
            (!split || Frame->AmsduPart == CONNAC2_AMSDU_FIRST)) {
            CountPeerRxRadio(Ref->Link, Frame);
        }
    }
    if (split) {
        ++Result->AwdlRxDataSplit;
    }
    MT7922_AWDL_RX_HEADER *sample = &Result->AwdlRxHeaders[split ? 1 : 0];
    if (sample->SampleBytes == 0) {
        sample->Length = (ULONG)Length;
        sample->AmsduPart = Frame->AmsduPart;
        sample->SampleBytes = (ULONG)(Length < sizeof(sample->Bytes) ? Length : sizeof(sample->Bytes));
        RtlCopyMemory(sample->Bytes, Bytes, sample->SampleBytes);
    }
    if (fc & 0x4000U) {
        return; /* An encrypted frame cannot provide a clear AWDL payload. */
    }

    const BOOLEAN qos = subtype == 8;
    if (!qos) {
        CountIpv6Reply(Result, Bytes + 24, Length - 24, selectedPeer, Ref, watch, 0);
        return;
    }
    if (Length < 26) {
        return;
    }
    const USHORT control = (USHORT)(Bytes[24] | (Bytes[25] << 8));
    size_t header = 26 + ((fc & 0x8000U) != 0 ? 4 : 0);
    if (Length < header) {
        return;
    }
    MT7922_AWDL_LINK *link = Ref->Link;
    const USHORT sequence = (USHORT)(Bytes[22] | (Bytes[23] << 8));
    const BOOLEAN matchingBa = link != NULL && link->Reorder.active && selectedPeer && toUs &&
        (control & 0xfU) == link->ReorderTid && (sequence & 0xfU) == 0 && (fc & 0x0400U) == 0;
    if (!split && (control & 0x0080U) == 0) {
        const size_t bodyBytes = Length - header;
        if (matchingBa && bodyBytes <= RX_BUFFER_BYTES) {
            DiscardSplitAmsdu(link);
            ReorderBody(Result, Ref, Bytes + header, bodyBytes, sequence >> 4, AwdlHostTime(), FALSE);
        } else {
            CountIpv6Reply(Result, Bytes + header, bodyBytes, selectedPeer, Ref, watch, 0);
        }
        return;
    }

    /* Connac2 adds two bytes after the MAC header for hardware-split A-MSDU. */
    header += split ? 2U : 0U;
    if (Length < header) {
        return;
    }
    const UCHAR *body = Bytes + header;
    const size_t bodyBytes = Length - header;
    if (link != NULL && link->ReceiveAmsdu && selectedPeer && toUs) {
        if (!matchingBa || !link->AmsduNegotiated || bodyBytes > MT7922_AWDL_MAX_AMSDU_BYTES) {
            ++link->AmsduReport.Discarded;
        } else if (split) {
            ReceiveSplitAmsdu(Result, Ref, body, bodyBytes, Frame->AmsduPart, sequence >> 4, AwdlHostTime());
        } else {
            DiscardSplitAmsdu(link);
            if (ValidAmsduBody(body, bodyBytes)) {
                ReorderBody(Result, Ref, body, bodyBytes, sequence >> 4, AwdlHostTime(), TRUE);
            } else {
                ++link->AmsduReport.Discarded;
            }
        }
        return;
    }
    DeliverAmsduBody(Result, body, bodyBytes, selectedPeer, Ref, watch, 0, split);
}

static void CollectTxCompletion(USHORT ChipId, const UCHAR *Data, ULONG Length, ULONG Type,
    const MT7922_AWDL_TX *Request, MT7922_TX_ATTEMPT_INFO *Attempt)
{
    if (Type == MT7922_TX_STATUS_PACKET_TYPE) {
        ++Attempt->TxStatusRecords;
        MT7922_TX_REPORT report;
        if (Mt792xReadTxReport(ChipId, Data, Length, Request, &report) == Mt7922TxReportMatched) {
            Attempt->ReportSeen = 1;
            Attempt->AckErrors = report.AckErrors;
            Attempt->ReportRate = report.RateCode;
        }
    } else if (Type == MT7922_TX_RELEASE_PACKET_TYPE) {
        MT7922_TX_RELEASE release;
        if (Mt792xReadTxRelease(ChipId, Data, Length, Request->Token, &release) == Mt7922TxReportMatched) {
            Attempt->ReleaseSeen = 1;
            Attempt->ReleaseStatus = release.Status;
            Attempt->RetryCount = release.Attempts;
        }
    }
}

static void ReadActionTxEvents(MT7922_RX_RING *Ring, const MT7922_AWDL_TX *Request,
    MT7922_TX_ATTEMPT_INFO *Attempt, MT7922_MCU_PROBE_INFO *Result,
    AWDL_SYNC_REF *SyncRef)
{
    for (ULONG i = 0; i < Ring->Count; ++i) {
        const UCHAR *data;
        ULONG control;
        if (!Mt7922PeekRxRing(Ring, &data, &control)) {
            return;
        }
        ULONG length = (control & DESCRIPTOR_LEN0) >> DESCRIPTOR_LEN_SHIFT;
        BOOLEAN last = (control & DESCRIPTOR_LAST0) != 0;
        if (!Ring->DiscardContinuation && last && length <= RX_BUFFER_BYTES &&
            (control & (DESCRIPTOR_DROP | DESCRIPTOR_TO_HOST_A)) == 0) {
            const ULONG type = length >= sizeof(ULONG)
                ? ReadLittleEndian32(data) >> 27 : MT7922_TX_RX_TYPE_BINS;
            RecordRxRecord(Result, data, length, type);
            CollectTxCompletion(Ring->ChipId, data, length, type, Request, Attempt);
            if (SyncRef != NULL && SyncRef->Unicast.Active) {
                CollectTxCompletion(Ring->ChipId, data, length, type, &SyncRef->Unicast.Request,
                    &SyncRef->Unicast.Completion);
            }
            if (SyncRef != NULL && type == 2) {
                MT7922_RX_FRAME frame;
                const MT7922_RX_STATUS frameStatus = Mt792xDecodeRx((USHORT)Ring->ChipId,
                    data, length, &frame);
                if (frameStatus != Mt7922RxRawFrame) {
                    ++Result->NormalRxSkipped;
                    Result->NormalRxFcsErrors += (frame.SkipFlags & MT7922_RX_SKIP_FCS) != 0;
                    Result->NormalRxTranslated += (frame.SkipFlags & MT7922_RX_SKIP_TRANSLATED) != 0;
                    Result->NormalRxAmsduErrors += (frame.SkipFlags & MT7922_RX_SKIP_AMSDU) != 0;
                    Result->NormalRxBadLength += (frame.SkipFlags & MT7922_RX_SKIP_LENGTH) != 0;
                    Result->NormalRxBadGroups += (frame.SkipFlags & MT7922_RX_SKIP_GROUPS) != 0;
                    if (Result->FirstSkippedRxBytes == 0 && frame.SkipFlags == 0) {
                        Result->FirstSkippedRxBytes = length;
                        for (ULONG word = 0; word < RTL_NUMBER_OF(Result->FirstSkippedRx) &&
                                (word + 1) * sizeof(ULONG) <= length; ++word) {
                            Result->FirstSkippedRx[word] = ReadLittleEndian32(data + word * sizeof(ULONG));
                        }
                    }
                } else if (SyncRef->Watch != NULL) {
                    AWDL_ACTION action;
                    const AWDL_ACTION_KIND kind = AwdlDecodeAction(frame.Bytes, frame.Length, &action);
                    if (kind == AwdlPsf || kind == AwdlMif) {
                        for (ULONG index = 0; index < 2; ++index) {
                            if (RtlCompareMemory(action.Sender, SyncRef->Watch->Peers + index * 6, 6) == 6) {
                                ++SyncRef->Watch->Info.From[index].SyncFrames;
                            }
                        }
                    } else {
                        CheckIpv6Reply(Result, &frame, SyncRef);
                    }
                } else {
                    Result->NormalRxRawTranslationError += frame.HeaderTranslationError != 0;
                    if (frame.Length >= 10) {
                        const USHORT fc = (USHORT)(frame.Bytes[0] | (frame.Bytes[1] << 8));
                        if (((fc >> 2) & 3U) == 1U) {
                            ++Result->ControlFrames;
                            if (((fc >> 4) & 0xfU) == 13U
                                && RtlCompareMemory(frame.Bytes + 4, Result->NicMac, 6) == 6) {
                                ++Result->AcksToUs;
                            }
                        } else if (frame.Length >= 16) {
                            if (RtlCompareMemory(frame.Bytes + 10, Result->NicMac, 6) == 6) {
                                ++Result->SelfFrames;
                            } else {
                                if (RtlCompareMemory(frame.Bytes + 4, Result->NicMac, 6) == 6) {
                                    ++Result->PeerAddressed;
                                    if (Result->PeerSamples < RTL_NUMBER_OF(Result->PeerSample)) {
                                        ULONG bytes = (ULONG)frame.Length;
                                        if (bytes > sizeof(Result->PeerSample[0])) {
                                            bytes = sizeof(Result->PeerSample[0]);
                                        }
                                        RtlCopyMemory(Result->PeerSample[Result->PeerSamples],
                                            frame.Bytes, bytes);
                                        ++Result->PeerSamples;
                                    }
                                    const USHORT ofc =
                                        (USHORT)(frame.Bytes[0] | (frame.Bytes[1] << 8));
                                    if (((ofc >> 2) & 3U) == 2U && ((ofc >> 4) & 8U) != 0) {
                                        ++Result->PeerQos;
                                        if (Result->PeerQosSampleBytes == 0) {
                                            ULONG bytes = (ULONG)frame.Length;
                                            if (bytes > sizeof(Result->PeerQosSample)) {
                                                bytes = sizeof(Result->PeerQosSample);
                                            }
                                            RtlCopyMemory(Result->PeerQosSample,
                                                frame.Bytes, bytes);
                                            Result->PeerQosSampleBytes = bytes;
                                        }
                                    }
                                }
                                for (ULONG b = 16; b + 6 <= frame.Length; ++b) {
                                    if (RtlCompareMemory(frame.Bytes + b, Result->NicMac, 6) == 6) {
                                        ++Result->PeerMentions;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    if (HandleBaReorderControl(Result, &frame, SyncRef)) {
                        SyncRef->Addba.Active = FALSE;
                    }
                    CheckIpv6Reply(Result, &frame, SyncRef);
                    (void)ReadAddbaRequest(Result, &frame, SyncRef->Peer, &SyncRef->Addba);
                    AWDL_ACTION action;
                    AWDL_ACTION_KIND kind = AwdlDecodeAction(frame.Bytes, frame.Length, &action);
                    if ((kind == AwdlPsf || kind == AwdlMif) && UpdateSyncRef(SyncRef, &action, Result)) {
                        ++Result->SyncPeerFrames;
                    }
                }
            }
        }
        Ring->DiscardContinuation = !last;
        Mt7922RecycleRxRing(Ring);
    }
}

static BOOLEAN BuildAwdlTxwiForChip(UCHAR *Txwi, const MT7922_AWDL_TX *Request,
    const UCHAR *Frame, ULONG FrameBytes, ULONG ChipId)
{
    MT7922_AWDL_TX chipRequest = *Request;
    chipRequest.ChipId = (USHORT)ChipId;
    return (BOOLEAN)Mt7922BuildAwdlTxwi(Txwi, &chipRequest, Frame, FrameBytes);
}

static NTSTATUS SendProbeFrame(MCU_RINGS *Rings, AWDL_PROBE_FRAME *Probe,
    MT7922_MCU_PROBE_INFO *Result, ULONG Index)
{
    const MT7922_AWDL_TX request = {
        Rings->DmaAddress + ACTION_FRAME_OFFSET, 0, (USHORT)(ACTION_TOKEN + Index),
        (UCHAR)(ACTION_PACKET_ID + Index), 0, 1, Rings->AwdlTxRateCode
    };
    UCHAR txwi[MT7922_TXWI_BYTES];
    Probe->Bytes[22] = (UCHAR)((Index + 1) << 4);
    Result->Stage = MT7922_MCU_TX_SUBMIT;
    if (!BuildAwdlTxwiForChip(txwi, &request, Probe->Bytes, Probe->Length, Rings->ChipId)) {
        return STATUS_DATA_ERROR;
    }
    RtlCopyMemory(Rings->Buffer + ACTION_FRAME_OFFSET, Probe->Bytes, Probe->Length);
    NTSTATUS status = SendPacket(Rings, &Rings->ActionTx, txwi, sizeof(txwi), Result);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    Result->Stage = MT7922_MCU_TX_REPORT;
    MT7922_TX_ATTEMPT_INFO *attempt = &Result->TxProbe[Index];
    ULONGLONG start = KeQueryInterruptTime();
    ULONGLONG releasedAt = 0;
    do {
        ReadActionTxEvents(&Rings->RomEvents, &request, attempt, Result, NULL);
        ReadActionTxEvents(&Rings->RuntimeEvents, &request, attempt, Result, NULL);
        ReadActionTxEvents(&Rings->DataRx, &request, attempt, Result, NULL);
        if (attempt->ReleaseSeen) {
            if (attempt->ReleaseStatus != 0) {
                return STATUS_DEVICE_PROTOCOL_ERROR;
            }
            if (attempt->ReportSeen) {
                return STATUS_SUCCESS;
            }
            if (releasedAt == 0) {
                releasedAt = KeQueryInterruptTime();
            } else if (KeQueryInterruptTime() - releasedAt
                >= (ULONGLONG)MT7922_TX_STATUS_GRACE_MS * 10000) {
                /* Broadcast releases do not always carry an ACK-bearing report. */
                return STATUS_SUCCESS;
            }
        }
        WaitMilliseconds(1);
    } while (KeQueryInterruptTime() - start < 1000ULL * 10000);
    return STATUS_IO_TIMEOUT;
}

static void WaitForAwdlPoll(const MCU_RINGS *Rings, const AWDL_SYNC_REF *Ref)
{
    const ULONGLONG start = AwdlHostTime();
    if (Rings->AwdlPollTimer == NULL || Ref->LocalAtUploadData == 0 ||
        start - Ref->LocalAtUploadData > 1500ULL * 10000) {
        WaitMilliseconds(1);
        return;
    }

    MT7922_AWDL_LINK *link = Ref->Link;
    const BOOLEAN shortPoll = link->ShortPollRequested &&
        start - Ref->LocalAtUploadData <= 12ULL * TU_TICKS &&
        InterlockedCompareExchange(&link->ShortPollActive, 0, 0) != 0;
    ExSetTimer(Rings->AwdlPollTimer, shortPoll ? -2500LL : -10000LL, 0, NULL);
    KeWaitForSingleObject(Rings->AwdlPollTimer, Executive, KernelMode, FALSE, NULL);
    const ULONGLONG elapsed = AwdlHostTime() - start;
    ++link->UploadFastPolls;
    link->UploadLatePolls += elapsed >= 4ULL * TU_TICKS;
    if (shortPoll) {
        ++link->UploadShortPolls;
        link->UploadShortPollTimeUs += (ULONG)(elapsed / 10U);
    }
}

static NTSTATUS SendSyncFrame(MCU_RINGS *Rings, MT7922_MCU_PROBE_INFO *Result,
    AWDL_SYNC_REF *SyncRef, const UCHAR *Frame, ULONG FrameBytes, USHORT Token)
{
    const MT7922_AWDL_TX request = {
        Rings->DmaAddress + ACTION_FRAME_OFFSET, 0, Token,
        (UCHAR)(ACTION_PACKET_ID + (Token & 1U)), 0, 1, Rings->AwdlTxRateCode
    };
    UCHAR txwi[MT7922_TXWI_BYTES];
    if (!BuildAwdlTxwiForChip(txwi, &request, Frame, FrameBytes, Rings->ChipId)) {
        return STATUS_DATA_ERROR;
    }
    RtlCopyMemory(Rings->Buffer + ACTION_FRAME_OFFSET, Frame, FrameBytes);
    NTSTATUS status = SendPacket(Rings, &Rings->ActionTx, txwi, sizeof(txwi), Result);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    MT7922_TX_ATTEMPT_INFO attempt = {0};
    ULONGLONG start = KeQueryInterruptTime();
    do {
        ReadActionTxEvents(&Rings->RomEvents, &request, &attempt, Result, SyncRef);
        ReadActionTxEvents(&Rings->RuntimeEvents, &request, &attempt, Result, SyncRef);
        ReadActionTxEvents(&Rings->DataRx, &request, &attempt, Result, SyncRef);
        if (attempt.ReleaseSeen) {
            return attempt.ReleaseStatus == 0 ? STATUS_SUCCESS : STATUS_DEVICE_PROTOCOL_ERROR;
        }
        WaitForAwdlPoll(Rings, SyncRef);
    } while (KeQueryInterruptTime() - start < (ULONGLONG)MT7922_TX_SYNC_RELEASE_MS * 10000);
    return STATUS_IO_TIMEOUT;
}

static void WriteLittleEndian16(UCHAR *Bytes, USHORT Value)
{
    Bytes[0] = (UCHAR)Value;
    Bytes[1] = (UCHAR)(Value >> 8);
}

static void WriteLittleEndian32(UCHAR *Bytes, ULONG Value)
{
    WriteLittleEndian16(Bytes, (USHORT)Value);
    WriteLittleEndian16(Bytes + 2, (USHORT)(Value >> 16));
}

static void LinkLocalFromMac(const UCHAR *Mac, UCHAR *Address)
{
    RtlZeroMemory(Address, 16);
    Address[0] = 0xfe;
    Address[1] = 0x80;
    Address[8] = (UCHAR)(Mac[0] ^ 0x02);
    Address[9] = Mac[1];
    Address[10] = Mac[2];
    Address[11] = 0xff;
    Address[12] = 0xfe;
    Address[13] = Mac[3];
    Address[14] = Mac[4];
    Address[15] = Mac[5];
}

static ULONG BuildIcmp6Echo(UCHAR *Packet, ULONG Capacity, const UCHAR *SourceMac,
    const UCHAR *DestMac, USHORT Sequence)
{
    const ULONG icmpBytes = 16;
    const ULONG ipBytes = 40 + icmpBytes;
    if (Capacity < ipBytes) {
        return 0;
    }
    RtlZeroMemory(Packet, ipBytes);
    Packet[0] = 0x60;
    Packet[5] = (UCHAR)icmpBytes;
    Packet[6] = 58;
    Packet[7] = 255;
    LinkLocalFromMac(SourceMac, Packet + 8);
    LinkLocalFromMac(DestMac, Packet + 24);
    UCHAR *icmp = Packet + 40;
    icmp[0] = 128;
    icmp[4] = AWDL_ECHO_ID >> 8;
    icmp[5] = AWDL_ECHO_ID & 0xffU;
    icmp[6] = (UCHAR)(Sequence >> 8);
    icmp[7] = (UCHAR)Sequence;
    for (ULONG i = 8; i < icmpBytes; ++i) {
        icmp[i] = (UCHAR)i;
    }
    const USHORT checksum = Ipv6TransportChecksum(Packet + 8, Packet + 24, 58, icmp, icmpBytes);
    icmp[2] = (UCHAR)(checksum >> 8);
    icmp[3] = (UCHAR)checksum;
    return ipBytes;
}

static ULONG BuildTcpProbe(UCHAR *Packet, ULONG Capacity, const MT7922_MCU_PROBE_INFO *Result, BOOLEAN Reset)
{
    const ULONG tcpBytes = 20;
    const ULONG ipBytes = 40 + tcpBytes;
    if (Capacity < ipBytes) {
        return 0;
    }
    RtlZeroMemory(Packet, ipBytes);
    Packet[0] = 0x60;
    Packet[5] = (UCHAR)tcpBytes;
    Packet[6] = 6;
    Packet[7] = 64;
    LinkLocalFromMac(Result->NicMac, Packet + 8);
    LinkLocalFromMac(Result->DataPeer, Packet + 24);
    UCHAR *tcp = Packet + 40;
    tcp[0] = (UCHAR)(Result->TcpSourcePort >> 8);
    tcp[1] = (UCHAR)Result->TcpSourcePort;
    tcp[2] = (UCHAR)(Result->TcpPort >> 8);
    tcp[3] = (UCHAR)Result->TcpPort;
    const ULONG sequence = Result->TcpInitialSequence + (Reset ? 1U : 0U);
    tcp[4] = (UCHAR)(sequence >> 24);
    tcp[5] = (UCHAR)(sequence >> 16);
    tcp[6] = (UCHAR)(sequence >> 8);
    tcp[7] = (UCHAR)sequence;
    tcp[12] = 5U << 4;
    tcp[13] = Reset ? AWDL_TCP_RST : AWDL_TCP_SYN;
    tcp[14] = Reset ? 0 : 0x10;
    const USHORT checksum = Ipv6TransportChecksum(Packet + 8, Packet + 24, 6, tcp, tcpBytes);
    tcp[16] = (UCHAR)(checksum >> 8);
    tcp[17] = (UCHAR)checksum;
    return ipBytes;
}

static ULONG BuildMdnsQuery(UCHAR *Packet, ULONG Capacity, const UCHAR *SourceMac,
    const UCHAR *DestMac)
{
    const ULONG udpBytes = 8 + DNS_HEADER_BYTES + sizeof(AirdropDnsName) + 4;
    const ULONG ipBytes = 40 + udpBytes;
    if (Capacity < ipBytes) {
        return 0;
    }
    RtlZeroMemory(Packet, ipBytes);
    Packet[0] = 0x60;
    Packet[5] = (UCHAR)udpBytes;
    Packet[6] = 17;
    Packet[7] = 255;
    LinkLocalFromMac(SourceMac, Packet + 8);
    if (DestMac == NULL) {
        RtlCopyMemory(Packet + 24, AwdlMdnsAddress, sizeof(AwdlMdnsAddress));
    } else {
        LinkLocalFromMac(DestMac, Packet + 24);
    }

    UCHAR *udp = Packet + 40;
    udp[0] = udp[2] = AWDL_MDNS_PORT >> 8;
    udp[1] = udp[3] = AWDL_MDNS_PORT & 0xffU;
    udp[5] = (UCHAR)udpBytes;
    UCHAR *dns = udp + 8;
    dns[5] = 1; /* One question. */
    RtlCopyMemory(dns + DNS_HEADER_BYTES, AirdropDnsName, sizeof(AirdropDnsName));
    UCHAR *question = dns + DNS_HEADER_BYTES + sizeof(AirdropDnsName);
    question[1] = 12; /* PTR. */
    question[2] = 0x80; /* Unicast reply requested; multicast replies are also accepted. */
    question[3] = 1; /* IN. */
    USHORT checksum = Ipv6TransportChecksum(Packet + 8, Packet + 24, 17, udp, udpBytes);
    if (checksum == 0) {
        checksum = 0xffff;
    }
    udp[6] = (UCHAR)(checksum >> 8);
    udp[7] = (UCHAR)checksum;
    return ipBytes;
}

static ULONG AppendPeerWtbl(UCHAR *payload, ULONG off, const UCHAR *Peer,
    USHORT Wcid, USHORT Aid, bool Station, bool Group)
{
    /* STA_REC_WTBL container with a reset-and-set request. */
    const ULONG wtblStart = off;
    WriteLittleEndian16(payload + off, 13);
    off += 4;
    payload[off] = (UCHAR)Wcid;
    payload[off + 1] = 1; /* WTBL_RESET_AND_SET. */
    off += 8;
    if (Peer == NULL) {
        WriteLittleEndian16(payload + wtblStart + 2, (USHORT)(off - wtblStart));
        return off;
    }
    WriteLittleEndian16(payload + wtblStart + 6, 3); /* Generic, RX, and header translation. */

    /* WTBL_GENERIC: receiver address, MUAR, QoS, and partial AID. */
    WriteLittleEndian16(payload + off, 0);
    WriteLittleEndian16(payload + off + 2, 20);
    RtlCopyMemory(payload + off + 4, Peer, 6);
    payload[off + 10] = Group ? 0x0e : payload[0];
    payload[off + 13] = !Station;
    WriteLittleEndian16(payload + off + 16, Aid);
    off += 20;

    /* WTBL_RX: station receives from its AP with both receiver checks enabled. */
    WriteLittleEndian16(payload + off, 1);
    WriteLittleEndian16(payload + off + 2, 12);
    payload[off + 5] = Station;
    payload[off + 6] = 1;
    payload[off + 7] = 1;
    off += 12;

    /* WTBL_HDR_TRANS: raw 802.11, no receive translation. */
    WriteLittleEndian16(payload + off, 6);
    WriteLittleEndian16(payload + off + 2, 8);
    payload[off + 4] = Station;
    payload[off + 5] = !Station;
    payload[off + 6] = 1;
    off += 8;

    WriteLittleEndian16(payload + wtblStart + 2, (USHORT)(off - wtblStart));
    return off;
}

static void WritePeerBasic(UCHAR *Tlv, const UCHAR *Peer, USHORT Aid,
    bool Station, UCHAR State, bool Newly)
{
    WriteLittleEndian16(Tlv, 0);
    WriteLittleEndian16(Tlv + 2, 20);
    WriteLittleEndian32(Tlv + 4, Station ? 0x00010002 : 0x00010001);
    Tlv[8] = State;
    Tlv[9] = !Station;
    WriteLittleEndian16(Tlv + 10, Aid);
    RtlCopyMemory(Tlv + 12, Peer, 6);
    WriteLittleEndian16(Tlv + 18, Newly ? 3 : 1);
}

static NTSTATUS ProgramPeerRecord(MCU_RINGS *Rings, MT7922_MCU_PROBE_INFO *Result,
    const UCHAR *Peer, USHORT Wcid, USHORT Aid, bool Station, bool Use5Ghz, bool Newly, UCHAR Port, ULONG TimeoutMs)
{
    UCHAR payload[sizeof(((MCU_UNIFIED_COMMAND *)0)->Payload)] = {0};

    /* STA request header with five TLVs. */
    payload[0] = Port;
    payload[1] = (UCHAR)Wcid;
    payload[5] = Port;
    WriteLittleEndian16(payload + 2, 5);
    payload[4] = 1;
    ULONG off = 8;

    /* Infrastructure peer. Station data stays gated until Windows authorizes it. */
    WritePeerBasic(payload + off, Peer, Aid, Station,
        Station ? MT7922_CONN_STATE_CONNECT : MT7922_CONN_STATE_PORT_SECURE, Newly);
    off += 20;

    /* STA_REC_RA: full legacy rate set for the band. */
    WriteLittleEndian16(payload + off, 1);
    WriteLittleEndian16(payload + off + 2, 16);
    WriteLittleEndian16(payload + off + 4, Use5Ghz ? 0x3fc0 : 0x3fcf);
    off += 16;

    /* STA_REC_PHY: OFDM on 5 GHz, HR/DSSS and ERP on 2.4 GHz. */
    WriteLittleEndian16(payload + off, 0x15);
    WriteLittleEndian16(payload + off + 2, 12);
    WriteLittleEndian16(payload + off + 4, Use5Ghz ? 0x0540 : 0x0fff);
    payload[off + 6] = Use5Ghz ? 8 : 3;
    off += 12;

    /* STA_REC_STATE: associated. */
    WriteLittleEndian16(payload + off, 7);
    WriteLittleEndian16(payload + off + 2, 12);
    payload[off + 8] = Station && Aid == 0 ? 0 : 2;
    off += 12;

    off = AppendPeerWtbl(payload, off, Peer, Wcid, Aid, Station, false);
    return ExchangeUnifiedWait(Rings, MCU_STA_REC, payload, off, Result, TimeoutMs, false);
}

static NTSTATUS RegisterDataPeer(MCU_RINGS *Rings, MT7922_MCU_PROBE_INFO *Result,
    const UCHAR *Peer)
{
    Result->Stage = MT7922_MCU_TX_WTBL;
    return ProgramPeerRecord(Rings, Result, Peer, MT7922_AWDL_DATA_WCID, 1,
        false, Rings->AwdlTxChannel > 14, true, 0, 3000);
}

static NTSTATUS ProgramStationGroup(MT7922_MCU_STATION *Station)
{
    /* The station's reserved group WCID uses MUAR 0xe and a WTBL-only update. */
    UCHAR payload[60] = {0, (UCHAR)Station->MulticastWcid, 1, 0, 1, 0x0e};
    const ULONG bytes = AppendPeerWtbl(payload, 8, Station->Ap.Bssid,
        Station->MulticastWcid, 0, true, true);
    return ExchangeUnified(Station->Scan.Rings, MCU_STA_REC, payload, bytes, &Station->Info);
}

static bool UploadWtblMatchesPeer(PUCHAR Window, ULONG Base, const UCHAR *Peer)
{
    const ULONG macTail = ReadRegister(Window, Base);
    const ULONG macHead = ReadRegister(Window, Base + sizeof(ULONG));
    return (macTail & 0xffffU) == (ULONG)(Peer[4] | (Peer[5] << 8)) &&
        macHead == ReadLittleEndian32(Peer);
}

static void ReadUploadWtblSnapshot(const MCU_RINGS *Rings, const UCHAR *Peer,
    MT7922_AWDL_UPLOAD_WTBL_SNAPSHOT *Snapshot)
{
    PUCHAR window = Rings->Registers;
    const ULONG base = LMAC_WTBL_OFFSET + STATION_SHARED_BA_WCID * LMAC_WTBL_STRIDE;
    *Snapshot = (MT7922_AWDL_UPLOAD_WTBL_SNAPSHOT){.State = 1};
    if (!UploadWtblMatchesPeer(window, base, Peer)) {
        return;
    }

    /* Connac2 LWTBL words 2, 5 through 7 and 9 through 10 contain the TX capabilities,
       aggregation limits, TID BA window codes and initial rate entries. */
    Snapshot->Capabilities = ReadRegister(window, base + 2 * sizeof(ULONG));
    Snapshot->Aggregation = ReadRegister(window, base + 5 * sizeof(ULONG));
    Snapshot->BaWindows = ReadRegister(window, base + 6 * sizeof(ULONG));
    Snapshot->Control = ReadRegister(window, base + 7 * sizeof(ULONG));
    Snapshot->Bandwidth = ReadRegister(window, base + 9 * sizeof(ULONG));
    Snapshot->Rates = ReadRegister(window, base + 10 * sizeof(ULONG));
    Snapshot->State = 2;
}

static NTSTATUS ConfigurePeerBa(MCU_RINGS *Rings, MT7922_MCU_PROBE_INFO *Result,
    const UCHAR *Peer, UCHAR Wcid, const MT7922_AWDL_ADDBA *Addba, BOOLEAN AllowAmsdu, bool Originator,
    UCHAR Port, MT7922_AWDL_UPLOAD_WTBL_REPORT *Trace)
{
    if (!Originator) {
        Result->RxBaFirmwareReply = MCU_NO_RESPONSE;
    }
    const UCHAR tid = (UCHAR)((Addba->Params >> 2) & 0x0fU);
    if (tid >= 8 || !(Addba->Params & 2U)) {
        return STATUS_NOT_SUPPORTED;
    }
    USHORT window = (USHORT)(Addba->Params >> 6);
    if (window == 0 || window > 64) {
        window = 64;
    }

    if (Trace != NULL) {
        Trace->Window = window;
        ReadUploadWtblSnapshot(Rings, Peer, &Trace->Before);
    }

    /* Connac2 programs WTBL_BA first, then STA_REC_BA for the same TID. */
    const UCHAR type = Originator ? 1 : 2;
    UCHAR wtbl[48] = {Port, Wcid, 1, 0, 1, Port, 0, 0,
        13, 0, 40, 0, Wcid, 2, 1, 0, 0, 0, 0, 0,
        8, 0, 28, 0, tid, type};
    WriteLittleEndian16(wtbl + 32, window);
    if (Originator) {
        WriteLittleEndian16(wtbl + 28, Addba->StartingSequence);
        wtbl[30] = 1; /* ba_en */
    } else {
        RtlCopyMemory(wtbl + 34, Peer, 6);
        wtbl[40] = tid;
        wtbl[42] = 1; /* Reset the recipient scoreboard. */
    }
    NTSTATUS status = ExchangeUnifiedWait(Rings, MCU_STA_REC, wtbl, sizeof(wtbl),
        Result, AWDL_RX_BA_REPLY_MS, false);
    if (Trace != NULL) {
        ReadUploadWtblSnapshot(Rings, Peer, &Trace->AfterWtbl);
    }
    if (!Originator) {
        Result->RxBaFirmwareReply = Result->MonitorReply;
    }
    if (!NT_SUCCESS(status)) {
        return status;
    }

    UCHAR sta[20] = {Port, Wcid, 1, 0, 1, Port, 0, 0,
        6, 0, 12, 0, tid, type, (UCHAR)(AllowAmsdu && (Addba->Params & 1U)), (UCHAR)(1U << tid)};
    WriteLittleEndian16(sta + 16, Addba->StartingSequence);
    WriteLittleEndian16(sta + 18, window);
    status = ExchangeUnifiedWait(Rings, MCU_STA_REC, sta, sizeof(sta), Result, AWDL_RX_BA_REPLY_MS, false);
    if (Trace != NULL) {
        ReadUploadWtblSnapshot(Rings, Peer, &Trace->AfterSta);
    }
    if (!Originator) {
        Result->RxBaFirmwareReply = Result->MonitorReply;
    }
    return status;
}

static NTSTATUS ConfigureReceiveBa(MCU_RINGS *Rings, MT7922_MCU_PROBE_INFO *Result,
    const UCHAR *Peer, UCHAR Wcid, const MT7922_AWDL_ADDBA *Addba, BOOLEAN AllowAmsdu)
{
    return ConfigurePeerBa(Rings, Result, Peer, Wcid, Addba, AllowAmsdu, false, 0, NULL);
}

static NTSTATUS SubmitUnicast(MCU_RINGS *Rings, MT7922_MCU_PROBE_INFO *Result,
    AWDL_SYNC_REF *Ref, const UCHAR *Frame, ULONG FrameBytes, USHORT RateCode, BOOLEAN IsAddba)
{
    const ULONG slot = Ref->Link != NULL ? Ref->LinkTxCount % MT7922_AWDL_DATA_PROBES :
        Result->DataProbeSent + Result->AddbaResponses;
    if (Ref->Unicast.Active || slot >= MT7922_AWDL_DATA_PROBES ||
        FrameBytes > UNICAST_TX_STRIDE - MT7922_TXWI_BYTES) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    const ULONG txwiOffset = UNICAST_TX_OFFSET + slot * UNICAST_TX_STRIDE;
    const ULONG frameOffset = txwiOffset + MT7922_TXWI_BYTES;
    const MT7922_AWDL_TX request = {
        Rings->DmaAddress + frameOffset, MT7922_AWDL_DATA_WCID,
        (USHORT)(UNICAST_TOKEN_BASE + slot), (UCHAR)(UNICAST_PACKET_BASE + slot), 0, 1,
        RateCode == 0 ? Rings->AwdlTxRateCode : RateCode
    };
    UCHAR txwi[MT7922_TXWI_BYTES];
    if (!BuildAwdlTxwiForChip(txwi, &request, Frame, FrameBytes, Rings->ChipId)) {
        return STATUS_DATA_ERROR;
    }

    /* Keep every unicast payload valid until reset even when its token is lost. */
    RtlCopyMemory(Rings->Buffer + frameOffset, Frame, FrameBytes);
    const ULONG broadcastOffset = Rings->ActionTx.PayloadOffset;
    Rings->ActionTx.PayloadOffset = txwiOffset;
    const ULONGLONG submittedAt = AwdlHostTime();
    NTSTATUS status = SendPacket(Rings, &Rings->ActionTx, txwi, sizeof(txwi), Result);
    Rings->ActionTx.PayloadOffset = broadcastOffset;
    if (!NT_SUCCESS(status)) {
        return status;
    }

    AWDL_UNICAST_TX *tx = &Ref->Unicast;
    RtlZeroMemory(tx, sizeof(*tx));
    tx->Request = request;
    tx->SubmittedAt = submittedAt;
    tx->DataIndex = Result->DataProbeSent;
    tx->AddbaToken = Ref->Addba.Token;
    tx->IsAddba = IsAddba;
    tx->Active = TRUE;
    if (Ref->Link != NULL) {
        ++Ref->LinkTxCount;
    }
    return STATUS_SUCCESS;
}

static void FinishUnicast(AWDL_SYNC_REF *Ref, MT7922_MCU_PROBE_INFO *Result, ULONGLONG Now)
{
    AWDL_UNICAST_TX *tx = &Ref->Unicast;
    if (!tx->Active) {
        return;
    }
    const MT7922_TX_ATTEMPT_INFO *completion = &tx->Completion;
    if (completion->ReleaseSeen && tx->ReleasedAt == 0) {
        tx->ReleasedAt = Now;
    }
    const BOOLEAN released = completion->ReleaseSeen != 0;
    const BOOLEAN acked = released && completion->ReleaseStatus == 0 &&
        completion->ReportSeen && completion->AckErrors == 0;
    if (Now - tx->SubmittedAt < 300ULL * 10000 &&
        (!released || (completion->ReleaseStatus == 0 && !completion->ReportSeen &&
            Now - tx->ReleasedAt < (ULONGLONG)MT7922_TX_STATUS_GRACE_MS * 10000))) {
        return;
    }

    tx->Active = FALSE;
    if (!released) {
        ++Result->UnicastReleaseTimeouts;
        Ref->LinkTxFailed = Ref->Link != NULL;
    }
    if (tx->IsAddba) {
        if (acked) {
            ++Result->AddbaAcked;
            if (Ref->Addba.Token == tx->AddbaToken) {
                Ref->Addba.Active = FALSE;
            }
        }
        return;
    }
    if (tx->IsTunnelData) {
        Ref->Link->Acked += acked;
        Ref->Link->NoAck += !acked;
        if (tx->Request.RateCode == MT7922_OFDM_24_MBPS || tx->Request.RateCode == MT7922_OFDM_54_MBPS) {
            Ref->Link->FastAckAcked += acked;
            Ref->Link->FastAckNoAck += !acked;
            if (completion->ReportSeen) {
                Ref->Link->FastAckReportRate = completion->ReportRate;
            }
        }
        if (completion->ReportSeen) {
            Ref->Link->ReportRate = completion->ReportRate;
        }
        return;
    }
    if (tx->IsRecoveryEcho) {
        Ref->Link->RecoveryEchoAcked += acked;
    }
    Result->DataProbeResult[tx->DataIndex] = acked;
    Result->DataProbeAckErrors = completion->AckErrors;
    Result->DataProbeReleaseStatus = released ? completion->ReleaseStatus : MAXULONG;
    Result->DataProbeRetries = completion->RetryCount;
    if (acked) {
        ++Result->DataProbeAcked;
        if ((Result->MdnsDirectProbeMask & (1U << tx->DataIndex)) != 0) {
            ++Result->MdnsDirectAcked;
        }
    } else {
        ++Result->DataProbeFailed;
    }
}

static void SetAdvertTiming(const AWDL_SYNC_REF *Ref, ULONGLONG Now, AWDL_SYNC *Sync)
{
    /* The received AW counter is for its current slot, even though the TLV
       calls it the next AW sequence number. Update phase and count together. */
    const ULONGLONG fromSlotStart = Now - Ref->LocalAtRx +
        (64U - Ref->UntilNextSlotTu) * TU_TICKS;
    Sync->UntilNextSlotTu = (USHORT)(64U - (fromSlotStart / TU_TICKS) % 64U);
    Sync->NextWindowSequence = (USHORT)((Ref->NextWindowSequence & ~3U) +
        fromSlotStart / AW_TICKS);
}

static BOOLEAN IsCurrentSharedWindow(const AWDL_SYNC_REF *Ref, ULONGLONG Now)
{
    if (Ref->Channel != 149 || Ref->Sequence.Count != AWDL_CHANNEL_SLOTS) {
        return FALSE;
    }
    const ULONG slot = (Ref->NextWindowSequence / 4U) % AWDL_CHANNEL_SLOTS;
    const ULONGLONG age = Now - Ref->LocalAtRx;
    /* Only send immediately after a peer frame, with time left in its advertised slot. */
    return Ref->Sequence.Channel[slot] == Ref->Channel &&
        age <= 4ULL * TU_TICKS && age + 12ULL * TU_TICKS <= Ref->UntilNextSlotTu * TU_TICKS;
}

/* Project the peer's last synchronized slot and keep three TU clear
   at both channel boundaries. */
static bool IsPeerChannelWindow(const AWDL_SYNC_REF *Ref, ULONGLONG Now, ULONGLONG *Phase)
{
    if (Ref->Sequence.Count != AWDL_CHANNEL_SLOTS ||
        Now - Ref->LocalAtRx > 2ULL * PSF_INTERVAL_TICKS) {
        return false;
    }
    const ULONGLONG elapsed = Now - Ref->LocalAtRx +
        (64U - Ref->UntilNextSlotTu) * TU_TICKS;
    *Phase = elapsed % SLOT_TICKS;
    const ULONG slot = (ULONG)((Ref->NextWindowSequence / 4U + elapsed / SLOT_TICKS)
        % AWDL_CHANNEL_SLOTS);
    return Ref->Sequence.Channel[slot] == Ref->Channel &&
        *Phase >= 3ULL * TU_TICKS && *Phase <= SLOT_TICKS - 3ULL * TU_TICKS;
}

static BOOLEAN CanReplyAfterUploadData(const AWDL_SYNC_REF *Ref, ULONGLONG Now)
{
    const ULONGLONG dataAge = Now - Ref->LocalAtUploadData;
    ULONGLONG phase;
    return Ref->LocalAtUploadData != 0 && dataAge <= 12ULL * TU_TICKS &&
        IsPeerChannelWindow(Ref, Now, &phase) && dataAge <= phase;
}

static NTSTATUS SendRecoveryEcho(MCU_RINGS *Rings, MT7922_MCU_PROBE_INFO *Result, AWDL_SYNC_REF *Ref)
{
    UCHAR ip[56];
    UCHAR frame[96];
    const ULONG ipBytes = BuildIcmp6Echo(ip, sizeof(ip), Result->NicMac, Ref->Peer,
        (USHORT)(Result->PingSent + 1U));
    const ULONG frameBytes = (ULONG)AwdlBuildIpFrame(frame, sizeof(frame), Ref->Peer, Result->NicMac,
        ip, ipBytes, (USHORT)(Result->DataProbeSent + Result->MdnsQueriesSent +
            Ref->Link->MdnsAnnounced + Ref->Link->Submitted + 1U));
    if (ipBytes == 0 || frameBytes == 0) {
        return STATUS_DATA_ERROR;
    }
    const NTSTATUS status = SubmitUnicast(Rings, Result, Ref, frame, frameBytes, 0, FALSE);
    if (NT_SUCCESS(status)) {
        Ref->Unicast.IsRecoveryEcho = TRUE;
        ++Ref->Link->RecoveryEchoSent;
        ++Result->DataProbeSent;
        ++Result->PingSent;
    }
    return status;
}

static NTSTATUS RunAwdlSync(MCU_RINGS *Rings, MT7922_MCU_PROBE_INFO *Result,
    const AWDL_PROBE_FRAME *Probe)
{
    Result->Stage = MT7922_MCU_TX_SYNC;
    AWDL_SYNC_REF ref = {0};
    ref.Link = Rings->AwdlLink;
    ref.Channel = Rings->AwdlTxChannel;
    const BOOLEAN advertiseVht = ref.Link != NULL && ref.Link->AdvertiseVht;
    if (advertiseVht && (!Rings->AwdlWide80 || ref.Channel != 149 ||
            !(Result->NicPhyFlags & MT7922_NIC_PHY_VHT) || Result->NicSpatialStreams < 2 ||
            Result->NicBandwidthCode < 2)) {
        return STATUS_NOT_SUPPORTED;
    }
    ref.Sequence = Probe->Sequence;
    ref.Election = Probe->Election;
    ref.MasterChannel = Probe->MasterChannel;
    RtlCopyMemory(ref.Peer, Probe->Peer, sizeof(ref.Peer));
    RtlCopyMemory(ref.Master, Probe->Master, sizeof(ref.Master));
    ref.UntilNextSlotTu = Probe->UntilNextSlotTu;
    ref.NextWindowSequence = Probe->NextWindowSequence;
    ref.PhysicalTx = Probe->PeerTimestamp;
    ref.LocalAtRx = Probe->PeerLocalTime;

    const ULONGLONG start = AwdlHostTime();
    RtlCopyMemory(Result->DataPeer, ref.Peer, sizeof(Result->DataPeer));
    const ULONG syncMs = ref.Link != NULL
        ? (ref.Link->AnnounceMdns ? AWDL_RECEIVE_LINK_MAX_MS : AWDL_LINK_MAX_MS)
        : (Result->TcpPort != 0 ? MT7922_AWDL_TCP_SYNC_MS : MT7922_AWDL_SYNC_MS);
    const ULONGLONG warmupEnd = start + (Result->TcpPort != 0 ? MT7922_AWDL_TCP_SYNC_MS : 0) * 10000ULL;
    ULONGLONG nextSharedMif = start;
    const ULONGLONG end = start + (ULONGLONG)syncMs * 10000ULL;
    ULONGLONG boundary = ref.LocalAtRx + (ULONGLONG)ref.UntilNextSlotTu * TU_TICKS;
    while (boundary <= start) {
        boundary += SLOT_TICKS;
    }
    ULONGLONG nextPsf = start;
    ULONGLONG nextMdns = start + 200ULL * 10000;
    ULONGLONG nextDirectMdns = start + 600ULL * 10000;
    ULONGLONG nextTcp = nextDirectMdns;
    ULONGLONG nextRecoveryEcho = warmupEnd;
    if (Result->TcpPort != 0) {
        Result->TcpSourcePort = 49152U + (ULONG)((start / 10U) & 0x3fffU);
        Result->TcpInitialSequence = (ULONG)start;
    }
    ULONGLONG nextMif = boundary - SLOT_TICKS / 2;
    while (nextMif < start) {
        nextMif += SLOT_TICKS;
    }
    const MT7922_AWDL_TX drain = {0, 0, 0x7fff, 0xff, 0, 0, 0};
    USHORT sequence = MT7922_AWDL_TX_MAX_ATTEMPTS + 1U;
    NTSTATUS status = STATUS_SUCCESS;

    while (ref.Unicast.Active || AwdlHostTime() < end) {
        MT7922_TX_ATTEMPT_INFO ignored = {0};
        ReadActionTxEvents(&Rings->RomEvents, &drain, &ignored, Result, &ref);
        ReadActionTxEvents(&Rings->RuntimeEvents, &drain, &ignored, Result, &ref);
        ReadActionTxEvents(&Rings->DataRx, &drain, &ignored, Result, &ref);

        const ULONGLONG now = AwdlHostTime();
        FinishUnicast(&ref, Result, now);
        if (ref.LinkTxFailed) {
            return STATUS_IO_TIMEOUT;
        }
        if (now >= end || (ref.Link != NULL && ref.Link->Stopped(ref.Link->Context))) {
            if (ref.Unicast.Active) {
                WaitForAwdlPoll(Rings, &ref);
                continue;
            }
            break;
        }
        PollBaReorder(Result, &ref, now);
        if (ref.Link != NULL && !ref.LinkOnline &&
            (now >= warmupEnd || Result->TcpRefused ||
                (Result->TcpSynAck && Result->TcpResetSent && !ref.Unicast.Active))) {
            UCHAR local[16], peer[16];
            LinkLocalFromMac(Result->NicMac, local);
            LinkLocalFromMac(Result->DataPeer, peer);
            ref.LinkOnline = TRUE;
            ref.Link->Ready(ref.Link->Context, local, peer);
        }
        if (ref.Link != NULL && ref.Addba.Active &&
            (ref.Addba.Attempts >= AWDL_ADDBA_MAX_RESPONSES ||
                Result->AddbaResponses >= AWDL_ADDBA_MAX_RESPONSES ||
                (!ref.Addba.HardwareReady && Result->RxBaAttempts >= AWDL_ADDBA_MAX_RESPONSES))) {
            ref.Addba.Active = FALSE;
        }
        if (!ref.Unicast.Active && ref.Addba.Active && !ref.Addba.HardwareReady &&
            Result->RxBaAttempts < AWDL_ADDBA_MAX_RESPONSES &&
            (ref.Link != NULL || Result->DataProbeSent + Result->AddbaResponses < MT7922_AWDL_DATA_PROBES) &&
            Result->AddbaResponses < AWDL_ADDBA_MAX_RESPONSES) {
            ++Result->RxBaAttempts;
            status = ConfigureReceiveBa(Rings, Result, ref.Peer, MT7922_AWDL_DATA_WCID, &ref.Addba, FALSE);
            Result->RxBaLastStatus = (ULONG)status;
            if (NT_SUCCESS(status)) {
                ++Result->RxBaConfigured;
                ref.Addba.HardwareReady = TRUE;
            } else if (Rings->TransmitPending) {
                return status;
            } else {
                ref.Addba.Active = FALSE;
            }
            continue;
        }
        if (ref.ProbeArmed && ref.ProbeTime <= now &&
            now - ref.ProbeTime > AWDL_PROBE_MAX_LATE_TICKS) {
            ref.ProbeArmed = FALSE;
            ++Result->ProbeWindowsMissed;
        }
        const BOOLEAN multicastMdns = !ref.LinkOnline && Result->MdnsQueriesSent < AWDL_MDNS_MAX_QUERIES &&
            Result->MdnsAirDropAnswers == 0 && now >= nextMdns;
        const BOOLEAN currentWindow = IsCurrentSharedWindow(&ref, now);
        const BOOLEAN currentMdns = multicastMdns &&
            Result->MdnsQueriesSent <= Result->MdnsDirectQueries && currentWindow;
        const BOOLEAN probeDue = ref.ProbeArmed && ref.ProbeTime <= now;
        /* Put the MIF on air while the selected peer is still listening. A
           completed broadcast outside its slot does not establish a neighbor. */
        const BOOLEAN awaitingPeer = ref.Link != NULL &&
            ((!ref.LinkOnline && Result->PingReplies == 0) ||
                (ref.Link->AnnounceMdns && ref.Link->Submitted == 0));
        const BOOLEAN sharedMifDue = awaitingPeer && !ref.Unicast.Active &&
            currentWindow && now >= nextSharedMif;
        /* The sender may open its share sheet after the listener starts. Keep
           testing the selected peer in observed shared windows until it answers. */
        if (ref.LinkOnline && ref.Link->AnnounceMdns && Result->TcpPort != 0 &&
            Result->PeerIpv6Frames == 0 && Result->DataProbeSent < MT7922_AWDL_DATA_PROBES &&
            !ref.Unicast.Active && !sharedMifDue && currentWindow && now >= nextRecoveryEcho) {
            status = SendRecoveryEcho(Rings, Result, &ref);
            if (!NT_SUCCESS(status)) {
                return status;
            }
            nextRecoveryEcho = now + 2000ULL * 10000ULL;
            continue;
        }

        const BOOLEAN uploadReply = ref.LinkOnline && !probeDue && !currentWindow &&
            CanReplyAfterUploadData(&ref, now);
        if (!sharedMifDue && !ref.Unicast.Active && ref.LinkOnline &&
            (probeDue || currentWindow || uploadReply) && !ref.Addba.Active) {
            MT7922_AWDL_LINK *link = ref.Link;
            USHORT rate = 0;
            const ULONG ipBytes = link->TakeSend(link->Context, link->Packet, &rate, NULL);
            if (ipBytes != 0) {
                if (link->AnnounceMdns && AwdlIsMdnsMulticast(link->Packet, ipBytes)) {
                    const ULONG frameBytes = (ULONG)AwdlBuildIpFrame(link->Frame, sizeof(link->Frame),
                        AwdlMdnsMac, Result->NicMac, link->Packet, ipBytes,
                        (USHORT)(Result->DataProbeSent + Result->MdnsQueriesSent +
                            link->MdnsAnnounced + link->Submitted + 1));
                    if (frameBytes == 0) {
                        return STATUS_DATA_ERROR;
                    }
                    const USHORT token = (USHORT)((Rings->ActionTx.Producer + 1) % Rings->ActionTx.Count);
                    status = SendSyncFrame(Rings, Result, &ref, link->Frame, frameBytes, token);
                    if (NT_SUCCESS(status)) {
                        if (link->MdnsAnnounced++ == 0) {
                            nextSharedMif = now;
                        }
                    } else if (status != STATUS_DEVICE_PROTOCOL_ERROR) {
                        return status;
                    }
                    if (probeDue) {
                        ref.ProbeArmed = FALSE;
                    }
                    continue;
                }
                const ULONG frameBytes = (ULONG)AwdlBuildIpFrame(link->Frame, sizeof(link->Frame),
                    ref.Peer, Result->NicMac, link->Packet, ipBytes,
                    (USHORT)(Result->DataProbeSent + Result->MdnsQueriesSent +
                        link->MdnsAnnounced + link->Submitted + 1));
                if (frameBytes == 0) {
                    return STATUS_DATA_ERROR;
                }
                status = SubmitUnicast(Rings, Result, &ref, link->Frame, frameBytes, rate, FALSE);
                if (!NT_SUCCESS(status)) {
                    return status;
                }
                ref.Unicast.IsTunnelData = TRUE;
                ++link->Submitted;
                link->FastAckSubmitted += rate != 0;
                link->UploadWindowReplies += uploadReply;
                if (probeDue) {
                    ref.ProbeArmed = FALSE;
                }
                continue;
            }
        }
        const BOOLEAN currentAddba = ref.Link != NULL && currentWindow &&
            ref.Addba.Active && ref.Addba.HardwareReady;
        if (!sharedMifDue && !ref.Unicast.Active && (probeDue || currentMdns || currentAddba)) {
            const ULONGLONG due = ref.ProbeTime;
            if (probeDue) {
                ref.ProbeArmed = FALSE;
            }
            const ULONG remaining = ref.LinkOnline ? MT7922_AWDL_DATA_PROBES :
                MT7922_AWDL_DATA_PROBES - Result->DataProbeSent - Result->AddbaResponses;
            const ULONG reservedDirect = Result->MdnsAirDropAnswers == 0
                ? AWDL_MDNS_MAX_QUERIES - Result->MdnsDirectQueries : 0;
            const BOOLEAN tcpSynPending = Result->TcpPort != 0 && !Result->TcpSynAck && !Result->TcpRefused &&
                Result->TcpSynSent < AWDL_TCP_MAX_SYN;
            const ULONG tcpSlots = tcpSynPending ? 2U :
                !Result->TcpRefused && !Result->TcpResetSent && Result->TcpSynSent != 0 ? 1U : 0U;
            if (!currentMdns && ref.Addba.Active && ref.Addba.HardwareReady &&
                ref.Addba.Attempts < AWDL_ADDBA_MAX_RESPONSES &&
                Result->AddbaResponses < AWDL_ADDBA_MAX_RESPONSES &&
                (ref.Link != NULL || Result->AddbaResponses <= Result->DataProbeSent) &&
                remaining > reservedDirect + tcpSlots) {
                UCHAR addba[64];
                const size_t addbaBytes = AwdlBuildAddbaResponse(addba, sizeof(addba),
                    ref.Peer, Result->NicMac, ref.Addba.Token, (USHORT)(ref.Addba.Params & ~1U),
                    ref.Addba.Timeout, sequence++);
                if (addbaBytes != 0) {
                    status = SubmitUnicast(Rings, Result, &ref, addba, (ULONG)addbaBytes, 0, TRUE);
                    if (!NT_SUCCESS(status)) {
                        return status;
                    }
                    StartBaReorder(Result, &ref);
                    ++Result->AddbaResponses;
                    ++ref.Addba.Attempts;
                }
                continue;
            }
            if (ref.LinkOnline) {
                continue;
            }
            UCHAR ip[96];
            UCHAR data[MT7922_PROBE_PSF_CAPACITY];
            const BOOLEAN directMdns = reservedDirect != 0 && now >= nextDirectMdns &&
                (ref.Channel == 149 || ref.ProbeOffsetTu == 54) &&
                (Result->TcpPort == 0 || Result->PingReplies != 0 ||
                    Result->PingSent > Result->MdnsDirectQueries);
            /* Sparse shared windows on 149 must serve both discovery paths. */
            if (multicastMdns && (currentMdns || ref.Channel != 149 || !directMdns ||
                    Result->MdnsQueriesSent <= Result->MdnsDirectQueries)) {
                const ULONG ipBytes = BuildMdnsQuery(ip, sizeof(ip), Result->NicMac, NULL);
                const size_t frameBytes = ipBytes == 0 ? 0
                    : AwdlBuildIpFrame(data, sizeof(data), AwdlMdnsMac, Result->NicMac,
                        ip, ipBytes, (USHORT)(Result->DataProbeSent + Result->MdnsQueriesSent + 1));
                if (frameBytes == 0) {
                    return STATUS_DATA_ERROR;
                }
                const USHORT token = (USHORT)((Rings->ActionTx.Producer + 1) % Rings->ActionTx.Count);
                status = SendSyncFrame(Rings, Result, &ref, data, (ULONG)frameBytes, token);
                ++Result->MdnsQueriesSent;
                Result->MdnsCurrentWindowQueries += currentMdns;
                if (NT_SUCCESS(status)) {
                    ++Result->MdnsQueriesReleased;
                } else if (status != STATUS_DEVICE_PROTOCOL_ERROR) {
                    return status;
                }
                nextMdns = now + (1000ULL << (Result->MdnsQueriesSent - 1U)) * 10000;
                continue;
            }

            const BOOLEAN tcpReset = Result->TcpSynAck != 0 && Result->TcpResetSent == 0;
            const BOOLEAN tcpSyn = tcpSynPending && Result->PingReplies != 0 &&
                Result->MdnsAirDropAnswers != 0 && now >= nextTcp && remaining >= 2;
            const BOOLEAN tcp = tcpReset || tcpSyn;
            if (remaining == 0 || (!tcp && remaining <= tcpSlots) ||
                (!tcp && !directMdns && remaining <= reservedDirect + tcpSlots)) {
                continue;
            }
            const ULONG ipBytes = tcp ? BuildTcpProbe(ip, sizeof(ip), Result, tcpReset)
                : directMdns ? BuildMdnsQuery(ip, sizeof(ip), Result->NicMac, ref.Peer)
                : BuildIcmp6Echo(ip, sizeof(ip), Result->NicMac, ref.Peer, (USHORT)(Result->PingSent + 1));
            const size_t dataBytes = ipBytes == 0 ? 0
                : AwdlBuildIpFrame(data, sizeof(data), ref.Peer, Result->NicMac,
                    ip, ipBytes, (USHORT)(Result->DataProbeSent + Result->MdnsQueriesSent + 1));
            if (dataBytes != 0) {
                status = SubmitUnicast(Rings, Result, &ref, data, (ULONG)dataBytes, 0, FALSE);
                if (!NT_SUCCESS(status)) {
                    return status;
                }
                const ULONG index = Result->DataProbeSent++;
                const ULONGLONG submitTime = ref.Unicast.SubmittedAt;
                Result->DataProbeOffset[index] = ref.ProbeOffsetTu;
                Result->ProbePlanUs[index] = (ULONG)((due - start) / 10);
                Result->ProbeSubmitUs[index] = (ULONG)((submitTime - start) / 10);
                Result->ProbePeerDeltaUs[index] = (ULONG)((submitTime - ref.LocalAtRx) / 10);
                if (tcp) {
                    Result->TcpProbeMask |= 1U << index;
                    if (tcpReset) {
                        ++Result->TcpResetSent;
                    } else {
                        ++Result->TcpSynSent;
                        nextTcp = now + 1000ULL * 10000;
                    }
                } else if (directMdns) {
                    Result->MdnsDirectProbeMask |= 1U << index;
                    ++Result->MdnsDirectQueries;
                    nextDirectMdns = now + 1000ULL * 10000;
                } else {
                    ++Result->PingSent;
                }
            }
            continue;
        }

        /* A paused worker sends at most one current advertisement per interval. */
        if (nextPsf <= now) {
            nextPsf += (now - nextPsf) / PSF_INTERVAL_TICKS * PSF_INTERVAL_TICKS;
        }
        if (nextMif <= now) {
            nextMif += (now - nextMif) / SLOT_TICKS * SLOT_TICKS;
        }
        const BOOLEAN mif = sharedMifDue || nextMif < nextPsf;
        if (!sharedMifDue && now < (mif ? nextMif : nextPsf)) {
            WaitForAwdlPoll(Rings, &ref);
            continue;
        }
        AWDL_ACTION advert = {0};
        advert.Sender = ref.Peer;
        advert.Election = ref.Election;
        advert.Sync.Master = ref.Master;
        advert.Sync.MasterChannel = ref.MasterChannel;
        advert.Sync.WindowTu = 16;
        advert.Sync.WindowsPerSlot = 4;
        advert.Sync.NextChannel = ref.Channel;
        advert.Sequence = ref.Sequence;
        SetAdvertTiming(&ref, now, &advert.Sync);
        const ULONG tsfUs = ref.PhysicalTx + (ULONG)((now - ref.LocalAtRx) / 10);
        const BOOLEAN serviceMif = mif && ref.Link != NULL && ref.Link->ServiceLabel != NULL &&
            ref.Link->MdnsAnnounced != 0;
        UCHAR frame[MT7922_AWDL_SERVICE_MIF_CAPACITY];
        size_t bytes = serviceMif
            ? AwdlBuildServiceMif(frame, sizeof(frame), Result->NicMac, &advert, tsfUs,
                (UCHAR)MT7922_AWDL_DEVICE_CLASS, ref.Channel, ref.Link->ServiceLabel, ref.Link->ReceivePort)
            : AwdlBuildAdvert(frame, sizeof(frame), Result->NicMac, &advert, tsfUs, mif,
                (UCHAR)MT7922_AWDL_DEVICE_CLASS, ref.Channel);
        if (advertiseVht) {
            bytes = AwdlAppendVhtCapabilities(frame, sizeof(frame), bytes);
        }
        if (bytes == 0) {
            return STATUS_DATA_ERROR;
        }
        WriteLittleEndian16(frame + 22, (USHORT)((sequence++ & 0x0fffU) << 4));
        const USHORT token = (USHORT)((Rings->ActionTx.Producer + 1) % Rings->ActionTx.Count);
        status = SendSyncFrame(Rings, Result, &ref, frame, (ULONG)bytes, token);
        if (mif) {
            if (nextMif <= now) {
                nextMif += SLOT_TICKS;
            }
            ++Result->SyncMifSent;
        } else {
            nextPsf += PSF_INTERVAL_TICKS;
            ++Result->SyncPsfSent;
        }
        if (!NT_SUCCESS(status)) {
            ++Result->SyncFailed;
            return status;
        }
        ++Result->SyncReleased;
        if (advertiseVht) {
            ++ref.Link->VhtAdverts;
        }
        if (serviceMif) {
            ++ref.Link->ServiceMifsReleased;
        }
        if (sharedMifDue) {
            ++ref.Link->SharedMifReleased;
            nextSharedMif = now + AWDL_SHARED_MIF_INTERVAL_MS * 10000ULL;
        }
        Result->SyncLastTsfUs = tsfUs;
        Result->SyncLastNextSlotTu = advert.Sync.UntilNextSlotTu;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS RunAwdlTransmit(MCU_RINGS *Rings, MT7922_MCU_PROBE_INFO *Result)
{
    AWDL_PROBE_FRAME probe = {0};
    probe.Source = Result->NicMac;
    probe.Target = Rings->AwdlTarget;
    probe.Capture = &Result->Capture[0];
    probe.Capture->Channel = Rings->AwdlTxChannel;
    if (Rings->AwdlTxChannel == 149) {
        probe.PreferMifUntil = AwdlHostTime() + 1200ULL * 10000;
    }
    const ULONGLONG start = KeQueryInterruptTime();
    NTSTATUS status = STATUS_IO_TIMEOUT;
    Result->Stage = MT7922_MCU_TX_FIND_PEER;

    for (ULONG index = 0; index < MT7922_AWDL_TX_MAX_ATTEMPTS; ++index) {
        probe.Length = 0;
        while (probe.Length == 0 &&
            KeQueryInterruptTime() - start < (ULONGLONG)MT7922_AWDL_PROBE_DWELL_MS * 10000) {
            if (Rings->AwdlLink != NULL && Rings->AwdlLink->Stopped(Rings->AwdlLink->Context)) {
                return STATUS_CANCELLED;
            }
            for (ULONG i = 0; i < Rings->DataRx.Count && probe.Length == 0; ++i) {
                MT7922_AWDL_RX_STATUS received = Mt7922PollAwdlRx(&Rings->DataRx, BuildIosProbe, &probe);
                if (received == Mt7922AwdlRxEmpty) {
                    break;
                }
                CountAwdlRxRecord(probe.Capture, received);
            }
            if (probe.Length == 0) {
                WaitMilliseconds(1);
            }
        }
        Result->TxPeerFound = probe.SeenIos;
        if (probe.Length == 0) {
            return status;
        }

        status = SendProbeFrame(Rings, &probe, Result, index);
        MT7922_TX_ATTEMPT_INFO *attempt = &Result->TxProbe[index];
        attempt->Status = (ULONG)status;
        Result->TxProbes = index + 1;
        Result->TxReportSeen = attempt->ReportSeen;
        Result->TxReportAckErrors = attempt->AckErrors;
        Result->TxReportRate = attempt->ReportRate;
        Result->TxReleaseSeen = attempt->ReleaseSeen;
        Result->TxReleaseStatus = attempt->ReleaseStatus;
        Result->TxAttempts = attempt->RetryCount;
        if (NT_SUCCESS(status)) {
            status = RegisterDataPeer(Rings, Result, probe.Peer);
            if (!NT_SUCCESS(status)) {
                return status;
            }
            if (Rings->AwdlLink != NULL && Rings->AwdlLink->AnnounceMdns) {
                Rings->AwdlPollTimer = ExAllocateTimer(NULL, NULL, EX_TIMER_HIGH_RESOLUTION);
            }
            status = RunAwdlSync(Rings, Result, &probe);
            if (Rings->AwdlLink != NULL && Rings->AwdlLink->Reorder.active) {
                MT7922_AWDL_LINK *link = Rings->AwdlLink;
                CollectBaReorderStats(link);
                /* Reset frees held copies when the tunnel can no longer deliver them. */
                ba_reorder_start(&link->Reorder, 0, 0);
                link->Reorder.active = 0;
            }
            if (Rings->AwdlPollTimer != NULL) {
                ExDeleteTimer(Rings->AwdlPollTimer, TRUE, TRUE, NULL);
                Rings->AwdlPollTimer = NULL;
            }
            return status;
        }
        /* Reuse the frame page only after the device releases its token. */
        if (!attempt->ReleaseSeen) {
            return status;
        }
    }
    return status;
}

static NTSTATUS ActivateMonitorAddress(MCU_RINGS *Rings, MT7922_MCU_PROBE_INFO *Result)
{
    Result->Stage = MT7922_MCU_TX_LOCAL_MAC;
    if (!IsUnicastAddress(Result->NicMac)) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    UCHAR device[16] = {0, 0, 0, 0, 0, 0, 12, 0, 1, 0};
    RtlCopyMemory(device + 10, Result->NicMac, 6);
    NTSTATUS status = ExchangeUnified(Rings, MCU_DEV_INFO, device, sizeof(device), Result);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Band 0 and OMAC 0, the monitor context used by mt7921. No beacon template. */
    UCHAR basic[36] = {0};
    basic[6] = 32; /* Basic BSS TLV bytes, including tag and length. */
    basic[8] = 1;  /* Active */
    basic[12] = 2;
    basic[14] = 1; /* CONNECTION_INFRA_AP (0x00010002) for a monitor VIF. */
    basic[16] = 1; /* Connection state */
    RtlCopyMemory(basic + 18, Result->NicMac, 6); /* BSSID. */
    status = ExchangeUnified(Rings, MCU_BSS_INFO, basic, sizeof(basic), Result);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* The transmit BSS uses the primary and center channel of the sniffer. */
    const UCHAR channel = Rings->AwdlTxChannel;
    const BOOLEAN wide = Rings->AwdlWide80;
    const UCHAR center = wide ? 155 : channel;
    /* BSS RLM encodes 80 MHz as 2. The primary channel stays 149. */
    UCHAR rlm[20] = {0, 0, 0, 0, 2, 0, 16, 0,
        channel, center, 0, wide ? 2 : 0, 2, 3, 1, wide ? 4 : 0,
        wide ? 1 : 0, channel > 14 ? 1 : 0, 0, 0};
    return ExchangeUnified(Rings, MCU_BSS_INFO, rlm, sizeof(rlm), Result);
}

static NTSTATUS InitializeRadioMac(MCU_RINGS *Rings, MT7922_MCU_PROBE_INFO *Result)
{
    const ULONG efuseMode = 0x00000100; /* Read the whole factory eFuse image. */
    const ULONG macEnable = 1; /* Enable band 0 without creating a BSS. */
    ULONG reply;
    Result->Stage = MT7922_MCU_RECEIVE_EFUSE;
    NTSTATUS status;
    if (Rings->ChipId == MT7925_CHIP_ID) {
        /* Connac3 exposes the complete eFuse image through the unified buffer API. */
        const UCHAR request[12] = {
            0, 0, 0, 0,
            MCU_UNI_EFUSE_BUFFER_MODE, 0,
            8, 0,
            0, /* EE_MODE_EFUSE */
            1, /* EE_FORMAT_WHOLE */
            0, 0 /* Firmware chooses the full image length. */
        };
        status = ExchangeUnifiedWait(Rings, MCU_UNI_EFUSE_CTRL, request,
            sizeof(request), Result, 3000, false);
        if (NT_SUCCESS(status)) {
            /* Connac3 uses direct MAC setup; the legacy MCU MAC_INIT is 7922-only. */
            ProgramMt7925BasicRates(Rings->Registers);
            return STATUS_SUCCESS;
        }
    } else {
        status = ExchangeMcu(Rings, MCU_EXTENDED | (MCU_EXT_EFUSE << 8),
            &efuseMode, 1, &reply, Result);
    }
    if (!NT_SUCCESS(status)) {
        return status;
    }
    Result->Stage = MT7922_MCU_RECEIVE_MAC;
    status = ExchangeMcu(Rings, MCU_EXTENDED | (MCU_EXT_MAC_INIT << 8),
        &macEnable, 1, &reply, Result);
    return status;
}

static NTSTATUS RunAwdlReceive(MCU_RINGS *Rings, MT7922_MCU_PROBE_INFO *Result)
{
    NTSTATUS status = InitializeRadioMac(Rings, Result);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    if (Rings->AwdlTxChannel != 0) {
        if (Rings->AwdlTxChannel > 14 && !(Result->NicBands & MT7922_NIC_BAND_5GHZ)) {
            return STATUS_NOT_SUPPORTED;
        }
        status = ActivateMonitorAddress(Rings, Result);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }

    /* Connac2 SNIFFER TLV 0: band 0, enable/disable. */
    UCHAR enable[12] = {0, 0, 0, 0, 0, 0, 8, 0, 1, 0, 0, 0};
    Result->Stage = MT7922_MCU_RECEIVE_ENABLE;
    status = ExchangeUnified(Rings, MCU_SNIFFER, enable, sizeof(enable), Result);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    static const UCHAR channels[MT7922_AWDL_SURVEY_CHANNELS] = {6, 44, 149};
    for (ULONG i = 0; i < RTL_NUMBER_OF(channels); ++i) {
        const UCHAR channel = Rings->AwdlWatch != NULL ? 149 :
            Rings->AwdlTxChannel != 0 ? Rings->AwdlTxChannel : channels[i];
        const BOOLEAN wide = Rings->AwdlWide80;
        const UCHAR center = wide ? 155 : channel;
        /* Sniffer TLV 1 uses a different width code, 1 for 80 MHz. */
        const UCHAR config[20] = {
            0, 0, 0, 0, 1, 0, 16, 0, 0, 0, channel <= 14 ? 1 : 2, wide ? 1 : 0,
            channel, wide ? 1 : 0, center, 0, 1, 0, 0, 0
        };
        Result->Stage = MT7922_MCU_RECEIVE_CHANNEL;
        status = ExchangeUnified(Rings, MCU_SNIFFER, config, sizeof(config), Result);
        if (channel == 149 && Rings->AwdlTxChannel == 0 && Rings->AwdlWatch == NULL) {
            Result->Capture149.Channel = channel;
            Result->Capture149Status = (ULONG)status;
            Result->Capture149FirmwareReply = Result->MonitorReply;
            if (status == STATUS_DEVICE_PROTOCOL_ERROR && !Rings->TransmitPending) {
                break;
            }
        }
        if (!NT_SUCCESS(status)) {
            return status;
        }

        /* Drain descriptors queued before or during the channel switch. */
        for (ULONG n = 0; n < Rings->DataRx.Count; ++n) {
            MT7922_AWDL_RX_COUNTS ignored = {0};
            if (Mt7922PollAwdlRx(&Rings->DataRx, CountIosAction, &ignored) == Mt7922AwdlRxEmpty) {
                break;
            }
        }
        if (Rings->AwdlWatch != NULL) {
            const MT7922_AWDL_TX drain = {0, 0, 0x7fff, 0xff, 0, 0, 0};
            MT7922_TX_ATTEMPT_INFO ignored = {0};
            AWDL_SYNC_REF listener = {0};
            listener.Watch = Rings->AwdlWatch;
            const ULONGLONG start = KeQueryInterruptTime();
            Result->Stage = MT7922_MCU_RECEIVE_LISTEN;
            do {
                ReadActionTxEvents(&Rings->DataRx, &drain, &ignored, Result, &listener);
                WaitMilliseconds(1);
            } while (KeQueryInterruptTime() - start < MT7922_AWDL_WATCH_MS * 10000ULL);
            break;
        }
        if (Rings->ActionTx.Count != 0) {
            status = RunAwdlTransmit(Rings, Result);
            if (!NT_SUCCESS(status)) {
                return status;
            }
            break;
        }
        MT7922_AWDL_RX_COUNTS *counts = i < RTL_NUMBER_OF(Result->Capture)
            ? &Result->Capture[i] : &Result->Capture149;
        counts->Channel = channel;
        AWDL_SURVEY_CONTEXT survey = {Result, counts, i};
        Result->Stage = MT7922_MCU_RECEIVE_LISTEN;
        ListenForAwdl(&Rings->DataRx, &survey);
    }

    enable[8] = 0;
    Result->Stage = MT7922_MCU_RECEIVE_DISABLE;
    return ExchangeUnified(Rings, MCU_SNIFFER, enable, sizeof(enable), Result);
}

static NTSTATUS VerifyFirmwareImage(const UCHAR *Image, SIZE_T Length, ULONG ChipId,
    const MCU_FIRMWARE **Firmware)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(acceptedFirmware); ++i) {
        const MCU_FIRMWARE *profile = &acceptedFirmware[i];
        if (ChipId != profile->ChipId || Length != profile->Bytes) {
            continue;
        }
        UCHAR digest[sizeof(profile->Sha256)];
        NTSTATUS status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, NULL, 0, (PUCHAR)Image,
            (ULONG)Length, digest, sizeof(digest));
        if (!NT_SUCCESS(status)) {
            return status;
        }
        if (RtlCompareMemory(digest, profile->Sha256, sizeof(digest)) != sizeof(digest)) {
            return STATUS_DATA_ERROR;
        }
        *Firmware = profile;
        return STATUS_SUCCESS;
    }
    return STATUS_INVALID_BUFFER_SIZE;
}

static void ClearProgrammedRings(PUCHAR Window, ULONG ChipId, BOOLEAN ReceiveAwdl, BOOLEAN transmitAwdl)
{
    if (transmitAwdl) {
        WriteRegister(Window, TX_ACTION_RING + RING_COUNT, 0);
        WriteRegister(Window, TX_ACTION_RING + RING_BASE, 0);
        WriteRegister(Window, TX_ACTION_RING + RING_CPU, 0);
    }
    if (ReceiveAwdl) {
        WriteRegister(Window, RX_DATA_RING + RING_COUNT, 0);
        WriteRegister(Window, RX_DATA_RING + RING_BASE, 0);
        WriteRegister(Window, RX_DATA_RING + RING_CPU, 0);
    }
    const ULONG mcuTxRing = McuTxRingRegister(ChipId);
    WriteRegister(Window, mcuTxRing + RING_COUNT, 0);
    WriteRegister(Window, mcuTxRing + RING_BASE, 0);
    WriteRegister(Window, mcuTxRing + RING_CPU, 0);
    WriteRegister(Window, TX_FWDL_RING + RING_COUNT, 0);
    WriteRegister(Window, TX_FWDL_RING + RING_BASE, 0);
    WriteRegister(Window, TX_FWDL_RING + RING_CPU, 0);
    WriteRegister(Window, RX_MCU_RING + RING_COUNT, 0);
    WriteRegister(Window, RX_MCU_RING + RING_BASE, 0);
    WriteRegister(Window, RX_MCU_RING + RING_CPU, 0);
    if (ChipId != 0x7925) {
        WriteRegister(Window, RX_RUNTIME_RING + RING_COUNT, 0);
        WriteRegister(Window, RX_RUNTIME_RING + RING_BASE, 0);
        WriteRegister(Window, RX_RUNTIME_RING + RING_CPU, 0);
    }
}

static NTSTATUS AllocateMcuRings(MCU_RINGS *Rings, WDFDMAENABLER DmaEnabler,
    BOOLEAN ReceiveData, BOOLEAN TransmitRaw, BOOLEAN StationTxBuffers, WDFCOMMONBUFFER *Buffer,
    MT7922_MCU_PROBE_INFO *Result)
{
    WDFCOMMONBUFFER buffer;
    Result->GetReply = MCU_NO_RESPONSE;
    Result->ReleaseReply = MCU_NO_RESPONSE;
    Result->RomResultStatus = MCU_NO_RESPONSE;
    Result->FirmwareReadyRemapped = MAXULONG;
    Result->MonitorReply = MCU_NO_RESPONSE;
    Result->Stage = MT7922_MCU_ALLOCATE;
    ULONG bufferBytes = ReceiveData ? CAPTURE_BUFFER_BYTES : COMMON_BUFFER_BYTES;
    if (TransmitRaw) {
        bufferBytes = StationTxBuffers ? STATION_TX_BUFFER_BYTES : ACTION_BUFFER_BYTES;
    }
    NTSTATUS status = WdfCommonBufferCreate(DmaEnabler, bufferBytes,
        WDF_NO_OBJECT_ATTRIBUTES, &buffer);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    Rings->ControlTx = (TX_RING){McuTxRingRegister(Rings->ChipId), 0, TX_PAYLOAD_OFFSET,
        TX_DESCRIPTOR_COUNT, 0};
    Rings->DownloadTx = (TX_RING){TX_FWDL_RING, FWDL_RING_OFFSET, TX_PAYLOAD_OFFSET, FWDL_DESCRIPTOR_COUNT, 0};
    if (TransmitRaw) {
        const ULONG count = StationTxBuffers ? STATION_ACTION_COUNT : TX_ACTION_COUNT;
        Rings->ActionTx = (TX_RING){TX_ACTION_RING, ACTION_RING_OFFSET, ACTION_TXWI_OFFSET, count, 0};
    }
    Rings->Buffer = WdfCommonBufferGetAlignedVirtualAddress(buffer);
    PHYSICAL_ADDRESS address = WdfCommonBufferGetAlignedLogicalAddress(buffer);
    if (Rings->Buffer == NULL || (address.QuadPart & (PAGE_SIZE - 1)) != 0 ||
        (ULONGLONG)address.QuadPart > (ULONGLONG)MAXULONG - (bufferBytes - 1) ||
        WdfCommonBufferGetLength(buffer) < bufferBytes) {
        WdfObjectDelete(buffer);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Rings->DmaAddress = address.LowPart;
    AssignRxRing(Rings, &Rings->RomEvents, RX_MCU_RING, RX_RING_OFFSET,
        RX_PAYLOAD_OFFSET, RX_DESCRIPTOR_COUNT);
    if (Rings->ChipId != 0x7925) {
        AssignRxRing(Rings, &Rings->RuntimeEvents, RX_RUNTIME_RING, RX_RUNTIME_OFFSET,
            RX_RUNTIME_PAYLOAD, RX_DESCRIPTOR_COUNT);
    }
    if (ReceiveData) {
        AssignRxRing(Rings, &Rings->DataRx, RX_DATA_RING, DATA_RING_OFFSET,
            DATA_PAYLOAD_OFFSET, DATA_DESCRIPTOR_COUNT);
    }
    RtlZeroMemory(Rings->Buffer, bufferBytes);

    *Buffer = buffer;
    return STATUS_SUCCESS;
}

static NTSTATUS BootMcuRings(MCU_RINGS *Rings, const UCHAR *Image, SIZE_T ImageBytes,
    const MCU_FIRMWARE *Firmware, MT7922_MCU_PROBE_INFO *Result)
{
    PUCHAR window = Rings->Registers;
    Result->Stage = MT7922_MCU_RINGS;
    NTSTATUS status = RequireIdleDma(window, Result);
    if (NT_SUCCESS(status)) {
        Result->Stage = MT7922_MCU_RESET;
        status = ResetWireless(window, Rings->ChipId, &Result->ResetRegister);
    }
    if (NT_SUCCESS(status)) {
        Result->Stage = MT7922_MCU_BOOT_ROM;
        status = ReadBootRomState(window, Rings->ChipId, &Result->BootRomState);
    }

    if (NT_SUCCESS(status)) {
        Result->Stage = MT7922_MCU_RINGS;
        status = PrepareDma(Rings, Result);
    }
    if (NT_SUCCESS(status)) {
        if (ImageBytes == MT7922_REFERENCE_IMAGE_BYTES) {
            WriteRegister(window, MCU_FIRMWARE_MODE, 0);
            status = CheckDmaRegister(window, MCU_FIRMWARE_MODE, 0, Result);
        }
        if (NT_SUCCESS(status)) {
            status = RunCommands(Rings, Image, Firmware, Result);
        }
        if (NT_SUCCESS(status) && Rings->ChipId == 0x7925) {
            ProgramMt7925BasicRates(window);
        }
    }
    return status;
}

NTSTATUS Mt7922RunMcuProbe(PUCHAR Window, ULONG ChipId, WDFDMAENABLER DmaEnabler,
    WDFCOMMONBUFFER *RetainedBuffer, const UCHAR *Image, SIZE_T ImageBytes,
    BOOLEAN ReceiveAwdl, UCHAR TransmitChannel, BOOLEAN Wide80, const UCHAR *TargetPeer, USHORT TcpPort, USHORT RateCode,
    MT7922_AWDL_LINK *Link, MT7922_AWDL_WATCH *Watch, MT7922_MCU_PROBE_INFO *Result)
{
    const BOOLEAN transmitAwdl = TransmitChannel != 0;
    if ((transmitAwdl && RateCode != MT7922_OFDM_6_MBPS && RateCode != MT7922_OFDM_12_MBPS) ||
        (TcpPort != 0 && TargetPeer == NULL) ||
        (TargetPeer != NULL && (!transmitAwdl || !IsUnicastAddress(TargetPeer))) ||
        (Watch != NULL && (!ReceiveAwdl || transmitAwdl || !Wide80 || Watch->Peers == NULL ||
            !IsUnicastAddress(Watch->Peers) || !IsUnicastAddress(Watch->Peers + 6) ||
            RtlCompareMemory(Watch->Peers, Watch->Peers + 6, 6) == 6)) ||
        (Wide80 && TransmitChannel != 149 && Watch == NULL) || (ReceiveAwdl && Image == NULL) || (transmitAwdl &&
        (!ReceiveAwdl || (TransmitChannel != 6 && TransmitChannel != 44 && TransmitChannel != 149)))) {
        return STATUS_INVALID_PARAMETER;
    }
    const MCU_FIRMWARE *firmware = NULL;
    if (Image != NULL) {
        Result->Stage = MT7922_MCU_VALIDATE_IMAGE;
        NTSTATUS verified = VerifyFirmwareImage(Image, ImageBytes, ChipId, &firmware);
        if (!NT_SUCCESS(verified)) {
            return verified;
        }
    }

    MCU_RINGS rings = {0};
    rings.Registers = Window;
    rings.ChipId = ChipId;
    rings.AwdlTxChannel = TransmitChannel;
    rings.AwdlTxRateCode = RateCode;
    rings.AwdlWide80 = Wide80;
    rings.AwdlTarget = TargetPeer;
    rings.AwdlLink = Link;
    rings.AwdlWatch = Watch;
    WDFCOMMONBUFFER buffer;
    Result->TcpPort = TcpPort;
    NTSTATUS status = AllocateMcuRings(&rings, DmaEnabler, ReceiveAwdl, transmitAwdl, FALSE, &buffer, Result);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    status = BootMcuRings(&rings, Image, ImageBytes, firmware, Result);
    if (NT_SUCCESS(status) && ReceiveAwdl) {
        status = RunAwdlReceive(&rings, Result);
    }

    if (rings.Programmed) {
        Result->DmaConfig = ReadRegister(Window, DMA_GLOBAL);
        Result->TxDmaIndex = ReadRegister(Window, McuTxRingRegister(ChipId) + RING_DEVICE);
        Result->DownloadDmaIndex = ReadRegister(Window, TX_FWDL_RING + RING_DEVICE);
        Result->RxDmaIndex = ReadRegister(Window, RX_MCU_RING + RING_DEVICE);
        Result->RuntimeRxDmaIndex = ChipId == 0x7925
            ? MAXULONG : ReadRegister(Window, RX_RUNTIME_RING + RING_DEVICE);
        if (ReceiveAwdl) {
            Result->DataRxDmaIndex = ReadRegister(Window, RX_DATA_RING + RING_DEVICE);
        }
        if (transmitAwdl) {
            Result->DataTxDmaIndex = ReadRegister(Window, TX_ACTION_RING + RING_DEVICE);
        }
        NTSTATUS stopStatus = Mt7922StopMcuDma(Window);
        Result->CleanupStatus = (ULONG)stopStatus;
        if (!NT_SUCCESS(stopStatus)) {
            /* Keep the WDF allocation alive until the bus stops the device. */
            *RetainedBuffer = buffer;
            return NT_SUCCESS(status) ? stopStatus : status;
        }
        ClearProgrammedRings(Window, rings.ChipId, ReceiveAwdl, transmitAwdl);
        if (rings.RestorePciePower) {
            WriteRegister(Window, PCIE_MAC_PM, rings.SavedPciePower);
        }
        if (Image != NULL || (Result->GetReply != MCU_PATCH_PRESENT &&
            Result->ReleaseReply != MCU_SEM_RELEASED && rings.ControlTx.Producer != 0)) {
            ULONG resetRegister;
            Result->CleanupStatus = (ULONG)ResetWireless(Window, rings.ChipId, &resetRegister);
        }
    }
    WdfObjectDelete(buffer);
    if (NT_SUCCESS(status) && Result->CleanupStatus == 0) {
        Result->Stage = MT7922_MCU_COMPLETE;
    }
    return status;
}

static void ClearStationAssociation(MT7922_MCU_STATION *Station)
{
    Station->AssociationId = 0;
    Station->FirmwarePortSecured = false;
    Station->WdiAssociationReported = false;
    Station->PendingFrameBytes = 0;
    Station->Scan.Associated = false;
    Station->Ap = (MT7922_STATION_AP){0};
    RtlZeroMemory(Station->PeerDisconnectFrame, sizeof(Station->PeerDisconnectFrame));
    RtlZeroMemory(Station->PairwiseRxPn, sizeof(Station->PairwiseRxPn));
    RtlZeroMemory(Station->GroupRxPn, sizeof(Station->GroupRxPn));
    Station->GroupKeyId = 0;
    Station->PeerWcid = 0;
}

NTSTATUS Mt7922McuStopStation(MT7922_MCU_STATION *Station)
{
    MCU_RINGS *rings = Station->Scan.Rings;
    Station->Ready = false;
    ClearStationAssociation(Station);
    Mt7922ObserverStop(&Station->Observer);
    Station->ObserverDeadline = 0;
    Station->SharedAdvertDeadline = 0;
    Station->SharedAdvertFrameBytes = 0;
    Station->SharedEchoActive = false;
    Station->SharedBaPort = 0;
    Station->SharedBaPeerRegistered = false;
    Station->SharedBaResetPending = false;
    Station->Scan.Radio = NULL;
    Station->EapolRxPrivacy = Mt7922PrivacyRequireKey;
    if (rings == NULL) {
        return STATUS_SUCCESS;
    }
    rings->StationStopping = true;
    NTSTATUS status = STATUS_SUCCESS;
    if (rings->Programmed) {
        status = Mt7922StopMcuDma(rings->Registers);
        Station->Info.CleanupStatus = (ULONG)status;
        if (!NT_SUCCESS(status)) {
            return status; /* The device can still access DMA memory. */
        }
        ClearProgrammedRings(rings->Registers, rings->ChipId, TRUE, TRUE);
    }
    if (rings->RestorePciePower) {
        WriteRegister(rings->Registers, PCIE_MAC_PM, rings->SavedPciePower);
    }
    if (Station->PairwiseKeyInstalled || Station->GroupKeyInstalled || Station->KeyStateInvalid) {
        ULONG resetRegister;
        status = ResetWireless(rings->Registers, rings->ChipId, &resetRegister);
        Station->Info.CleanupStatus = (ULONG)status;
    }
    Station->KeyStateInvalid = false;
    Station->PairwiseKeyInstalled = false;
    Station->GroupKeyInstalled = false;
    Mt7922McuReleaseStation(Station);
    return status;
}

void Mt7922McuReleaseStation(MT7922_MCU_STATION *Station)
{
    MCU_RINGS *rings = Station->Scan.Rings;
    if (rings != NULL) {
        RtlSecureZeroMemory(rings->Buffer + rings->ControlTx.PayloadOffset, PAGE_SIZE);
        WdfObjectDelete(Station->Buffer);
        ExFreePoolWithTag(rings, 'PctM');
    }
    RtlZeroMemory(Station, sizeof(*Station));
}

NTSTATUS Mt7922McuDisableStationMac(MT7922_MCU_STATION *Station)
{
    if (!Station->Ready || Station->AssociationId != 0) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (Station->Scan.Rings->ChipId == MT7925_CHIP_ID) {
        /* StopStation tears down the Connac3 transport; the legacy command is 7922-only. */
        return STATUS_SUCCESS;
    }
    const ULONG disable = 0; /* mt76_connac_mcu_set_mac_enable: disable band 0. */
    ULONG reply;
    return ExchangeMcu(Station->Scan.Rings, MCU_EXTENDED | (MCU_EXT_MAC_INIT << 8),
        &disable, 1, &reply, &Station->Info);
}

static NTSTATUS ConfigureStationPort(MT7922_MCU_STATION *Station, bool ConfigureDevice)
{
    UCHAR device[MT7922_STATION_DEVICE_BYTES];
    UCHAR bss[MT7922_STATION_IDLE_BSS_BYTES];
    if (!Mt7922BuildStationPort(Station->Info.NicMac, 0, Station->MulticastWcid, device, bss)) {
        return STATUS_INVALID_PARAMETER;
    }
    MCU_RINGS *rings = Station->Scan.Rings;
    if (ConfigureDevice) {
        NTSTATUS status = ExchangeUnified(rings, MCU_DEV_INFO, device, sizeof(device), &Station->Info);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }
    return ExchangeUnified(rings, MCU_BSS_INFO, bss, sizeof(bss), &Station->Info);
}

static NTSTATUS ReadStationEeprom(MT7922_MCU_STATION *Station, USHORT Offset,
    UCHAR Block[MCU_EEPROM_BLOCK_BYTES])
{
    /* mt7921_mcu_read_eeprom: block-aligned EFUSE_ACCESS query, 16 data bytes. */
    const ULONG address = Offset & ~(MCU_EEPROM_BLOCK_BYTES - 1U);
    const UCHAR request[4] = {
        (UCHAR)address, (UCHAR)(address >> 8), (UCHAR)(address >> 16), (UCHAR)(address >> 24)
    };
    Station->Info.ReplyPayloadBytes = 0;
    NTSTATUS status = SendStationRadioCommand(Station->Scan.Rings,
        MCU_EXTENDED | (MCU_EXT_EFUSE_ACCESS << 8), MCU_SET_QUERY_READ,
        request, sizeof(request), true, &Station->Info);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    if (Station->Info.ReplyPayloadBytes < 8U + MCU_EEPROM_BLOCK_BYTES ||
        ReadLittleEndian32(Station->Info.ReplyPayload) != address) {
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }
    RtlCopyMemory(Block, Station->Info.ReplyPayload + 8, MCU_EEPROM_BLOCK_BYTES);
    return STATUS_SUCCESS;
}

static NTSTATUS ApplyStationClc(MT7922_MCU_STATION *Station,
    const MT7922_STATION_RADIO_PROFILE *Profile, const UCHAR *Clc, SIZE_T ClcBytes)
{
    if (Clc == NULL) {
        return STATUS_SUCCESS;
    }
    UCHAR block[MCU_EEPROM_BLOCK_BYTES];
    NTSTATUS status = ReadStationEeprom(Station, MT_EE_HW_TYPE, block);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    const UCHAR encapsulation = block[MT_EE_HW_TYPE % MCU_EEPROM_BLOCK_BYTES] & 1U;
    MT7922_CLC_RULE rule;
    if (!Mt7922ClcSelectPower(Clc, ClcBytes, encapsulation, Profile->Country, &rule)) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    uint8_t command[MT7922_CLC_REQUEST_BYTES + MT7922_CLC_RULE_MAX];
    const size_t bytes = Mt7922BuildStationClc(command, sizeof(command), &rule);
    if (bytes == 0) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    /* No CLC event capability, so mt76 does not wait for the acknowledgement. */
    return SendStationRadioCommand(Station->Scan.Rings, MCU_CE_SET_CLC, MCU_SET_QUERY_SET,
        command, bytes, false, &Station->Info);
}

static NTSTATUS ConfigureStationRadio(MT7922_MCU_STATION *Station,
    const MT7922_STATION_RADIO_PROFILE *Profile);

NTSTATUS Mt7922McuStartStation(MT7922_MCU_STATION *Station, PUCHAR Window,
    ULONG ChipId, WDFDMAENABLER DmaEnabler, const UCHAR *Image, SIZE_T ImageBytes, uint16_t MulticastWcid,
    const MT7922_STATION_RADIO_PROFILE *Profile, const UCHAR *Clc, SIZE_T ClcBytes)
{
    if (Station->Scan.Rings != NULL) {
        return STATUS_DEVICE_BUSY;
    }
    if ((ChipId != MT7922_CHIP_ID && ChipId != MT7925_CHIP_ID) ||
        Window == NULL || DmaEnabler == NULL || Image == NULL || MulticastWcid == 0 ||
        MulticastWcid == STATION_SHARED_BA_WCID ||
        MulticastWcid > 255 || !Mt7922ValidateStationRadio(Profile) ||
        (Clc == NULL) != (ClcBytes == 0)) {
        return STATUS_INVALID_PARAMETER;
    }
    const MCU_FIRMWARE *firmware;
    NTSTATUS status = VerifyFirmwareImage(Image, ImageBytes, ChipId, &firmware);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    MCU_RINGS *rings = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*rings), 'PctM');
    if (rings == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    rings->Registers = Window;
    rings->ChipId = ChipId;
    RtlZeroMemory(&Station->Info, sizeof(Station->Info));
    status = AllocateMcuRings(rings, DmaEnabler, TRUE, TRUE, TRUE, &Station->Buffer, &Station->Info);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(rings, 'PctM');
        return status;
    }
    Station->Scan = (MT7922_MCU_SCAN_TRANSPORT){
        .Rings = rings,
        .Result = &Station->Info,
        .ChipId = (USHORT)rings->ChipId
    };
    rings->StationDataDebug = &Station->ConnectDebug.Data;
    rings->StationIoDebug = &Station->ConnectDebug.StationIo;
    rings->StationIoTraceDeadline = &Station->StationIoTraceDeadline;
    rings->SharedAdvertDebug = &Station->ConnectDebug.SharedAdvert;
    Station->MulticastWcid = MulticastWcid;
    status = BootMcuRings(rings, Image, ImageBytes, firmware, &Station->Info);
    if (NT_SUCCESS(status)) {
        status = InitializeRadioMac(rings, &Station->Info);
    }
    if (NT_SUCCESS(status)) {
        status = ApplyStationClc(Station, Profile, Clc, ClcBytes);
    }
    if (NT_SUCCESS(status)) {
        status = ConfigureStationRadio(Station, Profile);
    }
    if (NT_SUCCESS(status)) {
        status = ConfigureStationPort(Station, true);
    }
    if (!NT_SUCCESS(status)) {
        Mt7922McuStopStation(Station);
        return status;
    }
    /* WPA2's first EAPOL messages precede the pairwise key. A later WDI
       privacy-list request can override this EAPOL-only default. */
    Station->EapolRxPrivacy = Mt7922PrivacyClearUntilKey;
    Station->Ready = true;
    return STATUS_SUCCESS;
}

static NTSTATUS ConfigureStationRadio(MT7922_MCU_STATION *Station,
    const MT7922_STATION_RADIO_PROFILE *Profile)
{
    if ((Station->Info.NicBands & MT7922_NIC_BAND_5GHZ) == 0) {
        return STATUS_NOT_SUPPORTED;
    }

    MCU_RINGS *rings = Station->Scan.Rings;
    uint8_t payload[MT7922_STATION_POWER_BYTES];
    NTSTATUS status;
    if (rings->ChipId == MT7925_CHIP_ID) {
        uint8_t domain[MT7925_STATION_DOMAIN_HEADER + MT7925_STATION_DOMAIN_ENTRY * MT7922_STATION_RADIO_CHANNELS];
        const size_t bytes = Mt7925BuildStationDomain(domain, Profile);
        status = ExchangeUnifiedWait(rings, MCU_UNI_SET_DOMAIN_INFO, domain,
            (ULONG)bytes, &Station->Info, 3000, false);
        uint8_t power[MT7925_STATION_POWER_BYTES];
        for (size_t first = 0; NT_SUCCESS(status) && first < Profile->ChannelCount; first += MT7925_STATION_POWER_BATCH) {
            const size_t powerBytes = Mt7925BuildStationPower(power, Profile, first);
            status = ExchangeUnifiedWait(rings, MCU_UNI_SET_POWER_LIMIT, power,
                (ULONG)powerBytes, &Station->Info, 3000, false);
        }
    } else {
        size_t bytes = Mt7922BuildStationDomain(payload, Profile);
        status = SendStationRadioCommand(rings, MCU_CE_CHANNEL_DOMAIN, MCU_SET_QUERY_SET,
            payload, bytes, false, &Station->Info);
        if (NT_SUCCESS(status)) {
            Mt7922BuildStationChannel(payload, Profile->Channels[0].Number, 0,
                (uint8_t)Station->Info.NicSpatialStreams, true);
            status = SendStationRadioCommand(rings, MCU_EXTENDED | (MCU_EXT_SET_RX_PATH << 8), MCU_SET_QUERY_SET,
                payload, MT7922_STATION_CHANNEL_BYTES, true, &Station->Info);
        }
        for (size_t first = 0; NT_SUCCESS(status) && first < Profile->ChannelCount; first += MT7922_STATION_POWER_BATCH) {
            bytes = Mt7922BuildStationPower(payload, Profile, first);
            status = SendStationRadioCommand(rings, MCU_CE_RATE_TX_POWER, MCU_SET_QUERY_SET,
                payload, bytes, false, &Station->Info);
            if (NT_SUCCESS(status)) {
                const ULONG readPse[2] = {MCU_REG_PSE_BASE, 0};
                /* mt76 reads PSE between batches to pace firmware buffer usage. */
                status = SendStationRadioCommand(rings, MCU_CE_REG_READ, MCU_SET_QUERY_READ,
                    (const uint8_t *)readPse, sizeof(readPse), true, &Station->Info);
            }
        }
    }
    if (NT_SUCCESS(status)) {
        Station->Radio = *Profile;
        Station->Scan.Radio = &Station->Radio;
    }
    return status;
}

NTSTATUS Mt7922McuPrepareStationAp(MT7922_MCU_STATION *Station, const MT7922_STATION_AP *Ap)
{
    if (!Station->Ready || Station->KeyStateInvalid || Station->AssociationId != 0) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (Ap == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    if (Station->Scan.Radio == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (Ap->Band != 2 || !Mt7922StationRadioAllows(Station->Scan.Radio, Ap->Channel)) {
        return STATUS_NOT_SUPPORTED;
    }
    MT7922_STATION_AP selected = *Ap;
    if (Station->Info.NicBandwidthCode < 2 || !Mt7922StationRadioAllows80(Station->Scan.Radio)) {
        selected.Center80 = 0;
    }
    UCHAR bss[MT7922_STATION_JOIN_BSS_BYTES];
    UCHAR rlm[MT7922_STATION_RLM_BYTES];
    const UCHAR chains = Station->Info.NicSpatialStreams == 2 ? 3 : 1;
    if (!Mt7922BuildStationAp(&selected, 0, Station->MulticastWcid, chains, bss, rlm)) {
        return STATUS_INVALID_PARAMETER;
    }
    MCU_RINGS *rings = Station->Scan.Rings;
    if (rings->ChipId == MT7925_CHIP_ID) {
        /* Connac3 RLM uses band 2 for 5 GHz and stream counts, not chain masks. */
        bss[32] = 3; /* PHY_TYPE_OFDM_INDEX. */
        rlm[13] = chains == 3 ? 2 : 1;
        rlm[17] = 2;
    }
    if (rings->StationTxPending || rings->StationDataInFlight != 0) {
        return STATUS_DEVICE_BUSY;
    }
    Station->Ap.Band = 0;
    NTSTATUS status = STATUS_SUCCESS;
    if (rings->ChipId != MT7925_CHIP_ID) {
        uint8_t channel[MT7922_STATION_CHANNEL_BYTES];
        Mt7922BuildStationChannel(channel, selected.Channel, selected.Center80,
            (uint8_t)Station->Info.NicSpatialStreams, false);
        status = SendStationRadioCommand(rings, MCU_EXTENDED | (MCU_EXT_CHANNEL_SWITCH << 8),
            MCU_SET_QUERY_SET, channel, sizeof(channel), true, &Station->Info);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }
    status = ExchangeUnified(rings, MCU_BSS_INFO, bss, sizeof(bss), &Station->Info);
    if (NT_SUCCESS(status)) {
        status = ExchangeUnified(rings, MCU_BSS_INFO, rlm, sizeof(rlm), &Station->Info);
    }
    if (NT_SUCCESS(status)) {
        Station->Ap = selected;
    } else {
        const bool rejected = status == STATUS_DEVICE_PROTOCOL_ERROR &&
            Station->Info.MonitorReply != MCU_NO_RESPONSE && Station->Info.MonitorReply != 0;
        NTSTATUS restored = ConfigureStationPort(Station, false);
        if (!rejected || !NT_SUCCESS(restored)) {
            Station->Ready = false;
            rings->StationStopping = true;
        }
    }
    return status;
}

NTSTATUS Mt7922McuStartStationObserver(MT7922_MCU_STATION *Station, uint8_t Channel)
{
    if (!Station->Ready || Station->KeyStateInvalid || Station->AssociationId == 0 || Station->Ap.Center80 != 0 ||
        Channel == 0 || Channel != Station->Ap.Channel) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (Station->Observer.Running) {
        return STATUS_DEVICE_BUSY;
    }
    MCU_RINGS *rings = Station->Scan.Rings;
    UCHAR enable[MT7922_OBSERVER_ENABLE_BYTES];
    UCHAR config[MT7922_OBSERVER_CONFIG_BYTES];
    if (!Mt7922BuildStationSnifferConfig(config, Channel, Station->Ap.Band, true)) {
        return STATUS_INVALID_PARAMETER;
    }
    Mt7922BuildStationSnifferEnable(enable, true);
    NTSTATUS status = ExchangeUnified(rings, MCU_SNIFFER, enable, sizeof(enable), &Station->Info);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    status = ExchangeUnified(rings, MCU_SNIFFER, config, sizeof(config), &Station->Info);
    if (!NT_SUCCESS(status)) {
        Mt7922BuildStationSnifferEnable(enable, false);
        ExchangeUnified(rings, MCU_SNIFFER, enable, sizeof(enable), &Station->Info);
        return status;
    }
    Mt7922ObserverStart(&Station->Observer, Channel, Station->Ap.Band, Station->Info.NicMac);
    return STATUS_SUCCESS;
}

NTSTATUS Mt7922McuStopStationObserver(MT7922_MCU_STATION *Station)
{
    if (!Station->Observer.Running) {
        return STATUS_SUCCESS;
    }
    Mt7922ObserverStop(&Station->Observer);
    if (Station->ObserverDeadline != 0) {
        Station->ObserverDeadline = 0;
        return STATUS_SUCCESS; /* Software observation did not enable the sniffer. */
    }
    if (!Station->Ready) {
        return STATUS_DEVICE_NOT_READY;
    }
    UCHAR enable[MT7922_OBSERVER_ENABLE_BYTES];
    Mt7922BuildStationSnifferEnable(enable, false);
    return ExchangeUnified(Station->Scan.Rings, MCU_SNIFFER, enable, sizeof(enable),
        &Station->Info);
}

static void CheckSharedAmsduRecord(MT7922_MCU_STATION *Station, const UCHAR *Record, size_t Length)
{
    MT7922_RX_FRAME frame;
    if (Mt792xDecodeRx((USHORT)Station->Scan.Rings->ChipId, Record, Length, &frame) != Mt7922RxRawFrame || frame.Length < 26 ||
        frame.Bytes[0] != 0x88 || (frame.Bytes[1] & 0x47U) != 0 ||
        (frame.Bytes[24] & 0x0fU) != Station->SharedLink->ReorderTid ||
        (frame.AmsduPart == 0 && (frame.Bytes[24] & 0x80U) == 0) ||
        RtlCompareMemory(frame.Bytes + 10, Station->SharedLinkPeer, 6) != 6 ||
        RtlCompareMemory(frame.Bytes + 4, Station->Info.NicMac, 6) != 6) {
        return;
    }
    AWDL_SYNC_REF ref = {.Link = Station->SharedLink, .LinkOnline = TRUE};
    RtlCopyMemory(ref.Peer, Station->SharedLinkPeer, sizeof(ref.Peer));
    CheckIpv6Reply(&Station->Info, &frame, &ref);
    ++ref.Link->AmsduReport.DmaReassembled;
}

static void PollStationRing(MCU_RINGS *Rings, MT7922_RX_RING *Ring,
    MT7922_STATION_RECEIVE Receive, void *Context, MT7922_MCU_STATION *Station)
{
    for (ULONG i = 0; i < Ring->Count; ++i) {
        const UCHAR *record;
        ULONG control;
        if (!Mt7922PeekRxRing(Ring, &record, &control)) {
            return;
        }
        const ULONG bytes = (control & DESCRIPTOR_LEN0) >> DESCRIPTOR_LEN_SHIFT;
        const BOOLEAN last = (control & DESCRIPTOR_LAST0) != 0;
        const BOOLEAN valid = bytes != 0 && bytes <= RX_BUFFER_BYTES &&
            (control & (DESCRIPTOR_DROP | DESCRIPTOR_TO_HOST_A)) == 0;
        if (!Ring->DiscardContinuation && last && valid) {
            bool consumed = ConsumeStationTxRelease(Rings, record, bytes);
            RecordSharedAdvertTxReport(Rings, record, bytes);
            if (!consumed && Rings->ScanEvent != NULL) {
                consumed = Rings->ScanEvent(Rings->ScanContext, record, bytes);
            }
            if (!consumed && Rings->ScanFrame != NULL) {
                Rings->ScanFrame(Rings->ScanContext, record, bytes);
            }
            if (!consumed && Receive != NULL) {
                Receive(Context, record, bytes);
            }
        } else if (Station != NULL && Station->SharedLink != NULL) {
            MT7922_AWDL_LINK *link = Station->SharedLink;
            /* Collect a WFDMA continuation only for the opt-in shared BA session.
               Recycle each DMA buffer immediately after copying its bytes. */
            if (Station->SharedLinkReady && link->AmsduNegotiated && valid &&
                (!Ring->DiscardContinuation || link->AmsduRxRecordBytes != 0) &&
                bytes <= sizeof(link->AmsduRxRecord) - link->AmsduRxRecordBytes) {
                RtlCopyMemory(link->AmsduRxRecord + link->AmsduRxRecordBytes, record, bytes);
                link->AmsduRxRecordBytes += bytes;
                if (last) {
                    CheckSharedAmsduRecord(Station, link->AmsduRxRecord, link->AmsduRxRecordBytes);
                    link->AmsduRxRecordBytes = 0;
                }
            } else {
                link->AmsduRxRecordBytes = 0;
            }
        }
        Ring->DiscardContinuation = !last;
        Mt7922RecycleRxRing(Ring);
    }
}

void Mt7922McuPollStation(MT7922_MCU_STATION *Station,
    MT7922_STATION_RECEIVE Receive, void *Context)
{
    if (Station->Ready) {
        MCU_RINGS *rings = Station->Scan.Rings;
        CheckStationSharedDma(rings);
        /* Deliver queued BSS entries before scan completion on the MCU ring. */
        PollStationRing(rings, &rings->DataRx, Receive, Context, Station);
        PollStationRing(rings, &rings->RuntimeEvents, Receive, Context, NULL);
        PollStationRing(rings, &rings->RomEvents, Receive, Context, NULL);
    }
}

typedef struct {
    MT7922_MCU_STATION *Station;
    MT7922_STATION_MGMT_REPLY *Reply;
    MT7922_STATION_RECEIVE Receive;
    void *Context;
    bool Association;
    bool Received;
} STATION_MGMT_WAIT;

static bool UseProtectedEapol(MT7922_PRIVACY Privacy, bool PairwiseKeyInstalled)
{
    return Privacy == Mt7922PrivacyRequireKey ||
        (Privacy == Mt7922PrivacyClearUntilKey && PairwiseKeyInstalled);
}

static bool StationDataKeysReady(const MT7922_MCU_STATION *Station)
{
    /* Keep user traffic gated until both keys and the firmware peer are ready. */
    return Station->PairwiseKeyInstalled && Station->GroupKeyInstalled &&
        Station->FirmwarePortSecured;
}

static MT7922_RX_STATUS DecodeStationRx(MT7922_MCU_STATION *Station, const uint8_t *Record, size_t Bytes,
    MT7922_RX_FRAME *Frame)
{
    MT7922_EAPOL_DEBUG *debug = &Station->ConnectDebug.Eapol;
    ++debug->RxRecords;
    const MT7922_RX_STATUS status = Mt792xDecodeRx((USHORT)Station->Scan.Rings->ChipId,
        Record, Bytes, Frame);
    if (status == Mt7922RxRawFrame) {
        ++debug->RxRaw;
        if (Frame->Length >= 24 && (Frame->Bytes[0] & 0x0cU) == 0x08U &&
            (Frame->Bytes[1] & 3U) == 2U &&
            RtlCompareMemory(Frame->Bytes + 10, Station->Ap.Bssid, 6) == 6) {
            ++debug->RxApData;
        }
    } else if ((Frame->SkipFlags & MT7922_RX_SKIP_TRANSLATED) != 0) {
        ++debug->RxTranslated;
    }
    return status;
}

static size_t DecodeStationData(MT7922_MCU_STATION *Station,
    const MT7922_RX_FRAME *Frame, bool EapolOnly)
{
    if (Frame->AmsduPart != 0 || Frame->Length < 26) {
        return 0;
    }
    bool eapol;
    const size_t length = Mt7922ValidateStationRxData(Frame->Bytes, Frame->Length,
        Station->Info.NicMac, Station->Ap.Bssid, &eapol);
    if (length == 0) {
        return 0;
    }

    const bool protected = (Frame->Bytes[1] & 0x40U) != 0;
    MT7922_EAPOL_DEBUG *debug = &Station->ConnectDebug.Eapol;
    if (eapol) {
        ++debug->RxEapol;
        debug->RxPlain += !protected;
        if (protected != UseProtectedEapol(Station->EapolRxPrivacy, Station->PairwiseKeyInstalled)) {
            ++debug->RxPolicyDrop;
            return 0;
        }
    } else if (EapolOnly || !StationDataKeysReady(Station) || !protected) {
        return 0;
    }
    if (!protected) {
        RtlCopyMemory(Station->PendingFrame, Frame->Bytes, length);
        Station->PendingFrameEapol = eapol;
        return length;
    }

    /* Group RX uses the port's GTK. As in mt792x_rx_get_wcid, the descriptor can name
       the AP peer, which identifies the port for its group traffic. */
    const bool group = (Frame->Bytes[4] & 1U) != 0;
    const bool peer = Frame->ReceiveWcid == Station->PeerWcid;
    if (!Frame->CcmpDecrypted ||
        (group ? !Station->GroupKeyInstalled || Frame->ReceiveKeyId != Station->GroupKeyId ||
                 (!peer && Frame->ReceiveWcid != Station->MulticastWcid)
               : !Station->PairwiseKeyInstalled || Frame->ReceiveKeyId != 0 || !peer)) {
        debug->RxSecurityDrop += eapol;
        return 0;
    }

    /* Connac2 uses one counter per TID per key; non-QoS shares counter 0. */
    const unsigned tid = Frame->Bytes[0] == 0x88 ? Frame->Bytes[24] & 0x0fU : 0;
    uint64_t *last = group ? &Station->GroupRxPn[tid] : &Station->PairwiseRxPn[tid];
    if (Frame->CcmpPacketNumber <= *last) {
        debug->RxSecurityDrop += eapol;
        return 0;
    }
    *last = Frame->CcmpPacketNumber;
    RtlCopyMemory(Station->PendingFrame, Frame->Bytes, length);
    /* Hardware stripped the CCMP fields. Indicate the verified MSDU without an IV. */
    Station->PendingFrame[1] &= 0xbfU;
    Station->PendingFrameEapol = eapol;
    return length;
}

static void CheckStationManagement(void *Context, const uint8_t *Record, size_t Bytes)
{
    STATION_MGMT_WAIT *wait = Context;
    MT7922_MCU_STATION *station = wait->Station;
    MT7922_RX_FRAME frame;
    const bool raw = DecodeStationRx(station, Record, Bytes, &frame) == Mt7922RxRawFrame;
    if (!wait->Received && raw && frame.AmsduPart == 0 &&
        Mt7922ReadStationManagement(frame.Bytes, frame.Length, station->Info.NicMac,
            station->Ap.Bssid, wait->Association, wait->Reply)) {
        wait->Received = true;
        return;
    }
    if (raw && wait->Association && station->PendingFrameBytes == 0) {
        station->PendingFrameBytes = DecodeStationData(station, &frame, true);
        if (station->PendingFrameBytes != 0) {
            ++station->ConnectDebug.Eapol.RxEarly;
            return; /* Hold the early AP message until WiFiCx has seen the association. */
        }
    }
    if (wait->Receive != NULL) {
        wait->Receive(wait->Context, Record, Bytes);
    }
}

static NTSTATUS SubmitStationFrame(MT7922_MCU_STATION *Station, const uint8_t *Frame,
    size_t Bytes, USHORT Wcid, USHORT Sequence, bool Data)
{
    MCU_RINGS *rings = Station->Scan.Rings;
    CheckStationSharedDma(rings);
    if (rings->StationTxPending || rings->StationDataInFlight != 0 || rings->TransmitPending ||
        rings->SharedAdvertDmaPending) {
        return STATUS_DEVICE_BUSY;
    }
    USHORT token = (USHORT)(rings->StationTxToken % (STATION_TOKEN_COUNT - 1U) + 1U);
    if (token >= STATION_SHARED_TOKEN && token < STATION_DATA_TX_TOKEN_END) {
        token = (USHORT)STATION_DATA_TX_TOKEN_END;
    }
    MT7922_AWDL_TX tx = {
        rings->DmaAddress + ACTION_FRAME_OFFSET, Wcid, token,
        (UCHAR)(ACTION_PACKET_ID + token % 253U), 0, 3, MT7922_OFDM_6_MBPS
    };
    tx.ChipId = (USHORT)rings->ChipId;
    UCHAR txwi[MT7922_TXWI_BYTES];
    const int valid = Data ? Mt7922BuildStationDataTxwi(txwi, &tx, Frame, Bytes) :
        Mt7922BuildStationMgmtTxwi(txwi, &tx, Frame, Bytes);
    if (!valid) {
        return STATUS_INVALID_PARAMETER;
    }
    /* Both traffic types share the one DMA buffer protected by the release token. */
    RtlCopyMemory(rings->Buffer + ACTION_FRAME_OFFSET, Frame, Bytes);
    rings->StationSequence = Sequence;
    rings->StationTxToken = token;
    rings->StationTxPending = true;
    return SendPacket(rings, &rings->ActionTx, txwi, sizeof(txwi), &Station->Info);
}

static NTSTATUS ExchangeStationManagement(MT7922_MCU_STATION *Station,
    const MT7922_STATION_JOIN_REQUEST *Association, USHORT Wcid, STATION_MGMT_WAIT *Wait)
{
    MCU_RINGS *rings = Station->Scan.Rings;
    /* Drain older responses before associating a new exchange with the RX stream. */
    Mt7922McuPollStation(Station, Wait->Receive, Wait->Context);
    if (rings->StationTxPending || rings->TransmitPending) {
        return STATUS_DEVICE_BUSY;
    }
    UCHAR frame[MT7922_STATION_MGMT_MAX];
    const USHORT sequence = (USHORT)((rings->StationSequence + 1U) & 0xfffU);
    const size_t bytes = Mt7922BuildStationManagement(frame, sizeof(frame), Station->Info.NicMac,
        Station->Ap.Bssid, sequence, Association);
    Wait->Association = Association != NULL;
    Wait->Received = false;
    NTSTATUS status = SubmitStationFrame(Station, frame, bytes, Wcid, sequence, false);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Keep a bounded wait per management exchange while still dispatching ordinary RX. */
    const ULONGLONG start = KeQueryInterruptTime();
    do {
        Mt7922McuPollStation(Station, CheckStationManagement, Wait);
        if (!rings->StationTxPending) {
            if (Wait->Received) {
                return Wait->Reply->StatusCode == 0 ? STATUS_SUCCESS : STATUS_CONNECTION_REFUSED;
            }
            if (rings->StationTxStatus != 0) {
                return STATUS_DEVICE_PROTOCOL_ERROR;
            }
        }
        WaitMilliseconds(1);
    } while (KeQueryInterruptTime() - start < 500ULL * 10000U);
    return STATUS_IO_TIMEOUT;
}

NTSTATUS Mt7922McuExchangeStationJoin(MT7922_MCU_STATION *Station,
    const MT7922_STATION_JOIN_REQUEST *Request, MT7922_STATION_JOIN_RESULT *Result,
    MT7922_STATION_RECEIVE Receive, void *Context)
{
    if (!Station->Ready || Station->Ap.Band == 0) {
        return STATUS_DEVICE_NOT_READY;
    }
    if (Station->Ap.Band != 2) {
        return STATUS_NOT_SUPPORTED; /* Initial OFDM management path is 5 GHz. */
    }
    if (Station->KeyStateInvalid || Station->AssociationId != 0) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (Request == NULL || Result == NULL || Request->Wcid == 0 || Request->Wcid > 255 ||
        Request->Wcid == Station->MulticastWcid || Request->Wcid == STATION_SHARED_BA_WCID) {
        return STATUS_INVALID_PARAMETER;
    }
    /* Validate the association body before sending the authentication request. */
    UCHAR frame[MT7922_STATION_MGMT_MAX];
    const size_t frameBytes = Mt7922BuildStationManagement(frame, sizeof(frame), Station->Info.NicMac,
        Station->Ap.Bssid, 0, Request);
    if (frameBytes == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(Result, sizeof(*Result));
    Station->PendingFrameBytes = 0;
    MCU_RINGS *rings = Station->Scan.Rings;
    if (rings->StationTxPending || rings->TransmitPending) {
        return STATUS_DEVICE_BUSY;
    }
    NTSTATUS status = ProgramPeerRecord(rings, &Station->Info, Station->Ap.Bssid, Request->Wcid,
        0, true, true, true, 0, 3000);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    Result->RequestBodyBytes = (uint16_t)(frameBytes - 24);
    RtlCopyMemory(Result->RequestBody, frame + 24, Result->RequestBodyBytes);
    STATION_MGMT_WAIT wait = {Station, &Result->Authentication, Receive, Context, false, false};
    status = ExchangeStationManagement(Station, NULL, Request->Wcid, &wait);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    Result->AssociationStarted = true;
    wait.Reply = &Result->Association;
    status = ExchangeStationManagement(Station, Request, Request->Wcid, &wait);
    if (NT_SUCCESS(status)) {
        const USHORT aid = Result->Association.AssociationId;
        UCHAR bss[MT7922_STATION_JOIN_BSS_BYTES];
        UCHAR rlm[MT7922_STATION_RLM_BYTES];
        MT7922_STATION_AP ap = Station->Ap;
        ap.Qos = false; /* The initial legacy path does not negotiate WMM. */
        const UCHAR chains = Station->Info.NicSpatialStreams == 2 ? 3 : 1;
        if (!Mt7922BuildStationAp(&ap, 0, Station->MulticastWcid, chains, bss, rlm)) {
            return STATUS_INVALID_DEVICE_STATE;
        }
        bss[16] = 0; /* Associated in the firmware BSS, with host data authorization separate. */
        status = ExchangeUnified(rings, MCU_BSS_INFO, bss, sizeof(bss), &Station->Info);
        if (NT_SUCCESS(status)) {
            status = ProgramPeerRecord(rings, &Station->Info, Station->Ap.Bssid, Request->Wcid,
                aid, true, true, false, 0, 3000);
        }
        if (NT_SUCCESS(status)) {
            status = ProgramStationGroup(Station);
        }
        if (NT_SUCCESS(status)) {
            Station->PeerWcid = Request->Wcid;
            Station->AssociationId = aid;
            Station->Scan.Associated = true;
        } else {
            Station->KeyStateInvalid = true;
        }
    }
    if (!NT_SUCCESS(status)) {
        Station->PendingFrameBytes = 0;
    }
    return status;
}

static NTSTATUS ProgramStationKey(MT7922_MCU_STATION *Station, bool Group, uint8_t KeyId,
    const uint8_t Key[MT7922_CCMP_KEY_BYTES])
{
    const UCHAR wcid = (UCHAR)(Group ? Station->MulticastWcid : Station->PeerWcid);
    UCHAR payload[MT7922_CCMP_COMMAND_BYTES];
    if (!Mt7922BuildStationCcmp(payload, wcid, KeyId, Group, Key)) {
        return STATUS_INVALID_PARAMETER;
    }
    NTSTATUS status = ExchangeUnifiedWait(Station->Scan.Rings, MCU_STA_REC, payload, sizeof(payload),
        &Station->Info, 3000, true);
    RtlSecureZeroMemory(payload, sizeof(payload));
    return status;
}

static NTSTATUS SecureStationPort(MT7922_MCU_STATION *Station)
{
    /* mt76 enables STA_REC_BASIC with CONN_STATE_PORT_SECURE. Leave the installed keys
       and WTBL intact by updating only the BASIC TLV of the live AP peer. */
    UCHAR payload[28] = {0, (UCHAR)Station->PeerWcid, 1, 0, 1};
    WritePeerBasic(payload + 8, Station->Ap.Bssid, Station->AssociationId,
        true, MT7922_CONN_STATE_PORT_SECURE, false);
    NTSTATUS status = ExchangeUnifiedWait(Station->Scan.Rings, MCU_STA_REC, payload, sizeof(payload),
        &Station->Info, 3000, false);
    Station->ConnectDebug.Data.PortStatus = status;
    Station->FirmwarePortSecured = NT_SUCCESS(status);
    Station->ConnectDebug.Data.PortSecured = Station->FirmwarePortSecured;
    return status;
}

static NTSTATUS ResetPeerRecord(MCU_RINGS *Rings, MT7922_MCU_PROBE_INFO *Result,
    const UCHAR *Mac, USHORT Wcid, USHORT Aid, bool Station, bool Group, UCHAR Port, ULONG TimeoutMs)
{
    UCHAR payload[40] = {Port, (UCHAR)Wcid, Group ? 1 : 2, 0, 1, Group ? 0x0e : Port};
    ULONG offset = 8;
    if (!Group) {
        /* mt76_connac_mcu_sta_cmd: disconnected BASIC plus empty RESET_AND_SET. */
        WritePeerBasic(payload + offset, Mac, Aid, Station, MT7922_CONN_STATE_DISCONNECT, false);
        offset += 20;
    }
    const ULONG bytes = AppendPeerWtbl(payload, offset, NULL, Wcid, 0, Station, Group);
    return ExchangeUnifiedWait(Rings, MCU_STA_REC, payload, bytes, Result, TimeoutMs, false);
}

static NTSTATUS ResetStationPeer(MT7922_MCU_STATION *Station, bool Group)
{
    return ResetPeerRecord(Station->Scan.Rings, &Station->Info, Station->Ap.Bssid,
        Group ? Station->MulticastWcid : Station->PeerWcid, Station->AssociationId, true, Group, 0, 3000);
}

static bool DrainStationTx(MT7922_MCU_STATION *Station)
{
    const ULONGLONG start = KeQueryInterruptTime();
    MCU_RINGS *rings = Station->Scan.Rings;
    do {
        Mt7922McuPollStation(Station, NULL, NULL);
        if (!rings->StationTxPending && rings->StationDataInFlight == 0 && !rings->TransmitPending) {
            return true;
        }
        WaitMilliseconds(1);
    } while (KeQueryInterruptTime() - start < 250ULL * 10000U);
    return false;
}

NTSTATUS Mt7922McuDisconnectStation(MT7922_MCU_STATION *Station, uint16_t Reason)
{
    if (!Station->Ready || Station->AssociationId == 0) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    /* The observer rides the association; a failed disable must not block teardown. */
    (void)Mt7922McuStopStationObserver(Station);
    if (Station->SharedAdvertDeadline != 0) {
        MT7922_SHARED_ADVERT_DEBUG *debug = Station->SharedEchoActive
            ? &Station->ConnectDebug.SharedEcho : &Station->ConnectDebug.SharedAdvert;
        debug->Status = STATUS_CANCELLED;
    }
    Station->SharedAdvertDeadline = 0;
    Station->SharedAdvertFrameBytes = 0;
    /* Stop user data first; keep the ring and the radio profile live for reconnect. */
    Station->WdiAssociationReported = false;
    Station->PendingFrameBytes = 0;
    const bool drained = DrainStationTx(Station);
    if (!drained && Station->Scan.Rings->StationDataInFlight != 0) {
        /* A reset must stop DMA before recycling buffers whose tokens are still owned by firmware. */
        Station->KeyStateInvalid = true;
        return STATUS_IO_TIMEOUT;
    }
    if (drained && Reason != 0) {
        MCU_RINGS *rings = Station->Scan.Rings;
        const USHORT sequence = (USHORT)((rings->StationSequence + 1U) & 0xfffU);
        UCHAR frame[MT7922_STATION_DISCONNECT_BYTES];
        const size_t bytes = Mt7922BuildStationDisconnect(frame, Station->Info.NicMac,
            Station->Ap.Bssid, sequence, Reason);
        if (bytes != 0 && NT_SUCCESS(SubmitStationFrame(Station, frame, bytes,
                Station->PeerWcid, sequence, false))) {
            DrainStationTx(Station); /* Local teardown is required even when the AP cannot ACK. */
        }
    }

    bool *installed[] = {&Station->PairwiseKeyInstalled, &Station->GroupKeyInstalled};
    const uint8_t keyIds[] = {0, Station->GroupKeyId};
    NTSTATUS status = STATUS_SUCCESS;
    for (unsigned i = 0; i < RTL_NUMBER_OF(installed) && NT_SUCCESS(status); ++i) {
        if (*installed[i]) {
            status = ProgramStationKey(Station, i != 0, keyIds[i], NULL);
            if (NT_SUCCESS(status)) {
                *installed[i] = false;
            }
        }
        if (NT_SUCCESS(status)) {
            status = ResetStationPeer(Station, i != 0);
        }
    }
    if (NT_SUCCESS(status)) {
        status = ConfigureStationPort(Station, false);
    }
    if (!NT_SUCCESS(status)) {
        Station->KeyStateInvalid = true; /* The host must reset if firmware state is uncertain. */
        return status;
    }

    ClearStationAssociation(Station);
    Station->KeyStateInvalid = false;
    return STATUS_SUCCESS;
}

int32_t Mt7922McuInstallStationCcmp(void *Context, bool Group, uint8_t KeyId,
    const uint8_t Key[MT7922_CCMP_KEY_BYTES], uint64_t InitialRxPn)
{
    MT7922_MCU_STATION *station = Context;
    if (!station->Ready || station->AssociationId == 0 || station->KeyStateInvalid) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (Key == NULL || InitialRxPn > UINT64_C(0xffffffffffff) || (Group ? KeyId > 3 : KeyId != 0)) {
        return STATUS_INVALID_PARAMETER;
    }
    bool *installed = Group ? &station->GroupKeyInstalled : &station->PairwiseKeyInstalled;
    if (*installed) {
        /* A partial or repeated key batch requires a fresh association. */
        station->KeyStateInvalid = true;
        return STATUS_NOT_SUPPORTED;
    }
    if (station->Scan.Rings->StationDataInFlight != 0 && !DrainStationTx(station)) {
        station->KeyStateInvalid = true;
        return STATUS_IO_TIMEOUT;
    }
    NTSTATUS status = ProgramStationKey(station, Group, KeyId, Key);
    *installed = NT_SUCCESS(status);
    if (!*installed) {
        station->KeyStateInvalid = true;
        return status;
    }

    /* mt76 initializes each traffic identifier's replay counter when the key is installed. */
    uint64_t *rxPn = Group ? station->GroupRxPn : station->PairwiseRxPn;
    for (unsigned tid = 0; tid < RTL_NUMBER_OF(station->PairwiseRxPn); ++tid) {
        rxPn[tid] = InitialRxPn;
    }
    if (Group) {
        station->GroupKeyId = KeyId;
    } else if (station->EapolRxPrivacy == Mt7922PrivacyClearUntilKey) {
        station->PendingFrameBytes = 0;
    }
    if (station->PairwiseKeyInstalled && station->GroupKeyInstalled) {
        status = SecureStationPort(station);
        if (!NT_SUCCESS(status)) {
            station->KeyStateInvalid = true;
        } else if (station->Ap.Band == 2 && (station->Ap.Channel == 44 || station->Ap.Channel == 149)) {
            /* Owly's 5 GHz same-channel cases. Observe only records already
               delivered by the station RX path, with no firmware mode change. */
            Mt7922ObserverStart(&station->Observer, station->Ap.Channel, station->Ap.Band, station->Info.NicMac);
            station->ObserverDeadline = KeQueryInterruptTime() + 45ULL * 10000000ULL;
            station->ConnectDebug.Observation.Channel = station->Ap.Channel;
        }
    }
    return status;
}

static NTSTATUS PostStationRawTxwi(MT7922_MCU_STATION *Station, const UCHAR *Txwi,
    const UCHAR *Frame, size_t Bytes, ULONG Offset, bool DeferPublish);

static NTSTATUS SubmitStationDataFrame(MT7922_MCU_STATION *Station, const UCHAR *Frame,
    size_t Bytes, USHORT Sequence)
{
    MCU_RINGS *rings = Station->Scan.Rings;
    CheckStationSharedDma(rings);
    if (rings->StationTxPending || rings->TransmitPending || rings->SharedAdvertDmaPending ||
        rings->StationDataInFlight == MT7922_STATION_DATA_TX_SLOTS) {
        return STATUS_DEVICE_BUSY;
    }
    TX_RING *ring = &rings->ActionTx;
    const ULONG device = ReadRegister(rings->Registers, ring->Registers + RING_DEVICE);
    ObserveStationDataDma(rings, device);
    if (device >= ring->Count || (ring->Producer + 1U) % ring->Count == device) {
        return STATUS_DEVICE_BUSY;
    }

    /* Each outstanding data frame owns its buffer until its matching TX-free token. */
    ULONG i = 0;
    while (i < MT7922_STATION_DATA_TX_SLOTS && rings->StationDataTx[i].SubmittedAt != 0) {
        ++i;
    }
    if (i == MT7922_STATION_DATA_TX_SLOTS) {
        return STATUS_DEVICE_BUSY;
    }
    const ULONG offset = STATION_DATA_TX_OFFSET + i * UNICAST_TX_STRIDE;
    MT7922_AWDL_TX tx = {
        rings->DmaAddress + offset + MT7922_TXWI_BYTES, Station->PeerWcid,
        (USHORT)(STATION_DATA_TX_TOKEN_BASE + i), (UCHAR)(STATION_DATA_TX_PID_BASE + i),
        0, 3, MT7922_OFDM_6_MBPS
    };
    tx.ChipId = (USHORT)rings->ChipId;
    UCHAR txwi[MT7922_TXWI_BYTES];
    if (!Mt7922BuildStationDataTxwi(txwi, &tx, Frame, Bytes)) {
        return STATUS_INVALID_PARAMETER;
    }

    MT7922_STATION_IO_DEBUG *trace = &Station->ConnectDebug.StationIo;
    STATION_DATA_TX *slot = &rings->StationDataTx[i];
    *slot = (STATION_DATA_TX){
        .SubmittedAt = AwdlHostTime(),
        .TraceDeadline = trace->Active ? Station->StationIoTraceDeadline : 0,
        .Descriptor = ring->Producer
    };
    if (rings->StationDataInFlight++ == 0) {
        rings->StationDataDmaCursor = device;
    }
    rings->StationDataFastPollUntil = slot->SubmittedAt + 250ULL * 10000U;
    rings->StationSequence = Sequence;
#ifdef MT7922_STATION_PACKET_TRACE
    slot->PacketTraced = Mt7922MatchStationPacketTrace(Frame, Bytes, false, &slot->Packet);
    if (slot->PacketTraced) {
        Mt7922WriteStationPacketTrace(&slot->Packet, Mt7922TraceTxSubmit, 0);
    }
#endif
    const NTSTATUS status = PostStationRawTxwi(Station, txwi, Frame, Bytes, offset, false);
    if (trace->Active) {
        trace->TxInFlightPeak = max(trace->TxInFlightPeak, rings->StationDataInFlight);
        trace->TxSubmitted += NT_SUCCESS(status);
        trace->TxSubmitErrors += !NT_SUCCESS(status);
    }
    if (!NT_SUCCESS(status)) {
        /* A failed doorbell readback does not establish that DMA missed the frame. */
        slot->TraceDeadline = 0;
#ifdef MT7922_STATION_PACKET_TRACE
        if (slot->PacketTraced) {
            Mt7922WriteStationPacketTrace(&slot->Packet, Mt7922TraceTxFailed, status);
        }
#endif
    }
    return status;
}

bool Mt7922McuStationDataTxActive(const MT7922_MCU_STATION *Station)
{
    const MCU_RINGS *rings = Station->Scan.Rings;
    /* The regular poll can still recover a late release after the fast poll expires. */
    return Station->Ready && !Station->KeyStateInvalid && rings->StationDataInFlight != 0 &&
        AwdlHostTime() < rings->StationDataFastPollUntil;
}

NTSTATUS Mt7922McuSendStationData(MT7922_MCU_STATION *Station,
    const uint8_t *Ethernet, size_t Bytes, MT7922_PRIVACY Privacy)
{
    if (!Station->Ready || Station->AssociationId == 0 || !Station->WdiAssociationReported ||
        Station->KeyStateInvalid || Station->PeerDisconnectFrame[0] != 0) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (Ethernet == NULL || Bytes <= 14 || (unsigned)Privacy > Mt7922PrivacyClearUntilKey) {
        return STATUS_INVALID_PARAMETER;
    }
    const bool eapol = Mt7922StationIsEapol(Ethernet);
    MT7922_EAPOL_DEBUG *debug = &Station->ConnectDebug.Eapol;
    debug->TxEapol += eapol;
    if (!eapol && !StationDataKeysReady(Station)) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    const bool protect = !eapol || UseProtectedEapol(Privacy, Station->PairwiseKeyInstalled);
    if (protect && !Station->PairwiseKeyInstalled) {
        ++debug->TxNeedKey;
        debug->TxLastStatus = STATUS_INVALID_DEVICE_STATE;
        return STATUS_INVALID_DEVICE_STATE;
    }
    const USHORT sequence = (USHORT)((Station->Scan.Rings->StationSequence + 1U) & 0xfffU);
    uint8_t frame[MT7922_STATION_RAW_MAX];
    const size_t size = Mt7922EncapsulateStationData(frame, sizeof(frame), Ethernet, Bytes,
        Station->Info.NicMac, Station->Ap.Bssid, sequence, protect);
    if (!eapol) {
        return SubmitStationDataFrame(Station, frame, size, sequence);
    }
    const NTSTATUS status = SubmitStationFrame(Station, frame, size, Station->PeerWcid, sequence, true);
    debug->TxLastStatus = status;
    debug->TxSubmitted += NT_SUCCESS(status);
    debug->TxBusy += status == STATUS_DEVICE_BUSY;
    debug->TxError += !NT_SUCCESS(status) && status != STATUS_DEVICE_BUSY;
    return status;
}

NTSTATUS Mt7922McuArmSharedAdvert(MT7922_MCU_STATION *Station, const MT7922_SHARED_ADVERT_REQUEST *Request)
{
    if (Request->RequestId == 0 || Request->Echo > 1 || Request->Reserved != 0 ||
        !IsUnicastAddress(Request->Peer) ||
        RtlCompareMemory(Request->Peer, Station->Info.NicMac, 6) == 6) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!Station->Ready || !Station->WdiAssociationReported || Station->KeyStateInvalid ||
        !StationDataKeysReady(Station) || Station->Ap.Band != 2 || Station->Ap.Channel != 149) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (Station->SharedLink != NULL || Station->SharedAdvertDeadline != 0 ||
        Station->Scan.Rings->SharedAdvertReleasePending || Station->Scan.Rings->SharedAckInFlight != 0) {
        return STATUS_DEVICE_BUSY;
    }

    Station->SharedAdvertRequest = *Request;
    Station->SharedEchoActive = false;
    Station->SharedEchoSequence = (USHORT)(Request->RequestId % 65535U + 1U);
    Station->Info.PingReplies = 0;
    RtlCopyMemory(Station->Info.DataPeer, Request->Peer, sizeof(Request->Peer));
    Station->SharedAdvertDeadline = KeQueryInterruptTime() + STATION_SHARED_ADVERT_TICKS;
    Station->SharedAdvertFrameBytes = 0;
    Station->ConnectDebug.SharedAdvert = (MT7922_SHARED_ADVERT_DEBUG){
        .RequestId = Request->RequestId, .Status = STATUS_PENDING
    };
    Station->ConnectDebug.SharedEcho = (MT7922_SHARED_ADVERT_DEBUG){
        .RequestId = Request->Echo ? Request->RequestId : 0,
        .Status = Request->Echo ? STATUS_PENDING : STATUS_SUCCESS
    };
    Station->ConnectDebug.SharedEchoReplies = 0;
    return STATUS_SUCCESS;
}

bool Mt7922McuSharedAvailable(const MT7922_MCU_STATION *Station)
{
    return Station->Ready && Station->WdiAssociationReported && !Station->KeyStateInvalid &&
        Station->PeerDisconnectFrame[0] == 0 && StationDataKeysReady(Station) &&
        Station->Ap.Band == 2 && Station->Ap.Channel == 149 &&
        (Station->SharedLink != NULL ? NT_SUCCESS(Station->SharedLinkStatus) :
            Station->SharedAdvertDeadline == 0 && !Station->Scan.Rings->SharedAdvertReleasePending &&
            Station->Scan.Rings->SharedAckInFlight == 0);
}

static AWDL_SYNC_REF SharedReceiveRef(MT7922_MCU_STATION *Station)
{
    AWDL_SYNC_REF ref = {.Link = Station->SharedLink, .LinkOnline = Station->SharedLinkReady};
    RtlCopyMemory(ref.Peer, Station->SharedLinkPeer, sizeof(ref.Peer));
    return ref;
}

static NTSTATUS ConfigureAwdlUploadPort(MT7922_MCU_STATION *Station, bool Enable)
{
    static const UCHAR awdlBssid[6] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    UCHAR device[MT7922_STATION_DEVICE_BYTES];
    UCHAR idleBss[MT7922_STATION_IDLE_BSS_BYTES];
    const UCHAR port = Station->SharedBaPort;
    if (!Mt7922BuildStationPort(Station->Info.NicMac, port, STATION_SHARED_BA_WCID, device, idleBss)) {
        return STATUS_INVALID_PARAMETER;
    }
    MCU_RINGS *rings = Station->Scan.Rings;
    if (!Enable) {
        idleBss[8] = 0; /* Inactive BSS. */
        idleBss[12] = 2; /* Same infrastructure AP type as the raw monitor context. */
        idleBss[17] = 0; /* Keep the existing best-effort access category group. */
        RtlCopyMemory(idleBss + 18, awdlBssid, sizeof(awdlBssid));
        NTSTATUS status = ExchangeUnifiedWait(rings, MCU_BSS_INFO, idleBss, sizeof(idleBss),
            &Station->Info, AWDL_RX_BA_REPLY_MS, false);
        if (!NT_SUCCESS(status)) {
            return status;
        }
        device[8] = 0;
        return ExchangeUnifiedWait(rings, MCU_DEV_INFO, device, sizeof(device),
            &Station->Info, AWDL_RX_BA_REPLY_MS, false);
    }

    MT7922_STATION_AP ap = Station->Ap;
    RtlCopyMemory(ap.Bssid, awdlBssid, sizeof(awdlBssid));
    ap.Band = 2; /* The selected AWDL coex channel is 5 GHz. */
    ap.Qos = true;
    UCHAR bss[MT7922_STATION_JOIN_BSS_BYTES];
    UCHAR rlm[MT7922_STATION_RLM_BYTES];
    const UCHAR chains = Station->Info.NicSpatialStreams == 2 ? 3 : 1;
    if (!Mt7922BuildStationAp(&ap, port, STATION_SHARED_BA_WCID, chains, bss, rlm)) {
        return STATUS_INVALID_PARAMETER;
    }
    bss[12] = 2; /* Infrastructure AP role for raw frames, with no beacon offload. */
    bss[16] = 0; /* Active association state in the firmware BSS. */
    bss[17] = 0; /* Reuse WMM group 0 to isolate the PHY capability change. */
    bss[29] = 0xb1; /* 802.11a, HT5, VHT, HE5 in the BASIC BSS PHY format. */
    bss[32] = 0x78; /* OFDM, HT, VHT, HE in PHY_TYPE_BIT_* format. */
    NTSTATUS status = ExchangeUnifiedWait(rings, MCU_DEV_INFO, device, sizeof(device),
        &Station->Info, AWDL_RX_BA_REPLY_MS, false);
    if (NT_SUCCESS(status)) {
        status = ExchangeUnifiedWait(rings, MCU_BSS_INFO, bss, sizeof(bss),
            &Station->Info, AWDL_RX_BA_REPLY_MS, false);
    }
    if (NT_SUCCESS(status)) {
        status = ExchangeUnifiedWait(rings, MCU_BSS_INFO, rlm, sizeof(rlm),
            &Station->Info, AWDL_RX_BA_REPLY_MS, false);
    }
    return status;
}

static void ResetSharedBaPeerIfIdle(MT7922_MCU_STATION *Station)
{
    MCU_RINGS *rings = Station->Scan.Rings;
    if (!Station->SharedBaResetPending || !Station->Ready || rings == NULL || rings->StationStopping ||
        rings->StationTxPending || rings->StationDataInFlight != 0 || rings->TransmitPending || rings->SharedAdvertReleasePending ||
        rings->SharedAckInFlight != 0) {
        return;
    }
    Station->SharedBaResetPending = false;
    ++Station->ConnectDebug.Coexistence.BaResetAttempts;
    if (Station->SharedBaPeerRegistered) {
        Station->SharedBaCleanupStatus = ResetPeerRecord(rings, &Station->Info, Station->SharedBaPeer,
            STATION_SHARED_BA_WCID, 1, false, false, Station->SharedBaPort, AWDL_RX_BA_REPLY_MS);
        if (!NT_SUCCESS(Station->SharedBaCleanupStatus)) {
            return;
        }
        Station->SharedBaPeerRegistered = false;
    }
    if (Station->SharedBaPort != 0) {
        Station->SharedBaCleanupStatus = ConfigureAwdlUploadPort(Station, false);
        if (NT_SUCCESS(Station->SharedBaCleanupStatus)) {
            Station->SharedBaPort = 0;
        }
    }
}

void Mt7922McuSnapshotCoexistence(MT7922_MCU_STATION *Station)
{
    const MCU_RINGS *rings = Station->Scan.Rings;
    MT7922_COEX_DEBUG *debug = &Station->ConnectDebug.Coexistence;
    ++debug->Sample;
    debug->Flags = (Station->SharedLink != NULL ? MT7922_COEX_AWDL_ATTACHED : 0) |
        ((rings->StationTxPending || rings->StationDataInFlight != 0) ? MT7922_COEX_STATION_TX_PENDING : 0) |
        (rings->TransmitPending ? MT7922_COEX_SYNC_DMA_PENDING : 0) |
        (rings->SharedAdvertDmaPending ? MT7922_COEX_AWDL_DMA_PENDING : 0) |
        (rings->SharedAdvertReleasePending ? MT7922_COEX_AWDL_TX_PENDING : 0) |
        (Station->SharedBaResetPending ? MT7922_COEX_BA_RESET_PENDING : 0) |
        (Station->SharedBaPeerRegistered ? MT7922_COEX_BA_PEER_REGISTERED : 0);
    debug->BaCleanupStatus = Station->SharedBaCleanupStatus;
    debug->AckInFlight = rings->SharedAckInFlight;
}

void Mt7922McuAttachSharedLink(MT7922_MCU_STATION *Station, MT7922_AWDL_LINK *Link, const uint8_t *Peer)
{
    Station->SharedLink = Link;
    RtlCopyMemory(Station->SharedLinkPeer, Peer, sizeof(Station->SharedLinkPeer));
    RtlCopyMemory(Station->Info.DataPeer, Peer, sizeof(Station->Info.DataPeer));
    Station->Info.SyncPeerFrames = 0;
    Station->Info.PeerIpv6Frames = 0;
    Station->Info.Ipv6PacketsToUs = 0;
    Station->Info.AwdlRxDataSelected = 0;
    Station->Info.AwdlRxDataToUs = 0;
    Station->Info.MdnsQueriesSent = 0;
    Station->Info.MdnsAirDropAnswers = 0;
    Station->Info.MdnsSampleCount = 0;
    Station->Info.AddbaRequests = 0;
    Station->Info.AddbaResponses = 0;
    Station->Info.AddbaAcked = 0;
    Station->Info.RxBaAttempts = 0;
    Station->Info.RxBaConfigured = 0;
    Station->Info.RxBaLastStatus = 0;
    Station->Info.RxBaFirmwareReply = MCU_NO_RESPONSE;
    Station->SharedLinkReady = false;
    Station->SharedLinkStatus = RtlCompareMemory(Peer, Station->Info.NicMac, 6) == 6 ?
        STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
    if (NT_SUCCESS(Station->SharedLinkStatus) && Link->AdvertiseVht &&
        (Station->Ap.Center80 == 0 || Station->Info.NicSpatialStreams < 2 ||
            (Link->AdvertiseHe && !(Station->Info.NicPhyFlags & MT7922_NIC_PHY_HE)))) {
        Station->SharedLinkStatus = STATUS_NOT_SUPPORTED;
    }
    Station->SharedSynAckLength = 0;
    Station->SharedSynAckRetryPending = false;
    Station->SharedSynAckRetried = false;
    Station->SharedLinkNextMif = 0;
    Station->SharedLinkNextMdns = 0;
    Station->SharedLinkDataUntil = 0;
    Station->SharedPeerSyncAt = 0;
    Station->SharedPeerChannels = (AWDL_CHANNEL_SEQUENCE){0};
    RtlZeroMemory(Station->SharedPeerMaster, sizeof(Station->SharedPeerMaster));
    Station->SharedAdvertFrameBytes = 0;
    Station->SharedLinkTx = (MT7922_SHARED_ADVERT_DEBUG){0};
    Station->SharedBaReplyInFlight = false;
    Station->SharedBaResetPending = Station->SharedBaPeerRegistered || Station->SharedBaPort != 0;
    Station->SharedBaCleanupStatus = STATUS_SUCCESS;
}

static void ReapSharedAckTx(MT7922_MCU_STATION *Station)
{
    MCU_RINGS *rings = Station->Scan.Rings;
    if (rings == NULL || rings->SharedAckInFlight == 0) {
        return;
    }

    MT7922_AWDL_LINK *link = Station->SharedLink;
    const ULONGLONG now = AwdlHostTime();
    for (ULONG i = 0; i < STATION_SHARED_TX_SLOTS; ++i) {
        STATION_SHARED_ACK_TX *slot = &rings->SharedAckTx[i];
        if (slot->SubmittedAt == 0) {
            continue;
        }
        if (!slot->Released) {
            /* Keep the buffer reserved even if this AWDL session has ended. */
            if (link != NULL && NT_SUCCESS(Station->SharedLinkStatus) &&
                now - slot->SubmittedAt >= 10000000ULL) {
                link->UploadTxReport.TimeoutKind = MT7922_AWDL_UPLOAD_TX_DATA_TIMEOUT;
                link->UploadTxReport.TimeoutSequence = slot->Sequence;
                link->UploadTxReport.TimeoutAgeUs = (ULONG)((now - slot->SubmittedAt) / 10U);
                Station->SharedLinkStatus = STATUS_IO_TIMEOUT;
            }
            continue;
        }
        if (link != NULL) {
            const bool missing = slot->ReleaseStatus == 0 && !slot->ReportSeen;
            const bool acked = slot->ReleaseStatus == 0 && slot->ReportSeen && slot->Report.AckErrors == 0;
            ++link->SharedAckReport.Released;
            link->SharedAckReport.ReleaseErrors += slot->ReleaseStatus != 0;
            link->Acked += acked;
            link->NoAck += !acked && !missing;
            link->SharedTxStatusMissing += missing;
            if (slot->ReportSeen) {
                link->ReportRate = slot->Report.RateCode;
                if (!link->AnnounceMdns && slot->Wcid == STATION_SHARED_BA_WCID) {
                    const USHORT mode = (slot->Report.RateCode >> 6) & 0xfU;
                    link->UploadBaReport.He80TxReports += mode == 8 && slot->Report.Bandwidth == 2;
                    if (link->FastUpload && link->AckTrace != NULL && mode == 8) {
                        ++link->UploadHeTxByBandwidth[slot->Report.Bandwidth];
                    }
                    link->UploadBaReport.AmpduTxReports += slot->Report.Ampdu != 0;
                }
            }
        }
        *slot = (STATION_SHARED_ACK_TX){0};
        --rings->SharedAckInFlight;
    }
}

void Mt7922McuDetachSharedLink(MT7922_MCU_STATION *Station)
{
    if (Station->SharedLink != NULL) {
        ReapSharedAckTx(Station);
        MT7922_AWDL_UPLOAD_WTBL_REPORT *trace = &Station->SharedLink->UploadWtblReport;
        if (trace->Before.State != 0 && Station->SharedBaPeerRegistered) {
            ReadUploadWtblSnapshot(Station->Scan.Rings, Station->SharedLinkPeer, &trace->Finish);
        }
        if (Station->SharedLink->Reorder.active) {
            AWDL_SYNC_REF ref = SharedReceiveRef(Station);
            StopBaReorder(&Station->Info, &ref);
        }
        MCU_RINGS *rings = Station->Scan.Rings;
        rings->SharedSynAckQueueTiming = NULL;
        RtlZeroMemory(rings->SharedAckDmaTrace, sizeof(rings->SharedAckDmaTrace));
        rings->SharedAckDmaPending = 0;
        for (ULONG i = 0; i < STATION_SHARED_TX_SLOTS; ++i) {
            rings->SharedAckTx[i].Trace = NULL;
        }
        Station->SharedLink = NULL;
        Station->SharedLinkReady = false;
        Station->SharedAdvertFrameBytes = 0;
        Station->SharedBaResetPending = Station->SharedBaPeerRegistered || Station->SharedBaPort != 0;
        ResetSharedBaPeerIfIdle(Station);
    }
}

static void PrepareSharedAdvert(MT7922_MCU_STATION *Station, const MT7922_RX_FRAME *Frame)
{
    AWDL_ACTION action;
    const AWDL_ACTION_KIND kind = AwdlDecodeAction(Frame->Bytes, Frame->Length, &action);
    MT7922_AWDL_LINK *link = Station->SharedLink;
    const UCHAR *peer = link != NULL ? Station->SharedLinkPeer : Station->SharedAdvertRequest.Peer;
    if ((kind != AwdlPsf && kind != AwdlMif) || RtlCompareMemory(peer, action.Sender, 6) != 6) {
        return;
    }

    if (link != NULL && action.HeSupported) {
        link->HeReport.PeerSupported = 1;
    }
    if (link != NULL && link->FastUpload && (action.Phy.HtPresent || action.Phy.VhtPresent)) {
        AWDL_PEER_PHY *peerPhy = &link->UploadPeerPhy;
        if (action.Phy.HtPresent) {
            peerPhy->HtCapabilities = action.Phy.HtCapabilities;
            peerPhy->HtAmpdu = action.Phy.HtAmpdu;
            RtlCopyMemory(peerPhy->HtRxMcs, action.Phy.HtRxMcs, sizeof(peerPhy->HtRxMcs));
            peerPhy->HtPresent = 1;
        }
        if (action.Phy.VhtPresent) {
            peerPhy->VhtCapabilities = action.Phy.VhtCapabilities;
            peerPhy->VhtRxMcs = action.Phy.VhtRxMcs;
            peerPhy->VhtTxMcs = action.Phy.VhtTxMcs;
            peerPhy->VhtPresent = 1;
        }
    }
    Station->SharedAdvertFrameBytes = 0;
    AWDL_SYNC_REF ref = {.Channel = Station->Ap.Channel};
    RtlCopyMemory(ref.Peer, action.Sender, sizeof(ref.Peer));
    if (link != NULL && action.Sync.Master != NULL &&
        RtlCompareMemory(Station->SharedPeerMaster, action.Sync.Master, 6) == 6) {
        ref.Sequence = Station->SharedPeerChannels;
    }
    const BOOLEAN synchronized = UpdateSyncRef(&ref, &action, &Station->Info);
    const ULONGLONG now = AwdlHostTime();
    if (link != NULL) {
        Station->SharedLinkDataUntil = 0;
        if (synchronized) {
            ++Station->Info.SyncPeerFrames;
            Station->SharedPeerChannels = ref.Sequence;
            RtlCopyMemory(Station->SharedPeerMaster, ref.Master, sizeof(Station->SharedPeerMaster));
            Station->SharedPeerSyncAt = ref.LocalAtRx;
            Station->SharedPeerPhysicalTx = ref.PhysicalTx;
            Station->SharedPeerWindowSequence = ref.NextWindowSequence;
            Station->SharedPeerUntilNextSlotTu = ref.UntilNextSlotTu;
        }
    }
    const bool regularWindow = synchronized && IsCurrentSharedWindow(&ref, now);
    if (synchronized && link != NULL && link->AckTrace != NULL &&
        link->UploadBa.State == AwdlTxBaPrepared && ref.LocalAtRx > link->UploadBa.PreparedAt) {
        ++link->UploadAddbaTiming.FreshSyncs;
        link->UploadAddbaTiming.RegularWindows += regularWindow;
    }
    if (!regularWindow) {
        return;
    }
    if (link != NULL) {
        Station->SharedLinkDataUntil = ref.LocalAtRx + (ref.UntilNextSlotTu - 3U) * TU_TICKS;
        if (now < Station->SharedLinkNextMif) {
            return;
        }
    }

    if (link == NULL && Station->SharedEchoActive) {
        UCHAR ip[56];
        const ULONG bytes = BuildIcmp6Echo(ip, sizeof(ip), Station->Info.NicMac,
            action.Sender, Station->SharedEchoSequence);
        Station->SharedAdvertFrameBytes = AwdlBuildIpFrame(Station->SharedAdvertFrame,
            sizeof(Station->SharedAdvertFrame), action.Sender, Station->Info.NicMac,
            ip, bytes, Station->SharedEchoSequence);
    } else {
        /* Use the same advert builder and in-slot timing as the existing AWDL link. */
        SetAdvertTiming(&ref, now, &action.Sync);
        const ULONG tsfUs = ref.PhysicalTx + (ULONG)((now - ref.LocalAtRx) / 10);
        /* Match the exclusive receiver: publish the service only after its first
           user-mode mDNS announcement has reached the radio. */
        Station->SharedAdvertHasServiceRecords = link != NULL && link->ServiceLabel != NULL &&
            link->MdnsAnnounced != 0;
        Station->SharedAdvertFrameBytes = Station->SharedAdvertHasServiceRecords
            ? AwdlBuildServiceMif(Station->SharedAdvertFrame, sizeof(Station->SharedAdvertFrame),
                Station->Info.NicMac, &action, tsfUs, (UCHAR)MT7922_AWDL_DEVICE_CLASS,
                ref.Channel, link->ServiceLabel, link->ReceivePort)
            : AwdlBuildAdvert(Station->SharedAdvertFrame, sizeof(Station->SharedAdvertFrame),
                Station->Info.NicMac, &action, tsfUs, 1, (UCHAR)MT7922_AWDL_DEVICE_CLASS, ref.Channel);
        if (link != NULL && link->AdvertiseHe &&
            !AwdlSetHighEfficiency(Station->SharedAdvertFrame, Station->SharedAdvertFrameBytes)) {
            Station->SharedAdvertFrameBytes = 0;
        }
        if (link != NULL && link->AdvertiseVht) {
            Station->SharedAdvertFrameBytes = AwdlAppendVhtCapabilities(Station->SharedAdvertFrame,
                sizeof(Station->SharedAdvertFrame), Station->SharedAdvertFrameBytes);
        }
    }
    const USHORT remaining = ref.UntilNextSlotTu - 12U;
    Station->SharedAdvertWindowEnd = ref.LocalAtRx + min(4U, remaining) * TU_TICKS;
}

static bool BuildStationSharedTxwi(MT7922_MCU_STATION *Station, UCHAR *Txwi,
    UCHAR *Frame, size_t Bytes, ULONG Offset, MT7922_AWDL_TX *Tx)
{
    MCU_RINGS *rings = Station->Scan.Rings;
    Tx->ChipId = (USHORT)rings->ChipId;
    USHORT *sequenceCounter = Tx->Ampdu ? &Station->SharedLink->UploadBa.NextSequence : &rings->StationSequence;
    const USHORT sequence = (USHORT)((*sequenceCounter + 1U) & 0xfffU);
    WriteLittleEndian16(Frame + 22, (USHORT)(sequence << 4));

    /* mt76 uses WMM best effort for QoS data and ALTX for fixed-rate control. */
    const bool sharedUnicast = Station->SharedLink != NULL && (Frame[4] & 1U) == 0;
    Tx->FrameAddress = rings->DmaAddress + Offset + MT7922_TXWI_BYTES;
    Tx->Queue = sharedUnicast && !Tx->Ampdu ? MT7922_TX_QUEUE_ALTX0 : STATION_SHARED_AC;
    Tx->SpeIndex = sharedUnicast && !Tx->Ampdu && Station->Info.NicSpatialStreams == 2 ? 24 : 0;
    if (Tx->Wcid == STATION_SHARED_BA_WCID) {
        Tx->OwnMacIndex = Station->SharedBaPort;
    }
    if (!BuildAwdlTxwiForChip(Txwi, Tx, Frame, (ULONG)Bytes, rings->ChipId)) {
        return false;
    }
    *sequenceCounter = sequence;
    return true;
}

static NTSTATUS PostStationRawTxwi(MT7922_MCU_STATION *Station, const UCHAR *Txwi,
    const UCHAR *Frame, size_t Bytes, ULONG Offset, bool DeferPublish)
{
    MCU_RINGS *rings = Station->Scan.Rings;
    /* Keep the frame and TXWI in a dedicated DMA slot until firmware releases its token. */
    RtlCopyMemory(rings->Buffer + Offset + MT7922_TXWI_BYTES, Frame, Bytes);
    const ULONG oldOffset = rings->ActionTx.PayloadOffset;
    rings->ActionTx.PayloadOffset = Offset;
    const NTSTATUS status = PostPacket(rings, &rings->ActionTx, Txwi, MT7922_TXWI_BYTES,
        &Station->Info, false, DeferPublish);
    rings->ActionTx.PayloadOffset = oldOffset;
    return status;
}

static NTSTATUS PostStationSharedFrame(MT7922_MCU_STATION *Station, UCHAR *Frame, size_t Bytes,
    MT7922_SHARED_ADVERT_DEBUG *Debug, bool WaitForEcho, USHORT Rate, USHORT Wcid)
{
    MCU_RINGS *rings = Station->Scan.Rings;
    MT7922_AWDL_TX tx = {
        .Wcid = Wcid,
        .Token = STATION_SHARED_TOKEN,
        .PacketId = STATION_SHARED_PACKET_ID,
        .RateCode = Rate
    };
    UCHAR txwi[MT7922_TXWI_BYTES];
    if (!BuildStationSharedTxwi(Station, txwi, Frame, Bytes, UNICAST_TX_OFFSET, &tx)) {
        return STATUS_DATA_ERROR;
    }

    rings->SharedAdvertDebug = Debug;
    rings->SharedTxWaitForEcho = WaitForEcho;
    rings->SharedAdvertReleasePending = true;
    rings->SharedAdvertRequestId = Debug->RequestId;
    rings->SharedAdvertSequence = rings->StationSequence;
    rings->SharedAdvertWcid = Wcid;
    rings->SharedAdvertQueue = tx.Queue;
    rings->SharedAdvertDmaPending = true;
    const NTSTATUS status = PostStationRawTxwi(Station, txwi, Frame, Bytes, UNICAST_TX_OFFSET, false);
    Debug->Submitted = NT_SUCCESS(status);
    return status;
}

static bool IsStationPeerChannelWindow(const MT7922_MCU_STATION *Station, ULONGLONG Now)
{
    if (Station->SharedPeerSyncAt == 0) {
        return false;
    }
    const AWDL_SYNC_REF ref = {
        .Channel = Station->Ap.Channel,
        .Sequence = Station->SharedPeerChannels,
        .LocalAtRx = Station->SharedPeerSyncAt,
        .NextWindowSequence = Station->SharedPeerWindowSequence,
        .UntilNextSlotTu = Station->SharedPeerUntilNextSlotTu
    };
    ULONGLONG phase;
    return IsPeerChannelWindow(&ref, Now, &phase);
}

static bool IsSharedReceiveSynAck(const MT7922_AWDL_LINK *Link, ULONG Bytes)
{
    const UCHAR *ip = Link->Packet;
    return Link->AnnounceMdns && Bytes >= 60 && ip[6] == 6 &&
        ReadBigEndian16(ip + 40) == Link->ReceivePort &&
        (ip[53] & (AWDL_TCP_SYN | AWDL_TCP_ACK | AWDL_TCP_RST)) == (AWDL_TCP_SYN | AWDL_TCP_ACK);
}

static void ProgramSharedHePeer(MT7922_MCU_STATION *Station, bool Upload)
{
    enum { PhyBytes = 12, HeBytes = 28, HeV2Bytes = 28, WtblPhyBytes = 36 };
    const UCHAR port = Station->SharedBaPort;
    UCHAR payload[8 + PhyBytes + HeBytes + HeV2Bytes + WtblPhyBytes] =
        {port, STATION_SHARED_BA_WCID, 3, 0, 1, port};
    UCHAR *phy = payload + 8;
    UCHAR *he = phy + PhyBytes;
    UCHAR *heV2 = he + HeBytes;
    MT7922_AWDL_LINK *link = Station->SharedLink;

    /* AWDL Data Path State identifies HE support but carries no RX MCS map.
       An explicit upload profile allows MCS 0-11 on both streams for a verified peer. */
    const USHORT defaultPeerMcs = 0xfffcU; /* One stream, MCS 0-7. */
    const bool he2Stream = Upload && link->UploadHe2Stream && Station->Info.NicSpatialStreams == 2;
    const USHORT peerMcs80 = he2Stream ? 0xfffaU : defaultPeerMcs;
    const AWDL_PEER_PHY *peer = &link->UploadPeerPhy;
    const BOOLEAN amsdu = Upload ? link->FastUpload :
        link->ReceiveAmsdu && (link->SharedAddba.Params & 1U) != 0;

    /* STA_REC_PHY, STA_REC_HE and STA_REC_HE_V2 follow the mt76 Connac2 layout. */
    WriteLittleEndian16(phy, 0x15);
    WriteLittleEndian16(phy + 2, PhyBytes);
    WriteLittleEndian16(phy + 4, 0x0540);
    phy[6] = (1U << 3) | (1U << 6); /* OFDM and HE. */
    WriteLittleEndian16(he, 0x0e);
    WriteLittleEndian16(he + 2, HeBytes);
    WriteLittleEndian32(he + 4, (1U << 6) | (amsdu ? 1U << 4 : 0)); /* LDPC, A-MSDU in A-MPDU. */
    he[10] = 2; /* 2 suppresses a 40 MHz rung; 0x04 measured a 20->40->80 ramp in 0.70.0.75. */
    he[18] = 2; /* 16 us nominal packet padding. */
    WriteLittleEndian16(he + 20, peerMcs80);
    WriteLittleEndian16(heV2, 0x19);
    WriteLittleEndian16(heV2 + 2, HeV2Bytes);
    heV2[8] = amsdu ? 0x40 : 0;
    heV2[10] = 0x04; /* 5 GHz 40/80 MHz. */
    heV2[11] = 0x20; /* LDPC. */
    heV2[21] = 2;
    WriteLittleEndian16(heV2 + 22, peerMcs80);

    ULONG bytes = 8 + PhyBytes + HeBytes + HeV2Bytes;
    if (Upload) {
        /* As in mt76, report the peer's HT parameters to STA_REC_PHY and
           let its larger VHT factor raise the WTBL aggregation limit. */
        const UCHAR htAmpdu = peer->HtPresent ? peer->HtAmpdu : 0x1dU; /* 16 KiB, 16 us fallback. */
        const UCHAR htFactor = htAmpdu & 3U;
        const UCHAR vhtFactor = (UCHAR)((peer->VhtCapabilities >> 23) & 7U);
        const UCHAR wtblFactor = max(htFactor, vhtFactor);
        const UCHAR spacing = htAmpdu >> 2;
        phy[6] |= (1U << 4) | (1U << 5); /* HT and VHT. */
        phy[7] = htAmpdu;
        const UCHAR wtbl[WtblPhyBytes] = {
            13, 0, WtblPhyBytes, 0, STATION_SHARED_BA_WCID, 2, 2, 0, 0, 0, 0, 0,
            2, 0, 12, 0, 1, 0, wtblFactor, spacing, 0, 0, 0, 0,
            3, 0, 12, 0, 0, 0, 1 /* WTBL_VHT */
        };
        RtlCopyMemory(payload + bytes, wtbl, sizeof(wtbl));
        ++payload[2];
        bytes += sizeof(wtbl);
    }

    NTSTATUS status = ExchangeUnifiedWait(Station->Scan.Rings, MCU_STA_REC, payload,
        bytes, &Station->Info, AWDL_RX_BA_REPLY_MS, false);
    if (Upload && NT_SUCCESS(status)) {
        /* Publish the 5 GHz HE peer's conservative HT/VHT fallback and
           its 80 MHz operating width in the rate-control station record.
           Keep this update separate to fit the unified command buffer. */
        enum { HtBytes = 8, VhtBytes = 16, RateBytes = 16, StateBytes = 12 };
        UCHAR rates[8 + HtBytes + VhtBytes + RateBytes + StateBytes] =
            {port, STATION_SHARED_BA_WCID, 4, 0, 1, port};
        UCHAR *ht = rates + 8;
        UCHAR *vht = ht + HtBytes;
        UCHAR *ra = vht + VhtBytes;
        UCHAR *state = ra + RateBytes;

        WriteLittleEndian16(ht, 9);
        WriteLittleEndian16(ht + 2, HtBytes);
        WriteLittleEndian16(ht + 4, peer->HtPresent ? peer->HtCapabilities : 1U << 1);
        WriteLittleEndian16(vht, 10);
        WriteLittleEndian16(vht + 2, VhtBytes);
        WriteLittleEndian32(vht + 4, peer->VhtCapabilities);
        WriteLittleEndian16(vht + 8, peer->VhtPresent ? peer->VhtRxMcs : defaultPeerMcs);
        WriteLittleEndian16(vht + 10, peer->VhtPresent ? peer->VhtTxMcs : defaultPeerMcs);
        WriteLittleEndian16(ra, 1);
        WriteLittleEndian16(ra + 2, RateBytes);
        WriteLittleEndian16(ra + 4, 0x3fc0); /* Legacy OFDM rates. */
        ra[6] = peer->HtPresent ? peer->HtRxMcs[0] : 0xff;
        ra[7] = Station->Info.NicSpatialStreams == 2 ? peer->HtRxMcs[1] : 0;
        WriteLittleEndian16(state, 7);
        WriteLittleEndian16(state + 2, StateBytes);
        state[8] = 2; /* Associated. */
        state[9] = 2; /* 80 MHz. */
        if (peer->VhtPresent && (peer->VhtRxMcs & 0x0cU) != 0x0cU &&
            Station->Info.NicSpatialStreams == 2) {
            state[9] |= 1U << 4; /* Two VHT receive streams. */
        }
        status = ExchangeUnifiedWait(Station->Scan.Rings, MCU_STA_REC, rates,
            sizeof(rates), &Station->Info, AWDL_RX_BA_REPLY_MS, false);
    }
    link->HeReport.FirmwareStatus = status;
    link->HeReport.FirmwareConfigured = NT_SUCCESS(status);
}

static NTSTATUS RegisterSharedBaPeer(MT7922_MCU_STATION *Station)
{
    if (Station->SharedBaPeerRegistered) {
        return STATUS_SUCCESS;
    }
    RtlCopyMemory(Station->SharedBaPeer, Station->SharedLinkPeer, sizeof(Station->SharedBaPeer));
    /* A timed-out command may still have touched the reserved entry. */
    Station->SharedBaPeerRegistered = true;
    return ProgramPeerRecord(Station->Scan.Rings, &Station->Info, Station->SharedBaPeer,
        STATION_SHARED_BA_WCID, 1, false, true, true, Station->SharedBaPort, AWDL_RX_BA_REPLY_MS);
}

static void PrepareSharedReceiveBa(MT7922_MCU_STATION *Station)
{
    MT7922_AWDL_ADDBA *addba = &Station->SharedLink->SharedAddba;
    MT7922_MCU_PROBE_INFO *info = &Station->Info;
    MCU_RINGS *rings = Station->Scan.Rings;
    if (!NT_SUCCESS(Station->SharedBaCleanupStatus) || info->RxBaAttempts >= AWDL_ADDBA_MAX_RESPONSES) {
        addba->Active = FALSE;
        return;
    }
    ++info->RxBaAttempts;
    NTSTATUS status = STATUS_SUCCESS;
    if (!Station->SharedBaPeerRegistered) {
        status = RegisterSharedBaPeer(Station);
        info->RxBaFirmwareReply = info->MonitorReply;
        if (NT_SUCCESS(status) && Station->SharedLink->AdvertiseHe &&
            Station->SharedLink->HeReport.PeerSupported) {
            ProgramSharedHePeer(Station, false);
        }
    }
    if (NT_SUCCESS(status)) {
        status = ConfigureReceiveBa(rings, info, Station->SharedBaPeer, STATION_SHARED_BA_WCID,
            addba, Station->SharedLink->ReceiveAmsdu);
    }
    info->RxBaLastStatus = status;
    if (NT_SUCCESS(status)) {
        ++info->RxBaConfigured;
        addba->HardwareReady = TRUE;
    } else {
        AWDL_SYNC_REF ref = SharedReceiveRef(Station);
        if (ref.Link->Reorder.active) {
            StopBaReorder(info, &ref);
        }
        addba->Active = FALSE;
        Station->SharedBaResetPending = true;
    }
}

static void FallbackUploadBa(MT7922_MCU_STATION *Station)
{
    MT7922_AWDL_LINK *link = Station->SharedLink;
    link->UploadBa.State = AwdlTxBaFallback;
    link->AdvertiseHe = FALSE;
    link->AdvertiseVht = FALSE;
    Station->SharedBaResetPending = Station->SharedBaPeerRegistered || Station->SharedBaPort != 0;
}

static bool PrepareSharedUploadBa(MT7922_MCU_STATION *Station)
{
    MT7922_AWDL_LINK *link = Station->SharedLink;
    link->UploadBaReport.PeerHe = link->HeReport.PeerSupported;
    NTSTATUS status = Station->SharedBaCleanupStatus;
    if (!NT_SUCCESS(status) || Station->SharedBaResetPending || Station->Ap.Center80 == 0 ||
        !link->HeReport.PeerSupported || !(Station->Info.NicPhyFlags & MT7922_NIC_PHY_HE)) {
        link->UploadBaReport.FirmwareStatus = NT_SUCCESS(status) ? STATUS_NOT_SUPPORTED : status;
        FallbackUploadBa(Station);
        return false;
    }
    /* Compare the isolated upload BSS only during a diagnostic transfer. */
    if (link->FastUpload || link->AckTrace != NULL) {
        Station->SharedBaPort = STATION_AWDL_UPLOAD_PORT;
        status = ConfigureAwdlUploadPort(Station, true);
    }
    if (NT_SUCCESS(status)) {
        status = RegisterSharedBaPeer(Station);
    }
    if (NT_SUCCESS(status)) {
        ProgramSharedHePeer(Station, true);
        status = link->HeReport.FirmwareStatus;
    }
    link->UploadBaReport.FirmwareStatus = status;
    if (link->AckTrace != NULL && Station->SharedBaPeerRegistered) {
        ReadUploadWtblSnapshot(Station->Scan.Rings, Station->SharedLinkPeer,
            &link->UploadWtblReport.Before);
    }
    if (!NT_SUCCESS(status)) {
        FallbackUploadBa(Station);
        return false;
    }

    const USHORT previous = Station->Scan.Rings->StationSequence;
    link->AdvertiseHe = TRUE;
    link->AdvertiseVht = TRUE;
    link->UploadBa.Token = (UCHAR)(1U + previous % 255U);
    link->UploadBa.StartingSequence = (USHORT)((previous + 1U) & 0xfffU);
    link->UploadBa.NextSequence = previous;
    link->UploadBa.DataSequence = (USHORT)(link->Submitted + link->SharedMifReleased + 1U);
    link->UploadBaReport.PeerStatus = MAXULONG;
    link->UploadBa.State = AwdlTxBaPrepared;
    link->UploadBa.Requests = 0;
    link->UploadBa.PreparedAt = AwdlHostTime();
    link->UploadBa.Deadline = link->UploadBa.PreparedEarly ? 0 :
        link->UploadBa.PreparedAt + STATION_SHARED_UPLOAD_ADDBA_WAIT_MS * 10000ULL;
    return true;
}

static void EnableSharedUploadBa(MT7922_MCU_STATION *Station)
{
    MT7922_AWDL_LINK *link = Station->SharedLink;
    const MT7922_AWDL_ADDBA agreed = {
        .Params = (USHORT)((link->UploadBa.Window << 6) | 2U | (link->UploadBa.Amsdu ? 1U : 0U)),
        .StartingSequence = link->UploadBa.StartingSequence
    };
    const NTSTATUS status = ConfigurePeerBa(Station->Scan.Rings, &Station->Info,
        Station->SharedBaPeer, STATION_SHARED_BA_WCID, &agreed, link->UploadBa.Amsdu, true, Station->SharedBaPort,
        link->AckTrace != NULL ? &link->UploadWtblReport : NULL);
    link->UploadBaReport.FirmwareStatus = status;
    if (NT_SUCCESS(status) && link->UploadBa.State == AwdlTxBaAccepted) {
        link->UploadBa.State = AwdlTxBaOperational;
        if (link->UploadBa.PreparedEarly) {
            link->UploadBa.EarlyActivityAt = AwdlHostTime();
        }
    } else {
        FallbackUploadBa(Station);
    }
}

static void ObserveSharedAckWait(MT7922_AWDL_LINK *Link, UCHAR Flags)
{
    if (Link->ObserveAckWait != NULL) {
        Link->ObserveAckWait(Link->Context, Flags, AwdlHostTime());
    }
}

static void SubmitSharedLinkTx(MT7922_MCU_STATION *Station, bool Upload, bool EarlyData)
{
    MCU_RINGS *rings = Station->Scan.Rings;
    MT7922_AWDL_LINK *link = Station->SharedLink;
    TX_RING *ring = &rings->ActionTx;
    const bool sendData = Upload || EarlyData;
    const bool ampdu = sendData && link->UploadBa.State == AwdlTxBaOperational;
    const bool uploadAmsdu = ampdu && Upload && link->UploadBa.Amsdu;
    const uint32_t peerMpduLength = link->UploadPeerPhy.VhtCapabilities & 3U;
    /* VHT Max MPDU encodings 1/2 permit 7991/11454 bytes. */
    const bool uploadAmsdu3 = uploadAmsdu && link->UploadAmsdu3 && link->HeReport.FirmwareConfigured &&
        link->UploadPeerPhy.VhtPresent && (peerMpduLength == 1U || peerMpduLength == 2U);
    const bool usePeerWcid = (ampdu || (!sendData && link->PeerWcidAck && link->SharedAddba.HardwareReady)) &&
        Station->SharedBaPeerRegistered && !Station->SharedBaResetPending &&
        RtlCompareMemory(Station->SharedBaPeer, Station->SharedLinkPeer, sizeof(Station->SharedBaPeer)) ==
            sizeof(Station->SharedBaPeer);

    /* Bound outstanding bulk frames to leave airtime for WLAN and control. */
    ULONG limit = Upload ? STATION_SHARED_UPLOAD_IN_FLIGHT : STATION_SHARED_ACK_SLOTS;
    if (ampdu) {
        const ULONG uploadLimit = link->FastUpload ? STATION_SHARED_UPLOAD_FAST_IN_FLIGHT :
            STATION_SHARED_UPLOAD_AGGREGATE_IN_FLIGHT;
        limit = min(EarlyData ? STATION_SHARED_UPLOAD_EARLY_IN_FLIGHT : uploadLimit, link->UploadBa.Window);
    }
    /* Publish ready aggregate data together so firmware sees the available burst.
       Control traffic and pre-upload traffic retain their immediate notification. */
    const bool batch = Upload && !EarlyData && ampdu && link->FastUpload && usePeerWcid;
    bool staged = false;
    for (ULONG i = 0; i < limit && rings->SharedAckInFlight < limit; ++i) {
        STATION_SHARED_ACK_TX *slot = &rings->SharedAckTx[i];
        if (slot->SubmittedAt != 0) {
            continue;
        }

        const ULONGLONG now = AwdlHostTime();
        const bool regularWindow = now <= Station->SharedLinkDataUntil;
        if (!regularWindow && !IsStationPeerChannelWindow(Station, now)) {
            ObserveSharedAckWait(link, MT7922_ACK_WAIT_WINDOW);
            break;
        }
        const ULONG device = ReadRegister(rings->Registers, ring->Registers + RING_DEVICE);
        ObserveSharedAckDma(rings, device);
        if (device >= ring->Count || (ring->Producer + 1U) % ring->Count == device) {
            ObserveSharedAckWait(link, MT7922_ACK_WAIT_DMA);
            break;
        }

        USHORT rate = 0;
        ULONGLONG enqueuedAt = 0;
        MT7922_ACK_WAIT_SUMMARY wait = {0};
        const ULONG bytes = sendData ? link->TakeSend(link->Context, link->Packet, &rate, &enqueuedAt) :
            link->TakePureAck(link->Context, link->Packet, &rate, &enqueuedAt, &wait);
        if (bytes == 0) {
            break;
        }

        USHORT nextRate = 0;
        const ULONG secondBytes = uploadAmsdu ?
            link->TakeSend(link->Context, link->SecondUploadPacket, &nextRate, NULL) : 0;
        const ULONG thirdBytes = uploadAmsdu3 && secondBytes != 0 ?
            link->TakeSend(link->Context, link->ThirdUploadPacket, &nextRate, NULL) : 0;
        const USHORT sequence = ampdu && link->FastUpload ? link->UploadBa.DataSequence :
            (USHORT)(link->Submitted + link->SharedMifReleased + 1U);
        size_t frameBytes;
        if (secondBytes != 0) {
            frameBytes = AwdlBuildQosAmsduFrame(link->Frame, sizeof(link->Frame), Station->SharedLinkPeer,
                Station->Info.NicMac, link->Packet, bytes, link->SecondUploadPacket, secondBytes,
                link->ThirdUploadPacket, thirdBytes, sequence);
        } else if (ampdu) {
            frameBytes = AwdlBuildQosIpFrame(link->Frame, sizeof(link->Frame), Station->SharedLinkPeer,
                Station->Info.NicMac, link->Packet, bytes, sequence);
        } else {
            frameBytes = AwdlBuildIpFrame(link->Frame, sizeof(link->Frame), Station->SharedLinkPeer,
                Station->Info.NicMac, link->Packet, bytes, sequence);
        }
        const ULONG offset = UNICAST_TX_OFFSET + (i + 1U) * STATION_SHARED_TX_STRIDE;
        const USHORT defaultRate = Upload ? MT7922_OFDM_24_MBPS : MT7922_OFDM_6_MBPS;
        MT7922_AWDL_TX tx = {
            .Wcid = usePeerWcid ? STATION_SHARED_BA_WCID : 0,
            .Token = (USHORT)(STATION_SHARED_TX_TOKEN_BASE + i),
            .PacketId = (UCHAR)(STATION_SHARED_TX_PID_BASE + i),
            .RateCode = ampdu ? 0 : rate != 0 ? rate : defaultRate,
            .Ampdu = ampdu
        };
        UCHAR txwi[MT7922_TXWI_BYTES];
        if (frameBytes == 0 || !BuildStationSharedTxwi(Station, txwi, link->Frame, frameBytes, offset, &tx)) {
            Station->SharedLinkStatus = STATUS_DATA_ERROR;
            break;
        }

        /* The doorbell write can succeed even if its readback fails. Hold the slot until TX-free. */
        slot->Sequence = ampdu ? link->UploadBa.NextSequence : rings->StationSequence;
        slot->Wcid = tx.Wcid;
        slot->SubmittedAt = now;
        if (link->AckTrace != NULL && bytes >= 60 && link->Packet[6] == 6) {
            if (link->AckTraceCount < MT7922_AWDL_ACK_TRACE_MAX) {
                slot->Trace = &link->AckTrace[link->AckTraceCount++];
                slot->Trace->EnqueuedAt = enqueuedAt;
                slot->Trace->Wait = wait;
                slot->Trace->TcpSequence = ReadBigEndian32(link->Packet + 44);
                slot->Trace->TcpAck = ReadBigEndian32(link->Packet + 48);
                slot->Trace->PeerPort = ReadBigEndian16(link->Packet + (Upload ? 40 : 42));
                slot->Trace->WlanSequence = slot->Sequence;
                slot->Trace->Queue = tx.Queue;
                slot->Trace->Wcid = (UCHAR)tx.Wcid;
                slot->Trace->RateCode = tx.RateCode;
            } else {
                ++link->AckTraceOmitted;
            }
        }
        if (ampdu && link->FastUpload) {
            link->UploadBa.DataSequence = (USHORT)(sequence + 1U + (secondBytes != 0) + (thirdBytes != 0));
        }
        ++rings->SharedAckInFlight;
        ++link->SharedAckReport.Submitted;
        link->UploadBaReport.EligibleFrames += ampdu;
        if (EarlyData) {
            link->UploadBa.EarlyActivityAt = now;
        }
        link->SharedAckReport.PeakInFlight = max(link->SharedAckReport.PeakInFlight, rings->SharedAckInFlight);
        ++link->Submitted;
        link->UploadWindowReplies += !regularWindow;
        if (slot->Trace != NULL) {
            if (rings->SharedAckDmaPending == 0) {
                rings->SharedAckDmaCursor = device;
            }
            rings->SharedAckDmaTrace[ring->Producer] = slot->Trace;
            ++rings->SharedAckDmaPending;
            slot->Trace->SubmittedAt = AwdlHostTime();
        }
        const NTSTATUS status = PostStationRawTxwi(Station, txwi, link->Frame, frameBytes, offset, batch);
        if (!NT_SUCCESS(status)) {
            Station->SharedLinkStatus = status;
            break;
        }
        staged = batch;
    }
    if (staged) {
        const NTSTATUS status = PublishPackets(rings, ring, &Station->Info, false);
        if (!NT_SUCCESS(status)) {
            Station->SharedLinkStatus = status;
        }
    }
    if (rings->SharedAckInFlight == STATION_SHARED_ACK_SLOTS) {
        ObserveSharedAckWait(link, MT7922_ACK_WAIT_SLOTS);
    }
}

static void SampleUploadBandwidth(MT7922_MCU_STATION *Station, ULONGLONG Now)
{
    MT7922_AWDL_LINK *link = Station->SharedLink;
    MT7922_AWDL_UPLOAD_BW_REPORT *report = &link->UploadBandwidthReport;
    if (!link->FastUpload || link->AckTrace == NULL || link->UploadBa.State != AwdlTxBaOperational ||
        InterlockedCompareExchange(&link->SharedUploadActive, 0, 0) == 0 ||
        link->UploadBaReport.EligibleFrames == 0 || report->Count == MT7922_AWDL_UPLOAD_BW_SAMPLES ||
        Now < link->NextUploadBandwidthSample) {
        return;
    }

    const MCU_RINGS *rings = Station->Scan.Rings;
    const ULONG base = LMAC_WTBL_OFFSET + STATION_SHARED_BA_WCID * LMAC_WTBL_STRIDE;
    MT7922_AWDL_UPLOAD_BW_SAMPLE *sample = &report->Samples[report->Count++];
    sample->SampledAt = Now;
    sample->InFlight = rings->SharedAckInFlight;
    RtlCopyMemory(sample->HeTxByBandwidth, link->UploadHeTxByBandwidth, sizeof(sample->HeTxByBandwidth));
    if (UploadWtblMatchesPeer(rings->Registers, base, Station->SharedLinkPeer)) {
        sample->PeerMatched = 1;
        sample->Dw9 = ReadRegister(rings->Registers, base + 9 * sizeof(ULONG));
        sample->Dw10 = ReadRegister(rings->Registers, base + 10 * sizeof(ULONG));
        sample->Dw16 = ReadRegister(rings->Registers, base + 16 * sizeof(ULONG));
        sample->Dw17 = ReadRegister(rings->Registers, base + 17 * sizeof(ULONG));
        sample->Dw18 = ReadRegister(rings->Registers, base + 18 * sizeof(ULONG));
        sample->Dw19 = ReadRegister(rings->Registers, base + 19 * sizeof(ULONG));
    }
    link->NextUploadBandwidthSample = Now + 100ULL * 10000ULL;
}

static void ServiceStationSharedLink(MT7922_MCU_STATION *Station, bool StationTxWaiting)
{
    MT7922_AWDL_LINK *link = Station->SharedLink;
    MT7922_SHARED_ADVERT_DEBUG *tx = &Station->SharedLinkTx;
    MCU_RINGS *rings = Station->Scan.Rings;
    const ULONGLONG now = AwdlHostTime();
    if (!NT_SUCCESS(Station->SharedLinkStatus) || link->Stopped(link->Context)) {
        return;
    }
    SampleUploadBandwidth(Station, now);
    const bool regularWindow = now <= Station->SharedLinkDataUntil;
    /* Owly AWDL-16: a fixed-channel radio can drain queued frames
       whenever its channel matches the peer's synchronized slot. */
    const bool peerWindow = Station->SharedLinkReady &&
        IsStationPeerChannelWindow(Station, now);
    const bool traceAddbaWindow = link->AckTrace != NULL && regularWindow &&
        link->UploadBa.State == AwdlTxBaPrepared && link->UploadBa.Deadline != 0 &&
        Station->SharedPeerSyncAt > link->UploadBa.PreparedAt;
    if (tx->Submitted) {
        if (!tx->Released) {
            if (traceAddbaWindow) {
                link->UploadAddbaTiming.Deferrals |= MT7922_ACK_WAIT_AWDL_TX;
            }
            ObserveSharedAckWait(link, MT7922_ACK_WAIT_AWDL_TX);
            if (now - Station->SharedLinkSubmittedAt >= 10000000ULL) {
                CheckStationSharedDma(rings);
                link->UploadTxReport.TimeoutKind = MT7922_AWDL_UPLOAD_TX_CONTROL_TIMEOUT;
                link->UploadTxReport.TimeoutSequence = rings->SharedAdvertSequence;
                link->UploadTxReport.TimeoutAgeUs = (ULONG)((now - Station->SharedLinkSubmittedAt) / 10U);
                Station->SharedLinkStatus = tx->DmaFetched ? STATUS_IO_TIMEOUT : STATUS_DEVICE_HARDWARE_ERROR;
            }
            return;
        }
        if (Station->SharedLinkMifInFlight) {
            if (tx->ReleaseStatus == 0) {
                ++link->SharedMifReleased;
                link->VhtAdverts += link->AdvertiseVht != FALSE;
                link->HeReport.Adverts += link->AdvertiseHe != FALSE;
                link->ServiceMifsReleased += Station->SharedLinkServiceMifInFlight;
                if (!Station->SharedLinkReady) {
                    UCHAR local[16], peer[16];
                    LinkLocalFromMac(Station->Info.NicMac, local);
                    LinkLocalFromMac(Station->SharedLinkPeer, peer);
                    Station->SharedLinkReady = true;
                    link->Ready(link->Context, local, peer);
                }
            }
        } else if (Station->SharedLinkMulticastInFlight) {
            link->MdnsAnnounced += tx->ReleaseStatus == 0 && link->AnnounceMdns;
        } else {
            const bool missingReport = tx->ReleaseStatus == 0 && !tx->TxReportSeen;
            /* TX-free owns buffer lifetime. Preserve receive reply airtime
               by advancing immediately after release. */
            if (missingReport && (!link->AnnounceMdns || Station->SharedBaReplyInFlight)) {
                if (Station->SharedLinkReleasedAt == 0) {
                    Station->SharedLinkReleasedAt = now;
                }
                if (now - Station->SharedLinkReleasedAt < MT7922_TX_STATUS_GRACE_MS * 10000ULL) {
                    if (traceAddbaWindow) {
                        link->UploadAddbaTiming.Deferrals |= MT7922_ACK_WAIT_AWDL_TX;
                    }
                    ObserveSharedAckWait(link, MT7922_ACK_WAIT_AWDL_TX);
                    return;
                }
            }
            const bool acked = tx->ReleaseStatus == 0 && tx->TxReportSeen && tx->TxAckErrors == 0;
            if (link->UploadBa.RequestInFlight) {
                link->UploadBa.RequestInFlight = FALSE;
                ++link->UploadWtblReport.RequestsCompleted;
                link->UploadWtblReport.RequestsAcked += acked;
            }
            if (Station->SharedBaReplyInFlight) {
                Station->Info.AddbaAcked += acked;
                Station->SharedBaReplyInFlight = false;
                tx->Submitted = 0;
                return;
            }
            link->Acked += acked;
            link->NoAck += !acked && !missingReport;
            link->SharedTxStatusMissing += missingReport;
            if (Station->SharedSynAckLength != 0) {
                link->SharedSynAckAcked += acked;
                link->SharedSynAckFailed += !acked && !missingReport;
                Station->SharedSynAckRetryPending = !acked && !missingReport && !Station->SharedSynAckRetried;
                if (!Station->SharedSynAckRetryPending) {
                    Station->SharedSynAckLength = 0;
                }
            }
            if (tx->TxReportSeen) {
                link->ReportRate = tx->TxRateCode;
            }
        }
        tx->Submitted = 0;
    }
    const UCHAR blocked = ((StationTxWaiting || rings->StationTxPending) ? MT7922_ACK_WAIT_WLAN_TX : 0) |
        (rings->TransmitPending ? MT7922_ACK_WAIT_DMA : 0) |
        (rings->SharedAdvertDmaPending ? MT7922_ACK_WAIT_AWDL_TX : 0) |
        ((!regularWindow && !peerWindow) ? MT7922_ACK_WAIT_WINDOW : 0);
    if (blocked != 0) {
        if (traceAddbaWindow) {
            link->UploadAddbaTiming.Deferrals |= blocked;
        }
        ObserveSharedAckWait(link, blocked);
        return;
    }

    if (Station->SharedSynAckRetryPending &&
        now - Station->SharedLinkSubmittedAt > 2ULL * PSF_INTERVAL_TICKS) {
        Station->SharedSynAckRetryPending = false;
        Station->SharedSynAckLength = 0;
    }
    const bool retrySynAck = Station->SharedSynAckRetryPending;
    /* Give a failed TCP reply one more chance on fresh synchronization.
       The original buffer is reusable only after its TX-free notification. */
    if (retrySynAck && (!regularWindow || Station->SharedPeerSyncAt <= Station->SharedLinkSubmittedAt)) {
        ObserveSharedAckWait(link, MT7922_ACK_WAIT_CONTROL);
        return;
    }
    MT7922_AWDL_ADDBA *addba = &link->SharedAddba;
    const bool addbaReply = !retrySynAck && Station->SharedLinkReady && addba->Active &&
        !Station->SharedBaResetPending && addba->Attempts < AWDL_ADDBA_MAX_RESPONSES;
    const bool mif = !retrySynAck && !addbaReply && regularWindow && Station->SharedAdvertFrameBytes != 0 &&
        now <= Station->SharedAdvertWindowEnd;
    const bool browse = !mif && regularWindow && !link->AnnounceMdns && link->ReceivePort != 0 &&
        Station->Info.MdnsAirDropAnswers == 0 && Station->Info.MdnsQueriesSent < AWDL_MDNS_MAX_QUERIES &&
        now >= Station->SharedLinkNextMdns;
    const bool upload = InterlockedCompareExchange(&link->SharedUploadActive, 0, 0) != 0;
    MT7922_AWDL_TX_BA *uploadBa = &link->UploadBa;
    if (upload && uploadBa->PreparedEarly && uploadBa->State == AwdlTxBaOperational &&
        now - uploadBa->EarlyActivityAt >= STATION_SHARED_UPLOAD_BA_IDLE_TU * (ULONGLONG)TU_TICKS) {
        /* The peer may have expired the early BA while waiting for user acceptance. */
        FallbackUploadBa(Station);
    }
    if (upload && uploadBa->PreparedEarly && uploadBa->State == AwdlTxBaFallback &&
        NT_SUCCESS(Station->SharedBaCleanupStatus)) {
        if (Station->SharedBaResetPending) {
            return;
        }
        *uploadBa = (MT7922_AWDL_TX_BA){0};
    } else if (upload && uploadBa->State == AwdlTxBaOperational) {
        uploadBa->PreparedEarly = FALSE;
    }
    const bool prepareEarly = !upload && link->PrepareUploadEarly && !Station->SharedBaResetPending &&
        NT_SUCCESS(Station->SharedBaCleanupStatus) && Station->Ap.Center80 != 0 &&
        link->HeReport.PeerSupported && (Station->Info.NicPhyFlags & MT7922_NIC_PHY_HE) != 0;
    const bool manageUploadBa = upload || prepareEarly;
    const bool mayRequestUploadBa = upload || (prepareEarly &&
        InterlockedCompareExchange(&link->UploadDiscovered, 0, 0) != 0);
    if (mayRequestUploadBa && uploadBa->State == AwdlTxBaPrepared && uploadBa->Deadline == 0) {
        /* /Discover proved that the peer's service is listening. Start the BA timeout now. */
        uploadBa->Deadline = now + STATION_SHARED_UPLOAD_ADDBA_WAIT_MS * 10000ULL;
    }
    const bool expired = uploadBa->Deadline != 0 && now >= uploadBa->Deadline;
    const bool waitingResponse = uploadBa->State == AwdlTxBaWaiting && expired;
    if (mayRequestUploadBa && expired && (uploadBa->State == AwdlTxBaPrepared ||
        (waitingResponse && uploadBa->Requests >= STATION_SHARED_UPLOAD_ADDBA_ATTEMPTS))) {
        link->UploadAddbaTiming.ExpiredBeforeSubmit = uploadBa->State == AwdlTxBaPrepared;
        FallbackUploadBa(Station);
    }
    const bool idleControl = !retrySynAck && !addbaReply && !mif && !browse && Station->SharedLinkReady;
    if (traceAddbaWindow && !idleControl) {
        link->UploadAddbaTiming.Deferrals |= (mif ? MT7922_ADDBA_DEFER_MIF : 0U) |
            (browse ? MT7922_ADDBA_DEFER_MDNS : 0U) |
            ((retrySynAck || addbaReply) ? MT7922_ACK_WAIT_CONTROL : 0U);
    }
    if (manageUploadBa && idleControl && uploadBa->State == AwdlTxBaIdle) {
        uploadBa->PreparedEarly = prepareEarly;
        /* Programming can outlast the peer window used to reach this worker.
           Re-evaluate the window on the next pass before posting ADDBA. */
        PrepareSharedUploadBa(Station);
        return;
    }
    if (manageUploadBa && idleControl && uploadBa->State == AwdlTxBaAccepted) {
        EnableSharedUploadBa(Station);
        return;
    }
    const bool requestUploadBa = mayRequestUploadBa && idleControl && regularWindow &&
        ((uploadBa->State == AwdlTxBaPrepared && Station->SharedPeerSyncAt > uploadBa->PreparedAt) ||
            (uploadBa->State == AwdlTxBaWaiting && waitingResponse));
    const bool batch = idleControl && (!upload ||
        uploadBa->State == AwdlTxBaOperational || uploadBa->State == AwdlTxBaFallback);
    const bool earlyData = prepareEarly && uploadBa->State == AwdlTxBaOperational;
    if (batch && (upload || earlyData || link->TakePureAck != NULL)) {
        SubmitSharedLinkTx(Station, upload, earlyData);
    } else {
        ObserveSharedAckWait(link, MT7922_ACK_WAIT_CONTROL);
    }
    if (((upload || earlyData) && idleControl && !requestUploadBa) ||
        (!upload && rings->SharedAckInFlight != 0) ||
        !NT_SUCCESS(Station->SharedLinkStatus)) {
        return;
    }
    if (upload || requestUploadBa) {
        /* The control frame has a separate token and DMA buffer. It may pass
           outstanding data without waiting for the entire batch to be freed. */
        TX_RING *ring = &rings->ActionTx;
        const ULONG device = ReadRegister(rings->Registers, ring->Registers + RING_DEVICE);
        if (device >= ring->Count || (ring->Producer + 1U) % ring->Count == device) {
            if (requestUploadBa && traceAddbaWindow) {
                link->UploadAddbaTiming.Deferrals |= MT7922_ADDBA_DEFER_RING;
            }
            return;
        }
    }
    if (addbaReply && !addba->HardwareReady) {
        PrepareSharedReceiveBa(Station);
        return;
    }

    UCHAR *frame = Station->SharedAdvertFrame;
    size_t length = Station->SharedAdvertFrameBytes;
    USHORT rate = MT7922_OFDM_6_MBPS;
    bool multicast = false;
    if (retrySynAck) {
        frame = link->Frame;
        length = Station->SharedSynAckLength;
        rate = Station->SharedSynAckRate;
    } else if (requestUploadBa) {
        frame = link->Frame;
        length = AwdlBuildAddbaRequest(frame, sizeof(link->Frame), Station->SharedLinkPeer,
            Station->Info.NicMac, uploadBa->Token, STATION_SHARED_UPLOAD_BA_WINDOW,
            STATION_SHARED_UPLOAD_BA_IDLE_TU, uploadBa->StartingSequence, link->FastUpload);
    } else if (addbaReply) {
        frame = link->Frame;
        const USHORT params = link->ReceiveAmsdu ? addba->Params : (USHORT)(addba->Params & ~1U);
        length = AwdlBuildAddbaResponse(frame, sizeof(link->Frame), Station->SharedLinkPeer,
            Station->Info.NicMac, addba->Token, params, addba->Timeout, 0);
    } else if (!mif) {
        if (!Station->SharedLinkReady) {
            return;
        }
        USHORT requested = 0;
        const ULONG packetBytes = browse
            ? BuildMdnsQuery(link->Packet, sizeof(link->Packet), Station->Info.NicMac, NULL)
            : link->TakeSend(link->Context, link->Packet, &requested, NULL);
        if (packetBytes == 0) {
            return;
        }
        if (requested != 0) {
            rate = requested;
        }
        multicast = AwdlIsMdnsMulticast(link->Packet, packetBytes) != 0;
        frame = link->Frame;
        length = AwdlBuildIpFrame(frame, sizeof(link->Frame),
            multicast ? AwdlMdnsMac : Station->SharedLinkPeer, Station->Info.NicMac,
            link->Packet, packetBytes, (USHORT)(link->Submitted + link->SharedMifReleased + 1U));
        if (length == 0) {
            Station->SharedLinkStatus = STATUS_DATA_ERROR;
            return;
        }
        if (!multicast && IsSharedReceiveSynAck(link, packetBytes)) {
            Station->SharedSynAckLength = length;
            Station->SharedSynAckRate = rate;
            Station->SharedSynAckRetried = false;
        }
    }

    *tx = (MT7922_SHARED_ADVERT_DEBUG){.RequestId = tx->RequestId + 1U, .Status = STATUS_PENDING};
    Station->SharedLinkMifInFlight = mif;
    Station->SharedLinkServiceMifInFlight = mif && Station->SharedAdvertHasServiceRecords;
    Station->SharedLinkMulticastInFlight = multicast;
    Station->SharedBaReplyInFlight = addbaReply;
    Station->SharedLinkSubmittedAt = now;
    Station->SharedLinkReleasedAt = 0;
    const ULONGLONG synAckSubmitAt = Station->SharedSynAckLength != 0 ? AwdlHostTime() : 0;
    const bool firstMdns = multicast && link->AnnounceMdns && !link->FirstMdnsTiming.Submitted;
    const ULONGLONG mdnsSubmitAt = firstMdns ? AwdlHostTime() : 0;
    const NTSTATUS status = PostStationSharedFrame(Station, frame, length, tx, false, rate,
        (addbaReply || requestUploadBa) ? STATION_SHARED_BA_WCID : 0);
    if (!NT_SUCCESS(status)) {
        Station->SharedLinkStatus = status;
        return;
    }
    if (requestUploadBa) {
        ++link->UploadBaReport.Requests;
        ++uploadBa->Requests;
        uploadBa->RequestInFlight = TRUE;
        uploadBa->Deadline = AwdlHostTime() + STATION_SHARED_UPLOAD_ADDBA_WAIT_MS * 10000ULL;
        uploadBa->State = AwdlTxBaWaiting;
        return;
    }
    if (addbaReply) {
        AWDL_SYNC_REF ref = SharedReceiveRef(Station);
        ref.Addba = *addba;
        StartBaReorder(&Station->Info, &ref);
        ++Station->Info.AddbaResponses;
        ++addba->Attempts;
        addba->Active = FALSE;
        return;
    }
    if (Station->SharedSynAckLength != 0) {
        if (link->SharedSynAckSubmitted < MT7922_AWDL_SYN_ACK_TIMING_MAX) {
            link->SharedSynAckTiming[link->SharedSynAckSubmitted] = (MT7922_AWDL_SYN_ACK_TIMING){
                .Sequence = rings->StationSequence,
                .PeerPhysicalTx = Station->SharedPeerPhysicalTx,
                .SyncAgeUs = (ULONG)((synAckSubmitAt - Station->SharedPeerSyncAt) / 10U),
                .WindowFlags = (regularWindow ? 1U : 0U) | (peerWindow ? 2U : 0U) |
                    (retrySynAck ? 4U : 0U) |
                    (rings->SharedAdvertQueue == MT7922_TX_QUEUE_ALTX0 ? MT7922_AWDL_SYN_ACK_ALTX0 : 0U)
            };
            rings->SharedSynAckQueueTiming = &link->SharedSynAckQueueTiming[link->SharedSynAckSubmitted];
            rings->SharedSynAckQueueSubmittedAt = synAckSubmitAt;
        }
        ++link->SharedSynAckSubmitted;
        link->SharedSynAckRetries += retrySynAck;
        Station->SharedSynAckRetried = retrySynAck;
        Station->SharedSynAckRetryPending = false;
    } else if (firstMdns) {
        link->FirstMdnsTiming.Submitted = 1;
        link->FirstMdnsTiming.Sequence = rings->StationSequence;
        link->FirstMdnsTiming.QueueToSubmitUs = (ULONG)((mdnsSubmitAt - link->FirstMdnsQueuedAt) / 10U);
        rings->SharedSynAckQueueTiming = &link->FirstMdnsTiming.Timing;
        rings->SharedSynAckQueueSubmittedAt = mdnsSubmitAt;
    }
    if (mif) {
        Station->SharedAdvertFrameBytes = 0;
        Station->SharedLinkNextMif = now + AWDL_SHARED_MIF_INTERVAL_MS * 10000ULL;
    } else {
        ++link->Submitted;
        link->UploadWindowReplies += peerWindow && !regularWindow && !multicast;
        if (browse) {
            ++Station->Info.MdnsQueriesSent;
            Station->SharedLinkNextMdns = now + 10000000ULL;
        }
    }
}

void Mt7922McuServiceSharedAdvert(MT7922_MCU_STATION *Station, bool StationTxWaiting)
{
    ReapSharedAckTx(Station);
    if (!StationTxWaiting) {
        ResetSharedBaPeerIfIdle(Station);
    }
    if (Station->SharedLink != NULL) {
        ServiceStationSharedLink(Station, StationTxWaiting);
        return;
    }
    if (Station->SharedAdvertDeadline == 0) {
        return;
    }
    MCU_RINGS *rings = Station->Scan.Rings;
    MT7922_SHARED_ADVERT_DEBUG *debug = &Station->ConnectDebug.SharedAdvert;
    const ULONGLONG now = KeQueryInterruptTime();
    if (debug->Released && !Station->SharedEchoActive) {
        if (!Station->SharedAdvertRequest.Echo || !NT_SUCCESS(debug->Status)) {
            Station->SharedAdvertDeadline = 0;
            return;
        }
        Station->SharedEchoActive = true;
        Station->SharedAdvertDeadline = now + STATION_SHARED_ADVERT_TICKS;
        Station->SharedAdvertFrameBytes = 0;
    }
    if (Station->SharedEchoActive) {
        debug = &Station->ConnectDebug.SharedEcho;
        if (debug->Released && Station->ConnectDebug.SharedEchoReplies != 0 &&
            debug->Status == STATUS_PENDING) {
            debug->Status = STATUS_SUCCESS;
        }
    }
    if (debug->Status != STATUS_PENDING) {
        Station->SharedAdvertDeadline = 0;
        return;
    }
    if (Station->KeyStateInvalid || Station->PeerDisconnectFrame[0] != 0) {
        debug->Status = STATUS_CANCELLED;
    } else if (now >= Station->SharedAdvertDeadline) {
        debug->Status = STATUS_IO_TIMEOUT;
    } else if (debug->Submitted || StationTxWaiting || rings->StationTxPending || rings->TransmitPending ||
        Station->SharedAdvertFrameBytes == 0 || AwdlHostTime() > Station->SharedAdvertWindowEnd) {
        return;
    } else {
        const NTSTATUS posted = PostStationSharedFrame(Station, Station->SharedAdvertFrame,
            Station->SharedAdvertFrameBytes, debug, Station->SharedEchoActive, MT7922_OFDM_6_MBPS, 0);
        debug->Status = NT_SUCCESS(posted) ? STATUS_PENDING : posted;
        Station->SharedAdvertFrameBytes = 0;
        if (NT_SUCCESS(posted)) {
            Station->SharedAdvertDeadline = now + (Station->SharedEchoActive ? 3ULL : 1ULL) * 10000000ULL;
            return;
        }
    }
    Station->SharedAdvertDeadline = 0;
    Station->SharedAdvertFrameBytes = 0;
}

typedef struct {
    MT7922_MCU_STATION *Station;
    MT7922_RECEIVE_STATION_DATA Receive;
    void *Context;
} STATION_DELIVERY;

static void ObserveUploadBaFrame(MT7922_MCU_STATION *Station, const MT7922_RX_FRAME *Frame)
{
    const UCHAR *bytes = Frame->Bytes;
    MT7922_AWDL_LINK *link = Station->SharedLink;
    MT7922_AWDL_TX_BA *ba = &link->UploadBa;
    if (Frame->Length < 20 ||
        RtlCompareMemory(bytes + 4, Station->Info.NicMac, 6) != 6 ||
        RtlCompareMemory(bytes + 10, Station->SharedLinkPeer, 6) != 6) {
        return;
    }
    if (bytes[0] == 0x94 && Frame->Length >= 28 && ba->State == AwdlTxBaOperational) {
        const USHORT control = (USHORT)(bytes[16] | (bytes[17] << 8));
        if ((control & 0xf006U) == 0x0004U) { /* Compressed Block Ack, TID 0. */
            ULONG marked = 0;
            for (ULONG i = 20; i < 28; ++i) {
                for (UCHAR bits = bytes[i]; bits != 0; bits &= (UCHAR)(bits - 1U)) {
                    ++marked;
                }
            }
            ++link->UploadBaReport.BlockAcks;
            link->UploadBaReport.LargestBitmap = max(marked, link->UploadBaReport.LargestBitmap);
        }
        return;
    }
    static const UCHAR bssid[] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    if (Frame->Length < 30 || bytes[0] != 0xd0 || (bytes[1] & 0xc3U) != 0 || bytes[24] != 3 ||
        RtlCompareMemory(bytes + 16, bssid, sizeof(bssid)) != sizeof(bssid)) {
        return;
    }
    if (Frame->Length >= 33 && bytes[25] == 1 && ba->State == AwdlTxBaWaiting && bytes[26] == ba->Token) {
        const USHORT status = (USHORT)(bytes[27] | (bytes[28] << 8));
        const USHORT params = (USHORT)(bytes[29] | (bytes[30] << 8));
        ++link->UploadBaReport.Responses;
        link->UploadBaReport.PeerStatus = status;
        if (status != 0 || (params & 0x003eU) != 2U || (params >> 6) == 0) {
            FallbackUploadBa(Station);
            return;
        }
        ba->Window = (USHORT)min(params >> 6, STATION_SHARED_UPLOAD_BA_WINDOW);
        ba->Amsdu = link->FastUpload && (params & 1U) != 0;
        ba->State = AwdlTxBaAccepted;
    } else if (bytes[25] == 2 && (bytes[27] & 0xf8U) == 0 &&
            (ba->State == AwdlTxBaOperational || ba->State == AwdlTxBaAccepted)) {
        FallbackUploadBa(Station);
    }
}

static void HandleSharedBaFrame(MT7922_MCU_STATION *Station, const MT7922_RX_FRAME *Frame,
    AWDL_SYNC_REF *Ref)
{
    static const UCHAR awdlBssid[] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    MT7922_AWDL_LINK *link = Ref->Link;
    const UCHAR *bytes = Frame->Bytes;
    if (Frame->Length < 20 || (bytes[0] != 0x84 &&
        (Frame->Length < 24 || bytes[0] != 0xd0 ||
            RtlCompareMemory(bytes + 16, awdlBssid, sizeof(awdlBssid)) != sizeof(awdlBssid)))) {
        return;
    }
    if (HandleBaReorderControl(&Station->Info, Frame, Ref)) {
        link->SharedAddba = (MT7922_AWDL_ADDBA){0};
        Station->SharedBaResetPending = Station->SharedBaPeerRegistered || Station->SharedBaPort != 0;
    }
    MT7922_AWDL_ADDBA request = link->SharedAddba;
    if (ReadAddbaRequest(&Station->Info, Frame, Ref->Peer, &request) && link->ReorderRequested) {
        const unsigned tid = (request.Params >> 2) & 0xfU;
        /* The existing reorder engine owns one session. Preserve an already negotiated TID. */
        if ((request.Params & 2U) != 0 && tid < 8 &&
            (!link->Reorder.active || link->ReorderTid == tid)) {
            link->SharedAddba = request;
        }
    }
}

static void ForwardStationData(void *Context, const uint8_t *Record, size_t Bytes)
{
    STATION_DELIVERY *target = Context;
    MT7922_MCU_STATION *station = target->Station;
    if (station->PeerDisconnectFrame[0] != 0) {
        return;
    }
    MT7922_RX_FRAME frame;
    const MT7922_RX_STATUS status = DecodeStationRx(station, Record, Bytes, &frame);
    if (station->Observer.Running) {
        Mt7922ObserverIngest(&station->Observer, status, &frame);
    }
    if (status != Mt7922RxRawFrame) {
        return;
    }
    if (station->SharedLink != NULL && frame.SkipFlags == 0) {
        if (!frame.HeaderTranslationError) {
            PrepareSharedAdvert(station, &frame);
        }
        AWDL_SYNC_REF ref = SharedReceiveRef(station);
        if (ref.Link->AnnounceMdns) {
            HandleSharedBaFrame(station, &frame, &ref);
        } else if (ref.LinkOnline && ref.Link->UploadBa.State != AwdlTxBaIdle) {
            ObserveUploadBaFrame(station, &frame);
        }
        if (ref.LinkOnline) {
            CheckIpv6Reply(&station->Info, &frame, &ref);
        }
    } else if (station->SharedAdvertDeadline != 0 && frame.SkipFlags == 0) {
        const MT7922_SHARED_ADVERT_DEBUG *debug = station->SharedEchoActive
            ? &station->ConnectDebug.SharedEcho : &station->ConnectDebug.SharedAdvert;
        if (!debug->Submitted && !frame.HeaderTranslationError) {
            PrepareSharedAdvert(station, &frame);
        } else if (station->SharedEchoActive && debug->Submitted && debug->Status == STATUS_PENDING) {
            AWDL_SYNC_REF ref = {.EchoSequence = station->SharedEchoSequence};
            RtlCopyMemory(ref.Peer, station->SharedAdvertRequest.Peer, sizeof(ref.Peer));
            CheckIpv6Reply(&station->Info, &frame, &ref);
            station->ConnectDebug.SharedEchoReplies = station->Info.PingReplies;
        }
    }
    if (Mt7922ReadStationDisconnect(frame.Bytes, frame.Length, station->Info.NicMac, station->Ap.Bssid)) {
        RtlCopyMemory(station->PeerDisconnectFrame, frame.Bytes, sizeof(station->PeerDisconnectFrame));
        station->PendingFrameBytes = 0;
        return;
    }
#ifdef MT7922_STATION_PACKET_TRACE
    if (frame.CcmpDecrypted && frame.AmsduPart == 0 && frame.ReceiveWcid == station->PeerWcid) {
        Mt7922TraceStationFrame(frame.Bytes, frame.Length, true, Mt7922TraceRxDmaObserved);
    }
#endif
    MT7922_STATION_IO_DEBUG *trace = &station->ConnectDebug.StationIo;
    if (trace->Active && frame.CcmpDecrypted && frame.AmsduPart == 0 && frame.Length >= 26 &&
        (frame.Bytes[4] & 1U) == 0 && frame.ReceiveWcid == station->PeerWcid) {
        bool eapol = false;
        if (Mt7922ValidateStationRxData(frame.Bytes, frame.Length, station->Info.NicMac,
                station->Ap.Bssid, &eapol) != 0 && !eapol) {
            ++trace->RxCandidates;
            if (station->PendingFrameBytes != 0) {
                ++trace->RxPendingDiscards;
                trace->RxLastDiscardSequence = ((uint32_t)frame.Bytes[23] << 4) | (frame.Bytes[22] >> 4);
            }
        }
    }
    if (station->PendingFrameBytes == 0) {
        station->PendingFrameBytes = DecodeStationData(station, &frame, false);
        if (station->PendingFrameBytes != 0 &&
            target->Receive(target->Context, station->PendingFrame, station->PendingFrameBytes,
                station->PendingFrameEapol)) {
            station->PendingFrameBytes = 0;
        }
    }
}

void Mt7922McuPollStationData(MT7922_MCU_STATION *Station, MT7922_RECEIVE_STATION_DATA Receive, void *Context)
{
    if (!Station->Ready || Station->AssociationId == 0 || !Station->WdiAssociationReported ||
        Station->KeyStateInvalid || Receive == NULL) {
        return;
    }
    if (Station->ObserverDeadline != 0 && KeQueryInterruptTime() >= Station->ObserverDeadline) {
        Mt7922McuStopStationObserver(Station);
    }
    if (Station->PendingFrameBytes != 0 &&
        Receive(Context, Station->PendingFrame, Station->PendingFrameBytes, Station->PendingFrameEapol)) {
        Station->PendingFrameBytes = 0;
    }
    STATION_DELIVERY target = {Station, Receive, Context};
    Mt7922McuPollStation(Station, ForwardStationData, &target);
    if (Station->SharedLink != NULL &&
        (Station->SharedLink->Reorder.stored != 0 || Station->SharedLink->AmsduBodyBytes != 0)) {
        AWDL_SYNC_REF ref = SharedReceiveRef(Station);
        PollBaReorder(&Station->Info, &ref, AwdlHostTime());
    }
}
