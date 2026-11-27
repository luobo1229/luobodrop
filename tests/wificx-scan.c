#include <ntddk.h>
#include <ndis.h>
#include <dot11wificxintf.h>
#include <dot11wificxtypes.hpp>
#include <TlvGeneratorParser.hpp>
#include <assert.h>
#include <string.h>
#include "../src/wificx-scan.h"

typedef struct {
    uint8_t Packet[MT7922_SCAN_TX_BYTES];
    size_t PacketBytes;
    unsigned BssCount;
    unsigned CompletionCount;
    int32_t CompletionStatus;
    bool FailSubmit;
} SCAN_TEST;

static int32_t TestSubmit(void *Context, uint8_t Command, const uint8_t *Payload, size_t Bytes)
{
    SCAN_TEST *test = Context;
    test->PacketBytes = Mt7922FrameScanCommand(test->Packet, sizeof(test->Packet),
        Command, 9, Payload, Bytes);
    assert(test->PacketBytes != 0);
    return test->FailSubmit ? STATUS_IO_TIMEOUT : STATUS_SUCCESS;
}

static void TestIndicate(void *Context, uint16_t Message, const uint8_t *Buffer, size_t Bytes)
{
    SCAN_TEST *test = Context;
    WDI_MESSAGE_HEADER header;
    assert(Bytes >= sizeof(header));
    memcpy(&header, Buffer, sizeof(header));
    assert(header.PortId == 4 && header.IhvSpecificId == 13 && header.Reserved == 0);
    if (Message == WDI_INDICATION_SCAN_COMPLETE) {
        assert(Bytes == sizeof(header) && header.TransactionId == 78);
        test->CompletionStatus = (int32_t)header.Status;
        ++test->CompletionCount;
        return;
    }
    assert(Message == WDI_INDICATION_BSS_ENTRY_LIST);
    assert(header.TransactionId == 0 && header.Status == STATUS_SUCCESS);

    TLV_CONTEXT tlv = {0, WDI_VERSION_LATEST};
    WDI_INDICATION_BSS_ENTRY_LIST_PARAMETERS list = {0};
    assert(ParseWdiIndicationBssEntryListFromIhv((ULONG)(Bytes - sizeof(header)),
        Buffer + sizeof(header), &tlv, &list) == NDIS_STATUS_SUCCESS);
    assert(list.Optional.DeviceDescriptor_IsPresent && list.DeviceDescriptor.ElementCount == 1);
    const WDI_BSS_ENTRY_CONTAINER *entry = list.DeviceDescriptor.pElements;
    const uint8_t bssid[] = {2, 1, 2, 3, 4, 5};
    const uint8_t body[] = {0, 1, 0, 0, 0, 0, 0, 0, 100, 0, 1, 0, 0, 3, 'L', 'A', 'N'};
    assert(memcmp(entry->BSSID.Address, bssid, sizeof(bssid)) == 0);
    assert(entry->ChannelInfo.BandId == WDI_BAND_ID_5000);
    assert(entry->ChannelInfo.ChannelNumber == 149);
    assert(entry->SignalInfo.RSSI == -50 && entry->SignalInfo.LinkQuality == 100);
    const bool beacon = test->BssCount == 0;
    assert((entry->Optional.BeaconFrame_IsPresent != 0) == beacon);
    assert((entry->Optional.ProbeResponseFrame_IsPresent != 0) == !beacon);
    const WDI_BYTE_BLOB *blob = beacon ? &entry->BeaconFrame : &entry->ProbeResponseFrame;
    assert(blob->ElementCount == sizeof(body));
    assert(memcmp(blob->pElements, body, sizeof(body)) == 0);
    CleanupParsedWdiIndicationBssEntryListFromIhv(&list);
    ++test->BssCount;
}

