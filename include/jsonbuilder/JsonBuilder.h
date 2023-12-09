// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

/*
JsonBuilder stores data values of arbitrary type in an optimized tree
structure (i.e. values may have children). The JsonBuilder type is suitable for
storing any kind of hierarchical data. It was originally designed as a data
structure for building up a payload that will be serialized into a JSON string,
though it can be used in other scenarios that require similar organization.

Summary:

- enum JsonType
  Indicates the type of a value that is stored in a JsonBuilder.
- class JsonValue
  Interface to a value that is stored in a JsonBuilder.
- class JsonBuilder
  Object that stores a tree of values.
- class JsonImplementType<T>
  Traits type used to extend JsonBuilder to work with a user-defined type.

JsonBuilder design decisions:

- Follows STL container design conventions (e.g. begin() and end() methods).
- Optimized for building up, lightly-manipulating, and rendering payloads.
- Less-optimized for searching through payloads, i.e. items are not indexed.
  For example, finding a value with a particular name means iterating through
  all of the parent's children and checking each child for a matching name.
- Nodes in the tree are Simple or Complex. Simple nodes contain typed values
  (type tag plus binary data) but have no children. Complex nodes contain no
  data but may have child nodes.
- Simple nodes support arbitrary binary payloads. Any type of data can be
  stored with any type tag, and the JsonBuilder itself performs no validation
  of the stored data (though it does provide built-in support for creating
  or accessing the contents of nodes of certain built-in types). The
  JsonRenderer class and the convenience methods have built-in support for data
  stored as signed-integer (1, 2, 4, or 8 bytes), unsigned-integer (1, 2, 4, or
  8 bytes), floating-point (4 or 8 bytes), boolean (true/false), null, time
  (in 100ns units since 1601, i.e. Windows FILETIME), UUID (big-endian order),
  and string (UTF-8). The convenience methods and the renderer can be extended
  if other types are needed.
- Complex nodes come in two types: Object and Array. The JsonBuilder itself
  makes no distinction between these two types (they have identical behavior)
  but it is intended that the Object type contain named values (i.e. it is a
  dictionary with string keys) and that the Array type contain anonymous values
  (i.e. it is a list).
- Value name limited to 16M bytes (UTF-8) per value.
- Value data limited to 3GB per value.
- Names are stored as UTF-8.
- Memory usage for Complex (object and array) values is (in bytes):
  20 + sizeof(name) + padding to a multiple of 4.
- Memory usage for Simple (all other) values is (in bytes):
  12 + sizeof(name) + sizeof(data) + padding to multiple of 4.
- Total storage limited to 16GB per JsonBuilder (or available VA space).

Error handling:

- Some functions may throw std::bad_alloc for memory allocation failure or
  std::length_error if a size limit is exceeded.
- Some functions may assert for precondition violation.
- JsonBuilder uses JsonThrowBadAlloc(), JsonThrowLengthError(), and
  JsonThrowInvalidArgument() functions to raise exceptions. The
  library-provided implementations simply throw the corresponding exception
  (e.g. std::bad_alloc). If necessary, you can override these at link time in
  your own project by providing implementations as follows:

#include <jsonbuilder/JsonBuilder.h>
namespace jsonbuilder
{
    [[noreturn]] void _jsonbuilderDecl JsonThrowBadAlloc() noexcept(false)
    {
        throw MyBadAllocException();
    }
    [[noreturn]] void _jsonbuilderDecl JsonThrowLengthError(const char* what) noexcept(false)
    {
        throw MyLengthErrorException(what);
    }
    [[noreturn]] void _jsonbuilderDecl JsonThrowInvalidArgument(const char* what) noexcept(false)
    {
        throw MyInvalidArgumentException(what);
    }
}
*/

#pragma once

#include <chrono>       // std::chrono::system_clock::time_point
#include <iterator>     // std::forward_iterator_tag
#include <string_view>  // std::string_view
#include <type_traits>  // std::decay

#ifdef _WIN32
#include <sal.h>
#endif
#ifndef _In_z_
#define _In_z_
#endif
#ifndef _In_reads_
#define _In_reads_(c)
#endif
#ifndef _In_reads_bytes_
#define _In_reads_bytes_(cb)
#endif
#ifndef _Out_opt_
#define _Out_opt_
#endif
#ifndef _Out_writes_bytes_
#define _Out_writes_bytes_(cb)
#endif

// _jsonbuilderDecl - calling convention used by free functions:
#ifndef _jsonbuilderDecl
#ifdef _WIN32
#define _jsonbuilderDecl __stdcall
#else
#define _jsonbuilderDecl
#endif
#endif // _jsonbuilderDecl

namespace jsonbuilder {

// Forward declarations

class JsonValue;
class JsonBuilder;
template<class T>
class JsonImplementType;

// Default implementation throws std::bad_alloc.
// Can be replaced at link time.
[[noreturn]] void _jsonbuilderDecl JsonThrowBadAlloc() noexcept(false);

// Default implementation throws std::length_error.
// Can be replaced at link time.
[[noreturn]] void _jsonbuilderDecl JsonThrowLengthError(_In_z_ const char* what) noexcept(false);

// Default implementation throws std::invalid_argument.
// Can be replaced at link time.
[[noreturn]] void _jsonbuilderDecl JsonThrowInvalidArgument(_In_z_ const char* what) noexcept(false);

// Internal implementation details

namespace JsonInternal {

using JSON_UINT32 = decltype(0xFFFFFFFFu);
using JSON_UINT64 = decltype(0xFFFFFFFFFFFFFFFFu);
using JSON_SIZE_T = decltype(sizeof(0));
using JSON_PTRDIFF_T = decltype((char*)0 - (char*)0);

static_assert(sizeof(JSON_UINT32) == 4, "Bad UINT32");
static_assert(sizeof(JSON_UINT64) == 8, "Bad UINT64");

/*
PodVector:

Very simple vector class. For POD types only.

This vector does not initialize data on resize unless specifically requested.
In benchmarks, using PodVector instead of VC2013's std::vector improves
JsonBuilder and JsonRenderer performance by about 10%.
*/

class PodVectorBase
{
protected:

    using size_type = JSON_UINT32;

    /*
    assert(index < currentSize)
    */
    static void CheckOffset(size_type index, size_type currentSize) noexcept;

    /*
    assert(p1 <= p2 <= p3)
    */
    static void
    CheckRange(void const* p1, void const* p2, void const* p3) noexcept;

    /*
    return checked(a + b)
    */
    static size_type CheckedAdd(
        size_type a,
        size_type b)
        noexcept(false);  // may throw length_error

    /*
    memcpy(pDest, pSource, cb)
    */
    static void InitData(
        _Out_writes_bytes_(cb) void* pDest,
        _In_reads_bytes_(cb) void const* pSource,
        JSON_SIZE_T cb) noexcept;

