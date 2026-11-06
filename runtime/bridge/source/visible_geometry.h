// SPDX-License-Identifier: GPL-3.0-or-later
// Exact triangles from the *already saved* 019/020 private scenes. No .rom parse.
#pragma once
#include "bsp_scene.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

namespace visible_geometry {
using Point = bsp_scene::Point;
using VertexKey = std::array<uint32_t, 3>;
using TriangleKey = std::array<VertexKey, 3>;
struct Run { uint32_t first=0, count=0; };

inline bool point_key(const Point& p, VertexKey& out) {
    for (size_t i=0; i<3; ++i) {
        if (!std::isfinite(p[i])) return false;
        float f = p[i] == 0.f ? 0.f : p[i]; // +0/-0 are the same position.
        std::memcpy(&out[i], &f, sizeof(f));
    }
    return true;
}
inline bool triangle_key(const Point& a, const Point& b, const Point& c, TriangleKey& key) {
    if (!point_key(a,key[0]) || !point_key(b,key[1]) || !point_key(c,key[2]) ||
        !bsp_scene::fan_has_area(a,b,c)) return false;
    std::sort(key.begin(),key.end());
    return true;
}
struct Index {
    // Bit 1 is an eligible horizontal Shader-only triangle; bit 2 is any
    // other use of exactly the same triangle. Ambiguous triangles are rejected.
    std::map<TriangleKey,unsigned> triangles;
    unsigned changed_nodes=0, horizontal_nodes=0;
    bool build(const bsp_scene::Scene& a, const bsp_scene::Scene& b) {
        triangles.clear(); changed_nodes=horizontal_nodes=0;
        if (a.nodes.empty() || a.nodes.size()!=b.nodes.size()) return false;
        for (size_t i=0; i<a.nodes.size(); ++i) {
            const auto& x=a.nodes[i]; const auto& y=b.nodes[i];
            if (x.node!=y.node || x.surface!=y.surface || x.normal!=y.normal || x.points!=y.points ||
                (!x.opaque && y.opaque)) { triangles.clear(); return false; }
            bool changed=x.opaque && !y.opaque;
            if (changed) ++changed_nodes;
            bool eligible=changed && x.normal[2]>=.98f;
            if (eligible) ++horizontal_nodes;
            for (size_t j=1; j+1<x.points.size(); ++j) {
                TriangleKey key{};
                if (triangle_key(x.points[0],x.points[j],x.points[j+1],key))
                    triangles[key] |= eligible ? 1u : 2u;
            }
        }
        return changed_nodes && horizontal_nodes && !triangles.empty();
    }
    bool build_wall(const bsp_scene::Scene& a, const bsp_scene::Scene& b, bool retained_opaque_shaders=false) {
        triangles.clear(); changed_nodes=horizontal_nodes=0;
        if (a.nodes.empty() || a.nodes.size()!=b.nodes.size()) return false;
        for (size_t i=0; i<a.nodes.size(); ++i) {
            const auto& x=a.nodes[i]; const auto& y=b.nodes[i];
            if (x.node!=y.node || x.surface!=y.surface || x.normal!=y.normal || x.points!=y.points ||
                (!x.opaque && y.opaque)) { triangles.clear(); return false; }
            // 020 remains plain Texture only. Optional 019 opaque Shader
            // geometry is retained separately, not relabelled as 020/plain.
            // GPU diffuse bytes and actual opaque Draw state still intersect.
            const bool eligible=(retained_opaque_shaders?x.opaque:y.opaque) && std::abs(y.normal[2])<.25f;
            if (eligible) ++horizontal_nodes;
            for (size_t j=1; j+1<y.points.size(); ++j) {
                TriangleKey key{};
                if (triangle_key(y.points[0],y.points[j],y.points[j+1],key))
                    triangles[key] |= eligible ? 1u : 2u;
            }
        }
        return horizontal_nodes && !triangles.empty();
    }
    struct Report {
        unsigned target=0, other=0, unknown=0, ambiguous=0, degenerate=0, invalid=0;
        std::vector<Run> runs;
        bool accepted() const { return target && !other && !unknown && !ambiguous && !degenerate && !invalid; }
        bool replayable() const { return target && !invalid && !runs.empty(); }
    };
    Report inspect(const std::vector<Point>& points, const std::vector<uint32_t>& indices,
                   float ceiling=std::numeric_limits<float>::infinity()) const {
        Report report;
        if (indices.empty() || indices.size()%3 || std::isnan(ceiling)) { report.invalid=1; return report; }
        for (size_t i=0; i<indices.size(); i+=3) {
            if (indices[i]>=points.size() || indices[i+1]>=points.size() || indices[i+2]>=points.size()) { ++report.invalid; continue; }
            const auto& a=points[indices[i]];const auto& b=points[indices[i+1]];const auto& c=points[indices[i+2]];
            VertexKey ka{},kb{},kc{};
            if (!point_key(a,ka)||!point_key(b,kb)||!point_key(c,kc)) { ++report.invalid; continue; }
            TriangleKey key{};
            if (!triangle_key(a,b,c,key)) { ++report.degenerate; continue; }
            auto it=triangles.find(key);
            if (it==triangles.end()) ++report.unknown;
            else if (it->second==1 && std::max({a[2],b[2],c[2]})>ceiling) ++report.other;
            else if (it->second==1) {
                ++report.target;const auto first=uint32_t(i/3);
                if (!report.runs.empty() && report.runs.back().first+report.runs.back().count==first) ++report.runs.back().count;
                else report.runs.push_back({first,1});
            }
            else if (it->second==2) ++report.other;
            else ++report.ambiguous;
        }
        return report;
    }
    bool matches(const std::vector<Point>& points, const std::vector<uint32_t>& indices) const {
        return inspect(points,indices).accepted();
    }
};
}
