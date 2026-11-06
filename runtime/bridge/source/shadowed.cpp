// New private variant: reuse only the preserved loader/export seam.
#define Direct3DCreate9 LegacyDirect3DCreate9
#define Direct3DCreate9Ex LegacyDirect3DCreate9Ex
#include "observer.cpp"
#undef Direct3DCreate9
#undef Direct3DCreate9Ex
#include "frame_capture_core.h"
#include "shadow_core.h"
#include "write_capture_core.h"
#include "ue2_static_geometry.h"
#include "comparison_harness.h"
#include "bsp_scene.h"
#include "visible_geometry.h"
#include <d3dcompiler.h>
#include <array>

namespace shadowed {
namespace hybrid {void compose(IDirect3DDevice9*,const char*);void release();void before(IDirect3DDevice9*);void load();void finish();void command(unsigned);extern ULONG owned_refs;}
namespace boundary {void before_clear(IDirect3DDevice9*,DWORD,UINT);void after_clear(HRESULT);void pre_present(IDirect3DDevice9*);void post_present(HRESULT);void trace(const char*,HRESULT);}
using shadow_core::Mat;
visible_geometry::Index visible_index,wall_index;
bool wall_ready=false;
bool visible_ready=false, visible_enabled=false, visible_pair_attempted=false;
bool limited_caster=false;unsigned visible_pass=0;std::array<float,3>retained_light{};
uint64_t visible_considered=0, visible_unknown=0, visible_matched=0, visible_failed=0;
uint64_t visible_subset_calls=0,visible_subset_triangles=0;
template<class T> struct Ref{T*p=nullptr;~Ref(){if(p)p->Release();}};
thread_local bool inside=false;
struct Guard{bool before;Guard():before(inside){inside=true;}~Guard(){inside=before;}};
using Release=ULONG(STDMETHODCALLTYPE*)(IDirect3DDevice9*);
using Begin=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*);
using End=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DStateBlock9**);
using Clear=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD);
struct Route{void**vt=nullptr;Release release=nullptr;Begin begin=nullptr;End end=nullptr;Clear clear=nullptr;bool recording=false;};
std::array<Route,16> routes{};
Route*route(IDirect3DDevice9*d){for(auto&r:routes)if(r.vt==*(void***)d)return &r;return nullptr;}
Mat matrix(const D3DMATRIX&m){Mat a{};memcpy(a.data(),&m,sizeof(m));return a;}
D3DMATRIX native(const Mat&m){D3DMATRIX a{};memcpy(&a,m.data(),sizeof(a));return a;}
bool same(IUnknown*a,IUnknown*b){const IID iid={0,0,0,{0xc0,0,0,0,0,0,0,0x46}};Ref<IUnknown>x,y;
    return a&&b&&SUCCEEDED(a->QueryInterface(iid,(void**)&x.p))&&SUCCEEDED(b->QueryInterface(iid,(void**)&y.p))&&x.p==y.p;}
bool equal(IDirect3DDevice9*d,D3DRENDERSTATETYPE t,DWORD expected){DWORD x=0;return SUCCEEDED(d->GetRenderState(t,&x))&&x==expected;}
constexpr unsigned SIZE=512;
constexpr float FAR_Z=1600.f,NEAR_Z=5.f,SCALE=.577350269f;
struct Stats{
    IDirect3DDevice9*device=nullptr;bool active=false,disabled=false,placed=false,key_previous[3]{},frame_caster_failed=false;
    bool diagnostic_exclude_receiver_casters=false,diagnostic_only_receiver_casters=false,exclude_static_mesh_casters=false,specular_enabled=false,diagnostic_visibility=false,diagnostic_casters=false,diagnostic_position=false;
    unsigned mode=0,epoch=0;uint64_t frame=0,casters=0,receivers=0,shadow_receivers=0,failures=0,restore_failures=0;
    uint64_t rejected_layout=0,rejected_state=0,rejected_target=0,rejected_resource=0,receiver_only_exclusions=0;
    uint64_t complete_maps=0,map_releases=0,manual_captures=0,after90_draws=0,specular_draws=0;
    ULONGLONG started=0;LARGE_INTEGER frequency{},previous{};bool previous_capture=false;
    std::array<float,3> light{},scene_eye{};Mat light_view{},light_projection{},scene_projection{};
    unsigned alpha_mode=0;bool alpha_test=false,opaque_blend=false;
    struct Camera{Mat view{},projection{};unsigned count=0;};std::array<Camera,8> cameras{};
    comparison_harness::Harness harness;
    bool external_capture=false,external_finished=false,track_writes=false;unsigned last_request=0;ULONGLONG request_poll=0;
    bool pick_pending=false,pick_found=false,selected=false,selected_only=false,selected_mask=false;
    float pick_x=0,pick_y=0,pick_best=1;Mat picked_world{};
    uint64_t pick_considered=0,pick_ineligible=0,pick_lock_failures=0,pick_hits=0,pick_unindexed=0,pick_state=0,pick_unsafe=0,pick_unknown=0,pick_sampled=0;
    uint64_t independent_frame=UINT64_MAX,mask_frame=UINT64_MAX;
    bool independent_buffer_failed=false;
    unsigned picked_fvf=0,picked_vertices=0,picked_primitives=0;
    INT picked_base=0;UINT picked_min=0,picked_start=0;uint64_t mask_matches=0;
    std::vector<std::array<float,3>> picked_geometry,selected_geometry;
    unsigned pending_capture=0;uint64_t capture_frame=0,pick_started_frame=0;
    std::array<std::vector<double>,4> cpu,gpu;
} st;
bsp_scene::Scene bsp;bool bsp_enabled=false,bsp_have_view=false;
Mat bsp_view{},bsp_projection{};D3DVIEWPORT9 bsp_viewport{};uint64_t bsp_frame=UINT64_MAX,bsp_view_frame=UINT64_MAX;
IDirect3DVertexBuffer9*bsp_receiver_vb=nullptr;IDirect3DVertexShader9*bsp_receiver_vs=nullptr,*bsp_combined_vs=nullptr;ID3DBlob*bsp_receiver_code=nullptr,*bsp_combined_code=nullptr;
ULONG bsp_device_refs=0;int bsp_timing_bucket=-1;bool bsp_outline=false,bsp_probe_pending=false;DWORD bsp_mask_zfunc=D3DCMP_LESSEQUAL;
unsigned bsp_receiver_probe=0;
std::vector<uint32_t>bsp_tried_surfaces;
// Diagnostic identity only, valid for comparison inside the same frame. Do
// not retain an extra COM reference to the game's depth resource.
uintptr_t bsp_scene_depth_id=0;uint64_t bsp_scene_depth_frame=UINT64_MAX;
// Reuse the earlier write-time observer for the explicitly enabled comparison
// only. Game Lock/Unlock calls are forwarded once; unknown ranges are never
// filled from old frames or from buffer identities.
namespace write_capture {
shadow::Store store;
using Lock=HRESULT(STDMETHODCALLTYPE*)(void*,UINT,UINT,void**,DWORD);
using Unlock=HRESULT(STDMETHODCALLTYPE*)(void*);
using Release=ULONG(STDMETHODCALLTYPE*)(void*);
using QI=HRESULT(STDMETHODCALLTYPE*)(void*,REFIID,void**);
struct ResourceRoute{void**vt=nullptr;Lock lock=nullptr;Unlock unlock=nullptr;Release release=nullptr;QI qi=nullptr;bool complete=false;};
std::array<ResourceRoute,16> routes{};
ResourceRoute*route(void*p){auto v=*(void***)p;for(auto&r:routes)if(r.vt==v)return &r;return nullptr;}
struct Active{uintptr_t key=0;void*pointer=nullptr;};std::array<Active,shadow::slots> pointers{};
void put(uintptr_t k,void*p){for(auto&v:pointers)if(v.key==k){v.pointer=p;return;}for(auto&v:pointers)if(!v.key){v={k,p};return;}}
void*get(uintptr_t k){for(auto&v:pointers)if(v.key==k)return v.pointer;return nullptr;}
void erase(uintptr_t k){for(auto&v:pointers)if(v.key==k)v={};}
bool readable_range(const void*p,size_t n){uintptr_t a=(uintptr_t)p;if(!a||n>shadow::max_copy||a+n<a)return false;auto end=a+n;
    while(a<end){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery((void*)a,&m,sizeof(m))||m.State!=MEM_COMMIT||
      (m.Protect&(PAGE_GUARD|PAGE_NOACCESS))||!(m.Protect&(PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
      auto next=(uintptr_t)m.BaseAddress+m.RegionSize;if(next<=a)return false;a=next;}return true;}
HRESULT STDMETHODCALLTYPE lock(void*p,UINT offset,UINT length,void**out,DWORD flags){auto f=route(p)->lock;std::lock_guard<std::recursive_mutex>g(mutex);
    auto hr=f(p,offset,length,out,flags);try{if(!inside&&st.track_writes&&SUCCEEDED(hr)){store.begin((uintptr_t)p,offset,length,flags);put((uintptr_t)p,out?*out:nullptr);}}
    catch(...){try{store.invalidate((uintptr_t)p,true);}catch(...){}}return hr;}
HRESULT STDMETHODCALLTYPE unlock(void*p){auto f=route(p)->unlock;std::lock_guard<std::recursive_mutex>g(mutex);bool copied=false;
    try{auto*b=store.find((uintptr_t)p);auto q=get((uintptr_t)p);if(!inside&&st.track_writes&&b)copied=store.copy((uintptr_t)p,q,readable_range(q,b->length));}catch(...){}
    auto hr=f(p);try{if(!inside&&st.track_writes){store.finish((uintptr_t)p,copied,SUCCEEDED(hr));erase((uintptr_t)p);}}
    catch(...){try{store.invalidate((uintptr_t)p,true);}catch(...){}}return hr;}
ULONG STDMETHODCALLTYPE release(void*p){auto f=route(p)->release;uintptr_t key=(uintptr_t)p;std::lock_guard<std::recursive_mutex>g(mutex);
    auto n=f(p);try{if(!n){store.erase(key);erase(key);}}catch(...){}return n;}
HRESULT STDMETHODCALLTYPE qi(void*p,REFIID id,void**out){auto f=route(p)->qi;std::lock_guard<std::recursive_mutex>g(mutex);auto hr=f(p,id,out);
    try{if(SUCCEEDED(hr)&&out&&*out!=p){store.invalidate((uintptr_t)p,true);store.invalidate((uintptr_t)*out,true);}}catch(...){}return hr;}
void resource(void*p,IDirect3DDevice9*d,UINT bytes,bool shared){if(!p)return;auto*existing=route(p);bool ok=true;
    if(!existing){ResourceRoute*r=nullptr;for(auto&x:routes)if(!x.vt){r=&x;break;}if(!r){++store.capacity_misses;return;}r->vt=*(void***)p;
        ok &= patch(r->vt,0,(void*)qi,(void**)&r->qi);ok &= patch(r->vt,2,(void*)release,(void**)&r->release);
        ok &= patch(r->vt,11,(void*)lock,(void**)&r->lock);ok &= patch(r->vt,12,(void*)unlock,(void**)&r->unlock);r->complete=ok;existing=r;}
    erase((uintptr_t)p);store.create((uintptr_t)p,(uintptr_t)d,bytes,shared||!ok||!existing->complete);}
using CVB=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,DWORD,DWORD,D3DPOOL,IDirect3DVertexBuffer9**,HANDLE*);
using CIB=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DIndexBuffer9**,HANDLE*);
using PV=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,UINT,UINT,IDirect3DVertexBuffer9*,IDirect3DVertexDeclaration9*,DWORD);
struct DeviceRoute{void**vt=nullptr;CVB cvb=nullptr;CIB cib=nullptr;PV pv=nullptr;bool complete=false;};std::array<DeviceRoute,16> devices{};
DeviceRoute*device_route(IDirect3DDevice9*d){for(auto&r:devices)if(r.vt==*(void***)d)return &r;return nullptr;}
HRESULT STDMETHODCALLTYPE cvb(IDirect3DDevice9*d,UINT n,DWORD u,DWORD f,D3DPOOL pool,IDirect3DVertexBuffer9**out,HANDLE*shared){auto*r=device_route(d);std::lock_guard<std::recursive_mutex>g(mutex);
    auto hr=r->cvb(d,n,u,f,pool,out,shared);try{if(!inside&&st.track_writes&&SUCCEEDED(hr)&&out)resource(*out,d,n,shared!=nullptr||!r->complete);}catch(...){}return hr;}
HRESULT STDMETHODCALLTYPE cib(IDirect3DDevice9*d,UINT n,DWORD u,D3DFORMAT f,D3DPOOL pool,IDirect3DIndexBuffer9**out,HANDLE*shared){auto*r=device_route(d);std::lock_guard<std::recursive_mutex>g(mutex);
    auto hr=r->cib(d,n,u,f,pool,out,shared);try{if(!inside&&st.track_writes&&SUCCEEDED(hr)&&out)resource(*out,d,n,shared!=nullptr||!r->complete);}catch(...){}return hr;}
HRESULT STDMETHODCALLTYPE pv(IDirect3DDevice9*d,UINT src,UINT dest,UINT n,IDirect3DVertexBuffer9*vb,IDirect3DVertexDeclaration9*decl,DWORD flags){auto*r=device_route(d);std::lock_guard<std::recursive_mutex>g(mutex);
    try{if(!inside&&st.track_writes)store.invalidate((uintptr_t)vb);}catch(...){}return r->pv(d,src,dest,n,vb,decl,flags);}
