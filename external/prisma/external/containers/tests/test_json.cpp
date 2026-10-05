#include <ct/json.hpp>
#include <ct/json_stream.hpp>

#include <gtest/gtest.h>

#include <clocale>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

using ct::Json;

TEST(Json, ParsesStream)
{
    ct::MemoryStream stream("{\"answer\":42}", 13);
    Json::Error error;
    Json value = ct::parse_json(stream, &error);
    EXPECT_FALSE(error);
    EXPECT_EQ(value["answer"].as_int(), 42);
}
using ct::String;

namespace
{
    Json parse_ok(const char *text)
    {
        Json::Error err;
        Json j = Json::parse(text, &err);
        EXPECT_FALSE(static_cast<bool>(err))
            << "input: " << text << " erro: " << (err.message ? err.message : "");
        return j;
    }

    Json::Error parse_err(const char *text)
    {
        Json::Error err;
        Json j = Json::parse(text, &err);
        EXPECT_TRUE(static_cast<bool>(err)) << "devia falhar: " << text;
        EXPECT_TRUE(j.is_null());
        return err;
    }

    std::string nest(const char *open, const char *close, int depth, const char *core)
    {
        std::string s;
        for (int i = 0; i < depth; ++i)
            s += open;
        s += core;
        for (int i = 0; i < depth; ++i)
            s += close;
        return s;
    }
}

TEST(Json, DefaultIsNullAndLayout)
{
    Json j;
    EXPECT_TRUE(j.is_null());
    EXPECT_EQ(j.type(), Json::Null);
    EXPECT_STREQ(j.type_name(), "null");
    EXPECT_EQ(j.size(), 0u);
    EXPECT_TRUE(j.empty());
    EXPECT_EQ(sizeof(Json), sizeof(String) + sizeof(void *)); 
}

TEST(Json, ScalarConstructors)
{
    EXPECT_TRUE(Json(nullptr).is_null());
    EXPECT_TRUE(Json(true).is_bool());
    EXPECT_TRUE(Json(42).is_int());
    EXPECT_TRUE(Json(-7L).is_int());
    EXPECT_TRUE(Json(42u).is_uint());
    EXPECT_TRUE(Json(std::size_t(42)).is_uint());
    EXPECT_TRUE(Json(1.5).is_real());
    EXPECT_TRUE(Json(1.5f).is_real());
    EXPECT_TRUE(Json("texto").is_string());
    EXPECT_TRUE(Json(String("texto")).is_string());

    EXPECT_TRUE(Json(0).is_int());
    EXPECT_EQ(Json(0).as_int(), 0);

    EXPECT_TRUE(Json(42).is_number());
    EXPECT_FALSE(Json(true).is_number());
    EXPECT_EQ(Json(3.5).as_double(), 3.5);
    EXPECT_EQ(Json(42).as_double(), 42.0); 
    EXPECT_STREQ(Json("abc").as_cstr(), "abc");
    EXPECT_STREQ(Json(1).as_cstr("fallback"), "fallback"); 
    EXPECT_EQ(Json("abc").as_int(99), 99);
    EXPECT_TRUE(Json(1).as_bool(true));
}

TEST(Json, StringWithEmbeddedNul)
{
    Json j(String("a\0b", 3));
    EXPECT_EQ(j.str().size(), 3u);
    EXPECT_EQ(j.dump(), String("\"a\\u0000b\""));
    Json back = parse_ok(j.dump().c_str());
    EXPECT_EQ(back.str().size(), 3u);
    EXPECT_TRUE(back == j);
}

TEST(Json, NumberParsingKeepsIntegerType)
{
    EXPECT_TRUE(parse_ok("0").is_int());
    EXPECT_EQ(parse_ok("0").as_int(), 0);
    EXPECT_EQ(parse_ok("-0").as_int(), 0);
    EXPECT_TRUE(parse_ok("-0.0").is_real());
    EXPECT_TRUE(std::signbit(parse_ok("-0.0").as_double()));

    EXPECT_EQ(parse_ok("123").as_int(), 123);
    EXPECT_EQ(parse_ok("-123").as_int(), -123);
    EXPECT_TRUE(parse_ok("1.0").is_real());
    EXPECT_TRUE(parse_ok("1e2").is_real());
    EXPECT_EQ(parse_ok("1e2").as_double(), 100.0);
    EXPECT_EQ(parse_ok("1E+2").as_double(), 100.0);
    EXPECT_EQ(parse_ok("1e-2").as_double(), 0.01);
}

