# ct::containers

Header-only C++14 containers and utilities, built as a faster, leaner
replacement for the relevant parts of the standard library. No exceptions,
no hidden allocations, no speculative API surface - every type here exists
because something needed it.

## Usage

```cmake
add_subdirectory(containers)
target_link_libraries(your_target PRIVATE ct::ct)
```

`ct` (alias `ct::ct`) is a CMake `INTERFACE` target that just adds `include/`
to your include path. There's nothing to build or link - include what you
need. The only exception is threading: [thread.hpp](include/ct/thread.hpp) and
[threadpool.hpp](include/ct/threadpool.hpp) need pthreads on POSIX, so link
`ct::threads` instead of `ct::ct` in targets that use them. Network users link
`ct::sockets`, which adds `ct::threads` and Winsock (`ws2_32`) on Windows.

When `containers` is added as a subdirectory, the test suite and the
benchmarks are off by default, so consumers do not fetch GoogleTest or build
`-march=native` binaries. Turn them on with `-DCT_BUILD_TESTS=ON` and
`-DCT_BUILD_BENCH=ON`; both default to on when this is the top-level project.

```cpp
#include <ct/vector.hpp>
#include <ct/string.hpp>

ct::Vector<int> v;
v.push_back(42);
```

Requirements: a C++14 compiler. No external dependencies for the library
itself (GoogleTest is fetched only to build the test suite).

## Design rules

- **Header-only.** Every container is a single `#pragma once` header under
  `include/ct/`.
- **Fail-fast, not exceptions.** Invalid usage (capacity overflow, bad
  handles, etc.) calls `abort()` instead of throwing. See
  [tests/test_limits.cpp](tests/test_limits.cpp) for the exact guarantees.
- **No comments in the source.** The API is meant to read like the standard
  library's own headers: consistent naming, small surface, no prose.
- **No smart-pointer-by-default design.** Ownership is explicit; `ct::Rc`,
  `ct::Unique`, `ct::Weak` in [ptr.hpp](include/ct/ptr.hpp) exist for the
  cases that actually need shared/weak ownership, not as a default.

## What's in here

