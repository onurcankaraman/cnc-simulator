#!/usr/bin/env python3
"""Independent python-snap7 / asyncua / paho clients; no production modules imported."""
import asyncio,csv,json,struct,time,os,logging,urllib.request,pathlib
import snap7
from asyncua import Client,ua
from asyncua.crypto.security_policies import SecurityPolicyBasic256Sha256
import paho.mqtt.client as mqtt
import jsonschema
logging.basicConfig(level=logging.ERROR)
ROOT=pathlib.Path(__file__).resolve().parents[2]
HOST=os.getenv('TEST_HOST','127.0.0.1'); PORT=int(os.getenv('S7_PORT','102'))
rows=list(csv.DictReader(open(ROOT/'contracts/tags.csv')))
schema=json.load(open(ROOT/'contracts/mqtt.schema.json'))
def decode(blobs):
 out={}
 for r in rows:
  b=blobs[int(r['s7_db'])];off=int(r['s7_offset']);t=r['type']
  out[r['key']]=b[off+2:off+2+b[off+1]].decode('ascii') if t.startswith('STRING') else struct.unpack_from({'U8':'>B','UINT16':'>H','UINT32':'>I','REAL32':'>f'}[t],b,off)[0]
 return out
def same(a,b):
 for r in rows:
  k=r['key']
  assert abs(a[k]-b[k])<=1e-3 if r['type']=='REAL32' else a[k]==b[k],(k,a[k],b[k])
def api(p):return json.load(urllib.request.urlopen('http://'+HOST+':8080'+p,timeout=4))
async def main():
 messages={}; retained={};mq=mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,client_id='independent-contract-test')
 mq.username_pw_set('observer',(ROOT/'secrets/observer-password').read_text().strip())
 mq.on_connect=lambda c,u,f,rc,p:c.subscribe('plant/demo/cnc/CNC-01/#',qos=1)
 def receive(c,u,msg):
  j=json.loads(msg.payload);retained[msg.topic.rsplit('/',1)[1]]=j
  if msg.topic.endswith('/telemetry'):
   jsonschema.validate(j,schema);messages[j['sequence']]=j
   while len(messages)>120:messages.pop(next(iter(messages)))
 mq.on_message=receive;mq.connect(HOST,1883);mq.loop_start()
 s7=snap7.client.Client();s7.connect(HOST,0,1,tcp_port=PORT)
 client=Client('opc.tcp://'+HOST+':4840');client.application_uri='urn:cnc:test-client'
 if os.getenv('ALLOW_INSECURE_OPCUA','false')!='true':
  await client.set_security(SecurityPolicyBasic256Sha256,str(ROOT/'secrets/client.der'),str(ROOT/'secrets/client-key.pem'),server_certificate=str(ROOT/'secrets/server.der'),mode=ua.MessageSecurityMode.SignAndEncrypt)
 await client.connect()
 try:
  namespaces=await client.get_namespace_array();assert namespaces[2]=='urn:digitheta:cnc-simulator:machine:v1'
  nodes=[client.get_node(r['opc_node_id']) for r in rows];seqnode=client.get_node('ns=2;s=Machine.CNC-01.sequence')
  notices=[]
  class Handler:
   def datachange_notification(self,node,value,data):notices.append(value)
  sub=await client.create_subscription(250,Handler());await sub.subscribe_data_change(seqnode)
  matched=[];deadline=time.monotonic()+30
  while len(matched)<3 and time.monotonic()<deadline:
   await asyncio.sleep(.15)
   before=struct.unpack('>I',s7.db_read(1,8,4))[0]
   blobs={i:bytes(s7.db_read(i,0,n)) for i,n in [(1,256),(2,256),(3,256),(4,128)]}
   ua_before=await seqnode.read_value();values=await client.read_values(nodes);ua_after=await seqnode.read_value()
   after=struct.unpack('>I',s7.db_read(1,8,4))[0];d=decode(blobs)
   if before==after==d['sequence']==ua_before==ua_after and before in messages and before not in matched:
    same(d,dict(zip([r['key'] for r in rows],values)));same(d,messages[before]['data']);matched.append(before)
  assert len(matched)==3,('could not align snapshots',matched)
  try:s7.db_write(3,48,bytearray(b'\xff\xff\xff\xff'))
  except RuntimeError:pass
  else:raise AssertionError('S7 write unexpectedly succeeded')
  assert struct.unpack('>I',s7.db_read(3,48,4))[0]!=0xffffffff
  try:await nodes[0].write_value(ua.Variant(70,ua.VariantType.UInt16))
  except ua.UaStatusCodeError:pass
  else:raise AssertionError('UA write unexpectedly succeeded')
  dv=await seqnode.read_data_value();assert dv.SourceTimestamp is not None
  await asyncio.sleep(1.2);assert len(notices)>=2,'subscription did not update'
  await sub.delete()
  s7.disconnect();s7.connect(HOST,0,0,tcp_port=PORT);assert len(s7.db_read(1,0,256))==256
  assert len(api('/api/v1/tools')['data']['tools'])==12
  assert api('/api/v1/about')['data_origin']=='Synthetic'
  for asset in ('/','/app.js','/style.css'):
   with urllib.request.urlopen('http://'+HOST+':8080'+asset,timeout=4) as response:
    assert len(response.read())>100, 'empty static asset: '+asset
  for path in ['/admin/inject-fault','/api/v1/history?limit=5001','/api/v1/history?from=oops','/api/v1/history?step=0']:
   try:api(path)
   except urllib.error.HTTPError as e:assert e.code in (400,404)
   else:raise AssertionError('invalid request accepted '+path)
  print(json.dumps({'result':'PASS','fields':len(rows),'aligned_sequences':matched,'db_sizes':[256,256,256,128],'s7_slots':[0,1],'s7_write':'REJECTED','ua_write':'REJECTED','ua_subscription_notifications':len(notices),'ua_security':'Basic256Sha256/SignAndEncrypt','source_timestamp':dv.SourceTimestamp.isoformat(),'mqtt_schema':1,'http_validation':'PASS','utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())}))
 finally:
  await client.disconnect();s7.disconnect();mq.disconnect();mq.loop_stop()
asyncio.run(main())
