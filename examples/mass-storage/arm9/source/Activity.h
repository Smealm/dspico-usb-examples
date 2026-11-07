#pragma once
#include "common.h"

/// @brief Bytes the PC has read from the card since boot.
extern volatile u64 gHostReadBytes;

/// @brief Bytes the PC has written to the card since boot.
extern volatile u64 gHostWriteBytes;

/// @brief Clears both traffic counters.
void ActivityReset();
