#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

#if RETRO_REV0U
#include "Legacy/UserStorageLegacy.cpp"
#endif

// Macro to access the header variables of a block of memory.
// Note that this is pointless if the pointer is already pointing directly at the header rather than the memory after it.
#define HEADER(memory, header_value) memory[-HEADER_SIZE + header_value]

// Every block of allocated memory is prefixed with a header that consists of the following four longwords.
enum {
    // Whether the block of memory is actually allocated or not.
    HEADER_ACTIVE,
    // Which 'data set' this block of memory belongs to.
    HEADER_SET_ID,
    // The offset in the buffer which the block of memory begins at.
    HEADER_DATA_OFFSET,
    // How long the block of memory is (measured in 'uint32's).
    HEADER_DATA_LENGTH,
    // This is not part of the header: it's just a bit of enum magic to calculate the size of the header.
    HEADER_SIZE
};

#if !defined(P6_SCENE_TEST) // P6.3: relocates to WRAM-L (pointer form), defined in p6_io_main.cpp
DataStorage RSDK::dataStorage[DATASET_MAX];
#endif

bool32 RSDK::InitStorage()
{
    // Storage limits.
#if RETRO_PLATFORM == RETRO_SATURN
    // P4 data retarget (Task #203): these 5 pools are runtime malloc() heaps
    // (allocated below at line ~42), NOT .bss. The stock totals are
    // 24+8+32+2+8 = 74 MB -- malloc() CANNOT satisfy that on a 2 MB Saturn, so
    // InitStorage() returns false (line ~45) and boot dies before the first
    // object. Saturn-size them to a bounded heap covering the P5 proof (one
    // Ring's tile/sprite/string allocations). DATASET_STG (stage tiles +
    // collision + object data) and DATASET_TMP (transient decode scratch) carry
    // the GHZ load weight. These are TUNABLE -- the real GHZ1 working set is
    // measured at P5/P6; sized here for the bounded proof. Non-Saturn builds
    // keep the stock 74 MB totals byte-identical.
#if defined(P6_SCENE_TEST)
    // P6.4/P6.5a (Tasks #225/#208): trim the pools the zero-registered-class
    // proof barely uses, freeing WRAM-L for the relocated dataFileList
    // registry (57,344 B, P6.4) and the REAL tilesetPixels backing (262,144 B,
    // P6.5a). MEASURED basis: Title/Scene1.bin DATASET_STG persistent =
    // 13,184 B (64 KB keeps 4.9x headroom); DATASET_TMP peak = 95,264 B
    // (128 KB keeps 1.34x; the GIF decode adds GifDecoder 24,892 B + palette
    // 1 KB transiently AFTER the 88 KB tempEntityList is freed); MUS/SFX are
    // touched only by Audio StageLoad paths, which never run with
    // classCount == 0. Stock Saturn literals (the #else) stay the P6.5b+
    // basis and get re-measured when real stages load through the engine.
    dataStorage[DATASET_STG].storageLimit = 64 * 1024;  //  64 KB (proof-trim)
    dataStorage[DATASET_MUS].storageLimit = 16 * 1024;  //  16 KB (proof-trim)
    dataStorage[DATASET_SFX].storageLimit = 32 * 1024;  //  32 KB (proof-trim)
    dataStorage[DATASET_STR].storageLimit = 32 * 1024;  //  32 KB
    // P6.7 W11 closer C2: 128K -> 80K. The W11 band store removes the big
    // LAYOUT inflates from TMP (Saturn layouts never route through
    // ReadCompressed at the W11b wiring), but TileConfig's verbatim
    // ReadCompressed buffer is 77,824 B decompressed (2 x 1024 x 38 --
    // MEASURED: a 64K trim fired qa_p6_collision K3-K5 RED, alloc fail ->
    // packed window stayed zero) and bounds the pool. Other tenants
    // (capped tempEntityList 22,016 + GIF decoder 24,892 + the 32,768 B
    // band scratch) are non-concurrent peaks below it.
    dataStorage[DATASET_TMP].storageLimit = 80 * 1024;  //  80 KB (C2)
#else
    dataStorage[DATASET_STG].storageLimit = 256 * 1024; // 256 KB
    dataStorage[DATASET_MUS].storageLimit = 64 * 1024;  //  64 KB
    dataStorage[DATASET_SFX].storageLimit = 128 * 1024; // 128 KB
    dataStorage[DATASET_STR].storageLimit = 32 * 1024;  //  32 KB
    dataStorage[DATASET_TMP].storageLimit = 128 * 1024; // 128 KB
#endif
#else
    dataStorage[DATASET_STG].storageLimit = 24 * 1024 * 1024; // 24MB
    dataStorage[DATASET_MUS].storageLimit = 8 * 1024 * 1024;  //  8MB
    dataStorage[DATASET_SFX].storageLimit = 32 * 1024 * 1024; // 32MB
    dataStorage[DATASET_STR].storageLimit = 2 * 1024 * 1024;  //  2MB
    dataStorage[DATASET_TMP].storageLimit = 8 * 1024 * 1024;  //  8MB
#endif

#if RETRO_PLATFORM == RETRO_SATURN
    // P6.7 W11 closer C1: carve the per-dataset entry backings from the
    // dataStorage window itself, right after the 5 structs (Storage.hpp C1
    // comment; STG 0x800 entries, the rest 0x100 -- total 160 + 24,576 B
    // inside the P68_LWRAM_DATASTORAGE_PLANNED 24,832 B contract line).
    {
        static const uint16 p6EntryCaps[DATASET_MAX] = {
            STORAGE_ENTRY_COUNT_STG, STORAGE_ENTRY_COUNT_SMALL,
            STORAGE_ENTRY_COUNT_SMALL, STORAGE_ENTRY_COUNT_SMALL,
            STORAGE_ENTRY_COUNT_SMALL
        };
        uint8 *backing = (uint8 *)&dataStorage[DATASET_MAX];
        for (int32 s = 0; s < DATASET_MAX; ++s) {
            dataStorage[s].dataEntries = (uint32 ***)backing;
            backing += (uint32)p6EntryCaps[s] * sizeof(uint32 **);
            dataStorage[s].storageEntries = (uint32 **)backing;
            backing += (uint32)p6EntryCaps[s] * sizeof(uint32 *);
            dataStorage[s].entryCapacity = p6EntryCaps[s];
        }
    }
#endif

    for (int32 s = 0; s < DATASET_MAX; ++s) {
        dataStorage[s].usedStorage = 0;
        dataStorage[s].entryCount  = 0;
        dataStorage[s].clearCount  = 0;
        dataStorage[s].memoryTable = (uint32 *)malloc(dataStorage[s].storageLimit);

        if (dataStorage[s].memoryTable == NULL)
            return false;
    }

    return true;
}

