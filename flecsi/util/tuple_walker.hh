/*
    @@@@@@@@  @@           @@@@@@   @@@@@@@@ @@
   /@@/////  /@@          @@////@@ @@////// /@@
   /@@       /@@  @@@@@  @@    // /@@       /@@
   /@@@@@@@  /@@ @@///@@/@@       /@@@@@@@@@/@@
   /@@////   /@@/@@@@@@@/@@       ////////@@/@@
   /@@       /@@/@@//// //@@    @@       /@@/@@
   /@@       @@@//@@@@@@ //@@@@@@  @@@@@@@@ /@@
   //       ///  //////   //////  ////////  //

   Copyright (c) 2016, Triad National Security, LLC
   All rights reserved.
                                                                              */
#pragma once

/*! @file */

#include <tuple>

namespace flecsi {
namespace util {

/*!
  Type for iterating through tuples. This type uses CRTP to expose
  visitor methods for processing the tuple arguments as they are
  traversed.

  @tparam CRTP_TYPE  The Curiously Recurring Template Pattern (CRTP) type.

  @ingroup utils
 */

template<typename CRTP_TYPE>
struct tuple_walker {
private:
  template<typename T>
  void dispatch(T & t) {
    static_cast<CRTP_TYPE &>(*this).visit(t);
  }

  template<typename T>
  void dispatch() {
    static_cast<CRTP_TYPE &>(*this).template visit_type<T>();
  }

  template<std::size_t index = 0, typename TUPLE_TYPE>
  void walk_impl(TUPLE_TYPE & t) {
    if constexpr(index < std::tuple_size<TUPLE_TYPE>::value) {
      dispatch(std::get<index>(t));
      walk_impl<index + 1>(t);
    }
  }

  template<std::size_t index = 0, typename TUPLE_TYPE>
  void walk_types_impl() {
    if constexpr(index < std::tuple_size<TUPLE_TYPE>::value) {
      using type = typename std::tuple_element<index, TUPLE_TYPE>::type;
      dispatch<type>();
      walk_types_impl<index + 1, TUPLE_TYPE>();
    }
  }

public:
  /*!
    Walk the given tuple, applying the visit method to each element.

    @tparam TUPLE_TYPE The tuple type.

    @param t An instance of the tuple.

    @ingroup utils
   */

  template<typename TUPLE_TYPE>
  void walk(TUPLE_TYPE & t) {
    walk_impl(t);
  }

  /*!
    Walk the given tuple by type, applying the visit method to each element.

    @tparam TUPLE_TYPE The tuple type.

    @ingroup utils
   */

  template<typename TUPLE_TYPE>
  void walk_types() {
    walk_types_impl<0, TUPLE_TYPE>();
  }
}; // struct tuple_walker

} // namespace util
} // namespace flecsi
