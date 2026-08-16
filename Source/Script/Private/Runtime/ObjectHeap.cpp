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

#include <Runtime/ObjectHeap.hpp>

namespace Fluxion::Script
{

ObjectHeap::ObjectHeap()
{
    // Record zero is never handed out: it is what makes an all-zero slot
    // mean null, so a frame slot or object field that was never written
    // already reads as "no object".
    m_records.emplace_back();
}

u32 ObjectHeap::Resolve(ObjectHandle handle) const
{
    if (handle.index == 0 || handle.index >= m_records.size()) return 0;
    const Record& record = m_records[handle.index];
    if (!record.live || record.generation != handle.generation) return 0;
    return handle.index;
}

ObjectHandle ObjectHeap::Allocate(u32 classIndex, u32 fieldCount)
{
    u32 index = 0;

    auto reusable = m_freeRecords.find(fieldCount);
    if (reusable != m_freeRecords.end() && !reusable->second.empty())
    {
        index = reusable->second.back();
        reusable->second.pop_back();
    }
    else
    {
        index = (u32)m_records.size();
        Record created;
        created.fieldOffset = (u32)m_fieldStorage.size();
        created.fieldCount = fieldCount;
        m_records.push_back(created);
        m_fieldStorage.resize(m_fieldStorage.size() + fieldCount);
    }

    Record& record = m_records[index];
    record.classIndex = classIndex;
    record.live = true;
    record.marked = false;
    if (record.generation == 0) record.generation = 1;

    // Every field starts zeroed, which is already what the language says
    // an unassigned field of any type holds: zero, false, empty text or
    // null.
    for (u32 i = 0; i < record.fieldCount; ++i)
        m_fieldStorage[(size_t)record.fieldOffset + i] = Slot{};

    ++m_liveObjects;
    ++m_totalAllocations;
    ++m_allocationsSinceCollection;

    return ObjectHandle{ index, record.generation };
}

bool ObjectHeap::IsAlive(ObjectHandle handle) const
{
    return Resolve(handle) != 0;
}

u32 ObjectHeap::ClassOf(ObjectHandle handle) const
{
    const u32 index = Resolve(handle);
    if (index == 0) return kNoClass;
    return m_records[index].classIndex;
}

bool ObjectHeap::ReadField(ObjectHandle handle, u32 slot, Slot& outValue) const
{
    const u32 index = Resolve(handle);
    if (index == 0) return false;

    const Record& record = m_records[index];
    if (slot >= record.fieldCount) return false;

    outValue = m_fieldStorage[(size_t)record.fieldOffset + slot];
    return true;
}

bool ObjectHeap::WriteField(ObjectHandle handle, u32 slot, Slot value)
{
    const u32 index = Resolve(handle);
    if (index == 0) return false;

    const Record& record = m_records[index];
    if (slot >= record.fieldCount) return false;

    m_fieldStorage[(size_t)record.fieldOffset + slot] = value;
    return true;
}

bool ObjectHeap::TrySlotCount(ObjectHandle handle, u32& outCount) const
{
    const u32 index = Resolve(handle);
    if (index == 0) return false;

    outCount = m_records[index].fieldCount;
    return true;
}

void ObjectHeap::BeginCollection()
{
    for (Record& record : m_records) record.marked = false;
    m_greyList.clear();
}

void ObjectHeap::Mark(ObjectHandle handle, const std::vector<ClassInfo>& classes)
{
    const u32 root = Resolve(handle);
    if (root == 0 || m_records[root].marked) return;

    m_records[root].marked = true;
    m_greyList.push_back(root);

    while (!m_greyList.empty())
    {
        const u32 index = m_greyList.back();
        m_greyList.pop_back();

        const Record& record = m_records[index];
        if (record.classIndex >= classes.size()) continue;

        // The class says which of its field slots hold a reference, so
        // nothing else in the object is ever mistaken for one. A sequence
        // has no named slots to describe: every one of its elements is
        // the same type, so either all of them are followed or none are.
        const ClassInfo& classInfo = classes[record.classIndex];
        if (classInfo.isArray && !classInfo.elementIsReference) continue;

        for (u32 slot = 0; slot < record.fieldCount; ++slot)
        {
            if (!classInfo.isArray && !IsReferenceBitSet(classInfo.fieldReferenceBits, slot)) continue;

            const ObjectHandle field = SlotAsObject(m_fieldStorage[(size_t)record.fieldOffset + slot]);
            const u32 target = Resolve(field);
            if (target == 0 || m_records[target].marked) continue;

            m_records[target].marked = true;
            m_greyList.push_back(target);
        }
    }
}

u32 ObjectHeap::Sweep()
{
    u32 reclaimed = 0;

    for (u32 index = 1; index < (u32)m_records.size(); ++index)
    {
        Record& record = m_records[index];
        if (!record.live || record.marked) continue;

        record.live = false;

        // Raising the generation is what turns every reference still
        // naming this record into one that reports itself as stale.
        ++record.generation;
        if (record.generation == 0) record.generation = 1;

        m_freeRecords[record.fieldCount].push_back(index);
        ++reclaimed;
    }

    m_liveObjects -= reclaimed;
    ++m_collectionCount;
    m_allocationsSinceCollection = 0;
    return reclaimed;
}

} // namespace Fluxion::Script