TEST(Json, NumberExtremes)
{
    const std::int64_t imin = (std::numeric_limits<std::int64_t>::min)();
    const std::int64_t imax = (std::numeric_limits<std::int64_t>::max)();
    const std::uint64_t umax = (std::numeric_limits<std::uint64_t>::max)();

    Json a = parse_ok("9223372036854775807"); 
    EXPECT_TRUE(a.is_int());
    EXPECT_EQ(a.as_int(), imax);

    Json b = parse_ok("-9223372036854775808"); 
    EXPECT_TRUE(b.is_int());
    EXPECT_EQ(b.as_int(), imin);

    Json c = parse_ok("9223372036854775808"); 
    EXPECT_TRUE(c.is_uint());
    EXPECT_EQ(c.as_uint(), static_cast<std::uint64_t>(imax) + 1);

    Json d = parse_ok("18446744073709551615"); 
    EXPECT_TRUE(d.is_uint());
    EXPECT_EQ(d.as_uint(), umax);

    Json e = parse_ok("18446744073709551616"); 
    EXPECT_TRUE(e.is_real());
    EXPECT_DOUBLE_EQ(e.as_double(), 18446744073709551616.0);

    Json f = parse_ok("-99999999999999999999"); 
    EXPECT_TRUE(f.is_real());

    EXPECT_EQ(parse_ok("9223372036854775807").dump(), String("9223372036854775807"));
    EXPECT_EQ(parse_ok("-9223372036854775808").dump(), String("-9223372036854775808"));
    EXPECT_EQ(parse_ok("18446744073709551615").dump(), String("18446744073709551615"));
}

TEST(Json, DoubleRoundTripPrecision)
{
    const double values[] = {0.1,
                             1.0 / 3.0,
                             3.141592653589793,
                             2.2250738585072014e-308, 
                             1.7976931348623157e308,  
                             1e-7,
                             123456789.123456789,
                             -0.000001,
                             5e-324}; 
    for (double v : values)
    {
        Json j(v);
        String text = j.dump();
        Json back = parse_ok(text.c_str());
        EXPECT_TRUE(back.is_real()) << text.c_str();
        EXPECT_EQ(back.as_double(), v) << "round-trip falhou: " << text.c_str();
    }
}

TEST(Json, DoubleKeepsTypeAcrossRoundTrip)
{

    EXPECT_EQ(Json(1.0).dump(), String("1.0"));
    EXPECT_EQ(Json(-2.0).dump(), String("-2.0"));
    EXPECT_TRUE(parse_ok(Json(1.0).dump().c_str()).is_real());
    EXPECT_EQ(Json(100.0).dump(), String("100.0"));
    EXPECT_EQ(Json(1e30).dump(), String("1e+30"));
}

TEST(Json, NonFiniteDumpsAsNull)
{
    const double inf = (std::numeric_limits<double>::infinity)();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(Json(inf).dump(), String("null"));
    EXPECT_EQ(Json(-inf).dump(), String("null"));
    EXPECT_EQ(Json(nan).dump(), String("null"));

    EXPECT_STREQ(parse_err("1e400").message, "numero fora do alcance do double");
    EXPECT_STREQ(parse_err("-1e999").message, "numero fora do alcance do double");
    EXPECT_STREQ(parse_err("[1,1e400]").message, "numero fora do alcance do double");
    EXPECT_EQ(parse_ok("1.7976931348623157e308").as_double(),
              (std::numeric_limits<double>::max)()); 

    Json tiny = parse_ok("1e-400"); 
    EXPECT_EQ(tiny.as_double(), 0.0);
    EXPECT_EQ(tiny.dump(), String("0.0"));
}

TEST(Json, ManyDigitsMantissa)
{

    Json j = parse_ok("1.2345678901234567890123456789");
    EXPECT_DOUBLE_EQ(j.as_double(), 1.2345678901234567890123456789);
    Json k = parse_ok("123456789012345678901234567890");
    EXPECT_DOUBLE_EQ(k.as_double(), 123456789012345678901234567890.0);
}

TEST(Json, LocaleWithCommaDecimalSeparator)
{
    const char *tried[] = {"pt_PT.UTF-8", "de_DE.UTF-8", "fr_FR.UTF-8", "pt_BR.UTF-8"};
    const char *got = nullptr;
    for (const char *l : tried)
        if ((got = std::setlocale(LC_NUMERIC, l)) != nullptr)
            break;
    if (!got)
        GTEST_SKIP() << "sem locale de virgula decimal instalado";

    EXPECT_EQ(parse_ok("3.5").as_double(), 3.5);
    EXPECT_EQ(parse_ok("1.2345678901234567890123456789").as_double(),
              1.2345678901234567890123456789);
    EXPECT_EQ(Json(3.5).dump(), String("3.5"));
    EXPECT_EQ(Json(0.1).dump(), String("0.1"));
    std::setlocale(LC_NUMERIC, "C");
}

