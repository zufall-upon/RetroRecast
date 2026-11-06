// SPDX-License-Identifier: GPL-3.0-or-later
// Private scene records derived from the GPL map reader. No Engine.dll ABI.
#pragma once
#include "shadow_core.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <limits>
namespace bsp_scene {
using Point=std::array<float,3>;using Mat=shadow_core::Mat;
inline bool fan_has_area(const Point&a,const Point&b,const Point&c){
    Point u{b[0]-a[0],b[1]-a[1],b[2]-a[2]},v{c[0]-a[0],c[1]-a[1],c[2]-a[2]};
    Point cross{u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
    return double(cross[0])*cross[0]+double(cross[1])*cross[1]+double(cross[2])*cross[2]>=1e-10;
}
struct Node{uint32_t node=0,surface=0;Point normal{};std::vector<Point>points;bool opaque=true;};
struct Vertex{Point position,normal;};
struct Scene{
    std::vector<Node>nodes;std::vector<Vertex>receivers;
    bool load(FILE*f){nodes.clear();receivers.clear();char tag[8]{};uint32_t n=0;
        if(!f||fread(tag,1,8,f)!=8||memcmp(tag,"KF1BSP2\0",8)||fread(&n,4,1,f)!=1||!n||n>1000000)return false;
        for(uint32_t i=0;i<n;++i){Node v;uint32_t count=0;
            if(fread(&v.node,4,1,f)!=1||fread(&v.surface,4,1,f)!=1||fread(v.normal.data(),4,3,f)!=3||fread(&count,4,1,f)!=1||count<3||count>255)return false;
            uint32_t opaque=0;if(fread(&opaque,4,1,f)!=1||opaque>1)return false;v.opaque=opaque!=0;
            for(auto x:v.normal)if(!std::isfinite(x))return false;
            v.points.resize(count);
            if(fread(v.points.data(),sizeof(Point),count,f)!=count)return false;
            for(auto&p:v.points)for(auto x:p)if(!std::isfinite(x))return false;
            if(v.opaque){for(unsigned j=1;j+1<count;++j)if(fan_has_area(v.points[0],v.points[j],v.points[j+1]))
                for(auto k:{0u,j,j+1})receivers.push_back({v.points[k],v.normal});}
            nodes.push_back(std::move(v));}
        return fgetc(f)==EOF&&!ferror(f);
    }
    struct Choice{bool found=false;uint32_t surface=0,node=0;Point center{},normal{};float score=std::numeric_limits<float>::infinity();};
    Choice choose(const Mat&view,const Mat&projection,float x,float y)const{
        Choice best;
        if(!shadow_core::rigid(view)||!std::isfinite(x)||!std::isfinite(y)||x<0||x>=960||y<0||y>=540)return best;
        for(float v:projection)if(!std::isfinite(v))return best;
        // The ray below is for a standard, unskewed D3D perspective matrix.
        // Do not silently invent a ray for a different camera representation.
        if(std::abs(projection[11]-1)>1e-5f||std::abs(projection[15])>1e-5f||!projection[0]||!projection[5]||
           std::abs(projection[1])>1e-5f||std::abs(projection[4])>1e-5f||std::abs(projection[12])>1e-5f||std::abs(projection[13])>1e-5f)return best;
        auto inv=shadow_core::inverse_rigid(view);auto origin4=shadow_core::transform({0,0,0,1},inv);
        auto direction4=shadow_core::transform({((2*x/960-1)-projection[8])/projection[0],((1-2*y/540)-projection[9])/projection[5],1,0},inv);
        Point origin{origin4[0],origin4[1],origin4[2]},direction{direction4[0],direction4[1],direction4[2]};
        auto sub=[](Point a,Point b){return Point{a[0]-b[0],a[1]-b[1],a[2]-b[2]};};
        auto cross=[](Point a,Point b){return Point{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};
        auto dot=[](Point a,Point b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
        for(auto&n:nodes)for(size_t j=1;j+1<n.points.size();++j){auto a=n.points[0],u=sub(n.points[j],a),v=sub(n.points[j+1],a);
            auto h=cross(direction,v);float det=dot(u,h);if(std::abs(det)<1e-6f)continue;
            auto s=sub(origin,a);float bu=dot(s,h)/det;if(bu<0||bu>1)continue;auto q=cross(s,u);float bv=dot(direction,q)/det;
            if(bv<0||bu+bv>1)continue;
            float distance=dot(v,q)/det;if(distance<5||distance>=best.score)continue;
            Point hit{};for(unsigned k=0;k<3;++k)hit[k]=origin[k]+direction[k]*distance;
            best={n.opaque&&std::abs(n.normal[2])<.15f,n.surface,n.node,hit,n.normal,distance};
        }return best;
    }
    struct Sample {Choice choice;float x=0,y=0;};
    Sample choose_samples(const Mat&view,const Mat&projection,const std::vector<std::array<float,2>>&pixels,const std::vector<uint32_t>&excluded={})const{
        Sample best;
        for(auto pixel:pixels){auto candidate=choose(view,projection,pixel[0],pixel[1]);
            // Each ray independently stops at its closest BSP intersection.
            // Never select a wall behind an unknown/non-wall for that ray.
            if(candidate.found&&std::find(excluded.begin(),excluded.end(),candidate.surface)==excluded.end()&&candidate.score<best.choice.score)best={candidate,pixel[0],pixel[1]};}
        return best;
    }
    std::vector<Point>surface(uint32_t id)const{std::vector<Point>out;
        for(auto&n:nodes)if(n.surface==id&&n.opaque)for(size_t j=1;j+1<n.points.size();++j)
            if(fan_has_area(n.points[0],n.points[j],n.points[j+1]))for(auto k:{size_t(0),j,j+1})out.push_back(n.points[k]);
        return out;}
};
}
