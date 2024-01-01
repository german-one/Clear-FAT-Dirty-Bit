/// @copyright Copyright (c) 2024 Steffen Illhardt,
///            licensed under the MIT license
///            ( https://opensource.org/license/mit/ ).
/// @file      ClearFATDirtyBit.c
/// @version   1.0
/// @author    Steffen Illhardt
/// @date      2024
/// @pre       Requires at least C99 support and Windows 8.
/// @warning   This app does not fix any drive errors, it only tries to clear
///            the dirty bit on FAT32 or exFAT formatted drives. Running this
///            app may result in data loss for which THE AUTHOR IS NOT
///            RESPONSIBLE! Refer to the license text. Use at your own risk.
/// @brief     Clears the dirty bit on FAT32 or exFAT formatted drives without
///            fixing errors on the disk.
///
/// <hr><br>
/// SYNTAX: <br>
/// @code
/// ClearFATDirtyBit.exe <driveSpec>
/// @endcode
/// - `<driveSpec>` Drive letter followed by a colon (e.g. `E:`).
///
/// <hr><br>
/// NOTE: Windows Defender's ransomware protection (Windows 10 and newer) may
/// prohibit applications from writing to controlled folders and raw drive
/// sectors are treated as such. Allow this app through controlled folder
/// access: <br>
/// https://learn.microsoft.com/microsoft-365/security/defender-endpoint/customize-controlled-folders#allow-specific-apps-to-make-changes-to-controlled-folders
/// <br> E.g. run ...
/// @code
/// Add-MpPreference -ControlledFolderAccessAllowedApplications 'D:\full\path\to\this\ClearFATDirtyBit.exe'
/// @endcode
/// ... in an elevated PowerShell process (customize the path accordingly).
///
/// The app tries to lock the volume. Locking will fail if files are open or the
/// drive is accessed by other processes. In this case, it may still *appear*
/// dirty until the next drive removal or system reboot.
///
/// <hr><br>
/// Program flow:
/// - Check the passed argument, get the drive letter.
/// - Open a drive handle. (read/write)
/// - Lock the volume if possible. (not essential)
/// - Determine the file system, check if supported.
/// - Determine the physical sector size, check it fits into the buffer.
/// - Read sector 0 into the buffer.
/// - Check the dirty bit. (file system specific)
/// - Clear the dirty bit in the buffer.
/// - Set the file pointer to the begin of the sector.
/// - Write the buffered bytes back to the sector.
/// - Temporarily dismount the volume if locked.
/// - Close the drive handle.
///
/// <hr>

/// @cond _NO_DOC_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#ifdef _WIN32_WINNT
#  undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x602
#include <windows.h>

typedef union name_buf
{
  int64_t i64[2];
  int32_t i32[4];
  wchar_t wcs[8]; // 16-bit `wchar_t` on Windows
} name_buf_t;

