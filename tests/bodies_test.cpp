#include "core/bodies.hpp"
#include "core/local_system.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace albert::core;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    const auto* body=naturalBody("mercury");require(body&&body->horizonsId==199,"Mercury definition missing");
    Scene scene;scene.include(*body);require(scene.get("mercury").builtin,"Built-in ownership lost");
    bool protectedBody=false;try{scene.remove("mercury");}catch(const std::exception&){protectedBody=true;}require(protectedBody,"Built-in body removed externally");
    // CSPICE pxform(IAU_MERCURY,J2000), pck00011, generated independently.
    struct Fixture{double jd;double m[3][3];};
    const Fixture fixtures[]={
        {2451545.0, {{0.93150220629963498,0.35207063779646441,0.091377818208266209},{-0.27144843288744658,0.84008756363773163,-0.46964735036425953},{-0.24211441085127836,0.41267317749377763,0.8781124419089662}}},
        {2440000.0, {{0.86059025215274065,-0.50101876650998123,0.091458260996473778},{0.48094209609711913,0.74038063734967552,-0.4696075084998087},{0.16756824908493748,0.44812577190171216,0.8781253751350675}}},
        {2461327.5, {{-0.012903450226339125,0.99573894637004245,0.091309647103325745},{-0.88226910382753765,0.031636906471216725,-0.46968109881097514},{-0.47056851722529197,-0.086620187201419618,0.87810148261210741}}},
        {2488070.5, {{0.024856014217967154,0.99552937344603953,0.091123241619973913},{-0.88039226116299862,0.064980814104610887,-0.46977330733302053},{-0.47359438873523274,-0.068547504728020056,0.87807152018162127}}},
    };
    for(const auto& fixture:fixtures){const auto frame=bodyFrame(*body,fixture.jd);
        const Vector columns[]{frame.prime,frame.east,frame.pole};
        for(int col=0;col<3;++col){const double values[]{columns[col].x,columns[col].y,columns[col].z};
            for(int row=0;row<3;++row)require(std::abs(values[row]-fixture.m[row][col])<2e-11,"Orientation differs from independent CSPICE reference");}
    }
    struct PlanetFixture {const char* body;double jd;double m[3][3];};
    const PlanetFixture planets[]{
#include "planet_frames_fixture.inc"
    };
    require(naturalBodies().size()==7,"Missing a Solar System planet");
    for(const auto& fixture:planets){const auto* definition=naturalBody(fixture.body);require(definition&&definition->polarRadiusMeters>0&&definition->polarRadiusMeters<=definition->radiusMeters,"Invalid planetary dimensions");const auto frame=bodyFrame(*definition,fixture.jd);const Vector columns[]{frame.prime,frame.east,frame.pole};for(int col=0;col<3;++col){const double values[]{columns[col].x,columns[col].y,columns[col].z};for(int row=0;row<3;++row)require(std::abs(values[row]-fixture.m[row][col])<2e-9,"Planet orientation differs from CSPICE");}}
    require(std::abs(surfaceRadius("mercury")-2439400.)<.001,"DEM datum radius changed");
    const PlanetFixture moons[]{
#include "moon_frames_fixture.inc"
    };
    require(surfaceBodies().size()==25&&naturalBodies().size()==7,"Surface catalogue changed online planet provider scope");
    for(const auto& fixture:moons){const auto* definition=surfaceBody(fixture.body);require(definition&&!naturalBody(fixture.body)&&category(fixture.body)==Category::Moon,"Textured moon was reclassified as a planet");
        require(surfaceRadius(fixture.body)==localOrbit(fixture.body)->radius,"Surface and orbital/collision radii disagree");
        const auto frame=bodyFrame(*definition,fixture.jd);const Vector columns[]{frame.prime,frame.east,frame.pole};
        for(int col=0;col<3;++col){const double values[]{columns[col].x,columns[col].y,columns[col].z};for(int row=0;row<3;++row)require(std::abs(values[row]-fixture.m[row][col])<2e-9,"Moon orientation differs from CSPICE");}}
    require(surfaceBody("triton")->meridian[1]<0&&surfaceBody("miranda")->partialMap&&surfaceBody("charon")->partialMap,"Retrograde rotation or partial imagery metadata lost");
    require(surfaceBody("phobos")->axesMeters==std::array<double,3>{13000,11400,9100}&&surfaceBody("deimos")->axesMeters==std::array<double,3>{7800,6000,5100},"Martian reference ellipsoids differ from NAIF pck00011");
    std::cout<<"Bodies OK: planets and mapped moons against CSPICE at four epochs, radii, ownership and partial coverage\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
