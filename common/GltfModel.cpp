#include "GltfModel.h"

#include "third_party/cgltf.h"
#include "third_party/mikktspace.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zenapp
{

namespace
{

void directoryOf(const char* path, ct::String* out)
{
    const char* slash = strrchr(path, '/');
    if (!slash)
    {
        *out = ".";
        return;
    }
    *out = ct::String();
    out->append(path, static_cast<size_t>(slash - path));
}

void joinPath(const ct::String& directory, const char* uri, ct::String* out)
{
    *out = directory;
    *out += "/";
    ct::String decoded;
    decoded.append(uri);
    ct::Vector<char> buffer;
    buffer.resize(decoded.size() + 1);
    memcpy(buffer.data(), decoded.c_str(), decoded.size() + 1);
    cgltf_decode_uri(buffer.data());
    *out += buffer.data();
}

int ddsSourceIndex(const cgltf_texture& texture)
{
    for (cgltf_size i = 0; i < texture.extensions_count; ++i)
    {
        if (strcmp(texture.extensions[i].name, "MSFT_texture_dds") != 0) continue;
        const char* data = texture.extensions[i].data;
        const char* key = data ? strstr(data, "\"source\"") : nullptr;
        if (!key) return -1;
        key = strchr(key, ':');
        return key ? atoi(key + 1) : -1;
    }
    return -1;
}

int textureIndex(const cgltf_data* data, const cgltf_texture_view& view)
{
    if (!view.texture) return -1;
    return static_cast<int>(view.texture - data->textures);
}

void fillTexture(const cgltf_data* data, const cgltf_texture& source, const ct::String& directory,
        GltfTexture* out)
{
    const cgltf_image* image = source.image;
    if (image && image->uri && strncmp(image->uri, "data:", 5) != 0)
        joinPath(directory, image->uri, &out->path);
    else if (image && image->buffer_view)
    {
        const cgltf_buffer_view* view = image->buffer_view;
        const unsigned char* bytes = static_cast<const unsigned char*>(view->buffer->data) + view->offset;
        out->embedded.resize(view->size);
        memcpy(out->embedded.data(), bytes, view->size);
    }
    const int dds = ddsSourceIndex(source);
    if (dds >= 0 && static_cast<cgltf_size>(dds) < data->images_count && data->images[dds].uri)
        joinPath(directory, data->images[dds].uri, &out->ddsPath);
    out->repeat = !source.sampler || source.sampler->wrap_s != 33071;
}

void fillMaterial(const cgltf_data* data, const cgltf_material& source, GltfMaterial* out)
{
    if (source.name) out->name = source.name;
    out->alpha = source.alpha_mode == cgltf_alpha_mode_mask    ? GltfMaterial::Alpha::Mask
                 : source.alpha_mode == cgltf_alpha_mode_blend ? GltfMaterial::Alpha::Blend
                                                               : GltfMaterial::Alpha::Opaque;
    out->alphaCutoff = source.alpha_cutoff;
    out->doubleSided = source.double_sided != 0;
    for (int i = 0; i < 3; ++i) out->emissive[i] = source.emissive_factor[i];
    out->normalTexture = textureIndex(data, source.normal_texture);
    out->normalScale = source.normal_texture.texture ? source.normal_texture.scale : 1.0f;
    out->occlusionTexture = textureIndex(data, source.occlusion_texture);
    out->emissiveTexture = textureIndex(data, source.emissive_texture);
    if (source.has_transmission) out->transmission = source.transmission.transmission_factor;

    if (source.has_pbr_specular_glossiness)
    {
        const cgltf_pbr_specular_glossiness& sg = source.pbr_specular_glossiness;
        out->specularGlossiness = true;
        for (int i = 0; i < 4; ++i) out->baseColor[i] = sg.diffuse_factor[i];
        for (int i = 0; i < 3; ++i) out->specular[i] = sg.specular_factor[i];
        out->glossiness = sg.glossiness_factor;
        out->baseColorTexture = textureIndex(data, sg.diffuse_texture);
        out->surfaceTexture = textureIndex(data, sg.specular_glossiness_texture);
    }
    else if (source.has_pbr_metallic_roughness)
    {
        const cgltf_pbr_metallic_roughness& mr = source.pbr_metallic_roughness;
        for (int i = 0; i < 4; ++i) out->baseColor[i] = mr.base_color_factor[i];
        out->metallic = mr.metallic_factor;
        out->roughness = mr.roughness_factor;
        out->baseColorTexture = textureIndex(data, mr.base_color_texture);
        out->surfaceTexture = textureIndex(data, mr.metallic_roughness_texture);
    }
}

const cgltf_accessor* findAttribute(const cgltf_primitive& primitive, cgltf_attribute_type type,
        int index)
{
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i)
        if (primitive.attributes[i].type == type && primitive.attributes[i].index == index)
            return primitive.attributes[i].data;
    return nullptr;
}

bool validTangent(const float* t)
{
    const float length = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
    return isfinite(length) && isfinite(t[3]) && length > 0.5f && length < 1.5f &&
           fabsf(fabsf(t[3]) - 1.0f) < 1e-3f;
}

struct TangentContext
{
    const GltfVertex* vertices;
    const uint32_t* indices;
    size_t faceCount;
    GltfVertex* corners;
};

const TangentContext& tangentData(const SMikkTSpaceContext* context)
{
    return *static_cast<const TangentContext*>(context->m_pUserData);
}

const GltfVertex& cornerSource(const SMikkTSpaceContext* context, int face, int vert)
{
    const TangentContext& data = tangentData(context);
    return data.vertices[data.indices[face * 3 + vert]];
}

int mikkFaceCount(const SMikkTSpaceContext* context)
{
    return static_cast<int>(tangentData(context).faceCount);
}

int mikkVertexCount(const SMikkTSpaceContext*, const int)
{
    return 3;
}

void mikkPosition(const SMikkTSpaceContext* context, float* out, const int face, const int vert)
{
    memcpy(out, cornerSource(context, face, vert).position, sizeof(float) * 3);
}

void mikkNormal(const SMikkTSpaceContext* context, float* out, const int face, const int vert)
{
    memcpy(out, cornerSource(context, face, vert).normal, sizeof(float) * 3);
}

void mikkTexCoord(const SMikkTSpaceContext* context, float* out, const int face, const int vert)
{
    memcpy(out, cornerSource(context, face, vert).uv, sizeof(float) * 2);
}

void mikkSetTangent(const SMikkTSpaceContext* context, const float* tangent, const float sign,
        const int face, const int vert)
{
    GltfVertex& corner = tangentData(context).corners[face * 3 + vert];
    memcpy(corner.tangent, tangent, sizeof(float) * 3);
    corner.tangent[3] = sign;
}

uint32_t hashVertex(const GltfVertex& vertex)
{
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&vertex);
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof(GltfVertex); ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

void weldVertices(const ct::Vector<GltfVertex>& corners, ct::Vector<GltfVertex>* vertices,
        ct::Vector<uint32_t>* indices)
{
    const size_t count = corners.size();
    size_t bucketCount = 1;
    while (bucketCount < count * 2) bucketCount <<= 1;
    ct::Vector<int> buckets;
    buckets.resize(bucketCount);
    for (size_t i = 0; i < bucketCount; ++i) buckets[i] = -1;
    ct::Vector<int> next;
    next.resize(count);
    vertices->clear();
    indices->resize(count);
    for (size_t i = 0; i < count; ++i)
    {
        const size_t bucket = hashVertex(corners[i]) & (bucketCount - 1);
        int found = -1;
        for (int j = buckets[bucket]; j >= 0; j = next[j])
        {
            if (memcmp(&(*vertices)[j], &corners[i], sizeof(GltfVertex)) == 0)
            {
                found = j;
                break;
            }
        }
        if (found < 0)
        {
            found = static_cast<int>(vertices->size());
            vertices->push_back(corners[i]);
            next[found] = buckets[bucket];
            buckets[bucket] = found;
        }
        (*indices)[i] = static_cast<uint32_t>(found);
    }
}

} // namespace

