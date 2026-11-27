#include <ntddk.h>
#include <wdf.h>
#include <ndis.h>
#include <bcrypt.h>
#include <netadaptercx.h>
#include <wificx.h>
#include <dot11wificxintf.h>
#include <dot11wificxtypes.hpp>
#include <TlvGeneratorParser.hpp>
#include "probe.h"
#include "mcu.h"
#include "wificx-device.h"
#include "wificx-netqueues.h"
#include "wificx-scan-kernel.h"
#include "station-packet-trace.h"

#define STATION_PEER_WCID 1U
#define STATION_GROUP_WCID 19U
#define POLL_MILLISECONDS 10U
#define FAST_POLL_MILLISECONDS 1U
#define SCAN_TIMEOUT_100NS (15ULL * 1000U * 10000U)
#define CANCEL_TIMEOUT_100NS (1ULL * 1000U * 10000U)
#define MAX_PENDING_REQUESTS 16U

typedef struct {
    LIST_ENTRY Entry;
    WIFIREQUEST Request;
} STATION_REQUEST;

typedef struct {
    WDFDEVICE Device;
    ULONG ChipId;
    WDFWORKITEM Worker;
    WDFTIMER Timer;
    WDFTIMER FastPollTimer;
    KSPIN_LOCK Gate;
    LIST_ENTRY Requests;
    ULONG PendingRequests;
    bool Running;
    bool WorkQueued;
    bool FastPollActive;
    bool CapsReported;
    bool RadioEnabled;
    NETADAPTER Adapter;
    MT7922_NET_QUEUES *Queues;
    ULONG PortId;
    TLV_CONTEXT Tlv;
    PUCHAR Bar;
    WDFDMAENABLER Dma;
    WDFMEMORY Firmware;
    WDFMEMORY Clc;
    MT7922_STATION_RADIO_PROFILE Profile;
    MT7922_MCU_STATION Station;
    MT7922_WDI_SCAN Scan;
    ULONGLONG ScanDeadline;
    ULONGLONG HandshakeDebugNext;
    ULONGLONG HandshakeDebugDeadline;
    ULONGLONG SharedAdvertRequestPoll;
    ULONGLONG SharedReceiveLastPoll;
    ULONGLONG CoexistenceDebugNext;
    ULONGLONG CoexistenceDebugUntil;
    UCHAR PermanentMac[6];
} STATION_DEVICE;

typedef struct {
    STATION_DEVICE *Device;
} STATION_ADAPTER;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(STATION_DEVICE, GetStationDevice);
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(STATION_ADAPTER, GetStationAdapter);

static EVT_WIFI_DEVICE_SEND_COMMAND DispatchStationRequest;
static EVT_WIFI_DEVICE_CREATE_ADAPTER CreateStationAdapter;
static EVT_WIFI_DEVICE_CREATE_WIFIDIRECTDEVICE RejectWifiDirect;
static EVT_WDF_OBJECT_CONTEXT_CLEANUP CleanupStationAdapter;
static EVT_WDF_WORKITEM RunStationWorker;
static EVT_WDF_TIMER PollStation;

NTSTATUS Mt7922WifiConfigureDeviceInit(PWDFDEVICE_INIT DeviceInit)
{
    /* Microsoft's WiFiCx sample starts NetAdapterCx before WiFiCx. */
    NTSTATUS status = NetDeviceInitConfig(DeviceInit);
    return NT_SUCCESS(status) ? WifiDeviceInitConfig(DeviceInit) : status;
}

NTSTATUS Mt7922WifiInitializeDevice(WDFDEVICE Device)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, STATION_DEVICE);
    STATION_DEVICE *state;
    NTSTATUS status = WdfObjectAllocateContext(Device, &attributes, (void **)&state);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    state->Device = Device;
    state->RadioEnabled = true;
    KeInitializeSpinLock(&state->Gate);
    InitializeListHead(&state->Requests);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Device;
    WDF_WORKITEM_CONFIG work;
    WDF_WORKITEM_CONFIG_INIT(&work, RunStationWorker);
    work.AutomaticSerialization = FALSE;
    status = WdfWorkItemCreate(&work, &attributes, &state->Worker);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_TIMER_CONFIG timer;
    WDF_TIMER_CONFIG_INIT_PERIODIC(&timer, PollStation, POLL_MILLISECONDS);
    timer.AutomaticSerialization = FALSE;
    status = WdfTimerCreate(&timer, &attributes, &state->Timer);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_TIMER_CONFIG_INIT_PERIODIC(&timer, PollStation, FAST_POLL_MILLISECONDS);
    timer.AutomaticSerialization = FALSE;
    timer.UseHighResolutionTimer = WdfTrue;
    status = WdfTimerCreate(&timer, &attributes, &state->FastPollTimer);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WIFI_DEVICE_CONFIG config;
    WIFI_DEVICE_CONFIG_INIT(&config, WDI_VERSION_LATEST, DispatchStationRequest,
        CreateStationAdapter, RejectWifiDirect);
    status = WifiDeviceInitialize(Device, &config);
    if (NT_SUCCESS(status)) {
        state->Tlv.AllocationContext = 0;
        state->Tlv.PeerVersion = WifiDeviceGetOsWdiVersion(Device);
    }
    return status;
}

/* Modern driver packages must keep every file in the driver store (DIRID 13).
   Resolve the package directory from the service ImagePath instead of assuming
   System32\drivers. */
