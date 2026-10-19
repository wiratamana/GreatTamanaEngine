#include "GltfLoader.h"

#include "MeshNormalRecompute.h"

#include <cgltf.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gte {

namespace {

struct CgltfDataDeleter {
    void operator()(cgltf_data* data) const noexcept { cgltf_free(data); }
};
using CgltfDataPtr = std::unique_ptr<cgltf_data, CgltfDataDeleter>;

std::string PathToUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::filesystem::path Utf8ToPath(std::string_view utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

// File reader for cgltf that takes UTF-8 paths, so non-ASCII folders work on Windows.
cgltf_result ReadFileUtf8(const cgltf_memory_options* memory, const cgltf_file_options*, const char* path,
    cgltf_size* size, void** data)
{
    std::ifstream file(Utf8ToPath(path), std::ios::binary | std::ios::ate);
    if (!file) {
        return cgltf_result_file_not_found;
    }
    const std::streamoff fileSize = file.tellg();
    if (fileSize < 0) {
        return cgltf_result_io_error;
    }
    const cgltf_size wanted = (size != nullptr && *size != 0) ? *size : static_cast<cgltf_size>(fileSize);
    if (wanted > static_cast<cgltf_size>(fileSize)) {
        return cgltf_result_io_error;
    }

    void* buffer = memory->alloc_func != nullptr ? memory->alloc_func(memory->user_data, wanted)
                                                 : std::malloc(wanted);
    if (buffer == nullptr) {
        return cgltf_result_out_of_memory;
    }
    file.seekg(0, std::ios::beg);
    file.read(static_cast<char*>(buffer), static_cast<std::streamsize>(wanted));
    if (static_cast<cgltf_size>(file.gcount()) != wanted) {
        memory->free_func != nullptr ? memory->free_func(memory->user_data, buffer) : std::free(buffer);
        return cgltf_result_io_error;
    }
    if (size != nullptr) {
        *size = wanted;
    }
    *data = buffer;
    return cgltf_result_success;
}

void ReleaseFile(const cgltf_memory_options* memory, const cgltf_file_options*, void* data)
{
    memory->free_func != nullptr ? memory->free_func(memory->user_data, data) : std::free(data);
}

// Node world matrix (column-major) plus its normal matrix (cofactor of the upper-left 3x3).
struct NodeTransform {
    float world[16] = {};
    float normal[9] = {}; // row-major
    bool mirrored = false; // determinant < 0
};

NodeTransform BuildNodeTransform(const cgltf_node& node)
{
    NodeTransform t;
    cgltf_node_transform_world(&node, t.world);

    const float* m = t.world; // a(row, col) = m[col * 4 + row]
    const float a00 = m[0], a01 = m[4], a02 = m[8];
    const float a10 = m[1], a11 = m[5], a12 = m[9];
    const float a20 = m[2], a21 = m[6], a22 = m[10];

    t.normal[0] = a11 * a22 - a12 * a21;
    t.normal[1] = a12 * a20 - a10 * a22;
    t.normal[2] = a10 * a21 - a11 * a20;
    t.normal[3] = a02 * a21 - a01 * a22;
    t.normal[4] = a00 * a22 - a02 * a20;
    t.normal[5] = a01 * a20 - a00 * a21;
    t.normal[6] = a01 * a12 - a02 * a11;
    t.normal[7] = a02 * a10 - a00 * a12;
    t.normal[8] = a00 * a11 - a01 * a10;

    const float determinant = a00 * t.normal[0] + a01 * t.normal[1] + a02 * t.normal[2];
    t.mirrored = determinant < 0.0f;
    return t;
}

Vec3 TransformPosition(const NodeTransform& t, const Vec3& p)
{
    const float* m = t.world;
    return Vec3(m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
        m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
        m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]);
}

// The cofactor matrix is the inverse-transpose times the determinant, so a mirrored node flips the sign.
Vec3 TransformNormal(const NodeTransform& t, const Vec3& n)
{
    const float* c = t.normal;
    const Vec3 r = Normalize(Vec3(c[0] * n.x + c[1] * n.y + c[2] * n.z,
        c[3] * n.x + c[4] * n.y + c[5] * n.z,
        c[6] * n.x + c[7] * n.y + c[8] * n.z));
    return t.mirrored ? Vec3(-r.x, -r.y, -r.z) : r;
}

// glTF is right-handed, the engine is left-handed: mirror Z.
Vec3 ToEngineSpace(const Vec3& v)
{
    return Vec3(v.x, v.y, -v.z);
}

bool ReadFloats(const cgltf_accessor* accessor, cgltf_type expectedType, std::vector<float>* out)
{
    if (accessor == nullptr || accessor->type != expectedType) {
        return false;
    }
    const cgltf_size floatCount = accessor->count * cgltf_num_components(accessor->type);
    out->assign(floatCount, 0.0f);
    return cgltf_accessor_unpack_floats(accessor, out->data(), floatCount) == floatCount;
}

// Reads (or generates) a triangle index list. nullopt = malformed.
std::optional<std::vector<std::uint32_t>> ReadIndices(const cgltf_primitive& primitive, std::size_t vertexCount)
{
    std::vector<std::uint32_t> indices;
    if (primitive.indices != nullptr) {
        indices.resize(primitive.indices->count);
        for (cgltf_size i = 0; i < indices.size(); ++i) {
            indices[i] = static_cast<std::uint32_t>(cgltf_accessor_read_index(primitive.indices, i));
        }
    } else {
        indices.resize(vertexCount);
        for (std::size_t i = 0; i < indices.size(); ++i) {
            indices[i] = static_cast<std::uint32_t>(i);
        }
    }
    if (indices.size() % 3 != 0) {
        return std::nullopt;
    }
    for (const std::uint32_t index : indices) {
        if (index >= vertexCount) {
            return std::nullopt;
        }
    }
    return indices;
}

// One primitive as engine-space MeshData with 0-based local indices; nullopt if unsupported or malformed.
std::optional<MeshData> BuildPrimitiveMesh(const cgltf_primitive& primitive, const NodeTransform& transform)
{
    if (primitive.type != cgltf_primitive_type_triangles) {
        return std::nullopt;
    }

    std::vector<float> positionFloats;
    if (!ReadFloats(cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0), cgltf_type_vec3, &positionFloats)) {
        return std::nullopt;
    }
    std::vector<float> normalFloats;
    const bool hasNormals = ReadFloats(
        cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0), cgltf_type_vec3, &normalFloats);
    std::vector<float> uvFloats;
    const bool hasUvs = ReadFloats(
        cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0), cgltf_type_vec2, &uvFloats);

    const std::size_t vertexCount = positionFloats.size() / 3;
    if ((hasNormals && normalFloats.size() != vertexCount * 3) || (hasUvs && uvFloats.size() != vertexCount * 2)) {
        return std::nullopt;
    }

    std::optional<std::vector<std::uint32_t>> indices = ReadIndices(primitive, vertexCount);
    if (!indices.has_value()) {
        return std::nullopt;
    }

    MeshData mesh;
    mesh.positions.reserve(vertexCount);
    mesh.normals.reserve(vertexCount);
    mesh.uvs.reserve(vertexCount);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        const Vec3 p(positionFloats[i * 3], positionFloats[i * 3 + 1], positionFloats[i * 3 + 2]);
        mesh.positions.push_back(ToEngineSpace(TransformPosition(transform, p)));

        if (hasNormals) {
            const Vec3 n(normalFloats[i * 3], normalFloats[i * 3 + 1], normalFloats[i * 3 + 2]);
            mesh.normals.push_back(ToEngineSpace(TransformNormal(transform, n)));
        } else {
            mesh.normals.push_back(Vec3::Zero());
        }
        // glTF UV origin is top-left, same as decoded image rows: no V flip.
        mesh.uvs.push_back(hasUvs ? Vec2(uvFloats[i * 2], uvFloats[i * 2 + 1]) : Vec2::Zero());
    }

    // The Z mirror flips winding once; a mirrored node flips it back.
    if (!transform.mirrored) {
        for (std::size_t i = 0; i < indices->size(); i += 3) {
            std::swap((*indices)[i + 1], (*indices)[i + 2]);
        }
    }
    mesh.indices = std::move(*indices);

    if (!hasNormals) {
        RecomputeMeshNormalsFromGeometry(mesh);
    }
    return mesh;
}

