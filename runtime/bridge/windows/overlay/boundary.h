// Private synchronous single-slot tmpfs IPC; no stale result on failure.
// The game still performs its original Z clear and HUD drawing afterwards.
namespace boundary {
struct Draw { const char* name=nullptr; HRESULT hr=E_FAIL; uint64_t frame=UINT64_MAX; };
struct Run {
    bool active=false,finished=false,clear_pending=false,applied=false,restore_failed=false,failed=false,on=false,recording=false,combat=false,scenario=false,manual=false,diagnostic_pending=false;
    unsigned frames=0,pairs=0,draws=0,skipped=0,clip=0,diagnostics=0,gaps=0;
    uint64_t boundary_frame=UINT64_MAX,draw_frame=UINT64_MAX;
    ULONGLONG begin_ms=0,clear_ms=0,present_ms=0,window_ms=0,gap_ms=0;
    unsigned phase=0; Draw last[2]{}; std::string present_kind; HRESULT read_hr=E_FAIL; bool saved=false;
    HANDLE file=INVALID_HANDLE_VALUE,mapping=nullptr; unsigned char* shared=nullptr;
    Mat scenario_view{};uint64_t scenario_view_frame=UINT64_MAX;bool scenario_view_valid=false;
} run;
constexpr size_t head=64, pix=frame_capture::pixels, bytes=head+2*pix;
enum : LONG { FREE=0,REQUEST=1,BUSY=2,RESPONSE=3,ERROR_STATE=4 };
volatile LONG* state(){return reinterpret_cast<volatile LONG*>(run.shared);}
uint32_t get(unsigned offset){uint32_t x;memcpy(&x,run.shared+offset,4);return x;}
void put(unsigned offset,uint32_t x){memcpy(run.shared+offset,&x,4);}
bool open_slot(){
    if(run.shared)return true;
    FILE*f=_wfopen((directory+L"playable-ipc-path.txt").c_str(),L"rt");wchar_t path[1024]{};
    bool ok=f&&fgetws(path,1024,f);if(f)fclose(f);if(!ok)return false;
    auto len=wcslen(path);while(len&&(path[len-1]==L'\n'||path[len-1]==L'\r'))path[--len]=0;
    run.file=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    LARGE_INTEGER size{};if(run.file==INVALID_HANDLE_VALUE||!GetFileSizeEx(run.file,&size)||size.QuadPart!=bytes)return false;
    run.mapping=CreateFileMappingW(run.file,nullptr,PAGE_READWRITE,0,0,nullptr);if(!run.mapping)return false;
    run.shared=(unsigned char*)MapViewOfFile(run.mapping,FILE_MAP_ALL_ACCESS,0,0,bytes);
    return run.shared&&get(4)==1&&InterlockedCompareExchange(state(),FREE,FREE)==FREE;
}
void trace(const char*name,HRESULT hr){
    if(!run.active||run.finished)return;
    if(run.draw_frame!=st.frame){run.draw_frame=st.frame;run.draws=0;}
    ++run.draws;run.last[0]=run.last[1];run.last[1]={name,hr,st.frame};
}
void before_clear(IDirect3DDevice9*d,DWORD flags,UINT count){
    if(!run.active||!run.on||run.finished||run.failed||run.boundary_frame==st.frame||route(d)->recording||st.disabled)return;
    auto elapsed=GetTickCount64()-run.window_ms;
    unsigned preceding=run.draw_frame==st.frame?run.draws:0;
    if(preceding<32){++run.skipped;return;}
    Ref<IDirect3DSurface9>rt,bb,cpu;D3DSURFACE_DESC desc{};D3DVIEWPORT9 vp{};
    if(FAILED(d->GetRenderTarget(0,&rt.p))||FAILED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb.p))||
       !same(rt.p,bb.p)||FAILED(bb.p->GetDesc(&desc))||FAILED(d->GetViewport(&vp))||
       vp.X||vp.Y||vp.Width!=desc.Width||vp.Height!=desc.Height||
       !frame_capture::supported(desc.Width,desc.Height,desc.Format,desc.MultiSampleType,desc.Pool,desc.Usage))return;
    run.boundary_frame=st.frame;run.clear_pending=true;run.applied=false;run.begin_ms=GetTickCount64();Guard guard;
    if(run.scenario){D3DMATRIX view{};run.scenario_view_frame=st.frame;
        run.scenario_view_valid=SUCCEEDED(d->GetTransform(D3DTS_VIEW,&view))&&shadow_core::rigid(matrix(view));
        if(run.scenario_view_valid)run.scenario_view=matrix(view);}
    auto number=std::to_string(run.pairs+1);
    unsigned diagnostic=run.manual?0:run.recording?(run.diagnostics<3&&elapsed>=3000+run.diagnostics*6000?++run.diagnostics:0):
        (elapsed>=22000&&run.clip<36&&run.diagnostics<3?++run.diagnostics:0);
    run.diagnostic_pending=diagnostic!=0;
    if(diagnostic){HRESULT h;bool saved;capture(d,"playable-world-"+number,h,saved);}
    HRESULT read=E_FAIL;bool copied=false,requested=false,valid=false;
    if(open_slot()&&InterlockedCompareExchange(state(),FREE,FREE)==FREE&&
       SUCCEEDED(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&cpu.p,nullptr))&&cpu.p){
        read=d->GetRenderTargetData(bb.p,cpu.p);D3DLOCKED_RECT rect{};
        if(SUCCEEDED(read)&&SUCCEEDED(cpu.p->LockRect(&rect,nullptr,D3DLOCK_READONLY))){
            copied=rect.pBits&&frame_capture::pitch_ok(rect.Pitch);
            if(copied)for(unsigned y=0;y<desc.Height;++y)memcpy(run.shared+head+size_t(y)*frame_capture::row_bytes,
                (unsigned char*)rect.pBits+size_t(y)*rect.Pitch,frame_capture::row_bytes);
            copied=SUCCEEDED(cpu.p->UnlockRect())&&copied;
        }
    }
    ULONGLONG captured=GetTickCount64();
    if(copied){put(8,run.pairs+1);put(12,pix);put(16,0);put(20,0);put(28,diagnostic);
        InterlockedExchange(state(),REQUEST);requested=true;}
    ULONGLONG wait_start=GetTickCount64();LONG value=FREE;
    while(requested&&(value=InterlockedCompareExchange(state(),FREE,FREE))!=RESPONSE&&value!=ERROR_STATE&&
          GetTickCount64()-wait_start<5000&&GetFileAttributesW((directory+L"live-ipc\\worker-error.txt").c_str())==INVALID_FILE_ATTRIBUTES)Sleep(1);
    ULONGLONG returned=GetTickCount64();
    valid=requested&&value==RESPONSE&&get(4)==1&&get(16)==run.pairs+1&&get(20)==pix;
    Ref<IDirect3DTexture9> texture;D3DLOCKED_RECT bits{};bool uploaded=false;
    if(valid&&SUCCEEDED(d->CreateTexture(desc.Width,desc.Height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture.p,nullptr))&&texture.p&&
       SUCCEEDED(texture.p->LockRect(0,&bits,nullptr,0))){
        if(bits.pBits&&frame_capture::pitch_ok(bits.Pitch)){
            for(unsigned y=0;y<desc.Height;++y)memcpy((unsigned char*)bits.pBits+size_t(y)*bits.Pitch,
                run.shared+head+pix+size_t(y)*frame_capture::row_bytes,frame_capture::row_bytes);
            uploaded=true;
        }
        uploaded=SUCCEEDED(texture.p->UnlockRect(0))&&uploaded;
    }
    if(valid)InterlockedExchange(state(),FREE); // only after the complete copy
    ULONGLONG uploaded_at=GetTickCount64();
    hybrid::State state_snapshot(d);bool snapshot=uploaded&&state_snapshot.capture(),ok=snapshot,restored=false;HRESULT draw=E_FAIL;
    if(snapshot){
        ok &= SUCCEEDED(d->SetVertexShader(nullptr))&&SUCCEEDED(d->SetPixelShader(nullptr));
        ok &= SUCCEEDED(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
        ok &= SUCCEEDED(d->SetTexture(0,texture.p))&&SUCCEEDED(d->SetTexture(1,nullptr));
        ok &= SUCCEEDED(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));
        ok &= SUCCEEDED(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE));
        ok &= SUCCEEDED(d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));
        ok &= SUCCEEDED(d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE));
        ok &= SUCCEEDED(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));
        ok &= SUCCEEDED(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT))&&SUCCEEDED(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
        ok &= SUCCEEDED(d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP))&&SUCCEEDED(d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
        const std::pair<D3DRENDERSTATETYPE,DWORD> states[]={
            {D3DRS_ZENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},
            {D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},{D3DRS_FOGENABLE,FALSE},
            {D3DRS_LIGHTING,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_FILLMODE,D3DFILL_SOLID},
            {D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_COLORWRITEENABLE,7}};
        for(auto s:states)ok &= SUCCEEDED(d->SetRenderState(s.first,s.second));
        struct V{float x,y,z,rhw,u,v;};
        V q[]={{-.5f,-.5f,0,1,0,0},{float(desc.Width)-.5f,-.5f,0,1,1,0},
               {-.5f,float(desc.Height)-.5f,0,1,0,1},{float(desc.Width)-.5f,float(desc.Height)-.5f,0,1,1,1}};
        if(ok)draw=find(d)->dpu(d,D3DPT_TRIANGLESTRIP,2,q,sizeof(V));
        restored=state_snapshot.restore();
    }
    run.applied=ok&&SUCCEEDED(draw)&&restored;run.failed=!run.applied;
    if(snapshot&&!restored){run.restore_failed=true;st.disabled=true;}
    event("\"event\":\"boundary_composite\",\"frame\":"+std::to_string(st.frame)+
          ",\"pair\":"+number+",\"flags\":"+std::to_string(flags)+",\"rect_count\":"+std::to_string(count)+
          ",\"diagnostic\":"+std::to_string(diagnostic)+",\"B_read_hr\":"+std::to_string(read)+
          ",\"request_sent\":"+(requested?"true":"false")+",\"output_valid\":"+(valid?"true":"false")+
          ",\"uploaded\":"+(uploaded?"true":"false")+",\"state_captured\":"+(snapshot?"true":"false")+
          ",\"draw_hr\":"+std::to_string(draw)+",\"restored\":"+(restored?"true":"false")+
          ",\"applied\":"+(run.applied?"true":"false")+",\"readback_ms\":"+std::to_string(captured-run.begin_ms)+
          ",\"request_wait_ms\":"+std::to_string(returned-wait_start)+",\"upload_ms\":"+std::to_string(uploaded_at-returned)+
          ",\"draw_restore_ms\":"+std::to_string(GetTickCount64()-uploaded_at));
}
void after_clear(HRESULT hr){if(!run.clear_pending)return;run.clear_pending=false;run.clear_ms=GetTickCount64();
    event("\"event\":\"boundary_original_clear\",\"frame\":"+std::to_string(st.frame)+",\"pair\":"+std::to_string(run.pairs+1)+",\"original_hr\":"+std::to_string(hr));}
