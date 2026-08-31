#pragma once

#include "prereqs.h"
#include "SlotMapTracker.h"
#include <unordered_map>

// SlotMap with unordered_map as container for tracking of pointers
class SlotMap
{
public:
  SlotMap();

  void Reset();

  ObjectHandle GrantHandle(void* obj);
  void RecycleHandle(ObjectHandle id);

  void ReserveHandle(ObjectHandle handle);
  void OccupyHandle(ObjectHandle handle, void* obj);
  void ReserveId(ushort id);

  template<typename T>
  T* GetObjectByHandle(ObjectHandle id)
  {
    if (id == NULL_OBJECTHANDLE)
      return nullptr;
    auto it = mObjectIdHash.find(id);
    if (it != mObjectIdHash.end())
      return (T*)it->second;
    return nullptr;
  }

private:
  SlotMapTracker mTracker;

  std::unordered_map<ObjectHandle, void*> mObjectIdHash;
};

// SlotMap that combines ObjectHandles with std::vector for mem management.
// * T must have a member `ObjectHandle mHandle`.
// * iteration through the Data vector needs to check for Deleted flag
// eg. DeletedBit(handle)
template<typename T>
class SSlotMap
{
public:
  SSlotMap() { mData.reserve(16); }

  ~SSlotMap() {}

  void Clear()
  {
    mTracker.Reset();
    mData.clear();
  }

  ObjectHandle AddAndGetHandle() { return Add()->mHandle; }

  T* Add()
  {
    T* ret = nullptr;
    ObjectHandle handle = mTracker.GrantHandle();
    ushort arrayidx = GetHandleID(handle);
    if (arrayidx < (ushort)mData.size())
      ret = &mData[arrayidx];
    else
    {
      mData.push_back(T());
      ret = &mData[mData.size() - 1];
    }

    *ret = T();
    ret->mHandle = handle;
    return ret;
  }

  T* Get(ObjectHandle handle)
  {
    if (handle == NULL_OBJECTHANDLE)
      return nullptr;

    T* ret = nullptr;
    ushort arrayidx = GetHandleID(handle);
    if (arrayidx < (ushort)mData.size())
    {
      if (mData[arrayidx].mHandle == handle)
        ret = &mData[arrayidx];
    }

    // Warn
    /*if (!ret)
      LOG("Item %d not found", handle);*/
    return ret;
  }

  void Remove(ObjectHandle handle)
  {
    if (handle == NULL_OBJECTHANDLE)
      return;

    ushort arrayidx = GetHandleID(handle);
    if (arrayidx < (ushort)mData.size())
    {
      if (mData[arrayidx].mHandle == handle)
      {
        ObjectHandle newhandle =
          Make_ObjectHandle(GetHandleID(handle), GetRecycleVersion(handle) + 1);

        mTracker.mFreeIds.push_back(newhandle);

        newhandle = MarkDeleted(newhandle, true);
        mData[arrayidx].mHandle = newhandle;
      }
    }
  }

  // use size() for iteration with DeletedBit(ent->mHandle) to check if valid
  uint size() { return (uint)mData.size(); }
  // gets a count of all entries, excluding deleted ones
  uint count() { return mData.size() - mTracker.mFreeIds.size(); }

  // helper for iteration, remember to check for nullptr
  T* operator[](uint index)
  {
    if (index >= mData.size())
      return nullptr;

    T* ret = &mData[index];
    if (DeletedBit(ret->mHandle))
      return nullptr;
    return ret;
  }

  SlotMapTracker& GetTracker() { return mTracker; }
  std::vector<T>& GetData() { return mData; }

private:  // only for serialization/deserialization
  SlotMapTracker mTracker;
  std::vector<T> mData;
};
