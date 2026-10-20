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
# pick a ROCm whose LLVM is not newer than OSL's (ROCm 6.4 uses LLVM 19,
# ROCm 7.0 and 7.1 use LLVM 20, ROCm 7.2 uses LLVM 22, ROCm 7.14 uses
# LLVM 23). The HIP headers must also match the clang that compiles them:
# those of ROCm 7.2 and older do not compile with clang 23.
#
# ROCm up to 7.2 comes from repo.radeon.com and installs to /opt/rocm.
# ROCm 7.13 and newer come from repo.amd.com (the repository AMD's ROCm
# install guide uses), with new package names, and install to
# /opt/rocm/core-<major.minor>; set ROCM_PATH to that folder.
ROCM_VERSION=${ROCM_VERSION:=7.0.2}
ROCM_DISTRO=${ROCM_DISTRO:=$(. /etc/os-release && echo $VERSION_CODENAME)}
ROCM_MAJMIN=$(echo ${ROCM_VERSION} | cut -d. -f1-2)

sudo mkdir -p --mode=0755 /etc/apt/keyrings
if [[ $(printf '%s\n' 7.13 ${ROCM_MAJMIN} | sort -V | head -1) == 7.13 ]] ; then
    UBUNTU_DIR=ubuntu$(. /etc/os-release && echo $VERSION_ID | tr -d .)
    wget -q -O - https://repo.amd.com/rocm/packages-multi-arch/gpg/rocm.gpg \
        | gpg --dearmor | sudo tee /etc/apt/keyrings/rocm.gpg > /dev/null
    echo "deb [arch=amd64 signed-by=/etc/apt/keyrings/rocm.gpg] https://repo.amd.com/rocm/packages-multi-arch/${UBUNTU_DIR} stable main" \
        | sudo tee /etc/apt/sources.list.d/rocm.list
    time sudo apt-get update -q
    # amdrocm-llvm has the device libraries, amdrocm-runtime-dev the HIP
    # headers. The packages are named by major.minor; apt takes the newest
    # patch release of it.
    time sudo apt-get install -y --no-install-recommends \
        amdrocm-llvm${ROCM_MAJMIN} amdrocm-runtime-dev${ROCM_MAJMIN}
    ls /opt/rocm/core-${ROCM_MAJMIN}/lib/llvm/amdgcn/bitcode/ocml.bc
    ls -d /opt/rocm/core-${ROCM_MAJMIN}/include/hip
else
    wget -q -O - https://repo.radeon.com/rocm/rocm.gpg.key \
        | gpg --dearmor | sudo tee /etc/apt/keyrings/rocm.gpg > /dev/null
    echo "deb [arch=amd64 signed-by=/etc/apt/keyrings/rocm.gpg] https://repo.radeon.com/rocm/apt/${ROCM_VERSION} ${ROCM_DISTRO} main" \
        | sudo tee /etc/apt/sources.list.d/rocm.list
    # Ubuntu has its own, older HIP packages (hipcc, libamdhip64-dev, ...)
    # with higher version numbers than AMD's. Without this pin apt mixes
    # them in, and their headers in /usr/include/hip shadow the ROCm ones.
    printf 'Package: *\nPin: release o=repo.radeon.com\nPin-Priority: 600\n' \
        | sudo tee /etc/apt/preferences.d/rocm-pin-600
    time sudo apt-get update -q
    time sudo apt-get install -y --no-install-recommends rocm-device-libs hip-dev
    # Show where the packages put the files, so a CMake error about a
    # missing library or header is easy to trace.
    dpkg -L rocm-device-libs | grep "/ocml.bc$"
    ls -d /opt/rocm*/include/hip
fi

if [ -e /usr/include/hip ] ; then
    echo "Unexpected HIP headers in /usr/include/hip (not from ROCm)"
    exit 1
fi
