#include "tx.h"
#include "rx.h"

#define TXD_BYTES 32U
#define TXP_BUFFER0 40U
#define TXP_LENGTH0 44U
#define TXP_LENGTH1 46U
#define TXP_BUFFER1 48U
#define TXP_TOKEN_VALID (1U << 15)
#define TXP_BUFFER_LAST (1U << 15)
#define TXP_LENGTH_MASK 0xfffU
#define TXP_FIRST_SEGMENT_BYTES (4096U - MT7922_TXWI_BYTES)
#define TXD_LONG_FORMAT (1U << 31)
#define TXD_FORMAT_80211 (2U << 16)
#define TXD_FIX_RATE (1U << 31)
#define TXD_HTC_VALID (1U << 13)
#define TXD_MULTICAST (1U << 10)
#define TXD_SN_VALID (1U << 31)
#define TXD_BA_DISABLE (1U << 28)
#define TXD_NO_ACK (1U << 0)
#define TXD_PROTECT_FRAME (1U << 1)
#define TXD_STATUS_TO_HOST (1U << 10)
#define TXD_FIXED_BW (1U << 2)
#define TXD7_SPE_SHIFT 11U
#define TX_REPORT_HEADER_BYTES 8U
#define TX_REPORT_ENTRY_BYTES 32U
#define TX_REPORT_PACKET_TYPE 0U
#define TX_REPORT_RATE_MASK 0x3fffU
#define TX_REPORT_FIRST_PID 3U
#define TX_RELEASE_PACKET_TYPE 6U
#define TX_RELEASE_HEADER_BYTES 8U
#define TX_RELEASE_PAIR (1U << 31)
#define MT7925_TXD_BYTES 32U
#define MT7925_TXD1_FIXED_RATE (1U << 31)
#define MT7925_TXD1_OWN_MAC_SHIFT 25U
#define MT7925_TXD1_TID_SHIFT 21U
#define MT7925_TXD1_HDR_INFO_SHIFT 16U
#define MT7925_TXD1_HDR_FORMAT_80211 (2U << 14)
#define MT7925_TXD2_HTC_VALID (1U << 13)
#define MT7925_TXD3_SN_VALID (1U << 31)
#define MT7925_TXD3_BA_DISABLE (1U << 28)
#define MT7925_TXD3_BCM (1U << 4)
#define MT7925_TXD6_DAS (1U << 2)
#define MT7925_TXD6_MSDU_COUNT_ONE (1U << 4)
#define MT7925_TXD6_RATE_SHIFT 16U
#define MT7925_BASIC_RATES_TABLE 14U

static uint16_t ReadLe16(const uint8_t *Data)
{
    return (uint16_t)((uint16_t)Data[0] | (uint16_t)Data[1] << 8);
}

static uint32_t ReadLe32(const uint8_t *Data)
{
    return (uint32_t)Data[0] | (uint32_t)Data[1] << 8
        | (uint32_t)Data[2] << 16 | (uint32_t)Data[3] << 24;
}

static void WriteLe16(uint8_t *Data, uint16_t Value)
{
    Data[0] = (uint8_t)Value;
    Data[1] = (uint8_t)(Value >> 8);
}

static void WriteLe32(uint8_t *Data, uint32_t Value)
{
    WriteLe16(Data, (uint16_t)Value);
    WriteLe16(Data + 2, (uint16_t)(Value >> 16));
}

static int Mt7925RateTableIndex(uint16_t RateCode)
{
    /* mt76's legacy rate array has CCK 1/2/5.5/11 first, then OFDM. */
    switch (RateCode) {
    case MT7922_OFDM_6_MBPS:  return (int)(MT7925_BASIC_RATES_TABLE + 4U);
    case MT7922_OFDM_12_MBPS: return (int)(MT7925_BASIC_RATES_TABLE + 6U);
    case MT7922_OFDM_24_MBPS: return (int)(MT7925_BASIC_RATES_TABLE + 8U);
    case MT7922_OFDM_54_MBPS: return (int)(MT7925_BASIC_RATES_TABLE + 11U);
    default: return -1;
    }
}

