/* SPDX-License-Identifier: BSD-3-Clause-Clear */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MT7922_STATION_DEVICE_COMMAND 0x01U
#define MT7922_STATION_BSS_COMMAND 0x02U
#define MT7922_STATION_DEVICE_BYTES 16U
#define MT7922_STATION_IDLE_BSS_BYTES 36U
#define MT7922_STATION_JOIN_BSS_BYTES 44U
#define MT7922_STATION_RLM_BYTES 20U
#define MT7922_STATION_PRIMARY_80 149U
#define MT7922_STATION_CENTER_80 155U

typedef struct {
    uint8_t Bssid[6];
    uint8_t Channel;
    uint8_t Band; /* 1 = 2.4 GHz, 2 = 5 GHz. */
    uint8_t DtimPeriod; /* Zero until a valid TIM element is available. */
    uint16_t BeaconInterval;
    bool Qos;
    uint8_t Center80; /* Zero selects the ordinary 20 MHz path. */
} MT7922_STATION_AP;

bool Mt7922BuildStationPort(const uint8_t Mac[6], uint8_t Port, uint16_t MulticastWcid,
    uint8_t Device[MT7922_STATION_DEVICE_BYTES], uint8_t Bss[MT7922_STATION_IDLE_BSS_BYTES]);
bool Mt7922BuildStationAp(const MT7922_STATION_AP *Ap, uint8_t Port, uint16_t MulticastWcid,
    uint8_t ChainMask, uint8_t Bss[MT7922_STATION_JOIN_BSS_BYTES],
    uint8_t Rlm[MT7922_STATION_RLM_BYTES]);

/* The lifecycle supplies effective limits after applying region, board and SAR policy. */
#define MT7922_STATION_RADIO_CHANNELS 9U
#define MT7922_STATION_DOMAIN_HEADER 12U
#define MT7922_STATION_DOMAIN_ENTRY 8U
#define MT7922_STATION_POWER_HEADER 44U
#define MT7922_STATION_POWER_ENTRY 162U
#define MT7922_STATION_POWER_BATCH 8U
#define MT7922_STATION_POWER_BYTES (MT7922_STATION_POWER_HEADER + MT7922_STATION_POWER_ENTRY * MT7922_STATION_POWER_BATCH)
#define MT7922_STATION_CHANNEL_BYTES 76U
#define MT7925_STATION_DOMAIN_HEADER 16U
#define MT7925_STATION_DOMAIN_ENTRY 8U
#define MT7925_STATION_POWER_HEADER 52U
#define MT7925_STATION_POWER_ENTRY 450U
#define MT7925_STATION_POWER_BATCH 3U
#define MT7925_STATION_POWER_BYTES (MT7925_STATION_POWER_HEADER + MT7925_STATION_POWER_ENTRY * MT7925_STATION_POWER_BATCH)

typedef struct {
    uint8_t Number;
    int8_t LimitHalfDbm;
} MT7922_STATION_RADIO_CHANNEL;

typedef struct {
    uint8_t Country[2];
    uint8_t ChannelCount;
    MT7922_STATION_RADIO_CHANNEL Channels[MT7922_STATION_RADIO_CHANNELS];
} MT7922_STATION_RADIO_PROFILE;

bool Mt7922ValidateStationRadio(const MT7922_STATION_RADIO_PROFILE *Profile);
bool Mt7922StationRadioAllows(const MT7922_STATION_RADIO_PROFILE *Profile, uint8_t Channel);
bool Mt7922StationRadioAllows80(const MT7922_STATION_RADIO_PROFILE *Profile);
size_t Mt7922BuildStationDomain(uint8_t *Command, const MT7922_STATION_RADIO_PROFILE *Profile);
size_t Mt7925BuildStationDomain(uint8_t *Command, const MT7922_STATION_RADIO_PROFILE *Profile);
size_t Mt7925BuildStationPower(uint8_t *Command, const MT7922_STATION_RADIO_PROFILE *Profile, size_t First);
size_t Mt7922BuildStationPower(uint8_t *Command, const MT7922_STATION_RADIO_PROFILE *Profile, size_t First);
void Mt7922BuildStationChannel(uint8_t Command[MT7922_STATION_CHANNEL_BYTES], uint8_t Channel,
    uint8_t Center80, uint8_t Chains, bool ReceivePath);

#define MT7922_STATION_ASSOC_IE_MAX 512U
#define MT7922_STATION_MGMT_MAX (28U + MT7922_STATION_ASSOC_IE_MAX)