TEST(Json, StringEscapesParse)
{
    EXPECT_EQ(parse_ok(R"("a\"b")").str(), String("a\"b"));
    EXPECT_EQ(parse_ok(R"("a\\b")").str(), String("a\\b"));
    EXPECT_EQ(parse_ok(R"("a\/b")").str(), String("a/b"));
    EXPECT_EQ(parse_ok(R"("\b\f\n\r\t")").str(), String("\b\f\n\r\t"));
    EXPECT_EQ(parse_ok(R"("")").str(), String(""));
    EXPECT_EQ(parse_ok(R"("\u0041")").str(), String("A"));
    EXPECT_EQ(parse_ok(R"("\u00e9")").str(), String("\xc3\xa9"));     
    EXPECT_EQ(parse_ok(R"("\u20ac")").str(), String("\xe2\x82\xac")); 
    EXPECT_EQ(parse_ok(R"("\ud834\udd1e")").str(),
              String("\xf0\x9d\x84\x9e")); 
    EXPECT_EQ(parse_ok("\"acentua\xc3\xa7\xc3\xa3o\"").str(),
              String("acentua\xc3\xa7\xc3\xa3o")); 
    EXPECT_EQ(parse_ok(R"("\u0000")").str().size(), 1u); 
    EXPECT_EQ(parse_ok(R"("a\u0000b")").str().size(), 3u);
}

TEST(Json, StringEscapesDump)
{
    EXPECT_EQ(Json("a\"b").dump(), String("\"a\\\"b\""));
    EXPECT_EQ(Json("a\\b").dump(), String("\"a\\\\b\""));
    EXPECT_EQ(Json("\n\t\r\b\f").dump(), String("\"\\n\\t\\r\\b\\f\""));
    EXPECT_EQ(Json("\x1f").dump(), String("\"\\u001f\""));
    EXPECT_EQ(Json("a/b").dump(), String("\"a/b\""));           
    EXPECT_EQ(Json("\xc3\xa9").dump(), String("\"\xc3\xa9\"")); 
    EXPECT_EQ(Json("").dump(), String("\"\""));
}

