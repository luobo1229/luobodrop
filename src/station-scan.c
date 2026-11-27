/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Adapted from the Connac2 scan path in mt76_connac_mcu.c (MediaTek Inc.). */
#include "station-scan.h"

#include <string.h>

#define MCU_RX_HEADER_BYTES 36U
#define MCU_CONNAC3_RX_HEADER_BYTES 44U
#define MCU_RX_EVENT_TYPE 7U
#define MCU_TX_FORMAT (2U << 23)
#define MCU_TX_QUEUE (0x20U << 25)
#define MCU_TX_LONG_COMMAND 0x80010000U

#define SCAN_CHANNELS_PER_LIST 32U
#define SCAN_FUNCTION_SPLIT (1U << 5)
#define SCAN_CHANNEL_LIST 4U
#define SCAN_DONE_MIN_BYTES 20U

#define MT7925_SCAN_HDR_BYTES 4U
#define MT7925_SCAN_REQ_TLV_BYTES 20U
#define MT7925_SCAN_SSID_TLV_BYTES 368U
#define MT7925_SCAN_BSSID_TLV_BYTES 16U
#define MT7925_SCAN_CHANNEL_TLV_BYTES 136U
#define MT7925_SCAN_MISC_TLV_BYTES 12U
#define MT7925_SCAN_FIXED_BYTES (MT7925_SCAN_HDR_BYTES + MT7925_SCAN_REQ_TLV_BYTES + \
    MT7925_SCAN_SSID_TLV_BYTES + MT7925_SCAN_BSSID_TLV_BYTES + \
    MT7925_SCAN_CHANNEL_TLV_BYTES + MT7925_SCAN_MISC_TLV_BYTES)
#define MT7925_UNI_SCAN_REQ 1U
#define MT7925_UNI_SCAN_CANCEL 2U
#define MT7925_UNI_SCAN_SSID 10U
#define MT7925_UNI_SCAN_BSSID 11U
#define MT7925_UNI_SCAN_CHANNEL 12U
#define MT7925_UNI_SCAN_IE 13U
#define MT7925_UNI_SCAN_MISC 14U

#pragma pack(push, 1)
typedef struct {
    uint8_t Length[4];
    uint8_t Data[MT7922_SCAN_MAX_SSID_BYTES];
} SCAN_SSID;

typedef struct {
    uint8_t Sequence;
    uint8_t BssIndex;
    uint8_t Active;
    uint8_t SsidType;
    uint8_t SsidCount;
    uint8_t ProbeCount;
    uint8_t Function;
    uint8_t Version;
    SCAN_SSID Ssids[MT7922_SCAN_MAX_SSIDS];
    uint8_t ProbeDelay[2];
    uint8_t Dwell[2];
    uint8_t Timeout[2];
    uint8_t ChannelType;
    uint8_t ChannelCount;
    MT7922_SCAN_CHANNEL Channels[SCAN_CHANNELS_PER_LIST];
    uint8_t IeBytes[2];
    uint8_t InformationElements[MT7922_SCAN_MAX_IE_BYTES];
    uint8_t ExtendedChannelCount;
    uint8_t ExtendedSsidCount;
    uint8_t MinimumDwell[2];
    MT7922_SCAN_CHANNEL ExtendedChannels[SCAN_CHANNELS_PER_LIST];
    SCAN_SSID ExtendedSsids[6];
    uint8_t Bssid[6];
    uint8_t RandomMac[6];
    uint8_t Reserved[63];
    uint8_t SsidTypeExtension;
} SCAN_REQUEST;
#pragma pack(pop)

_Static_assert(sizeof(SCAN_REQUEST) == MT7922_SCAN_REQUEST_BYTES, "Connac2 scan request size");
_Static_assert(offsetof(SCAN_REQUEST, ExtendedChannelCount) == 826U, "Connac2 extension offset");
_Static_assert(offsetof(SCAN_REQUEST, SsidTypeExtension) == 1185U, "Connac2 SSID type offset");

