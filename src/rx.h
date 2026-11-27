#pragma once

#include <stddef.h>
#include <stdint.h>

#define MT7922_RX_SKIP_FCS 0x01U
#define MT7922_RX_SKIP_TRANSLATED 0x02U
#define MT7922_RX_SKIP_AMSDU 0x04U
#define MT7922_RX_SKIP_LENGTH 0x08U
#define MT7922_RX_SKIP_GROUPS 0x10U

typedef struct _MT7922_RX_FRAME {
    const uint8_t *Bytes;
    size_t Length;
    uint8_t SkipFlags;
    uint8_t AmsduPart;
    uint8_t HeaderTranslationError;
    uint8_t PhyPresent;
    uint8_t PhyMode;
    uint8_t PhyWidth;
    uint8_t PhyMcs;
    uint8_t Ampdu;
    uint32_t AmpduTimestamp;
    uint8_t Channel;
    uint8_t Band; /* Firmware band numbering, as used by the station scan request. */
    int8_t RssiDbm;
    uint8_t RssiPresent;
    uint16_t ReceiveWcid;
    uint8_t ReceiveKeyId;
    uint8_t CcmpDecrypted;
    uint64_t CcmpPacketNumber;
} MT7922_RX_FRAME;

typedef enum _MT7922_RX_STATUS {
    Mt7922RxMalformed,
    Mt7922RxIgnored,
    Mt7922RxRawFrame
} MT7922_RX_STATUS;

typedef enum _AWDL_ACTION_KIND {
    AwdlMalformed,
    AwdlOther,
    AwdlPsf,
    AwdlMif
} AWDL_ACTION_KIND;

#define AWDL_CHANNEL_SLOTS 16U

typedef struct _AWDL_SYNC {
    const uint8_t *Master;
    uint16_t WindowTu;
    uint16_t UntilNextSlotTu;
    uint16_t NextWindowSequence;
    uint8_t NextChannel;
    uint8_t MasterChannel;
    uint8_t WindowsPerSlot;
} AWDL_SYNC;

typedef struct _AWDL_CHANNEL_SEQUENCE {
    uint8_t Count;
    uint8_t Encoding;
    uint8_t Channel[AWDL_CHANNEL_SLOTS];
    uint8_t OperatingClass[AWDL_CHANNEL_SLOTS];
} AWDL_CHANNEL_SEQUENCE;

typedef struct _AWDL_ELECTION {
    uint32_t MasterCounter;
    uint32_t MasterMetric;
    uint32_t Distance;
    uint8_t Present;
} AWDL_ELECTION;

typedef struct _AWDL_PEER_PHY {
    uint32_t VhtCapabilities;
    uint16_t VhtRxMcs;
    uint16_t VhtTxMcs;
    uint16_t HtCapabilities;
    uint8_t HtAmpdu;
    uint8_t HtRxMcs[2];
    uint8_t HtPresent;
    uint8_t VhtPresent;
} AWDL_PEER_PHY;

typedef struct _AWDL_ACTION {
    AWDL_PEER_PHY Phy;
    AWDL_SYNC Sync;
    AWDL_ELECTION Election;
    AWDL_CHANNEL_SEQUENCE Sequence;
    const uint8_t *Sender;
    const uint8_t *Elements;
    size_t ElementBytes;
    uint32_t PhysicalTx;
    uint32_t TargetTx;
    uint8_t Version;
    uint8_t DeviceClass;
    uint8_t HeSupported;
    AWDL_ACTION_KIND Kind;
} AWDL_ACTION;

/* Returned views borrow the input bytes. SkipFlags is valid for any RX result.
   On success, AmsduPart is the hardware payload format (zero for a standalone frame). */
MT7922_RX_STATUS Mt7922DecodeRx(const uint8_t *Buffer, size_t DmaBytes,
    MT7922_RX_FRAME *Frame);
MT7922_RX_STATUS Mt7925DecodeRx(const uint8_t *Buffer, size_t DmaBytes,
    MT7922_RX_FRAME *Frame);
MT7922_RX_STATUS Mt792xDecodeRx(uint16_t ChipId, const uint8_t *Buffer,
    size_t DmaBytes, MT7922_RX_FRAME *Frame);
AWDL_ACTION_KIND AwdlDecodeAction(const uint8_t *Bytes, size_t Length,
    AWDL_ACTION *Action);
