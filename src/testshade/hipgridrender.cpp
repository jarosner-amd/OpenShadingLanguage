// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#include "hipgridrender.h"

#include <cctype>
#include <cstring>
#include <utility>

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/strutil.h>

#include <OSL/journal.h>
#include <OSL/oslexec.h>

#ifdef OSL_TESTSHADE_HIP
#    include "amdgpu_rend_lib.h"
#    include "amdgpu_textures.h"
#    include "hiploader.h"
#endif

OSL_NAMESPACE_BEGIN

static constexpr size_t
round_up(size_t n, size_t alignment)
{
    return (n + alignment - 1) / alignment * alignment;
}

#ifdef OSL_TESTSHADE_HIP

// The device memory of the grid, made by prepare_render() and reused by
// every render().
struct HipGridRenderer::Device {
    void* module          = nullptr;
    void* kernel          = nullptr;
    size_t groupdata_size = 0;
    void* interactive     = nullptr;  // owned by the shader group
    char* context         = nullptr;  // RenderContext
    char* uniform         = nullptr;  // the shading-state block
    char* identity        = nullptr;  // an identity Matrix44
    char* outputs         = nullptr;  // every output, at output_offset()
    void* context_journal = nullptr;  // the journal the RenderContext has
    // The journal of a launch (see set_journal()) and its copy on the host.
    char* journal       = nullptr;
    size_t journal_size = 0;
    std::vector<uint8_t> journal_host;
    // Device copies of the transforms the ShaderGlobals point to.
    std::vector<std::pair<const void*, char*>> xforms;
    // One chunk of grid points (see render()) and its copy on the host.
    char* chunk       = nullptr;
    size_t chunk_size = 0;
    std::vector<char> staging;
};

// StackClosurePool is its 1024-byte buffer, then the next free address.
static_assert(sizeof(StackClosurePool) == 1024 + sizeof(void*),
              "StackClosurePool layout changed");

// Points per launch: bounds the device memory of a large grid (about 1.5 KB
// per point, plus the group data).
static constexpr int k_chunk_points = 65536;

// The journal page of a grid point: as testshade's CPU journal pages, so
// that a message that fits there fits here too.
static constexpr uint32_t k_journal_page = 1024;

#else

// Without AMDGPU support there is nothing to hold.
class HipRuntime {};
class AmdgpuTextures {};
struct HipGridRenderer::Device {};

#endif



HipGridRenderer::HipGridRenderer() = default;

HipGridRenderer::~HipGridRenderer()
{
#ifdef OSL_TESTSHADE_HIP
    m_textures.reset();
    if (m_hip && m_dev) {
        m_hip->unload_module(m_dev->module);
        for (auto& x : m_dev->xforms)
            m_hip->free(x.second);
        for (char* p : { m_dev->context, m_dev->uniform, m_dev->identity,
                         m_dev->outputs, m_dev->chunk, m_dev->journal })
            m_hip->free(p);
    }
#endif
}



bool
HipGridRenderer::fail(string_view message)
{
    errhandler().errorfmt("{}", message);
    m_ok = false;
    return false;
}



// Each output gets its own region of the device output buffer, aligned to
// 256 bytes, with the same layout as its image: one value per grid point.
bool
HipGridRenderer::add_output(string_view varname, string_view filename,
                            TypeDesc datatype, int nchannels)
{
    if (!SimpleRenderer::add_output(varname, filename, datatype, nchannels))
        return false;
    const OIIO::ImageBuf* ib = outputbuf(int(noutputs() - 1));
    m_output_offsets.push_back(m_outputs_size);
    m_outputs_size = round_up(m_outputs_size + ib->spec().image_bytes(), 256);
    return true;
}



#ifndef OSL_TESTSHADE_HIP

std::string
HipGridRenderer::run(std::string& error)
{
    error = "this testshade was built without AMDGPU support "
            "(OSL_ENABLE_AMDGPU)";
    return {};
}

void
HipGridRenderer::prepare_render()
{
    fail("this testshade was built without AMDGPU support");
}

