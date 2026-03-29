import sys

with open('guideXOSServer.vcxproj.filters', 'r', encoding='utf-8') as f:
    content = f.read()

# Add new header file entries before the closing </ItemGroup> of the ClInclude section
old_h = '    <ClInclude Include="kernel\\core\\include\\kernel\\fb_console.h">\n      <Filter>Header Files</Filter>\n    </ClInclude>\n  </ItemGroup>'

new_headers = [
    'kernel\\core\\include\\kernel\\usb_audio_uac2.h',
    'kernel\\core\\include\\kernel\\pci_audio.h',
    'kernel\\core\\include\\kernel\\usb_video_uvc.h',
    'kernel\\arch\\amd64\\include\\arch\\pci_audio.h',
    'kernel\\arch\\x86\\include\\arch\\pci_audio.h',
    'kernel\\arch\\ia64\\include\\arch\\pci_audio.h',
    'kernel\\arch\\sparc\\include\\arch\\pci_audio.h',
    'kernel\\arch\\sparc64\\include\\arch\\pci_audio.h',
]

new_h = '    <ClInclude Include="kernel\\core\\include\\kernel\\fb_console.h">\n      <Filter>Header Files</Filter>\n    </ClInclude>\n'
for hdr in new_headers:
    new_h += '    <ClInclude Include="' + hdr + '">\n      <Filter>Header Files</Filter>\n    </ClInclude>\n'
new_h += '  </ItemGroup>'

if old_h in content:
    content = content.replace(old_h, new_h, 1)
    print('Headers replaced OK')
else:
    print('ERROR: Could not find header insertion point')
    sys.exit(1)

# Add new source file entries
old_s = '    <ClCompile Include="kernel\\core\\fb_console.cpp">\n      <Filter>Source Files</Filter>\n    </ClCompile>\n  </ItemGroup>\n  <ItemGroup>'

new_sources = [
    'kernel\\core\\usb_audio_uac2.cpp',
    'kernel\\core\\pci_audio.cpp',
    'kernel\\core\\usb_video_uvc.cpp',
    'kernel\\arch\\amd64\\pci_audio.cpp',
    'kernel\\arch\\x86\\pci_audio.cpp',
    'kernel\\arch\\ia64\\pci_audio.cpp',
    'kernel\\arch\\sparc\\pci_audio.cpp',
    'kernel\\arch\\sparc64\\pci_audio.cpp',
]

new_s = '    <ClCompile Include="kernel\\core\\fb_console.cpp">\n      <Filter>Source Files</Filter>\n    </ClCompile>\n'
for src in new_sources:
    new_s += '    <ClCompile Include="' + src + '">\n      <Filter>Source Files</Filter>\n    </ClCompile>\n'
new_s += '  </ItemGroup>\n  <ItemGroup>'

if old_s in content:
    content = content.replace(old_s, new_s, 1)
    print('Sources replaced OK')
else:
    print('ERROR: Could not find source insertion point')
    sys.exit(1)

with open('guideXOSServer.vcxproj.filters', 'w', encoding='utf-8') as f:
    f.write(content)

print('Done - filters file updated')
