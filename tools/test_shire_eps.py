"""EPS configuration and FSW acceptance workflow checks."""
from copy import deepcopy
from pathlib import Path
import tempfile
import unittest
import yaml
from shire_eps_config import normalize_eps, merge_component_config
from shire_eps_stacks import generate_stacks, write_xtce
from shire_eps_analysis import event_gate

ROOT=Path(__file__).resolve().parent.parent
BASE=yaml.safe_load((ROOT/'comp/eps/support/device_config.yaml').read_text())['eps']

class EpsConfigurationTests(unittest.TestCase):
    def resolve(self, overrides=None, available=('demo','adcs','radio')):
        return normalize_eps({**BASE,'wire_default_loads':True,**(overrides or {})},set(available))
    def test_defaults_and_shared_wiring(self):
        cfg=self.resolve()
        self.assertEqual([s['startup_on'] for s in cfg['switches']],[True,False,False,False,True,False,True,False])
        for name,load in cfg['loads'].items():
            self.assertEqual(load['boot_power_w'],max(load['mode_power_w']))
        for rail in range(8):
            cfg=self.resolve({'loads':{n:{'switch':rail} for n in ('demo','adcs','radio')}})
            self.assertEqual(sum(s['startup_on'] for s in cfg['switches']),1)
            self.assertTrue(cfg['switches'][rail]['startup_on'])
    def test_finite_bounds_availability_and_representability(self):
        bad=[{'battery_initial_soc':v} for v in (-1,60,1.1,float('nan'),True)]
        bad += [{'battery_capacity_wh':v} for v in (0,-1,float('inf'))]
        bad += [{'loads':{'demo':{'switch':v}}} for v in (-1,8,1.2,True)]
        bad += [{'loads':[]},{'loads':{'demo':[]}}, {'switches':[0]*8}]
        bad += [{'loads':{'imaginary':{'switch':0}}}, {'loads':{'demo':{'switch':0,'power_scale':float('nan')}}}, {'loads':{'radio':{'switch':0,'mode_power_w':[100]*6}}}, {'switches':[]}]
        for override in bad:
            with self.subTest(override=override), self.assertRaises(ValueError): self.resolve(override)
        with self.assertRaises(ValueError):self.resolve({'loads':{'radio':{'switch':1}}},available=('demo',))
    def test_partial_override_retains_wiring_and_mode_power(self):
        cfg={**BASE,'wire_default_loads':True,'loads':{'demo':{'switch':7,'mode_power_w':[1.5]*6}}}
        merge_component_config('eps',cfg,{'loads':{'demo':{'power_scale':.9}}})
        result=normalize_eps(cfg,{'demo','adcs','radio'})
        self.assertEqual(result['loads']['demo']['switch'],7)
        self.assertEqual(result['loads']['demo']['mode_power_w'],[1.5]*6)
        self.assertEqual(result['loads']['demo']['power_scale'],.9)
        self.assertEqual(set(result['loads']),{'demo','adcs','radio'})

    def test_explicit_startup_and_source_config_unchanged(self):
        cfg=self.resolve();switches=cfg['switches'];switches[0]['startup_on']=False
        original=deepcopy(switches)
        resolved=self.resolve({'switches':switches})
        self.assertFalse(resolved['switches'][0]['startup_on']);self.assertEqual(switches,original)
    def test_stacks_follow_topology_and_use_standard_conditions(self):
        for rail in range(8):
            cfg=self.resolve({'loads':{n:{'switch':rail} for n in ('demo','adcs','radio')}})
            stacks=generate_stacks(cfg)
            self.assertEqual(len(stacks),3)
            for stack in stacks.values():
                for step in stack['steps']:
                    for condition in step.get('condition',[]): self.assertIn(condition['operator'],('eq','ne','gt','gte','lt','lte'))
            verifies=[s for s in stacks['EpsPowerFunctional']['steps'] if s.get('comment')==f'rail {rail} off']
            self.assertTrue(any(c['parameter']==f'/EPS/SWITCH_{rail}_STATE' and c['value']=='OFF' for c in verifies[0]['condition']))
            self.assertTrue(all(not c['parameter'].startswith(('/SIM_EPS/','/EPS_CONSOLE/'))
                                for stack in stacks.values() for step in stack['steps'] for c in step.get('condition',[])))
            self.assertTrue(any(step.get('comment')=='FSW device requests fail while rail is off' for step in stacks['EpsPowerFunctional']['steps']))
    def test_native_radio_probes_select_and_restore_existing_links(self):
        for available in (('demo','adcs','radio'), ('demo',)):
            stacks = generate_stacks(self.resolve(available=available))
            for name, stack in stacks.items():
                steps = stack['steps']
                probes = [i for i,s in enumerate(steps) if s.get('comment') == 'EPS RF probe via radio-out']
                self.assertEqual(bool(probes), name == 'EpsPowerFunctional' and 'radio' in available)
                for i in probes:
                    self.assertEqual(steps[i-1]['name'], '/SHIRE_GROUND/USE_RADIO_UPLINK')
                    self.assertEqual(steps[i]['name'], '/EPS/EPS_NOOP_CC')
                    self.assertEqual(steps[i+1]['name'], '/SHIRE_GROUND/RESTORE_UPLINK')
                self.assertEqual(steps[1]['name'], '/SHIRE_GROUND/USE_DEBUG_UPLINK')
                self.assertEqual(sum(s.get('name','').startswith('/SHIRE_GROUND/') for s in steps), 1+2*len(probes))

    def test_ground_commands_route_inside_yamcs_before_flight_stream(self):
        import xml.etree.ElementTree as ET
        config = yaml.safe_load((ROOT/'yamcs/src/main/yamcs/etc/yamcs.shire.yaml').read_text())
        streams = config['streamConfig']['tc']
        names = [stream['name'] for stream in streams]
        self.assertLess(names.index('ground_control'), names.index('tc_realtime'))
        ground = next(stream for stream in streams if stream['name'] == 'ground_control')
        self.assertEqual(ground['tcPatterns'], ['/SHIRE_GROUND/.*'])
        self.assertFalse(any(link.get('stream') == 'ground_control' for link in config['dataLinks']))
        self.assertTrue(any(mdb.get('args',{}).get('file') == 'mdb/shire_ground.xtce' for mdb in config['mdb']))
        replay = yaml.safe_load((ROOT/'yamcs/src/main/yamcs/etc/yamcs.shire-replay.yaml').read_text())
        self.assertTrue(any(mdb.get('args',{}).get('file') == 'mdb/shire_ground.xtce' for mdb in replay['mdb']))
        self.assertFalse(any(service['class'].endswith('UplinkSelectionService') for service in replay['services']))
        ns = {'x':'http://www.omg.org/spec/XTCE/20180204'}
        commands = ET.parse(ROOT/'cfg/drm/gsw/shire_ground.xtce').findall('.//x:MetaCommand',ns)
        self.assertEqual([c.get('name') for c in commands], ['USE_DEBUG_UPLINK','USE_RADIO_UPLINK','RESTORE_UPLINK'])
        for command in commands:
            self.assertEqual(command.find(".//x:AncillaryData[@name='yamcs:stream']",ns).text, 'ground_control')

    def test_generated_stacks_remove_obsolete_companions(self):
        from shire_eps_stacks import write_stacks
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)
            (path/'EpsPowerFunctional.console.json').write_text('{}')
            write_stacks(self.resolve(),path)
            self.assertEqual(sorted(p.suffix for p in path.iterdir()),['.ycs']*3)
            self.assertFalse(any('Check simulator console' in p.read_text() for p in path.iterdir()))

    def test_campaign_metrics_are_fsw_and_exist_in_generated_checks(self):
        stacks=generate_stacks(self.resolve())
        for path in (ROOT/'cfg/drm/campaigns').glob('eps-*.yaml'):
            campaign=yaml.safe_load(path.read_text())
            for metric in campaign['metrics']:
                step=next(s for s in stacks[metric['stack']]['steps'] if s.get('comment')==metric['step_name'])
                self.assertIn(metric['field'],('actual','wall_end'))
                if 'parameter' in metric:
                    self.assertTrue(any(c['parameter']==metric['parameter'] for c in step['condition']))
                    self.assertFalse(metric['parameter'].startswith('/EPS_CONSOLE/'))

    def test_campaign_recovery_duration_uses_observed_phase_timestamps(self):
        import importlib.util,json
        spec=importlib.util.spec_from_file_location('campaign_timing',ROOT/'tools/shire-campaign.py')
        runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)
        metric={'name':'recovery_time_s','stack':'Example','step_name':'startup restored','field':'wall_end',
                'elapsed_since':{'step_name':'RECOVERY_BEGIN','field':'wall_start'}}
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'verify-Example.json'
            steps=[{'name':'RECOVERY_BEGIN','wall_start':'2026-10-07T10:00:00+00:00'},
                   {'name':'RECOVERY_BEGIN','wall_start':'2026-10-07T10:01:00+00:00'},
                   {'name':'startup restored','wall_end':'2026-10-07T10:01:35+00:00'}]
            path.write_text(json.dumps({'steps':steps}))
            self.assertEqual(runner.extract_metrics(Path(directory),[metric])['recovery_time_s'],35)
            steps.pop(1);steps.pop(0);path.write_text(json.dumps({'steps':steps}))
            self.assertIsNone(runner.extract_metrics(Path(directory),[metric])['recovery_time_s'])

    def test_topology_selector_and_flight_only_display(self):
        import xml.etree.ElementTree as ET
        ns={'x':'http://www.omg.org/spec/XTCE/20180204'}
        cfg=self.resolve(available=('demo','radio'))
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'eps.xtce'
            write_xtce(cfg,ROOT/'comp/eps/gsw/eps.xtce',path)
            tree=ET.parse(path)
            values=tree.findall(".//x:EnumeratedArgumentType[@name='BD_COMPONENT']/x:EnumerationList/x:Enumeration",ns)
            self.assertEqual([(v.get('label'),v.get('value')) for v in values],[('DEMO','0'),('RADIO','2')])
            description=tree.find(".//x:Parameter[@name='SWITCH_0_STATE']",ns).get('shortDescription')
            self.assertIn('DEMO',description);self.assertIn('3.3 V',description)
        import json
        display=json.loads((ROOT/'comp/eps/gsw/displays/Eps.par').read_text())['parameters']
        self.assertTrue(all(name.startswith('/EPS/') for name in display))
        fields=ET.parse(ROOT/'comp/eps/gsw/eps.xtce').findall('./x:TelemetryMetaData/x:ParameterSet/x:Parameter',ns)
        self.assertTrue(all('/EPS/'+p.get('name') in display for p in fields))
    def test_backdoor_stack_handles_low_scale_zero_solar_and_startup_off(self):
        cfg=self.resolve({'max_solar_power_w':0,'wire_default_loads':False,
                          'loads':{'demo':{'switch':0,'mode_power_w':[10000]*6,'power_scale':.001}}},available=('demo',))
        cfg['switches'][0]['startup_on']=False
        stack=generate_stacks(cfg)['EpsBackdoorFunctional']['steps']
        commands=[s for s in stack if s['type']=='command']
        self.assertTrue(any(s['name']=='/EPS/EPS_NOOP_CC' for s in commands))
        scale=next(s for s in commands if s['name']=='/EPS/BACKDOOR_EPS_SET_LOAD_SCALE')
        self.assertIn({'name':'SCALE_PPM','value':'500'},scale['arguments'])
        solar=next(s for s in commands if s['name']=='/EPS/BACKDOOR_EPS_SET_SOLAR')
        self.assertIn({'name':'POWER_MW','value':'0'},solar['arguments'])
        self.assertTrue(any(s['name']=='/DEMO/DEMO_DISABLE_CC' for s in commands))
        reset_index=next(i for i,s in enumerate(stack) if s.get('name')=='/EPS/BACKDOOR_EPS_RESET')
        self.assertFalse(any(s.get('name')=='/DEMO/DEMO_REQ_HK' for s in stack[reset_index+1:]))
    def test_backdoor_scale_and_failed_switches_have_fsw_readback(self):
        cfg=self.resolve({'loads':{name:{'switch':6} for name in ('demo','adcs','radio')}})
        stacks=generate_stacks(cfg)
        checks=[step for step in stacks['EpsBackdoorFunctional']['steps'] if step.get('comment')=='applied SET_LOAD_SCALE']
        # DEMO 0.4 + ADCS 0.5 + radio 4.5 W on 24 V after only DEMO is scaled.
        expected=5.4/24
        current=[c for c in checks[0]['condition'] if c['parameter']=='/EPS/SWITCH_6_CURRENT']
        self.assertEqual(len(current),2)
        self.assertLess(float(current[0]['value']),expected)
        self.assertGreater(float(current[1]['value']),expected)
        for step in stacks['EpsFaultInjection']['steps']:
            if step.get('comment') in ('failed request preserves state','failed off request preserves state'):
                self.assertTrue(any(c['parameter']=='/EPS/SWITCH_7_STATE' for c in step['condition']))

    def test_fault_stack_starts_standalone_and_keeps_debug_control(self):
        stack=generate_stacks(self.resolve())['EpsFaultInjection']
        commands=[s['name'] for s in stack['steps'] if s['type']=='command']
        self.assertEqual(commands[:2],['/SHIRE_GROUND/USE_DEBUG_UPLINK','/EPS/BACKDOOR_EPS_CLEAR_FAULTS'])
        self.assertIn('/DEMO/DEMO_ENABLE_CC',commands)
        self.assertIn('/ADCS/ADCS_ENABLE_CC',commands)
        preflight=next(s for s in stack['steps'] if s.get('comment','').startswith('debug path required'))
        self.assertEqual(preflight['timeout'],5000)
        self.assertEqual(stack['advancement']['wait'],1000)
        self.assertFalse(any(s.get('comment')=='EPS RF probe via radio-out' for s in stack['steps']))
        self.assertIn('enable debug-in',stack['steps'][0]['text'])

    def test_backdoor_stack_uses_debug_and_nominal_stack_retains_rf_checks(self):
        for rail in range(8):
            cfg=self.resolve({'loads':{'radio':{'switch':rail}}})
            stacks=generate_stacks(cfg)
            for name in ('EpsBackdoorFunctional','EpsFaultInjection'):
                self.assertFalse(any(s.get('comment')=='EPS RF probe via radio-out' for s in stacks[name]['steps']))
                self.assertFalse(any('select existing radio-out' in s.get('text','') for s in stacks[name]['steps']))
                self.assertFalse(any(c['parameter']=='/RADIO/RADIO_DEVICE_BytesReceived'
                                     for s in stacks[name]['steps'] for c in s.get('condition',[])))
            backdoor=stacks['EpsBackdoorFunctional']['steps']
            self.assertTrue(any(s.get('comment')=='EPS flight command observed' for s in backdoor))
            self.assertTrue(any(s.get('name')=='/RADIO/RADIO_CONFIG_CC' for s in backdoor))
            self.assertTrue(any(s.get('comment')=='EPS RF probe via radio-out' for s in stacks['EpsPowerFunctional']['steps']))
            self.assertTrue(any(s.get('comment')=='radio uplink and downlink observed'
                                for s in stacks['EpsPowerFunctional']['steps']))

    def test_yamcs_uses_existing_links_and_no_eps_simulator_mdb(self):
        config=yaml.safe_load((ROOT/'yamcs/src/main/yamcs/etc/yamcs.shire.yaml').read_text())
        links={link['name']:link for link in config['dataLinks']}
        self.assertNotIn('eps-radio-probe-out',links);self.assertNotIn('eps-sim-in',links)
        self.assertEqual(links['sim-backdoor']['stream'],'tc_backdoor')
        streams=config['streamConfig']['tc']
        self.assertIn('/EPS/BACKDOOR_EPS_.*',streams[0]['tcPatterns'])
        self.assertEqual(streams[-1]['name'],'tc_realtime')
        self.assertFalse(any('eps_sim' in str(entry) for entry in config['mdb']+config['streamConfig']['tm']))

    def test_console_decode_validates_time_shape_and_values(self):
        import json
        from shire_eps_console import decode_state, EpsConsole
        values=[0.]*110;values[1]=1.25;values[12]=9
        record={'time_ns':1250000000,'values':values}
        log='other log\nEPS_SIM_STATE '+json.dumps(record)+'\n'
        _,decoded=decode_state(log)
        self.assertEqual(decoded['TIME'],1.25)
        console=EpsConsole('test',read_log=lambda:log)
        self.assertEqual(console.parameter('/EPS_CONSOLE/BACKDOOR_GENERATION')['value'],9)
        self.assertEqual(console.parameter('/EPS_CONSOLE/TIME')['raw']['source'],'director console')
        for change in ({'time_ns':4},{'values':[0.]},{'values':[float('nan')]*110}):
            with self.assertRaises(ValueError):decode_state('EPS_SIM_STATE '+json.dumps({**record,**change}))
        with self.assertRaises(RuntimeError):decode_state('no marker')
        with self.assertRaises(ValueError):console.parameter('/EPS/DEVICE_COUNT')

    def test_terminal_partial_log_and_bounded_event_windows(self):
        from shire_runner_lib import parse_marker
        self.assertEqual(parse_marker('timestamp TERMINAL {"x":12026-10-06T22:00:00.123456789Z 23}','TERMINAL'),{'x':123})
        self.assertFalse(event_gate({'fsw':'2026-10-06T22:00:00Z EVS Port1 /EPS_APP 41: error'}, {}, ROOT)['passed'])
        phases={'stacks':[{'steps':[{'name':'FAULT_BEGIN','wall_start':'2026-10-06T21:59:00+00:00'}, {'name':'FAULT_END','wall_end':'2026-10-06T22:01:00+00:00'}]}]}
        self.assertTrue(event_gate({'fsw':'2026-10-06T22:00:00Z EVS Port1 /EPS_APP 41: error'}, phases, ROOT)['passed'])

    def test_report_requires_complete_replays_of_the_archived_campaign(self):
        import json
        from shire_eps_report import replay_check
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'replay-report.json';campaign=Path(d)/'campaign_report.json'
            report={'passed':True,'campaign':str(campaign),
                    'trials':[{'trial_index':i,'passed':True,'issues':[]} for i in (0,50,99)]}
            path.write_text(json.dumps(report));self.assertTrue(replay_check(path,campaign,[0,50,99])['passed'])
            report['trials'].pop();path.write_text(json.dumps(report))
            self.assertFalse(replay_check(path,campaign,[0,50,99])['passed'])
            report['trials'].append({'trial_index':99,'passed':False,'issues':['demand changed']})
            path.write_text(json.dumps(report));self.assertFalse(replay_check(path,campaign,[0,50,99])['passed'])
            self.assertFalse(replay_check(path,Path(d)/'other.json',[0,50,99])['passed'])

    def test_replay_requires_same_wiring_fsw_readings_and_command_order(self):
        from shire_eps_replay import compare
        cfg={'eps':self.resolve()}
        result={'pass':True,'verification':{'stacks':[{'steps':[{'type':'command','name':'/EPS/BACKDOOR_EPS_RESET'}]}]}}
        metrics={'switch_0_current_a':.8,'demo_device_count':20,'battery_voltage_v':1}
        self.assertTrue(compare(result,result,metrics,{**metrics,'demo_device_count':30,'battery_voltage_v':2},cfg,cfg)['passed'])
        self.assertFalse(compare(result,result,metrics,{**metrics,'switch_0_current_a':.9},cfg,cfg)['passed'])
        changed=deepcopy(cfg);changed['eps']['loads']['demo']['switch']=1
        self.assertFalse(compare(result,result,metrics,metrics,cfg,changed)['passed'])
        self.assertFalse(compare(result,{'pass':True,'verification':{'stacks':[]}},metrics,metrics,cfg,cfg)['passed'])

    def test_replay_clears_trial_overrides_after_success_and_exceptions(self):
        import importlib.util
        from shire_eps_replay import clear_trial_settings_on_exit
        spec=importlib.util.spec_from_file_location('campaign_cleanup',ROOT/'tools/shire-campaign.py')
        runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)
        with tempfile.TemporaryDirectory() as d:
            runner.ACTIVE_PATH=Path(d)/'active.yaml';runner.ACTIVE_LOCK_PATH=Path(d)/'active.lock'
            base={'mission':'drm','spacecraft':'sat-1','scenario':'eps-functional','user_setting':'keep'}
            trial={**base,'instance':'1234','port_offset':100,'image_tag':'trial','initial_conditions_file':'trial.yaml'}
            for interrupted in (False,True):
                runner.ACTIVE_PATH.write_text(yaml.safe_dump(trial))
                try:
                    with clear_trial_settings_on_exit(runner):
                        self.assertEqual(yaml.safe_load(runner.ACTIVE_PATH.read_text()),trial)
                        if interrupted:raise RuntimeError('replay failed')
                except RuntimeError:
                    self.assertTrue(interrupted)
                self.assertEqual(yaml.safe_load(runner.ACTIVE_PATH.read_text()),base)

if __name__=='__main__':unittest.main()
