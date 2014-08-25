#include <ctime>

#include <boost/format.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <QModelIndex>

#include <vtkLine.h>
#include <vtkPolyLine.h>
#include <vtkPolyData.h>
#include <vtkCellData.h>
#include <vtkCellArray.h>
#include <vtkSmartPointer.h>
#include <vtkRenderWindow.h>

#include <pcl/io/pcd_io.h>
#include <pcl/common/io.h>

#include "tviewer/color.h"

#include "main_window.h"
#include "ui_main_window.h"

#include "io.h"
#include "graph/common.h"
#include "graph/edge_weight_computer.h"
#include "graph/voxel_grid_graph_builder.h"
#include "graph/nearest_neighbors_graph_builder.h"

using namespace tviewer;

MainWindow::MainWindow (const std::string& filename, QWidget* parent)
: QMainWindow (parent)
, ui_ (new Ui::MainWindow)
, graph_ (new Graph)
, seed_selection_ (new SeedSelection)
{
  srand (time (0));

  ui_->setupUi (this);
  viewer_.reset (new pcl::visualization::PCLVisualizer ("PCL Visualizer", false));
  ui_->qvtkWidget->SetRenderWindow (viewer_->getRenderWindow ());
  viewer_->setupInteractor (ui_->qvtkWidget->GetInteractor (),
                            ui_->qvtkWidget->GetRenderWindow ());

  viewer_->registerPointPickingCallback (&MainWindow::pointPickingCallback, *this);

  cloud_.reset (new PointCloudT);
  if (pcl::io::loadPCDFile (filename, *cloud_))
    throw std::runtime_error ("failed to load input point cloud");

  if (!hasColor (filename))
    for (auto& point : *cloud_)
      point.rgba = 0x00FFFF;

  pcl::PointCloud<pcl::PointXYZRGB>::Ptr tmp (new pcl::PointCloud<pcl::PointXYZRGB>);
  viewer_->addPointCloud (tmp, "vertices");

  viewer_->addPointCloud (tmp, "seeds");
  viewer_->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE,
                                             5, "seeds");

  ui_->qvtkWidget->update ();

  ui_->list_labels->setModel (seed_selection_.get ());

  connect (ui_->list_labels->selectionModel (),
           SIGNAL (selectionChanged (QItemSelection, QItemSelection)),
           seed_selection_.get (),
           SLOT (currentChanged (QItemSelection, QItemSelection)));

  connect (seed_selection_.get (),
           SIGNAL (seedsChanged ()),
           this,
           SLOT (seedsChanged ()));

  QModelIndex index = seed_selection_->addNewLabel ();
  ui_->list_labels->selectionModel ()->select (index, QItemSelectionModel::ClearAndSelect);

  loadConfig ();

  onButtonUpdateGraphClicked ();
}

MainWindow::~MainWindow ()
{
  saveConfig ();
  delete ui_;
}

void
MainWindow::seedsChanged ()
{
  displaySeeds ();
}

void
MainWindow::onButtonUpdateGraphClicked ()
{
  typename pcl::graph::GraphBuilder<PointT, Graph>::Ptr gb;

  switch (ui_->tabs_graph_builder->currentIndex ())
  {
    case 0: // Voxel Grid
      {
        double r = ui_->spinbox_voxel_resolution->value ();
        typedef pcl::graph::VoxelGridGraphBuilder<PointT, Graph> Builder;
        Builder* graph_builder = new Builder (r);
        gb.reset (graph_builder);
        break;
      }
    case 1: // KNN
      {
        int n = ui_->spinbox_nearest_neighbors->value ();
        typedef pcl::graph::NearestNeighborsGraphBuilder<PointT, Graph> Builder;
        Builder* graph_builder = new Builder;
        graph_builder->setNumberOfNeighbors (n);
        graph_builder->useNearestKSearch ();
        gb.reset (graph_builder);
        break;
      }
    case 2: // Radius
      {
        int n = ui_->spinbox_max_neighbors->value ();
        double r = ui_->spinbox_radius->value ();
        typedef pcl::graph::NearestNeighborsGraphBuilder<PointT, Graph> Builder;
        Builder* graph_builder = new Builder;
        graph_builder->setNumberOfNeighbors (n);
        graph_builder->setRadius (r);
        graph_builder->useRadiusSearch ();
        gb.reset (graph_builder);
        break;
      }
  }

  gb->setInputCloud (cloud_);
  buildGraph (gb);

  pcl::graph::computeNormalsAndCurvatures (*graph_);
  pcl::graph::computeSignedCurvatures (*graph_);
  computeEdgeWeights ();

  displayGraphVertices ();
  displayGraphEdges ();
}

void
MainWindow::buttonNewLabelClicked ()
{
  QModelIndex index = seed_selection_->addNewLabel ();
  ui_->list_labels->selectionModel ()->select (index, QItemSelectionModel::ClearAndSelect);
}

void
MainWindow::buttonDeleteLabelClicked ()
{
  seed_selection_->deleteLabel ();
}

