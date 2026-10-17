#if HOSTLOGPROOF_C174_SHARED_MANAGED_RESIZE
using System;
using HostLogProof.Applications;

namespace HostLogProof;

public static class GuideXosManagedWindowResizeC174Tests
{
    public const int CaseCount = 22;
    public const int StressCaseCount = 4;
    public const int StressGeometryTransitions = 1000;
    public const int StressAlternatingCycles = 500;
    public const int StressIsolationTransitions = 500;

    public static long StressAllocationBytes { get; private set; }
    public static int StressDriftCount { get; private set; }
    public static int StressRollbackCount { get; private set; }
    public static int FirstFailedCase { get; private set; }

    private static readonly GuideXosManagedWindowResizeBoundsC174 SettingsBounds =
        new(ManagedSettingsCenter.MinimumWindowWidth,
            ManagedSettingsCenter.MinimumWindowHeight,
            ManagedSettingsCenter.MaximumWindowWidth,
            ManagedSettingsCenter.MaximumWindowHeight);
    private static readonly GuideXosManagedWindowResizeBoundsC174 ExplorerBounds =
        new(Applications.ManagedFileExplorerC163.MinimumWindowWidth,
            Applications.ManagedFileExplorerC163.MinimumWindowHeight,
            Applications.ManagedFileExplorerC163.MaximumWindowWidth,
            Applications.ManagedFileExplorerC163.MaximumWindowHeight);

