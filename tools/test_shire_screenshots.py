"""Scenario screenshot capture: manifest validation, log selection, terminal frames, and the step engine."""
from pathlib import Path
import tempfile
import textwrap
import unittest
from unittest import mock

import shire_screenshots as shots
from shire_screenshots import (CaptureError, Environment, ManifestError, Runner, YamcsClient, apply_redactions,
                               engineering_value, load_manifest, render_terminal_html, select_lines, strip_ansi,
                               values_equal)

BASIC = """
scenario: demo
steps:
  - goto: /links
  - shot: {name: 01-links}
"""


def write(directory: Path, text: str, name: str = "demo.yaml") -> Path:
    path = directory / name
    path.write_text(textwrap.dedent(text), encoding="utf-8")
    return path


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)

    def load(self, text: str):
        return load_manifest(write(self.dir, text))

    def test_defaults(self):
        manifest = self.load(BASIC)
        self.assertEqual(manifest.base_url, "http://localhost:8090")
        self.assertEqual((manifest.instance, manifest.processor), ("shire", "realtime"))
        self.assertEqual(manifest.viewport, {"width": 1280, "height": 800})
        self.assertEqual(manifest.output_dir, shots.DOCS / "assets/scenarios/demo")
        self.assertEqual(manifest.shot_names, ["01-links"])

    def test_viewport_and_address_overrides(self):
        manifest = self.load("""
            scenario: demo
            base_url: http://localhost:9000/
            viewport: {width: 900}
            steps: [{goto: /}]
        """)
        self.assertEqual(manifest.base_url, "http://localhost:9000")
        self.assertEqual(manifest.viewport, {"width": 900, "height": 800})

    def test_rejects_malformed_manifests(self):
        cases = {
            "'scenario' is required": "steps: [{goto: /}]",
            "non-empty list": "scenario: demo\nsteps: []",
            "exactly one action": "scenario: demo\nsteps: [{goto: /, click: x}]",
            "unknown action": "scenario: demo\nsteps: [{teleport: /}]",
            "takes a mapping": "scenario: demo\nsteps: [{wait: 5}]",
            "lowercase words": "scenario: demo\nsteps: [{shot: {name: Bad_Name}}]",
            "shot kind": "scenario: demo\nsteps: [{shot: {name: a, kind: video}}]",
            "terminal source": "scenario: demo\nsteps: [{shot: {name: a, kind: terminal, source: dmesg}}]",
            "qualified": "scenario: demo\nsteps: [{command: {name: SC_START_RTS}}]",
            "wait_param needs": "scenario: demo\nsteps: [{wait_param: {timeout_s: 3}}]",
            "wait_log needs": "scenario: demo\nsteps: [{wait_log: {timeout_s: 3}}]",
            "record needs": "scenario: demo\nsteps: [{record: {name: /A/B}}]",
            "invalid redact": "scenario: demo\nredact: [{pattern: '('}]\nsteps: [{goto: /}]",
            "duplicate shot": "scenario: demo\nsteps: [{shot: {name: a}}, {shot: {name: a}}]",
        }
        for fragment, text in cases.items():
            with self.subTest(fragment):
                with self.assertRaisesRegex(ManifestError, fragment):
                    self.load(text)

    def test_errors_name_the_step(self):
        with self.assertRaisesRegex(ManifestError, "step 2"):
            self.load("scenario: demo\nsteps: [{goto: /}, {teleport: /}]")


class DocsAgreementTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.docs = Path(self.tmp.name)
        patcher = mock.patch.object(shots, "DOCS", self.docs)
        patcher.start()
        self.addCleanup(patcher.stop)
        (self.docs / "scenarios").mkdir()
        (self.docs / "assets/scenarios/demo").mkdir(parents=True)

    def manifest(self):
        path = write(self.docs, """
            scenario: demo
            doc: scenarios/demo.md
            steps:
              - shot: {name: 01-a}
              - shot: {name: 02-b}
        """)
        return load_manifest(path)

    def test_agreement_is_clean(self):
        (self.docs / "scenarios/demo.md").write_text(
            "![a](../assets/scenarios/demo/01-a.png)\n![b](../assets/scenarios/demo/02-b.png)\n")
        for name in ("01-a", "02-b"):
            (self.docs / f"assets/scenarios/demo/{name}.png").write_bytes(b"png")
        self.assertEqual(shots.check_against_docs(self.manifest()), [])

    def test_reports_each_kind_of_drift(self):
        (self.docs / "scenarios/demo.md").write_text(
            "![a](../assets/scenarios/demo/01-a.png)\n![x](../assets/scenarios/demo/03-extra.png)\n")
        (self.docs / "assets/scenarios/demo/01-a.png").write_bytes(b"png")
        problems = shots.check_against_docs(self.manifest())
        self.assertIn("02-b: image has not been generated", problems)
        self.assertIn("02-b: not referenced by scenarios/demo.md", problems)
        self.assertIn("03-extra: referenced by scenarios/demo.md but has no shot in the manifest", problems)
        self.assertEqual(len(problems), 3)

    def test_missing_page_is_reported(self):
        self.assertIn("doc page does not exist: scenarios/demo.md", shots.check_against_docs(self.manifest()))


class TerminalTests(unittest.TestCase):
    LOG = textwrap.dedent("""\
        \x1b[32mshire-fsw-sat-1\x1b[0m  | EVS 23/1/SC 73: RTS Number 005 Started
        shire-server-drm       |   Simulation time: 10.000 seconds
        shire-fsw-sat-1  | EVS 23/1/CFE_ES 3: No-op command: first
        shire-fsw-sat-1  | EVS 23/1/LC 5: Enabling radio
        shire-fsw-sat-1  | EVS 23/1/CFE_ES 3: No-op command: second
        shire-fsw-sat-1  | EVS 23/1/CFE_ES 92: Build 1 by shire@1b81669dc597, config x
    """)

    def test_strip_ansi(self):
        self.assertEqual(strip_ansi("\x1b[1;32mgreen\x1b[0m plain"), "green plain")

    def test_build_banner_redaction_is_always_applied(self):
        text = apply_redactions("Build 2026 by shire@1b81669dc597, config", shots.DEFAULT_REDACTIONS)
        self.assertEqual(text, "Build 2026 by shire@builder, config")

    def test_custom_redaction(self):
        self.assertEqual(apply_redactions("key=abc123", [{"pattern": r"key=\w+", "replace": "key=REDACTED"}]),
                         "key=REDACTED")

    def test_after_starts_at_the_newest_match(self):
        lines = select_lines(self.LOG, {"after": "No-op command"})
        self.assertTrue(lines[0].endswith("second"))
        self.assertEqual(len(lines), 2)

    def test_include_exclude_head_tail(self):
        self.assertEqual(len(select_lines(self.LOG, {"include": "shire-fsw"})), 5)
        self.assertEqual(len(select_lines(self.LOG, {"include": ["RTS", "LC 5"]})), 2)
        self.assertEqual(len(select_lines(self.LOG, {"exclude": "Simulation"})), 5)
        self.assertEqual(len(select_lines(self.LOG, {"head": 2})), 2)
        self.assertTrue(select_lines(self.LOG, {"tail": 1})[0].endswith("config x"))

    def test_selecting_nothing_is_an_error_not_an_empty_image(self):
        for spec in ({"include": "no such line"}, {"after": "no such line"}):
            with self.subTest(spec), self.assertRaises(LookupError):
                select_lines(self.LOG, spec)

    def test_render_escapes_markup_and_colors_each_service_consistently(self):
        page = render_terminal_html(["shire-fsw-sat-1  | <b>&</b>", "shire-server-drm | x", "shire-fsw-sat-1  | y"],
                                    "make <start>")
        self.assertIn("&lt;b&gt;&amp;&lt;/b&gt;", page)
        self.assertIn("make &lt;start&gt;", page)
        self.assertNotIn("<b>", page)
        fsw = shots.TERMINAL_SERVICE_COLORS[0]
        self.assertEqual(page.count(f'style="color:{fsw}">shire-fsw-sat-1'), 2)
        self.assertIn(f'style="color:{shots.TERMINAL_SERVICE_COLORS[1]}">shire-server-drm', page)

    def test_unprefixed_lines_are_kept(self):
        self.assertIn("plain text", render_terminal_html(["plain text"], "t"))


