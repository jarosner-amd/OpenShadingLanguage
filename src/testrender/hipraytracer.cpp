// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#include "hipraytracer.h"

#include <cctype>
#include <condition_variable>
#include <cstring>

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/strutil.h>

#include <OSL/genclosure.h>
#include <OSL/oslclosure.h>
#include <OSL/oslexec.h>

#include "../testshade/amdgpu_artifacts.h"

#ifdef OSL_TESTRENDER_HIP
#    include "../testshade/amdgpu_rend_lib.h"
#    include "../testshade/amdgpu_textures.h"
#    include "../testshade/hiploader.h"
#endif

OSL_NAMESPACE_BEGIN

#ifdef OSL_TESTRENDER_HIP

// One shading call waiting to share a launch (see execute_group).
struct HipRaytracer::GpuCall {
    void* globals   = nullptr;
    void* groupdata = nullptr;
    bool done       = false;
    bool ok         = true;
};

// The most calls in one shared launch.
static constexpr size_t k_max_calls = 1024;

// A shader group made ready to run: its code object and kernels.
struct HipRaytracer::GpuGroup {
    void* module          = nullptr;
    void* list_kernel     = nullptr;  // one thread per call in `list`
    void* batch_kernel    = nullptr;  // one thread per shading point
    size_t groupdata_size = 0;
    void* interactive     = nullptr;  // device copy, owned by the group

    // The calls of all threads to this group share launches. One thread at
    // a time launches all the calls waiting, on `stream`, while the others
    // wait for `wake`.
    std::mutex mutex;
    std::condition_variable wake;
    std::vector<GpuCall*> waiting;
    bool launching = false;
    void* stream   = nullptr;
    // Page-locked: the globals of each call in a launch, then their group
    // data, k_max_calls of each.
    void** list = nullptr;
};

// The device memory that every shading call reads.
struct HipRaytracer::Device {
    char* context = nullptr;  // RenderContext
    char* uniform = nullptr;  // the shading-state block
    // The device memory of a batch of independent points
    // (execute_group_batch) and its copy on the host.
    char* batch       = nullptr;
    size_t batch_size = 0;
    std::vector<char> batch_staging;
};

// What one CPU thread needs for its shading calls (execute_group): its own
// memory for them, so that the calls of several threads can share a launch.
struct HipRaytracer::CallSlot {
    char* state = nullptr;  // RenderState, pointing at this block's pool
    // The call block (see below), in page-locked host memory.
    char* block       = nullptr;
    size_t block_size = 0;
};

// StackClosurePool is its 1024-byte buffer, then the next free address.
static_assert(sizeof(StackClosurePool) == 1024 + sizeof(void*),
              "StackClosurePool layout changed");

// Everything a shading call sends to the GPU or reads back is in one buffer,
// the call block. It is page-locked host memory that the GPU reads and writes
// directly, so a call needs no copies, only its part of a launch: each HIP
// command costs tens of us, and the GPU runs only about 9000 launches a
// second, however many threads or processes make them. In order: the
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
class AmdgpuTextures {};
struct HipRaytracer::GpuGroup {};
struct HipRaytracer::Device {};
struct HipRaytracer::CallSlot {};

#endif



static std::atomic<unsigned long long> s_next_id { 1 };

HipRaytracer::HipRaytracer() : m_id(s_next_id++)
{
}