    /*
    Returns a value newCapacity such that minCapacity <= newCapacity <=
    maxCapacity, chosen according to a vector growth policy. The current policy
    is:
    - If maxCapacity < minCapacity or maxCapacity < 15, assert.
    - If minCapacity < 15, return 15.
    - Let cap = the smallest value 2^N-1 that is greater than or equal to
    minCapacity.
    - If cap < maxCapacity, return cap. Else return maxCapacity.
    */
    static size_type GetNewCapacity(size_type minCapacity, size_type maxCapacity);

    /*
    Calls malloc. If allocation fails, throw bad_alloc.
    */
    static void* Allocate(
        JSON_SIZE_T cb,
        bool zeroInitializeMemory)
        noexcept(false);  // may throw bad_alloc, length_error

    /*
    Calls free.
    */
    static void Deallocate(void* pb) noexcept;
};

template<class T>
class PodVector : private PodVectorBase
{
    static size_type const
        m_maxSize = ~JSON_SIZE_T(0) / sizeof(T) > size_type(~size_type(1)) ?
        size_type(~size_type(1)) :
        ~JSON_SIZE_T(0) / sizeof(T);
    static_assert(
        m_maxSize < size_type(m_maxSize + 1),
        "Bad calculation of m_maxSize (1)");
    static_assert(
        m_maxSize * sizeof(T) / sizeof(T) == m_maxSize,
        "Bad calculation of m_maxSize (2)");

    T* m_data;
    size_type m_size;
    size_type m_capacity;
    bool m_zeroInitializeMemory;

  public:
    using PodVectorBase::size_type;

    ~PodVector() noexcept { Deallocate(m_data); }

    PodVector() noexcept
        : m_data(nullptr), m_size(0), m_capacity(0), m_zeroInitializeMemory(false)
    {
        return;
    }

    PodVector(PodVector&& other) noexcept
        : m_data(other.m_data)
        , m_size(other.m_size)
        , m_capacity(other.m_capacity)
        , m_zeroInitializeMemory(other.m_zeroInitializeMemory)
    {
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_capacity = 0;
        other.m_zeroInitializeMemory = false;
    }

    PodVector(PodVector const& other)
        noexcept(false) // may throw bad_alloc
        : m_data(nullptr)
        , m_size(other.m_size)
        , m_capacity(other.m_size)
        , m_zeroInitializeMemory(other.m_zeroInitializeMemory)
    {
        if (m_size != 0)
        {
            auto cb = m_size * sizeof(T);
            m_data = static_cast<T*>(Allocate(cb, m_zeroInitializeMemory));
            InitData(m_data, other.m_data, cb);
        }
    }

    PodVector(
        T const* data,
        size_type size)
        noexcept(false) // may throw bad_alloc
        : m_data(nullptr)
        , m_size(size)
        , m_capacity(size)
        , m_zeroInitializeMemory(false)
    {
        if (m_size != 0)
        {
            auto cb = m_size * sizeof(T);
            m_data = static_cast<T*>(Allocate(cb, m_zeroInitializeMemory));
            InitData(m_data, data, cb);
        }
    }

    PodVector& operator=(PodVector&& other) noexcept
    {
        PodVector(static_cast<PodVector&&>(other)).swap(*this);
        return *this;
    }

    PodVector& operator=(PodVector const& other)
        noexcept(false) // may throw bad_alloc
    {
        PodVector(other).swap(*this);
        return *this;
    }

    static constexpr size_type max_size() noexcept { return m_maxSize; }

    size_type size() const noexcept { return m_size; }

    bool empty() const noexcept { return m_size == 0; }

    size_type capacity() const noexcept { return m_capacity; }

    T const* data() const noexcept { return m_data; }

    T* data() noexcept { return m_data; }

    T const& operator[](unsigned i) const noexcept
    {
        CheckOffset(i, m_size);
        return m_data[i];
    }

    T& operator[](unsigned i) noexcept
    {
        CheckOffset(i, m_size);
        return m_data[i];
    }

    void clear() { m_size = 0; }

    void push_back(T const& val)
        noexcept(false) // may throw bad_alloc, length_error
    {
        if (m_size == m_capacity)
        {
            Grow();
        }
        m_data[m_size++] = val;
    }

    void append(
        T const* pItems,
        size_type cItems)
        noexcept(false) // may throw bad_alloc, length_error
    {
        if (cItems > m_capacity - m_size)
        {
            GrowBy(cItems);
        }

        InitData(m_data + m_size, pItems, cItems * sizeof(T));
        m_size += cItems;
    }

    void append(
        size_type cCopies,
        T const& val)
        noexcept(false) // may throw bad_alloc, length_error
    {
        if (cCopies > m_capacity - m_size)
        {
            GrowBy(cCopies);
        }

        auto p = m_data + m_size;
        for (size_type i = 0; i != cCopies; i += 1)
        {
            p[i] = val;
        }

        m_size += cCopies;
    }

    void reserve(size_type minCapacity)
        noexcept(false) // may throw bad_alloc, length_error
    {
        if (m_capacity < minCapacity)
        {
            GrowTo(minCapacity);
        }
    }

    /*
    NOTE: new items are uninitialized, unless m_zeroInitializeMemory is set
    */
    void resize(size_type newSize)
        noexcept(false) // may throw bad_alloc, length_error
    {
        reserve(newSize);
        m_size = newSize;
    }

    void swap(PodVector& other) noexcept
    {
        auto const d = m_data;
        m_data = other.m_data;
        other.m_data = d;

        auto const s = m_size;
        m_size = other.m_size;
        other.m_size = s;

        auto const c = m_capacity;
        m_capacity = other.m_capacity;
        other.m_capacity = c;

        auto const z = m_zeroInitializeMemory;
        m_zeroInitializeMemory = other.m_zeroInitializeMemory;
        other.m_zeroInitializeMemory = z;
    }

    void EnableZeroInitializeMemory() { m_zeroInitializeMemory = true; }

    /*
    Ensures capacity for at least cItems additional elements, then returns
    a pointer to the current end of the vector. Caller can write to this
    pointer, then call SetEndPointer to include the newly-written data in
    the vector.

    auto p = v.GetAppendPointer(10);
    p[0] = ...; // p has room for 10 items.
    v.SetEndPointer(p + 10); // Include the newly-written items in vector.
    */
    T* GetAppendPointer(unsigned cItems)
        noexcept(false) // may throw bad_alloc, length_error
    {
        if (cItems > m_capacity - m_size)
        {
            GrowBy(cItems);
        }
        return m_data + m_size;
    }

    void SetEndPointer(T* pNewEnd) noexcept
    {
        CheckRange(m_data, pNewEnd, m_data + m_capacity);
        m_size = static_cast<size_type>(pNewEnd - m_data);
    }

  private:
    void Grow()
        noexcept(false) // may throw bad_alloc, length_error
    {
        GrowTo(m_capacity + 1);
    }

    void GrowBy(size_type cItems)
        noexcept(false) // may throw bad_alloc, length_error
    {
        GrowTo(CheckedAdd(m_size, cItems));
    }

