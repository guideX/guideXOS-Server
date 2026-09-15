using System;

namespace HostLogProof;

public enum GuideXosEntryType : uint
{
    Regular = 1,
    Directory = 2,
}

public sealed class GuideXosDirectoryEntry
{
    public GuideXosDirectoryEntry(string name, GuideXosEntryType type, ulong size)
    {
        Name = name;
        Type = type;
        Size = size;
    }

    public string Name { get; }
    public GuideXosEntryType Type { get; }
    public ulong Size { get; }
}

public sealed class GuideXosDirectorySnapshot
{
    internal GuideXosDirectorySnapshot(
        GuideXosDirectoryEntry[] entries, bool hasMore)
    {
        Entries = entries;
        HasMore = hasMore;
    }

    public GuideXosDirectoryEntry[] Entries { get; }
    public bool HasMore { get; }
}

public sealed class GuideXosFileInfo
{
    internal GuideXosFileInfo(GuideXosEntryType type, ulong size)
    {
        Type = type;
        Size = size;
    }

    public GuideXosEntryType Type { get; }
    public ulong Size { get; }
}

/// <summary>
/// Reusable bounded UTF-8 file helpers. The application sees only path,
/// managed byte content, and stable file results; VFS handles and pointers are
/// kept inside the host bridge.
/// </summary>
public static class GuideXosFile
{
    public static GuideXosFileResult TryListDirectory(
        GuideXosHost host,
        ReadOnlySpan<byte> path,
        out GuideXosDirectorySnapshot snapshot)
    {
        return host.TryListDirectory(path, out snapshot);
    }

    public static GuideXosFileResult TryGetInfo(
        GuideXosHost host,
        ReadOnlySpan<byte> path,
        out GuideXosFileInfo info)
    {
        return host.TryGetInfo(path, out info);
    }

    public static GuideXosFileResult ReadAllTextUtf8(
        GuideXosHost host,
        ReadOnlySpan<byte> path,
        out byte[] utf8)
    {
        return host.TryReadAllBytes(path, out utf8);
    }

    public static GuideXosFileResult WriteAllTextUtf8(
        GuideXosHost host,
        ReadOnlySpan<byte> path,
        ReadOnlySpan<byte> utf8)
    {
        return host.TryWriteAllBytes(path, utf8);
    }
}
