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

/*!
  @file

  This file contains implementations of field accessor types.
 */

#include "flecsi/execution.hh"
#include "flecsi/topo/size.hh"
#include "flecsi/util/array_ref.hh"
#include <flecsi/data/field.hh>

#include <algorithm>
#include <iterator>
#include <memory>
#include <stack>

namespace flecsi {
namespace data {

namespace detail {
template<bool Span = true, class A>
void
construct(const A & a) {
  if constexpr(Span)
    construct<false>(a.span());
  else
    std::uninitialized_default_construct(a.begin(), a.end());
}
template<class T, layout L, Privileges P, bool Span>
void
destroy_task(typename field<T, L>::template accessor1<P> a) {
  const auto && s = [&] {
    if constexpr(Span)
      return a.span();
    else
      return a;
  }();
  std::destroy(s.begin(), s.end());
}
template<Privileges P,
  bool Span = true,
  class T,
  layout L,
  class Topo,
  typename Topo::index_space S>
void
destroy(const field_reference<T, L, Topo, S> & r) {
  execute<destroy_task<T, L, privilege_repeat<rw, privilege_count(P)>, Span>>(
    r);
}
template<class T>
inline constexpr bool forward_v = std::is_base_of_v<std::forward_iterator_tag,
  typename std::iterator_traits<T>::iterator_category>;
template<class T, Privileges P>
using element_t = std::conditional_t<privilege_write(P), T, const T>;
template<class T, Privileges P, bool M>
using particle_raw =
  typename field<T, data::particle>::base_type::template accessor1<
    !M && get_privilege(0, P) == wo ? privilege_pack<rw> : P>;
} // namespace detail

// All accessors are ultimately implemented in terms of those for the raw
// layout, minimizing the amount of backend-specific code required.

template<typename DATA_TYPE, Privileges PRIVILEGES>
struct accessor<single, DATA_TYPE, PRIVILEGES> : bind_tag, send_tag {
  using value_type = DATA_TYPE;
  // We don't actually inherit from base_type; we don't want its interface.
  using base_type = accessor<dense, DATA_TYPE, PRIVILEGES>;
  using element_type = typename base_type::element_type;

  explicit accessor(std::size_t s) : base(s) {}
  accessor(const base_type & b) : base(b) {}

  element_type & get() const {
    return base(0);
  } // data
  operator element_type &() const {
    return get();
  } // value

  const accessor & operator=(const DATA_TYPE & value) const {
    return const_cast<accessor &>(*this) = value;
  } // operator=
  accessor & operator=(const DATA_TYPE & value) {
    get() = value;
    return *this;
  } // operator=

  element_type operator->() const {
    static_assert(
      std::is_pointer<value_type>::value, "-> called on non-pointer type");
    return get();
  } // operator->

  base_type & get_base() {
    return base;
  }
  const base_type & get_base() const {
    return base;
  }
  template<class F>
  void send(F && f) {
    f(get_base(), [](const auto & r) { return r.template cast<dense>(); });
  }

private:
  base_type base;
}; // struct accessor

template<typename DATA_TYPE, Privileges PRIVILEGES>
struct accessor<raw, DATA_TYPE, PRIVILEGES> : bind_tag {
  using value_type = DATA_TYPE;
  using element_type = detail::element_t<DATA_TYPE, PRIVILEGES>;

  explicit accessor(field_id_t f) : f(f) {}

  field_id_t field() const {
    return f;
  }

  FLECSI_INLINE_TARGET
  auto span() const {
    return s;
  }

  void bind(util::span<element_type> x) { // for bind_accessors
    s = x;
  }

private:
  field_id_t f;
  util::span<element_type> s;
}; // struct accessor

template<class T, Privileges P>
struct accessor<dense, T, P> : accessor<raw, T, P>, send_tag {
  using base_type = accessor<raw, T, P>;
  using base_type::base_type;
  using size_type = typename decltype(base_type(0).span())::size_type;

  accessor(const base_type & b) : base_type(b) {}

  /*!
    Provide logical array-based access to the data referenced by this
    accessor.

    @param index The index of the logical array to access.
   */
  FLECSI_INLINE_TARGET
  typename accessor::element_type & operator()(size_type index) const {
    const auto s = this->span();
    flog_assert(index < s.size(), "index out of range");
    return s[index];
  } // operator()

  FLECSI_INLINE_TARGET
  typename accessor::element_type & operator[](size_type index) const {
    return this->span()[index];
  }