    void GrowTo(size_type minCapacity)
        noexcept(false) // may throw bad_alloc, length_error
    {
        auto const newCapacity = GetNewCapacity(minCapacity, m_maxSize);
        auto const newData = static_cast<T*>(Allocate(newCapacity * sizeof(T), m_zeroInitializeMemory));
        InitData(newData, m_data, m_size * sizeof(T));
        Deallocate(m_data);
        m_data = newData;
        m_capacity = newCapacity;
    }
};
}  // namespace JsonInternal
// namespace JsonInternal

// JsonType

/*
The built-in types supported by JsonBuilder and JsonRenderer.
Note that type values larger than 255 will not work correctly.
Note that JsonBuilder itself is actually only aware of types JsonHidden,
JsonArray and JsonObject. All of the other types are ignored by JsonBuilder.
There is no enforcement of any rules such as "names are not allowed on array
elements" or "names must be unique within an object". The JsonBuilder just
stores the names and data as binary blobs.

Additional simple types can be used with JsonBuilder as necessary. (It is not
possible to add new complex types, i.e. user-defined types cannot have child
nodes.) To add new types in your own project:

- Define JsonType constants for your types. Start numbering at 1. For example:
  const JsonType JsonMyStuff = static_cast<JsonType>(1);
- Define a class that is derived from JsonRenderer. Override the RenderCustom
  method to render values of your types. Use your derived class instead of
  using JsonRenderer.
- If you want jsonBuilder.push_back(..., data), jsonValue.GetUnchecked<T>(),
  and jsonValue.ConvertTo<T> to work for your types,
  define specializations of JsonImplementType<T> for each of your types.
*/
enum JsonType
    : JsonInternal::JSON_UINT32 // Note: Used as an 8-bit bitfield of UINT32.
{
    // Numbering for custom types should start at 1. Custom types never have
    // children. Numbering for custom types must not exceed 200.
    JsonTypeReserved = 201,
    JsonTypeBuiltIn = 244,
    JsonUtf8,    // No children. Data = UTF-8 string.
    JsonUInt,    // No children. Data = uint (1, 2, 4, or 8 bytes, little-endian).
    JsonInt,     // No children. Data = int (1, 2, 4, or 8 bytes, little-endian).
    JsonFloat,   // No children. Data = float (4 or 8 bytes, little-endian).
    JsonBool,    // No children. Data = bool (1 or 4 bytes, little-endian).
    JsonTime,    // No children. Data = int64 (number of 100ns intervals since
                 // 1601-01-01T00:00:00Z, compatible with Win32 FILETIME).
    JsonUuid,    // No children. Data = UUID (16 bytes universally unique
                 // identifier, network byte order, compatible with uuid_t,
                 // NOT the same byte order as Windows GUID).
    JsonNull,    // No children. Data = void (0 bytes).
    JsonHidden,  // An erased or sentinel value. No data. (Calling Data() on a
                 // hidden value is an error.)
    JsonArray,   // Anonymous children. No data. (Calling Data() on an array
                 // value is an error.)
    JsonObject,  // Named children. No data. (Calling Data() on an object value
                 // is an error.)
};

// JsonValue

/*
For internal use only.
Used for hidden erased or sentinel nodes, which do not need m_cbData.
*/
class JsonValueBase
{
    using JSON_UINT32 = JsonInternal::JSON_UINT32;
    friend class JsonBuilder;
    friend class JsonValue;
    using Index = JSON_UINT32;

    Index m_nextIndex;  // The index of the "next" node. (Nodes form a
                        // singly-linked list).
    JSON_UINT32 m_cchName : 24;
    JsonType m_type : 8;
};

/*
Each value in a JsonBuilder is represented as a JsonValue. Each JsonValue
stores:

- Type - a value from the JsonType enumeration, or a custom type.
- Name - UTF-8 string, up to 16M bytes.
- Data - binary blob, up to 3GB.

Note that Object, Array, and hidden values do not contain data.
- It is an error to use a non-zero value for cbData when creating an
array/object.
- It is an error to call the Data() method on an array/object/hidden value.
*/
class JsonValue : private JsonValueBase
{
    friend class JsonBuilder;
    using StoragePod = JSON_UINT32;

    union
    {
        JSON_UINT32 m_cbData;    // Normal node only (not present for sentinel,
                                 // array, or object)
        Index m_lastChildIndex;  // Array/Object node only (not present for
                                 // sentinel or normal)
    };

    /*
    Implementation details:

    The JsonBuilder stores a vector<StoragePod>, which is just storage for
    raw data. StoragePod could just be a byte, but storing at a larger
    granularity gives benefits, especially in terms of data alignment. At
    present StoragePod is uint32, so data is 32-bit aligned. This gives
    perfect alignment for everything except float64 and int64, at a price of
    up to 5 bytes per value wasted as padding (up to 2 bytes of padding after
    the name, and up to 3 bytes of padding after the data). Within the vector,
    information is arranged into nodes. The location within the vector where
    the node begins is used as the node's index, e.g.
    JsonValue& node = reinterpret_cast<JsonValue&>(storageVector[index]);

    Nodes are arranged into a tree. Only Array and Object nodes can be
    interior nodes of the tree. All other nodes are leaf nodes. When an array
    or object node is created, its first child is also created immediately,
    directly following the Array or Object node in the storage vector. The
    first child is always a hidden sentinel node. Since the first child always
    immediately follows its parent array or object node, we can easily compute
    the index of the first child as:

        parent.FirstChildIndex = parent.Index + DATA_OFFSET(parent.cchName).

    Since sentinel nodes are always hidden and never have data, we omit the
    m_cbData member, saving 4 bytes per array/object.

    Since Array and Object nodes never have data, we reuse the m_cbData field
    to store the index of the last child. If FirstChildIndex == LastChildIndex,
    the parent has no visible children (its only child is the hidden sentinel).

    Nodes are arranged into a singly-linked list via the m_nextIndex member.
    The end of the list is indicated by m_nextIndex == 0. All children of the
    same parent are together in the list, so iteration from Node.FirstChild to
    Node.LastChild covers all of the children of Node.

    If it exists, the node for the root object is stored at index 0, and its
    associated hidden first child is always at index 3 (root.cchName == 0, so
    DATA_OFFSET always returns 3 for root). Note that the root object is
    created lazily, so it is possible for the root object to not actually be
    stored (i.e. storageVector.size() might be 0). The root object is created
    the first time any child node is created. The root object serves as both
    the head and the tail of the singly-linked list of nodes.

    Nodes are erased by marking them as hidden. They are not removed from the
    storage vector and are not removed from the linked list. (Changing this
    would require several other changes such as adding a parentIndex and a
    prevIndex to each node.) Hidden nodes are skipped during iterator
    traversal. As long as there are no erased nodes, all iterator operations
    are essentially O(1). However, iteration must skip hidden/erased nodes,
    so begin(), begin(itParent), end(itParent), and operator++ operations can
    potentially become O(N) if there are a large number of erased or childless
    nodes.

    Note that methods in this class must never return a hidden node -- they
    must return the first non-hidden node after the node they might have
    otherwise returned. Because the external interface abstracts these hidden
    nodes away, external implementations of count() or find() must be slightly
    sub-optimal (they always have to find end(itParent), which is the first
    non-hidden node AFTER itParent's last child). Internal implementations of
    count(), find(), splice(), etc. do take advantage of the ability to
    reference hidden nodes to become slightly more efficient -- they stop
    iteration AT itParent's last child (hidden or not) instead of stopping
    AFTER itParent's last child.

    Normal node format:
     0: m_nextIndex (4 bytes)
     4: m_cchName   (3 bytes)
     7: m_type      (1 byte)
     8: m_cbData    (4 bytes)
    12: Name        (cchName * 2 bytes)
    xx: Padding     (to a multiple of StoragePod size)
    xx: Data        (cbValue bytes)
    xx: Padding     (to a multiple of StoragePod size)

    Composite node format (array or object):
     0: m_nextIndex      (4 bytes)
     4: m_cchName        (3 bytes)
     7: m_type           (1 byte)  // JsonObject, JsonArray.
     8: m_lastChildIndex (4 bytes)
    12: Name             (cchName * 2 bytes)
    xx: Padding          (to a multiple of StoragePod size)
    xx: FirstChild       (8 bytes) - a sentinel node.

    Hidden/sentinel node format:
     0: m_nextIndex      (4 bytes)
     4: m_cchName        (3 bytes)
     7: m_type           (1 byte)  // JsonHidden.
    */

