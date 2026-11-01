#!/usr/bin/env python3
"""Plot actual accelerated C++ test samples; clearly not live soak evidence."""
import csv,pathlib,matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
rows=[{k:float(v) for k,v in r.items()} for r in csv.DictReader(open('logs/synthetic-traces.csv'))]
rows=[r for r in rows if r['seconds']<=86400]
pathlib.Path('docs/figures').mkdir(exist_ok=True)
plt.style.use('seaborn-v0_8-whitegrid')
for name,fields in [('thermal-load',[('load','Spindle load (%)'),('temp','Spindle temperature (°C)')]),('wear-vibration',[('wear','Active tool wear (%)'),('vibration','Vibration RMS (mm/s)')]),('power-production',[('power','Power (kW)'),('parts','Completed parts')])]:
 fig,axes=plt.subplots(2,1,figsize=(11,5),sharex=True)
 for ax,(key,label) in zip(axes,fields):ax.plot([r['seconds']/3600 for r in rows],[r[key] for r in rows],lw=.7,color='#147b77');ax.set_ylabel(label)
 axes[-1].set_xlabel('Simulated hours (fake clock)');fig.suptitle('C++ deterministic test · 24 simulated hours · NOT a live soak');fig.tight_layout();fig.savefig(f'docs/figures/{name}.png',dpi=130);plt.close(fig)
fig,axes=plt.subplots(1,3,figsize=(11,3))
for ax,key,label in zip(axes,['load','vibration','power'],['Load (%)','Vibration (mm/s)','Power (kW)']):ax.hist([r[key] for r in rows],bins=30,color='#147b77');ax.set_xlabel(label);ax.set_ylabel('Minute samples')
fig.suptitle('Synthetic fake-clock samples · not field calibration');fig.tight_layout();fig.savefig('docs/figures/histograms.png',dpi=130)
print('Saved 3 time-series figures + histograms from real C++ test trace; simulated time only')
