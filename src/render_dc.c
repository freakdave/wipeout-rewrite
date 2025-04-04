#include "system.h"
#include "render.h"
#include "mem.h"
#include "utils.h"

#include <kos.h>

extern int load_OP;
extern int drawing_text;
void mat4_mul_fipr(mat4_t *res, mat4_t *a, mat4_t *b);

const float R_ViewportMatrix[4][4] = {
        {320.0f,    0.0f, 0.0f, 0.0f},
        {0.0f,   -240.0f, 0.0f, 0.0f},
        {0.0f,      0.0f, 1.0f, 0.0f},
        {320.0f,  240.0f, 0.0f, 1.0f},
};

int modes_dirty = 0;

const float R_Ident[4][4] = {
	{1,0,0,0},
	{0,1,0,0},
	{0,0,1,0},
	{0,0,0,1}
};

pvr_vertex_t __attribute__((aligned(32))) vs[5];

mat4_t viewport;

float screen_2d_z = -1.0f;

#define RENDER_STATEMAP(fg,w,t,c,bl) ((int)(fg) | ((int)(w) << 1) | ((int)(t) << 2) | ((int)(c) << 3) | ((int)(bl) << 4))

uint8_t cur_mode;

#define TEXTURES_MAX 1024
// next power of 2 greater than / equal to v
static inline uint32_t np2(uint32_t v)
{
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

#define NEAR_PLANE 16.0f
#define FAR_PLANE (RENDER_FADEOUT_FAR)

typedef struct {
	// 0 - 7
	vec2i_t offset;
	// 8 - 15
	vec2i_t size;
} render_texture_t;

uint8_t __attribute__((aligned(32))) last_mode[1024] = {0};

float doffset = 0.0f;

pvr_ptr_t __attribute__((aligned(32))) ptrs[1024] = {0};

uint16_t RENDER_NO_TEXTURE;

static vec2i_t screen_size;
static vec2i_t backbuffer_size;

static render_blend_mode_t blend_mode = RENDER_BLEND_NORMAL;

static mat4_t __attribute__((aligned(32))) projection_mat = mat4_identity();
static mat4_t __attribute__((aligned(32))) sprite_mat = mat4_identity();
static mat4_t __attribute__((aligned(32))) view_mat = mat4_identity();
static mat4_t __attribute__((aligned(32))) mvp_mat = mat4_identity();

pvr_dr_state_t dr_state;


static render_texture_t __attribute__((aligned(32))) textures[TEXTURES_MAX];
static uint32_t textures_len = 0;

static render_resolution_t render_res;

int dep_en = 0;
int cull_en = 0;
int test_en = 0;
pvr_poly_hdr_t __attribute__((aligned(32))) *chdr[1024] = {0};
pvr_poly_hdr_t chdr_notex;
pvr_poly_cxt_t ccxt;
pvr_vertex_t verts[4];

#include "wipeout/game.h"

void update_header(uint16_t texture_index, uint32_t notflat) {

		uint32_t *hp = (uint32_t *)chdr[texture_index];

// if you want to get flat-shaded polys for F3,F4,FT3,FT4, uncomment next 4 lines, but FPS will drop
//		if (notflat < 2u)
//			hp[0] = 0x82840008 + (notflat << 1);
//		else
//			hp[0] = 0x8284000a; //0x8284000e; e is specular enable, a is disable

		uint32_t header1 = hp[1];

		// depth write
		if (dep_en)
			header1 &= ~(1 << 26);
		else
			header1 = (header1 & ~(1 << 26)) | (1 << 26);

		// depth test
		if (test_en)
			header1 = (header1 & 0x1fffffff) | (PVR_DEPTHCMP_GREATER << 29);
		else
			header1 = (header1 & 0x1fffffff) | (PVR_DEPTHCMP_ALWAYS << 29);

		// culling on or off
		if (cull_en)
			header1 |= 0x10000000;
		else
			header1 &= 0xEFFFFFFF;

		hp[1] = header1;

		uint32_t header2 = hp[2];
		// normal or brighter
		if (!blend_mode)
			header2 = (header2 & 0x00FFFFFF) | 0x94000000;
		else
			header2 = (header2 & 0x00FFFFFF) | 0x84000000;

		hp[2] = header2;
}

extern int LOAD_UNFILTERED;

void compile_header(uint16_t texture_index) {
	render_texture_t *t = &textures[texture_index];

	if ((texture_index != 0)) {
		chdr[texture_index] = memalign(32, sizeof(pvr_poly_hdr_t));

		if (!load_OP) {
			if (LOAD_UNFILTERED) {
				pvr_poly_cxt_txr(&ccxt, PVR_LIST_TR_POLY, PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_TWIDDLED, t->offset.x, t->offset.y, ptrs[texture_index], PVR_FILTER_NONE);			
			} else {
				pvr_poly_cxt_txr(&ccxt, PVR_LIST_TR_POLY, PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_TWIDDLED, t->offset.x, t->offset.y, ptrs[texture_index], PVR_FILTER_BILINEAR);			
			}
		} else {
			if (LOAD_UNFILTERED) {
				pvr_poly_cxt_txr(&ccxt, PVR_LIST_OP_POLY, PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_TWIDDLED, t->offset.x, t->offset.y, ptrs[texture_index], PVR_FILTER_NONE);			
			} else {
				pvr_poly_cxt_txr(&ccxt, PVR_LIST_OP_POLY, PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_TWIDDLED, t->offset.x, t->offset.y, ptrs[texture_index], PVR_FILTER_BILINEAR);			
			}
		}

//		ccxt.gen.specular = PVR_SPECULAR_ENABLE;
		//ccxt.gen.fog_type = PVR_FOG_TABLE;
		ccxt.depth.write = PVR_DEPTHWRITE_DISABLE;
		ccxt.depth.comparison = PVR_DEPTHCMP_NEVER;
		pvr_poly_compile(chdr[texture_index], &ccxt);
	} else if (texture_index == 0) {
		pvr_poly_cxt_col(&ccxt, PVR_LIST_TR_POLY);
//		ccxt.gen.specular = PVR_SPECULAR_ENABLE;
		//ccxt.gen.fog_type = PVR_FOG_TABLE;
		pvr_poly_compile(&chdr_notex, &ccxt);
	}
}

pvr_poly_hdr_t hud_hdr;
uint16_t HUD_NO_TEXTURE = 65535;

void render_init(vec2i_t screen_size) {	
	cull_en = 1;
	dep_en = 1;
	test_en = 1;
	blend_mode = RENDER_BLEND_NORMAL;

	cur_mode = RENDER_STATEMAP(0,1,1,1,0);

	vs[0].flags = PVR_CMD_VERTEX;
	vs[1].flags = PVR_CMD_VERTEX;
	vs[2].flags = PVR_CMD_VERTEX_EOL;
	vs[3].flags = PVR_CMD_VERTEX_EOL;
	vs[4].flags = PVR_CMD_VERTEX_EOL;	

	for(int i=0;i<4;i++)
		for(int j=0;j<4;j++)
			viewport.cols[i][j] = R_ViewportMatrix[i][j];

	uint16_t white_pixels[4] = {0xffff,0xffff,0xffff,0xffff};

	RENDER_NO_TEXTURE = render_texture_create(2, 2, white_pixels);

	pvr_poly_cxt_col(&ccxt, PVR_LIST_TR_POLY);
	ccxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
	pvr_poly_compile(&hud_hdr, &ccxt);

	render_res = RENDER_RES_NATIVE;
	render_set_screen_size(screen_size);
#define PVR_MIN_Z 0.000010f
	pvr_set_zclip(PVR_MIN_Z);
#if 0
	pvr_fog_table_color(1.0f, 0.5f, 0.5f, 0.5f);
	pvr_fog_table_linear(RENDER_FADEOUT_NEAR, RENDER_FADEOUT_FAR);
#endif
}

void render_cleanup(void) {
	// TODO
}

mat4_t __attribute__((aligned(32))) vp_mat;

void render_set_screen_size(vec2i_t size) {
	screen_size = size;

	float aspect = (float)size.x / (float)size.y;
	float fov = (73.75 / 180.0) * 3.14159265358;
	float f = 1.0 / tan(fov / 2);
	float nf = 1.0 / (NEAR_PLANE - FAR_PLANE);
	projection_mat = mat4(
		f / aspect, 0, 0, 0,
		0, f, 0, 0,
		0, 0, (FAR_PLANE + NEAR_PLANE) * nf, -1,
		0, 0, 2 * FAR_PLANE * NEAR_PLANE * nf, 0);
	//mat4_mul_fipr(&vp_mat, &viewport, &projection_mat);
	memcpy(&vp_mat.m[0], &projection_mat.m[0], 16*4);
}

void render_set_resolution(render_resolution_t res) {
	render_res = res;

	if (res == RENDER_RES_NATIVE) {
		backbuffer_size = screen_size;
	}
	else {
		float aspect = (float)screen_size.x / (float)screen_size.y;
		if (res == RENDER_RES_240P) {
			backbuffer_size = vec2i(240.0 * aspect, 240);
		} else if (res == RENDER_RES_480P) {
			backbuffer_size = vec2i(480.0 * aspect, 480);	
		} else {
			die("Invalid resolution: %d", res);
		}
	}
}

void render_set_post_effect(render_post_effect_t post) {
}

vec2i_t render_size(void) {
	return backbuffer_size;
}

extern int in_race;
extern int in_menu;

void render_frame_prepare(void) {
	render_set_depth_write(true);
	render_set_depth_test(true);

	pvr_set_bg_color(0.0f,0.0f,0.0f);
	pvr_scene_begin();

	if (in_menu || in_race) {
	 	pvr_list_begin(PVR_LIST_OP_POLY);
	}
	else {
		pvr_list_begin(PVR_LIST_TR_POLY);
	}
	pvr_dr_init(&dr_state);

	vs[0].flags = PVR_CMD_VERTEX;
	vs[1].flags = PVR_CMD_VERTEX;
	vs[2].flags = PVR_CMD_VERTEX_EOL;
	vs[3].flags = PVR_CMD_VERTEX_EOL;
	vs[4].flags = PVR_CMD_VERTEX_EOL;	
}

void render_frame_end(void) {
    pvr_list_finish();
    pvr_scene_finish();
    pvr_wait_ready();

	screen_2d_z = -1.0f;
}

void render_set_view(vec3_t pos, vec3_t angles) {
	render_set_depth_write(true);
	render_set_depth_test(true);
	view_mat = mat4_identity();
	mat4_set_translation(&view_mat, vec3(0, 0, 0));
	mat4_set_roll_pitch_yaw(&view_mat, vec3(angles.x, -angles.y + F_PI, angles.z + F_PI));
	mat4_translate(&view_mat, vec3_inv(pos));
	mat4_set_yaw_pitch_roll(&sprite_mat, vec3(-angles.x, angles.y - F_PI, 0));
}

static mat4_t __attribute__((aligned(32))) vm_mat;
static mat4_t __attribute__((aligned(32))) vm2_mat;

void render_set_view_2d(void) {
	render_set_depth_test(false);
	render_set_depth_write(false);
	float near = -1;
	float far = 1;
	float left = 0;
	float right = screen_size.x;
	float bottom = screen_size.y;
	float top = 0;
	float lr = 1 / (left - right);
	float bt = 1 / (bottom - top);
	float nf = 1 / (near - far);
	mvp_mat = mat4(
		-2 * lr, 0, 0, 0,
		0, -2 * bt, 0, 0,
		0, 0, 2 * nf, 0,
		(left + right) * lr, (top + bottom) * bt, (far + near) * nf, 1);
	//mat4_mul_fipr(&vm2_mat, &viewport, &mvp_mat);
	memcpy(&vm2_mat.m[0], &mvp_mat.m[0], 16*4);
	mat_load(&vm2_mat.cols);
}


void render_set_model_mat(mat4_t *m) {
	mat4_mul_fipr(&vm_mat, &view_mat, m);
	//mat4_mul(&vm2_mat, &projection_mat, &vm_mat);
	//mat4_mul(&mvp_mat, &viewport, &vm2_mat);
	//mat_load(&mvp_mat.cols);
	mat4_mul(&mvp_mat, &vp_mat, &vm_mat);
}

void render_set_depth_write(bool enabled) {
	if ((int)enabled != dep_en) {
		dep_en = enabled;
		cur_mode = RENDER_STATEMAP(0,dep_en,test_en,cull_en,blend_mode);		
	}
}

void render_set_depth_test(bool enabled) {
	if ((int)enabled != test_en) {
		test_en = enabled;
		cur_mode = RENDER_STATEMAP(0,dep_en,test_en,cull_en,blend_mode);		
	}
}

void render_set_depth_offset(float offset) {
	doffset = offset;
}

void render_set_screen_position(vec2_t pos) {
}

void render_set_blend_mode(render_blend_mode_t new_mode) {
	if (new_mode != blend_mode) {
		blend_mode = new_mode;
		cur_mode = RENDER_STATEMAP(0,dep_en,test_en,cull_en,blend_mode);		
	}	
}

void render_set_cull_backface(bool enabled) {
	if (enabled != cull_en) {
		cull_en = enabled;
		cur_mode = RENDER_STATEMAP(0,dep_en,test_en,cull_en,blend_mode);
	}
}

static uint16_t last_index = 1025;

vec3_t render_transform(vec3_t pos) {
	//return vec3_transform(vec3_transform(pos, &view_mat), &projection_mat);
	return vec3_transform(pos, &mvp_mat);
}

static float wout; //xout,yout,zout,,uout,vout;

#define cliplerp(__a, __b, __t) ((__a) + (((__b) - (__a))*(__t)))

static uint32_t color_lerp(float ft, uint32_t c1, uint32_t c2) {
	uint8_t t = (ft * 255);
   	uint32_t maskRB = 0xFF00FF;  // Mask for Red & Blue channels
    uint32_t maskG  = 0x00FF00;  // Mask for Green channel
    uint32_t maskA  = 0xFF000000; // Mask for Alpha channel

    // Interpolate Red & Blue
    uint32_t rb = ((((c2 & maskRB) - (c1 & maskRB)) * t) >> 8) + (c1 & maskRB);
    
    // Interpolate Green
    uint32_t g  = ((((c2 & maskG) - (c1 & maskG)) * t) >> 8) + (c1 & maskG);

    // Interpolate Alpha
    uint32_t a  = ((((c2 & maskA) >> 24) - ((c1 & maskA) >> 24)) * t) >> 8;
    a = (a + (c1 >> 24)) << 24;  // Shift back into position

    return (a & maskA) | (rb & maskRB) | (g & maskG);
}

static void nearz_clip(pvr_vertex_t *v0, pvr_vertex_t *v1, pvr_vertex_t *outv, float w0, float w1) {
	const float d0 = w0 + v0->z;
	const float d1 = w1 + v1->z;
	float d1subd0 = d1 - d0;
	if (d1subd0 == 0.0f) d1subd0 = 0.0001f;
	float t = (fabsf(d0) * approx_recip(d1subd0)) + 0.000001f;
	outv->x = cliplerp(v0->x, v1->x, t);
	outv->y = cliplerp(v0->y, v1->y, t);
	outv->z = cliplerp(v0->z, v1->z, t);
	outv->u = cliplerp(v0->u, v1->u, t);
	outv->v = cliplerp(v0->v, v1->v, t);
	outv->argb = color_lerp(t, v0->argb, v1->argb);
	outv->oargb = color_lerp(t, v0->oargb, v1->oargb);
	wout = cliplerp(w0, w1, t);
}

static inline void perspdiv(float *x, float *y, float *z, float w)
{
	float invw = approx_recip(w);
	float _x = *x * invw;
	float _y = *y * invw;
#if RENDER_USE_FSAA
	_x = 640 + (640*_x);
#else
	_x = 320 + (320*_x);
#endif
	_y = 240 - (240*_y);
	*x = _x;
	*y = _y;
	*z = invw;
}

void render_noclip_quad(uint16_t texture_index) {
	float w0,w1,w2,w3;
	pvr_prim(&hud_hdr, sizeof(pvr_poly_hdr_t));

	mat_trans_single3_nodivw(vs[0].x, vs[0].y, vs[0].z, w0);
	mat_trans_single3_nodivw(vs[1].x, vs[1].y, vs[1].z, w1);
	mat_trans_single3_nodivw(vs[2].x, vs[2].y, vs[2].z, w2);
	mat_trans_single3_nodivw(vs[3].x, vs[3].y, vs[3].z, w3);

	perspdiv(&vs[0].x, &vs[0].y, &vs[0].z, w0);
	perspdiv(&vs[1].x, &vs[1].y, &vs[1].z, w1);
	perspdiv(&vs[2].x, &vs[2].y, &vs[2].z, w2);
	perspdiv(&vs[3].x, &vs[3].y, &vs[3].z, w3);

	pvr_prim(&vs[0], sizeof(pvr_vertex_t) * 4);
}

void render_quad(uint16_t texture_index) {
#define p0 vs[0]
#define p1 vs[1]
#define p2 vs[2]
#define p3 vs[3]
	render_texture_t *t = &textures[texture_index];
	int notex = (texture_index == RENDER_NO_TEXTURE);
	float w0,w1,w2,w3,w4;

	uint8_t clip_rej[4];

	mat_trans_single3_nodivw(p0.x,p0.y,p0.z,w0);
	mat_trans_single3_nodivw(p1.x,p1.y,p1.z,w1);
	mat_trans_single3_nodivw(p2.x,p2.y,p2.z,w2);
	mat_trans_single3_nodivw(p3.x,p3.y,p3.z,w3);

	clip_rej[0] = 0;
    if (p0.x < -w0) clip_rej[0] |= 1;
    if (p0.x >  w0) clip_rej[0] |= 2;
    if (p0.y < -w0) clip_rej[0] |= 4;
    if (p0.y >  w0) clip_rej[0] |= 8;
    if (p0.z < -w0) clip_rej[0] |= 16;
    if (p0.z >  w0) clip_rej[0] |= 32;

	clip_rej[1] = 0;
    if (p1.x < -w1) clip_rej[1] |= 1;
    if (p1.x >  w1) clip_rej[1] |= 2;
    if (p1.y < -w1) clip_rej[1] |= 4;
    if (p1.y >  w1) clip_rej[1] |= 8;
    if (p1.z < -w1) clip_rej[1] |= 16;
    if (p1.z >  w1) clip_rej[1] |= 32;

	clip_rej[2] = 0;
    if (p2.x < -w2) clip_rej[2] |= 1;
    if (p2.x >  w2) clip_rej[2] |= 2;
    if (p2.y < -w2) clip_rej[2] |= 4;
    if (p2.y >  w2) clip_rej[2] |= 8;
    if (p2.z < -w2) clip_rej[2] |= 16;
	if (p2.z >  w2) clip_rej[2] |= 32;		

	clip_rej[3] = 0;
    if (p3.x < -w3) clip_rej[3] |= 1;
    if (p3.x >  w3) clip_rej[3] |= 2;
    if (p3.y < -w3) clip_rej[3] |= 4;
    if (p3.y >  w3) clip_rej[3] |= 8;
    if (p3.z < -w3) clip_rej[3] |= 16;
    if (p3.z >  w3) clip_rej[3] |= 32;		

	if (clip_rej[0] & clip_rej[1] & clip_rej[2] & clip_rej[3]) {
        return;
    }

#define x0 p0.x
#define y0 p0.y
#define z0 p0.z
#define u0 p0.u
#define v0 p0.v
#define __w0 w0

#define x1 p1.x
#define y1 p1.y
#define z1 p1.z
#define u1 p1.u
#define v1 p1.v
#define __w1 w1

#define x2 p2.x
#define y2 p2.y
#define z2 p2.z
#define u2 p2.u
#define v2 p2.v
#define __w2 w2

#define x3 p3.x
#define y3 p3.y
#define z3 p3.z
#define u3 p3.u
#define v3 p3.v
#define __w3 w3

	uint32_t vismask = ((z0 > -__w0) | ((z1 > -__w1) << 1) | ((z2 > -__w2) << 2) | ((z3 > -__w3) << 3));

//	dbgio_printf("vismask %02x\n", vismask);

	if (vismask == 0)
		return;
	if (vismask == 6)
		return;
	if (vismask == 9)
		return;

	if (!notex) {
		float up2,vp2;

		up2 = approx_recip((float)t->offset.x);
		vp2 = approx_recip((float)t->offset.y);

		p0.u *= up2;
		p1.u *= up2;
		p2.u *= up2;
		p3.u *= up2;

		p0.v *= vp2;
		p1.v *= vp2;
		p2.v *= vp2;
		p3.v *= vp2;		
	}

	int sendverts = 4;

	if (vismask == 15) {
		goto sendit;
	} else {
		switch (vismask)
		{
	// quad only 0 visible
	case 1:
		sendverts = 3;

		nearz_clip(&p0, &p1, &p0, __w0, __w1);
		__w0 = wout;
		nearz_clip(&p0, &p2, &p2, __w0, __w2);
		__w2 = wout;
		vs[2].flags = PVR_CMD_VERTEX_EOL;

		break;

	// quad only 1 visible
	case 2:
		sendverts = 3;

		nearz_clip(&p0, &p1, &p0, __w0, __w1);
		nearz_clip(&p1, &p3, &p2, __w1, __w3);
		__w2 = wout;
		vs[2].flags = PVR_CMD_VERTEX_EOL;

		break;

	// quad 0 + 1 visible
	case 3:
		nearz_clip(&p0, &p2, &p2, __w0, __w2);
		__w2 = wout;
		nearz_clip(&p1, &p3, &p3, __w1, __w3);
		__w3 = wout;

		break;

	// quad only 2 visible
	case 4:
		sendverts = 3;

		nearz_clip(&p0, &p2, &p0, __w0, __w2);
		__w0 = wout;
		nearz_clip(&p2, &p3, &p1, __w2, __w3);
		__w1 = wout;

		vs[2].flags = PVR_CMD_VERTEX_EOL;

		break;

	// quad 0 + 2 visible
	case 5:
		nearz_clip(&p0, &p1, &p1, __w0, __w1);
		__w1 = wout;
		nearz_clip(&p2, &p3, &p3, __w2, __w3);
		__w3 = wout;

		break;

	// quad 0 + 1 + 2 visible
	case 7:
		sendverts = 5;

		nearz_clip(&p2, &p3, &vs[4], __w2, __w3);
		w4 = wout;
		nearz_clip(&p1, &p3, &p3, __w1, __w3);
		__w3 = wout;

		vs[3].flags = PVR_CMD_VERTEX;

		break;

	// quad only 3 visible
	case 8:
		sendverts = 3;

		nearz_clip(&p1, &p3, &p0, __w1, __w3);
		__w0 = wout;
		nearz_clip(&p2, &p3, &p2, __w2, __w3);
		__w2 = wout;

		memcpy(&p1, &p3, 32);
		__w1 = __w3;

		vs[1].flags = PVR_CMD_VERTEX;
		vs[2].flags = PVR_CMD_VERTEX_EOL;

		break;

	// quad 1 + 3 visible
	case 10:
		nearz_clip(&p0, &p1, &p0, __w0, __w1);
		__w0 = wout;
		nearz_clip(&p2, &p3, &p2, __w2, __w3);
		__w2 = wout;

		break;

	// quad 0 + 1 + 3 visible
	case 11:
		sendverts = 5;

		nearz_clip(&p2, &p3, &vs[4], __w2, __w3);
		w4 = wout;
		nearz_clip(&p0, &p2, &p2, __w0, __w2);
		__w2 = wout;

		vs[3].flags = PVR_CMD_VERTEX;

		break;

	// quad 2 + 3 visible
	case 12:
		nearz_clip(&p0, &p2, &p0, __w0, __w2);
		__w0 = wout;
		nearz_clip(&p1, &p3, &p1, __w1, __w3);
		__w1 = wout;

		break;

	// quad 0 + 2 + 3 visible
	case 13:
		sendverts = 5;

		memcpy(&vs[4], &p3, 32);
		w4 = __w3;

		nearz_clip(&p1, &p3, &p3, __w1, __w3);
		__w3 = wout;
		nearz_clip(&p0, &p1, &p1, __w0, __w1);
		__w1 = wout;

		vs[3].flags = PVR_CMD_VERTEX;
		vs[4].flags = PVR_CMD_VERTEX_EOL;

		break;

	// quad 1 + 2 + 3 visible
	case 14:
		sendverts = 5;

		memcpy(&vs[4], &p2, 32);
		w4 = __w2;

		nearz_clip(&p0, &p2, &p2, __w0, __w2);
		__w2 = wout;
		nearz_clip(&p0, &p1, &p0, __w0, __w1);
		__w0 = wout;

		vs[3].flags = PVR_CMD_VERTEX;
		vs[4].flags = PVR_CMD_VERTEX_EOL;
		break;
		}
	}
#undef x0
#undef y0
#undef z0
#undef u0
#undef v0
#undef __w0
#undef x1
#undef y1
#undef z1
#undef u1
#undef v1
#undef __w1
#undef x2
#undef y2
#undef z2
#undef u2
#undef v2
#undef __w2
#undef x3
#undef y3
#undef z3
#undef u3
#undef v3
#undef __w3
sendit:

	perspdiv(&p0.x, &p0.y, &p0.z, w0);
	perspdiv(&p1.x, &p1.y, &p1.z, w1);
	perspdiv(&p2.x, &p2.y, &p2.z, w2);
	if (sendverts >= 4) {
		perspdiv(&p3.x, &p3.y, &p3.z, w3);
	}
	if (sendverts == 5) {
		perspdiv(&vs[4].x, &vs[4].y, &vs[4].z, w4);
	}


	// don't do anything header-related if we're on the same texture as the last call
	// automatic saving every time quads are pushed as two tris
	// and savings in plenty of other cases
	if (last_index != texture_index) {
		last_index = texture_index;
		if (cur_mode != last_mode[texture_index]) {
			if (texture_index < 1024) {
				update_header(texture_index, p0.oargb);
			}
		}
		if(__builtin_expect(notex,0)) {
			pvr_prim(&chdr_notex, sizeof(pvr_poly_hdr_t));
		} else {
			pvr_prim(chdr[texture_index], sizeof(pvr_poly_hdr_t));
		}
	}

	pvr_prim(vs, sendverts * 32);
#undef p3
#undef p2
#undef p1
#undef p0
}


void render_tri(uint16_t texture_index) {
#define p0 vs[0]
#define p1 vs[1]
#define p2 vs[2]
	render_texture_t *t = &textures[texture_index];
	int notex = (texture_index == RENDER_NO_TEXTURE);
	float w0,w1,w2,w3;

	uint8_t clip_rej[3];

	mat_trans_single3_nodivw(p0.x,p0.y,p0.z,w0);
	mat_trans_single3_nodivw(p1.x,p1.y,p1.z,w1);
	mat_trans_single3_nodivw(p2.x,p2.y,p2.z,w2);

	clip_rej[0] = 0;
    if (p0.x < -w0) clip_rej[0] |= 1;
    if (p0.x >  w0) clip_rej[0] |= 2;
    if (p0.y < -w0) clip_rej[0] |= 4;
    if (p0.y >  w0) clip_rej[0] |= 8;
    if (p0.z < -w0) clip_rej[0] |= 16;
    if (p0.z >  w0) clip_rej[0] |= 32;

	clip_rej[1] = 0;
    if (p1.x < -w1) clip_rej[1] |= 1;
    if (p1.x >  w1) clip_rej[1] |= 2;
    if (p1.y < -w1) clip_rej[1] |= 4;
    if (p1.y >  w1) clip_rej[1] |= 8;
    if (p1.z < -w1) clip_rej[1] |= 16;
    if (p1.z >  w1) clip_rej[1] |= 32;

	clip_rej[2] = 0;
    if (p2.x < -w2) clip_rej[2] |= 1;
    if (p2.x >  w2) clip_rej[2] |= 2;
    if (p2.y < -w2) clip_rej[2] |= 4;
    if (p2.y >  w2) clip_rej[2] |= 8;
    if (p2.z < -w2) clip_rej[2] |= 16;
    if (p2.z >  w2) clip_rej[2] |= 32;		

	if (clip_rej[0] & clip_rej[1] & clip_rej[2]) {
        return;
    }

#define x0 p0.x
#define y0 p0.y
#define z0 p0.z
#define u0 p0.u
#define v0 p0.v
#define __w0 w0

#define x1 p1.x
#define y1 p1.y
#define z1 p1.z
#define u1 p1.u
#define v1 p1.v
#define __w1 w1

#define x2 p2.x
#define y2 p2.y
#define z2 p2.z
#define u2 p2.u
#define v2 p2.v
#define __w2 w2
	uint32_t vismask = (z0 > -__w0) | ((z1 > -__w1) << 1) | ((z2 > -__w2) << 2);

	int usespare = 0;
	if (vismask == 0) {
		return;
	}
	
	if (!notex) {
		float up2,vp2;

		up2 = approx_recip((float)t->offset.x);
		vp2 = approx_recip((float)t->offset.y);

		p0.u *= up2;
		p1.u *= up2;
		p2.u *= up2;

		p0.v *= vp2;
		p1.v *= vp2;
		p2.v *= vp2;
	}
	
	if (vismask == 7) {
		goto sendit;
	} else {
		switch (vismask)
		{
		case 1:
			nearz_clip(&p0, &p1, &p1, __w0, __w1);
			__w1 = wout;
			nearz_clip(&p0, &p2, &p2, __w0, __w2);
			__w2 = wout;
			break;
		case 2:
			nearz_clip(&p0, &p1, &p0, __w0, __w1);
			__w0 = wout;
			nearz_clip(&p1, &p2, &p2, __w1, __w2);
			__w2 = wout;
			break;
		case 3:
			usespare = 1;
			nearz_clip(&p1, &p2, &vs[3], __w1, __w2);
			w3 = wout;
			nearz_clip(&p0, &p2, &p2, __w0, __w2);
			__w2 = wout;
			break;
		case 4:
			nearz_clip(&p0, &p2, &p0, __w0, __w2);
			__w0 = wout;
			nearz_clip(&p1, &p2, &p1, __w1, __w2);
			__w1 = wout;
			break;
		case 5:
			usespare = 1;
			nearz_clip(&p1, &p2, &vs[3], __w1, __w2);
			w3 = wout;
			nearz_clip(&p0, &p1, &p1, __w0, __w1);
			__w1 = wout;
			break;
		case 6:
			usespare = 1;
			memcpy(&vs[3], &p2, 32);
			w3 = __w2;
			nearz_clip(&p0, &p2, &p2, __w0, __w2);
			__w2 = wout;
			nearz_clip(&p0, &p1, &p0, __w0, __w1);
			__w0 = wout;
			break;
		}
	}
#undef x0
#undef y0
#undef z0
#undef u0
#undef v0
#undef __w0
#undef x1
#undef y1
#undef z1
#undef u1
#undef v1
#undef __w1
#undef x2
#undef y2
#undef z2
#undef u2
#undef v2
#undef __w2
sendit:

#if 0
	float zw0 = 1.0f - p0.z / w0;
	float zw1 = 1.0f - p1.z / w1;
	float zw2 = 1.0f - p2.z / w2;

	uint8_t a0 = ((p0.argb >> 24)&0x000000ff);
        uint8_t a1 = ((p1.argb >> 24)&0x000000ff);
        uint8_t a2 = ((p2.argb >> 24)&0x000000ff);

        if (a0 !=48) {
                a0 = clamp(a0 * zw0 * FAR_PLANE * (2.0/255.0), 0, 255);
                p0.argb = (p0.argb & 0x00ffffff) | (a0 << 24);
        }
        if (a1 != 48) {
                a1 = clamp(a1 * zw1 * FAR_PLANE * (2.0/255.0), 0, 255);
                p1.argb = (p1.argb & 0x00ffffff) | (a1 << 24);
        }
        if (a2 != 48) {
                a2 = clamp(a2 * zw2 * FAR_PLANE * (2.0/255.0), 0, 255);
                p2.argb = (p2.argb & 0x00ffffff) | (a2 << 24);
        }
#endif

	perspdiv(&p0.x, &p0.y, &p0.z, w0);
	perspdiv(&p1.x, &p1.y, &p1.z, w1);
	perspdiv(&p2.x, &p2.y, &p2.z, w2);

	// don't do anything header-related if we're on the same texture as the last call
	// automatic saving every time quads are pushed as two tris
	// and savings in plenty of other cases
	if (/* do_it_next_time || */ last_index != texture_index) {
		last_index = texture_index;
		//subhdr = 1;
		if (/* do_it_next_time || */ cur_mode != last_mode[texture_index]) {
			if (texture_index < 1024) {
				update_header(texture_index, p0.oargb);
			}
		}
//		do_it_next_time = 0;
		if(__builtin_expect(notex,0)) {
			pvr_prim(&chdr_notex, sizeof(pvr_poly_hdr_t));
		} else {
			pvr_prim(chdr[texture_index], sizeof(pvr_poly_hdr_t));
		}
	}

	if (usespare) {
		vs[2].flags = PVR_CMD_VERTEX;
#if 0
	float zw3 = 1.0f - vs[3].z / w3;

        uint8_t a3 = ((vs[3].argb >> 24)&0x000000ff);

        if (a3 != 48) {
                a3 = clamp(a3 * zw3 * FAR_PLANE * (2.0/255.0), 0, 255);
                vs[3].argb = (vs[3].argb & 0x00ffffff) | (a3 << 24);
        }
#endif
		perspdiv(&vs[3].x, &vs[3].y, &vs[3].z, w3);
//		if (drawing_text) {
//			vs[3].z = 10;
//		}

		pvr_prim(vs, 4 * 32);
	} else {
		pvr_prim(vs, 3 * 32);
	}
#undef p2
#undef p1
#undef p0
}

void render_push_sprite(vec3_t pos, vec2i_t size, rgba_t color, uint16_t texture_index) {
	//error_if(texture_index >= textures_len, "Invalid texture %d", texture_index);
	screen_2d_z += 0.001f;
	vec3_t p1 = vec3_add(pos, vec3_transform(vec3(-size.x * 0.5, -size.y * 0.5, screen_2d_z), &sprite_mat));
	vec3_t p2 = vec3_add(pos, vec3_transform(vec3( size.x * 0.5, -size.y * 0.5, screen_2d_z), &sprite_mat));
	vec3_t p3 = vec3_add(pos, vec3_transform(vec3(-size.x * 0.5,  size.y * 0.5, screen_2d_z), &sprite_mat));
	vec3_t p4 = vec3_add(pos, vec3_transform(vec3( size.x * 0.5,  size.y * 0.5, screen_2d_z), &sprite_mat));
	uint32_t lcol = (color.a << 24) | (color.r << 16) | (color.g << 8) | (color.b);
	render_texture_t *t = &textures[texture_index];

	vs[0].flags = PVR_CMD_VERTEX;
	vs[0].x = p1.x;
	vs[0].y = p1.y;
	vs[0].z = p1.z;
	vs[0].u = 1;
	vs[0].v = 1;
	vs[0].argb = lcol;
	vs[0].oargb = 0;

	vs[1].flags = PVR_CMD_VERTEX;
	vs[1].x = p2.x;
	vs[1].y = p2.y;
	vs[1].z = p2.z;
	vs[1].u = t->size.x - 1;
	vs[1].v = 1;
	vs[1].argb = lcol;
	vs[1].oargb = 0;

	vs[2].flags = PVR_CMD_VERTEX;//_EOL;
	vs[2].x = p3.x;
	vs[2].y = p3.y;
	vs[2].z = p3.z;
	vs[2].u = 1;
	vs[2].v = t->size.y - 1;
	vs[2].argb = lcol;
	vs[2].oargb = 0;

	vs[3].flags = PVR_CMD_VERTEX_EOL;
	vs[3].x = p4.x;
	vs[3].y = p4.y;
	vs[3].z = p4.z;
	vs[3].u = t->size.x - 1;
	vs[3].v = t->size.y - 1;
	vs[3].argb = lcol;
	vs[3].oargb = 0;

	render_quad(texture_index);

#if 0
	render_tri(texture_index);

	vs[0].flags = PVR_CMD_VERTEX;
	vs[0].x = p3.x;
	vs[0].y = p3.y;
	vs[0].z = p3.z;
	vs[0].u = 1;
	vs[0].v = t->size.y - 1;
	vs[0].argb = lcol;
	vs[0].oargb = 0;

	vs[1].flags = PVR_CMD_VERTEX;
	vs[1].x = p2.x;
	vs[1].y = p2.y;
	vs[1].z = p2.z;
	vs[1].u = t->size.x - 1;
	vs[1].v = 1;
	vs[1].argb = lcol;
	vs[1].oargb = 0;

	vs[2].flags = PVR_CMD_VERTEX_EOL;
	vs[2].x = p4.x;
	vs[2].y = p4.y;
	vs[2].z = p4.z;
	vs[2].u = t->size.x - 1;
	vs[2].v = t->size.y - 1;
	vs[2].argb = lcol;
	vs[2].oargb = 0;

	render_tri(texture_index);
#endif	
}

void render_push_2d(vec2i_t pos, vec2i_t size, rgba_t color, uint16_t texture_index) {
	render_push_2d_tile(pos, vec2i(0, 0), render_texture_size(texture_index), size, color, texture_index);
}



void render_push_2d_tile(vec2i_t pos, vec2i_t uv_offset, vec2i_t uv_size, vec2i_t size, rgba_t color, uint16_t texture_index) {
//	error_if(texture_index >= textures_len, "Invalid texture %d", texture_index);
	screen_2d_z += 0.001f;

	uint32_t lcol = (color.a << 24) | (color.r << 16) | (color.g << 8) | (color.b);

	vs[0].flags = PVR_CMD_VERTEX;
	vs[0].x = pos.x;
	vs[0].y = pos.y;
	vs[0].z = screen_2d_z;
	vs[0].u = uv_offset.x;
	vs[0].v = uv_offset.y;
	vs[0].argb = lcol;
	vs[0].oargb = 0;

	vs[1].flags = PVR_CMD_VERTEX;
	vs[1].x = pos.x + size.x;
	vs[1].y = pos.y;
	vs[1].z = screen_2d_z;
	vs[1].u = uv_offset.x + uv_size.x;
	vs[1].v = uv_offset.y;
	vs[1].argb = lcol;
	vs[1].oargb = 0;

	vs[2].flags = PVR_CMD_VERTEX;//_EOL;
	vs[2].x = pos.x;
	vs[2].y = pos.y + size.y;
	vs[2].z = screen_2d_z;
	vs[2].u = uv_offset.x;
	vs[2].v = uv_offset.y + uv_size.y;
	vs[2].argb = lcol;
	vs[2].oargb = 0;

	vs[3].flags = PVR_CMD_VERTEX_EOL;
	vs[3].x = pos.x + size.x;
	vs[3].y = pos.y + size.y;
	vs[3].z = screen_2d_z;
	vs[3].u = uv_offset.x + uv_size.x;
	vs[3].v = uv_offset.y + uv_size.y;
	vs[3].argb = lcol;
	vs[3].oargb = 0;

	render_quad(texture_index);

#if 0
	render_tri(texture_index);

	vs[0].flags = PVR_CMD_VERTEX;
	vs[0].x = pos.x;
	vs[0].y = pos.y + size.y;
	vs[0].z = screen_2d_z;
	vs[0].u = uv_offset.x;
	vs[0].v = uv_offset.y + uv_size.y;
	vs[0].argb = lcol;
	vs[0].oargb = 0;

	vs[1].flags = PVR_CMD_VERTEX;
	vs[1].x = pos.x + size.x;
	vs[1].y = pos.y;
	vs[1].z = screen_2d_z;
	vs[1].u = uv_offset.x + uv_size.x;
	vs[1].v = uv_offset.y;
	vs[1].argb = lcol;
	vs[1].oargb = 0;

	vs[2].flags = PVR_CMD_VERTEX_EOL;
	vs[2].x = pos.x + size.x;
	vs[2].y = pos.y + size.y;
	vs[2].z = screen_2d_z;
	vs[2].u = uv_offset.x + uv_size.x;
	vs[2].v = uv_offset.y + uv_size.y;
	vs[2].argb = lcol;
	vs[2].oargb = 0;

	render_tri(texture_index);
#endif	
}

uint16_t tmpstore[512*512*2];

uint16_t render_texture_create(uint32_t tw, uint32_t th, uint16_t *pixels) {
	uint16_t texture_index = textures_len;
	if (texture_index != 0) {
		int wp2 = np2(tw);
		int hp2 = np2(th);

		ptrs[texture_index] = pvr_mem_malloc(wp2 * hp2 * 2);
		textures[texture_index] = (render_texture_t){ {wp2, hp2}, {tw, th} };

		for (uint32_t h=0;h<th;h++) {
			for(uint32_t w=0;w<tw;w++) {
				tmpstore[(h*wp2) + w] = pixels[(h*tw) + w];
			}
		}

		pvr_txr_load_ex(tmpstore, ptrs[texture_index], wp2, hp2, PVR_TXRLOAD_16BPP);
	}
	compile_header(texture_index);
	if (ptrs[texture_index] == 0) {
		chdr[texture_index] = &chdr_notex;
	}
	textures_len++;
	return texture_index;
}

vec2i_t render_texture_size(uint16_t texture_index) {
	error_if(texture_index >= textures_len, "Invalid texture %d", texture_index);
	return textures[texture_index].size;
}

void render_texture_replace_pixels(int16_t texture_index, uint16_t *pixels) {
}

uint16_t render_textures_len(void) {
	return textures_len;
}

void render_textures_reset(uint16_t len) {
	//error_if(len > textures_len, "Invalid texture reset len %d >= %d", len, textures_len);
	for (uint16_t curlen = len;curlen<textures_len;curlen++) {
		if (ptrs[curlen]) {
			pvr_mem_free(ptrs[curlen]);
			ptrs[curlen] = 0;
			free(chdr[curlen]);
			chdr[curlen] = NULL;
		}
		last_mode[curlen] = 0;
	}

	textures_len = len;

	// Clear completely and recreate the default white texture
	if (len == 0) {
		uint16_t white_pixels[4] = {0xffff, 0xffff, 0xffff, 0xffff};
		RENDER_NO_TEXTURE = render_texture_create(2, 2, white_pixels);
		return;
	}
}

void render_textures_dump(const char *path) {
}
