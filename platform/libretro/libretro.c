#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include <stdio.h>
#if defined(_WIN32) && !defined(_XBOX)
#include <windows.h>
#endif
#include "libretro.h"

#include "globaldefs.h"
#include "libretro_main.h" // Emulator wrapper

#include <valgrind/callgrind.h>

#define VIDEO_WIDTH DEFAULT_SCREEN_WIDTH
#define VIDEO_HEIGHT DEFAULT_SCREEN_HEIGHT
#define VIDEO_PIXELS VIDEO_WIDTH * VIDEO_HEIGHT

#define FPS 50
#define SAMPLERATE 48000

#define SAMPLES_PER_FRAME (SAMPLERATE/FPS)

#define BLKVVOD 1
#define BLKSBR 0

#define EXTENSIONS "rom|r0m|fdd|edd|wav"

static uint8_t *frame_buf;
static int16_t *audio_buf;

static float *float_audio_buf;

static FILE * raw_audiof = NULL;

static int blkvvod_delay_frames = 0;

static struct retro_log_callback logging;
static retro_log_printf_t log_cb;
static bool use_audio_cb;
char retro_base_directory[4096];
char retro_game_path[4096];

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
    (void)level;
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
}


static retro_environment_t environ_cb;

///* Callback type passed in RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK.
// * Called by the frontend in response to keyboard events.
// * down is set if the key is being pressed, or false if it is being released.
// * keycode is the RETROK value of the char.
// * character is the text character of the pressed key. (UTF-32).
// * key_modifiers is a set of RETROKMOD values or'ed together.
// *
// * The pressed/keycode state can be indepedent of the character.
// * It is also possible that multiple characters are generated from a
// * single keypress.
// * Keycode events should be treated separately from character events.
// * However, when possible, the frontend should try to synchronize these.
// * If only a character is posted, keycode should be RETROK_UNKNOWN.
// *
// * Similarily if only a keycode event is generated with no corresponding
// * character, character should be 0.
// */
//typedef void (RETRO_CALLCONV *retro_keyboard_event_t)(bool down, unsigned keycode,
//      uint32_t character, uint16_t key_modifiers);


void RETRO_CALLCONV retro_keyboard_event(bool down, unsigned keycode, uint32_t character, uint16_t key_modifiers)
{
    if (log_cb) {
        log_cb(RETRO_LOG_DEBUG, "retro_keyboard_event: down=%d keycode=%d char=%d modifiers=$%04x\n",
                down, keycode, character, key_modifiers);
    }
}

void retro_init(void)
{
    log_cb(RETRO_LOG_INFO, "retro_init:\n");

    // frame_buf = (uint8_t*)malloc(VIDEO_PIXELS * sizeof(uint32_t));
    //
    audio_buf = (int16_t*)malloc(SAMPLES_PER_FRAME * 2 * sizeof(int16_t));
    float_audio_buf = (float*)malloc(SAMPLES_PER_FRAME * 2 * sizeof(float));

    Emulator_Init();
    frame_buf = (uint8_t *)Emulator_GetPixels();

    const char *dir = NULL;
    if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir)
    {
        snprintf(retro_base_directory, sizeof(retro_base_directory), "%s", dir);
    }

    static struct retro_keyboard_callback keyboard_callback = {retro_keyboard_event};
    environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &keyboard_callback);
}

void retro_deinit(void)
{
    //free(frame_buf);
    //frame_buf = NULL;

    free(audio_buf);
    audio_buf = NULL;

    free(float_audio_buf);
    float_audio_buf = NULL;

    if (raw_audiof) {
        fclose(raw_audiof);
        free(raw_audiof);
        raw_audiof = NULL;
    }
}

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
    log_cb(RETRO_LOG_INFO, "Plugging device %u into port %u.\n", device, port);
}

void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof(*info));
    info->library_name     = "v06x";
#ifndef GIT_VERSION
#define GIT_VERSION "_3"
#endif
    info->library_version  = "0.9" GIT_VERSION;
    info->need_fullpath    = false;
    info->valid_extensions = EXTENSIONS;
}

static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    info->geometry.base_width   = VIDEO_WIDTH;
    info->geometry.base_height  = VIDEO_HEIGHT;
    info->geometry.max_width    = VIDEO_WIDTH;
    info->geometry.max_height   = VIDEO_HEIGHT;
    info->geometry.aspect_ratio = 5.f/4.f;

    info->timing.fps = 60; //FPS;//3e6/59904;
    info->timing.sample_rate = SAMPLERATE;
}


static const struct retro_system_content_info_override content_overrides[] = {
    {
        EXTENSIONS, /* extensions */
        false,     /* need_fullpath */
        false      /* persistent_data */
    },
    { NULL, false, false }
};

