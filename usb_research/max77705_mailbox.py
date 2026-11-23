"""Reference encoder/decoder for the MAX77705 USBC/PDIC OPCODE mailbox (I2C slave 0x25).

Pure byte-level logic derived from S23_USBC_ARCHITECTURE.md (section 2, "AP-to-MCU OPCODE Mailbox
Protocol" and the 0x25 register map). It performs no I/O and is not used by any driver; it exists so a
future s23usbc/s23pmic implementation can be checked against host unit tests
(tests/test_max77705_mailbox.py).

Only what the specification states is encoded here:
  * write:  [opcode, data...] starting at REG_OPCODE (0x21), aperture 0x21..0x41 inclusive;
            if the write does not reach 0x41, a separate write of REG_END_DATA (0x41) commits it.
            The value of that commit byte is NOT confirmed by the spec (it says to verify 0x00 against
            Samsung's max77705_usbc_opcode_write()), so it is a parameter with default 0x00.
  * read:   1 + read_length bytes from REG_OPCODE_READ (0x51), byte 0 echoes the opcode,
            response buffer 0x51..0x71 inclusive.
  * status registers: bit fields only. The meaning of individual field values (e.g. which CCStat value
    is UFP vs DFP) is not given by the spec and is therefore returned raw, not interpreted.
"""

SLAVE_ADDR = 0x25
REG_UIC_INT = 0x02
REG_CC_INT = 0x03
REG_PD_INT = 0x04
REG_BC_STATUS = 0x08
REG_CC_STATUS0 = 0x0A
REG_CC_STATUS1 = 0x0B
REG_PD_STATUS1 = 0x0D
REG_OPCODE = 0x21          # opcode byte, data follows at 0x22
REG_END_DATA = 0x41        # end of the write aperture / commit register
REG_OPCODE_READ = 0x51     # response: echoed opcode, payload follows
REG_OPCODE_READ_END = 0x71

MAX_WRITE_DATA = REG_END_DATA - REG_OPCODE                 # 32 data bytes (0x22..0x41)
MAX_READ_DATA = REG_OPCODE_READ_END - REG_OPCODE_READ       # 32 payload bytes (0x52..0x71)

UIC_INT_APCMDRESI = 1 << 7  # OPCODE response ready


class MailboxError(ValueError):
    pass


def encode_write(opcode, data=b"", commit_byte=0x00):
    """-> list of (register, bytes) I2C block writes to slave 0x25, in order."""
    if not 0 <= opcode <= 0xFF:
        raise MailboxError("opcode must be one byte")
    data = bytes(data)
    if len(data) > MAX_WRITE_DATA:
        raise MailboxError(f"payload of {len(data)} bytes exceeds the {MAX_WRITE_DATA}-byte mailbox")
    if not 0 <= commit_byte <= 0xFF:
        raise MailboxError("commit byte must be one byte")
    frame = bytes([opcode]) + data
    writes = [(REG_OPCODE, frame)]
    last_reg = REG_OPCODE + len(frame) - 1
    if last_reg < REG_END_DATA:
        writes.append((REG_END_DATA, bytes([commit_byte])))
    return writes


def read_request(read_length):
    """-> (register, byte count) of the bulk read that fetches a response with read_length payload bytes."""
    if not 0 <= read_length <= MAX_READ_DATA:
        raise MailboxError(f"read length must be 0..{MAX_READ_DATA}")
    return REG_OPCODE_READ, 1 + read_length


def decode_response(raw, opcode, read_length):
    """Validate a raw response read from 0x51 and return its payload."""
    raw = bytes(raw)
    _, expected = read_request(read_length)
    if len(raw) != expected:
        raise MailboxError(f"expected {expected} bytes, got {len(raw)}")
    if raw[0] != opcode:
        raise MailboxError(f"response echoes opcode 0x{raw[0]:02X}, expected 0x{opcode:02X}")
    return raw[1:]


def response_ready(uic_int):
    """REG_UIC_INT (0x02) bit 7 APCmdResI: an OPCODE response can be read."""
    return bool(uic_int & UIC_INT_APCMDRESI)


def _byte(v):
    if not 0 <= v <= 0xFF:
        raise MailboxError("register value must be one byte")
    return v


def decode_cc_status0(v):
    """REG_CC_STATUS0 (0x0A): CCPinStat[7:6], CCIStat[5:4], CCVcnStat[3], CCStat[2:0] (raw values)."""
    v = _byte(v)
    return {"CCPinStat": (v >> 6) & 3, "CCIStat": (v >> 4) & 3, "CCVcnStat": (v >> 3) & 1, "CCStat": v & 7}


def decode_cc_status1(v):
    """REG_CC_STATUS1 (0x0B): CCSBUSHORT[7:6], VCONNOCP[5], VCONNSC[4], VSAFE0V[3], AttachSrcErr[2],
    ConnStat[1], Altmode[0]."""
    v = _byte(v)
    return {"CCSBUSHORT": (v >> 6) & 3, "VCONNOCP": (v >> 5) & 1, "VCONNSC": (v >> 4) & 1, "VSAFE0V": (v >> 3) & 1,
            "AttachSrcErr": (v >> 2) & 1, "ConnStat": (v >> 1) & 1, "Altmode": v & 1}


def decode_bc_status(v):
    """REG_BC_STATUS (0x08): VBUSDet[7], PrChgTyp[5:3], DCDTmo[2], ChgTyp[1:0] (raw values)."""
    v = _byte(v)
    return {"VBUSDet": (v >> 7) & 1, "PrChgTyp": (v >> 3) & 7, "DCDTmo": (v >> 2) & 1, "ChgTyp": v & 3}


def decode_pd_status1(v):
    """REG_PD_STATUS1 (0x0D): PD_DataRole[7], PD_PSRDY[4], FCT_ID[3:0]."""
    v = _byte(v)
    return {"PD_DataRole": (v >> 7) & 1, "PD_PSRDY": (v >> 4) & 1, "FCT_ID": v & 0xF}
