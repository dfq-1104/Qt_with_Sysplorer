"""Real UDP / real Sysplorer integration. No model or solver substitutes."""
import json,os,pathlib,socket,subprocess,time,uuid,hashlib,traceback,sys
root=pathlib.Path(__file__).resolve().parent
artifact=root/'artifacts'/time.strftime('%Y%m%d_%H%M%S')
artifact.mkdir(parents=True,exist_ok=False)
report={'ok':False,'cases':[]}
def uid():return str(uuid.uuid4())
exe=str(pathlib.Path(os.environ.get('SYSPLORER_HOME',r'C:\Program Files\MWORKS\Sysplorer 2026a'))/'Bin64/mworks.exe')
env=dict(os.environ)
for k in ('QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','QT_PLUGIN_PATH','PYTHONHOME','PYTHONPATH'):
    env.pop(k,None)
env['PATH']=str(pathlib.Path(exe).parent)+os.pathsep+env['PATH']
work=pathlib.Path(os.environ['TEMP'])/'BatteryConsoleUDP';work.mkdir(exist_ok=True)
p=subprocess.Popen([exe,'-d',str(work),'-w',str(work)],env=env,cwd=str(work))
report['pid']=p.pid;print('Test host PID',p.pid,flush=True)
endpoint=pathlib.Path(os.environ['LOCALAPPDATA'])/'BatteryConsole'/'udp'/(str(p.pid)+'.json')
sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);sock.bind(('127.0.0.1',0));sock.settimeout(1)
client=uid();session=None
def request(cmd,task='',args=None,accepted=True,rid=None):
    r=dict(schema=1,id=rid or uid(),session=session['session'],token=session['token'],client=client,command=cmd,taskId=task,args=args or {})
    data=json.dumps(r).encode()
    for attempt in range(4):
        sock.sendto(data,('127.0.0.1',session['port']))
        try:
            while True:
                raw,peer=sock.recvfrom(32768);v=json.loads(raw)
                if v.get('id')==r['id']:break
            assert v['session']==session['session']
            if accepted:assert v.get('ok'),v
            return v
        except socket.timeout:
            if attempt==3:raise
def poll(task,cursor=0):
    return request('poll',task,{'cursor':cursor})['status']
try:
    for _ in range(180):
        if p.poll() is not None: raise RuntimeError('Host exited '+str(p.returncode))
        if endpoint.exists():
            session=json.loads(endpoint.read_text(encoding='utf-8-sig'))
            try:
                if request('hello').get('ready'): break
            except (socket.timeout,AssertionError): pass
        time.sleep(.5)
    else: raise TimeoutError('Host not ready')
    assert session['modelHash']==hashlib.sha256((root/'../../model/BatterySim.mo').read_bytes()).hexdigest(), 'Installed model version differs from source'
    task=uid()
    initial=dict(modelHash=session['modelHash'],mode=1,voltage=3.45,current=1.,soc0=.5,duration=90,speed=1)
    report['taskId']=task
    request('begin',task,initial)
    rows=[];cursor=0;state=None
    def tick():
        global state,cursor
        state=poll(task,cursor);cursor=state['next'];rows.extend(state['rows'])
        assert state['phase']!='Failed',state
        return state
    def wait_for(predicate,seconds=180):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            tick()
            if predicate(): return
            time.sleep(.1)
        raise TimeoutError(str(state))
    wait_for(lambda:state['phase']=='Running' and state['time']>=2)
    base=state['runtimeParams'];identity=(state['controller'],state['data'],state['taskId'])
    assert abs(rows[-1][2]-1)<1e-5,rows[-1]
    # Invalid, foreign and duplicate commands must not affect the trajectory.
    assert not request('tune',uid(),base,accepted=False)['ok']
    invalid=dict(base,iMax=6)
    assert not request('tune',task,invalid,accepted=False)['ok']
    def change(label,changes,expected,paused=False):
        global base
        if paused:
            request('pause',task);wait_for(lambda:state['phase']=='Paused',15)
        count=len(state['tuneHistory']);base=dict(base,**changes)
        rid=uid();first=request('tune',task,base,rid=rid);assert request('tune',task,base,rid=rid)==first
        if paused:
            wait_for(lambda:state['phase']=='Paused' and state['tunePending'],15)
            held=state['time'];time.sleep(.4);tick();assert state['phase']=='Paused' and state['time']==held
            request('resume',task)
        wait_for(lambda:len(state['tuneHistory'])==count+1 and state['phase']=='Running',20)
        event=state['tuneHistory'][-1]
        assert abs(event['socBefore']-event['socAfter'])<1e-12,event
        assert event['params']==base,(event,base)
        assert (state['controller'],state['data'],state['taskId'])==identity
        wait_for(lambda:state['time']>=event['time']+2,15)
        sample=rows[-1];assert expected(sample),dict(label=label,event=event,sample=sample)
        report['cases'].append(dict(label=label,event=event,sample=sample));print(label,event['time'],sample,flush=True)
    change('current',dict(current=2.),lambda r:abs(r[2]-2)<1e-5)
    change('current_limit',dict(iMax=.4),lambda r:abs(r[2]-.4)<1e-5)
    change('discharge',dict(mode=2,iMax=5.),lambda r:abs(r[2]+2)<1e-5)
    change('rest_while_paused',dict(mode=0),lambda r:abs(r[2])<1e-7,True)
    change('constant_voltage',dict(mode=3,voltage=3.275),lambda r:abs(r[3]-3.275)<1e-5 and r[2]>0)
    change('voltage_target',dict(voltage=3.28),lambda r:abs(r[3]-3.28)<1e-5 and r[2]>0)
    change('upper_cutoff',dict(mode=1,current=2.,voltage=3.27,vMax=3.27),lambda r:abs(r[2])<1e-7)
    change('lower_cutoff',dict(mode=2,vMax=3.45,vMin=3.26),lambda r:abs(r[2])<1e-7)
    change('release_cutoff',dict(vMin=2.5),lambda r:abs(r[2]+2)<1e-5)
    request('speed',task,dict(speed=20))
    wait_for(lambda:state['phase']=='Completed',60)
    job=pathlib.Path(state['jobDir']);manifest=json.loads((job/'result.json').read_text())
    report['jobDir']=str(job);report['manifest']=manifest
    assert manifest['complete'] and len(manifest['tuneHistory'])==len(report['cases'])
    assert manifest['csvHash']==hashlib.sha256((job/'result.csv').read_bytes()).hexdigest()
    import csv
    native=[[float(v) for v in r] for r in list(csv.reader((job/'result.csv').open()))[1:]]
    assert abs(native[0][1]-.5)<1e-6 and abs(native[-1][0]-90)<.02
    for previous,current in zip(native,native[1:]):
        dt=current[0]-previous[0];assert dt>=0
        assert abs(current[1]-previous[1]) <= 5*dt/9000+2e-6,(previous,current)
    # Check actual native solver outputs, not just the write acknowledgement.
    for case in report['cases']:
        event=case['event'];t=event['time']
        matching=[r for r in native if abs(r[0]-case['sample'][0])<.02]
        assert matching and all(abs(a-b)<1e-5 for a,b in zip(matching[-1],case['sample'])),case
    report['nativePoints']=len(native);report['ok']=True
except Exception:
    report['error']=traceback.format_exc();print(report['error'],flush=True)
finally:
    (artifact/'runtime_tuning.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    sock.close()
    # This process was created by this test and contains no user documents.
    if p.poll() is None:
        p.terminate()
        p.wait(timeout=10)
print('Evidence',artifact,flush=True)
sys.exit(0 if report['ok'] else 1)
