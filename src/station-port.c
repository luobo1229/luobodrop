/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* MediaTek Connac2 device, basic BSS, QBSS and RLM layouts from mt76_connac_mcu.c. */
#include "station-port.h"
#include <string.h>

#define FIRST_VIRTUAL_PORT_COUNT 4U
#define MAX_WCID 255U

static bool IsUnicast(const uint8_t Mac[6])
{
    static const uint8_t zero[6] = {0};
    return (Mac[0] & 1U) == 0 && memcmp(Mac, zero, sizeof(zero)) != 0;
}

static void SetLe16(uint8_t *Field, uint16_t Value)
{
    Field[0] = (uint8_t)Value;
    Field[1] = (uint8_t)(Value >> 8);
}

static void SetBssBasic(uint8_t *Bss, uint8_t Port, uint16_t MulticastWcid)
{
    Bss[0] = Port;
    Bss[6] = 32; /* UNI_BSS_INFO_BASIC TLV length. */
    Bss[8] = 1;
    Bss[9] = Port;
    Bss[10] = Port;
    Bss[12] = 1;
    Bss[14] = 1; /* CONNECTION_INFRA_STA (0x00010001). */
    Bss[16] = 1; /* Disconnected. The connection goes live after association. */
    Bss[17] = Port; /* mt7921 assigns WMM index from the VIF index. */
    SetLe16(Bss + 24, MulticastWcid);
    SetLe16(Bss + 30, MulticastWcid);
}

bool Mt7922BuildStationPort(const uint8_t Mac[6], uint8_t Port, uint16_t MulticastWcid,
    uint8_t Device[MT7922_STATION_DEVICE_BYTES], uint8_t Bss[MT7922_STATION_IDLE_BSS_BYTES])
{
    if (!IsUnicast(Mac) || Port >= FIRST_VIRTUAL_PORT_COUNT || MulticastWcid > MAX_WCID) {
        return false;
    }
    memset(Device, 0, MT7922_STATION_DEVICE_BYTES);
    Device[0] = Port;
    Device[6] = 12; /* DEV_INFO_ACTIVE TLV length. */
    Device[8] = 1;
    memcpy(Device + 10, Mac, 6);

    memset(Bss, 0, MT7922_STATION_IDLE_BSS_BYTES);
    SetBssBasic(Bss, Port, MulticastWcid);
    return true;
}

bool Mt7922BuildStationAp(const MT7922_STATION_AP *Ap, uint8_t Port, uint16_t MulticastWcid,
    uint8_t ChainMask, uint8_t Bss[MT7922_STATION_JOIN_BSS_BYTES],
    uint8_t Rlm[MT7922_STATION_RLM_BYTES])
{
    if (Ap == NULL || !IsUnicast(Ap->Bssid) || Ap->BeaconInterval == 0 ||
        Ap->Channel == 0 || (Ap->Band == 1 && Ap->Channel > 14) ||
        (Ap->Band == 2 && (Ap->Channel <= 14 || Ap->Channel > 177)) ||
        (Ap->Band != 1 && Ap->Band != 2) || Port >= FIRST_VIRTUAL_PORT_COUNT ||
        MulticastWcid > MAX_WCID || ChainMask == 0 || ChainMask > 3 ||
        (Ap->Center80 != 0 && (Ap->Band != 2 || Ap->Channel != MT7922_STATION_PRIMARY_80 ||
            Ap->Center80 != MT7922_STATION_CENTER_80))) {
        return false;
    }
    memset(Bss, 0, MT7922_STATION_JOIN_BSS_BYTES);
    SetBssBasic(Bss, Port, MulticastWcid);
    memcpy(Bss + 18, Ap->Bssid, sizeof(Ap->Bssid));
    SetLe16(Bss + 26, Ap->BeaconInterval);
    Bss[28] = Ap->DtimPeriod;
    /* Keep the infrastructure peer on the established legacy data path. */
    const uint8_t nonHtPhy = Ap->Band == 1 ? 6 : 1;
    Bss[29] = nonHtPhy;
    Bss[32] = Ap->Band == 1 ? 3 : 8; /* HR/DSSS + ERP or OFDM in PHY_TYPE_BIT_* format. */
    Bss[36] = 15; /* UNI_BSS_INFO_QBSS */
    Bss[38] = 8;
    Bss[40] = Ap->Qos;

    memset(Rlm, 0, MT7922_STATION_RLM_BYTES);
    Rlm[0] = Port;
    Rlm[4] = 2; /* UNI_BSS_INFO_RLM */
    Rlm[6] = 16;
    Rlm[8] = Ap->Channel;
    Rlm[9] = Ap->Center80 != 0 ? Ap->Center80 : Ap->Channel;
    Rlm[11] = Ap->Center80 != 0 ? 2 : 0; /* CMD_CBW_80MHZ / CMD_CBW_20MHZ. */
    Rlm[12] = ChainMask == 3 ? 2 : 1; /* TX streams, popcount of the antenna mask. */
    Rlm[13] = ChainMask;
    Rlm[14] = 1; /* Short slot time. */
    Rlm[15] = Ap->Center80 != 0 ? 4 : 0; /* HT 40 MHz allowed. */
    Rlm[16] = Ap->Center80 != 0 ? 1 : 0; /* Secondary channel above primary. */
    Rlm[17] = (uint8_t)(Ap->Band - 1);
    return true;
}

