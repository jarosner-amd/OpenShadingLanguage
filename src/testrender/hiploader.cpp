// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

// The HIP headers come first: OSL, OpenImageIO and Imath define
// __has_attribute(x) as 0 for MSVC, and after that the HIP headers take a
// clang-only path.
#include <hip/hip_runtime_api.h>
#include <hip/hiprtc.h>

#include "hiploader.h"

#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/plugin.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/sysutil.h>

OSL_NAMESPACE_BEGIN

// The entry points, typed from the HIP headers. hipMalloc has template
// overloads in C++, so its type is spelled out; hipGetDeviceProperties is
// a macro for the versioned entry point.
struct HipRuntime::Api {
    hipError_t (*Malloc)(void**, size_t)                     = nullptr;
    hipError_t (*GetDeviceProperties)(hipDeviceProp_t*, int) = nullptr;
    decltype(&::hipFree) Free                                = nullptr;
    decltype(&::hipMemcpy) Memcpy                            = nullptr;
    decltype(&::hipMemset) Memset                            = nullptr;
    decltype(&::hipGetDeviceCount) GetDeviceCount            = nullptr;
    decltype(&::hipSetDevice) SetDevice                      = nullptr;
    decltype(&::hipDeviceSynchronize) DeviceSynchronize      = nullptr;
    decltype(&::hipGetErrorString) GetErrorString            = nullptr;
    decltype(&::hipModuleLoadData) ModuleLoadData            = nullptr;
    decltype(&::hipModuleUnload) ModuleUnload                = nullptr;
    decltype(&::hipModuleGetFunction) ModuleGetFunction      = nullptr;
    decltype(&::hipModuleLaunchKernel) ModuleLaunchKernel    = nullptr;

    // Textures.
    decltype(&::hipMallocMipmappedArray) MallocMipmappedArray     = nullptr;
    decltype(&::hipFreeMipmappedArray) FreeMipmappedArray         = nullptr;
    decltype(&::hipGetMipmappedArrayLevel) GetMipmappedArrayLevel = nullptr;
    decltype(&::hipMemcpy2DToArray) Memcpy2DToArray               = nullptr;
    decltype(&::hipCreateTextureObject) CreateTextureObject       = nullptr;
    decltype(&::hipDestroyTextureObject) DestroyTextureObject     = nullptr;

    decltype(&::hiprtcCreateProgram) CreateProgram         = nullptr;
    decltype(&::hiprtcCompileProgram) CompileProgram       = nullptr;
    decltype(&::hiprtcGetProgramLogSize) GetProgramLogSize = nullptr;
    decltype(&::hiprtcGetProgramLog) GetProgramLog         = nullptr;
    decltype(&::hiprtcGetBitcodeSize) GetBitcodeSize       = nullptr;
    decltype(&::hiprtcGetBitcode) GetBitcode               = nullptr;
    decltype(&::hiprtcDestroyProgram) DestroyProgram       = nullptr;
    decltype(&::hiprtcLinkCreate) LinkCreate               = nullptr;
    decltype(&::hiprtcLinkAddData) LinkAddData             = nullptr;
    decltype(&::hiprtcLinkComplete) LinkComplete           = nullptr;
    decltype(&::hiprtcLinkDestroy) LinkDestroy             = nullptr;
};



