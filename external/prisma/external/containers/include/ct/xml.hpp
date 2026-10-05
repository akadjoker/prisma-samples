#pragma once

#include <clocale> 

#include "detail/utils.hpp"
#include "hashset.hpp"
#include "string.hpp"
#include "vector.hpp"

namespace ct
{
    namespace detail
    {
        struct XmlParser; 
    }

    class Xml
    {
        friend struct detail::XmlParser; 

    public:
        struct Attribute
        {
            String name;
            String value;

            Attribute() {}
            Attribute(String n, String v) : name(detail::move(n)), value(detail::move(v)) {}
        };

        using Children = Vector<Xml>;
        using Attributes = Vector<Attribute>;

        static constexpr std::size_t kMaxDepth = 200;

        struct Error
        {
            const char *message; 
            std::size_t offset;
            std::size_t line;   
            std::size_t column; 

            Error() noexcept : message(nullptr), offset(0), line(0), column(0) {}
            explicit operator bool() const noexcept { return message != nullptr; }
        };

        Xml() noexcept : children_(nullptr) {}
        explicit Xml(String tag) : tag_(detail::move(tag)), children_(nullptr) {}

        Xml(const Xml &o);
        Xml(Xml &&o) noexcept;
        Xml &operator=(const Xml &o);
        Xml &operator=(Xml &&o) noexcept;
        ~Xml();

        const String &tag() const noexcept { return tag_; }
        void set_tag(String t) { tag_ = detail::move(t); }

        const Attributes &attributes() const noexcept { return attrs_; }
        Attributes &attributes() noexcept { return attrs_; }

        const String *attr(const char *name) const noexcept
        {
            for (std::size_t i = 0; i < attrs_.size(); ++i)
                if (attrs_[i].name == name)
                    return &attrs_[i].value;
            return nullptr;
        }
        bool has_attr(const char *name) const noexcept { return attr(name) != nullptr; }

        const char *attr_cstr(const char *name, const char *def = "") const noexcept
        {
            const String *v = attr(name);
            return v ? v->c_str() : def;
        }
        std::int64_t attr_int(const char *name, std::int64_t def = 0) const noexcept;
        std::uint64_t attr_uint(const char *name, std::uint64_t def = 0) const noexcept;
        double attr_double(const char *name, double def = 0.0) const noexcept;
        bool attr_bool(const char *name, bool def = false) const noexcept;

        Xml &set_attr(String name, String value);
        bool erase_attr(const char *name);

        const String &text() const noexcept { return text_; }
        void set_text(String t) { text_ = detail::move(t); }

        const String &tail() const noexcept { return tail_; }
        void set_tail(String t) { tail_ = detail::move(t); }

        bool has_mixed_content() const noexcept;

        String text_trimmed() const;

        const Children &children() const noexcept;

        Children &children() noexcept;

        Xml *child(const char *tag) noexcept;
        const Xml *child(const char *tag) const noexcept;

        Xml &add_child(Xml node);

        std::size_t size() const noexcept { return children_ ? children_->size() : 0; }
        bool empty() const noexcept { return !children_ || children_->empty(); }

        static Xml parse(const char *text, std::size_t len, Error *err = nullptr);
        static Xml parse(const char *text, Error *err = nullptr)
        {
            return parse(text, text ? std::strlen(text) : 0, err);
        }
        static Xml parse(const String &text, Error *err = nullptr)
        {
            return parse(text.data(), text.size(), err);
        }

        String dump(int indent = -1) const;
        void dump_to(String &out, int indent = -1) const;

        String dump_document(int indent = -1) const;

    private:
        String tag_;
        Attributes attrs_;
        Children *children_; 

        String text_;
        String tail_;

        void ensure_children() { if (!children_) children_ = new_children(); }

        static Children *new_children();
        static Children *new_children(const Children &src);
        static void del_children(Children *p);

        void dump_impl(String &out, int indent, int level) const;
    };

