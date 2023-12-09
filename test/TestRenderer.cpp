// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <cstring>
#include <cstdio>
#include <iterator>
#include <type_traits>

#include <catch2/catch.hpp>
#include <jsonbuilder/JsonRenderer.h>

#ifdef _WIN32
#undef uuid_t
using uuid_t = char unsigned[16];
#else
#include <uuid/uuid.h>
#endif

using namespace jsonbuilder;

template<class N>
static void TestUInt(N n)
{
    unsigned const cchBuf = 24;
    char buf1[cchBuf];
    char buf2[cchBuf];
    unsigned cch;

    memset(buf1, 1, sizeof(buf1));
    cch = JsonRenderUInt(n, buf1);
    REQUIRE(cch < cchBuf);
    for (unsigned i = cch + 1; i != cchBuf; i++)
    {
        REQUIRE(buf1[i] == 1);
    }
    std::snprintf(
        buf2,
        std::extent<decltype(buf2)>::value,
        "%llu",
        static_cast<long long unsigned>(n));
    for (unsigned i = 0; i <= cch; i++)
    {
        REQUIRE(buf1[i] == buf2[i]);
    }
}

template<class N>
static void TestInt(N n)
{
    unsigned const cchBuf = 24;
    char buf1[cchBuf];
    char buf2[cchBuf];
    unsigned cch;

    memset(buf1, 1, sizeof(buf1));
    cch = JsonRenderInt(n, buf1);
    REQUIRE(cch < cchBuf);
    for (unsigned i = cch + 1; i != cchBuf; i++)
    {
        REQUIRE(buf1[i] == 1);
    }
    std::snprintf(
        buf2,
        std::extent<decltype(buf2)>::value,
        "%lld",
        static_cast<long long signed>(n));
    for (unsigned i = 0; i <= cch; i++)
    {
        REQUIRE(buf1[i] == buf2[i]);
    }
}

template<class N>
static void TestFloat(N n)
{
    unsigned const cchBuf = 32;
    char buf1[cchBuf];
    char buf2[cchBuf];
    unsigned cch;

    memset(buf1, 1, sizeof(buf1));
    cch = JsonRenderFloat(n, buf1);
    REQUIRE(cch < cchBuf);
    for (unsigned i = cch + 1; i != cchBuf; i++)
    {
        REQUIRE(buf1[i] == 1);
    }
    std::snprintf(
        buf2, std::extent<decltype(buf2)>::value, "%.17g", static_cast<double>(n));
    for (unsigned i = 0; i <= cch; i++)
    {
        REQUIRE(buf1[i] == buf2[i]);
    }
}

static void TestBool(bool n)
{
    unsigned const cchBuf = 6;
    char buf1[cchBuf];
    char const* buf2 = n ? "true" : "false";
    memset(buf1, 1, sizeof(buf1));
    unsigned cch = JsonRenderBool(n, buf1);
    REQUIRE(cch < cchBuf);
    for (unsigned i = cch + 1; i != cchBuf; i++)
    {
        REQUIRE(buf1[i] == 1);
    }
    for (unsigned i = 0; i <= cch; i++)
    {
        REQUIRE(buf1[i] == buf2[i]);
    }
}

// TODO: Unused
// static void TestTime() {}

template<class N>
static void TestUInts()
{
    SECTION("0") { TestUInt<N>(0); }
    SECTION("min") { TestUInt<N>(std::numeric_limits<N>::min()); }
    SECTION("max") { TestUInt<N>(std::numeric_limits<N>::max()); }
}

template<class N>
static void TestInts()
{
    SECTION("0") { TestInt<N>(0); }
    SECTION("min") { TestInt<N>(std::numeric_limits<N>::min()); }
    SECTION("max") { TestInt<N>(std::numeric_limits<N>::max()); }
}

template<class N>
static void TestFloats()
{
    SECTION("0") { TestFloat<N>(0); }
    SECTION("min") { TestFloat<N>(std::numeric_limits<N>::min()); }
    SECTION("max") { TestFloat<N>(std::numeric_limits<N>::max()); }
}

TEST_CASE("JsonRenderer values match printf", "[renderer]")
{
    SECTION("signed char") { TestInts<signed char>(); }
    SECTION("signed short") { TestInts<signed short>(); }
    SECTION("signed int") { TestInts<signed int>(); }
    SECTION("signed long") { TestInts<signed long>(); }
    SECTION("signed long long") { TestInts<signed long long>(); }

    SECTION("unsigned char") { TestUInts<unsigned char>(); }
    SECTION("unsigned short") { TestUInts<unsigned short>(); }
    SECTION("unsigned int") { TestUInts<unsigned int>(); }
    SECTION("unsigned long") { TestUInts<unsigned long>(); }
    SECTION("unsigned long long") { TestUInts<unsigned long long>(); }

    SECTION("float") { TestFloats<float>(); }
    SECTION("double") { TestFloats<double>(); }

    SECTION("bool-false") { TestBool(false); }
    SECTION("bool-true") { TestBool(true); }
}

