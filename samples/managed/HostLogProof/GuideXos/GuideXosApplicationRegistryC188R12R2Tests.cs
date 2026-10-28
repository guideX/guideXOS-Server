#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE && HOSTLOGPROOF_C188_EXTERNAL_APP_IDENTITY
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace HostLogProof;

public static unsafe class GuideXosApplicationRegistryC188R12R2Tests
{
    public const int CaseCount = 8;
    public static int FirstFailedCase { get; private set; }
    public static string FailureDetail { get; private set; } = string.Empty;

    private static readonly List<string> s_logs = new();
    private static ulong s_nextWindow = 0xC1881200u;
    private static bool s_rejectNextWindow;
    private static int s_windowRequests;
    private static string s_lastWindowTitle = string.Empty;
    private static int s_lastWindowWidth;
    private static int s_lastWindowHeight;

    public static bool Run()
    {
        FirstFailedCase = 0;
        FailureDetail = string.Empty;
        s_logs.Clear();
        s_rejectNextWindow = false;
        s_windowRequests = 0;
        s_lastWindowTitle = string.Empty;
        s_lastWindowWidth = 0;
        s_lastWindowHeight = 0;
        int cases = 0;
        bool passed = true;
        NativeHostCallTable table = CreateHostTable();

        NativeGxAppContext invalidTarget = CreateContext(&table, 10u, 3u, 0u);
        passed &= Case(ref cases,
            Program.DispatchC188ExternalAppIdentity(&invalidTarget) ==
                GxAbi.ErrorInvalidApplicationId);

        NativeGxAppContext missingRegistration = CreateContext(
            &table, 9u, 3u, 0u);
        passed &= Case(ref cases,
            GuideXosApplicationRegistry.Dispatch(&missingRegistration) ==
                GxAbi.ErrorInvalidApplicationId);

        NativeGxAppContext incompatibleRequest = CreateContext(
            &table, 5u, 3u, 0u);
        incompatibleRequest.size = 44u;
        incompatibleRequest.activationKind = GxAbi.ActivationKindDocument;
        passed &= Case(ref cases,
            GuideXosApplicationRegistry.Dispatch(&incompatibleRequest) ==
                (int)GuideXosResult.InvalidArgument);

        NativeGxAppContext closeNotes = CreateContext(&table, 4u, 2u,
            GxAbi.LaunchFlagAction |
                GuideXosApplicationRegistry.LifetimeCloseActionId);
        passed &= Case(ref cases,
            Program.DispatchC188ExternalAppIdentity(&closeNotes) == 0 &&
            !GuideXosApplicationRegistry.C188R11TryGetResident(4u, 2u,
                out _));

        s_logs.Clear();
        NativeGxAppContext settingsLaunch = CreateContext(
            &table, 5u, 3u, 0u);
        int settingsResult =
            Program.DispatchC188ExternalAppIdentity(&settingsLaunch);
        bool settingsResident =
            GuideXosApplicationRegistry.C188R11TryGetResident(5u, 3u,
                out GuideXosApplication settings) &&
            settings is Applications.ManagedSettingsCenter;
        bool settingsSurfaceRequested = s_windowRequests == 1 &&
            s_lastWindowTitle == "Managed Settings Center" &&
            s_lastWindowWidth == Applications.ManagedSettingsCenter.SurfaceWidth &&
            s_lastWindowHeight == Applications.ManagedSettingsCenter.SurfaceHeight;
        passed &= Case(ref cases,
            settingsResult == (int)GuideXosResult.Success &&
            settingsResident && settingsSurfaceRequested);

        int lifetimeBegin = FindLog("stage=lifetime-start"u8, "result=BEGIN"u8);
        int lifetimePass = FindLog("stage=lifetime-start"u8, "result=PASS"u8);
        int applicationEntry = FindLog("stage=application-entry"u8,
            "result=BEGIN"u8);
        int applicationReturn = FindLog("stage=application-return"u8,
            "result=PASS"u8);
        int dispatchReturn = FindLog("stage=dispatch-return"u8,
            "result=PASS"u8);
        bool markerOrderPassed = lifetimeBegin >= 0 &&
            lifetimePass > lifetimeBegin && applicationEntry > lifetimePass &&
            applicationReturn > applicationEntry &&
            dispatchReturn > applicationReturn;
        if (!markerOrderPassed)
        {
            FailureDetail = $"markers={lifetimeBegin},{lifetimePass},{applicationEntry},{applicationReturn},{dispatchReturn}; " +
                string.Join(" | ", s_logs.FindAll(line =>
                    line.Contains("C188R12R2_LAUNCH", StringComparison.Ordinal)));
        }
        passed &= Case(ref cases, markerOrderPassed);

        NativeGxAppContext closeSettings = CreateContext(&table, 5u, 3u,
            GxAbi.LaunchFlagAction |
                GuideXosApplicationRegistry.LifetimeCloseActionId);
        passed &= Case(ref cases,
            Program.DispatchC188ExternalAppIdentity(&closeSettings) == 0 &&
            !GuideXosApplicationRegistry.C188R11TryGetResident(5u, 3u,
                out _));

        s_logs.Clear();
        s_rejectNextWindow = true;
        NativeGxAppContext rejectedSettings = CreateContext(
            &table, 5u, 4u, 0u);
        int rejectedResult =
            Program.DispatchC188ExternalAppIdentity(&rejectedSettings);
        bool rejectionReturnedExplicitly =
            rejectedResult == (int)GuideXosResult.SurfaceCreationFailed &&
            !GuideXosApplicationRegistry.C188R11TryGetResident(5u, 4u,
                out _) &&
            FindLog("stage=application-entry"u8, "result=BEGIN"u8) >= 0 &&
            FindLog("stage=application-return"u8, "result=FAIL"u8) >= 0 &&
            FindLog("stage=dispatch-return"u8, "result=FAIL"u8) >= 0;
        passed &= Case(ref cases, rejectionReturnedExplicitly);

        if (!passed && FirstFailedCase == 0) FirstFailedCase = cases;
        return passed && cases == CaseCount;
    }

