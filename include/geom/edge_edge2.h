// $Id: edge_edge2.h,v 1.6 2008/07/10 07:16:23 gdiso Exp $

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



#ifndef __edge2_h__
#define __edge2_h__

// C++ includes


// Local includes
#include "genius_common.h"
#include "edge.h"


/**
 * The \p Edge2 is an element in 1D composed of 2 nodes. It is numbered
 * like this:
 *
   \verbatim
    EDGE2: o--------o
           0        1
   \endverbatim
 */

// ------------------------------------------------------------
// Edge class definition
class Edge2 : public Edge
{
 public:

  /**
   * Constructor.  By default this element has no parent.
   */
  Edge2 (Elem* p=NULL) :
    Edge(Edge2::n_nodes(), p) {}

  /**
   * Constructor.  Explicitly specifies the number of
   * nodes and neighbors for which storage will be allocated.
   */
  Edge2 (const unsigned int nn,
     const unsigned int ns,
     Elem* p) :
    Edge(nn, p) { assert (ns == 0); }

  /**
   * @returns 1
   */
  unsigned int n_sub_elem() const { return 1; }

  /**
   * @returns true iff the specified (local) node number is a vertex.
   */
  virtual bool is_vertex(const unsigned int i) const;

  /**
   * @returns true iff the specified (local) node number is an edge.
   */
  virtual bool is_edge(const unsigned int i) const;

  /**
   * @returns true iff the specified (local) node number is a face.
   */
  virtual bool is_face(const unsigned int i) const;

  /**
   * @returns true iff the specified (local) node number is on the
   * specified side
   */
  virtual bool is_node_on_side(const unsigned int n,
                   const unsigned int s) const;


  /**
   * @returns true iff the specified (local) node number is on the
   * specified edge (i.e. "returns true" in 1D)
   */
  virtual bool is_node_on_edge(const unsigned int n,
                   const unsigned int e) const;

  /**
   * @returns true iff the specified (local) edge number is on the
   * specified side
   */
  virtual bool is_edge_on_side(const unsigned int e, const unsigned int s) const;

  /**
   * get the node local index on edge e
   */
  virtual void nodes_on_edge (const unsigned int e,
                              std::vector<unsigned int> & nodes ) const ;

  /**
   * get the node local index on edge2 e
   */
  virtual void nodes_on_edge (const unsigned int e,
                              std::pair<unsigned int, unsigned int> & nodes ) const;

  /**
   * @return the length of the ith edge of element.
   */
  virtual Real edge_length(const unsigned int e) const { return volume(); }

  /**
   * @returns true if the point p is contained in this element,
   * false otherwise.
   */
  virtual bool contains_point (const Point& p) const;


  /*
   * @returns true iff the element map is definitely affine within
   * numerical tolerances
   */
  virtual bool has_affine_map () const { return true; }

  /**
   * @returns \p EDGE2
   */
  virtual ElemType type()  const { return EDGE2; }

  /**
   * @returns FIRST
   */
  Order default_order() const { return FIRST; }


  /**
   * @return the ith node on sth side
   */
  unsigned int side_node(unsigned int , unsigned int i) const
  { return i; }

  virtual void connectivity(const unsigned int sc,
                const IOPackage iop,
                std::vector<unsigned int>& conn) const;

  virtual void side_order( const IOPackage , std::vector<unsigned int>& ) const  {}

  /**
   * This function returns true iff node i and j are neighbors (linked by edge)
   */
  virtual bool node_node_connect(const unsigned int i, const unsigned int j)  const
  { return node_connect_graph[i][j];}

  /**
   * get the ray elem intersection result
   */
  virtual void ray_hit(const Point & , const Point & , IntersectionResult &, unsigned int=3) const;


  /**
   * @return the nearest point on this element to the given point p
   */
  virtual Point nearest_point(const Point &p, Real * dist = 0) const;

  /**
   * An optimized method for computing the length of a 2-node edge.
   */
  virtual Real volume () const;

  /**
   * This graph shows the node connection information
   */
  static const unsigned int node_connect_graph[2][2];

protected:


#ifdef ENABLE_AMR

  /**
   * Matrix used to create the elements children.
   */
  float embedding_matrix (const unsigned int i,
             const unsigned int j,
             const unsigned int k) const
  { return _embedding_matrix[i][j][k]; }

  /**
   * Matrix that computes new nodal locations/solution values
   * from current nodes/solution.
   */
  static const float _embedding_matrix[2][2][2];

#endif
};


#endif
