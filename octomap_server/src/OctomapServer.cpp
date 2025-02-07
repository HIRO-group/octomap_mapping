/*
 * Copyright (c) 2010-2013, A. Hornung, University of Freiburg
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the University of Freiburg nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <octomap_server/OctomapServer.h>
#include <map>

using namespace octomap;
using octomap_msgs::Octomap;


bool is_equal (double a, double b, double epsilon = 1.0e-7)
{
    return std::abs(a - b) < epsilon;
}

namespace octomap_server{

OctomapServer::OctomapServer(const ros::NodeHandle private_nh_, const ros::NodeHandle &nh_)
: m_nh(nh_),
  m_nh_private(private_nh_),
  m_pointCloudSub(NULL),
  m_tfPointCloudSub(NULL),
  m_reconfigureServer(m_config_mutex, private_nh_),
  m_octree(NULL),
  m_maxRange(-1.0),
  m_worldFrameId("/map"), m_baseFrameId("base_footprint"),
  m_useHeightMap(true),
  m_useColoredMap(false),
  m_colorFactor(0.8),
  m_latchedTopics(true),
  m_publishFreeSpace(false),
  m_res(0.05),
  m_treeDepth(0),
  m_maxTreeDepth(0),
  m_pointcloudMinX(-std::numeric_limits<double>::max()),
  m_pointcloudMaxX(std::numeric_limits<double>::max()),
  m_pointcloudMinY(-std::numeric_limits<double>::max()),
  m_pointcloudMaxY(std::numeric_limits<double>::max()),
  m_pointcloudMinZ(-std::numeric_limits<double>::max()),
  m_pointcloudMaxZ(std::numeric_limits<double>::max()),
  m_occupancyMinZ(-std::numeric_limits<double>::max()),
  m_occupancyMaxZ(std::numeric_limits<double>::max()),
  m_minSizeX(0.0), m_minSizeY(0.0),
  m_filterSpeckles(false), m_filterGroundPlane(false),
  m_groundFilterDistance(0.04), m_groundFilterAngle(0.15), m_groundFilterPlaneDistance(0.07),
  m_compressMap(true),
  m_incrementalUpdate(false),
  m_initConfig(true)
{

  double probHit, probMiss, thresMin, thresMax;

  m_nh_private.param("frame_id", m_worldFrameId, m_worldFrameId);
  m_nh_private.param("base_frame_id", m_baseFrameId, m_baseFrameId);
  m_nh_private.param("height_map", m_useHeightMap, m_useHeightMap);
  m_nh_private.param("colored_map", m_useColoredMap, m_useColoredMap);
  m_nh_private.param("color_factor", m_colorFactor, m_colorFactor);


  m_nh_private.param("pointcloud_min_x", m_pointcloudMinX,m_pointcloudMinX);
  m_nh_private.param("pointcloud_max_x", m_pointcloudMaxX,m_pointcloudMaxX);
  m_nh_private.param("pointcloud_min_y", m_pointcloudMinY,m_pointcloudMinY);
  m_nh_private.param("pointcloud_max_y", m_pointcloudMaxY,m_pointcloudMaxY);
  m_nh_private.param("pointcloud_min_z", m_pointcloudMinZ,m_pointcloudMinZ);
  m_nh_private.param("pointcloud_max_z", m_pointcloudMaxZ,m_pointcloudMaxZ);
  m_nh_private.param("occupancy_min_z", m_occupancyMinZ,m_occupancyMinZ);
  m_nh_private.param("occupancy_max_z", m_occupancyMaxZ,m_occupancyMaxZ);
  m_nh_private.param("min_x_size", m_minSizeX,m_minSizeX);
  m_nh_private.param("min_y_size", m_minSizeY,m_minSizeY);

  m_nh_private.param("filter_speckles", m_filterSpeckles, m_filterSpeckles);
  m_nh_private.param("filter_ground", m_filterGroundPlane, m_filterGroundPlane);
  // distance of points from plane for RANSAC
  m_nh_private.param("ground_filter/distance", m_groundFilterDistance, m_groundFilterDistance);
  // angular derivation of found plane:
  m_nh_private.param("ground_filter/angle", m_groundFilterAngle, m_groundFilterAngle);
  // distance of found plane from z=0 to be detected as ground (e.g. to exclude tables)
  m_nh_private.param("ground_filter/plane_distance", m_groundFilterPlaneDistance, m_groundFilterPlaneDistance);

  m_nh_private.param("sensor_model/max_range", m_maxRange, m_maxRange);

  m_nh_private.param("resolution", m_res, m_res);
  m_nh_private.param("sensor_model/hit", probHit, 0.7);
  m_nh_private.param("sensor_model/miss", probMiss, 0.4);
  m_nh_private.param("sensor_model/min", thresMin, 0.12);
  m_nh_private.param("sensor_model/max", thresMax, 0.97);
  m_nh_private.param("compress_map", m_compressMap, m_compressMap);
  m_nh_private.param("incremental_2D_projection", m_incrementalUpdate, m_incrementalUpdate);

  if (m_filterGroundPlane && (m_pointcloudMinZ > 0.0 || m_pointcloudMaxZ < 0.0)){
    ROS_WARN_STREAM("You enabled ground filtering but incoming pointclouds will be pre-filtered in ["
              <<m_pointcloudMinZ <<", "<< m_pointcloudMaxZ << "], excluding the ground level z=0. "
              << "This will not work.");
  }

  if (m_useHeightMap && m_useColoredMap) {
    ROS_WARN_STREAM("You enabled both height map and RGB color registration. This is contradictory. Defaulting to height map.");
    m_useColoredMap = false;
  }

  if (m_useColoredMap) {
#ifdef COLOR_OCTOMAP_SERVER
    ROS_INFO_STREAM("Using RGB color registration (if information available)");
#else
    ROS_ERROR_STREAM("Colored map requested in launch file - node not running/compiled to support colors, please define COLOR_OCTOMAP_SERVER and recompile or launch the octomap_color_server node");
#endif
  }

  // initialize octomap object & params
  m_octree = new OcTreeT(m_res);
  m_octree->setProbHit(probHit);
  m_octree->setProbMiss(probMiss);
  m_octree->setClampingThresMin(thresMin);
  m_octree->setClampingThresMax(thresMax);
  m_treeDepth = m_octree->getTreeDepth();
  m_maxTreeDepth = m_treeDepth;
  m_gridmap.info.resolution = m_res;

  double r, g, b, a;
  m_nh_private.param("color/r", r, 0.0);
  m_nh_private.param("color/g", g, 0.0);
  m_nh_private.param("color/b", b, 1.0);
  m_nh_private.param("color/a", a, 1.0);
  m_color.r = r;
  m_color.g = g;
  m_color.b = b;
  m_color.a = a;

  m_nh_private.param("color_free/r", r, 0.0);
  m_nh_private.param("color_free/g", g, 1.0);
  m_nh_private.param("color_free/b", b, 0.0);
  m_nh_private.param("color_free/a", a, 1.0);
  m_colorFree.r = r;
  m_colorFree.g = g;
  m_colorFree.b = b;
  m_colorFree.a = a;



  m_nh_private.param("publish_free_space", m_publishFreeSpace, m_publishFreeSpace);

  m_nh_private.param("latch", m_latchedTopics, m_latchedTopics);
  if (m_latchedTopics){
    ROS_INFO("Publishing latched (single publish will take longer, all topics are prepared)");
  } else
    ROS_INFO("Publishing non-latched (topics are only prepared as needed, will only be re-published on map change");

  m_markerPub = m_nh.advertise<visualization_msgs::MarkerArray>("occupied_cells_vis_array", 1, m_latchedTopics);
  m_binaryMapPub = m_nh.advertise<Octomap>("octomap_binary", 1, m_latchedTopics);
  m_fullMapPub = m_nh.advertise<Octomap>("octomap_full", 1, m_latchedTopics);
  m_pointCloudPub = m_nh.advertise<sensor_msgs::PointCloud2>("octomap_point_cloud_centers", 1, m_latchedTopics);
  m_mapPub = m_nh.advertise<nav_msgs::OccupancyGrid>("projected_map", 5, m_latchedTopics);
  m_fmarkerPub = m_nh.advertise<visualization_msgs::MarkerArray>("free_cells_vis_array", 1, m_latchedTopics);


  m_pointCloudSub = new message_filters::Subscriber<sensor_msgs::PointCloud2> (m_nh, "cloud_in", 1);
  m_tfPointCloudSub = new tf::MessageFilter<sensor_msgs::PointCloud2> (*m_pointCloudSub, m_tfListener, m_worldFrameId, 1);
  m_tfPointCloudSub->registerCallback(boost::bind(&OctomapServer::insertCloudCallback, this, _1));

  // m_proximitySensorSub = new message_filters::Subscriber<sensor_msgs::LaserScan> (m_nh, "proximity_data160", 10);
  // m_tfProximitySensorSub = new tf::MessageFilter<sensor_msgs::LaserScan> (*m_proximitySensorSub, m_tfListener, m_worldFrameId, 10);
  // m_tfProximitySensorSub->registerCallback(boost::bind(&OctomapServer::insertSingleSensorCallback, this, _1));

  m_combinedProximitySensorSub = new message_filters::Subscriber<hiro_collision_avoidance::CombinedProximityData> (m_nh, "proximity_combined", 1);
  m_tfCombinedProximitySensorSub = new tf::MessageFilter<hiro_collision_avoidance::CombinedProximityData> (*m_combinedProximitySensorSub, m_tfListener, m_worldFrameId, 1);
  // m_tfCombinedProximitySensorSub->registerCallback(&OctomapServer::insertCombinedProximityDataCallback, this);
  m_tfCombinedProximitySensorSub->registerCallback(boost::bind(&OctomapServer::insertCombinedProximityDataCallback, this, _1));


  // ros::Subscriber sub = m_nh.subscribe("proximity_combined", 1, boost::bind(&OctomapServer::insertCombinedProximityDataCallback, this, _1), this);
  m_octomapBinaryService = m_nh.advertiseService("octomap_binary", &OctomapServer::octomapBinarySrv, this);
  m_octomapFullService = m_nh.advertiseService("octomap_full", &OctomapServer::octomapFullSrv, this);
  m_clearBBXService = m_nh_private.advertiseService("clear_bbx", &OctomapServer::clearBBXSrv, this);
  m_resetService = m_nh_private.advertiseService("reset", &OctomapServer::resetSrv, this);

  dynamic_reconfigure::Server<OctomapServerConfig>::CallbackType f;
  f = boost::bind(&OctomapServer::reconfigureCallback, this, _1, _2);
  m_reconfigureServer.setCallback(f);
}

OctomapServer::~OctomapServer(){
  if (m_tfPointCloudSub){
    delete m_tfPointCloudSub;
    m_tfPointCloudSub = NULL;
  }

  if (m_pointCloudSub){
    delete m_pointCloudSub;
    m_pointCloudSub = NULL;
  }


  if (m_octree){
    delete m_octree;
    m_octree = NULL;
  }

}

bool OctomapServer::openFile(const std::string& filename){
  if (filename.length() <= 3)
    return false;

  std::string suffix = filename.substr(filename.length()-3, 3);
  if (suffix== ".bt"){
    if (!m_octree->readBinary(filename)){
      return false;
    }
  } else if (suffix == ".ot"){
    AbstractOcTree* tree = AbstractOcTree::read(filename);
    if (!tree){
      return false;
    }
    if (m_octree){
      delete m_octree;
      m_octree = NULL;
    }
    m_octree = dynamic_cast<OcTreeT*>(tree);
    if (!m_octree){
      ROS_ERROR("Could not read OcTree in file, currently there are no other types supported in .ot");
      return false;
    }

  } else{
    return false;
  }

  ROS_INFO("Octomap file %s loaded (%zu nodes).", filename.c_str(),m_octree->size());

  m_treeDepth = m_octree->getTreeDepth();
  m_maxTreeDepth = m_treeDepth;
  m_res = m_octree->getResolution();
  m_gridmap.info.resolution = m_res;
  double minX, minY, minZ;
  double maxX, maxY, maxZ;
  m_octree->getMetricMin(minX, minY, minZ);
  m_octree->getMetricMax(maxX, maxY, maxZ);

  m_updateBBXMin[0] = m_octree->coordToKey(minX);
  m_updateBBXMin[1] = m_octree->coordToKey(minY);
  m_updateBBXMin[2] = m_octree->coordToKey(minZ);

  m_updateBBXMax[0] = m_octree->coordToKey(maxX);
  m_updateBBXMax[1] = m_octree->coordToKey(maxY);
  m_updateBBXMax[2] = m_octree->coordToKey(maxZ);

  publishAll();

  return true;

}


// this is the callback for the combined proximity sensor data
void OctomapServer::insertCombinedProximityDataCallback(const hiro_collision_avoidance::CombinedProximityData::ConstPtr& combinedPoints){
  ros::WallTime startTime = ros::WallTime::now();
  // required info for sensor message:
  // 3d points
  // header with frame_id of one skin unit
  // create sensor message
  sensor_msgs::PointCloud2 cloud_;
  PCLPointCloud global_pc;
  PCLPointCloud pc; // input cloud for filtering and ground-detection

  bool isInf = false;
  std::vector<bool> isInfVector;
  std::vector<tf::Point> sensorPositions;
  // add to the point cloud
  #ifdef COLOR_OCTOMAP_SERVER
    for (auto it = combinedPoints->ranges.begin(); it != combinedPoints->ranges.end(); ++it) {
      if (isinf(*it)) {
        isInfVector.push_back(true);
        pc.push_back(pcl::PointXYZRGB(2.0, 0, 0));

      } else {
        isInfVector.push_back(false);
        pc.push_back(pcl::PointXYZRGB(*it, 0, 0));

      }
    }
  #else
    // this is the point cloud for the octomap
    for(int i = 0; i < combinedPoints->ranges.size(); i++) {
      PCLPointCloud individual_sensor_cloud;
      if (isinf(combinedPoints->ranges[i])) {
        isInfVector.push_back(true);
        individual_sensor_cloud.push_back(pcl::PointXYZ(2.0, 0, 0));
      } else {
        isInfVector.push_back(false);
        if (combinedPoints->ranges[i] > 2.0) {
          individual_sensor_cloud.push_back(pcl::PointXYZ(2.0, 0, 0));
        } else {
          individual_sensor_cloud.push_back(pcl::PointXYZ(combinedPoints->ranges[i], 0, 0));
        }
      }

      std::string frame_id = combinedPoints->frame_ids[i];
      tf::StampedTransform sensorToWorldTf;
      try {
        m_tfListener.waitForTransform("/world", combinedPoints->frame_ids[i], combinedPoints->header.stamp, ros::Duration(1.0));
        m_tfListener.lookupTransform("/world", combinedPoints->frame_ids[i], combinedPoints->header.stamp, sensorToWorldTf);
      } catch(tf::TransformException& ex){
        ROS_ERROR_STREAM( "Transform error of sensor data: " << ex.what() << ", quitting callback");
        return;
      }

      // convert sensor to world transform to matrix
      Eigen::Matrix4f sensorToWorld;
      pcl_ros::transformAsMatrix(sensorToWorldTf, sensorToWorld);

      // no filtering, just insert all points
      // no filtering of ground plane
      // directly transform to map frame:
      pcl::transformPointCloud(individual_sensor_cloud, individual_sensor_cloud, sensorToWorld);

      global_pc.push_back(individual_sensor_cloud.points[0]);
      sensorPositions.push_back(sensorToWorldTf.getOrigin());

    }


  #endif


  // set up filter for height range, also removes NANs:
  pcl::PassThrough<PCLPoint> pass_x;
  pass_x.setFilterFieldName("x");
  pass_x.setFilterLimits(m_pointcloudMinX, m_pointcloudMaxX);
  pcl::PassThrough<PCLPoint> pass_y;
  pass_y.setFilterFieldName("y");
  pass_y.setFilterLimits(m_pointcloudMinY, m_pointcloudMaxY);
  pcl::PassThrough<PCLPoint> pass_z;
  pass_z.setFilterFieldName("z");
  pass_z.setFilterLimits(m_pointcloudMinZ, m_pointcloudMaxZ);

  pass_x.setInputCloud(global_pc.makeShared());
  pass_x.filter(global_pc);

  pass_y.setInputCloud(global_pc.makeShared());
  pass_y.filter(global_pc);
  pass_z.setInputCloud(global_pc.makeShared());
  pass_z.filter(global_pc);

  PCLPointCloud pc_ground; // segmented ground plane
  PCLPointCloud pc_nonground; // everything else

  pc_nonground = global_pc;
  // pc_nonground is empty without ground segmentation
  pc_ground.header = global_pc.header;
  pc_nonground.header = global_pc.header;

  // uncomment the below lines to only use kinect data
  // pc_nonground = pc_ground;
  // pc_nonground.header = global_pc.header;

  // especially for moving obstacles
  insertScanBatch(sensorPositions, pc_ground, pc_nonground, isInfVector, true);

  double total_elapsed = (ros::WallTime::now() - startTime).toSec();
  ROS_DEBUG("Pointcloud insertion in OctomapServer done (%zu+%zu pts (ground/nonground), %f sec)", pc_ground.size(), pc_nonground.size(), total_elapsed);

  publishAll(combinedPoints->header.stamp);

}




// this is the callback for a single sensor
void OctomapServer::insertSingleSensorCallback(const sensor_msgs::LaserScan::ConstPtr& scanPoint){

  ros::WallTime startTime = ros::WallTime::now();
  // std::cout << "received proximity message from " << scanPoint->header.frame_id << std::endl;
  // required info for sensor message:
  // 3d points
  // header with frame_id of one skin unit
  // create sensor message
  sensor_msgs::PointCloud2 cloud_;

  // cloud_.header = scanPoint->header;
  // cloud_.height = 1;
  // cloud_.width = scanPoint->ranges.size();
  // cloud_.data.resize(scanPoint->ranges.size());
  // cloud_.data[0] = scanPoint->ranges[0];
  // cloud_.row_step = 1;
  //
  // ground filtering in base frame
  //
  // m_laserProjection.projectLaser(*scanPoint, cloud_, 5.0, laser_geometry::channel_option::Distance);
  // std::cout << cloud_.data[0] << "test" << std::endl;
  // sensor_msgs::PointCloud2 *cloud = &cloud_;

  PCLPointCloud pc; // input cloud for filtering and ground-detection
  // pcl::fromROSMsg(*cloud, pc);
  bool isInf = false;

  #ifdef COLOR_OCTOMAP_SERVER
    if (isinf(scanPoint->ranges[0])) {
      pc.push_back(pcl::PointXYZRGB(1.0, 0, 0));

    } else {
      pc.push_back(pcl::PointXYZRGB(scanPoint->ranges[0], 0, 0));
    }
  #else
    // this is the point cloud for the octomap
    if (isinf(scanPoint->ranges[0])) {
      pc.push_back(pcl::PointXYZ(2.0, 0, 0));
      isInf = true;

    } else {
      pc.push_back(pcl::PointXYZ(scanPoint->ranges[0], 0, 0));
    }
  #endif

  // std::cout << pc.points[0].x << " " << pc.points[0].y << " " << pc.points[0].z << std::endl;

//   int sensor_number = std::stoi(scan->header.frame_id.substr(scan->header.frame_id.find_first_of("0123456789"), scan->header.frame_id.length() -1))-startIdx;
//     try {
//         listener.lookupTransform("/world", "/proximity_link" + std::to_string(sensor_number+startIdx),
//                                  ros::Time(0), transform[sensor_number]);
//         translation1[sensor_number] << transform[sensor_number].getOrigin().getX(),
//                                        transform[sensor_number].getOrigin().getY(),
//                                        transform[sensor_number].getOrigin().getZ();
//         translation2[sensor_number] << scan->ranges[0],
//                                        0.0,
//                                        0.0;
//         rotation[sensor_number].w() = transform[sensor_number].getRotation().getW();
//         rotation[sensor_number].x() = transform[sensor_number].getRotation().getX();
//         rotation[sensor_number].y() = transform[sensor_number].getRotation().getY();
//         rotation[sensor_number].z() = transform[sensor_number].getRotation().getZ();
//         live_points[sensor_number] = translation1[sensor_number] + (rotation[sensor_number] * translation2[sensor_number]);
//     } catch (tf::TransformException ex) {
//         ROS_ERROR("%s", ex.what());
//         // ros::Duration(1.0).sleep();
//     }
// }

  std::vector<float> sphere_radiuses{0.23, 0.24, 0.2, 0.237, 0.225, 0.20, 0.27, 0.3};




  tf::StampedTransform sensorToWorldTf;
  try {
    m_tfListener.lookupTransform("/world", scanPoint->header.frame_id, scanPoint->header.stamp, sensorToWorldTf);
  } catch(tf::TransformException& ex){
    ROS_ERROR_STREAM( "Transform error of sensor data: " << ex.what() << ", quitting callback");
    return;
  }

  // convert sensor to world transform to matrix
  Eigen::Matrix4f sensorToWorld;
  pcl_ros::transformAsMatrix(sensorToWorldTf, sensorToWorld);


  // set up filter for height range, also removes NANs:
  pcl::PassThrough<PCLPoint> pass_x;
  pass_x.setFilterFieldName("x");
  pass_x.setFilterLimits(m_pointcloudMinX, m_pointcloudMaxX);
  pcl::PassThrough<PCLPoint> pass_y;
  pass_y.setFilterFieldName("y");
  pass_y.setFilterLimits(m_pointcloudMinY, m_pointcloudMaxY);
  pcl::PassThrough<PCLPoint> pass_z;
  pass_z.setFilterFieldName("z");
  pass_z.setFilterLimits(m_pointcloudMinZ, m_pointcloudMaxZ);

  PCLPointCloud pc_ground; // segmented ground plane
  PCLPointCloud pc_nonground; // everything else

  if (m_filterGroundPlane){
    tf::StampedTransform sensorToBaseTf, baseToWorldTf;
    try{
      m_tfListener.waitForTransform("/world", scanPoint->header.frame_id, scanPoint->header.stamp, ros::Duration(0.2));
      m_tfListener.lookupTransform("/world", scanPoint->header.frame_id, scanPoint->header.stamp, sensorToBaseTf);
      m_tfListener.lookupTransform("/world", "/world", scanPoint->header.stamp, baseToWorldTf);


    }catch(tf::TransformException& ex){
      ROS_ERROR_STREAM( "Transform error for ground plane filter: " << ex.what() << ", quitting callback.\n"
                        "You need to set the base_frame_id or disable filter_ground.");
    }


    Eigen::Matrix4f sensorToBase, baseToWorld;
    pcl_ros::transformAsMatrix(sensorToBaseTf, sensorToBase);
    pcl_ros::transformAsMatrix(baseToWorldTf, baseToWorld);

    // transform pointcloud from sensor frame to fixed robot frame
    pcl::transformPointCloud(pc, pc, sensorToBase);
    pass_x.setInputCloud(pc.makeShared());
    pass_x.filter(pc);

    pass_y.setInputCloud(pc.makeShared());
    pass_y.filter(pc);
    pass_z.setInputCloud(pc.makeShared());
    pass_z.filter(pc);
    filterGroundPlane(pc, pc_ground, pc_nonground);

    // transform clouds to world frame for insertion
    pcl::transformPointCloud(pc_ground, pc_ground, baseToWorld);
    pcl::transformPointCloud(pc_nonground, pc_nonground, baseToWorld);
  } else {
    // no filtering, just insert all points
    // no filtering of ground plane
    // directly transform to map frame:
    pcl::transformPointCloud(pc, pc, sensorToWorld);

    pass_x.setInputCloud(pc.makeShared());
    pass_x.filter(pc);

    pass_y.setInputCloud(pc.makeShared());
    pass_y.filter(pc);
    pass_z.setInputCloud(pc.makeShared());
    pass_z.filter(pc);

    pc_nonground = pc;
    // pc_nonground is empty without ground segmentation
    pc_ground.header = pc.header;
    pc_nonground.header = pc.header;
  }

  // todo -- we will need to implement some kind of decay here over time,
  // especially for moving obstacles
  insertScan(sensorToWorldTf.getOrigin(), pc_ground, pc_nonground, true, isInf);

  double total_elapsed = (ros::WallTime::now() - startTime).toSec();
  ROS_DEBUG("Pointcloud insertion in OctomapServer done (%zu+%zu pts (ground/nonground), %f sec)", pc_ground.size(), pc_nonground.size(), total_elapsed);

  publishAll(scanPoint->header.stamp);
}



// this is the cloud callback for the point cloud
void OctomapServer::insertCloudCallback(const sensor_msgs::PointCloud2::ConstPtr& cloud){
  ros::WallTime startTime = ros::WallTime::now();
  // std::cout << "received msg" << std::endl;

  //
  // ground filtering in base frame
  //
  PCLPointCloud pc; // input cloud for filtering and ground-detection
  pcl::fromROSMsg(*cloud, pc);

  tf::StampedTransform sensorToWorldTf;
  try {
    m_tfListener.lookupTransform(m_worldFrameId, cloud->header.frame_id, cloud->header.stamp, sensorToWorldTf);
  } catch(tf::TransformException& ex){
    ROS_ERROR_STREAM( "Transform error of sensor data: " << ex.what() << ", quitting callback");
    return;
  }

  Eigen::Matrix4f sensorToWorld;
  pcl_ros::transformAsMatrix(sensorToWorldTf, sensorToWorld);


  // set up filter for height range, also removes NANs:
  pcl::PassThrough<PCLPoint> pass_x;
  pass_x.setFilterFieldName("x");
  pass_x.setFilterLimits(m_pointcloudMinX, m_pointcloudMaxX);
  pcl::PassThrough<PCLPoint> pass_y;
  pass_y.setFilterFieldName("y");
  pass_y.setFilterLimits(m_pointcloudMinY, m_pointcloudMaxY);
  pcl::PassThrough<PCLPoint> pass_z;
  pass_z.setFilterFieldName("z");
  pass_z.setFilterLimits(m_pointcloudMinZ, m_pointcloudMaxZ);

  PCLPointCloud pc_ground; // segmented ground plane
  PCLPointCloud pc_nonground; // everything else
  // std::cout << m_filterGroundPlane << std::endl;
  if (m_filterGroundPlane){
    tf::StampedTransform sensorToBaseTf, baseToWorldTf;
    try{
      m_tfListener.waitForTransform("/world", cloud->header.frame_id, cloud->header.stamp, ros::Duration(0.2));
      m_tfListener.lookupTransform("/world", cloud->header.frame_id, cloud->header.stamp, sensorToBaseTf);
      m_tfListener.lookupTransform("/world", "/world", cloud->header.stamp, baseToWorldTf);


    }catch(tf::TransformException& ex){
      ROS_ERROR_STREAM( "Transform error for ground plane filter: " << ex.what() << ", quitting callback.\n"
                        "You need to set the base_frame_id or disable filter_ground.");
    }


    Eigen::Matrix4f sensorToBase, baseToWorld;
    pcl_ros::transformAsMatrix(sensorToBaseTf, sensorToBase);
    pcl_ros::transformAsMatrix(baseToWorldTf, baseToWorld);

    // transform pointcloud from sensor frame to fixed robot frame
    pcl::transformPointCloud(pc, pc, sensorToBase);
    pass_x.setInputCloud(pc.makeShared());
    pass_x.filter(pc);
    pass_y.setInputCloud(pc.makeShared());
    pass_y.filter(pc);
    pass_z.setInputCloud(pc.makeShared());
    pass_z.filter(pc);
    filterGroundPlane(pc, pc_ground, pc_nonground);

    // transform clouds to world frame for insertion
    pcl::transformPointCloud(pc_ground, pc_ground, baseToWorld);
    pcl::transformPointCloud(pc_nonground, pc_nonground, baseToWorld);
  } else {
    // directly transform to map frame:
    pcl::transformPointCloud(pc, pc, sensorToWorld);

    // just filter height range:
    pass_x.setInputCloud(pc.makeShared());
    pass_x.filter(pc);
    pass_y.setInputCloud(pc.makeShared());
    pass_y.filter(pc);
    pass_z.setInputCloud(pc.makeShared());
    pass_z.filter(pc);

    pc_nonground = pc;
    // pc_nonground is empty without ground segmentation
    pc_ground.header = pc.header;
    pc_nonground.header = pc.header;
  }
  pc_ground_kinect = pc_ground;
  pc_nonground_kinect = pc_nonground;
  kinect_sensor_origin = sensorToWorldTf.getOrigin();
  currentKinectStamp = cloud->header.stamp.toSec();
  return;

  // insertScan(sensorToWorldTf.getOrigin(), pc_ground, pc_nonground, false);

  double total_elapsed = (ros::WallTime::now() - startTime).toSec();
  ROS_DEBUG("Pointcloud insertion in OctomapServer done (%zu+%zu pts (ground/nonground), %f sec)", pc_ground.size(), pc_nonground.size(), total_elapsed);

  publishAll(cloud->header.stamp);
}

// insert scan with batched proximity data
void OctomapServer::insertScanBatch(const std::vector<tf::Point>& sensorOrigins, const PCLPointCloud& ground, const PCLPointCloud& nonground, const std::vector<bool> isInfVector, bool isProximityScan = false) {

  // log odds idea:
  /*
  based on the type of sensor:
  - if it is a kinect, we are more confident about it at a distance
  - if it is the proximity sensor(s), we are more confident about it at lower distances
  - formalization:
  - log_odds(kinect_point) = 1.5d for d <= 0.5m
                             -0.2d + 0.85 for d > 0.5m
  - log_odds(proximity_point) = -0.5d + 1

  */

  point3d kinectSensorOrigin = pointTfToOctomap(kinect_sensor_origin);

  if (!m_octree->coordToKeyChecked(kinectSensorOrigin, m_updateBBXMin)
    || !m_octree->coordToKeyChecked(kinectSensorOrigin, m_updateBBXMax))
  {
    ROS_ERROR_STREAM("Could not generate Key for origin "<<kinectSensorOrigin);
  }

  bool updateKinectData = false;
  // determine if we need to update the kinect data
  if (currentKinectStamp - lastAddedKinectStamp > 0.05){
    updateKinectData = true;
    lastAddedKinectStamp = currentKinectStamp;

  }


