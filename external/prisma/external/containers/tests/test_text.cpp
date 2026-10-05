#include <ct/text.hpp>
#include <gtest/gtest.h>

TEST(Text, LinesBomAndWords)
{
    const char source[] = "\xef\xbb\xbf" "first\r\nsecond\rthird\nlast";
    ct::MemoryStream stream(source, sizeof(source) - 1); ct::TextReader reader(stream); ct::String line;
    ASSERT_TRUE(reader.read_line(line)); EXPECT_EQ(line, "first"); ASSERT_TRUE(reader.read_line(line)); EXPECT_EQ(line, "second"); ASSERT_TRUE(reader.read_line(line)); EXPECT_EQ(line, "third"); ASSERT_TRUE(reader.read_line(line)); EXPECT_EQ(line, "last"); EXPECT_FALSE(reader.read_line(line));
    ct::MemoryStream words("  42  3.5 hello", 15); ct::TextReader tokens(words); long long integer; double real;
    EXPECT_TRUE(tokens.read_int(integer)); EXPECT_EQ(integer, 42); EXPECT_TRUE(tokens.read_double(real)); EXPECT_DOUBLE_EQ(real, 3.5); EXPECT_TRUE(tokens.read_word(line)); EXPECT_EQ(line, "hello");
}

TEST(Text, WriterBuffersAndFormats)
{
    ct::MemoryStream stream; { ct::TextWriter writer(stream); writer.line("hello").number(42).number(std::size_t(0)).number(-1L).write(' ').number(1.25, 2).write('\n').fmt("%s:%d", "x", 7); EXPECT_TRUE(writer.flush()); }
    ct::String text; ASSERT_TRUE(stream.seek(0, ct::Seek::Set)); ASSERT_TRUE(ct::TextReader(stream).read_all(text)); EXPECT_EQ(text, "hello\n420-1 1.2\nx:7");
}

TEST(Text, ReadAllDepoisDeReadLineDevolveORestoESaltaOBom)
{
    const char data[] = "line1\nline2\nline3\n";
    ct::MemoryStream memory(data, sizeof(data) - 1);
    ct::TextReader reader(memory);
    ct::String line, rest;
    ASSERT_TRUE(reader.read_line(line));
    EXPECT_EQ(line, "line1");
    ASSERT_TRUE(reader.read_all(rest));
    EXPECT_EQ(rest, "line2\nline3\n");
    const char bom[] = "\xEF\xBB\xBFhello";
    ct::MemoryStream with_bom(bom, sizeof(bom) - 1);
    ct::TextReader bom_reader(with_bom);
    EXPECT_EQ(bom_reader.peek(), 'h');
    ASSERT_TRUE(bom_reader.read_all(rest));
    EXPECT_EQ(rest, "hello");
    ct::MemoryStream again(bom, sizeof(bom) - 1);
    ASSERT_TRUE(ct::TextReader(again).read_all(rest));
    EXPECT_EQ(rest, "hello");
    const char two[] = "ab";
    ct::MemoryStream short_stream(two, 2);
    ASSERT_TRUE(ct::TextReader(short_stream).read_all(rest));
    EXPECT_EQ(rest, "ab");
}

namespace
{
    enum class Modo : int
    {
        Leitura = 4
    };
}

TEST(TextWriter, NumberAceitaEnums)
{
    ct::MemoryStream out;
    {
        ct::TextWriter w(out);
        w.number(Modo::Leitura);
    }
    ct::String text(reinterpret_cast<const char *>(out.data().data()), out.data().size());
    EXPECT_EQ(text, "4");
}