/* Follow the 802.11 management body layouts used by net80211. Authentication
   is open-system only; the Windows supplicant handles credentials and keys. */
static bool CheckAssociationElements(const uint8_t *Elements, size_t Bytes)
{
    bool ssid = false;
    bool rates = false;
    if (Elements == NULL || Bytes > MT7922_STATION_ASSOC_IE_MAX) {
        return false;
    }
    for (size_t offset = 0; offset < Bytes;) {
        if (Bytes - offset < 2 || Elements[offset + 1] > Bytes - offset - 2) {
            return false;
        }
        const uint8_t length = Elements[offset + 1];
        if (Elements[offset] == 0) {
            if (ssid || length == 0 || length > 32) {
                return false;
            }
            ssid = true;
        } else if (Elements[offset] == 1) {
            if (rates || length == 0 || length > 8) {
                return false;
            }
            rates = true;
        }
        offset += 2U + length;
    }
    return ssid && rates;
}

static void WriteStationManagementHeader(uint8_t *Frame,
    const uint8_t Local[6], const uint8_t Bssid[6], uint16_t Sequence)
{
    memcpy(Frame + 4, Bssid, 6);
    memcpy(Frame + 10, Local, 6);
    memcpy(Frame + 16, Bssid, 6);
    SetLe16(Frame + 22, (uint16_t)(Sequence << 4));
}

size_t Mt7922BuildStationManagement(uint8_t *Frame, size_t Capacity,
    const uint8_t Local[6], const uint8_t Bssid[6], uint16_t Sequence,
    const MT7922_STATION_JOIN_REQUEST *Association)
{
    if (!IsUnicast(Local) || !IsUnicast(Bssid) || memcmp(Local, Bssid, 6) == 0 || Sequence > 4095) {
        return 0;
    }
    size_t bytes = 30;
    if (Association != NULL) {
        if ((Association->Capability & 3U) != 1 || Association->ListenInterval == 0 ||
            !CheckAssociationElements(Association->InformationElements, Association->InformationElementBytes)) {
            return 0;
        }
        bytes = 28 + Association->InformationElementBytes;
    }
    if (Capacity < bytes) {
        return 0;
    }

    memset(Frame, 0, bytes);
    WriteStationManagementHeader(Frame, Local, Bssid, Sequence);
    if (Association == NULL) {
        Frame[0] = 0xb0;
        Frame[26] = 1; /* Open system, transaction 1, status success. */
    } else {
        SetLe16(Frame + 24, Association->Capability);
        SetLe16(Frame + 26, Association->ListenInterval);
        memcpy(Frame + 28, Association->InformationElements, Association->InformationElementBytes);
    }
    return bytes;
}

