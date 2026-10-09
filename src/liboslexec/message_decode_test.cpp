// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

// Messages from shadeops reach the host as string hashes, and are decoded
// there with ustring::from_hash. Check that a hash the host does not know
// is reported as such instead of crashing the decoder.

#include <cstring>
#include <string>
#include <vector>

#include <OSL/encodedtypes.h>
#include <OSL/hashes.h>
#include <OSL/journal.h>
#include <OpenImageIO/unittest.h>

using namespace OSL;


namespace {

class CollectErrors final : public journal::Reporter {
public:
    std::vector<std::string> errors;
    void report_error(int, int, const string_view& message) override
    {
        errors.emplace_back(message);
    }
    void report_warning(int, int, const string_view&) override {}
    void report_print(int, int, const string_view&) override {}
    void report_file_print(int, int, const string_view&,
                           const string_view&) override
    {
    }
};

// A string that nothing in this process makes into a ustring.
constexpr const char* never_registered = "message_decode_test: not a ustring";

}  // namespace



// An unknown format or string argument decodes to "<unknown 0x...>", and the
// next message in the journal still decodes.
static void
test_unknown_hashes()
{
    alignas(64) static uint8_t buffer[64 * 1024];
    OIIO_CHECK_ASSERT(
        journal::initialize_buffer(buffer, sizeof(buffer), 4 * 1024, 1));
    journal::Writer writer(buffer);

    const uint64_t unknown         = strhash(never_registered);
    const std::string unknown_text = OSL::fmtformat("<unknown 0x{:016x}>",
                                                    unknown);

    // An unknown format with one int argument.
    EncodedType int_type = EncodedType::kInt32;
    int32_t int_value    = 42;
    uint8_t int_bytes[sizeof(int32_t)];
    std::memcpy(int_bytes, &int_value, sizeof(int_value));
    writer.record_errorfmt(0, 0, ustringhash::from_hash(unknown), 1, &int_type,
                           sizeof(int_bytes), int_bytes);

    // A known format with an unknown string argument.
    EncodedType str_type = EncodedType::kUstringHash;
    uint8_t str_bytes[sizeof(uint64_t)];
    std::memcpy(str_bytes, &unknown, sizeof(unknown));
    writer.record_errorfmt(0, 1, ustringhash("Lookup \"{}\" failed"), 1,
                           &str_type, sizeof(str_bytes), str_bytes);

    CollectErrors reporter;
    journal::Reader reader(buffer, reporter);
    reader.process();
    OIIO_CHECK_EQUAL(reporter.errors.size(), size_t(2));
    if (reporter.errors.size() == 2) {
        OIIO_CHECK_EQUAL(reporter.errors[0], unknown_text);
        OIIO_CHECK_EQUAL(reporter.errors[1],
                         "Lookup \"" + unknown_text + "\" failed");
    }
}



int
main(int /*argc*/, char* /*argv*/[])
{
    test_unknown_hashes();
    return unit_test_failures;
}