bool hook(IDirect3DDevice9*d){if(device_route(d))return true;DeviceRoute*r=nullptr;for(auto&x:devices)if(!x.vt){r=&x;break;}if(!r)return false;
    r->vt=*(void***)d;bool ok=true;ok &= patch(r->vt,26,(void*)cvb,(void**)&r->cvb);ok &= patch(r->vt,27,(void*)cib,(void**)&r->cib);
    ok &= patch(r->vt,85,(void*)pv,(void**)&r->pv);r->complete=ok;return ok;}
}
shadow_core::Maps maps;
IDirect3DTexture9*textures[2]{};IDirect3DSurface9*surfaces[2]{};IDirect3DSurface9*depth=nullptr;
IDirect3DPixelShader9*caster_ps=nullptr,*receiver_ps=nullptr;
IDirect3DVertexShader9*independent_vs=nullptr;
IDirect3DVertexBuffer9*independent_vb=nullptr;IDirect3DIndexBuffer9*independent_ib=nullptr;
ID3DBlob*caster_code=nullptr,*receiver_code=nullptr,*independent_code=nullptr;bool compile_tried=false,compile_ok=false,format_failed=false;
ULONG map_device_refs=0,gpu_device_refs=0,independent_device_refs=0;
ULONG device_refs(IDirect3DDevice9*d){d->AddRef();return route(d)->release(d);}
const char*caster_source=R"(
float4 Range : register(c0);
sampler2D Base : register(s0);
float4 main(float2 uv:TEXCOORD0,float3 position:TEXCOORD1,float4 vertexColor:COLOR0):COLOR0 {
    float alpha=1;
    if(Range.y>2.5)alpha=vertexColor.a;
    else if(Range.y>1.5)alpha=tex2D(Base,uv).a*vertexColor.a;
    else if(Range.y>.5)alpha=tex2D(Base,uv).a;
    // Alpha-blended draws contribute only fully opaque texels. Glass and
    // fractional transparency are omitted, never turned into solid casters.
    if(Range.z>0)clip(alpha-Range.z);
    if(Range.w>0)return float4(0,1,1,alpha);
    float z=saturate(position.z*Range.x);return float4(z,z,z,alpha);
}
)";
const char*receiver_source=R"(
float4 Light : register(c0);
float4 ColorRadius : register(c1);
row_major float4x4 CameraToLight : register(c2);
float4 Projection : register(c6);
float4 BiasTexel : register(c7);
float4 FloorUpAndPass : register(c8);
sampler2D Shadow : register(s2);
float4 main(float3 position:TEXCOORD0,float3 normal:TEXCOORD1):COLOR0 {
    if(FloorUpAndPass.w>2.5&&FloorUpAndPass.w<3.5)return float4(0,1,1,1);
    float3 q=mul(float4(position,1),CameraToLight).xyz;
    if(q.z<=Projection.z||q.z>=Projection.w)return FloorUpAndPass.w>1.5?float4(1,0,1,1):0;
    float2 ndc=q.xy*Projection.xy/q.z;
    if(FloorUpAndPass.w>1.5 && max(abs(ndc.x),abs(ndc.y))>1)return float4(1,0,1,1);
    float spot=1-smoothstep(.85,1,length(ndc));
    float3 delta=Light.xyz-position;float d2=max(dot(delta,delta),.0001);
    float ndl=saturate(dot(normalize(normal),delta*rsqrt(d2)));
    float falloff=saturate(1-d2*ColorRadius.w);float visibility=1;float storedDepth=1;
    if(Light.w>0.5){
        float2 uv=float2(ndc.x*.5+.5,-ndc.y*.5+.5)+BiasTexel.yy*.5;
        float z=q.z/Projection.w;float bias=BiasTexel.x*(1+2*(1-ndl));visibility=0;
        storedDepth=tex2D(Shadow,uv).r;
        visibility+=(z<=tex2D(Shadow,uv+float2(-.5,-.5)*BiasTexel.y).r+bias)?.25:0;
        visibility+=(z<=tex2D(Shadow,uv+float2(.5,-.5)*BiasTexel.y).r+bias)?.25:0;
        visibility+=(z<=tex2D(Shadow,uv+float2(-.5,.5)*BiasTexel.y).r+bias)?.25:0;
        visibility+=(z<=tex2D(Shadow,uv+float2(.5,.5)*BiasTexel.y).r+bias)?.25:0;
    }
    float attenuation=falloff*falloff*spot*visibility;
    // Paired scalar diagnostic only on the visible game receiver: red is the
    // sampled caster depth, green is receiver depth, in the same /FAR units.
    if(FloorUpAndPass.w>3.5)return float4(storedDepth,q.z/Projection.w,0,1);
    // Explicit comparison-harness diagnostic on the game's visible receiver
    // only; this is not an image/readback of the shadow-map resource.
    if(FloorUpAndPass.w>1.5){float2 uv=float2(ndc.x*.5+.5,-ndc.y*.5+.5)+BiasTexel.yy*.5;
        float sampleDepth=tex2D(Shadow,uv).r;
        return sampleDepth>=.999?float4(0,0,1,1):float4(1-visibility,visibility,0,1);}
    if(FloorUpAndPass.w>0.5){
        // An artistic rough dielectric floor-like class, not recovered KF1
        // PBR data or a screen/scene reflection. The FFP normal decides the
        // horizontal-facing mask; keep the original diffuse/lightmap intact.
        float3 n=normalize(normal),v=normalize(-position),l=delta*rsqrt(d2);
        float3 h=normalize(v+l);float alpha=BiasTexel.z*BiasTexel.z;
        float exponent=min(64,max(1,2/(alpha*alpha)-2));
        float f=BiasTexel.w+(1-BiasTexel.w)*pow(1-saturate(dot(v,h)),5);
        float floorMask=smoothstep(.90,.98,dot(n,normalize(FloorUpAndPass.xyz)));
        float spec=2*f*pow(saturate(dot(n,h)),exponent)*ndl*floorMask;
        return float4(ColorRadius.xyz*spec*attenuation,0);
    }
    return float4(ColorRadius.xyz*ndl*attenuation,0);
}
)";
// Selected vertices have already been transformed to world space. Both the
// private caster and the receiver use the same light view and /FAR depth.
const char*independent_source=R"(
row_major float4x4 WorldToClip : register(c0);
row_major float4x4 WorldToLight : register(c4);
struct O { float4 position:POSITION; float2 uv:TEXCOORD0; float3 light:TEXCOORD1; float4 color:COLOR0; };
O main(float3 position:POSITION) { O o; float4 p=float4(position,1); o.position=mul(p,WorldToClip);
    o.light=mul(p,WorldToLight).xyz; o.uv=0; o.color=1; return o; }
)";
const char*bsp_receiver_source=R"(
row_major float4x4 View : register(c0);
row_major float4x4 Projection : register(c4);
struct O { float4 clip:POSITION; float3 position:TEXCOORD0; float3 normal:TEXCOORD1; };
O main(float3 position:POSITION,float3 normal:NORMAL) { O o; float4 p=mul(float4(position,1),View);
    o.clip=mul(p,Projection);o.position=p.xyz;o.normal=mul(float4(normal,0),View).xyz;return o; }
)";
// Same world positions and camera normals; only the floating-point order for
// clip coordinates differs. Used solely by an explicit comparison command.
const char*bsp_combined_source=R"(
row_major float4x4 View : register(c0);
row_major float4x4 ViewProjection : register(c4);
struct O { float4 clip:POSITION; float3 position:TEXCOORD0; float3 normal:TEXCOORD1; };
O main(float3 position:POSITION,float3 normal:NORMAL) { O o; float4 world=float4(position,1); float4 p=mul(world,View);
    o.clip=mul(world,ViewProjection);o.position=p.xyz;o.normal=mul(float4(normal,0),View).xyz;return o; }
)";
std::string text(const char*p){std::string s;if(!p)return s;for(unsigned i=0;p[i]&&i<1800;++i){unsigned char c=p[i];if(c=='"'||c=='\\'){s+='\\';s+=char(c);}else if(c=='\n')s+="\\n";else if(c>=32)s+=char(c);}return s;}
bool compile(){if(compile_tried)return compile_ok;compile_tried=true;
    using Compile=HRESULT(WINAPI*)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
    auto lib=LoadLibraryW(L"d3dcompiler_47.dll");auto fn=lib?(Compile)GetProcAddress(lib,"D3DCompile"):nullptr;Ref<ID3DBlob>a,b,c;HRESULT x=E_FAIL,y=E_FAIL,z=E_FAIL;
    if(fn){x=fn(caster_source,strlen(caster_source),"shadow_caster",nullptr,nullptr,"main","ps_3_0",0,0,&caster_code,&a.p);
        y=fn(receiver_source,strlen(receiver_source),"shadow_receiver",nullptr,nullptr,"main","ps_3_0",0,0,&receiver_code,&b.p);
        if(st.track_writes||bsp_enabled)z=fn(independent_source,strlen(independent_source),"independent_caster",nullptr,nullptr,"main","vs_3_0",0,0,&independent_code,&c.p);
        if(bsp_enabled){Ref<ID3DBlob>error,combined_error;auto h=fn(bsp_receiver_source,strlen(bsp_receiver_source),"bsp_receiver",nullptr,nullptr,"main","vs_3_0",0,0,&bsp_receiver_code,&error.p);
            HRESULT hc=S_OK;if(st.external_capture)hc=fn(bsp_combined_source,strlen(bsp_combined_source),"bsp_combined",nullptr,nullptr,"main","vs_3_0",0,0,&bsp_combined_code,&combined_error.p);
            event("\"event\":\"bsp_receiver_compile\",\"hr\":"+std::to_string(h)+",\"combined_hr\":"+std::to_string(hc)+",\"error\":\""+text(error.p?(char*)error.p->GetBufferPointer():nullptr)+"\",\"combined_error\":\""+text(combined_error.p?(char*)combined_error.p->GetBufferPointer():nullptr)+"\"");if(FAILED(h))z=h;else if(FAILED(hc))z=hc;}}
    compile_ok=SUCCEEDED(x)&&SUCCEEDED(y)&&caster_code&&receiver_code&&(!(st.track_writes||bsp_enabled)||(SUCCEEDED(z)&&independent_code))&&(!bsp_enabled||(bsp_receiver_code&&(!st.external_capture||bsp_combined_code)));
    event("\"event\":\"shadow_shader_compile\",\"caster_hr\":"+std::to_string(x)+",\"receiver_hr\":"+std::to_string(y)+",\"independent_requested\":"+(st.track_writes?"true":"false")+",\"independent_hr\":"+std::to_string(z)+",\"caster_error\":\""+text(a.p?(char*)a.p->GetBufferPointer():nullptr)+"\",\"receiver_error\":\""+text(b.p?(char*)b.p->GetBufferPointer():nullptr)+"\",\"independent_error\":\""+text(c.p?(char*)c.p->GetBufferPointer():nullptr)+"\"");return compile_ok;
}
void release_independent(){if(independent_vb){independent_vb->Release();independent_vb=nullptr;}
    if(independent_ib){independent_ib->Release();independent_ib=nullptr;}independent_device_refs=0;}
void release_resources(){Guard g;
    hybrid::release();
    if(bsp_receiver_vb){bsp_receiver_vb->Release();bsp_receiver_vb=nullptr;}bsp_device_refs=0;
    if(bsp_receiver_vs){bsp_receiver_vs->Release();bsp_receiver_vs=nullptr;}
    if(bsp_combined_vs){bsp_combined_vs->Release();bsp_combined_vs=nullptr;}
    if(caster_ps){auto*p=caster_ps;caster_ps=nullptr;p->Release();}if(receiver_ps){auto*p=receiver_ps;receiver_ps=nullptr;p->Release();}
    if(independent_vs){independent_vs->Release();independent_vs=nullptr;}release_independent();
    for(unsigned i=0;i<2;++i){if(surfaces[i]){auto*p=surfaces[i];surfaces[i]=nullptr;p->Release();}if(textures[i]){auto*p=textures[i];textures[i]=nullptr;p->Release();}}
    if(depth){auto*p=depth;depth=nullptr;p->Release();}map_device_refs=0;maps.invalidate();++st.map_releases;
}
bool resources(IDirect3DDevice9*d){if(textures[0]&&textures[1]&&depth&&caster_ps&&receiver_ps)return true;
    if(format_failed||!compile())return false;Guard guard;auto refs_before=device_refs(d);Ref<IDirect3D9>factory;D3DDEVICE_CREATION_PARAMETERS cp{};D3DDISPLAYMODE display{};
    HRESULT ch=E_FAIL,dh=E_FAIL;D3DFORMAT df=D3DFMT_UNKNOWN;
    if(SUCCEEDED(d->GetDirect3D(&factory.p))&&SUCCEEDED(d->GetCreationParameters(&cp))&&SUCCEEDED(factory.p->GetAdapterDisplayMode(cp.AdapterOrdinal,&display))){
        ch=factory.p->CheckDeviceFormat(cp.AdapterOrdinal,cp.DeviceType,display.Format,D3DUSAGE_RENDERTARGET,D3DRTYPE_TEXTURE,D3DFMT_R32F);
        for(auto f:{D3DFMT_D24X8,D3DFMT_D24S8,D3DFMT_D16}){
            dh=factory.p->CheckDeviceFormat(cp.AdapterOrdinal,cp.DeviceType,display.Format,D3DUSAGE_DEPTHSTENCIL,D3DRTYPE_SURFACE,f);
            if(SUCCEEDED(dh))dh=factory.p->CheckDepthStencilMatch(cp.AdapterOrdinal,cp.DeviceType,display.Format,D3DFMT_R32F,f);
            if(SUCCEEDED(dh)){df=f;break;}}
    }
    bool ok=SUCCEEDED(ch)&&SUCCEEDED(dh)&&df!=D3DFMT_UNKNOWN;
    if(ok)for(unsigned i=0;i<2;++i){ok &= SUCCEEDED(d->CreateTexture(SIZE,SIZE,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&textures[i],nullptr));
        if(textures[i])ok &= SUCCEEDED(textures[i]->GetSurfaceLevel(0,&surfaces[i]));else ok=false;}
    if(ok)ok &= SUCCEEDED(d->CreateDepthStencilSurface(SIZE,SIZE,df,D3DMULTISAMPLE_NONE,0,TRUE,&depth,nullptr));
    if(ok)ok &= SUCCEEDED(d->CreatePixelShader((DWORD*)caster_code->GetBufferPointer(),&caster_ps))&&SUCCEEDED(d->CreatePixelShader((DWORD*)receiver_code->GetBufferPointer(),&receiver_ps));
    if(ok&&(st.track_writes||bsp_enabled))ok &= SUCCEEDED(d->CreateVertexShader((DWORD*)independent_code->GetBufferPointer(),&independent_vs));
    if(ok&&bsp_enabled)ok &= SUCCEEDED(d->CreateVertexShader((DWORD*)bsp_receiver_code->GetBufferPointer(),&bsp_receiver_vs));
    if(ok&&bsp_enabled&&st.external_capture)ok &= SUCCEEDED(d->CreateVertexShader((DWORD*)bsp_combined_code->GetBufferPointer(),&bsp_combined_vs));
    auto refs_after=device_refs(d);map_device_refs=refs_after>=refs_before?refs_after-refs_before:0;
    if(refs_after<refs_before)ok=false;
    std::ostringstream s;s<<"\"event\":\"shadow_resources\",\"owned_device_reference_delta\":"<<map_device_refs<<",\"color_check_hr\":"<<ch<<",\"depth_check_hr\":"<<dh<<",\"color_format\":"<<D3DFMT_R32F<<",\"depth_format\":"<<df
        <<",\"size\":"<<SIZE<<",\"color_textures\":2,\"estimated_owned_gpu_payload_bytes\":"<<(SIZE*SIZE*(8+(df==D3DFMT_D16?2:4)))<<",\"ok\":"<<(ok?"true":"false");event(s.str());
    if(!ok){release_resources();format_failed=true;}return ok;
}

constexpr D3DRENDERSTATETYPE changed[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,
    D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_COLORWRITEENABLE,D3DRS_FOGENABLE,D3DRS_LIGHTING,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_CULLMODE,
    D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_DEPTHBIAS,D3DRS_SLOPESCALEDEPTHBIAS,D3DRS_SRGBWRITEENABLE,D3DRS_CLIPPING};