static NTSTATUS OpenStationPackageFile(const WCHAR *FileName, HANDLE *File)
{
    DECLARE_CONST_UNICODE_STRING(servicePath,
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\mt7922wificx");
    OBJECT_ATTRIBUTES keyAttributes;
    InitializeObjectAttributes(&keyAttributes, (PUNICODE_STRING)&servicePath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    HANDLE key;
    NTSTATUS status = ZwOpenKey(&key, KEY_QUERY_VALUE, &keyAttributes);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    DECLARE_CONST_UNICODE_STRING(valueName, L"ImagePath");
    enum { MaxPathChars = 260 };
    UCHAR buffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + MaxPathChars * sizeof(WCHAR)];
    ULONG resultBytes;
    status = ZwQueryValueKey(key, (PUNICODE_STRING)&valueName, KeyValuePartialInformation,
        buffer, sizeof(buffer), &resultBytes);
    ZwClose(key);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    const KEY_VALUE_PARTIAL_INFORMATION *information =
        (const KEY_VALUE_PARTIAL_INFORMATION *)buffer;
    if (resultBytes < (ULONG)FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) ||
        (information->Type != REG_SZ && information->Type != REG_EXPAND_SZ) ||
        information->DataLength < 2 * (ULONG)sizeof(WCHAR) ||
        information->DataLength > MaxPathChars * (ULONG)sizeof(WCHAR)) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    /* Keep the directory that holds the current package's .sys file. */
    const WCHAR *imagePath = (const WCHAR *)information->Data;
    ULONG directoryChars = information->DataLength / sizeof(WCHAR);
    while (directoryChars > 0 && imagePath[directoryChars - 1] != L'\\') {
        --directoryChars;
    }
    WCHAR nameChars = 0;
    while (nameChars < 64 && FileName[nameChars] != 0) {
        ++nameChars;
    }
    if (directoryChars == 0 || nameChars == 0 ||
        directoryChars + nameChars >= MaxPathChars) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    WCHAR path[MaxPathChars];
    RtlCopyMemory(path, imagePath, directoryChars * sizeof(WCHAR));
    RtlCopyMemory(path + directoryChars, FileName, nameChars * sizeof(WCHAR));
    UNICODE_STRING fullPath;
    fullPath.Buffer = path;
    fullPath.Length = (USHORT)((directoryChars + nameChars) * sizeof(WCHAR));
    fullPath.MaximumLength = fullPath.Length;
    OBJECT_ATTRIBUTES fileAttributes;
    InitializeObjectAttributes(&fileAttributes, &fullPath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    IO_STATUS_BLOCK io;
    return ZwOpenFile(File, FILE_GENERIC_READ, &fileAttributes, &io, FILE_SHARE_READ,
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
}

static NTSTATUS LoadStationFirmware(STATION_DEVICE *State)
{
    HANDLE file;
    const bool mt7925 = State->ChipId == MT7925_CHIP_ID;
    const ULONG firmwareBytes = mt7925
        ? MT7925_REFERENCE_IMAGE_BYTES : MT7922_REFERENCE_IMAGE_BYTES;
    NTSTATUS status = OpenStationPackageFile(mt7925
        ? L"mt7925-wificx-fw.bin" : L"mt7922-wificx-fw.bin", &file);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    IO_STATUS_BLOCK io;
    FILE_STANDARD_INFORMATION information;
    status = ZwQueryInformationFile(file, &io, &information, sizeof(information), FileStandardInformation);
    if (NT_SUCCESS(status) && information.EndOfFile.QuadPart != firmwareBytes) {
        status = STATUS_INVALID_IMAGE_FORMAT;
    }
    if (NT_SUCCESS(status)) {
        WDF_OBJECT_ATTRIBUTES attributes;
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = State->Device;
        void *buffer;
        status = WdfMemoryCreate(&attributes, NonPagedPoolNx, 'FctM',
            firmwareBytes, &State->Firmware, &buffer);
        if (NT_SUCCESS(status)) {
            status = ZwReadFile(file, NULL, NULL, NULL, &io, buffer, firmwareBytes, NULL, NULL);
            if (NT_SUCCESS(status) && io.Information != firmwareBytes) {
                status = STATUS_END_OF_FILE;
            }
        }
    }
    ZwClose(file);
    return status;
}

static NTSTATUS LoadStationClc(STATION_DEVICE *State)
{
    if (State->ChipId == MT7925_CHIP_ID) {
        /* The backed-up MT7925 RAM image has no CLC region. */
        return STATUS_SUCCESS;
    }
    HANDLE file;
    NTSTATUS status = OpenStationPackageFile(L"mt7922-wificx-clc.bin", &file);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    IO_STATUS_BLOCK io;
    FILE_STANDARD_INFORMATION information;
    status = ZwQueryInformationFile(file, &io, &information, sizeof(information), FileStandardInformation);
    if (NT_SUCCESS(status) && information.EndOfFile.QuadPart != MT7922_CLC_IMAGE_BYTES) {
        status = STATUS_INVALID_IMAGE_FORMAT;
    }
    if (NT_SUCCESS(status)) {
        WDF_OBJECT_ATTRIBUTES attributes;
        WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
        attributes.ParentObject = State->Device;
        void *buffer;
        status = WdfMemoryCreate(&attributes, NonPagedPoolNx, 'CctM',
            MT7922_CLC_IMAGE_BYTES, &State->Clc, &buffer);
        if (NT_SUCCESS(status)) {
            status = ZwReadFile(file, NULL, NULL, NULL, &io, buffer, MT7922_CLC_IMAGE_BYTES, NULL, NULL);
            if (NT_SUCCESS(status) && io.Information != MT7922_CLC_IMAGE_BYTES) {
                status = STATUS_END_OF_FILE;
            }
        }
    }
    ZwClose(file);
    if (NT_SUCCESS(status)) {
        /* The reference firmware's CLC region, packed by inspect-firmware.py. */
        UCHAR digest[32];
        status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, NULL, 0,
            WdfMemoryGetBuffer(State->Clc, NULL), MT7922_CLC_IMAGE_BYTES, digest, sizeof(digest));
        static const UCHAR expected[32] = {
            0xd5, 0xb4, 0x82, 0xcc, 0xd7, 0xd8, 0xe6, 0xdb, 0xfd, 0x59, 0xfb, 0x6e, 0xfd, 0x5c,
            0x8a, 0x5f, 0x82, 0xe1, 0x3d, 0xec, 0xa4, 0x2f, 0xa2, 0xc3, 0xa0, 0x8c, 0xef, 0xe4,
            0x5a, 0x69, 0x23, 0x9f
        };
        if (NT_SUCCESS(status) && RtlCompareMemory(digest, expected, sizeof(digest)) != sizeof(digest)) {
            status = STATUS_DATA_ERROR;
        }
    }
    if (!NT_SUCCESS(status) && State->Clc != NULL) {
        WdfObjectDelete(State->Clc);
        State->Clc = NULL;
    }
    return status;
}

NTSTATUS Mt7922WifiPrepareDevice(WDFDEVICE Device, PUCHAR Bar, WDFDMAENABLER Dma,
    ULONG ChipId)
{
    STATION_DEVICE *state = GetStationDevice(Device);
    if (ChipId != MT7922_CHIP_ID && ChipId != MT7925_CHIP_ID) {
        return STATUS_NOT_SUPPORTED;
    }
    state->ChipId = ChipId;
    WDFKEY key;
    NTSTATUS status = WdfDeviceOpenRegistryKey(Device, PLUGPLAY_REGKEY_DRIVER, KEY_QUERY_VALUE,
        WDF_NO_OBJECT_ATTRIBUTES, &key);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    DECLARE_CONST_UNICODE_STRING(profileName, L"StationRadioProfile");
    ULONG type;
    ULONG bytes;
    status = WdfRegistryQueryValue(key, &profileName, sizeof(state->Profile),
        &state->Profile, &bytes, &type);
    WdfRegistryClose(key);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    if (type != REG_BINARY || bytes != sizeof(state->Profile) || !Mt7922ValidateStationRadio(&state->Profile)) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    state->Bar = Bar;
    state->Dma = Dma;
    status = LoadStationFirmware(state);
    return NT_SUCCESS(status) ? LoadStationClc(state) : status;
}

/* Flags describe the observed state, including the scan dispatch preconditions. */
static uint32_t StationControlFlags(const STATION_DEVICE *State, bool Running, bool ValidPort)
{
    return (Running ? MT7922_CONTROL_RUNNING : 0U) |
        (State->RadioEnabled ? MT7922_CONTROL_RADIO_ENABLED : 0U) |
        (State->Station.Ready ? MT7922_CONTROL_STATION_READY : 0U) |
        (State->Station.Scan.Radio != NULL ? MT7922_CONTROL_SCAN_RADIO_BOUND : 0U) |
        (ValidPort ? MT7922_CONTROL_PORT_VALID : 0U) |
        (State->Scan.Active ? MT7922_CONTROL_SCAN_ACTIVE : 0U) |
        (State->Scan.Cancelling ? MT7922_CONTROL_SCAN_CANCELLING : 0U);
}

static void TraceRadioRequest(const char *Stage, const WDI_MESSAGE_HEADER *Header,
    const STATION_DEVICE *State, NTSTATUS Status, uint32_t Detail)
{
    Mt7922TraceStationControl(Stage, WDI_TASK_SET_RADIO_STATE,
        Header->TransactionId, Header->PortId, Status, StationControlFlags(State, true, false), Detail);
}

static NTSTATUS StartStationRadio(STATION_DEVICE *State)
{
    size_t bytes;
    size_t clcBytes = 0;
    const UCHAR *image = WdfMemoryGetBuffer(State->Firmware, &bytes);
    const UCHAR *clc = State->Clc != NULL ? WdfMemoryGetBuffer(State->Clc, &clcBytes) : NULL;
    NTSTATUS status = Mt7922McuStartStation(&State->Station, State->Bar, State->ChipId, State->Dma,
        image, bytes, STATION_GROUP_WCID, &State->Profile, clc, clcBytes);
    Mt7922TraceStationControl("McuStart", 0, 0, 0, status, 0,
        State->Station.Info.CleanupStatus);
    if (NT_SUCCESS(status)) {
        status = Mt7922BindWificxScan(&State->Scan, State->Device, &State->Station, &State->Tlv);
        Mt7922TraceStationControl("ScanBind", 0, 0, 0, status, 0, 0);
        if (NT_SUCCESS(status)) {
            RtlCopyMemory(State->PermanentMac, State->Station.Info.NicMac, sizeof(State->PermanentMac));
        } else {
            Mt7922McuStopStation(&State->Station);
        }
    }
    return status;
}

NTSTATUS Mt7922WifiEnterD0(WDFDEVICE Device)
{
    STATION_DEVICE *state = GetStationDevice(Device);
    if (state->Firmware == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    /* Read hardware capabilities on first boot even if a previous D0 had radio off. */
    if (state->RadioEnabled || !state->CapsReported) {
        NTSTATUS status = StartStationRadio(state);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }
    KIRQL irql;
    KeAcquireSpinLock(&state->Gate, &irql);
    state->Running = true;
    KeReleaseSpinLock(&state->Gate, irql);
    WdfTimerStart(state->Timer, WDF_REL_TIMEOUT_IN_MS(POLL_MILLISECONDS));
    return STATUS_SUCCESS;
}

NTSTATUS Mt7922WifiReportCapabilities(WDFDEVICE Device, WDF_POWER_DEVICE_STATE PreviousState)
{
    UNREFERENCED_PARAMETER(PreviousState);
    STATION_DEVICE *state = GetStationDevice(Device);
    if (state->CapsReported) {
        return STATUS_SUCCESS;
    }

    WIFI_DEVICE_CAPABILITIES device;
    WIFI_DEVICE_CAPABILITIES_INIT(&device);
    device.HardwareRadioState = TRUE;
    device.SoftwareRadioState = state->RadioEnabled;
    device.NumRxStreams = 1; /* Legacy OFDM station profile. */
    device.NumTxStreams = 1;
    NTSTATUS status = WifiDeviceSetDeviceCapabilities(Device, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    DOT11_AUTH_CIPHER_PAIR ccmp = {DOT11_AUTH_ALGO_RSNA_PSK, DOT11_CIPHER_ALGO_CCMP};
    WIFI_STATION_CAPABILITIES station;
    WIFI_STATION_CAPABILITIES_INIT(&station);
    station.ScanSSIDListSize = MT7922_SCAN_MAX_SSIDS;
    station.DesiredSSIDListSize = 1;
    station.PrivacyExemptionListSize = 1;
    station.KeyMappingTableSize = 1;
    station.DefaultKeyTableSize = 4;
    station.MaxNumPerSTA = 1;
    station.NumSupportedUnicastAlgorithms = 1;
    station.UnicastAlgorithmsList = &ccmp;
    station.NumSupportedMulticastDataAlgorithms = 1;
    station.MulticastDataAlgorithmsList = &ccmp;
    status = WifiDeviceSetStationCapabilities(Device, &station);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDI_PHY_TYPE ofdm = WDI_PHY_TYPE_OFDM;
    UINT32 width = 20;
    WDI_CHANNEL_MAPPING_ENTRY channels[MT7922_STATION_RADIO_CHANNELS];
    for (ULONG i = 0; i < state->Profile.ChannelCount; ++i) {
        channels[i].ChannelNumber = state->Profile.Channels[i].Number;
        channels[i].ChannelCenterFrequency = (5000U + 5U * channels[i].ChannelNumber) * 1000U;
    }
    WIFI_BAND_INFO info = {WDI_BAND_ID_5000, TRUE, 1, &ofdm,
        state->Profile.ChannelCount, channels, 1, &width};
    WIFI_BAND_CAPABILITIES band;
    WIFI_BAND_CAPABILITIES_INIT(&band);
    band.NumBands = 1;
    band.BandInfoList = &info;
    status = WifiDeviceSetBandCapabilities(Device, &band);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    static const UINT16 rates[] = {12, 18, 24, 36, 48, 72, 96, 108};
    WIFI_PHY_INFO entry = {0};
    entry.PhyType = ofdm;
    entry.NumberDataRateEntries = RTL_NUMBER_OF(rates);
    for (size_t i = 0; i < RTL_NUMBER_OF(rates); ++i) {
        entry.DataRateList[i] = (WDI_DATA_RATE_ENTRY){WDI_DATA_RATE_RX_RATE | WDI_DATA_RATE_TX_RATE, rates[i]};
    }
    WIFI_PHY_CAPABILITIES phy;
    WIFI_PHY_CAPABILITIES_INIT(&phy);
    phy.NumPhyTypes = 1;
    phy.PhyInfoList = &entry;
    status = WifiDeviceSetPhyCapabilities(Device, &phy);
    state->CapsReported = NT_SUCCESS(status);
    return status;
}

static void ScheduleStationWork(STATION_DEVICE *State)
{
    /* Caller holds Gate. D0 exit can then flush every queued callback. */
    if (State->Running && !State->WorkQueued) {
        State->WorkQueued = true;
        WdfWorkItemEnqueue(State->Worker);
    }
}

static void WakeStation(void *Context)
{
    STATION_DEVICE *state = Context;
    KIRQL irql;
    KeAcquireSpinLock(&state->Gate, &irql);
    ScheduleStationWork(state);
    KeReleaseSpinLock(&state->Gate, irql);
}

void Mt7922WifiWakeSharedLink(WDFDEVICE Device)
{
    WakeStation(GetStationDevice(Device));
}

static void PollStation(WDFTIMER Timer)
{
    Mt7922WifiWakeSharedLink(WdfTimerGetParentObject(Timer));
}

static void SetAdapterMediaState(NETADAPTER Adapter, bool Associated)
{
    NET_ADAPTER_LINK_STATE link;
    if (Associated) {
        NET_ADAPTER_LINK_STATE_INIT(&link, NDIS_LINK_SPEED_UNKNOWN, MediaConnectStateConnected,
            MediaDuplexStateUnknown, NetAdapterPauseFunctionTypeUnsupported,
            NetAdapterAutoNegotiationFlagNone);
    } else {
        NET_ADAPTER_LINK_STATE_INIT_DISCONNECTED(&link);
    }
    NetAdapterSetLinkState(Adapter, &link);
}

static void SetStationMediaState(STATION_DEVICE *State, bool Associated)
{
    KIRQL irql;
    KeAcquireSpinLock(&State->Gate, &irql);
    NETADAPTER adapter = State->Adapter;
    if (adapter != NULL) {
        WdfObjectReference(adapter);
    }
    KeReleaseSpinLock(&State->Gate, irql);

    if (adapter != NULL) {
        SetAdapterMediaState(adapter, Associated);
        WdfObjectDereference(adapter);
    }
}

static NTSTATUS CreateStationAdapter(WDFDEVICE Device, NETADAPTER_INIT *Init)
{
    STATION_DEVICE *state = GetStationDevice(Device);
    if (WifiAdapterInitGetType(Init) != WIFI_ADAPTER_EXTENSIBLE_STATION) {
        return STATUS_NOT_SUPPORTED;
    }
    static const UCHAR zeroMac[6] = {0};
    if ((state->PermanentMac[0] & 1U) != 0 ||
        RtlCompareMemory(state->PermanentMac, zeroMac, sizeof(zeroMac)) == sizeof(zeroMac)) {
        return STATUS_DEVICE_NOT_READY;
    }

    Mt7922SetNetAdapterQueueCallbacks(Init);
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, STATION_ADAPTER);
    attributes.EvtCleanupCallback = CleanupStationAdapter;
    NETADAPTER adapter;
    NTSTATUS status = NetAdapterCreate(Init, &attributes, &adapter);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    GetStationAdapter(adapter)->Device = state;
    status = WifiAdapterInitialize(adapter);
    MT7922_NET_QUEUES *queues = NULL;
    if (NT_SUCCESS(status)) {
        status = Mt7922AttachNetAdapterQueues(adapter, &state->Station, WakeStation, state, &queues);
    }
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(adapter);
        return status;
    }

    /* Native 802.11 frames use NetAdapterCx-managed receive buffers. */
    NET_ADAPTER_TX_CAPABILITIES tx;
    NET_ADAPTER_RX_CAPABILITIES rx;
    NET_ADAPTER_TX_CAPABILITIES_INIT(&tx, 1);
    NET_ADAPTER_RX_CAPABILITIES_INIT_SYSTEM_MANAGED(&rx, MT7922_STATION_RAW_MAX, 1);
    NetAdapterSetDataPathCapabilities(adapter, &tx, &rx);
    NET_ADAPTER_LINK_LAYER_CAPABILITIES link;
    NET_ADAPTER_LINK_LAYER_CAPABILITIES_INIT(&link, 54000000ULL, 54000000ULL);
    NetAdapterSetLinkLayerCapabilities(adapter, &link);
    NetAdapterSetLinkLayerMtuSize(adapter, 1500);
    NET_ADAPTER_LINK_LAYER_ADDRESS mac;
    NET_ADAPTER_LINK_LAYER_ADDRESS_INIT(&mac, sizeof(state->PermanentMac), state->PermanentMac);
    NetAdapterSetPermanentLinkLayerAddress(adapter, &mac);
    NetAdapterSetCurrentLinkLayerAddress(adapter, &mac);
    const ULONG port = WifiAdapterGetPortId(adapter);
    KIRQL irql;
    KeAcquireSpinLock(&state->Gate, &irql);
    if (state->Adapter == NULL) {
        state->Adapter = adapter;
        state->Queues = queues;
        state->PortId = port;
    } else {
        status = STATUS_DEVICE_BUSY;
    }
    KeReleaseSpinLock(&state->Gate, irql);
    if (NT_SUCCESS(status)) {
        SetAdapterMediaState(adapter, false);
        status = NetAdapterStart(adapter);
    }
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(adapter);
    }
    return status;
}

