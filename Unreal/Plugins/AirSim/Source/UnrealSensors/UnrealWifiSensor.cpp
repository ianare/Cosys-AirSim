// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#define DRAWDEBUGLINES

#include "UnrealWifiSensor.h"
#include "AirBlueprintLib.h"
#include "common/Common.hpp"
#include "NedTransform.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "CoreMinimal.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include <PxScene.h>
#include "Runtime/Core/Public/Async/ParallelFor.h"
#include "Beacons/WifiBeacon.h"
#include <typeinfo>
#include <mutex>
#include "common/CommonStructs.hpp"

using std::fill_n;
std::mutex mtxWifi;
// ctor
UnrealWifiSensor::UnrealWifiSensor(const AirSimSettings::WifiSetting& setting, AActor* actor, const NedTransform* ned_transform)
	: WifiSimple(setting), actor_(actor), ned_transform_(ned_transform), saved_clockspeed_(1), sensor_params_(getParams()), external_(getParams().external)
{
	// Initialize the trace directions
	sampleSphereCap(sensor_params_.number_of_traces, sensor_params_.sensor_opening_angle);

	traceRayMaxDistance = 200;
	traceRayMaxBounces = 5;
	traceRayMinSignalStrength = 0.001;

	for (TActorIterator<AWifiBeacon> It(actor_->GetWorld()); It; ++It)
	{
		beacon_actors.Add(*It);
	}
}

// Set WifiSensor object in correct pose in physical world
void UnrealWifiSensor::updatePose(const msr::airlib::Pose& sensor_pose, const msr::airlib::Pose& vehicle_pose)
{
	sensor_reference_frame_ = VectorMath::add(sensor_pose, vehicle_pose);
	if (sensor_params_.draw_sensor) {
		FVector sensor_position;
		if (external_) {
			sensor_position = ned_transform_->toFVector(sensor_reference_frame_.position, 100, true);
		}
		else {
			sensor_position = ned_transform_->fromLocalNed(sensor_reference_frame_.position);
		}
		DrawDebugPoint(actor_->GetWorld(), sensor_position, 5, FColor::Black, false, 0.3);
		FVector sensor_direction = Vector3rToFVector(VectorMath::rotateVector(VectorMath::front(), sensor_reference_frame_.orientation, 1));
		DrawDebugCoordinateSystem(actor_->GetWorld(), sensor_position, sensor_direction.Rotation(), 25, false, 0.3, 10);
	}
}

// Pause Unreal simulation
void UnrealWifiSensor::pause(const bool is_paused) {
	if (is_paused) {
		saved_clockspeed_ = UAirBlueprintLib::getUnrealClockSpeed(actor_);
		UAirBlueprintLib::setUnrealClockSpeed(actor_, 0);
	}
	else {
		UAirBlueprintLib::setUnrealClockSpeed(actor_, saved_clockspeed_);
	}
} 


// Get WifiSensor pose in Local NED
void UnrealWifiSensor::getLocalPose(msr::airlib::Pose& sensor_pose)
{
	FVector sensor_direction = Vector3rToFVector(VectorMath::rotateVector(VectorMath::front(), sensor_reference_frame_.orientation, 1)); ;
	sensor_pose = ned_transform_->toLocalNed(FTransform(sensor_direction.Rotation(), ned_transform_->toFVector(sensor_reference_frame_.position, 100, true), FVector(1, 1, 1)));
}

/*void UnrealWifiSensor::getPointCloud(const msr::airlib::Pose& sensor_pose, const msr::airlib::Pose& vehicle_pose, msr::airlib::vector<msr::airlib::real_T>& point_cloud)
{
	// Set the physical WifiSensor mesh in the correct location in the world
	updatePose(sensor_pose, vehicle_pose);

	point_cloud.clear();
	Vector3r random_vector = ned_transform_->toVector3r(FMath::RandPointInBox(FBox(FVector(-1, -1, -1), FVector(1, 1, 1))), 1.0f, true);
	point_cloud.emplace_back(random_vector.x());
	point_cloud.emplace_back(random_vector.y());
	point_cloud.emplace_back(random_vector.z());
}*/

