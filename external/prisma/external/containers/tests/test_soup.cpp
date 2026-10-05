#include <ct/soup.hpp>

#include <gtest/gtest.h>

using ct::Node;
using ct::Soup;
using ct::String;
using ct::StringView;

namespace
{
    // Every selector count in one place: the tests care how many nodes matched
    // far more often than which.
    std::size_t count(const Soup &soup, const char *selector)
    {
        return soup.select(selector).size();
    }
}

// ── Well-formed input ───────────────────────────────────────────────────────

TEST(Soup, ParsesBasicDocument)
{
    Soup soup = Soup::parse(
        "<!DOCTYPE html><html><head><title>Hello</title></head>"
        "<body><p class=\"lead\">World</p></body></html>");

    EXPECT_EQ(soup.title(), "Hello");

    const Node *p = soup.find("p");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getText(), "World");
    EXPECT_STREQ(p->attr("class"), "lead");
    EXPECT_TRUE(p->hasClass("lead"));
}

TEST(Soup, TagAndAttributeNamesAreCaseInsensitive)
{
    Soup soup = Soup::parse("<DIV CLASS=\"Card\">x</DIV>");
    const Node *div = soup.find("div");
    ASSERT_NE(div, nullptr);
    // The name folds, the value does not.
    EXPECT_STREQ(div->attr("class"), "Card");
    EXPECT_STREQ(div->attr("CLASS"), "Card");
}

TEST(Soup, NestingAndText)
{
    Soup soup = Soup::parse("<div><p>a<b>c</b>d</p><p>e</p></div>");

    const Node *div = soup.find("div");
    ASSERT_NE(div, nullptr);
    EXPECT_EQ(div->childElements("p").size(), 2u);
    EXPECT_EQ(div->getText(), "acde");

    const ct::Vector<const Node *> paragraphs = soup.findAll("p");
    ASSERT_EQ(paragraphs.size(), 2u);
    EXPECT_EQ(paragraphs[0]->getText(), "acd");
    EXPECT_EQ(paragraphs[1]->getText(), "e");
}

TEST(Soup, GetTextTrimmedCollapsesWhitespace)
{
    Soup soup = Soup::parse("<p>  a\n\n  b  </p>");
    const Node *p = soup.find("p");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getTextTrimmed(), "a b");
}

TEST(Soup, ClosestWalksUp)
{
    Soup soup = Soup::parse("<div id=outer><section><p><b>x</b></p></section></div>");
    const Node *b = soup.find("b");
    ASSERT_NE(b, nullptr);
    const Node *div = b->closest("div");
    ASSERT_NE(div, nullptr);
    EXPECT_EQ(div->id(), "outer");
    EXPECT_EQ(b->closest("table"), nullptr);
}

TEST(Soup, SiblingTraversal)
{
    Soup soup = Soup::parse("<ul><li>a</li>text<li>b</li></ul>");
    const Node *first = soup.find("li");
    ASSERT_NE(first, nullptr);

    // nextSibling sees the text node between them; nextElementSibling skips it.
    const Node *any = first->nextSibling();
    ASSERT_NE(any, nullptr);
    EXPECT_TRUE(any->isText());

    const Node *element = first->nextElementSibling();
    ASSERT_NE(element, nullptr);
    EXPECT_EQ(element->getText(), "b");
}

// ── Malformed input ─────────────────────────────────────────────────────────

TEST(Soup, VoidElementsDoNotNest)
{
    // A parser that pushes <br> on the open stack puts "b" inside it.
    Soup soup = Soup::parse("<p>a<br>b<img src=\"x.png\">c</p>");
    const Node *p = soup.find("p");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getText(), "abc");
    EXPECT_EQ(p->childElements().size(), 2u);
}

TEST(Soup, SelfClosedVoidElement)
{
    Soup soup = Soup::parse("<p>a<br/>b</p>");
    const Node *p = soup.find("p");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getText(), "ab");
}

