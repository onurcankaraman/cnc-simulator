#!/usr/bin/env python3
"""Opt-in isolated runtime; never changes the default Compose admin configuration."""
import json,pathlib,subprocess,time,urllib.request,urllib.error,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[2]
name='cnc-admin-test';tmp=tempfile.mkdtemp(prefix='cnc-admin-');pathlib.Path(tmp).chmod(0o777)
def call(path,body=None,token=None):
 headers={'Content-Type':'application/json'}
 if token:headers['Authorization']='Bearer '+token
 data=None if body is None else body if isinstance(body,bytes) else json.dumps(body).encode()
 try:
  with urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:18080'+path,data=data,headers=headers),timeout=4) as r:return r.status,json.load(r)
 except urllib.error.HTTPError as e:return e.code,e.read().decode()
try:
 subprocess.run(['docker','run','-d','--name',name,'--user','10001:10001','--read-only','--cap-drop','ALL','-p','127.0.0.1:18080:8080','-v',tmp+':/data','-v',str(ROOT/'secrets/admin-token')+':/run/secrets/admin-token:ro','-v',str(ROOT/'secrets/mqtt-password')+':/run/secrets/mqtt-password:ro','-e','ENABLE_ADMIN=true','-e','ADMIN_TOKEN_FILE=/run/secrets/admin-token','-e','MQTT_PASSWORD_FILE=/run/secrets/mqtt-password','-e','ALLOW_INSECURE_OPCUA=true','cnc-simulator:1.0'],check=True,stdout=subprocess.DEVNULL)
 for i in range(60):
  try:
   if call('/health/live')[0]==200:break
  except OSError:pass
  time.sleep(.25)
 token=(ROOT/'secrets/admin-token').read_text().strip()
 assert call('/admin/scenario',{'profile':'normal'})[0]==401
 assert call('/admin/scenario',{'profile':'normal'},'wrong')[0]==401
 assert call('/admin/scenario',b'{broken',token)[0]==400;time.sleep(1.1)
 assert call('/admin/scenario',{'profile':'invalid'},token)[0]==400;time.sleep(1.1)
 assert call('/admin/scenario',{'profile':'aging_tools'},token)[0]==200
 assert call('/admin/scenario',{'profile':'normal'},token)[0]==429;time.sleep(1.1)
 assert call('/admin/inject-fault',{'code':1402},token)[0]==200;time.sleep(.5)
 d=call('/api/v1/status')[1]['data'];assert d['state_code']==70 and d['estop_active']==1 and d['feed_actual_mm_min']==0
 time.sleep(1.1);assert call('/admin/ack-alarm',{},token)[0]==200
 assert any(e['type']=='admin_command' for e in call('/api/v1/events')[1])
 print('PASS admin authentication, malformed JSON, enum validation, rate limit, E-stop interlock, acknowledgement and SQL audit')
finally:subprocess.run(['docker','rm','-f',name],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
