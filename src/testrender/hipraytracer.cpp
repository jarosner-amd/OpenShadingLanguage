// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#include "hipraytracer.h"

#include <cctype>
#include <cstring>

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/strutil.h>

#include <OSL/genclosure.h>
#include <OSL/oslclosure.h>
#include <OSL/oslexec.h>

#include "../testshade/amdgpu_artifacts.h"

#ifdef OSL_TESTRENDER_HIP
#    include "amdgpu_rend_lib.h"
#    include "hiploader.h"
#endif

OSL_NAMESPACE_BEGIN

#ifdef OSL_TESTRENDER_HIP

// A shader group made ready to run: its code object and kernel.
struct HipRaytracer::GpuGroup {
    void* module          = nullptr;
    void* kernel          = nullptr;
    size_t groupdata_size = 0;
    void* interactive     = nullptr;  // device copy, owned by the group
};

// The device memory of shading calls, reused by every call.
struct HipRaytracer::Device {
    char* state   = nullptr;  // RenderState
    char* context = nullptr;  // RenderContext
    char* uniform = nullptr;  // the shading-state block
    // The call block (see below) and its copy on the host.
    char* block       = nullptr;
    size_t block_size = 0;
    std::vector<char> staging;
};

// StackClosurePool is its 1024-byte buffer, then the next free address.
static_assert(sizeof(StackClosurePool) == 1024 + sizeof(void*),
              "StackClosurePool layout changed");

// Everything a shading call sends to the GPU or reads back is in one device
// buffer, the call block, so that a call is one copy in, one launch and one
// copy out: each blocking HIP call costs 50 to 70 us. In order: the
// ShaderGlobals, the closure pool, three transforms (identity, shader2common,
// object2common), then the group data. The first two come back.
static constexpr size_t
round_up(size_t n, size_t alignment)
{
    return (n + alignment - 1) / alignment * alignment;
}
static constexpr size_t k_globals_offset = 0;
static constexpr size_t k_pool_offset    = round_up(sizeof(ShaderGlobals), 64);
static constexpr size_t k_xforms_offset
    = round_up(k_pool_offset + sizeof(StackClosurePool), 64);
static constexpr size_t k_groupdata_offset
    = round_up(k_xforms_offset + 3 * sizeof(Matrix44), 256);

#else

// Without AMDGPU support there is nothing to hold.
class HipRuntime {};
struct HipRaytracer::GpuGroup {};
struct HipRaytracer::Device {};

#endif



HipRaytracer::HipRaytracer() = default;

HipRaytracer::~HipRaytracer()
{
#ifdef OSL_TESTRENDER_HIP
    if (m_hip) {
        for (auto& t : m_textures) {
            m_hip->free(t.second.first);
            m_hip->destroy_texture(t.second.second);
        }
        for (auto& g : m_groups)
            m_hip->unload_module(g.second->module);
        if (m_dev)
            for (char* p :
                 { m_dev->state, m_dev->context, m_dev->uniform, m_dev->block })
                m_hip->free(p);
    }
#endif
}



bool
HipRaytracer::fail(string_view message)
{
    errhandler().errorfmt("{}", message);
    m_ok = false;
    return false;
}



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
        return false;
    }
    if (!ok || !m_run)
        return ok;
    int size = 0, num_exports = 0;
    shadingsys->getattribute(&group, "gpu_artifact:0:size", size);
    shadingsys->getattribute(&group, "gpu_artifact:0:num_exports", num_exports);
    std::vector<char> data(size_t(size > 0 ? size : 0));
    if (!shadingsys->getattribute(&group, "gpu_artifact:0:data",
                                  TypeDesc(TypeDesc::UINT8, size), data.data()))
        return fail("cannot read the AMDGPU artifact");
    std::string fused;
    for (int m = 0; m < num_exports; ++m) {
        ustring kind, symbol;
        std::string prefix = fmtformat("gpu_artifact:0:export:{}:", m);
        shadingsys->getattribute(&group, prefix + "kind", TypeString, &kind);
        shadingsys->getattribute(&group, prefix + "symbol", TypeString,
                                 &symbol);
        if (kind == "fused")
            fused = symbol.string();
    }
    if (fused.empty())
        return fail("the AMDGPU artifact has no fused entry point");
    return build_module(group, data, fused);
}



