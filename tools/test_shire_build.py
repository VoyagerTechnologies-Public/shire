"""Build topology regressions without invoking Docker or changing active settings."""
import contextlib
import importlib.util
import io
from pathlib import Path
import unittest
from unittest.mock import Mock, call, patch

spec = importlib.util.spec_from_file_location(
    'shire_build', Path(__file__).with_name('shire-build.py'))
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class CliTopologyTests(unittest.TestCase):
    def run_build(self, config):
        operations = Mock()
        with contextlib.ExitStack() as stack:
            stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
            stack.enter_context(patch.object(build, 'get_build_dirs', return_value={}))
            for name in ('build_42', 'build_simulith', 'build_component_sim',
                         'build_simulith_director_and_server', 'build_component_cli'):
                operations.attach_mock(stack.enter_context(patch.object(build, name)), name)
            build.build_cli(config)
        return operations.mock_calls

    def test_eps_cli_builds_consumers_before_packaging_director(self):
        config = {'global': {'build': {'cli': [{'name': 'eps'}]}},
                  'mission_cfg': {'components': [
                      {'name': n} for n in ('adcs', 'demo', 'eps', 'radio')]}}
        self.assertEqual(self.run_build(config), [
            call.build_42(config, {}), call.build_simulith(config, {}),
            call.build_component_sim('adcs', {}), call.build_component_sim('demo', {}),
            call.build_component_sim('eps', {}), call.build_component_sim('radio', {}),
            call.build_simulith_director_and_server(config, {}),
            call.build_component_cli('eps', {})])

    def test_spacecraft_topology_wins_and_explicit_cli_is_included_once(self):
        config = {'global': {'build': {'cli': [{'name': 'eps'}, {'name': 'demo'}]}},
                  'mission_cfg': {'components': [{'name': 'radio'}]},
                  'spacecraft_cfg': {'components': [{'name': 'demo'}]}}
        calls = self.run_build(config)
        self.assertEqual([c for c in calls if c[0] == 'build_component_sim'], [
            call.build_component_sim('demo', {}), call.build_component_sim('eps', {})])
        self.assertEqual([c for c in calls if c[0] == 'build_component_cli'], [
            call.build_component_cli('eps', {}), call.build_component_cli('demo', {})])

    def test_no_cli_clients_performs_no_build(self):
        self.assertEqual(self.run_build({}), [])


if __name__ == '__main__':
    unittest.main()
