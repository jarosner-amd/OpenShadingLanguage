<!-- SPDX-License-Identifier: CC-BY-4.0 -->
<!-- Copyright Contributors to the Open Shading Language Project. -->

Building OSL
============

OSL currently compiles and runs cleanly on Linux (x86_64), Mac OS X (x86_64
and aarch64), and Windows (x86_64). It may build and run on other platforms as
well, but we don't officially support or test other than these platforms.

Shader execution is supported on the native architectures of those x86_64 and
aarch64 platforms, a special batched 4-, 8- or 16-wide SIMD execution mode
requiring x86_64 with SSE2, AVX/AVX2 or AVX-512 instructions, as well as on
NVIDIA GPUs using Cuda+OptiX. OSL can also compile shader groups for AMD
GPUs (experimental and compile-only, see [AMD GPUs](#amd-gpus) below).

Dependencies
------------

OSL requires the following dependencies or tools.
NEW or CHANGED minimum dependencies since the last major release are **bold**.

* Build system: [CMake](https://cmake.org/) 3.23 or newer (tested
  through 4.2)

* A suitable C++17 compiler to build OSL itself, which may be any of:
   - GCC 9.3 or newer (tested through gcc 14)
   - Clang 5 or newer (tested through clang 23)
   - Microsoft Visual Studio 2017 or newer
   - **Intel LLVM-based icx compiler version 2022 or newer** (note: the classic `icc` compiler is no longer supported).

* [OpenImageIO](http://openimageio.org) 3.0 or newer (tested through 3.1
  and main)

    OSL uses OIIO both for its texture mapping functionality as well as
    numerous utility classes.  If you are integrating OSL into an existing
    renderer, you may use your own favorite texturing system rather than
    OpenImageIO with a little minor surgery.  There are only a few places
    where OIIO texturing calls are made, and they could easily be bypassed.
    But it is probably not possible to remove OIIO completely as a
    dependency, since we so heavily rely on a number of other utility classes
    that it provides (for which there was no point reinventing redundantly
    for OSL).

    After building OpenImageIO, if you don't have it installed in a
    "standard" place (like /usr/include), you should set the environment
    variable `$OpenImageIO_ROOT` to point to the compiled distribution, and
    then OSL's build scripts will be able to find it. You should also have
    $OpenImageIO_ROOT/lib to be in your LD_LIBRARY_PATH (or
    DYLD_LIBRARY_PATH on OS X).

* [LLVM](http://www.llvm.org) **14.0 or newer**, 15, 16, 17, 18, 19, 20, 21,
  22, 23, including clang libraries.

* (optional) For GPU rendering on NVIDIA GPUs:
    * [OptiX](https://developer.nvidia.com/rtx/ray-tracing/optix) 7.0 or higher.
    * [Cuda](https://developer.nvidia.com/cuda-downloads) 9.0 or higher. It is
      recommended that you use 11.0 or higher.

* (optional) For compiling shader groups for AMD GPUs with the device
  shadeops built in (`OSL_ENABLE_AMDGPU`, experimental, see
  [AMD GPUs](#amd-gpus) below):
    * LLVM 19 or newer with the AMDGPU target, and the `clang++`,
      `llvm-link`, `opt`, `llvm-dis` and `llc` tools from that same LLVM
      installation (configure checks that their versions match exactly).
    * [ROCm](https://rocm.docs.amd.com/) HIP headers and device libraries,
      including `oclc_abi_version_600.bc` and an `oclc_isa_version_<N>.bc`
      for each architecture. OSL's LLVM must be able to read this bitcode,
      so ROCm's own LLVM must not be newer than OSL's; configure checks
      this.
    * An OpenImageIO with HIP device support (device macros and fast-math
      functions for HIP). This is not yet in an OpenImageIO release.

* [Imath](https://github.com/AcademySoftwareFoundation/Imath) 3.1 or newer.
* [Flex](https://github.com/westes/flex) 2.5.35 or newer and
  [GNU Bison](https://www.gnu.org/software/bison/) 2.7 or newer.
  Note that on some MacOS/xcode releases, the system-installed Bison is too
  old, and it's better to install a newer Bison (via Homebrew is one way to
  do this easily).
* [PugiXML](http://pugixml.org/) >= 1.8 (we have tested through 1.16).
* (optional) [Partio](https://www.disneyanimation.com/technology/partio.html)
  If it is not found at build time, the OSL `pointcloud` functions will not
  be operative.
* (optional) Python: If you are building the Python bindings or running the
  testsuite:
    * **Python >= 3.9** (tested through 3.14)
    * NumPy (tested through 2.4)
    * A binding framework, depending on `OSL_PYTHON_BINDINGS_BACKEND` (see
      [Python binding backends](#python-binding-backends) below):
        * pybind11 >= 2.7 (tested through 3.0) -- needed for the `pybind11`
          backend and for `both`. It is the auto-selected default when
          OpenImageIO is older than 3.2 or Python is older than 3.10.
        * nanobind >= 2.8.0 (tested through 3.0), with Python >= 3.10 -- needed
          for the `nanobind` backend and for `both`. It is the auto-selected
          default when OpenImageIO is 3.2 or newer and Python is 3.10 or newer.
          Usually installed as a Python package (`pip install nanobind`, or
          `brew install nanobind`), which is enough: the build locates it by
          asking the interpreter. If it is not installed, the build fetches and
          builds it locally (it is a small header/CMake package, not a
          compiled library).
* (optional) Qt5 >= 5.6 or Qt6 (tested Qt5 through 5.15 and Qt6 through 6.10).
  If not found at build time, the `osltoy` application will be disabled.



Build process
-------------

Here are the steps to check out, build, and test the OSL distribution:

1. Install and build dependencies.

2. Check out a copy of the source code from the Git repository:

        git clone https://github.com/AcademySoftwareFoundation/OpenShadingLanguage.git osl

3. Change to the distribution directory and 'make'

        cd osl
        make

   Note: OSL uses 'CMake' for its cross-platform build system.  But for
   simplicity, we have made a "make wrapper" around it, so that by just
   typing 'make' everything will build.  Type 'make help' for other
   options, and note that 'make nuke' will blow everything away for the
   freshest possible compile.

   You can also ignore the top level Makefile wrapper, and instead use
   CMake directly:

       cmake -B build -S .
       cmake --build build --target install

   NOTE: If the build breaks due to compiler warnings which have been elevated
   to errors, you can try "make clean" followed by "make STOP_ON_WARNING=0",
   or if using cmake directly, add `-DSTOP_ON_WARNING=0` to the cmake
   configuration command. That will create a build that will only stop for
   full errors, not warnings.

4. After compilation, you'll end up with a full OSL distribution in
   dist/

5. Add the "dist/bin" to your `$PATH`, and "dist/lib" to your
   `$LD_LIBRARY_PATH` (or `$DYLD_LIBRARY_PATH` on MacOS), or copy the contents
   of those files to appropriate directories.  Public include files
   (those needed when building applications that incorporate OSL)
   can be found in "dist/include", and documentation can be found
   in "dist/share/doc".

6. After building (and setting your library path), you can run the
   test suite with:

        make test

AMD GPUs
--------

AMD GPU support is experimental and compile-only. OSL can compile a shader
group to AMDGPU LLVM bitcode (or LLVM IR text), one artifact per GPU
architecture, for a renderer to finish and run. OSL does not run shaders on
AMD GPUs itself.

A renderer selects it with the ShadingSystem attributes `gpu_backend`
("amdgpu"), `gpu_archs` and `gpu_artifact_kind` ("bitcode", the default, or
"llvmir"), and reads the results with the shader group attributes
`gpu_num_artifacts` and `gpu_artifact:N:...`; see `oslexec.h`. These
attributes cannot be changed after the first shader group is compiled, and
"amdgpu" cannot be used with a renderer that supports OptiX.

`testshade --amdgpu` does the same from the command line (not together with
`--optix` or `--batched`), for example:

    testshade --amdgpu --amdgpu-arch gfx1100 --save-amdgpu \
              --groupname mygroup -o Cout null myshader

`-o` names the shader output to compile for; replace `Cout` with an output
of your shader. This writes `amdgpu_mygroup_gfx1100.bc` to the current
directory.

This needs LLVM 19 or newer with the AMDGPU target, and one of two builds:

* With `USE_LLVM_BITCODE=OFF` (the default on Windows), nothing more is
  needed, but the OSL shadeops (the library functions shaders call) stay
  external declarations in the artifact.
* With `USE_LLVM_BITCODE=ON`, `OSL_ENABLE_AMDGPU` must also be ON, and the
  shadeops are built into each artifact. Otherwise compiling for AMDGPU
  fails with an error.

To build the shadeops in, configure with:

    cmake -B build -S . -DUSE_LLVM_BITCODE=ON -DOSL_ENABLE_AMDGPU=ON \
          -DROCM_PATH=/opt/rocm -DOSL_HIP_TARGET_ARCHS="gfx1100;gfx1201"

* `OSL_ENABLE_AMDGPU` (default OFF) compiles the device shadeops for each
  architecture in `OSL_HIP_TARGET_ARCHS` with LLVM's `clang++` in HIP mode,
  links the ROCm device libraries into them, and embeds the result in
  liboslexec. It needs `USE_LLVM_BITCODE=ON`.
* `OSL_HIP_TARGET_ARCHS` (default "gfx1100;gfx1201") lists the
  architectures. Artifacts can then be compiled for these architectures
  only.
* `ROCM_PATH` (default: the `ROCM_PATH` environment variable at the first
  configure) is the ROCm installation with the HIP headers and device
  libraries.

Built in: the math, noise and spline shadeops, matrices and named spaces,
color functions, the printf, error and warning shims, the texture, trace and
point cloud shadeops, the texture and trace option setters, closures, and
messages between the layers of a group (`setmessage`, `getmessage`).
Not built in yet: the string operations that need a string table
(`opstring.cpp`), and `getattribute` and interpolated parameters
(`osl_get_attribute`, `osl_bind_interpolated_param`).
A shader group that needs one of these does not compile for AMDGPU; the
error names the missing operations. A renderer must not define OSL's own
`osl_*` functions.
Messages are kept in the group's data (`llvm_groupdata_size` includes
them), so their names must be known when the group is compiled: a message
name, or a `getmessage` source, that is computed while shading is an error.
A `getmessage` with the source `"trace"` still goes to `rs_trace_get`.
Printing a closure (`printf("%s", Ci)`) is not supported on GPUs.

What the renderer supplies. The built-in shadeops reach the renderer only
through the `rs_*` functions of `OSL/rs_free_function.h` (named spaces,
texture, point cloud and trace lookups, closure memory through
`rs_allocate_closure`, `rs_errorfmt` and its siblings). The
renderer implements them and links them with each artifact itself; OSL does
not link renderer code. The renderer must also give every shading point a
`ShaderGlobals` whose `shadingStateUniform` points to a copy of OSL's uniform
shading state in device memory (the color system, the name of "common" space,
whether an unknown coordinate system is an error, and the warning limit), and
whose `renderstate` and transform pointers are valid on the device. The
renderer gets that state as a block of bytes with the ShadingSystem
attributes `shading_state_uniform` and `shading_state_uniform:size`, and
copies it as it is; its layout is private to OSL. This interface is
experimental. The texture handle that the renderer's
`get_texture_handle()` returns must be valid on the device, or null; see the
`gpu_backend` attribute in `oslexec.h`. OSL fills the texture options that
`rs_texture` and its siblings receive. Two of their fields hold something
other than their declared type: `subimagename` holds the 64-bit hash of the
name (a `ustringhash`), not a `ustring`, and `missingcolor` points to the
shader's own copy of the color, valid only during the call.
Closures must be registered before the group is compiled, with no `prepare`
or `setup` function: those would be host addresses in the artifact.

Python binding backends
-----------------------

OSL's Python bindings (the `oslquery` module, wrapping `OSLQuery`) can be
built with either [pybind11](https://github.com/pybind/pybind11) or
[nanobind](https://github.com/wjakob/nanobind). Both are generated from one
set of sources and expose exactly the same Python API; which one you get is a
build-time choice:

    cmake -B build -S .                                          # auto (see below)
    cmake -B build -S . -DOSL_PYTHON_BINDINGS_BACKEND=nanobind
    cmake -B build -S . -DOSL_PYTHON_BINDINGS_BACKEND=pybind11
    cmake -B build -S . -DOSL_PYTHON_BINDINGS_BACKEND=both

or equivalently by setting an environment variable of the same name.

When `OSL_PYTHON_BINDINGS_BACKEND` is left unset, OSL auto-selects `nanobind`
when both of these hold, and `pybind11` otherwise:

* OpenImageIO is 3.2 or newer. Reading `OSLQuery.Parameter.type` (see below)
  needs OSL's and OpenImageIO's Python modules to have been built with the
  same binding framework, and OpenImageIO switched its own default to nanobind
  in 3.2; matching it keeps `type` working out of the box.
* Python is 3.10 or newer (nanobind's minimum).

nanobind does not have to be installed for this -- if it is missing the build
fetches and builds it locally. If that local build is not possible in your
environment, configure with `-DOSL_PYTHON_BINDINGS_BACKEND=pybind11`.

With `pybind11` or `nanobind`, you get a single `oslquery` module installed in
the usual place, and it makes no difference to Python code which one it is.
With `both`, the pybind11 module keeps the ordinary location and the nanobind
one is installed alongside it under a `nanobind/` subdirectory of the
site-packages directory; put that subdirectory on `PYTHONPATH` to import it
instead. `both` exists so that the testsuite can run against each backend and
confirm they agree; it is not intended for deployment.

Why this is a choice at all: `OSLQuery.Parameter.type` returns an OpenImageIO
`TypeDesc`, and reading that attribute only works if OpenImageIO's own Python
module has been imported *and* was built with the same binding framework as
OSL's. (Each framework keeps its own registry of bound C++ types, and they
cannot see each other's.) So if you use that attribute, build OSL's bindings
to match whatever OpenImageIO you are pairing them with. Otherwise the
attribute raises `TypeError`.

Everything else in the module is free of that constraint, and
`Parameter.type_name` -- a plain string such as `"color"` or `"float[4]"` --
gives you the same information with no coupling to OpenImageIO at all. Prefer
it. `type` is retained for backward compatibility.

Conda Environment
-----------------

To simplify installation of Python and other dependencies, you can use
the provided Conda environment setup script located at `src/build-scripts/` 
by running:

    source src/build-scripts/configure_conda_env.bash

**This script will:**
  * Check for Miniconda installation.
  * Create a Conda environment named `osl-env` if it doesn't exist.
  * Install all required dependencies into the environment.
  * Activate the environment for the current shell session.

After running this script, the `osl-env` environment will be created, and
all you need to do when opening a new shell session is simply activate the 
Conda environment.

**When to use it:**  
Run this script after cloning the repository and before building OSL. It 
sets up a consistent development environment without manually installing 
all dependencies. If you already have all required dependencies installed, 
running it is optional.

Troubleshooting
----------------

- [Build issues on macOS Catalina (fatal error: 'wchar.h' file not found)](https://github.com/AcademySoftwareFoundation/OpenShadingLanguage/issues/1055#issuecomment-581920327)
