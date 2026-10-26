#include "ui16_font_sender.h"
#include "utils.h"
#include "ui16_link.h"
namespace {
std::uint8_t crc_byte(std::uint8_t crc, std::uint8_t value) {
    crc ^= value;
    for (unsigned i=0;i<8;++i) crc = crc&0x80 ? std::uint8_t((crc<<1)^7u) : std::uint8_t(crc<<1);
    return crc;
}
void raw_control(std::uint8_t value) {
    const auto crc=crc_byte(0,value);
    taskENTER_CRITICAL();
    fpga_tx_header(0x13,3);
    fpga_tx_byte(value);
    fpga_tx_byte(crc);
    taskEXIT_CRITICAL();
}
bool font_control(std::uint8_t value) {
#ifdef UI16_RELIABLE_FONT
    const std::uint8_t payload[]={value,crc_byte(0,value)};
    return ui16_link_send(0x13,payload,sizeof payload);
#else
    raw_control(value);
    return true;
#endif
}
}
bool ui16_font_blank() { return font_control(0); }
bool ui16_font_show() { return font_control(1); }
void ui16_font_raw_show() { raw_control(1); }
void ui16_font_raw_blank() { raw_control(0); }
bool ui16_font_write(std::uint16_t slot,const std::uint8_t (&rows)[32]) {
    // Higher slot bits are reserved; cap keeps the address within 16K words.
    if (slot >= 1024) return false;
    std::uint8_t crc=0;
#ifdef UI16_RELIABLE_FONT
    std::uint8_t payload[35];
    payload[0]=std::uint8_t(slot);payload[1]=std::uint8_t(slot>>8);
    for(unsigned i=0;i<32;++i)payload[i+2]=rows[i];
    for(unsigned i=0;i<34;++i)crc=crc_byte(crc,payload[i]);
    payload[34]=crc;
    return ui16_link_send(0x12,payload,sizeof payload);
#else
    taskENTER_CRITICAL();
    fpga_tx_header(0x12,36);
    auto send=[&](std::uint8_t b){fpga_tx_byte(b);crc=crc_byte(crc,b);};
    send(std::uint8_t(slot));send(std::uint8_t(slot>>8));
    for (auto b:rows)send(b);
    fpga_tx_byte(crc);
    taskEXIT_CRITICAL();
    return true;
#endif
}