#ifdef COLOR_OCTOMAP_SERVER
  unsigned char* colors = new unsigned char[3];
#endif


  // instead of direct scan insertion, compute update to filter ground:
  KeySet free_cells, occupied_cells;
  std::vector<bool> free_cells_is_proximity;
  std::vector<bool> occupied_cells_is_proximity;

  std::vector<double> free_cells_distances;
  std::vector<double> occupied_cells_distances;


  // insert ground points only as free:
  ros::WallTime startTime = ros::WallTime::now();
  int sensor_idx = -1;

  for (PCLPointCloud::const_iterator it = ground.begin(); it != ground.end(); ++it){
    sensor_idx++;
    point3d point(it->x, it->y, it->z);
    // maxrange check

    point3d sensorOrigin = pointTfToOctomap(sensorOrigins[sensor_idx]);


    if ((m_maxRange > 0.0) && ((point - sensorOrigin).norm() > m_maxRange) ) {
      point = sensorOrigin + (point - sensorOrigin).normalized() * m_maxRange;
    }

    // only clear space (ground points)
    if (m_octree->computeRayKeys(sensorOrigin, point, m_keyRay)){
      free_cells.insert(m_keyRay.begin(), m_keyRay.end());

      free_cells_is_proximity.push_back(true);
      free_cells_distances.push_back((point - sensorOrigin).norm());
    }


    octomap::OcTreeKey endKey;
    if (m_octree->coordToKeyChecked(point, endKey)){
      updateMinKey(endKey, m_updateBBXMin);
      updateMaxKey(endKey, m_updateBBXMax);
    } else{
      ROS_ERROR_STREAM("Could not generate Key for endpoint "<<point);
    }
  }

  double total_elapsed = (ros::WallTime::now() - startTime).toSec();
  // std::cout << total_elapsed << " sec for ground scan insertion" << std::endl;
  int i = -1;
  // std::cout << updateKinectData << std::endl;
  if (updateKinectData) {
    for (PCLPointCloud::const_iterator it = pc_ground_kinect.begin(); it != pc_ground_kinect.end(); ++it){
      i++;
      if (i % 40 != 0) {
        continue;
      }
      point3d point(it->x, it->y, it->z);
      // maxrange check
      if ((m_maxRange > 0.0) && ((point - kinectSensorOrigin).norm() > m_maxRange) ) {
        point = kinectSensorOrigin + (point - kinectSensorOrigin).normalized() * m_maxRange;
      }

      // only clear space (ground points)
      if (m_octree->computeRayKeys(kinectSensorOrigin, point, m_keyRay)){
        free_cells.insert(m_keyRay.begin(), m_keyRay.end());
        free_cells_is_proximity.push_back(false);
        free_cells_distances.push_back((point - kinectSensorOrigin).norm());
      }

      octomap::OcTreeKey endKey;
      if (m_octree->coordToKeyChecked(point, endKey)){
        updateMinKey(endKey, m_updateBBXMin);
        updateMaxKey(endKey, m_updateBBXMax);
      } else{
        ROS_ERROR_STREAM("Could not generate Key for endpoint "<<point);
      }
    }

  }


  startTime = ros::WallTime::now();
  sensor_idx = -1;
  // all other points: free on ray leading up to the endpoint, occupied on endpoint:
  for (PCLPointCloud::const_iterator it = nonground.begin(); it != nonground.end(); ++it){
    sensor_idx++;
    bool skip = false;
    point3d point(it->x, it->y, it->z);
    point3d sensorOrigin = pointTfToOctomap(sensorOrigins[sensor_idx]);
    // std::cout<< "is proximity: " << isProximity << std::endl;
    if (!m_octree->coordToKeyChecked(sensorOrigin, m_updateBBXMin)
      || !m_octree->coordToKeyChecked(sensorOrigin, m_updateBBXMax))
    {
      ROS_ERROR_STREAM("Could not generate Key for origin "<<sensorOrigin);
    }
    Eigen::Vector3d point_comp{it->x, it->y, it->z};
    // Remove sensed points on robot body
    // std::cout << "Point in space not ground: "  << point << std::endl;
    std::vector<float> sphere_radiuses{0.23+0.2, 0.24+0.2, 0.2+0.2, 0.237+0.2, 0.225+0.2, 0.20+0.2, 0.27+0.2, 0.3+0.2};
    int num_control_points = 8;
    std::unique_ptr<tf::StampedTransform[]> transform_control_points;
    std::unique_ptr<Eigen::Vector3d[]> translation_control_points;

    transform_control_points = std::make_unique<tf::StampedTransform[]>(num_control_points);
    translation_control_points = std::make_unique<Eigen::Vector3d[]>(num_control_points);

    for (int i = 0; i < num_control_points; i++) {
        m_tfListener.lookupTransform("/world", "/control_point" + std::to_string(i),
                                 ros::Time(0), transform_control_points[i]);
        translation_control_points[i] << transform_control_points[i].getOrigin().getX(),
                                         transform_control_points[i].getOrigin().getY(),
                                         transform_control_points[i].getOrigin().getZ();
        if ((point_comp - translation_control_points[i]).norm() < sphere_radiuses[i])
            skip = true;
    }

    if (skip) {
      continue;
    }

    // maxrange check
    if ((m_maxRange < 0.0) || ((point - sensorOrigin).norm() <= m_maxRange) ) {
      // std::cout << (point - sensorOrigin).norm() << " distance "<< std::endl;

      // free cells
      if (m_octree->computeRayKeys(sensorOrigin, point, m_keyRay)){
        free_cells.insert(m_keyRay.begin(), m_keyRay.end());
        free_cells_is_proximity.push_back(true);
        free_cells_distances.push_back((point - sensorOrigin).norm());
      }
      if (isInfVector[sensor_idx]) {
        continue;
      }
      // occupied endpoint
      OcTreeKey key;
      // convert a point to an octomap key
      if (m_octree->coordToKeyChecked(point, key)){
        occupied_cells.insert(key);
        occupied_cells_is_proximity.push_back(true);
        occupied_cells_distances.push_back((point - sensorOrigin).norm());

        updateMinKey(key, m_updateBBXMin);
        updateMaxKey(key, m_updateBBXMax);

#ifdef COLOR_OCTOMAP_SERVER // NB: Only read and interpret color if it's an occupied node
        m_octree->averageNodeColor(it->x, it->y, it->z, /*r=*/it->r, /*g=*/it->g, /*b=*/it->b);
#endif
      }
    } else {// ray longer than maxrange:;
      point3d new_end = sensorOrigin + (point - sensorOrigin).normalized() * m_maxRange;
      if (m_octree->computeRayKeys(sensorOrigin, new_end, m_keyRay)){
        free_cells.insert(m_keyRay.begin(), m_keyRay.end());
        free_cells_is_proximity.push_back(true);
        free_cells_distances.push_back((point - sensorOrigin).norm());

        octomap::OcTreeKey endKey;
        if (m_octree->coordToKeyChecked(new_end, endKey)){
          free_cells.insert(endKey);
          free_cells_is_proximity.push_back(true);
          free_cells_distances.push_back((point - sensorOrigin).norm());
          updateMinKey(endKey, m_updateBBXMin);
          updateMaxKey(endKey, m_updateBBXMax);
        } else{
          ROS_ERROR_STREAM("Could not generate Key for endpoint "<<new_end);
        }


      }
    }
  }

  total_elapsed = (ros::WallTime::now() - startTime).toSec();
  // std::cout << total_elapsed << " sec for non ground scan insertion" << std::endl;

  i = 0;
  int fails = 0;
  everyNthPointValue ++;
  everyNthPointValue = everyNthPointValue % 40;
  if (updateKinectData) {
      for (PCLPointCloud::const_iterator it = pc_nonground_kinect.begin(); it != pc_nonground_kinect.end(); ++it){

    bool skip = false;
    i++;
    if (i % 40 != everyNthPointValue) {
      continue;
    }
    point3d point(it->x, it->y, it->z);
    Eigen::Vector3d point_comp{it->x, it->y, it->z};
    // Remove sensed points on robot body
    // std::cout << "Point in space not ground: "  << point << std::endl;
    std::vector<float> sphere_radiuses{0.23+0.2, 0.24+0.2, 0.2+0.2, 0.237+0.35, 0.225+0.35 , 0.20+0.35, 0.27+0.35, 0.3+0.3};
    int num_control_points = 8;
    std::unique_ptr<tf::StampedTransform[]> transform_control_points;
    std::unique_ptr<Eigen::Vector3d[]> translation_control_points;

    transform_control_points = std::make_unique<tf::StampedTransform[]>(num_control_points);
    translation_control_points = std::make_unique<Eigen::Vector3d[]>(num_control_points);

    for (int i = 0; i < num_control_points; i++) {
        m_tfListener.lookupTransform("/world", "/control_point" + std::to_string(i),
                                 ros::Time(0), transform_control_points[i]);
        translation_control_points[i] << transform_control_points[i].getOrigin().getX(),
                                         transform_control_points[i].getOrigin().getY(),
                                         transform_control_points[i].getOrigin().getZ();
        if ((point_comp - translation_control_points[i]).norm() < sphere_radiuses[i])
            skip = true;
    }

    if (skip) {
      continue;
    }


    // maxrange check
    if ((m_maxRange < 0.0) || ((point - kinectSensorOrigin).norm() <= m_maxRange) ) {
      // free cells
      // std::cout << point << " " << kinectSensorOrigin << std::endl;
      // std::cout << (point - kinectSensorOrigin).norm() << " distance "<< std::endl;
      if (m_octree->computeRayKeys(kinectSensorOrigin, point, m_keyRay)){
        free_cells.insert(m_keyRay.begin(), m_keyRay.end());
        free_cells_is_proximity.push_back(false);
        free_cells_distances.push_back((point - kinectSensorOrigin).norm());
      }
      // occupied endpoint
      OcTreeKey key;
      // convert a point to an octomap key
      if (m_octree->coordToKeyChecked(point, key)){
        occupied_cells.insert(key);
        occupied_cells_is_proximity.push_back(false);
        occupied_cells_distances.push_back((point - kinectSensorOrigin).norm());
        // declare before hand
        updateMinKey(key, m_updateBBXMin);
        updateMaxKey(key, m_updateBBXMax);

#ifdef COLOR_OCTOMAP_SERVER // NB: Only read and interpret color if it's an occupied node
        m_octree->averageNodeColor(it->x, it->y, it->z, /*r=*/it->r, /*g=*/it->g, /*b=*/it->b);
#endif
      }
    } else {// ray longer than maxrange:;
      point3d new_end = kinectSensorOrigin + (point - kinectSensorOrigin).normalized() * m_maxRange;
      if (m_octree->computeRayKeys(kinectSensorOrigin, new_end, m_keyRay)){
        free_cells.insert(m_keyRay.begin(), m_keyRay.end());
        free_cells_is_proximity.push_back(false);
        free_cells_distances.push_back((point - kinectSensorOrigin).norm());

        octomap::OcTreeKey endKey;
        if (m_octree->coordToKeyChecked(new_end, endKey)){
          free_cells.insert(endKey);
          free_cells_is_proximity.push_back(false);
          free_cells_distances.push_back((point - kinectSensorOrigin).norm());
          updateMinKey(endKey, m_updateBBXMin);
          updateMaxKey(endKey, m_updateBBXMax);
        } else{
          ROS_ERROR_STREAM("Could not generate Key for endpoint "<<new_end);
        }


      }
    }
  }


  }


  // std::cout << free_cells.size() << " free cells" << std::endl;
  // mark free cells only if not seen occupied in this cloud
  for(KeySet::iterator it = free_cells.begin(), end=free_cells.end(); it!= end; ++it){
    // if (occupied_cells.find(*it) == occupied_cells.end()){
    //   m_octree->updateNode(*it, false);
    // }
      m_octree->updateNode(*it, false);

  }


  int num_free_cells = 0;
  int num_occupied_cells = 0;

  for (auto it = m_octree->begin(), end=m_octree->end(); it!= end; it++) {
    // decay the occupied nodes over time
    if (it->getOccupancy() > 0.5) {
      it->addValue(-0.001);
    }
    if (m_octree->isNodeOccupied(*it)) {
      num_occupied_cells++;
    } else {
      num_free_cells++;
    }
  }

  int occupied_idx = 0;
  for (KeySet::iterator it = occupied_cells.begin(), end=occupied_cells.end(); it!= end; it++) {
    // compute weights

  //   - log_odds(kinect_point) = 1.5d for d <= 0.5m
  //                            -0.2d + 0.85 for d > 0.5m
  // - log_odds(proximity_point) = -0.5d + 1
    float weight = 1;
    if (occupied_cells_is_proximity[occupied_idx]) {
      weight = (-0.07 * occupied_cells_distances[occupied_idx]) + 1;
      if (occupied_cells_distances[occupied_idx] < 0.04) {
        weight = 0;
      }
    } else {
      double distance = occupied_cells_distances[occupied_idx];
      std::cout << distance << std::endl;
      if (distance <= 0.5) {
        weight = 0;
        // weight = (1.5 * distance);
      } else {
        weight = (-0.1 * distance) + 1;
        // weight = 0.8;
      }
    }

    octomap::point3d point = m_octree->keyToCoord(*it);
    float z = point.z();
    if (z > 0.07) {
      OcTreeKey key = *it;
      // unsigned idx = key.getIndexKey();
      octomap::OcTreeKey::KeyHash hasher;
      size_t x = hasher(*it);
      

      // std::cout << "Add Idx: " << idx << std::endl;
      if(occupied_cells_is_proximity[occupied_idx]){
        m_key_to_is_proximity[x] = true;

      } else{
        // m_map_key_to_sensor[*it] = 1;
        m_key_to_is_proximity[x] = false;

      }
      m_octree->updateNode(*it, weight);
    }
    occupied_idx++;
    m_octree->updateNode(*it, false);

  }


  recentFreeCellCounts.push_back(num_free_cells);
  recentOccupiedCellCounts.push_back(num_occupied_cells);
  if (recentFreeCellCounts.size() > maxDequeSize) {
    recentFreeCellCounts.pop_front();
  }

  if (recentOccupiedCellCounts.size() > maxDequeSize) {
    recentOccupiedCellCounts.pop_front();
  }
  double mean = 0;
  for (auto it = recentFreeCellCounts.begin(); it != recentFreeCellCounts.end(); ++it) {
    mean += *it;
  }
  double freeCellsMean = mean / recentFreeCellCounts.size();

  mean = 0;
  for (auto it = recentOccupiedCellCounts.begin(); it != recentOccupiedCellCounts.end(); ++it) {
    mean += *it;
  }

  double occupiedCellsMean = mean / recentOccupiedCellCounts.size();



  std::cout << "occupied cells mean " << occupiedCellsMean << " free cells mean " << freeCellsMean << std::endl;
  // TODO: eval lazy+updateInner vs. proper insertion
  // non-lazy by default (updateInnerOccupancy() too slow for large maps)
  //m_octree->updateInnerOccupancy();
  octomap::point3d minPt, maxPt;
  ROS_DEBUG_STREAM("Bounding box keys (before): " << m_updateBBXMin[0] << " " <<m_updateBBXMin[1] << " " << m_updateBBXMin[2] << " / " <<m_updateBBXMax[0] << " "<<m_updateBBXMax[1] << " "<< m_updateBBXMax[2]);

  // TODO: snap max / min keys to larger voxels by m_maxTreeDepth
  //   if (m_maxTreeDepth < 16)
  //   {
  //      OcTreeKey tmpMin = getIndexKey(m_updateBBXMin, m_maxTreeDepth); // this should give us the first key at depth m_maxTreeDepth that is smaller or equal to m_updateBBXMin (i.e. lower left in 2D grid coordinates)
  //      OcTreeKey tmpMax = getIndexKey(m_updateBBXMax, m_maxTreeDepth); // see above, now add something to find upper right
  //      tmpMax[0]+= m_octree->getNodeSize( m_maxTreeDepth ) - 1;
  //      tmpMax[1]+= m_octree->getNodeSize( m_maxTreeDepth ) - 1;
  //      tmpMax[2]+= m_octree->getNodeSize( m_maxTreeDepth ) - 1;
  //      m_updateBBXMin = tmpMin;
  //      m_updateBBXMax = tmpMax;
  //   }

  // TODO: we could also limit the bbx to be within the map bounds here (see publishing check)
  minPt = m_octree->keyToCoord(m_updateBBXMin);
  maxPt = m_octree->keyToCoord(m_updateBBXMax);
  ROS_DEBUG_STREAM("Updated area bounding box: "<< minPt << " - "<<maxPt);
  ROS_DEBUG_STREAM("Bounding box keys (after): " << m_updateBBXMin[0] << " " <<m_updateBBXMin[1] << " " << m_updateBBXMin[2] << " / " <<m_updateBBXMax[0] << " "<<m_updateBBXMax[1] << " "<< m_updateBBXMax[2]);

  if (m_compressMap)
    m_octree->prune();

  #ifdef COLOR_OCTOMAP_SERVER
    if (colors)
    {
      delete[] colors;
      colors = NULL;
    }
  #endif
}

