"""Capture DLL unload failure in our own helper using the Windows debugger API."""
import ctypes as c
from ctypes import wintypes as w
import struct, subprocess, sys
k=c.WinDLL('kernel32',use_last_error=True)
k.ReadProcessMemory.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.c_void_p]
k.GetFinalPathNameByHandleW.argtypes=[w.HANDLE,w.LPWSTR,w.DWORD,w.DWORD]
k.OpenThread.argtypes=[w.DWORD,w.BOOL,w.DWORD]; k.OpenThread.restype=w.HANDLE
k.GetThreadContext.argtypes=[w.HANDLE,c.c_void_p]
k.WaitForDebugEvent.argtypes=[c.c_void_p,w.DWORD]
modules=[]
with open(sys.argv[1],'wb') as output:
 p=subprocess.Popen(sys.argv[2:],stdout=output,stderr=output,creationflags=2|0x08000000)
 def read(addr,n):
  b=c.create_string_buffer(n); count=c.c_size_t()
  k.ReadProcessMemory(int(p._handle),addr,b,n,c.byref(count))
  return b.raw[:count.value]
 def name(addr):
  for base,size,path in modules:
   if base<=addr<base+size: return f'{path}+0x{addr-base:x}'
  return hex(addr)
 while True:
  event=c.create_string_buffer(1024)
  if not k.WaitForDebugEvent(event,30000): raise RuntimeError('debug event timeout')
  raw=event.raw; code,pid,tid=struct.unpack_from('<III',raw)
  status=0x10002
  if code in (3,6):
   fh=struct.unpack_from('<Q',raw,16)[0]
   base=struct.unpack_from('<Q',raw,40 if code==3 else 24)[0]
   path=c.create_unicode_buffer(32768)
   k.GetFinalPathNameByHandleW(fh,path,len(path),0)
   head=read(base,4096)
   if len(head)>256:
    pe=struct.unpack_from('<I',head,60)[0]
    size=struct.unpack_from('<I',head,pe+24+56)[0]
    modules.append((base,size,path.value))
  if code==1:
   exc=struct.unpack_from('<I',raw,16)[0]
   addr=struct.unpack_from('<Q',raw,32)[0]
   if exc not in (0x80000003,0x40010006,0x4001000a): status=0x80010001
   if exc in (0xc0000409,0xc0000005):
    print(f'EXCEPTION {exc:08x} at {name(addr)} info={raw[48:80].hex()}',flush=True)
    thread=k.OpenThread(0x1fffff,False,tid)
    ctx=c.create_string_buffer(1250); ptr=(c.addressof(ctx)+15)&~15
    c.c_uint32.from_address(ptr+48).value=0x10001f
    if k.GetThreadContext(thread,ptr):
     rsp=c.c_uint64.from_address(ptr+152).value
     rip=c.c_uint64.from_address(ptr+248).value
     print('RIP',name(rip),'RSP',hex(rsp),flush=True)
     stack=read(rsp,4096-(rsp%4096))
     for off in range(0,len(stack)-7,8):
      value=struct.unpack_from('<Q',stack,off)[0]
      label=name(value)
      if '+0x' in label: print(f'  stack+{off:x}: {label}',flush=True)
    k.CloseHandle(thread)
  if code==5:
   print('DEBUG EXIT',hex(struct.unpack_from('<I',raw,16)[0]),flush=True)
   k.ContinueDebugEvent(pid,tid,status)
   break
  k.ContinueDebugEvent(pid,tid,status)