namespace {

#ifdef _WIN32
// The folders on PATH, and the bin folder of a HIP SDK install (HIP_PATH).
// Only Windows needs them, to find hiprtc by its versioned name.
std::vector<std::string>
search_folders()
{
    const char sep[] = ";";
    std::vector<std::string> folders;
    std::string hip_path = OIIO::Sysutil::getenv("HIP_PATH");
    if (!hip_path.empty())
        folders.push_back(hip_path + "/bin");
    for (string_view f :
         OIIO::Strutil::splitsv(OIIO::Sysutil::getenv("PATH"), sep))
        if (!f.empty())
            folders.emplace_back(f);
    return folders;
}
#endif

// Library names to try, in order: the environment variable, then the
// usual names. On Windows the hiprtc library carries the ROCm version in
// its name (hiprtc0714.dll), so any hiprtc*.dll in the search folders,
// other than the builtins library, is a candidate.
std::vector<std::string>
candidates(const char* envvar, bool hiprtc)
{
    std::vector<std::string> names;
    std::string env = OIIO::Sysutil::getenv(envvar);
    if (!env.empty())
        names.push_back(env);
#ifdef _WIN32
    if (!hiprtc) {
        names.push_back("amdhip64_7.dll");
        names.push_back("amdhip64_6.dll");
    } else {
        for (const std::string& folder : search_folders()) {
            std::vector<std::string> files;
            if (!OIIO::Filesystem::get_directory_entries(folder, files))
                continue;
            for (const std::string& f : files) {
                std::string name = OIIO::Filesystem::filename(f);
                if (OIIO::Strutil::istarts_with(name, "hiprtc")
                    && OIIO::Strutil::iends_with(name, ".dll")
                    && !OIIO::Strutil::icontains(name, "builtins"))
                    names.push_back(f);
            }
        }
    }
#else
    if (!hiprtc) {
        names.push_back("libamdhip64.so");
        names.push_back("libamdhip64.so.7");
        names.push_back("libamdhip64.so.6");
    } else {
        names.push_back("libhiprtc.so");
        names.push_back("libhiprtc.so.7");
        names.push_back("libhiprtc.so.6");
    }
#endif
    return names;
}

OIIO::Plugin::Handle
open_first(const char* envvar, bool hiprtc)
{
    for (const std::string& name : candidates(envvar, hiprtc)) {
        if (OIIO::Plugin::Handle h = OIIO::Plugin::open(name, false))
            return h;
        OIIO::Plugin::geterror();  // not this one: try the next
    }
    return nullptr;
}

template<typename F>
bool
get(OIIO::Plugin::Handle h, const char* name, F& fn, std::string& error)
{
    fn = reinterpret_cast<F>(OIIO::Plugin::getsym(h, name, false));
    if (!fn)
        error = OIIO::Strutil::fmt::format("{} is missing from the library",
                                           name);
    return fn != nullptr;
}

}  // namespace



HipRuntime::HipRuntime()  = default;
HipRuntime::~HipRuntime() = default;



bool
HipRuntime::load()
{
    OIIO::Plugin::Handle hip = open_first("OSL_HIP_RUNTIME_LIBRARY", false);
    if (!hip) {
        m_error = "cannot load the HIP runtime (set OSL_HIP_RUNTIME_LIBRARY "
                  "to its path)";
        return false;
    }
    OIIO::Plugin::Handle rtc = open_first("OSL_HIPRTC_LIBRARY", true);
    if (!rtc) {
        m_error = "cannot load hiprtc (set OSL_HIPRTC_LIBRARY to its path)";
        return false;
    }
    auto api       = std::make_unique<Api>();
    Api& a         = *api;
    std::string& e = m_error;
    bool ok = get(hip, "hipMalloc", a.Malloc, e)
              && get(hip, "hipGetDevicePropertiesR0600", a.GetDeviceProperties,
                     e)
              && get(hip, "hipFree", a.Free, e)
              && get(hip, "hipMemcpy", a.Memcpy, e)
              && get(hip, "hipMemset", a.Memset, e)
              && get(hip, "hipGetDeviceCount", a.GetDeviceCount, e)
              && get(hip, "hipSetDevice", a.SetDevice, e)
              && get(hip, "hipDeviceSynchronize", a.DeviceSynchronize, e)
              && get(hip, "hipGetErrorString", a.GetErrorString, e)
              && get(hip, "hipModuleLoadData", a.ModuleLoadData, e)
              && get(hip, "hipModuleUnload", a.ModuleUnload, e)
              && get(hip, "hipModuleGetFunction", a.ModuleGetFunction, e)
              && get(hip, "hipModuleLaunchKernel", a.ModuleLaunchKernel, e)
              && get(rtc, "hiprtcCreateProgram", a.CreateProgram, e)
              && get(rtc, "hiprtcCompileProgram", a.CompileProgram, e)
              && get(rtc, "hiprtcGetProgramLogSize", a.GetProgramLogSize, e)
              && get(rtc, "hiprtcGetProgramLog", a.GetProgramLog, e)
              && get(rtc, "hiprtcGetBitcodeSize", a.GetBitcodeSize, e)
              && get(rtc, "hiprtcGetBitcode", a.GetBitcode, e)
              && get(rtc, "hiprtcDestroyProgram", a.DestroyProgram, e)
              && get(rtc, "hiprtcLinkCreate", a.LinkCreate, e)
              && get(rtc, "hiprtcLinkAddData", a.LinkAddData, e)
              && get(rtc, "hiprtcLinkComplete", a.LinkComplete, e)
              && get(rtc, "hiprtcLinkDestroy", a.LinkDestroy, e);
    ok = ok && get(hip, "hipMallocMipmappedArray", a.MallocMipmappedArray, e)
         && get(hip, "hipFreeMipmappedArray", a.FreeMipmappedArray, e)
         && get(hip, "hipGetMipmappedArrayLevel", a.GetMipmappedArrayLevel, e)
         && get(hip, "hipMemcpy2DToArray", a.Memcpy2DToArray, e)
         && get(hip, "hipCreateTextureObject", a.CreateTextureObject, e)
         && get(hip, "hipDestroyTextureObject", a.DestroyTextureObject, e);
    if (ok)
        m_api = std::move(api);
    return ok;
}