void
HipGridRenderer::warmup()
{
}

void
HipGridRenderer::render(int, int)
{
}

void
HipGridRenderer::finalize_pixel_buffer()
{
}

bool
HipGridRenderer::build_module(const std::vector<char>&, const std::string&)
{
    return false;
}

bool
HipGridRenderer::init_device()
{
    return false;
}

bool
HipGridRenderer::upload_context(void*)
{
    return false;
}

bool
HipGridRenderer::read_journal()
{
    return false;
}

RendererServices::TextureHandle*
HipGridRenderer::get_texture_handle(ustring filename, ShadingContext* context,
                                    const TextureOpt* options)
{
    return SimpleRenderer::get_texture_handle(filename, context, options);
}

void*
HipGridRenderer::device_alloc(size_t)
{
    return nullptr;
}

void
HipGridRenderer::device_free(void*)
{
}

void*
HipGridRenderer::copy_to_device(void*, const void*, size_t)
{
    return nullptr;
}

#else  // OSL_TESTSHADE_HIP

std::string
HipGridRenderer::run(std::string& error)
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
        m_arch     = arch;
        m_textures = std::make_unique<AmdgpuTextures>(*m_hip, errhandler());
        return arch;
    }
    error = count ? fmtformat("no AMD GPU with an architecture this "
                              "testshade was built for (found: {})",
                              seen)
                  : "no AMD GPU";
    return {};
}



void
HipGridRenderer::prepare_render()
{
    if (!m_hip || m_arch.empty()) {
        fail("no AMD GPU was chosen (HipGridRenderer::run)");
        return;
    }
    if (shaders().empty() || !shaders()[0]) {
        fail("no shader group to run on the AMD GPU");
        return;
    }
    ShaderGroup& group = *shaders()[0];
    int num_artifacts  = 0;
    shadingsys->getattribute(&group, "gpu_num_artifacts", num_artifacts);
    if (num_artifacts < 1) {
        fail("no AMDGPU artifact was produced. Does the shader write any "
             "output? Mark one with -o VARIABLE FILENAME.");
        return;
    }
    int size = 0, num_exports = 0;
    shadingsys->getattribute(&group, "gpu_artifact:0:size", size);
    shadingsys->getattribute(&group, "gpu_artifact:0:num_exports", num_exports);
    std::vector<char> data(size_t(size > 0 ? size : 0));
    if (!shadingsys->getattribute(&group, "gpu_artifact:0:data",
                                  TypeDesc(TypeDesc::UINT8, size),
                                  data.data())) {
        fail("cannot read the AMDGPU artifact");
        return;
    }
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
    if (fused.empty()) {
        fail("the AMDGPU artifact has no fused entry point");
        return;
    }
    m_dev = std::make_unique<Device>();
    if (!build_module(data, fused) || !init_device())
        return;
    int gd = 0;
    shadingsys->getattribute(&group, "llvm_groupdata_size", gd);
    m_dev->groupdata_size = size_t(gd > 0 ? gd : 0);
    shadingsys->getattribute(&group, "device_interactive_params", TypeDesc::PTR,
                             &m_dev->interactive);
}



// The GPU code object of the group: a kernel that runs the group's fused
// entry point for n grid points from `first` on, a thread each, compiled
// with hiprtc and linked with the renderer services and the group's
// artifact. Grid point i has the shade index (and output index) first + i.
bool
HipGridRenderer::build_module(const std::vector<char>& artifact,
                              const std::string& fused)
{
    for (char c : fused)
        if (!(std::isalnum((unsigned char)c) || c == '_'))
            return fail(fmtformat("unexpected entry point name {}", fused));
    std::string source = fmtformat(
        "extern \"C\" __device__ void {0}(void*, void*, void*, void*, int, "
        "void*);\n"
        "extern \"C\" __global__ void osl_testshade_grid(char* sgs, "
        "unsigned long long sg_stride, char* gds, "
        "unsigned long long gd_stride, void* outputs, void* ip, int first, "
        "int n)\n"
        "{{\n"
        "    int i = blockIdx.x * blockDim.x + threadIdx.x;\n"
        "    if (i < n)\n"
        "        {0}(sgs + i * sg_stride, gds + i * gd_stride, nullptr, "
        "outputs, first + i, ip);\n"
        "}}\n",
        fused);
    std::vector<char> kernel, code;
    if (!m_hip->compile(source, "osl_testshade_grid.hip", m_arch, kernel)
        || !m_hip->link({ string_view(kernel.data(), kernel.size()),
                          amdgpu_rend_lib(m_arch),
                          string_view(artifact.data(), artifact.size()) },
                        code))
        return fail(m_hip->error());
    m_dev->module = m_hip->load_module(code);
    if (m_dev->module)
        m_dev->kernel = m_hip->kernel(m_dev->module, "osl_testshade_grid");
    if (!m_dev->kernel)
        return fail(m_hip->error());
    return true;
}



