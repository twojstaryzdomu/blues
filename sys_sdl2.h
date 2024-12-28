
#include <SDL.h>
#include "sys.h"
#include "sys_sine.h"
#include "util.h"

#define COPPER_BARS_H 80
#define MAX_SPRITES 512
#define MAX_SPRITESHEETS 3

static const int FADE_STEPS = 16;

struct spritesheet_t {
	int count;
	SDL_Rect *r;
	SDL_Surface *surface;
	SDL_Texture *texture;
};

static struct spritesheet_t _spritesheets[MAX_SPRITESHEETS];

struct sprite_t {
	int sheet;
	int num;
	int x, y;
	bool xflip;
	bool centred;
};

static struct sprite_t _sprites[MAX_SPRITES];
static int _sprites_count;
static SDL_Rect _sprites_cliprect;

struct slide_t {
	SDL_Rect *b;
	SDL_Rect *br;
	SDL_Rect *c;
	SDL_Rect *f;
	int *pos;
	int end;
	int last_pos;
	int step;
};

static struct slide_t _slide;

static int _window_w, _window_h;
static int _shake_dx, _shake_dy;
static int _centred_x_offset, _centred_y_offset;
static int _palette_x_offset, _palette_y_offset;
static SDL_Window *_window;
static SDL_Renderer *_renderer;
static SDL_Texture *_texture;
static SDL_PixelFormat *_fmt;
static SDL_Palette *_palette;
static uint32_t _screen_palette[256];
static uint32_t *_screen_buffer;
static int _copper_color_key;
static uint32_t _copper_palette[COPPER_BARS_H];
static const char *_caption;
static int8_t _scale;
static const char *_filter;
static bool _fullscreen;

static int _orig_w, _orig_h;
static int8_t _orig_scale;
static bool _size_lock;
static bool _orig_fullscreen;
static char const *_orig_filter;
static bool _orig_color;
static bool _print_palette;

static SDL_GameController *_controller;
static SDL_Joystick *_joystick;
static int _controller_up;
static bool _controller_up_setup;

static int16_t _sine_index;
static int _sine_direction;
static bool _sine_plot;
static int8_t _sine_offset_y;
static uint16_t _sine_scale_x;
static uint16_t _sine_scale_y, _orig_sine_scale_y;

static int sdl2_init() {
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | (g_sys.audio ? SDL_INIT_AUDIO : 0));
	SDL_ShowCursor(SDL_DISABLE);
	g_sys.w = g_sys.h = _orig_w = _orig_h = 0;
	memset(_screen_palette, 0, sizeof(_screen_palette));
	_palette = SDL_AllocPalette(256);
	_screen_buffer = 0;
	_copper_color_key = -1;
	SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
	_controller = 0;
	g_sys.input.raw = false;
	const int count = SDL_NumJoysticks();
	if (count > 0) {
		for (int i = 0; i < count; ++i) {
			if (SDL_IsGameController(i)) {
				_controller = SDL_GameControllerOpen(i);
				if (_controller) {
					fprintf(stdout, "Using controller '%s'\n", SDL_GameControllerName(_controller));
					switch (*g_sys.input.jump_button) {
					case 'A':
						_controller_up = SDL_CONTROLLER_BUTTON_A;
						break;
					case 'B':
						_controller_up = SDL_CONTROLLER_BUTTON_B;
						break;
					case 'X':
						_controller_up = SDL_CONTROLLER_BUTTON_X;
						break;
					case 'Y':
						_controller_up = SDL_CONTROLLER_BUTTON_Y;
						break;
					default:
						fprintf(stdout, "No such button available: %s\n", g_sys.input.jump_button);
						continue;
					}
					fprintf(stdout, "Jump button: %s\n", g_sys.input.jump_button);
					break;
				}
			}
		}
		if (!_controller) {
			_joystick = SDL_JoystickOpen(0);
			if (_joystick) {
				fprintf(stdout, "Using joystick '%s'\n", SDL_JoystickName(_joystick));
			}
		}
	}
	return 0;
}

static void sdl2_fini() {
	if (_fmt) {
		SDL_FreeFormat(_fmt);
		_fmt = 0;
	}
	if (_palette) {
		SDL_FreePalette(_palette);
		_palette = 0;
	}
	if (_texture) {
		SDL_DestroyTexture(_texture);
		_texture = 0;
	}
	if (_renderer) {
		SDL_DestroyRenderer(_renderer);
		_renderer = 0;
	}
	if (_window) {
		SDL_DestroyWindow(_window);
		_window = 0;
	}
	free(_screen_buffer);
	if (_controller) {
		SDL_GameControllerClose(_controller);
		_controller = 0;
	}
	if (_joystick) {
		SDL_JoystickClose(_joystick);
		_joystick = 0;
	}
	SDL_Quit();
}