/*void UnrealWifiSensor::setPointCloud(const msr::airlib::Pose& sensor_pose, msr::airlib::vector<msr::airlib::real_T>& point_cloud, msr::airlib::TTimePoint time_stamp) {
	// TODO consume point cloud (+ draw_time_)?

	const int DATA_PER_POINT = 5;
	for (int point_count = 0; point_count < point_cloud.size(); point_count += DATA_PER_POINT) {
		Vector3r point_local = Vector3r(point_cloud[point_count], point_cloud[point_count + 1], point_cloud[point_count + 2]);
		Vector3r point_global1 = VectorMath::transformToWorldFrame(point_local, sensor_pose, true);
		FVector point_global = ned_transform_->fromLocalNed(point_global1);

		DrawDebugPoint(actor_->GetWorld(), point_global, 10, FColor::Orange, false, 1.05f / sensor_params_.measurement_frequency);
	}
}*/

void UnrealWifiSensor::updateWifiRays() {
	const GroundTruth& ground_truth = getGroundTruth();
	
	Vector3r sensorBase_local = Vector3r(sensor_reference_frame_.position);
	FVector sensorBase_global = ned_transform_->fromLocalNed(sensorBase_local);

	//actor_->GetWorld()->GetPhysicsScene()->GetPxScene()->lockRead();

	// Clear oldest Wifi Hits 
	while (beaconsActive_.IsValidIndex(maxWifiHits)) {
		beaconsActive_.RemoveAt(0);
	}
	// Create new log record for newest Wifi measurements
	TArray<msr::airlib::WifiHit> WifiHitLog;

	//ParallelFor(sample_directions_.size(), [&](int32 direction_count) {
	for (int32 direction_count = 0; direction_count< sample_directions_.size(); direction_count++){
		Vector3r sample_direction = sample_directions_[direction_count];

		//FVector trace_direction = ned_transform_->toFVector(VectorMath::rotateVector(sample_direction, sensor_reference_frame_.orientation, 1), 1.0f, true); 

		msr::airlib::Quaternionr sensorOrientationQuat = sensor_reference_frame_.orientation;
		float roll, pitch, yaw;

		msr::airlib::VectorMath::toEulerianAngle(sensorOrientationQuat, roll, pitch, yaw);
		FRotator sensorOrientationEulerDegree = FRotator(FMath::RadiansToDegrees(pitch), FMath::RadiansToDegrees(yaw), FMath::RadiansToDegrees(roll));

		FVector lineEnd = wifiTraceMaxDistances[direction_count] * FVector(sample_direction[0], sample_direction[1], sample_direction[2]);
		lineEnd = sensorOrientationEulerDegree.RotateVector(lineEnd);
		lineEnd += sensorBase_global;
		//DrawDebugLine(actor_->GetWorld(), sensorBase_global, lineEnd, FColor::Red, false, 1);

		// Trace ray, bounce and add to hitlog if beacon was hit
		traceDirection(sensorBase_global, lineEnd, &WifiHitLog, 0, 0, 1, 0);
	//});
	}
	//actor_->GetWorld()->GetPhysicsScene()->GetPxScene()->unlockRead();
	beaconsActive_.Add(WifiHitLog);
}

