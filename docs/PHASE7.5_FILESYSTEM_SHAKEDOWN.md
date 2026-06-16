================================================================================
Phase 7.5: Filesystem Shakedown Plan
================================================================================

Status: ?? PLANNING
Priority: PREREQUISITE for Phase 8 (Universal Binary System)
Estimated Duration: 2-4 weeks

================================================================================
EXECUTIVE SUMMARY
================================================================================

Before implementing the Universal Binary System (Phase 8), we need to verify
that the filesystem infrastructure is complete, tested, and reliable. This
phase focuses on validating existing code, filling gaps, and establishing
a testing methodology.

Key Question: Can we reliably open("/apps/calculator.gxapp") and read its
contents into memory?

Answer: Not yet. While significant infrastructure exists, key pieces are
missing or untested.

================================================================================
CURRENT STATE ASSESSMENT
================================================================================

WHAT EXISTS (Implemented):
--------------------------

| Component              | File                      | Status           |
|------------------------|---------------------------|------------------|
| Block Device Layer     | block_device.cpp          | ? Complete      |
| RAM Disk Driver        | ramdisk.cpp               | ? Complete      |
| VFS Layer              | vfs.cpp                   | ?? Partial       |
| FAT32 Driver           | fs_fat.cpp                | ?? Partial       |
| ext2/ext4 Driver       | fs_ext4.cpp               | ?? Partial       |
| NTFS Driver            | fs_ntfs.cpp               | ?? Read-only     |
| UFS Driver             | fs_ufs.cpp                | ?? Read-only     |
| XFS Driver             | fs_xfs.cpp                | ?? Read-only     |
| ATA Driver             | ata.cpp                   | ? Complete      |
| NVMe Driver            | nvme.cpp                  | ? Complete      |
| USB Storage Driver     | usb_storage.cpp           | ? Complete      |
| VirtIO Block Driver    | virtio_block.cpp          | ? Complete      |

VFS Features Present:
- Mount point management
- Path normalization
- Filesystem type detection
- File handle table
- Basic open/close/read structure

WHAT'S MISSING OR INCOMPLETE:
-----------------------------

| Gap                           | Impact                            | Priority |
|-------------------------------|-----------------------------------|----------|
| Path-based file open          | Can't open("/path/to/file")       | CRITICAL |
| Directory traversal           | Can't navigate subdirectories     | CRITICAL |
| Write support (FAT32)         | Can't create/modify files         | HIGH     |
| File creation                 | Can't create new files            | HIGH     |
| Directory creation            | Can't mkdir                       | HIGH     |
| Disk formatting               | Can't format new disks            | MEDIUM   |
| Partition table parsing       | Can't auto-detect partitions      | MEDIUM   |
| Partition creation            | Can't partition new disks         | LOW      |
| HD Installer (legacy port)    | Can't install OS to disk          | DEFERRED |
| Syscall integration           | No sys_open/sys_read for userspace| Phase 8  |

TESTING STATUS:
---------------

| Test                          | Status       |
|-------------------------------|--------------|
| Mount FAT32 from IDE disk     | ? Working   |
| Read file from FAT32          | ? Working   |
| Write file to FAT32           | ? Deferred  |
| Mount ext4 ramdisk            | ? Untested  |
| Read file from ext4           | ? Untested  |
| Open file by path             | ? Working   |
| Directory listing             | ? Working   |
| Subdirectory traversal        | ? Working   |
| Large file read (>1MB)        | ? Untested  |
| Multiple open files           | ? Untested  |
| Mount multiple filesystems    | ? Untested  |

SHELL COMMANDS IMPLEMENTED:
---------------------------

| Command           | Description                    | Status      |
|-------------------|--------------------------------|-------------|
| vfsmount / <dev>  | Mount filesystem               | ? Working  |
| vfsinfo           | Show mounts and devices        | ? Working  |
| vfsls [path]      | List directory contents        | ? Working  |
| vfscat <file>     | Display file contents          | ? Working  |
| vfsstat <file>    | File information               | ? Working  |
| vfstest           | Run diagnostic tests           | ? Working  |
| ls [path]         | List via VFS                   | ? Working  |
| ll [path]         | Long listing via VFS           | ? Working  |
| cat <file>        | Display file via VFS           | ? Working  |

================================================================================
TWO APPROACHES
================================================================================

You asked about the best approach. Here are both options:


OPTION A: Full Internal Implementation
--------------------------------------
Build everything natively in guideXOS Server:
- Disk formatting utilities
- Partition table creation (MBR/GPT)
- Filesystem formatters (mkfs.fat, mke2fs)
- Live mode detection
- HD Installer port

