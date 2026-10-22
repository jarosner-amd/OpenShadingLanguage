// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "simpleraytracer.h"

OSL_NAMESPACE_BEGIN

class HipRuntime;

// The AMDGPU side of testrender. Without run(), it compiles every shader
// group of the scene for AMDGPU (the ShadingSystem must have gpu_backend
// "amdgpu") and reports, and optionally saves, the artifacts; nothing is
// launched. With run() (needs a build with OSL_ENABLE_AMDGPU), the scene
// renders as on the CPU, but every shader runs on an AMD GPU: one launch
// per shading call, the closures copied back for the CPU to evaluate.
class HipRaytracer final : public SimpleRaytracer {
public:
    HipRaytracer();
    ~HipRaytracer() override;

    // Write each artifact to amdgpu_<material>_<group>_<arch>.bc or .ll.
    void save_artifacts(bool save) { m_save = save; }

    // Render on the GPU. Loads the HIP runtime and picks the first AMD GPU
    // whose architecture this build has renderer services for; returns
    // that architecture (for gpu_archs), or an empty string on failure,
    // with the reason in `error`.
    std::string run(std::string& error);

    // Compile the scene's shader groups. With run(), also make a GPU code
    // object for each, then prepare the scene as SimpleRaytracer does
    // (which runs shaders, through execute_group).
    void prepare_render() override;
    void warmup() override {}
    void render(int xres, int yres) override;

    bool execute_group(ShadingContext& ctx, ShaderGroup& group,
                       ShaderGlobals& sg, ShadeKind kind) override;
    // One launch with a thread per point, for the independent calls of the
    // background's importance table.
    bool execute_group_batch(
        ShadingContext& ctx, ShaderGroup& group, ShaderGlobals* sgs, int n,
        ShadeKind kind,
        const std::function<void(int, ShaderGlobals&)>& done) override;

    // With run(): a GPU texture for the file (made once, kept until the end),
    // or null if it cannot be read, which the device treats as a missing
    // texture. Otherwise as SimpleRaytracer.
    using SimpleRaytracer::get_texture_handle;
    TextureHandle* get_texture_handle(ustring filename, ShadingContext* context,
                                      const TextureOpt* options) override;

    // GPU memory for OSL's interactive parameters.
    void* device_alloc(size_t size) override;
    void device_free(void* ptr) override;
    void* copy_to_device(void* dst_device, const void* src_host,
                         size_t size) override;

    // False if a group failed to compile or produced no artifact, or the
    // GPU failed.
    bool ok() const { return m_ok; }
    bool running() const { return m_run; }

private:
    struct GpuGroup;
    struct Device;

    bool compile_group(ShaderGroup& group, string_view stem);
    bool build_module(ShaderGroup& group, const std::vector<char>& artifact,
                      const std::string& fused);
    bool relocate_closures(char* pool, const char* device_base,
                           ClosureColor*& ci);
    bool init_device();
    bool fail(string_view message);
    // A texture on the GPU: the device GpuTexture (the handle OSL passes
    // on) and the two texture objects it refers to.
    struct TextureRecord {
        void* device     = nullptr;
        void* object     = nullptr;
        void* env_object = nullptr;
    };
    TextureRecord load_texture(ustring filename);

    bool m_save = false;
    bool m_ok   = true;
    bool m_run  = false;
    std::string m_arch;
    std::unique_ptr<HipRuntime> m_hip;
    std::unique_ptr<Device> m_dev;
    std::unordered_map<const ShaderGroup*, std::unique_ptr<GpuGroup>> m_groups;
    std::mutex m_mutex;  // one GPU shading call at a time
    // Per file: the device GpuTexture (the handle) and its texture object.
    std::unordered_map<ustring, TextureRecord> m_textures;
    std::mutex m_textures_mutex;
};

OSL_NAMESPACE_END