static void sdl2_set_screen_size(int w, int h, const char *caption, int scale, const char *filter, bool fullscreen, bool hybrid_color) {
	_window_w = w;
	_window_h = h;
	_caption = caption;
	_filter = filter;
	_fullscreen = fullscreen;
	const int screen_w = MAX(w / scale, ORIG_W);
	const int screen_h = MAX(h / scale, ORIG_H);
	if (screen_w * scale <= _window_w && screen_h * scale <= _window_h) {
		_scale = MAX(scale, 1);
	} else {
		_scale = MAX(_scale - 1, 1);
		print_warning("Unable to fit %dx scaled %dx%d screen within %dx%d window bounds", scale, screen_w, screen_h, _window_w, _window_h, _scale);
		g_message.add("Unable to scale to %d", scale);
		return; // refuse to resize when not possible to fit window within bounds
	}
	if (!_size_lock) {
		g_sys.w = screen_w;
		g_sys.h = screen_h;
	}
	_centred_x_offset = (g_sys.w - ORIG_W) / 2;
	_centred_y_offset = (g_sys.h - ORIG_H) / 2;
	if (!filter || !strcmp(filter, "nearest")) {
		SDL_SetHintWithPriority(SDL_HINT_RENDER_SCALE_QUALITY, "0", SDL_HINT_OVERRIDE);
	} else if (!strcmp(filter, "linear")) {
		SDL_SetHintWithPriority(SDL_HINT_RENDER_SCALE_QUALITY, "1", SDL_HINT_OVERRIDE);
	} else {
		print_warning("Unhandled filter '%s'", filter);
	}
	if (!_orig_w) {
		_orig_w = _window_w;
		_orig_h = _window_h;
		_orig_scale = scale;
		_orig_fullscreen = _fullscreen;
		_orig_filter = _filter;
		_orig_color = hybrid_color;
		fprintf(stderr, "Original window size %dx%d, scale %d, fullscreen %d, filter %s, color %d\n", _orig_w, _orig_h, _orig_scale, _orig_fullscreen, _orig_filter, _orig_color);
	}
	const int flags = fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_RESIZABLE;
	if (!_window) {
		_window = SDL_CreateWindow(caption, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, _window_w, _window_h, flags);
	} else {
		bool is_fullscreen = SDL_GetWindowFlags(_window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
		fprintf(stderr, "%s change to %s\n", is_fullscreen ? "Fullscreen" : "Window", _fullscreen ? "fullscreen" : "window");
		SDL_SetWindowFullscreen(_window, _fullscreen);
		int window_w, window_h;
		SDL_GetWindowSize(_window, &window_w, &window_h);
		fprintf(stderr, "Existing window size: %dx%d\n", window_w, window_h);
		if ((window_w != _window_w || window_h != _window_h) && !is_fullscreen) {
			SDL_SetWindowSize(_window, _window_w, _window_h);
			SDL_GetWindowSize(_window, &window_w, &window_w);
			fprintf(stderr, "Restored window size: %dx%d\n", window_w, window_w);
		}
	}
	if (!_renderer) {
		_renderer = SDL_CreateRenderer(_window, -1, 0);
	}
	SDL_RenderSetLogicalSize(_renderer, g_sys.w, g_sys.h);
	SDL_RenderSetScale(_renderer, _scale, _scale);
	fprintf(stderr, "Window size: %dx%d, game size: %dx%d, scale: %d\n", _window_w, _window_h, g_sys.w, g_sys.h, _scale);
	print_debug(DBG_SYSTEM, "set_screen_size %d,%d", w, h);
	if (_screen_buffer)
		free(_screen_buffer);
	_screen_buffer = (uint32_t *)calloc(g_sys.w * g_sys.h, sizeof(uint32_t));
	if (!_screen_buffer) {
		print_error("Failed to allocate screen buffer");
	}
	if (_texture) {
		SDL_DestroyTexture(_texture);
	}
	static const uint32_t pfmt = SDL_PIXELFORMAT_RGB888;
	_texture = SDL_CreateTexture(_renderer, pfmt, SDL_TEXTUREACCESS_STREAMING, g_sys.w, g_sys.h);
	if (!_fmt)
		_fmt = SDL_AllocFormat(pfmt);
	_sprites_cliprect.x = 0;
	_sprites_cliprect.y = 0;
	_sprites_cliprect.w = g_sys.w;
	_sprites_cliprect.h = g_sys.h;
	_sine_index = 0;
	_sine_direction = 1;
	_sine_offset_y = ORIG_H / 2 - sine_tbl[0];
	_sine_scale_x = 2;
	_sine_scale_y = 10;
	_orig_sine_scale_y = _sine_scale_y;
	g_sys.hybrid_color = hybrid_color;
}

static uint32_t convert_amiga_color(uint16_t color) {
	uint8_t r = (color >> 8) & 15;
	r |= r << 4;
	uint8_t g = (color >> 4) & 15;
	g |= g << 4;
	uint8_t b =  color       & 15;
	b |= b << 4;
	return SDL_MapRGB(_fmt, r, g, b);
}

static void set_amiga_color(uint16_t color, SDL_Color *p) {
	const uint8_t r = (color >> 8) & 15;
	p->r = (r << 4) | r;
	const uint8_t g = (color >> 4) & 15;
	p->g = (g << 4) | g;
	const uint8_t b =  color       & 15;
	p->b = (b << 4) | b;
}

static void sdl2_set_palette_amiga(const uint16_t *colors, int offset) {
	SDL_Color *palette_colors = &_palette->colors[offset];
	for (int i = 0; i < 16; ++i) {
		_screen_palette[offset + i] = convert_amiga_color(colors[i]);
		set_amiga_color(colors[i], &palette_colors[i]);
	}
}

static void sdl2_set_copper_bars(const uint16_t *data) {
	if (!data) {
		_copper_color_key = -1;
	} else {
		_copper_color_key = (data[0] - 0x180) / 2;
		const uint16_t *src = data + 1;
		uint32_t *dst = _copper_palette;
		for (int i = 0; i < COPPER_BARS_H / 5; ++i) {
			const int j = i + 1;
			*dst++ = convert_amiga_color(src[j]);
			*dst++ = convert_amiga_color(src[i]);
			*dst++ = convert_amiga_color(src[j]);
			*dst++ = convert_amiga_color(src[i]);
			*dst++ = convert_amiga_color(src[j]);
		}
	}
}

static void sdl2_set_screen_palette(const uint8_t *colors, int offset, int count, int depth) {
	SDL_Color *palette_colors = &_palette->colors[offset];
	const int shift = 8 - depth;
	for (int i = 0; i < count; ++i) {
		int r = colors[0];
		int g = colors[1];
		int b = colors[2];
		if (depth != 8) {
			r = (r << shift) | (r >> (depth - shift));
			g = (g << shift) | (g >> (depth - shift));
			b = (b << shift) | (b >> (depth - shift));
		}
		_screen_palette[offset + i] = SDL_MapRGB(_fmt, r, g, b);
		palette_colors[i].r = r;
		palette_colors[i].g = g;
		palette_colors[i].b = b;
		colors += 3;
	}
	for (int i = 0; i < ARRAYSIZE(_spritesheets); ++i) {
		struct spritesheet_t *sheet = &_spritesheets[i];
		if (sheet->surface) {
			SDL_DestroyTexture(sheet->texture);
			sheet->texture = SDL_CreateTextureFromSurface(_renderer, sheet->surface);
		}
	}
}

static void sdl2_set_palette_color(int i, const uint8_t *colors) {
	int r = colors[0];
	r = (r << 2) | (r >> 4);
	int g = colors[1];
	g = (g << 2) | (g >> 4);
	int b = colors[2];
	b = (b << 2) | (b >> 4);
	_screen_palette[i] = SDL_MapRGB(_fmt, r, g, b);
}

static void sdl2_update_sprites_screen() {
	if (!g_sys.centred) {
		SDL_RenderSetClipRect(_renderer, !_slide.c? &_sprites_cliprect : _slide.c);
	} else {
		SDL_Rect c = { .x = _centred_x_offset, .y = _centred_y_offset, .w = ORIG_W, .h = ORIG_H };
		SDL_RenderSetClipRect(_renderer, &c);
	}
	for (int i = 0; i < _sprites_count; ++i) {
		const struct sprite_t *spr = &_sprites[i];
		struct spritesheet_t *sheet = &_spritesheets[spr->sheet];
		if (spr->num >= sheet->count) {
			continue;
		}
		if (g_sys.paused)
			if (spr->num < 241 || spr->num > 241 + 32)
				continue;
		SDL_Rect r;
		r.x = spr->x + _shake_dx;
		r.y = spr->y + _shake_dy;
		r.w = sheet->r[spr->num].w;
		r.h = sheet->r[spr->num].h;
		if (_slide.br) {
			r.x += _slide.br->x - _centred_x_offset;
			r.y += _slide.br->y - _centred_y_offset;
		}
		if (spr->centred) {
			r.x += _centred_x_offset;
			r.y += _centred_y_offset;
		}
		if (!spr->xflip) {
			SDL_RenderCopy(_renderer, sheet->texture, &sheet->r[spr->num], &r);
		} else {
			SDL_RenderCopyEx(_renderer, sheet->texture, &sheet->r[spr->num], &r, 0., 0, SDL_FLIP_HORIZONTAL);
		}
	}
	SDL_RenderSetClipRect(_renderer, 0);
}

static void fade_palette_helper(int in) {
	SDL_SetRenderDrawBlendMode(_renderer, SDL_BLENDMODE_BLEND);
	SDL_Rect r;
	r.x = r.y = 0;
	SDL_GetRendererOutputSize(_renderer, &r.w, &r.h);
	for (int i = 0; i <= FADE_STEPS; ++i) {
		int alpha = 255 * i / FADE_STEPS;
		if (in) {
			alpha = 255 - alpha;
		}
		SDL_SetRenderDrawColor(_renderer, 0, 0, 0, alpha);
		SDL_RenderClear(_renderer);
		if (_texture)
			SDL_RenderCopy(_renderer, _texture, 0, 0);
		sdl2_update_sprites_screen();
		if (_slide.f) {
			SDL_RenderCopy(_renderer, _texture, _slide.f, _slide.f);
		}
		SDL_RenderFillRect(_renderer, &r);
		SDL_RenderPresent(_renderer);
		SDL_Delay(30);
	}
	g_sys.render_clear_sprites();
}

static void sdl2_fade_in_palette() {
	if (!g_sys.input.quit) {
		fade_palette_helper(1);
	}
}

static void sdl2_fade_out_palette() {
	if (!g_sys.input.quit) {
		fade_palette_helper(0);
	}
}

static void sdl2_transition_screen(const struct sys_rect_t *s, enum sys_transition_e type, bool open) {
	const int step_w = s->w / (FADE_STEPS + 1);
	const int step_h = s->h / (FADE_STEPS + 1) * s->w / s->h;
	print_debug(DBG_SYSTEM, "sdl2_transition_screen: FADE_STEPS = %d; s->w = %d, s->h = %d, step_w = %d; step_h = %d", FADE_STEPS, s->w, s->h, step_w, step_h);
	SDL_Rect b = { .x = 0, .y = 0, .h = s->h, .w = s->w };
	SDL_Rect r;
	r.x = 0;
	r.w = 0;
	r.y = 0;
	int step = 0;
	r.h = (type == TRANSITION_CURTAIN) ? s->h : 0;
	do {
		if (open) {
			r.w += step_w;
			r.x = (s->w - r.w) / 2;
		} else {
			r.w = s->w;
			r.y += step_h / 2 + step_h % 2;
			r.h -= MIN(step_h, r.h);
		}
		SDL_RenderFillRect(_renderer, &b);
		if (type == TRANSITION_SQUARE) {
			r.y = (s->h - r.h) / 2;
			if (r.y < 0) {
				r.y = 0;
			}
			r.h += step_h;
			if (r.y + r.h > s->h) {
				r.h = s->h - r.y;
			}
		}
		step += 1;
		print_debug(DBG_SYSTEM, "sdl2_transition_screen: %d,%d-%d,%d, r.w = %d, r.h = %d, open = %d, step = %d", r.x, r.y, r.x + r.w, r.y + r.h, r.w, r.h, open, step);
		SDL_RenderCopy(_renderer, _texture, &r, &r);
		SDL_RenderPresent(_renderer);
		SDL_Delay(30);
	} while (((r.x > r.x % step_w && open) || (r.y < s->h / 2 && !open)) && (type == TRANSITION_CURTAIN || r.y > r.y % step_h));
}

static SDL_Rect *set_rect(int x, int y, int w, int h){
	SDL_Rect *r = malloc(sizeof(SDL_Rect));
	r->x = x;
	r->y = y;
	r->w = w;
	r->h = h;
	return r;
}

static void init_slide(){
	if (!_slide.br) {
		enum sys_slide_e type	= g_sys.slide_type;
		struct sys_rect_t *s	= &g_sys.slide_rect;
		SDL_Rect *f		= set_rect(0, s->y ? s->y : g_sys.h - s->h, g_sys.w, s->h);
		SDL_Rect *b		= set_rect(0, 0, ORIG_W, ORIG_H);
		SDL_Rect *br		= set_rect(b->x, b->y, b->w, b->h);
		switch (type) {
		case SLIDE_BOTTOM:
			br->y		= g_sys.h;
			_slide.end	= g_sys.slide_end
					? g_sys.slide_end
					: b->y > _centred_y_offset
					? f->y + f->h
					: _centred_y_offset;
			_slide.pos	= &(br->y);
			*_slide.pos	= MAX(0, *_slide.pos - _slide.last_pos);
			_slide.step	= -1;
			if (!g_sys.centred) {
				br->x	+= _centred_x_offset;
			}
		}
		SDL_Rect *c		= set_rect(_centred_x_offset, 0, ORIG_W, f->y);
		_slide.b		= b;
		_slide.br		= br;
		_slide.c		= c;
		_slide.f		= f;
	}
}

static void clear_slide(){
	g_sys.slide_type = 0;
	free(_slide.b);
	free(_slide.br);
	free(_slide.c);
	free(_slide.f);
	_slide.b = 0;
	_slide.br = 0;
	_slide.c = 0;
	_slide.f = 0;
	_slide.last_pos = 0;
}

static void update_slide() {
	if (g_sys.slide_type) {
		if (*_slide.pos != _slide.end) {
			*_slide.pos += _slide.step;
			++_slide.last_pos;
		}
	}
}

static void reinit_slide() {
	if (g_sys.slide_type) {
		uint8_t slide_type = g_sys.slide_type;
		uint8_t last_pos = _slide.last_pos;
		clear_slide();
		g_sys.slide_type = slide_type;
		_slide.last_pos = last_pos;
		init_slide();
	}
}

static void sdl2_resize_screen() {
	sdl2_set_screen_size(_window_w, _window_h, _caption, _scale, _filter, _fullscreen, g_sys.hybrid_color);
	reinit_slide();
}

static void sdl2_rescale_screen(int n) {
	if (_scale > 1 || n > 0) {
		fprintf(stderr, "Scale %d, ", _scale);
		_scale += n;
		fprintf(stderr, "%screasing to %d\n", n > 0 ? "in" : "de", abs(_scale));
		g_sys.resize = true;
		g_sys.resize_screen();
	}
}

static void sdl2_rehint_screen(const char *f) {
	_filter = f;
	g_sys.rehint = true;
	g_sys.resize_screen();
	fprintf(stderr, "Set hint quality %s\n", _filter);
	g_message.clear(strcmp(_filter, "linear") ? "linear": "nearest");
	g_message.add(_filter);
}

static void sdl2_print_palette() {
	if (!_print_palette)
		return;
	int x_scale = 2;
	int y_scale = 2;
	int x_ncolors = 16;
	int y_ncolors = 16;
	for (int i = 0; i < g_sys.w * g_sys.h; ++i) {
		/*
		 *  Breakdown:
		 *  p = pitch = g_sys.w
		 *  i         -> (x,y) = index ; i+        -> (x,y) = index++
		 *  0         -> (0,0) =       ; 1         -> (1,0) =
		 *  0 + p * 1 -> (1,0) =       ; 1 + p * 1 -> (1,1) =
		 *  0 + p * 2 -> (2,0) =       ; 1 + p * 2 -> (2,1) =
		 *  |       |                    |       |
		 *  |       \--> i / p           |       \--> i / p
		 *  \----------> i % p           \----------> i % p
		 *
		 */
		int x = i % g_sys.w;
		int y = i / g_sys.w;
		if (x < x_ncolors * x_scale && y < y_ncolors * y_scale) {
			int index = x / x_scale + y / y_scale * x_ncolors;
			_screen_buffer[i + _palette_x_offset + _palette_y_offset * g_sys.w] = _screen_palette[index];
		}
	}
}

static void sdl2_sine_screen() {
	uint16_t sine_x = _sine_index;
	int16_t sine_y = ((sine_tbl[_sine_index]) + _sine_offset_y) * _sine_scale_y / 10 * _sine_scale_x;
	SDL_Rect s1 = { .x = _centred_x_offset + MAX(0, -sine_x), .y =  _centred_y_offset + MAX(0, -sine_y), .w = ORIG_W - abs(sine_x), .h = ORIG_H - abs(sine_y) };
	SDL_Rect d1 = { .x = _centred_x_offset + MAX(0, sine_x), .y = _centred_y_offset + MAX(0, sine_y), .w = s1.w, .h = s1.h };
	SDL_Rect s2 = { .x = sine_x > 0 ? s1.x + s1.w : _centred_x_offset, .y = sine_y > 0 ? s1.y + s1.h : _centred_y_offset, .w = ORIG_W - s1.w, .h = ORIG_H - s1.h };
	SDL_Rect d2 = { .x = _centred_x_offset, .y = sine_y < 0 ? d1.h + d1.y : _centred_y_offset, .w = sine_x, .h = s2.h };
	SDL_Rect s3 = { .x = s1.x, .y = sine_y > 0 ? s1.y + s1.h : _centred_y_offset, .w = s1.w, .h = ORIG_H - s1.h };
	SDL_Rect d3 = { .x = _centred_x_offset + MAX(0, sine_x), .y = d2.y, .w = d1.w, .h = ORIG_H - d1.h };
	SDL_Rect s4 = { .x = s1.x + s1.w, .y = s1.y, .w = ORIG_W - s1.w, .h = s1.h };
	SDL_Rect d4 = { .x = _centred_x_offset, .y = _centred_y_offset + MAX(0, sine_y), .w = sine_x, .h = d1.h };
	print_debug(DBG_SYSTEM, "Sine wave: %d, %d @ %d "
				"s1: %d,%d - %d,%d "
				"d1: %d,%d - %d,%d "
				"s1.h = %d; s1.y = %d; d1.h = %d; d1.y = %d; dir = %d",
				sine_x, sine_y, _sine_index,
				s1.x, s1.y, s1.w + s1.x, s1.h + s1.y,
				d1.x, d1.y, d1.w + d1.x, d1.h + d1.y,
				s1.h, s1.y, d1.h, d1.y, _sine_direction);
	SDL_RenderCopy(_renderer, _texture, &s1, &d1);
	SDL_RenderCopy(_renderer, _texture, &s2, &d2);
	SDL_RenderCopy(_renderer, _texture, &s3, &d3);
	SDL_RenderCopy(_renderer, _texture, &s4, &d4);
	if (_sine_plot) {
		SDL_SetRenderDrawColor(_renderer, 255, 0, 0, 255);
		uint16_t x;
		int16_t sine_plot_y;
		for (x = 0; x <= _sine_index; x++) {
			sine_plot_y = ((sine_tbl[x]) + _sine_offset_y) * _sine_scale_y / 10 * _sine_scale_x;
			SDL_RenderDrawPoint(_renderer, x + _centred_x_offset, sine_plot_y + _centred_y_offset);
		}
		print_debug(DBG_SYSTEM, "Sine plot: %d, %d _sine_scale_x %d _sine_scale_y %d _sine_offset_y %d sine_tbl(x) %d",
					x, sine_plot_y, _sine_scale_x, _sine_scale_y / 10, _sine_offset_y, sine_tbl[x]);
	}
	SDL_SetRenderDrawColor(_renderer, 0, 0, 0, 0);
	_sine_index = (_sine_index + _sine_direction + ORIG_W) % ORIG_W;
}

static void sdl2_update_screen_cached(const uint8_t *p, int present, bool cache_redraw) {
	if (!cache_redraw) {
		if (_copper_color_key != -1) {
			for (int j = 0; j < g_sys.h; ++j) {
				if (j / 2 < COPPER_BARS_H) {
					const uint32_t line_color = _copper_palette[j / 2];
					for (int i = 0; i < g_sys.w; ++i) {
						_screen_buffer[j * g_sys.w + i] = (p[i] == _copper_color_key) ? line_color : _screen_palette[p[i]];
					}
				} else {
					for (int i = 0; i < g_sys.w; ++i) {
						_screen_buffer[j * g_sys.w + i] = _screen_palette[p[i]];
					}
				}
				p += g_sys.w;
			}
		} else {
			for (int i = 0; i < g_sys.w * g_sys.h; ++i) {
				_screen_buffer[i] = _screen_palette[p[i]];
			}
			sdl2_print_palette();
		}
		SDL_UpdateTexture(_texture, 0, _screen_buffer, g_sys.w * sizeof(uint32_t));
	}
	if (present) {
		SDL_Rect r, *src, *dst;
		r.x = _shake_dx;
		r.y = _shake_dy;
		r.w = g_sys.w;
		r.h = g_sys.h;
		SDL_RenderClear(_renderer);
		if (g_sys.slide_type) {
			init_slide();
			src = _slide.b;
			dst = _slide.br;
			SDL_RenderSetClipRect(_renderer, _slide.c);
		} else {
			src = 0;
			dst = &r;
		}
		if (g_sys.sine) {
			sdl2_sine_screen();
		} else
			SDL_RenderCopy(_renderer, _texture, src, dst);
		sdl2_update_sprites_screen();
		if (_slide.f)
			SDL_RenderCopy(_renderer, _texture, _slide.f, _slide.f);
		SDL_RenderPresent(_renderer);
		if (_slide.br)
			update_slide();
	}
}

static void sdl2_update_screen(const uint8_t *p, int present) {
	sdl2_update_screen_cached(p, present, false);
}

static void sdl2_shake_screen(int dx, int dy) {
	_shake_dx = dx;
	_shake_dy = dy;
}

static void sdl2_toggle_animate_tiles() {
	g_sys.animate_tiles = !g_sys.animate_tiles;
	g_message.clear("Animated tiles %s", g_sys.animate_tiles ? "off" : "on");
	g_message.add("Animated tiles %s", g_sys.animate_tiles ? "on" : "off");
}

static void handle_keyevent(const SDL_Keysym *keysym, bool keydown, struct input_t *input, bool *paused) {
	uint8_t debug_channel;
	uint16_t debug_level;
	bool debug_enabled;
	if (g_sys.input.raw && keysym->sym >= '0' && keysym->sym < 'g') {
		if (keydown) {
			g_sys.input.hex = keysym->sym;
			return;
		}
	}
	switch (keysym->sym) {
	case SDLK_LEFT:
		if (keydown) {
			switch(keysym->mod) {
			case KMOD_LALT:
				--_palette_x_offset;
				g_sys.redraw_cache = true;
				break;
			default:
				input->direction |= INPUT_DIRECTION_LEFT;
			}
		} else {
			input->direction &= ~INPUT_DIRECTION_LEFT;
		}
		break;
	case SDLK_RIGHT:
		if (keydown) {
			switch(keysym->mod) {
			case KMOD_LALT:
				++_palette_x_offset;
				g_sys.redraw_cache = true;
				break;
			default:
				input->direction |= INPUT_DIRECTION_RIGHT;
			}
		} else {
			input->direction &= ~INPUT_DIRECTION_RIGHT;
		}
		break;
	case SDLK_UP:
		if (keydown) {
			switch(keysym->mod) {
			case KMOD_LALT:
				--_palette_y_offset;
				g_sys.redraw_cache = true;
				break;
			default:
				input->direction |= INPUT_DIRECTION_UP;
			}
		} else {
			input->direction &= ~INPUT_DIRECTION_UP;
		}
		break;
	case SDLK_DOWN:
		if (keydown) {
			switch(keysym->mod) {
			case KMOD_LALT:
				++_palette_y_offset;
				g_sys.redraw_cache = true;
				break;
			default:
				input->direction |= INPUT_DIRECTION_DOWN;
			}
		} else {
			input->direction &= ~INPUT_DIRECTION_DOWN;
		}
		break;
	case SDLK_RETURN:
		if (keydown) {
			switch(keysym->mod) {
			case KMOD_LALT:
				_fullscreen = !_fullscreen;
				fprintf(stderr, "Toggling fullscreen to %s\n", _fullscreen ? "on" : "off");
				g_sys.resize = true;
				g_sys.resize_screen();
				SDL_ShowCursor(_fullscreen);
				print_debug(DBG_SYSTEM, "Fullscreen is %s", _fullscreen ? "on" : "off");
			}
		}
		break;
	case SDLK_SPACE:
		input->space = keydown;
		break;
	case SDLK_ESCAPE:
		if (keydown) {
			g_sys.input.quit = true;
		}
		break;
	case SDLK_F1:
	case SDLK_F2:
	case SDLK_F3:
	case SDLK_F4:
	case SDLK_F5:
	case SDLK_F6:
	case SDLK_F7:
	case SDLK_F8:
	case SDLK_F9:
	case SDLK_F10:
	case SDLK_F11:
	case SDLK_F12:
		if (keydown) {
			debug_channel = keysym->sym - SDLK_F1;
			debug_level = 1 << debug_channel;
			debug_enabled = g_debug_mask & debug_level;
			if (debug_enabled)
				g_debug_mask &= ~debug_level;
			else
				g_debug_mask |= debug_level;
			g_message.clear("%sabled debug %lu", debug_enabled  ? "En" : "Dis", debug_level);
			g_message.add("%sabled debug %lu", debug_enabled ? "Dis" : "En", debug_level);
		}
		break;
	case SDLK_KP_2:
		if (keydown)
			_sine_offset_y++;
		break;
	case SDLK_KP_4:
		if (keydown) {
			_sine_scale_y += _orig_sine_scale_y / 10;
			_sine_offset_y -= 2 * _orig_sine_scale_y / 10;
		}
		break;
	case SDLK_KP_6:
		if (keydown) {
			_sine_scale_y -= _orig_sine_scale_y / 10;
			_sine_offset_y += 2 * _orig_sine_scale_y / 10;
		}
		break;
	case SDLK_KP_8:
		if (keydown)
			_sine_offset_y--;
		break;
	case SDLK_MINUS:
		if (keydown) {
			g_sys.palette_offset = -1;
			g_sys.cycle_palette = true;
		}
		break;
	case SDLK_EQUALS:
	case SDLK_PLUS:
		if (keydown) {
			g_sys.palette_offset = 1;
			g_sys.cycle_palette = true;
		}
		break;
	case SDLK_1:
		input->digit1 = keydown;
		break;
	case SDLK_2:
		input->digit2 = keydown;
		break;
	case SDLK_3:
		input->digit3 = keydown;
		break;
	case SDLK_a:
		if (keydown) {
			g_sys.audio = !g_sys.audio;
			SDL_PauseAudio(!g_sys.audio);
			g_message.clear("Sound %s", g_sys.audio ? "off" : "on");
			g_message.add("Sound %s", g_sys.audio ? "on" : "off");
		}
		break;
	case SDLK_c:
		if (keydown) {
			g_sys.reset_cache_counters = true;
		}
		break;
	case SDLK_d:
		if (keydown)
			sdl2_rescale_screen(-1);
		break;
	case SDLK_e:
		if (keydown) {
			g_sys.redraw_cache = true;
			_print_palette = !_print_palette;
		}
		break;
	case SDLK_g:
		if (keydown) {
			sdl2_toggle_animate_tiles();
		}
		break;
	case SDLK_h:
		if (keydown) {
			g_sys.hybrid_color = !g_sys.hybrid_color;
			g_sys.cycle_palette = true;
			g_message.clear("Hybrid colour %s", g_sys.hybrid_color ? "off" : "on");
			g_message.add("Hybrid colour %s", g_sys.hybrid_color ? "on" : "off");
		}
		break;
	case SDLK_i:
		if (keydown)
			sdl2_rescale_screen(1);
		break;
	case SDLK_j:
		if (keydown) {
			g_message.add("Press jump button");
			_controller_up_setup = true;
		}
		break;
	case SDLK_k:
		if (keydown)
			_sine_plot = !_sine_plot;
		break;
	case SDLK_l:
		if (keydown)
			sdl2_rehint_screen("linear");
		break;
	case SDLK_n:
		if (keydown)
			sdl2_rehint_screen("nearest");
		break;
	case SDLK_o:
		if (keydown) {
			fprintf(stderr, "Restoring original window size %dx%d, scale %d, fullscreen %d\n", _orig_w, _orig_h, _orig_scale, _orig_fullscreen);
			SDL_RestoreWindow(_window);
			g_sys.resize = true;
			g_sys.rehint = true;
			_size_lock = false;
			sdl2_set_screen_size(_orig_w, _orig_h, _caption, _orig_scale, _orig_filter, _orig_fullscreen, _orig_color);
			reinit_slide();
		}
		break;
	case SDLK_p:
		if (keydown) {
			*paused = (bool)(*paused ? false : true);
			if (g_sys.audio)
				SDL_PauseAudio(*paused);
		}
		break;
	case SDLK_q:
		if (keydown)
			_sine_direction = -_sine_direction;
		break;
	case SDLK_s:
		if (keydown) {
			_size_lock = !_size_lock;
			g_message.clear("Size %s", _size_lock ? "unlocked" : "locked");
			g_message.add("Size %s", _size_lock ? "locked" : "unlocked");
		}
		break;
	case SDLK_t:
		if (keydown) {
			_scale = (_scale == _orig_scale ? 1 : _orig_scale);
			fprintf(stderr, "Toggling scale to %d\n", _scale);
			SDL_GetWindowSize(_window, &g_sys.w, &g_sys.h);
			g_sys.resize = true;
			g_sys.resize_screen();
		}
	}
}

static void handle_controlleraxis(int axis, int value, struct input_t *input) {
	static const int THRESHOLD = 3200;
	switch (axis) {
	case SDL_CONTROLLER_AXIS_LEFTX:
	case SDL_CONTROLLER_AXIS_RIGHTX:
		if (value < -THRESHOLD) {
			input->direction |= INPUT_DIRECTION_LEFT;
		} else {
			input->direction &= ~INPUT_DIRECTION_LEFT;
		}
		if (value > THRESHOLD) {
			input->direction |= INPUT_DIRECTION_RIGHT;
		} else {
			input->direction &= ~INPUT_DIRECTION_RIGHT;
		}
		break;
	case SDL_CONTROLLER_AXIS_LEFTY:
	case SDL_CONTROLLER_AXIS_RIGHTY:
		if (value < -THRESHOLD) {
			input->direction |= INPUT_DIRECTION_UP;
		} else {
			input->direction &= ~INPUT_DIRECTION_UP;
		}
		if (value > THRESHOLD) {
			input->direction |= INPUT_DIRECTION_DOWN;
		} else {
			input->direction &= ~INPUT_DIRECTION_DOWN;
		}
		break;
	}
}

static void handle_controllerbutton(int button, bool pressed, struct input_t *input) {
	if (_controller_up_setup) {
		if (pressed) {
			_controller_up = button;
			_controller_up_setup = false;
			g_message.add("Jump on %c key", _controller_up + 65);
			return;
		}
	}
	if (button == _controller_up) {
		if (pressed) {
			input->direction |= INPUT_DIRECTION_UP;
		} else {
			input->direction &= ~INPUT_DIRECTION_UP;
		}
		return;
	}
	switch (button) {
	case SDL_CONTROLLER_BUTTON_A:
	case SDL_CONTROLLER_BUTTON_B:
	case SDL_CONTROLLER_BUTTON_X:
	case SDL_CONTROLLER_BUTTON_Y:
		input->space = pressed;
		break;
	case SDL_CONTROLLER_BUTTON_BACK:
		g_sys.input.quit = true;
		break;
	case SDL_CONTROLLER_BUTTON_START:
		if (pressed) {
			g_sys.paused = (bool)(g_sys.paused ? false : true);
			if (g_sys.audio)
				SDL_PauseAudio(g_sys.paused);
		}
		break;
	case SDL_CONTROLLER_BUTTON_DPAD_UP:
		if (pressed) {
			input->direction |= INPUT_DIRECTION_UP;
		} else {
			input->direction &= ~INPUT_DIRECTION_UP;
		}
		break;
	case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
		if (pressed) {
			input->direction |= INPUT_DIRECTION_DOWN;
		} else {
			input->direction &= ~INPUT_DIRECTION_DOWN;
		}
		break;
	case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
		if (pressed) {
			input->direction |= INPUT_DIRECTION_LEFT;
		} else {
			input->direction &= ~INPUT_DIRECTION_LEFT;
		}
		break;
	case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
		if (pressed) {
			input->direction |= INPUT_DIRECTION_RIGHT;
		} else {
			input->direction &= ~INPUT_DIRECTION_RIGHT;
		}
		break;
	case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
		if (pressed) {
			sdl2_rescale_screen(-1);
		}
		break;
	case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
		if (pressed) {
			sdl2_rescale_screen(1);
		}
		break;
	case SDL_CONTROLLER_BUTTON_LEFTSTICK:
		if (pressed) {
			sdl2_toggle_animate_tiles();
		}
		break;
	}
}

static void handle_joystickhatmotion(int value, struct input_t *input) {
	input->direction = 0;
	if (value & SDL_HAT_UP) {
		input->direction |= INPUT_DIRECTION_UP;
	}
	if (value & SDL_HAT_DOWN) {
		input->direction |= INPUT_DIRECTION_DOWN;
	}
	if (value & SDL_HAT_LEFT) {
		input->direction |= INPUT_DIRECTION_LEFT;
	}
	if (value & SDL_HAT_RIGHT) {
		input->direction |= INPUT_DIRECTION_RIGHT;
	}
}

static void handle_joystickaxismotion(int axis, int value, struct input_t *input) {
	static const int THRESHOLD = 3200;
	switch (axis) {
	case 0:
		input->direction &= ~(INPUT_DIRECTION_RIGHT | INPUT_DIRECTION_LEFT);
		if (value > THRESHOLD) {
			input->direction |= INPUT_DIRECTION_RIGHT;
		} else if (value < -THRESHOLD) {
			input->direction |= INPUT_DIRECTION_LEFT;
		}
		break;
	case 1:
		input->direction &= ~(INPUT_DIRECTION_UP | INPUT_DIRECTION_DOWN);
		if (value > THRESHOLD) {
			input->direction |= INPUT_DIRECTION_DOWN;
		} else if (value < -THRESHOLD) {
			input->direction |= INPUT_DIRECTION_UP;
		}
		break;
	}
}

static void handle_joystickbutton(int button, int pressed, struct input_t *input) {
	if (button == 0) {
		input->space = pressed;
	}
}

static int handle_event(const SDL_Event *ev) {
	int window_w, window_h;
	switch (ev->type) {
	case SDL_QUIT:
		g_sys.input.quit = true;
		break;
	case SDL_WINDOWEVENT:
		switch (ev->window.event) {
		case SDL_WINDOWEVENT_FOCUS_GAINED:
		case SDL_WINDOWEVENT_FOCUS_LOST:
			g_sys.paused = (ev->window.event == SDL_WINDOWEVENT_FOCUS_LOST);
			if (g_sys.audio)
				SDL_PauseAudio(g_sys.paused);
			break;
		case SDL_WINDOWEVENT_MINIMIZED:
			fprintf(stderr, "Window minimized");
		case SDL_WINDOWEVENT_SIZE_CHANGED:
			if (ev->window.data1 && ev->window.data2) {
				fprintf(stderr, "Size changed from %dx%d to %dx%d, scale %d\n", _window_w, _window_h, ev->window.data1, ev->window.data2, _scale);
				if (_window_w != ev->window.data1 || _window_h != ev->window.data2) {
					if (!_size_lock) {
						_window_w = ev->window.data1;
						_window_h = ev->window.data2;
						g_sys.resize = true;
						g_sys.resize_screen();
					} else {
						fprintf(stderr, "Scaling is locked\n");
					}
				} else {
					fprintf(stderr, "No change needed\n");
				}
			} else {
				fprintf(stderr, " and bad %dx%d size ignored\n", ev->window.data1, ev->window.data2);
			}
			break;
		case SDL_WINDOWEVENT_RESIZED:
			SDL_GetWindowSize(_window, &window_w, &window_h);
			fprintf(stderr, "Resize completed to %dx%d, scale %d\n", window_w, window_h, _scale);
			break;
		}
		break;
	case SDL_KEYUP:
		handle_keyevent(&ev->key.keysym, 0, &g_sys.input, &g_sys.paused);
		break;
	case SDL_KEYDOWN:
		handle_keyevent(&ev->key.keysym, 1, &g_sys.input, &g_sys.paused);
		break;
	case SDL_CONTROLLERDEVICEADDED:
		if (!_controller) {
			_controller = SDL_GameControllerOpen(ev->cdevice.which);
			if (_controller) {
				fprintf(stdout, "Using controller '%s'\n", SDL_GameControllerName(_controller));
			}
		}
		break;
	case SDL_CONTROLLERDEVICEREMOVED:
		if (_controller == SDL_GameControllerFromInstanceID(ev->cdevice.which)) {
			fprintf(stdout, "Removed controller '%s'\n", SDL_GameControllerName(_controller));
			SDL_GameControllerClose(_controller);
			_controller = 0;
		}
		break;
	case SDL_CONTROLLERBUTTONUP:
		if (_controller) {
			handle_controllerbutton(ev->cbutton.button, 0, &g_sys.input);
		}
		break;
	case SDL_CONTROLLERBUTTONDOWN:
		if (_controller) {
			handle_controllerbutton(ev->cbutton.button, 1, &g_sys.input);
		}
		break;
	case SDL_CONTROLLERAXISMOTION:
		if (_controller) {
			handle_controlleraxis(ev->caxis.axis, ev->caxis.value, &g_sys.input);
		}
		break;
	case SDL_JOYHATMOTION:
		if (_joystick) {
			handle_joystickhatmotion(ev->jhat.value, &g_sys.input);
		}
		break;
	case SDL_JOYAXISMOTION:
		if (_joystick) {
			handle_joystickaxismotion(ev->jaxis.axis, ev->jaxis.value, &g_sys.input);
		}
		break;
	case SDL_JOYBUTTONUP:
		if (_joystick) {
			handle_joystickbutton(ev->jbutton.button, 0, &g_sys.input);
		}
		break;
	case SDL_JOYBUTTONDOWN:
		if (_joystick) {
			handle_joystickbutton(ev->jbutton.button, 1, &g_sys.input);
		}
		break;
	default:
		return -1;
	}
	return 0;
}

static void sdl2_process_events() {
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		handle_event(&ev);
		if (g_sys.input.quit) {
			break;
		}
	}
}