// The device memory every launch reads: the render context, the shading
// state block, an identity matrix (for null transforms) and the outputs,
// zeroed as the CPU's output images are.
bool
HipGridRenderer::init_device()
{
    HipRuntime& hip = *m_hip;
    void* block     = nullptr;
    long long size  = 0;
    if (!shadingsys->getattribute("shading_state_uniform", TypeDesc::PTR, &block)
        || !shadingsys->getattribute("shading_state_uniform:size",
                                     TypeDesc::LONGLONG, &size)
        || !block || size <= 0)
        return fail("no shading_state_uniform from the ShadingSystem");
    Device& d  = *m_dev;
    d.context  = (char*)hip.alloc(sizeof(RenderContext));
    d.uniform  = (char*)hip.alloc(size_t(size));
    d.identity = (char*)hip.alloc(sizeof(Matrix44));
    d.outputs  = (char*)hip.alloc(m_outputs_size ? m_outputs_size : 256);
    Matrix44 identity;
    identity.makeIdentity();
    if (!d.context || !d.uniform || !d.identity || !d.outputs
        || !hip.to_device(d.uniform, block, size_t(size))
        || !hip.to_device(d.identity, &identity, sizeof(identity))
        || !hip.fill(d.outputs, 0, m_outputs_size ? m_outputs_size : 256))
        return fail(hip.error());
    return upload_context(nullptr);  // no journal until render() has one
}



// The device RenderContext, with a device journal or none (null).
bool
HipGridRenderer::upload_context(void* journal)
{
    RenderContext rc {};
    export_context(rc);
    rc.journal_buffer = journal;
    shadingsys->getattribute("max_warnings_per_thread",
                             rc.max_warnings_per_thread);
    if (!m_hip->to_device(m_dev->context, &rc, sizeof(rc)))
        return fail(m_hip->error());
    m_dev->context_journal = journal;
    return true;
}



// After a launch: copy back the bookkeeping of the journal and, if any
// message was recorded (or did not fit), the journal itself for the reader.
bool
HipGridRenderer::read_journal()
{
    using journal::pvt::Organization;
    using journal::pvt::PageInfo;
    Device& d                 = *m_dev;
    uint8_t* jh               = d.journal_host.data();
    const Organization& org   = *reinterpret_cast<const Organization*>(jh);
    const uint32_t first_free = org.free_pos.load();
    if (!m_hip->to_host(jh, d.journal, org.calc_end_of_page_infos()))
        return fail(m_hip->error());
    bool written         = org.free_pos.load() != first_free
                           || org.additional_bytes_required.load()
                           || org.exceeded_page_size.load();
    const PageInfo* info = reinterpret_cast<const PageInfo*>(
        jh + sizeof(Organization));
    for (int t = 0; t < org.thread_count && !written; ++t)
        written = info[t].remaining != org.page_size;
    if (!written)
        return true;
    if (!m_hip->to_host(jh, d.journal,
                        std::min(org.free_pos.load(), org.buf_size)))
        return fail(m_hip->error());
    m_journal_reader(jh);
    return true;
}



void
HipGridRenderer::warmup()
{
    render(m_xres, m_yres);
}