static void VerifyScanRoundTrip(const WDI_SCAN_PARAMETERS *Scan, const uint8_t *Expected)
{
    const WDI_MESSAGE_HEADER header = {.PortId = 4, .TransactionId = 78, .IhvSpecificId = 13};
    const TLV_CONTEXT tlv = {0, WDI_VERSION_LATEST};
    SCAN_TEST test = {0};
    MT7922_WDI_SCAN state = {
        .Transport = &test, .Submit = TestSubmit, .Host = &test, .Indicate = TestIndicate,
        .Tlv = &tlv
    };
    test.FailSubmit = true;
    assert(Mt7922StartWdiScan(&state, &header, Scan, 1, 2) == STATUS_IO_TIMEOUT);
    assert(!state.Active && test.CompletionCount == 0);
    test.FailSubmit = false;
    state.Sequence = 0;
    assert(Mt7922StartWdiScan(&state, &header, Scan, 1, 2) == STATUS_SUCCESS);
    assert(Mt7922StartWdiScan(&state, &header, Scan, 1, 2) == STATUS_DEVICE_BUSY);

    const uint8_t txHeader[MT7922_SCAN_TX_HEADER_BYTES] = {
        0xe2, 0x04, 0x00, 0x41, 0x00, 0x00, 0x01, 0x80,
        [32] = 0xc2, 0x04, 0x00, 0x80, 0x03, 0xa0, 0x01, 0x09
    };
    assert(test.PacketBytes == MT7922_SCAN_TX_BYTES);
    assert(memcmp(test.Packet, txHeader, sizeof(txHeader)) == 0);
    assert(memcmp(test.Packet + sizeof(txHeader), Expected, MT7922_SCAN_REQUEST_BYTES) == 0);

    uint8_t rx[73] = {
        sizeof(rx), 0, 0, 0x10, [5] = 0x20, [13] = 149, [28] = 120, [29] = 110,
        [32] = 0x80,
        [48] = 2, 1, 2, 3, 4, 5,
        [56] = 0, 1, 0, 0, 0, 0, 0, 0, 100, 0, 1, 0, 0, 3, 'L', 'A', 'N'
    };
    assert(!Mt7922WdiScanFrame(&state, rx, sizeof(rx) - 1));
    rx[69] = 4; /* Truncated SSID element. */
    assert(!Mt7922WdiScanFrame(&state, rx, sizeof(rx)));
    rx[69] = 3;
    assert(Mt7922WdiScanFrame(&state, rx, sizeof(rx)));
    rx[32] = 0x50;
    rx[2] = 1;
    rx[3] = 7U << 3; /* Firmware-marked RX event carrying a normal 802.11 frame. */
    assert(Mt7922WdiScanFrame(&state, rx, sizeof(rx)));

    uint8_t event[56] = {sizeof(event), 0, 0, 7U << 3,
        [28] = MT7922_SCAN_DONE_EVENT, [36] = 1};
    assert(!Mt7922WdiScanEvent(&state, event, sizeof(event)));
    event[36] = 0x81;
    event[28] = 1; /* A different event cannot complete this scan. */
    assert(!Mt7922WdiScanEvent(&state, event, sizeof(event)));
    event[28] = MT7922_SCAN_DONE_EVENT;
    event[29] = 9; /* Linux dispatches scan done by event ID regardless of MCU sequence. */
    assert(!Mt7922WdiScanEvent(&state, event, sizeof(event) - 1));
    event[2] = 1;
    assert(!Mt7922WdiScanEvent(&state, event, sizeof(event)));
    event[2] = 0;
    assert(Mt7922WdiScanEvent(&state, event, sizeof(event)));
    assert(!Mt7922WdiScanEvent(&state, event, sizeof(event)));
    assert(!Mt7922WdiScanFrame(&state, rx, sizeof(rx)));
    assert(test.BssCount == 2 && test.CompletionCount == 1 && test.CompletionStatus == STATUS_SUCCESS);

    assert(Mt7922StartWdiScan(&state, &header, Scan, 1, 2) == STATUS_SUCCESS);
    assert(Mt7922CancelWdiScan(&state) == STATUS_SUCCESS);
    assert(test.Packet[36] == MT7922_SCAN_CANCEL_COMMAND && test.Packet[64] == 2);
    assert(!Mt7922WdiScanFrame(&state, rx, sizeof(rx)));
    assert(!Mt7922WdiScanEvent(&state, event, sizeof(event)));
    event[36] = 0x82;
    assert(Mt7922WdiScanEvent(&state, event, sizeof(event)));
    assert(test.CompletionCount == 2 && test.CompletionStatus == STATUS_CANCELLED);

    assert(Mt7922StartWdiScan(&state, &header, Scan, 1, 2) == STATUS_SUCCESS);
    Mt7922ExpireWdiScan(&state);
    assert(test.CompletionCount == 3 && test.CompletionStatus == STATUS_IO_TIMEOUT);
    assert(Mt7922StartWdiScan(&state, &header, Scan, 1, 2) == STATUS_DEVICE_BUSY);
    event[36] = 0x83;
    assert(Mt7922WdiScanEvent(&state, event, sizeof(event)));
    assert(!state.Active && !state.Cancelling && test.CompletionCount == 3);

    SCAN_TEST c3Test = {0};
    MT7922_WDI_SCAN c3State = {
        .Transport = &c3Test, .Submit = TestSubmit, .Host = &c3Test, .Indicate = TestIndicate,
        .Tlv = &tlv, .ChipId = 0x7925
    };
    assert(Mt7922StartWdiScan(&c3State, &header, Scan, 1, 2) == STATUS_SUCCESS);
    const uint8_t c3Done[52] = {
        sizeof(c3Done), 0, 0, 7U << 3, [36] = MT7925_SCAN_DONE_EVENT,
        [48] = 0, [50] = 4
    };
    assert(!Mt7922WdiScanEvent(&c3State, c3Done, sizeof(c3Done) - 1));
    assert(Mt7922WdiScanEvent(&c3State, c3Done, sizeof(c3Done)));
    assert(!c3State.Active && c3Test.CompletionCount == 1 &&
        c3Test.CompletionStatus == STATUS_SUCCESS);
}

