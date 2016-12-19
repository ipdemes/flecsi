/*~--------------------------------------------------------------------------~*
 *  @@@@@@@@  @@           @@@@@@   @@@@@@@@ @@
 * /@@/////  /@@          @@////@@ @@////// /@@
 * /@@       /@@  @@@@@  @@    // /@@       /@@
 * /@@@@@@@  /@@ @@///@@/@@       /@@@@@@@@@/@@
 * /@@////   /@@/@@@@@@@/@@       ////////@@/@@
 * /@@       /@@/@@//// //@@    @@       /@@/@@
 * /@@       @@@//@@@@@@ //@@@@@@  @@@@@@@@ /@@
 * //       ///  //////   //////  ////////  //
 *
 * Copyright (c) 2016 Los Alamos National Laboratory, LLC
 * All rights reserved
 *~--------------------------------------------------------------------------~*/

#include <iostream>

#include "flecsi/execution/context.h"
#include "flecsi/partition/index_partition.h"
#include "flecsi/partition/init_partitions_task.h"

namespace flecsi {
namespace dmp {

parts
get_numbers_of_cells_task(
  const Legion::Task *task, 
  const std::vector<Legion::PhysicalRegion> & regions,
  Legion::Context ctx, Legion::HighLevelRuntime *runtime) 
{
  struct parts partitions; 
  using index_partition_t = index_partition__<size_t>;

  flecsi::execution::context_t & context_ =
    flecsi::execution::context_t::instance();
  //getting cells partitioning info from MPI
  index_partition_t ip_cells =
    context_.interop_helper_.data_storage_[0];
  
  //getting vertices partitioning info from MPI
  index_partition_t ip_vertices =
    context_.interop_helper_.data_storage_[1]; 
#if 0
  size_t rank; 
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  std::cout << "in get_numbers_of_cells native legion task rank is " <<
       rank <<  std::endl;

  for(size_t i=0; i<ip.primary.size(); i++) {
    auto element = ip.primary[i];
    std::cout << " Found ghost elemment: " << element <<
        " on rank: " << rank << std::endl;
  }
    
  for(size_t i=0; i< ip.exclusive.size();i++ ) {
    auto element = ip.exclusive[i];
    std::cout << " Found exclusive elemment: " << element << 
        " on rank: " << rank << std::endl;
  }
  
  for(size_t i=0; i< ip.shared.size(); i++) {
    auto element = ip.shared_id(i);
    std::cout << " Found shared elemment: " << element << 
        " on rank: " << rank << std::endl;
  } 

  for(size_t i=0; i<ip.ghost.size(); i++) {
    auto element = ip.ghost_id(i);
    std::cout << " Found ghost elemment: " << element << 
        " on rank: " << rank << std::endl;
  }

#endif
  
  partitions.primary_cells = ip_cells.primary.size();
  partitions.exclusive_cells = ip_cells.exclusive.size();
  partitions.shared_cells = ip_cells.shared.size();
  partitions.ghost_cells = ip_cells.ghost.size();

  partitions.primary_vertices = ip_vertices.primary.size();
  partitions.exclusive_vertices = ip_vertices.exclusive.size();
  partitions.shared_vertices = ip_vertices.shared.size();
  partitions.ghost_vertices = ip_vertices.ghost.size();

  std::cout << "about to return partitions (primary,exclusive,shared,ghost) ("
            << partitions.primary_cells << "," 
            <<partitions.exclusive_cells << "," << partitions.shared_cells <<
             "," << partitions.ghost_cells << ")" << std::endl;

  return partitions; 
}//get_numbers_of_cells_task

void
initialization_task(
  const Legion::Task *task,
  const std::vector<Legion::PhysicalRegion> & regions,
  Legion::Context ctx, Legion::HighLevelRuntime *runtime
)
{

  assert(regions.size() == 2);
  assert(task->regions.size() == 2);
  assert(task->regions[0].privilege_fields.size() == 1);
  assert(task->regions[1].privilege_fields.size() == 1);
  std::cout << "Here I am in init_cells" << std::endl;

  using index_partition_t = index_partition__<size_t>;

  flecsi::execution::context_t & context_ =
    flecsi::execution::context_t::instance();
  index_partition_t ip_cells =
    context_.interop_helper_.data_storage_[0];
  index_partition_t ip_vert =
    context_.interop_helper_.data_storage_[1];

  //cells:
  LegionRuntime::HighLevel::LogicalRegion lr_cells =
      regions[0].get_logical_region();
  LegionRuntime::HighLevel::IndexSpace is_cells = lr_cells.get_index_space();

  LegionRuntime::HighLevel::IndexIterator itr_cells(runtime, ctx, is_cells);

  auto acc_cells = regions[0].get_field_accessor(0).typeify<size_t>();

  for (auto primary_cell : ip_cells.primary) {
    assert(itr_cells.has_next());
    size_t id =primary_cell;
    ptr_t ptr = itr_cells.next();
    acc_cells.write(ptr, id);
  }

  //vertices
  LegionRuntime::HighLevel::LogicalRegion lr_vert =
      regions[1].get_logical_region();
  LegionRuntime::HighLevel::IndexSpace is_vert = lr_vert.get_index_space();

  LegionRuntime::HighLevel::IndexIterator itr_vert(runtime, ctx, is_vert);

  auto acc_vert = regions[1].get_field_accessor(0).typeify<size_t>();

  for (auto primary_vert : ip_vert.primary) {
    assert(itr_vert.has_next());
    size_t id =primary_vert;
    ptr_t ptr = itr_vert.next();
    acc_vert.write(ptr, id);
  }

}//initialization_task


partition_lr
shared_part_task(
  const Legion::Task *task,
  const std::vector<Legion::PhysicalRegion> & regions,
  Legion::Context ctx, Legion::HighLevelRuntime *runtime
)
{
  assert(regions.size() == 2);
  assert(task->regions.size() == 2);
  assert(task->regions[0].privilege_fields.size() == 1);
  assert(task->regions[1].privilege_fields.size() == 1);

  using index_partition_t = index_partition__<size_t>;
  using legion_domain = LegionRuntime::HighLevel::Domain;

  flecsi::execution::context_t & context_ =
    flecsi::execution::context_t::instance();
  index_partition_t ip_cells =
    context_.interop_helper_.data_storage_[0];
  index_partition_t ip_vert =
    context_.interop_helper_.data_storage_[1];

  partition_lr shared_lr;

  LegionRuntime::HighLevel::LogicalRegion cells_lr =
    regions[0].get_logical_region();
  LegionRuntime::HighLevel::IndexSpace cells_is = cells_lr.get_index_space();

  Rect<1> cells_shared_rect(Point<1>(0), Point<1>(ip_cells.shared.size()-1));
  LegionRuntime::HighLevel::IndexSpace cells_shared_is =
          runtime->create_index_space(
          ctx, legion_domain::from_rect<1>(cells_shared_rect));

  LegionRuntime::HighLevel::FieldSpace cells_shared_fs =
          runtime->create_field_space(ctx);
  {
    LegionRuntime::HighLevel::FieldAllocator allocator =
        runtime->create_field_allocator(ctx,cells_shared_fs);
    allocator.allocate_field(sizeof(ptr_t), FID_SHARED);
  }

  shared_lr.cells=
       runtime->create_logical_region(ctx,cells_shared_is, cells_shared_fs);
  runtime->attach_name(shared_lr.cells, "shared temp  logical region");

  {
    LegionRuntime::HighLevel::RegionRequirement req(shared_lr.cells,
                      READ_WRITE, EXCLUSIVE, shared_lr.cells);
    req.add_field(FID_SHARED);
    LegionRuntime::HighLevel::InlineLauncher shared_launcher(req);
    LegionRuntime::HighLevel::PhysicalRegion shared_region =
              runtime->map_region(ctx, shared_launcher);
    shared_region.wait_until_valid();
    LegionRuntime::Accessor::RegionAccessor<
      LegionRuntime::Accessor::AccessorType::Generic, ptr_t> acc =
      shared_region.get_field_accessor(FID_SHARED).typeify<ptr_t>();

    int indx=0;
    LegionRuntime::HighLevel::IndexIterator itr(runtime, ctx, cells_is);
    ptr_t start=itr.next();
    for (auto shared_cell : ip_cells.shared) {
      ptr_t ptr = (start.value+shared_cell.offset);
      acc.write(LegionRuntime::HighLevel::DomainPoint::from_point<1>(
      make_point(indx)),ptr);
      indx++;
    }//end for

    runtime->unmap_region(ctx, shared_region);
  }//scope


  LegionRuntime::HighLevel::LogicalRegion vert_lr =
    regions[1].get_logical_region();
  LegionRuntime::HighLevel::IndexSpace vert_is = vert_lr.get_index_space();

  Rect<1> vert_shared_rect(Point<1>(0), Point<1>(ip_vert.shared.size()-1));
  LegionRuntime::HighLevel::IndexSpace vert_shared_is =
          runtime->create_index_space(
          ctx, legion_domain::from_rect<1>(vert_shared_rect));

  LegionRuntime::HighLevel::FieldSpace vert_shared_fs =
          runtime->create_field_space(ctx);
  {
    LegionRuntime::HighLevel::FieldAllocator allocator =
        runtime->create_field_allocator(ctx,vert_shared_fs);
    allocator.allocate_field(sizeof(ptr_t), FID_SHARED);
  }

  shared_lr.vert=
       runtime->create_logical_region(ctx,vert_shared_is, vert_shared_fs);
  runtime->attach_name(shared_lr.vert, "shared temp  logical region");

  {
    LegionRuntime::HighLevel::RegionRequirement req(shared_lr.vert,
                      READ_WRITE, EXCLUSIVE, shared_lr.vert);
    req.add_field(FID_SHARED);
    LegionRuntime::HighLevel::InlineLauncher shared_launcher(req);
    LegionRuntime::HighLevel::PhysicalRegion shared_region =
              runtime->map_region(ctx, shared_launcher);
    shared_region.wait_until_valid();
    LegionRuntime::Accessor::RegionAccessor<
      LegionRuntime::Accessor::AccessorType::Generic, ptr_t> acc =
      shared_region.get_field_accessor(FID_SHARED).typeify<ptr_t>();

    size_t indx=0;
    LegionRuntime::HighLevel::IndexIterator itr(runtime, ctx, vert_is);
    ptr_t start=itr.next();
#if 0
//debug
size_t i=0;
for (auto shared_vert : ip_vert.primary) {
std::cout<<"primary["<<i<<"] = "<<shared_vert<< std::endl;
i++;
}
#endif

//debug

    for (auto shared_vert : ip_vert.shared) {
      ptr_t ptr = (start.value+shared_vert.offset);
      acc.write(LegionRuntime::HighLevel::DomainPoint::from_point<1>(
      make_point(indx)),ptr);
#if 0
std::cout <<"DEBUG inside shared create= " <<shared_vert.id<< " , offset= "
<<shared_vert.offset<<std::endl;
#endif
      indx++;
    }//end for

    runtime->unmap_region(ctx, shared_region);
  }//scope
 

  return shared_lr;
}//shared_part_task

partition_lr
exclusive_part_task(
  const Legion::Task *task,
  const std::vector<Legion::PhysicalRegion> & regions,
  Legion::Context ctx, Legion::HighLevelRuntime *runtime
)
{
  assert(regions.size() == 2);
  assert(task->regions.size() == 2);
  assert(task->regions[0].privilege_fields.size() == 1);
  assert(task->regions[1].privilege_fields.size() == 1);
  
  using index_partition_t = index_partition__<size_t>;
  using legion_domain = LegionRuntime::HighLevel::Domain;
  
  flecsi::execution::context_t & context_ =
    flecsi::execution::context_t::instance();
  index_partition_t ip_cells =
    context_.interop_helper_.data_storage_[0];
  index_partition_t ip_vert =
    context_.interop_helper_.data_storage_[1];
 
  partition_lr exclusive_lr;
 
  LegionRuntime::HighLevel::LogicalRegion cells_lr =
      regions[0].get_logical_region();
  LegionRuntime::HighLevel::IndexSpace cells_is = cells_lr.get_index_space();
  LegionRuntime::HighLevel::LogicalRegion vert_lr =
      regions[1].get_logical_region();
  LegionRuntime::HighLevel::IndexSpace vert_is = vert_lr.get_index_space();
  
  Rect<1> cells_exclusive_rect(Point<1>(0), Point<1>(
      ip_cells.exclusive.size()-1));
  LegionRuntime::HighLevel::IndexSpace cells_exclusive_is =
	runtime->create_index_space(ctx, legion_domain::from_rect<1>(
      cells_exclusive_rect));
  
  LegionRuntime::HighLevel::FieldSpace cells_exclusive_fs =
          runtime->create_field_space(ctx);
  { 
    LegionRuntime::HighLevel::FieldAllocator allocator =
        runtime->create_field_allocator(ctx,cells_exclusive_fs);
    allocator.allocate_field(sizeof(ptr_t), FID_EXCLUSIVE);
  }
  
  exclusive_lr.cells=runtime->create_logical_region(ctx,cells_exclusive_is, 
       cells_exclusive_fs);
  runtime->attach_name(exclusive_lr.cells, 
          "exclusive temp  logical region");
  {
    LegionRuntime::HighLevel::RegionRequirement req(exclusive_lr.cells,
                      READ_WRITE, EXCLUSIVE, exclusive_lr.cells);
    req.add_field(FID_EXCLUSIVE);
    LegionRuntime::HighLevel::InlineLauncher exclusive_launcher(req);
    LegionRuntime::HighLevel::PhysicalRegion exclusive_region =
              runtime->map_region(ctx, exclusive_launcher);
    exclusive_region.wait_until_valid();
    LegionRuntime::Accessor::RegionAccessor<
      LegionRuntime::Accessor::AccessorType::Generic, ptr_t> acc =
      exclusive_region.get_field_accessor(FID_EXCLUSIVE).typeify<ptr_t>();

    size_t indx=0;
    LegionRuntime::HighLevel::IndexIterator itr(runtime, ctx, cells_is);
    ptr_t start=itr.next();
    for (auto exclusive_cell : ip_cells.exclusive) {
      ptr_t ptr = (start.value+exclusive_cell.offset);
      acc.write(LegionRuntime::HighLevel::DomainPoint::from_point<1>(
      make_point(indx)),ptr);
      indx++;
    }//end for

    runtime->unmap_region(ctx, exclusive_region);
  }//end scope


  Rect<1> vert_exclusive_rect(Point<1>(0), Point<1>(
      ip_vert.exclusive.size()-1));
  LegionRuntime::HighLevel::IndexSpace vert_exclusive_is =
  runtime->create_index_space(ctx, legion_domain::from_rect<1>(
      vert_exclusive_rect));

  LegionRuntime::HighLevel::FieldSpace vert_exclusive_fs =
          runtime->create_field_space(ctx);
  {
    LegionRuntime::HighLevel::FieldAllocator allocator =
        runtime->create_field_allocator(ctx, vert_exclusive_fs);
    allocator.allocate_field(sizeof(ptr_t), FID_EXCLUSIVE);
  }

  exclusive_lr.vert=runtime->create_logical_region(ctx, vert_exclusive_is,
       vert_exclusive_fs);
  runtime->attach_name(exclusive_lr.vert,
          "exclusive temp  logical region");
  {
    LegionRuntime::HighLevel::RegionRequirement req(exclusive_lr.vert,
                      READ_WRITE, EXCLUSIVE, exclusive_lr.vert);
    req.add_field(FID_EXCLUSIVE);
    LegionRuntime::HighLevel::InlineLauncher exclusive_launcher(req);
    LegionRuntime::HighLevel::PhysicalRegion exclusive_region =
              runtime->map_region(ctx, exclusive_launcher);
    exclusive_region.wait_until_valid();
    LegionRuntime::Accessor::RegionAccessor<
      LegionRuntime::Accessor::AccessorType::Generic, ptr_t> acc =
      exclusive_region.get_field_accessor(FID_EXCLUSIVE).typeify<ptr_t>();

    size_t indx=0;
    LegionRuntime::HighLevel::IndexIterator itr(runtime, ctx, vert_is);
    ptr_t start=itr.next();
    for (auto exclusive_vert : ip_vert.exclusive) {
      ptr_t ptr = (start.value+exclusive_vert.offset);
      acc.write(LegionRuntime::HighLevel::DomainPoint::from_point<1>(
      make_point(indx)),ptr);
      indx++;
    }//end for

    runtime->unmap_region(ctx, exclusive_region);
  }//end scope


  return exclusive_lr; 
}//exclusive_part_task

partition_lr
ghost_part_task(
  const Legion::Task *task,
  const std::vector<Legion::PhysicalRegion> & regions,
  Legion::Context ctx, Legion::HighLevelRuntime *runtime
)
{
  assert(regions.size() == 2);
  assert(task->regions.size() == 2);
  assert(task->regions[0].privilege_fields.size() == 1);
  assert(task->regions[1].privilege_fields.size() == 1);
  
  using index_partition_t = index_partition__<size_t>;
  using legion_domain = LegionRuntime::HighLevel::Domain;
  using generic_type = LegionRuntime::Accessor::AccessorType::Generic; 
  using field_id = LegionRuntime::HighLevel::FieldID;
 
  flecsi::execution::context_t & context_ =
    flecsi::execution::context_t::instance();
  index_partition_t ip_cells =
    context_.interop_helper_.data_storage_[0];
  index_partition_t ip_vert =
    context_.interop_helper_.data_storage_[1];

  partition_lr ghost_lr;

  field_id fid_cells_global = *(task->regions[0].privilege_fields.begin());
  LegionRuntime::HighLevel::LogicalRegion cells_lr =
      regions[0].get_logical_region();
  LegionRuntime::HighLevel::IndexSpace cells_is = cells_lr.get_index_space();
  LegionRuntime::Accessor::RegionAccessor<generic_type, size_t>
    acc_cells_global= regions[0].get_field_accessor(
      fid_cells_global).typeify<size_t>();

  field_id fid_vert_global = *(task->regions[1].privilege_fields.begin());
  LegionRuntime::HighLevel::LogicalRegion vert_lr =
      regions[0].get_logical_region();
  LegionRuntime::HighLevel::IndexSpace vert_is = vert_lr.get_index_space();
  LegionRuntime::Accessor::RegionAccessor<generic_type, size_t>
    acc_vert_global= regions[1].get_field_accessor(
      fid_vert_global).typeify<size_t>();
 
  Rect<1> ghost_cells_rect(Point<1>(0), Point<1>(ip_cells.ghost.size()-1));
  LegionRuntime::HighLevel::IndexSpace ghost_cells_is =
  runtime->create_index_space(
    ctx, legion_domain::from_rect<1>(ghost_cells_rect));

  LegionRuntime::HighLevel::FieldSpace ghost_cells_fs =
          runtime->create_field_space(ctx);
  { 
    LegionRuntime::HighLevel::FieldAllocator allocator =
        runtime->create_field_allocator(ctx,ghost_cells_fs);
    allocator.allocate_field(sizeof(ptr_t), FID_GHOST);
  }
  
  ghost_lr.cells= runtime->create_logical_region(ctx,ghost_cells_is,
        ghost_cells_fs);
  runtime->attach_name(ghost_lr.cells, "ghost temp  logical region");
 
  {
    LegionRuntime::HighLevel::RegionRequirement req(ghost_lr.cells,
                        READ_WRITE, EXCLUSIVE, ghost_lr.cells);
    req.add_field(FID_GHOST);
    LegionRuntime::HighLevel::InlineLauncher ghost_launcher(req);
    LegionRuntime::HighLevel::PhysicalRegion ghost_region =
                runtime->map_region(ctx, ghost_launcher);
    ghost_region.wait_until_valid();
    LegionRuntime::Accessor::RegionAccessor<generic_type, ptr_t> acc =
      ghost_region.get_field_accessor(FID_GHOST).typeify<ptr_t>();

    size_t indx=0;
    for (auto ghost_cell : ip_cells.ghost) {
      LegionRuntime::HighLevel::IndexIterator itr(runtime, ctx, cells_is);
      size_t id_ghost = ghost_cell.id;
      while(itr.has_next()){
        ptr_t ptr = itr.next();
        size_t id_global = acc_cells_global.read(ptr);
        if (id_global == id_ghost){
           acc.write(LegionRuntime::HighLevel::DomainPoint::from_point<1>(
              make_point(indx)),ptr);
           indx++;
        }//end while
      }//end for
    }//end for

    runtime->unmap_region(ctx, ghost_region);
  }//end scope

  Rect<1> ghost_vert_rect(Point<1>(0), Point<1>(ip_vert.ghost.size()-1));
  LegionRuntime::HighLevel::IndexSpace ghost_vert_is =
  runtime->create_index_space(
    ctx, legion_domain::from_rect<1>(ghost_vert_rect));

  LegionRuntime::HighLevel::FieldSpace ghost_vert_fs =
          runtime->create_field_space(ctx);
  {
    LegionRuntime::HighLevel::FieldAllocator allocator =
        runtime->create_field_allocator(ctx,ghost_vert_fs);
    allocator.allocate_field(sizeof(ptr_t), FID_GHOST);
  }

  ghost_lr.vert= runtime->create_logical_region(ctx,ghost_vert_is,
        ghost_vert_fs);
  runtime->attach_name(ghost_lr.vert, "ghost temp  logical region");

  {
    LegionRuntime::HighLevel::RegionRequirement req(ghost_lr.vert,
                        READ_WRITE, EXCLUSIVE, ghost_lr.vert);
    req.add_field(FID_GHOST);
    LegionRuntime::HighLevel::InlineLauncher ghost_launcher(req);
    LegionRuntime::HighLevel::PhysicalRegion ghost_region =
                runtime->map_region(ctx, ghost_launcher);
    ghost_region.wait_until_valid();
    LegionRuntime::Accessor::RegionAccessor<generic_type, ptr_t> acc =
      ghost_region.get_field_accessor(FID_GHOST).typeify<ptr_t>();

    size_t indx=0;
    for (auto ghost_cell : ip_vert.ghost) {
      LegionRuntime::HighLevel::IndexIterator itr(runtime, ctx, vert_is);
      size_t id_ghost = ghost_cell.id;
      while(itr.has_next()){
        ptr_t ptr = itr.next();
        size_t id_global = acc_vert_global.read(ptr);
        if (id_global == id_ghost){
           acc.write(LegionRuntime::HighLevel::DomainPoint::from_point<1>(
              make_point(indx)),ptr);
           indx++;
        }//end while
      }//end for
    }//end for

    runtime->unmap_region(ctx, ghost_region);
  }//end scope

  return ghost_lr;
}//ghost_part_task

void
check_partitioning_task(
  const Legion::Task *task,
  const std::vector<Legion::PhysicalRegion> & regions,
  Legion::Context ctx, Legion::HighLevelRuntime *runtime
)
{
  assert(regions.size() == 6);
  assert(task->regions.size() == 6);
  assert(task->regions[0].privilege_fields.size() == 1);
  assert(task->regions[1].privilege_fields.size() == 1);
  assert(task->regions[2].privilege_fields.size() == 1);
  assert(task->regions[3].privilege_fields.size() == 1);
  assert(task->regions[4].privilege_fields.size() == 1);
  assert(task->regions[5].privilege_fields.size() == 1);

  using index_partition_t = index_partition__<size_t>;
  using generic_type = LegionRuntime::Accessor::AccessorType::Generic;
  using field_id = LegionRuntime::HighLevel::FieldID;

  //checking cells:
  {
    LegionRuntime::HighLevel::LogicalRegion lr_shared =
       regions[0].get_logical_region();
   LegionRuntime::HighLevel::IndexSpace is_shared = lr_shared.get_index_space();
    LegionRuntime::HighLevel::IndexIterator itr_shared(runtime, ctx, is_shared);
    field_id fid_shared = *(task->regions[0].privilege_fields.begin());
    LegionRuntime::Accessor::RegionAccessor<generic_type, size_t>
      acc_shared= regions[0].get_field_accessor(fid_shared).typeify<size_t>();

    LegionRuntime::HighLevel::LogicalRegion lr_exclusive = 
        regions[1].get_logical_region();
    LegionRuntime::HighLevel::IndexSpace is_exclusive =
        lr_exclusive.get_index_space();
    LegionRuntime::HighLevel::IndexIterator itr_exclusive(runtime,
        ctx, is_exclusive);
    field_id fid_exclusive = *(task->regions[1].privilege_fields.begin());
    LegionRuntime::Accessor::RegionAccessor<generic_type, size_t>
      acc_exclusive=regions[1].get_field_accessor(
          fid_exclusive).typeify<size_t>();


    LegionRuntime::HighLevel::LogicalRegion lr_ghost = 
        regions[2].get_logical_region();
    LegionRuntime::HighLevel::IndexSpace is_ghost = lr_ghost.get_index_space();
    LegionRuntime::HighLevel::IndexIterator itr_ghost(runtime, ctx, is_ghost);
    field_id fid_ghost = *(task->regions[2].privilege_fields.begin());
    LegionRuntime::Accessor::RegionAccessor<generic_type, size_t>
      acc_ghost= regions[2].get_field_accessor(fid_ghost).typeify<size_t>();  

    flecsi::execution::context_t & context_ =
      flecsi::execution::context_t::instance();
    index_partition_t ip =
      context_.interop_helper_.data_storage_[0];

    size_t indx = 0;
    for (auto shared_cell : ip.shared) {
    assert(itr_shared.has_next());
     ptr_t ptr=itr_shared.next();
     assert(shared_cell.id == acc_shared.read(ptr));
     indx++;
    }
    assert (indx == ip.shared.size());

    indx = 0;
    for (auto exclusive_cell : ip.exclusive) {
     assert(itr_exclusive.has_next());
     ptr_t ptr=itr_exclusive.next();
     assert(exclusive_cell.id == acc_exclusive.read(ptr));
    indx++;
    }
    assert (indx == ip.exclusive.size());


    indx = 0;
    while(itr_ghost.has_next()){
      ptr_t ptr = itr_ghost.next();
      bool found=false;
      size_t ghost_id = acc_ghost.read(ptr);
      for (auto ghost_cell : ip.ghost) {
        if (ghost_cell.id == ghost_id){
          found=true;
        }
      }
      assert(found);
      indx++;
     }
     assert (indx == ip.ghost.size());
    }//scope

    //checking vertices
    {
     LegionRuntime::HighLevel::LogicalRegion lr_shared =
       regions[3].get_logical_region();
    LegionRuntime::HighLevel::IndexSpace is_shared=lr_shared.get_index_space();
    LegionRuntime::HighLevel::IndexIterator itr_shared(runtime, ctx, is_shared);
    field_id fid_shared = *(task->regions[3].privilege_fields.begin());
    LegionRuntime::Accessor::RegionAccessor<generic_type, size_t>
      acc_shared= regions[3].get_field_accessor(fid_shared).typeify<size_t>();

    LegionRuntime::HighLevel::LogicalRegion lr_exclusive =
        regions[4].get_logical_region();
    LegionRuntime::HighLevel::IndexSpace is_exclusive =
        lr_exclusive.get_index_space();
    LegionRuntime::HighLevel::IndexIterator itr_exclusive(runtime,
        ctx, is_exclusive);
    field_id fid_exclusive = *(task->regions[4].privilege_fields.begin());
    LegionRuntime::Accessor::RegionAccessor<generic_type, size_t>
      acc_exclusive=regions[4].get_field_accessor(
      fid_exclusive).typeify<size_t>();


    LegionRuntime::HighLevel::LogicalRegion lr_ghost =
        regions[5].get_logical_region();
    LegionRuntime::HighLevel::IndexSpace is_ghost = lr_ghost.get_index_space();
    LegionRuntime::HighLevel::IndexIterator itr_ghost(runtime, ctx, is_ghost);
    field_id fid_ghost = *(task->regions[5].privilege_fields.begin());
    LegionRuntime::Accessor::RegionAccessor<generic_type, size_t>
      acc_ghost= regions[5].get_field_accessor(fid_ghost).typeify<size_t>();

    flecsi::execution::context_t & context_ =
      flecsi::execution::context_t::instance();
    index_partition_t ip =
      context_.interop_helper_.data_storage_[1];

    size_t indx = 0;
    for (auto shared_cell : ip.shared) {
    assert(itr_shared.has_next());
     ptr_t ptr=itr_shared.next();
     assert(shared_cell.id == acc_shared.read(ptr));
     indx++;
    }
    assert (indx == ip.shared.size());

    indx = 0;
    for (auto exclusive_cell : ip.exclusive) {
     assert(itr_exclusive.has_next());
     ptr_t ptr=itr_exclusive.next();
     assert(exclusive_cell.id == acc_exclusive.read(ptr));
    indx++;
    }
    assert (indx == ip.exclusive.size());
    
    indx = 0;
    while(itr_ghost.has_next()){
      ptr_t ptr = itr_ghost.next();
      bool found=false;
      size_t ghost_id = acc_ghost.read(ptr);
      for (auto ghost_cell : ip.ghost) {
        if (ghost_cell.id == ghost_id){
          found=true;
        }
      }
      assert(found);
      indx++;
     }
     assert (indx == ip.ghost.size());

    }

  std::cout << "test for shared/ghost/exclusive partitions ... passed" 
  << std::endl;
}//check_partitioning_task

} // namespace dmp
} // namespace flecsi

/*~-------------------------------------------------------------------------~-*
 * Formatting options for vim.
 * vim: set tabstop=2 shiftwidth=2 expandtab :
 *~-------------------------------------------------------------------------~-*/
