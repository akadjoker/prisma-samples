#pragma once

#include <cmath>   
#include <cfloat>  
#include <clocale> 

#include "detail/utils.hpp"
#include "string.hpp"
#include "vector.hpp"

namespace ct
{
    namespace detail
    {

        inline bool json_is_digit(char c) { return c >= '0' && c <= '9'; }

        inline double json_pow10(int e)
        {
            static const double kTable[23] = {
                1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
            return kTable[e];
        }

        inline char json_decimal_point()
        {
            const char *dp = std::localeconv()->decimal_point;
            return (dp && *dp) ? *dp : '.';
        }

        inline double json_strtod_locale(const char *p, std::size_t n)
        {
            char small[64];
            char *buf = small;
            String big;
            if (n + 1 > sizeof(small))
            {
                big.resize(n + 1);
                buf = big.data();
            }
            std::memcpy(buf, p, n);
            buf[n] = '\0';
            const char dp = json_decimal_point();
            if (dp != '.')
                for (std::size_t i = 0; i < n; ++i)
                    if (buf[i] == '.')
                        buf[i] = dp;
            return std::strtod(buf, nullptr);
        }

        inline double json_to_double(const char *p, const char *end)
        {
            const char *const start = p;
            bool neg = false;
            if (p != end && (*p == '-' || *p == '+'))
                neg = (*p++ == '-');

            std::uint64_t mant = 0;
            int digits = 0;   
            int exp10 = 0;    
            bool truncated = false;

            for (; p != end && json_is_digit(*p); ++p)
            {
                if (digits < 19)
                {
                    mant = mant * 10 + static_cast<unsigned>(*p - '0');
                    if (mant != 0)
                        ++digits;
                }
                else
                {
                    ++exp10; 
                    truncated = true;
                }
            }
            if (p != end && *p == '.')
            {
                ++p;
                for (; p != end && json_is_digit(*p); ++p)
                {
                    if (digits < 19)
                    {
                        mant = mant * 10 + static_cast<unsigned>(*p - '0');
                        if (mant != 0)
                            ++digits;
                        --exp10;
                    }
                    else
                        truncated = true;
                }
            }
            if (p != end && (*p == 'e' || *p == 'E'))
            {
                ++p;
                bool eneg = false;
                if (p != end && (*p == '+' || *p == '-'))
                    eneg = (*p++ == '-');
                int e = 0;
                for (; p != end && json_is_digit(*p); ++p)
                    if (e < 100000)
                        e = e * 10 + (*p - '0');
                exp10 += eneg ? -e : e;
            }

            if (!truncated && mant <= (std::uint64_t(1) << 53) && exp10 >= -22 && exp10 <= 22)
            {
                double d = static_cast<double>(mant);
                d = exp10 >= 0 ? d * json_pow10(exp10) : d / json_pow10(-exp10);
                return neg ? -d : d;
            }
            return json_strtod_locale(start, static_cast<std::size_t>(end - start));
        }

        inline long double json_pow10l(int e)
        {
            static const long double kSmall[10] = {1e0L, 1e1L, 1e2L, 1e3L, 1e4L,
                                                   1e5L, 1e6L, 1e7L, 1e8L, 1e9L};
            static const long double kBig[36] = {
                1e0L,   1e10L,  1e20L,  1e30L,  1e40L,  1e50L,  1e60L,  1e70L,  1e80L,
                1e90L,  1e100L, 1e110L, 1e120L, 1e130L, 1e140L, 1e150L, 1e160L, 1e170L,
                1e180L, 1e190L, 1e200L, 1e210L, 1e220L, 1e230L, 1e240L, 1e250L, 1e260L,
                1e270L, 1e280L, 1e290L, 1e300L, 1e310L, 1e320L, 1e330L, 1e340L, 1e350L};
            return kSmall[e % 10] * kBig[e / 10];
        }

        inline int json_format_digits(char *buf, bool neg, std::uint64_t d, int digits,
                                      int k, int prec)
        {
            char tmp[24];
            for (int i = digits - 1; i >= 0; --i)
            {
                tmp[i] = static_cast<char>('0' + d % 10);
                d /= 10;
            }
            int nd = digits;
            while (nd > 1 && tmp[nd - 1] == '0')
                --nd;

            int p = 0;
            if (neg)
                buf[p++] = '-';
            if (k < -4 || k >= prec)
            {
                buf[p++] = tmp[0];
                if (nd > 1)
                {
                    buf[p++] = '.';
                    for (int i = 1; i < nd; ++i)
                        buf[p++] = tmp[i];
                }
                buf[p++] = 'e';
                int e = k;
                if (e < 0)
                {
                    buf[p++] = '-';
                    e = -e;
                }
                else
                    buf[p++] = '+';
                if (e >= 100)
                {
                    buf[p++] = static_cast<char>('0' + e / 100);
                    e %= 100;
                }
                buf[p++] = static_cast<char>('0' + e / 10);
                buf[p++] = static_cast<char>('0' + e % 10);
            }
            else if (k >= 0)
            {
                for (int i = 0; i <= k; ++i)
                    buf[p++] = (i < nd) ? tmp[i] : '0';
                if (nd > k + 1)
                {
                    buf[p++] = '.';
                    for (int i = k + 1; i < nd; ++i)
                        buf[p++] = tmp[i];
                }
            }
            else
            {
                buf[p++] = '0';
                buf[p++] = '.';
                for (int i = 0; i < -k - 1; ++i)
                    buf[p++] = '0';
                for (int i = 0; i < nd; ++i)
                    buf[p++] = tmp[i];
            }
            buf[p] = '\0';
            return p;
        }

#if defined(LDBL_MANT_DIG) && LDBL_MANT_DIG >= 64
#define CT_JSON_FAST_DTOA 1

        inline int json_dtoa_fast(char *buf, double v)
        {
            const bool neg = v < 0;
            const double a = neg ? -v : v;

            int k = static_cast<int>(std::floor(std::log10(a)));
            if (k < -324 || k > 308)
                return -1;

            long double scaled;
            for (int tentativa = 0; tentativa < 3; ++tentativa)
            {
                const int shift = 16 - k;
                scaled = (shift >= 0) ? static_cast<long double>(a) * json_pow10l(shift)
                                      : static_cast<long double>(a) / json_pow10l(-shift);
                if (scaled >= 1e16L && scaled < 1e17L)
                    break;
                k += (scaled >= 1e17L) ? 1 : -1; 
                if (tentativa == 2)
                    return -1;
            }

            std::uint64_t n = static_cast<std::uint64_t>(scaled + 0.5L);
            if (n >= 100000000000000000ull) 
            {
                n /= 10;
                ++k;
            }
            if (n < 10000000000000000ull)
                return -1;

            static const std::uint64_t kDiv[3] = {100, 10, 1}; 
            for (int i = 0; i < 3; ++i)
            {
                const int digits = 15 + i;
                std::uint64_t d = n;
                int ke = k;
                if (kDiv[i] > 1)
                    d = (n + kDiv[i] / 2) / kDiv[i];
                std::uint64_t limite = 1;
                for (int j = 0; j < digits; ++j)
                    limite *= 10;
                if (d >= limite) 
                {
                    d /= 10;
                    ++ke;
                }
                const int len = json_format_digits(buf, neg, d, digits, ke, digits);
                if (json_to_double(buf, buf + len) == v)
                    return len;
            }
            return -1;
        }
#endif

        inline void json_append_double(String &out, double v)
        {
            std::uint64_t bits;
            std::memcpy(&bits, &v, sizeof(bits));
            const bool neg = (bits >> 63) != 0;

            if (v == 0.0) 
            {
                if (neg)
                    out.append("-0.0", 4);
                else
                    out.append("0.0", 3);
                return;
            }

            if (v > -1e15 && v < 1e15)
            {
                const std::int64_t i = static_cast<std::int64_t>(v);
                if (static_cast<double>(i) == v)
                {
                    out.append_number(static_cast<long long>(i));
                    out.append(".0", 2);
                    return;
                }
            }

            char buf[48];
            int n = 0;
#if defined(CT_JSON_FAST_DTOA)
            n = json_dtoa_fast(buf, v);
            if (n > 0)
            {
                out.append(buf, static_cast<std::size_t>(n));
                bool tem_marca = false;
                for (int k = 0; k < n; ++k)
                    if (buf[k] == '.' || buf[k] == 'e')
                        tem_marca = true;
                if (!tem_marca)
                    out.append(".0", 2);
                return;
            }
            n = 0;
#endif
            const char dp = json_decimal_point();
            for (int prec = 15; prec <= 17; ++prec)
            {
                n = std::snprintf(buf, sizeof(buf), "%.*g", prec, v);
                if (n <= 0 || static_cast<std::size_t>(n) >= sizeof(buf))
                {
                    n = std::snprintf(buf, sizeof(buf), "%.17g", v);
                    if (n <= 0)
                    {
                        out.append("0.0", 3);
                        return;
                    }
                    if (static_cast<std::size_t>(n) >= sizeof(buf))
                        n = static_cast<int>(sizeof(buf)) - 1;
                    break;
                }
                if (dp != '.')
                    for (int k = 0; k < n; ++k)
                        if (buf[k] == dp)
                            buf[k] = '.';
                if (json_to_double(buf, buf + n) == v)
                    break;
            }
            if (dp != '.')
                for (int k = 0; k < n; ++k)
                    if (buf[k] == dp)
                        buf[k] = '.';

            bool marcado = false;
            for (int k = 0; k < n; ++k)
                if (buf[k] == '.' || buf[k] == 'e' || buf[k] == 'E' || buf[k] == 'n' ||
                    buf[k] == 'i')
                    marcado = true;
            out.append(buf, static_cast<std::size_t>(n));
            if (!marcado)
                out.append(".0", 2);
        }

        inline void json_encode_utf8(String &out, std::uint32_t cp)
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

        inline void json_escape_to(String &out, const char *s, std::size_t n)
        {
            out.push_back('"');
            std::size_t chunk = 0;
            char esc_buf[8];
            for (std::size_t i = 0; i < n; ++i)
            {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                const char *esc = nullptr;
                std::size_t esc_len = 2;
                switch (c)
                {
                case '"': esc = "\\\""; break;
                case '\\': esc = "\\\\"; break;
                case '\b': esc = "\\b"; break;
                case '\f': esc = "\\f"; break;
                case '\n': esc = "\\n"; break;
                case '\r': esc = "\\r"; break;
                case '\t': esc = "\\t"; break;
                default:
                    if (c < 0x20)
                    {
                        std::snprintf(esc_buf, sizeof(esc_buf), "\\u%04x", c);
                        esc = esc_buf;
                        esc_len = 6;
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
            out.push_back('"');
        }

    } // namespace detail

    class Json
    {
    public:
        enum Type : unsigned char
        {
            Null = 0,
            Bool,
            Int,  // inteiro com sinal
            Uint, // inteiro sem sinal que não cabe em int64
            Real,
            Str,
            Arr,
            Obj
        };

        struct Member; // definido logo a seguir à classe
        using Array = Vector<Json>;
        using Object = Vector<Member>;

        // limite de recursão: protege o stack contra "[[[[[[..." vindo de fora
        static constexpr std::size_t kMaxDepth = 200;

        struct Error
        {
            const char *message; // literal estático; nullptr = sem erro
            std::size_t offset;
            std::size_t line;   // 1-based
            std::size_t column; // 1-based

            Error() noexcept : message(nullptr), offset(0), line(0), column(0) {}
            explicit operator bool() const noexcept { return message != nullptr; }
        };

        // ---- construção ------------------------------------------------------

        Json() noexcept : type_(Null) { v_.u = 0; }
        Json(std::nullptr_t) noexcept : type_(Null) { v_.u = 0; }
        Json(bool b) noexcept : type_(Bool) { v_.b = b; }

        template <typename I,
                  typename detail::enable_if<std::is_integral<I>::value &&
                                                 std::is_signed<I>::value,
                                             int>::type = 0>
        Json(I i) noexcept : type_(Int)
        {
            v_.i = static_cast<std::int64_t>(i);
        }

        template <typename U,
                  typename detail::enable_if<std::is_integral<U>::value &&
                                                 !std::is_signed<U>::value &&
                                                 !detail::is_same<U, bool>::value,
                                             int>::type = 0>
        Json(U u) noexcept : type_(Uint)
        {
            v_.u = static_cast<std::uint64_t>(u);
        }

        template <typename F,
                  typename detail::enable_if<std::is_floating_point<F>::value, int>::type = 0>
        Json(F f) noexcept : type_(Real)
        {
            v_.d = static_cast<double>(f);
        }

        Json(const char *s) : type_(Str) { ::new (static_cast<void *>(&v_.s)) String(s); }
        Json(const char *s, std::size_t n) : type_(Str)
        {
            ::new (static_cast<void *>(&v_.s)) String(s, n);
        }
        Json(const String &s) : type_(Str) { ::new (static_cast<void *>(&v_.s)) String(s); }
        Json(String &&s) : type_(Str)
        {
            ::new (static_cast<void *>(&v_.s)) String(detail::move(s));
        }

        static Json array();
        static Json object();

        Json(const Json &o);
        Json(Json &&o) noexcept;
        Json &operator=(const Json &o);
        Json &operator=(Json &&o) noexcept;
        ~Json() { destroy(); }

        void clear() noexcept { destroy(); } // volta a null
        void swap(Json &o) noexcept;

        // ---- tipo ------------------------------------------------------------

        Type type() const noexcept { return type_; }
        const char *type_name() const noexcept;

        bool is_null() const noexcept { return type_ == Null; }
        bool is_bool() const noexcept { return type_ == Bool; }
        bool is_int() const noexcept { return type_ == Int; }
        bool is_uint() const noexcept { return type_ == Uint; }
        bool is_real() const noexcept { return type_ == Real; }
        bool is_number() const noexcept { return type_ == Int || type_ == Uint || type_ == Real; }
        bool is_string() const noexcept { return type_ == Str; }
        bool is_array() const noexcept { return type_ == Arr; }
        bool is_object() const noexcept { return type_ == Obj; }

        // ---- leitura de escalares (com default se o tipo não bater) -----------

        bool as_bool(bool def = false) const noexcept;
        std::int64_t as_int(std::int64_t def = 0) const noexcept;
        std::uint64_t as_uint(std::uint64_t def = 0) const noexcept;
        double as_double(double def = 0.0) const noexcept;
        const char *as_cstr(const char *def = "") const noexcept;

        // acesso direto: fatal se o tipo não for esse (erro de programa)
        const String &str() const;
        Array &items();
        const Array &items() const;
        Object &members();
        const Object &members() const;

        // ---- comum -----------------------------------------------------------

        // array/objeto: nº de elementos; null: 0; escalar: 1
        std::size_t size() const noexcept;
        bool empty() const noexcept { return size() == 0; }

        // ---- array -----------------------------------------------------------

        template <typename I,
                  typename detail::enable_if<std::is_integral<I>::value, int>::type = 0>
        Json &operator[](I i) { return at(static_cast<std::size_t>(i)); }
        template <typename I,
                  typename detail::enable_if<std::is_integral<I>::value, int>::type = 0>
        const Json &operator[](I i) const { return at(static_cast<std::size_t>(i)); }

        Json &at(std::size_t i);
        const Json &at(std::size_t i) const;

        void push_back(const Json &v);
        void push_back(Json &&v);
        void pop_back();
        void erase(std::size_t i);
        void reserve(std::size_t n);

        // ---- objeto ----------------------------------------------------------

        Json *find(const char *key) noexcept;
        const Json *find(const char *key) const noexcept;
        Json *find(const String &key) noexcept;
        const Json *find(const String &key) const noexcept;
        bool contains(const char *key) const noexcept { return find(key) != nullptr; }
        bool contains(const String &key) const noexcept { return find(key) != nullptr; }

        // não-const: cria a chave (e transforma null em objeto). Atenção: pode
        // realocar o Vector interno e invalidar referências obtidas antes.
        Json &operator[](const char *key);
        Json &operator[](const String &key);
        // const: chave em falta devolve um null partilhado, para encadear
        // cfg["janela"]["largura"].as_int(1280) sem rebentar
        const Json &operator[](const char *key) const;
        const Json &operator[](const String &key) const;

        Json &set(String key, Json value);
        bool erase(const char *key);

        static const Json &null_value();

        // ---- parse / dump ----------------------------------------------------

        static Json parse(const char *text, std::size_t len, Error *err = nullptr);
        static Json parse(const char *text, Error *err = nullptr)
        {
            return parse(text, text ? std::strlen(text) : 0, err);
        }
        static Json parse(const String &text, Error *err = nullptr)
        {
            return parse(text.data(), text.size(), err);
        }

        // indent < 0 → compacto; >= 0 → uma linha por elemento com esse recuo
        String dump(int indent = -1) const;
        void dump_to(String &out, int indent = -1) const;

        bool operator==(const Json &o) const;
        bool operator!=(const Json &o) const { return !(*this == o); }

    private:
        union Storage
        {
            bool b;
            std::int64_t i;
            std::uint64_t u;
            double d;
            String s;
            Array *a;
            Object *o;

            Storage() {}
            ~Storage() {}
        };

        Storage v_;
        Type type_;

        void destroy() noexcept;
        void steal(Json &o) noexcept; // assume que *this já foi destruído
        void dump_impl(String &out, int indent, int level) const;

        static Array *new_array();
        static Array *new_array(const Array &src);
        static void del_array(Array *p);
        static Object *new_object();
        static Object *new_object(const Object &src);
        static void del_object(Object *p);
    };

    struct Json::Member
    {
        String key;
        Json value;

        Member() {}
        Member(String k, Json v) : key(detail::move(k)), value(detail::move(v)) {}
    };

    // ---- ciclo de vida -------------------------------------------------------

    inline Json::Array *Json::new_array()
    {
        HeapAlloc a;
        return ::new (a.allocate(sizeof(Array), alignof(Array))) Array();
    }
    inline Json::Array *Json::new_array(const Array &src)
    {
        HeapAlloc a;
        return ::new (a.allocate(sizeof(Array), alignof(Array))) Array(src);
    }
    inline void Json::del_array(Array *p)
    {
        if (!p)
            return;
        p->~Array();
        HeapAlloc().deallocate(p, sizeof(Array));
    }
    inline Json::Object *Json::new_object()
    {
        HeapAlloc a;
        return ::new (a.allocate(sizeof(Object), alignof(Object))) Object();
    }
    inline Json::Object *Json::new_object(const Object &src)
    {
        HeapAlloc a;
        return ::new (a.allocate(sizeof(Object), alignof(Object))) Object(src);
    }
    inline void Json::del_object(Object *p)
    {
        if (!p)
            return;
        p->~Object();
        HeapAlloc().deallocate(p, sizeof(Object));
    }

    inline Json Json::array()
    {
        Json j;
        j.v_.a = new_array();
        j.type_ = Arr;
        return j;
    }

    inline Json Json::object()
    {
        Json j;
        j.v_.o = new_object();
        j.type_ = Obj;
        return j;
    }

    inline void Json::destroy() noexcept
    {
        switch (type_)
        {
        case Str: v_.s.~String(); break;
        case Arr: del_array(v_.a); break;
        case Obj: del_object(v_.o); break;
        default: break;
        }
        type_ = Null;
        v_.u = 0;
    }

    inline void Json::steal(Json &o) noexcept
    {
        type_ = o.type_;
        switch (type_)
        {
        case Str:
            ::new (static_cast<void *>(&v_.s)) String(detail::move(o.v_.s));
            o.v_.s.~String();
            break;
        case Arr: v_.a = o.v_.a; break;
        case Obj: v_.o = o.v_.o; break;
        case Bool: v_.b = o.v_.b; break;
        case Int: v_.i = o.v_.i; break;
        case Uint: v_.u = o.v_.u; break;
        case Real: v_.d = o.v_.d; break;
        default: v_.u = 0; break;
        }
        o.type_ = Null;
        o.v_.u = 0;
    }

    inline Json::Json(const Json &o) : type_(o.type_)
    {
        switch (type_)
        {
        case Str: ::new (static_cast<void *>(&v_.s)) String(o.v_.s); break;
        case Arr: v_.a = new_array(*o.v_.a); break;
        case Obj: v_.o = new_object(*o.v_.o); break;
        case Bool: v_.b = o.v_.b; break;
        case Int: v_.i = o.v_.i; break;
        case Uint: v_.u = o.v_.u; break;
        case Real: v_.d = o.v_.d; break;
        default: v_.u = 0; break;
        }
    }

    inline Json::Json(Json &&o) noexcept : type_(Null)
    {
        v_.u = 0;
        steal(o);
    }

    inline Json &Json::operator=(const Json &o)
    {
        if (this != &o)
        {
            Json tmp(o);
            destroy();
            steal(tmp);
        }
        return *this;
    }

    inline Json &Json::operator=(Json &&o) noexcept
    {
        if (this != &o)
        {
            destroy();
            steal(o);
        }
        return *this;
    }

    inline void Json::swap(Json &o) noexcept
    {
        if (this == &o)
            return;
        Json tmp(detail::move(*this));
        *this = detail::move(o);
        o = detail::move(tmp);
    }

    inline const Json &Json::null_value()
    {
        static const Json kNull;
        return kNull;
    }

    inline const char *Json::type_name() const noexcept
    {
        switch (type_)
        {
        case Null: return "null";
        case Bool: return "bool";
        case Int: return "int";
        case Uint: return "uint";
        case Real: return "real";
        case Str: return "string";
        case Arr: return "array";
        case Obj: return "object";
        }
        return "?";
    }

    // ---- escalares -----------------------------------------------------------

    inline bool Json::as_bool(bool def) const noexcept
    {
        return type_ == Bool ? v_.b : def;
    }

    namespace detail
    {
        inline bool json_real_fits_int(double d) noexcept
        {
            return d == d && d >= -9223372036854775808.0 && d < 9223372036854775808.0;
        }

        inline bool json_real_fits_uint(double d) noexcept
        {
            return d == d && d >= 0.0 && d < 18446744073709551616.0;
        }
    }

    inline std::int64_t Json::as_int(std::int64_t def) const noexcept
    {
        switch (type_)
        {
        case Int: return v_.i;
        case Uint:
            return v_.u <= static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())
                       ? static_cast<std::int64_t>(v_.u)
                       : def;
        case Real: return detail::json_real_fits_int(v_.d) ? static_cast<std::int64_t>(v_.d) : def;
        default: return def;
        }
    }

    inline std::uint64_t Json::as_uint(std::uint64_t def) const noexcept
    {
        switch (type_)
        {
        case Int: return v_.i >= 0 ? static_cast<std::uint64_t>(v_.i) : def;
        case Uint: return v_.u;
        case Real: return detail::json_real_fits_uint(v_.d) ? static_cast<std::uint64_t>(v_.d) : def;
        default: return def;
        }
    }

    inline double Json::as_double(double def) const noexcept
    {
        switch (type_)
        {
        case Int: return static_cast<double>(v_.i);
        case Uint: return static_cast<double>(v_.u);
        case Real: return v_.d;
        default: return def;
        }
    }

    inline const char *Json::as_cstr(const char *def) const noexcept
    {
        return type_ == Str ? v_.s.c_str() : def;
    }

    inline const String &Json::str() const
    {
        if (CT_UNLIKELY(type_ != Str))
            detail::fatal("ct::Json::str: o valor nao e uma string");
        return v_.s;
    }

    inline Json::Array &Json::items()
    {
        if (CT_UNLIKELY(type_ != Arr))
            detail::fatal("ct::Json::items: o valor nao e um array");
        return *v_.a;
    }
    inline const Json::Array &Json::items() const
    {
        if (CT_UNLIKELY(type_ != Arr))
            detail::fatal("ct::Json::items: o valor nao e um array");
        return *v_.a;
    }
    inline Json::Object &Json::members()
    {
        if (CT_UNLIKELY(type_ != Obj))
            detail::fatal("ct::Json::members: o valor nao e um objeto");
        return *v_.o;
    }
    inline const Json::Object &Json::members() const
    {
        if (CT_UNLIKELY(type_ != Obj))
            detail::fatal("ct::Json::members: o valor nao e um objeto");
        return *v_.o;
    }

    inline std::size_t Json::size() const noexcept
    {
        switch (type_)
        {
        case Null: return 0;
        case Arr: return v_.a->size();
        case Obj: return v_.o->size();
        default: return 1;
        }
    }

    // ---- array ---------------------------------------------------------------

    inline Json &Json::at(std::size_t i)
    {
        if (CT_UNLIKELY(type_ != Arr))
            detail::fatal("ct::Json::at: indexar por numero um valor que nao e array");
        if (CT_UNLIKELY(i >= v_.a->size()))
            detail::fatal("ct::Json::at: index fora dos limites");
        return (*v_.a)[i];
    }

    inline const Json &Json::at(std::size_t i) const
    {
        if (CT_UNLIKELY(type_ != Arr))
            detail::fatal("ct::Json::at: indexar por numero um valor que nao e array");
        if (CT_UNLIKELY(i >= v_.a->size()))
            detail::fatal("ct::Json::at: index fora dos limites");
        return (*v_.a)[i];
    }

    inline void Json::push_back(const Json &v)
    {
        if (type_ == Null)
            *this = Json::array();
        if (CT_UNLIKELY(type_ != Arr))
            detail::fatal("ct::Json::push_back: o valor nao e um array");
        v_.a->push_back(v);
    }

    inline void Json::push_back(Json &&v)
    {
        if (type_ == Null)
            *this = Json::array();
        if (CT_UNLIKELY(type_ != Arr))
            detail::fatal("ct::Json::push_back: o valor nao e um array");
        v_.a->push_back(detail::move(v));
    }

    inline void Json::pop_back()
    {
        if (CT_UNLIKELY(type_ != Arr))
            detail::fatal("ct::Json::pop_back: o valor nao e um array");
        if (CT_UNLIKELY(v_.a->empty()))
            detail::fatal("ct::Json::pop_back: array vazio");
        v_.a->pop_back();
    }

    inline void Json::erase(std::size_t i)
    {
        if (CT_UNLIKELY(type_ != Arr))
            detail::fatal("ct::Json::erase: o valor nao e um array");
        if (CT_UNLIKELY(i >= v_.a->size()))
            detail::fatal("ct::Json::erase: index fora dos limites");
        v_.a->erase(v_.a->begin() + static_cast<std::ptrdiff_t>(i));
    }

    inline void Json::reserve(std::size_t n)
    {
        if (type_ == Arr)
            v_.a->reserve(n);
        else if (type_ == Obj)
            v_.o->reserve(n);
    }

    // ---- objeto --------------------------------------------------------------

    inline Json *Json::find(const char *key) noexcept
    {
        if (type_ != Obj || !key)
            return nullptr;
        const std::size_t n = std::strlen(key);
        Object &members = *v_.o;
        for (std::size_t i = 0; i < members.size(); ++i)
            if (members[i].key.size() == n &&
                std::memcmp(members[i].key.data(), key, n) == 0)
                return &members[i].value;
        return nullptr;
    }

    inline const Json *Json::find(const char *key) const noexcept
    {
        return const_cast<Json *>(this)->find(key);
    }

    inline Json *Json::find(const String &key) noexcept
    {
        if (type_ != Obj)
            return nullptr;
        Object &members = *v_.o;
        for (std::size_t i = 0; i < members.size(); ++i)
            if (members[i].key == key)
                return &members[i].value;
        return nullptr;
    }

    inline const Json *Json::find(const String &key) const noexcept
    {
        return const_cast<Json *>(this)->find(key);
    }

    inline Json &Json::operator[](const char *key)
    {
        if (type_ == Null)
            *this = Json::object();
        if (CT_UNLIKELY(type_ != Obj))
            detail::fatal("ct::Json: indexar por chave um valor que nao e objeto");
        if (Json *found = find(key))
            return *found;
        v_.o->emplace_back(String(key), Json());
        return v_.o->back().value;
    }

    inline Json &Json::operator[](const String &key)
    {
        if (type_ == Null)
            *this = Json::object();
        if (CT_UNLIKELY(type_ != Obj))
            detail::fatal("ct::Json: indexar por chave um valor que nao e um objeto");
        if (Json *found = find(key))
            return *found;
        v_.o->emplace_back(String(key), Json());
        return v_.o->back().value;
    }

    inline const Json &Json::operator[](const char *key) const
    {
        const Json *found = find(key);
        return found ? *found : null_value();
    }

    inline const Json &Json::operator[](const String &key) const
    {
        const Json *found = find(key);
        return found ? *found : null_value();
    }

    inline Json &Json::set(String key, Json value)
    {
        if (type_ == Null)
            *this = Json::object();
        if (CT_UNLIKELY(type_ != Obj))
            detail::fatal("ct::Json::set: o valor nao e um objeto");
        if (Json *found = find(key))
        {
            *found = detail::move(value);
            return *found;
        }
        v_.o->emplace_back(detail::move(key), detail::move(value));
        return v_.o->back().value;
    }

    inline bool Json::erase(const char *key)
    {
        if (type_ != Obj || !key)
            return false;
        const std::size_t n = std::strlen(key);
        Object &members = *v_.o;
        for (std::size_t i = 0; i < members.size(); ++i)
            if (members[i].key.size() == n &&
                std::memcmp(members[i].key.data(), key, n) == 0)
            {
                members.erase(members.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
        return false;
    }

    // ---- comparação ----------------------------------------------------------

    inline bool Json::operator==(const Json &o) const
    {
        if (is_number() && o.is_number())
        {
            if (type_ == Real || o.type_ == Real)
                return as_double() == o.as_double();
            if (type_ == o.type_)
                return type_ == Int ? v_.i == o.v_.i : v_.u == o.v_.u;
            const Json &si = (type_ == Int) ? *this : o;   // com sinal
            const Json &un = (type_ == Int) ? o : *this;   // sem sinal
            return si.v_.i >= 0 && static_cast<std::uint64_t>(si.v_.i) == un.v_.u;
        }
        if (type_ != o.type_)
            return false;
        switch (type_)
        {
        case Null:
            return true;
        case Bool:
            return v_.b == o.v_.b;
        case Str:
            return v_.s.size() == o.v_.s.size() &&
                   std::memcmp(v_.s.data(), o.v_.s.data(), v_.s.size()) == 0;
        case Arr:
        {
            const Array &a = *v_.a;
            const Array &b = *o.v_.a;
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (!(a[i] == b[i]))
                    return false;
            return true;
        }
        case Obj:
        {
            // objeto JSON não tem ordem: cada membro tem de ter um par igual por usar no outro lado
            const Object &a = *v_.o;
            const Object &b = *o.v_.o;
            if (a.size() != b.size())
                return false;
            Vector<std::uint8_t> used;
            used.resize(b.size(), 0);
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                std::size_t hit = b.size();
                if (!used[i] && a[i].key == b[i].key && a[i].value == b[i].value)
                    hit = i;
                else
                    for (std::size_t j = 0; j < b.size(); ++j)
                        if (!used[j] && a[i].key == b[j].key && a[i].value == b[j].value)
                        {
                            hit = j;
                            break;
                        }
                if (hit == b.size())
                    return false;
                used[hit] = 1;
            }
            return true;
        }
        default:
            return false;
        }
    }

    // ---- dump ----------------------------------------------------------------

    namespace detail
    {
        inline void json_newline(String &out, int indent, int level)
        {
            if (indent < 0)
                return;
            static const char kSpaces[] =
                "                                                                ";
            const std::size_t kN = sizeof(kSpaces) - 1; // 64
            out.push_back('\n');
            std::size_t spaces = static_cast<std::size_t>(indent) *
                                 static_cast<std::size_t>(level);
            while (spaces >= kN)
            {
                out.append(kSpaces, kN);
                spaces -= kN;
            }
            if (spaces)
                out.append(kSpaces, spaces);
        }
    } // namespace detail

    inline void Json::dump_impl(String &out, int indent, int level) const
    {
        switch (type_)
        {
        case Null:
            out.append("null", 4);
            break;
        case Bool:
            if (v_.b)
                out.append("true", 4);
            else
                out.append("false", 5);
            break;
        case Int:
            out.append_number(static_cast<long long>(v_.i));
            break;
        case Uint:
            out.append_number(static_cast<unsigned long long>(v_.u));
            break;
        case Real:
        {
            const double d = v_.d;
            const double inf = (std::numeric_limits<double>::infinity)();
            if (d != d || d == inf || d == -inf) // JSON não tem NaN nem infinito
                out.append("null", 4);
            else
                detail::json_append_double(out, d);
            break;
        }
        case Str:
            detail::json_escape_to(out, v_.s.data(), v_.s.size());
            break;
        case Arr:
        {
            const Array &a = *v_.a;
            if (a.empty())
            {
                out.append("[]", 2);
                break;
            }
            out.push_back('[');
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                if (i)
                    out.push_back(',');
                detail::json_newline(out, indent, level + 1);
                a[i].dump_impl(out, indent, level + 1);
            }
            detail::json_newline(out, indent, level);
            out.push_back(']');
            break;
        }
        case Obj:
        {
            const Object &m = *v_.o;
            if (m.empty())
            {
                out.append("{}", 2);
                break;
            }
            out.push_back('{');
            for (std::size_t i = 0; i < m.size(); ++i)
            {
                if (i)
                    out.push_back(',');
                detail::json_newline(out, indent, level + 1);
                detail::json_escape_to(out, m[i].key.data(), m[i].key.size());
                out.push_back(':');
                if (indent >= 0)
                    out.push_back(' ');
                m[i].value.dump_impl(out, indent, level + 1);
            }
            detail::json_newline(out, indent, level);
            out.push_back('}');
            break;
        }
        }
    }

    inline void Json::dump_to(String &out, int indent) const { dump_impl(out, indent, 0); }

    inline String Json::dump(int indent) const
    {
        String out;
        out.reserve(64);
        dump_impl(out, indent, 0);
        return out;
    }

    // ---- parser --------------------------------------------------------------

    namespace detail
    {
        struct JsonParser
        {
            const char *first;
            const char *cur;
            const char *last;
            const char *err_at;
            const char *err_msg;
            std::size_t depth;

            JsonParser(const char *b, const char *e)
                : first(b), cur(b), last(e), err_at(b), err_msg(nullptr), depth(0) {}

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

            bool read_hex4(std::uint32_t &out)
            {
                if (last - cur < 4)
                    return fail("escape \\u incompleto", cur);
                std::uint32_t v = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const char c = cur[i];
                    std::uint32_t d;
                    if (c >= '0' && c <= '9')
                        d = static_cast<std::uint32_t>(c - '0');
                    else if (c >= 'a' && c <= 'f')
                        d = static_cast<std::uint32_t>(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F')
                        d = static_cast<std::uint32_t>(c - 'A' + 10);
                    else
                        return fail("digito hexadecimal invalido no \\u", cur + i);
                    v = (v << 4) | d;
                }
                cur += 4;
                out = v;
                return true;
            }

            bool decode_escape(String &out)
            {
                if (cur == last)
                    return fail("escape incompleto", cur);
                const char e = *cur++;
                switch (e)
                {
                case '"': out.push_back('"'); return true;
                case '\\': out.push_back('\\'); return true;
                case '/': out.push_back('/'); return true;
                case 'b': out.push_back('\b'); return true;
                case 'f': out.push_back('\f'); return true;
                case 'n': out.push_back('\n'); return true;
                case 'r': out.push_back('\r'); return true;
                case 't': out.push_back('\t'); return true;
                case 'u':
                {
                    std::uint32_t cp = 0;
                    if (!read_hex4(cp))
                        return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) // surrogate alto: precisa do par
                    {
                        if (last - cur < 2 || cur[0] != '\\' || cur[1] != 'u')
                            return fail("surrogate alto sem o par \\u seguinte", cur);
                        cur += 2;
                        std::uint32_t lo = 0;
                        if (!read_hex4(lo))
                            return false;
                        if (lo < 0xDC00 || lo > 0xDFFF)
                            return fail("segundo \\u nao e um surrogate baixo", cur - 4);
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    else if (cp >= 0xDC00 && cp <= 0xDFFF)
                        return fail("surrogate baixo sem o alto", cur - 4);
                    json_encode_utf8(out, cp);
                    return true;
                }
                default:
                    return fail("escape desconhecido", cur - 1);
                }
            }

            // cur aponta para a aspa de abertura
            bool parse_string_raw(String &out)
            {
                ++cur;
                const char *chunk = cur;
                for (;;)
                {
                    if (cur == last)
                        return fail("string sem aspas de fecho", chunk - 1);
                    const unsigned char c = static_cast<unsigned char>(*cur);
                    if (c == '"')
                    {
                        out.append(chunk, static_cast<std::size_t>(cur - chunk));
                        ++cur;
                        return true;
                    }
                    if (c == '\\')
                    {
                        out.append(chunk, static_cast<std::size_t>(cur - chunk));
                        ++cur;
                        if (!decode_escape(out))
                            return false;
                        chunk = cur;
                        continue;
                    }
                    if (c < 0x20)
                        return fail("caracter de controlo cru dentro da string", cur);
                    ++cur;
                }
            }

            bool parse_number(Json &out)
            {
                const char *const start = cur;
                if (cur != last && *cur == '-')
                    ++cur;
                if (cur == last)
                    return fail("numero incompleto", start);

                if (*cur == '0')
                    ++cur; // JSON não deixa zeros à esquerda ("01" é inválido)
                else if (json_is_digit(*cur))
                    while (cur != last && json_is_digit(*cur))
                        ++cur;
                else
                    return fail("valor inesperado", start);

                bool is_real = false;
                if (cur != last && *cur == '.')
                {
                    ++cur;
                    is_real = true;
                    if (cur == last || !json_is_digit(*cur))
                        return fail("faltam digitos depois do ponto decimal", cur);
                    while (cur != last && json_is_digit(*cur))
                        ++cur;
                }
                if (cur != last && (*cur == 'e' || *cur == 'E'))
                {
                    ++cur;
                    is_real = true;
                    if (cur != last && (*cur == '+' || *cur == '-'))
                        ++cur;
                    if (cur == last || !json_is_digit(*cur))
                        return fail("expoente sem digitos", cur);
                    while (cur != last && json_is_digit(*cur))
                        ++cur;
                }

                if (!is_real)
                {
                    const bool neg = (*start == '-');
                    const char *d = start + (neg ? 1 : 0);
                    std::uint64_t mag = 0;
                    bool overflow = false;
                    for (; d != cur; ++d)
                    {
                        const std::uint64_t digit = static_cast<std::uint64_t>(*d - '0');
                        if (mag > ((std::numeric_limits<std::uint64_t>::max)() - digit) / 10)
                        {
                            overflow = true;
                            break;
                        }
                        mag = mag * 10 + digit;
                    }
                    if (!overflow)
                    {
                        const std::uint64_t kMax =
                            static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
                        if (neg)
                        {
                            if (mag <= kMax)
                                out = Json(-static_cast<std::int64_t>(mag));
                            else if (mag == kMax + 1)
                                out = Json((std::numeric_limits<std::int64_t>::min)());
                            else
                                overflow = true;
                        }
                        else if (mag <= kMax)
                            out = Json(static_cast<std::int64_t>(mag));
                        else
                            out = Json(mag); // não cabe em int64 → Uint
                    }
                    if (!overflow)
                        return true;
                    // demasiado grande para um inteiro: passa a double
                }
                const double d = json_to_double(start, cur);
                // JSON não tem infinito: aceitar 1e999 como inf faria o dump escrever
                // null e perder o valor em silêncio — melhor rejeitar já
                const double inf = (std::numeric_limits<double>::infinity)();
                if (d == inf || d == -inf)
                    return fail("numero fora do alcance do double", start);
                out = Json(d);
                return true;
            }

            bool parse_value(Json &out);

            bool parse_array(Json &out)
            {
                ++cur; // '['
                out = Json::array();
                Json::Array &arr = out.items();
                skip_ws();
                if (cur != last && *cur == ']')
                {
                    ++cur;
                    return true;
                }
                for (;;)
                {
                    arr.push_back(Json());
                    if (!parse_value(arr.back()))
                        return false;
                    skip_ws();
                    if (cur == last)
                        return fail("array sem ']'", cur);
                    if (*cur == ',')
                    {
                        ++cur;
                        continue;
                    }
                    if (*cur == ']')
                    {
                        ++cur;
                        return true;
                    }
                    return fail("esperava ',' ou ']'", cur);
                }
            }

            bool parse_object(Json &out)
            {
                ++cur; // '{'
                out = Json::object();
                Json::Object &members = out.members();
                skip_ws();
                if (cur != last && *cur == '}')
                {
                    ++cur;
                    return true;
                }
                for (;;)
                {
                    skip_ws();
                    if (cur == last)
                        return fail("objeto sem '}'", cur);
                    if (*cur != '"')
                        return fail("esperava uma chave entre aspas", cur);
                    String key;
                    if (!parse_string_raw(key))
                        return false;
                    skip_ws();
                    if (cur == last || *cur != ':')
                        return fail("esperava ':' depois da chave", cur);
                    ++cur;
                    // chaves repetidas ficam ambas (o RFC não define): find() dá a 1ª
                    members.emplace_back(detail::move(key), Json());
                    if (!parse_value(members.back().value))
                        return false;
                    skip_ws();
                    if (cur == last)
                        return fail("objeto sem '}'", cur);
                    if (*cur == ',')
                    {
                        ++cur;
                        continue;
                    }
                    if (*cur == '}')
                    {
                        ++cur;
                        return true;
                    }
                    return fail("esperava ',' ou '}'", cur);
                }
            }
        };

        inline bool JsonParser::parse_value(Json &out)
        {
            skip_ws();
            if (cur == last)
                return fail("fim de input inesperado", cur);
            switch (*cur)
            {
            case '{':
            case '[':
            {
                if (depth >= Json::kMaxDepth)
                    return fail("aninhamento demasiado profundo", cur);
                ++depth;
                const bool ok = (*cur == '{') ? parse_object(out) : parse_array(out);
                --depth;
                return ok;
            }
            case '"':
            {
                String s;
                if (!parse_string_raw(s))
                    return false;
                out = Json(detail::move(s));
                return true;
            }
            case 't':
                if (last - cur >= 4 && std::memcmp(cur, "true", 4) == 0)
                {
                    cur += 4;
                    out = Json(true);
                    return true;
                }
                return fail("literal invalido (esperava true)", cur);
            case 'f':
                if (last - cur >= 5 && std::memcmp(cur, "false", 5) == 0)
                {
                    cur += 5;
                    out = Json(false);
                    return true;
                }
                return fail("literal invalido (esperava false)", cur);
            case 'n':
                if (last - cur >= 4 && std::memcmp(cur, "null", 4) == 0)
                {
                    cur += 4;
                    out = Json();
                    return true;
                }
                return fail("literal invalido (esperava null)", cur);
            default:
                return parse_number(out);
            }
        }
    } // namespace detail

    inline Json Json::parse(const char *text, std::size_t len, Error *err)
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
            return Json();
        }

        detail::JsonParser ps(text, text + len);
        std::size_t bom = 0;
        if (len >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
            static_cast<unsigned char>(text[1]) == 0xBB &&
            static_cast<unsigned char>(text[2]) == 0xBF)
            bom = 3;
        ps.cur += bom;

        Json root;
        if (ps.parse_value(root))
        {
            ps.skip_ws();
            if (ps.cur != ps.last)
                ps.fail("lixo depois do valor JSON", ps.cur);
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
            return Json();
        }
        return root;
    }

    inline void swap(Json &a, Json &b) noexcept { a.swap(b); }

} // namespace ct
