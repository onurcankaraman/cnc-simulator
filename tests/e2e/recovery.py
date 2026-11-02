#!/usr/bin/env python3
import json,pathlib,subprocess,time,urllib.request,os
import psycopg
ROOT=pathlib.Path(__file__).resolve().parents[2]
def run(*args):subprocess.run(args,cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
def get(p='/api/v1/status'):return json.load(urllib.request.urlopen('http://127.0.0.1:8080'+p,timeout=3))
def wait(predicate,seconds=60):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  try:
   x=get()
   if predicate(x):return x
  except (OSError,ValueError):pass
  time.sleep(.25)
 raise AssertionError('recovery timeout')
# Quiescent graceful shutdown flushes the exact durable checkpoint.
before=get();run('docker','compose','stop','cnc-simulator')
con=psycopg.connect(host='127.0.0.1',port=os.getenv('POSTGRES_PORT','15432'),dbname='cnc',user='postgres',password=(ROOT/'secrets/db-admin-password').read_text().strip());saved=con.execute("select value_json from runtime_state where key='engine'").fetchone()[0];con.close()
run('docker','compose','start','cnc-simulator');after=wait(lambda x:x['health']['database'] and x['health']['mqtt'])
for k in ('total_parts_count','total_good_count','total_reject_count','job_id'):assert after['data'][k]==saved['data'][k],k
assert after['data']['energy_kwh']>=saved['energy']-.001
for i in range(1,13):
 k=f'tool_{i:02}_wear_pct';assert abs(after['data'][k]-saved['data'][k])<.001,k
assert after['sequence']>before['sequence']
assert any(e['type']=='restart_interruption' for e in get('/api/v1/events'))
# Broker outage: the engine, S7/UA loops and DB remain live; MQTT recovers.
run('docker','compose','stop','mqtt-broker');offline=wait(lambda x:not x['health']['mqtt']);time.sleep(6);during=get();assert during['sequence']>offline['sequence'] and during['health']['database'] and during['health']['s7'] and during['health']['opcua']
run('docker','compose','start','mqtt-broker');online=wait(lambda x:x['health']['mqtt']);assert online['sequence']>during['sequence']
print(json.dumps({'result':'PASS','restart':'counters/tools/order/energy preserved; incomplete cycle not counted','broker_outage':'engine/DB continued; MQTT reconnected','outage_sequence_delta':during['sequence']-offline['sequence'],'utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())}))

# DB outage must not reset the engine; reconnect and persist a visible sample gap.
run('docker','compose','stop','timescaledb');offline=wait(lambda x:not x['health']['database']);time.sleep(6);during=get();assert during['sequence']>offline['sequence'] and during['health']['mqtt']
run('docker','compose','start','timescaledb');online=wait(lambda x:x['health']['database']);assert online['sequence']>during['sequence']
assert any(e['type']=='sample_gap' for e in get('/api/v1/events'))
print(json.dumps({'result':'PASS','database_outage':'engine continued; reconnected; sample_gap recorded'}))
