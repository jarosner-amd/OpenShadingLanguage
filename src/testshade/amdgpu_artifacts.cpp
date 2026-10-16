// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#include "amdgpu_artifacts.h"

#include <cctype>
#include <iostream>
#include <string>
#include <vector>

#include <OpenImageIO/filesystem.h>

#include <OSL/gpu_target_desc.h>

OSL_NAMESPACE_BEGIN

bool
report_amdgpu_artifacts(ShadingSystem& ss, ShaderGroup& group,
                        string_view stem_in, bool save, int& num_artifacts)
{
    bool ok       = true;
    num_artifacts = 0;
    ss.getattribute(&group, "gpu_num_artifacts", num_artifacts);

    std::string stem(stem_in);
    for (char& c : stem) {
        if (!(std::isalnum((unsigned char)c) || c == '.' || c == '-'))
            c = '_';
    }

    for (int i = 0; i < num_artifacts; ++i) {
        std::string prefix = fmtformat("gpu_artifact:{}:", i);
        ustring arch, triple;
        int kind = 0, num_exports = 0, size = 0;
        bool got
            = ss.getattribute(&group, prefix + "arch", TypeString, &arch)
              && ss.getattribute(&group, prefix + "triple", TypeString, &triple)
              && ss.getattribute(&group, prefix + "kind", kind)
              && ss.getattribute(&group, prefix + "num_exports", num_exports)
              && ss.getattribute(&group, prefix + "size", size);
        if (!got) {
            std::cerr << "ERROR: Could not query AMDGPU artifact " << i << "\n";
            ok = false;
            continue;
        }
        const bool is_ir = (kind == int(GPUArtifactKind::LLVMIR));
        print("AMDGPU artifact {}: arch={} triple={} format={} exports={}\n", i,
              arch.empty() ? ustring("generic") : arch, triple,
              is_ir ? "llvmir" : "bitcode", num_exports);
        for (int m = 0; m < num_exports; ++m) {
            std::string eprefix = fmtformat("{}export:{}:", prefix, m);
            ustring ekind, elayer, esymbol;
            if (!ss.getattribute(&group, eprefix + "kind", TypeString, &ekind)
                || !ss.getattribute(&group, eprefix + "layer", TypeString,
                                    &elayer)
                || !ss.getattribute(&group, eprefix + "symbol", TypeString,
                                    &esymbol)) {
                std::cerr << "ERROR: Could not query export " << m
                          << " of AMDGPU artifact " << i << "\n";
                ok = false;
                continue;
            }
            print("  export {}: kind={} layer={} symbol={}\n", m, ekind,
                  elayer.empty() ? ustring("-") : elayer, esymbol);
        }

        if (!save)
            continue;
        std::vector<unsigned char> data(size_t(size > 0 ? size : 0));
        if (!ss.getattribute(&group, prefix + "data",
                             TypeDesc(TypeDesc::UINT8, int(size)),
                             data.data())) {
            std::cerr << "ERROR: Could not read the data of AMDGPU artifact "
                      << i << "\n";
            ok = false;
            continue;
        }
        std::string filename = fmtformat("amdgpu_{}_{}.{}", stem,
                                         arch.empty() ? "generic"
                                                      : arch.string(),
                                         is_ir ? "ll" : "bc");
        if (!OIIO::Filesystem::write_binary_file(filename, data)) {
            std::cerr << "ERROR: Could not write " << filename << "\n";
            ok = false;
            continue;
        }
        print("Wrote {}\n", filename);
    }
    return ok;
}

OSL_NAMESPACE_END
