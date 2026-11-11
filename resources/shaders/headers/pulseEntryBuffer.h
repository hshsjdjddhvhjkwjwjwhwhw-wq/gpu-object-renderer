#define PULSE_FLAG_IS_BASE_ONLY   (1u << 26u)
#define PULSE_FLAG_IS_DETAIL_ONLY (1u << 27u)
#define PULSE_FLAG_IS_HSV         (1u << 28u)
#define PULSE_FLAG_IS_DYNAMIC_HSV (1u << 29u)
#define PULSE_FLAG_IS_ACTIVE      (1u << 30u)
#define PULSE_FLAG_GET_VALUE(F) (float((F) & 0xffffu) / 65535.0f)
#define PULSE_FLAG_GET_COLOR_CHANNEL(F) (((F) >> 16u) & 0x3ffu)

struct PulseGroupEntry {
    uint flags;
    uint colorOrHSV;
    uint hsvSatAndVal;
};

STORAGE_BUFFER(PULSE_ENTRY_BUFFER_BINDING) PulseEntryBuffer {
    PulseGroupEntry u_pulseEntries[CPP_ONLY(0)];
};

#ifdef GLSL

vec3 applyPulseEntry(vec3 color, PulseGroupEntry entry);

vec3 applyPulseEntries(vec3 color, uint pulseIndex, bool isBaseColor) {
    for (;; pulseIndex++) {
        uint flags = u_pulseEntries[pulseIndex].flags;
        if (flags == 0u)
            break;

        if ((flags & PULSE_FLAG_IS_BASE_ONLY) != 0 && !isBaseColor)
            continue;

        if ((flags & PULSE_FLAG_IS_DETAIL_ONLY) != 0 && isBaseColor)
            continue;

        color = applyPulseEntry(color, u_pulseEntries[pulseIndex]);
    }

    return color;
}

vec3 applyPulseEntry(vec3 color, PulseGroupEntry entry) {
    float value = PULSE_FLAG_GET_VALUE(entry.flags);

    if (value <= 0.0f)
        return color;

    if ((entry.flags & PULSE_FLAG_IS_HSV) != 0u) {
        uint colorChannel = PULSE_FLAG_GET_COLOR_CHANNEL(entry.flags);

        PackedHSV packedHsv;
        packedHsv.hueAndFlags = entry.colorOrHSV;
        packedHsv.satAndVal = entry.hsvSatAndVal;

        HSVStruct hsv = readHSV(packedHsv);

        vec3 srcColor = color;
        if (colorChannel != 0u)
            srcColor = getColorChannelColor(colorChannel).rgb;

        if (value == 1.0f)
            return applyHSVStruct(hsv, vec4(srcColor, 1.0)).rgb;

        if ((entry.flags & PULSE_FLAG_IS_DYNAMIC_HSV) != 0u)
            hsv = getMultipliedHSV(hsv, value);

        vec3 dstColor = applyHSVStruct(hsv, vec4(srcColor, 1.0)).rgb;

        return mix(color, dstColor, value);
    }

    vec3 dstColor = unpackUnorm4x8(entry.colorOrHSV).rgb;

    return mix(color, dstColor, value);
}

#endif // GLSL