int main(int argc, char *argv[])
{
  name_buf_t name = { .wcs = L"\\\\.\\\0:" }; // pattern "\\.\*:" with wildcard * at index 4 set to zero to be replaced with the drive letter
  if (argc < 2 ||
      (unsigned)(name.wcs[4] = argv[1][0] & L'\xDF') - L'A' > 25 || // try to convert the drive letter from `char` to both `wchar_t` and uppercase, check if the result is in range A-Z
      argv[1][1] != ':' || argv[1][2])
  {
    fputs("Syntax error. Usage:\nClearFATDirtyBit.exe <driveSpec>\n  <driveSpec>   Drive letter followed by a colon (e.g. E:).\n", stderr);
    return 1;
  }

  // the drive spec is prepared to be used with `CreateFileW()` as the `...A()` API most likely performs an internal string conversion to UTF-16 first, just to call the `...W()` API anyway
  const HANDLE driveHandle = CreateFileW(name.wcs, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
  if (driveHandle == INVALID_HANDLE_VALUE)
  {
    fputs("Unable to access the specified drive.\n", stderr); // drive does not exist, OS drive, user is a "Guest" account, ... whatsoever
    return 1;
  }

  DWORD nBytes;
  const BOOL isLocked = DeviceIoControl(driveHandle, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &nBytes, NULL); // Locking will fail if there are still files open.

  GetVolumeInformationByHandleW(driveHandle, NULL, 0, NULL, NULL, NULL, name.wcs, ARRAYSIZE(name.wcs)); // if this fails `name` will still contain the drive spec, which is okay as comparisons will also fail
  int dirtyByte; // offset of the byte in sector 0 that contains the dirty bit
  uint8_t dirtyFlag; // bit in the byte described above that marks the volume dirty if set to 1
  // integer comparison used for strings (instead of iterative comparison `wcscmp(s1, s2)`), Little Endian byte order, 2-byte aligned
  static const name_buf_t fat32 = { .wcs = L"FAT32" };
  static const name_buf_t exfat = { .wcs = L"exFAT" };
  if (name.i64[0] == fat32.i64[0] && //  3 T A F
      name.i32[2] == fat32.i32[2]) //   \0 2
  {
    dirtyByte = 0x41; // `CurrentHead` field used for the Windows NT family (in contrast to DOS and older Windows versions) as commonly understood but not actually documented; Microsoft open source reference: https://github.com/Microsoft/Windows-driver-samples/blob/main/filesys/fastfat/fat.h#L210
    dirtyFlag = 0x01; // 0000'0001
  }
  else if (name.i64[0] == exfat.i64[0] && //  A F x e
           name.i32[2] == exfat.i32[2]) //   \0 T
  {
    dirtyByte = 0x6A; // https://learn.microsoft.com/windows/win32/fileio/exfat-specification
    dirtyFlag = 0x02; // 0000'0010
  }
  else
  {
    fputs("Not a FAT32 or exFAT file system.\n", stderr);
    CloseHandle(driveHandle);
    return 1;
  }

  // R/W operations must be sector-aligned. Even if 512 B is probably the logical sector size, 4 KB is common as the physical sector size.
  // The physical sector size is taken to avoid additional RMW. https://learn.microsoft.com/windows/win32/w8cookbook/advanced-format--4k--disk-compatibility-update
  uint8_t sectorBytes[4096];
  FILE_STORAGE_INFO storageInfo;
  if (!GetFileInformationByHandleEx(driveHandle, FileStorageInfo, &storageInfo, sizeof(storageInfo)) || // `DeviceIoControl(...,IOCTL_DISK_GET_DRIVE_GEOMETRY,...)` would just have provided the logical sector size
      storageInfo.PhysicalBytesPerSectorForAtomicity > sizeof(sectorBytes) ||
      !ReadFile(driveHandle, sectorBytes, storageInfo.PhysicalBytesPerSectorForAtomicity, &nBytes, NULL))
  {
    fputs("Reading drive data failed.\n", stderr);
    CloseHandle(driveHandle);
    return 1;
  }

  if (!(sectorBytes[dirtyByte] & dirtyFlag))
  {
    puts("Drive is clean.");
    CloseHandle(driveHandle);
    return 0;
  }

  sectorBytes[dirtyByte] &= ~dirtyFlag; // clear the dirty bit in the buffer written back to the sector
  // Writing to sector 0 does not require the volume lock. However, if writing fails, Windows Defender could be the culprit. Refer to the explanation in the comments at the top.
  if (!SetFilePointerEx(driveHandle, (LARGE_INTEGER){ 0 }, NULL, FILE_BEGIN) ||
      !WriteFile(driveHandle, sectorBytes, storageInfo.PhysicalBytesPerSectorForAtomicity, &nBytes, NULL))
  {
    fputs("Unable to clear the dirty bit.\nEnsure Windows Defender allows this app to make changes in controlled folders.\n", stderr);
    CloseHandle(driveHandle);
    return 1;
  }

  if (isLocked) // If not locked, the drive may appear dirty until the next drive removal or system reboot, even though the dirty bit was cleared.
    DeviceIoControl(driveHandle, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &nBytes, NULL); // make the drive appear clean in Explorer once it gets automatically re-mounted

  puts("Dirty bit successfully cleared.");
  CloseHandle(driveHandle); // any `CloseHandle()` in the code does also unlock the volume if necessary, https://learn.microsoft.com/windows/win32/api/winioctl/ni-winioctl-fsctl_lock_volume#remarks
  return 0;
}

/// @endcond
