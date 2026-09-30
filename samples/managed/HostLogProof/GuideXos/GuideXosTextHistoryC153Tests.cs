using System;

namespace HostLogProof;

public static class GuideXosTextHistoryC153Tests
{
    public const int CaseCount = 26;

    public static bool Run(GuideXosHost host)
    {
        int cases = 0;
        bool all = true;

        GuideXosTextHistory empty = new(256);
        all &= Case(ref cases, empty.Reset(ReadOnlySpan<char>.Empty, 0, 0) &&
            empty.Count == 1 && empty.CurrentRevision.IsValid);
        all &= Case(ref cases, !empty.CanUndo && !empty.CanRedo);

        GuideXosTextRevision baseline = empty.CurrentRevision;
        all &= Case(ref cases, empty.Record("a".AsSpan(), 1, 1) &&
            empty.Count == 2 && empty.CurrentRevision != baseline &&
            empty.CanUndo && !empty.CanRedo);
        char[] restored = new char[256];
        all &= Case(ref cases, empty.TryUndo(restored, out int length,
                out int caret, out int anchor, out GuideXosTextRevision undoRevision) &&
            length == 0 && caret == 0 && anchor == 0 &&
            undoRevision == baseline && empty.CanRedo && !empty.CanUndo);
        all &= Case(ref cases, empty.TryRedo(restored, out length, out caret,
                out anchor, out GuideXosTextRevision redoRevision) &&
            length == 1 && restored[0] == 'a' && caret == 1 && anchor == 1 &&
            redoRevision == empty.CurrentRevision && !empty.CanRedo);

        GuideXosTextHistory multiple = NewHistory("A", 8);
        multiple.Record("AB".AsSpan(), 2, 2);
        multiple.Record("ABC".AsSpan(), 3, 3);
        all &= Case(ref cases, multiple.Count == 3 && multiple.CurrentIndex == 2);
        bool twoUndo = multiple.TryUndo(restored, out length, out caret,
                out anchor, out _) && length == 2 && restored[1] == 'B' &&
            multiple.TryUndo(restored, out length, out caret, out anchor, out _) &&
            length == 1 && restored[0] == 'A';
        all &= Case(ref cases, twoUndo && multiple.CurrentIndex == 0);
        bool twoRedo = multiple.TryRedo(restored, out length, out caret,
                out anchor, out _) && length == 2 && restored[1] == 'B' &&
            multiple.TryRedo(restored, out length, out caret, out anchor, out _) &&
            length == 3 && restored[2] == 'C';
        all &= Case(ref cases, twoRedo && multiple.CurrentIndex == 2);

        int unavailableIndex = multiple.CurrentIndex;
        all &= Case(ref cases, !multiple.TryRedo(restored, out _, out _,
            out _, out _) && multiple.CurrentIndex == unavailableIndex);
        while (multiple.CanUndo) multiple.TryUndo(restored, out _, out _,
            out _, out _);
        unavailableIndex = multiple.CurrentIndex;
        all &= Case(ref cases, !multiple.TryUndo(restored, out _, out _,
            out _, out _) && multiple.CurrentIndex == unavailableIndex);

        GuideXosTextHistory branch = NewHistory("A", 8);
        branch.Record("AB".AsSpan(), 2, 2);
        branch.Record("ABC".AsSpan(), 3, 3);
        branch.TryUndo(restored, out _, out _, out _, out _);
        all &= Case(ref cases, branch.Record("ABD".AsSpan(), 3, 3) &&
            !branch.CanRedo && branch.Count == 3 && branch.CurrentIndex == 2);

        GuideXosTextHistory bounded = NewHistory("0", 3);
        bounded.Record("1".AsSpan(), 1, 1);
        GuideXosTextRevision saved = bounded.CurrentRevision;
        bounded.Record("2".AsSpan(), 1, 1);
        bounded.Record("3".AsSpan(), 1, 1);
        all &= Case(ref cases, bounded.Count == 3 && bounded.CanUndo &&
            bounded.ContainsRevision(saved));
        bounded.Record("4".AsSpan(), 1, 1);
        all &= Case(ref cases, bounded.Count == 3 &&
            !bounded.ContainsRevision(saved));
        GuideXosTextRevision reusedContentRevision = bounded.CurrentRevision;
        bounded.Record("5".AsSpan(), 1, 1);
        bounded.Record("1".AsSpan(), 1, 1);
        all &= Case(ref cases, bounded.CurrentRevision != saved &&
            bounded.CurrentRevision != reusedContentRevision &&
            !bounded.ContainsRevision(saved));
        bool oldestStops = true;
        while (bounded.CanUndo)
        {
            oldestStops &= bounded.TryUndo(restored, out _, out _, out _, out _);
        }
        oldestStops &= !bounded.CanUndo &&
            !bounded.TryUndo(restored, out _, out _, out _, out _);
        all &= Case(ref cases, oldestStops);

        GuideXosTextHistory selection = NewHistory("abcd", 4);
        selection.UpdateCurrentCaret(2, 4);
        selection.Record("abXcd".AsSpan(), 3, 3);
        bool caretRoundTrip = selection.TryUndo(restored, out length, out caret,
                out anchor, out _) && length == 4 && caret == 2 && anchor == 4 &&
            selection.TryRedo(restored, out length, out caret, out anchor, out _) &&
            length == 5 && caret == 3 && anchor == 3;
        all &= Case(ref cases, caretRoundTrip);

        GuideXosTextHistory emptyState = NewHistory(string.Empty, 4);
        emptyState.Record("x".AsSpan(), 1, 1);
        all &= Case(ref cases, emptyState.TryUndo(restored, out length,
            out caret, out anchor, out _) && length == 0 && caret == 0 &&
            anchor == 0);

        char[] maximum = new char[256];
        Array.Fill(maximum, 'M');
        GuideXosTextHistory maximumState = new(256);
        bool maximumWorks = maximumState.Reset(maximum, 256, 256) &&
            maximumState.Record(maximum.AsSpan(0, 255), 255, 255) &&
            maximumState.TryUndo(restored, out length, out caret, out anchor, out _) &&
            length == 256 && restored.AsSpan(0, length).SequenceEqual(maximum);
        all &= Case(ref cases, maximumWorks);

        GuideXosTextHistory noPhantom = NewHistory("A", 4);
        noPhantom.Record("AB".AsSpan(), 2, 2);
        noPhantom.TryUndo(restored, out _, out _, out _, out _);
        bool identicalPreservesRedo = !noPhantom.Record("A".AsSpan(), 0, 0) &&
            noPhantom.CanRedo && noPhantom.Count == 2;
        all &= Case(ref cases, identicalPreservesRedo);

        GuideXosTextHistory tooSmall = NewHistory("A", 4);
        tooSmall.Record("AB".AsSpan(), 2, 2);
        int tooSmallIndex = tooSmall.CurrentIndex;
        all &= Case(ref cases, !tooSmall.TryUndo(Span<char>.Empty, out _, out _,
            out _, out _) && tooSmall.CurrentIndex == tooSmallIndex);

        GuideXosTextHistory oneSlot = new(8, 1);
        bool oneSlotWorks = oneSlot.Reset("A".AsSpan(), 1, 1) &&
            oneSlot.Record("B".AsSpan(), 1, 1) && oneSlot.Count == 1 &&
            !oneSlot.CanUndo && !oneSlot.CanRedo;
        all &= Case(ref cases, oneSlotWorks);

        GuideXosTextHistory replacement = NewHistory("old document", 4);
        GuideXosTextRevision stale = replacement.CurrentRevision;
        bool resetIsolated = replacement.Record("edited".AsSpan(), 6, 6) &&
            replacement.Reset("new".AsSpan(), 3, 3) &&
            replacement.Count == 1 && !replacement.CanUndo &&
            !replacement.CanRedo && !replacement.ContainsRevision(stale);
        all &= Case(ref cases, resetIsolated);

        GuideXosTextHistory identity = NewHistory("same", 3);
        GuideXosTextRevision firstIdentity = identity.CurrentRevision;
        bool resetIdentityUnique = identity.Reset("same".AsSpan(), 4, 4) &&
            identity.CurrentRevision != firstIdentity && identity.Count == 1;
        all &= Case(ref cases, resetIdentityUnique);

        GuideXosTextHistory invalid = new(2);
        invalid.Reset("A".AsSpan(), 1, 1);
        char[] overCapacity = new char[3];
        bool invalidRecordPreserves = !invalid.Record(overCapacity, 3, 3) &&
            invalid.Count == 1 && !invalid.CanUndo &&
            invalid.CurrentRevision.IsValid;
        all &= Case(ref cases, invalidRecordPreserves);

        all &= Case(ref cases, empty.Capacity == 16 &&
            empty.CurrentIndex >= 0 && empty.Count <= empty.Capacity);

        GuideXosTextHistory stress = new(1);
        char[] stressCopy = new char[1];
        Span<char> stressInput = stackalloc char[1];
        bool stressValid = stress.Reset("0".AsSpan(), 1, 1);
        char stressValue = '0';
        for (int index = 0; index < 100; index++)
        {
            int stressLength;
            char next = stressValue == 'A' ? 'B' : 'A';
            stressInput[0] = next;
            stressValid &= stress.Record(stressInput, 1, 1);
            stressValue = next;
            if (index % 4 == 0 && stress.CanUndo)
            {
                stressValid &= stress.TryUndo(stressCopy, out stressLength,
                    out _, out _, out _) && stressLength == 1;
                stressValue = stressCopy[0];
                stressValid &= stress.TryRedo(stressCopy, out stressLength,
                    out _, out _, out _) && stressLength == 1 &&
                    stressCopy[0] == next;
                stressValue = stressCopy[0];
            }
            else if (index % 7 == 0 && stress.CanUndo)
            {
                stressValid &= stress.TryUndo(stressCopy, out stressLength,
                    out _, out _, out _) && stressLength == 1;
                stressValue = stressCopy[0];
            }

            stressValid &= stress.Count <= stress.Capacity &&
                stress.CurrentIndex >= 0 && stress.CurrentIndex < stress.Count &&
                stress.CurrentRevision.IsValid;
            if (stress.CanUndo)
            {
                GuideXosTextRevision current = stress.CurrentRevision;
                stressValid &= stress.TryUndo(stressCopy, out stressLength,
                        out _, out _, out _) &&
                    stress.TryRedo(stressCopy, out stressLength,
                        out _, out _, out _) && stressCopy[0] == stressValue &&
                    stress.CurrentRevision == current;
            }
        }
        char newestStressValue = stressValue;
        bool stressBounded = stressValid && stress.Count == stress.Capacity;
        int drainLength;
        while (stress.CanUndo)
        {
            stressBounded &= stress.TryUndo(stressCopy, out drainLength,
                out _, out _, out _) && drainLength == 1 &&
                stress.CurrentRevision.IsValid;
        }
        stressBounded &= !stress.CanUndo && stress.Count == stress.Capacity &&
            stress.TryUndo(stressCopy, out _, out _, out _, out _) == false;
        while (stress.CanRedo)
        {
            stressBounded &= stress.TryRedo(stressCopy, out drainLength,
                out _, out _, out _) && drainLength == 1;
            stressValue = stressCopy[0];
        }
        stressBounded &= stressValue == newestStressValue &&
            stress.Count == stress.Capacity && !stress.CanRedo;
        all &= Case(ref cases, stressBounded);

        bool countCorrect = cases == CaseCount;
        host?.TryLog(all && countCorrect
            ? "C153-HISTORY-CORE cases=26 bounded=16 ring=PASS stress=100 identity=PASS result=PASS"u8
            : "C153-HISTORY-CORE result=FAIL"u8);
        return all && countCorrect;
    }

    private static GuideXosTextHistory NewHistory(string text, int capacity)
    {
        GuideXosTextHistory history = new(256, capacity);
        history.Reset(text.AsSpan(), text.Length, text.Length);
        return history;
    }

    private static bool Case(ref int count, bool passed)
    {
        ++count;
        return passed;
    }

}