TEST(Soup, StrayEndTagForVoidElementIsIgnored)
{
    Soup soup = Soup::parse("<div><br></br>x</div>");
    const Node *div = soup.find("div");
    ASSERT_NE(div, nullptr);
    EXPECT_EQ(div->getText(), "x");
}

TEST(Soup, UnclosedListItemsBecomeSiblings)
{
    Soup soup = Soup::parse("<ul><li>a<li>b<li>c</ul>");
    const Node *ul = soup.find("ul");
    ASSERT_NE(ul, nullptr);

    const ct::Vector<const Node *> items = ul->childElements("li");
    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0]->getText(), "a");
    EXPECT_EQ(items[1]->getText(), "b");
    EXPECT_EQ(items[2]->getText(), "c");
}

TEST(Soup, ParagraphClosedByBlockElement)
{
    Soup soup = Soup::parse("<p>one<p>two<div>three</div>");
    const ct::Vector<const Node *> ps = soup.findAll("p");
    ASSERT_EQ(ps.size(), 2u);
    // The div is a sibling of the paragraphs, not a child of the second.
    EXPECT_EQ(ps[1]->getText(), "two");
}

TEST(Soup, TableCellsCloseEachOther)
{
    Soup soup = Soup::parse("<table><tr><td>a<td>b<tr><td>c</table>");
    EXPECT_EQ(soup.findAll("tr").size(), 2u);
    EXPECT_EQ(soup.findAll("td").size(), 3u);
}

TEST(Soup, ImplicitCloseUnwindsInlineElements)
{
    // The <a> in the second item never closes. A browser unwinds it along with
    // the <li>, so the third item is a sibling and not a grandchild.
    Soup soup = Soup::parse("<ul><li><a href=1>One</a><li><a href=2>Two"
                            "<li class=off><a href=3>Three</a></ul>");
    const Node *ul = soup.find("ul");
    ASSERT_NE(ul, nullptr);
    EXPECT_EQ(ul->childElements("li").size(), 3u);
    EXPECT_EQ(count(soup, "li a[href]"), 3u);
}

TEST(Soup, ImplicitCloseStopsAtABlock)
{
    // An unclosed <li> in one list must not swallow the section after it.
    Soup soup = Soup::parse("<ul><li>a</ul><div><p>after</p></div>");
    const Node *p = soup.find("p");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->closest("li"), nullptr);
    EXPECT_NE(p->closest("div"), nullptr);
}

TEST(Soup, UnclosedElementKeepsItsChildren)
{
    Soup soup = Soup::parse("<div><span>a</span>");
    const Node *div = soup.find("div");
    ASSERT_NE(div, nullptr);
    EXPECT_EQ(div->getText(), "a");
}

TEST(Soup, OverlappingTagsKeepTheirText)
{
    Soup soup = Soup::parse("<p><b><i>x</b></i>y</p>");
    const Node *p = soup.find("p");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getText(), "xy");
}

TEST(Soup, StrayEndTagsAreIgnored)
{
    Soup soup = Soup::parse("</div><p>a</p>");
    EXPECT_EQ(soup.getText(), "a");

    Soup mess = Soup::parse("<a><b><c></a></b></c><d>x");
    const Node *d = mess.find("d");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->getText(), "x");
}

// ── Raw text ────────────────────────────────────────────────────────────────

TEST(Soup, ScriptContentsAreNotMarkup)
{
    // The "</div>" lives in a JavaScript string: reading it as a tag tears the
    // tree apart. This is the case naive parsers fail most often.
    Soup soup = Soup::parse(
        "<div><script>if (a < b && c > d) { s = \"</div>\"; }</script><p>after</p></div>");

    const Node *p = soup.find("p");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getText(), "after");
    EXPECT_NE(p->closest("div"), nullptr);
}

