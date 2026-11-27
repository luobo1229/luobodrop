#include <ntddk.h>
#include <wdf.h>
#include <ndis.h>
#include <netadaptercx.h>
#include <wificx.h>
#include <dot11wificxintf.h>
#include <dot11wificxtypes.hpp>
#include <TlvGeneratorParser.hpp>
#include "probe.h"
#include "mcu.h"
#include "wificx-scan-kernel.h"
#ifdef MT7922_STATION_PACKET_TRACE
#include "station-packet-trace.h"
#endif

NTSTATUS Mt7922PublishWificxIndication(WDFDEVICE Device, uint16_t Message,
    const uint8_t *Buffer, size_t Bytes)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Device;
    WDFMEMORY data;
    void *destination;
    NTSTATUS status = WdfMemoryCreate(&attributes, NonPagedPoolNx, 'SctM',
        Bytes, &data, &destination);
    if (NT_SUCCESS(status)) {
        RtlCopyMemory(destination, Buffer, Bytes);
        WifiDeviceReceiveIndication(Device, Message, data);
        WdfObjectDelete(data);
    }
    return status;
}

static void PublishScanIndication(void *Context, uint16_t Message,
    const uint8_t *Buffer, size_t Bytes)
{
    Mt7922PublishWificxIndication(Context, Message, Buffer, Bytes);
}

static bool DeliverScanEvent(void *Context, const uint8_t *Record, size_t Bytes)
{
    return Mt7922WdiScanEvent(Context, Record, Bytes);
}

static bool DeliverScanFrame(void *Context, const uint8_t *Record, size_t Bytes)
{
    return Mt7922WdiScanFrame(Context, Record, Bytes);
}

