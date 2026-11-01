#!/usr/bin/env python3
import json,time,urllib.request
start=time.monotonic()
with urllib.request.urlopen('http://127.0.0.1:8080/api/v1/history?limit=5000&step=1',timeout=15) as r:
 body=r.read();assert r.headers['Transfer-Encoding']=='chunked'
rows=json.loads(body);assert 0<len(rows)<=5000
assert all(rows[i]['ts_utc']<=rows[i+1]['ts_utc'] for i in range(len(rows)-1))
assert len(rows[0]['data'])==158
print(json.dumps({'result':'PASS','history_rows':len(rows),'response_bytes':len(body),'duration_seconds':round(time.monotonic()-start,3),'transfer_encoding':'chunked','fields_per_row':158}))
