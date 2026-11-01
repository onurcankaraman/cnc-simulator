#!/usr/bin/env python3
import csv,json,math,collections,pathlib
rows=[{k:float(v) for k,v in row.items()} for row in csv.DictReader(open('logs/synthetic-traces.csv'))]
groups=collections.defaultdict(list)
for r in rows:
 if r['seconds']<=86400 and r['cutting'] and r['rpm']>300:
  groups[(int(r['program']),int(r['tool']),round(r['rpm']/100)*100)].append(r)
def correlation(rows,k):
 x=[r['wear'] for r in rows];y=[r[k] for r in rows];xm=sum(x)/len(x);ym=sum(y)/len(y);den=math.sqrt(sum((z-xm)**2 for z in x)*sum((z-ym)**2 for z in y));return sum((a-xm)*(b-ym) for a,b in zip(x,y))/den if den else 0
out=[]
for group,data in groups.items():
 if len(data)>=20 and max(r['wear'] for r in data)-min(r['wear'] for r in data)>=1:
  out.append({'program':group[0],'tool':group[1],'rpm_bucket':group[2],'samples':len(data),'wear_load_r':round(correlation(data,'load'),4),'wear_vibration_r':round(correlation(data,'vibration'),4)})
assert out,'no eligible strata'
weighted=lambda k:sum(r[k]*r['samples'] for r in out)/sum(r['samples'] for r in out)
result={'source':'actual C++ fake-clock trace; first 24 simulated hours','scope':'cutting samples stratified by program, tool and 100-rpm bucket; n>=20, wear range>=1%','strata':out,'weighted_wear_load_r':round(weighted('wear_load_r'),4),'weighted_wear_vibration_r':round(weighted('wear_vibration_r'),4)}
result['heuristic_result']='PASS' if weighted('wear_load_r')>0 and weighted('wear_vibration_r')>0 else 'FAIL'
pathlib.Path('docs/evidence/synthetic-correlations.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
