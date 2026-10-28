// Reuse the launch bank's finite ray integration, stable noise and interior-camera handling.
// Water has its own density and lighting: white steam, ballistic crown, no trench or dust.
void splashdownPrograms(const char* surface,const std::vector<std::pair<std::string,int>>& attributes){
    using albert::gpu::Program;using albert::gpu::Uniform;
    Program p;p.attributes=attributes;p.uniforms={{"bank",Uniform::vec4},{"extent",Uniform::vec3},{"source",Uniform::vec3},{"samples",Uniform::f32},{"burst",Uniform::vec4}};
    std::string volume=launchCloudShader;
    volume.replace(volume.find("samples: f32 }"),14,"samples: f32, burst: vec4f }");
    const auto begin=volume.find("fn launchDensity"),end=volume.find("@fragment");
    volume.replace(begin,end-begin,R"(
fn launchDensity(p: vec3f, detail: bool) -> f32 {
    let h = p.z + material.extent.z * 0.45;
    let r = length(p.xy); let extent = material.extent.x;
    let scale = 5.0 + material.extent.z * 0.17;
    let flow = p + vec3f(0,0,-material.bank.x * 0.65);
    let coarse = cloudNoise(flow / scale);
    var fine = coarse; if (detail) { fine = cloudNoise(flow / (scale * 0.38) + vec3f(3,17,8)); }
    let angle = atan2(p.y,p.x);
    let front = extent * (0.68 + 0.08 * sin(angle * 7.0) + 0.05 * sin(angle * 11.0 + 0.8));
    let top = material.extent.z * (0.48 + 0.32 * coarse) * (1.0 - 0.4 * smoothstep(0.1,0.9,r/extent));
    let body = 1.0 - r*r/(front*front) - (h-top*0.3)*(h-top*0.3)/max(1.0,top*top*0.6) + 0.9*(coarse-0.5) + 0.3*(fine-0.5);
    let steam = smoothstep(0.0,0.6,body) * (0.5 + coarse) * material.bank.y;
    // A radial sheet rises, fans out, and falls; angular lobes break its rim into coherent fingers.
    let t = min(material.burst.x,6.0); let crownR = 8.0 + 10.0*t;
    let crownH = max(0.0,19.0*t-4.9*t*t) * (0.7+0.3*sin(angle*9.0+0.7));
    let dr = (length(p.xy-material.burst.zw)-crownR)/(3.0+2.0*t); let dz = (h-crownH)/(3.5+1.2*t);
    let crown = exp(-dr*dr-dz*dz) * material.burst.y * (0.4+coarse);
    return (steam + crown*1.6) * smoothstep(-0.3,1.2,h);
}
)");
    volume.replace(volume.find("let wet ="),volume.find("let distance =")-volume.find("let wet ="),R"(
            let albedo = vec3f(0.96,0.98,1.0);
            let ambient = 0.4 + 0.4 * smoothstep(0.0,material.extent.z,h);
            let lit = albedo * (sky * ambient + sunlight * (0.24 + 0.76 * direct)) * 0.36;
            // Primary bow centred on the antisolar direction, only on sunlit mist.
            let viewSun = dot(normalize(delta),sun);
            let rainbow = exp(-((viewSun-0.743)/0.018)*((viewSun-0.743)/0.018));
            let hue = clamp((viewSun-0.725)/0.035,0.0,1.0);
            let bow = mix(vec3f(0.3,0.4,1.0),vec3f(1.0,0.25,0.08),hue) * rainbow * sunlight * 0.045;
)");
    volume.replace(volume.find("let colour = lit +"),std::string("let colour = lit +").size(),"let colour = lit + bow +");
    // Density already includes steam decay and the independent impact crown.
    const auto density=volume.find("rho * material.bank.y * metres");volume.replace(density, std::string("rho * material.bank.y * metres").size(),"rho * metres");
    p.wgsl=std::string(surface)+volume;albert::gpu::define("albert:splashdown",p);
    p.uniforms={{"state",Uniform::vec4},{"area",Uniform::vec4}};
    const std::string noise=std::string(launchCloudShader).substr(std::string(launchCloudShader).find("fn cloudHash"),std::string(launchCloudShader).find("fn launchDensity")-std::string(launchCloudShader).find("fn cloudHash"));
    p.wgsl=noise+R"(
struct Material { state: vec4f, area: vec4f };
@group(2) @binding(0) var<uniform> material: Material;
struct Surface { @builtin(position) clip: vec4f, @location(0) local: vec2f, @location(1) world: vec3f, @location(2) normal: vec3f };
fn foamNoise(q: vec2f) -> f32 {
    return 0.68*cloudNoise(vec3f(q*0.16,0.0))+0.32*cloudNoise(vec3f(q*0.43,7.0));
}
@vertex fn vs_main(@location(0) position: vec3f, @location(1) normal: vec3f, @location(2) uv: vec2f) -> Surface {
    var o: Surface;let basis=mat3x3f(object.model[0].xyz,object.model[1].xyz,object.model[2].xyz);
    let units=length(basis[0])/material.area.x;let up=normalize(basis[2]);
    var world=(object.model*vec4f(position,1)).xyz;
    let rel=(world-frame.ocean.xyz)*frame.oceanEast.w;
    let seaUV=vec2f(dot(rel,frame.oceanEast.xyz),dot(rel,frame.oceanNorth.xyz));
    var height=0.0;
    if(frame.ocean.w>0.0){for(var c=0;c<3;c++){let area=array<f32,3>(512,128,32);height+=textureSampleLevel(oceanDisplacement,oceanSampler,seaUV/area[c],c,0.0).y;}}
    let local=position.xy*material.area.xy;let r=length(local);let front=15.0+material.state.x*2.1;
    let wave=sin((r-front)*1.1)*exp(-abs(r-front)*0.15)*exp(-material.state.z/35.0)*0.32;
    // The engine's ocean geometry is at mean sea level; its FFT waves currently deform normals.
    // Keep foam above that depth surface while crests and the disturbance lift it continuously.
    world+=up*(0.08+max(0.0,height+wave)*0.22)*units;o.local=local;o.world=world;o.normal=up;
    o.clip=frame.projection*frame.view*vec4f(world,1);return o;
}
@fragment fn fs_main(v: Surface) -> @location(0) vec4f {
    let r=length(v.local);let t=material.state.x;let front=min(material.area.x*0.86,15.0+t*2.1);
    let pixel=max(length(fwidth(v.local)),0.08);
    let noise=foamNoise(v.local-vec2f(t*0.25,-t*0.09));
    let width=3.5+pixel;let ring=exp(-((r-front)/width)*((r-front)/width));
    let ripple=0.12*(0.5+0.5*sin((r-front)*1.1))*exp(-abs(r-front)/18.0);
    let filtering=clamp(pixel*0.06,0.035,0.2);
    let field=(1.0-smoothstep(12.0+material.state.w*12.0,42.0+t*0.24,r)) * smoothstep(0.46-filtering,0.60+filtering,noise);
    let edge=1.0-smoothstep(material.area.x*0.88,material.area.x,r);
    let alpha=clamp((field*0.78+ring*0.8+ripple)*material.state.y*edge,0.0,0.94);
    if(alpha<0.003){discard;} // Empty foam pixels avoid atmosphere lookups and lighting.
    let light=airLight(v.world,v.normal);
    let glow=vec3f(1.0,0.22,0.04)*material.state.y*exp(-r*r/200.0)*exp(-material.state.z);
    let colour=vec3f(0.83,0.94,0.98)*(light.sky*frame.sunColor.rgb+frame.ambient.rgb+light.sun*frame.sunColor.rgb)*0.3+glow;
    return vec4f(finish(airApply(v.world,colour))*alpha,alpha);
}
)";
    albert::gpu::define("albert:sea-foam",p);
}
