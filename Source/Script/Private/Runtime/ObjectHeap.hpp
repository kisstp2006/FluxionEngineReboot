#pragma once

#include <Fluxion/Foundation/Types.h>
#include <Fluxion/Script/Runtime/Bytecode.hpp>
#include <Fluxion/Script/Runtime/Value.hpp>

#include <unordered_map>
#include <vector>

namespace Fluxion::Script
{

// Where every object a script creates lives. Objects never move: a record
// keeps its index for the lifetime of the machine, and reclaiming one
// only raises its generation, so a reference held past the point its
// object went away is recognized instead of quietly reading whatever
// occupies the record now.
//
// The collector is mark-and-sweep, which is what lets a group of objects
// that only reference each other be reclaimed -- counting references
// would keep such a group alive forever. Marking is driven from outside:
// the caller hands in the roots it knows about, and the heap follows each
// class's field bitmap from there.
class ObjectHeap
{
public:
    ObjectHeap();

    // Returns a null handle when the class index is not one this heap was
    // told about, which the caller reports as a fault.
    ObjectHandle Allocate(u32 classIndex, u32 fieldCount);

    bool IsAlive(ObjectHandle handle) const;

    // kNoClass when the handle does not name a live object.
    u32 ClassOf(ObjectHandle handle) const;

    bool ReadField(ObjectHandle handle, u32 slot, Slot& outValue) const;
    bool WriteField(ObjectHandle handle, u32 slot, Slot value);

    // A collection is three steps: clear every mark, mark from each root,
    // then reclaim whatever stayed unmarked.
    void BeginCollection();
    void Mark(ObjectHandle handle, const std::vector<ClassInfo>& classes);
    u32 Sweep();

    u32 LiveObjects() const { return m_liveObjects; }
    u64 TotalAllocations() const { return m_totalAllocations; }
    u32 CollectionCount() const { return m_collectionCount; }
    u32 AllocationsSinceCollection() const { return m_allocationsSinceCollection; }

private:
    struct Record
    {
        u32 generation = 0;
        u32 classIndex = kNoClass;
        u32 fieldOffset = 0;
        u32 fieldCount = 0;
        bool live = false;
        bool marked = false;
    };

    // Index into m_records, or 0 when the handle names nothing live.
    u32 Resolve(ObjectHandle handle) const;

    std::vector<Record> m_records;
    std::vector<Slot> m_fieldStorage;

    // Reclaimed records, grouped by how many field slots they own, so a
    // class of the same shape can take one over whole rather than growing
    // the storage again.
    std::unordered_map<u32, std::vector<u32>> m_freeRecords;

    // Marking worklist, kept as a member so a deep object graph costs no
    // host stack.
    std::vector<u32> m_greyList;

    u32 m_liveObjects = 0;
    u64 m_totalAllocations = 0;
    u32 m_collectionCount = 0;
    u32 m_allocationsSinceCollection = 0;
};

} // namespace Fluxion::Script
