#!/usr/bin/env python

import setup_path 
import airsim
import rospy
import tf
from std_msgs.msg import String
from geometry_msgs.msg import PoseStamped
import sensor_msgs.point_cloud2 as pc2
from sensor_msgs.msg import PointCloud2, PointField
from roslib import message
import random 
import time
import numpy as np


def airpub():
    pub = rospy.Publisher("airsimLidar", PointCloud2, queue_size=1)
    rospy.init_node('airsim_lidar_pub', anonymous=True)
    rate = rospy.Rate(10) # 10hz

    # connect to the AirSim simulator 
    client = airsim.CarClient()
    client.confirmConnection()

    lastTimestamp = None

    while not rospy.is_shutdown():

        # initiate point cloud
        pcloud = PointCloud2()

        # get lidar data
        lidarData = client.getLidarData()

        # check first if the data is from a new measurement by comparing timestamps
        if lidarData.time_stamp != lastTimestamp:
            #Check if there are any points in the data
            if (len(lidarData.point_cloud) < 4):
                lastTimestamp = lidarData.time_stamp
            else:
                lastTimestamp = lidarData.time_stamp

                points = np.array(lidarData.point_cloud, dtype=np.dtype('f4'))
                points = np.reshape(points, (int(points.shape[0] / 3), 3))
                # make point cloud
                cloud = []
                for point in points:      
                    cloud.append(list(point * np.array([1, -1, -1])))

                pcloud.header.frame_id = "airSimPoseFrame"
                pcloud = pc2.create_cloud_xyz32(pcloud.header, cloud)
                
                #publish Pointcloud2 message
                pub.publish(pcloud)

        # sleeps until next cycle 
        rate.sleep()



if __name__ == '__main__':
    try:
        airpub()
    except rospy.ROSInterruptException:
        pass
