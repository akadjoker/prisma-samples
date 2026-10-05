#pragma once

#include "detail/utils.hpp"
#include "span.hpp"
#include "string.hpp"
#include "vector.hpp"

namespace ct
{
    namespace detail
    {
        namespace re
        {
            constexpr std::size_t npos = static_cast<std::size_t>(-1);

            inline bool is_word(unsigned char c) noexcept
            {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
            }

            inline bool is_digit(unsigned char c) noexcept { return c >= '0' && c <= '9'; }

            inline bool is_space(unsigned char c) noexcept
            {
                return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
            }

            inline int hex_value(unsigned char c) noexcept
            {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                return -1;
            }

            inline std::uint32_t fold(std::uint32_t cp) noexcept
            {
                return (cp >= 'A' && cp <= 'Z') ? cp + 32 : cp;
            }

            inline std::size_t utf8_decode(const char *s, std::size_t n, std::size_t i, std::uint32_t &cp) noexcept
            {
                unsigned char c = static_cast<unsigned char>(s[i]);
                if (c < 0x80)
                {
                    cp = c;
                    return 1;
                }
                std::size_t len;
                std::uint32_t v;
                if ((c & 0xE0) == 0xC0)
                {
                    len = 2;
                    v = c & 0x1F;
                }
                else if ((c & 0xF0) == 0xE0)
                {
                    len = 3;
                    v = c & 0x0F;
                }
                else if ((c & 0xF8) == 0xF0)
                {
                    len = 4;
                    v = c & 0x07;
                }
                else
                {
                    cp = c;
                    return 1;
                }
                if (i + len > n)
                {
                    cp = c;
                    return 1;
                }
                for (std::size_t k = 1; k < len; ++k)
                {
                    unsigned char d = static_cast<unsigned char>(s[i + k]);
                    if ((d & 0xC0) != 0x80)
                    {
                        cp = c;
                        return 1;
                    }
                    v = (v << 6) | (d & 0x3F);
                }
                cp = v;
                return len;
            }

            inline std::size_t utf8_encode(std::uint32_t cp, char out[4]) noexcept
            {
                if (cp < 0x80)
                {
                    out[0] = static_cast<char>(cp);
                    return 1;
                }
                if (cp < 0x800)
                {
                    out[0] = static_cast<char>(0xC0 | (cp >> 6));
                    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
                    return 2;
                }
                if (cp < 0x10000)
                {
                    out[0] = static_cast<char>(0xE0 | (cp >> 12));
                    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
                    return 3;
                }
                out[0] = static_cast<char>(0xF0 | (cp >> 18));
                out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out[3] = static_cast<char>(0x80 | (cp & 0x3F));
                return 4;
            }

            inline bool utf8_step_back(const char *s, std::size_t pos, std::size_t count, std::size_t &out) noexcept
            {
                while (count > 0)
                {
                    if (pos == 0)
                        return false;
                    --pos;
                    std::size_t guard = 0;
                    while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80 && guard < 3)
                    {
                        --pos;
                        ++guard;
                    }
                    --count;
                }
                out = pos;
                return true;
            }

            struct CharClass
            {
                std::uint32_t bits[8];
                Vector<std::uint32_t> ranges;
                bool negated;

                CharClass() : negated(false)
                {
                    for (int i = 0; i < 8; ++i)
                        bits[i] = 0;
                }

                void set_bit(std::uint32_t c) noexcept { bits[c >> 5] |= (1u << (c & 31)); }
                bool bit(std::uint32_t c) const noexcept { return (bits[c >> 5] >> (c & 31)) & 1u; }

                void add(std::uint32_t lo, std::uint32_t hi)
                {
                    if (lo > hi)
                        return;
                    for (std::uint32_t c = lo; c <= hi && c < 256; ++c)
                        set_bit(c);
                    if (hi >= 256)
                    {
                        ranges.push_back(lo < 256 ? 256 : lo);
                        ranges.push_back(hi);
                    }
                }

                void add_digit()
                {
                    add('0', '9');
                }
                void add_word()
                {
                    add('a', 'z');
                    add('A', 'Z');
                    add('0', '9');
                    add('_', '_');
                }
                void add_space()
                {
                    add(' ', ' ');
                    add('\t', '\r');
                }
                void add_not_digit()
                {
                    add(0, '0' - 1);
                    add('9' + 1, 0x10FFFF);
                }
                void add_not_word()
                {
                    for (std::uint32_t c = 0; c < 128; ++c)
                        if (!is_word(static_cast<unsigned char>(c)))
                            set_bit(c);
                    add(128, 0x10FFFF);
                }
                void add_not_space()
                {
                    for (std::uint32_t c = 0; c < 128; ++c)
                        if (!is_space(static_cast<unsigned char>(c)))
                            set_bit(c);
                    add(128, 0x10FFFF);
                }

                void fold_case()
                {
                    for (std::uint32_t c = 'a'; c <= 'z'; ++c)
                    {
                        if (bit(c) || bit(c - 32))
                        {
                            set_bit(c);
                            set_bit(c - 32);
                        }
                    }
                }

                bool contains(std::uint32_t cp) const noexcept
                {
                    bool in;
                    if (cp < 256)
                        in = bit(cp);
                    else
                    {
                        in = false;
                        for (std::size_t i = 0; i < ranges.size(); i += 2)
                        {
                            if (cp >= ranges[i] && cp <= ranges[i + 1])
                            {
                                in = true;
                                break;
                            }
                        }
                    }
                    return in != negated;
                }

                bool may_match_high() const noexcept
                {
                    if (negated || !ranges.empty())
                        return true;
                    for (int i = 4; i < 8; ++i)
                        if (bits[i])
                            return true;
                    return false;
                }
            };

            enum NodeKind : std::uint8_t
            {
                N_Empty,
                N_Char,
                N_Any,
                N_Class,
                N_Concat,
                N_Alt,
                N_Repeat,
                N_Group,
                N_Backref,
                N_Assert,
                N_Look,
                N_Atomic
            };

            enum AssertKind : std::uint8_t
            {
                As_Bol,
                As_BolMulti,
                As_Eol,
                As_EolMulti,
                As_Begin,
                As_End,
                As_WordB,
                As_NotWordB
            };

            enum LookKind : std::uint8_t
            {
                L_Ahead = 0,
                L_NegAhead = 1,
                L_Behind = 2,
                L_NegBehind = 3
            };

            enum RepeatMode : std::uint8_t
            {
                R_Greedy,
                R_Lazy,
                R_Possessive
            };

            struct Node
            {
                std::uint8_t kind;
                std::uint8_t flag;
                std::int32_t a;
                std::int32_t b;
                std::int32_t child;
                std::int32_t next;
            };

            enum Op : std::uint8_t
            {
                Op_Char,
                Op_CharFold,
                Op_Any,
                Op_AnyNoNL,
                Op_Class,
                Op_Split,
                Op_Jmp,
                Op_Save,
                Op_Match,
                Op_Assert,
                Op_Backref,
                Op_RepInit,
                Op_RepEnter,
                Op_RepNext,
                Op_LookStart,
                Op_LookEnd,
                Op_AtomicStart,
                Op_AtomicEnd
            };

            struct Inst
            {
                std::uint8_t op;
                std::uint8_t arg;
                std::uint16_t split;
                std::int32_t x;
                std::int32_t y;
            };

            struct RepInfo
            {
                std::int32_t min;
                std::int32_t max;
                bool lazy;
            };

            struct Program
            {
                Vector<Inst> code;
                Vector<CharClass> classes;
                Vector<RepInfo> reps;
                Vector<String> names;
                std::size_t ngroups;
                std::size_t nsplits;
                bool memo_ok;
                bool anchored;
                std::size_t min_len;
                bool use_first;
                int first_single;
                std::uint32_t first[8];

                Program() : ngroups(0), nsplits(0), memo_ok(false), anchored(false), min_len(0), use_first(false), first_single(-1)
                {
                    for (int i = 0; i < 8; ++i)
                        first[i] = 0;
                }

                bool first_has(unsigned char c) const noexcept { return (first[c >> 5] >> (c & 31)) & 1u; }
            };

            enum Flag : unsigned
            {
                F_IgnoreCase = 1,
                F_Multiline = 2,
                F_DotAll = 4,
                F_Verbose = 8,
                F_Ascii = 16
            };

            class Parser
            {
            public:
                Parser(const char *p, std::size_t n, unsigned flags, Program &prog, Vector<Node> &nodes)
                    : p_(p), n_(n), i_(0), flags_(flags), prog_(prog), nodes_(nodes),
                      err_(nullptr), err_off_(0), depth_(0), root_(-1)
                {
                    prog_.names.push_back(String());
                }

                bool parse()
                {
                    root_ = parse_alt();
                    if (err_)
                        return false;
                    if (i_ < n_)
                    {
                        if (p_[i_] == ')')
                            fail("unbalanced parenthesis", i_);
                        else
                            fail("unexpected character", i_);
                        return false;
                    }
                    return true;
                }

                int root() const noexcept { return root_; }
                unsigned flags() const noexcept { return flags_; }
                const char *error() const noexcept { return err_; }
                std::size_t error_offset() const noexcept { return err_off_; }

            private:
                const char *p_;
                std::size_t n_;
                std::size_t i_;
                unsigned flags_;
                Program &prog_;
                Vector<Node> &nodes_;
                const char *err_;
                std::size_t err_off_;
                int depth_;
                int root_;
                Vector<int> open_;