TEST(Json, StringErrors)
{
    EXPECT_STREQ(parse_err(R"("sem fecho)").message, "string sem aspas de fecho");
    EXPECT_STREQ(parse_err("\"controlo\nno meio\"").message,
                 "caracter de controlo cru dentro da string");
    EXPECT_STREQ(parse_err(R"("\q")").message, "escape desconhecido");
    EXPECT_STREQ(parse_err(R"("\u12")").message, "escape \\u incompleto");
    EXPECT_STREQ(parse_err(R"("\u12g4")").message, "digito hexadecimal invalido no \\u");
    EXPECT_STREQ(parse_err(R"("\ud834")").message, "surrogate alto sem o par \\u seguinte");
    EXPECT_STREQ(parse_err(R"("\ud834\u0041")").message,
                 "segundo \\u nao e um surrogate baixo");
    EXPECT_STREQ(parse_err(R"("\udd1e")").message, "surrogate baixo sem o alto");
    EXPECT_STREQ(parse_err("\"fim\\").message, "escape incompleto");
}

// ================= erros de sintaxe =================

TEST(Json, SyntaxErrors)
{
    EXPECT_STREQ(parse_err("").message, "fim de input inesperado");
    EXPECT_STREQ(parse_err("   \n\t ").message, "fim de input inesperado");
    EXPECT_STREQ(parse_err("{").message, "objeto sem '}'");
    EXPECT_STREQ(parse_err("[").message, "fim de input inesperado");
    EXPECT_STREQ(parse_err("[1").message, "array sem ']'");
    EXPECT_STREQ(parse_err("[1,]").message, "valor inesperado"); // virgula a mais
    EXPECT_STREQ(parse_err(R"({"a":1,})").message, "esperava uma chave entre aspas");
    EXPECT_STREQ(parse_err(R"({a:1})").message, "esperava uma chave entre aspas");
    EXPECT_STREQ(parse_err(R"({"a" 1})").message, "esperava ':' depois da chave");
    EXPECT_STREQ(parse_err(R"({"a":1 "b":2})").message, "esperava ',' ou '}'");
    EXPECT_STREQ(parse_err("[1 2]").message, "esperava ',' ou ']'");
    EXPECT_STREQ(parse_err("tru").message, "literal invalido (esperava true)");
    EXPECT_STREQ(parse_err("nul").message, "literal invalido (esperava null)");
    EXPECT_STREQ(parse_err("fals").message, "literal invalido (esperava false)");
    EXPECT_STREQ(parse_err("False").message, "valor inesperado");
    EXPECT_STREQ(parse_err("{} lixo").message, "lixo depois do valor JSON");
    EXPECT_STREQ(parse_err("1 2").message, "lixo depois do valor JSON");
    EXPECT_STREQ(parse_err("'aspas simples'").message, "valor inesperado");
}

TEST(Json, NumberSyntaxErrors)
{
    EXPECT_STREQ(parse_err("01").message, "lixo depois do valor JSON"); // zero a esquerda
    EXPECT_STREQ(parse_err("-").message, "numero incompleto");
    EXPECT_STREQ(parse_err("1.").message, "faltam digitos depois do ponto decimal");
    EXPECT_STREQ(parse_err(".5").message, "valor inesperado");
    EXPECT_STREQ(parse_err("+1").message, "valor inesperado");
    EXPECT_STREQ(parse_err("1e").message, "expoente sem digitos");
    EXPECT_STREQ(parse_err("1e+").message, "expoente sem digitos");
    EXPECT_STREQ(parse_err("NaN").message, "valor inesperado");
    EXPECT_STREQ(parse_err("Infinity").message, "valor inesperado");
    EXPECT_STREQ(parse_err("0x10").message, "lixo depois do valor JSON");
}

TEST(Json, ErrorPositionLineAndColumn)
{
    Json::Error err;
    Json::parse("{\n  \"a\": 1,\n  \"b\": tru\n}", &err);
    ASSERT_TRUE(static_cast<bool>(err));
    EXPECT_EQ(err.line, 3u);
    EXPECT_EQ(err.column, 8u);
    EXPECT_EQ(err.offset, 19u); 

    Json::Error err2;
    Json::parse("[1,2,", &err2);
    ASSERT_TRUE(static_cast<bool>(err2));
    EXPECT_EQ(err2.line, 1u);
    EXPECT_EQ(err2.column, 6u);

    Json::Error err3;
    Json::parse(nullptr, std::size_t(0), &err3);
    EXPECT_TRUE(static_cast<bool>(err3));
    EXPECT_STREQ(err3.message, "input nulo");

    EXPECT_TRUE(Json::parse("{{{").is_null());
}

TEST(Json, DepthLimitProtectsTheStack)
{
    const std::string ok = nest("[", "]", static_cast<int>(Json::kMaxDepth), "1");
    Json::Error e1;
    Json::parse(ok.c_str(), &e1);
    EXPECT_FALSE(static_cast<bool>(e1));

    const std::string deep = nest("[", "]", static_cast<int>(Json::kMaxDepth) + 1, "1");
    Json::Error e2;
    Json::parse(deep.c_str(), &e2);
    ASSERT_TRUE(static_cast<bool>(e2));
    EXPECT_STREQ(e2.message, "aninhamento demasiado profundo");

    const std::string bomb = nest("[", "]", 100000, "1"); 
    Json::Error e3;
    Json::parse(bomb.c_str(), &e3);
    EXPECT_TRUE(static_cast<bool>(e3));

    const std::string objs =
        nest(R"({"a":)", "}", static_cast<int>(Json::kMaxDepth) + 1, "1");
    Json::Error e4;
    Json::parse(objs.c_str(), &e4);
    EXPECT_TRUE(static_cast<bool>(e4));
}

TEST(Json, Utf8BomIsSkipped)
{
    Json j = parse_ok("\xEF\xBB\xBF{\"a\":1}");
    EXPECT_TRUE(j.is_object());
    EXPECT_EQ(j["a"].as_int(), 1);

    EXPECT_TRUE(static_cast<bool>(parse_err("{\xEF\xBB\xBF\"a\":1}")));
}

TEST(Json, WhitespaceHandling)
{
    Json j = parse_ok(" \t\r\n { \n \"a\" \t : \r 1 , \"b\" : [ 1 , 2 ] \n } \n ");
    EXPECT_EQ(j.size(), 2u);
    EXPECT_EQ(j["b"].size(), 2u);
}

TEST(Json, ObjectKeepsInsertionOrder)
{
    Json j = parse_ok(R"({"z":1,"a":2,"m":3})");
    ASSERT_EQ(j.size(), 3u);
    EXPECT_EQ(j.members()[0].key, String("z"));
    EXPECT_EQ(j.members()[1].key, String("a"));
    EXPECT_EQ(j.members()[2].key, String("m"));
    EXPECT_EQ(j.dump(), String(R"({"z":1,"a":2,"m":3})")); 
}

TEST(Json, ObjectLookupAndMutation)
{
    Json j = Json::object();
    j.set("nome", "ct");
    j.set("versao", 2);
    EXPECT_TRUE(j.contains("nome"));
    EXPECT_FALSE(j.contains("xpto"));
    ASSERT_NE(j.find("nome"), nullptr);
    EXPECT_STREQ(j.find("nome")->as_cstr(), "ct");
    EXPECT_EQ(j.find("xpto"), nullptr);

    j.set("versao", 3); 
    EXPECT_EQ(j.size(), 2u);
    EXPECT_EQ(j["versao"].as_int(), 3);

    EXPECT_TRUE(j.erase("nome"));
    EXPECT_FALSE(j.erase("nome"));
    EXPECT_EQ(j.size(), 1u);

    Json vazio; 
    vazio["a"]["b"] = 5;
    EXPECT_TRUE(vazio.is_object());
    EXPECT_EQ(vazio["a"]["b"].as_int(), 5);
    EXPECT_EQ(vazio.dump(), String(R"({"a":{"b":5}})"));
}

TEST(Json, StringKeysWithEmbeddedNulKeepTheirFullLength)
{
    const String key("a\0b", 3);
    Json j = Json::object();
    j.set(String(key), 1);
    EXPECT_TRUE(j.contains(key));
    ASSERT_NE(j.find(key), nullptr);
    EXPECT_EQ(j[key].as_int(), 1);

    // Atualizar pelo mesmo String nao pode procurar apenas ate ao primeiro NUL.
    j[key] = 2;
    j.set(String(key), 3);
    EXPECT_EQ(j.size(), 1u);
    EXPECT_EQ(j[key].as_int(), 3);

    const Json &constant = j;
    EXPECT_EQ(constant[key].as_int(), 3);
}

TEST(Json, ObjectConstLookupIsForgiving)
{
    const Json j = parse_ok(R"({"janela":{"largura":1280}})");
    EXPECT_EQ(j["janela"]["largura"].as_int(0), 1280);

    EXPECT_EQ(j["janela"]["altura"].as_int(720), 720);
    EXPECT_EQ(j["nada"]["de"]["nada"].as_int(-1), -1);
    EXPECT_TRUE(j["nada"].is_null());
    EXPECT_EQ(j.size(), 1u); 
}

TEST(Json, NonConstOperatorBracketCreatesKeys)
{

    Json j = parse_ok(R"({"a":1})");
    EXPECT_EQ(j.size(), 1u);
    EXPECT_TRUE(j["b"].is_null());
    EXPECT_EQ(j.size(), 2u);
    EXPECT_EQ(j.find("c"), nullptr);
    EXPECT_EQ(j.size(), 2u);
}

TEST(Json, DuplicateKeysKeepBothFindReturnsFirst)
{
    Json j = parse_ok(R"({"a":1,"a":2})");
    EXPECT_EQ(j.size(), 2u);
    EXPECT_EQ(j["a"].as_int(), 1);
    j.set("a", 9); 
    EXPECT_EQ(j.members()[0].value.as_int(), 9);
    EXPECT_EQ(j.members()[1].value.as_int(), 2);
}

TEST(Json, ArrayOperations)
{
    Json j = Json::array();
    EXPECT_TRUE(j.is_array());
    EXPECT_TRUE(j.empty());
    j.push_back(1);
    j.push_back("dois");
    j.push_back(3.5);
    j.push_back(Json());
    ASSERT_EQ(j.size(), 4u);
    EXPECT_EQ(j[0].as_int(), 1);
    EXPECT_STREQ(j[1].as_cstr(), "dois");
    EXPECT_EQ(j[2].as_double(), 3.5);
    EXPECT_TRUE(j[3].is_null());

    j.erase(std::size_t(1));
    EXPECT_EQ(j.size(), 3u);
    EXPECT_EQ(j[1].as_double(), 3.5);
    j.pop_back();
    EXPECT_EQ(j.size(), 2u);

    Json vazio; 
    vazio.push_back(1);
    EXPECT_TRUE(vazio.is_array());
    EXPECT_EQ(vazio.dump(), String("[1]"));

    j.reserve(64);
    for (int i = 0; i < 64; ++i)
        j.push_back(i);
    EXPECT_EQ(j.size(), 66u);
    EXPECT_EQ(j[65].as_int(), 63);
}

TEST(Json, ArrayIterationAndNesting)
{
    Json j = parse_ok(R"([[1,2],[3,[4,[5]]],[]])");
    EXPECT_EQ(j.size(), 3u);
    EXPECT_EQ(j[0][1].as_int(), 2);
    EXPECT_EQ(j[1][1][1][0].as_int(), 5);
    EXPECT_TRUE(j[2].is_array());
    EXPECT_TRUE(j[2].empty());

    long long soma = 0;
    for (const Json &v : j[0].items())
        soma += v.as_int();
    EXPECT_EQ(soma, 3);
}

TEST(Json, DumpCompactAndPretty)
{
    Json j = parse_ok(R"({"a":[1,2],"b":{"c":null},"d":[],"e":{}})");
    EXPECT_EQ(j.dump(), String(R"({"a":[1,2],"b":{"c":null},"d":[],"e":{}})"));
    const char *esperado =
        "{\n"
        "  \"a\": [\n"
        "    1,\n"
        "    2\n"
        "  ],\n"
        "  \"b\": {\n"
        "    \"c\": null\n"
        "  },\n"
        "  \"d\": [],\n"
        "  \"e\": {}\n"
        "}";
    EXPECT_EQ(j.dump(2), String(esperado));

    String buf("prefixo:");
    j["d"].dump_to(buf);
    EXPECT_EQ(buf, String("prefixo:[]"));

    EXPECT_EQ(Json().dump(), String("null"));
    EXPECT_EQ(Json(true).dump(), String("true"));
    EXPECT_EQ(Json(false).dump(), String("false"));
    EXPECT_EQ(Json::array().dump(4), String("[]"));
}

TEST(Json, RoundTripEverything)
{
    const char *docs[] = {
        R"({"vazio":{},"lista":[],"nulo":null,"t":true,"f":false})",
        R"([0,-1,1.5,"a",[["b"]],{"c":{"d":[1,2,3]}}])",
        R"({"unicode":"\u00e9\u20ac\ud834\udd1e","escapes":"\"\\\/\b\f\n\r\t"})",
        R"([1e100,-1e-100,0.5,123456789012345678901234567890])",
        "{}",
        "[]",
        "null",
        "0",
        R"("")"};
    for (const char *doc : docs)
    {
        Json a = parse_ok(doc);
        String texto = a.dump();
        Json b = parse_ok(texto.c_str());
        EXPECT_TRUE(a == b) << doc << " -> " << texto.c_str();
        EXPECT_EQ(b.dump(), texto);           // estavel ao segundo dump
        Json c = parse_ok(a.dump(2).c_str()); // e o pretty da o mesmo valor
        EXPECT_TRUE(a == c) << doc;
    }
}

