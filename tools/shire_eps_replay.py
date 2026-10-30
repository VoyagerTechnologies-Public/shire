#!/usr/bin/env python3
"""Replay selected archived EPS campaign inputs and compare functional evidence."""
import argparse
from contextlib import contextmanager
import fcntl
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import sys
import yaml

ROOT = Path(__file__).resolve().parent.parent


@contextmanager
def clear_trial_settings_on_exit(runner):
    """Leave the next ordinary build free of archived trial overrides."""
    try:
        yield
    finally:
        runner.ACTIVE_LOCK_PATH.parent.mkdir(parents=True, exist_ok=True)
        with runner.ACTIVE_LOCK_PATH.open('w') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            try:
                runner.reset_active_campaign_fields()
            finally:
                fcntl.flock(lock, fcntl.LOCK_UN)


def compare(reference, replay, old_metrics, new_metrics, old_config, new_config):
    issues = []
    if not reference.get('pass') or not replay.get('pass'):
        issues.append('reference or replay failed acceptance')
    if old_config['eps'] != new_config['eps']:
        issues.append('resolved EPS configuration changed')
    # Wiring and stack behavior must agree. Live transaction counts and
    # battery voltage can vary with wall-clock command admission.
    stable = [key for key in old_metrics if key.startswith('switch_')]
    for key in stable:
        a, b = old_metrics[key], new_metrics.get(key)
        tolerance = 32/255 if '_voltage_' in key else 10/255
        if a is None or b is None or not math.isclose(a, b, rel_tol=0, abs_tol=tolerance+1e-7):
            issues.append('metric changed: ' + key)
    def commands(result):
        return [step['name'] for stack in result.get('verification', {}).get('stacks', [])
                for step in stack.get('steps', []) if step.get('type') == 'command'
                and step.get('name', '').startswith('/EPS/BACKDOOR_EPS_')]
    if commands(reference) != commands(replay):
        issues.append('backdoor command order changed')
    return {'passed': not issues, 'issues': issues, 'compared_metrics': stable}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--campaign-report', required=True)
    parser.add_argument('--trials', required=True, help='Comma-separated trial indices')
    parser.add_argument('--report-dir', required=True)
    parser.add_argument('--port-offset', type=int, default=1200)
    args = parser.parse_args()
    source = Path(args.campaign_report).resolve()
    if source.is_dir():
        source /= 'campaign_report.json'
    campaign = json.loads(source.read_text())
    indices = [int(value) for value in args.trials.split(',')]
    selected = {t['trial_index']: t for t in campaign['trials']}
    if not indices or any(i not in selected for i in indices):
        parser.error('trial selection is outside the archived campaign')
    destination = Path(args.report_dir).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('shire_campaign', ROOT/'tools/shire-campaign.py')
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    outcomes = []
    with clear_trial_settings_on_exit(runner):
        for index in indices:
            trial = selected[index]
            original = source.parent/trial['report_dir']
            reference = json.loads((original/'result.json').read_text())
            target = destination/f'trial-{index:04d}'
            target.mkdir(parents=True, exist_ok=True)
            command = [sys.executable, str(ROOT/'tools/shire-scenario.py'),
                       '--scenario', reference['scenario'], '--no-build',
                       '--image-tag', reference['image_tag'],
                       '--instance-id', f'epr{index:04d}', '--port-offset', str(args.port_offset),
                       '--initial-conditions-file', str(source.parent/trial['ic_file']),
                       '--report-dir', str(target)]
            print(f'[eps-replay] trial {index}: fresh replay starting', flush=True)
            with (target/'replay.log').open('w') as log:
                code = subprocess.call(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
            if code or not (target/'result.json').exists():
                outcome = {'passed': False, 'issues': ['replay process failed'], 'exit_code': code}
            else:
                replay = json.loads((target/'result.json').read_text())
                metrics = runner.extract_metrics(target, campaign['campaign']['metrics'])
                old_config = yaml.safe_load((original/'resolved-settings.yaml').read_text())
                new_config = yaml.safe_load((target/'resolved-settings.yaml').read_text())
                outcome = compare(reference, replay, trial['metrics'], metrics, old_config, new_config)
                outcome['metrics'] = metrics
            outcome.update(trial_index=index, reference=str(original), report_dir=str(target))
            outcomes.append(outcome)
            (destination/'replay-report.json').write_text(json.dumps(
                {'passed': all(o['passed'] for o in outcomes), 'campaign': str(source), 'trials': outcomes},
                indent=2)+'\n')
            print(f"[eps-replay] trial {index}: passed={outcome['passed']}", flush=True)
        return 0 if all(o['passed'] for o in outcomes) else 1


if __name__ == '__main__':
    sys.exit(main())
