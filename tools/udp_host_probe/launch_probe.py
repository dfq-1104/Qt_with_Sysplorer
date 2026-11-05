"""Start the target GUI and locate its in-process UDP probe. No simulation via Python."""
import json, os, pathlib, secrets, sys, time
root=pathlib.Path(__file__).resolve().parent
job=root/'evidence'
job.mkdir(exist_ok=True)
log=job/'host.jsonl'
if log.exists():
    raise RuntimeError('Use a fresh evidence directory; refusing to overwrite a prior run')
token=secrets.token_hex(24)
os.environ['BATTERY_UDP_PROBE_TOKEN']=token
os.environ['BATTERY_UDP_PROBE_LOG']=str(log)
for name in ('QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','QT_PLUGIN_PATH','PYTHONHOME','PYTHONPATH'):
    os.environ.pop(name,None)
os.environ['PATH']=r'C:\Program Files\MWORKS\Sysplorer 2026a\Bin64'+os.pathsep+os.environ['PATH']
import mworks.sysplorer as eng
exe=r'C:\Program Files\MWORKS\Sysplorer 2026a\Bin64\mworks.exe'
print('Starting target GUI:',exe,flush=True)
res=eng.StartSysplorer(processPath=exe)
print('StartSysplorer:',res,flush=True)
assert eng.SendHeart(), 'GUI API heartbeat failed'
for i in range(60):
    if log.exists():
        lines=[json.loads(s) for s in log.read_text(encoding='utf-8').splitlines() if s.strip()]
        events=[s for s in lines if s.get('event')=='plugin_initialized']
        if events:
            value=dict(events[0]);value['token']=token
            (job/'session.private.json').write_text(json.dumps(value,indent=2),encoding='utf-8')
            print('UDP plugin:',{k:v for k,v in value.items() if k!='token'},flush=True)
            sys.exit(0)
    time.sleep(0.5)
raise RuntimeError('GUI alive but no UDP plugin initialization evidence')
