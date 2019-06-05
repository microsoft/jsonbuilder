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
- Nodes in the tree are one of two types. Simple nodes contain typed values
  (type tag plus binary data) but have no children. Complex nodes contain no
  data but may have child nodes.
- Simple nodes support arbitrary binary payloads. Any type of data can be
  stored with any type tag, and the JsonBuilder itself performs no validation
  of the stored data (though it does provide built-in support for creating
  or accessing the contents of nodes of certain built-in types). The
  JsonRenderer class and the convenience methods have built-in support for data
  stored as signed-integer (1, 2, 4, or 8 bytes), unsigned-integer (1, 2, 4, or
  8 bytes), floating-point (4 or 8 bytes), boolean (true/false), null, time
  (FILETIME), UUID (GUID), and string (utf-16). The convenience methods and the
  renderer can be extended if other types are needed.
- Complex nodes come in two types: Object and Array. The JsonBuilder itself
  makes no distinction between these two types (they have identical behavior)
  but it is intended that the Object type contain named values (i.e. it is a
  dictionary with string keys) and that the Array type contain anonymous values
  (i.e. it is a list).
- Value name limited to 16M utf-16 characters per value.
- Value data limited to 3GB per value.
- Names are stored as utf-16.
- Memory usage for object and array values is (in bytes):
  20 + sizeof(name) + padding to a multiple of 4.
- Memory usage for all other values is (in bytes):
  12 + sizeof(name) + sizeof(data) + padding to multiple of 4.
- Total storage limited to 16GB per JsonBuilder (or available VA space).

Error handling:

    Throw std::exception variants
*/

#pragma once
#include <algorithm>  // std::min
#include <chrono>
#include <cstring>
#include <inttypes.h>
#include <math.h>     // isfinite, sqrt
#include <stdexcept>  // std::invalid_argument

#include <nonstd/string_view.hpp>
#include <uuid/uuid.h>

namespace jsonbuilder {
struct UuidStruct
{
    uuid_t Data;
};

// Forward declarations

class JsonValue;
class JsonBuilder;
template<class T>
class JsonImplementType;

// Internal implementation details

namespace JsonInternal {
/*
PodVector:

Very simple vector class. For POD types only. Has the following performance
advantages over std::vector:

- This vector uses HeapReAlloc to grow its buffer. In cases where the
  HeapReAlloc can grow in-place, this avoids the overhead of allocating a
  new buffer, copying the data to the new buffer, and freeing the old
  buffer.
- This vector does not initialize data on resize unless specifically requested.

In benchmarks, using PodVector instead of VC2013's std::vector improves
JsonBuilder and JsonRenderer performance by about 10%.

In addition, using our own vector avoids creating a hard dependency on STL,
which can make it easier for customers to consume this library.
*/

class PodVectorBase
{
  protected:
    typedef unsigned size_type;

    /*
    assert(index < currentSize)
    */
    static void CheckOffset(size_type index, size_type currentSize) throw();

    /*
    assert(p1 <= p2 <= p3)
    */
    static void
    CheckRange(void const* p1, void const* p2, void const* p3) throw();

    /*
    return checked(a + b)
    */
    static size_type CheckedAdd(
        size_type a,
        size_type b);  // may throw length_error

    /*
    memcpy(pDest, pSource, cb)
    */
    static void InitData(void* pDest, void const* pSource, size_t cb) throw();

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
    Calls HeapAlloc or HeapReAlloc. If allocation fails, throw bad_alloc.
    */
    static void* Reallocate(
        void* pb,
        size_t cb,
        bool zeroInitializeMemory = false);  // may throw bad_alloc, length_error

    /*
    Calls HeapFree.
    */
    static void Deallocate(void* pb) throw();
};

template<class T>
class PodVector : private PodVectorBase
{
    static size_type const
        m_maxSize = ~size_t(0) / sizeof(T) > size_type(~size_type(1)) ?
        size_type(~size_type(1)) :
        ~size_t(0) / sizeof(T);
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