                static constexpr int kMaxDepth = 200;
                static constexpr std::size_t kMaxGroups = 1 << 15;

                bool at_end() const noexcept { return i_ >= n_; }
                char peek() const noexcept { return p_[i_]; }
                bool eat(char c) noexcept
                {
                    if (i_ < n_ && p_[i_] == c)
                    {
                        ++i_;
                        return true;
                    }
                    return false;
                }

                int fail(const char *msg, std::size_t off)
                {
                    if (!err_)
                    {
                        err_ = msg;
                        err_off_ = off;
                    }
                    return -1;
                }

                int node(std::uint8_t kind, std::int32_t a = 0, std::int32_t b = 0, std::uint8_t flag = 0)
                {
                    Node nd;
                    nd.kind = kind;
                    nd.flag = flag;
                    nd.a = a;
                    nd.b = b;
                    nd.child = -1;
                    nd.next = -1;
                    nodes_.push_back(nd);
                    return static_cast<int>(nodes_.size() - 1);
                }

                void skip_verbose()
                {
                    if (!(flags_ & F_Verbose))
                        return;
                    while (!at_end())
                    {
                        char c = peek();
                        if (is_space(static_cast<unsigned char>(c)))
                            ++i_;
                        else if (c == '#')
                        {
                            while (!at_end() && peek() != '\n')
                                ++i_;
                        }
                        else
                            break;
                    }
                }

                int parse_alt()
                {
                    if (++depth_ > kMaxDepth)
                        return fail("pattern nesting too deep", i_);
                    int first = parse_concat();
                    if (first < 0)
                        return -1;
                    if (!eat('|'))
                    {
                        --depth_;
                        return first;
                    }
                    int alt = node(N_Alt);
                    nodes_[alt].child = first;
                    int last = first;
                    do
                    {
                        int c = parse_concat();
                        if (c < 0)
                            return -1;
                        nodes_[last].next = c;
                        last = c;
                    } while (eat('|'));
                    --depth_;
                    const int merged = merge_single_char_alt(alt);
                    return merged >= 0 ? merged : alt;
                }

                int merge_single_char_alt(int alt)
                {
                    CharClass merged;
                    for (int c = nodes_[alt].child; c >= 0; c = nodes_[c].next)
                    {
                        if (nodes_[c].kind != N_Concat)
                            return -1;
                        const int atom = nodes_[c].child;
                        if (atom < 0 || nodes_[atom].next >= 0)
                            return -1;
                        const Node &nd = nodes_[atom];
                        if (nd.kind == N_Char)
                        {
                            if (nd.a < 0 || nd.a >= 128)
                                return -1;
                            CharClass one;
                            one.add(static_cast<std::uint32_t>(nd.a), static_cast<std::uint32_t>(nd.a));
                            if (nd.flag)
                                one.fold_case();
                            merge_class(merged, one);
                        }
                        else if (nd.kind == N_Class)
                        {
                            const CharClass &cls = prog_.classes[static_cast<std::size_t>(nd.a)];
                            if (cls.negated)
                                return -1;
                            merge_class(merged, cls);
                        }
                        else
                            return -1;
                    }
                    prog_.classes.push_back(detail::move(merged));
                    return node(N_Class, static_cast<std::int32_t>(prog_.classes.size() - 1));
                }

                static void merge_class(CharClass &into, const CharClass &from)
                {
                    for (int i = 0; i < 8; ++i)
                        into.bits[i] |= from.bits[i];
                    for (std::size_t i = 0; i < from.ranges.size(); ++i)
                        into.ranges.push_back(from.ranges[i]);
                }

                int parse_concat()
                {
                    int cat = node(N_Concat);
                    int last = -1;
                    for (;;)
                    {
                        skip_verbose();
                        if (at_end() || peek() == ')' || peek() == '|')
                            break;
                        int a = parse_quantified();
                        if (a < 0)
                            return -1;
                        if (last < 0)
                            nodes_[cat].child = a;
                        else
                            nodes_[last].next = a;
                        last = a;
                    }
                    return cat;
                }

                bool parse_braces(std::int32_t &min, std::int32_t &max)
                {
                    std::size_t save = i_;
                    ++i_;
                    if (!at_end() && peek() == '}')
                    {
                        i_ = save;
                        return false;
                    }
                    long long lo = -1, hi = -1;
                    bool has_comma = false;
                    bool too_large = false;
                    if (!at_end() && is_digit(static_cast<unsigned char>(peek())))
                    {
                        lo = 0;
                        while (!at_end() && is_digit(static_cast<unsigned char>(peek())))
                        {
                            if (lo <= 0x7FFFFFFF)
                                lo = lo * 10 + (peek() - '0');
                            if (lo > 0x7FFFFFFF)
                                too_large = true;
                            ++i_;
                        }
                    }
                    if (eat(','))
                    {
                        has_comma = true;
                        if (!at_end() && is_digit(static_cast<unsigned char>(peek())))
                        {
                            hi = 0;
                            while (!at_end() && is_digit(static_cast<unsigned char>(peek())))
                            {
                                if (hi <= 0x7FFFFFFF)
                                    hi = hi * 10 + (peek() - '0');
                                if (hi > 0x7FFFFFFF)
                                    too_large = true;
                                ++i_;
                            }
                        }
                    }
                    else
                        hi = lo;
                    if (!eat('}'))
                    {
                        i_ = save;
                        return false;
                    }
                    if (too_large)
                    {
                        fail("the repetition number is too large", save);
                        return false;
                    }
                    min = lo < 0 ? 0 : static_cast<std::int32_t>(lo);
                    max = has_comma ? (hi < 0 ? -1 : static_cast<std::int32_t>(hi)) : min;
                    return true;
                }

                int parse_quantified()
                {
                    std::size_t atom_off = i_;
                    int a = parse_atom();
                    if (a < 0)
                        return -1;
                    for (;;)
                    {
                        skip_verbose();
                        if (at_end())
                            break;
                        std::size_t qoff = i_;
                        char c = peek();
                        std::int32_t min, max;
                        if (c == '*')
                        {
                            min = 0;
                            max = -1;
                            ++i_;
                        }
                        else if (c == '+')
                        {
                            min = 1;
                            max = -1;
                            ++i_;
                        }
                        else if (c == '?')
                        {
                            min = 0;
                            max = 1;
                            ++i_;
                        }
                        else if (c == '{')
                        {
                            if (!parse_braces(min, max))
                            {
                                if (err_)
                                    return -1;
                                break;
                            }
                            if (max >= 0 && max < min)
                                return fail("min repeat greater than max repeat", qoff);
                        }
                        else
                            break;
                        std::uint8_t kind = nodes_[a].kind;
                        if (kind == N_Assert || kind == N_Empty)
                            return fail("nothing to repeat", atom_off);
                        if (kind == N_Repeat)
                            return fail("multiple repeat", qoff);
                        std::uint8_t mode = R_Greedy;
                        if (eat('?'))
                            mode = R_Lazy;
                        else if (eat('+'))
                            mode = R_Possessive;
                        int r = node(N_Repeat, min, max, mode);
                        nodes_[r].child = a;
                        a = r;
                    }
                    return a;
                }

                int parse_atom()
                {
                    std::size_t off = i_;
                    char c = peek();
                    switch (c)
                    {
                    case '(':
                        ++i_;
                        return parse_group(off);
                    case '[':
                        ++i_;
                        return parse_class(off);
                    case '.':
                        ++i_;
                        return node(N_Any, 0, 0, (flags_ & F_DotAll) ? 1 : 0);
                    case '^':
                        ++i_;
                        return node(N_Assert, (flags_ & F_Multiline) ? As_BolMulti : As_Bol);
                    case '$':
                        ++i_;
                        return node(N_Assert, (flags_ & F_Multiline) ? As_EolMulti : As_Eol);
                    case '\\':
                        ++i_;
                        return parse_escape(off);
                    case '*':
                    case '+':
                    case '?':
                        return fail("nothing to repeat", off);
                    case '{':
                    {
                        std::int32_t min, max;
                        if (parse_braces(min, max))
                            return fail("nothing to repeat", off);
                        ++i_;
                        return literal('{');
                    }
                    default:
                    {
                        std::uint32_t cp;
                        i_ += utf8_decode(p_, n_, i_, cp);
                        return literal(cp);
                    }
                    }
                }

                int literal(std::uint32_t cp)
                {
                    return node(N_Char, static_cast<std::int32_t>(cp), 0, (flags_ & F_IgnoreCase) ? 1 : 0);
                }

                bool parse_name(String &out, std::size_t off)
                {
                    std::size_t start = i_;
                    while (!at_end() && peek() != '>' && peek() != ')')
                        ++i_;
                    if (at_end())
                    {
                        fail("missing >, unterminated name", off);
                        return false;
                    }
                    if (i_ == start)
                    {
                        fail("missing group name", off);
                        return false;
                    }
                    for (std::size_t k = start; k < i_; ++k)
                    {
                        unsigned char ch = static_cast<unsigned char>(p_[k]);
                        bool ok = is_word(ch) || ch >= 0x80;
                        if (!ok || (k == start && is_digit(ch)))
                        {
                            fail("bad character in group name", start);
                            return false;
                        }
                    }
                    out = String(p_ + start, i_ - start);
                    return true;
                }

                int find_group(const String &name) const
                {
                    for (std::size_t g = 1; g < prog_.names.size(); ++g)
                        if (prog_.names[g] == name)
                            return static_cast<int>(g);
                    return -1;
                }

