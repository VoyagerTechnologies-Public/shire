#!/usr/bin/env python3
"""Generate standalone EPS campaign reports and plots from retained evidence.

Provide smoke, nominal and fault campaigns, nominal/fault replay reports, and an
output directory. Each path may identify a report or its containing directory.
Requires Matplotlib 3.9 or later and PyYAML.
"""
from __future__ import annotations
import argparse
import csv
import importlib.util
import json
import math
from pathlib import Path
import statistics
import sys
import yaml


def wilson(passed, total):
    if not total: return (0.,1.)
    z=1.959963984540054; fraction=passed/total; den=1+z*z/total
    center=(fraction+z*z/(2*total))/den
    radius=z*math.sqrt(fraction*(1-fraction)/total+z*z/(4*total*total))/den
    return center-radius,center+radius


def load_campaign(path):
    path=Path(path)
    if path.is_dir(): path=path/'campaign_report.json'
    return path,json.loads(path.read_text())


def sampling_check(report, directory):
    """Recreate every deterministic draw and compare both values and saved ICs."""
    spec=importlib.util.spec_from_file_location('shire_campaign',Path(__file__).with_name('shire-campaign.py'))
    campaign=importlib.util.module_from_spec(spec);spec.loader.exec_module(campaign)
    cfg=report['campaign']
    base=yaml.safe_load((campaign.IC_DIR/(cfg['base_initial_conditions']+'.yaml')).read_text())
    samples=campaign.sample_trials(cfg,base)
    issues=[]
    for trial,(ic,detail) in zip(report['trials'],samples):
        values={row['path']:row['resolved_value'] for row in detail}
        if trial['parameters'] != values:issues.append({'trial':trial['trial_index'],'reason':'sample mismatch'})
        saved=yaml.safe_load((directory/trial['ic_file']).read_text())
        saved = {k:v for k,v in saved.items() if k not in ('name','description')}
        ic = {k:v for k,v in ic.items() if k not in ('name','description')}
        if saved != ic:issues.append({'trial':trial['trial_index'],'reason':'saved IC mismatch'})
    return {'passed':len(samples)==len(report['trials']) and not issues,'sample_count':len(samples),'issues':issues}


def replay_check(path, campaign_path, required_indices):
    path=Path(path)
    if path.is_dir():path=path/'replay-report.json'
    report=json.loads(path.read_text())
    trials=report.get('trials',[])
    indices=[t['trial_index'] for t in trials]
    passed=(report.get('passed') is True and len(indices)==len(required_indices)
            and set(indices)==set(required_indices)
            and Path(report['campaign']).resolve()==Path(campaign_path).resolve()
            and all(t.get('passed') is True and not t.get('issues') for t in trials))
    return {'passed':passed,'source':str(path.resolve()),'trial_indices':indices}