/* Diagnostic capture sizes for the ConnectDebug registry value. */
#define MT7922_CONNECT_DEBUG_BODY_SAMPLE 192U
#define MT7922_CONNECT_DEBUG_VENDOR_SAMPLE 64U

typedef struct {
    const uint8_t *InformationElements; /* Association request IEs for the selected network. */
    size_t InformationElementBytes;
    uint16_t Capability;
    uint16_t ListenInterval;
    uint16_t Wcid; /* Preallocated station peer index. */
} MT7922_STATION_JOIN_REQUEST;

typedef struct {
    uint16_t StatusCode;
    uint16_t AssociationId;
    uint16_t BodyBytes;
    uint8_t Body[2024]; /* RX DMA record is at most 2048 bytes. */
} MT7922_STATION_MGMT_REPLY;

typedef struct {
    MT7922_STATION_MGMT_REPLY Authentication;
    MT7922_STATION_MGMT_REPLY Association;
    bool AssociationStarted;
    uint16_t RequestBodyBytes;
    uint8_t RequestBody[MT7922_STATION_MGMT_MAX - 24U];
} MT7922_STATION_JOIN_RESULT;

#define MT7922_STATION_DISCONNECT_BYTES 26U
size_t Mt7922BuildStationDisconnect(uint8_t Frame[MT7922_STATION_DISCONNECT_BYTES],
    const uint8_t Local[6], const uint8_t Bssid[6], uint16_t Sequence, uint16_t Reason);
bool Mt7922ReadStationDisconnect(const uint8_t *Frame, size_t Bytes,
    const uint8_t Local[6], const uint8_t Bssid[6]);

#define MT7922_CCMP_KEY_BYTES 16U
#define MT7922_CCMP_COMMAND_BYTES 88U /* Full sta_rec_sec storage, as in mt76. */

/* A null Key removes the key from the selected WCID. */
bool Mt7922BuildStationCcmp(uint8_t Command[MT7922_CCMP_COMMAND_BYTES], uint8_t Wcid,
    uint8_t KeyId, bool Group, const uint8_t Key[MT7922_CCMP_KEY_BYTES]);

size_t Mt7922BuildStationManagement(uint8_t *Frame, size_t Capacity,
    const uint8_t Local[6], const uint8_t Bssid[6], uint16_t Sequence,
    const MT7922_STATION_JOIN_REQUEST *Association);
bool Mt7922ReadStationManagement(const uint8_t *Frame, size_t Bytes,
    const uint8_t Local[6], const uint8_t Bssid[6], bool Association,
    MT7922_STATION_MGMT_REPLY *Reply);

/* Mirrors the WDK WDI_EXEMPTION_ACTION_TYPE settings. */
typedef enum {
    Mt7922PrivacyRequireKey = 0,
    Mt7922PrivacyAlwaysClear = 1,
    Mt7922PrivacyClearUntilKey = 2
} MT7922_PRIVACY;

#define MT7922_STATION_ETHERNET_MAX 1514U
#define MT7922_STATION_RAW_MAX (MT7922_STATION_ETHERNET_MAX + 20U)

bool Mt7922StationIsEapol(const uint8_t *Ethernet); /* Ethernet header already validated. */
size_t Mt7922EncapsulateStationData(uint8_t *Frame, size_t Capacity,
    const uint8_t *Ethernet, size_t Bytes, const uint8_t Local[6], const uint8_t Bssid[6],
    uint16_t Sequence, bool Protected);
size_t Mt7922ValidateStationRxData(const uint8_t *Frame, size_t Bytes,
    const uint8_t Local[6], const uint8_t Bssid[6], bool *Eapol);
size_t Mt7922ConvertStationTxData(uint8_t *Frame, size_t Bytes,
    const uint8_t Local[6], const uint8_t Bssid[6]);

#ifdef MT7922_STATION_PACKET_TRACE
typedef struct {
    uint32_t SourceIpv4;
    uint32_t DestinationIpv4;
    uint32_t Sequence;
    uint32_t Acknowledgment;
    uint16_t SourcePort;
    uint16_t DestinationPort;
    uint16_t Ipv4Id;
    uint16_t PayloadBytes;
    uint16_t WlanSequence;
    uint8_t Flags;
} MT7922_STATION_TCP_HEADER;

bool Mt7922ReadStationTcpHeader(const uint8_t *Frame, size_t Bytes, bool Receive,
    MT7922_STATION_TCP_HEADER *Packet);
#endif
