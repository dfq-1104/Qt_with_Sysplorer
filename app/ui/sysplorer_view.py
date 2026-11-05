"""View the C++ SDK result in the installed Sysplorer; never simulate here."""
import json
import os
from pathlib import Path
import sys
import time
import traceback

job = Path(sys.argv[1])
exe = sys.argv[2]
report_path = job / 'sysplorer_view.json'

def report(**values):
    temporary = report_path.with_suffix('.tmp')
    temporary.write_text(json.dumps(values, ensure_ascii=False, indent=2), encoding='utf-8')
    os.replace(str(temporary), str(report_path))

try:
    report(stage='connecting')
    import mworks.sysplorer as eng
    import psutil
    ports = eng.FindSysplorer()
    # FindSysplorer can also return console instances; only reuse GUI processes.
    gui_ports = []
    for port in ports:
        for conn in psutil.net_connections(kind='tcp'):
            if conn.laddr and conn.laddr.port == port and conn.pid:
                try:
                    process = psutil.Process(conn.pid)
                    cmd = process.cmdline()
                    same_exe = os.path.normcase(os.path.realpath(process.exe())) == os.path.normcase(os.path.realpath(exe))
                    if same_exe and not any(x.lower() in ('-q', '-quiet', '--quiet') for x in cmd):
                        gui_ports.append(port)
                except psutil.Error:
                    pass
                break
    reused = bool(gui_ports)
    if reused:
        eng.ConnectSysplorer(port=gui_ports[0])
    else:
        eng.StartSysplorer(processPath=exe)
    if not eng.SendHeart():
        raise RuntimeError('Sysplorer GUI connection failed')
    report(stage='connected', reused=reused)
    deadline = time.monotonic() + 330
    request_path = job / 'sysplorer_view_request.json'
    while not request_path.exists():
        if time.monotonic() > deadline:
            raise TimeoutError('No validated SDK result received within 330 seconds')
        time.sleep(0.3)
    request = json.loads(request_path.read_text(encoding='utf-8-sig'))
    if not request.get('ok'):
        report(stage='cancelled', reused=reused)
    else:
        result = request['result']
        # CreatePlot(resultFile=...) loads an absent result and reuses an already
        # loaded result. OpenResult emits a modal "already opened" dialog.
        plots = []
        for name, title in [('cell.SOC', 'SOC'), ('cell.V_term', 'Voltage'), ('cell.I_act', 'Current')]:
            plots.append(bool(eng.CreatePlot(y=[name], heading='BatteryConsole ' + title, resultFile=result)))
        if not all(plots):
            raise RuntimeError('Sysplorer loaded the result but could not draw all three plots')
        report(stage='opened', reused=reused, result=result, plots=plots)
except Exception:
    report(stage='failed', error=traceback.format_exc())
    sys.exit(1)