constexpr D3DSAMPLERSTATETYPE sampler_states[]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE,D3DSAMP_MAXMIPLEVEL,D3DSAMP_MAXANISOTROPY};
struct Snapshot{
    IDirect3DDevice9*d;Ref<IDirect3DSurface9>rt,z;Ref<IDirect3DVertexShader9>vs;Ref<IDirect3DPixelShader9>ps;Ref<IDirect3DBaseTexture9>texture;
    D3DVIEWPORT9 vp{};D3DMATRIX view{},proj{},tex[2]{};DWORD rs[sizeof(changed)/sizeof(changed[0])]{},ss[8]{},tci[2]{},ttf[2]{};float pc[36]{};
    explicit Snapshot(IDirect3DDevice9*x):d(x){}
    bool capture(){bool ok=SUCCEEDED(d->GetRenderTarget(0,&rt.p))&&SUCCEEDED(d->GetDepthStencilSurface(&z.p))&&SUCCEEDED(d->GetViewport(&vp));
        D3DCAPS9 caps{};ok &= SUCCEEDED(d->GetDeviceCaps(&caps));
        for(unsigned i=1;i<std::min<DWORD>(4,caps.NumSimultaneousRTs);++i){Ref<IDirect3DSurface9>other;auto hr=d->GetRenderTarget(i,&other.p);if((FAILED(hr)&&hr!=D3DERR_NOTFOUND)||other.p)ok=false;}
        ok &= SUCCEEDED(d->GetVertexShader(&vs.p))&&SUCCEEDED(d->GetPixelShader(&ps.p))&&SUCCEEDED(d->GetTexture(2,&texture.p))&&SUCCEEDED(d->GetPixelShaderConstantF(0,pc,9));
        ok &= SUCCEEDED(d->GetTransform(D3DTS_VIEW,&view))&&SUCCEEDED(d->GetTransform(D3DTS_PROJECTION,&proj));
        for(unsigned i=0;i<sizeof(changed)/sizeof(changed[0]);++i)ok &= SUCCEEDED(d->GetRenderState(changed[i],&rs[i]));
        for(unsigned i=0;i<8;++i)ok &= SUCCEEDED(d->GetSamplerState(2,sampler_states[i],&ss[i]));
        for(unsigned i=0;i<2;++i)ok &= SUCCEEDED(d->GetTextureStageState(i,D3DTSS_TEXCOORDINDEX,&tci[i]))&&SUCCEEDED(d->GetTextureStageState(i,D3DTSS_TEXTURETRANSFORMFLAGS,&ttf[i]))&&SUCCEEDED(d->GetTransform(D3DTRANSFORMSTATETYPE(D3DTS_TEXTURE0+i),&tex[i]));return ok;
    }
    bool restore(){bool ok=true;
        ok &= SUCCEEDED(d->SetTexture(2,nullptr));ok &= SUCCEEDED(d->SetDepthStencilSurface(nullptr));ok &= SUCCEEDED(d->SetRenderTarget(0,rt.p));ok &= SUCCEEDED(d->SetDepthStencilSurface(z.p));ok &= SUCCEEDED(d->SetViewport(&vp));
        ok &= SUCCEEDED(d->SetTransform(D3DTS_VIEW,&view))&&SUCCEEDED(d->SetTransform(D3DTS_PROJECTION,&proj));
        ok &= SUCCEEDED(d->SetVertexShader(vs.p))&&SUCCEEDED(d->SetPixelShader(ps.p))&&SUCCEEDED(d->SetPixelShaderConstantF(0,pc,9));
        for(unsigned i=0;i<sizeof(changed)/sizeof(changed[0]);++i)ok &= SUCCEEDED(d->SetRenderState(changed[i],rs[i]));
        for(unsigned i=0;i<8;++i)ok &= SUCCEEDED(d->SetSamplerState(2,sampler_states[i],ss[i]));
        for(unsigned i=0;i<2;++i)ok &= SUCCEEDED(d->SetTextureStageState(i,D3DTSS_TEXCOORDINDEX,tci[i]))&&SUCCEEDED(d->SetTextureStageState(i,D3DTSS_TEXTURETRANSFORMFLAGS,ttf[i]))&&SUCCEEDED(d->SetTransform(D3DTRANSFORMSTATETYPE(D3DTS_TEXTURE0+i),&tex[i]));
        ok &= SUCCEEDED(d->SetTexture(2,texture.p));
        Ref<IDirect3DSurface9>r2,z2;Ref<IDirect3DVertexShader9>v2;Ref<IDirect3DPixelShader9>p2;Ref<IDirect3DBaseTexture9>t2;D3DVIEWPORT9 vp2{};float pc2[36]{};D3DMATRIX a{},b{};
        ok &= SUCCEEDED(d->GetRenderTarget(0,&r2.p))&&same(r2.p,rt.p)&&SUCCEEDED(d->GetDepthStencilSurface(&z2.p))&&same(z2.p,z.p)&&SUCCEEDED(d->GetViewport(&vp2))&&!memcmp(&vp,&vp2,sizeof(vp));
        ok &= SUCCEEDED(d->GetVertexShader(&v2.p))&&v2.p==vs.p&&SUCCEEDED(d->GetPixelShader(&p2.p))&&p2.p==ps.p&&SUCCEEDED(d->GetTexture(2,&t2.p))&&t2.p==texture.p;
        ok &= SUCCEEDED(d->GetPixelShaderConstantF(0,pc2,9))&&!memcmp(pc,pc2,sizeof(pc));
        ok &= SUCCEEDED(d->GetTransform(D3DTS_VIEW,&a))&&!memcmp(&a,&view,sizeof(a))&&SUCCEEDED(d->GetTransform(D3DTS_PROJECTION,&b))&&!memcmp(&b,&proj,sizeof(b));
        for(unsigned i=0;i<sizeof(changed)/sizeof(changed[0]);++i){DWORD x=0;ok &= SUCCEEDED(d->GetRenderState(changed[i],&x))&&x==rs[i];}
        for(unsigned i=0;i<8;++i){DWORD x=0;ok &= SUCCEEDED(d->GetSamplerState(2,sampler_states[i],&x))&&x==ss[i];}
        for(unsigned i=0;i<2;++i){DWORD x=0,y=0;D3DMATRIX m{};ok &= SUCCEEDED(d->GetTextureStageState(i,D3DTSS_TEXCOORDINDEX,&x))&&x==tci[i]&&SUCCEEDED(d->GetTextureStageState(i,D3DTSS_TEXTURETRANSFORMFLAGS,&y))&&y==ttf[i]&&SUCCEEDED(d->GetTransform(D3DTRANSFORMSTATETYPE(D3DTS_TEXTURE0+i),&m))&&!memcmp(&m,&tex[i],sizeof(m));}return ok;
    }
};
bool generated(IDirect3DDevice9*d,unsigned stage,DWORD kind){auto id=native(shadow_core::identity());return SUCCEEDED(d->SetTextureStageState(stage,D3DTSS_TEXCOORDINDEX,kind))&&SUCCEEDED(d->SetTextureStageState(stage,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_COUNT3))&&SUCCEEDED(d->SetTransform(D3DTRANSFORMSTATETYPE(D3DTS_TEXTURE0+stage),&id));}
bool render_states(IDirect3DDevice9*d,const DWORD*values){bool ok=true;for(unsigned i=0;i<sizeof(changed)/sizeof(changed[0]);++i)ok &= SUCCEEDED(d->SetRenderState(changed[i],values[i]));return ok;}
bool caster_setup(IDirect3DDevice9*d){auto view=native(st.light_view),proj=native(st.light_projection);D3DVIEWPORT9 vp{0,0,SIZE,SIZE,0,1};float range[4]={1/FAR_Z,float(st.alpha_mode),st.opaque_blend?.9999f:0.f,0};
    bool ok=SUCCEEDED(d->SetTexture(2,nullptr));ok &= SUCCEEDED(d->SetDepthStencilSurface(nullptr));ok &= SUCCEEDED(d->SetRenderTarget(0,surfaces[maps.write]));ok &= SUCCEEDED(d->SetDepthStencilSurface(depth));ok &= SUCCEEDED(d->SetViewport(&vp));
    ok &= SUCCEEDED(d->SetTransform(D3DTS_VIEW,&view))&&SUCCEEDED(d->SetTransform(D3DTS_PROJECTION,&proj));
    ok &= SUCCEEDED(d->SetVertexShader(nullptr))&&SUCCEEDED(d->SetPixelShader(caster_ps))&&SUCCEEDED(d->SetPixelShaderConstantF(0,range,1))&&generated(d,1,D3DTSS_TCI_CAMERASPACEPOSITION);
    const DWORD values[]={TRUE,TRUE,D3DCMP_LESSEQUAL,FALSE,D3DBLEND_ONE,D3DBLEND_ZERO,D3DBLENDOP_ADD,FALSE,15,FALSE,FALSE,DWORD(st.alpha_test),FALSE,D3DCULL_NONE,FALSE,0,0,0,FALSE,TRUE};
    ok &= render_states(d,values);
    if(ok&&!maps.writing){maps.begin(st.frame);ok &= SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0));if(!ok)maps.failed=true;}
    return ok;
}
bool receiver_setup(IDirect3DDevice9*d,const Mat&view,bool specular,bool diagnostic=false){auto inv=shadow_core::inverse_rigid(view),to_light=shadow_core::multiply(inv,st.light_view);
    auto p=shadow_core::transform({st.light[0],st.light[1],st.light[2],1},view);float constants[36]{};memcpy(constants,p.data(),16);
    constants[3]=st.mode==2&&maps.usable()?1.f:0.f;constants[4]=.80f;constants[5]=.55f;constants[6]=.32f;constants[7]=1/(FAR_Z*FAR_Z);
    memcpy(constants+8,to_light.data(),64);constants[24]=SCALE;constants[25]=SCALE;constants[26]=NEAR_Z;constants[27]=FAR_Z;constants[28]=1.2f/FAR_Z;constants[29]=1.f/SIZE;
    constants[30]=.55f;constants[31]=.04f;auto up=shadow_core::transform({0,0,1,0},view);memcpy(constants+32,up.data(),16);constants[35]=st.diagnostic_position?4.f:st.diagnostic_casters?3.f:diagnostic?2.f:specular?1.f:0.f;
    bool ok=SUCCEEDED(d->SetVertexShader(nullptr))&&SUCCEEDED(d->SetPixelShader(receiver_ps))&&SUCCEEDED(d->SetPixelShaderConstantF(0,constants,9));
    ok &= generated(d,0,D3DTSS_TCI_CAMERASPACEPOSITION)&&generated(d,1,D3DTSS_TCI_CAMERASPACENORMAL);
    ok &= SUCCEEDED(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_EQUAL))&&SUCCEEDED(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));
    diagnostic=diagnostic||st.diagnostic_casters||st.diagnostic_position;
    ok &= SUCCEEDED(d->SetRenderState(D3DRS_SRCBLEND,(specular||diagnostic)?D3DBLEND_ONE:D3DBLEND_DESTCOLOR))&&SUCCEEDED(d->SetRenderState(D3DRS_DESTBLEND,diagnostic?D3DBLEND_ZERO:D3DBLEND_ONE))&&SUCCEEDED(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
    ok &= SUCCEEDED(d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_COLORWRITEENABLE,7))&&SUCCEEDED(d->SetRenderState(D3DRS_FOGENABLE,FALSE));
    ok &= SUCCEEDED(d->SetTexture(2,maps.usable()?textures[maps.read]:nullptr));const DWORD sampler[]={D3DTADDRESS_CLAMP,D3DTADDRESS_CLAMP,D3DTEXF_POINT,D3DTEXF_POINT,D3DTEXF_NONE,FALSE,0,1};
    for(unsigned i=0;i<8;++i)ok &= SUCCEEDED(d->SetSamplerState(2,sampler_states[i],sampler[i]));return ok;
}
bool caster_coverage_setup(IDirect3DDevice9*d){float range[4]={0,float(st.alpha_mode),st.opaque_blend?.9999f:0.f,1};
    bool ok=SUCCEEDED(d->SetPixelShader(caster_ps))&&SUCCEEDED(d->SetPixelShaderConstantF(0,range,1))&&generated(d,1,D3DTSS_TCI_CAMERASPACEPOSITION);
    ok &= SUCCEEDED(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_EQUAL));
    ok &= SUCCEEDED(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_COLORWRITEENABLE,7));return ok;
}

// Alpha-test coverage is reproduced only for an understood FFP alpha chain.
// Unsupported blend/cutout combinations remain original-only, not solid casters.
bool alpha_coverage(IDirect3DDevice9*d,DWORD fvf){
    st.alpha_mode=0;st.alpha_test=!equal(d,D3DRS_ALPHATESTENABLE,FALSE);
    if(!st.alpha_test&&!st.opaque_blend)return true;
    DWORD op=0,a=0,b=0,next=0,func=0;
    if(FAILED(d->GetRenderState(D3DRS_ALPHAFUNC,&func))||func<D3DCMP_NEVER||func>D3DCMP_ALWAYS)return false;
    if(func==D3DCMP_ALWAYS){st.alpha_test=false;if(!st.opaque_blend)return true;}
    if(FAILED(d->GetTextureStageState(0,D3DTSS_ALPHAOP,&op))||FAILED(d->GetTextureStageState(0,D3DTSS_ALPHAARG1,&a))||
       FAILED(d->GetTextureStageState(0,D3DTSS_ALPHAARG2,&b))||FAILED(d->GetTextureStageState(1,D3DTSS_ALPHAOP,&next)))return false;
    if(next!=D3DTOP_DISABLE){DWORD current=0;
        if(next!=D3DTOP_SELECTARG1||FAILED(d->GetTextureStageState(1,D3DTSS_ALPHAARG1,&current))||current!=D3DTA_CURRENT)return false;
        for(unsigned i=2;i<8;++i){DWORD later=0;if(FAILED(d->GetTextureStageState(i,D3DTSS_ALPHAOP,&later)))return false;if(later==D3DTOP_DISABLE)break;
            if(later!=D3DTOP_SELECTARG1||FAILED(d->GetTextureStageState(i,D3DTSS_ALPHAARG1,&current))||current!=D3DTA_CURRENT)return false;}}
    auto arg=op==D3DTOP_SELECTARG2?b:a;
    if((op==D3DTOP_SELECTARG1||op==D3DTOP_SELECTARG2)&&arg==D3DTA_DIFFUSE&&(fvf&D3DFVF_DIFFUSE)){st.alpha_mode=3;return true;}
    Ref<IDirect3DBaseTexture9>texture;DWORD tci=0,flags=0;
    if(!(fvf&D3DFVF_TEXCOUNT_MASK)||FAILED(d->GetTexture(0,&texture.p))||!texture.p||texture.p->GetType()!=D3DRTYPE_TEXTURE||
       FAILED(d->GetTextureStageState(0,D3DTSS_TEXCOORDINDEX,&tci))||tci!=0||
       FAILED(d->GetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,&flags))||(flags&D3DTTFF_PROJECTED))return false;
    if((op==D3DTOP_SELECTARG1||op==D3DTOP_SELECTARG2)&&arg==D3DTA_TEXTURE){st.alpha_mode=1;return true;}
    if(op==D3DTOP_MODULATE&&(fvf&D3DFVF_DIFFUSE)&&((a==D3DTA_TEXTURE&&b==D3DTA_DIFFUSE)||(b==D3DTA_TEXTURE&&a==D3DTA_DIFFUSE))){st.alpha_mode=2;return true;}
    return false;
}
void rejected_state_sample(IDirect3DDevice9*d,DWORD fvf){
    static std::vector<std::string>seen;if((!st.harness.enabled&&!(st.external_capture&&visible_enabled))||seen.size()>=32)return;
    std::ostringstream s;s<<"\"fvf\":"<<fvf<<",\"states\":[";
    const D3DRENDERSTATETYPE kinds[]={D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAFUNC,D3DRS_ALPHAREF};
    for(unsigned i=0;i<8;++i){DWORD x=0;d->GetRenderState(kinds[i],&x);if(i)s<<',';s<<x;}s<<"],\"alpha_stages\":[";
    for(unsigned j=0;j<3;++j)for(auto kind:{D3DTSS_ALPHAOP,D3DTSS_ALPHAARG1,D3DTSS_ALPHAARG2}){DWORD x=0;d->GetTextureStageState(j,kind,&x);if(j||kind!=D3DTSS_ALPHAOP)s<<',';s<<x;}s<<']';
    if(std::find(seen.begin(),seen.end(),s.str())==seen.end()){seen.push_back(s.str());event("\"event\":\"excluded_caster_state\","+s.str());}
}
// Test-only, bounded distinct state samples; does not snapshot or retain a
// dynamic vertex/index buffer and is not a complete submitted-scene census.
void accepted_state_sample(IDirect3DDevice9*d,DWORD fvf,UINT stride,bool receiver){
    static std::vector<std::string>seen;if((!st.harness.enabled&&!(st.external_capture&&visible_enabled))||seen.size()>=32)return;
    DWORD write=0,zfunc=0,blend=0;d->GetRenderState(D3DRS_ZWRITEENABLE,&write);
    d->GetRenderState(D3DRS_ZFUNC,&zfunc);d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend);
    std::ostringstream s;s<<"\"fvf\":"<<fvf<<",\"stride\":"<<stride<<",\"zwrite\":"<<write
        <<",\"zfunc\":"<<zfunc<<",\"blend\":"<<blend<<",\"receiver\":"<<(receiver?"true":"false");
    if(std::find(seen.begin(),seen.end(),s.str())==seen.end()){seen.push_back(s.str());event("\"event\":\"accepted_caster_state\","+s.str());}
}
bool candidate(IDirect3DDevice9*d,D3DPRIMITIVETYPE type,bool indexed,bool&receiver,Mat&view){
    receiver=false;if(type!=D3DPT_TRIANGLELIST){++st.rejected_layout;return false;}
    Ref<IDirect3DVertexShader9>vs;Ref<IDirect3DPixelShader9>ps;DWORD fvf=0;
    if(FAILED(d->GetVertexShader(&vs.p))||vs.p||FAILED(d->GetPixelShader(&ps.p))||ps.p||FAILED(d->GetFVF(&fvf))){++st.rejected_layout;return false;}
    const bool blend=!equal(d,D3DRS_ALPHABLENDENABLE,FALSE),write=equal(d,D3DRS_ZWRITEENABLE,TRUE);
    const bool receiver_state=!blend&&write&&equal(d,D3DRS_ZFUNC,D3DCMP_LESSEQUAL)&&equal(d,D3DRS_ALPHATESTENABLE,FALSE)&&equal(d,D3DRS_COLORWRITEENABLE,15);
    st.opaque_blend=blend&&write&&equal(d,D3DRS_SRCBLEND,D3DBLEND_SRCALPHA)&&equal(d,D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA)&&equal(d,D3DRS_BLENDOP,D3DBLENDOP_ADD);
    if(!equal(d,D3DRS_ZENABLE,TRUE)||(blend&&!st.opaque_blend)||!equal(d,D3DRS_STENCILENABLE,FALSE)||!equal(d,D3DRS_VERTEXBLEND,D3DVBF_DISABLE)||!equal(d,D3DRS_INDEXEDVERTEXBLENDENABLE,FALSE)||!alpha_coverage(d,fvf)){rejected_state_sample(d,fvf);++st.rejected_state;return false;}
    Ref<IDirect3DSurface9>rt,bb,z;D3DVIEWPORT9 vp{};D3DSURFACE_DESC desc{};
    if(FAILED(d->GetRenderTarget(0,&rt.p))||FAILED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb.p))||!same(rt.p,bb.p)||FAILED(d->GetDepthStencilSurface(&z.p))||!z.p||FAILED(bb.p->GetDesc(&desc))||FAILED(d->GetViewport(&vp))||vp.X||vp.Y||vp.Width!=desc.Width||vp.Height!=desc.Height||vp.MinZ!=0||vp.MaxZ!=1){++st.rejected_target;return false;}
    Ref<IDirect3DVertexDeclaration9>decl;D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1]{};UINT count=MAXD3DDECLLENGTH+1;bool position=false;
    if(FAILED(d->GetVertexDeclaration(&decl.p))||!decl.p||FAILED(decl.p->GetDeclaration(elements,&count))||count>MAXD3DDECLLENGTH+1){++st.rejected_layout;return false;}
    for(unsigned i=0;i<count&&elements[i].Stream!=0xff;++i){auto&e=elements[i];
        if(e.Usage==D3DDECLUSAGE_POSITIONT||e.Usage==D3DDECLUSAGE_BLENDWEIGHT||e.Usage==D3DDECLUSAGE_BLENDINDICES){++st.rejected_layout;return false;}
        if(e.Usage==D3DDECLUSAGE_POSITION&&e.UsageIndex==0&&e.Stream==0&&e.Type==D3DDECLTYPE_FLOAT3&&e.Offset==0)position=true;}
    if(!position||((fvf&D3DFVF_POSITION_MASK)!=D3DFVF_XYZ)){++st.rejected_layout;return false;}
    Ref<IDirect3DVertexBuffer9>vb;Ref<IDirect3DIndexBuffer9>ib;UINT offset=0,stride=0,freq=0;
    if(FAILED(d->GetStreamSource(0,&vb.p,&offset,&stride))||!vb.p||stride<12||FAILED(d->GetStreamSourceFreq(0,&freq))||freq!=1||(indexed&&(FAILED(d->GetIndices(&ib.p))||!ib.p))){++st.rejected_resource;return false;}
    D3DMATRIX w{},v{},p{};if(FAILED(d->GetTransform(D3DTS_WORLD,&w))||FAILED(d->GetTransform(D3DTS_VIEW,&v))||FAILED(d->GetTransform(D3DTS_PROJECTION,&p))){++st.rejected_resource;return false;}view=matrix(v);
    if(st.placed&&matrix(p)!=st.scene_projection){++st.rejected_target;return false;}
    if(!shadow_core::affine(matrix(w))){++st.rejected_resource;return false;}
    if(!shadow_core::rigid(view)){++st.rejected_target;return false;}
    auto eye=shadow_core::transform({0,0,0,1},shadow_core::inverse_rigid(view));
    if(st.placed&&!shadow_core::same_scene_eye(eye,st.scene_eye)){++st.rejected_target;return false;}
    auto id=shadow_core::identity();receiver=receiver_state&&fvf==530&&stride==40&&matrix(w)==id;
    accepted_state_sample(d,fvf,stride,receiver);
    if(receiver&&st.placed)for(unsigned i=0;i<3;++i)st.scene_eye[i]=eye[i];
    if(!receiver)++st.receiver_only_exclusions;return true;
}
void observe(IDirect3DDevice9*d,const Mat&v){D3DMATRIX p{};if(FAILED(d->GetTransform(D3DTS_PROJECTION,&p)))return;
    for(auto&c:st.cameras)if(c.count&&c.view==v){++c.count;return;}for(auto&c:st.cameras)if(!c.count){c.view=v;c.projection=matrix(p);c.count=1;return;}}
