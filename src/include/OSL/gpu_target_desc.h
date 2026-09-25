// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <OSL/oslconfig.h>



OSL_NAMESPACE_BEGIN

/// Which GPU backend a shader group is being compiled for. `None` means
/// the ordinary CPU JIT path -- no GPU compilation is happening at all.
enum class GPUBackendKind { None, NVPTX, AMDGPU };

/// The concrete form a compiled GPU artifact takes. This is independent of
/// `GPUBackendKind`: for example AMDGPU's first milestone always produces
/// `LLVMBitcode`, but a later milestone might produce `HSACO` instead.
enum class GPUArtifactKind { None, PTX, LLVMBitcode, LLVMIR, HSACO };

/// Describes the GPU target a shader group should be compiled for: which
/// backend, what kind of artifact to emit, and the LLVM-level target
/// details (triple, CPU, features, data layout) needed to configure a
/// module for that target. Replaces the single `use_optix()` boolean that
/// OSL previously used to mean "compiling for a GPU".
struct GPUTargetDesc {
    GPUBackendKind backend   = GPUBackendKind::None;
    GPUArtifactKind artifact = GPUArtifactKind::None;
    std::string triple;
    std::string cpu;
    std::string features;
    std::string data_layout;
    /// One entry per requested architecture, e.g. {"gfx1100", "gfx1201"}
    /// for AMDGPU, or {"sm_70"} for NVPTX. A separate artifact is produced
    /// per architecture -- see `CompiledGPUArtifact::arch`.
    std::vector<std::string> archs;
    /// Whether the artifact is compiled with relocatable device code
    /// (`-fgpu-rdc`). Must be applied consistently across every device
    /// artifact class that gets linked together, or the final link fails
    /// on a relocation-model mismatch.
    bool rdc             = false;
    int code_obj_version = 5;
    bool enable_cache    = false;
};

/// What role a single exported symbol plays within a compiled GPU artifact.
/// Entry-point ABI differs enough between backends (and may require more
/// than one exported symbol per shader group) that a single "entry_name"
/// field is not enough to describe it.
enum class GPUExportKind { Init, EntryLayer, FusedEntry };

/// One exported symbol inside a `CompiledGPUArtifact`: which kind of entry
/// point it is, which shader layer it corresponds to (if any), and the
/// mangled symbol name a renderer would look up to call it.
struct GPUExportedSymbol {
    GPUExportKind kind = GPUExportKind::EntryLayer;
    std::string layer_name;
    std::string symbol_name;
};

/// A single compiled GPU artifact for one shader group and one target
/// architecture. `ShaderGroup` stores a vector of these -- one per
/// requested architecture -- replacing the old PTX-only storage model.
/// The binary payload is opaque to callers; its format is determined by
/// `artifact`.
struct CompiledGPUArtifact {
    GPUBackendKind backend   = GPUBackendKind::None;
    GPUArtifactKind artifact = GPUArtifactKind::None;
    std::string triple;
    /// The single architecture this artifact was compiled for, e.g.
    /// "gfx1100". Corresponds to one entry of the `GPUTargetDesc::archs`
    /// that produced it.
    std::string arch;
    /// The LLVM version string used to produce this artifact, recorded
    /// because LLVM bitcode is backward- but not forward-compatible: a
    /// consumer needs to know whether its own LLVM can read this payload.
    std::string llvm_version;
    bool rdc = false;
    std::vector<GPUExportedSymbol> exports;
    std::vector<uint8_t> payload;
};

OSL_NAMESPACE_END