void OctomapServer::insertScan(const tf::Point& sensorOriginTf, const PCLPointCloud& ground, const PCLPointCloud& nonground, bool isProximity, bool isInf){
  point3d sensorOrigin = pointTfToOctomap(sensorOriginTf);
  point3d kinectSensorOrigin = pointTfToOctomap(kinect_sensor_origin);
  // std::cout<< "is proximity: " << isProximity << std::endl;
  if (!m_octree->coordToKeyChecked(sensorOrigin, m_updateBBXMin)
    || !m_octree->coordToKeyChecked(sensorOrigin, m_updateBBXMax))
  {
    ROS_ERROR_STREAM("Could not generate Key for origin "<<sensorOrigin);
  }

  bool updateKinectData = false;

  if (currentKinectStamp - lastAddedKinectStamp > 2.0){
    updateKinectData = true;
    lastAddedKinectStamp = currentKinectStamp;

  }

#ifdef COLOR_OCTOMAP_SERVER
  unsigned char* colors = new unsigned char[3];
#endif

  // combine point clouds here

  // instead of direct scan insertion, compute update to filter ground:
  KeySet free_cells, occupied_cells;
  // insert ground points only as free:
  ros::WallTime startTime = ros::WallTime::now();

  for (PCLPointCloud::const_iterator it = ground.begin(); it != ground.end(); ++it){
    point3d point(it->x, it->y, it->z);
    // maxrange check
    if ((m_maxRange > 0.0) && ((point - sensorOrigin).norm() > m_maxRange) ) {
      point = sensorOrigin + (point - sensorOrigin).normalized() * m_maxRange;
    }

    // only clear space (ground points)
    if (m_octree->computeRayKeys(sensorOrigin, point, m_keyRay)){
      free_cells.insert(m_keyRay.begin(), m_keyRay.end());
    }


    octomap::OcTreeKey endKey;
    if (m_octree->coordToKeyChecked(point, endKey)){
      updateMinKey(endKey, m_updateBBXMin);
      updateMaxKey(endKey, m_updateBBXMax);
    } else{
      ROS_ERROR_STREAM("Could not generate Key for endpoint "<<point);
    }
  }

  double total_elapsed = (ros::WallTime::now() - startTime).toSec();
  // std::cout << total_elapsed << " sec for ground scan insertion" << std::endl;
  int i = -1;
  // std::cout << updateKinectData << std::endl;
  if (updateKinectData) {
    for (PCLPointCloud::const_iterator it = pc_ground_kinect.begin(); it != pc_ground_kinect.end(); ++it){
      i++;
      if (i % 40 != 0) {
        continue;
      }
      point3d point(it->x, it->y, it->z);
      // maxrange check
      if ((m_maxRange > 0.0) && ((point - kinectSensorOrigin).norm() > m_maxRange) ) {
        point = kinectSensorOrigin + (point - kinectSensorOrigin).normalized() * m_maxRange;
      }

      // only clear space (ground points)
      if (m_octree->computeRayKeys(kinectSensorOrigin, point, m_keyRay)){
        free_cells.insert(m_keyRay.begin(), m_keyRay.end());
      }

      octomap::OcTreeKey endKey;
      if (m_octree->coordToKeyChecked(point, endKey)){
        updateMinKey(endKey, m_updateBBXMin);
        updateMaxKey(endKey, m_updateBBXMax);
      } else{
        ROS_ERROR_STREAM("Could not generate Key for endpoint "<<point);
      }
    }

  }

  startTime = ros::WallTime::now();

  // all other points: free on ray leading up to the endpoint, occupied on endpoint:
  for (PCLPointCloud::const_iterator it = nonground.begin(); it != nonground.end(); ++it){
    bool skip = false;
    point3d point(it->x, it->y, it->z);
    Eigen::Vector3d point_comp{it->x, it->y, it->z};
    // Remove sensed points on robot body
    // std::cout << "Point in space not ground: "  << point << std::endl;
    std::vector<float> sphere_radiuses{0.23, 0.24, 0.2, 0.237, 0.225, 0.20, 0.27, 0.3};
    int num_control_points = 8;
    std::unique_ptr<tf::StampedTransform[]> transform_control_points;
    std::unique_ptr<Eigen::Vector3d[]> translation_control_points;

    transform_control_points = std::make_unique<tf::StampedTransform[]>(num_control_points);
    translation_control_points = std::make_unique<Eigen::Vector3d[]>(num_control_points);

    for (int i = 0; i < num_control_points; i++) {
        m_tfListener.lookupTransform("/world", "/control_point" + std::to_string(i),
                                 ros::Time(0), transform_control_points[i]);
        translation_control_points[i] << transform_control_points[i].getOrigin().getX(),
                                         transform_control_points[i].getOrigin().getY(),
                                         transform_control_points[i].getOrigin().getZ();
        if ((point_comp - translation_control_points[i]).norm() < sphere_radiuses[i])
            skip = true;
    }

    if (skip) {
      continue;
    }

    // maxrange check
    if ((m_maxRange < 0.0) || ((point - sensorOrigin).norm() <= m_maxRange) ) {
      // std::cout << (point - sensorOrigin).norm() << " distance "<< std::endl;

      // free cells
      if (m_octree->computeRayKeys(sensorOrigin, point, m_keyRay)){
        free_cells.insert(m_keyRay.begin(), m_keyRay.end());
      }
      if (isInf) {
        continue;
      }
      // occupied endpoint
      OcTreeKey key;
      // convert a point to an octomap key
      if (m_octree->coordToKeyChecked(point, key)){
        occupied_cells.insert(key);

        updateMinKey(key, m_updateBBXMin);
        updateMaxKey(key, m_updateBBXMax);

#ifdef COLOR_OCTOMAP_SERVER // NB: Only read and interpret color if it's an occupied node
        m_octree->averageNodeColor(it->x, it->y, it->z, /*r=*/it->r, /*g=*/it->g, /*b=*/it->b);
#endif
      }
    } else {// ray longer than maxrange:;
      point3d new_end = sensorOrigin + (point - sensorOrigin).normalized() * m_maxRange;
      if (m_octree->computeRayKeys(sensorOrigin, new_end, m_keyRay)){
        free_cells.insert(m_keyRay.begin(), m_keyRay.end());

        octomap::OcTreeKey endKey;
        if (m_octree->coordToKeyChecked(new_end, endKey)){
          free_cells.insert(endKey);
          updateMinKey(endKey, m_updateBBXMin);
          updateMaxKey(endKey, m_updateBBXMax);
        } else{
          ROS_ERROR_STREAM("Could not generate Key for endpoint "<<new_end);
        }


      }
    }
  }

  total_elapsed = (ros::WallTime::now() - startTime).toSec();
  // std::cout << total_elapsed << " sec for non ground scan insertion" << std::endl;

  i = 0;
  int fails = 0;
  everyNthPointValue ++;
  everyNthPointValue = everyNthPointValue % 40;
  if (updateKinectData) {
      for (PCLPointCloud::const_iterator it = pc_nonground_kinect.begin(); it != pc_nonground_kinect.end(); ++it){

    bool skip = false;
    i++;
    if (i % 40 != everyNthPointValue) {
      continue;
    }
    point3d point(it->x, it->y, it->z);
    Eigen::Vector3d point_comp{it->x, it->y, it->z};
    // Remove sensed points on robot body
    // std::cout << "Point in space not ground: "  << point << std::endl;
    std::vector<float> sphere_radiuses{0.23, 0.24, 0.2, 0.237+0.25, 0.225+0.25 , 0.20+0.25, 0.27+0.25, 0.3+0.4};
    int num_control_points = 8;
    std::unique_ptr<tf::StampedTransform[]> transform_control_points;
    std::unique_ptr<Eigen::Vector3d[]> translation_control_points;

    transform_control_points = std::make_unique<tf::StampedTransform[]>(num_control_points);
    translation_control_points = std::make_unique<Eigen::Vector3d[]>(num_control_points);

    for (int i = 0; i < num_control_points; i++) {
        m_tfListener.lookupTransform("/world", "/control_point" + std::to_string(i),
                                 ros::Time(0), transform_control_points[i]);
        translation_control_points[i] << transform_control_points[i].getOrigin().getX(),
                                         transform_control_points[i].getOrigin().getY(),
                                         transform_control_points[i].getOrigin().getZ();
        if ((point_comp - translation_control_points[i]).norm() < sphere_radiuses[i])
            skip = true;
    }

    if (skip) {
      continue;
    }


    // maxrange check
    if ((m_maxRange < 0.0) || ((point - kinectSensorOrigin).norm() <= m_maxRange) ) {
      // free cells
      // std::cout << point << " " << kinectSensorOrigin << std::endl;
      // std::cout << (point - kinectSensorOrigin).norm() << " distance "<< std::endl;
      if (m_octree->computeRayKeys(kinectSensorOrigin, point, m_keyRay)){
        free_cells.insert(m_keyRay.begin(), m_keyRay.end());
      }
      // occupied endpoint
      OcTreeKey key;
      // convert a point to an octomap key
      if (m_octree->coordToKeyChecked(point, key)){
        occupied_cells.insert(key);
        // declare before hand
        updateMinKey(key, m_updateBBXMin);
        updateMaxKey(key, m_updateBBXMax);

#ifdef COLOR_OCTOMAP_SERVER // NB: Only read and interpret color if it's an occupied node
        m_octree->averageNodeColor(it->x, it->y, it->z, /*r=*/it->r, /*g=*/it->g, /*b=*/it->b);
#endif
      }
    } else {// ray longer than maxrange:;
      point3d new_end = kinectSensorOrigin + (point - kinectSensorOrigin).normalized() * m_maxRange;
      if (m_octree->computeRayKeys(kinectSensorOrigin, new_end, m_keyRay)){
        free_cells.insert(m_keyRay.begin(), m_keyRay.end());

        octomap::OcTreeKey endKey;
        if (m_octree->coordToKeyChecked(new_end, endKey)){
          free_cells.insert(endKey);
          updateMinKey(endKey, m_updateBBXMin);
          updateMaxKey(endKey, m_updateBBXMax);
        } else{
          ROS_ERROR_STREAM("Could not generate Key for endpoint "<<new_end);
        }


      }
    }
  }


  }
  // std::cout << free_cells.size() << " free cells" << std::endl;
  // mark free cells only if not seen occupied in this cloud
  for(KeySet::iterator it = free_cells.begin(), end=free_cells.end(); it!= end; ++it){
    // if (occupied_cells.find(*it) == occupied_cells.end()){
    //   m_octree->updateNode(*it, false);
    // }
      m_octree->updateNode(*it, false);

  }


  for (auto it = m_octree->begin(), end=m_octree->end(); it!= end; it++) {
    // decay the occupied nodes over time
    if (it->getOccupancy() > 0.5) {
      it->addValue(-0.002);
    }
  }


  for (KeySet::iterator it = occupied_cells.begin(), end=occupied_cells.end(); it!= end; it++) {
    m_octree->updateNode(*it, true);
    // octomap::OcTreeKey::KeyHash hasher;
    // size_t x = hasher(*it);
    // // size_t v
    // // Eigen::Vector3d point_comp{point.x, point.y, point.z};
    // keyToOccupied[x] = std::make_tuple(*it, true, ros::WallTime::now().toSec());

  }

  // for(std::map<size_t, std::tuple<octomap::OcTreeKey, bool, double>>::iterator iter = keyToOccupied.begin(); iter != keyToOccupied.end(); ++iter)
  // {
  //   double diff = ros::WallTime::now().toSec() - std::get<2>(iter->second);
  //   if (diff > 5.0 && std::get<1>(iter->second) == true) {
  //     keyToOccupied[iter->first] = std::make_tuple(std::get<0>(iter->second), false, ros::WallTime::now().toSec());
  //     m_octree->updateNode(std::get<0>(iter->second), false);

  //   }
  //   //ignore value
  //   //Value v = iter->second;
  // }


  // TODO: eval lazy+updateInner vs. proper insertion
  // non-lazy by default (updateInnerOccupancy() too slow for large maps)
  //m_octree->updateInnerOccupancy();
  octomap::point3d minPt, maxPt;
  ROS_DEBUG_STREAM("Bounding box keys (before): " << m_updateBBXMin[0] << " " <<m_updateBBXMin[1] << " " << m_updateBBXMin[2] << " / " <<m_updateBBXMax[0] << " "<<m_updateBBXMax[1] << " "<< m_updateBBXMax[2]);

  // TODO: snap max / min keys to larger voxels by m_maxTreeDepth
//   if (m_maxTreeDepth < 16)
//   {
//      OcTreeKey tmpMin = getIndexKey(m_updateBBXMin, m_maxTreeDepth); // this should give us the first key at depth m_maxTreeDepth that is smaller or equal to m_updateBBXMin (i.e. lower left in 2D grid coordinates)
//      OcTreeKey tmpMax = getIndexKey(m_updateBBXMax, m_maxTreeDepth); // see above, now add something to find upper right
//      tmpMax[0]+= m_octree->getNodeSize( m_maxTreeDepth ) - 1;
//      tmpMax[1]+= m_octree->getNodeSize( m_maxTreeDepth ) - 1;
//      tmpMax[2]+= m_octree->getNodeSize( m_maxTreeDepth ) - 1;
//      m_updateBBXMin = tmpMin;
//      m_updateBBXMax = tmpMax;
//   }

  // TODO: we could also limit the bbx to be within the map bounds here (see publishing check)
  minPt = m_octree->keyToCoord(m_updateBBXMin);
  maxPt = m_octree->keyToCoord(m_updateBBXMax);
  ROS_DEBUG_STREAM("Updated area bounding box: "<< minPt << " - "<<maxPt);
  ROS_DEBUG_STREAM("Bounding box keys (after): " << m_updateBBXMin[0] << " " <<m_updateBBXMin[1] << " " << m_updateBBXMin[2] << " / " <<m_updateBBXMax[0] << " "<<m_updateBBXMax[1] << " "<< m_updateBBXMax[2]);

  if (m_compressMap)
    m_octree->prune();

#ifdef COLOR_OCTOMAP_SERVER
  if (colors)
  {
    delete[] colors;
    colors = NULL;
  }
#endif
}



