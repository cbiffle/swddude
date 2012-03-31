#ifndef BITFIELD_H
#define BITFIELD_H

template <unsigned hi, unsigned lo>
class Bitfield {
public:
    static unsigned const bit_count = (hi - lo) + 1;

    inline Bitfield() {}

    template <typename inttype>
    inline inttype extract(inttype in) const
    {
        inttype const mask = (1 << bit_count) - 1;
        return (in >> lo) & mask;
    }
};

#endif  // BITFIELD_H
