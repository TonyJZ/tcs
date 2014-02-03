/*
 * Software License Agreement (BSD License)
 *
 *  Point Cloud Library (PCL) - www.pointclouds.org
 *
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the copyright holder(s) nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef PCL_GRAPH_NEAREST_NEIGHBORS_GRAPH_BUILDER_H
#define PCL_GRAPH_NEAREST_NEIGHBORS_GRAPH_BUILDER_H

#include <pcl/search/search.h>

#include "graph/graph_builder.h"

namespace pcl
{

  namespace graph
  {

    /** \brief This class builds a BGL graph representing an input dataset by
      * using nearest neighbor search.
      *
      * The points from the input cloud become vertices and the edges are
      * established between each point and its neighbors (as found by the search
      * object provided with setSearchMethod()). The user may choose to use the
      * default search method, which will be either KdTree or OrganizedNeighbor
      * depending on whether the input point cloud is organized or not.
      *
      * For additional information see documentation for \ref GraphBuilder.
      *
      * \author Sergey Alexandrov
      * \ingroup graph
      */
    template <typename PointT, typename Graph>
    class PCL_EXPORTS NearestNeighborsGraphBuilder : public GraphBuilder<PointT, Graph>
    {

        using PCLBase<PointT>::initCompute;
        using PCLBase<PointT>::deinitCompute;
        using PCLBase<PointT>::indices_;
        using PCLBase<PointT>::input_;
        using PCLBase<PointT>::use_indices_;
        using PCLBase<PointT>::fake_indices_;

      public:

        using typename GraphBuilder<PointT, Graph>::PointInT;
        using typename GraphBuilder<PointT, Graph>::PointOutT;
        using typename GraphBuilder<PointT, Graph>::VertexId;

        typedef pcl::search::Search<PointOutT> Search;
        typedef typename Search::Ptr SearchPtr;

        NearestNeighborsGraphBuilder (size_t num_neighbors = 14)
        : num_neighbors_ (num_neighbors)
        {
        }

        /** Build a graph based on the provided input data. */
        virtual void
        compute (Graph& graph);

        virtual void
        getPointToVertexMap (std::vector<VertexId>& indices);

        /** Set search method that will be used for finding K nearest neighbors
          * when building a graph. */
        inline void
        setSearchMethod (const SearchPtr& search)
        {
          search_ = search;
        }

        /** Set the number of neighbors to find when building a graph. */
        inline void
        setNumberOfNeighbors (size_t num_neighbors)
        {
          num_neighbors_ = num_neighbors;
        }

        /** Returns the number of neighbors to find when building a graph. */
        inline size_t
        getNumberOfNeighbors () const
        {
          return num_neighbors_;
        }

      private:

        /** The search method that will be used for finding K nearest neighbors
          * when building a graph. */
        SearchPtr search_;

        /** The number of neighbors to find when building a graph. */
        size_t num_neighbors_;

    };

  }

}

#include "graph/impl/nearest_neighbors_graph_builder.hpp"

#endif /* PCL_GRAPH_NEAREST_NEIGHBORS_GRAPH_BUILDER_H */

