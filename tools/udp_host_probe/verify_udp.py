import json, pathlib, socket, time, uuid, traceback, sys
root=pathlib.Path(__file__).resolve().parent
job=root/(sys.argv[1] if len(sys.argv)>1 else 'evidence')
session=json.loads((job/'session.private.json').read_text(encoding='utf-8'))
model=sys.argv[2] if len(sys.argv)>2 else 'UdpProbeModel'
report={'model':model,'pid':session['pid'],'port':session['port'],'executable':session['executable'],'commands':[],'ok':False}
sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
sock.bind(('127.0.0.1',0));sock.settimeout(5)
def call(command,allow_rejected=False,**kw):
    request=dict(id=uuid.uuid4().hex,token=session['token'],command=command,**kw)
    data=json.dumps(request).encode()
    for attempt in range(3):
        sock.sendto(data,('127.0.0.1',session['port']))
        try:
            while True:
                raw,peer=sock.recvfrom(32768)
                result=json.loads(raw)
                if result['id']==request['id']:break
            break
        except socket.timeout:
            if attempt==2:raise
    assert result['pid']==session['pid'], result
    report['commands'].append(result)
    print(command,json.dumps(result,ensure_ascii=True),flush=True)
    if not result.get('ok'):
        if allow_rejected:return None
        raise AssertionError(result)
    return result.get('after',{})
def wait_for(expected,timeout=60):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        s=call('status')
        if s.get('state')==expected:return s
        time.sleep(.5)
    raise TimeoutError(expected)
try:
    s=call('status');assert s['sameAsHostPanel']
    call('load',model=model)
    baseline=len((job/'host.jsonl').read_text(encoding='utf-8').splitlines())
    translate_deadline=time.monotonic()+60
    while call('translate',allow_rejected=True) is None:
        if time.monotonic()>translate_deadline:raise TimeoutError('Host refused translation')
        time.sleep(1)
    deadline=time.monotonic()+90
    while time.monotonic()<deadline:
        lines=[json.loads(x) for x in (job/'host.jsonl').read_text(encoding='utf-8').splitlines()[baseline:] if x.strip()]
        events=[x for x in lines if x.get('event')=='translated']
        if events:
            assert events[-1]['ok'],events[-1]
            break
        time.sleep(.5)
    else:raise TimeoutError('No successful host translation signal')
    report['translation']=events[-1]
    s=call('status')
    controller=s['controller'];data=s['data']
    call('start');s=wait_for('Running');data=s['data'];assert data!='0x0',s
    time.sleep(2)
    call('pause');a=wait_for('Paused')
    time.sleep(2);b=call('status')
    assert b['state']=='Paused' and abs(a['time']-b['time'])<.001,(a,b)
    report['pausedTimeStable']=[a['time'],b['time']]
    import mworks.sysplorer as eng
    eng.ConnectSysplorer(port=session['apiport'])
    report['officialApiPausedState']=eng.GetSimulationState()
    report['officialApiPausedTime']=eng.GetCurrentSimTime()
    assert report['officialApiPausedState']=='Paused',report
    call('resume');wait_for('Running')
    time.sleep(2);resumed=call('status')
    assert resumed['time']>b['time']+.5,resumed
    assert resumed['controller']==controller and resumed['data']==data,resumed
    call('stop');last=wait_for('Idle')
    report['officialApiStoppedState']=eng.GetSimulationState()
    report['controller']=controller;report['data']=data
    report['ok']=True
except Exception:
    report['error']=traceback.format_exc()
    print(report['error'],flush=True)
finally:
    (job/('verification_battery.json' if model=='BatterySim.SingleRun' else 'verification.json')).write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
    sock.close()
sys.exit(0 if report['ok'] else 1)



