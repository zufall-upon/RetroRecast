#pragma once
// Offset-free conversion boundary for a separately verified UE2 model reader.
// These are owned snapshots, NOT the in-memory ABI of any Engine.dll version.
// The box/radius idea was informed by the fixed KF1 compatibility source; its
// code and unknown engine offsets are not copied. No engine hook is installed.
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
namespace ue2_static_geometry {
using Point=std::array<float,3>;
struct Bounds {Point low{},high{};bool valid=false;};
enum class Surface { unknown, opaque, translucent_or_invisible };
struct Node {int64_t first=-1,count=0,section=-1;Bounds bounds{};Surface surface=Surface::unknown;};
struct Snapshot {std::vector<Point> points;std::vector<uint32_t> vertex_points;std::vector<Node> nodes;size_t section_count=0;};
enum class Result { ok, bad_request, invalid_node, nonopaque, outside_radius, invalid_geometry, too_large };
inline bool finite(const Point&p){return std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2]);}
inline bool near_box(const Bounds&b,const Point&p,float radius){
    if(!b.valid||!finite(b.low)||!finite(b.high)||!finite(p)||!std::isfinite(radius)||radius<0)return false;
    double d2=0;for(unsigned i=0;i<3;++i){if(b.low[i]>b.high[i])return false;
        double v=p[i]<b.low[i]?double(b.low[i])-p[i]:p[i]>b.high[i]?double(p[i])-b.high[i]:0.;d2+=v*v;}
    return d2<=double(radius)*radius;
}
// A BSP node's polygon is a convex fan only after the future reader has
// verified the version and polygon interpretation. No arbitrary box geometry
// is invented. On any invalidity the output remains empty.
inline Result node_triangles(const Snapshot&s,size_t id,const Point&center,float radius,size_t max_triangles,std::vector<Point>&out){
    out.clear();if(!finite(center)||!std::isfinite(radius)||radius<0)return Result::bad_request;
    if(id>=s.nodes.size())return Result::invalid_node;
    const auto&n=s.nodes[id];
    if(n.section<0||uint64_t(n.section)>=s.section_count||!n.bounds.valid||!finite(n.bounds.low)||!finite(n.bounds.high))return Result::invalid_node;
    for(unsigned i=0;i<3;++i)if(n.bounds.low[i]>n.bounds.high[i])return Result::invalid_node;
    if(n.surface!=Surface::opaque)return Result::nonopaque;
    if(!near_box(n.bounds,center,radius))return Result::outside_radius;
    if(n.first<0||n.count<3||uint64_t(n.first)>s.vertex_points.size()||uint64_t(n.count)>s.vertex_points.size()-uint64_t(n.first))return Result::invalid_geometry;
    uint64_t triangles=uint64_t(n.count)-2;if(triangles>max_triangles||triangles>SIZE_MAX/3)return Result::too_large;
    std::vector<Point>vertices;vertices.reserve(size_t(n.count));
    for(int64_t i=0;i<n.count;++i){auto p=s.vertex_points[size_t(n.first+i)];
        if(p>=s.points.size()||!finite(s.points[p]))return Result::invalid_geometry;
        vertices.push_back(s.points[p]);}
    std::vector<Point>result;result.reserve(size_t(triangles)*3);
    for(size_t i=1;i+1<vertices.size();++i){auto a=vertices[0],b=vertices[i],c=vertices[i+1];
        double ux=b[0]-a[0],uy=b[1]-a[1],uz=b[2]-a[2],vx=c[0]-a[0],vy=c[1]-a[1],vz=c[2]-a[2];
        double x=uy*vz-uz*vy,y=uz*vx-ux*vz,z=ux*vy-uy*vx;
        if(x*x+y*y+z*z<=1e-12)return Result::invalid_geometry;
        result.push_back(a);result.push_back(b);result.push_back(c);}
    out.swap(result);return Result::ok;
}
}