TEST_CASE("JsonRenderer JsonNull")
{
    unsigned const cchBuf = 24;
    char buf1[cchBuf];
    char const* buf2 = "null";
    memset(buf1, 1, sizeof(buf1));
    unsigned cch = JsonRenderNull(buf1);
    REQUIRE(cch < cchBuf);
    for (unsigned i = cch + 1; i != cchBuf; i++)
    {
        REQUIRE(buf1[i] == 1);
    }
    for (unsigned i = 0; i <= cch; i++)
    {
        REQUIRE(buf1[i] == buf2[i]);
    }
}

using namespace std::string_view_literals;

TEST_CASE("JsonRenderer JsonTime", "[renderer]")
{
    auto epoch = std::chrono::system_clock::from_time_t(0);
    using namespace std::chrono_literals;

    char chars[39];
    unsigned cch;

    memset(chars, 1, sizeof(chars));
    cch = JsonRenderTime(epoch, chars);
    REQUIRE(cch == strlen(chars));
    REQUIRE(chars == "1970-01-01T00:00:00.0000000Z"sv);

    memset(chars, 1, sizeof(chars));
    cch = JsonRenderTime(epoch + 2s, chars);
    REQUIRE(cch == strlen(chars));
    REQUIRE(chars == "1970-01-01T00:00:02.0000000Z"sv);

    memset(chars, 1, sizeof(chars));
    cch = JsonRenderTime(epoch - 2s, chars);
    REQUIRE(cch == strlen(chars));
    REQUIRE(chars == "1969-12-31T23:59:58.0000000Z"sv);

    memset(chars, 1, sizeof(chars));
    cch = JsonRenderTime(epoch + 2ms, chars);
    REQUIRE(cch == strlen(chars));
    REQUIRE(chars == "1970-01-01T00:00:00.0020000Z"sv);

    memset(chars, 1, sizeof(chars));
    cch = JsonRenderTime(epoch - 2ms, chars);
    REQUIRE(cch == strlen(chars));
    REQUIRE(chars == "1969-12-31T23:59:59.9980000Z"sv);

    memset(chars, 1, sizeof(chars));
    cch = JsonRenderTime(TimeStruct::FromValue(0xFEDCBA9876543210), chars);
    REQUIRE(cch == strlen(chars));
    REQUIRE(chars == "FILETIME(0xFEDCBA9876543210)"sv);
}

TEST_CASE("JsonRenderer JsonUuid", "[renderer]")
{
    uuid_t uuid;
    for (char unsigned i = 0; i < 16; i++)
    {
        uuid[i] = i;
    }

    char chars[39];
    memset(chars, 1, sizeof(chars));

    SECTION("Without braces")
    {
        unsigned cch = JsonRenderUuid(uuid, chars);
        REQUIRE(cch == strlen(chars));
        REQUIRE(chars == "00010203-0405-0607-0809-0A0B0C0D0E0F"sv);
    }

    SECTION("With braces")
    {
        unsigned cch = JsonRenderUuidWithBraces(uuid, chars);
        REQUIRE(cch == strlen(chars));
        REQUIRE(chars == "{00010203-0405-0607-0809-0A0B0C0D0E0F}"sv);
    }
}

TEST_CASE("JsonRenderer full object", "[renderer]")
{
    JsonBuilder b;

    auto objItr = b.push_back(b.root(), "obj", JsonObject);
    b.push_back(objItr, "str", "strval");
    b.push_back(objItr, "str2", "str2val");
    b.push_back(objItr, "hugeUintVal", std::numeric_limits<uint64_t>::max());
    b.push_back(objItr, "mostNegativeIntVal", std::numeric_limits<int64_t>::min());

    auto arrItr = b.push_back(b.root(), "arr", JsonArray);
    b.push_back(arrItr, "useless", 1);
    b.push_back(arrItr, "useless2", 2);

    SECTION("Default renderer")
    {
        JsonRenderer renderer;
        auto renderString = renderer.Render(b);

        const char* expectedString =
            R"({"obj":{"str":"strval","str2":"str2val","hugeUintVal":18446744073709551615,"mostNegativeIntVal":-9223372036854775808},"arr":[1,2]})";

        REQUIRE(renderString == expectedString);
    }

    SECTION("Pretty renderer")
    {
        JsonRenderer renderer;
        renderer.Pretty(true);
        auto renderString = renderer.Render(b);

        const char* expectedString =
            R"({
  "obj": {
    "str": "strval",
    "str2": "str2val",
    "hugeUintVal": 18446744073709551615,
    "mostNegativeIntVal": -9223372036854775808
  },
  "arr": [
    1,
    2
  ]
})";

        REQUIRE(renderString == expectedString);
    }
}
