#include "rx.h"

#define RXD_BASE_BYTES 24U
#define RXD_PACKET_NORMAL 2U
#define RXD_PACKET_EVENT 7U
#define RXD_PACKET_NORMAL_MCU 8U
#define RXD1_GROUP_1 (1U << 11)
#define RXD1_GROUP_2 (1U << 12)
#define RXD1_GROUP_3 (1U << 13)
#define RXD1_GROUP_4 (1U << 14)
#define RXD1_GROUP_5 (1U << 15)
#define RXD1_ICV_ERROR (1U << 25)
#define RXD1_TKIP_MIC_ERROR (1U << 26)
#define RXD1_FCS_ERROR (1U << 27)
#define RXD1_CIPHER_MISMATCH (1U << 23)
#define RXD1_CIPHER_LENGTH_MISMATCH (1U << 24)
#define RXD1_SECURITY_DONE (1U << 31)
#define RXD_CIPHER_CCMP 4U /* Hardware RX mode, distinct from the MCU key command. */
#define RXD2_FRAGMENT (1U << 27)
#define RXD3_AMSDU (1U << 22)
#define RXD2_TRANSLATED (1U << 13)
#define RXD2_AMSDU_ERROR (1U << 23)
#define RXD2_MAX_LENGTH_ERROR (1U << 24)
#define RXD2_TRANSLATION_ERROR (1U << 25)
#define RXD2_NON_AMPDU (1U << 30)
#define WLAN_ACTION_FC 0x00d0U
#define WLAN_TYPE_SUBTYPE_VERSION 0x00ffU
#define WLAN_ACTION_UNSUPPORTED_FLAGS 0x4700U /* DS, More Fragments, Protected */
#define WLAN_ORDER 0x8000U
#define WLAN_MGMT_BYTES 24U
#define AWDL_ACTION_BYTES 16U
#define AWDL_ELEMENT_HEADER_BYTES 3U
#define AWDL_SYNC_PARAMETERS 4U
#define AWDL_HT_CAPABILITIES 7U
#define AWDL_DATA_PATH_STATE 12U
#define AWDL_80211_CONTAINER 17U
#define WLAN_EID_VHT_CAPABILITIES 191U
#define AWDL_CHANNEL_SEQUENCE_TYPE 18U
#define AWDL_VERSION_TYPE 21U
#define AWDL_ELECTION_V2_TYPE 24U
#define AWDL_SEQUENCE_HEADER_BYTES 6U
#define AWDL_SEQUENCE_SIMPLE 0U
#define AWDL_SEQUENCE_LEGACY 1U
#define AWDL_SEQUENCE_OPCLASS 3U

static uint16_t ReadLe16(const uint8_t *Bytes)
{
    return (uint16_t)((uint16_t)Bytes[0] | (uint16_t)Bytes[1] << 8);
}

static uint32_t ReadLe32(const uint8_t *Bytes)
{
    return (uint32_t)Bytes[0] | (uint32_t)Bytes[1] << 8
        | (uint32_t)Bytes[2] << 16 | (uint32_t)Bytes[3] << 24;
}

