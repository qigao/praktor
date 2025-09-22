#pragma once
#include <cmath>
#include <cstdint>
#include <iostream>

template <class T, class V>
T NarrowCast(V v) {
    auto r = static_cast<T>(v);
    if (static_cast<V>(r) != v) { throw std::runtime_error("Narrow cast failed."); }
    return r;
}

struct HumanReadable {
    std::uintmax_t size{};

private:
    friend std::ostream& operator<<(std::ostream& os, HumanReadable hr) {
        int o{};
        double mantissa = NarrowCast<double>(hr.size);
        for (; mantissa >= 1024.; mantissa /= 1024., ++o) { ; }
        os << std::ceil(mantissa * 10.) / 10. << "BKMGTPE"[o];
        return o ? os << "B (" << hr.size << ')' : os;
    }
};