void retro_set_environment(retro_environment_t cb)
{
    environ_cb = cb;

    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging)) {
        log_cb = logging.log;
        if (log_cb)
            log_cb(RETRO_LOG_INFO, "retro_set_environment: using logging from env\n");
    } else {
        log_cb = fallback_log;
        if (log_cb)
            log_cb(RETRO_LOG_INFO, "retro_set_environment: using fallback log\n");
    }

    // why not start without a game
    bool no_rom = true;
    environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_rom);

//
//    static const struct retro_controller_description controllers[] = {
//        { "Nintendo DS", RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0) },
//    };
//
//    static const struct retro_controller_info ports[] = {
//        { controllers, 1 },
//        { NULL, 0 },
//    };

    static const struct retro_controller_description port_user[] = {
        { "None",              RETRO_DEVICE_NONE },
        { "Joystick",          RETRO_DEVICE_JOYPAD },
        { 0 },
    };

    static const struct retro_controller_description port_kbd[] = {
        { "Keyboard",         RETRO_DEVICE_KEYBOARD },
        { "Joystick",          RETRO_DEVICE_JOYPAD },
        { 0 },
    };

    static struct retro_controller_info ports[] =
    {
        {
            .types = port_kbd,
            .num_types = 2
        },
        {
            .types = port_user,
            .num_types = 3
        },
        {
            NULL, 0
        }
    };


    environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);

    environ_cb(RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE, (void*)content_overrides);
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
    audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
    input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
    input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
    video_cb = cb;
}


void retro_reset(void)
{
    //Emulator_Reset(BLKVVOD);
    blkvvod_delay_frames = 20;
}

static uint8_t keydown[350];

static void key_make(int key)
{
    if (!keydown[key]) {
        Emulator_KeyDown(key);
        keydown[key] = 1;
    }
}

static void key_break(int key)
{
    keydown[key] = 0;
    Emulator_KeyUp(key);
}

static void joy_key(int button, int key)
{
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, button)) {
        key_make(key);
    }
    else if (keydown[button]) {
        key_break(key);
    }
}

static void update_input(void)
{
    input_poll_cb();

    //if (game_data && framectr > 2) {
    //}
    //
    for (int key = 0; key < 350; ++key)  // Safe key range
    {
        if (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, key))
        {
            key_make(key);
        }
        else if (keydown[key]) {
            key_break(key);
        }
    }

    joy_key(RETRO_DEVICE_ID_JOYPAD_SELECT, RETROK_F1);
    joy_key(RETRO_DEVICE_ID_JOYPAD_START, RETROK_F12);

    joy_key(RETRO_DEVICE_ID_JOYPAD_L, RETROK_F6);           // rus
    joy_key(RETRO_DEVICE_ID_JOYPAD_L2, RETROK_LSHIFT);      // ss/shift
    joy_key(RETRO_DEVICE_ID_JOYPAD_R2, RETROK_LCTRL);       // us/ctrl
    joy_key(RETRO_DEVICE_ID_JOYPAD_R, RETROK_RALT);         // ps

    joy_key(RETRO_DEVICE_ID_JOYPAD_X, RETROK_RETURN);       // vk
    joy_key(RETRO_DEVICE_ID_JOYPAD_Y, RETROK_SPACE);        // probl

    if (blkvvod_delay_frames) {
        --blkvvod_delay_frames;
        if (blkvvod_delay_frames == 0) {
            //key_make(RETROK_F11); // blk+vvod F11
            Emulator_Reset(BLKVVOD);
        }
    }

    // sticks
    uint8_t state[2];
    for (int pad = 0; pad < 2; ++pad) {
        state[pad] = 0xff; 
                                    //port device              index  id
        state[pad] &= ~(input_state_cb(pad, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT) << 0);
        state[pad] &= ~(input_state_cb(pad, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT) << 1);
        state[pad] &= ~(input_state_cb(pad, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP) << 2);
        state[pad] &= ~(input_state_cb(pad, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN) << 3);
        state[pad] &= ~(input_state_cb(pad, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A) << 6);
        state[pad] &= ~(input_state_cb(pad, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B) << 7);
    }

    Emulator_SetJoysticks(state[0], state[1]);
    //log_cb(RETRO_LOG_DEBUG, "state0=%02x state1=%02x\n", state[0], state[1]);
}


static void check_variables(void)
{

}

static void audio_set_state(bool enable)
{
    (void)enable;
}

void record_audio(uint16_t * buf, size_t nframes)
{
    if (raw_audiof == NULL) 
        raw_audiof = fopen("audio.raw", "wb");
    //fwrite(buf, nframes * 2, 1, raw_audiof);
    fwrite(float_audio_buf, 2 * 960 * sizeof(float), 1, raw_audiof);
}

static void convert_audio()
{
    for (size_t i = 0; i < 2 * SAMPLES_PER_FRAME; ++i) {
        float samp = float_audio_buf[i];
        int16_t isamp = samp * 16384;
        audio_buf[i] = isamp;
    }
}

static int cadence_pos = 0;

void shit_audio()
{
    static int pha = 0;
    for (int i = 0; i < 960; ++i) {
        pha = (pha + 1) % 110;
        audio_buf[i*2] = pha * 5;
        audio_buf[i*2+1] = pha * 5;
    }
}



