#pragma once

#include "rx-queue.h"
#include "station-clc.h"
#include "station-observer.h"
#include "station-port.h"
#include "tx.h"
#include <stdbool.h>
#include <stdint.h>

struct _MCU_RINGS;
typedef bool (*MT7922_SCAN_RECORD_HANDLER)(void *Scan, const uint8_t *Record, size_t Bytes);

typedef struct {
    struct _MCU_RINGS *Rings;
    MT7922_MCU_PROBE_INFO *Result;
    const MT7922_STATION_RADIO_PROFILE *Radio;
    uint16_t ChipId;
    bool Associated;
    uint8_t ScanSequence;
    uint8_t ScanBssIndex;
    bool ScanActive;
} MT7922_MCU_SCAN_TRANSPORT;

/* Counts only. Handshake payloads and key material are never recorded here. */
typedef struct {
    uint32_t RxRecords;
    uint32_t RxRaw;
    uint32_t RxTranslated;
    uint32_t RxApData;
    uint32_t RxEapol;
    uint32_t RxPlain;
    uint32_t RxPolicyDrop;
    uint32_t RxSecurityDrop;
    uint32_t RxEarly;
    uint32_t RxStaged;
    uint32_t RxStageBusy;
    uint32_t RxPosted;
    uint32_t TxEapol;
    uint32_t TxNeedKey;
    uint32_t TxSubmitted;
    uint32_t TxBusy;
    uint32_t TxError;
    int32_t TxLastStatus;
    uint32_t PrivacyRequests;
    int32_t PrivacyStatus;
    uint32_t RxPolicy;
    uint32_t ReportedCipher;
} MT7922_EAPOL_DEBUG;

typedef struct {
    uint32_t PortSecured;
    int32_t PortStatus;
    uint32_t TxHostData;
    uint32_t TxFormatDrop;
    uint32_t TxSubmitted;
    uint32_t TxBusy;
    uint32_t TxError;
    int32_t TxLastStatus;
    uint32_t TxCompleted;
    uint32_t TxCompletionErrors;
    uint32_t TxReleaseStatus;
    uint32_t TxReleaseDuringCommand;
} MT7922_STATION_DATA_DEBUG;

#define MT7922_STATION_DATA_TX_SLOTS 16U

#define MT7922_COEX_AWDL_ATTACHED     0x01U
#define MT7922_COEX_STATION_TX_PENDING 0x02U
#define MT7922_COEX_SYNC_DMA_PENDING  0x04U
#define MT7922_COEX_AWDL_DMA_PENDING  0x08U
#define MT7922_COEX_AWDL_TX_PENDING   0x10U
#define MT7922_COEX_BA_RESET_PENDING  0x20U
#define MT7922_COEX_BA_PEER_REGISTERED 0x40U

typedef struct {
    uint32_t Sample;
    uint32_t Flags;
    uint32_t BaResetAttempts;
    int32_t BaCleanupStatus;
    uint32_t AckInFlight;
    uint32_t StationRxStaged;
} MT7922_COEX_DEBUG;

/* A bounded, opt-in measurement. No packet data or keys are retained. */
typedef struct {
    uint32_t Active;
    uint32_t RxCandidates;
    uint32_t RxPendingDiscards;
    uint32_t RxLastDiscardSequence;
    uint32_t TxSubmitted;
    uint32_t TxSubmitErrors;
    uint32_t TxMaxSubmitToDmaUs;
    uint32_t TxReleased;
    uint32_t TxMaxDmaToReleaseUs;
    uint32_t TxReleaseOver50Ms;
    uint32_t RxQueuePeak;
    uint32_t RxQueueFullEvents;
    uint32_t TxInFlightPeak;
    uint32_t TxDmaObservedAtRelease;
} MT7922_STATION_IO_DEBUG;

typedef struct {
    uint32_t Channel;
    uint32_t Running;
    uint32_t Records;
    uint32_t Invalid;
    uint32_t Psf;
    uint32_t Mif;
    uint32_t Data;
    uint32_t DataToUs;
    uint32_t PeerCount;
} MT7922_STATION_OBSERVATION_DEBUG;

typedef struct {
    uint32_t RequestId;
    uint8_t Peer[6];
    uint8_t Echo;
    uint8_t Reserved;
} MT7922_SHARED_ADVERT_REQUEST;

typedef struct {
    uint32_t RequestId;
    uint32_t Submitted;
    uint32_t Released;
    uint32_t ReleaseStatus;
    int32_t Status;
    uint32_t DmaFetched;
    uint32_t TxReportSeen;
    uint32_t TxRateCode;
    uint32_t TxAckErrors;
} MT7922_SHARED_ADVERT_DEBUG;

