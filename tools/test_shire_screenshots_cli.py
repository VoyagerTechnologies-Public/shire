"""Scenario screenshot command line: setup, library discovery, stack control, readiness, log reading, and dispatch.

The browser, Docker, and every subprocess are replaced, so nothing here starts a stack or downloads anything."""
import argparse
import contextlib
import importlib.util
import io
import pathlib
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

TOOLS = pathlib.Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

_spec = importlib.util.spec_from_file_location("shire_screenshots_cli", TOOLS / "shire-screenshots.py")
cli = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(cli)


def completed(returncode=0, stdout="", stderr=""):
    return subprocess.CompletedProcess([], returncode, stdout, stderr)


class QuietTestCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = pathlib.Path(self.tmp.name)
        for target in ("builtins.print",):
            patcher = mock.patch(target)
            self.printed = patcher.start()
            self.addCleanup(patcher.stop)


class PathTests(QuietTestCase):
    def test_the_venv_python_lives_under_the_screenshots_environment(self):
        self.assertEqual(cli.venv_python(), cli.VENV_DIR / "bin" / "python")

    def test_library_path_prefers_unpacked_libraries_and_keeps_the_callers_path(self):
        libs = self.dir / "libs"
        (libs / "usr/lib/x86_64-linux-gnu").mkdir(parents=True)
        with mock.patch.object(cli, "LIBS_DIR", libs):
            with mock.patch.dict(cli.os.environ, {"LD_LIBRARY_PATH": "/opt/lib"}):
                self.assertEqual(cli.library_path(), f"{libs}/usr/lib/x86_64-linux-gnu:/opt/lib")
            with mock.patch.dict(cli.os.environ, {}, clear=True):
                self.assertEqual(cli.library_path(), f"{libs}/usr/lib/x86_64-linux-gnu")
        with mock.patch.object(cli, "LIBS_DIR", self.dir / "none"):
            with mock.patch.dict(cli.os.environ, {}, clear=True):
                self.assertEqual(cli.library_path(), "")

    def test_the_active_mission_comes_from_the_saved_selection(self):
        root = self.dir
        (root / "build").mkdir()
        (root / "build/active.yaml").write_text("mission: drm\nboard: rpi5\n", encoding="utf-8")
        with mock.patch.object(cli, "ROOT", root):
            self.assertEqual(cli.active_mission(), "drm")

    def test_sh_captures_text_output(self):
        result = cli.sh([sys.executable, "-c", "print('hello')"])
        self.assertEqual((result.returncode, result.stdout.strip()), (0, "hello"))


class SetupTests(QuietTestCase):
    def setUp(self):
        super().setUp()
        self.venv = self.dir / "venv"
        patcher = mock.patch.object(cli, "VENV_DIR", self.venv)
        patcher.start()
        self.addCleanup(patcher.stop)
        patcher = mock.patch.object(cli, "ROOT", self.dir)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.run_calls = []

    def fake_run(self, *args, **kwargs):
        self.run_calls.append(args[0])
        return completed()

    def test_a_working_environment_is_reused_and_playwright_installed_with_its_pip(self):
        (self.venv / "bin").mkdir(parents=True)
        (self.venv / "bin/python").write_text("")
        with mock.patch.object(cli, "sh", return_value=completed()) as sh, \
                mock.patch.object(cli.subprocess, "run", side_effect=self.fake_run), \
                mock.patch.object(cli, "ensure_libraries", return_value=0) as ensure:
            self.assertEqual(cli.setup(argparse.Namespace()), 0)
        sh.assert_called_once()
        self.assertEqual(self.run_calls[0][-3:-1], ["install", "-r"])
        self.assertEqual(self.run_calls[1][1:], ["-m", "playwright", "install", "chromium"])
        ensure.assert_called_once()

    def test_a_system_without_ensurepip_gets_a_bare_environment_and_the_system_pip(self):
        sh_results = [completed(returncode=1), completed(returncode=1)]
        with mock.patch.object(cli, "sh", side_effect=sh_results), \
                mock.patch.object(cli.subprocess, "run", side_effect=self.fake_run), \
                mock.patch.object(cli.shutil, "rmtree") as rmtree, \
                mock.patch.object(cli, "ensure_libraries", return_value=0):
            self.assertEqual(cli.setup(argparse.Namespace()), 0)
        rmtree.assert_called_once()
        self.assertIn("--without-pip", self.run_calls[0])
        self.assertEqual(self.run_calls[1][:3], ["pip3", "--python", str(self.venv / "bin/python")])

    def test_the_result_of_the_library_check_is_the_result_of_setup(self):
        (self.venv / "bin").mkdir(parents=True)
        (self.venv / "bin/python").write_text("")
        with mock.patch.object(cli, "sh", return_value=completed()), \
                mock.patch.object(cli.subprocess, "run", side_effect=self.fake_run), \
                mock.patch.object(cli, "ensure_libraries", return_value=1):
            self.assertEqual(cli.setup(argparse.Namespace()), 1)