void CollectMeshNodes(const cgltf_node* node, std::vector<const cgltf_node*>* out)
{
    if (node->mesh != nullptr) {
        out->push_back(node);
    }
    for (cgltf_size i = 0; i < node->children_count; ++i) {
        CollectMeshNodes(node->children[i], out);
    }
}

// Every mesh node of the default scene (or of all root nodes if the file has no scene).
std::vector<const cgltf_node*> CollectSceneMeshNodes(const cgltf_data& data)
{
    std::vector<const cgltf_node*> nodes;
    const cgltf_scene* scene = data.scene != nullptr ? data.scene : (data.scenes_count > 0 ? &data.scenes[0] : nullptr);
    if (scene != nullptr) {
        for (cgltf_size i = 0; i < scene->nodes_count; ++i) {
            CollectMeshNodes(scene->nodes[i], &nodes);
        }
    } else {
        for (cgltf_size i = 0; i < data.nodes_count; ++i) {
            if (data.nodes[i].parent == nullptr) {
                CollectMeshNodes(&data.nodes[i], &nodes);
            }
        }
    }
    return nodes;
}

// Maps each glTF image to one MaterialData::textures entry, shared by every slot that uses it.
struct TextureTable {
    std::filesystem::path baseDirectory;
    MaterialData* out = nullptr;
    std::unordered_map<const cgltf_image*, std::int32_t> imageToTexture;

