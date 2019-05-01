// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <jsonbuilder/JsonBuilder.h>

#include <cassert>


#define StorageSize sizeof(JsonValue::StoragePod)
#define DataMax 0xf0000000

/*
Computes the difference between a value's index and the index at which its
data begins. Used to determine the DataIndex for a value:
value.DataIndex = value.Index + DATA_OFFSET(value.cchName).
*/
#define DATA_OFFSET(cchName) \
    (((cchName) + sizeof(JsonValue) + (StorageSize - 1)) / StorageSize)

#define IS_SPECIAL_TYPE(type) (JsonHidden <= (type))
#define IS_NORMAL_TYPE(type) ((type) < JsonHidden)
#define IS_COMPOSITE_TYPE(type) (JsonArray <= (type))

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

JsonType JsonValue::Type() const throw()
{
    return static_cast<JsonType>(m_type);
}

nonstd::string_view JsonValue::Name() const throw()
{
    return nonstd::string_view(reinterpret_cast<char const*>(this + 1), m_cchName);
}

void const* JsonValue::Data(unsigned* pcbData) const throw()
{
    return const_cast<JsonValue*>(this)->Data(pcbData);
}

void* JsonValue::Data(unsigned* pcbData) throw()
{
    assert(!IS_SPECIAL_TYPE(m_type));  // Can't call Data() on hidden,
    // object, or array values.
    if (pcbData != nullptr)
    {
        *pcbData = m_cbData;
    }

    return reinterpret_cast<StoragePod*>(this) + DATA_OFFSET(m_cchName);
}

unsigned JsonValue::DataSize() const throw()
{
    assert(!IS_SPECIAL_TYPE(m_type));  // Can't call DataSize() on hidden,
    // object, or array values.
    return m_cbData;
}

void JsonValue::ReduceDataSize(unsigned cbNew) throw()
{
    if (IS_SPECIAL_TYPE(m_type) || cbNew > m_cbData)
    {
        assert(!"JsonBuilder: invalid use of ReduceDataSize().");
        std::terminate();
    }

    m_cbData = cbNew;
}

bool JsonValue::IsNull() const throw()
{
    return m_type == JsonNull;
}

// JsonConstIterator

JsonConstIterator::JsonConstIterator() throw() : m_pContainer(), m_index()
{
    return;
}

JsonConstIterator::JsonConstIterator(JsonBuilder const* pContainer, Index index) throw()
    : m_pContainer(pContainer), m_index(index)
{
    return;
}

bool JsonConstIterator::operator==(JsonConstIterator const& other) const throw()
{
    assert(m_pContainer == other.m_pContainer);
    return m_index == other.m_index;
}

bool JsonConstIterator::operator!=(JsonConstIterator const& other) const throw()
{
    assert(m_pContainer == other.m_pContainer);
    return m_index != other.m_index;
}

JsonConstIterator::reference JsonConstIterator::operator*() const throw()
{
    m_pContainer->AssertNotEnd(m_index);  // Do not dereference the end()
                                          // iterator.
    return m_pContainer->GetValue(m_index);
}

JsonConstIterator::pointer JsonConstIterator::operator->() const throw()
{
    m_pContainer->AssertNotEnd(m_index);  // Do not dereference the end()
                                          // iterator.
    return &m_pContainer->GetValue(m_index);
}

JsonConstIterator& JsonConstIterator::operator++() throw()
{
    m_index = m_pContainer->NextIndex(m_index);  // Implicitly asserts !end().
    return *this;
}

JsonConstIterator JsonConstIterator::operator++(int) throw()
{
    auto old = *this;
    m_index = m_pContainer->NextIndex(m_index);  // Implicitly asserts !end().
    return old;
}

JsonConstIterator JsonConstIterator::begin() const throw()
{
    return m_pContainer->begin(*this);
}

JsonConstIterator JsonConstIterator::end() const throw()
{
    return m_pContainer->end(*this);
}

bool JsonConstIterator::IsRoot() const throw()
{
    return m_index == 0;
}

