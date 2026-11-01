#!/usr/bin/env python3
import csv,pathlib,sys
rows=list(csv.DictReader(open('contracts/tags.csv')))
used={i:set() for i in range(1,5)}
out=['// Generated from contracts/tags.csv; do not edit.','#pragma once','#include <array>','#include <string>','#include <cstdint>','namespace cnc {','enum class Kind {U8,UINT16,UINT32,REAL32,STRING32,STRING48};','struct Tag {const char* key; Kind kind; int db,offset,size; const char* group; double low,high;};',f'inline constexpr std::array<Tag,{len(rows)}> tags = {{{{']
for r in rows:
 t=r['type']; n={'U8':1,'UINT16':2,'UINT32':4,'REAL32':4,'STRING32':34,'STRING48':50}[t]; db=int(r['s7_db']); off=int(r['s7_offset']); span=set(range(off,off+n))
 assert not used[db]&span and off+n<={1:256,2:256,3:256,4:128}[db],r
 used[db]|=span
 assert r['opc_node_id']=='ns=2;s=Machine.CNC-01.'+r['key'] and r['mqtt_path']=='data.'+r['key']
 out.append('{"%s",Kind::%s,%d,%d,%d,"%s",%s,%s},'%(r['key'],t,db,off,n,r['opc_group'],r['min'] or '0',r['max'] or '0'))
out+=['}};','}']
pathlib.Path(sys.argv[1] if len(sys.argv)>1 else 'src/transport/contract.hpp').write_text('\n'.join(out)+'\n')
print(f'contract: {len(rows)} fields; DB bounds/overlaps/NodeIds PASS')
