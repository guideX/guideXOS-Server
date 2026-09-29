using System;

namespace HostLogProof;

#if HOSTLOGPROOF_MANAGED_APP_RETURN
/// <summary>
/// The managed App Model keeps only its current application object. Starting
/// another app replaces that reference, so a return creates a new instance.
/// </summary>
internal sealed class GuideXosManagedApplicationLifetime
{
    private GuideXosApplication _active;
    private uint _activeSelector;
    private uint _generation;

    public bool TryStart(
        GuideXosApplicationDescriptor descriptor,
        out GuideXosApplication application,
        out uint generation)
    {
        _active = null;
        _activeSelector = 0u;
        application = descriptor.Factory?.Invoke();
        if (application == null)
        {
            generation = _generation;
            return false;
        }
        _active = application;
        _activeSelector = descriptor.Selector;
        generation = ++_generation;
        return true;
    }

    public bool TryGet(uint selector, out GuideXosApplication application)
    {
        application = _activeSelector == selector ? _active : null;
        return application != null;
    }

    public void Clear(uint selector)
    {
        if (_activeSelector != selector) return;
        _active = null;
        _activeSelector = 0u;
    }
}
#endif

#if HOSTLOGPROOF_C150_MANAGED_APP_RETURN
internal static class GuideXosManagedApplicationC150Tests
{
    private sealed class TestApplication : GuideXosApplication
    {
        public override GuideXosResult Launch(GuideXosHost host) =>
            GuideXosResult.Success;
    }

    public static bool Run()
    {
        GuideXosManagedApplicationLifetime lifetime = new();
        GuideXosApplicationDescriptor notes = new(
            4u, "Managed Notes"u8, static () => new TestApplication());
        GuideXosApplicationDescriptor settings = new(
            5u, "Managed Settings Center"u8,
            static () => new TestApplication());

        bool empty = !lifetime.TryGet(4u, out _);
        bool firstStart = lifetime.TryStart(notes, out GuideXosApplication first,
            out uint firstGeneration) && firstGeneration == 1u;
        bool currentAvailable = lifetime.TryGet(4u, out GuideXosApplication current) &&
            ReferenceEquals(first, current);
        bool wrongIdentityRejected = !lifetime.TryGet(5u, out _);
        bool replacement = lifetime.TryStart(settings,
            out GuideXosApplication second, out uint secondGeneration) &&
            secondGeneration == 2u && !ReferenceEquals(first, second);
        bool oldInstanceReleased = !lifetime.TryGet(4u, out _) &&
            lifetime.TryGet(5u, out current) && ReferenceEquals(second, current);
        lifetime.Clear(4u);
        bool unrelatedClearIgnored = lifetime.TryGet(5u, out current) &&
            ReferenceEquals(second, current);
        lifetime.Clear(5u);
        lifetime.Clear(5u);
        bool cleared = !lifetime.TryGet(5u, out _);

        return empty && firstStart && currentAvailable && wrongIdentityRejected &&
            replacement && oldInstanceReleased && unrelatedClearIgnored && cleared;
    }
}
#endif
