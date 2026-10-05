#include <ct/xml.hpp>
#include <ct/xml_stream.hpp>

#include <gtest/gtest.h>

#include <ctime>
#include <string>

#include <cstring>
#include <utility>
#include <limits>

using ct::String;
using ct::Xml;

TEST(Xml, ParsesStream)
{
    ct::MemoryStream stream("<root><item/></root>", 20);
    Xml::Error error;
    Xml value = ct::parse_xml(stream, &error);
    EXPECT_FALSE(error);
    EXPECT_EQ(value.tag(), "root");
    EXPECT_EQ(value.size(), 1u);
}

namespace
{
    Xml parse_ok(const char *text)
    {
        Xml::Error err;
        Xml x = Xml::parse(text, &err);
        EXPECT_FALSE(static_cast<bool>(err))
            << "input: " << text << " erro: " << (err.message ? err.message : "");
        return x;
    }

    Xml::Error parse_err(const char *text)
    {
        Xml::Error err;
        Xml x = Xml::parse(text, &err);
        EXPECT_TRUE(static_cast<bool>(err)) << "devia falhar: " << text;
        EXPECT_TRUE(x.tag().empty());
        return err;
    }
}

TEST(Xml, DefaultIsEmpty)
{
    Xml x;
    EXPECT_TRUE(x.tag().empty());
    EXPECT_TRUE(x.text().empty());
    EXPECT_TRUE(x.empty());
    EXPECT_EQ(x.size(), 0u);
    EXPECT_EQ(x.attributes().size(), 0u);
}

TEST(Xml, SelfClosing)
{
    Xml x = parse_ok("<root/>");
    EXPECT_EQ(x.tag(), "root");
    EXPECT_TRUE(x.empty());
    EXPECT_TRUE(x.text().empty());
}

TEST(Xml, SelfClosingWithSpace)
{
    Xml x = parse_ok("<root />");
    EXPECT_EQ(x.tag(), "root");
}

TEST(Xml, OpenClose)
{
    Xml x = parse_ok("<root></root>");
    EXPECT_EQ(x.tag(), "root");
    EXPECT_TRUE(x.empty());
}

TEST(Xml, WhitespaceIsTrimmedFromWhitespaceOnlyToken)
{
    Xml x = parse_ok("  \n <root/>\n ");
    EXPECT_EQ(x.tag(), "root");
}

TEST(Xml, AttributesDoubleAndSingleQuotes)
{
    Xml x = parse_ok("<root a=\"1\" b='2'/>");
    ASSERT_TRUE(x.has_attr("a"));
    ASSERT_TRUE(x.has_attr("b"));
    EXPECT_STREQ(x.attr_cstr("a"), "1");
    EXPECT_STREQ(x.attr_cstr("b"), "2");
    EXPECT_FALSE(x.has_attr("c"));
    EXPECT_STREQ(x.attr_cstr("c", "def"), "def");
}

TEST(Xml, AttributeEntitiesAndNumericRefs)
{
    Xml x = parse_ok("<root a=\"x&amp;y&lt;z&gt;w&quot;q&apos;r\" b=\"&#65;&#x42;\"/>");
    EXPECT_STREQ(x.attr_cstr("a"), "x&y<z>w\"q'r");
    EXPECT_STREQ(x.attr_cstr("b"), "AB");
}

TEST(Xml, AttributeWhitespaceNormalization)
{

    Xml x = parse_ok("<root a=\"x\ty\nz\"/>");
    EXPECT_STREQ(x.attr_cstr("a"), "x y z");
}

