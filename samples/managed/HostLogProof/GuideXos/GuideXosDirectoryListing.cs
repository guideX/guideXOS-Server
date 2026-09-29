using System;

namespace HostLogProof;

/// <summary>
/// Shared bounded directory snapshot and parent-path operations for managed
/// Open and Save dialogs. The caller owns only the returned at-most-64 rows.
/// </summary>
internal static class GuideXosDirectoryListing
{
    internal static GuideXosFileResult Load(
        GuideXosHost host,
        string directory,
        out GuideXosDirectoryEntry[] entries,
        out bool hasMore)
    {
        entries = Array.Empty<GuideXosDirectoryEntry>();
        hasMore = false;
        GuideXosPickerPathStatus pathStatus =
            GuideXosPickerPath.TryNormalizeDirectory(directory, out string normalized);
        if (pathStatus != GuideXosPickerPathStatus.Success ||
            !string.Equals(directory, normalized, StringComparison.Ordinal))
        {
            return GuideXosFileResult.InvalidPath;
        }

        GuideXosFileResult result = GuideXosFile.TryListDirectory(
            host, System.Text.Encoding.UTF8.GetBytes(normalized),
            out GuideXosDirectorySnapshot snapshot);
        if (result != GuideXosFileResult.Success || snapshot?.Entries == null)
        {
            return result == GuideXosFileResult.Success
                ? GuideXosFileResult.IoFailure : result;
        }
        if (snapshot.Entries.Length > GuideXosOpenFileDialog.MaximumDirectoryEntries)
        {
            return GuideXosFileResult.IoFailure;
        }

        GuideXosDirectoryEntry[] bounded = new GuideXosDirectoryEntry[
            GuideXosOpenFileDialog.MaximumDirectoryEntries];
        int count = 0;
        for (int index = 0; index < snapshot.Entries.Length; index++)
        {
            GuideXosDirectoryEntry entry = snapshot.Entries[index];
            if (entry == null || string.IsNullOrEmpty(entry.Name) ||
                entry.Name.Length > GuideXosOpenFileDialog.MaximumNameBytes ||
                (entry.Type != GuideXosEntryType.Directory &&
                    entry.Type != GuideXosEntryType.Regular))
            {
                return GuideXosFileResult.IoFailure;
            }
            if (entry.Name == "." || entry.Name == "..") continue;
            bounded[count++] = entry;
        }
        Array.Resize(ref bounded, count);
        GuideXosOpenFileDialog.SortForExplorer(bounded, count);
        entries = bounded;
        hasMore = snapshot.HasMore;
        return GuideXosFileResult.Success;
    }

    internal static bool TryGetParent(
        string directory, string root, out string parent)
    {
        parent = null;
        if (string.IsNullOrEmpty(directory) || string.IsNullOrEmpty(root) ||
            string.Equals(directory, root, StringComparison.Ordinal))
        {
            return false;
        }
        int slash = directory.LastIndexOf('/');
        parent = slash <= root.Length ? root : directory.Substring(0, slash);
        return GuideXosPickerPath.TryNormalizeDirectory(parent, out string normalized) ==
                GuideXosPickerPathStatus.Success &&
            string.Equals(parent, normalized, StringComparison.Ordinal);
    }
}