void
HipRaytracer::prepare_render()
{
    // Compiling runs no shader; SimpleRaytracer::prepare_render does (for
    // the background, displacement and lights), which only works when the
    // shaders run on the GPU.
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
    if (m_run && m_ok)
        SimpleRaytracer::prepare_render();
}



void
HipRaytracer::render(int xres, int yres)
{
    if (m_run && m_ok)
        SimpleRaytracer::render(xres, yres);
}



#ifndef OSL_TESTRENDER_HIP

std::string
HipRaytracer::run(std::string& error)
{
    error = "this testrender was built without AMDGPU support "
            "(OSL_ENABLE_AMDGPU)";
    return {};
}

bool
HipRaytracer::build_module(ShaderGroup&, const std::vector<char>&,
                           const std::string&)
{
    return false;
}

bool
HipRaytracer::relocate_closures(std::vector<char>&, const char*, ClosureColor*&)
{
    return false;
}

bool
HipRaytracer::execute_group(ShadingContext& ctx, ShaderGroup& group,
                            ShaderGlobals& sg, ShadeKind kind)
{
    return SimpleRaytracer::execute_group(ctx, group, sg, kind);
}

RendererServices::TextureHandle*
HipRaytracer::get_texture_handle(ustring filename, ShadingContext* context,
                                 const TextureOpt* options)
{
    return SimpleRaytracer::get_texture_handle(filename, context, options);
}

void*
HipRaytracer::load_texture(ustring, void*&)
{
    return nullptr;
}

void*
HipRaytracer::device_alloc(size_t)
{
    return nullptr;
}

void
HipRaytracer::device_free(void*)
{
}

void*
HipRaytracer::copy_to_device(void*, const void*, size_t)
{
    return nullptr;
}

#else  // OSL_TESTRENDER_HIP

std::string
HipRaytracer::run(std::string& error)
{
    m_hip = std::make_unique<HipRuntime>();
    if (!m_hip->load()) {
        error = m_hip->error();
        return {};
    }
    int count = m_hip->device_count();
    std::string seen;
    for (int d = 0; d < count; ++d) {
        std::string name, arch;
        if (!m_hip->device_info(d, name, arch))
            continue;
        seen += (seen.empty() ? "" : ", ") + arch;
        if (amdgpu_rend_lib(arch).empty() || !m_hip->set_device(d))
            continue;
        print("AMD GPU {}: {} ({})\n", d, name, arch);
        m_arch = arch;
        m_run  = true;
        return arch;
    }
    error = count ? fmtformat("no AMD GPU with an architecture this "
                              "testrender was built for (found: {})",
                              seen)
                  : "no AMD GPU";
    return {};
}



// Make the GPU code object of one group: a one-point kernel that calls the
// group's fused entry point, compiled with hiprtc and linked with the
// renderer services and the group's artifact.
bool
HipRaytracer::build_module(ShaderGroup& group,
                           const std::vector<char>& artifact,
                           const std::string& fused)
{
    for (char c : fused)
        if (!(std::isalnum((unsigned char)c) || c == '_'))
            return fail(fmtformat("unexpected entry point name {}", fused));
    std::string source = fmtformat(
        "extern \"C\" __device__ void {0}(void*, void*, void*, void*, int, "
        "void*);\n"
        "extern \"C\" __global__ void osl_testrender_shade(void* sg, "
        "void* gd, void* ip)\n"
        "{{\n    {0}(sg, gd, nullptr, nullptr, 0, ip);\n}}\n",
        fused);
    std::vector<char> kernel, code;
    if (!m_hip->compile(source, "osl_testrender_shade.hip", m_arch, kernel)
        || !m_hip->link({ string_view(kernel.data(), kernel.size()),
                          amdgpu_rend_lib(m_arch),
                          string_view(artifact.data(), artifact.size()) },
                        code))
        return fail(m_hip->error());
    auto g    = std::make_unique<GpuGroup>();
    g->module = m_hip->load_module(code);
    if (g->module)
        g->kernel = m_hip->kernel(g->module, "osl_testrender_shade");
    if (!g->kernel) {
        m_hip->unload_module(g->module);
        return fail(m_hip->error());
    }
    int gd = 0;
    shadingsys->getattribute(&group, "llvm_groupdata_size", gd);
    g->groupdata_size = size_t(gd > 0 ? gd : 0);
    shadingsys->getattribute(&group, "device_interactive_params", TypeDesc::PTR,
                             &g->interactive);
    m_groups[&group] = std::move(g);
    return true;
}