TEST(Soup, GetTextIgnoresScriptBodies)
{
    Soup soup = Soup::parse("<div><script>var a = 1;</script>text</div>");
    const Node *div = soup.find("div");
    ASSERT_NE(div, nullptr);
    EXPECT_EQ(div->getText(), "text");
}

TEST(Soup, StyleContentsAreNotMarkup)
{
    Soup soup = Soup::parse("<style>a > b { content: \"<x>\"; }</style><p>ok</p>");
    const Node *p = soup.find("p");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getText(), "ok");
}

TEST(Soup, UnterminatedScriptIsCaptured)
{
    Soup soup = Soup::parse("<div><script>var a = 1;");
    EXPECT_NE(soup.find("script"), nullptr);
}

TEST(Soup, TitleDecodesEntitiesButScriptDoesNot)
{
    Soup soup = Soup::parse("<title>a &amp; b</title><script>x &amp; y</script>");
    EXPECT_EQ(soup.title(), "a & b");
    const Node *script = soup.find("script");
    ASSERT_NE(script, nullptr);
    EXPECT_EQ(script->children[0]->text, "x &amp; y");
}

// ── Attributes ──────────────────────────────────────────────────────────────

TEST(Soup, AttributeQuotingVariants)
{
    Soup soup = Soup::parse("<input type=text name='user' value=\"a b\" disabled data-x=1>");
    const Node *input = soup.find("input");
    ASSERT_NE(input, nullptr);

    EXPECT_STREQ(input->attr("type"), "text");
    EXPECT_STREQ(input->attr("name"), "user");
    EXPECT_STREQ(input->attr("value"), "a b");
    EXPECT_STREQ(input->attr("data-x"), "1");

    // Present with no value differs from absent.
    EXPECT_TRUE(input->hasAttr("disabled"));
    EXPECT_STREQ(input->attr("disabled"), "");
    EXPECT_FALSE(input->hasAttr("nope"));
    EXPECT_EQ(input->attribute("nope"), nullptr);
}

TEST(Soup, DuplicateAttributeKeepsTheFirst)
{
    Soup soup = Soup::parse("<a href=\"first\" href=\"second\">x</a>");
    const Node *a = soup.find("a");
    ASSERT_NE(a, nullptr);
    EXPECT_STREQ(a->attr("href"), "first");
}

TEST(Soup, UnterminatedQuoteDoesNotHang)
{
    Soup soup = Soup::parse("<a href=\"never-closed>text");
    EXPECT_NE(soup.find("a"), nullptr);
}

TEST(Soup, JunkBetweenAttributesIsSkipped)
{
    Soup soup = Soup::parse("<div = == class=\"c\">x</div>");
    const Node *div = soup.find("div");
    ASSERT_NE(div, nullptr);
    EXPECT_STREQ(div->attr("class"), "c");
}

TEST(Soup, HasClassMatchesWholeWords)
{
    Soup soup = Soup::parse("<div class=\"card featured wide\">x</div>");
    const Node *div = soup.find("div");
    ASSERT_NE(div, nullptr);
    EXPECT_TRUE(div->hasClass("card"));
    EXPECT_TRUE(div->hasClass("featured"));
    EXPECT_TRUE(div->hasClass("wide"));
    // A substring of a class is not a class.
    EXPECT_FALSE(div->hasClass("car"));
    EXPECT_FALSE(div->hasClass("feature"));
    EXPECT_FALSE(div->hasClass(""));
}

// ── Entities ────────────────────────────────────────────────────────────────

TEST(Soup, DecodesEntities)
{
    EXPECT_EQ(ct::decodeEntities("a &amp; b"), "a & b");
    EXPECT_EQ(ct::decodeEntities("&lt;tag&gt;"), "<tag>");
    EXPECT_EQ(ct::decodeEntities("&#65;&#66;"), "AB");
    EXPECT_EQ(ct::decodeEntities("&#x41;&#X42;"), "AB");
    EXPECT_EQ(ct::decodeEntities("caf&eacute;"), "caf\xc3\xa9");
    EXPECT_EQ(ct::decodeEntities("&#8364;"), "\xe2\x82\xac");
    EXPECT_EQ(ct::decodeEntities("&nbsp;"), "\xc2\xa0");
}

