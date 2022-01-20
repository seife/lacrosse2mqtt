#include "SX127x.h"
#include <SPI.h>

/* datarates in bps which will be cycled in NextDataRate() */
static int _rates[] = { 9579, 17241 };

bool SX127x::ready()
{
    byte flags = ReadReg(REG_IRQFLAGS2);
    if (!(flags & RF_IRQFLAGS2_FIFOLEVEL))
        return false;
    if (!(flags & RF_IRQFLAGS2_PAYLOADREADY))
        return false;
    return true;
}

bool SX127x::Receive(byte &length)
{
    byte len = FRAME_LENGTH;
    byte i = 0;
    if (! ready())
        return false;

    while (ReadReg(REG_IRQFLAGS2) & RF_IRQFLAGS2_PAYLOADREADY) {
        byte bt = GetByteFromFifo();
        m_payload[i] = bt;
        i++;
        if (ReadReg(REG_IRQFLAGS2) & RF_IRQFLAGS2_FIFOEMPTY)
            break;
    }
    if (i >= len) {
        m_rssi = ReadReg(REG_RSSIVALUE);
        m_payloadready = true;
    }

    if (!m_payloadready)
        return false;

    m_payloadready = false;
    length = i;

    EnableReceiver(false);
    return true;
}

byte *SX127x::GetPayloadPointer()
{
    return &m_payload[0];
}

void SX127x::NextDataRate(byte idx)
{
    int rate;
    if (idx != 0xff)
        m_datarate = idx;
    else
        m_datarate++;
    m_datarate %= (sizeof(_rates)/sizeof(int));
    rate = _rates[m_datarate];
    word r = ((32000000UL + (rate / 2)) / rate);
    WriteReg(REG_BITRATEMSB, r >> 8);
    WriteReg(REG_BITRATELSB, r & 0xFF);
}

void SX127x::SetFrequency(unsigned long kHz)
{
    m_frequency = kHz;
    unsigned long f = (((kHz * 1000) << 2) / (32000000L >> 11)) << 6;
    WriteReg(REG_FRFMSB, f >> 16);
    WriteReg(REG_FRFMID, f >> 8);
    WriteReg(REG_FRFLSB, f);
}

void SX127x::EnableReceiver(bool enable, int len)
{
    if (!enable) { /* disable... */
        WriteReg(REG_OPMODE, (ReadReg(REG_OPMODE) & RF_OPMODE_MASK) | RF_OPMODE_STANDBY);
        return;
    }
    /* enable... */
    WriteReg(REG_OPMODE, (ReadReg(REG_OPMODE) & RF_OPMODE_MASK) | RF_OPMODE_RECEIVER);
    WriteReg(REG_FIFOTHRESH, RF_FIFOTHRESH_TXSTARTCONDITION_FIFONOTEMPTY | len - 1);
    WriteReg(REG_PAYLOADLENGTH, len);
    ClearFifo();
}

byte SX127x::GetByteFromFifo()
{
    return ReadReg(0x00);
}

void SX127x::ClearFifo()
{
    WriteReg(REG_IRQFLAGS2, 16);
}

