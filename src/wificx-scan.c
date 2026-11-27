/* Translate the WiFiCx sample's parsed scan parameters to Connac2. */
#include <ntddk.h>
#include <ndis.h>
#include <dot11wificxintf.h>
#include <dot11wificxtypes.hpp>
#include <TlvGeneratorParser.hpp>
#include <string.h>
#include "wificx-scan.h"
#ifdef MT7922_STATION_PACKET_TRACE
#include "station-packet-trace.h"
#endif
#include "rx.h"

static uint8_t FirmwareBand(WDI_BAND_ID Band)
{
    switch (Band) {
    case WDI_BAND_ID_2400: return 1;
    case WDI_BAND_ID_5000: return 2;
    case WDI_BAND_ID_6000: return 3;
    default: return 0;
    }
}

bool Mt7922BuildWdiScanRequest(uint8_t *Buffer, size_t Bytes,
    const WDI_SCAN_PARAMETERS *Scan, uint8_t Sequence, uint8_t BandIndex, uint8_t BssIndex)
{
    if (Scan == NULL || Scan->ScanModeParameters.ScanRepeatCount != 1 ||
        Scan->Optional.SixGHzBandChannelList_IsPresent) {
        return false;
    }

    const WDI_SCAN_TYPE type = Scan->ScanModeParameters.AllowedScanType;
    if (type != WDI_SCAN_TYPE_ACTIVE_ONLY && type != WDI_SCAN_TYPE_PASSIVE_ONLY &&
        type != WDI_SCAN_TYPE_AUTO) {
        return false;
    }

    MT7922_SCAN_SSID ssids[MT7922_SCAN_MAX_SSIDS] = {0};
    MT7922_SCAN_CHANNEL channels[MT7922_SCAN_MAX_CHANNELS] = {0};
    MT7922_SCAN_PARAMETERS request = {0};
    request.Sequence = Sequence;
    request.BandIndex = BandIndex;
    request.BssIndex = BssIndex;
    request.Active = type == WDI_SCAN_TYPE_ACTIVE_ONLY;
    request.Ssids = ssids;
    request.Channels = channels;

    if (Scan->SSIDList.ElementCount > RTL_NUMBER_OF(ssids) ||
        (Scan->SSIDList.ElementCount != 0 && Scan->SSIDList.pElements == NULL)) {
        return false;
    }
    for (UINT32 i = 0; i < Scan->SSIDList.ElementCount; ++i) {
        const WDI_SSID *ssid = &Scan->SSIDList.pElements[i];
        if (ssid->ElementCount == 0) {
            continue;
        }
        if (ssid->ElementCount > MT7922_SCAN_MAX_SSID_BYTES || ssid->pElements == NULL ||
            type == WDI_SCAN_TYPE_PASSIVE_ONLY) {
            return false;
        }
        request.Active = true;
        MT7922_SCAN_SSID *output = &ssids[request.SsidCount++];
        output->Length = (uint8_t)ssid->ElementCount;
        memcpy(output->Data, ssid->pElements, ssid->ElementCount);
    }

    if (Scan->Optional.BandChannelList_IsPresent) {
        if (Scan->BandChannelList.ElementCount == 0 || Scan->BandChannelList.pElements == NULL) {
            return false;
        }
        for (UINT32 i = 0; i < Scan->BandChannelList.ElementCount; ++i) {
            const WDI_SCAN_BAND_CHANNEL_LIST_CONTAINER *group = &Scan->BandChannelList.pElements[i];
            const uint8_t band = FirmwareBand(group->BandID);
            if (band == 0 || group->ChannelList.ElementCount == 0 ||
                group->ChannelList.ElementCount > RTL_NUMBER_OF(channels) - request.ChannelCount ||
                group->ChannelList.pElements == NULL) {
                return false;
            }
            for (UINT32 j = 0; j < group->ChannelList.ElementCount; ++j) {
                const WDI_CHANNEL_NUMBER number = group->ChannelList.pElements[j];
                if (number == 0 || number > UINT8_MAX) {
                    return false;
                }
                channels[request.ChannelCount++] = (MT7922_SCAN_CHANNEL){band, (uint8_t)number};
            }
        }
    }

    if (Scan->Optional.VendorIEs_IsPresent) {
        request.InformationElements = Scan->VendorIEs.pElements;
        request.InformationElementBytes = Scan->VendorIEs.ElementCount;
    }

    static const uint8_t unspecified[6] = {0};
    request.Bssid = memcmp(Scan->BSSID.Address, unspecified, sizeof(unspecified)) == 0
        ? NULL : Scan->BSSID.Address;
    const UINT32 dwell = request.Active ? Scan->DwellTime.ActiveChannelDwellTime :
        Scan->DwellTime.PassiveChannelDwellTime;
    if (dwell > UINT16_MAX || Scan->DwellTime.MaximumScanTime > UINT16_MAX) {
        return false;
    }
    request.DwellMilliseconds = (uint16_t)dwell;
    request.TimeoutMilliseconds = (uint16_t)Scan->DwellTime.MaximumScanTime;
    return Mt7922BuildScanRequest(Buffer, Bytes, &request);
}


int32_t Mt7922StartWdiScan(MT7922_WDI_SCAN *State, const WDI_MESSAGE_HEADER *Header,
    const WDI_SCAN_PARAMETERS *Scan, uint8_t BandIndex, uint8_t BssIndex)
{
    if (State->Active || State->Cancelling) {
        return STATUS_DEVICE_BUSY;
    }
    uint8_t request[MT7922_SCAN_REQUEST_BYTES];
    const uint8_t sequence = (uint8_t)(State->Sequence % 127 + 1);
    if (Header == NULL || State->Submit == NULL || State->Indicate == NULL || State->Tlv == NULL ||
        !Mt7922BuildWdiScanRequest(request, sizeof(request), Scan, sequence, BandIndex, BssIndex)) {
        return STATUS_INVALID_PARAMETER;
    }
    State->Sequence = sequence;
    const int32_t status = State->Submit(State->Transport, MT7922_SCAN_START_COMMAND,
        request, sizeof(request));
    if (NT_SUCCESS(status)) {
        State->TransactionId = Header->TransactionId;
        State->IhvSpecificId = Header->IhvSpecificId;
        State->PortId = Header->PortId;
        State->BandIndex = BandIndex;
        State->ReportedEntries = 0;
        State->Active = true;
    }
    return status;
}

