#!/usr/bin/env python3
"""Real PostgreSQL permission failure, not an ENOSPC simulation; always restore grants."""
import json,pathlib,time,urllib.request,psycopg,os
root=pathlib.Path(__file__).resolve().parents[2]
def get():return json.load(urllib.request.urlopen('http://127.0.0.1:8080/api/v1/status',timeout=4))
with psycopg.connect(host='127.0.0.1',port=os.getenv('POSTGRES_PORT','15432'),dbname='cnc',user='postgres',password=(root/'secrets/db-admin-password').read_text().strip(),autocommit=True) as c:
 try:
  c.execute('REVOKE INSERT,UPDATE ON runtime_state FROM cnc_writer')
  deadline=time.monotonic()+15
  while get()['health']['database'] and time.monotonic()<deadline:time.sleep(.2)
  before=get();assert not before['health']['database']
  checkpoint=c.execute('SELECT updated_ms FROM runtime_state').fetchone()[0]
  time.sleep(3);assert get()['sequence']>before['sequence']
  assert c.execute('SELECT updated_ms FROM runtime_state').fetchone()[0]==checkpoint
 finally:c.execute('GRANT INSERT,UPDATE ON runtime_state TO cnc_writer')
 deadline=time.monotonic()+15
 while not get()['health']['database'] and time.monotonic()<deadline:time.sleep(.2)
 assert get()['health']['database']
print(json.dumps({'result':'PASS','database_write_failure':'SQL permission failure; engine continued; durable checkpoint stable; recovered after grant'}))
