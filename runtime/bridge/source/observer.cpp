// Observation-only derivative of the fixed upstream load_real / factory /
// device-vtable seam. No upstream core, menu, helper or rendering path is linked.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <climits>
#include "observation_core.h"
static_assert(sizeof(void*) == 4, "KF1 needs PE32");

namespace {
std::wstring directory;
HMODULE real_dll;
FILE* journal;
ULONGLONG origin;
std::recursive_mutex mutex;
observation::Gate gate;
bool armed = false, finished = false;
uint64_t detail_id = 0;
IDirect3DDevice9* capture_device=nullptr;

void event(const std::string& body) {
    if (!journal) return;
    fprintf(journal, "{\"ms_since_attach\":%llu,%s}\n",
            (unsigned long long)(GetTickCount64()-origin), body.c_str());
    fflush(journal);
}
std::string ptr(const void* p) { std::ostringstream s; s << '"' << p << '"'; return s.str(); }
void floats(std::ostream& s, const float* p, unsigned n) {
    s << '[';
    for (unsigned i=0;i<n;++i) { if(i) s << ','; if(std::isfinite(p[i])) s << std::setprecision(9) << p[i]; else s << "null"; }
    s << ']';
}
bool readable(const void* ptr, size_t length) {
    uintptr_t a = reinterpret_cast<uintptr_t>(ptr);
    if (!a || length > 512 || a+length < a) return false;
    const uintptr_t end = a+length;
    while (a < end) {
        MEMORY_BASIC_INFORMATION m{};
        if (!VirtualQuery((void*)a,&m,sizeof(m)) || m.State != MEM_COMMIT ||
            (m.Protect & (PAGE_GUARD|PAGE_NOACCESS)) ||
            !(m.Protect & (PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) return false;
        uintptr_t next = reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;
        if(next<=a) return false;
        a=next;
    }
    return true;
}
// Read-only locks are attempted only for non-write-only, non-dynamic,
// non-default-pool buffers. Failed locks and unlock HRESULTs are retained.
bool eligible(DWORD usage,D3DPOOL pool) {
    return !(usage & (D3DUSAGE_WRITEONLY|D3DUSAGE_DYNAMIC)) && pool != D3DPOOL_DEFAULT;
}
struct Layout { bool spatial=false; int pos=-1,normal=-1; DWORD fvf=0; HRESULT fvf_hr,decl_hr; std::string declaration; };
Layout layout(IDirect3DDevice9* d) {
    Layout l; l.fvf_hr=d->GetFVF(&l.fvf);
    IDirect3DVertexDeclaration9* decl=nullptr;
    l.decl_hr=d->GetVertexDeclaration(&decl);
    std::ostringstream s; s << '[';
    if (SUCCEEDED(l.decl_hr) && decl) {
        D3DVERTEXELEMENT9 e[MAXD3DDECLLENGTH+1]{}; UINT n=MAXD3DDECLLENGTH+1;
        HRESULT hr=decl->GetDeclaration(e,&n);
        if(SUCCEEDED(hr) && n<=MAXD3DDECLLENGTH+1) for(UINT i=0;i<n && e[i].Stream!=0xff;++i) {
            if(i) s << ',';
            s << "{\"stream\":" << e[i].Stream << ",\"offset\":" << e[i].Offset
              << ",\"type\":" << unsigned(e[i].Type) << ",\"usage\":" << unsigned(e[i].Usage)
              << ",\"usage_index\":" << unsigned(e[i].UsageIndex) << '}';
            if(e[i].Usage==D3DDECLUSAGE_POSITION && e[i].UsageIndex==0 &&
               (e[i].Type==D3DDECLTYPE_FLOAT3 || e[i].Type==D3DDECLTYPE_FLOAT4)) {
                l.spatial=true; if(e[i].Stream==0) l.pos=e[i].Offset;
            }
            if(e[i].Usage==D3DDECLUSAGE_NORMAL && e[i].UsageIndex==0 && e[i].Stream==0 && e[i].Type==D3DDECLTYPE_FLOAT3) l.normal=e[i].Offset;
        }
        decl->Release();
    }
    s << ']'; l.declaration=s.str();
    // FVF-derived declarations are exposed by D3D9 too. POSITIONT/XYZRHW is
    // never a spatial candidate, even if a neighboring declaration is present.
    DWORD p=l.fvf&D3DFVF_POSITION_MASK;
    if (SUCCEEDED(l.fvf_hr) && l.fvf) {
        if(p==D3DFVF_XYZRHW) { l.spatial=false; l.pos=-1; }
        else if(p==D3DFVF_XYZ) { l.spatial=true; l.pos=0; if(l.fvf&D3DFVF_NORMAL) l.normal=12; }
    }
    return l;
}
typedef HRESULT (STDMETHODCALLTYPE *DP)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT);
typedef HRESULT (STDMETHODCALLTYPE *DIP)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);
typedef HRESULT (STDMETHODCALLTYPE *DPU)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT);
typedef HRESULT (STDMETHODCALLTYPE *DIPU)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT);
typedef HRESULT (STDMETHODCALLTYPE *PRESENT)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
typedef HRESULT (STDMETHODCALLTYPE *PRESENTEX)(IDirect3DDevice9Ex*,const RECT*,const RECT*,HWND,const RGNDATA*,DWORD);
typedef HRESULT (STDMETHODCALLTYPE *RESET)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
typedef HRESULT (STDMETHODCALLTYPE *RESETEX)(IDirect3DDevice9Ex*,D3DPRESENT_PARAMETERS*,D3DDISPLAYMODEEX*);
struct Device { void** vt; DP dp; DIP dip; DPU dpu; DIPU dipu; PRESENT present; PRESENTEX presentex; RESET reset; RESETEX resetex; uint64_t frame=0,counts[4]={},total[4]={}; ULONGLONG last_heartbeat=0; bool selected=false; };
std::vector<Device> devices;
Device* find(IDirect3DDevice9* d) { for(auto& o:devices) if(o.vt==*(void***)d) return &o; return nullptr; }