/* Last WDI connect attempt, mirrored to the service registry for bring-up. */
typedef struct {
    uint32_t Sequence;
    int32_t PlanStatus;
    int32_t PrepareStatus;
    int32_t JoinStatus;
    int32_t ReportStatus;
    int32_t KeyStatus;
    uint32_t KeyCount;
    uint32_t WdiAuthAlgorithm;
    uint32_t WdiUnicastCipher;
    uint32_t WdiFlags;
    uint32_t CandidateCount;
    uint32_t FirstBandId;
    uint32_t FirstChannel;
    uint32_t FirstBody;
    uint32_t CandidateReason;
    uint8_t OsBand;
    uint8_t OsChannel;
    uint8_t ApBand;
    uint8_t ApChannel;
    uint8_t ApBssid[6];
    uint8_t RadioWidthMhz;
    uint8_t RadioCenterChannel;
    uint16_t AuthStatusCode;
    uint16_t AssocStatusCode;
    uint16_t AssociationId;
    /* First candidate IE screening: 0 passed, 1 PMKID/FT flag, 2 capability bits,
       3 SSID element, 4 RSN element, 5 rate set, 6 required elements missing,
       7 OS extra association IEs, 8 BSS extra association IEs. */
    uint32_t IeFail;
    uint32_t BodyBytes;
    uint32_t ParamsVendorBytes;
    uint32_t EntryVendorBytes;
    uint8_t BodySample[MT7922_CONNECT_DEBUG_BODY_SAMPLE];
    uint8_t ParamsVendorSample[MT7922_CONNECT_DEBUG_VENDOR_SAMPLE];
    uint8_t EntryVendorSample[MT7922_CONNECT_DEBUG_VENDOR_SAMPLE];
    MT7922_EAPOL_DEBUG Eapol;
    MT7922_STATION_DATA_DEBUG Data;
    MT7922_STATION_OBSERVATION_DEBUG Observation;
    MT7922_SHARED_ADVERT_DEBUG SharedAdvert;
    MT7922_SHARED_ADVERT_DEBUG SharedEcho;
    uint32_t SharedEchoReplies;
    MT7922_COEX_DEBUG Coexistence;
    MT7922_STATION_IO_DEBUG StationIo;
} MT7922_CONNECT_DEBUG;

typedef struct {
    MT7922_MCU_SCAN_TRANSPORT Scan;
    MT7922_STATION_RADIO_PROFILE Radio;
    WDFCOMMONBUFFER Buffer;
    MT7922_MCU_PROBE_INFO Info;
    uint16_t MulticastWcid;
    uint16_t PeerWcid;
    uint16_t AssociationId;
    uint16_t WdiPortId;
    bool KeyStateInvalid;
    bool PairwiseKeyInstalled;
    bool GroupKeyInstalled;
    bool FirmwarePortSecured;
    bool WdiAssociationReported;
    MT7922_PRIVACY EapolRxPrivacy;
    uint32_t EapolPrivacyRequests;
    int32_t EapolPrivacyStatus;
    uint64_t PairwiseRxPn[16];
    uint64_t GroupRxPn[16];
    uint8_t GroupKeyId;
    uint8_t PeerDisconnectFrame[MT7922_STATION_DISCONNECT_BYTES];
    size_t PendingFrameBytes;
    bool PendingFrameEapol;
    uint8_t PendingFrame[MT7922_STATION_RAW_MAX];
    MT7922_STATION_AP Ap;
    MT7922_STATION_OBSERVER Observer;
    uint64_t ObserverDeadline;
    uint64_t StationIoTraceDeadline;
    MT7922_SHARED_ADVERT_REQUEST SharedAdvertRequest;
    bool SharedEchoActive;
    uint16_t SharedEchoSequence;
    uint64_t SharedAdvertDeadline;
    uint64_t SharedAdvertWindowEnd;
    size_t SharedAdvertFrameBytes;
    uint8_t SharedAdvertFrame[MT7922_AWDL_SERVICE_MIF_CAPACITY];
    struct _MT7922_AWDL_LINK *SharedLink;
    uint8_t SharedLinkPeer[6];
    bool SharedLinkReady;
    NTSTATUS SharedLinkStatus;
    bool SharedLinkMifInFlight;
    bool SharedLinkServiceMifInFlight;
    bool SharedLinkMulticastInFlight;
    bool SharedBaReplyInFlight;
    UCHAR SharedBaPort;
    bool SharedBaPeerRegistered;
    bool SharedBaResetPending;
    uint8_t SharedBaPeer[6];
    NTSTATUS SharedBaCleanupStatus;
    bool SharedAdvertHasServiceRecords;
    uint64_t SharedLinkDataUntil;
    AWDL_CHANNEL_SEQUENCE SharedPeerChannels;
    uint64_t SharedPeerSyncAt;
    uint32_t SharedPeerPhysicalTx;
    uint8_t SharedPeerMaster[6];
    uint16_t SharedPeerWindowSequence;
    uint16_t SharedPeerUntilNextSlotTu;
    uint64_t SharedLinkNextMif;
    uint64_t SharedLinkNextMdns;
    uint64_t SharedLinkSubmittedAt;
    uint64_t SharedLinkReleasedAt;
    MT7922_SHARED_ADVERT_DEBUG SharedLinkTx;
    size_t SharedSynAckLength;
    uint16_t SharedSynAckRate;
    bool SharedSynAckRetryPending;
    bool SharedSynAckRetried;
    MT7922_CONNECT_DEBUG ConnectDebug;
    bool Ready;
} MT7922_MCU_STATION;

