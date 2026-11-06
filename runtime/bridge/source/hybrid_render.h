// Private scene-aware experiment. New shader code, not SSSR/Remix code.
// Included in namespace shadowed after the retained public draw helpers.
namespace hybrid {
struct Material { unsigned w=0,h=0;D3DFORMAT format=D3DFMT_UNKNOWN;std::string name,role;std::vector<unsigned char>bytes; };
std::vector<Material> materials;
bool enabled=false,failed=false,have_view=false;unsigned mode=0;
uint64_t frame=UINT64_MAX,done=UINT64_MAX,draws=0,wet_draws=0,composites=0,restores_failed=0,errors=0;
Mat main_view{},main_projection{};std::array<float,3>eye{};
unsigned width=0,height=0;ULONG owned_refs=0;
IDirect3DTexture9*buffer=nullptr,*color=nullptr;
IDirect3DSurface9*target=nullptr,*color_target=nullptr;
IDirect3DPixelShader9*gbuffer_ps=nullptr,*composite_ps=nullptr,*material_ps=nullptr;
ID3DBlob*gbuffer_code=nullptr,*composite_code=nullptr,*material_code=nullptr;
struct Cached { IDirect3DBaseTexture9*texture=nullptr;int material=-1; };
std::vector<Cached> cache;
struct PixelDraw {IDirect3DBaseTexture9*texture=nullptr;int material=-1;DrawArgs args{};unsigned selected=0;DWORD fvf=0,tci=0,transform=0;UINT stride=0;};
std::vector<PixelDraw> pixel_draws;bool pixel_probe_done=false;
void clear_pixel_draws(){for(auto&r:pixel_draws)if(r.texture)r.texture->Release();pixel_draws.clear();}
bool measure=false,previous_valid=false,query_tried=false,query_supported=false;
LARGE_INTEGER previous_time{},clock_frequency{};
std::vector<double> intervals_off,intervals_ssr,composite_cpu,composite_gpu;
struct Ticket { IDirect3DQuery9*a=nullptr,*b=nullptr,*freq=nullptr,*disjoint=nullptr;bool pending=false; };
std::array<Ticket,3> tickets{};
std::vector<Mat> pair_views;
void release_surface_programs();
std::vector<Mat> material_pair_views;
struct Replay { IDirect3DStateBlock9*state=nullptr;DrawArgs args{};std::vector<visible_geometry::Run> runs;Mat view{};bool wet=false; };
std::vector<Replay> replays;
std::vector<Replay> reference_replays;
bool reference_attempted=false;
uint64_t queued_wall=0,queued_road=0,rendered_wall=0,rendered_road=0,material_fallback=0;
void clear_replays(){for(auto&r:replays)if(r.state)r.state->Release();replays.clear();}
void clear_reference_replays(){for(auto&r:reference_replays)if(r.state)r.state->Release();reference_replays.clear();}
std::string metric(const std::vector<double>&samples){auto v=samples;std::sort(v.begin(),v.end());double sum=0;for(auto x:v)sum+=x;
    return "{\"count\":"+std::to_string(v.size())+",\"mean_ms\":"+std::to_string(v.empty()?0:sum/v.size())+",\"p95_ms\":"+std::to_string(v.empty()?0:v[size_t(std::ceil(v.size()*.95))-1])+"}";}
void poll_gpu(){for(auto&t:tickets)if(t.pending){UINT64 a=0,b=0,f=0;BOOL disjoint=TRUE;
    auto x=t.a->GetData(&a,sizeof(a),0),y=t.b->GetData(&b,sizeof(b),0),z=t.freq->GetData(&f,sizeof(f),0),w=t.disjoint->GetData(&disjoint,sizeof(disjoint),0);
    if(x==S_FALSE||y==S_FALSE||z==S_FALSE||w==S_FALSE)continue;t.pending=false;
    if(SUCCEEDED(x)&&SUCCEEDED(y)&&SUCCEEDED(z)&&SUCCEEDED(w)&&!disjoint&&f&&b>=a&&composite_gpu.size()<512)composite_gpu.push_back(1000.*double(b-a)/double(f));}}
Ticket*begin_gpu(IDirect3DDevice9*d){if(!measure||mode!=4)return nullptr;
    if(!query_tried){query_tried=true;ULONG before=device_refs(d);query_supported=true;
        for(auto&t:tickets){query_supported &= SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&t.a));query_supported &= SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&t.b));
            query_supported &= SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&t.freq));query_supported &= SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&t.disjoint));}
        owned_refs+=device_refs(d)-before;event("\"event\":\"hybrid_gpu_timing\",\"supported\":"+std::string(query_supported?"true":"false")+",\"blocking_getdata\":false");}
    if(!query_supported)return nullptr;poll_gpu();for(auto&t:tickets)if(!t.pending){
        if(FAILED(t.disjoint->Issue(D3DISSUE_BEGIN))||FAILED(t.a->Issue(D3DISSUE_END)))return nullptr;return &t;}return nullptr;
}
void end_gpu(Ticket*t){if(t){bool ok=SUCCEEDED(t->b->Issue(D3DISSUE_END))&&SUCCEEDED(t->freq->Issue(D3DISSUE_END))&&SUCCEEDED(t->disjoint->Issue(D3DISSUE_END));t->pending=ok;}}
void frame_time(bool capture_frame){if(!measure)return;LARGE_INTEGER now{};QueryPerformanceCounter(&now);
    if(previous_valid&&!capture_frame){double ms=1000.*double(now.QuadPart-previous_time.QuadPart)/double(clock_frequency.QuadPart);
        auto&v=mode==4?intervals_ssr:intervals_off;if((mode==0||mode==4)&&v.size()<1024)v.push_back(ms);}
    previous_time=now;previous_valid=!capture_frame;}
void clear_cache(){for(auto&c:cache)if(c.texture)c.texture->Release();cache.clear();}
void release(){release_surface_programs();clear_cache();clear_pixel_draws();clear_replays();clear_reference_replays();for(auto*p:{target,color_target})if(p)p->Release();target=color_target=nullptr;
    for(auto&t:tickets){for(auto*p:{t.a,t.b,t.freq,t.disjoint})if(p)p->Release();t={};}query_tried=query_supported=false;
    for(auto*p:{buffer,color})if(p)p->Release();buffer=color=nullptr;
    if(gbuffer_ps)gbuffer_ps->Release();if(composite_ps)composite_ps->Release();if(material_ps)material_ps->Release();gbuffer_ps=composite_ps=material_ps=nullptr;
    owned_refs=0;width=height=0;frame=done=UINT64_MAX;have_view=false;}
