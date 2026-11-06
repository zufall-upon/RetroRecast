#pragma once
// Explicit test only. Normal mode never synthesizes input or diagnostics.
namespace comparison_harness {
struct Input {unsigned due;int action;WORD key;bool up;};
constexpr Input inputs[]={
    {1000,1,VK_RETURN,false},{1200,1,VK_RETURN,true},
    {3000,1,VK_ESCAPE,false},{3200,1,VK_ESCAPE,true},
    {4800,2,0,false},{5000,3,0,false},{5200,3,0,true},
    // Comparison state must not depend on window focus. The normal controller
    // separately tests the actual F7 binding and records whether it was sent.
    {12300,13,0,false},
    {14030,14,0,false},{14100,12,0,false},{14200,14,0,false},
    {14550,15,0,false},{14700,15,0,false},
    {14900,6,0,false},{15100,9,0,false},{15350,6,0,false},
    {15650,16,0,false},{15950,16,0,false},{16100,9,0,false},
    {16200,10,0,false},{16800,10,0,false},
    {16900,11,0,false},{17500,11,0,false},
    {22000,1,'A',false},{22600,1,'A',true},
    {23500,1,'D',false},{24700,1,'D',true},
    {26000,4,0,false},{28000,3,0,false},{28200,3,0,true},
    {30000,5,0,false},
    {34000,1,'W',false},{35500,1,'W',true}};
struct Shot{unsigned due;const char*name;};
constexpr Shot shots[]={{12000,"mod-off"},{14000,"light-only"},{14070,"shadowed"},
    {14140,"return-off"},{14660,"shadow-without-static"},
    {15300,"diagnostic-without-receivers"},{15600,"diagnostic-visibility"},
    {15900,"diagnostic-only-receivers"},{16600,"diagnostic-casters"},{17300,"diagnostic-position"},
    {20000,"shadow-stable"},{23000,"shadow-left"},
    {25500,"shadow-right"},{28500,"after-fire"},{39000,"after-enemies"},{45000,"gameplay-final"},
    {96000,"runtime-after-90"}};
struct Harness {
    bool enabled=false,long_run=false,finished=false;
    bool input_done[sizeof(inputs)/sizeof(inputs[0])]{},shot_done[sizeof(shots)/sizeof(shots[0])]{};
    template<class Send>void tick(uint64_t ms,Send send){if(!enabled||finished)return;
        for(unsigned i=0;i<sizeof(inputs)/sizeof(inputs[0]);++i)if(!input_done[i]&&ms>=inputs[i].due){input_done[i]=true;send(inputs[i]);}}
    const char*shot(uint64_t ms){if(!enabled||finished)return nullptr;
        for(unsigned i=0;i<sizeof(shots)/sizeof(shots[0]);++i)if(!shot_done[i]&&ms>=shots[i].due){shot_done[i]=true;return shots[i].name;}
        return nullptr;}
    bool end(uint64_t ms){if(enabled&&!finished&&ms>=(long_run?100000u:46000u)){finished=true;return true;}return false;}
};
}
