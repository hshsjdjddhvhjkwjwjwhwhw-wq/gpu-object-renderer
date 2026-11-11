#define COLOR_CHANNEL_COUNT 1101

#define COLOR_CHANNEL_BG      1000u
#define COLOR_CHANNEL_G1      1001u
#define COLOR_CHANNEL_LINE    1002u
#define COLOR_CHANNEL_3DL     1003u
#define COLOR_CHANNEL_OBJ     1004u
#define COLOR_CHANNEL_P1      1005u
#define COLOR_CHANNEL_P2      1006u
#define COLOR_CHANNEL_LBG     1007u
#define COLOR_CHANNEL_G2      1009u
#define COLOR_CHANNEL_BLACK   1010u
#define COLOR_CHANNEL_WHITE   1011u
#define COLOR_CHANNEL_LIGHTER 1012u
#define COLOR_CHANNEL_MG      1013u
#define COLOR_CHANNEL_MG2     1014u

STORAGE_BUFFER(COLOR_CHANNEL_BUFFER_BINDING) ColorChannelBuffer {
    RGBA u_channelColors[COLOR_CHANNEL_COUNT];
};

#ifdef GLSL

vec4 getColorChannelColor(uint colorChannel) {
    return unpackUnorm4x8(u_channelColors[colorChannel]);
}

#endif // GLSL