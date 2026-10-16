#!/usr/bin/env python

# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

# Link an AMDGPU shader group artifact with testrender's renderer services
# for the same architecture (rs_simplerend_amdgpu_<arch>.bc, built by
# testrender's CMake), and print every function the result still only
# declares, other than LLVM intrinsics. A renderer supplies its rs_*
# functions this way, so the list must be empty.
#
#   link_check.py artifact_<...>_<arch>.ll
#
# Needs OSL_AMDGPU_LLVM_LINK, OSL_AMDGPU_LLVM_DIS and OSL_TESTRENDER_RS_DIR
# (the folder with the renderer service bitcode) in the environment.

import os
import re
import subprocess
import sys

link = os.environ.get("OSL_AMDGPU_LLVM_LINK", "")
dis = os.environ.get("OSL_AMDGPU_LLVM_DIS", "")
rs_dir = os.environ.get("OSL_TESTRENDER_RS_DIR", "")
if not (link and dis and rs_dir):
    print("error: OSL_AMDGPU_LLVM_LINK, OSL_AMDGPU_LLVM_DIS and "
          "OSL_TESTRENDER_RS_DIR must be set")
    sys.exit(1)

artifact = sys.argv[1]
m = re.search(r"_(gfx[0-9a-f]+)\.[a-z]+$", artifact)
if not m:
    print("error: no architecture in the file name " + artifact)
    sys.exit(1)
arch = m.group(1)
rs = os.path.join(rs_dir, "rs_simplerend_amdgpu_" + arch + ".bc")

linked = artifact + ".linked.bc"
result = subprocess.run([link, artifact, rs, "-o", linked],
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        universal_newlines=True)
if result.returncode != 0:
    print(result.stdout, end="")
    print("link " + artifact + ": FAILED")
    sys.exit(1)
result = subprocess.run([dis, linked, "-o", "-"], stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, universal_newlines=True)
if result.returncode != 0:
    print(result.stdout, end="")
    print("llvm-dis " + linked + ": FAILED")
    sys.exit(1)
undefined = sorted(set(
    f for f in re.findall(r"^declare\b.*?@([\w.$]+)\(", result.stdout, re.M)
    if not f.startswith("llvm.")))
print("link " + artifact + " with the renderer services: "
      + ("complete" if not undefined
         else "still undefined: " + " ".join(undefined)))
sys.exit(1 if undefined else 0)