void SX127x::SetupForLaCrosse()
{
    digitalWrite(m_ss, HIGH);
    EnableReceiver(false);

    /* MODULATIONSHAPING_00 == bit 3 and 4 of REG_OPMODE, bit 4 is "reserved", bit 3 is "LowFrequencyModeOn"
     * according to the datasheet. "LowFrequency" => 433MHz, so having it off here is OK. */
    /* 0x01 */ WriteReg(REG_OPMODE, RF_OPMODE_LONGRANGEMODE_OFF | RF_OPMODE_MODULATIONTYPE_FSK |
                                    RF_OPMODE_MODULATIONSHAPING_00 | RF_OPMODE_STANDBY);
    /* 0x04 */ WriteReg(REG_FDEVMSB, RF_FDEVMSB_30000_HZ);
    /* 0x05 */ WriteReg(REG_FDEVLSB, RF_FDEVLSB_30000_HZ);
//  /* 0x0d */ WriteReg(REG_RXCONFIG, RF_RXCONFIG_AFCAUTO_ON|RF_RXCONFIG_AGCAUTO_ON|RF_RXCONFIG_RXTRIGER_PREAMBLEDETECT);
//  /* 0x12 */ WriteReg(REG_RXBW, RF_RXBW_MANT_20 | RF_RXBW_EXP_2); // 100kHz
    /* 0x12 */ WriteReg(REG_RXBW, RF_RXBW_MANT_16 | RF_RXBW_EXP_2); // 125kHz
//  /* 0x12 */ WriteReg(REG_RXBW, RF_RXBW_MANT_24 | RF_RXBW_EXP_1); // 166kHz
    /* 0x3F */ WriteReg(REG_IRQFLAGS2, RF_IRQFLAGS2_FIFOOVERRUN);
    /* 0x29 */ WriteReg(REG_RSSITHRESH, 210);
    /* 0x27 */ WriteReg(REG_SYNCCONFIG, RF_SYNCCONFIG_AUTORESTARTRXMODE_WAITPLL_ON |
                                        RF_SYNCCONFIG_SYNC_ON | RF_SYNCCONFIG_SYNCSIZE_2);
    /* 0x28 */ WriteReg(REG_SYNCVALUE1, 0x2D);
    /* 0x29 */ WriteReg(REG_SYNCVALUE2, 0xD4);
    /* 0x30 */ WriteReg(REG_PACKETCONFIG1, RF_PACKETCONFIG1_CRCAUTOCLEAR_OFF);
    /* 0x31 */ WriteReg(REG_PACKETCONFIG2, RF_PACKETCONFIG2_DATAMODE_PACKET);
//  /* 0x35 */ WriteReg(REG_FIFOTHRESH, RF_FIFOTHRESH_TXSTARTCONDITION_FIFONOTEMPTY | RF_FIFOTHRESH_FIFOTHRESHOLD_THRESHOLD);
//  /* 0x35 */ WriteReg(REG_FIFOTHRESH, RF_FIFOTHRESH_TXSTARTCONDITION_FIFONOTEMPTY | 4);
    /* 0x35 */ WriteReg(REG_FIFOTHRESH, 4);
    /* 0x38 */ WriteReg(REG_PAYLOADLENGTH, 6 /*PAYLOADSIZE*/);
    /* 0x40 */ WriteReg(REG_DIOMAPPING1, RF_DIOMAPPING1_DIO0_00);
    /* 0x41 */ WriteReg(REG_DIOMAPPING2, RF_DIOMAPPING2_MAP_PREAMBLEDETECT);

}

byte SX127x::ReadReg(byte addr)
{
    SPI.beginTransaction(SPISettings(SPI_CLOCK_DIV4, MSBFIRST, SPI_MODE0));
    digitalWrite(m_ss, LOW);
    SPI.transfer(addr & 0x7F);
    uint8_t regval = SPI.transfer(0);
    digitalWrite(m_ss, HIGH);
    SPI.endTransaction();
    return regval;
}

void SX127x::WriteReg(byte addr, byte value)
{
    SPI.beginTransaction(SPISettings(SPI_CLOCK_DIV4, MSBFIRST, SPI_MODE0));
    digitalWrite(m_ss, LOW);
    SPI.transfer(addr | 0x80);
    SPI.transfer(value);
    digitalWrite(m_ss, HIGH);
    SPI.endTransaction();
}

bool SX127x::init()
{
    pinMode(m_ss, OUTPUT);
    delay(10);
    digitalWrite(m_ss, HIGH);
    if (m_reset != -1) {
        pinMode(m_reset, OUTPUT);
        digitalWrite(m_reset, LOW);
        delay(10);
        digitalWrite(m_reset, HIGH);
        delay(10);
    }
    SPI.begin();
    uint8_t version = ReadReg(REG_VERSION);
    if (version != 0x12)
        return false;
    return true;
}

SX127x::SX127x(byte ss, byte reset)
{
    m_reset = reset;
    m_ss = ss;
    m_datarate = 0;
    m_frequency = 868300;
    m_payloadready = false;
}

int SX127x::GetDataRate()
{
    return _rates[m_datarate];
}

int SX127x::GetRSSI()
{
    return -(m_rssi / 2);
}