static void VerifyStationCandidate(void)
{
    UINT8 beacon[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 1, 0,   /* Fixed fields. */
        0, 3, 'L', 'A', 'N',                     /* SSID. */
        5, 4, 0, 2, 0, 0,                       /* TIM, DTIM every second beacon. */
        221, 7, 0, 0x50, 0xf2, 0x02, 0, 1, 0    /* WMM information. */
    };
    const UINT8 bssid[] = {2, 0x21, 0x22, 0x23, 0x24, 0x25};
    WDI_CONNECT_BSS_ENTRY_CONTAINER entry = {0};
    entry.ChannelInfo.BandId = WDI_BAND_ID_5000;
    entry.ChannelInfo.ChannelNumber = 149;
    memcpy(entry.BSSID.Address, bssid, sizeof(bssid));
    entry.Optional.BeaconFrame_IsPresent = 1;
    entry.BeaconFrame.ElementCount = sizeof(beacon);
    entry.BeaconFrame.pElements = beacon;
    MT7922_STATION_AP ap = {0};
    assert(Mt7922ReadWdiStationAp(&entry, &ap));
    assert(ap.Band == 2 && ap.Channel == 149 && ap.BeaconInterval == 100);
    assert(memcmp(ap.Bssid, bssid, sizeof(bssid)) == 0 && ap.DtimPeriod == 2 && ap.Qos);

    uint8_t bss[MT7922_STATION_JOIN_BSS_BYTES];
    uint8_t rlm[MT7922_STATION_RLM_BYTES];
    assert(Mt7922BuildStationAp(&ap, 0, 19, 3, bss, rlm));
    assert(bss[16] == 1 && memcmp(bss + 18, bssid, sizeof(bssid)) == 0 && rlm[8] == 149);

    entry.BeaconFrame.ElementCount--;
    assert(!Mt7922ReadWdiStationAp(&entry, &ap));
    entry.Optional.BeaconFrame_IsPresent = 0;
    entry.Optional.ProbeResponseFrame_IsPresent = 1;
    entry.ProbeResponseFrame.ElementCount = 17;
    entry.ProbeResponseFrame.pElements = beacon;
    assert(Mt7922ReadWdiStationAp(&entry, &ap));
    assert(!ap.Qos && ap.DtimPeriod == 0);
    entry.ChannelInfo.BandId = WDI_BAND_ID_6000;
    assert(!Mt7922ReadWdiStationAp(&entry, &ap));
    entry.ChannelInfo.BandId = WDI_BAND_ID_5000;
    beacon[10] = 2; /* IBSS. */
    assert(!Mt7922ReadWdiStationAp(&entry, &ap));
}

typedef struct {
    unsigned Count;
    WDI_ASSOC_STATUS Result;
    NTSTATUS Completion;
} JOIN_TEST;