typedef void (*MT7922_STATION_RECEIVE)(void *Context, const uint8_t *Record, size_t Bytes);

/* The serialized station worker owns this zeroed context for the D0 session.
   The device must be exclusively owned by the caller. After a failed stop,
   retry before releasing the DMA enabler or BAR mapping. */
/* Clc is the firmware CLC region (mt7922-wificx-clc.bin). A null CLC skips
   SET_CLC when the selected firmware image has no separate CLC region. */
NTSTATUS Mt7922McuStartStation(MT7922_MCU_STATION *Station, PUCHAR Window,
    ULONG ChipId, WDFDMAENABLER DmaEnabler, const UCHAR *Image, SIZE_T ImageBytes, uint16_t MulticastWcid,
    const MT7922_STATION_RADIO_PROFILE *Profile, const UCHAR *Clc, SIZE_T ClcBytes);
NTSTATUS Mt7922McuStopStation(MT7922_MCU_STATION *Station);
NTSTATUS Mt7922McuDisableStationMac(MT7922_MCU_STATION *Station);
/* Release retained DMA after PnP has stopped PCI access, even if D0 exit timed out. */
void Mt7922McuReleaseStation(MT7922_MCU_STATION *Station);
void Mt7922McuPollStation(MT7922_MCU_STATION *Station,
    MT7922_STATION_RECEIVE Receive, void *Context);
/* Tune to an allowed 5 GHz channel and prepare the disconnected BSS and RLM. */
NTSTATUS Mt7922McuPrepareStationAp(MT7922_MCU_STATION *Station, const MT7922_STATION_AP *Ap);

/* Caller serializes the exchange with scan and configures the AP channel first.
   Success means the AP accepted association; keys and data are still handled separately. */
NTSTATUS Mt7922McuExchangeStationJoin(MT7922_MCU_STATION *Station,
    const MT7922_STATION_JOIN_REQUEST *Request, MT7922_STATION_JOIN_RESULT *Result,
    MT7922_STATION_RECEIVE Receive, void *Context);

/* Reason zero handles a peer-initiated disconnect without sending a management frame. */
NTSTATUS Mt7922McuDisconnectStation(MT7922_MCU_STATION *Station, uint16_t Reason);

/* Receive-only AWDL observation pinned to the associated router channel.
   Enabling it opens the firmware sniffer; nothing here retunes or transmits.
   Not wired into the device start path yet. */
NTSTATUS Mt7922McuStartStationObserver(MT7922_MCU_STATION *Station, uint8_t Channel);
NTSTATUS Mt7922McuStopStationObserver(MT7922_MCU_STATION *Station);
NTSTATUS Mt7922McuArmSharedAdvert(MT7922_MCU_STATION *Station, const MT7922_SHARED_ADVERT_REQUEST *Request);
void Mt7922McuServiceSharedAdvert(MT7922_MCU_STATION *Station, bool StationTxWaiting);

/* Call after association, on the same serialized worker as station commands. */
int32_t Mt7922McuInstallStationCcmp(void *Context, bool Group, uint8_t KeyId,
    const uint8_t Key[MT7922_CCMP_KEY_BYTES], uint64_t InitialRxPn);

/* Receive copies validated Native 802.11 frames before the worker reuses the buffer. */
typedef bool (*MT7922_RECEIVE_STATION_DATA)(void *Context, const uint8_t *Frame, size_t Bytes, bool Eapol);