void place(){unsigned best=0;for(unsigned i=1;i<8;++i)if(st.cameras[i].count>st.cameras[best].count)best=i;
    if(!st.cameras[best].count)return;auto&v=st.cameras[best].view;st.scene_projection=st.cameras[best].projection;auto inv=shadow_core::inverse_rigid(v);auto p=shadow_core::transform({0,0,0,1},inv);
    // Trial-003 measured a visible lamp-foot receiver at ~1092 camera-z.
    // Put the authored light BEYOND and ABOVE that caster, so its projection
    // extends toward the camera onto visible road, not away into the horizon.
    // At a ~900 ground height and ~300 caster height, a foot at z~1100
    // projects to z~850 (light z~1600), inside the visible receiving pool.
    // It is fixed in world after placement; scene_eye tracks only camera
    // identity, not light motion. F8 deliberately requests a new placement.
    for(unsigned i=0;i<3;++i){st.scene_eye[i]=p[i];st.light[i]=p[i]+1600*v[i*4+2];}st.light[2]+=800;
    st.light_view=shadow_core::down_view(st.light);st.light_projection=shadow_core::projection(SCALE,NEAR_Z,FAR_Z);st.placed=true;maps.invalidate();
    std::ostringstream s;s<<"\"event\":\"fixed_spot_placed\",\"position\":["<<st.light[0]<<','<<st.light[1]<<','<<st.light[2]<<"],\"main_camera\":["<<p[0]<<','<<p[1]<<','<<p[2]<<"],\"selected_view_count\":"<<st.cameras[best].count<<",\"light_generation\":"<<maps.generation<<",\"far_radius\":"<<FAR_Z<<",\"spot_degrees\":120,\"game_light_identified\":false";event(s.str());
}

struct DrawArgs{bool indexed;INT base;UINT min,vertices,start,primitives;};
void capture(IDirect3DDevice9*d,const std::string&phase,HRESULT&read_hr,bool&saved);

// Copy only demonstrated safe managed buffers or the successfully intercepted
// write-time data; never retain a game buffer or invent missing bytes.
bool visible_select_draw(IDirect3DDevice9*d,const DrawArgs&a,float camera_height,std::vector<visible_geometry::Run>&runs,const visible_geometry::Index*selected_index=nullptr){
    if(!visible_ready||!a.indexed||!a.vertices||!a.primitives)return false;
    auto unavailable=[&](const char*reason){
        ++visible_unknown;static std::vector<std::string>seen;
        std::ostringstream s;s<<"\"event\":\"visible_read_rejection\",\"reason\":\""<<reason<<"\",\"vertices\":"<<a.vertices<<",\"primitives\":"<<a.primitives<<",\"start\":"<<a.start;
        if(seen.size()<24&&std::find(seen.begin(),seen.end(),s.str())==seen.end()){seen.push_back(s.str());event(s.str());}return false;};
    Ref<IDirect3DVertexBuffer9>vb;Ref<IDirect3DIndexBuffer9>ib;UINT offset=0,stride=0;
    D3DVERTEXBUFFER_DESC vd{};D3DINDEXBUFFER_DESC id{};
    if(FAILED(d->GetStreamSource(0,&vb.p,&offset,&stride))||!vb.p||FAILED(vb.p->GetDesc(&vd))||
       FAILED(d->GetIndices(&ib.p))||!ib.p||FAILED(ib.p->GetDesc(&id))||stride<12||stride>128)return false;
    unsigned width=id.Format==D3DFMT_INDEX16?2:id.Format==D3DFMT_INDEX32?4:0;
    uint64_t count=uint64_t(a.primitives)*3,ibytes=count*width,vbytes=uint64_t(a.vertices)*stride,at=0;
    if(!width||ibytes>4*1024*1024||vbytes>8*1024*1024||
       !observation::span(id.Size,uint64_t(a.start)*width,ibytes)||
       !observation::vertex_offset(vd.Size,offset,stride,a.base,a.min,at)||!observation::span(vd.Size,at,vbytes))return false;
    bool direct=eligible(vd.Usage,vd.Pool)&&eligible(id.Usage,id.Pool);
    std::vector<unsigned char>raw_i(size_t(ibytes),0),raw_v;
    if(direct){raw_v.resize(size_t(vbytes));void*p=nullptr;
        auto hr=ib.p->Lock(UINT(uint64_t(a.start)*width),UINT(ibytes),&p,D3DLOCK_READONLY);
        if(FAILED(hr))return false;if(p)memcpy(raw_i.data(),p,size_t(ibytes));auto uh=ib.p->Unlock();if(!p||FAILED(uh))return false;
        p=nullptr;hr=vb.p->Lock(UINT(at),UINT(vbytes),&p,D3DLOCK_READONLY);
        if(FAILED(hr))return false;if(p)memcpy(raw_v.data(),p,size_t(vbytes));uh=vb.p->Unlock();if(!p||FAILED(uh))return false;
    }else if(!write_capture::store.sample((uintptr_t)ib.p,uint64_t(a.start)*width,UINT(ibytes),raw_i.data()))return unavailable("index_write_range");
    std::vector<visible_geometry::Point>points(a.vertices);std::vector<unsigned char>sampled(a.vertices,0);
    std::vector<uint32_t>indices(size_t(count),0);
    for(size_t i=0;i<indices.size();++i){uint32_t value=0;memcpy(&value,raw_i.data()+i*width,width);
        if(value<a.min||uint64_t(value)-a.min>=a.vertices)return false;indices[i]=value-a.min;
        if(!sampled[indices[i]]){auto byte_offset=at+uint64_t(indices[i])*stride;
            if(direct)memcpy(points[indices[i]].data(),raw_v.data()+size_t(indices[i])*stride,12);
            else if(!write_capture::store.sample((uintptr_t)vb.p,byte_offset,12,points[indices[i]].data()))return unavailable("vertex_write_range");
            sampled[indices[i]]=1;}}
    // candidate() requires the identity world matrix. These are actual draw
    // positions in the same representation as the retained scene positions.
    auto report=(selected_index?*selected_index:visible_index).inspect(points,indices,camera_height);
    static std::vector<std::string>seen;
    if(seen.size()<32){
        std::array<float,3>lo{INFINITY,INFINITY,INFINITY},hi{-INFINITY,-INFINITY,-INFINITY};
        for(size_t i=0;i<points.size();++i)if(sampled[i])for(unsigned k=0;k<3;++k){lo[k]=std::min(lo[k],points[i][k]);hi[k]=std::max(hi[k],points[i][k]);}
        bool finite_bounds=true;for(unsigned k=0;k<3;++k)finite_bounds &= std::isfinite(lo[k])&&std::isfinite(hi[k]);
        if(!finite_bounds){lo={0,0,0};hi={0,0,0};}
        std::ostringstream s;s<<"\"event\":\"visible_triangle_report\",\"index\":\""<<(selected_index?"wall":"road")<<"\",\"vertices\":"<<a.vertices<<",\"primitives\":"<<a.primitives
          <<",\"target\":"<<report.target<<",\"other\":"<<report.other<<",\"unknown\":"<<report.unknown<<",\"ambiguous\":"<<report.ambiguous<<",\"degenerate\":"<<report.degenerate<<",\"invalid\":"<<report.invalid
          <<",\"bounds_finite\":"<<(finite_bounds?"true":"false")<<",\"bounds_world\":["<<lo[0]<<','<<lo[1]<<','<<lo[2]<<','<<hi[0]<<','<<hi[1]<<','<<hi[2]<<']';
        if(std::find(seen.begin(),seen.end(),s.str())==seen.end()){seen.push_back(s.str());event(s.str());}}
    if(!report.replayable())return false;runs=std::move(report.runs);return true;
}
// Restrict readback to the observer's previously demonstrated safe path. No
// dynamic/default/WRITEONLY storage is locked and no game buffer is retained.
// Resource caps are per-request safety choices, not completeness claims.
void pick_candidate(IDirect3DDevice9*d,const Mat&view,const Mat&world,DWORD fvf,const DrawArgs&a){
    if(!st.pick_pending)return;++st.pick_considered;
    if(!a.indexed){++st.pick_ineligible;++st.pick_unindexed;return;}
    if(!equal(d,D3DRS_ZWRITEENABLE,TRUE)||!equal(d,D3DRS_ALPHABLENDENABLE,FALSE)||
       !equal(d,D3DRS_ALPHATESTENABLE,FALSE)||!a.vertices||!a.primitives){++st.pick_ineligible;++st.pick_state;return;}
    Ref<IDirect3DVertexBuffer9>vb;Ref<IDirect3DIndexBuffer9>ib;UINT offset=0,stride=0;
    D3DVERTEXBUFFER_DESC vd{};D3DINDEXBUFFER_DESC id{};D3DMATRIX proj{};D3DVIEWPORT9 vp{};
    if(FAILED(d->GetStreamSource(0,&vb.p,&offset,&stride))||!vb.p||FAILED(vb.p->GetDesc(&vd))||
       FAILED(d->GetIndices(&ib.p))||!ib.p||FAILED(ib.p->GetDesc(&id))||stride<12||stride>128||
       FAILED(d->GetTransform(D3DTS_PROJECTION,&proj))||FAILED(d->GetViewport(&vp))){++st.pick_ineligible;return;}
    unsigned width=id.Format==D3DFMT_INDEX16?2:id.Format==D3DFMT_INDEX32?4:0;
    uint64_t index_count=uint64_t(a.primitives)*3,ibytes=index_count*width,vbytes=uint64_t(a.vertices)*stride,at=0;
    if(!width||ibytes>4*1024*1024||vbytes>8*1024*1024||
       !observation::span(id.Size,uint64_t(a.start)*width,ibytes)||
       !observation::vertex_offset(vd.Size,offset,stride,a.base,a.min,at)||!observation::span(vd.Size,at,vbytes)){
        ++st.pick_ineligible;return;}
    bool direct=eligible(vd.Usage,vd.Pool)&&eligible(id.Usage,id.Pool);if(!direct)++st.pick_unsafe;
    std::vector<unsigned char>raw_i(size_t(ibytes),0),raw_v;
    if(direct){raw_v.resize(size_t(vbytes));void*p=nullptr;HRESULT lh=ib.p->Lock(UINT(uint64_t(a.start)*width),UINT(ibytes),&p,D3DLOCK_READONLY);
        if(FAILED(lh)){++st.pick_lock_failures;return;}if(p)memcpy(raw_i.data(),p,size_t(ibytes));
        HRESULT uh=ib.p->Unlock();if(!p||FAILED(uh)){++st.pick_lock_failures;return;}
        p=nullptr;lh=vb.p->Lock(UINT(at),UINT(vbytes),&p,D3DLOCK_READONLY);
        if(FAILED(lh)){++st.pick_lock_failures;return;}if(p)memcpy(raw_v.data(),p,size_t(vbytes));
        uh=vb.p->Unlock();if(!p||FAILED(uh)){++st.pick_lock_failures;return;}
    }else if(!write_capture::store.sample((uintptr_t)ib.p,uint64_t(a.start)*width,UINT(ibytes),raw_i.data())){++st.pick_unknown;return;}
    std::vector<std::array<float,3>>points(a.vertices);std::vector<unsigned char>sampled(a.vertices,0);std::vector<uint32_t>indices(size_t(index_count),0);
    for(size_t i=0;i<indices.size();++i){uint32_t value=0;memcpy(&value,raw_i.data()+i*width,width);
        if(value<a.min||uint64_t(value)-a.min>=a.vertices){++st.pick_ineligible;return;}indices[i]=value-a.min;
        if(!sampled[indices[i]]){float xyz[3];auto byte_offset=at+uint64_t(indices[i])*stride;
            if(direct)memcpy(xyz,raw_v.data()+size_t(indices[i])*stride,12);
            else if(!write_capture::store.sample((uintptr_t)vb.p,byte_offset,12,xyz)){++st.pick_unknown;return;}
            auto v=shadow_core::transform({xyz[0],xyz[1],xyz[2],1},world);
            for(unsigned j=0;j<3;++j){if(!std::isfinite(v[j])){++st.pick_ineligible;return;}points[indices[i]][j]=v[j];}sampled[indices[i]]=1;}}
    ++st.pick_sampled;
    auto hit=shadow_core::pixel_hit(points,indices,shadow_core::multiply(view,matrix(proj)),float(vp.Width),float(vp.Height),st.pick_x,st.pick_y);
    if(!hit.found)return;++st.pick_hits;if(st.pick_found&&hit.depth>=st.pick_best)return;
    std::vector<std::array<float,3>>geometry;
    if(!shadow_core::component(points,indices,hit.triangle,geometry))return;
    st.pick_found=true;st.pick_best=hit.depth;st.picked_world=world;st.picked_fvf=fvf;
    st.picked_vertices=a.vertices;st.picked_primitives=a.primitives;st.picked_base=a.base;st.picked_min=a.min;st.picked_start=a.start;st.picked_geometry.swap(geometry);
}
void finish_pick(){if(!st.pick_pending||st.frame<=st.pick_started_frame)return;st.pick_pending=false;st.selected=st.pick_found;
    std::ostringstream stats;stats<<",\"considered\":"<<st.pick_considered<<",\"ineligible\":"<<st.pick_ineligible<<",\"unindexed\":"<<st.pick_unindexed<<",\"state_excluded\":"<<st.pick_state
        <<",\"unsafe_for_readonly\":"<<st.pick_unsafe<<",\"write_shadow_unknown\":"<<st.pick_unknown<<",\"sampled\":"<<st.pick_sampled
        <<",\"lock_failures\":"<<st.pick_lock_failures<<",\"pixel_hits\":"<<st.pick_hits<<",\"write_shadow_peak\":"<<write_capture::store.peak<<",\"write_shadow_capacity_misses\":"<<write_capture::store.capacity_misses;
    if(!st.selected){event("\"event\":\"single_caster_pick\",\"found\":false"+stats.str());return;}
    st.selected_geometry.swap(st.picked_geometry);release_independent();st.selected_only=true;
    std::array<float,3>lo=st.selected_geometry[0],hi=lo;
    for(auto&p:st.selected_geometry)for(unsigned i=0;i<3;++i){lo[i]=std::min(lo[i],p[i]);hi[i]=std::max(hi[i],p[i]);}
    // A fixed trial light, based on actual component bounds, not object origin.
    unsigned best=0;for(unsigned i=1;i<8;++i)if(st.cameras[i].count>st.cameras[best].count)best=i;
    auto&view=st.cameras[best].view;
    for(unsigned i=0;i<3;++i)st.light[i]=(lo[i]+hi[i])*.5f+250*view[i*4+2];st.light[2]+=600;
    st.light_view=shadow_core::down_view(st.light);maps.invalidate();
    std::ostringstream s;s<<"\"event\":\"single_caster_pick\",\"found\":true,\"fvf\":"<<st.picked_fvf<<",\"pixel_depth\":"<<st.pick_best
       <<",\"draw_vertices\":"<<st.picked_vertices<<",\"draw_primitives\":"<<st.picked_primitives<<",\"component_triangles\":"<<st.selected_geometry.size()/3
       <<",\"bounds_min\":["<<lo[0]<<','<<lo[1]<<','<<lo[2]<<"],\"bounds_max\":["<<hi[0]<<','<<hi[1]<<','<<hi[2]
       <<"],\"light\":["<<st.light[0]<<','<<st.light[1]<<','<<st.light[2]<<"],\"identity\":\"submitted_connected_component_not_engine_object_id\""<<stats.str();event(s.str());
}
void pick_bsp(float x,float y,bool automatic=false){
    if(!bsp_have_view||bsp_view_frame!=st.frame){st.selected=false;st.selected_geometry.clear();release_independent();
        event("\"event\":\"bsp_surface_pick\",\"found\":false,\"reason\":\"no_current_scene_view\"");return;}
    bsp_scene::Scene::Choice chosen;
    if(automatic){std::vector<std::array<float,2>>pixels;for(float y0:{150.f,210.f,270.f})for(float x0:{180.f,260.f,340.f,420.f,500.f,580.f,660.f,740.f,820.f})pixels.push_back({x0,y0});
        auto sample=bsp.choose_samples(bsp_view,bsp_projection,pixels,bsp_tried_surfaces);chosen=sample.choice;x=sample.x;y=sample.y;
        if(chosen.found)bsp_tried_surfaces.push_back(chosen.surface);}
    else chosen=bsp.choose(bsp_view,bsp_projection,x,y);
    st.selected=chosen.found;st.selected_only=true;st.selected_geometry.clear();release_independent();
    std::ostringstream camera;camera<<",\"strategy\":\""<<(automatic?"independent_nearest_ray_grid":"explicit_pixel")<<"\",\"pixel\":";
    if(automatic&&!chosen.found)camera<<"null";else camera<<'['<<x<<','<<y<<']';camera<<",\"view_frame\":"<<bsp_view_frame<<",\"view\":[";
    for(unsigned i=0;i<16;++i){if(i)camera<<',';camera<<bsp_view[i];}camera<<"],\"projection\":[";
    for(unsigned i=0;i<16;++i){if(i)camera<<',';camera<<bsp_projection[i];}camera<<']';
    if(!chosen.found){event("\"event\":\"bsp_surface_pick\",\"found\":false,\"reason\":\""+std::string(automatic?"no_eligible_grid_ray":std::isfinite(chosen.score)?"nearest_BSP_ineligible_or_nonwall":"no_BSP_intersection")+"\",\"surface\":"+std::to_string(chosen.surface)+camera.str());return;}
    st.selected_geometry=bsp.surface(chosen.surface);auto lo=st.selected_geometry[0],hi=lo;
    for(auto&p:st.selected_geometry)for(unsigned j=0;j<3;++j){lo[j]=std::min(lo[j],p[j]);hi[j]=std::max(hi[j],p[j]);}
    auto eye=shadow_core::transform({0,0,0,1},shadow_core::inverse_rigid(bsp_view));
    auto n=chosen.normal;float facing=0;for(unsigned j=0;j<3;++j)facing+=(eye[j]-chosen.center[j])*n[j];if(facing<0)for(auto&v:n)v=-v;
    for(unsigned j=0;j<3;++j)st.light[j]=chosen.center[j]-200*n[j];st.light[2]=hi[2]+250;
    st.light_view=shadow_core::down_view(st.light);maps.invalidate();st.independent_buffer_failed=false;
    std::ostringstream s;s<<"\"event\":\"bsp_surface_pick\",\"found\":true,\"node\":"<<chosen.node<<",\"surface\":"<<chosen.surface<<",\"triangles\":"<<st.selected_geometry.size()/3
        <<",\"bounds_min\":["<<lo[0]<<','<<lo[1]<<','<<lo[2]<<"],\"bounds_max\":["<<hi[0]<<','<<hi[1]<<','<<hi[2]<<"],\"hit\":["<<chosen.center[0]<<','<<chosen.center[1]<<','<<chosen.center[2]<<"],\"ray_distance\":"<<chosen.score<<",\"light\":["<<st.light[0]<<','<<st.light[1]<<','<<st.light[2]<<']'<<camera.str();event(s.str());
}
void external_command(unsigned command,float x,float y){
    if(command>=20&&command<=28){hybrid::command(command);return;}
    if(limited_caster&&command>=17&&command<=19){visible_enabled=command!=19;visible_pass=command==17?1:command==18?2:0;st.mode=visible_pass;
        event("\"event\":\"retained_caster_mode\",\"mode\":"+std::to_string(visible_pass)+",\"frame\":"+std::to_string(st.frame));return;}
    if(command==15||command==16){visible_enabled=command==15;visible_pass=0;st.mode=0;
        event("\"event\":\"visible_diagnostic_mode\",\"enabled\":"+std::string(visible_enabled?"true":"false")+",\"frame\":"+std::to_string(st.frame));return;}
    if(st.selected_mask)event("\"event\":\"selected_mask_matches\",\"matching_draws\":"+std::to_string(st.mask_matches));
    st.selected_mask=false;bsp_outline=false;bsp_mask_zfunc=D3DCMP_LESSEQUAL;bsp_receiver_probe=0;st.diagnostic_casters=false;
    if((command==1||command==7||command==12)&&bsp_enabled){if(command!=12)bsp_tried_surfaces.clear();st.mode=1;pick_bsp(x,y,command!=1);}
    if(command==1&&!bsp_enabled){st.mode=1;st.selected=false;st.selected_only=true;st.pick_pending=true;st.pick_found=false;st.pick_started_frame=st.frame;st.pick_x=x;st.pick_y=y;st.independent_buffer_failed=false;
        st.pick_considered=st.pick_ineligible=st.pick_lock_failures=st.pick_hits=st.pick_unindexed=st.pick_state=st.pick_unsafe=st.pick_unknown=st.pick_sampled=0;st.picked_geometry.clear();st.selected_geometry.clear();release_independent();maps.invalidate();}
    if(command==2){st.mode=1;st.selected_mask=st.selected;st.mask_matches=0;}
    if(command==3)st.mode=1;
    if(command==4){st.mode=2;st.selected_only=true;}
    if(command==5)st.mode=0;
    if(command==6&&bsp_enabled){st.mode=1;st.selected_mask=st.selected;bsp_outline=true;}
    // Fixture-only depth discrimination; these filled masks are diagnostics,
    // not visibility or shadow quality evidence.
    if((command==8||command==9)&&bsp_enabled){st.mode=1;st.selected_mask=st.selected;bsp_mask_zfunc=command==8?D3DCMP_ALWAYS:D3DCMP_GREATER;}
    if((command==10||command==11||command==13||command==14)&&bsp_enabled){st.mode=1;st.diagnostic_casters=true;bsp_receiver_probe=command;}
    if(bsp_enabled)bsp_probe_pending=command!=5;
    if(bsp_enabled)bsp_timing_bucket=command==3?1:command==4?2:command==5?0:-1;
    maps.invalidate();
    event("\"event\":\"single_caster_command\",\"command\":"+std::to_string(command)+",\"mode\":"+std::to_string(st.mode)+",\"selected\":"+(st.selected?"true":"false"));
}

