#!/usr/bin/env python3
"""Actual ENOSPC on an isolated 1MiB tmpfs; never fills the host disk."""
import json,pathlib,subprocess,time,urllib.request
ROOT=pathlib.Path(__file__).resolve().parents[2];name='cnc-disk-test'
def get():return json.load(urllib.request.urlopen('http://127.0.0.1:18081/api/v1/status',timeout=4))
try:
 subprocess.run(['docker','run','-d','--name',name,'--read-only','--cap-drop','ALL','--tmpfs','/data:rw,size=1m,uid=10001,gid=10001,mode=700','-p','127.0.0.1:18081:8080','-v',str(ROOT/'secrets/mqtt-password')+':/run/secrets/mqtt-password:ro','-e','MQTT_PASSWORD_FILE=/run/secrets/mqtt-password','-e','ALLOW_INSECURE_OPCUA=true','--entrypoint','sh','cnc-simulator:1.0','-c','dd if=/dev/zero of=/data/reserve bs=1024 count=700 2>/dev/null; exec /app/cnc-simulator'],check=True,stdout=subprocess.DEVNULL)
 deadline=time.monotonic()+90;bad=None
 while time.monotonic()<deadline:
  try:
   s=get()
   if not s['health']['database']:bad=s;break
  except OSError:pass
  time.sleep(.5)
 assert bad,'ENOSPC not observed'
 time.sleep(2);assert get()['sequence']>bad['sequence'],'engine stopped on disk failure'
 subprocess.run(['docker','exec',name,'rm','/data/reserve'],check=True)
 deadline=time.monotonic()+20
 while time.monotonic()<deadline:
  s=get()
  if s['health']['database']:break
  time.sleep(.5)
 assert s['health']['database'],'database did not recover'
 print(json.dumps({'result':'PASS','failure':bad['health']['database_error'],'continued_sequences':s['sequence']-bad['sequence'],'recovered':s['health']['database'],'persistence_failures':s['health']['persistence_failures']}))
finally:
 subprocess.run(['docker','logs',name],stdout=open(ROOT/'logs/disk-runtime.log','w'),stderr=subprocess.STDOUT)
 subprocess.run(['docker','rm','-f',name],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
