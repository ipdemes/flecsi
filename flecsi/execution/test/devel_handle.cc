/*~-------------------------------------------------------------------------~~*
 * Copyright (c) 2014 Los Alamos National Security, LLC
 * All rights reserved.
 *~-------------------------------------------------------------------------~~*/

#include <cinchdevel.h>

#include <flecsi/execution/context.h>

#include <flecsi/execution/test/harness.h>

clog_register_tag(devel_handle);

namespace flecsi {
namespace execution {

//----------------------------------------------------------------------------//
// Variable registration
//----------------------------------------------------------------------------//

flecsi_register_field(mesh_t, data, pressure, double, dense, 1,
  index_spaces::cells);

//----------------------------------------------------------------------------//
// Initialize pressure
//----------------------------------------------------------------------------//

void initialize_pressure(mesh<ro> m, field<rw, rw, ro> p) {
  size_t count{0};

  auto & context { context_t::instance() };

  for(auto c: m.cells(owned)) {
    p(c) = (context.color() + 1)*1000.0 + count++;
  } // for

} // initialize_pressure

flecsi_register_task(initialize_pressure, flecsi::execution, loc, single);

//----------------------------------------------------------------------------//
// Update pressure
//----------------------------------------------------------------------------//

void update_pressure(mesh<ro> m, field<rw, rw, ro> p) {
  size_t count{0};

  for(auto c: m.cells(owned)) {
    p(c) = 2.0*p(c);
  } // for

} // initialize_pressure

flecsi_register_task(update_pressure, flecsi::execution, loc, single);

//----------------------------------------------------------------------------//
// Print task
//----------------------------------------------------------------------------//

void print_mesh(mesh<ro> m, field<ro, ro, ro> p) {
  {
  clog_tag_guard(devel_handle);
  clog(info) << "print_mesh task" << std::endl;
  } // scope

  auto & context = context_t::instance();
  auto & vertex_map = context.index_map(index_spaces::vertices);
  auto & cell_map = context.index_map(index_spaces::cells);

  for(auto c: m.cells(owned)) {
    const size_t cid = c->template id<0>();

    {
    clog_tag_guard(devel_handle);
    clog(trace) << "color: " << context.color() << " cell id: (" <<
      cid << ", " << cell_map[cid] << ")" << std::endl;
    clog(trace) << "color: " << context.color() << " pressure: " <<
      p(c) << std::endl;
    } // scope

    size_t vcount(0);
    for(auto v: m.vertices(c)) {
      const size_t vid = v->template id<0>();

      {
      clog_tag_guard(devel_handle);
      clog(trace) << "color: " << context.color() << " vertex id: (" <<
        vid << ", " << vertex_map[vid] << ") " << vcount << std::endl;

      point_t coord = v->coordinates();
      clog(trace) << "color: " << context.color() << " coordinates: (" <<
        coord[0] << ", " << coord[1] << ")" << std::endl;
      } // scope

      vcount++;
    } // for
  } // for
} // print_mesh

flecsi_register_task(print_mesh, flecsi::execution, loc, single);

//----------------------------------------------------------------------------//
// User driver.
//----------------------------------------------------------------------------//

void driver(int argc, char ** argv) {

  auto mh = flecsi_get_client_handle(mesh_t, clients, m);
  auto ph = flecsi_get_handle(mh, data, pressure, double, dense, 0);

  flecsi_execute_task(initialize_pressure, flecsi::execution, single, mh, ph);
  flecsi_execute_task(update_pressure, flecsi::execution, single, mh, ph);
  flecsi_execute_task(print_mesh, flecsi::execution, single, mh, ph);

} // driver

} // namespace execution
} // namespace flecsi

DEVEL(devel_handle) {}

/*~------------------------------------------------------------------------~--*
 * Formatting options for vim.
 * vim: set tabstop=2 shiftwidth=2 expandtab :
 *~------------------------------------------------------------------------~--*/
