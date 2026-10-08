"""Generate standard Yamcs stacks from resolved, file-based EPS wiring."""
import json
from pathlib import Path

FSW_COUNTERS = {
    'DEMO': ('/DEMO/DEVICE_COUNT', '/DEMO/DEVICE_ERR_COUNT'),
    'ADCS': ('/ADCS/DEVICE_COUNT', '/ADCS/DEVICE_ERR_COUNT'),
    'RADIO': ('/RADIO/RADIO_HK_DeviceCount', '/RADIO/RADIO_HK_DeviceErrorCount'),
}


def generate_stacks(cfg):
    loads = cfg['loads']
    all_stacks = {}

    class Builder:
        def __init__(self, radio_probes=True):
            self.steps = []
            self.scales = {name: load['power_scale'] for name, load in loads.items()}
            self.modes = {name: 3 if name == 'radio' else 0 for name in loads}
            self.radio_probes = radio_probes
            setup = 'Before running in Yamcs Links: enable debug-in and sim-backdoor. Keep the simulation playing. This stack selects debug-out and disables radio-out automatically for normal EPS and device commands.'
            if radio_probes and 'radio' in loads:
                setup += ' This stack automatically selects radio-out for each marked RF probe and restores the prior command-link states before requesting housekeeping.'
            else:
                setup += ' Keep radio-out disabled throughout this stack.'
            self.text(setup)
            self.command('/SHIRE_GROUND/USE_DEBUG_UPLINK')
        def text(self, text):
            self.steps.append({'type': 'text', 'text': text, 'comment': text})
        def command(self, name, **args):
            self.steps.append({'type': 'command', 'name': name, 'arguments':
                               [{'name': k, 'value': str(v)} for k, v in args.items()], 'extraOptions': []})
        def verify(self, name, conditions, timeout=30000):
            flight = []
            for parameter, op, value in conditions:
                if not parameter.startswith('/'):
                    if parameter.startswith('SWITCH_') and parameter.endswith('_EFFECTIVE'):
                        parameter = '/EPS/'+parameter.replace('_EFFECTIVE', '_STATE')
                        value = 'ON' if value else 'OFF'
                    elif parameter == 'ADCS_MODE': parameter = '/ADCS/MODE'
                    elif parameter == 'RADIO_MODE': parameter = '/RADIO/RADIO_DEVICE_Mode'
                    elif parameter.endswith('_SUCCESSFUL_REQUESTS') and name == 'fresh device transactions':
                        parameter = FSW_COUNTERS[parameter.removesuffix('_SUCCESSFUL_REQUESTS')][0]
                    else: continue  # Internal diagnostics are not FSW acceptance data.
                flight.append({'parameter': parameter, 'operator': op, 'value': str(value)})
            # Request the real application HK packet rather than waiting for
            # another scheduled publication. Simulator diagnostics stay separate.
            namespaces = sorted({c['parameter'].split('/')[1] for c in flight})
            for namespace in namespaces:
                if namespace in ('EPS','DEMO','ADCS','RADIO'):
                    self.command(f'/{namespace}/{namespace}_REQ_HK')
            if flight:
                self.steps.append({'type': 'verify', 'comment': name, 'timeout': timeout, 'condition': flight})
        def debug_preflight(self):
            if 'radio' not in loads: return
            rail = loads['radio']['switch']
            self.text('Debug-path preflight briefly cuts radio supply. If the next check fails, enable debug-in/debug-out and disable radio-out before restarting this stack.')
            self.command('/EPS/BACKDOOR_EPS_SET_SWITCH',SWITCH=rail,STATE=0)
            self.verify('debug path required: fresh EPS housekeeping while radio is off',
                        [(f'SWITCH_{rail}_EFFECTIVE','eq',0),('BACKDOOR_LAST_ID','eq',3),
                         ('BACKDOOR_LAST_STATUS','eq',1),('BACKDOOR_LAST_STATUS','eq',1)],timeout=5000)
            self.command('/EPS/EPS_RST_COUNTERS_CC')
            self.verify('debug flight command baseline',[('/EPS/CMD_COUNT','eq',0)],timeout=5000)
            self.command('/EPS/EPS_NOOP_CC')
            self.verify('debug flight command received while radio is off',[('/EPS/CMD_COUNT','eq',1)],timeout=5000)
            self.switch(rail,True)
        def radio_probe(self):
            self.text('RF probe: Yamcs ground commands select radio-out exclusively for the following EPS flight NOOP, then restore the previous uplink states. Keep the simulation playing; no manual link changes are needed.')
            self.command('/SHIRE_GROUND/USE_RADIO_UPLINK')
            self.command('/EPS/EPS_NOOP_CC')
            self.steps[-1]['comment'] = 'EPS RF probe via radio-out'
            self.command('/SHIRE_GROUND/RESTORE_UPLINK')
        def rail_current(self, name):
            rail = loads[name]['switch']
            watts = sum(load['mode_power_w'][self.modes[peer]] * self.scales[peer]
                        for peer, load in loads.items() if load['switch'] == rail)
            current = watts / cfg['switches'][rail]['voltage_v']
            return [(f'/EPS/SWITCH_{rail}_CURRENT','gte',max(0,current-10/255-1e-7)),
                    (f'/EPS/SWITCH_{rail}_CURRENT','lte',current+10/255+1e-7)]
        def value(self, name, value):
            return [(name, 'gte', value-1e-8), (name, 'lte', value+1e-8)]
        def backdoor(self, suffix, command_id, conditions=(), **args):
            conditions = list(conditions)
            if suffix == 'SET_SOC':
                voltage = cfg['battery_voltage_min'] + args['SOC_PPM']/1e6 * (cfg['battery_voltage_max']-cfg['battery_voltage_min'])
                conditions += [('/EPS/BATTERY_VOLTAGE','gte',max(0,voltage-32/255-.02)),
                               ('/EPS/BATTERY_VOLTAGE','lte',voltage+32/255+.02)]
            elif suffix == 'SET_SOLAR' and args['MODE'] == 1:
                voltage = 4.5 if args['POWER_MW'] else 0
                conditions += [('/EPS/SOLAR_VOLTAGE','gte',max(0,voltage-32/255)),
                               ('/EPS/SOLAR_VOLTAGE','lte',voltage+32/255)]
            elif suffix == 'RESET':
                conditions += [(f'/EPS/SWITCH_{i}_STATE','eq','ON' if s['startup_on'] else 'OFF') for i,s in enumerate(cfg['switches'])]
            self.command('/EPS/BACKDOOR_EPS_'+suffix, **args)
            self.verify('applied '+suffix, conditions)
        def switch(self, index, on):
            self.command('/EPS/EPS_SWITCH_'+('ON' if on else 'OFF')+'_CC', SWITCH_NUMBER=index)
            cond = [(f'SWITCH_{index}_REQUESTED','eq',int(on)), (f'SWITCH_{index}_EFFECTIVE','eq',int(on))]
            for name, load in loads.items():
                if load['switch'] == index:
                    cond += [(name.upper()+'_READY','eq',int(on))]
                    if not on: cond += [(name.upper()+'_POWER_W','eq',0)]
            self.verify(f'rail {index} '+('on' if on else 'off'),cond)
            mapped = [name for name, load in loads.items() if load['switch'] == index]
            if not on and mapped:
                for name in mapped: self.command(f'/{name.upper()}/{name.upper()}_REQ_HK')
                self.verify('FSW device requests fail while rail is off',
                            [(FSW_COUNTERS[name.upper()][1], 'gt', 0) for name in mapped])
            if self.radio_probes and not on and 'radio' in loads and loads['radio']['switch'] == index:
                self.command('/EPS/EPS_RST_COUNTERS_CC')
                self.verify('radio outage EPS baseline', [('/EPS/CMD_COUNT','eq',0)])
                self.radio_probe()
                self.verify('radio outage rejects RF uplink', [('/EPS/CMD_COUNT','eq',0),('RADIO_READY','eq',0)])
        def prepare_devices(self):
            for i in sorted({l['switch'] for l in loads.values()}):
                self.command('/EPS/EPS_SWITCH_ON_CC',SWITCH_NUMBER=i)
            for name in loads:
                if name == 'radio':
                    self.verify('DRM radio startup enabled', [('/RADIO/RADIO_HK_DeviceEnabled','eq',1)])
                else:
                    self.command(f'/{name.upper()}/{name.upper()}_REQ_HK')
                    self.command(f'/{name.upper()}/{name.upper()}_ENABLE_CC')
                    self.steps[-1]['comment'] = f'Enable if disabled: /{name.upper()}/DEVICE_ENABLED'
            self.verify('initial devices ready',[(n.upper()+'_READY','eq',1) for n in loads])
            self.probe()
        def prepare(self):
            self.text('OUTAGE_BEGIN')
            self.prepare_devices()
            if 'radio' in loads:
                self.command('/RADIO/RADIO_CONFIG_CC', MODE='Duplex Mode')
            if self.radio_probes and 'radio' in loads:
                self.command('/EPS/EPS_RST_COUNTERS_CC')
                self.verify('EPS counters for RF probe', [('/EPS/CMD_COUNT','eq',0)])
                self.radio_probe()
                self.verify('radio uplink and downlink observed', [('/EPS/CMD_COUNT','eq',1),
                            ('RADIO_RF_RECEIVED','gt',0),('RADIO_RF_SENT','gt',0),
                            ('/RADIO/RADIO_DEVICE_BytesReceived','gt',0),('/RADIO/RADIO_DEVICE_BytesSent','gt',0)])
            else:
                self.command('/EPS/EPS_RST_COUNTERS_CC')
                self.verify('EPS command baseline', [('/EPS/CMD_COUNT','eq',0)])
                self.command('/EPS/EPS_NOOP_CC')
                self.verify('EPS flight command observed', [('/EPS/CMD_COUNT','eq',1)])
            self.text('OUTAGE_END')
        def probe(self, names=None):
            names=list(loads) if names is None else names
            for name in names:
                self.command(f'/{name.upper()}/{name.upper()}_REQ_HK')
            self.verify('fresh device transactions',[(n.upper()+'_SUCCESSFUL_REQUESTS','gt',0) for n in names])
        def electrical(self, on, startup=False):
            self.command('/EPS/EPS_REQ_HK')
            conditions=[]
            for i,switch in enumerate(cfg['switches']):
                enabled = switch['startup_on'] if startup else on
                voltage=switch['voltage_v'] if enabled else 0
                watts=sum(l['mode_power_w'][3 if name=='radio' else 0]*l['power_scale']
                          for name,l in loads.items() if l['switch']==i) if enabled else 0
                for suffix,expected,count in (('VOLTAGE',voltage,32/255),('CURRENT',watts/switch['voltage_v'],10/255)):
                    parameter=f'/EPS/SWITCH_{i}_{suffix}'
                    conditions.extend([(parameter,'gte',max(0,expected-count-1e-7)),
                                       (parameter,'lte',expected+count+1e-7)])
            self.verify('startup electrical readings' if startup else 'encoded rail readings '+('on' if on else 'off'),conditions)
        def recovery(self):
            self.text('RECOVERY_BEGIN')
            for name,load in loads.items():
                if not cfg['switches'][load['switch']]['startup_on']:
                    self.command(f'/{name.upper()}/{name.upper()}_DISABLE_CC')
            # Power cycles restore private state. FSW retains its own counters and enabled flags.
            for i in range(8): self.switch(i, False)
            for i, switch in enumerate(cfg['switches']):
                if switch['startup_on']: self.switch(i, True)
            self.probe([n for n,l in loads.items() if cfg['switches'][l['switch']]['startup_on']])
            self.verify('startup restored', [(f'SWITCH_{i}_EFFECTIVE','eq',int(s['startup_on'])) for i,s in enumerate(cfg['switches'])] + [('ADCS_MODE','eq',0)] if 'adcs' in loads else [(f'SWITCH_{i}_EFFECTIVE','eq',int(s['startup_on'])) for i,s in enumerate(cfg['switches'])])
            self.electrical(True, startup=True)
            self.text('RECOVERY_END')
        def result(self, stack_name):
            fields = [('/EPS/BATTERY_VOLTAGE','gte',0), ('/EPS/SOLAR_VOLTAGE','gte',0),
                      ('/EPS/DEVICE_COUNT','gte',0), ('/EPS/DEVICE_ERR_COUNT','gte',0)]
            fields += [(parameter,'gte',0) for name,load in loads.items()
                       if cfg['switches'][load['switch']]['startup_on']
                       for parameter in FSW_COUNTERS[name.upper()]]
            fields += [(f'/EPS/SWITCH_{i}_{suffix}','gte',0) for i in range(8) for suffix in ('VOLTAGE','CURRENT')]
            self.verify('acceptance metrics', fields)
            return {'$schema': 'https://yamcs.org/schema/stack.schema.json','steps': self.steps,
                    'advancement': {'acknowledgment': 'Acknowledge_Sent', 'wait': 1000}}

    b=Builder()
    b.text('EPS functional: resolved wiring; switch effects use flight commands')
    b.prepare()
    b.text('OUTAGE_BEGIN')
    for i in range(8):
        b.switch(i,False); b.switch(i,True); b.switch(i,True)
        b.probe()
    for i in range(8): b.switch(i,False)
    b.verify('all rails off', [('LOAD_POWER_W','eq',0)])
    b.electrical(False)
    for i in range(8): b.switch(i,True)
    b.probe()
    b.electrical(True)
    b.text('OUTAGE_END')
    if 'adcs' in loads:
        l=loads['adcs']
        for mode,label in enumerate(['PASSIVE','BDOT','SUNSAFE','NADIR','TARGET','INERTIAL']):
            b.command('/ADCS/ADCS_SET_MODE_CC',MODE=label)
            b.modes['adcs'] = mode
            b.verify('ADCS demand '+label,[('ADCS_MODE','eq',mode)]+b.value('ADCS_POWER_W',l['mode_power_w'][mode]*l['power_scale'])+b.rail_current('adcs'))
    if 'radio' in loads:
        l=loads['radio']
        for mode,label in enumerate(['Sleep Mode','Transmit Only Mode','Receive Only Mode','Duplex Mode']):
            b.command('/RADIO/RADIO_CONFIG_CC',MODE=label)
            b.modes['radio'] = mode
            b.verify('radio demand '+label,[('RADIO_MODE','eq',mode)]+b.value('RADIO_POWER_W',l['mode_power_w'][mode]*l['power_scale'])+b.rail_current('radio'))
    b.recovery()
    all_stacks['EpsPowerFunctional']=b.result('EpsPowerFunctional')

    b=Builder(radio_probes=False);b.text('EPS backdoors: confirm effects in fresh FSW telemetry; no retries'); b.prepare()
    b.backdoor('SET_SOC',1,[('SOC','gte',0.49),('SOC','lte',0.51),('ENERGY_WH','gte',cfg['battery_capacity_wh']*.5-.05),('ENERGY_WH','lte',cfg['battery_capacity_wh']*.5+.05)],SOC_PPM=500000)
    solar_mw=int(min(1.0,cfg['max_solar_power_w'])*1000)
    b.backdoor('SET_SOLAR',2,[('SOLAR_OVERRIDE','eq',1),('SOLAR_POWER_W','eq',solar_mw/1000)],MODE=1,POWER_MW=solar_mw)
    b.backdoor('SET_SOLAR',2,[('SOLAR_OVERRIDE','eq',1),('SOLAR_POWER_W','eq',0)],MODE=1,POWER_MW=0)
    b.backdoor('SET_SOLAR',2,[('SOLAR_OVERRIDE','eq',0)],MODE=0,POWER_MW=0)
    b.text('OUTAGE_BEGIN')
    for i in range(8):
        mapped=[n.upper() for n,l in loads.items() if l['switch']==i]
        b.backdoor('SET_SWITCH',3,[(f'SWITCH_{i}_EFFECTIVE','eq',0)]+[(n+'_READY','eq',0) for n in mapped]+[(n+'_POWER_W','eq',0) for n in mapped],SWITCH=i,STATE=0)
        b.backdoor('SET_SWITCH',3,[(f'SWITCH_{i}_EFFECTIVE','eq',1)]+[(n+'_READY','eq',1) for n in mapped],SWITCH=i,STATE=1)
    b.text('OUTAGE_END')
    for idx,name in enumerate(['demo','adcs','radio']):
        if name in loads:
            scale_ppm=int(loads[name]['power_scale']*.5*1e6)
            scale=scale_ppm/1e6
            b.scales[name] = scale
            b.backdoor('SET_LOAD_SCALE',4,[(name.upper()+'_SCALE','eq',scale),(name.upper()+'_READY','eq',1)]+b.value(name.upper()+'_POWER_W',loads[name]['mode_power_w'][3 if name=='radio' else 0]*scale)+b.rail_current(name),COMPONENT=name.upper(),SCALE_PPM=scale_ppm)
    b.text('RECOVERY_BEGIN')
    for name,load in loads.items():
        if not cfg['switches'][load['switch']]['startup_on']:
            b.command(f'/{name.upper()}/{name.upper()}_DISABLE_CC')
    b.backdoor('RESET',9,[('/EPS/CMD_COUNT','eq',1),('SOLAR_OVERRIDE','eq',0),('CRC_REMAINING','eq',0),('FAIL_REMAINING','eq',0)]+[(n.upper()+'_SCALE','eq',l['power_scale']) for n,l in loads.items()])
    b.verify('reset boot completed',[(n.upper()+'_READY','eq',int(cfg['switches'][l['switch']]['startup_on'])) for n,l in loads.items()])
    b.probe([n for n,l in loads.items() if cfg['switches'][l['switch']]['startup_on']]);b.electrical(True,startup=True);b.text('RECOVERY_END')
    all_stacks['EpsBackdoorFunctional']=b.result('EpsBackdoorFunctional')
    b=Builder(radio_probes=False);b.text('FAULT_BEGIN')
    # Standalone entry clears previous injections and initializes FSW devices.
    b.backdoor('CLEAR_FAULTS',8,[('FAIL_REMAINING','eq',0),('CRC_REMAINING','eq',0)])
    b.debug_preflight()
    b.prepare_devices()
    # Stuck switches must retain requested state while flight commands still succeed.
    for i in sorted({l['switch'] for l in loads.values()}):
        b.backdoor('SET_SWITCH_FAULT',5,[(f'SWITCH_{i}_FAULT','eq',1),(f'SWITCH_{i}_EFFECTIVE','eq',0)],SWITCH=i,FAULT=1)
        b.command('/EPS/EPS_SWITCH_ON_CC',SWITCH_NUMBER=i)
        b.verify('stuck off commanded on',[(f'SWITCH_{i}_REQUESTED','eq',1),(f'SWITCH_{i}_EFFECTIVE','eq',0)])
        b.backdoor('SET_SWITCH_FAULT',5,[(f'SWITCH_{i}_FAULT','eq',2),(f'SWITCH_{i}_EFFECTIVE','eq',1)],SWITCH=i,FAULT=2)
        b.command('/EPS/EPS_SWITCH_OFF_CC',SWITCH_NUMBER=i)
        b.verify('stuck on commanded off',[(f'SWITCH_{i}_REQUESTED','eq',0),(f'SWITCH_{i}_EFFECTIVE','eq',1)])
        b.backdoor('CLEAR_FAULTS',8,[(f'SWITCH_{i}_FAULT','eq',0),(f'SWITCH_{i}_EFFECTIVE','eq',0)])
        b.switch(i,True)
    # Keep CRC injection alive long enough to observe it and the flight read rejection.
    b.command('/EPS/EPS_RST_COUNTERS_CC')
    b.backdoor('CORRUPT_HK_CRC',6,[('CRC_REMAINING','gt',0)],COUNT=20)
    b.command('/EPS/EPS_REQ_HK')
    b.verify('CRC rejection observed',[('/EPS/DEVICE_ERR_COUNT','gt',0)])
    b.backdoor('CORRUPT_HK_CRC',6,[('CRC_REMAINING','eq',0)],COUNT=0)
    b.switch(7,False)
    b.backdoor('FAIL_REQUESTS',7,[('FAIL_REMAINING','eq',1)],SELECTOR=2,COUNT=1)
    b.command('/EPS/EPS_SWITCH_ON_CC',SWITCH_NUMBER=7)
    b.verify('failed request preserves state',[('FAIL_REMAINING','eq',0),('SWITCH_7_REQUESTED','eq',0),('/EPS/SWITCH_7_STATE','eq','OFF')])
    b.backdoor('FAIL_REQUESTS',7,[('FAIL_REMAINING','eq',0)],SELECTOR=0,COUNT=0)
    b.switch(7,True)
    b.backdoor('FAIL_REQUESTS',7,[('FAIL_REMAINING','eq',1)],SELECTOR=3,COUNT=1)
    b.command('/EPS/EPS_SWITCH_OFF_CC',SWITCH_NUMBER=7)
    b.verify('failed off request preserves state',[('FAIL_REMAINING','eq',0),('SWITCH_7_REQUESTED','eq',1),('/EPS/SWITCH_7_STATE','eq','ON')])
    for selector,label in ((0,'any'),(1,'housekeeping')):
        # Periodic HK may consume the injection before the next telemetry packet.
        # A long finite count makes application observable without retrying.
        b.backdoor('FAIL_REQUESTS',7,[('FAIL_REMAINING','gt',0),('FAIL_SELECTOR','eq',selector)],SELECTOR=selector,COUNT=20)
        b.command('/EPS/EPS_REQ_HK')
        b.verify('failed '+label+' request observed',[('/EPS/DEVICE_ERR_COUNT','gt',0),('EPS_REQUESTS_REJECTED','gt',0)])
        b.backdoor('FAIL_REQUESTS',7,[('FAIL_REMAINING','eq',0)],SELECTOR=selector,COUNT=0)
    b.backdoor('CLEAR_FAULTS',8,[('FAIL_REMAINING','eq',0),('CRC_REMAINING','eq',0)])
    b.backdoor('RESET',9,[('SOLAR_OVERRIDE','eq',0)])
    b.text('FAULT_END'); b.recovery()
    all_stacks['EpsFaultInjection']=b.result('EpsFaultInjection')
    return all_stacks


