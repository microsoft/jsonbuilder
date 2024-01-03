// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <jsonbuilder/JsonBuilder.h>

#include <cassert>
#include <cstring>

#ifndef _Out_writes_to_
#define _Out_writes_to_(count, written)
#endif

#define StorageSize sizeof(JsonValue::StoragePod)

/*
Computes the difference between a value's index and the index at which its
data begins. Used to determine the DataIndex for a value:
value.DataIndex = value.Index + DATA_OFFSET(value.cchName).
*/
#define DATA_OFFSET(cchName) ( \
    ((cchName) + static_cast<unsigned>(sizeof(JsonValue) + (StorageSize - 1))) \
    / static_cast<unsigned>(StorageSize) \
    )

#define IS_SPECIAL_TYPE(type) (JsonHidden <= (type))
#define IS_NORMAL_TYPE(type) ((type) < JsonHidden)
#define IS_COMPOSITE_TYPE(type) (JsonArray <= (type))

auto constexpr NameMax = 0xFFFFFFu;
auto constexpr DataMax = 0xF0000000u;

auto constexpr TicksPerSecond = 10'000'000u;
auto constexpr FileTime1970Ticks = 116444736000000000u;
using ticks = std::chrono::duration<std::int64_t, std::ratio<1, TicksPerSecond>>;

static unsigned
Utf16ToUtf8(
    _Out_writes_to_(cchSrc * 3, return) char unsigned* pchDest,
    _In_reads_(cchSrc) char16_t const* pchSrc,
    unsigned cchSrc)
    noexcept
{
    unsigned iDest = 0;
    for (unsigned iSrc = 0; iSrc != cchSrc; iSrc += 1)
    {
        auto const ch = pchSrc[iSrc];
        if (ch < 0x80)
        {
            pchDest[iDest++] = static_cast<char unsigned>(ch);
        }
        else if (ch < 0x800)
        {
            pchDest[iDest++] = static_cast<char unsigned>(0xC0 | (ch >> 6));
            pchDest[iDest++] = static_cast<char unsigned>(0x80 | (ch & 0x3F));
        }
        else if (ch >= 0xD800 && ch < 0xDC00 &&
            iSrc + 1 != cchSrc &&
            pchSrc[iSrc + 1] >= 0xDC00 && pchSrc[iSrc + 1] < 0xE000)
        {
            // Surrogate pair.
            iSrc += 1;
            auto const ch32 = ((ch - 0xD800) << 10) + (pchSrc[iSrc] - 0xDC00) + 0x10000;
            pchDest[iDest++] = static_cast<char unsigned>(0xF0 | (ch32 >> 18));
            pchDest[iDest++] = static_cast<char unsigned>(0x80 | ((ch32 >> 12) & 0x3F));
            pchDest[iDest++] = static_cast<char unsigned>(0x80 | ((ch32 >> 6) & 0x3F));
            pchDest[iDest++] = static_cast<char unsigned>(0x80 | (ch32 & 0x3F));
        }
        else
        {
            pchDest[iDest++] = static_cast<char unsigned>(0xE0 | (ch >> 12));
            pchDest[iDest++] = static_cast<char unsigned>(0x80 | ((ch >> 6) & 0x3F));
            pchDest[iDest++] = static_cast<char unsigned>(0x80 | (ch & 0x3F));
        }
    }

    return iDest;
}

static unsigned
Utf32ToUtf8(
    _Out_writes_to_(cchSrc * 4, return) char unsigned* pchDest,
    _In_reads_(cchSrc) char32_t const* pchSrc,
    unsigned cchSrc)
    noexcept
{
    unsigned iDest = 0;
    for (unsigned iSrc = 0; iSrc != cchSrc; iSrc += 1)
    {
        auto const ch = pchSrc[iSrc];
        if (ch < 0x80)
        {
            pchDest[iDest++] = static_cast<char unsigned>(ch);
        }
        else if (ch < 0x800)
        {
            pchDest[iDest++] = static_cast<char unsigned>(0xC0 | (ch >> 6));
            pchDest[iDest++] = static_cast<char unsigned>(0x80 | (ch & 0x3F));
        }
        else if (ch < 0x10000)
        {
            pchDest[iDest++] = static_cast<char unsigned>(0xE0 | (ch >> 12));
            pchDest[iDest++] = static_cast<char unsigned>(0x80 | ((ch >> 6) & 0x3F));
            pchDest[iDest++] = static_cast<char unsigned>(0x80 | (ch & 0x3F));
        }
        else
        {
            // If ch is outside the Unicode range, we'll ignore the high bits.
            pchDest[iDest++] = static_cast<char unsigned>(0xF0 | (ch >> 18));
            pchDest[iDest++] = static_cast<char unsigned>(0x80 | ((ch >> 12) & 0x3F));
            pchDest[iDest++] = static_cast<char unsigned>(0x80 | ((ch >> 6) & 0x3F));
            pchDest[iDest++] = static_cast<char unsigned>(0x80 | (ch & 0x3F));
        }
    }

    return iDest;
}

// JsonValue

