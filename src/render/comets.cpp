#include "comets.hpp"
#include "animation.hpp"
#include "core/local_system.hpp"
#include "threepp/textures/DataTexture.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace threepp;

namespace {
constexpr double AU=149597870700.;
constexpr std::size_t BETAS=20,AGES=40;

float render(double metres){return static_cast<float>(metres/albert::core::earthRadiusMeters);}
Vector3 graphical(albert::core::Vector v){return {render(v.x),render(v.z),render(-v.y)};}
double length(albert::core::Vector v){return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);}

// Halley-Marcus phase function as log values, 0-180° in 1° texels.
std::shared_ptr<Texture> phaseTexture(){
    std::vector<float> texels;
    for(int degree=0;degree<=180;++degree)texels.insert(texels.end(),{static_cast<float>(std::log(albert::core::dustPhase(degree))),0.f,0.f,1.f});
    auto t=DataTexture::create(ImageData(std::move(texels)),181,1);
    t->type=Type::Float;t->magFilter=Filter::Linear;t->minFilter=Filter::Linear;t->generateMipmaps=false;t->colorSpace=ColorSpace::NoColorSpace;t->needsUpdate();return t;
}

// Shared shading: Halley-Marcus phase, reddish dust (~10% per 1000 A) and the display.
// Physical radiance by default; the stretch eases into an asinh scale, as in
// astrophotography, mapping the reference reflectance to I/F 0.15 and fading to black
// below about 5e-5 of it, where the tail ends: the coma and the far fainter tail show
// together.
constexpr const char* phaseFunction=R"(
    uniform sampler2D phase;uniform float intensity,stretch,reference;
    const vec3 DUST=vec3(1.06,1.0,0.94);
    const float PI=3.141592653589793;
    float dustPhase(float cosine){float degrees=degrees(acos(clamp(cosine,-1.0,1.0)));return exp(texture2D(phase,vec2((degrees+0.5)/181.0,0.5)).r);}
    float arcsinh(float x){return log(x+sqrt(x*x+1.0));}
    vec3 shade(float reflectance){
        float soft=reference*5e-5,enhanced=0.15*arcsinh(reflectance/soft)/arcsinh(reference/soft);
        float value=exp(mix(log(max(reflectance,1e-30)),log(max(enhanced,1e-30)),stretch));
        return DUST*value*intensity/PI;
    }
)";

// Steady coma: the albedo-weighted column of a 1/r^2 outflow is Afρ/(8b) in I/F, here
// averaged over the pixel footprint and limited to the part of the ray in front of
// the camera. Grains turn back at the radiation-pressure apex X sunward, inside a
// paraboloid with the nucleus at its focus (fountain model), which opens antisunward;
// grain sizes and speeds spread X over orders of magnitude, so the boundary is soft.
// The column fades over 10 X as older grains leave for the Finson-Probstein tail sheet,
// reaching black well inside the 80 X shell even on the stretched scale.
std::shared_ptr<ShaderMaterial> comaMaterial(Texture* phase){
    auto m=ShaderMaterial::create();m->transparent=true;m->depthWrite=false;m->side=Side::Front;m->blending=Blending::Additive;
    m->uniforms={{"phase",Uniform(phase)},{"centre",Uniform(Vector3())},{"solar",Uniform(Vector3(1,0,0))},{"afrho",Uniform(0.f)},{"apex",Uniform(1.f)},
        {"radius",Uniform(1.f)},{"nucleus",Uniform(0.f)},{"intensity",Uniform(2.4f)},{"stretch",Uniform(0.f)},{"reference",Uniform(1e-5f)}};
    m->vertexShader=R"(varying vec3 vPosition;void main(){vec4 view=modelViewMatrix*vec4(position,1.0);vPosition=view.xyz;gl_Position=projectionMatrix*view;})";
    m->fragmentShader=std::string(R"(
        uniform vec3 centre,solar;uniform float afrho,apex,radius,nucleus;varying vec3 vPosition;)")+phaseFunction+R"(
        void main(){
            vec3 d=normalize(vPosition);float along=dot(centre,d);vec3 q=along*d-centre;float b=length(q);
            float footprint=fwidth(b),reach=sqrt(b*b+0.25*footprint*footprint);
            float ahead=0.5+atan(along,reach)/PI; // Share of the 1/r^2 column beyond the camera.
            float column=afrho/(8.0*reach)*ahead*exp(-b/(10.0*apex));
            float parabolic=(b+dot(q,solar))/(2.0*apex),envelope=1.0/(1.0+parabolic*parabolic);
            float edge=1.0-smoothstep(0.8*radius,radius,b);
            float hidden=b<nucleus?0.5:1.0; // The far half lies behind the nucleus.
            float reflectance=column*envelope*edge*hidden*dustPhase(dot(solar,-d));
            gl_FragColor=vec4(shade(reflectance),1.0);
            #include <tonemapping_fragment>
            #include <encodings_fragment>
        })";
    return m;
}