    ~PodVector() throw() { Deallocate(m_data); }

    PodVector() throw()
        : m_data(nullptr), m_size(0), m_capacity(0), m_zeroInitializeMemory(false)
    {
        return;
    }

    PodVector(PodVector&& other) throw()
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

    PodVector(PodVector const& other)  // may throw bad_alloc
        : m_data(nullptr)
        , m_size(other.m_size)
        , m_capacity(other.m_size)
        , m_zeroInitializeMemory(other.m_zeroInitializeMemory)
    {
        if (m_size != 0)
        {
            auto cb = m_size * sizeof(T);
            m_data =
                static_cast<T*>(Reallocate(nullptr, cb, m_zeroInitializeMemory));
            InitData(m_data, other.m_data, cb);
        }
    }

    PodVector(
        T const* data,
        size_type size)  // may throw bad_alloc
        : m_data(nullptr)
        , m_size(size)
        , m_capacity(size)
        , m_zeroInitializeMemory(false)
    {
        if (m_size != 0)
        {
            auto cb = m_size * sizeof(T);
            m_data =
                static_cast<T*>(Reallocate(nullptr, cb, m_zeroInitializeMemory));
            InitData(m_data, data, cb);
        }
    }

    PodVector& operator=(PodVector&& other) throw()
    {
        PodVector(static_cast<PodVector&&>(other)).swap(*this);
        return *this;
    }

    PodVector& operator=(PodVector const& other)  // may throw bad_alloc
    {
        PodVector(other).swap(*this);
        return *this;
    }

    static constexpr size_type max_size() throw() { return m_maxSize; }

    size_type size() const throw() { return m_size; }

    bool empty() const throw() { return m_size == 0; }

    size_type capacity() const throw() { return m_capacity; }

    T const* data() const throw() { return m_data; }

    T* data() throw() { return m_data; }

    T const& operator[](unsigned i) const throw()
    {
        CheckOffset(i, m_size);
        return m_data[i];
    }

    T& operator[](unsigned i) throw()
    {
        CheckOffset(i, m_size);
        return m_data[i];
    }

    void clear() { m_size = 0; }

    void push_back(T const& val)  // may throw bad_alloc, length_error
    {
        if (m_size == m_capacity)
        {
            Grow();
        }
        m_data[m_size++] = val;
    }

    void append(
        T const* pItems,
        size_type cItems)  // may throw bad_alloc, length_error
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
        T const& val)  // may throw bad_alloc, length_error
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

    void reserve(size_type minCapacity)  // may throw bad_alloc, length_error
    {
        if (m_capacity < minCapacity)
        {
            GrowTo(minCapacity);
        }
    }

    /*
    NOTE: new items are uninitialized, unless m_zeroInitializeMemory is set
    */
    void resize(size_type newSize)  // may throw bad_alloc, length_error
    {
        reserve(newSize);
        m_size = newSize;
    }

    void swap(PodVector& other) throw()
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
    T* GetAppendPointer(unsigned cItems)  // may throw bad_alloc, length_error
    {
        if (cItems > m_capacity - m_size)
        {
            GrowBy(cItems);
        }
        return m_data + m_size;
    }

    void SetEndPointer(T* pNewEnd) throw()
    {
        CheckRange(m_data, pNewEnd, m_data + m_capacity);
        m_size = static_cast<size_type>(pNewEnd - m_data);
    }

  private:
    void Grow()  // may throw bad_alloc, length_error
    {
        GrowTo(m_capacity + 1);
    }

    void GrowBy(size_type cItems)  // may throw bad_alloc, length_error
    {
        GrowTo(CheckedAdd(m_size, cItems));
    }

