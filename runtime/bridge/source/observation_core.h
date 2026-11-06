#pragma once
#include <cstdint>
#include <limits>
#include <utility>

namespace observation {
// Used by the actual four ABI hooks as well as the native CPU test.
template<class F, class... A> auto forward_once(F f, A&&... a)
    -> decltype(f(std::forward<A>(a)...)) {
    return f(std::forward<A>(a)...);
}
inline uint64_t element_count(unsigned type, unsigned primitives) {
    switch (type) {
    case 1: return primitives;
    case 2: return uint64_t(primitives)*2;
    case 3: return primitives ? uint64_t(primitives)+1 : 0;
    case 4: return uint64_t(primitives)*3;
    case 5: case 6: return primitives ? uint64_t(primitives)+2 : 0;
    default: return 0;
    }
}
inline bool span(uint64_t size, uint64_t offset, uint64_t length) {
    return offset <= size && length <= size-offset;
}
inline bool vertex_offset(uint64_t size, unsigned offset, unsigned stride,
                          int base, unsigned index, uint64_t& out) {
    const int64_t vertex = int64_t(base)+index;
    if (!stride || stride > 128 || vertex < 0) return false;
    const uint64_t n = uint64_t(vertex);
    if (n > (std::numeric_limits<uint64_t>::max()-offset)/stride) return false;
    out = offset+n*stride;
    return span(size, out, stride);
}
struct Gate {
    unsigned draws = 0, frames = 0;
    bool selected = false;
    bool take(bool armed, bool spatial) {
        if (!armed || !spatial || draws >= 32 || (!selected && frames >= 2)) return false;
        if (!selected) { selected = true; ++frames; }
        ++draws;
        return true;
    }
    void present() { selected = false; }
    bool done() const { return frames >= 2 || draws >= 32; }
};
}