bool Mt7922BuildScanRequest(uint8_t *Buffer, size_t Bytes, const MT7922_SCAN_PARAMETERS *Parameters)
{
    if (Buffer == NULL || Bytes < sizeof(SCAN_REQUEST) || Parameters == NULL ||
        Parameters->Sequence > 0x7fU || Parameters->BandIndex > 1 ||
        Parameters->SsidCount > MT7922_SCAN_MAX_SSIDS ||
        Parameters->ChannelCount > MT7922_SCAN_MAX_CHANNELS ||
        Parameters->InformationElementBytes > MT7922_SCAN_MAX_IE_BYTES ||
        (Parameters->SsidCount != 0 && (Parameters->Ssids == NULL || !Parameters->Active)) ||
        (Parameters->ChannelCount != 0 && Parameters->Channels == NULL) ||
        (Parameters->InformationElementBytes != 0 && Parameters->InformationElements == NULL)) {
        return false;
    }

    SCAN_REQUEST request = {0};
    request.Sequence = (uint8_t)(Parameters->Sequence | (Parameters->BandIndex << 7));
    request.BssIndex = Parameters->BssIndex;
    request.Active = Parameters->Active;
    request.ProbeCount = Parameters->Active ? 2 : 0;
    request.Function = SCAN_FUNCTION_SPLIT; /* Firmware supplies dwell times for Connac2. */
    request.Version = 1;
    if (Parameters->Bssid != NULL) {
        memcpy(request.Bssid, Parameters->Bssid, sizeof(request.Bssid));
    } else {
        memset(request.Bssid, 0xff, sizeof(request.Bssid));
    }
    request.Dwell[0] = (uint8_t)Parameters->DwellMilliseconds;
    request.Dwell[1] = (uint8_t)(Parameters->DwellMilliseconds >> 8);
    memcpy(request.MinimumDwell, request.Dwell, sizeof(request.Dwell));
    request.Timeout[0] = (uint8_t)Parameters->TimeoutMilliseconds;
    request.Timeout[1] = (uint8_t)(Parameters->TimeoutMilliseconds >> 8);

    for (size_t i = 0; i < Parameters->SsidCount; ++i) {
        const MT7922_SCAN_SSID *ssid = &Parameters->Ssids[i];
        if (ssid->Length > sizeof(ssid->Data)) {
            return false;
        }
        if (ssid->Length != 0) {
            SCAN_SSID *output = &request.Ssids[request.SsidCount++];
            output->Length[0] = ssid->Length;
            memcpy(output->Data, ssid->Data, ssid->Length);
        }
    }
    request.SsidType = request.SsidCount != 0 ? (1U << 2) : (1U << 0);
    request.SsidTypeExtension = request.SsidCount != 0;

    if (Parameters->ChannelCount != 0) {
        request.ChannelType = SCAN_CHANNEL_LIST;
        const size_t mainCount = Parameters->ChannelCount < SCAN_CHANNELS_PER_LIST
            ? Parameters->ChannelCount : SCAN_CHANNELS_PER_LIST;
        request.ChannelCount = (uint8_t)mainCount;
        request.ExtendedChannelCount = (uint8_t)(Parameters->ChannelCount - mainCount);
        for (size_t i = 0; i < Parameters->ChannelCount; ++i) {
            const MT7922_SCAN_CHANNEL channel = Parameters->Channels[i];
            if (channel.Band < 1 || channel.Band > 3 || channel.Number == 0) {
                return false;
            }
            if (i < mainCount) {
                request.Channels[i] = channel;
            } else {
                request.ExtendedChannels[i - mainCount] = channel;
            }
        }
    }

    if (Parameters->InformationElementBytes != 0) {
        request.IeBytes[0] = (uint8_t)Parameters->InformationElementBytes;
        request.IeBytes[1] = (uint8_t)(Parameters->InformationElementBytes >> 8);
        memcpy(request.InformationElements, Parameters->InformationElements,
            Parameters->InformationElementBytes);
    }
    memcpy(Buffer, &request, sizeof(request));
    return true;
}

bool Mt7922LimitStationScan(uint8_t *Buffer, const uint8_t *Input,
    const MT7922_STATION_RADIO_PROFILE *Radio)
{
    SCAN_REQUEST request;
    memcpy(&request, Input, sizeof(request));
    if ((request.ChannelType != 0 && request.ChannelType != SCAN_CHANNEL_LIST) ||
        request.ChannelCount > SCAN_CHANNELS_PER_LIST ||
        request.ExtendedChannelCount > SCAN_CHANNELS_PER_LIST ||
        ((request.ChannelType == SCAN_CHANNEL_LIST) != (request.ChannelCount != 0)) ||
        (request.ExtendedChannelCount != 0 && request.ChannelCount != SCAN_CHANNELS_PER_LIST)) {
        return false;
    }
    bool allow[256] = {0};
    for (size_t i = 0; i < request.ChannelCount; ++i) {
        if (request.Channels[i].Band == 2) {
            allow[request.Channels[i].Number] = true;
        }
    }
    for (size_t i = 0; i < request.ExtendedChannelCount; ++i) {
        if (request.ExtendedChannels[i].Band == 2) {
            allow[request.ExtendedChannels[i].Number] = true;
        }
    }
    const bool all = request.ChannelType == 0;
    request.ChannelCount = request.ExtendedChannelCount = 0;
    memset(request.Channels, 0, sizeof(request.Channels));
    memset(request.ExtendedChannels, 0, sizeof(request.ExtendedChannels));
    for (size_t i = 0; i < Radio->ChannelCount; ++i) {
        const uint8_t number = Radio->Channels[i].Number;
        if (all || allow[number]) {
            request.Channels[request.ChannelCount++] = (MT7922_SCAN_CHANNEL){2, number};
        }
    }
    if (request.ChannelCount == 0) {
        return false;
    }
    request.ChannelType = SCAN_CHANNEL_LIST;
    memcpy(Buffer, &request, sizeof(request));
    return true;
}