// Every mip level of the file as 4 floats per texel; channels the file
// does not have are 0, as the CPU texture system's default fill. Returns
// the device GpuTexture, and the texture object in `object`.
void*
HipRaytracer::load_texture(ustring filename, void*& object)
{
    object = nullptr;
    std::vector<HipRuntime::TextureLevel> levels;
    for (int level = 0;; ++level) {
        OIIO::ImageBuf image(filename, 0, level);
        if (!image.init_spec(filename, 0, level)) {
            image.geterror();  // no such level: that is how the loop ends
            break;
        }
        const OIIO::ImageSpec& spec = image.spec();
        HipRuntime::TextureLevel l;
        l.width  = spec.width;
        l.height = spec.height;
        l.rgba.assign(size_t(l.width) * l.height * 4, 0.0f);
        OIIO::ROI roi(spec.x, spec.x + spec.width, spec.y, spec.y + spec.height,
                      spec.z, spec.z + 1, 0, std::min(4, spec.nchannels));
        if (!image.get_pixels(roi, OIIO::TypeFloat, l.rgba.data(),
                              4 * sizeof(float))) {
            errhandler().errorfmt("Could not read {} (mip level {}): {}",
                                  filename, level, image.geterror());
            return nullptr;
        }
        levels.push_back(std::move(l));
    }
    if (levels.empty()) {
        errhandler().errorfmt("Could not read {}", filename);
        return nullptr;
    }
    // A file without mip levels: make them, halving with a box filter down
    // to one texel, so that a lookup with a large footprint averages over
    // it, as the CPU texture system does. With a single level, the GPU
    // would take one bilinear sample instead.
    const bool make_mips = levels.size() == 1;
    while (make_mips && (levels.back().width > 1 || levels.back().height > 1)) {
        const HipRuntime::TextureLevel& from = levels.back();
        HipRuntime::TextureLevel to;
        to.width  = std::max(1, from.width / 2);
        to.height = std::max(1, from.height / 2);
        to.rgba.assign(size_t(to.width) * to.height * 4, 0.0f);
        for (int y = 0; y < to.height; ++y)
            for (int x = 0; x < to.width; ++x)
                for (int c = 0; c < 4; ++c) {
                    float sum = 0.0f;
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx) {
                            int sx = std::min(2 * x + dx, from.width - 1);
                            int sy = std::min(2 * y + dy, from.height - 1);
                            sum += from.rgba[(size_t(sy) * from.width + sx) * 4
                                             + c];
                        }
                    to.rgba[(size_t(y) * to.width + x) * 4 + c] = 0.25f * sum;
                }
        levels.push_back(std::move(to));
    }
    object = m_hip->create_texture(levels);
    if (!object) {
        errhandler().errorfmt("Could not make a GPU texture of {}: {}",
                              filename, m_hip->error());
        return nullptr;
    }
    // What environment() needs besides the texels: which way is up, as
    // OpenImageIO decides it, and the height for clamping at the poles.
    // The texture system's default ("latlong_up", y unless set otherwise),
    // unless the file says.
    GpuTexture info;
    info.object    = object;
    info.y_up      = true;
    const char* up = nullptr;
    if (texturesys()->getattribute("latlong_up", OIIO::TypeString, &up) && up)
        info.y_up = (up[0] == 'y');
    OIIO::ImageBuf top(filename);
    if (top.init_spec(filename, 0, 0)) {
        std::string file_up = top.spec().get_string_attribute(
            "oiio:updirection");
        if (file_up == "y")
            info.y_up = true;
        else if (file_up == "z")
            info.y_up = false;
    }
    info.height  = levels[0].height;
    void* device = m_hip->alloc(sizeof(info));
    if (!device || !m_hip->to_device(device, &info, sizeof(info))) {
        m_hip->free(device);
        m_hip->destroy_texture(object);
        object = nullptr;
        errhandler().errorfmt("Could not copy the GPU texture of {}: {}",
                              filename, m_hip->error());
        return nullptr;
    }
    return device;
}



