// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

// The only file that includes a HART SDK header. Built with OSL_USE_HART.

// The HART header comes first. It includes the HIP runtime API header, which
// MSVC parses only before OSL's and OIIO's headers: after them it fails in
// amd_hip_vector_types.h.
// clang-format off
#include <amd/hart/hart_device.h>
// clang-format on

#include "hart_marker.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>

OSL_NAMESPACE_BEGIN
namespace pvt {

uint64_t
hart_storage_abi()
{
    return amd::hart::detail::kDeviceStorageAbi;
}



void
add_hart_storage_marker(llvm::Module& module)
{
    const char* name = amd::hart::detail::kDeviceStorageAbiMarkerName;
    if (module.getNamedGlobal(name))
        return;
    // The same global that hart_device.h makes in device code: an internal
    // 64-bit constant in the constant address space, kept alive through
    // llvm.compiler.used. HART checks it and removes it before linking.
    const unsigned constant_addrspace = 4;
    llvm::Type* i64 = llvm::Type::getInt64Ty(module.getContext());
    auto* marker    = new llvm::GlobalVariable(
        module, i64, /*isConstant=*/true, llvm::GlobalValue::InternalLinkage,
        llvm::ConstantInt::get(i64, hart_storage_abi()), name, nullptr,
        llvm::GlobalVariable::NotThreadLocal, constant_addrspace);
    marker->setAlignment(llvm::Align(8));
    llvm::appendToCompilerUsed(module, { marker });
}

}  // namespace pvt
OSL_NAMESPACE_END
