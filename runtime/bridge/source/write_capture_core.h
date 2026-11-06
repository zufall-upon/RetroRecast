#pragma once
// Derived from tools/kf1_draw_observer/shadow_core.h.  The limits below are
// choices for this private comparison, not user requirements or full coverage.
#include "observation_core.h"
#include <array>
#include <memory>
#include <cstring>
namespace shadow {
constexpr size_t budget=32*1024*1024, max_copy=4*1024*1024, slots=512, history=32;
constexpr uint32_t discard=0x2000, nooverwrite=0x1000, readonly=0x10;
struct Write { uint64_t serial=0; uint32_t offset=0,length=0,flags=0; };
struct Buffer {
    uintptr_t key=0,device=0; uint64_t generation=0,epoch=0;
    uint32_t size=0; bool blocked=false,locked=false,ambiguous=false;
    uint32_t offset=0,length=0,flags=0; unsigned next=0;
    std::unique_ptr<unsigned char[]> bytes;
    std::unique_ptr<uint16_t[]> valid;
    std::array<Write,history> writes{};
};
struct Store {
    std::array<Buffer,slots> buffers{};
    size_t allocated=0,peak=0; uint64_t generation=0,serial=0,epoch=0;
    uint64_t capacity_misses=0,unknown=0,invalidations=0,commits=0,lock_success=0;
    std::array<unsigned char,max_copy> scratch{};
    Buffer* find(uintptr_t key) { for(auto&b:buffers)if(b.key==key&&key)return &b;return nullptr; }
    void clear(Buffer&b) { if(b.valid)std::memset(b.valid.get(),0,b.size*sizeof(uint16_t));++invalidations; }
    void erase(uintptr_t key) { if(auto*b=find(key)){if(b->bytes)allocated-=size_t(b->size)*3;*b=Buffer{};} }
    void create(uintptr_t key,uintptr_t device,uint32_t size,bool blocked) {
        erase(key); ++generation;
        for(auto&b:buffers)if(!b.key){b.key=key;b.device=device;b.size=size;b.generation=generation;b.epoch=epoch;b.blocked=blocked;return;}
        ++capacity_misses;
    }
    void invalidate(uintptr_t key,bool permanent=false) { if(auto*b=find(key)){clear(*b);b->blocked|=permanent;b->ambiguous|=b->locked;} }
    void reset(uintptr_t device) { ++epoch;for(auto&b:buffers)if(b.key&&b.device==device){clear(b);b.epoch=epoch;b.ambiguous|=b.locked;} }
    void begin(uintptr_t key,uint32_t offset,uint32_t length,uint32_t flags) {
        ++lock_success; auto*b=find(key);if(!b){++unknown;return;}
        if(b->locked){clear(*b);b->ambiguous=true;return;}
        b->locked=true;b->ambiguous=false;b->offset=offset;b->length=length;b->flags=flags;
        if(flags&discard)clear(*b);
        if(offset>b->size){clear(*b);b->ambiguous=true;return;}
        if(!length)length=b->size-offset;
        b->length=length;
        if(!observation::span(b->size,offset,length)){clear(*b);b->ambiguous=true;return;}
        if(b->valid)std::memset(b->valid.get()+offset,0,length*sizeof(uint16_t));
        if((flags&readonly)||b->blocked||length>max_copy||!length){b->ambiguous=true;return;}
        if(!b->bytes){
            size_t n=size_t(b->size)*3;
            if(n>budget-allocated){++capacity_misses;b->ambiguous=true;return;}
            auto bytes=std::make_unique<unsigned char[]>(b->size);
            auto valid=std::make_unique<uint16_t[]>(b->size);
            b->bytes=std::move(bytes);b->valid=std::move(valid);allocated+=n;if(allocated>peak)peak=allocated;
        }
    }
    bool copy(uintptr_t key,const void* pointer,bool readable) {
        auto*b=find(key);if(!b||!b->locked||b->ambiguous||!b->bytes||!readable||!pointer)return false;
        std::memcpy(scratch.data(),pointer,b->length);return true;
    }
    void finish(uintptr_t key,bool copied,bool succeeded) {
        auto*b=find(key);if(!b)return;
        if(!b->locked){clear(*b);return;}
        if(copied&&succeeded&&!b->ambiguous&&!b->blocked){
            if(b->next==history){clear(*b);b->next=0;}
            auto i=b->next++;b->writes[i]={++serial,b->offset,b->length,b->flags};
            std::memcpy(b->bytes.get()+b->offset,scratch.data(),b->length);
            for(uint32_t j=0;j<b->length;++j)b->valid[b->offset+j]=uint16_t(i+1);
            ++commits;
        } else if(!succeeded||b->ambiguous)clear(*b);
        b->locked=false;b->ambiguous=false;
    }
    bool sample(uintptr_t key,uint64_t offset,uint32_t n,void*out) {
        auto*b=find(key);if(!b||b->blocked||b->locked||!b->valid||!observation::span(b->size,offset,n))return false;
        for(uint32_t i=0;i<n;++i)if(!b->valid[offset+i])return false;
        std::memcpy(out,b->bytes.get()+offset,n);return true;
    }
};
}
