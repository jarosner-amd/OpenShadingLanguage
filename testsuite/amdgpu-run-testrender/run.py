#!/usr/bin/env python

# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

# Render scenes twice, on the CPU and with every shader run on an AMD GPU
# (testrender --amdgpu-run), and compare. Needs an AMD GPU and a build with
# OSL_ENABLE_AMDGPU, so it only runs with OSL_TEST_AMDGPU_RUN. testrender's
# own output names the GPU, so it goes to testrender.log instead of out.txt.
#
# render-cornell's scene uses no textures: the images must be identical.
#
# render-background's scene (background.xml, with a smaller importance
# table) lights everything with a textured environment map. The GPU filters
# texture lookups differently from OpenImageIO's texture system, so the
# paths, and the noise, differ from pixel to pixel. Averages over 8x6
# regions must agree within 0.08 (they differ by at most 0.045 today, and
# by 0.18 when large lookups are not averaged on the GPU).
#
# environment.xml is the same scene, with the background a lat-long
# environment map looked up with environment(). The map is a generated
# colour ramp, so that a wrong orientation shows: averages over 8x6 regions
# must agree within 0.03 (0.01 today, 0.16 with the map upside down).
# kitchen_probe.hdr is not used: it has black rows, for which the importance
# table of testrender's background gives NaN, on the CPU too.

def render(scene, size, name) :
    args = size + " " + scene
    return (osl_app("testrender") + " " + args + " cpu_" + name
            + ".exr >> testrender.log 2>&1 ;\n"
            + osl_app("testrender") + " --amdgpu-run " + args + " gpu_" + name
            + ".exr >> testrender.log 2>&1 ;\n")

command = render("cornell.xml", "-r 64 64 -aa 2", "cornell")
command += oiiotool("--fail 0 --diff cpu_cornell.exr gpu_cornell.exr")
command += render("background.xml", "-r 64 48 -aa 4", "background")
command += oiiotool("cpu_background.exr --resize 8x6 -o cpu_regions.exr",
                    silent=True)
command += oiiotool("gpu_background.exr --resize 8x6 -o gpu_regions.exr",
                    silent=True)
command += oiiotool("--fail 0.08 --warn 0.08 --diff cpu_regions.exr"
                    + " gpu_regions.exr")
command += oiiotool("-pattern fill:topleft=0.1,0.1,0.1:topright=1,0.1,0.1"
                    + ":bottomleft=0.1,1,0.1:bottomright=1,1,1 256x128 3"
                    + " -d half -oenv ramp.env", silent=True)
command += render("environment.xml", "-r 64 48 -aa 4", "environment")
command += oiiotool("cpu_environment.exr --resize 8x6 -o cpu_env_regions.exr",
                    silent=True)
command += oiiotool("gpu_environment.exr --resize 8x6 -o gpu_env_regions.exr",
                    silent=True)
command += oiiotool("--fail 0.03 --warn 0.03 --diff cpu_env_regions.exr"
                    + " gpu_env_regions.exr")

outputs = [ "out.txt" ]
