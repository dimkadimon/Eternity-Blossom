// Eternity-Blossom -- minimal stand-in for boost::container::small_vector.
//
// SPDX-License-Identifier: MIT
//
// Blossom VI includes <boost/container/small_vector.hpp>.  Installing all of
// Boost just for that one header is a heavy dependency for a puzzle solver (and
// impossible without root in a bare container), so when CMake cannot find Boost
// it adds this directory to the include path instead.
//
// Functionally this is std::vector with an ignored inline-capacity parameter:
// the small-buffer optimisation is a constant-factor win for Blossom VI, not a
// correctness requirement.  If you have Boost installed, CMake will prefer the
// real header and this file is never compiled.

#ifndef ETERNITY_BLOSSOM_BOOST_SHIM_SMALL_VECTOR_HPP
#define ETERNITY_BLOSSOM_BOOST_SHIM_SMALL_VECTOR_HPP

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <vector>

namespace boost {
namespace container {

template <class T, std::size_t InlineCapacity, class Allocator = std::allocator<T>>
class small_vector : public std::vector<T, Allocator> {
    using base = std::vector<T, Allocator>;

public:
    using base::base;
    small_vector() = default;
    small_vector(std::initializer_list<T> init) : base(init) {}
    small_vector(std::size_t n, const T& value) : base(n, value) {}
    template <class It>
    small_vector(It first, It last) : base(first, last) {}
};

}  // namespace container
}  // namespace boost

#endif  // ETERNITY_BLOSSOM_BOOST_SHIM_SMALL_VECTOR_HPP
