#!/usr/bin/env python

# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

# String operations in a build with the AMDGPU device shadeops: hash() of a
# string known only at run time compiles; strlen() of one does not, and the
# error names the operation instead of leaving it for the renderer's link.
# Nor does a message whose name is known only at run time.

failureok = 1     # the strlen and message groups are expected to fail

checker = os.path.join(os.path.abspath(test_source_dir), os.pardir,
                       "amdgpu-emit", "check_amdgpu.py")

def amdgpu(group, shader):
    return testshade("-g 1 1 --groupname " + group + " --layer L --amdgpu"
                     + " --amdgpu-arch gfx1201 --amdgpu-format llvmir"
                     + " --save-amdgpu -o Cout null " + shader)

command = amdgpu("h1", "hash")
command += run_app('"' + pythonbin + '" "' + checker + '" --linked amdgpu_h1_gfx1201.ll')
command += amdgpu("s1", "strlen")
command += amdgpu("n1", "message_name")

outputs = [ "out.txt" ]
