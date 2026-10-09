"""Read EPS diagnostics from the director console, outside the Yamcs MDB."""
import argparse
import json
import math
import subprocess
import time

from shire_runner_lib import parse_marker

FIELDS = 'DYN_TIME TIME ENERGY_WH SOC SOLAR_POWER_W LOAD_POWER_W EXPECTED_ENERGY_WH ENERGY_RESIDUAL_WH BACKDOOR_ACCEPTED BACKDOOR_REJECTED BACKDOOR_LAST_ID BACKDOOR_LAST_STATUS BACKDOOR_GENERATION BACKDOOR_APPLIED_TIME SOLAR_OVERRIDE CRC_REMAINING FAIL_REMAINING FAIL_SELECTOR INITIAL_ENERGY_WH SOLAR_ENERGY_WH LOAD_ENERGY_WH ADJUSTMENT_WH'.split()
FIELDS += [f'SWITCH_{i}_{s}' for i in range(8) for s in 'REQUESTED EFFECTIVE FAULT VOLTAGE_V CURRENT_A POWER_W'.split()]
FIELDS += [f'{n}_{s}' for n in ['DEMO','ADCS','RADIO'] for s in 'SUPPLIED READY MODE CYCLES SUCCESSFUL_REQUESTS REJECTED_REQUESTS LAST_RESPONSE_TIME POWER_W SCALE RF_RECEIVED RF_SENT SWITCH'.split()]
FIELDS += ['BACKDOOR_APPLIED_TICK','SOLAR_OVERRIDE_W','EPS_REQUESTS_SUCCESSFUL','EPS_REQUESTS_REJECTED']


PREFIX = '/EPS_CONSOLE/'


def decode_state(log):
    record = parse_marker(log, 'EPS_SIM_STATE')
    values = record['values']
    if len(values) != 110 or not all(isinstance(v, (int, float)) and not isinstance(v, bool)
                                    and math.isfinite(v) for v in values):
        raise ValueError('invalid EPS console state vector')
    if round(values[1] * 1e9) != record['time_ns']:
        raise ValueError('EPS console time does not match its boundary')
    return record, dict(zip(FIELDS[:110], values))


class EpsConsole:
    def __init__(self, container, read_log=None):
        self.container = container
        self.read_log = read_log or self._docker_log
        self._cached = None
        self._expires = 0.

    def _docker_log(self):
        result = subprocess.run(['docker', 'logs', '--tail', '256', self.container],
                                capture_output=True, text=True, timeout=10)
        if result.returncode:
            raise RuntimeError('cannot read EPS simulator console: ' + result.stderr.strip())
        return result.stdout + result.stderr

    def snapshot(self):
        if self._cached is None or time.monotonic() >= self._expires:
            try:
                self._cached = decode_state(self.read_log())
            except (ValueError, KeyError) as exc:
                raise RuntimeError('invalid EPS simulator console: ' + str(exc)) from exc
            self._expires = time.monotonic() + .1
        return self._cached

    def parameter(self, name):
        if not name.startswith(PREFIX):
            raise ValueError('not an EPS console diagnostic: ' + name)
        record, values = self.snapshot()
        key = name.removeprefix(PREFIX)
        if key not in values:
            raise ValueError('unknown EPS console diagnostic: ' + key)
        return {'value': values[key], 'raw': {'acquisitionTime': f"{record['time_ns']:020d}",
                'generationTime': values['TIME'], 'source': 'director console'}}

    def time(self):
        return self.parameter(PREFIX+'TIME')['value']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--container', required=True, help='Director container name')
    parser.add_argument('--follow', action='store_true', help='Print each newly observed console snapshot')
    args = parser.parse_args()
    console = EpsConsole(args.container)
    previous = None
    try:
        while True:
            record, values = console.snapshot()
            if record['time_ns'] != previous:
                print(json.dumps(values, indent=2), flush=True)
                previous = record['time_ns']
            if not args.follow: return 0
            time.sleep(.5)
    except KeyboardInterrupt:
        return 0


if __name__ == '__main__':
    raise SystemExit(main())
