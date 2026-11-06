// 独自実装。元FFPの有効stageをPSへ展開し、実Drawのscratch比較後だけ置換。
#include "surface_retry.h"
struct SurfaceProgram {std::string key;IDirect3DPixelShader9*shader=nullptr;surface_retry::Policy policy;unsigned material=0;};
std::vector<SurfaceProgram> surface_programs;
std::array<bool,2> surface_pairs{};
std::array<bool,32> surface_material_pairs{};
void release_surface_programs(){for(auto&p:surface_programs)if(p.shader)p.shader->Release();surface_programs.clear();}
float surface_float(DWORD v){float f;memcpy(&f,&v,4);return f;}
std::string surface_rgba(DWORD v){std::ostringstream s;s<<"float4("<<float((v>>16)&255)/255<<','<<float((v>>8)&255)/255<<','<<float(v&255)/255<<','<<float(v>>24)/255<<')';return s.str();}
std::string surface_arg(DWORD arg){
    std::string s;switch(arg&D3DTA_SELECTMASK){case D3DTA_DIFFUSE:s="vd";break;case D3DTA_CURRENT:s="cur";break;
    case D3DTA_TEXTURE:s="tex";break;case D3DTA_TFACTOR:s="tf";break;case D3DTA_SPECULAR:s="vs";break;
    case D3DTA_TEMP:s="tmp";break;case D3DTA_CONSTANT:s="constant";break;default:return {};}
    if(arg&~DWORD(D3DTA_SELECTMASK|D3DTA_COMPLEMENT|D3DTA_ALPHAREPLICATE))return {};
    if(arg&D3DTA_ALPHAREPLICATE)s="("+s+").aaaa";if(arg&D3DTA_COMPLEMENT)s="(1-("+s+"))";return "("+s+")";
}
std::string surface_op(DWORD op,const std::string&a,const std::string&b,const std::string&c){
    switch(op){case D3DTOP_SELECTARG1:return a;case D3DTOP_SELECTARG2:return b;
    case D3DTOP_MODULATE:return a+"*"+b;case D3DTOP_MODULATE2X:return "2*"+a+"*"+b;case D3DTOP_MODULATE4X:return "4*"+a+"*"+b;
    case D3DTOP_ADD:return a+"+"+b;case D3DTOP_ADDSIGNED:return a+"+"+b+"-.5";case D3DTOP_SUBTRACT:return a+"-"+b;
    case D3DTOP_BLENDDIFFUSEALPHA:return "lerp("+b+","+a+",vd.a)";case D3DTOP_BLENDTEXTUREALPHA:return "lerp("+b+","+a+",tex.a)";
    case D3DTOP_BLENDFACTORALPHA:return "lerp("+b+","+a+",tf.a)";case D3DTOP_BLENDCURRENTALPHA:return "lerp("+b+","+a+",cur.a)";
    case D3DTOP_MODULATEALPHA_ADDCOLOR:return a+"+"+a+".aaaa*"+b;case D3DTOP_MODULATECOLOR_ADDALPHA:return a+"*"+b+"+"+a+".aaaa";
    case D3DTOP_MULTIPLYADD:return a+"*"+b+"+"+c;case D3DTOP_LERP:return "lerp("+b+","+a+","+c+")";default:return {};}
}
bool surface_source(IDirect3DDevice9*d,bool wet,std::string&source){
    DWORD factor=0,specular=0,fog=0,table=0,vertex=0,range=0,fogcolor=0,start=0,end=0,density=0;bool ok=true;
    auto get=[&](D3DRENDERSTATETYPE k,DWORD&v){ok &= SUCCEEDED(d->GetRenderState(k,&v));};
    get(D3DRS_TEXTUREFACTOR,factor);get(D3DRS_SPECULARENABLE,specular);get(D3DRS_FOGENABLE,fog);get(D3DRS_FOGTABLEMODE,table);
    get(D3DRS_FOGVERTEXMODE,vertex);get(D3DRS_RANGEFOGENABLE,range);get(D3DRS_FOGCOLOR,fogcolor);get(D3DRS_FOGSTART,start);get(D3DRS_FOGEND,end);get(D3DRS_FOGDENSITY,density);
    if(!ok)return false;unsigned fm=fog?(table?table:vertex):0;
    // Camera-distance reconstruction is compared against the actual FFP below;
    // vertex-fog interpolation is not assumed equivalent just from its flag.
    if(fm>D3DFOG_LINEAR)return false;
    float fs=surface_float(start),fe=surface_float(end),fd=surface_float(density);
    if(fm&&(!std::isfinite(fs)||!std::isfinite(fe)||!std::isfinite(fd)||(fm==D3DFOG_LINEAR&&fs==fe)))return false;
    std::ostringstream s;
    for(unsigned i=0;i<5;++i){Ref<IDirect3DBaseTexture9>t;if(FAILED(d->GetTexture(i,&t.p)))return false;
        s<<(t.p&&t.p->GetType()==D3DRTYPE_CUBETEXTURE?"samplerCUBE":"sampler2D")<<" S"<<i<<":register(s"<<i<<");\n";}
    s<<"float4 Light:register(c0);float4 Opt:register(c1);\n"
    <<"float4 main(float4 U0:TEXCOORD0,float4 U1:TEXCOORD1,float4 U2:TEXCOORD2,float4 U3:TEXCOORD3,float4 U4:TEXCOORD4,float3 p:TEXCOORD7,float4 vd:COLOR0,float4 vs:COLOR1):COLOR0{\n"
    <<"float4 tf="<<surface_rgba(factor)<<",cur=vd,tmp=0;float3 raw=0;\n";
    unsigned active=0;
    for(unsigned i=0;i<8;++i){DWORD op=0;if(FAILED(d->GetTextureStageState(i,D3DTSS_COLOROP,&op)))return false;
        if(op==D3DTOP_DISABLE)break;if(i>=5)return false;
        DWORD ca[3]{},aa[3]{},ao=0,result=0,constant=0,transform=0,tc=0;
        const D3DTEXTURESTAGESTATETYPE ckeys[]={D3DTSS_COLORARG1,D3DTSS_COLORARG2,D3DTSS_COLORARG0},akeys[]={D3DTSS_ALPHAARG1,D3DTSS_ALPHAARG2,D3DTSS_ALPHAARG0};
        for(unsigned j=0;j<3;++j){ok &= SUCCEEDED(d->GetTextureStageState(i,ckeys[j],&ca[j]));ok &= SUCCEEDED(d->GetTextureStageState(i,akeys[j],&aa[j]));}
        ok &= SUCCEEDED(d->GetTextureStageState(i,D3DTSS_ALPHAOP,&ao));ok &= SUCCEEDED(d->GetTextureStageState(i,D3DTSS_RESULTARG,&result));
        ok &= SUCCEEDED(d->GetTextureStageState(i,D3DTSS_CONSTANT,&constant));ok &= SUCCEEDED(d->GetTextureStageState(i,D3DTSS_TEXTURETRANSFORMFLAGS,&transform));ok &= SUCCEEDED(d->GetTextureStageState(i,D3DTSS_TEXCOORDINDEX,&tc));
        Ref<IDirect3DBaseTexture9>t;ok &= SUCCEEDED(d->GetTexture(i,&t.p));
        bool cube=t.p&&t.p->GetType()==D3DRTYPE_CUBETEXTURE;
        if(!ok||(t.p&&t.p->GetType()!=D3DRTYPE_TEXTURE&&!cube)||(result!=D3DTA_CURRENT&&result!=D3DTA_TEMP))return false;
        if((transform&~DWORD(D3DTTFF_PROJECTED|7))||((transform&7)>4)||((transform&D3DTTFF_PROJECTED)&&((transform&7)<3)))return false;
        if(i==0&&(tc!=0||transform!=0||!t.p||cube||ca[0]!=D3DTA_TEXTURE||
            (op!=D3DTOP_SELECTARG1&&((op!=D3DTOP_MODULATE&&op!=D3DTOP_MODULATE2X)||ca[1]!=D3DTA_DIFFUSE))))return false;
        std::string uv="U"+std::to_string(i)+(cube?".xyz":".xy");
        if(transform&D3DTTFF_PROJECTED){if(cube&&(transform&7)!=4)return false;uv="("+uv+"/U"+std::to_string(i)+((transform&7)==3?".z)":".w)");}
        std::string a[3],b[3];for(unsigned j=0;j<3;++j){a[j]=surface_arg(ca[j]);b[j]=surface_arg(aa[j]);if(a[j].empty()||b[j].empty())return false;}
        auto rgb=surface_op(op,a[0],a[1],a[2]),alpha=ao==D3DTOP_DISABLE?std::string("cur"):surface_op(ao,b[0],b[1],b[2]);if(rgb.empty()||alpha.empty())return false;
        if(!t.p){bool reads=(op!=D3DTOP_SELECTARG2&&(ca[0]&D3DTA_SELECTMASK)==D3DTA_TEXTURE)||(op!=D3DTOP_SELECTARG1&&(ca[1]&D3DTA_SELECTMASK)==D3DTA_TEXTURE)||
            (ao!=D3DTOP_DISABLE&&ao!=D3DTOP_SELECTARG2&&(aa[0]&D3DTA_SELECTMASK)==D3DTA_TEXTURE)||(ao!=D3DTOP_DISABLE&&ao!=D3DTOP_SELECTARG1&&(aa[1]&D3DTA_SELECTMASK)==D3DTA_TEXTURE);if(reads)return false;}
        s<<"{float4 constant="<<surface_rgba(constant)<<",tex="<<(t.p?std::string(cube?"texCUBE(S":"tex2D(S")+std::to_string(i)+","+uv+")":"float4(1,1,1,1)")<<";\n";
        if(i==0)s<<"raw=tex.rgb;\n";
        s<<"float4 next=float4(saturate("<<rgb<<").rgb,saturate("<<alpha<<").a);"<<(result==D3DTA_TEMP?"tmp":"cur")<<"=next;}\n";++active;
    }
    if(!active)return false;if(specular)s<<"cur.rgb=saturate(cur.rgb+vs.rgb);\n";
    Ref<IDirect3DBaseTexture9>diffuse;D3DSURFACE_DESC dd{};
    if(FAILED(d->GetTexture(0,&diffuse.p))||!diffuse.p||diffuse.p->GetType()!=D3DRTYPE_TEXTURE||FAILED(static_cast<IDirect3DTexture9*>(diffuse.p)->GetLevelDesc(0,&dd))||!dd.Width||!dd.Height)return false;
    // 元Diffuseと同じUVの有限差分から近似bumpを作る。焼込み陰影も含むため
    // 実形状/測定normal mapではない。texture/lighting chainは消さない。
    s<<"if(Opt.x>0.5){float3 n=normalize(cross(ddy(p),ddx(p)));if(dot(n,-p)<0)n=-n;"
    <<"float3 px=ddx(p),py=ddy(p);float2 ux=ddx(U0.xy),uy=ddy(U0.xy);float det=ux.x*uy.y-ux.y*uy.x;"
    <<"float3 t=(px*uy.y-py*ux.y)/(abs(det)>.0000001?det:1),b=(-px*uy.x+py*ux.x)/(abs(det)>.0000001?det:1);"
    <<"t=normalize(t-n*dot(n,t));b=normalize(cross(n,t))*(dot(cross(n,t),b)<0?-1:1);"
    <<"float2 stepUV=float2("<<1.f/dd.Width<<','<<1.f/dd.Height<<");float3 lum=float3(.2126,.7152,.0722);"
    <<"float dx=dot(tex2D(S0,U0.xy+float2(stepUV.x,0)).rgb-tex2D(S0,U0.xy-float2(stepUV.x,0)).rgb,lum);"
    <<"float dy=dot(tex2D(S0,U0.xy+float2(0,stepUV.y)).rgb-tex2D(S0,U0.xy-float2(0,stepUV.y)).rgb,lum);"
    <<"if(abs(det)>.0000001)n=normalize(n-(t*dx+b*dy)*"<<(wet?"1.5":"4.0")<<");"
    <<"float3 l=normalize(Light.xyz),v=normalize(-p),halfv=normalize(l+v);float ndl=saturate(dot(n,l)),ndv=max(.05,saturate(dot(n,v))),ndh=saturate(dot(n,halfv)),vdh=saturate(dot(v,halfv));"
    <<"float rough="<<(wet?".28":".82")<<",a2=rough*rough*rough*rough;float D=a2/(3.14159265*pow(ndh*ndh*(a2-1)+1,2)),k=pow(rough+1,2)/8;"
    <<"float G=(ndl/(ndl*(1-k)+k))*(ndv/(ndv*(1-k)+k)),F=.04+.96*pow(1-vdh,5),spec=D*G*F/max(.02,4*ndl*ndv);"
    <<"float dark=saturate(dot(cur.rgb,float3(.2126,.7152,.0722))*4);cur.rgb=saturate(cur.rgb*(.40+.95*ndl)*float3(1.04,1.0,.92)+raw*spec*ndl*dark*.45);}\n";
    if(fm){s<<"float distance="<<(range?"length(p)":"p.z")<<",f=";
        if(fm==D3DFOG_LINEAR)s<<"saturate(("<<fe<<"-distance)/("<<fe-fs<<"))";else if(fm==D3DFOG_EXP)s<<"saturate(exp(-"<<fd<<"*distance))";else s<<"saturate(exp(-pow("<<fd<<"*distance,2)))";
        s<<";cur.rgb=lerp("<<surface_rgba(fogcolor)<<".rgb,cur.rgb,f);\n";}
    s<<"return cur;}";source=s.str();return true;
}
SurfaceProgram*surface_program(IDirect3DDevice9*d,int mat){
    std::string code;if(!surface_source(d,materials[mat].role=="wet_bsp",code))return nullptr;std::string key=std::to_string(mat)+code;
    for(auto&p:surface_programs)if(p.key==key)return &p;if(surface_programs.size()>=24)return nullptr;
    using Compile=HRESULT(WINAPI*)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
    auto lib=LoadLibraryW(L"d3dcompiler_47.dll");auto fn=lib?(Compile)GetProcAddress(lib,"D3DCompile"):nullptr;
    Ref<ID3DBlob>binary,error;SurfaceProgram prog;prog.key=key;prog.material=unsigned(mat);ULONG refs_before=device_refs(d);
    HRESULT hr=fn?fn(code.data(),code.size(),"observed_ffp_surface",nullptr,nullptr,"main","ps_3_0",0,0,&binary.p,&error.p):E_FAIL;
    if(SUCCEEDED(hr))hr=d->CreatePixelShader((DWORD*)binary.p->GetBufferPointer(),&prog.shader);
    event("\"event\":\"material_program\",\"mip_file\":\""+materials[mat].name+"\",\"hr\":"+std::to_string(hr)+",\"error\":\""+text(error.p?(char*)error.p->GetBufferPointer():nullptr)+"\"");
    owned_refs+=device_refs(d)-refs_before;
    if(lib)FreeLibrary(lib);if(FAILED(hr))prog.policy.terminal=true;
    // Failed programs are cached too: no compile/log storm on every Draw.
    surface_programs.push_back(std::move(prog));return &surface_programs.back();
}
bool bind_surface(IDirect3DDevice9*d,SurfaceProgram&prog,const Mat&view,bool relight){
    auto l=shadow_core::transform({.35f,-.48f,.80f,0},view);float light[4]={l[0],l[1],l[2],0},options[4]={relight?1.f:0.f,0,0,0};
    return SUCCEEDED(d->SetPixelShader(prog.shader))&&SUCCEEDED(d->SetPixelShaderConstantF(0,light,1))&&SUCCEEDED(d->SetPixelShaderConstantF(1,options,1))&&
    generated(d,7,D3DTSS_TCI_CAMERASPACEPOSITION)&&SUCCEEDED(d->SetRenderState(D3DRS_FOGENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE))&&
    SUCCEEDED(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_EQUAL))&&SUCCEEDED(d->SetRenderState(D3DRS_COLORWRITEENABLE,7));
}
HRESULT replay_surface(IDirect3DDevice9*d,const DrawArgs&a,const std::vector<visible_geometry::Run>&runs){HRESULT hr=S_OK;
    for(auto&r:runs){hr=find(d)->dip(d,D3DPT_TRIANGLELIST,a.base,a.min,a.vertices,a.start+r.first*3,r.count);if(FAILED(hr))break;}return hr;}
