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
