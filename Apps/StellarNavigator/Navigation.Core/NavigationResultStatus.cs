namespace Navigation.Core;

public enum NavigationResultStatus
{
    ValidatedReference,
    ApproximateOrSimulated,
    StaleData,
    InsufficientInformation,
    UnsupportedCalculation,
    InvalidInput
}