def write_stacks(cfg, directory):
    directory=Path(directory);directory.mkdir(parents=True,exist_ok=True)
    for name,stack in generate_stacks(cfg).items():
        (directory/(name+'.ycs')).write_text(json.dumps(stack,indent=2)+'\n')
        # Remove obsolete companions only in the generated stack directory.
        (directory/(name+'.console.json')).unlink(missing_ok=True)


def write_xtce(cfg, source, destination):
    import xml.etree.ElementTree as ET
    tree = ET.parse(source)
    root = tree.getroot()
    ns = root.tag.split('}')[0][1:]
    ET.register_namespace('xtce', ns)
    tag = lambda name: '{'+ns+'}'+name
    values = root.find('.//'+tag('EnumeratedArgumentType')+"[@name='BD_COMPONENT']/"+tag('EnumerationList'))
    if values is not None:
        values.clear()
        for i,name in enumerate(('demo','adcs','radio')):
            if name in cfg['loads']:
                ET.SubElement(values,tag('Enumeration'),value=str(i),label=name.upper())
        # Empty topologies retain an unusable selector, rejected by the simulator.
        if not len(values): ET.SubElement(values,tag('Enumeration'),value='255',label='UNMAPPED')
    parameters=root.find('./'+tag('TelemetryMetaData')+'/'+tag('ParameterSet'))
    if parameters is not None:
        for i,switch in enumerate(cfg['switches']):
            consumers=', '.join(name.upper() for name,load in cfg['loads'].items() if load['switch']==i) or 'unloaded'
            description=f"{switch['label']}: {consumers}, {switch['voltage_v']} V, startup {'ON' if switch['startup_on'] else 'OFF'}"
            for suffix in ('STATE','VOLTAGE','CURRENT'):
                parameter=parameters.find(tag('Parameter')+f"[@name='SWITCH_{i}_{suffix}']")
                if parameter is not None:parameter.set('shortDescription',description)
    ET.indent(tree, space='  ')
    tree.write(destination, encoding='UTF-8', xml_declaration=True)
