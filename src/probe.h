#pragma once

#define MT7922_PROBE_VERSION 17
#define MT7922_FACTORY_IMAGE_BYTES 889680
#define MT7922_REFERENCE_IMAGE_BYTES 892880
#define MT7922_CLC_IMAGE_BYTES 477920
#define MT7922_PCI_VENDOR 0x14c3
#define MT7922_PCI_DEVICE 0x0616
#define MT7925_PCI_DEVICE 0x7925
#define MT7922_CHIP_ID 0x7922
#define MT7925_CHIP_ID 0x7925
#define MT7925_REFERENCE_IMAGE_BYTES 1276032U
#define MT7922_PCI_COMMAND_MEMORY 0x2
#define MT7922_PCI_COMMAND_MASTER 0x4

DEFINE_GUID(GUID_DEVINTERFACE_MT7922_PROBE,
    0xbf1a2054, 0xdc33, 0x4538, 0x84, 0x2c, 0x5f, 0x22, 0x89, 0x07, 0xde, 0x89);

DEFINE_GUID(GUID_DEVINTERFACE_MT7922_SHARED,
    0x36446677, 0x94db, 0x41f4, 0xba, 0x29, 0x98, 0xc3, 0x72, 0xe7, 0xe0, 0xb6);

#define IOCTL_MT7922_PROBE_GET_INFO \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_MT7922_PROBE_MCU \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_MT7922_BOOT_FIRMWARE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_MT7922_RECEIVE_AWDL \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_MT7922_TRANSMIT_AWDL \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_MT7922_TRANSMIT_AWDL_44 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_MT7922_TRANSMIT_AWDL_149 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x806, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define MT7922_AWDL_PEER_BYTES 6U
#define MT7922_AWDL_TCP_TARGET_BYTES (MT7922_AWDL_PEER_BYTES + 2U)
#define MT7922_AWDL_LINK_TARGET_BYTES (MT7922_AWDL_TCP_TARGET_BYTES + 1U)
#define MT7922_AWDL_UPLOAD_AMSDU_TARGET_BYTES (MT7922_AWDL_LINK_TARGET_BYTES + 1U)
#define MT7922_AWDL_UPLOAD_AMSDU3 3U
#define MT7922_AWDL_RECEIVER_TARGET_BYTES (MT7922_AWDL_LINK_TARGET_BYTES + 1U)
#define MT7922_AWDL_RECEIVER_MODE 1U
#define MT7922_AWDL_RECEIVER_WARMUP_MODE 2U
#define MT7922_AWDL_RECEIVER_WINAWDL_MODE 3U
#define MT7922_AWDL_RECEIVER_HE 0x08U
#define MT7922_AWDL_RECEIVER_AMSDU 0x10U
#define MT7922_AWDL_RECEIVER_REORDER 0x20U
#define MT7922_AWDL_RECEIVER_SHORT_POLL 0x40U
#define MT7922_AWDL_SHARED_RECEIVER_MTU1500 0x40U /* Shared mode has no short-poll flag. */
#define MT7922_AWDL_SHARED_UPLOAD_EARLY 0x01U /* Sender's rate byte. */
#define MT7922_AWDL_SHARED_UPLOAD_BATCH64 0x08U /* Sender's rate byte. */
#define MT7922_AWDL_SHARED_UPLOAD_BATCH 0x10U /* Sender's rate byte. */
#define MT7922_AWDL_SHARED_UPLOAD_HE_2_STREAM 0x20U /* Sender's rate byte. */
#define MT7922_AWDL_SHARED_UPLOAD_FAST 0x40U /* Sender's rate byte. */
#define MT7922_AWDL_SHARED_PEER_WCID_ACK 0x80U /* Flag in the shared START rate byte. */
#define MT7922_AWDL_RECEIVER_VHT 0x80U
#define MT7922_AWDL_RECEIVER_RX256 0x04U
#define MT7922_AWDL_RECEIVER_PEER_PORT 8770U
#define MT7922_AWDL_SERVICE_LABEL_BYTES 12U
#define MT7922_AWDL_SERVICE_TARGET_BYTES (MT7922_AWDL_RECEIVER_TARGET_BYTES + MT7922_AWDL_SERVICE_LABEL_BYTES)

