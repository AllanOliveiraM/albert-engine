#!/usr/bin/env python3
"""Reproduce the illustrative orientation of a body without an IAU rotation model.

Offline, standard library only:
  python3 scripts/anchor-orientation.py hyperion
Hyperion rotates chaotically, so pck00011 has no orientation for it. The inventory
keeps one instantaneous Cassini spin solution (pole and rate) fixed, and anchors the
rotation phase on a sub-solar point observed in the shape model's own frame. The
Sun direction uses Standish's approximate Saturn elements; the published spin
solutions must reproduce their listed sub-solar latitudes, which checks it.
"""
import datetime,json,math,pathlib,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
# Bauer & Thomas (2007), instantaneous spin solutions: (JD UTC mid-interval, RA, Dec, listed sub-solar latitude).
SOLUTIONS=[(2453531.59,208.4,35.3,-14.1),(2453598.96,271.3,35.6,27.0),(2453915.14,184.5,34.8,-43.7)]

def saturn_direction(jd):
 # Standish (JPL) Keplerian elements, 1800-2050, J2000 ecliptic -> ICRF unit vector Sun->Saturn.
 T=(jd-2451545.0)/36525
 a=9.53667594-0.00125060*T;e=0.05386179-0.00050991*T;i=math.radians(2.48599187+0.00193609*T)
 L=49.95424423+1222.49362201*T;w=92.59887831-0.41897216*T;node=113.66242448-0.28867794*T
 M=math.radians((L-w+180)%360-180);E=M
 for _ in range(30):E-=(E-e*math.sin(E)-M)/(1-e*math.cos(E))
 x,y=a*(math.cos(E)-e),a*math.sqrt(1-e*e)*math.sin(E);om,O=math.radians(w-node),math.radians(node)
 X=(math.cos(om)*math.cos(O)-math.sin(om)*math.sin(O)*math.cos(i))*x+(-math.sin(om)*math.cos(O)-math.cos(om)*math.sin(O)*math.cos(i))*y
 Y=(math.cos(om)*math.sin(O)+math.sin(om)*math.cos(O)*math.cos(i))*x+(-math.sin(om)*math.sin(O)+math.cos(om)*math.cos(O)*math.cos(i))*y
 Z=math.sin(om)*math.sin(i)*x+math.cos(om)*math.sin(i)*y;eps=math.radians(23.43928)
 v=(X,Y*math.cos(eps)-Z*math.sin(eps),Y*math.sin(eps)+Z*math.cos(eps));n=math.sqrt(sum(c*c for c in v))
 return [c/n for c in v]

def equator_frame(ra,dec,sun):
 # Sun direction rotated by Rx(90-dec) Rz(90+ra): body equator, prime meridian not yet applied.
 a,d=math.radians(90+ra),math.radians(90-dec);x,y,z=sun
 x,y=x*math.cos(a)+y*math.sin(a),-x*math.sin(a)+y*math.cos(a)
 y,z=y*math.cos(d)+z*math.sin(d),-y*math.sin(d)+z*math.cos(d)
 return x,y,z

def subsolar_latitude(ra,dec,jd):
 return math.degrees(math.asin(equator_frame(ra,dec,[-c for c in saturn_direction(jd)])[2]))

def main():
 body=sys.argv[1] if len(sys.argv)==2 else sys.exit(__doc__)
 record=next(b for b in json.loads((ROOT/'assets/moon-sources.json').read_text())['bodies'] if b['id']==body);o=record['orientation'];anchor=o['anchor']
 for jd,ra,dec,listed in SOLUTIONS:
  if abs(subsolar_latitude(ra,dec,jd)-listed)>1:raise SystemExit('Sun direction does not reproduce a published spin solution')
 utc=datetime.datetime.fromisoformat(anchor['utc']);jd=2440587.5+utc.replace(tzinfo=datetime.timezone.utc).timestamp()/86400
 latitude=subsolar_latitude(o['pole_ra_deg'],o['pole_dec_deg'],jd)
 # Sub-solar east longitude = atan2(y, x) - W in the rotating frame.
 x,y,_=equator_frame(o['pole_ra_deg'],o['pole_dec_deg'],[-c for c in saturn_direction(jd)])
 days=jd+anchor['tdb_minus_utc_s']/86400-2451545.0;w=(math.degrees(math.atan2(y,x))+anchor['subsolar_west_lon_deg']-o['rate_deg_per_day']*days)%360
 print('sub-solar latitude: pole %.2f deg, observed %.2f deg; W0 %.4f deg (inventory %.4f)'%(latitude,anchor['subsolar_lat_deg'],w,o['w0_deg']))
 if abs(latitude-anchor['subsolar_lat_deg'])>.5:raise SystemExit('Pole does not reproduce the observed sub-solar latitude')
 if abs((w-o['w0_deg']+180)%360-180)>.001:raise SystemExit('Inventory W0 differs from the anchored phase')

if __name__=='__main__':main()