int32_t Mt7922CancelWdiScan(MT7922_WDI_SCAN *State)
{
    if (!State->Active || State->Cancelling) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    uint8_t request[MT7922_SCAN_CANCEL_BYTES];
    if (!Mt7922BuildScanCancel(request, sizeof(request), State->Sequence)) {
        return STATUS_INVALID_PARAMETER;
    }
    const int32_t status = State->Submit(State->Transport, MT7922_SCAN_CANCEL_COMMAND,
        request, sizeof(request));
    if (NT_SUCCESS(status)) {
        State->Cancelling = true;
    }
    return status;
}

static void CompleteScan(MT7922_WDI_SCAN *State, int32_t Status)
{
    if (State->Active) {
        const WDI_MESSAGE_HEADER header = {
            .PortId = State->PortId,
            .Status = Status,
            .TransactionId = State->TransactionId,
            .IhvSpecificId = State->IhvSpecificId
        };
        State->Active = false;
#ifdef MT7922_STATION_PACKET_TRACE
        Mt7922TraceStationControl("ScanComplete", WDI_TASK_SCAN, State->TransactionId, State->PortId,
            Status, State->Cancelling ? MT7922_CONTROL_SCAN_CANCELLING : 0U, State->ReportedEntries);
#endif
        State->Indicate(State->Host, WDI_INDICATION_SCAN_COMPLETE,
            (const uint8_t *)&header, sizeof(header));
    }
}

void Mt7922ExpireWdiScan(MT7922_WDI_SCAN *State)
{
    if (!State->Active) {
        return;
    }
    const bool cancelled = State->Cancelling;
    if (!cancelled) {
        Mt7922CancelWdiScan(State);
        State->Cancelling = true;
    }
    CompleteScan(State, cancelled ? STATUS_CANCELLED : STATUS_IO_TIMEOUT);
}

bool Mt7922WdiScanEvent(MT7922_WDI_SCAN *State, const uint8_t *Record, size_t Bytes)
{
    if ((!State->Active && !State->Cancelling) ||
        !Mt792xMatchScanRecord(State->ChipId, Record, Bytes,
            State->Sequence, State->BandIndex)) {
        return false;
    }
    const bool cancelled = State->Cancelling;
    State->Cancelling = false;
    CompleteScan(State, cancelled ? STATUS_CANCELLED : STATUS_SUCCESS);
    return true;
}

bool Mt7922WdiScanFrame(MT7922_WDI_SCAN *State, const uint8_t *Record, size_t Bytes)
{
    const size_t headerBytes = 24;
    const size_t fixedBytes = 12;
    MT7922_RX_FRAME frame;
    if (!State->Active || State->Cancelling || Record == NULL ||
        Mt792xDecodeRx(State->ChipId, Record, Bytes, &frame) != Mt7922RxRawFrame ||
        frame.Length < headerBytes + fixedBytes || frame.Channel == 0 || !frame.RssiPresent ||
        frame.AmsduPart != 0) {
        return false;
    }
    const bool beacon = frame.Bytes[0] == 0x80;
    const uint8_t *bssid = frame.Bytes + 16;
    static const uint8_t zeroMac[6] = {0};
    if ((!beacon && frame.Bytes[0] != 0x50) || (frame.Bytes[1] & 0xc7U) != 0 ||
        (frame.Bytes[22] & 0x0fU) != 0 || (bssid[0] & 1U) != 0 ||
        memcmp(bssid, zeroMac, sizeof(zeroMac)) == 0) {
        return false;
    }

    /* Microsoft's sample uses the frame body starting with the timestamp. */
    const size_t bodyBytes = frame.Length - headerBytes;
    const uint8_t *body = frame.Bytes + headerBytes;
    bool hasSsid = false;
    for (size_t i = fixedBytes; i < bodyBytes;) {
        if (bodyBytes - i < 2 || body[i + 1] > bodyBytes - i - 2) {
            return false;
        }
        if (body[i] == 0) {
            if (hasSsid || body[i + 1] > MT7922_SCAN_MAX_SSID_BYTES) {
                return false;
            }
            hasSsid = true; /* An empty SSID is valid for a hidden network. */
        }
        i += 2U + body[i + 1];
    }
    if (!hasSsid) {
        return false;
    }

    WDI_BSS_ENTRY_CONTAINER entry = {0};
    memcpy(entry.BSSID.Address, bssid, sizeof(entry.BSSID.Address));
    entry.ChannelInfo.ChannelNumber = frame.Channel;
    entry.ChannelInfo.BandId = frame.Band == 3 ? WDI_BAND_ID_6000 :
        frame.Band == 2 ? WDI_BAND_ID_5000 : WDI_BAND_ID_2400;
    entry.SignalInfo.RSSI = frame.RssiDbm;
    /* Linear estimate clamped to the WDI link-quality range. */
    const int quality = (frame.RssiDbm + 100) * 2;
    entry.SignalInfo.LinkQuality = (UINT32)(quality < 0 ? 0 : quality > 100 ? 100 : quality);
    WDI_BYTE_BLOB *blob = beacon ? &entry.BeaconFrame : &entry.ProbeResponseFrame;
    entry.Optional.BeaconFrame_IsPresent = beacon;
    entry.Optional.ProbeResponseFrame_IsPresent = !beacon;
    blob->ElementCount = (UINT32)bodyBytes;
    blob->pElements = (UINT8 *)body;

    WDI_INDICATION_BSS_ENTRY_LIST_PARAMETERS list = {0};
    list.Optional.DeviceDescriptor_IsPresent = 1;
    list.DeviceDescriptor.ElementCount = 1;
    list.DeviceDescriptor.pElements = &entry;
    UINT8 *buffer;
    ULONG length;
    if (GenerateWdiIndicationBssEntryList(&list, sizeof(WDI_MESSAGE_HEADER), State->Tlv,
            &length, &buffer) != NDIS_STATUS_SUCCESS) {
        return false;
    }
    const WDI_MESSAGE_HEADER header = {
        .PortId = State->PortId,
        .Status = STATUS_SUCCESS,
        .IhvSpecificId = State->IhvSpecificId
    };
    memcpy(buffer, &header, sizeof(header));
    State->Indicate(State->Host, WDI_INDICATION_BSS_ENTRY_LIST, buffer, length);
    ++State->ReportedEntries;
    FreeGenerated(buffer);
    return true;
}