// Dust sheet: albedo cross-section per area sigma gives I/F = sigma*phase/4 through a
// thin layer, divided by the cosine of the view to the sheet, limited by its thickness.
std::shared_ptr<ShaderMaterial> tailMaterial(Texture* phase){
    auto m=ShaderMaterial::create();m->transparent=true;m->depthWrite=false;m->side=Side::Double;m->blending=Blending::Additive;
    m->uniforms={{"phase",Uniform(phase)},{"solar",Uniform(Vector3(1,0,0))},{"intensity",Uniform(2.4f)},{"stretch",Uniform(0.f)},{"reference",Uniform(1e-5f)}};
    m->vertexShader=R"(
        attribute float density,thickness;varying float vDensity,vThickness;varying vec3 vWorld,vNormal;
        void main(){vDensity=density;vThickness=thickness;vNormal=normalize(mat3(modelMatrix)*normal);
            vec4 world=modelMatrix*vec4(position,1.0);vWorld=world.xyz;gl_Position=projectionMatrix*viewMatrix*world;})";
    m->fragmentShader=std::string(R"(
        uniform vec3 solar;varying float vDensity,vThickness;varying vec3 vWorld,vNormal;)")+phaseFunction+R"(
        void main(){
            vec3 view=normalize(cameraPosition-vWorld);
            float slant=max(abs(dot(normalize(vNormal),view)),vThickness);
            float reflectance=vDensity*0.25*dustPhase(dot(solar,view))/slant;
            gl_FragColor=vec4(shade(reflectance),1.0);
            #include <tonemapping_fragment>
            #include <encodings_fragment>
        })";
    return m;
}

// Radiation-pressure apex of beta = 0.1 grains: X = v^2/(2 beta g).
double apexMetres(const albert::core::CometActivity& c,double r){
    const double speed=c.grainSpeed/std::sqrt(r),gravity=albert::core::solarGM/(r*AU*r*AU);
    return speed*speed/(2*.1*gravity);
}
}

struct albert::CometVisuals::Comet {
    const core::CometActivity* activity;const core::LocalOrbit* orbit;
    std::shared_ptr<Mesh> coma,tail;std::shared_ptr<ShaderMaterial> comaShader,tailShader;
    double afrho=0,distance=0,computedJd=std::numeric_limits<double>::quiet_NaN();
    std::chrono::steady_clock::time_point computed{};
    float reference=1,stretch=0,stretchTarget=0;
    ~Comet(){coma->geometry()->dispose();tail->geometry()->dispose();comaShader->dispose();tailShader->dispose();}
};