| Header | Type(s) | What it is |
|---|---|---|
| [vector.hpp](include/ct/vector.hpp) | `Vector` | Dynamic array, `std::vector` replacement |
| [deque.hpp](include/ct/deque.hpp) | `Deque` | Power-of-two ring-buffer double-ended queue |
| [stack.hpp](include/ct/stack.hpp) | `Stack` | LIFO adaptor over `Vector` |
| [queue.hpp](include/ct/queue.hpp) | `Queue` | FIFO adaptor over `Deque` |
| [priority_queue.hpp](include/ct/priority_queue.hpp) | `PriorityQueue` | Binary heap adaptor over `Vector` |
| [string.hpp](include/ct/string.hpp) | `String` | Small-string-optimized string, `std::string` replacement |
| [span.hpp](include/ct/span.hpp) | `Span`, `StringView` | Non-owning views over contiguous data |
| [array.hpp](include/ct/array.hpp) | `Array` | Fixed-size array, `std::array` replacement |
| [arena.hpp](include/ct/arena.hpp) | `BasicArena` | Bump allocator for fixed-lifetime allocations |
| [pool.hpp](include/ct/pool.hpp) | `Pool` | Fixed-size slot allocator with a free list |
| [slotmap.hpp](include/ct/slotmap.hpp) | `SlotMap` | Stable-index container with generation-checked handles |
| [slotmap32.hpp](include/ct/slotmap32.hpp) | `SlotMap32` | Same as `SlotMap` with a 4-byte handle: 20-bit index (about one million slots), 12-bit generation (a stale handle can alias after 2048 reuses of one slot) |
| [flatmap.hpp](include/ct/flatmap.hpp) | `FlatMap` | Sorted-vector associative container |
| [hashmap.hpp](include/ct/hashmap.hpp) | `HashMap` | Open-addressing hash map |
| [hashset.hpp](include/ct/hashset.hpp) | `HashSet` | Open-addressing hash set |
| [treemap.hpp](include/ct/treemap.hpp) | `TreeMap` | Ordered associative container |
| [function.hpp](include/ct/function.hpp) | `Function` | Type-erased callable, `std::function` replacement |
| [variant.hpp](include/ct/variant.hpp) | `Variant` | Tagged union, `std::variant` replacement (no exceptions) |
| [ptr.hpp](include/ct/ptr.hpp) | `Rc`, `Unique`, `Weak` | Smart pointers for the cases that need them |
| [sort.hpp](include/ct/sort.hpp) | `insertion_sort`, `heap_sort`, `intro_sort`, `radix_sort` | Sorting algorithms |
| [json.hpp](include/ct/json.hpp) | `Json` | Self-contained JSON parser/serializer |
| [soup.hpp](include/ct/soup.hpp) | `Soup`, `Node` | Tolerant HTML parser with CSS selectors (see below) |
| [xml.hpp](include/ct/xml.hpp) | `Xml` | Self-contained XML parser; mixed content keeps its order (`text()` before the first child, `tail()` after each child) |
| [rectpacker.hpp](include/ct/rectpacker.hpp) | `RectPacker` | 2D rectangle bin packing (texture/atlas packing) |
| [regex.hpp](include/ct/regex.hpp) | `Regex`, `Match` | Regular expressions with Python `re` semantics (see below) |
| [thread.hpp](include/ct/thread.hpp) | `Thread`, `Mutex`, `LockGuard`, `CondVar`, `Atomic` | Cross-platform threading primitives (pthreads / Win32), no `<thread>` |
| [threadpool.hpp](include/ct/threadpool.hpp) | `ThreadPool` | Fixed worker pool with `submit`, `wait_all` and `parallel_for` |
| [stream.hpp](include/ct/stream.hpp) | `Stream`, `FileStream`, `MemoryStream`, `SubStream`, `File` | Generic byte streams for files, memory and bounded views |
| [binary.hpp](include/ct/binary.hpp) | `BinaryReader`, `BinaryWriter` | Little-endian binary serialization |
| [text.hpp](include/ct/text.hpp) | `TextReader`, `TextWriter` | Buffered text input and output |
| [json_stream.hpp](include/ct/json_stream.hpp) | `parse_json` | Optional JSON adapter for `Stream` |
| [xml_stream.hpp](include/ct/xml_stream.hpp) | `parse_xml` | Optional XML adapter for `Stream` |
| [socket.hpp](include/ct/socket.hpp) | `Address`, `TcpListener`, `TcpStream`, `UdpSocket`, `Poller` | Cross-platform IPv4/IPv6 sockets |
| [socket_stream.hpp](include/ct/socket_stream.hpp) | `SocketStream` | Blocking TCP adapter for `Stream` |
| [http.hpp](include/ct/http.hpp) | `HttpRequest`, `HttpResponse`, `HttpParser` | Incremental HTTP/1.0 and HTTP/1.1 protocol layer. `reset()` after `Done` drops the finished message and keeps the bytes that follow it, in the middle of a message it keeps everything received so far, after an `Error` it discards the buffer. Requests are parsed strictly (no `Content-Length` with `Transfer-Encoding`, only a final `chunked`); responses follow RFC 9112 (other transfer codings are read until close, `Transfer-Encoding` wins over `Content-Length`). Decoded request paths keep `%2F` and `%25` encoded and reject control bytes |
| [http_client.hpp](include/ct/http_client.hpp) | `HttpClient` | Blocking HTTP client |
| [http_server.hpp](include/ct/http_server.hpp) | `HttpServer` | Poll-based HTTP server with routes and static files |
| [ini.hpp](include/ct/ini.hpp) | `Ini` | INI settings parser/serializer with typed getters and file IO. Values are plain text with no escapes and no inline comments; a value that starts with `"` and ends with the matching `"` is unquoted (`""` is a literal quote, line breaks are allowed inside), anything else is kept verbatim, and `dump()` quotes only what would not survive a round trip |

