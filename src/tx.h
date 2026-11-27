#pragma once

#include <stddef.h>
#include <stdint.h>
#include "rx.h"

#define MT7922_TXWI_BYTES 64U
#define MT7922_PROBE_PSF_CAPACITY 320U
#define MT7922_AWDL_SERVICE_MIF_CAPACITY 432U
#define AWDL_AIRDROP_ID_BYTES 12U
#define MT7922_OFDM_6_MBPS 0x004bU
#define MT7922_OFDM_12_MBPS 0x004aU
#define MT7922_OFDM_24_MBPS 0x0049U
#define MT7922_OFDM_54_MBPS 0x004cU
#define MT7922_TX_STATUS_PACKET_TYPE 0U
#define MT7922_TX_RELEASE_PACKET_TYPE 6U
#define MT7922_TX_QUEUE_ALTX0 16U

typedef struct _MT7922_AWDL_TX {
    uint32_t FrameAddress;
    uint16_t Wcid;
    uint16_t Token;
    uint8_t PacketId;
    uint8_t OwnMacIndex;
    uint8_t Queue;
    uint16_t RateCode;
    uint8_t SpeIndex;
    uint8_t Ampdu;
    uint16_t ChipId; /* 0/0x7922 selects Connac2; 0x7925 selects Connac3. */
} MT7922_AWDL_TX;

typedef enum _MT7922_TX_REPORT_RESULT {
    Mt7922TxReportMalformed,
    Mt7922TxReportUnmatched,
    Mt7922TxReportMatched
} MT7922_TX_REPORT_RESULT;

typedef struct _MT7922_TX_REPORT {
    uint16_t RateCode;
    uint16_t Sequence;
    uint8_t AckErrors; /* Zero indicates ACK success for a unicast request. */
    uint8_t Ampdu;
    uint8_t Bandwidth;
} MT7922_TX_REPORT;

typedef struct _MT7922_TX_RELEASE {
    uint16_t Attempts;
    uint8_t Status;
} MT7922_TX_RELEASE;

/* Build a chip-specific TXD and one-buffer TXP. ChipId 0 preserves the
   original MT7922 builder for existing callers. The caller owns the DMA frame
   until the TX token is released by the device or the DMA engine is stopped. */
int Mt7922BuildAwdlTxwi(uint8_t *Txwi, const MT7922_AWDL_TX *Request,
    const uint8_t *Frame, size_t FrameBytes);
int Mt7922BuildStationMgmtTxwi(uint8_t *Txwi, const MT7922_AWDL_TX *Request,
    const uint8_t *Frame, size_t FrameBytes);
int Mt7922BuildStationDataTxwi(uint8_t *Txwi, const MT7922_AWDL_TX *Request,
    const uint8_t *Frame, size_t FrameBytes);
MT7922_TX_REPORT_RESULT Mt7922ReadTxReport(const uint8_t *Data, size_t Length,
    const MT7922_AWDL_TX *Request, MT7922_TX_REPORT *Report);
MT7922_TX_REPORT_RESULT Mt7922ReadTxRelease(const uint8_t *Data, size_t Length,
    uint16_t Token, MT7922_TX_RELEASE *Release);
MT7922_TX_REPORT_RESULT Mt792xReadTxReport(uint16_t ChipId, const uint8_t *Data,
    size_t Length, const MT7922_AWDL_TX *Request, MT7922_TX_REPORT *Report);
MT7922_TX_REPORT_RESULT Mt792xReadTxRelease(uint16_t ChipId, const uint8_t *Data,
    size_t Length, uint16_t Token, MT7922_TX_RELEASE *Release);

size_t AwdlBuildProbePsf(uint8_t *Frame, size_t Capacity, const uint8_t *Source,
    const AWDL_ACTION *Peer, uint32_t Timestamp, uint8_t Channel);
size_t AwdlBuildAdvert(uint8_t *Frame, size_t Capacity, const uint8_t *Source,
    const AWDL_ACTION *Peer, uint32_t Timestamp, int Mif, uint8_t DeviceClass, uint8_t Channel);
size_t AwdlBuildServiceMif(uint8_t *Frame, size_t Capacity, const uint8_t *Source,
    const AWDL_ACTION *Peer, uint32_t Timestamp, uint8_t DeviceClass, uint8_t Channel,
    const uint8_t *ServiceId, uint16_t Port);
size_t AwdlAppendVhtCapabilities(uint8_t *Frame, size_t Capacity, size_t Length);
int AwdlSetHighEfficiency(uint8_t *Frame, size_t Length);
size_t AwdlBuildDataProbe(uint8_t *Frame, size_t Capacity, const uint8_t *Dest,
    const uint8_t *Source, uint8_t Sequence);
size_t AwdlBuildUnicastProbe(uint8_t *Frame, size_t Capacity, const uint8_t *Dest,
    const uint8_t *Source, const AWDL_ACTION *Peer, uint32_t Timestamp,
    uint8_t DeviceClass);
int AwdlIsPureTcpAck(const uint8_t *Ip, size_t Length, uint16_t SourcePort);
int AwdlIsMdnsMulticast(const uint8_t *Ip, size_t Length);
size_t AwdlBuildIpFrame(uint8_t *Frame, size_t Capacity, const uint8_t *Dest,
    const uint8_t *Source, const uint8_t *Ip, size_t IpBytes, uint16_t Sequence);
size_t AwdlBuildQosIpFrame(uint8_t *Frame, size_t Capacity, const uint8_t *Dest,
    const uint8_t *Source, const uint8_t *Ip, size_t IpBytes, uint16_t Sequence);
#define AWDL_TX_AMSDU_FRAME_BYTES 3088U /* Two IPv6 packets of up to 1500 bytes. */
#define AWDL_TX_AMSDU3_FRAME_BYTES 4620U /* Three IPv6 packets of up to 1500 bytes. */
size_t AwdlBuildQosAmsduFrame(uint8_t *Frame, size_t Capacity, const uint8_t *Dest,
    const uint8_t *Source, const uint8_t *First, size_t FirstBytes,
    const uint8_t *Second, size_t SecondBytes, const uint8_t *Third, size_t ThirdBytes,
    uint16_t Sequence);
size_t AwdlBuildAddbaRequest(uint8_t *Frame, size_t Capacity, const uint8_t *Dest,
    const uint8_t *Source, uint8_t Token, uint16_t Window, uint16_t Timeout, uint16_t StartingSequence,
    int AllowAmsdu);
size_t AwdlBuildAddbaResponse(uint8_t *Frame, size_t Capacity, const uint8_t *Dest,
    const uint8_t *Source, uint8_t Token, uint16_t RequestedParams, uint16_t Timeout,
    uint16_t Sequence);