bool
HipRuntime::check(int result, const char* what)
{
    if (result == hipSuccess)
        return true;
    m_error
        = OIIO::Strutil::fmt::format("{} failed: {}", what,
                                     m_api->GetErrorString(hipError_t(result)));
    return false;
}



int
HipRuntime::device_count()
{
    int count = 0;
    return check(m_api->GetDeviceCount(&count), "hipGetDeviceCount") ? count
                                                                     : 0;
}

bool
HipRuntime::device_info(int device, std::string& name, std::string& arch)
{
    hipDeviceProp_t props;
    if (!check(m_api->GetDeviceProperties(&props, device),
               "hipGetDeviceProperties"))
        return false;
    name = props.name;
    arch = props.gcnArchName;
    arch = arch.substr(0, arch.find(':'));
    return true;
}

bool
HipRuntime::set_device(int device)
{
    return check(m_api->SetDevice(device), "hipSetDevice");
}

void*
HipRuntime::alloc(size_t size)
{
    void* p = nullptr;
    return check(m_api->Malloc(&p, size), "hipMalloc") ? p : nullptr;
}

void
HipRuntime::free(void* device_ptr)
{
    if (device_ptr)
        m_api->Free(device_ptr);
}

bool
HipRuntime::to_device(void* dst, const void* src, size_t size)
{
    return check(m_api->Memcpy(dst, src, size, hipMemcpyHostToDevice),
                 "hipMemcpy to the device");
}

bool
HipRuntime::to_host(void* dst, const void* src, size_t size)
{
    return check(m_api->Memcpy(dst, src, size, hipMemcpyDeviceToHost),
                 "hipMemcpy to the host");
}

bool
HipRuntime::fill(void* dst, int value, size_t size)
{
    return check(m_api->Memset(dst, value, size), "hipMemset");
}



bool
HipRuntime::compile(const std::string& source, const std::string& name,
                    const std::string& arch, std::vector<char>& bitcode)
{
    hiprtcProgram prog;
    if (m_api->CreateProgram(&prog, source.c_str(), name.c_str(), 0, nullptr,
                             nullptr)
        != HIPRTC_SUCCESS) {
        m_error = "hiprtcCreateProgram failed";
        return false;
    }
    std::string archopt = "--gpu-architecture=" + arch;
    const char* opts[]  = { "-fgpu-rdc", archopt.c_str() };
    bool ok = m_api->CompileProgram(prog, 2, opts) == HIPRTC_SUCCESS;
    if (ok) {
        size_t size = 0;
        m_api->GetBitcodeSize(prog, &size);
        bitcode.resize(size);
        m_api->GetBitcode(prog, bitcode.data());
    } else {
        size_t size = 0;
        m_api->GetProgramLogSize(prog, &size);
        std::string log(size, '\0');
        m_api->GetProgramLog(prog, &log[0]);
        m_error = "hiprtc could not compile " + name + ": " + log;
    }
    m_api->DestroyProgram(&prog);
    return ok;
}

bool
HipRuntime::link(const std::vector<string_view>& inputs,
                 std::vector<char>& code_object)
{
    hiprtcLinkState state;
    if (m_api->LinkCreate(0, nullptr, nullptr, &state) != HIPRTC_SUCCESS) {
        m_error = "hiprtcLinkCreate failed";
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < inputs.size() && ok; ++i) {
        std::string name = OIIO::Strutil::fmt::format("input{}", i);
        ok = m_api->LinkAddData(state, HIPRTC_JIT_INPUT_LLVM_BITCODE,
                                const_cast<char*>(inputs[i].data()),
                                inputs[i].size(), name.c_str(), 0, nullptr,
                                nullptr)
             == HIPRTC_SUCCESS;
        if (!ok)
            m_error = "hiprtc could not read the bitcode of " + name;
    }
    void* code  = nullptr;
    size_t size = 0;
    if (ok && m_api->LinkComplete(state, &code, &size) != HIPRTC_SUCCESS) {
        m_error = "hiprtc could not link the code object";
        ok      = false;
    }
    if (ok)
        code_object.assign((const char*)code, (const char*)code + size);
    m_api->LinkDestroy(state);  // owns `code`
    return ok;
}

