// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage


/////////////////////////////////////////////////////////////////////////
/// \file
///
/// getattribute() and interpolated (userdata) parameters on a GPU whose
/// shadeops reach the renderer only through the rs_* functions (AMD GPUs):
/// osl_get_attribute and osl_bind_interpolated_param call the renderer's
/// rs_get_attribute and rs_get_userdata. On the CPU these two shadeops are
/// in shadingsys.cpp and call RendererServices; CUDA renderers supply their
/// own.
///
/////////////////////////////////////////////////////////////////////////

#include <cstring>

#include "oslexec_pvt.h"
#include "shadeops_contract.h"
#include <OSL/rs_free_function.h>


OSL_NAMESPACE_BEGIN
namespace pvt {

#if OSL_GPU_DEVICE_COMPILE && OSL_SHADEOPS_RS_ONLY

// The size of a value of type t, in bytes. TypeDesc::size() is host only in
// OpenImageIO, so this is the same computation for the device.
static OSL_HOSTDEVICE int
type_size(TypeDesc t)
{
    int base = 0;
    switch (t.basetype) {
    case TypeDesc::UINT8:
    case TypeDesc::INT8: base = 1; break;
    case TypeDesc::UINT16:
    case TypeDesc::INT16:
    case TypeDesc::HALF: base = 2; break;
    case TypeDesc::UINT32:
    case TypeDesc::INT32:
    case TypeDesc::FLOAT: base = 4; break;
    case TypeDesc::UINT64:
    case TypeDesc::INT64:
    case TypeDesc::DOUBLE:
    case TypeDesc::STRING:  // a ustringhash
    case TypeDesc::PTR: base = 8; break;
    default: break;
    }
    return int(t.numelements()) * t.aggregate * base;
}



OSL_SHADEOP OSL_HOSTDEVICE int
osl_get_attribute(OpaqueExecContextPtr oec, int dest_derivs,
                  ustringhash_pod obj_name_, ustringhash_pod attr_name_,
                  int array_lookup, int index, long long attr_type,
                  void* attr_dest)
{
    return rs_get_attribute(oec, ustringhash_from(obj_name_),
                            ustringhash_from(attr_name_), TYPEDESC(attr_type),
                            dest_derivs, array_lookup ? index : -1, attr_dest);
}



// As the CPU version in shadingsys.cpp, with rs_get_userdata in place of
// RendererServices::get_userdata, and without the statistics.
OSL_SHADEOP OSL_HOSTDEVICE int
osl_bind_interpolated_param(OpaqueExecContextPtr oec, ustringhash_pod name_,
                            long long type, int userdata_has_derivs,
                            void* userdata_data, int /*symbol_has_derivs*/,
                            void* symbol_data, int symbol_data_size,
                            char* userdata_initialized, int /*userdata_index*/)
{
    char status = *userdata_initialized;
    if (status == 0) {
        // First time retrieving this userdata
        bool ok = rs_get_userdata(oec, ustringhash_from(name_), TYPEDESC(type),
                                  userdata_has_derivs, userdata_data);
        *userdata_initialized = status = 1 + ok;  // 1 = not found, 2 = found
    }
    if (status == 2) {
        // If userdata was present, copy it to the shader variable. A string
        // is a ustringhash, so it copies as plain bytes too.
        int udata_size = (userdata_has_derivs ? 3 : 1)
                         * type_size(TYPEDESC(type));
        memcpy(symbol_data, userdata_data,
               symbol_data_size < udata_size ? symbol_data_size : udata_size);
        if (symbol_data_size > udata_size)
            memset((char*)symbol_data + udata_size, 0,
                   symbol_data_size - udata_size);
        return 1;
    }
    return 0;  // no such user data
}

#endif

}  // namespace pvt
OSL_NAMESPACE_END