static void CleanupStationAdapter(WDFOBJECT Adapter)
{
    STATION_DEVICE *state = GetStationAdapter(Adapter)->Device;
    KIRQL irql;
    KeAcquireSpinLock(&state->Gate, &irql);
    if (state->Adapter == Adapter) {
        state->Adapter = NULL;
        state->Queues = NULL;
    }
    KeReleaseSpinLock(&state->Gate, irql);
}

static NTSTATUS RejectWifiDirect(WDFDEVICE Device, WIFIDIRECT_DEVICE_INIT *Init)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Init);
    return STATUS_NOT_SUPPORTED;
}

static void CompleteStationRequest(WIFIREQUEST Request, NTSTATUS Status)
{
    UINT input;
    UINT output;
    WDI_MESSAGE_HEADER *header = WifiRequestGetInOutBuffer(Request, &input, &output);
    if (header == NULL || output < sizeof(*header)) {
        WifiRequestComplete(Request, Status, 0);
        return;
    }
    header->Status = Status;
    WifiRequestComplete(Request, Status, sizeof(*header));
}

static void DispatchStationRequest(WDFDEVICE Device, WIFIREQUEST Request)
{
    STATION_DEVICE *state = GetStationDevice(Device);
    const UINT16 message = WifiRequestGetMessageId(Request);
    const bool trace = (message == WDI_TASK_SET_RADIO_STATE || message == WDI_TASK_SCAN) &&
        Mt7922StationPacketTraceEnabled();
    WDI_MESSAGE_HEADER requestHeader = {0};
    if (trace) {
        UINT input, output;
        const WDI_MESSAGE_HEADER *header = WifiRequestGetInOutBuffer(Request, &input, &output);
        const bool validHeader = header != NULL && input >= sizeof(*header);
        if (validHeader) {
            requestHeader = *header;
        }
        Mt7922TraceStationControl("RequestDispatch", message, requestHeader.TransactionId,
            requestHeader.PortId, validHeader ? STATUS_PENDING : STATUS_INVALID_BUFFER_SIZE, 0, 0);
    }
    STATION_REQUEST *item = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*item), 'WctM');
    if (item == NULL) {
        CompleteStationRequest(Request, STATUS_INSUFFICIENT_RESOURCES);
        return;
    }
    item->Request = Request;
    KIRQL irql;
    KeAcquireSpinLock(&state->Gate, &irql);
    NTSTATUS status = !state->Running ? STATUS_DEVICE_NOT_READY :
        state->PendingRequests >= MAX_PENDING_REQUESTS ? STATUS_DEVICE_BUSY : STATUS_SUCCESS;
    if (NT_SUCCESS(status)) {
        InsertTailList(&state->Requests, &item->Entry);
        ++state->PendingRequests;
        ScheduleStationWork(state);
    }
    KeReleaseSpinLock(&state->Gate, irql);
    if (!NT_SUCCESS(status)) {
        if (trace) {
            Mt7922TraceStationControl("DispatchRejected", message, requestHeader.TransactionId,
                requestHeader.PortId, status, 0, 0);
        }
        ExFreePoolWithTag(item, 'WctM');
        CompleteStationRequest(Request, status);
    }
}

