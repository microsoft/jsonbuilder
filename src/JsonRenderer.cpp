// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <cassert>
#include <cmath>
#include <string>

#include <jsonbuilder/JsonRenderer.h>
#include <uuid/uuid.h>

#define WriteChar(ch) m_renderBuffer.push_back(ch)
#define WriteChars(pch, cch) m_renderBuffer.append(pch, cch)

namespace jsonbuilder {
/*
Efficiently multiply two 32-bit unsigned integers to get a 64-bit result.
(The current VC compiler does not optimize this -- if we don't use an
intrinsic, it makes a call to _aullmul.)
*/
#if defined(_M_ARM) || defined(_M_ARM64)
#    define UMul64(a, b) _arm_umull(a, b)
#elif defined(_M_IX86) || defined(_M_X64)
#    define UMul64(a, b) __emulu(a, b)
#else
static long long unsigned UMul64(unsigned a, unsigned b)
{
    return static_cast<long long unsigned>(a) * b;
}
#endif

/*
    Given a number from 0..15, returns the corresponding uppercase hex digit,
    i.e. '0'..'F'. Note: it is an error to pass a value larger than 15.
*/
static inline constexpr char u4_to_hex_upper(char unsigned _Val4)
{
    return ("0123456789ABCDEF")[_Val4];
}

/*
Multiply two 64-bit unsigned integers to get a 128-bit result. Return the high
64 bits of the answer.
*/
__attribute__((unused)) static long long unsigned
UMul128Hi(long long unsigned a, long long unsigned b)
{
#if defined(_M_X64) || defined(_M_ARM64)
    long long unsigned const high = __umulh(a, b);
#else
    long long unsigned const mid =
        UMul64(static_cast<unsigned>(a), static_cast<unsigned>(b >> 32)) +
        (UMul64(static_cast<unsigned>(a), static_cast<unsigned>(b)) >> 32);
    long long unsigned const high =
        UMul64(static_cast<unsigned>(a >> 32), static_cast<unsigned>(b >> 32)) +
        (mid >> 32) +
        ((UMul64(static_cast<unsigned>(a >> 32), static_cast<unsigned>(b)) +
          static_cast<unsigned>(mid)) >>
         32);
#endif
    return high;
}

/*
Formats a uint32 with leading 0s. Always writes cch characters.
*/
static void FormatUint(unsigned n, char* pch, unsigned cch)
{
    do
    {
        --cch;
        pch[cch] = '0' + (n % 10);
        n = n / 10;
    } while (cch != 0);
}

template<unsigned CB, class N>
static unsigned JsonRenderXInt(N const& n, char* pBuffer)
{
    // TODO: Why can't we use std::to_wchars when we're using c++17?
    std::string result = std::to_string(n);
    std::size_t const cch =
        std::min(result.size(), static_cast<std::size_t>(CB - 1));
    strncpy(pBuffer, result.c_str(), cch);
    pBuffer[cch] = 0;

    pBuffer[cch] = 0;
    return static_cast<unsigned>(cch);
}

unsigned JsonRenderUInt(long long unsigned n, char* pBuffer) throw()
{
    return JsonRenderXInt<21>(n, pBuffer);
}

unsigned JsonRenderInt(long long signed n, char* pBuffer) throw()
{
    return JsonRenderXInt<21>(n, pBuffer);
}

unsigned JsonRenderFloat(double n, char* pBuffer) throw()
{
    unsigned cch;

    if (std::isfinite(n))
    {
        cch = static_cast<unsigned>(snprintf(pBuffer, 31, "%.17g", n));
        if (cch > 31)
        {
            cch = 31;
        }
        pBuffer[cch] = 0;
    }
    else
    {
        cch = JsonRenderNull(pBuffer);
    }

    return cch;
}

unsigned JsonRenderBool(bool b, char* pBuffer) throw()
{
    unsigned cch;
    if (b)
    {
        strcpy(pBuffer, "true");
        cch = 4;
    }
    else
    {
        strcpy(pBuffer, "false");
        cch = 5;
    }
    return cch;
}

unsigned JsonRenderNull(char* pBuffer) throw()
{
    strcpy(pBuffer, "null");
    return 4;
}

unsigned JsonRenderTime(
    std::chrono::system_clock::time_point const& timePoint,
    char* pBuffer) throw()
{
    time_t printableTime = std::chrono::system_clock::to_time_t(timePoint);
    tm timeStruct = tm();
    gmtime_r(&printableTime, &timeStruct);

    auto subsecondDuration =
        timePoint.time_since_epoch() % std::chrono::seconds{ 1 };
    auto subsecondNanos =
        std::chrono::duration_cast<std::chrono::nanoseconds>(subsecondDuration);

    FormatUint(static_cast<unsigned>(timeStruct.tm_year + 1900), pBuffer + 0, 4);
    pBuffer[4] = '-';
    FormatUint(static_cast<unsigned>(timeStruct.tm_mon + 1), pBuffer + 5, 2);
    pBuffer[7] = '-';
    FormatUint(static_cast<unsigned>(timeStruct.tm_mday), pBuffer + 8, 2);
    pBuffer[10] = 'T';
    FormatUint(static_cast<unsigned>(timeStruct.tm_hour), pBuffer + 11, 2);
    pBuffer[13] = ':';
    FormatUint(static_cast<unsigned>(timeStruct.tm_min), pBuffer + 14, 2);
    pBuffer[16] = ':';
    FormatUint(static_cast<unsigned>(timeStruct.tm_sec), pBuffer + 17, 2);
    pBuffer[19] = '.';
    FormatUint(
        static_cast<unsigned>(subsecondNanos.count() / 100), pBuffer + 20, 7);
    pBuffer[27] = 'Z';
    pBuffer[28] = 0;
    return 28;
}

unsigned JsonRenderUuid(uuid_t const& g, char* pBuffer) throw()
{
    uuid_unparse_upper(g, pBuffer);
    return 36;
}

unsigned JsonRenderUuidWithBraces(uuid_t const& g, char* pBuffer) throw()
{
    pBuffer[0] = '{';
    JsonRenderUuid(g, pBuffer + 1);
    pBuffer[37] = '}';
    pBuffer[38] = 0;
    return 38;
}

JsonRenderer::JsonRenderer(
    bool pretty,
    nonstd::string_view const& newLine,
    unsigned indentSpaces) throw()
    : m_newLine(newLine), m_indentSpaces(indentSpaces), m_pretty(pretty)
{
    return;
}

void JsonRenderer::Reserve(size_type cb)
{
    m_renderBuffer.reserve(cb);
}

JsonRenderer::size_type JsonRenderer::Size() const throw()
{
    return m_renderBuffer.size();
}

JsonRenderer::size_type JsonRenderer::Capacity() const throw()
{
    return m_renderBuffer.capacity();
}

bool JsonRenderer::Pretty() const throw()
{
    return m_pretty;
}

void JsonRenderer::Pretty(bool value) throw()
{
    m_pretty = value;
}

nonstd::string_view const& JsonRenderer::NewLine() const throw()
{
    return m_newLine;
}

void JsonRenderer::NewLine(nonstd::string_view const& value) throw()
{
    m_newLine = value;
}

unsigned JsonRenderer::IndentSpaces() const throw()
{
    return m_indentSpaces;
}

void JsonRenderer::IndentSpaces(unsigned value) throw()
{
    m_indentSpaces = value;
}

nonstd::string_view JsonRenderer::Render(JsonBuilder const& builder)
{
    auto itRoot = builder.root();
    m_renderBuffer.clear();
    m_indent = 0;
    RenderStructure(itRoot, true);
    WriteChar('\0');
    return nonstd::string_view(m_renderBuffer.data(), m_renderBuffer.size() - 1);
}

nonstd::string_view JsonRenderer::Render(JsonBuilder::const_iterator const& it)
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
    return nonstd::string_view(m_renderBuffer.data(), m_renderBuffer.size() - 1);
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
        RenderString(it->GetUnchecked<nonstd::string_view>());
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
        RenderTime(it->GetUnchecked<std::chrono::system_clock::time_point>());
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

void JsonRenderer::RenderFloat(double const& value)
{
    auto pch = m_renderBuffer.GetAppendPointer(32);
    pch += JsonRenderFloat(value, pch);
    m_renderBuffer.SetEndPointer(pch);
}

void JsonRenderer::RenderInt(long long signed const& value)
{
    auto const cchMax = 20u;
    auto pch = m_renderBuffer.GetAppendPointer(cchMax);

    // TODO: Why can't we use std::to_wchars when we're using c++17?
    unsigned cch = static_cast<unsigned>(snprintf(pch, cchMax, "%lld", value));
    pch += cch > cchMax ? cchMax : cch;
    m_renderBuffer.SetEndPointer(pch);
}

void JsonRenderer::RenderUInt(long long unsigned const& value)
{
    auto const cchMax = 20u;
    auto pch = m_renderBuffer.GetAppendPointer(cchMax);

    // TODO: Why can't we use std::to_wchars when we're using c++17?
    unsigned cch = static_cast<unsigned>(snprintf(pch, cchMax, "%llu", value));
    pch += cch > cchMax ? cchMax : cch;
    m_renderBuffer.SetEndPointer(pch);
}

void JsonRenderer::RenderTime(std::chrono::system_clock::time_point const& value)
{
    auto pch = m_renderBuffer.GetAppendPointer(32);
    *pch++ = '"';
    pch += JsonRenderTime(value, pch);
    *pch++ = '"';
    m_renderBuffer.SetEndPointer(pch);
}

void JsonRenderer::RenderUuid(uuid_t const& value)
{
    auto pch = m_renderBuffer.GetAppendPointer(38);
    *pch++ = '"';
    pch += JsonRenderUuid(value, pch);
    *pch++ = '"';
    m_renderBuffer.SetEndPointer(pch);
}

void JsonRenderer::RenderString(nonstd::string_view const& value)
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
                *p++ = u4_to_hex_upper((ch >> 4) & 0xf);
                *p++ = u4_to_hex_upper(ch & 0xf);
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