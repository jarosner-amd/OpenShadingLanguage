// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#pragma once

#include <cstdint>

#include <OSL/oslconfig.h>

namespace llvm {
class Module;
}

OSL_NAMESPACE_BEGIN
namespace pvt {

// HART refuses a shader module unless it holds a constant named
// __hart_device_storage_abi_v1 whose value matches the HART version that
// links it. These two functions exist only with OSL_USE_HART.

// The marker value of the HART headers OSL was built with.
uint64_t
hart_storage_abi();

// Add the marker to `module`, unless it already has one.
void
add_hart_storage_marker(llvm::Module& module);

}  // namespace pvt
OSL_NAMESPACE_END
