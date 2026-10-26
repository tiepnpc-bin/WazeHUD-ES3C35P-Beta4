#include "audio/alert_audio_policy.h"
#include "protocol/hlp_core.h"
#include "protocol/hlp_decoder.h"
#include <cassert>
#include <cstring>
#include <string>
#include <iostream>
using namespace waze_hud;
static int frames=0;static std::string result;
void frame(const char* s,size_t n,void*){++frames;result.assign(s,n);}
int main(){
 HudState s;s.connected=true;s.hasProducerState=true;s.speedLimitKmh=60;
 DeviceSettings c;s.speedKmh=59;assert(!overspeedActive(s,c));
 s.speedKmh=60;assert(overspeedActive(s,c));s.speedKmh=61;assert(overspeedActive(s,c));
 c.overspeedOffsetKmh=5;s.speedKmh=64;assert(!overspeedActive(s,c));s.speedKmh=65;assert(overspeedActive(s,c));
 s.signalStale=true;assert(!overspeedActive(s,c));s.signalStale=false;s.speedLimitKmh=0;s.overSpeed=true;assert(!overspeedActive(s,c));
 c.overspeedOffsetKmh=0;s.speedLimitKmh=60;s.speedKmh=60;
 audio::AlertAudioPolicy p;assert(p.poll(s,c,100).clip==audio::Clip::None);
 assert(p.poll(s,c,1600).clip==audio::Clip::OverSpeed);p.finished(1600);
 assert(p.poll(s,c,10000).clip==audio::Clip::None);
 s.connected=false;assert(p.poll(s,c,32000).clip==audio::Clip::None);
 hlp_receiver_t rx;hlp_receiver_init(&rx,frame,nullptr);
 const std::string line="{\"v\":1,\"t\":\"s\",\"st\":\"Đường\"}\n";
 for(unsigned char b:line)hlp_receiver_feed(&rx,&b,1);
 assert(frames==1 && result==line.substr(0,line.size()-1));
 std::string big(700,'a');big+='\n';hlp_receiver_feed(&rx,(const uint8_t*)big.data(),big.size());assert(rx.oversized==1);
 const uint8_t bad[]={0xff,'\n'};hlp_receiver_feed(&rx,bad,2);assert(rx.malformed_utf8==1);
 hlp_receiver_feed(&rx,(const uint8_t*)line.data(),line.size());assert(frames==2);
 HlpDecoder dec;HudState decoded;auto* j=cJSON_Parse("{\"ts\":10,\"spd\":60,\"lim\":60,\"dst\":350,\"alr\":2,\"alrD\":500}");
 assert(dec.decodeState(j,decoded));assert(decoded.speedKmh==60 && decoded.maneuverDistanceM==350 && decoded.nearestAlert.distanceM==500);cJSON_Delete(j);
 j=cJSON_Parse("{\"ts\":9,\"spd\":1}");assert(!dec.decodeState(j,decoded));assert(decoded.speedKmh==60);cJSON_Delete(j);
 std::cout<<"PASS: >= boundary, offset, unknown/stale, warning cooldown, UTF-8 fragmentation, oversized recovery, HLP distances and ordering\n";
}
