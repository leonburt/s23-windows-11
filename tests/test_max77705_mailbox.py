"""usb_research/max77705_mailbox.py: OPCODE mailbox framing and 0x25 status-register bit fields
as specified in usb_research/S23_USBC_ARCHITECTURE.md (pure byte logic, no hardware)."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "usb_research"))
import max77705_mailbox as mb  # noqa: E402


class MailboxFramingTest(unittest.TestCase):
    def test_aperture_constants_match_spec(self):
        self.assertEqual((mb.REG_OPCODE, mb.REG_END_DATA, mb.REG_OPCODE_READ, mb.REG_OPCODE_READ_END),
                         (0x21, 0x41, 0x51, 0x71))
        self.assertEqual(mb.MAX_WRITE_DATA, 32)   # 0x22..0x41
        self.assertEqual(mb.MAX_READ_DATA, 32)    # 0x52..0x71

    def test_short_write_gets_commit_write(self):
        # OPCODE_READ_SBU (0x25): write [0x01] (len 1)
        self.assertEqual(mb.encode_write(0x25, b"\x01"), [(0x21, b"\x25\x01"), (0x41, b"\x00")])

    def test_opcode_only_write(self):
        self.assertEqual(mb.encode_write(0x55), [(0x21, b"\x55"), (0x41, b"\x00")])

    def test_write_reaching_end_register_needs_no_commit(self):
        data = bytes(range(32))
        w = mb.encode_write(0x30, data)
        self.assertEqual(w, [(0x21, b"\x30" + data)])
        self.assertEqual(0x21 + len(w[0][1]) - 1, 0x41)

    def test_write_one_short_of_end_still_commits(self):
        w = mb.encode_write(0x30, bytes(31))
        self.assertEqual(len(w), 2)
        self.assertEqual(w[1][0], 0x41)

    def test_commit_byte_is_configurable(self):
        # the spec leaves the exact commit byte to be confirmed against Samsung's driver
        self.assertEqual(mb.encode_write(0x56, b"\x07", commit_byte=0xFF)[1], (0x41, b"\xff"))

    def test_invalid_writes(self):
        for args in [(0x100,), (-1,), (0x30, bytes(33))]:
            with self.assertRaises(mb.MailboxError):
                mb.encode_write(*args)
        with self.assertRaises(mb.MailboxError):
            mb.encode_write(0x30, b"", commit_byte=256)

    def test_read_request_and_response(self):
        self.assertEqual(mb.read_request(2), (0x51, 3))
        self.assertEqual(mb.decode_response(b"\x25\x10\x20", 0x25, 2), b"\x10\x20")   # sbu1, sbu2
        self.assertEqual(mb.decode_response(b"\x55", 0x55, 0), b"")
        self.assertEqual(mb.read_request(32), (0x51, 33))
        with self.assertRaises(mb.MailboxError):
            mb.read_request(33)

    def test_response_validation(self):
        with self.assertRaises(mb.MailboxError):
            mb.decode_response(b"\x24\x10\x20", 0x25, 2)      # wrong echo
        with self.assertRaises(mb.MailboxError):
            mb.decode_response(b"\x25\x10", 0x25, 2)          # truncated
        with self.assertRaises(mb.MailboxError):
            mb.decode_response(b"\x25\x10\x20\x30", 0x25, 2)  # too long

    def test_response_ready_bit(self):
        self.assertTrue(mb.response_ready(0x80))
        self.assertTrue(mb.response_ready(0xFF))
        self.assertFalse(mb.response_ready(0x7F))


class StatusRegisterTest(unittest.TestCase):
    def test_cc_status0_fields(self):
        self.assertEqual(mb.decode_cc_status0(0b10_11_1_101),
                         {"CCPinStat": 2, "CCIStat": 3, "CCVcnStat": 1, "CCStat": 5})
        self.assertEqual(mb.decode_cc_status0(0), {"CCPinStat": 0, "CCIStat": 0, "CCVcnStat": 0, "CCStat": 0})

    def test_cc_status1_fields(self):
        d = mb.decode_cc_status1(0b01_1_0_1_0_1_1)
        self.assertEqual(d, {"CCSBUSHORT": 1, "VCONNOCP": 1, "VCONNSC": 0, "VSAFE0V": 1,
                             "AttachSrcErr": 0, "ConnStat": 1, "Altmode": 1})

    def test_bc_status_fields(self):
        self.assertEqual(mb.decode_bc_status(0b1_0_101_1_10), {"VBUSDet": 1, "PrChgTyp": 5, "DCDTmo": 1, "ChgTyp": 2})

    def test_pd_status1_fields(self):
        self.assertEqual(mb.decode_pd_status1(0b1_00_1_1010), {"PD_DataRole": 1, "PD_PSRDY": 1, "FCT_ID": 0xA})

    def test_every_byte_round_trips_through_its_fields(self):
        for v in range(256):
            c = mb.decode_cc_status0(v)
            self.assertEqual((c["CCPinStat"] << 6) | (c["CCIStat"] << 4) | (c["CCVcnStat"] << 3) | c["CCStat"], v)
            b = mb.decode_bc_status(v)
            self.assertEqual((b["VBUSDet"] << 7) | (b["PrChgTyp"] << 3) | (b["DCDTmo"] << 2) | b["ChgTyp"], v & 0xBF)

    def test_out_of_range_register_value(self):
        for f in (mb.decode_cc_status0, mb.decode_cc_status1, mb.decode_bc_status, mb.decode_pd_status1):
            with self.assertRaises(mb.MailboxError):
                f(256)


if __name__ == "__main__":
    unittest.main()
