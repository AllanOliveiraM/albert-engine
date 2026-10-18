#pragma once
#include "physics/vector.hpp"
#include <array>
#include <cstdint>
namespace albert::physics {
// Time: TT nanoseconds since J2000.0 (2000-01-01 12:00:00 TT); exact fixed steps, ±292 years.
using Tt=std::int64_t;
constexpr Tt second=1000000000;
constexpr double secondsSinceJ2000(Tt t){return static_cast<double>(t/second)+static_cast<double>(t%second)*1e-9;}
Tt ttFromUtc(double unixSeconds); // ERFA leap-second table.
double utcFromTt(Tt t);

struct Planet {
    double gm,radius,flattening,rate; // Ellipsoid a (m), f, sidereal rate (rad/s).
    double gravityRadius;             // Reference radius of the zonal coefficients.
    std::array<double,5> zonal{};     // J0..J4 (J0, J1 unused).
    int degree=0;                     // 0 point mass; 2..4 zonal; with egm2008, 2..70 (degree and order).
    bool egm2008=false;               // EGM2008 tide-free spherical harmonics (its own GM and radius).
    double e2()const{return flattening*(2-flattening);}
};
// WGS 84 ellipsoid and rotation; EGM2008 tide-free GM, 20×20 field (J2..J4 kept for zonal use).
constexpr Planet earth{3.986004415e14,6378137.,1/298.257223563,7.292115e-5,6378136.3,{0,0,1.0826261738522e-3,-2.532410518567e-6,-1.619897599917e-6},20,true};
constexpr int egm2008Maximum=70;

struct Geodetic {double latitude,longitude,height;}; // rad, rad, m above the ellipsoid.
Geodetic geodetic(const Planet& p,V3 fixed); // Vermeille (2004), closed form.
V3 fixedPosition(const Planet& p,Geodetic g);
M3 nedFromFixed(double latitude,double longitude); // Rows: north, east, down.
V3 gravitation(const Planet& p,V3 fixed);           // Attraction only, body-fixed axes (m/s²).
V3 harmonics(int degree,V3 fixed);                 // EGM2008 to degree × order (Cunningham recursion).

// Inertial (GCRF) to body-fixed. Uniform: rotation about z from the epoch (check cases).
// IAU: CIO-based IAU 2006/2000A bias-precession-nutation (cached 60 s) and Earth rotation
// angle; polar motion neglected (≤15 m).
struct Orientation {M3 fixed;V3 omega;}; // ECI→ECEF matrix; Earth angular velocity in ECI.
class Rotation {
public:
    static Rotation uniform(double rate,double angle=0,Tt epoch=0){Rotation r;r.rate_=rate;r.angle_=angle;r.epoch_=epoch;return r;}
    static Rotation iau(double ttMinusUt1){Rotation r;r.iau_=true;r.deltaT_=static_cast<Tt>(std::llround(ttMinusUt1*1e9));return r;}
    Orientation at(Tt t)const;
    bool iau()const{return iau_;}
private:
    bool iau_=false;double rate_=0,angle_=0;Tt epoch_=0,deltaT_=0;
    mutable Tt cached_=INT64_MIN;mutable M3 c2i_,c2i1_;
};
// Geocentric Sun and Moon (GCRS, m) from ERFA epv00/moon98; TDB taken as TT (<2 ms).
struct Perturbers {V3 sun,moon;};
Perturbers perturbers(Tt t);
constexpr double sunGM=1.32712440018e20,moonGM=4.902800066e12;
inline V3 tidal(V3 r,V3 body,double gm){const V3 d=body-r;const double dn=norm(d),bn=norm(body);return gm*(d/(dn*dn*dn)-body/(bn*bn*bn));}
}
