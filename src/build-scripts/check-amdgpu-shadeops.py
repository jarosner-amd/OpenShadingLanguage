#!/usr/bin/env python

# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

# Check the AMDGPU device shadeops built for one GPU architecture, given as
# LLVM IR text, against the host shadeops bitcode. Needs no GPU.
#
# Usage: check-amdgpu-shadeops.py <llvm-dis> <host.bc> <arch> <code-object-version> <device.ll>
#
# Prints every problem found and exits non-zero if there is any.

import re
import subprocess
import sys

# Linkage keywords that keep a definition out of the module's exports.
LOCAL = r"\b(internal|private|linkonce_odr|weak_odr|linkonce|available_externally)\b"
# Parameter and return attributes, which may differ between targets.
ATTRS = (r"\b(noundef|nonnull|readonly|writeonly|nocapture|signext|zeroext|"
         r"inreg|noalias|returned|dead_on_unwind|writable|nofree|"
         r"captures\([^)]*\)|align \d+|dereferenceable(_or_null)?\(\d+\)|"
         r"range\([^)]*\)|nofpclass\([^)]*\)|initializes\([^)]*\))")


def prototypes(text):
    """Map each defined osl_* function to (return type, parameter types)."""
    protos = {}
    for m in re.finditer(r"^define ([^@]*)@(osl_\w+)\(([^)]*)\)", text, re.M):
        ret = re.sub(ATTRS, "", m.group(1)).split()[-1]
        params = tuple(re.sub(ATTRS, "", p).split()[0]
                       for p in m.group(3).split(",") if p.strip())
        protos[m.group(2)] = (ret, params)
    return protos


def main():
    if len(sys.argv) != 6:
        print("usage: check-amdgpu-shadeops.py <llvm-dis> <host.bc> <arch> "
              "<code-object-version> <device.ll>")
        return 2
    llvm_dis, host_bc, arch, cov, device_ll = sys.argv[1:]
    text = open(device_ll, encoding="utf-8").read()
    host = subprocess.run([llvm_dis, host_bc, "-o", "-"], check=True,
                          capture_output=True, text=True).stdout
    problems = []

    triple = re.search(r'^target triple = "([^"]*)"', text, re.M)
    if not triple or triple.group(1) != "amdgcn-amd-amdhsa":
        problems.append("target triple is not amdgcn-amd-amdhsa")

    flag = re.search(r'"amdhsa_code_object_version", i32 (\d+)', text)
    if not flag or flag.group(1) != str(int(cov) * 100):
        problems.append("code object version is not " + cov)

    # Every definition must be built for this architecture: the inliner
    # needs the group's functions and the shadeops to agree on it.
    groups = dict(re.findall(r"^attributes #(\d+) = \{(.*)\}$", text, re.M))
    cpus = set()
    for line in re.findall(r"^define\b(.*)$", text, re.M):
        tail = line[line.rfind(")"):]
        found = set()
        for g in re.findall(r"#(\d+)", tail):
            found.update(re.findall(r'"target-cpu"="([^"]*)"',
                                    groups.get(g, "")))
        cpus.update(found or {"(none)"})
    if cpus != {arch}:
        problems.append("target-cpu is %s, expected %s"
                        % (" ".join(sorted(cpus)), arch))

    # The device math libraries must be linked in, not left to the consumer.
    for name in re.findall(r"^declare\b.*@(__ocml_\w+|__ockl_\w+|__oclc_\w+)\(",
                           text, re.M):
        problems.append("device library function not linked in: " + name)

    if re.search(r"^define\b.*\bamdgpu_kernel\b", text, re.M):
        problems.append("defines a kernel")

    # The device shadeops must define every shadeop the host bitcode
    # defines, with the same prototype: shader groups are generated against
    # those prototypes.
    host_protos = prototypes(host)
    device_protos = prototypes(text)
    for name, proto in sorted(host_protos.items()):
        if name not in device_protos:
            problems.append("missing shadeop: " + name)
        elif device_protos[name] != proto:
            problems.append("prototype differs from the host: " + name)
    exported = [m.group(2) for m in
                re.finditer(r"^define ([^@]*)@(osl_\w+)\(", text, re.M)
                if not re.search(LOCAL, m.group(1))]

    for p in problems:
        print(arch + ": " + p)
    print("%s: %d shadeops exported, %d problems"
          % (arch, len(exported), len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
