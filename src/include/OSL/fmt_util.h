// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#pragma once

#include <OSL/rs_free_function.h>

// OSL library functions as well as Renderer Service free functions
// can utilize OSL::filefmt, OSL::printfmt, OSL::errorfmt, and
// OSL::warningfmt wrappers to accept variable arguments to be
// encoded into a EncodeType array and PackedArgument buffer
// that underlying render service free functions require.
// NOTE:  All strings arguments to the underlying render service free
// functions must be ustringhash.  To make this easier, these wrappers
// automatically convert ustring and const char * to ustringhash.

OSL_NAMESPACE_BEGIN

namespace pvt {
// PackedArgs is similar to tuple but packs its data back to back
// in memory layout, which is what we need to build up payload
// to the fmt reporting system
OSL_PACK_STRUCTS_BEGIN
template<int IndexT, typename TypeT> struct alignas(1) PackedArg {
    OSL_HOSTDEVICE explicit PackedArg(const TypeT& a_value) : m_value(a_value)
    {
    }
    TypeT m_value;
};
OSL_PACK_STRUCTS_END

template<typename IntSequenceT, typename... TypeListT> struct PackedArgsBase;
// Specialize to extract a parameter pack of the IntegerSquence
// so it can be expanded alongside the TypeListT parameter pack
OSL_PACK_STRUCTS_BEGIN
template<int... IntegerListT, typename... TypeListT>
struct alignas(1)
    PackedArgsBase<std::integer_sequence<int, IntegerListT...>, TypeListT...>
    : public PackedArg<IntegerListT, TypeListT>... {
    OSL_HOSTDEVICE explicit PackedArgsBase(const TypeListT&... a_values)
        // multiple inheritance of individual components
        // uniquely identified by the <Integer,Type> combo
        : PackedArg<IntegerListT, TypeListT>(a_values)...
    {
    }
};
OSL_PACK_STRUCTS_END

template<typename... TypeListT> struct PackedArgs {
    typedef std::make_integer_sequence<int, sizeof...(TypeListT)>
        IndexSequenceType;
    PackedArgsBase<IndexSequenceType, TypeListT...> m_components;

    OSL_HOSTDEVICE explicit PackedArgs(const TypeListT&... a_values)
        : m_components(a_values...)
    {
    }
};

static_assert(sizeof(PackedArgs<int, char, int>)
                  == sizeof(int) + sizeof(char) + sizeof(int),
              "PackedArgs<> type is not packed");
static_assert(alignof(PackedArgs<int, char, int>) == 1,
              "PackedArgs<> type is not aligned to 1");

// The hash of a format specification or file name. A character array is
// measured without strlen, which a GPU device does not have: up to its
// first zero, so that a string literal and a buffer holding a shorter
// string both hash like the same text as a ustring. For a literal the loop
// folds away at compile time.
template<typename T>
OSL_HOSTDEVICE inline OSL::ustringhash
fmt_spec_hash(const T& spec)
{
    return OSL::ustringhash { spec };
}

template<size_t N>
OSL_HOSTDEVICE inline OSL::ustringhash
fmt_spec_hash(const char (&spec)[N])
{
    size_t len = 0;
    while (len < N && spec[len] != '\0')
        ++len;
    return OSL::ustringhash(spec, len);
}

}  // namespace pvt



template<typename FilenameT, typename SpecifierT, typename... ArgListT>
OSL_HOSTDEVICE void
filefmt(OpaqueExecContextPtr oec, const FilenameT& filename_hash,
        const SpecifierT& fmt_specification, ArgListT... args)
{
    constexpr int32_t count = sizeof...(args);
    constexpr OSL::EncodedType argTypes[]
        = { pvt::TypeEncoder<ArgListT>::value... };
    pvt::PackedArgs<typename pvt::TypeEncoder<ArgListT>::DataType...> argValues {
        pvt::TypeEncoder<ArgListT>::Encode(args)...
    };

    rs_filefmt(oec, pvt::fmt_spec_hash(filename_hash),
               pvt::fmt_spec_hash(fmt_specification), count,
               (count == 0) ? nullptr : argTypes,
               static_cast<uint32_t>((count == 0) ? 0 : sizeof(argValues)),
               (count == 0) ? nullptr : reinterpret_cast<uint8_t*>(&argValues));
}

template<typename SpecifierT, typename... ArgListT>
OSL_HOSTDEVICE void
printfmt(OpaqueExecContextPtr oec, const SpecifierT& fmt_specification,
         ArgListT... args)
{
    constexpr int32_t count = sizeof...(args);
    constexpr OSL::EncodedType argTypes[]
        = { pvt::TypeEncoder<ArgListT>::value... };
    pvt::PackedArgs<typename pvt::TypeEncoder<ArgListT>::DataType...> argValues {
        pvt::TypeEncoder<ArgListT>::Encode(args)...
    };

    rs_printfmt(oec, pvt::fmt_spec_hash(fmt_specification), count,
                (count == 0) ? nullptr : argTypes,
                static_cast<uint32_t>((count == 0) ? 0 : sizeof(argValues)),
                (count == 0) ? nullptr
                             : reinterpret_cast<uint8_t*>(&argValues));
}

template<typename SpecifierT, typename... ArgListT>
OSL_HOSTDEVICE void
errorfmt(OpaqueExecContextPtr oec, const SpecifierT& fmt_specification,
         ArgListT... args)
{
    constexpr int32_t count = sizeof...(args);
    constexpr OSL::EncodedType argTypes[]
        = { pvt::TypeEncoder<ArgListT>::value... };
    pvt::PackedArgs<typename pvt::TypeEncoder<ArgListT>::DataType...> argValues {
        pvt::TypeEncoder<ArgListT>::Encode(args)...
    };

    rs_errorfmt(oec, pvt::fmt_spec_hash(fmt_specification), count,
                (count == 0) ? nullptr : argTypes,
                static_cast<uint32_t>((count == 0) ? 0 : sizeof(argValues)),
                (count == 0) ? nullptr
                             : reinterpret_cast<uint8_t*>(&argValues));
}

template<typename SpecifierT, typename... ArgListT>
OSL_HOSTDEVICE void
warningfmt(OpaqueExecContextPtr oec, const SpecifierT& fmt_specification,
           ArgListT... args)
{
    constexpr int32_t count = sizeof...(args);
    constexpr OSL::EncodedType argTypes[]
        = { pvt::TypeEncoder<ArgListT>::value... };
    pvt::PackedArgs<typename pvt::TypeEncoder<ArgListT>::DataType...> argValues {
        pvt::TypeEncoder<ArgListT>::Encode(args)...
    };

    rs_warningfmt(oec, pvt::fmt_spec_hash(fmt_specification), count,
                  (count == 0) ? nullptr : argTypes,
                  static_cast<uint32_t>((count == 0) ? 0 : sizeof(argValues)),
                  (count == 0) ? nullptr
                               : reinterpret_cast<uint8_t*>(&argValues));
}


OSL_NAMESPACE_END
