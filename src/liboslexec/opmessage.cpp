// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#include "oslexec_pvt.h"
#include "shadeops_strings.h"
#include <OSL/fmt_util.h>
#include <OSL/shaderglobals.h>


/////////////////////////////////////////////////////////////////////////
// Notes on how messages work:
//
// On the CPU the messages are stored in a MessageList in the
// ShadingContext. On AMDGPU each constant message name has a MessageSlot
// in the group's groupdata instead (see llvm_type_groupdata), used by
// osl_setmessage_slot and osl_getmessage_slot below with the same rules.
//
// FIXME -- setmessage only stores message values, not derivs, so
// getmessage only retrieves the values and has zero derivs.
// We should come back and fix this later.
//
// A closure is stored as its pointer; an array of closures as an array of
// pointers (the type becomes PTR with the same array length).
//


OSL_NAMESPACE_BEGIN
namespace pvt {


#if !OSL_GPU_DEVICE_COMPILE
// The CPU versions, using the ShadingContext's MessageList.

OSL_SHADEOP void
osl_setmessage(ShaderGlobals* sg, ustringhash_pod name_, long long type_,
               void* val, int layeridx, ustringhash_pod sourcefile_,
               int sourceline)
{
    auto name       = ustringhash_from(name_);
    auto sourcefile = ustringhash_from(sourcefile_);
    // recreate TypeDesc -- we just crammed it into an int!
    TypeDesc type   = TYPEDESC(type_);
    bool is_closure = type.basetype == TypeDesc::UNKNOWN;  // indicates closure
    if (is_closure)
        type.basetype = TypeDesc::PTR;  // for closures, we store a pointer

    MessageList& messages(sg->context->messages());
    const Message* m = messages.find(name);
    if (m) {
        if (m->name == name) {
            // message already exists?
            if (m->has_data()) {
                OSL::errorfmt(
                    sg,
                    "message \"{}\" already exists (created here: {}:{})"
                    " cannot set again from {}:{}",
                    name, m->sourcefile, m->sourceline, sourcefile, sourceline);
            } else {  // NOTE: this cannot be triggered when strict_messages=false because we won't record "failed" getmessage calls
                OSL::errorfmt(
                    sg,
                    "message \"{}\" was queried before being set (queried here: {}:{})"
                    " setting it now ({}:{}) would lead to inconsistent results",
                    name, m->sourcefile, m->sourceline, sourcefile, sourceline);
            }
            return;
        }
    }
    // The message didn't exist - create it
    messages.add(name, val, type, layeridx, sourcefile, sourceline);
}



OSL_SHADEOP int
osl_getmessage(ShaderGlobals* sg, ustringhash_pod source_,
               ustringhash_pod name_, long long type_, void* val, int derivs,
               int layeridx, ustringhash_pod sourcefile_, int sourceline)
{
    auto source     = ustringhash_from(source_);
    auto name       = ustringhash_from(name_);
    auto sourcefile = ustringhash_from(sourcefile_);

    // recreate TypeDesc -- we just crammed it into an int!
    TypeDesc type   = TYPEDESC(type_);
    bool is_closure = type.basetype == TypeDesc::UNKNOWN;  // indicates closure
    if (is_closure)
        type.basetype = TypeDesc::PTR;  // for closures, we store a pointer

    static ustring ktrace("trace");

    if (source == ustringhash_from(ktrace)) {
        // Source types where we need to ask the renderer
        return sg->renderer->getmessage(sg, source, name, type, val, derivs);
    }

    MessageList& messages(sg->context->messages());
    const Message* m = messages.find(name);
    if (m) {
        if (m->name == name) {
            if (m->type != type) {
                // found message, but types don't match
                OSL::errorfmt(
                    sg,
                    "type mismatch for message \"{}\" ({} as {} here: {}:{})"
                    " cannot fetch as {} from {}:{}",
                    name, m->has_data() ? "created" : "queried",
                    m->type == TypeDesc::PTR ? "closure color"
                                             : m->type.c_str(),
                    m->sourcefile, m->sourceline,
                    is_closure ? "closure color" : type.c_str(), sourcefile,
                    sourceline);
                return 0;
            }
            if (!m->has_data()) {
                // getmessage ran before and found nothing - just return 0
                return 0;
            }
            if (m->layeridx > layeridx) {
                // found message, but was set by a layer deeper than the one querying the message
                OSL::errorfmt(sg,
                              "message \"{}\" was set by layer #{} ({}:{})"
                              " but is being queried by layer #{} ({}:{})"
                              " - messages may only be transferred from nodes "
                              "that appear earlier in the shading network",
                              name, m->layeridx, m->sourcefile, m->sourceline,
                              layeridx, sourcefile, sourceline);
                return 0;
            }
            // Message found!
            size_t size = type.size();
            memcpy(val, m->data, size);
            if (derivs)  // TODO: move this to llvm code gen?
                memset(((char*)val) + size, 0, 2 * size);
            return 1;
        }
    }
    // Message not found -- we must record this event in case another layer tries to set the message again later on
    if (sg->context->shadingsys().strict_messages())
        messages.add(name, nullptr, type, layeridx, sourcefile, sourceline);
    return 0;
}
#endif  // !OSL_GPU_DEVICE_COMPILE



// The AMDGPU versions: the message lives in the MessageSlot that `slot`
// points to, inside the group's groupdata. The code generator resolves the
// name to its slot and passes everything that needs the host as constants:
// `type` is the TypeDesc with closures already as PTR, `size` its size,
// and the type names are ustring hashes for the error messages
// (`type_name` as osl_getmessage prints the querying type, `slot_type_name`
// as it prints a stored message's type). Defined on the host as well,
// where they are never called.

OSL_SHADEOP OSL_HOSTDEVICE void
osl_setmessage_slot(OpaqueExecContextPtr oec, void* slot_,
                    ustringhash_pod name_, long long type,
                    ustringhash_pod slot_type_name, int size, void* val,
                    int layeridx, ustringhash_pod sourcefile_, int sourceline)
{
    MessageSlot* m = reinterpret_cast<MessageSlot*>(slot_);
    if (m->state != MessageSlot::None) {
        OSL::errorfmt(oec,
                      m->state == MessageSlot::Set
                          ? ShadeopsHashes::message_exists
                          : ShadeopsHashes::message_queried_before_set,
                      ustringhash_from(name_), ustringhash_from(m->sourcefile),
                      m->sourceline, ustringhash_from(sourcefile_), sourceline);
        return;
    }
    m->state      = MessageSlot::Set;
    m->layeridx   = layeridx;
    m->type       = uint64_t(type);
    m->type_name  = slot_type_name;
    m->sourcefile = sourcefile_;
    m->sourceline = sourceline;
    memcpy(m + 1, val, size);
}



OSL_SHADEOP OSL_HOSTDEVICE int
osl_getmessage_slot(OpaqueExecContextPtr oec, void* slot_,
                    ustringhash_pod name_, long long type,
                    ustringhash_pod type_name, ustringhash_pod slot_type_name,
                    int size, void* val, int derivs, int layeridx,
                    ustringhash_pod sourcefile_, int sourceline, int strict)
{
    MessageSlot* m = reinterpret_cast<MessageSlot*>(slot_);
    if (m->state != MessageSlot::None) {
        if (m->type != uint64_t(type)) {
            // found message, but types don't match
            OSL::errorfmt(oec, ShadeopsHashes::message_type_mismatch,
                          ustringhash_from(name_),
                          m->state == MessageSlot::Set
                              ? ShadeopsHashes::created
                              : ShadeopsHashes::queried,
                          ustringhash_from(m->type_name),
                          ustringhash_from(m->sourcefile), m->sourceline,
                          ustringhash_from(type_name),
                          ustringhash_from(sourcefile_), sourceline);
            return 0;
        }
        if (m->state == MessageSlot::Queried) {
            // getmessage ran before and found nothing - just return 0
            return 0;
        }
        if (m->layeridx > layeridx) {
            // set by a layer deeper than the one querying the message
            OSL::errorfmt(oec, ShadeopsHashes::message_wrong_layer_order,
                          ustringhash_from(name_), m->layeridx,
                          ustringhash_from(m->sourcefile), m->sourceline,
                          layeridx, ustringhash_from(sourcefile_), sourceline);
            return 0;
        }
        memcpy(val, m + 1, size);
        if (derivs)
            memset(((char*)val) + size, 0, 2 * size);
        return 1;
    }
    // Not found: record the query, in case a later setmessage tries to set
    // it (only with strict_messages, as on the CPU).
    if (strict) {
        m->state      = MessageSlot::Queried;
        m->layeridx   = layeridx;
        m->type       = uint64_t(type);
        m->type_name  = slot_type_name;
        m->sourcefile = sourcefile_;
        m->sourceline = sourceline;
    }
    return 0;
}


}  // namespace pvt
OSL_NAMESPACE_END
