#include <cgltf.h>
#define CGLTF_WRITE_IMPLEMENTATION
#include <cgltf_write.h>

#include <draco/compression/decode.h>

#include <ct/string.hpp>
#include <ct/vector.hpp>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

namespace
{

struct Decoded
{
    const cgltf_buffer_view* key;
    draco::Mesh* mesh;
};

struct Output
{
    ct::Vector<unsigned char> blob;
    ct::Vector<Decoded> meshes;
    cgltf_buffer_view* views;
    cgltf_size viewCount;
    cgltf_buffer* buffer;
    unsigned accessorsFilled;
};

bool readFile(const char* path, ct::Vector<unsigned char>* out)
{
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    fseek(file, 0, SEEK_END);
    const long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    out->resize(static_cast<size_t>(length));
    const size_t read = length > 0 ? fread(out->data(), 1, static_cast<size_t>(length), file) : 0;
    fclose(file);
    return read == static_cast<size_t>(length);
}

bool writeFile(const char* path, const void* data, size_t size)
{
    FILE* file = fopen(path, "wb");
    if (!file) return false;
    const size_t written = size > 0 ? fwrite(data, 1, size, file) : 0;
    fclose(file);
    return written == size;
}

void makeDirectories(const ct::String& path)
{
    ct::String current;
    for (size_t i = 0; i < path.size(); ++i)
    {
        if (path[i] == '/' && i > 0) mkdir(current.c_str(), 0755);
        current.append(1, path[i]);
    }
    mkdir(current.c_str(), 0755);
}

void directoryOf(const char* path, ct::String* out)
{
    const char* slash = strrchr(path, '/');
    *out = ct::String();
    if (!slash) out->append(".");
    else out->append(path, static_cast<size_t>(slash - path));
}

void stemOf(const char* path, ct::String* out)
{
    const char* slash = strrchr(path, '/');
    const char* name = slash ? slash + 1 : path;
    const char* dot = strrchr(name, '.');
    *out = ct::String();
    out->append(name, dot ? static_cast<size_t>(dot - name) : strlen(name));
}

draco::Mesh* findMesh(const cgltf_data* data, const cgltf_buffer_view* view, Output* output)
{
    for (size_t i = 0; i < output->meshes.size(); ++i)
        if (output->meshes[i].key == view) return output->meshes[i].mesh;
    draco::DecoderBuffer buffer;
    buffer.Init(reinterpret_cast<const char*>(cgltf_buffer_view_data(view)), view->size);
    draco::Decoder decoder;
    const auto type = decoder.GetEncodedGeometryType(&buffer);
    if (!type.ok() || type.value() != draco::EncodedGeometryType::TRIANGULAR_MESH) return nullptr;
    auto status = decoder.DecodeMeshFromBuffer(&buffer);
    if (!status.ok()) return nullptr;
    Decoded decoded;
    decoded.key = view;
    decoded.mesh = std::move(status).value().release();
    output->meshes.push_back(decoded);
    return decoded.mesh;
}

cgltf_buffer_view* addView(Output* output, cgltf_size size, cgltf_buffer_view_type type)
{
    while (output->blob.size() % 4 != 0) output->blob.push_back(0);
    cgltf_buffer_view* view = &output->views[output->viewCount++];
    memset(view, 0, sizeof(*view));
    view->buffer = output->buffer;
    view->offset = output->blob.size();
    view->size = size;
    view->type = type;
    output->blob.resize(output->blob.size() + size);
    return view;
}

template<typename T>
bool convertFaces(cgltf_accessor* target, const draco::Mesh* mesh, Output* output)
{
    const cgltf_size size = mesh->num_faces() * 3 * sizeof(T);
    cgltf_buffer_view* view = addView(output, size, cgltf_buffer_view_type_indices);
    T* dest = reinterpret_cast<T*>(output->blob.data() + view->offset);
    for (uint32_t id = 0, n = mesh->num_faces(); id < n; ++id)
    {
        const draco::Mesh::Face& face = mesh->face(draco::FaceIndex(id));
        *dest++ = static_cast<T>(face[0].value());
        *dest++ = static_cast<T>(face[1].value());
        *dest++ = static_cast<T>(face[2].value());
    }
    target->buffer_view = view;
    target->offset = 0;
    target->stride = sizeof(T);
    return true;
}

template<typename T>
bool convertAttribs(cgltf_accessor* target, const draco::PointAttribute* attribute, uint32_t count,
        Output* output)
{
    const int components = attribute->num_components();
    if (components > 4 || static_cast<cgltf_size>(components) !=
                                  cgltf_calc_size(target->type, cgltf_component_type_r_8u))
        return false;
    cgltf_buffer_view* view = addView(output, count * components * sizeof(T),
            cgltf_buffer_view_type_vertices);
    T* dest = reinterpret_cast<T*>(output->blob.data() + view->offset);
    for (draco::PointIndex i(0); i < count; ++i, dest += components)
        attribute->ConvertValue(attribute->mapped_index(i), components, dest);
    target->buffer_view = view;
    target->offset = 0;
    target->stride = components * sizeof(T);
    return true;
}

bool fillIndices(cgltf_accessor* target, const draco::Mesh* mesh, Output* output)
{
    if (target->buffer_view) return true;
    if (target->count != static_cast<cgltf_size>(mesh->num_faces()) * 3)
    {
        fprintf(stderr, "index count %zu, the Draco mesh has %u\n", static_cast<size_t>(target->count),
                static_cast<unsigned>(mesh->num_faces() * 3));
        return false;
    }
    ++output->accessorsFilled;
    switch (target->component_type)
    {
    case cgltf_component_type_r_8u: return convertFaces<uint8_t>(target, mesh, output);
    case cgltf_component_type_r_16u: return convertFaces<uint16_t>(target, mesh, output);
    case cgltf_component_type_r_32u: return convertFaces<uint32_t>(target, mesh, output);
    default: return false;
    }
}

bool fillAttribute(cgltf_accessor* target, const draco::Mesh* mesh, uint32_t id, Output* output)
{
    if (target->buffer_view) return true;
    const draco::PointAttribute* attribute = mesh->GetAttributeByUniqueId(id);
    if (!attribute) return false;
    const uint32_t count = mesh->num_points();
    if (target->count != count)
    {
        fprintf(stderr, "vertex count %zu, the Draco mesh has %u\n", static_cast<size_t>(target->count),
                count);
        return false;
    }
    ++output->accessorsFilled;
    switch (target->component_type)
    {
    case cgltf_component_type_r_8: return convertAttribs<int8_t>(target, attribute, count, output);
    case cgltf_component_type_r_8u: return convertAttribs<uint8_t>(target, attribute, count, output);
    case cgltf_component_type_r_16: return convertAttribs<int16_t>(target, attribute, count, output);
    case cgltf_component_type_r_16u: return convertAttribs<uint16_t>(target, attribute, count, output);
    case cgltf_component_type_r_32u: return convertAttribs<uint32_t>(target, attribute, count, output);
    case cgltf_component_type_r_32f: return convertAttribs<float>(target, attribute, count, output);
    default: return false;
    }
}

cgltf_accessor* findAccessor(const cgltf_primitive& primitive, cgltf_attribute_type type,
        cgltf_int index)
{
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i)
        if (primitive.attributes[i].type == type && primitive.attributes[i].index == index)
            return primitive.attributes[i].data;
    return nullptr;
}