namespace jsonbuilder {
// Verify that our conditions like (type >= JsonArray) will work.
static_assert(JsonHidden == 253, "Incorrect enum numbering for JsonHidden");
static_assert(JsonArray == 254, "Incorrect enum numbering for JsonArray");
static_assert(JsonObject == 255, "Incorrect enum numbering for JsonObject");

static_assert(
    sizeof(unsigned) >= 4,
    "JsonValue assumes that unsigned is at least 32 bits");
static_assert(sizeof(JsonValueBase) == 8, "JsonValueBase changed size");
static_assert(sizeof(JsonValue) == 12, "JsonValue changed size");

JsonType JsonValue::Type() const noexcept
{
    return static_cast<JsonType>(m_type);
}

std::string_view JsonValue::Name() const noexcept
{
    return std::string_view(reinterpret_cast<char const*>(this + 1), m_cchName);
}

void const* JsonValue::Data(_Out_opt_ unsigned* pcbData) const noexcept
{
    return const_cast<JsonValue*>(this)->Data(pcbData);
}

void* JsonValue::Data(_Out_opt_ unsigned* pcbData) noexcept
{
    assert(!IS_SPECIAL_TYPE(m_type));  // Can't call Data() on hidden,
    // object, or array values.
    if (pcbData != nullptr)
    {
        *pcbData = m_cbData;
    }

    return reinterpret_cast<StoragePod*>(this) + DATA_OFFSET(m_cchName);
}

unsigned JsonValue::DataSize() const noexcept
{
    assert(!IS_SPECIAL_TYPE(m_type));  // Can't call DataSize() on hidden,
    // object, or array values.
    return m_cbData;
}

void JsonValue::ReduceDataSize(unsigned cbNew) noexcept
{
    if (IS_SPECIAL_TYPE(m_type) || cbNew > m_cbData)
    {
        assert(!"JsonBuilder: invalid use of ReduceDataSize().");
        std::terminate();
    }

    m_cbData = cbNew;
}

bool JsonValue::IsNull() const noexcept
{
    return m_type == JsonNull;
}

// JsonConstIterator

JsonConstIterator::JsonConstIterator() noexcept : m_pContainer(), m_index()
{
    return;
}

JsonConstIterator::JsonConstIterator(JsonBuilder const* pContainer, Index index) noexcept
    : m_pContainer(pContainer), m_index(index)
{
    return;
}

bool JsonConstIterator::operator==(JsonConstIterator const& other) const noexcept
{
    assert(m_pContainer == other.m_pContainer);
    return m_index == other.m_index;
}

bool JsonConstIterator::operator!=(JsonConstIterator const& other) const noexcept
{
    assert(m_pContainer == other.m_pContainer);
    return m_index != other.m_index;
}

JsonConstIterator::reference JsonConstIterator::operator*() const noexcept
{
    m_pContainer->AssertNotEnd(m_index);  // Do not dereference the end()
                                          // iterator.
    return m_pContainer->GetValue(m_index);
}

JsonConstIterator::pointer JsonConstIterator::operator->() const noexcept
{
    m_pContainer->AssertNotEnd(m_index);  // Do not dereference the end()
                                          // iterator.
    return &m_pContainer->GetValue(m_index);
}

JsonConstIterator& JsonConstIterator::operator++() noexcept
{
    m_index = m_pContainer->NextIndex(m_index);  // Implicitly asserts !end().
    return *this;
}

JsonConstIterator JsonConstIterator::operator++(int) noexcept
{
    auto old = *this;
    m_index = m_pContainer->NextIndex(m_index);  // Implicitly asserts !end().
    return old;
}

JsonConstIterator JsonConstIterator::begin() const noexcept
{
    return m_pContainer->begin(*this);
}

JsonConstIterator JsonConstIterator::end() const noexcept
{
    return m_pContainer->end(*this);
}

bool JsonConstIterator::IsRoot() const noexcept
{
    return m_index == 0;
}

// JsonIterator

JsonIterator::JsonIterator() noexcept : JsonConstIterator()
{
    return;
}

JsonIterator::JsonIterator(JsonConstIterator const& other) noexcept
    : JsonConstIterator(other)
{
    return;
}

JsonIterator::reference JsonIterator::operator*() const noexcept
{
    return const_cast<reference>(JsonConstIterator::operator*());
}

JsonIterator::pointer JsonIterator::operator->() const noexcept
{
    return const_cast<pointer>(JsonConstIterator::operator->());
}

JsonIterator& JsonIterator::operator++() noexcept
{
    JsonConstIterator::operator++();
    return *this;
}

JsonIterator JsonIterator::operator++(int) noexcept
{
    JsonIterator old(*this);
    JsonConstIterator::operator++();
    return old;
}

JsonIterator JsonIterator::begin() const noexcept
{
    return JsonIterator(JsonConstIterator::begin());
}

JsonIterator JsonIterator::end() const noexcept
{
    return JsonIterator(JsonConstIterator::end());
}

// JsonBuilder::RestoreOldSize

class JsonBuilder::RestoreOldSize
{
    StorageVec::size_type const m_oldSize;
    StorageVec* m_pVec;

public:

    ~RestoreOldSize()
    {
        if (m_pVec != nullptr)
        {
            m_pVec->resize(m_oldSize);
        }
    }

    explicit
    RestoreOldSize(StorageVec& vec) noexcept : m_oldSize(vec.size()), m_pVec(&vec)
    {
        return;
    }

