// The contents of this file are subject to the Common Public Attribution
// License Version 1.0 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// https://opensource.org/license/cpal-1-0. The License is based on the
// Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
// to cover use of software over a computer network and provide for limited
// attribution for the Original Developer. In addition, Exhibit A has been
// modified to be consistent with Exhibit B.
//
// Software distributed under the License is distributed on an "AS IS" basis,
// WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
// for the specific language governing rights and limitations under the
// License.
//
// The Original Code is Fluxion Engine.
//
// The Original Developer is not the Initial Developer and is __________. If
// left blank, the Original Developer is the Initial Developer.
//
// The Initial Developer of the Original Code is Kiss Tibor Péter. All
// portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
// All Rights Reserved.
//
// Contributor ______________________.
//
// Alternatively, the contents of this file may be used under the terms of
// the Fluxion Engine Commercial License Agreement Version 1.0, separately
// obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
// License"), in which case the provisions of the Commercial License are
// applicable instead of those above.
//
// If you wish to allow use of your version of this file only under the terms
// of the Commercial License and not to allow others to use your version of
// this file under the CPAL, indicate your decision by deleting the
// provisions above and replace them with the notice and other provisions
// required by the Commercial License. If you do not delete the provisions
// above, a recipient may use your version of this file under either the CPAL
// or the Commercial License.
//
// SPDX-License-Identifier: CPAL-1.0

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

    // How many slots the object was created with. For a sequence that is
    // its element count, which is per-object rather than per-class and so
    // has nowhere else to live. False when the handle names nothing live.
    bool TrySlotCount(ObjectHandle handle, u32& outCount) const;

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
