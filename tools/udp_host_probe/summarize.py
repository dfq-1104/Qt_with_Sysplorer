import json,pathlib,hashlib,math
root=pathlib.Path(__file__).resolve().parent
job=root/'evidence_v3'
r=json.loads((job/'verification_battery.json').read_text(encoding='utf-8'))
q=json.loads((job/'battery_result_query.json').read_text(encoding='utf-8'))
s=json.loads((job/'verification.json').read_text(encoding='utf-8'))
summary={'probePassed':s['ok'],'batteryPassed':r['ok'],'pid':r['pid'],'controller':r['controller'],'batteryData':r['data'],'batteryPausedTimes':r['pausedTimeStable'],'apiPausedState':r['officialApiPausedState'],'apiPausedTime':r['officialApiPausedTime'],'apiStoppedState':r['officialApiStoppedState'],'variables':{}}
assert s['ok'] and r['ok']
for name in ('SOC','current','voltage'):
    values=q[name]
    assert isinstance(values,list) and len(values)>0 and all(isinstance(x,(int,float)) and math.isfinite(x) for x in values)
    summary['variables'][name]={'count':len(values),'first':values[0],'last':values[-1],'min':min(values),'max':max(values)}
summary['files']={}
for relative in ('release/battery_udp_host_probe.dll','probe.cpp','probe.h','probe.json','probe.qrc','udp_host_probe.pro','verify_udp.py'):
    p=root/relative;summary['files'][relative]={'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size}
(job/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in summary.items() if k!='files'},ensure_ascii=False,indent=2))
