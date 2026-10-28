#pragma once
// A Starship-class mission over the flight API, in lockstep:
// - the stack and then the ship by the generic ascent program (gravity_turn.hpp);
// - the booster back to the tower by BoosterReturn (starship.hpp);
// - about a day later, the ship home to the same tower by ShipReturn (ship_return.hpp).
#include "countdown.hpp"
#include "flight_api.hpp"
#include "navigation.hpp"
#include "ship_return.hpp"
#include "starbase_osm.hpp"
#include "starbase_production.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <numbers>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
namespace albert::example::starship {
// The pad at Starbase: Pad 1 as rebuilt for V3 (a flame trench and a mount on legs over it, as Pad 2's), its tower (Tower 1, where
// OpenStreetMap has it) 22 m west of the mount; the ground's height there (WGS 84; ~1 m over the mean sea by the Copernicus relief it is
// drawn from, whose geoid is 25.1 m under the ellipsoid: the site stood 29 m over its drawn ground), the arms' height over it, the bars' radius; the mount's deck the stack
// stands on (its base over the ground: the Raptors seen from below, the trench under it); launches toward `heading` (degrees from north).
constexpr double towerLatitude=25.996128,towerLongitude=-97.154517,towerGround=-24,armHeight=100,armRadius=.25,armLip=.2,deck=12,heading=93;
// The ascent as Flights 12-14 (V3, 2026) flew it, by their webcast telemetry (build/voos-reais): the 33 at 90% off the pad
// (~1.45 g) to MECO, a pitch-over of kickAngle (rad) eased in at 0.6°/s.
inline double stackThrottle=.9,kickAngle=.2,gLimit=2.2*9.80665,mecoSpeed=1700; // mecoSpeed: over the ground at cutoff (m/s; Flights 12-14: ~6,000 km/h).
// Their flight path angle by speed over the ground, from those flights' altitude and speed (smoothed over 20 s; Flights 13
// and 14 alike within ~2°): the pitch program the stack flies after its kick.
inline std::vector<std::pair<double,double>> flownPitch(){const double d=std::numbers::pi/180;
    return {{60,85*d},{150,66*d},{300,57*d},{500,47*d},{780,36*d},{1150,31*d},{1450,27*d},{1700,23*d}};}
inline Vec towerSite(){ // WGS 84 (ECEF, m).
    const double a=6378137,e2=6.69437999014e-3,phi=towerLatitude*deg,lam=towerLongitude*deg,n=a/std::sqrt(1-e2*std::sin(phi)*std::sin(phi));
    return{(n+towerGround)*std::cos(phi)*std::cos(lam),(n+towerGround)*std::cos(phi)*std::sin(lam),(n*(1-e2)+towerGround)*std::sin(phi)};
}
// The tower's crown (its column 22 m west of the catch's axis, 146 m up): the anemometer and the radio beacon. towerNed: the
// site's north, east and down rows (ECEF).
inline Mat towerNed(){const double phi=towerLatitude*deg,lam=towerLongitude*deg;
    return{{-std::sin(phi)*std::cos(lam),-std::sin(phi)*std::sin(lam),std::cos(phi)},{-std::sin(lam),std::cos(lam),0},{-std::cos(phi)*std::cos(lam),-std::cos(phi)*std::sin(lam),-std::sin(phi)}};}
constexpr double crownEast=-22,crownHeight=146;
inline Vec towerCrown(){const Mat n=towerNed();return towerSite()+n.r1*crownEast+n.r2*-crownHeight;}
inline double pinRest(){return std::sqrt(armRadius*armRadius-armLip*armLip);} // The pins over the bars' axes in the groove (m).
// The arms hinge on the carriage at the tower's east face (east of the tower's centre at towerX, its half width half) and swing
// at 8°/s, open 30° apart from the closed catch (the north one to the north) while a vehicle goes up or comes down past them.
constexpr double towerX=-22,half=4.75,armPivot=towerX+half+1.5,armSwing=8*deg,armOpen=30*deg;
// Starbase around the tower as it looks (drawn only; its places and sizes approximate, from photographs): the tower, a 9.5 m
// lattice 146 m tall west of the mount, its carriage and catch arms (the rails above are the structures), the ship's quick-
// disconnect arm swung back, the launch mount (a ring the stack stands on, its clamps), the concrete pad, the tank farm
// (upright and lying tanks) and its propellant line.
inline void addStarbase(api::CreateFlightRequest& create){
    using Shape=api::SceneryPart;constexpr std::uint32_t lattice=0x6b6e71,galvanised=0xa3abb0,dark=0x46484b,armSteel=0x252b30,concrete=0x9d9a94,apron=0x7f7d79,wall=0x8a8884,white=0xe4e4e2,tankSteel=0xb9bdc0,wet=0x343c40;
    auto part=[&](const char* name,Shape::Shape shape,Vec at,Vec size,std::uint32_t colour,double heading=0,double tilt=0,double inner=0){auto* x=create.add_scenery();
        x->set_name(name);x->set_shape(shape);put(x->mutable_centre_enu_meters(),at);put(x->mutable_size_meters(),size);x->set_color(colour);x->set_heading_degrees(heading);
        x->set_tilt_degrees(tilt);x->set_inner_diameter_meters(inner);};
    for(const auto& row:starbaseRoads){auto* road=create.add_roads();road->set_osm_way(row.id);road->set_width_meters(row.width);
        road->set_surface(static_cast<api::SceneryRoad::Surface>(row.surface));road->set_highway(row.highway);road->set_stop_start(row.stopStart);road->set_stop_end(row.stopEnd);
        for(unsigned i=0;i<row.count;++i){const auto& p=starbaseRoadPoints[row.first+i];put(road->add_points_enu_meters(),{p.x,p.y,0});}}
    for(const auto& row:productionRoads){if(std::any_of(std::begin(starbaseRoads),std::end(starbaseRoads),[&](const auto& r){return r.id==row.id;}))continue;
        auto* road=create.add_roads();road->set_osm_way(row.id);road->set_width_meters(row.width);road->set_surface(static_cast<api::SceneryRoad::Surface>(row.surface));road->set_highway(row.highway);
        for(unsigned i=0;i<row.count;++i){const auto& p=productionRoadPoints[row.first+i];put(road->add_points_enu_meters(),{p.x,p.y,0});}}
    for(const auto& row:productionBuildings){auto* b=create.add_buildings();b->set_name(row.name);b->set_osm_way(row.id);b->set_style(static_cast<api::SceneryBuilding::Style>(row.style));
        put(b->mutable_centre_enu_meters(),{row.x,row.y,0});put(b->mutable_size_meters(),{row.width,row.length,row.height});b->set_heading_degrees(row.heading);
        for(unsigned j=0;j<row.count;++j){auto* ring=b->add_rings();const auto& outline=productionRings[row.first+j];
            for(unsigned i=0;i<outline.count;++i){const auto& p=productionPoints[outline.first+i];put(ring->add_points_enu_meters(),{p.x,p.y,0});}}}
    constexpr double beam=3.2,depth=4;
    const double trench=33,s=std::sin(trench*deg),c=std::cos(trench*deg);
    auto place=[&](double across,double along,double z){return Vec{across*c+along*s,-across*s+along*c,z};}; // Keep the exhaust axis clear of the tower.
    auto pipe=[&](const char* name,Vec a,Vec b,double diameter){const Vec d=b+a*-1.;part(name,Shape::CYLINDER,(a+b)*.5,{diameter,diameter,norm(d)},tankSteel,std::atan2(d.x,d.y)/deg,std::atan2(std::hypot(d.x,d.y),d.z)/deg);};
    auto onPart=[&](unsigned on){if(on)create.mutable_scenery(create.scenery_size()-1)->set_structure(on);};
    auto bar=[&](const char* name,Vec a,Vec b,double width,std::uint32_t colour,unsigned on=0){const Vec d=b+a*-1.;part(name,Shape::BOX,(a+b)*.5,{width,width,norm(d)},colour,std::atan2(d.x,d.y)/deg,std::atan2(std::hypot(d.x,d.y),d.z)/deg);onPart(on);};
    auto guard=[&](Vec a,Vec b,int bays,unsigned on=0,double inset=0){const Vec d=b+a*-1.;for(const double z:{.55,1.1})bar("safety rail",a+Vec{0,0,z},b+Vec{0,0,z},.065,white,on);
        for(int i=0;i<=bays;++i){const Vec p=a+d*(double(i)/bays+(i==0?inset:i==bays?-inset:0)/norm(d));bar("railing post",p+Vec{0,0,-.03},p+Vec{0,0,1.13},.125,dark,on);}};
    auto tower=[&](Vec centre,double bearing,double height,bool service=true){const double sn=std::sin(bearing*deg),cs=std::cos(bearing*deg),section=height/16;
        auto p=[&](double x,double y,double z){return centre+Vec{x*cs+y*sn,-x*sn+y*cs,z};};
        part("tower concrete plinth",Shape::BOX,p(0,0,.78),{13.8,13.8,1.5},concrete,bearing);
        if(service){part("tower service building",Shape::BOX,p(-10,-5,3.7),{5.5,9,5.5},concrete,bearing);
        part("service building roof",Shape::BOX,p(-10,-5,6.53),{5.8,9.3,.18},tankSteel,bearing);
        for(const double z:{2.4,4.9})for(int i=0;i<3;++i)part("service building window",Shape::BOX,p(-12.78,-7.5+i*2.5,z),{.08,1.5,.9},0x344e60,bearing);}
        // Render markers expand into merged steel, panel seams and fittings, never contact structures.
        part("launch tower lattice",Shape::TRUSS,p(0,0,height/2),{9.5,9.5,height},galvanised,bearing);
        part("tower cladding panels",Shape::BOX,p(0,0,height/6+.8),{9.74,9.74,height/3},0x858e95,bearing);
        part("tower crane crown",Shape::TRUSS,p(0,0,height),{9.5,9.5,4.5},armSteel,bearing);
        for(const Vec at:{p(-4.6,-4.6,height+4.8),p(4.6,4.6,height+4.8),p(0,0,height+17.5)})part("tower aviation beacon",Shape::CYLINDER,at,{.28,.28,.32},0xff2210);
        part("enclosed lift and stairs",Shape::BOX,p(-2.9,-3.2,height/2),{1.7,2.2,height-.06},0x85898c,bearing);
        for(const int level:{3,6,9,12,15,16}){const double z=level*section-.28;
            part("tower service landing",Shape::BOX,p(0,-3.65,z),{9.12,1.6,.18},dark,bearing);
            for(const double h:{.64,1.19})bar("landing handrail",p(-4.48,-4.39,z+h),p(4.48,-4.39,z+h),.065,white);
            for(const double x:{-4.48,-1.5,1.5,4.48})bar("landing rail post",p(x,-4.39,z+.06),p(x,-4.39,z+1.22),.125,dark);
            for(const double side:{-1.,1.})bar("landing return rail",p(side*4.48,-4.39,z+1.22),p(side*4.48,-2.9,z+1.22),.065,white);
            if(level==16)for(int face=0;face<3;++face){const bool end=face==0;const double side=face==1?-1.:1.;
                part("crown access deck",Shape::BOX,p(end?0:side*3.8,end?3.65:0,z-.03*(face+1)),end?Vec{9.06,1.6,.18}:Vec{1.6,5.76,.18},dark,bearing);
                guard(p(end?-4.48:side*4.39,end?4.39:-2.85,z+.06-.03*face),p(end?4.48:side*4.39,end?4.39:4.39,z+.06-.03*face),1,0,.2);}} // Landings sit within the original column footprint.
        for(int i=0;i<5;++i)pipe("tower service pipe",p(-1.6+i*.8,-3.92,.12),p(-1.6+i*.8,-3.92,height-.12),i<2?.55:i==2?.42:.3);
        for(const double side:{-1.,1.})part("tower cable tray",Shape::BOX,p(side*2.35,-4.5,height/2),{.45,.3,height-.12},dark,bearing);
        for(int z=10;z<height;z+=10)part("service bank clamp",Shape::BOX,p(0,-4.25,z),{5.3,.42,.16},tankSteel,bearing); // Shared clamp bars retain the whole service bank.
        for(const double y:{-3.05,3.05})part("carriage guide rail",Shape::BOX,p(4.42,y,height/2),{.62,.4,height-.08},dark,bearing);
        part("crown drawworks housing",Shape::BOX,p(1.1,0,height+2.6),{5.6,6,3},0x85898c,bearing);
        pipe("hoist drum",p(3.6,-2.7,height+2.3),p(3.6,2.7,height+2.3),1.3);
        for(const double y:{-2.65,2.65})pipe("carriage hoist cable",p(4.04,y,armHeight+1.2),p(4.04,y,height+2.3),.09);
        for(const double z:{height*.38,height*.76,height+3.8}){bar("tower camera boom",p(-4.2,3.7,z),p(-6.3,4.5,z+.6),.16,dark);
            part("tower camera",Shape::BOX,p(-6.3,4.5,z+.6),{.4,.75,.35},white,bearing+145,20);
            part("tower floodlight back",Shape::BOX,p(-4.05,4.6,z+.55),{1.4,.3,.75},dark,bearing,20);
            part("tower floodlight lens",Shape::BOX,p(-4.05,4.78,z+.6),{1.3,.06,.65},0xfff4d2,bearing,20);}
    };
    auto armDetail=[&](Vec root,Vec axis,Vec across,double length,double side,unsigned on,double railFrom,double railTo){
        auto p=[&](double along,double out,double z){return root+axis*along+across*out+Vec{0,0,z};};
        guard(p(.25,side*1.3,0),p(length-.25,side*.55,0),4,on); // Keep the walkway outboard of the catch groove.
        for(const double out:{-.85,.85}){const Vec a=p(.2,out,-2.25),b=p(5.1,out,-.85),d=b+a*-1.;pipe("catch hinge actuator barrel",a,a+d*.52,.65);onPart(on);pipe("catch hinge actuator rod",a+d*.51,b,.25);onPart(on);}
        part("catch inner rail bed",Shape::BOX,p((railFrom+railTo)/2,-side*.8,-.48),{.7,.38,railTo-railFrom},armSteel,std::atan2(axis.x,axis.y)/deg,90);onPart(on);
        if(!on)for(const double lip:{-armLip,armLip})pipe("tower 2 catch rail",p(railFrom,-side*.8+lip,armRadius),p(railTo,-side*.8+lip,armRadius),2*armRadius); // Tower 1's rails are drawn from its unchanged contact bars.
        for(const double along:{railFrom,railTo}){part("catch rail shock bumper",Shape::CYLINDER,p(along,-side*.8,.38),{.75,.75,1.1},wet);onPart(on);}
    };
    auto flex=[&](Vec a,Vec b,double diameter,double sag,unsigned on){auto p=[&](double t){return a+(b+a*-1.)*t+Vec{0,0,-sag*std::sin(t*std::numbers::pi)};};
        for(int i=0;i<4;++i){const Vec from=p(i*.25),to=p((i+1)*.25),d=to+from*-1.;pipe("QD flexible hose",from,to,diameter);create.mutable_scenery(create.scenery_size()-1)->set_color(wet);onPart(on);
            part("QD braided sleeve ferrule",Shape::CYLINDER,(from+to)*.5,{diameter+.06,diameter+.06,norm(d)-.06},tankSteel,std::atan2(d.x,d.y)/deg,std::atan2(std::hypot(d.x,d.y),d.z)/deg,diameter-.03);onPart(on);
            if(i==1||i==2){part("QD diagonal braid binding",Shape::CYLINDER,(from+to)*.5,{diameter+.12,diameter+.12,.08},dark,std::atan2(d.x,d.y)/deg,std::atan2(std::hypot(d.x,d.y),d.z)/deg+(i==1?18:-18),diameter+.04);onPart(on);}}}; // Opposed bindings break the silver sleeve into a woven flex run.
    tower({towerX,0,0},0,146);
    part("catch carriage frame",Shape::BOX,{towerX,0,armHeight-armRadius-depth/2},{2*half+3.04,2*half+3,depth+3},armSteel);
    for(const double side:{-1.,1.}){const double y=side*(pinOut+.8);const double from=armPivot,to=13;const unsigned on=side>0?3:1; // The catch arms, under their rails.
        part("tapered catch truss",Shape::TRUSS,{(from+to)/2,y,armHeight-armRadius-(depth+.33)/2},{beam-.1,depth-.33,to-from},armSteel,90,90);onPart(on);
        part("catch arm walkway",Shape::BOX,{(from+to)/2,y,armHeight-armRadius-.15},{beam,.3,to-from},armSteel,90,90);onPart(on);
        armDetail({from,y,armHeight-armRadius},{1,0,0},{0,1,0},to-from,side,on,-10-from,10-from);}
    auto shipArm=[&](Vec shipRoot,Vec shipAxis,Vec shipAcross,double shipReach,unsigned on){const double bearing=std::atan2(shipAxis.x,shipAxis.y)/deg;
        auto shipPart=[&](){create.mutable_scenery(create.scenery_size()-1)->set_structure(on);};const double girderEnd=shipReach-3.5;
        part("ship QD truss",Shape::TRUSS,shipRoot+shipAxis*((girderEnd+.03)/2),{3,3,girderEnd-.03},armSteel,bearing,90);shipPart();
        part("ship arm hinge",Shape::CYLINDER,shipRoot,{2.5,2.5,3.4},dark);shipPart();
        pipe("ship arm brace",shipRoot+Vec{0,0,-6},shipRoot+shipAxis*(girderEnd*.72)+Vec{0,0,-1.47},.8);shipPart();
        part("ship QD head",Shape::BOX,shipRoot+shipAxis*(shipReach-2.28),{3.5,2,2.5},0x777c80,bearing,90);shipPart(); // Overlap girder 3 cm; clear plate 3 cm.
        part("ship QD head pivot",Shape::CYLINDER,shipRoot+shipAxis*(girderEnd+.37),{1.05,1.05,2.46},dark);shipPart();
        part("ship QD plate",Shape::BOX,shipRoot+shipAxis*shipReach,{3,3,2},dark,bearing,90);shipPart(); // Preserve hull contact.
        for(int i=-1;i<=1;++i){const Vec offset=shipAcross*2.02+Vec{0,0,i*.75},bend=shipRoot+shipAxis*(girderEnd-.4)+offset;
            pipe("ship QD hose",shipRoot+shipAxis*.6+offset,bend,.6);create.mutable_scenery(create.scenery_size()-1)->set_color(wet);shipPart();
            pipe("ship QD hose droop",bend,shipRoot+shipAxis*(shipReach-2.28)+offset+Vec{0,0,-.25},.6);create.mutable_scenery(create.scenery_size()-1)->set_color(wet);shipPart();} // Hoses enter the head's side.
        for(int i=0;i<3;++i)flex(shipRoot+shipAxis*(shipReach-2.28)+shipAcross*-1.72+Vec{0,0,.65-i*.6},shipRoot+shipAcross*(-2.05-i*.48)+Vec{0,0,-4.8},i==2?.22:.38,1.8,on);
        part("SQD tower valve cabinet",Shape::BOX,shipRoot+shipAcross*-2.65+Vec{0,0,-5.15},{2.1,1.5,1.6},tankSteel,bearing);shipPart(); // The cabinet and hose tails follow the arm hinge.
    };
    const Vec shipRoot{towerX+half,half,deck+78},shipAxis{1/std::sqrt(2.),1/std::sqrt(2.),0},shipAcross{shipAxis.y,-shipAxis.x,0};
    shipArm(shipRoot,shipAxis,shipAcross,std::hypot(shipRoot.x,shipRoot.y)-5.56,8); // Preserve the existing QD plate and hinge positions.
    auto roadDistance=[&](Vec q,const auto& road){double best=1e9;for(unsigned i=1;i<road.count;++i){const auto& a=starbaseRoadPoints[road.first+i-1];const auto& b=starbaseRoadPoints[road.first+i];const Vec d{b.x-a.x,b.y-a.y,0},v=q+Vec{-a.x,-a.y,0};const double t=std::clamp((v.x*d.x+v.y*d.y)/(d.x*d.x+d.y*d.y),0.,1.);best=std::min(best,norm(v+d*(-t)));}return best;};
    auto groundPart=[&](const char* name,Vec at,Vec size,std::uint32_t colour,double bearing){const double radius=std::hypot(size.x,size.y)/2+.4;
        for(const auto& road:starbaseRoads)if(roadDistance(at,road)<road.width/2+radius)return; // Keep entrances and service lanes open, including Pad 2's diagonal access.
        part(name,Shape::BOX,at,size,colour,bearing);};
    auto pad=[&](Vec centre,double bearing,Vec qd,unsigned on=0){const double s=std::sin(bearing*deg),c=std::cos(bearing*deg);
        auto place=[&](double across,double along,double z){return centre+Vec{across*c+along*s,-across*s+along*c,z};};
        part("west bunker apron",Shape::BOX,place(-39.515,-42.5,.47),{60.97,215,1},apron,bearing);
        part("east bunker apron",Shape::BOX,place(44.515,-42.5,.47),{70.97,215,1},apron,bearing);
        part("south exhaust apron",Shape::BOX,place(0,-96,.11),{18.12,108,.28},apron,bearing);
        part("north exhaust apron",Shape::BOX,place(0,53.5,.11),{18.12,23,.28},apron,bearing); // Lower the exits so neither end is blocked.
        part("trench floor",Shape::BOX,place(0,0,.15),{18.08,84,.3},wet,bearing);
    for(const double side:{-1.,1.}){
        part("trench wall",Shape::BOX,place(side*9.75,0,3.13),{1.5,84,5.74},wall,bearing);
        part("steel wall facing",Shape::BOX,place(side*9.02,0,3.125),{.08,83.96,5.69},dark,bearing); // Sink the lining without sharing its top face.
        for(int i=0;i<3;++i)part("raised pad terrace",Shape::BOX,place(side*(12+3*i),0,2.975-.99*i),{3.06,84.06,5.99-1.98*i},wall,bearing); // Steps expose the bathtub above natural ground.
        const double drop=4.4;part("flame bucket ramp",Shape::BOX,place(0,side*21,2.6),{17.8,std::hypot(42.,drop)+.04,.35},concrete,side>0?bearing:bearing+180,std::atan2(drop,42.)/deg); // Split the plume below the hanging bells.
        for(const double along:{-8.,8.})part("mount leg",Shape::BOX,place(side*9.75,along,5.3),{5.8,5.8,9.5},concrete,bearing); // Drawn-only splayed piers reach the underside of the unchanged steel deck.
        part("mount deck side",Shape::BOX,place(side*8.2,0,deck-1.02),{5.6,22,2},lattice,bearing);
        part("mount deck end",Shape::BOX,place(0,side*8.2,deck-1.05),{10.84,5.6,2},lattice,bearing); // Stagger overlapping faces by 3 cm.
    }
        for(int row=-1;row<=1;++row)for(int i=-2;i<=2;++i)part("crest spray pipe",Shape::BOX,place(i*3.2,row*.45,5.02-std::abs(row)*.1),{.8,.3,.24},wet,bearing); // Suggest the bucket's water outlets.
        part("mount opening rim",Shape::CYLINDER,place(0,0,deck-1.05),{15.4,15.4,2.1},dark,0,0,10.8); // Fill the cutout's corners with offset faces.
        for(int i=0;i<20;++i){const double a=(i*18+bearing-trench)*deg;
            part("clamp",Shape::BOX,centre+Vec{5.13*std::sin(a),5.13*std::cos(a),deck+.23},{.8,1.2,.5},dark,i*18+bearing-trench);} // Overhang the opening to grip the base; their inner faces 3 cm off the hull (at 4.5 m they shared its surface and shimmered).
        const Vec across{qd.y,-qd.x,0};
        auto qdPoint=[&](double radius,double side,double z){return centre+qd*(on?6.5-(z-deck-1.2):radius)+across*side+Vec{0,0,on?deck+1.2+radius-6.5:z};}; // Undo the connected hinge pose for scenery.
        for(const double side:{-1.,1.}){part("booster QD truss hood",Shape::BOX,qdPoint(5.5,side*2.1,deck+1.75),on?Vec{3.5,3,3}:Vec{3,3,3.5},armSteel,std::atan2(qd.x,qd.y)/deg+90);
            if(on)create.mutable_scenery(create.scenery_size()-1)->set_structure(on);
            for(const double line:{-.45,.45}){pipe("booster QD umbilical",qdPoint(6.4,side*2.1+line,deck+.05),qdPoint(6.4,side*2.1+line,deck+2.6),.55);
                if(on)create.mutable_scenery(create.scenery_size()-1)->set_structure(on);}
            flex(qdPoint(5.7,side*2.65,deck+2.65),qdPoint(8.7,side*2.65,deck+.35),.44,.6,on);
            pipe("booster QD conduit bundle",qdPoint(5.8,side*3.05,deck+1.95),qdPoint(8.7,side*3.05,deck+.35),.18);onPart(on);
            part("booster QD valve cabinet",Shape::BOX,qdPoint(8.95,side*2.65,deck+.2),on?Vec{1.15,1.4,1.65}:Vec{1.65,1.4,1.15},tankSteel,std::atan2(qd.x,qd.y)/deg+90);onPart(on);} // Keep all hose tails on their folding hood.
        for(const double side:{-1.,1.}){
            for(const double end:{-1.,1.})guard(place(side*10.35,end*12,6.03),place(side*10.35,end*41.5,6.03),4);
            guard(place(side*10.65,-10.65,deck+.01),place(side*10.65,10.65,deck+.01),2,0,.2);
            guard(place(-10.65,side*10.65,deck+.04),place(10.65,side*10.65,deck+.04),2,0,.2);}
        for(const double x:{-48.,48.})for(const double y:{-38.,38.}){
            part("floodlight pole footing",Shape::BOX,place(x,y,.88),{2.4,2.4,.85},concrete,bearing);
            part("pad floodlight pole",Shape::CYLINDER,place(x,y,14),{.5,.5,26},dark);
            bar("floodlight crossarm",place(x-1.5,y,26.9),place(x+1.5,y,26.9),.18,dark);
            for(const double lamp:{-1.,1.})part("pad floodlight",Shape::BOX,place(x+lamp,y,26.75),{1.1,.4,.8},0xfff4d2,bearing+(y>0?180:0),28);}
        for(const double side:{-1.,1.}){part("covered cable trench",Shape::BOX,place(side*27,0,1.01),{.8,84,.14},dark,bearing);
            part("pad electrical junction",Shape::BOX,place(side*27,36,1.65),{1.5,1.1,1.4},tankSteel,bearing);} // Keep cable routes outside the terrace steps.
        // Furnish the outer apron, leaving both exhaust exits and the tank-farm access open.
        for(const double x:{-67.,77.})for(int i=0;i<11;++i)if(i!=4&&i!=5)groundPart("perimeter T wall",place(x,-136+i*16,2.07),{1.8,15.8,2.2},concrete,bearing);
        for(const double side:{-1.,1.})for(int i=0;i<5;++i)groundPart("perimeter T wall",place(side*(24+i*10),-147,2.07),{1.8,9.8,2.2},concrete,bearing+90);
        for(int row=0;row<2;++row)for(int i=0;i<5;++i)groundPart(i==4?"detail truck":"detail pickup",place(-53+row*11,-118+i*7.5,.97),i==4?Vec{2.72,7,2.1}:Vec{2.22,5.5,1.8},i%3==0?0x637b8a:white,bearing+90);
        for(int i=0;i<9;++i)groundPart("detail cone",place(-34,-126+i*5,.97),{.48,.48,.7},0xf37924,bearing);
        for(int i=0;i<4;++i){groundPart("detail skid",place(-42+i*6,47,.97),{2.5,4,1.5},tankSteel,bearing);
            groundPart("detail cable tray",place(-42+i*6,36,.97),{.9,8,.4},dark,bearing);}
    };
    pad({0,0,0},trench,{-1,0,0},9);
    for(const double side:{-1.,1.})part("steam vent",Shape::BOX,place(0,side*42,3),{18,1,6},wet,side>0?trench:trench+180); // Not drawn: where the deluge's steam leaves the trench.
    const OsmPart *pad2=nullptr,*tower2=nullptr;for(const auto& row:starbaseOsm){const std::string kind=row.kind;if(kind=="trench")pad2=&row;else if(kind=="tower")tower2=&row;}
    for(const auto& row:starbaseOsm){const std::string kind=row.kind,name=row.name;const Vec centre{row.x,row.y,0},axis{std::sin(row.heading*deg),std::cos(row.heading*deg),0};
        if(kind=="trench"){const Vec toward{tower2->x-row.x,tower2->y-row.y,0};pad(centre,row.heading,toward*(1/norm(toward)));}
        else if(kind=="tower"){const Vec toward{pad2->x-row.x,pad2->y-row.y,0},reach=toward*(1/norm(toward)),side{reach.y,-reach.x,0};const double bearing=std::atan2(reach.x,reach.y)/deg;
            tower(centre,row.heading,row.height,false); // Its OSM service building already occupies the foot of Tower 2.
            part("catch carriage frame",Shape::BOX,centre+Vec{0,0,armHeight-armRadius-depth/2},{12.54,12.5,depth+3},armSteel,row.heading);
            for(const double sign:{-1.,1.}){part("tapered catch truss",Shape::TRUSS,centre+reach*(half+1.5+12.5)+side*(sign*5)+Vec{0,0,armHeight-armRadius-(depth+.33)/2},{beam-.1,depth-.33,25},armSteel,bearing,90);
                part("catch arm walkway",Shape::BOX,centre+reach*(half+1.5+12.5)+side*(sign*5)+Vec{0,0,armHeight-armRadius-.15},{beam,.3,25},armSteel,bearing,90);
                armDetail(centre+reach*(half+1.5)+side*(sign*5)+Vec{0,0,armHeight-armRadius},reach,side,25,sign,0,3,23);}
            shipArm(centre+reach*half+side*half+Vec{0,0,deck+78},reach,side,13,0);
        }else if(kind=="building"){const bool vapor=name.find("Vaporizer")!=std::string::npos,bunker=name.find("bunker")!=std::string::npos||name.find("Bunker")!=std::string::npos;
            const std::string marker=vapor?"farm vaporizer":name=="Starhopper"?name:"farm building "+name;
            part(marker.c_str(),Shape::BOX,centre+Vec{0,0,name=="Starhopper"?row.height/2:0},{row.width,row.length,vapor?6:row.height},bunker?0x8a8780:0xa4a9ab,row.heading);
        }else if(kind=="tank"||kind=="lying tank"){const bool lying=kind=="lying tank",methane=name.find("methane")!=std::string::npos,oxygen=name.find("oxygen")!=std::string::npos,nitrogen=name.find("nitrogen")!=std::string::npos;
            const double diameter=lying?row.width:row.length,z=lying?diameter/2+.6:row.height/2;const std::uint32_t colour=nitrogen?tankSteel:name.find("water")!=std::string::npos?0xd8d6cf:oxygen||methane||!lying?white:tankSteel;
            part(lying?"farm horizontal tank":"farm vertical tank",Shape::BOX,centre,{diameter,lying?row.length:diameter,lying?diameter:row.height},colour,row.heading);
            if(oxygen||methane||nitrogen)part("tank vent",Shape::BOX,centre+axis*(lying?row.length/2-.7:0)+Vec{0,0,lying?z+diameter/2+.72:row.height+.72},{.6,.6,1.5},dark,row.heading);
        }
    }
    // The photo's long covered western bank. OSM tank centres remain untouched; the open sides expose the service ends.
    part("farm canopy",Shape::BOX,{-88.6,116.2,0},{98,54,10},0xc6cbcd,13.3);

    for(const Vec at:{Vec{-145,102,0},Vec{47,74,0},Vec{-113,181,0},Vec{-210,177,0}})
        part("farm light pole",Shape::BOX,at,{.4,.4,9},galvanised);
    auto services=[&](Vec centre,double bearing,Vec qd,Vec farm,bool ownWater){const double sn=std::sin(bearing*deg),cs=std::cos(bearing*deg);const Vec across{qd.y,-qd.x,0};
        auto p=[&](double x,double y,double z){return centre+Vec{x*cs+y*sn,-x*sn+y*cs,z};};
        const Vec foot=p(-25,0,0),riser=centre+qd*12.5,d1=foot+farm*-1.,d2=riser+foot*-1.,n1{d1.y/norm(d1),-d1.x/norm(d1),0},n2{d2.y/norm(d2),-d2.x/norm(d2),0},mitre=(n1+n2)*(1/(1+n1.x*n2.x+n1.y*n2.y));
        part("farm manifold",Shape::BOX,farm+d1*(-.8/norm(d1)),{5.5,2,2.2},tankSteel,std::atan2(d1.x,d1.y)/deg);
        auto rack=[&](Vec a,Vec b,Vec left,Vec right){const Vec d=b+a*-1.;part("farm pipe trestle",Shape::BOX,(a+b)*.5,{5,norm(d),2.6},lattice,std::atan2(d.x,d.y)/deg);
            for(int i=0;i<4;++i){const double diameter=i<2?1.:i==2?.3:.18,z=2.6+diameter/2,offset=-1.8+1.2*i;
                pipe(i==0?"vacuum jacket LOX":i==1?"vacuum jacket methane":i==2?"rack GN2":"rack helium",a+left*offset+Vec{0,0,z},b+right*offset+Vec{0,0,z},diameter);}};
        rack(farm,foot,n1,mitre);rack(foot,riser,mitre,n2); // Mitre the elbows so each lane stays separate around the bend.
        for(int i=0;i<4;++i){const double diameter=i<2?1.:i==2?.3:.18,offset=-1.8+1.2*i,out=i==0?-2.65:i==1?2.65:i==2?-3.05:3.05;
            const Vec low=riser+n2*offset+Vec{0,0,2.6+diameter/2},high=riser+n2*offset+Vec{0,0,deck+.25};
            pipe("mount service riser",low,high,diameter);pipe("mount valve header",high,centre+qd*8.95+across*out+Vec{0,0,deck+.25},diameter);}
        if(ownWater)for(const double side:{-1.,1.}){
            part("deluge water tank",Shape::CYLINDER,p(70,side*9,8.45),{10,10,15},white);
            pipe("deluge supply",p(70,side*9,1.55),p(14,side*9,1.55),1.2);pipe("deluge riser",p(14,side*9,1.55),p(14,side*9,6.3),1.2);
            pipe("wall water header",p(14,side*9,6.3),p(9.6,side*9,6.3),1.2);
        }else{const Vec water{-300.6-19.57*std::sin(15*deg),146.6-19.57*std::cos(15*deg),1.55};pipe("OSM water farm supply",water,p(14,9,1.55),1.2);
            pipe("pad 2 deluge riser",p(14,9,1.55),p(14,9,6.3),1.2);pipe("pad 2 water header",p(14,9,6.3),p(9.6,9,6.3),1.2);}
        for(const double side:{-1.,1.})pipe("trench deluge manifold",p(side*9.6,-40,6.3),p(side*9.6,40,6.3),.65);
        pipe("deluge cross feed",p(-9.6,9,6.3),p(9.6,9,6.3),.65);pipe("mount deluge riser",p(9.6,9,6.3),p(9.6,9,deck-2.2),.65);
        pipe("mount deluge feed",p(9.6,9,deck-2.2),p(6,6,deck-2.2),.65);
        part("mount deluge ring",Shape::CYLINDER,centre+Vec{0,0,deck-2.2},{16.6,16.6,.5},tankSteel,0,0,15.7); // Leave the engine opening clear beneath the deck.
    };
    services({0,0,0},trench,{-1,0,0},{-34,84,0},true);
    if(pad2&&tower2){const Vec toward{tower2->x-pad2->x,tower2->y-pad2->y,0};services({pad2->x,pad2->y,0},pad2->heading,toward*(1/norm(toward)),{-148,98,0},false);}
    // Launch-day parking follows the saved highway tangents, outside its full paved width and clear of junctions.
    int parked=0;
    for(const auto& road:starbaseRoads)if(road.highway)for(unsigned i=1;i<road.count&&parked<20;i+=2){const auto& a=starbaseRoadPoints[road.first+i-1];const auto& b=starbaseRoadPoints[road.first+i];Vec d{b.x-a.x,b.y-a.y,0};if(norm(d)<8)continue;d=d*(1/norm(d));
        for(const double side:{-1.,1.}){const Vec at=Vec{(a.x+b.x)*.5,(a.y+b.y)*.5,0}+Vec{d.y,-d.x,0}*(side*(road.width/2+1.7));bool clear=at.x>-500&&at.x<250&&at.y<220;
            for(const auto& other:starbaseRoads)if(roadDistance(at,other)<other.width/2+(other.highway?.9:8.))clear=false;
            for(const auto& row:starbaseOsm)if(norm(at+Vec{-row.x,-row.y,0})<std::hypot(row.length,row.width)/2+4)clear=false;
            for(int k=0;k<create.scenery_size();++k){const auto& p=create.scenery(k);if(!p.name().starts_with("detail "))continue;const auto& v=p.centre_enu_meters();if(std::hypot(at.x-v.x(),at.y-v.y())<9)clear=false;}
            if(clear){part("detail car",Shape::BOX,at,{2,4.5,1.8},parked%3==0?0x637b8a:white,std::atan2(d.x,d.y)/deg);++parked;}}}
}
// What a vehicle coming back must never strike or come down on, as its computer knows the site (TowerApproach's columns and
// works): the tower's column (its lattice's corners, the crown and the mast), the tank farm; the catch arms and the ship's
// arm as beams; the pad (the mount on it, the propellant line, the tank farm within it).
inline void siteWorks(TowerApproach& a){
    a.columns={{towerX,0,half*std::sqrt(2.),-5,164,"rota de colisão com a torre"},{0,112,75,0,12,"rota de colisão com o parque de tanques"},
        {-165,135,70,0,12,"rota de colisão com os prédios de apoio"}}; // The tank farm and the GSE buildings north of the pad (OpenStreetMap).
    a.works={{-90,90,-110,110},{-250,245,20,200}}; // The pad with its trench and aprons; the tank farm, GSE and bunkers north of it.
    for(const double side:{-1.,1.})a.arms.push_back({armPivot,side*(pinOut+.8),13-armPivot,1.5,armHeight-armRadius-2.1,armHeight+armRadius,0, // The beam, its rails above.
        side<0?"rota de colisão com o braço de captura sul":"rota de colisão com o braço de captura norte",side*armOpen,armSwing});
    a.arms.push_back({towerX+half,half,std::hypot(towerX+half,half)-5.1,1.5,deck+76,deck+80,45*deg,"rota de colisão com o braço da nave"}); // Returns clear the stowed truss.
}
// The arms' angles as their sensors read them (the south one's from its first rail, the north one's from its first), for the
// guidance's keep-out.
inline void armAngles(TowerApproach& a,const api::FlightSample& s){if(s.structure_radians_size()>=3&&a.arms.size()>=2){a.arms[0].angle=s.structure_radians(0);a.arms[1].angle=s.structure_radians(2);}}
// Each arm a rail of two steel bars 0.4 m apart (0.25 m round), east-west under a pin, shock-absorbed (ζ ~0.8 under the booster):
// the pin rests in the groove between them, the inner bar ~0.5 m off the hull; under it a beam (1 m round, ~1 m off the hull),
// both swinging with the arm. The
// structures: 1-2 the south rail, 3-4 the north one, 5 the south beam, 6 the north one (hinged, starting open), 7 the tower's
// column (fixed, round to its lattice's corners); the beams and the column drawn by the scenery.
inline void addTower(api::CreateFlightRequest& create,bool mounted=false){ // mounted: the stack on the mount, its quick disconnects connected (else stowed).
    create.mutable_site()->set_latitude_degrees(towerLatitude);create.mutable_site()->set_longitude_degrees(towerLongitude);create.mutable_site()->set_height_meters(towerGround);create.set_deck_height_meters(deck);
    auto add=[&](const char* name,Vec from,Vec to,double round,double side,bool drawn){auto* a=create.add_structures();a->set_name(name);put(a->mutable_start_enu_meters(),from);
        put(a->mutable_end_enu_meters(),to);a->set_radius_meters(round);a->set_stiffness_newtons_per_meter(1e8);a->set_damping_newton_seconds_per_meter(6e6);a->set_friction(.8);
        if(!drawn)a->set_drawn(false);if(side==0)return;
        put(a->mutable_hinge_enu_meters(),armPivot,side*(pinOut+.8),armHeight);a->set_swing_rate_radians_per_second(armSwing);
        a->set_min_radians(side>0?0:-armOpen);a->set_max_radians(side>0?armOpen:0);a->set_start_radians(side*armOpen);};
    for(const double side:{-1.,1.})for(const double lip:{-armLip,armLip}){add("arm rail",{-10,side*pinOut+lip,armHeight},{10,side*pinOut+lip,armHeight},armRadius,side,true);
        auto* r=create.mutable_structures(create.structures_size()-1);r->set_stiffness_newtons_per_meter(4e8);r->set_damping_newton_seconds_per_meter(1.2e7);} // Stiff enough that a pin bearing a whole vehicle stays over the bars' axes (at 1e8 it sank between them and through), ζ kept.
    for(const double side:{-1.,1.})add("arm beam",{armPivot,side*(pinOut+1),armHeight-armRadius-1.1},{13,side*(pinOut+1),armHeight-armRadius-1.1},1,side,false);
    add("tower",{towerX,0,-5},{towerX,0,150},half*std::sqrt(2.),0,false);
    const Vec root{towerX+half,half,deck+78};const double reach=std::hypot(root.x,root.y)-5.1,connected=std::atan2(-root.y,-root.x)-45*deg;
    add("ship quick disconnect",root,root+Vec{1,1,0}*(reach/std::sqrt(2.)),.1,0,false);auto* sqd=create.mutable_structures(create.structures_size()-1);
    put(sqd->mutable_hinge_enu_meters(),root);sqd->set_swing_rate_radians_per_second(-connected/3);sqd->set_min_radians(connected);sqd->set_max_radians(0);sqd->set_start_radians(mounted?connected:0); // Stowed is zero so catch feedback ignores the retracted QDs.
    add("booster quick disconnects",{-6.5,-2.1,deck-.3},{-6.5,2.1,deck-.3},.1,0,false);auto* bqd=create.mutable_structures(create.structures_size()-1);
    put(bqd->mutable_hinge_enu_meters(),-6.5,0,deck+1.2);put(bqd->mutable_hinge_axis(),0,1,0);bqd->set_swing_rate_radians_per_second(90*deg);bqd->set_min_radians(-90*deg);bqd->set_max_radians(0);bqd->set_start_radians(mounted?-90*deg:0); // Fold down 1.5 m and away 1.5 m as the clamps let go.
    addStarbase(create);
}
// The vehicles' avionics: three navigation-grade IMUs, a space GNSS receiver with RTK from a base station at the site (to 30 km),
// a star tracker, the tanks' gauges (the flight software navigates on them: navigation.hpp).
inline void avionics(api::CreateFlightRequest& create,std::uint64_t seed=1){
    create.mutable_imu()->set_grade(api::ImuSettings::NAVIGATION);create.mutable_imu()->set_seed(seed);create.mutable_imu()->set_count(3);auto* q=create.mutable_sensors();q->set_seed(seed*7919+1);
    q->mutable_gnss()->set_enabled(true);q->mutable_gnss()->set_rtk_range_meters(30e3);q->mutable_star_tracker()->set_enabled(true);q->mutable_propellant_gauge()->set_enabled(true);
    q->mutable_anemometer()->set_enabled(true);q->mutable_anemometer()->set_east_meters(crownEast);q->mutable_anemometer()->set_height_meters(crownHeight);
    q->mutable_beacon()->set_enabled(true);q->mutable_beacon()->set_east_meters(crownEast);q->mutable_beacon()->set_height_meters(crownHeight);
}
// The truth from a sample (for the test's checks and the alignment, never the guidance): ECEF state, body rates and the
// attitude as ECEF to body (Bᵀ N, N the geodetic NED).
inline State state(const api::FlightSample& s){
    State x;x.position=vec(s.position_ecef_meters());x.velocity=vec(s.velocity_ecef_meters_per_second());x.rate=vec(s.angular_rate_radians_per_second());
    x.contact=vec(s.contact_force_newtons());x.mass=s.mass_kilograms();x.propellant=s.propellant_kilograms();x.thrust=s.thrust_newtons();x.height=s.geodetic().height_meters();x.q=s.dynamic_pressure_pascals();
    x.specific=vec(s.specific_force_meters_per_second2());for(const auto& f:s.surfaces())x.surfaces.push_back(f.deflection_radians());
    for(const auto& e:s.engines())x.failed.push_back(e.failed());
    const double la=s.geodetic().latitude_degrees()*deg,lo=s.geodetic().longitude_degrees()*deg;
    const Mat b=bodyToNed(s.attitude_ned().w(),s.attitude_ned().x(),s.attitude_ned().y(),s.attitude_ned().z());
    const Mat n{{-std::sin(la)*std::cos(lo),-std::sin(la)*std::sin(lo),std::cos(la)},{-std::sin(lo),std::cos(lo),0},{-std::cos(la)*std::cos(lo),-std::cos(la)*std::sin(lo),-std::sin(la)}};
    x.toBody={n.r0*b.r0.x+n.r1*b.r1.x+n.r2*b.r2.x,n.r0*b.r0.y+n.r1*b.r1.y+n.r2*b.r2.y,n.r0*b.r0.z+n.r1*b.r1.z+n.r2*b.r2.z};
    return x;
}
// The catch arms open (a vehicle going up or coming down past them) or closed (catching it), by a step's commands.
inline void arms(api::StepRequest& q,bool open){ // Structures 1, 2 and 5 the south arm's, 3, 4 and 6 the north one's.
    for(unsigned i=1;i<=6;++i){auto* k=q.add_commands();k->set_index(i);k->set_structure_radians(open?(i<=2||i==5?-armOpen:armOpen):0.);}
}
inline bool armsClosed(const api::FlightSample& s){ // The arms' six (arms()), not the quick disconnects after them: those rest stowed or connected whatever the arms do.
    return s.structure_radians_size()>=6&&std::all_of(s.structure_radians().begin(),s.structure_radians().begin()+6,[](double a){return std::abs(a)<.2*deg;});}
inline void apply(api::StepRequest& q,const Command& c){ // A guidance command as flight commands (indices from 1).
    if(c.terminate){q.add_commands()->set_terminate(true);return;} // FTS must not depend on an engine command being accepted.
    for(int i:c.ignite){auto* k=q.add_commands();k->set_ignite(true);k->set_index(static_cast<unsigned>(i));}
    for(int i:c.cutoff){auto* k=q.add_commands();k->set_cutoff(true);k->set_index(static_cast<unsigned>(i));}
    if(c.throttle>=0)q.add_commands()->set_throttle(c.throttle);
    if(c.vent>=0)q.add_commands()->set_vent_kilograms_per_second(c.vent);
    auto* g=q.add_commands()->mutable_gimbal();g->set_pitch_radians(c.gimbalPitch);g->set_yaw_radians(c.gimbalYaw);
    for(const auto& e:c.gimbals){auto* k=q.add_commands();k->set_index(static_cast<unsigned>(e.engine));k->mutable_gimbal()->set_pitch_radians(e.pitch);k->mutable_gimbal()->set_yaw_radians(e.yaw);}
    for(std::size_t i=0;i<c.surfaces.size();++i){auto* k=q.add_commands();k->set_surface_radians(c.surfaces[i]);k->set_index(static_cast<unsigned>(i+1));}
    if(c.torqueOn){auto* t=q.add_commands()->mutable_torque_newton_meters();t->set_x(c.torque.x);t->set_y(c.torque.y);t->set_z(c.torque.z);}
}
// Where a vehicle is over the ground from the tower's base: east and north (m).
inline std::pair<double,double> aside(const api::FlightSample& s){
    const double la=towerLatitude*deg,lo=towerLongitude*deg;const Vec d=vec(s.position_ecef_meters())+towerSite()*-1.;
    return{dot(d,Vec{-std::sin(lo),std::cos(lo),0}),dot(d,Vec{-std::sin(la)*std::cos(lo),-std::sin(la)*std::sin(lo),std::cos(la)})};
}
// How far a vehicle (upright, `length` m, 4.5 m round) keeps from the tower's lattice while its lowest end is below the
// tower's top: its axis to the tower's centre less both half-widths (m; huge when above it).
inline double clearance(const api::FlightSample& s,double length){
    if(s.geodetic().height_meters()-towerGround-length/2>150)return 1e9;
    const auto [east,north]=aside(s);return std::hypot(east-towerX,north)-radius-half*std::sqrt(2.);
}
inline double offAxis(const api::FlightSample& s,Vec up){const Vec rel=vec(s.position_ecef_meters())+towerSite()*-1.;return norm(rel+up*-dot(rel,up));}
inline double speedOf(const api::FlightSample& s){const auto& v=s.velocity_ned_meters_per_second();return std::hypot(v.x(),v.y(),v.z());}
// ALBERT_CSV=path: a mission's profile once a second, for comparing with real flights' webcast telemetry: T (s), the vehicle,
// height (km), speed over the ground (km/h) and inertial (m/s), climb (m/s), downrange from the tower (km, great circle),
// propellant and mass (t), engines lit, throttle, q (kPa), phase.
inline void profile(std::FILE* f,double t,const char* who,const api::FlightSample& s,const char* phase){
    if(!f)return;const auto& v=s.velocity_ned_meters_per_second();const auto& i=s.velocity_meters_per_second();const auto& g=s.geodetic();int lit=0;for(const auto& e:s.engines())lit+=e.on();
    const double la=g.latitude_degrees()*deg,la0=towerLatitude*deg,a=std::pow(std::sin((la-la0)/2),2)+std::cos(la)*std::cos(la0)*std::pow(std::sin((g.longitude_degrees()-towerLongitude)*deg/2),2);
    std::fprintf(f,"%.1f,%s,%.3f,%.0f,%.1f,%.1f,%.2f,%.1f,%.1f,%d,%.2f,%.2f,%s\n",t,who,g.height_meters()/1e3,std::hypot(v.x(),v.y(),v.z())*3.6,std::hypot(i.x(),i.y(),i.z()),-v.z(),
        2*6371.0*std::asin(std::sqrt(a)),s.propellant_kilograms()/1e3,s.mass_kilograms()/1e3,lit,s.throttle(),s.dynamic_pressure_pascals()/1e3,phase);
}
// Real time with a warp: each step's simulated seconds take 1/rate of them on the wall clock (no waiting when fast).
struct Pacer {
    bool fast=false;std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();double wall=0,last=0;
    double hurry=1; // --landings: every phase but the landings this many times faster (1: as they happen).
    void operator()(double sim,double rate){if(fast){last=sim;return;}wall+=(sim-last)/rate;last=sim;paceSim=sim;
        const auto due=start+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(wall)),now=std::chrono::steady_clock::now();
        static const bool log=std::getenv("ALBERT_PACE_LOG")!=nullptr;if(log&&now-due>std::chrono::milliseconds(150))std::fprintf(stderr,"PACE late %.0f ms at sim %.1f s\n",std::chrono::duration<double,std::milli>(now-due).count(),sim);
        if(now-due>std::chrono::milliseconds(250))start+=now-due; // Behind (a slow or hot machine): on from now, never sprinting to catch up (the scene ran in pulses at 2x).
        else{std::this_thread::sleep_until(due);paceSlept+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-now).count();}}
};
// along, across: where it rests on the arms, along their rails (east) and across them (north) from the tower's axis (m).
// down: the catch given up (gaveUp: why), the ship down at sea or on the ground `away` m from the tower's axis.
struct Home {double deorbit=0,entry=0,flip=0,maxTile=0,caught=0,offAxis=0,along=0,across=0,speed=0,propellant=0,towerClearance=1e9,away=0,downEast=0,downNorth=0,handoff=-1,handoffMiss=-1;int overheats=0;bool down=false;std::string gaveUp; // towerClearance: see clearance().
    bool clear()const{return downEast<-110-shipLength||downEast>60+shipLength||downNorth<-115-shipLength||downNorth>55+shipLength;} // Down clear of the ground's works by its length (as Mission's).
    bool onRails()const{return std::abs(across)<.2&&along>-5&&along<9;}}; // The pins in the rails' grooves (TowerApproach::inGrooves, the crests 0.2 m out).