    private static int FindLog(ReadOnlySpan<byte> stage,
        ReadOnlySpan<byte> result)
    {
        for (int index = 0; index < s_logs.Count; index++)
        {
            string line = s_logs[index];
            if (Contains(line, stage) && Contains(line, result)) return index;
        }
        return -1;
    }

    private static bool Contains(string value, ReadOnlySpan<byte> sought) =>
        value.Contains(Encoding.UTF8.GetString(sought),
            StringComparison.Ordinal);

    private static bool Case(ref int count, bool result)
    {
        count++;
        if (!result && FirstFailedCase == 0) FirstFailedCase = count;
        return result;
    }

    private static NativeHostCallTable CreateHostTable()
    {
        NativeHostCallTable table = default;
        table.size = GxAbi.HostCallTableSize;
        table.version = GxAbi.HostAbiVersion;
        table.log = &Log;
        table.requestWindow = &RequestWindow;
        table.drawText = &DrawText;
        table.drawRect = &DrawRect;
        table.addButton = &AddButton;
        table.closeWindow = &CloseWindow;
        table.addActionButton = &AddActionButton;
        table.capabilities = (ulong)(GuideXosCapability.Surface |
            GuideXosCapability.Text | GuideXosCapability.Primitive |
            GuideXosCapability.Action | GuideXosCapability.Close |
            GuideXosCapability.LaunchContext | GuideXosCapability.Log);
        return table;
    }

    private static NativeGxAppContext CreateContext(
        NativeHostCallTable* table, uint selector, uint generation,
        uint launchFlags)
    {
        NativeGxAppContext context = default;
        context.size = GxAbi.AppContextLaunchPrefixSize;
        context.apiVersion = GxAbi.ApiVersion;
        context.host = table;
        context.userData = (void*)(nuint)(((ulong)generation << 32) | selector);
        context.launchFlags = launchFlags;
        return context;
    }

    [UnmanagedCallersOnly]
    private static int Log(NativeGxAppContext* context, byte* message)
    {
        if (message == null) return -1;
        int length = 0;
        while (message[length] != 0) length++;
        s_logs.Add(Encoding.UTF8.GetString(new ReadOnlySpan<byte>(message,
            length)));
        return 0;
    }

    [UnmanagedCallersOnly]
    private static int RequestWindow(NativeGxAppContext* context,
        byte* title, int width, int height, ulong* window)
    {
        if (window == null) return -2;
        s_windowRequests++;
        s_lastWindowTitle = ReadUtf8(title);
        s_lastWindowWidth = width;
        s_lastWindowHeight = height;
        if (s_rejectNextWindow)
        {
            s_rejectNextWindow = false;
            *window = 0u;
            return -5;
        }
        *window = s_nextWindow++;
        return 0;
    }

    private static string ReadUtf8(byte* value)
    {
        if (value == null) return string.Empty;
        int length = 0;
        while (value[length] != 0) length++;
        return Encoding.UTF8.GetString(new ReadOnlySpan<byte>(value, length));
    }

    [UnmanagedCallersOnly]
    private static int DrawText(NativeGxAppContext* context, ulong window,
        int x, int y, byte* text) => 0;

    [UnmanagedCallersOnly]
    private static int DrawRect(NativeGxAppContext* context, ulong window,
        int x, int y, int width, int height, uint color) => 0;

    [UnmanagedCallersOnly]
    private static int AddButton(NativeGxAppContext* context, ulong window,
        int x, int y, int width, int height, byte* label, int* widget)
    {
        if (widget == null) return -2;
        *widget = 1;
        return 0;
    }

    [UnmanagedCallersOnly]
    private static int AddActionButton(NativeGxAppContext* context,
        ulong window, int x, int y, int width, int height, byte* label,
        uint action, int* widget)
    {
        if (widget == null) return -2;
        *widget = 1;
        return 0;
    }

    [UnmanagedCallersOnly]
    private static int CloseWindow(NativeGxAppContext* context, ulong window) => 0;
}
#endif
