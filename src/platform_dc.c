#include "platform.h"
#include "input.h"
#include "system.h"
#include "utils.h"
#include "mem.h"
#include "types.h"

#include <string.h>
#include <sys/time.h>

static uint64_t perf_freq = 0;
static bool wants_to_exit = false;
void *gamepad;
static void (*audio_callback)(float *buffer, uint32_t len) = NULL;
char *path_assets = "";
char *path_userdata = "";
char *temp_path = NULL;

void vmu_icon(void);

void platform_exit(void) {
	wants_to_exit = true;
}

void *platform_find_gamepad(void) {
	return NULL;
}


#include <dc/maple.h>
#include <dc/maple/controller.h>

#define configDeadzone (0x20)
void platform_pump_events() {
    maple_device_t *cont;
    cont_state_t *state;

    cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (!cont)
        return;
    state = (cont_state_t *) maple_dev_status(cont);

	if ((state->buttons & CONT_START) && state->ltrig && state->rtrig) {
		platform_exit();
	}

	int	last_joyx = state->joyx;
	int	last_joyy = state->joyy;

	if (last_joyy == -128)
			last_joyy = -127;

	const uint32_t magnitude_sq = (uint32_t)(last_joyx * last_joyx) + (uint32_t)(last_joyy * last_joyy);
	float stick_x = 0;
	float stick_y = 0;
	if (magnitude_sq > (uint32_t)(configDeadzone * configDeadzone)) {
		stick_x = ((float)last_joyx/127.0f);
		stick_y = ((float)last_joyy/127.0f);
	}
    if (state->buttons & CONT_START){
			input_set_button_state(INPUT_GAMEPAD_START, 1.0f);
		} else {
			input_set_button_state(INPUT_GAMEPAD_START, 0.0f);
		}
    if (state->buttons & CONT_A){
			input_set_button_state(INPUT_GAMEPAD_A, 1.0f);
		} else {
			input_set_button_state(INPUT_GAMEPAD_A, 0.0f);
		}
		if (state->buttons & CONT_B){
			input_set_button_state(INPUT_GAMEPAD_B, 1.0f);
		} else {
			input_set_button_state(INPUT_GAMEPAD_B, 0.0f);
		}
		if (state->buttons & CONT_X){
			input_set_button_state(INPUT_GAMEPAD_X, 1.0f);
		} else {
			input_set_button_state(INPUT_GAMEPAD_X, 0.0f);
		}
		if (state->buttons & CONT_Y){
			input_set_button_state(INPUT_GAMEPAD_Y, 1.0f);
		} else {
			input_set_button_state(INPUT_GAMEPAD_Y, 0.0f);
		}
    if ((uint8_t)state->ltrig & 0x80)
		{
			input_set_button_state(INPUT_GAMEPAD_L_TRIGGER, 1.0f);
		} else {
			input_set_button_state(INPUT_GAMEPAD_L_TRIGGER, 0.0f);
		}
    if ((uint8_t)state->rtrig & 0x80){
			input_set_button_state(INPUT_GAMEPAD_R_TRIGGER, 1.0f);
		} else {
			input_set_button_state(INPUT_GAMEPAD_R_TRIGGER, 0.0f);
		}
    if (state->buttons & CONT_DPAD_UP){
			input_set_button_state(INPUT_GAMEPAD_DPAD_UP, 1.0f);
		} else {
			input_set_button_state(INPUT_GAMEPAD_DPAD_UP, 0.0f);
		}
		if (state->buttons & CONT_DPAD_DOWN){
			input_set_button_state(INPUT_GAMEPAD_DPAD_DOWN, 1.0f);
		} else {
			input_set_button_state(INPUT_GAMEPAD_DPAD_DOWN, 0.0f);
		}
		if (state->buttons & CONT_DPAD_LEFT){
			input_set_button_state(INPUT_GAMEPAD_DPAD_LEFT, 1.0f);
		} else {
			input_set_button_state(INPUT_GAMEPAD_DPAD_LEFT, 0.0f);
		}
		if (state->buttons & CONT_DPAD_RIGHT){
			input_set_button_state(INPUT_GAMEPAD_DPAD_RIGHT, 1.0f);
		} else {
			input_set_button_state(INPUT_GAMEPAD_DPAD_RIGHT, 0.0f);
		}

		// joystick
		if (stick_x < 0) {
			input_set_button_state(INPUT_GAMEPAD_L_STICK_LEFT, -stick_x);
			input_set_button_state(INPUT_GAMEPAD_L_STICK_RIGHT, 0);
		}
		else {
			input_set_button_state(INPUT_GAMEPAD_L_STICK_RIGHT, stick_x);
			input_set_button_state(INPUT_GAMEPAD_L_STICK_LEFT, 0);
		}
		if (stick_y < 0) {
			input_set_button_state(INPUT_GAMEPAD_L_STICK_UP, -stick_y);
			input_set_button_state(INPUT_GAMEPAD_L_STICK_DOWN, 0);
		}
		else {
			input_set_button_state(INPUT_GAMEPAD_L_STICK_DOWN, stick_y);
			input_set_button_state(INPUT_GAMEPAD_L_STICK_UP, 0);
		}
}

