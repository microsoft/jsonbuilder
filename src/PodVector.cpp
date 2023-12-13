// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <jsonbuilder/JsonBuilder.h>

#include <cassert>
#include <cstring>

namespace jsonbuilder { namespace JsonInternal {

void PodVectorBase::CheckOffset(size_type index, size_type currentSize) noexcept
{
    (void) index;        // Unreferenced parameter
    (void) currentSize;  // Unreferenced parameter
    assert(index < currentSize);
}

void PodVectorBase::CheckRange(void const* p1, void const* p2, void const* p3) noexcept
{
    (void) p1;  // Unreferenced parameter
    (void) p2;  // Unreferenced parameter
    (void) p3;  // Unreferenced parameter
    assert(p1 <= p2 && p2 <= p3);
}

PodVectorBase::size_type PodVectorBase::CheckedAdd(size_type a, size_type b)
{
    size_type c = a + b;
    if (c < a)
    {
        JsonThrowLengthError("JsonVector - exceeded maximum capacity");
    }
    return c;
}

void PodVectorBase::InitData(
    _Out_writes_bytes_(cb) void* pDest,
    _In_reads_bytes_(cb) void const* pSource,
    std::size_t cb) noexcept
{
    memcpy(pDest, pSource, cb);
}

PodVectorBase::size_type
PodVectorBase::GetNewCapacity(size_type minCapacity, size_type maxCapacity)
{
    size_type cap;
    
    if (minCapacity <= 15)
    {
        cap = 15;
    }
    else
    {
#ifdef __has_builtin
#if __has_builtin(__builtin_clz)
#define HAS_BUILTIN_CLZ 1
#endif
#endif
#if defined(HAS_BUILTIN_CLZ)
        cap = 0xFFFFFFFF >> __builtin_clz(minCapacity);
#elif defined(_MSC_VER)
        unsigned long index;
        _BitScanReverse(&index, minCapacity);
        cap = (2u << index) - 1;
#else
        cap = 31;
        while (cap < minCapacity)
        {
            cap += cap + 1; // Always one less than a power of 2. Cannot overflow.
        }
#endif
    }

    if (maxCapacity < cap)
    {
        if (maxCapacity < minCapacity)
        {
            JsonThrowLengthError("JsonVector - exceeded maximum capacity");
        }

        cap = maxCapacity;
    }

    assert(15 <= cap);
    assert(minCapacity <= cap);
    assert(cap <= maxCapacity);
    return cap;
}

void* PodVectorBase::Allocate(size_t cb, bool zeroInitializeMemory)
{
    void* const pbNew = malloc(cb);

    if (pbNew == nullptr)
    {
        JsonThrowBadAlloc();
    }

    if (zeroInitializeMemory)
    {
        memset(pbNew, 0, cb);
    }

    return pbNew;
}

void PodVectorBase::Deallocate(void* pb) noexcept
{
    if (pb)
    {
        free(pb);
    }
}

}}