static uint16_t ScanReadLe16(const uint8_t *Bytes)
{
    return (uint16_t)(Bytes[0] | ((uint16_t)Bytes[1] << 8));
}

static uint32_t ScanReadLe32(const uint8_t *Bytes)
{
    return (uint32_t)Bytes[0] | ((uint32_t)Bytes[1] << 8) |
        ((uint32_t)Bytes[2] << 16) | ((uint32_t)Bytes[3] << 24);
}

static void ScanWriteLe16(uint8_t *Bytes, uint16_t Value)
{
    Bytes[0] = (uint8_t)Value;
    Bytes[1] = (uint8_t)(Value >> 8);
}

static void ScanWriteLe32(uint8_t *Bytes, uint32_t Value)
{
    Bytes[0] = (uint8_t)Value;
    Bytes[1] = (uint8_t)(Value >> 8);
    Bytes[2] = (uint8_t)(Value >> 16);
    Bytes[3] = (uint8_t)(Value >> 24);
}

size_t Mt7925BuildScanRequest(uint8_t *Buffer, size_t Capacity,
    const uint8_t *Connac2Request, size_t RequestBytes)
{
    if (Buffer == NULL || Connac2Request == NULL || RequestBytes != sizeof(SCAN_REQUEST)) {
        return 0;
    }

    SCAN_REQUEST request;
    memcpy(&request, Connac2Request, sizeof(request));
    const uint16_t ieBytes = ScanReadLe16(request.IeBytes);
    if (request.Active > 1 || request.SsidCount > MT7922_SCAN_MAX_SSIDS ||
        (request.SsidCount != 0 && request.Active == 0) ||
        request.ChannelType != SCAN_CHANNEL_LIST || request.ChannelCount == 0 ||
        request.ChannelCount > SCAN_CHANNELS_PER_LIST || request.ExtendedChannelCount != 0 ||
        request.ExtendedSsidCount != 0 || ieBytes > MT7922_SCAN_MAX_IE_BYTES) {
        return 0;
    }
    for (size_t i = 0; i < request.SsidCount; ++i) {
        const uint32_t ssidBytes = ScanReadLe32(request.Ssids[i].Length);
        if (ssidBytes > MT7922_SCAN_MAX_SSID_BYTES) {
            return 0;
        }
    }
    for (size_t i = 0; i < request.ChannelCount; ++i) {
        if (request.Channels[i].Band != 2 || request.Channels[i].Number == 0) {
            return 0;
        }
    }

    const size_t ieTlvBytes = ieBytes == 0 ? 0 : 8U + ieBytes;
    const size_t totalBytes = MT7925_SCAN_FIXED_BYTES + ieTlvBytes;
    if (Capacity < totalBytes || totalBytes > MT7925_SCAN_REQUEST_MAX_BYTES) {
        return 0;
    }

    memset(Buffer, 0, totalBytes);
    Buffer[0] = request.Sequence;
    Buffer[1] = request.BssIndex;

    size_t offset = MT7925_SCAN_HDR_BYTES;
    ScanWriteLe16(Buffer + offset, MT7925_UNI_SCAN_REQ);
    ScanWriteLe16(Buffer + offset + 2, MT7925_SCAN_REQ_TLV_BYTES);
    Buffer[offset + 4] = request.Active ? 1U : 0U;
    Buffer[offset + 5] = request.Active ? request.ProbeCount : 0U;
    Buffer[offset + 6] = (uint8_t)(request.Function | SCAN_FUNCTION_SPLIT);
    memcpy(Buffer + offset + 8, request.MinimumDwell, 2);
    memcpy(Buffer + offset + 10, request.Dwell, 2);
    memcpy(Buffer + offset + 12, request.Timeout, 2);
    memcpy(Buffer + offset + 14, request.ProbeDelay, 2);
    offset += MT7925_SCAN_REQ_TLV_BYTES;

    ScanWriteLe16(Buffer + offset, MT7925_UNI_SCAN_SSID);
    ScanWriteLe16(Buffer + offset + 2, MT7925_SCAN_SSID_TLV_BYTES);
    size_t ssidCount = 0;
    for (size_t i = 0; i < request.SsidCount; ++i) {
        const uint32_t ssidBytes = ScanReadLe32(request.Ssids[i].Length);
        if (ssidBytes == 0) {
            continue;
        }
        uint8_t *ssid = Buffer + offset + 8U + ssidCount * 36U;
        ScanWriteLe32(ssid, ssidBytes);
        memcpy(ssid + 4, request.Ssids[i].Data, ssidBytes);
        ++ssidCount;
    }
    Buffer[offset + 4] = ssidCount != 0 ? (1U << 2) : (1U << 0);
    Buffer[offset + 5] = (uint8_t)ssidCount;
    offset += MT7925_SCAN_SSID_TLV_BYTES;

    ScanWriteLe16(Buffer + offset, MT7925_UNI_SCAN_BSSID);
    ScanWriteLe16(Buffer + offset + 2, MT7925_SCAN_BSSID_TLV_BYTES);
    memcpy(Buffer + offset + 4, request.Bssid, sizeof(request.Bssid));
    offset += MT7925_SCAN_BSSID_TLV_BYTES;

    ScanWriteLe16(Buffer + offset, MT7925_UNI_SCAN_CHANNEL);
    ScanWriteLe16(Buffer + offset + 2, MT7925_SCAN_CHANNEL_TLV_BYTES);
    Buffer[offset + 4] = SCAN_CHANNEL_LIST;
    Buffer[offset + 5] = request.ChannelCount;
    for (size_t i = 0; i < request.ChannelCount; ++i) {
        Buffer[offset + 8U + 2U * i] = request.Channels[i].Band;
        Buffer[offset + 9U + 2U * i] = request.Channels[i].Number;
    }
    offset += MT7925_SCAN_CHANNEL_TLV_BYTES;

    ScanWriteLe16(Buffer + offset, MT7925_UNI_SCAN_MISC);
    ScanWriteLe16(Buffer + offset + 2, MT7925_SCAN_MISC_TLV_BYTES);
    memcpy(Buffer + offset + 4, request.RandomMac, sizeof(request.RandomMac));
    offset += MT7925_SCAN_MISC_TLV_BYTES;

    if (ieBytes != 0) {
        ScanWriteLe16(Buffer + offset, MT7925_UNI_SCAN_IE);
        ScanWriteLe16(Buffer + offset + 2, (uint16_t)(8U + ieBytes));
        ScanWriteLe16(Buffer + offset + 4, ieBytes);
        Buffer[offset + 6] = 2; /* 5 GHz. */
        memcpy(Buffer + offset + 8, request.InformationElements, ieBytes);
    }
    return totalBytes;
}

