// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#include "hipraytracer.h"

#include <OSL/oslexec.h>

#include "../testshade/amdgpu_artifacts.h"

OSL_NAMESPACE_BEGIN

bool
HipRaytracer::compile_group(ShaderGroup& group, string_view stem)
{
    PerThreadInfo* thread_info = shadingsys->create_thread_info();
    ShadingContext* ctx        = shadingsys->get_context(thread_info);
    shadingsys->optimize_group(&group, ctx);
    // Compile errors are held by the context until it is released.
    shadingsys->release_context(ctx);
    shadingsys->destroy_thread_info(thread_info);
    if (had_error())
        return false;

    int num_artifacts = 0;
    bool ok = report_amdgpu_artifacts(*shadingsys, group, stem, m_save,
                                      num_artifacts);
    if (num_artifacts < 1) {
        errhandler().errorfmt("No AMDGPU artifact for shader group {}", stem);
        ok = false;
    }
    return ok;
}



void
HipRaytracer::prepare_render()
{
    // Unlike SimpleRaytracer::prepare_render, nothing here runs a shader:
    // the groups are compiled for AMDGPU, which this process cannot run.
    for (size_t i = 0; i < shaders().size(); ++i) {
        const Material& m = shaders()[i];
        for (const ShaderGroupRef& g : { m.surf, m.disp }) {
            if (!g)
                continue;
            ustring name;
            shadingsys->getattribute(g.get(), "groupname", name);
            print("Material {} group \"{}\":\n", i, name);
            if (!compile_group(*g, fmtformat("{}_{}", i, name)))
                m_ok = false;
        }
    }
}

OSL_NAMESPACE_END