NTSTATUS Mt7922BindWificxScan(MT7922_WDI_SCAN *Scan, WDFDEVICE Device,
    MT7922_MCU_STATION *Station, const TLV_CONTEXT *Tlv)
{
    if (!Station->Ready || Station->Scan.Radio == NULL || Station->Scan.Rings == NULL ||
        Device == NULL || Tlv == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    *Scan = (MT7922_WDI_SCAN){
        .Transport = &Station->Scan,
        .Submit = Mt7922McuSubmitStationScan,
        .Host = Device,
        .Indicate = PublishScanIndication,
        .Tlv = Tlv,
        .ChipId = Station->Scan.ChipId
    };
    Mt7922McuSetScanReceiver(Station->Scan.Rings, Scan, DeliverScanEvent, DeliverScanFrame);
    return STATUS_SUCCESS;
}

void Mt7922HandleWificxScan(MT7922_WDI_SCAN *Scan, WIFIREQUEST Request,
    uint8_t BandIndex, uint8_t BssIndex)
{
    UINT inputBytes;
    UINT outputBytes;
    WDI_MESSAGE_HEADER *header = WifiRequestGetInOutBuffer(Request, &inputBytes, &outputBytes);
    if (header == NULL || inputBytes < sizeof(*header) || outputBytes < sizeof(*header)) {
        WifiRequestComplete(Request, STATUS_INVALID_BUFFER_SIZE, 0);
        return;
    }
    WDI_SCAN_PARAMETERS parameters = {0};
    const UINT8 *tlv = (const UINT8 *)header + sizeof(*header);
    const ULONG length = (ULONG)(inputBytes - sizeof(*header));
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    if (ParseWdiTaskScan(length, tlv, Scan->Tlv, &parameters) == NDIS_STATUS_SUCCESS) {
        status = Mt7922StartWdiScan(Scan, header, &parameters, BandIndex, BssIndex);
        CleanupParsedWdiTaskScan(&parameters);
    }
#ifdef MT7922_STATION_PACKET_TRACE
    const MT7922_MCU_SCAN_TRANSPORT *transport = Scan->Transport;
    const uint32_t flags = (Scan->Active ? MT7922_CONTROL_SCAN_ACTIVE : 0U) |
        (Scan->Cancelling ? MT7922_CONTROL_SCAN_CANCELLING : 0U) |
        (transport != NULL && transport->Radio != NULL ? MT7922_CONTROL_SCAN_RADIO_BOUND : 0U);
    Mt7922TraceStationControl("ScanSubmit", WDI_TASK_SCAN, header->TransactionId, header->PortId,
        status, flags, Scan->Sequence);
#endif
    header->Status = status;
    WifiRequestComplete(Request, status, sizeof(*header));
    /* The same worker dispatches RX after the request-start completion. */
}

C_ASSERT(FIELD_OFFSET(MT7922_CONNECT_DEBUG, Eapol) == 416);
C_ASSERT(FIELD_OFFSET(MT7922_CONNECT_DEBUG, RadioWidthMhz) == 70);
C_ASSERT(FIELD_OFFSET(MT7922_CONNECT_DEBUG, Data) == 504);
C_ASSERT(FIELD_OFFSET(MT7922_CONNECT_DEBUG, Observation) == 552);
C_ASSERT(FIELD_OFFSET(MT7922_CONNECT_DEBUG, SharedAdvert) == 588);
C_ASSERT(FIELD_OFFSET(MT7922_CONNECT_DEBUG, SharedEcho) == 624);
C_ASSERT(FIELD_OFFSET(MT7922_CONNECT_DEBUG, Coexistence) == 664);
C_ASSERT(FIELD_OFFSET(MT7922_CONNECT_DEBUG, StationIo) == 688);
C_ASSERT(sizeof(MT7922_CONNECT_DEBUG) == 744);
C_ASSERT(sizeof(MT7922_SHARED_ADVERT_REQUEST) == 12);

static NTSTATUS OpenWificxServiceKey(ACCESS_MASK Access, HANDLE *Key)
{
    UNICODE_STRING path;
    RtlInitUnicodeString(&path,
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\mt7922wificx");
    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, &path,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    return ZwOpenKey(Key, Access, &attributes);
}

void Mt7922PollWificxSharedAdvertRequest(MT7922_MCU_STATION *Station)
{
    if (Station->SharedAdvertDeadline != 0) {
        return;
    }
    HANDLE key;
    if (!NT_SUCCESS(OpenWificxServiceKey(KEY_QUERY_VALUE | KEY_SET_VALUE, &key))) {
        return;
    }

    UNICODE_STRING name;
#ifdef MT7922_STATION_PACKET_TRACE
    if (Mt7922StationPacketTraceEnabled()) {
        RtlInitUnicodeString(&name, L"StationPacketTraceTargets");
        ULONG storage[5] = {0};
        ULONG length;
        if (NT_SUCCESS(ZwQueryValueKey(key, &name, KeyValuePartialInformation,
                storage, sizeof(storage), &length))) {
            const KEY_VALUE_PARTIAL_INFORMATION *targets = (const KEY_VALUE_PARTIAL_INFORMATION *)storage;
            if (targets->Type == REG_BINARY && targets->DataLength == 8) {
                Mt7922SetStationPacketTraceTargets(targets->Data);
                (void)ZwDeleteValueKey(key, &name);
            }
        }
    }
#endif
    RtlInitUnicodeString(&name, L"StationIoTraceSeconds");
    ULONG traceData[4] = {0};
    ULONG traceLength;
    if (NT_SUCCESS(ZwQueryValueKey(key, &name, KeyValuePartialInformation,
            traceData, sizeof(traceData), &traceLength))) {
        const KEY_VALUE_PARTIAL_INFORMATION *traceValue = (const KEY_VALUE_PARTIAL_INFORMATION *)traceData;
        if (traceValue->Type == REG_DWORD && traceValue->DataLength == sizeof(ULONG) &&
            NT_SUCCESS(ZwDeleteValueKey(key, &name))) {
            const ULONG seconds = traceData[3];
            if (seconds > 0 && seconds <= 120 && !Station->ConnectDebug.StationIo.Active) {
                Station->ConnectDebug.StationIo = (MT7922_STATION_IO_DEBUG){.Active = 1};
                Station->StationIoTraceDeadline = KeQueryInterruptTime() + seconds * 10000000ULL;
            }
        }
    }
    RtlInitUnicodeString(&name, L"SharedAdvertRequest");
    ULONG data[3 + sizeof(MT7922_SHARED_ADVERT_REQUEST) / sizeof(ULONG)] = {0};
    ULONG length;
    const NTSTATUS queried = ZwQueryValueKey(key, &name, KeyValuePartialInformation,
        data, sizeof(data), &length);
    const KEY_VALUE_PARTIAL_INFORMATION *value = (const KEY_VALUE_PARTIAL_INFORMATION *)data;
    if (NT_SUCCESS(queried) && value->Type == REG_BINARY &&
        value->DataLength == sizeof(MT7922_SHARED_ADVERT_REQUEST) &&
        NT_SUCCESS(ZwDeleteValueKey(key, &name))) {
        MT7922_SHARED_ADVERT_REQUEST request;
        RtlCopyMemory(&request, value->Data, sizeof(request));
        const NTSTATUS status = Mt7922McuArmSharedAdvert(Station, &request);
        if (!NT_SUCCESS(status)) {
            Station->ConnectDebug.SharedAdvert = (MT7922_SHARED_ADVERT_DEBUG){0};
            Station->ConnectDebug.SharedAdvert.RequestId = request.RequestId;
            Station->ConnectDebug.SharedAdvert.Status = status;
        }
    }
    ZwClose(key);
}

void Mt7922SaveWificxConnectDebug(MT7922_MCU_STATION *Station)
{
    Station->ConnectDebug.Eapol.PrivacyRequests = Station->EapolPrivacyRequests;
    Station->ConnectDebug.Eapol.PrivacyStatus = Station->EapolPrivacyStatus;
    Station->ConnectDebug.Eapol.RxPolicy = (uint32_t)Station->EapolRxPrivacy;
    if (Station->ConnectDebug.Observation.Channel != 0) {
        const MT7922_STATION_OBSERVER *observer = &Station->Observer;
        Station->ConnectDebug.Observation = (MT7922_STATION_OBSERVATION_DEBUG){
            .Channel = observer->Channel,
            .Running = observer->Running,
            .Records = observer->Records,
            .Invalid = observer->Invalid,
            .Psf = observer->Psf,
            .Mif = observer->Mif,
            .Data = observer->Data,
            .DataToUs = observer->DataToUs,
            .PeerCount = observer->PeerCount
        };
    }
    HANDLE key;
    if (!NT_SUCCESS(OpenWificxServiceKey(KEY_SET_VALUE, &key))) {
        return;
    }
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, L"ConnectDebug");
    ZwSetValueKey(key, &name, 0, REG_BINARY, (PVOID)&Station->ConnectDebug,
        sizeof(Station->ConnectDebug));
    ZwClose(key);
}

typedef struct {
    WDFDEVICE Device;
    NTSTATUS Status;
} JOIN_INDICATION;

static void PublishJoinIndication(void *Context, uint16_t Message,
    const uint8_t *Buffer, size_t Bytes)
{
    JOIN_INDICATION *indication = Context;
    if (NT_SUCCESS(indication->Status)) {
        indication->Status = Mt7922PublishWificxIndication(indication->Device, Message, Buffer, Bytes);
    }
}

NTSTATUS Mt7922CompleteWificxJoin(MT7922_MCU_STATION *Station, WDFDEVICE Device,
    const WDI_MESSAGE_HEADER *Header, const TLV_CONTEXT *Tlv,
    const MT7922_STATION_JOIN_REQUEST *Join, bool Wpa2PersonalCcmp,
    MT7922_STATION_RECEIVE Receive, void *Context)
{
    if (Header == NULL || Tlv == NULL || Device == NULL || !Station->Ready || Station->Ap.Band != 2 ||
        Station->AssociationId != 0) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    MT7922_STATION_JOIN_RESULT *result = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*result), 'ActM');
    NTSTATUS joined = STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;
    if (result != NULL) {
        joined = Mt7922McuExchangeStationJoin(Station, Join, result, Receive, Context);
        if (NT_SUCCESS(joined)) {
            Station->WdiPortId = Header->PortId;
        }
        Station->ConnectDebug.JoinStatus = joined;
        Station->ConnectDebug.AuthStatusCode = result->Authentication.StatusCode;
        Station->ConnectDebug.AssocStatusCode = result->Association.StatusCode;
        Station->ConnectDebug.AssociationId = result->Association.AssociationId;
        JOIN_INDICATION indication = {Device, STATUS_SUCCESS};
        status = Mt7922IndicateWdiStationJoin(Header, Tlv, Station->Ap.Bssid,
            Wpa2PersonalCcmp, result, joined, PublishJoinIndication, &indication);
        ExFreePoolWithTag(result, 'ActM');
        if (NT_SUCCESS(status)) {
            status = indication.Status;
        }
        Station->ConnectDebug.ReportStatus = status;
        if (NT_SUCCESS(joined) && NT_SUCCESS(status)) {
            Station->ConnectDebug.Eapol.ReportedCipher =
                Wpa2PersonalCcmp ? WDI_CIPHER_ALGO_CCMP : WDI_CIPHER_ALGO_NONE;
        }
        Mt7922SaveWificxConnectDebug(Station);
    }
    if (!NT_SUCCESS(status)) {
        if (NT_SUCCESS(joined)) {
            Station->KeyStateInvalid = true;
        }
        const WDI_MESSAGE_HEADER failed = {
            .PortId = Header->PortId, .TransactionId = Header->TransactionId,
            .IhvSpecificId = Header->IhvSpecificId, .Status = status
        };
        Mt7922PublishWificxIndication(Device, WDI_INDICATION_CONNECT_COMPLETE,
            (const uint8_t *)&failed, sizeof(failed));
        return status;
    }
    Station->WdiAssociationReported = NT_SUCCESS(joined);
    return joined;
}