TEST(Soup, LeavesNonEntitiesAlone)
{
    // A bare "&" in prose is far more common than a broken entity.
    EXPECT_EQ(ct::decodeEntities("a & b"), "a & b");
    EXPECT_EQ(ct::decodeEntities("&nosuch;"), "&nosuch;");
    EXPECT_EQ(ct::decodeEntities("&amp"), "&amp");
    EXPECT_EQ(ct::decodeEntities("&"), "&");
    EXPECT_EQ(ct::decodeEntities("&#;"), "&#;");
    EXPECT_EQ(ct::decodeEntities("&#x;"), "&#x;");
    EXPECT_EQ(ct::decodeEntities(""), "");
}

TEST(Soup, DecodesEntitiesInTextAndAttributes)
{
    Soup soup = Soup::parse("<a title=\"a &amp; b\">x &lt; y</a>");
    const Node *a = soup.find("a");
    ASSERT_NE(a, nullptr);
    EXPECT_STREQ(a->attr("title"), "a & b");
    EXPECT_EQ(a->getText(), "x < y");
}

// ── Comments, doctype, bare angle brackets ──────────────────────────────────

TEST(Soup, CommentsAreNotParsedAsMarkup)
{
    Soup soup = Soup::parse("<div><!-- <p>not real</p> -->text</div>");
    EXPECT_EQ(soup.find("p"), nullptr);
    const Node *div = soup.find("div");
    ASSERT_NE(div, nullptr);
    EXPECT_EQ(div->getText(), "text");
}

TEST(Soup, UnterminatedCommentDoesNotHang)
{
    Soup soup = Soup::parse("<div>a<!-- never closed");
    EXPECT_NE(soup.find("div"), nullptr);
}

TEST(Soup, BareAngleBracketsAreText)
{
    Soup soup = Soup::parse("<p>1 < 2 and 3 > 2</p>");
    const Node *p = soup.find("p");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getText(), "1 < 2 and 3 > 2");
}

TEST(Soup, CdataBecomesText)
{
    Soup soup = Soup::parse("<div><![CDATA[a <b> c]]></div>");
    const Node *div = soup.find("div");
    ASSERT_NE(div, nullptr);
    EXPECT_EQ(div->getText(), "a <b> c");
    EXPECT_EQ(div->find("b"), nullptr);
}

// ── Degenerate input ────────────────────────────────────────────────────────

TEST(Soup, EmptyAndTextOnlyDocuments)
{
    Soup empty = Soup::parse("");
    EXPECT_EQ(empty.getText(), "");
    EXPECT_EQ(empty.findAll("div").size(), 0u);

    Soup text = Soup::parse("just text");
    EXPECT_EQ(text.getText(), "just text");

    Soup nullish = Soup::parse(static_cast<const char *>(nullptr));
    EXPECT_EQ(nullish.findAll("div").size(), 0u);
}

TEST(Soup, BrokenMarkupDoesNotCrash)
{
    EXPECT_EQ(Soup::parse("<").getText(), "<");
    EXPECT_NO_FATAL_FAILURE(Soup::parse("<<<>>>"));
    EXPECT_NO_FATAL_FAILURE(Soup::parse("<a<b<c"));
    EXPECT_NO_FATAL_FAILURE(Soup::parse("</>"));
    EXPECT_NO_FATAL_FAILURE(Soup::parse("<!"));
    EXPECT_NO_FATAL_FAILURE(Soup::parse("<!--"));
    EXPECT_NO_FATAL_FAILURE(Soup::parse("<div attr"));
}

TEST(Soup, DeepNesting)
{
    // Well past ct::Xml's 200-deep limit, and more than any real page.
    String deep;
    for (int i = 0; i < 500; ++i)
        deep.append("<div>");
    deep.append("x");

    Soup soup = Soup::parse(deep);
    EXPECT_EQ(soup.getText(), "x");
}

