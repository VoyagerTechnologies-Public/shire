"""Bounded flight-event classification for EPS functional scenarios."""
import datetime as dt
from pathlib import Path
import re

def event_gate(logs, verification, root):
    error_ids = {}
    headers = list((root/'comp').glob('*/src/*events.h')) + list((root/'cfs/apps').glob('*/fsw/**/*events.h')) + list((root/'cfs/cfe/modules').glob('*/fsw/inc/*eventids.h'))
    for header in headers:
        if 'modules' in header.parts:
            name='CFE_'+header.parts[header.parts.index('modules')+1].upper()
        else:
            name=(header.parts[header.parts.index('apps')+1] if 'apps' in header.parts else header.parent.parent.name).upper()+'_APP'
        definitions=re.findall(r'#define\s+(\w+)\s+(\d+)\b',header.read_text())
        error_ids.setdefault(name,{}).update({int(n):symbol for symbol,n in definitions
            if '_ERR_' in symbol or '_ERROR_' in symbol or symbol.endswith(('_ERR','_ERROR'))})
    windows=[]
    for stack in verification.get('stacks',[]):
        open_windows={}
        for step in stack.get('steps',[]):
            name=step.get('name') or ''
            for kind in ('OUTAGE','FAULT','RECOVERY'):
                if name == kind+'_BEGIN': open_windows[kind]=dt.datetime.fromisoformat(step['wall_start'])
                if name == kind+'_END' and kind in open_windows:
                    windows.append((kind,open_windows.pop(kind),dt.datetime.fromisoformat(step['wall_end'])+dt.timedelta(seconds=1)))
        # An interrupted phase ends at its last observed step, never at an unbounded future time.
        for kind,start in open_windows.items():
            if stack.get('steps'): windows.append((kind,start,dt.datetime.fromisoformat(stack['steps'][-1]['wall_end'])))
    allowed={'DEMO_APP':{41,42,43}, 'ADCS_APP':{41,42,43}, 'RADIO_APP':{41,42,43}, 'EPS_APP':{14,16,41}}
    events=[]; unexpected=[]
    for role,log in logs.items():
        for line in log.splitlines():
            match=re.search(r'/([A-Z_]+)\s+(\d+):',line)
            if match:
                app,eid=match[1],int(match[2]); symbol=error_ids.get(app,{}).get(eid)
                event={'role':role,'app':app,'id':eid,'symbol':symbol,'line':line,'expected':False}
                if symbol:
                    try: stamp=dt.datetime.fromisoformat(line.split()[0].replace('Z','+00:00'))
                    except ValueError: stamp=None
                    event['expected']=stamp is not None and eid in allowed.get(app,set()) and any(start<=stamp<=end for _,start,end in windows)
                    if not event['expected']: unexpected.append(event)
                events.append(event)
            elif ' ERROR' in line or ' CRITICAL' in line:
                event={'role':role,'line':line,'expected':False};events.append(event);unexpected.append(event)
    return {'passed':not unexpected,'events':events,'unexpected':unexpected,
            'windows':[{'phase':kind,'start':start.isoformat(),'end':end.isoformat()} for kind,start,end in windows]}