    namespace detail
    {
        inline bool xml_is_digit(char c) { return c >= '0' && c <= '9'; }

        inline bool xml_is_all_ws(const String &s)
        {
            for (std::size_t i = 0; i < s.size(); ++i)
            {
                const char c = s[i];
                if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
                    return false;
            }
            return true;
        }

        inline char xml_decimal_point()
        {
            const char *dp = std::localeconv()->decimal_point;
            return (dp && *dp) ? *dp : '.';
        }

        inline bool xml_parse_digits(const char *&p, std::uint64_t &mag)
        {
            bool fits = true;
            mag = 0;
            for (; xml_is_digit(*p); ++p)
            {
                const std::uint64_t digit = static_cast<std::uint64_t>(*p - '0');
                if (mag > ((std::numeric_limits<std::uint64_t>::max)() - digit) / 10)
                    fits = false;
                else
                    mag = mag * 10 + digit;
            }
            return fits;
        }

        inline bool xml_real_fits_int(double d) noexcept
        {
            return d == d && d >= -9223372036854775808.0 && d < 9223372036854775808.0;
        }

        inline bool xml_real_fits_uint(double d) noexcept
        {
            return d == d && d >= 0.0 && d < 18446744073709551616.0;
        }

        inline double xml_strtod_locale(const char *s)
        {
            const char dp = xml_decimal_point();
            if (dp == '.')
                return std::strtod(s, nullptr);
            char small[64];
            char *buf = small;
            String big;
            const std::size_t n = std::strlen(s);
            if (n + 1 > sizeof(small))
            {
                big.resize(n + 1);
                buf = big.data();
            }
            std::memcpy(buf, s, n + 1);
            for (std::size_t i = 0; i < n; ++i)
                if (buf[i] == '.')
                    buf[i] = dp;
            return std::strtod(buf, nullptr);
        }
    } 

    inline std::int64_t Xml::attr_int(const char *name, std::int64_t def) const noexcept
    {
        const String *v = attr(name);
        if (!v || v->empty())
            return def;
        const char *p = v->c_str();
        bool neg = false;
        if (*p == '+' || *p == '-')
            neg = (*p++ == '-');
        if (!detail::xml_is_digit(*p))
            return def;
        std::uint64_t mag = 0;
        const bool fits = detail::xml_parse_digits(p, mag);
        if (*p == '\0')
        {
            const std::uint64_t limit =
                static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
            if (!fits)
                return def;
            if (neg)
            {
                if (mag <= limit)
                    return -static_cast<std::int64_t>(mag);
                if (mag == limit + 1)
                    return (std::numeric_limits<std::int64_t>::min)();
                return def;
            }
            return mag <= limit ? static_cast<std::int64_t>(mag) : def;
        }
        if (*p == '.' || *p == 'e' || *p == 'E')
        {
            const double d = detail::xml_strtod_locale(v->c_str());
            return detail::xml_real_fits_int(d) ? static_cast<std::int64_t>(d) : def;
        }
        return def;
    }

    inline std::uint64_t Xml::attr_uint(const char *name, std::uint64_t def) const noexcept
    {
        const String *v = attr(name);
        if (!v || v->empty() || *v->c_str() == '-')
            return def;
        const char *p = v->c_str();
        if (*p == '+')
            ++p;
        if (!detail::xml_is_digit(*p))
            return def;
        std::uint64_t mag = 0;
        const bool fits = detail::xml_parse_digits(p, mag);
        if (*p == '\0')
            return fits ? mag : def;
        if (*p == '.' || *p == 'e' || *p == 'E')
        {
            const double d = detail::xml_strtod_locale(v->c_str());
            return detail::xml_real_fits_uint(d) ? static_cast<std::uint64_t>(d) : def;
        }
        return def;
    }

