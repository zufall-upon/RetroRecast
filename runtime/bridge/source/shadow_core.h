#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <vector>
namespace shadow_core {
using Mat=std::array<float,16>;
inline Mat identity(){Mat m{};for(int i=0;i<4;++i)m[i*4+i]=1;return m;}
inline Mat multiply(const Mat&a,const Mat&b){Mat c{};for(int i=0;i<4;++i)for(int j=0;j<4;++j)for(int k=0;k<4;++k)c[i*4+j]+=a[i*4+k]*b[k*4+j];return c;}
inline std::array<float,4> transform(std::array<float,4> v,const Mat&m){std::array<float,4> r{};for(int j=0;j<4;++j)for(int i=0;i<4;++i)r[j]+=v[i]*m[i*4+j];return r;}
inline bool affine(const Mat&m){for(auto v:m)if(!std::isfinite(v))return false;
    return std::abs(m[3])+std::abs(m[7])+std::abs(m[11])+std::abs(m[15]-1)<=1e-4f;}
inline bool rigid(const Mat&m){if(!affine(m))return false;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){float v=0;for(int k=0;k<3;++k)v+=m[k*4+i]*m[k*4+j];if(std::abs(v-(i==j?1.f:0.f))>.002f)return false;}
    return true;}
inline Mat inverse_rigid(const Mat&m){Mat r=identity();for(int i=0;i<3;++i)for(int j=0;j<3;++j)r[i*4+j]=m[j*4+i];
    for(int j=0;j<3;++j){r[12+j]=0;for(int k=0;k<3;++k)r[12+j]-=m[12+k]*r[k*4+j];}return r;}
inline Mat down_view(const std::array<float,3>&p){Mat m=identity();m[5]=-1;m[10]=-1;m[12]=-p[0];m[13]=p[1];m[14]=p[2];return m;}
inline Mat projection(float scale,float near_z,float far_z){Mat m{};m[0]=m[5]=scale;m[10]=far_z/(far_z-near_z);m[11]=1;m[14]=-near_z*m[10];return m;}
inline bool edge(bool down,bool focused,bool&previous){bool r=down&&!previous&&focused;previous=down;return r;}
inline bool release_owned_resources(unsigned remaining_with_guard,unsigned owned_device_refs){return remaining_with_guard<=owned_device_refs+1;}
inline bool same_scene_eye(const std::array<float,4>&eye,const std::array<float,3>&previous){float distance2=0;
    for(unsigned i=0;i<3;++i){if(!std::isfinite(eye[i])||!std::isfinite(previous[i]))return false;distance2+=(eye[i]-previous[i])*(eye[i]-previous[i]);}
    return distance2<=256.f*256.f;}
// Pick an actual submitted triangle, not the transform origin.  Only triangles
// wholly in front of the near plane are considered: no guessed clipping or
// object identity.  The caller must check the visible mask against the game.
struct PixelHit{bool found=false;float depth=1;size_t triangle=0;};
inline PixelHit pixel_hit(const std::vector<std::array<float,3>>&points,const std::vector<uint32_t>&indices,
                          const Mat&view_projection,float width,float height,float x,float y){
    PixelHit best;if(width<=0||height<=0||x<0||y<0||x>=width||y>=height)return best;
    for(size_t i=0;i+2<indices.size();i+=3){
        float sx[3],sy[3],z[3];bool valid=true;
        for(unsigned j=0;j<3;++j){auto index=indices[i+j];if(index>=points.size()){valid=false;break;}
            auto&p=points[index];auto q=transform({p[0],p[1],p[2],1},view_projection);
            if(!std::isfinite(q[0])||!std::isfinite(q[1])||!std::isfinite(q[2])||!std::isfinite(q[3])||q[3]<=0||q[2]<0||q[2]>q[3]){valid=false;break;}
            sx[j]=(q[0]/q[3]*.5f+.5f)*width;sy[j]=(-q[1]/q[3]*.5f+.5f)*height;z[j]=q[2]/q[3];}
        if(!valid)continue;
        float area=(sx[1]-sx[0])*(sy[2]-sy[0])-(sy[1]-sy[0])*(sx[2]-sx[0]);
        if(!std::isfinite(area)||std::abs(area)<1e-5f)continue;
        float b=((x-sx[0])*(sy[2]-sy[0])-(y-sy[0])*(sx[2]-sx[0]))/area;
        float c=((sx[1]-sx[0])*(y-sy[0])-(sy[1]-sy[0])*(x-sx[0]))/area;
        if(b<0||c<0||b+c>1)continue;
        float depth=(1-b-c)*z[0]+b*z[1]+c*z[2];
        if(depth>=0&&depth<=1&&(!best.found||depth<best.depth)){best={true,depth,i/3};}
    }return best;
}
// Shared-index connectivity gives a bounded submitted component, not an engine
// object: disconnected mesh sections and semantically unrelated joined parts
// remain limitations. Return an explicit triangle list in world coordinates.
inline bool component(const std::vector<std::array<float,3>>&points,const std::vector<uint32_t>&indices,
                      size_t triangle,std::vector<std::array<float,3>>&out){
    out.clear();if(indices.size()%3||triangle>=indices.size()/3||points.empty())return false;
    std::vector<uint32_t>parent(points.size());for(size_t i=0;i<parent.size();++i)parent[i]=uint32_t(i);
    auto root=[&](uint32_t v){while(parent[v]!=v){parent[v]=parent[parent[v]];v=parent[v];}return v;};
    for(size_t i=0;i<indices.size();i+=3){if(indices[i]>=points.size()||indices[i+1]>=points.size()||indices[i+2]>=points.size())return false;
        auto a=root(indices[i]),b=root(indices[i+1]),c=root(indices[i+2]);parent[b]=a;parent[c]=a;}
    auto selected=root(indices[triangle*3]);
    for(size_t i=0;i<indices.size();i+=3)if(root(indices[i])==selected)
        for(unsigned j=0;j<3;++j)out.push_back(points[indices[i+j]]);
    return !out.empty();
}
struct Maps {
    unsigned read=0,write=1;uint64_t generation=1,read_generation=0,read_frame=0,write_frame=0;
    bool valid=false,writing=false,failed=false;unsigned draws=0;
    void invalidate(){++generation;valid=false;writing=false;failed=false;draws=0;}
    void begin(uint64_t frame){writing=true;failed=false;draws=0;write_frame=frame;}
    void drawn(bool ok){if(writing){if(ok)++draws;else failed=true;}}
    bool complete(bool present_ok){bool done=present_ok&&writing&&!failed&&draws>0;
        if(done){std::swap(read,write);read_generation=generation;read_frame=write_frame;valid=true;}
        // Never reuse a partial, failed, or absent caster frame.  Otherwise a
        // moving camera can keep projecting an arbitrarily old silhouette.
        else valid=false;
        writing=false;return done;}
    bool usable()const{return valid&&read_generation==generation&&read!=write;}
};
template<class Configure,class Draw,class Restore> bool transaction(Configure configure,Draw draw,Restore restore,bool&attempted,long&hr)noexcept{
    attempted=false;hr=-1;try{if(configure()){attempted=true;hr=draw();}}catch(...){}
    try{return restore();}catch(...){return false;}
}
}