class ParameterValueTests(unittest.TestCase):
    def test_engineering_value_reads_whichever_typed_field_is_present(self):
        self.assertEqual(engineering_value({"engValue": {"type": "UINT32", "uint32Value": 3}}), 3)
        self.assertEqual(engineering_value({"engValue": {"type": "ENUMERATED", "stringValue": "DUPLEX"}}), "DUPLEX")
        self.assertIsNone(engineering_value({}))

    def test_nul_padding_on_flight_strings_is_trimmed(self):
        padded = {"engValue": {"type": "STRING", "stringValue": "sv2023259230208.ds\x00\x00\x00"}}
        self.assertEqual(engineering_value(padded), "sv2023259230208.ds")

    def test_values_equal_compares_numbers_numerically_and_text_as_text(self):
        self.assertTrue(values_equal(3, "3"))
        self.assertTrue(values_equal(3.0, 3))
        self.assertTrue(values_equal("DUPLEX", "DUPLEX"))
        self.assertFalse(values_equal(2, 3))
        self.assertFalse(values_equal("DUPLEX", "RECEIVE"))
        self.assertTrue(values_equal(True, True))
        self.assertFalse(values_equal(False, True))

    def test_client_transfers_reads_the_service_endpoint(self):
        session = mock.Mock()
        session.get.return_value = mock.Mock(ok=True, json=lambda: {"transfers": [{"state": "COMPLETED"}]})
        client = YamcsClient("http://h:8090", "shire", "realtime", session)
        self.assertEqual(client.transfers(), [{"state": "COMPLETED"}])
        self.assertEqual(session.get.call_args.args[0], "http://h:8090/api/filetransfer/shire/cfdp/transfers")
        session.get.return_value = mock.Mock(ok=False)
        self.assertIsNone(client.transfers())

    def test_client_parameter_builds_the_processor_url_and_tolerates_failures(self):
        session = mock.Mock()
        session.get.return_value = mock.Mock(ok=True, json=lambda: {"engValue": {"uint32Value": 1}})
        client = YamcsClient("http://h:8090", "shire", "realtime", session)
        self.assertEqual(client.parameter("/RADIO/MODE"), {"engValue": {"uint32Value": 1}})
        self.assertEqual(session.get.call_args.args[0],
                         "http://h:8090/api/processors/shire/realtime/parameters/RADIO/MODE")
        session.get.return_value = mock.Mock(ok=False)
        self.assertIsNone(client.parameter("/RADIO/MODE"))
        session.get.side_effect = shots.requests.ConnectionError()
        self.assertIsNone(client.parameter("/RADIO/MODE"))
        self.assertFalse(client.ready())


class FakeClock:
    def __init__(self):
        self.time = 0.0

    def now(self):
        return self.time

    def sleep(self, seconds):
        self.time += seconds


class FakeYamcs:
    """Returns each queued reply in turn, then keeps returning the last one."""

    def __init__(self, *replies, transfers=None):
        self.replies = list(replies)
        self.transfer_replies = list(transfers or [[]])

    def transfers(self, service="cfdp"):
        return self.transfer_replies.pop(0) if len(self.transfer_replies) > 1 else self.transfer_replies[0]

    def parameter(self, name):
        return self.replies.pop(0) if len(self.replies) > 1 else self.replies[0]


