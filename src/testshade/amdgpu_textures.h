// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#pragma once

#include <mutex>
#include <unordered_map>

#include <OpenImageIO/errorhandler.h>

#include <OSL/oslconfig.h>

OSL_NAMESPACE_BEGIN

class HipRuntime;

// The textures of shaders that run on an AMD GPU (testshade and testrender
// --amdgpu-run): each file is loaded once, into HIP texture objects and a
// device GpuTexture (render_state.h), whose device address is the shader's
// texture handle. The texture objects filter like the GPU's texture units,
// not like OpenImageIO's texture system.
class AmdgpuTextures {
public:
    AmdgpuTextures(HipRuntime& hip, OIIO::ErrorHandler& errhandler);
    ~AmdgpuTextures();

    // The device GpuTexture of the file, loaded at the first call (the
    // texture system says which way is up for lat-long maps); null, after
    // an error, if the file cannot be read or copied to the GPU.
    void* handle(ustring filename, TextureSystem* texturesys);

private:
    struct Record {
        void* object     = nullptr;  // hipTextureObject_t
        void* env_object = nullptr;  // the same texels, as a lat-long map
        void* device     = nullptr;  // the GpuTexture
    };
    Record load(ustring filename, TextureSystem* texturesys);

    HipRuntime& m_hip;
    OIIO::ErrorHandler& m_errhandler;
    std::unordered_map<ustring, Record> m_textures;
    std::mutex m_mutex;
};

OSL_NAMESPACE_END