// ================= copia, move, igualdade =================

TEST(Json, DeepCopyAndMove)
{
    Json a = parse_ok(R"({"x":[1,{"y":"z"}]})");
    Json b = a; // copia profunda
    EXPECT_TRUE(a == b);
    b["x"][1]["y"] = "outro";
    EXPECT_STREQ(a["x"][1]["y"].as_cstr(), "z");
    EXPECT_FALSE(a == b);

    Json c = std::move(b);
    EXPECT_TRUE(b.is_null()); // o movido fica null, nao invalido
    EXPECT_STREQ(c["x"][1]["y"].as_cstr(), "outro");

    Json d;
    d = c; // copy assign sobre null
    EXPECT_TRUE(d == c);
    d = Json(1); // assign sobre objeto liberta o objeto
    EXPECT_TRUE(d.is_int());

    Json e = parse_ok("[1,2]");
    Json *self = &e;
    e = *self;
    EXPECT_EQ(e.size(), 2u);

    Json f = parse_ok(R"("str")");
    Json g = parse_ok("[1]");
    f.swap(g);
    EXPECT_TRUE(f.is_array());
    EXPECT_TRUE(g.is_string());
    swap(f, g); // ADL
    EXPECT_TRUE(f.is_string());
    f.swap(f);
    EXPECT_STREQ(f.as_cstr(), "str");
}