// Thanks Girmi
void UnrealWifiSensor::sampleSphereCap(int num_points, float opening_angle) {
	sample_directions_.clear();

	wifiTraceMaxDistances.clear();

	// Add point in frontal direction
	sample_directions_.emplace(sample_directions_.begin(), Vector3r(1, 0, 0));
	wifiTraceMaxDistances.push_back(10000);

	//Convert opening angle to plane coordinates
	float x_limit = FMath::Cos(FMath::DegreesToRadians(opening_angle) / 2);

	// Calculate ratio of sphere surface to cap surface.
	// Scale points accordingly, e.g. if nPoints = 10 and ratio = 0.01,
	// generate 1000 points on the sphere
	float h = 1 - x_limit;
	float surface_ratio = h / 2;  // (4 * pi * R^2) / (2 * pi * R * h)
	int num_sphere_points = FMath::CeilToInt(num_points * 1 / surface_ratio);

	// Generate points on the sphere, retain those within the opening angle
	float offset = 2.0f / num_sphere_points;
	float increment = PI * (3.0f - FMath::Sqrt(5.0f));
	for (auto i = 1; i <= num_sphere_points; ++i)
	{
		float y = ((i * offset) - 1) + (offset / 2.0f);
		float r = FMath::Sqrt(1 - FMath::Pow(y, 2));
		float phi = ((i + 1) % num_sphere_points) * increment;
		float x = FMath::Cos(phi) * r;
		if (sample_directions_.size() == num_points)
		{
			return;
		}
		else if (x >= x_limit)
		{
			float z = FMath::Sin(phi) * r;
			sample_directions_.emplace_back(Vector3r(x, y, z));

			// Calculate max distance (and probably attentuation and such)
			Vector3r center = Vector3r(1, 0, 0);
			float angleToCenter = FMath::RadiansToDegrees(VectorMath::angleBetween(center, Vector3r(x, y, z), true));
			angleToCenter += 10000;
			wifiTraceMaxDistances.push_back(angleToCenter);
		}
	}
}

int UnrealWifiSensor::traceDirection(FVector trace_start_position, FVector trace_end_position, TArray<msr::airlib::WifiHit> *WifiHitLog, float traceRayCurrentDistance, float traceRayCurrentbounces, float traceRayCurrentSignalStrength, bool drawDebug) {
	FHitResult trace_hit_result;
	bool trace_hit;
	TArray<AActor*> ignore_actors_;

	if (traceRayCurrentDistance < traceRayMaxDistance) {
		if (traceRayCurrentbounces < traceRayMaxBounces) {
			if (traceRayCurrentSignalStrength > traceRayMinSignalStrength) {
				trace_hit_result = FHitResult(ForceInit);
				trace_hit = UAirBlueprintLib::GetObstacleAdv(actor_, trace_start_position, trace_end_position, trace_hit_result, ignore_actors_, ECC_Visibility, true, true);
				

				// Stop if nothing was hit to reflect off
				if (!trace_hit) {
					if (drawDebug) {
						DrawDebugLine(actor_->GetWorld(), trace_start_position, trace_end_position, FColor::Red, false, 0.1);
					}
					return 1;
				}

				// Bounce trace
				FVector trace_direction;
				float trace_length;
				FVector trace_start_original = trace_start_position;
				bounceTrace(trace_start_position, trace_direction, trace_length, trace_hit_result, traceRayCurrentDistance, traceRayCurrentSignalStrength);
				trace_end_position = trace_start_position + trace_direction * trace_length;
				traceRayCurrentbounces += 1;

				// If beacon was hit
				if (trace_hit_result.Actor != nullptr) {
					//if ((trace_hit_result.Actor->GetName().Len() >= 10) && (trace_hit_result.Actor->GetName().Left(10) == "wifiBeacon")) {
					if (trace_hit_result.Actor->IsA(AWifiBeacon::StaticClass())) {
						mtxWifi.lock();
						auto hitActor = trace_hit_result.GetActor();
						
						int tmpRssi = (int)traceRayCurrentSignalStrength;
						FVector beaconPos = trace_hit_result.Actor->GetActorLocation();
						
						msr::airlib::WifiHit thisHit;
						
						thisHit.beaconID = TCHAR_TO_UTF8(*hitActor->GetName());
						
						thisHit.rssi = tmpRssi;
						thisHit.beaconPosX = beaconPos[0];
						thisHit.beaconPosY = beaconPos[1];
						thisHit.beaconPosZ = beaconPos[2];
						WifiHitLog->Add(thisHit);
						mtxWifi.unlock();
					}
				}

				if (drawDebug) {
					DrawDebugLine(actor_->GetWorld(), trace_start_original, trace_start_position, FColor::Red, false, 0.1);
				}

				return(traceDirection(trace_start_position, trace_end_position, WifiHitLog, traceRayCurrentDistance, traceRayCurrentbounces, traceRayCurrentSignalStrength, drawDebug));		
			}
		}
	}
	return 1;
}