    void GrowTo(size_type minCapacity)  // may throw bad_alloc, length_error
    {
        size_type const newCapacity = GetNewCapacity(minCapacity, m_maxSize);
        m_data = static_cast<T*>(Reallocate(
            m_data, newCapacity * sizeof(T), m_zeroInitializeMemory));
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
enum JsonType : uint8_t
{
    // Numbering for custom types should start at 1. Custom types never have
    // children. Numbering for custom types must not exceed 200.
    JsonTypeReserved = 201,
    JsonTypeBuiltIn = 244,
    JsonUtf8,    // No children. Data = utf-8 string.
    JsonUInt,    // No children. Data = uint (1, 2, 4, or 8 bytes).
    JsonInt,     // No children. Data = int (1, 2, 4, or 8 bytes).
    JsonFloat,   // No children. Data = float (4 or 8 bytes).
    JsonBool,    // No children. Data = bool (1 or 4 bytes).
    JsonTime,    // No children. Data = uint64 (number of 100ns intervals since
                 // 1601-01-01T00:00:00Z, i.e. Win32 FILETIME).
    JsonUuid,    // No children. Data = UUID (16 bytes universally unique
                 // identifier, i.e. Win32 GUID).
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
    friend class JsonBuilder;
    friend class JsonValue;
    typedef uint32_t Index;

    Index m_nextIndex;  // The index of the "next" node. (Nodes form a
                        // singly-linked list).
    uint32_t m_cchName : 24;
    JsonType m_type : 8;
};

/*
Each value in a JsonBuilder is represented as a JsonValue. Each JsonValue
stores:

- Type - a value from the JsonType enumeration, or a custom type.
- Name - utf-16 string, up to 16M characters.
- Data - binary blob, up to 3GB.

Note that Object, Array, and hidden values do not contain data.
- It is an error to use a non-zero value for cbData when creating an
array/object.
- It is an error to call the Data() method on an array/object/hidden value.
*/
class JsonValue : private JsonValueBase
{
    friend class JsonBuilder;
    typedef unsigned StoragePod;

    union
    {
        uint32_t m_cbData;       // Normal node only (not present for sentinel,
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
    JsonType Type() const throw();

    /*
    Gets the name of the value.
    */
    nonstd::string_view Name() const throw();

    /*
    Gets the size of the data of the value, in bytes.
    Note that hidden, object, and array values do not have data, and it is an
    error to call DataSize() on a value where Type() is hidden, object, or
    array.
    */
    unsigned DataSize() const throw();

    /*
    Reduces the size recorded for the data.
    Does not change the size of the underlying buffer.
    Requires: cbNew <= DataSize().
    */
    void ReduceDataSize(unsigned cbNew) throw();

    /*
    Gets a pointer to the data of the value.
    Note that hidden, object, and array values do not have data, and it is an
    error to call Data() on a value where Type() is hidden, object, or array.
    */
    void const* Data(unsigned* pcbData = nullptr) const throw();

    /*
    Gets a pointer to the data of the value. The data can be modified, though
    the length can only be reduced, never increased.
    Note that hidden, object, and array values do not have data, and it is an
    error to call Data() on a value where Type() is hidden, object, or array.
    */
    void* Data(unsigned* pcbData = nullptr) throw();

    /*
    Returns true if Type==Null.
    */
    bool IsNull() const throw();

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
    - For string data: utl::string_view.
    - For integer data: signed and unsigned char, short, int, long, long long.
    - For float data: float, double, long double.
    - Any user-defined type for which JsonImplementType<T>::GetUnchecked exists.

    Detailed semantics:
    assert(this->Type() is an exact match for typeof(T));
    If DataSize() is a legal size for T, return the stored data as a T;
    Else assert(false), return a default value of T (e.g. 0);
    */
    template<class T>
    auto GetUnchecked() const throw()
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
    - For string data: utl::string_view.
    - For integer data: signed and unsigned char, short, int, long, long long.
    - For float data: float, double, long double.
    - Any user-defined type for which JsonImplementType<T>::ConvertTo exists.
    */
    template<
        class T,
        class ConvertToDoesNotSupportThisType =
            decltype(JsonImplementType<typename std::decay<T>::type>::ConvertTo(
                *(JsonValue const*) 0,
                *(T*) 0))>
    bool ConvertTo(T& result) const throw()
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
    typedef uint32_t Index;

    JsonBuilder const* m_pContainer;
    Index m_index;

    JsonConstIterator(JsonBuilder const* pContainer, Index index) throw();

  public:
    typedef std::forward_iterator_tag iterator_category;
    typedef JsonValue value_type;
    typedef std::ptrdiff_t difference_type;
    typedef JsonValue const* pointer;
    typedef JsonValue const& reference;

    JsonConstIterator() throw();

    bool operator==(JsonConstIterator const&) const throw();
    bool operator!=(JsonConstIterator const&) const throw();

    reference operator*() const throw();
    pointer operator->() const throw();

    JsonConstIterator& operator++() throw();
    JsonConstIterator operator++(int) throw();

    /*
    For iterating over this value's children.
    iterator.begin() is equivalent to builder.begin(iterator).
    O(1), unless hidden nodes need to be skipped.
    */
    JsonConstIterator begin() const throw();

    /*
    For iterating over this value's children.
    iterator.end() is equivalent to builder.end(iterator).
    O(1), unless hidden nodes need to be skipped.
    */
    JsonConstIterator end() const throw();

    /*
    Returns true if this iterator refers to the root object of a JsonBuilder.
    Note that it is an error to dereference or ++ an iterator that references
    the root object (e.g. it is an error to do jsonBuilder.end()++).
    */
    bool IsRoot() const throw();
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
    explicit JsonIterator(JsonConstIterator const&) throw();

  public:
    typedef JsonValue& reference;
    typedef JsonValue* pointer;

    JsonIterator() throw();

    reference operator*() const throw();
    pointer operator->() const throw();

    JsonIterator& operator++() throw();
    JsonIterator operator++(int) throw();

    /*
    For iterating  over this value's children.
    iterator.begin() is equivalent to builder.begin(iterator).
    O(1), unless hidden nodes need to be skipped.
    */
    JsonIterator begin() const throw();

    /*
    For iterating  over this value's children.
    iterator.end() is equivalent to builder.end(iterator).
    O(1), unless hidden nodes need to be skipped.
    */
    JsonIterator end() const throw();
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
            JsonValue::StoragePod const* pStorage,
            size_type cStorage);  // may throw bad_alloc

        void Validate();  // may throw invalid_argument

      private:
        void ValidateRecurse(Index index);

        void
        UpdateMap(Index index, ValidationState expectedVal, ValidationState newVal);
    };

  public:
    typedef JsonValue value_type;
    typedef JsonValue* pointer;
    typedef JsonValue const* const_pointer;
    typedef size_t size_type;
    typedef int difference_type;
    typedef JsonIterator iterator;
    typedef JsonConstIterator const_iterator;

    /*
    Initializes a new instance of the JsonBuilder class.
    */
    JsonBuilder() throw();

    /*
    Initializes a new instance of the JsonBuilder class using at least the
    specified initial capacity (in bytes).
    */
    explicit JsonBuilder(size_type cbInitialCapacity);  // may throw bad_alloc,
                                                        // length_error

    /*
    Initializes a new instance of the JsonBuilder class, copying its data from
    other.
    */
    JsonBuilder(JsonBuilder const& other);  // may throw bad_alloc

    /*
    Initializes a new instance of the JsonBuilder class, moving the data from
    other.
    NOTE: Invalidates all iterators pointing into other.
    */
    JsonBuilder(JsonBuilder&& other) throw();

    /*
    Initializes a new instance of the JsonBuilder class, copying its data from
    a memory buffer. Optionally runs ValidateData (i.e. for untrusted input).
    */
    JsonBuilder(
        void const* pbRawData,
        size_type cbRawData,
        bool validateData = true);  // may throw bad_alloc, length_error,
                                    // invalid_argument

    /*
    Copies data from other.
    */
    JsonBuilder& operator=(JsonBuilder const& other);  // may throw bad_alloc

    /*
    Moves the data from other.
    NOTE: Invalidates all iterators pointing into other.
    */
    JsonBuilder& operator=(JsonBuilder&& other) throw();

    /*
    Throws an exception if the data in this JsonBuilder is corrupt.
    Mainly for use in debugging, but this can also be used when feeding
    untrusted data to JsonBuilder.
    */
    void ValidateData() const;  // May throw bad_alloc, invalid_argument.

    iterator begin() throw();
    const_iterator begin() const throw();
    const_iterator cbegin() const throw();

    iterator end() throw();
    const_iterator end() const throw();
    const_iterator cend() const throw();

    iterator root() throw();
    const_iterator root() const throw();
    const_iterator croot() const throw();

    /*
    Returns a pointer to the first element in the backing raw data vector.
    */
    void const* buffer_data() const throw();

    /*
    Returns the size (in bytes) of the memory currently being used by this
    JsonBuilder (i.e. vector.size() of the underlying storage vector). This
    value is primarily intended to help in determining the appropriate value
    for the initialCapacity constructor parameter. This is also the size of
    the data returned by buffer_data().
    */
    size_type buffer_size() const throw();

    /*
    Returns the size (in bytes) of the memory currently allocated by this
    JsonBuilder (i.e. vector.capacity() of the underlying storage vector).
    This value is primarily intended to help in determining the appropriate
    value for the initialCapacity constructor parameter.
    */
    size_type buffer_capacity() const throw();

    /*
    Returns the maximum size (in bytes) of the memory that could be passed
    to buffer_reserve or returned from buffer_size.
    On 32-bit systems, this is currently slightly less than 4GB.
    On 64-bit systems, this is currently slightly less than 16GB.
    */
    static constexpr size_type buffer_max_size() throw()
    {
        return StorageVec::max_size() * sizeof(JsonValue::StoragePod);
    }

    /*
    If less than the specified amount of memory is currently allocated by this
    JsonBuilder, allocates additional memory so that at least the specified
    amount is allocated.
    */
    void buffer_reserve(size_type cbMinimumCapacity);  // may throw bad_alloc,
                                                       // length_error

    /*
    Removes all data from this JsonBuilder and prepares it for reuse.
    Keeps the currently-allocated buffer.
    O(1).
    */
    void clear() throw();

    /*
    Marks the specified value as Erased. Equivalent to erase(itValue,
    itValue+1). Requires: itValue+1 is valid (i.e. requires that itValue !=
    end()). Returns: itValue+1. Implementation detail: Erased values have their
    Type() changed to Hidden, and will be skipped during iteration, but they
    continue to take up space in the tree. O(1), unless there are erased values
    that have to be skipped to find itValue+1.
    */
    iterator erase(const_iterator itValue) throw();

    /*
    Marks the specified range as Erased.
    Requires: itBegin <= itEnd (i.e. repeated itBegin++ will reach itEnd).
    Returns: itEnd.
    Implementation detail: Erased values have their Type() changed to Hidden,
    and will be skipped during iteration, but they continue to take up space
    in the tree.
    O(n), where n = the number of values in the range itBegin..itEnd.
    */
    iterator erase(const_iterator itBegin, const_iterator itEnd) throw();

    /*
    Replaces the contents of this with the contents of other.
    NOTE: Invalidates all iterators pointing into this and other.
    O(1).
    */
    void swap(JsonBuilder& other) throw();

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
    iterator find(
        nonstd::string_view const& firstName,
        NameTys const&... additionalNames) throw()
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
    find(nonstd::string_view const& firstName, NameTys const&... additionalNames) const
        throw()
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
        nonstd::string_view const& firstName,
        NameTys const&... additionalNames) throw()
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
        nonstd::string_view const& firstName,
        NameTys const&... additionalNames) const throw()
    {
        ValidateIterator(itParent);
        return const_iterator(
            this, Find(itParent.m_index, firstName, additionalNames...));
    }