// The ship home from orbit to a catch, then 30 s on the arms; warp: the orbit's time compression in real time. Throws on a
// failure.
// Why a vehicle broke up, if it did (the flight's Breakup), in words; null while whole.
inline const char* broken(const api::FlightSample& s){
    if(!s.has_breakup())return nullptr;
    switch(s.breakup().cause()){case api::Breakup::GROUND:return "bateu no chão";case api::Breakup::STRUCTURE:return "bateu numa estrutura";case api::Breakup::HEAT:return "superaqueceu";case api::Breakup::TERMINATED:return "terminado pelo FTS";default:return nullptr;}
}
// A mission event for the broadcast's timeline: done now (the step's start), or planned at a UTC instant.
inline void mark(api::StepRequest& q,const char* name,double plannedUtc=-1){auto* m=q.add_milestones();m->set_name(name);if(plannedUtc>=0){m->set_utc_unix_seconds(plannedUtc);m->set_planned(true);}}
inline void decide(api::StepRequest& q,const std::string& what,const std::string& why){auto* d=q.mutable_decision();d->set_what(what);d->set_why(why);}
// A failure to inject: on "stack" (before staging), "booster" or "ship", at its mission time (s from T-0: negative while counting) or as its guidance enters a
// phase (BoosterReturn's or ShipReturn's, by name). Written "flight@time:what" or "flight@phase:what", several joined by
// commas; what:
// - n: engine n (1-based) fails, as its own sensors report it; silent:n: it fails saying it is fine; gimbal:n: its gimbal
//   jams where it is; surface:n: surface n jams;
// - a sensor that fails or lies, sensor=mode[:x,y,z]: sensor imuN.accel or imuN.gyro (N 1-3), gnss, radar, star or gauge;
//   mode dead, stuck, bias, drift, noise or none (value: SensorFault's units);
// - gust:n,e,d (m/s) or push:n,e,d (N): wind or an outside force from then on; terminate: the FTS.
struct Fault {std::string flight,phase,what;double at=0;bool timed=false,done=false;}; // timed: at its mission time (negative in the count), else its phase.
inline api::FlightCommand faultCommand(const std::string& what){
    api::FlightCommand c;auto vec3=[](const std::string& t,api::Vector3* v){double x[3]{};std::size_t i=0,k=0;
        while(k<3&&i<=t.size()){const std::size_t e=std::min(t.find(',',i),t.size());if(e>i)x[k]=std::stod(t.substr(i,e-i));++k;i=e+1;}v->set_x(x[0]);v->set_y(x[1]);v->set_z(x[2]);};
    if(!what.empty()&&std::isdigit(static_cast<unsigned char>(what[0]))){c.set_fail(true);c.set_index(static_cast<unsigned>(std::stoi(what)));return c;}
    if(what.rfind("silent:",0)==0){c.set_fail_silent(true);c.set_index(static_cast<unsigned>(std::stoi(what.substr(7))));return c;}
    if(what=="terminate"){c.set_terminate(true);return c;}
    if(what.rfind("gimbal:",0)==0){c.set_stick_gimbal(true);c.set_index(static_cast<unsigned>(std::stoi(what.substr(7))));return c;}
    if(what.rfind("surface:",0)==0){c.set_stick_surface(true);c.set_index(static_cast<unsigned>(std::stoi(what.substr(8))));return c;}
    if(what.rfind("gust:",0)==0){vec3(what.substr(5),c.mutable_gust_ned_meters_per_second());return c;}
    if(what.rfind("push:",0)==0){vec3(what.substr(5),c.mutable_force_ned_newtons());return c;}
    const std::size_t eq=what.find('=');if(eq==std::string::npos)throw std::invalid_argument("A failure is an engine, silent:n, gimbal:n, surface:n, sensor=mode, gust:, push: or terminate: "+what);
    const std::string sensor=what.substr(0,eq),rest=what.substr(eq+1),mode=rest.substr(0,rest.find(':'));auto* f=c.mutable_sensor_fault();
    if(sensor.rfind("imu",0)==0&&sensor.size()>=5){f->set_unit(static_cast<unsigned>(sensor[3]-'1'));f->set_sensor(sensor.find("gyro")!=std::string::npos?api::SensorFault::GYROSCOPE:api::SensorFault::ACCELEROMETER);}
    else if(sensor=="gnss")f->set_sensor(api::SensorFault::GNSS);else if(sensor=="radar")f->set_sensor(api::SensorFault::RADAR_ALTIMETER);
    else if(sensor=="star")f->set_sensor(api::SensorFault::STAR_TRACKER);else if(sensor=="gauge")f->set_sensor(api::SensorFault::PROPELLANT_GAUGE);
    else throw std::invalid_argument("No such sensor: "+sensor);
    const char* modes[]{"none","dead","stuck","bias","drift","noise"};int m=-1;for(int i=0;i<6;++i)if(mode==modes[i])m=i;if(m<0)throw std::invalid_argument("No such sensor mode: "+mode);
    f->set_mode(static_cast<api::SensorFault::Mode>(m));if(rest.find(':')!=std::string::npos)vec3(rest.substr(rest.find(':')+1),f->mutable_value());return c;
}
inline std::vector<Fault> faults(const std::string& spec){
    std::vector<Fault> out;std::size_t from=0;
    while(from<spec.size()){std::size_t end=spec.find(',',from); // A comma inside a value (x,y,z) belongs to it: up to the next "@".
        for(;end!=std::string::npos;end=spec.find(',',end+1)){const std::size_t next=spec.find('@',end);const std::size_t comma=spec.find(',',end+1);if(next!=std::string::npos&&(comma==std::string::npos||comma>next))break;
            if(next==std::string::npos){end=std::string::npos;break;}}
        if(end==std::string::npos)end=spec.size();const std::string x=spec.substr(from,end-from);from=end+1;
        const std::size_t a=x.find('@'),b=x.find(':',a);if(a==std::string::npos||b==std::string::npos)throw std::invalid_argument("A failure is flight@time-or-phase:what: "+x);
        Fault f;f.flight=x.substr(0,a);const std::string when=x.substr(a+1,b-a-1);f.what=x.substr(b+1);(void)faultCommand(f.what); // Checked now.
        if(f.flight!="stack"&&f.flight!="booster"&&f.flight!="ship")throw std::invalid_argument("A failure's flight is stack, booster or ship: "+x);
        if(!when.empty()&&(std::isdigit(static_cast<unsigned char>(when[0]))||when[0]=='.'||when[0]=='-')){f.at=std::stod(when);f.timed=true;}else f.phase=when;out.push_back(f);}
    return out;
}
// The failures due now on that flight (its time, its phase's name) as its commands.
inline void inject(std::vector<Fault>& faults,const char* flight,double t,const char* phase,api::StepRequest& q){
    for(auto& f:faults)if(!f.done&&f.flight==flight&&((f.timed&&t>=f.at)||(!f.timed&&phase&&f.phase==phase))){
        *q.add_commands()=faultCommand(f.what);f.done=true;std::printf("%9.1f s  FALHA: %s (%s, %s)\n",t,f.what.c_str(),flight,f.timed?"no instante":phase);}
}
inline Home flyHome(api::FlightService::Stub& flight,api::FlightSample& ship,Pacer& pace,double warp,Navigator nav,std::vector<Fault> faults={},double zero=0,ShipReturn::Phase from=ShipReturn::Phase::orbit,std::FILE* csv=nullptr,std::pair<std::string,std::string> pending={}){ // zero: T-0 on its flight's clock; from: the phase its guidance takes up.
    ShipReturn back(towerSite(),towerLatitude*deg,towerLongitude*deg,armHeight,pinRest());siteWorks(back);if(from!=ShipReturn::Phase::orbit)back.enter(from);Home out;double report=0,caughtAt=-1,traced=0,low=0;const bool trace=std::getenv("ALBERT_TRACE")!=nullptr,entryTrace=trace&&std::string(std::getenv("ALBERT_TRACE"))=="entry";
    const char* names[]{"em órbita","orientação retrógrada","queima de saída de órbita","planeio até o ar","reentrada","giro","queima de pouso","pairando","descendo","capturada","desvio","no chão"};auto last=back.phase();
    bool open=false,fleeing=false; // The arms: opened for the way down past them, closed over the hover.
    bool skip=false; // The operator's SKIP_ENTRY: the entry quickened (the same flight) until its belly flop.
    double csvAt=0;
    std::vector<std::string> marks; // Events seen since the last step, for the timeline.
    for(;;){
        const double t=(ship.elapsed_seconds()-zero);if(csv&&t>=csvAt){csvAt=std::floor(t)+1;profile(csv,t,"ship",ship,names[static_cast<int>(back.phase())]);}back.armsClosed=armsClosed(ship);armAngles(back,ship);const Command c=back.step(t,nav.state(ship));out.handoff=back.handoff();out.handoffMiss=back.handoffMiss();
        const bool quick=back.phase()==ShipReturn::Phase::orbit||back.phase()==ShipReturn::Phase::glide;
        const double ahead=quick?back.nextStep():std::min(back.nextStep(),.25); // In real time: short steps, so the scene's clock never waits on one (a 10 s step showed it paused).
        api::StepRequest q;q.set_ticks(static_cast<unsigned>(std::clamp(std::llround(ahead*1000),1LL,600000LL)));apply(q,c);
        if(!pending.first.empty()){decide(q,pending.first,pending.second);pending={};}
        {static const char* phases[]{"orbit","orient","deorbit","glide","entry","flip","landing","hover","descend","caught","divert","down"};inject(faults,"ship",t,phases[static_cast<int>(back.phase())],q);}
        for(const auto& m:marks)mark(q,m.c_str());marks.clear();
        {const bool shut=back.closeArms()||back.phase()==ShipReturn::Phase::caught; // As its guidance asks (shutting round it as it settles: shutting()).
            if(shut==open){arms(q,!shut);open=!shut;}}
        call([&](grpc::ClientContext& ctx){return flight.Step(&ctx,q,&ship);});nav.update(ship);
        for(const auto& u:ship.uplinks())if(u=="SKIP_ENTRY"&&!skip&&(back.phase()==ShipReturn::Phase::glide||back.phase()==ShipReturn::Phase::entry)&&!back.falling()){skip=true;
            std::printf("%9.1f s  nave: reentrada acelerada até a queda de barriga\n",ship.elapsed_seconds()-zero);pending={"reentrada acelerada","pedido do operador: até a queda de barriga"};}
        for(const auto& u:ship.uplinks())if(u=="ABORT"&&!back.gaveUp()){back.abort(nav.state(ship));std::printf("%9.1f s  nave: ABORTAR recebido\n",ship.elapsed_seconds()-zero);pending={"ABORTAR recebido","ABORT"};}
        for(const auto& w:nav.reports){std::printf("%9.1f s  nave: FDIR: %s\n",(ship.elapsed_seconds()-zero),w.c_str());pending={"FDIR",w};}nav.reports.clear();
        if(entryTrace&&back.phase()==ShipReturn::Phase::entry&&t>=low){low=t+2;const State x=state(ship);const Vec vb=mul(x.toBody,x.velocity);const double v=norm(vb); // ALBERT_TRACE=entry: its attitude in the air.
            const auto [ea,no]=aside(ship);const auto& vv=ship.velocity_ned_meters_per_second();std::printf("ENTRY %.0f e %.0f n %.0f ve %.1f vn %.1f h %.1f km v %.0f q %.0f alpha %.1f beta %.1f bank %.0f/%.0f rate %.2f %.2f %.2f flaps %.0f %.0f %.0f %.0f torque %.2g %.2g %.2g gas %.0f\n",t,ea,no,vv.y(),vv.x(),x.height/1e3,v,x.q,std::atan2(vb.z,vb.x)/deg,std::asin(vb.y/std::max(v,1.))/deg,
                back.bank()/deg,back.bankWanted()/deg,x.rate.x/deg,x.rate.y/deg,x.rate.z/deg,x.surfaces.size()>3?x.surfaces[0]/deg:0,x.surfaces.size()>3?x.surfaces[1]/deg:0,x.surfaces.size()>3?x.surfaces[2]/deg:0,x.surfaces.size()>3?x.surfaces[3]/deg:0,c.torque.x,c.torque.y,c.torque.z,ship.thruster_gas_kilograms());}
        if(trace&&(ship.elapsed_seconds()-zero)>=traced){traced=(ship.elapsed_seconds()-zero)+(back.phase()==ShipReturn::Phase::orbit?3600:10);const State x=state(ship); // ALBERT_TRACE: the navigation's errors.
            const Quat qt=quatOf(transposed(x.toBody));const Vec ge=mul(earthFromInertial(ship.utc_unix_seconds()),vec(ship.position_meters()));
            std::printf("NAV %.0f %s pos %.2f m vel %.3f m/s att %.4f° refused %d stars %d gnss %d st %d eo %.4f°\n",(ship.elapsed_seconds()-zero),names[static_cast<int>(back.phase())],
                norm(nav.position+x.position*-1.),norm(nav.velocity+x.velocity*-1.),norm(angleOf(qt*conj(nav.attitude)))/deg,nav.refused(),nav.refusedStars(),ship.gnss().valid(),ship.star_tracker().valid(),
                std::acos(std::clamp(dot(unit(ge),unit(x.position)),-1.,1.))/deg);}
        if(const double at=ship.elapsed_seconds()-zero;trace&&back.phase()>=ShipReturn::Phase::landing&&at>=low){low=at+.5; // ALBERT_TRACE: its way down, twice a second (as the booster's).
            const auto [e,n]=aside(ship);const State x=state(ship);const Vec u=back.up(),east{-std::sin(towerLongitude*deg),std::cos(towerLongitude*deg),0},h=x.velocity+u*-dot(x.velocity,u);int lit=0;for(const auto& en:ship.engines())lit+=en.on();
            std::printf("TRACE %.1f %s e %.1f n %.1f h %.1f ve %.2f vn %.2f vz %.2f lit %d prop %.0f arms %.2f close %d open %d contact %.0f push %.2f\n",at,names[static_cast<int>(back.phase())],e,n,ship.geodetic().height_meters(),
                dot(h,east),dot(h,cross(u,east)),dot(x.velocity,u),lit,ship.propellant_kilograms()/1e3,ship.structure_radians_size()?ship.structure_radians(0)/deg:0.,back.closeArms(),open,norm(x.contact),norm(back.disturbance()));}
        for(const auto& e:ship.events())if(e.kind()==api::FlightEvent::OVERHEAT&&e.index()<static_cast<unsigned>(ship.thermal_nodes_size())){++out.overheats;
            std::printf("%9.1f s  nave: %s acima do limite\n",(ship.elapsed_seconds()-zero),ship.thermal_nodes(static_cast<int>(e.index())).name().c_str());}
        for(const auto& n:ship.thermal_nodes())out.maxTile=std::max(out.maxTile,n.temperature_kelvin());
        out.towerClearance=std::min(out.towerClearance,clearance(ship,shipLength));
        if(back.phase()!=last){last=back.phase();const double now=(ship.elapsed_seconds()-zero);
            if(last==ShipReturn::Phase::deorbit)out.deorbit=now;else if(last==ShipReturn::Phase::entry)out.entry=now;else if(last==ShipReturn::Phase::flip)out.flip=now;else if(last==ShipReturn::Phase::caught)caughtAt=out.caught=now;
            static const char* events[]{nullptr,nullptr,"SAÍDA DE ÓRBITA",nullptr,"REENTRADA","GIRO","QUEIMA DE POUSO DA NAVE",nullptr,nullptr,"CAPTURA DA NAVE","NAVE ABORTA A CAPTURA",nullptr};
            if(const char* e=events[static_cast<int>(last)])marks.push_back(e);
            std::printf("%9.1f s  nave: %s (h %.2f km, v %.0f m/s, %.0f t de propelante, pouso previsto %.1f km além e %.1f km à direita da torre)\n",now,names[static_cast<int>(last)],
                ship.geodetic().height_meters()/1e3,speedOf(ship),ship.propellant_kilograms()/1e3,back.along()/1e3,back.beside()/1e3);
            char why[512];std::snprintf(why,sizeof why,"h %.2f km, v %.0f m/s, %.0f t de propelante, pouso previsto %.1f km além e %.1f km à direita da torre",
                ship.geodetic().height_meters()/1e3,speedOf(ship),ship.propellant_kilograms()/1e3,back.along()/1e3,back.beside()/1e3);pending={skip&&last==ShipReturn::Phase::entry?"reentrada acelerada":names[static_cast<int>(last)],why};}
        const double now=(ship.elapsed_seconds()-zero);
        if(now>=report){const bool orbiting=last==ShipReturn::Phase::orbit;report=orbiting?std::floor(now/3600)*3600+3600:std::floor(now/30)*30+30;
            std::printf("%9.1f s  nave: h %7.1f km  v %5.0f m/s  q %6.0f Pa  inclinação lateral %4.0f°  pouso previsto %.1f km além, %.1f à direita  ladrilho mais quente %4.0f K gás %.0f kg\n",now,
                ship.geodetic().height_meters()/1e3,speedOf(ship),ship.dynamic_pressure_pascals(),back.bank()/deg,back.along()/1e3,back.beside()/1e3,out.maxTile,ship.thruster_gas_kilograms());}
        if(const char* why=back.escaping();why&&!fleeing){fleeing=true;marks.push_back("NAVE EM FUGA");std::printf("%9.1f s  nave: FUGA com todos os recursos (%s)\n",now,why);pending={"fuga com todos os recursos",why};}else if(!back.escaping())fleeing=false;
        if(back.gaveUp()&&out.gaveUp.empty()){out.gaveUp=back.gaveUp();std::printf("%9.1f s  nave: captura cancelada (%s); para longe da torre\n",now,back.gaveUp());pending={"captura cancelada",back.gaveUp()};}
        if(back.gaveUp()&&(back.phase()==ShipReturn::Phase::down||ship.landed()||broken(ship))){ // Down away from the tower (or broken up there): its flight over.
            out.down=true;out.away=offAxis(ship,back.up());std::tie(out.downEast,out.downNorth)=aside(ship);std::printf("%9.1f s  nave: no chão a %.0f m da torre, %.1f m/s%s\n",now,out.away,speedOf(ship),broken(ship)?" (desfeita)":"");
            if(const char* why=broken(ship);why&&!out.clear())throw std::runtime_error(std::string("A nave se desfez sobre a infraestrutura: ")+why); // Never on the tower or the works, given up or not.
            return out;}
        if(const char* why=broken(ship))throw std::runtime_error(std::string("A nave se desfez: ")+why+" ("+std::to_string(static_cast<int>(ship.breakup().speed_meters_per_second()))+" m/s)");
        if(ship.landed()||ship.geodetic().height_meters()<towerGround)throw std::runtime_error("A nave atingiu o solo");
        if(caughtAt>=0&&now-caughtAt>=30){out.offAxis=offAxis(ship,back.up());std::tie(out.along,out.across)=aside(ship);out.speed=speedOf(ship);out.propellant=ship.propellant_kilograms();
            std::printf("Nave: a menor folga da torre, abaixo do topo dela, %.1f m.\n",out.towerClearance);
            std::printf("Nave capturada aos %.1f s; 30 s depois, nos braços: %.2f m do eixo da torre, %.3f m/s, %.0f t de propelante; ladrilho mais quente %.0f K, %d alertas de calor.\n",
                out.caught,out.offAxis,out.speed,out.propellant/1e3,out.maxTile,out.overheats);return out;}
        if(now>4*86400)throw std::runtime_error("A nave não voltou em 4 dias");
        // Quickened while it waits in orbit (~a day, for its track to pass over Starbase) and while it falls from the deorbit burn to the
        // air (up to 60x), the entry too when the operator skips it, until the belly flop; the turn, the burn, the entry and the landing as they happen.
        if(back.falling()||(back.phase()!=ShipReturn::Phase::glide&&back.phase()!=ShipReturn::Phase::entry))skip=false; // Into the fall: as it happens again.
        const bool skipping=skip&&back.phase()==ShipReturn::Phase::entry;
        const bool landing=back.phase()>=ShipReturn::Phase::flip||(back.falling()&&ship.geodetic().height_meters()-towerGround<6e3); // --landings: as it happens from 6 km in its belly flop.
        const double rate=back.phase()==ShipReturn::Phase::orbit?warp:back.phase()==ShipReturn::Phase::glide||skipping?std::min(warp,60.):1;
        pace(ship.elapsed_seconds(),landing?rate:std::max(rate,pace.hurry));
    }
}
// The flight from Starbase:
// - the terminal count from T-60 s on the stack's computer (Countdown): self-tests, the gimbals swept, the engines lit from
//   T-3 s and the clamps let go at T-0 once all of them are at thrust; else it scrubs, the stack left on the pad;
// - the stack lifts off on 33 engines and cuts off with 570 t left in the booster (as its gauges read it);
// - the booster flies on as its own flight ("booster", spawned) to a catch on the tower's arms, rests there 30 s and is
//   taken off (its flight ended);
// - the ship goes on to orbit and, unless orbitOnly, home to the same arms (flyHome).
// Throws when any part fails. onSpawn: called once the booster is a flight (scene id "flight.booster").
// boosterDown: the catch given up (gaveUp: why), the booster down at sea or on the ground boosterAway m from the tower's axis.
// scrubbed: the count stopped on the pad (why), padSafe: the stack then whole, clamped and every engine out.
struct Mission {double meco=0,mecoHeight=0,seco=0,perigee=0,apoapsis=0,maxQ=0,maxQAt=0,caught=0,offAxis=0,along=0,across=0,speed=0,propellant=0,towerClearance=1e9,boosterAway=0;
    bool boosterLost=false,boosterDown=false,padSafe=false;std::string gaveUp,scrubbed;std::vector<std::string> fdir;Home ship; // fdir: what the computers' FDIR found.
    double downEast=0,downNorth=0; // Where the booster came down, from the tower's axis (m).
    // Down clear of the ground's works by its length, as its computer knows them (siteWorks: the pad's box).
    bool clear()const{return downEast<-110-boosterLength||downEast>60+boosterLength||downNorth<-115-boosterLength||downNorth>55+boosterLength;}
    bool onRails()const{return std::abs(across)<.2&&along>-5&&along<9;}}; // The booster's pins in the grooves (along, across: as Home's).