  public:
    JsonValue(JsonValue const&) = delete;
    void operator=(JsonValue const&) = delete;

    /*
    Gets the type of the value.
    */
    JsonType Type() const noexcept;

    /*
    Gets the name of the value.
    */
    std::string_view Name() const noexcept;

    /*
    Gets the size of the data of the value, in bytes.
    Note that hidden, object, and array values do not have data, and it is an
    error to call DataSize() on a value where Type() is hidden, object, or
    array.
    */
    unsigned DataSize() const noexcept;

    /*
    Reduces the size recorded for the data.
    Does not change the size of the underlying buffer.
    Requires: cbNew <= DataSize().
    */
    void ReduceDataSize(unsigned cbNew) noexcept;

    /*
    Gets a pointer to the data of the value.
    Note that hidden, object, and array values do not have data, and it is an
    error to call Data() on a value where Type() is hidden, object, or array.
    */
    void const* Data(_Out_opt_ unsigned* pcbData = nullptr) const noexcept;

    /*
    Gets a pointer to the data of the value. The data can be modified, though
    the length can only be reduced, never increased.
    Note that hidden, object, and array values do not have data, and it is an
    error to call Data() on a value where Type() is hidden, object, or array.
    */
    void* Data(_Out_opt_ unsigned* pcbData = nullptr) noexcept;

    /*
    Returns true if Type==Null.
    */
    bool IsNull() const noexcept;

    /*
    Returns the value's data as a T.
    Requires: this->Type() is an exact match for type T. (Checked via assert.)
    Requires: this->DataSize() is a valid size for this->Type(). (Checked by
      assert, returns 0 in retail if size is invalid.)

    - This method asserts that this->Type() is an exact match for T. For
      example, T=int matches JsonInt but does not match JsonUInt.
    - This method asserts that this->DataSize() makes sense for the target type.
      For example, T=int means DataSize() must be 1, 2, 4, or 8.
    - This method behaves much like a reinterpret_cast, but it takes the value's
      actual data size into account. For example, if T=int and DataSize() is 1,
      this method returns *reinterpret_cast<int8*>(Data()).
    - If running in retail and data size does not make sense for the target
      type, this method returns a default value of T (typically 0). For example,
      if T=int and DataSize() is 3, this method will assert and then return 0.

    T must be a supported type. Supported types include:

    - For boolean data: bool.
    - For UTF-8 string data: std::string_view.
    - For integer data: signed and unsigned char, short, int, long, long long.
    - For float data: float, double.
    - For time data: TimeStruct, std::chrono::system_clock::time_point.
    - For UUID data: UuidStruct.
    - Any user-defined type for which JsonImplementType<T>::GetUnchecked exists.

    Detailed semantics:
    assert(this->Type() is an exact match for typeof(T));
    If DataSize() is a legal size for T, return the stored data as a T;
    Else assert(false), return a default value of T (e.g. 0);
    */
    template<class T>
    auto GetUnchecked() const noexcept
        -> decltype(JsonImplementType<typename std::decay<T>::type>::GetUnchecked(
            *(JsonValue const*) 0))
    {
        return JsonImplementType<typename std::decay<T>::type>::GetUnchecked(
            *this);
    }

    /*
    Attempts to convert the data to T.
    If the data can be converted, sets result to the data and returns true.
    Else sets result to a default value (e.g. 0) and returns false.

    For integer and float conversions, performs a range check. If source value
    is out of the target type's range, returns false.

    T must be a supported type. Supported types include:

    - For boolean data: bool.
    - For UTF-8 string data: std::string_view.
    - For integer data: signed and unsigned char, short, int, long, long long.
    - For float data: float, double.
    - For time data: TimeStruct, std::chrono::system_clock::time_point.
    - For UUID data: UuidStruct.
    - Any user-defined type for which JsonImplementType<T>::ConvertTo exists.
    */
    template<
        class T,
        class ConvertToDoesNotSupportThisType =
            decltype(JsonImplementType<typename std::decay<T>::type>::ConvertTo(
                *(JsonValue const*) 0,
                *(T*) 0))>
    bool ConvertTo(T& result) const noexcept
    {
        return JsonImplementType<typename std::decay<T>::type>::ConvertTo(
            *this, result);
    }
};

// JsonIterator

/*
Implementation of JsonBuilder::const_iterator.
*/
class JsonConstIterator
{
    friend class JsonBuilder;  // JsonBuilder needs to construct const_iterators.
    using Index = JsonInternal::JSON_UINT32;

    JsonBuilder const* m_pContainer;
    Index m_index;

    JsonConstIterator(JsonBuilder const* pContainer, Index index) noexcept;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = JsonValue;
    using difference_type = JsonInternal::JSON_PTRDIFF_T;
    using pointer = JsonValue const*;
    using reference = JsonValue const&;

    JsonConstIterator() noexcept;

    bool operator==(JsonConstIterator const&) const noexcept;
    bool operator!=(JsonConstIterator const&) const noexcept;

    reference operator*() const noexcept;
    pointer operator->() const noexcept;

    JsonConstIterator& operator++() noexcept;
    JsonConstIterator operator++(int) noexcept;

    /*
    For iterating over this value's children.
    iterator.begin() is equivalent to builder.begin(iterator).
    O(1), unless hidden nodes need to be skipped.
    */
    JsonConstIterator begin() const noexcept;

    /*
    For iterating over this value's children.
    iterator.end() is equivalent to builder.end(iterator).
    O(1), unless hidden nodes need to be skipped.
    */
    JsonConstIterator end() const noexcept;

