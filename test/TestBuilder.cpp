// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <catch2/catch.hpp>
#include <jsonbuilder/JsonBuilder.h>
#include <string.h>

#ifdef _WIN32
using uuid_t = char unsigned[16];
static void uuid_generate(uuid_t uuid)
{
    for (unsigned i = 0; i != sizeof(uuid_t); i++)
    {
        uuid[i] = static_cast<char unsigned>(rand());
    }
}
#else
#include <uuid/uuid.h>
#endif

using namespace jsonbuilder;

auto constexpr TicksPerSecond = 10'000'000u;
auto constexpr FileTime1970 = 116444736000000000;
using ticks = std::chrono::duration<std::int64_t, std::ratio<1, TicksPerSecond>>;

TEST_CASE("JsonBuilder buffer reserve", "[builder]")
{
    constexpr auto c_maxSize = JsonBuilder::buffer_max_size();

    JsonBuilder b;

    SECTION("Buffer begins empty")
    {
        REQUIRE(b.buffer_size() == 0);
        REQUIRE(b.buffer_capacity() == 0);
    }

    SECTION("Buffer reserving 0 remains empty")
    {
        b.buffer_reserve(0);
        REQUIRE(b.buffer_size() == 0);
        REQUIRE(b.buffer_capacity() == 0);
    }

    SECTION("Buffer reserving 1 keeps size 0 and makes a capacity of at least "
            "the size of one element")
    {
        b.buffer_reserve(1);
        REQUIRE(b.buffer_size() == 0);
        REQUIRE(b.buffer_capacity() >= 4);
    }

    SECTION("Buffer reserving 2 keeps size 0 and makes a capacity of at least "
            "the size of one element")
    {
        b.buffer_reserve(2);
        REQUIRE(b.buffer_size() == 0);
        REQUIRE(b.buffer_capacity() >= 4);
    }

    SECTION("Buffer reserving 5 keeps size 0 and makes a capacity of at least "
            "the size of two elements")
    {
        b.buffer_reserve(5);
        REQUIRE(b.buffer_size() == 0);
        REQUIRE(b.buffer_capacity() >= 8);
    }

    SECTION("Buffer reserving max succeeds or throws std::bad_alloc")
    {
        auto reserveMax = [&]() {
            try
            {
                b.buffer_reserve(c_maxSize);
            }
            catch (const std::bad_alloc&)
            {}
        };

        REQUIRE_NOTHROW(reserveMax());
    }

    SECTION("Buffer reserving one more than max throws std::length_error")
    {
        REQUIRE_THROWS_AS(b.buffer_reserve(c_maxSize + 1), std::length_error);
    }

    SECTION("Buffer reserving maximum of size_type throws std::length_error")
    {
        auto maxSizeType = ~JsonBuilder::size_type(0);
        REQUIRE_THROWS_AS(b.buffer_reserve(maxSizeType), std::length_error);
    }
}

template<class InputType, class OutputType>
static void TestInputOutputScalar()
{
    using InputLimits = std::numeric_limits<InputType>;
    using OutputLimits = std::numeric_limits<OutputType>;

    JsonBuilder b;

#define USTRING(prefix) prefix ## "\u0024\u00A3\u0418\u0939\u20AC\uD55C\U00010348"

    b.push_back(b.root(), USTRING(u), InputLimits::lowest());
    b.push_back(b.root(), USTRING(U), InputLimits::min());
    b.push_back(b.root(), USTRING(L), InputLimits::max());
    b.push_back(b.root(), USTRING(u8), OutputLimits::lowest());
    b.push_back(b.root(), "", OutputLimits::min());
    b.push_back(b.root(), "", OutputLimits::max());

    REQUIRE_NOTHROW(b.ValidateData());

    InputType i;

    auto it = b.begin();
    REQUIRE(it->Name() == USTRING(u8));
    REQUIRE(it->GetUnchecked<InputType>() == InputLimits::lowest());
    REQUIRE(it->ConvertTo(i));
    REQUIRE(i == InputLimits::lowest());

    ++it;
    REQUIRE(it->Name() == USTRING(u8));
    REQUIRE(it->GetUnchecked<InputType>() == InputLimits::min());
    REQUIRE(it->ConvertTo(i));
    REQUIRE(i == InputLimits::min());

    ++it;
    REQUIRE(it->Name() == USTRING(u8));
    REQUIRE(it->GetUnchecked<InputType>() == InputLimits::max());
    REQUIRE(it->ConvertTo(i));
    REQUIRE(i == InputLimits::max());

    ++it;
    REQUIRE(it->Name() == USTRING(u8));
    REQUIRE(
        it->GetUnchecked<InputType>() ==
        static_cast<InputType>(OutputLimits::lowest()));
    if (it->ConvertTo(i))
    {
        REQUIRE(i == static_cast<InputType>(OutputLimits::lowest()));
    }
    else
    {
        REQUIRE(i == 0);
        REQUIRE(it->GetUnchecked<InputType>() != OutputLimits::lowest());
    }

    ++it;
    REQUIRE(
        it->GetUnchecked<InputType>() ==
        static_cast<InputType>(OutputLimits::min()));
    if (it->ConvertTo(i))
    {
        REQUIRE(i == static_cast<InputType>(OutputLimits::min()));
    }
    else
    {
        REQUIRE(i == 0);
        REQUIRE(it->GetUnchecked<InputType>() != OutputLimits::min());
    }

    ++it;
    REQUIRE(
        it->GetUnchecked<InputType>() ==
        static_cast<InputType>(OutputLimits::max()));
    if (it->ConvertTo(i))
    {
        REQUIRE(i == static_cast<InputType>(OutputLimits::max()));
    }
    else
    {
        REQUIRE(i == 0);
        REQUIRE(it->GetUnchecked<InputType>() != OutputLimits::max());
    }
}