static NTSTATUS PublishRadioStatus(STATION_DEVICE *State, const WDI_MESSAGE_HEADER *RequestHeader)
{
    WDI_INDICATION_RADIO_STATUS_PARAMETERS parameters = {0};
    parameters.RadioState.HardwareState = TRUE;
    parameters.RadioState.SoftwareState = State->RadioEnabled;
    UINT8 *buffer;
    ULONG bytes;
    const NDIS_STATUS encoded = GenerateWdiIndicationRadioStatus(&parameters,
        sizeof(WDI_MESSAGE_HEADER), &State->Tlv, &bytes, &buffer);
    if (encoded != NDIS_STATUS_SUCCESS) {
        return encoded == NDIS_STATUS_RESOURCES ? STATUS_INSUFFICIENT_RESOURCES : STATUS_UNSUCCESSFUL;
    }
    WDI_MESSAGE_HEADER indication = *RequestHeader;
    indication.TransactionId = 0;
    indication.Status = STATUS_SUCCESS;
    RtlCopyMemory(buffer, &indication, sizeof(indication));
    const NTSTATUS status = Mt7922PublishWificxIndication(State->Device,
        WDI_INDICATION_RADIO_STATUS, buffer, bytes);
    FreeGenerated(buffer);
    return status;
}

static void HandleRadioState(STATION_DEVICE *State, WIFIREQUEST Request,
    const WDI_MESSAGE_HEADER *Header, const UCHAR *Tlv, ULONG Bytes)
{
    WDI_SET_RADIO_STATE_PARAMETERS parameters = {0};
    if (ParseWdiTaskSetRadioState(Bytes, Tlv, &State->Tlv, &parameters) != NDIS_STATUS_SUCCESS) {
        TraceRadioRequest("RadioParseRejected", Header, State, STATUS_INVALID_PARAMETER, Bytes);
        CompleteStationRequest(Request, STATUS_INVALID_PARAMETER);
        return;
    }
    const bool enable = parameters.SoftwareRadioState != 0;
    CleanupParsedWdiTaskSetRadioState(&parameters);
    TraceRadioRequest(enable ? "RadioOn" : "RadioOff", Header, State, STATUS_PENDING,
        State->Station.AssociationId);
    if (!enable && State->Station.AssociationId != 0) {
        /* WiFiCx disconnects its active peer before requesting radio off. */
        TraceRadioRequest("RadioOffRejected", Header, State, STATUS_DEVICE_BUSY, State->Station.AssociationId);
        CompleteStationRequest(Request, STATUS_DEVICE_BUSY);
        return;
    }
    WDI_MESSAGE_HEADER completion = *Header;
    CompleteStationRequest(Request, STATUS_SUCCESS);
    NTSTATUS status = STATUS_SUCCESS;
    if (enable && !State->Station.Ready) {
        status = StartStationRadio(State);
    } else if (!enable && State->Station.Scan.Rings != NULL) {
        Mt7922ExpireWdiScan(&State->Scan);
        status = Mt7922McuDisableStationMac(&State->Station);
        TraceRadioRequest("MacDisable", &completion, State, status, 0);
        if (NT_SUCCESS(status)) {
            status = Mt7922McuStopStation(&State->Station);
            TraceRadioRequest("McuStop", &completion, State, status, State->Station.Info.CleanupStatus);
        }
    }
    if (NT_SUCCESS(status) && State->RadioEnabled != enable) {
        State->RadioEnabled = enable;
        const NTSTATUS published = PublishRadioStatus(State, &completion);
        TraceRadioRequest("RadioStatus", &completion, State, published, enable ? 1U : 0U);
    }
    completion.Status = status;
    const NTSTATUS indicated = Mt7922PublishWificxIndication(State->Device,
        WDI_INDICATION_SET_RADIO_STATE_COMPLETE, (const uint8_t *)&completion, sizeof(completion));
    TraceRadioRequest("RadioComplete", &completion, State, status, (uint32_t)indicated);
}