#define IOCTL_MT7922_AWDL_LINK_START \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x808, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_MT7922_AWDL_LINK_STATUS \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x809, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_MT7922_AWDL_LINK_EXCHANGE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80a, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_MT7922_AWDL_LINK_STOP \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80b, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_MT7922_AWDL_WATCH_PAIR \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80c, METHOD_BUFFERED, FILE_WRITE_ACCESS)

/* Packed IPv6 output. An opted-in fast sender can also submit packed IPv6 input. */
#define IOCTL_MT7922_AWDL_LINK_EXCHANGE_BATCH \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80d, METHOD_BUFFERED, FILE_WRITE_ACCESS)

/* Like LINK_EXCHANGE_BATCH, with 24 Mbps for eligible upload ACKs. */
#define IOCTL_MT7922_AWDL_LINK_EXCHANGE_FAST_ACK \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80e, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_MT7922_AWDL_LINK_EXCHANGE_ACK54 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80f, METHOD_BUFFERED, FILE_WRITE_ACCESS)

/* The shared sender's /Discover succeeded; permit early outbound ADDBA. */
#define IOCTL_MT7922_AWDL_UPLOAD_DISCOVERED \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x810, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define MT7922_AWDL_WATCH_MS 45000U
#define MT7922_AWDL_WATCH_PEER_BYTES (2U * MT7922_AWDL_PEER_BYTES)

typedef struct _MT7922_AWDL_WATCH_DIRECTION {
    ULONG SyncFrames;
    ULONG DataFrames;
    ULONG Ipv6Packets;
    ULONG TcpSyn;
    ULONG TcpSynAck;
    ULONG TcpPayload;
} MT7922_AWDL_WATCH_DIRECTION;

typedef struct _MT7922_AWDL_WATCH_INFO {
    ULONG Status;
    ULONG Stage;
    ULONG CleanupStatus;
    MT7922_AWDL_WATCH_DIRECTION From[2];
} MT7922_AWDL_WATCH_INFO;

#define MT7922_AWDL_LINK_MTU 1280U
#define MT7922_AWDL_WINAWDL_MTU 1450U
#define MT7922_AWDL_MAX_MTU 1500U
#define MT7922_AWDL_UPLOAD_BATCH_DEFAULT 16U
#define MT7922_AWDL_UPLOAD_BATCH_MAX 64U
#define MT7922_AWDL_LINK_BATCH_MIN 8U
#ifdef MT7922_WIFICX
#define MT7922_AWDL_LINK_BATCH_MAX 64U
#else
#define MT7922_AWDL_LINK_BATCH_MAX 16U
#endif
#define MT7922_AWDL_LINK_WAITING 1U
#define MT7922_AWDL_LINK_READY 2U

typedef struct _MT7922_AWDL_LINK_STATUS {
    ULONG State;
    UCHAR Local[16];
    UCHAR Peer[16];
} MT7922_AWDL_LINK_STATUS;

