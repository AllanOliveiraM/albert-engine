#pragma once
namespace albert::physics {
// Horizontal Wind Model 2014 (Drob et al. 2015, doi:10.1002/2014EA000089), ported from NRL's
// HWM14.123114 Fortran: quiet-time climatology from the ground to ~450 km (flat above 350 km) plus
// the disturbance winds DWM07 (above ~110 km) for a 3-hour ap ≥ 0; negative ap: quiet only.
// Inputs are taken in single precision, as the model's; heights below 0 count as 0.
struct HorizontalWind {double north=0,east=0;}; // m/s.
HorizontalWind hwm14(int dayOfYear,double secondsUt,double heightKm,double latitudeDeg,double longitudeDeg,double ap);
// The disturbance part alone, geographic (dwm07) and in quasi-dipole coordinates (dwm07b: magnetic
// local time in hours, quasi-dipole latitude in degrees, Kp).
HorizontalWind dwm07(int dayOfYear,double secondsUt,double heightKm,double latitudeDeg,double longitudeDeg,double ap);
HorizontalWind dwm07b(double mltHours,double mlatDeg,double kp);
}