void fixView(cgltf_buffer_view** view, const cgltf_buffer_view* oldViews, cgltf_buffer_view* newViews)
{
    if (*view) *view = newViews + (*view - oldViews);
}

void removeExtension(char** names, cgltf_size* count, const char* name)
{
    cgltf_size kept = 0;
    for (cgltf_size i = 0; i < *count; ++i)
        if (strcmp(names[i], name) != 0) names[kept++] = names[i];
    *count = kept;
}

bool convert(cgltf_data* data, Output* output)
{
    cgltf_size missing = 0;
    for (cgltf_size i = 0; i < data->accessors_count; ++i)
        if (!data->accessors[i].buffer_view && !data->accessors[i].is_sparse) ++missing;

    cgltf_buffer_view* oldViews = data->buffer_views;
    output->views = static_cast<cgltf_buffer_view*>(
            calloc(data->buffer_views_count + missing + 1, sizeof(cgltf_buffer_view)));
    memcpy(output->views, oldViews, sizeof(cgltf_buffer_view) * data->buffer_views_count);
    output->viewCount = data->buffer_views_count;
    for (cgltf_size i = 0; i < data->accessors_count; ++i)
    {
        cgltf_accessor& accessor = data->accessors[i];
        fixView(&accessor.buffer_view, oldViews, output->views);
        if (accessor.is_sparse)
        {
            fixView(&accessor.sparse.indices_buffer_view, oldViews, output->views);
            fixView(&accessor.sparse.values_buffer_view, oldViews, output->views);
        }
    }
    for (cgltf_size i = 0; i < data->images_count; ++i)
        fixView(&data->images[i].buffer_view, oldViews, output->views);
    for (cgltf_size m = 0; m < data->meshes_count; ++m)
        for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p)
            if (data->meshes[m].primitives[p].has_draco_mesh_compression)
                fixView(&data->meshes[m].primitives[p].draco_mesh_compression.buffer_view, oldViews,
                        output->views);
    data->buffer_views = output->views;
    data->memory.free_func(data->memory.user_data, oldViews);

    for (cgltf_size m = 0; m < data->meshes_count; ++m)
    {
        for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p)
        {
            cgltf_primitive& primitive = data->meshes[m].primitives[p];
            if (!primitive.has_draco_mesh_compression) continue;
            const cgltf_draco_mesh_compression& draco = primitive.draco_mesh_compression;
            draco::Mesh* mesh = findMesh(data, draco.buffer_view, output);
            if (!mesh)
            {
                fprintf(stderr, "mesh %zu primitive %zu: Draco decoding failed\n",
                        static_cast<size_t>(m), static_cast<size_t>(p));
                return false;
            }
            if (primitive.indices && !fillIndices(primitive.indices, mesh, output)) return false;
            for (cgltf_size a = 0; a < draco.attributes_count; ++a)
            {
                const uint32_t id = static_cast<uint32_t>(draco.attributes[a].data - data->accessors);
                cgltf_accessor* accessor =
                        findAccessor(primitive, draco.attributes[a].type, draco.attributes[a].index);
                if (!accessor) continue;
                if (!fillAttribute(accessor, mesh, id, output))
                {
                    fprintf(stderr, "mesh %zu primitive %zu: attribute %u failed\n",
                            static_cast<size_t>(m), static_cast<size_t>(p), id);
                    return false;
                }
            }
            primitive.has_draco_mesh_compression = 0;
        }
    }
    data->buffer_views_count = output->viewCount;
    removeExtension(data->extensions_used, &data->extensions_used_count, "KHR_draco_mesh_compression");
    removeExtension(data->extensions_required, &data->extensions_required_count,
            "KHR_draco_mesh_compression");
    return true;
}

