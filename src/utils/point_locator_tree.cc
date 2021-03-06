// $Id: point_locator_tree.cc,v 1.1 2008/05/22 14:13:24 gdiso Exp $

// The libMesh Finite Element Library.
// Copyright (C) 2002-2007  Benjamin S. Kirk, John W. Peterson

// This library is free software; you can redistribute it and/or
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

// Local Includes
#include "mesh_base.h"
#include "elem.h"
#include "tree.h"
#include "point_locator_tree.h"
#include "mesh_tools.h"



//------------------------------------------------------------------
// PointLocator methods
PointLocatorTree::PointLocatorTree (const MeshBase& mesh,
				    const PointLocatorBase* master) :
  PointLocatorBase (mesh,master),
  _tree            (NULL),
  _element         (NULL),
  _out_of_mesh_mode(false)
{
  this->init(Trees::NODES);
}




PointLocatorTree::PointLocatorTree (const MeshBase& mesh,
				    const Trees::BuildType build_type,
				    const PointLocatorBase* master) :
  PointLocatorBase (mesh,master),
  _tree            (NULL),
  _element         (NULL),
  _out_of_mesh_mode(false)
{
  this->init(build_type);
}




PointLocatorTree::~PointLocatorTree ()
{
  this->clear ();
}




void PointLocatorTree::clear ()
{
  // only delete the tree when we are the master
  if (this->_tree != NULL)
    {
      if (this->_master == NULL)
	  // we own the tree
	  delete this->_tree;
      else
	  // someone else owns and therefore deletes the tree
	  this->_tree = NULL;
    }
}





void PointLocatorTree::init (const Trees::BuildType build_type)
{
  assert (this->_tree == NULL);

  if (this->_initialized)
    {
      std::cerr << "ERROR: Already initialized!  Will ignore this call..."
		<< std::endl;
    }

  else

    {

      if (this->_master == NULL)
        {
	  if (this->_mesh.mesh_dimension() == 3)
	    _tree = new Trees::OctTree (this->_mesh, 100, 10, build_type); // max 20 elements in each tree node
	  else
	    {
	      // A 1D/2D mesh in 3D space needs special consideration.
	      // If the mesh is planar XY, we want to build a QuadTree
	      // to search efficiently.  If the mesh is truly a manifold,
	      // then we need an octree
	      bool is_planar_xy = false;

	      // Build the bounding box for the mesh.  If the delta-z bound is
	      // negligibly small then we can use a quadtree.
	      {
		MeshTools::BoundingBox bbox = MeshTools::bounding_box(this->_mesh);

		const Real
		  Dx = bbox.second(0) - bbox.first(0),
		  Dz = bbox.second(2) - bbox.first(2);

		if (std::abs(Dz/(Dx + 1.e-20)) < 1e-10)
		  is_planar_xy = true;
	      }

	      if (is_planar_xy)
		_tree = new Trees::QuadTree (this->_mesh, 100, 10, build_type);
	      else
		_tree = new Trees::OctTree  (this->_mesh, 100, 10, build_type);
	    }
	}

      else

        {
	  // We are _not_ the master.  Let our Tree point to
	  // the master's tree.  But for this we first transform
	  // the master in a state for which we are friends.
	  // And make sure the master @e has a tree!
	  const PointLocatorTree* my_master =
	    dynamic_cast<const PointLocatorTree*>(this->_master);

	  if (my_master->initialized())
	    this->_tree = my_master->_tree;
	  else
	    {
	      std::cerr << "ERROR: Initialize master first, then servants!"
			<< std::endl;
	      genius_error();
	    }
        }


      // Not all PointLocators may own a tree, but all of them
      // use their own element pointer.  Let the element pointer
      // be unique for every interpolator.
      // Suppose the interpolators are used concurrently
      // at different locations in the mesh, then it makes quite
      // sense to have unique start elements.
      this->_element = NULL;
    }


  // ready for take-off
  this->_initialized = true;
}





const Elem* PointLocatorTree::operator() (const Point& p) const
{
  assert (this->_initialized);

  // First check the element from last time before asking the tree
  if (this->_element==NULL || !(this->_element->contains_point(p)))
    {
	// ask the tree
	this->_element = this->_tree->find_element (p);

	if (this->_element == NULL)
	  {
	      // No element seems to contain this point.  If out-of-mesh
	      // mode is enabled, just return NULL.  If not, however, we
	      // have to perform a linear search before we call \p
	      // genius_error() since in the case of curved elements, the
	      // bounding box computed in \p TreeNode::insert(const
	      // Elem*) might be slightly inaccurate.
	    if(!_out_of_mesh_mode)
	      {
		MeshBase::const_element_iterator       pos     = this->_mesh.active_elements_begin();
		const MeshBase::const_element_iterator end_pos = this->_mesh.active_elements_end();

		for ( ; pos != end_pos; ++pos)
		  if ((*pos)->contains_point(p))
		    return this->_element = (*pos);

		if (this->_element == NULL)
		  {
		    std::cerr << std::endl
			      << " ******** Serious Problem.  Could not find an Element "
			      << "in the Mesh"
			      << std:: endl
			      << " ******** that contains the Point "
			      << p;
		    genius_error();
		  }
	      }
	  }
    }

  // return the element
  return this->_element;
}


void PointLocatorTree::enable_out_of_mesh_mode (void)
{
  /* Out-of-mesh mode is currently only supported if all of the
     elements have affine mappings.  The reason is that for quadratic
     mappings, it is not easy to construct a relyable bounding box of
     the element, and thus, the fallback linear search in \p
     operator() is required.  Hence, out-of-mesh mode would be
     extremely slow.  */
  if(!_out_of_mesh_mode)
    {
#ifdef DEBUG
      MeshBase::const_element_iterator       pos     = this->_mesh.active_elements_begin();
      const MeshBase::const_element_iterator end_pos = this->_mesh.active_elements_end();
      for ( ; pos != end_pos; ++pos)
	if (!(*pos)->has_affine_map())
	  {
	    std::cerr << "ERROR: Out-of-mesh mode is currently only supported if all elements have affine mappings." << std::endl;
	    genius_error();
	  }
#endif

      _out_of_mesh_mode = true;
    }
}

void PointLocatorTree::disable_out_of_mesh_mode (void)
{
  _out_of_mesh_mode = false;
}