struct Args { unsigned api; D3DPRIMITIVETYPE type; int base=0; unsigned min=0,vertices=0,start=0,primitives=0,stride=0; const void* vp=nullptr; const void* ip=nullptr; D3DFORMAT format=D3DFMT_UNKNOWN; };
std::string capture(IDirect3DDevice9* d,Device& o,const Args& a) {
    ++o.counts[a.api];
    ++o.total[a.api];
    if(!armed || finished || gate.draws>=32 || (capture_device && capture_device!=d)) return {};
    Layout l=layout(d);
    if(!gate.take(armed,l.spatial)) return {};
    capture_device=d;
    o.selected=true;
    std::ostringstream s;
    s << "\"event\":\"draw\",\"id\":" << ++detail_id << ",\"device\":" << ptr(d)
      << ",\"frame\":" << o.frame << ",\"api\":" << a.api << ",\"type\":" << a.type
      << ",\"base\":" << a.base << ",\"min_vertex\":" << a.min << ",\"num_vertices\":" << a.vertices
      << ",\"start\":" << a.start << ",\"primitive_count\":" << a.primitives
      << ",\"fvf_hr\":" << l.fvf_hr << ",\"fvf\":" << l.fvf << ",\"declaration_hr\":" << l.decl_hr
      << ",\"declaration\":" << l.declaration << ",\"position_stream0_float3_offset\":" << l.pos
      << ",\"normal_stream0_float3_offset\":" << l.normal;
    IDirect3DVertexShader9* vs=nullptr; HRESULT vs_hr=d->GetVertexShader(&vs);
    s << ",\"vertex_shader_hr\":" << vs_hr << ",\"vertex_shader\":" << ptr(vs);
    if(SUCCEEDED(vs_hr) && !vs) {
        const D3DTRANSFORMSTATETYPE kinds[]={D3DTS_WORLD,D3DTS_VIEW,D3DTS_PROJECTION};
        s << ",\"fixed_function_transforms\":[";
        for(unsigned i=0;i<3;++i) { if(i)s << ','; D3DMATRIX m{}; HRESULT hr=d->GetTransform(kinds[i],&m);
            s << "{\"state\":" << kinds[i] << ",\"hr\":" << hr << ",\"values\":";
            if(SUCCEEDED(hr)) floats(s,&m.m[0][0],16); else s << "null"; s << '}'; }
        s << ']';
    } else if(vs) {
        float c[64]{}; HRESULT hr=d->GetVertexShaderConstantF(0,c,16);
        s << ",\"unverified_vs_constants_c0_c15_hr\":" << hr << ",\"unverified_vs_constants_c0_c15\":";
        if(SUCCEEDED(hr)) floats(s,c,64); else s << "null";
    }
    if(vs)vs->Release();
    s << ",\"render_states\":[";
    D3DRENDERSTATETYPE states[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_LIGHTING,D3DRS_CULLMODE};
    for(unsigned i=0;i<5;++i) { if(i)s << ','; DWORD v=0; HRESULT hr=d->GetRenderState(states[i],&v); s << "{\"state\":" << states[i] << ",\"hr\":" << hr << ",\"value\":" << v << '}'; }
    s << ']';
    D3DVIEWPORT9 viewport{}; HRESULT vhr=d->GetViewport(&viewport);
    s << ",\"viewport_hr\":" << vhr << ",\"viewport\":[" << viewport.X << ',' << viewport.Y << ',' << viewport.Width << ',' << viewport.Height << ',' << viewport.MinZ << ',' << viewport.MaxZ << ']';
    IDirect3DSurface9* depth=nullptr; HRESULT dh=d->GetDepthStencilSurface(&depth);
    s << ",\"depth_surface_hr\":" << dh << ",\"depth_surface\":" << ptr(depth) << ",\"depth_pixels\":null";
    if(depth) { D3DSURFACE_DESC desc{}; HRESULT hr=depth->GetDesc(&desc); s << ",\"depth_desc_hr\":" << hr << ",\"depth_format\":" << desc.Format << ",\"depth_size\":[" << desc.Width << ',' << desc.Height << ']'; depth->Release(); }
    IDirect3DVertexBuffer9* vb=nullptr; UINT offset=0,stride=a.stride; D3DVERTEXBUFFER_DESC vd{};
    HRESULT stream_hr=E_NOTIMPL,vdhr=E_NOTIMPL;
    if(a.api<2) { stream_hr=d->GetStreamSource(0,&vb,&offset,&stride); if(vb)vdhr=vb->GetDesc(&vd); }
    s << ",\"stream0_hr\":" << stream_hr << ",\"vb\":" << ptr(vb) << ",\"offset\":" << offset << ",\"stride\":" << stride << ",\"vb_desc_hr\":" << vdhr
      << ",\"vb_bytes\":" << vd.Size << ",\"vb_usage\":" << vd.Usage << ",\"vb_pool\":" << vd.Pool;
    unsigned indices[3]={a.start,a.start+1,a.start+2}; unsigned count=unsigned((std::min)(uint64_t(3),observation::element_count(a.type,a.primitives)));
    IDirect3DIndexBuffer9* ib=nullptr; D3DINDEXBUFFER_DESC id{}; HRESULT ihr=E_NOTIMPL,idh=E_NOTIMPL,lockhr=E_NOTIMPL,unlockhr=E_NOTIMPL;
    bool index_valid=a.api==0 || a.api==2;
    unsigned char indexbytes[12]{}; unsigned indexn=0;
    if(a.api==1 || a.api==3) {
        D3DFORMAT fmt=a.format; const void* src=nullptr; void* locked=nullptr;
        if(a.api==1) { ihr=d->GetIndices(&ib); if(ib) { idh=ib->GetDesc(&id); fmt=id.Format; } }
        const unsigned width=fmt==D3DFMT_INDEX16?2:fmt==D3DFMT_INDEX32?4:0;
        indexn=count*width;
        if(width && indexn) {
            if(a.api==3) { if(readable(a.ip,indexn))src=a.ip; }
            else if(ib && SUCCEEDED(idh) && eligible(id.Usage,id.Pool) && observation::span(id.Size,uint64_t(a.start)*width,indexn)) {
                lockhr=ib->Lock(a.start*width,indexn,&locked,D3DLOCK_READONLY); if(SUCCEEDED(lockhr))src=locked;
            }
            if(src) { memcpy(indexbytes,src,indexn); index_valid=true;
                for(unsigned i=0;i<count;++i) { indices[i]=0; memcpy(&indices[i],indexbytes+i*width,width); } }
            if(locked)unlockhr=ib->Unlock();
        }
        s << ",\"ib_hr\":" << ihr << ",\"ib\":" << ptr(ib) << ",\"ib_desc_hr\":" << idh << ",\"ib_bytes\":" << id.Size
          << ",\"ib_usage\":" << id.Usage << ",\"ib_pool\":" << id.Pool << ",\"index_format\":" << fmt
          << ",\"index_lock_hr\":" << lockhr << ",\"index_unlock_hr\":" << unlockhr;
    }
    s << ",\"actual_indices\":";
    if((a.api==1 || a.api==3) && index_valid) { s << '['; for(unsigned i=0;i<count;++i){if(i)s << ',';s << indices[i];}s << ']'; } else s << "null";
    s << ",\"vertex_samples\":[";
    for(unsigned i=0;i<count && index_valid;++i) {
        if(i)s << ',';
        uint64_t at=0; unsigned char bytes[128]{}; bool actual=false;
        HRESULT lh=E_NOTIMPL,uh=E_NOTIMPL;
        bool range = !(a.api==1 || a.api==3) || (uint64_t(indices[i])>=a.min && uint64_t(indices[i])-a.min<a.vertices);
        uint64_t size=a.api>=2 ? uint64_t(UINT_MAX) : vd.Size;
        if(range && observation::vertex_offset(size,offset,stride,a.base,indices[i],at)) {
            if(a.api>=2) {
                uintptr_t base=reinterpret_cast<uintptr_t>(a.vp);
                if(base && at<=UINTPTR_MAX-base && readable((const void*)(base+at),stride)) { memcpy(bytes,(const void*)(base+at),stride); actual=true; }
            } else if(vb && SUCCEEDED(vdhr) && eligible(vd.Usage,vd.Pool)) {
                void* data=nullptr; lh=vb->Lock(UINT(at),stride,&data,D3DLOCK_READONLY);
                if(SUCCEEDED(lh) && data) { memcpy(bytes,data,stride); actual=true; }
                if(SUCCEEDED(lh))uh=vb->Unlock();
            }
        }
        s << "{\"index\":" << indices[i] << ",\"byte_offset\":" << at << ",\"actual\":" << (actual?"true":"false")
          << ",\"lock_hr\":" << lh << ",\"unlock_hr\":" << uh << ",\"hex\":\"";
        if(actual) for(unsigned j=0;j<stride;++j)s << std::hex << std::setw(2) << std::setfill('0') << unsigned(bytes[j]);
        s << std::dec << "\",\"position\":";
        if(actual && l.pos>=0 && observation::span(stride,l.pos,12)) { float p[3];memcpy(p,bytes+l.pos,12);floats(s,p,3); }else s << "null";
        s << ",\"normal\":";
        if(actual && l.normal>=0 && observation::span(stride,l.normal,12)) { float p[3];memcpy(p,bytes+l.normal,12);floats(s,p,3); }else s << "null";
        s << '}';
    }
    s << ']';
    if(vb)vb->Release(); if(ib)ib->Release();
    return s.str();
}
std::string before(IDirect3DDevice9* d,const Args& a) noexcept {
    try { std::lock_guard<std::recursive_mutex> lock(mutex); return capture(d,*find(d),a); }
    catch(...) { return {}; }
}
void after(std::string& c,HRESULT hr) noexcept { try { if(!c.empty()){std::lock_guard<std::recursive_mutex> lock(mutex);event(c+",\"original_draw_hr\":"+std::to_string(hr));} }catch(...){} }
HRESULT STDMETHODCALLTYPE dp(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT start,UINT n){Args a{0,t};a.start=start;a.primitives=n;auto c=before(d,a);auto hr=observation::forward_once(find(d)->dp,d,t,start,n);after(c,hr);return hr;}
HRESULT STDMETHODCALLTYPE dip(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,INT b,UINT m,UINT v,UINT start,UINT n){Args a{1,t};a.base=b;a.min=m;a.vertices=v;a.start=start;a.primitives=n;auto c=before(d,a);auto hr=observation::forward_once(find(d)->dip,d,t,b,m,v,start,n);after(c,hr);return hr;}
HRESULT STDMETHODCALLTYPE dpu(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT n,const void*p,UINT stride){Args a{2,t};a.primitives=n;a.vp=p;a.stride=stride;auto c=before(d,a);auto hr=observation::forward_once(find(d)->dpu,d,t,n,p,stride);after(c,hr);return hr;}
HRESULT STDMETHODCALLTYPE dipu(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT m,UINT v,UINT n,const void*i,D3DFORMAT fmt,const void*p,UINT stride){Args a{3,t};a.min=m;a.vertices=v;a.primitives=n;a.ip=i;a.format=fmt;a.vp=p;a.stride=stride;auto c=before(d,a);auto hr=observation::forward_once(find(d)->dipu,d,t,m,v,n,i,fmt,p,stride);after(c,hr);return hr;}
void presented(IDirect3DDevice9*d,HRESULT hr) noexcept {
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex); auto* o=find(d);
        if(!o->frame || GetTickCount64()-o->last_heartbeat>=1000) {
            std::ostringstream s;s << "\"event\":\"present_heartbeat\",\"device\":" << ptr(d)
                << ",\"frame\":" << o->frame << ",\"original_present_hr\":" << hr << ",\"draw_entry_totals\":[";
            for(int i=0;i<4;++i){if(i)s << ',';s << o->total[i];}s << ']';event(s.str());o->last_heartbeat=GetTickCount64();
        }
        if(o->selected) {
            std::ostringstream s; s << "\"event\":\"target_present\",\"device\":" << ptr(d) << ",\"frame\":" << o->frame << ",\"original_present_hr\":" << hr << ",\"draw_counts\":[";
            for(int i=0;i<4;++i){if(i)s << ',';s << o->counts[i];}s << ']';event(s.str());
            o->selected=false;gate.present();
            if(gate.done()) { finished=true; event("\"event\":\"capture_finished\",\"detailed_draws\":"+std::to_string(gate.draws)+",\"target_frames\":"+std::to_string(gate.frames)); }
        }
        ++o->frame; memset(o->counts,0,sizeof(o->counts));
        if(!armed && !finished && GetFileAttributesW((directory+L"arm.txt").c_str())!=INVALID_FILE_ATTRIBUTES) {
            armed=true;event("\"event\":\"armed_by_private_map_log\"");
        }
        if(o->frame==1) event("\"event\":\"first_present\",\"device\":"+ptr(d)+",\"original_present_hr\":"+std::to_string(hr));
    }catch(...){}
}
HRESULT STDMETHODCALLTYPE present(IDirect3DDevice9*d,const RECT*a,const RECT*b,HWND c,const RGNDATA*e){auto hr=observation::forward_once(find(d)->present,d,a,b,c,e);presented(d,hr);return hr;}
HRESULT STDMETHODCALLTYPE presentex(IDirect3DDevice9Ex*d,const RECT*a,const RECT*b,HWND c,const RGNDATA*e,DWORD f){auto hr=observation::forward_once(find(d)->presentex,d,a,b,c,e,f);presented(d,hr);return hr;}
HRESULT STDMETHODCALLTYPE reset(IDirect3DDevice9*d,D3DPRESENT_PARAMETERS*p){auto hr=observation::forward_once(find(d)->reset,d,p);try{std::lock_guard<std::recursive_mutex>l(mutex);event("\"event\":\"reset\",\"hr\":"+std::to_string(hr));}catch(...){}return hr;}
HRESULT STDMETHODCALLTYPE resetex(IDirect3DDevice9Ex*d,D3DPRESENT_PARAMETERS*p,D3DDISPLAYMODEEX*m){auto hr=observation::forward_once(find(d)->resetex,d,p,m);return hr;}
bool patch(void** vt,int index,void* hook,void** original) {
    DWORD old=0; if(!VirtualProtect(vt+index,sizeof(void*),PAGE_EXECUTE_READWRITE,&old))return false;
    *original=vt[index]; vt[index]=hook; DWORD ignored; VirtualProtect(vt+index,sizeof(void*),old,&ignored); return true;
}
void hook_device(IDirect3DDevice9*d,bool ex) {
    std::lock_guard<std::recursive_mutex> lock(mutex); if(find(d))return;
    Device o{};o.vt=*(void***)d;
    // Publish originals before any hook is visible. Fixed capacity is reserved.
    devices.push_back(o);auto& x=devices.back();
    bool ok=true;
    ok&=patch(x.vt,81,(void*)dp,(void**)&x.dp);ok&=patch(x.vt,82,(void*)dip,(void**)&x.dip);
    ok&=patch(x.vt,83,(void*)dpu,(void**)&x.dpu);ok&=patch(x.vt,84,(void*)dipu,(void**)&x.dipu);
    ok&=patch(x.vt,17,(void*)present,(void**)&x.present);ok&=patch(x.vt,16,(void*)reset,(void**)&x.reset);
    if(ex){ok&=patch(x.vt,121,(void*)presentex,(void**)&x.presentex);ok&=patch(x.vt,132,(void*)resetex,(void**)&x.resetex);}
    event("\"event\":\"hook_device\",\"device\":"+ptr(d)+",\"all_patches_ok\":"+(ok?"true":"false"));
}
using CreateDevice=HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**);
using CreateDeviceEx=HRESULT(STDMETHODCALLTYPE*)(IDirect3D9Ex*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,D3DDISPLAYMODEEX*,IDirect3DDevice9Ex**);
struct Factory {void** vt;CreateDevice create;CreateDeviceEx createex;};std::vector<Factory> factories;
Factory* factory(void*d){for(auto&f:factories)if(f.vt==*(void***)d)return &f;return nullptr;}
HRESULT STDMETHODCALLTYPE create(IDirect3D9*f,UINT a,D3DDEVTYPE t,HWND w,DWORD flags,D3DPRESENT_PARAMETERS*p,IDirect3DDevice9**out){auto hr=observation::forward_once(factory(f)->create,f,a,t,w,flags,p,out);try{event("\"event\":\"create_device\",\"hr\":"+std::to_string(hr)+",\"behavior_flags\":"+std::to_string(flags));if(SUCCEEDED(hr)&&out&&*out)hook_device(*out,false);}catch(...){}return hr;}
HRESULT STDMETHODCALLTYPE createex(IDirect3D9Ex*f,UINT a,D3DDEVTYPE t,HWND w,DWORD flags,D3DPRESENT_PARAMETERS*p,D3DDISPLAYMODEEX*m,IDirect3DDevice9Ex**out){auto hr=observation::forward_once(factory(f)->createex,f,a,t,w,flags,p,m,out);try{if(SUCCEEDED(hr)&&out&&*out)hook_device(*out,true);}catch(...){}return hr;}
void hook_factory(IDirect3D9*d,bool ex){std::lock_guard<std::recursive_mutex>lock(mutex);if(factory(d))return;factories.push_back({*(void***)d,nullptr,nullptr});auto& f=factories.back();patch(f.vt,16,(void*)create,(void**)&f.create);if(ex)patch(f.vt,20,(void*)createex,(void**)&f.createex);}
bool load_real(){
    std::lock_guard<std::recursive_mutex> lock(mutex);if(real_dll)return true;
    real_dll=LoadLibraryW((directory+L"d3d9_chain.dll").c_str());
    // Fail closed on missing selected chain: no hidden backend substitution.
    event("\"event\":\"load_real_chain\",\"ok\":"+std::string(real_dll?"true":"false"));return real_dll!=nullptr;
}
template<class F>F proc(const char*n){return load_real()?(F)GetProcAddress(real_dll,n):nullptr;}
}
extern "C" {
IDirect3D9* WINAPI Direct3DCreate9(UINT sdk){auto f=proc<IDirect3D9*(WINAPI*)(UINT)>("Direct3DCreate9");auto d=f?f(sdk):nullptr;if(d)hook_factory(d,false);return d;}
HRESULT WINAPI Direct3DCreate9Ex(UINT sdk,IDirect3D9Ex**out){auto f=proc<HRESULT(WINAPI*)(UINT,IDirect3D9Ex**)>("Direct3DCreate9Ex");auto hr=f?f(sdk,out):D3DERR_NOTAVAILABLE;if(SUCCEEDED(hr)&&out&&*out)hook_factory(*out,true);return hr;}
int WINAPI D3DPERF_BeginEvent(D3DCOLOR c,LPCWSTR n){auto f=proc<int(WINAPI*)(D3DCOLOR,LPCWSTR)>("D3DPERF_BeginEvent");return f?f(c,n):0;}
int WINAPI D3DPERF_EndEvent(){auto f=proc<int(WINAPI*)()>("D3DPERF_EndEvent");return f?f():0;}
DWORD WINAPI D3DPERF_GetStatus(){auto f=proc<DWORD(WINAPI*)()>("D3DPERF_GetStatus");return f?f():0;}
BOOL WINAPI D3DPERF_QueryRepeatFrame(){auto f=proc<BOOL(WINAPI*)()>("D3DPERF_QueryRepeatFrame");return f?f():FALSE;}
void WINAPI D3DPERF_SetMarker(D3DCOLOR c,LPCWSTR n){auto f=proc<void(WINAPI*)(D3DCOLOR,LPCWSTR)>("D3DPERF_SetMarker");if(f)f(c,n);}
void WINAPI D3DPERF_SetRegion(D3DCOLOR c,LPCWSTR n){auto f=proc<void(WINAPI*)(D3DCOLOR,LPCWSTR)>("D3DPERF_SetRegion");if(f)f(c,n);}
void WINAPI D3DPERF_SetOptions(DWORD o){auto f=proc<void(WINAPI*)(DWORD)>("D3DPERF_SetOptions");if(f)f(o);}
void WINAPI Direct3D9EnableMaximizedWindowedModeShim(BOOL o){auto f=proc<void(WINAPI*)(BOOL)>("Direct3D9EnableMaximizedWindowedModeShim");if(f)f(o);}
}
BOOL WINAPI DllMain(HINSTANCE self,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(self);origin=GetTickCount64();wchar_t path[32768];DWORD n=GetModuleFileNameW(self,path,32768);if(!n||n>=32768)return FALSE;directory=std::wstring(path,n);directory.resize(directory.find_last_of(L"\\/")+1);journal=_wfopen((directory+L"draw-events.jsonl").c_str(),L"wbx");devices.reserve(16);factories.reserve(8);event("\"event\":\"attach\",\"nr_helper_ai\":false");}
    // Do not perform loader work or resource teardown at forced process exit.
    return TRUE;
}