// JsonIterator

JsonIterator::JsonIterator() throw() : JsonConstIterator()
{
    return;
}

JsonIterator::JsonIterator(JsonConstIterator const& other) throw()
    : JsonConstIterator(other)
{
    return;
}

JsonIterator::reference JsonIterator::operator*() const throw()
{
    return const_cast<reference>(JsonConstIterator::operator*());
}

JsonIterator::pointer JsonIterator::operator->() const throw()
{
    return const_cast<pointer>(JsonConstIterator::operator->());
}

JsonIterator& JsonIterator::operator++() throw()
{
    JsonConstIterator::operator++();
    return *this;
}

JsonIterator JsonIterator::operator++(int) throw()
{
    JsonIterator old(*this);
    JsonConstIterator::operator++();
    return old;
}

JsonIterator JsonIterator::begin() const throw()
{
    return JsonIterator(JsonConstIterator::begin());
}

JsonIterator JsonIterator::end() const throw()
{
    return JsonIterator(JsonConstIterator::end());
}

// JsonBuilder::Validator

constexpr unsigned char MapBits = 2;
constexpr unsigned char MapMask = (1 << MapBits) - 1;
constexpr unsigned char MapPerByte = 8 / MapBits;
#define MapSize(cStorage) ((cStorage) / MapPerByte + 1u)

JsonBuilder::Validator::~Validator()
{
    Deallocate(m_pMap);
}

JsonBuilder::Validator::Validator(
    JsonValue::StoragePod const* pStorage,
    size_type cStorage)
    : m_pStorage(pStorage)
    , m_size(cStorage)
    , m_pMap(static_cast<unsigned char*>(Reallocate(nullptr, MapSize(cStorage))))
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
                    throw std::invalid_argument("JsonBuilder - corrupt data");
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
        throw std::invalid_argument("JsonBuilder - corrupt data");
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
        throw std::invalid_argument("JsonBuilder - corrupt data");
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
        throw std::invalid_argument("JsonBuilder - corrupt data");
    }

    m_pMap[i] |= newVal << shift;
    assert(newVal == ((m_pMap[i] >> shift) & MapMask));
}

// JsonBuilder

JsonBuilder::JsonBuilder() throw()
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

JsonBuilder::JsonBuilder(JsonBuilder&& other) throw()
    : m_storage(std::move(other.m_storage))
{
    return;
}