bool independent_buffers(IDirect3DDevice9*d){
    if(independent_vb&&independent_ib)return true;
    if(st.independent_buffer_failed||st.selected_geometry.empty()||st.selected_geometry.size()>UINT_MAX/sizeof(st.selected_geometry[0]))return false;
    Guard guard;auto before=device_refs(d);UINT count=UINT(st.selected_geometry.size());
    HRESULT vh=d->CreateVertexBuffer(count*sizeof(st.selected_geometry[0]),D3DUSAGE_WRITEONLY,D3DFVF_XYZ,D3DPOOL_DEFAULT,&independent_vb,nullptr);
    HRESULT ih=SUCCEEDED(vh)?d->CreateIndexBuffer(count*sizeof(uint32_t),D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&independent_ib,nullptr):E_FAIL;
    bool ok=SUCCEEDED(vh)&&SUCCEEDED(ih)&&independent_vb&&independent_ib;void*p=nullptr;
    if(ok){auto hr=independent_vb->Lock(0,0,&p,0);ok=SUCCEEDED(hr);if(ok){if(p)memcpy(p,st.selected_geometry.data(),count*sizeof(st.selected_geometry[0]));
            bool unlocked=SUCCEEDED(independent_vb->Unlock());ok=p&&unlocked;}}
    p=nullptr;if(ok){auto hr=independent_ib->Lock(0,0,&p,0);ok=SUCCEEDED(hr);if(ok){if(p)for(UINT i=0;i<count;++i)static_cast<uint32_t*>(p)[i]=i;
            bool unlocked=SUCCEEDED(independent_ib->Unlock());ok=p&&unlocked;}}
    auto after=device_refs(d);independent_device_refs=after>=before?after-before:0;if(after<before)ok=false;
    std::ostringstream s;s<<"\"event\":\"independent_caster_buffers\",\"ok\":"<<(ok?"true":"false")<<",\"vertex_hr\":"<<vh<<",\"index_hr\":"<<ih
       <<",\"world_vertices\":"<<count<<",\"owned_device_reference_delta\":"<<independent_device_refs;event(s.str());
    if(!ok){release_independent();st.independent_buffer_failed=true;}return ok;
}
// State touched by private geometry in addition to the renderer's Snapshot.
// Explicitly preserve the actual declaration, stream, indices and VS registers.
struct GeometrySnapshot{
    IDirect3DDevice9*d;Ref<IDirect3DVertexDeclaration9>decl;Ref<IDirect3DVertexBuffer9>vb;Ref<IDirect3DIndexBuffer9>ib;
    Ref<IDirect3DStateBlock9>block;
    UINT offset=0,stride=0;DWORD fvf=0,fill=0;float constants[32]{};
    explicit GeometrySnapshot(IDirect3DDevice9*x):d(x){}
    bool capture(){return SUCCEEDED(d->CreateStateBlock(D3DSBT_ALL,&block.p))&&block.p&&SUCCEEDED(block.p->Capture())&&SUCCEEDED(d->GetRenderState(D3DRS_FILLMODE,&fill))&&SUCCEEDED(d->GetFVF(&fvf))&&SUCCEEDED(d->GetVertexDeclaration(&decl.p))&&decl.p&&SUCCEEDED(d->GetStreamSource(0,&vb.p,&offset,&stride))&&
        SUCCEEDED(d->GetIndices(&ib.p))&&SUCCEEDED(d->GetVertexShaderConstantF(0,constants,8));}
    bool restore(){bool ok=block.p&&SUCCEEDED(block.p->Apply());
        Ref<IDirect3DVertexDeclaration9>dc;Ref<IDirect3DVertexBuffer9>vc;Ref<IDirect3DIndexBuffer9>ic;UINT o=0,s=0;float c[32]{};
        DWORD current_fvf=0;ok &= SUCCEEDED(d->GetFVF(&current_fvf))&&current_fvf==fvf;
        DWORD current_fill=0;ok &= SUCCEEDED(d->GetRenderState(D3DRS_FILLMODE,&current_fill))&&current_fill==fill;
        ok &= SUCCEEDED(d->GetVertexDeclaration(&dc.p))&&dc.p==decl.p;
        ok &= SUCCEEDED(d->GetStreamSource(0,&vc.p,&o,&s))&&vc.p==vb.p&&o==offset&&s==stride;
        ok &= SUCCEEDED(d->GetIndices(&ic.p))&&ic.p==ib.p;
        ok &= SUCCEEDED(d->GetVertexShaderConstantF(0,c,8))&&!memcmp(c,constants,sizeof(c));
        if(!ok){std::ostringstream log;log<<"\"event\":\"geometry_restore_mismatch\",\"fvf_expected\":"<<fvf<<",\"fvf_actual\":"<<current_fvf
            <<",\"decl_expected\":"<<uintptr_t(decl.p)<<",\"decl_actual\":"<<uintptr_t(dc.p)<<",\"vb_equal\":"<<(vc.p==vb.p?"true":"false")
            <<",\"ib_equal\":"<<(ic.p==ib.p?"true":"false")<<",\"offset_expected\":"<<offset<<",\"offset_actual\":"<<o
            <<",\"stride_expected\":"<<stride<<",\"stride_actual\":"<<s<<",\"vs_constants_equal\":"<<(!memcmp(c,constants,sizeof(c))?"true":"false");event(log.str());}return ok;}
};
bool independent_pass(IDirect3DDevice9*d,const Mat&view,Snapshot&snapshot,bool mask){
    if(!independent_buffers(d))return true;
    GeometrySnapshot extra(d);if(!extra.capture()){++st.failures;st.frame_caster_failed=true;return true;}
    D3DMATRIX projection{};bool have_projection=SUCCEEDED(d->GetTransform(D3DTS_PROJECTION,&projection));if(bsp_enabled&&mask)projection=native(bsp_projection);
    Mat clip=mask?shadow_core::multiply(view,matrix(projection)):shadow_core::multiply(st.light_view,st.light_projection);
    float constants[32]{};memcpy(constants,clip.data(),64);memcpy(constants+16,st.light_view.data(),64);
    bool attempted=false;long hr=-1;
    bool restored=shadow_core::transaction([&]{
        if(!have_projection)return false;
        bool ok=mask?caster_coverage_setup(d):caster_setup(d);
        float range[4]={1/FAR_Z,0,0,mask?1.f:0.f};
        ok &= SUCCEEDED(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));
        if(mask)ok &= SUCCEEDED(d->SetRenderState(D3DRS_ZENABLE,TRUE))&&SUCCEEDED(d->SetRenderState(D3DRS_ZFUNC,bsp_enabled?bsp_mask_zfunc:D3DCMP_LESSEQUAL))&&SUCCEEDED(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE))&&
            SUCCEEDED(d->SetRenderState(D3DRS_STENCILENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_CLIPPLANEENABLE,0));
        if(mask&&bsp_enabled)ok &= SUCCEEDED(d->SetViewport(&bsp_viewport))&&SUCCEEDED(d->SetRenderState(D3DRS_FILLMODE,bsp_outline?D3DFILL_WIREFRAME:D3DFILL_SOLID))&&SUCCEEDED(d->SetRenderState(D3DRS_ZENABLE,!bsp_outline));
        ok &= SUCCEEDED(d->SetPixelShaderConstantF(0,range,1));
        ok &= SUCCEEDED(d->SetFVF(D3DFVF_XYZ))&&SUCCEEDED(d->SetVertexShader(independent_vs));
        ok &= SUCCEEDED(d->SetVertexShaderConstantF(0,constants,8));
        ok &= SUCCEEDED(d->SetStreamSource(0,independent_vb,0,sizeof(st.selected_geometry[0])))&&SUCCEEDED(d->SetIndices(independent_ib));
        return ok;
    },[&]{return long(find(d)->dip(d,D3DPT_TRIANGLELIST,0,0,UINT(st.selected_geometry.size()),0,UINT(st.selected_geometry.size()/3)));},
    [&]{bool a=extra.restore();bool b=snapshot.restore();return a&&b;},attempted,hr);
    if(!mask){if(attempted){maps.drawn(hr>=0);if(hr>=0)++st.casters;else{++st.failures;st.frame_caster_failed=true;}}
        else{maps.failed=true;st.frame_caster_failed=true;++st.failures;}}
    if(!restored){++st.restore_failures;st.disabled=true;event("\"event\":\"shadow_state_restore_failed\",\"pass\":\"independent\"");}
    if(!attempted||hr<0)event("\"event\":\"independent_caster_draw_failed\",\"mask\":"+std::string(mask?"true":"false")+",\"attempted\":"+(attempted?"true":"false")+",\"hr\":"+std::to_string(hr));
    return restored;
}

