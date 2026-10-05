#include "ObjModel.h"

#include "Media.h"

#include <ct/hashmap.hpp>

#include <math.h>
#include <string.h>

namespace zenapp
{

namespace
{

const float kObjMetallic = 0.0f;
const float kObjRoughness = 0.5f;

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

const char* skipSpaces(const char* p, const char* end)
{
    while (p < end && isSpace(*p)) ++p;
    return p;
}

const char* lineEnd(const char* p, const char* end)
{
    while (p < end && *p != '\n' && *p != '\r') ++p;
    return p;
}

const char* nextLine(const char* p, const char* end)
{
    while (p < end && *p != '\n') ++p;
    return p < end ? p + 1 : p;
}

bool keyword(const char* p, const char* end, const char* word)
{
    const size_t length = strlen(word);
    if (static_cast<size_t>(end - p) <= length || strncmp(p, word, length) != 0) return false;
    return isSpace(p[length]);
}

float parseFloat(const char*& p, const char* end)
{
    p = skipSpaces(p, end);
    bool negative = false;
    if (p < end && (*p == '-' || *p == '+'))
    {
        negative = *p == '-';
        ++p;
    }
    double value = 0.0;
    while (p < end && *p >= '0' && *p <= '9') value = value * 10.0 + (*p++ - '0');
    if (p < end && *p == '.')
    {
        ++p;
        double fraction = 0.1;
        while (p < end && *p >= '0' && *p <= '9')
        {
            value += (*p++ - '0') * fraction;
            fraction *= 0.1;
        }
    }
    if (p < end && (*p == 'e' || *p == 'E'))
    {
        ++p;
        bool negativeExponent = false;
        if (p < end && (*p == '-' || *p == '+'))
        {
            negativeExponent = *p == '-';
            ++p;
        }
        int exponent = 0;
        while (p < end && *p >= '0' && *p <= '9') exponent = exponent * 10 + (*p++ - '0');
        value *= pow(10.0, negativeExponent ? -exponent : exponent);
    }
    return static_cast<float>(negative ? -value : value);
}

int parseInt(const char*& p, const char* end)
{
    p = skipSpaces(p, end);
    bool negative = false;
    if (p < end && (*p == '-' || *p == '+'))
    {
        negative = *p == '-';
        ++p;
    }
    int value = 0;
    while (p < end && *p >= '0' && *p <= '9') value = value * 10 + (*p++ - '0');
    return negative ? -value : value;
}

void lineValue(const char* p, const char* end, ct::String* out)
{
    p = skipSpaces(p, end);
    const char* last = lineEnd(p, end);
    while (last > p && isSpace(last[-1])) --last;
    *out = ct::String();
    for (; p < last; ++p) out->append(1, *p == '\\' ? '/' : *p);
}

void directoryOf(const char* path, ct::String* out)
{
    const char* slash = strrchr(path, '/');
    *out = ct::String();
    if (!slash) out->append(".");
    else out->append(path, static_cast<size_t>(slash - path));
}

void joinPath(const ct::String& directory, const ct::String& name, ct::String* out)
{
    if (name.empty() || name[0] == '/')
    {
        *out = name;
        return;
    }
    *out = directory;
    *out += "/";
    *out += name;
}

int resolveIndex(int index, size_t count)
{
    if (index > 0) return index - 1;
    if (index < 0) return static_cast<int>(count) + index;
    return -1;
}

struct Vec3f
{
    float x, y, z;
};

struct Vec2f
{
    float x, y;
};

struct VertexKey
{
    int position;
    int uv;
    int normal;

    bool operator==(const VertexKey& other) const
    {
        return position == other.position && uv == other.uv && normal == other.normal;
    }