                int open_group(String name, std::size_t off)
                {
                    if (prog_.ngroups + 1 >= kMaxGroups)
                        return fail("too many groups", off);
                    if (!name.empty() && find_group(name) >= 0)
                        return fail("redefinition of group name", off);
                    ++prog_.ngroups;
                    prog_.names.push_back(detail::move(name));
                    int idx = static_cast<int>(prog_.ngroups);
                    open_.push_back(idx);
                    return idx;
                }

                int finish_group(int idx, std::size_t off)
                {
                    int body = parse_alt();
                    if (body < 0)
                        return -1;
                    if (!eat(')'))
                        return fail("missing ), unterminated subpattern", off);
                    open_.pop_back();
                    int g = node(N_Group, idx);
                    nodes_[g].child = body;
                    return g;
                }

                int finish_wrapped(std::uint8_t kind, std::int32_t a, std::size_t off)
                {
                    int body = parse_alt();
                    if (body < 0)
                        return -1;
                    if (!eat(')'))
                        return fail("missing ), unterminated subpattern", off);
                    if (kind == N_Look && (a == L_Behind || a == L_NegBehind))
                    {
                        int w = width(body);
                        if (w < 0)
                            return fail("look-behind requires fixed-width pattern", off);
                        int n = node(kind, a, w);
                        nodes_[n].child = body;
                        return n;
                    }
                    int n = node(kind, a);
                    nodes_[n].child = body;
                    return n;
                }

                int parse_group(std::size_t off)
                {
                    if (!eat('?'))
                    {
                        int idx = open_group(String(), off);
                        if (idx < 0)
                            return -1;
                        return finish_group(idx, off);
                    }
                    if (at_end())
                        return fail("unexpected end of pattern", i_);
                    char c = p_[i_++];
                    switch (c)
                    {
                    case ':':
                        return finish_wrapped(N_Group, -1, off);
                    case 'P':
                    {
                        if (eat('<'))
                        {
                            String name;
                            if (!parse_name(name, off))
                                return -1;
                            if (!eat('>'))
                                return fail("missing >, unterminated name", off);
                            int idx = open_group(detail::move(name), off);
                            if (idx < 0)
                                return -1;
                            return finish_group(idx, off);
                        }
                        if (eat('='))
                        {
                            String name;
                            if (!parse_name(name, off))
                                return -1;
                            if (!eat(')'))
                                return fail("missing ), unterminated name", off);
                            int g = find_group(name);
                            if (g < 0)
                                return fail("unknown group name", off);
                            return backref(g, off);
                        }
                        return fail("unknown extension ?P", off);
                    }
                    case '=':
                        return finish_wrapped(N_Look, L_Ahead, off);
                    case '!':
                        return finish_wrapped(N_Look, L_NegAhead, off);
                    case '<':
                        if (eat('='))
                            return finish_wrapped(N_Look, L_Behind, off);
                        if (eat('!'))
                            return finish_wrapped(N_Look, L_NegBehind, off);
                        return fail("unknown extension ?<", off);
                    case '>':
                        return finish_wrapped(N_Atomic, 0, off);
                    case '#':
                        while (!at_end() && peek() != ')')
                            ++i_;
                        if (!eat(')'))
                            return fail("missing ), unterminated comment", off);
                        return node(N_Empty);
                    default:
                        --i_;
                        return parse_flags(off);
                    }
                }

                static unsigned flag_bit(char c) noexcept
                {
                    switch (c)
                    {
                    case 'i':
                        return F_IgnoreCase;
                    case 'm':
                        return F_Multiline;
                    case 's':
                        return F_DotAll;
                    case 'x':
                        return F_Verbose;
                    case 'a':
                    case 'u':
                        return F_Ascii;
                    default:
                        return 0;
                    }
                }

                int parse_flags(std::size_t off)
                {
                    unsigned on = 0, offb = 0;
                    bool minus = false;
                    for (;;)
                    {
                        if (at_end())
                            return fail("missing -, : or )", i_);
                        char c = peek();
                        if (c == 'L')
                            return fail("bad inline flags: cannot use 'L' flag with a str pattern", i_);
                        unsigned b = flag_bit(c);
                        if (b)
                        {
                            if (minus)
                            {
                                if (b == F_Ascii)
                                    return fail("bad inline flags: cannot turn off flags 'a', 'u' and 'L'", i_);
                                offb |= b;
                            }
                            else
                                on |= b;
                            ++i_;
                            continue;
                        }
                        if (c == '-')
                        {
                            if (minus)
                                return fail("bad inline flags: flag turned on and off", i_);
                            minus = true;
                            ++i_;
                            if (at_end())
                                return fail("missing flag", i_);
                            if (!flag_bit(peek()))
                                return fail("missing flag", i_);
                            continue;
                        }
                        if (c == ':')
                        {
                            ++i_;
                            break;
                        }
                        if (c == ')')
                        {
                            ++i_;
                            if (minus)
                                return fail("bad inline flags: cannot turn off flags globally", off);
                            if (off != 0)
                                return fail("global flags not at the start of the expression", off);
                            if (on == 0)
                                return fail("missing flag", off);
                            flags_ |= on;
                            return node(N_Empty);
                        }
                        if (on == 0 && offb == 0 && !minus)
                            return fail("unknown extension", off);
                        return fail("missing -, : or )", i_);
                    }
                    if (on & offb)
                        return fail("bad inline flags: flag turned on and off", off);
                    unsigned saved = flags_;
                    flags_ = (flags_ | on) & ~offb;
                    int body = parse_alt();
                    if (body < 0)
                        return -1;
                    flags_ = saved;
                    if (!eat(')'))
                        return fail("missing ), unterminated subpattern", off);
                    int n = node(N_Group, -1);
                    nodes_[n].child = body;
                    return n;
                }

                int backref(int g, std::size_t off)
                {
                    for (std::size_t k = 0; k < open_.size(); ++k)
                        if (open_[k] == g)
                            return fail("cannot refer to an open group", off);
                    return node(N_Backref, g, 0, (flags_ & F_IgnoreCase) ? 1 : 0);
                }

                bool read_hex(int count, std::uint32_t &out, std::size_t off, const char *what)
                {
                    out = 0;
                    for (int k = 0; k < count; ++k)
                    {
                        if (at_end() || hex_value(static_cast<unsigned char>(peek())) < 0)
                        {
                            fail(what, off);
                            return false;
                        }
                        out = (out << 4) | static_cast<std::uint32_t>(hex_value(static_cast<unsigned char>(peek())));
                        ++i_;
                    }
                    return true;
                }

                enum EscKind
                {
                    E_Char,
                    E_Class,
                    E_Node
                };

                struct Esc
                {
                    EscKind kind;
                    std::uint32_t cp;
                    char cls;
                    int node;
                };

                bool parse_escape_body(bool in_class, Esc &e, std::size_t off)
                {
                    if (at_end())
                    {
                        fail("bad escape (end of pattern)", off);
                        return false;
                    }
                    char c = p_[i_++];
                    e.kind = E_Char;
                    e.node = -1;
                    switch (c)
                    {
                    case 'n':
                        e.cp = '\n';
                        return true;
                    case 't':
                        e.cp = '\t';
                        return true;
                    case 'r':
                        e.cp = '\r';
                        return true;
                    case 'f':
                        e.cp = '\f';
                        return true;
                    case 'v':
                        e.cp = '\v';
                        return true;
                    case 'a':
                        e.cp = '\a';
                        return true;
                    case 'x':
                        return read_hex(2, e.cp, off, "incomplete escape \\x");
                    case 'u':
                        return read_hex(4, e.cp, off, "incomplete escape \\u");
                    case 'U':
                        if (!read_hex(8, e.cp, off, "incomplete escape \\U"))
                            return false;
                        if (e.cp > 0x10FFFF)
                        {
                            fail("bad escape \\U", off);
                            return false;
                        }
                        return true;
                    case 'd':
                    case 'D':
                    case 's':
                    case 'S':
                    case 'w':
                    case 'W':
                        e.kind = E_Class;
                        e.cls = c;
                        return true;
                    case 'b':
                        if (in_class)
                        {
                            e.cp = 8;
                            return true;
                        }
                        e.kind = E_Node;
                        e.node = node(N_Assert, As_WordB);
                        return true;
                    case 'B':
                        if (in_class)
                            break;
                        e.kind = E_Node;
                        e.node = node(N_Assert, As_NotWordB);
                        return true;
                    case 'A':
                        if (in_class)
                            break;
                        e.kind = E_Node;
                        e.node = node(N_Assert, As_Begin);
                        return true;
                    case 'Z':
                        if (in_class)
                            break;
                        e.kind = E_Node;
                        e.node = node(N_Assert, As_End);
                        return true;
                    case '0':
                    {
                        std::uint32_t v = 0;
                        int k = 0;
                        while (k < 2 && !at_end() && peek() >= '0' && peek() <= '7')
                        {
                            v = v * 8 + static_cast<std::uint32_t>(peek() - '0');
                            ++i_;
                            ++k;
                        }
                        e.cp = v;
                        return true;
                    }
                    default:
                        break;
                    }
                    if (c >= '1' && c <= '9')
                    {
                        std::size_t start = i_ - 1;
                        if (!in_class && start + 2 < n_ && p_[start + 1] >= '0' && p_[start + 1] <= '7' &&
                            p_[start + 2] >= '0' && p_[start + 2] <= '7' && c <= '7')
                        {
                            std::uint32_t v = static_cast<std::uint32_t>((c - '0') * 64 + (p_[start + 1] - '0') * 8 + (p_[start + 2] - '0'));
                            if (v <= 0377)
                            {
                                i_ = start + 3;
                                e.cp = v;
                                return true;
                            }
                        }
                        if (in_class)
                        {
                            if (c > '7')
                            {
                                fail("bad escape", off);
                                return false;
                            }
                            std::uint32_t v = static_cast<std::uint32_t>(c - '0');
                            int k = 1;
                            while (k < 3 && !at_end() && peek() >= '0' && peek() <= '7')
                            {
                                v = v * 8 + static_cast<std::uint32_t>(peek() - '0');
                                ++i_;
                                ++k;
                            }
                            if (v > 0377)
                            {
                                fail("octal escape value outside of range 0-0o377", off);
                                return false;
                            }
                            e.cp = v;
                            return true;
                        }
                        int g = c - '0';
                        if (!at_end() && is_digit(static_cast<unsigned char>(peek())))
                        {
                            g = g * 10 + (peek() - '0');
                            ++i_;
                        }
                        if (g > static_cast<int>(prog_.ngroups))
                        {
                            fail("invalid group reference", off);
                            return false;
                        }
                        e.kind = E_Node;
                        e.node = backref(g, off);
                        return e.node >= 0;
                    }
                    unsigned char uc = static_cast<unsigned char>(c);
                    if (is_word(uc))
                    {
                        fail("bad escape", off);
                        return false;
                    }
                    if (uc >= 0x80)
                    {
                        --i_;
                        i_ += utf8_decode(p_, n_, i_, e.cp);
                        return true;
                    }
                    e.cp = uc;
                    return true;
                }