bool Mt7922McuStationDataTxActive(const MT7922_MCU_STATION *Station);
NTSTATUS Mt7922McuSendStationData(MT7922_MCU_STATION *Station,
    const uint8_t *Ethernet, size_t Bytes, MT7922_PRIVACY Privacy);
void Mt7922McuPollStationData(MT7922_MCU_STATION *Station,
    MT7922_RECEIVE_STATION_DATA Receive, void *Context);
void Mt7922McuSnapshotCoexistence(MT7922_MCU_STATION *Station);

/* Use from the queue owner at PASSIVE_LEVEL, after boot and while the rings are live. */
int32_t Mt7922McuSubmitStationScan(void *Context, uint8_t Command,
    const uint8_t *Payload, size_t Bytes);
void Mt7922McuSetScanReceiver(struct _MCU_RINGS *Rings, void *Context,
    MT7922_SCAN_RECORD_HANDLER Event, MT7922_SCAN_RECORD_HANDLER Frame);
#include "../tools/pulseit-ba/ba_reorder.h"

typedef struct {
    USHORT Params;
    USHORT Timeout;
    USHORT StartingSequence;
    UCHAR Token;
    ULONG Attempts;
    BOOLEAN HardwareReady;
    BOOLEAN Seen;
    BOOLEAN Active;
} MT7922_AWDL_ADDBA;

typedef enum {
    AwdlTxBaIdle,
    AwdlTxBaPrepared,
    AwdlTxBaWaiting,
    AwdlTxBaAccepted,
    AwdlTxBaOperational,
    AwdlTxBaFallback
} MT7922_AWDL_TX_BA_STATE;

typedef struct {
    MT7922_AWDL_TX_BA_STATE State;
    UCHAR Token;
    USHORT Window;
    USHORT StartingSequence;
    USHORT NextSequence;
    USHORT DataSequence;
    BOOLEAN Amsdu;
    BOOLEAN RequestInFlight;
    BOOLEAN PreparedEarly;
    UCHAR Requests;
    ULONGLONG PreparedAt;
    ULONGLONG Deadline;
    ULONGLONG EarlyActivityAt;
} MT7922_AWDL_TX_BA;

#define MT7922_AWDL_MAX_AMSDU_BYTES 3895U /* Advertised VHT maximum MPDU size. */
#define MT7922_AWDL_MAX_RX_RECORD (MT7922_AWDL_MAX_AMSDU_BYTES + 128U)

