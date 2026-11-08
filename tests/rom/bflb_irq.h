#pragma once
#include <cstdint>
std::uintptr_t bflb_irq_save();
void bflb_irq_restore(std::uintptr_t);