  base_type & get_base() {
    return *this;
  }
  const base_type & get_base() const {
    return *this;
  }
  template<class F>
  void send(F && f) {
    f(get_base(), [](const auto & r) {
      // TODO: use just one task for all fields
      if constexpr(privilege_discard(P) && !std::is_trivially_destructible_v<T>)
        r.get_region().cleanup(r.fid(), [r] { detail::destroy<P>(r); });
      return r.template cast<raw>();
    });
    if constexpr(privilege_discard(P))
      detail::construct(*this); // no-op on caller side
  }
};

// The offsets privileges are separate because they are writable for mutators
// but read-only for even writable accessors.
template<class T, Privileges P, Privileges OP = P>
struct ragged_accessor
  : accessor<raw, T, P>,
    send_tag,
    util::with_index_iterator<const ragged_accessor<T, P, OP>> {
  using base_type = typename ragged_accessor::accessor;
  using typename base_type::element_type;
  using Offsets = accessor<dense, std::size_t, OP>;
  using Offset = typename Offsets::value_type;
  using size_type = typename Offsets::size_type;
  using row = util::span<element_type>;

  using base_type::base_type;
  ragged_accessor(const base_type & b) : base_type(b) {}

  row operator[](size_type i) const {
    // Without an extra element, we must store one endpoint implicitly.
    // Storing the end usefully ignores any overallocation.
    return this->span().first(off(i)).subspan(i ? off(i - 1) : 0);
  }
  size_type size() const noexcept { // not total!
    return off.span().size();
  }
  Offset total() const noexcept {
    const auto s = off.span();
    return s.empty() ? 0 : s.back();
  }

  util::span<element_type> span() const {
    return get_base().span().first(total());
  }

  base_type & get_base() {
    return *this;
  }
  const base_type & get_base() const {
    return *this;
  }

  Offsets & get_offsets() {
    return off;
  }
  const Offsets & get_offsets() const {
    return off;
  }
  template<class F>
  void send(F && f) {
    f(get_base(), [](const auto & r) {
      using R = std::remove_reference_t<decltype(r)>;
      const field_id_t i = r.fid();
      r.get_region().template ghost_copy<P>(r);
      auto & t = r.topology().ragged;
      r.get_region(t).cleanup(
        i,
        [=] {
          if constexpr(!std::is_trivially_destructible_v<T>)
            detail::destroy<P>(r);
        },
        privilege_discard(P));
      // Resize after the ghost copy (which can add elements and can perform
      // its own resize) rather than in the mutator before getting here:
      if constexpr(privilege_write(OP))
        r.get_partition(r.topology().ragged).resize();
      // We rely on the fact that field_reference uses only the field ID.
      return field_reference<T,
        raw,
        topo::ragged<typename R::Topology>,
        R::space>({i, 0, {}}, t);
    });
    f(get_offsets(), [](const auto & r) {
      // Disable normal ghost copy of offsets:
      r.get_region().template ghost<privilege_pack<wo, wo>>(r.fid());
      return r.template cast<dense, Offset>();
    });
    if constexpr(privilege_discard(P))
      detail::construct(*this); // no-op on caller side
  }

  template<class Topo, typename Topo::index_space S>
  static ragged_accessor parameter(
    const field_reference<T, data::ragged, Topo, S> & r) {
    return exec::replace_argument<base_type>(r.template cast<data::raw>());
  }

private:
  Offsets off{this->field()};
};

template<class T, Privileges P>
struct accessor<ragged, T, P>
  : ragged_accessor<T, P, privilege_repeat<ro, privilege_count(P)>> {
  using accessor::ragged_accessor::ragged_accessor;
};

template<class T, Privileges P>
struct mutator<ragged, T, P>
  : bind_tag, send_tag, util::with_index_iterator<const mutator<ragged, T, P>> {
  static_assert(privilege_write(P) && !privilege_discard(P),
    "mutators require read/write permissions");
  using base_type = ragged_accessor<T, P>;
  using size_type = typename base_type::size_type;

private:
  using base_row = typename base_type::row;
  using base_size = typename base_row::size_type;

public:
  struct Overflow {
    base_size del;
    std::vector<T> add;
  };
  using TaskBuffer = std::vector<Overflow>;

private:
  struct raw_row {
    using size_type = base_size;

    base_row s;
    Overflow * o;

    T & operator[](size_type i) const {
      const auto b = brk();
      if(i < b)
        return s[i];
      i -= b;
      flog_assert(i < o->add.size(), "index out of range");
      return o->add[i];
    }

    size_type brk() const noexcept {
      return s.size() - o->del;
    }
    size_type size() const noexcept {
      return brk() + o->add.size();
    }

    void destroy(size_type skip = 0) const {
      std::destroy(s.begin() + skip, s.begin() + brk());
    }
  };

public:
  struct row : util::with_index_iterator<const row>, private raw_row {
    using value_type = T;
    using typename raw_row::size_type;
    using difference_type = typename base_row::difference_type;
    using typename row::with_index_iterator::iterator;

    row(const raw_row & r) : raw_row(r) {}

    void assign(size_type n, const T & t) const {
      clear();
      resize(n, t);
    }
    template<class I, class = std::enable_if_t<!std::is_integral_v<I>>>
    void assign(I a, I b) const {
      clear();
      if constexpr(detail::forward_v<I>) {
        for(auto n = std::min(std::distance(a, b), s.size()); n--; ++a)
          push_span(*a);
        o->add.assign(a, b);
      }
      else
        while(a != b)
          push_back(*a++);
    }
    void assign(std::initializer_list<T> l) const {
      assign(l.begin(), l.end());
    }

    using raw_row::operator[];
    T & front() const {
      return *this->begin();
    }
    T & back() const {
      return this->end()[-1];
    }

    using raw_row::size;
    bool empty() const noexcept {
      return !this->size();
    }
    size_type max_size() const noexcept {
      return o->add.max_size();
    }
    size_type capacity() const noexcept {
      // We can't count the span and the vector, since the client might remove
      // all the elements from the span and then add to the vector up to our
      // return value.  The strange result is
      // that size() can be greater than capacity(), but that just means that
      // every operation might invalidate everything.
      const auto c = o->add.capacity();
      return o->add.empty() ? std::max(s.size(), c) : c;
    }
    void reserve(size_type n) const {
      if(!o->add.empty() || n > s.size())
        o->add.reserve(n);
    }
    void shrink_to_fit() const { // repacks into span; only basic guarantee
      const size_type mv = std::min(o->add.size(), o->del);
      const auto b = o->add.begin(), e = b + mv;
      std::uninitialized_move(b, e, s.end());
      o->del -= mv;
      if(mv || o->add.size() < o->add.capacity())
        decltype(o->add)(
          std::move_iterator(e), std::move_iterator(o->add.end()))
          .swap(o->add);
    }

    void clear() const noexcept {
      o->add.clear();
      for(auto n = brk(); n--;)
        pop_span();
    }
    iterator insert(iterator i, const T & t) const {
      return put(i, t);
    }
    iterator insert(iterator i, T && t) const {
      return put(i, std::move(t));
    }
    iterator insert(iterator i, size_type n, const T & t) const {
      const auto b = brki();
      auto & a = o->add;
      if(i < b) {
        const size_type
          // used elements assigned from t:
          filla = std::min(n, b.index() - i.index()),
          fillx = n - filla, // other spaces to fill
          fillc = std::min(fillx, o->del), // slack spaces constructed from t
          fillo = fillx - fillc, // overflow copies of t
          // slack spaces move-constructed from used elements:
          mvc = std::min(o->del - fillc, filla);

        // Perform the moves and fills mostly in reverse order.
        T *const s0 = s.data(), *const ip = s0 + i.index(),
                 *const bp = s0 + b.index();
        // FIXME: Is there a way to do just one shift?
        a.insert(a.begin(), fillo, t); // first to reduce the volume of shifting
        a.insert(a.begin() + fillo, bp - (filla - mvc), bp);
        std::uninitialized_move_n(
          bp - filla, mvc, std::uninitialized_fill_n(bp, fillc, t));
        o->del -= fillc + mvc; // before copies that might throw
        std::fill(ip, std::move_backward(ip, bp - filla, bp), t);
      }
      else {
        if(i == b)
          for(; n && o->del; --n) // fill in the gap
            push_span(t);
        a.insert(a.begin(), n, t);
      }
      return i;
    }
    template<class I, class = std::enable_if_t<!std::is_integral_v<I>>>
    iterator insert(iterator i, I a, I b) const {
      // FIXME: use size when available
      for(iterator j = i; a != b; ++a, ++j)
        insert(j, *a);
      return i;
    }
    iterator insert(iterator i, std::initializer_list<T> l) const {
      return insert(i, l.begin(), l.end());
    }
    template<class... AA>
    iterator emplace(iterator i, AA &&... aa) const {
      return put<true>(i, std::forward<AA>(aa)...);
    }
    iterator erase(iterator i) const noexcept {
      const auto b = brki();
      if(i < b) {
        std::move(i + 1, b, i);
        pop_span();
      }
      else
        o->add.erase(o->add.begin() + (i - b));
      return i;
    }
    iterator erase(iterator i, iterator j) const noexcept {
      const auto b = brki();
      if(i < b) {
        if(j == i)
          ;
        else if(j <= b) {
          std::move(j, b, i);
          for(auto n = j - i; n--;)
            pop_span();
        }
        else {
          erase(b, j);
          erase(i, b);
        }
      }
      else {
        const auto ab = o->add.begin();
        o->add.erase(ab + (i - b), ab + (j - b));
      }
      return i;
    }
    void push_back(const T & t) const {
      emplace_back(t);
    }
    void push_back(T && t) const {
      emplace_back(std::move(t));
    }
    template<class... AA>
    T & emplace_back(AA &&... aa) const {
      return !o->del || !o->add.empty()
               ? o->add.emplace_back(std::forward<AA>(aa)...)
               : push_span(std::forward<AA>(aa)...);
    }
    void pop_back() const noexcept {
      if(o->add.empty())
        pop_span();
      else
        o->add.pop_back();
    }
    void resize(size_type n) const {
      extend(n);
    }
    void resize(size_type n, const T & t) const {
      extend(n, t);
    }
    // No swap: it would swap the handles, not the contents

  private:
    using raw_row::brk;
    using raw_row::o;
    using raw_row::s;

    auto brki() const noexcept {
      return this->begin() + brk();
    }
    template<bool Destroy = false, class... AA>
    iterator put(iterator i, AA &&... aa) const {
      static_assert(Destroy || sizeof...(AA) == 1);
      const auto b = brki();
      auto & a = o->add;
      if(i < b) {
        auto & last = s[brk() - 1];
        if(o->del)
          push_span(std::move(last));
        else
          a.insert(a.begin(), std::move(last));
        std::move_backward(i, b - 1, b);
        if constexpr(Destroy) {
          auto * p = &*i;
          p->~T();
          new(p) T(std::forward<AA>(aa)...);
        }
        else
          *i = (std::forward<AA>(aa), ...);
      }
      else if(i == b && o->del)
        push_span(std::forward<AA>(aa)...);
      else {
        const auto j = a.begin() + (i - b);
        if constexpr(Destroy)
          a.emplace(j, std::forward<AA>(aa)...);
        else
          a.insert(j, std::forward<AA>(aa)...);
      }
      return i;
    }
    template<class... U> // {} or {const T&}
    void extend(size_type n, U &&... u) const {
      auto sz = this->size();
      if(n <= sz) {
        while(sz-- > n)
          pop_back();
      }
      else {
        // We can reduce the reservation because we're only appending:
        if(const auto sc = o->add.empty() ? s.size() : brk(); n > sc)
          o->add.reserve(n - sc);

        struct cleanup {
          const row & r;
          size_type sz0;
          bool fail = true;
          ~cleanup() {
            if(fail)
              r.resize(sz0);
          }
        } guard = {*this, sz};

        while(sz++ < n)
          emplace_back(std::forward<U>(u)...);
        guard.fail = false;
      }
    }
    template<class... AA>
    T & push_span(AA &&... aa) const {
      auto & ret = *new(&s[brk()]) T(std::forward<AA>(aa)...);
      --o->del;
      return ret;
    }
    void pop_span() const noexcept {
      ++o->del;
      s[brk()].~T();
    }
  };

  mutator(const base_type & b, const topo::resize::policy & p)
    : acc(b), grow(p) {}

  row operator[](size_type i) const {
    return raw_get(i);
  }
  size_type size() const noexcept {
    return acc.size();
  }

  base_type & get_base() {
    return acc;
  }
  const base_type & get_base() const {
    return acc;
  }
  auto & get_size() {
    return sz;
  }
  const topo::resize::policy & get_grow() const {
    return grow;
  }
  void buffer(TaskBuffer & b) { // for unbind_accessors
    over = &b;
  }
  template<class F>
  void send(F && f) {
    f(get_base(), util::identity());
    f(get_size(), [](const auto & r) {
      return r.get_partition(r.topology().ragged).sizes();
    });
    if(over)
      over->resize(acc.size()); // no-op on caller side
  }

  void commit() const {
    // To move each element before overwriting it, we propagate moves outward
    // from each place where the movement switches from rightward to leftward.
    const auto all = acc.get_base().span();
    const size_type n = size();
    // Read and write cursors.  It would be possible, if ugly, to run cursors
    // backwards for the rightward-moving portions and do without the stack.
    size_type is = 0, id = 0;
    base_size js = 0, jd = 0;
    raw_row rs = raw_get(is), rd = raw_get(id);
    struct work {
      void operator()() const {
        if(assign)
          *w = std::move(*r);
        else
          new(w) T(std::move(*r));
      }
      T *r, *w;
      bool assign;
    };
    std::stack<work> stk;
    const auto back = [&stk] {
      for(; !stk.empty(); stk.pop())
        stk.top()();
    };
    for(;; ++js, ++jd) {
      while(js == rs.size()) { // js indexes the split row
        if(++is == n)
          break;
        rs = raw_get(is);
        js = 0;
      }
      if(is == n)
        break;
      while(jd == rd.s.size()) { // jd indexes the span (including slack space)
        if(++id == n)
          break; // we can write off the end of the last
        else
          rd = raw_get(id);
        jd = 0;
      }
      const auto r = &rs[js], w = rd.s.data() + jd;
      flog_assert(w != all.end(),
        "ragged entries for last " << n - is << " rows overrun allocation for "
                                   << all.size() << " entries");
      if(r != w)
        stk.push({r, w, jd < rd.brk()});
      // Perform this move, and any already queued, if it's not to the right.
      // Offset real elements by one position to come after insertions.
      if(rs.s.data() + std::min(js + 1, rs.brk()) > w)
        back();
    }
    back();
    if(jd < rd.brk())
      rd.destroy(jd);
    while(++id < n)
      raw_get(id).destroy();
    auto & off = acc.get_offsets();
    base_size delta = 0; // may be "negative"; aliasing is implausible
    for(is = 0; is < n; ++is) {
      auto & ov = (*over)[is];
      off(is) += delta += ov.add.size() - ov.del;
      ov.add.clear();
    }
    sz = grow(acc.total(), all.size());
  }

private:
  raw_row raw_get(size_type i) const {
    return {get_base()[i], &(*over)[i]};
  }

  base_type acc;
  topo::resize::accessor<wo> sz;
  topo::resize::policy grow;
  TaskBuffer * over = nullptr;
};

// Many compilers incorrectly require the 'template' for a base class.
template<class T, Privileges P>
struct accessor<sparse, T, P>
  : field<T, sparse>::base_type::template accessor1<P>,
    util::with_index_iterator<const accessor<sparse, T, P>> {
  static_assert(!privilege_discard(P),
    "sparse accessor requires read permission");

private:
  using Field = field<T, sparse>;
  using FieldBase = typename Field::base_type;

public:
  using value_type = typename FieldBase::value_type;
  using base_type = typename FieldBase::template accessor1<P>;
  using element_type =
    std::tuple_element_t<1, typename base_type::element_type>;

private:
  using base_row = typename base_type::row;

public:
  // Use the ragged interface to obtain the span directly.
  struct row {
    using key_type = typename Field::key_type;
    row(base_row s) : s(s) {}
    element_type & operator()(key_type c) const {
      return std::partition_point(
        s.begin(), s.end(), [c](const value_type & v) { return v.first < c; })
        ->second;
    }

  private:
    base_row s;
  };

  using base_type::base_type;
  accessor(const base_type & b) : base_type(b) {}

  row operator[](typename accessor::size_type i) const {
    return get_base()[i];
  }

  base_type & get_base() {
    return *this;
  }
  const base_type & get_base() const {
    return *this;
  }
  template<class F>
  void send(F && f) {
    f(get_base(),
      [](const auto & r) { return r.template cast<ragged, value_type>(); });
  }
};

template<class T, Privileges P>
struct mutator<sparse, T, P>
  : bind_tag, send_tag, util::with_index_iterator<const mutator<sparse, T, P>> {
private:
  using Field = field<T, sparse>;

public:
  using base_type = typename Field::base_type::template mutator1<P>;
  using size_type = typename base_type::size_type;
  using TaskBuffer = typename base_type::TaskBuffer;

private:
  using base_row = typename base_type::row;
  using base_iterator = typename base_row::iterator;

public:
  struct row {
    using key_type = typename Field::key_type;
    using value_type = typename base_row::value_type;
    using size_type = typename base_row::size_type;

    // NB: invalidated by insertions/deletions, unlike std::map::iterator.
    struct iterator {
    public:
      using value_type = std::pair<const key_type &, T &>;
      using difference_type = std::ptrdiff_t;
      using reference = value_type;
      using pointer = void;
      // We could easily implement random access, but std::map doesn't.
      using iterator_category = std::bidirectional_iterator_tag;

      iterator(base_iterator i = {}) : i(i) {}

      value_type operator*() const {
        return {i->first, i->second};
      }

      iterator & operator++() {
        ++i;
        return *this;
      }
      iterator operator++(int) {
        iterator ret = *this;
        ++*this;
        return ret;
      }
      iterator & operator--() {
        --i;
        return *this;
      }
      iterator operator--(int) {
        iterator ret = *this;
        --*this;
        return ret;
      }

      bool operator==(const iterator & o) const {
        return i == o.i;
      }
      bool operator!=(const iterator & o) const {
        return i != o.i;
      }

      base_iterator get_base() const {
        return i;
      }

    private:
      base_iterator i;
    };

    row(base_row r) : r(r) {}

    T & operator[](key_type c) const {
      return try_emplace(c).first->second;
    }

    iterator begin() const noexcept {
      return r.begin();
    }
    iterator end() const noexcept {
      return r.end();
    }

    bool empty() const noexcept {
      return r.empty();
    }
    size_type size() const noexcept {
      return r.size();
    }
    size_type max_size() const noexcept {
      return r.max_size();
    }

    void clear() const noexcept {
      r.clear();
    }
    std::pair<iterator, bool> insert(const value_type & p) const {
      auto [i, hit] = lookup(p.first);
      if(!hit)
        i = r.insert(i, p); // assignment is no-op
      return {i, !hit};
    }
    // TODO: insert(U&&), insert(value_type&&)
    template<class I>
    void insert(I a, I b) const {
      for(; a != b; ++a)
        insert(*a);
    }
    void insert(std::initializer_list<value_type> l) const {
      insert(l.begin(), l.end());
    }
    template<class U>
    std::pair<iterator, bool> insert_or_assign(key_type c, U && u) const {
      auto [i, hit] = lookup(c);
      if(hit)
        i->second = std::forward<U>(u);
      else
        i = r.insert(i, {c, std::forward<U>(u)}); // assignment is no-op
      return {i, !hit};
    }
    // We don't support emplace since we can't avoid moving the result.
    template<class... AA>
    std::pair<iterator, bool> try_emplace(key_type c, AA &&... aa) const {
      auto [i, hit] = lookup(c);
      if(!hit)
        i = r.insert(i,
          {std::piecewise_construct,
            std::make_tuple(c),
            std::forward_as_tuple(
              std::forward<AA>(aa)...)}); // assignment is no-op
      return {i, !hit};
    }

    iterator erase(iterator i) const {
      return r.erase(i.get_base());
    }
    iterator erase(iterator i, iterator j) const {
      return r.erase(i.get_base(), j.get_base());
    }
    size_type erase(key_type c) const {
      const auto [i, hit] = lookup(c);
      if(hit)
        r.erase(i);
      return hit;
    }
    // No swap: it would swap the handles, not the contents

    size_type count(key_type c) const {
      return lookup(c).second;
    }
    iterator find(key_type c) const {
      const auto [i, hit] = lookup(c);
      return hit ? i : end();
    }
    std::pair<iterator, iterator> equal_range(key_type c) const {
      const auto [i, hit] = lookup(c);
      return {i, i + hit};
    }
    iterator lower_bound(key_type c) const {
      return lower(c);
    }
    iterator upper_bound(key_type c) const {
      const auto [i, hit] = lookup(c);
      return i + hit;
    }

  private:
    base_iterator lower(key_type c) const {
      return std::partition_point(
        r.begin(), r.end(), [c](const value_type & v) { return v.first < c; });
    }
    std::pair<base_iterator, bool> lookup(key_type c) const {
      const auto i = lower(c);
      return {i, i != r.end() && i->first == c};
    }

    // We simply keep the (ragged) row sorted; this avoids the complexity of
    // two lookaside structures and is efficient for small numbers of inserted
    // elements and for in-order initialization.
    base_row r;
  };

  mutator(const base_type & b) : rag(b) {}

  row operator[](size_type i) const {
    return get_base()[i];
  }
  size_type size() const noexcept {
    return rag.size();
  }

  base_type & get_base() {
    return rag;
  }
  const base_type & get_base() const {
    return rag;
  }
  template<class F>
  void send(F && f) {
    f(get_base(), [](const auto & r) {
      return r.template cast<ragged, typename base_row::value_type>();
    });
  }
  void buffer(TaskBuffer & b) { // for unbind_accessors
    rag.buffer(b);
  }

  void commit() const {
    rag.commit();
  }

private:
  base_type rag;
};

template<class T, Privileges P, bool M>
struct particle_accessor : detail::particle_raw<T, P, M>, send_tag {
  static_assert(privilege_count(P) == 1, "particles cannot be ghosts");
  using base_type = detail::particle_raw<T, P, M>;
  using size_type = typename decltype(base_type(0).span())::size_type;
  using Particle = typename base_type::value_type;
  // Override base class aliases:
  using value_type = T;
  using element_type = detail::element_t<T, P>;