// The grid in chunks of up to k_chunk_points points, one launch each. A
// chunk's device memory holds, for every point, its ShaderGlobals, closure
// pool, RenderState and group data; the ShaderGlobals are filled as on the
// CPU, then their pointers are made device pointers. The outputs stay in the
// device output buffer until finalize_pixel_buffer(); only the journal, if
// there is a reader, comes back after each launch.
void
HipGridRenderer::render(int xres, int yres)
{
    if (!m_ok || !m_dev || !m_dev->kernel || !m_setup)
        return;
    HipRuntime& hip = *m_hip;
    Device& d       = *m_dev;
    const int total = xres * yres;

    const size_t sg_stride    = round_up(sizeof(ShaderGlobals), 64);
    const size_t pool_stride  = round_up(sizeof(StackClosurePool), 64);
    const size_t state_stride = round_up(sizeof(RenderState), 16);
    const size_t gd_stride    = round_up(d.groupdata_size, 64);
    const size_t points       = size_t(std::min(total, k_chunk_points));
    const size_t off_sgs      = 0;
    const size_t off_pools    = off_sgs + points * sg_stride;
    const size_t off_states   = off_pools + points * pool_stride;
    const size_t off_gds     = round_up(off_states + points * state_stride, 64);
    const size_t chunk_bytes = off_gds + points * gd_stride;
    if (chunk_bytes > d.chunk_size) {
        hip.free(d.chunk);
        d.chunk      = nullptr;
        d.chunk_size = 0;
        d.chunk      = (char*)hip.alloc(chunk_bytes);
        if (!d.chunk) {
            fail(hip.error());
            return;
        }
        d.chunk_size = chunk_bytes;
        d.staging.assign(chunk_bytes, 0);
    }

    // The journal: its bookkeeping, a first page for each grid point, and
    // the pool for more pages.
    const bool journal   = bool(m_journal_reader);
    size_t journal_bytes = 0;
    if (journal) {
        journal_bytes
            = sizeof(journal::pvt::Organization)
              + points * (sizeof(journal::pvt::PageInfo) + k_journal_page)
              + m_journal_pool;
        if (journal_bytes > 0xffffffffu) {
            fail("the journal of a launch must be less than 4 GB");
            return;
        }
        if (journal_bytes > d.journal_size) {
            if (d.context_journal && !upload_context(nullptr))
                return;
            hip.free(d.journal);
            d.journal      = (char*)hip.alloc(journal_bytes);
            d.journal_size = d.journal ? journal_bytes : 0;
            if (!d.journal) {
                fail(hip.error());
                return;
            }
            d.journal_host.assign(journal_bytes, 0);
        }
    }
    void* context_journal = journal ? d.journal : nullptr;
    if (d.context_journal != context_journal
        && !upload_context(context_journal))
        return;

    // The device copy of a transform the ShaderGlobals point to (one copy
    // for each distinct host matrix), or the identity for none.
    auto device_xform = [&](TransformationPtr host) -> TransformationPtr {
        if (!host)
            return TransformationPtr(d.identity);
        for (auto& x : d.xforms)
            if (x.first == host)
                return TransformationPtr(x.second);
        char* dev = (char*)hip.alloc(sizeof(Matrix44));
        if (!dev || !hip.to_device(dev, host, sizeof(Matrix44)))
            return nullptr;
        d.xforms.emplace_back(host, dev);
        return TransformationPtr(dev);
    };

    char* const stage = d.staging.data();
    for (int first = 0; first < total; first += k_chunk_points) {
        const int n = std::min(k_chunk_points, total - first);
        for (int i = 0; i < n; ++i) {
            const int index       = first + i;
            const size_t pool_at  = off_pools + size_t(i) * pool_stride;
            const size_t state_at = off_states + size_t(i) * state_stride;
            ShaderGlobals sg;
            m_setup(sg, index % xres, index / xres);
            sg.renderstate         = d.chunk + state_at;
            sg.tracedata           = nullptr;
            sg.objdata             = nullptr;
            sg.context             = nullptr;
            sg.renderer            = nullptr;
            sg.shadingStateUniform = d.uniform;
            sg.Ci                  = nullptr;
            sg.thread_index        = i;  // its journal thread
            sg.shade_index         = index;
            sg.shader2common       = device_xform(sg.shader2common);
            sg.object2common       = device_xform(sg.object2common);
            if (!sg.shader2common || !sg.object2common) {
                fail(hip.error());
                return;
            }
            std::memcpy(stage + off_sgs + size_t(i) * sg_stride, &sg,
                        sizeof(sg));
            RenderState rs { reinterpret_cast<RenderContext*>(d.context),
                             reinterpret_cast<StackClosurePool*>(d.chunk
                                                                 + pool_at) };
            std::memcpy(stage + state_at, &rs, sizeof(rs));
            std::memset(stage + pool_at, 0, sizeof(StackClosurePool));
            void* next = d.chunk + pool_at;
            std::memcpy(stage + pool_at + 1024, &next, sizeof(next));
            if (d.groupdata_size)
                std::memset(stage + off_gds + size_t(i) * gd_stride, 0,
                            d.groupdata_size);
        }
        void* kernel_sgs          = d.chunk + off_sgs;
        void* kernel_gds          = d.chunk + off_gds;
        unsigned long long sg_str = sg_stride;
        unsigned long long gd_str = gd_stride;
        void* outputs             = d.outputs;
        void* interactive         = d.interactive;
        int kernel_first          = first;
        int kernel_n              = n;
        void* args[] = { &kernel_sgs, &sg_str,      &kernel_gds,   &gd_str,
                         &outputs,    &interactive, &kernel_first, &kernel_n };
        if (journal) {
            uint8_t* jh = d.journal_host.data();
            journal::initialize_buffer(jh, uint32_t(journal_bytes),
                                       k_journal_page, n);
            const auto& org = *reinterpret_cast<journal::pvt::Organization*>(
                jh);
            if (!hip.to_device(d.journal, jh, org.calc_end_of_page_infos())) {
                fail(hip.error());
                return;
            }
        }
        if (!hip.to_device(d.chunk, stage, chunk_bytes)
            || !hip.launch(d.kernel, unsigned((n + 63) / 64), 64, args)) {
            fail(hip.error());
            return;
        }
        if (journal && !read_journal())
            return;
    }
    m_rendered = true;
}



