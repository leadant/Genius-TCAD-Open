// $Id: mesh_base.cc,v 1.10 2008/06/20 14:02:28 gdiso Exp $

// The libMesh Finite Element Library.
// Copyright (C) 2002-2007  Benjamin S. Kirk, John W. Peterson

// This library is free software; you can redistribute and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA





// C++ includes
#include <algorithm> // for std::min
#include <sstream>   // for std::ostringstream
#include <map>       // for std::multimap


// Local includes
#include "mesh_base.h"
#include "metis_partitioner.h" // for default partitioning
//#include "parmetis_partitioner.h" // for parallel partitioning
#include "elem.h"
#include "boundary_info.h"
#include "point_locator_base.h"
#include "surface_locator_hub.h"
#include "perf_log.h"

// ------------------------------------------------------------
// MeshBase class member functions
MeshBase::MeshBase (unsigned int d) :
    boundary_info   (new BoundaryInfo(*this)),
    _magic_num      (invalid_uint),
    _n_sbd          (1),
    _n_parts        (1),
    _dim            (d),
    _mesh_dim       (0),
    _is_prepared    (false),
    _point_locator  (NULL),
    _surface_locator(NULL)
{
  assert (DIM <= 3);
  assert (DIM >= _dim);
}



MeshBase::MeshBase (const MeshBase& other_mesh) :
    boundary_info   (new BoundaryInfo(*this)), // no copy constructor defined for BoundaryInfo?
    _magic_num      (other_mesh._magic_num),
    _n_sbd          (other_mesh._n_sbd),
    _n_parts        (other_mesh._n_parts),
    _dim            (other_mesh._dim),
    _mesh_dim       (other_mesh._mesh_dim),
    _is_prepared    (other_mesh._is_prepared),
    _point_locator  (NULL),
    _surface_locator(NULL)

{}



MeshBase::~MeshBase()
{
  this->clear();
}



void MeshBase::prepare_for_use (const bool skip_renumber_nodes_and_elements)
{
  this->count_mesh_dimension();

  // Renumber the nodes and elements so that they in contiguous
  // blocks.  By default, skip_renumber_nodes_and_elements is false,
  // however we may skip this step by passing
  // skip_renumber_nodes_and_elements==true to this function.
  //
  // Instances where you if prepare_for_use() should not renumber the nodes
  // and elements include reading in e.g. an xda/r or gmv file. In
  // this case, the ordering of the nodes may depend on an accompanying
  // solution, and the node ordering cannot be changed.
  if(!skip_renumber_nodes_and_elements)
    this->renumber_nodes_and_elements();

  // Let all the elements find their neighbors
  this->find_neighbors();

  // Reorder the node index by Reverse Cuthill-McKee Algorithm
  if(!skip_renumber_nodes_and_elements)
    this->reorder_nodes();

  // Partition the mesh.
  this->partition();

  // Reset our PointLocator.  This needs to happen any time the elements
  // in the underlying elements in the mesh have changed, so we do it here.
  this->clear_point_locator();
  this->clear_surface_locator();

  // The mesh is now prepared for use.
  _is_prepared = true;
}




void MeshBase::count_mesh_dimension()
{
  const_element_iterator       el  = this->elements_begin();
  const const_element_iterator end = this->elements_end();

  for (; el!=end; ++el)
    _mesh_dim = std::max(_mesh_dim, (*el)->dim());

  //Parallel::max(_mesh_dim);
}



unsigned int MeshBase::n_active_elem () const
{
  return static_cast<unsigned int>(std::distance (this->active_elements_begin(),
                                   this->active_elements_end()));
  //   unsigned int num=0;

  //   const_element_iterator       el  = this->active_elements_begin();
  //   const const_element_iterator end = this->active_elements_end();

  //   for (; el!=end; ++el)
  //     num++;

  //   return num;
}



void MeshBase::clear ()
{

  // Reset the number of subdomains
  _n_sbd  = 1;

  // Reset the number of partitions
  _n_parts = 1;

  // clear _mesh_dim
  _mesh_dim = 0;

  // Reset the _is_prepared flag
  _is_prepared = false;

  // Clear boundary information
  this->boundary_info->clear();

  // Clear our point locator.
  this->clear_point_locator();
  this->clear_surface_locator();

  // clear subdomain material and label information
  _subdomain_labels_to_ids.clear();

  _subdomain_ids_to_labels.clear();

  _subdomain_materials.clear();

  _subdomain_weight.clear();
}



