#include "physics/sensors.hpp"
#include <cmath>
#include <stdexcept>
namespace albert::physics {
namespace {
std::uint64_t splitmix(std::uint64_t& x){std::uint64_t z=(x+=0x9e3779b97f4a7c15ull);z=(z^(z>>30))*0xbf58476d1ce4e5b9ull;z=(z^(z>>27))*0x94d049bb133111ebull;return z^(z>>31);}
std::uint64_t rotl(std::uint64_t x,int k){return x<<k|x>>(64-k);}
constexpr double g0=9.80665,deg=3.14159265358979323846/180,hour=3600;
}
Noise::Noise(std::uint64_t seed){for(auto& v:s_)v=splitmix(seed);}
std::uint64_t Noise::next(){
    const std::uint64_t r=rotl(s_[1]*5,7)*9,t=s_[1]<<17;
    s_[2]^=s_[0];s_[3]^=s_[1];s_[1]^=s_[2];s_[0]^=s_[3];s_[2]^=t;s_[3]=rotl(s_[3],45);return r;
}
double Noise::gaussian(){
    if(spare_){spare_=false;return cached_;}
    double u=uniform();while(u<=0)u=uniform();
    const double r=std::sqrt(-2*std::log(u)),a=2*3.14159265358979323846*uniform();
    cached_=r*std::sin(a);spare_=true;return r*std::cos(a);
}
ImuModel imuModel(ImuGrade grade){ // Representative datasheet values, not a specific product.
    ImuModel m;
    if(grade==ImuGrade::tactical){
        m.gyroscope={.1*deg/std::sqrt(hour),1.07*1*deg/hour,300,.01*deg/hour/std::sqrt(hour),3*deg/hour,100e-6};
        m.accelerometer={.05/std::sqrt(hour),1.07*50e-6*g0,300,1e-6*g0,500e-6*g0,300e-6};m.pressureNoise=2;m.pressureBias=20;
    }else if(grade==ImuGrade::industrial){
        m.gyroscope={.3*deg/std::sqrt(hour),1.07*10*deg/hour,100,.1*deg/hour/std::sqrt(hour),30*deg/hour,500e-6};
        m.accelerometer={.1/std::sqrt(hour),1.07*100e-6*g0,100,5e-6*g0,2e-3*g0,1e-3};m.pressureNoise=5;m.pressureBias=50;
    }
    return m;
}
Imu::Imu(const ImuModel& m,std::uint64_t seed):model_(m),noise_(seed){
    for(int i=0;i<6;++i){const Channel& c=i<3?m.accelerometer:m.gyroscope;Axis& a=axes_[static_cast<std::size_t>(i)];
        a.bias=c.bias*noise_.gaussian();a.scale=c.scale*noise_.gaussian();a.markov=c.instability*noise_.gaussian();}
    pressureBias_=m.pressureBias*noise_.gaussian();
}
double Imu::error(const Channel& c,Axis& a,double dt,double& white){
    if(c.walk>0)a.walk+=c.walk*std::sqrt(dt)*noise_.gaussian();
    if(c.instability>0){const double k=std::exp(-dt/c.correlation);a.markov=k*a.markov+c.instability*std::sqrt(1-k*k)*noise_.gaussian();}
    white=c.white>0?c.white/std::sqrt(dt)*noise_.gaussian():0.;
    return a.bias+a.markov+a.walk;
}
Imu::Reading Imu::measure(double dt,V3 f,V3 w,V3 dv,V3 da,double p){
    if(!(dt>0))throw std::invalid_argument("IMU interval must be positive");
    Reading r;double fv[3]{f.x,f.y,f.z},wv[3]{w.x,w.y,w.z},dvv[3]{dv.x,dv.y,dv.z},dav[3]{da.x,da.y,da.z},of[3],ow[3],odv[3],oda[3];
    for(int i=0;i<3;++i){double n=0;
        const double ba=error(model_.accelerometer,axes_[static_cast<std::size_t>(i)],dt,n),sa=1+axes_[static_cast<std::size_t>(i)].scale;of[i]=sa*fv[i]+ba+n;odv[i]=sa*dvv[i]+(ba+n)*dt;
        const double bg=error(model_.gyroscope,axes_[static_cast<std::size_t>(i+3)],dt,n),sg=1+axes_[static_cast<std::size_t>(i+3)].scale;ow[i]=sg*wv[i]+bg+n;oda[i]=sg*dav[i]+(bg+n)*dt;}
    r.specificForce={of[0],of[1],of[2]};r.rate={ow[0],ow[1],ow[2]};r.deltaVelocity={odv[0],odv[1],odv[2]};r.deltaAngle={oda[0],oda[1],oda[2]};
    r.pressure=p>0?std::max(0.,p+pressureBias_+model_.pressureNoise*noise_.gaussian()):0.;
    return r;
}
Gnss::Gnss(const GnssModel& m,std::uint64_t seed):model_(m),noise_(seed){drift_={m.drift*noise_.gaussian(),m.drift*noise_.gaussian(),m.drift*noise_.gaussian()};}
Gnss::Reading Gnss::measure(double time,double dt,V3 p,V3 v,const M3& ned,double height,double speed,bool available){
    if(model_.drift>0&&dt>0){const double k=std::exp(-dt/model_.correlation),w=model_.drift*std::sqrt(1-k*k);drift_={k*drift_.x+w*noise_.gaussian(),k*drift_.y+w*noise_.gaussian(),k*drift_.z+w*noise_.gaussian()};}
    const V3 e{model_.horizontal*noise_.gaussian(),model_.horizontal*noise_.gaussian(),model_.vertical*noise_.gaussian()},u{model_.velocity*noise_.gaussian(),model_.velocity*noise_.gaussian(),model_.velocity*noise_.gaussian()};
    Reading r;r.time=time; // The noise is drawn whether or not there is a fix: the sequence does not depend on availability.
    if(!available||(model_.maxHeight>0&&height>model_.maxHeight)||(model_.maxSpeed>0&&speed>model_.maxSpeed))return r;
    const M3 back=transpose(ned);r.valid=true;r.position=p+back*(e+drift_);r.velocity=v+back*u;return r;
}
RadarAltimeter::RadarAltimeter(const RadarModel& m,std::uint64_t seed):model_(m),noise_(seed){}
RadarAltimeter::Reading RadarAltimeter::measure(double time,double height,V3 b){
    Reading r;r.time=time;const double n=noise_.gaussian(),c=b.z; // Cosine to the downward vertical.
    if(!(height>0)||c<=std::cos(model_.halfAngle))return r;
    const double range=height/c;if(range>model_.maxRange)return r;
    r.valid=true;r.range=std::max(0.,range+std::max(model_.floor,model_.fraction*range)*n);return r;
}
StarTracker::StarTracker(const StarTrackerModel& m,std::uint64_t seed):model_(m),noise_(seed){}
StarTracker::Reading StarTracker::measure(double time,Q q,V3 sun,V3 body,double bodyRadius,V3 rate){
    Reading r;r.time=time;const double a=model_.cross*noise_.gaussian(),b=model_.cross*noise_.gaussian(),c=model_.roll*noise_.gaussian();
    const V3 bore=model_.boresight/norm(model_.boresight),look=matrix(q)*bore; // Inertial.
    if(dot(look,sun)>std::cos(model_.sunExclusion)||dot(look,body)>std::cos(std::min(bodyRadius+model_.bodyMargin,3.14159265358979323846))||norm(rate)>model_.maxRate)return r;
    const V3 helper=std::abs(bore.x)<.9?V3{1,0,0}:V3{0,1,0},u=cross(bore,helper)/norm(cross(bore,helper)),w=cross(bore,u);
    const V3 e=u*a+w*b+bore*c;const double angle=norm(e); // A small body-axis rotation error.
    const Q d=angle>0?Q{std::cos(angle/2),e.x/angle*std::sin(angle/2),e.y/angle*std::sin(angle/2),e.z/angle*std::sin(angle/2)}:Q{1,0,0,0};
    r.valid=true;r.attitude=normalized(q*d);return r;
}
}