void retro_run(void)
{
    CALLGRIND_START_INSTRUMENTATION;
    CALLGRIND_TOGGLE_COLLECT;

    update_input();

    bool updated = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
        check_variables();


    cadence_pos = (cadence_pos + 1) % 6;
    if (cadence_pos == 1) {
        video_cb(frame_buf, VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_WIDTH * sizeof(uint32_t));
    }
    else {
        Emulator_ExecuteFrame(float_audio_buf);

        video_cb(frame_buf, VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_WIDTH * sizeof(uint32_t));

        convert_audio();

        audio_batch_cb(audio_buf, SAMPLES_PER_FRAME);

        //record_audio(audio_buf, SAMPLES_PER_FRAME);

        //shit_audio();
        //audio_batch_cb(audio_buf, 960);
    }

    CALLGRIND_TOGGLE_COLLECT;
    CALLGRIND_STOP_INSTRUMENTATION;

}

        // port, device, index, id, description 
#define DESCRIPTOR_BLOCK(user) \
        { user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left" },\
        { user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up" },\
        { user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down" },\
        { user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },\
        { user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },\
        { user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "A" },\
        { user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "РУС" },\
        { user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,     "CC" },\
        { user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,     "УС" },\
        { user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "ПС" },\
        { user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "ВК" }, \
        { user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Space"}, \
        { user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Reset (БЛК+СБР)" }, \
        { user, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,"F1" }

bool retro_load_game(const struct retro_game_info *info)
{
    struct retro_input_descriptor descriptors[] = {
        DESCRIPTOR_BLOCK(0),
        DESCRIPTOR_BLOCK(1),
        { 0 }
    };

    environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, descriptors);

    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
    {
        log_cb(RETRO_LOG_INFO, "XRGB8888 is not supported.\n");
        return false;
    }

    const struct retro_game_info_ext *info_ext = NULL;
    const char *extension = NULL;

    if (!info) {
        Emulator_Reset(BLKVVOD);
        return true;
    }

    if (environ_cb(RETRO_ENVIRONMENT_GET_GAME_INFO_EXT, &info_ext) &&
            info_ext && (info_ext->file_in_archive ? info_ext->archive_file : info_ext->full_path)) {
        extension = strrchr(info_ext->file_in_archive ? info_ext->archive_file : info_ext->full_path, '.');
    } else if (info->path) {
        extension = strrchr(info->path, '.');
    }

    snprintf(retro_game_path, sizeof(retro_game_path) - 1, "%s", info->path);

    int loadkind = -1;
    int org = 0x100;
    if (extension && strcasecmp(extension, ".rom") == 0) {
        loadkind = LOADKIND_ROM;
        org = 0x100;
    }
    else if (extension && strcasecmp(extension, ".r0m") == 0) {
        loadkind = LOADKIND_ROM;
        org = 0x0;
    }
    else if (extension && strcasecmp(extension, ".fdd") == 0) {
        loadkind = LOADKIND_FDD;
        org = 0x0;
    }
    else if (extension && strcasecmp(extension, ".edd") == 0) {
        loadkind = LOADKIND_EDD;
    }
    else if (extension && strcasecmp(extension, ".wav") == 0) {
        loadkind = LOADKIND_WAV;
    }

    Emulator_LoadAsset(info->data, info->size, loadkind, org);
    if (loadkind == LOADKIND_ROM || loadkind == LOADKIND_EDD) {
        Emulator_Reset(BLKSBR);
    }


    //struct retro_audio_callback audio_cb = { audio_callback, audio_set_state };
    //use_audio_cb = environ_cb(RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK, &audio_cb);

    check_variables();

    (void)info;
    return true;
}

void retro_unload_game(void)
{

}

unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC; //RETRO_REGION_PAL;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
    return false;
}

size_t retro_serialize_size(void)
{
    size_t sz = Emulator_ExportState(NULL, 0);
    log_cb(RETRO_LOG_DEBUG, "retro_serialize_size: %I64u\n", sz);
    return sz;
}

bool retro_serialize(void *data, size_t size)
{
    size_t sz = Emulator_ExportState(data, size);
    log_cb(RETRO_LOG_DEBUG, "retro_serialize: set size=%I64u result size=%I64u\n", size, sz);
    return sz <= size;
}

bool retro_unserialize(const void *data, size_t size)
{
    bool res =  Emulator_RestoreState(data, size);
    log_cb(RETRO_LOG_DEBUG, "retro_unserialize: size=%I64u result=%d\n", size, res);
    return res;
}

void *retro_get_memory_data(unsigned id)
{
    if (id == RETRO_MEMORY_SYSTEM_RAM) {
        return Emulator_GetMemory();
    }
    return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
    if (id == RETRO_MEMORY_SYSTEM_RAM) {
        return Emulator_GetMemSize();
    }
    return 0;
}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
    (void)index;
    (void)enabled;
    (void)code;
}