typedef struct _MT7922_AWDL_LINK_REPORT {
    ULONG Status;
    ULONG Stage;
    ULONG CleanupStatus;
    ULONG PeerSyncFrames;
    ULONG AddbaRequests;
    ULONG AddbaResponses;
    ULONG AddbaAcked;
    ULONG MdnsQueries;
    ULONG MdnsAnswers;
    ULONG PingSent;
    ULONG PingReplies;
    ULONG ControlSyn;
    ULONG ControlSynAck;
    ULONG ControlRefused;
    ULONG ControlReset;
    ULONG Submitted;
    ULONG Acked;
    ULONG NoAck;
    ULONG PeerIpv6Frames;
    ULONG LocalIpv6Frames;
    ULONG Received;
    ULONG RxQueueDrops;
    ULONG ConfiguredRateCode;
    ULONG ProbeReportRate;
    ULONG TunnelReportRate;
    ULONG SharedMifReleased;
    ULONG MdnsAnnounced;
    ULONG ServiceMifsReleased;
    ULONG PeerDataFrames;
    ULONG LocalDataFrames;
    ULONG SkippedRxRecords;
    ULONG UploadWindowReplies;
    ULONG UploadFastPolls;
    ULONG UploadLatePolls;
    ULONG RecoveryEchoSent;
    ULONG RecoveryEchoAcked;
    ULONG VhtAdverts;
    ULONG RxPhyLegacy;
    ULONG RxPhyHt;
    ULONG RxPhyVht;
    ULONG RxPhyVht80;
    ULONG RxPhyOther;
    ULONG RxPhyUnknown;
    ULONG RxAmpduFrames;
    ULONG RxAmpduBursts;
    ULONG RxAmpduMax;
    ULONG FastAckSubmitted;
    ULONG FastAckAcked;
    ULONG FastAckNoAck;
    ULONG FastAckReportRate;
    ULONG UploadShortPolls;
    ULONG UploadShortPollTimeUs;
    ULONG BaReorderSessions;
    ULONG BaReorderDirect;
    ULONG BaReorderReleased;
    ULONG BaReorderDuplicates;
    ULONG BaReorderHoles;
    ULONG BaReorderBars;
    ULONG BaReorderTimeouts;
    ULONG RxQueueDepth;
    ULONG RxQueuePeak;
    ULONG RxFetchBatches;
    ULONG RxFetchPackets;
    ULONG RxFetchGapCount;
    ULONG RxFetchGapSumUs;
    ULONG RxFetchGapMinUs;
    ULONG RxFetchGapMaxUs;
} MT7922_AWDL_LINK_REPORT;

typedef struct _MT7922_AWDL_SHARED_TX_REPORT {
    ULONG TxStatusMissing;
    ULONG SynAckSubmitted;
    ULONG SynAckAcked;
    ULONG SynAckFailed;
    ULONG SynAckRetries;
} MT7922_AWDL_SHARED_TX_REPORT;

typedef struct _MT7922_AWDL_SHARED_BA_REPORT {
    ULONG Attempts;
    ULONG Configured;
    ULONG Status;
    ULONG FirmwareReply;
    ULONG CleanupStatus;
} MT7922_AWDL_SHARED_BA_REPORT;

typedef struct _MT7922_AWDL_SHARED_ACK_REPORT {
    ULONG Submitted;
    ULONG Released;
    ULONG PeakInFlight;
    ULONG ReleaseErrors;
} MT7922_AWDL_SHARED_ACK_REPORT;

#define MT7922_AWDL_UPLOAD_TX_REPORT_MAGIC 0x58545055U /* UPTX */
#define MT7922_AWDL_UPLOAD_TX_CONTROL_TIMEOUT 1U
#define MT7922_AWDL_UPLOAD_TX_DATA_TIMEOUT 2U

typedef struct _MT7922_AWDL_UPLOAD_TX_REPORT {
    ULONG Magic;
    ULONG TimeoutKind;
    ULONG TimeoutSequence;
    ULONG TimeoutAgeUs;
} MT7922_AWDL_UPLOAD_TX_REPORT;

#define MT7922_AWDL_UPLOAD_BA_REPORT_MAGIC 0x41425055U /* UPBA */
typedef struct _MT7922_AWDL_UPLOAD_BA_REPORT {
    ULONG Magic;
    ULONG Requests;
    ULONG Responses;
    ULONG PeerStatus;
    ULONG FirmwareStatus;
    ULONG EligibleFrames;
    ULONG BlockAcks;
    ULONG LargestBitmap;
    ULONG PeerHe;
    ULONG He80TxReports;
    ULONG AmpduTxReports;
} MT7922_AWDL_UPLOAD_BA_REPORT;

#define MT7922_AWDL_UPLOAD_WTBL_REPORT_MAGIC 0x4c425457U /* WTBL */
typedef struct _MT7922_AWDL_UPLOAD_WTBL_SNAPSHOT {
    ULONG State; /* 0: not sampled, 1: peer address mismatch, 2: matched */
    ULONG Capabilities;
    ULONG Aggregation;
    ULONG BaWindows;
    ULONG Control;
    ULONG Bandwidth;
    ULONG Rates;
} MT7922_AWDL_UPLOAD_WTBL_SNAPSHOT;

