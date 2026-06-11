#ifndef STORAGE_H
#define STORAGE_H

namespace RSDK
{
#if RETRO_PLATFORM == RETRO_SATURN
// P6.7c (Task #210) data retarget, same class as the P4 x4->x1 flips: the two
// entry arrays below are 4 B/slot each, so 0x1000 slots = 32,788 B per
// DataStorage x 5 datasets = 163,940 B of WRAM-L bookkeeping. The Title proof
// path allocates < 100 entries per pool and GHZ-scale stays in the low
// hundreds (sheets/anims/layers/sfx/staticvars -- entities do NOT allocate
// per-instance); 0x800 (2048) slots halve the bookkeeping to 16,404 B per
// dataset. Storage.cpp's GC/defrag loops are bounded by entryCount, not the
// array size, so no 0x1000 assumption exists outside this define.
#define STORAGE_ENTRY_COUNT (0x800)
// P6.7 W11 closer C1 (Task #210): PER-DATASET entry capacities. Only
// DATASET_STG carries hundreds of live allocations (sheets/anims/layers/
// statics); MUS/SFX/STR/TMP peak in the tens (diag-measured < 100 each).
// The uniform 0x800 costs 5 x 16,404 = 82,020 B of bookkeeping; per-dataset
// (STG 0x800, others 0x100) costs 160 B of structs + 24,576 B of backings
// = 24,736 B -- the P68_LWRAM_DATASTORAGE_PLANNED contract line. The entry
// arrays become POINTERS into backings carved by InitStorage from the same
// dataStorage window; every indexing site compiles identically, and the
// existing append bounds checks compare against entryCapacity via
// STORAGE_ENTRY_CAP below (overflow stays checked, now per-dataset).
#define STORAGE_ENTRY_COUNT_STG   (0x800)
#define STORAGE_ENTRY_COUNT_SMALL (0x100)
#else
#define STORAGE_ENTRY_COUNT (0x1000)
#endif

enum StorageDataSets {
    DATASET_STG = 0,
    DATASET_MUS = 1,
    DATASET_SFX = 2,
    DATASET_STR = 3,
    DATASET_TMP = 4,
    DATASET_MAX, // used to signify limits
};

#if RETRO_PLATFORM == RETRO_SATURN
struct DataStorage {
    uint32 *memoryTable;
    uint32 usedStorage;
    uint32 storageLimit;
    uint32 ***dataEntries;   // -> backing[entryCapacity] (C1: was embedded)
    uint32 **storageEntries; // -> backing[entryCapacity]
    uint32 entryCapacity;    // C1: per-dataset bound for the append checks
    uint32 entryCount;
    uint32 clearCount;
};
#define STORAGE_ENTRY_CAP(storagePtr) ((storagePtr)->entryCapacity)
#else
struct DataStorage {
    uint32 *memoryTable;
    uint32 usedStorage;
    uint32 storageLimit;
    uint32 **dataEntries[STORAGE_ENTRY_COUNT];   // pointer to the actual variable
    uint32 *storageEntries[STORAGE_ENTRY_COUNT]; // pointer to the storage in "memoryTable"
    uint32 entryCount;
    uint32 clearCount;
};
#define STORAGE_ENTRY_CAP(storagePtr) (STORAGE_ENTRY_COUNT)
#endif

template <typename T> class List
{
    T *entries   = NULL;
    int32 count  = 0;
    int32 length = 0;

public:
    List()
    {
        entries = NULL;
        count   = 0;
    }
    ~List()
    {
        if (entries) {
            free(entries);
            entries = NULL;
        }
    }
    T *Append()
    {
        if (count == length) {
            length += 32;
            size_t len         = sizeof(T) * length;
            T *entries_realloc = (T *)realloc(entries, len);

            if (entries_realloc) {
                entries = entries_realloc;
            }
        }

        T *entry = &entries[count];
        memset(entry, 0, sizeof(T));
        count++;
        return entry;
    }
    void Remove(uint32 index)
    {
        // move every item back one
        for (int32 i = index; i < count; i++) {
            if (i + 1 < count) {
                entries[i] = entries[i + 1];
            }
            else { // Last Item, free it
                count--;
            }
        }

        if (count < length - 32) {
            length -= 32;
            size_t len         = sizeof(T) * length;
            T *entries_realloc = (T *)realloc(entries, len);

            if (entries_realloc)
                entries = entries_realloc;
        }
    }

    inline T *At(int32 index) { return &entries[index]; }

    inline void Clear(bool32 dealloc = false)
    {
        for (int32 i = count - 1; i >= 0; i--) {
            Remove(i);
        }

        if (entries && dealloc) {
            free(entries);
            entries = NULL;
        }
    }

    inline int32 Count() { return count; }
};

#if defined(P6_SCENE_TEST)
extern DataStorage *dataStorage; // P6.3: relocated to WRAM-L (pointer form), defined in p6_io_main.cpp
#else
extern DataStorage dataStorage[DATASET_MAX];
#endif

bool32 InitStorage();
void ReleaseStorage();

void AllocateStorage(void **dataPtr, uint32 size, StorageDataSets dataSet, bool32 clear);
void DefragmentAndGarbageCollectStorage(StorageDataSets set);
void RemoveStorageEntry(void **dataPtr);
void CopyStorage(uint32 **src, uint32 **dst);
void GarbageCollectStorage(StorageDataSets dataSet);

#if RETRO_REV0U
#include "Legacy/UserStorageLegacy.hpp"
#endif

} // namespace RSDK

#endif // STORAGE_H
