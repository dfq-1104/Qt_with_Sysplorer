import json
import traceback
report = {}
try:
    path = 'C:/Users/TR/AppData/Local/Temp/BatteryConsole/20261104_022152_c6ca2583-d85f-4d53-8e1e-3819de1668de/solver/Result.msr'
    report['open'] = bool(OpenResult(path))
    if report['open']:
        report['plot'] = bool(CreatePlot(y=['cell.SOC'], resultFile=path, heading='Battery SDK SOC'))
except Exception:
    report['error'] = traceback.format_exc()
with open('C:/Users/TR/Documents/ChatGPT/qt界面开发/logs/probe_open_result.json', 'w', encoding='utf-8') as f:
    json.dump(report, f, ensure_ascii=False, indent=2)