class LibraryTests(QuietTestCase):
    def test_chromium_is_the_newest_installed_headless_shell(self):
        home = self.dir
        for version in ("chromium_headless_shell-1100", "chromium_headless_shell-1200"):
            binary = home / ".cache/ms-playwright" / version / "linux64" / "chrome-headless-shell"
            binary.parent.mkdir(parents=True)
            binary.write_text("")
        with mock.patch.object(cli.pathlib.Path, "home", return_value=home):
            self.assertIn("1200", str(cli.chromium_binary()))
        with mock.patch.object(cli.pathlib.Path, "home", return_value=self.dir / "empty"):
            self.assertIsNone(cli.chromium_binary())

    def test_missing_libraries_are_the_ones_ldd_cannot_find(self):
        output = "\tlibc.so.6 => /lib/libc.so.6\n\tlibnss3.so => not found\n\tlibnspr4.so => not found\n"
        with mock.patch.object(cli, "sh", return_value=completed(stdout=output)):
            self.assertEqual(cli.missing_libraries(pathlib.Path("/x/chrome")), ["libnss3.so", "libnspr4.so"])

    def test_no_browser_is_an_error(self):
        with mock.patch.object(cli, "chromium_binary", return_value=None):
            self.assertEqual(cli.ensure_libraries(), 1)

    def test_a_browser_with_every_library_needs_nothing(self):
        with mock.patch.object(cli, "chromium_binary", return_value=pathlib.Path("/x/chrome")), \
                mock.patch.object(cli, "missing_libraries", return_value=[]):
            self.assertEqual(cli.ensure_libraries(), 0)

    def test_missing_libraries_are_unpacked_from_distribution_packages(self):
        libs = self.dir / "libs"
        calls = []

        def fake_sh(command, **kwargs):
            calls.append(command)
            if command[0] == "apt-get":
                (libs / "debs").mkdir(parents=True, exist_ok=True)
                (libs / "debs/libnss3.deb").write_text("")
            return completed()

        with mock.patch.object(cli, "LIBS_DIR", libs), \
                mock.patch.object(cli, "ROOT", self.dir), \
                mock.patch.object(cli, "chromium_binary", return_value=pathlib.Path("/x/chrome")), \
                mock.patch.object(cli, "missing_libraries", side_effect=[["libnss3.so", "libnssutil3.so"], []]), \
                mock.patch.object(cli, "sh", side_effect=fake_sh), \
                mock.patch.object(cli.subprocess, "run") as run:
            self.assertEqual(cli.ensure_libraries(), 0)
        download = next(call for call in calls if call[0] == "apt-get")
        self.assertEqual(download, ["apt-get", "download", "libnss3"])
        self.assertEqual(run.call_args.args[0][:2], ["dpkg-deb", "-x"])

    def test_libraries_that_cannot_be_provided_say_how_to_install_them(self):
        libs = self.dir / "libs"
        with mock.patch.object(cli, "LIBS_DIR", libs), \
                mock.patch.object(cli, "chromium_binary", return_value=pathlib.Path("/x/chrome")), \
                mock.patch.object(cli, "missing_libraries", return_value=["libasound.so.2"]), \
                mock.patch.object(cli, "sh", return_value=completed(returncode=1)):
            self.assertEqual(cli.ensure_libraries(), 1)
        message = " ".join(str(call.args[0]) for call in self.printed.call_args_list)
        self.assertIn("playwright install-deps chromium", message)