static void TestJoinIndicate(void *Context, uint16_t Message, const uint8_t *Buffer, size_t Bytes)
{
    JOIN_TEST *seen = Context;
    WDI_MESSAGE_HEADER header;
    assert(Bytes >= sizeof(header));
    memcpy(&header, Buffer, sizeof(header));
    assert(header.PortId == 4 && header.IhvSpecificId == 13);
    if (Message == WDI_INDICATION_CONNECT_COMPLETE) {
        assert(seen->Count == 1 && Bytes == sizeof(header));
        assert(header.TransactionId == 78 && header.Status == seen->Completion);
    } else {
        assert(Message == WDI_INDICATION_ASSOCIATION_RESULT && seen->Count == 0);
        assert(header.TransactionId == 0 && header.Status == STATUS_SUCCESS);
        const TLV_CONTEXT tlv = {0, WDI_VERSION_LATEST};
        WDI_INDICATION_ASSOCIATION_RESULT_LIST list = {0};
        assert(ParseWdiIndicationAssociationResultFromIhv((ULONG)(Bytes - sizeof(header)),
            Buffer + sizeof(header), &tlv, &list) == NDIS_STATUS_SUCCESS);
        assert(list.AssociationResults.ElementCount == 1);
        const WDI_ASSOCIATION_RESULT_CONTAINER *entry = list.AssociationResults.pElements;
        const WDI_ASSOCIATION_RESULT_PARAMETERS *result = &entry->AssociationResultParameters;
        assert(result->AssociationStatus == seen->Result && !result->PortAuthorized);
        assert(result->BandID == WDI_BAND_ID_5000 && entry->BSSID.Address[5] == 5);
        assert(entry->Optional.AuthenticationResponseFrame_IsPresent);
        assert(entry->AuthenticationResponseFrame.ElementCount == 6);
        assert(entry->AuthenticationResponseFrame.pElements[2] == 2);
        if (seen->Result == WDI_ASSOC_STATUS_SUCCESS) {
            assert(result->AuthAlgorithm == WDI_AUTH_ALGO_RSNA_PSK);
            assert(result->UnicastCipherAlgorithm == WDI_CIPHER_ALGO_CCMP);
            assert(entry->Optional.AssociationRequestFrame_IsPresent &&
                entry->Optional.AssociationResponseFrame_IsPresent);
            assert(entry->AssociationRequestFrame.pElements[2] == 3);
            assert(entry->AssociationResponseFrame.pElements[4] == 7);
        } else {
            assert(result->StatusCode == 13 && !entry->Optional.AssociationRequestFrame_IsPresent &&
                !entry->Optional.AssociationResponseFrame_IsPresent);
        }
        CleanupParsedWdiIndicationAssociationResultFromIhv(&list);
    }
    ++seen->Count;
}

static void VerifyWdiAssociationResult(void)
{
    const WDI_MESSAGE_HEADER header = {.PortId = 4, .TransactionId = 78, .IhvSpecificId = 13};
    const TLV_CONTEXT tlv = {0, WDI_VERSION_LATEST};
    const uint8_t bssid[] = {2, 1, 2, 3, 4, 5};
    MT7922_STATION_JOIN_RESULT join = {
        .Authentication = {.BodyBytes = 6, .Body = {0, 0, 2, 0, 0, 0}},
        .Association = {.BodyBytes = 6, .Body = {0x11, 0, 0, 0, 7, 0xc0}, .AssociationId = 7},
        .AssociationStarted = true,
        .RequestBodyBytes = 4, .RequestBody = {0x11, 0, 3, 0}
    };
    JOIN_TEST seen = {.Result = WDI_ASSOC_STATUS_SUCCESS, .Completion = STATUS_SUCCESS};
    assert(Mt7922IndicateWdiStationJoin(&header, &tlv, bssid, true, &join, STATUS_SUCCESS,
        TestJoinIndicate, &seen) == STATUS_SUCCESS);
    assert(seen.Count == 2);
    join.Authentication.StatusCode = 13;
    join.Authentication.Body[4] = 13;
    join.AssociationStarted = false;
    join.Association.BodyBytes = 0;
    seen = (JOIN_TEST){.Result = WDI_ASSOC_STATUS_AUTH_FAILED_BY_PEER, .Completion = STATUS_CONNECTION_REFUSED};
    assert(Mt7922IndicateWdiStationJoin(&header, &tlv, bssid, true, &join, STATUS_CONNECTION_REFUSED,
        TestJoinIndicate, &seen) == STATUS_SUCCESS);
    assert(seen.Count == 2);
}

static void VerifyDisconnectIndication(void *Context, uint16_t Message,
    const uint8_t *Buffer, size_t Bytes)
{
    const WDI_ASSOC_STATUS reason = *(WDI_ASSOC_STATUS *)Context;
    const TLV_CONTEXT tlv = {0, WDI_VERSION_LATEST};
    assert(Message == WDI_INDICATION_DISASSOCIATION && Bytes > sizeof(WDI_MESSAGE_HEADER));
    const WDI_MESSAGE_HEADER *header = (const WDI_MESSAGE_HEADER *)Buffer;
    assert(header->PortId == 4 && header->TransactionId == 0 && header->Status == STATUS_SUCCESS);
    WDI_INDICATION_DISASSOCIATION_PARAMETERS parsed = {0};
    assert(ParseWdiIndicationDisassociationFromIhv((ULONG)(Bytes - sizeof(*header)), Buffer + sizeof(*header),
        &tlv, &parsed) == NDIS_STATUS_SUCCESS);
    const uint8_t bssid[] = {2, 1, 2, 3, 4, 5};
    assert(memcmp(parsed.DisconnectIndicationParameters.MacAddress.Address, bssid, sizeof(bssid)) == 0);
    assert(parsed.DisconnectIndicationParameters.DisassociationWABIReason == reason);
    assert(!parsed.Optional.DeauthFrame_IsPresent);
    assert((parsed.Optional.DisassociationFrame_IsPresent != 0) == (reason == WDI_ASSOC_STATUS_PEER_DISASSOCIATED));
    if (parsed.Optional.DisassociationFrame_IsPresent) {
        assert(parsed.DisassociationFrame.ElementCount == 2 && parsed.DisassociationFrame.pElements[0] == 4);
    }
    CleanupParsedWdiIndicationDisassociationFromIhv(&parsed);
}

