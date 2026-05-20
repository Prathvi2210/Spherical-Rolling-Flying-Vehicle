/**
 * global_slam_node.cpp
 *
 * Global SLAM layer for autonomous drone.
 * Hardware: Synexens CS20 ToF (60°×45° FOV, 5m range, 8fps) on Jetson Orin Nano Super.
 *
 * Pipeline
 * ────────
 *   /kiss_icp/odometry   → keyframe gate → GTSAM BetweenFactor (odometry edges)
 *   /kiss_icp/local_map  → transform to sensor frame → Scan Context descriptor
 *                       → ring-key KNN + full-SC distance → loop-closure candidate
 *                       → ICP verification (using /cs20/pointcloud)
 *                       → GTSAM BetweenFactor (loop-closure edge)
 *                       → LM optimisation
 *
 * Published topics
 * ────────────────
 *   /gtsam/odometry    nav_msgs/Odometry      – globally corrected pose (20+ Hz)
 *   /gtsam/map_cloud   sensor_msgs/PointCloud2 – globally consistent 3-D map
 *   /gtsam/path        nav_msgs/Path           – full corrected trajectory
 *
 * Thread model
 * ────────────
 *   Main executor thread : odomCallback, publishes /gtsam/odometry at KissICP rate
 *   LC thread (2 Hz)     : SC detection + ICP + GTSAM optimisation on loop closure
 *
 * Mutex hierarchy (never hold inner while acquiring outer):
 *   1. gtsam_mutex_  (innermost – graph + values)
 *   2. kf_mutex_     (keyframe vector + SC manager)
 *   3. buf_mutex_    (sensor message buffers)
 */

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <nav_msgs/msg/path.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>

// PCL
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/registration/icp.h>
#include <pcl/common/transforms.h>

// GTSAM
#include <gtsam/geometry/Pose3.h>
#include <gtsam/geometry/Rot3.h>
#include <gtsam/inference/Symbol.h>
#include <gtsam/nonlinear/LevenbergMarquardtOptimizer.h>
#include <gtsam/nonlinear/NonlinearFactorGraph.h>
#include <gtsam/nonlinear/Values.h>
#include <gtsam/slam/BetweenFactor.h>
#include <gtsam/slam/PriorFactor.h>

// Scan Context (header-only, this package)
#include "global_slam/scan_context.hpp"

// Standard
#include <Eigen/Dense>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

using gtsam::symbol_shorthand::X;  // X(0), X(1), …

// ═══════════════════════════════════════════════════════════════════════════════
//  Geometry helpers
// ═══════════════════════════════════════════════════════════════════════════════
static Eigen::Isometry3d odomToEigen(const nav_msgs::msg::Odometry& msg)
{
    const auto& p = msg.pose.pose;
    Eigen::Quaterniond q(p.orientation.w, p.orientation.x,
                         p.orientation.y, p.orientation.z);
    Eigen::Isometry3d T = Eigen::Isometry3d::Identity();
    T.linear()      = q.normalized().toRotationMatrix();
    T.translation() = Eigen::Vector3d(p.position.x, p.position.y, p.position.z);
    return T;
}

static gtsam::Pose3 eigenToGtsam(const Eigen::Isometry3d& T)
{
    const Eigen::Quaterniond q(T.linear());
    return gtsam::Pose3(
        gtsam::Rot3(gtsam::Quaternion(q.w(), q.x(), q.y(), q.z())),
        gtsam::Point3(T.translation()));
}

static Eigen::Isometry3d gtsamToEigen(const gtsam::Pose3& pose)
{
    const auto qg = pose.rotation().toQuaternion();
    Eigen::Quaterniond q(qg.w(), qg.x(), qg.y(), qg.z());
    Eigen::Isometry3d T = Eigen::Isometry3d::Identity();
    T.linear()      = q.normalized().toRotationMatrix();
    T.translation() = Eigen::Vector3d(pose.x(), pose.y(), pose.z());
    return T;
}