size_t Mt7925BuildScanCancel(uint8_t *Buffer, size_t Capacity, uint8_t Sequence, uint8_t BssIndex)
{
    if (Buffer == NULL || Capacity < MT7925_SCAN_CANCEL_BYTES) {
        return 0;
    }
    memset(Buffer, 0, MT7925_SCAN_CANCEL_BYTES);
    Buffer[0] = Sequence;
    Buffer[1] = BssIndex;
    ScanWriteLe16(Buffer + 4, MT7925_UNI_SCAN_CANCEL);
    ScanWriteLe16(Buffer + 6, 8U);
    return MT7925_SCAN_CANCEL_BYTES;
}

bool Mt7922BuildScanCancel(uint8_t *Buffer, size_t Bytes, uint8_t Sequence)
{
    if (Buffer == NULL || Bytes < MT7922_SCAN_CANCEL_BYTES || Sequence > 0x7fU) {
        return false;
    }
    const uint8_t request[MT7922_SCAN_CANCEL_BYTES] = {Sequence, 0, 0, 0};
    memcpy(Buffer, request, sizeof(request));
    return true;
}

bool Mt7922IsScanDone(uint8_t Event, const uint8_t *Payload, size_t Bytes,
    uint8_t Sequence, uint8_t BandIndex)
{
    return Event == MT7922_SCAN_DONE_EVENT && Payload != NULL && Bytes >= SCAN_DONE_MIN_BYTES &&
        Sequence <= 0x7fU && BandIndex <= 1 && Payload[0] == (Sequence | (BandIndex << 7));
}


