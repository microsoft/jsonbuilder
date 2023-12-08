// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

/*
JsonRenderer converts JsonBuilder trees into utf8 JSON text.

Summary:
- JsonRenderer
- JsonRenderBool
- JsonRenderFalse
- JsonRenderTime
- JsonRenderFloat
- JsonRenderInt
- JsonRenderNull
- JsonRenderTrue
- JsonRenderUInt
- JsonRenderUuid
- JsonRenderUuidWithBraces
*/

#pragma once

#include <jsonbuilder/JsonBuilder.h>

#ifndef _Out_writes_z_
#define _Out_writes_z_(c)
#endif

namespace jsonbuilder {
/*
Converts JsonBuilder data into utf-8 JSON text.
Recognizes the built-in JsonType types. To support other (custom) types,
derive from JsonRenderer and override RenderCustom.
*/
class JsonRenderer
{
  protected:
    typedef JsonInternal::PodVector<char> RenderBuffer;
    typedef JsonBuilder::const_iterator iterator;

  private:
    RenderBuffer m_renderBuffer;
    std::string_view m_newLine;
    unsigned m_indentSpaces;
    unsigned m_indent;
    bool m_pretty;

  public:
    typedef JsonInternal::PodVector<char>::size_type size_type;

    virtual ~JsonRenderer();

    /*
    Initializes a new instance of the JsonRenderer class.
    Optionally sets the initial value of the formatting properties.
    */
    explicit JsonRenderer(
        bool pretty = false,
        std::string_view newLine = "\n",
        unsigned indentSpaces = 2) throw();

    /*
    Preallocates memory in the rendering buffer (increases capacity).
    */
    void Reserve(size_type cb);  // may throw bad_alloc, length_error

    /*
    Gets the current size of the rendering buffer, in bytes.
    */
    size_type Size() const throw();

    /*
    Gets the current capacity of the rendering buffer (how large it can grow
    without allocating additional memory), in bytes.
    */
    size_type Capacity() const throw();

    /*
    Gets a value indicating whether the output will be formatted nicely.
    If true, insignificant whitespace (spaces and newlines) will be added to
    improve readability by humans and to put each value on its own line.
    If false, all insignificant whitespace will be omitted.
    Default value is false.
    */
    bool Pretty() const throw();

    /*
    Set a value indicating whether the output will be formatted nicely.
    If true, insignificant whitespace (spaces and newlines) will be added to
    improve readability by humans and to put each value on its own line.
    If false, all insignificant whitespace will be omitted.
    Default value is false.
    */
    void Pretty(bool) throw();

    /*
    Gets the string that is used for newline when Pretty() is true.
    Default value is "\n".
    */
    std::string_view NewLine() const throw();

    /*
    Sets the string that is used for newline when Pretty() is true.
    Note that the JsonRenderer does not make a copy of the string that the
    specified string_view references. The string passed in here must be valid
    for as long as the JsonRenderer exists (string literal is ok).
    Default value is "\n".
    */
    void NewLine(std::string_view value) throw();

    /*
    Gets the number of spaces per indent level. Default value is 2.
    */
    unsigned IndentSpaces() const throw();

    /*
    Sets the number of spaces per indent level. Default value is 2.
    */
    void IndentSpaces(unsigned value) throw();

    /*
    Renders the contents of the specified JsonBuilder as utf-8 JSON, starting
    at the root value.
    Returns a string_view with the resulting JSON string. The returned string
    is nul-terminated, but the nul is not included as part of the string_view
    itself, so return.data()[return.size()] == '\0'.
    The returned string_view is valid until the next call to Render or until
    this JsonBuilder is destroyed.
    */
    std::string_view Render(JsonBuilder const& builder);  // may throw
                                                          // bad_alloc,
                                                          // length_error

    /*
    Renders the contents of a JsonBuilder as utf-8 JSON, starting at the
    specified value.
    Returns a string_view with the resulting JSON string. The returned string
    is nul-terminated, but the nul is not included as part of the string_view
    itself, so return.data()[return.size()] == '\0'.
    The returned string_view is valid until the next call to Render or until
    this JsonBuilder is destroyed.
    */
    std::string_view
    Render(JsonBuilder::const_iterator const& it);  // may throw bad_alloc,
                                                    // length_error

  protected:
    /*
    Override this method to provide rendering behavior for custom value types.
    The utf-8 representation of the value referenced by itValue should be
    appended to the end of buffer by calling buffer.push_back with the bytes
    of the utf-8 representation.
    */
    virtual void RenderCustom(
        RenderBuffer& buffer,
        iterator const& itValue);  // may throw bad_alloc, length_error