void copyImages(const cgltf_data* data, const ct::String& sourceDirectory,
        const ct::String& outputDirectory)
{
    for (cgltf_size i = 0; i < data->images_count; ++i)
    {
        const char* uri = data->images[i].uri;
        if (!uri || strncmp(uri, "data:", 5) == 0) continue;
        ct::Vector<char> decoded;
        decoded.resize(strlen(uri) + 1);
        memcpy(decoded.data(), uri, decoded.size());
        cgltf_decode_uri(decoded.data());
        ct::String from = sourceDirectory;
        from += "/";
        from += decoded.data();
        ct::String to = outputDirectory;
        to += "/";
        to += decoded.data();
        ct::String toDirectory;
        directoryOf(to.c_str(), &toDirectory);
        makeDirectories(toDirectory);
        ct::Vector<unsigned char> bytes;
        if (!readFile(from.c_str(), &bytes) || !writeFile(to.c_str(), bytes.data(), bytes.size()))
            fprintf(stderr, "cannot copy %s\n", from.c_str());
    }
}

bool verify(const char* path)
{
    cgltf_options options;
    memset(&options, 0, sizeof(options));
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path, &data) != cgltf_result_success) return false;
    bool ok = cgltf_load_buffers(&options, data, path) == cgltf_result_success &&
              cgltf_validate(data) == cgltf_result_success;
    size_t triangles = 0;
    size_t vertices = 0;
    for (cgltf_size m = 0; m < data->meshes_count && ok; ++m)
        for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p)
        {
            const cgltf_primitive& primitive = data->meshes[m].primitives[p];
            if (primitive.has_draco_mesh_compression) ok = false;
            if (primitive.indices) triangles += primitive.indices->count / 3;
            const cgltf_accessor* position = findAccessor(primitive, cgltf_attribute_type_position, 0);
            if (!position || !position->buffer_view) ok = false;
            else vertices += position->count;
        }
    printf("verify: %zu meshes, %zu vertices, %zu triangles, %s\n",
            static_cast<size_t>(data->meshes_count), vertices, triangles, ok ? "ok" : "FAILED");
    cgltf_free(data);
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: gltf_undraco input.gltf|input.glb output_directory\n");
        return 2;
    }
    cgltf_options options;
    memset(&options, 0, sizeof(options));
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, argv[1], &data) != cgltf_result_success)
    {
        fprintf(stderr, "cannot parse %s\n", argv[1]);
        return 1;
    }
    if (cgltf_load_buffers(&options, data, argv[1]) != cgltf_result_success ||
            data->buffers_count != 1 || !data->buffers[0].data)
    {
        fprintf(stderr, "%s needs exactly one buffer that can be loaded\n", argv[1]);
        return 1;
    }

    Output output;
    output.buffer = &data->buffers[0];
    output.accessorsFilled = 0;
    output.blob.resize(data->buffers[0].size);
    memcpy(output.blob.data(), data->buffers[0].data, data->buffers[0].size);
    if (!convert(data, &output)) return 1;

    ct::String stem;
    stemOf(argv[1], &stem);
    ct::String sourceDirectory;
    directoryOf(argv[1], &sourceDirectory);
    ct::String outputDirectory;
    outputDirectory.append(argv[2]);
    makeDirectories(outputDirectory);

    ct::String binName = stem;
    binName += ".bin";
    ct::String binPath = outputDirectory;
    binPath += "/";
    binPath += binName;
    ct::String gltfPath = outputDirectory;
    gltfPath += "/";
    gltfPath += stem;
    gltfPath += ".gltf";

    data->buffers[0].data = output.blob.data();
    data->buffers[0].size = output.blob.size();
    data->buffers[0].uri = const_cast<char*>(binName.c_str());
    if (!writeFile(binPath.c_str(), output.blob.data(), output.blob.size()))
    {
        fprintf(stderr, "cannot write %s\n", binPath.c_str());
        return 1;
    }
    if (cgltf_write_file(&options, gltfPath.c_str(), data) != cgltf_result_success)
    {
        fprintf(stderr, "cannot write %s\n", gltfPath.c_str());
        return 1;
    }
    copyImages(data, sourceDirectory, outputDirectory);
    printf("%s: %zu Draco meshes, %u accessors filled, %zu bytes of data\n", gltfPath.c_str(),
            output.meshes.size(), output.accessorsFilled, output.blob.size());
    const bool ok = verify(gltfPath.c_str());
    for (size_t i = 0; i < output.meshes.size(); ++i) delete output.meshes[i].mesh;
    return ok ? 0 : 1;
}
