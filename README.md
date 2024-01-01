Clears the dirty bit on FAT32 or exFAT formatted drives on Windows.  

----

WARNING:  
This app does not fix any drive errors, it only tries to clear the dirty bit on FAT32 or exFAT formatted drives. Running this app may result in data loss for which THE AUTHOR IS NOT RESPONSIBLE! Refer to the license text. Use at your own risk.  

----

SYNTAX:  
```
ClearFATDirtyBit.exe <driveSpec>
```
 * `<driveSpec>` Drive letter followed by a colon (e.g. `E:`).  

Typically the app does not need to be run with elevated rights.  

----

NOTE:  
Windows Defender's ransomware protection (Windows 10 and newer) may prohibit applications from writing to controlled folders and raw drive sectors are treated as such. Allow this app through controlled folder access:  
https://learn.microsoft.com/microsoft-365/security/defender-endpoint/customize-controlled-folders#allow-specific-apps-to-make-changes-to-controlled-folders  
E.g. run ...  
```
Add-MpPreference -ControlledFolderAccessAllowedApplications 'D:\full\path\to\this\ClearFATDirtyBit.exe'
```
... in an elevated PowerShell process (customize the path accordingly).  

The app tries to lock the volume. Locking will fail if files are open or the drive is accessed by other processes. In this case, it may still *appear* dirty until the next drive removal or system reboot.  

----

Addendum:  
As mentioned above, the app does not repair the drive. It doesn't even try to find any failures. You may be wondering what this little utility is good for then? In fact, I don't recommend using it just to get rid of the dirty bit. There might be a good reason why the drive has been marked dirty. My use case is to work around a broken USB port on my laptop (actually they're all broken already) that the hub for my external drives is connected to. I think the device is still too good to throw away. Although only that would really solve the problem. However, the dirty bit is now set frequently, making the drives even more unstable to use. To skip the lengthy drive check (which in my case never found any errors), I decided to just clear the dirty bit.  
So ultimately the app isn't really useful per se. I did some research on the internet before writing the code. The comments may be of interest to those who intend to write their own application to access drive sectors, which is the main reason why I decided to published the source code.  

----