bool generateTangents(ct::Vector<GltfVertex>* vertices, ct::Vector<uint32_t>* indices)
{
    const size_t faces = indices->size() / 3;
    ct::Vector<GltfVertex> corners;
    corners.resize(faces * 3);
    for (size_t i = 0; i < faces * 3; ++i) corners[i] = (*vertices)[(*indices)[i]];

    TangentContext data = { vertices->data(), indices->data(), faces, corners.data() };
    SMikkTSpaceInterface callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.m_getNumFaces = mikkFaceCount;
    callbacks.m_getNumVerticesOfFace = mikkVertexCount;
    callbacks.m_getPosition = mikkPosition;
    callbacks.m_getNormal = mikkNormal;
    callbacks.m_getTexCoord = mikkTexCoord;
    callbacks.m_setTSpaceBasic = mikkSetTangent;
    SMikkTSpaceContext context;
    context.m_pInterface = &callbacks;
    context.m_pUserData = &data;
    if (!genTangSpaceDefault(&context)) return false;

    ct::Vector<GltfVertex> welded;
    ct::Vector<uint32_t> weldedIndices;
    weldVertices(corners, &welded, &weldedIndices);
    *vertices = welded;
    *indices = weldedIndices;
    return true;
}

void orthogonalizeTangent(GltfVertex* vertex)
{
    const float* n = vertex->normal;
    float* t = vertex->tangent;
    const float d = t[0] * n[0] + t[1] * n[1] + t[2] * n[2];
    float r[3] = { t[0] - n[0] * d, t[1] - n[1] * d, t[2] - n[2] * d };
    float length = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    if (!(length > 1e-4f))
    {
        const float axis[3] = { fabsf(n[0]) < 0.9f ? 1.0f : 0.0f, fabsf(n[0]) < 0.9f ? 0.0f : 1.0f,
            0.0f };
        r[0] = n[1] * axis[2] - n[2] * axis[1];
        r[1] = n[2] * axis[0] - n[0] * axis[2];
        r[2] = n[0] * axis[1] - n[1] * axis[0];
        length = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    }
    for (int k = 0; k < 3; ++k) t[k] = r[k] / length;
    t[3] = t[3] < 0.0f ? -1.0f : 1.0f;
}

