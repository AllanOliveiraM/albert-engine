#include "astro_time.hpp"
#include <erfa.h>
#include <cmath>
#include <stdexcept>
namespace albert::astro {
namespace {
constexpr double epoch=2440587.5;
void valid(double jd){if(!std::isfinite(jd)||jd<0||jd>1e8)throw std::invalid_argument("Invalid astronomical epoch");}
}
double julianTT(double utc){valid(utc);double a,b,c,d;if(eraUtctai(2400000.5,utc-2400000.5,&a,&b)<0||eraTaitt(a,b,&c,&d)!=0)throw std::runtime_error("UTC/TAI/TT conversion failed");return c+d;}
double julianTDB(double utc){const double tt=julianTT(utc);return tt+eraDtdb(2400000.5,tt-2400000.5,0,0,0,0)/86400.;}
double utcToTdb(double seconds){if(!std::isfinite(seconds))throw std::invalid_argument("Invalid UTC timestamp");return julianTDB(epoch+seconds/86400.);}
double tdbToUtc(double tdb){valid(tdb);double tt=tdb;for(int i=0;i<2;++i)tt=tdb-eraDtdb(2400000.5,tt-2400000.5,0,0,0,0)/86400.;double a,b,c,d;if(eraTttai(2400000.5,tt-2400000.5,&a,&b)!=0||eraTaiutc(a,b,&c,&d)<0)throw std::runtime_error("TDB/TT/TAI/UTC conversion failed");return ((c-epoch)+d)*86400.;}
}
