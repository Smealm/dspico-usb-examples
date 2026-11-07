#pragma once
#include "common.h"

/// @brief Geometry and usage of the micro SD card, parsed from its boot sector.
struct CardInfo
{
    bool valid = false;
    u32 sectorSize = 0;
    u32 sectorsPerCluster = 0;
    u32 clusterCount = 0;
    u64 totalBytes = 0;
    u64 freeBytes = 0;
    bool freeKnown = false;
    char fsType[8] = "";
    char label[12] = "";
};

/// @brief Reads and parses the card's boot sector.
/// @return True if the filesystem was understood.
bool ReadCardInfo(CardInfo& info);