    /*
    Returns the number of children of itParent.
    O(n), where n is the number of children of itParent.
    */
    unsigned count(const_iterator const& itParent) const throw();

    /*
    Returns the first child of itParent.
    If itParent has no children, returns end(itParent).
    O(1), unless there are erased values that have to be skipped.
    */
    iterator begin(const_iterator const& itParent) throw();

    /*
    Returns the first child of itParent.
    If itParent has no children, returns end(itParent).
    O(1), unless there are erased values that have to be skipped.
    */
    const_iterator begin(const_iterator const& itParent) const throw();

    /*
    Returns the first child of itParent.
    If itParent has no children, returns cend(itParent).
    O(1), unless there are erased values that have to be skipped.
    */
    const_iterator cbegin(const_iterator const& itParent) const throw();

    /*
    Returns the iterator after the last child of itParent.
    O(1), unless there are erased values that have to be skipped.
    */
    iterator end(const_iterator const& itParent) throw();

    /*
    Returns the iterator after the last child of itParent.
    O(1), unless there are erased values that have to be skipped.
    */
    const_iterator end(const_iterator const& itParent) const throw();

    /*
    Returns the iterator after the last child of itParent.
    O(1), unless there are erased value that have to be skipped.
    */
    const_iterator cend(const_iterator const& itParent) const throw();

