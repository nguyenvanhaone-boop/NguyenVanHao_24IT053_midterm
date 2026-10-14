#!/usr/bin/env python3
"""Black-box checks against the assignment contract, not all GNU ls defaults."""
import errno
import os
from pathlib import Path
import pty
import select
import socket
import stat
import subprocess
import tempfile
import time
import unittest

BINARY = str(Path(os.environ.get('MYLS_BINARY', './myls')).resolve())

class MylsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='myls-test-')
        self.root = Path(self.temp.name)
        (self.root / 'a').write_bytes(b'a')
        (self.root / 'z').write_bytes(b'z' * 5000)
        (self.root / '.hidden').touch()
        (self.root / 'sub').mkdir()
        (self.root / 'sub' / 'child').touch()
        self.env = {**os.environ, 'LC_ALL': 'C', 'TZ': 'UTC'}
        self.env.pop('BLOCKSIZE', None)

    def tearDown(self):
        self.temp.cleanup()

    def run_ls(self, *args, env=None):
        result = subprocess.run([BINARY, *args], cwd=self.root,
                                env={**self.env, **(env or {})},
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=8)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        self.assertEqual(result.stderr, b'')
        return result.stdout.decode()

    def test_default_name_sort(self):
        expected = ['a', 'sub', 'z']
        if os.geteuid() == 0: expected.insert(0, '.hidden')
        self.assertEqual(self.run_ls().splitlines(), expected)
        self.assertEqual(self.run_ls('-r').splitlines(), expected[::-1])

    def test_hidden_and_combined_flags(self):
        self.assertEqual(self.run_ls('-A').splitlines(), ['.hidden', 'a', 'sub', 'z'])
        for option in ['-a', '-aA', '-Aa']:
            self.assertEqual(self.run_ls(option).splitlines(), ['.', '..', '.hidden', 'a', 'sub', 'z'])

    def test_no_sort_does_not_imply_all(self):
        raw = [x.name for x in os.scandir(self.root) if not x.name.startswith('.') or os.geteuid() == 0]
        self.assertEqual(self.run_ls('-f').splitlines(), raw)
        self.assertEqual(self.run_ls('-frSt').splitlines(), raw)

    def test_operand_paths_and_order(self):
        self.assertEqual(self.run_ls('z', './a'), './a\nz\n')
        self.assertEqual(self.run_ls('-d', 'sub/', './a'), './a\nsub/\n')
        (self.root / 'adir').mkdir()
        self.assertEqual(self.run_ls('sub', 'z', 'adir', 'a'),
                         'a\nz\n\nadir:\n\nsub:\nchild\n')

    def test_size_sort_reverse_and_ties(self):
        (self.root / 'b').write_bytes(b'b')
        self.assertEqual(self.run_ls('-S', 'a', 'b', 'z'), 'z\na\nb\n')
        self.assertEqual(self.run_ls('-Sr', 'a', 'b', 'z'), 'b\na\nz\n')

    def test_time_sort_nanoseconds_and_access(self):
        os.utime(self.root / 'a', ns=(1600000100000000000, 1600000000000000001))
        os.utime(self.root / 'z', ns=(1600000000000000000, 1600000000000000002))
        self.assertEqual(self.run_ls('-t', 'a', 'z'), 'z\na\n')
        self.assertEqual(self.run_ls('-tu', 'a', 'z'), 'a\nz\n')
        self.assertEqual(self.run_ls('-tr', 'a', 'z'), 'a\nz\n')
        self.assertEqual(self.run_ls('-tS', 'a', 'z'), 'z\na\n')
        self.assertEqual(self.run_ls('-Stu', 'a', 'z'), 'a\nz\n')

    def test_long_fields_and_option_order(self):
        fields = self.run_ls('-ln', 'a').split()
        s = (self.root / 'a').stat()
        self.assertEqual(fields[0], stat.filemode(s.st_mode))
        self.assertEqual(fields[1:5], [str(s.st_nlink), str(s.st_uid), str(s.st_gid), '1'])
        import pwd
        self.assertEqual(self.run_ls('-nl', 'a').split()[2], pwd.getpwuid(s.st_uid).pw_name)

    def test_selected_times_and_timezone(self):
        os.utime(self.root / 'a', (1600000000, 1700000000))
        s = (self.root / 'a').stat()
        for opt, stamp in [('-l', s.st_mtime), ('-lu', s.st_atime), ('-lc', s.st_ctime),
                           ('-lcu', s.st_atime), ('-luc', s.st_ctime)]:
            expected = time.strftime('%b %e %H:%M', time.gmtime(stamp)).split()
            self.assertEqual(self.run_ls(opt, 'a').split()[5:8], expected)
        expected = time.strftime('%b %e %H:%M', time.gmtime(s.st_mtime - 5 * 3600)).split()
        self.assertEqual(self.run_ls('-l', 'a', env={'TZ':'EST5'}).split()[5:8], expected)

    def test_inode(self):
        self.assertEqual(self.run_ls('-i', 'a').split(), [str((self.root / 'a').stat().st_ino), 'a'])

    def test_blocks_environment_and_rounding(self):
        allocated = (self.root / 'z').stat().st_blocks * 512
        for value, unit in [('512',512), ('1024',1024), ('3K',3072), ('K',1024), ('1M',1048576)]:
            self.assertEqual(self.run_ls('-s', 'z', env={'BLOCKSIZE':value}).split()[0],
                             str((allocated + unit - 1)//unit))
        self.assertEqual(self.run_ls('-sk', 'z', env={'BLOCKSIZE':'3K'}).split()[0], str((allocated+1023)//1024))
        self.assertFalse(self.run_ls('-s').startswith('total'))

    def test_human_and_units_order(self):
        self.assertEqual(self.run_ls('-lh', 'z').split()[4], '4.9K')
        self.assertEqual(self.run_ls('-lhk', 'z').split()[4], '5000')
        self.assertEqual(self.run_ls('-lkh', 'z').split()[4], '4.9K')
        self.assertTrue(self.run_ls('-lh').splitlines()[0].startswith('total '))
        self.assertTrue(self.run_ls('-lh').splitlines()[0].endswith('K'))

    def test_sparse_file_allocation(self):
        with (self.root / 'sparse').open('wb') as f: f.truncate(10 * 1024 * 1024)
        fields = self.run_ls('-sn', 'sparse').split()
        self.assertEqual(int(fields[0]), (self.root / 'sparse').stat().st_blocks)
        self.assertEqual(int(fields[5]), 10 * 1024 * 1024)

    def test_links_and_recursion(self):
        (self.root / 'link').symlink_to('sub')
        (self.root / 'broken').symlink_to('missing')
        (self.root / 'sub' / 'back').symlink_to('..')
        self.assertEqual(self.run_ls('link'), 'back\nchild\n')
        self.assertEqual(self.run_ls('-d', 'link'), 'link\n')
        self.assertIn('link -> sub', self.run_ls('-l', 'link'))
        self.assertIn('broken -> missing', self.run_ls('-l', 'broken'))
        recursive = self.run_ls('-Ra')
        self.assertEqual(recursive.count('./sub:'), 1)
        self.assertNotIn('./sub/back:', recursive)
        self.assertNotIn('./link:', recursive)
        self.assertEqual(self.run_ls('-Rd', 'sub'), 'sub\n')
        self.assertTrue(self.run_ls('-dR', 'sub').startswith('sub:\n'))

    def test_classification(self):
        (self.root / 'a').chmod(0o755)
        (self.root / 'link').symlink_to('a')
        os.mkfifo(self.root / 'pipe')
        with socket.socket(socket.AF_UNIX) as s:
            s.bind(str(self.root / 'sock'))
            self.assertEqual(self.run_ls('-F', 'a', 'link', 'pipe', 'sock', '-d', 'sub'),
                             'a*\nlink@\npipe|\nsock=\nsub/\n')

    def test_special_permission_bits(self):
        for mode, text in [(0o4755, '-rwsr-xr-x'), (0o4644, '-rwSr--r--'),
                           (0o2755, '-rwxr-sr-x'), (0o2644, '-rw-r-Sr--'),
                           (0o1755, '-rwxr-xr-t'), (0o1644, '-rw-r--r-T')]:
            (self.root / 'a').chmod(mode)
            self.assertEqual(self.run_ls('-l', 'a').split()[0], text)

    def test_nonprintable_and_last_option(self):
        name = 'bad\nname\t'
        (self.root / name).touch()
        self.assertEqual(self.run_ls('-q', name), 'bad?name?\n')
        self.assertEqual(self.run_ls('-wq', name), 'bad?name?\n')
        self.assertEqual(self.run_ls('-qw', name), name+'\n')
        self.assertEqual(self.run_ls(name), name+'\n')
        (self.root / 'link').symlink_to(name)
        self.assertTrue(self.run_ls('-lq', 'link').endswith('link -> bad?name?\n'))

    def test_terminal_defaults(self):
        (self.root / 'bad\tname').touch()
        master, slave = pty.openpty()
        try:
            proc = subprocess.Popen([BINARY, '-s'], cwd=self.root, env=self.env,
                                    stdout=slave, stderr=subprocess.PIPE)
            os.close(slave)
            output = b''
            while True:
                ready, _, _ = select.select([master], [], [], 8)
                self.assertTrue(ready, 'terminal output timed out')
                try: chunk = os.read(master, 65536)
                except OSError as e:
                    if e.errno == errno.EIO: break
                    raise
                if not chunk: break
                output += chunk
            self.assertEqual(proc.wait(timeout=8), 0)
            self.assertTrue(output.startswith(b'total '))
            self.assertIn(b'bad?name', output)
            proc.stderr.close()
        finally: os.close(master)

    def test_device_major_minor(self):
        fields = self.run_ls('-n', '/dev/null').split()
        device = os.stat('/dev/null').st_rdev
        self.assertEqual(fields[0][0], 'c')
        self.assertEqual(fields[4], f'{os.major(device)},{os.minor(device)}')

    def test_empty_directory_and_invalid_option(self):
        (self.root / 'empty').mkdir()
        self.assertEqual(self.run_ls('empty'), '')
        self.assertEqual(self.run_ls('-l', 'empty'), 'total 0\n')
        result = subprocess.run([BINARY, '-Z'], capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn(b'usage:', result.stderr)

    def test_missing_operand_continues(self):
        result = subprocess.run([BINARY, 'missing', 'a'], cwd=self.root, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b'a\n')
        self.assertIn(b'missing', result.stderr)

    @unittest.skipIf(os.geteuid() == 0, 'root bypasses permission checks')
    def test_recursive_permission_failure(self):
        restricted = self.root / 'sub' / 'denied'
        restricted.mkdir()
        restricted.chmod(0)
        try:
            result = subprocess.run([BINARY, '-R'], cwd=self.root, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b'denied', result.stderr)
            self.assertIn(b'child', result.stdout)
        finally: restricted.chmod(0o700)

    def test_dash_operand_and_large_directory(self):
        (self.root / '-l').touch()
        self.assertEqual(self.run_ls('--', '-l'), '-l\n')
        for i in range(130): (self.root / f'f{i:03}').touch()
        output = self.run_ls('-A').splitlines()
        self.assertEqual(len(output), 135)
        self.assertEqual(output, sorted(output))

    @unittest.skipUnless(Path('/dev/full').exists(), '/dev/full requires Linux')
    def test_output_failure(self):
        with open('/dev/full', 'wb') as out:
            result = subprocess.run([BINARY], cwd=self.root, stdout=out, stderr=subprocess.PIPE)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'standard output', result.stderr)

if __name__ == '__main__': unittest.main(verbosity=2)
