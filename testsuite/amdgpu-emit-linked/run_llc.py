#!/usr/bin/env python

# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

# Compile an emitted AMDGPU artifact to a device object file with llc, for
# the architecture in its file name (amdgpu_<group>_<arch>.<ext>). llc also
# verifies the module. Prints one stable line on success; on failure prints
# llc's errors and exits with an error, which fails the test.

import os
import re
import subprocess
import sys

llc = os.environ.get("OSL_AMDGPU_LLC", "")
if not llc:
    print("error: OSL_AMDGPU_LLC is not set")
    sys.exit(1)

artifact = sys.argv[1]
m = re.search(r"_(gfx[0-9a-f]+)\.[a-z]+$", artifact)
if not m:
    print("error: no architecture in the file name " + artifact)
    sys.exit(1)
arch = m.group(1)

result = subprocess.run([llc, "-mtriple=amdgcn-amd-amdhsa", "-mcpu=" + arch,
                         "-filetype=obj", artifact, "-o", artifact + ".o"],
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        universal_newlines=True)
if result.returncode != 0:
    print(result.stdout, end="")
    print("llc " + arch + " " + artifact + ": FAILED")
    sys.exit(1)
print("llc " + arch + " " + artifact + ": ok")