  struct iterator {
    using reference = element_type &;
    using value_type = element_type;
    using pointer = element_type *;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::bidirectional_iterator_tag;

    iterator() noexcept : iterator(nullptr, 0) {}
    iterator(const particle_accessor * a, size_type i) : a(a), i(i) {}

    reference operator*() const {
      return a->span()[i].data;
    }
    pointer operator->() const {
      return &**this;
    }

    iterator & operator++() {
      const auto s = a->span();
      if(++i != s.size())
        i += s[i].skip;
      return *this;
    }
    iterator operator++(int) {
      iterator ret = *this;
      ++*this;
      return ret;
    }
    iterator & operator--() {
      if(--i)
        i -= a->span()[i].skip;
      return *this;
    }
    iterator operator--(int) {
      iterator ret = *this;
      --*this;
      return ret;
    }

    bool operator==(const iterator & o) const {
      return i == o.i;
    }
    bool operator!=(const iterator & o) const {
      return i != o.i;
    }
    bool operator<(const iterator & o) const {
      return i < o.i;
    }
    bool operator<=(const iterator & o) const {
      return i <= o.i;
    }
    bool operator>(const iterator & o) const {
      return i > o.i;
    }
    bool operator>=(const iterator & o) const {
      return i >= o.i;
    }

    size_type location() const noexcept {
      return i;
    }

