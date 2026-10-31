#include "hud_integration.h"
#include "bluetooth/ble_transport.h"
#include "protocol/hlp_protocol.h"
#include "state/hud_state_store.h"
#include "state/overspeed.h"
#include "audio/alert_audio_policy.h"
#include "audio/hud_voices.h"
#include "audio/audio_codec.h"
#include "application.h"
#include "board.h"
#include "wifi_board.h"
#include "sdkconfig.h"
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
    *mode_label, *lanes, *alert_card, *alert_icon, *alert_distance, *nav_arrow, *unit_label, *divider;
std::atomic<int> sound_mode{2};
bool showing_hud = true;
bool demo_mode = false;
audio::AlertAudioPolicy policy;
uint64_t next_sound = 0;
uint64_t Now() { return esp_timer_get_time()/1000; }
bool Valid(const HudState& s) { return s.connected && s.hasProducerState && !s.signalStale; }
bool IsDayTheme(const DeviceSettings& cfg) { return cfg.theme == UiTheme::Day; }
HudState DemoState() {
 HudState s{};
 s.connected=true; s.hasProducerState=true; s.navigationActive=true;
 s.speedKmh=52; s.speedLimitKmh=60; s.maneuver=Maneuver::Continue;
 s.maneuverDistanceM=183; s.remainingMinutes=8;
 std::snprintf(s.currentStreet.data(),s.currentStreet.size(),"BACH THANH PHONG - CHE DO XEM THU");
 std::snprintf(s.nextStreet.data(),s.nextStreet.size(),"DUONG PHIA TRUOC");
 std::snprintf(s.eta.data(),s.eta.size(),"17:32");
 s.nearestAlert.kind=AlertKind::SpeedCamera; s.nearestAlert.distanceM=200;
 s.laneCount=3;
 s.lanes[0]={0x02,0x00}; s.lanes[1]={0x01,0x01}; s.lanes[2]={0x04,0x00};
 return s;
}
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
const char* TurnArrow(Maneuver m) {
 switch(m) {
 case Maneuver::Left: case Maneuver::SlightLeft: case Maneuver::SharpLeft:
 case Maneuver::KeepLeft: case Maneuver::ExitLeft: return "<";
 case Maneuver::Right: case Maneuver::SlightRight: case Maneuver::SharpRight:
 case Maneuver::KeepRight: case Maneuver::ExitRight: return ">";
 case Maneuver::UTurn: return "U";
 case Maneuver::Roundabout: case Maneuver::RoundaboutLeft:
 case Maneuver::RoundaboutRight: return "O";
 default: return "^";
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
// The ST77922 panel transfer uses byte-swapped RGB565 to keep glyph edges
// sharp.  Black and white survive that operation unchanged, but coloured
// pixels do not.  Convert every intended accent colour to its pre-swapped
// RGB565 representation here, so the physical display shows the standard
// traffic-sign colours rather than cyan/magenta/yellow substitutions.
lv_color_t HudAccent(uint32_t rgb) {
 const uint16_t intended = static_cast<uint16_t>(((rgb >> 8) & 0xf800) |
                                                  ((rgb >> 5) & 0x07e0) |
                                                  ((rgb >> 3) & 0x001f));
 // ST77922 is configured in BGR mode (MADCTL.BGR).  Reverse red/blue before
 // byte preparation; the controller then restores the requested RGB colour.
 const uint16_t bgr = static_cast<uint16_t>(((intended & 0x001f) << 11) |
                                             (intended & 0x07e0) |
                                             ((intended & 0xf800) >> 11));
 const uint16_t preswapped = static_cast<uint16_t>((bgr << 8) | (bgr >> 8));
 const uint8_t r = static_cast<uint8_t>(((preswapped >> 11) & 0x1f) * 255 / 31);
 const uint8_t g = static_cast<uint8_t>(((preswapped >> 5) & 0x3f) * 255 / 63);
 const uint8_t b = static_cast<uint8_t>((preswapped & 0x1f) * 255 / 31);
 return lv_color_make(r, g, b);
}
bool IsWarningSign(AlertKind k);
bool IsRegulatorySign(AlertKind k);
bool IsDirectionSign(AlertKind k);
lv_color_t AlertColor(AlertKind k) {
 if (IsWarningSign(k) || IsRegulatorySign(k)) return lv_color_black();
 switch(k) {
 case AlertKind::SpeedCamera: case AlertKind::RedLightCamera:
 case AlertKind::DistanceCamera: case AlertKind::PhoneCamera:
 case AlertKind::SeatbeltCamera: case AlertKind::DummyCamera:
 case AlertKind::BusLaneCamera: case AlertKind::NoiseCamera:
 case AlertKind::StopSignCamera:
  return lv_color_white(); // camera card: dark face, red border
 case AlertKind::Police: return lv_color_white();
 case AlertKind::TrafficJam: return lv_color_white();
 case AlertKind::Hazard: case AlertKind::Accident: case AlertKind::RoadClosed:
 case AlertKind::Construction: case AlertKind::Pothole: case AlertKind::Flood:
 case AlertKind::SlipperyRoad: case AlertKind::Railway: case AlertKind::SchoolZone:
  return lv_color_black(); // yellow warning face
 default: return lv_color_white();
 }
}
bool IsWarningSign(AlertKind k) {
 switch (k) {
 case AlertKind::Hazard: case AlertKind::Accident: case AlertKind::RoadClosed:
 case AlertKind::Construction: case AlertKind::Pothole: case AlertKind::Flood:
 case AlertKind::SlipperyRoad: case AlertKind::Railway: case AlertKind::SchoolZone:
 case AlertKind::Animal: case AlertKind::ObjectOnRoad: case AlertKind::Roadkill:
 case AlertKind::Fog: case AlertKind::Hail: case AlertKind::Snow: case AlertKind::Ice:
 case AlertKind::SpeedBump: case AlertKind::LanesMerging: case AlertKind::DangerousCurve:
 case AlertKind::Fork: case AlertKind::Cyclist: case AlertKind::EmergencyVehicle:
  return true;
 default: return false;
 }
}
bool IsRegulatorySign(AlertKind k) {
 switch (k) {
 case AlertKind::NoPassing: case AlertKind::NoCar: case AlertKind::NoMotorcycle:
 case AlertKind::NoLeftTurn: case AlertKind::NoRightTurn: case AlertKind::NoUTurn:
 case AlertKind::NoStraight: case AlertKind::ProhibitedRoad:
 case AlertKind::CombinedTurnRestriction: case AlertKind::NoStraightAndRight:
 case AlertKind::NoLeftAndUTurn: case AlertKind::NoStraightAndLeft:
 case AlertKind::NoLeftAndRight: case AlertKind::CarNoLeftAndUTurn:
 case AlertKind::CarNoRightAndUTurn: case AlertKind::NoRightAndUTurn:
 case AlertKind::CarNoLeftTurn: case AlertKind::CarNoRightTurn:
 case AlertKind::CarNoUTurn:
  return true;
 default: return false;
 }
}
bool IsDirectionSign(AlertKind k) {
 switch (k) {
 case AlertKind::MandatoryStraight: case AlertKind::MandatoryRight:
 case AlertKind::MandatoryLeft: case AlertKind::CarLane: case AlertKind::MotorcycleLane:
 case AlertKind::OneWay: case AlertKind::ExpresswayExit: case AlertKind::ExpresswayRestStop:
 case AlertKind::RestStop: case AlertKind::TollBooth:
  return true;
 default: return false;
 }
}
lv_color_t AlertBackground(AlertKind k) {
 if (IsWarningSign(k)) return HudAccent(0xffd740);       // warning: yellow face
 if (IsRegulatorySign(k)) return lv_color_white();        // restriction: white face
 if (IsDirectionSign(k)) return HudAccent(0x1267c5);      // direction: blue face
 if (k == AlertKind::TrafficJam) return HudAccent(0xd84315);
 return lv_color_black();                                 // camera/police: dark card
}
lv_color_t AlertBorderColor(AlertKind k) {
 switch(k) {
 case AlertKind::SpeedCamera: case AlertKind::RedLightCamera:
 case AlertKind::DistanceCamera: case AlertKind::PhoneCamera:
 case AlertKind::SeatbeltCamera: case AlertKind::DummyCamera:
 case AlertKind::BusLaneCamera: case AlertKind::NoiseCamera:
 case AlertKind::StopSignCamera: return HudAccent(0xe02020); // enforcement: red
 case AlertKind::Police: return HudAccent(0x318cff);          // information: blue
 case AlertKind::TrafficJam: return HudAccent(0xd84315);
 default:
  if (IsWarningSign(k) || IsRegulatorySign(k)) return HudAccent(0xe02020); // red rim
  if (IsDirectionSign(k)) return HudAccent(0x1267c5);
  return HudAccent(0x318cff);
 }
}
void FormatDistance(int meters, char* out, size_t n) {
 if(meters < 0) std::snprintf(out,n,"--");
 else if(meters < 1000) std::snprintf(out,n,"%d m",meters);
 else std::snprintf(out,n,"%.1f km",meters/1000.0);
}
void FormatLane(const LaneState& lane, char* out, size_t n) {
 // A compact forward-arrow representation stays legible at driving distance.
 // The selected route lane has a star; raw bit masks are implementation
 // details and should not occupy the road-facing HUD.
 std::snprintf(out, n, lane.selectedMask ? "^*" : "^");
}
lv_obj_t* Label(lv_obj_t* parent,int x,int y,int w,const lv_font_t* font) {
 auto* obj=lv_label_create(parent);lv_obj_set_pos(obj,x,y);lv_obj_set_width(obj,w);
 lv_obj_set_style_text_font(obj,font,0);lv_obj_set_style_text_color(obj,lv_color_white(),0);
 lv_label_set_long_mode(obj,LV_LABEL_LONG_CLIP);return obj;
}
void Button(lv_obj_t* parent, const char* title,int x,int y,int w,lv_event_cb_t cb) {
 auto* btn=lv_button_create(parent);lv_obj_set_pos(btn,x,y);lv_obj_set_size(btn,w,28);
 lv_obj_add_event_cb(btn,cb,LV_EVENT_CLICKED,nullptr);
 // Muted amber is readable at night without the hard white button blocks.
 // Use HudAccent so the desired colour survives ST77922 byte swapping.
 lv_obj_set_style_bg_color(btn,HudAccent(0xb96518),0);
 lv_obj_set_style_bg_opa(btn,LV_OPA_COVER,0);
 lv_obj_set_style_border_color(btn,HudAccent(0xf0a34b),0);
 lv_obj_set_style_border_width(btn,1,0);
 auto* text=lv_label_create(btn);SetLabel(text,title);lv_obj_set_style_text_color(text,lv_color_white(),0);lv_obj_center(text);
}
// Xiaozhi can apply its theme after the HUD has been created.  Repair the
// opaque surfaces only when a theme actually changed them; rewriting these
// styles on every timer tick invalidates the full QSPI frame and flickers.
void EnsureHudSurface(bool day) {
 const lv_color_t background = day ? lv_color_hex(0xf4f7fb) : lv_color_black();
 const auto ensure_background = [background](lv_obj_t* object) {
  if (object == nullptr) return;
  if (lv_obj_get_style_bg_opa(object, LV_PART_MAIN) != LV_OPA_COVER)
   lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
  if (lv_color_to_u32(lv_obj_get_style_bg_color(object, LV_PART_MAIN)) !=
      lv_color_to_u32(background))
   lv_obj_set_style_bg_color(object, background, 0);
 };
 // The base Xiaozhi screen remains visible whenever HUD is intentionally
 // hidden.  Keep it dark as well: changing only the overlay made the AI
 // transition reveal the generated light theme, which was mistaken for a
 // backlight problem on the ES3C35P.
 // Keep the underlying Xiaozhi screen dark.  Only the opaque HUD overlay is
 // switched to day colours; hiding it must never reveal a white AI frame.
 if (!day) ensure_background(lv_screen_active());
 ensure_background(root);
 ensure_background(panel);
}
void Toggle(lv_event_t*) {
 showing_hud=!showing_hud;
#if CONFIG_BOARD_TYPE_LCDWIKI_ES3C35P
 // Do not create a SoftAP from the live HUD.  On this board BLE, the QSPI
 // renderer and the audio AFE leave too little contiguous internal RAM for
 // ESP-IDF's SoftAP beacon buffer; the resulting allocation failure resets
 // the CPU and disconnects WazeMod.  AI mode may use an already-connected
 // station, while first-time provisioning must use the dedicated boot path.
 // This button is therefore strictly a HUD/AI view switch.
#endif
}
void Rotate(lv_event_t*) { DeviceConfig::instance().toggleRotation(); }
void Theme(lv_event_t*) { DeviceConfig::instance().toggleTheme(); }
void Demo(lv_event_t*) {
 demo_mode=!demo_mode;
 HudStateStore::instance().refresh();
 // Demo has no live Waze/BLE session. PlayHudWarning deliberately rejects
 // non-live sessions, therefore the explicit local demo test plays the voice
 // through the regular audio path.
 if (demo_mode && sound_mode.load() == static_cast<int>(AudioMode::Voice)) {
  const auto voice = HudVoice(static_cast<unsigned>(audio::Clip::SpeedCamera), 0);
  if (!voice.empty()) Application::GetInstance().PlaySound(voice);
 }
}
void Brightness(lv_event_t*) { DeviceConfig::instance().cycleBrightness(); }
void Talk(lv_event_t*) {
 if(Now()<next_sound)return;
 Application::GetInstance().ToggleChatState();
}
void Sound(lv_event_t*) {
 int mode=(sound_mode.load()+1)%3;sound_mode.store(mode);
 Settings cfg("hud_audio",true);cfg.SetInt("mode",mode);
 // This is also a local speaker health check.  The former button only changed
 // the stored mode, so a disconnected AI session looked exactly like a dead
 // ES8311/amplifier.
 if(mode!=static_cast<int>(AudioMode::Off)) {
  // A previous configuration session can leave the persisted codec gain too
  // low to hear.  Restore a known audible level when the user explicitly
  // selects BEEP or VOICE; OFF remains genuinely silent.
  if (auto* codec = Board::GetInstance().GetAudioCodec(); codec != nullptr && codec->output_volume() < 80)
   codec->SetOutputVolume(80);
  Application::GetInstance().PlaySound(Lang::Sounds::OGG_EXCLAMATION);
 }
}
void Tick(lv_timer_t*) {
 auto s=HudStateStore::instance().snapshot();auto cfg=DeviceConfig::instance().snapshot();
 cfg.audioMode=static_cast<AudioMode>(sound_mode.load());
 bool valid=Valid(s);
 // DEMO is intentionally subordinate to live Waze data: as soon as WazeMod
 // supplies a valid state, the real route and warnings always win.
 const bool showing_demo=demo_mode && !valid;
 if(showing_demo) { s=DemoState(); valid=true; }
 const bool day=IsDayTheme(cfg); char tmp[192],d[24];
 // The HUD resides in LVGL's top layer, so it is already above the Xiaozhi
 // widgets.  Reordering it and rewriting full-screen styles every 250 ms
 // invalidated the complete QSPI framebuffer and was visible as flashing on
 // the ST77922.  Change visibility only when the user actually toggles mode.
 static bool applied_showing_hud = true;
 if (applied_showing_hud != showing_hud) {
  if (showing_hud) {
   EnsureHudSurface(day);
   lv_obj_remove_flag(root, LV_OBJ_FLAG_HIDDEN);
  }
  else lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
  applied_showing_hud = showing_hud;
 }
 EnsureHudSurface(day);
 static int applied_theme=-1;
 if(applied_theme != int(cfg.theme)) {
  const lv_color_t text=day?lv_color_hex(0x111827):lv_color_white();
  for(auto* object : {link,speed,turn,second_turn,distance,eta,street,lanes,mini,mode_label,unit_label})
   lv_obj_set_style_text_color(object,text,0);
  lv_obj_set_style_text_color(alert_icon,text,0);
  lv_obj_set_style_text_color(alert_distance,text,0);
  lv_obj_set_style_bg_color(divider,HudAccent(0x2475d5),0);
  lv_obj_set_style_bg_color(alert_card,day?lv_color_hex(0xffffff):lv_color_hex(0x090b12),0);
  applied_theme=int(cfg.theme);
 }
  static int applied_rotation=-1;
  if(applied_rotation!=int(cfg.rotateDisplay)){
   lv_display_set_rotation(lv_display_get_default(),cfg.rotateDisplay?LV_DISPLAY_ROTATION_270:LV_DISPLAY_ROTATION_90);
   applied_rotation=int(cfg.rotateDisplay);
  }
 static int applied_brightness=-1;
 if(applied_brightness!=cfg.brightness){Board::GetInstance().GetBacklight()->SetBrightness(cfg.brightness,false);applied_brightness=cfg.brightness;}

 SetLabel(link,showing_demo?"HUD: XEM THU":!s.connected?"BLE: CHUA KET NOI":!valid?"BLE: CHO DU LIEU":"BLE: WAZEMOD");
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
  lv_obj_set_style_text_color(speed,lv_color_hex((s.speedLimitKmh>0 && s.speedKmh>s.speedLimitKmh)?0xff3030:(day?0x111827:0xffffff)),0);
  SetLabel(turn,s.navigationActive?TurnName(s.maneuver):"CHUA DAN DUONG");
  SetLabel(nav_arrow,s.navigationActive?TurnArrow(s.maneuver):"");
  SetLabel(second_turn, s.navigationActive && s.secondManeuver != Maneuver::None
                            ? TurnName(s.secondManeuver) : "");
  FormatDistance(s.maneuverDistanceM,d,sizeof(d));SetLabel(distance,s.navigationActive?d:"");
  if (s.navigationActive && s.eta[0] != '\0') std::snprintf(tmp, sizeof(tmp), "ETA %s", s.eta.data());
  else tmp[0] = '\0';
  SetLabel(eta, tmp);
  FormatDistance(s.nearestAlert.distanceM,d,sizeof(d));
  if(s.nearestAlert.kind!=AlertKind::None)std::snprintf(tmp,sizeof(tmp),"%s",AlertName(s.nearestAlert.kind));
  else if(s.noPassingZone)std::snprintf(tmp,sizeof(tmp),"CAM VUOT");else tmp[0]=0;
  SetLabel(alert,tmp);
  SetLabel(alert_distance,tmp[0] != '\0' ? d : "");
  SetLabel(alert_icon,tmp[0] != '\0' ? "CAM" : "");
  lv_obj_set_style_text_color(alert,AlertColor(s.nearestAlert.kind),0);
  lv_obj_set_style_text_color(alert_icon,AlertColor(s.nearestAlert.kind),0);
  lv_obj_set_style_text_color(alert_distance,AlertColor(s.nearestAlert.kind),0);
  lv_obj_set_style_bg_color(alert_card,AlertBackground(s.nearestAlert.kind),0);
  lv_obj_set_style_border_color(alert_card,AlertBorderColor(s.nearestAlert.kind),0);
  if (tmp[0] != '\0') lv_obj_remove_flag(alert_card, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(alert_card, LV_OBJ_FLAG_HIDDEN);
  SetLabel(street,cfg.showStreet?s.currentStreet.data():"");
  char lane_text[100]{};size_t used=0;
  for(unsigned i=0;i<s.laneCount && i<s.lanes.size() && used+8<sizeof(lane_text);++i)
   { char lane[8]{}; FormatLane(s.lanes[i], lane, sizeof(lane));
    const int written=std::snprintf(lane_text+used,sizeof(lane_text)-used,"%s ",lane);
    if(written<0)break;
    used+=std::min<size_t>(static_cast<size_t>(written),sizeof(lane_text)-used-1); }
  SetLabel(lanes,lane_text);
  std::snprintf(tmp,sizeof(tmp),"%d km/h | %s",s.speedKmh,showing_demo?"DEMO":overspeedActive(s,cfg)?"CANH BAO":"WazeMod");
 } else {
  SetLabel(speed,"--");SetLabel(limit,"");lv_obj_add_flag(speed_sign,LV_OBJ_FLAG_HIDDEN);SetLabel(turn,"CHO WAZEMOD");
  SetLabel(nav_arrow,"");SetLabel(second_turn,"");SetLabel(distance,"");SetLabel(eta,"");SetLabel(alert,"");SetLabel(alert_icon,"");SetLabel(alert_distance,"");SetLabel(street,"");SetLabel(lanes,"");
  lv_obj_set_style_text_color(alert,day?lv_color_hex(0x111827):lv_color_white(),0);
  lv_obj_add_flag(alert_card, LV_OBJ_FLAG_HIDDEN);
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
 // Rotate before creating the HUD layer.  Creating a 480x320 overlay while
 // LVGL still has the native 320x480 geometry leaves the uncovered AI screen
 // visible as a white strip until the first timer tick.
 const auto initial_config=DeviceConfig::instance().snapshot();
 lv_display_set_rotation(lv_display_get_default(),initial_config.rotateDisplay?LV_DISPLAY_ROTATION_270:LV_DISPLAY_ROTATION_90);
 lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
 lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
 auto* layer=lv_layer_top();
 root=lv_obj_create(layer);lv_obj_remove_style_all(root);lv_obj_set_pos(root,0,0);lv_obj_set_size(root,480,320);
 lv_obj_set_style_bg_color(root,lv_color_black(),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
 lv_obj_set_style_border_width(root,0,0);lv_obj_set_style_pad_all(root,0,0);lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
 panel=lv_obj_create(root);lv_obj_remove_style_all(panel);lv_obj_set_pos(panel,0,30);lv_obj_set_size(panel,480,290);
 lv_obj_set_style_bg_color(panel,lv_color_black(),0);lv_obj_set_style_bg_opa(panel,LV_OPA_COVER,0);
 lv_obj_set_style_border_width(panel,0,0);lv_obj_set_style_radius(panel,0,0);lv_obj_set_style_pad_all(panel,0,0);
 lv_obj_remove_flag(panel,LV_OBJ_FLAG_SCROLLABLE);
 EnsureHudSurface(false);
 // Dark 480x320 vehicle HUD: limit/speed on the left, navigation in the
 // center, and an alert card on the right.  The layout intentionally mirrors
 // a road HUD instead of the phone-chat screen used by Xiaozhi.
 link=Label(panel,10,2,140,&lv_font_montserrat_20);
 speed=Label(panel,22,128,120,&lv_font_montserrat_48);
 unit_label=Label(panel,45,184,90,&lv_font_montserrat_20);SetLabel(unit_label,"km/h");
 speed_sign=lv_obj_create(panel);lv_obj_remove_style_all(speed_sign);lv_obj_set_pos(speed_sign,18,12);lv_obj_set_size(speed_sign,112,112);
 // International/Vietnamese maximum-speed sign: white face, red circular
 // border and black numeral.  Local styles keep it independent of AI theme.
 lv_obj_set_style_radius(speed_sign,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(speed_sign,lv_color_white(),0);
 lv_obj_set_style_bg_opa(speed_sign,LV_OPA_COVER,0);lv_obj_set_style_border_color(speed_sign,HudAccent(0xE02020),0);
 lv_obj_set_style_border_width(speed_sign,10,0);lv_obj_set_style_pad_all(speed_sign,0,0);lv_obj_remove_flag(speed_sign,LV_OBJ_FLAG_SCROLLABLE);
 limit=Label(speed_sign,0,0,112,&lv_font_montserrat_48);lv_obj_set_style_text_align(limit,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_color(limit,lv_color_black(),0);lv_obj_center(limit);
 divider=lv_obj_create(panel);lv_obj_remove_style_all(divider);lv_obj_set_pos(divider,148,8);lv_obj_set_size(divider,2,246);
 lv_obj_set_style_bg_color(divider,HudAccent(0x2475d5),0);lv_obj_set_style_bg_opa(divider,LV_OPA_COVER,0);
 nav_arrow=Label(panel,222,3,56,&lv_font_montserrat_48);lv_obj_set_style_text_align(nav_arrow,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_color(nav_arrow,HudAccent(0x249bff),0);
 turn=Label(panel,162,55,172,&lv_font_montserrat_20);lv_obj_set_style_text_align(turn,LV_TEXT_ALIGN_CENTER,0);
 distance=Label(panel,162,80,172,&lv_font_montserrat_48);lv_obj_set_style_text_align(distance,LV_TEXT_ALIGN_CENTER,0);
 second_turn=Label(panel,162,134,172,&lv_font_montserrat_20);lv_obj_set_style_text_align(second_turn,LV_TEXT_ALIGN_CENTER,0);
 eta=Label(panel,162,158,172,&lv_font_montserrat_20);lv_obj_set_style_text_align(eta,LV_TEXT_ALIGN_CENTER,0);
 street=Label(panel,158,184,180,&lv_font_montserrat_20);lv_obj_set_style_text_align(street,LV_TEXT_ALIGN_CENTER,0);lv_label_set_long_mode(street,LV_LABEL_LONG_SCROLL_CIRCULAR);
 auto theme=Board::GetInstance().GetDisplay()->GetTheme();
 if(theme && theme->GetTextFont())lv_obj_set_style_text_font(street,theme->GetTextFont()->font(),0);
 lanes=Label(panel,158,218,180,&lv_font_montserrat_20);lv_obj_set_style_text_align(lanes,LV_TEXT_ALIGN_CENTER,0);
 mode_label=Label(panel,16,248,125,&lv_font_montserrat_20);
 alert_card=lv_obj_create(panel);lv_obj_remove_style_all(alert_card);lv_obj_set_pos(alert_card,350,10);lv_obj_set_size(alert_card,118,176);
 lv_obj_set_style_bg_color(alert_card,lv_color_hex(0x090b12),0);lv_obj_set_style_bg_opa(alert_card,LV_OPA_COVER,0);
 lv_obj_set_style_border_color(alert_card,HudAccent(0xe73030),0);lv_obj_set_style_border_width(alert_card,3,0);lv_obj_set_style_radius(alert_card,10,0);
 // The alert card deliberately uses three labels so a camera name and its
 // distance never clip each other on 480x320 displays.
 alert_icon=Label(alert_card,10,10,98,&lv_font_montserrat_20);lv_obj_set_style_text_align(alert_icon,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_color(alert_icon,lv_color_white(),0);
 alert=Label(alert_card,7,50,104,&lv_font_montserrat_20);lv_obj_set_style_text_align(alert,LV_TEXT_ALIGN_CENTER,0);lv_label_set_long_mode(alert,LV_LABEL_LONG_WRAP);lv_obj_set_height(alert,58);lv_obj_set_style_text_color(alert,HudAccent(0xffd040),0);
 alert_distance=Label(alert_card,7,118,104,&lv_font_montserrat_20);lv_obj_set_style_text_align(alert_distance,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_color(alert_distance,lv_color_white(),0);
 lv_obj_add_flag(alert_card,LV_OBJ_FLAG_HIDDEN);
 mini=Label(root,8,4,235,&lv_font_montserrat_20);
 Button(root,"AM",248,0,45,Sound);Button(root,"AI",298,0,45,Toggle);Button(root,"NOI",348,0,58,Talk);Button(root,"XOAY",411,0,60,Rotate);
 Button(panel,"NEN",145,248,60,Theme);Button(panel,"DEMO",212,248,70,Demo);Button(panel,"SANG",289,248,55,Brightness);
 lv_timer_create(Tick,250,nullptr);
 lvgl_port_unlock();
 return ESP_OK;
}
}