typedef struct _MT7922_AWDL_UPLOAD_WTBL_REPORT {
    ULONG Magic;
    ULONG Window;
    ULONG RequestsCompleted;
    ULONG RequestsAcked;
    MT7922_AWDL_UPLOAD_WTBL_SNAPSHOT Before;
    MT7922_AWDL_UPLOAD_WTBL_SNAPSHOT AfterWtbl;
    MT7922_AWDL_UPLOAD_WTBL_SNAPSHOT AfterSta;
    MT7922_AWDL_UPLOAD_WTBL_SNAPSHOT Finish;
} MT7922_AWDL_UPLOAD_WTBL_REPORT;

#define MT7922_AWDL_ADDBA_TIMING_MAGIC 0x54414241U /* ABAT */
#define MT7922_ADDBA_DEFER_MIF        0x40U
#define MT7922_ADDBA_DEFER_MDNS       0x80U
#define MT7922_ADDBA_DEFER_RING       0x100U
typedef struct _MT7922_AWDL_ADDBA_TIMING {
    ULONG Magic;
    ULONG FreshSyncs;
    ULONG RegularWindows;
    ULONG Deferrals;
    ULONG ExpiredBeforeSubmit;
} MT7922_AWDL_ADDBA_TIMING;

#define MT7922_AWDL_UPLOAD_BW_MAGIC 0x57425055U /* UPBW */
#define MT7922_AWDL_UPLOAD_BW_SAMPLES 41U
typedef struct _MT7922_AWDL_UPLOAD_BW_SAMPLE {
    ULONGLONG SampledAt;
    ULONG PeerMatched;
    ULONG Dw9;
    ULONG Dw10;
    ULONG Dw16;
    ULONG Dw17;
    ULONG Dw18;
    ULONG Dw19;
    ULONG InFlight;
    ULONG HeTxByBandwidth[4];
} MT7922_AWDL_UPLOAD_BW_SAMPLE;

typedef struct _MT7922_AWDL_UPLOAD_BW_REPORT {
    ULONG Magic;
    ULONG Count;
    MT7922_AWDL_UPLOAD_BW_SAMPLE Samples[MT7922_AWDL_UPLOAD_BW_SAMPLES];
} MT7922_AWDL_UPLOAD_BW_REPORT;

#define MT7922_AWDL_ACK_TRACE_MAGIC 0x544b4341U /* ACKT */
#define MT7922_AWDL_ACK_TRACE_MAX 4096U
#define MT7922_ACK_WAIT_WLAN_TX    0x01U
#define MT7922_ACK_WAIT_AWDL_TX    0x02U
#define MT7922_ACK_WAIT_WINDOW     0x04U
#define MT7922_ACK_WAIT_DMA        0x08U
#define MT7922_ACK_WAIT_CONTROL    0x10U
#define MT7922_ACK_WAIT_SLOTS      0x20U
#define MT7922_ACK_WAIT_COUNT     6U

typedef struct _MT7922_ACK_WAIT_SUMMARY {
    UCHAR Flags;
    UCHAR Primary;
    USHORT Observed100Us;
} MT7922_ACK_WAIT_SUMMARY;

typedef struct _MT7922_AWDL_ACK_TRACE_HEADER {
    ULONG Magic;
    ULONG Count;
    ULONG Omitted;
    ULONG EntryBytes;
} MT7922_AWDL_ACK_TRACE_HEADER;

typedef struct _MT7922_AWDL_ACK_TRACE_ENTRY {
    ULONGLONG EnqueuedAt;
    ULONGLONG SubmittedAt;
    ULONGLONG TxStatusAt;
    ULONGLONG ReleasedAt;
    ULONG TcpSequence;
    ULONG TcpAck;
    USHORT PeerPort;
    USHORT WlanSequence;
    USHORT RateCode;
    USHORT Attempts;
    UCHAR Queue;
    UCHAR AckErrors;
    UCHAR ReleaseStatus;
    UCHAR Wcid;
    MT7922_ACK_WAIT_SUMMARY Wait;
    ULONGLONG DmaObservedAt; /* First host observation of descriptor consumption. */
} MT7922_AWDL_ACK_TRACE_ENTRY;