    /*
    Removes all children from itOldParent.
    Re-inserts them as the first children of itNewParent.
    Requires: itNewParent must reference an array or an object value.
    O(n), where n is the number of children of itOldParent.
    */
    void splice_front(
        const_iterator const& itOldParent,
        const_iterator const& itNewParent) throw()
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
        const_iterator const& itNewParent) throw()
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
        PredTy&& pred) throw()
    {
        Splice(true, itOldParent, itNewParent, std::forward<PredTy&&>(pred));
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
        PredTy&& pred) throw()
    {
        Splice(false, itOldParent, itNewParent, std::forward<PredTy&&>(pred));
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
        nonstd::string_view const& name,
        JsonType type,
        unsigned cbData = 0,
        void const* pbData = nullptr);  // may throw bad_alloc, length_error

    /*
    Creates a new value. Inserts it as the first child of itParent.
    If pbData is null, the new value's payload is uninitialized.
    Requires: itParent must reference an array or an object value.
    Requires: if type is Array or Object, cbValue must be 0 and pbValue must be
    null. Returns: an iterator that references the new value. O(1).
    */
    iterator push_front(
        const_iterator const& itParent,
        nonstd::string_view const& name,
        JsonType type,
        unsigned cbData = 0,
        void const* pbData = nullptr)  // may throw bad_alloc, length_error
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
        nonstd::string_view const& name,
        JsonType type,
        unsigned cbData = 0,
        void const* pbData = nullptr)  // may throw bad_alloc, length_error
    {
        return AddValue(false, itParent, name, type, cbData, pbData);
    }

    /*
    Creates a new value. Inserts it as the first (if front is true) or last
    (if front is false) child of itParent.

    Data must be a supported type. Supported types include:

    - For boolean data: bool.
    - For string data: utl::string_view, wchar_t*, __wchar_t.
    - For integer data: signed and unsigned char, short, int, long, long long.
    - For float data: float, double, long double.
    - For time data: FILETIME.
    - For UUID data: GUID.
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
        nonstd::string_view const& name,
        T const& data)  // may throw bad_alloc, length_error
    {
        return JsonImplementType<typename std::decay<T>::type>::AddValue(
            *this, front, itParent, name, data);
    }

    /*
    Creates a new value. Inserts it as the first child of itParent.

    Data must be a supported type. Supported types include:

    - For boolean data: bool.
    - For string data: utl::string_view, wchar_t*, __wchar_t.
    - For integer data: signed and unsigned char, short, int, long, long long.
    - For float data: float, double, long double.
    - For time data: FILETIME.
    - For UUID data: GUID.
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
        nonstd::string_view const& name,
        T const& data)  // may throw bad_alloc, length_error
    {
        return JsonImplementType<typename std::decay<T>::type>::AddValue(
            *this, true, itParent, name, data);
    }

    /*
    Creates a new value. Inserts it as the last child of itParent.

    Data must be a supported type. Supported types include:

    - For boolean data: bool.
    - For string data: utl::string_view, wchar_t*, __wchar_t.
    - For integer data: signed and unsigned char, short, int, long, long long.
    - For float data: float, double, long double.
    - For time data: FILETIME.
    - For UUID data: GUID.
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
        nonstd::string_view const& name,
        T const& data)  // may throw bad_alloc, length_error
    {
        return JsonImplementType<typename std::decay<T>::type>::AddValue(
            *this, false, itParent, name, data);
    }

  private:
    static void AssertNotEnd(Index) throw();
    static void AssertHidden(JsonType) throw();
    void ValidateIterator(const_iterator const&) const throw();
    void ValidateParentIterator(Index) const throw();  // Note: assumes !empty()
    bool CanIterateOver(const_iterator const&) const
        throw();  // False if empty() or if iterator is not a parent.
    JsonValue const& GetValue(Index) const throw();
    JsonValue& GetValue(Index) throw();
    Index FirstChild(Index) const throw();  // Given array/object index, return
                                            // index of first child.
    Index LastChild(Index) const throw();   // Given array/object index, return
                                            // index of last child.
    Index NextIndex(Index) const throw();   // Given index, return next
                                            // non-hidden index.
    void EnsureRootExists();  // If root object does not exist, create it. May
                              // throw bad_alloc.

    unsigned
    FindImpl(Index parentIndex, nonstd::string_view const& name) const throw();

    /*
    Adds an unlinked node to the storage vector. The caller must add the new
    node to the list. Returns the index of the new value.
    It is always ok for pbValue to be null. If so, the data will be
    uninitialized. If type is JsonArray or JsonObject, cbData must be 0.
    */
    Index CreateValue(
        nonstd::string_view const& name,
        JsonType type,
        unsigned cbData,
        void const* pbData);  // may throw bad_alloc, length_error

    unsigned Find(Index parentIndex) const throw() { return parentIndex; }

    template<class... NameTys>
    unsigned Find(
        Index parentIndex,
        nonstd::string_view const& firstName,
        NameTys const&... additionalNames) const throw()
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
        bool operator()(const_iterator const&) const throw() { return true; }
    };

    template<class PredTy>
    void Splice(
        bool front,
        const_iterator const& itOldParent,
        const_iterator const& itNewParent,
        PredTy&& pred) throw()
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
void swap(JsonBuilder&, JsonBuilder&) throw();

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

    static T GetUnchecked( // return value may be "T" or "const T&" at your
    discretion. JsonValue const& value);

    static bool ConvertTo(
        JsonValue const& value,
        T& result);

    static JsonIterator AddValue(
        JsonBuilder& builder,
        bool front,
        JsonConstIterator const& itParent,
        utl::string_view const& name,
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
        static T getRef GetUnchecked(JsonValue const& value) throw();     \
        static bool ConvertTo(JsonValue const& value, T& result) throw(); \
        static JsonIterator AddValue(                                     \
            JsonBuilder& builder,                                         \
            bool front,                                                   \
            JsonConstIterator const& itParent,                            \
            nonstd::string_view const& name,                              \
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
            nonstd::string_view const& name,       \
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

JSON_DECLARE_JsonImplementType(std::chrono::system_clock::time_point, );
JSON_DECLARE_JsonImplementType(UuidStruct, );
JSON_DECLARE_JsonImplementType(nonstd::string_view, );
JSON_DECLARE_JsonImplementType_AddValue(std::string);
JSON_DECLARE_JsonImplementType_AddValue(char);

template<>
class JsonImplementType<char*>
{
  public:
    static JsonIterator AddValue(
        JsonBuilder& builder,
        bool front,
        JsonConstIterator const& itParent,
        nonstd::string_view const& name,
        char const* psz);
};

template<>
class JsonImplementType<char const*> : public JsonImplementType<char*>
{};

}  // namespace jsonbuilder
