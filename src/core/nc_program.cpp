#include "core/nc_program.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace cnc {
namespace {
std::string number(double n) { std::ostringstream s; s << std::fixed << std::setprecision(3) << n; return s.str(); }
std::string motion(const Json &xyz, bool rapid, double feed) {
  return std::string(rapid ? "G00" : "G01") + " X" + number(xyz[0]) + " Y" + number(xyz[1]) + " Z" + number(xyz[2]) + (rapid ? "" : " F" + number(feed));
}
void position(const Json &xyz) {
  if (!xyz.is_array() || xyz.size()!=3) throw std::runtime_error("invalid NC path position");
  for(int i=0;i<3;++i) if(!xyz[i].is_number() || !std::isfinite(double(xyz[i])) || double(xyz[i])<0 || double(xyz[i])>(i==0?800:500)) throw std::runtime_error("NC path position out of bounds");
}
}
void compile_nc_program(Json &p) {
  p["gcode_lines"]=Json::array(); p["planned_tool_changes"]=0;
  p["gcode_lines"].push_back({{"line",0},{"code","O"+std::to_string(p["id"].get<int>())},{"segment",0},{"tool_id",0}});
  int line=10,segment=0;
  auto emit=[&](const std::string &code,int tool) {
    Json entry={{"line",line},{"code","N"+std::to_string(line)+" "+code},{"segment",segment},{"tool_id",tool}};
    p["gcode_lines"].push_back(entry);line+=10;return entry;
  };
  emit("(SYNTHETIC PREVIEW - NOT FOR A REAL MACHINE)",0);
  emit("G21 G90 G17 G94",0);
  for(auto &s:p["segments"]) {
    ++segment;std::string step=s["step"];int tool=s["tool_id"];
    if(step=="TOOL_SWAP") {p["planned_tool_changes"]=int(p["planned_tool_changes"])+1;s["nc"]=emit("T"+std::to_string(tool)+" M06",tool);}
    else if(step=="RAPID" || step=="CUT" || step=="DRILL") {
      emit(double(s["rpm"])>0 ? "S"+number(s["rpm"])+" M03" : "M05",tool);
      emit(s["coolant"].get<bool>()?"M08":"M09",tool);
      if(s.contains("path")) {
        if((step!="CUT" && step!="DRILL") || !s["path"].is_array() || s["path"].empty() || s["path"].size()>128) throw std::runtime_error("invalid bounded NC path");
        int repeat=s.value("repeat",1);if(repeat<1 || repeat>16) throw std::runtime_error("invalid NC repeat");
        Json prev=s.at("start_xyz_mm");position(prev);
        if(repeat>1 && s["path"].back()["xyz"]!=prev) throw std::runtime_error("repeated NC path must close");
        s["motion_path"]=Json::array();double duration=0;
        for(int n=0;n<repeat;++n) for(const auto &point:s["path"]) {
          const auto &xyz=point.at("xyz");position(xyz);std::string mode=point.at("mode");
          if(mode!="RAPID" && mode!=step) throw std::runtime_error("NC path mode disagrees with operation");
          double feed=mode=="RAPID"?6000.:double(s["feed_mm_min"]);
          if(!std::isfinite(feed) || feed<=0) throw std::runtime_error("moving NC path requires feed");
          double distance=0;for(int i=0;i<3;++i)distance+=std::pow(double(xyz[i])-double(prev[i]),2);
          duration+=std::sqrt(distance)*60/feed;prev=xyz;
          auto block=emit(motion(xyz,mode=="RAPID",feed),tool);
          block["xyz"]=xyz;block["mode"]=mode;block["feed_mm_min"]=feed;s["motion_path"].push_back(block);
        }
        if(std::abs(duration-double(s["duration_s"]))>.01 || duration<=0) throw std::runtime_error("NC duration must match geometric path/feed");
        if(prev!=s["target_xyz_mm"]) throw std::runtime_error("NC path endpoint mismatch");
        s["nc"]=s["motion_path"][0];
      } else s["nc"]=emit(motion(s["target_xyz_mm"],step=="RAPID",s["feed_mm_min"]),tool);
    } else if(step=="DWELL") s["nc"]=emit("G04 P"+number(s["duration_s"]),tool);
    else {if(step=="UNLOAD"){emit("M05",tool);emit("M09",tool);}s["nc"]=emit("("+step+" - SIMULATED)",tool);}
  }
  emit("M30",0);
}
}