## Hashing

`HashMap` and `HashSet` hash keys through `ct::Hash<K>`. It is provided for
the integral types, `bool`, `float`, `double` (`-0.0` and `0.0` hash alike),
pointers, enums, `StringView`, `String` and `ct::Handle<T>`; any other type
works if it has a `hash()` member or if you pass your own hasher as the third
template argument. `ct::hash_combine(seed, h)` builds hashes for composite keys.

`Hash<String>` is transparent, so a `String`-keyed map or set is searched with
a `StringView` or a string literal without building a temporary `String`:

```cpp
ct::HashMap<ct::String, int> ids;
ids.find("player");
ids.contains(ct::StringView(text, n));
```

A hasher opts in by declaring `is_transparent`, an `operator()` for the other
key type and a static `equal(const K &, const Q &)`.

## IO

`Stream` is the common byte interface for files, owned or borrowed memory,
and bounded slices of another stream. Binary serialization is always
little-endian. Text readers and writers buffer in 4 KB chunks. JSON and XML
remain independent of IO unless their optional adapter header is included.

```cpp
#include <ct/binary.hpp>

ct::MemoryStream memory;
ct::BinaryWriter writer(memory);
writer.u32(42);
writer.string("player");

memory.seek(0, ct::Seek::Set);
ct::BinaryReader reader(memory);
unsigned id = reader.u32();
ct::String name;
reader.string(name);
```

## Network and HTTP

`socket.hpp` wraps BSD sockets and Winsock with the same move-only API. TCP
and UDP are blocking by default; `set_nonblocking` plus `Poller` supports
event loops. `SocketStream` is intended only for blocking sequential IO.

HTTP is split so protocol-only code does not include socket APIs. The parser
accepts fragmented requests/responses, `Content-Length`, chunked bodies and
pipelined messages. HTTPS/TLS is not included.

```cpp
#include <ct/http_client.hpp>
#include <ct/http_server.hpp>

ct::HttpServer server;
server.route("GET", "/users/:id",
    [](const ct::HttpRequest &request, ct::HttpResponse &response) {
        response.text(request.param("id"));
    });
server.serve_files("/assets", "data/assets");

ct::Address address;
ct::Address::parse("127.0.0.1", 8080, address);
server.listen(address);
server.run();

ct::HttpResponse response;
ct::HttpClient::get("http://127.0.0.1:8080/users/42", response);
```

`HttpServer::run()` owns the polling loop and blocks until another thread
calls `stop()`. Alternatively, call `poll(timeout_ms)` from an existing game
or editor loop. Route handlers currently run in the polling thread, so long
jobs should be handed to an application worker pool.

## Threads

`thread.hpp` wraps pthreads on POSIX (Linux, macOS, Android, Emscripten with
`-pthread`) and `_beginthreadex` / `SRWLOCK` / `CONDITION_VARIABLE` on
Windows. `Atomic<T>` uses the GCC/Clang `__atomic` builtins or MSVC
`Interlocked*`, for 32/64-bit integers and pointers. A `Thread` joins in its
destructor; `join`/`detach` on a non-joinable thread and a failed thread
creation call `abort()` like every other invalid use in the library.

```cpp
#include <ct/threadpool.hpp>

ct::ThreadPool pool;                       // hardware_concurrency() - 1 workers
pool.submit([] { work(); });
pool.wait_all();                           // the caller runs jobs too while waiting

pool.parallel_for(0, bodies.size(), [&](std::size_t i) { integrate(bodies[i]); });
```