typedef struct _MT7922_AWDL_LINK {
    void *Context;
    BOOLEAN (*Stopped)(void *Context);
    void (*Ready)(void *Context, const UCHAR *Local, const UCHAR *Peer);
    void (*Receive)(void *Context, const UCHAR *Packet, ULONG Length);
    ULONG (*TakeSend)(void *Context, UCHAR *Packet, USHORT *RateCode, ULONGLONG *EnqueuedAt);
    ULONG (*TakePureAck)(void *Context, UCHAR *Packet, USHORT *RateCode, ULONGLONG *EnqueuedAt,
        MT7922_ACK_WAIT_SUMMARY *Wait);
    void (*ObserveAckWait)(void *Context, UCHAR Flags, ULONGLONG Now);
    BOOLEAN PeerWcidAck;
    BOOLEAN FastUpload;
    BOOLEAN UploadHe2Stream;
    BOOLEAN PrepareUploadEarly;
    BOOLEAN UploadAmsdu3;
    volatile LONG UploadDiscovered;
    volatile LONG SharedUploadActive;
    MT7922_AWDL_UPLOAD_TX_REPORT UploadTxReport;
    MT7922_AWDL_UPLOAD_BA_REPORT UploadBaReport;
    MT7922_AWDL_UPLOAD_WTBL_REPORT UploadWtblReport;
    MT7922_AWDL_ADDBA_TIMING UploadAddbaTiming;
    MT7922_AWDL_UPLOAD_BW_REPORT UploadBandwidthReport;
    ULONGLONG NextUploadBandwidthSample;
    ULONG UploadHeTxByBandwidth[4];
    MT7922_AWDL_TX_BA UploadBa;
    AWDL_PEER_PHY UploadPeerPhy;
    MT7922_AWDL_SHARED_ACK_REPORT SharedAckReport;
    MT7922_AWDL_ACK_TRACE_ENTRY *AckTrace;
    ULONG AckTraceCount;
    ULONG AckTraceOmitted;
    ULONG Submitted;
    ULONG Acked;
    ULONG NoAck;
    ULONG ReportRate;
    ULONG SharedTxStatusMissing;
    ULONG SharedSynAckSubmitted;
    ULONG SharedSynAckAcked;
    ULONG SharedSynAckFailed;
    ULONG SharedSynAckRetries;
    MT7922_AWDL_SYN_ACK_TIMING SharedSynAckTiming[MT7922_AWDL_SYN_ACK_TIMING_MAX];
    MT7922_AWDL_SYN_ACK_QUEUE_TIMING SharedSynAckQueueTiming[MT7922_AWDL_SYN_ACK_TIMING_MAX];
    ULONGLONG FirstMdnsQueuedAt;
    MT7922_AWDL_MDNS_TIMING FirstMdnsTiming;
    ULONG SharedMifReleased;
    ULONG MdnsAnnounced;
    ULONG ServiceMifsReleased;
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
    ULONG RxAmpduTimestamp;
    ULONG RxAmpduRun;
    ULONG FastAckSubmitted;
    ULONG FastAckAcked;
    ULONG FastAckNoAck;
    ULONG FastAckReportRate;
    ULONG UploadShortPolls;
    ULONG UploadShortPollTimeUs;
    volatile LONG ShortPollActive;
    BOOLEAN ShortPollRequested;
    ULONG BaReorderSessions;
    ULONG BaReorderDirect;
    ULONG BaReorderReleased;
    ULONG BaReorderDuplicates;
    ULONG BaReorderHoles;
    ULONG BaReorderBars;
    ULONG BaReorderTimeouts;
    MT7922_AWDL_ADDBA SharedAddba;
    struct ba_reorder Reorder;
    UCHAR ReorderInput[sizeof(ULONGLONG) + 1 + MT7922_AWDL_MAX_AMSDU_BYTES];
    UCHAR AmsduBody[MT7922_AWDL_MAX_AMSDU_BYTES];
    UCHAR AmsduRxRecord[MT7922_AWDL_MAX_RX_RECORD];
    size_t AmsduBodyBytes;
    size_t AmsduRxRecordBytes;
    ULONGLONG AmsduArrival;
    USHORT AmsduSequence;
    BOOLEAN AmsduNegotiated;
    BOOLEAN ReceiveAmsdu;
    MT7922_AWDL_SHARED_AMSDU_REPORT AmsduReport;
    USHORT ReorderStart;
    UCHAR ReorderTid;
    UCHAR ReorderToken;
    BOOLEAN ReorderRequested;
    BOOLEAN AnnounceMdns;
    BOOLEAN AdvertiseVht;
    BOOLEAN AdvertiseHe;
    MT7922_AWDL_SHARED_HE_REPORT HeReport;
    const UCHAR *ServiceLabel;
    USHORT ReceivePort;
    USHORT Mtu;
    UCHAR Packet[MT7922_AWDL_MAX_MTU];
    UCHAR SecondUploadPacket[MT7922_AWDL_MAX_MTU];
    UCHAR ThirdUploadPacket[MT7922_AWDL_MAX_MTU];
    UCHAR Frame[AWDL_TX_AMSDU3_FRAME_BYTES];
} MT7922_AWDL_LINK;

static inline ULONG Mt7922AwdlLinkMtu(const MT7922_AWDL_LINK *Link)
{
    return Link->Mtu != 0 ? Link->Mtu : MT7922_AWDL_LINK_MTU;
}

bool Mt7922McuSharedAvailable(const MT7922_MCU_STATION *Station);
void Mt7922McuAttachSharedLink(MT7922_MCU_STATION *Station, MT7922_AWDL_LINK *Link, const uint8_t *Peer);
void Mt7922McuDetachSharedLink(MT7922_MCU_STATION *Station);

typedef struct _MT7922_AWDL_WATCH {
    const UCHAR *Peers;
    MT7922_AWDL_WATCH_INFO Info;
} MT7922_AWDL_WATCH;

NTSTATUS Mt7922RunMcuProbe(PUCHAR Window, ULONG ChipId, WDFDMAENABLER DmaEnabler,
    WDFCOMMONBUFFER *RetainedBuffer, const UCHAR *Image, SIZE_T ImageBytes,
    BOOLEAN ReceiveAwdl, UCHAR TransmitChannel, BOOLEAN Wide80, const UCHAR *TargetPeer, USHORT TcpPort, USHORT RateCode,
    MT7922_AWDL_LINK *Link, MT7922_AWDL_WATCH *Watch, MT7922_MCU_PROBE_INFO *Result);
NTSTATUS Mt7922StopMcuDma(PUCHAR Window);
