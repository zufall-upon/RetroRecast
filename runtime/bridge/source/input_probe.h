// SPDX-License-Identifier: GPL-3.0-or-later
// 新006専用のread-only入力観測。元Draw/stageを変更せず、効果は適用しない。
std::array<bool,32> bridge_input_seen{};
void record_bridge_inputs(IDirect3DDevice9*d,int mat,const DrawArgs&a,const std::vector<visible_geometry::Run>&runs){
    if(!st.external_capture||runs.empty()||bridge_input_seen[mat])return;
    const auto&name=materials[mat].name;if(name!="Car_Tunnel_Wall.dxt"&&name!="LondonPavement.dxt")return;
    bridge_input_seen[mat]=true;
    for(unsigned stage=0;stage<4;++stage){DWORD op=0;if(FAILED(d->GetTextureStageState(stage,D3DTSS_COLOROP,&op))||op==D3DTOP_DISABLE)break;
        Ref<IDirect3DBaseTexture9>base;D3DSURFACE_DESC desc{};UINT level=0;bool saved=false;size_t n=0;
        std::string file="bridge-"+name+"-stage-"+std::to_string(stage)+"-mip.bin";
        bool got=SUCCEEDED(d->GetTexture(stage,&base.p))&&base.p&&base.p->GetType()==D3DRTYPE_TEXTURE;
        if(got){auto*t=static_cast<IDirect3DTexture9*>(base.p);UINT count=t->GetLevelCount();got=count>0;level=count?std::min<UINT>(2,count-1):0;
            got=got&&SUCCEEDED(t->GetLevelDesc(level,&desc))&&desc.Pool==D3DPOOL_MANAGED&&!desc.Usage&&desc.Width&&desc.Height&&desc.Width<=2048&&desc.Height<=2048;
            size_t pitch=0,rows=desc.Height;
            if(desc.Format==D3DFMT_DXT1||desc.Format==D3DFMT_DXT5){pitch=size_t((desc.Width+3)/4)*(desc.Format==D3DFMT_DXT1?8:16);rows=(desc.Height+3)/4;}
            else if(desc.Format==D3DFMT_A8R8G8B8||desc.Format==D3DFMT_X8R8G8B8)pitch=size_t(desc.Width)*4;
            else if(desc.Format==D3DFMT_R5G6B5)pitch=size_t(desc.Width)*2;
            got=got&&pitch&&pitch*rows<=4*1024*1024;
            D3DLOCKED_RECT bits{};if(got&&SUCCEEDED(t->LockRect(level,&bits,nullptr,D3DLOCK_READONLY))){
                auto path=directory+std::wstring(file.begin(),file.end());FILE*f=nullptr;if(bits.pBits&&bits.Pitch>=int(pitch))f=_wfopen(path.c_str(),L"wbx");
                saved=f!=nullptr;if(f){for(size_t y=0;y<rows;++y){size_t wrote=fwrite((char*)bits.pBits+y*bits.Pitch,1,pitch,f);n+=wrote;if(wrote!=pitch)saved=false;}if(fclose(f))saved=false;}
                saved=SUCCEEDED(t->UnlockRect(level))&&saved;}}
        event("\"event\":\"bridge_stage_input\",\"frame\":"+std::to_string(st.frame)+",\"material\":\""+name+"\",\"stage\":"+std::to_string(stage)+
            ",\"saved\":"+(saved?"true":"false")+",\"filename\":\""+file+"\",\"level\":"+std::to_string(level)+",\"width\":"+std::to_string(desc.Width)+
            ",\"height\":"+std::to_string(desc.Height)+",\"format\":"+std::to_string(unsigned(desc.Format))+",\"bytes\":"+std::to_string(n)+",\"semantic_identity\":\"unknown_until_cpu_byte_match\"");
    }
    Ref<IDirect3DVertexBuffer9>vb;Ref<IDirect3DIndexBuffer9>ib;D3DVERTEXBUFFER_DESC vd{};D3DINDEXBUFFER_DESC id{};DWORD fvf=0;UINT offset=0,stride=0;
    bool ok=a.indexed&&SUCCEEDED(d->GetFVF(&fvf))&&SUCCEEDED(d->GetStreamSource(0,&vb.p,&offset,&stride))&&vb.p&&SUCCEEDED(vb.p->GetDesc(&vd))&&
        SUCCEEDED(d->GetIndices(&ib.p))&&ib.p&&SUCCEEDED(ib.p->GetDesc(&id))&&((fvf==338&&stride==36)||(fvf==530&&stride==40));
    auto read=[&](auto*buffer,bool direct,UINT at,UINT bytes,void*out){if(!direct)return write_capture::store.sample((uintptr_t)buffer,at,bytes,out);
        void*data=nullptr;HRESULT h=buffer->Lock(at,bytes,&data,D3DLOCK_READONLY);if(FAILED(h))return false;if(data)memcpy(out,data,bytes);HRESULT u=buffer->Unlock();return data&&SUCCEEDED(u);};
    unsigned bytes=id.Format==D3DFMT_INDEX16?2:id.Format==D3DFMT_INDEX32?4:0;uint32_t index=0;uint64_t first=uint64_t(a.start)+uint64_t(runs[0].first)*3,at=0;unsigned char raw[40]{};
    ok=ok&&bytes&&observation::span(id.Size,first*bytes,bytes);
    if(ok)ok=read(ib.p,eligible(id.Usage,id.Pool),UINT(first*bytes),bytes,&index);
    ok=ok&&index>=a.min&&uint64_t(index)-a.min<a.vertices&&observation::vertex_offset(vd.Size,offset,stride,a.base,index,at)&&observation::span(vd.Size,at,stride);
    if(ok)ok=read(vb.p,eligible(vd.Usage,vd.Pool),UINT(at),stride,raw);
    float values[10]{};memcpy(values,raw,stride<=40?stride:0);for(unsigned i=0;i<6;++i)ok=ok&&std::isfinite(values[i]);
    unsigned uv_start=fvf==338?7:6;for(unsigned i=uv_start;i<(fvf==338?9:10);++i)ok=ok&&std::isfinite(values[i]);
    std::ostringstream s;s<<"\"event\":\"bridge_vertex_input\",\"frame\":"<<st.frame<<",\"material\":\""<<name<<"\",\"available\":"<<(ok?"true":"false")<<",\"fvf\":"<<fvf<<",\"stride\":"<<stride<<",\"index\":"<<index;
    if(ok){s<<",\"xyz_normal\":[";for(unsigned i=0;i<6;++i){if(i)s<<',';s<<values[i];}s<<"],\"uv0\":["<<values[uv_start]<<','<<values[uv_start+1]<<']';
        if(fvf==338){DWORD color=0;memcpy(&color,raw+24,4);s<<",\"packed_vertex_diffuse\":"<<color;}else s<<",\"uv1\":["<<values[8]<<','<<values[9]<<']';}
    s<<",\"not_albedo_or_vertex_shader_output\":true";event(s.str());
    observe_material_state(d,mat,a,runs);
}
