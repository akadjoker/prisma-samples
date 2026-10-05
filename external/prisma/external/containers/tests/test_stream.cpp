#include <ct/stream.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

TEST(Stream, MemoryReadWriteSeekAndTake)
{
    ct::MemoryStream stream;
    EXPECT_TRUE(stream.write_all("hello", 5));
    EXPECT_EQ(stream.size(), 5);
    EXPECT_TRUE(stream.seek(1, ct::Seek::Set));
    char text[4] = {};
    EXPECT_TRUE(stream.read_exact(text, 3));
    EXPECT_EQ(std::memcmp(text, "ell", 3), 0);
    EXPECT_TRUE(stream.seek(0, ct::Seek::End));
    EXPECT_TRUE(stream.eof());
    ct::Vector<std::uint8_t> data = stream.take();
    ASSERT_EQ(data.size(), 5u);
    EXPECT_EQ(std::memcmp(data.data(), "hello", 5), 0);
}

TEST(Stream, ReadOnlyMemoryAndSubstream)
{
    const char text[] = "0123456789";
    ct::MemoryStream memory(text, 10);
    ct::SubStream sub(memory, 3, 4);
    char out[5] = {};
    EXPECT_TRUE(sub.read_exact(out, 4));
    EXPECT_EQ(std::memcmp(out, "3456", 4), 0);
    EXPECT_TRUE(sub.eof());
    EXPECT_FALSE(sub.write("x", 1));
    EXPECT_NE(sub.error(), nullptr);
}

TEST(Stream, FileHelpers)
{
    const char *path = "/tmp/ct_stream_test.bin";
    ASSERT_TRUE(ct::File::write_all(path, "abc", 3));
    ct::String text;
    ASSERT_TRUE(ct::File::read_all(path, text));
    EXPECT_EQ(text, "abc");
    EXPECT_EQ(ct::File::size(path), 3);
    EXPECT_TRUE(ct::File::remove(path));
}

TEST(Stream, SubstreamNoFimDevolveZeroSemErro)
{
    const char text[] = "0123456789";
    ct::MemoryStream memory(text, 10);
    ct::SubStream sub(memory, 3, 4);
    char out[8] = {};
    EXPECT_EQ(sub.read(out, 8), 4u);
    EXPECT_EQ(sub.read(out, 8), 0u);
    EXPECT_EQ(sub.error(), nullptr);
    EXPECT_TRUE(sub.eof());
    ct::String all;
    EXPECT_TRUE(sub.seek(0, ct::Seek::Set));
    EXPECT_TRUE(sub.read_all(all));
    EXPECT_EQ(all, "3456");
}

TEST(Stream, SubstreamLimitaSeAoTamanhoDaBase)
{
    const char text[] = "0123456789";
    ct::MemoryStream memory(text, 10);
    ct::SubStream tail(memory, 7, 100);
    EXPECT_EQ(tail.size(), 3);
    ct::String all;
    EXPECT_TRUE(tail.read_all(all));
    EXPECT_EQ(all, "789");
    EXPECT_TRUE(tail.eof());
    ct::SubStream beyond(memory, 50, 5);
    EXPECT_EQ(beyond.size(), 0);
    EXPECT_TRUE(beyond.eof());
    char c;
    EXPECT_EQ(beyond.read(&c, 1), 0u);
}

TEST(Stream, SubstreamSeekNaoFazOverflow)
{
    const char text[] = "0123456789";
    ct::MemoryStream memory(text, 10);
    ct::SubStream sub(memory, 1, 5);
    EXPECT_TRUE(sub.seek(1, ct::Seek::Set));
    EXPECT_FALSE(sub.seek(std::numeric_limits<std::int64_t>::max(), ct::Seek::Cur));
    EXPECT_FALSE(sub.seek(std::numeric_limits<std::int64_t>::min(), ct::Seek::Cur));
    EXPECT_EQ(sub.tell(), 1);
    ct::SubStream far(memory, std::numeric_limits<std::int64_t>::max(), 5);
    char c;
    EXPECT_EQ(far.read(&c, 1), 0u);
}