void OctomapServer::publishAll(const ros::Time& rostime){
  ros::WallTime startTime = ros::WallTime::now();
  size_t octomapSize = m_octree->size();
  // TODO: estimate num occ. voxels for size of arrays (reserve)
  if (octomapSize <= 1){
    ROS_WARN("Nothing to publish, octree is empty");
    return;
  }

  bool publishFreeMarkerArray = m_publishFreeSpace && (m_latchedTopics || m_fmarkerPub.getNumSubscribers() > 0);
  bool publishMarkerArray = (m_latchedTopics || m_markerPub.getNumSubscribers() > 0);
  bool publishPointCloud = (m_latchedTopics || m_pointCloudPub.getNumSubscribers() > 0);
  bool publishBinaryMap = (m_latchedTopics || m_binaryMapPub.getNumSubscribers() > 0);
  bool publishFullMap = (m_latchedTopics || m_fullMapPub.getNumSubscribers() > 0);
  m_publish2DMap = (m_latchedTopics || m_mapPub.getNumSubscribers() > 0);

  // init markers for free space:
  visualization_msgs::MarkerArray freeNodesVis;
  // each array stores all cubes of a different size, one for each depth level:
  freeNodesVis.markers.resize(m_treeDepth+1);

  geometry_msgs::Pose pose;
  pose.orientation = tf::createQuaternionMsgFromYaw(0.0);

  // init markers:
  visualization_msgs::MarkerArray occupiedNodesVis;
  // each array stores all cubes of a different size, one for each depth level:
  occupiedNodesVis.markers.resize(m_treeDepth+1);

  // init pointcloud:
  pcl::PointCloud<PCLPoint> pclCloud;

  // call pre-traversal hook:
  handlePreNodeTraversal(rostime);

  // now, traverse all leafs in the tree:
  int current_tree_node_idx = 0;
  for (OcTreeT::iterator it = m_octree->begin(m_maxTreeDepth),
      end = m_octree->end(); it != end; ++it)
  {
    bool inUpdateBBX = isInUpdateBBX(it);

    // call general hook:
    handleNode(it);
    if (inUpdateBBX)
      handleNodeInBBX(it);

    if (m_octree->isNodeOccupied(*it)){
      double z = it.getZ();
      double half_size = it.getSize() / 2.0;
      if (z + half_size > m_occupancyMinZ && z - half_size < m_occupancyMaxZ)
      {
        double size = it.getSize();
        double x = it.getX();
        double y = it.getY();
#ifdef COLOR_OCTOMAP_SERVER
        int r = it->getColor().r;
        int g = it->getColor().g;
        int b = it->getColor().b;
#endif

        // Ignore speckles in the map:
        if (m_filterSpeckles && (it.getDepth() == m_treeDepth +1) && isSpeckleNode(it.getKey())){
          ROS_DEBUG("Ignoring single speckle at (%f,%f,%f)", x, y, z);
          continue;
        } // else: current octree node is no speckle, send it out

        handleOccupiedNode(it);
        if (inUpdateBBX)
          handleOccupiedNodeInBBX(it);


        //create marker:
        if (publishMarkerArray){
          unsigned idx = it.getDepth();
          assert(idx < occupiedNodesVis.markers.size());

          geometry_msgs::Point cubeCenter;
          cubeCenter.x = x;
          cubeCenter.y = y;
          cubeCenter.z = z;

          occupiedNodesVis.markers[idx].points.push_back(cubeCenter);
          double h = 0;
          if (m_useHeightMap){
            double minX, minY, minZ, maxX, maxY, maxZ;
            m_octree->getMetricMin(minX, minY, minZ);
            m_octree->getMetricMax(maxX, maxY, maxZ);
            // std::cout << "IDX:::" << mapIdx(it.getIndexKey()) << std::endl;
            // if (m_key_to_is_proximity.find(mapIdx(it.getIndexKey()))->second){
            //   std::cout << " is what: " << m_key_to_is_proximity.find(mapIdx(it.getIndexKey()))->second << std::endl;
            // }

            // unsigned idx = mapIdx(it.getIndexKey());
            // std::pair<std::map<unsigned,bool>::iterator,bool> ret;
            // bool is_proximity = m_key_to_is_proximity.find(idx)->second;
            // std::cout << "IS Proximity: " << is_proximity << std::endl;
            auto key = it.getKey();

            octomap::OcTreeKey::KeyHash hasher;
            size_t x = hasher(key);
            if(m_key_to_is_proximity[x]){
              h = (1.0 - std::min(std::max((minZ-minZ)/ (maxZ - minZ), 0.0), 1.0)) *m_colorFactor;
            }else{
              h = (1.0 - std::min(std::max((maxZ-minZ)/ (maxZ - minZ), 0.0), 1.0)) *m_colorFactor;;
            }
            //(1.0 - std::min(std::max((cubeCenter.z-minZ)/ (maxZ - minZ), 0.0), 1.0)) *m_colorFactor;
            occupiedNodesVis.markers[idx].colors.push_back(heightMapColor(h));
          }

#ifdef COLOR_OCTOMAP_SERVER
          if (m_useColoredMap) {
            std_msgs::ColorRGBA _color; _color.r = (r / 255.); _color.g = (g / 255.); _color.b = (b / 255.); _color.a = 1.0; // TODO/EVALUATE: potentially use occupancy as measure for alpha channel?
            occupiedNodesVis.markers[idx].colors.push_back(_color);
          }
#endif
        }

        // insert into pointcloud:
        if (publishPointCloud) {
#ifdef COLOR_OCTOMAP_SERVER
          PCLPoint _point = PCLPoint();
          _point.x = x; _point.y = y; _point.z = z;
          _point.r = r; _point.g = g; _point.b = b;
          pclCloud.push_back(_point);
#else
          pclCloud.push_back(PCLPoint(x, y, z));
#endif
        }

      }
    } else{ // node not occupied => mark as free in 2D map if unknown so far
      double z = it.getZ();
      double half_size = it.getSize() / 2.0;
      if (z + half_size > m_occupancyMinZ && z - half_size < m_occupancyMaxZ)
      {
        handleFreeNode(it);
        if (inUpdateBBX)
          handleFreeNodeInBBX(it);

        if (m_publishFreeSpace){
          double x = it.getX();
          double y = it.getY();

          //create marker for free space:
          if (publishFreeMarkerArray){
            unsigned idx = it.getDepth();
            assert(idx < freeNodesVis.markers.size());

            geometry_msgs::Point cubeCenter;
            cubeCenter.x = x;
            cubeCenter.y = y;
            cubeCenter.z = z;

            freeNodesVis.markers[idx].points.push_back(cubeCenter);
          }
        }

      }
    }
  }

  // call post-traversal hook:
  handlePostNodeTraversal(rostime);

  // finish MarkerArray:
  if (publishMarkerArray){
    for (unsigned i= 0; i < occupiedNodesVis.markers.size(); ++i){
      double size = m_octree->getNodeSize(i);

      occupiedNodesVis.markers[i].header.frame_id = m_worldFrameId;
      occupiedNodesVis.markers[i].header.stamp = rostime;
      occupiedNodesVis.markers[i].ns = "map";
      occupiedNodesVis.markers[i].id = i;
      occupiedNodesVis.markers[i].type = visualization_msgs::Marker::CUBE_LIST;
      occupiedNodesVis.markers[i].scale.x = size;
      occupiedNodesVis.markers[i].scale.y = size;
      occupiedNodesVis.markers[i].scale.z = size;
      if (!m_useColoredMap)
        occupiedNodesVis.markers[i].color = m_color;


      if (occupiedNodesVis.markers[i].points.size() > 0)
        occupiedNodesVis.markers[i].action = visualization_msgs::Marker::ADD;
      else
        occupiedNodesVis.markers[i].action = visualization_msgs::Marker::DELETE;
    }

    m_markerPub.publish(occupiedNodesVis);
  }


  // finish FreeMarkerArray:
  if (publishFreeMarkerArray){
    for (unsigned i= 0; i < freeNodesVis.markers.size(); ++i){
      double size = m_octree->getNodeSize(i);

      freeNodesVis.markers[i].header.frame_id = m_worldFrameId;
      freeNodesVis.markers[i].header.stamp = rostime;
      freeNodesVis.markers[i].ns = "map";
      freeNodesVis.markers[i].id = i;
      freeNodesVis.markers[i].type = visualization_msgs::Marker::CUBE_LIST;
      freeNodesVis.markers[i].scale.x = size;
      freeNodesVis.markers[i].scale.y = size;
      freeNodesVis.markers[i].scale.z = size;
      freeNodesVis.markers[i].color = m_colorFree;


      if (freeNodesVis.markers[i].points.size() > 0)
        freeNodesVis.markers[i].action = visualization_msgs::Marker::ADD;
      else
        freeNodesVis.markers[i].action = visualization_msgs::Marker::DELETE;
    }

    m_fmarkerPub.publish(freeNodesVis);
  }


  // finish pointcloud:
  if (publishPointCloud){
    sensor_msgs::PointCloud2 cloud;
    pcl::toROSMsg (pclCloud, cloud);
    cloud.header.frame_id = m_worldFrameId;
    cloud.header.stamp = rostime;
    m_pointCloudPub.publish(cloud);
  }

  if (publishBinaryMap)
    publishBinaryOctoMap(rostime);

  if (publishFullMap)
    publishFullOctoMap(rostime);


  double total_elapsed = (ros::WallTime::now() - startTime).toSec();
  ROS_DEBUG("Map publishing in OctomapServer took %f sec", total_elapsed);

}


