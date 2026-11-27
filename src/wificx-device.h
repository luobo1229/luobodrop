#pragma once

/* KMDF lifecycle entry points for the separate WiFiCx build. */
NTSTATUS Mt7922WifiConfigureDeviceInit(PWDFDEVICE_INIT DeviceInit);
NTSTATUS Mt7922WifiInitializeDevice(WDFDEVICE Device);
NTSTATUS Mt7922WifiPrepareDevice(WDFDEVICE Device, PUCHAR Bar, WDFDMAENABLER Dma,
    ULONG ChipId);
NTSTATUS Mt7922WifiEnterD0(WDFDEVICE Device);
EVT_WDF_DEVICE_D0_ENTRY_POST_INTERRUPTS_ENABLED Mt7922WifiReportCapabilities;
NTSTATUS Mt7922WifiLeaveD0(WDFDEVICE Device);
void Mt7922WifiReleaseDevice(WDFDEVICE Device);
void Mt7922WifiWakeSharedLink(WDFDEVICE Device);
void Mt7922WifiServiceSharedLink(WDFDEVICE Device, MT7922_MCU_STATION *Station, bool Running);
