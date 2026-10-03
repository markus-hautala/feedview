// feedview-test-sender: publishes an NDI(R) test source (colour bars, a moving box and
// one sine tone per audio channel) so you can check FeedView without a camera or vMix.
//
//   feedview-test-sender --name "Bars" --size 1920x1080 --fps 50 --channels 2
//
// Audio channel N carries a (N x 500) Hz tone, so channel 1 = 500 Hz, channel 2 = 1 kHz ...
// --solid RRGGBB sends one plain colour instead (handy for checking fades between sources).

#include "ndi_runtime.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::atomic<bool> gStop{false};
void onSignal(int) { gStop = true; }

struct Rgb {
    double r, g, b;
};

// 75% colour bars, left to right.
const Rgb kBars[] = {{.75, .75, .75}, {.75, .75, 0}, {0, .75, .75}, {0, .75, 0},
                     {.75, 0, .75},   {.75, 0, 0},   {0, 0, .75}};

// BT.709 limited range (what HD NDI sources send).
void rgbToYuv709(const Rgb& c, uint8_t& y, uint8_t& u, uint8_t& v) {
    const double Y = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
    const double Cb = (c.b - Y) / 1.8556, Cr = (c.r - Y) / 1.5748;
    y = uint8_t(std::lround(16 + 219 * Y));
    u = uint8_t(std::lround(128 + 224 * Cb));
    v = uint8_t(std::lround(128 + 224 * Cr));
}

std::string dirOf(const char* path) {
    std::string p = path ? path : "";
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? std::string() : p.substr(0, s + 1);
}

}  // namespace

