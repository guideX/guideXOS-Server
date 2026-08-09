# Inherited Outcome E fault evidence

Source artifact: `out/dotnet/gc-first-non-null-root-callback-boundary/build/artifact/NativeAotGcSingleThreadSuspendEe.exe`.

The regenerated thread-static proof image has a different layout; the old
`inherited-fault-disassembly.txt` generated during the new build is therefore
not used for this analysis.

Map symbols:

```text
S_P_CoreLib_Internal_Runtime_ThreadStatics__GetInlinedThreadStaticBaseSlow 0x1008e270
RhpGetModuleSection 0x1000a4f0
S_P_TypeLoader_Internal_Runtime_TypeLoader_TypeLoaderEnvironment__GetThreadStaticGCDescForDynamicType 0x10001548
RhNewObject 0x10084f60
RhRegisterInlinedThreadStaticRoot 0x1000a030
RhpCheckedAssignRef 0x1000b260
__GCSTATICS@S_P_CoreLib_Internal_Runtime_Augments_RuntimeAugments@@ 0x1011c260
```

Relevant disassembly:

```text
1008e270: 57                    push   rdi
1008e271: 56                    push   rsi
1008e272: 53                    push   rbx
1008e273: 48 83 ec 30           sub    rsp,0x30
1008e277: 48 8b d9              mov    rbx,rcx
1008e27a: 48 8d 0d 37 6f 03 00  lea    rcx,[rip+0x36f37]        # 0x100c51b8
1008e281: e8 0a 08 00 00        call   0x1008ea90
1008e286: 48 8b f0              mov    rsi,rax
1008e289: 48 89 74 24 20        mov    QWORD PTR [rsp+0x20],rsi
1008e28e: 48 8d 4c 24 20        lea    rcx,[rsp+0x20]
1008e293: 4c 8d 44 24 28        lea    r8,[rsp+0x28]
1008e298: ba ca 00 00 00        mov    edx,0xca
1008e29d: e8 4e c2 f7 ff        call   0x1000a4f0
1008e2a2: 8b 54 24 28           mov    edx,DWORD PTR [rsp+0x28]
1008e2a6: 8b ca                 mov    ecx,edx
1008e2a8: c1 f9 1f              sar    ecx,0x1f
1008e2ab: 83 e1 07              and    ecx,0x7
1008e2ae: 03 d1                 add    edx,ecx
1008e2b0: c1 fa 03              sar    edx,0x3
1008e2b3: 85 d2                 test   edx,edx
1008e2b5: 7f 1e                 jg     0x1008e2d5
1008e2b7: 48 8b 15 a2 df 08 00  mov    rdx,QWORD PTR [rip+0x8dfa2]        # 0x1011c260
1008e2be: 48 8b 52 10           mov    rdx,QWORD PTR [rdx+0x10]
1008e2c2: 38 12                 cmp    BYTE PTR [rdx],dl
1008e2c4: 48 8b d6              mov    rdx,rsi
1008e2c7: 33 c9                 xor    ecx,ecx
1008e2c9: 45 33 c0              xor    r8d,r8d
1008e2cc: 39 09                 cmp    DWORD PTR [rcx],ecx
1008e2ce: e8 75 32 f7 ff        call   0x10001548
1008e2d3: eb 03                 jmp    0x1008e2d8
1008e2d5: 48 8b 00              mov    rax,QWORD PTR [rax]
1008e2d8: 48 8b c8              mov    rcx,rax
1008e2db: e8 80 6c ff ff        call   0x10084f60
1008e2e0: 48 8b f8              mov    rdi,rax
1008e2e3: 48 8b cb              mov    rcx,rbx
1008e2e6: 48 8b d6              mov    rdx,rsi
1008e2e9: e8 42 bd f7 ff        call   0x1000a030
1008e2ee: 48 8b cb              mov    rcx,rbx
1008e2f1: 48 8b d7              mov    rdx,rdi
1008e2f4: e8 67 cf f7 ff        call   0x1000b260
1008e2f9: 65 48 8b 0c 25 58 00  mov    rcx,QWORD PTR gs:0x58
1008e300: 00 00
1008e302: 8b 15 e4 49 1a 00     mov    edx,DWORD PTR [rip+0x1a49e4]        # 0x10232cec
1008e308: b8 10 00 00 00        mov    eax,0x10
1008e30d: 48 03 04 d1           add    rax,QWORD PTR [rcx+rdx*8]
1008e311: 48 8b 18              mov    rbx,QWORD PTR [rax]
```

QEMU state at the fault (the other two inherited boots matched):

```text
exception: #PF, not-present read, kernel CPL 0
RIP=0x000000001008E2BE  CR2=0x00000000FFFB5FF9
RDX=0x00000000FFFB5FE9  RAX=0x0000000000000000
RBX=0x000000000392CBE0  RCX=0x0000000000000000
RSI=0x0000000000000000  RDI=0x0000000000000000
RSP=0x0000000004E68B30  RBP=0x0000000004E68C10
GS base=0x000000000392CCE0
```

The exact instruction is four bytes, `48 8B 52 10`, decoded as
`mov rdx,QWORD PTR [rdx+0x10]`; its memory operand is 8 bytes wide. The
preceding helper call is `RhpGetModuleSection` at `0x1000a4f0`. The returned
section length made `edx/8 <= 0`, selecting the dynamic-type branch. The
effective address is:

```text
[0x1011c260] = 0x00000000FFFB5FE9
0x00000000FFFB5FE9 + 0x10 = 0x00000000FFFB5FF9 = CR2
```

This is not a TLS/FLS base, module TLS offset, thread-static index, or
Thread-relative field. The cell at `0x1011c260` held an unrehydrated
NativeAOT `RuntimeAugments` GC-static placeholder. The missing contract was
the generated `InitializeModules` startup publication boundary.