def reading(value, generation="t0"):
    return {"engValue": {"type": "UINT32", "uint32Value": value}, "generationTime": generation}


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        printing = mock.patch("builtins.print")
        printing.start()
        self.addCleanup(printing.stop)
        self.clock = FakeClock()
        self.page = mock.MagicMock()
        self.terminal_page = mock.MagicMock()
        self.logs = mock.Mock(return_value="shire-fsw-sat-1  | EVS 23/1/SC 86: RTS 005 Execution Completed\n")

    def runner(self, steps, yamcs=None, only=None, dry_run=False):
        manifest = shots.Manifest(scenario="demo", doc=None, output_dir=self.dir / "out", base_url="http://h:8090",
                                  instance="shire", processor="realtime", viewport={"width": 1280, "height": 800},
                                  redactions=shots.DEFAULT_REDACTIONS, steps=steps)
        env = Environment(page=self.page, terminal_page=self.terminal_page, yamcs=yamcs or FakeYamcs(reading(0)),
                          logs=self.logs, sleep=self.clock.sleep, now=self.clock.now)
        return Runner(manifest, env, only=only, dry_run=dry_run)

    def test_goto_adds_the_processor_to_yamcs_pages_but_not_the_viewer(self):
        self.runner([{"goto": "/links"}, {"goto": "/events?severity=INFO"}, {"goto": "/visualization/"}]).run()
        urls = [call.args[0] for call in self.page.goto.call_args_list]
        self.assertEqual(urls, ["http://h:8090/links?c=shire__realtime",
                                "http://h:8090/events?severity=INFO&c=shire__realtime",
                                "http://h:8090/visualization/"])

    def test_web_shot_writes_png_and_honors_clip_hide_and_full_page(self):
        runner = self.runner([{"shot": {"name": "a"}},
                              {"shot": {"name": "b", "clip": "table", "hide": [".host"]}},
                              {"shot": {"name": "c", "full_page": True}}])
        written = runner.run()
        self.assertEqual([p.name for p in written], ["a.png", "b.png", "c.png"])
        self.assertTrue((self.dir / "out").is_dir())
        self.page.locator.assert_called_with("table")
        clipped = self.page.locator.return_value.first.screenshot.call_args.kwargs
        self.assertEqual(clipped["style"], ".host { visibility: hidden !important; }")
        self.assertTrue(self.page.screenshot.call_args_list[-1].kwargs["full_page"])

    def test_only_writes_selected_shots_but_still_runs_every_action(self):
        runner = self.runner([{"goto": "/a"}, {"shot": {"name": "a"}}, {"goto": "/b"}, {"shot": {"name": "b"}}],
                             only={"b"})
        self.assertEqual([p.name for p in runner.run()], ["b.png"])
        self.assertEqual(self.page.goto.call_count, 2)

    def test_unknown_only_name_fails_before_anything_runs(self):
        with self.assertRaisesRegex(CaptureError, "unknown shot name.*zzz"):
            self.runner([{"goto": "/a"}, {"shot": {"name": "a"}}], only={"zzz"}).run()
        self.page.goto.assert_not_called()

    def test_dry_run_runs_steps_but_writes_nothing(self):
        runner = self.runner([{"goto": "/a"}, {"shot": {"name": "a"}},
                              {"shot": {"name": "t", "kind": "terminal", "include": "RTS"}}], dry_run=True)
        runner.run()
        self.page.screenshot.assert_not_called()
        self.terminal_page.locator.assert_not_called()
        self.assertFalse((self.dir / "out").exists())

    def test_terminal_shot_renders_on_its_own_page_with_redaction(self):
        self.logs.return_value = "shire-fsw-sat-1  | Build 1 by shire@1b81669dc597 ok\n"
        self.runner([{"shot": {"name": "t", "kind": "terminal", "service": "shire-fsw", "title": "FSW"}}]).run()
        self.logs.assert_called_once_with("compose_logs", "shire-fsw")
        html = self.terminal_page.set_content.call_args.args[0]
        self.assertIn("shire@builder", html)
        self.assertNotIn("1b81669dc597", html)
        self.assertIn("FSW", html)
        self.page.set_content.assert_not_called()

    def test_terminal_shot_with_no_matching_lines_fails_the_run(self):
        with self.assertRaisesRegex(CaptureError, "step 1.*selected no log lines"):
            self.runner([{"shot": {"name": "t", "kind": "terminal", "include": "absent"}}]).run()

    def test_wait_param_returns_once_the_value_matches(self):
        yamcs = FakeYamcs(reading(2), reading(2), reading(3))
        self.runner([{"wait_param": {"name": "/RADIO/MODE", "equals": 3, "timeout_s": 10}}], yamcs).run()
        self.assertEqual(self.clock.time, 2.0)

    def test_wait_param_times_out_with_a_useful_message(self):
        with self.assertRaisesRegex(CaptureError, r"timed out after 5s waiting for /RADIO/MODE to equal 3"):
            self.runner([{"wait_param": {"name": "/RADIO/MODE", "equals": 3, "timeout_s": 5}}],
                        FakeYamcs(reading(2))).run()

    def test_fresh_requires_a_newer_packet_even_when_the_value_already_matches(self):
        yamcs = FakeYamcs(reading(3, "t0"), reading(3, "t0"), reading(3, "t0"), reading(3, "t1"))
        self.runner([{"wait_param": {"name": "/RADIO/MODE", "equals": 3, "fresh": True}}], yamcs).run()
        self.assertEqual(self.clock.time, 2.0)

    def test_parameter_that_does_not_exist_yet_is_waited_for(self):
        yamcs = FakeYamcs(None, None, reading(1))
        self.runner([{"wait_param": {"name": "/X/Y"}}], yamcs).run()
        self.assertEqual(self.clock.time, 2.0)

    def test_wait_log_polls_until_the_pattern_appears(self):
        self.logs.side_effect = ["nothing\n", "still nothing\n", "\x1b[32mRTS 005 Execution Completed\x1b[0m\n"]
        self.runner([{"wait_log": {"pattern": "RTS 005 Execution Completed", "source": "start_log"}}]).run()
        self.assertEqual(self.clock.time, 2.0)
        self.logs.assert_called_with("start_log", None)

    def test_wait_log_times_out(self):
        self.logs.return_value = "nothing\n"
        with self.assertRaisesRegex(CaptureError, "timed out after 4s waiting for log line"):
            self.runner([{"wait_log": {"pattern": "never", "timeout_s": 4}}]).run()

    def test_wait_transfer_waits_for_every_transfer_to_complete(self):
        running, done = [{"id": "1", "state": "RUNNING"}], [{"id": "1", "state": "COMPLETED"}]
        yamcs = FakeYamcs(reading(0), transfers=[[], None, running, done])
        self.runner([{"wait_transfer": {}}], yamcs).run()
        self.assertEqual(self.clock.time, 3.0)

    def test_wait_transfer_fails_fast_on_a_failed_transfer(self):
        failed = [{"id": "7", "state": "FAILED", "failureReason": "file not found"}]
        with self.assertRaisesRegex(CaptureError, "transfer 7 ended FAILED: file not found"):
            self.runner([{"wait_transfer": {}}], FakeYamcs(reading(0), transfers=[failed])).run()

    def test_wait_transfer_times_out_while_still_running(self):
        running = [{"id": "1", "state": "RUNNING"}]
        with self.assertRaisesRegex(CaptureError, "timed out after 3s waiting for cfdp transfers"):
            self.runner([{"wait_transfer": {"timeout_s": 3}}], FakeYamcs(reading(0), transfers=[running])).run()

    def test_wait_requires_a_condition(self):
        with self.assertRaisesRegex(CaptureError, "wait needs one of"):
            self.runner([{"wait": {}}]).run()

    def test_wait_dispatches_on_ms_selector_and_text(self):
        self.runner([{"wait": {"ms": 250}}, {"wait": {"selector": ".ready", "timeout_s": 2}},
                     {"wait": {"text": "debug-in"}}]).run()
        self.page.wait_for_timeout.assert_called_once_with(250)
        self.page.wait_for_selector.assert_called_once_with(".ready", timeout=2000)
        self.page.get_by_text.return_value.first.wait_for.assert_called_once()

    def test_click_and_fill_use_text_or_selector_locators(self):
        self.runner([{"click": "Links"}, {"click": {"selector": "#go"}},
                     {"fill": {"selector": "input.filter", "value": 10}}]).run()
        self.page.get_by_text.assert_called_with("Links", exact=False)
        self.page.locator.assert_any_call("#go")
        self.page.locator.return_value.first.fill.assert_called_once_with("10")

    def test_command_opens_the_form_expands_defaults_fills_arguments_and_sends(self):
        self.page.get_by_text.return_value.count.return_value = 1
        field = self.page.get_by_text.return_value.first.locator.return_value
        field.evaluate.return_value = "input"
        self.runner([{"command": {"name": "/SC/SC_COMMANDS/SC_START_RTS", "args": {"RTSID": 6, "PADDING": 0},
                                  "send": True}}]).run()
        self.assertEqual(self.page.goto.call_args.args[0],
                         "http://h:8090/commanding/send/SC/SC_COMMANDS/SC_START_RTS?c=shire__realtime")
        self.assertEqual([c.args[0] for c in field.fill.call_args_list], ["6", "0"])
        self.page.get_by_role.assert_called_with("button", name="Send", exact=False)

    def test_send_action_sends_an_already_filled_form(self):
        self.runner([{"send": True}]).run()
        self.page.get_by_role.assert_called_with("button", name="Send", exact=False)
        self.page.get_by_text.return_value.first.wait_for.assert_called_once()

    def test_command_without_send_leaves_the_form_unsent(self):
        self.page.get_by_text.return_value.count.return_value = 0
        self.runner([{"command": {"name": "/SC/SC_COMMANDS/SC_START_RTS"}}]).run()
        self.page.get_by_role.assert_not_called()

    def test_enumeration_arguments_choose_a_menu_entry(self):
        field = self.page.get_by_text.return_value.first.locator.return_value
        field.evaluate.return_value = "button"
        self.runner([{"command": {"name": "/RADIO/RADIO_CONFIG_CC", "args": {"MODE": "Duplex Mode"}}}]).run()
        field.click.assert_called_once()
        role, kwargs = self.page.get_by_role.call_args.args[0], self.page.get_by_role.call_args.kwargs
        self.assertEqual(role, "menuitem")
        pattern = kwargs["name"]
        self.assertTrue(pattern.match("Duplex Mode"))
        self.assertTrue(pattern.match("check\nDuplex Mode"))
        self.assertFalse(pattern.match("Transmit Only Mode"))

    def test_record_stores_a_value_that_later_steps_substitute(self):
        yamcs = FakeYamcs({"engValue": {"type": "STRING", "stringValue": "sv1980.ds"}})
        self.page.get_by_text.return_value.count.return_value = 0
        field = self.page.get_by_text.return_value.first.locator.return_value
        field.evaluate.return_value = "input"
        runner = self.runner([{"record": {"name": "/FM/FM_DIRLISTPKT/FILENAME1", "as": "file"}},
                              {"command": {"name": "/CF/CF_COMMANDS/CF_TX_FILE",
                                           "args": {"SOURCE_FILENAME": "/d/{file}", "KEEP": "{unset}"}}}], yamcs)
        runner.run()
        self.assertEqual(runner.recorded, {"file": "sv1980.ds"})
        self.assertEqual([c.args[0] for c in field.fill.call_args_list], ["/d/sv1980.ds", "{unset}"])

    def test_record_waits_for_the_parameter_to_exist(self):
        runner = self.runner([{"record": {"name": "/X/Y", "as": "v"}}], FakeYamcs(None, reading(7)))
        runner.run()
        self.assertEqual((runner.recorded["v"], self.clock.time), (7, 1.0))

    def test_wait_param_equals_a_recorded_baseline_plus_an_offset(self):
        yamcs = FakeYamcs(reading(4), reading(4), reading(5))
        self.runner([{"record": {"name": "/CFE_ES/CMD", "as": "es"}},
                     {"wait_param": {"name": "/CFE_ES/CMD", "equals": {"recorded": "es", "plus": 1}}}], yamcs).run()
        self.assertEqual(self.clock.time, 1.0)

    def test_wait_param_min_and_max_bounds(self):
        yamcs = FakeYamcs(reading(0), reading(5), reading(5))
        self.runner([{"wait_param": {"name": "/FM/SIZE", "min": 1}}], yamcs).run()
        self.assertEqual(self.clock.time, 1.0)
        with self.assertRaisesRegex(CaptureError, "timed out after 2s waiting for /FM/SIZE max 3"):
            self.runner([{"wait_param": {"name": "/FM/SIZE", "max": 3, "timeout_s": 2}}], FakeYamcs(reading(5))).run()

    def test_min_is_not_satisfied_by_a_text_value(self):
        text = {"engValue": {"type": "STRING", "stringValue": "abc"}}
        with self.assertRaises(CaptureError):
            self.runner([{"wait_param": {"name": "/X", "min": 1, "timeout_s": 1}}], FakeYamcs(text)).run()

    def test_hover_and_scroll_to_use_locators(self):
        self.runner([{"hover": {"selector": "[mattooltip=Run]"}}, {"scroll_to": "ANGRATE_Z"}]).run()
        self.page.locator.return_value.first.hover.assert_called_once()
        self.page.get_by_text.return_value.first.scroll_into_view_if_needed.assert_called_once()

    def test_a_failing_step_is_reported_with_its_number_and_action(self):
        self.page.goto.side_effect = RuntimeError("net::ERR_CONNECTION_REFUSED")
        with self.assertRaisesRegex(CaptureError, r"step 1 \(goto '/links'\) failed: net::ERR_CONNECTION_REFUSED"):
            self.runner([{"goto": "/links"}]).run()


if __name__ == "__main__":
    unittest.main()