  private:
    const particle_accessor * a;
    size_type i;
  };

  using base_type::base_type;
  particle_accessor(const base_type & b) : base_type(b) {}

  // This interface is a subset of that proposed for std::hive.

  size_type size() const {
    const auto s = this->span();
    const auto n = s.size();
    const auto i = n ? s.front().skip : 0;
    return i == n ? n : s[i].free.prev;
  }
  size_type capacity() const {
    return this->span().size();
  }
  bool empty() const {
    return !size();
  }

  iterator begin() const {
    const auto s = this->span();
    return {this, s.empty() || s.front().skip ? 0 : 1 + first_skip()};
  }
  iterator end() const {
    return {this, capacity()};
  }

  iterator get_iterator_from_pointer(element_type * the_pointer) const {
    static_assert(std::is_standard_layout_v<Particle>);
    const auto * const p = reinterpret_cast<Particle *>(the_pointer);
    const auto ret = p - this->span().data();
    // Some skipfield values are unused, so this isn't reliable:
    flog_assert(!p->skip != !ret, "field slot is empty");
    return iterator(this, ret);
  }

  base_type & get_base() {
    return *this;
  }
  const base_type & get_base() const {
    return *this;
  }

  template<class F>
  void send(F && f) {
    std::forward<F>(f)(get_base(), [](const auto & r) {
      if constexpr(privilege_discard(P) && !std::is_trivially_destructible_v<T>)
        r.get_region().cleanup(r.fid(), [r] { detail::destroy<P, false>(r); });
      return r.template cast<raw, Particle>();
    });
  }

protected:
  size_type first_skip() const { // after special first element
    const auto s = this->span();
    return s.size() == 1 ? 0 : s[1].skip;
  }
};

template<class T, Privileges P>
struct accessor<particle, T, P> : particle_accessor<T, P, false> {
  using accessor::particle_accessor::particle_accessor;

