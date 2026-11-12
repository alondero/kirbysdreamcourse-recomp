/* Game-specific opponent: searches the guest's trajectory preview, then uses
 * its shot controls. No replacement collision/physics implementation. */
#include "cpu_opponent.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { unsigned angle, loft, spin_x, spin_y, power; } Shot;
typedef struct { double x, y, z; } Point;
static unsigned difficulty, trial, trials, turn, settle, max_power, score_wait;
static bool searching, preview, ready, shooting, scheduled, refining;
static Shot current, best, coarse;
static double best_score;
static uint32_t rng;
static Point targets[32];
static bool cups[32];
static unsigned target_count;
static unsigned angles[10], angle_count;

static unsigned Word(const uint8_t *r, unsigned a) { return r[a] | (unsigned)r[a+1] << 8; }
static void Put(uint8_t *r, unsigned a, unsigned v) { r[a]=(uint8_t)v; r[a+1]=(uint8_t)(v>>8); }
static double Fixed(const uint8_t *r, unsigned lo, unsigned hi)
{ return (int16_t)Word(r,hi) + Word(r,lo)/65536.0; }
static uint32_t Random(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
const char *KdcCpuName(unsigned d)
{ static const char *const names[]={"HUMAN", "EASY", "MODERATE", "EXPERT"}; return names[d<4?d:0]; }
unsigned KdcCpuDifficulty(void) { return difficulty; }
void KdcCpuReset(void)
{
    trial=trials=turn=settle=target_count=score_wait=0;
    searching=preview=ready=shooting=scheduled=refining=false;
    rng=0x4b444331;
}
void KdcCpuSelect(unsigned d) { difficulty=d<=3?d:0; KdcCpuReset(); }
void KdcCpuFrame(uint32_t *inputs)
{
    if (difficulty) *inputs &= 0xfff; /* The physical P2 seat is CPU-owned. */
}

static bool OwnTurn(const uint8_t *r)
{ return difficulty && Word(r,0x17ff)==1 && Word(r,0x1803)==2 && !Word(r,0xed07); }

static Shot Candidate(unsigned n)
{
    if (refining) {
        unsigned step=difficulty==3?1:difficulty==2?2:4;
        Shot s=coarse;
        int delta=(int)(n%9)-4;
        s.angle=(s.angle+360+delta*(int)step)%360;
        int power=(int)s.power+((int)(n/9)-2)*3;
        s.power=(unsigned)(power<1?1:power>(int)max_power?(int)max_power:power);
        return s;
    }
    static const unsigned powers[]={6,16,32,48,63};
    /* Easy never deliberately banks or adds spin. Moderate explores loft and
     * spin, with a wider repertoire on one turn in four. */
    unsigned v=n/(angle_count*5);
    Shot s={ angles[n%angle_count], 0, 6, 6, powers[(n/angle_count)%5] };
    if (s.power>max_power) s.power=max_power;
    if (v) { s.loft= v>=5?60:30; s.spin_y=v==1?6:v==2?3:v==3?9:6; }
    if (v==4) s.spin_x=3;
    if (v==6) s.spin_x=9;
    if (v==7) { s.loft=0; s.spin_x=3; }
    if (v==8) { s.loft=0; s.spin_x=9; }
    return s;
}

static void Begin(const uint8_t *r)
{
    ++turn;
    max_power=Word(r,0x6b3c);
    if (!max_power || max_power>63) max_power=63;
    target_count=0;
    for (unsigned i=0;i<64;i+=2) {
        unsigned entity=Word(r,0xb4b8+i);
        if (entity==0xffff) break;
        if (entity!=8 && (entity<0x40 || entity>=0x58)) continue;
        unsigned slot=Word(r,0xb4f8+i);
        if (slot>=128 || (slot&1) || Word(r,0x4e1c+slot)!=0x49) continue;
        if (entity!=8 && Word(r,0x539c+slot)==1) continue;
        cups[target_count]=entity==8;
        targets[target_count++]=(Point){Word(r,0xb538+i)+0.5,
            Word(r,0xb578+i)+0.5,Fixed(r,0xb5b8+i,0xb5f8+i)};
    }
    trial=0; settle=0; best_score=-1e30;
    refining=false;
    /* Direct approaches first. Keep a bounded shortlist even on busy holes. */
    double origin_x=Fixed(r,0xd8ac,0xd8b0), origin_y=Fixed(r,0xd8b4,0xd8b8);
    unsigned chosen=0;
    angle_count=0;
    for (unsigned j=0;j<target_count && j<4;++j) {
        double nearest=1e30; unsigned next=0;
        for (unsigned t=0;t<target_count;++t) {
            if (chosen & (1u<<t)) continue;
            double dx=targets[t].x-origin_x,dy=targets[t].y-origin_y;
            double distance=dx*dx+dy*dy;
            if(distance<nearest) { nearest=distance; next=t; }
        }
        chosen |= 1u<<next;
        int angle=(int)lround(atan2(targets[next].y-origin_y,targets[next].x-origin_x)*180/3.14159265358979323846);
        angles[angle_count++]=(unsigned)((angle+360)%360);
    }
    if (!angle_count) angles[angle_count++]=Word(r,0xd9b4)%360;
    if (difficulty==3 || (difficulty==2 && turn%4==0)) {
        angles[angle_count++]=(angles[0]+45)%360;
        angles[angle_count++]=(angles[0]+315)%360;
    }
    trials=angle_count*5*(difficulty==1?1:difficulty==2?(turn%4==0?7:4):9);
    current=best=Candidate(0);
    searching=true; ready=shooting=false;
}

static double Score(const uint8_t *r)
{
    unsigned count=Word(r,0x6aee);
    if (!count || count>512 || !target_count) return -1e20;
    double score=0, nearest=1e20;
    Point origin={Fixed(r,0x6ae2,0x6ae4),Fixed(r,0x6ae6,0x6ae8),0};
    double angle=current.angle*3.14159265358979323846/180;
    unsigned last=(count-1)*2;
    double end_x=Fixed(r,0xb746+last,0xbb46+last), end_y=Fixed(r,0xbf46+last,0xc346+last);
    if (end_x<0 || end_y<0 || end_x>=Word(r,0x8b92) || end_y>=Word(r,0x8b94))
        score-=500;
    for (unsigned t=0;t<target_count;++t) {
        double distance=1e20;
        double previous_forward=0;
        for (unsigned i=0;i<count;++i) {
            unsigned o=i*2;
            Point p={Fixed(r,0xb746+o,0xbb46+o),Fixed(r,0xbf46+o,0xc346+o),
                     Fixed(r,0xc746+o,0xcb46+o)};
            double dx=p.x-targets[t].x, dy=p.y-targets[t].y, dz=p.z-targets[t].z;
            double d=dx*dx+dy*dy+dz*dz;
            if (cups[t] && i) {
                double vx=p.x-Fixed(r,0xb746+o-2,0xbb46+o-2);
                double vy=p.y-Fixed(r,0xbf46+o-2,0xc346+o-2);
                /* Passing the cup at speed is less useful than a soft entry. */
                d+=4*(vx*vx+vy*vy);
            }
            /* Restrict Easy's useful path to the initial straight corridor. */
            if (difficulty==1) {
                double forward=(p.x-origin.x)*cos(angle)+(p.y-origin.y)*sin(angle);
                if (fabs((p.x-origin.x)*sin(angle)-(p.y-origin.y)*cos(angle))>0.35 ||
                    forward+0.01<previous_forward) break;
                previous_forward=forward;
            }
            if (d<distance) distance=d;
        }
        if (distance<nearest) nearest=distance;
        if (distance<0.20) score+=100;
    }
    /* Prefer closer approaches and modest power when predicted hits tie. */
    return score - sqrt(nearest)*10 - current.power*0.01;
}

static void Apply(uint8_t *r, Shot s)
{
    Put(r,0xd9b4,s.angle); Put(r,0xd9b6,s.loft);
    Put(r,0xd9b8,s.spin_x); Put(r,0xd9ba,s.spin_y);
    Put(r,0x6ad6,s.loft?2:0); Put(r,0x6ad8,s.loft?0:1);
    Put(r,0x6ae0,1);
    scheduled=true;
}

static void Observe(CpuState *cpu, uint32_t pc)
{
    uint8_t *r=cpu->ram;
    if (difficulty && Word(r,0x17ff)==1) {
        if ((pc & 0x7fffff)==0x00f48c) score_wait=0;
        /* The scorecard waits for the hole winner's controller, not P1.
         * Give the human two seconds to read a CPU win, then acknowledge it. */
        if ((pc & 0x7fffff)==0x03d495 && Word(r,0x6b84)==1 && ++score_wait>=120)
            Put(r,0x51,Word(r,0x51)|0x80);
    }
    if (!OwnTurn(r)) return;
    switch (pc & 0x7fffff) {
    case 0x01a619: /* New aiming task, including a consecutive CPU turn. */
        Begin(r); break;
    case 0x01a680:
        Put(r,0x67,0); Put(r,0x6b,0);
        if (!searching && !ready && !shooting) Begin(r);
        if (searching) Apply(r,current);
        else if (ready) {
            if (!settle++) Apply(r,best);
            else if (settle>20) { Put(r,0x6b,0x80); ready=false; shooting=true; }
        }
        break;
    case 0x01a9b8: preview=scheduled; scheduled=false; break;
    case 0x01aa0e:
        /* Refine over the guest's long preview horizon, including the lie
         * after a ground shot has passed its target. The ordinary aiming
         * guide uses only 40 ground simulation ticks. */
        if (preview && refining) Put(r,0x6ad2,240);
        break;
    case 0x01c2e9:
        if (preview) cpu->A=(uint16_t)(searching?current.power:best.power);
        break;
    case 0x01aa3d:
        if (preview && searching) {
            double score=Score(r);
            if (score>best_score) { best_score=score; best=current; }
            if (++trial<trials) current=Candidate(trial);
            else if (!refining) {
                coarse=best; refining=true; trial=0; trials=45;
                best_score=-1e30;
                current=Candidate(0);
            } else {
                searching=false; ready=true; settle=0;
                unsigned miss=difficulty==1?45:difficulty==2?22:8;
                if (Random()%100<miss) {
                    int error=(int)(Random()%9)-4;
                    best.angle=(best.angle+360+error)%360;
                    best.power=best.power>5?best.power-3:best.power;
                }
                fprintf(stderr,"CPU %s: turn %u angle %u loft %u spin %u/%u power %u score %.2f\n",
                    KdcCpuName(difficulty),turn,best.angle,best.loft,best.spin_x,best.spin_y,best.power,best_score);
            }
        }
        preview=false; break;
    case 0x01bfa9: /* Spin selection: same controls and oscillation as a human. */
        if (!shooting) break;
        Put(r,0x67,Word(r,0x6b50)<best.spin_x?0x100:Word(r,0x6b50)>best.spin_x?0x200:0);
        Put(r,0x6b,0);
        break;
    case 0x01bfb5:
        if (shooting && Word(r,0x6b50)==best.spin_x && Word(r,0x6b52)==best.spin_y)
            Put(r,0x6b,0x80);
        break;
    case 0x01c005:
        if (shooting) Put(r,0x6b,Word(r,0x6b3a)>=best.power?0x80:0);
        break;
    case 0x01c04c: shooting=false; break;
    }
}

void KdcCpuInitialize(void)
{
    static const uint32_t sites[]={0x81a619,0x81a680,0x81a9b8,0x81aa0e,0x81c2e9,0x81aa3d,
        0x81bfa9,0x81bfb5,0x81c005,0x81c04c,0x80f48c,0x83d495};
    for (unsigned i=0;i<sizeof(sites)/sizeof(sites[0]);++i)
        if (!interp_bridge_add_pre_opcode_hook(sites[i],Observe)) {
            fputs("Unable to register CPU opponent\n",stderr); abort();
        }
}
