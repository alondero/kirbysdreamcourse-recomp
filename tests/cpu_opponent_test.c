#include "cpu_opponent.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"CHECK %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static uint8_t ram[0x20000], before[0x20000];
static uint32_t sites[16];
static InterpPreOpcodeHook hooks[16];
static unsigned count;
bool interp_bridge_add_pre_opcode_hook(uint32_t pc,InterpPreOpcodeHook hook)
{ CHECK(count<16); sites[count]=pc; hooks[count++]=hook; return true; }
static unsigned Word(unsigned a) { return ram[a] | (unsigned)ram[a+1]<<8; }
static void Put(unsigned a,unsigned v) { ram[a]=(uint8_t)v; ram[a+1]=(uint8_t)(v>>8); }
static void At(CpuState *cpu,uint32_t pc)
{ for(unsigned i=0;i<count;++i) if(sites[i]==pc) { hooks[i](cpu,pc); return; } CHECK(0); }

int main(void)
{
    CpuState cpu={0}; cpu.ram=ram;
    KdcCpuInitialize();
    uint32_t input=0xffffff;
    KdcCpuFrame(&input); CHECK(input==0xffffff);
    for(unsigned d=1;d<=3;++d) {
        KdcCpuSelect(d); input=0xffffff; KdcCpuFrame(&input); CHECK(input==0xfff);
        memset(ram,0,sizeof(ram)); Put(0x17ff,1);
        memcpy(before,ram,sizeof(ram));
        for(unsigned i=0;i<count;++i) At(&cpu,sites[i]);
        CHECK(!memcmp(before,ram,sizeof(ram))); /* Human turn cannot be changed. */
        Put(0x1803,2);
        Put(0x6b3c,d*8); /* Low-energy power caps must remain reachable. */
        Put(0xb4b8,75); Put(0xb4ba,0xffff); Put(0xb4f8,0);
        Put(0x4e1c,0x49); Put(0xb538,8); Put(0xb578,6);
        At(&cpu,0x81a619);
        unsigned wanted_spin=d==1?6:d==2?3:9;
        unsigned iterations;
        for(iterations=0;iterations<1000;++iterations) {
            At(&cpu,0x81a680);
            if(Word(0x6b)==0x80) break;
            CHECK(Word(0xd9b4)<360 && Word(0xd9b6)<=60);
            if(d==1) CHECK(!Word(0xd9b6) && Word(0xd9b8)==6 && Word(0xd9ba)==6);
            At(&cpu,0x81a9b8); cpu.A=d*8; At(&cpu,0x81c2e9); CHECK(cpu.A<=d*8);
            /* Only the requested spin reaches this synthetic target. This
             * checks that higher tiers can actually select both spin signs. */
            bool reaches=Word(0xd9ba)==wanted_spin;
            Put(0x6aee,1); Put(0xb746,0x8000); Put(0xbb46,reaches?8:0);
            Put(0xbf46,0x8000); Put(0xc346,reaches?6:0);
            At(&cpu,0x81aa3d);
        }
        CHECK(iterations<1000);
        CHECK(Word(0xd9ba)==wanted_spin);
        /* Spin and power waits must be bounded by the guest's valid ranges. */
        bool confirmed=false;
        for(unsigned y=0;y<=12 && !confirmed;++y) for(unsigned x=0;x<=12;++x) {
            Put(0x6b50,x); Put(0x6b52,y); Put(0x6b,0);
            At(&cpu,0x81bfb5); if(Word(0x6b)==0x80) { confirmed=true; break; }
        }
        CHECK(confirmed);
        CHECK(Word(0x6b52)==wanted_spin);
        Put(0x6b3a,0); At(&cpu,0x81c005); CHECK(!Word(0x6b));
        Put(0x6b3a,d*8); At(&cpu,0x81c005); CHECK(Word(0x6b)==0x80);
        At(&cpu,0x81c04c);
        Put(0x1803,0); Put(0x6b84,1); Put(0x51,0);
        At(&cpu,0x80f48c);
        for(unsigned i=0;i<119;++i) At(&cpu,0x83d495);
        CHECK(!Word(0x51));
        At(&cpu,0x83d495); CHECK(Word(0x51)==0x80);
        Put(0x6b84,0); Put(0x51,0);
        At(&cpu,0x83d495); CHECK(!Word(0x51));
        KdcCpuSelect(0); memcpy(before,ram,sizeof(ram));
        for(unsigned i=0;i<count;++i) At(&cpu,sites[i]);
        CHECK(!memcmp(before,ram,sizeof(ram)));
    }
    return 0;
}