void
MainWindow::buttonSegmentClicked ()
{
  pcl::segmentation::RandomWalkerSegmentation<pcl::PointXYZRGB> rws;
  rws.setInputGraph (graph_);
  rws.setSeeds (seed_selection_->getSelectedSeeds ());
  std::vector<pcl::PointIndices> clusters;
  rws.segment (clusters);
  displayGraphVertices (false);
}

void
MainWindow::pointPickingCallback (const pcl::visualization::PointPickingEvent& event, void*)
{
  int idx = event.getPointIndex ();
  if (idx == -1)
    return;

  pcl::PointXYZ p;
  event.getPoint (p.x, p.y, p.z);

  seed_selection_->pickPoint (p);
}

void
MainWindow::displayGraphVertices (bool how)
{
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr vertices (new pcl::PointCloud<pcl::PointXYZRGB>);
  if (ui_->action_graph_vertices->isChecked ())
  {
    pcl::copyPointCloud (*pcl::graph::point_cloud (*graph_), *vertices);
    boost::get (boost::vertex_color, *graph_);
    if (how == false)
      for (size_t i = 0; i < vertices->size (); ++i)
      {
        uint32_t label = boost::get (boost::vertex_color, *graph_, i);
        if (!colormap_.count (label))
          colormap_[label] = generateRandomColor ();
        vertices->at (i).rgba = colormap_[label];
      }
  }
  viewer_->updatePointCloud (vertices, "vertices");
  ui_->qvtkWidget->update ();
}

void
MainWindow::displayGraphEdges (uint32_t color)
{
  viewer_->removeShape ("edges");
  if (ui_->action_graph_edges->isChecked ())
  {
    vtkSmartPointer<vtkCellArray> cells = vtkSmartPointer<vtkCellArray>::New ();
    vtkSmartPointer<vtkPolyData> polydata = vtkSmartPointer<vtkPolyData>::New ();
    vtkSmartPointer<vtkPoints> points = vtkSmartPointer<vtkPoints>::New ();
    vtkSmartPointer<vtkUnsignedCharArray> colors = vtkSmartPointer<vtkUnsignedCharArray>::New ();
    colors->SetNumberOfComponents (3);
    unsigned char c[3];
    boost::graph_traits<Graph>::edge_iterator s, e;
    int id = 0;
    for (boost::tie (s, e) = boost::edges (*graph_); s != e; ++s)
    {
      vtkSmartPointer<vtkLine> line = vtkSmartPointer<vtkLine>::New ();
      points->InsertNextPoint ((*graph_)[boost::source (*s, *graph_)].getVector3fMap ().data ());
      points->InsertNextPoint ((*graph_)[boost::target (*s, *graph_)].getVector3fMap ().data ());
      line->GetPointIds ()->SetId (0, id++);
      line->GetPointIds ()->SetId (1, id++);
      cells->InsertNextCell (line);
      getRGBFromColor (getColor (boost::get (boost::edge_weight_t (), *graph_, *s)), c);
      colors->InsertNextTupleValue (c);
    }
    polydata->SetPoints (points);
    polydata->SetLines (cells);
    polydata->GetCellData ()->SetScalars (colors);
    viewer_->addModelFromPolyData (polydata, "edges");
  }
}

void
MainWindow::displaySeeds ()
{
  viewer_->updatePointCloud (seed_selection_->getPointCloudForVisualization (), "seeds");
  ui_->qvtkWidget->update ();
}

void
MainWindow::buildGraph (pcl::graph::GraphBuilder<PointT, Graph>::Ptr graph_builder)
{
  ui_->status_bar->showMessage ("Building graph...");

  graph_builder->compute (*graph_);

  boost::format fmt ("Built a graph with %i vertices and %i edges");
  std::string status (boost::str (fmt % boost::num_vertices (*graph_) % boost::num_edges (*graph_)));
  ui_->status_bar->showMessage (status.c_str ());
}

void
MainWindow::computeEdgeWeights ()
{
  using namespace pcl::graph;
  typedef EdgeWeightComputer<Graph> EWC;
  EWC computer;
  if (ui_->checkbox_xyz->checkState ())
  {
    float influence = ui_->spinbox_xyz_influence->value ();
    float multiplier = ui_->checkbox_xyz_only_concave->checkState () ? 0.0 : 1.0;
    computer.addTerm<terms::XYZ> (influence, multiplier, EWC::NORMALIZATION_LOCAL);
  }
  if (ui_->checkbox_normal->checkState ())
  {
    float influence = ui_->spinbox_normal_influence->value ();
    float multiplier = ui_->checkbox_normal_only_concave->checkState () ? 0.0 : 1.0;
    computer.addTerm<terms::Normal> (influence, multiplier);
  }
  if (ui_->checkbox_curvature->checkState ())
  {
    float influence = ui_->spinbox_curvature_influence->value ();
    float multiplier = ui_->checkbox_curvature_only_concave->checkState () ? 0.0 : 1.0;
    computer.addTerm<terms::Curvature> (influence, multiplier);
  }
  if (ui_->checkbox_rgb->checkState ())
  {
    float influence = ui_->spinbox_rgb_influence->value ();
    float multiplier = ui_->checkbox_rgb_only_concave->checkState () ? 0.0 : 1.0;
    computer.addTerm<terms::RGB> (influence, multiplier, EWC::NORMALIZATION_GLOBAL);
  }
  computer.setSmallWeightThreshold (1e-5);
  computer.setSmallWeightPolicy (EWC::SMALL_WEIGHT_COERCE_TO_THRESHOLD);
  computer.compute (*graph_);
}