  template<class F>
  void send(F && f) {
    accessor::particle_accessor::send(std::forward<F>(f));
    if constexpr(privilege_discard(P))
      detail::construct<false>(*this); // no-op on caller side
  }
};

template<class T, Privileges P>
struct mutator<particle, T, P> : particle_accessor<T, P, true> {
  using base_type = particle_accessor<T, P, true>;
  using typename base_type::iterator;
  using typename base_type::value_type;
  using Skip = typename base_type::base_type::value_type::size_type;

  // We don't support automatic resizing since we don't control the region.
  // TODO: Support manual resizing (by updating the skipfield appropriately)
  using base_type::base_type;

  // Since, by design, the skipfield data structure requires no lookaside
  // tables, accessor could provide it instead of having this class at all.
  // However, the natural wo semantics differ between the two cases.

  void clear() const {
    std::destroy(this->begin(), this->end());
    init();
  }

  iterator insert(const value_type & v) const {
    return emplace(v);
  }
  iterator insert(value_type && v) const {
    return emplace(std::move(v));
  }
  /// Create an element, in constant time.
  /// \return an iterator to the new element
  template<class... AA>
  iterator emplace(AA &&... aa) const {
    const auto s = this->span();
    const auto n = s.size();
    flog_assert(n, "no particle space");
    Skip &ptr = s.front().skip, ret = ptr;
    flog_assert(ret != n, "too many particles");
    auto * const head = &s[ret];
    Skip & skip = head->skip;
    const auto link = head->emplace(std::forward<AA>(aa)...);
    if(!ret || skip == 1) // erasing a run
      ptr = link.next;
    else { // shrinking a run
      auto & h2 = s[++ptr];
      h2.skip = head[skip - 1].skip = skip - 1;
      h2.free.next = link.next;
    }
    if(ret)
      skip = 0;
    if(ptr != n) // update size
      s[ptr].free.prev = link.prev + 1;
    return {this, ret};
  }