void Mt7922HandleWificxCcmpKeys(MT7922_MCU_STATION *Station, WIFIREQUEST Request,
    const TLV_CONTEXT *Tlv)
{
    UINT inputBytes;
    UINT outputBytes;
    WDI_MESSAGE_HEADER *header = WifiRequestGetInOutBuffer(Request, &inputBytes, &outputBytes);
    if (header == NULL || inputBytes < sizeof(*header) || outputBytes < sizeof(*header)) {
        WifiRequestComplete(Request, STATUS_INVALID_BUFFER_SIZE, 0);
        return;
    }

    NTSTATUS status = STATUS_INVALID_DEVICE_STATE;
    Station->ConnectDebug.KeyStatus = status;
    if (Station->Ready && Station->AssociationId != 0 && Station->WdiAssociationReported &&
        !Station->KeyStateInvalid && header->PortId == Station->WdiPortId && Tlv != NULL) {
        WDI_SET_ADD_CIPHER_KEYS_PARAMETERS keys = {0};
        const UINT8 *tlvs = (const UINT8 *)header + sizeof(*header);
        const ULONG bytes = (ULONG)(inputBytes - sizeof(*header));
        status = STATUS_INVALID_PARAMETER;
        if (ParseWdiSetAddCipherKeys(bytes, tlvs, Tlv, &keys) == NDIS_STATUS_SUCCESS) {
            Station->ConnectDebug.KeyCount += keys.SetCipherKey.ElementCount;
            status = Mt7922ApplyWdiCcmpKeys(&keys, Station->Ap.Bssid,
                Mt7922McuInstallStationCcmp, Station);
            for (UINT32 i = 0; i < keys.SetCipherKey.ElementCount; ++i) {
                WDI_PRIVATE_BYTE_BLOB *key = &keys.SetCipherKey.pElements[i].CCMPKey;
                if (key->MemoryInternallyAllocated && key->pElements != NULL) {
                    RtlSecureZeroMemory(key->pElements, key->ElementCount);
                }
            }
            CleanupParsedWdiSetAddCipherKeys(&keys);
        }
    }

    Station->ConnectDebug.KeyStatus = status;
    Mt7922SaveWificxConnectDebug(Station);
    header->Status = status;
    WifiRequestComplete(Request, status, sizeof(*header));
}

