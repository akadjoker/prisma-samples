#pragma once
// A tolerant HTML parser with CSS selectors - BeautifulSoup's shape, in C++.
// Never fails: what it cannot parse becomes text. ct::Xml is strict, so it
// rejects the unclosed tags and bare "<" that real pages are full of.
//
//     Soup soup = Soup::parse(html);
//     for (const Node* a : soup.select("a[href$=.m3u8]"))
//         puts(a->attr("href", ""));

#include <cstddef>

#include "span.hpp"
#include "string.hpp"
#include "vector.hpp"

namespace ct
{

enum class NodeType : unsigned char
{
    Document,
    Element,
    Text,
    Comment,
    Doctype,
};

struct Attribute
{
    String name;
    String value;

    Attribute() {}
    Attribute(String n, String v);
};

class Node
{
public:
    NodeType type = NodeType::Element;
    String tag;
    String text;
    Vector<Attribute> attributes;
    Vector<Node*> children;
    Node* parent = nullptr;

    ~Node();

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Node() {}

    bool isElement() const { return type == NodeType::Element; }
    bool isText() const { return type == NodeType::Text; }

    const String* attribute(StringView name) const;
    const char* attr(StringView name, const char* fallback = "") const;
    bool hasAttr(StringView name) const;
    bool hasClass(StringView name) const;
    String id() const;

    String getText() const;
    String getTextTrimmed() const;

    Vector<const Node*> childElements(StringView tag = StringView()) const;
    const Node* firstChild(StringView tag) const;
    Vector<const Node*> findAll(StringView tag) const;
    const Node* find(StringView tag) const;
    const Node* closest(StringView tag) const;

    const Node* nextSibling() const;
    const Node* nextElementSibling() const;

    Vector<const Node*> select(StringView selector) const;
    const Node* selectOne(StringView selector) const;

    String html() const;

    String openTag() const;

private:
    void collectText(String& out) const;
    void collectAll(StringView tag, Vector<const Node*>& out) const;
    void htmlInto(String& out) const;
};

class Soup
{
public:
    Soup() {}
    ~Soup();

    Soup(const Soup&) = delete;
    Soup& operator=(const Soup&) = delete;
    Soup(Soup&& other);
    Soup& operator=(Soup&& other);

    static Soup parse(StringView html);
    static Soup parse(const char* html);

    const Node& root() const { return *root_; }

    Vector<const Node*> select(StringView selector) const { return root_->select(selector); }
    const Node* selectOne(StringView selector) const { return root_->selectOne(selector); }
    Vector<const Node*> findAll(StringView tag) const { return root_->findAll(tag); }
    const Node* find(StringView tag) const { return root_->find(tag); }
    String getText() const { return root_->getText(); }
    String getTextTrimmed() const { return root_->getTextTrimmed(); }

    String title() const;

private:
    Node* root_ = nullptr;
};

String decodeEntities(StringView text);

namespace
{

inline bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

inline bool isAlpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

inline String toLower(StringView text)
{
    String out;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        out.push_back(lower(text[i]));
    }
    return out;
}

inline bool equalsIgnoreCase(StringView a, StringView b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (lower(a[i]) != lower(b[i]))
        {
            return false;
        }
    }
    return true;
}

// Never children, never an end tag: they must not go on the open stack, or
inline bool isVoid(StringView tag)
{
    static const char* kVoid[] = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr",
    };
    for (std::size_t i = 0; i < sizeof(kVoid) / sizeof(kVoid[0]); ++i)
    {
        if (tag == StringView(kVoid[i]))
        {
            return true;
        }
    }
    return false;
}

// Everything to the end tag is character data, even "<" and "&" - a script
inline bool isRawText(StringView tag)
{
    return tag == StringView("script") || tag == StringView("style") ||
           tag == StringView("textarea") || tag == StringView("title");
}

// Does a start tag <next> implicitly close an open <current>? Makes <li>a<li>b
inline bool closedBy(StringView current, StringView next)
{
    if (current == StringView("p"))
    {
        static const char* kBlocks[] = {
            "address", "article", "aside", "blockquote", "details", "div", "dl",
            "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2",
            "h3", "h4", "h5", "h6", "header", "hr", "main", "nav", "ol", "p",
            "pre", "section", "table", "ul",
        };
        for (std::size_t i = 0; i < sizeof(kBlocks) / sizeof(kBlocks[0]); ++i)
        {
            if (next == StringView(kBlocks[i]))
            {
                return true;
            }
        }
        return false;
    }
    if (current == StringView("li"))
    {
        return next == StringView("li");
    }
    if (current == StringView("dt") || current == StringView("dd"))
    {
        return next == StringView("dt") || next == StringView("dd");
    }
    if (current == StringView("td") || current == StringView("th"))
    {
        return next == StringView("td") || next == StringView("th") ||
               next == StringView("tr");
    }
    if (current == StringView("tr"))
    {
        return next == StringView("tr");
    }
    if (current == StringView("option"))
    {
        return next == StringView("option") || next == StringView("optgroup");
    }
    if (current == StringView("thead") || current == StringView("tbody"))
    {
        return next == StringView("tbody") || next == StringView("tfoot");
    }
    return false;
}

}