typedef struct _MT7922_AWDL_SHARED_AMSDU_REPORT {
    ULONG Negotiations;
    ULONG Frames;
    ULONG Subframes;
    ULONG SplitRecords;
    ULONG Discarded;
    ULONG DmaReassembled;
} MT7922_AWDL_SHARED_AMSDU_REPORT;

typedef struct _MT7922_AWDL_SHARED_HE_REPORT {
    ULONG Adverts;
    ULONG PeerSupported;
    ULONG FirmwareConfigured;
    ULONG FirmwareStatus;
    ULONG Frames;
    ULONG Frames80;
    ULONG Mcs10;
    ULONG Mcs11;
} MT7922_AWDL_SHARED_HE_REPORT;

#define MT7922_AWDL_SYN_ACK_TIMING_MAX 32U
#define MT7922_AWDL_SYN_ACK_ALTX0 8U
#define MT7922_AWDL_SYN_ACK_DMA_OBSERVED 16U
typedef struct _MT7922_AWDL_SYN_ACK_TIMING {
    ULONG Sequence;
    ULONG PeerPhysicalTx;
    ULONG SyncAgeUs;
    ULONG WindowFlags; /* Recent sync window (1), projected peer window (2), retry (4), ALTX0 (8). */
} MT7922_AWDL_SYN_ACK_TIMING;

typedef struct _MT7922_AWDL_SYN_ACK_QUEUE_TIMING {
    ULONG DmaObservedUs;
    ULONG ReleaseObservedUs;
    ULONG SamplesPresent; /* PLE samples in bits 0-3 and DMA observation in bit 4. */
    ULONG NonemptySamples;
} MT7922_AWDL_SYN_ACK_QUEUE_TIMING;

#define MT7922_AWDL_MDNS_TIMING_MAGIC 0x534E444DU
typedef struct _MT7922_AWDL_MDNS_TIMING {
    ULONG Magic;
    ULONG Queued;
    ULONG Submitted;
    ULONG Sequence;
    ULONG QueueToSubmitUs;
    MT7922_AWDL_SYN_ACK_QUEUE_TIMING Timing;
} MT7922_AWDL_MDNS_TIMING;

#define IOCTL_MT7922_TRANSMIT_AWDL_149_WIDE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x807, METHOD_BUFFERED, FILE_WRITE_ACCESS)

enum {
    MT7922_MCU_ALLOCATE = 1,
    MT7922_MCU_RESET,
    MT7922_MCU_BOOT_ROM,
    MT7922_MCU_RINGS,
    MT7922_MCU_GET,
    MT7922_MCU_RELEASE,
    MT7922_MCU_COMPLETE,
    MT7922_MCU_VALIDATE_IMAGE,
    MT7922_MCU_PATCH,
    MT7922_MCU_RAM,
    MT7922_MCU_START,
    MT7922_MCU_CAPABILITIES,
    MT7922_MCU_RECEIVE_EFUSE,
    MT7922_MCU_RECEIVE_MAC,
    MT7922_MCU_RECEIVE_ENABLE,
    MT7922_MCU_RECEIVE_CHANNEL,
    MT7922_MCU_RECEIVE_LISTEN,
    MT7922_MCU_RECEIVE_DISABLE,
    MT7922_MCU_TX_FIND_PEER,
    MT7922_MCU_TX_SUBMIT,
    MT7922_MCU_TX_REPORT,
    MT7922_MCU_TX_LOCAL_MAC,
    MT7922_MCU_TX_SYNC,
    MT7922_MCU_TX_WTBL
};

#define MT7922_NIC_BAND_24GHZ 0x1
#define MT7922_NIC_BAND_5GHZ 0x2
#define MT7922_NIC_BAND_6GHZ 0x4
#define MT7922_NIC_PHY_HT 0x1
#define MT7922_NIC_PHY_VHT 0x2
#define MT7922_NIC_PHY_HE 0x4
#define MT7922_NIC_PHY_EHT 0x8

