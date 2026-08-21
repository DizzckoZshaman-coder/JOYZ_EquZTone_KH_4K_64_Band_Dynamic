#include <clap/clap.h>
#include <pugl/pugl.h>
#include <pugl/pugl_gl.h>
#define NANOVG_GL3_IMPLEMENTATION
#include <nanovg.h>
#include <nanovg_gl.h>

#include <array>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <atomic> // SENJATA RAHASIA THREAD-SAFE (Biar gak glitch!)

// ============================================================================
// 1. DATA STRUCTURES (MSC IRINA ARCHITECTURE)
// ============================================================================
struct EQBand {
    bool active = false; // Sistem Custom Load!
    float freq = 1000.0f;
    float gain = 0.0f;
    float q = 1.0f;
    int type = 0;   
    int slope = 1;  
    float dyn_thresh = -20.0f;
    float dyn_range = -6.0f; 
};

struct BiquadState {
    float z1_l = 0.0f, z2_l = 0.0f;
    float z1_r = 0.0f, z2_r = 0.0f;
};

// ============================================================================
// 2. THE MAIN PLUGIN CLASS (MENGGABUNGKAN DSP & GUI STATE)
// ============================================================================
class JoyzEquZTone4K {
public:
    // --- AUDIO THREAD DATA ---
    std::array<EQBand, 64> bands;
    std::array<std::array<BiquadState, 5>, 64> filter_states;
    float sample_rate = 48000.0f;
    float env_level = 0.0f;
    float frame_decay = 0.0f;

    // --- SHARED MEMORY BUS (AUDIO -> GUI) VIA ATOMIC RING BUFFER ---
    // Menyimpan 2048 sampel terakhir sebagai "Proxy" buat diolah GUI
    std::array<std::atomic<float>, 2048> proxy_buffer;
    std::atomic<int> proxy_write_idx{0};

    // --- GUI THREAD DATA (PUGL & NANOVG) ---
    PuglView* view = nullptr;
    NVGcontext* vg = nullptr;
    float ui_float_x = 100.0f, ui_float_y = 100.0f;
    bool ui_float_active = false;
    int active_band_idx = -1;
    bool is_clicked = false;
    int window_width = 1200;
    int window_height = 600;

    JoyzEquZTone4K() {
        // Pabrik Kosong (Zero CPU Load saat idle)
        for(int i = 0; i < 2048; ++i) {
            proxy_buffer[i].store(0.0f, std::memory_order_relaxed);
        }
    }

    void init(float s_rate) {
        sample_rate = s_rate;
        frame_decay = std::exp(-1.0f / (0.05f * sample_rate));
    }

    // ============================================================================
    // 3. DSP ENGINE (CLAP PROCESS) - LOSSLESS MURNI
    // ============================================================================
    void calc_biquad(int b_idx, float current_gain, float& a0, float& a1, float& a2, float& b0, float& b1, float& b2) {
        float w0 = 2.0f * M_PI * bands[b_idx].freq / sample_rate;
        float alpha = std::sin(w0) / (2.0f * bands[b_idx].q);
        float A = std::pow(10.0f, current_gain / 40.0f);

        if (bands[b_idx].type == 0) { // BELL
            a0 = 1.0f + alpha / A;
            b0 = (1.0f + alpha * A) / a0;
            b1 = (-2.0f * std::cos(w0)) / a0;
            b2 = (1.0f - alpha * A) / a0;
            a1 = (-2.0f * std::cos(w0)) / a0;
            a2 = (1.0f - alpha / A) / a0;
        }
    }

    clap_process_status process(const clap_process_t *process) {
        const uint32_t num_frames = process->frames_count;
        const float *in_l = process->audio_inputs[0].data32[0];
        const float *in_r = process->audio_inputs[0].data32[1];
        float *out_l = process->audio_outputs[0].data32[0];
        float *out_r = process->audio_outputs[0].data32[1];

        for (uint32_t i = 0; i < num_frames; ++i) {
            float spl_l = in_l[i];
            float spl_r = in_r[i];

            // DSP EQ Logic
            float env_in = (std::abs(spl_l) + std::abs(spl_r)) * 0.5f;
            env_level = (env_in > env_level) ? env_in : env_level * frame_decay;
            float env_db = 20.0f * std::log10(std::max(env_level, 0.0001f));

            for (size_t b = 0; b < 64; ++b) {
                if (!bands[b].active) continue;

                float dyn_diff = env_db - bands[b].dyn_thresh;
                float target_gain = bands[b].gain;

                if (dyn_diff > 0.0f) {
                    float reduction = std::min(dyn_diff * 0.5f, std::abs(bands[b].dyn_range));
                    target_gain += (bands[b].dyn_range < 0.0f) ? -reduction : reduction;
                }

                float a0, a1, a2, b0, b1, b2;
                calc_biquad(b, target_gain, a0, a1, a2, b0, b1, b2);

                for (int c = 0; c < bands[b].slope; ++c) {
                    float out_l_tmp = (b0 * spl_l) + (b1 * filter_states[b][c].z1_l) + (b2 * filter_states[b][c].z2_l) 
                                      - (a1 * filter_states[b][c].z1_l) - (a2 * filter_states[b][c].z2_l);
                    filter_states[b][c].z2_l = filter_states[b][c].z1_l;
                    filter_states[b][c].z1_l = out_l_tmp;
                    spl_l = out_l_tmp; 

                    float out_r_tmp = (b0 * spl_r) + (b1 * filter_states[b][c].z1_r) + (b2 * filter_states[b][c].z2_r) 
                                      - (a1 * filter_states[b][c].z1_r) - (a2 * filter_states[b][c].z2_r);
                    filter_states[b][c].z2_r = filter_states[b][c].z1_r;
                    filter_states[b][c].z1_r = out_r_tmp;
                    spl_r = out_r_tmp; 
                }
            }
            
            out_l[i] = spl_l;
            out_r[i] = spl_r;

            // THE PROXY BRIDGE: Lempar sampel Mono ke tong sampah Atomic!
            // Operasi ini sangat ringan dan 100% bebas dari ancaman audio glitch/dropouts
            int p_idx = proxy_write_idx.load(std::memory_order_relaxed);
            proxy_buffer[p_idx].store(env_in, std::memory_order_relaxed); // Simpan level absolut
            proxy_write_idx.store((p_idx + 1) % 2048, std::memory_order_relaxed);
        }
        return CLAP_PROCESS_CONTINUE;
    }