namespace
{

bool appendPrimitive(const cgltf_data* data, const cgltf_primitive& source, GltfModel* out)
{
    if (source.type != cgltf_primitive_type_triangles) return false;
    const cgltf_accessor* positions = findAttribute(source, cgltf_attribute_type_position, 0);
    if (!positions) return false;
    const cgltf_accessor* normals = findAttribute(source, cgltf_attribute_type_normal, 0);
    const cgltf_accessor* tangents = findAttribute(source, cgltf_attribute_type_tangent, 0);
    const cgltf_accessor* uvs = findAttribute(source, cgltf_attribute_type_texcoord, 0);

    const size_t count = positions->count;
    ct::Vector<GltfVertex> vertices;
    vertices.resize(count);
    memset(vertices.data(), 0, sizeof(GltfVertex) * count);

    ct::Vector<float> scratch;
    scratch.resize(count * 4);
    cgltf_accessor_unpack_floats(positions, scratch.data(), count * 3);
    for (size_t i = 0; i < count; ++i)
        memcpy(vertices[i].position, scratch.data() + i * 3, sizeof(float) * 3);
    if (normals && normals->count == count)
    {
        cgltf_accessor_unpack_floats(normals, scratch.data(), count * 3);
        for (size_t i = 0; i < count; ++i)
            memcpy(vertices[i].normal, scratch.data() + i * 3, sizeof(float) * 3);
    }
    const bool hasTangentData = tangents && tangents->count == count;
    if (hasTangentData)
    {
        cgltf_accessor_unpack_floats(tangents, scratch.data(), count * 4);
        for (size_t i = 0; i < count; ++i)
            memcpy(vertices[i].tangent, scratch.data() + i * 4, sizeof(float) * 4);
    }
    const bool hasUvs = uvs && uvs->count == count;
    if (hasUvs)
    {
        cgltf_accessor_unpack_floats(uvs, scratch.data(), count * 2);
        for (size_t i = 0; i < count; ++i)
            memcpy(vertices[i].uv, scratch.data() + i * 2, sizeof(float) * 2);
    }

    ct::Vector<uint32_t> indices;
    if (source.indices)
    {
        indices.resize(source.indices->count);
        cgltf_accessor_unpack_indices(source.indices, indices.data(), sizeof(uint32_t),
                source.indices->count);
    }
    else
    {
        indices.resize(count - count % 3);
        for (size_t i = 0; i < indices.size(); ++i) indices[i] = static_cast<uint32_t>(i);
    }
    indices.resize(indices.size() - indices.size() % 3);
    for (size_t i = 0; i < indices.size(); ++i)
        if (indices[i] >= count) return false;

    if (!normals)
    {
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            const uint32_t ids[3] = { indices[i], indices[i + 1], indices[i + 2] };
            const float* p0 = vertices[ids[0]].position;
            const float* p1 = vertices[ids[1]].position;
            const float* p2 = vertices[ids[2]].position;
            const float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
            const float e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
            const float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                e1[0] * e2[1] - e1[1] * e2[0] };
            for (int t = 0; t < 3; ++t)
                for (int k = 0; k < 3; ++k) vertices[ids[t]].normal[k] += n[k];
        }
        for (size_t i = 0; i < count; ++i)
        {
            float* n = vertices[i].normal;
            const float length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (length > 0.0f)
                for (int k = 0; k < 3; ++k) n[k] /= length;
            else
                n[1] = 1.0f;
        }
    }

    bool tangentsValid = hasTangentData;
    if (hasTangentData)
        for (size_t i = 0; i < count && tangentsValid; ++i)
            tangentsValid = validTangent(vertices[i].tangent);
    const bool normalMapped = source.material && source.material->normal_texture.texture;
    if (!tangentsValid && normalMapped && hasUvs && indices.size() >= 3)
        tangentsValid = generateTangents(&vertices, &indices);
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        if (tangentsValid)
            orthogonalizeTangent(&vertices[i]);
        else
            memset(vertices[i].tangent, 0, sizeof(float) * 4);
    }

    GltfPrimitive primitive;
    primitive.firstVertex = static_cast<uint32_t>(out->vertices.size());
    primitive.vertexCount = static_cast<uint32_t>(vertices.size());
    primitive.firstIndex = static_cast<uint32_t>(out->indices.size());
    primitive.indexCount = static_cast<uint32_t>(indices.size());
    primitive.material = source.material ? static_cast<int>(source.material - data->materials) : -1;
    primitive.hasTangents = tangentsValid;
    for (int i = 0; i < 3; ++i)
    {
        primitive.boundsMin[i] = 1e30f;
        primitive.boundsMax[i] = -1e30f;
    }
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        for (int c = 0; c < 3; ++c)
        {
            const float v = vertices[i].position[c];
            primitive.boundsMin[c] = v < primitive.boundsMin[c] ? v : primitive.boundsMin[c];
            primitive.boundsMax[c] = v > primitive.boundsMax[c] ? v : primitive.boundsMax[c];
        }
    }
    for (size_t i = 0; i < vertices.size(); ++i) out->vertices.push_back(vertices[i]);
    for (size_t i = 0; i < indices.size(); ++i) out->indices.push_back(indices[i]);
    out->primitives.push_back(primitive);
    return true;
}

