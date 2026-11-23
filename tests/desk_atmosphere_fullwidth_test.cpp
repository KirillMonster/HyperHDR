#include <base/DeskAtmosphereGradient.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>
using Processor=hyperhdr::DeskAtmosphereGradient;
using Rgb=Processor::Rgb;
constexpr unsigned W=240,H=144;
using Frame=std::vector<std::uint8_t>;
Frame solid(Rgb c){Frame f(W*H*3);for(size_t i=0;i<f.size();i+=3){f[i]=c.r;f[i+1]=c.g;f[i+2]=c.b;}return f;}
void pixel(Frame&f,unsigned x,unsigned y,Rgb c){size_t i=(y*W+x)*3;f[i]=c.r;f[i+1]=c.g;f[i+2]=c.b;}
Processor::Output run(Processor &p,const Frame&f,int ms){return p.render(f.data(),W,H,0,0,W,H,ms);}
int delta(Rgb a,Rgb b){return abs(int(a.r)-int(b.r))+abs(int(a.g)-int(b.g))+abs(int(a.b)-int(b.b));}
int peak(Rgb a){return std::max({int(a.r),int(a.g),int(a.b)});}
int sat(Rgb a){return peak(a)-std::min({int(a.r),int(a.g),int(a.b)});}
int main(){
  Processor p;
  auto f=solid({20,70,230});
  for(unsigned y=0;y<H;++y)for(unsigned x=W/3;x<2*W/3;++x)pixel(f,x,y,{235,150,20});
  for(unsigned y=0;y<H;++y)for(unsigned x=2*W/3;x<W;++x)pixel(f,x,y,{165,24,210});
  auto o=run(p,f,0);
  assert(o[33].b>o[33].r);                      // near-left front -> blue
  assert(o[60].r>o[60].b);                      // central front -> warm gold
  assert(o[88].b>o[88].g);                      // near-right front -> magenta
  assert(delta(o[33],o[60])>80);
  assert(delta(o[60],o[88])>80);
  for(size_t k=0;k<56;++k)assert(delta(o[33+k],o[177-k])==0);
  for(size_t k=0;k<178;++k)assert(peak(o[k])>0);
  // Full-white scenes must not flood the room with 255 white.
  Processor mono;
  auto white=run(mono,solid({255,255,255}),0);
  for(const auto c:white) { assert(peak(c)<=110); assert(c.r==c.g&&c.g==c.b); }
  // Slightly colored center against a mostly pale background is not discarded.
  Processor tinted;
  Frame one=solid({220,220,220});
  for(unsigned y=0;y<H;++y)for(unsigned x=112;x<122;++x)pixel(one,x,y,{20,175,80});
  auto s=run(tinted,one,0);
  assert(sat(s[60])>60);
  // Neutral center inherits a saturated edge's atmosphere.
  Processor edge;
  auto g=solid({230,230,230});
  for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W/5;++x)pixel(g,x,y,{35,80,210});
  auto e=run(edge,g,0);
  assert(sat(e[60])>50);
  // Vertical side variation remains independent of horizontal mapping.
  Processor vertical;
  Frame v=solid({15,105,230});
  for(unsigned y=H/2;y<H;++y)for(unsigned x=0;x<W/3;++x)pixel(v,x,y,{40,230,120});
  auto z=run(vertical,v,0);
  assert(delta(z[0],z[32])>3);
  Processor dark;
  auto stable=run(dark,f,0);
  auto held=run(dark,solid({0,0,0}),100);
  assert(delta(held[60],stable[60])==0);
  auto blank=run(dark,solid({0,0,0}),500);
  for(auto c:blank)assert(peak(c)==0);
  auto restored=run(dark,f,600);
  assert(peak(restored[60])>120);
  std::cout<<"DeskAtmosphereGradient fullwidth: all tests passed\n";
}
