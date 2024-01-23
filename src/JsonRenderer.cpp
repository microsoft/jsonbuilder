// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <jsonbuilder/JsonRenderer.h>

#ifdef _WIN32
#include <windows.h>
#else
#define __USE_TIME_BITS64
#include <time.h>
static_assert(sizeof(time_t) == 8, "time_t must be 64 bits");
#endif

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <charconv>
#include <limits>

#ifdef __cpp_lib_to_chars
#define FORMAT_DOUBLE_USING_TO_CHARS 1
#else
#define FORMAT_DOUBLE_USING_TO_CHARS 0
#endif

#if !FORMAT_DOUBLE_USING_TO_CHARS
#include <cstdio>
#endif

#ifndef _Out_writes_
#define _Out_writes_(c)
#endif

#define WriteChar(ch) m_renderBuffer.push_back(ch)
#define WriteChars(pch, cch) m_renderBuffer.append(pch, cch)

auto constexpr TicksPerSecond = 10'000'000u;
auto constexpr FileTime1970Ticks = 116444736000000000u;
auto constexpr FileTime10000Ticks = 2650467744000000000u;
using ticks = std::chrono::duration<std::int64_t, std::ratio<1, TicksPerSecond>>;

namespace jsonbuilder {

static constexpr unsigned Log10Ceil(unsigned n)
{
    return n < 10 ? 1 : 1 + Log10Ceil(n / 10);
}

template<unsigned N>
static unsigned
MemCpyFromLiteral(_Out_writes_(N) char* dest, char const (&src)[N]) noexcept
{
    memcpy(dest, src, N);
    return N - 1;
}

/*
Given a number from 0..15, returns the corresponding uppercase hex digit,
i.e. '0'..'F'. Note: it is an error to pass a value larger than 15.
*/
static constexpr char u4_to_hex_upper(char unsigned val4)
{
    assert(val4 < 16);
    return ("0123456789ABCDEF")[val4];
}

static unsigned u8_to_hex_upper(char unsigned val8, _Out_writes_(2) char* pch)
{
    pch[0] = u4_to_hex_upper((val8 >> 4) & 0xf);
    pch[1] = u4_to_hex_upper(val8 & 0xf);
    return 2;
}

/*
Formats a uint32 with leading 0s. Always writes cch characters.
*/
static void FormatUint(unsigned n, _Out_writes_(cch) char* pch, unsigned cch)
{
    do
    {
        --cch;
        pch[cch] = '0' + (n % 10);
        n = n / 10;
    } while (cch != 0);
}

template<unsigned CB, class N>
static unsigned JsonRenderXInt(N const& n, _Out_writes_z_(CB) char* pBuffer)
{
    auto const result = std::to_chars(pBuffer, pBuffer + CB - 1, n);
    unsigned const cch = static_cast<unsigned>(result.ptr - pBuffer);
    assert(result.ec == std::errc());
    assert(cch < CB);

    pBuffer[cch] = 0;
    return cch;
}

unsigned JsonRenderUInt(long long unsigned n, _Out_writes_z_(21) char* pBuffer) noexcept
{
    return JsonRenderXInt<21>(n, pBuffer);
}

unsigned JsonRenderInt(long long signed n, _Out_writes_z_(21) char* pBuffer) noexcept
{
    return JsonRenderXInt<21>(n, pBuffer);
}

unsigned JsonRenderFloat(double n, _Out_writes_z_(32) char* pBuffer) noexcept
{
    auto const CB = 32u;
    unsigned cch;

    if (std::isfinite(n))
    {
        // "-1.234e+56\0"
        auto constexpr cchMax =
            sizeof("-.e+") + // Non-digit chars, including NUL.
            std::numeric_limits<double>::max_digits10 +
            Log10Ceil(std::numeric_limits<double>::max_exponent10);
        static_assert(cchMax <= CB, "Unexpected max_digits10");

#if FORMAT_DOUBLE_USING_TO_CHARS

        auto const result = std::to_chars(pBuffer, pBuffer + CB - 1, n);
        cch = static_cast<unsigned>(result.ptr - pBuffer);
        assert(result.ec == std::errc());
        assert(cch < CB);
        pBuffer[cch] = 0;

#else // FORMAT_DOUBLE_USING_TO_CHARS

        cch = static_cast<unsigned>(std::snprintf(pBuffer, CB, "%.17g", n));
        if (cch >= CB)
        {
            // Unexpected - should always fit.
            assert(false);
            cch = CB - 1;
            pBuffer[cch] = 0;
        }

#endif // FORMAT_DOUBLE_USING_TO_CHARS
    }
    else
    {
        cch = JsonRenderNull(pBuffer);
    }

    return cch;
}

unsigned JsonRenderBool(bool b, _Out_writes_z_(6) char* pBuffer) noexcept
{
    return b ? MemCpyFromLiteral(pBuffer, "true") : MemCpyFromLiteral(pBuffer, "false");
}

unsigned JsonRenderNull(_Out_writes_z_(5) char* pBuffer) noexcept
{
    return MemCpyFromLiteral(pBuffer, "null");
}

static unsigned
RenderFileTime(uint64_t ft, _Out_writes_z_(29) char* pBuffer) noexcept
{
    // Only attempt to render years between 1601 and 9999.
    if (ft < FileTime10000Ticks)
    {
        auto const subsecondTicks = static_cast<unsigned>(ft % TicksPerSecond);

#ifdef _WIN32
        SYSTEMTIME st;
        if (!FileTimeToSystemTime(reinterpret_cast<FILETIME const*>(&ft), &st))
        {
            assert(false); // Unexpected - should succeed for all values 1601..32000.
            goto NotOk;
        }

        auto const year = st.wYear;
        auto const month = st.wMonth;
        auto const day = st.wDay;
        auto const hour = st.wHour;
        auto const minute = st.wMinute;
        auto const second = st.wSecond;
#else
        auto const seconds1601 = ft / TicksPerSecond;
        time_t const seconds1970 = static_cast<int64_t>(seconds1601) - (FileTime1970Ticks / TicksPerSecond);

        tm timeStruct;
        if (0 == gmtime_r(&seconds1970, &timeStruct))
        {
            goto NotOk;
        }

        auto const year = static_cast<unsigned>(timeStruct.tm_year + 1900);
        auto const month = static_cast<unsigned>(timeStruct.tm_mon + 1);
        auto const day = static_cast<unsigned>(timeStruct.tm_mday);
        auto const hour = static_cast<unsigned>(timeStruct.tm_hour);
        auto const minute = static_cast<unsigned>(timeStruct.tm_min);
        auto const second = static_cast<unsigned>(timeStruct.tm_sec);
#endif

        FormatUint(year, pBuffer + 0, 4);
        pBuffer[4] = '-';
        FormatUint(month, pBuffer + 5, 2);
        pBuffer[7] = '-';
        FormatUint(day, pBuffer + 8, 2);
        pBuffer[10] = 'T';
        FormatUint(hour, pBuffer + 11, 2);
        pBuffer[13] = ':';
        FormatUint(minute, pBuffer + 14, 2);
        pBuffer[16] = ':';
        FormatUint(second, pBuffer + 17, 2);
        pBuffer[19] = '.';
        FormatUint(subsecondTicks, pBuffer + 20, 7);
        pBuffer[27] = 'Z';
        pBuffer[28] = 0;
    }
    else
    {
    NotOk:

        // Unable to print a nice date-time. Print the raw FILETIME instead,
        // something like "FILETIME(0x0000000000000000)" (still 28 chars).
        auto p = pBuffer;
        p += MemCpyFromLiteral(p, "FILETIME(0x");

        auto const pData = reinterpret_cast<unsigned char const*>(&ft);
        for (unsigned i = 0; i != 8; i += 1)
        {
            p += u8_to_hex_upper(pData[7u - i], p);
        }

        p += MemCpyFromLiteral(p, ")");
        assert(p == pBuffer + 28);
        assert(pBuffer[28] == 0);
    }

    return 28;
}

unsigned JsonRenderTime(
    TimeStruct const t,
    _Out_writes_z_(29) char* pBuffer) noexcept
{
    return RenderFileTime(t.Value(), pBuffer);
}

unsigned JsonRenderTime(
    std::chrono::system_clock::time_point const timePoint,
    _Out_writes_z_(29) char* pBuffer) noexcept
{
    uint64_t const ft = FileTime1970Ticks + std::chrono::duration_cast<ticks>(timePoint.time_since_epoch()).count();
    return RenderFileTime(ft, pBuffer);
}

unsigned JsonRenderUuid(_In_reads_(16) char unsigned const* g, _Out_writes_z_(37) char* pBuffer) noexcept
{
    u8_to_hex_upper(g[0], pBuffer + 0);
    u8_to_hex_upper(g[1], pBuffer + 2);
    u8_to_hex_upper(g[2], pBuffer + 4);
    u8_to_hex_upper(g[3], pBuffer + 6);
    pBuffer[8] = '-';
    u8_to_hex_upper(g[4], pBuffer + 9);
    u8_to_hex_upper(g[5], pBuffer + 11);
    pBuffer[13] = '-';
    u8_to_hex_upper(g[6], pBuffer + 14);
    u8_to_hex_upper(g[7], pBuffer + 16);
    pBuffer[18] = '-';
    u8_to_hex_upper(g[8], pBuffer + 19);
    u8_to_hex_upper(g[9], pBuffer + 21);
    pBuffer[23] = '-';
    u8_to_hex_upper(g[10], pBuffer + 24);
    u8_to_hex_upper(g[11], pBuffer + 26);
    u8_to_hex_upper(g[12], pBuffer + 28);
    u8_to_hex_upper(g[13], pBuffer + 30);
    u8_to_hex_upper(g[14], pBuffer + 32);
    u8_to_hex_upper(g[15], pBuffer + 34);
    pBuffer[36] = 0;
    return 36;
}

unsigned JsonRenderUuidWithBraces(_In_reads_(16) char unsigned const* g, _Out_writes_z_(39) char* pBuffer) noexcept
{
    pBuffer[0] = '{';
    JsonRenderUuid(g, pBuffer + 1);
    pBuffer[37] = '}';
    pBuffer[38] = 0;
    return 38;
}

JsonRenderer::~JsonRenderer()
{
    return;
}

JsonRenderer::JsonRenderer(
    bool pretty,
    std::string_view newLine,
    unsigned indentSpaces) noexcept
    : m_newLine(newLine), m_indentSpaces(indentSpaces), m_indent(0), m_pretty(pretty)
{
    return;
}

void JsonRenderer::Reserve(size_type cb)
{
    m_renderBuffer.reserve(cb);
}

JsonRenderer::size_type JsonRenderer::Size() const noexcept
{
    return m_renderBuffer.size();
}

JsonRenderer::size_type JsonRenderer::Capacity() const noexcept
{
    return m_renderBuffer.capacity();
}

bool JsonRenderer::Pretty() const noexcept
{
    return m_pretty;
}

void JsonRenderer::Pretty(bool value) noexcept
{
    m_pretty = value;
}

std::string_view JsonRenderer::NewLine() const noexcept
{
    return m_newLine;
}

void JsonRenderer::NewLine(std::string_view const value) noexcept
{
    m_newLine = value;
}

unsigned JsonRenderer::IndentSpaces() const noexcept
{
    return m_indentSpaces;
}

void JsonRenderer::IndentSpaces(unsigned value) noexcept
{
    m_indentSpaces = value;
}

std::string_view JsonRenderer::Render(JsonBuilder const& builder)
{
    auto itRoot = builder.root();
    m_renderBuffer.clear();
    m_indent = 0;
    RenderStructure(itRoot, true);
    WriteChar('\0');
    return std::string_view(m_renderBuffer.data(), m_renderBuffer.size() - 1);
}

std::string_view JsonRenderer::Render(JsonBuilder::const_iterator const& it)
{
    m_renderBuffer.clear();
    m_indent = 0;
    if (it.IsRoot())
    {
        RenderStructure(it, true);
    }
    else
    {
        RenderValue(it);
    }
    WriteChar('\0');
    return std::string_view(m_renderBuffer.data(), m_renderBuffer.size() - 1);
}

void JsonRenderer::RenderCustom(RenderBuffer&, iterator const& it)
{
    auto const cchMax = 32u;
    auto pch = m_renderBuffer.GetAppendPointer(cchMax);
    unsigned cch =
        static_cast<unsigned>(snprintf(pch, cchMax, "\"Custom#%u\"", it->Type()));
    pch += cch > cchMax ? cchMax : cch;
    m_renderBuffer.SetEndPointer(pch);
}

void JsonRenderer::RenderValue(iterator const& it)
{
    assert(!it.IsRoot());
    switch (it->Type())
    {
    case JsonObject:
        RenderStructure(it, true);
        break;
    case JsonArray:
        RenderStructure(it, false);
        break;
    case JsonNull:
        WriteChars("null", 4);
        break;
    case JsonBool:
        if (it->GetUnchecked<bool>())
        {
            WriteChars("true", 4);
        }
        else
        {
            WriteChars("false", 5);
        }
        break;
    case JsonUtf8:
        RenderString(it->GetUnchecked<std::string_view>());
        break;
    case JsonFloat:
        RenderFloat(it->GetUnchecked<double>());
        break;
    case JsonInt:
        RenderInt(it->GetUnchecked<long long signed>());
        break;
    case JsonUInt:
        RenderUInt(it->GetUnchecked<long long unsigned>());
        break;
    case JsonTime:
        RenderTime(it->GetUnchecked<TimeStruct>());
        break;
    case JsonUuid:
        RenderUuid(it->GetUnchecked<UuidStruct>().Data);
        break;
    default:
        RenderCustom(m_renderBuffer, it);
        break;
    }
}

void JsonRenderer::RenderStructure(iterator const& itParent, bool showNames)
{
    WriteChar(showNames ? '{' : '[');

    auto it = itParent.begin();
    auto itEnd = itParent.end();
    if (it != itEnd)
    {
        m_indent += m_indentSpaces;

        for (;;)
        {
            if (m_pretty)
            {
                RenderNewline();
            }

            if (showNames)
            {
                RenderString(it->Name());
                WriteChar(':');

                if (m_pretty)
                {
                    WriteChar(' ');
                }
            }

            RenderValue(it);

            ++it;
            if (it == itEnd)
            {
                break;
            }

            WriteChar(',');
        }

        m_indent -= m_indentSpaces;

        if (m_pretty)
        {
            RenderNewline();
        }
    }

    WriteChar(showNames ? '}' : ']');
}

void JsonRenderer::RenderFloat(double const value)
{
    auto pch = m_renderBuffer.GetAppendPointer(32);
    pch += JsonRenderFloat(value, pch);
    m_renderBuffer.SetEndPointer(pch);
}

void JsonRenderer::RenderInt(long long signed const value)
{
    auto const cchMax = 21u;
    auto pch = m_renderBuffer.GetAppendPointer(cchMax);
    auto result = std::to_chars(pch, pch + cchMax, value);
    assert(result.ec == std::errc());
    m_renderBuffer.SetEndPointer(result.ptr);
}

void JsonRenderer::RenderUInt(long long unsigned const value)
{
    auto const cchMax = 21u;
    auto pch = m_renderBuffer.GetAppendPointer(cchMax);
    auto result = std::to_chars(pch, pch + cchMax, value);
    assert(result.ec == std::errc());
    m_renderBuffer.SetEndPointer(result.ptr);
}

void JsonRenderer::RenderTime(TimeStruct value)
{
    auto pch = m_renderBuffer.GetAppendPointer(32);
    *pch++ = '"';
    pch += RenderFileTime(value.Value(), pch);
    *pch++ = '"';
    m_renderBuffer.SetEndPointer(pch);
}

void JsonRenderer::RenderUuid(_In_reads_(16) char unsigned const* value)
{
    auto pch = m_renderBuffer.GetAppendPointer(38);
    *pch++ = '"';
    pch += JsonRenderUuid(value, pch);
    *pch++ = '"';
    m_renderBuffer.SetEndPointer(pch);
}

void JsonRenderer::RenderString(std::string_view const value)
{
    WriteChar('"');
    for (auto ch : value)
    {
        if (static_cast<unsigned char>(ch) < 0x20)
        {
            // Control character - must be escaped.
            switch (ch)
            {
            case 8:
                WriteChar('\\');
                WriteChar('b');
                break;
            case 9:
                WriteChar('\\');
                WriteChar('t');
                break;
            case 10:
                WriteChar('\\');
                WriteChar('n');
                break;
            case 12:
                WriteChar('\\');
                WriteChar('f');
                break;
            case 13:
                WriteChar('\\');
                WriteChar('r');
                break;
            default:
                auto p = m_renderBuffer.GetAppendPointer(6);
                *p++ = '\\';
                *p++ = 'u';
                *p++ = '0';
                *p++ = '0';
                p += u8_to_hex_upper(ch, p);
                m_renderBuffer.SetEndPointer(p);
                break;
            }
        }
        else if (ch == '"' || ch == '\\')
        {
            // ASCII character - pass through (escape quote and backslash)
            WriteChar('\\');
            WriteChar(ch);
        }
        else
        {
            WriteChar(ch);
        }
    }
    WriteChar('"');
}

void JsonRenderer::RenderNewline()
{
    WriteChars(m_newLine.data(), static_cast<unsigned>(m_newLine.size()));
    m_renderBuffer.append(m_indent, ' ');
}
}  // namespace jsonbuilder