void collectNodes(const cgltf_data* data, const cgltf_node* node, GltfModel* out,
        const ct::Vector<int>& meshMap)
{
    if (node->mesh || node->light || node->camera)
    {
        float world[16];
        cgltf_node_transform_world(node, world);
        if (node->camera && !out->camera.valid && node->camera->type == cgltf_camera_type_perspective)
        {
            const cgltf_camera_perspective& p = node->camera->data.perspective;
            out->camera.valid = true;
            memcpy(out->camera.world, world, sizeof(world));
            out->camera.yfov = p.yfov;
            out->camera.aspect = p.has_aspect_ratio ? p.aspect_ratio : 1.7777778f;
            out->camera.nearPlane = p.znear;
            out->camera.farPlane = p.has_zfar ? p.zfar : 1000.0f;
        }
        if (node->light)
        {
            const cgltf_light& source = *node->light;
            GltfLight light;
            light.type = source.type == cgltf_light_type_directional ? GltfLight::Type::Directional
                         : source.type == cgltf_light_type_spot      ? GltfLight::Type::Spot
                                                                     : GltfLight::Type::Point;
            for (int i = 0; i < 3; ++i) light.color[i] = source.color[i];
            light.intensity = source.intensity;
            light.range = source.range;
            light.innerCone = source.spot_inner_cone_angle;
            light.outerCone = source.spot_outer_cone_angle;
            light.position[0] = world[12];
            light.position[1] = world[13];
            light.position[2] = world[14];
            light.direction[0] = -world[8];
            light.direction[1] = -world[9];
            light.direction[2] = -world[10];
            const float length = sqrtf(light.direction[0] * light.direction[0] +
                                       light.direction[1] * light.direction[1] +
                                       light.direction[2] * light.direction[2]);
            if (length > 0.0f)
                for (int i = 0; i < 3; ++i) light.direction[i] /= length;
            out->lights.push_back(light);
        }
        const int mesh = node->mesh ? meshMap[static_cast<size_t>(node->mesh - data->meshes)] : -1;
        if (mesh >= 0 || node->light)
        {
            GltfNode entry;
            memcpy(entry.world, world, sizeof(world));
            entry.mesh = mesh;
            entry.light = node->light ? static_cast<int>(out->lights.size()) - 1 : -1;
            if (node->name) entry.name = node->name;
            out->nodes.push_back(entry);
        }
    }
    for (cgltf_size i = 0; i < node->children_count; ++i)
        collectNodes(data, node->children[i], out, meshMap);
}