void RSDK::ReleaseStorage()
{
    for (int32 s = 0; s < DATASET_MAX; ++s) {
        if (dataStorage[s].memoryTable != NULL)
            free(dataStorage[s].memoryTable);

        dataStorage[s].usedStorage = 0;
        dataStorage[s].entryCount  = 0;
        dataStorage[s].clearCount  = 0;
    }

    // this code isn't in steam executable, since it omits the "load datapack into memory" feature.
    // I don't think it's in the console versions either, but this never seems to be freed in those versions.
    // so, I figured doing it here would be the neatest.
#if !RETRO_USE_ORIGINAL_CODE
    for (int32 p = 0; p < dataPackCount; ++p) {
        if (dataPacks[p].fileBuffer)
            free(dataPacks[p].fileBuffer);

        dataPacks[p].fileBuffer = NULL;
    }
#endif
}

void RSDK::AllocateStorage(void **dataPtr, uint32 size, StorageDataSets dataSet, bool32 clear)
{
    uint32 **data = (uint32 **)dataPtr;
    *data         = NULL;

    if ((uint32)dataSet < DATASET_MAX) {
        // Align allocation to prevent unaligned memory accesses later on.
        const uint32 size_aligned = size & -(int32)sizeof(void *);

        if (size_aligned < size)
            size = size_aligned + sizeof(void *);

        if (dataStorage[dataSet].entryCount < STORAGE_ENTRY_CAP(&dataStorage[dataSet])) {
            DataStorage *storage = &dataStorage[dataSet];

#if !RETRO_USE_ORIGINAL_CODE
            // Bug: The original release never takes into account the size of the header when checking if there's enough storage left.
            // Omitting this will overflow the memory pool when (storageLimit - usedStorage + size) < header size (16 bytes here).
            if (storage->usedStorage * sizeof(uint32) + size + (HEADER_SIZE * sizeof(uint32)) < storage->storageLimit) {
#else
            if (storage->usedStorage * sizeof(uint32) + size < storage->storageLimit) {
#endif
                // HEADER_ACTIVE
                storage->memoryTable[storage->usedStorage] = true;
                ++storage->usedStorage;

                // HEADER_SET_ID
                storage->memoryTable[storage->usedStorage] = dataSet;
                ++storage->usedStorage;

                // HEADER_DATA_OFFSET
                storage->memoryTable[storage->usedStorage] = storage->usedStorage + HEADER_SIZE - HEADER_DATA_OFFSET;
                ++storage->usedStorage;

                // HEADER_DATA_LENGTH
                storage->memoryTable[storage->usedStorage] = size;
                ++storage->usedStorage;

                *data = &storage->memoryTable[storage->usedStorage];
                storage->usedStorage += size / sizeof(uint32);

                dataStorage[dataSet].dataEntries[storage->entryCount]    = data;
                dataStorage[dataSet].storageEntries[storage->entryCount] = *data;

                ++storage->entryCount;
            }
            else {
                // We've run out of room, so perform defragmentation and garbage-collection.
                DefragmentAndGarbageCollectStorage(dataSet);

                // If there is now room, then perform allocation.
                // Yes, this really is a massive chunk of duplicate code.
#if !RETRO_USE_ORIGINAL_CODE
                if (storage->usedStorage * sizeof(uint32) + size + (HEADER_SIZE * sizeof(uint32)) < storage->storageLimit) {
#else
                if (storage->usedStorage * sizeof(uint32) + size < storage->storageLimit) {
#endif
                    // HEADER_ACTIVE
                    storage->memoryTable[storage->usedStorage] = true;
                    ++storage->usedStorage;

                    // HEADER_SET_ID
                    storage->memoryTable[storage->usedStorage] = dataSet;
                    ++storage->usedStorage;

                    // HEADER_DATA_OFFSET
                    storage->memoryTable[storage->usedStorage] = storage->usedStorage + HEADER_SIZE - HEADER_DATA_OFFSET;
                    ++storage->usedStorage;

                    // HEADER_DATA_LENGTH
                    storage->memoryTable[storage->usedStorage] = size;
                    ++storage->usedStorage;

                    *data = &storage->memoryTable[storage->usedStorage];
                    storage->usedStorage += size / sizeof(uint32);

                    dataStorage[dataSet].dataEntries[storage->entryCount]    = data;
                    dataStorage[dataSet].storageEntries[storage->entryCount] = *data;

                    ++storage->entryCount;
                }
            }

            // If there are too many storage entries, then perform garbage collection.
            if (storage->entryCount >= STORAGE_ENTRY_CAP(storage))
                GarbageCollectStorage(dataSet);

            // Clear the allocated memory if requested.
            if (*data != NULL && clear == (bool32)true)
                memset(*data, 0, size);
        }
    }
}

void RSDK::RemoveStorageEntry(void **dataPtr)
{
    if (dataPtr != NULL && *dataPtr != NULL) {
        uint32 *data = *(uint32 **)dataPtr;

        uint32 set = HEADER(data, HEADER_SET_ID);
        for (int32 e = 0; e < dataStorage[set].entryCount; ++e) {
#if !RETRO_USE_ORIGINAL_CODE
            // make sure dataEntries[e] isn't null. If it is null by some ungodly chance then it was prolly already freed or something idk
            if (dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[e] && *dataPtr == *dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[e]) {
                *dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[e] = NULL;
                dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[e]  = NULL;
            }
#else
            if (*dataPtr == *dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[e]) {
                *dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[e] = NULL;
                dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[e]  = NULL;
            }
#endif

            set = HEADER(data, HEADER_SET_ID);
        }

        uint32 newEntryCount = 0;
        set                  = HEADER(data, HEADER_SET_ID);
        for (uint32 entryID = 0; entryID < dataStorage[set].entryCount; ++entryID) {
            if (dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[entryID]) {
                if (entryID != newEntryCount) {
                    dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[newEntryCount] =
                        dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[entryID];
                    dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[entryID] = NULL;
                    dataStorage[HEADER(data, HEADER_SET_ID)].storageEntries[newEntryCount] =
                        dataStorage[HEADER(data, HEADER_SET_ID)].storageEntries[entryID];
                    dataStorage[HEADER(data, HEADER_SET_ID)].storageEntries[entryID] = NULL;
                }

                ++newEntryCount;
            }

            set = HEADER(data, HEADER_SET_ID);
        }

        dataStorage[HEADER(data, HEADER_SET_ID)].entryCount = newEntryCount;

        for (uint32 e = newEntryCount; e < STORAGE_ENTRY_CAP(&dataStorage[HEADER(data, HEADER_SET_ID)]); ++e) {
            dataStorage[HEADER(data, HEADER_SET_ID)].dataEntries[e]    = NULL;
            dataStorage[HEADER(data, HEADER_SET_ID)].storageEntries[e] = NULL;
        }

        HEADER(data, HEADER_ACTIVE) = false;
    }
}

// This defragments the storage, leaving all empty space at the end.
void RSDK::DefragmentAndGarbageCollectStorage(StorageDataSets set)
{
    uint32 processedStorage = 0;
    uint32 unusedStorage    = 0;

    uint32 *defragmentDestination = dataStorage[set].memoryTable;
    uint32 *currentHeader         = dataStorage[set].memoryTable;

    ++dataStorage[set].clearCount;

    // Perform garbage-collection. This deallocates all memory allocations that are no longer being used.
    GarbageCollectStorage(set);

    // This performs defragmentation. It works by removing 'gaps' between the various blocks of allocated memory,
    // grouping them all together at the start of the buffer while all the empty space goes at the end.
    // Avoiding fragmentation is important, as fragmentation can cause allocations to fail despite there being
    // enough free memory because that free memory isn't contiguous.
    while (processedStorage < dataStorage[set].usedStorage) {
        uint32 *dataPtr = &dataStorage[set].memoryTable[currentHeader[HEADER_DATA_OFFSET]];
        uint32 size     = (currentHeader[HEADER_DATA_LENGTH] / sizeof(uint32)) + HEADER_SIZE;

        // Check if this block of memory is currently allocated.
        currentHeader[HEADER_ACTIVE] = false;

        for (int32 e = 0; e < dataStorage[set].entryCount; ++e)
            if (dataPtr == dataStorage[set].storageEntries[e])
                currentHeader[HEADER_ACTIVE] = true;

        if (currentHeader[HEADER_ACTIVE]) {
            // This memory is being used.
            processedStorage += size;

            if (currentHeader > defragmentDestination) {
                // This memory has a gap before it, so move it backwards into that free space.
                for (uint32 i = 0; i < size; ++i) *defragmentDestination++ = *currentHeader++;
            }
            else {
                // This memory doesn't have a gap before it, so we don't need to move it - just skip it instead.
                defragmentDestination += size;
                currentHeader += size;
            }
        }
        else {
            // This memory is not being used, so skip it.
            currentHeader += size;
            processedStorage += size;
            unusedStorage += size;
        }
    }

    // If defragmentation occurred, then we need to update every single
    // pointer to allocated memory to point to their new locations in the buffer.
    if (unusedStorage != 0) {
        dataStorage[set].usedStorage -= unusedStorage;

        uint32 *currentHeader = dataStorage[set].memoryTable;

        uint32 dataOffset = 0;
        while (dataOffset < dataStorage[set].usedStorage) {
            uint32 *dataPtr = &dataStorage[set].memoryTable[currentHeader[HEADER_DATA_OFFSET]];
            uint32 size     = (currentHeader[HEADER_DATA_LENGTH] / sizeof(uint32)) + HEADER_SIZE; // size (in int32s)

            // Find every single pointer to this memory allocation and update them with its new address.
            for (int32 c = 0; c < dataStorage[set].entryCount; ++c)

#if !RETRO_USE_ORIGINAL_CODE
                // make sure dataEntries[e] isn't null. If it is null by some ungodly chance then it was prolly already freed or something idk
                if (dataPtr == dataStorage[set].storageEntries[c] && dataStorage[set].dataEntries[c])
                    dataStorage[set].storageEntries[c] = *dataStorage[set].dataEntries[c] = currentHeader + HEADER_SIZE;
#else
                if (dataPtr == dataStorage[set].storageEntries[c])
                    dataStorage[set].storageEntries[c] = *dataStorage[set].dataEntries[c] = currentHeader + HEADER_SIZE;
#endif


            // Update the offset in the allocation's header too.
            currentHeader[HEADER_DATA_OFFSET] = dataOffset + HEADER_SIZE;

            // Advance to the next memory allocation.
            currentHeader += size;
            dataOffset += size;
        }
    }
}

void RSDK::CopyStorage(uint32 **src, uint32 **dst)
{
    if (dst != NULL) {
        uint32 *dstPtr = *dst;
        *src           = *dst;

        if (dataStorage[HEADER(dstPtr, HEADER_SET_ID)].entryCount < STORAGE_ENTRY_CAP(&dataStorage[HEADER(dstPtr, HEADER_SET_ID)])) {
            dataStorage[HEADER(dstPtr, HEADER_SET_ID)].dataEntries[dataStorage[HEADER(dstPtr, HEADER_SET_ID)].entryCount]    = src;
            dataStorage[HEADER(dstPtr, HEADER_SET_ID)].storageEntries[dataStorage[HEADER(dstPtr, HEADER_SET_ID)].entryCount] = *src;

            ++dataStorage[HEADER(dstPtr, HEADER_SET_ID)].entryCount;

            if (dataStorage[HEADER(dstPtr, HEADER_SET_ID)].entryCount >= STORAGE_ENTRY_CAP(&dataStorage[HEADER(dstPtr, HEADER_SET_ID)]))
                GarbageCollectStorage((StorageDataSets)HEADER(dstPtr, HEADER_SET_ID));
        }
    }
}

void RSDK::GarbageCollectStorage(StorageDataSets set)
{
    if ((uint32)set < DATASET_MAX) {
        for (uint32 e = 0; e < dataStorage[set].entryCount; ++e) {
            // So what's happening here is the engine is checking to see if the storage entry
            // (which is the pointer to the "memoryTable" offset that is allocated for this entry)
            // matches what the actual variable that allocated the storage is currently pointing to.
            // if they don't match, the storage entry is considered invalid and marked for removal.

            if (dataStorage[set].dataEntries[e] != NULL && *dataStorage[set].dataEntries[e] != dataStorage[set].storageEntries[e])
                dataStorage[set].dataEntries[e] = NULL;
        }

        uint32 newEntryCount = 0;
        for (uint32 entryID = 0; entryID < dataStorage[set].entryCount; ++entryID) {
            if (dataStorage[set].dataEntries[entryID]) {
                if (entryID != newEntryCount) {
                    dataStorage[set].dataEntries[newEntryCount]    = dataStorage[set].dataEntries[entryID];
                    dataStorage[set].dataEntries[entryID]          = NULL;
                    dataStorage[set].storageEntries[newEntryCount] = dataStorage[set].storageEntries[entryID];
                    dataStorage[set].storageEntries[entryID]       = NULL;
                }

                ++newEntryCount;
            }
        }
        dataStorage[set].entryCount = newEntryCount;

        for (int32 e = dataStorage[set].entryCount; e < (int32)STORAGE_ENTRY_CAP(&dataStorage[set]); ++e) {
            dataStorage[set].dataEntries[e]    = NULL;
            dataStorage[set].storageEntries[e] = NULL;
        }
    }
}