RendererServices::TextureHandle*
HipRaytracer::get_texture_handle(ustring filename, ShadingContext* context,
                                 const TextureOpt* options)
{
    if (!m_run)
        return SimpleRaytracer::get_texture_handle(filename, context, options);
    std::lock_guard<std::mutex> lock(m_textures_mutex);
    auto found = m_textures.find(filename);
    if (found == m_textures.end()) {
        void* object = nullptr;
        void* device = load_texture(filename, object);
        found
            = m_textures.emplace(filename, std::make_pair(device, object)).first;
    }
    return reinterpret_cast<TextureHandle*>(found->second.first);
}



void*
HipRaytracer::device_alloc(size_t size)
{
    return m_hip ? m_hip->alloc(size) : nullptr;
}

void
HipRaytracer::device_free(void* ptr)
{
    if (m_hip)
        m_hip->free(ptr);
}

void*
HipRaytracer::copy_to_device(void* dst_device, const void* src_host,
                             size_t size)
{
    return m_hip && m_hip->to_device(dst_device, src_host, size) ? dst_device
                                                                 : nullptr;
}



// The closures came back as a copy of the device closure pool. Point their
// links (add and mul children, closure-typed parameters) into the copy.
bool
HipRaytracer::relocate_closures(std::vector<char>& pool,
                                const char* device_base, ClosureColor*& ci)
{
    const size_t used = sizeof(StackClosurePool) - sizeof(void*);
    auto to_host      = [&](const void* p, ClosureColor*& out) {
        out = nullptr;
        if (!p)
            return true;
        const char* d = static_cast<const char*>(p);
        if (d < device_base || d + sizeof(ClosureColor) > device_base + used)
            return false;
        out = reinterpret_cast<ClosureColor*>(pool.data() + (d - device_base));
        return true;
    };
    std::vector<ClosureColor*> todo;
    if (!to_host(ci, ci))
        return false;
    if (ci)
        todo.push_back(ci);
    while (!todo.empty()) {
        ClosureColor* c = todo.back();
        todo.pop_back();
        if (c->id == ClosureColor::ADD) {
            auto* a = reinterpret_cast<ClosureAdd*>(c);
            ClosureColor *x, *y;
            if (!to_host(a->closureA, x) || !to_host(a->closureB, y))
                return false;
            a->closureA = x;
            a->closureB = y;
            for (ClosureColor* n : { x, y })
                if (n)
                    todo.push_back(n);
        } else if (c->id == ClosureColor::MUL) {
            auto* m = reinterpret_cast<ClosureMul*>(c);
            ClosureColor* x;
            if (!to_host(m->closure, x))
                return false;
            m->closure = x;
            if (x)
                todo.push_back(x);
        } else {
            int id                     = c->id;
            const ClosureParam* params = nullptr;
            if (!shadingsys->query_closure(nullptr, &id, &params) || !params)
                return false;
            char* data = reinterpret_cast<char*>(
                reinterpret_cast<ClosureComponent*>(c)->data());
            for (const ClosureParam* p = params; p->type != TypeDesc(); ++p) {
                if (p->type != TypeDesc::PTR)
                    continue;
                const void* dev;
                std::memcpy(&dev, data + p->offset, sizeof(dev));
                ClosureColor* x;
                if (!to_host(dev, x))
                    return false;
                std::memcpy(data + p->offset, &x, sizeof(x));
                if (x)
                    todo.push_back(x);
            }
        }
    }
    return true;
}