unsigned int MeshBase::n_elem_on_proc (const unsigned int proc_id) const
{
  assert (proc_id < Genius::n_processors());
  return static_cast<unsigned int>(std::distance (this->pid_elements_begin(proc_id),
                                   this->pid_elements_end  (proc_id)));
  //   assert (proc_id < Genius::n_processors());

  //   unsigned int ne=0;

  //   const_element_iterator       el  = this->pid_elements_begin(proc_id);
  //   const const_element_iterator end = this->pid_elements_end(proc_id);

  //   for (; el!=end; ++el)
  //     ne++;

  //   return ne;
}



unsigned int MeshBase::n_active_elem_on_proc (const unsigned int proc_id) const
{
  assert (proc_id < Genius::n_processors());
  return static_cast<unsigned int>(std::distance (this->active_pid_elements_begin(proc_id),
                                   this->active_pid_elements_end  (proc_id)));
  //   assert (proc_id < libMesh::n_processors());

  //   unsigned int ne=0;

  //   const_element_iterator       el  = this->active_pid_elements_begin(proc_id);
  //   const const_element_iterator end = this->active_pid_elements_end(proc_id);


  //   for (; el!=end; ++el)
  //     ne++;

  //   return ne;
}



unsigned int MeshBase::n_sub_elem () const
{
  unsigned int ne=0;

  const_element_iterator       el  = this->elements_begin();
  const const_element_iterator end = this->elements_end();

  for (; el!=end; ++el)
    ne += (*el)->n_sub_elem();

  return ne;
}



unsigned int MeshBase::n_active_sub_elem () const
{
  unsigned int ne=0;

  const_element_iterator       el  = this->active_elements_begin();
  const const_element_iterator end = this->active_elements_end();

  for (; el!=end; ++el)
    ne += (*el)->n_sub_elem();

  return ne;
}



std::string MeshBase::get_info() const
{
  std::ostringstream out;

  out << " Mesh Information:"                                  << '\n'
  << "  mesh_dimension()="    << this->mesh_dimension()    << '\n'
  << "  spatial_dimension()=" << this->spatial_dimension() << '\n'
  << "  n_nodes()="           << this->n_nodes()           << '\n'
  << "  n_elem()="            << this->n_elem()            << '\n'
  << "   n_local_elem()="     << this->n_local_elem()      << '\n'
#ifdef ENABLE_AMR
  << "   n_active_elem()="    << this->n_active_elem()     << '\n'
#endif
  << "  n_processors()="      << this->n_processors()      << '\n'
  << "  processor_id()="      << this->processor_id()      << '\n'
  << "  n_subdomains()="      << this->n_subdomains()      << '\n';

  for(unsigned int n = 0; n < n_subdomains (); n++)
    out << "   subdomain " << n <<" label = "    << subdomain_label_by_id(n)    << '\t'
    <<" material = " << subdomain_material(n) << '\n';


  return out.str();
}


void MeshBase::print_info(std::ostream& os) const
{
  os << this->get_info()
  << std::endl;
  os.flush();
}


std::ostream& operator << (std::ostream& os, const MeshBase& m)
{
  m.print_info(os);
  return os;
}




void MeshBase::partition (const unsigned int n_parts)
{
  START_LOG("partition()", "Mesh");

//#ifdef PETSC_HAVE_PARMETIS
//  ParmetisPartitioner partitioner;
//  partitioner.partition (*this, n_parts);
//#else
//  MetisPartitioner partitioner;
//  partitioner.partition (*this, n_parts);
//#endif

  std::vector<std::vector<unsigned int> > cluster;
  this->partition_cluster(cluster);

  MetisPartitioner partitioner;
  partitioner.partition (*this, &cluster, n_parts);

  STOP_LOG("partition()", "Mesh");
}



unsigned int MeshBase::recalculate_n_partitions()
{
  const_element_iterator       el  = this->active_elements_begin();
  const const_element_iterator end = this->active_elements_end();

  unsigned int max_proc_id=0;

  for (; el!=end; ++el)
    max_proc_id = std::max(max_proc_id, static_cast<unsigned int>((*el)->processor_id()));

  // The number of partitions is one more than the max processor ID.
  _n_parts = max_proc_id+1;

  return _n_parts;
}



const PointLocatorBase & MeshBase::point_locator () const
{
  if (_point_locator.get() == NULL)
    _point_locator.reset (PointLocatorBase::build(PointLocator_TREE, *this).release());

  return *_point_locator;
}



void MeshBase::clear_point_locator ()
{
  _point_locator.reset(NULL);
}


SurfaceLocatorHub & MeshBase::surface_locator () const
{
  if (_surface_locator.get() == NULL)
    _surface_locator.reset (new SurfaceLocatorHub(*this, SurfaceLocator_SPHERE));

  return *_surface_locator;
}



void MeshBase::clear_surface_locator ()
{
  _surface_locator.reset(NULL);
}


const Elem* MeshBase::element_have_point(const Point & p)
{
   const PointLocatorBase & locator = this->point_locator();
   return locator(p);
}


