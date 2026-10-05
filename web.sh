#!/bin/sh
# Builds the demos listed in web_demos.txt for the browser with Emscripten (WebGL 2) and copies
# each to <out>/<name>/ as <name>.html, .js, .wasm and .data.
#
#   ./web.sh [build-dir] [out-dir]
#
# Needs the Emscripten SDK on the path (source emsdk_env.sh), glslangValidator, a native build of
# this project (the shader tool spirv-cross comes from its build folder) and the media folder in
# PRISMA_MEDIA_DIR of that native build or in the environment variable PRISMA_MEDIA.
root="$(cd "$(dirname "$0")" && pwd)"
build="${1:-build-web}"
out="${2:-web-out}"
native="${NATIVE_BUILD:-$root/build}"

cd "$root" || exit 1
if ! command -v emcmake > /dev/null 2>&1; then
    echo "web.sh: emcmake not found, source emsdk_env.sh first"
    exit 1
fi
spirv="$(command -v spirv-cross || echo "$native/spirv-cross/spirv-cross")"
if [ ! -x "$spirv" ]; then
    echo "web.sh: spirv-cross not found; build the project natively first (cmake -S . -B build && cmake --build build)"
    exit 1
fi
media="${PRISMA_MEDIA:-$(sed -n 's/^PRISMA_MEDIA_DIR:PATH=//p' "$native/CMakeCache.txt" 2> /dev/null)}"
if [ -z "$media" ]; then
    echo "web.sh: set PRISMA_MEDIA to the media folder"
    exit 1
fi

emcmake cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release -DPRISMA_GLES=ON -DPRISMA_VULKAN=OFF \
    -DSAMPLES_BUILD_TESTS=OFF -DPRISMA_SPIRV_CROSS="$spirv" -DPRISMA_MODELS_DIR= \
    -DPRISMA_MEDIA_DIR="$media" || exit 1

status=0
mkdir -p "$out/thumbs"
cards=""
while IFS='|' read -r name folder title text; do
    case "$name" in ''|'#'*) continue ;; esac
    cmake --build "$build" --target "$name" -j "${JOBS:-$(nproc)}" || { status=1; continue; }
    mkdir -p "$out/$name"
    for ext in html js wasm data; do
        [ -f "$build/$name.$ext" ] && cp "$build/$name.$ext" "$out/$name/"
    done
    # the page of the demo is its index, so that the address is out/<name>/
    cp "$out/$name/$name.html" "$out/$name/index.html"
    if [ -f "images/$folder.png" ] && command -v convert > /dev/null 2>&1; then
        convert "images/$folder.png" -resize 640x360 -quality 82 "$out/thumbs/$name.jpg"
    fi
    number="${folder%%_*}"
    cards="$cards<a class=\"card\" href=\"$name/\"><img src=\"thumbs/$name.jpg\" alt=\"\" loading=\"lazy\"><div class=\"body\"><h2><span>$number</span> $title</h2><p>$text</p></div></a>
"
    echo "web: $out/$name"
done < web_demos.txt

sed -e "/@CARDS@/{r /dev/stdin" -e "d}" web/index.html.in > "$out/index.html" << CARDS
$cards
CARDS
exit $status