TEST(Json, EqualityAcrossNumberTypes)
{
    EXPECT_TRUE(Json(1) == Json(1u));
    EXPECT_TRUE(Json(1) == Json(1.0));
    EXPECT_TRUE(Json(1u) == Json(1.0));
    EXPECT_FALSE(Json(-1) == Json(static_cast<std::uint64_t>(-1))); // negativo != uint enorme
    EXPECT_FALSE(Json(1) == Json(1.5));
    EXPECT_FALSE(Json(1) == Json(true)); // bool nao e numero
    EXPECT_FALSE(Json(0) == Json());     // null nao e 0
    EXPECT_FALSE(Json("1") == Json(1));

    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(Json(nan) == Json(nan)); // NaN nunca e igual a nada

    // objetos: mesma composicao, ordem diferente -> iguais
    EXPECT_TRUE(parse_ok(R"({"a":1,"b":2})") == parse_ok(R"({"b":2,"a":1})"));
    EXPECT_FALSE(parse_ok(R"({"a":1})") == parse_ok(R"({"a":1,"b":2})"));
    EXPECT_FALSE(parse_ok(R"({"a":1})") == parse_ok(R"({"b":1})"));
    // arrays: a ordem conta
    EXPECT_FALSE(parse_ok("[1,2]") == parse_ok("[2,1]"));
    EXPECT_TRUE(parse_ok("[1,2]") != parse_ok("[2,1]"));
}

TEST(Json, ClearAndReuse)
{
    Json j = parse_ok(R"({"a":[1,2,3]})");
    j.clear();
    EXPECT_TRUE(j.is_null());
    j.push_back(1);
    EXPECT_TRUE(j.is_array());
}

// ================= erros de programa =================

