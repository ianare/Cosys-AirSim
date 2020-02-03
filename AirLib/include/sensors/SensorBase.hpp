// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#ifndef msr_airlib_SensorBase_hpp
#define msr_airlib_SensorBase_hpp

#include "common/Common.hpp"
#include "common/UpdatableObject.hpp"
#include "common/CommonStructs.hpp"
#include "physics/Environment.hpp"
#include "physics/Kinematics.hpp"

#include <map>

namespace msr { namespace airlib {

/*
    Derived classes should not do any work in constructor which requires ground truth.
    After construction of the derived class an initialize(...) must be made which would
    set the sensor in good-to-use state by call to reset.
*/
class SensorBase : public UpdatableObject  {
public:
    enum class SensorType : uint {
        Barometer = 1,
        Imu = 2,
        Gps = 3,
        Magnetometer = 4,
        Distance = 5,
        Lidar = 6,
		Echo = 7,
		GPULidar = 8
    };

    SensorBase(const std::string& sensor_name = "", const std::string& attach_link_name = "")
        : name_(sensor_name), attach_link_name_(attach_link_name)
    {}

protected:
    struct GroundTruth {
        const Kinematics::State* kinematics;
        const Environment* environment;
    };
public:
    virtual void initialize(const Kinematics::State* kinematics, const Environment* environment)
    {
        ground_truth_.kinematics = kinematics;
        ground_truth_.environment = environment;
    }

    const GroundTruth& getGroundTruth() const
    {
        return ground_truth_;
    }
   
    const std::string& getName() const
    {
        return name_;
    }

    const std::string& getAttachLinkName() const
    {
        return attach_link_name_;
    }

    virtual ~SensorBase() = default;

private:
    //ground truth can be shared between many sensors
    GroundTruth ground_truth_;
    std::string name_ = "";
    std::string attach_link_name_ = "";
};



}} //namespace
#endif
