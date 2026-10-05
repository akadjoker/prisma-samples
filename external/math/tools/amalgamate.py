#!/usr/bin/env python3
"""
Generates the distributable single-header dist/mathc.h from the development
mathc.h + mathc.cpp pair. Run after any change to either file.

Follows the classic stb_image.h pattern: definitions inside the
MATHC_IMPLEMENTATION block use plain (non-inline, non-static) linkage, same
as mathc.cpp today. The user is responsible for defining
MATHC_IMPLEMENTATION in exactly one .cpp of their project before the
#include, exactly like STB_IMAGE_IMPLEMENTATION — not `inline`, which was
tried first and reverted: a plain-inline definition that a given TU never
calls itself can be silently omitted by the compiler (the same "undefined
reference" bug hit early on with mathc.cpp), which breaks the common case of
a dedicated implementation TU that only does `#define ... / #include ...`
and is never itself a call site.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "mathc.h"
SOURCE = ROOT / "mathc.cpp"
OUT = ROOT / "dist" / "mathc.h"


def main():
    header_text = HEADER.read_text()
    cpp_text = SOURCE.read_text()

    cpp_text = re.sub(r'^#include "mathc\.h"\n', '', cpp_text, count=1, flags=re.MULTILINE)

    # The source header may use either a bare #endif or the documented
    # comment. Match the final include-guard terminator rather than relying
    # on its exact whitespace/comment formatting.
    marker_match = re.search(
        r"^#endif(?:[ \t]+//[ \t]*MATHC_INCLUDE_MATHC_H)?[ \t]*(?:\n)?\Z",
        header_text,
        flags=re.MULTILINE,
    )
    if marker_match is None:
        print("ERROR: end-of-header marker not found in mathc.h", file=sys.stderr)
        sys.exit(1)
    decl_part = header_text[:marker_match.start()]
    tail = header_text[marker_match.end():]
    marker = "#endif // MATHC_INCLUDE_MATHC_H"

    impl_block = (
        "#ifdef MATHC_IMPLEMENTATION\n\n"
        + cpp_text.rstrip("\n") + "\n"
        + "\n#endif // MATHC_IMPLEMENTATION\n\n"
    )

    banner = (
        "// mathc.h -- single-header math library (generated, do not edit by hand)\n"
        "// Source: mathc.h + mathc.cpp in the mathc repo, run tools/amalgamate.py to regenerate.\n"
        "//\n"
        "// Usage (stb-style): in exactly ONE .cpp file of your project, do\n"
        "//     #define MATHC_IMPLEMENTATION\n"
        "//     #include \"mathc.h\"\n"
        "// Every other file just does #include \"mathc.h\" with no define.\n"
        "// Optionally #define MATHC_NO_EXTRA before including to drop\n"
        "// Transform2D/Ray/Plane/Box/Frustum from the build entirely.\n\n"
    )
    result = banner + decl_part.rstrip("\n") + "\n\n" + impl_block + marker + tail

    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text(result)
    print(f"Wrote {OUT} ({len(result)} bytes, {result.count(chr(10))} lines)")


if __name__ == "__main__":
    main()