// State blocks retain streams, indices, transforms, all textures, declarations,
// shaders/constants and RS/SS. RT/DS/viewport are saved explicitly as well.
struct State {
    IDirect3DDevice9*d;Ref<IDirect3DStateBlock9>block;Ref<IDirect3DSurface9>rt,z;
    Ref<IDirect3DVertexBuffer9>vb;Ref<IDirect3DIndexBuffer9>ib;D3DVIEWPORT9 vp{};
    UINT offset=0,stride=0;DWORD fvf=0;
    bool captured=false,restored=false;
    explicit State(IDirect3DDevice9*x):d(x){}
    ~State(){if(captured&&!restored)restore();}
    bool capture(){captured=SUCCEEDED(d->GetRenderTarget(0,&rt.p))&&SUCCEEDED(d->GetDepthStencilSurface(&z.p))&&
        SUCCEEDED(d->GetViewport(&vp))&&SUCCEEDED(d->GetFVF(&fvf))&&SUCCEEDED(d->GetStreamSource(0,&vb.p,&offset,&stride))&&
        SUCCEEDED(d->GetIndices(&ib.p))&&SUCCEEDED(d->CreateStateBlock(D3DSBT_ALL,&block.p))&&block.p;
        D3DCAPS9 caps{};captured=captured&&SUCCEEDED(d->GetDeviceCaps(&caps));
        for(unsigned i=1;i<std::min<DWORD>(4,caps.NumSimultaneousRTs);++i){Ref<IDirect3DSurface9>other;auto hr=d->GetRenderTarget(i,&other.p);if(other.p||(FAILED(hr)&&hr!=D3DERR_NOTFOUND))captured=false;}
        return captured;}
    bool restore(){restored=true;bool ok=true;for(unsigned i=0;i<3;++i)ok &= SUCCEEDED(d->SetTexture(i,nullptr));
        ok &= SUCCEEDED(d->SetDepthStencilSurface(nullptr));ok &= SUCCEEDED(d->SetRenderTarget(0,rt.p));
        ok &= SUCCEEDED(d->SetDepthStencilSurface(z.p))&&SUCCEEDED(block.p->Apply())&&SUCCEEDED(d->SetViewport(&vp));
        Ref<IDirect3DSurface9>r2,z2;Ref<IDirect3DVertexBuffer9>v2;Ref<IDirect3DIndexBuffer9>i2;DWORD f2=0;UINT o2=0,s2=0;
        ok &= SUCCEEDED(d->GetRenderTarget(0,&r2.p))&&same(r2.p,rt.p)&&SUCCEEDED(d->GetDepthStencilSurface(&z2.p))&&same(z2.p,z.p);
        ok &= SUCCEEDED(d->GetFVF(&f2))&&f2==fvf&&SUCCEEDED(d->GetStreamSource(0,&v2.p,&o2,&s2))&&v2.p==vb.p&&o2==offset&&s2==stride;
        ok &= SUCCEEDED(d->GetIndices(&i2.p))&&i2.p==ib.p;
        if(!ok){++restores_failed;failed=true;event("\"event\":\"hybrid_restore_failed\"");}return ok;}
};
const char*gbuffer_source=R"(
float4 Kind : register(c0);
float4 Projection : register(c1);
// Generated by the unchanged FFP world/view transform of this actual Draw.
float4 main(float3 p:TEXCOORD0):COLOR0 {
    float3 n=normalize(cross(ddy(p),ddx(p)));
    if(dot(n,-p)<0)n=-n;
    n/=max(abs(n.x)+abs(n.y)+abs(n.z),.000001);
    float2 oct=n.xy;
    if(n.z<0)oct=(1-abs(oct.yx))*float2(oct.x>=0?1:-1,oct.y>=0?1:-1);
    // Sign of z is the verified material mask. Visibility comes from replay
    // against the game's unchanged DS, not reconstructed device depth.
    return float4(oct,p.z*(Kind.x>0?1:-1),Kind.y);
})";
// Samples the actual FFP texture chain, not a guessed albedo from BackBuffer.
// Stage 1/2 stay in the chain; their semantic lightmap identity is unknown.
// Position derivatives give a geometric normal, not an acquired normal map.
const char*material_source=R"(
sampler2D Diffuse:register(s0); sampler2D Stage1:register(s1); sampler2D Stage2:register(s2);
float4 Light:register(c0); float4 Kind:register(c1);
float4 main(float2 uv0:TEXCOORD0,float2 uv1:TEXCOORD1,float2 uv2:TEXCOORD2,float3 p:TEXCOORD3):COLOR0 {
    float3 diffuse=tex2D(Diffuse,uv0).rgb;
    float3 originalChain=saturate(2*diffuse*tex2D(Stage1,uv1).rgb);
    originalChain=saturate(2*originalChain*tex2D(Stage2,uv2).rgb);
    float3 n=normalize(cross(ddy(p),ddx(p))); if(dot(n,-p)<0)n=-n;
    float3 l=normalize(Light.xyz), v=normalize(-p), h=normalize(l+v);
    // Trial parameters: designed directional light, not a game LightActor;
    // roughness/F0 are assumed, with no claim of occlusion or measured PBR.
    float ndl=saturate(dot(n,l)); float exponent=Kind.x>0.5?32:8;
    float spec=pow(saturate(dot(n,h)),exponent)*(Kind.x>0.5?.14:.035);
    float3 result=originalChain*(.48+.9*ndl+spec);
    return float4(saturate(result),1);
})";
const char*composite_source=R"(
sampler2D Geometry : register(s0);
sampler2D Scene : register(s1);
float4 Projection : register(c0);
float4 Options : register(c1);
float3 decode(float2 e) {
    float3 n=float3(e,1-abs(e.x)-abs(e.y));
    float t=saturate(-n.z);n.xy+=float2(n.x>=0?-t:t,n.y>=0?-t:t);return normalize(n);
}
float3 position(float2 uv,float z) {return float3((uv.x*2-1-Projection.z)/Projection.x,(1-uv.y*2-Projection.w)/Projection.y,1)*z;}
float2 project(float3 p) {return float2((p.x*Projection.x/p.z+Projection.z)*.5+.5,.5-(p.y*Projection.y/p.z+Projection.w)*.5);}
struct O { float4 color:COLOR0; };
O main(float2 uv:TEXCOORD0) {
    O o;float4 g=tex2D(Geometry,uv);float4 original=tex2D(Scene,uv);
    o.color=original;
    if(abs(g.z)<.001)discard;
    float3 n=decode(g.xy);float3 p=position(uv,abs(g.z));
    if(Options.x<1.5){o.color=float4(n*.5+.5,1);return o;}
    if(Options.x<2.5){o.color=float4(abs(g.z)/4096,abs(g.z)/4096,abs(g.z)/4096,1);return o;}
     if(Options.x<3.5){o.color=g.z>0?float4(0,1,1,1):original;return o;}
    if(Options.x>5.5){float id=g.w;o.color=id>0?float4(frac(id*.618),frac(id*.381+.23),frac(id*.173+.47),1):original;return o;}
    if(Options.x>4.5)return o;
    if(g.z<0)return o;
    float3 incident=normalize(p),ray=reflect(incident,n);
    float previous=-1;float distance=12;float previousDistance=12;float2 hit=0;float confidence=0;
    // Actual scene ray/depth intersection. No flip, mirror texture or history.
    [loop] for(int i=0;i<48;i++) {
        distance+=8+float(i)*1.6;
        float3 q=p+n*1.5+ray*distance;if(q.z<5)break;
        float2 sampleUV=project(q);if(any(sampleUV<.002)||any(sampleUV>.998))break;
        float sampleZ=abs(tex2Dlod(Geometry,float4(sampleUV,0,0)).z);
        float gap=q.z-sampleZ;
        float thickness=max(2,sampleZ*.003);
        if(sampleZ>5 && previous<0 && gap>=0 && distance>24) {
            float low=previousDistance,high=distance;
            [unroll] for(int j=0;j<5;j++) {
                float middle=(low+high)*.5;float3 test=p+n*1.5+ray*middle;
                float2 testUV=project(test);float testZ=abs(tex2Dlod(Geometry,float4(testUV,0,0)).z);
                if(testZ>5 && test.z>=testZ)high=middle;else low=middle;
            }
            float3 end=p+n*1.5+ray*high;hit=project(end);
            float endZ=abs(tex2Dlod(Geometry,float4(hit,0,0)).z);
            if(endZ>5 && abs(end.z-endZ)<thickness) {
                confidence=saturate(min(min(hit.x,1-hit.x),min(hit.y,1-hit.y))*24);break;
            }
        }
        if(sampleZ>5)previous=gap;else previous=-1;previousDistance=distance;
    }
    float fresnel=.04+.96*pow(1-saturate(dot(n,-incident)),5);
    // Explicit trial dielectric/F0, not recovered game roughness/PBR assets.
    float weight=min(.65,fresnel)*confidence;
    float3 reflected=tex2Dlod(Scene,float4(hit,0,0)).rgb;
    o.color=float4(lerp(original.rgb,reflected,weight),original.a);return o;
})";
bool compile_shaders(IDirect3DDevice9*d){
    if(gbuffer_ps&&composite_ps&&material_ps)return true;
    using Compile=HRESULT(WINAPI*)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
    auto lib=LoadLibraryW(L"d3dcompiler_47.dll");auto fn=lib?(Compile)GetProcAddress(lib,"D3DCompile"):nullptr;
    HRESULT a=E_FAIL,b=E_FAIL,c=E_FAIL;Ref<ID3DBlob>e1,e2,e3;
    if(fn){if(!gbuffer_code)a=fn(gbuffer_source,strlen(gbuffer_source),"actual_draw_gbuffer",nullptr,nullptr,"main","ps_3_0",0,0,&gbuffer_code,&e1.p);else a=S_OK;
        if(!composite_code)b=fn(composite_source,strlen(composite_source),"actual_scene_ssr",nullptr,nullptr,"main","ps_3_0",0,0,&composite_code,&e2.p);else b=S_OK;
        if(!material_code)c=fn(material_source,strlen(material_source),"actual_material_chain",nullptr,nullptr,"main","ps_3_0",0,0,&material_code,&e3.p);else c=S_OK;}
    bool ok=SUCCEEDED(a)&&SUCCEEDED(b)&&SUCCEEDED(c)&&gbuffer_code&&composite_code&&material_code;
    if(ok)ok=SUCCEEDED(d->CreatePixelShader((DWORD*)gbuffer_code->GetBufferPointer(),&gbuffer_ps))&&SUCCEEDED(d->CreatePixelShader((DWORD*)composite_code->GetBufferPointer(),&composite_ps))&&SUCCEEDED(d->CreatePixelShader((DWORD*)material_code->GetBufferPointer(),&material_ps));
    event("\"event\":\"hybrid_compile\",\"ok\":"+std::string(ok?"true":"false")+",\"gbuffer_hr\":"+std::to_string(a)+",\"ssr_hr\":"+std::to_string(b)+",\"material_hr\":"+std::to_string(c)+",\"gbuffer_error\":\""+text(e1.p?(char*)e1.p->GetBufferPointer():nullptr)+"\",\"ssr_error\":\""+text(e2.p?(char*)e2.p->GetBufferPointer():nullptr)+"\",\"material_error\":\""+text(e3.p?(char*)e3.p->GetBufferPointer():nullptr)+"\"");
    if(lib)FreeLibrary(lib);return ok;
}
bool resources(IDirect3DDevice9*d){if(buffer&&color&&gbuffer_ps&&composite_ps)return true;if(failed)return false;
    ULONG before=device_refs(d);Ref<IDirect3DSurface9>bb,game_depth;D3DSURFACE_DESC desc{},ds{};Ref<IDirect3D9>factory;D3DDEVICE_CREATION_PARAMETERS cp{};D3DDISPLAYMODE display{};
    bool ok=SUCCEEDED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb.p))&&SUCCEEDED(bb.p->GetDesc(&desc))&&desc.MultiSampleType==D3DMULTISAMPLE_NONE;
    ok=ok&&SUCCEEDED(d->GetDepthStencilSurface(&game_depth.p))&&game_depth.p&&SUCCEEDED(game_depth.p->GetDesc(&ds))&&ds.MultiSampleType==D3DMULTISAMPLE_NONE;
    ok=ok&&SUCCEEDED(d->GetDirect3D(&factory.p))&&SUCCEEDED(d->GetCreationParameters(&cp))&&SUCCEEDED(factory.p->GetAdapterDisplayMode(cp.AdapterOrdinal,&display));
    HRESULT fmt=E_FAIL,depth_hr=E_FAIL;
    if(ok){fmt=factory.p->CheckDeviceFormat(cp.AdapterOrdinal,cp.DeviceType,display.Format,D3DUSAGE_RENDERTARGET,D3DRTYPE_TEXTURE,D3DFMT_A32B32G32R32F);
        depth_hr=factory.p->CheckDepthStencilMatch(cp.AdapterOrdinal,cp.DeviceType,display.Format,D3DFMT_A32B32G32R32F,ds.Format);}
    ok=ok&&SUCCEEDED(fmt)&&SUCCEEDED(depth_hr)&&compile_shaders(d);
    if(ok){width=desc.Width;height=desc.Height;
        ok=SUCCEEDED(d->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&buffer,nullptr))&&
           SUCCEEDED(buffer->GetSurfaceLevel(0,&target))&&
           SUCCEEDED(d->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,desc.Format,D3DPOOL_DEFAULT,&color,nullptr))&&SUCCEEDED(color->GetSurfaceLevel(0,&color_target));}
    owned_refs=device_refs(d)-before;
    event("\"event\":\"hybrid_resources\",\"ok\":"+std::string(ok?"true":"false")+",\"format_hr\":"+std::to_string(fmt)+",\"depth_hr\":"+std::to_string(depth_hr)+",\"width\":"+std::to_string(width)+",\"height\":"+std::to_string(height)+",\"estimated_payload_bytes\":"+std::to_string(uint64_t(width)*height*20)+",\"owned_device_refs\":"+std::to_string(owned_refs));
    if(!ok){release();failed=true;}return ok;
}
void load(){enabled=GetFileAttributesW((directory+L"hybrid-scene.txt").c_str())!=INVALID_FILE_ATTRIBUTES;if(!enabled)return;
    auto*f=_wfopen((directory+L"hybrid-materials.txt").c_str(),L"rt");if(!f){failed=true;return;}
    unsigned w=0,h=0;char fmt[16]{},name[80]{},role[16]{};
    while(materials.size()<32&&fscanf(f,"%u %u %15s %79s %15s",&w,&h,fmt,name,role)==5){
        if(!w||!h||w>2048||h>2048||strchr(name,'/')||strchr(name,'\\')||strstr(name,"..")){failed=true;break;}
        D3DFORMAT format=!strcmp(fmt,"DXT1")?D3DFMT_DXT1:!strcmp(fmt,"DXT5")?D3DFMT_DXT5:D3DFMT_UNKNOWN;
        if(format==D3DFMT_UNKNOWN){failed=true;break;}size_t n=size_t((w+3)/4)*((h+3)/4)*(format==D3DFMT_DXT1?8:16);
        std::string s=name;auto path=directory+std::wstring(s.begin(),s.end());auto*data=_wfopen(path.c_str(),L"rb");
        Material mat;mat.w=w;mat.h=h;mat.format=format;mat.name=name;mat.role=role;mat.bytes.resize(n);
        if(mat.role!="wet_bsp"&&mat.role!="wall_bsp"&&mat.role!="wall_static"){if(data)fclose(data);failed=true;break;}
        bool ok=data&&fread(mat.bytes.data(),1,n,data)==n&&fgetc(data)==EOF;if(data)fclose(data);if(!ok){failed=true;break;}materials.push_back(std::move(mat));}
    fclose(f);event("\"event\":\"hybrid_material_inputs\",\"count\":"+std::to_string(materials.size())+",\"exact_mip_comparison\":true");
}
int material(IDirect3DDevice9*d){Ref<IDirect3DBaseTexture9>base;if(FAILED(d->GetTexture(0,&base.p))||!base.p||base.p->GetType()!=D3DRTYPE_TEXTURE)return -1;
    for(auto&c:cache)if(c.texture==base.p)return c.material;
    auto*t=static_cast<IDirect3DTexture9*>(base.p);int found=-1;
    for(UINT level=0;level<t->GetLevelCount()&&found<0;++level){D3DSURFACE_DESC desc{};if(FAILED(t->GetLevelDesc(level,&desc)))continue;
        if(desc.Pool!=D3DPOOL_MANAGED||desc.Usage)continue;
        for(size_t i=0;i<materials.size();++i){auto&m=materials[i];if(desc.Width!=m.w||desc.Height!=m.h||desc.Format!=m.format)continue;
            D3DLOCKED_RECT rect{};if(FAILED(t->LockRect(level,&rect,nullptr,D3DLOCK_READONLY)))continue;
            size_t pitch=size_t((m.w+3)/4)*(m.format==D3DFMT_DXT1?8:16),rows=(m.h+3)/4;bool equal=rect.pBits&&rect.Pitch>=int(pitch);
            if(equal)for(size_t r=0;r<rows;++r)if(memcmp((char*)rect.pBits+r*rect.Pitch,m.bytes.data()+r*pitch,pitch)){equal=false;break;}
            bool unlocked=SUCCEEDED(t->UnlockRect(level));if(equal&&unlocked){found=int(i);break;}}}
    if(cache.size()<128){base.p->AddRef();cache.push_back({base.p,found});}
    static std::array<unsigned,32>reported{};if(found>=0&&reported[size_t(found)]++<2){std::ostringstream s;s<<"\"event\":\"hybrid_material_match\",\"stage\":0,\"mip_file\":\""<<materials[found].name<<"\",\"byte_exact\":true,\"stages\":[";
        for(unsigned i=0;i<3;++i){DWORD op=0,arg1=0,arg2=0,tci=0;d->GetTextureStageState(i,D3DTSS_COLOROP,&op);d->GetTextureStageState(i,D3DTSS_COLORARG1,&arg1);d->GetTextureStageState(i,D3DTSS_COLORARG2,&arg2);d->GetTextureStageState(i,D3DTSS_TEXCOORDINDEX,&tci);if(i)s<<',';s<<'['<<op<<','<<arg1<<','<<arg2<<','<<tci<<']';}s<<']';event(s.str());}return found;
}
// Bounded observations are independent Get calls, including failures. Numeric
// fog values are raw DWORDs (float render states retain their bit pattern).
// Matrices are inputs to FFP, not a claim that vertex varyings were captured.
void observe_material_state(IDirect3DDevice9*d,int mat,const DrawArgs&draw,const std::vector<visible_geometry::Run>&runs){
    static std::array<unsigned,32>seen{};if(seen[size_t(mat)]++>=2)return;
    D3DCAPS9 caps{};HRESULT ch=d->GetDeviceCaps(&caps);DWORD fvf=0;HRESULT fh=d->GetFVF(&fvf);
    Ref<IDirect3DVertexBuffer9>vb;UINT offset=0,stride=0;HRESULT vh=d->GetStreamSource(0,&vb.p,&offset,&stride);
    Ref<IDirect3DVertexShader9>vs;HRESULT vsh=d->GetVertexShader(&vs.p);
    Ref<IDirect3DPixelShader9>ps;HRESULT psh=d->GetPixelShader(&ps.p);
    std::ostringstream s;s<<"\"event\":\"material_state_detail\",\"frame\":"<<st.frame<<",\"mip_file\":\""<<materials[size_t(mat)].name
      <<"\",\"caps_hr\":"<<ch<<",\"fvf\":["<<fh<<','<<fvf<<"],\"stream\":["<<vh<<','<<offset<<','<<stride<<','<<(vb.p?1:0)
      <<"],\"vertex_shader\":["<<vsh<<','<<(vs.p?1:0)<<"],\"pixel_shader\":["<<psh<<','<<(ps.p?1:0)<<"],\"fog\":[";
    const D3DRENDERSTATETYPE fog[]={D3DRS_FOGENABLE,D3DRS_FOGCOLOR,D3DRS_FOGTABLEMODE,D3DRS_FOGVERTEXMODE,D3DRS_FOGSTART,D3DRS_FOGEND,D3DRS_FOGDENSITY,D3DRS_RANGEFOGENABLE};
    for(unsigned i=0;i<sizeof(fog)/sizeof(*fog);++i){DWORD value=0;HRESULT hr=d->GetRenderState(fog[i],&value);if(i)s<<',';s<<'['<<unsigned(fog[i])<<','<<hr<<','<<value<<']';}
    s<<"],\"render_states\":[";
    const D3DRENDERSTATETYPE rs[]={D3DRS_TEXTUREFACTOR,D3DRS_LIGHTING,D3DRS_NORMALIZENORMALS,D3DRS_SPECULARENABLE,D3DRS_AMBIENT,
        D3DRS_COLORVERTEX,D3DRS_DIFFUSEMATERIALSOURCE,D3DRS_SPECULARMATERIALSOURCE,D3DRS_AMBIENTMATERIALSOURCE,D3DRS_EMISSIVEMATERIALSOURCE,
        D3DRS_LOCALVIEWER,D3DRS_SHADEMODE,D3DRS_CULLMODE,D3DRS_SRGBWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,
        D3DRS_ALPHAFUNC,D3DRS_ALPHAREF,D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_STENCILENABLE,D3DRS_COLORWRITEENABLE};
    for(unsigned i=0;i<sizeof(rs)/sizeof(*rs);++i){DWORD value=0;HRESULT hr=d->GetRenderState(rs[i],&value);if(i)s<<',';s<<'['<<unsigned(rs[i])<<','<<hr<<','<<value<<']';}
    s<<"],\"matrices\":[";
    auto emit_matrix=[&](D3DTRANSFORMSTATETYPE which){D3DMATRIX m{};HRESULT hr=d->GetTransform(which,&m);s<<'['<<unsigned(which)<<','<<hr<<",[";
        const float*values=&m._11;for(unsigned j=0;j<16;++j){if(j)s<<',';if(SUCCEEDED(hr)&&std::isfinite(values[j]))s<<values[j];else s<<"null";}s<<"]]";};
    emit_matrix(D3DTS_WORLD);s<<',';emit_matrix(D3DTS_VIEW);s<<',';emit_matrix(D3DTS_PROJECTION);
    s<<"],\"stages\":[";
    const D3DTEXTURESTAGESTATETYPE kinds[]={D3DTSS_COLOROP,D3DTSS_COLORARG0,D3DTSS_COLORARG1,D3DTSS_COLORARG2,
        D3DTSS_ALPHAOP,D3DTSS_ALPHAARG0,D3DTSS_ALPHAARG1,D3DTSS_ALPHAARG2,D3DTSS_RESULTARG,D3DTSS_CONSTANT,
        D3DTSS_TEXCOORDINDEX,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTSS_BUMPENVMAT00,D3DTSS_BUMPENVMAT01,
        D3DTSS_BUMPENVMAT10,D3DTSS_BUMPENVMAT11,D3DTSS_BUMPENVLSCALE,D3DTSS_BUMPENVLOFFSET};
    const D3DSAMPLERSTATETYPE samplers[]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW,D3DSAMP_BORDERCOLOR,
        D3DSAMP_MAGFILTER,D3DSAMP_MINFILTER,D3DSAMP_MIPFILTER,D3DSAMP_MIPMAPLODBIAS,D3DSAMP_MAXMIPLEVEL,
        D3DSAMP_MAXANISOTROPY,D3DSAMP_SRGBTEXTURE,D3DSAMP_ELEMENTINDEX,D3DSAMP_DMAPOFFSET};
    unsigned count=SUCCEEDED(ch)?std::min<DWORD>(8,caps.MaxTextureBlendStages):0;
    for(unsigned i=0;i<count;++i){if(i)s<<',';Ref<IDirect3DBaseTexture9>t;HRESULT th=d->GetTexture(i,&t.p);
        s<<"{\"stage\":"<<i<<",\"texture\":["<<th<<','<<(t.p?1:0)<<','<<(t.p?unsigned(t.p->GetType()):0)<<"],\"states\":[";
        for(unsigned j=0;j<sizeof(kinds)/sizeof(*kinds);++j){DWORD value=0;HRESULT hr=d->GetTextureStageState(i,kinds[j],&value);if(j)s<<',';s<<'['<<unsigned(kinds[j])<<','<<hr<<','<<value<<']';}
        s<<"],\"samplers\":[";
        for(unsigned j=0;j<sizeof(samplers)/sizeof(*samplers);++j){DWORD value=0;HRESULT hr=d->GetSamplerState(i,samplers[j],&value);if(j)s<<',';s<<'['<<unsigned(samplers[j])<<','<<hr<<','<<value<<']';}
        s<<"],\"transform\":";emit_matrix(D3DTRANSFORMSTATETYPE(D3DTS_TEXTURE0+i));s<<'}';}
    s<<"],\"vertex_output\":\"not_captured; FVF inputs and transforms are not interpolated vertex outputs\"";
    event(s.str());
    // One actual indexed vertex from a matched triangle, only through the
    // existing safe managed-buffer or captured-write paths. It is an input,
    // not a measurement of the FFP vertex stage's interpolated output.
    Ref<IDirect3DIndexBuffer9>ib;D3DINDEXBUFFER_DESC id{};D3DVERTEXBUFFER_DESC vd{};
    bool ok=!runs.empty()&&draw.indexed&&SUCCEEDED(d->GetIndices(&ib.p))&&ib.p&&SUCCEEDED(ib.p->GetDesc(&id))&&
        vb.p&&SUCCEEDED(vb.p->GetDesc(&vd))&&fvf==530&&stride==40;
    unsigned bytes=id.Format==D3DFMT_INDEX16?2:id.Format==D3DFMT_INDEX32?4:0;uint32_t index=0;float vertex[10]{};uint64_t at=0;
    auto read_buffer=[&](auto*buffer,bool direct,UINT start,UINT n,void*out){
        if(!direct)return write_capture::store.sample((uintptr_t)buffer,start,n,out);
        void*p=nullptr;HRESULT hr=buffer->Lock(start,n,&p,D3DLOCK_READONLY);if(FAILED(hr))return false;
        if(p)memcpy(out,p,n);HRESULT uh=buffer->Unlock();return p&&SUCCEEDED(uh);};
    uint64_t first=uint64_t(draw.start)+uint64_t(runs.empty()?0:runs[0].first)*3;
    ok=ok&&bytes&&observation::span(id.Size,first*bytes,bytes);
    if(ok)ok=read_buffer(ib.p,eligible(id.Usage,id.Pool),UINT(first*bytes),bytes,&index);
    ok=ok&&index>=draw.min&&uint64_t(index)-draw.min<draw.vertices&&
        observation::vertex_offset(vd.Size,offset,stride,draw.base,index,at)&&observation::span(vd.Size,at,sizeof(vertex));
    if(ok)ok=read_buffer(vb.p,eligible(vd.Usage,vd.Pool),UINT(at),sizeof(vertex),vertex);
    if(ok)for(float v:vertex)ok &= std::isfinite(v);
    std::ostringstream v;v<<"\"event\":\"material_vertex_input\",\"frame\":"<<st.frame<<",\"mip_file\":\""<<materials[size_t(mat)].name
        <<"\",\"available\":"<<(ok?"true":"false")<<",\"index\":"<<index<<",\"fvf\":530,\"stride\":40";
    if(ok){v<<",\"xyz_normal_uv0_uv1\":[";for(unsigned i=0;i<10;++i){if(i)v<<',';v<<vertex[i];}v<<']';}
    v<<",\"is_vertex_output\":false";event(v.str());
}
#include "material_pipeline.h"
#include "input_probe.h"
// Retained 001 three-stage experiment, not used by the new replacement path.
// Only the already-observed FFP chain is reproduced. Unknown stage, UV,
// fog, texture or shader state falls back to the unchanged original Draw.
bool material_chain(IDirect3DDevice9*d,int mat){
    bool ok=equal(d,D3DRS_FOGENABLE,FALSE);std::ostringstream sample;
    sample<<"\"event\":\"material_chain_observed\",\"mip_file\":\""<<materials[size_t(mat)].name<<"\",\"fvf\":530,\"stages\":[";
    const DWORD ops[]={D3DTOP_SELECTARG1,D3DTOP_MODULATE2X,D3DTOP_MODULATE2X};
    const DWORD tcs[]={0,1,0};
    for(unsigned i=0;i<3;++i){DWORD op=0,a=0,b=0,tc=0,tf=0;Ref<IDirect3DBaseTexture9>t;
        HRESULT h0=d->GetTexture(i,&t.p),h1=d->GetTextureStageState(i,D3DTSS_COLOROP,&op),h2=d->GetTextureStageState(i,D3DTSS_COLORARG1,&a),
            h3=d->GetTextureStageState(i,D3DTSS_COLORARG2,&b),h4=d->GetTextureStageState(i,D3DTSS_TEXCOORDINDEX,&tc),h5=d->GetTextureStageState(i,D3DTSS_TEXTURETRANSFORMFLAGS,&tf);
        bool q=SUCCEEDED(h0)&&t.p&&SUCCEEDED(h1)&&SUCCEEDED(h2)&&SUCCEEDED(h3)&&SUCCEEDED(h4)&&SUCCEEDED(h5);
        ok &= q&&op==ops[i]&&a==D3DTA_TEXTURE&&(i==0||b==D3DTA_CURRENT)&&tc==tcs[i]&&tf==D3DTTFF_DISABLE;
        if(i)sample<<',';sample<<'['<<op<<','<<a<<','<<b<<','<<tc<<','<<tf<<','<<(t.p?1:0)<<']';}
    DWORD next=0;ok &= SUCCEEDED(d->GetTextureStageState(3,D3DTSS_COLOROP,&next))&&next==D3DTOP_DISABLE;
    static std::array<unsigned,32>seen{};if(seen[size_t(mat)]++<2){DWORD fog=0;d->GetRenderState(D3DRS_FOGENABLE,&fog);sample<<"],\"stage3_op\":"<<next<<",\"fog\":"<<fog<<",\"supported\":"<<(ok?"true":"false")<<",\"stage1_lightmap_identified\":false";event(sample.str());}
    return ok;
}
// The previous three-stage test does not establish FFP pixel equivalence:
// alpha, all active stages, lighting, fog and interpolated outputs still need
// same-frame proof. Never use the replacement shader on that test alone.
void queue_material(IDirect3DDevice9*d,const DrawArgs&a,const std::vector<visible_geometry::Run>&runs,const Mat&view,int mat){
    if((mode!=4&&mode!=5)||runs.empty())return;
    static std::array<unsigned,32>matched{};if(matched[size_t(mat)]++<2){unsigned triangles=0;for(auto&run:runs)triangles+=run.count;
        event("\"event\":\"material_geometry_match\",\"mip_file\":\""+materials[size_t(mat)].name+"\",\"kind\":\""+materials[size_t(mat)].role+"\",\"triangles\":"+std::to_string(triangles)+",\"byte_exact_mip\":true,\"saved_bsp_triangle\":"+(materials[mat].role=="wall_static"?"false":"true"));}
    observe_material_state(d,mat,a,runs);
    // Immediate replay retains later decals and avoids stale dynamic buffers.
    replace_surface(d,a,runs,view,mat);
}
void render_ffp_reference(IDirect3DDevice9*d){
    if(reference_attempted||reference_replays.empty()||!color_target)return;
    reference_attempted=true;State saved(d);if(!saved.capture()){++errors;clear_reference_replays();return;}
    D3DSURFACE_DESC desc{};HRESULT copied=E_FAIL;unsigned attempted=0,succeeded=0,road_candidates=0,wall_candidates=0;
    for(auto&r:reference_replays)if(r.wet)++road_candidates;else ++wall_candidates;
    bool ok=SUCCEEDED(saved.rt.p->GetDesc(&desc))&&frame_capture::supported(desc.Width,desc.Height,desc.Format,desc.MultiSampleType,desc.Pool,desc.Usage);
    if(ok){copied=d->StretchRect(saved.rt.p,nullptr,color_target,nullptr,D3DTEXF_NONE);ok=SUCCEEDED(copied);}
    for(auto&r:reference_replays){
        if(!ok)break;++attempted;
        bool bound=SUCCEEDED(r.state->Apply())&&SUCCEEDED(d->SetDepthStencilSurface(nullptr))&&SUCCEEDED(d->SetRenderTarget(0,color_target))&&
            SUCCEEDED(d->SetDepthStencilSurface(saved.z.p))&&SUCCEEDED(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE))&&
            SUCCEEDED(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_EQUAL));
        HRESULT hr=bound?S_OK:E_FAIL;
        if(bound)for(auto&run:r.runs){hr=find(d)->dip(d,D3DPT_TRIANGLELIST,r.args.base,r.args.min,r.args.vertices,r.args.start+run.first*3,run.count);if(FAILED(hr))break;}
        if(bound&&SUCCEEDED(hr))++succeeded;else ok=false;
    }
    bool restored=saved.restore();
    Ref<IDirect3DSurface9>original,reference;D3DLOCKED_RECT a{},b{};bool locked_a=false,locked_b=false;uint64_t rgb_changed=0,alpha_changed=0;
    if(ok&&restored)ok=SUCCEEDED(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&original.p,nullptr))&&original.p&&
        SUCCEEDED(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&reference.p,nullptr))&&reference.p&&
        SUCCEEDED(d->GetRenderTargetData(saved.rt.p,original.p))&&SUCCEEDED(d->GetRenderTargetData(color_target,reference.p));
    if(ok){locked_a=SUCCEEDED(original.p->LockRect(&a,nullptr,D3DLOCK_READONLY));locked_b=SUCCEEDED(reference.p->LockRect(&b,nullptr,D3DLOCK_READONLY));
        ok=locked_a&&locked_b&&a.pBits&&b.pBits&&frame_capture::pitch_ok(a.Pitch)&&frame_capture::pitch_ok(b.Pitch);}
    if(ok)for(unsigned y=0;y<desc.Height;++y){auto*x=(const unsigned char*)a.pBits+size_t(y)*a.Pitch;auto*z=(const unsigned char*)b.pBits+size_t(y)*b.Pitch;
        for(unsigned i=0;i<desc.Width;++i){rgb_changed+=memcmp(x+4*i,z+4*i,3)!=0;alpha_changed+=x[4*i+3]!=z[4*i+3];}}
    bool before=false,after=false;
    auto save_surface=[&](const wchar_t*name,const D3DLOCKED_RECT&rect){auto path=directory+name;FILE*f=_wfopen(path.c_str(),L"wbx");if(!f)return false;
        auto header=frame_capture::header();bool good=fwrite(header.data(),1,header.size(),f)==header.size();
        if(good)good=frame_capture::rows(rect.pBits,rect.Pitch,[&](const unsigned char*p,size_t n){return fwrite(p,1,n,f)==n;});if(fclose(f))good=false;return good;};
    if(ok){before=save_surface(L"backbuffer-ffp-reference-before.bmp",a);after=save_surface(L"backbuffer-ffp-reference-scratch.bmp",b);}
    if(locked_a)ok &= SUCCEEDED(original.p->UnlockRect());if(locked_b)ok &= SUCCEEDED(reference.p->UnlockRect());
    event("\"event\":\"ffp_reference_same_frame\",\"frame\":"+std::to_string(st.frame)+",\"attempted_draws\":"+std::to_string(attempted)+
        ",\"successful_draws\":"+std::to_string(succeeded)+",\"road_candidates\":"+std::to_string(road_candidates)+",\"wall_candidates\":"+std::to_string(wall_candidates)+
        ",\"copy_hr\":"+std::to_string(copied)+",\"comparison_available\":"+(ok?"true":"false")+
        ",\"rgb_changed_pixels\":"+(ok?std::to_string(rgb_changed):"null")+",\"alpha_changed_pixels\":"+(ok?std::to_string(alpha_changed):"null")+
        ",\"before_saved\":"+(before?"true":"false")+",\"scratch_saved\":"+(after?"true":"false")+
        ",\"restored\":"+(restored?"true":"false")+",\"stage\":\"before_weapon_and_HUD\",\"not_full_scene_equivalence\":true,\"zero_delta_not_proof_draw_visible\":true");
    if(!ok||!restored)++errors;clear_reference_replays();
}
void render_materials(IDirect3DDevice9*d){
    if(replays.empty())return;State saved(d);if(!saved.capture()){++errors;clear_replays();return;}
    bool distinct=material_pair_views.empty();if(!distinct)for(unsigned i=0;i<12;++i)distinct |= std::abs(material_pair_views[0][i]-main_view[i])>.01f;
    bool pair=st.external_capture&&material_pair_views.size()<2&&distinct;
    unsigned id=unsigned(material_pair_views.size())+1;HRESULT a=E_FAIL,b=E_FAIL;bool before=false,after=false;
    if(pair){material_pair_views.push_back(main_view);capture(d,"material-pair-"+std::to_string(id)+"-before",a,before);}
    for(auto&r:replays){
        auto light=shadow_core::transform({.35f,-.48f,.80f,0},r.view);float l[4]={light[0],light[1],light[2],0},k[4]={r.wet?1.f:0.f,0,0,0};
        bool ok=SUCCEEDED(r.state->Apply())&&SUCCEEDED(d->SetPixelShader(material_ps))&&SUCCEEDED(d->SetPixelShaderConstantF(0,l,1))&&
            SUCCEEDED(d->SetPixelShaderConstantF(1,k,1))&&generated(d,3,D3DTSS_TCI_CAMERASPACEPOSITION)&&
            SUCCEEDED(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_EQUAL))&&
            SUCCEEDED(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_COLORWRITEENABLE,7));
        HRESULT hr=ok?S_OK:E_FAIL;
        if(ok)for(auto&run:r.runs){hr=find(d)->dip(d,D3DPT_TRIANGLELIST,r.args.base,r.args.min,r.args.vertices,r.args.start+run.first*3,run.count);if(FAILED(hr))break;}
        if(ok&&SUCCEEDED(hr)){if(r.wet)++rendered_road;else ++rendered_wall;}else ++errors;
    }
    bool restored=saved.restore();if(pair){capture(d,"material-pair-"+std::to_string(id)+"-after",b,after);
        event("\"event\":\"material_same_frame_pair\",\"pair\":"+std::to_string(id)+",\"frame\":"+std::to_string(st.frame)+",\"before_saved\":"+(before?"true":"false")+",\"after_saved\":"+(after?"true":"false")+",\"restored\":"+(restored?"true":"false")+",\"stage\":\"before_weapon_and_HUD\"");}
    clear_replays();
}
bool bind_gbuffer(IDirect3DDevice9*d,bool clear,float wet,IDirect3DSurface9*game_depth){
    bool ok=SUCCEEDED(d->SetDepthStencilSurface(nullptr))&&SUCCEEDED(d->SetRenderTarget(0,target))&&SUCCEEDED(d->SetDepthStencilSurface(game_depth));
    // Never clear/write the game's depth. Original FFP geometry/rasterizer
    // supplies exact visibility, rather than a pixel-shader depth recompute.
    if(clear)ok &= SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
    float kind[4]={wet,0,0,0},p[4]={0,0,main_projection[10],main_projection[14]};
    ok &= SUCCEEDED(d->SetPixelShader(gbuffer_ps))&&SUCCEEDED(d->SetPixelShaderConstantF(0,kind,1))&&SUCCEEDED(d->SetPixelShaderConstantF(1,p,1))&&generated(d,0,D3DTSS_TCI_CAMERASPACEPOSITION);
    ok &= SUCCEEDED(d->SetRenderState(D3DRS_FOGENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE))&&
        SUCCEEDED(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_EQUAL))&&SUCCEEDED(d->SetRenderState(D3DRS_COLORWRITEENABLE,15))&&SUCCEEDED(d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));return ok;
}
void probe_pixels(IDirect3DDevice9*d){
    if(pixel_probe_done||!st.external_capture)return;pixel_probe_done=true;
    HRESULT capture_hr=E_FAIL;bool source_saved=false;capture(d,"surface-probe-original",capture_hr,source_saved);
    Ref<IDirect3DSurface9>cpu;D3DLOCKED_RECT rect{};bool locked=false;
    bool ok=SUCCEEDED(d->CreateOffscreenPlainSurface(width,height,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&cpu.p,nullptr))&&
        SUCCEEDED(d->GetRenderTargetData(target,cpu.p));
    if(ok){locked=SUCCEEDED(cpu.p->LockRect(&rect,nullptr,D3DLOCK_READONLY));ok=locked&&rect.pBits&&rect.Pitch>=int(width*16);}
    const unsigned points[][2]={{100,180},{160,260},{320,420},{800,260}};
    for(unsigned k=0;k<4;++k){unsigned x=points[k][0],y=points[k][1],id=0;
        if(ok&&x<width&&y<height){float tag=0;memcpy(&tag,(char*)rect.pBits+size_t(y)*rect.Pitch+x*16+12,4);if(std::isfinite(tag)&&tag>=1&&tag<=pixel_draws.size())id=unsigned(tag);}
        std::ostringstream s;s<<"\"event\":\"surface_pixel_draw\",\"frame\":"<<st.frame<<",\"pixel\":["<<x<<','<<y<<"],\"draw_id\":"<<id<<",\"source_saved\":"<<(source_saved?"true":"false");
        if(id){auto&r=pixel_draws[id-1];s<<",\"mip_file\":\""<<(r.material>=0?materials[r.material].name:"unknown")<<"\",\"selected_triangles\":"<<r.selected<<",\"fvf\":"<<r.fvf<<",\"stride\":"<<r.stride
            <<",\"stage0_tci\":"<<r.tci<<",\"stage0_transform\":"<<r.transform<<",\"draw\":["<<r.args.base<<','<<r.args.min<<','<<r.args.vertices<<','<<r.args.start<<']';
            bool dumped=false;unsigned w=0,h=0,level=0,format=0;std::string name="surface-pixel-"+std::to_string(k)+"-mip.dxt";
            if(r.texture&&r.texture->GetType()==D3DRTYPE_TEXTURE){auto*t=static_cast<IDirect3DTexture9*>(r.texture);level=std::min<UINT>(2,t->GetLevelCount()-1);D3DSURFACE_DESC desc{};
                if(SUCCEEDED(t->GetLevelDesc(level,&desc))&&desc.Pool==D3DPOOL_MANAGED&&!desc.Usage&&(desc.Format==D3DFMT_DXT1||desc.Format==D3DFMT_DXT5)&&desc.Width<=2048&&desc.Height<=2048){
                    w=desc.Width;h=desc.Height;format=unsigned(desc.Format);D3DLOCKED_RECT bits{};
                    if(SUCCEEDED(t->LockRect(level,&bits,nullptr,D3DLOCK_READONLY))){size_t pitch=size_t((w+3)/4)*(desc.Format==D3DFMT_DXT1?8:16),rows=(h+3)/4;
                        FILE*f=nullptr;if(bits.pBits&&bits.Pitch>=int(pitch)){auto path=directory+std::wstring(name.begin(),name.end());f=_wfopen(path.c_str(),L"wbx");}
                        dumped=f!=nullptr;if(f){for(size_t row=0;row<rows;++row)if(fwrite((char*)bits.pBits+row*bits.Pitch,1,pitch,f)!=pitch)dumped=false;if(fclose(f))dumped=false;}dumped=SUCCEEDED(t->UnlockRect(level))&&dumped;}}}
            s<<",\"gpu_mip_dump\":{\"saved\":"<<(dumped?"true":"false")<<",\"filename\":\""<<name<<"\",\"width\":"<<w<<",\"height\":"<<h<<",\"level\":"<<level<<",\"format\":"<<format<<'}';}
        s<<",\"mapping\":\"actual depth-equal opaque draw at this pixel; not a whole-object or stage-semantic proof\"";event(s.str());
    }
    if(locked)cpu.p->UnlockRect();
}
void compose(IDirect3DDevice9*d,const char*reason){if(!enabled||failed||!mode||frame!=st.frame||done==frame||!buffer)return;
    Ref<IDirect3DSurface9>current,bb;if(FAILED(d->GetRenderTarget(0,&current.p))||FAILED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb.p))||!same(current.p,bb.p))return;
    done=frame;Guard guard;if(mode==4||mode==5){render_ffp_reference(d);render_materials(d);}else{clear_reference_replays();clear_replays();}bool probe_now=mode==6&&!pixel_probe_done;if(mode==6)probe_pixels(d);State saved(d);if(!saved.capture()){++errors;return;}
    bool distinct_view=pair_views.empty();if(!pair_views.empty())for(unsigned i=0;i<12;++i)distinct_view |= std::abs(pair_views[0][i]-main_view[i])>.01f;
    bool pair=mode==4&&st.external_capture&&pair_views.size()<2&&distinct_view;
    unsigned pair_id=unsigned(pair_views.size())+1;HRESULT pre_hr=E_FAIL,post_hr=E_FAIL;bool pre_saved=false,post_saved=false;
    if(pair){pair_views.push_back(main_view);capture(d,"hybrid-pair-"+std::to_string(pair_id)+"-before",pre_hr,pre_saved);previous_valid=false;}
    Ticket*ticket=pair?nullptr:begin_gpu(d);LARGE_INTEGER cpu_begin{},cpu_end{};QueryPerformanceCounter(&cpu_begin);
    HRESULT copied=d->StretchRect(saved.rt.p,nullptr,color_target,nullptr,D3DTEXF_NONE);
    bool ok=SUCCEEDED(copied)&&SUCCEEDED(d->SetVertexShader(nullptr))&&SUCCEEDED(d->SetPixelShader(composite_ps))&&SUCCEEDED(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    float p[4]={main_projection[0],main_projection[5],main_projection[8],main_projection[9]},opt[4]={float(mode==7?4:mode),1.f/width,1.f/height,0};
    ok &= SUCCEEDED(d->SetPixelShaderConstantF(0,p,1))&&SUCCEEDED(d->SetPixelShaderConstantF(1,opt,1))&&SUCCEEDED(d->SetTexture(0,buffer))&&SUCCEEDED(d->SetTexture(1,color));
    for(unsigned i=0;i<2;++i){const DWORD values[]={D3DTADDRESS_CLAMP,D3DTADDRESS_CLAMP,D3DTEXF_POINT,D3DTEXF_POINT,D3DTEXF_NONE,FALSE,0,1};for(unsigned j=0;j<8;++j)ok &= SUCCEEDED(d->SetSamplerState(i,sampler_states[j],values[j]));}
    // Geometry buffer already uses the original game's exact visibility.
    // The fullscreen color merge must not reproject/requantize that depth.
    ok &= SUCCEEDED(d->SetRenderState(D3DRS_ZENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE))&&
        SUCCEEDED(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_STENCILENABLE,FALSE))&&
        SUCCEEDED(d->SetRenderState(D3DRS_FOGENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE))&&SUCCEEDED(d->SetRenderState(D3DRS_COLORWRITEENABLE,7))&&
        SUCCEEDED(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));
    struct Vertex{float x,y,z,w,u,v;};Vertex q[]={{-.5f,-.5f,0,1,0,0},{float(width)-.5f,-.5f,0,1,1,0},{-.5f,float(height)-.5f,0,1,0,1},{float(width)-.5f,float(height)-.5f,0,1,1,1}};
    HRESULT hr=ok?find(d)->dpu(d,D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex)):E_FAIL;
    bool restored=saved.restore();if(ok&&SUCCEEDED(hr)&&restored)++composites;else ++errors;
    if(probe_now){HRESULT mh=E_FAIL;bool ms=false;capture(d,"surface-probe-draw-mask",mh,ms);event("\"event\":\"surface_pixel_mask\",\"frame\":"+std::to_string(st.frame)+",\"saved\":"+(ms?"true":"false")+",\"draw_id_rgb\":\"frac(id * [.618,.381,.173] + [0,.23,.47])\"");}
    QueryPerformanceCounter(&cpu_end);end_gpu(ticket);
    if(measure&&mode==4&&!pair&&composite_cpu.size()<512)composite_cpu.push_back(1000.*double(cpu_end.QuadPart-cpu_begin.QuadPart)/double(clock_frequency.QuadPart));
    if(pair){capture(d,"hybrid-pair-"+std::to_string(pair_id)+"-after",post_hr,post_saved);
        event("\"event\":\"hybrid_same_frame_pair\",\"pair\":"+std::to_string(pair_id)+",\"frame\":"+std::to_string(st.frame)+",\"before_saved\":"+(pre_saved?"true":"false")+",\"after_saved\":"+(post_saved?"true":"false")+",\"restored\":"+(restored?"true":"false")+",\"stage\":\"before_weapon_and_HUD\"");}
    static unsigned reported=0;if(reported++<12)event("\"event\":\"hybrid_composite\",\"frame\":"+std::to_string(st.frame)+",\"mode\":"+std::to_string(mode)+",\"reason\":\""+reason+"\",\"hr\":"+std::to_string(hr)+",\"restored\":"+(restored?"true":"false"));
}
void before(IDirect3DDevice9*d){if(inside||!enabled||d!=st.device)return;DWORD fvf=0;D3DMATRIX p{};
    bool transformed=SUCCEEDED(d->GetFVF(&fvf))&&(fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZRHW;
    bool changed=have_view&&SUCCEEDED(d->GetTransform(D3DTS_PROJECTION,&p))&&matrix(p)!=main_projection;
    if(transformed||changed)compose(d,transformed?"before_transformed_overlay":"before_projection_change");}
template<class F>HRESULT draw(IDirect3DDevice9*d,D3DPRIMITIVETYPE type,const DrawArgs&a,F forward){
    if(inside)return forward();std::lock_guard<std::recursive_mutex>lock(mutex);before(d);auto hr=forward();
    if(!enabled||failed||!st.active||st.disabled||d!=st.device||FAILED(hr)||route(d)->recording||done==st.frame)return hr;
    Guard guard;
    try{bool receiver=false;Mat view{};if(!candidate(d,type,a.indexed,receiver,view)||st.alpha_test||st.opaque_blend)return hr;
        D3DMATRIX projection{};if(FAILED(d->GetTransform(D3DTS_PROJECTION,&projection)))return hr;auto p=matrix(projection);
        if(p[3]!=0||p[7]!=0||p[11]!=1||p[15]!=0||p[2]!=0||p[6]!=0||!std::isfinite(p[0])||!std::isfinite(p[5])||p[0]==0||p[5]==0)return hr;
        auto camera=shadow_core::transform({0,0,0,1},shadow_core::inverse_rigid(view));
        std::vector<visible_geometry::Run>runs;
        if(receiver&&visible_select_draw(d,a,camera[2],runs)){
            main_view=view;main_projection=p;for(unsigned i=0;i<3;++i)eye[i]=camera[i];have_view=true;
        }
        if(!have_view||p!=main_projection||!shadow_core::same_scene_eye(camera,eye))return hr;
        if(!resources(d))return hr;bool fresh=frame!=st.frame;
        if(fresh){clear_cache();clear_pixel_draws();clear_replays();clear_reference_replays();frame=st.frame;main_view=view;}
        DWORD draw_fvf=0;Ref<IDirect3DVertexBuffer9>mesh_vb;UINT mesh_offset=0,mesh_stride=0;
        bool static_layout=a.indexed&&SUCCEEDED(d->GetFVF(&draw_fvf))&&draw_fvf==338&&SUCCEEDED(d->GetStreamSource(0,&mesh_vb.p,&mesh_offset,&mesh_stride))&&mesh_stride==36;
        bool static_opaque=static_layout&&equal(d,D3DRS_ALPHABLENDENABLE,FALSE)&&equal(d,D3DRS_ZWRITEENABLE,TRUE)&&equal(d,D3DRS_ZFUNC,D3DCMP_LESSEQUAL)&&equal(d,D3DRS_ALPHATESTENABLE,FALSE)&&equal(d,D3DRS_COLORWRITEENABLE,15);
        int mat=(receiver||static_opaque)?material(d):-1;
        unsigned selected=0;
        if(mat>=0){std::vector<visible_geometry::Run>material_runs;
            if(receiver&&materials[mat].role=="wet_bsp")material_runs=runs;
            else if(receiver&&materials[mat].role=="wall_bsp"&&wall_ready)visible_select_draw(d,a,std::numeric_limits<float>::infinity(),material_runs,&wall_index);
            else if(static_opaque&&materials[mat].role=="wall_static")material_runs.push_back({0,a.primitives});
            for(auto&r:material_runs)selected+=r.count;
            record_bridge_inputs(d,mat,a,material_runs);
            queue_material(d,a,material_runs,view,mat);}
        float tag=0;if(mode==6&&pixel_draws.size()<512){PixelDraw info;info.material=mat;info.args=a;info.selected=selected;
            if(SUCCEEDED(d->GetTexture(0,&info.texture))){d->GetFVF(&info.fvf);d->GetTextureStageState(0,D3DTSS_TEXCOORDINDEX,&info.tci);d->GetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,&info.transform);
                Ref<IDirect3DVertexBuffer9>vb;UINT offset=0;d->GetStreamSource(0,&vb.p,&offset,&info.stride);pixel_draws.push_back(info);tag=float(pixel_draws.size());}}
        State saved(d);if(!saved.capture()){++errors;return hr;}
        bool ok=bind_gbuffer(d,fresh,0,saved.z.p);float label[4]={0,tag,0,0};ok=ok&&SUCCEEDED(d->SetPixelShaderConstantF(0,label,1));HRESULT replay=ok?forward():E_FAIL;
        // Only exact retained-road ranges AND exact known Diffuse bytes carry
        // material ID. Other geometry contributes depth/occlusion, never wet.
        if(ok&&SUCCEEDED(replay)&&receiver&&mat>=0&&materials[mat].role=="wet_bsp"){float k[4]={1,tag,0,0};ok=SUCCEEDED(d->SetPixelShaderConstantF(0,k,1));
            if(ok)for(auto&r:runs){replay=find(d)->dip(d,type,a.base,a.min,a.vertices,a.start+r.first*3,r.count);if(FAILED(replay))break;}if(ok&&SUCCEEDED(replay))++wet_draws;}
        if(ok&&SUCCEEDED(replay))++draws;else ++errors;saved.restore();
    }catch(...){failed=true;++errors;}return hr;
}
void command(unsigned command){if(!enabled||command<20||command>28)return;previous_valid=false;
    if(command==25){measure=true;QueryPerformanceFrequency(&clock_frequency);event("\"event\":\"hybrid_measurement_started\",\"frame\":"+std::to_string(st.frame));return;}
    mode=command==26?6:command==27?5:command==28?7:command==20?0:command-20;event("\"event\":\"hybrid_mode\",\"mode\":"+std::to_string(mode)+",\"frame\":"+std::to_string(st.frame));}