// ── Selectors ───────────────────────────────────────────────────────────────

namespace
{
    const char *kSelectorPage =
        "<body>"
        "  <div class=\"card featured\" id=\"one\">"
        "    <h2>First</h2><a href=\"a.mp4\">A</a>"
        "  </div>"
        "  <div class=\"card\" id=\"two\">"
        "    <h2>Second</h2><a href=\"b.m3u8\" class=\"stream\">B</a>"
        "  </div>"
        "  <footer><a href=\"c.m3u8\">C</a></footer>"
        "</body>";
}

TEST(SoupSelect, SimpleSelectors)
{
    Soup soup = Soup::parse(kSelectorPage);
    EXPECT_EQ(count(soup, "div"), 2u);
    EXPECT_EQ(count(soup, ".card"), 2u);
    EXPECT_EQ(count(soup, ".featured"), 1u);
    EXPECT_EQ(count(soup, "#two"), 1u);
    EXPECT_EQ(count(soup, "div.card.featured"), 1u);
    EXPECT_EQ(count(soup, "a"), 3u);
    EXPECT_EQ(count(soup, "*"), 9u);
    EXPECT_EQ(count(soup, "video"), 0u);
}

TEST(SoupSelect, AttributeOperators)
{
    Soup soup = Soup::parse(kSelectorPage);
    EXPECT_EQ(count(soup, "a[href]"), 3u);
    EXPECT_EQ(count(soup, "a[href=b.m3u8]"), 1u);
    EXPECT_EQ(count(soup, "a[href^=a]"), 1u);
    EXPECT_EQ(count(soup, "a[href$=.m3u8]"), 2u);
    EXPECT_EQ(count(soup, "a[href*=m3u]"), 2u);
    EXPECT_EQ(count(soup, "div[class~=card]"), 2u);
    EXPECT_EQ(count(soup, "a[href$=\".m3u8\"]"), 2u);
    EXPECT_EQ(count(soup, "a[href$='.m3u8']"), 2u);
}

TEST(SoupSelect, Combinators)
{
    Soup soup = Soup::parse(kSelectorPage);
    EXPECT_EQ(count(soup, "div a"), 2u);
    EXPECT_EQ(count(soup, "div > a"), 2u);
    EXPECT_EQ(count(soup, "body a"), 3u);
    // A grandchild is not a child.
    EXPECT_EQ(count(soup, "body > a"), 0u);
    EXPECT_EQ(count(soup, "div.card#two > a.stream"), 1u);
}

TEST(SoupSelect, DescendantCombinatorBacktracks)
{
    // The nearest <b> has no <a> above it; the outer one does. A right-to-left
    // match that does not backtrack misses this.
    Soup soup = Soup::parse("<div class=a><div class=b><p>x</p></div></div>"
                            "<div class=b><p>y</p></div>");
    const ct::Vector<const Node *> got = soup.select(".a .b p");
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0]->getText(), "x");
}

TEST(SoupSelect, UnionAndDocumentOrder)
{
    Soup soup = Soup::parse(kSelectorPage);
    EXPECT_EQ(count(soup, "h2, footer"), 3u);
    // A node matching both halves appears once.
    EXPECT_EQ(count(soup, "a, a[href]"), 3u);

    const ct::Vector<const Node *> anchors = soup.select("a");
    ASSERT_EQ(anchors.size(), 3u);
    EXPECT_EQ(anchors[0]->getText(), "A");
    EXPECT_EQ(anchors[1]->getText(), "B");
    EXPECT_EQ(anchors[2]->getText(), "C");
}

TEST(SoupSelect, SelectOne)
{
    Soup soup = Soup::parse(kSelectorPage);
    const Node *first = soup.selectOne("a[href$=.m3u8]");
    ASSERT_NE(first, nullptr);
    EXPECT_STREQ(first->attr("href"), "b.m3u8");
    EXPECT_EQ(soup.selectOne("video"), nullptr);
}

