#pragma once
#include "core/camera_mode.hpp"
#include "objects.hpp"
namespace albert {
// All mount motion is integrated in site ENU metres, never in rebased float world coordinates.
// Changing rigs is a broadcast cut: no interpolated eye can fly through a tower or a hull.
struct BroadcastView {
    threepp::Vector3 eye,aim,up;
    float fov=45;
    bool fisheye=false;
    core::CameraMode shot=core::CameraMode::Tracking;
};
class BroadcastCamera {
public:
    void select(core::CameraMode mode){mode_=mode;reset();primary_.clear();booster_.clear();directorDone_=false;}
    core::CameraMode mode()const{return mode_;}
    core::CameraMode shot()const{return shot_;}
    void cloudLayer(float base,float top){cloudBase_=base;cloudTop_=top;}
    void reset(){ready_=false;subject_.clear();}
    std::optional<core::ObjectId> nextFocus(const core::Scene&,const SceneObjects&,const core::ObjectId&,double utc);
    std::optional<BroadcastView> update(const core::Scene&,const SceneObjects&,const core::ObjectId&,double utc,float seconds);
private:
    core::CameraMode mode_=core::CameraMode::Orbit,shot_=core::CameraMode::Tracking;
    std::string subject_,site_;std::uint64_t instance_=0;
    bool ready_=false,returning_=false,staged_=false;
    double utc_=0,stagingUntil_=0;float age_=0,fov_=45;
    float cloudBase_=0,cloudTop_=0;
    threepp::Vector3 eye_,aim_,previous_,travel_{1,0,0};
    std::string primary_,booster_;double directorUtc_=0,separatedAt_=0,caughtAt_=-1;bool directorDone_=false;
};
}