static void VerifyWdiDisconnect(void)
{
    const TLV_CONTEXT tlv = {0, WDI_VERSION_LATEST};
    const uint8_t bssid[] = {2, 1, 2, 3, 4, 5};
    const uint8_t peerFrame[MT7922_STATION_DISCONNECT_BYTES] = {0xa0, [24] = 4};
    WDI_ASSOC_STATUS expected = WDI_ASSOC_STATUS_DISASSOCIATED_BY_HOST;
    assert(Mt7922IndicateWdiDisconnect(&tlv, 4, bssid, NULL, false,
        VerifyDisconnectIndication, &expected) == STATUS_SUCCESS);
    expected = WDI_ASSOC_STATUS_PEER_DISASSOCIATED;
    assert(Mt7922IndicateWdiDisconnect(&tlv, 4, bssid, peerFrame, false,
        VerifyDisconnectIndication, &expected) == STATUS_SUCCESS);
}

static int32_t TestApplyKey(void *Context, bool Group, uint8_t Index,
    const uint8_t Key[MT7922_CCMP_KEY_BYTES], uint64_t InitialRxPn)
{
    unsigned *calls = Context;
    assert(Group == (*calls != 0));
    assert(Index == (Group ? 2 : 0));
    assert(Key[0] == (Group ? 0x62 : 0x31));
    assert(InitialRxPn == (Group ? UINT64_C(0x665544332211) : 0));
    ++*calls;
    return STATUS_SUCCESS;
}