    public static bool Run(GuideXosHost host)
    {
        _ = host;
        FirstFailedCase = 0;
        int cases = 0;
        bool passed = true;

        passed &= Case(ref cases, SettingsBounds.TryCreateCandidate(512, 222,
            out GuideXosManagedWindowResizeCandidateC174 settingsMin) &&
            settingsMin.ClientWidth == 512 && settingsMin.ClientHeight == 198);
        passed &= Case(ref cases, SettingsBounds.TryCreateCandidate(720, 420,
            out GuideXosManagedWindowResizeCandidateC174 settingsMax) &&
            settingsMax.ClientWidth == 720 && settingsMax.ClientHeight == 396);
        passed &= Case(ref cases, ExplorerBounds.TryCreateCandidate(600, 360,
            out GuideXosManagedWindowResizeCandidateC174 explorerMin) &&
            explorerMin.ClientWidth == 600 && explorerMin.ClientHeight == 336);
        passed &= Case(ref cases, ExplorerBounds.TryCreateCandidate(960, 526,
            out GuideXosManagedWindowResizeCandidateC174 explorerMax) &&
            explorerMax.ClientWidth == 960 && explorerMax.ClientHeight == 502);

        passed &= Case(ref cases, !SettingsBounds.TryCreateCandidate(511, 222,
            out _));
        passed &= Case(ref cases, !SettingsBounds.TryCreateCandidate(512, 221,
            out _));
        passed &= Case(ref cases, !SettingsBounds.TryCreateCandidate(721, 420,
            out _));
        passed &= Case(ref cases, !SettingsBounds.TryCreateCandidate(720, 421,
            out _));
        passed &= Case(ref cases, !ExplorerBounds.TryCreateCandidate(599, 360,
            out _));
        passed &= Case(ref cases, !ExplorerBounds.TryCreateCandidate(600, 359,
            out _));
        passed &= Case(ref cases, !ExplorerBounds.TryCreateCandidate(961, 526,
            out _));
        passed &= Case(ref cases, !ExplorerBounds.TryCreateCandidate(960, 527,
            out _));

        GuideXosManagedWindowResizeBoundsC174 malformedBounds = new(
            0, 20, 720, 420);
        passed &= Case(ref cases, !malformedBounds.TryCreateCandidate(512, 222,
            out _));

        GuideXosManagedWindowResizeTransactionC174 invalid = new(
            in SettingsBounds, 560, 340, 511, 222);
        passed &= Case(ref cases, !invalid.IsValid &&
            invalid.Failure == GuideXosManagedWindowResizeFailureC174.Geometry &&
            invalid.Result == GuideXosResult.InvalidArgument &&
            !invalid.RequiresRollback);

        GuideXosManagedWindowResizeTransactionC174 badPrior = new(
            in ExplorerBounds, 599, 360, 800, 390);
        passed &= Case(ref cases, !badPrior.IsValid &&
            badPrior.Failure == GuideXosManagedWindowResizeFailureC174.Geometry);

        GuideXosManagedWindowResizeTransactionC174 layoutFailure = new(
            in SettingsBounds, 560, 340, 512, 222);
        bool layoutAccepted = layoutFailure.Complete(layoutAccepted: false,
            renderResult: GuideXosResult.Success, stateAccepted: false);
        passed &= Case(ref cases, !layoutAccepted && layoutFailure.RequiresRollback &&
            layoutFailure.Failure == GuideXosManagedWindowResizeFailureC174.Layout &&
            layoutFailure.Result == GuideXosResult.InvalidArgument &&
            layoutFailure.PriorOuterWidth == 560 &&
            layoutFailure.PriorOuterHeight == 340);

        GuideXosManagedWindowResizeTransactionC174 renderFailure = new(
            in ExplorerBounds, 800, 390, 960, 526);
        bool renderAccepted = renderFailure.Complete(layoutAccepted: true,
            renderResult: GuideXosResult.CapabilityUnavailable,
            stateAccepted: true);
        passed &= Case(ref cases, !renderAccepted && renderFailure.RequiresRollback &&
            renderFailure.Failure == GuideXosManagedWindowResizeFailureC174.Render &&
            renderFailure.Result == GuideXosResult.CapabilityUnavailable &&
            renderFailure.PriorOuterWidth == 800 &&
            renderFailure.PriorOuterHeight == 390);

        GuideXosManagedWindowResizeTransactionC174 stateFailure = new(
            in ExplorerBounds, 800, 390, 600, 360);
        bool stateAccepted = stateFailure.Complete(layoutAccepted: true,
            renderResult: GuideXosResult.Success, stateAccepted: false);
        passed &= Case(ref cases, !stateAccepted && stateFailure.RequiresRollback &&
            stateFailure.Failure == GuideXosManagedWindowResizeFailureC174.State &&
            stateFailure.Result == GuideXosResult.InvalidArgument &&
            stateFailure.PriorOuterWidth == 800 &&
            stateFailure.PriorOuterHeight == 390);

        GuideXosManagedWindowResizeTransactionC174 accepted = new(
            in SettingsBounds, 560, 340, 720, 420);
        bool committed = accepted.Complete(layoutAccepted: true,
            renderResult: GuideXosResult.Success, stateAccepted: true);
        passed &= Case(ref cases, committed && accepted.IsAccepted &&
            !accepted.RequiresRollback &&
            accepted.Failure == GuideXosManagedWindowResizeFailureC174.None &&
            accepted.Result == GuideXosResult.Success);

        GuideXosManagedWindowResizeTransactionC174 unchanged = new(
            in SettingsBounds, 560, 340, 560, 340);
        passed &= Case(ref cases, unchanged.IsValid && !unchanged.IsChanged &&
            unchanged.Candidate.OuterWidth == 560 &&
            unchanged.Candidate.OuterHeight == 340 &&
            !unchanged.RequiresRollback);

        GuideXosManagedWindowResizeTransactionC174 settings = new(
            in SettingsBounds, 560, 340, 512, 222);
        GuideXosManagedWindowResizeTransactionC174 explorer = new(
            in ExplorerBounds, 800, 390, 960, 526);
        passed &= Case(ref cases, settings.Candidate.OuterWidth == 512 &&
            settings.PriorOuterWidth == 560 &&
            explorer.Candidate.OuterWidth == 960 &&
            explorer.PriorOuterWidth == 800 &&
            !settings.Complete(false, GuideXosResult.Success, false) &&
            explorer.Complete(true, GuideXosResult.Success, true) &&
            settings.RequiresRollback && explorer.IsAccepted);

        passed &= Case(ref cases, !accepted.Complete(true,
            GuideXosResult.Success, true));

        if (cases != CaseCount) passed = false;
        return passed;
    }

