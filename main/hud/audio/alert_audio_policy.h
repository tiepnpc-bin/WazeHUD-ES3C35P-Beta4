#pragma once
#include "state/overspeed.h"
#include <array>
#include <cstdint>
namespace waze_hud::audio {
enum class Clip : uint8_t { OverSpeed, SpeedCamera, RedLightCamera, DistanceCamera, PhoneCamera, SeatbeltCamera, SpeedDrop, Residential, NoPassing, ProhibitedRoad, Railway, Hazard, CurrentLimit, UpcomingLimit, UpcomingEndLimit, EndLimit, None=255 };
inline bool supportedLimit(int value){return value>=20&&value<=120&&value%10==0;}
inline Clip clipFor(AlertKind k){
 switch(k){
  case AlertKind::SpeedCamera:return Clip::SpeedCamera;
  case AlertKind::RedLightCamera:return Clip::RedLightCamera;
  case AlertKind::DistanceCamera:return Clip::DistanceCamera;
  case AlertKind::PhoneCamera:return Clip::PhoneCamera;
  case AlertKind::SeatbeltCamera:return Clip::SeatbeltCamera;
  // WazeMod reports these camera subclasses on different roads/regions.
  // They are all safety cameras and must retain the same bounded warning
  // policy as an ordinary speed camera instead of silently falling through.
  case AlertKind::DummyCamera:
  case AlertKind::BusLaneCamera:
  case AlertKind::NoiseCamera:
  case AlertKind::StopSignCamera:return Clip::SpeedCamera;
  case AlertKind::SpeedDrop:return Clip::SpeedDrop;
  case AlertKind::ResidentialStart:return Clip::Residential;
  case AlertKind::NoPassing:return Clip::NoPassing;
  case AlertKind::ProhibitedRoad:return Clip::ProhibitedRoad;
  case AlertKind::Railway:return Clip::Railway;
  case AlertKind::EndSpeedRestriction:return Clip::UpcomingEndLimit;
  case AlertKind::Hazard:case AlertKind::DangerousRoad:case AlertKind::DangerousCurve:
  case AlertKind::ObjectOnRoad:case AlertKind::Flood:case AlertKind::SlipperyRoad:return Clip::Hazard;
  default:return Clip::None;
 }
}
struct Event { Clip clip{Clip::None};AlertKind kind{AlertKind::None};int valueKmh{0}; };
inline bool valid(const HudState&s){return s.connected&&s.hasProducerState&&!s.signalStale;}
inline bool present(const HudState&s,AlertKind kind){
 if(s.nearestAlert.kind==kind&&s.nearestAlert.distanceM>=0&&s.nearestAlert.distanceM<=500)return true;
 for(unsigned i=0;i<s.upcomingAlertCount&&i<s.upcomingAlerts.size();++i)
  if(s.upcomingAlerts[i].kind==kind&&s.upcomingAlerts[i].distanceM>=0&&s.upcomingAlerts[i].distanceM<=500)return true;
 return false;
}
inline bool eventPresent(const HudState&s,const Event&e){
 if(e.clip==Clip::CurrentLimit)return s.speedLimitKmh==e.valueKmh;
 auto matches=[&](const AlertState&a){
  if(a.kind!=e.kind||a.distanceM<0||a.distanceM>500)return false;
  if(e.clip==Clip::UpcomingLimit&&a.valueKmh!=e.valueKmh)return false;
  if(e.clip==Clip::UpcomingEndLimit&&a.distanceM==0)return false;
  if(e.clip==Clip::EndLimit&&a.distanceM!=0)return false;
  return true;
 };
 if(matches(s.nearestAlert))return true;
 for(unsigned i=0;i<s.upcomingAlertCount&&i<s.upcomingAlerts.size();++i)if(matches(s.upcomingAlerts[i]))return true;
 return false;
}
class AlertAudioPolicy {
 struct Track{uint64_t seen=0,spoken=0;int distance=-1,value=0;uint8_t stages=0;bool known=false,said=false;};
 std::array<Track,76> tracks_{};
 bool over_=false,saidOver_=false;uint64_t overSince_=0,lastOver_=0,next_=0;uint32_t session_=0;bool sessionKnown_=false;
 int candidateLimit_=0,spokenLimit_=0;uint64_t limitSince_=0;
public:
 void finished(uint64_t now){next_=now+2000;}
 Event poll(const HudState&s,const DeviceSettings&cfg,uint64_t now){
  if(!valid(s)){*this={};return {};}
  if(!sessionKnown_||session_!=s.sessionId){*this={};sessionKnown_=true;session_=s.sessionId;}
  const int limit=supportedLimit(s.speedLimitKmh)?s.speedLimitKmh:0;
  if(limit!=candidateLimit_){candidateLimit_=limit;limitSince_=now;}
  // Unknown/unsupported is not an end-of-restriction sign. Do not invent speech.
  if(limit==0)spokenLimit_=0;
  const bool excess=overspeedActive(s,cfg);
  if(!excess){over_=false;saidOver_=false;}
  else if(!over_){over_=true;overSince_=now;}
  // Observe even while muted/stationary; no backlog of old spoken messages.
  std::array<AlertState,4> alerts{};alerts[0]=s.nearestAlert;
  for(unsigned i=0;i<s.upcomingAlertCount&&i<3;++i)alerts[i+1]=s.upcomingAlerts[i];
  for(unsigned i=0;i<alerts.size();++i){
   for(unsigned j=0;j<i;++j)if(alerts[i].kind!=AlertKind::None&&alerts[i].kind==alerts[j].kind){
    // Prefer the nearest valid occurrence; do not discard a closer warning.
    if(alerts[i].distanceM>=0&&(alerts[j].distanceM<0||alerts[i].distanceM<alerts[j].distanceM))
      alerts[j].kind=AlertKind::None;
    else alerts[i].kind=AlertKind::None;
   }
  }
  for(const auto&a:alerts){const unsigned k=static_cast<unsigned>(a.kind);if(k==0||k>=tracks_.size()||a.distanceM<0)continue;
   auto&t=tracks_[k];
   if(!t.known||now-t.seen>15000||a.distanceM>t.distance+250||
      (a.kind==AlertKind::SpeedDrop&&a.valueKmh!=t.value)){t={};t.known=true;}
   t.seen=now;t.distance=a.distanceM;t.value=a.valueKmh;
  }
  if(cfg.audioMode==AudioMode::Off||adjustedSpeed(s.speedKmh,cfg)<=0||now<next_)return {};
  if(excess&&now-overSince_>=1500&&(!saidOver_||now-lastOver_>=30000)){
   saidOver_=true;lastOver_=now;return {Clip::OverSpeed,AlertKind::None};
  }
  for(const auto&a:alerts){
   Clip clip=clipFor(a.kind);if(clip==Clip::None||a.distanceM<0||a.distanceM>500)continue;
   if(a.kind==AlertKind::SpeedDrop&&supportedLimit(a.valueKmh))clip=Clip::UpcomingLimit;
   if(a.kind==AlertKind::EndSpeedRestriction&&a.distanceM==0)clip=Clip::EndLimit;
   auto&t=tracks_[static_cast<unsigned>(a.kind)];
   const uint8_t stage=clip==Clip::EndLimit?4:a.distanceM<=150?2:1;
   if((t.stages&stage)||(stage==1&&(t.stages&6))||(t.said&&stage!=4&&now-t.spoken<10000))continue;
   t.stages|=stage;t.said=true;t.spoken=now;return {clip,a.kind,a.valueKmh};
  }
  if(!excess&&limit>0&&limit!=spokenLimit_&&now-limitSince_>=2000){
   spokenLimit_=limit;return {Clip::CurrentLimit,AlertKind::None,limit};
  }
  return {};
 }
};
}
