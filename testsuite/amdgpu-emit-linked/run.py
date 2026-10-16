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

run_llc = os.path.join(os.path.abspath(test_source_dir), "run_llc.py")

# Check what was emitted, then compile it to a device object with llc.
def check(artifact):
    return (run_app('"' + pythonbin + '" "' + checker + '" --linked ' + artifact)
            + run_app('"' + pythonbin + '" "' + run_llc + '" ' + artifact))

archs = "--amdgpu-arch gfx1100 --amdgpu-arch gfx1201"
command = amdgpu("g1", "test", archs + " --amdgpu-format llvmir")
command += amdgpu("g2", "ops", archs + " --amdgpu-format llvmir")
command += amdgpu("g3", "ops", "--amdgpu-arch gfx1201")
# Two explicit entry layers, A and B, and a last layer C that is neither an
# entry nor used, so it is not compiled. Each entry layer gets its own
# export, and the fused export runs init, A and B.
command += testshade("-g 1 1 --groupname g5 --layer A entry_a --layer B entry_b"
                     + " --layer C entry_c --entry A --entry B --amdgpu"
                     + " --amdgpu-arch gfx1100 --amdgpu-format llvmir"
                     + " --save-amdgpu -o Cout null")
# Shadeops that reach the renderer only through rs_* functions: the
# artifact declares rs_* functions and no osl_* ones.
command += amdgpu("g6", "services", "--amdgpu-arch gfx1201 --amdgpu-format llvmir")
# Texture, trace and point cloud calls, with the option setters built in.
command += amdgpu("g7", "lookups", "--amdgpu-arch gfx1201 --amdgpu-format llvmir")
# Closures, with their memory from rs_allocate_closure.
command += amdgpu("g8", "closures", "--amdgpu-arch gfx1201 --amdgpu-format llvmir")
# Messages from layer A to layer B, kept in the group's own data.
command += testshade("-g 1 1 --groupname g9 --layer A message_set"
                     + " --layer B message_get --connect A dummy B dummy"
                     + " --amdgpu --amdgpu-arch gfx1201 --amdgpu-format llvmir"
                     + " --save-amdgpu -o Cout null")
# Getters the renderer names (testshade with --use_rs_bitcode) are declared.
command += amdgpu("g10", "getters", "--use_rs_bitcode --amdgpu-arch gfx1201"
                  + " --amdgpu-format llvmir")
for f in [ "amdgpu_g1_gfx1100.ll", "amdgpu_g1_gfx1201.ll",
           "amdgpu_g2_gfx1100.ll", "amdgpu_g2_gfx1201.ll",
           "amdgpu_g3_gfx1201.bc", "amdgpu_g5_gfx1100.ll",
           "amdgpu_g6_gfx1201.ll", "amdgpu_g7_gfx1201.ll",
           "amdgpu_g8_gfx1201.ll", "amdgpu_g9_gfx1201.ll",
           "amdgpu_g10_gfx1201.ll" ] :
    command += check(f)

outputs = [ "out.txt" ]