static int BuildConnac3RawTxwi(uint8_t *Txwi, const MT7922_AWDL_TX *Request,
    const uint8_t *Frame, size_t FrameBytes, uint32_t HeaderWords, uint32_t Tid, int Protect)
{
    if (FrameBytes < 26 || FrameBytes > AWDL_TX_AMSDU3_FRAME_BYTES || Request->FrameAddress == 0
        || (FrameBytes > TXP_LENGTH_MASK && (!Request->Ampdu || (Frame[24] & 0x80U) == 0))
        || Request->FrameAddress > UINT32_MAX - (FrameBytes - 1)
        || Request->Wcid > 0xfffU || Request->Token >= TXP_TOKEN_VALID
        || Request->PacketId < TX_REPORT_FIRST_PID || Request->OwnMacIndex > 0x3fU
        || (Request->Queue > 3 && Request->Queue != MT7922_TX_QUEUE_ALTX0) || Request->SpeIndex > 31U
        || (Request->Ampdu && ((Frame[4] & 1U) != 0 || Frame[0] != 0x88 ||
            Request->RateCode != 0 || Request->Wcid == 0))) {
        return 0;
    }

    const uint16_t fc = ReadLe16(Frame);
    const uint32_t frameType = (fc >> 2) & 3U;
    const uint32_t frameSubtype = (fc >> 4) & 15U;
    const uint32_t sequence = (uint32_t)(ReadLe16(Frame + 22) >> 4);
    const int multicast = (Frame[4] & 1U) != 0;
    const int rateIndex = Request->Ampdu ? -1 : Mt7925RateTableIndex(Request->RateCode);
    if (!Request->Ampdu && rateIndex < 0) {
        return 0;
    }

    for (size_t i = 0; i < MT7922_TXWI_BYTES; ++i) {
        Txwi[i] = 0;
    }

    /* Connac3 TXD0 keeps the CT packet format at zero. Management frames use
       the firmware's ALTX0 scheduler queue; data retains its requested AC. */
    const uint32_t queue = frameType == 0 ? MT7922_TX_QUEUE_ALTX0 : Request->Queue;
    WriteLe32(Txwi, (uint32_t)(FrameBytes + MT7925_TXD_BYTES) | queue << 25);

    uint32_t txd1 = MT7925_TXD1_HDR_FORMAT_80211 |
        HeaderWords << MT7925_TXD1_HDR_INFO_SHIFT |
        Tid << MT7925_TXD1_TID_SHIFT |
        (uint32_t)Request->OwnMacIndex << MT7925_TXD1_OWN_MAC_SHIFT |
        Request->Wcid;
    if (!Request->Ampdu) {
        txd1 |= MT7925_TXD1_FIXED_RATE;
    }
    WriteLe32(Txwi + 4, txd1);

    const uint32_t htc = (fc & 0x8000U) != 0 ? MT7925_TXD2_HTC_VALID : 0;
    WriteLe32(Txwi + 8, htc | frameType << 4 | frameSubtype);

    const uint32_t txd3 = MT7925_TXD3_SN_VALID |
        (Request->Ampdu ? 0 : MT7925_TXD3_BA_DISABLE) | sequence << 16 |
        15U << 11 | (multicast ? TXD_NO_ACK | MT7925_TXD3_BCM : 0) |
        (Protect ? TXD_PROTECT_FRAME : 0);
    WriteLe32(Txwi + 12, txd3);
    WriteLe32(Txwi + 20, TXD_STATUS_TO_HOST | Request->PacketId);
    if (!Request->Ampdu) {
        WriteLe32(Txwi + 24, MT7925_TXD6_DAS | MT7925_TXD6_MSDU_COUNT_ONE |
            (uint32_t)rateIndex << MT7925_TXD6_RATE_SHIFT);
    } else {
        WriteLe32(Txwi + 24, MT7925_TXD6_DAS | MT7925_TXD6_MSDU_COUNT_ONE);
    }

    /* Connac3 uses the same 32-byte hardware TXP layout and LAST bit as
       Connac2. MT7925 PCIe DMA is constrained to 32-bit addresses. */
    WriteLe16(Txwi + TXD_BYTES, (uint16_t)(TXP_TOKEN_VALID | Request->Token));
    WriteLe32(Txwi + TXP_BUFFER0, Request->FrameAddress);
    if (FrameBytes <= TXP_LENGTH_MASK) {
        WriteLe16(Txwi + TXP_LENGTH0, (uint16_t)(TXP_BUFFER_LAST | FrameBytes));
    } else {
        WriteLe16(Txwi + TXP_LENGTH0, TXP_FIRST_SEGMENT_BYTES);
        WriteLe16(Txwi + TXP_LENGTH1,
            (uint16_t)(TXP_BUFFER_LAST | (FrameBytes - TXP_FIRST_SEGMENT_BYTES)));
        WriteLe32(Txwi + TXP_BUFFER1, Request->FrameAddress + TXP_FIRST_SEGMENT_BYTES);
    }
    return 1;
}

static int BuildRawTxwi(uint8_t *Txwi, const MT7922_AWDL_TX *Request,
    const uint8_t *Frame, size_t FrameBytes, uint32_t HeaderWords, uint32_t Tid, int Protect)
{
    if (Request->ChipId == 0x7925) {
        return BuildConnac3RawTxwi(Txwi, Request, Frame, FrameBytes, HeaderWords, Tid, Protect);
    }
    if (Request->ChipId != 0 && Request->ChipId != 0x7922) {
        return 0;
    }
    if (FrameBytes < 26 || FrameBytes > AWDL_TX_AMSDU3_FRAME_BYTES || Request->FrameAddress == 0
        || (FrameBytes > TXP_LENGTH_MASK && (!Request->Ampdu || (Frame[24] & 0x80U) == 0))
        || Request->FrameAddress > UINT32_MAX - (FrameBytes - 1)
        || Request->Wcid > 0x3ffU || Request->Token >= TXP_TOKEN_VALID
        || Request->PacketId < TX_REPORT_FIRST_PID || Request->OwnMacIndex > 0x3fU
        || (Request->Queue > 3 && Request->Queue != MT7922_TX_QUEUE_ALTX0) || Request->SpeIndex > 31U
        || (!Request->Ampdu && Request->RateCode != MT7922_OFDM_6_MBPS && Request->RateCode != MT7922_OFDM_12_MBPS
            && Request->RateCode != MT7922_OFDM_24_MBPS && Request->RateCode != MT7922_OFDM_54_MBPS)) {
        return 0;
    }

    const uint16_t fc = ReadLe16(Frame);
    const uint32_t frameType = (fc >> 2) & 3U;
    const uint32_t frameSubtype = (fc >> 4) & 15U;
    const uint32_t sequence = (uint32_t)(ReadLe16(Frame + 22) >> 4);
    const int multicast = (Frame[4] & 1U) != 0;
    if (Request->Ampdu && (multicast || Frame[0] != 0x88 ||
        Request->RateCode != 0 || Request->Wcid == 0)) {
        return 0;
    }

    for (size_t i = 0; i < MT7922_TXWI_BYTES; ++i) {
        Txwi[i] = 0;
    }
    WriteLe32(Txwi, (uint32_t)(FrameBytes + TXD_BYTES) | (uint32_t)Request->Queue << 25);
    WriteLe32(Txwi + 4, TXD_LONG_FORMAT | TXD_FORMAT_80211 | HeaderWords << 11
        | Tid << 20 | (uint32_t)Request->OwnMacIndex << 24 | Request->Wcid);
    const uint32_t fixedRate = Request->Ampdu ? 0 : TXD_FIX_RATE | TXD_HTC_VALID;
    WriteLe32(Txwi + 8, fixedRate | frameType << 4 | frameSubtype
        | (multicast ? TXD_MULTICAST : 0));
    WriteLe32(Txwi + 12, TXD_SN_VALID | (Request->Ampdu ? 0 : TXD_BA_DISABLE) | sequence << 16
        | 15U << 11 | (multicast ? TXD_NO_ACK : 0) | (Protect ? TXD_PROTECT_FRAME : 0));
    /* Raw AWDL uses host sequence numbers. Leave the ADDBA SSN intact so
       it matches the first QoS MPDU, instead of having WTBL supply it. */
    WriteLe32(Txwi + 20, TXD_STATUS_TO_HOST | Request->PacketId);
    if (!Request->Ampdu) {
        WriteLe32(Txwi + 24, (uint32_t)Request->RateCode << 16 | TXD_FIXED_BW);
    }
    WriteLe32(Txwi + 28, frameType << 20 | frameSubtype << 16
        | (uint32_t)Request->SpeIndex << TXD7_SPE_SHIFT);
    WriteLe16(Txwi + TXD_BYTES, (uint16_t)(TXP_TOKEN_VALID | Request->Token));
    WriteLe32(Txwi + TXP_BUFFER0, Request->FrameAddress);
    if (FrameBytes <= TXP_LENGTH_MASK) {
        WriteLe16(Txwi + TXP_LENGTH0, (uint16_t)(TXP_BUFFER_LAST | FrameBytes));
    } else {
        /* Connac2 hardware TXP can gather two buffers under the same token. */
        WriteLe16(Txwi + TXP_LENGTH0, TXP_FIRST_SEGMENT_BYTES);
        WriteLe16(Txwi + TXP_LENGTH1,
            (uint16_t)(TXP_BUFFER_LAST | (FrameBytes - TXP_FIRST_SEGMENT_BYTES)));
        WriteLe32(Txwi + TXP_BUFFER1, Request->FrameAddress + TXP_FIRST_SEGMENT_BYTES);
    }
    return 1;
}

