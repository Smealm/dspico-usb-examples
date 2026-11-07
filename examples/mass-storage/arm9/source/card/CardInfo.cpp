#include "card/CardInfo.h"
#include <string.h>
#include "dldiIpc.h"

namespace
{
alignas(32) u8 sSector[512];

u16 ReadU16(const u8* data)
{
    return (u16)(data[0] | (data[1] << 8));
}

u32 ReadU32(const u8* data)
{
    return (u32)data[0] | ((u32)data[1] << 8) | ((u32)data[2] << 16) | ((u32)data[3] << 24);
}

u64 ReadU64(const u8* data)
{
    return (u64)ReadU32(data) | ((u64)ReadU32(data + 4) << 32);
}

void ReadSector(u32 sector)
{
    dldi_readSectors(sSector, sector, 1);
}

/// @brief Copies a fixed width, space padded label and drops non-printable bytes.
void CopyLabel(char* destination, size_t destinationSize, const u8* source, size_t sourceLength)
{
    size_t length = sourceLength;
    while (length > 0 && (source[length - 1] == ' ' || source[length - 1] == 0))
        length--;
    if (length >= destinationSize)
        length = destinationSize - 1;
    memcpy(destination, source, length);
    destination[length] = 0;
    for (size_t i = 0; i < length; i++)
    {
        if (destination[i] < 32 || destination[i] > 126)
            destination[i] = '?';
    }
}

/// @brief Counts free clusters in a FAT12 or FAT16 allocation table.
bool CountFreeClusters(u32 firstFatSector, u32 fatSectors, u32 clusterCount, bool fat12, u32& freeClusters)
{
    freeClusters = 0;
    u32 entry = 0;

    for (u32 sector = 0; sector < fatSectors; sector++)
    {
        ReadSector(firstFatSector + sector);

        if (!fat12)
        {
            for (u32 i = 0; i < 256; i++, entry++)
            {
                if (entry >= 2 && entry <= clusterCount + 1 && ReadU16(sSector + i * 2) == 0)
                    freeClusters++;
            }
        }
        else
        {
            for (u32 offset = 0; offset + 3 <= 512; offset += 3)
            {
                u32 first = sSector[offset] | ((sSector[offset + 1] & 0x0F) << 8);
                u32 second = (sSector[offset + 1] >> 4) | (sSector[offset + 2] << 4);

                if (entry >= 2 && entry <= clusterCount + 1 && first == 0)
                    freeClusters++;
                entry++;
                if (entry >= 2 && entry <= clusterCount + 1 && second == 0)
                    freeClusters++;
                entry++;
            }
        }
    }
    return true;
}

bool ReadExFatInfo(CardInfo& info)
{
    u32 shift = sSector[108];
    if (shift < 9 || shift > 12)
        return false;

    info.sectorSize = 1u << shift;
    info.sectorsPerCluster = 1u << sSector[109];
    info.clusterCount = ReadU32(sSector + 92);
    info.totalBytes = ReadU64(sSector + 72) * info.sectorSize;
    strcpy(info.fsType, "exFAT");

    // The exFAT label is eleven UTF-16 characters. Only ASCII survives.
    char label[12];
    for (u32 i = 0; i < 11; i++)
        label[i] = (char)sSector[96 + i * 2];
    CopyLabel(info.label, sizeof(info.label), (const u8*)label, 11);

    info.valid = true;
    return true;
}
}

bool ReadCardInfo(CardInfo& info)
{
    info = CardInfo{};

    ReadSector(0);
    if (ReadU16(sSector + 510) != 0xAA55)
        return false;

    if (memcmp(sSector + 3, "EXFAT   ", 8) == 0)
        return ReadExFatInfo(info);

    u32 bytesPerSector = ReadU16(sSector + 11);
    if (bytesPerSector != 512)
        return false;

    u32 reservedSectors = ReadU16(sSector + 14);
    u32 fatCount = sSector[16];
    u32 rootEntries = ReadU16(sSector + 17);
    u32 totalSectors = ReadU16(sSector + 19);
    u32 fatSectors = ReadU16(sSector + 22);
    if (totalSectors == 0 || fatSectors == 0)
    {
        totalSectors = ReadU32(sSector + 32);
        fatSectors = ReadU32(sSector + 36);
    }

    u32 rootDirectorySectors = ((rootEntries * 32) + (bytesPerSector - 1)) / bytesPerSector;
    u32 dataSectors = totalSectors - (reservedSectors + fatCount * fatSectors + rootDirectorySectors);

    info.sectorSize = bytesPerSector;
    info.sectorsPerCluster = sSector[13];
    if (info.sectorsPerCluster == 0 || dataSectors == 0)
        return false;

    info.clusterCount = dataSectors / info.sectorsPerCluster;
    info.totalBytes = (u64)totalSectors * bytesPerSector;
    info.valid = true;
    CopyLabel(info.label, sizeof(info.label), sSector + 0x47, 11);

    if (info.clusterCount < 4085)
        strcpy(info.fsType, "FAT12");
    else if (info.clusterCount < 65525)
        strcpy(info.fsType, "FAT16");
    else
        strcpy(info.fsType, "FAT32");

    u64 bytesPerCluster = (u64)info.sectorsPerCluster * bytesPerSector;

    if (memcmp(info.fsType, "FAT32", 6) == 0)
    {
        // FAT32 caches the free cluster count in the FSInfo sector.
        u32 fsInfoSector = ReadU16(sSector + 48);
        if (fsInfoSector != 0 && fsInfoSector != 0xFFFF)
        {
            ReadSector(fsInfoSector);
            if (ReadU32(sSector) == 0x41615252 && ReadU32(sSector + 484) == 0x61417272)
            {
                u32 freeClusters = ReadU32(sSector + 488);
                if (freeClusters <= info.clusterCount)
                {
                    info.freeBytes = (u64)freeClusters * bytesPerCluster;
                    info.freeKnown = true;
                }
            }
        }
        return true;
    }

    // FAT12/16 have no cached count, so walk the first FAT. Cards with a huge
    // table are left with an unknown free space rather than stalling the UI.
    if (fatSectors > 512)
        return true;

    bool fat12 = memcmp(info.fsType, "FAT12", 6) == 0;
    u32 freeClusters = 0;
    CountFreeClusters(reservedSectors, fatSectors, info.clusterCount, fat12, freeClusters);
    info.freeBytes = (u64)freeClusters * bytesPerCluster;
    info.freeKnown = true;
    return true;
}