static void VerifyWdiKeyRequest(void)
{
    const TLV_CONTEXT tlv = {0, WDI_VERSION_LATEST};
    const uint8_t bssid[] = {2, 1, 2, 3, 4, 5};
    uint8_t keys[2][MT7922_CCMP_KEY_BYTES];
    memset(keys[0], 0x31, sizeof(keys[0]));
    memset(keys[1], 0x62, sizeof(keys[1]));
    WDI_SET_ADD_CIPHER_KEYS_CONTAINER entries[2] = {0};
    for (unsigned i = 0; i < 2; ++i) {
        WDI_SET_ADD_CIPHER_KEYS_CONTAINER *key = &entries[i];
        key->Optional.CCMPKey_IsPresent = 1;
        key->CCMPKey.ElementCount = MT7922_CCMP_KEY_BYTES;
        key->CCMPKey.pElements = keys[i];
        key->CipherKeyTypeInfo.CipherAlgorithm = WDI_CIPHER_ALGO_CCMP;
        key->CipherKeyTypeInfo.Direction = i == 0 ? WDI_CIPHER_KEY_DIRECTION_BOTH :
            WDI_CIPHER_KEY_DIRECTION_INBOUND;
        key->CipherKeyTypeInfo.KeyType = i == 0 ? WDI_CIPHER_KEY_TYPE_PAIRWISE_KEY :
            WDI_CIPHER_KEY_TYPE_GROUP_KEY;
    }
    entries[0].Optional.PeerMacAddress_IsPresent = 1;
    memcpy(entries[0].PeerMacAddress.Address, bssid, sizeof(bssid));
    entries[1].Optional.CipherKeyID_IsPresent = 1;
    entries[1].CipherKeyID.CipherKeyID = 2;
    entries[1].Optional.ReceiveSequenceCount_IsPresent = 1;
    const uint8_t groupPn[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    memcpy(entries[1].ReceiveSequenceCount.ReceiveSequenceCount, groupPn, sizeof(groupPn));

    WDI_SET_ADD_CIPHER_KEYS_PARAMETERS keysIn = {0};
    keysIn.SetCipherKey.ElementCount = 2;
    keysIn.SetCipherKey.pElements = entries;
    WDI_SET_ADD_CIPHER_KEYS_PARAMETERS parsed = {0};
    UINT8 *request;
    ULONG bytes;
    assert(GenerateWdiSetAddCipherKeysToIhv(&keysIn, 0, &tlv, &bytes, &request) == NDIS_STATUS_SUCCESS);
    assert(ParseWdiSetAddCipherKeys(bytes, request, &tlv, &parsed) == NDIS_STATUS_SUCCESS);
    unsigned calls = 0;
    assert(Mt7922ApplyWdiCcmpKeys(&parsed, bssid, TestApplyKey, &calls) == STATUS_SUCCESS);
    assert(calls == 2);
    CleanupParsedWdiSetAddCipherKeys(&parsed);
    FreeGenerated(request);

    calls = 0;
    entries[0].PeerMacAddress.Address[5] ^= 1;
    assert(Mt7922ApplyWdiCcmpKeys(&keysIn, bssid, TestApplyKey, &calls) == STATUS_NOT_SUPPORTED);
    assert(calls == 0);
}

static void VerifyWdiConnectRequest(void)
{
    const TLV_CONTEXT tlv = {0, WDI_VERSION_LATEST};
    UINT8 body[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0x11, 0,
        0, 3, 'L', 'A', 'N', 1, 3, 0x8c, 0x98, 0xb0,
        48, 20, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 4,
        1, 0, 0, 0x0f, 0xac, 2, 0, 0
    };
    UINT8 ssidBytes[] = {'L', 'A', 'N'};
    UINT8 vendor[] = {221, 4, 0x11, 0x22, 0x33, 0x44};
    WDI_SSID ssid = {0};
    ssid.ElementCount = ARRAYSIZE(ssidBytes);
    ssid.pElements = ssidBytes;
    WDI_AUTH_ALGORITHM auth[] = {WDI_AUTH_ALGO_RSNA_PSK};
    WDI_CIPHER_ALGORITHM cipher[] = {WDI_CIPHER_ALGO_CCMP};
    WDI_CONNECT_BSS_ENTRY_CONTAINER candidates[2] = {0};
    const UINT8 bssid[] = {2, 1, 2, 3, 4, 5};
    for (unsigned i = 0; i < ARRAYSIZE(candidates); ++i) {
        WDI_CONNECT_BSS_ENTRY_CONTAINER *entry = &candidates[i];
        memcpy(entry->BSSID.Address, bssid, sizeof(bssid));
        entry->ChannelInfo.ChannelNumber = 149;
        entry->ChannelInfo.BandId = WDI_BAND_ID_5000;
        entry->Optional.BeaconFrame_IsPresent = 1;
        entry->BeaconFrame.ElementCount = ARRAYSIZE(body);
        entry->BeaconFrame.pElements = body;
    }
    candidates[0].ChannelInfo.ChannelNumber = 44; /* Preferred, but outside the profile. */
    WDI_MAC_ADDRESS allowed[] = {{.Address = {2, 1, 2, 3, 4, 5}}};

    WDI_TASK_CONNECT_PARAMETERS connect = {0};
    connect.PreferredBSSEntryList.ElementCount = ARRAYSIZE(candidates);
    connect.PreferredBSSEntryList.pElements = candidates;
    WDI_CONNECT_PARAMETERS_CONTAINER *policy = &connect.ConnectParameters;
    policy->SSIDList.ElementCount = 1;
    policy->SSIDList.pElements = &ssid;
    policy->AuthenticationAlgorithms.ElementCount = ARRAYSIZE(auth);
    policy->AuthenticationAlgorithms.pElements = auth;
    policy->UnicastCipherAlgorithms.ElementCount = ARRAYSIZE(cipher);
    policy->UnicastCipherAlgorithms.pElements = cipher;
    policy->MulticastCipherAlgorithms.ElementCount = ARRAYSIZE(cipher);
    policy->MulticastCipherAlgorithms.pElements = cipher;
    policy->Optional.AllowedBSSIDs_IsPresent = 1;
    policy->AllowedBSSIDs.ElementCount = ARRAYSIZE(allowed);
    policy->AllowedBSSIDs.pElements = allowed;
    policy->Optional.AssociationRequestVendorIE_IsPresent = 1;
    policy->AssociationRequestVendorIE.ElementCount = ARRAYSIZE(vendor);
    policy->AssociationRequestVendorIE.pElements = vendor;

    UINT8 *request;
    ULONG bytes;
    WDI_TASK_CONNECT_PARAMETERS parsed = {0};
    assert(GenerateWdiTaskConnectToIhv(&connect, 0, &tlv, &bytes, &request) == NDIS_STATUS_SUCCESS);
    assert(ParseWdiTaskConnect(bytes, request, &tlv, &parsed) == NDIS_STATUS_SUCCESS);
    const MT7922_STATION_RADIO_PROFILE radio = { .ChannelCount = 1, .Channels = {{149, 20}} };
    MT7922_WDI_CONNECT_PLAN plan;
    assert(Mt7922MakeWdiConnectPlan(&parsed, 1, &radio, &plan) == STATUS_SUCCESS);
    CleanupParsedWdiTaskConnect(&parsed);
    FreeGenerated(request);
    assert(memcmp(plan.Ap.Bssid, bssid, sizeof(bssid)) == 0 && plan.Ap.Channel == 149);
    assert(plan.Join.Wcid == 1 && plan.Join.Capability == 0x11 && !plan.Ap.Qos);
    assert(plan.Join.InformationElements == plan.Elements &&
        plan.Join.InformationElementBytes == sizeof(body) - 12 + sizeof(vendor));
    assert(memcmp(plan.Elements, body + 12, sizeof(body) - 12) == 0);
    assert(memcmp(plan.Elements + sizeof(body) - 12, vendor, sizeof(vendor)) == 0);

    candidates[1].ChannelInfo.ChannelNumber = 44;
    assert(Mt7922MakeWdiConnectPlan(&connect, 1, &radio, &plan) == STATUS_NOT_FOUND);
    candidates[1].ChannelInfo.ChannelNumber = 149;
    body[42] = 0x40; /* PMF required by the AP. */
    assert(Mt7922MakeWdiConnectPlan(&connect, 1, &radio, &plan) == STATUS_NOT_FOUND);
    body[42] = 0;
    body[41] = 8; /* SAE-only candidate. */
    assert(Mt7922MakeWdiConnectPlan(&connect, 1, &radio, &plan) == STATUS_NOT_FOUND);
    body[41] = 2;
    ssidBytes[0] = 'A';
    assert(Mt7922MakeWdiConnectPlan(&connect, 1, &radio, &plan) == STATUS_NOT_FOUND);
    ssidBytes[0] = 'L';
    /* Optional settings do not block the open-authentication association. */
    policy->ConnectionSettings.MFPEnabled = TRUE;
    assert(Mt7922MakeWdiConnectPlan(&connect, 1, &radio, &plan) == STATUS_SUCCESS && plan.Flags != 0);
    policy->ConnectionSettings.MFPEnabled = FALSE;
}

static void VerifyWdiEapolPrivacy(void)
{
    const TLV_CONTEXT tlv = {0, WDI_VERSION_LATEST};
    WDI_PRIVACY_EXEMPTION_PARAMETERS_STRUCT entries[2] = {0};
    const uint8_t networkType[] = {0x88, 0x8e};
    memcpy(&entries[0].EtherType, networkType, sizeof(networkType));
    entries[0].ExemptionActionType = WDI_EXEMPT_ON_KEY_MAPPING_KEY_UNAVAILABLE;
    entries[0].ExemptionPacketType = WDI_EXEMPT_PACKET_TYPE_UNICAST;
    WDI_SET_PRIVACY_EXEMPTION_LIST_PARAMETERS input = {0};
    input.Optional.PrivacyExemptionEntry_IsPresent = 1;
    input.PrivacyExemptionEntry.ElementCount = 1;
    input.PrivacyExemptionEntry.pElements = entries;
    UINT8 *request;
    ULONG bytes;
    WDI_SET_PRIVACY_EXEMPTION_LIST_PARAMETERS parsed = {0};
    assert(GenerateWdiSetPrivacyExemptionListToIhv(&input, 0, &tlv, &bytes, &request) == NDIS_STATUS_SUCCESS);
    assert(ParseWdiSetPrivacyExemptionList(bytes, request, &tlv, &parsed) == NDIS_STATUS_SUCCESS);
    MT7922_PRIVACY privacy;
    assert(Mt7922ReadWdiEapolPrivacy(&parsed, &privacy) == STATUS_SUCCESS);
    assert(privacy == Mt7922PrivacyClearUntilKey);
    CleanupParsedWdiSetPrivacyExemptionList(&parsed);
    FreeGenerated(request);

    entries[1] = entries[0];
    input.PrivacyExemptionEntry.ElementCount = 2;
    assert(Mt7922ReadWdiEapolPrivacy(&input, &privacy) == STATUS_INVALID_PARAMETER);
    input.Optional.PrivacyExemptionEntry_IsPresent = 0;
    assert(Mt7922ReadWdiEapolPrivacy(&input, &privacy) == STATUS_SUCCESS);
    assert(privacy == Mt7922PrivacyRequireKey);
}

void VerifyWificxScan(void)
{
    VerifyStationCandidate();
    VerifyWdiConnectRequest();
    VerifyWdiAssociationResult();
    VerifyWdiDisconnect();
    VerifyWdiKeyRequest();
    VerifyWdiEapolPrivacy();
    UINT8 name[] = {'L', 'A', 'N'};
    UINT8 vendorIe[] = {0xdd, 3, 0x00, 0x17, 0xf2};
    WDI_SSID ssid = {0};
    ssid.ElementCount = ARRAYSIZE(name);
    ssid.pElements = name;

    WDI_CHANNEL_NUMBER primary[] = {44, 149};
    WDI_SCAN_BAND_CHANNEL_LIST_CONTAINER band = {0};
    band.BandID = WDI_BAND_ID_5000;
    band.ChannelList.ElementCount = ARRAYSIZE(primary);
    band.ChannelList.pElements = primary;

    WDI_SCAN_PARAMETERS scan = {0};
    scan.ScanModeParameters.ScanRepeatCount = 1;
    scan.ScanModeParameters.AllowedScanType = WDI_SCAN_TYPE_AUTO;
    scan.DwellTime.ActiveChannelDwellTime = 40;
    scan.DwellTime.PassiveChannelDwellTime = 130;
    scan.DwellTime.MaximumScanTime = 1500;
    scan.SSIDList.ElementCount = 1;
    scan.SSIDList.pElements = &ssid;
    scan.Optional.BandChannelList_IsPresent = 1;
    scan.BandChannelList.ElementCount = 1;
    scan.BandChannelList.pElements = &band;
    scan.Optional.VendorIEs_IsPresent = 1;
    scan.VendorIEs.ElementCount = ARRAYSIZE(vendorIe);
    scan.VendorIEs.pElements = vendorIe;
    const UINT8 bssid[] = {0x02, 1, 2, 3, 4, 5};
    memcpy(scan.BSSID.Address, bssid, sizeof(bssid));

    uint8_t actual[MT7922_SCAN_REQUEST_BYTES];
    uint8_t expected[MT7922_SCAN_REQUEST_BYTES] = {
        0x81, 2, 1, 4, 1, 2, 0x20, 1, 3, 0, 0, 0, 'L', 'A', 'N'
    };
    expected[154] = 40;
    expected[156] = 0xdc;
    expected[157] = 0x05;
    expected[158] = 4;
    expected[159] = 2;
    expected[160] = 2;
    expected[161] = 44;
    expected[162] = 2;
    expected[163] = 149;
    expected[224] = sizeof(vendorIe);
    expected[828] = 40;
    memcpy(expected + 226, vendorIe, sizeof(vendorIe));
    memcpy(expected + 1110, bssid, sizeof(bssid));
    expected[1185] = 1;

    assert(Mt7922BuildWdiScanRequest(actual, sizeof(actual), &scan, 1, 1, 2));
    assert(memcmp(actual, expected, sizeof(expected)) == 0);

    /* Round-trip the request through the same WDK parser as the kernel handler. */
    const TLV_CONTEXT tlv = {0, WDI_VERSION_LATEST};
    UINT8 *request;
    ULONG requestBytes;
    WDI_SCAN_PARAMETERS parsed = {0};
    assert(GenerateWdiTaskScanToIhv(&scan, 0, &tlv, &requestBytes, &request) == NDIS_STATUS_SUCCESS);
    assert(ParseWdiTaskScan(requestBytes, request, &tlv, &parsed) == NDIS_STATUS_SUCCESS);
    VerifyScanRoundTrip(&parsed, expected);
    CleanupParsedWdiTaskScan(&parsed);
    FreeGenerated(request);
    scan.ScanModeParameters.ScanRepeatCount = 0;
    assert(!Mt7922BuildWdiScanRequest(actual, sizeof(actual), &scan, 1, 1, 2));
    scan.ScanModeParameters.ScanRepeatCount = 1;
    band.BandID = WDI_BAND_ID_60000;
    assert(!Mt7922BuildWdiScanRequest(actual, sizeof(actual), &scan, 1, 1, 2));
    band.BandID = WDI_BAND_ID_5000;
    scan.ScanModeParameters.AllowedScanType = WDI_SCAN_TYPE_PASSIVE_ONLY;
    assert(!Mt7922BuildWdiScanRequest(actual, sizeof(actual), &scan, 1, 1, 2));
    scan.SSIDList.ElementCount = 0;
    assert(Mt7922BuildWdiScanRequest(actual, sizeof(actual), &scan, 1, 1, 2));
    assert(actual[2] == 0 && actual[5] == 0 && actual[154] == 130);
}