static void sdl2_sleep(int duration) {
	SDL_Delay(duration);
}

static uint32_t sdl2_get_timestamp() {
	return SDL_GetTicks();
}

static void sdl2_start_audio(sys_audio_cb callback, void *param) {
	if (!g_sys.audio)
		return;
	SDL_AudioSpec desired;
	memset(&desired, 0, sizeof(desired));
	desired.freq = SYS_AUDIO_FREQ;
	desired.format = AUDIO_S16;
	desired.channels = 1;
	desired.samples = 2048;
	desired.callback = callback;
	desired.userdata = param;
	if (SDL_OpenAudio(&desired, 0) == 0) {
		SDL_PauseAudio(0);
	}
}

static void sdl2_stop_audio() {
	if (g_sys.audio)
		SDL_CloseAudio();
}

static void sdl2_lock_audio() {
	if (g_sys.audio)
		SDL_LockAudio();
}

static void sdl2_unlock_audio() {
	if (g_sys.audio)
		SDL_UnlockAudio();
}

static void render_load_sprites(int spr_type, int count, const struct sys_rect_t *r, const uint8_t *data, int w, int h, uint8_t color_key, bool update_pal) {
	assert(spr_type < ARRAYSIZE(_spritesheets));
	struct spritesheet_t *sheet = &_spritesheets[spr_type];
	sheet->count = count;
	sheet->r = (SDL_Rect *)malloc(count * sizeof(SDL_Rect));
	for (int i = 0; i < count; ++i) {
		SDL_Rect *rect = &sheet->r[i];
		rect->x = r[i].x;
		rect->y = r[i].y;
		rect->w = r[i].w;
		rect->h = r[i].h;
	}
	SDL_Surface *surface = SDL_CreateRGBSurface(0, w, h, 8, 0x0, 0x0, 0x0, 0x0);
	SDL_SetSurfacePalette(surface, _palette);
	SDL_SetColorKey(surface, 1, color_key);
	SDL_LockSurface(surface);
	for (int y = 0; y < h; ++y) {
		memcpy(((uint8_t *)surface->pixels) + y * surface->pitch, data + y * w, w);
	}
	SDL_UnlockSurface(surface);
	sheet->texture = SDL_CreateTextureFromSurface(_renderer, surface);
	if (update_pal) { /* update texture on palette change */
		sheet->surface = surface;
	} else {
		SDL_FreeSurface(surface);
	}
}

static void render_unload_sprites(int spr_type) {
	struct spritesheet_t *sheet = &_spritesheets[spr_type];
	free(sheet->r);
	if (sheet->surface) {
		SDL_FreeSurface(sheet->surface);
	}
	if (sheet->texture) {
		SDL_DestroyTexture(sheet->texture);
	}
	memset(sheet, 0, sizeof(struct spritesheet_t));
}

static void render_add_sprite(int spr_type, int frame, int x, int y, int xflip, bool centred) {
	assert(_sprites_count < MAX_SPRITES);
	struct sprite_t *spr = &_sprites[_sprites_count];
	spr->sheet = spr_type;
	spr->num = frame;
	spr->x = x;
	spr->y = y;
	spr->xflip = xflip;
	spr->centred = centred;
	++_sprites_count;
}

static void render_clear_sprites() {
	_sprites_count = 0;
}

static void render_set_sprites_clipping_rect(int x, int y, int w, int h) {
	_sprites_cliprect.x = x;
	_sprites_cliprect.y = y;
	_sprites_cliprect.w = w;
	_sprites_cliprect.h = h;
}