#define MT7922_AWDL_PROBE_CHANNELS 2U
#define MT7922_AWDL_SURVEY_CHANNELS 3U
#define MT7922_AWDL_SURVEY_PEERS 4U
#define MT7922_AWDL_SURVEY_SLOTS 16U
#define MT7922_AWDL_PROBE_DWELL_MS 10000U
#define MT7922_AWDL_TX_MAX_ATTEMPTS 3U
#define MT7922_TX_RX_TYPE_BINS 32U
#define MT7922_TX_SAMPLE_WORDS 16U
#define MT7922_TX_STATUS_GRACE_MS 50U
#define MT7922_AWDL_SYNC_MS 10000U
#define MT7922_AWDL_TCP_SYNC_MS 30000U
#define MT7922_AWDL_DEVICE_CLASS 1U /* Mac; iOS peers use class 2. */
#define MT7922_AWDL_DATA_PROBES 32U
#define MT7922_AWDL_DATA_WCID 1U

typedef struct _MT7922_TX_ATTEMPT_INFO {
    ULONG Status;
    ULONG ReportSeen;
    ULONG AckErrors;
    ULONG ReportRate;
    ULONG ReleaseSeen;
    ULONG ReleaseStatus;
    ULONG RetryCount;
    ULONG TxStatusRecords;
} MT7922_TX_ATTEMPT_INFO;

typedef struct _MT7922_AWDL_RX_COUNTS {
    ULONG Channel;
    ULONG Descriptors;
    ULONG Invalid;
    ULONG Psf;
    ULONG Mif;
    ULONG Ios;
} MT7922_AWDL_RX_COUNTS;

typedef struct _MT7922_AWDL_SURVEY_PEER {
    ULONG Received[MT7922_AWDL_SURVEY_CHANNELS];
    UCHAR Address[6];
    UCHAR LastChannel;
    UCHAR NextChannel;
    UCHAR SlotCount;
    UCHAR Slots[MT7922_AWDL_SURVEY_SLOTS];
} MT7922_AWDL_SURVEY_PEER;

#define MT7922_AWDL_RX_HEADER_BYTES 56U

typedef struct _MT7922_AWDL_RX_HEADER {
    ULONG Length;
    ULONG SampleBytes;
    ULONG AmsduPart;
    UCHAR Bytes[MT7922_AWDL_RX_HEADER_BYTES];
} MT7922_AWDL_RX_HEADER;

#define MT7922_AWDL_MDNS_SAMPLES 3U
#define MT7922_AWDL_MDNS_SAMPLE_BYTES 1232U

typedef struct _MT7922_AWDL_MDNS_SAMPLE {
    ULONG Length;
    UCHAR SourceIpv6[16];
    UCHAR Bytes[MT7922_AWDL_MDNS_SAMPLE_BYTES];
} MT7922_AWDL_MDNS_SAMPLE;

#define MT7922_MCU_REPLY_PAYLOAD_BYTES 32U

