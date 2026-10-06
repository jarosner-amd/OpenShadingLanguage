#!/usr/bin/env python

# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

# Print stable facts about one AMDGPU artifact saved by
# "testshade --amdgpu --save-amdgpu", so the test can compare them with a
# reference. Only facts that do not depend on the LLVM version or the machine
# are printed: never the data layout, attribute group numbers or value names.

import re
import sys


def check_bitcode(data):
    print("  bitcode magic: " + ("ok" if data[:4] == b"BC\xc0\xde" else "BAD"))


def check_llvm_ir(text, linked):
    triple = re.search(r'^target triple = "([^"]*)"', text, re.M)
    print("  triple: " + (triple.group(1) if triple else "MISSING"))

    # Attribute groups: "attributes #N = { ... }". The "target-cpu" attribute
    # lives in these groups, not on the "define" line.
    groups = {}
    for m in re.finditer(r"^attributes #(\d+) = \{(.*)\}", text, re.M):
        groups[m.group(1)] = m.group(2)

    exports = []
    cpus = set()
    with_cpu = 0
    defines = 0
    for m in re.finditer(r"^define\b(.*)$", text, re.M):
        line = m.group(1)
        defines += 1
        name = re.search(r"@([\w.$]+)\(", line)
        if name and name.group(1).startswith("__direct_callable__"):
            exports.append(name.group(1))
        # Function attribute groups follow the closing parenthesis of the
        # parameter list.
        tail = line[line.rfind(")"):]
        found = set()
        for g in re.findall(r"#(\d+)", tail):
            found.update(re.findall(r'"target-cpu"="([^"]*)"',
                                    groups.get(g, "")))
        if found:
            with_cpu += 1
            cpus.update(found)

    print("  exports: %d" % len(exports))
    for e in sorted(exports):
        print("    " + e)
    print("  target-cpu: " + (" ".join(sorted(cpus)) if cpus else "none"))
    print("  every define has target-cpu: "
          + ("yes" if defines and with_cpu == defines else "no"))
    cov = re.search(r'"amdhsa_code_object_version", i32 (\d+)', text)
    print("  code object version: " + (cov.group(1) if cov else "MISSING"))

    # A non-zero integer turned into a pointer is almost certainly a host
    # address that means nothing on the GPU. This catches both the constant
    # expression and the instruction form. It does not catch a host pointer
    # kept as a plain i64 constant.
    ptrs = re.findall(r"inttoptr\s*\(?\s*i64\s+(-?[1-9]\d*)", text)
    print("  host pointers: " + (" ".join(ptrs) if ptrs else "none"))
    print("  declares osl noise: "
          + ("yes" if re.search(r"^declare\b.*@osl_\w*noise\w*\(", text, re.M)
             else "no"))

    # With the device shadeops linked in, every shadeop they define is
    # inlined, so any osl_* function still declared or called comes from a
    # source the device shadeops do not include yet.
    if linked:
        declared = set(re.findall(r"^declare\b.*@(osl_\w+)\(", text, re.M))
        called = set(re.findall(r"\bcall\b[^@\n]*@(osl_\w+)\(", text))
        print("  osl functions declared: "
              + (" ".join(sorted(declared)) if declared else "none"))
        print("  osl functions called: "
              + (" ".join(sorted(called)) if called else "none"))


def main():
    args = sys.argv[1:]
    linked = "--linked" in args
    if linked:
        args.remove("--linked")
    if len(args) != 1:
        print("usage: check_amdgpu.py [--linked] <artifact.bc|artifact.ll>")
        return 1
    path = args[0]
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        print("check " + path + ": file not found")
        return 1
    print("check " + path + ":")
    if not data:
        print("  empty file")
        return 1
    if path.endswith(".bc"):
        check_bitcode(data)
    else:
        check_llvm_ir(data.decode("utf-8", "replace"), linked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