static bool ReadWdiApBody(const WDI_BYTE_BLOB *Body, MT7922_STATION_AP *Ap)
{
    if (Body->pElements == NULL || Body->ElementCount < 12 || Body->ElementCount > 2048) {
        return false;
    }
    const uint8_t *frame = Body->pElements;
    /* Require infrastructure ESS. The discovery frame's fixed fields begin at the TSF. */
    if ((frame[10] & 3U) != 1U) {
        return false;
    }
    Ap->BeaconInterval = (uint16_t)(frame[8] | ((uint16_t)frame[9] << 8));
    static const uint8_t wmmPrefix[] = {0x00, 0x50, 0xf2, 0x02};
    for (size_t offset = 12; offset < Body->ElementCount;) {
        const size_t remaining = Body->ElementCount - offset;
        if (remaining < 2 || frame[offset + 1] > remaining - 2) {
            return false;
        }
        const size_t bytes = frame[offset + 1];
        const uint8_t *value = frame + offset + 2;
        if (frame[offset] == 5 && bytes >= 4) {
            Ap->DtimPeriod = value[1];
        } else if (frame[offset] == 192 && bytes == 5 && Ap->Band == 2 &&
            Ap->Channel == MT7922_STATION_PRIMARY_80 && value[0] == 1 &&
            value[1] == MT7922_STATION_CENTER_80 && value[2] == 0) {
            /* VHT Operation: an 80 MHz channel, without a second segment. */
            Ap->Center80 = value[1];
        } else if (frame[offset] == 221 && bytes >= 7 &&
            memcmp(value, wmmPrefix, sizeof(wmmPrefix)) == 0 && value[5] == 1 &&
            ((value[4] == 0 && bytes == 7) || (value[4] == 1 && bytes >= 24))) {
            Ap->Qos = true;
        }
        offset += 2 + bytes;
    }
    return Ap->BeaconInterval != 0;
}

/* Fallback when the OS BSS entry carries no channel: read the DS Parameter Set. */
static uint16_t ReadBodyChannel(const WDI_BYTE_BLOB *Body)
{
    if (Body->pElements == NULL || Body->ElementCount < 14 || Body->ElementCount > 2048) {
        return 0;
    }
    const uint8_t *frame = Body->pElements;
    for (size_t offset = 12; offset < Body->ElementCount;) {
        const size_t remaining = Body->ElementCount - offset;
        if (remaining < 2 || frame[offset + 1] > remaining - 2) {
            return 0;
        }
        if (frame[offset] == 3 && frame[offset + 1] >= 1) {
            return frame[offset + 2];
        }
        offset += 2U + frame[offset + 1];
    }
    return 0;
}

bool Mt7922ReadWdiStationAp(const WDI_CONNECT_BSS_ENTRY_CONTAINER *Entry, MT7922_STATION_AP *Ap)
{
    if (Entry == NULL || Ap == NULL) {
        return false;
    }
    static const uint8_t zero[6] = {0};
    if ((Entry->BSSID.Address[0] & 1U) != 0 ||
        memcmp(Entry->BSSID.Address, zero, sizeof(zero)) == 0) {
        return false;
    }
    /* The beacon carries TIM. Probe responses can be used before a beacon arrives. */
    const WDI_BYTE_BLOB *body = Entry->Optional.BeaconFrame_IsPresent ? &Entry->BeaconFrame :
        Entry->Optional.ProbeResponseFrame_IsPresent ? &Entry->ProbeResponseFrame : NULL;
    if (body == NULL) {
        return false;
    }
    uint16_t channel = (uint16_t)Entry->ChannelInfo.ChannelNumber;
    if (channel == 0) {
        channel = ReadBodyChannel(body);
    }
    const uint8_t band = FirmwareBand(Entry->ChannelInfo.BandId) != 0
        ? FirmwareBand(Entry->ChannelInfo.BandId) : (uint8_t)(channel > 14 ? 2 : 1);
    if (band > 2 || channel == 0 ||
        (band == 1 && channel > 14) || (band == 2 && (channel <= 14 || channel > 177))) {
        return false;
    }
    MT7922_STATION_AP selected = {.Band = band, .Channel = (uint8_t)channel};
    memcpy(selected.Bssid, Entry->BSSID.Address, sizeof(selected.Bssid));
    if (!ReadWdiApBody(body, &selected)) {
        return false;
    }
    *Ap = selected;
    return true;
}

