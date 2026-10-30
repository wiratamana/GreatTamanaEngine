# Mesh clusters
> The cluster table of a mesh: file format, how it is built, and how it reaches the GPU. For anyone importing big meshes or touching the build code.
> Verified against code: 2026-10-30

## TL;DR
- A cluster is a small group of triangles (default up to 64 vertices and 124 triangles) with its own bounds and normal cone.
- The table is optional. It sits at the end of the mesh payload in the `.gta` file.
- A mesh without a valid table still draws on the CPU path. Nothing breaks.
- Four doors build a table: import, Inspector, `POST /build_clusters`, and clone.
- The GPU culling that uses the table is in [GPU-driven rendering](gpu-driven-rendering.md).

## Diagram
```text
 MeshData (positions, normals, uvs, indices)
      |  BuildMeshClusters()        src/Assets/MeshClustering.cpp
      |    validate -> weld -> cluster per material (meshoptimizer)
      |    -> reorder vertices -> ValidateClusterTable
      v
 mesh payload: ... | tangent tail | 'GCL1' count | N x 64 B | CRC-32
      |  MeshAssetGpuCatalog::EnsureMeshAsset()
      v
 rebase per part -> upload MeshClusterTable buffer -> Mesh::HasClusters()
```

## Cluster record
`MeshCluster` (`src/Assets/MeshCluster.h`) is 64 bytes in mesh local space. The file record and the GPU record are the same bytes.

| Field | Meaning |
|---|---|
| Bounding sphere, AABB | Used by the frustum and Hi-Z tests |
| Normal cone | Axis plus cutoff. The cutoff is the sine of the half angle. 1 means no cone. |
| `firstIndex`, `indexCount` | The index range of the cluster |

- The index buffer is stored in cluster order. It is still a valid mesh, so every CPU path keeps working.
- The table must tile `[0, indexCount)` exactly. `ValidateClusterTable()` checks it in the builder, the decoder and the catalog.
- The cone test: for a cluster with cutoff below 1, every triangle normal `n` satisfies `dot(n, axis) >= sqrt(1 - cutoff * cutoff)`.