                int parse_escape(std::size_t off)
                {
                    Esc e;
                    if (!parse_escape_body(false, e, off))
                        return -1;
                    if (e.kind == E_Char)
                        return literal(e.cp);
                    if (e.kind == E_Node)
                        return e.node;
                    CharClass cls;
                    add_class_escape(cls, e.cls);
                    prog_.classes.push_back(detail::move(cls));
                    return node(N_Class, static_cast<std::int32_t>(prog_.classes.size() - 1));
                }

                static void add_class_escape(CharClass &cls, char c)
                {
                    switch (c)
                    {
                    case 'd':
                        cls.add_digit();
                        break;
                    case 'D':
                        cls.add_not_digit();
                        break;
                    case 'w':
                        cls.add_word();
                        break;
                    case 'W':
                        cls.add_not_word();
                        break;
                    case 's':
                        cls.add_space();
                        break;
                    case 'S':
                        cls.add_not_space();
                        break;
                    default:
                        break;
                    }
                }

                bool class_item(Esc &e, std::size_t off)
                {
                    if (peek() == '\\')
                    {
                        ++i_;
                        return parse_escape_body(true, e, off);
                    }
                    e.kind = E_Char;
                    i_ += utf8_decode(p_, n_, i_, e.cp);
                    return true;
                }

                int parse_class(std::size_t off)
                {
                    CharClass cls;
                    cls.negated = eat('^');
                    bool first = true;
                    for (;;)
                    {
                        if (at_end())
                            return fail("unterminated character set", off);
                        if (peek() == ']' && !first)
                        {
                            ++i_;
                            break;
                        }
                        first = false;
                        std::size_t item_off = i_;
                        Esc lo;
                        if (!class_item(lo, item_off))
                            return -1;
                        if (lo.kind == E_Class)
                        {
                            add_class_escape(cls, lo.cls);
                            if (!at_end() && peek() == '-' && i_ + 1 < n_ && p_[i_ + 1] != ']')
                                return fail("bad character range", item_off);
                            continue;
                        }
                        if (!at_end() && peek() == '-' && i_ + 1 < n_ && p_[i_ + 1] != ']')
                        {
                            ++i_;
                            Esc hi;
                            if (!class_item(hi, i_))
                                return -1;
                            if (hi.kind == E_Class || hi.cp < lo.cp)
                                return fail("bad character range", item_off);
                            cls.add(lo.cp, hi.cp);
                            continue;
                        }
                        cls.add(lo.cp, lo.cp);
                    }
                    if (flags_ & F_IgnoreCase)
                        cls.fold_case();
                    prog_.classes.push_back(detail::move(cls));
                    return node(N_Class, static_cast<std::int32_t>(prog_.classes.size() - 1));
                }

                int width(int ni) const
                {
                    const Node &nd = nodes_[ni];
                    switch (nd.kind)
                    {
                    case N_Empty:
                    case N_Assert:
                    case N_Look:
                        return 0;
                    case N_Char:
                    case N_Any:
                    case N_Class:
                        return 1;
                    case N_Concat:
                    {
                        int total = 0;
                        for (int c = nd.child; c >= 0; c = nodes_[c].next)
                        {
                            int w = width(c);
                            if (w < 0)
                                return -1;
                            const long long sum = static_cast<long long>(total) + w;
                            total = sum > 0x7FFFFFFFll ? 0x7FFFFFFF : static_cast<int>(sum);
                        }
                        return total;
                    }
                    case N_Alt:
                    {
                        int w = -2;
                        for (int c = nd.child; c >= 0; c = nodes_[c].next)
                        {
                            int cw = width(c);
                            if (cw < 0)
                                return -1;
                            if (w == -2)
                                w = cw;
                            else if (w != cw)
                                return -1;
                        }
                        return w < 0 ? 0 : w;
                    }
                    case N_Repeat:
                    {
                        if (nd.a != nd.b)
                            return -1;
                        int w = width(nd.child);
                        if (w < 0)
                            return -1;
                        const long long product = static_cast<long long>(w) * nd.a;
                        return product > 0x7FFFFFFFll ? 0x7FFFFFFF : static_cast<int>(product);
                    }
                    case N_Group:
                    case N_Atomic:
                        return width(nd.child);
                    case N_Backref:
                    default:
                        return -1;
                    }
                }
            };

            class Compiler
            {
            public:
                Compiler(Program &prog, const Vector<Node> &nodes)
                    : prog_(prog), nodes_(nodes), sizes_(nodes.size(), -1)
                {
                }

                void compile(int root)
                {
                    gen(root);
                    emit(Op_Match);
                    analyze(root);
                }

            private:
                Program &prog_;
                const Vector<Node> &nodes_;
                Vector<int> sizes_;

                int pc() const noexcept { return static_cast<int>(prog_.code.size()); }

                int emit(std::uint8_t op, std::int32_t x = 0, std::int32_t y = 0, std::uint8_t arg = 0)
                {
                    Inst in;
                    in.op = op;
                    in.arg = arg;
                    in.split = 0;
                    in.x = x;
                    in.y = y;
                    prog_.code.push_back(in);
                    return pc() - 1;
                }

                bool can_be_empty(int ni) const { return min_len(ni) == 0; }

                std::size_t min_len(int ni) const
                {
                    const Node &nd = nodes_[ni];
                    switch (nd.kind)
                    {
                    case N_Char:
                    {
                        char tmp[4];
                        return utf8_encode(static_cast<std::uint32_t>(nd.a), tmp);
                    }
                    case N_Any:
                    case N_Class:
                        return 1;
                    case N_Concat:
                    {
                        std::size_t t = 0;
                        for (int c = nd.child; c >= 0; c = nodes_[c].next)
                            t += min_len(c);
                        return t;
                    }
                    case N_Alt:
                    {
                        std::size_t m = npos;
                        for (int c = nd.child; c >= 0; c = nodes_[c].next)
                        {
                            std::size_t v = min_len(c);
                            if (v < m)
                                m = v;
                        }
                        return m == npos ? 0 : m;
                    }
                    case N_Repeat:
                        return min_len(nd.child) * static_cast<std::size_t>(nd.a);
                    case N_Group:
                    case N_Atomic:
                        return min_len(nd.child);
                    default:
                        return 0;
                    }
                }

                void gen(int ni)
                {
                    const int before = pc();
                    gen_node(ni);
                    sizes_[static_cast<std::size_t>(ni)] = pc() - before;
                }

                void gen_node(int ni)
                {
                    const Node &nd = nodes_[ni];
                    switch (nd.kind)
                    {
                    case N_Empty:
                        break;
                    case N_Char:
                    {
                        std::uint32_t cp = static_cast<std::uint32_t>(nd.a);
                        if (nd.flag && cp < 128 && fold(cp) != cp)
                            emit(Op_CharFold, static_cast<std::int32_t>(fold(cp)));
                        else if (nd.flag && cp >= 'a' && cp <= 'z')
                            emit(Op_CharFold, static_cast<std::int32_t>(cp));
                        else
                        {
                            char buf[4];
                            std::size_t len = utf8_encode(cp, buf);
                            for (std::size_t k = 0; k < len; ++k)
                                emit(Op_Char, static_cast<unsigned char>(buf[k]));
                        }
                        break;
                    }
                    case N_Any:
                        emit(nd.flag ? Op_Any : Op_AnyNoNL);
                        break;
                    case N_Class:
                        emit(Op_Class, nd.a);
                        break;
                    case N_Concat:
                        for (int c = nd.child; c >= 0; c = nodes_[c].next)
                            gen(c);
                        break;
                    case N_Alt:
                    {
                        Vector<int> jumps;
                        for (int c = nd.child; c >= 0; c = nodes_[c].next)
                        {
                            if (nodes_[c].next < 0)
                            {
                                gen(c);
                                break;
                            }
                            int split = emit(Op_Split);
                            prog_.code[split].x = pc();
                            gen(c);
                            jumps.push_back(emit(Op_Jmp));
                            prog_.code[split].y = pc();
                        }
                        for (std::size_t k = 0; k < jumps.size(); ++k)
                            prog_.code[jumps[k]].x = pc();
                        break;
                    }
                    case N_Group:
                        if (nd.a < 0)
                        {
                            gen(nd.child);
                            break;
                        }
                        emit(Op_Save, nd.a * 2);
                        gen(nd.child);
                        emit(Op_Save, nd.a * 2 + 1);
                        break;
                    case N_Repeat:
                        gen_repeat(nd);
                        break;
                    case N_Backref:
                        emit(Op_Backref, nd.a, 0, nd.flag);
                        break;
                    case N_Assert:
                        emit(Op_Assert, nd.a);
                        break;
                    case N_Look:
                    {
                        int start = emit(Op_LookStart, nd.b, 0, static_cast<std::uint8_t>(nd.a));
                        gen(nd.child);
                        emit(Op_LookEnd);
                        prog_.code[start].y = pc();
                        break;
                    }
                    case N_Atomic:
                        emit(Op_AtomicStart);
                        gen(nd.child);
                        emit(Op_AtomicEnd);
                        break;
                    default:
                        break;
                    }
                }