static void fillOdomMsg(nav_msgs::msg::Odometry&     msg,
                        const Eigen::Isometry3d&     T,
                        const rclcpp::Time&          stamp,
                        const std::string&           frame,
                        const std::string&           child_frame)
{
    msg.header.stamp    = stamp;
    msg.header.frame_id = frame;
    msg.child_frame_id  = child_frame;
    Eigen::Quaterniond q(T.linear());
    msg.pose.pose.position.x    = T.translation().x();
    msg.pose.pose.position.y    = T.translation().y();
    msg.pose.pose.position.z    = T.translation().z();
    msg.pose.pose.orientation.x = q.x();
    msg.pose.pose.orientation.y = q.y();
    msg.pose.pose.orientation.z = q.z();
    msg.pose.pose.orientation.w = q.w();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  GlobalSlamNode
// ═══════════════════════════════════════════════════════════════════════════════
class GlobalSlamNode : public rclcpp::Node
{
public:
    GlobalSlamNode() : Node("global_slam_node")
    {
        // ── Parameters ───────────────────────────────────────────────────────
        declare_parameter("keyframe_dist_m",       0.3);
        declare_parameter("keyframe_angle_deg",    10.0);
        declare_parameter("icp_fitness_threshold", 0.25);
        declare_parameter("map_voxel_size_m",      0.10);
        declare_parameter("sc_voxel_size_m",       0.15);  // voxel size for SC input cloud
        declare_parameter("odom_frame",            std::string("odom"));
        declare_parameter("body_frame",            std::string("base_link"));

        kf_dist_m_  = get_parameter("keyframe_dist_m").as_double();
        kf_angle_r_ = get_parameter("keyframe_angle_deg").as_double() * M_PI / 180.0;
        icp_thresh_ = get_parameter("icp_fitness_threshold").as_double();
        map_voxel_  = get_parameter("map_voxel_size_m").as_double();
        sc_voxel_   = get_parameter("sc_voxel_size_m").as_double();
        odom_frame_ = get_parameter("odom_frame").as_string();
        body_frame_ = get_parameter("body_frame").as_string();

        // ── Subscriptions ─────────────────────────────────────────────────────
        const auto sensor_qos = rclcpp::SensorDataQoS();

        sub_odom_ = create_subscription<nav_msgs::msg::Odometry>(
            "/kiss_icp/odometry", sensor_qos,
            std::bind(&GlobalSlamNode::odomCallback, this, std::placeholders::_1));

        sub_local_map_ = create_subscription<sensor_msgs::msg::PointCloud2>(
            "/kiss_icp/local_map", sensor_qos,
            [this](sensor_msgs::msg::PointCloud2::SharedPtr msg) {
                std::lock_guard<std::mutex> lk(buf_mutex_);
                local_map_buf_ = std::move(msg);
            });

        sub_raw_ = create_subscription<sensor_msgs::msg::PointCloud2>(
            "/cs20/pointcloud", sensor_qos,
            [this](sensor_msgs::msg::PointCloud2::SharedPtr msg) {
                std::lock_guard<std::mutex> lk(buf_mutex_);
                raw_cloud_buf_ = std::move(msg);
            });

        // ── Publishers ────────────────────────────────────────────────────────
        pub_odom_ = create_publisher<nav_msgs::msg::Odometry>(
            "/gtsam/odometry", rclcpp::QoS(10));

        pub_map_ = create_publisher<sensor_msgs::msg::PointCloud2>(
            "/gtsam/map_cloud", rclcpp::QoS(1).transient_local());

        pub_path_ = create_publisher<nav_msgs::msg::Path>(
            "/gtsam/path", rclcpp::QoS(10));

        // ── Background loop-closure thread ────────────────────────────────────
        lc_thread_ = std::thread(&GlobalSlamNode::loopClosureThread, this);

        RCLCPP_INFO(get_logger(),
            "[GlobalSLAM] Ready — kf_dist=%.2f m  kf_angle=%.1f°  icp_thresh=%.3f",
            kf_dist_m_, kf_angle_r_ * 180.0 / M_PI, icp_thresh_);
    }

    ~GlobalSlamNode()
    {
        shutdown_.store(true);
        if (lc_thread_.joinable()) lc_thread_.join();
    }

private:
    // ─────────────────────────────────────────────────────────────────────────
    //  Keyframe
    // ─────────────────────────────────────────────────────────────────────────
    struct Keyframe {
        int          id;
        rclcpp::Time stamp;

        // Pose in odom frame as given by Kiss-ICP (drift accumulates here)
        Eigen::Isometry3d pose_odom;
        // Globally corrected pose maintained by GTSAM (updated on optimisation)
        Eigen::Isometry3d pose_gtsam;

        // Downsampled cloud in sensor/body frame (used for ICP and map building)
        pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_sensor;

        // Scan Context descriptor built from this keyframe
        scan_context::SCDesc sc;
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  odomCallback  (runs on ROS executor thread)
    // ─────────────────────────────────────────────────────────────────────────
    void odomCallback(const nav_msgs::msg::Odometry::SharedPtr msg)
    {
        const Eigen::Isometry3d pose_odom = odomToEigen(*msg);

        // Always publish corrected odometry at full Kiss-ICP rate
        {
            nav_msgs::msg::Odometry out;
            fillOdomMsg(out, applyCorrectionToPose(pose_odom),
                        msg->header.stamp, odom_frame_, body_frame_);
            pub_odom_->publish(out);
        }

        // ── Keyframe gating ───────────────────────────────────────────────────
        if (!shouldAddKeyframe(pose_odom)) return;

        // Grab latest sensor buffers (non-blocking copy)
        sensor_msgs::msg::PointCloud2::SharedPtr local_map_msg, raw_msg;
        {
            std::lock_guard<std::mutex> lk(buf_mutex_);
            local_map_msg = local_map_buf_;
            raw_msg       = raw_cloud_buf_;
        }
        if (!local_map_msg || !raw_msg) {
            // Buffers not yet populated — update last pose anyway to avoid
            // immediately re-triggering on the next callback
            last_kf_pose_ = pose_odom;
            return;
        }

        // ── Build keyframe ────────────────────────────────────────────────────
        Keyframe kf;
        kf.id        = static_cast<int>(keyframes_.size());
        kf.stamp     = msg->header.stamp;
        kf.pose_odom = pose_odom;

        // Transform /kiss_icp/local_map from odom frame into sensor frame.
        // local_map is expressed in odom frame; we need body-centred geometry
        // for the SC descriptor.
        pcl::PointCloud<pcl::PointXYZ> cloud_odom;
        pcl::fromROSMsg(*local_map_msg, cloud_odom);

        const Eigen::Isometry3d T_sensor_from_odom = pose_odom.inverse();
        pcl::PointCloud<pcl::PointXYZ> cloud_sensor_full;
        pcl::transformPointCloud(cloud_odom, cloud_sensor_full,
                                  T_sensor_from_odom.matrix().cast<float>());

        // Voxel downsample for SC computation and ICP
        auto cloud_ds = std::make_shared<pcl::PointCloud<pcl::PointXYZ>>();
        {
            pcl::VoxelGrid<pcl::PointXYZ> vg;
            vg.setInputCloud(cloud_sensor_full.makeShared());
            vg.setLeafSize(static_cast<float>(sc_voxel_),
                           static_cast<float>(sc_voxel_),
                           static_cast<float>(sc_voxel_));
            vg.filter(*cloud_ds);
        }
        kf.cloud_sensor = cloud_ds;

        // Build SC descriptor
        kf.sc = scan_context::makeScancontext(*cloud_ds);

        // Corrected pose — propagate last GTSAM correction to this keyframe
        kf.pose_gtsam = applyCorrectionToPose(pose_odom);

        // ── Add to GTSAM graph ────────────────────────────────────────────────
        addOdometryFactor(kf);

        // ── Store (this is the only writer of keyframes_) ─────────────────────
        {
            std::lock_guard<std::mutex> lk(kf_mutex_);
            keyframes_.push_back(kf);
            sc_manager_.addKeyframe(kf.sc);
        }
        new_kf_flag_.store(true);

        last_kf_pose_ = pose_odom;

        RCLCPP_DEBUG(get_logger(), "[GlobalSLAM] KF %d  pose=(%.2f, %.2f, %.2f)  cloud_pts=%zu",
            kf.id, pose_odom.translation().x(), pose_odom.translation().y(),
            pose_odom.translation().z(), cloud_ds->size());

        publishGlobalMap();
        publishPath();
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  GTSAM helpers
    // ─────────────────────────────────────────────────────────────────────────
    void addOdometryFactor(const Keyframe& kf)
    {
        std::lock_guard<std::mutex> lk(gtsam_mutex_);

        gtsam_values_.insert(X(kf.id), eigenToGtsam(kf.pose_gtsam));

        if (kf.id == 0) {
            // Tight prior at origin
            const auto prior_noise = gtsam::noiseModel::Diagonal::Sigmas(
                (gtsam::Vector6() << 1e-6, 1e-6, 1e-6, 1e-4, 1e-4, 1e-4).finished());
            gtsam_graph_.addPrior(X(0), eigenToGtsam(kf.pose_gtsam), prior_noise);
        } else {
            // Odometry edge: relative pose between consecutive keyframes.
            // Noise scales with translation distance (5% of distance).
            const Eigen::Isometry3d T_rel = prev_kf_odom_pose_.inverse() * kf.pose_odom;
            const double d = T_rel.translation().norm();
            const gtsam::Vector6 sigmas =
                (gtsam::Vector6() << 0.05, 0.05, 0.10,
                                     d * 0.05 + 0.02,
                                     d * 0.05 + 0.02,
                                     d * 0.05 + 0.02).finished();
            const auto odom_noise = gtsam::noiseModel::Diagonal::Sigmas(sigmas);
            gtsam_graph_.emplace_shared<gtsam::BetweenFactor<gtsam::Pose3>>(
                X(kf.id - 1), X(kf.id), eigenToGtsam(T_rel), odom_noise);
        }
        prev_kf_odom_pose_ = kf.pose_odom;
    }

    void addLoopClosureFactor(int from_id, int to_id,
                               const Eigen::Isometry3d& T_from_to)
    {
        // Noise model for loop closures: looser than odometry
        // (ICP can be off by a few cm / a few degrees on CS20 geometry)
        const auto lc_noise = gtsam::noiseModel::Diagonal::Sigmas(
            (gtsam::Vector6() << 0.3, 0.3, 0.3, 0.10, 0.10, 0.10).finished());

        {
            std::lock_guard<std::mutex> lk(gtsam_mutex_);
            gtsam_graph_.emplace_shared<gtsam::BetweenFactor<gtsam::Pose3>>(
                X(from_id), X(to_id), eigenToGtsam(T_from_to), lc_noise);
        }

        optimizeGraph();
        publishGlobalMap();
        publishPath();
    }

    // Levenberg–Marquardt optimisation.
    // Acquires gtsam_mutex_ then kf_mutex_ (in that order — never reversed).
    void optimizeGraph()
    {
        gtsam::Values result;
        {
            std::lock_guard<std::mutex> lk(gtsam_mutex_);
            gtsam::LevenbergMarquardtParams params;
            params.maxIterations    = 30;
            params.relativeErrorTol = 1e-5;
            params.absoluteErrorTol = 1e-6;
            params.verbosity        = gtsam::NonlinearOptimizerParams::SILENT;
            try {
                result = gtsam::LevenbergMarquardtOptimizer(
                             gtsam_graph_, gtsam_values_, params).optimize();
                gtsam_values_ = result;
            } catch (const std::exception& e) {
                RCLCPP_ERROR(get_logger(), "[GlobalSLAM] GTSAM optimisation failed: %s", e.what());
                return;
            }
        }

        // Propagate corrected poses back into keyframe records
        {
            std::lock_guard<std::mutex> lk(kf_mutex_);
            for (auto& kf : keyframes_) {
                if (result.exists(X(kf.id))) {
                    kf.pose_gtsam = gtsamToEigen(result.at<gtsam::Pose3>(X(kf.id)));
                }
            }
        }
        correction_ready_.store(true);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Loop closure background thread  (2 Hz)
    // ─────────────────────────────────────────────────────────────────────────
    void loopClosureThread()
    {
        rclcpp::Rate rate(2.0);

        while (!shutdown_.load()) {
            rate.sleep();
            if (!new_kf_flag_.exchange(false)) continue;

            // ── Snapshot current state (quick, under kf_mutex_) ───────────────
            int    current_id;
            scan_context::SCDesc query_sc;
            pcl::PointCloud<pcl::PointXYZ>::Ptr query_cloud;

            {
                std::lock_guard<std::mutex> lk(kf_mutex_);
                if (keyframes_.size() < 2) continue;
                const auto& back = keyframes_.back();
                current_id  = back.id;
                query_sc    = back.sc;
                query_cloud = back.cloud_sensor;
            }

            // ── SC candidate search ───────────────────────────────────────────
            // detectLoop is const on the SC manager – safe without mutex
            // as long as we never write to sc_manager_ concurrently.
            // (addKeyframe in odomCallback holds kf_mutex_, LC thread does not.)
            auto [loop_id, yaw_diff_rad, sc_dist] =
                sc_manager_.detectLoop(query_sc, current_id);

            if (loop_id < 0) continue;

            RCLCPP_INFO(get_logger(),
                "[GlobalSLAM] SC candidate  cur=%d  cand=%d  dist=%.3f  yaw=%.1f°",
                current_id, loop_id, sc_dist, yaw_diff_rad * 180.0 / M_PI);

            // ── Retrieve target keyframe data ─────────────────────────────────
            pcl::PointCloud<pcl::PointXYZ>::Ptr target_cloud;
            Eigen::Isometry3d target_pose, current_pose;
            {
                std::lock_guard<std::mutex> lk(kf_mutex_);
                if (loop_id >= static_cast<int>(keyframes_.size())) continue;
                target_cloud  = keyframes_[loop_id].cloud_sensor;
                target_pose   = keyframes_[loop_id].pose_gtsam;
                current_pose  = keyframes_[current_id].pose_gtsam;
            }

            if (!target_cloud || target_cloud->empty()) continue;

            // ── ICP verification ──────────────────────────────────────────────
            // Initial guess: yaw from SC + translation from pose graph
            Eigen::Isometry3d T_init = Eigen::Isometry3d::Identity();
            T_init.linear() = Eigen::AngleAxisd(yaw_diff_rad,
                                                 Eigen::Vector3d::UnitZ())
                              .toRotationMatrix();
            // Override translation with pose-graph estimate (more reliable than SC)
            T_init.translation() = (target_pose.inverse() * current_pose).translation();

            pcl::IterativeClosestPoint<pcl::PointXYZ, pcl::PointXYZ> icp;
            icp.setInputSource(query_cloud);
            icp.setInputTarget(target_cloud);
            icp.setMaxCorrespondenceDistance(1.0);   // 1 m: generous for init error
            icp.setMaximumIterations(50);
            icp.setTransformationEpsilon(1e-6);
            icp.setEuclideanFitnessEpsilon(1e-5);

            pcl::PointCloud<pcl::PointXYZ> aligned;
            icp.align(aligned, T_init.matrix().cast<float>());

            const double fitness = icp.getFitnessScore();
            if (!icp.hasConverged() || fitness > icp_thresh_) {
                RCLCPP_WARN(get_logger(),
                    "[GlobalSLAM] ICP rejected  fitness=%.4f (threshold=%.4f)  converged=%d",
                    fitness, icp_thresh_, icp.hasConverged());
                continue;
            }

            // ── Accept loop closure ───────────────────────────────────────────
            Eigen::Isometry3d T_icp;
            T_icp.matrix() = icp.getFinalTransformation().cast<double>();

            RCLCPP_WARN(get_logger(),
                "[GlobalSLAM] ✓ Loop closure ACCEPTED  %d → %d  "
                "fitness=%.4f  sc_dist=%.3f",
                loop_id, current_id, fitness, sc_dist);

            addLoopClosureFactor(loop_id, current_id, T_icp);
            lc_count_++;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Keyframe selection
    // ─────────────────────────────────────────────────────────────────────────
    bool shouldAddKeyframe(const Eigen::Isometry3d& pose_now) const
    {
        if (keyframes_.empty()) return true;

        const Eigen::Isometry3d delta = last_kf_pose_.inverse() * pose_now;
        const double dist  = delta.translation().norm();
        const double angle = Eigen::AngleAxisd(delta.linear()).angle();

        return dist >= kf_dist_m_ || angle >= kf_angle_r_;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Pose correction
    //  Computes: correction = gtsam_last * odom_last.inverse()
    //  Applies to any odom-frame pose to get a globally corrected pose.
    // ─────────────────────────────────────────────────────────────────────────
    Eigen::Isometry3d applyCorrectionToPose(const Eigen::Isometry3d& pose_odom) const
    {
        if (!correction_ready_.load()) return pose_odom;

        std::lock_guard<std::mutex> lk(kf_mutex_);
        if (keyframes_.empty()) return pose_odom;

        const auto& last = keyframes_.back();
        const Eigen::Isometry3d correction =
            last.pose_gtsam * last.pose_odom.inverse();
        return correction * pose_odom;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Publishers
    // ─────────────────────────────────────────────────────────────────────────
    void publishGlobalMap()
    {
        auto global_map = std::make_shared<pcl::PointCloud<pcl::PointXYZ>>();

        {
            std::lock_guard<std::mutex> lk(kf_mutex_);
            for (const auto& kf : keyframes_) {
                if (!kf.cloud_sensor || kf.cloud_sensor->empty()) continue;
                pcl::PointCloud<pcl::PointXYZ> tmp;
                pcl::transformPointCloud(*kf.cloud_sensor, tmp,
                    kf.pose_gtsam.matrix().cast<float>());
                *global_map += tmp;
            }
        }

        if (global_map->empty()) return;

        pcl::VoxelGrid<pcl::PointXYZ> vg;
        vg.setInputCloud(global_map);
        vg.setLeafSize(static_cast<float>(map_voxel_),
                       static_cast<float>(map_voxel_),
                       static_cast<float>(map_voxel_));
        pcl::PointCloud<pcl::PointXYZ> map_ds;
        vg.filter(map_ds);

        sensor_msgs::msg::PointCloud2 out;
        pcl::toROSMsg(map_ds, out);
        out.header.stamp    = now();
        out.header.frame_id = odom_frame_;
        pub_map_->publish(out);
    }

    void publishPath()
    {
        nav_msgs::msg::Path path;
        path.header.stamp    = now();
        path.header.frame_id = odom_frame_;

        {
            std::lock_guard<std::mutex> lk(kf_mutex_);
            path.poses.reserve(keyframes_.size());
            for (const auto& kf : keyframes_) {
                geometry_msgs::msg::PoseStamped ps;
                ps.header = path.header;
                ps.header.stamp = kf.stamp;
                Eigen::Quaterniond q(kf.pose_gtsam.linear());
                ps.pose.position.x    = kf.pose_gtsam.translation().x();
                ps.pose.position.y    = kf.pose_gtsam.translation().y();
                ps.pose.position.z    = kf.pose_gtsam.translation().z();
                ps.pose.orientation.x = q.x();
                ps.pose.orientation.y = q.y();
                ps.pose.orientation.z = q.z();
                ps.pose.orientation.w = q.w();
                path.poses.push_back(ps);
            }
        }
        pub_path_->publish(path);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Member variables
    // ─────────────────────────────────────────────────────────────────────────

    // Parameters
    double      kf_dist_m_, kf_angle_r_, icp_thresh_, map_voxel_, sc_voxel_;
    std::string odom_frame_, body_frame_;

    // Keyframe state (guarded by kf_mutex_)
    std::vector<Keyframe>            keyframes_;
    scan_context::ScanContextManager sc_manager_;
    mutable std::mutex               kf_mutex_;

    // GTSAM graph (guarded by gtsam_mutex_)
    gtsam::NonlinearFactorGraph gtsam_graph_;
    gtsam::Values               gtsam_values_;
    Eigen::Isometry3d           prev_kf_odom_pose_ = Eigen::Isometry3d::Identity();
    mutable std::mutex          gtsam_mutex_;

    // Keyframe gating state (main thread only – no mutex needed)
    Eigen::Isometry3d last_kf_pose_ = Eigen::Isometry3d::Identity();

    // Cross-thread signals
    std::atomic<bool> correction_ready_{ false };
    std::atomic<bool> new_kf_flag_{ false };
    std::atomic<bool> shutdown_{ false };

    // Counters
    int lc_count_ = 0;

    // Sensor message buffers (guarded by buf_mutex_)
    sensor_msgs::msg::PointCloud2::SharedPtr local_map_buf_, raw_cloud_buf_;
    std::mutex                               buf_mutex_;

    // ROS interfaces
    rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr       sub_odom_;
    rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr sub_local_map_;
    rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr sub_raw_;

    rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr          pub_odom_;
    rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr    pub_map_;
    rclcpp::Publisher<nav_msgs::msg::Path>::SharedPtr              pub_path_;

    // Background thread
    std::thread lc_thread_;
};

// ═══════════════════════════════════════════════════════════════════════════════
//  main
// ═══════════════════════════════════════════════════════════════════════════════
int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);

    rclcpp::executors::MultiThreadedExecutor executor(
        rclcpp::ExecutorOptions(), 2);  // 2 threads: callbacks + LC thread

    auto node = std::make_shared<GlobalSlamNode>();
    executor.add_node(node);
    executor.spin();
    rclcpp::shutdown();
    return 0;
}