    // ============================================================================
    // 4. GUI RENDERER (NANOVG @ 60 FPS)
    // ============================================================================
    void draw_joyz_gui() {
        glClearColor(0.06f, 0.06f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        nvgBeginFrame(vg, window_width, window_height, 1.0f);

        // 1. SEDOT DATA DARI PROXY ATOMIC (Biar CPU gak nangis!)
        std::array<float, 2048> local_proxy;
        int current_idx = proxy_write_idx.load(std::memory_order_acquire);
        for (int i = 0; i < 2048; ++i) {
            int read_idx = (current_idx + i) % 2048;
            local_proxy[i] = proxy_buffer[read_idx].load(std::memory_order_relaxed);
        }

        // 2. GAMBAR 4096 MICROBAR (Dengan efek kemiringan Warlord Fletcher-Munson)
        float tilt_per_octave = 3.0f; 
        float center_freq = 1000.0f;  
        
        nvgBeginPath(vg);
        nvgFillColor(vg, nvgRGBA(42, 207, 200, 230)); // Cyan Warlord
        
        for (int i = 0; i < 4096; ++i) {
            // Pemetaan simulasi data logaritmik (Hanya dihitung saat nge-render 60 FPS!)
            float current_freq = 440.0f * std::pow(2.0f, (i - 2208.0f) / 384.0f);
            float octaves_from_center = std::log2(std::max(current_freq / center_freq, 0.001f));
            float tilt_db = octaves_from_center * tilt_per_octave;
            
            // Ambil sampel secara berulang dari proxy 2048 ke 4096 bar
            float raw_level = local_proxy[(i % 2048)];
            float raw_db = 20.0f * std::log10(std::max(raw_level, 0.0001f));
            float human_db = raw_db + tilt_db; 

            float x_pos = 25.0f + (i * ((window_width - 50.0f) / 4096.0f));
            float bar_height = (human_db + 80.0f) * 2.0f; // Asumsi lantai -80dB
            float y_pos = window_height - 20.0f - bar_height;
            y_pos = std::clamp(y_pos, 0.0f, (float)window_height); // Pengaman atap

            nvgRect(vg, x_pos, y_pos, 1.0f, window_height - y_pos); 
        }
        nvgFill(vg);

        // 3. RENDER NODE BULAT (CUSTOM LOAD)
        for (int i = 0; i < 64; ++i) {
            if (bands[i].active) {
                float node_x = 25.0f + (std::log10(bands[i].freq / 20.0f) / std::log10(20000.0f / 20.0f)) * (window_width - 50.0f);
                float node_y = (window_height / 2.0f) - (bands[i].gain * 10.0f);
                
                nvgBeginPath(vg);
                nvgCircle(vg, node_x, node_y, 8.0f);
                nvgFillColor(vg, nvgRGBA(255, 255, 255, 200));
                nvgFill(vg);
            }
        }

        // 4. FLOATING WINDOW
        if (ui_float_active) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, ui_float_x, ui_float_y, 320, 160, 5.0f);
            nvgFillColor(vg, nvgRGBA(25, 25, 25, 240));
            nvgFill(vg);
        }