TEST(Json, TypeMisuseIsFatal)
{
    Json num(1);
    const Json arr = parse_ok("[1]");
    EXPECT_DEATH(num.items(), "nao e um array");
    EXPECT_DEATH(num.members(), "nao e um objeto");
    EXPECT_DEATH(num.str(), "nao e uma string");
    EXPECT_DEATH(arr.at(5), "fora dos limites");
    EXPECT_DEATH(num.at(std::size_t(0)), "nao e array");
    EXPECT_DEATH(num["chave"], "nao e objeto");
    EXPECT_DEATH(num.push_back(1), "nao e um array");
    EXPECT_DEATH(Json::array().pop_back(), "array vazio");
    EXPECT_DEATH(num.erase(std::size_t(0)), "nao e um array");
}

// ================= dtoa: guarda permanente =================

TEST(Json, EveryDoubleRoundTrips)
{
    // o dtoa rapido gera digitos por escalamento e verifica; este teste garante que
    // a verificacao nunca deixa passar um valor que nao volte exatamente igual
    std::mt19937_64 rng(20260822);
    int testados = 0;
    for (int i = 0; i < 200000; ++i)
    {
        std::uint64_t bits = rng();
        double v;
        std::memcpy(&v, &bits, sizeof(v));
        if (v != v || v > 1e308 || v < -1e308) // NaN/infinito nao existem em JSON
            continue;
        ++testados;
        const String texto = Json(v).dump();
        Json back = Json::parse(texto.data(), texto.size());
        ASSERT_TRUE(back.is_real()) << texto.c_str();
        ASSERT_EQ(back.as_double(), v) << "round-trip falhou: " << texto.c_str();
    }
    EXPECT_GT(testados, 100000);

    // e os valores tipicos de jogo (float promovido a double)
    std::uniform_real_distribution<float> fd(-10000.0f, 10000.0f);
    for (int i = 0; i < 50000; ++i)
    {
        const double v = static_cast<double>(fd(rng));
        const String texto = Json(v).dump();
        ASSERT_EQ(Json::parse(texto.data(), texto.size()).as_double(), v)
            << "round-trip falhou: " << texto.c_str();
    }
}

// ================= ficheiro real =================