                void gen_repeat(const Node &nd)
                {
                    bool possessive = nd.flag == R_Possessive;
                    bool lazy = nd.flag == R_Lazy;
                    if (possessive)
                        emit(Op_AtomicStart);
                    std::int32_t min = nd.a, max = nd.b;
                    if (max == 0)
                    {
                    }
                    else if (min == 0 && max == 1)
                    {
                        int split = emit(Op_Split);
                        int body = pc();
                        gen(nd.child);
                        int exit = pc();
                        prog_.code[split].x = lazy ? exit : body;
                        prog_.code[split].y = lazy ? body : exit;
                    }
                    else if (max < 0 && min <= 1 && !can_be_empty(nd.child))
                    {
                        if (min == 1)
                        {
                            int body = pc();
                            gen(nd.child);
                            int split = emit(Op_Split);
                            int exit = pc();
                            prog_.code[split].x = lazy ? exit : body;
                            prog_.code[split].y = lazy ? body : exit;
                        }
                        else
                        {
                            int split = emit(Op_Split);
                            int body = pc();
                            gen(nd.child);
                            emit(Op_Jmp, split);
                            int exit = pc();
                            prog_.code[split].x = lazy ? exit : body;
                            prog_.code[split].y = lazy ? body : exit;
                        }
                    }
                    else if (!can_be_empty(nd.child) && unroll(nd.child, min, max, lazy))
                    {
                    }
                    else
                    {
                        RepInfo info;
                        info.min = min;
                        info.max = max;
                        info.lazy = lazy;
                        prog_.reps.push_back(info);
                        std::int32_t r = static_cast<std::int32_t>(prog_.reps.size() - 1);
                        emit(Op_RepInit, r);
                        int enter = emit(Op_RepEnter, r);
                        gen(nd.child);
                        emit(Op_RepNext, r, enter);
                        prog_.code[enter].y = pc();
                    }
                    if (possessive)
                        emit(Op_AtomicEnd);
                }

                static constexpr int kMaxUnroll = 2048;

                bool unroll(int child, std::int32_t min, std::int32_t max, bool lazy)
                {
                    int before = pc();
                    std::size_t reps_before = prog_.reps.size();
                    long copies = max < 0 ? (min > 1 ? min : 1) : max;
                    const int known = sizes_[static_cast<std::size_t>(child)];
                    if (known >= 0 && copies * known > kMaxUnroll)
                        return false;
                    gen(child);
                    int body = pc() - before;
                    if (copies * body > kMaxUnroll)
                    {
                        prog_.code.resize(static_cast<std::size_t>(before));
                        prog_.reps.resize(reps_before);
                        return false;
                    }
                    for (std::int32_t k = 1; k < min; ++k)
                        gen(child);
                    if (max < 0)
                    {
                        if (min == 0)
                        {
                            prog_.code.resize(static_cast<std::size_t>(before));
                            prog_.reps.resize(reps_before);
                            return false;
                        }
                        int loop = pc() - body;
                        int split = emit(Op_Split);
                        int exit = pc();
                        prog_.code[split].x = lazy ? exit : loop;
                        prog_.code[split].y = lazy ? loop : exit;
                        return true;
                    }
                    if (min == 0)
                    {
                        prog_.code.resize(static_cast<std::size_t>(before));
                        prog_.reps.resize(reps_before);
                    }
                    Vector<int> splits;
                    for (std::int32_t k = min; k < max; ++k)
                    {
                        splits.push_back(emit(Op_Split));
                        gen(child);
                    }
                    int exit = pc();
                    for (std::size_t k = 0; k < splits.size(); ++k)
                    {
                        prog_.code[splits[k]].x = lazy ? exit : splits[k] + 1;
                        prog_.code[splits[k]].y = lazy ? splits[k] + 1 : exit;
                    }
                    return true;
                }

                bool anchored(int ni) const
                {
                    const Node &nd = nodes_[ni];
                    switch (nd.kind)
                    {
                    case N_Assert:
                        return nd.a == As_Begin || nd.a == As_Bol;
                    case N_Concat:
                        return nd.child >= 0 && anchored(nd.child);
                    case N_Alt:
                        for (int c = nd.child; c >= 0; c = nodes_[c].next)
                            if (!anchored(c))
                                return false;
                        return true;
                    case N_Group:
                    case N_Atomic:
                        return anchored(nd.child);
                    case N_Repeat:
                        return nd.a >= 1 && anchored(nd.child);
                    default:
                        return false;
                    }
                }

                static void set_first(std::uint32_t set[8], unsigned char c) noexcept { set[c >> 5] |= (1u << (c & 31)); }
                static void set_all(std::uint32_t set[8]) noexcept
                {
                    for (int i = 0; i < 8; ++i)
                        set[i] = 0xFFFFFFFFu;
                }

                bool first_set(int ni, std::uint32_t set[8]) const
                {
                    const Node &nd = nodes_[ni];
                    switch (nd.kind)
                    {
                    case N_Char:
                    {
                        std::uint32_t cp = static_cast<std::uint32_t>(nd.a);
                        char buf[4];
                        utf8_encode(cp, buf);
                        set_first(set, static_cast<unsigned char>(buf[0]));
                        if (nd.flag && cp < 128)
                        {
                            if (cp >= 'a' && cp <= 'z')
                                set_first(set, static_cast<unsigned char>(cp - 32));
                            else if (cp >= 'A' && cp <= 'Z')
                                set_first(set, static_cast<unsigned char>(cp + 32));
                        }
                        return false;
                    }
                    case N_Any:
                        set_all(set);
                        return false;
                    case N_Class:
                    {
                        const CharClass &cls = prog_.classes[static_cast<std::size_t>(nd.a)];
                        for (std::uint32_t c = 0; c < 128; ++c)
                            if (cls.contains(c))
                                set_first(set, static_cast<unsigned char>(c));
                        if (cls.may_match_high())
                            for (std::uint32_t c = 128; c < 256; ++c)
                                set_first(set, static_cast<unsigned char>(c));
                        return false;
                    }
                    case N_Concat:
                        for (int c = nd.child; c >= 0; c = nodes_[c].next)
                            if (!first_set(c, set))
                                return false;
                        return true;
                    case N_Alt:
                    {
                        bool e = false;
                        for (int c = nd.child; c >= 0; c = nodes_[c].next)
                            e = first_set(c, set) || e;
                        return e;
                    }
                    case N_Repeat:
                    {
                        if (nd.b == 0)
                            return true;
                        bool e = first_set(nd.child, set);
                        return e || nd.a == 0;
                    }
                    case N_Group:
                    case N_Atomic:
                        return first_set(nd.child, set);
                    case N_Backref:
                        set_all(set);
                        return true;
                    default:
                        return true;
                    }
                }

                void analyze(int root)
                {
                    prog_.memo_ok = true;
                    std::size_t nsplits = 0;
                    for (std::size_t i = 0; i < prog_.code.size(); ++i)
                    {
                        Inst &in = prog_.code[i];
                        if (in.op == Op_Split)
                            in.split = static_cast<std::uint16_t>(nsplits++);
                        else if (in.op == Op_Backref || in.op == Op_RepInit || in.op == Op_LookStart || in.op == Op_AtomicStart)
                            prog_.memo_ok = false;
                    }
                    prog_.nsplits = nsplits;
                    if (nsplits > 0xFFFF)
                        prog_.memo_ok = false;
                    prog_.min_len = min_len(root);
                    prog_.anchored = anchored(root);
                    std::uint32_t set[8] = {0, 0, 0, 0, 0, 0, 0, 0};
                    bool empty = first_set(root, set);
                    int count = 0;
                    int single = -1;
                    for (int c = 0; c < 256; ++c)
                    {
                        if ((set[c >> 5] >> (c & 31)) & 1u)
                        {
                            ++count;
                            single = c;
                        }
                    }
                    prog_.use_first = !empty && count > 0 && count < 256;
                    prog_.first_single = (prog_.use_first && count == 1) ? single : -1;
                    for (int i = 0; i < 8; ++i)
                        prog_.first[i] = set[i];
                }
            };

            enum FrameKind : std::uint32_t
            {
                Fr_Alt,
                Fr_UndoCap,
                Fr_UndoCnt,
                Fr_UndoLast,
                Fr_Mark,
                Fr_Look,
                Fr_AltRun
            };