void UnrealWifiSensor::bounceTrace(FVector &trace_start_position, FVector &trace_direction, float &trace_length, const FHitResult &trace_hit_result, float &total_distance, float &signal_attenuation) {

	float distance_traveled = ned_transform_->toNed(FVector::Distance(trace_start_position, trace_hit_result.ImpactPoint));

	//TMP
	float attenuation_atmospheric_ = 50;

	// Attenuate signal
	signal_attenuation += getFreeSpaceLoss(total_distance, distance_traveled);
	signal_attenuation += (distance_traveled * attenuation_atmospheric_);
	signal_attenuation += getReflectionAttenuation(trace_hit_result);

	total_distance += distance_traveled;

	// Reflect signal 
	trace_direction = trace_hit_result.ImpactPoint - trace_start_position;
	trace_direction.Normalize();
	trace_start_position = trace_hit_result.ImpactPoint;
	trace_length = ned_transform_->fromNed(remainingDistance(signal_attenuation, total_distance));
}

float UnrealWifiSensor::angleBetweenVectors(FVector vector1, FVector vector2) {
	// Location relative to origin
	vector1.Normalize();
	vector2.Normalize();

	return FMath::Acos(FVector::DotProduct(vector1, vector2));
}

float UnrealWifiSensor::getFreeSpaceLoss(float previous_distance, float added_distance) {
	float spread_attenuation;
	float total_distance = previous_distance + added_distance;

	//TMP
	float wavelength_ = 0.1;

	float attenuation_previous = -20 * FMath::LogX(10, 4 * M_PI * previous_distance / wavelength_);
	attenuation_previous = (attenuation_previous > 0) ? 0 : attenuation_previous;

	float attenuation_total = -20 * FMath::LogX(10, 4 * M_PI * total_distance / wavelength_);
	attenuation_total = (attenuation_total > 0) ? 0 : attenuation_total;

	spread_attenuation = attenuation_total - attenuation_previous;

	return spread_attenuation;
}

float UnrealWifiSensor::getReflectionAttenuation(const FHitResult &trace_hit_result) {
	float materialAttenuation = 0.0f;

	if (trace_hit_result.PhysMaterial.IsValid()) {
		EPhysicalSurface currentSurfaceType = UPhysicalMaterial::DetermineSurfaceType(trace_hit_result.PhysMaterial.Get());

		for (std::map<EPhysicalSurface, float>::const_iterator it = material_attenuations_.begin(); it != material_attenuations_.end(); ++it) {
			if (currentSurfaceType == it->first)
			{
				materialAttenuation = it->second;
			}
		}
	}

	return materialAttenuation;
}

float UnrealWifiSensor::remainingDistance(float signal_attenuation, float total_distance) {
	//	float distanceFSPL;
	//	float distanceAtmospheric = FMath::Pow(10, (signal_attenuation - attenuation_limit_) / 20) - 1;

	//TMP
	float distance_limit_ = 500;

	float distanceLength = distance_limit_ - total_distance;

	return FMath::Max(distanceLength, 0.0f);
}

FVector UnrealWifiSensor::Vector3rToFVector(const Vector3r& input_vector) {
	return FVector(input_vector.x(), input_vector.y(), -input_vector.z());
}

void UnrealWifiSensor::updateActiveBeacons() {
	beaconsActive_.Empty();
}