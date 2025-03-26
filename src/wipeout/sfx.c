#include "../utils.h"
#include "../mem.h"
#include "../platform.h"
#include <kos.h>
#include <dc/sound/sound.h>

#include "sfx.h"
#include "game.h"

#include "../sndwav.h"

typedef struct {
	int8_t *samples;
	uint32_t len;
} sfx_data_t;

volatile uint32_t music_track_index;
sfx_music_mode_t music_mode;

mutex_t song_mtx;
condvar_t song_cv;

enum {
	VAG_REGION_START = 1,
	VAG_REGION = 2,
	VAG_REGION_END = 4
};

static const int32_t vag_tab[5][2] = {
	{    0,      0}, // {         0.0,          0.0}, << 14
	{15360,      0}, // { 60.0 / 64.0,          0.0}, << 14
	{29440, -13312}, // {115.0 / 64.0, -52.0 / 64.0}, << 14
	{25088, -14080}, // { 98.0 / 64.0, -55.0 / 64.0}, << 14
	{31232, -15360}, // {122.0 / 64.0, -60.0 / 64.0}, << 14
};

static sfx_data_t *sources;
static uint32_t num_sources;
static sfx_t *nodes;

#include <dc/sound/sfxmgr.h>

sfxhnd_t handles[64];

void *song_worker(void *arg);

void sfx_load(void) {
	music_mode = SFX_MUSIC_RANDOM;
	music_track_index = 0xffffffff;

	// Load SFX samples
	nodes = mem_bump(SFX_MAX * sizeof(sfx_t));
	for(int i=0;i<SFX_MAX;i++) {
		memset(&nodes->data, 0, sizeof(sfx_play_data_t));
		nodes->chn = -1;//snd_sfx_chn_alloc();
		nodes->data.chn = nodes->chn;
	}

	mutex_init(&song_mtx, MUTEX_TYPE_NORMAL);
	cond_init(&song_cv);
	kthread_attr_t song_attr;
	song_attr.create_detached = 1;
	song_attr.stack_size = 16384;
	song_attr.stack_ptr = NULL;
	song_attr.prio = 10;
	song_attr.label = "SONG_WORKER";	
	thd_create_ex(&song_attr, song_worker, NULL);

	// 16 byte blocks: 2 byte header, 14 bytes with 2x4bit samples each
	uint32_t vb_size;
	uint8_t *vb = platform_load_asset("wipeout/sound/wipeout.vb", &vb_size);
	uint32_t num_samples = (vb_size / 16) * 28;

	int8_t *sample_buffer = mem_bump(num_samples * sizeof(int8_t));
	sources = mem_mark();
	num_sources = 0;

	uint32_t sample_index = 0;
	int32_t history[2] = {0, 0};
	for (int p = 0; p < vb_size;) {
		uint8_t header = vb[p++];
		uint8_t flags = vb[p++];
		uint8_t shift = header & 0x0f;
		uint8_t predictor = clamp(header >> 4, 0, 4);

		if (flags_is(flags, VAG_REGION_END)) {
			mem_bump(sizeof(sfx_data_t));
			sources[num_sources].samples = &sample_buffer[sample_index];
		}
		for (uint32_t bs = 0; bs < 14; bs++) {
			int32_t nibbles[2] = {
				(vb[p] & 0x0f) << 12,
				(vb[p] & 0xf0) <<  8
			};
			p++;

			for (int ni = 0; ni < 2; ni++) {
				int32_t sample = nibbles[ni];
				if (sample & 0x8000) {
					sample |= 0xffff0000;
				}
				sample >>= shift;
				sample += (history[0] * vag_tab[predictor][0] + history[1] * vag_tab[predictor][1]) >> 14;
				history[1] = history[0];
				history[0] = sample;
				// convert to 8-bit
				sample_buffer[sample_index++] = clamp((sample>>8), -128, 127);
			}
		}

		if (flags_is(flags, VAG_REGION_START)) {
			error_if(sources[num_sources].samples == NULL, "VAG_REGION_START without VAG_REGION_END");
			sources[num_sources].len = &sample_buffer[sample_index] - sources[num_sources].samples;
			dbgio_printf("loading %08x %d\n", (char *)sources[num_sources].samples, sources[num_sources].len);
			handles[num_sources] = snd_sfx_load_raw_buf( (char *)sources[num_sources].samples, sources[num_sources].len, 22050, 8, 1);
			num_sources++;
		}
	}

	mem_temp_free(vb);
}

void sfx_reset(void) {
	for (int i = 0; i < SFX_MAX; i++) {
		if (flags_is(nodes[i].flags, SFX_LOOP)) {
			snd_sfx_stop(nodes[i].chn);
			snd_sfx_chn_free(nodes[i].chn);
			memset(&nodes[i].data, 0, sizeof(sfx_play_data_t));
			nodes[i].chn = -1;
			nodes[i].data.chn = nodes[i].chn;
			flags_set(nodes[i].flags, SFX_NONE);
		}
	}
}