Pros:
- Self-contained OS
- Can install guideXOS to bare metal
- Production-ready

Cons:
- Much more work (weeks to months)
- Significant testing overhead
- Delays Phase 8 considerably

OPTION B: External Disk Preparation (RECOMMENDED for now)
---------------------------------------------------------
Use host OS tools to create test disk images:
- Create FAT32/ext4 images with Linux/Windows tools
- Place test files in the images
- Attach images to QEMU as virtual drives
- guideXOS only needs to READ from pre-formatted disks

Pros:
- Can start testing immediately
- Focus on what Phase 8 actually needs (reading files)
- Uses well-tested formatting tools
- Much faster to get working

Cons:
- Can't format disks from within guideXOS
- Requires external tools for disk prep
- Not production-ready for installation

RECOMMENDATION: Start with Option B
------------------------------------
For Phase 8, we only need to:
1. Mount a filesystem
2. Open a file by path
3. Read the entire file into memory

We do NOT need:
- Disk formatting
- Partition creation
- Write support (initially)
- OS installation

We can add formatting/installation features in a future phase after
the Universal Binary System is working.

================================================================================
PHASE 7.5 PLAN (Option B - Minimal Viable Filesystem)
================================================================================

GOAL: Verify we can open("/apps/test.gxapp") and read its contents.

PHASE 7.5a: Test Disk Image Creation (Host-Side)
------------------------------------------------
Duration: 1 day

Tasks:
1. Create FAT32 test disk image (64MB)
   - Use Linux: dd + mkfs.fat
   - Or Windows: diskpart + format
   
2. Create ext4 test disk image (64MB)
   - Use Linux: dd + mke2fs
   
3. Populate with test files:
   /test.txt              (small text file)
   /apps/                 (directory)
   /apps/hello.txt        (text file)
   /apps/test.bin         (binary file, 1KB)
   /apps/large.bin        (binary file, 1MB)
   
4. Create QEMU launch script that attaches disk images

Deliverables:
- scripts/create-test-disk.sh
- scripts/run-qemu-with-disk.sh
- disks/test-fat32.img
- disks/test-ext4.img

PHASE 7.5b: VFS Path Resolution Fix
-----------------------------------
Duration: 2-3 days

Problem: vfs::open() doesn't actually traverse directories to find files.

Current Code (vfs.cpp line ~580):
```cpp
// FAT uses cluster-based open, need to find file first
// For now, simplified implementation
// In full implementation, we'd traverse directories
break;
```

This is a stub! We need to implement:
1. Parse path into components: "/apps/hello.txt" ? ["apps", "hello.txt"]
2. Start at root directory
3. For each component, search directory entries
4. When file found, call fs_fat::open_file() with cluster

Tasks:
7.5b.1 - Implement path_split() function
7.5b.2 - Implement directory traversal in VFS
7.5b.3 - Connect to FAT32 directory iteration
7.5b.4 - Connect to ext4 directory iteration
7.5b.5 - Test with simple path: /test.txt
7.5b.6 - Test with subdirectory: /apps/hello.txt

PHASE 7.5c: Read Verification
-----------------------------
Duration: 1-2 days

Verify we can read file contents correctly:

Tasks:
7.5c.1 - Read small text file, verify contents
7.5c.2 - Read binary file, verify SHA256 matches
7.5c.3 - Read large file (>1 cluster), verify integrity
7.5c.4 - Read multiple files sequentially
7.5c.5 - Test file handle limits

PHASE 7.5d: Integration Test
----------------------------
Duration: 1 day

Create a kernel test command that simulates .gxapp loading:

```cpp
// In shell.cpp or a new test file
void test_gxapp_load() {
    // 1. Mount the disk
    uint8_t mount = vfs::mount("/", blockDevIndex);
    
    // 2. Open the file
    uint8_t fd = vfs::open("/apps/test.bin", OPEN_READ);
    
    // 3. Get file size
    int64_t size = vfs::file_size(fd);
    
    // 4. Allocate buffer
    uint8_t* buffer = (uint8_t*)allocate(size);
    
    // 5. Read entire file
    int32_t bytesRead = vfs::read(fd, buffer, size);
    
    // 6. Verify
    serial::puts("Read ");
    serial::putdec(bytesRead);
    serial::puts(" bytes\n");
    
    // 7. Cleanup
    vfs::close(fd);
    free(buffer);
}
```

PHASE 7.5e: Documentation
-------------------------
Duration: 1 day

- Document what works and what doesn't
- Create developer guide for disk image creation
- Update Phase 8 plan with any discoveries

