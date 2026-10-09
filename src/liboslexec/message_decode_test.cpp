// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

// Messages from shadeops reach the host as string hashes, and are decoded
// there with ustring::from_hash. Check that every string the shadeops pass
// is known to the host once liboslexec is loaded, that a hash the host
// does not know is reported as such instead of crashing the decoder, and
// that a format given as a character array hashes like its text.

#include <cstring>
#include <string>
#include <vector>

#include <OSL/encodedtypes.h>
#include <OSL/fmt_util.h>
#include <OSL/hashes.h>
#include <OSL/journal.h>
#include <OpenImageIO/unittest.h>

#include "shadeops_strings.h"

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



// The host holds every string the shadeops pass as a hash (shadingsys.cpp
// makes them into ustrings when liboslexec is loaded). Hash with strhash:
// making a ustringhash from the text would register it here and prove
// nothing.
static void
test_shadeops_strings_registered()
{
    OIIO_CHECK_ASSERT(ustring::from_hash(strhash(never_registered)).c_str()
                      == nullptr);
#define CHECK_REGISTERED(str, var_name)                                     \
    OIIO_CHECK_EQUAL(pvt::ShadeopsHashes::var_name.hash(), strhash(str));   \
    OIIO_CHECK_ASSERT(ustring::from_hash(strhash(str)).c_str() != nullptr); \
    OIIO_CHECK_EQUAL(ustring::from_hash(strhash(str)).string(),             \
                     std::string(str));
    OSL_SHADEOPS_STRINGS(CHECK_REGISTERED)
#undef CHECK_REGISTERED
}



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



// printfmt and its siblings hash their format with pvt::fmt_spec_hash. A
// character array must hash like its text up to the first zero, whether it
// is a string literal or a larger buffer holding a shorter string. On the
// host, ustringhash(str, len) stops at the first zero by itself; on a GPU
// device it hashes all len bytes, so fmt_spec_hash must find the end. Only
// the unterminated array tells the two apart here, on the host.
static void
test_fmt_spec_hash()
{
    OIIO_CHECK_EQUAL(pvt::fmt_spec_hash("Lookup failed").hash(),
                     strhash("Lookup failed"));
    char buffer[64] = "Lookup failed";
    OIIO_CHECK_EQUAL(pvt::fmt_spec_hash(buffer).hash(),
                     strhash("Lookup failed"));
    const char const_buffer[64] = "Lookup failed";
    OIIO_CHECK_EQUAL(pvt::fmt_spec_hash(const_buffer).hash(),
                     strhash("Lookup failed"));
    const char unterminated[3] = { 'a', 'b', 'c' };
    OIIO_CHECK_EQUAL(pvt::fmt_spec_hash(unterminated).hash(), strhash("abc"));
}



int
main(int /*argc*/, char* /*argv*/[])
{
    test_shadeops_strings_registered();
    test_unknown_hashes();
    test_fmt_spec_hash();
    return unit_test_failures;
}
