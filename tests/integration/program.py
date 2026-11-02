#!/usr/bin/env python3
"""Independent clients compare NC context across HTTP, MQTT and PostgreSQL."""
import json,pathlib,time,os,urllib.request
import psycopg,jsonschema
import paho.mqtt.client as mqtt
root=pathlib.Path(__file__).resolve().parents[2]
def api(path):return json.load(urllib.request.urlopen('http://127.0.0.1:8080/api/v1/'+path,timeout=5))
schema=json.loads((root/'contracts/program.schema.json').read_text())
catalog=api('programs')['programs'];assert len(catalog)==4
for p in catalog:
 assert p['planned_tool_changes']==2 and 700<p['nominal_cycle_seconds']<1000
 assert p['gcode_lines'] and p['synthetic'] and p['revision']==2
messages={};m=mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,client_id='nc-context-test')
m.username_pw_set('observer',(root/'secrets/observer-password').read_text().strip())
m.on_connect=lambda c,*_:c.subscribe('plant/demo/cnc/CNC-01/program')
def receive(c,u,msg):
 j=json.loads(msg.payload);jsonschema.validate(j,schema);messages[j['sequence']]=j['data']
m.on_message=receive;m.connect('127.0.0.1',1883);m.loop_start()
try:
 deadline=time.monotonic()+20;http_sequence=None;sql_sequence=None
 with psycopg.connect(host='127.0.0.1',port=os.getenv('POSTGRES_PORT','15432'),dbname='cnc',user='cnc_reader',password=(root/'secrets/db-reader-password').read_text().strip(),autocommit=True) as db:
  while time.monotonic()<deadline:
   status=api('status');program=api('program');jsonschema.validate(program,schema)
   if status['sequence']==program['sequence'] and program['sequence'] in messages:
    assert status['nc']==program['data']==messages[program['sequence']];http_sequence=program['sequence']
   row=db.execute('SELECT seq,nc_json,payload_json FROM telemetry ORDER BY ts_ms DESC LIMIT 1').fetchone()
   if row and row[0] in messages:
    assert row[1]==messages[row[0]] and len(row[2]['data'])==158
    assert row[1]['program_id']==row[2]['data']['program_id'] and row[1]['segment']==row[2]['data']['program_block'];sql_sequence=row[0]
   if http_sequence and sql_sequence:break
   time.sleep(.2)
  assert http_sequence and sql_sequence,'NC snapshot alignment timeout'
 print(json.dumps({'result':'PASS','programs':[{k:p[k] for k in ('id','nominal_cycle_seconds','planned_tool_changes','revision')} for p in catalog],'HTTP_MQTT_sequence':http_sequence,'SQL_MQTT_sequence':sql_sequence,'telemetry_fields_unchanged':158}))
finally:m.disconnect();m.loop_stop()