int32_t Mt7922IndicateWdiStationJoin(const WDI_MESSAGE_HEADER *Header, const TLV_CONTEXT *Tlv,
    const uint8_t Bssid[6], bool Wpa2PersonalCcmp, const MT7922_STATION_JOIN_RESULT *Result,
    int32_t JoinStatus, MT7922_SCAN_INDICATE Indicate, void *Context)
{
    if (Header == NULL || Tlv == NULL || Result == NULL || Bssid == NULL || Indicate == NULL ||
        Result->RequestBodyBytes > sizeof(Result->RequestBody) ||
        Result->Authentication.BodyBytes > sizeof(Result->Authentication.Body) ||
        Result->Association.BodyBytes > sizeof(Result->Association.Body) ||
        (NT_SUCCESS(JoinStatus) && (Result->Association.AssociationId == 0 ||
            Result->Association.BodyBytes < 6 || Result->Authentication.BodyBytes < 6 ||
            Result->RequestBodyBytes < 4))) {
        return STATUS_INVALID_PARAMETER;
    }

    WDI_ASSOCIATION_RESULT_CONTAINER entry = {0};
    memcpy(entry.BSSID.Address, Bssid, sizeof(entry.BSSID.Address));
    WDI_ASSOCIATION_RESULT_PARAMETERS *result = &entry.AssociationResultParameters;
    result->BandID = WDI_BAND_ID_5000;
    result->AssociationStatus = WDI_ASSOC_STATUS_FAILURE;
    if (NT_SUCCESS(JoinStatus)) {
        result->AssociationStatus = WDI_ASSOC_STATUS_SUCCESS;
        result->AuthAlgorithm = Wpa2PersonalCcmp ? WDI_AUTH_ALGO_RSNA_PSK : WDI_AUTH_ALGO_80211_OPEN;
        result->UnicastCipherAlgorithm = Wpa2PersonalCcmp ? WDI_CIPHER_ALGO_CCMP : WDI_CIPHER_ALGO_NONE;
        result->MulticastDataCipherAlgorithm = result->UnicastCipherAlgorithm;
    } else if (Result->Association.StatusCode != 0) {
        result->AssociationStatus = WDI_ASSOC_STATUS_ASSOC_FAILED_BY_PEER;
        result->StatusCode = Result->Association.StatusCode;
    } else if (Result->Authentication.StatusCode != 0) {
        result->AssociationStatus = WDI_ASSOC_STATUS_AUTH_FAILED_BY_PEER;
        result->StatusCode = Result->Authentication.StatusCode;
    } else if (JoinStatus == STATUS_IO_TIMEOUT) {
        result->AssociationStatus = Result->AssociationStarted
            ? WDI_ASSOC_STATUS_NO_ASSOC_RESPONSE : WDI_ASSOC_STATUS_NO_AUTH_RESPONSE;
    }
    result->PortAuthorized = FALSE; /* Windows owns port authorization. */
    result->IHVAssociationStatus = (UINT32)JoinStatus;

    entry.Optional.AssociationRequestFrame_IsPresent = Result->AssociationStarted &&
        Result->RequestBodyBytes != 0;
    entry.AssociationRequestFrame.ElementCount = Result->RequestBodyBytes;
    entry.AssociationRequestFrame.pElements = (UINT8 *)Result->RequestBody;
    entry.Optional.AuthenticationResponseFrame_IsPresent = Result->Authentication.BodyBytes != 0;
    entry.AuthenticationResponseFrame.ElementCount = Result->Authentication.BodyBytes;
    entry.AuthenticationResponseFrame.pElements = (UINT8 *)Result->Authentication.Body;
    entry.Optional.AssociationResponseFrame_IsPresent = Result->Association.BodyBytes != 0;
    entry.AssociationResponseFrame.ElementCount = Result->Association.BodyBytes;
    entry.AssociationResponseFrame.pElements = (UINT8 *)Result->Association.Body;
    WDI_PHY_TYPE phy = WDI_PHY_TYPE_OFDM;
    entry.ActivePhyTypeList.ElementCount = 1;
    entry.ActivePhyTypeList.pElements = &phy;

    WDI_INDICATION_ASSOCIATION_RESULT_LIST list = {0};
    list.AssociationResults.ElementCount = 1;
    list.AssociationResults.pElements = &entry;
    UINT8 *buffer;
    ULONG bytes;
    if (GenerateWdiIndicationAssociationResult(&list, sizeof(WDI_MESSAGE_HEADER), Tlv,
            &bytes, &buffer) != NDIS_STATUS_SUCCESS) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    WDI_MESSAGE_HEADER indication = {
        .PortId = Header->PortId,
        .IhvSpecificId = Header->IhvSpecificId,
        .Status = STATUS_SUCCESS
    };
    memcpy(buffer, &indication, sizeof(indication));
    Indicate(Context, WDI_INDICATION_ASSOCIATION_RESULT, buffer, bytes);
    FreeGenerated(buffer);

    indication.TransactionId = Header->TransactionId;
    indication.Status = JoinStatus;
    Indicate(Context, WDI_INDICATION_CONNECT_COMPLETE, (const uint8_t *)&indication, sizeof(indication));
    return STATUS_SUCCESS;
}

int32_t Mt7922IndicateWdiDisconnect(const TLV_CONTEXT *Tlv, uint16_t PortId,
    const uint8_t Bssid[6], const uint8_t *PeerFrame, bool Reset,
    MT7922_SCAN_INDICATE Indicate, void *Context)
{
    WDI_INDICATION_DISASSOCIATION_PARAMETERS parameters = {0};
    WDI_DISCONNECT_INDICATION_PARAMETERS_CONTAINER *info = &parameters.DisconnectIndicationParameters;
    memcpy(info->MacAddress.Address, Bssid, 6);
    info->DisassociationWABIReason = Reset ? WDI_ASSOC_STATUS_DISASSOCIATE_BY_DEVICE_RESET :
        WDI_ASSOC_STATUS_DISASSOCIATED_BY_HOST;
    if (PeerFrame != NULL) {
        const bool deauth = PeerFrame[0] == 0xc0;
        info->DisassociationWABIReason = deauth ? WDI_ASSOC_STATUS_PEER_DEAUTHENTICATED :
            WDI_ASSOC_STATUS_PEER_DISASSOCIATED;
        parameters.Optional.DeauthFrame_IsPresent = deauth;
        parameters.Optional.DisassociationFrame_IsPresent = !deauth;
        WDI_BYTE_BLOB *body = deauth ? &parameters.DeauthFrame : &parameters.DisassociationFrame;
        body->pElements = (uint8_t *)(PeerFrame + 24);
        body->ElementCount = MT7922_STATION_DISCONNECT_BYTES - 24;
    }

    UINT8 *buffer;
    ULONG bytes;
    const NDIS_STATUS status = GenerateWdiIndicationDisassociation(&parameters, sizeof(WDI_MESSAGE_HEADER),
        Tlv, &bytes, &buffer);
    if (status != NDIS_STATUS_SUCCESS) {
        return (int32_t)status;
    }
    const WDI_MESSAGE_HEADER header = {.PortId = PortId, .Status = STATUS_SUCCESS};
    memcpy(buffer, &header, sizeof(header));
    Indicate(Context, WDI_INDICATION_DISASSOCIATION, buffer, bytes);
    FreeGenerated(buffer);
    return STATUS_SUCCESS;
}