inline Mission mission(api::FlightService::Stub& flight,bool fast,double warp=1000,bool orbitOnly=false,const std::function<void()>& onSpawn={},std::vector<Fault> faults={},double utc=0,double hurry=1){
    Mission out;Pacer pace;pace.fast=fast;pace.hurry=hurry;
    api::CreateFlightRequest create;create.set_vehicle(api::CUSTOM);*create.mutable_definition()=stack();create.set_focus_camera(true);create.set_heading_degrees(heading);
    create.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);addTower(create,true);if(utc>0)create.set_utc_unix_seconds(utc); // Unset: the scene's instant.
    create.set_hold_down(true); // The clamps let go on the count's command only.
    avionics(create);api::FlightSample ship,boost;call([&](grpc::ClientContext& c){return flight.CreateFlight(&c,create,&ship);});
    Navigator nav,bnav;nav.base=towerSite();nav.beacon=towerCrown();nav.beaconNed=towerNed();nav.design(create.definition());nav.align(ship,7); // The stack's computer, aligned on the pad; the booster's from it at separation.
    std::printf("%s de Starbase, %s\n",ship.vehicle().c_str(),fast?"o mais rápido possível":"tempo real");
    BoosterReturn back(towerSite(),towerLatitude*deg,towerLongitude*deg,armHeight,pinRest());siteWorks(back);GravityTurn ascent(heading*deg,kickAngle);ascent.kickRate=.6*deg;ascent.pitch=flownPitch();ascent.insertion=true; // A small kick: lofted, so the booster's way back stays short.
    ascent.slewRate=1*deg; // The ship's nose eased onto its insertion guidance after staging, not swung there.
    double thrust=stackThrottle,need=0,needAt=0;bool flapsOut=false,spawned=false,orbit=false,bucket=false,coasting=false,shut=false,mirror=false,peaked=false,fleeing=false;double report=0,maxQ=0,maxQAt=0,spawnAt=0,caughtAt=0,lostAt=-1;
    std::vector<std::string> marks,found; // Events seen since the last step, for the timeline; the FDIR's findings, for the count.
    std::pair<std::string,std::string> pending,bpending; // Stack/ship and booster decisions belong to their own next requests.
    const bool trace=std::getenv("ALBERT_TRACE")!=nullptr,attitude=trace&&std::string(std::getenv("ALBERT_TRACE")).rfind("attitude",0)==0,fine=attitude&&std::string(std::getenv("ALBERT_TRACE"))=="attitude-fine";double traced=0,turned=0;bool entered=false;
    Countdown count(create.definition(),ship,{{"MAX-Q",62},{"MECO",143},{"SEPARAÇÃO A QUENTE",146},{"BOOSTBACK",152},{"QUEIMA DE POUSO DO BOOSTER",396},{"CAPTURA DO BOOSTER",418},
        {"SECO",505}},120,1800,8,9); // QDs follow the held count; catch structures 1-7 keep their indices.
    auto clock=[&]{return count.clock(ship.elapsed_seconds());}; // The mission's time (T, s).
    const char* labels[]{"espera","giro","boostback","costa","queima de pouso","aproximação final","pairando","descendo","capturado","desvio","no chão"};auto last=back.phase();
    const std::unique_ptr<std::FILE,int(*)(std::FILE*)> csv(std::getenv("ALBERT_CSV")?std::fopen(std::getenv("ALBERT_CSV"),"w"):nullptr,&std::fclose);double csvAt=0;
    if(csv)std::fprintf(csv.get(),"t,vehicle,height_km,speed_kmh,inertial_ms,climb_ms,downrange_km,propellant_t,mass_t,lit,throttle,q_kpa,phase\n");
    static const char* ascents[]{"pad","vertical","kick","turn","coast","separated","upper","orbit","toApoapsis","circularize"};
    static const char* names[]{"wait","flip","boostback","coast","landing","final","hover","descend","caught","divert","down"}; // For the failures.
    for(;;){
        const double t=clock();
        api::StepRequest step;step.set_ticks(20);inject(faults,spawned?"ship":"stack",t,nullptr,step);
        if(!pending.first.empty()){decide(step,pending.first,pending.second);pending={};}
        if(count.phase()!=Countdown::Phase::launched){ // On the pad: the count, until the clamps let go.
            count.step(ship,nav,found,step);found.clear();
            call([&](grpc::ClientContext& c){return flight.Step(&c,step,&ship);});nav.update(ship);
            for(const auto& w:nav.reports){std::printf("%7.2f s  pilha: FDIR: %s\n",clock(),w.c_str());out.fdir.push_back("pilha: "+w);found.push_back(w);pending={"FDIR",w};}nav.reports.clear();
            if(const char* why=broken(ship))throw std::runtime_error(std::string("A pilha se desfez na plataforma: ")+why);
            if(count.phase()==Countdown::Phase::scrubbed){ // Safed: 3 s for the engines to wind down, the stack on its clamps.
                for(int i=0;i<150;++i){api::StepRequest idle;idle.set_ticks(20);if(!pending.first.empty()){decide(idle,pending.first,pending.second);pending={};}call([&](grpc::ClientContext& c){return flight.Step(&c,idle,&ship);});pace(ship.elapsed_seconds(),1);}
                const bool lit=std::any_of(ship.engines().begin(),ship.engines().end(),[](const api::EngineSample& e){return e.on();});
                out.scrubbed=count.why();out.padSafe=ship.clamped()&&!broken(ship)&&!lit;
                std::printf("Lançamento cancelado (%s); a pilha %s.\n",count.why().c_str(),out.padSafe?"inteira, presa à plataforma e com os motores apagados":"NÃO ficou segura");return out;}
            pace(ship.elapsed_seconds(),pace.hurry);continue;}
        for(const auto& m:marks)mark(step,m.c_str());marks.clear();
        if(!spawned&&ship.stage()==0&&!ship.clamped()&&clock()>90&&clock()>=needAt){ // What its way back takes, once a second (the booster's own prediction: predictive MECO);
            // seconds apart while far above it (the gap closes under 28 t/s: ~20 burnt, the need growing ~7; 40 t/s kept), the same second's grid.
            const State x=nav.state(ship);int working=0;for(int i=0;i<13&&i<static_cast<int>(x.failed.size());++i)working+=!x.failed[static_cast<std::size_t>(i)];need=1.05*back.returnNeed(x,x.propellant,working);
            needAt=clock()+std::clamp(std::floor((x.propellant-need)/40e3),1.,10.);}
        if(!spawned&&ship.stage()==0&&!ship.clamped()&&(nav.state(ship).propellant<=std::max(reserve,need)||(clock()>30&&speedOf(ship)>=mecoSpeed))){ // At the flights' cutoff speed, or down to what the booster's way back takes (its own prediction, 5% kept; never under the reserve), as its gauges read it. // MECO and hot staging: the booster becomes its own flight.
            api::StepRequest stage;stage.add_commands()->set_cutoff(true);auto* sp=stage.add_commands()->mutable_spawn();sp->set_flight_id("booster");*sp->mutable_definition()=superHeavy();
            if(step.has_decision()){decide(stage,step.decision().what(),step.decision().why());step.clear_decision();}
            const Vec stack=vec(ship.position_ecef_meters());sp->set_nose_offset_meters(shipLength);call([&](grpc::ClientContext& c){return flight.Step(&c,stage,&ship);});
            {api::FlightRequest q;q.set_flight_id("booster");call([&](grpc::ClientContext& c){return flight.GetFlight(&c,q,&boost);});}
            // Each computer from the stack's estimate, moved from the stack's centre of mass to its own (the design's geometry).
            bnav.from(nav,boost,vec(boost.position_ecef_meters())+stack*-1.);
            bnav.design(superHeavy()); // Its engines' and thrusters' flows (its RCS on the tanks).
            nav.update(ship);nav.position=nav.position+vec(ship.position_ecef_meters())+stack*-1.; // The ship's computer onto its own centre of mass.
            std::printf("%7.2f s  MECO e separação a quente: h %.1f km, v %.0f m/s; booster com %.0f t\n",t,ship.geodetic().height_meters()/1e3,speedOf(ship),boost.propellant_kilograms()/1e3);
            {char why[256];std::snprintf(why,sizeof why,"h %.1f km, v %.0f m/s; booster com %.0f t",ship.geodetic().height_meters()/1e3,speedOf(ship),boost.propellant_kilograms()/1e3);decide(step,"MECO e separação a quente",why);}
            spawned=true;spawnAt=t;if(onSpawn)onSpawn();mark(step,"MECO");mark(step,"SEPARAÇÃO A QUENTE");out.meco=t;out.mecoHeight=ship.geodetic().height_meters();back.start(boost.elapsed_seconds());step.add_commands()->set_ignite(true);ascent.upper(nav.reading(ship));
        }
        if(!spawned){ // Off the pad at the flights' thrust (V3's ~1.45 g at liftoff, Flights 12-14's telemetry), then held to 2.5 g
            // as it lightens (their speed's steady ~20 m/s² gain from 110 s to cutoff): the throttle eased by the IMU's specific force.
            const auto& f=ship.specific_force_meters_per_second2();const double g=std::hypot(f.x(),f.y(),f.z());
            const double want=std::clamp(g>1?thrust*gLimit/g:stackThrottle,.4,stackThrottle);if(!bucket||std::abs(want-thrust)>.005){thrust=want;step.add_commands()->set_throttle(thrust);bucket=true;}}
        if(mirror){arms(step,!shut);mirror=false;}
        if(orbit&&!coasting){auto* q=step.add_commands()->mutable_torque_newton_meters();q->set_x(0);q->set_y(0);q->set_z(0);step.add_commands()->mutable_gimbal();coasting=true;}
        if(!orbit){const Commands c=ascent.step(nav.reading(ship));
            if(c.ignite&&!spawned)step.add_commands()->set_ignite(true);if(c.cutoff)step.add_commands()->set_cutoff(true);
            auto* q=step.add_commands()->mutable_torque_newton_meters();q->set_x(c.torque.x);q->set_y(c.torque.y);q->set_z(c.torque.z);
            auto* g=step.add_commands()->mutable_gimbal();g->set_pitch_radians(c.gimbalPitch);g->set_yaw_radians(c.gimbalYaw); // The Raptors steer the stack.
            if(spawned&&!flapsOut&&ship.surfaces_size()>=4){flapsOut=true;for(unsigned i=1;i<=4;++i){auto* k=step.add_commands();k->set_surface_radians(45*deg);k->set_index(i);}} // The ship's flaps held straight out, as on the stack.
            if(ascent.phase()==GravityTurn::Phase::orbit){orbit=true;out.seco=t;marks.push_back("SECO");out.perigee=GravityTurn::perigee(vec(ship.position_meters()),vec(ship.velocity_meters_per_second()));
                out.apoapsis=GravityTurn::apoapsis(vec(ship.position_meters()),vec(ship.velocity_meters_per_second()));std::printf("%7.2f s  SECO: perigeu %.0f km, apogeu %.0f km, girando a %.2f°/s\n",t,out.perigee/1e3,out.apoapsis/1e3,norm(vec(ship.angular_rate_radians_per_second()))/deg);
                char why[256];std::snprintf(why,sizeof why,"perigeu %.0f km, apogeu %.0f km, girando a %.2f°/s",out.perigee/1e3,out.apoapsis/1e3,norm(vec(ship.angular_rate_radians_per_second()))/deg);decide(step,"SECO",why);}}
        call([&](grpc::ClientContext& c){return flight.Step(&c,step,&ship);});nav.update(ship);
        for(const auto& w:nav.reports){std::printf("%7.2f s  %s: FDIR: %s\n",clock(),spawned?"nave":"pilha",w.c_str());out.fdir.push_back(std::string(spawned?"nave: ":"pilha: ")+w);pending={"FDIR",w};}nav.reports.clear();
        if(const char* why=broken(ship))throw std::runtime_error(std::string(spawned?"A nave se desfez: ":"A pilha se desfez: ")+why);
        if(ship.dynamic_pressure_pascals()>maxQ){maxQ=ship.dynamic_pressure_pascals();maxQAt=clock();}
        else if(!peaked&&maxQ>1e4&&ship.dynamic_pressure_pascals()<.97*maxQ){peaked=true;marks.push_back("MAX-Q");}
        if(spawned&&lostAt>=0){if(!out.boosterLost&&!out.boosterDown){ // Broken up: what is left falls, uncommanded, until it lies on the ground, then its flight ends.
            api::StepRequest b;b.set_flight_id("booster");b.set_ticks(20);if(!bpending.first.empty()){decide(b,bpending.first,bpending.second);bpending={};}call([&](grpc::ClientContext& ctx){return flight.Step(&ctx,b,&boost);});
            if(boost.landed()&&!out.boosterLost){api::FlightRequest q;q.set_flight_id("booster");api::Applied a;call([&](grpc::ClientContext& c){return flight.EndFlight(&c,q,&a);});out.boosterLost=true;}}
        }
        else if(spawned){ // Caught, it stays a flight: resting on the arms.
            back.armsClosed=armsClosed(boost);armAngles(back,boost);const Command c=back.step(boost.elapsed_seconds(),bnav.state(boost));api::StepRequest b;b.set_flight_id("booster");b.set_ticks(20);apply(b,c); // The booster's own clock: 0 at its spawn.
            if(!bpending.first.empty()){decide(b,bpending.first,bpending.second);bpending={};}
            inject(faults,"booster",spawnAt+boost.elapsed_seconds(),names[static_cast<int>(back.phase())],b);
            if(back.closeArms()!=shut){shut=back.closeArms();arms(b,!shut);mirror=true;} // As its guidance asks; the site shows the ship's flight's arms: those too.
            if(const char* why=back.escaping();why&&!fleeing){fleeing=true;marks.push_back("BOOSTER EM FUGA");const auto [east,north]=aside(boost);
                std::printf("%7.2f s  booster: FUGA com todos os recursos (%s; h %.0f m, %.0f m a leste e %.0f m ao norte da torre)\n",spawnAt+boost.elapsed_seconds(),why,boost.geodetic().height_meters()-towerGround,east,north);
                char reason[1024];std::snprintf(reason,sizeof reason,"%s; h %.0f m, %.0f m a leste e %.0f m ao norte da torre",why,boost.geodetic().height_meters()-towerGround,east,north);decide(b,"fuga com todos os recursos",reason);}
            else if(!back.escaping())fleeing=false;
            if(back.gaveUp()&&out.gaveUp.empty()){out.gaveUp=back.gaveUp();marks.push_back("BOOSTER ABORTA A CAPTURA");const auto [east,north]=aside(boost);
                std::printf("%7.2f s  booster: captura cancelada (%s); desviando para longe da torre (h %.2f km, %.0f m a leste e %.0f m ao norte da torre)\n",spawnAt+boost.elapsed_seconds(),
                    back.gaveUp(),boost.geodetic().height_meters()/1e3,east,north);decide(b,"captura cancelada",back.gaveUp());}
            if(c.terminate){std::printf("%7.2f s  booster: FTS autônomo (%s)\n",spawnAt+boost.elapsed_seconds(),back.termination());decide(b,"FTS autônomo",back.termination());} // Last decision wins over a simultaneous abort or FDIR.
            try{call([&](grpc::ClientContext& ctx){return flight.Step(&ctx,b,&boost);});}
            catch(const std::exception& e){ // A command refused (an engine with nothing to burn): the step without them.
                std::printf("%7.2f s  booster: comando recusado (%s); segue sem ele\n",spawnAt+boost.elapsed_seconds(),e.what());
                api::StepRequest bare;bare.set_flight_id("booster");bare.set_ticks(20);if(c.terminate){bare.add_commands()->set_terminate(true);decide(bare,"FTS autônomo",back.termination());}else decide(bare,"comando recusado",e.what());call([&](grpc::ClientContext& ctx){return flight.Step(&ctx,bare,&boost);});}
            bnav.update(boost);for(const auto& w:bnav.reports){std::printf("%7.2f s  booster: FDIR: %s\n",spawnAt+boost.elapsed_seconds(),w.c_str());out.fdir.push_back("booster: "+w);bpending={"FDIR",w};}bnav.reports.clear();
            for(const auto& u:boost.uplinks())if(u=="ABORT"&&!back.gaveUp()){back.abort(bnav.state(boost));std::printf("%7.2f s  booster: ABORTAR recebido\n",spawnAt+boost.elapsed_seconds());bpending={"ABORTAR recebido","ABORT"};}
            if(back.phase()!=last){last=back.phase();if(last==BoosterReturn::Phase::caught)caughtAt=spawnAt+boost.elapsed_seconds();
                if(last==BoosterReturn::Phase::boostback)marks.push_back("BOOSTBACK");else if(last==BoosterReturn::Phase::landing)marks.push_back("QUEIMA DE POUSO DO BOOSTER");
                else if(last==BoosterReturn::Phase::caught)marks.push_back("CAPTURA DO BOOSTER");
                const auto [east,north]=aside(boost);
                std::printf("%7.2f s  booster: %s (h %.2f km, v %.0f m/s, %.0f t de propelante, %.0f m a leste e %.0f m ao norte da torre, pouso previsto a %.0f m do ponto sobre o mar)\n",
                    spawnAt+boost.elapsed_seconds(),labels[static_cast<int>(last)],boost.geodetic().height_meters()/1e3,speedOf(boost),boost.propellant_kilograms()/1e3,east,north,back.predicted());
                char why[512];std::snprintf(why,sizeof why,"h %.2f km, v %.0f m/s, %.0f t de propelante, %.0f m a leste e %.0f m ao norte da torre, pouso previsto a %.0f m do ponto sobre o mar",
                    boost.geodetic().height_meters()/1e3,speedOf(boost),boost.propellant_kilograms()/1e3,east,north,back.predicted());bpending={labels[static_cast<int>(last)],why};}
            out.towerClearance=std::min(out.towerClearance,clearance(boost,boosterLength));
            if(trace&&!entered&&back.phase()==BoosterReturn::Phase::coast&&boost.geodetic().height_meters()<50e3){entered=true;const auto [e,n]=aside(boost);const auto& v=boost.velocity_ned_meters_per_second(); // ALBERT_TRACE: the landing bench's start (Gate).
                std::printf("GATE east %.1f north %.1f height %.1f vn %.2f ve %.2f vd %.2f propellant %.0f\n",e,n,boost.geodetic().height_meters()-towerGround,v.x(),v.y(),v.z(),boost.propellant_kilograms());}
            if(attitude&&back.phase()>=BoosterReturn::Phase::flip){const double now=spawnAt+boost.elapsed_seconds();if(now>=turned){turned=now+(fine?0:.25); // ALBERT_TRACE=attitude: its turning, four times a second (attitude-fine: every step).
                const State x=state(boost);double rcs=0;for(const double d:boost.thruster_duty())rcs+=d;std::printf("ATT %.2f %s h %.2f km q %.0f Pa rate %.2f %.2f %.2f °/s fins",now,labels[static_cast<int>(back.phase())],boost.geodetic().height_meters()/1e3,x.q,x.rate.x/deg,x.rate.y/deg,x.rate.z/deg);
                for(const double f:x.surfaces)std::printf(" %.1f",f/deg);std::printf(" rcs %.2f gimbal %.2f %.2f lit %d\n",rcs,boost.engines_size()?boost.engines(0).gimbal().pitch_radians()/deg:0.,boost.engines_size()?boost.engines(0).gimbal().yaw_radians()/deg:0.,static_cast<int>(std::count_if(boost.engines().begin(),boost.engines().end(),[](const api::EngineSample& e){return e.on();})));}}
            if(trace&&back.phase()>=BoosterReturn::Phase::landing){const double now=spawnAt+boost.elapsed_seconds();if(now>=traced){traced=now+.5; // ALBERT_TRACE: its way down, twice a second.
                const auto [e,n]=aside(boost);const State x=state(boost);const Vec u=back.up(),h=x.velocity+u*-dot(x.velocity,u);int lit=0;for(const auto& en:boost.engines())lit+=en.on();
                std::printf("TRACE %.1f %s e %.1f n %.1f h %.1f ve %.2f vn %.2f vz %.2f lit %d prop %.0f arms %.2f contact %.0f nav %.2f m %.3f m/s push %.2f\n",now,labels[static_cast<int>(back.phase())],e,n,boost.geodetic().height_meters(),
                    dot(h,Vec{-std::sin(towerLongitude*deg),std::cos(towerLongitude*deg),0}),dot(h,cross(u,Vec{-std::sin(towerLongitude*deg),std::cos(towerLongitude*deg),0})),dot(x.velocity,u),lit,
                    boost.propellant_kilograms()/1e3,boost.structure_radians_size()?boost.structure_radians(0)/deg:0.,norm(x.contact),norm(bnav.position+x.position*-1.),norm(bnav.velocity+x.velocity*-1.),norm(back.disturbance()));}}
            if(const char* why=broken(boost)){lostAt=spawnAt+boost.elapsed_seconds();marks.push_back("BOOSTER PERDIDO");out.boosterAway=offAxis(boost,back.up());std::tie(out.downEast,out.downNorth)=aside(boost);
                std::printf("%7.2f s  booster: DESTRUÍDO, %s (%.0f m/s)\n",lostAt,why,boost.breakup().speed_meters_per_second());}
            else if(back.gaveUp()&&(back.phase()==BoosterReturn::Phase::down||boost.landed())){ // Down away from the tower: its flight ended.
                if(!out.boosterDown){out.boosterDown=true;out.boosterAway=offAxis(boost,back.up());const auto [east,north]=aside(boost);out.downEast=east;out.downNorth=north;
                    std::printf("%7.2f s  booster: no chão a %.0f m da torre (%.0f m a leste, %.0f m ao norte), %.1f m/s, %.0f t de propelante\n",spawnAt+boost.elapsed_seconds(),
                        out.boosterAway,east,north,speedOf(boost),boost.propellant_kilograms()/1e3);
                    api::FlightRequest q;q.set_flight_id("booster");api::Applied a;call([&](grpc::ClientContext& c){return flight.EndFlight(&c,q,&a);});lostAt=spawnAt+boost.elapsed_seconds();}}
            else if(boost.landed()||boost.geodetic().height_meters()<towerGround)throw std::runtime_error("O booster atingiu o solo");
        }
        if(csv&&clock()>=csvAt){csvAt=std::floor(clock())+1;profile(csv.get(),clock(),spawned?"ship":"stack",ship,orbit?"orbit":ascents[static_cast<int>(ascent.phase())]);
            if(spawned&&lostAt<0&&!out.boosterDown)profile(csv.get(),spawnAt+boost.elapsed_seconds(),"booster",boost,labels[static_cast<int>(back.phase())]);}
        if(clock()>=report){report=std::floor(clock()/30)*30+30;
            std::printf("%7.2f s  nave: h %6.1f km  v %6.0f m/s  q %6.0f Pa  %7.0f t%s\n",clock(),ship.geodetic().height_meters()/1e3,std::hypot(ship.velocity_meters_per_second().x(),ship.velocity_meters_per_second().y(),ship.velocity_meters_per_second().z()),
                ship.dynamic_pressure_pascals(),ship.mass_kilograms()/1e3,spawned?(std::string("; booster h ")+std::to_string(static_cast<int>(boost.geodetic().height_meters()))+" m").c_str():"");}
        if(orbit&&(out.boosterLost||out.boosterDown)){std::printf(out.boosterDown?"Booster no mar; a nave segue.\n":"Booster perdido; a nave segue.\n");if(orbitOnly)return out;out.ship=flyHome(flight,ship,pace,warp,nav,faults,count.zero(),ShipReturn::Phase::orbit,csv.get(),pending);return out;}
        if(orbit&&back.phase()==BoosterReturn::Phase::caught&&t-caughtAt>=30){ // 30 s on the arms.
            out.maxQ=maxQ;out.maxQAt=maxQAt;out.caught=caughtAt;out.offAxis=offAxis(boost,back.up());std::tie(out.along,out.across)=aside(boost);out.speed=speedOf(boost);out.propellant=boost.propellant_kilograms();
            std::printf("Booster: a menor folga da torre, abaixo do topo dela, %.1f m.\n",out.towerClearance);
            std::printf("Max-Q %.0f Pa aos %.0f s. Booster capturado aos %.1f s; %.0f s depois, nos braços: %.2f m do eixo da torre, %.3f m/s, %.0f t de propelante restante.\n",maxQ,maxQAt,caughtAt,
                t-caughtAt,out.offAxis,out.speed,out.propellant/1e3);
            if(orbitOnly)return out;
            {api::FlightRequest q;q.set_flight_id("booster");api::Applied a;call([&](grpc::ClientContext& c){return flight.EndFlight(&c,q,&a);});} // Taken off the arms.
            out.ship=flyHome(flight,ship,pace,warp,nav,faults,count.zero(),ShipReturn::Phase::orbit,csv.get(),pending);return out;}
        if(clock()>1500)throw std::runtime_error("Missão não concluída em 25 minutos");
        // The booster's way back in real time, its coast too; with --landings only its last 20 km down to 20 s on the arms (or
        // its end away from them).
        const bool landing=spawned&&lostAt<0&&!out.boosterDown&&((back.phase()==BoosterReturn::Phase::coast&&boost.geodetic().height_meters()<20e3)
            ||(back.phase()>BoosterReturn::Phase::coast&&!(back.phase()==BoosterReturn::Phase::caught&&t-caughtAt>=20)));
        pace(ship.elapsed_seconds(),landing?1:pace.hurry);
    }
}
// The booster's way down alone, for sweeps of what its landing rides out (a test bench, ~40 s simulated a run): from a state
// falling engines first toward its landing burn (Gate: east and north of the tower's axis and height over its base, m; its
// velocity north, east and down, m/s; propellant, kg; the computers' seed) through its coast guidance, the burn, the final
// approach and the catch or the escape, the site as in the mission (the arms on its own flight). The defaults: the mission's
// booster as it lights its landing burn. wind: added to the air from the start (NED, m/s). Its outcome as Mission's booster
// fields (caught, at rest, down or lost and where, the tower's clearance, why given up, FDIR).
struct Gate {double east=22117.2,north=-2027.7,height=50019.6,vn=52.03,ve=-540.35,vd=963.74,propellant=163140;std::uint64_t seed=7;Vec wind;}; // The mission's at 50 km down (ALBERT_TRACE's GATE), its ascent as Flights 12-14's.
inline Mission landing(api::FlightService::Stub& flight,const Gate& g,std::vector<Fault> faults={},bool quiet=true){
    Mission out;api::CreateFlightRequest create;create.set_vehicle(api::CUSTOM);*create.mutable_definition()=superHeavy();
    create.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);addTower(create);avionics(create,g.seed);
    const double la=towerLatitude*deg,r=6378137;auto* w=create.mutable_start();w->set_stage(0);w->set_propellant_kilograms(g.propellant);auto* l=w->mutable_local();
    l->mutable_position()->set_latitude_degrees(towerLatitude+g.north/r/deg);l->mutable_position()->set_longitude_degrees(towerLongitude+g.east/(r*std::cos(la))/deg);
    l->mutable_position()->set_height_meters(towerGround+g.height);put(l->mutable_velocity_ned_meters_per_second(),g.vn,g.ve,g.vd);
    l->set_heading_degrees(std::atan2(-g.ve,-g.vn)/deg);l->set_pitch_degrees(std::atan2(g.vd,std::hypot(g.vn,g.ve))/deg); // Engines first: its nose against the way it falls.
    api::FlightSample boost;call([&](grpc::ClientContext& c){return flight.CreateFlight(&c,create,&boost);});
    if(norm(g.wind)>0){api::StepRequest q;q.set_ticks(1);put(q.add_commands()->mutable_gust_ned_meters_per_second(),g.wind.x,g.wind.y,g.wind.z);call([&](grpc::ClientContext& c){return flight.Step(&c,q,&boost);});}
    Navigator nav;nav.base=towerSite();nav.beacon=towerCrown();nav.beaconNed=towerNed();nav.design(superHeavy());nav.align(boost,g.seed);
    BoosterReturn back(towerSite(),towerLatitude*deg,towerLongitude*deg,armHeight,pinRest());siteWorks(back);back.enter(BoosterReturn::Phase::coast);
    static const char* names[]{"wait","flip","boostback","coast","landing","final","hover","descend","caught","divert","down"};
    bool shut=false;double caughtAt=-1;auto last=back.phase();
    std::pair<std::string,std::string> pending;
    for(;;){const double t=boost.elapsed_seconds();
        back.armsClosed=armsClosed(boost);armAngles(back,boost);const Command c=back.step(t,nav.state(boost));api::StepRequest b;b.set_ticks(20);apply(b,c);
        if(!pending.first.empty()){decide(b,pending.first,pending.second);pending={};}
        inject(faults,"booster",t,names[static_cast<int>(back.phase())],b);if(back.closeArms()!=shut){shut=back.closeArms();arms(b,!shut);}
        if(back.gaveUp()&&out.gaveUp.empty()){out.gaveUp=back.gaveUp();if(!quiet)std::printf("%7.2f s  booster: captura cancelada (%s)\n",t,back.gaveUp());decide(b,"captura cancelada",back.gaveUp());}
        if(c.terminate){if(!quiet)std::printf("%7.2f s  booster: FTS autônomo (%s)\n",t,back.termination());decide(b,"FTS autônomo",back.termination());}
        try{call([&](grpc::ClientContext& ctx){return flight.Step(&ctx,b,&boost);});}
        catch(const std::exception&){api::StepRequest bare;bare.set_ticks(20);if(c.terminate)bare.add_commands()->set_terminate(true);if(b.has_decision())decide(bare,b.decision().what(),b.decision().why());call([&](grpc::ClientContext& ctx){return flight.Step(&ctx,bare,&boost);});} // A refused engine command cannot swallow FTS.
        nav.update(boost);for(const auto& r:nav.reports){out.fdir.push_back("booster: "+r);if(!quiet){std::printf("%7.2f s  FDIR: %s\n",t,r.c_str());pending={"FDIR",r};}}nav.reports.clear();
        if(!quiet&&std::getenv("ALBERT_TRACE")&&std::fmod(t+1e-9,.5)<.02){const auto [e,n]=aside(boost);const State x=state(boost);const Vec u=back.up(),east{-std::sin(towerLongitude*deg),std::cos(towerLongitude*deg),0},h=x.velocity+u*-dot(x.velocity,u);int lit=0;
            const Vec nn=cross(u,east);double ro=std::atan2(dot(x.toBody.r2,nn),dot(x.toBody.r1,nn));if(ro>pi/2)ro-=pi;else if(ro<=-pi/2)ro+=pi;
            for(const auto& en:boost.engines())lit+=en.on();std::printf("TRACE %.1f %s e %.1f n %.1f h %.1f ve %.2f vn %.2f vz %.2f lit %d prop %.0f arms %.1f push %.2f roll %.1f rate %.3f tilt %.1f\n",t,names[static_cast<int>(back.phase())],e,n,
                boost.geodetic().height_meters()-towerGround,dot(h,east),dot(h,cross(u,east)),dot(x.velocity,u),lit,boost.propellant_kilograms()/1e3,boost.structure_radians_size()?boost.structure_radians(0)/deg:0.,norm(back.disturbance()),ro/deg,x.rate.x,
                std::acos(std::clamp(dot(x.toBody.r0,unit(x.position)),-1.,1.))/deg);}
        if(!quiet&&std::getenv("ALBERT_PROFILE")&&std::fmod(t+1e-9,1.)<.02){const auto& v=boost.velocity_ned_meters_per_second();const auto& f=boost.specific_force_meters_per_second2(); // ALBERT_PROFILE: the descent's profile, once a second.
            const double across=std::hypot(v.x(),v.y());std::printf("PROFILE %.0f %s h %.0f m v %.0f m/s (across %.0f, down %.0f) path %.1f° from vertical, q %.0f kPa, sensed %.1f m/s²\n",t,names[static_cast<int>(back.phase())],
                boost.geodetic().height_meters()-towerGround,std::hypot(across,v.z()),across,v.z(),std::atan2(across,v.z())/deg,boost.dynamic_pressure_pascals()/1e3,std::hypot(f.x(),f.y(),f.z()));}
        out.towerClearance=std::min(out.towerClearance,clearance(boost,boosterLength));if(back.gaveUp()&&out.gaveUp.empty())out.gaveUp=back.gaveUp();
        if(back.phase()!=last){last=back.phase();if(last==BoosterReturn::Phase::caught)caughtAt=t;
            if(!quiet){const auto [e,n]=aside(boost);std::printf("%7.2f s  %s (h %.0f m, %.0f m a leste, %.0f m ao norte, %.0f t)\n",t,names[static_cast<int>(last)],boost.geodetic().height_meters()-towerGround,e,n,boost.propellant_kilograms()/1e3);
                static const char* labels[]{"espera","giro","boostback","costa","queima de pouso","aproximação final","pairando","descendo","capturado","desvio","no chão"};
                char why[256];std::snprintf(why,sizeof why,"h %.0f m, %.0f m a leste, %.0f m ao norte, %.0f t",boost.geodetic().height_meters()-towerGround,e,n,boost.propellant_kilograms()/1e3);pending={labels[static_cast<int>(last)],why};}}
        if(broken(boost)){out.boosterLost=true;out.boosterAway=offAxis(boost,back.up());std::tie(out.downEast,out.downNorth)=aside(boost);return out;}
        if(back.gaveUp()&&(back.phase()==BoosterReturn::Phase::down||boost.landed())){out.boosterDown=true;out.boosterAway=offAxis(boost,back.up());std::tie(out.downEast,out.downNorth)=aside(boost);return out;}
        if(boost.landed()||boost.geodetic().height_meters()<towerGround){out.boosterLost=true;out.boosterAway=offAxis(boost,back.up());std::tie(out.downEast,out.downNorth)=aside(boost);return out;} // Down without giving up: on the site.
        if(caughtAt>=0&&t-caughtAt>=30){out.caught=caughtAt;out.offAxis=offAxis(boost,back.up());std::tie(out.along,out.across)=aside(boost);out.speed=speedOf(boost);out.propellant=boost.propellant_kilograms();return out;}
        if(t>300){out.gaveUp=out.gaveUp.empty()?"(tempo esgotado)":out.gaveUp;return out;}
    }
}
// The stack on the mount at Starbase, not launched, the camera going once around it in `seconds` (distance: times the
// default, which takes in the tower).
inline void onPad(api::FlightService::Stub& flight,api::CameraService::Stub& camera,double seconds=60,double distance=.5){
    api::CreateFlightRequest create;create.set_vehicle(api::CUSTOM);*create.mutable_definition()=stack();create.set_focus_camera(true);create.set_heading_degrees(heading);
    create.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);addTower(create,true);api::FlightSample s;call([&](grpc::ClientContext& c){return flight.CreateFlight(&c,create,&s);});
    std::printf("%s na plataforma de Starbase\n",s.vehicle().c_str());std::this_thread::sleep_for(std::chrono::seconds(4)); // The camera's arrival.
    {api::OrbitRequest o;o.set_distance_scale(distance);api::Applied a;call([&](grpc::ClientContext& c){return camera.Orbit(&c,o,&a);});}
    const auto start=std::chrono::steady_clock::now();double turned=0;
    for(double t=0;t<seconds;t=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()){
        api::OrbitRequest o;o.set_azimuth_radians(2*pi*t/seconds-turned);api::Applied a;call([&](grpc::ClientContext& c){return camera.Orbit(&c,o,&a);});turned=2*pi*t/seconds;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));}
}
// The ship alone, started in a 205 km circular orbit at the inclination a launch from Starbase toward `heading` gives, `ahead`
// degrees of orbit before the point over the tower: its way home without the day in orbit (its propellant: what the mission
// leaves it, ~189 t, unless given).
inline Home homecoming(api::FlightService::Stub& flight,bool fast,double warp=1000,double ahead=560,std::vector<Fault> faults={},double propellant=189e3){
    const double inc=std::acos(std::cos(towerLatitude*deg)*std::sin(heading*deg)),us=std::asin(std::min(std::sin(towerLatitude*deg)/std::sin(inc),1.)),u0=us-ahead*deg;
    const double r=6378137+205e3,speed=std::sqrt(earth.mu/r),period=2*pi*r/speed,turn=7.292115e-5*ahead/360*period;
    auto along=[&](double u){return std::atan2(std::cos(inc)*std::sin(u),std::cos(u));};double gone=along(us)-along(u0);while(gone<=0)gone+=2*pi;while(gone<ahead*deg-pi)gone+=2*pi;
    const double la=std::asin(std::sin(inc)*std::sin(u0)),lo=towerLongitude*deg+turn-gone,az=std::atan2(std::cos(inc),std::sin(inc)*std::cos(u0));
    api::CreateFlightRequest create;create.set_vehicle(api::CUSTOM);*create.mutable_definition()=shipAlone();create.set_focus_camera(true);
    create.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);addTower(create);
    auto* w=create.mutable_start();w->set_stage(0);w->set_propellant_kilograms(propellant);auto* l=w->mutable_local();
    l->mutable_position()->set_latitude_degrees(la/deg);l->mutable_position()->set_longitude_degrees(std::remainder(lo,2*pi)/deg);l->mutable_position()->set_height_meters(205e3);
    put(l->mutable_velocity_ned_meters_per_second(),speed*std::cos(az),speed*std::sin(az)-7.292115e-5*r*std::cos(la),0);l->set_heading_degrees(az/deg);
    avionics(create,11);api::FlightSample ship;call([&](grpc::ClientContext& c){return flight.CreateFlight(&c,create,&ship);});
    std::printf("%s em órbita, %.0f° antes de passar sobre Starbase, %s\n",ship.vehicle().c_str(),ahead,fast?"o mais rápido possível":"tempo real");
    Navigator nav;nav.base=towerSite();nav.beacon=towerCrown();nav.beaconNed=towerNed();nav.design(create.definition());nav.align(ship,11); // As if it had been navigating since launch.
    Pacer pace;pace.fast=fast;pace.last=ship.elapsed_seconds();return flyHome(flight,ship,pace,warp,nav,std::move(faults));
}
// The ship alone from where its belly flop begins (the bench for the way down from there: seconds a run): over the tower's
// ground (m east and north of its axis, up), moving (NED, m/s), belly-first at `alpha` along its way, its heading that way; its
// propellant and its RCS's gas as a mission leaves them there. The default: a homecoming's (ENTRY at 300 m/s).
struct ShipGate {double east=-943,north=-1957,height=17.5e3,vn=59.8,ve=198.2,vd=233.6,propellant=181e3,gas=1500,alpha=75;std::uint64_t seed=11;};
inline Home falling(api::FlightService::Stub& flight,const ShipGate& g,std::vector<Fault> faults={}){
    api::CreateFlightRequest create;create.set_vehicle(api::CUSTOM);*create.mutable_definition()=shipAlone();create.mutable_definition()->mutable_stages(0)->set_thruster_gas_kilograms(g.gas);
    create.set_weather(api::CreateFlightRequest::STANDARD_ATMOSPHERE);addTower(create);avionics(create,g.seed);
    const double la=towerLatitude*deg,r=6378137;auto* w=create.mutable_start();w->set_stage(0);w->set_propellant_kilograms(g.propellant);auto* l=w->mutable_local();
    l->mutable_position()->set_latitude_degrees(towerLatitude+g.north/r/deg);l->mutable_position()->set_longitude_degrees(towerLongitude+g.east/(r*std::cos(la))/deg);
    l->mutable_position()->set_height_meters(towerGround+g.height);put(l->mutable_velocity_ned_meters_per_second(),g.vn,g.ve,g.vd);
    l->set_heading_degrees(std::atan2(g.ve,g.vn)/deg);l->set_pitch_degrees(g.alpha-std::atan2(g.vd,std::hypot(g.vn,g.ve))/deg);l->set_roll_degrees(0); // Its nose alpha above its way, belly down.
    api::FlightSample ship;call([&](grpc::ClientContext& c){return flight.CreateFlight(&c,create,&ship);});
    Navigator nav;nav.base=towerSite();nav.beacon=towerCrown();nav.beaconNed=towerNed();nav.design(create.definition());nav.align(ship,g.seed);
    Pacer pace;pace.fast=true;pace.last=ship.elapsed_seconds();return flyHome(flight,ship,pace,1,nav,std::move(faults),0,ShipReturn::Phase::entry);
}
}