static NTSTATUS CheckAdapterConfiguration(STATION_DEVICE *State, const UCHAR *Tlv, ULONG Bytes)
{
    WDI_SET_FIRMWARE_CONFIGURATION_PARAMETERS parameters = {0};
    if (ParseWdiSetAdapterConfiguration(Bytes, Tlv, &State->Tlv, &parameters) != NDIS_STATUS_SUCCESS) {
        return STATUS_INVALID_PARAMETER;
    }
    NTSTATUS status = STATUS_SUCCESS;
    if (parameters.Optional.MacAddress_IsPresent &&
        RtlCompareMemory(parameters.MacAddress.Address, State->PermanentMac, sizeof(State->PermanentMac)) !=
            sizeof(State->PermanentMac)) {
        status = STATUS_NOT_SUPPORTED;
    }
    if (parameters.Optional.DisallowedBandList_IsPresent) {
        for (UINT32 i = 0; i < parameters.DisallowedBandList.ElementCount; ++i) {
            if (parameters.DisallowedBandList.pElements[i] == WDI_BAND_ID_5000) {
                status = STATUS_NOT_SUPPORTED;
                break;
            }
        }
    }
    CleanupParsedWdiSetAdapterConfiguration(&parameters);
    return status;
}

static NTSTATUS AbortStationScan(STATION_DEVICE *State, const UCHAR *Tlv, ULONG Bytes)
{
    WDI_TASK_ABORT_PARAMETERS parameters = {0};
    if (ParseWdiAbortTask(Bytes, Tlv, &State->Tlv, &parameters) != NDIS_STATUS_SUCCESS) {
        return STATUS_INVALID_PARAMETER;
    }
    const WDI_CANCEL_PARAMETERS_CONTAINER *cancel = &parameters.CancelParameters;
    NTSTATUS status = cancel->OriginalTaskOID == OID_WDI_TASK_SCAN ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
    if (State->Scan.Active && cancel->OriginalTaskOID == OID_WDI_TASK_SCAN &&
        cancel->OriginalTransactionId == State->Scan.TransactionId && cancel->OriginalPortId == State->Scan.PortId) {
        status = State->Scan.Cancelling ? STATUS_SUCCESS : Mt7922CancelWdiScan(&State->Scan);
        if (NT_SUCCESS(status)) {
            State->ScanDeadline = KeQueryInterruptTime() + CANCEL_TIMEOUT_100NS;
        }
    }
    CleanupParsedWdiAbortTask(&parameters);
    return status;
}