        nvgEndFrame(vg);
    }

    // ============================================================================
    // 5. GUI SENSOR (PUGL EVENT LOOP) - SENSOR CUSTOM LOAD
    // ============================================================================
    static PuglStatus onEvent(PuglView* view, const PuglEvent* event) {
        JoyzEquZTone4K* plugin = (JoyzEquZTone4K*)puglGetHandle(view);

        switch (event->type) {
            case PUGL_MOTION:
                if (plugin->is_clicked && plugin->ui_float_active) {
                    plugin->ui_float_x = event->motion.x - 160; 
                    plugin->ui_float_y = event->motion.y - 80;
                }
                puglPostRedisplay(view); // Minta render 60 FPS
                break;

            case PUGL_BUTTON_PRESS:
                // KLIK KIRI (Spawning / Custom Load Band Baru)
                if (event->button.button == 1) { 
                    plugin->is_clicked = true;
                    
                    bool clicked_on_node = false;

                    if (!clicked_on_node) {
                        for (int i = 0; i < 64; ++i) {
                            if (!plugin->bands[i].active) {
                                plugin->bands[i].active = true;
                                float normalized_x = (event->button.x - 25.0f) / (plugin->window_width - 50.0f);
                                plugin->bands[i].freq = 20.0f * std::pow(1000.0f, normalized_x);
                                plugin->bands[i].gain = 0.0f;
                                
                                plugin->active_band_idx = i;
                                plugin->ui_float_active = true;
                                plugin->ui_float_x = event->button.x + 15;
                                plugin->ui_float_y = event->button.y + 15;
                                break;
                            }
                        }
                    }
                }
                // KLIK KANAN (Disable/Delete Band)
                else if (event->button.button == 3) {
                    if (plugin->active_band_idx != -1) {
                        plugin->bands[plugin->active_band_idx].active = false;
                        plugin->ui_float_active = false;
                        plugin->active_band_idx = -1;
                    }
                }
                break;

            case PUGL_BUTTON_RELEASE:
                plugin->is_clicked = false;
                break;

            case PUGL_EXPOSE:
                plugin->draw_joyz_gui(); 
                break;

            default: break;
        }
        return PUGL_SUCCESS;
    }
};

// ============================================================================
// 6. CLAP PROTOCOL: THE ENTRY POINT 
// ============================================================================
static const clap_plugin_descriptor_t joyz_descriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "com.joyzmusic.equztone4k",
    .name = "JOYZ EquZTone KH 4K",
    .vendor = "JOYZ Music",
    .url = "https://joyzmusic.com",
    .manual_url = "",
    .support_url = "",
    .version = "2.0.SINTING",
    .description = "64-Band Dynamic EQ Warlord Edition",
    .features = (const char*[]){CLAP_PLUGIN_FEATURE_EQUALIZER, CLAP_PLUGIN_FEATURE_STEREO, NULL}
};

// ============================================================================
// 7. CLAP C-API WRAPPER (JEMBATAN DAW KE C++ CLASS)
// ============================================================================
struct JoyzPluginWrapper {
    clap_plugin_t plugin;
    JoyzEquZTone4K* engine;
};

static bool joyz_init(const struct clap_plugin *plugin) {
    JoyzPluginWrapper* wrapper = (JoyzPluginWrapper*)plugin;
    wrapper->engine = new JoyzEquZTone4K();
    wrapper->engine->init(48000.0f); 
    return true;
}

static void joyz_destroy(const struct clap_plugin *plugin) {
    JoyzPluginWrapper* wrapper = (JoyzPluginWrapper*)plugin;
    delete wrapper->engine;
    delete wrapper;
}

static clap_process_status joyz_process(const struct clap_plugin *plugin, const clap_process_t *process) {
    JoyzPluginWrapper* wrapper = (JoyzPluginWrapper*)plugin;
    return wrapper->engine->process(process);
}

static const void* joyz_get_extension(const struct clap_plugin *plugin, const char *id) {
    return nullptr; 
}

static const clap_plugin_t joyz_plugin_template = {
    &joyz_descriptor,
    nullptr,
    joyz_init,
    joyz_destroy,
    nullptr, 
    nullptr, 
    nullptr, 
    nullptr, 
    nullptr, 
    joyz_process,
    joyz_get_extension,
    nullptr, 
};

// ============================================================================
// 8. THE FACTORY (PABRIK PLUGIN)
// ============================================================================
static uint32_t joyz_get_plugin_count(const struct clap_plugin_factory *factory) {
    return 1; 
}

static const clap_plugin_descriptor_t* joyz_get_plugin_descriptor(const struct clap_plugin_factory *factory, uint32_t index) {
    return &joyz_descriptor;
}

static const clap_plugin_t* joyz_create_plugin(const struct clap_plugin_factory *factory, const clap_host_t *host, const char *plugin_id) {
    if (strcmp(plugin_id, joyz_descriptor.id) == 0) {
        JoyzPluginWrapper* wrapper = new JoyzPluginWrapper();
        wrapper->plugin = joyz_plugin_template;
        wrapper->plugin.plugin_data = wrapper;
        return &wrapper->plugin;
    }
    return nullptr;
}

static const clap_plugin_factory_t joyz_factory = {
    joyz_get_plugin_count,
    joyz_get_plugin_descriptor,
    joyz_create_plugin,
};

// ============================================================================
// 9. THE ENTRY POINT (MUTLAK HARUS ADA!)
// ============================================================================
CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT,
    [](const char *path) -> bool { return true; },
    [](void) {},
    [](const char *factory_id) -> const void* {
        if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) {
            return &joyz_factory;
        }
        return nullptr;
    }
};




