# mathc

A small C++17 math library for graphics and game code. Vectors, matrices,
quaternions, and a handful of geometry helpers (ray, plane, box, frustum),
all in `namespace Math`.

It is written to behave like a simpler, self-contained GLM: the matrix
layouts and the transform conventions match what GLM uses, so swapping
between the two in your code is mostly a rename. Unlike GLM it ships no
templates and no dependencies — just a header plus an implementation file,
or a single-header build if you prefer.

## What's in it

- `Vec2`, `Vec3`, `Vec4` — arithmetic, dot/cross, length, normalize,
  lerp, min/max/clamp, per-component access via `.x/.y/...` or `[i]`.
- `Mat2`, `Mat3`, `Mat4` — arithmetic, transpose, determinant, inverse,
  plus the usual constructors: scale, rotation, translation, `LookAt`,
  `Perspective`, `Ortho`.
- `Quaternion` — Hamilton convention, so `(a * b)` applied to a vector
  rotates by `b` first, then `a`, matching matrix composition order.
  Axis-angle, Euler, from/to, look rotation, `Slerp`/`Lerp`, conversion
  to `Mat3`/`Mat4`.
- Higher-level types, guarded by `MATHC_NO_EXTRA`:
  - `Transform2D` — affine 2D transform (basis + origin) with TRS
    composition, pivots, and fast rigid inverse.
  - `Ray`, `Plane`, `Box`, `Frustum` — basic intersection and culling
    helpers.
- `Safe` variants where division by zero or a singular matrix is a real
  possibility: `NormalizedSafe()`, `InverseSafe()`, `InverseAffine()`.

Matrices are column-major and match GLM's memory layout, so `m[col]` is a
vector and `float*` data can be passed to OpenGL/Vulkan directly. Vectors
have a `union` of named fields and a `v[]` array, so `glVertexAttribPointer`
can consume `v` without a copy.

## Building

Requirements: CMake >= 3.15 and a C++17 compiler. GLM (`glm/glm.hpp`) is
needed for the test suite and the benchmarks; if it isn't found on the
include path those targets are skipped with a warning.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Build options:

| Option | Default | Effect |
| --- | --- | --- |
| `MATHC_BUILD_TESTS` | `ON` | GoogleTest suite, validated against GLM as reference |
| `MATHC_BUILD_BENCH` | `ON` | `bench_vec2/3/4`, `bench_mat4`, `bench_quaternion` vs GLM |
| `MATHC_NATIVE_ARCH` | `ON` | `-march=native` on the benchmark targets (x86_64 host builds only) |

The tests cover every public function against the corresponding GLM
operation. There is also `mathc_smoke_no_extra`, a dependency-free
executable that proves the single-header build compiles and links with
`MATHC_NO_EXTRA`.

CI (GitHub Actions) runs GCC and Clang, an ASan/UBSan pass, and a check
that `dist/mathc.h` is up to date with the sources.

## Using it

Two ways, pick whichever fits your project.

### As a library

Add `mathc.h` and `mathc.cpp` to your build and link against them:

```cpp
#include "mathc.h"

using namespace Math;

Vec3 p(1.0f, 2.0f, 3.0f);
float len = p.Length();
p = p.Normalized();

Mat4 view = Mat4::LookAt(Vec3(0, 0, 5), Vec3(0, 0, 0), Vec3(0, 1, 0));
Mat4 proj = Mat4::Perspective(60.0f * DEG2RAD, 16.0f / 9.0f, 0.1f, 100.0f);
Vec3 clip = (proj * view).TransformPointPerspective(p);

Quaternion q = Quaternion::FromAxisAngle(Vec3(0, 1, 0), 0.5f);
p = q * p;   // rotate p by 0.5 rad around the Y axis
```

### As a single header

`dist/mathc.h` is an amalgamated copy of `mathc.h` + `mathc.cpp`, the
classic stb pattern:

```cpp
// in exactly one .cpp of your project, before the include:
#define MATHC_IMPLEMENTATION
#include "mathc.h"
```

Every other translation unit just includes the header with no define.
If you only need the core math types, define `MATHC_NO_EXTRA` before the
include to strip `Transform2D`, `Ray`, `Plane`, `Box`, and `Frustum` from
the build.

`dist/mathc.h` is generated — don't edit it by hand. Regenerate it with:

```sh
python3 tools/amalgamate.py
```

after changing `mathc.h` or `mathc.cpp`.

## SIMD

`Vec4`, `Mat4`, and `Quaternion` are `alignas(16)`, and the implementation
uses GCC/Clang vector extensions (`__attribute__((vector_size(16)))`) so
the same source compiles to SSE2 on x86/x64, NEON on ARM, and wasm SIMD128
on Emscripten (with `-msimd128`). Other compilers, MSVC included, get a
plain scalar fallback via the `MATHC_SIMD` switch. Loads and stores go
through `memcpy` rather than union type punning to stay strict-aliasing
safe.

## Benchmarks

The `bench_*` targets run the same operation loop against mathc and GLM and
print wall-clock times and ops/sec, e.g.:

```sh
./build/bench_vec3
```

These are microbenchmarks and the numbers are only meaningful relative to
each other on your own machine. `MATHC_NATIVE_ARCH` is on by default for
x86_64 host builds so the comparison is on even footing with GLM.

## Layout

```
mathc.h              declarations
mathc.cpp            definitions (SIMD-aware)
dist/mathc.h         generated single-header distribution
tests/               GoogleTest suite + smoke_no_extra.cpp
bench/               mathc vs GLM microbenchmarks
tools/amalgamate.py  regenerates dist/mathc.h
```

## License

MIT. See [LICENSE](LICENSE).