float Sys_FloatTime(void) {
  struct timeval tp;
  struct timezone tzp;
  static int secbase;

  gettimeofday(&tp, &tzp);

#define divisor (1 / 1000000.0f)

  if (!secbase) {
    secbase = tp.tv_sec;
    return tp.tv_usec * divisor;
  }

  return (tp.tv_sec - secbase) + tp.tv_usec * divisor;
}


float platform_now(void) {
	return (float)Sys_FloatTime();
}

bool platform_get_fullscreen(void) {
	return true;
}

void platform_set_fullscreen(bool fullscreen) {
}

void platform_audio_callback(void* userdata, uint8_t* stream, int len) {
}

void platform_set_audio_mix_cb(void (*cb)(float *buffer, uint32_t len)) {
}


FILE *platform_open_asset(const char *name, const char *mode) {
	char *path = strcat(strcpy(temp_path, path_assets), name);
	return fopen(path, mode);
}

uint8_t *platform_load_asset(const char *name, uint32_t *bytes_read) {
	char *path = strcat(strcpy(temp_path, path_assets), name);
	return file_load(path, bytes_read);
}


#include <kos.h>
#include <dc/vmu_fb.h>
#include <dc/vmu_pkg.h>
#if 0
#include "vmudata.h"

int32_t ControllerPakStatus = 1;
int32_t Pak_Memory = 0;
int32_t Pak_Size = 0;
uint8_t *Pak_Data;
dirent_t __attribute__((aligned(32))) FileState[200];

static char full_fn[512];

static char *get_vmu_fn(maple_device_t *vmudev, char *fn) {
	if (fn)
		sprintf(full_fn, "/vmu/%c%d/%s", 'a'+vmudev->port, vmudev->unit, fn);
	else
		sprintf(full_fn, "/vmu/%c%d", 'a'+vmudev->port, vmudev->unit);

	return full_fn;
}

int vmu_check(void)
{
	maple_device_t *vmudev = NULL;

	ControllerPakStatus = 0;

	vmudev = maple_enum_type(0, MAPLE_FUNC_MEMCARD);
	if (!vmudev)
		return -1;

	file_t d;
	dirent_t *de;

	d = fs_open(get_vmu_fn(vmudev, NULL), O_RDONLY | O_DIR);
	if(!d)
		return -2;

	Pak_Memory = 200;

	memset(FileState, 0, sizeof(dirent_t)*200);

	int FileCount = 0;
	while (NULL != (de = fs_readdir(d))) {
		if (strcmp(de->name, ".") == 0)
			continue;
		if (strcmp(de->name, "..") == 0)
			continue;

		memcpy(&FileState[FileCount++], de, sizeof(dirent_t));			
		Pak_Memory -= (de->size / 512);
	}

	fs_close(d);

	ControllerPakStatus = 1;

	return 0;
}