TEST(SoupSelect, PseudoClasses)
{
    Soup soup = Soup::parse("<ul><li>a</li><li>b</li><li class=\"last\">c</li></ul>"
                            "<div><span>only</span></div><p></p>");

    EXPECT_EQ(count(soup, "li:first-child"), 1u);
    EXPECT_EQ(count(soup, "li:last-child"), 1u);
    EXPECT_EQ(count(soup, "span:only-child"), 1u);
    EXPECT_EQ(count(soup, "p:empty"), 1u);
    EXPECT_EQ(count(soup, "li:not(.last)"), 2u);

    const Node *last = soup.selectOne("li:last-child");
    ASSERT_NE(last, nullptr);
    EXPECT_EQ(last->getText(), "c");
}

TEST(SoupSelect, EmptyCountsWhitespaceAsText)
{
    Soup soup = Soup::parse("<p></p><p> </p><p><b></b></p>");
    // Only the first is empty: the second holds whitespace, the third an element.
    EXPECT_EQ(count(soup, "p:empty"), 1u);
}

TEST(SoupSelect, MalformedSelectorsMatchNothing)
{
    // Selectors arrive from config files and text boxes, so a typo must give no
    // results rather than a crash or a wrong answer.
    Soup soup = Soup::parse("<div><a href=\"x\">a</a></div>");

    static const char *kBad[] = {
        "",        "  ",      ".",         "#",          "[",
        "]",       "[]",      "[=]",       "[href",      "[href=]",
        ">",       "div >",   "> div",     "div,",       ",div",
        "div , , a", ":",     ":nope",     ":not(",      ":not()",
        "div:not(", "a[href$]", "a[href!=x]", "((",      "@media",
        "div..class", "#a#b#c#",
    };
    for (std::size_t i = 0; i < sizeof(kBad) / sizeof(kBad[0]); ++i)
    {
        EXPECT_EQ(count(soup, kBad[i]), 0u) << "selector: \"" << kBad[i] << "\"";
    }

    // A valid selector still works afterwards.
    EXPECT_EQ(count(soup, "a[href=x]"), 1u);
}

// ── Extracting media, which is what this is for ─────────────────────────────

TEST(SoupSelect, FindsStreamUrls)
{
    Soup soup = Soup::parse(
        "<html><body>"
        "<video id=\"player\" poster=\"thumb.jpg\">"
        "  <source src=\"https://example.com/stream/master.m3u8\" type=\"application/x-mpegURL\">"
        "  <source src=\"https://example.com/stream/720p.mp4\" type=\"video/mp4\">"
        "</video>"
        "<div class=\"cam\" data-hls=\"https://example.com/beach/live.m3u8\">Beach</div>"
        "<a href=\"https://example.com/other/live.m3u8\">Another cam</a>"
        "<iframe src=\"https://player.example.com/embed/42\"></iframe>"
        "</body></html>");

    EXPECT_EQ(count(soup, "video source[src]"), 2u);

    const Node *hls = soup.selectOne("video source[src$=.m3u8]");
    ASSERT_NE(hls, nullptr);
    EXPECT_STREQ(hls->attr("src"), "https://example.com/stream/master.m3u8");
    EXPECT_STREQ(hls->attr("type"), "application/x-mpegURL");

    const Node *cam = soup.selectOne("[data-hls]");
    ASSERT_NE(cam, nullptr);
    EXPECT_STREQ(cam->attr("data-hls"), "https://example.com/beach/live.m3u8");
    EXPECT_EQ(cam->getTextTrimmed(), "Beach");

    // One selector, three attribute names: every playlist on the page.
    EXPECT_EQ(count(soup, "[src$=.m3u8], [href$=.m3u8], [data-hls]"), 3u);

    const Node *video = soup.selectOne("video#player");
    ASSERT_NE(video, nullptr);
    EXPECT_STREQ(video->attr("poster"), "thumb.jpg");

    const Node *iframe = soup.selectOne("iframe[src]");
    ASSERT_NE(iframe, nullptr);
    EXPECT_STREQ(iframe->attr("src"), "https://player.example.com/embed/42");
}