TEST_CASE("JsonBuilder numeric limits", "[builder]")
{
    SECTION("signed char") { TestInputOutputScalar<signed char, int64_t>(); }
    SECTION("signed short") { TestInputOutputScalar<signed short, int64_t>(); }
    SECTION("signed int") { TestInputOutputScalar<signed int, int64_t>(); }
    SECTION("signed long") { TestInputOutputScalar<signed long, int64_t>(); }
    SECTION("signed long long")
    {
        TestInputOutputScalar<signed long long, int64_t>();
    }

    SECTION("unsigned char")
    {
        TestInputOutputScalar<unsigned char, uint64_t>();
    }
    SECTION("unsigned short")
    {
        TestInputOutputScalar<unsigned short, uint64_t>();
    }
    SECTION("unsigned int") { TestInputOutputScalar<unsigned int, uint64_t>(); }
    SECTION("unsigned long")
    {
        TestInputOutputScalar<unsigned long, uint64_t>();
    }
    SECTION("unsigned long long")
    {
        TestInputOutputScalar<unsigned long long, uint64_t>();
    }

    SECTION("float") { TestInputOutputScalar<float, double>(); }
    SECTION("double") { TestInputOutputScalar<double, double>(); }
}

TEST_CASE("JsonBuilder internal name realloc")
{
    JsonBuilder b;
    std::string_view constexpr expectedName = "expectedName";
    std::string_view actualName = expectedName;
    std::string_view constexpr expectedVal = "expectedVal";
    std::string_view actualVal = expectedVal;

    auto it = b.push_back(b.root(), actualName, actualVal);
    actualName = it->Name();
    actualVal = it->GetUnchecked<std::string_view>();

    // Verify that the correct name and value get added even if they point into
    // the buffer and the buffer is reallocated.
    auto const cap = b.buffer_capacity();
    while (cap == b.buffer_capacity())
    {
        it = b.push_back(b.root(), actualName, actualVal);

        actualName = it->Name();
        REQUIRE(actualName.data() != expectedName.data());
        REQUIRE(actualName == expectedName);

        actualVal = it->GetUnchecked<std::string_view>();
        REQUIRE(actualVal.data() != expectedVal.data());
        REQUIRE(actualVal == expectedVal);
    }
}

TEST_CASE("JsonBuilder string push_back")
{
    JsonBuilder b;

    SECTION("push_back std::string_view")
    {
        auto itr = b.push_back(b.root(), "", std::string_view{ "ABCDE" });
        REQUIRE(itr->GetUnchecked<std::string_view>() == "ABCDE");
    }

    SECTION("push_back std::string")
    {
        auto itr = b.push_back<std::string_view>(b.root(), "", std::string{ "ABCDE" });
        REQUIRE(itr->GetUnchecked<std::string_view>() == "ABCDE");
    }

    SECTION("push_back char*")
    {
        auto itr = b.push_back(b.root(), "", const_cast<char*>("ABC"));
        REQUIRE(itr->GetUnchecked<std::string_view>() == "ABC");
    }

    SECTION("push_back const char*")
    {
        auto itr = b.push_back(b.root(), "", static_cast<const char*>("DEF"));
        REQUIRE(itr->GetUnchecked<std::string_view>() == "DEF");
    }

    SECTION("push_back const char[]")
    {
        auto itr = b.push_back(b.root(), "", "HIJ");
        REQUIRE(itr->GetUnchecked<std::string_view>() == "HIJ");
    }
}

