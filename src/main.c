#include <math.h>

#include "plugin.h"

static const clap_plugin_descriptor_t pluginDescriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id = "geff.Frailty",
    .name = "Frailty",
    .vendor = "geff",
    .url = "https://geff.xyz/",
    .manual_url = "",
    .support_url = "",
    .version = "1.0.0",
    .description = "A soundfont (sf2) sampler",

    .features = (const char *[]){
        CLAP_PLUGIN_FEATURE_INSTRUMENT,
        CLAP_PLUGIN_FEATURE_STEREO,
        NULL,
    },
};

// HELPER

static void plugin_processEvent(SamplerPlugin *plugin, const clap_event_header_t *event) {
    if (event->space_id == CLAP_CORE_EVENT_SPACE_ID) {
        if (event->type == CLAP_EVENT_NOTE_ON || event->type == CLAP_EVENT_NOTE_OFF || event->type == CLAP_EVENT_NOTE_CHOKE) {
            const clap_event_note_t *note_event = (const clap_event_note_t *)event;

            for (i32 i = 0; i < plugin->voice->used; i++) {
                Voice *voice = (Voice *)plugin->voice->data[i];

                if ((note_event->key == -1 || voice->key == note_event->key) && (note_event->note_id == -1 || voice->note_id == note_event->note_id) && (note_event->channel == -1 || voice->channel == note_event->channel)) {
                    if (event->type == CLAP_EVENT_NOTE_CHOKE) {
                        deleteArray(plugin->voice, i);
                    } else {
                        voice->held = false;
                    }
                }
            }

            if (event->type == CLAP_EVENT_NOTE_ON) {
                Voice *voice = calloc(1, sizeof(Voice));
                voice->held = true;
                voice->note_id = note_event->note_id;
                voice->channel = note_event->channel;
                voice->key = note_event->key;
                voice->phase = 0.0f;
                insertArray(plugin->voice, voice);
            }
        } else if (event->type == CLAP_EVENT_PARAM_VALUE) {
            const clap_event_param_value_t *value_event = (const clap_event_param_value_t *)event;

            u32 i = value_event->param_id;

            MUTEX_ACQUIRE(plugin->sync_parameters);
            plugin->params[i] = value_event->value;
            plugin->changed[i] = true;
            MUTEX_RELEASE(plugin->sync_parameters);
        } else if (event->type == CLAP_EVENT_PARAM_MOD) {
            const clap_event_param_mod_t *mod_event = (const clap_event_param_mod_t *)event;

            for (i32 i = 0; i < plugin->voice->used; i++) {
                Voice *voice = (Voice *)plugin->voice->data[i];

                if ((mod_event->key == -1 || voice->key == mod_event->key) && (mod_event->note_id == -1 || voice->note_id == mod_event->note_id) && (mod_event->channel == -1 || voice->channel == mod_event->channel)) {
                    voice->param_offsets[mod_event->param_id] = mod_event->amount;
                    break;
                }
            }
        }
    }
}

static void plugin_renderAudio(SamplerPlugin *plugin, u32 start, u32 end, float *output_l, float *output_r) {
    for (u32 index = start; index < end; index++) {
        float sum = 0.0f;

        for (u64 i = 0; i < plugin->voice->used; i++) {
            Voice *voice = (Voice *)plugin->voice->data[i];

            if (!voice->held) {
                continue;
            }

            float volume = floatClamp(plugin->params[PARAM_VOLUME] + voice->param_offsets[PARAM_VOLUME]);
            sum += sinf(voice->phase * 2.0f * (float)PI_f) * 0.2f * volume;
            voice->phase += 440.0f * exp2f((voice->key - 57.0f) / 12.0f) / plugin->sample_rate;
            voice->phase -= floorf(voice->phase);
        }

        output_l[index] = sum;
        output_r[index] = sum;
    }
}

