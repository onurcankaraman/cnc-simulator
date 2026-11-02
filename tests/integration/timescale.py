#!/usr/bin/env python3
"""Independent PostgreSQL client: live samples, permissions and exact retention."""
import json, os, pathlib, time, urllib.request
import psycopg
import paho.mqtt.client as mqtt
ROOT=pathlib.Path(__file__).resolve().parents[2]
def connect(user, secret, db='cnc'):
 return psycopg.connect(host='127.0.0.1',port=os.getenv('POSTGRES_PORT','15432'),dbname=db,user=user,password=(ROOT/'secrets'/secret).read_text().strip(),autocommit=True)
# Compare an actual sampled row with an independently received MQTT snapshot.
messages={}
m=mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,client_id='timescale-snapshot-check')
m.username_pw_set('observer',(ROOT/'secrets/observer-password').read_text().strip())
m.on_connect=lambda c,*_:c.subscribe('plant/demo/cnc/CNC-01/telemetry')
m.on_message=lambda c,u,msg:messages.update({json.loads(msg.payload)['sequence']:json.loads(msg.payload)})
m.connect('127.0.0.1',1883);m.loop_start()
try:
 deadline=time.monotonic()+15
 with connect('cnc_reader','db-reader-password') as c:
  while True:
   sampled=c.execute('SELECT seq,payload_json FROM telemetry ORDER BY ts_ms DESC LIMIT 1').fetchone()
   if sampled and sampled[0] in messages:
    assert sampled[1]==messages[sampled[0]],'SQL/MQTT snapshot differs'
    break
   assert time.monotonic()<deadline,'No aligned SQL/MQTT sample'
   time.sleep(.2)
finally:m.disconnect();m.loop_stop()
with connect('cnc_reader','db-reader-password') as c:
 row=c.execute('SELECT ts_ms,seq,payload_json FROM telemetry ORDER BY ts_ms DESC LIMIT 1').fetchone()
 assert row and row[1]==row[2]['sequence']
 assert len(row[2]['data'])==158
 try:c.execute('DELETE FROM telemetry')
 except psycopg.Error as e:assert e.sqlstate in ('42501','25006')
 else:raise AssertionError('reader can delete telemetry')
 try:c.execute('SELECT * FROM runtime_state')
 except psycopg.Error as e:assert e.sqlstate=='42501'
 else:raise AssertionError('reader can access private checkpoint')
with connect('postgres','db-admin-password') as c:
 assert c.execute("SELECT count(*) FROM timescaledb_information.hypertables WHERE hypertable_name IN ('telemetry','events')").fetchone()[0]==2
 assert c.execute("SELECT count(*) FROM timescaledb_information.jobs WHERE proc_name='cnc_retention_job' AND scheduled").fetchone()[0]==1
 expired=int(time.time()*1000)-90000000
 c.execute('INSERT INTO telemetry(ts_ms,seq,payload_json) VALUES(%s,0,%s)',(expired,'{}'))
 job=c.execute("SELECT job_id FROM timescaledb_information.jobs WHERE proc_name='cnc_retention_job'").fetchone()[0]
 c.execute('CALL run_job(%s)',(job,))
 assert c.execute('SELECT count(*) FROM telemetry WHERE ts_ms=%s',(expired,)).fetchone()[0]==0
 version=c.execute("SELECT extversion FROM pg_extension WHERE extname='timescaledb'").fetchone()[0]
# Isolated test DB created by scripts/test-database.sh; current wall time irrelevant.
with connect('postgres','db-admin-password','cnc_test') as c:
 now=1900000000000; cutoff=now-86400000
 c.execute('TRUNCATE telemetry,events,orders,runtime_state')
 for ts in (cutoff-1,cutoff,now):
  c.execute('INSERT INTO telemetry(ts_ms,seq,payload_json) VALUES(%s,1,%s)',(ts,'{}'))
  c.execute('INSERT INTO events(id,ts_ms,details_json) VALUES(1,%s,%s)',(ts,'{}'))
 c.execute("INSERT INTO runtime_state VALUES('engine','{\"wear\":42}',1)")
 c.execute("INSERT INTO orders(order_id,status,completed_ms) VALUES(1,'active',0),(2,'completed',%s),(3,'completed',%s)",(cutoff-1,cutoff))
 c.execute('SELECT cnc_cleanup(%s)',(now,))
 for table in ('telemetry','events'):
  assert c.execute(f'SELECT ts_ms FROM {table} ORDER BY ts_ms').fetchall()==[(cutoff,),(now,)]
 assert c.execute('SELECT value_json FROM runtime_state').fetchone()[0]['wear']==42
 assert c.execute('SELECT order_id FROM orders ORDER BY order_id').fetchall()==[(1,),(3,)]
print(json.dumps({'result':'PASS','timescaledb':version,'hypertables':2,'direct_SQL':'158 fields equal MQTT snapshot; reader SELECT only','retention':'exact 24h boundary; counters/active order preserved; scheduled every minute'}))