TEST_CASE("JsonBuilder chrono push_back", "[builder]")
{
    auto now = std::chrono::system_clock::now();
    auto nowTicks = std::chrono::time_point_cast<ticks>(now);

    JsonBuilder b;

    // Make sure a system time can round-trip (with loss of precision)
    auto itr = b.push_back(b.root(), "CurrentTime", now);
    auto retrieved = itr->GetUnchecked<std::chrono::system_clock::time_point>();
    REQUIRE(retrieved == nowTicks);
}

TEST_CASE("JsonBuilder chrono-to-TimeStruct push_back", "[builder]")
{
    JsonBuilder b;

    // Make sure a system time converts to the correct file time
    auto itr = b.push_back(b.root(), "+2", std::chrono::system_clock::from_time_t(2));
    auto retrieved = itr->GetUnchecked<TimeStruct>();
    auto const expected = FileTime1970 + 2 * TicksPerSecond;
    REQUIRE(retrieved.Value() == expected);
}

TEST_CASE("JsonBuilder TimeStruct-to-chrono push_back", "[builder]")
{
    JsonBuilder b;

    // Make sure a system time converts to the correct file time
    auto itr = b.push_back(b.root(), "+2", TimeStruct::FromValue(FileTime1970 + 2 * TicksPerSecond));
    auto retrieved = itr->GetUnchecked<std::chrono::system_clock::time_point>();
    REQUIRE(retrieved == std::chrono::system_clock::from_time_t(2));
}

TEST_CASE("JsonBuilder uuid push_back", "[builder]")
{
    UuidStruct uuid;
    uuid_generate(uuid.Data);

    JsonBuilder b;
    auto itr = b.push_back(b.root(), "Uuid", uuid);

    auto retrieved = itr->GetUnchecked<UuidStruct>();
    REQUIRE(memcmp(retrieved.Data, uuid.Data, sizeof(uuid.Data)) == 0);
}

TEST_CASE("JsonBuilder find", "[builder]")
{
    JsonBuilder b;

    // Empty object
    REQUIRE(b.find("a1") == b.end());
    REQUIRE(b.find("a1", "a2") == b.end());

    // Single object (a1)
    auto itA1 = b.push_back(b.root(), "a1", JsonObject);
    REQUIRE(b.find("a1") == itA1);
    REQUIRE(b.find(b.root(), "a1") == itA1);
    REQUIRE(b.find("b1") == b.end());
    REQUIRE(b.find(b.root(), "b1") == b.end());
    REQUIRE(b.find("a1", "a2") == b.end());

    // Second object b2, sibling of a1
    auto itB1 = b.push_back(b.root(), "b1", JsonObject);
    REQUIRE(b.find("a1") == itA1);
    REQUIRE(b.find("a1", "a2") == b.end());
    REQUIRE(b.find("b1") == itB1);
    REQUIRE(b.find("c1") == b.end());

    // First child of a1, a2
    auto itA1A2 = b.push_back(itA1, "a2", JsonObject);
    REQUIRE(b.find("a1") == itA1);
    REQUIRE(b.find("a1", "a2") == itA1A2);
    REQUIRE(b.find(b.root(), "a1", "a2") == itA1A2);
    REQUIRE(b.find("a1", "a2", "a3") == b.end());
    REQUIRE(b.find("b1") == itB1);
    REQUIRE(b.find("c1") == b.end());

    // First child of a2, a3
    auto itA1A2A3 = b.push_back(itA1A2, "a3", 0);
    REQUIRE(b.find("a1", "a2", "a3") == itA1A2A3);
    REQUIRE(b.find(itA1, "a2") == itA1A2);
    REQUIRE(b.find(itB1, "a2") == b.end());

    REQUIRE_NOTHROW(b.ValidateData());
}