void checkpoint_pose(const std::string&label){
    if(!run.scenario||label.rfind("request-",0)!=0)return;
    bool valid=run.scenario_view_valid&&run.scenario_view_frame==st.frame;
    std::ostringstream row;row<<"{\"frame\":"<<st.frame<<",\"tick_ms\":"<<GetTickCount64()<<",\"valid\":"<<(valid?"true":"false");
    if(valid){auto eye=shadow_core::transform({0,0,0,1},shadow_core::inverse_rigid(run.scenario_view));
        row<<",\"eye\":["<<eye[0]<<','<<eye[1]<<','<<eye[2]<<"],\"forward\":["<<run.scenario_view[2]<<','<<run.scenario_view[6]<<','<<run.scenario_view[10]<<']';}
    row<<'}';auto name=std::string("backbuffer-")+label+"-pose.json";
    auto path=directory+std::wstring(name.begin(),name.end());FILE*f=_wfopen(path.c_str(),L"wbx");
    if(f){auto data=row.str();fwrite(data.data(),1,data.size(),f);fclose(f);}
}
void pre_present(IDirect3DDevice9*d,bool checkpoint_capture=false){
    if(run.finished)return;
    if(!run.active){if(GetFileAttributesW((directory+L"boundary-start.txt").c_str())==INVALID_FILE_ATTRIBUTES)return;
        FILE*mode=_wfopen((directory+L"playable-mode.txt").c_str(),L"rt");char value[16]{};
        bool ok=mode&&fgets(value,sizeof(value),mode);if(mode)fclose(mode);
        if(!ok||(strncmp(value,"on\n",3)&&strncmp(value,"off\n",4))){run.failed=true;}
        run.on=!strncmp(value,"on\n",3);run.recording=GetFileAttributesW((directory+L"recording-mode.txt").c_str())!=INVALID_FILE_ATTRIBUTES;
        run.manual=GetFileAttributesW((directory+L"manual-boundary.txt").c_str())!=INVALID_FILE_ATTRIBUTES;
        run.combat=run.recording&&GetFileAttributesW((directory+L"combat-mode.txt").c_str())!=INVALID_FILE_ATTRIBUTES;
        run.scenario=run.recording&&GetFileAttributesW((directory+L"scenario-mode.txt").c_str())!=INVALID_FILE_ATTRIBUTES;
        run.active=true;run.window_ms=GetTickCount64();
        event("\"event\":\"boundary_window_started\",\"frame\":"+std::to_string(st.frame)+",\"mode\":\""+(run.on?"on":"off")+"\",\"recipe\":\""+(run.manual?"manual-v1":run.recording?"recording-v1":"followup-v1")+"\"");}
    run.phase=checkpoint_capture?2:run.on?1:0;
    run.present_kind.clear();run.saved=false;run.read_hr=E_FAIL;
    if(run.boundary_frame==st.frame&&run.applied){run.present_kind="C-"+std::to_string(run.pairs+1);
        if(!run.manual&&((run.recording&&run.diagnostic_pending)||(!run.recording&&GetTickCount64()-run.window_ms>=22000&&run.clip<36))){
            run.phase=2;capture(d,"boundary-"+run.present_kind,run.read_hr,run.saved);if(run.saved)++run.clip;}}
    else if(!run.manual&&run.on&&!run.failed&&run.pairs>=20&&run.gaps<3&&(!run.gaps||GetTickCount64()-run.gap_ms>=500)){
        run.phase=2;run.gap_ms=GetTickCount64();++run.gaps;HRESULT gap_hr=E_FAIL;bool gap_saved=false;
        capture(d,"playable-gap-"+std::to_string(st.frame),gap_hr,gap_saved);
        event("\"event\":\"boundary_gap_capture\",\"frame\":"+std::to_string(st.frame)+
              ",\"after_pair\":"+std::to_string(run.pairs)+",\"sample\":"+std::to_string(run.gaps)+
              ",\"elapsed_ms\":"+std::to_string(GetTickCount64()-run.window_ms)+
              ",\"draws\":"+std::to_string(run.draw_frame==st.frame?run.draws:0)+
              ",\"read_hr\":"+std::to_string(gap_hr)+",\"saved\":"+(gap_saved?"true":"false"));}
    run.present_ms=GetTickCount64();
}
void post_present(HRESULT hr){
    if(!run.active||run.finished)return;++run.frames;
    auto end=GetTickCount64();
    event("\"event\":\"boundary_present\",\"kind\":\""+run.present_kind+"\",\"frame\":"+std::to_string(st.frame)+
          ",\"phase\":"+std::to_string(run.phase)+",\"saved\":"+(run.saved?"true":"false")+
          ",\"draws\":"+std::to_string(run.draw_frame==st.frame?run.draws:0)+
          ",\"original_present_hr\":"+std::to_string(hr)+",\"pre_present_tick_ms\":"+std::to_string(run.present_ms)+
          ",\"present_end_tick_ms\":"+std::to_string(end));
    if(!run.present_kind.empty())++run.pairs;
    // The recording-only combat trial is bounded at 50 s. A lost gameplay
    // screen stops it early; the ordinary OFF/ON recipe remains 40 s.
    bool controller_stop=(run.manual||run.recording)&&GetFileAttributesW((directory+L"boundary-stop.txt").c_str())!=INVALID_FILE_ATTRIBUTES;
    // Windows private overlay only: allow one bounded three-minute manual take.
    if(end-run.window_ms>=(run.manual?200000:run.scenario?60000:run.combat?50000:40000)||controller_stop||run.restore_failed||run.failed){run.finished=true;
        event("\"event\":\"boundary_window_finished\",\"frames\":"+std::to_string(run.frames)+",\"pairs\":"+std::to_string(run.pairs)+
              ",\"clip\":"+std::to_string(run.clip)+",\"gaps\":"+std::to_string(run.gaps)+",\"elapsed_ms\":"+std::to_string(end-run.window_ms)+
              ",\"mode\":\""+(run.on?"on":"off")+"\""+
              ",\"failed\":"+(run.failed?"true":"false")+",\"restore_failed\":"+(run.restore_failed?"true":"false"));
        FILE*f=_wfopen((directory+L"boundary-done.txt").c_str(),L"wbx");if(f)fclose(f);}
}
}
