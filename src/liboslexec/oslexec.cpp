// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#include <cstdio>
#include <string>
#include <vector>

#include <OpenImageIO/strutil.h>
#include <OpenImageIO/thread.h>

#include "oslexec_pvt.h"
#include "osoreader.h"



OSL_NAMESPACE_BEGIN

namespace pvt {  // OSL::pvt


string_view
shadertypename(ShaderType s)
{
    switch (s) {
    case ShaderType::Generic: return ("shader");
    case ShaderType::Surface: return ("surface");
    case ShaderType::Displacement: return ("displacement");
    case ShaderType::Volume: return ("volume");
    case ShaderType::Light: return ("light");
    default: OSL_DASSERT(0 && "Invalid shader type"); return "unknown";
    }
}



ShaderType
shadertype_from_name(string_view name)
{
    if (name == "shader" || name == "generic")
        return ShaderType::Generic;
    if (name == "surface")
        return ShaderType::Surface;
    if (name == "displacement")
        return ShaderType::Displacement;
    if (name == "volume")
        return ShaderType::Volume;
    if (name == "light")
        return ShaderType::Light;
    return ShaderType::Unknown;
}



std::string
optix_cache_wrap(string_view ptx, size_t groupdata_size,
                 size_t groupdata_alignment)
{
    // Cache string is the ptx file with the groupdata size and alignment on
    // top as a comment. This way the cache string is a valid ptx program,
    // which can be useful for debugging. Both values have to be here: a cache
    // hit skips the backend that computes them, so anything not stored is
    // lost. The alignment was added after the size, which is why the reader
    // below tolerates its absence.
    return fmtformat("// {} {}\n{}", groupdata_size, groupdata_alignment, ptx);
}



void
optix_cache_unwrap(string_view cache_value, std::string& ptx,
                   size_t& groupdata_size, size_t& groupdata_alignment)
{
    groupdata_alignment        = 0;  // 0 means "not recorded"
    size_t groupdata_end_index = cache_value.find('\n');
    if (groupdata_end_index != std::string::npos) {
        constexpr int offset = 3;  // Account for the "// " prefix
        std::string groupdata_string
            = cache_value.substr(offset, groupdata_end_index - offset);
        // Header is "<size>" in blobs written before alignment was recorded
        // and "<size> <alignment>" after; stoll stops at the space either way.
        groupdata_size = std::stoll(groupdata_string);

        size_t sep = groupdata_string.find(' ');
        if (sep != std::string::npos) {
            // int because that is what stoi yields; widened below once it is
            // known good. Strutil::stoi does not throw on malformed input, it
            // returns 0 -- exactly the "unknown" value we want.
            int align = OIIO::Strutil::stoi(groupdata_string.substr(sep + 1));
            // An alignment is always a power of two. Anything else means a
            // truncated or corrupted blob, and this value would otherwise be
            // handed to a device allocator, so treat it as unknown instead.
            if (align > 0 && (align & (align - 1)) == 0)
                groupdata_alignment = (size_t)align;
        }

        ptx = cache_value.substr(groupdata_end_index + 1);
    }
}

};  // namespace pvt
OSL_NAMESPACE_END
