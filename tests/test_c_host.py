"""Runs the host C tests in tests/c (s23gpu VA allocator, msm_validate.h, CreateAllocation range check)
when a C compiler is available; skipped otherwise. CI runs them separately with ASan/UBSan."""
import os
import subprocess
import sys
import unittest

RUNNER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "c", "run_c_tests.py")


class HostCTests(unittest.TestCase):
    def test_c_suite(self):
        p = subprocess.run([sys.executable, RUNNER], capture_output=True, text=True)
        if p.returncode == 77:
            self.skipTest("no C compiler found")
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertIn("3/3 passed", p.stdout)


if __name__ == "__main__":
    unittest.main()