class CheckTests(QuietTestCase):
    def args(self, **kwargs):
        return argparse.Namespace(scenario="demo", manifest=None, **kwargs)

    def test_a_missing_manifest_is_an_error(self):
        with mock.patch.object(cli, "load_manifest", side_effect=OSError("no such file")):
            self.assertEqual(cli.check(self.args()), 1)

    def test_a_malformed_manifest_is_an_error(self):
        with mock.patch.object(cli, "load_manifest", side_effect=cli.ManifestError("bad")):
            self.assertEqual(cli.check(self.args()), 1)

    def test_drift_between_the_manifest_and_the_page_fails_and_is_listed(self):
        manifest = mock.Mock(shot_names=["a"], doc="scenarios/demo.md")
        with mock.patch.object(cli, "load_manifest", return_value=manifest), \
                mock.patch.object(cli, "check_against_docs", return_value=["no image for shot 'a'"]):
            self.assertEqual(cli.check(self.args()), 1)
        self.assertIn("no image for shot 'a'", str(self.printed.call_args_list))

    def test_agreement_reports_the_screenshot_count(self):
        manifest = mock.Mock(shot_names=["a", "b"], doc="scenarios/demo.md")
        with mock.patch.object(cli, "load_manifest", return_value=manifest), \
                mock.patch.object(cli, "check_against_docs", return_value=[]):
            self.assertEqual(cli.check(self.args()), 0)
        self.assertIn("demo: 2 screenshots match scenarios/demo.md", str(self.printed.call_args_list))

    def test_a_manifest_with_no_page_is_described_as_the_manifest(self):
        manifest = mock.Mock(shot_names=["a"], doc=None)
        with mock.patch.object(cli, "load_manifest", return_value=manifest), \
                mock.patch.object(cli, "check_against_docs", return_value=[]):
            self.assertEqual(cli.check(self.args()), 0)
        self.assertIn("match the manifest", str(self.printed.call_args_list))


class StackTests(QuietTestCase):
    def test_start_runs_make_start_in_its_own_session_and_logs_to_the_file(self):
        run_dir = self.dir / "run"
        with mock.patch.object(cli, "RUN_DIR", run_dir), mock.patch.object(cli, "ROOT", self.dir), \
                mock.patch.object(cli.subprocess, "Popen") as popen:
            process = cli.start_stack(run_dir / "start.log")
        self.assertIs(process, popen.return_value)
        self.assertEqual(popen.call_args.args[0], ["make", "start"])
        self.assertTrue(popen.call_args.kwargs["start_new_session"])
        self.assertTrue((run_dir / "start.log").exists())

    def test_stop_interrupts_a_running_stack_and_always_runs_make_stop(self):
        process = mock.Mock(pid=4242)
        process.poll.return_value = None
        with mock.patch.object(cli.os, "getpgid", return_value=77), mock.patch.object(cli.os, "killpg") as killpg, \
                mock.patch.object(cli.subprocess, "run") as run:
            cli.stop_stack(process)
        killpg.assert_called_once_with(77, signal.SIGINT)
        process.wait.assert_called_once_with(timeout=60)
        self.assertEqual(run.call_args.args[0], ["make", "stop"])

    def test_a_stack_that_ignores_the_interrupt_is_killed(self):
        process = mock.Mock(pid=4242)
        process.poll.return_value = None
        process.wait.side_effect = subprocess.TimeoutExpired("make", 60)
        with mock.patch.object(cli.os, "getpgid", return_value=77), mock.patch.object(cli.os, "killpg") as killpg, \
                mock.patch.object(cli.subprocess, "run"):
            cli.stop_stack(process)
        self.assertEqual([call.args[1] for call in killpg.call_args_list], [signal.SIGINT, signal.SIGKILL])

    def test_an_attached_or_finished_stack_is_not_signalled_but_stop_still_runs(self):
        finished = mock.Mock()
        finished.poll.return_value = 0
        for process in (None, finished):
            with mock.patch.object(cli.os, "killpg") as killpg, mock.patch.object(cli.subprocess, "run") as run:
                cli.stop_stack(process)
            killpg.assert_not_called()
            run.assert_called_once()