size_t Mt7922FrameScanCommand(uint8_t *Buffer, size_t Bytes, uint8_t Command,
    uint8_t McuSequence, const uint8_t *Payload, size_t PayloadBytes)
{
    const size_t required = Command == MT7922_SCAN_START_COMMAND ? MT7922_SCAN_REQUEST_BYTES :
        Command == MT7922_SCAN_CANCEL_COMMAND ? MT7922_SCAN_CANCEL_BYTES : 0;
    return required != 0 && PayloadBytes == required ?
        Mt7922FrameLegacyMcuCommand(Buffer, Bytes, Command, 0, 1, McuSequence, Payload, PayloadBytes) : 0;
}

size_t Mt7922FrameLegacyMcuCommand(uint8_t *Buffer, size_t Bytes, uint8_t Command,
    uint8_t Extension, uint8_t SetQuery, uint8_t Sequence, const uint8_t *Payload, size_t PayloadBytes)
{
    const size_t header = MT7922_SCAN_TX_HEADER_BYTES;
    if (Buffer == NULL || Bytes < header || PayloadBytes > Bytes - header ||
        PayloadBytes > UINT16_MAX - header || (Payload == NULL && PayloadBytes != 0) ||
        Sequence == 0 || Sequence > 15) {
        return 0;
    }
    const size_t total = header + PayloadBytes;
    const uint32_t descriptor = (uint32_t)total | MCU_TX_FORMAT | MCU_TX_QUEUE;
    const uint32_t longCommand = MCU_TX_LONG_COMMAND;
    const size_t mcuBytes = total - 32;
    memset(Buffer, 0, MT7922_SCAN_TX_HEADER_BYTES);
    for (unsigned i = 0; i < 4; ++i) {
        Buffer[i] = (uint8_t)(descriptor >> (8U * i));
        Buffer[4 + i] = (uint8_t)(longCommand >> (8U * i));
    }
    Buffer[32] = (uint8_t)mcuBytes;
    Buffer[33] = (uint8_t)(mcuBytes >> 8);
    Buffer[35] = 0x80; /* WM port and queue zero. */
    Buffer[36] = Command;
    Buffer[37] = 0xa0; /* MCU packet ID. */
    Buffer[38] = SetQuery;
    Buffer[39] = Sequence;
    Buffer[41] = Extension;
    Buffer[43] = Extension != 0;
    if (PayloadBytes != 0) {
        memcpy(Buffer + header, Payload, PayloadBytes);
    }
    return total;
}

bool Mt7922MatchScanRecord(const uint8_t *Record, size_t Bytes,
    uint8_t Sequence, uint8_t BandIndex)
{
    return Mt792xMatchScanRecord(0x7922U, Record, Bytes, Sequence, BandIndex);
}

bool Mt792xMatchScanRecord(uint16_t ChipId, const uint8_t *Record, size_t Bytes,
    uint8_t Sequence, uint8_t BandIndex)
{
    if (Sequence > 0x7fU || BandIndex > 1) {
        return false;
    }
    const size_t headerBytes = ChipId == 0x7925U
        ? MCU_CONNAC3_RX_HEADER_BYTES : MCU_RX_HEADER_BYTES;
    const size_t eventOffset = headerBytes - 8U;
    if (Record == NULL || Bytes < headerBytes ||
        (Record[3] >> 3) != MCU_RX_EVENT_TYPE || (Record[2] & 0x0fU) == 1U) {
        return false;
    }
    const size_t recordBytes = Record[0] | ((size_t)Record[1] << 8);
    if (recordBytes < headerBytes || recordBytes > Bytes) {
        return false;
    }
    const size_t payloadOffset = headerBytes + (ChipId == 0x7925U ? 4U : 0U);
    if (recordBytes < payloadOffset) {
        return false;
    }
    const size_t payloadBytes = recordBytes - payloadOffset;
    if (ChipId == 0x7925U) {
        if (Record[eventOffset] != MT7925_SCAN_DONE_EVENT || payloadBytes < 4U) {
            return false;
        }
        const uint8_t *tlv = Record + payloadOffset;
        const uint16_t tlvBytes = ScanReadLe16(tlv + 2);
        return ScanReadLe16(tlv) == 0 && tlvBytes >= 4U && tlvBytes <= payloadBytes;
    }
    return Mt7922IsScanDone(Record[eventOffset], Record + payloadOffset,
        payloadBytes, Sequence, BandIndex);
}
