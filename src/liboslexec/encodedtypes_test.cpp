// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

// OSL::decode_message (OSL/encodedtypes.h) is what a renderer calls to turn
// the format hash and encoded arguments it receives in rs_errorfmt and its
// siblings into text. This test is a separate program linked with
// liboslexec, as a renderer is, so it also checks that the function is
// exported.

#include <cstdint>
#include <cstring>
#include <string>

#include <OSL/encodedtypes.h>
#include <OSL/oslconfig.h>

#include <OpenImageIO/unittest.h>

using namespace OSL;



static void
test_decode_message()
{
    // The strings must be ustrings in this process, as they would be for a
    // renderer, so that their hashes can be turned back into text.
    ustring format("{} is {:.2f}, {}");
    ustring name("answer");

    const EncodedType types[] = { EncodedType::kUstringHash,
                                  EncodedType::kFloat, EncodedType::kInt32 };
    uint8_t values[sizeof(uint64_t) + sizeof(float) + sizeof(int32_t)];
    uint64_t name_hash = name.hash();
    float f            = 0.5f;
    int32_t i          = 42;
    std::memcpy(values, &name_hash, sizeof(name_hash));
    std::memcpy(values + sizeof(name_hash), &f, sizeof(f));
    std::memcpy(values + sizeof(name_hash) + sizeof(f), &i, sizeof(i));

    std::string text = "not overwritten";
    int used         = decode_message(format.hash(), 3, types, values, text);
    OIIO_CHECK_EQUAL(text, "answer is 0.50, 42");
    OIIO_CHECK_EQUAL(used, int(sizeof(values)));
}



int
main(int /*argc*/, char* /*argv*/[])
{
    test_decode_message();
    return unit_test_failures;
}
