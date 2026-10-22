#pragma once
// The ship's way home after orbital insertion (starship.hpp: its flaps, RCS, heat shield and pins):
// - in orbit, nose forward and belly down by RCS pulses, until the pass that brings it back over the tower (about a day after
//   a launch from Starbase);
// - a deorbit burn on its 3 sea-level Raptors once an entry flown from there would end at the tower;
// - the entry belly-first (angle of attack 65° hypersonic to 80° subsonic), steered by bank angle onto the tower, the flaps
//   and the RCS holding the attitude;
// - the flip and the landing burn on 3 engines, down to the centre one once it alone carries it (none lit anew), the hover over
//   the arms, a disturbance observer netting what pushes it across, and the catch on them;
// - deciding all the way down whether the catch can still be made (the descent its engines can still make, the propellant to
//   finish, a course into the tower): once not, given up for good, away from the tower with all it has while anything
//   threatens it and down at sea east of it (TowerApproach: never onto the tower or the ground's works).
#include "starship.hpp"
namespace albert::example::starship {
class ShipReturn:public TowerApproach {
public:
    enum class Phase {orbit,orient,deorbit,glide,entry,flip,landing,hover,descend,caught,divert,down};
    ShipReturn(Vec site,double latitude,double longitude,double arms,double rest)
        :TowerApproach(site,latitude,longitude,arms,rest,shipLength,shipPinAt,combine(ship(true)),combine(ship(false)),shipPropellant){header_=shipHeader;headerAt_=shipHeaderAt;holdAlong=true;slide=3;} // From in front of the arms' V it slides in as the booster does, at 3 m/s at most.
    Phase phase()const{return phase_;}
    double nextStep()const{return next_;} // How far (s) the client may step next: long in orbit, 20 ms otherwise.
    double along()const{return along_;} // The latest predicted landing past the tower (m).
    double beside()const{return cross_;} // And right of it (m).
    double bank()const{return held_;} // Commanded (rad, right positive).
    double bankWanted()const{return bank_;} // What the guidance asks of it.
    void enter(Phase p){phase_=p;check_=0;held_=0;} // Its guidance taken up at a phase (a test starting it there, e.g. falling toward its flip).
    const char* gaveUp()const{return why_;} // Why the catch was given up (null while it is on).
    const char* escaping()const{return escape_?threat_:nullptr;} // Getting away with all it has: from what (null otherwise).
    double dragFactor()const{return drag_;} // The measured over the predicted (drag, lift).
    double liftFactor()const{return lift_;}
    double deorbit=100,crossLimit=150e3; // The burn's Δv (m/s); the farthest a pass may run beside the tower (m).
    Command step(double t,const State& s){
        observe(s,t-seen_,(phase_>=Phase::landing&&phase_<=Phase::descend)||phase_==Phase::divert);seen_=t;
        Command c;next_=.02;const Vec rel=s.position+site_*-1;const double over=dot(rel,up_),height=norm(s.position)-norm(site_);const Vec flat=rel+up_*-over;route(flat);
        if(catch_&&phase_>=Phase::landing&&phase_<=Phase::descend){const double weight=s.mass*norm(gravity(s.position)),usable=s.propellant-residual,second=weight/raptorExhaust;
            // One sea-level Raptor at its least lifting more than it weighs, it cannot come down onto the arms; short of what
            // finishing takes (down to the arms at 1 m/s, 3 s more), away while that can still be done, else onto them anyway.
            if(set_.working(s,3)<1)giveUp(s,"sem motores para a captura");
            else if(phase_!=Phase::landing&&.4*raptorSea>.97*weight)giveUp(s,"leve demais para descer nos braços");
            else if(const double vz=dot(s.velocity,up_),g=norm(gravity(s.position)),left=std::max(over-catchHeight(s),0.),
                finish=phase_==Phase::landing?1.1*s.mass*(norm(s.velocity)+g*(2*left/std::max(-vz,1.)+17))/raptorExhaust:(left+3)*second; // Its Δv from up high (as the booster's final), or down at 1 m/s and 3 s more.
                !hurry_&&(usable<finish||usable<escapeNeed(s,flat)+3*second)){ // Short of finishing, or the bingo: as the booster's.
                if(usable<finish?!escapes(s,flat):phase_==Phase::descend||aligned(s,flat))hurry_=true;
                else giveUp(s,usable<finish?"propelante insuficiente para terminar a captura":"propelante no limite: captura abandonada enquanto a fuga é possível");}}
        // Never into the tower, never down onto the ground's works (as the booster's).
        if(phase_>=Phase::landing&&phase_<=Phase::descend&&!(phase_==Phase::descend&&norm(s.contact)>0)||phase_==Phase::divert){
            const Vec across=s.velocity+up_*-dot(s.velocity,up_);const double brake=stopping(s,flat,std::max(set_.count(),1),rush(s));(void)across;
            if(const char* why=into(collides(s,flat,(phase_==Phase::landing?.45:.7)*brake,phase_==Phase::divert?std::min(kept_,over):catchHeight(s)+hoverAbove));why&&!escape_){giveUp(s,why);if(phase_==Phase::divert){escape_=true;threat_=why;}}} // In the burn, braking assumed at less: an escape from its lean and fall gets less than all of it.
        switch(phase_){
        case Phase::orbit: // Where a deorbit burn now would land, each minute (each second within 2000 km of it); nose forward and
            // belly to the Earth, by a half-second pulse of the RCS every 30 s, torque-free between them (long steps).
            if(t>=check_){const Arrival a=plan(s);along_=a.along;cross_=a.cross;const bool pass=a.down&&std::abs(a.cross)<crossLimit&&a.along<0&&a.along>-2e6;
                check_=t+(pass?1.:60.);if(pass&&a.along>-1.2e6)phase_=Phase::orient;}
            c.torqueOn=true;if(t>=pulse_){c.torque=level(s);pulse_=t+30;next_=.5;}else next_=std::max(std::min(check_,pulse_)-t,.001);break;
        case Phase::orient: // Retrograde on the RCS; the burn once the landing it gives reaches the tower, pointed within 3°.
            hold(c,s,unit(inertial(s)*-1));
            if(t>=check_){const Arrival a=plan(s);along_=a.along;cross_=a.cross;check_=t+1;
                if(a.along>=0&&dot(bodyX(s),unit(inertial(s)*-1))>std::cos(3*deg)){burn_.use(c,s,3);c.throttle=.4;phase_=Phase::deorbit;v0_=norm(inertial(s));}}
            break;
        case Phase::deorbit: // Cut once the landing without more of it, at the planned bank, is back at the tower (within the pass's
            // 2000 km: a burn barely begun can come down a lap or more later, as far short).
            aim(c,s,unit(inertial(s)*-1));{const Arrival a=arrive(s.position,s.velocity,s.mass,50*deg);along_=a.along;cross_=a.cross;
                burn_.keep(c,s);if((a.down&&a.along<=0&&a.along>-2e6)||v0_-norm(inertial(s))>=2*deorbit){burn_.use(c,s,0);phase_=Phase::glide;}}break;
        case Phase::glide: // Torque-free down to 140 km, then belly-first at the entry attitude on the RCS; the main tanks vented
            // meanwhile down to the header tanks' load (what the landing takes, forward: the centre of mass the flaps are trimmed for).
            if(!vented_&&ventOn){c.vent=500;vented_=true;}
            if(height>140e3){c.torqueOn=true;c.torque={};next_=std::min(10.,(height-140e3)/std::max(-dot(s.velocity,unit(s.position)),1.));}
            else attitude(c,s,alpha(norm(s.velocity),height),0);
            if(height<100e3){phase_=Phase::entry;sign_=cross_>0?-1:1;check_=t;held_=0;}
            break;
        case Phase::entry:{ // Bank by a predictor: |σ| for the range, its side toward the tower within a corridor.
            const double speed=norm(s.velocity);
            if(speed<=300)fall(c,s,t,height); // The belly flop: falling, steered by tilting the belly.
            else{if(t>=check_){check_=t+1;guide(s);}held_+=std::clamp(bank_-held_,-4*deg*.02,4*deg*.02);attitude(c,s,alpha(speed,height),held_);} // The bank at most 4°/s.
            if(speed<150&&height<flipHeight(s)){phase_=Phase::flip;set_.use(c,s,3);c.throttle=.5;} // On its height alone: falling belly-first is never the way down.
            break;}
        case Phase::flip:{ // Nose up onto the engines: the forward flaps open, the aft ones folded, the gimbals; toward the burn's
            // braking lean (up to 20° against its way across: past upright when the flip's own push has it moving the nose's way)
            // rather than upright, the burn taken up from there as it nears it (no lean left over the wrong way to turn out of).
            const Vec v=s.velocity+up_*-dot(s.velocity,up_);const double lean=std::min(20*deg,std::atan(norm(v)/10));const Vec to=norm(v)>.1?unit(up_*std::cos(lean)+unit(v)*-std::sin(lean)):up_;
            set_.keep(c,s);aim(c,s,to,set_.centre());c.surfaces={45*deg,45*deg,-45*deg,-45*deg};c.throttle=.5;
            if(dot(bodyX(s),to)>std::cos(25*deg)&&dot(bodyX(s),up_)>std::cos(25*deg)){phase_=Phase::landing; // What the burn cannot bring in front of the arms (at rest) leaning 20° at most: away from the tower.
                const double above=over-catchHeight(s)-hoverAbove,vz=dot(s.velocity,up_),most=std::tan(20*deg)*((above>1&&vz<0?vz*vz/(2*above):0)+norm(gravity(s.position)));
                if(norm(flat+front()*-1.)>150||std::max(norm(across(s,flat,false,true)),norm(across(s,flat,true,true)))>1.3*most||closing(s,flat)>safe(s,flat,.6*most))giveUp(s,"pouso previsto fora do alcance da torre");} // Or closing on the tower faster than it could stop short of it.
            break;}
        case Phase::landing:{ // On 3 engines (the fewest that carry it at 85%, of those lit), the observer's push netted: first to the
            // hover point in front of the arms' V (across by E-guidance, no walk across after it); its last 12 s on their line in
            // front of the V, sliding in as it comes down (the booster's final approach, the real catches': along the line at what
            // it can still brake, onto(), at most 8 m/s; across it onto the line, no speed across on arrival, -6 n/T² - 4 v/T over
            // the time left), not below a 27° slope down to the axis from the approach, the arms closing round it as it comes in;
            // within 3 m of the axis, on down to the arms themselves at 0.5 m/s (no hover: straight on down onto them if aligned in
            // the shut arms; else held over them).
            const double vz=dot(s.velocity,up_),e=dot(flat,cross(north_,up_)),nn=dot(flat,north_);const Vec v=s.velocity+up_*-vz;
            const bool line=std::abs(nn)<=8&&e>-8&&e<80,in=line&&over-catchHeight(s)<150,axis=line&&norm(flat)<3; // in: on their line in front of the V, low: going in.
            // Going in: down the 27° slope (tan 0.5) whose apex is the arms themselves (3 m round the axis): no hover point on the way.
            const double above=in?over-catchHeight(s)-.5*std::max(norm(flat)-3,0.):over-catchHeight(s)-hoverAbove,vf=in?-.5:-1.,T=std::max(2*std::max(above,0.)/std::max(-vz-vf,1.),std::sqrt(6.)*3/bandwidth(s));
            const double brake=above<0?.5*(std::min(3.,-.3*above)-vz):vz<vf?(vz*vz-vf*vf)/(2*std::max(above,.1)):.5*(vf-vz),up=std::max(brake+norm(gravity(s.position)),0.),limit=std::tan(20*deg)*up;
            Vec side;
            if(in){const double kv=std::min(.6,bandwidth(s)/3),vn=dot(v,north_);side=onto(s,flat+north_*-nn,kv,8,20*deg);side=side+north_*(-dot(side,north_)-6*nn/(T*T)-4*vn/T);
                // The arms shut as it comes in: once where it will be when they are (braking to rest along over that time, its drift
                // across half gone; its centre of mass, upright by then: where the pins will be, not where its lean now puts them) lies
                // within the rails' room.
                const double ve=dot(v,cross(north_,up_)),shut=TowerApproach::closing,along=e+ve*shut/2,off=nn+vn*shut/2;close_=close_||(std::abs(off)<.5&&std::abs(vn)<.5&&along>-4&&along<8&&e<25&&norm(v)<7);}
            else{side=across(s,flat);if(closing(s,flat)>safe(s,flat,.5*limit))side=v*(-limit/std::max(norm(v),1e-3));} // Never at the tower faster than half its lean stops it short: all of it against its way.
            side=side+push_*-1.;if(norm(side)>limit)side=side*(limit/norm(side));
            set_.keep(c,s);const Vec a=up_*up+side;const double want=s.mass*norm(a);
            if(const int n=std::max(1,static_cast<int>(std::ceil(want/(.85*raptorSea))));n<set_.count())set_.use(c,s,n);
            c.throttle=std::clamp(want/(std::max(set_.count(),1)*raptorSea),.4,1.);
            aim(c,s,unit(a),set_.centre());roll(c,s,&set_);c.surfaces={-45*deg,-45*deg,-45*deg,-45*deg};
            if(axis&&over-catchHeight(s)<3){if(armsClosed&&aligned(s,flat))phase_=Phase::descend;else if(over-catchHeight(s)<1.5)phase_=Phase::hover;} // On down if aligned in the shut arms; else held (back up over them).
            else if(!in&&(above<2||vz>-1)){phase_=Phase::hover;if(norm(flat+front()*-1.)>30&&!in_)giveUp(s,"pouso previsto fora do alcance da torre");} // Not in front of the V: no walk across to it.
            break;}
        case Phase::hover:case Phase::descend: // Over the tower, then down at 1 m/s until the pins rest on the arms; off the axis or the
            // arms' line, held again.
            close_=centred(s,flat,phase_==Phase::descend);if(phase_==Phase::hover&&armsClosed&&aligned(s,flat))phase_=Phase::descend;
            if(const Vec p=pinsAt(s,flat);hurry_){if(onRails(p))close_=true;if(armsClosed&&inGrooves(p,.2))phase_=Phase::descend;} // Nothing left to get away on: onto the arms now.
            else if(phase_==Phase::descend&&(norm(s.contact)==0?std::abs(dot(p,north_))>.25||std::abs(dot(p,cross(north_,up_)))>5||std::abs(rollError(s))>6*deg:!inGrooves(p)))phase_=Phase::hover; // (Not hurrying: the branch above.)
            {set_.keep(c,s);const int n=std::max(set_.count(),1);const double demand=overArms(c,s,flat,over,phase_==Phase::descend,n,set_.centre(),&set_,hurry_?2:1,aloft(s,flat,shipLength-cg(s.propellant)));c.surfaces={-45*deg,-45*deg,-45*deg,-45*deg};
                // As light as it gets, 2 Raptors at their least lift more than its weight: the fewest that carry it at 80% (of those
                // lit), one more once short of 95% of what they give; going down onto the arms, two while two work (one alone, off the
                // axis, shoves it out of the grooves).
                if(const int k=std::max(phase_==Phase::descend?std::min(2,set_.working(s,3)):1,static_cast<int>(std::ceil(demand/(.8*raptorSea))));k<n)set_.use(c,s,k);else if(n<set_.working(s,3)&&demand>.95*n*raptorSea)set_.use(c,s,n+1);}
            if(phase_==Phase::descend&&norm(s.contact)>0&&inGrooves(pinsAt(s,flat),hurry_?.2:.15)){c.throttle=.4;phase_=Phase::caught;caught_=t;} // The pins in the arms' grooves (hurrying: inside the crests), not a closing arm on the hull.
            break;
        case Phase::caught: // On the arms, the engine at its least while they take the load (one at its least nearly carries it all),
            // the thrust's line through the pins, then off once it hangs still (or 3 s); slipping off in the first second, or its pins
            // off the grooves while lit: back off them and onto them again.
            if(!off_){if((norm(s.contact)==0&&t-caught_<1)||!inGrooves(pinsAt(s,flat),hurry_?.2:.15)){phase_=Phase::descend;break;}hang(c,s,&set_);c.throttle=.4;
                if(t-caught_>.5&&(still(s)||t-caught_>3)){set_.use(c,s,0);off_=true;}}
            else{c.torqueOn=true;c.torque={};}break;
        case Phase::divert:{ // Away with all it has while the tower threatens it (escape()); then toward the aim as fast as its lean
            // allows (20°, less where its hull's swing would near the tower), its height held until clear of the ground's works by its
            // length (however little it has left: dry, it falls on along its way), then down there braking (a hoverslam at 35% of what
            // it can; its engines lit a second ahead of it, one alone falling: three at their least would hold it up).
            set_.keep(c,s);const double vz=dot(s.velocity,up_),stand=shipLength-cg(s.propellant),h=std::max(over-stand,0.),g=norm(gravity(s.position)),most=rush(s);
            const Vec across=s.velocity+up_*-vz,to=flat+aim_*-1.;c.surfaces={-45*deg,-45*deg,-45*deg,-45*deg};
            if(escape_){if(set_.count()<set_.working(s,3))set_.use(c,s,set_.working(s,3)); // Every engine it has.
                const Vec dir=away(s,flat,to);push(c,s,escape(s,flat,dir,most));
                if(collides(s,flat,1.,over)<0&&gap(s,flat,dir,0)>10&&dot(across,dir)>=0){escape_=false;kept_=std::max(kept_,over);}} // The height it got to held.
            else{const bool clear=!overWorks(flat,shipLength);
                const double braking=.35*(set_.working(s,3)*raptorSea/s.mass-g),stop=vz<-1?(vz*vz-1)/(2*std::max(h,.5)):0.;
                const double up=clear?(stop>=braking?g+stop:std::max(g+1.5*(-30-vz),0.)):g+.5*(-vz)+.2*(kept_-over);
                const double d=norm(to);const Vec want=d>0&&!(clear&&h<120)?to*(-std::min(15.,std::sqrt(2*2.5*d))*(clear?std::clamp((h-120)/150,0.,1.):1)/d):Vec{};Vec side=(want+across*-1)*.5+push_*-1.;
                const double lift=std::max(up,.3*g0),limit=std::tan(lean(s,flat,side,20*deg))*std::max(lift,.4*set_.count()*raptorSea/s.mass*std::cos(20*deg));
                if(norm(side)>limit)side=side*(limit/norm(side));
                const Vec a=up_*lift+side;const int all=std::max(set_.working(s,3),1),n=clear&&(stop>.25*braking||vz<-12)?all:std::clamp(static_cast<int>(std::ceil(s.mass*norm(a)/(.85*raptorSea))),1,all);
                if(n>set_.count()||(n<set_.count()&&s.mass*norm(a)<.42*set_.count()*raptorSea))set_.use(c,s,n);
                const double floor=.4*set_.count()*raptorSea/s.mass;push(c,s,up_*(lift*lift+dot(side,side)<floor*floor?std::sqrt(floor*floor-dot(side,side)):lift)+side);} // The lean that gives the side asked on the least its engines give.
            if(h<.5){set_.use(c,s,0);phase_=Phase::down;}
            break;}
        case Phase::down:c.torqueOn=true;c.torque={};if(set_.count())set_.use(c,s,0);break; // Down: engines off.
        }
        return c;
    }
private:
    static constexpr double residual=8e3; // What its engines cannot draw (cryogens(), the ship's).
    // The thrust it gets away with (m/s²): 3 g, or all its working sea-level Raptors give when that is less.
    double rush(const State& s)const{return std::min(set_.working(s,3)*raptorSea/s.mass,3*g0);}
    void push(Command& c,const State& s,Vec a)const{ // The acceleration a by the lit engines, through the centre of mass, the pins rolled.
        c.throttle=std::clamp(s.mass*norm(a)/(std::max(set_.count(),1)*raptorSea),.4,1.);aim(c,s,unit(a),set_.centre());roll(c,s,&set_);
    }
    // What getting clear of the ground's works by its length with all it has takes (kg), and whether it has it (as the booster's).
    double escapeNeed(const State& s,Vec flat)const{
        const Vec dir=away(s,flat,flat+aim_*-1.);double d=0;while(d<600&&overWorks(flat+dir*d,shipLength))d+=5;
        const double g=norm(gravity(s.position)),h=std::max(dot(s.position+site_*-1.,up_)-(shipLength-cg(s.propellant)),0.),vz=dot(s.velocity,up_); // By its Δv, as the booster's.
        return 1.2*s.mass*(g*(d/15+5)+std::max(-vz,0.)+g*h/30+std::min(30.,std::sqrt(2*g*h)))/raptorExhaust;
    }
    bool escapes(const State& s,Vec flat)const{return s.propellant-residual>escapeNeed(s,flat);}
    // Gives the catch up for good: the arms stay open, the aim out to sea east of the tower; low, away at once, with all it has
    // when a column threatens it.
    void giveUp(const State& s,const char* why){
        if(!catch_)return;catch_=false;why_=why;close_=false;hurry_=false;const Vec rel=s.position+site_*-1;const double over=dot(rel,up_),east=dot(rel,cross(north_,up_));
        // Already well clear of the works: down where it is; else out to sea east of the tower, or from west of it inland, north of the tanks.
        aim_=!overWorks(rel+up_*-over,3*shipLength)?rel+up_*-over:east>-22?cross(north_,up_)*1500.:cross(north_,up_)*-400.+north_*300.;
        phase_=Phase::divert;kept_=over;const Vec flat=rel+up_*-over,across=s.velocity+up_*-dot(s.velocity,up_);
        if(collides(s,flat,.7*stopping(s,flat,std::max(set_.count(),1),rush(s)),over)>=0||closing(s,flat)>safe(s,flat,.5*std::tan(20*deg)*norm(gravity(s.position)))){escape_=true;threat_=why;}(void)across; // Or closing on it faster than a divert's lean would stop it short: never across it (over it) on its way out.
    }
    struct Arrival {double along=0,cross=0,seconds=0;bool down=false;}; // down: it reaches the ground's height (else along, cross: where it gave up).
    // The flaps' drag at their openings now (times |sin α|; their entry trim before any), on the reference area.
    double flapDrag(const State& s)const{
        double d=0;for(std::size_t i=0;i<4;++i)d+=flapCn*flaps[i].area/(pi*radius*radius)*std::sin(45*deg+(s.surfaces.size()==4?s.surfaces[i]:i<2?-30*deg:13*deg));return d;}
    static Vec inertial(const State& s){return s.velocity+cross(Vec{0,0,7.292115e-5},s.position);}
    // The torque that over a half-second pulse brings the body's rates to the local horizontal frame's (nose along the
    // inertial velocity's horizontal, belly down: turning once an orbit) plus a turn toward it at 0.005/s (faster than the
    // gravity gradient's pull off it, √3 n).
    Vec level(const State& s)const{
        const Vec vi=inertial(s),down=unit(s.position)*-1.,x=unit(vi+down*-dot(vi,down)),y=cross(down,x);
        const Vec e=(cross(s.toBody.r0,x)+cross(s.toBody.r1,y)+cross(s.toBody.r2,down))*.5,turn=cross(s.position,vi)*(1/dot(s.position,s.position));
        const Vec dw=mul(s.toBody,turn+e*.005)+s.rate*-1.;const double ix=rollInertia(s.propellant),iy=pitchInertia(s.propellant);
        return {ix*dw.x/.5,iy*dw.y/.5,iy*dw.z/.5};
    }
    // The angle of attack by Mach: 65° hypersonic, 75° at Mach 1, 80° below.
    static double alpha(double speed,double height){const double m=speed/sound(height);return (m>5?65:m>1?65+(5-m)*2.5:80)*deg;}
    // The heading error to the tower (rad, right positive) and the distance to it over the ground (m).
    double heading(const State& s)const{
        const Vec up=unit(s.position),vh=unit(s.velocity+up*-dot(s.velocity,up)),d=site_+s.position*-1,dh=unit(d+up*-dot(d,up));
        return std::atan2(-dot(cross(vh,dh),up),dot(vh,dh));
    }
    double range(const State& s)const{return 6371e3*std::acos(std::clamp(dot(unit(s.position),unit(site_)),-1.,1.));}
    double flipHeight(const State& s)const{
        const double v=norm(s.velocity),a=std::max(set_.working(s,3)*.8*raptorSea/s.mass-g0,2.);return catchHeight(s)+hoverAbove+v*v/(2*a)+4*v+550; // On the engines that work; 500 m more for the burn to take out what the flip leaves across (~15 m/s, tens of metres) within its lean.
    }
    // Body axes (rows, in ECEF) for the angle of attack a along the velocity v at r, banked b (right positive).
    static Mat axes(Vec r,Vec v,double a,double b){
        const Vec u=unit(v),up=unit(r),l0=unit(up+u*-dot(up,u)),l=l0*std::cos(b)+cross(u,l0)*std::sin(b);
        const Vec x=u*std::cos(a)+l*std::sin(a),z=u*std::sin(a)+l*-std::cos(a);return{x,cross(z,x),z};
    }
    // The attitude for (a, b) by PD on the rotation error, in the flow's axes: pitch and the axis across the flow in the x-z plane
    // (what the flaps' drag can turn about) by the flaps at 1 rad/s; about the flow itself (the bank) by the RCS at 0.2 rad/s,
    // critically damped, within what its thrusters give. Out of the air: everything by the RCS at 0.2 rad/s, the flaps at trim.
    void attitude(Command& c,const State& s,double a,double b)const{attitude(c,s,axes(s.position,s.velocity,a,b));}
    void attitude(Command& c,const State& s,const Mat& d)const{ // To the body axes d (rows, in ECEF).
        const Mat& m=s.toBody;
        const Vec e=mul(m,(cross(m.r0,d.r0)+cross(m.r1,d.r1)+cross(m.r2,d.r2))*.5),w=s.rate;const double ix=rollInertia(s.propellant),iy=pitchInertia(s.propellant);
        c.torqueOn=true;if(s.q<10){c.surfaces={-30*deg,-30*deg,13*deg,13*deg};c.torque={ix*(.04*e.x-.4*w.x),iy*(.04*e.y-.4*w.y),iy*(.04*e.z-.4*w.z)};return;}
        const Vec u=unit(mul(m,s.velocity)),across=unit(Vec{u.z,0,-u.x});auto inertia=[&](Vec n){return ix*n.x*n.x+iy*(n.y*n.y+n.z*n.z);};
        actuate(c,s,Vec{0,iy*(e.y-1.4*w.y),0}+across*(inertia(across)*(dot(e,across)-1.4*dot(w,across)))+u*(inertia(u)*(.04*dot(e,u)-.4*dot(w,u))));
    }
    // A moment (body) from the flaps: pitch past the body's own moment by opening the forward pair against the aft one, the axis
    // across the flow by opening one side against the other; the RCS for the rest.
    void actuate(Command& c,const State& s,Vec want)const{
        const Vec vb=mul(s.toBody,s.velocity);const double al=std::atan2(vb.z,vb.x),sa=std::sin(al),ca=std::cos(al),area=pi*radius*radius,g=cg(s.propellant);
        if(sa<.2){c.surfaces={-30*deg,-30*deg,13*deg,13*deg};c.torque=want;return;}
        const double side=1.2*shipLength*2*radius/area,body=(g-shipLength/2)*(liftOf(shipLength,al)*ca+dragOf(shipLength,al,norm(s.velocity)/sound(norm(s.position)-norm(site_)))*sa)*s.q*area;
        double k[4],p[4];for(int i=0;i<4;++i){k[i]=s.q*flapCn*flaps[i].area*sa;p[i]=(g-flaps[i].x)*sa*k[i];}(void)side;
        const Vec axis{sa,0,-ca}; // A left flap's moment, per newton and metre off the axis.
        const double u=(want.y-body-p[0]-1.2*p[2])/(p[0]-p[2]),fwd=std::clamp(1+u,0.,2.),aft=std::clamp(1.2-u,0.,2.),dif=dot(want,axis)/(radius*(k[0]+k[2]));
        const double f[4]{std::clamp((fwd+dif)/2,0.,1.),std::clamp((fwd-dif)/2,0.,1.),std::clamp((aft+dif)/2,0.,1.),std::clamp((aft-dif)/2,0.,1.)};
        double pitch=0;c.surfaces.resize(4);for(int i=0;i<4;++i){pitch+=p[i]*f[i];c.surfaces[static_cast<std::size_t>(i)]=std::asin(f[i])-45*deg;}
        const Vec turned=axis*(radius*(k[0]*(f[0]-f[1])+k[2]*(f[2]-f[3])));c.torque=Vec{want.x-turned.x,want.y-body-pitch,want.z-turned.z};
    }
    // An entry from (r, v) at mass m banked b throughout to 1 km over the tower's ground (gravity with J2, the Earth's turning,
    // the standard atmosphere, the crossflow lift and drag at the scheduled angle of attack; only the lift's vertical part, cos b,
    // as bank reversals cancel the rest; all of it when `held`, a bank kept to the end): where it ends past the tower along its
    // final track and right of it (m), and how long it takes (s).
    Arrival arrive(Vec r,Vec v,double m,double b,bool held=false)const{ // held: also the belly flop flown as fall() begins it (square to the flow below 300 m/s: no glide).
        const Vec w{0,0,7.292115e-5};const double area=pi*radius*radius,re=6378137,j2=1.08263e-3,base=norm(site_);
        auto acc=[&](Vec p,Vec u){
            const double rn=norm(p),zz=p.z*p.z/(rn*rn),f=1.5*j2*(re/rn)*(re/rn);
            Vec a=Vec{p.x*(1+f*(1-5*zz)),p.y*(1+f*(1-5*zz)),p.z*(1+f*(3-5*zz))}*(-earth.mu/(rn*rn*rn))+cross(w,u)*-2.+cross(w,cross(w,p))*-1.;
            const double h=rn-base,rho=density(h),speed=norm(u);
            if(rho>0&&speed>0){const Vec uh=u*(1/speed),up=p*(1/rn),l0=unit(up+uh*-dot(up,uh)),l=l0*std::cos(b)+(held?cross(uh,l0)*std::sin(b):Vec{});
                const double al=held&&speed<=300?90*deg:alpha(speed,h),k=.5*rho*speed*speed*area/m;
                a=a+uh*(-k*drag_*(dragOf(shipLength,al,speed/sound(h))+flaps_*std::sin(al)))+l*(k*lift_*liftOf(shipLength,al));}
            return a;};
        double t=0;bool down=false;
        for(int n=0;n<20000;++n){const double h=norm(r)-base;if(h<1e3&&dot(v,r)<0){down=true;break;}const double dt=h>1.2e5?2.:h>3e4?.5:.25;
            const Vec a1=acc(r,v),v1=v+a1*(dt/2),r1=r+v*(dt/2),a2=acc(r1,v1),v2=v+a2*(dt/2),r2=r+v1*(dt/2),a3=acc(r2,v2),v3=v+a3*dt,r3=r+v2*dt,a4=acc(r3,v3);
            r=r+(v+v1*2+v2*2+v3)*(dt/6);v=v+(a1+a2*2+a3*2+a4)*(dt/6);t+=dt;}
        const Vec n=unit(cross(r,v)),rs=unit(site_),ru=unit(r),p=unit(rs+n*-dot(rs,n));
        return{-6371e3*std::atan2(dot(cross(ru,p),n),dot(ru,p)),6371e3*std::asin(std::clamp(dot(rs,n),-1.,1.)),t,down};
    }
    // A deorbit burn now (impulsive, retrograde): its landing at a 50° bank toward the tower, and how far beside it unbanked.
    Arrival plan(const State& s){
        flaps_=flapDrag(State{}); // Their entry trim.
        const Vec v=s.velocity+unit(inertial(s))*-deorbit;const Arrival flat=arrive(s.position,v,s.mass,0);
        if(!flat.down||std::abs(flat.cross)>2*crossLimit||flat.along<-3e6||flat.along>0)return flat;
        Arrival a=arrive(s.position,v,s.mass,(flat.cross>0?-50:50)*deg);a.cross=flat.cross;return a;
    }
    // The drag and lift measured (IMU) over those predicted, filtered: the predictions' corrections.
    void calibrate(const State& s){
        flaps_=flapDrag(s);const double speed=norm(s.velocity),height=norm(s.position)-norm(site_);if(s.q<500||speed<1)return;
        const Vec u=s.velocity*(1/speed),f=tmul(s.toBody,s.specific),vb=mul(s.toBody,s.velocity);const double al=std::atan2(vb.z,vb.x),k=s.q*pi*radius*radius/s.mass;
        const double drag=-dot(f,u),lift=norm(f+u*drag),d=k*(dragOf(shipLength,al,speed/sound(height))+flaps_*std::abs(std::sin(al))),l=k*std::abs(liftOf(shipLength,al));
        if(d>0)drag_+=.2*(std::clamp(drag/d,.5,2.)-drag_);if(l>0)lift_+=.2*(std::clamp(lift/l,.5,2.)-lift_);
    }
    // Its speed across toward the tower's axis (m/s), and how near that may be come at speed (m): the arms' tips (13 m), the
    // hull, the clearance and 6 m more.
    double closing(const State& s,Vec flat)const{return norm(flat)>0?std::max(-dot(s.velocity,unit(flat)),0.):0.;}
    static constexpr double keepOut=25;
    // The fastest it may close on the tower braking at a m/s² (its lean against its way once turned there: ~2 s on, the body
    // still leaning as it was meanwhile) and stop short of that.
    double safe(const State& s,Vec flat,double a)const{return std::sqrt(2*a*std::max(norm(flat)-keepOut-2*closing(s,flat),0.));}
    // Where the landing burn ends: in front of the arms' V on their line, 40 m out (as the booster's: tens of metres off it
    // across leave the hull clear of the arms' tips; from there it slides in).
    Vec front()const{return cross(north_,up_)*40.;}
    // Across onto that point by the end of the landing burn, at rest there (from behind the tower: by the way round it):
    // E-guidance (6d/T² - 4v/T), T the burn's time left at its braking so far (twice the height to go over the speed down), not
    // less than keeps the loop within a third of the attitude's bandwidth (√6·3/bandwidth: no last lunge for the hover to inherit).
    Vec across(const State& s,Vec flat,bool end=false,bool direct=false)const{ // end: what the plan asks at its end (6d/T² + 2v/T the other way), its most
        // there or now; direct: straight to that point (what it really has to cover), not by the way round the tower.
        const double above=std::max(dot(s.position+site_*-1.,up_)-catchHeight(s)-hoverAbove,0.),vz=dot(s.velocity,up_),T=std::max(2*above/std::max(-vz,1.),std::sqrt(6.)*3/bandwidth(s));
        const Vec to=direct||dot(flat,cross(north_,up_))>-8?flat+front()*-1.:approach(flat),v=s.velocity+up_*-dot(s.velocity,up_);return end?to*(6/(T*T))+v*(2/T):to*(-6/(T*T))+v*(-4/T);
    }
    // The belly flop (subsonic), flown by prediction (as the entry): where it would end its flip (from the point the landing burn
    // is to hold it at, in front of the arms' V, and its speed across the ground then) for a way of the nose (turned there about
    // the vertical at 3°/s) and a tilt held to the flip once the nose is within 30° of that way (reached at 5°/s; none while it
    // turns: turned first, then gliding): its fall flown out with the tables' crossflow lift and
    // drag (the measured factors and the flaps' drag, as the entry's prediction), the lift in the plane of the nose and the flow
    // (a body of revolution's only: ahead of 90° of attack toward the nose, past it toward the tail), the Earth's turning; then
    // the flip as measured on the bench: its own push along the nose, J = 1.7 n F/m (~41 m/s on 3 at 181 t), over ~6.7 s, 0.52
    // of it carried through.
    struct Flipped {Vec p,v;};
    Flipped flipped(const State& s,Vec nose,double tilt,Vec way,double want)const{
        const Vec w{0,0,7.292115e-5};const double area=pi*radius*radius,base=norm(site_),dt=.5;Vec r=s.position,v=s.velocity,nz=nose;double th=tilt;State x=s;
        for(int i=0;i<500;++i){
            const Vec up=unit(r);const double h=norm(r)-base,speed=norm(v);x.position=r;x.velocity=v;if(speed<150&&h<flipHeight(x))break;
            {const double turn=std::atan2(dot(cross(nz,way),up),dot(nz,way)),q=std::clamp(turn,-3*deg*dt,3*deg*dt);nz=unit(nz*std::cos(q)+cross(up,nz)*std::sin(q));nz=unit(nz+up*-dot(nz,up));}
            th+=std::clamp((dot(nz,way)>std::cos(30*deg)?want:0.)-th,-5*deg*dt,5*deg*dt); // Turned first, then tilted.
            const Vec u=v*(1/std::max(speed,1e-3)),side=unit(nz+u*-dot(nz,u));const double al=90*deg-th,k=.5*density(h)*speed*speed*area/s.mass;
            const Vec a=gravity(r)+cross(w,v)*-2.+cross(w,cross(w,r))*-1.+u*(-k*drag_*(dragOf(shipLength,al,speed/sound(h))+flaps_*std::sin(al)))+side*(k*lift_*liftOf(shipLength,al));
            v=v+a*dt;r=r+v*dt;}
        const Vec up=unit(r),vh=v+up*-dot(v,up),rel=r+site_*-1.+front()*-1.,ph=rel+up*-dot(rel,up),nh=unit(nz+up*-dot(nz,up));
        const double J=1.7*set_.working(s,3)*raptorSea/s.mass,tf=6.7;return{ph+vh*tf+nh*(.52*J*tf),vh+nh*J};
    }
    // How far from that point the flip ends (in 50 m) and how fast across (in 10 m/s), how fast toward the tower (in 5 m/s),
    // and how far short of 30 m out from its axis on the V's side (in 10 m: over or behind the tower the burn would cross it):
    // what to keep least (the burn then has little to do, nothing toward or over the tower).
    double flipCost(Vec p,Vec v)const{
        const Vec axis=p+front();const double closing=norm(axis)>0?std::max(-dot(v,unit(axis)),0.):0.,behind=std::max(30-dot(axis,cross(north_,up_)),0.);
        return dot(p,p)/2500+dot(v,v)/100+closing*closing/25+behind*behind/100;
    }
    // Each second: the nose's way (round about the vertical every 30°, then 10° about the best) and the tilt (every 5°, within
    // 30°) that end the flip best; between, those held.
    void fall(Command& c,const State& s,double t,double height){
        const Vec up=unit(s.position),u=unit(s.velocity),vh=s.velocity+up*-dot(s.velocity,up);const double dt=std::clamp(t-fell_,0.,.5);
        if(!falling_){falling_=true;fell_=t;check_=t;tilt_=std::asin(std::clamp(dot(s.toBody.r0,u),-1.,1.));const Vec x=s.toBody.r0+up*-dot(s.toBody.r0,up);nose_=norm(x)>.1?unit(x):unit(vh);way_=nose_;want_=tilt_;}
        fell_=t;calibrate(s);
        if(t>=check_){check_=t+1;double best=1e300;Vec bw=way_;double bt=want_;
            auto rot=[&](Vec a,double g){return unit(a*std::cos(g)+cross(up,a)*std::sin(g));};
            for(int k=0;k<12;++k)for(int m=-6;m<=6;++m){const Vec w=rot(nose_,k*30*deg);const auto f=flipped(s,nose_,tilt_,w,m*5*deg);if(const double e=flipCost(f.p,f.v);e<best){best=e;bw=w;bt=m*5*deg;}}
            for(int k=-2;k<=2;++k)for(int m=-2;m<=2;++m){const Vec w=rot(bw,k*10*deg);const double th=std::clamp(bt+m*2.5*deg,-30*deg,30*deg);const auto f=flipped(s,nose_,tilt_,w,th);if(const double e=flipCost(f.p,f.v);e<best){best=e;bw=w;bt=th;}}
            way_=bw;want_=bt;const auto f=flipped(s,nose_,tilt_,way_,want_);const Vec ahead=norm(vh)>1?unit(vh):nose_;along_=dot(f.p,ahead);cross_=dot(f.p,cross(ahead,up));
            if(std::getenv("ALBERT_FALL")){const Vec east=cross(north_,up_),rel=s.position+site_*-1.+front()*-1.;std::printf("FALL %.0f at %.0f %.0f v %.1f %.1f nose %.0f way %.0f tilt %.1f want %.1f -> p %.0f %.0f v %.1f %.1f cost %.2f\n",t,dot(rel,east),dot(rel,north_),dot(vh,east),dot(vh,north_),
                std::atan2(dot(nose_,east),dot(nose_,north_))/deg,std::atan2(dot(way_,east),dot(way_,north_))/deg,tilt_/deg,want_/deg,dot(f.p,east),dot(f.p,north_),dot(f.v,east),dot(f.v,north_),best);}}
        {const double turn=std::atan2(dot(cross(nose_,way_),up),dot(nose_,way_)),r=std::clamp(turn,-3*deg*dt,3*deg*dt);nose_=unit(nose_*std::cos(r)+cross(up,nose_)*std::sin(r));} // About the vertical.
        tilt_+=std::clamp((dot(nose_,way_)>std::cos(30*deg)?want_:0.)-tilt_,-5*deg*dt,5*deg*dt); // Turned first (within 30°), then tilted.
        const Vec across=unit(nose_+u*-dot(nose_,u)); // The nose's way, square to the flow.
        const Vec x=unit(u*std::sin(tilt_)+across*std::cos(tilt_)),z=unit(u*std::cos(tilt_)+across*-std::sin(tilt_));attitude(c,s,Mat{x,cross(z,x),z});
        held_=tilt_; // Shown: the glide's tilt (ahead positive).
    }
    void guide(const State& s){
        calibrate(s);
        // The bank's side: away from where the landing falls beside the tower, kept until that passes a corridor narrowing with
        // the range (to 300 m); within 40 km, the bank (every 10°, then 2°, kept to the end, all its lift) that brings where it comes down nearest the tower
        // both ways (the belly flop's body lift turned across: the real ship's way onto the tower, not a walk across on its
        // engines after the flip); short of it even unbanked, gently toward it.
        const double distance=range(s),corridor=300+.03*distance;Arrival a=arrive(s.position,s.velocity,s.mass,0);
        if(distance<40e3){auto miss=[&](double b){const Arrival q=arrive(s.position,s.velocity,s.mass,b,true);return q.along*q.along+q.cross*q.cross;};
            double best=1e300,b=0;for(int i=-12;i<=12;++i)if(const double m=miss(i*10*deg);m<best){best=m;b=i*10*deg;}
            for(int i=-4;i<=4;++i)if(const double m=miss(b+i*2*deg);m<best){best=m;b+=i*2*deg;i=-5;} // Refined about the best.
            bank_=b;a=arrive(s.position,s.velocity,s.mass,b,true);along_=a.along;cross_=a.cross;return;}
        if(sign_*a.cross>corridor)sign_=-sign_;
        if(a.along<=0)bank_=std::clamp(2*heading(s),-30*deg,30*deg); // Short even with all the lift up: turned toward the tower (the shortest way left).
        else{double lo=0,hi=120*deg;a=arrive(s.position,s.velocity,s.mass,sign_*hi);
            if(a.along>=0)bank_=sign_*hi;
            else{for(int i=0;i<8;++i){const double mid=(lo+hi)/2;a=arrive(s.position,s.velocity,s.mass,sign_*mid);(a.along>0?lo:hi)=mid;}bank_=sign_*(lo+hi)/2;}}
        along_=a.along;cross_=a.cross;
    }
    Phase phase_=Phase::orbit;double held_=0,drag_=1,lift_=1,flaps_=1.8,next_=.02,check_=0,pulse_=0,along_=0,cross_=0,bank_=0,v0_=0,caught_=0,seen_=0;int sign_=1;bool off_=false;
    bool catch_=true,hurry_=false,escape_=false,vented_=false,falling_=false;Vec nose_,way_;double fell_=0,tilt_=0,want_=0;
public:
    bool ventOn=false; // Venting the main tanks down to the headers (off: its entry's range prediction is not yet calibrated for so light a ship).
private:const char* why_=nullptr;const char* threat_=nullptr;Vec aim_;double kept_=0; // kept_: the height a divert holds.
    // The landing's engines: the sea-level Raptors; the deorbit burn's: those, then the vacuum ones (in vacuum either will do).
    Engines set_{{1,2,3},shipNozzle,3},burn_{{1,2,3,4,5,6},shipNozzle};
};
}
