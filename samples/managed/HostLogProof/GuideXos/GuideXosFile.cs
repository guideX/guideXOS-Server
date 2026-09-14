using System;

namespace HostLogProof;

/// <summary>
/// Reusable bounded UTF-8 file helpers. The application sees only path,
/// managed byte content, and stable file results; VFS handles and pointers are
/// kept inside the host bridge.
/// </summary>
public static class GuideXosFile
{
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