bool OctomapServer::octomapBinarySrv(OctomapSrv::Request  &req,
                                    OctomapSrv::Response &res)
{
  ros::WallTime startTime = ros::WallTime::now();
  ROS_INFO("Sending binary map data on service request");
  res.map.header.frame_id = m_worldFrameId;
  res.map.header.stamp = ros::Time::now();
  if (!octomap_msgs::binaryMapToMsg(*m_octree, res.map))
    return false;

  double total_elapsed = (ros::WallTime::now() - startTime).toSec();
  ROS_INFO("Binary octomap sent in %f sec", total_elapsed);
  return true;
}

bool OctomapServer::octomapFullSrv(OctomapSrv::Request  &req,
                                    OctomapSrv::Response &res)
{
  ROS_INFO("Sending full map data on service request");
  res.map.header.frame_id = m_worldFrameId;
  res.map.header.stamp = ros::Time::now();


  if (!octomap_msgs::fullMapToMsg(*m_octree, res.map))
    return false;

  return true;
}

bool OctomapServer::clearBBXSrv(BBXSrv::Request& req, BBXSrv::Response& resp){
  point3d min = pointMsgToOctomap(req.min);
  point3d max = pointMsgToOctomap(req.max);

  double thresMin = m_octree->getClampingThresMin();
  for(OcTreeT::leaf_bbx_iterator it = m_octree->begin_leafs_bbx(min,max),
      end=m_octree->end_leafs_bbx(); it!= end; ++it){

    it->setLogOdds(octomap::logodds(thresMin));
    //			m_octree->updateNode(it.getKey(), -6.0f);
  }
  // TODO: eval which is faster (setLogOdds+updateInner or updateNode)
  m_octree->updateInnerOccupancy();

  publishAll(ros::Time::now());

  return true;
}

