#include "core/engine.hpp"
#include "core/nc_program.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace cnc;
static void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
int main(){try{
 auto cfg=load_config("config");
 cfg["plant"]["calendar"]["breaks_per_shift"]=Json::array();
 cfg["plant"]["simulation"]["setup_duration_seconds"]={0,0};
 for(auto &f:cfg["faults"]["faults"])f["hazard_per_operating_hour"]=0;
 for(auto &t:cfg["tools"]["tools"]){t["initial_wear_pct"]=0;}
 const int64_t epoch=1793516400000;
 for(int program=0;program<4;++program){
  Engine e(cfg,42);auto state=e.save();state["program"]=program;
  state["data"]["program_id"]=cfg["programs"]["programs"][program]["id"];
  state["data"]["job_target_qty"]=3;state["data"]["job_id"]=1;
  e.restore(state,epoch);e.take_events();
  double last_change=-1,min_interval=1e9;int changes=0,rapid_drill_ticks=0,cut_ticks=0;
  for(int tick=0;tick<25000 && int(e.data()["total_parts_count"])<2;++tick){
   auto before=e.tools();e.tick(.2,epoch+tick*200);auto s=e.snapshot();
   check(s.nc["program_id"]==s.data["program_id"] && s.nc["segment"]==s.data["program_block"],"NC snapshot alignment");
   double norm=0;for(auto axis:{"x","y","z"})norm+=std::pow(double(s.data[std::string("axis_")+axis+"_velocity_mm_s"]),2);
   if(int(s.data["state_code"])==40)check(std::abs(std::sqrt(norm)*60-double(s.data["feed_actual_mm_min"]))<.01,"path velocity/feed consistency");
   const auto code=s.nc["code"].get<std::string>();
   if(int(s.data["current_step_type"])==4 && code.find("G00")!=std::string::npos){
    check(!int(s.data["is_cutting"]),"drill retract/position must not cut");++rapid_drill_ticks;
    for(int i=0;i<12;++i)check(before[i]["wear_pct"]==e.tools()[i]["wear_pct"],"rapid drill moves must not wear tools");
   }
   if(int(s.data["is_cutting"])){check(code.find("G01")!=std::string::npos,"cutting must identify feed block");++cut_ticks;}
   for(const auto &event:e.take_events())if(event["type"]=="tool_change"){
    double now=tick*.2;if(last_change>=0)min_interval=std::min(min_interval,now-last_change);last_change=now;++changes;
   }
  }
  check(int(e.data()["total_parts_count"])==2,"two full long cycles");
  check(changes==4 && min_interval>=240,"tool changes must be at least four minutes apart without faults");
  check(rapid_drill_ticks>0 && cut_ticks>1000,"long cuts and noncutting drill positioning exercised");
  std::cout<<"PASS program="<<int(e.data()["program_id"])<<" nominal_s="<<double(e.data()["nominal_cycle_s"])<<" changes_per_two_parts="<<changes<<" min_tool_run_s="<<min_interval<<"\n";
 }
 Engine base(cfg,1);auto saved=base.save();saved["tool"]=3;saved["data"]["job_target_qty"]=3;saved["data"]["job_id"]=1;saved["data"]["program_id"]=1001;
 saved["program_revision"]=1;saved["data"]["nominal_cycle_s"]=102;
 saved["data"]["total_parts_count"]=40;saved["data"]["total_good_count"]=39;saved["data"]["total_reject_count"]=1;
 Engine restored(cfg,9);restored.restore(saved,epoch);
 check(restored.data()["total_parts_count"]==40 && restored.tools()==base.tools(),"upgrade must retain counters and wear");
 check(double(restored.data()["nominal_cycle_s"])>700,"upgrade must refresh cycle timing");
 bool revision=false,skip=false;for(auto &ev:restored.take_events())if(ev["type"]=="program_revision_changed")revision=true;
 for(int i=0;i<1000 && !int(restored.data()["is_cutting"]);++i){restored.tick(.2,epoch+i*200);for(auto &ev:restored.take_events())if(ev["type"]=="tool_change_skipped")skip=true;}
 check(revision && skip && int(restored.data()["tool_change_counter"])==0 && int(restored.data()["is_cutting"]),"same loaded tool must not trigger a fake change");
 auto bad=cfg["programs"]["programs"][0];bad["segments"][3]["path"][0]["xyz"][0]=900;
 bool rejected=false;try{compile_nc_program(bad);}catch(const std::runtime_error &){rejected=true;}check(rejected,"invalid path rejected");
 std::cout<<"PASS upgrade checkpoint, same-tool skip, NC path bounds\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