void sfx_unpause(void) {
	for (int i = 0; i < SFX_MAX; i++) {
		if (flags_is(nodes[i].flags, SFX_LOOP_PAUSE)) {
			flags_rm(nodes[i].flags, SFX_LOOP_PAUSE);
		}
	}
}

void sfx_pause(void) {
	for (int i = 0; i < SFX_MAX; i++) {
		if (flags_is(nodes[i].flags, SFX_LOOP)) {
			flags_add(nodes[i].flags, SFX_LOOP_PAUSE);
		}
	}
}



// Sound effects

sfx_t *sfx_get_node(sfx_source_t source_index) {

	error_if(source_index < 0 || source_index > num_sources, "Invalid audio source");

	sfx_t *sfx = NULL;
	for (int i = 0; i < SFX_MAX; i++) {
		if (flags_none(nodes[i].flags, SFX_RESERVE)){
			sfx = &nodes[i];
			break;
		}
	}

	error_if(!sfx, "All audio nodes reserved");

	flags_set(sfx->flags, SFX_NONE);
	sfx->source = source_index;
	sfx->volume = 1;
	sfx->current_volume = 1;
	sfx->pan = 0;
	sfx->current_pan = 0;
	sfx->position = 0;

	// Set default pitch. All voice samples are 44khz, 
	// other effects 22khz
	sfx->pitch = source_index >= SFX_VOICE_MINES ? 1.0 : 0.5;

	return sfx;
}

sfx_t *sfx_play(sfx_source_t source_index) {
	sfx_t *sfx = sfx_get_node(source_index);
	sfx->data.chn = -1;
	sfx->data.idx = handles[source_index];
	sfx->data.loop = 0;
	sfx->data.loopstart = 0;
	sfx->data.loopend = 0;
	sfx->data.vol = (sfx->volume * 255) * save.sfx_volume;
	sfx->data.pan = 127 + (sfx->pan*128);
	sfx->data.freq = sfx->pitch == 1.0f ? 44100 : 22050;

	snd_sfx_play_ex(&sfx->data);

	return sfx;
}

sfx_t *sfx_play_at(sfx_source_t source_index, vec3_t pos, vec3_t vel, float volume) {
	sfx_t *sfx = sfx_get_node(source_index);
//	sfx_set_position(sfx, pos, vel, volume);
	vec3_t relative_position = vec3_sub(g.camera.position, pos);
	vec3_t relative_velocity = vec3_sub(g.camera.real_velocity, vel);
	float distance = vec3_len(relative_position);

	sfx->volume = clamp(scale(distance, 512, 32768, 1, 0), 0, 1) * volume;
	sfx->pan = -sinf(bump_atan2f(g.camera.position.x - pos.x, g.camera.position.z - pos.z)+g.camera.angle.y);

	// Doppler effect
	float away = vec3_dot(relative_velocity, relative_position) / distance;
	sfx->pitch = (262144.0 - away) / 524288.0;

	if (sfx->volume > 0) {
		sfx->data.chn = -1;
		sfx->data.idx = handles[source_index];
		sfx->data.loop = 0;
		sfx->data.loopstart = 0;
		sfx->data.loopend = 0;
		sfx->data.vol = (sfx->volume * 255) * save.sfx_volume;
		sfx->data.pan = 127 + (sfx->pan*128);
		sfx->data.freq = sfx->pitch * 22050;
		snd_sfx_play_ex(&sfx->data);
	}
	return sfx;
}

sfx_t *sfx_reserve_loop(sfx_source_t source_index) {
	sfx_t *sfx = sfx_get_node(source_index);
	flags_set(sfx->flags, SFX_RESERVE | SFX_LOOP);
	sfx->volume = 0;
	sfx->current_volume = 0;
	sfx->current_pan = 0;
	sfx->pan = 0;
	sfx->position = rand_float(0, sources[source_index].len);

	sfx->chn = snd_sfx_chn_alloc();
	sfx->data.chn = sfx->chn;
	sfx->data.idx = handles[source_index];
	sfx->data.loop = 1;
	sfx->data.loopstart = 0;
	sfx->data.loopend = sources[source_index].len;
	sfx->data.vol = 0;
	sfx->data.pan = 127;
	sfx->data.freq = 22050;

	snd_sfx_play_ex(&sfx->data);	

	return sfx;
}

void sfx_update_ex(sfx_t *sfx) {
	sfx->data.vol = (sfx->volume * 255) * save.sfx_volume;
	sfx->data.pan = 127 + (sfx->pan*128);
	sfx->data.freq = sfx->pitch * 22050;

	// SORRY, KOS main doesn't have this yet
	snd_sfx_update_ex(&sfx->data);
}

