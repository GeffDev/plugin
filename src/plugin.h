#ifndef PLUGIN_H
#define PLUGIN_H

#include "utility.h"

#include "clap.h"

// i don't care
#ifdef _WIN32
// include gui_win.h
#else
#include "gui/gui_x11.h"

#include <pthread.h>

typedef pthread_mutex_t Mutex;

#define MUTEX_ACQUIRE(mutex) pthread_mutex_lock(&mutex)
#define MUTEX_RELEASE(mutex) pthread_mutex_unlock(&mutex)
#define MUTEX_INITIALISE(mutex) pthread_mutex_init(&mutex, NULL)
#define MUTEX_DESTROY(mutex) pthread_mutex_destroy(&mutex)
#endif

typedef enum {
    // add more when sf2 is implemented
    PARAM_VOLUME,
    PARAM_COUNT,
} Params;

typedef struct {
    bool held;
    i32 note_id;
    i16 channel, key;
    float phase;
    float param_offsets[PARAM_COUNT];
} Voice;

typedef struct {
    clap_plugin_t plugin;
    const clap_host_t *host;
    double sample_rate;
    ArrayList *voice;
    float params[PARAM_COUNT], main_params[PARAM_COUNT];
    bool changed[PARAM_COUNT], main_changed[PARAM_COUNT];
    Mutex sync_parameters;
} SamplerPlugin;

#endif