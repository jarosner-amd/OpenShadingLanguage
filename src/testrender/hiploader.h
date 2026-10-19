// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <OSL/oslconfig.h>

OSL_NAMESPACE_BEGIN

// The parts of the HIP runtime and of hiprtc that testrender uses, loaded
// at run time, so that testrender needs neither to build or to start. The
// libraries are found through the environment variables
// OSL_HIP_RUNTIME_LIBRARY and OSL_HIPRTC_LIBRARY, or else by their usual
// names. Only hiploader.cpp sees the HIP headers: they clash with
// testrender's own (both define float3), so this interface uses plain
// types. On failure, a call returns false or null and error() says why.
class HipRuntime {
public:
    HipRuntime();
    ~HipRuntime();

    bool load();
    const std::string& error() const { return m_error; }

    int device_count();
    // The device's name and architecture, without feature suffixes
    // ("gfx1201", not "gfx1201:xnack-").
    bool device_info(int device, std::string& name, std::string& arch);
    bool set_device(int device);

    void* alloc(size_t size);
    void free(void* device_ptr);
    bool to_device(void* dst, const void* src, size_t size);
    bool to_host(void* dst, const void* src, size_t size);
    bool fill(void* dst, int value, size_t size);

    // Compile HIP source for `arch` to relocatable LLVM bitcode.
    bool compile(const std::string& source, const std::string& name,
                 const std::string& arch, std::vector<char>& bitcode);
    // Link LLVM bitcode modules into a code object.
    bool link(const std::vector<string_view>& inputs,
              std::vector<char>& code_object);
    void* load_module(const std::vector<char>& code_object);
    void unload_module(void* module);
    void* kernel(void* module, const char* name);
    // Run a kernel with one thread and wait for it.
    bool launch_one(void* kernel, void** args);

private:
    struct Api;
    std::unique_ptr<Api> m_api;
    std::string m_error;

    bool check(int result, const char* what);
};

OSL_NAMESPACE_END
