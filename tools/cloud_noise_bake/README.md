# cloud_noise_bake

Offline tool that bakes the tileable cloud noise assets once and saves them to files. Plain C++20, no dependency except `third_party/stb` (header only).

## Automatic bake in the build

`cmake --build build --target GreatTamanaEditor` builds this tool (`build\tools\cloud_noise_bake.exe`) and runs it once into `build\assets\clouds`, the folder the engine reads at run time (see `cmake\CloudNoiseAssets.cmake`). It reruns only when an output is missing or the tool changed. The build never touches the network: the blue noise comes from `data\LDR_LLL1_0.png` (CC0, `data\LICENSE.txt`). Output is byte-identical to the manual run below.

## Build and run

```
cd tools\cloud_noise_bake
g++ -std=c++20 -O2 -I..\..\third_party\stb -o cloud_noise_bake.exe main.cpp -pthread
cloud_noise_bake.exe bake   --out out --blue path\to\LDR_LLL1_0.png   (add --gen-blue to force void-and-cluster)
cloud_noise_bake.exe verify --dir out                                  (stats, seam test, correlation, PNG previews in out\preview)
```

Bake time: about 3.5 s total on 8 threads (shape 1.7 s, weather 0.9 s, detail 14 ms, void-and-cluster 0.4 s). Output is deterministic (fixed seeds, per-voxel evaluation, independent of thread count).

## File format GTVOL ("GTV1"), little endian

32-byte header, then voxels tightly packed, x fastest, then y, then z.

| offset | type | field |
|---|---|---|
| 0 | char[4] | magic `GTV1` |
| 4 | u32 | width |
| 8 | u32 | height |
| 12 | u32 | depth (1 for 2D) |
| 16 | u32 | format: 0 = RGBA8_UNorm, 1 = R8_UNorm |
| 20 | u32 | flags: bit0 = tileable on all axes |
| 24 | u32 | reserved0 (0) |
| 28 | u32 | reserved1 (0) |

## Assets and channels

| file | size | R | G | B | A |
|---|---|---|---|---|---|
| cloud_shape_128.gtvol | 128^3 RGBA8 (8 MiB) | Perlin-Worley | Worley 8 cells | Worley 16 cells | Worley 32 cells |
| cloud_detail_32.gtvol | 32^3 RGBA8 (128 KiB) | Worley 4 cells | Worley 8 cells | Worley 16 cells | 0.625 R + 0.25 G + 0.125 B (precomputed FBM) |
| cloud_weather_512.gtvol / .png | 512^2 RGBA8 | low-altitude coverage | high-altitude coverage | cloud type / peak height | density multiplier 0.45..1 |
| blue_noise_128.gtvol / .png | 128^2 R8 | blue noise | | | |

"Worley" = inverted F1 (`1 - min(F1,1)`): bright blobs at the feature points, dark at the borders. This is the "puffy" polarity the Horizon Zero Dawn recipe wants. Cells = lattice cells per tile, so G/B/A are 2x apart in frequency.

Perlin-Worley (R of shape): `perlinFbm` (4 octaves, base 4 cells, gain 0.5) is remapped into `[worleyFbm, 1]`, i.e. `R = w + p * (1 - w)`, where `w` is the inverted Worley FBM (cells 4/8/16, weights 0.625/0.25/0.125). Both inputs are min/max stretched to 0..1 before the combine and R is stretched again.

Every channel is min/max stretched to the full 0..255 range after generation.

## How tiling is done (and why not FastNoise)

The generator is periodic by construction: lattice cell indices are wrapped modulo the cell count before hashing, and every FBM octave uses an integer cell count (base * 2^n), so every octave has period exactly 1 tile. Voxel x is sampled at `x / N`, so voxel N == voxel 0. FastNoiseLite cannot tile at all. FastNoise2 v1.1.1 only has `GenTileable2D` (4D torus trick) and nothing for 3D; see work\notes\P1a_noise_bake.md.

The weather map is a z = 0.37 slice of the periodic 3D noise, which is periodic in x and y.

## Blue noise

`LDR_LLL1_0.png` (128x128) from https://github.com/Calinou/free-blue-noise-textures, CC0 (see `out\blue_noise_LICENSE.txt`). If the file is missing or not 128x128 the tool generates one with void-and-cluster (sigma 1.9, torus). Both give a 5x5-blur variance ratio of about 0.0065 (white noise: about 0.04).

## Verification

`verify` reads the .gtvol files back from disk and prints, per channel: min/max/mean/sd/histogram; per axis: mean absolute difference between neighbouring slices (interior) vs the wrap pair (last -> first) vs a far pair; Pearson correlation of channels; PNG previews (`*_slices.png`, `*_tiled2x2.png`). The seam test passes when the wrap difference is no larger than the largest interior neighbour difference. Result stored in `out\verify_report.txt`.
