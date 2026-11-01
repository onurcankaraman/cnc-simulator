#!/usr/bin/env python3
"""Measures process RSS, CPU time and cgroup stats; never labels a short run 24h."""
import argparse,csv,datetime,json,pathlib,subprocess,time,urllib.request
p=argparse.ArgumentParser();p.add_argument('--seconds',type=int,default=86400);p.add_argument('--interval',type=int,default=10);p.add_argument('--output',default='logs/resources.csv');a=p.parse_args()
def cmd(*args):return subprocess.check_output(args,text=True).strip()
services=['cnc-simulator','mqtt-broker'];ids=[cmd('docker','compose','ps','-q',s) for s in services]
def process(cid,name):
 # /proc counters include only the main runtime, excluding measurement subprocesses.
 script='for p in /proc/[0-9]*; do read n < "$p/comm" 2>/dev/null || continue; if [ "$n" = "'+name+'" ]; then cat "$p/stat"; awk \'/VmRSS:/{print $2}\' "$p/status"; fi; done'
 out=cmd('docker','exec',cid,'sh','-c',script).splitlines();fields=out[0].split(') ',1)[1].split();return int(out[1]),(int(fields[11])+int(fields[12]))/100
start=time.monotonic();first=None;peak=0;samples=[]
with open(a.output,'w') as f:
 w=csv.writer(f);w.writerow(['utc','elapsed_s','simulator_rss_kib','broker_rss_kib','combined_mib','cpu_seconds','sequence'])
 while True:
  readings=[process(ids[0],'cnc-simulator'),process(ids[1],'mosquitto')];elapsed=time.monotonic()-start;cpu=sum(x[1] for x in readings);first=cpu if first is None else first;total=sum(x[0] for x in readings)/1024;peak=max(peak,total)
  with urllib.request.urlopen('http://127.0.0.1:8080/api/v1/status',timeout=4) as r:status=json.load(r)
  assert status['health']['database'] and status['health']['mqtt']
  row=[datetime.datetime.now(datetime.timezone.utc).isoformat(),round(elapsed,2),readings[0][0],readings[1][0],round(total,3),cpu,status['sequence']];w.writerow(row);f.flush();samples.append(row)
  if elapsed>=a.seconds:break
  time.sleep(min(a.interval,a.seconds-elapsed))
summary={'duration_s':round(elapsed,2),'initial_rss_mib':samples[0][4],'final_rss_mib':samples[-1][4],'peak_rss_mib':round(peak,3),'average_cpu_one_core_pct':round((cpu-first)/elapsed*100,3),'samples':len(samples),'24h_soak':'PASS' if elapsed>=86400 and peak<=100 else 'NOT RUN','memory_target_short_run':'PASS' if peak<=100 else 'FAIL'}
pathlib.Path(a.output+'.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary))
