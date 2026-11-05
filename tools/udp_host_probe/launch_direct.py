import json, os, pathlib, secrets, socket, subprocess, time, sys
root=pathlib.Path(__file__).resolve().parent
job=root/(sys.argv[1] if len(sys.argv)>1 else 'evidence')
job.mkdir(exist_ok=True)
log=job/'host.jsonl'
assert not log.exists(), 'Refusing overwrite of prior evidence'
token=secrets.token_hex(24)
env=dict(os.environ)
for name in ('QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','QT_PLUGIN_PATH','PYTHONHOME','PYTHONPATH'):
    env.pop(name,None)
env['BATTERY_UDP_PROBE_TOKEN']=token
env['BATTERY_UDP_PROBE_LOG']=str(log)
env['PATH']=r'C:\Program Files\MWORKS\Sysplorer 2026a\Bin64'+os.pathsep+env['PATH']
with socket.socket() as s:
    s.bind(('127.0.0.1',0)); apiport=s.getsockname()[1]
work=pathlib.Path(os.environ['TEMP'])/'BatteryUdpHostProbe'
work.mkdir(exist_ok=True)
exe=r'C:\Program Files\MWORKS\Sysplorer 2026a\Bin64\mworks.exe'
p=subprocess.Popen([exe,'--script_addr','127.0.0.1:'+str(apiport),'-d',str(work),'-w',str(work)],env=env,cwd=str(work))
print('Started target pid',p.pid,'api port',apiport,flush=True)
for i in range(120):
    if p.poll() is not None:
        raise RuntimeError('Target exited '+str(p.returncode))
    if log.exists():
        events=[json.loads(x) for x in log.read_text(encoding='utf-8').splitlines() if x.strip()]
        for v in events:
            if v.get('event')=='plugin_initialized':
                assert v['pid']==p.pid
                v.update(token=token,apiport=apiport)
                (job/'session.private.json').write_text(json.dumps(v,indent=2),encoding='utf-8')
                print('Loaded UDP plugin:',{k:x for k,x in v.items() if k!='token'},flush=True)
                raise SystemExit(0)
    time.sleep(.5)
raise RuntimeError('Target running but plugin did not initialize within 60 seconds')