bool Mt7922ReadStationManagement(const uint8_t *Frame, size_t Bytes,
    const uint8_t Local[6], const uint8_t Bssid[6], bool Association,
    MT7922_STATION_MGMT_REPLY *Reply)
{
    if (Bytes < 30 || Bytes - 24 > sizeof(Reply->Body) || Frame[0] != (Association ? 0x10 : 0xb0) ||
        (Frame[1] & 0xc7U) != 0 || (Frame[22] & 0xfU) != 0 ||
        memcmp(Frame + 4, Local, 6) != 0 || memcmp(Frame + 10, Bssid, 6) != 0 ||
        memcmp(Frame + 16, Bssid, 6) != 0) {
        return false;
    }
    uint16_t code;
    uint16_t associationId = 0;
    if (!Association) {
        if (Frame[24] != 0 || Frame[25] != 0 || Frame[26] != 2 || Frame[27] != 0) {
            return false;
        }
        code = (uint16_t)(Frame[28] | ((uint16_t)Frame[29] << 8));
    } else {
        code = (uint16_t)(Frame[26] | ((uint16_t)Frame[27] << 8));
        if (code == 0) {
            const uint16_t aid = (uint16_t)(Frame[28] | ((uint16_t)Frame[29] << 8));
            associationId = aid & 0x3fffU;
            if ((aid & 0xc000U) != 0xc000U || associationId == 0 || associationId > 2007) {
                return false;
            }
        }
    }
    Reply->StatusCode = code;
    Reply->AssociationId = associationId;
    Reply->BodyBytes = (uint16_t)(Bytes - 24);
    memcpy(Reply->Body, Frame + 24, Reply->BodyBytes);
    return true;
}

size_t Mt7922BuildStationDisconnect(uint8_t Frame[MT7922_STATION_DISCONNECT_BYTES],
    const uint8_t Local[6], const uint8_t Bssid[6], uint16_t Sequence, uint16_t Reason)
{
    if (!IsUnicast(Local) || !IsUnicast(Bssid) || memcmp(Local, Bssid, 6) == 0 ||
        Sequence > 4095 || Reason == 0) {
        return 0;
    }
    memset(Frame, 0, MT7922_STATION_DISCONNECT_BYTES);
    Frame[0] = 0xc0; /* Deauthentication also ends the association. */
    WriteStationManagementHeader(Frame, Local, Bssid, Sequence);
    SetLe16(Frame + 24, Reason);
    return MT7922_STATION_DISCONNECT_BYTES;
}