    /*
    Returns true if this iterator refers to the root object of a JsonBuilder.
    Note that it is an error to dereference or ++ an iterator that references
    the root object (e.g. it is an error to do jsonBuilder.end()++).
    */
    bool IsRoot() const noexcept;
};

/*
Implementation of JsonBuilder::iterator.
*/
class JsonIterator : public JsonConstIterator
{
    friend class JsonBuilder;  // JsonBuilder needs to construct iterators.

    /*
    A JsonIterator is created by creating a JsonConstIterator and passing it
    to the constructor of a JsonIterator. This avoids the need for
    JsonConstIterator to declare JsonIterator as a friend.
    */
    explicit JsonIterator(JsonConstIterator const&) noexcept;

  public:
    typedef JsonValue& reference;
    typedef JsonValue* pointer;

    JsonIterator() noexcept;

    reference operator*() const noexcept;
    pointer operator->() const noexcept;

    JsonIterator& operator++() noexcept;
    JsonIterator operator++(int) noexcept;

    /*
    For iterating  over this value's children.
    iterator.begin() is equivalent to builder.begin(iterator).
    O(1), unless hidden nodes need to be skipped.
    */
    JsonIterator begin() const noexcept;

    /*
    For iterating  over this value's children.
    iterator.end() is equivalent to builder.end(iterator).
    O(1), unless hidden nodes need to be skipped.
    */
    JsonIterator end() const noexcept;
};

// JsonBuilder

/*
Stores data in a logical tree structure.

The tree stores values of various types.
Object values store no data but contain any number of named child values.
Array values store no data but contain any number of unnamed child values.
All other values are leaf nodes in the tree and can store arbitrary data.

The root of the tree is an Object. This object is implicit -- it is always
present and need not be added by the user. In methods that accept a "parent
iterator" parameter, use the root() iterator to refer to the root of the tree.
*/
class JsonBuilder
{
    friend class JsonConstIterator;
    typedef JsonValue::Index Index;
    typedef JsonInternal::PodVector<JsonValue::StoragePod> StorageVec;
    StorageVec m_storage;

    class Validator : private JsonInternal::PodVectorBase
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

  public:
    using value_type = JsonValue;
    using pointer = JsonValue*;
    using const_pointer = JsonValue const*;
    using size_type = JsonInternal::JSON_SIZE_T;
    using difference_type = JsonInternal::JSON_PTRDIFF_T;
    using iterator = JsonIterator;
    using const_iterator = JsonConstIterator;

    /*
    Initializes a new instance of the JsonBuilder class.
    */
    JsonBuilder() noexcept;

    /*
    Initializes a new instance of the JsonBuilder class using at least the
    specified initial capacity (in bytes).
    */
    explicit JsonBuilder(size_type cbInitialCapacity)
        noexcept(false);  // may throw bad_alloc, length_error

    /*
    Initializes a new instance of the JsonBuilder class, copying its data from
    other.
    */
    JsonBuilder(JsonBuilder const& other)
        noexcept(false);  // may throw bad_alloc

    /*
    Initializes a new instance of the JsonBuilder class, moving the data from
    other.
    NOTE: Invalidates all iterators pointing into other.
    */
    JsonBuilder(JsonBuilder&& other) noexcept;

    /*
    Initializes a new instance of the JsonBuilder class, copying its data from
    a memory buffer. Optionally runs ValidateData (i.e. for untrusted input).
    */
    JsonBuilder(
        _In_reads_bytes_(cbRawData) void const* pbRawData,
        size_type cbRawData,
        bool validateData = true)
        noexcept(false);  // may throw bad_alloc, length_error invalid_argument

    /*
    Copies data from other.
    */
    JsonBuilder& operator=(JsonBuilder const& other)
        noexcept(false);  // may throw bad_alloc

    /*
    Moves the data from other.
    NOTE: Invalidates all iterators pointing into other.
    */
    JsonBuilder& operator=(JsonBuilder&& other) noexcept;

    /*
    Throws an exception if the data in this JsonBuilder is corrupt.
    Mainly for use in debugging, but this can also be used when feeding
    untrusted data to JsonBuilder.
    */
    void ValidateData() const noexcept(false);  // May throw bad_alloc, invalid_argument.

    iterator begin() noexcept;
    const_iterator begin() const noexcept;
    const_iterator cbegin() const noexcept;

    iterator end() noexcept;
    const_iterator end() const noexcept;
    const_iterator cend() const noexcept;

    iterator root() noexcept;
    const_iterator root() const noexcept;
    const_iterator croot() const noexcept;

    /*
    Returns a pointer to the first element in the backing raw data vector.
    */
    void const* buffer_data() const noexcept;

    /*
    Returns the size (in bytes) of the memory currently being used by this
    JsonBuilder (i.e. vector.size() of the underlying storage vector). This
    value is primarily intended to help in determining the appropriate value
    for the initialCapacity constructor parameter. This is also the size of
    the data returned by buffer_data().
    */
    size_type buffer_size() const noexcept;

    /*
    Returns the size (in bytes) of the memory currently allocated by this
    JsonBuilder (i.e. vector.capacity() of the underlying storage vector).
    This value is primarily intended to help in determining the appropriate
    value for the initialCapacity constructor parameter.
    */
    size_type buffer_capacity() const noexcept;

    /*
    Returns the maximum size (in bytes) of the memory that could be passed
    to buffer_reserve or returned from buffer_size.
    On 32-bit systems, this is currently slightly less than 4GB.
    On 64-bit systems, this is currently slightly less than 16GB.
    */
    static constexpr size_type buffer_max_size() noexcept
    {
        return StorageVec::max_size() * sizeof(JsonValue::StoragePod);
    }

    /*
    If less than the specified amount of memory is currently allocated by this
    JsonBuilder, allocates additional memory so that at least the specified
    amount is allocated.
    */
    void buffer_reserve(size_type cbMinimumCapacity)
        noexcept(false);  // may throw bad_alloc, length_error

    /*
    Removes all data from this JsonBuilder and prepares it for reuse.
    Keeps the currently-allocated buffer.
    O(1).
    */
    void clear() noexcept;

    /*
    Marks the specified value as Erased. Equivalent to erase(itValue,
    itValue+1). Requires: itValue+1 is valid (i.e. requires that itValue !=
    end()). Returns: itValue+1. Implementation detail: Erased values have their
    Type() changed to Hidden, and will be skipped during iteration, but they
    continue to take up space in the tree. O(1), unless there are erased values
    that have to be skipped to find itValue+1.
    */
    iterator erase(const_iterator itValue) noexcept;

    /*
    Marks the specified range as Erased.
    Requires: itBegin <= itEnd (i.e. repeated itBegin++ will reach itEnd).
    Returns: itEnd.
    Implementation detail: Erased values have their Type() changed to Hidden,
    and will be skipped during iteration, but they continue to take up space
    in the tree.
    O(n), where n = the number of values in the range itBegin..itEnd.
    */
    iterator erase(const_iterator itBegin, const_iterator itEnd) noexcept;

