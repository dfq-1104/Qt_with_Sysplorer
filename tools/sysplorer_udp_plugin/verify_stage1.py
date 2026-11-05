"""Real UDP / real Sysplorer integration. No model or solver substitutes."""
import json,os,pathlib,socket,subprocess,time,uuid,hashlib,traceback,sys
root=pathlib.Path(__file__).resolve().parent
artifact=root/'artifacts'/time.strftime('%Y%m%d_%H%M%S')
artifact.mkdir(parents=True,exist_ok=False)
report={'ok':False,'cases':[]}
def uid():return str(uuid.uuid4())
exe=r'C:\Program Files\MWORKS\Sysplorer 2026a\Bin64\mworks.exe'
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
        if p.poll() is not None:raise RuntimeError('Test host exited '+str(p.returncode))
        if endpoint.exists():
            session=json.loads(endpoint.read_text(encoding='utf-8-sig'))
            try:
                hello=request('hello')
                if hello.get('ready') and hello['status']['phase']=='Ready':break
            except (socket.timeout,AssertionError):pass
        time.sleep(.5)
    else:raise TimeoutError('No ready plugin')
    report['session']={k:v for k,v in session.items() if k!='token'}
    for partial,fast in ((True,False),(False,False),(False,True)):
        task=uid();args=dict(modelHash=session['modelHash'],mode=1 if partial else 2,voltage=3.45 if partial else 2.5,current=1.0 if partial else 2.0,soc0=.5 if partial else .6,duration=120 if fast else (12 if partial else 6),speed=60 if fast else 1)
        case={'taskId':task,'partial':partial,'fast':fast,'args':args,'statuses':[]};report['cases'].append(case)
        begin=request('begin',task,args);case['begin']=begin
        # Re-sending one accepted request must not create a second task.
        assert request('begin',task,args)['status']['taskId']==task
        cursor=0;rows=[];paused=False;resumed=False;stopping=False;previous_phase='';run_started=None;deadline=time.monotonic()+150
        while time.monotonic()<deadline:
            state=poll(task,cursor);cursor=state['next'];rows.extend(state['rows'])
            case['statuses'].append({k:state[k] for k in ('phase','time','total','controller','data')})
            phase=state['phase']
            if phase=='Running' and run_started is None:run_started=time.monotonic()
            if fast and run_started and phase=='Running' and time.monotonic()-run_started>12:
                request('stop',task);raise AssertionError('60x solver speed did not take effect')
            if phase!=previous_phase:print(task[:8],phase,state['time'],'live',len(rows),flush=True);previous_phase=phase
            if phase=='Failed':raise AssertionError(state)
            if partial and phase=='Running' and rows and rows[-1][0]>=1 and not paused:
                request('pause',task);paused=True
            elif partial and phase=='Paused' and paused and not resumed:
                t=state['time']
                for _ in range(8):
                    time.sleep(.15);check=poll(task,cursor);cursor=check['next'];rows.extend(check['rows'])
                    assert check['phase']=='Paused',check
                assert abs(check['time']-t)<.05,(t,check)
                case['pausedAt']=t;request('resume',task);resumed=True
            elif partial and resumed and phase=='Running' and state['time']>case['pausedAt']+1 and not stopping:
                request('stop',task);stopping=True
            if phase in ('Completed','Stopped'):
                while cursor<state['total']:
                    state=poll(task,cursor);cursor=state['next'];rows.extend(state['rows'])
                case['terminal']=state;case['runningWallSeconds']=time.monotonic()-run_started if run_started else None;break
            time.sleep(.12)
        else:raise TimeoutError('Task did not finish')
        assert len(rows)>2,'No real-time streamed samples'
        assert any(abs(r[1]-args['soc0'])>1e-5 for r in rows),'SOC did not change in live stream'
        assert any(abs(r[2]-(1 if partial else -2))<1e-4 for r in rows),'User current did not take effect'
        assert case['terminal']['phase']==('Stopped' if partial else 'Completed')
        job=pathlib.Path(state['jobDir']);manifest=json.loads((job/'result.json').read_text(encoding='utf-8'))
        assert manifest['complete']==(not partial)
        assert manifest['csvHash']==hashlib.sha256((job/'result.csv').read_bytes()).hexdigest()
        case['manifest']=manifest;case['liveRows']=rows
        assert state['controller']==report['cases'][0]['begin']['status']['controller']
        # An unrelated task must not control this task.
        denied=request('stop',uid(),accepted=False);assert not denied['ok'];case['foreignTaskRejected']=True
    report['ok']=True
except Exception:
    report['error']=traceback.format_exc();print(report['error'],flush=True)
finally:
    (artifact/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    sock.close()
print('Evidence',artifact,flush=True)
sys.exit(0 if report['ok'] else 1)
