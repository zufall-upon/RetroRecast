#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

namespace frame_capture {
constexpr unsigned width=960,height=540,row_bytes=width*4;
constexpr size_t pixels=size_t(row_bytes)*height,bmp_bytes=pixels+54;
constexpr size_t surface_cap=2304*1024,image_cap=8*1024*1024;
inline bool supported(unsigned w,unsigned h,unsigned format,unsigned msaa,unsigned pool,unsigned usage) {
    return w==width&&h==height&&(format==21||format==22)&&msaa==0&&pool==0&&(usage&1);
}
inline bool pitch_ok(int pitch) {
    return pitch>=int(row_bytes)&&size_t(pitch)*height<=surface_cap;
}
template<class W> bool rows(const void*bits,int pitch,W write) {
    if(!bits||!pitch_ok(pitch))return false;
    for(unsigned y=0;y<height;++y)if(!write((const unsigned char*)bits+size_t(y)*pitch,row_bytes))return false;
    return true;
}
inline std::array<unsigned char,54> header() {
    std::array<unsigned char,54> b{};
    auto put=[&](unsigned at,uint32_t v){for(unsigned i=0;i<4;++i)b[at+i]=unsigned(v>>(i*8));};
    b[0]='B';b[1]='M';put(2,bmp_bytes);put(10,54);put(14,40);put(18,width);
    put(22,uint32_t(-int(height)));b[26]=1;b[28]=32;put(34,pixels);return b;
}
// 読取り・記録が例外でも元Presentの引数/回数/戻り値は変えない。
template<class B,class F,class A> auto present_once(B before,F forward,A after) -> decltype(forward()) {
    try{before();}catch(...){}auto hr=forward();try{after(hr);}catch(...){}return hr;
}
}