bool
HipRaytracer::execute_group(ShadingContext& ctx, ShaderGroup& group,
                            ShaderGlobals& sg, ShadeKind kind)
{
    if (!m_run)
        return SimpleRaytracer::execute_group(ctx, group, sg, kind);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_ok)
        return false;
    auto found = m_groups.find(&group);
    if (found == m_groups.end())
        return fail("a shader group has no GPU code object");
    const GpuGroup& g = *found->second;
    HipRuntime& hip   = *m_hip;

    if (!m_dev) {
        // First call: the device memory every call reuses.
        void* block    = nullptr;
        long long size = 0;
        if (!shadingsys->getattribute("shading_state_uniform", TypeDesc::PTR,
                                      &block)
            || !shadingsys->getattribute("shading_state_uniform:size",
                                         TypeDesc::LONGLONG, &size)
            || !block || size <= 0)
            return fail("no shading_state_uniform from the ShadingSystem");
        m_dev     = std::make_unique<Device>();
        Device& d = *m_dev;
        d.state   = (char*)hip.alloc(sizeof(RenderState));
        d.context = (char*)hip.alloc(sizeof(RenderContext));
        d.uniform = (char*)hip.alloc(size_t(size));
        RenderContext rc {};
        fill_render_context(rc);
        if (!d.state || !d.context || !d.uniform
            || !hip.to_device(d.context, &rc, sizeof(rc))
            || !hip.to_device(d.uniform, block, size_t(size)))
            return fail(hip.error());
    }
    Device& d           = *m_dev;
    const size_t needed = k_groupdata_offset + g.groupdata_size;
    if (needed > d.block_size) {
        // A larger group than any before: a new call block. Nothing in it
        // outlives a call, but the render state points at its closure pool.
        hip.free(d.block);
        d.block      = nullptr;
        d.block_size = 0;
        d.block      = (char*)hip.alloc(needed);
        if (!d.block)
            return fail(hip.error());
        d.block_size = needed;
        d.staging.assign(needed, 0);
        RenderState rs { reinterpret_cast<RenderContext*>(d.context),
                         reinterpret_cast<StackClosurePool*>(d.block
                                                             + k_pool_offset) };
        if (!hip.to_device(d.state, &rs, sizeof(rs)))
            return fail(hip.error());
    }

    // The globals with device pointers. Transforms that are null mean
    // identity, as in SimpleRaytracer::get_matrix.
    char* const stage       = d.staging.data();
    ShaderGlobals dsg       = sg;
    dsg.renderstate         = d.state;
    dsg.tracedata           = nullptr;
    dsg.objdata             = nullptr;
    dsg.context             = nullptr;
    dsg.renderer            = nullptr;
    dsg.shadingStateUniform = d.uniform;
    dsg.Ci                  = nullptr;
    Matrix44 identity;
    identity.makeIdentity();
    std::memcpy(stage + k_xforms_offset, &identity, sizeof(Matrix44));
    TransformationPtr host_xf[2] = { sg.shader2common, sg.object2common };
    TransformationPtr dev_xf[2];
    for (int i = 0; i < 2; ++i) {
        const size_t slot = k_xforms_offset + (i + 1) * sizeof(Matrix44);
        if (host_xf[i])
            std::memcpy(stage + slot, host_xf[i], sizeof(Matrix44));
        dev_xf[i] = TransformationPtr(d.block
                                      + (host_xf[i] ? slot : k_xforms_offset));
    }
    dsg.shader2common = dev_xf[0];
    dsg.object2common = dev_xf[1];
    std::memcpy(stage + k_globals_offset, &dsg, sizeof(dsg));
    // The pool as reset() leaves it: zeros, and its pointer at its buffer.
    std::memset(stage + k_pool_offset, 0, sizeof(StackClosurePool));
    void* next = d.block + k_pool_offset;
    std::memcpy(stage + k_pool_offset + 1024, &next, sizeof(next));
    if (g.groupdata_size)
        std::memset(stage + k_groupdata_offset, 0, g.groupdata_size);
    void* globals     = d.block + k_globals_offset;
    void* groupdata   = d.block + k_groupdata_offset;
    void* interactive = g.interactive;
    void* args[]      = { &globals, &groupdata, &interactive };
    if (!hip.to_device(d.block, stage, needed)
        || !hip.launch_one(g.kernel, args)
        || !hip.to_host(stage, d.block, k_xforms_offset))
        return fail(hip.error());

    // Back: the globals (displacement moves P) and the closures, which
    // stay valid in this thread's buffer for this kind until its next call.
    thread_local std::vector<char> closures[4];
    std::vector<char>& pool = closures[int(kind)];
    pool.assign(stage + k_pool_offset,
                stage + k_pool_offset + sizeof(StackClosurePool));
    std::memcpy(&dsg, stage + k_globals_offset, sizeof(dsg));
    ClosureColor* ci = dsg.Ci;
    if (!relocate_closures(pool, d.block + k_pool_offset, ci))
        return fail("a closure points outside the GPU closure memory");

    ShaderGlobals host     = sg;
    sg                     = dsg;
    sg.renderstate         = host.renderstate;
    sg.tracedata           = host.tracedata;
    sg.objdata             = host.objdata;
    sg.context             = host.context;
    sg.renderer            = host.renderer;
    sg.shadingStateUniform = host.shadingStateUniform;
    sg.shader2common       = host.shader2common;
    sg.object2common       = host.object2common;
    sg.Ci                  = ci;
    return true;
}

#endif  // OSL_TESTRENDER_HIP

OSL_NAMESPACE_END