void sfx_set_position(sfx_t *sfx, vec3_t pos, vec3_t vel, float volume) {

	vec3_t relative_position = vec3_sub(g.camera.position, pos);
	vec3_t relative_velocity = vec3_sub(g.camera.real_velocity, vel);
	float distance = vec3_len(relative_position);

	sfx->volume = clamp(scale(distance, 512, 32768, 1, 0), 0, 1) * volume;
	sfx->pan = -sinf(bump_atan2f(g.camera.position.x - pos.x, g.camera.position.z - pos.z)+g.camera.angle.y);

	// Doppler effect
	float away = vec3_dot(relative_velocity, relative_position) / distance;
	sfx->pitch = (262144.0 - away) / 524288.0;

	sfx->data.vol = (sfx->volume * 255)* save.sfx_volume;
	sfx->data.pan = 127 + (sfx->pan*128);
	sfx->data.freq = sfx->pitch * 22050;

	// SORRY, KOS main doesn't have this yet
	snd_sfx_update_ex(&sfx->data);
}


// Music

uint32_t sfx_music_decode_frame(void) {
return 0;
}

void sfx_music_rewind(void) {
}
extern char *temp_path;
extern char *path_userdata;
extern char *path_assets;

volatile int cur_hnd = SND_STREAM_INVALID;
void sfx_music_open(char *path) {
    char *newpath = strcat(strcpy(temp_path, path_assets), path);

	cur_hnd = wav_create(newpath, 1);

	if (cur_hnd == SND_STREAM_INVALID) {
		dbgio_printf("Could not create wav %s\n", newpath);
	}

	return;
}

#if 0
void sfx_music_done_callback(void) {
	
	if (music_mode == SFX_MUSIC_RANDOM) {
//		printf("end track cb RANDOM\n");
		sfx_music_play(rand_int(0, len(def.music)));
	}
	else if (music_mode == SFX_MUSIC_SEQUENTIAL) {
//		printf("end track cb SEQUENTIAL\n");
		sfx_music_play((music_track_index + 1) % len(def.music));
	}
	else if (music_mode == SFX_MUSIC_LOOP) {
//		printf("end track cb LOOP\n");
		sfx_music_play(music_track_index);
	}
}
#endif

/*

music player mechanism

main thread starts a song 

the song needs to play for a known duration plus some wiggle room

if nothing else happens, when the duration is elapsed, a signal is sent to something that starts the next song










*/
static uint32_t runtimes[11] = {
315,
323,
306,
316,
316,
318,
326,
307,
386,
292,
375	
};


#include <errno.h>

void *song_worker(void *arg) {
	int song_interrupted = 0;

	uint32_t ran = 0;

	wav_init();

	while (1) {
		if (!ran) {
			// wait to get first signal that says "OK, LETS PLAY A NEW SONG"
			mutex_lock(&song_mtx);
			cond_wait(&song_cv, &song_mtx);
			mutex_unlock(&song_mtx);
		
			ran = 1;
		}

		// if a song ever played before, kill it
		if (cur_hnd != SND_STREAM_INVALID) {
			wav_destroy();
			cur_hnd = SND_STREAM_INVALID;
		}

		// now, we need to fire off the new song

		// this does wav_create under the hood (in looping mode)
		sfx_music_open(def.music[music_track_index].path);
		// and then actually play it
		wav_play();

		int duration = runtimes[music_track_index];

		// always start by assuming that the song will complete uninterrupted
		song_interrupted = 0;

		// while the song shouldn't be done playing yet
		// do a timed wait for a full second
		mutex_lock(&song_mtx);
		int rv = cond_wait_timed(&song_cv, &song_mtx, (duration * 1000) + 50);
		mutex_unlock(&song_mtx);

//		printf("%d %s\n", rv, strerror(errno));
		// if rv != 0, we timed out, we don't have to update anything

		if (0 == rv) {
			// if we were explicitly interrupted, someone called `sfx_music_play`
			// so clear this flag to indicate to the next iteration of the loop
			// to not wait again
			song_interrupted = 1;
		}

		thd_pass();

		// if we made it to the end of the song without being interrupted,
		// do the music logic from the original mixer code
		if (!song_interrupted) {
			if (music_mode == SFX_MUSIC_RANDOM) {
				music_track_index = rand_int(0, len(def.music));
			}
			else if (music_mode == SFX_MUSIC_SEQUENTIAL) {
				music_track_index = (music_track_index + 1) % len(def.music);
			}
			/*else if (music_mode == SFX_MUSIC_LOOP) {

			}*/
		}

		thd_pass();		
	}

	return NULL;
}

void sfx_music_play(uint32_t index) {
	error_if(index >= len(def.music), "Invalid music index");
	music_track_index = index;

	mutex_lock(&song_mtx);
	cond_signal(&song_cv);
	mutex_unlock(&song_mtx);
}

void sfx_music_mode(sfx_music_mode_t mode) {
	music_mode = mode;
}

// Mixing

void sfx_set_external_mix_cb(void (*cb)(float *, uint32_t len)) {
}

void sfx_stero_mix(float *buffer, uint32_t len) {
}