void finish(){poll_gpu();if(enabled){event("\"event\":\"material_finished\",\"queued_wall\":"+std::to_string(queued_wall)+",\"queued_road\":"+std::to_string(queued_road)+",\"rendered_wall\":"+std::to_string(rendered_wall)+",\"rendered_road\":"+std::to_string(rendered_road)+",\"fallback\":"+std::to_string(material_fallback)+",\"light_actor_identified\":false,\"normal_map\":false,\"stage1_lightmap_identified\":false,\"roughness_measured\":false");
    event("\"event\":\"hybrid_finished\",\"gbuffer_draws\":"+std::to_string(draws)+",\"verified_material_draws\":"+std::to_string(wet_draws)+",\"composites\":"+std::to_string(composites)+",\"errors\":"+std::to_string(errors)+",\"restore_failures\":"+std::to_string(restores_failed)+",\"depth_complete\":false,\"normal_source\":\"actual_camera_position_derivatives\",\"ai\":false");
    event("\"event\":\"hybrid_timing_summary\",\"off_present_interval_with_gbuffer\":"+metric(intervals_off)+",\"ssr_present_interval_with_gbuffer\":"+metric(intervals_ssr)+",\"composite_cpu_submission\":"+metric(composite_cpu)+",\"composite_gpu_copy_and_merge\":"+metric(composite_gpu)+",\"not_input_latency_or_full_mod_gpu_cost\":true");}}
} // namespace hybrid
