namespace Xmax.SDK;

public sealed record EncodingConfiguration(int MinimumBitrateKbps = -1, int MaximumBitrateKbps = 0,
    EncodingPreference Preference = EncodingPreference.Automatic);
