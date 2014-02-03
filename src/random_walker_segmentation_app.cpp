#include <boost/make_shared.hpp>
#include <boost/format.hpp>

#include <pcl/console/parse.h>
#include <pcl/console/print.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/io/pcd_io.h>
#include <pcl/common/io.h>

#include "tviewer/tviewer.h"
#include "io.h"
#include "graph_visualizer.h"
#include "seed_utils.h"
#include "measure_runtime.h"

#include "factory/weight_computer_factory.h"
#include "factory/graph_builder_factory.h"

#include "graph/common.h"
#include "graph/weight.h"

#include "random_walker_segmentation.h"

typedef pcl::PointXYZRGBA PointT;
typedef pcl::PointXYZRGBNormal PointWithNormalT;
typedef pcl::Normal NormalT;

typedef pcl::PointCloud<PointT> PointCloudT;
typedef pcl::PointCloud<NormalT> NormalCloudT;

typedef typename pcl::segmentation::RandomWalkerSegmentation<PointT>::Graph Graph;
typedef typename pcl::segmentation::RandomWalkerSegmentation<PointT>::GraphPtr GraphPtr;

int main (int argc, char ** argv)
{
  factory::WeightComputerFactory<PointWithNormalT, Graph> wc_factory;
  factory::GraphBuilderFactory<PointT, Graph> gb_factory;

  if (argc < 2 || pcl::console::find_switch (argc, argv, "--help"))
  {
    pcl::console::print_error ("Usage: %s <pcd-file>\n"
                               "--save-seeds <pcd-file>\n"
                               "--load-seeds <pcd-file>\n"
                               "--save\n"
                               "--save-clusters\n"
                               "--no-gui\n"
                               "--potential\n"
                               "--fixed-colors\n"
                               "%s\n"
                               "%s\n"
                               , argv[0]
                               , wc_factory.getUsage ().c_str ()
                               , gb_factory.getUsage ().c_str ());
    return (1);
  }

  typename PointCloudT::Ptr cloud (new PointCloudT);
  typename NormalCloudT::Ptr normals (new NormalCloudT);

  if (!load<PointT> (argv[1], cloud, normals))
    return (1);

  bool option_save_seeds = pcl::console::find_switch (argc, argv, "--save-seeds");
  bool option_load_seeds = pcl::console::find_switch (argc, argv, "--load-seeds");

  bool mode_potential = pcl::console::find_switch (argc, argv, "--potential");
  bool option_fixed_colors = pcl::console::find_switch (argc, argv, "--fixed-colors");

  std::string seeds_save_filename;
  if (option_save_seeds)
    pcl::console::parse (argc, argv, "--save-seeds", seeds_save_filename);

  std::string seeds_load_filename;
  if (option_load_seeds)
    pcl::console::parse (argc, argv, "--load-seeds", seeds_load_filename);

  bool option_save = pcl::console::find_switch (argc, argv, "--save");
  bool option_save_clusters = pcl::console::find_switch (argc, argv, "--save-clusters");
  bool mode_no_gui = pcl::console::find_switch (argc, argv, "--no-gui");

  if (mode_no_gui && !option_load_seeds)
  {
    pcl::console::print_error ("No GUI mode can only be used with --load-seeds option.\n");
    return (2);
  }

  auto wc = wc_factory.instantiate (argc, argv);
  auto gb = gb_factory.instantiate (argc, argv);

  wc_factory.printValues ();
  gb_factory.printValues ();


  /*********************************************************************
   *                        Setup visualization                        *
   *********************************************************************/


  using namespace tviewer;
  auto viewer = create (!mode_no_gui);

  viewer->registerVisualizationObject<PointCloudObject<PointT>> (
      "input",
      "input point cloud",
      "i",
      cloud,
      4,
      0.95
  );


  /*********************************************************************
   *                         Pre-compute graph                         *
   *********************************************************************/


  GraphPtr g (new Graph);
  auto& graph = *g;

  gb->setInputCloud (cloud);

  MEASURE_RUNTIME ("Building graph... ", gb->compute (graph));
  MEASURE_RUNTIME ("Computing normals... ", pcl::graph::computeNormalsAndCurvatures (graph));
  MEASURE_RUNTIME ("Computing curvature signs... ", pcl::graph::computeSignedCurvatures (graph));
  MEASURE_RUNTIME ("Computing edge weights... ", wc (graph));

  pcl::console::print_info ("Built a graph with %zu vertices and %zu edges\n",
                            boost::num_vertices (graph),
                            boost::num_edges (graph));


  /*********************************************************************
   *                          Visualize graph                          *
   *********************************************************************/


  typedef GraphVisualizer<Graph> GraphVisualizer;
  GraphVisualizer gv (graph);

  viewer->registerVisualizationObject<PointCloudObject<pcl::PointXYZRGBA>> (
      "vertices",
      "graph vertices",
      "v",
      gv.getVerticesCloudColorsNatural (),
      6,
      0.95
  );

  viewer->registerVisualizationObject<PointCloudObject<pcl::PointXYZRGBA>> (
      "curvature",
      "vertex curvature",
      "C",
      gv.getVerticesCloudColorsCurvature (),
      6,
      0.95
  );

  viewer->registerVisualizationObject<NormalCloudObject> (
      "normals",
      "vertex normals",
      "n",
      gv.getVerticesNormalsCloud (),
      1,
      0.01
  );

  viewer->registerVisualizationObject<PolyDataObject> (
      "edges",
      "adjacency edges",
      "a",
      gv.getEdgesPolyData ()
  );

  viewer->showVisualizationObject ("vertices");


  /*********************************************************************
   *                          Seed selection                           *
   *********************************************************************/


  typename pcl::PointCloud<pcl::PointXYZL>::Ptr seeds_cloud (new pcl::PointCloud<pcl::PointXYZL>);
  std::vector<pcl::PointIndices> seeds_indices;

  if (option_load_seeds)
  {
    pcl::io::loadPCDFile<pcl::PointXYZL> (seeds_load_filename, *seeds_cloud);
  }
  else
  {
    viewer->waitPointsSelected (*seeds_cloud, seeds_indices);
    if (option_save_seeds)
      pcl::io::savePCDFile (seeds_save_filename, *seeds_cloud);
  }

  viewer->registerVisualizationObject<PointCloudObject<PointT>> (
      "seeds",
      "random walker seeds",
      "S",
      seeds::createColoredCloudFromSeeds (*seeds_cloud),
      14,
      0.65,
      0xFF0000
  );


  /*********************************************************************
   *                         Run segmentation                          *
   *********************************************************************/


  pcl::segmentation::RandomWalkerSegmentation<PointT> rws (mode_potential);
  rws.setInputGraph (g);
  rws.setSeeds (seeds_cloud);

  std::vector<pcl::PointIndices> clusters;

  rws.segment (clusters);

  viewer->registerVisualizationObject<PointCloudWithColorShufflingObject<pcl::PointXYZRGBA>> (
      "clusters",
      "object clusters",
      "c",
      option_fixed_colors ?
      std::bind (&GraphVisualizer::getVerticesCloudColorsFromPropertyFixed, gv) :
      std::bind (&GraphVisualizer::getVerticesCloudColorsFromPropertyRandom, gv),
      3,
      1.0
  );

  viewer->updateVisualizationObjects ();
  viewer->hideVisualizationObject ("vertices");

  if (mode_potential)
  {
    size_t index = 0;
    Eigen::VectorXf potential = rws.getPotentials ().col (0);

    viewer->registerVisualizationObject<PointCloudObject<PointT>> (
        "potential",
        "random walker potentials",
        "p",
        std::bind (&GraphVisualizer::getVerticesCloudColorsFromVector, gv, std::cref (potential)),
        3,
        1.0
    );

    viewer->updateVisualizationObject ("potential");
    viewer->showVisualizationObject ("potential");

    while (viewer->waitPointSelected (index))
    {
      uint32_t color = boost::get (boost::vertex_color, graph, index);
      if (color == 0)
      {
        pcl::console::print_warn ("Selected point has no label and therefore no potentials\n");
      }
      else
      {
        pcl::console::print_info ("Potential for vertex %zu (color %zu)\n", index, color);
        potential = rws.getPotentials ().col (color - 1);
        viewer->updateVisualizationObject ("potential");
      }
    }
  }
  else
  {
    viewer->showVisualizationObject ("clusters");
    viewer->run ();
  }

  if (option_save_clusters)
  {
    boost::format fmt ("cluster%i.pcd");
    for (size_t i = 0; i < clusters.size () - 1; ++i)
    {
      if (clusters[i].indices.size ())
      {
        PointCloudT cluster;
        pcl::copyPointCloud (*cloud, clusters[i], cluster);
        pcl::io::savePCDFile (boost::str (fmt % i), cluster);
      }
    }
  }

  if (option_save)
  {
    pcl::PointCloud<pcl::PointXYZL> labeled;
    pcl::copyPointCloud (*cloud, labeled);
    std::vector<boost::graph_traits<Graph>::vertex_descriptor> point_to_vertex_map;
    gb->getPointToVertexMap (point_to_vertex_map);
    for (size_t i = 0; i < labeled.size (); ++i)
    {
      const auto& v = point_to_vertex_map[i];
      if (v >= boost::num_vertices (graph))
        labeled[i].label = 0;
      else
        labeled[i].label = boost::get (boost::vertex_color, graph, v);
    }
    pcl::io::savePCDFile ("segmentation.pcd", labeled);
  }

  return (0);
}