void
MainWindow::saveConfig ()
{
  using boost::property_tree::ptree;
  ptree pt;

  pt.put ("GraphBuilder.Type", ui_->tabs_graph_builder->currentIndex ());
  pt.put ("GraphBuilder.VoxelGrid.Resolution", ui_->spinbox_voxel_resolution->value ());
  pt.put ("GraphBuilder.KNN.NearestNeighbors", ui_->spinbox_nearest_neighbors->value ());
  pt.put ("GraphBuilder.Radius.Radius", ui_->spinbox_radius->value ());
  pt.put ("GraphBuilder.Radius.MaxNeighbors", ui_->spinbox_max_neighbors->value ());

  pt.put ("EdgeWeights.XYZ.Enabled", ui_->checkbox_xyz->checkState ());
  pt.put ("EdgeWeights.XYZ.Influence", ui_->spinbox_xyz_influence->value ());
  pt.put ("EdgeWeights.XYZ.OnlyConcave", ui_->checkbox_rgb_only_concave->checkState ());
  pt.put ("EdgeWeights.Normal.Enabled", ui_->checkbox_normal->checkState ());
  pt.put ("EdgeWeights.Normal.Influence", ui_->spinbox_normal_influence->value ());
  pt.put ("EdgeWeights.Normal.OnlyConcave", ui_->checkbox_normal_only_concave->checkState ());
  pt.put ("EdgeWeights.Curvature.Enabled", ui_->checkbox_curvature->checkState ());
  pt.put ("EdgeWeights.Curvature.Influence", ui_->spinbox_curvature_influence->value ());
  pt.put ("EdgeWeights.Curvature.OnlyConcave", ui_->checkbox_curvature_only_concave->checkState ());
  pt.put ("EdgeWeights.RGB.Enabled", ui_->checkbox_rgb->checkState ());
  pt.put ("EdgeWeights.RGB.Influence", ui_->spinbox_rgb_influence->value ());
  pt.put ("EdgeWeights.RGB.OnlyConcave", ui_->checkbox_rgb_only_concave->checkState ());

  write_json ("config.json", pt);
}

void
MainWindow::loadConfig ()
{
  using boost::property_tree::ptree;
  ptree pt;

  try
  {
    read_json ("config.json", pt);
  }
  catch (std::runtime_error& e)
  {
    // Cannot load config file, do nothing
    return;
  }

  ui_->tabs_graph_builder->setCurrentIndex (pt.get ("GraphBuilder.Type", 0));
  ui_->spinbox_voxel_resolution->setValue (pt.get ("GraphBuilder.VoxelGrid.Resolution", 0.005));
  ui_->spinbox_nearest_neighbors->setValue (pt.get ("GraphBuilder.KNN.NearestNeighbors", 15));
  ui_->spinbox_radius->setValue (pt.get ("GraphBuilder.Radius.Radius", 0.01));
  ui_->spinbox_max_neighbors->setValue (pt.get ("GraphBuilder.Radius.MaxNeighbors", 10));

  ui_->checkbox_xyz->setCheckState (Qt::CheckState (pt.get ("EdgeWeights.XYZ.Enabled", 2)));
  ui_->spinbox_xyz_influence->setValue (pt.get ("EdgeWeights.XYZ.Influence", 3.0));
  ui_->checkbox_rgb_only_concave->setCheckState (Qt::CheckState (pt.get ("EdgeWeights.XYZ.OnlyConcave", 0)));
  ui_->checkbox_normal->setCheckState (Qt::CheckState (pt.get ("EdgeWeights.Normal.Enabled", 2)));
  ui_->spinbox_normal_influence->setValue (pt.get ("EdgeWeights.Normal.Influence", 0.01));
  ui_->checkbox_normal_only_concave->setCheckState (Qt::CheckState (pt.get ("EdgeWeights.Normal.OnlyConcave", 2)));
  ui_->checkbox_curvature->setCheckState (Qt::CheckState (pt.get ("EdgeWeights.Curvature.Enabled", 2)));
  ui_->spinbox_curvature_influence->setValue (pt.get ("EdgeWeights.Curvature.Influence", 0.0001));
  ui_->checkbox_curvature_only_concave->setCheckState (Qt::CheckState (pt.get ("EdgeWeights.Curvature.OnlyConcave", 2)));
  ui_->checkbox_rgb->setCheckState (Qt::CheckState (pt.get ("EdgeWeights.RGB.Enabled", 2)));
  ui_->spinbox_rgb_influence->setValue (pt.get ("EdgeWeights.RGB.Influence", 3.0));
  ui_->checkbox_rgb_only_concave->setCheckState (Qt::CheckState (pt.get ("EdgeWeights.RGB.OnlyConcave", 0)));
}