static vmu_pkg_t pkg;

#define USERDATA_BLOCK_COUNT 6
#include "owl.h"

unsigned char tmp[6 * 32];

void fix_xbm(unsigned char *p)
{
	for (int i = 31; i > -1; i--) {
		memcpy(&tmp[(31 - i) * 6], &p[i * 6], 6);
	}

	memcpy(p, tmp, 6 * 32);

	for (int j = 0; j < 32; j++) {
		for (int i = 0; i < 6; i++) {
			uint8_t tmpb = p[(j * 6) + (5 - i)];
			tmp[(j * 6) + i] = tmpb;
		}
	}

	memcpy(p, tmp, 6 * 32);
}

void vmu_icon(void) {
	maple_device_t *vmudev = NULL;

	fix_xbm(owl2_bits);

	if ((vmudev = maple_enum_type(0, MAPLE_FUNC_LCD)))
		vmu_draw_lcd(vmudev, owl2_bits);
//		vmufb_present(&vmubuf, vmudev);
//	vmu_set_icon(owl_data);	
}
#endif
uint8_t *platform_load_userdata(const char *name, uint32_t *bytes_read) {
//	vmu_check();
//	if (!ControllerPakStatus) {
		*bytes_read = 0;
		return NULL;
//	}
/*
	ssize_t size;
	maple_device_t *vmudev = NULL;
	uint8_t *data;

	ControllerPakStatus = 0;

	vmudev = maple_enum_type(0, MAPLE_FUNC_MEMCARD);
	if (!vmudev) {
		*bytes_read = 0;
		return NULL;
	}

	file_t d = fs_open(get_vmu_fn(vmudev, "wipeout.dat"), O_RDONLY);
	if (!d) {
		*bytes_read = 0;
		return NULL;
	}

	size = fs_total(d);
	data = calloc(1, size);

	if (!data) {
		fs_close(d);
 		*bytes_read = 0;
		return NULL;
	}

	memset(&pkg, 0, sizeof(pkg));
	ssize_t res = fs_read(d, data, size);

	if (res < 0) {
		fs_close(d);
 		*bytes_read = 0;
		return NULL;
	}
	ssize_t total = res;
	while (total < size) {
		res = fs_read(d, data + total, size - total);
		if (res < 0) {
			fs_close(d);
			*bytes_read = 0;
			return NULL;
		}
		total += res;
	}

	if (total != size) {
		fs_close(d);
 		*bytes_read = 0;
		return NULL;
	}

	fs_close(d);

	if(vmu_pkg_parse(data, &pkg) < 0) {
		free(data);
 		*bytes_read = 0;
		return NULL;
	}

	uint8_t *bytes = mem_temp_alloc(pkg.data_len);
	if (!bytes) {
		free(data);
 		*bytes_read = 0;
		return NULL;
	}

	memcpy(bytes, pkg.data, pkg.data_len);
	ControllerPakStatus = 1;
	free(data);

	*bytes_read = pkg.data_len;
	return bytes;*/
}