`parallel_for` splits the range into `4 x (workers + 1)` chunks (or `chunk`
elements each), the calling thread helps run them, and it is safe to call
from inside a job. [tests/test_thread.cpp](tests/test_thread.cpp) runs clean
under ThreadSanitizer.

## Regex

`ct::Regex` implements the syntax and semantics of Python's `re` module
(`re.ASCII` mode): groups, named groups, backreferences, lazy/possessive
quantifiers, atomic groups, lookahead/lookbehind, inline and scoped flags,
verbose mode, UTF-8 aware `.` and character classes. The API mirrors `re`:

```cpp
#include <ct/regex.hpp>

ct::Regex::Error err;
ct::Regex re = ct::Regex::compile(R"((?P<user>\w+)@(?P<host>[\w.]+))", 0, &err);
if (!re)
    printf("%s at %zu\n", err.message, err.offset);

ct::Match m;
if (re.search("mail bob@site.com now", &m))
    printf("%.*s\n", int(m["host"].size()), m["host"].data());   // site.com

re.findall(text);                         // Vector<String> of whole matches
re.finditer(text);                        // Vector<Match>
re.split(text);                           // Vector<String>, groups included like Python
re.sub(text, "\\g<host>:\\1");            // Python templates: \1, \g<name>
re.sub(text, [](const ct::Match &m) { return ct::String("x"); });
ct::Regex::escape("1+1=2?");
```

Flags: `IgnoreCase`, `Multiline`, `DotAll`, `Verbose` (`Ascii` is implied).
Positions are byte offsets. Invalid patterns return an invalid `Regex` plus
an `Error` with Python's message and offset; no exceptions.

The engine compiles to bytecode and runs a backtracking VM with an explicit
stack (no recursion). Patterns without backreferences, lookaround, atomic groups or general
counted/nullable repeats (`{n,m}` that cannot be unrolled, `(a*)*`) also get a
visited-state bitmap (as in RE2's BitState), which bounds the work to
`O(splits x text)` and removes catastrophic backtracking. Nullable bodies
under an unbounded repeat, such as `(a*)*b` or `(a?a?)*b`, still backtrack
exponentially, exactly as CPython does. The same holds for nested counted
repeats whose bodies can match the empty string, such as
`(?:(?:(|)+()*)+){3,}`: CPython is just as slow on them, and ct does not
try to be smarter.

[tests/test_regex.cpp](tests/test_regex.cpp) checks ~3000 expectations
generated by running CPython's `re` over the same patterns
([tests/gen_regex_cases.py](tests/gen_regex_cases.py) writes
`tests/regex_cases.inc`), so "same as Python" is verified rather than
promised.

Numbers from [bench/bench_regex.cpp](bench/bench_regex.cpp) on a 2 MB log
text (best of 5, `-O3 -march=native`; CPython 3.12 measured with the same
patterns and text):

| workload | ct::Regex | std::regex | CPython re |
|---|---|---|---|
| findall literal | 1.8 ms | 30 ms | 1.3 ms |
| findall `[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+` | 20 ms | 51 ms | 26 ms |
| findall `[\w.]+@[\w.]+\.com` | 43 ms | 118 ms | 39 ms |
| findall `latency=(\d+)\.(\d+)` | 4.3 ms | 31 ms | 5.8 ms |
| findall `host=(alpha\|beta\|gamma\|delta)` | 2.4 ms | 29 ms | 5.2 ms |
| findall `user=(.+?)@` | 4.1 ms | 30 ms | 5.8 ms |
| findall `(?i)HOST=ALPHA` | 2.0 ms | 33 ms | 11 ms |
| sub `\s+` | 9.0 ms | 52 ms | 48 ms |
| sub `(\w+)@(\w+)` with `\2:\1` | 62 ms | 120 ms | 69 ms |
| 100k `fullmatch` on short strings | 18 ms | 26 ms | 27 ms |
| `(x+x+)+y` on 18 x's, x1000 | 3 ms (30 x's) | 11.8 s | 12.2 s |
| compile x2000 | 2.2 ms | 67 ms | 120 ms |

