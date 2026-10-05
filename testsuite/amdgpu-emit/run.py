#!/usr/bin/env python

# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

# Compile shader groups for AMDGPU without running them ("emit-only"), and
# check what was emitted. This needs no GPU and no ROCm. testshade prints one
# line per artifact; check_amdgpu.py prints stable facts about each saved file.
# Each group has its own name, so the saved files never overwrite each other.

def amdgpu(group, shader, args):
    return testshade("-g 1 1 --groupname " + group + " --layer L --amdgpu "
                     + args + " --save-amdgpu -o Cout null " + shader)

def check(artifact):
    return run_app('"' + pythonbin + '" data/check_amdgpu.py ' + artifact)

# Two architectures, as bitcode and as LLVM IR text.
archs = "--amdgpu-arch gfx1100 --amdgpu-arch gfx1201"
command = amdgpu("g1", "test", archs)
command += amdgpu("g1", "test", archs + " --amdgpu-format llvmir")
for f in [ "amdgpu_g1_gfx1100.bc", "amdgpu_g1_gfx1201.bc",
           "amdgpu_g1_gfx1100.ll", "amdgpu_g1_gfx1201.ll" ] :
    command += check(f)

# No architecture: one generic artifact, which must carry no target-cpu. The
# shader calls shadeops, which stay as external declarations.
command += amdgpu("g2", "ops", "--amdgpu-format llvmir")
command += check("amdgpu_g2_generic.ll")

outputs = [ "out.txt" ]