bool OctomapServer::resetSrv(std_srvs::Empty::Request& req, std_srvs::Empty::Response& resp) {
  visualization_msgs::MarkerArray occupiedNodesVis;
  occupiedNodesVis.markers.resize(m_treeDepth +1);
  ros::Time rostime = ros::Time::now();
  m_octree->clear();
  // clear 2D map:
  m_gridmap.data.clear();
  m_gridmap.info.height = 0.0;
  m_gridmap.info.width = 0.0;
  m_gridmap.info.resolution = 0.0;
  m_gridmap.info.origin.position.x = 0.0;
  m_gridmap.info.origin.position.y = 0.0;

  ROS_INFO("Cleared octomap");
  publishAll(rostime);

  publishBinaryOctoMap(rostime);
  for (unsigned i= 0; i < occupiedNodesVis.markers.size(); ++i){

    occupiedNodesVis.markers[i].header.frame_id = m_worldFrameId;
    occupiedNodesVis.markers[i].header.stamp = rostime;
    occupiedNodesVis.markers[i].ns = "map";
    occupiedNodesVis.markers[i].id = i;
    occupiedNodesVis.markers[i].type = visualization_msgs::Marker::CUBE_LIST;
    occupiedNodesVis.markers[i].action = visualization_msgs::Marker::DELETE;
  }

  m_markerPub.publish(occupiedNodesVis);

  visualization_msgs::MarkerArray freeNodesVis;
  freeNodesVis.markers.resize(m_treeDepth +1);

  for (unsigned i= 0; i < freeNodesVis.markers.size(); ++i){

    freeNodesVis.markers[i].header.frame_id = m_worldFrameId;
    freeNodesVis.markers[i].header.stamp = rostime;
    freeNodesVis.markers[i].ns = "map";
    freeNodesVis.markers[i].id = i;
    freeNodesVis.markers[i].type = visualization_msgs::Marker::CUBE_LIST;
    freeNodesVis.markers[i].action = visualization_msgs::Marker::DELETE;
  }
  m_fmarkerPub.publish(freeNodesVis);

  return true;
}