static bool IsSupportedCcmpKey(const WDI_SET_ADD_CIPHER_KEYS_CONTAINER *Key, const uint8_t Bssid[6])
{
    const WDI_CIPHER_KEY_TYPE_STRUCT *type = &Key->CipherKeyTypeInfo;
    const UINT32 index = Key->Optional.CipherKeyID_IsPresent ? Key->CipherKeyID.CipherKeyID : 0;
    if (type->CipherAlgorithm != WDI_CIPHER_ALGO_CCMP || type->Static ||
        !Key->Optional.CCMPKey_IsPresent || Key->CCMPKey.ElementCount != MT7922_CCMP_KEY_BYTES ||
        Key->CCMPKey.pElements == NULL || Key->Optional.LinkId_IsPresent ||
        Key->Optional.GCMPKey_IsPresent || Key->Optional.GCMP_256Key_IsPresent ||
        Key->Optional.BIP_GMAC_256Key_IsPresent || Key->Optional.TKIPInfo_IsPresent ||
        Key->Optional.BIPKey_IsPresent || Key->Optional.WEPKey_IsPresent ||
        Key->Optional.IHVKey_IsPresent) {
        return false;
    }
    if (type->KeyType == WDI_CIPHER_KEY_TYPE_PAIRWISE_KEY) {
        return type->Direction == WDI_CIPHER_KEY_DIRECTION_BOTH && index == 0 &&
            Key->Optional.PeerMacAddress_IsPresent &&
            memcmp(Key->PeerMacAddress.Address, Bssid, 6) == 0;
    }
    return type->KeyType == WDI_CIPHER_KEY_TYPE_GROUP_KEY &&
        (type->Direction == WDI_CIPHER_KEY_DIRECTION_INBOUND || type->Direction == WDI_CIPHER_KEY_DIRECTION_BOTH) &&
        Key->Optional.CipherKeyID_IsPresent && index <= 3 &&
        (!Key->Optional.PeerMacAddress_IsPresent || memcmp(Key->PeerMacAddress.Address, Bssid, 6) == 0);
}

int32_t Mt7922ApplyWdiCcmpKeys(const WDI_SET_ADD_CIPHER_KEYS_PARAMETERS *Parameters,
    const uint8_t Bssid[6], MT7922_INSTALL_CCMP Install, void *Context)
{
    if (Parameters == NULL || Bssid == NULL || Install == NULL ||
        Parameters->SetCipherKey.pElements == NULL || Parameters->SetCipherKey.ElementCount == 0 ||
        Parameters->SetCipherKey.ElementCount > 2) {
        return STATUS_INVALID_PARAMETER;
    }
    unsigned seen = 0;
    for (UINT32 i = 0; i < Parameters->SetCipherKey.ElementCount; ++i) {
        const WDI_SET_ADD_CIPHER_KEYS_CONTAINER *key = &Parameters->SetCipherKey.pElements[i];
        if (!IsSupportedCcmpKey(key, Bssid)) {
            return STATUS_NOT_SUPPORTED;
        }
        const unsigned slot = key->CipherKeyTypeInfo.KeyType == WDI_CIPHER_KEY_TYPE_PAIRWISE_KEY
            ? 0 : 1;
        if (seen & (1U << slot)) {
            return STATUS_INVALID_PARAMETER;
        }
        seen |= 1U << slot;
    }
    for (UINT32 i = 0; i < Parameters->SetCipherKey.ElementCount; ++i) {
        const WDI_SET_ADD_CIPHER_KEYS_CONTAINER *key = &Parameters->SetCipherKey.pElements[i];
        const bool group = key->CipherKeyTypeInfo.KeyType == WDI_CIPHER_KEY_TYPE_GROUP_KEY;
        uint64_t initialRxPn = 0;
        if (key->Optional.ReceiveSequenceCount_IsPresent) {
            /* WDI_TLV_CIPHER_KEY_RECEIVE_SEQUENCE_COUNT is a little-endian 48-bit PN. */
            const UINT8 *pn = key->ReceiveSequenceCount.ReceiveSequenceCount;
            for (unsigned octet = 0; octet < 6; ++octet) {
                initialRxPn |= (uint64_t)pn[octet] << (8U * octet);
            }
        }
        const int32_t status = Install(Context, group,
            group ? (uint8_t)key->CipherKeyID.CipherKeyID : 0, key->CCMPKey.pElements, initialRxPn);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }
    return STATUS_SUCCESS;
}

/* Optional settings describe features Windows may use; none of them is required
   for an open-authentication association that leaves WPA2 to the supplicant. */
static uint32_t ConnectFlags(const WDI_CONNECT_PARAMETERS_CONTAINER *Parameters)
{
    const WDI_CONNECTION_SETTINGS *settings = &Parameters->ConnectionSettings;
    uint32_t flags = 0;
    flags |= settings->RoamRequest ? MT7922_WDI_FLAG_ROAM : 0;
    flags |= settings->HiddenNetwork ? MT7922_WDI_FLAG_HIDDEN : 0;
    flags |= settings->MFPEnabled ? MT7922_WDI_FLAG_MFP : 0;
    flags |= settings->HostFIPSModeEnabled ? MT7922_WDI_FLAG_HOST_FIPS : 0;
    flags |= settings->IsFIPSConnection ? MT7922_WDI_FLAG_FIPS : 0;
    flags |= settings->BSSTransitionSupported ? MT7922_WDI_FLAG_BSS_TRANS : 0;
    flags |= settings->MSCSSupported ? MT7922_WDI_FLAG_MSCS : 0;
    flags |= settings->DSCPToUPMappingSupported ? MT7922_WDI_FLAG_DSCP_UP : 0;
    flags |= Parameters->Optional.OWEDHIE_IsPresent ? MT7922_WDI_FLAG_OWE : 0;
    flags |= Parameters->Optional.HESSIDInfo_IsPresent ? MT7922_WDI_FLAG_HESSID : 0;
    flags |= Parameters->Optional.RsnaAkmCipherSuites_IsPresent ? MT7922_WDI_FLAG_AKM_CIPHER : 0;
    flags |= Parameters->Optional.RsnaAkmSuites_IsPresent ? MT7922_WDI_FLAG_AKM : 0;
    return flags;
}

