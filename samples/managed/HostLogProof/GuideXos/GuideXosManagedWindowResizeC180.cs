namespace HostLogProof;

/// <summary>Routes the existing C172 action through explicit app opt-in.</summary>
internal static class GuideXosManagedWindowResizeDispatchC180
{
    internal static GuideXosResult DispatchAction(
        GuideXosApplication application, GuideXosHost host, uint actionId)
    {
        if (!GuideXosManagedWindowGeometryC172.TryDecodeAction(actionId,
                out int outerWidth, out int outerHeight))
            return application.HandleAction(host, actionId);

        if (!application.TryGetManagedResizeGeometryC180(
                out GuideXosManagedWindowResizeBoundsC174 bounds,
                out int currentOuterWidth, out int currentOuterHeight))
            return GuideXosResult.InvalidAction;
        GuideXosManagedWindowResizeTransactionC174 transaction = new(
            in bounds, currentOuterWidth, currentOuterHeight, outerWidth,
            outerHeight);
        if (!transaction.IsValid)
        {
#if GUIDEXOS_PROOF_CONTROL
            host?.TryLog("C180-MANAGED-GEOMETRY result=REJECT layout=unchanged"u8);
#endif
            return transaction.Result;
        }
        if (!transaction.IsChanged)
            return transaction.Result;
        return application.HandleManagedResizeC180(host, ref transaction);
    }
}
