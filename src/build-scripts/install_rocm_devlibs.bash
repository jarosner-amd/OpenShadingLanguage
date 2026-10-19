#!/usr/bin/env bash

# Utility script to install the ROCm device libraries (ocml, ockl, oclc_*)
# and the HIP headers from AMD's apt repository, for building OSL with
# OSL_ENABLE_AMDGPU=ON. No GPU, driver or HIP runtime is installed.
#
# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

# Exit the whole script if any command fails.
set -ex

# The device libraries are bitcode, so OSL's LLVM must be able to read them:
# pick a ROCm whose LLVM is not newer than OSL's (ROCm 7.0 uses LLVM 20).
ROCM_VERSION=${ROCM_VERSION:=7.0.2}
ROCM_DISTRO=${ROCM_DISTRO:=$(. /etc/os-release && echo $VERSION_CODENAME)}

sudo mkdir -p --mode=0755 /etc/apt/keyrings
wget -q -O - https://repo.radeon.com/rocm/rocm.gpg.key \
    | gpg --dearmor | sudo tee /etc/apt/keyrings/rocm.gpg > /dev/null
echo "deb [arch=amd64 signed-by=/etc/apt/keyrings/rocm.gpg] https://repo.radeon.com/rocm/apt/${ROCM_VERSION} ${ROCM_DISTRO} main" \
    | sudo tee /etc/apt/sources.list.d/rocm.list
time sudo apt-get update -q
time sudo apt-get install -y --no-install-recommends rocm-device-libs hip-dev

# Show where the packages put the files, so a CMake error about a missing
# library or header is easy to trace.
dpkg -L rocm-device-libs | grep "/ocml.bc$"
ls -d /opt/rocm*/include/hip