bool bsp_receiver_buffers(IDirect3DDevice9*d){if(bsp_receiver_vb)return true;if(bsp.receivers.empty())return false;
    auto before=device_refs(d);UINT bytes=UINT(bsp.receivers.size()*sizeof(bsp_scene::Vertex));
    auto hr=d->CreateVertexBuffer(bytes,D3DUSAGE_WRITEONLY,D3DFVF_XYZ|D3DFVF_NORMAL,D3DPOOL_DEFAULT,&bsp_receiver_vb,nullptr);bool ok=SUCCEEDED(hr)&&bsp_receiver_vb;void*p=nullptr;
    if(ok){ok=SUCCEEDED(bsp_receiver_vb->Lock(0,bytes,&p,0));if(ok){if(p)memcpy(p,bsp.receivers.data(),bytes);ok=SUCCEEDED(bsp_receiver_vb->Unlock())&&p;}}
    auto after=device_refs(d);bsp_device_refs=after>=before?after-before:0;
    event("\"event\":\"bsp_receiver_buffer\",\"ok\":"+std::string(ok?"true":"false")+",\"vertices\":"+std::to_string(bsp.receivers.size()));return ok;
}
void render_bsp(IDirect3DDevice9*d,const char*trigger){if(!bsp_enabled||!bsp_have_view||bsp_view_frame!=st.frame||!st.placed||!st.active||!st.mode||st.disabled||route(d)->recording||bsp_frame==st.frame)return;
    Guard guard;if(!resources(d))return;Snapshot snapshot(d);if(!snapshot.capture()){++st.failures;return;}
    Ref<IDirect3DSurface9>bb;auto bbhr=d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb.p);bool main_target=SUCCEEDED(bbhr)&&same(bb.p,snapshot.rt.p);
    static bool target_reported=false;if(!target_reported){target_reported=true;D3DSURFACE_DESC desc{};if(snapshot.rt.p)snapshot.rt.p->GetDesc(&desc);
        std::ostringstream s;s<<"\"event\":\"bsp_render_target\",\"is_backbuffer\":"<<(main_target?"true":"false")<<",\"rt_size\":["<<desc.Width<<','<<desc.Height<<"],\"viewport\":["<<snapshot.vp.Width<<','<<snapshot.vp.Height<<"],\"scene_viewport\":["<<bsp_viewport.Width<<','<<bsp_viewport.Height<<']';event(s.str());}
    if(!main_target)return;
    if(bsp_probe_pending){bsp_probe_pending=false;DWORD ze=0,zw=0,zf=0,bias=0,slope=0,fill=0;
        d->GetRenderState(D3DRS_ZENABLE,&ze);d->GetRenderState(D3DRS_ZWRITEENABLE,&zw);d->GetRenderState(D3DRS_ZFUNC,&zf);
        d->GetRenderState(D3DRS_DEPTHBIAS,&bias);d->GetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,&slope);d->GetRenderState(D3DRS_FILLMODE,&fill);
        std::ostringstream s;s<<"\"event\":\"bsp_comparison_boundary\",\"trigger\":\""<<trigger<<"\",\"frame\":"<<st.frame
            <<",\"selected\":"<<(st.selected?"true":"false")<<",\"outline\":"<<(bsp_outline?"true":"false")<<",\"mask\":"<<(st.selected_mask?"true":"false")<<",\"mask_z_func\":"<<bsp_mask_zfunc<<",\"receiver_probe\":"<<bsp_receiver_probe
            <<",\"viewport\":["<<snapshot.vp.X<<','<<snapshot.vp.Y<<','<<snapshot.vp.Width<<','<<snapshot.vp.Height<<"],\"scene_viewport\":["<<bsp_viewport.X<<','<<bsp_viewport.Y<<','<<bsp_viewport.Width<<','<<bsp_viewport.Height
            <<"],\"scene_depth_same_pointer\":"<<((bsp_scene_depth_frame==st.frame&&bsp_scene_depth_id==uintptr_t(snapshot.z.p))?"true":"false")
            <<",\"z_enable\":"<<ze<<",\"z_write\":"<<zw<<",\"z_func\":"<<zf<<",\"depth_bias_bits\":"<<bias<<",\"slope_bias_bits\":"<<slope<<",\"fill\":"<<fill;event(s.str());}
    // UE2 draws a 128x128 preview before its real HUD. Skipping that target
    // must not consume the main scene's once-per-frame submission.
    bsp_frame=st.frame;
    static bool active_reported=false;if(!active_reported){active_reported=true;event("\"event\":\"bsp_main_target_active\",\"frame\":"+std::to_string(st.frame));}
    if(st.mode==2&&st.selected&&!independent_pass(d,bsp_view,snapshot,false))return;
    GeometrySnapshot extra(d);if(!extra.capture()){++st.failures;return;}
    if(!bsp_receiver_buffers(d)){++st.failures;return;}
    bool combined=bsp_receiver_probe==13||bsp_receiver_probe==14;auto projection=combined?shadow_core::multiply(bsp_view,bsp_projection):bsp_projection;
    float constants[32]{};memcpy(constants,bsp_view.data(),64);memcpy(constants+16,projection.data(),64);bool attempted=false;long hr=-1;
    bool restored=shadow_core::transaction([&]{bool ok=receiver_setup(d,bsp_view,false)&&SUCCEEDED(d->SetViewport(&bsp_viewport));
        if(bsp_receiver_probe==11||bsp_receiver_probe==14)ok &= SUCCEEDED(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
        ok &= SUCCEEDED(d->SetRenderState(D3DRS_ZENABLE,TRUE))&&SUCCEEDED(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE))&&SUCCEEDED(d->SetRenderState(D3DRS_STENCILENABLE,FALSE));
        ok &= SUCCEEDED(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_CLIPPLANEENABLE,0));
        ok &= SUCCEEDED(d->SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL))&&SUCCEEDED(d->SetVertexShader(combined?bsp_combined_vs:bsp_receiver_vs))&&SUCCEEDED(d->SetVertexShaderConstantF(0,constants,8));
        ok &= SUCCEEDED(d->SetStreamSource(0,bsp_receiver_vb,0,sizeof(bsp_scene::Vertex)));return ok;
    },[&]{return long(find(d)->dp(d,D3DPT_TRIANGLELIST,0,UINT(bsp.receivers.size()/3)));},
    [&]{bool a=extra.restore();bool b=snapshot.restore();return a&&b;},attempted,hr);
    if(attempted&&hr>=0){++st.receivers;if(st.mode==2&&maps.usable())++st.shadow_receivers;if(GetTickCount64()-st.started>90000)++st.after90_draws;}else ++st.failures;
    if(!restored){++st.restore_failures;st.disabled=true;event("\"event\":\"bsp_state_restore_failed\"");return;}
    if(st.selected&&st.selected_mask){++st.mask_matches;independent_pass(d,bsp_view,snapshot,true);}
}
void before_hud(IDirect3DDevice9*d){if(inside||!bsp_enabled||d!=st.device)return;DWORD fvf=0;D3DMATRIX p{};
    const bool transformed=SUCCEEDED(d->GetFVF(&fvf))&&(fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZRHW;
    const bool ortho=SUCCEEDED(d->GetTransform(D3DTS_PROJECTION,&p))&&std::abs(p._34)<1e-6f&&std::abs(p._44-1)<1e-6f;
    if(transformed||ortho)render_bsp(d,transformed?"transformed_draw":"orthographic_draw");
}
HRESULT STDMETHODCALLTYPE clear9(IDirect3DDevice9*d,DWORD n,const D3DRECT*r,DWORD flags,D3DCOLOR color,float z,DWORD stencil){
    if(!inside&&d==st.device&&(flags&D3DCLEAR_ZBUFFER)&&!(flags&D3DCLEAR_TARGET)){
        std::lock_guard<std::recursive_mutex>lock(mutex);boundary::before_clear(d,flags,n);}
    if(!inside&&bsp_enabled&&d==st.device&&(flags&D3DCLEAR_ZBUFFER)){
        std::lock_guard<std::recursive_mutex>lock(mutex);
        if(st.external_capture&&bsp_probe_pending&&bsp_have_view&&bsp_view_frame==st.frame){Ref<IDirect3DSurface9>rt,bb,ds;D3DSURFACE_DESC desc{};
            d->GetRenderTarget(0,&rt.p);d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb.p);d->GetDepthStencilSurface(&ds.p);if(rt.p)rt.p->GetDesc(&desc);
            std::ostringstream s;s<<"\"event\":\"bsp_pending_depth_clear\",\"frame\":"<<st.frame<<",\"flags\":"<<flags<<",\"rect_count\":"<<n<<",\"depth\":"<<z
                <<",\"rt_is_backbuffer\":"<<(same(rt.p,bb.p)?"true":"false")<<",\"rt_size\":["<<desc.Width<<','<<desc.Height<<"],\"scene_depth_same_pointer\":"
                <<((bsp_scene_depth_frame==st.frame&&bsp_scene_depth_id==uintptr_t(ds.p))?"true":"false");event(s.str());}
        if(!(flags&D3DCLEAR_TARGET))render_bsp(d,"depth_clear");}
    auto hr=route(d)->clear(d,n,r,flags,color,z,stencil);
    if(!inside&&d==st.device&&(flags&D3DCLEAR_ZBUFFER)&&!(flags&D3DCLEAR_TARGET)){
        std::lock_guard<std::recursive_mutex>lock(mutex);boundary::after_clear(hr);}
    return hr;
}

int bucket(ULONGLONG elapsed){if(bsp_enabled&&st.external_capture)return elapsed>=90000&&st.mode==2?3:bsp_timing_bucket;
    if(!st.harness.enabled)return elapsed>=90000&&st.mode==2?3:-1;
    if(elapsed>=10000&&elapsed<11900&&st.mode==0)return 0;
    if(elapsed>=13000&&elapsed<13400&&st.mode==1)return 1;
    if(elapsed>=14500&&elapsed<14900&&st.mode==2)return 2;
    if(elapsed>=27000&&elapsed<34000&&st.mode==2)return 3;return -1;}
// Nonblocking GPU timestamps. The span includes original game draws, not only
// the MOD. No FLUSH, sleep or busy-wait is used to collect samples.
struct GpuTicket{IDirect3DQuery9*freq=nullptr,*disjoint=nullptr,*a=nullptr,*b=nullptr;bool pending=false;int bucket=-1;};
std::array<GpuTicket,8> tickets{};bool gpu_tried=false,gpu_ok=false;int gpu_current=-1;unsigned gpu_cursor=0,gpu_dropped=0;
void gpu_release(){for(auto&t:tickets){for(auto q:{t.freq,t.disjoint,t.a,t.b})if(q)q->Release();t={};}gpu_device_refs=0;gpu_ok=false;gpu_current=-1;}
void gpu_init(IDirect3DDevice9*d){if(gpu_tried)return;gpu_tried=true;gpu_ok=true;
    auto refs_before=device_refs(d);
    for(auto&t:tickets){gpu_ok &= SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&t.freq));gpu_ok &= SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&t.disjoint));gpu_ok &= SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&t.a));gpu_ok &= SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&t.b));}
    auto refs_after=device_refs(d);gpu_device_refs=refs_after>=refs_before?refs_after-refs_before:0;if(refs_after<refs_before)gpu_ok=false;
    if(!gpu_ok)gpu_release();event("\"event\":\"gpu_timestamp_queries\",\"created\":"+std::string(gpu_ok?"true":"false")+",\"owned_device_reference_delta\":"+std::to_string(gpu_device_refs)+",\"scope\":\"frame_draw_span_includes_original_not_input_latency\"");}
void gpu_begin(IDirect3DDevice9*d){if(gpu_current>=0)return;auto b=bucket(GetTickCount64()-st.started);if(b<0)return;gpu_init(d);if(!gpu_ok)return;
    auto&t=tickets[gpu_cursor];if(t.pending){++gpu_dropped;return;}t.bucket=b;
    auto a=t.disjoint->Issue(D3DISSUE_BEGIN),c=t.a->Issue(D3DISSUE_END);
    if(FAILED(a)||FAILED(c)){gpu_ok=false;event("\"event\":\"gpu_timestamp_issue_failed\",\"begin_hr\":"+std::to_string(a)+",\"timestamp_hr\":"+std::to_string(c));return;}gpu_current=int(gpu_cursor);}
void gpu_end(){if(gpu_current>=0){auto&t=tickets[gpu_current];auto a=t.b->Issue(D3DISSUE_END),b=t.disjoint->Issue(D3DISSUE_END),c=t.freq->Issue(D3DISSUE_END);t.pending=SUCCEEDED(a)&&SUCCEEDED(b)&&SUCCEEDED(c);gpu_cursor=(unsigned(gpu_current)+1)%tickets.size();gpu_current=-1;}
    for(auto&t:tickets)if(t.pending){UINT64 a=0,b=0,f=0;BOOL disjoint=TRUE;
        auto x=t.a->GetData(&a,sizeof(a),0),y=t.b->GetData(&b,sizeof(b),0),z=t.freq->GetData(&f,sizeof(f),0),w=t.disjoint->GetData(&disjoint,sizeof(disjoint),0);
        if(x==S_OK&&y==S_OK&&z==S_OK&&w==S_OK){if(!disjoint&&f&&b>=a&&st.gpu[t.bucket].size()<4096)st.gpu[t.bucket].push_back(1000.0*double(b-a)/double(f));t.pending=false;}
        else if(FAILED(x)||FAILED(y)||FAILED(z)||FAILED(w))t.pending=false;}}