TEST(SoupSelect, HandlesARealisticallyBrokenPage)
{
    // Unclosed tags, uppercase, unquoted attributes, a script full of markup, a
    // decoy in a comment, entities and a stray end tag - all at once, because
    // that is what a generated page looks like.
    Soup soup = Soup::parse(
        "<!doctype HTML>\n"
        "<HTML>\n"
        "<HEAD>\n"
        "  <META charset=utf-8>\n"
        "  <TITLE>Praia &amp; Mar &mdash; ao vivo</TITLE>\n"
        "  <script>var cfg = {\"url\":\"https://cdn.example.com/a/live.m3u8\"};\n"
        "    if (w < 700) { document.write('<div class=\"mobile\">'); }</script>\n"
        "</HEAD>\n"
        "<BODY class=page>\n"
        "  <ul class=cams>\n"
        "    <li><a href=/cam/1 data-hls=https://cdn.example.com/1/live.m3u8>Cam&nbsp;1</a>\n"
        "    <li><a href=/cam/2 data-hls=https://cdn.example.com/2/live.m3u8>Cam 2\n"
        "    <li class=offline><a href=/cam/3>Cam 3</a>\n"
        "  </ul>\n"
        "  <!-- <ul class=cams><li>a decoy in a comment</ul> -->\n"
        "  </span>\n"
        "  <p>1 < 2\n"
        "</BODY>\n");

    EXPECT_EQ(soup.title(), "Praia & Mar \xe2\x80\x94 ao vivo");

    // The decoy inside the comment is not a list.
    EXPECT_EQ(count(soup, "ul.cams"), 1u);
    EXPECT_EQ(count(soup, "ul.cams > li"), 3u);

    const ct::Vector<const Node *> streams = soup.select("li a[data-hls]");
    ASSERT_EQ(streams.size(), 2u);
    EXPECT_STREQ(streams[0]->attr("data-hls"), "https://cdn.example.com/1/live.m3u8");
    EXPECT_EQ(streams[0]->getTextTrimmed(), "Cam\xc2\xa0" "1");
    // Cam 2's anchor never closed, and still keeps its own text.
    EXPECT_EQ(streams[1]->getTextTrimmed(), "Cam 2");

    EXPECT_EQ(count(soup, "li:not(.offline) a[href]"), 2u);

    // document.write's markup was inside a script: it created no elements.
    EXPECT_EQ(count(soup, ".mobile"), 0u);

    const Node *p = soup.selectOne("p");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getTextTrimmed(), "1 < 2");
}

// ── Serialising ─────────────────────────────────────────────────────────────

TEST(Soup, HtmlRoundTripsStructure)
{
    Soup soup = Soup::parse("<div class=\"a\"><p>x</p><br></div>");
    const Node *div = soup.find("div");
    ASSERT_NE(div, nullptr);
    EXPECT_EQ(div->html(), "<div class=\"a\"><p>x</p><br></div>");
}

TEST(Soup, OpenTagDescribesNodes)
{
    Soup soup = Soup::parse("<a href=\"x\" download>t</a>");
    const Node *a = soup.find("a");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->openTag(), "<a href=\"x\" download>");
    ASSERT_EQ(a->children.size(), 1u);
    EXPECT_EQ(a->children[0]->openTag(), "#text");
}

TEST(Soup, MoveAssignment)
{
    Soup a = Soup::parse("<p>first</p>");
    Soup b = Soup::parse("<p>second</p>");
    a = static_cast<Soup &&>(b);
    EXPECT_EQ(a.getText(), "second");
}
