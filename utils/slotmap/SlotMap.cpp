
#include "SlotMap.h"
#include <assert.h>

/*
Bit layout

  bits 0–14 : ID(15 bits)
  bits 15–29 : Version(15 bits)
  bit 30 : Deleted bit 31
  bit 31 : Reserved
*/

#define HANDLE_ID_BITS      15
#define HANDLE_VERSION_BITS 15
#define HANDLE_DELETE_BIT   1

// shift 15 bits of length, minus 1 to get mask
#define HANDLE_ID_MASK      ((1 << HANDLE_ID_BITS) - 1)
#define HANDLE_VERSION_MASK ((1 << HANDLE_VERSION_BITS) - 1)

ushort GetHandleID(ObjectHandle handle)
{ return (handle & HANDLE_ID_MASK); }

ushort GetRecycleVersion(ObjectHandle handle)
{ return (handle >> HANDLE_ID_BITS) & HANDLE_VERSION_MASK; }

bool DeletedBit(ObjectHandle handle)
{
  uint b = (handle >> (HANDLE_ID_BITS + HANDLE_VERSION_BITS)) & HANDLE_DELETE_BIT;
  return b != 0;
}

ObjectHandle MarkDeleted(ObjectHandle handle, bool del)
{
  if (del)
    handle = handle | (1 << (HANDLE_ID_BITS + HANDLE_VERSION_BITS));
  else
  {
    int mask = (1 << (HANDLE_ID_BITS + HANDLE_VERSION_BITS)) - 1u;
    handle = handle & mask;
  }
  return handle;
}

ObjectHandle Make_ObjectHandle(ushort id, ushort recycle_ver)
{
  uint id_portion = (uint)(id & HANDLE_ID_MASK);
  uint recycle_portion = ((uint)(recycle_ver & HANDLE_VERSION_MASK) << HANDLE_ID_BITS);
  ObjectHandle h(recycle_portion | id_portion);
  return h;
}

/**** SlotMap implementation ****/

SlotMap::SlotMap() {}

void SlotMap::Reset()
{
  mTracker.Reset();
  mObjectIdHash.clear();
}

ObjectHandle SlotMap::GrantHandle(void* obj)
{
  ObjectHandle newhandle = mTracker.GrantHandle();
  mObjectIdHash.insert(std::make_pair(newhandle, obj));
  return newhandle;
}

void SlotMap::RecycleHandle(ObjectHandle id)
{
  if (id == NULL_OBJECTHANDLE)
    return;
  mTracker.RecycleHandle(id);
  mObjectIdHash.erase(id);
}

void SlotMap::ReserveHandle(ObjectHandle handle)
{ ReserveId(GetHandleID(handle)); }

void SlotMap::OccupyHandle(ObjectHandle handle, void* obj)
{
  ReserveId(GetHandleID(handle));

  auto it = mObjectIdHash.find(handle);
  if (it != mObjectIdHash.end())
  {
    assert(0 || printf("Handle %d already in use!", handle));
  }
  mObjectIdHash.insert(std::make_pair(handle, obj));
}

void SlotMap::ReserveId(ushort id)
{
  if (mTracker.mHighestUnrecycledId <= id)
    mTracker.mHighestUnrecycledId = id + 1;
}