  /// Remove an element, in constant time.
  /// \return an iterator past the removed element
  iterator erase(const iterator & it) const {
    const auto s = this->span();
    const auto n = s.size();
    flog_assert(n, "no particles to erase");
    Skip & ptr = s.front().skip;
    const typename mutator::size_type i = it.location();
    auto * const rm = &s[i];

    const Skip end = i > 1 ? rm[-1].skip : 0, // adjacent empty run lengths
      beg = i && i < n - 1 ? rm[1].skip : 0;
    if(i)
      rm[-end].skip = rm[beg].skip = beg + end + 1; // set up new run

    if(end)
      rm->reset(); // no links in middle of run
    // Update free list:
    if(beg) {
      const auto & f = rm[1].free;
      Skip & up = ptr == i + 1 ? ptr : s[f.prev].free.next;
      if(end)
        up = f.next; // remove right-hand run from list
      else {
        --up;
        rm->reset(f.prev, f.next);
      }
    }
    else if(!end) { // new run
      Skip & up = ptr ? ptr : s.front().free.next; // keep element 0 first
      if(!ptr && up < n)
        s[up].free.prev = i;
      rm->reset(ptr && ptr < n ? /* size: */ s[ptr].free.prev : ptr, up);
      up = i;
    }
    --s[ptr].free.prev;

    return iterator(this, 1 + (i ? i + beg : this->first_skip()));
  }