            struct Frame
            {
                std::uint32_t kind;
                std::int32_t pc;
                std::size_t pos;
                std::size_t val;
            };

            class Runner
            {
            public:
                static constexpr std::size_t kMaxMemoBits = std::size_t(1) << 28;

                Runner(const Program &prog, StringView text)
                    : prog_(prog), s_(text.data()), n_(text.size()), memo_ready_(false), memo_on_(false),
                      steps_(0), threshold_(0), stride_(text.size() + 1), caps_dirty_(false)
                {
                    caps_.resize((prog.ngroups + 1) * 2, npos);
                    cnt_.resize(prog.reps.size(), 0);
                    last_.resize(prog.reps.size(), npos);
                    stack_.reserve(64);
                    if (prog.memo_ok && prog.nsplits > 0)
                    {
                        std::size_t bits = prog.nsplits * stride_;
                        memo_ready_ = bits / stride_ == prog.nsplits && bits <= kMaxMemoBits;
                        threshold_ = 64 + bits / 512;
                    }
                }

                void enable_memo()
                {
                    memo_.resize(((prog_.nsplits * stride_) + 63) / 64, 0);
                    memo_on_ = true;
                }

                void reset_memo()
                {
                    for (std::size_t k = 0; k < touched_.size(); ++k)
                        memo_[touched_[k]] = 0;
                    touched_.clear();
                }

                const Vector<std::size_t> &caps() const noexcept { return caps_; }

                bool exec(std::size_t start, bool match_all, bool must_advance, std::size_t search_start)
                {
                    stack_.clear();
                    if (caps_dirty_)
                    {
                        for (std::size_t k = 0; k < caps_.size(); ++k)
                            caps_[k] = npos;
                        caps_dirty_ = false;
                    }
                    const Inst *code = prog_.code.data();
                    std::int32_t pc = 0;
                    std::size_t pos = start;
                    for (;;)
                    {
                        const Inst &in = code[pc];
                        switch (in.op)
                        {
                        case Op_Char:
                            if (pos < n_ && static_cast<unsigned char>(s_[pos]) == static_cast<std::uint32_t>(in.x))
                            {
                                ++pos;
                                ++pc;
                                continue;
                            }
                            break;
                        case Op_CharFold:
                            if (pos < n_ && fold(static_cast<unsigned char>(s_[pos])) == static_cast<std::uint32_t>(in.x))
                            {
                                ++pos;
                                ++pc;
                                continue;
                            }
                            break;
                        case Op_Any:
                            if (pos < n_)
                            {
                                std::uint32_t cp;
                                pos += utf8_decode(s_, n_, pos, cp);
                                ++pc;
                                continue;
                            }
                            break;
                        case Op_AnyNoNL:
                            if (pos < n_ && s_[pos] != '\n')
                            {
                                std::uint32_t cp;
                                pos += utf8_decode(s_, n_, pos, cp);
                                ++pc;
                                continue;
                            }
                            break;
                        case Op_Class:
                            if (pos < n_)
                            {
                                std::uint32_t cp;
                                std::size_t len = utf8_decode(s_, n_, pos, cp);
                                if (prog_.classes[static_cast<std::size_t>(in.x)].contains(cp))
                                {
                                    pos += len;
                                    ++pc;
                                    continue;
                                }
                            }
                            break;
                        case Op_Split:
                            if (!memo_on_ && memo_ready_ && ++steps_ > threshold_)
                                enable_memo();
                            if (memo_on_)
                            {
                                std::size_t bit = static_cast<std::size_t>(in.split) * stride_ + pos;
                                std::size_t w = bit >> 6;
                                std::uint64_t mask = std::uint64_t(1) << (bit & 63);
                                if (memo_[w] & mask)
                                    break;
                                if (memo_[w] == 0)
                                    touched_.push_back(static_cast<std::uint32_t>(w));
                                memo_[w] |= mask;
                            }
                            push_alt(in.y, pos);
                            pc = in.x;
                            continue;
                        case Op_Jmp:
                            pc = in.x;
                            continue;
                        case Op_Save:
                            push(Fr_UndoCap, in.x, 0, caps_[static_cast<std::size_t>(in.x)]);
                            caps_[static_cast<std::size_t>(in.x)] = pos;
                            ++pc;
                            continue;
                        case Op_Assert:
                            if (check_assert(in.x, pos))
                            {
                                ++pc;
                                continue;
                            }
                            break;
                        case Op_Backref:
                        {
                            std::size_t a = caps_[static_cast<std::size_t>(in.x) * 2];
                            std::size_t b = caps_[static_cast<std::size_t>(in.x) * 2 + 1];
                            if (a == npos || b == npos)
                                break;
                            std::size_t len = b - a;
                            if (pos + len > n_)
                                break;
                            bool ok = true;
                            if (in.arg)
                            {
                                for (std::size_t k = 0; k < len; ++k)
                                    if (fold(static_cast<unsigned char>(s_[a + k])) != fold(static_cast<unsigned char>(s_[pos + k])))
                                    {
                                        ok = false;
                                        break;
                                    }
                            }
                            else
                                ok = std::memcmp(s_ + a, s_ + pos, len) == 0;
                            if (ok)
                            {
                                pos += len;
                                ++pc;
                                continue;
                            }
                            break;
                        }
                        case Op_RepInit:
                        {
                            std::size_t r = static_cast<std::size_t>(in.x);
                            push(Fr_UndoCnt, in.x, 0, cnt_[r]);
                            cnt_[r] = 0;
                            push(Fr_UndoLast, in.x, 0, last_[r]);
                            last_[r] = npos;
                            ++pc;
                            continue;
                        }
                        case Op_RepEnter:
                        {
                            std::size_t r = static_cast<std::size_t>(in.x);
                            const RepInfo &info = prog_.reps[r];
                            std::size_t c = cnt_[r];
                            if (c >= static_cast<std::size_t>(info.min))
                            {
                                if ((info.max >= 0 && c >= static_cast<std::size_t>(info.max)) || pos == last_[r])
                                {
                                    pc = in.y;
                                    continue;
                                }
                            }
                            if (c < static_cast<std::size_t>(info.min))
                            {
                                ++pc;
                                continue;
                            }
                            push(Fr_UndoLast, in.x, 0, last_[r]);
                            last_[r] = pos;
                            if (info.lazy)
                            {
                                push(Fr_Alt, pc + 1, pos, 0);
                                pc = in.y;
                                continue;
                            }
                            push(Fr_Alt, in.y, pos, 0);
                            ++pc;
                            continue;
                        }
                        case Op_RepNext:
                        {
                            std::size_t r = static_cast<std::size_t>(in.x);
                            push(Fr_UndoCnt, in.x, 0, cnt_[r]);
                            ++cnt_[r];
                            pc = in.y;
                            continue;
                        }
                        case Op_AtomicStart:
                            push(Fr_Mark, 0, 0, 0);
                            ++pc;
                            continue;
                        case Op_AtomicEnd:
                        {
                            std::size_t m = find_frame(Fr_Mark);
                            compact(m);
                            ++pc;
                            continue;
                        }
                        case Op_LookStart:
                        {
                            std::uint8_t kind = in.arg;
                            std::size_t p = pos;
                            if (kind >= L_Behind)
                            {
                                if (!utf8_step_back(s_, pos, static_cast<std::size_t>(in.x), p))
                                {
                                    if (kind & 1)
                                    {
                                        pc = in.y;
                                        continue;
                                    }
                                    break;
                                }
                            }
                            push(Fr_Look, in.y, pos, kind);
                            pos = p;
                            ++pc;
                            continue;
                        }
                        case Op_LookEnd:
                        {
                            std::size_t m = find_frame(Fr_Look);
                            Frame f = stack_[m];
                            bool behind = f.val >= L_Behind;
                            bool neg = (f.val & 1) != 0;
                            if (behind && pos != f.pos)
                                break;
                            if (!neg)
                            {
                                compact(m);
                                pos = f.pos;
                                pc = f.pc;
                                continue;
                            }
                            unwind(m);
                            break;
                        }
                        case Op_Match:
                            if (match_all && pos != n_)
                                break;
                            if (must_advance && pos == search_start)
                                break;
                            caps_[0] = start;
                            caps_[1] = pos;
                            caps_dirty_ = true;
                            return true;
                        default:
                            break;
                        }
                        bool resumed = false;
                        while (!stack_.empty())
                        {
                            Frame f = stack_.back();
                            stack_.pop_back();
                            if (f.kind == Fr_Alt)
                            {
                                pc = f.pc;
                                pos = f.pos;
                                resumed = true;
                                break;
                            }
                            if (f.kind == Fr_AltRun)
                            {
                                pc = f.pc;
                                pos = f.pos;
                                const std::size_t len = f.val & 7;
                                const std::size_t count = f.val >> 3;
                                f.pos -= len;
                                if (count > 2)
                                    f.val -= 8;
                                else
                                {
                                    f.kind = Fr_Alt;
                                    f.val = 0;
                                }
                                stack_.push_back(f);
                                resumed = true;
                                break;
                            }
                            if (f.kind == Fr_UndoCap)
                                caps_[static_cast<std::size_t>(f.pc)] = f.val;
                            else if (f.kind == Fr_UndoCnt)
                                cnt_[static_cast<std::size_t>(f.pc)] = f.val;
                            else if (f.kind == Fr_UndoLast)
                                last_[static_cast<std::size_t>(f.pc)] = f.val;
                            else if (f.kind == Fr_Look && (f.val & 1))
                            {
                                pc = f.pc;
                                pos = f.pos;
                                resumed = true;
                                break;
                            }
                        }
                        if (!resumed)
                        {
#ifdef CT_RE_CHECK_CAPS
                            for (std::size_t k = 0; k < caps_.size(); ++k)
                                if (caps_[k] != npos)
                                    __builtin_trap();
#endif
                            return false;
                        }
                    }
                }