namespace
{

struct NamedEntity
{
    const char* name;
    const char* utf8;
};

// What real pages use; a miss is left as literal text.
const NamedEntity kEntities[] = {
    { "amp", "&" },     { "lt", "<" },      { "gt", ">" },
    { "quot", "\"" },   { "apos", "'" },    { "nbsp", "\xc2\xa0" },
    { "copy", "\xc2\xa9" },  { "reg", "\xc2\xae" },
    { "trade", "\xe2\x84\xa2" },
    { "hellip", "\xe2\x80\xa6" },
    { "mdash", "\xe2\x80\x94" },  { "ndash", "\xe2\x80\x93" },
    { "lsquo", "\xe2\x80\x98" },  { "rsquo", "\xe2\x80\x99" },
    { "ldquo", "\xe2\x80\x9c" },  { "rdquo", "\xe2\x80\x9d" },
    { "bull", "\xe2\x80\xa2" },   { "middot", "\xc2\xb7" },
    { "deg", "\xc2\xb0" },        { "plusmn", "\xc2\xb1" },
    { "times", "\xc3\x97" },      { "divide", "\xc3\xb7" },
    { "euro", "\xe2\x82\xac" },   { "pound", "\xc2\xa3" },
    { "yen", "\xc2\xa5" },        { "cent", "\xc2\xa2" },
    { "sect", "\xc2\xa7" },       { "para", "\xc2\xb6" },
    { "laquo", "\xc2\xab" },      { "raquo", "\xc2\xbb" },
    { "aacute", "\xc3\xa1" },     { "agrave", "\xc3\xa0" },
    { "acirc", "\xc3\xa2" },      { "atilde", "\xc3\xa3" },
    { "ccedil", "\xc3\xa7" },     { "eacute", "\xc3\xa9" },
    { "ecirc", "\xc3\xaa" },      { "iacute", "\xc3\xad" },
    { "oacute", "\xc3\xb3" },     { "otilde", "\xc3\xb5" },
    { "ocirc", "\xc3\xb4" },      { "uacute", "\xc3\xba" },
    { "ntilde", "\xc3\xb1" },     { "uuml", "\xc3\xbc" },
};

void appendUtf8(String& out, unsigned cp)
{
    if (cp == 0 || cp > 0x10FFFFu)
    {
        return;
    }
    if (cp < 0x80u)
    {
        out.push_back((char)cp);
    }
    else if (cp < 0x800u)
    {
        out.push_back((char)(0xC0u | (cp >> 6)));
        out.push_back((char)(0x80u | (cp & 0x3Fu)));
    }
    else if (cp < 0x10000u)
    {
        out.push_back((char)(0xE0u | (cp >> 12)));
        out.push_back((char)(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back((char)(0x80u | (cp & 0x3Fu)));
    }
    else
    {
        out.push_back((char)(0xF0u | (cp >> 18)));
        out.push_back((char)(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back((char)(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back((char)(0x80u | (cp & 0x3Fu)));
    }
}

}

inline String decodeEntities(StringView text)
{
    String out;
    std::size_t i = 0;
    while (i < text.size())
    {
        if (text[i] != '&')
        {
            out.push_back(text[i++]);
            continue;
        }

        std::size_t end = i + 1;
        const std::size_t limit = i + 12 < text.size() ? i + 12 : text.size();
        while (end < limit && text[end] != ';' && !isSpace(text[end]) && text[end] != '&')
        {
            ++end;
        }
        if (end >= text.size() || text[end] != ';' || end == i + 1)
        {
            out.push_back(text[i++]);
            continue;
        }

        StringView body = text.substr(i + 1, end - i - 1);

        if (body[0] == '#')
        {
            unsigned cp = 0;
            bool ok = body.size() > 1;
            if (ok && (body[1] == 'x' || body[1] == 'X'))
            {
                ok = body.size() > 2;
                for (std::size_t k = 2; ok && k < body.size(); ++k)
                {
                    const char c = lower(body[k]);
                    if (isDigit(c))
                    {
                        cp = cp * 16u + (unsigned)(c - '0');
                    }
                    else if (c >= 'a' && c <= 'f')
                    {
                        cp = cp * 16u + (unsigned)(c - 'a' + 10);
                    }
                    else
                    {
                        ok = false;
                    }
                }
            }
            else
            {
                for (std::size_t k = 1; ok && k < body.size(); ++k)
                {
                    if (!isDigit(body[k]))
                    {
                        ok = false;
                    }
                    else
                    {
                        cp = cp * 10u + (unsigned)(body[k] - '0');
                    }
                }
            }
            if (ok && cp != 0)
            {
                appendUtf8(out, cp);
                i = end + 1;
                continue;
            }
            out.push_back(text[i++]);
            continue;
        }

        bool found = false;
        for (std::size_t k = 0; k < sizeof(kEntities) / sizeof(kEntities[0]); ++k)
        {
            if (body == StringView(kEntities[k].name))
            {
                out.append(kEntities[k].utf8);
                found = true;
                break;
            }
        }
        if (found)
        {
            i = end + 1;
        }
        else
        {
            out.push_back(text[i++]);
        }
    }
    return out;
}

inline Attribute::Attribute(String n, String v)
    : name(n), value(v)
{
}

inline Node::~Node()
{
    for (std::size_t i = 0; i < children.size(); ++i)
    {
        delete children[i];
    }
}

inline const String* Node::attribute(StringView name) const
{
    for (std::size_t i = 0; i < attributes.size(); ++i)
    {
        if (equalsIgnoreCase(StringView(attributes[i].name), name))
        {
            return &attributes[i].value;
        }
    }
    return nullptr;
}

inline const char* Node::attr(StringView name, const char* fallback) const
{
    const String* value = attribute(name);
    return value ? value->c_str() : fallback;
}

inline bool Node::hasAttr(StringView name) const
{
    return attribute(name) != nullptr;
}

inline bool Node::hasClass(StringView name) const
{
    const String* classes = attribute(StringView("class"));
    if (!classes || name.empty())
    {
        return false;
    }
    StringView all(*classes);
    std::size_t i = 0;
    while (i < all.size())
    {
        while (i < all.size() && isSpace(all[i]))
        {
            ++i;
        }
        const std::size_t start = i;
        while (i < all.size() && !isSpace(all[i]))
        {
            ++i;
        }
        if (i > start && all.substr(start, i - start) == name)
        {
            return true;
        }
    }
    return false;
}

inline String Node::id() const
{
    const String* value = attribute(StringView("id"));
    return value ? *value : String();
}

inline void Node::collectText(String& out) const
{
    if (type == NodeType::Text)
    {
        out.append(text);
        return;
    }
    if (type == NodeType::Element &&
        (StringView(tag) == StringView("script") || StringView(tag) == StringView("style")))
    {
        return;
    }
    for (std::size_t i = 0; i < children.size(); ++i)
    {
        children[i]->collectText(out);
    }
}

inline String Node::getText() const
{
    String out;
    collectText(out);
    return out;
}

inline String Node::getTextTrimmed() const
{
    const String raw = getText();
    String out;
    bool pendingSpace = false;
    for (std::size_t i = 0; i < raw.size(); ++i)
    {
        const char c = raw[i];
        if (isSpace(c))
        {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace)
        {
            out.push_back(' ');
            pendingSpace = false;
        }
        out.push_back(c);
    }
    return out;
}

inline Vector<const Node*> Node::childElements(StringView tagName) const
{
    Vector<const Node*> out;
    for (std::size_t i = 0; i < children.size(); ++i)
    {
        const Node* child = children[i];
        if (child->isElement() && (tagName.empty() || StringView(child->tag) == tagName))
        {
            out.push_back(child);
        }
    }
    return out;
}

inline const Node* Node::firstChild(StringView tagName) const
{
    for (std::size_t i = 0; i < children.size(); ++i)
    {
        const Node* child = children[i];
        if (child->isElement() && StringView(child->tag) == tagName)
        {
            return child;
        }
    }
    return nullptr;
}

inline void Node::collectAll(StringView tagName, Vector<const Node*>& out) const
{
    for (std::size_t i = 0; i < children.size(); ++i)
    {
        const Node* child = children[i];
        if (child->isElement())
        {
            if (tagName.empty() || StringView(child->tag) == tagName)
            {
                out.push_back(child);
            }
            child->collectAll(tagName, out);
        }
    }
}

inline Vector<const Node*> Node::findAll(StringView tagName) const
{
    Vector<const Node*> out;
    collectAll(tagName, out);
    return out;
}

inline const Node* Node::find(StringView tagName) const
{
    for (std::size_t i = 0; i < children.size(); ++i)
    {
        const Node* child = children[i];
        if (!child->isElement())
        {
            continue;
        }
        if (tagName.empty() || StringView(child->tag) == tagName)
        {
            return child;
        }
        if (const Node* deeper = child->find(tagName))
        {
            return deeper;
        }
    }
    return nullptr;
}

inline const Node* Node::closest(StringView tagName) const
{
    for (const Node* up = parent; up; up = up->parent)
    {
        if (up->isElement() && StringView(up->tag) == tagName)
        {
            return up;
        }
    }
    return nullptr;
}

inline const Node* Node::nextSibling() const
{
    if (!parent)
    {
        return nullptr;
    }
    for (std::size_t i = 0; i + 1 < parent->children.size(); ++i)
    {
        if (parent->children[i] == this)
        {
            return parent->children[i + 1];
        }
    }
    return nullptr;
}

inline const Node* Node::nextElementSibling() const
{
    if (!parent)
    {
        return nullptr;
    }
    std::size_t index = parent->children.size();
    for (std::size_t i = 0; i < parent->children.size(); ++i)
    {
        if (parent->children[i] == this)
        {
            index = i;
            break;
        }
    }
    for (std::size_t i = index + 1; i < parent->children.size(); ++i)
    {
        if (parent->children[i]->isElement())
        {
            return parent->children[i];
        }
    }
    return nullptr;
}

inline String Node::openTag() const
{
    if (type == NodeType::Text)
    {
        return String("#text");
    }
    if (type == NodeType::Comment)
    {
        return String("#comment");
    }
    if (type == NodeType::Document)
    {
        return String("#document");
    }
    String out("<");
    out.append(tag);
    for (std::size_t i = 0; i < attributes.size(); ++i)
    {
        out.append(" ").append(attributes[i].name);
        if (!attributes[i].value.empty())
        {
            out.append("=\"").append(attributes[i].value).append("\"");
        }
    }
    out.append(">");
    return out;
}

inline void Node::htmlInto(String& out) const
{
    switch (type)
    {
        case NodeType::Text:
            out.append(text);
            return;
        case NodeType::Comment:
            out.append("<!--").append(text).append("-->");
            return;
        case NodeType::Doctype:
            out.append("<!DOCTYPE ").append(text).append(">");
            return;
        case NodeType::Document:
            for (std::size_t i = 0; i < children.size(); ++i)
            {
                children[i]->htmlInto(out);
            }
            return;
        case NodeType::Element:
            break;
    }

    out.append(openTag());
    if (isVoid(StringView(tag)))
    {
        return;
    }
    for (std::size_t i = 0; i < children.size(); ++i)
    {
        children[i]->htmlInto(out);
    }
    out.append("</").append(tag).append(">");
}

inline String Node::html() const
{
    String out;
    htmlInto(out);
    return out;
}

inline Soup::~Soup()
{
    delete root_;
}

inline Soup::Soup(Soup&& other)
    : root_(other.root_)
{
    other.root_ = nullptr;
}

inline Soup& Soup::operator=(Soup&& other)
{
    if (this != &other)
    {
        delete root_;
        root_ = other.root_;
        other.root_ = nullptr;
    }
    return *this;
}

inline String Soup::title() const
{
    const Node* node = root_ ? root_->find(StringView("title")) : nullptr;
    return node ? node->getTextTrimmed() : String();
}

namespace
{

class Parser
{
public:
    Parser(StringView html, Node* root)
        : html_(html)
    {
        open_.push_back(root);
    }

    void run()
    {
        while (i_ < html_.size())
        {
            if (html_[i_] == '<')
            {
                const char next = i_ + 1 < html_.size() ? html_[i_ + 1] : '\0';
                if (isAlpha(next))
                {
                    startTag();
                    continue;
                }
                if (next == '/')
                {
                    endTag();
                    continue;
                }
                if (next == '!')
                {
                    bang();
                    continue;
                }
                if (next == '?')
                {
                    skipTo(">");
                    continue;
                }
            }
            textUntilTag();
        }
    }

private:
    StringView html_;
    std::size_t i_ = 0;
    Vector<Node*> open_;

    Node* current() { return open_[open_.size() - 1]; }

    void append(Node* node)
    {
        node->parent = current();
        current()->children.push_back(node);
    }

    void textUntilTag()
    {
        const std::size_t start = i_;
        while (i_ < html_.size())
        {
            if (html_[i_] == '<')
            {
                const char next = i_ + 1 < html_.size() ? html_[i_ + 1] : '\0';
                if (isAlpha(next) || next == '/' || next == '!' || next == '?')
                {
                    break;
                }
            }
            ++i_;
        }
        emitText(html_.substr(start, i_ - start), true);
    }

    void emitText(StringView raw, bool decode)
    {
        if (raw.empty())
        {
            return;
        }
        Node* node = new Node();
        node->type = NodeType::Text;
        node->text = decode ? decodeEntities(raw) : String(raw);
        append(node);
    }

    void skipTo(const char* terminator)
    {
        StringView end(terminator);
        while (i_ < html_.size())
        {
            if (html_[i_] == end[0] && html_.substr(i_).starts_with(terminator))
            {
                i_ += end.size();
                return;
            }
            ++i_;
        }
        i_ = html_.size();
    }

    void bang()
    {
        if (html_.substr(i_).starts_with("<!--"))
        {
            i_ += 4;
            const std::size_t start = i_;
            std::size_t end = i_;
            while (end < html_.size() && !html_.substr(end).starts_with("-->"))
            {
                ++end;
            }
            Node* node = new Node();
            node->type = NodeType::Comment;
            node->text = String(html_.substr(start, end - start));
            append(node);
            i_ = end < html_.size() ? end + 3 : html_.size();
            return;
        }

        if (html_.substr(i_).starts_with("<![CDATA["))
        {
            i_ += 9;
            const std::size_t start = i_;
            std::size_t end = i_;
            while (end < html_.size() && !html_.substr(end).starts_with("]]>"))
            {
                ++end;
            }
            emitText(html_.substr(start, end - start), false);
            i_ = end < html_.size() ? end + 3 : html_.size();
            return;
        }

        const std::size_t start = i_ + 2;
        std::size_t end = start;
        while (end < html_.size() && html_[end] != '>')
        {
            ++end;
        }
        StringView body = html_.substr(start, end - start);
        if (body.size() >= 7 && equalsIgnoreCase(body.substr(0, 7), StringView("doctype")))
        {
            Node* node = new Node();
            node->type = NodeType::Doctype;
            std::size_t k = 7;
            while (k < body.size() && isSpace(body[k]))
            {
                ++k;
            }
            node->text = String(body.substr(k));
            append(node);
        }
        i_ = end < html_.size() ? end + 1 : html_.size();
    }

    // What a start tag may unwind past while looking for the element it
    static bool isUnwindable(StringView tag)
    {
        static const char* kInline[] = {
            "a", "abbr", "b", "bdi", "bdo", "big", "cite", "code", "em", "font",
            "i", "kbd", "label", "mark", "nobr", "q", "s", "samp", "small",
            "span", "strike", "strong", "sub", "sup", "time", "tt", "u", "var",
        };
        for (std::size_t i = 0; i < sizeof(kInline) / sizeof(kInline[0]); ++i)
        {
            if (tag == StringView(kInline[i]))
            {
                return true;
            }
        }
        return false;
    }

    void closeImplicit(StringView name)
    {
        for (std::size_t depth = open_.size(); depth-- > 1;)
        {
            const StringView tag(open_[depth]->tag);
            if (closedBy(tag, name))
            {
                open_.resize(depth);
                return;
            }
            if (!isUnwindable(tag))
            {
                return;
            }
        }
    }

    void startTag()
    {
        const std::size_t nameStart = i_ + 1;
        std::size_t k = nameStart;
        while (k < html_.size() && (isAlpha(html_[k]) || isDigit(html_[k]) ||
                                    html_[k] == '-' || html_[k] == ':' || html_[k] == '_'))
        {
            ++k;
        }
        const String name = toLower(html_.substr(nameStart, k - nameStart));

        Node* node = new Node();
        node->type = NodeType::Element;
        node->tag = name;

        bool selfClosing = false;
        i_ = k;
        readAttributes(node, selfClosing);

        closeImplicit(StringView(name));

        append(node);

        if (selfClosing || isVoid(StringView(name)))
        {
            return;
        }

        if (isRawText(StringView(name)))
        {
            rawText(node, StringView(name));
            return;
        }

        open_.push_back(node);
    }

    void rawText(Node* node, StringView name)
    {
        const std::size_t start = i_;
        std::size_t end = i_;
        for (;;)
        {
            if (end >= html_.size())
            {
                end = html_.size();
                break;
            }
            if (html_[end] == '<' && end + 1 < html_.size() && html_[end + 1] == '/' &&
                html_.size() - (end + 2) >= name.size() &&
                equalsIgnoreCase(html_.substr(end + 2, name.size()), name))
            {
                break;
            }
            ++end;
        }

        if (end > start)
        {
            Node* text = new Node();
            text->type = NodeType::Text;
            StringView raw = html_.substr(start, end - start);
            text->text = name == StringView("title") || name == StringView("textarea")
                             ? decodeEntities(raw)
                             : String(raw);
            text->parent = node;
            node->children.push_back(text);
        }

        i_ = end;
        if (i_ < html_.size())
        {
            while (i_ < html_.size() && html_[i_] != '>')
            {
                ++i_;
            }
            if (i_ < html_.size())
            {
                ++i_;
            }
        }
    }

    void readAttributes(Node* node, bool& selfClosing)
    {
        for (;;)
        {
            while (i_ < html_.size() && isSpace(html_[i_]))
            {
                ++i_;
            }
            if (i_ >= html_.size())
            {
                return;
            }
            if (html_[i_] == '>')
            {
                ++i_;
                return;
            }
            if (html_[i_] == '/')
            {
                selfClosing = true;
                ++i_;
                continue;
            }

            const std::size_t nameStart = i_;
            while (i_ < html_.size() && !isSpace(html_[i_]) && html_[i_] != '=' &&
                   html_[i_] != '>' && html_[i_] != '/')
            {
                ++i_;
            }
            if (i_ == nameStart)
            {
                ++i_;
                continue;
            }
            const String name = toLower(html_.substr(nameStart, i_ - nameStart));

            while (i_ < html_.size() && isSpace(html_[i_]))
            {
                ++i_;
            }

            String value;
            if (i_ < html_.size() && html_[i_] == '=')
            {
                ++i_;
                while (i_ < html_.size() && isSpace(html_[i_]))
                {
                    ++i_;
                }
                if (i_ < html_.size() && (html_[i_] == '"' || html_[i_] == '\''))
                {
                    const char quote = html_[i_++];
                    const std::size_t start = i_;
                    while (i_ < html_.size() && html_[i_] != quote)
                    {
                        ++i_;
                    }
                    value = decodeEntities(html_.substr(start, i_ - start));
                    if (i_ < html_.size())
                    {
                        ++i_;
                    }
                }
                else
                {
                    const std::size_t start = i_;
                    while (i_ < html_.size() && !isSpace(html_[i_]) && html_[i_] != '>')
                    {
                        ++i_;
                    }
                    value = decodeEntities(html_.substr(start, i_ - start));
                }
            }

            if (!node->attribute(StringView(name)))
            {
                node->attributes.push_back(Attribute(name, value));
            }
        }
    }

    void endTag()
    {
        const std::size_t nameStart = i_ + 2;
        std::size_t k = nameStart;
        while (k < html_.size() && html_[k] != '>' && !isSpace(html_[k]))
        {
            ++k;
        }
        const String name = toLower(html_.substr(nameStart, k - nameStart));
        while (k < html_.size() && html_[k] != '>')
        {
            ++k;
        }
        i_ = k < html_.size() ? k + 1 : html_.size();

        if (name.empty() || isVoid(StringView(name)))
        {
            return;
        }

        for (std::size_t depth = open_.size(); depth-- > 1;)
        {
            if (StringView(open_[depth]->tag) == StringView(name))
            {
                open_.resize(depth);
                return;
            }
        }
    }
};

}

inline Soup Soup::parse(StringView html)
{
    Soup soup;
    soup.root_ = new Node();
    soup.root_->type = NodeType::Document;
    Parser parser(html, soup.root_);
    parser.run();
    return soup;
}

inline Soup Soup::parse(const char* html)
{
    return parse(StringView(html ? html : ""));
}

namespace
{

// Excludes ":" and "." on purpose: otherwise "li:first-child" reads as one tag.
inline bool isNameChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || (unsigned char)c >= 0x80u;
}

enum class AttrOp : unsigned char
{
    Exists,
    Equals,
    Prefix,
    Suffix,
    Contains,
    Word,
};

struct AttrTest
{
    String name;
    String value;
    AttrOp op = AttrOp::Exists;
};

enum class Pseudo : unsigned char
{
    FirstChild,
    LastChild,
    OnlyChild,
    Empty,
    Not,
};

struct Compound;

struct PseudoTest
{
    Pseudo kind = Pseudo::FirstChild;
    Compound* inner = nullptr;
};

struct Compound
{
    String tag;
    Vector<String> classes;
    String id;
    Vector<AttrTest> attributes;
    Vector<PseudoTest> pseudos;

    ~Compound()
    {
        for (std::size_t i = 0; i < pseudos.size(); ++i)
        {
            delete pseudos[i].inner;
        }
    }

    Compound() {}
    Compound(const Compound&) = delete;
    Compound& operator=(const Compound&) = delete;
};

enum class Combinator : unsigned char
{
    Descendant,
    Child,
};

struct Selector
{
    Vector<Compound*> steps;
    Vector<Combinator> combinators;

    ~Selector()
    {
        for (std::size_t i = 0; i < steps.size(); ++i)
        {
            delete steps[i];
        }
    }

    Selector() {}
    Selector(const Selector&) = delete;
    Selector& operator=(const Selector&) = delete;
};

class SelectorParser
{
public:
    explicit SelectorParser(StringView text)
        : text_(text)
    {
    }

    bool parseList(Vector<Selector*>& out)
    {
        for (;;)
        {
            skipSpace();
            Selector* selector = new Selector();
            if (!parseSelector(*selector))
            {
                delete selector;
                return false;
            }
            out.push_back(selector);
            skipSpace();
            if (i_ >= text_.size())
            {
                return true;
            }
            if (text_[i_] != ',')
            {
                return false;
            }
            ++i_;
        }
    }

private:
    StringView text_;
    std::size_t i_ = 0;

    void skipSpace()
    {
        while (i_ < text_.size() && isSpace(text_[i_]))
        {
            ++i_;
        }
    }

    bool atEnd() const { return i_ >= text_.size(); }

    bool parseSelector(Selector& out)
    {
        Compound* first = new Compound();
        if (!parseCompound(*first))
        {
            delete first;
            return false;
        }
        out.steps.push_back(first);

        for (;;)
        {
            const std::size_t save = i_;
            bool sawSpace = false;
            while (i_ < text_.size() && isSpace(text_[i_]))
            {
                sawSpace = true;
                ++i_;
            }
            if (atEnd() || text_[i_] == ',')
            {
                i_ = atEnd() ? i_ : i_;
                return true;
            }

            Combinator combinator = Combinator::Descendant;
            if (text_[i_] == '>')
            {
                combinator = Combinator::Child;
                ++i_;
                skipSpace();
            }
            else if (!sawSpace)
            {
                i_ = save;
                return false;
            }

            Compound* step = new Compound();
            if (!parseCompound(*step))
            {
                delete step;
                return false;
            }
            out.steps.push_back(step);
            out.combinators.push_back(combinator);
        }
    }

    String readName()
    {
        String out;
        while (i_ < text_.size() && isNameChar(text_[i_]))
        {
            out.push_back(lower(text_[i_]));
            ++i_;
        }
        return out;
    }

    bool parseCompound(Compound& out)
    {
        bool any = false;

        if (i_ < text_.size() && text_[i_] == '*')
        {
            out.tag = String("*");
            ++i_;
            any = true;
        }
        else if (i_ < text_.size() && isNameChar(text_[i_]))
        {
            out.tag = readName();
            any = !out.tag.empty();
        }

        for (;;)
        {
            if (atEnd())
            {
                break;
            }
            const char c = text_[i_];

            if (c == '.')
            {
                ++i_;
                const String name = readName();
                if (name.empty())
                {
                    return false;
                }
                out.classes.push_back(name);
                any = true;
                continue;
            }

            if (c == '#')
            {
                ++i_;
                const String name = readName();
                if (name.empty())
                {
                    return false;
                }
                out.id = name;
                any = true;
                continue;
            }

            if (c == '[')
            {
                ++i_;
                if (!parseAttribute(out))
                {
                    return false;
                }
                any = true;
                continue;
            }

            if (c == ':')
            {
                ++i_;
                if (!parsePseudo(out))
                {
                    return false;
                }
                any = true;
                continue;
            }

            break;
        }

        return any;
    }

    bool parseAttribute(Compound& out)
    {
        skipSpace();
        AttrTest test;
        test.name = readName();
        if (test.name.empty())
        {
            return false;
        }
        skipSpace();
        if (atEnd())
        {
            return false;
        }

        if (text_[i_] == ']')
        {
            test.op = AttrOp::Exists;
            ++i_;
            out.attributes.push_back(test);
            return true;
        }

        switch (text_[i_])
        {
            case '=': test.op = AttrOp::Equals; ++i_; break;
            case '^': test.op = AttrOp::Prefix; ++i_; break;
            case '$': test.op = AttrOp::Suffix; ++i_; break;
            case '*': test.op = AttrOp::Contains; ++i_; break;
            case '~': test.op = AttrOp::Word; ++i_; break;
            default: return false;
        }
        if (test.op != AttrOp::Equals)
        {
            if (atEnd() || text_[i_] != '=')
            {
                return false;
            }
            ++i_;
        }

        skipSpace();
        if (atEnd())
        {
            return false;
        }

        if (text_[i_] == '"' || text_[i_] == '\'')
        {
            const char quote = text_[i_++];
            const std::size_t start = i_;
            while (i_ < text_.size() && text_[i_] != quote)
            {
                ++i_;
            }
            if (atEnd())
            {
                return false;
            }
            test.value = String(text_.substr(start, i_ - start));
            ++i_;
        }
        else
        {
            const std::size_t start = i_;
            while (i_ < text_.size() && text_[i_] != ']' && !isSpace(text_[i_]))
            {
                ++i_;
            }
            test.value = String(text_.substr(start, i_ - start));
        }

        skipSpace();
        if (atEnd() || text_[i_] != ']')
        {
            return false;
        }
        ++i_;

        if (test.value.empty())
        {
            return false;
        }
        out.attributes.push_back(test);
        return true;
    }

    bool parsePseudo(Compound& out)
    {
        const String name = readName();
        PseudoTest test;

        if (name == "first-child")
        {
            test.kind = Pseudo::FirstChild;
        }
        else if (name == "last-child")
        {
            test.kind = Pseudo::LastChild;
        }
        else if (name == "only-child")
        {
            test.kind = Pseudo::OnlyChild;
        }
        else if (name == "empty")
        {
            test.kind = Pseudo::Empty;
        }
        else if (name == "not")
        {
            if (atEnd() || text_[i_] != '(')
            {
                return false;
            }
            ++i_;
            skipSpace();
            test.kind = Pseudo::Not;
            test.inner = new Compound();
            if (!parseCompound(*test.inner))
            {
                delete test.inner;
                return false;
            }
            skipSpace();
            if (atEnd() || text_[i_] != ')')
            {
                delete test.inner;
                return false;
            }
            ++i_;
        }
        else
        {
            return false;
        }

        out.pseudos.push_back(test);
        return true;
    }
};

inline bool matchAttr(const Node& node, const AttrTest& test)
{
    const String* value = node.attribute(StringView(test.name));
    if (!value)
    {
        return false;
    }
    StringView have(*value);
    StringView want(test.value);

    switch (test.op)
    {
        case AttrOp::Exists:
            return true;
        case AttrOp::Equals:
            return have == want;
        case AttrOp::Prefix:
            return have.size() >= want.size() && have.substr(0, want.size()) == want;
        case AttrOp::Suffix:
            return have.size() >= want.size() &&
                   have.substr(have.size() - want.size()) == want;
        case AttrOp::Contains:
            return have.find(want.data()) != StringView::npos;
        case AttrOp::Word:
        {
            std::size_t i = 0;
            while (i < have.size())
            {
                while (i < have.size() && isSpace(have[i]))
                {
                    ++i;
                }
                const std::size_t start = i;
                while (i < have.size() && !isSpace(have[i]))
                {
                    ++i;
                }
                if (i > start && have.substr(start, i - start) == want)
                {
                    return true;
                }
            }
            return false;
        }
    }
    return false;
}

inline bool matchCompound(const Node& node, const Compound& compound);

inline bool matchPseudo(const Node& node, const PseudoTest& test)
{
    switch (test.kind)
    {
        case Pseudo::FirstChild:
        {
            if (!node.parent)
            {
                return false;
            }
            const Vector<const Node*> siblings = node.parent->childElements();
            return !siblings.empty() && siblings[0] == &node;
        }
        case Pseudo::LastChild:
        {
            if (!node.parent)
            {
                return false;
            }
            const Vector<const Node*> siblings = node.parent->childElements();
            return !siblings.empty() && siblings[siblings.size() - 1] == &node;
        }
        case Pseudo::OnlyChild:
        {
            if (!node.parent)
            {
                return false;
            }
            const Vector<const Node*> siblings = node.parent->childElements();
            return siblings.size() == 1 && siblings[0] == &node;
        }
        case Pseudo::Empty:
        {
            for (std::size_t i = 0; i < node.children.size(); ++i)
            {
                const Node* child = node.children[i];
                if (child->isElement())
                {
                    return false;
                }
                if (child->isText() && !child->text.empty())
                {
                    return false;
                }
            }
            return true;
        }
        case Pseudo::Not:
            return test.inner ? !matchCompound(node, *test.inner) : false;
    }
    return false;
}

inline bool matchCompound(const Node& node, const Compound& compound)
{
    if (!node.isElement())
    {
        return false;
    }
    if (!compound.tag.empty() && compound.tag != "*" &&
        StringView(node.tag) != StringView(compound.tag))
    {
        return false;
    }
    if (!compound.id.empty())
    {
        const String* have = node.attribute(StringView("id"));
        if (!have || *have != compound.id)
        {
            return false;
        }
    }
    for (std::size_t i = 0; i < compound.classes.size(); ++i)
    {
        if (!node.hasClass(StringView(compound.classes[i])))
        {
            return false;
        }
    }
    for (std::size_t i = 0; i < compound.attributes.size(); ++i)
    {
        if (!matchAttr(node, compound.attributes[i]))
        {
            return false;
        }
    }
    for (std::size_t i = 0; i < compound.pseudos.size(); ++i)
    {
        if (!matchPseudo(node, compound.pseudos[i]))
        {
            return false;
        }
    }
    return true;
}

inline bool matchAncestors(const Node& node, const Selector& selector, std::size_t step)
{
    if (step == 0)
    {
        return true;
    }
    const Combinator combinator = selector.combinators[step - 1];
    const Compound& want = *selector.steps[step - 1];

    if (combinator == Combinator::Child)
    {
        const Node* parent = node.parent;
        if (!parent || !matchCompound(*parent, want))
        {
            return false;
        }
        return matchAncestors(*parent, selector, step - 1);
    }

    for (const Node* up = node.parent; up; up = up->parent)
    {
        if (matchCompound(*up, want) && matchAncestors(*up, selector, step - 1))
        {
            return true;
        }
    }
    return false;
}

inline bool matches(const Node& node, const Selector& selector)
{
    const std::size_t last = selector.steps.size() - 1;
    if (!matchCompound(node, *selector.steps[last]))
    {
        return false;
    }
    return matchAncestors(node, selector, last);
}

inline void collect(const Node& node, Vector<const Node*>& out)
{
    for (std::size_t i = 0; i < node.children.size(); ++i)
    {
        const Node* child = node.children[i];
        if (child->isElement())
        {
            out.push_back(child);
            collect(*child, out);
        }
    }
}

}

inline Vector<const Node*> Node::select(StringView selector) const
{
    Vector<const Node*> out;

    Vector<Selector*> selectors;
    SelectorParser parser(selector);
    const bool ok = parser.parseList(selectors);

    if (ok)
    {
        Vector<const Node*> candidates;
        collect(*this, candidates);

        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            for (std::size_t k = 0; k < selectors.size(); ++k)
            {
                if (matches(*candidates[i], *selectors[k]))
                {
                    out.push_back(candidates[i]);
                    break;
                }
            }
        }
    }

    for (std::size_t i = 0; i < selectors.size(); ++i)
    {
        delete selectors[i];
    }
    return out;
}

inline const Node* Node::selectOne(StringView selector) const
{
    const Vector<const Node*> all = select(selector);
    return all.empty() ? nullptr : all[0];
}

}
