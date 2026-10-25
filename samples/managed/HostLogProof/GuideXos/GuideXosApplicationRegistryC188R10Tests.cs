#if HOSTLOGPROOF_C188R10_MANAGED_STATE_REENTRY
using System;
using System.Runtime.InteropServices;

namespace HostLogProof;

public static unsafe class GuideXosApplicationRegistryC188R10Tests
{
    public const int CaseCount = 14;
    public static int FirstFailedCase { get; private set; }
    private static int s_notesHandlerEntries;
    private static ulong s_nextWindow = 0xC1880010u;

    public static bool Run()
    {
        FirstFailedCase = 0;
        s_notesHandlerEntries = 0;
        int cases = 0;
        bool passed = true;

        bool descriptors = GuideXosApplicationRegistry.RegistrationCount == 5;
        for (uint selector = 4u; selector <= 8u; selector++)
        {
            descriptors &= GuideXosApplicationRegistry.TryFind(selector,
                out GuideXosApplicationDescriptor descriptor) &&
                descriptor.Selector == selector;
        }
        passed &= Case(ref cases, descriptors);

        passed &= Case(ref cases,
            !GuideXosApplicationRegistry.C188R10TryGetResident(4u, 1u,
                out _));

        NativeHostCallTable table = CreateHostTable();
        NativeGxAppContext context = CreateContext(&table, 4u, 1u, 0u);
        int launchResult = Program.DispatchC188ExternalAppIdentity(&context);
        passed &= Case(ref cases,
            launchResult == (int)GuideXosResult.Success);

        bool launchResident = GuideXosApplicationRegistry.C188R10TryGetResident(
            4u, 1u, out GuideXosApplication notes) &&
            notes is Applications.ManagedNotes;
        passed &= Case(ref cases, launchResident);

        uint registryOwner =
            GuideXosApplicationRegistry.C188R10RegistryOwnerIdentity;
        uint lifetimeOwner =
            GuideXosApplicationRegistry.C188R10LifetimeOwnerIdentity;
        passed &= Case(ref cases, registryOwner != 0u && lifetimeOwner != 0u &&
            GuideXosApplicationRegistry.
                C188R10RegistryInitializationGeneration == 1u);

        bool repeatedCallbacks = true;
        for (int invocation = 0; invocation < 3; invocation++)
        {
            context = CreateContext(&table, 4u, 1u,
                GxAbi.LaunchFlagInput | GxAbi.LaunchFlagInputKeyChar |
                    (uint)'x');
            int callbackResult =
                Program.DispatchC188ExternalAppIdentity(&context);
            repeatedCallbacks &= callbackResult == (int)GuideXosResult.Success;
        }
        passed &= Case(ref cases, repeatedCallbacks &&
            s_notesHandlerEntries == 3 &&
            GuideXosApplicationRegistry.C188R10TryGetResident(4u, 1u,
                out GuideXosApplication afterInput) &&
            ReferenceEquals(notes, afterInput));

        passed &= Case(ref cases,
            GuideXosApplicationRegistry.C188R10RegistryOwnerIdentity ==
                registryOwner &&
            GuideXosApplicationRegistry.C188R10LifetimeOwnerIdentity ==
                lifetimeOwner &&
            GuideXosApplicationRegistry.
                C188R10RegistryInitializationGeneration == 1u);

        context = CreateContext(&table, 4u, 2u,
            GxAbi.LaunchFlagInput | GxAbi.LaunchFlagInputKeyChar |
                (uint)'s');
        bool staleGenerationRejected =
            Program.DispatchC188ExternalAppIdentity(&context) ==
                GxAbi.ErrorInvalidApplicationId &&
            GuideXosApplicationRegistry.C188R10TryGetResident(4u, 1u,
                out GuideXosApplication stillNotes) &&
            ReferenceEquals(notes, stillNotes);
        passed &= Case(ref cases, staleGenerationRejected);

        context = CreateContext(&table, 5u, 1u,
            GxAbi.LaunchFlagInput | GxAbi.LaunchFlagInputKeyChar |
                (uint)'z');
        bool crossSelectorRejected =
            Program.DispatchC188ExternalAppIdentity(&context) ==
                GxAbi.ErrorInvalidApplicationId &&
            s_notesHandlerEntries == 3 &&
            GuideXosApplicationRegistry.C188R10TryGetResident(4u, 1u,
                out GuideXosApplication sameNotes) &&
            ReferenceEquals(notes, sameNotes);
        passed &= Case(ref cases, crossSelectorRejected);

        context = CreateContext(&table, 4u, 1u,
            GxAbi.LaunchFlagAction |
                GuideXosApplicationRegistry.LifetimeCloseActionId);
        bool closed = Program.DispatchC188ExternalAppIdentity(&context) == 0 &&
            !GuideXosApplicationRegistry.C188R10TryGetResident(4u, 1u,
                out _);
        passed &= Case(ref cases, closed);

        context = CreateContext(&table, 4u, 2u, 0u);
        int reopenResult = Program.DispatchC188ExternalAppIdentity(&context);
        GuideXosApplication reopenedNotes = null;
        bool reopened = reopenResult == (int)GuideXosResult.Success &&
            GuideXosApplicationRegistry.C188R10TryGetResident(4u, 2u,
                out reopenedNotes) &&
            reopenedNotes is Applications.ManagedNotes &&
            !ReferenceEquals(notes, reopenedNotes) &&
            !GuideXosApplicationRegistry.C188R10TryGetResident(4u, 1u,
                out _);
        passed &= Case(ref cases, reopened);

        context = CreateContext(&table, 4u, 2u,
            GxAbi.LaunchFlagInput | GxAbi.LaunchFlagInputKeyChar |
                (uint)'r');
        int reopenInputResult =
            Program.DispatchC188ExternalAppIdentity(&context);
        passed &= Case(ref cases, reopenInputResult == 0 &&
            s_notesHandlerEntries == 4 &&
            GuideXosApplicationRegistry.C188R10TryGetResident(4u, 2u,
                out GuideXosApplication currentNotes) &&
            ReferenceEquals(reopenedNotes, currentNotes));

        passed &= Case(ref cases,
            GuideXosApplicationRegistry.C188R10RegistryOwnerIdentity ==
                registryOwner &&
            GuideXosApplicationRegistry.C188R10LifetimeOwnerIdentity ==
                lifetimeOwner && GuideXosApplicationRegistry.TryFind(8u,
                out GuideXosApplicationDescriptor fileExplorer) &&
            fileExplorer.Selector == 8u);

        passed &= Case(ref cases,
            GuideXosApplicationRegistry.RegistrationCount == 5 &&
            GuideXosApplicationRegistry.TryFind(4u, out _) &&
            GuideXosApplicationRegistry.TryFind(5u, out _) &&
            GuideXosApplicationRegistry.TryFind(6u, out _) &&
            GuideXosApplicationRegistry.TryFind(7u, out _) &&
            GuideXosApplicationRegistry.TryFind(8u, out _));

        if (!passed && FirstFailedCase == 0) FirstFailedCase = cases;
        return passed && cases == CaseCount;
    }

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
        NativeHostCallTable* table,
        uint selector,
        uint generation,
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
        ReadOnlySpan<byte> handler = "C188R10 state stage=handler-enter"u8;
        int index = 0;
        while (index < handler.Length && message[index] == handler[index]) index++;
        if (index == handler.Length) s_notesHandlerEntries++;
        return 0;
    }

    [UnmanagedCallersOnly]
    private static int RequestWindow(NativeGxAppContext* context,
        byte* title, int width, int height, ulong* window)
    {
        if (window == null) return -2;
        *window = s_nextWindow++;
        return 0;
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