    uint64_t hash() const
    {
        uint64_t h = static_cast<uint64_t>(position + 1);
        h = ct::hash_combine(h, static_cast<uint64_t>(uv + 1));
        return ct::hash_combine(h, static_cast<uint64_t>(normal + 1));
    }
};

struct RawMaterial
{
    ct::String name;
    float diffuse[3] = { 0.75f, 0.75f, 0.75f };
    ct::String albedo;
    ct::String normalMap;
    bool cutout = false;
};

struct Part
{
    int material = 0;
    ct::Vector<VertexKey> corners;
};

void parseMtl(const char* path, ct::Vector<RawMaterial>* materials)
{
    ct::Vector<unsigned char> bytes;
    if (!readFile(path, &bytes)) return;
    const char* p = reinterpret_cast<const char*>(bytes.data());
    const char* end = p + bytes.size();
    RawMaterial* current = nullptr;
    while (p < end)
    {
        p = skipSpaces(p, end);
        if (keyword(p, end, "newmtl"))
        {
            materials->push_back(RawMaterial());
            current = &(*materials)[materials->size() - 1];
            lineValue(p + 6, end, &current->name);
        }
        else if (current && keyword(p, end, "Kd"))
        {
            p += 2;
            for (int i = 0; i < 3; ++i) current->diffuse[i] = parseFloat(p, end);
        }
        else if (current && keyword(p, end, "map_Kd"))
            lineValue(p + 6, end, &current->albedo);
        else if (current && keyword(p, end, "map_bump"))
            lineValue(p + 8, end, &current->normalMap);
        else if (current && keyword(p, end, "bump"))
            lineValue(p + 4, end, &current->normalMap);
        else if (current && keyword(p, end, "map_d"))
            current->cutout = true;
        p = nextLine(p, end);
    }
}

int textureFor(GltfModel* out, const ct::String& path)
{
    if (path.empty()) return -1;
    for (size_t i = 0; i < out->textures.size(); ++i)
        if (strcmp(out->textures[i].path.c_str(), path.c_str()) == 0) return static_cast<int>(i);
    GltfTexture texture;
    texture.path = path;
    out->textures.push_back(texture);
    return static_cast<int>(out->textures.size()) - 1;
}

bool appendPart(const Part& part, bool normalMapped, const ct::Vector<Vec3f>& positions,
        const ct::Vector<Vec3f>& normals, const ct::Vector<Vec2f>& uvs, GltfModel* out)
{
    ct::HashMap<VertexKey, uint32_t> lookup;
    ct::Vector<VertexKey> keys;
    ct::Vector<uint32_t> indices;
    indices.resize(part.corners.size());
    for (size_t i = 0; i < part.corners.size(); ++i)
    {
        const uint32_t* found = lookup.find(part.corners[i]);
        if (found)
        {
            indices[i] = *found;
            continue;
        }
        const uint32_t added = static_cast<uint32_t>(keys.size());
        lookup[part.corners[i]] = added;
        keys.push_back(part.corners[i]);
        indices[i] = added;
    }

    bool hasUvs = false;
    ct::Vector<GltfVertex> vertices;
    vertices.resize(keys.size());
    memset(vertices.data(), 0, sizeof(GltfVertex) * keys.size());
    for (size_t i = 0; i < keys.size(); ++i)
    {
        const VertexKey& key = keys[i];
        GltfVertex& vertex = vertices[i];
        const Vec3f& position = positions[static_cast<size_t>(key.position)];
        vertex.position[0] = position.x;
        vertex.position[1] = position.y;
        vertex.position[2] = position.z;
        if (key.normal >= 0 && static_cast<size_t>(key.normal) < normals.size())
        {
            const Vec3f& normal = normals[static_cast<size_t>(key.normal)];
            vertex.normal[0] = normal.x;
            vertex.normal[1] = normal.y;
            vertex.normal[2] = normal.z;
        }
        if (key.uv >= 0 && static_cast<size_t>(key.uv) < uvs.size())
        {
            vertex.uv[0] = uvs[static_cast<size_t>(key.uv)].x;
            vertex.uv[1] = uvs[static_cast<size_t>(key.uv)].y;
        }
        if (key.uv >= 0) hasUvs = true;
    }

    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const uint32_t ids[3] = { indices[i], indices[i + 1], indices[i + 2] };
        const float* p0 = vertices[ids[0]].position;
        const float* p1 = vertices[ids[1]].position;
        const float* p2 = vertices[ids[2]].position;
        const float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
        const float e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
        const float face[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
            e1[0] * e2[1] - e1[1] * e2[0] };
        for (int t = 0; t < 3; ++t)
            if (keys[ids[t]].normal < 0)
                for (int k = 0; k < 3; ++k) vertices[ids[t]].normal[k] += face[k];
    }
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        float* n = vertices[i].normal;
        const float length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (length > 0.0f)
            for (int k = 0; k < 3; ++k) n[k] /= length;
        else
            n[1] = 1.0f;
    }

