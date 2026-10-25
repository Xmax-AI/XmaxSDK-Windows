using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace XLab.Shared.Services;

// Generic credentials belong to the signed-in Windows user and survive app restarts.
internal sealed class ApiKeyStore(string target = "XMAX.AI/XLabWpf/ApiKey")
{
    private const uint Generic = 1, LocalMachine = 2;
    private const int NotFound = 1168;

    internal string Load()
    {
        if (!CredRead(target, Generic, 0, out var pointer))
        {
            int error = Marshal.GetLastWin32Error();
            if (error == NotFound) return "";
            throw new Win32Exception(error, "Could not restore the saved API Key.");
        }
        try
        {
            var credential = Marshal.PtrToStructure<Credential>(pointer);
            return Marshal.PtrToStringUni(credential.Blob, checked((int)credential.BlobSize / 2)) ?? "";
        }
        finally { CredFree(pointer); }
    }

    internal unsafe void Save(string key)
    {
        if (string.IsNullOrWhiteSpace(key))
        {
            if (!CredDelete(target, Generic, 0))
            {
                int error = Marshal.GetLastWin32Error();
                if (error != NotFound) throw new Win32Exception(error, "Could not remove the saved API Key.");
            }
            return;
        }
        byte[] bytes = Encoding.Unicode.GetBytes(key);
        try
        {
            fixed (byte* blob = bytes)
            {
                var credential = new Credential
                {
                    Type = Generic, TargetName = target, Persist = LocalMachine,
                    UserName = "API Key", BlobSize = checked((uint)bytes.Length), Blob = (IntPtr)blob
                };
                if (!CredWrite(ref credential, 0))
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "Could not remember the API Key. Please enter it again next time.");
            }
        }
        finally { CryptographicOperations.ZeroMemory(bytes); }
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct Credential
    {
        internal uint Flags, Type;
        internal string? TargetName, Comment;
        internal System.Runtime.InteropServices.ComTypes.FILETIME LastWritten;
        internal uint BlobSize;
        internal IntPtr Blob;
        internal uint Persist, AttributeCount;
        internal IntPtr Attributes;
        internal string? TargetAlias, UserName;
    }
    [DllImport("advapi32.dll", EntryPoint = "CredReadW", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CredRead(string target, uint type, uint flags, out IntPtr credential);
    [DllImport("advapi32.dll", EntryPoint = "CredWriteW", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CredWrite(ref Credential credential, uint flags);
    [DllImport("advapi32.dll", EntryPoint = "CredDeleteW", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CredDelete(string target, uint type, uint flags);
    [DllImport("advapi32.dll", ExactSpelling = true)]
    private static extern void CredFree(IntPtr credential);
}