class ReadyTests(QuietTestCase):
    def test_ready_returns_as_soon_as_yamcs_answers(self):
        yamcs = mock.Mock()
        yamcs.ready.side_effect = [False, True]
        with mock.patch.object(cli.time, "sleep") as sleep:
            cli.wait_ready(yamcs, None)
        sleep.assert_called_once_with(2)

    def test_a_stack_that_exits_before_it_is_ready_is_reported_with_its_log(self):
        yamcs = mock.Mock()
        yamcs.ready.return_value = False
        process = mock.Mock(returncode=2)
        process.poll.return_value = 2
        with mock.patch.object(cli, "RUN_DIR", self.dir / "run"), mock.patch.object(cli, "ROOT", self.dir):
            with self.assertRaisesRegex(cli.CaptureError, r"exited with code 2.*start\.log"):
                cli.wait_ready(yamcs, process)

    def test_a_stack_that_never_answers_times_out(self):
        yamcs = mock.Mock()
        yamcs.ready.return_value = False
        clock = iter([0, 1, cli.READY_TIMEOUT_S + 1])
        with mock.patch.object(cli.time, "monotonic", side_effect=lambda: next(clock)), \
                mock.patch.object(cli.time, "sleep"):
            with self.assertRaisesRegex(cli.CaptureError, f"not ready after {cli.READY_TIMEOUT_S}s"):
                cli.wait_ready(yamcs, mock.Mock(**{"poll.return_value": None}))


class LogReaderTests(QuietTestCase):
    def reader(self, start_log=None):
        (self.dir / "build/drm").mkdir(parents=True, exist_ok=True)
        (self.dir / "build/active.yaml").write_text("mission: drm\n", encoding="utf-8")
        with mock.patch.object(cli, "ROOT", self.dir):
            return cli.log_reader(start_log)

    def test_start_log_shots_read_the_saved_output(self):
        log = self.dir / "start.log"
        log.write_text("shire-fsw | boot\n", encoding="utf-8")
        self.assertEqual(self.reader(log)("start_log", None), "shire-fsw | boot\n")

    def test_start_log_shots_need_a_log(self):
        for start_log in (None, self.dir / "missing.log"):
            with self.assertRaisesRegex(cli.CaptureError, "needs the stack started by this tool"):
                self.reader(start_log)("start_log", None)

    def test_compose_logs_come_from_the_active_missions_compose_file_and_may_name_a_service(self):
        read = self.reader()
        with mock.patch.object(cli, "sh", return_value=completed(stdout="line\n")) as sh:
            self.assertEqual(read("compose_logs", "shire-fsw"), "line\n")
            self.assertEqual(read("compose_logs", None), "line\n")
        with_service, without = (call.args[0] for call in sh.call_args_list)
        self.assertEqual(with_service[-1], "shire-fsw")
        self.assertEqual(without[-1], "--no-color")
        self.assertIn(str(self.dir / "build/drm/shire-compose.yaml"), with_service)

    def test_a_failing_compose_logs_command_is_an_error_with_its_message(self):
        read = self.reader()
        with mock.patch.object(cli, "sh", return_value=completed(returncode=1, stderr="no such service\n")):
            with self.assertRaisesRegex(cli.CaptureError, "docker compose logs failed: no such service"):
                read("compose_logs", "x")