static void plugin_syncMainToAudio(SamplerPlugin *plugin, const clap_output_events_t *out) {
    MUTEX_ACQUIRE(plugin->sync_parameters);

    for (i32 i = 0; i < PARAM_COUNT; i++) {
        if (plugin->main_changed[i]) {
            plugin->params[i] = plugin->main_params[i];
            plugin->main_changed[i] = false;

            clap_event_param_value_t event = {0};
            event.header.size = sizeof(event);
            event.header.time = 0;
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.header.type = CLAP_EVENT_PARAM_VALUE;
            event.header.flags = 0;
            event.param_id = i;
            event.cookie = NULL;
            event.note_id = -1;
            event.port_index = -1;
            event.channel = -1;
            event.key = -1;
            event.value = plugin->params[i];
            out->try_push(out, &event.header);
        }
    }

    MUTEX_RELEASE(plugin->sync_parameters);
}

static bool plugin_syncAudioToMain(SamplerPlugin *plugin) {
    bool any_changed = false;

    MUTEX_ACQUIRE(plugin->sync_parameters);

    for (i32 i = 0; i < PARAM_COUNT; i++) {
        if (plugin->changed[i]) {
            plugin->main_params[i] = plugin->params[i];
            plugin->changed[i] = false;
            any_changed = true;
        }
    }

    MUTEX_RELEASE(plugin->sync_parameters);
    return any_changed;
}

// - clap_plugin_params_t -

u32 params_count(const clap_plugin_t *plugin) {
    return PARAM_COUNT;
}

bool params_getInfo(const clap_plugin_t *plugin, u32 i, clap_param_info_t *info) {
    if (i == PARAM_VOLUME) {
        memset(info, 0, sizeof(clap_param_info_t));
        info->id = i;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE | CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID;
        info->min_value = 0.0f;
        info->max_value = 1.0f;
        info->default_value = 0.5f;
        strcpy(info->name, "Volume");
        return true;
    } else {
        return false;
    }
}

bool params_getValue(const clap_plugin_t *plugin, clap_id id, double *value) {
    SamplerPlugin *sampler_plugin = (SamplerPlugin *)plugin->plugin_data;

    i32 i = id;
    if (i >= PARAM_COUNT) {
        return false;
    }

    MUTEX_ACQUIRE(sampler_plugin->sync_parameters);
    *value = sampler_plugin->main_changed[i] ? sampler_plugin->main_params[i] : sampler_plugin->params[i];
    MUTEX_RELEASE(sampler_plugin->sync_parameters);

    return true;
}

bool params_valueToText(const clap_plugin_t *plugin, clap_id id, double value, char *display, u32 size) {
    u32 i = id;
    if (i >= PARAM_COUNT) {
        return false;
    }

    snprintf(display, size, "%f", value);

    return true;
}

bool params_textToValue(const clap_plugin_t *plugin, clap_id id, const char *display, double *value) {
    // TODO
    return false;
}

void params_flush(const clap_plugin_t *plugin, const clap_input_events_t *in, const clap_output_events_t *out) {
    SamplerPlugin *sampler_plugin = (SamplerPlugin *)plugin->plugin_data;

    const u32 ev_count = in->size(in);

    plugin_syncMainToAudio(sampler_plugin, out);

    for (i32 ev_index = 0; ev_index < ev_count; ev_index++) {
        plugin_processEvent(sampler_plugin, in->get(in, ev_index));
    }
}

static const clap_plugin_params_t extension_params = {
    .count = params_count,
    .get_info = params_getInfo,
    .get_value = params_getValue,
    .value_to_text = params_valueToText,
    .text_to_value = params_textToValue,
    .flush = params_flush,
};

// - clap_plugin_state_t -

bool state_save(const clap_plugin_t *plugin, const clap_ostream_t *stream) {
    SamplerPlugin *sampler_plugin = (SamplerPlugin *)plugin->plugin_data;

    plugin_syncAudioToMain(sampler_plugin);

    return sizeof(float) * PARAM_COUNT == stream->write(stream, sampler_plugin->main_params, sizeof(float) * PARAM_COUNT);
}

