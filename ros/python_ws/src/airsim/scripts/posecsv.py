# In settings.json first activate computer vision mode: 
# https://github.com/Microsoft/AirSim/blob/master/docs/image_apis.md#computer-vision-mode

import airsimpy
import os
import csv
from geometry_msgs.msg import Pose, Point, Quaternion, PoseStamped

class PoseCSV():
    """
        Read or write Pose messages into a csv file for Airsim
    """
    def __init__(self, location, isWriter):
        """
            Read or write ROS Pose messages into a csv file

                location: (str) location of csv file
                isWriter: (bool) act as writer or reader
        """
        self.location = location
        self.csvColumns = ['px', 'py', 'pz', 'ox', 'oy', 'oz', 'ow']
        self.isWriter = isWriter
        if self.isWriter:
            print("Initialising writer...")
            try:
                self.csvFile = open(location, 'w')
                self.writer = csv.DictWriter(self.csvFile, fieldnames=self.csvColumns)
                self.writer.writeheader()
            except IOError as e:
                print("I/O Error: {}".format(e))
                exit()
            except Exception as e:
                print("Error: {}".format(e))
                self.csvFile.close()
                exit()
        else:
            print("Initialising reader...")
            try:
                self.csvFile = open(location, 'r')
                self.reader = csv.DictReader(self.csvFile)
            except IOError as e:
                print("I/O Error: {}".format(e))
                exit()
            except Exception as e:
                print("Error: {}".format(e))
                self.csvFile.close()
                exit()
        
    def writePose(self, pose):
        """
            Write pose of type Airsim Pose to csv file and transform for driving using navigation stack ROS (move base)
        """
        pos = pose.position
        #inverse of pos
        pos.x_val = pos.x_val
        pos.y_val = -pos.y_val
        pos.z_val = -pos.z_val
        orientation = pose.orientation.inverse()
        poserow = {'px': pos.x_val, 'py': pos.y_val, 'pz': pos.z_val, 
                    'ox': orientation.x_val, 'oy': orientation.x_val, 'oz': orientation.z_val, 'ow': orientation.w_val}
        self.writer.writerow(poserow)

    def readPoses(self):
        """
            Read all poses as ROS Pose objects from csv file
        """
        poses = []
        try:
            for dictpose in self.reader:
                pose = Pose(Point(float(dictpose['px']), float(dictpose['py']), float(dictpose['pz'])), Quaternion(float(dictpose['ox']), float(dictpose['oy']), float(dictpose['oz']), float(dictpose['ow'])))
                poses.append(pose)
        except Exception as e:
            print("Could not read csv file, error: {}".format(e))
            self.close()
            exit()
        return poses

    def close(self):
        self.csvFile.close()
        
if __name__ == '__main__':
    import argparse
    from multiprocessing import Process
    from time import sleep
    parser = argparse.ArgumentParser(description="Record a traject (set of poses).")
    parser.add_argument('--csvfile', type=str, default="poses.csv", help="File location to read or write poses")
    parser.add_argument('--record', action='store_true', help="Enable record and write functionality")
    args = parser.parse_args()

    def looprecord(client, posewriter):
        global isActive
        nr = 0
        while isActive:
            print("Recording pose {}...".format(nr))
            pose = client.simGetVehiclePose('airsimvehicle')
            posewriter.writePose(pose)
            print(str(pose) + "\n") #is airsim pose, but we need ROS Pose
            nr = nr + 1
            sleep(4)

    
    csvFileLocation = args.csvfile
    isRecord = args.record
    
    writer = PoseCSV(csvFileLocation, isRecord)

    if isRecord:
        global isActive
        client = airsimpy.VehicleClient()
        client.confirmConnection()
        p = Process(target=looprecord, args=(client, writer,))

        while True:
            command = raw_input("[MAIN] Enter command 'c' to stop and 'r' to start recording: ")
            if command == 'c':
                isActive = False
                break
            elif command == 'r':
                isActive = True
                print("[MAIN] Starting record loop (2 Hz)...")
                sleep(2)
                p.start()
            else:
                print("[MAIN] Did not understand command: {}".format(command))
        
        if p.is_alive():
            p.join()
        writer.close()
        print("[MAIN] Verifying file ...")

    print("Reading out poses...")
    reader = PoseCSV(csvFileLocation, False)
    poses = reader.readPoses()
    for pose in poses:
        print(pose)
    reader.close()
    