TEST(Xml, TypedAttributeGetters)
{
    Xml x = parse_ok("<root i=\"-42\" u=\"42\" f=\"3.5\" t=\"true\" tt=\"1\" "
                      "fa=\"false\" ff=\"0\" trunc=\"7.9\"/>");
    EXPECT_EQ(x.attr_int("i"), -42);
    EXPECT_EQ(x.attr_uint("u"), 42u);
    EXPECT_DOUBLE_EQ(x.attr_double("f"), 3.5);
    EXPECT_TRUE(x.attr_bool("t"));
    EXPECT_TRUE(x.attr_bool("tt"));
    EXPECT_FALSE(x.attr_bool("fa"));
    EXPECT_FALSE(x.attr_bool("ff"));
    EXPECT_EQ(x.attr_int("trunc"), 7); 
    EXPECT_EQ(x.attr_int("nope", -1), -1);
    EXPECT_EQ(x.attr_uint("nope", 9u), 9u);
    EXPECT_DOUBLE_EQ(x.attr_double("nope", 1.5), 1.5);
    EXPECT_EQ(x.attr_bool("nope", true), true);
}

TEST(Xml, SetAttrOverwritesAndAdds)
{
    Xml x("root");
    x.set_attr("a", "1");
    x.set_attr("a", "2");
    x.set_attr("b", "3");
    ASSERT_EQ(x.attributes().size(), 2u);
    EXPECT_STREQ(x.attr_cstr("a"), "2");
    EXPECT_STREQ(x.attr_cstr("b"), "3");
    EXPECT_TRUE(x.erase_attr("a"));
    EXPECT_FALSE(x.has_attr("a"));
    EXPECT_FALSE(x.erase_attr("a"));
}

TEST(Xml, TextContent)
{
    Xml x = parse_ok("<root>ola mundo</root>");
    EXPECT_EQ(x.text(), "ola mundo");
}

TEST(Xml, TextEntities)
{
    Xml x = parse_ok("<root>a&amp;b &lt;tag&gt; &#9731;</root>");
    EXPECT_EQ(x.text(), "a&b <tag> \xE2\x98\x83"); 
}

TEST(Xml, Cdata)
{
    Xml x = parse_ok("<data><![CDATA[1,2,<3>,&4]]></data>");
    EXPECT_EQ(x.text(), "1,2,<3>,&4"); 
}

TEST(Xml, CdataConcatenatesWithSurroundingText)
{
    Xml x = parse_ok("<data>a<![CDATA[b]]>c</data>");
    EXPECT_EQ(x.text(), "abc");
}

TEST(Xml, TextTrimmed)
{
    Xml x = parse_ok("<data>\n  1,2,3  \n</data>");
    EXPECT_EQ(x.text_trimmed(), "1,2,3");
}

TEST(Xml, InsignificantWhitespaceBetweenChildrenIsDropped)
{
    Xml x = parse_ok("<root>\n  <a/>\n  <b/>\n</root>");
    EXPECT_TRUE(x.text().empty()); 
    EXPECT_EQ(x.size(), 2u);
}

TEST(Xml, WhitespaceOnlyLeafKeepsText)
{

    Xml x = parse_ok("<sep>   </sep>");
    EXPECT_EQ(x.text(), "   ");
}

TEST(Xml, ChildrenAndLookup)
{
    Xml x = parse_ok("<map><tileset id=\"1\"/><layer name=\"chao\"/><layer name=\"topo\"/></map>");
    EXPECT_EQ(x.size(), 3u);
    Xml *ts = x.child("tileset");
    ASSERT_NE(ts, nullptr);
    EXPECT_STREQ(ts->attr_cstr("id"), "1");
    Xml *layer = x.child("layer"); 
    ASSERT_NE(layer, nullptr);
    EXPECT_STREQ(layer->attr_cstr("name"), "chao");
    EXPECT_EQ(x.child("nope"), nullptr);
}

TEST(Xml, AddChildAndAccessors)
{
    Xml root("root");
    root.add_child(Xml("a")).set_attr("k", "v");
    root.add_child(Xml("b"));
    EXPECT_EQ(root.size(), 2u);
    EXPECT_STREQ(root.child("a")->attr_cstr("k"), "v");
}