    /*
    Replaces the contents of this with the contents of other.
    NOTE: Invalidates all iterators pointing into this and other.
    O(1).
    */
    void swap(JsonBuilder& other) noexcept;

    /*
    Causes all future memory allocations by this JsonBuilder to be initialized
    to zero.
    */
    void EnableZeroInitializeMemory()
    {
        m_storage.EnableZeroInitializeMemory();
    }

    /*
    Navigates to a child value, starting at the root. For each name provided,
    navigates to the first child with the specified name.
    Each name must be a string_view or implicitly-convertible to string_view.
    Returns the first match, or end() if there are no matches.
    O(n), where n is the total number of children at each level.
    */
    template<class... NameTys>
    iterator
    find(std::string_view const& firstName, NameTys const&... additionalNames) noexcept
    {
        return iterator(
            const_iterator(this, Find(0, firstName, additionalNames...)));
    }

    /*
    Navigates to a child value, starting at the root. For each name provided,
    navigates to the first child with the specified name.
    Each name must be a string_view or implicitly-convertible to string_view.
    Returns the first match, or end() if there are no matches.
    O(n), where n is the total number of children at each level.
    */
    template<class... NameTys>
    const_iterator
    find(std::string_view const& firstName, NameTys const&... additionalNames) const
        noexcept
    {
        return const_iterator(this, Find(0, firstName, additionalNames...));
    }

    /*
    Navigates to a child value, starting at itParent. For each name provided,
    navigates to the first child with the specified name.
    Each name must be a string_view or implicitly-convertible to string_view.
    Returns the first match, or end() if there are no matches.
    O(n), where n is the total number of children at each level.
    */
    template<class... NameTys>
    iterator find(
        const_iterator const& itParent,
        std::string_view const& firstName,
        NameTys const&... additionalNames) noexcept
    {
        ValidateIterator(itParent);
        return iterator(const_iterator(
            this, Find(itParent.m_index, firstName, additionalNames...)));
    }

    /*
    Navigates to a child value, starting at itParent. For each name provided,
    navigates to the first child with the specified name.
    Each name must be a string_view or implicitly-convertible to string_view.
    Returns the first match, or end() if there are no matches.
    O(n), where n is the total number of children at each level.
    */
    template<class... NameTys>
    const_iterator find(
        const_iterator const& itParent,
        std::string_view const& firstName,
        NameTys const&... additionalNames) const noexcept
    {
        ValidateIterator(itParent);
        return const_iterator(
            this, Find(itParent.m_index, firstName, additionalNames...));
    }

    /*
    Returns the number of children of itParent.
    O(n), where n is the number of children of itParent.
    */
    unsigned count(const_iterator const& itParent) const noexcept;

    /*
    Returns the first child of itParent.
    If itParent has no children, returns end(itParent).
    O(1), unless there are erased values that have to be skipped.
    */
    iterator begin(const_iterator const& itParent) noexcept;

    /*
    Returns the first child of itParent.
    If itParent has no children, returns end(itParent).
    O(1), unless there are erased values that have to be skipped.
    */
    const_iterator begin(const_iterator const& itParent) const noexcept;

    /*
    Returns the first child of itParent.
    If itParent has no children, returns cend(itParent).
    O(1), unless there are erased values that have to be skipped.
    */
    const_iterator cbegin(const_iterator const& itParent) const noexcept;

    /*
    Returns the iterator after the last child of itParent.
    O(1), unless there are erased values that have to be skipped.
    */
    iterator end(const_iterator const& itParent) noexcept;

    /*
    Returns the iterator after the last child of itParent.
    O(1), unless there are erased values that have to be skipped.
    */
    const_iterator end(const_iterator const& itParent) const noexcept;

    /*
    Returns the iterator after the last child of itParent.
    O(1), unless there are erased value that have to be skipped.
    */
    const_iterator cend(const_iterator const& itParent) const noexcept;

    /*
    Removes all children from itOldParent.
    Re-inserts them as the first children of itNewParent.
    Requires: itNewParent must reference an array or an object value.
    O(n), where n is the number of children of itOldParent.
    */
    void splice_front(
        const_iterator const& itOldParent,
        const_iterator const& itNewParent) noexcept
    {
        Splice(true, itOldParent, itNewParent, PredicateTrue());
    }

    /*
    Removes all children from itOldParent.
    Re-inserts them as the last children of itNewParent.
    Requires: itNewParent must reference an array or an object value.
    O(n), where n is the number of children of itOldParent.
    */
    void splice_back(
        const_iterator const& itOldParent,
        const_iterator const& itNewParent) noexcept
    {
        Splice(false, itOldParent, itNewParent, PredicateTrue());
    }

    /*
    Removes all children from itOldParent where pred(itChild) returns true.
    Re-inserts them as the first children of itNewParent.
    Requires: itNewParent must reference an array or an object value.
    O(n), where n is the number of children of itOldParent.
    */
    template<class PredTy>
    void splice_front(
        const_iterator const& itOldParent,
        const_iterator const& itNewParent,
        PredTy&& pred) noexcept
    {
        Splice(true, itOldParent, itNewParent, static_cast<PredTy&&>(pred));
    }

    /*
    Removes all children from itOldParent where pred(itChild) returns true.
    Re-inserts them as the last children of itNewParent.
    Requires: itNewParent must reference an array or an object value.
    O(n), where n is the number of children of itOldParent.
    */
    template<class PredTy>
    void splice_back(
        const_iterator const& itOldParent,
        const_iterator const& itNewParent,
        PredTy&& pred) noexcept
    {
        Splice(false, itOldParent, itNewParent, static_cast<PredTy&&>(pred));
    }

    /*
    Creates a new value. Inserts it as the first (if front is true) or last
    (if front is false) child of itParent.
    If pbData is null, the new value's payload is uninitialized.
    Requires: itParent must reference an array or an object value.
    Requires: if type is Array or Object, cbValue must be 0 and pbValue must be
    null. Returns: an iterator that references the new value. O(1).
    */
    iterator AddValue(
        bool front,
        const_iterator const& itParent,
        std::string_view const& name,
        JsonType type,
        unsigned cbData = 0,
        _In_reads_bytes_(cbData) void const* pbData = nullptr)
        noexcept(false);  // may throw bad_alloc, length_error

    /*
    Creates a new value. Inserts it as the first child of itParent.
    If pbData is null, the new value's payload is uninitialized.
    Requires: itParent must reference an array or an object value.
    Requires: if type is Array or Object, cbValue must be 0 and pbValue must be
    null. Returns: an iterator that references the new value. O(1).
    */
    iterator push_front(
        const_iterator const& itParent,
        std::string_view const& name,
        JsonType type,
        unsigned cbData = 0,
        _In_reads_bytes_(cbData) void const* pbData = nullptr)
        noexcept(false) // may throw bad_alloc, length_error
    {
        return AddValue(true, itParent, name, type, cbData, pbData);
    }

