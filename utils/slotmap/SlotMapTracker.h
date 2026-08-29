#pragma once

#include "prereqs.h"
#include <vector>

// Utility functions for ObjectHandle manipulation
extern ushort GetHandleID(ObjectHandle handle);
extern ushort GetRecycleVersion(ObjectHandle handle);
extern bool DeletedBit(ObjectHandle handle);
extern ObjectHandle MarkDeleted(ObjectHandle handle, bool del);
extern ObjectHandle Make_ObjectHandle(ushort id, ushort recycle_ver);

// Slotmap component with only the functionality to keep track of handles.
// Up to users to manage storage.
class SlotMapTracker
{
public:
  SlotMapTracker() { mFreeIds.reserve(16); }

  ~SlotMapTracker() { Reset(); }

  void Reset()
  {
    mHighestUnrecycledId = 0;
    mFreeIds.clear();
  }

  ObjectHandle GrantHandle()
  {
    if (!mFreeIds.empty())
    {
      ObjectHandle handle = mFreeIds.back();
      mFreeIds.pop_back();

      ObjectHandle newhandle =
        Make_ObjectHandle(GetHandleID(handle), GetRecycleVersion(handle) + 1);
      return newhandle;
    }
    else
    {
      ObjectHandle newhandle = Make_ObjectHandle(mHighestUnrecycledId, 0);
      ++mHighestUnrecycledId;
      return newhandle;
    }
  }

  void RecycleHandle(ObjectHandle id)
  {
    if (id == NULL_OBJECTHANDLE)
      return;
    mFreeIds.emplace_back(id);
  }

  bool HandleInFreeList(ObjectHandle id)
  {
    for (uint i = 0; i < mFreeIds.size(); ++i)
    {
      if (mFreeIds[i] == id)
        return true;
    }
    return false;
  }

  ushort mHighestUnrecycledId = 0;  // id to be given if there are no freeids

  std::vector<ObjectHandle> mFreeIds;
};
