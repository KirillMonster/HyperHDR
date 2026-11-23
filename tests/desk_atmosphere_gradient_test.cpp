#include <base/DeskAtmosphereGradient.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>
using Processor=hyperhdr::DeskAtmosphereGradient;
using Rgb=Processor::Rgb;
constexpr unsigned W=160, H=96;
using Frame=std::vector<std::uint8_t>;
Frame solid(Rgb c) {
    Frame v(W*H*3);
    for(std::size_t i=0;i<v.size();i+=3){v[i]=c.r;v[i+1]=c.g;v[i+2]=c.b;}
    return v;
}
void pixel(Frame& f,unsigned x,unsigned y,Rgb c){auto i=(y*W+x)*3;f[i]=c.r;f[i+1]=c.g;f[i+2]=c.b;}
Processor::Output run(Processor& p,const Frame& f,std::int64_t ms){return p.render(f.data(),W,H,0,0,W,H,ms);}
int diff(Rgb a,Rgb b){return std::abs(int(a.r)-b.r)+std::abs(int(a.g)-b.g)+std::abs(int(a.b)-b.b);}
int main() {
    Processor p;
    const Frame red=solid({255,0,0});
    const auto all=run(p,red,0);
    for(const auto c:all) assert(diff(c,{255,0,0})<=2);
    Processor split;
    Frame gradient=solid({15,20,30});
    for(unsigned y=0;y<H;++y) for(unsigned x=0;x<W;++x) {
      if (x<W/3) pixel(gradient,x,y,{30,80,235});
      else if(x<2*W/3) pixel(gradient,x,y,{240,160,25});
      else pixel(gradient,x,y,{145,30,210});
    }
    const auto out=run(split,gradient,0);
    assert(out[5].b > out[5].r);
    assert(out[100].b > out[100].g);
    assert(out[60].r > out[60].b);
    for(int i=0;i<56;++i) assert(diff(out[33+i],out[177-i])==0);
    static_assert(33+56+33+56==Processor::kLedCount);
    Processor sides;
    Frame vertical=solid({22,100,240});
    for(unsigned y=H/2;y<H;++y)for(unsigned x=0;x<W/3;++x)pixel(vertical,x,y,{22,240,130});
    auto v=run(sides,vertical,0);
    assert(diff(v[0],v[32])>3);
    Processor dark;
    auto a=run(dark,red,0);
    auto b=run(dark,solid({0,0,0}),100);
    assert(diff(b[0],a[0])==0);
    auto c=run(dark,solid({0,0,0}),500);
    for(const auto led:c) assert(diff(led,{0,0,0})==0);
    auto d=run(dark,red,600);
    assert(d[0].r>200);
    Processor overlay;
    auto black=solid({0,0,0});
    run(overlay,black,1000);run(overlay,black,1400);
    for(unsigned y=0;y<12;++y)for(unsigned x=0;x<12;++x)pixel(black,x,y,{0,100,250});
    auto withOverlay=run(overlay,black,1450);
    for(const auto led:withOverlay) assert(diff(led,{0,0,0})==0);
    std::cout<<"DeskAtmosphereGradient: tests passed\n";
}