                bool search(std::size_t from, bool must_advance, std::size_t &ms, std::size_t &me)
                {
                    if (from > n_)
                        return false;
                    if (memo_on_)
                        reset_memo();
                    if (prog_.anchored)
                    {
                        if (from != 0)
                            return false;
                        if (exec(0, false, must_advance, from))
                        {
                            ms = caps_[0];
                            me = caps_[1];
                            return true;
                        }
                        return false;
                    }
                    std::size_t st = from;
                    for (;;)
                    {
                        if (n_ - st < prog_.min_len)
                            return false;
                        if (prog_.use_first)
                        {
                            if (prog_.first_single >= 0)
                            {
                                const void *p = std::memchr(s_ + st, prog_.first_single, n_ - st);
                                if (!p)
                                    return false;
                                st = static_cast<std::size_t>(static_cast<const char *>(p) - s_);
                            }
                            else
                            {
                                while (st < n_ && !prog_.first_has(static_cast<unsigned char>(s_[st])))
                                    st += unit_length(st);
                                if (st >= n_)
                                    return false;
                            }
                            if (n_ - st < prog_.min_len)
                                return false;
                        }
                        if (exec(st, false, must_advance, from))
                        {
                            ms = caps_[0];
                            me = caps_[1];
                            return true;
                        }
                        if (st >= n_)
                            return false;
                        st += unit_length(st);
                    }
                }

            private:
                std::size_t unit_length(std::size_t at) const noexcept
                {
                    std::uint32_t cp;
                    return utf8_decode(s_, n_, at, cp);
                }

                const Program &prog_;
                const char *s_;
                std::size_t n_;
                Vector<Frame> stack_;
                Vector<std::size_t> caps_;
                Vector<std::size_t> cnt_;
                Vector<std::size_t> last_;
                Vector<std::uint64_t> memo_;
                Vector<std::uint32_t> touched_;
                bool memo_ready_;
                bool memo_on_;
                std::size_t steps_;
                std::size_t threshold_;
                std::size_t stride_;
                bool caps_dirty_;

                void push_alt(std::int32_t pc, std::size_t pos)
                {
                    if (!stack_.empty())
                    {
                        Frame &top = stack_.back();
                        if (top.pc == pc && top.pos < pos && pos - top.pos <= 4)
                        {
                            const std::size_t len = pos - top.pos;
                            if (top.kind == Fr_AltRun && (top.val & 7) == len)
                            {
                                top.pos = pos;
                                top.val += 8;
                                return;
                            }
                            if (top.kind == Fr_Alt)
                            {
                                top.kind = Fr_AltRun;
                                top.pos = pos;
                                top.val = (std::size_t(2) << 3) | len;
                                return;
                            }
                        }
                    }
                    push(Fr_Alt, pc, pos, 0);
                }

                void push(std::uint32_t kind, std::int32_t pc, std::size_t pos, std::size_t val)
                {
                    Frame f;
                    f.kind = kind;
                    f.pc = pc;
                    f.pos = pos;
                    f.val = val;
                    stack_.push_back(f);
                }

                std::size_t find_frame(std::uint32_t kind) const
                {
                    std::size_t i = stack_.size();
                    while (i > 0)
                    {
                        --i;
                        if (stack_[i].kind == kind)
                            return i;
                    }
                    detail::fatal("ct::Regex: pilha de backtracking corrompida");
                }

                static bool is_undo(std::uint32_t kind) noexcept
                {
                    return kind == Fr_UndoCap || kind == Fr_UndoCnt || kind == Fr_UndoLast;
                }

                void compact(std::size_t m)
                {
                    std::size_t j = m;
                    for (std::size_t i = m + 1; i < stack_.size(); ++i)
                        if (is_undo(stack_[i].kind))
                            stack_[j++] = stack_[i];
                    stack_.resize(j);
                }

                void unwind(std::size_t m)
                {
                    while (stack_.size() > m)
                    {
                        Frame f = stack_.back();
                        stack_.pop_back();
                        if (f.kind == Fr_UndoCap)
                            caps_[static_cast<std::size_t>(f.pc)] = f.val;
                        else if (f.kind == Fr_UndoCnt)
                            cnt_[static_cast<std::size_t>(f.pc)] = f.val;
                        else if (f.kind == Fr_UndoLast)
                            last_[static_cast<std::size_t>(f.pc)] = f.val;
                    }
                }