albert::CometVisuals::CometVisuals(Scene& scene):scene_(scene),phase_(phaseTexture()){
    for(const auto& activity:core::cometActivities()){
        const auto* orbit=core::localOrbit(activity.id);if(!orbit||orbit->parent!="sun")continue;
        auto c=std::make_unique<Comet>();c->activity=&activity;c->orbit=orbit;
        c->comaShader=comaMaterial(phase_.get());c->coma=Mesh::create(SphereGeometry::create(1.f,64,32),c->comaShader);
        c->tailShader=tailMaterial(phase_.get());
        auto geometry=BufferGeometry::create();std::vector<unsigned> index;
        for(unsigned i=0;i+1<BETAS;++i)for(unsigned j=0;j+1<AGES;++j){const unsigned a=static_cast<unsigned>(i*AGES+j),b=a+1,cc=a+static_cast<unsigned>(AGES),d=cc+1;index.insert(index.end(),{a,cc,b,b,cc,d});}
        geometry->setIndex(std::move(index));
        geometry->setAttribute("position",FloatBufferAttribute::create(std::vector<float>(BETAS*AGES*3),3));
        geometry->setAttribute("normal",FloatBufferAttribute::create(std::vector<float>(BETAS*AGES*3),3));
        geometry->setAttribute("density",FloatBufferAttribute::create(std::vector<float>(BETAS*AGES),1));
        geometry->setAttribute("thickness",FloatBufferAttribute::create(std::vector<float>(BETAS*AGES,1.f),1));
        c->tail=Mesh::create(geometry,c->tailShader);
        for(auto* mesh:{c->coma.get(),c->tail.get()}){mesh->frustumCulled=false;mesh->renderOrder=3;mesh->visible=false;}
        scene.add(c->coma);scene.add(c->tail);
        // Stretch reference: the coma at a fifth of the apex, at the reference activity.
        const double apex=apexMetres(activity,activity.referenceAu);
        c->reference=static_cast<float>(activity.afrhoMetres/(8*.2*apex));
        comets_[std::string(activity.id)]=std::move(c);
    }
}
albert::CometVisuals::~CometVisuals(){for(auto& [id,c]:comets_){(void)id;scene_.remove(*c->coma);scene_.remove(*c->tail);}phase_->dispose();}
double albert::CometVisuals::afrho(const std::string& id) const{return comets_.contains(id)?comets_.at(id)->afrho:0.;}
double albert::CometVisuals::distanceAu(const std::string& id) const{return comets_.contains(id)?comets_.at(id)->distance:0.;}
bool albert::CometVisuals::enhanced(const std::string& id) const{return comets_.contains(id)&&comets_.at(id)->stretchTarget>0;}
void albert::CometVisuals::enhance(const std::string& id,bool enhanced){if(comets_.contains(id))comets_.at(id)->stretchTarget=enhanced?1.f:0.f;}