typedef struct {
    WDFDEVICE Device;
    NTSTATUS Status;
} DISCONNECT_INDICATION;

static void PublishDisconnect(void *Context, uint16_t Message, const uint8_t *Buffer, size_t Bytes)
{
    DISCONNECT_INDICATION *indication = Context;
    indication->Status = Mt7922PublishWificxIndication(indication->Device, Message, Buffer, Bytes);
}

static NTSTATUS ReportStationDisconnect(STATION_DEVICE *State, uint16_t Port, const uint8_t Bssid[6],
    const uint8_t *PeerFrame, bool Reset)
{
    DISCONNECT_INDICATION indication = {State->Device, STATUS_SUCCESS};
    NTSTATUS status = Mt7922IndicateWdiDisconnect(&State->Tlv, Port, Bssid, PeerFrame,
        Reset, PublishDisconnect, &indication);
    return NT_SUCCESS(status) ? indication.Status : status;
}

static void DiscardStagedData(STATION_DEVICE *State)
{
    KIRQL irql;
    KeAcquireSpinLock(&State->Gate, &irql);
    if (State->Queues != NULL) {
        Mt7922DiscardStagedStationData(State->Queues);
    }
    KeReleaseSpinLock(&State->Gate, irql);
}

static void CompleteStationTask(STATION_DEVICE *State, uint16_t Message,
    const WDI_MESSAGE_HEADER *Header, NTSTATUS Status)
{
    WDI_MESSAGE_HEADER completion = *Header;
    completion.Status = Status;
    Mt7922PublishWificxIndication(State->Device, Message, (const uint8_t *)&completion, sizeof(completion));
}

static NTSTATUS DisconnectStation(STATION_DEVICE *State, uint16_t Port, uint16_t Reason)
{
    uint8_t peer[6];
    RtlCopyMemory(peer, State->Station.Ap.Bssid, sizeof(peer));
    uint8_t frame[MT7922_STATION_DISCONNECT_BYTES];
    RtlCopyMemory(frame, State->Station.PeerDisconnectFrame, sizeof(frame));
    DiscardStagedData(State);
    const bool wasAssociated = State->Station.WdiAssociationReported;
    NTSTATUS status = Mt7922McuDisconnectStation(&State->Station, Reason);
    if (wasAssociated && !State->Station.WdiAssociationReported) {
        SetStationMediaState(State, false);
    }
    if (State->HandshakeDebugNext != 0) {
        Mt7922SaveWificxConnectDebug(&State->Station);
        State->HandshakeDebugNext = 0;
    }
    if (NT_SUCCESS(status)) {
        status = ReportStationDisconnect(State, Port, peer, frame[0] != 0 ? frame : NULL, false);
    }
    return status;
}

static void HandleDisconnectRequest(STATION_DEVICE *State, WIFIREQUEST Request,
    const WDI_MESSAGE_HEADER *Header, const UCHAR *Tlv, ULONG Bytes)
{
    WDI_TASK_DISCONNECT_PARAMETERS parameters = {0};
    if (ParseWdiTaskDisconnect(Bytes, Tlv, &State->Tlv, &parameters) != NDIS_STATUS_SUCCESS) {
        CompleteStationRequest(Request, STATUS_INVALID_PARAMETER);
        return;
    }
    const WDI_DISCONNECT_PARAMETERS_CONTAINER *target = &parameters.DisconnectParameters;
    const bool associated = State->Station.AssociationId != 0;
    const bool matches = !associated || RtlCompareMemory(target->MacAddress.Address,
        State->Station.Ap.Bssid, sizeof(State->Station.Ap.Bssid)) == sizeof(State->Station.Ap.Bssid);
    const uint16_t reason = target->Disassociation80211Reason;
    CleanupParsedWdiTaskDisconnect(&parameters);
    if (!matches || reason == 0) {
        CompleteStationRequest(Request, STATUS_INVALID_PARAMETER);
        return;
    }

    const WDI_MESSAGE_HEADER original = *Header;
    CompleteStationRequest(Request, STATUS_SUCCESS);
    const NTSTATUS status = associated ? DisconnectStation(State, original.PortId, reason) : STATUS_SUCCESS;
    CompleteStationTask(State, WDI_INDICATION_DISCONNECT_COMPLETE, &original, status);
}