void Mt7922HandleWificxConnect(MT7922_MCU_STATION *Station, WDFDEVICE Device,
    WIFIREQUEST Request, const TLV_CONTEXT *Tlv, const MT7922_WDI_SCAN *Scan,
    uint16_t Wcid, MT7922_STATION_RECEIVE Receive, void *Context)
{
    UINT inputBytes;
    UINT outputBytes;
    WDI_MESSAGE_HEADER *header = WifiRequestGetInOutBuffer(Request, &inputBytes, &outputBytes);
    if (header == NULL || inputBytes < sizeof(*header) || outputBytes < sizeof(*header)) {
        WifiRequestComplete(Request, STATUS_INVALID_BUFFER_SIZE, 0);
        return;
    }

    const uint32_t connectSequence = Station->ConnectDebug.Sequence + 1;
    RtlZeroMemory(&Station->ConnectDebug, sizeof(Station->ConnectDebug));
    Station->ConnectDebug.Sequence = connectSequence;

    MT7922_WDI_CONNECT_PLAN plan = {0};
    NTSTATUS status = STATUS_INVALID_DEVICE_STATE;
    if (Station->Ready && !Station->KeyStateInvalid && Station->AssociationId == 0 &&
        Scan != NULL && !Scan->Active && !Scan->Cancelling &&
        Tlv != NULL && Device != NULL && Station->Scan.Radio != NULL && Wcid != Station->MulticastWcid) {
        WDI_TASK_CONNECT_PARAMETERS parameters = {0};
        const UINT8 *bytes = (const UINT8 *)header + sizeof(*header);
        status = STATUS_INVALID_PARAMETER;
        if (ParseWdiTaskConnect((ULONG)(inputBytes - sizeof(*header)), bytes, Tlv, &parameters) ==
                NDIS_STATUS_SUCCESS) {
            status = Mt7922MakeWdiConnectPlan(&parameters, Wcid, Station->Scan.Radio, &plan);
            CleanupParsedWdiTaskConnect(&parameters);
        }
    }
    Station->ConnectDebug.PlanStatus = status;
    Station->ConnectDebug.WdiAuthAlgorithm = plan.AuthAlgorithm;
    Station->ConnectDebug.WdiUnicastCipher = plan.UnicastCipher;
    Station->ConnectDebug.WdiFlags = plan.Flags;
    Station->ConnectDebug.CandidateCount = plan.CandidateCount;
    Station->ConnectDebug.FirstBandId = plan.FirstBandId;
    Station->ConnectDebug.FirstChannel = plan.FirstChannel;
    Station->ConnectDebug.FirstBody = plan.FirstBody;
    Station->ConnectDebug.CandidateReason = plan.CandidateReason;
    Station->ConnectDebug.IeFail = plan.IeFail;
    Station->ConnectDebug.BodyBytes = plan.BodyBytes;
    Station->ConnectDebug.ParamsVendorBytes = plan.ParamsVendorBytes;
    Station->ConnectDebug.EntryVendorBytes = plan.EntryVendorBytes;
    RtlCopyMemory(Station->ConnectDebug.BodySample, plan.BodySample,
        sizeof(Station->ConnectDebug.BodySample));
    RtlCopyMemory(Station->ConnectDebug.ParamsVendorSample, plan.ParamsVendorSample,
        sizeof(Station->ConnectDebug.ParamsVendorSample));
    RtlCopyMemory(Station->ConnectDebug.EntryVendorSample, plan.EntryVendorSample,
        sizeof(Station->ConnectDebug.EntryVendorSample));
    if (NT_SUCCESS(status)) {
        Station->ConnectDebug.OsBand = plan.Ap.Band;
        Station->ConnectDebug.OsChannel = plan.Ap.Channel;
        RtlCopyMemory(Station->ConnectDebug.ApBssid, plan.Ap.Bssid,
            sizeof(Station->ConnectDebug.ApBssid));
    }
    Mt7922SaveWificxConnectDebug(Station);

    const WDI_MESSAGE_HEADER original = *header;
    header->Status = status;
    WifiRequestComplete(Request, status, sizeof(*header));
    if (!NT_SUCCESS(status)) {
        return;
    }

    status = Mt7922McuPrepareStationAp(Station, &plan.Ap);
    Station->ConnectDebug.PrepareStatus = status;
    Station->ConnectDebug.ApBand = Station->Ap.Band;
    Station->ConnectDebug.ApChannel = Station->Ap.Channel;
    if (NT_SUCCESS(status)) {
        Station->ConnectDebug.RadioWidthMhz = Station->Ap.Center80 != 0 ? 80 : 20;
        Station->ConnectDebug.RadioCenterChannel = Station->Ap.Center80 != 0
            ? Station->Ap.Center80 : Station->Ap.Channel;
    }
    Mt7922SaveWificxConnectDebug(Station);
    if (NT_SUCCESS(status)) {
        Mt7922CompleteWificxJoin(Station, Device, &original, Tlv, &plan.Join, true, Receive, Context);
    } else {
        WDI_MESSAGE_HEADER completed = original;
        completed.Status = status;
        Mt7922PublishWificxIndication(Device, WDI_INDICATION_CONNECT_COMPLETE,
            (const uint8_t *)&completed, sizeof(completed));
    }
}

