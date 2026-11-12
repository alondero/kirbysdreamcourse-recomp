import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / 'tools'


def powershell_executable():
    for name in ('pwsh', 'powershell'):
        found = shutil.which(name)
        if found:
            return found
    return None


SHELL = powershell_executable()


def probe(script):
    """Run a PowerShell snippet and return its stdout, stderr and markers."""
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'probe.ps1'
        path.write_text(script, encoding='utf-8-sig')
        result = subprocess.run(
            [SHELL, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(path)],
            capture_output=True, text=True)
    values = {}
    for line in result.stdout.splitlines():
        if '=' in line:
            key, _, value = line.partition('=')
            values[key.strip()] = value.strip()
    return result, values


def noise_script(directory):
    """A script that writes to standard error, writes to standard output and
    exits with a chosen status."""
    path = Path(directory) / 'noise.py'
    path.write_text(
        'import sys\n'
        'sys.stderr.write("progress noise\\n")\n'
        'print("real output")\n'
        'sys.exit(3)\n', encoding='utf-8')
    return path


@unittest.skipUnless(SHELL, 'PowerShell is not available on this machine.')
class NativeCommandHelperTest(unittest.TestCase):
    """Windows PowerShell 5.1 turns native standard error into a terminating
    error as soon as the error stream is redirected, which used to abort
    tools/setup.ps1 on ordinary build progress output."""

    def test_native_stderr_does_not_abort_the_script(self):
        with tempfile.TemporaryDirectory() as directory:
            noise = noise_script(directory)
            script = (
                "$ErrorActionPreference = 'Stop'\n"
                ". '" + str(TOOLS / 'invoke_native.ps1') + "'\n"
                "$result = Invoke-NativeCommand -FilePath '" + sys.executable + "' -Quiet "
                "-ArgumentList @('" + str(noise) + "')\n"
                "Write-Host ('EXITCODE=' + $result.ExitCode)\n"
                "Write-Host ('STDOUT=' + ($result.Output -join '|'))\n"
                "Write-Host ('STDERR=' + ($result.Error -join '|'))\n"
                "Write-Host 'REACHED-END'\n")
            result, values = probe(script)
        self.assertIn('REACHED-END', result.stdout,
                      'the script aborted on native standard error: ' + result.stderr)
        self.assertEqual(values.get('EXITCODE'), '3')
        self.assertEqual(values.get('STDOUT'), 'real output')
        self.assertIn('progress noise', values.get('STDERR', ''))

    def test_result_can_be_assigned_without_output_pollution(self):
        with tempfile.TemporaryDirectory() as directory:
            noise = noise_script(directory)
            script = (
                "$ErrorActionPreference = 'Stop'\n"
                ". '" + str(TOOLS / 'invoke_native.ps1') + "'\n"
                "$result = Invoke-NativeCommand -FilePath '" + sys.executable + "' -Quiet "
                "-ArgumentList @('" + str(noise) + "')\n"
                "Write-Host ('OUTPUT=' + ($result.Output -join '|'))\n")
            result, values = probe(script)
        self.assertEqual(values.get('OUTPUT'), 'real output')

    def test_missing_command_is_reported(self):
        script = (
            "$ErrorActionPreference = 'Stop'\n"
            ". '" + str(TOOLS / 'invoke_native.ps1') + "'\n"
            "try { Invoke-NativeCommand -FilePath 'kdc-missing-command' -Quiet | Out-Null }\n"
            "catch { Write-Host 'REACHED-END' }\n")
        result, _ = probe(script)
        self.assertIn('REACHED-END', result.stdout)


@unittest.skipUnless(SHELL, 'PowerShell is not available on this machine.')
class NativeCMakeDiscoveryTest(unittest.TestCase):
    """A native Windows CMake is required: the MSYS build that ships with
    devkitPro can shadow the native one on PATH."""

    def test_a_native_cmake_is_discovered(self):
        script = (
            "$ErrorActionPreference = 'Stop'\n"
            ". '" + str(TOOLS / 'find_native_cmake.ps1') + "'\n"
            "try { $found = Find-NativeCMake }\n"
            "catch { Write-Host ('ERROR=' + $_.Exception.Message); exit 0 }\n"
            "Write-Host ('FOUND=' + $found)\n"
            "$generators = @(Get-NativeCMakeGenerators -Path $found)\n"
            "Write-Host ('MINGW=' + ('MinGW Makefiles' -in $generators))\n")
        result, values = probe(script)
        if 'ERROR' in values:
            self.skipTest('no native CMake is installed on this machine')
        self.assertTrue(values.get('FOUND', '').lower().endswith('cmake.exe'))
        self.assertEqual(values.get('MINGW'), 'True')

    def test_a_non_native_cmake_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            stub = Path(directory) / 'cmake.cmd'
            # A stub that reports only Unix Makefiles, like the MSYS build.
            stub.write_text('@echo off\r\necho {"generators": [{"name": "Unix Makefiles"}]}\r\n',
                            encoding='ascii')
            script = (
                "$ErrorActionPreference = 'Stop'\n"
                ". '" + str(TOOLS / 'find_native_cmake.ps1') + "'\n"
                "$stub = '" + str(stub) + "'\n"
                "Write-Host ('NATIVE=' + (Test-NativeCMake -Path $stub))\n"
                "try { Find-NativeCMake -CMake $stub | Out-Null; Write-Host 'REFUSED=False' }\n"
                "catch { Write-Host 'REFUSED=True' }\n")
            result, values = probe(script)
        self.assertEqual(values.get('NATIVE'), 'False')
        self.assertEqual(values.get('REFUSED'), 'True')

    def test_the_repository_tools_do_not_require_a_cmake_argument(self):
        for name in ('setup.ps1', 'prepare_guarded.ps1'):
            text = (TOOLS / name).read_text(encoding='utf-8-sig')
            self.assertNotIn('[Parameter(Mandatory=$true)][string]$CMake', text)
            self.assertIn('Find-NativeCMake', text)


if __name__ == '__main__':
    unittest.main()