MT7922_RX_STATUS Mt7922DecodeRx(const uint8_t *Buffer, size_t DmaBytes,
    MT7922_RX_FRAME *Frame)
{
    uint32_t word0, word1, word2;
    size_t recordBytes, offset;

    Frame->SkipFlags = 0;
    Frame->AmsduPart = 0;
    Frame->HeaderTranslationError = 0;
    Frame->PhyPresent = 0;
    Frame->Ampdu = 0;
    Frame->RssiPresent = 0;
    Frame->CcmpDecrypted = 0;
    Frame->CcmpPacketNumber = 0;
    if (DmaBytes < sizeof(uint32_t)) {
        Frame->SkipFlags |= MT7922_RX_SKIP_LENGTH;
        return Mt7922RxMalformed;
    }
    word0 = ReadLe32(Buffer);
    recordBytes = word0 & 0xffffU;
    if (recordBytes < sizeof(uint32_t) || recordBytes > DmaBytes) {
        Frame->SkipFlags |= MT7922_RX_SKIP_LENGTH;
        return Mt7922RxMalformed;
    }
    const uint32_t type = word0 >> 27;
    const uint32_t flags = (word0 >> 16) & 0xfU;
    if (type != RXD_PACKET_NORMAL && type != RXD_PACKET_NORMAL_MCU &&
        !(type == RXD_PACKET_EVENT && flags == 1U))
        return Mt7922RxIgnored;
    if (recordBytes < RXD_BASE_BYTES) {
        Frame->SkipFlags |= MT7922_RX_SKIP_LENGTH;
        return Mt7922RxMalformed;
    }

    word1 = ReadLe32(Buffer + 4);
    word2 = ReadLe32(Buffer + 8);
    if (word1 & RXD1_FCS_ERROR)
        Frame->SkipFlags |= MT7922_RX_SKIP_FCS;
    if (word2 & RXD2_TRANSLATED)
        Frame->SkipFlags |= MT7922_RX_SKIP_TRANSLATED;
    if (word2 & RXD2_AMSDU_ERROR)
        Frame->SkipFlags |= MT7922_RX_SKIP_AMSDU;
    if (word2 & RXD2_MAX_LENGTH_ERROR)
        Frame->SkipFlags |= MT7922_RX_SKIP_LENGTH;
    Frame->HeaderTranslationError = (word2 & RXD2_TRANSLATION_ERROR) != 0;
    /* The translation-error bit does not invalidate an untranslated 802.11 frame. */
    if ((word1 & (RXD1_FCS_ERROR | RXD1_ICV_ERROR | RXD1_TKIP_MIC_ERROR))
        || (word2 & (RXD2_TRANSLATED | RXD2_AMSDU_ERROR | RXD2_MAX_LENGTH_ERROR)))
        return Mt7922RxIgnored;

    /* Connac2 MT7921/MT7922 normal RXD groups appear in 4, 1, 2, 3, 5 order.
       Linux mt7921_mac_fill_rx only reads group 5 after group 3. */
    if ((word1 & RXD1_GROUP_5) && !(word1 & RXD1_GROUP_3)) {
        Frame->SkipFlags |= MT7922_RX_SKIP_GROUPS;
        return Mt7922RxMalformed;
    }
    offset = RXD_BASE_BYTES;
    if (word1 & RXD1_GROUP_4)
        offset += 16;
    const size_t group1 = offset;
    if (word1 & RXD1_GROUP_1)
        offset += 16;
    const size_t group2 = offset;
    if (word1 & RXD1_GROUP_2)
        offset += 8;
    const size_t group3 = offset;
    if (word1 & RXD1_GROUP_3)
        offset += 8;
    if (word1 & RXD1_GROUP_5)
        offset += 72;
    offset += 2U * ((word2 >> 14) & 3U);
    if (offset >= recordBytes) {
        Frame->SkipFlags |= MT7922_RX_SKIP_LENGTH;
        return Mt7922RxMalformed;
    }

    const uint32_t rx3 = ReadLe32(Buffer + 12);
    Frame->ReceiveWcid = (uint16_t)(word1 & 0x3ffU);
    Frame->ReceiveKeyId = (uint8_t)((word1 >> 21) & 3U);
    /* MT792x Group 1 has six PN octets in least-significant-first order.
       Only accept authenticated, intact CCMP frames with a usable replay counter. */
    if ((word1 & (RXD1_GROUP_1 | RXD1_SECURITY_DONE)) == (RXD1_GROUP_1 | RXD1_SECURITY_DONE) &&
        ((word1 >> 16) & 0x1fU) == RXD_CIPHER_CCMP &&
        (word1 & (RXD1_CIPHER_MISMATCH | RXD1_CIPHER_LENGTH_MISMATCH)) == 0 &&
        (word2 & RXD2_FRAGMENT) == 0 && (rx3 & RXD3_AMSDU) == 0) {
        const uint8_t *pn = Buffer + group1;
        for (unsigned i = 0; i < 6; ++i) {
            Frame->CcmpPacketNumber |= (uint64_t)pn[i] << (8U * i);
        }
        Frame->CcmpDecrypted = 1;
    }

    /* Connac2 channel coding from mt792x_get_status_freq_info. */
    const uint8_t channel = Buffer[13];
    Frame->Band = channel > 180 ? 3 : channel > 14 ? 2 : 1;
    Frame->Channel = channel > 180 ? (uint8_t)((channel - 181) * 4 + 1) : channel;

    if (word1 & RXD1_GROUP_3) {
        const uint32_t phy = ReadLe32(Buffer + group3);
        /* mt7921 RX uses the RCPI in group 5 when present. MT7922 has two chains. */
        const uint8_t *rcpi = Buffer + group3 + ((word1 & RXD1_GROUP_5) ? 32 : 4);
        for (unsigned chain = 0; chain < 2; ++chain) {
            if (rcpi[chain] != 0 && rcpi[chain] <= 220) {
                const int8_t dbm = (int8_t)((int)rcpi[chain] / 2 - 110);
                if (!Frame->RssiPresent || dbm > Frame->RssiDbm) {
                    Frame->RssiDbm = dbm;
                    Frame->RssiPresent = 1;
                }
            }
        }
        Frame->PhyPresent = 1;
        Frame->PhyMode = (uint8_t)((phy >> 24) & 15U);
        Frame->PhyWidth = (uint8_t)((phy >> 12) & 7U);
        Frame->PhyMcs = (uint8_t)(phy & 0x0fU);
    }
    if ((word1 & RXD1_GROUP_2) && !(word2 & RXD2_NON_AMPDU)) {
        Frame->Ampdu = 1;
        Frame->AmpduTimestamp = ReadLe32(Buffer + group2);
    }
    Frame->AmsduPart = Buffer[16] & 3U;
    Frame->Bytes = Buffer + offset;
    Frame->Length = recordBytes - offset;
    return Mt7922RxRawFrame;
}

