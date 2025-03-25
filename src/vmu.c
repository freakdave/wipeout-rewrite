#include <kos.h>
#include <dc/vmu_fb.h>
#include <dc/vmu_pkg.h>

#if 1
#include "vmudata.h"

int32_t ControllerPakStatus = 1;
int32_t Pak_Memory = 0;

static char full_fn[128];

char *get_vmu_fn(maple_device_t *vmudev, char *fn) {
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

	while (NULL != (de = fs_readdir(d))) {
		if (strcmp(de->name, ".") == 0)
			continue;
		if (strcmp(de->name, "..") == 0)
			continue;

		Pak_Memory -= (de->size / 512);
	}

	fs_close(d);

	ControllerPakStatus = 1;

	return 0;
}

#include "owl.h"

void fix_xbm(unsigned char *p)
{
    unsigned char tmp[6*32];
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

void draw_vmu_icon(void) {
	maple_device_t *vmudev = NULL;

	fix_xbm(owl2_bits);

	if ((vmudev = maple_enum_type(0, MAPLE_FUNC_LCD)))
		vmu_draw_lcd(vmudev, owl2_bits);
//		vmufb_present(&vmubuf, vmudev);
//	vmu_set_icon(owl_data);	
}
#endif