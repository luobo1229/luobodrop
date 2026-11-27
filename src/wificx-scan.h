#ifndef MT7922_WIFI_CX_SCAN_H
#define MT7922_WIFI_CX_SCAN_H

#include "station-scan.h"
#include "station-port.h"

struct _WDI_SCAN_PARAMETERS;
struct _WDI_CONNECT_BSS_ENTRY_CONTAINER;

bool Mt7922ReadWdiStationAp(const struct _WDI_CONNECT_BSS_ENTRY_CONTAINER *Entry,
    MT7922_STATION_AP *Ap);

bool Mt7922BuildWdiScanRequest(uint8_t *Buffer, size_t Bytes,
    const struct _WDI_SCAN_PARAMETERS *Scan, uint8_t Sequence, uint8_t BandIndex,
    uint8_t BssIndex);

struct _WDI_MESSAGE_HEADER;
struct _TLV_CONTEXT;

typedef int32_t (*MT7922_SCAN_SUBMIT)(void *Context, uint8_t Command,
    const uint8_t *Payload, size_t Bytes);
typedef void (*MT7922_SCAN_INDICATE)(void *Context, uint16_t Message,
    const uint8_t *Buffer, size_t Bytes);

/* All operations are serialized by the station worker. Indications copy the
   supplied buffer before returning. The TLV context is owned by the adapter. */
typedef struct {
    void *Transport;
    MT7922_SCAN_SUBMIT Submit;
    void *Host;
    MT7922_SCAN_INDICATE Indicate;
    const struct _TLV_CONTEXT *Tlv;
    uint32_t TransactionId;
    uint32_t IhvSpecificId;
    uint32_t ReportedEntries;
    uint16_t PortId;
    uint16_t ChipId;
    uint8_t Sequence;
    uint8_t BandIndex;
    bool Active;
    bool Cancelling;
} MT7922_WDI_SCAN;

int32_t Mt7922StartWdiScan(MT7922_WDI_SCAN *State,
    const struct _WDI_MESSAGE_HEADER *Header, const struct _WDI_SCAN_PARAMETERS *Scan,
    uint8_t BandIndex, uint8_t BssIndex);
int32_t Mt7922CancelWdiScan(MT7922_WDI_SCAN *State);
bool Mt7922WdiScanEvent(MT7922_WDI_SCAN *State, const uint8_t *Record, size_t Bytes);
bool Mt7922WdiScanFrame(MT7922_WDI_SCAN *State, const uint8_t *Record, size_t Bytes);
/* On the worker's deadline, complete the Windows task and quarantine its
   firmware token until a matching event arrives or the device stops. */
void Mt7922ExpireWdiScan(MT7922_WDI_SCAN *State);

struct _WDI_TASK_CONNECT_PARAMETERS;

/* Optional connect settings observed on the last request (bring-up diagnostics). */
#define MT7922_WDI_FLAG_ROAM         0x00000001U
#define MT7922_WDI_FLAG_HIDDEN       0x00000002U
#define MT7922_WDI_FLAG_MFP          0x00000004U
#define MT7922_WDI_FLAG_HOST_FIPS    0x00000008U
#define MT7922_WDI_FLAG_FIPS         0x00000010U
#define MT7922_WDI_FLAG_BSS_TRANS    0x00000020U
#define MT7922_WDI_FLAG_MSCS         0x00000040U
#define MT7922_WDI_FLAG_DSCP_UP      0x00000080U
#define MT7922_WDI_FLAG_OWE          0x00000100U
#define MT7922_WDI_FLAG_HESSID       0x00000200U
#define MT7922_WDI_FLAG_AKM_CIPHER   0x00000400U
#define MT7922_WDI_FLAG_AKM          0x00000800U

typedef struct {
    MT7922_STATION_AP Ap;
    MT7922_STATION_JOIN_REQUEST Join;
    uint32_t AuthAlgorithm;
    uint32_t UnicastCipher;
    uint32_t Flags;
    /* First candidate and why it failed: 1 read, 2 band, 3 channel, 4 PHY, 5 IEs. */
    uint32_t CandidateCount;
    uint32_t FirstBandId;
    uint32_t FirstChannel;
    uint32_t FirstBody; /* 0 none, 1 beacon, 2 probe response. */
    uint32_t CandidateReason;
    /* First candidate IE screening detail, see MT7922_CONNECT_DEBUG.IeFail. */
    uint32_t IeFail;
    uint32_t BodyBytes;
    uint32_t ParamsVendorBytes;
    uint32_t EntryVendorBytes;
    uint8_t BodySample[MT7922_CONNECT_DEBUG_BODY_SAMPLE];
    uint8_t ParamsVendorSample[MT7922_CONNECT_DEBUG_VENDOR_SAMPLE];
    uint8_t EntryVendorSample[MT7922_CONNECT_DEBUG_VENDOR_SAMPLE];
    uint8_t Elements[MT7922_STATION_ASSOC_IE_MAX];
} MT7922_WDI_CONNECT_PLAN;

/* Select the first eligible 5 GHz WPA2-Personal CCMP candidate in Windows order. */
int32_t Mt7922MakeWdiConnectPlan(const struct _WDI_TASK_CONNECT_PARAMETERS *Parameters,
    uint16_t Wcid, const MT7922_STATION_RADIO_PROFILE *Radio, MT7922_WDI_CONNECT_PLAN *Plan);

struct _WDI_SET_PRIVACY_EXEMPTION_LIST_PARAMETERS;

int32_t Mt7922ReadWdiEapolPrivacy(const struct _WDI_SET_PRIVACY_EXEMPTION_LIST_PARAMETERS *Parameters,
    MT7922_PRIVACY *Privacy);

struct _WDI_SET_ADD_CIPHER_KEYS_PARAMETERS;

typedef int32_t (*MT7922_INSTALL_CCMP)(void *Context, bool Group, uint8_t KeyId,
    const uint8_t Key[MT7922_CCMP_KEY_BYTES], uint64_t InitialRxPn);

/* These functions use the Microsoft WDK's request parser and indication generator. */
int32_t Mt7922IndicateWdiStationJoin(const struct _WDI_MESSAGE_HEADER *Header,
    const struct _TLV_CONTEXT *Tlv, const uint8_t Bssid[6], bool Wpa2PersonalCcmp,
    const MT7922_STATION_JOIN_RESULT *Result, int32_t JoinStatus,
    MT7922_SCAN_INDICATE Indicate, void *Context);
int32_t Mt7922IndicateWdiDisconnect(const struct _TLV_CONTEXT *Tlv, uint16_t PortId,
    const uint8_t Bssid[6], const uint8_t *PeerFrame, bool Reset,
    MT7922_SCAN_INDICATE Indicate, void *Context);
int32_t Mt7922ApplyWdiCcmpKeys(const struct _WDI_SET_ADD_CIPHER_KEYS_PARAMETERS *Parameters,
    const uint8_t Bssid[6], MT7922_INSTALL_CCMP Install, void *Context);

#endif