    void
    Dismiss() noexcept
    {
        m_pVec = nullptr;
    }
};

// JsonBuilder::Validator

constexpr unsigned char MapBits = 2;
constexpr unsigned char MapMask = (1 << MapBits) - 1;
constexpr unsigned char MapPerByte = 8 / MapBits;
#define MapSize(cStorage) ((cStorage) / MapPerByte + 1u)

class JsonBuilder::Validator
    : private JsonInternal::PodVectorBase
{
    // 2-bit value.
    enum ValidationState : char unsigned
    {
        ValNone = 0,
        ValTail = 1,
        ValHead = 2,
        ValReached = 3,
        ValMax
    };

    JsonValue::StoragePod const* const m_pStorage;
    size_type const m_size;
    char unsigned* const m_pMap;  // 2 bits per StoragePod in m_pStorage.

    public:
    ~Validator();

    explicit Validator(
        _In_reads_(cStorage) JsonValue::StoragePod const* pStorage,
        size_type cStorage)
        noexcept(false);  // may throw bad_alloc

    void Validate()
        noexcept(false);  // may throw invalid_argument

    private:
    void ValidateRecurse(Index index) noexcept(false);

    void
    UpdateMap(Index index, ValidationState expectedVal, ValidationState newVal) noexcept(false);
};

JsonBuilder::Validator::~Validator()
{
    Deallocate(m_pMap);
}

JsonBuilder::Validator::Validator(
    _In_reads_(cStorage) JsonValue::StoragePod const* pStorage,
    size_type cStorage)
    : m_pStorage(pStorage)
    , m_size(cStorage)
    , m_pMap(static_cast<unsigned char*>(Allocate(MapSize(cStorage), false)))
{
    static_assert(
        ValMax <= (1 << MapBits), "Too many ValidationStates for MapBits");
    assert(m_size != 0);  // No need to validate an empty builder.
    return;
}

void JsonBuilder::Validator::Validate()
{
    memset(m_pMap, 0, MapSize(m_size));

    // Traverse the linked list, starting at head.
    // Ensure that items fit in storage without overlap.
    // Ensure that there are no loops.
    // Mark all valid heads.
    Index index = 0;
    do
    {
        static_assert(
            sizeof(JsonValueBase) % StorageSize == 0,
            "JsonValueBase not a multiple of StorageSize");
        static_assert(
            sizeof(JsonValue) % StorageSize == 0,
            "JsonValue not a multiple of StorageSize");

        // Note: don't dereference pValue until the appropriate part has been
        // validated.
        JsonValue const* const pValue =
            reinterpret_cast<JsonValue const*>(m_pStorage + index);

        // Mark start of JsonValueBase as head.
        UpdateMap(index, ValNone, ValHead);

        // Mark remainder of JsonValueBase as tail.
        Index const baseEnd = sizeof(JsonValueBase) / StorageSize;
        for (Index i = 1; i != baseEnd; i++)
        {
            UpdateMap(index + i, ValNone, ValTail);
        }

        // Now safe to dereference: m_nextIndex, m_cchName, m_type.

        if (pValue->m_type != JsonHidden)
        {
            // Mark m_cbData/m_lastChildIndex, Name as tail.
            Index const nameEnd = DATA_OFFSET(pValue->m_cchName);
            for (Index i = baseEnd; i != nameEnd; i++)
            {
                UpdateMap(index + i, ValNone, ValTail);
            }

            // Now safe to dereference: m_cbData/m_lastChildIndex, Name.

            if (IS_NORMAL_TYPE(pValue->m_type))
            {
                if (pValue->m_cbData > DataMax)
                {
                    JsonThrowInvalidArgument("JsonBuilder - corrupt data");
                }

                // Mark Data as tail.
                Index const dataEnd = nameEnd +
                    (pValue->m_cbData + StorageSize - 1) / StorageSize;
                for (Index i = nameEnd; i != dataEnd; i++)
                {
                    UpdateMap(index + i, ValNone, ValTail);
                }
            }
        }

        index = pValue->m_nextIndex;
    } while (index != 0);

    // Validate root.
    UpdateMap(0, ValHead, ValReached);
    if (reinterpret_cast<JsonValue const*>(m_pStorage)->m_cchName != 0 ||
        reinterpret_cast<JsonValue const*>(m_pStorage)->m_type != JsonObject)
    {
        JsonThrowInvalidArgument("JsonBuilder - corrupt data");
    }

    // Traverse the tree, starting at root.
    // Ensure that all reachable indexes are valid heads.
    // Ensure that there are no child->parent loops (no head reached more than
    // once).
    ValidateRecurse(0);

    return;
}

void JsonBuilder::Validator::ValidateRecurse(Index parent)
{
    JsonValue const* const pParent =
        reinterpret_cast<JsonValue const*>(m_pStorage + parent);

    // Validate first child (always hidden/sentinel).

    Index child = parent + DATA_OFFSET(pParent->m_cchName);
    UpdateMap(child, ValHead, ValReached);
    JsonValue const* pChild =
        reinterpret_cast<JsonValue const*>(m_pStorage + child);

    if (pChild->m_type != JsonHidden)
    {
        JsonThrowInvalidArgument("JsonBuilder - corrupt data");
    }

    // Validate remaining children.

    while (child != pParent->m_lastChildIndex)
    {
        child = pChild->m_nextIndex;
        UpdateMap(child, ValHead, ValReached);
        pChild = reinterpret_cast<JsonValue const*>(m_pStorage + child);

        if (IS_COMPOSITE_TYPE(pChild->m_type))
        {
            ValidateRecurse(child);
        }
    }

    return;
}

void JsonBuilder::Validator::UpdateMap(
    Index index,
    ValidationState expectedVal,
    ValidationState newVal)
{
    assert(newVal == (expectedVal | newVal));
    Index i = index / MapPerByte;
    char shift = (index % MapPerByte) * MapBits;
    if (m_size <= index || ((m_pMap[i] >> shift) & MapMask) != expectedVal)
    {
        JsonThrowInvalidArgument("JsonBuilder - corrupt data");
    }

    m_pMap[i] |= newVal << shift;
    assert(newVal == ((m_pMap[i] >> shift) & MapMask));
}

// JsonBuilder

JsonBuilder::JsonBuilder() noexcept
{
    return;
}

JsonBuilder::JsonBuilder(size_type cbInitialCapacity)
{
    buffer_reserve(cbInitialCapacity);
}

JsonBuilder::JsonBuilder(JsonBuilder const& other) : m_storage(other.m_storage)
{
    return;
}

JsonBuilder::JsonBuilder(JsonBuilder&& other) noexcept
    : m_storage(std::move(other.m_storage))
{
    return;
}

JsonBuilder::JsonBuilder(
    _In_reads_bytes_(cbRawData) void const* pbRawData,
    size_type cbRawData,
    bool validateData)
    : m_storage(
          static_cast<JsonValue::StoragePod const*>(pbRawData),
          static_cast<unsigned>(cbRawData / StorageSize))
{
    if (cbRawData % StorageSize != 0 ||
        cbRawData / StorageSize > StorageVec::max_size())
    {
        JsonThrowInvalidArgument("cbRawData invalid");
    }
    else if (validateData)
    {
        ValidateData();
    }
}

JsonBuilder& JsonBuilder::operator=(JsonBuilder const& other)
{
    m_storage = other.m_storage;
    return *this;
}

JsonBuilder& JsonBuilder::operator=(JsonBuilder&& other) noexcept
{
    m_storage = std::move(other.m_storage);
    return *this;
}

void JsonBuilder::ValidateData() const
{
    if (!m_storage.empty())
    {
        Validator(m_storage.data(), m_storage.size()).Validate();
    }
}

JsonBuilder::iterator JsonBuilder::begin() noexcept
{
    return iterator(cbegin());
}

JsonBuilder::const_iterator JsonBuilder::begin() const noexcept
{
    return cbegin();
}

JsonBuilder::const_iterator JsonBuilder::cbegin() const noexcept
{
    Index index = 0;
    if (!m_storage.empty())
    {
        // Return index of first non-hidden node after root.
        index = GetValue(0).m_nextIndex;
        for (;;)
        {
            auto& value = GetValue(index);
            if (value.m_type != JsonHidden)
            {
                break;
            }

            AssertNotEnd(index);  // end() should never be hidden.
            index = value.m_nextIndex;
        }
    }
    return const_iterator(this, index);
}

JsonBuilder::iterator JsonBuilder::end() noexcept
{
    return iterator(cend());
}

JsonBuilder::const_iterator JsonBuilder::end() const noexcept
{
    return cend();
}

JsonBuilder::const_iterator JsonBuilder::cend() const noexcept
{
    return const_iterator(this, 0);
}

JsonBuilder::iterator JsonBuilder::root() noexcept
{
    return end();
}

JsonBuilder::const_iterator JsonBuilder::root() const noexcept
{
    return end();
}

JsonBuilder::const_iterator JsonBuilder::croot() const noexcept
{
    return end();
}

JsonBuilder::size_type JsonBuilder::buffer_size() const noexcept
{
    return m_storage.size() * StorageSize;
}

void const* JsonBuilder::buffer_data() const noexcept
{
    return m_storage.data();
}

JsonBuilder::size_type JsonBuilder::buffer_capacity() const noexcept
{
    return m_storage.capacity() * StorageSize;
}

void JsonBuilder::buffer_reserve(size_type cbMinimumCapacity)
{
    if (cbMinimumCapacity > StorageVec::max_size() * StorageSize)
    {
        JsonThrowLengthError("requested capacity is too large");
    }
    auto const cItems = (cbMinimumCapacity + StorageSize - 1) / StorageSize;
    m_storage.reserve(static_cast<unsigned>(cItems));
}

void JsonBuilder::clear() noexcept
{
    m_storage.clear();
}

JsonBuilder::iterator JsonBuilder::erase(const_iterator itValue) noexcept
{
    ValidateIterator(itValue);
    if (itValue.m_index == 0)
    {
        assert(!"JsonBuilder: cannot erase end()");
        std::terminate();
    }

    GetValue(itValue.m_index).m_type = JsonHidden;
    return iterator(const_iterator(this, NextIndex(itValue.m_index)));
}

JsonBuilder::iterator
JsonBuilder::erase(const_iterator itBegin, const_iterator itEnd) noexcept
{
    ValidateIterator(itBegin);
    ValidateIterator(itEnd);
    auto index = itBegin.m_index;
    while (index != itEnd.m_index)
    {
        if (index == 0)
        {
            assert(!"JsonBuilder: invalid use of ReduceDataSize().");
            std::terminate();
        }

        auto& value = GetValue(index);
        value.m_type = JsonHidden;
        index = value.m_nextIndex;
    }
    return iterator(itEnd);
}

void JsonBuilder::swap(JsonBuilder& other) noexcept
{
    m_storage.swap(other.m_storage);
}

unsigned
JsonBuilder::FindImpl(Index parentIndex, std::string_view const& name) const noexcept
{
    Index result = 0;
    if (!m_storage.empty() && IS_COMPOSITE_TYPE(GetValue(parentIndex).m_type))
    {
        auto index = FirstChild(parentIndex);
        auto const lastIndex = LastChild(parentIndex);
        if (index != lastIndex)
        {
            AssertNotEnd(index);
            auto& sentinelValue = GetValue(index);
            assert(sentinelValue.m_type == JsonHidden);
            index = sentinelValue.m_nextIndex;
            for (;;)
            {
                AssertNotEnd(index);
                auto& value = GetValue(index);

                if (value.m_type != JsonHidden && value.Name() == name)
                {
                    result = index;
                    break;
                }

                if (index == lastIndex)
                {
                    break;
                }

                index = value.m_nextIndex;
            }
        }
    }
    return result;
}

unsigned JsonBuilder::count(const_iterator const& itParent) const noexcept
{
    ValidateIterator(itParent);

    unsigned result = 0;
    if (CanIterateOver(itParent))
    {
        auto index = FirstChild(itParent.m_index);
        auto const lastIndex = LastChild(itParent.m_index);
        if (index != lastIndex)
        {
            AssertNotEnd(index);
            auto& sentinelValue = GetValue(index);
            assert(sentinelValue.m_type == JsonHidden);
            index = sentinelValue.m_nextIndex;
            for (;;)
            {
                AssertNotEnd(index);
                auto& value = GetValue(index);

                if (value.m_type != JsonHidden)
                {
                    ++result;
                }

                if (index == lastIndex)
                {
                    break;
                }

                index = value.m_nextIndex;
            }
        }
    }
    return result;
}

JsonBuilder::iterator JsonBuilder::begin(const_iterator const& itParent) noexcept
{
    return iterator(cbegin(itParent));
}

JsonBuilder::const_iterator
JsonBuilder::begin(const_iterator const& itParent) const noexcept
{
    return cbegin(itParent);
}

JsonBuilder::const_iterator
JsonBuilder::cbegin(const_iterator const& itParent) const noexcept
{
    ValidateIterator(itParent);
    Index index = 0;
    if (CanIterateOver(itParent))
    {
        index = NextIndex(FirstChild(itParent.m_index));
    }
    return const_iterator(this, index);
}

JsonBuilder::iterator JsonBuilder::end(const_iterator const& itParent) noexcept
{
    return iterator(cend(itParent));
}

JsonBuilder::const_iterator
JsonBuilder::end(const_iterator const& itParent) const noexcept
{
    return cend(itParent);
}

JsonBuilder::const_iterator
JsonBuilder::cend(const_iterator const& itParent) const noexcept
{
    ValidateIterator(itParent);
    Index index = 0;
    if (CanIterateOver(itParent))
    {
        index = NextIndex(LastChild(itParent.m_index));
    }
    return const_iterator(this, index);
}

void
JsonBuilder::CreateRoot() noexcept(false)
{
    unsigned constexpr RootIndex = 0u;
    unsigned constexpr SentinelIndex = RootIndex + DATA_OFFSET(0u);
    m_storage.resize(RootSize);
    auto const pStorage = m_storage.data();

    auto const pRootValue = reinterpret_cast<JsonValue*>(pStorage + RootIndex);
    pRootValue->m_nextIndex = SentinelIndex;
    pRootValue->m_cchName = 0u;
    pRootValue->m_type = JsonObject;
    pRootValue->m_lastChildIndex = SentinelIndex;

    auto const pSentinel = reinterpret_cast<JsonValueBase*>(pStorage + SentinelIndex);
    pSentinel->m_nextIndex = RootIndex;
    pSentinel->m_cchName = 0;
    pSentinel->m_type = JsonHidden;
}

void const*
JsonBuilder::NewValueInitImpl(
    bool front,
    const_iterator const& itParent,
    void const* pName,
    unsigned cbNameReserve,
    unsigned cbDataHint)
    noexcept(false)  // may throw bad_alloc, length_error
{
    ValidateIterator(itParent);

    if (cbDataHint < sizeof(void*) || cbDataHint > DataMax)
    {
        if (cbDataHint > DataMax)
        {
            JsonThrowLengthError("JsonBuilder - cbData too large");
        }

        // We need at least enough room for a pointer.
        cbDataHint = sizeof(void*);
    }

    unsigned const valueIndex = static_cast<unsigned>(m_storage.size());
    unsigned const dataIndex = valueIndex + DATA_OFFSET(cbNameReserve);
    unsigned const newStorageSize = dataIndex + (cbDataHint + StorageSize - 1) / StorageSize;

    if (dataIndex <= valueIndex || newStorageSize < dataIndex)
    {
        // Integer overflow.
        JsonThrowLengthError("JsonBuilder - too much data");
    }

    void const* pNameAfterAlloc;
    if (m_storage.empty())
    {
        // Validate itParent.
        // Since builder is empty, the only possible parent is the root.
        if (itParent.m_index != 0)
        {
            assert(!"JsonBuilder: destination must be an array or object");
            std::terminate();
        }

        if (newStorageSize + RootSize < RootSize)
        {
            // Integer overflow.
            JsonThrowLengthError("JsonBuilder - too much data");
        }

        // Reserve room for root and new value (avoid extra reallocation).
        m_storage.reserve(newStorageSize + RootSize); // Reserve, not Resize.
        CreateRoot();

        // Since builder was empty, it's not possible for pName to be inside storage.
        pNameAfterAlloc = pName;
    }
    else
    {
        // Validate itParent.
        ValidateParentIterator(itParent.m_index);

        // Reserve room for new value.
        if (m_storage.capacity() >= newStorageSize)
        {
            // No reallocation, so pName is still valid.
            pNameAfterAlloc = pName;
        }
        else
        {
            auto const oldDataBegin = reinterpret_cast<char*>(m_storage.data());
            auto const oldDataEnd = reinterpret_cast<char*>(m_storage.data() + m_storage.size());
            m_storage.reserve(newStorageSize); // Reserve, not Resize.

            if (static_cast<char const*>(pName) > oldDataBegin &&
                static_cast<char const*>(pName) < oldDataEnd)
            {
                // They're copying the name from within the vector, and we just resized
                // out from under them. Technically, we could consider this a bug in the
                // caller, but it's an easy mistake to make, easy to miss in testing,
                // and hard to diagnose if it hits. Dealing with this is easy for us, so
                // just fix up the problem instead of making it an error.
                pNameAfterAlloc = reinterpret_cast<char*>(m_storage.data()) +
                    (static_cast<char const*>(pName) - oldDataBegin);
            }
            else
            {
                pNameAfterAlloc = pName;
            }
        }
    }

    auto const pValue = reinterpret_cast<JsonValue*>(m_storage.data() + m_storage.size());
    pValue->m_nextIndex = itParent.m_index; // Stash itParent for use by _newValueCommit.
    pValue->m_type = static_cast<JsonType>(front); // Stash front for use by _newValueCommit.

    return pNameAfterAlloc;
}

void
JsonBuilder::NewValueInit(
    bool front,
    const_iterator const& itParent,
    _In_reads_(cchName) char const* pchNameUtf8,
    size_type cchName,
    unsigned cbDataHint)
    noexcept(false)  // may throw bad_alloc, length_error
{
    auto constexpr WorstCaseMultiplier = 1u;
    if (cchName > NameMax / WorstCaseMultiplier)
    {
        JsonThrowLengthError("JsonBuilder - cchName too large");
    }

    auto const cchSrc = static_cast<unsigned>(cchName);
    auto const cbNameReserve = cchSrc * WorstCaseMultiplier;
    auto const pOldStorageData = m_storage.data();
    auto const pchSrc = static_cast<char const*>(
        NewValueInitImpl(front, itParent, pchNameUtf8, cbNameReserve, cbDataHint));
    auto const pValue = reinterpret_cast<JsonValue*>(m_storage.data() + m_storage.size());
    auto const pchDest = reinterpret_cast<char unsigned*>(pValue + 1);

    // Stash the name for use by _newValueCommit.
    auto const cchDest = cchSrc;
    memcpy(pchDest, pchSrc, cchSrc); // No conversion needed.
    pValue->m_cchName = cchDest;
    
    // Stash the old pointer for use by _newValueCommit.
    memcpy(reinterpret_cast<StoragePod*>(pValue) + DATA_OFFSET(cchDest),
        &pOldStorageData, sizeof(pOldStorageData));
}

void
JsonBuilder::NewValueInit(
    bool front,
    const_iterator const& itParent,
    _In_reads_(cchName) char16_t const* pchNameUtf16,
    size_type cchName,
    unsigned cbDataHint)
    noexcept(false)  // may throw bad_alloc, length_error
{
    auto constexpr WorstCaseMultiplier = 3u; // 1 UTF-16 code unit -> 3 UTF-8 code units.
    if (cchName > NameMax / WorstCaseMultiplier)
    {
        JsonThrowLengthError("JsonBuilder - cchName too large");
    }

    auto const cchSrc = static_cast<unsigned>(cchName);
    auto const cbNameReserve = cchSrc * WorstCaseMultiplier;
    auto const pOldStorageData = m_storage.data();
    auto const pchSrc = static_cast<char16_t const*>(
        NewValueInitImpl(front, itParent, pchNameUtf16, cbNameReserve, cbDataHint));
    auto const pValue = reinterpret_cast<JsonValue*>(m_storage.data() + m_storage.size());
    auto const pchDest = reinterpret_cast<char unsigned*>(pValue + 1);

    // Stash the name for use by _newValueCommit.
    auto const cchDest = Utf16ToUtf8(pchDest, pchSrc, cchSrc);
    pValue->m_cchName = cchDest;

    // Stash the old pointer for use by _newValueCommit.
    memcpy(reinterpret_cast<StoragePod*>(pValue) + DATA_OFFSET(cchDest),
        &pOldStorageData, sizeof(pOldStorageData));
}

void
JsonBuilder::NewValueInit(
    bool front,
    const_iterator const& itParent,
    _In_reads_(cchName) char32_t const* pchNameUtf32,
    size_type cchName,
    unsigned cbDataHint)
    noexcept(false)  // may throw bad_alloc, length_error
{
    auto constexpr WorstCaseMultiplier = 4u; // 1 UTF-32 code unit -> 4 UTF-8 code units.
    if (cchName > NameMax / WorstCaseMultiplier)
    {
        JsonThrowLengthError("JsonBuilder - cchName too large");
    }

    auto const cchSrc = static_cast<unsigned>(cchName);
    auto const cbNameReserve = cchSrc * WorstCaseMultiplier;
    auto const pOldStorageData = m_storage.data();
    auto const pchSrc = static_cast<char32_t const*>(
        NewValueInitImpl(front, itParent, pchNameUtf32, cbNameReserve, cbDataHint));
    auto const pValue = reinterpret_cast<JsonValue*>(m_storage.data() + m_storage.size());
    auto const pchDest = reinterpret_cast<char unsigned*>(pValue + 1);

    // Stash the name for use by _newValueCommit.
    auto const cchDest = Utf32ToUtf8(pchDest, pchSrc, cchSrc);
    pValue->m_cchName = cchDest;

    // Stash the old pointer for use by _newValueCommit.
    memcpy(reinterpret_cast<StoragePod*>(pValue) + DATA_OFFSET(cchDest),
        &pOldStorageData,
        sizeof(pOldStorageData));
}

JsonBuilder::iterator
JsonBuilder::_newValueCommit(
    JsonType type,
    unsigned cbData,
    _In_reads_bytes_opt_(cbData) void const* pbData)
    noexcept(false)  // may throw bad_alloc, length_error
{
    if (cbData > DataMax)
    {
        JsonThrowLengthError("JsonBuilder - cbValue too large");
    }

    auto const newIndex = static_cast<unsigned>(m_storage.size());

    // We expect front, parentIndex, name, and pOldStorageData to have been
    // stashed by NewValueInit.

    auto pValue = reinterpret_cast<JsonValue*>(m_storage.data() + newIndex);
    assert(m_storage.capacity() - m_storage.size() >= sizeof(JsonValue) / StorageSize);

    auto const dataIndex = newIndex + DATA_OFFSET(pValue->m_cchName);
    assert(m_storage.capacity() >= dataIndex + (sizeof(void*) + StorageSize - 1) / StorageSize);

    auto const parentIndex = pValue->m_nextIndex;
    assert(parentIndex < newIndex);
    assert(IS_COMPOSITE_TYPE(GetValue(parentIndex).m_type));

    auto const front = pValue->m_type != 0;
    assert(pValue->m_type == 0 || pValue->m_type == 1);

    StoragePod const* pOldStorageData;
    memcpy(&pOldStorageData, m_storage.data() + dataIndex, sizeof(pOldStorageData));

    if (IS_COMPOSITE_TYPE(type))
    {
        assert(cbData == 0);
        cbData = sizeof(JsonValueBase);
    }

    unsigned const newStorageSize = dataIndex + (cbData + StorageSize - 1) / StorageSize;
    if (newStorageSize < dataIndex)
    {
        JsonThrowLengthError("JsonBuilder - too much data");
    }

    if (m_storage.capacity() < newStorageSize)
    {
        RestoreOldSize restoreOldSize(m_storage); // In case resize(newStorageSize) throws.
        m_storage.resize(dataIndex); // Does not reallocate. Ensures that name gets copied during reallocation.
        m_storage.resize(newStorageSize); // Reallocate.

        // Commit.
        restoreOldSize.Dismiss();
        pValue = reinterpret_cast<JsonValue*>(m_storage.data() + newIndex);
    }
    else
    {
        // Commit.
        m_storage.resize(newStorageSize);
    }

    pValue->m_nextIndex = 0;
    pValue->m_type = type;

    if (IS_COMPOSITE_TYPE(type))
    {
        pValue->m_lastChildIndex = dataIndex;

        // Set up sentinel node. Insert it into the linked list.
        auto pRootValue = reinterpret_cast<JsonValue*>(m_storage.data());
        auto pSentinel = reinterpret_cast<JsonValueBase*>(m_storage.data() + dataIndex);
        pSentinel->m_nextIndex = pRootValue->m_nextIndex;
        pSentinel->m_cchName = 0;
        pSentinel->m_type = JsonHidden;
        pRootValue->m_nextIndex = dataIndex;
    }
    else
    {
        pValue->m_cbData = cbData;

        if (pbData != nullptr)
        {
            if (pOldStorageData != m_storage.data() && // We reallocated.
                pOldStorageData != nullptr &&          // Wasn't empty.
                pbData > static_cast<void const*>(pOldStorageData) &&
                pbData < static_cast<void const*>(pOldStorageData + newIndex))
            {
                // They're copying the data from within the vector, and we just resized
                // out from under them. Technically, we could consider this a bug in the
                // caller, but it's an easy mistake to make, easy to miss in testing,
                // and hard to diagnose if it hits. Dealing with this is easy for us, so
                // just fix up the problem instead of making it an error.
                pbData = reinterpret_cast<char const*>(m_storage.data()) +
                    (static_cast<char const*>(pbData) - reinterpret_cast<char const*>(pOldStorageData));
            }

            memcpy(m_storage.data() + dataIndex, pbData, cbData);
        }
    }

    // Find the right place in the linked list for the new node.
    // Update the parent's lastChildIndex if necessary.

    auto& parentValue = GetValue(parentIndex);
    Index prevIndex;  // The node that the new node goes after.
    if (front)
    {
        prevIndex = FirstChild(parentIndex);
        if (prevIndex == parentValue.m_lastChildIndex)
        {
            parentValue.m_lastChildIndex = newIndex;
        }
    }
    else
    {
        prevIndex = parentValue.m_lastChildIndex;
        parentValue.m_lastChildIndex = newIndex;
    }

    // Insert the new node into the linked list after prev.

    auto& prevValue = GetValue(prevIndex);
    auto& newValue = GetValue(newIndex);
    newValue.m_nextIndex = prevValue.m_nextIndex;
    prevValue.m_nextIndex = newIndex;

    return iterator(const_iterator(this, newIndex));
}

JsonBuilder::iterator
JsonBuilder::NewValueCommitAsUtfImpl(
    JsonType type,
    JsonInternal::JSON_SIZE_T cchData,
    _In_reads_(cchData) char const* pchDataUtf8)
    noexcept(false)
{
    if (cchData > DataMax)
    {
        JsonThrowLengthError("JsonBuilder - cchData too large");
    }
    return _newValueCommit(type, static_cast<unsigned>(cchData), pchDataUtf8);
}

JsonBuilder::iterator
JsonBuilder::NewValueCommitAsUtfImpl(
    JsonType type,
    JsonInternal::JSON_SIZE_T cchData,
    _In_reads_(cchData) char16_t const* pchDataUtf16)
    noexcept(false)  // may throw bad_alloc, length_error
{
    auto constexpr WorstCaseMultiplier = 3u; // 1 UTF-16 code unit -> 3 UTF-8 code units.
    if (cchData > DataMax / WorstCaseMultiplier)
    {
        JsonThrowLengthError("JsonBuilder - cchData too large");
    }

    // Create node. Reserve worst-case size for data.
    auto const cchSrc = static_cast<unsigned>(cchData);
    auto const valueIt = _newValueCommit(type, cchSrc * WorstCaseMultiplier, nullptr);

    // Copy data into node, converting to UTF-8.
    auto& value = GetValue(valueIt.m_index);
    auto const valueDataIndex = valueIt.m_index + DATA_OFFSET(value.m_cchName);
    auto const cbDest = Utf16ToUtf8(reinterpret_cast<unsigned char*>(&m_storage[valueDataIndex]), pchDataUtf16, cchSrc);

    // Shrink to fit actual data size.
    value.m_cbData = cbDest; // Shrink
    m_storage.resize(valueDataIndex + (cbDest + StorageSize - 1) / StorageSize); // Shrink

    return valueIt;
}

JsonBuilder::iterator
JsonBuilder::NewValueCommitAsUtfImpl(
    JsonType type,
    JsonInternal::JSON_SIZE_T cchData,
    _In_reads_(cchData) char32_t const* pchDataUtf32)
    noexcept(false)  // may throw bad_alloc, length_error
{
    auto constexpr WorstCaseMultiplier = 4u; // 1 UTF-32 code unit -> 4 UTF-8 code units.
    if (cchData > DataMax / WorstCaseMultiplier)
    {
        JsonThrowLengthError("JsonBuilder - cchData too large");
    }

    // Create node. Reserve worst-case size for data.
    auto const cchSrc = static_cast<unsigned>(cchData);
    auto const valueIt = _newValueCommit(type, cchSrc * WorstCaseMultiplier, nullptr);

    // Copy data into node, converting to UTF-8.
    auto& value = GetValue(valueIt.m_index);
    auto const valueDataIndex = valueIt.m_index + DATA_OFFSET(value.m_cchName);
    auto const cbDest = Utf32ToUtf8(reinterpret_cast<unsigned char*>(&m_storage[valueDataIndex]), pchDataUtf32, cchSrc);

    // Shrink to fit actual data size.
    value.m_cbData = cbDest; // Shrink
    m_storage.resize(valueDataIndex + (cbDest + StorageSize - 1) / StorageSize); // Shrink

    return valueIt;
}

void JsonBuilder::AssertNotEnd(Index index) noexcept
{
    (void) index;  // Unreferenced parameter in release builds.
    assert(index != 0);
}

void JsonBuilder::AssertHidden(JsonType type) noexcept
{
    (void) type;  // Unreferenced parameter in release builds.
    assert(type == JsonHidden);
}

void JsonBuilder::ValidateIterator(const_iterator const& it) const noexcept
{
    assert(it.m_index == 0 || it.m_index < m_storage.size());

    if (it.m_pContainer != this)
    {
        assert(!"JsonBuilder: iterator is from a different container");
        std::terminate();
    }
}

void JsonBuilder::ValidateParentIterator(Index index) const noexcept
{
    assert(!m_storage.empty());

    if (!IS_COMPOSITE_TYPE(GetValue(index).m_type))
    {
        assert(!"JsonBuilder: destination must be an array or object");
        std::terminate();
    }
}

bool JsonBuilder::CanIterateOver(const_iterator const& it) const noexcept
{
    return !m_storage.empty() && IS_COMPOSITE_TYPE(GetValue(it.m_index).m_type);
}

JsonValue const& JsonBuilder::GetValue(Index index) const noexcept
{
    return reinterpret_cast<JsonValue const&>(m_storage[index]);
}

JsonValue& JsonBuilder::GetValue(Index index) noexcept
{
    return reinterpret_cast<JsonValue&>(m_storage[index]);
}

JsonBuilder::Index JsonBuilder::FirstChild(Index index) const noexcept
{
    auto cchName = reinterpret_cast<JsonValue const&>(m_storage[index]).m_cchName;
    return index + DATA_OFFSET(cchName);
}

JsonBuilder::Index JsonBuilder::LastChild(Index index) const noexcept
{
    return reinterpret_cast<JsonValue const&>(m_storage[index]).m_lastChildIndex;
}

JsonBuilder::Index JsonBuilder::NextIndex(Index index) const noexcept
{
    assert(index < m_storage.size());
    auto pValue = reinterpret_cast<JsonValue const*>(m_storage.data() + index);
    for (;;)
    {
        assert(index != 0);  // assert(it != end())
        index = pValue->m_nextIndex;
        pValue = reinterpret_cast<JsonValue const*>(m_storage.data() + index);
        if (pValue->m_type != JsonHidden)
        {
            break;
        }
    }
    return index;
}

void JsonBuilder::EnsureRootExists()
{
    if (m_storage.empty())
    {
        CreateRoot();
    }
}

void swap(JsonBuilder& a, JsonBuilder& b) noexcept
{
    a.swap(b);
}

// JsonImplementType

/*
The macro-based GetUnchecked and ConvertTo (for f32, u8, u16, u32, i8, i16,
and i32) aren't perfectly optimal... But they're probably close enough.
*/

#define IMPLEMENT_AddValue(DataType, DataSize, DataPtr, ValueType, InRef) \
    JsonIterator JsonImplementType<DataType>::AddValueCommit(      \
        JsonBuilder& builder,                                      \
        DataType InRef data)                                      \
    {                                                              \
        return builder._newValueCommit(ValueType, DataSize, DataPtr);\
    }

#define IMPLEMENT_GetUnchecked(DataType, ValueType)                   \
    DataType JsonImplementType<DataType>::GetUnchecked(               \
        JsonValue const& value) noexcept                              \
    {                                                                 \
        return static_cast<DataType>(GetUnchecked##ValueType(value)); \
    }

#define IMPLEMENT_JsonImplementType(DataType, ValueType, InRef)   \
    IMPLEMENT_AddValue(DataType, sizeof(data), &data, ValueType, InRef); \
    IMPLEMENT_GetUnchecked(DataType, ValueType);                  \
                                                                  \
    bool JsonImplementType<DataType>::ConvertTo(                  \
        JsonValue const& value, DataType& result) noexcept        \
    {                                                             \
        return ConvertTo##ValueType(value, result);               \
    }

// JsonBool

bool JsonImplementType<bool>::GetUnchecked(JsonValue const& value) noexcept
{
    assert(value.Type() == JsonBool);
    bool result;
    unsigned cb;
    void const* pb = value.Data(&cb);
    switch (cb)
    {
    case 1:
        result = *static_cast<char const*>(pb) != 0;
        break;
    case 4:
        result = *static_cast<int const*>(pb) != 0;
        break;
    default:
        result = 0;
        assert(!"Invalid size for JsonBool");
        break;
    }
    return result;
}

bool JsonImplementType<bool>::ConvertTo(JsonValue const& value, bool& result) noexcept
{
    bool success;
    switch (value.Type())
    {
    case JsonBool:
        result = GetUnchecked(value);
        success = true;
        break;
    default:
        result = false;
        success = false;
        break;
    }
    return success;
}

IMPLEMENT_AddValue(bool, sizeof(data), &data, JsonBool, );

// JsonUInt

bool JsonImplementType<unsigned long long>::ConvertTo(
    JsonValue const& value,
    unsigned long long& result) noexcept
{
    static double const UnsignedHuge = 18446744073709551616.0;

    bool success;
    switch (value.Type())
    {
    case JsonUInt:
        result = JsonImplementType<unsigned long long>::GetUnchecked(value);
        success = true;
        goto Done;

    case JsonInt:
        result = static_cast<unsigned long long>(
            JsonImplementType<signed long long>::GetUnchecked(value));
        if (result < 0x8000000000000000)
        {
            success = true;
            goto Done;
        }
        break;

    case JsonFloat: {
        auto f = JsonImplementType<double>::GetUnchecked(value);
        if (0.0 <= f && f < UnsignedHuge)
        {
            result = static_cast<long long unsigned>(f);
            success = true;
            goto Done;
        }
        break;
    }

    default:
        break;
    }

    result = 0;
    success = false;

Done:

    return success;
}

static uint64_t GetUncheckedJsonUInt(JsonValue const& value)
{
    assert(value.Type() == JsonUInt);
    uint64_t result;
    unsigned cb;
    void const* pb = value.Data(&cb);
    switch (cb)
    {
    case 1:
        result = *static_cast<uint8_t const*>(pb);
        break;
    case 2:
        result = *static_cast<uint16_t const*>(pb);
        break;
    case 4:
        result = *static_cast<uint32_t const*>(pb);
        break;
    case 8:
        result = *static_cast<uint64_t const*>(pb);
        break;
    default:
        result = 0;
        assert(!"Invalid size for JsonUInt");
        break;
    }
    return result;
}

IMPLEMENT_AddValue(unsigned long long, sizeof(data), &data, JsonUInt, );
IMPLEMENT_GetUnchecked(unsigned long long, JsonUInt);

template<class T>
static bool ConvertToJsonUInt(JsonValue const& value, T& result)
{
    unsigned long long implResult;
    bool success;
    if (JsonImplementType<unsigned long long>::ConvertTo(value, implResult) &&
        implResult <= (0xffffffffffffffff >> (64 - sizeof(T) * 8)))
    {
        result = static_cast<T>(implResult);
        success = true;
    }
    else
    {
        result = 0;
        success = false;
    }
    return success;
}

IMPLEMENT_JsonImplementType(unsigned char, JsonUInt, );
IMPLEMENT_JsonImplementType(unsigned short, JsonUInt, );
IMPLEMENT_JsonImplementType(unsigned int, JsonUInt, );
IMPLEMENT_JsonImplementType(unsigned long, JsonUInt, );

// JsonInt

bool JsonImplementType<signed long long>::ConvertTo(
    JsonValue const& value,
    signed long long& result) noexcept
{
    static double const SignedHuge = 9223372036854775808.0;

    bool success;
    switch (value.Type())
    {
    case JsonInt:
        result = JsonImplementType<signed long long>::GetUnchecked(value);
        success = true;
        goto Done;

    case JsonUInt:
        result = static_cast<signed long long>(
            JsonImplementType<unsigned long long>::GetUnchecked(value));
        if (result >= 0)
        {
            success = true;
            goto Done;
        }
        break;

    case JsonFloat: {
        auto f = JsonImplementType<double>::GetUnchecked(value);
        if (-SignedHuge <= f && f < SignedHuge)
        {
            result = static_cast<signed long long>(f);
            success = true;
            goto Done;
        }
        break;
    }

    default:
        break;
    }

    result = 0;
    success = false;

Done:

    return success;
}

static int64_t GetUncheckedJsonInt(JsonValue const& value)
{
    assert(value.Type() == JsonInt);
    int64_t result;
    unsigned cb;
    void const* pb = value.Data(&cb);
    switch (cb)
    {
    case 1:
        result = *static_cast<int8_t const*>(pb);
        break;
    case 2:
        result = *static_cast<int16_t const*>(pb);
        break;
    case 4:
        result = *static_cast<int32_t const*>(pb);
        break;
    case 8:
        result = *static_cast<int64_t const*>(pb);
        break;
    default:
        result = 0;
        assert(!"Invalid size for JsonInt");
        break;
    }
    return result;
}

IMPLEMENT_AddValue(signed long long, sizeof(data), &data, JsonInt, );
IMPLEMENT_GetUnchecked(signed long long, JsonInt);

template<class T>
static bool ConvertToJsonInt(JsonValue const& value, T& result)
{
    signed long long implResult;
    bool success;

    if (JsonImplementType<signed long long>::ConvertTo(value, implResult) &&
        (sizeof(T) == sizeof(signed long long) ||
         (implResult < (1ll << (sizeof(T) * 8 - 1)) &&
          implResult >= -(1ll << (sizeof(T) * 8 - 1)))))
    {
        result = static_cast<T>(implResult);
        success = true;
    }
    else
    {
        result = 0;
        success = false;
    }
    return success;
}

IMPLEMENT_JsonImplementType(signed char, JsonInt, );
IMPLEMENT_JsonImplementType(signed short, JsonInt, );
IMPLEMENT_JsonImplementType(signed int, JsonInt, );
IMPLEMENT_JsonImplementType(signed long, JsonInt, );

// JsonFloat

double JsonImplementType<double>::GetUnchecked(JsonValue const& value) noexcept
{
    assert(value.Type() == JsonFloat);
    double result;
    unsigned cb;
    void const* pb = value.Data(&cb);
    switch (cb)
    {
    case 4:
        result = *static_cast<float const*>(pb);
        break;
    case 8:
        result = *static_cast<double const*>(pb);
        break;
    default:
        result = 0;
        assert(!"Invalid size for JsonFloat");
        break;
    }
    return result;
}

bool JsonImplementType<double>::ConvertTo(
    JsonValue const& value,
    double& result) noexcept
{
    bool success;

    switch (value.Type())
    {
    case JsonUInt:
        result = static_cast<double>(
            JsonImplementType<unsigned long long>::GetUnchecked(value));
        success = true;
        break;

    case JsonInt:
        result = static_cast<double>(
            JsonImplementType<signed long long>::GetUnchecked(value));
        success = true;
        break;

    case JsonFloat:
        result = GetUnchecked(value);
        success = true;
        break;

    default:
        result = 0.0;
        success = false;
        break;
    }

    return success;
}

IMPLEMENT_AddValue(double, sizeof(data), &data, JsonFloat, );

#define GetUncheckedJsonFloat(value) \
    JsonImplementType<double>::GetUnchecked(value)

template<class T>
static bool ConvertToJsonFloat(JsonValue const& value, T& result)
{
    double implResult;
    bool success = JsonImplementType<double>::ConvertTo(value, implResult);
    result = static_cast<T>(implResult);
    return success;
}

IMPLEMENT_JsonImplementType(float, JsonFloat, );

// JsonUtf8

JsonIterator
JsonImplementType<char*>::AddValueCommit(
    JsonBuilder& builder,
    _In_z_ char const* psz)
{
    return builder._newValueCommitAsUtf8(JsonUtf8, psz);
}

JsonIterator
JsonImplementType<wchar_t*>::AddValueCommit(
    JsonBuilder& builder,
    _In_z_ wchar_t const* psz)
{
    return builder._newValueCommitAsUtf8(JsonUtf8, psz);
}

JsonIterator
JsonImplementType<char16_t*>::AddValueCommit(
    JsonBuilder& builder,
    _In_z_ char16_t const* psz)
{
    return builder._newValueCommitAsUtf8(JsonUtf8, psz);
}

JsonIterator
JsonImplementType<char32_t*>::AddValueCommit(
    JsonBuilder& builder,
    _In_z_ char32_t const* psz)
{
    return builder._newValueCommitAsUtf8(JsonUtf8, psz);
}

std::string_view
JsonImplementType<std::string_view>::GetUnchecked(JsonValue const& value) noexcept
{
    assert(value.Type() == JsonUtf8);
    unsigned cb;
    void const* pb = value.Data(&cb);
    return std::string_view(static_cast<char const*>(pb), cb);
}

bool JsonImplementType<std::string_view>::ConvertTo(
    JsonValue const& value,
    std::string_view& result) noexcept
{
    bool success;

    if (value.Type() == JsonUtf8)
    {
        result = GetUnchecked(value);
        success = true;
    }
    else
    {
        result = std::string_view();
        success = false;
    }

    return success;
}

JsonIterator
JsonImplementType<std::string_view>::AddValueCommit(
    JsonBuilder& builder,
    std::string_view data)
{
    return builder._newValueCommitAsUtf8(JsonUtf8, data);
}

JsonIterator
JsonImplementType<std::wstring_view>::AddValueCommit(
    JsonBuilder& builder,
    std::wstring_view data)
{
    return builder._newValueCommitAsUtf8(JsonUtf8, data);
}

JsonIterator
JsonImplementType<std::u16string_view>::AddValueCommit(
    JsonBuilder& builder,
    std::u16string_view data)
{
    return builder._newValueCommitAsUtf8(JsonUtf8, data);
}

JsonIterator
JsonImplementType<std::u32string_view>::AddValueCommit(
    JsonBuilder& builder,
    std::u32string_view data)
{
    return builder._newValueCommitAsUtf8(JsonUtf8, data);
}

// JsonTime

JsonIterator
JsonImplementType<std::chrono::system_clock::time_point>::AddValueCommit(
    JsonBuilder& builder,
    std::chrono::system_clock::time_point data)
{
    uint64_t const ft = FileTime1970Ticks + std::chrono::duration_cast<ticks>(data.time_since_epoch()).count();
    auto const timeStruct = TimeStruct::FromValue(ft);
    return builder._newValueCommit(JsonTime, sizeof(timeStruct), &timeStruct);
}

bool JsonImplementType<std::chrono::system_clock::time_point>::ConvertTo(
    JsonValue const& jsonValue,
    std::chrono::system_clock::time_point& value) noexcept
{
    bool success;

    if (jsonValue.Type() == JsonTime)
    {
        value = jsonValue.GetUnchecked<std::chrono::system_clock::time_point>();
        success = true;
    }
    else
    {
        value = std::chrono::system_clock::time_point{};
        success = false;
    }

    return success;
}

std::chrono::system_clock::time_point
JsonImplementType<std::chrono::system_clock::time_point>::GetUnchecked(
    JsonValue const& jsonValue) noexcept
{
    assert(jsonValue.Type() == JsonTime);
    assert(jsonValue.DataSize() == sizeof(TimeStruct));
    if (jsonValue.DataSize() == sizeof(TimeStruct))
    {
        auto constexpr SystemClock1970Ticks = -std::chrono::duration_cast<ticks>(
            std::chrono::system_clock::time_point().time_since_epoch()
            ).count();
        auto const ft = jsonValue.GetUnchecked<TimeStruct>().Value();
        auto const ticksSinceEpoch = ticks(ft + (SystemClock1970Ticks - FileTime1970Ticks));
        return std::chrono::system_clock::time_point(ticksSinceEpoch);
    }
    else
    {
        return {};
    }
}

IMPLEMENT_AddValue(TimeStruct, sizeof(TimeStruct), &data, JsonTime, );

bool JsonImplementType<TimeStruct>::ConvertTo(
    JsonValue const& jsonValue,
    TimeStruct& value) noexcept
{
    bool success;

    if (jsonValue.Type() == JsonTime)
    {
        assert(jsonValue.DataSize() == sizeof(TimeStruct));
        value = *static_cast<const TimeStruct*>(jsonValue.Data());
        success = true;
    }
    else
    {
        value = TimeStruct();
        success = false;
    }

    return success;
}

TimeStruct
JsonImplementType<TimeStruct>::GetUnchecked(JsonValue const& jsonValue) noexcept
{
    assert(jsonValue.Type() == JsonTime);
    assert(jsonValue.DataSize() == sizeof(TimeStruct));

    return jsonValue.DataSize() == sizeof(TimeStruct) ?
        *static_cast<const TimeStruct*>(jsonValue.Data()) :
        TimeStruct();
}

// JsonUuid

IMPLEMENT_AddValue(UuidStruct, sizeof(UuidStruct), &data, JsonUuid, const&);

bool JsonImplementType<UuidStruct>::ConvertTo(
    JsonValue const& jsonValue,
    UuidStruct& value) noexcept
{
    bool success;

    if (jsonValue.Type() == JsonUuid)
    {
        assert(jsonValue.DataSize() == sizeof(UuidStruct));
        value = *static_cast<const UuidStruct*>(jsonValue.Data());
        success = true;
    }
    else
    {
        value = UuidStruct();
        success = false;
    }

    return success;
}

UuidStruct const&
JsonImplementType<UuidStruct>::GetUnchecked(JsonValue const& jsonValue) noexcept
{
    assert(jsonValue.Type() == JsonUuid);
    assert(jsonValue.DataSize() == sizeof(UuidStruct));

    static constexpr UuidStruct emptyUuid = { 0 };
    return jsonValue.DataSize() == sizeof(UuidStruct) ?
        *static_cast<const UuidStruct*>(jsonValue.Data()) :
        emptyUuid;
}

}  // namespace jsonbuilder
