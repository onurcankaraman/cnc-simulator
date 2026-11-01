#!/usr/bin/env python3
import asyncio,os,pathlib,sys,time,signal
import snap7
from asyncua import Client,ua
from asyncua.crypto.security_policies import SecurityPolicyBasic256Sha256
import paho.mqtt.client as mqtt
ROOT=pathlib.Path(__file__).resolve().parents[2]
class Handler:
 def datachange_notification(self,*args):pass
async def main():
 loop=asyncio.get_running_loop();task=asyncio.current_task();loop.add_signal_handler(signal.SIGTERM,task.cancel)
 duration=int(sys.argv[1]) if len(sys.argv)>1 else 86400
 s7=[];ua_clients=[];mq=[];subs=[]
 try:
  for i in range(3):
   c=snap7.client.Client();c.connect('127.0.0.1',0,1,tcp_port=int(os.getenv('S7_PORT','102')));s7.append(c)
   u=Client('opc.tcp://127.0.0.1:4840');u.application_uri='urn:cnc:test-client';await u.set_security(SecurityPolicyBasic256Sha256,str(ROOT/'secrets/client.der'),str(ROOT/'secrets/client-key.pem'),server_certificate=str(ROOT/'secrets/server.der'),mode=ua.MessageSecurityMode.SignAndEncrypt);await u.connect();ua_clients.append(u)
   sub=await u.create_subscription(1000,Handler());await sub.subscribe_data_change([u.get_node('ns=2;s=Machine.CNC-01.'+k) for k in ('sequence','spindle_actual_rpm','active_wear_pct','state_code')]);subs.append(sub)
   m=mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,client_id='soak-'+str(i));m.username_pw_set('observer',(ROOT/'secrets/observer-password').read_text().strip());m.on_connect=lambda c,*args:c.subscribe('plant/demo/cnc/CNC-01/#');m.connect('127.0.0.1');m.loop_start();mq.append(m)
  print('LOAD_READY 3 S7 + 3 encrypted OPC UA subscriptions + 3 MQTT subscribers',flush=True)
  end=time.monotonic()+duration
  while time.monotonic()<end:
   for c in s7:
    for db,n in [(1,256),(2,256),(3,256),(4,128)]:assert len(c.db_read(db,0,n))==n
   await asyncio.sleep(1)
 finally:
  for sub in subs:await sub.delete()
  for u in ua_clients:await u.disconnect()
  for c in s7:c.disconnect()
  for m in mq:m.disconnect();m.loop_stop()
try:asyncio.run(main())
except (asyncio.CancelledError,KeyboardInterrupt):pass