static bool AllowsWpa2Ccmp(const WDI_CONNECT_PARAMETERS_CONTAINER *Parameters)
{
    bool psk = false;
    bool unicast = false;
    bool multicast = false;
    if (Parameters->AuthenticationAlgorithms.pElements == NULL ||
        Parameters->UnicastCipherAlgorithms.pElements == NULL ||
        Parameters->MulticastCipherAlgorithms.pElements == NULL) {
        return false;
    }
    for (UINT32 i = 0; i < Parameters->AuthenticationAlgorithms.ElementCount; ++i) {
        psk |= Parameters->AuthenticationAlgorithms.pElements[i] == WDI_AUTH_ALGO_RSNA_PSK;
    }
    for (UINT32 i = 0; i < Parameters->UnicastCipherAlgorithms.ElementCount; ++i) {
        unicast |= Parameters->UnicastCipherAlgorithms.pElements[i] == WDI_CIPHER_ALGO_CCMP;
    }
    for (UINT32 i = 0; i < Parameters->MulticastCipherAlgorithms.ElementCount; ++i) {
        multicast |= Parameters->MulticastCipherAlgorithms.pElements[i] == WDI_CIPHER_ALGO_CCMP;
    }
    return psk && unicast && multicast;
}

static bool HasBssid(const WDI_ADDRESS_LIST_CONTAINER *List, const uint8_t Bssid[6])
{
    for (UINT32 i = 0; List->pElements != NULL && i < List->ElementCount; ++i) {
        if (memcmp(List->pElements[i].Address, Bssid, 6) == 0) {
            return true;
        }
    }
    return false;
}

