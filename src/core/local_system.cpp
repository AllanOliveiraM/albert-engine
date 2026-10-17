#include "local_system.hpp"
#include "bodies.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
namespace albert::core {
namespace {
const LocalOrbit definitions[]{
#include "local_orbits.inc"
};
constexpr double radians=std::numbers::pi/180.;
double length(Vector p){return std::hypot(p.x,p.y,p.z);}
int editDistance(std::string_view a,std::string_view b){
    // Optimal string alignment: insertion/deletion/substitution/transposition.
    std::vector<int> older(b.size()+1),previous(b.size()+1),row(b.size()+1);
    for(std::size_t j=0;j<=b.size();++j)previous[j]=static_cast<int>(j);
    for(std::size_t i=1;i<=a.size();++i){row[0]=static_cast<int>(i);
        for(std::size_t j=1;j<=b.size();++j){row[j]=std::min({row[j-1]+1,previous[j]+1,previous[j-1]+(a[i-1]!=b[j-1])});
            if(i>1&&j>1&&a[i-1]==b[j-2]&&a[i-2]==b[j-1])row[j]=std::min(row[j],older[j-2]+1);}
        older.swap(previous);previous.swap(row);
    }return previous.back();
}
}
std::span<const LocalOrbit> localOrbits(){return definitions;}
const LocalOrbit* localOrbit(std::string_view id){for(const auto& o:definitions)if(o.id==id)return &o;return nullptr;}
Sample localState(const LocalOrbit& o,double jd){
    if(!std::isfinite(jd))throw std::invalid_argument("Invalid local orbital epoch");
    const double mean=std::remainder((o.meanAnomaly+std::remainder((jd-o.epoch)*86400.,o.period)*o.meanMotion)*radians,2*std::numbers::pi);
    double eccentric=mean;
    for(int i=0;i<32;++i){const double step=(eccentric-o.e*std::sin(eccentric)-mean)/(1-o.e*std::cos(eccentric));eccentric-=step;if(std::abs(step)<1e-13)break;}
    const double c=std::cos(eccentric),s=std::sin(eccentric),root=std::sqrt(1-o.e*o.e),rate=o.meanMotion*radians/(1-o.e*c);
    const double w=o.periapsis*radians,n=o.node*radians,inc=o.inclination*radians;
    const Vector p{std::cos(n)*std::cos(w)-std::sin(n)*std::sin(w)*std::cos(inc),std::sin(n)*std::cos(w)+std::cos(n)*std::sin(w)*std::cos(inc),std::sin(w)*std::sin(inc)};
    const Vector q{-std::cos(n)*std::sin(w)-std::sin(n)*std::cos(w)*std::cos(inc),-std::sin(n)*std::sin(w)+std::cos(n)*std::cos(w)*std::cos(inc),std::cos(w)*std::sin(inc)};
    return {0,p*(o.a*(c-o.e))+q*(o.a*root*s),p*(-o.a*s*rate)+q*(o.a*root*c*rate),{},true};
}
void Scene::include(const LocalOrbit& o){
    if(localOrbit(o.id)!=&o)throw std::invalid_argument("Unknown local orbital definition");
    Object body;body.id=o.id;body.name=o.name;
    body.source="NASA/JPL Horizons: elementos embarcados; propagacao de dois corpos aproximada; superficie conforme assets locais";
    body.appearance={Shape::Sphere,{o.radius*2,o.radius*2,o.radius*2},o.category==Category::Comet?0x8f908eu:0xb6b2aau};
    body.samples.push_back({});create(std::move(body));objects_.at(std::string(o.id)).builtin=true;
}
void includeLocalSystem(Scene& scene){for(const auto& o:definitions)scene.include(o);}
void updateLocalSystem(Scene& scene,double utc,double jd){for(const auto& o:definitions){auto state=localState(o,jd);const auto parent=scene.evaluate(std::string(o.parent),utc).state;
    state.utcSeconds=utc;state.positionMeters=state.positionMeters+parent.positionMeters;state.velocityMetersPerSecond=state.velocityMetersPerSecond+parent.velocityMetersPerSecond;
    scene.builtinState(std::string(o.id),state);}}
Category category(std::string_view id){if(id=="sun")return Category::Star;if(id=="moon")return Category::Moon;if(id=="earth"||naturalBody(id))return Category::Planet;if(const auto* o=localOrbit(id))return o->category;return Category::External;}
const char* categoryName(Category c){switch(c){case Category::Star:return "Estrela";case Category::Planet:return "Planetas";case Category::Moon:return "Luas";case Category::Dwarf:return "Planetas anoes";case Category::Asteroid:return "Asteroides";case Category::Comet:return "Cometas";case Category::External:return "Objetos da API";}return "";}
std::string normalizedName(std::string_view text){std::string result;result.reserve(text.size());
    for(std::size_t i=0;i<text.size()&&result.size()<256;++i){const auto c=static_cast<unsigned char>(text[i]);
        if(c==0xc3&&i+1<text.size()){const auto d=static_cast<unsigned char>(text[++i]);char replacement=0;
            if((d>=0x80&&d<=0x85)||(d>=0xa0&&d<=0xa5))replacement='a';
            else if(d==0x87||d==0xa7)replacement='c';else if((d>=0x88&&d<=0x8b)||(d>=0xa8&&d<=0xab))replacement='e';
            else if((d>=0x8c&&d<=0x8f)||(d>=0xac&&d<=0xaf))replacement='i';else if(d==0x91||d==0xb1)replacement='n';
            else if((d>=0x92&&d<=0x96)||(d>=0xb2&&d<=0xb6))replacement='o';else if((d>=0x99&&d<=0x9c)||(d>=0xb9&&d<=0xbc))replacement='u';
            if(replacement)result+=replacement;
        }else if(c<128){if(c>='A'&&c<='Z')result+=char(c+32);else if((c>='a'&&c<='z')||(c>='0'&&c<='9'))result+=char(c);else if(!result.empty()&&result.back()!=' ')result+=' ';}
    }while(!result.empty()&&result.back()==' ')result.pop_back();return result;}
int matchScore(std::string_view raw,std::string_view name,std::string_view id){const auto query=normalizedName(raw);if(query.empty())return 0;
    int best=-1;for(const auto& value:{normalizedName(name),normalizedName(id)}){
        if(value==query)best=std::max(best,10000);
        else if(value.starts_with(query))best=std::max(best,9000-static_cast<int>(value.size()-query.size()));
        else if(value.find(query)!=std::string::npos)best=std::max(best,8000-static_cast<int>(value.size()-query.size()));
        const int distance=editDistance(query,value),limit=query.size()<3?0:query.size()<7?1:2;
        if(distance<=limit)best=std::max(best,7000-distance*100-static_cast<int>(value.size()));
        // Also match a token in multiword names (e.g. Churyumov).
        std::size_t begin=0;while(begin<value.size()){const auto end=value.find(' ',begin);const auto token=std::string_view(value).substr(begin,end==std::string::npos?value.size()-begin:end-begin);
            const int d=editDistance(query,token);if(d<=limit)best=std::max(best,7000-d*100-static_cast<int>(token.size()));if(end==std::string::npos)break;begin=end+1;}
    }return best;
}
double solarOrder(const Scene& scene,std::string_view id,double utc){
    // Satellites share their parent's heliocentric distance, then orbit radius.
    if(id=="moon")id="earth";
    double tie=0;if(const auto* o=localOrbit(id);o&&o->category==Category::Moon){tie=o->a*1e-6;id=o->parent;}
    const auto p=scene.evaluate(std::string(id),utc).state.positionMeters,solar=scene.evaluate("sun",utc).state.positionMeters;
    return length(p-solar)+tie;
}
}
