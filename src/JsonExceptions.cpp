#include <jsonbuilder/JsonBuilder.h>
namespace jsonbuilder
{
    [[noreturn]] void _jsonbuilderDecl JsonThrowBadAlloc() noexcept(false)
    {
        throw std::bad_alloc();
    }
    [[noreturn]] void _jsonbuilderDecl JsonThrowLengthError(_In_z_ const char* what) noexcept(false)
    {
        throw std::length_error(what);
    }
    [[noreturn]] void _jsonbuilderDecl JsonThrowInvalidArgument(_In_z_ const char* what) noexcept(false)
    {
        throw std::invalid_argument(what);
    }
}
