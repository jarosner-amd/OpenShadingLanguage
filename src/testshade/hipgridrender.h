// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "simplerend.h"

OSL_NAMESPACE_BEGIN

class HipRuntime;

// testshade's grid on an AMD GPU (testshade --amdgpu-run): the shader group
// of the first ShaderGroupRef runs once for every point of the grid, a GPU
// thread each, and the outputs land in the output images as on the CPU. The
// ShadingSystem must have gpu_backend "amdgpu", the architecture that run()
// returns, and output placement: the outputs' symbol locations are offsets
// into one device buffer (output_offset()). Needs a build with
// OSL_ENABLE_AMDGPU; printf, error and warning messages are dropped on the
// GPU, and there is no userdata.
class HipGridRenderer final : public SimpleRenderer {
public:
    HipGridRenderer();
    ~HipGridRenderer() override;

    // Load the HIP runtime and pick the first AMD GPU with an architecture
    // this build has renderer services for. Returns its architecture, or
    // an empty string and sets `error`.
    std::string run(std::string& error);

    // Fills the ShaderGlobals of grid point (x, y), as for the CPU. The
    // pointers in it are host pointers; render() replaces them with device
    // ones.
    using GlobalsSetup = std::function<void(ShaderGlobals&, int x, int y)>;
    void set_globals_setup(GlobalsSetup setup) { m_setup = std::move(setup); }

    bool add_output(string_view varname, string_view filename,
                    TypeDesc datatype, int nchannels) override;
    // Where output `index` starts in the device output buffer.
    size_t output_offset(size_t index) const
    {
        return index < m_output_offsets.size() ? m_output_offsets[index] : 0;
    }

    // Make the GPU code object of the (already optimized) group and set up
    // the device memory.
    void prepare_render() override;
    void warmup() override;
    void render(int xres, int yres) override;
    void finalize_pixel_buffer() override;

    // False once anything failed; the error went to errhandler().
    bool ok() const { return m_ok; }

    void* device_alloc(size_t size) override;
    void device_free(void* ptr) override;
    void* copy_to_device(void* dst_device, const void* src_host,
                         size_t size) override;

private:
    struct Device;

    bool fail(string_view message);
    bool build_module(const std::vector<char>& artifact,
                      const std::string& fused);
    bool init_device();

    std::unique_ptr<HipRuntime> m_hip;
    std::unique_ptr<Device> m_dev;
    std::string m_arch;
    GlobalsSetup m_setup;
    std::vector<size_t> m_output_offsets;
    size_t m_outputs_size = 0;
    bool m_rendered       = false;
    bool m_ok             = true;
};

OSL_NAMESPACE_END
