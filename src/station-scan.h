/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Firmware format adapted from mt76_connac_mcu.h, Copyright (C) 2020 MediaTek Inc. */
#ifndef MT7922_STATION_SCAN_H
#define MT7922_STATION_SCAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "station-port.h"

#define MT7922_SCAN_START_COMMAND 0x03U
#define MT7922_SCAN_CANCEL_COMMAND 0x1bU
#define MT7922_SCAN_DONE_EVENT 0x0dU
#define MT7925_SCAN_DONE_EVENT 0x0eU
#define MT7922_SCAN_REQUEST_BYTES 1186U
#define MT7922_SCAN_CANCEL_BYTES 4U
#define MT7922_SCAN_TX_HEADER_BYTES 64U
#define MT7922_SCAN_TX_BYTES (MT7922_SCAN_TX_HEADER_BYTES + MT7922_SCAN_REQUEST_BYTES)
#define MT7922_SCAN_MAX_CHANNELS 64U
#define MT7922_SCAN_MAX_SSIDS 4U
#define MT7922_SCAN_MAX_IE_BYTES 600U
#define MT7922_SCAN_MAX_SSID_BYTES 32U
#define MT7925_SCAN_REQUEST_MAX_BYTES 1164U
#define MT7925_SCAN_CANCEL_BYTES 12U

typedef struct {
    uint8_t Band; /* 1 = 2.4 GHz, 2 = 5 GHz, 3 = 6 GHz. */
    uint8_t Number;
} MT7922_SCAN_CHANNEL;

typedef struct {
    uint8_t Length;
    uint8_t Data[MT7922_SCAN_MAX_SSID_BYTES];
} MT7922_SCAN_SSID;

typedef struct {
    uint8_t Sequence; /* Seven-bit firmware scan ID. */
    uint8_t BandIndex;
    uint8_t BssIndex;
    bool Active;
    const MT7922_SCAN_SSID *Ssids;
    size_t SsidCount;
    const MT7922_SCAN_CHANNEL *Channels;
    size_t ChannelCount;
    const uint8_t *InformationElements;
    size_t InformationElementBytes;
    const uint8_t *Bssid; /* NULL means broadcast. */
    uint16_t DwellMilliseconds;
    uint16_t TimeoutMilliseconds;
} MT7922_SCAN_PARAMETERS;

bool Mt7922BuildScanRequest(uint8_t *Buffer, size_t Bytes, const MT7922_SCAN_PARAMETERS *Parameters);
size_t Mt7925BuildScanRequest(uint8_t *Buffer, size_t Capacity,
    const uint8_t *Connac2Request, size_t RequestBytes);
size_t Mt7925BuildScanCancel(uint8_t *Buffer, size_t Capacity, uint8_t Sequence, uint8_t BssIndex);
bool Mt7922LimitStationScan(uint8_t *Buffer, const uint8_t *Input,
    const MT7922_STATION_RADIO_PROFILE *Radio);
bool Mt7922BuildScanCancel(uint8_t *Buffer, size_t Bytes, uint8_t Sequence);
bool Mt7922IsScanDone(uint8_t Event, const uint8_t *Payload, size_t Bytes,
    uint8_t Sequence, uint8_t BandIndex);

/* Legacy Connac2 CE SET/QUERY and extended commands share one envelope. */
size_t Mt7922FrameLegacyMcuCommand(uint8_t *Buffer, size_t Bytes, uint8_t Command,
    uint8_t Extension, uint8_t SetQuery, uint8_t Sequence, const uint8_t *Payload, size_t PayloadBytes);

/* Scan commands use the legacy Connac2 envelope with the CE SET flag. */
size_t Mt7922FrameScanCommand(uint8_t *Buffer, size_t Bytes, uint8_t Command,
    uint8_t McuSequence, const uint8_t *Payload, size_t PayloadBytes);
bool Mt7922MatchScanRecord(const uint8_t *Record, size_t Bytes,
    uint8_t Sequence, uint8_t BandIndex);
bool Mt792xMatchScanRecord(uint16_t ChipId, const uint8_t *Record, size_t Bytes,
    uint8_t Sequence, uint8_t BandIndex);

#endif

