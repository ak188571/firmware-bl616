#pragma once
#include <cstddef>
#include <cstdint>
#ifdef UI16_RELIABLE_FONT
void ui16_link_set_platform(std::uint32_t (*millis)(),void (*wait_one_ms)());
void ui16_link_monitor_reset();
void ui16_link_accept_ack(const std::uint8_t *body,std::size_t length);
bool ui16_link_prepare();
bool ui16_link_send(std::uint8_t command,const std::uint8_t *payload,std::size_t length);
#endif
