# libcna fork regression tests

Tests for defects fixed on this fork's `cna` branch. Each `cna_*_tests.c` is one executable and one
CTest test; `cna_test.h` holds the few helpers they share.

    cmake -S <mojoshader> -B <build> -DMOJOSHADER_CNA_TESTS=ON
    cmake --build <build>
    ctest --test-dir <build> --output-on-failure

The tests compile their own copy of the library with every profile enabled (HLSL and Metal too)
and with FNA3D's switches, independent of the `PROFILE_*` options of the build around them. When
`spirv-val` (SPIRV-Tools) is on the `PATH` at configure time, every SPIR-V module a test produces is
also validated by it; without it those checks are reported as skipped. Likewise, when the shaderc
library (glslang's HLSL front end; the runtime library is enough, no headers) is found,
`cna_hlsl_metal_tests` compiles every HLSL source it produces. There is no Metal compiler to run, so
Metal sources are checked by their structure only.

A test needs only valid shader bytecode, written out as tokens in the test, and its expected
translation. Nothing here renders.

`cna_sdlgpu_tests` covers the SDL_GPU glue and is built only when CMake finds an SDL3 package
(`-DCMAKE_PREFIX_PATH=<SDL3 install>`). It uses the dynamic SDL_shadercross path, and on Linux the
build makes a stand-in `libSDL3_shadercross.so.0` for it, so its cases without a GPU device run
anywhere. The cases that create a GPU device run only with `MOJOSHADER_CNA_SDLGPU_DEVICE=1`, which
belongs on a private display, not on a desktop someone is using; under AddressSanitizer add
`ASAN_OPTIONS=detect_leaks=0`, since Vulkan drivers leave process allocations behind:

    MOJOSHADER_CNA_SDLGPU_DEVICE=1 ASAN_OPTIONS=detect_leaks=0 ctest --test-dir <build> -R sdlgpu

`-DMOJOSHADER_CNA_SDL_SHADERCROSS_DIR=<dir>` puts a real `libSDL3_shadercross.so.0` there instead of
the stand-in, and the device cases then also link a program through it.

`cna_texreg_spirv_tests` checks TEXREG2AR/GB/RGB with 204 literal coordinate and
result-dataflow variants across both SPIR-V profiles, valid shader versions,
register pairs, mapped texture types and RGB signed scaling. It interprets standard
SPIR-V operations, validates each module, and checks eight explicit refusals of
cube/volume samplers for the two-coordinate AR/GB instructions. The other three
legacy texture opcodes retain their exact named refusals.

The opt-in `texreg2_native_programs` SDL_GPU case creates 48 real SPIR-V program
pairs and checks their mapped sampler reflection and allocation cleanup. These
are module/dataflow and native shader-creation checks. Rendered pixel comparison
through a consuming framework remains an integration step.