HipRaytracer::~HipRaytracer()
{
#ifdef OSL_TESTRENDER_HIP
    if (m_hip) {
        m_textures.reset();
        for (auto& g : m_groups) {
            m_hip->stream_destroy(g.second->stream);
            m_hip->host_free(g.second->list);
            m_hip->unload_module(g.second->module);
        }
        for (auto& s : m_slots) {
            m_hip->free(s->state);
            m_hip->host_free(s->block);
        }
        if (m_dev)
            for (char* p : { m_dev->context, m_dev->uniform, m_dev->batch })
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
HipRaytracer::relocate_closures(char*, const char*, ClosureColor*&)
{
    return false;
}

bool
HipRaytracer::init_device()
{
    return false;
}

bool
HipRaytracer::execute_group_batch(
    ShadingContext& ctx, ShaderGroup& group, ShaderGlobals* sgs, int n,
    ShadeKind kind, const std::function<void(int, ShaderGlobals&)>& done)
{
    return SimpleRaytracer::execute_group_batch(ctx, group, sgs, n, kind, done);
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
        m_arch     = arch;
        m_run      = true;
        m_textures = std::make_unique<AmdgpuTextures>(*m_hip, errhandler());
        return arch;
    }
    error = count ? fmtformat("no AMD GPU with an architecture this "
                              "testrender was built for (found: {})",
                              seen)
                  : "no AMD GPU";
    return {};
}



// Make the GPU code object of one group: kernels that call the group's fused
// entry point, for a list of calls and for a batch of points, compiled with
// hiprtc and linked with the renderer services and the group's artifact.
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
        "extern \"C\" __global__ void osl_testrender_shade_list(void** sgs, "
        "void** gds, void* ip, int n)\n"
        "{{\n"
        "    int i = blockIdx.x * blockDim.x + threadIdx.x;\n"
        "    if (i < n)\n"
        "        {0}(sgs[i], gds[i], nullptr, nullptr, 0, ip);\n"
        "}}\n"
        "extern \"C\" __global__ void osl_testrender_shade_batch(char* sgs, "
        "unsigned long long sg_stride, char* gds, "
        "unsigned long long gd_stride, void* ip, int n)\n"
        "{{\n"
        "    int i = blockIdx.x * blockDim.x + threadIdx.x;\n"
        "    if (i < n)\n"
        "        {0}(sgs + i * sg_stride, gds + i * gd_stride, nullptr, "
        "nullptr, 0, ip);\n"
        "}}\n",
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
    if (g->module) {
        g->list_kernel  = m_hip->kernel(g->module, "osl_testrender_shade_list");
        g->batch_kernel = m_hip->kernel(g->module,
                                        "osl_testrender_shade_batch");
        g->stream       = m_hip->stream_create();
        g->list = (void**)m_hip->host_alloc(2 * k_max_calls * sizeof(void*));
    }
    if (!g->list_kernel || !g->batch_kernel || !g->stream || !g->list) {
        m_hip->stream_destroy(g->stream);
        m_hip->host_free(g->list);
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



RendererServices::TextureHandle*
HipRaytracer::get_texture_handle(ustring filename, ShadingContext* context,
                                 const TextureOpt* options)
{
    if (!m_run)
        return SimpleRaytracer::get_texture_handle(filename, context, options);
    return reinterpret_cast<TextureHandle*>(
        m_textures->handle(filename, texturesys()));
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
HipRaytracer::relocate_closures(char* pool, const char* device_base,
                                ClosureColor*& ci)
{
    const size_t used = sizeof(StackClosurePool) - sizeof(void*);
    auto to_host      = [&](const void* p, ClosureColor*& out) {
        out = nullptr;
        if (!p)
            return true;
        const char* d = static_cast<const char*>(p);
        if (d < device_base || d + sizeof(ClosureColor) > device_base + used)
            return false;
        out = reinterpret_cast<ClosureColor*>(pool + (d - device_base));
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
    if (!m_ok)
        return false;
    // The groups are set up before rendering, and the shared device memory
    // before this thread's first call (in call_slot). Both are only read
    // here; the launches are shared under the group's own lock.
    auto found = m_groups.find(&group);
    if (found == m_groups.end())
        return fail("a shader group has no GPU code object");
    GpuGroup& g     = *found->second;
    HipRuntime& hip = *m_hip;
    CallSlot* slot  = call_slot();
    if (!slot)
        return false;
    CallSlot& d         = *slot;
    const size_t needed = k_groupdata_offset + g.groupdata_size;
    if (needed > d.block_size) {
        // A larger group than any before in this thread: a new call block.
        // Nothing in it outlives a call, but the render state points at its
        // closure pool.
        hip.host_free(d.block);
        d.block_size = 0;
        d.block      = (char*)hip.host_alloc(needed);
        if (!d.block)
            return fail(hip.error());
        d.block_size = needed;
        std::memset(d.block, 0, needed);
        RenderState rs { reinterpret_cast<RenderContext*>(m_dev->context),
                         reinterpret_cast<StackClosurePool*>(d.block
                                                             + k_pool_offset) };
        if (!hip.to_device(d.state, &rs, sizeof(rs)))
            return fail(hip.error());
    }

    // The globals with device pointers. Transforms that are null mean
    // identity, as in SimpleRaytracer::get_matrix.
    char* const stage       = d.block;
    ShaderGlobals dsg       = sg;
    dsg.renderstate         = d.state;
    dsg.tracedata           = nullptr;
    dsg.objdata             = nullptr;
    dsg.context             = nullptr;
    dsg.renderer            = nullptr;
    dsg.shadingStateUniform = m_dev->uniform;
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
    GpuCall call;
    call.globals   = d.block + k_globals_offset;
    call.groupdata = d.block + k_groupdata_offset;
    if (!share_launch(g, call))
        return false;

    // Back: the globals (displacement moves P) and the closures, which
    // stay valid in this thread's buffer for this kind until its next call.
    thread_local std::vector<char> closures[4];
    std::vector<char>& pool = closures[int(kind)];
    pool.assign(stage + k_pool_offset,
                stage + k_pool_offset + sizeof(StackClosurePool));
    std::memcpy(&dsg, stage + k_globals_offset, sizeof(dsg));
    ClosureColor* ci = dsg.Ci;
    if (!relocate_closures(pool.data(), d.block + k_pool_offset, ci))
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



// Run one call, in a launch shared with the calls of other threads to the
// same group. Whichever thread finds no launch running launches every call
// waiting; the others wait until it is done. So a call waits at most for the
// launch already running, and while one runs the next one fills up.
bool
HipRaytracer::share_launch(GpuGroup& g, GpuCall& call)
{
    std::unique_lock<std::mutex> lock(g.mutex);
    g.waiting.push_back(&call);
    while (!call.done) {
        if (g.launching) {
            g.wake.wait(lock);
            continue;
        }
        g.launching    = true;
        const size_t n = std::min(g.waiting.size(), k_max_calls);
        std::vector<GpuCall*> calls(g.waiting.begin(), g.waiting.begin() + n);
        g.waiting.erase(g.waiting.begin(), g.waiting.begin() + n);
        lock.unlock();

        for (size_t i = 0; i < n; ++i) {
            g.list[i]               = calls[i]->globals;
            g.list[k_max_calls + i] = calls[i]->groupdata;
        }
        void* sgs         = g.list;
        void* gds         = g.list + k_max_calls;
        void* interactive = g.interactive;
        int count         = int(n);
        void* args[]      = { &sgs, &gds, &interactive, &count };
        const bool ok = m_hip->launch_async(g.list_kernel,
                                            unsigned((n + 63) / 64), 64, args,
                                            g.stream)
                        && m_hip->stream_wait(g.stream);
        if (!ok)
            fail(m_hip->error());

        lock.lock();
        for (GpuCall* c : calls) {
            c->ok   = ok;
            c->done = true;
        }
        g.launching = false;
        g.wake.notify_all();
    }
    return call.ok;
}



// The device memory every call reuses, made at the first call.
bool
HipRaytracer::init_device()
{
    if (m_dev)
        return true;
    HipRuntime& hip = *m_hip;
    void* block     = nullptr;
    long long size  = 0;
    if (!shadingsys->getattribute("shading_state_uniform", TypeDesc::PTR, &block)
        || !shadingsys->getattribute("shading_state_uniform:size",
                                     TypeDesc::LONGLONG, &size)
        || !block || size <= 0)
        return fail("no shading_state_uniform from the ShadingSystem");
    auto dev     = std::make_unique<Device>();
    dev->context = (char*)hip.alloc(sizeof(RenderContext));
    dev->uniform = (char*)hip.alloc(size_t(size));
    RenderContext rc {};
    fill_render_context(rc);
    m_dev = std::move(dev);
    if (!m_dev->context || !m_dev->uniform
        || !hip.to_device(m_dev->context, &rc, sizeof(rc))
        || !hip.to_device(m_dev->uniform, block, size_t(size)))
        return fail(hip.error());
    return true;
}



// The calling thread's call slot, made on its first shading call (with the
// shared device memory, the first time any thread calls). Null on failure.
HipRaytracer::CallSlot*
HipRaytracer::call_slot()
{
    thread_local unsigned long long owner = 0;
    thread_local CallSlot* slot           = nullptr;
    if (owner == m_id)
        return slot;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!init_device())
        return nullptr;
    auto s   = std::make_unique<CallSlot>();
    s->state = (char*)m_hip->alloc(sizeof(RenderState));
    if (!s->state) {
        fail(m_hip->error());
        return nullptr;
    }
    slot  = s.get();
    owner = m_id;
    m_slots.push_back(std::move(s));
    return slot;
}



// n independent points in one launch, a thread each. The device block holds
// the identity matrix, then for every point its ShaderGlobals, closure pool,
// RenderState, two transforms and group data. The globals and the pools come
// back, and each point's closures are relocated into its copy of the pool.
bool
HipRaytracer::execute_group_batch(
    ShadingContext& ctx, ShaderGroup& group, ShaderGlobals* sgs, int n,
    ShadeKind kind, const std::function<void(int, ShaderGlobals&)>& done)
{
    if (!m_run || n <= 0)
        return SimpleRaytracer::execute_group_batch(ctx, group, sgs, n, kind,
                                                    done);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_ok)
        return false;
    auto found = m_groups.find(&group);
    if (found == m_groups.end())
        return fail("a shader group has no GPU code object");
    const GpuGroup& g = *found->second;
    HipRuntime& hip   = *m_hip;
    if (!init_device())
        return false;
    Device& d = *m_dev;

    const size_t count        = size_t(n);
    const size_t sg_stride    = round_up(sizeof(ShaderGlobals), 64);
    const size_t pool_stride  = round_up(sizeof(StackClosurePool), 64);
    const size_t state_stride = round_up(sizeof(RenderState), 16);
    const size_t xform_stride = 2 * sizeof(Matrix44);
    const size_t gd_stride    = round_up(g.groupdata_size, 64);
    const size_t off_sgs      = 64;
    const size_t off_pools    = off_sgs + count * sg_stride;
    const size_t off_states   = off_pools + count * pool_stride;
    const size_t off_xforms   = off_states + count * state_stride;
    const size_t off_gds      = round_up(off_xforms + count * xform_stride, 64);
    const size_t total        = off_gds + count * gd_stride;
    if (total > d.batch_size) {
        hip.free(d.batch);
        d.batch      = nullptr;
        d.batch_size = 0;
        d.batch      = (char*)hip.alloc(total);
        if (!d.batch)
            return fail(hip.error());
        d.batch_size = total;
        d.batch_staging.assign(total, 0);
    }
    char* const stage = d.batch_staging.data();
    Matrix44 identity;
    identity.makeIdentity();
    std::memcpy(stage, &identity, sizeof(identity));
    for (size_t i = 0; i < count; ++i) {
        const size_t pool_at         = off_pools + i * pool_stride;
        const size_t state_at        = off_states + i * state_stride;
        const size_t xform_at        = off_xforms + i * xform_stride;
        ShaderGlobals dsg            = sgs[i];
        dsg.renderstate              = d.batch + state_at;
        dsg.tracedata                = nullptr;
        dsg.objdata                  = nullptr;
        dsg.context                  = nullptr;
        dsg.renderer                 = nullptr;
        dsg.shadingStateUniform      = d.uniform;
        dsg.Ci                       = nullptr;
        TransformationPtr host_xf[2] = { sgs[i].shader2common,
                                         sgs[i].object2common };
        TransformationPtr dev_xf[2];
        for (int k = 0; k < 2; ++k) {
            const size_t slot = xform_at + size_t(k) * sizeof(Matrix44);
            if (host_xf[k])
                std::memcpy(stage + slot, host_xf[k], sizeof(Matrix44));
            dev_xf[k] = TransformationPtr(d.batch + (host_xf[k] ? slot : 0));
        }
        dsg.shader2common = dev_xf[0];
        dsg.object2common = dev_xf[1];
        std::memcpy(stage + off_sgs + i * sg_stride, &dsg, sizeof(dsg));
        RenderState rs { reinterpret_cast<RenderContext*>(d.context),
                         reinterpret_cast<StackClosurePool*>(d.batch
                                                             + pool_at) };
        std::memcpy(stage + state_at, &rs, sizeof(rs));
        std::memset(stage + pool_at, 0, sizeof(StackClosurePool));
        void* next = d.batch + pool_at;
        std::memcpy(stage + pool_at + 1024, &next, sizeof(next));
        if (g.groupdata_size)
            std::memset(stage + off_gds + i * gd_stride, 0, g.groupdata_size);
    }
    void* kernel_sgs          = d.batch + off_sgs;
    void* kernel_gds          = d.batch + off_gds;
    unsigned long long sg_str = sg_stride;
    unsigned long long gd_str = gd_stride;
    void* interactive         = g.interactive;
    int kernel_n              = n;
    void* args[]              = { &kernel_sgs, &sg_str,      &kernel_gds,
                                  &gd_str,     &interactive, &kernel_n };
    if (!hip.to_device(d.batch, stage, total)
        || !hip.launch(g.batch_kernel, unsigned((count + 63) / 64), 64, args)
        || !hip.to_host(stage + off_sgs, d.batch + off_sgs,
                        off_states - off_sgs))
        return fail(hip.error());

    for (size_t i = 0; i < count; ++i) {
        ShaderGlobals dsg;
        std::memcpy(&dsg, stage + off_sgs + i * sg_stride, sizeof(dsg));
        const size_t pool_at = off_pools + i * pool_stride;
        ClosureColor* ci     = dsg.Ci;
        if (!relocate_closures(stage + pool_at, d.batch + pool_at, ci))
            return fail("a closure points outside the GPU closure memory");
        const ShaderGlobals host   = sgs[i];
        sgs[i]                     = dsg;
        sgs[i].renderstate         = host.renderstate;
        sgs[i].tracedata           = host.tracedata;
        sgs[i].objdata             = host.objdata;
        sgs[i].context             = host.context;
        sgs[i].renderer            = host.renderer;
        sgs[i].shadingStateUniform = host.shadingStateUniform;
        sgs[i].shader2common       = host.shader2common;
        sgs[i].object2common       = host.object2common;
        sgs[i].Ci                  = ci;
        done(int(i), sgs[i]);
    }
    return true;
}

#endif  // OSL_TESTRENDER_HIP

OSL_NAMESPACE_END