MT7922_RX_STATUS Mt7925DecodeRx(const uint8_t *Buffer, size_t DmaBytes,
    MT7922_RX_FRAME *Frame)
{
    const size_t MT7925_RXD_BASE_BYTES = 32;
    const uint32_t MT7925_RXD_PACKET_NORMAL = 2;
    const uint32_t MT7925_RXD_PACKET_EVENT = 7;
    const uint32_t MT7925_RXD_PACKET_NORMAL_MCU = 8;
    const uint32_t MT7925_RXD1_GROUP_1 = 1U << 16;
    const uint32_t MT7925_RXD1_GROUP_2 = 1U << 17;
    const uint32_t MT7925_RXD1_GROUP_3 = 1U << 18;
    const uint32_t MT7925_RXD1_GROUP_4 = 1U << 19;
    const uint32_t MT7925_RXD1_GROUP_5 = 1U << 20;
    const uint32_t MT7925_RXD1_ICV_ERROR = 1U << 25;
    const uint32_t MT7925_RXD1_TKIP_MIC_ERROR = 1U << 26;
    const uint32_t MT7925_RXD1_CIPHER_MISMATCH = 1U << 23;
    const uint32_t MT7925_RXD1_CIPHER_LENGTH_MISMATCH = 1U << 24;
    const uint32_t MT7925_RXD1_SECURITY_DONE = 1U << 31;
    const uint32_t MT7925_RXD2_TRANSLATED = 1U << 7;
    const uint32_t MT7925_RXD2_FRAGMENT = 1U << 27;
    const uint32_t MT7925_RXD2_AMSDU_ERROR = 1U << 23;
    const uint32_t MT7925_RXD2_MAX_LENGTH_ERROR = 1U << 24;
    const uint32_t MT7925_RXD2_TRANSLATION_ERROR = 1U << 25;
    const uint32_t MT7925_RXD2_NON_AMPDU = 1U << 30;
    const uint32_t MT7925_RXD3_FCS_ERROR = 1U << 24;
    const uint32_t MT7925_RXD_CIPHER_CCMP = 4;
    uint32_t word0, word1, word2, word3, word4;
    size_t recordBytes, offset, group1, group2, group3;

    *Frame = (MT7922_RX_FRAME){0};
    if (DmaBytes < sizeof(uint32_t)) {
        Frame->SkipFlags |= MT7922_RX_SKIP_LENGTH;
        return Mt7922RxMalformed;
    }
    word0 = ReadLe32(Buffer);
    recordBytes = word0 & 0xffffU;
    if (recordBytes < sizeof(uint32_t) || recordBytes > DmaBytes) {
        Frame->SkipFlags |= MT7922_RX_SKIP_LENGTH;
        return Mt7922RxMalformed;
    }
    const uint32_t type = word0 >> 27;
    const uint32_t packetFlags = (word0 >> 16) & 0xfU;
    if (type != MT7925_RXD_PACKET_NORMAL && type != MT7925_RXD_PACKET_NORMAL_MCU &&
        !(type == MT7925_RXD_PACKET_EVENT && packetFlags == 1U)) {
        return Mt7922RxIgnored;
    }
    if (recordBytes < MT7925_RXD_BASE_BYTES) {
        Frame->SkipFlags |= MT7922_RX_SKIP_LENGTH;
        return Mt7922RxMalformed;
    }

    word1 = ReadLe32(Buffer + 4);
    word2 = ReadLe32(Buffer + 8);
    word3 = ReadLe32(Buffer + 12);
    word4 = ReadLe32(Buffer + 16);
    if (word3 & MT7925_RXD3_FCS_ERROR) Frame->SkipFlags |= MT7922_RX_SKIP_FCS;
    if (word2 & MT7925_RXD2_TRANSLATED) Frame->SkipFlags |= MT7922_RX_SKIP_TRANSLATED;
    if (word2 & MT7925_RXD2_AMSDU_ERROR) Frame->SkipFlags |= MT7922_RX_SKIP_AMSDU;
    if (word2 & MT7925_RXD2_MAX_LENGTH_ERROR) Frame->SkipFlags |= MT7922_RX_SKIP_LENGTH;
    Frame->HeaderTranslationError = (word2 & MT7925_RXD2_TRANSLATION_ERROR) != 0;
    if ((word3 & MT7925_RXD3_FCS_ERROR) || (word1 & (MT7925_RXD1_ICV_ERROR | MT7925_RXD1_TKIP_MIC_ERROR)) ||
        (word2 & (MT7925_RXD2_TRANSLATED | MT7925_RXD2_AMSDU_ERROR | MT7925_RXD2_MAX_LENGTH_ERROR))) {
        return Mt7922RxIgnored;
    }

    if ((word1 & MT7925_RXD1_GROUP_5) && !(word1 & MT7925_RXD1_GROUP_3)) {
        Frame->SkipFlags |= MT7922_RX_SKIP_GROUPS;
        return Mt7922RxMalformed;
    }
    offset = MT7925_RXD_BASE_BYTES;
    if (word1 & MT7925_RXD1_GROUP_4) offset += 16;
    group1 = offset;
    if (word1 & MT7925_RXD1_GROUP_1) offset += 16;
    group2 = offset;
    if (word1 & MT7925_RXD1_GROUP_2) offset += 16;
    group3 = offset;
    if (word1 & MT7925_RXD1_GROUP_3) {
        offset += 16;
        if (word1 & MT7925_RXD1_GROUP_5) offset += 24U * sizeof(uint32_t);
    }
    offset += 2U * ((word2 >> 13) & 7U);
    if (offset + 2U > recordBytes) {
        Frame->SkipFlags |= MT7922_RX_SKIP_LENGTH;
        return Mt7922RxMalformed;
    }

    Frame->ReceiveWcid = (uint16_t)(word1 & 0xfffU);
    Frame->ReceiveKeyId = (uint8_t)((word1 >> 21) & 3U);
    if ((word1 & (MT7925_RXD1_GROUP_1 | MT7925_RXD1_SECURITY_DONE)) ==
            (MT7925_RXD1_GROUP_1 | MT7925_RXD1_SECURITY_DONE) &&
        ((word2 >> 16) & 0x1fU) == MT7925_RXD_CIPHER_CCMP &&
        (word1 & (MT7925_RXD1_CIPHER_MISMATCH | MT7925_RXD1_CIPHER_LENGTH_MISMATCH)) == 0 &&
        (word2 & MT7925_RXD2_FRAGMENT) == 0 && (word4 & 3U) == 0) {
        for (unsigned i = 0; i < 6; ++i) {
            Frame->CcmpPacketNumber |= (uint64_t)Buffer[group1 + i] << (8U * i);
        }
        Frame->CcmpDecrypted = 1;
    }

    const uint8_t channel = Buffer[13];
    Frame->Band = channel > 180 ? 3 : channel > 14 ? 2 : 1;
    Frame->Channel = channel > 180 ? (uint8_t)((channel - 181) * 4 + 1) : channel;
    if (word1 & MT7925_RXD1_GROUP_3) {
        const uint32_t rxv0 = ReadLe32(Buffer + group3);
        const uint32_t rxv2 = ReadLe32(Buffer + group3 + 8);
        const uint8_t *rcpi = Buffer + group3 + 12;
        for (unsigned chain = 0; chain < 4; ++chain) {
            if (rcpi[chain] != 0 && rcpi[chain] <= 220) {
                const int8_t dbm = (int8_t)((int)rcpi[chain] / 2 - 110);
                if (!Frame->RssiPresent || dbm > Frame->RssiDbm) {
                    Frame->RssiDbm = dbm;
                    Frame->RssiPresent = 1;
                }
            }
        }
        Frame->PhyPresent = 1;
        Frame->PhyMode = (uint8_t)((rxv2 >> 11) & 0xfU);
        Frame->PhyWidth = (uint8_t)(rxv2 & 7U);
        Frame->PhyMcs = (uint8_t)(rxv0 & 0x7fU);
    }
    if ((word1 & MT7925_RXD1_GROUP_2) && !(word2 & MT7925_RXD2_NON_AMPDU)) {
        Frame->Ampdu = 1;
        Frame->AmpduTimestamp = ReadLe32(Buffer + group2);
    }
    Frame->AmsduPart = (uint8_t)(word4 & 3U);
    Frame->Bytes = Buffer + offset;
    Frame->Length = recordBytes - offset;
    return Mt7922RxRawFrame;
}