                bool check_assert(std::int32_t kind, std::size_t pos) const noexcept
                {
                    switch (kind)
                    {
                    case As_Bol:
                    case As_Begin:
                        return pos == 0;
                    case As_BolMulti:
                        return pos == 0 || s_[pos - 1] == '\n';
                    case As_Eol:
                        return pos == n_ || (pos + 1 == n_ && s_[pos] == '\n');
                    case As_EolMulti:
                        return pos == n_ || s_[pos] == '\n';
                    case As_End:
                        return pos == n_;
                    case As_WordB:
                    case As_NotWordB:
                    {
                        if (kind == As_NotWordB && n_ == 0)
                            return false;
                        bool a = pos > 0 && is_word(static_cast<unsigned char>(s_[pos - 1]));
                        bool b = pos < n_ && is_word(static_cast<unsigned char>(s_[pos]));
                        return (a != b) == (kind == As_WordB);
                    }
                    default:
                        return false;
                    }
                }
            };
        }
    }

    class Regex;

    class Match
    {
    public:
        static constexpr std::size_t npos = static_cast<std::size_t>(-1);

        Match() noexcept : re_(nullptr) {}

        bool valid() const noexcept { return re_ != nullptr; }
        const Regex *regex() const noexcept { return re_; }
        StringView text() const noexcept { return text_; }
        std::size_t groups() const noexcept { return spans_.empty() ? 0 : spans_.size() / 2 - 1; }

        bool matched(std::size_t g = 0) const
        {
            check(g);
            return spans_[2 * g] != npos;
        }
        std::size_t start(std::size_t g = 0) const
        {
            check(g);
            return spans_[2 * g];
        }
        std::size_t end(std::size_t g = 0) const
        {
            check(g);
            return spans_[2 * g + 1];
        }
        StringView group(std::size_t g = 0) const
        {
            check(g);
            std::size_t a = spans_[2 * g];
            if (a == npos)
                return StringView();
            return text_.substr(a, spans_[2 * g + 1] - a);
        }

        bool matched(StringView name) const { return matched(index(name)); }
        std::size_t start(StringView name) const { return start(index(name)); }
        std::size_t end(StringView name) const { return end(index(name)); }
        StringView group(StringView name) const { return group(index(name)); }

        StringView operator[](std::size_t g) const { return group(g); }
        StringView operator[](StringView name) const { return group(name); }

    private:
        friend class Regex;
        const Regex *re_;
        StringView text_;
        Vector<std::size_t> spans_;

        void check(std::size_t g) const
        {
            if (!re_ || g > groups())
                detail::fatal("ct::Match: grupo invalido");
        }
        std::size_t index(StringView name) const;
    };

    constexpr std::size_t Match::npos;

    class Regex
    {
    public:
        enum : unsigned
        {
            IgnoreCase = detail::re::F_IgnoreCase,
            Multiline = detail::re::F_Multiline,
            DotAll = detail::re::F_DotAll,
            Verbose = detail::re::F_Verbose,
            Ascii = detail::re::F_Ascii
        };

        static constexpr std::size_t npos = static_cast<std::size_t>(-1);

        struct Error
        {
            const char *message;
            std::size_t offset;

            Error() noexcept : message(nullptr), offset(0) {}
            explicit operator bool() const noexcept { return message != nullptr; }
        };

        Regex() noexcept : flags_(0), valid_(false) {}

        static Regex compile(StringView pattern, unsigned flags = 0, Error *err = nullptr);

        bool valid() const noexcept { return valid_; }
        explicit operator bool() const noexcept { return valid_; }
        const String &pattern() const noexcept { return pattern_; }
        unsigned flags() const noexcept { return flags_; }
        std::size_t group_count() const noexcept { return prog_.ngroups; }

        int group_index(StringView name) const noexcept
        {
            for (std::size_t g = 1; g < prog_.names.size(); ++g)
                if (StringView(prog_.names[g]) == name)
                    return static_cast<int>(g);
            return -1;
        }

        StringView group_name(std::size_t g) const
        {
            if (g >= prog_.names.size())
                detail::fatal("ct::Regex::group_name: grupo invalido");
            return StringView(prog_.names[g]);
        }

        bool match(StringView text, Match *m = nullptr, std::size_t pos = 0) const;
        bool fullmatch(StringView text, Match *m = nullptr, std::size_t pos = 0) const;
        bool search(StringView text, Match *m = nullptr, std::size_t pos = 0) const;

        Vector<Match> finditer(StringView text, std::size_t pos = 0) const;
        Vector<String> findall(StringView text, std::size_t pos = 0) const;
        Vector<String> split(StringView text, std::size_t maxsplit = 0) const;

        String sub(StringView text, StringView repl, std::size_t count = 0) const;

        template <typename F,
                  typename detail::enable_if<!std::is_convertible<F, StringView>::value, int>::type = 0>
        String sub(StringView text, F &&fn, std::size_t count = 0) const
        {
            String out;
            std::size_t last = 0;
            for_each_match(text, 0, count, [&](const Match &m) {
                out.append(text.data() + last, m.start() - last);
                String r = fn(m);
                out.append(r);
                last = m.end();
            });
            out.append(text.data() + last, text.size() - last);
            return out;
        }

        static String escape(StringView s);

    private:
        String pattern_;
        unsigned flags_;
        bool valid_;
        detail::re::Program prog_;

        void require_valid() const
        {
            if (!valid_)
                detail::fatal("ct::Regex: padrao invalido");
        }

        void fill(Match &m, StringView text, const Vector<std::size_t> &caps) const
        {
            m.re_ = this;
            m.text_ = text;
            m.spans_ = caps;
        }

        bool run_anchored(StringView text, Match *m, std::size_t pos, bool full) const
        {
            require_valid();
            if (pos > text.size())
                pos = text.size();
            detail::re::Runner r(prog_, text);
            if (!r.exec(pos, full, false, pos))
                return false;
            if (m)
                fill(*m, text, r.caps());
            return true;
        }

        template <typename F>
        void for_each_match(StringView text, std::size_t pos, std::size_t count, F &&fn) const
        {
            require_valid();
            detail::re::Runner r(prog_, text);
            std::size_t st = pos < text.size() ? pos : text.size();
            bool must_advance = false;
            std::size_t done = 0;
            Match m;
            while (st <= text.size())
            {
                std::size_t ms, me;
                if (!r.search(st, must_advance, ms, me))
                    break;
                fill(m, text, r.caps());
                fn(static_cast<const Match &>(m));
                ++done;
                if (count && done >= count)
                    break;
                must_advance = (ms == me);
                st = me;
            }
        }

        struct Piece
        {
            int group;
            String lit;
        };

        void parse_template(StringView repl, Vector<Piece> &pieces) const;
    };

    constexpr std::size_t Regex::npos;

    inline std::size_t Match::index(StringView name) const
    {
        if (!re_)
            detail::fatal("ct::Match: grupo invalido");
        int g = re_->group_index(name);
        if (g < 0)
            detail::fatal("ct::Match: nome de grupo desconhecido");
        return static_cast<std::size_t>(g);
    }

    inline Regex Regex::compile(StringView pattern, unsigned flags, Error *err)
    {
        Regex re;
        re.pattern_ = String(pattern.data(), pattern.size());
        re.flags_ = flags;
        if (err)
            *err = Error();
        Vector<detail::re::Node> nodes;
        nodes.reserve(pattern.size() + 4);
        detail::re::Parser parser(pattern.data(), pattern.size(), flags, re.prog_, nodes);
        if (!parser.parse())
        {
            if (err)
            {
                err->message = parser.error();
                err->offset = parser.error_offset();
            }
            re.prog_ = detail::re::Program();
            return re;
        }
        re.flags_ = parser.flags();
        detail::re::Compiler compiler(re.prog_, nodes);
        compiler.compile(parser.root());
        re.valid_ = true;
        return re;
    }

    inline bool Regex::match(StringView text, Match *m, std::size_t pos) const
    {
        return run_anchored(text, m, pos, false);
    }

    inline bool Regex::fullmatch(StringView text, Match *m, std::size_t pos) const
    {
        return run_anchored(text, m, pos, true);
    }

    inline bool Regex::search(StringView text, Match *m, std::size_t pos) const
    {
        require_valid();
        if (pos > text.size())
            pos = text.size();
        detail::re::Runner r(prog_, text);
        std::size_t ms, me;
        if (!r.search(pos, false, ms, me))
            return false;
        if (m)
            fill(*m, text, r.caps());
        return true;
    }

    inline Vector<Match> Regex::finditer(StringView text, std::size_t pos) const
    {
        Vector<Match> out;
        for_each_match(text, pos, 0, [&](const Match &m) { out.push_back(m); });
        return out;
    }

    inline Vector<String> Regex::findall(StringView text, std::size_t pos) const
    {
        Vector<String> out;
        for_each_match(text, pos, 0, [&](const Match &m) {
            StringView g = m.group(0);
            out.push_back(String(g.data(), g.size()));
        });
        return out;
    }

    inline Vector<String> Regex::split(StringView text, std::size_t maxsplit) const
    {
        Vector<String> out;
        std::size_t last = 0;
        for_each_match(text, 0, maxsplit, [&](const Match &m) {
            out.push_back(String(text.data() + last, m.start() - last));
            for (std::size_t g = 1; g <= m.groups(); ++g)
            {
                StringView s = m.group(g);
                out.push_back(String(s.data(), s.size()));
            }
            last = m.end();
        });
        out.push_back(String(text.data() + last, text.size() - last));
        return out;
    }

    inline void Regex::parse_template(StringView repl, Vector<Piece> &pieces) const
    {
        Piece cur;
        cur.group = -1;
        const char *p = repl.data();
        std::size_t n = repl.size();
        std::size_t i = 0;
        auto flush = [&]() {
            if (!cur.lit.empty())
            {
                pieces.push_back(cur);
                cur.lit.clear();
            }
        };
        while (i < n)
        {
            char c = p[i++];
            if (c != '\\')
            {
                cur.lit.push_back(c);
                continue;
            }
            if (i >= n)
                detail::fatal("ct::Regex::sub: bad escape (end of pattern)");
            char e = p[i++];
            if (e >= '0' && e <= '9')
            {
                int g = e - '0';
                if (e == '0')
                {
                    std::uint32_t v = 0;
                    int k = 0;
                    while (k < 2 && i < n && p[i] >= '0' && p[i] <= '7')
                    {
                        v = v * 8 + static_cast<std::uint32_t>(p[i] - '0');
                        ++i;
                        ++k;
                    }
                    cur.lit.push_back(static_cast<char>(v));
                    continue;
                }
                if (i < n && p[i] >= '0' && p[i] <= '9')
                {
                    g = g * 10 + (p[i] - '0');
                    ++i;
                }
                if (g > static_cast<int>(prog_.ngroups))
                    detail::fatal("ct::Regex::sub: invalid group reference");
                flush();
                Piece gp;
                gp.group = g;
                pieces.push_back(gp);
                continue;
            }
            if (e == 'g')
            {
                if (i >= n || p[i] != '<')
                    detail::fatal("ct::Regex::sub: missing <");
                ++i;
                std::size_t start = i;
                while (i < n && p[i] != '>')
                    ++i;
                if (i >= n)
                    detail::fatal("ct::Regex::sub: missing >, unterminated name");
                StringView name(p + start, i - start);
                ++i;
                if (name.empty())
                    detail::fatal("ct::Regex::sub: missing group name");
                int g;
                bool numeric = true;
                for (std::size_t k = 0; k < name.size(); ++k)
                    if (!detail::re::is_digit(static_cast<unsigned char>(name[k])))
                        numeric = false;
                if (numeric)
                {
                    g = 0;
                    for (std::size_t k = 0; k < name.size(); ++k)
                    {
                        g = g * 10 + (name[k] - '0');
                        if (g > static_cast<int>(prog_.ngroups))
                            detail::fatal("ct::Regex::sub: invalid group reference");
                    }
                }
                else
                {
                    g = group_index(name);
                    if (g < 0)
                        detail::fatal("ct::Regex::sub: unknown group name");
                }
                flush();
                Piece gp;
                gp.group = g;
                pieces.push_back(gp);
                continue;
            }
            char out;
            switch (e)
            {
            case 'n':
                out = '\n';
                break;
            case 't':
                out = '\t';
                break;
            case 'r':
                out = '\r';
                break;
            case 'f':
                out = '\f';
                break;
            case 'v':
                out = '\v';
                break;
            case 'a':
                out = '\a';
                break;
            case 'b':
                out = '\b';
                break;
            case '\\':
                out = '\\';
                break;
            default:
                if (detail::re::is_word(static_cast<unsigned char>(e)))
                    detail::fatal("ct::Regex::sub: bad escape");
                cur.lit.push_back('\\');
                out = e;
                break;
            }
            cur.lit.push_back(out);
        }
        flush();
    }

    inline String Regex::sub(StringView text, StringView repl, std::size_t count) const
    {
        require_valid();
        Vector<Piece> pieces;
        parse_template(repl, pieces);
        String out;
        std::size_t last = 0;
        for_each_match(text, 0, count, [&](const Match &m) {
            out.append(text.data() + last, m.start() - last);
            for (std::size_t k = 0; k < pieces.size(); ++k)
            {
                const Piece &pc = pieces[k];
                if (pc.group < 0)
                    out.append(pc.lit);
                else
                {
                    StringView g = m.group(static_cast<std::size_t>(pc.group));
                    out.append(g.data(), g.size());
                }
            }
            last = m.end();
        });
        out.append(text.data() + last, text.size() - last);
        return out;
    }

    inline String Regex::escape(StringView s)
    {
        String out;
        out.reserve(s.size() + 8);
        for (std::size_t i = 0; i < s.size(); ++i)
        {
            char c = s[i];
            switch (c)
            {
            case '(':
            case ')':
            case '[':
            case ']':
            case '{':
            case '}':
            case '?':
            case '*':
            case '+':
            case '-':
            case '|':
            case '^':
            case '$':
            case '\\':
            case '.':
            case '&':
            case '~':
            case '#':
            case ' ':
            case '\t':
            case '\n':
            case '\r':
            case '\v':
            case '\f':
                out.push_back('\\');
                break;
            default:
                break;
            }
            out.push_back(c);
        }
        return out;
    }
}
