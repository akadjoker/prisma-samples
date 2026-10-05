#!/bin/sh
build="${1:-build-shots}"
out="${2:-images}"
backend="$3"
mkdir -p "$out"
status=0
while read -r folder target frame; do
    [ -z "$folder" ] && continue
    if [ ! -x "$build/$target" ]; then
        echo "missing $target"
        status=1
        continue
    fi
    PRISMA_SHOT="$out/$folder.png" PRISMA_SHOT_FRAME="$frame" timeout 120 "$build/$target" still $backend > "/tmp/shot-$target.log" 2>&1
    code=$?
    if [ $code -ne 0 ] || [ ! -f "$out/$folder.png" ]; then
        echo "$folder: failed ($code)"
        status=1
    else
        echo "$folder: ok"
    fi
done <<LIST
01_clear clear 5
02_triangle triangle 5
03_cube cube 10
04_two_cubes two_cubes 10
05_lighting lighting 10
06_texture texture 10
07_model model 10
08_reflection reflection 10
09_blend_stencil blend_stencil 10
10_instancing instancing 10
11_offscreen_msaa offscreen_msaa 10
12_particles particles 300
13_hdr hdr 10
14_shadow_map shadow_map 10
15_soldier soldier 10
16_tessellation tessellation 10
17_point_sprites point_sprites 10
18_cascaded_shadows cascaded_shadows 10
19_variance_shadows variance_shadows 10
20_contact_hardening contact_hardening 10
21_pn_triangles pn_triangles 10
22_displacement displacement 10
23_fluid fluid 400
24_nbody nbody 200
25_oit oit 10
26_basic_compute basic_compute 10
27_compute_sort compute_sort 26
28_shadow_volume shadow_volume 10
29_hdr_tonemap_compute hdr_tonemap_compute 10
30_pbr_ibl pbr_ibl 10
31_gltf_viewer gltf_viewer 10
32_ibl_clear_coat ibl_clear_coat 10
33_flight_helmet flight_helmet 10
34_drone drone 10
35_materials materials 6
36_refraction refraction 6
37_area_lights area_lights 10
38_instant_radiosity instant_radiosity 40
LIST
exit $status