int main(int argc, char** argv) {
    std::string name = "FeedView Test";
    int width = 1920, height = 1080, fpsN = 50, fpsD = 1, channels = 2;
    bool alpha = false;
    double seconds = 0;
    bool solid = false;
    Rgb solidColor{0, 0, 0};

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--name") name = next();
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &width, &height);
        else if (a == "--fps") {
            std::string f = next();
            if (std::sscanf(f.c_str(), "%d/%d", &fpsN, &fpsD) < 2) {
                double v = std::atof(f.c_str());
                if (std::fabs(v - 59.94) < 0.01) fpsN = 60000, fpsD = 1001;
                else if (std::fabs(v - 29.97) < 0.01) fpsN = 30000, fpsD = 1001;
                else fpsN = int(v), fpsD = 1;
            }
        } else if (a == "--channels") channels = std::atoi(next().c_str());
        else if (a == "--alpha") alpha = true;
        else if (a == "--seconds") seconds = std::atof(next().c_str());
        else if (a == "--solid") {
            unsigned rgb = 0;
            if (std::sscanf(next().c_str(), "%6x", &rgb) != 1) {
                std::fprintf(stderr, "--solid needs a colour like FF0000\n");
                return 1;
            }
            solid = true;
            solidColor = {((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0};
        } else {
            std::printf("Usage: %s [--name N] [--size WxH] [--fps 50|59.94|N/D] [--channels N] [--alpha] "
                        "[--solid RRGGBB] [--seconds S]\n",
                        argv[0]);
            return a == "--help" ? 0 : 1;
        }
    }
    if (solid) alpha = false;
    width &= ~1;  // UYVY needs an even width
    if (width < 16 || height < 16 || fpsN <= 0 || fpsD <= 0 || channels < 0 || channels > 16) {
        std::fprintf(stderr, "Invalid arguments\n");
        return 1;
    }

    NdiRuntime runtime;
    if (!runtime.load(dirOf(argv[0])) || !runtime.api().send_create) {
        std::fprintf(stderr, "NDI runtime not available: %s\n", runtime.error().c_str());
        return 2;
    }
    const NdiApi& ndi = runtime.api();

    NDIlib_send_create_t sc;
    sc.p_ndi_name = name.c_str();
    sc.clock_video = true;  // NDI paces send_video to the frame rate
    sc.clock_audio = false;
    NDIlib_send_instance_t sender = ndi.send_create(&sc);
    if (!sender) {
        std::fprintf(stderr, "Could not create the NDI sender\n");
        return 3;
    }
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    // ---- Static background (bars on top 2/3, black below)
    const int barsH = height * 2 / 3;
    std::vector<uint8_t> background, frameBuf;
    int stride;
    if (alpha) {
        stride = width * 4;
        background.assign(size_t(stride) * height, 0);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                uint8_t* p = &background[size_t(y) * stride + size_t(x) * 4];
                // Alpha fades from opaque (left) to transparent (right) to exercise blending.
                uint8_t a = uint8_t(255 - (255 * x) / (width - 1));
                if (y < barsH) {
                    const Rgb& c = kBars[std::min(6, x * 7 / width)];
                    p[0] = uint8_t(std::lround(c.b * 255));
                    p[1] = uint8_t(std::lround(c.g * 255));
                    p[2] = uint8_t(std::lround(c.r * 255));
                }
                p[3] = a;
            }
        }
    } else {
        stride = width * 2;
        background.assign(size_t(stride) * height, 0);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; x += 2) {
                uint8_t* p = &background[size_t(y) * stride + size_t(x) * 2];
                uint8_t Y = 16, U = 128, V = 128;
                if (solid) rgbToYuv709(solidColor, Y, U, V);
                else if (y < barsH) rgbToYuv709(kBars[std::min(6, x * 7 / width)], Y, U, V);
                p[0] = U; p[1] = Y; p[2] = V; p[3] = Y;  // UYVY
            }
        }
    }

    // ---- Audio
    const int rate = 48000;
    std::vector<float> audio;
    double phaseSamples = 0;
    long long samplesSent = 0;

    std::printf("Sending \"%s\": %dx%d @ %d/%d, %s%s, %d audio channel(s). Ctrl+C to stop.\n", name.c_str(), width,
                height, fpsN, fpsD, alpha ? "BGRA with alpha" : "UYVY", solid ? " solid colour" : "", channels);
    std::fflush(stdout);

    const auto start = std::chrono::steady_clock::now();
    const int box = std::max(16, height / 8);
    for (long long n = 0; !gStop; ++n) {
        if (seconds > 0 &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >= seconds)
            break;

        // Moving white box below the bars so motion/judder is visible.
        frameBuf = background;
        const int travel = width - box;
        const int period = 2 * travel;
        int pos = int((n * 8) % period);
        int bx = (pos < travel ? pos : period - pos) & ~1;
        int by = barsH + (height - barsH - box) / 2;
        for (int y = by; y < by + box && y < height && !solid; ++y) {
            uint8_t* row = &frameBuf[size_t(y) * stride];
            for (int x = bx; x < bx + box && x < width; x += 2) {
                if (alpha) {
                    for (int k = 0; k < 2; ++k) std::memset(row + size_t(x + k) * 4, 255, 4);
                } else {
                    uint8_t* p = row + size_t(x) * 2;
                    p[0] = 128; p[1] = 235; p[2] = 128; p[3] = 235;
                }
            }
        }

        NDIlib_video_frame_v2_t vf;
        vf.xres = width;
        vf.yres = height;
        vf.FourCC = alpha ? NDIlib_FourCC_video_type_BGRA : NDIlib_FourCC_video_type_UYVY;
        vf.frame_rate_N = fpsN;
        vf.frame_rate_D = fpsD;
        vf.picture_aspect_ratio = float(width) / float(height);
        vf.frame_format_type = NDIlib_frame_format_type_progressive;
        vf.p_data = frameBuf.data();
        vf.line_stride_in_bytes = stride;

        if (channels > 0) {
            // Exact number of samples for this frame so audio and video never drift apart.
            long long target = ((n + 1) * (long long)rate * fpsD) / fpsN;
            int count = int(target - samplesSent);
            audio.assign(size_t(count) * channels, 0.0f);
            for (int c = 0; c < channels; ++c) {
                const double freq = 500.0 * (c + 1);
                float* plane = &audio[size_t(c) * count];
                for (int i = 0; i < count; ++i)
                    plane[i] = float(0.1 * std::sin(2 * 3.14159265358979323846 * freq * (phaseSamples + i) / rate));  // -20 dBFS
            }
            NDIlib_audio_frame_v3_t af;
            af.sample_rate = rate;
            af.no_channels = channels;
            af.no_samples = count;
            af.FourCC = NDIlib_FourCC_audio_type_FLTP;
            af.p_data = reinterpret_cast<uint8_t*>(audio.data());
            af.channel_stride_in_bytes = count * int(sizeof(float));
            if (ndi.send_send_audio_v3) ndi.send_send_audio_v3(sender, &af);
            samplesSent += count;
            phaseSamples += count;
            if (phaseSamples >= rate) phaseSamples -= rate;  // every tone is a whole number of Hz
        }
        ndi.send_send_video_v2(sender, &vf);
    }

    ndi.send_destroy(sender);
    return 0;
}