typedef struct _MT7922_MCU_PROBE_INFO {
    ULONG Status;
    ULONG Stage;
    ULONG CleanupStatus;
    ULONG ResetRegister;
    ULONG BootRomState;
    ULONG DmaConfig;
    ULONG TxDmaIndex;
    ULONG RxDmaIndex;
    ULONG EventId;
    ULONG EventSequence;
    ULONG GetReply;
    ULONG ReleaseReply;
    ULONG BlockedRegister;
    ULONG BlockedValue;
    ULONG LastCommand;
    ULONG DownloadPackets;
    ULONG DownloadBytes;
    ULONG DownloadDmaIndex;
    ULONG FirmwareReady;
    ULONG RuntimeRxDmaIndex;
    ULONG NicBands;
    ULONG NicSpatialStreams;
    ULONG NicBandwidthCode;
    ULONG NicPhyFlags;
    ULONG LastTxRing;
    ULONG LastTxHostIndex;
    ULONG LastTxDescriptorControl;
    ULONG DmaInterruptStatus;
    ULONG DmaGlobalExtension;
    ULONG DmaScheduler;
    ULONG PcieMacPower;
    ULONG RomResultCommand;
    ULONG RomResultStatus;
    ULONG RomResultBytes;
    ULONG FirmwareReadyRemapped;
    ULONG DownloadDonePackets;
    ULONG MonitorReply;
    ULONG DataRxDmaIndex;
    MT7922_AWDL_RX_COUNTS Capture[MT7922_AWDL_PROBE_CHANNELS];
    ULONG TxPeerFound;
    ULONG DataTxDmaIndex;
    ULONG TxReportSeen;
    ULONG TxReportAckErrors;
    ULONG TxReportRate;
    ULONG TxReleaseSeen;
    ULONG TxReleaseStatus;
    ULONG TxAttempts;
    UCHAR NicMac[6];
    UCHAR ReservedMac[2];
    ULONG TxProbes;
    MT7922_TX_ATTEMPT_INFO TxProbe[MT7922_AWDL_TX_MAX_ATTEMPTS];
    ULONG TxRxTypeCount[MT7922_TX_RX_TYPE_BINS];
    ULONG TxStatusSampleBytes;
    ULONG TxStatusSample[MT7922_TX_SAMPLE_WORDS];
    ULONG TxReleaseSampleBytes;
    ULONG TxReleaseSample[MT7922_TX_SAMPLE_WORDS];
    ULONG SyncPsfSent;
    ULONG SyncMifSent;
    ULONG SyncReleased;
    ULONG SyncFailed;
    ULONG SyncPeerFrames;
    ULONG SyncLastTsfUs;
    ULONG SyncLastNextSlotTu;
    ULONG DataProbeSent;
    ULONG DataProbeAcked;
    ULONG DataProbeFailed;
    ULONG DataProbeAckErrors;
    ULONG DataProbeReleaseStatus;
    ULONG DataProbeRetries;
    UCHAR DataPeer[6];
    UCHAR ReservedData[2];
    ULONG MgmtProbeSent;
    ULONG MgmtProbeAcked;
    ULONG MgmtProbeFailed;
    ULONG MgmtProbeAckErrors;
    ULONG MgmtProbeReleaseStatus;
    ULONG MgmtProbeRetries;
    ULONG SelfFrames;
    ULONG PeerAddressed;
    ULONG PeerMentions;
    ULONG ControlFrames;
    ULONG AcksToUs;
    ULONG DataProbeOffset[MT7922_AWDL_DATA_PROBES];
    ULONG DataProbeResult[MT7922_AWDL_DATA_PROBES];
    ULONG PingSent;
    ULONG PingReplies;
    ULONG PingLastTtl;
    ULONG PeerSamples;
    UCHAR PeerSample[3][48];
    ULONG AddbaRequests;
    ULONG AddbaResponses;
    ULONG AddbaAcked;
    ULONG PeerQos;
    ULONG PeerQosSampleBytes;
    UCHAR PeerQosSample[64];
    ULONG ProbePlanUs[MT7922_AWDL_DATA_PROBES];
    ULONG ProbeSubmitUs[MT7922_AWDL_DATA_PROBES];
    ULONG ProbePeerDeltaUs[MT7922_AWDL_DATA_PROBES];
    ULONG PeerCount;
    ULONG DataPeerChanges;
    ULONG PeerFrames[4];
    UCHAR PeerAddr[4][6];
    ULONG ProbeWindowsMissed;
    UCHAR AddbaPeerAddr[6];
    USHORT UnicastReleaseTimeouts;
    ULONG RxBaAttempts;
    ULONG RxBaConfigured;
    ULONG RxBaLastStatus;
    ULONG RxBaFirmwareReply;
    ULONG MdnsQueriesSent;
    ULONG MdnsQueriesReleased;
    ULONG MdnsResponses;
    ULONG MdnsAirDropAnswers;
    ULONG MdnsDirectQueries;
    ULONG MdnsDirectAcked;
    ULONG MdnsDirectProbeMask;
    ULONG MdnsOtherResponses;
    ULONG MdnsOtherAirDropAnswers;
    ULONG PeerIpv6Frames;
    MT7922_AWDL_RX_COUNTS Capture149;
    ULONG Capture149Status;
    ULONG Capture149FirmwareReply;
    ULONG SurveyPeerCount;
    MT7922_AWDL_SURVEY_PEER SurveyPeers[MT7922_AWDL_SURVEY_PEERS];
    ULONG AwdlRxDataSelected;
    ULONG AwdlRxDataOther;
    ULONG AwdlRxDataToUs;
    ULONG NormalRxSkipped;
    ULONG NormalRxFcsErrors;
    ULONG NormalRxTranslated;
    ULONG NormalRxAmsduErrors;
    ULONG NormalRxBadLength;
    ULONG NormalRxBadGroups;
    ULONG AwdlRxDataSplit;
    MT7922_AWDL_RX_HEADER AwdlRxHeaders[2];
    ULONG FirstSkippedRxBytes;
    ULONG FirstSkippedRx[5];
    ULONG NormalRxRawTranslationError;
    ULONG WrappedIpv6Packets;
    ULONG Ipv6PacketsToUs;
    ULONG MdnsSampleCount;
    MT7922_AWDL_MDNS_SAMPLE MdnsSamples[MT7922_AWDL_MDNS_SAMPLES];
    ULONG MdnsCurrentWindowQueries;
    ULONG TcpPort;
    ULONG TcpSourcePort;
    ULONG TcpInitialSequence;
    ULONG TcpSynSent;
    ULONG TcpSynAck;
    ULONG TcpRefused;
    ULONG TcpResetSent;
    ULONG TcpProbeMask;
    UCHAR ReplyPayload[MT7922_MCU_REPLY_PAYLOAD_BYTES];
    ULONG ReplyPayloadBytes;
} MT7922_MCU_PROBE_INFO;

