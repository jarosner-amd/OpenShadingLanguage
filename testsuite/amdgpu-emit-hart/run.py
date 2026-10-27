#!/usr/bin/env python

# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

# OSL_USE_HART: with the ShadingSystem option amdgpu_hart_marker=1, every
# AMDGPU artifact carries the device-storage marker that HART requires;
# without the option, the artifacts have none. A value other than 0 or 1 is
# an error.

failureok = 1     # amdgpu_hart_marker=2 is expected to fail

checker = os.path.join(os.path.abspath(test_source_dir), "check_marker.py")

def amdgpu(group, options):
    return testshade("-g 1 1 --groupname " + group + " --layer L --amdgpu"
                     + " --amdgpu-arch gfx1201 --amdgpu-format llvmir"
                     + " --save-amdgpu" + options + " -o Cout null plain")

def check(args):
    return run_app('"' + pythonbin + '" "' + checker + '" ' + args)

command = amdgpu("hart", " --options amdgpu_hart_marker=1")
command += check("amdgpu_hart_gfx1201.ll")
command += amdgpu("none", "")
command += check("--absent amdgpu_none_gfx1201.ll")
command += amdgpu("bad", " --options amdgpu_hart_marker=2")

outputs = [ "out.txt" ]