  void commit() const {}

  template<class F>
  void send(F && f) {
    base_type::send(std::forward<F>(f));
    if constexpr(privilege_discard(P))
      init(); // no-op on caller side
  }

private:
  void init() const {
    const auto s = this->span();
    if(const auto n = s.size()) {
      auto & a = s.front();
      a.free = {0, 1};
      a.skip = 0;
      if(n > 1) {
        auto & b = s[1];
        b.free = {0, n};
        b.skip = s.back().skip = n - 1;
      }
    }
  }
};

template<auto & F>
struct scalar_access : bind_tag {

  typedef
    typename std::remove_reference_t<decltype(F)>::Field::value_type value_type;

  template<class Func, class S>
  void topology_send(Func && f, S && s) {
    accessor_member<F, privilege_pack<ro>> acc;
    acc.topology_send(std::forward<Func>(f), std::forward<S>(s));

    if(const auto p = acc.get_base().get_base().span().data())
      scalar_ = get_scalar_from_accessor(p);
  }

  const value_type * operator->() const {
    return &scalar_;
  }

  const value_type & operator*() const {
    return scalar_;
  }

private:
  value_type scalar_;
};

} // namespace data

template<data::layout L, class T, Privileges P>
struct exec::detail::task_param<data::accessor<L, T, P>> {
  template<class Topo, typename Topo::index_space S>
  static auto replace(const data::field_reference<T, L, Topo, S> & r) {
    return data::accessor<L, T, P>(r.fid());
  }
};
template<data::layout L, class T, Privileges P>
struct exec::detail::task_param<data::mutator<L, T, P>> {
  template<class Topo, typename Topo::index_space S>
  static auto replace(const data::field_reference<T, L, Topo, S> & r) {
    return data::mutator<L, T, P>(r.fid());
  }
};
template<class T, Privileges P>
struct exec::detail::task_param<data::mutator<data::ragged, T, P>> {
  using type = data::mutator<data::ragged, T, P>;
  template<class Topo, typename Topo::index_space S>
  static type replace(
    const data::field_reference<T, data::ragged, Topo, S> & r) {
    return {type::base_type::parameter(r),
      r.get_partition(r.topology().ragged).growth};
  }
};
template<class T, Privileges P>
struct exec::detail::task_param<data::mutator<data::sparse, T, P>> {
  using type = data::mutator<data::sparse, T, P>;
  template<class Topo, typename Topo::index_space S>
  static type replace(
    const data::field_reference<T, data::sparse, Topo, S> & r) {
    return exec::replace_argument<typename type::base_type>(
      r.template cast<data::ragged,
        typename field<T, data::sparse>::base_type::value_type>());
  }
};

} // namespace flecsi
