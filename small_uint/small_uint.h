#ifndef SMALL_UINT_H
#define SMALL_UINT_H

#include <stdint.h>
#include <stdexcept>

template<size_t n>
class small_uint {
private:
    template<bool cond, typename OnTrue, typename OnFalse>
    struct if_then_else {
        typedef OnTrue type;
    };

    template<typename OnTrue, typename OnFalse>
    struct if_then_else<false, OnTrue, OnFalse>  {
        typedef OnFalse type;
    };

    template<size_t i>
    struct best_type {
        typedef typename if_then_else<
            (i <= 8),
            uint8_t,
            typename if_then_else<
                (i <= 16),
                uint16_t,
                typename if_then_else<
                    (i <= 32),
                    uint32_t,
                    uint64_t
                >::type
            >::type
        >::type type;
    };
    
    template<size_t base, size_t pow>
    struct power {
        static const size_t value = base * power<base, pow - 1>::value;
    };
    
    template<size_t base>
    struct power<base, 0> {
        static const size_t value = 1;
    };
public:
    typedef typename best_type<n>::type repr_type;
    static const repr_type max_value = power<2, n>::value - 1;
    
    small_uint() : value() {}
    
    small_uint(repr_type val) {
        if(val > max_value)
            throw std::runtime_error("Value is too large");
        value = val;
    }
    
    operator repr_type() const {
        return value;
    }
private:
    repr_type value;
};

template<size_t n>
bool operator==(const small_uint<n> &lhs, const small_uint<n> &rhs) {
    return lhs.value == rhs.value;
}

template<size_t n>
bool operator!=(const small_uint<n> &lhs, const small_uint<n> &rhs) {
    return !(lhs == rhs);
}

#endif // SMALL_UINT_H