TEST(Xml, NestedTraversal)
{
    Xml x = parse_ok(
        "<map>"
        "<layer name=\"chao\"><data encoding=\"csv\">1,2,3,4</data></layer>"
        "</map>");
    Xml *layer = x.child("layer");
    ASSERT_NE(layer, nullptr);
    Xml *data = layer->child("data");
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->text(), "1,2,3,4");
}

TEST(Xml, PrologAndCommentsAreSkipped)
{
    Xml x = parse_ok("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                      "<!-- comentario antes -->\n"
                      "<root>\n"
                      "  <!-- comentario dentro -->\n"
                      "  <a/>\n"
                      "</root>\n"
                      "<!-- comentario depois -->\n");
    EXPECT_EQ(x.tag(), "root");
    EXPECT_EQ(x.size(), 1u);
}

TEST(Xml, DoctypeIsSkipped)
{
    Xml x = parse_ok("<!DOCTYPE root SYSTEM \"x.dtd\">\n<root/>");
    EXPECT_EQ(x.tag(), "root");
}

TEST(Xml, DoctypeWithInternalSubsetIsSkipped)
{
    Xml x = parse_ok("<!DOCTYPE root [ <!ELEMENT root (#PCDATA)> ]>\n<root/>");
    EXPECT_EQ(x.tag(), "root");
}

TEST(Xml, ProcessingInstructionInsideContentIsSkipped)
{
    Xml x = parse_ok("<root><?some-pi data?><a/></root>");
    EXPECT_EQ(x.size(), 1u);
}

TEST(Xml, Utf8Bom)
{
    const char with_bom[] = "\xEF\xBB\xBF<root/>";
    Xml x = parse_ok(with_bom);
    EXPECT_EQ(x.tag(), "root");
}

TEST(Xml, ErrorEmptyInput)
{
    Xml::Error e = parse_err("");
    EXPECT_TRUE(static_cast<bool>(e));
}

TEST(Xml, ErrorNoRootElement)
{
    parse_err("   \n  ");
}

TEST(Xml, ErrorUnclosedTag)
{
    parse_err("<a><b>");
}

TEST(Xml, ErrorMismatchedCloseTag)
{
    parse_err("<a><b></c></a>");
}

TEST(Xml, ErrorTrailingGarbage)
{
    parse_err("<a/><b/>");
}

TEST(Xml, ErrorRawLessThanInAttribute)
{
    parse_err("<a b=\"<\"/>");
}

TEST(Xml, ErrorUnknownEntityWithoutDtd)
{
    Xml::Error e = parse_err("<a>&bogus;</a>");
    EXPECT_STREQ(e.message, "entidade desconhecida (sem suporte a DTD)");
}

TEST(Xml, ErrorUnterminatedComment)
{
    parse_err("<a><!-- sem fecho</a>");
}

TEST(Xml, ErrorUnterminatedCdata)
{
    parse_err("<a><![CDATA[sem fecho</a>");
}

TEST(Xml, ErrorNullInput)
{
    Xml::Error e;
    Xml x = Xml::parse(static_cast<const char *>(nullptr), &e);
    EXPECT_TRUE(static_cast<bool>(e));
    EXPECT_TRUE(x.tag().empty());
}

TEST(Xml, ErrorLineAndColumn)
{
    Xml::Error e = parse_err("<a>\n  <b>\n</a>"); 
    EXPECT_GE(e.line, 1u);
    EXPECT_GE(e.column, 1u);
}

TEST(Xml, ErrorDepthLimit)
{
    ct::String s;
    for (std::size_t i = 0; i < Xml::kMaxDepth + 10; ++i)
        s.append("<a>", 3);
    Xml::Error e;
    Xml::parse(s, &e);
    EXPECT_TRUE(static_cast<bool>(e));
}

TEST(Xml, DumpSelfClosingWhenEmpty)
{
    Xml x("root");
    x.set_attr("a", "1");
    EXPECT_EQ(x.dump(), "<root a=\"1\"/>");
}

