#pragma once
#include "simulation.hpp"
#include "time.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace albert::core {
using ObjectId=std::string;
inline constexpr double earthRadiusMeters=6378137.;
struct Vector {double x=0,y=0,z=0;Vector operator+(Vector b) const{return {x+b.x,y+b.y,z+b.z};}Vector operator-(Vector b) const{return {x-b.x,y-b.y,z-b.z};}Vector operator*(double k) const{return {x*k,y*k,z*k};}};
struct Quaternion {double x=0,y=0,z=0,w=1;};
struct Sample {
    Sample(double utc=0,Vector position={},Vector velocity={},std::optional<Quaternion> rotation={},bool measuredVelocity=false):
        utcSeconds(utc),positionMeters(position),velocityMetersPerSecond(velocity),attitude(rotation),hasVelocity(measuredVelocity){}
    double utcSeconds=0; // Unix UTC seconds; not receipt time or TDB.
    Vector positionMeters,velocityMetersPerSecond;
    std::optional<Quaternion> attitude; // Local axes -> geocentric ICRF, XYZW.
    bool hasVelocity=false;
    // A vehicle's moving parts (Object::look), interpolated as the rest: per engine its thrust share (0: off) and gimbal pitch
    // and yaw (rad); per surface its deflection (rad); per thruster its duty; then the ambient pressure (Pa), the stagnation
    // heat flux (W/m²), the tanks' fill (0-1), the Mach number and the seconds since it broke up (negative: whole). Up to 1024.
    std::shared_ptr<const std::vector<float>> parts;
};
// How a vehicle looks beyond a plain shape, in metres along its stage (x aft of the nose, y right, z down, as the flight API's;
// drawn about the stage's middle): its body as frustums (black tiles on an arc centred on +z), parts on hinges turning by a
// surface's deflection (gain, offset; surface 1-based, 0: fixed), its engines' bells and plumes, its thrusters' jets.
struct LookSection {double from=0,to=0,radiusFrom=.5,radiusTo=.5;std::uint32_t color=0xd8d8d8;double tiles=0;};
struct LookPart {std::string name;Vector hinge,axis{1,0,0},outward{0,1,0};double span=1,width=1,thickness=.1,gain=1,offset=0;unsigned surface=0;std::uint32_t color=0x505050;bool grid=false;
    double tipSpan=-1,tipShift=0,tipThickness=-1;}; // The plate's outer edge: its length along the hinge and its centre's shift along it, its thickness there (negative: as at the hinge).
