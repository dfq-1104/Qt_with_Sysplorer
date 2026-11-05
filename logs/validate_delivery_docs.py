from pathlib import Path
from urllib.parse import unquote
from collections import defaultdict
from datetime import datetime, timezone
import hashlib
import json
import re
import struct
import unicodedata

root = Path.cwd() / 'docs' / '交付文档'
docs = sorted(root.glob('0[1-6]_*.md'))
all_docs = sorted(root.glob('*.md'))
errors = []
warnings = []
stats = []
blocks = defaultdict(list)

def slug(s):
    s = re.sub(r'[`*]', '', s).strip().lower()
    return ''.join(c for c in s if unicodedata.category(c)[0] in 'LNM' or c in ' _-').replace(' ', '-')

def headings(text):
    return {slug(x) for x in re.findall(r'^#{1,6} (.+)$', text, re.M)}

if len(docs) != 6:
    errors.append(f'Expected six documents, found {len(docs)}')

for p in all_docs:
    text = p.read_text(encoding='utf-8')
    if '\ufffd' in text or '\x00' in text:
        errors.append(f'{p.name}: replacement/NUL character')
    opened = False
    lang = ''
    body = []
    diagrams = []
    outside = []
    table_width = None
    for number, line in enumerate(text.splitlines(), 1):
        if line.startswith('```'):
            if opened:
                if lang == 'mermaid':
                    diagram = '\n'.join(body)
                    diagrams.append(diagram)
                    first = diagram.splitlines()[0] if diagram else ''
                    if not re.match(r'^(flowchart (LR|TD)|sequenceDiagram|stateDiagram-v2)$', first):
                        errors.append(f'{p.name}:{number}: unknown/empty Mermaid type')
                    # Structural checks only; not an actual Mermaid parser.
                    if diagram.count('[') != diagram.count(']') or diagram.count('{') != diagram.count('}'):
                        errors.append(f'{p.name}:{number}: Mermaid delimiter mismatch')
                    if first == 'sequenceDiagram':
                        count = 0
                        for ln in diagram.splitlines():
                            if re.match(r'\s*(alt |loop |opt |par |critical |break )', ln):
                                count += 1
                            if ln.strip() == 'end':
                                count -= 1
                        if count:
                            errors.append(f'{p.name}:{number}: sequence block mismatch')
                opened = False
                lang = ''
                body = []
            else:
                opened = True
                lang = line[3:].strip()
            table_width = None
            continue
        if opened:
            body.append(line)
            continue
        outside.append(line)
        if line.startswith('|'):
            width = len(re.findall(r'(?<!\\)\|', line)) - 1
            if table_width is not None and width != table_width:
                errors.append(f'{p.name}:{number}: table width {width} != {table_width}')
            table_width = width
        else:
            table_width = None
    if opened:
        errors.append(f'{p.name}: unclosed fenced code block')
    md = '\n'.join(outside)
    for target in re.findall(r'!?\[[^\]]*\]\(([^)]+)\)', md):
        if re.match(r'^(https?|mailto):', target):
            continue
        raw_path, _, fragment = target.strip('<>').partition('#')
        q = (p.parent / unquote(raw_path)).resolve() if raw_path else p
        if not q.exists():
            errors.append(f'{p.name}: missing link {target}')
        elif fragment and q.suffix.lower() == '.md' and unquote(fragment) not in headings(q.read_text(encoding='utf-8')):
            errors.append(f'{p.name}: missing anchor {target}')
    if p in docs:
        for section in ['目录', '背景与目标', '范围与非目标', '术语表', '验收清单与交叉引用']:
            if not re.search(r'^## ' + re.escape(section) + r'$', text, re.M):
                errors.append(f'{p.name}: missing section {section}')
        for field in ['文档版本', '日期', '读者', '状态', '关联文档']:
            if f'| {field} |' not in text:
                errors.append(f'{p.name}: missing metadata {field}')
        if not diagrams:
            errors.append(f'{p.name}: no Mermaid diagram')
        for para in re.split(r'\n\s*\n', md):
            if para.lstrip().startswith(('|', '#', '- [', '>')):
                continue
            normalized = re.sub(r'\s+', '', para)
            if len(normalized) >= 120:
                blocks[normalized].append(p.name)
    stats.append({'file': p.name, 'lines': len(text.splitlines()), 'characters': len(text),
                  'mermaidDiagrams': len(diagrams), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()})

duplicates = [names for _, names in blocks.items() if len(set(names)) > 1]
if duplicates:
    errors.append(f'Cross-document exact paragraph duplicates >=120 characters: {duplicates}')

prompt_doc = (root / '06_提示词评价与后续建议.md').read_text(encoding='utf-8')
prompt_rows = len(re.findall(r'^\| P\d{2} \|', prompt_doc, re.M))
templates = len(re.findall(r'^### T[1-6]：', prompt_doc, re.M))
error_cases = len(re.findall(r'^### ERR-\d{2}：', (root / '03_错误总结文档.md').read_text(encoding='utf-8'), re.M))
if (prompt_rows, templates, error_cases) != (14, 6, 12):
    errors.append(f'Coverage counts: prompts={prompt_rows}, templates={templates}, cases={error_cases}')

assets = []
for p in sorted((root / 'assets').glob('*.png')):
    data = p.read_bytes()
    if not data.startswith(b'\x89PNG\r\n\x1a\n'):
        errors.append(f'Invalid PNG: {p.name}')
    else:
        width, height = struct.unpack('>II', data[16:24])
        assets.append({'file': 'assets/' + p.name, 'width': width, 'height': height,
                       'sha256': hashlib.sha256(data).hexdigest()})

warnings.append('Mermaid renderer/parser unavailable locally: structural checks only, no render acceptance claimed.')
result = {'status': 'passed' if not errors else 'failed',
          'checkedAtUtc': datetime.now(timezone.utc).isoformat(),
          'formalDocuments': len(docs), 'files': stats, 'assets': assets,
          'coverage': {'visiblePromptGroups': prompt_rows, 'promptTemplates': templates, 'errorCases': error_cases},
          'manualReview': ['Current implementation distinguished from historical/intermediate solutions and proposals.',
                           'Unique detailed ownership reviewed for protocols, features, cases, UI state, ADRs and prompt evaluation.'],
          'errors': errors, 'limitations': warnings,
          'duplicateCheck': 'Exact normalized prose paragraphs >=120 characters, excluding boilerplate/tables/code; manual ownership review also required.'}
(root / '交付检查记录.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'status': result['status'], 'formalDocuments': len(docs),
                  'mermaidDiagrams': sum(x['mermaidDiagrams'] for x in stats),
                  'assets': len(assets), 'errors': errors, 'limitations': warnings}, ensure_ascii=False, indent=2))
raise SystemExit(1 if errors else 0)