namespace
{
    std::string ler_ficheiro(const char *path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return std::string();
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    // conta nos e confirma que cada double sobrevive ao dump
    void verifica_subarvore(const Json &j, int &nos)
    {
        ++nos;
        if (j.is_real())
        {
            const String t = j.dump();
            ASSERT_EQ(Json::parse(t.data(), t.size()).as_double(), j.as_double())
                << t.c_str();
        }
        else if (j.is_array())
            for (const Json &v : j.items())
                verifica_subarvore(v, nos);
        else if (j.is_object())
            for (const Json::Member &m : j.members())
            {
                EXPECT_FALSE(m.key.empty());
                verifica_subarvore(m.value, nos);
            }
    }
}

TEST(Json, CenaRealDoRadion)
{
    const char *env = std::getenv("CT_SCENE_JSON");
    const char *candidatos[] = {
        env,
        "/media/projectos/projects/cpp/Radion/teste/Scenes/plane.scene.json",
        "/media/projectos/projects/cpp/Radion/teste/Scenes/game.scene.json",
        "/media/projectos/projects/cpp/Radion/teste/Scenes/castel.scene.json"};

    int ficheiros = 0;
    for (const char *path : candidatos)
    {
        if (!path)
            continue;
        const std::string texto = ler_ficheiro(path);
        if (texto.empty())
            continue;
        ++ficheiros;

        Json::Error err;
        Json cena = Json::parse(texto.data(), texto.size(), &err);
        ASSERT_FALSE(static_cast<bool>(err))
            << path << ": " << (err.message ? err.message : "") << " (linha " << err.line
            << ", coluna " << err.column << ")";

        // estrutura que o editor grava
        ASSERT_TRUE(cena.is_object());
        EXPECT_STREQ(cena["format"].as_cstr(), "radion-scene");
        EXPECT_TRUE(cena["renderSettings"].is_object());
        EXPECT_TRUE(cena["scene"]["objects"].is_array());
        EXPECT_GT(cena["scene"]["objects"].size(), 0u);

        // cada objeto da cena tem id, nome e transform com 3 posicoes
        for (const Json &obj : cena["scene"]["objects"].items())
        {
            ASSERT_TRUE(obj.is_object());
            EXPECT_TRUE(obj["id"].is_number());
            EXPECT_TRUE(obj["name"].is_string());
            const Json &pos = obj["transform"]["position"];
            ASSERT_TRUE(pos.is_array()) << obj["name"].as_cstr();
            EXPECT_EQ(pos.size(), 3u);
            for (const Json &c : pos.items())
                EXPECT_TRUE(c.is_number());
        }

        int nos = 0;
        verifica_subarvore(cena, nos);
        EXPECT_GT(nos, 100);

        // round-trip completo: dump -> parse -> igual, e o segundo dump e identico
        const String compacto = cena.dump();
        Json::Error e2;
        Json volta = Json::parse(compacto.data(), compacto.size(), &e2);
        ASSERT_FALSE(static_cast<bool>(e2)) << (e2.message ? e2.message : "");
        EXPECT_TRUE(cena == volta) << path;
        EXPECT_EQ(volta.dump(), compacto) << path;

        // e o mesmo pelo caminho indentado
        const String bonito = cena.dump(4);
        Json volta2 = Json::parse(bonito.data(), bonito.size());
        EXPECT_TRUE(cena == volta2) << path;
        EXPECT_LT(compacto.size(), bonito.size());

        std::printf("[          ] %s: %zu bytes, %d nos, dump %zu bytes\n", path,
                    texto.size(), nos, compacto.size());
    }

    if (ficheiros == 0)
        GTEST_SKIP() << "sem cenas do Radion a mao (usa CT_SCENE_JSON=<ficheiro>)";
}

TEST(Json, ErroDeStreamNaoApontaParaOStream)
{
    Json::Error error;
    {
        ct::FileStream closed;
        Json value = ct::parse_json(closed, &error);
        EXPECT_TRUE(value.is_null());
    }
    ASSERT_TRUE(error);
    EXPECT_GT(std::strlen(error.message), 0u);
    EXPECT_EQ(error.line, 1u);
}

TEST(Json, AsIntEAsUintDevolvemFallbackForaDaGama)
{
    EXPECT_EQ(Json(1e300).as_int(-7), -7);
    EXPECT_EQ(Json(-1e300).as_int(-7), -7);
    EXPECT_EQ(Json(std::numeric_limits<double>::quiet_NaN()).as_int(-7), -7);
    EXPECT_EQ(Json(std::numeric_limits<double>::infinity()).as_uint(9), 9u);
    EXPECT_EQ(Json(-1.5).as_uint(9), 9u);
    EXPECT_EQ(Json(-1).as_uint(9), 9u);
    EXPECT_EQ(Json(std::numeric_limits<std::uint64_t>::max()).as_int(-7), -7);
    EXPECT_EQ(Json(9007199254740992.0).as_int(), 9007199254740992);
    EXPECT_EQ(Json(-9223372036854775808.0).as_int(), std::numeric_limits<std::int64_t>::min());
    EXPECT_EQ(Json(9223372036854775808.0).as_int(-7), -7);
    EXPECT_EQ(Json(18446744073709551616.0).as_uint(9), 9u);
    EXPECT_EQ(parse_ok("18446744073709551616").as_int(-7), -7);
}

TEST(Json, IgualdadeDeObjectosESimetricaEAceitaChavesComNul)
{
    Json a = parse_ok(R"({"a":1,"a":1})");
    Json b = parse_ok(R"({"a":1,"b":1})");
    EXPECT_FALSE(a == b);
    EXPECT_FALSE(b == a);
    Json k1 = parse_ok(R"({"a\u0000b":1})");
    Json k2 = parse_ok(R"({"a\u0000b":1})");
    Json k3 = parse_ok(R"({"a":1})");
    EXPECT_TRUE(k1 == k1);
    EXPECT_TRUE(k1 == k2);
    EXPECT_FALSE(k1 == k3);
    EXPECT_FALSE(k3 == k1);
}

TEST(Json, ColunaDoErroIgnoraBomEContaCodePoints)
{
    Json::Error err;
    Json::parse("\xEF\xBB\xBF{x", &err);
    ASSERT_TRUE(err);
    EXPECT_EQ(err.line, 1u);
    EXPECT_EQ(err.column, 2u);
    Json::Error err2;
    Json::parse("{\"\xC3\xA9\xC3\xA9\":x}", &err2);
    ASSERT_TRUE(err2);
    EXPECT_EQ(err2.column, 7u);
    Json::Error err3;
    Json::parse("{\n \"k\": ]", &err3);
    ASSERT_TRUE(err3);
    EXPECT_EQ(err3.line, 2u);
    EXPECT_EQ(err3.column, 7u);
}

TEST(Json, ObjetosComChavesRepetidasComparamPorMulticonjunto)
{
    Json a = parse_ok(R"({"x":1,"x":1,"y":1})");
    Json b = parse_ok(R"({"x":1,"y":1,"y":1})");
    EXPECT_FALSE(a == b);
    EXPECT_FALSE(b == a);
    Json c = parse_ok(R"({"y":1,"x":1,"x":1})");
    EXPECT_TRUE(a == c);
    Json d = parse_ok(R"({"x":1,"x":2})");
    Json e = parse_ok(R"({"x":2,"x":1})");
    EXPECT_TRUE(d == e);
    EXPECT_FALSE(d == parse_ok(R"({"x":1,"x":1})"));
}