template<class F>HRESULT draw(IDirect3DDevice9*d,D3DPRIMITIVETYPE type,const DrawArgs&args,F forward){if(inside)return forward();
    std::unique_lock<std::recursive_mutex>lock(mutex,std::defer_lock);try{lock.lock();}catch(...){return forward();}
    {Guard guard;try{if(st.active&&d==st.device)gpu_begin(d);}catch(...){}}
    before_hud(d);auto original=forward();if(!st.active||!st.mode||st.disabled||d!=st.device||FAILED(original)||route(d)->recording)return original;
    Guard guard;try{bool receiver=false;Mat view{};if(!candidate(d,type,args.indexed,receiver,view))return original;
        if(receiver&&bsp_enabled){bsp_view=view;D3DMATRIX p{};if(SUCCEEDED(d->GetTransform(D3DTS_PROJECTION,&p))&&SUCCEEDED(d->GetViewport(&bsp_viewport))){bsp_projection=matrix(p);bsp_have_view=true;bsp_view_frame=st.frame;
                if(st.external_capture&&bsp_probe_pending){Ref<IDirect3DSurface9>ds;if(SUCCEEDED(d->GetDepthStencilSurface(&ds.p))&&ds.p){bsp_scene_depth_id=uintptr_t(ds.p);bsp_scene_depth_frame=st.frame;}}}}
        if(!st.placed){if(receiver)observe(d,view);return original;}if(bsp_enabled)return original;if(!resources(d))return original;
        Snapshot snapshot(d);if(!snapshot.capture()){++st.failures;st.frame_caster_failed=true;return original;}
        bool attempted=false;long hr=-1;
        bool restored=true;
        DWORD caster_fvf=0;d->GetFVF(&caster_fvf);
        D3DMATRIX native_world{};d->GetTransform(D3DTS_WORLD,&native_world);auto world=matrix(native_world);
        pick_candidate(d,view,world,caster_fvf,args);
        if(st.selected_only&&st.selected&&st.mode==2&&st.independent_frame!=st.frame){st.independent_frame=st.frame;
            if(!independent_pass(d,view,snapshot,false))return original;}
        // Draw the diagnostic immediately after the corresponding original
        // draw, rather than the first scene draw (which can be overwritten by
        // the selected original later in the same frame). Metadata is only a
        // scheduling hint; it is not a semantic object identity.
        bool mask_match=args.indexed&&caster_fvf==st.picked_fvf&&world==st.picked_world&&args.base==st.picked_base&&
            args.min==st.picked_min&&args.start==st.picked_start&&args.vertices==st.picked_vertices&&args.primitives==st.picked_primitives;
        if(st.selected_only&&st.selected&&st.selected_mask&&mask_match&&st.mask_frame!=st.frame){st.mask_frame=st.frame;++st.mask_matches;
            if(!independent_pass(d,view,snapshot,true))return original;}
        if(!st.selected_only&&!(st.diagnostic_exclude_receiver_casters&&receiver)&&!(st.diagnostic_only_receiver_casters&&!receiver)&&!(st.exclude_static_mesh_casters&&caster_fvf==338)){
            restored=shadow_core::transaction([&]{return caster_setup(d);},[&]{return long(forward());},[&]{return snapshot.restore();},attempted,hr);
            if(attempted){maps.drawn(hr>=0);if(hr>=0)++st.casters;else{++st.failures;st.frame_caster_failed=true;}}else{maps.failed=true;st.frame_caster_failed=true;++st.failures;}
            if(!restored){++st.restore_failures;st.disabled=true;event("\"event\":\"shadow_state_restore_failed\",\"pass\":\"caster\"");return original;}
        }
        if(st.diagnostic_casters){restored=shadow_core::transaction([&]{return caster_coverage_setup(d);},[&]{return long(forward());},[&]{return snapshot.restore();},attempted,hr);
            if(!restored){++st.restore_failures;st.disabled=true;}return original;}
        if(receiver){bool usable=st.mode==2&&maps.usable();restored=shadow_core::transaction([&]{return receiver_setup(d,view,false,st.diagnostic_visibility);},[&]{return long(forward());},[&]{return snapshot.restore();},attempted,hr);
            if(attempted&&hr>=0){++st.receivers;if(usable)++st.shadow_receivers;if(GetTickCount64()-st.started>90000)++st.after90_draws;}else ++st.failures;
            if(!restored){++st.restore_failures;st.disabled=true;event("\"event\":\"shadow_state_restore_failed\",\"pass\":\"receiver\"");return original;}
            if(st.specular_enabled&&!st.diagnostic_visibility){restored=shadow_core::transaction([&]{return receiver_setup(d,view,true);},[&]{return long(forward());},[&]{return snapshot.restore();},attempted,hr);
                if(attempted&&hr>=0)++st.specular_draws;else ++st.failures;
                if(!restored){++st.restore_failures;st.disabled=true;event("\"event\":\"shadow_state_restore_failed\",\"pass\":\"specular\"");}}
        }
    }catch(...){++st.failures;}return original;
}
template<class F,class G>HRESULT visible_draw(IDirect3DDevice9*d,D3DPRIMITIVETYPE type,const DrawArgs&args,F forward,G replay){
    if(inside)return forward();std::unique_lock<std::recursive_mutex>lock(mutex,std::defer_lock);
    try{lock.lock();}catch(...){return forward();}
    auto original=forward();
    if(!visible_enabled||!visible_ready||!st.active||st.disabled||d!=st.device||FAILED(original)||route(d)->recording)return original;
    Guard guard;
    try{bool receiver=false;Mat view{};
        if(!candidate(d,type,args.indexed,receiver,view)||!receiver)return original;
        auto eye=shadow_core::transform({0,0,0,1},shadow_core::inverse_rigid(view));
        if(!std::isfinite(eye[2]))return original;
        std::vector<visible_geometry::Run>runs;++visible_considered;if(!visible_select_draw(d,args,eye[2],runs))return original;++visible_matched;
        // The full-size BackBuffer also receives skybox FFP draws. Bind scene
        // identity only after exact retained-road geometry was actually found.
        if(limited_caster&&visible_pass&&!st.placed){D3DMATRIX projection{};if(FAILED(d->GetTransform(D3DTS_PROJECTION,&projection)))return original;
            st.scene_projection=matrix(projection);for(unsigned i=0;i<3;++i)st.scene_eye[i]=eye[i];
            st.light=retained_light;st.light_view=shadow_core::down_view(st.light);st.light_projection=shadow_core::projection(SCALE,NEAR_Z,FAR_Z);st.placed=true;maps.invalidate();
            event("\"event\":\"retained_light_anchored\",\"recorded_world_position\":["+std::to_string(st.light[0])+","+std::to_string(st.light[1])+","+std::to_string(st.light[2])+"],\"current_camera\":["+std::to_string(eye[0])+","+std::to_string(eye[1])+","+std::to_string(eye[2])+"]");}
        if(!resources(d)){++visible_failed;return original;}
        Snapshot snapshot(d);if(!snapshot.capture()){++visible_failed;return original;}
        if(limited_caster&&visible_pass==2&&st.independent_frame!=st.frame){st.independent_frame=st.frame;
            if(!independent_pass(d,view,snapshot,false))return original;}
        bool pair=!visible_pair_attempted;HRESULT before_hr=E_FAIL,after_hr=E_FAIL;bool before=false,after=false;
        if(pair){visible_pair_attempted=true;capture(d,"surface-before-draw",before_hr,before);}
        bool attempted=false;long hr=-1;
        // Keep the original bound VB/IB, base/min/vertex range and FFP transform.
        // Only startIndex/primitiveCount select exact eligible triangle runs;
        // no private geometry, texture deletion or depth tolerance is involved.
        bool restored=shadow_core::transaction([&]{return visible_pass?receiver_setup(d,view,false):caster_coverage_setup(d);},[&]{
            HRESULT result=S_OK;for(const auto& run:runs){result=replay(args.start+run.first*3,run.count);if(FAILED(result))break;++visible_subset_calls;visible_subset_triangles+=run.count;}return long(result);
        },[&]{return snapshot.restore();},attempted,hr);
        if(attempted&&hr>=0){++st.receivers;if(visible_pass==2&&maps.usable())++st.shadow_receivers;}else{++visible_failed;++st.failures;}
        if(pair){capture(d,"surface-after-draw",after_hr,after);std::ostringstream s;
            s<<"\"event\":\"visible_same_draw_pair\",\"frame\":"<<st.frame<<",\"before_saved\":"<<(before?"true":"false")
             <<",\"before_hr\":"<<before_hr<<",\"after_saved\":"<<(after?"true":"false")<<",\"after_hr\":"<<after_hr
             <<",\"replay_hr\":"<<hr<<",\"restored\":"<<(restored?"true":"false");event(s.str());}
        if(!restored){++st.restore_failures;st.disabled=true;event("\"event\":\"visible_restore_failed\"");}
    }catch(...){++visible_failed;}return original;
}
#include "hybrid_render.h"
#include "boundary.h"
HRESULT STDMETHODCALLTYPE dip(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,INT b,UINT m,UINT v,UINT s,UINT n){auto f=find(d)->dip;auto hr=hybrid::draw(d,t,{true,b,m,v,s,n},[&]{return f(d,t,b,m,v,s,n);});if(!inside&&d==st.device)boundary::trace("DIP",hr);return hr;}
HRESULT STDMETHODCALLTYPE dp(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT s,UINT n){auto f=find(d)->dp;auto hr=hybrid::draw(d,t,{false,0,0,0,s,n},[&]{return f(d,t,s,n);});if(!inside&&d==st.device)boundary::trace("DP",hr);return hr;}
// UP draws are forwarded unchanged: shadow replay does not retain CPU pointers.
HRESULT STDMETHODCALLTYPE dpu(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT n,const void*p,UINT stride){std::lock_guard<std::recursive_mutex>lock(mutex);hybrid::before(d);auto hr=find(d)->dpu(d,t,n,p,stride);if(!inside&&d==st.device)boundary::trace("DPU",hr);return hr;}
HRESULT STDMETHODCALLTYPE dipu(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT m,UINT v,UINT n,const void*i,D3DFORMAT fmt,const void*p,UINT stride){std::lock_guard<std::recursive_mutex>lock(mutex);hybrid::before(d);auto hr=find(d)->dipu(d,t,m,v,n,i,fmt,p,stride);if(!inside&&d==st.device)boundary::trace("DIPU",hr);return hr;}

bool foreground(IDirect3DDevice9*d,HWND&window){D3DDEVICE_CREATION_PARAMETERS cp{};DWORD pid=0;bool ours=SUCCEEDED(d->GetCreationParameters(&cp))&&cp.hFocusWindow&&IsWindow(cp.hFocusWindow);
    if(ours){window=cp.hFocusWindow;GetWindowThreadProcessId(window,&pid);ours=pid==GetCurrentProcessId();}return ours&&GetForegroundWindow()==window;}
bool controls(IDirect3DDevice9*d){HWND w=nullptr;bool focused=foreground(d,w),capture=false;const WORD keys[]={VK_F7,VK_F8,VK_F11};
    for(unsigned i=0;i<3;++i)if(shadow_core::edge((GetAsyncKeyState(keys[i])&0x8000)!=0,focused,st.key_previous[i])){
        if(i==0){auto previous=st.mode;st.mode=(st.mode+1)%3;if(!previous&&st.mode)maps.invalidate();event("\"event\":\"mode_change\",\"mode\":"+std::to_string(st.mode)+",\"frame\":"+std::to_string(st.frame)+",\"original_key_not_consumed\":true");}
        if(i==1){st.placed=false;st.cameras={};maps.invalidate();event("\"event\":\"light_reanchor_request\"");}
        if(i==2&&st.manual_captures<8)capture=true;
    }
    return capture;}
void send_input(IDirect3DDevice9*d,const comparison_harness::Input&a){HWND w=nullptr;bool focused=foreground(d,w);
    if(a.action==6){st.diagnostic_exclude_receiver_casters=!st.diagnostic_exclude_receiver_casters;maps.invalidate();
        event("\"event\":\"test_exclude_receiver_representation\",\"enabled\":"+std::string(st.diagnostic_exclude_receiver_casters?"true":"false")+",\"frame\":"+std::to_string(st.frame));return;}
    if(a.action==7&&st.placed){st.light[0]+=400.f;st.light_view=shadow_core::down_view(st.light);maps.invalidate();
        std::ostringstream s;s<<"\"event\":\"test_light_shift\",\"position\":["<<st.light[0]<<','<<st.light[1]<<','<<st.light[2]<<"],\"frame\":"<<st.frame;event(s.str());return;}
    if(a.action==8){st.specular_enabled=!st.specular_enabled;event("\"event\":\"test_specular_component\",\"enabled\":"+std::string(st.specular_enabled?"true":"false")+",\"frame\":"+std::to_string(st.frame));return;}
    if(a.action==9){st.diagnostic_visibility=!st.diagnostic_visibility;event("\"event\":\"test_receiver_visibility\",\"enabled\":"+std::string(st.diagnostic_visibility?"true":"false")+",\"frame\":"+std::to_string(st.frame));return;}
    if(a.action==10){st.diagnostic_casters=!st.diagnostic_casters;return;}
    if(a.action==11){st.diagnostic_position=!st.diagnostic_position;return;}
    if(a.action==15){st.exclude_static_mesh_casters=!st.exclude_static_mesh_casters;maps.invalidate();
        event("\"event\":\"test_static_caster_ablation\",\"fvf\":338,\"excluded\":"+std::string(st.exclude_static_mesh_casters?"true":"false"));return;}
    if(a.action==16){st.diagnostic_only_receiver_casters=!st.diagnostic_only_receiver_casters;maps.invalidate();
        event("\"event\":\"test_only_receiver_casters\",\"enabled\":"+std::string(st.diagnostic_only_receiver_casters?"true":"false")+",\"frame\":"+std::to_string(st.frame));return;}
    if(a.action>=12&&a.action<=14){auto previous=st.mode;st.mode=unsigned(a.action-12);
        if(!previous&&st.mode)maps.invalidate();event("\"event\":\"comparison_mode\",\"mode\":"+std::to_string(st.mode)+",\"frame\":"+std::to_string(st.frame));return;}
    if(w){SetForegroundWindow(w);focused=GetForegroundWindow()==w;}UINT sent=0;BOOL moved=FALSE;
    if(focused&&a.action==1){INPUT i{};i.type=INPUT_KEYBOARD;i.ki.wVk=a.key;i.ki.dwFlags=a.up?KEYEVENTF_KEYUP:0;sent=SendInput(1,&i,sizeof(i));}
    if(focused&&a.action==2){RECT r{};POINT p{};if(GetClientRect(w,&r)){p.x=r.right*825/960;p.y=r.bottom*525/540;if(ClientToScreen(w,&p))moved=SetCursorPos(p.x,p.y);}}
    if(focused&&a.action==3){INPUT i{};i.type=INPUT_MOUSE;i.mi.dwFlags=a.up?MOUSEEVENTF_LEFTUP:MOUSEEVENTF_LEFTDOWN;sent=SendInput(1,&i,sizeof(i));}
    if(focused&&(a.action==4||a.action==5)){INPUT i{};i.type=INPUT_MOUSE;i.mi.dwFlags=MOUSEEVENTF_MOVE;i.mi.dx=a.action==4?120:-120;sent=SendInput(1,&i,sizeof(i));}
    event("\"event\":\"harness_owned_input\",\"action\":"+std::to_string(a.action)+",\"key\":"+std::to_string(a.key)+",\"up\":"+(a.up?"true":"false")+",\"foreground_owned\":"+(focused?"true":"false")+",\"sent\":"+std::to_string(sent)+",\"cursor_moved\":"+(moved?"true":"false")+",\"game_consumption_unverified\":true");}
void capture(IDirect3DDevice9*d,const std::string&phase,HRESULT&read_hr,bool&saved){Ref<IDirect3DSurface9>bb,cpu;D3DSURFACE_DESC desc{};D3DLOCKED_RECT rect{};read_hr=E_FAIL;saved=false;
    if(FAILED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb.p))||!bb.p||FAILED(bb.p->GetDesc(&desc))||!frame_capture::supported(desc.Width,desc.Height,desc.Format,desc.MultiSampleType,desc.Pool,desc.Usage))return;
    if(FAILED(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&cpu.p,nullptr))||!cpu.p)return;
    read_hr=d->GetRenderTargetData(bb.p,cpu.p);if(FAILED(read_hr)||FAILED(cpu.p->LockRect(&rect,nullptr,D3DLOCK_READONLY)))return;
    auto name=std::string("backbuffer-")+phase+".bmp";auto path=directory+std::wstring(name.begin(),name.end());FILE*f=nullptr;
    bool ok=rect.pBits&&frame_capture::pitch_ok(rect.Pitch);if(ok)f=_wfopen(path.c_str(),L"wbx");ok=ok&&f;
    if(f){auto header=frame_capture::header();ok=fwrite(header.data(),1,header.size(),f)==header.size();if(ok)ok=frame_capture::rows(rect.pBits,rect.Pitch,[&](const unsigned char*p,size_t n){return fwrite(p,1,n,f)==n;});if(fclose(f))ok=false;}
    ok=SUCCEEDED(cpu.p->UnlockRect())&&ok;saved=ok;}