MT7922_RX_STATUS Mt792xDecodeRx(uint16_t ChipId, const uint8_t *Buffer,
    size_t DmaBytes, MT7922_RX_FRAME *Frame)
{
    if (ChipId == 0 || ChipId == 0x7922) {
        return Mt7922DecodeRx(Buffer, DmaBytes, Frame);
    }
    if (ChipId == 0x7925) {
        return Mt7925DecodeRx(Buffer, DmaBytes, Frame);
    }
    Frame->SkipFlags = MT7922_RX_SKIP_LENGTH;
    return Mt7922RxMalformed;
}

static int DecodeChannelSequence(const uint8_t *Value, size_t Length,
    AWDL_CHANNEL_SEQUENCE *Sequence)
{
    size_t slotBytes;
    uint8_t encoding;

    if (Length < AWDL_SEQUENCE_HEADER_BYTES || Value[0] != AWDL_CHANNEL_SLOTS - 1
        || Value[2] != 0 || Value[3] != 3 || ReadLe16(Value + 4) != 0xffffU)
        return 0;

    encoding = Value[1];
    switch (encoding) {
    case AWDL_SEQUENCE_SIMPLE:
        slotBytes = 1;
        break;
    case AWDL_SEQUENCE_LEGACY:
    case AWDL_SEQUENCE_OPCLASS:
        slotBytes = 2;
        break;
    default:
        return 0;
    }
    if (Length < AWDL_SEQUENCE_HEADER_BYTES + AWDL_CHANNEL_SLOTS * slotBytes)
        return 0;

    Sequence->Count = AWDL_CHANNEL_SLOTS;
    Sequence->Encoding = encoding;
    for (size_t i = 0; i < AWDL_CHANNEL_SLOTS; ++i) {
        const uint8_t *slot = Value + AWDL_SEQUENCE_HEADER_BYTES + i * slotBytes;
        Sequence->Channel[i] = slot[encoding == AWDL_SEQUENCE_LEGACY ? 1 : 0];
        Sequence->OperatingClass[i] = encoding == AWDL_SEQUENCE_OPCLASS ? slot[1] : 0;
    }
    return 1;
}

