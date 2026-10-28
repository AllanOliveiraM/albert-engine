"""The local WGS84 east/north projection shared by Starbase's drawn OSM layers."""
import math

def metres_per_degree(latitude):
    a=6378137.0;e2=6.69437999014e-3;ph=math.radians(latitude)
    return (a/math.sqrt(1-e2*math.sin(ph)**2)*math.cos(ph)*math.pi/180,
            a*(1-e2)/(1-e2*math.sin(ph)**2)**1.5*math.pi/180)

def enu(point,latitude,longitude):
    mlon,mlat=metres_per_degree(latitude)
    return ((point['lon']-longitude)*mlon,(point['lat']-latitude)*mlat)