TEST(Xml, DumpEscapesAttributesAndText)
{
    Xml x("root");
    x.set_attr("a", "x&y<z>\"q");
    x.set_text("a&b<c>");
    ct::String out = x.dump();

    EXPECT_TRUE(out.find("&amp;") != ct::String::npos);
    EXPECT_TRUE(out.find("&lt;") != ct::String::npos);
    EXPECT_TRUE(out.find("&quot;") != ct::String::npos);
}

TEST(Xml, DumpCompactRoundTrip)
{
    Xml original = parse_ok(
        "<map version=\"1.10\"><tileset firstgid=\"1\" name=\"a &amp; b\"/>"
        "<layer name=\"chao\"><data>1,2,3</data></layer></map>");
    ct::String dumped = original.dump();
    Xml reparsed = parse_ok(dumped.c_str());
    EXPECT_EQ(reparsed.tag(), "map");
    EXPECT_STREQ(reparsed.attr_cstr("version"), "1.10");
    EXPECT_STREQ(reparsed.child("tileset")->attr_cstr("name"), "a & b");
    EXPECT_EQ(reparsed.child("layer")->child("data")->text(), "1,2,3");
}

TEST(Xml, DumpIndentedIsParseable)
{
    Xml original = parse_ok("<a><b/><c><d/></c></a>");
    ct::String dumped = original.dump(2);
    Xml reparsed = parse_ok(dumped.c_str());
    EXPECT_EQ(reparsed.tag(), "a");
    EXPECT_EQ(reparsed.size(), 2u);
    EXPECT_EQ(reparsed.child("c")->size(), 1u);
}

TEST(Xml, DumpDocumentHasPrologAndParses)
{
    Xml x("root");
    x.add_child(Xml("a"));
    ct::String doc = x.dump_document(2);
    EXPECT_EQ(doc.find("<?xml"), 0u);
    Xml::Error err;
    Xml reparsed = Xml::parse(doc, &err);
    EXPECT_FALSE(static_cast<bool>(err));
    EXPECT_EQ(reparsed.tag(), "root");
}

TEST(Xml, DumpAttributeSpecialWhitespaceRoundTrips)
{
    Xml x("root");
    x.set_attr("a", "line1\nline2\ttab");
    ct::String dumped = x.dump();
    Xml reparsed = parse_ok(dumped.c_str());
    EXPECT_STREQ(reparsed.attr_cstr("a"), "line1\nline2\ttab");
}

TEST(Xml, CopyIsDeep)
{
    Xml a("root");
    a.add_child(Xml("child1"));
    Xml b = a;
    b.add_child(Xml("child2"));
    EXPECT_EQ(a.size(), 1u);
    EXPECT_EQ(b.size(), 2u);
}

TEST(Xml, CopyAssignIsDeep)
{
    Xml a("root");
    a.add_child(Xml("child1"));
    Xml b("outro");
    b = a;
    b.add_child(Xml("child2"));
    EXPECT_EQ(a.size(), 1u);
    EXPECT_EQ(b.size(), 2u);
    EXPECT_EQ(b.tag(), "root");
}

TEST(Xml, MoveLeavesSourceValidAndEmpty)
{
    Xml a("root");
    a.add_child(Xml("child1"));
    Xml b(ct::detail::move(a));
    EXPECT_EQ(b.size(), 1u);
    EXPECT_EQ(a.size(), 0u); 
    EXPECT_TRUE(a.empty());
}

TEST(Xml, MoveAssignLeavesSourceValid)
{
    Xml a("root");
    a.add_child(Xml("child1"));
    Xml b;
    b = ct::detail::move(a);
    EXPECT_EQ(b.size(), 1u);
    EXPECT_TRUE(a.empty());
    a.add_child(Xml("still works")); 
    EXPECT_EQ(a.size(), 1u);
}