static int DataPathSupportsHe(const uint8_t *Value, size_t Length)
{
    if (Length < 2) {
        return 0;
    }
    const uint16_t flags = ReadLe16(Value);
    if ((flags & 0x8000U) == 0) {
        return 0;
    }
    size_t offset = 2 + ((flags & 0x0100U) ? 3U : 0U) + ((flags & 0x0200U) ? 2U : 0U)
        + ((flags & 1U) ? 8U : 0U) + ((flags & 2U) ? 6U : 0U) + ((flags & 4U) ? 6U : 0U)
        + ((flags & 0x0010U) ? 2U : 0U);
    if (offset + 2 > Length) {
        return 0;
    }
    if (flags & 0x1000U) {
        offset += 2U + ReadLe16(Value + offset);
    }
    return offset + 2 <= Length && (ReadLe16(Value + offset) & 0x0100U) != 0;
}

static void DecodeVhtCapabilities(const uint8_t *Element, size_t Bytes, AWDL_PEER_PHY *Phy)
{
    while (Bytes >= 2) {
        const size_t length = Element[1];
        if (length > Bytes - 2) {
            return;
        }
        if (Element[0] == WLAN_EID_VHT_CAPABILITIES && length == 12) {
            Phy->VhtCapabilities = ReadLe32(Element + 2);
            Phy->VhtRxMcs = ReadLe16(Element + 6);
            Phy->VhtTxMcs = ReadLe16(Element + 10);
            Phy->VhtPresent = 1;
            return;
        }
        Element += length + 2;
        Bytes -= length + 2;
    }
}

