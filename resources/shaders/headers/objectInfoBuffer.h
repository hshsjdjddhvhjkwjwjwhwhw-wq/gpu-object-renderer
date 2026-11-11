#define A_COLOR_CHANNEL_IS_SPRITE_DETAIL 0x1000

#define OBJECT_FLAG_USES_AUDIO_SCALE      (1u << 0u)
#define OBJECT_FLAG_CUSTOM_AUDIO_SCALE    (1u << 1u)
#define OBJECT_FLAG_IS_ORB                (1u << 2u)
#define OBJECT_FLAG_IS_INVISIBLE_BLOCK    (1u << 3u)
#define OBJECT_FLAG_SPECIAL_GLOW_COLOR    (1u << 4u)
#define OBJECT_FLAG_IS_GLOW_COLOR_LBG     (1u << 5u)
#define OBJECT_FLAG_HAS_BASE_HSV          (1u << 6u)
#define OBJECT_FLAG_HAS_DETAIL_HSV        (1u << 7u)
#define OBJECT_FLAG_IS_STATIC_OBJECT      (1u << 8u)
#define OBJECT_FLAG_HAS_STATIC_GLOW_COLOR (1u << 9u)

/*
    Rendering info of an object that never changes.
*/
struct ObjectInfo {
    float rotationSpeed;
// Flags are the macro variables beginning with OBJECT_FLAG_*
    uint flags;
    float audioScaleMin;
    float audioScaleMax;
    float fadeMargin;
    uint opacityValues;
    uint baseColorChannelId;
    PackedHSV baseHSV;
    uint detailColorChannelId;
    PackedHSV detailHSV;
    RGBA staticGlowColor;
};

/*
    This is the buffer that contains all the info of
    every object required in shader processing
*/
STORAGE_BUFFER(OBJECT_INFO_BUFFER_BINDING) ObjectInfoBuffer {
    ObjectInfo u_objects[CPP_ONLY(0)];
};