TEST_CASE("JsonBuilder constructors", "[builder]")
{
    JsonBuilder b;
    b.push_back(b.root(), "aname", "ava");
    b.push_back(b.root(), "bname", "bva");
    REQUIRE_NOTHROW(b.ValidateData());

    SECTION("Copy constructor")
    {
        JsonBuilder copy{ b };
        REQUIRE_NOTHROW(copy.ValidateData());

        auto it = copy.begin();
        REQUIRE(it->Name() == "aname");
        REQUIRE(it->GetUnchecked<std::string_view>() == "ava");

        ++it;
        REQUIRE(it->Name() == "bname");
        REQUIRE(it->GetUnchecked<std::string_view>() == "bva");
    }

    SECTION("Move constructor")
    {
        JsonBuilder move{ std::move(b) };
        REQUIRE_NOTHROW(move.ValidateData());

        auto it = move.begin();
        REQUIRE(it->Name() == "aname");
        REQUIRE(it->GetUnchecked<std::string_view>() == "ava");

        ++it;
        REQUIRE(it->Name() == "bname");
        REQUIRE(it->GetUnchecked<std::string_view>() == "bva");
    }
}

TEST_CASE("JsonBuilder erase", "[builder]")
{
    JsonBuilder b;
    b.push_back(b.root(), "aname", "ava");
    b.push_back(b.root(), "bname", "bva");
    REQUIRE_NOTHROW(b.ValidateData());

    SECTION("erase a single child element")
    {
        auto itr = b.erase(b.begin());
        REQUIRE_NOTHROW(b.ValidateData());
        REQUIRE(itr == b.begin());
        REQUIRE(b.count(b.root()) == 1);
    }

    SECTION("erase all children")
    {
        auto itr = b.erase(b.begin(), b.end());
        REQUIRE_NOTHROW(b.ValidateData());
        REQUIRE(itr == b.end());
        REQUIRE(b.begin() == b.end());
        REQUIRE(b.count(itr) == 0);
    }
}

