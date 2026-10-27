#!/usr/bin/env python

# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

# Check the HART device-storage marker in an AMDGPU artifact saved as LLVM IR.
#   check_marker.py FILE.ll            the marker must be there, in the form
#                                      HART accepts
#   check_marker.py --absent FILE.ll   there must be no marker
# The value depends on the HART version, so it is not printed.

import re
import sys

absent = sys.argv[1] == "--absent"
path = sys.argv[-1]
with open(path) as f:
    lines = f.read().splitlines()

name = "@__hart_device_storage_abi_v1"
defs = [l for l in lines if l.startswith(name + " ")]
if absent:
    ok = not defs and not any(name in l for l in lines)
    print("marker absent: " + ("ok" if ok else "FAILED"))
    sys.exit(0 if ok else 1)

form = re.compile(re.escape(name)
                  + r" = internal addrspace\(4\) constant i64 \d+, align 8$")
used = [l for l in lines if l.startswith("@llvm.compiler.used ")]
checks = [
    ("one definition", len(defs) == 1),
    ("internal i64 constant, addrspace 4, align 8",
     len(defs) == 1 and form.match(defs[0]) is not None),
    ("in llvm.compiler.used", len(used) == 1 and name in used[0]),
]
for what, ok in checks:
    print("marker " + what + ": " + ("ok" if ok else "FAILED"))
sys.exit(0 if all(ok for _, ok in checks) else 1)