int Mt7922BuildAwdlTxwi(uint8_t *Txwi, const MT7922_AWDL_TX *Request,
    const uint8_t *Frame, size_t FrameBytes)
{
    if (FrameBytes < 26) {
        return 0;
    }
    const uint16_t fc = ReadLe16(Frame);
    uint32_t headerWords;
    uint32_t tid = 0;
    if ((fc >> 2) == 0x34U) { /* Management action. */
        if (FrameBytes >= 26 && Frame[24] == 0x7f) {
            AWDL_ACTION action;
            AWDL_ACTION_KIND kind = AwdlDecodeAction(Frame, FrameBytes, &action);
            if (kind != AwdlPsf && kind != AwdlMif) {
                return 0;
            }
        }
        headerWords = (fc & 0x8000U) ? 14U : 12U;
    } else if ((fc >> 2) == 0x02U) { /* Data. */
        if (FrameBytes < 24) {
            return 0;
        }
        headerWords = 12;
    } else if ((fc >> 2) == 0x22U) { /* QoS data. */
        if (FrameBytes < 26) {
            return 0;
        }
        headerWords = 13;
        tid = Frame[24] & 0xfU;
    } else {
        return 0;
    }
    return BuildRawTxwi(Txwi, Request, Frame, FrameBytes, headerWords, tid, 0);
}

int Mt7922BuildStationMgmtTxwi(uint8_t *Txwi, const MT7922_AWDL_TX *Request,
    const uint8_t *Frame, size_t FrameBytes)
{
    if (FrameBytes < 26 || Frame[1] != 0 || (Frame[4] & 1U) != 0) {
        return 0;
    }
    switch (Frame[0]) {
    case 0x00:
        if (FrameBytes < 28) return 0;
        break;
    case 0xb0:
        if (FrameBytes != 30) return 0;
        break;
    case 0xc0:
        if (FrameBytes != 26) return 0;
        break;
    default:
        return 0;
    }
    return BuildRawTxwi(Txwi, Request, Frame, FrameBytes, 12, 0, 0);
}

int Mt7922BuildStationDataTxwi(uint8_t *Txwi, const MT7922_AWDL_TX *Request,
    const uint8_t *Frame, size_t FrameBytes)
{
    if (FrameBytes < 33 || Frame[0] != 0x08 || (Frame[1] != 0x01 && Frame[1] != 0x41) ||
        (Frame[4] & 1U) != 0) {
        return 0;
    }
    /* The station codec already validated LLC/SNAP. Only EAPOL can be sent clear. */
    const int protect = Frame[1] == 0x41;
    if (!protect && (FrameBytes < 36 || Frame[30] != 0x88 || Frame[31] != 0x8e)) {
        return 0;
    }
    return BuildRawTxwi(Txwi, Request, Frame, FrameBytes, 12, 0, protect);
}

MT7922_TX_REPORT_RESULT Mt7922ReadTxReport(const uint8_t *Data, size_t Length,
    const MT7922_AWDL_TX *Request, MT7922_TX_REPORT *Report)
{
    if (Length < sizeof(uint32_t)) {
        return Mt7922TxReportMalformed;
    }
    if ((ReadLe32(Data) >> 27) != TX_REPORT_PACKET_TYPE) {
        return Mt7922TxReportUnmatched;
    }
    if (Length < TX_REPORT_HEADER_BYTES + TX_REPORT_ENTRY_BYTES
        || (Length - TX_REPORT_HEADER_BYTES) % TX_REPORT_ENTRY_BYTES != 0) {
        return Mt7922TxReportMalformed;
    }

    for (size_t offset = TX_REPORT_HEADER_BYTES; offset < Length; offset += TX_REPORT_ENTRY_BYTES) {
        const uint8_t *entry = Data + offset;
        const uint32_t word0 = ReadLe32(entry);
        const uint32_t format = (word0 >> 23) & 3U;
        const uint32_t wcid = (ReadLe32(entry + 8) >> 16) & 0x3ffU;
        const uint32_t packetId = ReadLe32(entry + 12) >> 24;
        if (format > 1 || packetId != Request->PacketId || wcid != Request->Wcid) {
            continue;
        }
        Report->RateCode = (uint16_t)(word0 & TX_REPORT_RATE_MASK);
        Report->Sequence = (uint16_t)(ReadLe32(entry + 4) >> 20);
        Report->AckErrors = (uint8_t)((word0 >> 16) & 7U);
        Report->Ampdu = (uint8_t)((word0 >> 25) & 1U);
        Report->Bandwidth = (uint8_t)((word0 >> 29) & 3U);
        return Mt7922TxReportMatched;
    }
    return Mt7922TxReportUnmatched;
}