void Mt7922HandleWificxEapolPrivacy(MT7922_MCU_STATION *Station, WIFIREQUEST Request,
    const TLV_CONTEXT *Tlv)
{
    UINT inputBytes;
    UINT outputBytes;
    WDI_MESSAGE_HEADER *header = WifiRequestGetInOutBuffer(Request, &inputBytes, &outputBytes);
    if (header == NULL || inputBytes < sizeof(*header) || outputBytes < sizeof(*header)) {
        WifiRequestComplete(Request, STATUS_INVALID_BUFFER_SIZE, 0);
        return;
    }

    ++Station->EapolPrivacyRequests;
    NTSTATUS status = STATUS_INVALID_DEVICE_STATE;
    if (Station->Ready && !Station->KeyStateInvalid && Tlv != NULL &&
        (!Station->WdiAssociationReported || header->PortId == Station->WdiPortId)) {
        WDI_SET_PRIVACY_EXEMPTION_LIST_PARAMETERS parameters = {0};
        const UINT8 *tlvs = (const UINT8 *)header + sizeof(*header);
        const ULONG bytes = (ULONG)(inputBytes - sizeof(*header));
        status = STATUS_INVALID_PARAMETER;
        if (ParseWdiSetPrivacyExemptionList(bytes, tlvs, Tlv, &parameters) == NDIS_STATUS_SUCCESS) {
            MT7922_PRIVACY policy;
            status = Mt7922ReadWdiEapolPrivacy(&parameters, &policy);
            if (NT_SUCCESS(status) && policy != Station->EapolRxPrivacy) {
                Station->EapolRxPrivacy = policy;
                if (Station->PendingFrameBytes != 0 && Station->PendingFrameEapol) {
                    Station->PendingFrameBytes = 0;
                }
            }
            CleanupParsedWdiSetPrivacyExemptionList(&parameters);
        }
    }
    Station->EapolPrivacyStatus = status;
    if (Station->ConnectDebug.Sequence != 0) {
        Mt7922SaveWificxConnectDebug(Station);
    }
    header->Status = status;
    WifiRequestComplete(Request, status, sizeof(*header));
}