// Copy the outputs back into the output images.
void
HipGridRenderer::finalize_pixel_buffer()
{
    if (!m_ok || !m_rendered || !m_outputs_size)
        return;
    std::vector<char> host(m_outputs_size);
    if (!m_hip->to_host(host.data(), m_dev->outputs, m_outputs_size)) {
        fail(m_hip->error());
        return;
    }
    for (size_t k = 0; k < noutputs(); ++k) {
        OIIO::ImageBuf* ib = outputbuf(int(k));
        ib->set_pixels(OIIO::ROI::All(), ib->spec().format,
                       host.data() + m_output_offsets[k]);
    }
}



// The device GpuTexture of the file: the handle that the renderer services
// on the GPU (rs_simplerend.cpp) look up with.
RendererServices::TextureHandle*
HipGridRenderer::get_texture_handle(ustring filename, ShadingContext* context,
                                    const TextureOpt* options)
{
    if (!m_textures)
        return SimpleRenderer::get_texture_handle(filename, context, options);
    return reinterpret_cast<TextureHandle*>(
        m_textures->handle(filename, texturesys()));
}



void*
HipGridRenderer::device_alloc(size_t size)
{
    return m_hip ? m_hip->alloc(size) : nullptr;
}

void
HipGridRenderer::device_free(void* ptr)
{
    if (m_hip)
        m_hip->free(ptr);
}

void*
HipGridRenderer::copy_to_device(void* dst_device, const void* src_host,
                                size_t size)
{
    return m_hip && m_hip->to_device(dst_device, src_host, size) ? dst_device
                                                                 : nullptr;
}

#endif  // OSL_TESTSHADE_HIP

OSL_NAMESPACE_END
