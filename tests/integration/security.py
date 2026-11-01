#!/usr/bin/env python3
import asyncio,pathlib,time,uuid,threading
import paho.mqtt.client as mqtt
from asyncua import Client
ROOT=pathlib.Path(__file__).resolve().parents[2]
async def main():
 c=Client('opc.tcp://127.0.0.1:4840',timeout=3)
 try:await c.connect()
 except Exception:pass
 else:
  await c.disconnect();raise AssertionError('insecure UA session accepted')
 outcomes=[];anon=mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,client_id='anonymous-negative-test');anon.on_connect=lambda c,u,f,rc,p:outcomes.append(rc.is_failure);anon.connect('127.0.0.1');anon.loop_start()
 for _ in range(30):
  if outcomes:break
  await asyncio.sleep(.1)
 anon.disconnect();anon.loop_stop();assert outcomes and outcomes[0],'anonymous MQTT accepted'
 topic='plant/demo/cnc/CNC-01/acl-probe-'+uuid.uuid4().hex;got=[];ready=threading.Event();m=mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,client_id='acl-negative-test');m.username_pw_set('observer',(ROOT/'secrets/observer-password').read_text().strip());m.on_connect=lambda c,*_:c.subscribe(topic,1);m.on_subscribe=lambda *_:ready.set();m.on_message=lambda c,u,msg:got.append(msg.payload);m.connect('127.0.0.1');m.loop_start()
 for _ in range(30):
  if ready.is_set():break
  await asyncio.sleep(.1)
 assert ready.is_set();m.publish(topic,'unauthorized-write',qos=1);await asyncio.sleep(2);m.disconnect();m.loop_stop();assert not got,'observer write ACL failed'
 print('PASS default OPC None session rejected; anonymous MQTT rejected; observer MQTT publish not delivered')
asyncio.run(main())