    inline double Xml::attr_double(const char *name, double def) const noexcept
    {
        const String *v = attr(name);
        if (!v || v->empty())
            return def;
        char *end = nullptr;
        const char dp = detail::xml_decimal_point();
        double d;
        if (dp == '.')
            d = std::strtod(v->c_str(), &end);
        else
            return detail::xml_strtod_locale(v->c_str()); 
        if (end == v->c_str())
            return def; 
        return d;
    }

    inline bool Xml::attr_bool(const char *name, bool def) const noexcept
    {
        const String *v = attr(name);
        if (!v)
            return def;
        if (*v == "true" || *v == "1")
            return true;
        if (*v == "false" || *v == "0")
            return false;
        return def;
    }

    inline Xml &Xml::set_attr(String name, String value)
    {
        for (std::size_t i = 0; i < attrs_.size(); ++i)
            if (attrs_[i].name == name)
            {
                attrs_[i].value = detail::move(value);
                return *this;
            }
        attrs_.push_back(Attribute(detail::move(name), detail::move(value)));
        return *this;
    }

    inline bool Xml::erase_attr(const char *name)
    {
        for (std::size_t i = 0; i < attrs_.size(); ++i)
            if (attrs_[i].name == name)
            {
                attrs_.erase(attrs_.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
        return false;
    }

    inline String Xml::text_trimmed() const
    {
        const char *p = text_.data();
        const char *e = p + text_.size();
        auto is_ws = [](char c)
        { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
        while (p != e && is_ws(*p))
            ++p;
        while (e != p && is_ws(*(e - 1)))
            --e;
        return String(p, static_cast<std::size_t>(e - p));
    }

    inline Xml::Children *Xml::new_children()
    {
        HeapAlloc a;
        return ::new (a.allocate(sizeof(Children), alignof(Children))) Children();
    }
    inline Xml::Children *Xml::new_children(const Children &src)
    {
        HeapAlloc a;
        return ::new (a.allocate(sizeof(Children), alignof(Children))) Children(src);
    }
    inline void Xml::del_children(Children *p)
    {
        if (!p)
            return;
        p->~Children();
        HeapAlloc().deallocate(p, sizeof(Children));
    }

    inline Xml::Xml(const Xml &o)
        : tag_(o.tag_), attrs_(o.attrs_),
          children_(o.children_ ? new_children(*o.children_) : nullptr), text_(o.text_), tail_(o.tail_)
    {
    }

    inline Xml::Xml(Xml &&o) noexcept
        : tag_(detail::move(o.tag_)), attrs_(detail::move(o.attrs_)),
          children_(o.children_), text_(detail::move(o.text_)), tail_(detail::move(o.tail_))
    {
        o.children_ = nullptr;
    }

    inline Xml &Xml::operator=(const Xml &o)
    {
        if (this != &o)
        {
            tag_ = o.tag_;
            attrs_ = o.attrs_;
            text_ = o.text_;
            tail_ = o.tail_;
            Children *nc = o.children_ ? new_children(*o.children_) : nullptr;
            del_children(children_);
            children_ = nc;
        }
        return *this;
    }

    inline Xml &Xml::operator=(Xml &&o) noexcept
    {
        if (this != &o)
        {
            tag_ = detail::move(o.tag_);
            attrs_ = detail::move(o.attrs_);
            text_ = detail::move(o.text_);
            tail_ = detail::move(o.tail_);
            del_children(children_);
            children_ = o.children_;
            o.children_ = nullptr;
        }
        return *this;
    }

    inline bool Xml::has_mixed_content() const noexcept
    {
        if (!children_ || children_->empty())
            return false;
        if (!text_.empty())
            return true;
        for (std::size_t i = 0; i < children_->size(); ++i)
            if (!(*children_)[i].tail_.empty())
                return true;
        return false;
    }

    inline Xml::~Xml() { del_children(children_); }

    inline const Xml::Children &Xml::children() const noexcept
    {
        static const Children kEmpty;
        return children_ ? *children_ : kEmpty;
    }
    inline Xml::Children &Xml::children() noexcept
    {
        ensure_children();
        return *children_;
    }

    inline Xml *Xml::child(const char *tag) noexcept
    {
        if (!children_)
            return nullptr;
        for (std::size_t i = 0; i < children_->size(); ++i)
            if ((*children_)[i].tag_ == tag)
                return &(*children_)[i];
        return nullptr;
    }
    inline const Xml *Xml::child(const char *tag) const noexcept
    {
        if (!children_)
            return nullptr;
        for (std::size_t i = 0; i < children_->size(); ++i)
            if ((*children_)[i].tag_ == tag)
                return &(*children_)[i];
        return nullptr;
    }

    inline Xml &Xml::add_child(Xml node)
    {
        ensure_children();
        children_->push_back(detail::move(node));
        return children_->back();
    }

    namespace detail
    {
        inline void xml_newline(String &out, int indent, int level)
        {
            if (indent < 0)
                return;
            static const char kSpaces[] =
                "                                                                ";
            const std::size_t kN = sizeof(kSpaces) - 1; 
            out.push_back('\n');
            std::size_t spaces =
                static_cast<std::size_t>(indent) * static_cast<std::size_t>(level);
            while (spaces >= kN)
            {
                out.append(kSpaces, kN);
                spaces -= kN;
            }
            if (spaces)
                out.append(kSpaces, spaces);
        }

        inline void xml_escape_text(String &out, const char *s, std::size_t n)
        {
            std::size_t chunk = 0;
            char buf[16];
            for (std::size_t i = 0; i < n; ++i)
            {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                const char *esc = nullptr;
                std::size_t esc_len = 0;
                switch (c)
                {
                case '&': esc = "&amp;"; esc_len = 5; break;
                case '<': esc = "&lt;"; esc_len = 4; break;
                case '>': esc = "&gt;"; esc_len = 4; break;
                default:
                    if (c < 0x20 && c != '\t' && c != '\n' && c != '\r')
                    {
                        std::snprintf(buf, sizeof(buf), "&#%u;", c);
                        esc = buf;
                        esc_len = std::strlen(buf);
                    }
                    break;
                }
                if (!esc)
                {
                    ++chunk;
                    continue;
                }
                if (chunk)
                {
                    out.append(s + i - chunk, chunk);
                    chunk = 0;
                }
                out.append(esc, esc_len);
            }
            if (chunk)
                out.append(s + n - chunk, chunk);
        }

        inline void xml_escape_attr(String &out, const char *s, std::size_t n)
        {
            std::size_t chunk = 0;
            char buf[16];
            for (std::size_t i = 0; i < n; ++i)
            {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                const char *esc = nullptr;
                std::size_t esc_len = 0;
                switch (c)
                {
                case '&': esc = "&amp;"; esc_len = 5; break;
                case '<': esc = "&lt;"; esc_len = 4; break;
                case '>': esc = "&gt;"; esc_len = 4; break;
                case '"': esc = "&quot;"; esc_len = 6; break;
                case '\t': esc = "&#9;"; esc_len = 4; break;
                case '\n': esc = "&#10;"; esc_len = 5; break;
                case '\r': esc = "&#13;"; esc_len = 5; break;
                default:
                    if (c < 0x20)
                    {
                        std::snprintf(buf, sizeof(buf), "&#%u;", c);
                        esc = buf;
                        esc_len = std::strlen(buf);
                    }
                    break;
                }
                if (!esc)
                {
                    ++chunk;
                    continue;
                }
                if (chunk)
                {
                    out.append(s + i - chunk, chunk);
                    chunk = 0;
                }
                out.append(esc, esc_len);
            }
            if (chunk)
                out.append(s + n - chunk, chunk);
        }
    } 

    inline void Xml::dump_impl(String &out, int indent, int level) const
    {
        out.push_back('<');
        out.append(tag_.data(), tag_.size());
        for (std::size_t i = 0; i < attrs_.size(); ++i)
        {
            out.push_back(' ');
            out.append(attrs_[i].name.data(), attrs_[i].name.size());
            out.append("=\"", 2);
            detail::xml_escape_attr(out, attrs_[i].value.data(), attrs_[i].value.size());
            out.push_back('"');
        }

        const bool has_text = !text_.empty();
        const bool has_children = children_ && !children_->empty();
        if (!has_text && !has_children)
        {
            out.append("/>", 2);
            return;
        }

        out.push_back('>');
        if (has_text)
            detail::xml_escape_text(out, text_.data(), text_.size());
        const bool mixed = has_mixed_content();
        for (std::size_t i = 0; has_children && i < children_->size(); ++i)
        {
            const Xml &child = (*children_)[i];
            if (!mixed)
                detail::xml_newline(out, indent, level + 1);
            child.dump_impl(out, indent, level + 1);
            if (!child.tail_.empty())
                detail::xml_escape_text(out, child.tail_.data(), child.tail_.size());
        }
        if (has_children && !mixed)
            detail::xml_newline(out, indent, level);
        out.append("</", 2);
        out.append(tag_.data(), tag_.size());
        out.push_back('>');
    }

    inline void Xml::dump_to(String &out, int indent) const { dump_impl(out, indent, 0); }

    inline String Xml::dump(int indent) const
    {
        String out;
        out.reserve(64);
        dump_impl(out, indent, 0);
        return out;
    }

    inline String Xml::dump_document(int indent) const
    {
        String out;
        out.reserve(96);
        out.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
        detail::xml_newline(out, indent, 0);
        if (indent < 0)
            out.push_back(' '); 
        dump_impl(out, indent, 0);
        return out;
    }

    namespace detail
    {
        inline void xml_encode_utf8(String &out, std::uint32_t cp)
        {
            if (cp < 0x80)
                out.push_back(static_cast<char>(cp));
            else if (cp < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else if (cp < 0x10000)
            {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }

        inline bool xml_is_name_start(char c)
        {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' ||
                   c == ':' || (static_cast<unsigned char>(c) >= 0x80);
        }
        inline bool xml_is_name_char(char c)
        {
            return xml_is_name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
        }
        inline bool xml_is_hex(char c)
        {
            return xml_is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }
        inline unsigned xml_hex_val(char c)
        {
            if (c <= '9')
                return static_cast<unsigned>(c - '0');
            if (c <= 'F')
                return static_cast<unsigned>(c - 'A' + 10);
            return static_cast<unsigned>(c - 'a' + 10);
        }

        struct XmlParser
        {
            const char *first;
            const char *cur;
            const char *last;
            const char *err_at;
            const char *err_msg;

            XmlParser(const char *b, const char *e)
                : first(b), cur(b), last(e), err_at(b), err_msg(nullptr) {}

            bool fail(const char *msg, const char *at)
            {
                if (!err_msg)
                {
                    err_msg = msg;
                    err_at = at;
                }
                return false;
            }

            void skip_ws()
            {
                while (cur != last &&
                       (*cur == ' ' || *cur == '\t' || *cur == '\n' || *cur == '\r'))
                    ++cur;
            }

            bool starts_with(const char *lit, std::size_t n) const
            {
                return static_cast<std::size_t>(last - cur) >= n &&
                       std::memcmp(cur, lit, n) == 0;
            }

            bool skip_comment()
            {
                const char *start = cur;
                cur += 4; 
                for (;; ++cur)
                {
                    if (last - cur < 3)
                        return fail("comentario sem fecho '-->'", start);
                    if (cur[0] == '-' && cur[1] == '-' && cur[2] == '>')
                    {
                        cur += 3;
                        return true;
                    }
                }
            }

            bool skip_pi()
            {
                const char *start = cur;
                cur += 2; 
                for (;; ++cur)
                {
                    if (last - cur < 2)
                        return fail("instrucao de processamento sem fecho '?>'", start);
                    if (cur[0] == '?' && cur[1] == '>')
                    {
                        cur += 2;
                        return true;
                    }
                }
            }

            bool skip_doctype()
            {
                const char *start = cur;
                cur += 9; 
                int bracket = 0;
                for (;;)
                {
                    if (cur == last)
                        return fail("DOCTYPE sem fecho", start);
                    const char c = *cur;
                    if (c == '"' || c == '\'')
                    {
                        const char *quote_start = cur;
                        ++cur;
                        while (cur != last && *cur != c)
                            ++cur;
                        if (cur == last)
                            return fail("literal do DOCTYPE sem fecho", quote_start);
                        ++cur;
                        continue;
                    }
                    if (c == '<' && starts_with("<!--", 4))
                    {
                        if (!skip_comment())
                            return false;
                        continue;
                    }
                    if (c == '<' && starts_with("<?", 2))
                    {
                        if (!skip_pi())
                            return false;
                        continue;
                    }
                    if (c == '[')
                        ++bracket;
                    else if (c == ']')
                        --bracket;
                    else if (c == '>' && bracket <= 0)
                    {
                        ++cur;
                        return true;
                    }
                    ++cur;
                }
            }

            bool skip_misc()
            {
                for (;;)
                {
                    skip_ws();
                    if (cur == last)
                        return true;
                    if (starts_with("<!--", 4))
                    {
                        if (!skip_comment())
                            return false;
                        continue;
                    }
                    if (starts_with("<?", 2))
                    {
                        if (!skip_pi())
                            return false;
                        continue;
                    }
                    if (starts_with("<!DOCTYPE", 9))
                    {
                        if (!skip_doctype())
                            return false;
                        continue;
                    }
                    return true;
                }
            }

            bool read_name(String &out)
            {
                if (cur == last || !xml_is_name_start(*cur))
                    return fail("nome invalido", cur);
                const char *start = cur;
                ++cur;
                while (cur != last && xml_is_name_char(*cur))
                    ++cur;
                out.assign(start, static_cast<std::size_t>(cur - start));
                return true;
            }

            bool decode_entity(String &out)
            {
                if (cur == last)
                    return fail("entidade incompleta", cur);
                if (*cur == '#')
                {
                    ++cur;
                    bool hex = false;
                    if (cur != last && (*cur == 'x' || *cur == 'X'))
                    {
                        hex = true;
                        ++cur;
                    }
                    const char *start = cur;
                    std::uint32_t cp = 0;
                    bool overflow = false;
                    if (hex)
                        for (; cur != last && xml_is_hex(*cur); ++cur)
                        {
                            cp = cp * 16 + xml_hex_val(*cur);
                            if (cp > 0x10FFFF)
                                overflow = true;
                        }
                    else
                        for (; cur != last && xml_is_digit(*cur); ++cur)
                        {
                            cp = cp * 10 + static_cast<unsigned>(*cur - '0');
                            if (cp > 0x10FFFF)
                                overflow = true;
                        }
                    if (cur == start)
                        return fail("referencia numerica vazia", start);
                    if (cur == last || *cur != ';')
                        return fail("referencia numerica sem ';'", cur);
                    ++cur;
                    if (overflow || cp == 0 || (cp >= 0xD800 && cp <= 0xDFFF))
                        return fail("codepoint invalido em referencia numerica", start);
                    xml_encode_utf8(out, cp);
                    return true;
                }
                const char *start = cur;
                while (cur != last && *cur != ';' && (cur - start) < 16)
                    ++cur;
                if (cur == last || *cur != ';')
                    return fail("entidade sem ';'", start);
                const std::size_t n = static_cast<std::size_t>(cur - start);
                ++cur; 
                if (n == 3 && std::memcmp(start, "amp", 3) == 0)
                    out.push_back('&');
                else if (n == 2 && std::memcmp(start, "lt", 2) == 0)
                    out.push_back('<');
                else if (n == 2 && std::memcmp(start, "gt", 2) == 0)
                    out.push_back('>');
                else if (n == 4 && std::memcmp(start, "quot", 4) == 0)
                    out.push_back('"');
                else if (n == 4 && std::memcmp(start, "apos", 4) == 0)
                    out.push_back('\'');
                else
                    return fail("entidade desconhecida (sem suporte a DTD)", start);
                return true;
            }

            bool parse_char_data(String &out)
            {
                for (;;)
                {
                    const char *chunk = cur;
                    while (cur != last && *cur != '<' && *cur != '&')
                        ++cur;
                    if (cur != chunk)
                        out.append(chunk, static_cast<std::size_t>(cur - chunk));
                    if (cur == last)
                        return fail("conteudo do elemento sem fecho", chunk);
                    if (*cur == '<')
                        return true;
                    ++cur; 
                    if (!decode_entity(out))
                        return false;
                }
            }

            bool parse_attr_value(String &out, char quote)
            {
                for (;;)
                {
                    if (cur == last)
                        return fail("valor de atributo sem fecho", cur);
                    const char c = *cur;
                    if (c == quote)
                    {
                        ++cur;
                        return true;
                    }
                    if (c == '<')
                        return fail("'<' cru dentro de valor de atributo", cur);
                    if (c == '&')
                    {
                        ++cur;
                        if (!decode_entity(out))
                            return false;
                        continue;
                    }
                    if (c == '\t' || c == '\n' || c == '\r') 
                    {
                        out.push_back(' ');
                        ++cur;
                        continue;
                    }
                    const char *chunk = cur;
                    while (cur != last && *cur != quote && *cur != '<' && *cur != '&' &&
                           *cur != '\t' && *cur != '\n' && *cur != '\r')
                        ++cur;
                    out.append(chunk, static_cast<std::size_t>(cur - chunk));
                }
            }

            static String &text_target(Xml &out)
            {
                if (out.children_ && !out.children_->empty())
                    return out.children_->back().tail_;
                return out.text_;
            }

            bool parse_element(Xml &out, std::size_t depth)
            {
                if (depth > Xml::kMaxDepth)
                    return fail("profundidade maxima de elementos excedida", cur);
                ++cur; 
                if (!read_name(out.tag_))
                    return false;

                static constexpr std::size_t kLinearAttrs = 8;
                HashSet<String> seen_attrs;
                for (;;)
                {
                    skip_ws();
                    if (cur == last)
                        return fail("tag sem fecho", cur);
                    if (*cur == '/')
                    {
                        ++cur;
                        if (cur == last || *cur != '>')
                            return fail("esperado '>' depois de '/'", cur);
                        ++cur;
                        return true; 
                    }
                    if (*cur == '>')
                    {
                        ++cur;
                        break;
                    }
                    Xml::Attribute a;
                    const char *name_at = cur;
                    if (!read_name(a.name))
                        return false;
                    if (seen_attrs.empty() && out.attrs_.size() < kLinearAttrs)
                    {
                        for (std::size_t k = 0; k < out.attrs_.size(); ++k)
                            if (out.attrs_[k].name == a.name)
                                return fail("atributo repetido", name_at);
                    }
                    else
                    {
                        if (seen_attrs.empty())
                            for (std::size_t k = 0; k < out.attrs_.size(); ++k)
                                seen_attrs.insert(out.attrs_[k].name);
                        if (!seen_attrs.insert(a.name))
                            return fail("atributo repetido", name_at);
                    }
                    skip_ws();
                    if (cur == last || *cur != '=')
                        return fail("esperado '=' no atributo", cur);
                    ++cur;
                    skip_ws();
                    if (cur == last || (*cur != '"' && *cur != '\''))
                        return fail("esperado aspas no valor do atributo", cur);
                    const char quote = *cur;
                    ++cur;
                    if (!parse_attr_value(a.value, quote))
                        return false;
                    out.attrs_.push_back(detail::move(a));
                    if (cur != last && *cur != '>' && *cur != '/' && *cur != ' ' && *cur != '\t' &&
                        *cur != '\n' && *cur != '\r')
                        return fail("esperado espaco entre atributos", cur);
                }

                for (;;)
                {
                    if (cur == last)
                        return fail("elemento sem tag de fecho", cur);
                    if (*cur != '<')
                    {
                        if (!parse_char_data(text_target(out)))
                            return false;
                        continue;
                    }
                    if (starts_with("</", 2))
                    {
                        const char *close_at = cur;
                        cur += 2;
                        String close_name;
                        if (!read_name(close_name))
                            return false;
                        skip_ws();
                        if (cur == last || *cur != '>')
                            return fail("esperado '>' na tag de fecho", cur);
                        ++cur;
                        if (close_name != out.tag_)
                            return fail("tag de fecho nao corresponde a abertura", close_at);

                        if (out.children_)
                        {
                            bool has_text = !xml_is_all_ws(out.text_);
                            for (std::size_t i = 0; i < out.children_->size() && !has_text; ++i)
                                has_text = !xml_is_all_ws((*out.children_)[i].tail_);
                            if (!has_text)
                            {
                                out.text_.clear();
                                for (std::size_t i = 0; i < out.children_->size(); ++i)
                                    (*out.children_)[i].tail_.clear();
                            }
                        }
                        return true;
                    }
                    if (starts_with("<!--", 4))
                    {
                        if (!skip_comment())
                            return false;
                        continue;
                    }
                    if (starts_with("<![CDATA[", 9))
                    {
                        const char *start = cur;
                        cur += 9;
                        const char *data = cur;
                        while (cur != last && !starts_with("]]>", 3))
                            ++cur;
                        if (cur == last)
                            return fail("CDATA sem fecho ']]>'", start);
                        text_target(out).append(data, static_cast<std::size_t>(cur - data));
                        cur += 3;
                        continue;
                    }
                    if (starts_with("<?", 2))
                    {
                        if (!skip_pi())
                            return false;
                        continue;
                    }
                    Xml child;
                    if (!parse_element(child, depth + 1))
                        return false;
                    out.ensure_children();
                    out.children_->push_back(detail::move(child));
                }
            }
        };
    } 

    inline Xml Xml::parse(const char *text, std::size_t len, Error *err)
    {
        if (err)
            *err = Error();
        if (!text)
        {
            if (err)
            {
                err->message = "input nulo";
                err->line = 1;
                err->column = 1;
            }
            return Xml();
        }

        detail::XmlParser ps(text, text + len);
        std::size_t bom = 0;
        if (len >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
            static_cast<unsigned char>(text[1]) == 0xBB &&
            static_cast<unsigned char>(text[2]) == 0xBF)
            bom = 3;
        ps.cur += bom;

        Xml root;
        if (ps.skip_misc())
        {
            if (ps.cur == ps.last || *ps.cur != '<')
                ps.fail("documento sem elemento raiz", ps.cur);
            else if (ps.parse_element(root, 0))
            {
                if (ps.skip_misc() && ps.cur != ps.last)
                    ps.fail("lixo depois do elemento raiz", ps.cur);
            }
        }

        if (ps.err_msg)
        {
            if (err)
            {
                const std::size_t off = static_cast<std::size_t>(ps.err_at - text);
                err->message = ps.err_msg;
                err->offset = off;
                err->line = 1;
                err->column = 1;
                for (std::size_t i = bom; i < off && i < len; ++i)
                {
                    if (text[i] == '\n')
                    {
                        ++err->line;
                        err->column = 1;
                    }
                    else if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
                        ++err->column;
                }
            }
            return Xml();
        }
        return root;
    }

}