def analyze(paths, output, replay_paths=()):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    datasets=[];rows=[];issues=[]
    for name,path,required,seed in paths:
        source,report=load_campaign(path);cfg=report['campaign'];trials=report['trials']
        sampling=sampling_check(report,source.parent)
        good=sum(t.get('pass') is True and t.get('error') is None for t in trials)
        interval=wilson(good,len(trials))
        entry={'name':name,'source':str(source.resolve()),'required_trials':required,'trials':len(trials),'passed':good,
               'seed':cfg['seed'],'wilson_95':interval,'sampling_replay':sampling}
        if len(trials)!=required or good!=required or cfg['seed']!=seed or not sampling['passed']:
            issues.append({'campaign':name,'reason':'trial count, pass count, seed or sampling acceptance failed'})
        fault_counts={i:0 for i in range(1,10)}
        for trial in trials:
            folder=source.parent/trial['report_dir'];result=json.loads((folder/'result.json').read_text()) if (folder/'result.json').exists() else {}
            events=result.get('event_gate',{})
            if (result.get('eps_acceptance_basis') != 'fresh FSW stack results'
                    or not result.get('verification',{}).get('passed')
                    or not events.get('passed') or not result.get('archive',{}).get('replay_ready')):
                issues.append({'campaign':name,'trial':trial['trial_index'],'reason':'missing or failed FSW, event or archive acceptance'})
            for m in cfg['metrics']:
                if m.get('required') and trial.get('metrics',{}).get(m['name']) is None:
                    issues.append({'campaign':name,'trial':trial['trial_index'],'reason':'missing '+m['name']})
            commands = [step for stack in result.get('verification',{}).get('stacks',[])
                        for step in stack.get('steps',[]) if step.get('type')=='command'
                        and step.get('status')=='passed']
            suffixes=['SET_SOC','SET_SOLAR','SET_SWITCH','SET_LOAD_SCALE','SET_SWITCH_FAULT',
                      'CORRUPT_HK_CRC','FAIL_REQUESTS','CLEAR_FAULTS','RESET']
            for i,suffix in enumerate(suffixes,1):
                fault_counts[i]+=sum(step.get('name')=='/EPS/BACKDOOR_EPS_'+suffix for step in commands)
            if name=='faults' and not all(any(step.get('name')=='/EPS/BACKDOOR_EPS_'+suffix
                                           for step in commands) for suffix in suffixes):
                issues.append({'campaign':name,'trial':trial['trial_index'],'reason':'one or more backdoor commands not exercised'})
            rows.append({'campaign':name,'trial':trial['trial_index'],'passed':trial.get('pass'),**trial.get('metrics',{})})
        entry['backdoor_commands']=fault_counts
        datasets.append((entry,trials))
    replays={}
    for name,path,indices in replay_paths:
        campaign=next(e for e,_ in datasets if e['name']==name)
        try:replays[name]=replay_check(path,campaign['source'],indices)
        except (OSError,ValueError,KeyError) as exc:replays[name]={'passed':False,'error':str(exc)}
        if not replays[name]['passed']:issues.append({'campaign':name,'reason':'selected functional replay failed'})
    for name in ('nominal','faults'):
        if name not in replays:issues.append({'campaign':name,'reason':'selected functional replay evidence missing'})
    summary={'passed':not issues,'campaigns':[entry for entry,_ in datasets],'functional_replays':replays,'issues':issues}
    (output/'acceptance.json').write_text(json.dumps(summary,indent=2)+'\n')
    keys=list(dict.fromkeys(k for row in rows for k in row))
    with (output/'metrics.csv').open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=keys);writer.writeheader();writer.writerows(rows)
    plt.rcParams.update({'font.size':10,'figure.dpi':140})
    def save(fig,name):
        fig.tight_layout();fig.savefig(output/(name+'.png'),dpi=240);fig.savefig(output/(name+'.pdf'));plt.close(fig)
    fig,ax=plt.subplots(figsize=(7,4));xs=list(range(len(datasets)))
    rates=[e['passed']/e['trials'] for e,_ in datasets]
    ax.bar(xs,rates,color=['#247887','#4168b0','#bc6d32'])
    ax.errorbar(xs,rates,yerr=[[r-e['wilson_95'][0] for r,(e,_) in zip(rates,datasets)], [e['wilson_95'][1]-r for r,(e,_) in zip(rates,datasets)]],fmt='none',color='black',capsize=5)
    ax.set(xticks=xs,xticklabels=[f"{e['name']} ({e['passed']}/{e['trials']})" for e,_ in datasets],ylim=(0,1.08),ylabel='Pass fraction',title='EPS campaign outcomes and 95% Wilson intervals');save(fig,'pass-rates')
    fig,ax=plt.subplots(figsize=(7,4))
    for entry,trials in datasets:
        recovery=[t.get('metrics',{}).get('recovery_time_s') for t in trials]
        ax.plot([v for v in recovery if v is not None],'.',label=entry['name'])
    ax.set(xlabel='Trial',ylabel='Recovery phase duration (wall s)');ax.legend();save(fig,'recovery-time')
    nominal=next((t for e,t in datasets if e['name']=='nominal'),[])
    fig,ax=plt.subplots(figsize=(8,4))
    populated=[(i,[t['metrics'][f'switch_{i}_current_a'] for t in nominal
                  if t.get('metrics',{}).get(f'switch_{i}_current_a') is not None]) for i in range(8)]
    populated=[(i,v) for i,v in populated if v]
    if populated:ax.boxplot([v for _,v in populated],tick_labels=[str(i) for i,_ in populated])
    ax.set(xlabel='Switch',ylabel='FSW rail current (A)',title='Rail currents after startup restoration');save(fig,'rail-current')
    topology=[[sum(t['parameters'].get(f'component_overrides.eps.loads.{device}.switch')==i for t in nominal) for i in range(8)] for device in ('demo','adcs','radio')]
    fig,ax=plt.subplots(figsize=(8,3));im=ax.imshow(topology,cmap='Blues',aspect='auto');ax.set(xticks=range(8),yticks=range(3),yticklabels=['DEMO','ADCS','Radio'],xlabel='Switch',title='Nominal sampled load assignments')
    topology_max=max((v for row in topology for v in row),default=0)
    for y,row in enumerate(topology):
        for x,v in enumerate(row):ax.text(x,y,str(v),ha='center',va='center',color='white' if topology_max and v>=.7*topology_max else 'black')
    fig.colorbar(im,ax=ax,label='Trials');save(fig,'topology')
    faults=next((e['backdoor_commands'] for e,_ in datasets if e['name']=='faults'),{})
    fig,ax=plt.subplots(figsize=(10,4));ax.bar(range(1,10),[faults.get(i,0) for i in range(1,10)],color='#bc6d32')
    ax.set(xticks=range(1,10),xticklabels=['SOC','Solar','Switch','Scale','Stuck','CRC','Requests','Clear','Reset'],ylabel='Commands sent in passing stacks',title='Backdoor commands exercised in the fault campaign');save(fig,'fault-outcomes')
    lines=['# EPS acceptance evidence','',f"Acceptance: **{'PASS' if summary['passed'] else 'FAIL'}**",'', '| Campaign | Completed and passed | Seed | 95% Wilson interval |','|---|---:|---:|---:|']
    for entry,_ in datasets:
        lo,hi=entry['wilson_95'];lines.append(f"| {entry['name']} | {entry['passed']}/{entry['required_trials']} | {entry['seed']} | {lo:.4f}–{hi:.4f} |")
    lines += ['', 'Acceptance uses fresh FSW stack results, complete named FSW metrics, startup restoration, no unexpected events, and complete Yamcs archives.', '', 'Backdoor command counts show commands sent in passing stacks. Their observable effects are checked through FSW telemetry. These results do not establish exact simulator energy, internal fault counts, or actuation and traffic at every simulation tick.', '', 'Sampling is replayed against archived inputs. Selected fresh functional replays compare resolved wiring, FSW rail readings and command order. Per-trial folders retain settings, verification samples, events, logs and archive identifiers.','']
    for name in ('pass-rates','recovery-time','rail-current','topology','fault-outcomes'):lines.append(f'![{name}]({name}.png)\n')
    if issues:lines+=['## Unmet acceptance checks','', '```json',json.dumps(issues,indent=2),'```']
    (output/'REPORT.md').write_text('\n'.join(lines)+'\n')
    return summary


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('smoke','nominal','faults'):p.add_argument('--'+name,required=True)
    for name in ('nominal','fault'):p.add_argument('--'+name+'-replay',required=True)
    p.add_argument('--output',required=True);a=p.parse_args()
    report=analyze([('smoke',a.smoke,5,20261006),('nominal',a.nominal,100,20261006),('faults',a.faults,20,20261007)],a.output,
                   [('nominal',a.nominal_replay,[0,50,99]),('faults',a.fault_replay,[0,19])])
    print(json.dumps({'passed':report['passed'],'issues':len(report['issues'])}))
    return 0 if report['passed'] else 1

if __name__=='__main__':sys.exit(main())