AWDL_ACTION_KIND AwdlDecodeAction(const uint8_t *Bytes, size_t Length,
    AWDL_ACTION *Action)
{
    uint16_t fc;
    size_t headerBytes, elementBytes, remaining;
    const uint8_t *body, *element, *election = NULL;
    AWDL_ACTION_KIND kind;
    AWDL_ACTION decoded = {0};

    if (Length < 2)
        return AwdlMalformed;
    fc = ReadLe16(Bytes);
    if ((fc & WLAN_TYPE_SUBTYPE_VERSION) != WLAN_ACTION_FC
        || (fc & WLAN_ACTION_UNSUPPORTED_FLAGS))
        return AwdlOther;
    headerBytes = WLAN_MGMT_BYTES + ((fc & WLAN_ORDER) ? 4U : 0U);
    if (Length < headerBytes + 1)
        return AwdlMalformed;
    if ((Bytes[22] & 0x0f) != 0)
        return AwdlOther;

    /* BSSID 00:25:00:ff:94:73. */
    if (ReadLe32(Bytes + 16) != 0xff002500U || ReadLe16(Bytes + 20) != 0x7394U)
        return AwdlOther;

    body = Bytes + headerBytes;
    if (body[0] != 127)
        return AwdlOther;
    if (Length - headerBytes < 5)
        return AwdlMalformed;
    if (ReadLe32(body + 1) != 0x08f21700U) /* Apple OUI 00:17:f2, type 8. */
        return AwdlOther;
    if (Length - headerBytes < AWDL_ACTION_BYTES)
        return AwdlMalformed;

    switch (body[6]) {
    case 0:
        kind = AwdlPsf;
        break;
    case 3:
        kind = AwdlMif;
        break;
    default:
        return AwdlOther;
    }

    element = body + AWDL_ACTION_BYTES;
    elementBytes = Length - headerBytes - AWDL_ACTION_BYTES;
    remaining = elementBytes;
    while (remaining >= AWDL_ELEMENT_HEADER_BYTES) {
        const uint8_t *value = element + AWDL_ELEMENT_HEADER_BYTES;
        size_t valueBytes = ReadLe16(element + 1);
        if (valueBytes > remaining - AWDL_ELEMENT_HEADER_BYTES)
            return AwdlMalformed;

        switch (element[0]) {
        case AWDL_SYNC_PARAMETERS:
            if (valueBytes < 31 || decoded.Sync.Master != NULL)
                return AwdlMalformed;
            decoded.Sync.NextChannel = value[0];
            decoded.Sync.UntilNextSlotTu = ReadLe16(value + 1);
            decoded.Sync.MasterChannel = value[3];
            decoded.Sync.WindowTu = ReadLe16(value + 5);
            decoded.Sync.Master = value + 21;
            decoded.Sync.WindowsPerSlot = value[27];
            decoded.Sync.NextWindowSequence = ReadLe16(value + 29);
            break;
        case AWDL_HT_CAPABILITIES:
            if (valueBytes >= 9) {
                decoded.Phy.HtCapabilities = ReadLe16(value + 2);
                decoded.Phy.HtAmpdu = value[4] & 0x1fU;
                decoded.Phy.HtRxMcs[0] = value[5];
                decoded.Phy.HtRxMcs[1] = value[6];
                decoded.Phy.HtPresent = 1;
            }
            break;
        case AWDL_DATA_PATH_STATE:
            decoded.HeSupported = (uint8_t)DataPathSupportsHe(value, valueBytes);
            break;
        case AWDL_80211_CONTAINER:
            DecodeVhtCapabilities(value, valueBytes, &decoded.Phy);
            break;
        case AWDL_CHANNEL_SEQUENCE_TYPE:
            if (decoded.Sequence.Count != 0
                || !DecodeChannelSequence(value, valueBytes, &decoded.Sequence))
                return AwdlMalformed;
            break;
        case AWDL_VERSION_TYPE:
            if (valueBytes >= 2)
                decoded.DeviceClass = value[1];
            break;
        case AWDL_ELECTION_V2_TYPE:
            if (valueBytes >= 40 && election == NULL)
                election = value;
            break;
        }

        element += AWDL_ELEMENT_HEADER_BYTES + valueBytes;
        remaining -= AWDL_ELEMENT_HEADER_BYTES + valueBytes;
    }
    /* Some MIF transmitters append up to two zero padding bytes. */
    for (size_t i = 0; i < remaining; ++i) {
        if (element[i] != 0)
            return AwdlMalformed;
    }

    if (election != NULL && decoded.Sync.Master != NULL
        && ReadLe32(election) == ReadLe32(decoded.Sync.Master)
        && ReadLe16(election + 4) == ReadLe16(decoded.Sync.Master + 4)) {
        decoded.Election.MasterCounter = ReadLe32(election + 12);
        decoded.Election.Distance = ReadLe32(election + 16);
        decoded.Election.MasterMetric = ReadLe32(election + 20);
        decoded.Election.Present = 1;
    }
    decoded.Sender = Bytes + 10;
    decoded.Version = body[5];
    decoded.PhysicalTx = ReadLe32(body + 8);
    decoded.TargetTx = ReadLe32(body + 12);
    decoded.Elements = body + AWDL_ACTION_BYTES;
    decoded.ElementBytes = elementBytes - remaining;
    decoded.Kind = kind;
    *Action = decoded;
    return kind;
}
