#pragma once
namespace albert::astro {
double julianTT(double julianUTC);
double julianTDB(double julianUTC);
double utcToTdb(double unixUtcSeconds);
double tdbToUtc(double julianTDB);
}