void OctomapServer::publishBinaryOctoMap(const ros::Time& rostime) const{

  Octomap map;
  map.header.frame_id = m_worldFrameId;
  map.header.stamp = rostime;

  if (octomap_msgs::binaryMapToMsg(*m_octree, map))
    m_binaryMapPub.publish(map);
  else
    ROS_ERROR("Error serializing OctoMap");
}

void OctomapServer::publishFullOctoMap(const ros::Time& rostime) const{

  Octomap map;
  map.header.frame_id = m_worldFrameId;
  map.header.stamp = rostime;

  if (octomap_msgs::fullMapToMsg(*m_octree, map))
    m_fullMapPub.publish(map);
  else
    ROS_ERROR("Error serializing OctoMap");

}


void OctomapServer::filterGroundPlane(const PCLPointCloud& pc, PCLPointCloud& ground, PCLPointCloud& nonground) const{
  ground.header = pc.header;
  nonground.header = pc.header;

  if (pc.size() < 50){
    ROS_WARN("Pointcloud in OctomapServer too small, skipping ground plane extraction");
    nonground = pc;
  } else {
    // plane detection for ground plane removal:
    pcl::ModelCoefficients::Ptr coefficients (new pcl::ModelCoefficients);
    pcl::PointIndices::Ptr inliers (new pcl::PointIndices);

    // Create the segmentation object and set up:
    pcl::SACSegmentation<PCLPoint> seg;
    seg.setOptimizeCoefficients (true);
    // TODO: maybe a filtering based on the surface normals might be more robust / accurate?
    seg.setModelType(pcl::SACMODEL_PERPENDICULAR_PLANE);
    seg.setMethodType(pcl::SAC_RANSAC);
    seg.setMaxIterations(200);
    seg.setDistanceThreshold (m_groundFilterDistance);
    seg.setAxis(Eigen::Vector3f(0,0,1));
    seg.setEpsAngle(m_groundFilterAngle);


    PCLPointCloud cloud_filtered(pc);
    // Create the filtering object
    pcl::ExtractIndices<PCLPoint> extract;
    bool groundPlaneFound = false;

    while(cloud_filtered.size() > 10 && !groundPlaneFound){
      seg.setInputCloud(cloud_filtered.makeShared());
      seg.segment (*inliers, *coefficients);
      if (inliers->indices.size () == 0){
        ROS_INFO("PCL segmentation did not find any plane.");

        break;
      }

      extract.setInputCloud(cloud_filtered.makeShared());
      extract.setIndices(inliers);

      if (std::abs(coefficients->values.at(3)) < m_groundFilterPlaneDistance){
        ROS_DEBUG("Ground plane found: %zu/%zu inliers. Coeff: %f %f %f %f", inliers->indices.size(), cloud_filtered.size(),
                  coefficients->values.at(0), coefficients->values.at(1), coefficients->values.at(2), coefficients->values.at(3));
        extract.setNegative (false);
        extract.filter (ground);

        // remove ground points from full pointcloud:
        // workaround for PCL bug:
        if(inliers->indices.size() != cloud_filtered.size()){
          extract.setNegative(true);
          PCLPointCloud cloud_out;
          extract.filter(cloud_out);
          nonground += cloud_out;
          cloud_filtered = cloud_out;
        }

        groundPlaneFound = true;
      } else{
        ROS_DEBUG("Horizontal plane (not ground) found: %zu/%zu inliers. Coeff: %f %f %f %f", inliers->indices.size(), cloud_filtered.size(),
                  coefficients->values.at(0), coefficients->values.at(1), coefficients->values.at(2), coefficients->values.at(3));
        pcl::PointCloud<PCLPoint> cloud_out;
        extract.setNegative (false);
        extract.filter(cloud_out);
        nonground +=cloud_out;
        // debug
        //            pcl::PCDWriter writer;
        //            writer.write<PCLPoint>("nonground_plane.pcd",cloud_out, false);

        // remove current plane from scan for next iteration:
        // workaround for PCL bug:
        if(inliers->indices.size() != cloud_filtered.size()){
          extract.setNegative(true);
          cloud_out.points.clear();
          extract.filter(cloud_out);
          cloud_filtered = cloud_out;
        } else{
          cloud_filtered.points.clear();
        }
      }

    }
    // TODO: also do this if overall starting pointcloud too small?
    if (!groundPlaneFound){ // no plane found or remaining points too small
      ROS_WARN("No ground plane found in scan");

      // do a rough fitlering on height to prevent spurious obstacles
      pcl::PassThrough<PCLPoint> second_pass;
      second_pass.setFilterFieldName("z");
      second_pass.setFilterLimits(-m_groundFilterPlaneDistance, m_groundFilterPlaneDistance);
      second_pass.setInputCloud(pc.makeShared());
      second_pass.filter(ground);

      second_pass.setFilterLimitsNegative (true);
      second_pass.filter(nonground);
    }

    // debug:
    //        pcl::PCDWriter writer;
    //        if (pc_ground.size() > 0)
    //          writer.write<PCLPoint>("ground.pcd",pc_ground, false);
    //        if (pc_nonground.size() > 0)
    //          writer.write<PCLPoint>("nonground.pcd",pc_nonground, false);

  }


}