TEST(Xml, ParseFromCtString)
{
    ct::String s = "<root a=\"1\"/>";
    Xml x = parse_ok(s.c_str());
    EXPECT_STREQ(x.attr_cstr("a"), "1");
    Xml::Error err;
    Xml y = Xml::parse(s, &err);
    EXPECT_FALSE(static_cast<bool>(err));
    EXPECT_EQ(y.tag(), "root");
}

TEST(Xml, AttrIntEAttrUintNaoFazemOverflow)
{
    ct::Xml x = ct::Xml::parse(
        "<a min='-9223372036854775808' max='9223372036854775807' over='9223372036854775808' "
        "under='-9223372036854775809' huge='99999999999999999999' big='1e30' neg='-1e30' "
        "umax='18446744073709551615' uover='18446744073709551616' ok='12.75'/>");
    ASSERT_EQ(x.tag(), "a");
    EXPECT_EQ(x.attr_int("min"), std::numeric_limits<std::int64_t>::min());
    EXPECT_EQ(x.attr_int("max"), std::numeric_limits<std::int64_t>::max());
    EXPECT_EQ(x.attr_int("over", -1), -1);
    EXPECT_EQ(x.attr_int("under", -1), -1);
    EXPECT_EQ(x.attr_int("huge", -1), -1);
    EXPECT_EQ(x.attr_int("big", -1), -1);
    EXPECT_EQ(x.attr_int("neg", -1), -1);
    EXPECT_EQ(x.attr_int("ok"), 12);
    EXPECT_EQ(x.attr_uint("umax"), std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(x.attr_uint("uover", 5u), 5u);
    EXPECT_EQ(x.attr_uint("huge", 5u), 5u);
    EXPECT_EQ(x.attr_uint("big", 5u), 5u);
    EXPECT_EQ(x.attr_uint("ok"), 12u);
}

TEST(Xml, ErroDeStreamNaoApontaParaOStream)
{
    ct::Xml::Error error;
    {
        ct::FileStream closed;
        ct::Xml x = ct::parse_xml(closed, &error);
        EXPECT_TRUE(x.empty());
    }
    ASSERT_TRUE(error);
    EXPECT_GT(std::strlen(error.message), 0u);
}

TEST(Xml, ColunaDoErroIgnoraBom)
{
    ct::Xml::Error err;
    ct::Xml::parse("\xEF\xBB\xBF<a><b></a>", &err);
    ASSERT_TRUE(err);
    EXPECT_EQ(err.line, 1u);
    EXPECT_LT(err.column, 12u);
    ct::Xml::Error plain;
    ct::Xml::parse("<a><b></a>", &plain);
    ASSERT_TRUE(plain);
    EXPECT_EQ(err.column, plain.column);
}

TEST(Xml, ConteudoMistoMantemAOrdemNoRoundTrip)
{
    const char *doc = "<p>Hello <b>bold</b> world <i>it</i>!</p>";
    Xml x = parse_ok(doc);
    EXPECT_EQ(x.text(), "Hello ");
    ASSERT_EQ(x.size(), 2u);
    EXPECT_EQ(x.children()[0].tag(), "b");
    EXPECT_EQ(x.children()[0].text(), "bold");
    EXPECT_EQ(x.children()[0].tail(), " world ");
    EXPECT_EQ(x.children()[1].tail(), "!");
    EXPECT_TRUE(x.has_mixed_content());
    EXPECT_EQ(x.dump(), doc);
    EXPECT_EQ(x.dump(2), doc);
    Xml again = parse_ok(x.dump(2).c_str());
    EXPECT_EQ(again.dump(), doc);
    Xml copy = x;
    EXPECT_EQ(copy.dump(), doc);
    Xml moved = std::move(copy);
    EXPECT_EQ(moved.dump(), doc);
}

TEST(Xml, ConteudoMistoComCdataEEntidadesEEspacosSoNoMeio)
{
    Xml x = parse_ok("<t>a &amp; <![CDATA[<raw>]]><e/> &lt;b<f/></t>");
    EXPECT_EQ(x.text(), "a & <raw>");
    EXPECT_EQ(x.children()[0].tail(), " <b");
    EXPECT_TRUE(x.children()[1].tail().empty());
    EXPECT_EQ(x.dump(), "<t>a &amp; &lt;raw&gt;<e/> &lt;b<f/></t>");
    Xml only_ws = parse_ok("<r>\n  <a/>\n  <b/>\n</r>");
    EXPECT_FALSE(only_ws.has_mixed_content());
    EXPECT_TRUE(only_ws.children()[0].tail().empty());
    EXPECT_EQ(only_ws.dump(2), "<r>\n  <a/>\n  <b/>\n</r>");
}

TEST(Xml, SetTailConstroiConteudoMisto)
{
    Xml p("p");
    p.set_text("x ");
    Xml b("b");
    b.set_text("y");
    b.set_tail(" z");
    p.add_child(std::move(b));
    EXPECT_EQ(p.dump(), "<p>x <b>y</b> z</p>");
    Xml back = parse_ok(p.dump().c_str());
    EXPECT_EQ(back.children()[0].tail(), " z");
}

TEST(Xml, AtributosRepetidosSaoErro)
{
    ct::Xml::Error err;
    ct::Xml::parse("<a x='1' y='2' x='3'/>", &err);
    ASSERT_TRUE(err);
    EXPECT_STREQ(err.message, "atributo repetido");
    EXPECT_EQ(err.column, 16u);
    ct::Xml::Error ok;
    ct::Xml x = ct::Xml::parse("<a x='1' y='2' z='3'><b x='1'/></a>", &ok);
    EXPECT_FALSE(ok);
    EXPECT_EQ(x.attr_int("z"), 3);
}

TEST(Xml, AtributosPrecisamDeEspacoEntreSi)
{
    ct::Xml::Error err;
    ct::Xml::parse("<a x='1'y='2'/>", &err);
    ASSERT_TRUE(err);
    EXPECT_STREQ(err.message, "esperado espaco entre atributos");
    ct::Xml::Error ok;
    ct::Xml::parse("<a x='1' y='2'/>", &ok);
    EXPECT_FALSE(ok);
    ct::Xml::parse("<a x='1'\n\ty='2'></a>", &ok);
    EXPECT_FALSE(ok);
    ct::Xml::parse("<a x='1'/>", &ok);
    EXPECT_FALSE(ok);
    ct::Xml::parse("<a x='1'>t</a>", &ok);
    EXPECT_FALSE(ok);
}

TEST(Xml, DoctypeComAspasEComentariosNoSubconjunto)
{
    ct::Xml::Error err;
    ct::Xml a = ct::Xml::parse("<!DOCTYPE a SYSTEM \"x>y\"><a/>", &err);
    EXPECT_FALSE(err);
    EXPECT_EQ(a.tag(), "a");
    ct::Xml b = ct::Xml::parse("<!DOCTYPE a PUBLIC 'p>q' \"s>t\" [ <!ENTITY e \"v>w\"> <!-- it's ] > --> ]><a/>", &err);
    EXPECT_FALSE(err);
    EXPECT_EQ(b.tag(), "a");
    ct::Xml::parse("<!DOCTYPE a SYSTEM \"x><a/>", &err);
    ASSERT_TRUE(err);
    ct::Xml::Error plain;
    ct::Xml c = ct::Xml::parse("<!DOCTYPE note [<!ELEMENT note (#PCDATA)>]><note>t</note>", &plain);
    EXPECT_FALSE(plain);
    EXPECT_EQ(c.text(), "t");
}

TEST(Xml, EspacoEntreElementosEmConteudoMistoEPreservado)
{
    const char *doc = "<p>Hello <b>b</b> <i>i</i> world</p>";
    Xml x = parse_ok(doc);
    ASSERT_EQ(x.size(), 2u);
    EXPECT_EQ(x.children()[0].tail(), " ");
    EXPECT_EQ(x.dump(), doc);
    Xml tail_only = parse_ok("<p><b>b</b> <i>i</i>x</p>");
    EXPECT_EQ(tail_only.children()[0].tail(), " ");
    EXPECT_EQ(tail_only.dump(), "<p><b>b</b> <i>i</i>x</p>");
}

TEST(Xml, MuitosAtributosNaoSaoQuadraticosEDuplicadoContinuaDetectado)
{
    std::string doc = "<a";
    const int n = 40000;
    for (int i = 0; i < n; ++i)
        doc += " a" + std::to_string(i) + "=''";
    doc += "/>";
    const std::clock_t begin = std::clock();
    Xml::Error err;
    Xml x = Xml::parse(doc.c_str(), &err);
    const double seconds = double(std::clock() - begin) / CLOCKS_PER_SEC;
    EXPECT_FALSE(static_cast<bool>(err));
    EXPECT_EQ(x.attributes().size(), static_cast<std::size_t>(n));
    EXPECT_LT(seconds, 5.0);
    doc.insert(doc.size() - 2, " a39999='x'");
    Xml::Error dup;
    Xml::parse(doc.c_str(), &dup);
    ASSERT_TRUE(static_cast<bool>(dup));
    EXPECT_STREQ(dup.message, "atributo repetido");
    Xml::Error nove;
    Xml::parse("<a b='1' c='2' d='3' e='4' f='5' g='6' h='7' i='8' j='9' b='x'/>", &nove);
    ASSERT_TRUE(static_cast<bool>(nove));
    EXPECT_STREQ(nove.message, "atributo repetido");
}

TEST(Xml, DoctypeComInstrucaoDeProcessamentoComAspasNoSubconjunto)
{
    Xml x = parse_ok("<!DOCTYPE a [<?pi it's?>]><a/>");
    EXPECT_EQ(x.tag(), "a");
    Xml y = parse_ok("<!DOCTYPE a [<?pi \"?>]><b/>");
    EXPECT_EQ(y.tag(), "b");
    parse_err("<!DOCTYPE a [<?pi it's]><a/>");
}

namespace
{
    unsigned xml_rng_state = 99u;
    unsigned xml_rng()
    {
        xml_rng_state = xml_rng_state * 1664525u + 1013904223u;
        return xml_rng_state >> 8;
    }

    String xml_random_text()
    {
        const char *pieces[] = {"a", "b ", " ", "x&amp;y", "&lt;t&gt;", "  z", "w\n", "."};
        String out;
        const unsigned n = xml_rng() % 3;
        for (unsigned i = 0; i < n; ++i)
            out.append(pieces[xml_rng() % 8]);
        return out;
    }

    void xml_random_tree(String &out, int depth)
    {
        out.append("<n");
        const unsigned attrs = xml_rng() % 12;
        for (unsigned i = 0; i < attrs; ++i)
        {
            out.append(" a");
            out.append_number(i);
            out.append("=\"v");
            out.append_number(xml_rng() % 10);
            out.append("\"");
        }
        out.append(">");
        out.append(xml_random_text());
        const unsigned children = depth < 3 ? xml_rng() % 4 : 0;
        for (unsigned c = 0; c < children; ++c)
        {
            xml_random_tree(out, depth + 1);
            out.append(xml_random_text());
        }
        out.append("</n>");
    }
}

TEST(Xml, ArvoresAleatoriasDeConteudoMistoFazemRoundTrip)
{
    for (int round = 0; round < 300; ++round)
    {
        String source;
        xml_random_tree(source, 0);
        Xml::Error err;
        Xml first = Xml::parse(source.c_str(), &err);
        ASSERT_FALSE(static_cast<bool>(err)) << source.c_str();
        String once = first.dump();
        Xml second = Xml::parse(once.c_str(), &err);
        ASSERT_FALSE(static_cast<bool>(err)) << once.c_str();
        ASSERT_EQ(second.dump(), once) << source.c_str();
        Xml copy = first;
        ASSERT_EQ(copy.dump(), once);
    }
}