TEST(Stream, FileReadWritePreservaOConteudo)
{
    const char *path = "/tmp/ct_stream_rw.bin";
    ASSERT_TRUE(ct::File::write_all(path, "0123456789ABCDEF", 16));
    {
        ct::FileStream f(path, ct::FileStream::ReadWrite);
        ASSERT_TRUE(f.is_open());
        EXPECT_EQ(f.size(), 16);
        EXPECT_TRUE(f.seek(4, ct::Seek::Set));
        EXPECT_TRUE(f.write_all("xy", 2));
    }
    ct::String text;
    ASSERT_TRUE(ct::File::read_all(path, text));
    EXPECT_EQ(text, "0123xy6789ABCDEF");
    EXPECT_TRUE(ct::File::remove(path));
    {
        ct::FileStream f(path, ct::FileStream::ReadWrite);
        ASSERT_TRUE(f.is_open());
        EXPECT_EQ(f.size(), 0);
        EXPECT_TRUE(f.write_all("novo", 4));
    }
    ASSERT_TRUE(ct::File::read_all(path, text));
    EXPECT_EQ(text, "novo");
    EXPECT_TRUE(ct::File::remove(path));
}

TEST(Stream, FileSizeNaoLimpaEof)
{
    const char *path = "/tmp/ct_stream_eof.bin";
    ASSERT_TRUE(ct::File::write_all(path, "abc", 3));
    ct::FileStream f(path, ct::FileStream::Read);
    ASSERT_TRUE(f.is_open());
    char buf[8];
    EXPECT_EQ(f.read(buf, 8), 3u);
    EXPECT_TRUE(f.eof());
    EXPECT_EQ(f.size(), 3);
    EXPECT_TRUE(f.eof());
    EXPECT_EQ(f.read(buf, 8), 0u);
    f.close();
    EXPECT_TRUE(ct::File::remove(path));
}

TEST(Stream, SubstreamAcompanhaBaseQueCresce)
{
    ct::MemoryStream base;
    ASSERT_TRUE(base.write_all("abc", 3));
    ct::SubStream sub(base, 0, 100);
    char buf[16] = {};
    EXPECT_EQ(sub.read(buf, sizeof(buf)), 3u);
    EXPECT_EQ(std::memcmp(buf, "abc", 3), 0);
    EXPECT_TRUE(sub.eof());
    EXPECT_EQ(sub.size(), 3);
    EXPECT_EQ(sub.read(buf, sizeof(buf)), 0u);
    EXPECT_EQ(sub.error(), nullptr);
    ASSERT_TRUE(base.seek(0, ct::Seek::End));
    ASSERT_TRUE(base.write_all("defg", 4));
    EXPECT_EQ(sub.size(), 7);
    EXPECT_FALSE(sub.eof());
    EXPECT_EQ(sub.read(buf, sizeof(buf)), 4u);
    EXPECT_EQ(std::memcmp(buf, "defg", 4), 0);
    EXPECT_EQ(sub.error(), nullptr);
}

TEST(Stream, SubstreamComOffsetAlemDaBaseEFimSemErro)
{
    ct::MemoryStream base;
    ASSERT_TRUE(base.write_all("abc", 3));
    ct::SubStream sub(base, 10, 5);
    char c;
    EXPECT_EQ(sub.read(&c, 1), 0u);
    EXPECT_EQ(sub.error(), nullptr);
    EXPECT_TRUE(sub.eof());
}

TEST(Stream, SubstreamSobreFicheiroQueCresce)
{
    const char *path = "/tmp/ct_substream_grow.bin";
    ASSERT_TRUE(ct::File::write_all(path, "abc", 3));
    ct::FileStream reader(path, ct::FileStream::Read);
    ASSERT_TRUE(reader.is_open());
    ct::SubStream sub(reader, 0, 100);
    char buf[16] = {};
    EXPECT_EQ(sub.read(buf, sizeof(buf)), 3u);
    {
        std::FILE *f = std::fopen(path, "ab");
        ASSERT_NE(f, nullptr);
        std::fwrite("defg", 1, 4, f);
        std::fclose(f);
    }
    std::memset(buf, 0, sizeof(buf));
    EXPECT_EQ(sub.read(buf, sizeof(buf)), 4u);
    EXPECT_EQ(std::memcmp(buf, "defg", 4), 0);
    EXPECT_EQ(sub.size(), 7);
    ct::File::remove(path);
}
