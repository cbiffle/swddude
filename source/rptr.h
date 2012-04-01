#ifndef RPTR_H
#define RPTR_H

/*
 * rptr and rptr_const represent memory addresses in a remote system --
 * read/write and read-only addresses, respectively.  They behave like normal
 * pointers in most respects (arithmetic, comparison, etc.), but cannot be
 * dereferenced except through a Target implementation.
 *
 * Note: Using these types to reference structs can be dangerous.  The struct
 * must be carefully designed to have the same size/alignment on both the host
 * and target for sizeof (and thus arithmetic) to work.
 */

#include <stdint.h>
#include <stddef.h>


/*
 * Base type giving common pointer-like functionality.  Uses the Curiously
 * Recurring Template Pattern to provide covariant operators.
 *
 * This is designed to inline away and become roughly as efficient as an int.
 * Subclasses should attempt to respect this.
 */
template <typename type, typename self> class rptr_base
{
    uint32_t _bits;

protected:
    explicit inline rptr_base(uint32_t bits) : _bits(bits) {}

public:
    inline self const & operator=(self const &other)
    {
        _bits = other._bits;
    }

    inline uint32_t bits() const { return _bits; }

    inline size_t size() const { return sizeof(type); }

    /*
     * Returns a single bit of the address.  This version should be used when
     * the bit index is known at compile time, because it will detect mistakes
     * like shifting by 32 or more bits.
     */
    template <unsigned bit_index>
    inline bool bit() const { return (_bits >> bit_index) & 1; }


    /***************************************************************************
     * Comparison
     */

    inline bool operator==(self const & other) const
    {
        return _bits == other._bits;
    }

    inline bool operator!=(self const & other) const
    {
        return _bits != other._bits;
    }

    inline bool operator<(self const & other) const
    {
        return _bits < other._bits;
    }

    inline bool operator>(self const & other) const
    {
        return _bits < other._bits;
    }

    inline bool operator>=(self const & other) const
    {
        return _bits >= other._bits;
    }

    inline bool operator<=(self const & other) const
    {
        return _bits <= other._bits;
    }

    /***************************************************************************
     * Arithmetic - pointer style.  Like native pointers, rptrs "move" during
     * arithmetic in integral units of the size of the type they reference.
     */

    inline self operator+(int d) const
    {
        return self(_bits + d * sizeof(type));
    }

    inline self operator++() {
        _bits += sizeof(type);
        return *(self *) this;
    }

    inline self operator-(int d) const
    {
        return self(_bits - d * sizeof(type));
    }

    inline self operator--() const { return *this - 1; }
};


/*******************************************************************************
 * Remote pointer to constant (read-only) data.
 */
template <typename type>
class rptr_const : public rptr_base<type, rptr_const<type> >
{
public:
    explicit inline rptr_const(uint32_t bits) :
        rptr_base<type, rptr_const<type> >(bits) {}

    /*
     * Allow explicit casts between rptr_consts of different types.
     */
    template <typename U>
    explicit inline rptr_const<type>(const rptr_const<U> & other) :
        rptr_base<type, rptr_const<type> >(other.bits()) {}
};


/*******************************************************************************
 * Remote pointer to non-constant (read-write) data.
 */
template <typename type>
class rptr : public rptr_base<type, rptr<type> >
{
public:
    explicit inline rptr(uint32_t bits) : rptr_base<type, rptr<type> >(bits) {}

    /*
     * Allow explicit casts between rptrs of different types.
     */
    template <typename other_type>
    explicit inline rptr<type>(const rptr<other_type> & other) :
        rptr_base<type, rptr<type> >(other.bits()) {}

    /*
     * Allow rptr<type> to be used in place of rptr_const<type>.
     */
    inline operator rptr_const<type>() const
    {
        return rptr_const<type>(this->bits());
    }
};

#endif  // RPTR_H
