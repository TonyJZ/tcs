#ifndef IO_H
#define IO_H

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <Eigen/Sparse>

template <typename PointT> bool
load (const std::string& filename,
      typename pcl::PointCloud<PointT>::Ptr& cloud,
      typename pcl::PointCloud<pcl::Normal>::Ptr normals = pcl::PointCloud<pcl::Normal>::Ptr ());

template <typename Graph> bool
saveGraph (const std::string& filename,
           const Graph& graph);

template <typename Graph> bool
loadGraph (const std::string& filename,
           Graph& graph);

void
save (const std::string& filename,
      const Eigen::SparseMatrix<float>& M);

void
load (const std::string& filename,
      Eigen::SparseMatrix<float>& M);

#include "impl/io.hpp"

#endif /* IO_H */

