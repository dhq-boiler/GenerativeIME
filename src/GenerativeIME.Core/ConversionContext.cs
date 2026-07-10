namespace GenerativeIME.Core;

public sealed record ConversionContext(
    string LineBeforeCursor,
    string LineAfterCursor,
    string Reading);