    /*
    Creates a new value. Inserts it as the first child of itParent.
    If pbData is null, the new value's payload is uninitialized.
    Requires: itParent must reference an array or an object value.
    Requires: if type is Array or Object, cbValue must be 0 and pbValue must be
    null. Returns: an iterator that references the new value. O(1).
    */
    iterator push_back(
        const_iterator const& itParent,
        std::string_view const& name,
        JsonType type,
        unsigned cbData = 0,
        _In_reads_bytes_(cbData) void const* pbData = nullptr)
        noexcept(false) // may throw bad_alloc, length_error
    {
        return AddValue(false, itParent, name, type, cbData, pbData);
    }

    /*
    Creates a new value. Inserts it as the first (if front is true) or last
    (if front is false) child of itParent.

    Data must be a supported type. Supported types include:

    - For boolean data: bool.
    - For UTF-8 string data: std::string_view, char*.
    - For integer data: signed and unsigned char, short, int, long, long long.
    - For float data: float, double.
    - For time data: TimeStruct, std::chrono::system_clock::time_point.
    - For UUID data: UuidStruct.
    - Any user-defined type for which JsonImplementType<T>::AddValue exists.

    We specifically do not support char because the intent is ambiguous.

    - Convert a char to __wchar_t, signed char, or unsigned char, as
    appropriate.
    - Convert a char* to wchar_t* (or use JsonPushBackMbcs).

    Requires: itParent must reference an array or an object value.
    Returns: an iterator that references the new value.
    O(1).
    */
    template<
        class T,
        class EnableIfType =
            decltype(JsonImplementType<typename std::decay<T>::type>::AddValue)>
    iterator AddValue(
        bool front,
        const_iterator const& itParent,
        std::string_view const& name,
        T const& data)
        noexcept(false) // may throw bad_alloc, length_error
    {
        return JsonImplementType<typename std::decay<T>::type>::AddValue(
            *this, front, itParent, name, data);
    }

    /*
    Creates a new value. Inserts it as the first child of itParent.

    Data must be a supported type. Supported types include:

    - For boolean data: bool.
    - For UTF-8 string data: std::string_view, char*.
    - For integer data: signed and unsigned char, short, int, long, long long.
    - For float data: float, double.
    - For time data: TimeStruct, std::chrono::system_clock::time_point.
    - For UUID data: UuidStruct.
    - Any user-defined type for which JsonImplementType<T>::AddValue exists.

    We specifically do not support char because the intent is ambiguous.

    - Convert a char to __wchar_t, signed char, or unsigned char, as
    appropriate.
    - Convert a char* to wchar_t* (or use JsonPushBackMbcs).

    Requires: itParent must reference an array or an object value.
    Returns: an iterator that references the new value.
    O(1).
    */
    template<
        class T,
        class EnableIfType =
            decltype(JsonImplementType<typename std::decay<T>::type>::AddValue)>
    iterator push_front(
        const_iterator const& itParent,
        std::string_view const& name,
        T const& data)
        noexcept(false) // may throw bad_alloc, length_error
    {
        return JsonImplementType<typename std::decay<T>::type>::AddValue(
            *this, true, itParent, name, data);
    }

    /*
    Creates a new value. Inserts it as the last child of itParent.

    Data must be a supported type. Supported types include:

    - For boolean data: bool.
    - For UTF-8 string data: std::string_view, char*.
    - For integer data: signed and unsigned char, short, int, long, long long.
    - For float data: float, double.
    - For time data: TimeStruct, std::chrono::system_clock::time_point.
    - For UUID data: UuidStruct.
    - Any user-defined type for which JsonImplementType<T>::AddValue exists.

    We specifically do not support char because the intent is ambiguous.

    - Convert a char to __wchar_t, signed char, or unsigned char, as
    appropriate.
    - Convert a char* to wchar_t* (or use JsonPushBackMbcs).

    Requires: itParent must reference an array or an object value.
    Returns: an iterator that references the new value.
    O(1).
    */
    template<
        class T,
        class EnableIfType =
            decltype(JsonImplementType<typename std::decay<T>::type>::AddValue)>
    iterator push_back(
        const_iterator const& itParent,
        std::string_view const& name,
        T const& data)
        noexcept(false) // may throw bad_alloc, length_error
    {
        return JsonImplementType<typename std::decay<T>::type>::AddValue(
            *this, false, itParent, name, data);
    }

  private:
    static void AssertNotEnd(Index) noexcept;
    static void AssertHidden(JsonType) noexcept;
    void ValidateIterator(const_iterator const&) const noexcept;
    void ValidateParentIterator(Index) const noexcept;  // Note: assumes !empty()
    bool CanIterateOver(const_iterator const&) const
        noexcept;  // False if empty() or if iterator is not a parent.
    JsonValue const& GetValue(Index) const noexcept;
    JsonValue& GetValue(Index) noexcept;
    Index FirstChild(Index) const noexcept; // Given array/object index, return
                                            // index of first child.
    Index LastChild(Index) const noexcept;  // Given array/object index, return
                                            // index of last child.
    Index NextIndex(Index) const noexcept;  // Given index, return next
                                            // non-hidden index.
    void EnsureRootExists() // If root object does not exist, create it. 
        noexcept(false); // May throw bad_alloc.

    unsigned
    FindImpl(Index parentIndex, std::string_view const& name) const noexcept;

    /*
    Adds an unlinked node to the storage vector. The caller must add the new
    node to the list. Returns the index of the new value.
    It is always ok for pbValue to be null. If so, the data will be
    uninitialized. If type is JsonArray or JsonObject, cbData must be 0.
    */
    Index CreateValue(
        std::string_view const& name,
        JsonType type,
        unsigned cbData,
        _In_reads_bytes_(cbData) void const* pbData)
        noexcept(false);  // may throw bad_alloc, length_error

    unsigned Find(Index parentIndex) const noexcept { return parentIndex; }

    template<class... NameTys>
    unsigned Find(
        Index parentIndex,
        std::string_view const& firstName,
        NameTys const&... additionalNames) const noexcept
    {
        Index childIndex = FindImpl(parentIndex, firstName);
        if (childIndex)
        {
            childIndex = Find(childIndex, additionalNames...);
        }
        return childIndex;
    }

    struct PredicateTrue
    {
        bool operator()(const_iterator const&) const noexcept { return true; }
    };

