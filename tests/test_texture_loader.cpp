#include "Check.h"
#include "TextureLoader.h"

#include <stdio.h>
#include <stdlib.h>

namespace
{

ct::Vector<unsigned char> make(dds::DXGI_FORMAT format, unsigned width, unsigned height,
        unsigned mips, unsigned array, bool cube, unsigned depth, size_t payload)
{
    ct::Vector<unsigned char> file;
    file.resize(sizeof(dds::Header) + payload);
    memset(file.data(), 0, file.size());
    dds::write_header(file.data(), format, width, height, mips, array, cube, depth);
    return file;
}

bool parseFile(const ct::Vector<unsigned char>& file, bool srgb, zenapp::DdsInfo* info)
{
    return zenapp::parseDds(file.data(), file.size(), srgb, info);
}

} // namespace

int main()
{
    using namespace prisma;
    zenapp::DdsInfo info;

    ct::Vector<unsigned char> plain =
            make(dds::DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4, 3, 1, false, 0, 84);
    CHECK(parseFile(plain, false, &info));
    CHECK(info.type == TextureType::Texture2D && info.format == TextureFormat::RGBA8);
    CHECK(info.width == 4 && info.height == 4 && info.mipLevels == 3 && info.layers == 1);
    CHECK(parseFile(plain, true, &info) && info.format == TextureFormat::RGBA8Srgb);

    ct::Vector<unsigned char> shortFile =
            make(dds::DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4, 3, 1, false, 0, 83);
    CHECK(!parseFile(shortFile, false, &info));

    ct::Vector<unsigned char> block = make(dds::DXGI_FORMAT_BC1_UNORM, 8, 8, 4, 1, false, 0, 56);
    CHECK(parseFile(block, false, &info));
    CHECK(info.format == TextureFormat::BC1 && info.mipLevels == 4);
    CHECK(parseFile(block, true, &info) && info.format == TextureFormat::BC1Srgb);

    ct::Vector<unsigned char> cube = make(dds::DXGI_FORMAT_R8G8B8A8_UNORM, 2, 2, 1, 6, true, 0, 96);
    CHECK(parseFile(cube, false, &info));
    CHECK(info.type == TextureType::TextureCube && info.layers == 6);
    ct::Vector<unsigned char> cubes =
            make(dds::DXGI_FORMAT_R8G8B8A8_UNORM, 2, 2, 1, 12, true, 0, 192);
    CHECK(parseFile(cubes, false, &info));
    CHECK(info.type == TextureType::TextureCubeArray && info.layers == 12);

    ct::Vector<unsigned char> volume =
            make(dds::DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4, 1, 1, false, 4, 256);
    CHECK(parseFile(volume, false, &info));
    CHECK(info.type == TextureType::Texture3D && info.depth == 4 && info.layers == 1);

    ct::Vector<unsigned char> array =
            make(dds::DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4, 1, 3, false, 0, 192);
    CHECK(parseFile(array, false, &info));
    CHECK(info.type == TextureType::Texture2DArray && info.layers == 3);

    ct::Vector<unsigned char> bgra =
            make(dds::DXGI_FORMAT_B8G8R8A8_UNORM, 2, 2, 1, 1, false, 0, 16);
    CHECK(parseFile(bgra, false, &info));
    CHECK(info.format == TextureFormat::RGBA8 && info.swapRedBlue && !info.opaqueAlpha);

    ct::Vector<unsigned char> half =
            make(dds::DXGI_FORMAT_R16G16B16A16_FLOAT, 2, 2, 1, 1, false, 0, 32);
    CHECK(parseFile(half, false, &info) && info.format == TextureFormat::RGBA16F);

    CHECK(!zenapp::parseDds(nullptr, 0, false, &info));
    unsigned char garbage[256] = { 1, 2, 3 };
    CHECK(!zenapp::parseDds(garbage, sizeof(garbage), false, &info));

    DriverDesc desc;
    desc.type = DriverType::Null;
    Driver* driver = createDriver(desc);
    CHECK(driver != nullptr);
    if (driver)
    {
        CHECK(zenapp::createTextureFromDds(driver, plain.data(), plain.size(), false, false)
                        .valid());
        CHECK(zenapp::createTextureFromDds(driver, bgra.data(), bgra.size(), true, false).valid());
        CHECK(zenapp::createTextureFromDds(driver, volume.data(), volume.size(), false, false)
                        .valid());
        CHECK(!zenapp::createTextureFromDds(driver, block.data(), block.size(), false, false)
                        .valid());
        CHECK(!zenapp::createTextureFromDds(driver, shortFile.data(), shortFile.size(), false,
                false)
                        .valid());
        destroyDriver(driver);
    }

    const char* const media[] = { "misc/seafloor.dds", "Lobby/LobbyCube.dds",
        "Light Probes/uffizi_cross.dds", "Squid/Misc_Boss_3_1024.dds", "Tiny/Tiny_skin.dds",
        "misc/MarbleClouds.dds", "misc/NormTest.dds" };
    int found = 0;
    for (size_t i = 0; i < sizeof(media) / sizeof(media[0]); ++i)
    {
        char path[512];
        ct::Vector<unsigned char> file;
        if (!zenapp::mediaPath(media[i], path, sizeof(path)) || !zenapp::readFile(path, &file))
            continue;
        ++found;
        const bool parsed = zenapp::parseDds(file.data(), file.size(), false, &info);
        if (!parsed) printf("could not parse %s\n", path);
        CHECK(parsed);
    }
    printf("media files parsed: %d of %d\n", found,
            static_cast<int>(sizeof(media) / sizeof(media[0])));

    if (failures)
    {
        printf("%d failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