bool state_load(const clap_plugin_t *plugin, const clap_istream_t *stream) {
    SamplerPlugin *sampler_plugin = (SamplerPlugin *)plugin->plugin_data;

    MUTEX_ACQUIRE(sampler_plugin->sync_parameters);
    bool success = sizeof(float) * PARAM_COUNT == stream->read(stream, sampler_plugin->main_params, sizeof(float) * PARAM_COUNT);

    for (i32 i = 0; i < PARAM_COUNT; i++) {
        sampler_plugin->main_changed[i] = true;
    }

    MUTEX_RELEASE(sampler_plugin->sync_parameters);

    return success;
}

static const clap_plugin_state_t extension_state = {
    .save = state_save,
    .load = state_load,
};

// - clap_plugin_note_ports_t -

u32 note_count(const clap_plugin_t *plugin, bool is_input) {
    return is_input ? 1 : 0;
}

bool note_get(const clap_plugin_t *plugin, u32 i, bool is_input, clap_note_port_info_t *info) {
    if (!is_input || i == 1) return false;

    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    snprintf(info->name, sizeof(info->name), "%s", "Note Port");

    return true;
}

static const clap_plugin_note_ports_t extension_note_ports = {
    .count = note_count,
    .get = note_get,
};

// - clap_plugin_audio_ports_t -

u32 audio_count(const clap_plugin_t *plugin, bool is_input) {
    return is_input ? 0 : 1;
}

bool audio_get(const clap_plugin_t *plugin, u32 i, bool is_input, clap_audio_port_info_t *info) {
    if (is_input || i == 1) return false;

    info->id = 0;
    info->channel_count = 2;
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    snprintf(info->name, sizeof(info->name), "%s", "Audio Output");

    return true;
}

static const clap_plugin_audio_ports_t extension_audio_ports = {
    .count = audio_count,
    .get = audio_get,
};

// - clap_plugin_t -

bool plugin_init(const struct clap_plugin *plugin) {
    SamplerPlugin *sampler_plugin = (SamplerPlugin *)plugin->plugin_data;
    sampler_plugin->voice = initArray(128);

    MUTEX_INITIALISE(sampler_plugin->sync_parameters);

    for (i32 i = 0; i < PARAM_COUNT; i++) {
        clap_param_info_t info = {0};
        extension_params.get_info(plugin, i, &info);
        sampler_plugin->main_params[i] = sampler_plugin->params[i] = info.default_value;
    }

    return true;
}

void plugin_destroy(const struct clap_plugin *plugin) {
    SamplerPlugin *sampler_plugin = (SamplerPlugin *)plugin->plugin_data;
    freeArray(sampler_plugin->voice);
    MUTEX_DESTROY(sampler_plugin->sync_parameters);
    free(sampler_plugin);
}

bool plugin_activate(const struct clap_plugin *plugin, double sample_rate, u32 min_frames_count, u32 max_frames_count) {
    SamplerPlugin *sampler_plugin = (SamplerPlugin *)plugin->plugin_data;
    sampler_plugin->sample_rate = sample_rate;
    return true;
}

void plugin_deactivate(const struct clap_plugin *plugin) {}

bool plugin_startProcessing(const struct clap_plugin *plugin) {
    return true;
}

void plugin_stopProcessing(const struct clap_plugin *plugin) {}

void plugin_reset(const struct clap_plugin *plugin) {
    SamplerPlugin *sampler_plugin = (SamplerPlugin *)plugin->plugin_data;
    freeArray(sampler_plugin->voice);
    sampler_plugin->voice = initArray(128);
}

