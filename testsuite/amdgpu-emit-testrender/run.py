#!/usr/bin/env python

# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

# Compile every shader group of a testrender scene for AMDGPU, without
# rendering, in a build with the AMDGPU device shadeops. Each artifact is
# checked like those of amdgpu-emit-linked, compiled with llc, and linked
# with testrender's own renderer services for AMDGPU, which must supply
# every rs_* function it calls. This needs no GPU.

checker = os.path.join(os.path.abspath(test_source_dir), os.pardir,
                       "amdgpu-emit", "check_amdgpu.py")
run_llc = os.path.join(os.path.abspath(test_source_dir), os.pardir,
                       "amdgpu-emit-linked", "run_llc.py")
link_check = os.path.join(os.path.abspath(test_source_dir), "link_check.py")

command = testrender("--amdgpu --amdgpu-arch gfx1201 --amdgpu-format llvmir"
                     + " --save-amdgpu scene.xml")
for i in range(5) :
    f = "amdgpu_" + str(i) + "_group_gfx1201.ll"
    for script, args in [ (checker, "--linked " + f), (run_llc, f),
                          (link_check, f) ] :
        command += run_app('"' + pythonbin + '" "' + script + '" ' + args)

outputs = [ "out.txt" ]