static void HandleResetRequest(STATION_DEVICE *State, WIFIREQUEST Request,
    const WDI_MESSAGE_HEADER *Header, const UCHAR *Tlv, ULONG Bytes)
{
    WDI_TASK_DOT11_RESET_PARAMETERS parameters = {0};
    if (ParseWdiTaskDot11Reset(Bytes, Tlv, &State->Tlv, &parameters) != NDIS_STATUS_SUCCESS) {
        CompleteStationRequest(Request, STATUS_INVALID_PARAMETER);
        return;
    }
    const bool unsupported = parameters.Optional.ResetMloLinkMACAddresses_IsPresent ||
        (parameters.Optional.ResetMACAddress_IsPresent &&
         RtlCompareMemory(parameters.ResetMACAddress.Address, State->PermanentMac, sizeof(State->PermanentMac)) !=
            sizeof(State->PermanentMac));
    const bool defaults = parameters.Dot11ResetParameters.SetDefaultMIB != 0;
    CleanupParsedWdiTaskDot11Reset(&parameters);
    if (unsupported) {
        CompleteStationRequest(Request, STATUS_NOT_SUPPORTED);
        return;
    }

    const WDI_MESSAGE_HEADER original = *Header;
    CompleteStationRequest(Request, STATUS_SUCCESS);
    DiscardStagedData(State);
    Mt7922ExpireWdiScan(&State->Scan);
    if (State->HandshakeDebugNext != 0) {
        Mt7922SaveWificxConnectDebug(&State->Station);
        State->HandshakeDebugNext = 0;
    }
    const bool associated = State->Station.AssociationId != 0;
    const MT7922_PRIVACY privacy = State->Station.EapolRxPrivacy;
    uint8_t peer[6];
    RtlCopyMemory(peer, State->Station.Ap.Bssid, sizeof(peer));
    const bool wasAssociated = State->Station.WdiAssociationReported;
    NTSTATUS status = Mt7922McuStopStation(&State->Station);
    if (wasAssociated) {
        SetStationMediaState(State, false);
    }
    if (NT_SUCCESS(status)) {
        if (associated) {
            ReportStationDisconnect(State, original.PortId, peer, NULL, true);
        }
        if (State->RadioEnabled) {
            status = StartStationRadio(State);
            if (NT_SUCCESS(status) && !defaults) {
                State->Station.EapolRxPrivacy = privacy;
            }
        }
    }
    CompleteStationTask(State, WDI_INDICATION_DOT11_RESET_COMPLETE, &original, status);
}

static void HandleStationRequest(STATION_DEVICE *State, WIFIREQUEST Request, bool Running)
{
    UINT input;
    UINT output;
    const WDI_MESSAGE_HEADER *header = WifiRequestGetInOutBuffer(Request, &input, &output);
    if (header == NULL || input < sizeof(*header) || output < sizeof(*header)) {
        CompleteStationRequest(Request, STATUS_INVALID_BUFFER_SIZE);
        return;
    }
    const UINT16 message = WifiRequestGetMessageId(Request);
    if (!Running) {
        if (message == WDI_TASK_SET_RADIO_STATE || message == WDI_TASK_SCAN) {
            Mt7922TraceStationControl("WorkerNotRunning", message, header->TransactionId,
                header->PortId, STATUS_CANCELLED, 0, 0);
        }
        CompleteStationRequest(Request, STATUS_CANCELLED);
        return;
    }

    const UCHAR *tlv = (const UCHAR *)header + sizeof(*header);
    const ULONG bytes = input - (ULONG)sizeof(*header);
    if (header->PortId == WDI_PORT_ID_ADAPTER) {
        if (message == WDI_TASK_SET_RADIO_STATE) {
            HandleRadioState(State, Request, header, tlv, bytes);
        } else {
            CompleteStationRequest(Request, message == WDI_SET_ADAPTER_CONFIGURATION
                ? CheckAdapterConfiguration(State, tlv, bytes) : STATUS_NOT_SUPPORTED);
        }
        return;
    }

    KIRQL irql;
    KeAcquireSpinLock(&State->Gate, &irql);
    const bool validPort = State->Adapter != NULL && State->PortId == header->PortId;
    KeReleaseSpinLock(&State->Gate, irql);
    if (validPort && message == WDI_TASK_DOT11_RESET) {
        HandleResetRequest(State, Request, header, tlv, bytes);
        return;
    }
    if (!validPort || !State->Station.Ready || !State->RadioEnabled) {
        if (message == WDI_TASK_SCAN) {
            Mt7922TraceStationControl("ScanRejected", message, header->TransactionId,
                header->PortId, STATUS_DEVICE_NOT_READY, StationControlFlags(State, Running, validPort),
                State->Station.AssociationId);
        }
        CompleteStationRequest(Request, STATUS_DEVICE_NOT_READY);
        return;
    }

    switch (message) {
    case WDI_TASK_SCAN:
        if (!State->Scan.Active) {
            State->ScanDeadline = KeQueryInterruptTime() + SCAN_TIMEOUT_100NS;
        }
        Mt7922HandleWificxScan(&State->Scan, Request, 0, 0);
        break;
    case WDI_ABORT_TASK:
        CompleteStationRequest(Request, AbortStationScan(State, tlv, bytes));
        break;
    case WDI_TASK_CONNECT: {
        const uint32_t sequence = State->Station.ConnectDebug.Sequence;
        Mt7922HandleWificxConnect(&State->Station, State->Device, Request, &State->Tlv,
            &State->Scan, STATION_PEER_WCID, NULL, NULL);
        if (State->Station.WdiAssociationReported && State->Station.ConnectDebug.Sequence != sequence &&
            NT_SUCCESS(State->Station.ConnectDebug.PlanStatus)) {
            /* Make the datapath available to the host for WPA2 before servicing its queues. */
            SetStationMediaState(State, true);
            State->HandshakeDebugNext = KeQueryInterruptTime() + 10000000ULL;
            State->HandshakeDebugDeadline = State->HandshakeDebugNext + 110000000ULL;
        }
        break;
    }
    case WDI_TASK_DISCONNECT:
        HandleDisconnectRequest(State, Request, header, tlv, bytes);
        break;
    case WDI_SET_ADD_CIPHER_KEYS:
        Mt7922HandleWificxCcmpKeys(&State->Station, Request, &State->Tlv);
        if (State->Station.ObserverDeadline != 0) {
            State->HandshakeDebugNext = KeQueryInterruptTime() + 10000000ULL;
            State->HandshakeDebugDeadline = State->Station.ObserverDeadline;
        }
        break;
    case WDI_SET_PRIVACY_EXEMPTION_LIST:
        Mt7922HandleWificxEapolPrivacy(&State->Station, Request, &State->Tlv);
        break;
    default:
        CompleteStationRequest(Request, STATUS_NOT_SUPPORTED);
        break;
    }
}

static void UpdateFastRadioPolling(STATION_DEVICE *State)
{
    MT7922_AWDL_LINK *link = State->Station.SharedLink;
    const bool awdlActive = link != NULL && (link->AnnounceMdns ||
        InterlockedCompareExchange(&link->SharedUploadActive, 0, 0) != 0);
    const bool fastPoll = awdlActive || Mt7922McuStationDataTxActive(&State->Station);
    if (fastPoll != State->FastPollActive) {
        if (fastPoll) {
            WdfTimerStart(State->FastPollTimer,
                WDF_REL_TIMEOUT_IN_MS(FAST_POLL_MILLISECONDS));
        } else {
            WdfTimerStop(State->FastPollTimer, FALSE);
        }
        State->FastPollActive = fastPoll;
    }
    if (awdlActive) {
        ULONGLONG counter;
        const ULONGLONG now = KeQueryInterruptTimePrecise(&counter);
        if (State->SharedReceiveLastPoll != 0) {
            ++link->UploadFastPolls;
            link->UploadLatePolls += now - State->SharedReceiveLastPoll >= 40000ULL;
        }
        State->SharedReceiveLastPoll = now;
    } else {
        State->SharedReceiveLastPoll = 0;
    }
}