    // Returns the textures[] index, or -1 for no texture (also for embedded images, which have no file).
    std::int32_t Resolve(const cgltf_texture_view& view)
    {
        if (view.texture == nullptr || view.texture->image == nullptr) {
            return -1;
        }
        const cgltf_image* image = view.texture->image;
        if (const auto found = imageToTexture.find(image); found != imageToTexture.end()) {
            return found->second;
        }
        if (image->uri == nullptr || std::string_view(image->uri).starts_with("data:")) {
            return -1;
        }

        std::string uri = image->uri;
        uri.resize(cgltf_decode_uri(uri.data())); // "%20" etc.

        MaterialTextureRef ref;
        ref.sourcePath = PathToUtf8((baseDirectory / Utf8ToPath(uri)).lexically_normal());

        const auto index = static_cast<std::int32_t>(out->textures.size());
        out->textures.push_back(std::move(ref));
        imageToTexture.emplace(image, index);
        return index;
    }
};

Material ConvertMaterial(const cgltf_material& source, std::size_t index, TextureTable& textures)
{
    Material m;
    m.name = source.name != nullptr ? source.name : "Material_" + std::to_string(index);

    if (source.has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness& pbr = source.pbr_metallic_roughness;
        m.diffuse = Vec4(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2],
            pbr.base_color_factor[3]);
        m.textureIndex = textures.Resolve(pbr.base_color_texture); // base-color slot
        m.metallicRoughnessTextureIndex = textures.Resolve(pbr.metallic_roughness_texture);
    }
    m.normalTextureIndex = textures.Resolve(source.normal_texture);
    m.occlusionTextureIndex = textures.Resolve(source.occlusion_texture);
    m.emissiveTextureIndex = textures.Resolve(source.emissive_texture);

    m.bothFacesVisible = source.double_sided != 0;
    m.alphaCutoff = source.alpha_cutoff;
    switch (source.alpha_mode) {
    case cgltf_alpha_mode_mask: m.alphaMode = AlphaMode::Mask; break;
    case cgltf_alpha_mode_blend: m.alphaMode = AlphaMode::Blend; break;
    default: m.alphaMode = AlphaMode::Opaque; break;
    }
    return m;
}

} // namespace