#define MT7922_MCU_PROBE_V1_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, LastTxRing))
#define MT7922_MCU_PROBE_V2_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, RomResultCommand))
#define MT7922_MCU_PROBE_V3_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, MonitorReply))
#define MT7922_MCU_PROBE_V4_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, TxPeerFound))
#define MT7922_MCU_PROBE_V5_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, TxProbes))
#define MT7922_MCU_PROBE_V6_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, TxRxTypeCount))
#define MT7922_MCU_PROBE_V7_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, SyncPsfSent))
#define MT7922_MCU_PROBE_V8_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, DataProbeSent))
#define MT7922_MCU_PROBE_V9_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, MgmtProbeSent))
#define MT7922_MCU_PROBE_V10_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, SelfFrames))
#define MT7922_MCU_PROBE_V11_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, ControlFrames))
#define MT7922_MCU_PROBE_V12_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, DataProbeOffset))
#define MT7922_MCU_PROBE_V13_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, PingSent))
#define MT7922_MCU_PROBE_V14_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, PeerSamples))
#define MT7922_MCU_PROBE_V15_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, AddbaRequests))
#define MT7922_MCU_PROBE_V16_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, AddbaAcked))
#define MT7922_MCU_PROBE_V17_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, ProbePlanUs))
#define MT7922_MCU_PROBE_V18_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, RxBaAttempts))
#define MT7922_MCU_PROBE_V19_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, MdnsQueriesSent))
#define MT7922_MCU_PROBE_V20_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, MdnsDirectQueries))
#define MT7922_MCU_PROBE_V21_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, Capture149))
#define MT7922_MCU_PROBE_V22_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, AwdlRxDataSelected))
#define MT7922_MCU_PROBE_V23_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, NormalRxFcsErrors))
#define MT7922_MCU_PROBE_V24_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, NormalRxBadLength))
#define MT7922_MCU_PROBE_V25_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, NormalRxRawTranslationError))
#define MT7922_MCU_PROBE_V26_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, MdnsSampleCount))
#define MT7922_MCU_PROBE_V27_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, MdnsCurrentWindowQueries))
#define MT7922_MCU_PROBE_V28_BYTES ((ULONG)FIELD_OFFSET(MT7922_MCU_PROBE_INFO, TcpPort))

typedef struct _MT7922_PROBE_INFO {
    ULONG Version;
    ULONG VendorId;
    ULONG DeviceId;
    ULONG MemoryRangeCount;
    ULONG InterruptCount;
    ULONG RegisterWindowBytes;
    ULONG RegisterStatus;
    ULONG ChipId;
    ULONG ChipRevision;
    ULONG FirmwareProbeStatus;
    ULONG OwnershipBefore;
    ULONG OwnershipWhileProbing;
    ULONG OwnershipAfter;
    ULONG FirmwareState;
    ULONG DmaConfig;
    ULONG DmaBufferStatus;
    ULONG DmaBufferBytes;
    ULONG PciCommand;
} MT7922_PROBE_INFO;