Not implemented: Unicode categories (`\w` etc. are ASCII, as with
`re.ASCII`), `\N{...}`, `LOCALE`. Case folding is ASCII only.

## HTML

`ct::Soup` parses HTML the way a browser does rather than the way a validator
does: it never fails. Tags that never close (`<br>`, `<img>`), tags closed
implicitly by a sibling (`<li>`, `<p>`, `<td>`), attributes without quotes,
`<` in prose, `</div>` inside a `<script>` string, mismatched end tags - all of
it parses into a usable tree. This is what `ct::Xml` cannot do, by design:
`Xml` is strict and reports an error, and real pages are not well-formed.

```cpp
#include <ct/soup.hpp>

ct::Soup soup = ct::Soup::parse(html);

soup.title();                              // <title>, trimmed
soup.getTextTrimmed();                     // page text, whitespace collapsed

const ct::Node *node = soup.selectOne("video source[src$=.m3u8]");
if (node)
    puts(node->attr("src"));               // "" when absent; attribute() gives null

for (const ct::Node *a : soup.select("a[href], [data-hls]"))
    printf("%s %s\n", a->tag.c_str(), a->getTextTrimmed().c_str());

soup.findAll("li");                        // every descendant <li>
node->closest("table");                    // nearest ancestor
node->childElements("td");                 // element children only
node->hasClass("active");                  // whole-word class match
node->html();                              // re-serialise the subtree
```

Selectors: `tag`, `#id`, `.class`, `*`, `[attr]`, `[attr=v]`, `[attr^=v]`,
`[attr$=v]`, `[attr*=v]`, `[attr~=v]`, descendant (`a b`), child (`a > b`),
union (`a, b`), and `:first-child`, `:last-child`, `:only-child`, `:empty`,
`:not(simple)`. Attribute values may be bare, `'quoted'` or `"quoted"`.
Matching runs right to left, so `div p` does not walk every `div`'s subtree.
Results come back in document order, deduplicated across a union.

A selector that does not parse matches nothing instead of aborting, because
selectors arrive from config files and text boxes rather than from source.

Tag and attribute names fold to lowercase; attribute values do not. Entities
are decoded in text and in attribute values (`&amp;`, `&#65;`, `&#x41;`, and
the named ones pages actually use); an unknown `&foo;` is left verbatim, as
browsers leave it. `<script>` and `<style>` bodies are kept as raw text and
excluded from `getText()`.

Not implemented: `:nth-child()` and the other structural pseudo-classes,
sibling combinators (`a + b`, `a ~ b`), namespaces, and the full HTML5
tree-construction algorithm (`<table>` foster parenting, the active
formatting-elements list). Parsing allocates a node per element; there is no
arena.

[tests/test_soup.cpp](tests/test_soup.cpp) covers the malformed-input cases
above alongside the well-formed ones, since those are the ones that matter.

## Building and testing

```sh
cmake -S . -B build
cmake --build build --target ct_tests ct_headers
ctest --test-dir build --output-on-failure
```

- `ct_tests` runs the GoogleTest suite (one `test_*.cpp` per header, plus
  cross-cutting limit/overflow checks).
- `ct_headers` compiles every public header standalone to make sure none of
  them silently depends on include order.
- `ct_torture` ([tests/torture.cpp](tests/torture.cpp)) runs randomized,
  seeded stress tests against the containers; used under ASan/UBSan in CI.
- `bench/` holds standalone benchmarks (`ct_bench_*` targets) comparing
  containers against their `std::` equivalents.

CI ([.github/workflows/ci.yml](.github/workflows/ci.yml)) builds with GCC
and Clang in Debug with `-Wall -Wextra -Wpedantic -Werror`, and runs a
separate AddressSanitizer + UndefinedBehaviorSanitizer job against the test
suite and the torture test.
