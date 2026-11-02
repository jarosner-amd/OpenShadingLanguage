// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#include "amdgpu_textures.h"

#include <algorithm>
#include <string>
#include <vector>

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/texture.h>

#include "render_state.h"
#include "hiploader.h"

OSL_NAMESPACE_BEGIN

AmdgpuTextures::AmdgpuTextures(HipRuntime& hip, OIIO::ErrorHandler& errhandler)
    : m_hip(hip), m_errhandler(errhandler)
{
}



AmdgpuTextures::~AmdgpuTextures()
{
    for (auto& t : m_textures) {
        m_hip.free(t.second.device);
        m_hip.destroy_texture(t.second.object);
        m_hip.destroy_texture(t.second.env_object);
    }
}



void*
AmdgpuTextures::handle(ustring filename, TextureSystem* texturesys)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto found = m_textures.find(filename);
    if (found == m_textures.end())
        found = m_textures.emplace(filename, load(filename, texturesys)).first;
    return found->second.device;
}



// Every mip level of the file as 4 floats per texel; channels the file
// does not have are 0, as the CPU texture system's default fill. Returns
// the device GpuTexture and the texture objects.
AmdgpuTextures::Record
AmdgpuTextures::load(ustring filename, TextureSystem* texturesys)
{
    Record none;
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
            m_errhandler.errorfmt("Could not read {} (mip level {}): {}",
                                  filename, level, image.geterror());
            return none;
        }
        levels.push_back(std::move(l));
    }
    if (levels.empty()) {
        m_errhandler.errorfmt("Could not read {}", filename);
        return none;
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
    // The wrap modes the file asks for, as the CPU texture system takes
    // them when a lookup does not name any: black unless the file says
    // otherwise ("wrapmodes" holds one mode, or one for s and one for t).
    OIIO::ImageBuf top(filename);
    top.init_spec(filename, 0, 0);
    std::string wrap_s = "black", wrap_t = "black";
    std::string wraps = top.spec().get_string_attribute("wrapmodes");
    if (!wraps.empty()) {
        size_t comma = wraps.find(',');
        wrap_s       = wraps.substr(0, comma);
        wrap_t = comma == std::string::npos ? wrap_s : wraps.substr(comma + 1);
    }
    Record rec;
    if (!m_hip.create_textures(levels, wrap_s, wrap_t, rec.object,
                               rec.env_object)) {
        m_errhandler.errorfmt("Could not make a GPU texture of {}: {}",
                              filename, m_hip.error());
        return none;
    }
    // What environment() needs besides the texels: which way is up, as
    // OpenImageIO decides it, and the height for clamping at the poles.
    // The texture system's default ("latlong_up", y unless set otherwise),
    // unless the file says.
    GpuTexture info;
    info.object     = rec.object;
    info.env_object = rec.env_object;
    info.y_up       = true;
    const char* up  = nullptr;
    if (texturesys
        && texturesys->getattribute("latlong_up", OIIO::TypeString, &up) && up)
        info.y_up = (up[0] == 'y');
    std::string file_up = top.spec().get_string_attribute("oiio:updirection");
    if (file_up == "y")
        info.y_up = true;
    else if (file_up == "z")
        info.y_up = false;
    info.height = levels[0].height;
    rec.device  = m_hip.alloc(sizeof(info));
    if (!rec.device || !m_hip.to_device(rec.device, &info, sizeof(info))) {
        m_hip.free(rec.device);
        m_hip.destroy_texture(rec.object);
        m_hip.destroy_texture(rec.env_object);
        m_errhandler.errorfmt("Could not copy the GPU texture of {}: {}",
                              filename, m_hip.error());
        return none;
    }
    return rec;
}

OSL_NAMESPACE_END