  private:
    /*
    Renders any value and its children by dispatching to the appropriate
    subroutine.
    CANNOT RENDER THE ROOT! (Don't call this if it.IsRoot() is true.)
    */
    void RenderValue(iterator const& it);

    /*
    Renders an object/array value and its children.
    itParent must be an array or object.
    Set showNames = true for object. Set showNames = false for array.
    Can be called with itParent == end() to render the root.
    Example output: {"ObjectName":{"ArrayName":[7]}}
    */
    void RenderStructure(iterator const& itParent, bool showNames);

    /*
    Renders value as floating-point. Requires that cb be 4 or 8. Data will be
    interpreted as a little-endian float or double.
    Example output: 123.45
    */
    void RenderFloat(double value);

    /*
    Renders value as signed integer. Requires that cb be 1, 2, 4 or 8. Data will
    be interpreted as a little-endian signed integer.
    Example output: -12345
    */
    void RenderInt(long long signed value);

    /*
    Renders value as unsigned integer. Requires that cb be 1, 2, 4 or 8. Data
    will be interpreted as a little-endian unsigned integer.
    Example output: 12345
    */
    void RenderUInt(long long unsigned value);

    /*
    Renders value as time. Requires that cb be 8. Data will be interpreted as
    number of 100ns intervals since 1601-01-01T00:00:00Z.
    Example output: "2015-04-02T02:09:14.7927652Z".
    */
    void RenderTime(TimeStruct value);

    /*
    Renders big-endian value as UUID. Compatible with uuid_t from libuuid.
    Example output: "CD8D0A5E-6409-4B8E-9366-B815CEF0E35D".
    */
    void RenderUuid(_In_reads_(16) char unsigned const* value);

    /*
    Renders value as a string. Escapes any control characters and adds quotes
    around the result. Example output: "String\n"
    */
    void RenderString(std::string_view value);

    /*
    If pretty-printing is disabled, has no effect.
    If pretty-printing is enabled, writes m_newLine followed by
    (m_indent * m_indentSpaces) space characters.
    */
    void RenderNewline();
};

/*
Renders the given value as an unsigned decimal integer, e.g. "123".
Returns the number of characters written, not counting the nul-termination.
*/
unsigned JsonRenderUInt(long long unsigned n, _Out_writes_z_(21) char* pBuffer) throw();

/*
Renders the given value as a signed decimal integer, e.g. "-123".
Returns the number of characters written, not counting the nul-termination.
*/
unsigned JsonRenderInt(long long signed n, _Out_writes_z_(21) char* pBuffer) throw();

/*
Renders the given value as a signed floating-point, e.g. "-123.1", or "null"
if the value is not finite.
Returns the number of characters written, not counting the nul-termination.
*/
unsigned JsonRenderFloat(double n, _Out_writes_z_(32) char* pBuffer) throw();

/*
Renders the string "true" or "false".
Returns the number of characters written, not counting the nul-termination.
(Always returns 4 or 5.)
*/
unsigned JsonRenderBool(bool b, _Out_writes_z_(6) char* pBuffer) throw();

/*
Renders the string "null".
Returns the number of characters written, not counting the nul-termination.
(Always returns 4.)
*/
unsigned JsonRenderNull(_Out_writes_z_(5) char* pBuffer) throw();

/*
Renders the given date/time value as an ISO 8601 string, e.g.
"2015-04-02T02:09:14.7927652Z".
Returns the number of characters written, not counting the nul-termination.
(Always returns 28.)
*/
unsigned JsonRenderTime(
    TimeStruct t,
    _Out_writes_z_(29) char* pBuffer) throw();

/*
Renders the given time_point value as an ISO 8601 string, e.g.
"2015-04-02T02:09:14.7927652Z".
Returns the number of characters written, not counting the nul-termination.
(Always returns 28.)
*/
unsigned JsonRenderTime(
    std::chrono::system_clock::time_point t,
    _Out_writes_z_(29) char* pBuffer) throw();

/*
Renders the given big-endian uuid_t value as a string in uppercase without
braces, e.g. "CD8D0A5E-6409-4B8E-9366-B815CEF0E35D".
Returns the number of characters written, not counting the nul-termination.
(Always returns 36.)
*/
unsigned JsonRenderUuid(_In_reads_(16) char unsigned const* g, _Out_writes_z_(37) char* pBuffer) throw();

/*
Renders the given big-endian uuid_t value as a string in uppercase with braces,
e.g. "{CD8D0A5E-6409-4B8E-9366-B815CEF0E35D}".
Returns the number of characters written, not counting the nul-termination.
(Always returns 38.)
*/
unsigned JsonRenderUuidWithBraces(_In_reads_(16) char unsigned const* g, _Out_writes_z_(39) char* pBuffer) throw();

}  // namespace jsonbuilder
