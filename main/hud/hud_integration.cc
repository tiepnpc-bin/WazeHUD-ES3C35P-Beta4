#include "hud_integration.h"
#include "bluetooth/ble_transport.h"
#include "protocol/hlp_protocol.h"
#include "state/hud_state_store.h"
#include "state/overspeed.h"
#include "audio/alert_audio_policy.h"
#include "audio/hud_voices.h"
#include "application.h"
#include "board.h"
#include "settings.h"
#include "display.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "lvgl_font.h"
#include "assets/lang_config.h"
#include <esp_lvgl_port.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <esp_check.h>
#include <lvgl.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace waze_hud {
namespace {
lv_obj_t *root, *panel, *speed, *limit, *speed_sign, *turn, *second_turn, *distance, *eta, *alert, *street, *link, *mini,
    *mode_label, *lanes;
std::atomic<int> sound_mode{2};
bool showing_hud = true;
audio::AlertAudioPolicy policy;
uint64_t next_sound = 0;
uint64_t Now() { return esp_timer_get_time()/1000; }
bool Valid(const HudState& s) { return s.connected && s.hasProducerState && !s.signalStale; }
void SetLabel(lv_obj_t* obj,const char* value) {
 if(std::strcmp(lv_label_get_text(obj),value)!=0)lv_label_set_text(obj,value);
}
const char* TurnName(Maneuver m) {
 switch(m) {
 case Maneuver::Left: case Maneuver::SharpLeft: return "RE TRAI";
 case Maneuver::Right: case Maneuver::SharpRight: return "RE PHAI";
 case Maneuver::SlightLeft: case Maneuver::KeepLeft: return "CHECH TRAI";
 case Maneuver::SlightRight: case Maneuver::KeepRight: return "CHECH PHAI";
 case Maneuver::UTurn: return "QUAY DAU";
 case Maneuver::Roundabout: case Maneuver::RoundaboutLeft: case Maneuver::RoundaboutRight: return "VONG XUYEN";
 case Maneuver::ExitLeft: return "LOI RA TRAI";
 case Maneuver::ExitRight: return "LOI RA PHAI";
 case Maneuver::Arrive: return "DEN NOI";
 default: return "DI THANG";
 }
}
const char* AlertName(AlertKind k) {
 switch(k) {
 case AlertKind::None: return "";
 case AlertKind::SpeedCamera: return "CAMERA TOC DO";
 case AlertKind::RedLightCamera: return "CAMERA DEN DO";
 case AlertKind::DistanceCamera: return "DO TOC DO TB";
 case AlertKind::PhoneCamera: return "CAMERA DIEN THOAI";
 case AlertKind::SeatbeltCamera: return "CAMERA DAY AN TOAN";
 case AlertKind::DummyCamera: return "CAMERA PHIA TRUOC";
 case AlertKind::BusLaneCamera: return "CAMERA LAN XE BUYT";
 case AlertKind::NoiseCamera: return "CAMERA TIENG ON";
 case AlertKind::StopSignCamera: return "CAMERA BIEN STOP";
 case AlertKind::NoPassing: return "CAM VUOT";
 case AlertKind::EndNoPassing: return "HET CAM VUOT";
 case AlertKind::SpeedDrop: return "GIAM TOC DO";
 case AlertKind::Railway: return "DUONG SAT";
 case AlertKind::Police: return "CANH SAT";
 case AlertKind::RoadClosed: return "DUONG DONG";
 case AlertKind::Accident: return "TAI NAN";
 case AlertKind::TrafficJam: return "KET XE";
 case AlertKind::Construction: return "CONG TRUONG";
 case AlertKind::Pothole: return "O GA";
 case AlertKind::Flood: return "NGAP NUOC";
 case AlertKind::SlipperyRoad: return "DUONG TRON";
 case AlertKind::SchoolZone: return "KHU VUC TRUONG HOC";
 case AlertKind::EndSpeedRestriction: return "HET HAN CHE TOC DO";
 default: return "CANH BAO PHIA TRUOC";
 }
}
void FormatDistance(int meters, char* out, size_t n) {
 if(meters < 0) std::snprintf(out,n,"--");
 else if(meters < 1000) std::snprintf(out,n,"%d m",meters);
 else std::snprintf(out,n,"%.1f km",meters/1000.0);
}
void FormatLane(const LaneState& lane, char* out, size_t n) {
 // HLP carries the supported and selected maneuvers as bit masks. Keep the
 // complete mask visible instead of discarding it: the UI can therefore
 // distinguish lanes with different directions even when more than one
 // maneuver is allowed for the same lane.
 std::snprintf(out, n, "[%c:%02X]", lane.selectedMask ? '+' : '-',
               static_cast<unsigned>(lane.directionMask));
}
lv_obj_t* Label(lv_obj_t* parent,int x,int y,int w,const lv_font_t* font) {
 auto* obj=lv_label_create(parent);lv_obj_set_pos(obj,x,y);lv_obj_set_width(obj,w);
 lv_obj_set_style_text_font(obj,font,0);lv_obj_set_style_text_color(obj,lv_color_white(),0);
 lv_label_set_long_mode(obj,LV_LABEL_LONG_CLIP);return obj;
}
void Button(lv_obj_t* parent, const char* title,int x,int y,int w,lv_event_cb_t cb) {
 auto* btn=lv_button_create(parent);lv_obj_set_pos(btn,x,y);lv_obj_set_size(btn,w,36);
 lv_obj_add_event_cb(btn,cb,LV_EVENT_CLICKED,nullptr);
 auto* text=lv_label_create(btn);SetLabel(text,title);lv_obj_center(text);
}
void Toggle(lv_event_t*) { showing_hud=!showing_hud; }
void Rotate(lv_event_t*) { DeviceConfig::instance().toggleRotation(); }
void Talk(lv_event_t*) {
 if(Now()<next_sound)return;
 Application::GetInstance().ToggleChatState();
}
void Sound(lv_event_t*) {
 int mode=(sound_mode.load()+1)%3;sound_mode.store(mode);
 Settings cfg("hud_audio",true);cfg.SetInt("mode",mode);
}
void Tick(lv_timer_t*) {
 auto s=HudStateStore::instance().snapshot();auto cfg=DeviceConfig::instance().snapshot();
 cfg.audioMode=static_cast<AudioMode>(sound_mode.load());
 const bool valid=Valid(s);char tmp[192],d[24];
 // The HUD resides in LVGL's top layer, so it is already above the Xiaozhi
 // widgets.  Reordering it and rewriting full-screen styles every 250 ms
 // invalidated the complete QSPI framebuffer and was visible as flashing on
 // the ST77922.  Change visibility only when the user actually toggles mode.
 static bool applied_showing_hud = true;
 if (applied_showing_hud != showing_hud) {
  if (showing_hud) lv_obj_remove_flag(root, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
  applied_showing_hud = showing_hud;
 }
  static int applied_rotation=-1;
  if(applied_rotation!=int(cfg.rotateDisplay)){
   lv_display_set_rotation(lv_display_get_default(),cfg.rotateDisplay?LV_DISPLAY_ROTATION_270:LV_DISPLAY_ROTATION_90);
   applied_rotation=int(cfg.rotateDisplay);
  }
 static int applied_brightness=-1;
 if(applied_brightness!=cfg.brightness){Board::GetInstance().GetBacklight()->SetBrightness(cfg.brightness,false);applied_brightness=cfg.brightness;}

 SetLabel(link,!s.connected?"BLE: CHUA KET NOI":!valid?"BLE: CHO DU LIEU":"BLE: WAZEMOD");
 if(valid) {
  std::snprintf(tmp,sizeof(tmp),"%d",s.speedKmh);SetLabel(speed,tmp);
  if(s.speedLimitKmh>0) {
   std::snprintf(tmp,sizeof(tmp),"%d",s.speedLimitKmh);
   lv_obj_remove_flag(speed_sign,LV_OBJ_FLAG_HIDDEN);
  } else {
   tmp[0]='\0';
   // Do not draw a non-standard teal/question-mark sign when Waze has not
   // supplied a speed limit yet.
   lv_obj_add_flag(speed_sign,LV_OBJ_FLAG_HIDDEN);
  }
  SetLabel(limit,tmp);
  lv_obj_set_style_text_color(speed,lv_color_hex((s.speedLimitKmh>0 && s.speedKmh>s.speedLimitKmh)?0xff3030:0xffffff),0);
  SetLabel(turn,s.navigationActive?TurnName(s.maneuver):"CHUA DAN DUONG");
  SetLabel(second_turn, s.navigationActive && s.secondManeuver != Maneuver::None
                            ? TurnName(s.secondManeuver) : "");
  FormatDistance(s.maneuverDistanceM,d,sizeof(d));SetLabel(distance,s.navigationActive?d:"");
  if (s.navigationActive && s.eta[0] != '\0') std::snprintf(tmp, sizeof(tmp), "ETA %s", s.eta.data());
  else tmp[0] = '\0';
  SetLabel(eta, tmp);
  FormatDistance(s.nearestAlert.distanceM,d,sizeof(d));
  if(s.nearestAlert.kind!=AlertKind::None)std::snprintf(tmp,sizeof(tmp),"%s  %s",AlertName(s.nearestAlert.kind),d);
  else if(s.noPassingZone)std::snprintf(tmp,sizeof(tmp),"CAM VUOT");else tmp[0]=0;
  SetLabel(alert,tmp);
  SetLabel(street,cfg.showStreet?s.currentStreet.data():"");
  char lane_text[100]{};size_t used=0;
  for(unsigned i=0;i<s.laneCount && i<s.lanes.size() && used+8<sizeof(lane_text);++i)
   { char lane[8]{}; FormatLane(s.lanes[i], lane, sizeof(lane));
    const int written=std::snprintf(lane_text+used,sizeof(lane_text)-used,"%s ",lane);
    if(written<0)break;
    used+=std::min<size_t>(static_cast<size_t>(written),sizeof(lane_text)-used-1); }
  SetLabel(lanes,lane_text);
  std::snprintf(tmp,sizeof(tmp),"%d km/h | %s",s.speedKmh,overspeedActive(s,cfg)?"CANH BAO":"WazeMod");
 } else {
  SetLabel(speed,"--");SetLabel(limit,"");lv_obj_add_flag(speed_sign,LV_OBJ_FLAG_HIDDEN);SetLabel(turn,"CHO WAZEMOD");
  SetLabel(second_turn,"");SetLabel(distance,"");SetLabel(eta,"");SetLabel(alert,"");SetLabel(street,"");SetLabel(lanes,"");
  std::snprintf(tmp,sizeof(tmp),"HUD: CHUA CO DU LIEU");
 }
 SetLabel(mini,tmp);
 SetLabel(mode_label,cfg.audioMode==AudioMode::Off?"AM: TAT":cfg.audioMode==AudioMode::Beep?"AM: BIP":"AM: DOC");
 // All UI/config updates here run under LVGL. Audio scheduling is nonblocking;
 // Ogg/Opus uses the same codec owner as AI (never another I2S instance).
 auto event=policy.poll(s,cfg,Now());
 if(event.clip!=audio::Clip::None && Now()>=next_sound) {
  std::string_view sound=cfg.audioMode==AudioMode::Beep?Lang::Sounds::OGG_EXCLAMATION:HudVoice(static_cast<unsigned>(event.clip),event.valueKmh);
  if(!sound.empty()) {
   Application::GetInstance().PlayHudWarning(sound);
   next_sound=Now()+7000;
   policy.finished(Now()+5000);
  }
 }
}
}
esp_err_t StartHud() {
 if(!HudStateStore::instance().init())return ESP_ERR_NO_MEM;
 ESP_RETURN_ON_ERROR(DeviceConfig::instance().init(),"HUD","Config init");
 // Start protocol before advertising so the very first connection is handled.
 ESP_RETURN_ON_ERROR(HlpProtocol::instance().start(),"HUD","Protocol init");
 ESP_RETURN_ON_ERROR(BleTransport::instance().init(),"HUD","BLE init");
 Settings settings("hud_audio");sound_mode.store(std::clamp<int>(settings.GetInt("mode",2),0,2));
 // Keep the native Xiaozhi screen dark as well.  The HUD itself is an opaque
 // black top-layer surface, but selecting the dark theme removes white frames
 // during an intentional HUD/AI mode switch.
 if (auto* dark_theme = LvglThemeManager::GetInstance().GetTheme("dark"); dark_theme != nullptr) {
  Board::GetInstance().GetDisplay()->SetTheme(dark_theme);
 }
 if(!lvgl_port_lock(1000))return ESP_ERR_TIMEOUT;
 lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
 lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
 auto* layer=lv_layer_top();
 root=lv_obj_create(layer);lv_obj_remove_style_all(root);lv_obj_set_pos(root,0,0);lv_obj_set_size(root,480,320);
 lv_obj_set_style_bg_color(root,lv_color_black(),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
 lv_obj_set_style_border_width(root,0,0);lv_obj_set_style_pad_all(root,0,0);lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
 panel=lv_obj_create(root);lv_obj_remove_style_all(panel);lv_obj_set_pos(panel,0,44);lv_obj_set_size(panel,480,276);
 lv_obj_set_style_bg_color(panel,lv_color_black(),0);lv_obj_set_style_bg_opa(panel,LV_OPA_COVER,0);
 lv_obj_set_style_border_width(panel,0,0);lv_obj_set_style_radius(panel,0,0);lv_obj_set_style_pad_all(panel,0,0);
 lv_obj_remove_flag(panel,LV_OBJ_FLAG_SCROLLABLE);
 link=Label(panel,12,4,330,&lv_font_montserrat_20);
 speed=Label(panel,20,42,180,&lv_font_montserrat_48);
 auto* unit=Label(panel,24,102,150,&lv_font_montserrat_20);SetLabel(unit,"km/h");
 speed_sign=lv_obj_create(panel);lv_obj_remove_style_all(speed_sign);lv_obj_set_pos(speed_sign,174,24);lv_obj_set_size(speed_sign,156,156);
 // International/Vietnamese maximum-speed sign: white face, red circular
 // border and black numeral.  Local styles keep it independent of AI theme.
 lv_obj_set_style_radius(speed_sign,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(speed_sign,lv_color_white(),0);
 lv_obj_set_style_bg_opa(speed_sign,LV_OPA_COVER,0);lv_obj_set_style_border_color(speed_sign,lv_color_hex(0xE02020),0);
 lv_obj_set_style_border_width(speed_sign,10,0);lv_obj_set_style_pad_all(speed_sign,0,0);lv_obj_remove_flag(speed_sign,LV_OBJ_FLAG_SCROLLABLE);
 limit=Label(speed_sign,0,48,156,&lv_font_montserrat_48);lv_obj_set_style_text_align(limit,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_color(limit,lv_color_black(),0);
 turn=Label(panel,341,40,130,&lv_font_montserrat_20);distance=Label(panel,341,75,130,&lv_font_montserrat_24);
 second_turn=Label(panel,341,108,130,&lv_font_montserrat_20);eta=Label(panel,341,132,130,&lv_font_montserrat_20);
 alert=Label(panel,12,164,455,&lv_font_montserrat_20);lv_obj_set_style_text_color(alert,lv_color_hex(0xffc040),0);
 street=Label(panel,12,188,455,&lv_font_montserrat_20);
 auto theme=Board::GetInstance().GetDisplay()->GetTheme();
 if(theme && theme->GetTextFont())lv_obj_set_style_text_font(street,theme->GetTextFont()->font(),0);
 lanes=Label(panel,12,212,455,&lv_font_montserrat_20);
 mode_label=Label(panel,12,240,150,&lv_font_montserrat_20);
 mini=Label(root,8,5,265,&lv_font_montserrat_20);
 Button(root,"HUD / AI",280,0,100,Toggle);Button(root,"NOI",390,0,80,Talk);
 Button(panel,"XOAY",180,233,110,Rotate);
 Button(panel,"AM THANH",305,233,155,Sound);
 lv_timer_create(Tick,250,nullptr);
 lvgl_port_unlock();
 return ESP_OK;
}
}