void*
HipRuntime::load_module(const std::vector<char>& code_object)
{
    hipModule_t module = nullptr;
    return check(m_api->ModuleLoadData(&module, code_object.data()),
                 "hipModuleLoadData")
               ? module
               : nullptr;
}

void
HipRuntime::unload_module(void* module)
{
    if (module)
        m_api->ModuleUnload(hipModule_t(module));
}

void*
HipRuntime::kernel(void* module, const char* name)
{
    hipFunction_t fn = nullptr;
    return check(m_api->ModuleGetFunction(&fn, hipModule_t(module), name),
                 "hipModuleGetFunction")
               ? fn
               : nullptr;
}

bool
HipRuntime::launch_one(void* kernel, void** args)
{
    return check(m_api->ModuleLaunchKernel(hipFunction_t(kernel), 1, 1, 1, 1, 1,
                                           1, 0, nullptr, args, nullptr),
                 "hipModuleLaunchKernel")
           && check(m_api->DeviceSynchronize(), "hipDeviceSynchronize");
}



static hipTextureAddressMode
address_mode(const std::string& wrap)
{
    if (wrap == "clamp")
        return hipAddressModeClamp;
    if (wrap == "periodic")
        return hipAddressModeWrap;
    if (wrap == "mirror")
        return hipAddressModeMirror;
    return hipAddressModeBorder;
}

bool
HipRuntime::create_textures(const std::vector<TextureLevel>& levels,
                            const std::string& wrap_s,
                            const std::string& wrap_t, void*& texture,
                            void*& env_texture)
{
    texture     = nullptr;
    env_texture = nullptr;
    if (levels.empty()) {
        m_error = "a texture needs at least one level";
        return false;
    }
    hipChannelFormatDesc channels = { 32, 32, 32, 32,
                                      hipChannelFormatKindFloat };
    hipMipmappedArray_t mipmaps   = nullptr;
    if (!check(m_api->MallocMipmappedArray(&mipmaps, &channels,
                                           make_hipExtent(levels[0].width,
                                                          levels[0].height, 0),
                                           unsigned(levels.size()), 0),
               "hipMallocMipmappedArray"))
        return false;
    for (size_t l = 0; l < levels.size(); ++l) {
        const TextureLevel& level = levels[l];
        hipArray_t array          = nullptr;
        size_t pitch              = size_t(level.width) * 4 * sizeof(float);
        if (!check(m_api->GetMipmappedArrayLevel(&array, mipmaps, unsigned(l)),
                   "hipGetMipmappedArrayLevel")
            || !check(m_api->Memcpy2DToArray(array, 0, 0, level.rgba.data(),
                                             pitch, pitch, size_t(level.height),
                                             hipMemcpyHostToDevice),
                      "hipMemcpy2DToArray")) {
            m_api->FreeMipmappedArray(mipmaps);
            return false;
        }
    }
    hipResourceDesc resource   = {};
    resource.resType           = hipResourceTypeMipmappedArray;
    resource.res.mipmap.mipmap = mipmaps;
    hipTextureDesc desc        = {};
    desc.addressMode[0]        = address_mode(wrap_s);
    desc.addressMode[1]        = address_mode(wrap_t);
    desc.filterMode            = hipFilterModeLinear;
    desc.readMode              = hipReadModeElementType;
    desc.normalizedCoords      = 1;
    // Not 1, unlike CUDA: the HIP runtime on Windows rejects every texture
    // with "operation not supported" when maxAnisotropy is 1.
    desc.maxAnisotropy        = 0;
    desc.mipmapFilterMode     = hipFilterModeLinear;
    desc.maxMipmapLevelClamp  = float(levels.size() - 1);
    hipTextureObject_t object = nullptr;
    if (!check(m_api->CreateTextureObject(&object, &resource, &desc, nullptr),
               "hipCreateTextureObject")) {
        m_api->FreeMipmappedArray(mipmaps);
        return false;
    }
    desc.addressMode[0]           = hipAddressModeWrap;
    desc.addressMode[1]           = hipAddressModeClamp;
    hipTextureObject_t env_object = nullptr;
    if (!check(m_api->CreateTextureObject(&env_object, &resource, &desc,
                                          nullptr),
               "hipCreateTextureObject")) {
        m_api->DestroyTextureObject(object);
        m_api->FreeMipmappedArray(mipmaps);
        return false;
    }
    // The mip-mapped array stays allocated as long as the process: the
    // texture objects do not own it.
    texture     = object;
    env_texture = env_object;
    return true;
}

void
HipRuntime::destroy_texture(void* texture)
{
    if (texture)
        m_api->DestroyTextureObject(hipTextureObject_t(texture));
}

OSL_NAMESPACE_END
