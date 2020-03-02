// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <cassert>

#include <jsonbuilder/JsonBuilder.h>

namespace jsonbuilder { namespace JsonInternal {

static inline unsigned char
BitScanReverse(unsigned long* Index, unsigned long Mask)
{
    if (Mask == 0 || Index == 0)
        return 0;

    int ii = 0;
    for (ii = ((sizeof(Mask) * 8) - 1); ii >= 0; --ii)
    {
        unsigned long tempMask = 1ul << ii;
        if ((Mask & tempMask) != 0)
        {
            *Index = ii;
            break;
        }
    }
    return (ii >= 0 ? (unsigned char) 1 : (unsigned char) 0);
}

void PodVectorBase::CheckOffset(size_type index, size_type currentSize) throw()
{
    (void) index;        // Unreferenced parameter
    (void) currentSize;  // Unreferenced parameter
    assert(index < currentSize);
}

void PodVectorBase::CheckRange(void const* p1, void const* p2, void const* p3) throw()
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
        throw std::length_error("JsonVector - exceeded maximum capacity");
    }
    return c;
}

void PodVectorBase::InitData(void* pDest, void const* pSource, size_t cb) throw()
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
        long unsigned index = 0;
        if (!BitScanReverse(&index, minCapacity))
        {
            std::terminate();
        }
        cap = static_cast<uint32_t>((2 << index) - 1);
    }

    if (maxCapacity < cap)
    {
        if (maxCapacity < minCapacity)
        {
            throw std::length_error("JsonVector - exceeded maximum capacity");
        }

        cap = maxCapacity;
    }

    assert(15 <= cap);
    assert(minCapacity <= cap);
    assert(cap <= maxCapacity);
    return cap;
}

void* PodVectorBase::Reallocate(void* pb, size_t cb, bool zeroInitializeMemory)
{
    void* const pbNew = pb ? realloc(pb, cb) : malloc(cb);

    if (pbNew == nullptr)
    {
        throw std::bad_alloc();
    }

    if (zeroInitializeMemory)
    {
        memset(pbNew, 0, cb);
    }

    return pbNew;
}

void PodVectorBase::Deallocate(void* pb) throw()
{
    if (pb)
    {
        free(pb);
    }
}

}}