bool fileExists(const char* path)
{
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    fclose(file);
    return true;
}

bool usesBuffer(const cgltf_accessor* accessor, const cgltf_buffer* buffer)
{
    return accessor && accessor->buffer_view && accessor->buffer_view->buffer == buffer;
}

bool bufferNeededForRendering(const cgltf_data* data, const cgltf_buffer* buffer)
{
    for (cgltf_size m = 0; m < data->meshes_count; ++m)
    {
        for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p)
        {
            const cgltf_primitive& primitive = data->meshes[m].primitives[p];
            if (usesBuffer(primitive.indices, buffer)) return true;
            for (cgltf_size a = 0; a < primitive.attributes_count; ++a)
                if (usesBuffer(primitive.attributes[a].data, buffer)) return true;
        }
    }
    for (cgltf_size i = 0; i < data->images_count; ++i)
        if (data->images[i].buffer_view && data->images[i].buffer_view->buffer == buffer) return true;
    return false;
}

void fillMissingBuffers(cgltf_data* data, const ct::String& directory)
{
    for (cgltf_size i = 0; i < data->buffers_count; ++i)
    {
        cgltf_buffer& buffer = data->buffers[i];
        if (buffer.data || !buffer.uri || strncmp(buffer.uri, "data:", 5) == 0) continue;
        ct::String path;
        joinPath(directory, buffer.uri, &path);
        if (fileExists(path.c_str()) || bufferNeededForRendering(data, &buffer)) continue;
        buffer.data = data->memory.alloc_func(data->memory.user_data, buffer.size);
        memset(buffer.data, 0, buffer.size);
        buffer.data_free_method = cgltf_data_free_method_memory_free;
    }
}

} // namespace

bool loadGltf(const char* path, GltfModel* out)
{
    *out = GltfModel();
    directoryOf(path, &out->directory);

    cgltf_options options;
    memset(&options, 0, sizeof(options));
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path, &data);
    if (result != cgltf_result_success)
    {
        out->error = "cannot parse the glTF file, cgltf result ";
        out->error += ct::String::number(static_cast<int>(result));
        return false;
    }
    fillMissingBuffers(data, out->directory);
    result = cgltf_load_buffers(&options, data, path);
    if (result != cgltf_result_success)
    {
        out->error = "cannot read the glTF buffers, cgltf result ";
        out->error += ct::String::number(static_cast<int>(result));
        cgltf_free(data);
        return false;
    }

    out->materials.resize(data->materials_count);
    for (cgltf_size i = 0; i < data->materials_count; ++i)
        fillMaterial(data, data->materials[i], &out->materials[i]);
    out->textures.resize(data->textures_count);
    for (cgltf_size i = 0; i < data->textures_count; ++i)
        fillTexture(data, data->textures[i], out->directory, &out->textures[i]);

    ct::Vector<int> meshMap;
    meshMap.resize(data->meshes_count);
    for (cgltf_size m = 0; m < data->meshes_count; ++m)
    {
        GltfMesh mesh;
        if (data->meshes[m].name) mesh.name = data->meshes[m].name;
        mesh.firstPrimitive = static_cast<uint32_t>(out->primitives.size());
        for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p)
            appendPrimitive(data, data->meshes[m].primitives[p], out);
        mesh.primitiveCount = static_cast<uint32_t>(out->primitives.size()) - mesh.firstPrimitive;
        if (mesh.primitiveCount == 0)
            meshMap[m] = -1;
        else
        {
            meshMap[m] = static_cast<int>(out->meshes.size());
            out->meshes.push_back(mesh);
        }
    }

    const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
    if (scene)
        for (cgltf_size i = 0; i < scene->nodes_count; ++i)
            collectNodes(data, scene->nodes[i], out, meshMap);

    cgltf_free(data);
    return true;
}

} // namespace zenapp