    public static bool RunStress(GuideXosHost host)
    {
        _ = host;
        StressAllocationBytes = -1;
        StressDriftCount = 0;
        StressRollbackCount = 0;

        int width = 560;
        int height = 340;
        for (int warmup = 0; warmup < 32; warmup++)
        {
            bool toMinimum = (warmup & 1) == 0;
            GuideXosManagedWindowResizeTransactionC174 transaction = new(
                in SettingsBounds, width, height,
                toMinimum ? 512 : 720, toMinimum ? 222 : 420);
            if (!transaction.Complete(true, GuideXosResult.Success, true))
                return false;
            width = transaction.Candidate.OuterWidth;
            height = transaction.Candidate.OuterHeight;
        }

        long allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
        for (int index = 0; index < StressGeometryTransitions; index++)
        {
            bool toMinimum = (index & 1) == 0;
            GuideXosManagedWindowResizeTransactionC174 transaction = new(
                in SettingsBounds, width, height,
                toMinimum ? 512 : 720, toMinimum ? 222 : 420);
            if (!transaction.IsValid || !transaction.IsChanged ||
                !transaction.Complete(true, GuideXosResult.Success, true))
            {
                ++StressDriftCount;
                continue;
            }
            width = transaction.Candidate.OuterWidth;
            height = transaction.Candidate.OuterHeight;
            if ((width != 512 && width != 720) ||
                (height != 222 && height != 420))
                ++StressDriftCount;
        }
        StressAllocationBytes = GC.GetAllocatedBytesForCurrentThread() -
            allocatedBefore;

        for (int index = 0; index < StressAlternatingCycles; index++)
        {
            int priorWidth = width;
            int priorHeight = height;
            GuideXosManagedWindowResizeTransactionC174 rollback = new(
                in SettingsBounds, priorWidth, priorHeight,
                priorWidth == 512 ? 720 : 512,
                priorHeight == 222 ? 420 : 222);
            if (rollback.Complete(true, GuideXosResult.CapabilityUnavailable,
                    true) || !rollback.RequiresRollback ||
                rollback.PriorOuterWidth != priorWidth ||
                rollback.PriorOuterHeight != priorHeight)
                ++StressDriftCount;
            else
                ++StressRollbackCount;
        }

        int settingsWidth = 560;
        int settingsHeight = 340;
        int explorerWidth = 800;
        int explorerHeight = 390;
        for (int index = 0; index < StressIsolationTransitions; index++)
        {
            GuideXosManagedWindowResizeTransactionC174 settingsTransaction =
                new(in SettingsBounds, settingsWidth, settingsHeight,
                    (index & 1) == 0 ? 512 : 720,
                    (index & 1) == 0 ? 222 : 420);
            GuideXosManagedWindowResizeTransactionC174 explorerTransaction =
                new(in ExplorerBounds, explorerWidth, explorerHeight,
                    (index & 1) == 0 ? 600 : 960,
                    (index & 1) == 0 ? 360 : 526);
            if (!settingsTransaction.Complete(true, GuideXosResult.Success,
                    true) || !explorerTransaction.Complete(true,
                    GuideXosResult.Success, true))
            {
                ++StressDriftCount;
                continue;
            }
            settingsWidth = settingsTransaction.Candidate.OuterWidth;
            settingsHeight = settingsTransaction.Candidate.OuterHeight;
            explorerWidth = explorerTransaction.Candidate.OuterWidth;
            explorerHeight = explorerTransaction.Candidate.OuterHeight;
            if (settingsWidth == explorerWidth ||
                settingsHeight == explorerHeight)
                ++StressDriftCount;
        }

        return StressAllocationBytes == 0 && StressDriftCount == 0 &&
            StressRollbackCount == StressAlternatingCycles &&
            settingsWidth == 720 && settingsHeight == 420 &&
            explorerWidth == 960 && explorerHeight == 526;
    }

    private static bool Case(ref int cases, bool condition)
    {
        ++cases;
        if (condition) return true;
        if (FirstFailedCase == 0) FirstFailedCase = cases;
        return false;
    }
}
#endif