void albert::CometVisuals::update(const PerspectiveCamera& camera,const Vector3& sun,const std::map<std::string,Vector3>& nuclei,
                                  const std::map<std::string,core::StateVector>& heliocentric,double jd,float seconds){
    for(auto& [id,entry]:comets_){
        auto& c=*entry;const auto nucleus=nuclei.find(id);const auto known=heliocentric.find(id);
        if(known==heliocentric.end()){c.coma->visible=c.tail->visible=false;continue;}
        const auto& now=known->second;const double r=length(now.position)/AU;
        const bool inbound=now.position.x*now.velocity.x+now.position.y*now.velocity.y+now.position.z*now.velocity.z<0;
        c.afrho=core::afrho(*c.activity,r,inbound);c.distance=r;
        c.stretch=animateValue(c.stretch,c.stretchTarget,seconds); // Blended in log radiance by the shaders.
        const bool shown=nucleus!=nuclei.end()&&c.afrho>0;c.coma->visible=c.tail->visible=shown;
        if(!shown){c.computedJd=std::numeric_limits<double>::quiet_NaN();continue;}
        const double apex=apexMetres(*c.activity,r),intensity=2.4/(r*r);
        Vector3 solar=sun;solar.sub(nucleus->second).normalize();
        // Coma shell: 80 apexes, where the faded column is far below the stretch floor.
        const float radius=render(80*apex);c.coma->position.copy(nucleus->second);c.coma->scale.setScalar(radius);
        Vector3 centre=nucleus->second;centre.applyMatrix4(camera.matrixWorldInverse);Vector3 viewSolar=solar;viewSolar.transformDirection(camera.matrixWorldInverse);
        auto& u=c.comaShader->uniforms;
        u.at("centre").setValue(centre);u.at("solar").setValue(viewSolar);u.at("afrho").setValue(render(c.afrho));u.at("apex").setValue(render(apex));
        u.at("radius").setValue(radius);u.at("nucleus").setValue(render(c.orbit->radius));
        // One surface per ray: the near side from outside, the far side from inside.
        c.comaShader->side=centre.length()<radius?Side::Back:Side::Front;
        c.tailShader->uniforms.at("solar").setValue(solar);
        for(auto* m:{c.comaShader.get(),c.tailShader.get()}){m->uniforms.at("intensity").setValue(static_cast<float>(intensity));m->uniforms.at("stretch").setValue(c.stretch);m->uniforms.at("reference").setValue(c.reference);}
        c.tail->position.copy(nucleus->second);
        // Recompute the grains after a simulated minute, at most ten times a second.
        const auto clock=std::chrono::steady_clock::now();
        if(!(std::abs(jd-c.computedJd)<1./1440.)&&clock-c.computed>std::chrono::milliseconds(100)){
            c.computedJd=jd;c.computed=clock;
            const double speed=c.activity->grainSpeed/std::sqrt(r),young=std::max(600.,apex/speed);
            std::vector<double> betas(BETAS),ages(AGES);
            for(std::size_t i=0;i<BETAS;++i)betas[i]=.003*std::pow(.9/.003,static_cast<double>(i)/(BETAS-1));
            for(std::size_t j=0;j<AGES;++j)ages[j]=young*std::pow(120*86400./young,static_cast<double>(j)/(AGES-1));
            // Osculating orbit through the current state: perturbations over 120 days are small.
            const auto grid=core::dustGrid(core::osculating(now,jd),*c.activity,jd,betas,ages);
            auto geometry=c.tail->geometry();
            auto* positions=geometry->getAttribute<float>("position");auto* normals=geometry->getAttribute<float>("normal");
            auto* density=geometry->getAttribute<float>("density");auto* thickness=geometry->getAttribute<float>("thickness");
            auto node=[&](std::size_t i,std::size_t j){return grid.offsets[i*AGES+j];};
            // Albedo cross-section per area of each cell; grains spread at least over their
            // ejection distance v*age. Nodes average their cells.
            std::vector<double> sum(BETAS*AGES,0.),count(BETAS*AGES,0.),spread(BETAS*AGES,1.);
            for(std::size_t i=0;i+1<BETAS;++i)for(std::size_t j=0;j+1<AGES;++j){
                auto cross=[](core::Vector a,core::Vector b){return core::Vector{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
                const auto p00=node(i,j),p10=node(i+1,j),p01=node(i,j+1),p11=node(i+1,j+1);
                const double area=.5*(length(cross(p10-p00,p01-p00))+length(cross(p11-p10,p11-p01)));
                const double age=.5*(ages[j]+ages[j+1]),beta=std::sqrt(betas[i]*betas[i+1]);
                const double ejection=c.activity->grainSpeed*std::sqrt(beta/.1/r)*age;
                const double rate=.25*(grid.rate[i*AGES+j]+grid.rate[(i+1)*AGES+j]+grid.rate[i*AGES+j+1]+grid.rate[(i+1)*AGES+j+1]);
                const double sigma=rate*std::log(betas[i+1]/betas[i])*(ages[j+1]-ages[j])/std::max(area,ejection*ejection);
                const double layer=std::clamp(2*ejection/std::sqrt(std::max(area,1.)),.02,1.);
                for(const auto n:{i*AGES+j,(i+1)*AGES+j,i*AGES+j+1,(i+1)*AGES+j+1}){sum[n]+=sigma;count[n]+=1;spread[n]=std::min(spread[n],layer);}
            }
            for(std::size_t i=0;i<BETAS;++i)for(std::size_t j=0;j<AGES;++j){
                const std::size_t n=i*AGES+j;const auto p=graphical(node(i,j));positions->setXYZ(n,p.x,p.y,p.z);
                const auto along=node(std::min(i+1,BETAS-1),j)-node(i>0?i-1:0,j),later=node(i,std::min(j+1,AGES-1))-node(i,j>0?j-1:0);
                Vector3 normal=graphical(core::Vector{along.y*later.z-along.z*later.y,along.z*later.x-along.x*later.z,along.x*later.y-along.y*later.x});
                if(normal.lengthSq()>0)normal.normalize();else normal.set(0,1,0);
                normals->setXYZ(n,normal.x,normal.y,normal.z);
                // Soft borders: the youngest grains hand over from the coma, the oldest leave,
                // and the beta extremes taper (sin^2 over 30% of the range on each side).
                auto taper=[](double t){t=std::clamp(t,0.,1.);const double s=std::sin(t*std::numbers::pi/2);return s*s;};
                const double age=static_cast<double>(j)/(AGES-1),size=static_cast<double>(i)/(BETAS-1);
                const float fade=static_cast<float>(taper(age/.25)*(1-std::pow(age,4))*taper(std::min(size,1-size)/.3));
                density->setX(n,static_cast<float>(count[n]>0?sum[n]/count[n]:0)*fade);thickness->setX(n,static_cast<float>(spread[n]));
            }
            positions->needsUpdate();normals->needsUpdate();density->needsUpdate();thickness->needsUpdate();
        }
    }
}