static void UpdateCoexistenceDebug(STATION_DEVICE *State, ULONGLONG Now)
{
    MT7922_MCU_STATION *station = &State->Station;
    const bool attached = station->SharedLink != NULL;
    const bool wasAttached = (station->ConnectDebug.Coexistence.Flags & MT7922_COEX_AWDL_ATTACHED) != 0;
    if (attached != wasAttached) {
        State->CoexistenceDebugUntil = attached ? 0 : Now + 75ULL * 10000000ULL;
    } else if ((!station->ConnectDebug.StationIo.Active && !attached && Now > State->CoexistenceDebugUntil) ||
        Now < State->CoexistenceDebugNext) {
        return;
    }
    if (station->ConnectDebug.StationIo.Active && Now >= station->StationIoTraceDeadline) {
        station->ConnectDebug.StationIo.Active = 0;
    }
    Mt7922McuSnapshotCoexistence(station);
    Mt7922SaveWificxConnectDebug(station);
    State->CoexistenceDebugNext = Now + 10000000ULL;
}

static void RunStationWorker(WDFWORKITEM Worker)
{
    STATION_DEVICE *state = GetStationDevice(WdfWorkItemGetParentObject(Worker));
    bool polled = false;
    for (;;) {
        KIRQL irql;
        KeAcquireSpinLock(&state->Gate, &irql);
        const bool running = state->Running;
        if (!IsListEmpty(&state->Requests)) {
            STATION_REQUEST *item = CONTAINING_RECORD(RemoveHeadList(&state->Requests), STATION_REQUEST, Entry);
            --state->PendingRequests;
            KeReleaseSpinLock(&state->Gate, irql);
            HandleStationRequest(state, item->Request, running);
            ExFreePoolWithTag(item, 'WctM');
        } else if (running && !polled) {
            NETADAPTER adapter = state->Adapter;
            MT7922_NET_QUEUES *queues = state->Queues;
            if (adapter != NULL) {
                WdfObjectReference(adapter);
            }
            KeReleaseSpinLock(&state->Gate, irql);
            if (state->Scan.Active && KeQueryInterruptTime() >= state->ScanDeadline) {
                Mt7922ExpireWdiScan(&state->Scan);
            }
            Mt7922WifiServiceSharedLink(state->Device, &state->Station, queues != NULL);
            if (state->Station.Ready) {
                if (queues != NULL && state->Station.WdiAssociationReported) {
                    const MT7922_SHARED_ADVERT_DEBUG previousAdvert = state->Station.ConnectDebug.SharedAdvert;
                    const MT7922_SHARED_ADVERT_DEBUG previousEcho = state->Station.ConnectDebug.SharedEcho;
                    const uint32_t previousEchoReplies = state->Station.ConnectDebug.SharedEchoReplies;
                    const ULONGLONG now = KeQueryInterruptTime();
                    if (state->Station.FirmwarePortSecured && now >= state->SharedAdvertRequestPoll) {
                        Mt7922PollWificxSharedAdvertRequest(&state->Station);
                        state->SharedAdvertRequestPoll = now + 10000000ULL;
                    }
                    Mt7922ServiceNetAdapterData(queues);
                    UpdateCoexistenceDebug(state, now);
                    if (RtlCompareMemory(&previousAdvert, &state->Station.ConnectDebug.SharedAdvert,
                            sizeof(previousAdvert)) != sizeof(previousAdvert) ||
                        RtlCompareMemory(&previousEcho, &state->Station.ConnectDebug.SharedEcho,
                            sizeof(previousEcho)) != sizeof(previousEcho) ||
                        previousEchoReplies != state->Station.ConnectDebug.SharedEchoReplies) {
                        Mt7922SaveWificxConnectDebug(&state->Station);
                    }
                    if (state->HandshakeDebugNext != 0 && now >= state->HandshakeDebugNext) {
                        Mt7922SaveWificxConnectDebug(&state->Station);
                        state->HandshakeDebugNext = now >= state->HandshakeDebugDeadline ? 0 : now + 10000000ULL;
                    }
                    if (state->Station.PeerDisconnectFrame[0] != 0) {
                        DisconnectStation(state, state->Station.WdiPortId, 0);
                    }
                } else {
                    Mt7922McuPollStation(&state->Station, NULL, NULL);
                }
            }
            UpdateFastRadioPolling(state);
            if (adapter != NULL) {
                WdfObjectDereference(adapter);
            }
            polled = true;
        } else {
            state->WorkQueued = false;
            KeReleaseSpinLock(&state->Gate, irql);
            return;
        }
    }
}

static void QuiesceStationWorker(STATION_DEVICE *State)
{
    KIRQL irql;
    KeAcquireSpinLock(&State->Gate, &irql);
    State->Running = false;
    KeReleaseSpinLock(&State->Gate, irql);
    WdfTimerStop(State->Timer, TRUE);
    WdfWorkItemFlush(State->Worker);
    WdfTimerStop(State->FastPollTimer, TRUE);
    State->FastPollActive = false;
    State->SharedReceiveLastPoll = 0;
    Mt7922WifiServiceSharedLink(State->Device, &State->Station, false);
    if (State->HandshakeDebugNext != 0) {
        Mt7922SaveWificxConnectDebug(&State->Station);
        State->HandshakeDebugNext = 0;
    }
}

NTSTATUS Mt7922WifiLeaveD0(WDFDEVICE Device)
{
    STATION_DEVICE *state = GetStationDevice(Device);
    QuiesceStationWorker(state);
    Mt7922ExpireWdiScan(&state->Scan);
    if (state->Station.WdiAssociationReported) {
        SetStationMediaState(state, false);
    }
    return Mt7922McuStopStation(&state->Station);
}

void Mt7922WifiReleaseDevice(WDFDEVICE Device)
{
    STATION_DEVICE *state = GetStationDevice(Device);
    QuiesceStationWorker(state);
    Mt7922McuReleaseStation(&state->Station);
    RtlZeroMemory(&state->Scan, sizeof(state->Scan));
    state->RadioEnabled = true;
    state->Bar = NULL;
    state->Dma = NULL;
    state->CapsReported = false;
    if (state->Firmware != NULL) {
        WdfObjectDelete(state->Firmware);
        state->Firmware = NULL;
    }
    if (state->Clc != NULL) {
        WdfObjectDelete(state->Clc);
        state->Clc = NULL;
    }
}