clap_process_status plugin_process(const struct clap_plugin *plugin, const clap_process_t *process) {
    SamplerPlugin *sampler_plugin = (SamplerPlugin *)plugin->plugin_data;

    plugin_syncMainToAudio(sampler_plugin, process->out_events);

    const u32 frame_count = process->frames_count;
    const u32 input_event_count = process->in_events->size(process->in_events);
    u32 event_index = 0;
    u32 next_event_frame = input_event_count ? 0 : frame_count;

    for (u32 i = 0; i < frame_count;) {
        while (event_index < input_event_count && next_event_frame == i) {
            const clap_event_header_t *event = process->in_events->get(process->in_events, event_index);

            if (event->time != i) {
                next_event_frame = event->time;
                break;
            }

            plugin_processEvent(sampler_plugin, event);
            ++event_index;

            if (event_index == input_event_count) {
                next_event_frame = frame_count;
                break;
            }
        }

        plugin_renderAudio(sampler_plugin, i, next_event_frame, process->audio_outputs[0].data32[0], process->audio_outputs[0].data32[1]);
        i = next_event_frame;
    }

    for (u64 i = 0; i < sampler_plugin->voice->used; i++) {
        Voice *voice = (Voice *)sampler_plugin->voice->data[i];

        if (!voice->held) {
            clap_event_note_t event = {0};
            event.header.time = sizeof(event);
            event.header.time = 0;
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.header.type = CLAP_EVENT_NOTE_END;
            event.header.flags = 0;
            event.key = voice->key;
            event.note_id = voice->note_id;
            event.channel = voice->channel;
            event.port_index = 0;
            process->out_events->try_push(process->out_events, &event.header);

            deleteArray(sampler_plugin->voice, i--);
        }
    }

    return CLAP_PROCESS_CONTINUE;
}

const void *plugin_getExtension(const struct clap_plugin *plugin, const char *id) {
    if (strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) {
        return &extension_note_ports;
    }
    if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
        return &extension_audio_ports;
    }
    if (strcmp(id, CLAP_EXT_PARAMS) == 0) {
        return &extension_params;
    }
    if (strcmp(id, CLAP_EXT_STATE) == 0) {
        return &extension_state;
    }

    return NULL;
}

void plugin_onMainThread(const struct clap_plugin *plugin) {}

static const clap_plugin_t plugin_class = {
    .desc = &pluginDescriptor,
    .plugin_data = NULL,

    .init = plugin_init,
    .destroy = plugin_destroy,
    .activate = plugin_activate,
    .deactivate = plugin_deactivate,
    .start_processing = plugin_startProcessing,
    .stop_processing = plugin_stopProcessing,
    .reset = plugin_reset,
    .process = plugin_process,
    .get_extension = plugin_getExtension,
    .on_main_thread = plugin_onMainThread,
};

// - clap_plugin_factory_t -

u32 factory_getPluginCount(const struct clap_plugin_factory *factory) {
    return 1;
}

const clap_plugin_descriptor_t *factory_getPluginDescriptor(const struct clap_plugin_factory *factory, u32 i) {
    if (i == 0) {
        return &pluginDescriptor;
    }

    return NULL;
}

const clap_plugin_t *factory_createPlugin(const struct clap_plugin_factory *factory, const clap_host_t *host, const char *plugin_id) {
    if (!clap_version_is_compatible(host->clap_version) || strcmp(plugin_id, pluginDescriptor.id)) {
        return NULL;
    }

    SamplerPlugin *plugin = (SamplerPlugin *)calloc(1, sizeof(SamplerPlugin));
    plugin->host = host;
    plugin->plugin = plugin_class;
    plugin->plugin.plugin_data = plugin;
    return &plugin->plugin;
}

static const clap_plugin_factory_t plugin_factory = {
    .get_plugin_count = factory_getPluginCount,
    .get_plugin_descriptor = factory_getPluginDescriptor,
    .create_plugin = factory_createPlugin,
};

// - clap_entry -

static bool entry_init(const char *plugin_path) {
    return true;
}

static void entry_deinit(void) {}

static const void *entry_getFactory(const char *factory_id) {
    if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return &plugin_factory;
    }

    return NULL;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init = entry_init,
    .deinit = entry_deinit,
    .get_factory = entry_getFactory,
};