    bool tangentsValid = false;
    if (normalMapped && hasUvs && indices.size() >= 3)
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
    primitive.material = part.material;
    primitive.hasTangents = tangentsValid;
    for (int k = 0; k < 3; ++k)
    {
        primitive.boundsMin[k] = 1e30f;
        primitive.boundsMax[k] = -1e30f;
    }
    for (size_t i = 0; i < vertices.size(); ++i)
        for (int k = 0; k < 3; ++k)
        {
            const float v = vertices[i].position[k];
            primitive.boundsMin[k] = v < primitive.boundsMin[k] ? v : primitive.boundsMin[k];
            primitive.boundsMax[k] = v > primitive.boundsMax[k] ? v : primitive.boundsMax[k];
        }

    for (size_t i = 0; i < vertices.size(); ++i) out->vertices.push_back(vertices[i]);
    for (size_t i = 0; i < indices.size(); ++i) out->indices.push_back(indices[i]);
    out->primitives.push_back(primitive);
    return true;
}

} // namespace

bool loadObj(const char* path, GltfModel* out, bool allTangents)
{
    *out = GltfModel();
    directoryOf(path, &out->directory);

    ct::Vector<unsigned char> bytes;
    if (!readFile(path, &bytes))
    {
        out->error = "cannot read the OBJ file";
        return false;
    }

    ct::Vector<Vec3f> positions;
    ct::Vector<Vec3f> normals;
    ct::Vector<Vec2f> uvs;
    ct::Vector<RawMaterial> raw;
    ct::HashMap<ct::String, uint32_t> materialIndex;
    ct::Vector<Part> parts;
    ct::HashMap<uint32_t, uint32_t> partOfMaterial;
    uint32_t currentMaterial = 0;

    RawMaterial fallback;
    fallback.name = "default";
    raw.push_back(fallback);
    materialIndex[fallback.name] = 0;

    const char* p = reinterpret_cast<const char*>(bytes.data());
    const char* end = p + bytes.size();
    while (p < end)
    {
        p = skipSpaces(p, end);
        if (p >= end) break;

        if (keyword(p, end, "v"))
        {
            ++p;
            Vec3f value;
            value.x = parseFloat(p, end);
            value.y = parseFloat(p, end);
            value.z = parseFloat(p, end);
            positions.push_back(value);
        }
        else if (keyword(p, end, "vn"))
        {
            p += 2;
            Vec3f value;
            value.x = parseFloat(p, end);
            value.y = parseFloat(p, end);
            value.z = parseFloat(p, end);
            normals.push_back(value);
        }
        else if (keyword(p, end, "vt"))
        {
            p += 2;
            Vec2f value;
            value.x = parseFloat(p, end);
            value.y = 1.0f - parseFloat(p, end);
            uvs.push_back(value);
        }
        else if (keyword(p, end, "mtllib"))
        {
            ct::String name;
            lineValue(p + 6, end, &name);
            ct::String mtlPath;
            joinPath(out->directory, name, &mtlPath);
            const size_t first = raw.size();
            parseMtl(mtlPath.c_str(), &raw);
            for (size_t i = first; i < raw.size(); ++i)
                materialIndex[raw[i].name] = static_cast<uint32_t>(i);
        }
        else if (keyword(p, end, "usemtl"))
        {
            ct::String name;
            lineValue(p + 6, end, &name);
            const uint32_t* found = materialIndex.find(name);
            if (found)
                currentMaterial = *found;
            else
            {
                RawMaterial material;
                material.name = name;
                currentMaterial = static_cast<uint32_t>(raw.size());
                raw.push_back(material);
                materialIndex[name] = currentMaterial;
            }
        }
        else if (keyword(p, end, "f"))
        {
            const uint32_t* known = partOfMaterial.find(currentMaterial);
            uint32_t partIndex;
            if (known)
                partIndex = *known;
            else
            {
                partIndex = static_cast<uint32_t>(parts.size());
                Part part;
                part.material = static_cast<int>(currentMaterial);
                parts.push_back(part);
                partOfMaterial[currentMaterial] = partIndex;
            }
            Part& part = parts[partIndex];

            const char* cursor = p + 1;
            const char* faceEnd = lineEnd(cursor, end);
            VertexKey first = { 0, 0, 0 };
            VertexKey previous = { 0, 0, 0 };
            unsigned count = 0;
            while ((cursor = skipSpaces(cursor, faceEnd)) < faceEnd)
            {
                VertexKey key = { resolveIndex(parseInt(cursor, faceEnd), positions.size()), -1, -1 };
                if (cursor < faceEnd && *cursor == '/')
                {
                    ++cursor;
                    if (cursor < faceEnd && *cursor != '/')
                        key.uv = resolveIndex(parseInt(cursor, faceEnd), uvs.size());
                    if (cursor < faceEnd && *cursor == '/')
                    {
                        ++cursor;
                        key.normal = resolveIndex(parseInt(cursor, faceEnd), normals.size());
                    }
                }
                if (key.position < 0 || static_cast<size_t>(key.position) >= positions.size())
                {
                    out->error = "a face uses a position that does not exist";
                    return false;
                }
                if (key.uv >= 0 && static_cast<size_t>(key.uv) >= uvs.size()) key.uv = -1;
                if (key.normal >= 0 && static_cast<size_t>(key.normal) >= normals.size())
                    key.normal = -1;
                if (count == 0)
                    first = key;
                else if (count >= 2)
                {
                    part.corners.push_back(first);
                    part.corners.push_back(previous);
                    part.corners.push_back(key);
                }
                previous = key;
                ++count;
            }
        }
        p = nextLine(p, end);
    }

    out->materials.resize(raw.size());
    for (size_t i = 0; i < raw.size(); ++i)
    {
        GltfMaterial& material = out->materials[i];
        material.name = raw[i].name;
        for (int k = 0; k < 3; ++k) material.baseColor[k] = raw[i].diffuse[k];
        material.metallic = kObjMetallic;
        material.roughness = kObjRoughness;
        if (raw[i].cutout)
        {
            material.alpha = GltfMaterial::Alpha::Mask;
            material.doubleSided = true;
        }
        ct::String file;
        joinPath(out->directory, raw[i].albedo, &file);
        material.baseColorTexture = raw[i].albedo.empty() ? -1 : textureFor(out, file);
        joinPath(out->directory, raw[i].normalMap, &file);
        material.normalTexture = raw[i].normalMap.empty() ? -1 : textureFor(out, file);
    }

    GltfMesh mesh;
    mesh.firstPrimitive = 0;
    for (size_t i = 0; i < parts.size(); ++i)
        if (!parts[i].corners.empty())
            appendPart(parts[i],
                    allTangents ||
                            out->materials[static_cast<size_t>(parts[i].material)].normalTexture >= 0,
                    positions, normals, uvs, out);
    mesh.primitiveCount = static_cast<uint32_t>(out->primitives.size());
    if (mesh.primitiveCount == 0)
    {
        out->error = "the OBJ file has no faces";
        return false;
    }
    const char* slash = strrchr(path, '/');
    mesh.name = slash ? slash + 1 : path;
    out->meshes.push_back(mesh);

    GltfNode node;
    for (int i = 0; i < 16; ++i) node.world[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    node.mesh = 0;
    node.name = mesh.name;
    out->nodes.push_back(node);
    return true;
}

} // namespace zenapp