TEST_CASE("JsonBuilder conversions", "[builder]")
{
    int64_t ival;
    uint64_t uval;
    double fval;
    std::string_view sval;
    bool bval;
    std::chrono::system_clock::time_point tval;
    UuidStruct uuidval;

    JsonBuilder b;

    SECTION("JsonNull")
    {
        auto itr = b.push_back(b.root(), "FirstItem", JsonNull);

        REQUIRE(itr->IsNull());
        REQUIRE(!itr->ConvertTo(bval));
        REQUIRE(!itr->ConvertTo(fval));
        REQUIRE(!itr->ConvertTo(ival));
        REQUIRE(!itr->ConvertTo(uval));
        REQUIRE(!itr->ConvertTo(sval));
        REQUIRE(!itr->ConvertTo(tval));
        REQUIRE(!itr->ConvertTo(uuidval));
    }

    SECTION("false")
    {
        auto itr = b.push_back(b.root(), "", false);

        REQUIRE(itr->GetUnchecked<bool>() == false);
        REQUIRE((itr->ConvertTo(bval) && !bval));
        REQUIRE(!itr->ConvertTo(fval));
        REQUIRE(!itr->ConvertTo(ival));
        REQUIRE(!itr->ConvertTo(uval));
        REQUIRE(!itr->ConvertTo(sval));
        REQUIRE(!itr->ConvertTo(tval));
        REQUIRE(!itr->ConvertTo(uuidval));
    }

    SECTION("true")
    {
        auto itr = b.push_back(b.root(), "", true);

        REQUIRE(itr->GetUnchecked<bool>() == true);
        REQUIRE((itr->ConvertTo(bval) && bval));
        REQUIRE(!itr->ConvertTo(fval));
        REQUIRE(!itr->ConvertTo(ival));
        REQUIRE(!itr->ConvertTo(uval));
        REQUIRE(!itr->ConvertTo(sval));
        REQUIRE(!itr->ConvertTo(tval));
        REQUIRE(!itr->ConvertTo(uuidval));
    }

    SECTION("int64_t")
    {
        auto itr = b.push_back(b.root(), "", 123);

        REQUIRE(itr->GetUnchecked<int64_t>() == 123);
        REQUIRE(!itr->ConvertTo(bval));
        REQUIRE((itr->ConvertTo(fval) && fval == 123));
        REQUIRE((itr->ConvertTo(ival) && ival == 123));
        REQUIRE((itr->ConvertTo(uval) && uval == 123));
        REQUIRE(!itr->ConvertTo(sval));
        REQUIRE(!itr->ConvertTo(tval));
        REQUIRE(!itr->ConvertTo(uuidval));
    }

    SECTION("uint64_t")
    {
        auto itr = b.push_back(b.root(), "", 123u);

        REQUIRE(itr->GetUnchecked<uint64_t>() == 123u);
        REQUIRE(!itr->ConvertTo(bval));
        REQUIRE((itr->ConvertTo(fval) && fval == 123));
        REQUIRE((itr->ConvertTo(ival) && ival == 123));
        REQUIRE((itr->ConvertTo(uval) && uval == 123));
        REQUIRE(!itr->ConvertTo(sval));
        REQUIRE(!itr->ConvertTo(tval));
        REQUIRE(!itr->ConvertTo(uuidval));
    }

    SECTION("double")
    {
        auto itr = b.push_back(b.root(), "", 123.0);

        REQUIRE(itr->GetUnchecked<double>() == 123.0);
        REQUIRE(!itr->ConvertTo(bval));
        REQUIRE((itr->ConvertTo(fval) && fval == 123));
        REQUIRE((itr->ConvertTo(ival) && ival == 123));
        REQUIRE((itr->ConvertTo(uval) && uval == 123));
        REQUIRE(!itr->ConvertTo(sval));
        REQUIRE(!itr->ConvertTo(tval));
        REQUIRE(!itr->ConvertTo(uuidval));
    }

    SECTION("string")
    {
        auto itr = b.push_back(b.root(), "", "ABC");

        REQUIRE(itr->GetUnchecked<std::string_view>() == "ABC");
        REQUIRE(!itr->ConvertTo(bval));
        REQUIRE(!itr->ConvertTo(fval));
        REQUIRE(!itr->ConvertTo(ival));
        REQUIRE(!itr->ConvertTo(uval));
        REQUIRE((itr->ConvertTo(sval) && sval == "ABC"));
        REQUIRE(!itr->ConvertTo(tval));
        REQUIRE(!itr->ConvertTo(uuidval));
    }

    SECTION("less than int64_t min as double")
    {
        auto itr = b.push_back(b.root(), "", -9223372036854777856.0);

        REQUIRE(!itr->ConvertTo(bval));
        REQUIRE((itr->ConvertTo(fval) && fval == -9223372036854777856.0));
        REQUIRE(!itr->ConvertTo(ival));
        REQUIRE(!itr->ConvertTo(uval));
        REQUIRE(!itr->ConvertTo(sval));
        REQUIRE(!itr->ConvertTo(tval));
        REQUIRE(!itr->ConvertTo(uuidval));
    }

    SECTION("int64_t min")
    {
        // This value of -9223372036854775808 cannot be described as a literal
        // due to negative literals being positive literals with a unary minus
        // applied.
        constexpr int64_t c_int64min = (-9223372036854775807ll - 1);

        auto itr = b.push_back(b.root(), "", c_int64min);

        REQUIRE(!itr->ConvertTo(bval));
        REQUIRE((itr->ConvertTo(fval) && fval == -9223372036854775808.0));
        REQUIRE((itr->ConvertTo(ival) && ival == c_int64min));
        REQUIRE(!itr->ConvertTo(uval));
        REQUIRE(!itr->ConvertTo(sval));
        REQUIRE(!itr->ConvertTo(tval));
        REQUIRE(!itr->ConvertTo(uuidval));
    }

    SECTION("-1")
    {
        auto itr = b.push_back(b.root(), "", -1);

        REQUIRE(!itr->ConvertTo(bval));
        REQUIRE((itr->ConvertTo(fval) && fval == -1.0));
        REQUIRE((itr->ConvertTo(ival) && ival == -1));
        REQUIRE(!itr->ConvertTo(uval));
        REQUIRE(!itr->ConvertTo(sval));
        REQUIRE(!itr->ConvertTo(tval));
        REQUIRE(!itr->ConvertTo(uuidval));
    }

    SECTION("0")
    {
        auto itr = b.push_back(b.root(), "", 0);

        REQUIRE(!itr->ConvertTo(bval));
        REQUIRE((itr->ConvertTo(fval) && fval == 0.0));
        REQUIRE((itr->ConvertTo(ival) && ival == 0));
        REQUIRE((itr->ConvertTo(uval) && uval == 0));
        REQUIRE(!itr->ConvertTo(sval));
        REQUIRE(!itr->ConvertTo(tval));
        REQUIRE(!itr->ConvertTo(uuidval));
    }

    SECTION("int64_t max")
    {
        auto itr = b.push_back(b.root(), "", 9223372036854775807);

        REQUIRE(!itr->ConvertTo(bval));
        REQUIRE((itr->ConvertTo(fval) && fval == 9223372036854775807.0));
        REQUIRE((itr->ConvertTo(ival) && ival == 9223372036854775807));
        REQUIRE((itr->ConvertTo(uval) && uval == 9223372036854775807));
        REQUIRE(!itr->ConvertTo(sval));
        REQUIRE(!itr->ConvertTo(tval));
        REQUIRE(!itr->ConvertTo(uuidval));
    }

    SECTION("greater than int64_t max and less than uint64_t max")
    {
        auto itr = b.push_back(b.root(), "", 9223372036854775808ull);

        REQUIRE(!itr->ConvertTo(bval));
        REQUIRE((itr->ConvertTo(fval) && fval == 9223372036854775808.0));
        REQUIRE(!itr->ConvertTo(ival));
        REQUIRE((itr->ConvertTo(uval) && uval == 9223372036854775808ull));
        REQUIRE(!itr->ConvertTo(sval));
        REQUIRE(!itr->ConvertTo(tval));
        REQUIRE(!itr->ConvertTo(uuidval));
    }

    SECTION("greater than int64_t max and less than uint64_t max as double")
    {
        auto itr = b.push_back(b.root(), "", 9223372036854777856.0);

        REQUIRE(!itr->ConvertTo(bval));
        REQUIRE((itr->ConvertTo(fval) && fval == 9223372036854777856.0));
        REQUIRE(!itr->ConvertTo(ival));
        REQUIRE((itr->ConvertTo(uval) && uval == 9223372036854777856ull));
        REQUIRE(!itr->ConvertTo(sval));
        REQUIRE(!itr->ConvertTo(tval));
        REQUIRE(!itr->ConvertTo(uuidval));
    }

    SECTION("uint64_t max")
    {
        auto itr = b.push_back(b.root(), "", 18446744073709551615ull);

        REQUIRE(!itr->ConvertTo(bval));
        REQUIRE((itr->ConvertTo(fval) && fval == 18446744073709551615.0));
        REQUIRE(!itr->ConvertTo(ival));
        REQUIRE((itr->ConvertTo(uval) && uval == 18446744073709551615ull));
        REQUIRE(!itr->ConvertTo(sval));
        REQUIRE(!itr->ConvertTo(tval));
        REQUIRE(!itr->ConvertTo(uuidval));
    }

    SECTION("greater than uint64_t max")
    {
        auto itr = b.push_back(b.root(), "", 18446744073709551616.0);

        REQUIRE(!itr->ConvertTo(bval));
        REQUIRE((itr->ConvertTo(fval) && fval == 18446744073709551615.0));
        REQUIRE(!itr->ConvertTo(ival));
        REQUIRE(!itr->ConvertTo(uval));
        REQUIRE(!itr->ConvertTo(sval));
        REQUIRE(!itr->ConvertTo(tval));
        REQUIRE(!itr->ConvertTo(uuidval));
    }

    SECTION("time")
    {
        auto now = std::chrono::system_clock::now();
        auto nowTicks = std::chrono::time_point_cast<ticks>(now);
        auto itr = b.push_back(b.root(), "", now);

        REQUIRE(!itr->ConvertTo(bval));
        REQUIRE(!itr->ConvertTo(fval));
        REQUIRE(!itr->ConvertTo(ival));
        REQUIRE(!itr->ConvertTo(uval));
        REQUIRE(!itr->ConvertTo(sval));
        REQUIRE((itr->ConvertTo(tval) && tval == nowTicks));
        REQUIRE(!itr->ConvertTo(uuidval));
    }

    SECTION("uuid")
    {
        UuidStruct uuid;
        uuid_generate(uuid.Data);

        auto itr = b.push_back(b.root(), "", uuid);

        REQUIRE(!itr->ConvertTo(bval));
        REQUIRE(!itr->ConvertTo(fval));
        REQUIRE(!itr->ConvertTo(ival));
        REQUIRE(!itr->ConvertTo(uval));
        REQUIRE(!itr->ConvertTo(sval));
        REQUIRE(!itr->ConvertTo(tval));
        REQUIRE(
            (itr->ConvertTo(uuidval) &&
             0 == memcmp(uuidval.Data, uuid.Data, sizeof(uuidval.Data))));
    }
}
