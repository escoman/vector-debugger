#include <iostream>
#include <fstream>
#include <iterator>

#include "memory.h"
#include "io.h"
#include "tv.h"
#include "board.h"
#include "emulator.h"
#include "options.h"
#include "keyboard.h"
#include "8253.h"
#include "sound.h"
#include "ay.h"
#include "wav.h"
#include "util.h"

#include "libretro_main.h"

Memory memory;
FD1793 fdc;
Wav wav;
WavPlayer tape_player(wav);
Keyboard keyboard;
I8253 timer;
TimerWrapper tw(timer);
AY ay;
AYWrapper aw(ay);
Soundnik soundnik(tw, aw);
IO io(memory, keyboard, timer, fdc, ay, tape_player);//Options.nofdc ? fdc_dummy : fdc);
TV tv;
PixelFiller filler(memory, io, tv);
Debug debug(&memory);
Board board(memory, io, filler, soundnik, tv, tape_player, debug);
Emulator lator(board);

extern "C" int Emulator_Init()
{
    WavRecorder rec;
    WavRecorder * prec = 0;

    if (Options.audio_rec_path.length()) {
        rec.init(Options.audio_rec_path);
        prec = &rec;
    }

    filler.init();
    soundnik.init(prec);    // this may switch the audio output off
    tv.init();
    board.init();
    fdc.init();
    if (Options.bootpalette) {
        io.yellowblue();
    }

    keyboard.onreset = [](bool blkvvod) {
        board.reset(blkvvod ?
                Board::ResetMode::BLKVVOD : Board::ResetMode::BLKSBR);
    };

    if (Options.autostart) {
        int seq = 0;
        io.onruslat = [&seq](bool ruslat) {
            seq = (seq << 1) | (ruslat ? 1 : 0);
            if ((seq & 15) == 6) {
                board.reset(Board::ResetMode::BLKSBR);
                io.onruslat = nullptr;
            }
        };
    }

    board.reset(Board::ResetMode::BLKVVOD);

//    if (Options.wavfile.length() != 0) {
//        load_wav(wav, Options.wavfile);
//    }
//
//    load_disks(fdc);


//    bootstrap_scriptnik();

    return (signed)0xdeadbeef;
}

void load_rom(const uint8_t * bytes, size_t size, int org)
{
    const std::vector<uint8_t> bin(bytes, bytes + size);
    memory.init_from_vector(bin, org);
}

// this has changed, see V06X_Mount in godot
void load_fdd(const uint8_t * bytes, size_t size, int drive)
{
    const std::vector<uint8_t> fdd_image(bytes, bytes + size);
    fdc.disk(drive).attach(fdd_image);
}

void load_edd(const uint8_t * bytes, size_t size, int slot)
{
    const std::vector<uint8_t> edd(bytes, bytes+size);
    memory.init_from_vector(edd, 0x10000 + slot * 0x40000);
}

void load_wav(const uint8_t* bytes, size_t size)
{
    const std::vector<uint8_t> v(bytes, bytes + size);
    wav.set_bytes(v);
}

extern "C" int Emulator_ExecuteFrame(uint8_t * pixels, float * samples)
{
    lator.execute_frame();

    lator.export_pixel_bytes(pixels);
    lator.export_audio_frame(samples, 2*48000/50);

    return 0;
}

extern "C" void Emulator_KeyDown(int scancode)
{
    lator.keydown(scancode);
}

extern "C" void Emulator_KeyUp(int scancode)
{
    lator.keyup(scancode);
}


extern "C" void Emulator_LoadAsset(const uint8_t *data, size_t data_sz, int kind, int org)
{
    switch (kind) {
        case LOADKIND_COM:
        case LOADKIND_ROM:
            load_rom(data, data_sz, org);
            break;
        case LOADKIND_FDD:
            load_fdd(data, data_sz, org);
            break;
        case LOADKIND_EDD:
            load_edd(data, data_sz, org);
            break;
        case LOADKIND_WAV:
            load_wav(data, data_sz);
    }
}

//// dir or fdd
//godot_variant V06X_Mount(godot_object* p_instance, void* p_method_data,
//  void* p_user_data, int p_num_args, godot_variant** p_args)
//{
//    godot_int device = api->godot_variant_as_int(p_args[0]);
//    godot_string wpath = api->godot_variant_as_string(p_args[1]);
//    godot_char_string cpath = api->godot_string_ascii(&wpath);
//
//    const char* path = api->godot_char_string_get_data(&cpath);
//
//    try {
//        fdc.disk(device).attach(path);
//    } catch (...) {
//        printf("Mount: dev=%d path=%s failed\n", device, path);
//    }
//
//    api->godot_char_string_destroy(&cpath);
//
//    godot_variant ret;
//    api->godot_variant_new_bool(&ret, 1);
//    return ret;
//}


extern "C" void Emulator_Reset(int blkvvod)
{
    board.reset(blkvvod ?
                Board::ResetMode::BLKVVOD : Board::ResetMode::BLKSBR);
}

//extern "C" JNIEXPORT jbyteArray JNICALL
//Java_com_svofski_v06x_cpp_Emulator_ExportState(JNIEnv * env, jobject) {
//    std::vector<uint8_t> state;
//    lator.save_state(state);
//    jbyteArray out_state = env->NewByteArray(state.size());
//
//    jbyte * jbytes = env->GetByteArrayElements(out_state, NULL);
//    std::copy(state.begin(), state.end(), jbytes);
//
//    env->ReleaseByteArrayElements(out_state, jbytes, JNI_COMMIT);
//
//    return out_state;
//}
//
//extern "C" JNIEXPORT jboolean JNICALL
//Java_com_svofski_v06x_cpp_Emulator_RestoreState(JNIEnv * env, jobject, jbyteArray in_state) {
//    jsize size = env->GetArrayLength(in_state);
//    jbyte * jbytes = env->GetByteArrayElements(in_state, NULL);
//
//    std::vector<uint8_t> state((uint8_t *)jbytes, (uint8_t *)jbytes + size);
//    jboolean result = lator.restore_state(state);
//
//    env->ReleaseByteArrayElements(in_state, jbytes, JNI_ABORT);
//
//    return result;
//}

