import os, struct, subprocess

# Directory of this script (works from any checkout, e.g. /mnt/c/<repo>/drivers/s23aclfix); S23_DRV_DIR overrides it.
DRV_DIR = os.environ.get("S23_DRV_DIR") or os.path.dirname(os.path.abspath(__file__))
OUT_DIR = f"{DRV_DIR}/out"
os.makedirs(OUT_DIR, exist_ok=True)

nt_def = """LIBRARY ntoskrnl.exe
EXPORTS
  RtlInitUnicodeString
  RtlInitializeSid
  RtlSubAuthoritySid
  RtlCreateAcl
  RtlAddAccessAllowedAceEx
  RtlCreateSecurityDescriptor
  RtlSetDaclSecurityDescriptor
  RtlAbsoluteToSelfRelativeSD
  RtlValidSecurityDescriptor
  ZwOpenSymbolicLinkObject
  ZwQuerySymbolicLinkObject
  ZwOpenFile
  ZwCreateFile
  ZwWriteFile
  ZwQueryDirectoryFile
  ZwSetSecurityObject
  ZwFlushBuffersFile
  ZwOpenKey
  ZwSetValueKey
  ZwClose
  ExAllocatePool2
  ExFreePoolWithTag
  KeQueryPerformanceCounter
"""

open(f"{OUT_DIR}/ntoskrnl.def", "w").write(nt_def)
subprocess.check_call(["llvm-dlltool", "-m", "arm64", "-d", f"{OUT_DIR}/ntoskrnl.def", "-l", f"{OUT_DIR}/ntoskrnl.lib"])

cc_cmd = [
    "clang-18",
    "--target=aarch64-pc-windows-msvc",
    "-ffreestanding",
    "-fno-builtin",
    "-fno-stack-protector",
    "-mno-stack-arg-probe",
    "-O2",
    "-Wall", "-Wextra", "-Werror",
    "-c", f"{DRV_DIR}/s23aclfix.c",
    "-o", f"{OUT_DIR}/s23aclfix.obj",
]
print("CC:", " ".join(cc_cmd))
subprocess.check_call(cc_cmd)

sys_path = f"{OUT_DIR}/s23aclfix.sys"
ld_cmd = [
    "lld-link",
    "/NOLOGO",
    "/DRIVER:WDM",
    "/SUBSYSTEM:NATIVE,10.00",
    "/OSVERSION:10.0",
    "/BASE:0x1C0000000",
    "/DYNAMICBASE",
    "/HIGHENTROPYVA",
    "/FIXED:NO",
    "/NXCOMPAT",
    "/TSAWARE:NO",
    "/INCREMENTAL:NO",
    "/ENTRY:DriverEntry",
    "/NODEFAULTLIB",
    "/MACHINE:ARM64",
    "/OPT:REF",
    "/OPT:ICF",
    f"/OUT:{sys_path}",
    f"{OUT_DIR}/s23aclfix.obj",
    f"{OUT_DIR}/ntoskrnl.lib",
]
print("LD:", " ".join(ld_cmd))
subprocess.check_call(ld_cmd)

data = bytearray(open(sys_path, "rb").read())
pe = struct.unpack_from("<I", data, 0x3C)[0]
csum_off = pe + 24 + 64
struct.pack_into("<I", data, csum_off, 0)
s = 0
n = len(data)
if n & 1:
    data.append(0)
for i in range(0, len(data), 2):
    s += data[i] | (data[i + 1] << 8)
    s = (s & 0xFFFF) + (s >> 16)
s = (s & 0xFFFF) + (s >> 16)
csum = (s + n) & 0xFFFFFFFF
struct.pack_into("<I", data, csum_off, csum)
open(sys_path, "wb").write(data[:n])

machine = struct.unpack_from("<H", data, pe + 4)[0]
reloc_rva, reloc_sz = struct.unpack_from("<2I", data, pe + 24 + 112 + 5 * 8)
print(f"BUILT s23aclfix.sys: {n} bytes, Machine=0x{machine:04X}, Reloc=0x{reloc_rva:X}+{reloc_sz}, CheckSum=0x{csum:08X}")
assert machine == 0xAA64 and reloc_sz > 0, "Invalid ARM64 PE or missing .reloc!"
