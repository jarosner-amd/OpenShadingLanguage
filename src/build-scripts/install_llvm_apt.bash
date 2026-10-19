#!/usr/bin/env bash

# Utility script to install LLVM and clang from apt.llvm.org, LLVM's own
# Ubuntu packages. They are built with all targets, including AMDGPU, and
# with ordinary (not LTO) static libraries, so gcc can link against them.
# Set LLVM_ROOT=/usr/lib/llvm-${LLVM_APT_VERSION} for the OSL build.
#
# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

# Exit the whole script if any command fails.
set -ex

LLVM_APT_VERSION=${LLVM_APT_VERSION:=20}
LLVM_APT_DISTRO=${LLVM_APT_DISTRO:=$(. /etc/os-release && echo $VERSION_CODENAME)}

sudo mkdir -p --mode=0755 /etc/apt/keyrings
wget -q -O - https://apt.llvm.org/llvm-snapshot.gpg.key \
    | gpg --dearmor | sudo tee /etc/apt/keyrings/llvm.gpg > /dev/null
echo "deb [signed-by=/etc/apt/keyrings/llvm.gpg] https://apt.llvm.org/${LLVM_APT_DISTRO}/ llvm-toolchain-${LLVM_APT_DISTRO}-${LLVM_APT_VERSION} main" \
    | sudo tee /etc/apt/sources.list.d/llvm.list
time sudo apt-get update -q
time sudo apt-get install -y --no-install-recommends \
    llvm-${LLVM_APT_VERSION}-dev clang-${LLVM_APT_VERSION} \
    libclang-${LLVM_APT_VERSION}-dev libclang-cpp${LLVM_APT_VERSION}-dev \
    zlib1g-dev libzstd-dev libxml2-dev

# Show what was installed, and that the AMDGPU target is in it.
/usr/lib/llvm-${LLVM_APT_VERSION}/bin/llvm-config --version
/usr/lib/llvm-${LLVM_APT_VERSION}/bin/llvm-config --targets-built
ls /usr/lib/llvm-${LLVM_APT_VERSION}/bin/{clang++,llvm-link,opt,llvm-dis,llc}