================================================================================
DEFERRED TO FUTURE PHASES
================================================================================

The following features are NOT needed for Phase 8 MVP:

Phase 9 or Later: Disk Management
---------------------------------
- Partition table parsing (MBR/GPT)
- Disk formatting (mkfs)
- Partition creation
- HD Installer port from Legacy
- Live mode detection
- Write support for FAT32/ext4

These will be needed eventually for:
- Installing guideXOS to hard drive
- Creating new filesystems
- USB drive formatting

================================================================================
TEST DISK IMAGE CREATION GUIDE
================================================================================

LINUX (Recommended):
--------------------

# Create 64MB FAT32 image
dd if=/dev/zero of=test-fat32.img bs=1M count=64
mkfs.fat -F 32 test-fat32.img

# Mount and populate
sudo mkdir -p /mnt/testdisk
sudo mount -o loop test-fat32.img /mnt/testdisk
sudo mkdir -p /mnt/testdisk/apps
echo "Hello guideXOS" | sudo tee /mnt/testdisk/test.txt
echo "Test app file" | sudo tee /mnt/testdisk/apps/hello.txt
sudo dd if=/dev/urandom of=/mnt/testdisk/apps/test.bin bs=1024 count=1
sudo dd if=/dev/urandom of=/mnt/testdisk/apps/large.bin bs=1024 count=1024
sudo umount /mnt/testdisk

# Create 64MB ext4 image
dd if=/dev/zero of=test-ext4.img bs=1M count=64
mkfs.ext4 test-ext4.img

# Mount and populate (same as above)
sudo mount -o loop test-ext4.img /mnt/testdisk
# ... same file creation ...
sudo umount /mnt/testdisk

WINDOWS (PowerShell + WSL):
---------------------------

# Using WSL (Windows Subsystem for Linux)
wsl dd if=/dev/zero of=test-fat32.img bs=1M count=64
wsl mkfs.fat -F 32 test-fat32.img
# Then mount in WSL to populate

# Or using diskpart (more complex, creates real disk files)
# Not recommended for simple testing

QEMU COMMAND:
-------------

qemu-system-x86_64 \
    -kernel kernel.elf \
    -drive file=test-fat32.img,format=raw,if=ide \
    -drive file=test-ext4.img,format=raw,if=ide \
    -serial stdio \
    -m 256M

================================================================================
SUCCESS CRITERIA
================================================================================

Phase 7.5 is COMPLETE when:

[_] FAT32 test image created and boots in QEMU
[_] ext4 test image created and boots in QEMU
[_] vfs::mount("/", ...) succeeds
[_] vfs::open("/test.txt", OPEN_READ) returns valid handle
[_] vfs::read() returns correct file contents
[_] vfs::open("/apps/hello.txt", OPEN_READ) works (subdirectory)
[_] Large file (1MB) reads completely without corruption
[_] Multiple files can be opened simultaneously
[_] All file handles are properly closed
[_] No memory leaks during file operations

Phase 8 Prerequisites Met:
[_] Can simulate loading a .gxapp file from disk
[_] File reading is reliable and tested

================================================================================
TIMELINE
================================================================================

Week 1:
- Day 1-2: Create test disk images, QEMU scripts
- Day 3-4: Fix VFS path resolution
- Day 5: Begin read verification

Week 2:
- Day 1-2: Complete read verification
- Day 3: Integration testing
- Day 4: Bug fixes
- Day 5: Documentation, Phase 8 readiness check

================================================================================
QUESTIONS FOR DISCUSSION
================================================================================

1. Do you have access to a Linux environment for creating disk images?
   (WSL on Windows works fine)

2. Should we start with FAT32 or ext4? 
   Recommendation: FAT32 (simpler, already partially implemented)

3. Are there any specific test scenarios you want to include?

4. Do you want to attempt any write operations in this phase, or
   defer all writes to a future phase?

5. Should we create a simple "disk image builder" script that can
   be run from the host OS to prepare test disks?

================================================================================
APPENDIX: CODE GAPS IDENTIFIED
================================================================================

1. vfs.cpp:open() - Line ~580
   Directory traversal not implemented, just has a TODO comment.

2. vfs.cpp - No open_by_path() that parses "/path/to/file"
   Current open() expects caller to have already resolved the path.

3. fs_fat.cpp - open_file() requires cluster number
   No function to find a file by name in a directory.

4. fs_fat.cpp - No recursive directory traversal
   Only supports root directory iteration.

5. vfs.cpp - stat() not fully implemented
   Cannot get file info without opening.

================================================================================
END OF DOCUMENT
================================================================================