## File layout
Mesh payload, little-endian. See [Asset pipeline](asset-pipeline.md#payload-and-metadata-formats) for the other blocks.

| Block | Size |
|---|---|
| Magic `GTEMESH1`, `vertexCount`, `indexCount` | 8 + 4 + 4 bytes |
| Positions, normals, uvs, indices | `12v`, `12v`, `8v`, `4i` bytes |
| Optional tangent tail | `uint32` count (equals `vertexCount`), then 16 bytes each |
| Optional cluster tail | `GCL1`, `uint32` count (at least 1), count x 64 bytes, CRC-32 of count and table |

- Nothing may follow the last block.
- A damaged cluster tail (bad count, CRC, validation, truncation) does not reject the mesh. The decoder returns it without clusters and a reason. The catalog logs the reason once.
- A damaged vertex, index or tangent block rejects the file.
- A file without a tail has the old bytes. An old build rejects a clustered file.
- The table limit is `kMaxMeshClusters` (128 MiB / 64 bytes).

## Build options
Defaults live in `src/Assets/MeshClusterOptions.h` and `AssetImporter.h`.

| Option | Default | Allowed |
|---|---|---|
| Vertices per cluster | 64 | 3 to 255 |
| Triangles per cluster | 124 | 4 to 512, multiple of 4 |
| Cone weight | 0.5 | 0 to 1, finite |
| Weld | `Exact` | `Exact`, `PositionOnly` |
| Import mode | `Auto` | `Off`, `Auto`, `Always` |
| Auto triangle threshold | 100000 | any |

| Weld | Effect |
|---|---|
| `Exact` | Merges identical vertices only. Lossless. A mesh of unshared triangles stays poorly filled. |
| `PositionOnly` | Merges by position and recomputes smooth normals. Refused for meshes with uvs or a texture. |

- Meshes with skin weights, bones, morphs or physics are never clustered. Welding reorders vertices.
- `poorFill` is set when the average triangles per cluster is below 50% of the limit.
- `BuildMeshClusters()` never throws. On failure the mesh stays unchanged.

## The four doors
| Door | How | Result |
|---|---|---|
| Import | `ImportAssetFile()` takes `MeshImportOptions`. `Off` never builds. `Always` builds for rigless meshes. `Auto` needs the triangle threshold and skips poor fill. | Table inside the new `.gta` |
| Inspector | "Build Clusters" starts the editor's single `MeshClusterBuildRunner`. `EditorHost` polls it each frame. | Rewrites the `.gta` |
| HTTP | `POST /build_clusters` with `gta_path`. It shares one bridge with `/import_asset`. | Rewrites the `.gta` |
| Clone | "Create Clustered Copy" or `POST /clone_clusters` | New `<stem>_clustered.gta` with a new Guid. The source is untouched. |

- `Auto` with a textured or PBR material only runs with the `Exact` weld. With another weld it is skipped and `clusterMessage` says so.
- Clustering never fails an import. `clusterMessage` carries the reason.
- Clone with weld `auto` (the default) tries `Exact`, then `PositionOnly` when the fill is poor. It uses create-only writes. If the name exists it picks `_clustered_2`, and so on.
- STL meshes have no shared vertices. `Auto` skips them ("expected cluster fill is poor"). Use the clone.
- Doors 1 to 3 give the same bytes for the same input and options.

Writes follow the [atomic write rules](asset-pipeline.md#atomic-writes). A build records the file identity when it starts. An import that replaces the file in the meantime makes the build fail with "changed on disk". A failed or cancelled build leaves the old file byte-identical.

## Runtime table
`MeshAssetGpuCatalog::EnsureMeshAsset()` turns the file table into GPU tables.

1. It rebases the clusters onto the untextured part's merged index buffer.
2. It does the same for every opaque, static, textured or PBR slice, onto that slice's own index buffer.
3. It validates each table and uploads one device-local buffer into the `Mesh`.

- Alpha-tested slices and GPU-skinned models get no table.
- A table above the limit, or a failed upload, is dropped. The mesh draws on the CPU path. Each case logs once.
- An untextured mesh without a table joins the cluster path when a frame holds at least 4 instances of it (`kMinInstancesForSynthesizedClusters`).
- `Core::EnsureClusterTable()` then builds one cluster over the whole index range, with no cone. A failed build retries after 120 frames, then doubles up to a cap.
- `InvalidateCachedMeshAsset()` only affects new spawns. Respawn to use a rebuilt table.

## Key files
| File | Role |
|---|---|
| `src/Assets/MeshClustering.*` | `BuildMeshClusters`, weld, report |
| `src/Assets/MeshCluster.h`, `MeshClusterValidation.*` | Record and validation |
| `src/Assets/MeshClusterPersistence.*`, `MeshClusterRebase.*`, `MeshClusterClone.*` | Save, rebase per slice, clone |
| `src/Assets/MeshFile.*` | Tail encode and decode |
| `src/Editor/MeshClusterBuildRunner.*` | The one editor build job |
| `src/Game/Instantiation/MeshAssetGpuCatalog.*` | Runtime tables |
| `src/Renderer/Culling/ClusterCullMath.h` | Limits, CPU oracle of the cull shader |

## Gotchas
- Two writers on one `.gta` (import and build) have no extra lock. The atomic rules are the lock. The second one fails with "busy".
- A build cannot be cancelled inside one meshoptimizer call. Closing the editor waits for it.
- With one job worker or no job system, a build runs inline and stalls the editor.
- Encoding returns nothing for an invalid table. The importer then retries without clusters.

## Related
- [GPU-driven rendering](gpu-driven-rendering.md) · [GPU-driven scene feature](../reference/render-features/gpu-driven-scene.md)
- [Asset pipeline](asset-pipeline.md) · [HTTP routes](../debugging/http-routes.md) · [HTTP cheat sheet](../debugging/http-cheat-sheet.md)