struct LookEngine {Vector position,direction{1,0,0};double exitRadius=.5;};
struct LookThruster {Vector position,direction{1,0,0};};
struct LookTank {double from=0,to=0;bool full=false;}; // Frosted below its liquid: filled from aft by the stage's fill, or always.
struct VehicleLook {double length=1,diameter=1;std::vector<LookSection> sections;std::vector<LookPart> parts;std::vector<LookEngine> engines;std::vector<LookThruster> thrusters;std::vector<LookTank> tanks;};
// A site's scenery (a launch pad's tower, mount and tanks), in metres east, north and up of its place, drawn only: boxes,
// round tanks (diameters size x by y, size z long, hollow inside `inner`) and trusses (a lattice box size z long, bars `bar`
// thick), each turned by `heading` (rad: its y toward it, clockwise from north) and tilted (rad: its z leaning from up toward
// its y), and turning with a hinged structure (1-based; 0: fixed); and the structures that carry the flight's physics as
// steel bars (radius 0: not drawn), a hinged one about its axis through its pivot by its angle (Sample::parts: one per
// structure).
struct SceneryPart {enum class Shape {Box,Cylinder,Truss} shape=Shape::Box;Vector centre,size{1,1,1};double heading=0,tilt=0,inner=0,bar=0;std::uint32_t color=0xb0b0b0;unsigned structure=0;};
struct SceneryBar {Vector from,to;double radius=.5;bool hinged=false;Vector pivot,axis{0,0,1};};
struct Scenery {std::vector<SceneryPart> parts;std::vector<SceneryBar> bars;};
// Fixed on a built-in body (the Earth): its place in metres in the body-fixed frame (ITRS) and its local axes there; its
// state then follows the body's frame (Scene::builtinFrame), its samples standing in until that is known.
struct Anchor {ObjectId body;Vector position;Quaternion attitude;};
enum class Shape {Builtin,Sphere,Box,Cylinder,Model};
struct Appearance {Shape shape=Shape::Sphere;Vector sizeMeters{1000,1000,1000};std::uint32_t color=0x77cddd;std::string modelId;double metersPerUnit=1;};
struct Object {
    ObjectId id;std::string name,source;
    Appearance appearance;
    double radiusMeters=500,defaultDistanceMeters=2000,minDistanceMeters=550;
    bool builtin=false,visible=true,providerManaged=false;std::uint64_t instance=0;
    std::vector<Sample> samples;
    std::shared_ptr<const VehicleLook> look; // A vehicle's, drawn instead of the shape (with Sample::parts).
    std::shared_ptr<const Scenery> scenery; // Drawn instead of the shape.
    std::optional<Anchor> anchor;
};
struct Evaluated {Sample state;bool stale=false;};
struct CameraState {ObjectId target="earth";Vector positionMeters;double distanceMeters=0;bool transitioning=false;};
struct Model;
struct ModelInfo {std::string id,source;std::size_t bytes=0,vertices=0,triangles=0;Vector size;double radius=0;};
enum class LayerKind {Points,Polyline};
enum class LayerFrame {GeocentricICRF,ObjectLocal,EarthSurface};
struct Geographic {double longitude=0,latitude=0;};
struct LayerData {std::vector<Vector> positions;};
struct Layer {
    std::string id,name,source,anchor;LayerKind kind=LayerKind::Points;LayerFrame frame=LayerFrame::GeocentricICRF;
    std::uint32_t color=0x66d9ee;float widthPixels=6;double clearanceMeters=100;
    bool visible=true;std::uint64_t instance=0;std::shared_ptr<const LayerData> data;
};
struct LayerInfo {std::string id,name,source,anchor;LayerKind kind;LayerFrame frame;std::size_t points=0;std::uint32_t color;float widthPixels;double clearanceMeters;bool visible;};
// utcSeconds is the simulated instant of the object states; clock is the disciplined
// real clock, which equals it only while the simulation is live.
struct Snapshot {std::uint64_t revision=0;double utcSeconds=0;std::vector<Object> objects;CameraState camera;time::Status clock;std::vector<ModelInfo> models;std::vector<LayerInfo> layers;time::Simulated simulation;bool approximatePositions=false;};
struct NaturalBody;struct LocalOrbit;
class Scene {
public:
    static constexpr size_t maxObjects=2048,maxSamples=256;
    Scene();
    static constexpr std::size_t maxLayers=64,maxLayerInputPoints=2048,maxLayerPoints=16384,maxSceneLayerPoints=32768;
    void create(Object object,bool replace=false); // replace: an existing object's definition, keeping its id and anchored layers.
    void createLayer(Layer);
    void removeLayer(const std::string& id);
    void layerVisible(const std::string& id,bool visible);
    const std::map<std::string,Layer>& layers()const{return layers_;}
    void installModel(std::shared_ptr<const Model>);
    void removeModel(const std::string& id);
    const std::map<std::string,std::shared_ptr<const Model>>& models()const{return models_;}
    void include(const NaturalBody& body);
    void include(const LocalOrbit& body);
    void remove(const ObjectId& id);
    void visible(const ObjectId& id,bool enabled);
    void samples(const ObjectId& id,std::vector<Sample> values);
    void replace(const ObjectId& id,std::vector<Sample> values);
    void builtinState(const ObjectId& id,Sample value);
    // A built-in body's orientation at an instant (celestial to body-fixed, row-major) and its spin about its fixed z (rad/s):
    // where the objects anchored on it are, near that instant.
    void builtinFrame(const ObjectId& id,double utcSeconds,const std::array<double,9>& celestialToFixed,double spin);
    const Object& get(const ObjectId& id) const;
    const std::map<ObjectId,Object>& objects() const{return objects_;}
    Evaluated evaluate(const ObjectId& id,double utcSeconds) const;
    Snapshot snapshot(double utcSeconds,const CameraState& camera) const;
    void cameraChanged(){++revision_;}
    std::uint64_t revision() const{return revision_;}
private:
    std::map<ObjectId,Object> objects_;
    std::map<std::string,std::shared_ptr<const Model>> models_;
    std::map<std::string,Layer> layers_;
    struct Frame {double utc=0;std::array<double,9> m{};double spin=0;};std::map<ObjectId,Frame> frames_;
    std::uint64_t revision_=0;
};
bool validObjectId(std::string_view id);
void validate(Sample& sample);
}
