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

Device-free tests need valid shader bytecode, written out as tokens, and independent expected
translation/dataflow. The opt-in SDL_GPU cases also render literal native pixel references.

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
cube/volume samplers for the two-coordinate AR/GB instructions. It also checks
96 TEXDP3/DP3TEX dot/dataflow variants and four explicit incompatible DP3TEX maps.
Only TEXM3X2DEPTH still has its named missing-emitter refusal.

The opt-in `texreg2_native_programs` SDL_GPU case creates 48 real SPIR-V program
pairs and checks their mapped sampler reflection and allocation cleanup. These
are native shader-creation checks. The separately selectable `texreg2_native_pixels`
and `texdp3_native_pixels` cases compare complete native images; downstream framework
integration remains a separate qualification.

### Mapped dependent-texture native pixels (TQ-TEXREG-PIXELS-01)

`cna_sdlgpu_tests texreg2_native_pixels` is an opt-in real SDL_GPU test
(`MOJOSHADER_CNA_SDLGPU_DEVICE=1`, private display, `SDL_ASSERT=abort`). It exercises
TEXREG2AR/GB in 2D and TEXREG2RGB in 2D, cube and volume forms, including RGB_bx2,
through all six legal t0..t3 register pairs: 48 draws, each checking all 8x8 pixels.
A `(32,96,160,224)` source selects a literal texel in distinct point-clamped
lookup images. Cube face/coordinates follow Vulkan's cube-map selection table;
volume texels use floor(4*coordinate), then clamp. The source arithmetic and
literal expected colors are recorded in `cna_texreg_pixels.h`.

This invokes MojoShader's existing explicit sampler-map API and renders actual
native SPIR-V. It does not qualify FNA3D/CNA's inference of legacy sampler types
from effect metadata or bound textures. Default device-free runs skip this GPU
case; shader-creation-only coverage remains separately selectable as
`texreg2_native_programs`. ShaderCross is not the route creating these shaders.

### Dependent texture dots (TQ-TEXDP3-01)

SPIR-V and GLSPIRV now emit `texdp3` as dot(UVW,RGB) broadcast to all RGBA lanes,
and `texdp3tex` as a destination-stage lookup at (dot,0), using the existing 2D
sampler representation for this one-dimensional lookup. Legal source `_bx2`
scaling applies before the dot. No destination sampler is declared for TEXDP3.

The numeric oracle covers both shader versions1.2/1.3, both profiles, every
source<destination pair among t0..t3, and both source scaling modes:96 variants.
Distinct per-stage input coordinates and a large alpha component distinguish
wrong input registers and DP4. The native `texdp3_native_pixels` case checks
all64 pixels in24 draws; TEXDP3's ordinary RGBA bytes are88/96/104 by destination
stage, while signed output clamps to0. TEXDP3TEX checks literal texels selected
at y=0. Run device cases with the private runner and `SDL_ASSERT=abort`.

### Zero-result pixel observation (TQ-TEXREG-ZERO-01)

Native targets start with a distinct nonzero RGBA clear color. A signed TEXDP3
result of zero therefore requires an actual fragment write. A valid SPIR-V
control-flow mutation that kills only those fragments passed with the former
zero background; the nonzero background catches all six dropped-output cases.
All existing literal color expectations remain in place. The mutation is restored
byte-for-byte before the pristine native harness is run.