class RunTests(QuietTestCase):
    def args(self, **overrides):
        values = dict(scenario="demo", manifest=None, base_url=None, output_dir=None, start_log=None, attach=False,
                      keep_running=False, only=None, dry_run=False)
        values.update(overrides)
        return argparse.Namespace(**values)

    def setUp(self):
        super().setUp()
        self.manifest = mock.Mock(base_url="http://h:8090", instance="shire", processor="realtime",
                                  viewport={"width": 1280, "height": 800}, output_dir=self.dir / "out")
        self.browser = mock.MagicMock()
        self.sync = mock.MagicMock()
        self.sync.return_value.__enter__.return_value.chromium.launch.return_value = self.browser
        self.yamcs = mock.Mock()
        self.yamcs.ready.return_value = False
        self.runner = mock.Mock()
        self.runner.return_value.run.return_value = [self.dir / "out/a.png"]
        modules = {"playwright": mock.MagicMock(), "playwright.sync_api": mock.MagicMock(sync_playwright=self.sync)}
        patcher = mock.patch.dict(sys.modules, modules)
        patcher.start()
        self.addCleanup(patcher.stop)
        for name, value in {"load_manifest": mock.Mock(return_value=self.manifest),
                            "YamcsClient": mock.Mock(return_value=self.yamcs), "Runner": self.runner,
                            "start_stack": mock.Mock(), "stop_stack": mock.Mock(), "wait_ready": mock.Mock(),
                            "log_reader": mock.Mock(), "display_path": mock.Mock(return_value="out")}.items():
            patcher = mock.patch.object(cli, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)
        patcher = mock.patch.object(cli, "RUN_DIR", self.dir / "run")
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_without_playwright_it_says_how_to_install_it(self):
        with mock.patch.dict(sys.modules, {"playwright.sync_api": None}):
            self.assertEqual(cli.run(self.args()), 2)
        self.assertIn("make screenshots-setup", str(self.printed.call_args_list))

    def test_a_bad_manifest_is_an_error_before_anything_starts(self):
        cli.load_manifest.side_effect = cli.ManifestError("bad")
        self.assertEqual(cli.run(self.args()), 1)
        cli.start_stack.assert_not_called()

    def test_a_stack_that_is_already_answering_is_not_replaced(self):
        self.yamcs.ready.return_value = True
        self.assertEqual(cli.run(self.args()), 2)
        cli.start_stack.assert_not_called()
        self.assertIn("--attach", str(self.printed.call_args_list))

    def test_a_capture_starts_the_stack_runs_the_steps_and_stops_it(self):
        self.assertEqual(cli.run(self.args(only=["a"], dry_run=True)), 0)
        cli.start_stack.assert_called_once()
        cli.wait_ready.assert_called_once()
        self.assertEqual(self.runner.call_args.kwargs, {"only": {"a"}, "dry_run": True})
        self.browser.close.assert_called_once()
        cli.stop_stack.assert_called_once_with(cli.start_stack.return_value)

    def test_overrides_replace_the_manifests_address_and_image_folder(self):
        self.assertEqual(cli.run(self.args(base_url="http://other:9000/", output_dir=self.dir / "elsewhere")), 0)
        self.assertEqual(self.manifest.base_url, "http://other:9000")
        self.assertEqual(self.manifest.output_dir, (self.dir / "elsewhere").resolve())

    def test_attaching_uses_the_running_stack_and_leaves_it_up(self):
        self.assertEqual(cli.run(self.args(attach=True)), 0)
        cli.start_stack.assert_not_called()
        cli.stop_stack.assert_not_called()

    def test_keep_running_leaves_a_stack_that_it_started(self):
        self.assertEqual(cli.run(self.args(keep_running=True)), 0)
        cli.start_stack.assert_called_once()
        cli.stop_stack.assert_not_called()

    def test_a_failed_capture_is_reported_and_still_stops_the_stack(self):
        self.runner.return_value.run.side_effect = cli.CaptureError("step 3 failed")
        self.assertEqual(cli.run(self.args()), 1)
        cli.stop_stack.assert_called_once()
        self.assertIn("step 3 failed", str(self.printed.call_args_list))


class MainTests(QuietTestCase):
    def dispatch(self, argv):
        with mock.patch.object(sys, "argv", ["shire-screenshots.py", *argv]):
            return cli.main()

    def test_each_subcommand_reaches_its_handler_with_its_options(self):
        with mock.patch.object(cli, "setup", return_value=0) as setup:
            self.assertEqual(self.dispatch(["setup"]), 0)
        setup.assert_called_once()
        with mock.patch.object(cli, "check", return_value=1) as check:
            self.assertEqual(self.dispatch(["check", "--scenario", "demo"]), 1)
        self.assertEqual(check.call_args.args[0].scenario, "demo")
        with mock.patch.object(cli, "run", return_value=0) as run:
            self.dispatch(["run", "--scenario", "demo", "--only", "a", "b", "--attach", "--keep-running", "--dry-run",
                           "--base-url", "http://h:1", "--start-log", "x.log", "--output-dir", "out"])
        options = run.call_args.args[0]
        self.assertEqual((options.only, options.attach, options.keep_running, options.dry_run),
                         (["a", "b"], True, True, True))
        self.assertEqual((options.base_url, options.start_log, options.output_dir),
                         ("http://h:1", pathlib.Path("x.log"), pathlib.Path("out")))

    def test_a_command_and_a_scenario_are_required(self):
        for argv in ([], ["check"], ["run"]):
            with self.subTest(argv=argv), self.assertRaises(SystemExit) as raised, \
                    contextlib.redirect_stderr(io.StringIO()):
                self.dispatch(argv)
            self.assertEqual(raised.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