uint32_t platform_store_userdata(const char *name, void *bytes, int32_t len) {
/*	uint8 *pkg_out;
	ssize_t pkg_size;
	maple_device_t *vmudev = NULL;

	vmu_check();
	if (!ControllerPakStatus) {*/
		return 0;
/*	}

	ControllerPakStatus = 0;

	vmudev = maple_enum_type(0, MAPLE_FUNC_MEMCARD);
	if (!vmudev)
		return 0;

	memset(&pkg, 0, sizeof(vmu_pkg_t));
	strcpy(pkg.desc_short,"Wipeout userdata");
	strcpy(pkg.desc_long, "Wipeout userdata");
	strcpy(pkg.app_id, "Wipeout");
	pkg.icon_cnt = 3;
	pkg.icon_data = icon1_data;
	memcpy(pkg.icon_pal, vmu_icon_pal, sizeof(vmu_icon_pal));
	pkg.data_len = len;
	pkg.data = bytes;

	file_t d = fs_open(get_vmu_fn(vmudev, "wipeout.dat"), O_RDONLY);
	if (!d) {
		if (Pak_Memory < USERDATA_BLOCK_COUNT)
			return 0;
		d = fs_open(get_vmu_fn(vmudev, "wipeout.dat"), O_RDWR | O_CREAT);
		if (!d)
			return 0;
	} else {
		fs_close(d);
		d = fs_open(get_vmu_fn(vmudev, "wipeout.dat"), O_WRONLY);
		if (!d)
			return 0;
	}

	vmu_pkg_build(&pkg, &pkg_out, &pkg_size);
	if (!pkg_out || pkg_size <= 0) {
		fs_close(d);
		return 0;
	}

	ssize_t rv = fs_write(d, pkg_out, pkg_size);
	ssize_t total = rv;
	while (total < pkg_size) {
		rv = fs_write(d, pkg_out + total, pkg_size - total);
		if (rv < 0) {
			fs_close(d);
			return -2;
		}
		total += rv;
	}

	fs_close(d);

	free(pkg_out);

	if (total == pkg_size) {
		ControllerPakStatus = 1;
		return len;
	} else {
	    return 0;
	}*/
}

	#define PLATFORM_WINDOW_FLAGS 0
	static void *screenbuffer_pixels = NULL;
	static int screenbuffer_pitch;
	static vec2i_t screenbuffer_size = vec2i(0, 0);
	static vec2i_t screen_size = vec2i(0, 0);

	void platform_video_init(void) {
	}

	void platform_video_cleanup(void) {
	}

	void platform_prepare_frame(void) {
	}

	void platform_end_frame(void) {
	}

	rgba_t *platform_get_screenbuffer(int32_t *pitch) {
		return NULL;
	}

	vec2i_t platform_screen_size(void) {
//		int width, height;
//		SDL_GetWindowSize(window, &width, &height);

		// float aspect = (float)width / (float)height;
		// screen_size = vec2i(240 * aspect, 240);
		screen_size = vec2i(640,480);//width, height);
		return screen_size;
	}

#include <kos.h>

extern int wav_init(void);
int main(int argc, char *argv[]) {
	// Figure out the absolute asset and userdata paths. These may either be
	// supplied at build time through -DPATH_ASSETS=.. and -DPATH_USERDATA=..
	// or received at runtime from SDL. Note that SDL may return NULL for these.
	// We fall back to the current directory (i.e. just "") in this case.
	file_t f = fs_open("/pc/wipeout/common/mine.cmp", O_RDONLY);
	if (f != -1) {
		fs_close(f);
		f = 0;
		path_assets = "/pc";
		path_userdata = "/pc/wipeout";		
	} else {
		f = fs_open("/cd/wipeout/common/mine.cmp", O_RDONLY);
		if (f != -1) {
			fs_close(f);
			f = 0;
			path_assets = "/cd";
			path_userdata = "/cd/wipeout";				
		} else {
		printf("CANT FIND ASSETS ON /PC or /CD; TERMINATING!\n");
		exit(-1);
		}
	}

	wav_init();
//	vmu_icon();

	// Reserve some space for concatenating the asset and userdata paths with
	// local filenames.
	temp_path = mem_bump(max(strlen(path_assets), strlen(path_userdata)) + 64);

	// Load gamecontrollerdb.txt if present.
	// FIXME: Should this load from userdata instead?
//	char *gcdb_path = strcat(strcpy(temp_path, path_assets), "gamecontrollerdb.txt");
//	int gcdb_res = SDL_GameControllerAddMappingsFromFile(gcdb_path);
//	if (gcdb_res < 0) {
//		printf("Failed to load gamecontrollerdb.txt\n");
//	}
//	else {
//		printf("load gamecontrollerdb.txt\n");
//	}

	gamepad = platform_find_gamepad();

	platform_video_init();
	system_init();

	while (!wants_to_exit) {
		platform_pump_events();
		platform_prepare_frame();
		system_update();
		platform_end_frame();
	}

	system_cleanup();
	platform_video_cleanup();

	return 0;
}