void OctomapServer::handlePreNodeTraversal(const ros::Time& rostime){
  if (m_publish2DMap){
    // init projected 2D map:
    m_gridmap.header.frame_id = m_worldFrameId;
    m_gridmap.header.stamp = rostime;
    nav_msgs::MapMetaData oldMapInfo = m_gridmap.info;

    // TODO: move most of this stuff into c'tor and init map only once (adjust if size changes)
    double minX, minY, minZ, maxX, maxY, maxZ;
    m_octree->getMetricMin(minX, minY, minZ);
    m_octree->getMetricMax(maxX, maxY, maxZ);

    octomap::point3d minPt(minX, minY, minZ);
    octomap::point3d maxPt(maxX, maxY, maxZ);
    octomap::OcTreeKey minKey = m_octree->coordToKey(minPt, m_maxTreeDepth);
    octomap::OcTreeKey maxKey = m_octree->coordToKey(maxPt, m_maxTreeDepth);

    ROS_DEBUG("MinKey: %d %d %d / MaxKey: %d %d %d", minKey[0], minKey[1], minKey[2], maxKey[0], maxKey[1], maxKey[2]);

    // add padding if requested (= new min/maxPts in x&y):
    double halfPaddedX = 0.5*m_minSizeX;
    double halfPaddedY = 0.5*m_minSizeY;
    minX = std::min(minX, -halfPaddedX);
    maxX = std::max(maxX, halfPaddedX);
    minY = std::min(minY, -halfPaddedY);
    maxY = std::max(maxY, halfPaddedY);
    minPt = octomap::point3d(minX, minY, minZ);
    maxPt = octomap::point3d(maxX, maxY, maxZ);

    OcTreeKey paddedMaxKey;
    if (!m_octree->coordToKeyChecked(minPt, m_maxTreeDepth, m_paddedMinKey)){
      ROS_ERROR("Could not create padded min OcTree key at %f %f %f", minPt.x(), minPt.y(), minPt.z());
      return;
    }
    if (!m_octree->coordToKeyChecked(maxPt, m_maxTreeDepth, paddedMaxKey)){
      ROS_ERROR("Could not create padded max OcTree key at %f %f %f", maxPt.x(), maxPt.y(), maxPt.z());
      return;
    }

    ROS_DEBUG("Padded MinKey: %d %d %d / padded MaxKey: %d %d %d", m_paddedMinKey[0], m_paddedMinKey[1], m_paddedMinKey[2], paddedMaxKey[0], paddedMaxKey[1], paddedMaxKey[2]);
    assert(paddedMaxKey[0] >= maxKey[0] && paddedMaxKey[1] >= maxKey[1]);

    m_multires2DScale = 1 << (m_treeDepth - m_maxTreeDepth);
    m_gridmap.info.width = (paddedMaxKey[0] - m_paddedMinKey[0])/m_multires2DScale +1;
    m_gridmap.info.height = (paddedMaxKey[1] - m_paddedMinKey[1])/m_multires2DScale +1;

    int mapOriginX = minKey[0] - m_paddedMinKey[0];
    int mapOriginY = minKey[1] - m_paddedMinKey[1];
    assert(mapOriginX >= 0 && mapOriginY >= 0);

    // might not exactly be min / max of octree:
    octomap::point3d origin = m_octree->keyToCoord(m_paddedMinKey, m_treeDepth);
    double gridRes = m_octree->getNodeSize(m_maxTreeDepth);
    m_projectCompleteMap = (!m_incrementalUpdate || (std::abs(gridRes-m_gridmap.info.resolution) > 1e-6));
    m_gridmap.info.resolution = gridRes;
    m_gridmap.info.origin.position.x = origin.x() - gridRes*0.5;
    m_gridmap.info.origin.position.y = origin.y() - gridRes*0.5;
    if (m_maxTreeDepth != m_treeDepth){
      m_gridmap.info.origin.position.x -= m_res/2.0;
      m_gridmap.info.origin.position.y -= m_res/2.0;
    }

    // workaround for  multires. projection not working properly for inner nodes:
    // force re-building complete map
    if (m_maxTreeDepth < m_treeDepth)
      m_projectCompleteMap = true;


    if(m_projectCompleteMap){
      ROS_DEBUG("Rebuilding complete 2D map");
      m_gridmap.data.clear();
      // init to unknown:
      m_gridmap.data.resize(m_gridmap.info.width * m_gridmap.info.height, -1);

    } else {

       if (mapChanged(oldMapInfo, m_gridmap.info)){
          ROS_DEBUG("2D grid map size changed to %dx%d", m_gridmap.info.width, m_gridmap.info.height);
          adjustMapData(m_gridmap, oldMapInfo);
       }
       nav_msgs::OccupancyGrid::_data_type::iterator startIt;
       size_t mapUpdateBBXMinX = std::max(0, (int(m_updateBBXMin[0]) - int(m_paddedMinKey[0]))/int(m_multires2DScale));
       size_t mapUpdateBBXMinY = std::max(0, (int(m_updateBBXMin[1]) - int(m_paddedMinKey[1]))/int(m_multires2DScale));
       size_t mapUpdateBBXMaxX = std::min(int(m_gridmap.info.width-1), (int(m_updateBBXMax[0]) - int(m_paddedMinKey[0]))/int(m_multires2DScale));
       size_t mapUpdateBBXMaxY = std::min(int(m_gridmap.info.height-1), (int(m_updateBBXMax[1]) - int(m_paddedMinKey[1]))/int(m_multires2DScale));

       assert(mapUpdateBBXMaxX > mapUpdateBBXMinX);
       assert(mapUpdateBBXMaxY > mapUpdateBBXMinY);

       size_t numCols = mapUpdateBBXMaxX-mapUpdateBBXMinX +1;

       // test for max idx:
       uint max_idx = m_gridmap.info.width*mapUpdateBBXMaxY + mapUpdateBBXMaxX;
       if (max_idx  >= m_gridmap.data.size())
         ROS_ERROR("BBX index not valid: %d (max index %zu for size %d x %d) update-BBX is: [%zu %zu]-[%zu %zu]", max_idx, m_gridmap.data.size(), m_gridmap.info.width, m_gridmap.info.height, mapUpdateBBXMinX, mapUpdateBBXMinY, mapUpdateBBXMaxX, mapUpdateBBXMaxY);

       // reset proj. 2D map in bounding box:
       for (unsigned int j = mapUpdateBBXMinY; j <= mapUpdateBBXMaxY; ++j){
          std::fill_n(m_gridmap.data.begin() + m_gridmap.info.width*j+mapUpdateBBXMinX,
                      numCols, -1);
       }

    }



  }

}

void OctomapServer::handlePostNodeTraversal(const ros::Time& rostime){

  if (m_publish2DMap)
    m_mapPub.publish(m_gridmap);
}

void OctomapServer::handleOccupiedNode(const OcTreeT::iterator& it){

  if (m_publish2DMap && m_projectCompleteMap){
    update2DMap(it, true);
  }
}

void OctomapServer::handleFreeNode(const OcTreeT::iterator& it){

  if (m_publish2DMap && m_projectCompleteMap){
    update2DMap(it, false);
  }
}

void OctomapServer::handleOccupiedNodeInBBX(const OcTreeT::iterator& it){

  if (m_publish2DMap && !m_projectCompleteMap){
    update2DMap(it, true);
  }
}

void OctomapServer::handleFreeNodeInBBX(const OcTreeT::iterator& it){

  if (m_publish2DMap && !m_projectCompleteMap){
    update2DMap(it, false);
  }
}

void OctomapServer::update2DMap(const OcTreeT::iterator& it, bool occupied){

  // update 2D map (occupied always overrides):

  if (it.getDepth() == m_maxTreeDepth){
    unsigned idx = mapIdx(it.getKey());
    if (occupied)
      m_gridmap.data[mapIdx(it.getKey())] = 100;
    else if (m_gridmap.data[idx] == -1){
      m_gridmap.data[idx] = 0;
    }

  } else{
    int intSize = 1 << (m_maxTreeDepth - it.getDepth());
    octomap::OcTreeKey minKey=it.getIndexKey();
    for(int dx=0; dx < intSize; dx++){
      int i = (minKey[0]+dx - m_paddedMinKey[0])/m_multires2DScale;
      for(int dy=0; dy < intSize; dy++){
        unsigned idx = mapIdx(i, (minKey[1]+dy - m_paddedMinKey[1])/m_multires2DScale);
        if (occupied)
          m_gridmap.data[idx] = 100;
        else if (m_gridmap.data[idx] == -1){
          m_gridmap.data[idx] = 0;
        }
      }
    }
  }


}



bool OctomapServer::isSpeckleNode(const OcTreeKey&nKey) const {
  OcTreeKey key;
  bool neighborFound = false;
  for (key[2] = nKey[2] - 1; !neighborFound && key[2] <= nKey[2] + 1; ++key[2]){
    for (key[1] = nKey[1] - 1; !neighborFound && key[1] <= nKey[1] + 1; ++key[1]){
      for (key[0] = nKey[0] - 1; !neighborFound && key[0] <= nKey[0] + 1; ++key[0]){
        if (key != nKey){
          OcTreeNode* node = m_octree->search(key);
          if (node && m_octree->isNodeOccupied(node)){
            // we have a neighbor => break!
            neighborFound = true;
          }
        }
      }
    }
  }

  return neighborFound;
}

void OctomapServer::reconfigureCallback(octomap_server::OctomapServerConfig& config, uint32_t level){
  if (m_maxTreeDepth != unsigned(config.max_depth))
    m_maxTreeDepth = unsigned(config.max_depth);
  else{
    m_pointcloudMinZ            = config.pointcloud_min_z;
    m_pointcloudMaxZ            = config.pointcloud_max_z;
    m_occupancyMinZ             = config.occupancy_min_z;
    m_occupancyMaxZ             = config.occupancy_max_z;
    m_filterSpeckles            = config.filter_speckles;
    m_filterGroundPlane         = config.filter_ground;
    m_compressMap               = config.compress_map;
    m_incrementalUpdate         = config.incremental_2D_projection;

    // Parameters with a namespace require an special treatment at the beginning, as dynamic reconfigure
    // will overwrite them because the server is not able to match parameters' names.
    if (m_initConfig){
		// If parameters do not have the default value, dynamic reconfigure server should be updated.
		if(!is_equal(m_groundFilterDistance, 0.04))
          config.ground_filter_distance = m_groundFilterDistance;
		if(!is_equal(m_groundFilterAngle, 0.15))
          config.ground_filter_angle = m_groundFilterAngle;
	    if(!is_equal( m_groundFilterPlaneDistance, 0.07))
          config.ground_filter_plane_distance = m_groundFilterPlaneDistance;
        if(!is_equal(m_maxRange, -1.0))
          config.sensor_model_max_range = m_maxRange;
        if(!is_equal(m_octree->getProbHit(), 0.7))
          config.sensor_model_hit = m_octree->getProbHit();
	    if(!is_equal(m_octree->getProbMiss(), 0.4))
          config.sensor_model_miss = m_octree->getProbMiss();
		if(!is_equal(m_octree->getClampingThresMin(), 0.12))
          config.sensor_model_min = m_octree->getClampingThresMin();
		if(!is_equal(m_octree->getClampingThresMax(), 0.97))
          config.sensor_model_max = m_octree->getClampingThresMax();
        m_initConfig = false;

	    boost::recursive_mutex::scoped_lock reconf_lock(m_config_mutex);
        m_reconfigureServer.updateConfig(config);
    }
    else{
	  m_groundFilterDistance      = config.ground_filter_distance;
      m_groundFilterAngle         = config.ground_filter_angle;
      m_groundFilterPlaneDistance = config.ground_filter_plane_distance;
      m_maxRange                  = config.sensor_model_max_range;
      m_octree->setClampingThresMin(config.sensor_model_min);
      m_octree->setClampingThresMax(config.sensor_model_max);

     // Checking values that might create unexpected behaviors.
      if (is_equal(config.sensor_model_hit, 1.0))
		config.sensor_model_hit -= 1.0e-6;
      m_octree->setProbHit(config.sensor_model_hit);
	  if (is_equal(config.sensor_model_miss, 0.0))
		config.sensor_model_miss += 1.0e-6;
      m_octree->setProbMiss(config.sensor_model_miss);
	}
  }
  publishAll();
}

void OctomapServer::adjustMapData(nav_msgs::OccupancyGrid& map, const nav_msgs::MapMetaData& oldMapInfo) const{
  if (map.info.resolution != oldMapInfo.resolution){
    ROS_ERROR("Resolution of map changed, cannot be adjusted");
    return;
  }

  int i_off = int((oldMapInfo.origin.position.x - map.info.origin.position.x)/map.info.resolution +0.5);
  int j_off = int((oldMapInfo.origin.position.y - map.info.origin.position.y)/map.info.resolution +0.5);

  if (i_off < 0 || j_off < 0
      || oldMapInfo.width  + i_off > map.info.width
      || oldMapInfo.height + j_off > map.info.height)
  {
    ROS_ERROR("New 2D map does not contain old map area, this case is not implemented");
    return;
  }

  nav_msgs::OccupancyGrid::_data_type oldMapData = map.data;

  map.data.clear();
  // init to unknown:
  map.data.resize(map.info.width * map.info.height, -1);

  nav_msgs::OccupancyGrid::_data_type::iterator fromStart, fromEnd, toStart;

  for (int j =0; j < int(oldMapInfo.height); ++j ){
    // copy chunks, row by row:
    fromStart = oldMapData.begin() + j*oldMapInfo.width;
    fromEnd = fromStart + oldMapInfo.width;
    toStart = map.data.begin() + ((j+j_off)*m_gridmap.info.width + i_off);
    copy(fromStart, fromEnd, toStart);

//    for (int i =0; i < int(oldMapInfo.width); ++i){
//      map.data[m_gridmap.info.width*(j+j_off) +i+i_off] = oldMapData[oldMapInfo.width*j +i];
//    }

  }

}


std_msgs::ColorRGBA OctomapServer::heightMapColor(double h) {

  std_msgs::ColorRGBA color;
  color.a = 1.0;
  // blend over HSV-values (more colors)

  double s = 1.0;
  double v = 1.0;

  h -= floor(h);
  h *= 6;
  int i;
  double m, n, f;

  i = floor(h);
  f = h - i;
  if (!(i & 1))
    f = 1 - f; // if i is even
  m = v * (1 - s);
  n = v * (1 - s * f);

  switch (i) {
    case 6:
    case 0:
      color.r = v; color.g = n; color.b = m;
      break;
    case 1:
      color.r = n; color.g = v; color.b = m;
      break;
    case 2:
      color.r = m; color.g = v; color.b = n;
      break;
    case 3:
      color.r = m; color.g = n; color.b = v;
      break;
    case 4:
      color.r = n; color.g = m; color.b = v;
      break;
    case 5:
      color.r = v; color.g = m; color.b = n;
      break;
    default:
      color.r = 1; color.g = 0.5; color.b = 0.5;
      break;
  }

  return color;
}
}



