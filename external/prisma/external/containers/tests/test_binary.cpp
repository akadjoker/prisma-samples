#include <ct/binary.hpp>
#include <gtest/gtest.h>

TEST(Binary, RoundTripAndLittleEndian)
{
    ct::MemoryStream stream;
    ct::BinaryWriter writer(stream);
    writer.u32(0x01020304); writer.i64(-42); writer.f32(1.5f); writer.varint(300); writer.string("ola");
    ASSERT_TRUE(writer.ok());
    const ct::Span<const std::uint8_t> raw = stream.data();
    ASSERT_GE(raw.size(), 4u);
    EXPECT_EQ(raw[0], 4); EXPECT_EQ(raw[1], 3); EXPECT_EQ(raw[2], 2); EXPECT_EQ(raw[3], 1);
    ASSERT_TRUE(stream.seek(0, ct::Seek::Set));
    ct::BinaryReader reader(stream);
    EXPECT_EQ(reader.u32(), 0x01020304u); EXPECT_EQ(reader.i64(), -42); EXPECT_FLOAT_EQ(reader.f32(), 1.5f); EXPECT_EQ(reader.varint(), 300u);
    ct::String text; EXPECT_TRUE(reader.string(text)); EXPECT_EQ(text, "ola"); EXPECT_TRUE(reader.ok());
}

TEST(Binary, TruncatedReadFails)
{
    const std::uint8_t raw[] = {1, 2}; ct::MemoryStream stream(raw, sizeof(raw)); ct::BinaryReader reader(stream);
    EXPECT_EQ(reader.u32(), 0u); EXPECT_FALSE(reader.ok()); EXPECT_EQ(reader.u8(), 0);
}

namespace
{
    class SoLeitura : public ct::Stream
    {
    public:
        SoLeitura(const void *data, std::size_t n) : data_(static_cast<const std::uint8_t *>(data)), size_(n), pos_(0) {}
        std::size_t read(void *dst, std::size_t n) override
        {
            std::size_t left = size_ - pos_;
            if (n > left) n = left;
            std::memcpy(dst, data_ + pos_, n);
            pos_ += n;
            return n;
        }
        std::size_t write(const void *, std::size_t) override { return 0; }
        bool seek(std::int64_t, ct::Seek) override { return false; }
        std::int64_t tell() const override { return -1; }
        std::int64_t size() const override { return -1; }
        void close() override {}
        bool is_open() const override { return true; }
        bool eof() const override { return pos_ >= size_; }
        bool can_read() const override { return true; }
        bool can_write() const override { return false; }
        bool can_seek() const override { return false; }
    private:
        const std::uint8_t *data_;
        std::size_t size_, pos_;
    };
}

TEST(Binary, PrefixoDeTamanhoMaiorQueOStreamFalhaSemAlocar)
{
    const std::uint8_t bytes[] = {0xff, 0xff, 0xff, 0xff, 'a', 'b'};
    {
        ct::MemoryStream memory(bytes, sizeof(bytes));
        ct::BinaryReader reader(memory);
        ct::String out("lixo");
        EXPECT_FALSE(reader.string(out));
        EXPECT_FALSE(reader.ok());
        EXPECT_LE(out.capacity(), 8192u);
    }
    {
        ct::MemoryStream memory(bytes, sizeof(bytes));
        ct::BinaryReader reader(memory);
        ct::Vector<std::uint8_t> out;
        EXPECT_FALSE(reader.bytes(out, 0xffffffffu));
        EXPECT_LE(out.capacity(), 8192u);
    }
    {
        SoLeitura raw(bytes, sizeof(bytes));
        ct::BinaryReader reader(raw);
        ct::String out;
        EXPECT_FALSE(reader.string(out));
        EXPECT_FALSE(reader.ok());
        EXPECT_LE(out.capacity(), 8192u);
    }
}

TEST(Binary, StringGrandeEmStreamNaoPosicionavel)
{
    ct::MemoryStream memory;
    ct::BinaryWriter writer(memory);
    ct::String big(200000, 'z');
    writer.string(big);
    ct::Vector<std::uint8_t> raw = memory.take();
    SoLeitura stream(raw.data(), raw.size());
    ct::BinaryReader reader(stream);
    ct::String out;
    ASSERT_TRUE(reader.string(out));
    EXPECT_EQ(out.size(), big.size());
    EXPECT_TRUE(out == big);
}

TEST(Binary, StringEmStreamSemSizeNemTellFunciona)
{
    ct::MemoryStream memory;
    {
        ct::BinaryWriter writer(memory);
        writer.string("ola");
        writer.u32(42);
    }
    ct::Vector<std::uint8_t> raw = memory.take();
    SoLeitura stream(raw.data(), raw.size());
    ct::BinaryReader reader(stream);
    ct::String out;
    ASSERT_TRUE(reader.string(out));
    EXPECT_EQ(out, "ola");
    EXPECT_EQ(reader.u32(), 42u);
    EXPECT_TRUE(reader.ok());
}

namespace
{
    class ContaConsultas : public ct::MemoryStream
    {
    public:
        ContaConsultas(const void *data, std::size_t n) : ct::MemoryStream(data, n), consultas(0) {}
        std::int64_t tell() const override
        {
            ++consultas;
            return ct::MemoryStream::tell();
        }
        std::int64_t size() const override
        {
            ++consultas;
            return ct::MemoryStream::size();
        }
        mutable int consultas;
    };
}

TEST(Binary, StringsCurtasNaoConsultamTamanhoNemPosicaoDoStream)
{
    ct::MemoryStream memory;
    {
        ct::BinaryWriter writer(memory);
        for (int i = 0; i < 1000; ++i)
            writer.string("abcdefgh");
    }
    ct::Vector<std::uint8_t> raw = memory.take();
    ContaConsultas stream(raw.data(), raw.size());
    ct::BinaryReader reader(stream);
    ct::String out;
    for (int i = 0; i < 1000; ++i)
    {
        ASSERT_TRUE(reader.string(out));
        EXPECT_EQ(out.size(), 8u);
    }
    EXPECT_EQ(stream.consultas, 0);
}

TEST(Binary, ManyStringsRoundTripThroughFileStream)
{
    const char *path = "/tmp/ct_binary_many.bin";
    {
        ct::FileStream out(path, ct::FileStream::Write);
        ct::BinaryWriter writer(out);
        for (int i = 0; i < 5000; ++i)
        {
            ct::String s;
            s.append_number(i);
            s.append(ct::String(static_cast<std::size_t>(i % 70000), 'q'));
            writer.string(s);
        }
        ASSERT_TRUE(writer.ok());
    }
    ct::FileStream in(path, ct::FileStream::Read);
    ct::BinaryReader reader(in);
    for (int i = 0; i < 5000; ++i)
    {
        ct::String s;
        ASSERT_TRUE(reader.string(s)) << i;
        ct::String expect;
        expect.append_number(i);
        expect.append(ct::String(static_cast<std::size_t>(i % 70000), 'q'));
        ASSERT_TRUE(s == expect) << i;
    }
    ct::String extra;
    EXPECT_FALSE(reader.string(extra));
    ct::File::remove(path);
}