MT7922_TX_REPORT_RESULT Mt7922ReadTxRelease(const uint8_t *Data, size_t Length,
    uint16_t Token, MT7922_TX_RELEASE *Release)
{
    if (Length < sizeof(uint32_t)) {
        return Mt7922TxReportMalformed;
    }
    const uint32_t word0 = ReadLe32(Data);
    if ((word0 >> 27) != TX_RELEASE_PACKET_TYPE) {
        return Mt7922TxReportUnmatched;
    }
    if (Length < TX_RELEASE_HEADER_BYTES || (Length - TX_RELEASE_HEADER_BYTES) % 4U != 0) {
        return Mt7922TxReportMalformed;
    }

    uint32_t remaining = (word0 >> 16) & 0x3ffU;
    MT7922_TX_RELEASE decoded = {0};
    int found = 0;
    for (size_t offset = TX_RELEASE_HEADER_BYTES; remaining != 0; offset += 4) {
        if (offset >= Length) {
            return Mt7922TxReportMalformed;
        }
        uint32_t info = ReadLe32(Data + offset);
        if (info & TX_RELEASE_PAIR) {
            continue;
        }
        --remaining;
        if ((info >> 16) != Token) {
            continue;
        }
        if (found) {
            return Mt7922TxReportMalformed;
        }
        decoded.Attempts = (uint16_t)(info & 0x1fffU);
        decoded.Status = (uint8_t)((info >> 13) & 3U);
        found = 1;
    }
    if (!found) {
        return Mt7922TxReportUnmatched;
    }
    *Release = decoded;
    return Mt7922TxReportMatched;
}

static MT7922_TX_REPORT_RESULT Mt7925ReadTxReport(const uint8_t *Data, size_t Length,
    const MT7922_AWDL_TX *Request, MT7922_TX_REPORT *Report)
{
    if (Length < sizeof(uint32_t)) {
        return Mt7922TxReportMalformed;
    }
    const uint32_t header = ReadLe32(Data);
    if ((header >> 27) != TX_REPORT_PACKET_TYPE) {
        return Mt7922TxReportUnmatched;
    }
    if (Length < TX_REPORT_HEADER_BYTES + TX_REPORT_ENTRY_BYTES ||
        (Length - TX_REPORT_HEADER_BYTES) % TX_REPORT_ENTRY_BYTES != 0) {
        return Mt7922TxReportMalformed;
    }

    for (size_t offset = TX_REPORT_HEADER_BYTES; offset < Length; offset += TX_REPORT_ENTRY_BYTES) {
        const uint8_t *entry = Data + offset;
        const uint32_t word0 = ReadLe32(entry);
        const uint32_t format = (word0 >> 23) & 3U;
        const uint32_t wcid = (ReadLe32(entry + 8) >> 16) & 0xfffU;
        const uint32_t packetId = ReadLe32(entry + 12) >> 24;
        if (format > 1U || packetId != Request->PacketId || wcid != Request->Wcid) {
            continue;
        }
        Report->RateCode = (uint16_t)(word0 & TX_REPORT_RATE_MASK);
        Report->Sequence = (uint16_t)((ReadLe32(entry + 4) >> 20) & 0xfffU);
        Report->AckErrors = (uint8_t)((word0 >> 16) & 7U);
        Report->Ampdu = (uint8_t)((word0 >> 25) & 1U);
        Report->Bandwidth = (uint8_t)((word0 >> 29) & 7U);
        return Mt7922TxReportMatched;
    }
    return Mt7922TxReportUnmatched;
}

static MT7922_TX_REPORT_RESULT Mt7925ReadTxRelease(const uint8_t *Data, size_t Length,
    uint16_t Token, MT7922_TX_RELEASE *Release)
{
    const uint32_t txFreePair = 1U << 31;
    const uint32_t txFreeHeader = 1U << 30;
    if (Length < TX_RELEASE_HEADER_BYTES || (Length - TX_RELEASE_HEADER_BYTES) % 4U != 0) {
        return Mt7922TxReportMalformed;
    }
    const uint32_t word0 = ReadLe32(Data);
    if ((word0 >> 27) != TX_RELEASE_PACKET_TYPE) {
        return Mt7922TxReportUnmatched;
    }
    const uint32_t version = (ReadLe32(Data + 4) >> 16) & 0xfU;
    if (version < 4U) {
        return Mt7922TxReportMalformed;
    }

    uint32_t remaining = (word0 >> 16) & 0x3ffU;
    uint16_t attempts = 0;
    uint8_t status = 0;
    int found = 0;
    for (size_t offset = TX_RELEASE_HEADER_BYTES; remaining != 0 && offset < Length; offset += 4) {
        const uint32_t info = ReadLe32(Data + offset);
        if (info & txFreePair) {
            continue;
        }
        if (info & txFreeHeader) {
            attempts = (uint16_t)((info >> 24) & 0xfU);
            status = (uint8_t)((info >> 28) & 3U);
            continue;
        }
        const uint16_t ids[2] = {
            (uint16_t)(info & 0x7fffU),
            (uint16_t)((info >> 15) & 0x7fffU)
        };
        for (unsigned i = 0; i < 2; ++i) {
            if (ids[i] == 0x7fffU) {
                continue;
            }
            if (remaining == 0) {
                return Mt7922TxReportMalformed;
            }
            --remaining;
            if (ids[i] == Token) {
                if (found) {
                    return Mt7922TxReportMalformed;
                }
                Release->Attempts = attempts;
                Release->Status = status;
                found = 1;
            }
        }
    }
    if (remaining != 0) {
        return Mt7922TxReportMalformed;
    }
    return found ? Mt7922TxReportMatched : Mt7922TxReportUnmatched;
}

MT7922_TX_REPORT_RESULT Mt792xReadTxReport(uint16_t ChipId, const uint8_t *Data,
    size_t Length, const MT7922_AWDL_TX *Request, MT7922_TX_REPORT *Report)
{
    if (ChipId == 0x7925) {
        return Mt7925ReadTxReport(Data, Length, Request, Report);
    }
    return Mt7922ReadTxReport(Data, Length, Request, Report);
}

MT7922_TX_REPORT_RESULT Mt792xReadTxRelease(uint16_t ChipId, const uint8_t *Data,
    size_t Length, uint16_t Token, MT7922_TX_RELEASE *Release)
{
    if (ChipId == 0x7925) {
        return Mt7925ReadTxRelease(Data, Length, Token, Release);
    }
    return Mt7922ReadTxRelease(Data, Length, Token, Release);
}