static bool Allows5GhzOfdm(const WDI_CONNECT_PARAMETERS_CONTAINER *Parameters, const uint8_t Bssid[6])
{
    static const uint8_t wildcard[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    if ((Parameters->Optional.AllowedBSSIDs_IsPresent &&
            !HasBssid(&Parameters->AllowedBSSIDs, Bssid) &&
            !HasBssid(&Parameters->AllowedBSSIDs, wildcard)) ||
        (Parameters->Optional.DisallowedBSSIDs_IsPresent &&
            HasBssid(&Parameters->DisallowedBSSIDs, Bssid))) {
        return false;
    }
    if (Parameters->Optional.UnavailableBandList_IsPresent) {
        if (Parameters->UnavailableBandList.pElements == NULL) {
            return false;
        }
        for (UINT32 i = 0; i < Parameters->UnavailableBandList.ElementCount; ++i) {
            if (Parameters->UnavailableBandList.pElements[i] == WDI_BAND_ID_5000) {
                return false;
            }
        }
    }
    if (!Parameters->Optional.ActivePhyTypeList_IsPresent) {
        return true;
    }
    for (UINT32 i = 0; Parameters->ActivePhyTypeList.pElements != NULL &&
        i < Parameters->ActivePhyTypeList.ElementCount; ++i) {
        if (Parameters->ActivePhyTypeList.pElements[i] == WDI_PHY_TYPE_OFDM) {
            return true;
        }
    }
    return false;
}

static bool AllowsSsid(const WDI_CONNECT_PARAMETERS_CONTAINER *Parameters,
    const uint8_t *Ssid, size_t Bytes)
{
    for (UINT32 i = 0; Parameters->SSIDList.pElements != NULL &&
        i < Parameters->SSIDList.ElementCount; ++i) {
        const WDI_SSID *ssid = &Parameters->SSIDList.pElements[i];
        if (ssid->pElements != NULL && ssid->ElementCount == Bytes &&
            memcmp(ssid->pElements, Ssid, Bytes) == 0) {
            return true;
        }
    }
    return false;
}

static uint16_t ReadWdiLe16(const uint8_t *Bytes)
{
    return (uint16_t)(Bytes[0] | ((uint16_t)Bytes[1] << 8));
}

static bool SupportsPskCcmp(const uint8_t *Rsn, size_t Bytes)
{
    static const uint8_t ccmp[] = {0, 0x0f, 0xac, 4};
    static const uint8_t psk[] = {0, 0x0f, 0xac, 2};
    if (Bytes < 18 || ReadWdiLe16(Rsn) != 1 || memcmp(Rsn + 2, ccmp, sizeof(ccmp)) != 0) {
        return false;
    }
    size_t offset = 6;
    bool match[2] = {false, false};
    for (unsigned list = 0; list < 2; ++list) {
        if (Bytes - offset < 2) {
            return false;
        }
        const uint16_t count = ReadWdiLe16(Rsn + offset);
        offset += 2;
        if (count == 0 || count > (Bytes - offset) / 4) {
            return false;
        }
        for (uint16_t i = 0; i < count; ++i) {
            match[list] |= memcmp(Rsn + offset, list == 0 ? ccmp : psk, 4) == 0;
            offset += 4;
        }
    }
    if (!match[0] || !match[1]) {
        return false;
    }
    if (Bytes == offset) {
        return true;
    }
    /* MFPR cannot be negotiated by this initial connection profile. */
    if (Bytes - offset < 2 || (ReadWdiLe16(Rsn + offset) & 0x0040U) != 0) {
        return false;
    }
    offset += 2;
    if (Bytes == offset) {
        return true;
    }
    if (Bytes - offset < 2) {
        return false;
    }
    const uint16_t pmkids = ReadWdiLe16(Rsn + offset);
    offset += 2;
    if (pmkids > (Bytes - offset) / 16) {
        return false;
    }
    offset += (size_t)pmkids * 16U;
    return Bytes == offset || Bytes - offset == 4; /* Optional group management cipher. */
}

static bool CopyConnectVendorIes(const WDI_BYTE_BLOB *Ies, uint8_t *Output, size_t *Used)
{
    if (Ies->ElementCount > MT7922_STATION_ASSOC_IE_MAX - *Used ||
        (Ies->ElementCount != 0 && Ies->pElements == NULL)) {
        return false;
    }
    for (size_t offset = 0; offset < Ies->ElementCount;) {
        const uint8_t *ie = Ies->pElements + offset;
        const size_t remaining = Ies->ElementCount - offset;
        if (remaining < 2 || ie[0] != 221 || ie[1] > remaining - 2 || ie[1] < 4) {
            return false;
        }
        /* The driver selects RSN and legacy QoS explicitly. */
        if (ie[2] == 0 && ie[3] == 0x50 && ie[4] == 0xf2 && (ie[5] == 1 || ie[5] == 2)) {
            return false;
        }
        offset += 2U + ie[1];
    }
    if (Ies->ElementCount != 0) {
        memcpy(Output + *Used, Ies->pElements, Ies->ElementCount);
        *Used += Ies->ElementCount;
    }
    return true;
}

static bool MakeCandidateIes(const WDI_CONNECT_PARAMETERS_CONTAINER *Parameters,
    const WDI_CONNECT_BSS_ENTRY_CONTAINER *Entry, MT7922_WDI_CONNECT_PLAN *Plan,
    uint32_t *Fail)
{
    *Fail = 0;
    if (Entry->Optional.PMKID_IsPresent || Entry->Optional.FTInitialAssocParameters_IsPresent ||
        Entry->Optional.FTReAssocParameters_IsPresent) {
        *Fail = 1;
        return false;
    }
    const WDI_BYTE_BLOB *beacon = Entry->Optional.BeaconFrame_IsPresent ? &Entry->BeaconFrame :
        &Entry->ProbeResponseFrame;
    /* Mt7922ReadWdiStationAp already validated fixed fields and IE bounds.
       Capability bits: require ESS and Privacy. Bit 8 (Spectrum Management)
       is legitimately set by 5 GHz APs and must not reject the candidate. */
    const UINT8 *raw = beacon->pElements;
    if ((raw[10] & 0x11U) != 0x11U) {
        *Fail = 2;
        return false;
    }
    const uint8_t *ssid = NULL;
    size_t ssidBytes = 0;
    bool rsn = false;
    uint8_t rates[8] = {0};
    static const uint8_t ofdm[] = {12, 18, 24, 36, 48, 72, 96, 108};
    for (size_t offset = 12; offset < beacon->ElementCount;) {
        const uint8_t id = raw[offset];
        const uint8_t len = raw[offset + 1];
        const uint8_t *value = raw + offset + 2;
        if (id == 0) {
            if (ssid != NULL || len == 0 || len > 32 || !AllowsSsid(Parameters, value, len)) {
                *Fail = 3;
                return false;
            }
            ssid = value;
            ssidBytes = len;
        } else if (id == 48) {
            if (rsn || !SupportsPskCcmp(value, len)) {
                *Fail = 4;
                return false;
            }
            rsn = true;
        } else if (id == 1 || id == 50) {
            for (unsigned i = 0; i < len; ++i) {
                bool found = false;
                for (size_t rate = 0; rate < sizeof(ofdm); ++rate) {
                    if ((value[i] & 0x7fU) == ofdm[rate]) {
                        rates[rate] |= value[i];
                        found = true;
                        break;
                    }
                }
                if (!found && (value[i] & 0x80U) != 0) {
                    *Fail = 5;
                    return false;
                }
            }
        }
        offset += 2U + len;
    }
    if (!rsn || ssid == NULL || rates[0] == 0 || rates[2] == 0 || rates[4] == 0) {
        *Fail = 6;
        return false; /* 6, 12, and 24 Mbps are the required OFDM rates. */
    }

    uint8_t *ie = Plan->Elements;
    ie[0] = 0;
    ie[1] = (uint8_t)ssidBytes;
    memcpy(ie + 2, ssid, ssidBytes);
    size_t used = 2 + ssidBytes;
    ie[used++] = 1;
    const size_t rateCount = used++;
    for (size_t i = 0; i < sizeof(rates); ++i) {
        if (rates[i] != 0) {
            ie[used++] = rates[i];
        }
    }
    ie[rateCount] = (uint8_t)(used - rateCount - 1);

    /* RSN selection: one CCMP pairwise suite and one PSK AKM. Windows owns EAPOL. */
    static const uint8_t rsnElement[] = {
        48, 20, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 4,
        1, 0, 0, 0x0f, 0xac, 2, 0, 0
    };
    memcpy(ie + used, rsnElement, sizeof(rsnElement));
    used += sizeof(rsnElement);
    if (Parameters->Optional.AssociationRequestVendorIE_IsPresent &&
        !CopyConnectVendorIes(&Parameters->AssociationRequestVendorIE, ie, &used)) {
        *Fail = 7;
        return false;
    }
    if (Entry->Optional.AssociationRequestVendorIE_IsPresent &&
        !CopyConnectVendorIes(&Entry->AssociationRequestVendorIE, ie, &used)) {
        *Fail = 8;
        return false;
    }
    /* Keep station association on the established non-WMM OFDM profile. */
    Plan->Ap.Qos = false;
    Plan->Join.InformationElements = ie;
    Plan->Join.InformationElementBytes = used;
    Plan->Join.Capability = 0x0011;
    Plan->Join.ListenInterval = 3;
    return true;
}

int32_t Mt7922MakeWdiConnectPlan(const WDI_TASK_CONNECT_PARAMETERS *Parameters,
    uint16_t Wcid, const MT7922_STATION_RADIO_PROFILE *Radio, MT7922_WDI_CONNECT_PLAN *Plan)
{
    if (Parameters == NULL || Plan == NULL || Radio == NULL || Wcid == 0 || Wcid > 255 ||
        Parameters->PreferredBSSEntryList.pElements == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    const WDI_CONNECT_PARAMETERS_CONTAINER *connect = &Parameters->ConnectParameters;
    Plan->Flags = ConnectFlags(connect);
    Plan->AuthAlgorithm = connect->AuthenticationAlgorithms.pElements != NULL &&
            connect->AuthenticationAlgorithms.ElementCount != 0
        ? (uint32_t)connect->AuthenticationAlgorithms.pElements[0] : 0;
    Plan->UnicastCipher = connect->UnicastCipherAlgorithms.pElements != NULL &&
            connect->UnicastCipherAlgorithms.ElementCount != 0
        ? (uint32_t)connect->UnicastCipherAlgorithms.pElements[0] : 0;
    if (!AllowsWpa2Ccmp(connect)) {
        return STATUS_NOT_SUPPORTED;
    }
    Plan->CandidateCount = Parameters->PreferredBSSEntryList.ElementCount;
    for (UINT32 i = 0; i < Parameters->PreferredBSSEntryList.ElementCount; ++i) {
        const WDI_CONNECT_BSS_ENTRY_CONTAINER *entry = &Parameters->PreferredBSSEntryList.pElements[i];
        if (i == 0) {
            Plan->FirstBandId = entry->ChannelInfo.BandId;
            Plan->FirstChannel = entry->ChannelInfo.ChannelNumber;
            Plan->FirstBody = entry->Optional.BeaconFrame_IsPresent ? 1 :
                entry->Optional.ProbeResponseFrame_IsPresent ? 2 : 0;
            const WDI_BYTE_BLOB *body = entry->Optional.BeaconFrame_IsPresent ? &entry->BeaconFrame :
                entry->Optional.ProbeResponseFrame_IsPresent ? &entry->ProbeResponseFrame : NULL;
            if (body != NULL) {
                Plan->BodyBytes = body->ElementCount;
                const size_t sample = body->ElementCount < sizeof(Plan->BodySample)
                    ? body->ElementCount : sizeof(Plan->BodySample);
                if (body->pElements != NULL && sample != 0) {
                    memcpy(Plan->BodySample, body->pElements, sample);
                }
            }
            if (connect->Optional.AssociationRequestVendorIE_IsPresent) {
                const WDI_BYTE_BLOB *vendor = &connect->AssociationRequestVendorIE;
                Plan->ParamsVendorBytes = vendor->ElementCount;
                const size_t sample = vendor->ElementCount < sizeof(Plan->ParamsVendorSample)
                    ? vendor->ElementCount : sizeof(Plan->ParamsVendorSample);
                if (vendor->pElements != NULL && sample != 0) {
                    memcpy(Plan->ParamsVendorSample, vendor->pElements, sample);
                }
            }
            if (entry->Optional.AssociationRequestVendorIE_IsPresent) {
                const WDI_BYTE_BLOB *vendor = &entry->AssociationRequestVendorIE;
                Plan->EntryVendorBytes = vendor->ElementCount;
                const size_t sample = vendor->ElementCount < sizeof(Plan->EntryVendorSample)
                    ? vendor->ElementCount : sizeof(Plan->EntryVendorSample);
                if (vendor->pElements != NULL && sample != 0) {
                    memcpy(Plan->EntryVendorSample, vendor->pElements, sample);
                }
            }
        }
        const bool apRead = Mt7922ReadWdiStationAp(entry, &Plan->Ap);
        const bool bandOk = apRead && Plan->Ap.Band == 2;
        const bool radioOk = bandOk && Mt7922StationRadioAllows(Radio, Plan->Ap.Channel);
        const bool phyOk = radioOk && Allows5GhzOfdm(connect, Plan->Ap.Bssid);
        uint32_t ieFail = 0;
        const bool iesOk = phyOk && MakeCandidateIes(connect, entry, Plan, &ieFail);
        if (iesOk) {
            Plan->Join.Wcid = Wcid;
            return STATUS_SUCCESS;
        }
        if (i == 0) {
            Plan->IeFail = ieFail;
            Plan->CandidateReason = !apRead ? 1 : !bandOk ? 2 : !radioOk ? 3 : !phyOk ? 4 : 5;
        }
    }
    return STATUS_NOT_FOUND;
}

_Static_assert(Mt7922PrivacyRequireKey == 0 && Mt7922PrivacyAlwaysClear == 1 &&
    Mt7922PrivacyClearUntilKey == 2, "internal privacy action values");
_Static_assert(WDI_EXEMPT_NO_EXEMPTION == 0 && WDI_EXEMPT_ALWAYS == 1 &&
    WDI_EXEMPT_ON_KEY_MAPPING_KEY_UNAVAILABLE == 2, "WDI privacy action values");

int32_t Mt7922ReadWdiEapolPrivacy(const WDI_SET_PRIVACY_EXEMPTION_LIST_PARAMETERS *Parameters,
    MT7922_PRIVACY *Privacy)
{
    if (Parameters == NULL || Privacy == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    MT7922_PRIVACY policy = Mt7922PrivacyRequireKey;
    if (Parameters->Optional.PrivacyExemptionEntry_IsPresent) {
        if (Parameters->PrivacyExemptionEntry.ElementCount != 0 &&
            Parameters->PrivacyExemptionEntry.pElements == NULL) {
            return STATUS_INVALID_PARAMETER;
        }
        bool seen = false;
        for (UINT32 i = 0; i < Parameters->PrivacyExemptionEntry.ElementCount; ++i) {
            const WDI_PRIVACY_EXEMPTION_PARAMETERS_STRUCT *entry =
                &Parameters->PrivacyExemptionEntry.pElements[i];
            /* WDI defines EtherType as bytes in network order. */
            static const uint8_t eapolType[] = {0x88, 0x8e};
            if (memcmp(&entry->EtherType, eapolType, sizeof(eapolType)) != 0 ||
                entry->ExemptionPacketType == WDI_EXEMPT_PACKET_TYPE_MULTICAST) {
                continue; /* This receive queue only delivers unicast EAPOL. */
            }
            if (seen || (entry->ExemptionPacketType != WDI_EXEMPT_PACKET_TYPE_UNICAST &&
                    entry->ExemptionPacketType != WDI_EXEMPT_PACKET_TYPE_BOTH) ||
                (unsigned)entry->ExemptionActionType > Mt7922PrivacyClearUntilKey) {
                return STATUS_INVALID_PARAMETER;
            }
            policy = (MT7922_PRIVACY)entry->ExemptionActionType;
            seen = true;
        }
    }
    *Privacy = policy;
    return STATUS_SUCCESS;
}
