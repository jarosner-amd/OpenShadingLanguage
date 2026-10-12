// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

// A GPU renderer gets OSL's uniform shading state as a block of bytes with
// getattribute("shading_state_uniform") and its size from
// getattribute("shading_state_uniform:size"). Check what that call promises.

#include <cstdint>
#include <type_traits>

#include <OSL/oslexec.h>
#include <OSL/rendererservices.h>

// Before unittest.h: it adds a float max() to namespace simd, and with
// OIIO 3.3 that hides the SIMD max() for fmath.h if fmath.h comes later.
#include "shading_state_uniform.h"

#include <OpenImageIO/unittest.h>

using namespace OSL;


// The documented promise "align the copy to at least 16 bytes" must be enough
// for the real struct.
static_assert(
    alignof(pvt::ShadingStateUniform) <= 16,
    "shading_state_uniform needs an alignment above the documented 16");



static void
test_block()
{
    RendererServices renderer;
    ShadingSystem ss(&renderer, nullptr, nullptr);

    // The values come from these attributes, so the block must follow them.
    OIIO_CHECK_ASSERT(ss.attribute("commonspace", "myworld"));
    OIIO_CHECK_ASSERT(ss.attribute("unknown_coordsys_error", 0));
    OIIO_CHECK_ASSERT(ss.attribute("max_warnings_per_thread", 7));
    OIIO_CHECK_ASSERT(ss.attribute("colorspace", "sRGB"));

    void* block    = nullptr;
    long long size = 0;
    OIIO_CHECK_ASSERT(
        ss.getattribute("shading_state_uniform", TypeDesc::PTR, &block));
    OIIO_CHECK_ASSERT(ss.getattribute("shading_state_uniform:size",
                                      TypeDesc::LONGLONG, &size));
    OIIO_CHECK_ASSERT(block != nullptr);
    OIIO_CHECK_EQUAL(size, (long long)sizeof(pvt::ShadingStateUniform));

    const auto* ssu = static_cast<const pvt::ShadingStateUniform*>(block);
    OIIO_CHECK_ASSERT(ssu->m_commonspace_synonym == ustringhash("myworld"));
    OIIO_CHECK_EQUAL(ssu->m_unknown_coordsys_error, false);
    OIIO_CHECK_EQUAL(ssu->m_max_warnings_per_thread, 7);
    OIIO_CHECK_ASSERT(ssu->m_colorsystem.colorspace() == ustringhash("sRGB"));

    // The older "colorsystem" attribute, which renderers already use, is the
    // first member of the same block.
    void* colorsystem = nullptr;
    OIIO_CHECK_ASSERT(
        ss.getattribute("colorsystem", TypeDesc::PTR, &colorsystem));
    OIIO_CHECK_ASSERT(colorsystem == block);

    // The block is live: a later change of an attribute shows in it.
    OIIO_CHECK_ASSERT(ss.attribute("max_warnings_per_thread", 11));
    OIIO_CHECK_EQUAL(ssu->m_max_warnings_per_thread, 11);
}



// A request with the wrong type fails and leaves the caller's value alone.
static void
test_wrong_type()
{
    RendererServices renderer;
    ShadingSystem ss(&renderer, nullptr, nullptr);

    int i = 123;
    OIIO_CHECK_ASSERT(
        !ss.getattribute("shading_state_uniform", TypeDesc::INT, &i));
    OIIO_CHECK_EQUAL(i, 123);
    OIIO_CHECK_ASSERT(
        !ss.getattribute("shading_state_uniform:size", TypeDesc::INT, &i));
    OIIO_CHECK_EQUAL(i, 123);
}



int
main(int /*argc*/, char* /*argv*/[])
{
    test_block();
    test_wrong_type();
    return unit_test_failures;
}