static size_t BuildActionFrame(uint8_t *Frame, size_t Capacity, const uint8_t *Dest,
    const uint8_t *Source, const AWDL_ACTION *Peer, uint32_t Timestamp, int Mif,
    uint8_t DeviceClass, uint8_t Channel, const uint8_t *ServiceId)
{
    static const uint8_t broadcast[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    static const uint8_t bssid[] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    static const uint8_t signature[] = {0x7f, 0, 0x17, 0xf2, 8, 0x10, 0, 0};
    static const uint8_t hostName[] = {'M', 'T', '7', '9', '2', '2', '-', 'P', 'C'};
    const uint8_t *arpaName = ServiceId != NULL ? ServiceId : hostName;
    const size_t arpaNameBytes = ServiceId != NULL ? AWDL_AIRDROP_ID_BYTES : sizeof(hostName);
    enum { FrameHeader = 40, SyncBytes = 73, ElectionBytes = 21, SequenceBytes = 38,
        SequenceTlvBytes = 41, Election2Bytes = 40, ServiceBytes = 9, HtBytes = 8,
        PathBytes = 15, VersionBytes = 2 };
    const size_t arpaValueBytes = 2 + arpaNameBytes + 2;
    const size_t arpaBytes = Mif ? 3 + arpaValueBytes : 0;
    const size_t frameBytes = FrameHeader + 3 + SyncBytes + 3 + ElectionBytes
        + 3 + SequenceTlvBytes + 3 + Election2Bytes + 3 + ServiceBytes
        + 3 + HtBytes + arpaBytes + 3 + PathBytes + 3 + VersionBytes;
    if ((Channel != 6 && Channel != 44 && Channel != 149) || Capacity < frameBytes || Peer->Sync.Master == NULL ||
        Peer->Sync.WindowTu != 16 || Peer->Sync.WindowsPerSlot != 4 ||
        Peer->Sync.UntilNextSlotTu == 0 || Peer->Sync.UntilNextSlotTu > 64) {
        return 0;
    }

    for (size_t i = 0; i < frameBytes; ++i) {
        Frame[i] = 0;
    }
    Frame[0] = 0xd0;
    const uint8_t *dest = Dest != NULL ? Dest : broadcast;
    for (size_t i = 0; i < sizeof(bssid); ++i) {
        Frame[4 + i] = dest[i];
        Frame[10 + i] = Source[i];
        Frame[16 + i] = bssid[i];
    }
    for (size_t i = 0; i < sizeof(signature); ++i) {
        Frame[24 + i] = signature[i];
    }
    Frame[30] = Mif ? 3 : 0;
    WriteLe32(Frame + 32, Timestamp);
    WriteLe32(Frame + 36, Timestamp);

    uint8_t channelSequence[SequenceBytes] = {15, 3, 0, 3, 0xff, 0xff};
    const int restrictSlots = Channel == 149 && Peer->Sequence.Count == AWDL_CHANNEL_SLOTS;
    for (size_t i = 0; i < AWDL_CHANNEL_SLOTS; ++i) {
        if (!restrictSlots || Peer->Sequence.Channel[i] == Channel) {
            channelSequence[6 + i * 2] = Channel;
            channelSequence[7 + i * 2] = Channel > 14 ? 128 : 81;
        }
    }

    uint8_t *tlv = Frame + FrameHeader;
    tlv[0] = 4;
    WriteLe16(tlv + 1, SyncBytes);
    uint8_t *sync = tlv + 3;
    /* Report the tuned channel here. Availability is described by the slot table below. */
    sync[0] = Channel;
    WriteLe16(sync + 1, Peer->Sync.UntilNextSlotTu);
    sync[3] = Peer->Sync.MasterChannel != 0 ? Peer->Sync.MasterChannel : Channel;
    sync[4] = 0;
    WriteLe16(sync + 5, 16);
    WriteLe16(sync + 7, 110);
    WriteLe16(sync + 9, 0x1800);
    WriteLe16(sync + 11, 16);
    WriteLe16(sync + 13, 16);
    /* The 16 TU common window ends 48 TU before the end of the slot. */
    const uint16_t commonRemaining = Peer->Sync.UntilNextSlotTu > 48
        ? (uint16_t)(Peer->Sync.UntilNextSlotTu - 48) : 0;
    WriteLe16(sync + 15, commonRemaining);
    for (size_t i = 17; i < 21; ++i) {
        sync[i] = 3;
    }
    for (size_t i = 0; i < 6; ++i) {
        sync[21 + i] = Peer->Sync.Master[i];
    }
    sync[27] = 4;
    sync[28] = 0;
    WriteLe16(sync + 29, Peer->Sync.NextWindowSequence);
    WriteLe16(sync + 31, Peer->Sync.NextWindowSequence);
    for (size_t i = 0; i < SequenceBytes; ++i) {
        sync[33 + i] = channelSequence[i];
    }
    tlv += 3 + SyncBytes;

    const uint32_t ownMetric = 60;
    const uint32_t masterMetric = Peer->Election.Present ? Peer->Election.MasterMetric : ownMetric;
    const uint32_t distance = Peer->Election.Distance == UINT32_MAX
        ? UINT32_MAX : Peer->Election.Distance + 1;
    tlv[0] = 5;
    WriteLe16(tlv + 1, ElectionBytes);
    uint8_t *election = tlv + 3;
    election[3] = (uint8_t)(distance > UINT8_MAX ? UINT8_MAX : distance);
    for (size_t i = 0; i < 6; ++i) {
        election[5 + i] = Peer->Sync.Master[i];
    }
    WriteLe32(election + 11, masterMetric);
    WriteLe32(election + 15, ownMetric);
    tlv += 3 + ElectionBytes;

    tlv[0] = 18;
    WriteLe16(tlv + 1, SequenceTlvBytes);
    for (size_t i = 0; i < SequenceBytes; ++i) {
        tlv[3 + i] = channelSequence[i];
    }
    tlv += 3 + SequenceTlvBytes;

    tlv[0] = 24;
    WriteLe16(tlv + 1, Election2Bytes);
    uint8_t *election2 = tlv + 3;
    for (size_t i = 0; i < 6; ++i) {
        election2[i] = Peer->Sync.Master[i];
        election2[6 + i] = Peer->Sender[i];
    }
    WriteLe32(election2 + 12, Peer->Election.MasterCounter);
    WriteLe32(election2 + 16, distance);
    WriteLe32(election2 + 20, masterMetric);
    WriteLe32(election2 + 24, ownMetric);
    tlv += 3 + Election2Bytes;

    tlv[0] = 6;
    WriteLe16(tlv + 1, ServiceBytes);
    tlv += 3 + ServiceBytes;

    /* HT Capabilities: one stream, all MCS, as an iPhone advertises. */
    tlv[0] = 7;
    WriteLe16(tlv + 1, HtBytes);
    WriteLe16(tlv + 5, 0x11ce);
    tlv[7] = 0x1b;
    tlv[8] = 0xff;
    tlv += 3 + HtBytes;

    if (Mif) {
        tlv[0] = 16;
        WriteLe16(tlv + 1, (uint16_t)arpaValueBytes);
        tlv[3] = 3;
        tlv[4] = (uint8_t)arpaNameBytes;
        for (size_t i = 0; i < arpaNameBytes; ++i) {
            tlv[5 + i] = arpaName[i];
        }
        tlv[5 + arpaNameBytes] = 0xc0;
        tlv[6 + arpaNameBytes] = 0x0c;
        tlv += arpaBytes;
    }

    /* Data Path State: legacy flags, country X0, home channel, our MAC. */
    tlv[0] = 12;
    WriteLe16(tlv + 1, PathBytes);
    WriteLe16(tlv + 3, 0x8f24);
    tlv[5] = 'X';
    tlv[6] = '0';
    tlv[7] = 0;
    WriteLe16(tlv + 8, Channel == 149 ? 4 : Channel == 44 ? 2 : 1);
    for (size_t i = 0; i < 6; ++i) {
        tlv[10 + i] = Source[i];
    }
    tlv += 3 + PathBytes;

    tlv[0] = 21;
    WriteLe16(tlv + 1, VersionBytes);
    tlv[3] = 0x34;
    tlv[4] = DeviceClass;
    return frameBytes;
}

size_t AwdlBuildProbePsf(uint8_t *Frame, size_t Capacity, const uint8_t *Source,
    const AWDL_ACTION *Peer, uint32_t Timestamp, uint8_t Channel)
{
    /* The link probe sends a PSF only and does not claim an Apple device class. */
    return BuildActionFrame(Frame, Capacity, NULL, Source, Peer, Timestamp, 0, 0, Channel, NULL);
}

size_t AwdlBuildAdvert(uint8_t *Frame, size_t Capacity, const uint8_t *Source,
    const AWDL_ACTION *Peer, uint32_t Timestamp, int Mif, uint8_t DeviceClass, uint8_t Channel)
{
    return BuildActionFrame(Frame, Capacity, NULL, Source, Peer, Timestamp, Mif, DeviceClass, Channel, NULL);
}

size_t AwdlAppendVhtCapabilities(uint8_t *Frame, size_t Capacity, size_t Length)
{
    enum { VhtElementBytes = 12, VhtValueBytes = 2 + VhtElementBytes, VhtTlvBytes = 3 + VhtValueBytes };
    if (Length == 0 || Length > Capacity || Capacity - Length < VhtTlvBytes) {
        return 0;
    }
    uint8_t *tlv = Frame + Length;
    tlv[0] = 17;                  /* IEEE 802.11 element container. */
    WriteLe16(tlv + 1, VhtValueBytes);
    tlv[3] = 191;                 /* VHT Capabilities element. */
    tlv[4] = VhtElementBytes;
    /* Match the WinAWDL and MT7922 mt76 VHT receive capabilities. */
    WriteLe32(tlv + 5, (7U << 23) | (1U << 5)); /* 1 MiB - 1 A-MPDU, 80 MHz short GI. */
    WriteLe16(tlv + 9, 0xfffa);   /* Receive MCS 0-9 on two streams. */
    WriteLe16(tlv + 11, 0);       /* Highest receive rate unspecified. */
    WriteLe16(tlv + 13, 0xfffa);
    WriteLe16(tlv + 15, 0);
    return Length + VhtTlvBytes;
}

int AwdlSetHighEfficiency(uint8_t *Frame, size_t Length)
{
    /* Our PSF/MIF builders use a 24-byte MAC header and a 16-byte AWDL header.
       Set the HE extended flag in the Data Path State layout we generate. */
    for (size_t offset = 40; offset + 3 <= Length;) {
        uint8_t *tlv = Frame + offset;
        const size_t bytes = ReadLe16(tlv + 1);
        if (bytes > Length - offset - 3) {
            return 0;
        }
        if (tlv[0] == 12 && bytes == 15 && ReadLe16(tlv + 3) == 0x8f24U) {
            tlv[17] |= 1U; /* High byte of the extended flags at Data Path State offset 13. */
            return 1;
        }
        offset += 3 + bytes;
    }
    return 0;
}

/* AWDL Service Response TLVs use fixed name suffix codes rather than mDNS pointers. */
static uint8_t *AppendServiceResponse(uint8_t *Tlv, const uint8_t *Name, uint16_t NameBytes,
    uint8_t Type, const uint8_t *Value, uint16_t ValueBytes)
{
    Tlv[0] = 2;
    WriteLe16(Tlv + 1, (uint16_t)(7U + NameBytes + ValueBytes));
    WriteLe16(Tlv + 3, (uint16_t)(NameBytes + 1U));
    Tlv += 5;
    for (size_t i = 0; i < NameBytes; ++i) {
        *Tlv++ = Name[i];
    }
    *Tlv++ = Type;
    WriteLe16(Tlv, ValueBytes);
    WriteLe16(Tlv + 2, 0);
    Tlv += 4;
    for (size_t i = 0; i < ValueBytes; ++i) {
        *Tlv++ = Value[i];
    }
    return Tlv;
}

size_t AwdlBuildServiceMif(uint8_t *Frame, size_t Capacity, const uint8_t *Source,
    const AWDL_ACTION *Peer, uint32_t Timestamp, uint8_t DeviceClass, uint8_t Channel,
    const uint8_t *ServiceId, uint16_t Port)
{
    static const uint8_t serviceName[] = {0xc0, 0x07}; /* _airdrop._tcp.local */
    static const uint8_t flags[] = {10, 'f', 'l', 'a', 'g', 's', '=', '1', '0', '1', '9'};
    uint8_t instance[1 + AWDL_AIRDROP_ID_BYTES + 2] = {AWDL_AIRDROP_ID_BYTES};
    uint8_t server[6 + sizeof(instance)] = {0};
    for (size_t i = 0; i < AWDL_AIRDROP_ID_BYTES; ++i) {
        instance[1 + i] = server[7 + i] = ServiceId[i];
    }
    instance[sizeof(instance) - 2] = server[sizeof(server) - 2] = 0xc0;
    instance[sizeof(instance) - 1] = 0x07;
    server[sizeof(server) - 1] = 0x0c; /* .local, also sent in ARPA */
    server[4] = (uint8_t)(Port >> 8);
    server[5] = (uint8_t)Port;
    server[6] = AWDL_AIRDROP_ID_BYTES;

    const size_t serviceBytes = 3 * 10 + sizeof(serviceName) + 3 * sizeof(instance) +
        sizeof(server) + sizeof(flags);
    if (Capacity < serviceBytes) {
        return 0;
    }
    const size_t mifBytes = BuildActionFrame(Frame, Capacity - serviceBytes, NULL, Source,
        Peer, Timestamp, 1, DeviceClass, Channel, ServiceId);
    if (mifBytes == 0) {
        return 0;
    }
    uint8_t *tlv = AppendServiceResponse(Frame + mifBytes, serviceName, sizeof(serviceName), 12,
        instance, sizeof(instance));
    tlv = AppendServiceResponse(tlv, instance, sizeof(instance), 33, server, sizeof(server));
    tlv = AppendServiceResponse(tlv, instance, sizeof(instance), 16, flags, sizeof(flags));
    return (size_t)(tlv - Frame);
}

size_t AwdlBuildUnicastProbe(uint8_t *Frame, size_t Capacity, const uint8_t *Dest,
    const uint8_t *Source, const AWDL_ACTION *Peer, uint32_t Timestamp,
    uint8_t DeviceClass)
{
    return BuildActionFrame(Frame, Capacity, Dest, Source, Peer, Timestamp, 0, DeviceClass, 6, NULL);
}

static size_t BuildBlockAckAction(uint8_t *Frame, size_t Capacity, const uint8_t *Dest,
    const uint8_t *Source, uint8_t Action, uint8_t Token, uint16_t Sequence)
{
    static const uint8_t bssid[] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    const size_t frameBytes = 24 + 9;
    if (Capacity < frameBytes || Dest == NULL || Source == NULL) {
        return 0;
    }
    for (size_t i = 0; i < frameBytes; ++i) {
        Frame[i] = 0;
    }
    Frame[0] = 0xd0;
    for (size_t i = 0; i < 6; ++i) {
        Frame[4 + i] = Dest[i];
        Frame[10 + i] = Source[i];
        Frame[16 + i] = bssid[i];
    }
    WriteLe16(Frame + 22, (uint16_t)((Sequence & 0x0fffU) << 4));
    Frame[24] = 3; /* Block Ack category. */
    Frame[25] = Action;
    Frame[26] = Token;
    return frameBytes;
}

size_t AwdlBuildAddbaRequest(uint8_t *Frame, size_t Capacity, const uint8_t *Dest,
    const uint8_t *Source, uint8_t Token, uint16_t Window, uint16_t Timeout, uint16_t StartingSequence,
    int AllowAmsdu)
{
    const size_t bytes = BuildBlockAckAction(Frame, Capacity, Dest, Source, 0, Token, 0);
    if (bytes == 0) {
        return 0;
    }
    WriteLe16(Frame + 27, (uint16_t)((Window << 6) | 2U | (AllowAmsdu != 0))); /* TID 0, immediate BA. */
    WriteLe16(Frame + 29, Timeout);
    WriteLe16(Frame + 31, (uint16_t)((StartingSequence & 0x0fffU) << 4));
    return bytes;
}

size_t AwdlBuildAddbaResponse(uint8_t *Frame, size_t Capacity, const uint8_t *Dest,
    const uint8_t *Source, uint8_t Token, uint16_t RequestedParams, uint16_t Timeout, uint16_t Sequence)
{
    const size_t frameBytes = BuildBlockAckAction(Frame, Capacity, Dest, Source, 1, Token, Sequence);
    if (frameBytes == 0) {
        return 0;
    }
    /* Status 0 at 27..28, then the requested parameters with immediate Block
       Ack and the window capped at 64. */
    uint16_t window = (uint16_t)((RequestedParams >> 6) & 0x3ffU);
    if (window == 0 || window > 64) {
        window = 64;
    }
    const uint16_t params = (uint16_t)((RequestedParams & 0x003dU) | 0x0002U
        | (window << 6));
    Frame[29] = (uint8_t)params;
    Frame[30] = (uint8_t)(params >> 8);
    Frame[31] = (uint8_t)Timeout;
    Frame[32] = (uint8_t)(Timeout >> 8);
    return frameBytes;
}

int AwdlIsPureTcpAck(const uint8_t *Ip, size_t Length, uint16_t SourcePort)
{
    if (Length < 60 || (Ip[0] >> 4) != 6 || Ip[6] != 6 || Ip[24] == 0xff || Ip[53] != 0x10) {
        return 0;
    }
    const size_t tcpHeader = (Ip[52] >> 4) * 4U;
    const uint16_t port = (uint16_t)((Ip[40] << 8) | Ip[41]);
    const size_t payload = ((size_t)Ip[4] << 8) | Ip[5];
    return tcpHeader >= 20 && tcpHeader == Length - 40 && payload == tcpHeader && port == SourcePort;
}

int AwdlIsMdnsMulticast(const uint8_t *Ip, size_t Length)
{
    static const uint8_t group[16] = {0xff, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xfb};
    if (Length < 60 || (Ip[0] >> 4) != 6 || Ip[6] != 17 || Ip[7] != 255 ||
        Ip[40] != 0x14 || Ip[41] != 0xe9 || Ip[42] != 0x14 || Ip[43] != 0xe9 ||
        (((size_t)Ip[44] << 8) | Ip[45]) != Length - 40) {
        return 0;
    }
    for (size_t i = 0; i < sizeof(group); ++i) {
        if (Ip[24 + i] != group[i]) {
            return 0;
        }
    }
    return 1;
}

#define AWDL_IP_HEADER_BYTES 16U /* SNAP and AWDL data headers. */
#define AWDL_AMSDU_SUBFRAME_HEADER_BYTES 14U

static void WriteIpFrameHeader(uint8_t *Frame, size_t FrameBytes, const uint8_t *Dest,
    const uint8_t *Source, uint16_t Sequence, int Qos)
{
    static const uint8_t bssid[] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    for (size_t i = 0; i < FrameBytes; ++i) {
        Frame[i] = 0;
    }
    Frame[0] = Qos ? 0x88 : 0x08;
    for (size_t i = 0; i < 6; ++i) {
        Frame[4 + i] = Dest[i];
        Frame[10 + i] = Source[i];
        Frame[16 + i] = bssid[i];
    }
    WriteLe16(Frame + 22, (uint16_t)((Sequence & 0x0fffU) << 4));
}

static void WriteIpPayload(uint8_t *Body, const uint8_t *Ip, size_t IpBytes, uint16_t Sequence)
{
    static const uint8_t snap[] = {0xaa, 0xaa, 0x03, 0x00, 0x17, 0xf2, 0x08, 0x00};
    for (size_t i = 0; i < sizeof(snap); ++i) {
        Body[i] = snap[i];
    }
    uint8_t *awdl = Body + sizeof(snap);
    awdl[0] = 0x03;
    awdl[1] = 0x04;
    awdl[2] = (uint8_t)(Sequence & 0xff);
    awdl[3] = (uint8_t)(Sequence >> 8);
    awdl[6] = 0x86;
    awdl[7] = 0xdd;
    for (size_t i = 0; i < IpBytes; ++i) {
        awdl[8 + i] = Ip[i];
    }
}

static size_t BuildIpFrame(uint8_t *Frame, size_t Capacity, const uint8_t *Dest,
    const uint8_t *Source, const uint8_t *Ip, size_t IpBytes, uint16_t Sequence, int Qos)
{
    const size_t headerBytes = Qos ? 26U : 24U;
    const size_t frameBytes = headerBytes + AWDL_IP_HEADER_BYTES + IpBytes;
    if (Capacity < frameBytes || Dest == NULL || Source == NULL || Ip == NULL) {
        return 0;
    }
    WriteIpFrameHeader(Frame, frameBytes, Dest, Source, Sequence, Qos);
    WriteIpPayload(Frame + headerBytes, Ip, IpBytes, Sequence);
    return frameBytes;
}

static void WriteAmsduSubframe(uint8_t *Body, const uint8_t *Dest, const uint8_t *Source,
    const uint8_t *Ip, size_t IpBytes, uint16_t Sequence)
{
    const size_t payloadBytes = AWDL_IP_HEADER_BYTES + IpBytes;
    for (size_t i = 0; i < 6; ++i) {
        Body[i] = Dest[i];
        Body[6 + i] = Source[i];
    }
    Body[12] = (uint8_t)(payloadBytes >> 8);
    Body[13] = (uint8_t)payloadBytes;
    WriteIpPayload(Body + AWDL_AMSDU_SUBFRAME_HEADER_BYTES, Ip, IpBytes, Sequence);
}

size_t AwdlBuildQosAmsduFrame(uint8_t *Frame, size_t Capacity, const uint8_t *Dest,
    const uint8_t *Source, const uint8_t *First, size_t FirstBytes,
    const uint8_t *Second, size_t SecondBytes, const uint8_t *Third, size_t ThirdBytes,
    uint16_t Sequence)
{
    if (Dest == NULL || Source == NULL || First == NULL || Second == NULL ||
        (Dest[0] & 1U) != 0 || FirstBytes < 40 || SecondBytes < 40 ||
        FirstBytes > 1500 || SecondBytes > 1500 ||
        (ThirdBytes != 0 && (Third == NULL || ThirdBytes < 40 || ThirdBytes > 1500))) {
        return 0;
    }

    const size_t headerBytes = 26U;
    const size_t subframeHeader = AWDL_AMSDU_SUBFRAME_HEADER_BYTES + AWDL_IP_HEADER_BYTES;
    const size_t firstPadded = (subframeHeader + FirstBytes + 3U) & ~(size_t)3U;
    const size_t secondLength = subframeHeader + SecondBytes;
    const size_t secondPadded = (secondLength + 3U) & ~(size_t)3U;
    const size_t secondOffset = headerBytes + firstPadded;
    const size_t thirdOffset = secondOffset + secondPadded;
    const size_t frameBytes = ThirdBytes != 0 ? thirdOffset + subframeHeader + ThirdBytes :
        secondOffset + secondLength;
    if (Capacity < frameBytes) {
        return 0;
    }
    WriteIpFrameHeader(Frame, frameBytes, Dest, Source, Sequence, 1);
    Frame[24] = 0x80; /* A-MSDU present, TID 0. */
    WriteAmsduSubframe(Frame + headerBytes, Dest, Source, First, FirstBytes, Sequence);
    WriteAmsduSubframe(Frame + secondOffset, Dest, Source, Second, SecondBytes,
        (uint16_t)(Sequence + 1U));
    if (ThirdBytes != 0) {
        WriteAmsduSubframe(Frame + thirdOffset, Dest, Source, Third, ThirdBytes,
            (uint16_t)(Sequence + 2U));
    }
    return frameBytes;
}

size_t AwdlBuildIpFrame(uint8_t *Frame, size_t Capacity, const uint8_t *Dest,
    const uint8_t *Source, const uint8_t *Ip, size_t IpBytes, uint16_t Sequence)
{
    return BuildIpFrame(Frame, Capacity, Dest, Source, Ip, IpBytes, Sequence, 0);
}

size_t AwdlBuildQosIpFrame(uint8_t *Frame, size_t Capacity, const uint8_t *Dest,
    const uint8_t *Source, const uint8_t *Ip, size_t IpBytes, uint16_t Sequence)
{
    return BuildIpFrame(Frame, Capacity, Dest, Source, Ip, IpBytes, Sequence, 1);
}

size_t AwdlBuildDataProbe(uint8_t *Frame, size_t Capacity, const uint8_t *Dest,
    const uint8_t *Source, uint8_t Sequence)
{
    static const uint8_t bssid[] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    static const uint8_t payload[] = "MT7922-AWDL-DATA";
    const size_t frameBytes = 24 + sizeof(payload) - 1;
    if (Capacity < frameBytes || Dest == NULL || Source == NULL) {
        return 0;
    }
    for (size_t i = 0; i < frameBytes; ++i) {
        Frame[i] = 0;
    }
    Frame[0] = 0x08; /* Data; captured AWDL data frames are not QoS. */
    for (size_t i = 0; i < 6; ++i) {
        Frame[4 + i] = Dest[i];
        Frame[10 + i] = Source[i];
        Frame[16 + i] = bssid[i];
    }
    WriteLe16(Frame + 22, (uint16_t)(Sequence << 4));
    for (size_t i = 0; i < sizeof(payload) - 1; ++i) {
        Frame[24 + i] = payload[i];
    }
    return frameBytes;
}
