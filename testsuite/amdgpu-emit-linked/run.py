#!/usr/bin/env python

# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

# Compile shader groups for AMDGPU without running them, in a build with the
# AMDGPU device shadeops (USE_LLVM_BITCODE and OSL_ENABLE_AMDGPU), and check
# what was emitted. Each artifact has the device shadeops for its own
# architecture linked in. This needs no GPU. The checker is the one of the
# amdgpu-emit test.

checker = os.path.join(os.path.abspath(test_source_dir), os.pardir,
                       "amdgpu-emit", "check_amdgpu.py")

def amdgpu(group, shader, args):
    return testshade("-g 1 1 --groupname " + group + " --layer L --amdgpu "
                     + args + " --save-amdgpu -o Cout null " + shader)

def check(artifact):
    return run_app('"' + pythonbin + '" "' + checker + '" --linked ' + artifact)

archs = "--amdgpu-arch gfx1100 --amdgpu-arch gfx1201"
command = amdgpu("g1", "test", archs + " --amdgpu-format llvmir")
command += amdgpu("g2", "ops", archs + " --amdgpu-format llvmir")
command += amdgpu("g3", "ops", "--amdgpu-arch gfx1201")
for f in [ "amdgpu_g1_gfx1100.ll", "amdgpu_g1_gfx1201.ll",
           "amdgpu_g2_gfx1100.ll", "amdgpu_g2_gfx1201.ll",
           "amdgpu_g3_gfx1201.bc" ] :
    command += check(f)

outputs = [ "out.txt" ]