bool Mt7922ReadStationDisconnect(const uint8_t *Frame, size_t Bytes,
    const uint8_t Local[6], const uint8_t Bssid[6])
{
    static const uint8_t broadcast[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    return Bytes >= MT7922_STATION_DISCONNECT_BYTES && (Frame[0] == 0xa0 || Frame[0] == 0xc0) &&
        (Frame[1] & 0xc7U) == 0 && (Frame[22] & 0xfU) == 0 &&
        (memcmp(Frame + 4, Local, 6) == 0 || memcmp(Frame + 4, broadcast, 6) == 0) &&
        memcmp(Frame + 10, Bssid, 6) == 0 && memcmp(Frame + 16, Bssid, 6) == 0 &&
        (Frame[24] != 0 || Frame[25] != 0);
}

bool Mt7922BuildStationCcmp(uint8_t Command[MT7922_CCMP_COMMAND_BYTES], uint8_t Wcid,
    uint8_t KeyId, bool Group, const uint8_t Key[MT7922_CCMP_KEY_BYTES])
{
    if (Wcid == 0 || (Group ? KeyId > 3 : KeyId != 0)) {
        return false;
    }
    /* mt76_connac_mcu_sta_key_tlv: STA_REC_KEY_V2 and one sec_key entry. */
    memset(Command, 0, MT7922_CCMP_COMMAND_BYTES);
    Command[1] = Wcid;
    Command[2] = 1;   /* One record TLV. */
    Command[4] = 1;   /* Append TLVs. */
    Command[5] = Group ? 0x0e : 0;
    Command[8] = 0x11; /* STA_REC_KEY_V2. */
    if (Key == NULL) {
        Command[10] = 8;
        Command[12] = 1; /* mac80211 DISABLE_KEY, zero cipher entries. */
        return true;
    }
    Command[10] = 44;  /* sta_rec_sec prefix plus one 36-byte sec_key. */
    Command[12] = 0;   /* mac80211 SET_KEY. */
    Command[13] = 1;
    Command[16] = 5;   /* MCU_CIPHER_AES_CCMP. */
    Command[17] = 36;
    Command[18] = KeyId;
    Command[19] = MT7922_CCMP_KEY_BYTES;
    memcpy(Command + 20, Key, MT7922_CCMP_KEY_BYTES);
    return true;
}

bool Mt7922ValidateStationRadio(const MT7922_STATION_RADIO_PROFILE *Profile)
{
    if (Profile == NULL || Profile->ChannelCount == 0 ||
        Profile->ChannelCount > MT7922_STATION_RADIO_CHANNELS ||
        Profile->Country[0] < 'A' || Profile->Country[0] > 'Z' ||
        Profile->Country[1] < 'A' || Profile->Country[1] > 'Z') {
        return false;
    }
    uint8_t previous = 0;
    for (size_t i = 0; i < Profile->ChannelCount; ++i) {
        const MT7922_STATION_RADIO_CHANNEL channel = Profile->Channels[i];
        const bool low = channel.Number >= 36 && channel.Number <= 48 && channel.Number % 4 == 0;
        const bool high = channel.Number >= 149 && channel.Number <= 165 && channel.Number % 4 == 1;
        if (channel.Number <= previous || (!low && !high) ||
            channel.LimitHalfDbm < -64 || channel.LimitHalfDbm > 63) {
            return false;
        }
        previous = channel.Number;
    }
    return true;
}

bool Mt7922StationRadioAllows(const MT7922_STATION_RADIO_PROFILE *Profile, uint8_t Channel)
{
    for (size_t i = 0; i < Profile->ChannelCount; ++i) {
        if (Profile->Channels[i].Number == Channel) {
            return true;
        }
    }
    return false;
}

bool Mt7922StationRadioAllows80(const MT7922_STATION_RADIO_PROFILE *Profile)
{
    for (uint8_t channel = MT7922_STATION_PRIMARY_80; channel <= 161; channel += 4) {
        if (!Mt7922StationRadioAllows(Profile, channel)) {
            return false;
        }
    }
    return true;
}

size_t Mt7922BuildStationDomain(uint8_t *Command, const MT7922_STATION_RADIO_PROFILE *Profile)
{
    /* Connac2 channel domain: BW_20M = 1, BW_20_40_80M = 2. */
    const size_t bytes = MT7922_STATION_DOMAIN_HEADER + MT7922_STATION_DOMAIN_ENTRY * Profile->ChannelCount;
    memset(Command, 0, bytes);
    memcpy(Command, Profile->Country, sizeof(Profile->Country));
    Command[4] = Command[5] = Command[6] = 1;
    if (Mt7922StationRadioAllows80(Profile)) {
        Command[5] = 2;
    }
    Command[9] = Profile->ChannelCount;
    for (size_t i = 0; i < Profile->ChannelCount; ++i) {
        Command[MT7922_STATION_DOMAIN_HEADER + MT7922_STATION_DOMAIN_ENTRY * i] = Profile->Channels[i].Number;
    }
    return bytes;
}

size_t Mt7925BuildStationDomain(uint8_t *Command, const MT7922_STATION_RADIO_PROFILE *Profile)
{
    /* Connac3 SET_DOMAIN_INFO: four-byte country header followed by a channel TLV. */
    if (Command == NULL || !Mt7922ValidateStationRadio(Profile)) {
        return 0;
    }
    const size_t tlvBytes = 8U + MT7925_STATION_DOMAIN_ENTRY * Profile->ChannelCount;
    const size_t bytes = MT7925_STATION_DOMAIN_HEADER + MT7925_STATION_DOMAIN_ENTRY * Profile->ChannelCount;
    memset(Command, 0, bytes);
    memcpy(Command, Profile->Country, sizeof(Profile->Country));
    Command[5] = 2; /* Advertise only 20/40/80 MHz, the station path implemented here. */
    Command[8] = 2; /* UNI_DOMAIN_INFO_CHANNEL TLV. */
    Command[10] = (uint8_t)tlvBytes;
    Command[11] = (uint8_t)(tlvBytes >> 8);
    Command[13] = Profile->ChannelCount;
    for (size_t i = 0; i < Profile->ChannelCount; ++i) {
        const size_t offset = MT7925_STATION_DOMAIN_HEADER + MT7925_STATION_DOMAIN_ENTRY * i;
        Command[offset] = Profile->Channels[i].Number;
        Command[offset + 1] = 0;
        /* Keep flags clear: only explicitly listed channels are exposed. */
    }
    return bytes;
}

size_t Mt7925BuildStationPower(uint8_t *Command, const MT7922_STATION_RADIO_PROFILE *Profile, size_t First)
{
    if (Command == NULL || !Mt7922ValidateStationRadio(Profile) || First >= Profile->ChannelCount) {
        return 0;
    }
    const size_t remaining = Profile->ChannelCount - First;
    const size_t batch = remaining < MT7925_STATION_POWER_BATCH ? remaining : MT7925_STATION_POWER_BATCH;
    const size_t bytes = MT7925_STATION_POWER_HEADER + MT7925_STATION_POWER_ENTRY * batch;
    memset(Command, 0, bytes);
    Command[4] = 1; /* Connac3 SET_POWER_LIMIT SKU TLV. */
    Command[6] = (uint8_t)(bytes - 4U);
    Command[7] = (uint8_t)((bytes - 4U) >> 8);
    Command[12] = (uint8_t)batch;
    Command[13] = 2; /* 5 GHz. */
    Command[14] = First + batch == Profile->ChannelCount;
    memcpy(Command + 16, Profile->Country, sizeof(Profile->Country));
    for (size_t i = 0; i < batch; ++i) {
        const MT7922_STATION_RADIO_CHANNEL channel = Profile->Channels[First + i];
        uint8_t *sku = Command + MT7925_STATION_POWER_HEADER + MT7925_STATION_POWER_ENTRY * i;
        sku[0] = channel.Number;
        memset(sku + 1, (uint8_t)channel.LimitHalfDbm, MT7925_STATION_POWER_ENTRY - 1U);
    }
    return bytes;
}

size_t Mt7922BuildStationPower(uint8_t *Command, const MT7922_STATION_RADIO_PROFILE *Profile, size_t First)
{
    /* mt76_connac_mcu_rate_txpower_band: 44-byte header, up to eight 162-byte SKUs. */
    const size_t remaining = Profile->ChannelCount - First;
    const size_t batch = remaining < MT7922_STATION_POWER_BATCH ? remaining : MT7922_STATION_POWER_BATCH;
    memset(Command, 0, MT7922_STATION_POWER_HEADER);
    Command[4] = (uint8_t)batch;
    Command[5] = 2; /* Firmware 5 GHz band. */
    Command[6] = batch == remaining;
    memcpy(Command + 8, Profile->Country, sizeof(Profile->Country));
    for (size_t i = 0; i < batch; ++i) {
        const MT7922_STATION_RADIO_CHANNEL channel = Profile->Channels[First + i];
        uint8_t *sku = Command + MT7922_STATION_POWER_HEADER + MT7922_STATION_POWER_ENTRY * i;
        sku[0] = channel.Number;
        /* Apply the effective channel ceiling on every rate. Factory calibration can lower it. */
        memset(sku + 1, (uint8_t)channel.LimitHalfDbm, MT7922_STATION_POWER_ENTRY - 1);
    }
    return MT7922_STATION_POWER_HEADER + MT7922_STATION_POWER_ENTRY * batch;
}

void Mt7922BuildStationChannel(uint8_t Command[MT7922_STATION_CHANNEL_BYTES], uint8_t Channel,
    uint8_t Center80, uint8_t Chains, bool ReceivePath)
{
    /* mt7921_mcu_set_chan_info, one radio, normal switch on non-DFS channels. */
    memset(Command, 0, MT7922_STATION_CHANNEL_BYTES);
    Command[0] = Channel;
    Command[1] = Center80 != 0 ? Center80 : Channel;
    Command[2] = Center80 != 0 ? 2 : 0;
    Command[3] = Chains;
    Command[4] = ReceivePath && Chains == 2 ? 3 : Chains;
    Command[10] = 1; /* NL80211_BAND_5GHZ in this command. */
}

/* Infrastructure addresses and RFC 1042 SNAP follow Linux ieee80211_{build_hdr,data_to_8023}. */
static const uint8_t Rfc1042[] = {0xaa, 0xaa, 0x03, 0, 0, 0};
static const uint8_t BridgeTunnel[] = {0xaa, 0xaa, 0x03, 0, 0, 0xf8};

bool Mt7922StationIsEapol(const uint8_t *Ethernet)
{
    return Ethernet[12] == 0x88 && Ethernet[13] == 0x8e;
}

static bool UsesBridgeTunnel(const uint8_t *EtherType)
{
    return (EtherType[0] == 0x80 && EtherType[1] == 0xf3) ||
        (EtherType[0] == 0x81 && EtherType[1] == 0x37); /* AARP and IPX. */
}

static size_t EapolLength(const uint8_t *Data, size_t Bytes)
{
    if (Bytes < 4 || Data[0] < 1 || Data[0] > 3 || Data[1] > 4) {
        return 0;
    }
    const size_t actual = 4U + ((size_t)Data[2] << 8) + Data[3];
    return actual <= Bytes ? actual : 0;
}

size_t Mt7922EncapsulateStationData(uint8_t *Frame, size_t Capacity,
    const uint8_t *Ethernet, size_t Bytes, const uint8_t Local[6], const uint8_t Bssid[6],
    uint16_t Sequence, bool Protected)
{
    static const uint8_t paeGroup[] = {0x01, 0x80, 0xc2, 0, 0, 0x03};
    if (Ethernet == NULL || Frame == NULL || Bytes <= 14 || Bytes > MT7922_STATION_ETHERNET_MAX ||
        Sequence > 4095 || Ethernet[12] < 6 || memcmp(Ethernet + 6, Local, 6) != 0) {
        return 0;
    }
    size_t payload = Bytes - 14;
    if (Mt7922StationIsEapol(Ethernet)) {
        if (memcmp(Ethernet, Bssid, 6) != 0 && memcmp(Ethernet, paeGroup, 6) != 0) {
            return 0;
        }
        payload = EapolLength(Ethernet + 14, payload);
    } else if (!Protected || ((Ethernet[0] & 1U) == 0 && !IsUnicast(Ethernet))) {
        return 0;
    }
    if (payload == 0 || Capacity < 32 + payload) {
        return 0;
    }
    memset(Frame, 0, 24);
    Frame[0] = 0x08; /* Data, To DS. */
    Frame[1] = Protected ? 0x41 : 0x01;
    memcpy(Frame + 4, Bssid, 6);
    memcpy(Frame + 10, Local, 6);
    memcpy(Frame + 16, Ethernet, 6);
    SetLe16(Frame + 22, (uint16_t)(Sequence << 4));
    memcpy(Frame + 24, UsesBridgeTunnel(Ethernet + 12) ? BridgeTunnel : Rfc1042, sizeof(Rfc1042));
    memcpy(Frame + 30, Ethernet + 12, 2 + payload);
    return 32 + payload;
}

static size_t StationDataHeader(const uint8_t *Frame, size_t Bytes, uint8_t Direction)
{
    if (Bytes < 33 || (Frame[0] != 0x08 && Frame[0] != 0x88) ||
        (Frame[1] & 0x87U) != Direction || (Frame[22] & 0x0fU) != 0) {
        return 0;
    }
    const size_t header = Frame[0] == 0x88 ? 26 : 24;
    if (Bytes <= header + 8 || (header == 26 && (Frame[24] & 0x80U) != 0)) {
        return 0;
    }
    return header;
}

static size_t StationSnapPayload(const uint8_t *Snap, size_t Bytes, bool *Eapol)
{
    if (Snap[6] < 6 ||
        !(memcmp(Snap, BridgeTunnel, sizeof(BridgeTunnel)) == 0 ||
          (!UsesBridgeTunnel(Snap + 6) && memcmp(Snap, Rfc1042, sizeof(Rfc1042)) == 0))) {
        return 0;
    }
    *Eapol = Snap[6] == 0x88 && Snap[7] == 0x8e;
    const size_t payload = *Eapol ? EapolLength(Snap + 8, Bytes - 8) : Bytes - 8;
    return payload <= MT7922_STATION_ETHERNET_MAX - 14U ? payload : 0;
}

size_t Mt7922ValidateStationRxData(const uint8_t *Frame, size_t Bytes,
    const uint8_t Local[6], const uint8_t Bssid[6], bool *Eapol)
{
    const size_t header = StationDataHeader(Frame, Bytes, 2);
    if (header == 0 || memcmp(Frame + 10, Bssid, 6) != 0 || !IsUnicast(Frame + 16)) {
        return 0;
    }
    const bool group = (Frame[4] & 1U) != 0;
    if (group ? memcmp(Frame + 16, Local, 6) == 0 : memcmp(Frame + 4, Local, 6) != 0) {
        return 0;
    }
    const size_t payload = StationSnapPayload(Frame + header, Bytes - header, Eapol);
    if (payload == 0 || (*Eapol ? group || memcmp(Frame + 16, Bssid, 6) != 0
        : (Frame[1] & 0x40U) == 0)) {
        return 0;
    }
    return header + 8 + payload;
}

size_t Mt7922ConvertStationTxData(uint8_t *Frame, size_t Bytes,
    const uint8_t Local[6], const uint8_t Bssid[6])
{
    const size_t header = StationDataHeader(Frame, Bytes, 1);
    if (header == 0 || memcmp(Frame + 4, Bssid, 6) != 0 || memcmp(Frame + 10, Local, 6) != 0) {
        return 0;
    }
    const uint8_t *snap = Frame + header;
    bool eapol;
    const size_t payload = StationSnapPayload(snap, Bytes - header, &eapol);
    if (payload == 0) {
        return 0;
    }
    uint8_t source[6];
    memcpy(source, Frame + 10, sizeof(source));
    memcpy(Frame, Frame + 16, 6);
    memcpy(Frame + 6, source, sizeof(source));
    memmove(Frame + 12, snap + 6, 2 + payload);
    return 14 + payload;
}

#ifdef MT7922_STATION_PACKET_TRACE
static uint16_t StationReadBe16(const uint8_t *Bytes)
{
    return (uint16_t)(((uint16_t)Bytes[0] << 8) | Bytes[1]);
}

static uint32_t StationReadBe32(const uint8_t *Bytes)
{
    return ((uint32_t)StationReadBe16(Bytes) << 16) | StationReadBe16(Bytes + 2);
}

bool Mt7922ReadStationTcpHeader(const uint8_t *Frame, size_t Bytes, bool Receive,
    MT7922_STATION_TCP_HEADER *Packet)
{
    const size_t header = StationDataHeader(Frame, Bytes, Receive ? 2 : 1);
    if (header == 0 || Bytes < header + 48) {
        return false;
    }
    const uint8_t *snap = Frame + header;
    const uint8_t *ip = snap + 8;
    const size_t ipHeader = (ip[0] & 15U) * 4U;
    const uint16_t ipLength = StationReadBe16(ip + 2);
    if (memcmp(snap, Rfc1042, sizeof(Rfc1042)) != 0 || StationReadBe16(snap + 6) != 0x0800 ||
        (ip[0] >> 4) != 4 || ipHeader < 20 || ip[9] != 6 || (StationReadBe16(ip + 6) & 0x3fffU) != 0 ||
        ipLength < ipHeader + 20 || ipLength > Bytes - header - 8) {
        return false;
    }
    const uint8_t *tcp = ip + ipHeader;
    const size_t tcpHeader = (tcp[12] >> 4) * 4U;
    if (tcpHeader < 20 || ipLength < ipHeader + tcpHeader) {
        return false;
    }
    *Packet = (MT7922_STATION_TCP_HEADER){
        .SourceIpv4 = StationReadBe32(ip + 12),
        .DestinationIpv4 = StationReadBe32(ip + 16),
        .Sequence = StationReadBe32(tcp + 4),
        .Acknowledgment = StationReadBe32(tcp + 8),
        .SourcePort = StationReadBe16(tcp),
        .DestinationPort = StationReadBe16(tcp + 2),
        .Ipv4Id = StationReadBe16(ip + 4),
        .PayloadBytes = (uint16_t)(ipLength - ipHeader - tcpHeader),
        .WlanSequence = (uint16_t)(((uint16_t)Frame[23] << 4) | (Frame[22] >> 4)),
        .Flags = tcp[13]
    };
    return true;
}
#endif