GltfLoadResult LoadGltfModel(const std::string& filePath)
{
    GltfLoadResult result;

    cgltf_options options{};
    options.file.read = &ReadFileUtf8;
    options.file.release = &ReleaseFile;

    cgltf_data* rawData = nullptr;
    if (const cgltf_result parsed = cgltf_parse_file(&options, filePath.c_str(), &rawData);
        parsed != cgltf_result_success) {
        result.message = "Failed to parse glTF (cgltf error " + std::to_string(static_cast<int>(parsed))
            + "). Only external-file .gltf is supported.";
        return result;
    }
    const CgltfDataPtr data(rawData);

    if (cgltf_load_buffers(&options, data.get(), filePath.c_str()) != cgltf_result_success) {
        result.message = "Failed to load the glTF buffers (is the .bin next to the .gltf?).";
        return result;
    }
    if (cgltf_validate(data.get()) != cgltf_result_success) {
        result.message = "glTF validation failed.";
        return result;
    }

    TextureTable textures{ Utf8ToPath(filePath).parent_path(), &result.materials, {} };

    // Group triangles per material so each material owns one contiguous index run.
    struct MaterialBucket {
        Material material;
        std::vector<std::uint32_t> indices;
    };
    std::vector<MaterialBucket> buckets(data->materials_count + 1); // last bucket = primitives with no material
    for (std::size_t i = 0; i < data->materials_count; ++i) {
        buckets[i].material = ConvertMaterial(data->materials[i], i, textures);
    }
    buckets.back().material.name = "Default";

    std::size_t skippedPrimitives = 0;
    for (const cgltf_node* node : CollectSceneMeshNodes(*data)) {
        const NodeTransform transform = BuildNodeTransform(*node);
        for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
            const cgltf_primitive& primitive = node->mesh->primitives[p];
            const std::optional<MeshData> built = BuildPrimitiveMesh(primitive, transform);
            if (!built.has_value()) {
                ++skippedPrimitives;
                continue;
            }

            const auto baseVertex = static_cast<std::uint32_t>(result.mesh.positions.size());
            result.mesh.positions.insert(result.mesh.positions.end(), built->positions.begin(), built->positions.end());
            result.mesh.normals.insert(result.mesh.normals.end(), built->normals.begin(), built->normals.end());
            result.mesh.uvs.insert(result.mesh.uvs.end(), built->uvs.begin(), built->uvs.end());

            std::vector<std::uint32_t>& target = primitive.material != nullptr
                ? buckets[cgltf_material_index(data.get(), primitive.material)].indices
                : buckets.back().indices;
            target.reserve(target.size() + built->indices.size());
            for (const std::uint32_t index : built->indices) {
                target.push_back(baseVertex + index);
            }
        }
    }

    for (std::size_t i = 0; i < buckets.size(); ++i) {
        MaterialBucket& bucket = buckets[i];
        const bool isDefaultBucket = i + 1 == buckets.size();
        if (isDefaultBucket && bucket.indices.empty()) {
            continue;
        }
        bucket.material.indexCount = static_cast<std::uint32_t>(bucket.indices.size());
        result.mesh.indices.insert(result.mesh.indices.end(), bucket.indices.begin(), bucket.indices.end());
        result.materials.materials.push_back(std::move(bucket.material));
    }

    if (result.mesh.positions.empty() || result.mesh.indices.empty()) {
        result.message = "The glTF has no triangle geometry we can import.";
        return result;
    }

    // UV range: a value outside [0,1] means the textures tile.
    Vec2 uvMin(1e30f, 1e30f);
    Vec2 uvMax(-1e30f, -1e30f);
    for (const Vec2& uv : result.mesh.uvs) {
        uvMin = Vec2(std::min(uvMin.x, uv.x), std::min(uvMin.y, uv.y));
        uvMax = Vec2(std::max(uvMax.x, uv.x), std::max(uvMax.y, uv.y));
    }

    result.success = true;
    result.message = "Loaded glTF: " + std::to_string(result.mesh.positions.size()) + " vertices, "
        + std::to_string(result.mesh.indices.size() / 3) + " triangles, "
        + std::to_string(result.materials.materials.size()) + " materials, "
        + std::to_string(result.materials.textures.size()) + " textures, "
        + std::to_string(skippedPrimitives) + " skipped primitives, UV range ["
        + std::to_string(uvMin.x) + "," + std::to_string(uvMin.y) + "]-["
        + std::to_string(uvMax.x) + "," + std::to_string(uvMax.y) + "].";
    return result;
}

} // namespace gte