bool test_surface(IDirect3DDevice9*d,SurfaceProgram&prog,const DrawArgs&a,const std::vector<visible_geometry::Run>&runs,const Mat&view){
    State saved(d);if(!saved.capture()){prog.policy.terminal=true;return false;}
    D3DSURFACE_DESC desc{};bool ok=SUCCEEDED(saved.rt.p->GetDesc(&desc))&&desc.MultiSampleType==D3DMULTISAMPLE_NONE;
    Ref<IDirect3DSurface9>original,reference;D3DLOCKED_RECT x{},y{};bool lx=false,ly=false;
    ok=ok&&SUCCEEDED(d->StretchRect(saved.rt.p,nullptr,color_target,nullptr,D3DTEXF_NONE))&&SUCCEEDED(d->SetDepthStencilSurface(nullptr))&&
    SUCCEEDED(d->SetRenderTarget(0,color_target))&&SUCCEEDED(d->SetDepthStencilSurface(saved.z.p))&&bind_surface(d,prog,view,false)&&SUCCEEDED(replay_surface(d,a,runs));
    ok=saved.restore()&&ok;
    if(ok)ok=SUCCEEDED(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&original.p,nullptr))&&
    SUCCEEDED(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&reference.p,nullptr))&&
    SUCCEEDED(d->GetRenderTargetData(saved.rt.p,original.p))&&SUCCEEDED(d->GetRenderTargetData(color_target,reference.p));
    uint64_t changed=0;unsigned maxerr=0;
    if(ok){lx=SUCCEEDED(original.p->LockRect(&x,nullptr,D3DLOCK_READONLY));ly=SUCCEEDED(reference.p->LockRect(&y,nullptr,D3DLOCK_READONLY));ok=lx&&ly&&x.pBits&&y.pBits&&frame_capture::pitch_ok(x.Pitch)&&frame_capture::pitch_ok(y.Pitch);}
    if(ok)for(unsigned row=0;row<desc.Height;++row){auto*u=(unsigned char*)x.pBits+size_t(row)*x.Pitch;auto*v=(unsigned char*)y.pBits+size_t(row)*y.Pitch;
        for(unsigned col=0;col<desc.Width;++col){bool diff=false;for(unsigned c=0;c<3;++c){unsigned e=unsigned(std::abs(int(u[col*4+c])-int(v[col*4+c])));maxerr=std::max(maxerr,e);diff|=e!=0;}changed+=diff;}}
    if(lx)ok &= SUCCEEDED(original.p->UnlockRect());if(ly)ok &= SUCCEEDED(reference.p->UnlockRect());
    uint64_t visible=0;State second(d);bool captured=second.capture();
    bool q=ok&&captured&&maxerr<=1&&SUCCEEDED(d->StretchRect(saved.rt.p,nullptr,color_target,nullptr,D3DTEXF_NONE))&&SUCCEEDED(d->SetDepthStencilSurface(nullptr))&&
    SUCCEEDED(d->SetRenderTarget(0,color_target))&&SUCCEEDED(d->SetDepthStencilSurface(saved.z.p))&&bind_surface(d,prog,view,true)&&SUCCEEDED(replay_surface(d,a,runs));
    if(captured)q=second.restore()&&q;if(q)q=SUCCEEDED(d->GetRenderTargetData(color_target,reference.p));lx=ly=false;
    if(q){lx=SUCCEEDED(original.p->LockRect(&x,nullptr,D3DLOCK_READONLY));ly=SUCCEEDED(reference.p->LockRect(&y,nullptr,D3DLOCK_READONLY));q=lx&&ly&&x.pBits&&y.pBits;}
    if(q)for(unsigned row=0;row<desc.Height;++row){auto*u=(unsigned char*)x.pBits+size_t(row)*x.Pitch;auto*v=(unsigned char*)y.pBits+size_t(row)*y.Pitch;for(unsigned col=0;col<desc.Width;++col)visible+=memcmp(u+col*4,v+col*4,3)!=0;}
    if(lx)q &= SUCCEEDED(original.p->UnlockRect());if(ly)q &= SUCCEEDED(reference.p->UnlockRect());
    prog.policy.result(ok&&q,maxerr,visible);
    event("\"event\":\"material_shader_same_frame\",\"mip_file\":\""+materials[prog.material].name+"\",\"frame\":"+std::to_string(st.frame)+
    ",\"comparison_available\":"+(ok?"true":"false")+",\"baseline_changed_pixels\":"+std::to_string(changed)+",\"baseline_max_byte_error\":"+std::to_string(maxerr)+
    ",\"relit_visible_changed_pixels\":"+std::to_string(visible)+",\"accepted\":"+(prog.policy.accepted?"true":"false")+",\"attempt\":"+std::to_string(prog.policy.attempts)+",\"terminal_rejection\":"+(prog.policy.terminal?"true":"false")+",\"tolerance_bytes\":1,\"draw_local_not_full_scene_proof\":true");return prog.policy.accepted;
}
uint64_t surface_candidate_key(IDirect3DDevice9*d,const DrawArgs&a,const std::vector<visible_geometry::Run>&runs,const Mat&view){
    uint64_t hash=1469598103934665603ULL;auto add=[&](const void*p,size_t n){auto*b=(const unsigned char*)p;while(n--){hash^=*b++;hash*=1099511628211ULL;}};
    add(&a.base,sizeof(a.base));add(&a.min,sizeof(a.min));add(&a.vertices,sizeof(a.vertices));add(&a.start,sizeof(a.start));
    for(auto&r:runs){add(&r.first,sizeof(r.first));add(&r.count,sizeof(r.count));}
    add(view.data(),sizeof(float)*view.size());D3DMATRIX world{};if(SUCCEEDED(d->GetTransform(D3DTS_WORLD,&world)))add(&world,sizeof(world));return hash;
}
void replace_surface(IDirect3DDevice9*d,const DrawArgs&a,const std::vector<visible_geometry::Run>&runs,const Mat&view,int mat){
    auto*p=surface_program(d,mat);if(!p){++material_fallback;return;}if(p->policy.begin(surface_candidate_key(d,a,runs,view)))test_surface(d,*p,a,runs,view);if(!p->policy.accepted){++material_fallback;return;}
    State saved(d);if(!saved.capture()){++errors;return;}unsigned kind=materials[mat].role=="wet_bsp"?0:1;bool pair=st.external_capture&&!surface_pairs[kind];HRESULT bh=E_FAIL,ah=E_FAIL;bool before=false,after=false;
    std::string label=kind?"material-wall":"material-road";if(pair)capture(d,label+"-before",bh,before);
    bool static_pair=st.external_capture&&materials[mat].role=="wall_static"&&!surface_material_pairs[mat];
    std::string static_label="material-static-"+materials[mat].name;HRESULT sb=E_FAIL,sa=E_FAIL;bool sbs=false,sas=false;
    if(static_pair)capture(d,static_label+"-before",sb,sbs);
    bool ok=bind_surface(d,*p,view,true)&&SUCCEEDED(replay_surface(d,a,runs)),restored=saved.restore();
    if(ok&&restored){if(kind){++queued_wall;++rendered_wall;}else{++queued_road;++rendered_road;}}else ++errors;
    if(static_pair){surface_material_pairs[mat]=true;capture(d,static_label+"-after",sa,sas);event("\"event\":\"material_static_same_frame_pair\",\"mip_file\":\""+materials[mat].name+"\",\"frame\":"+std::to_string(st.frame)+",\"before_saved\":"+(sbs?"true":"false")+",\"after_saved\":"+(sas?"true":"false")+",\"restored\":"+(restored?"true":"false")+",\"stage\":\"immediately_after_original_draw_before_later_scene_and_HUD\"");}
    if(pair){surface_pairs[kind]=true;capture(d,label+"-after",ah,after);event("\"event\":\"material_draw_same_frame_pair\",\"kind\":\""+label+"\",\"mip_file\":\""+materials[mat].name+
    "\",\"frame\":"+std::to_string(st.frame)+",\"before_saved\":"+(before?"true":"false")+",\"after_saved\":"+(after?"true":"false")+",\"restored\":"+(restored?"true":"false")+",\"stage\":\"immediately_after_original_draw_before_later_scene_and_HUD\"");}
}
