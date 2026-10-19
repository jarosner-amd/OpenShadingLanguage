#!/usr/bin/env python

# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

# Render render-cornell's scene twice, on the CPU and with every shader run
# on an AMD GPU (testrender --amdgpu-run), and require identical images.
# Needs an AMD GPU and a build with OSL_ENABLE_AMDGPU, so it only runs with
# OSL_TEST_AMDGPU_RUN. testrender's own output names the GPU, so it goes to
# testrender.log instead of out.txt.

args = "-r 64 64 -aa 2 cornell.xml"
command = (osl_app("testrender") + " " + args + " cpu.exr > testrender.log 2>&1 ;\n")
command += (osl_app("testrender") + " --amdgpu-run " + args
            + " gpu.exr >> testrender.log 2>&1 ;\n")
command += oiiotool("--fail 0 --diff cpu.exr gpu.exr")

outputs = [ "out.txt" ]