double mean(const std::vector<double>&v){double sum=0;for(auto x:v)sum+=x;return v.empty()?0:sum/v.size();}
double pct(std::vector<double>v,double p){if(v.empty())return 0;std::sort(v.begin(),v.end());return v[size_t(std::ceil(p*v.size()))-1];}
void finish_event(const char*reason="test_harness_finished_core_runtime_not_stopped"){hybrid::finish();std::ostringstream s;s<<"\"event\":\"capture_finished\",\"reason\":\""<<reason<<"\",\"frame\":"<<st.frame
    <<",\"mode\":"<<st.mode<<",\"caster_draws\":"<<st.casters<<",\"receiver_draws\":"<<st.receivers<<",\"shadow_receiver_draws\":"<<st.shadow_receivers<<",\"specular_draws\":"<<st.specular_draws
    <<",\"completed_maps\":"<<st.complete_maps<<",\"map_resource_releases\":"<<st.map_releases<<",\"failed_passes\":"<<st.failures<<",\"restore_failures\":"<<st.restore_failures
    <<",\"reject_layout\":"<<st.rejected_layout<<",\"reject_state\":"<<st.rejected_state<<",\"reject_target\":"<<st.rejected_target<<",\"reject_resource\":"<<st.rejected_resource
    <<",\"receiver_only_exclusions\":"<<st.receiver_only_exclusions<<",\"receiver_draws_after_90_seconds\":"<<st.after90_draws<<",\"visible_considered\":"<<visible_considered<<",\"visible_unknown\":"<<visible_unknown<<",\"visible_matched\":"<<visible_matched<<",\"visible_failed\":"<<visible_failed<<",\"visible_subset_calls\":"<<visible_subset_calls<<",\"visible_subset_triangles\":"<<visible_subset_triangles<<",\"core_runtime_has_elapsed_stop\":false,\"gpu_query_dropped\":"<<gpu_dropped<<",\"timing\":[";
    for(unsigned i=0;i<4;++i){if(i)s<<',';s<<"{\"bucket\":"<<i<<",\"cpu_count\":"<<st.cpu[i].size()<<",\"cpu_mean_ms\":"<<mean(st.cpu[i])<<",\"cpu_p95_ms\":"<<pct(st.cpu[i],.95)<<",\"cpu_p99_ms\":"<<pct(st.cpu[i],.99)
        <<",\"gpu_count\":"<<st.gpu[i].size()<<",\"gpu_draw_span_mean_ms\":"<<mean(st.gpu[i])<<",\"gpu_draw_span_p95_ms\":"<<pct(st.gpu[i],.95)<<'}';}s<<"],\"caster_coverage\":\"partial_camera_submitted_opaque_FFP\",\"shadow_latency_frames\":1";event(s.str());}
template<class F>HRESULT present(IDirect3DDevice9*d,F forward){if(inside||d!=st.device)return forward();std::unique_lock<std::recursive_mutex>lock(mutex,std::defer_lock);try{lock.lock();}catch(...){return forward();}
    Guard guard;std::string phase;bool saved=false;HRESULT read_hr=E_FAIL;ULONGLONG elapsed=st.active?GetTickCount64()-st.started:0;
    if(st.active){gpu_end();bool manual=controls(d);const char*name=st.harness.shot(elapsed);if(manual)phase="manual-"+std::to_string(st.manual_captures++);else if(name)phase=name;
        if(st.external_capture&&GetTickCount64()-st.request_poll>=100){st.request_poll=GetTickCount64();
            auto file=_wfopen((directory+L"capture-request.txt").c_str(),L"rt");unsigned request=0,command=0;float x=0,y=0;
            if(file){int parsed=fscanf(file,"%u %u %f %f",&request,&command,&x,&y);fclose(file);if(parsed>=1&&request>st.last_request&&request<=128){
                st.last_request=request;st.pending_capture=request;st.capture_frame=st.frame+(command?4:0);
                if(command&&command<=28)external_command(command,x,y);}}}
        if(st.pending_capture&&st.frame>=st.capture_frame){char label[32]{};snprintf(label,sizeof(label),"request-%03u",st.pending_capture);phase=label;st.pending_capture=0;}
        LARGE_INTEGER now{};QueryPerformanceCounter(&now);int b=bucket(elapsed);
        if(b>=0&&!st.previous_capture&&phase.empty()&&st.previous.QuadPart&&st.cpu[b].size()<4096)st.cpu[b].push_back(1000.*double(now.QuadPart-st.previous.QuadPart)/double(st.frequency.QuadPart));st.previous=now;st.previous_capture=!phase.empty();
        hybrid::frame_time(!phase.empty());
        if(!phase.empty())try{capture(d,phase,read_hr,saved);boundary::checkpoint_pose(phase);}catch(...){}
    }
    if(st.active)boundary::pre_present(d,!phase.empty());
    auto hr=forward();if(st.active)boundary::post_present(hr);try{if(st.active){
        if(!phase.empty()){std::ostringstream s;s<<"\"event\":\"backbuffer_capture\",\"phase\":\""<<phase<<"\",\"filename\":\"backbuffer-"<<phase<<".bmp\",\"frame\":"<<st.frame<<",\"epoch\":"<<st.epoch
            <<",\"saved\":"<<(saved?"true":"false")<<",\"readback_hr\":"<<read_hr<<",\"present_hr\":"<<hr<<",\"mode\":"<<st.mode<<",\"light_generation\":"<<maps.generation<<",\"read_map_generation\":"<<maps.read_generation
            <<",\"read_map_valid\":"<<(maps.usable()?"true":"false")<<",\"read_map_frame\":"<<maps.read_frame
            <<",\"receiver_draws\":"<<st.receivers<<",\"receiver_draws_after_90_seconds\":"<<st.after90_draws<<",\"specular_draws\":"<<st.specular_draws
            <<",\"restore_failures\":"<<st.restore_failures<<",\"active_elapsed_ms\":"<<elapsed
            <<",\"late_cpu_count\":"<<st.cpu[3].size()<<",\"late_cpu_mean_ms\":"<<mean(st.cpu[3])<<",\"late_cpu_p95_ms\":"<<pct(st.cpu[3],.95)
            <<",\"late_gpu_count\":"<<st.gpu[3].size()<<",\"late_gpu_draw_span_mean_ms\":"<<mean(st.gpu[3])<<",\"late_gpu_draw_span_p95_ms\":"<<pct(st.gpu[3],.95)
            <<",\"capture_point\":\"game_BackBuffer_before_present\"";event(s.str());}
        if(st.frame_caster_failed)maps.failed=true;
        if(maps.complete(SUCCEEDED(hr)))++st.complete_maps;
        st.frame_caster_failed=false;
        finish_pick();
        if(st.mode&&!st.placed)place();
        st.harness.tick(elapsed,[&](const comparison_harness::Input&a){send_input(d,a);});
        if(st.external_capture&&!st.external_finished&&GetFileAttributesW((directory+L"controller-finished.txt").c_str())!=INVALID_FILE_ATTRIBUTES){
            st.external_finished=true;finish_event("external_test_controller_finished_core_runtime_not_stopped");}
        if(st.harness.end(elapsed))finish_event();++st.frame;
    }else if(!st.harness.enabled||GetFileAttributesW((directory+L"arm.txt").c_str())!=INVALID_FILE_ATTRIBUTES){st.active=true;st.started=GetTickCount64();QueryPerformanceFrequency(&st.frequency);
        event("\"event\":\"shadow_runtime_active\",\"test_harness\":"+std::string(st.harness.enabled?"true":"false")+",\"normal_mode_has_automatic_input\":false,\"core_runtime_has_elapsed_stop\":false");}}
    catch(...){}return hr;}
HRESULT STDMETHODCALLTYPE present9(IDirect3DDevice9*d,const RECT*a,const RECT*b,HWND c,const RGNDATA*e){auto f=find(d)->present;return present(d,[&]{return f(d,a,b,c,e);});}
HRESULT STDMETHODCALLTYPE presentex(IDirect3DDevice9Ex*d,const RECT*a,const RECT*b,HWND c,const RGNDATA*e,DWORD flags){auto f=find(d)->presentex;return present(d,[&]{return f(d,a,b,c,e,flags);});}
HRESULT STDMETHODCALLTYPE reset(IDirect3DDevice9*d,D3DPRESENT_PARAMETERS*p){auto f=find(d)->reset;std::lock_guard<std::recursive_mutex>lock(mutex);Guard guard;if(d==st.device){write_capture::store.reset((uintptr_t)d);release_resources();gpu_release();gpu_tried=false;format_failed=false;st.placed=false;st.cameras={};st.selected=false;st.selected_geometry.clear();st.independent_buffer_failed=false;++st.epoch;}auto hr=f(d,p);event("\"event\":\"shadow_reset\",\"hr\":"+std::to_string(hr));return hr;}
HRESULT STDMETHODCALLTYPE resetex(IDirect3DDevice9Ex*d,D3DPRESENT_PARAMETERS*p,D3DDISPLAYMODEEX*m){auto f=find(d)->resetex;std::lock_guard<std::recursive_mutex>lock(mutex);Guard guard;if(d==st.device){write_capture::store.reset((uintptr_t)d);release_resources();gpu_release();gpu_tried=false;format_failed=false;st.placed=false;st.cameras={};st.selected=false;st.selected_geometry.clear();st.independent_buffer_failed=false;++st.epoch;}return f(d,p,m);}
ULONG STDMETHODCALLTYPE release(IDirect3DDevice9*d){auto f=route(d)->release;if(inside)return f(d);std::lock_guard<std::recursive_mutex>lock(mutex);Guard guard;
    // A temporary guard prevents deletion while distinguishing a caller's
    // temporary reference from its final ownership. Resource-created public
    // device references are measured locally when those resources are made.
    d->AddRef();auto remaining=f(d);
    if(d==st.device&&shadow_core::release_owned_resources(remaining,map_device_refs+gpu_device_refs+independent_device_refs+bsp_device_refs+hybrid::owned_refs)){release_resources();gpu_release();st.placed=false;st.cameras={};}
    auto n=f(d);if(!n&&d==st.device){st.device=nullptr;st.active=false;}return n;}
HRESULT STDMETHODCALLTYPE begin_state(IDirect3DDevice9*d){auto*r=route(d);auto hr=r->begin(d);if(SUCCEEDED(hr))r->recording=true;return hr;}
HRESULT STDMETHODCALLTYPE end_state(IDirect3DDevice9*d,IDirect3DStateBlock9**p){auto*r=route(d);auto hr=r->end(d,p);if(SUCCEEDED(hr))r->recording=false;return hr;}
void device(IDirect3DDevice9*d,bool ex){std::lock_guard<std::recursive_mutex>lock(mutex);if(find(d)||devices.size()>=16)return;Route*r=nullptr;for(auto&v:routes)if(!v.vt){r=&v;break;}if(!r)return;
    auto vt=*(void***)d;r->vt=vt;r->release=(Release)vt[2];r->begin=(Begin)vt[60];r->end=(End)vt[61];r->clear=(Clear)vt[43];Device o{};o.vt=vt;
    o.dp=(DP)vt[81];o.dip=(DIP)vt[82];o.dpu=(DPU)vt[83];o.dipu=(DIPU)vt[84];o.present=(PRESENT)vt[17];o.reset=(RESET)vt[16];if(ex){o.presentex=(PRESENTEX)vt[121];o.resetex=(RESETEX)vt[132];}devices.push_back(o);
    void*unused=nullptr;bool ok=true;ok &= patch(vt,81,(void*)dp,&unused);ok &= patch(vt,82,(void*)dip,&unused);ok &= patch(vt,83,(void*)dpu,&unused);ok &= patch(vt,84,(void*)dipu,&unused);
    ok &= patch(vt,17,(void*)present9,&unused);ok &= patch(vt,16,(void*)reset,&unused);ok &= patch(vt,2,(void*)release,&unused);ok &= patch(vt,60,(void*)begin_state,&unused);ok &= patch(vt,61,(void*)end_state,&unused);ok &= patch(vt,43,(void*)clear9,&unused);
    if(ex){ok &= patch(vt,121,(void*)presentex,&unused);ok &= patch(vt,132,(void*)resetex,&unused);}if(!ok)st.disabled=true;
    if(!st.device){st.device=d;st.harness.enabled=GetFileAttributesW((directory+L"test-harness.txt").c_str())!=INVALID_FILE_ATTRIBUTES;st.harness.long_run=GetFileAttributesW((directory+L"long-harness.txt").c_str())!=INVALID_FILE_ATTRIBUTES;
        st.external_capture=GetFileAttributesW((directory+L"external-controller.txt").c_str())!=INVALID_FILE_ATTRIBUTES;
        hybrid::load();
        auto f=_wfopen((directory+L"bsp-scene.bin").c_str(),L"rb");if(f){bsp_enabled=bsp.load(f);fclose(f);}
        if(bsp_enabled){st.selected_only=true;st.selected=true;for(auto&v:bsp.receivers)st.selected_geometry.push_back(v.position);
            event("\"event\":\"bsp_scene_loaded\",\"nodes_including_ray_blockers\":"+std::to_string(bsp.nodes.size())+",\"opaque_triangles\":"+std::to_string(bsp.receivers.size()/3)+",\"coverage\":\"partial_BSP_verified_plain_Texture_only_not_static_mesh\"");}
        bsp_scene::Scene old_shader,old_plain;auto f19=_wfopen((directory+L"visible-019.bin").c_str(),L"rb");auto f20=_wfopen((directory+L"visible-020.bin").c_str(),L"rb");
        bool loaded19=f19&&old_shader.load(f19),loaded20=f20&&old_plain.load(f20);if(f19)fclose(f19);if(f20)fclose(f20);
        visible_ready=loaded19&&loaded20&&visible_index.build(old_shader,old_plain);
        wall_ready=loaded19&&loaded20&&wall_index.build_wall(old_shader,old_plain,true);
        event("\"event\":\"wall_scene_index\",\"ready\":"+std::string(wall_ready?"true":"false")+",\"retained_019_opaque_vertical_nodes\":"+std::to_string(wall_index.horizontal_nodes)+",\"020_still_plain_only\":true,\"material_name_in_scene\":false");
        event("\"event\":\"visible_scene_index\",\"ready\":"+std::string(visible_ready?"true":"false")+",\"changed_nodes\":"+std::to_string(visible_index.changed_nodes)+",\"horizontal_nodes\":"+std::to_string(visible_index.horizontal_nodes)+",\"individual_material_name_verified\":false");
        auto retained=_wfopen((directory+L"limited-caster.txt").c_str(),L"rt");if(retained){unsigned surface=0;float x=0,y=0,z=0;
            int fields=fscanf(retained,"%u %f %f %f",&surface,&x,&y,&z);fclose(retained);
            if(fields==4&&loaded20&&std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)){
                st.selected_geometry=old_plain.surface(surface);limited_caster=!st.selected_geometry.empty();st.selected=limited_caster;st.selected_only=limited_caster;retained_light={x,y,z};
                event("\"event\":\"retained_caster_loaded\",\"surface\":"+std::to_string(surface)+",\"triangles\":"+std::to_string(st.selected_geometry.size()/3)+",\"ready\":"+(limited_caster?"true":"false")+",\"selection\":\"preserved_020_not_reselected\"");}}
        st.track_writes=st.external_capture;
        if(st.track_writes){bool capture_ok=write_capture::hook(d);event("\"event\":\"write_shadow_hooks\",\"all_patches_ok\":"+std::string(capture_ok?"true":"false")+",\"source\":\"prior_successful_write_time_path\"");}}
    event("\"event\":\"shadow_hooks\",\"all_patches_ok\":"+std::string(ok?"true":"false"));}
HRESULT STDMETHODCALLTYPE create(IDirect3D9*f,UINT a,D3DDEVTYPE t,HWND w,DWORD flags,D3DPRESENT_PARAMETERS*p,IDirect3DDevice9**out){auto hr=factory(f)->create(f,a,t,w,flags,p,out);try{if(SUCCEEDED(hr)&&out&&*out)device(*out,false);}catch(...){}return hr;}
HRESULT STDMETHODCALLTYPE createex(IDirect3D9Ex*f,UINT a,D3DDEVTYPE t,HWND w,DWORD flags,D3DPRESENT_PARAMETERS*p,D3DDISPLAYMODEEX*m,IDirect3DDevice9Ex**out){auto hr=factory(f)->createex(f,a,t,w,flags,p,m,out);try{if(SUCCEEDED(hr)&&out&&*out)device(*out,true);}catch(...){}return hr;}
void hook(IDirect3D9*d,bool ex){std::lock_guard<std::recursive_mutex>lock(mutex);if(factory(d)||factories.size()>=8)return;factories.push_back({*(void***)d,nullptr,nullptr});auto&r=factories.back();patch(r.vt,16,(void*)create,(void**)&r.create);if(ex)patch(r.vt,20,(void*)createex,(void**)&r.createex);}
}
extern "C" IDirect3D9*WINAPI ShadowDirect3DCreate9(UINT sdk){auto f=proc<IDirect3D9*(WINAPI*)(UINT)>("Direct3DCreate9");auto d=f?f(sdk):nullptr;try{if(d)shadowed::hook(d,false);}catch(...){}return d;}
extern "C" HRESULT WINAPI ShadowDirect3DCreate9Ex(UINT sdk,IDirect3D9Ex**out){auto f=proc<HRESULT(WINAPI*)(UINT,IDirect3D9Ex**)>("Direct3DCreate9Ex");auto hr=f?f(sdk,out):D3DERR_NOTAVAILABLE;try{if(SUCCEEDED(hr)&&out&&*out)shadowed::hook(*out,true);}catch(...){}return hr;}
