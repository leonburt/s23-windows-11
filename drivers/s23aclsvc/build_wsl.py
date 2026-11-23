import os, struct, subprocess

# Directory of this script (works from any checkout, e.g. /mnt/c/<repo>/drivers/s23aclsvc); S23_DRV_DIR overrides it.
DRV_DIR = os.environ.get("S23_DRV_DIR") or os.path.dirname(os.path.abspath(__file__))
OUT_DIR = f"{DRV_DIR}/out"
os.makedirs(OUT_DIR, exist_ok=True)

defs = {
    "kernel32": """LIBRARY kernel32.dll
EXPORTS
  CreateFileW
  WriteFile
  FlushFileBuffers
  CloseHandle
  CreateProcessW
  WaitForSingleObject
  GetExitCodeProcess
  GetLastError
  GetTickCount
  CreateThread
  GetCurrentProcess
  Sleep
  ExitProcess
""",
    "advapi32": """LIBRARY advapi32.dll
EXPORTS
  StartServiceCtrlDispatcherW
  RegisterServiceCtrlHandlerW
  SetServiceStatus
  RegOpenKeyExW
  RegSetValueExW
  RegFlushKey
  RegCloseKey
  OpenProcessToken
  AdjustTokenPrivileges
""",
    "ntdll": """LIBRARY ntdll.dll
EXPORTS
  NtShutdownSystem
""",
}

libs = []
for nm, body in defs.items():
    dp = f"{OUT_DIR}/{nm}.def"
    lp = f"{OUT_DIR}/{nm}.lib"
    open(dp, "w").write(body)
    subprocess.check_call(["llvm-dlltool", "-m", "arm64", "-d", dp, "-l", lp])
    libs.append(lp)

obj = f"{OUT_DIR}/s23aclsvc.obj"
exe = f"{OUT_DIR}/s23aclsvc.exe"
subprocess.check_call([
    "clang-18",
    "--target=aarch64-pc-windows-msvc",
    "-ffreestanding",
    "-fno-builtin",
    "-fno-stack-protector",
    "-mno-stack-arg-probe",
    "-O2",
    "-Wall", "-Wextra", "-Werror",
    "-c", f"{DRV_DIR}/s23aclsvc.c",
    "-o", obj,
])
subprocess.check_call([
    "lld-link",
    "/NOLOGO",
    "/SUBSYSTEM:CONSOLE,10.00",
    "/OSVERSION:10.0",
    "/DYNAMICBASE",
    "/HIGHENTROPYVA",
    "/NXCOMPAT",
    "/INCREMENTAL:NO",
    "/NODEFAULTLIB",
    "/MACHINE:ARM64",
    "/ENTRY:S23Entry",
    "/OPT:REF",
    "/OPT:ICF",
    f"/OUT:{exe}",
    obj,
    *libs,
])

data = open(exe, "rb").read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
mach = struct.unpack_from("<H", data, pe + 4)[0]
reloc_rva, reloc_sz = struct.unpack_from("<2I", data, pe + 24 + 112 + 5 * 8)
print(f"BUILT s23aclsvc.exe: {len(data)} B, Machine=0x{mach:04X}, Reloc=0x{reloc_rva:X}+{reloc_sz}")
assert mach == 0xAA64 and reloc_sz > 0
