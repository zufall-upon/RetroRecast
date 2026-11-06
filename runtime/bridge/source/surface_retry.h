// 不可視候補だけを有界再検査する。shader不一致/失敗を可視性へ読み替えない。
#pragma once
#include <array>
#include <cstdint>
namespace surface_retry {
struct Policy {
    std::array<uint64_t,16> seen{};
    unsigned attempts=0;
    bool terminal=false,accepted=false;
    bool begin(uint64_t key){
        if(terminal||accepted||attempts==seen.size())return false;
        for(unsigned i=0;i<attempts;++i)if(seen[i]==key)return false;
        seen[attempts++]=key;return true;
    }
    void result(bool compared,unsigned max_error,uint64_t visible){
        if(!compared||max_error>1){terminal=true;return;}
        accepted=visible>=64;
    }
};
}