JsonBuilder::JsonBuilder(void const* pbRawData, size_type cbRawData, bool validateData)
    : m_storage(
          static_cast<JsonValue::StoragePod const*>(pbRawData),
          static_cast<unsigned>(cbRawData / StorageSize))
{
    if (cbRawData % StorageSize != 0 ||
        cbRawData / StorageSize > StorageVec::max_size())
    {
        throw std::invalid_argument("cbRawData invalid");
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

JsonBuilder& JsonBuilder::operator=(JsonBuilder&& other) throw()
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

JsonBuilder::iterator JsonBuilder::begin() throw()
{
    return iterator(cbegin());
}

JsonBuilder::const_iterator JsonBuilder::begin() const throw()
{
    return cbegin();
}

JsonBuilder::const_iterator JsonBuilder::cbegin() const throw()
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

JsonBuilder::iterator JsonBuilder::end() throw()
{
    return iterator(cend());
}

JsonBuilder::const_iterator JsonBuilder::end() const throw()
{
    return cend();
}

JsonBuilder::const_iterator JsonBuilder::cend() const throw()
{
    return const_iterator(this, 0);
}

JsonBuilder::size_type JsonBuilder::buffer_size() const throw()
{
    return m_storage.size() * StorageSize;
}

void const* JsonBuilder::buffer_data() const throw()
{
    return m_storage.data();
}

JsonBuilder::size_type JsonBuilder::buffer_capacity() const throw()
{
    return m_storage.capacity() * StorageSize;
}

void JsonBuilder::buffer_reserve(size_type cbMinimumCapacity)
{
    if (cbMinimumCapacity > StorageVec::max_size() * StorageSize)
    {
        throw std::length_error("requested capacity is too large");
    }
    auto const cItems = (cbMinimumCapacity + StorageSize - 1) / StorageSize;
    m_storage.reserve(static_cast<unsigned>(cItems));
}

void JsonBuilder::clear() throw()
{
    m_storage.clear();
}

JsonBuilder::iterator JsonBuilder::erase(const_iterator itValue) throw()
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
JsonBuilder::erase(const_iterator itBegin, const_iterator itEnd) throw()
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

void JsonBuilder::swap(JsonBuilder& other) throw()
{
    m_storage.swap(other.m_storage);
}

unsigned
JsonBuilder::FindImpl(Index parentIndex, nonstd::string_view const& name) const
    throw()
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

unsigned JsonBuilder::count(const_iterator const& itParent) const throw()
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

JsonBuilder::iterator JsonBuilder::begin(const_iterator const& itParent) throw()
{
    return iterator(cbegin(itParent));
}

JsonBuilder::const_iterator
JsonBuilder::begin(const_iterator const& itParent) const throw()
{
    return cbegin(itParent);
}

JsonBuilder::const_iterator
JsonBuilder::cbegin(const_iterator const& itParent) const throw()
{
    ValidateIterator(itParent);
    Index index = 0;
    if (CanIterateOver(itParent))
    {
        index = NextIndex(FirstChild(itParent.m_index));
    }
    return const_iterator(this, index);
}

JsonBuilder::iterator JsonBuilder::end(const_iterator const& itParent) throw()
{
    return iterator(cend(itParent));
}

JsonBuilder::const_iterator
JsonBuilder::end(const_iterator const& itParent) const throw()
{
    return cend(itParent);
}

JsonBuilder::const_iterator
JsonBuilder::cend(const_iterator const& itParent) const throw()
{
    ValidateIterator(itParent);
    Index index = 0;
    if (CanIterateOver(itParent))
    {
        index = NextIndex(LastChild(itParent.m_index));
    }
    return const_iterator(this, index);
}

JsonBuilder::iterator JsonBuilder::AddValue(
    bool front,
    const_iterator const& itParent,
    nonstd::string_view const& name,
    JsonType type,
    unsigned cbData,
    void const* pbData)
{
    ValidateIterator(itParent);
    EnsureRootExists();
    ValidateParentIterator(itParent.m_index);
    Index const newIndex = CreateValue(name, type, cbData, pbData);

    // Find the right place in the linked list for the new node.
    // Update the parent's lastChildIndex if necessary.

    auto& parentValue = GetValue(itParent.m_index);  // GetValue(parent) must be
                                                     // AFTER the CreateValue.
    Index prevIndex;  // The node that the new node goes after.
    if (front)
    {
        prevIndex = FirstChild(itParent.m_index);
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

void JsonBuilder::AssertNotEnd(Index index) throw()
{
    (void) index;  // Unreferenced parameter in release builds.
    assert(index != 0);
}

void JsonBuilder::AssertHidden(JsonType type) throw()
{
    (void) type;  // Unreferenced parameter in release builds.
    assert(type == JsonHidden);
}

void JsonBuilder::ValidateIterator(const_iterator const& it) const throw()
{
    assert(it.m_index == 0 || it.m_index < m_storage.size());

    if (it.m_pContainer != this)
    {
        assert(!"JsonBuilder: iterator is from a different container");
        std::terminate();
    }
}

void JsonBuilder::ValidateParentIterator(Index index) const throw()
{
    assert(!m_storage.empty());

    if (!IS_COMPOSITE_TYPE(GetValue(index).m_type))
    {
        assert(!"JsonBuilder: destination must be an array or object");
        std::terminate();
    }
}

bool JsonBuilder::CanIterateOver(const_iterator const& it) const throw()
{
    return !m_storage.empty() && IS_COMPOSITE_TYPE(GetValue(it.m_index).m_type);
}

JsonValue const& JsonBuilder::GetValue(Index index) const throw()
{
    return reinterpret_cast<JsonValue const&>(m_storage[index]);
}

JsonValue& JsonBuilder::GetValue(Index index) throw()
{
    return reinterpret_cast<JsonValue&>(m_storage[index]);
}

JsonBuilder::Index JsonBuilder::FirstChild(Index index) const throw()
{
    auto cchName = reinterpret_cast<JsonValue const&>(m_storage[index]).m_cchName;
    return index + DATA_OFFSET(cchName);
}

JsonBuilder::Index JsonBuilder::LastChild(Index index) const throw()
{
    return reinterpret_cast<JsonValue const&>(m_storage[index]).m_lastChildIndex;
}

JsonBuilder::Index JsonBuilder::NextIndex(Index index) const throw()
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
        unsigned index;
        index = CreateValue(nonstd::string_view(), JsonObject, 0, nullptr);
        assert(index == 0);
    }
}

JsonBuilder::Index JsonBuilder::CreateValue(
    nonstd::string_view const& name,
    JsonType type,
    unsigned cbData,
    void const* pbData)
{
    assert(type <= 255);
    if (name.size() > 0xffffff)
    {
        throw std::invalid_argument("JsonBuilder - cchName too large");
    }

    if (cbData > DataMax)
    {
        throw std::invalid_argument("JsonBuilder - cbValue too large");
    }

    if (IS_COMPOSITE_TYPE(type))
    {
        assert(cbData == 0);
        cbData = sizeof(JsonValueBase);
    }

    unsigned const cchName = static_cast<unsigned>(name.size());
    unsigned const valueIndex = static_cast<unsigned>(m_storage.size());
    unsigned const dataIndex = valueIndex + DATA_OFFSET(cchName);
    unsigned const newStorageSize =
        dataIndex + (cbData + StorageSize - 1) / StorageSize;
    auto const pOldStorageData =
        reinterpret_cast<char const* const>(m_storage.data());

    if (newStorageSize <= valueIndex)
    {
        throw std::invalid_argument("JsonBuilder - too much data");
    }

    m_storage.resize(newStorageSize);

    JsonValue* const pValue =
        reinterpret_cast<JsonValue*>(m_storage.data() + valueIndex);
    pValue->m_nextIndex = 0;
    pValue->m_cchName = cchName;
    pValue->m_type = type;

    auto pNameData = reinterpret_cast<char const*>(name.data());
    auto const pNewStorageData =
        reinterpret_cast<char const* const>(m_storage.data());
    if (pOldStorageData != pNewStorageData && pOldStorageData < pNameData &&
        pNameData < pOldStorageData + (valueIndex * StorageSize))
    {
        // They're copying the name from within the vector, and we just resized
        // out from under them. Technically, we could consider this a bug in the
        // caller, but it's an easy mistake to make, easy to miss in testing,
        // and hard to diagnose if it hits. Dealing with this is easy for us, so
        // just fix up the problem instead of making it an error.
        pNameData = pNewStorageData + (pNameData - pOldStorageData);
    }

    // Write into the memory beyond the JsonValue's end
    std::memcpy(reinterpret_cast<char*>(pValue + 1), pNameData, cchName);

    if (IS_COMPOSITE_TYPE(type))
    {
        pValue->m_lastChildIndex = dataIndex;

        // Set up sentinel node. Insert it into the linked list.
        auto pRootValue = reinterpret_cast<JsonValue*>(m_storage.data());
        auto pSentinel =
            reinterpret_cast<JsonValueBase*>(m_storage.data() + dataIndex);
        pSentinel->m_nextIndex = pRootValue->m_nextIndex;
        pSentinel->m_cchName = 0;
        pSentinel->m_type = JsonHidden;
        pRootValue->m_nextIndex = dataIndex;
    }
    else
    {
        pValue->m_cbData = cbData;

        // Set up data.
        if (pbData != nullptr)
        {
            auto pData = static_cast<char const*>(pbData);
            if (pOldStorageData != pNewStorageData && pOldStorageData < pData &&
                pData < pOldStorageData + (valueIndex * StorageSize))
            {
                // They're copying the data from within the vector, and we just
                // resized out from under them. Technically, we could consider
                // this a bug in the caller, but it's an easy mistake to make,
                // easy to miss in testing, and hard to diagnose when it hits.
                // Dealing with this is easy for us, so just fix up the problem
                // instead of making it an error.
                pData = pNewStorageData + (pData - pOldStorageData);
            }

            memcpy(m_storage.data() + dataIndex, pData, cbData);
        }
    }

    return valueIndex;
}

void swap(JsonBuilder& a, JsonBuilder& b) throw()
{
    a.swap(b);
}

// JsonImplementType

/*
The macro-based GetUnchecked and ConvertTo (for f32, u8, u16, u32, i8, i16,
and i32) aren't perfectly optimal... But they're probably close enough.
*/

#define IMPLEMENT_AddValue(DataType, DataSize, DataPtr, ValueType) \
    JsonIterator JsonImplementType<DataType>::AddValue(            \
        JsonBuilder& builder,                                      \
        bool front,                                                \
        JsonConstIterator const& itParent,                         \
        nonstd::string_view const& name,                              \
        DataType const& data)                                      \
    {                                                              \
        return builder.AddValue(                                   \
            front, itParent, name, ValueType, DataSize, DataPtr);  \
    }

#define IMPLEMENT_GetUnchecked(DataType, ValueType)                   \
    DataType JsonImplementType<DataType>::GetUnchecked(               \
        JsonValue const& value) throw()                               \
    {                                                                 \
        return static_cast<DataType>(GetUnchecked##ValueType(value)); \
    }

#define IMPLEMENT_JsonImplementType(DataType, ValueType)          \
    IMPLEMENT_AddValue(DataType, sizeof(data), &data, ValueType)  \
    IMPLEMENT_GetUnchecked(DataType, ValueType)                   \
                                                                  \
    bool JsonImplementType<DataType>::ConvertTo(                  \
        JsonValue const& value, DataType& result) throw()         \
    {                                                             \
        return ConvertTo##ValueType(value, result);               \
    }

// JsonBool

bool JsonImplementType<bool>::GetUnchecked(JsonValue const& value) throw()
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

bool JsonImplementType<bool>::ConvertTo(JsonValue const& value, bool& result) throw()
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

IMPLEMENT_AddValue(bool, sizeof(data), &data, JsonBool)

// JsonUInt

bool JsonImplementType<unsigned long long>::ConvertTo(
    JsonValue const& value,
    unsigned long long& result) throw()
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

    case JsonFloat:
    {
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

IMPLEMENT_AddValue(unsigned long long, sizeof(data), &data, JsonUInt)
IMPLEMENT_GetUnchecked(unsigned long long, JsonUInt)

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

IMPLEMENT_JsonImplementType(unsigned char, JsonUInt)
IMPLEMENT_JsonImplementType(unsigned short, JsonUInt)
IMPLEMENT_JsonImplementType(unsigned int, JsonUInt)
IMPLEMENT_JsonImplementType(unsigned long, JsonUInt)

// JsonInt

bool JsonImplementType<signed long long>::ConvertTo(
    JsonValue const& value,
    signed long long& result) throw()
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

    case JsonFloat:
    {
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

IMPLEMENT_AddValue(signed long long, sizeof(data), &data, JsonInt)
IMPLEMENT_GetUnchecked(signed long long, JsonInt)

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

IMPLEMENT_JsonImplementType(signed char, JsonInt)
IMPLEMENT_JsonImplementType(signed short, JsonInt)
IMPLEMENT_JsonImplementType(signed int, JsonInt)
IMPLEMENT_JsonImplementType(signed long, JsonInt)

// JsonFloat

double JsonImplementType<double>::GetUnchecked(JsonValue const& value) throw()
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
    double& result) throw()
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

IMPLEMENT_AddValue(double, sizeof(data), &data, JsonFloat)

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

IMPLEMENT_JsonImplementType(float, JsonFloat)

// JsonUtf8

JsonIterator JsonImplementType<char*>::AddValue(
    JsonBuilder& builder,
    bool front,
    JsonConstIterator const& itParent,
    nonstd::string_view const& name,
    char const* psz)
{
    return builder.AddValue(
        front, itParent, name, JsonUtf8, static_cast<unsigned>(strlen(psz)), psz);
}

IMPLEMENT_AddValue(char, sizeof(data), &data, JsonUtf8)
IMPLEMENT_AddValue(std::string, data.size(), data.data(), JsonUtf8)

nonstd::string_view
JsonImplementType<nonstd::string_view>::GetUnchecked(JsonValue const& value) throw()
{
    assert(value.Type() == JsonUtf8);
    unsigned cb;
    void const* pb = value.Data(&cb);
    return nonstd::string_view(static_cast<char const*>(pb), cb);
}

bool JsonImplementType<nonstd::string_view>::ConvertTo(
    JsonValue const& value,
    nonstd::string_view& result) throw()
{
    bool success;

    if (value.Type() == JsonUtf8)
    {
        result = GetUnchecked(value);
        success = true;
    }
    else
    {
        result = nonstd::string_view();
        success = false;
    }

    return success;
}

JsonIterator JsonImplementType<nonstd::string_view>::AddValue(
    JsonBuilder& builder,
    bool front,
    JsonConstIterator const& itParent,
    nonstd::string_view const& name,
    nonstd::string_view const& data)
{
    return builder.AddValue(
        front,
        itParent,
        name,
        JsonUtf8,
        static_cast<unsigned>(data.size()),
        data.data());
}

// JsonTime

JsonIterator JsonImplementType<std::chrono::system_clock::time_point>::AddValue(
    JsonBuilder& builder,
    bool front,
    JsonConstIterator const& itParent,
    nonstd::string_view const& name,
    std::chrono::system_clock::time_point const& data)
{
    std::chrono::nanoseconds nanosSinceEpoch =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            data.time_since_epoch());

    int64_t nanosInt64 = nanosSinceEpoch.count();
    return builder.AddValue(
        front, itParent, name, JsonTime, sizeof(nanosInt64), &nanosInt64);
}

bool JsonImplementType<std::chrono::system_clock::time_point>::ConvertTo(
    JsonValue const& jsonValue,
    std::chrono::system_clock::time_point& value) throw()
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
    JsonValue const& jsonValue) throw()
{
    assert(jsonValue.Type() == JsonTime);
    assert(jsonValue.DataSize() == 8);
    if (jsonValue.DataSize() == 8)
    {
        int64_t nanosSinceEpoch = *static_cast<const int64_t*>(jsonValue.Data());
        return std::chrono::system_clock::time_point{ std::chrono::nanoseconds{
            nanosSinceEpoch } };
    }
    else
    {
        return {};
    }
}

// JsonUuid

static const UuidStruct k_emptyUuid = {};

IMPLEMENT_AddValue(UuidStruct, sizeof(UuidStruct), &data, JsonUuid)

bool JsonImplementType<UuidStruct>::ConvertTo(
    JsonValue const& jsonValue,
    UuidStruct& value) throw()
{
    bool success;

    if (jsonValue.Type() == JsonUuid)
    {
        assert(jsonValue.DataSize() == 16);
        value = *static_cast<const UuidStruct*>(jsonValue.Data());
        success = true;
    }
    else
    {
        value = k_emptyUuid;
        success = false;
    }

    return success;
}

UuidStruct
JsonImplementType<UuidStruct>::GetUnchecked(JsonValue const& jsonValue) throw()
{
    assert(jsonValue.Type() == JsonUuid);
    assert(jsonValue.DataSize() == 16);

    return jsonValue.DataSize() == 16 ?
        *static_cast<const UuidStruct*>(jsonValue.Data()) :
        k_emptyUuid;
}

}  // namespace jsonbuilder