    template<class PredTy>
    void Splice(
        bool front,
        const_iterator const& itOldParent,
        const_iterator const& itNewParent,
        PredTy&& pred) noexcept
    {
        ValidateIterator(itOldParent);
        ValidateIterator(itNewParent);

        if (CanIterateOver(itOldParent))
        {
            ValidateParentIterator(itNewParent.m_index);

            auto& oldParent = GetValue(itOldParent.m_index);
            auto prevIndex = FirstChild(itOldParent.m_index);
            auto const lastIndex = oldParent.m_lastChildIndex;
            if (prevIndex != lastIndex)
            {
                // Make a linked list of the items that we're moving.
                Index headIndex = 0;
                Index* pTailIndex = &headIndex;  // pTail points at the tail's
                                                 // m_nextIndex, which is used
                                                 // to store the tail's index.

                auto pPrev = &GetValue(prevIndex);
                AssertNotEnd(prevIndex);
                AssertHidden(pPrev->m_type);
                for (;;)
                {
                    auto currentIndex = pPrev->m_nextIndex;
                    auto& current = GetValue(currentIndex);
                    AssertNotEnd(currentIndex);  // got to end() before we got
                                                 // to oldParent.LastChild

                    if (current.m_type != JsonHidden &&
                        pred(const_iterator(this, currentIndex)))
                    {
                        pPrev->m_nextIndex = current.m_nextIndex;
                        *pTailIndex = currentIndex;  // Link current into list
                                                     // of moved items.
                        pTailIndex = &current.m_nextIndex;
                        *pTailIndex = currentIndex;  // Store tail's index.

                        if (currentIndex == lastIndex)
                        {
                            oldParent.m_lastChildIndex = prevIndex;
                            break;
                        }
                    }
                    else
                    {
                        if (currentIndex == lastIndex)
                        {
                            break;
                        }

                        prevIndex = currentIndex;
                        pPrev = &current;
                    }
                }

                if (headIndex != 0)
                {
                    // Find the right place in the linked list for the moved
                    // nodes. Update the parent's lastChildIndex if necessary.
                    auto& newParent = GetValue(itNewParent.m_index);
                    if (front)
                    {
                        prevIndex = FirstChild(itNewParent.m_index);
                        if (prevIndex == newParent.m_lastChildIndex)
                        {
                            newParent.m_lastChildIndex = *pTailIndex;
                        }
                    }
                    else
                    {
                        prevIndex = newParent.m_lastChildIndex;
                        newParent.m_lastChildIndex = *pTailIndex;
                    }

                    // Insert the moved nodes into the linked list after prev.
                    pPrev = &GetValue(prevIndex);
                    *pTailIndex = pPrev->m_nextIndex;
                    pPrev->m_nextIndex = headIndex;
                }
            }
        }
    }
};

/*
Exchanges the contents of two JsonBuilder objects.
*/
void swap(JsonBuilder&, JsonBuilder&) noexcept;

/*
UUID object, network byte order (big-endian), compatible with uuid_t.
Does NOT use the same byte order as the Windows GUID type.
*/
struct UuidStruct
{
    char unsigned Data[16]; // Compatible with uuid_t from libuuid.
};

/*
DateTime, number of 100ns intervals since 1601, compatible with Win32 FILETIME.
*/
struct TimeStruct
{
private:
    using JSON_UINT32 = JsonInternal::JSON_UINT32;
    using JSON_UINT64 = JsonInternal::JSON_UINT64;

public:

    JSON_UINT32 Low;
    JSON_UINT32 High;

    static constexpr TimeStruct
    FromValue(JSON_UINT64 value) noexcept
    {
        return { static_cast<JSON_UINT32>(value), static_cast<JSON_UINT32>(value >> 32) };
    }

    constexpr JSON_UINT64
    Value() const noexcept
    {
        return (static_cast<JSON_UINT64>(High) << 32) | Low;
    }
};

// JsonImplementType

/*
The JsonImplementType class is used to implement the following methods:

- jsonBuilder.AddValue<DataType>(..., data)
- jsonBuilder.push_front<DataType>(..., data)
- jsonBuilder.push_back<DataType>(..., data)
- jsonValue.GetUnchecked<DataType>()
- jsonValue.ConvertTo<DataType>(result)

These methods will only work if a specialization of JsonImplementType<DataType>
has been defined. (More specifically, there must be a specialization of
JsonImplementType<decay_t<DataType>>.) The JsonBuilder.h header provides
specializations for core built-in types. If you want these methods to support
other types, you can define specializations of the JsonImplementType class for
them.
*/
template<class T>
class JsonImplementType
{
    // Default - no special support for T.

    /*
    Specializations of JsonImplementType<T> may implement any or all of the
    following:

    static T // return value may be "T" or "const T&" at your discretion.
    GetUnchecked(JsonValue const& value);

    static bool
    ConvertTo(JsonValue const& value, T& result);

    static JsonIterator
    AddValue(
        JsonBuilder& builder,
        bool front,
        JsonConstIterator const& itParent,
        std::string_view const& name,
        T data); // data parameter may be "T" or "const T&" at your discretion.

    The specialization may leave the method undefined if the given operation
    should not be supported. If the specialization does provide an
    implementation for a method, it should conform to the semantics of the
    corresponding methods of the JsonValue or JsonBuilder class.
    */
};

#define JSON_DECLARE_JsonImplementType(T, getRef)                         \
    template<>                                                            \
    class JsonImplementType<T>                                            \
    {                                                                     \
      public:                                                             \
        static T getRef GetUnchecked(JsonValue const& value) noexcept;    \
        static bool ConvertTo(JsonValue const& value, T& result) noexcept;\
        static JsonIterator AddValue(                                     \
            JsonBuilder& builder,                                         \
            bool front,                                                   \
            JsonConstIterator const& itParent,                            \
            std::string_view const& name,                                 \
            T const& data);                                               \
    }

#define JSON_DECLARE_JsonImplementType_AddValue(T) \
    template<>                                     \
    class JsonImplementType<T>                     \
    {                                              \
      public:                                      \
        static JsonIterator AddValue(              \
            JsonBuilder& builder,                  \
            bool front,                            \
            JsonConstIterator const& itParent,     \
            std::string_view const& name,          \
            T const& data);                        \
    }

JSON_DECLARE_JsonImplementType(bool, );

JSON_DECLARE_JsonImplementType(unsigned char, );
JSON_DECLARE_JsonImplementType(unsigned short, );
JSON_DECLARE_JsonImplementType(unsigned int, );
JSON_DECLARE_JsonImplementType(unsigned long, );
JSON_DECLARE_JsonImplementType(unsigned long long, );

JSON_DECLARE_JsonImplementType(signed char, );
JSON_DECLARE_JsonImplementType(signed short, );
JSON_DECLARE_JsonImplementType(signed int, );
JSON_DECLARE_JsonImplementType(signed long, );
JSON_DECLARE_JsonImplementType(signed long long, );

JSON_DECLARE_JsonImplementType(float, );
JSON_DECLARE_JsonImplementType(double, );

JSON_DECLARE_JsonImplementType(TimeStruct, );
JSON_DECLARE_JsonImplementType(std::chrono::system_clock::time_point, );
JSON_DECLARE_JsonImplementType(UuidStruct, const&);
JSON_DECLARE_JsonImplementType(std::string_view, );

template<>
class JsonImplementType<char*>
{
  public:
    static JsonIterator AddValue(
        JsonBuilder& builder,
        bool front,
        JsonConstIterator const& itParent,
        std::string_view const& name,
        _In_z_ char const* psz);
};

template<>
class JsonImplementType<char const*> : public JsonImplementType<char*>
{};

} // namespace jsonbuilder
