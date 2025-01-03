
/* screen drawing */

#include "game.h"
#include "resource.h"
#include "sys.h"
#include "util.h"
#include <stdarg.h>

#define MAX_SPRITES 480
#define MAX_SPRITESHEET_W 2048
#define MAX_SPRITESHEET_H 1024
#define MAX_FRONT_TILES 168

static void decode_planar(const uint8_t *src, uint8_t *dst, int dst_pitch, int w, int h, uint8_t transparent_color) {
	const int plane_size = h * w / 8;
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w / 8; ++x) {
			for (int i = 0; i < 8; ++i) {
				const uint8_t mask = 1 << (7 - i);
				uint8_t color = 0;
				for (int b = 0; b < 4; ++b) {
					if (src[b * plane_size] & mask) {
						color |= (1 << b);
					}
				}
				if (color != transparent_color) {
					dst[x * 8 + i] = color;
				}
			}
			++src;
		}
		dst += dst_pitch;
	}
}

void video_draw_fonts() {
	decode_planar(g_res.allfonts + GAME_SCREEN_W + GAME_SCREEN_H, g_res.vga, GAME_SCREEN_W, GAME_SCREEN_W, GAME_SCREEN_H, 0);
}

static void convert_planar_tile_4bpp(const uint8_t *src, uint8_t *dst, int dst_pitch) {
	static const int tile_h = 16;
	static const int tile_w = 16;
	static const int plane_size = 16 * (16 / 8);
	for (int y = 0; y < tile_h; ++y) {
		for (int x = 0; x < tile_w / 8; ++x) {
			for (int i = 0; i < 8; ++i) {
				const uint8_t mask = 1 << (7 - i);
				uint8_t color = 0;
				for (int b = 0; b < 4; ++b) {
					if (src[b * plane_size] & mask) {
						color |= (1 << b);
					}
				}
				if (i & 1) {
					dst[x * 4 + (i >> 1)] |= color;
				} else {
					dst[x * 4 + (i >> 1)] = color << 4;
				}
			}
			++src;
		}
		dst += dst_pitch;
	}
}

void video_draw_string(int offset, int hspace, const char *s) {
	offset += hspace;
	const int y = (offset * 8) / ORIG_W + (GAME_SCREEN_H - ORIG_H) / 2;
	const int x = (offset * 8) % ORIG_W + (GAME_SCREEN_W - ORIG_W) / 2;
	uint8_t *dst = g_res.vga + y * GAME_SCREEN_W + x;
	while (*s) {
		uint8_t code = *s++;
		if (code != 0x20) {
			code -= 0x30;
			if (code > 9) {
				code -= 2;
			}
			decode_planar(g_res.allfonts + code * STRING_W * STRING_H / 2, dst, GAME_SCREEN_W, STRING_W, STRING_H, 0);
		}
		dst += STRING_W;
	}
}

void video_draw_panel_number(int offset, int num) {
	const uint8_t *fnt = g_res.allfonts + 48 * 41 + 160 * 23;
	const int y = (offset * 8) / ORIG_W + (GAME_SCREEN_H - ORIG_H);
	const int x = (offset * 8) % ORIG_W + (GAME_SCREEN_W - ORIG_W) / 2;
	decode_planar(fnt + num * PANEL_NUMBER_W * PANEL_NUMBER_H / 2, g_res.vga + y * GAME_SCREEN_W + x, GAME_SCREEN_W, PANEL_NUMBER_W, PANEL_NUMBER_H, 0);
}

void video_draw_number(int offset, int num) {
	const uint8_t *fnt = g_res.allfonts + DIGITS_OFFSET;
	const int y = (offset * 8) / ORIG_W;
	const int x = (offset * 8) % ORIG_W;
	decode_planar(fnt + num * NUMBER_W * NUMBER_H / 2, g_res.vga + y * GAME_SCREEN_W + x, GAME_SCREEN_W, NUMBER_W, NUMBER_H, 0);
}

void video_draw_character_spr(int offset, uint8_t chr) {
	const int y = (offset * 8) / ORIG_W;
	const int x = (offset * 8) % ORIG_W;
	video_draw_sprite(CHARACTER_OFFSET + chr, x, y, 0, false);
}

static void video_draw_string2(const char *s, bool clip, int y_offset, int wspace, bool bold, bool chr_spacing) {
	const uint8_t uppercase_offset = 65;
	const uint8_t lowercase_offset = 32;
	const uint8_t number_offset = 6;
	const uint8_t digits_ascii = '0' + 0xF;
	const uint8_t l = strlen(s);
	const int y = (TILEMAP_SCREEN_H - STRING_SPR_H) / 2 + y_offset;
	uint8_t nspaces = 0;
	for (int i = 0; s[i]; i++)
		if (s[i] == ' ')
			nspaces++;
	if (!wspace)
		wspace = 1;
	uint16_t string_screen_w = STRING_SPR_W * (l + chr_spacing * (l - nspaces - 1) / wspace);
	int x = (TILEMAP_SCREEN_W - string_screen_w) / 2;
	if (bold)
		x--;
	if (clip)
		g_sys.render_set_sprites_clipping_rect(x, y, string_screen_w, STRING_SPR_H);
	while (*s) {
		const uint8_t chr = *s++;
		if (chr != ' ') {
			uint8_t offset = chr > digits_ascii ? uppercase_offset : number_offset;
			offset += chr > lowercase_offset + uppercase_offset - 1 ? lowercase_offset : 0;
			if (bold)
				video_draw_sprite(CHARACTER_OFFSET + chr - offset, x + 1, y + 1, 0, false);
			video_draw_sprite(CHARACTER_OFFSET + chr - offset, x, y, 0, false);
			x += chr_spacing * STRING_SPR_W / wspace;
		}
		x += STRING_SPR_W;
	}
}

void video_draw_motif_string(const char *s, bool clip, int y_offset, int wspace) {
	video_draw_string2(s, clip, y_offset, wspace, true, wspace);
}

void video_draw_string_clipped(const char *s, int x, int y, bool clip) {
	const uint8_t l = strlen(s);
	const uint8_t uppercase_offset = 65;
	const uint8_t lowercase_offset = 32;
	const uint8_t number_offset = 6;
	const uint8_t digits_ascii = '0' + 9;
	if (clip) {
		g_sys.render_set_sprites_clipping_rect(x, y, STRING_SPR_W * l, STRING_SPR_H);
	}
	while (*s) {
		const uint8_t chr = *s++;
		if (chr != ' ') {
			uint8_t offset = chr > digits_ascii ? uppercase_offset : number_offset;
			offset += chr > lowercase_offset + uppercase_offset - 1 ? lowercase_offset : 0;
			video_draw_sprite(CHARACTER_OFFSET + chr - offset, x, y, 0, false);
		}
		x += STRING_SPR_W;
	}
}

void video_draw_format_string(int offset, const char *format, ...) {
	char *s;
	va_list args;
	va_start(args, format);
	vasprintf(&s, format, args);
	va_end(args);
	const int y = (offset * 8) / ORIG_W;
	const int x = (offset * 8) % ORIG_W;
	video_draw_string_clipped(s, x, y, 0);
	free(s);
}

void video_draw_string_centred(const char *s, bool clip) {
	const uint8_t l = strlen(s);
	int x = (TILEMAP_SCREEN_W - STRING_SPR_W * l) / 2;
	const int y = (TILEMAP_SCREEN_H - STRING_SPR_H) / 2;
	video_draw_string_clipped(s, x, y, clip);
}

void video_resize() {
	if (g_sys.resize) {
		free(g_res.vga);
		g_res.vga = (uint8_t *)calloc(GAME_SCREEN_W * GAME_SCREEN_H, 1);
		if (!g_res.vga) {
			print_error("Failed to reallocate vga buffer, %d bytes", GAME_SCREEN_W * GAME_SCREEN_H);
		}
		g_sys.resize = false;
	}
	if (g_sys.rehint)
		video_load_sprites();
}

void video_clear() {
	if (g_sys.resize) {
		video_resize();
	} else {
		memset(g_res.vga, 0, GAME_SCREEN_W * GAME_SCREEN_H);
	}
}

void video_copy_img(const uint8_t *src) {
	decode_planar(src, g_res.background, ORIG_W, ORIG_W, ORIG_H, 0xFF);
}

void video_copy_map(const uint8_t *src) {
	decode_planar(src, g_res.map, MAP_W, MAP_W, MAP_H, 0xFF);
}

void video_copy_offset(uint8_t *src, int w, int h, int x_offs, int y_offs) {
	for (int y = 0; y < h; ++y) {
		memcpy(g_res.vga + (y_offs + y) * GAME_SCREEN_W + x_offs, src + y * w, w);
	}
}

void video_copy(uint8_t *src, int w, int h) {
	video_copy_offset(src, w, h, 0, 0);
}

void video_copy_centred(uint8_t *src, int w, int h) {
	int x_offs = (GAME_SCREEN_W - w) / 2;
	int y_offs = (GAME_SCREEN_H - h) / 2;
	video_copy_offset(src, w, h, x_offs, y_offs);
}

void video_copy_background() {
	if (GAME_SCREEN_W * GAME_SCREEN_H == ORIG_W * ORIG_H) {
		memcpy(g_res.vga, g_res.background, ORIG_W * ORIG_H);
	} else {
		video_clear();
		for (int y = 0; y < MAX(ORIG_H, TILEMAP_SCREEN_H); ++y) {
			for (int x = 0; x < GAME_SCREEN_W; x += ORIG_W) {
				memcpy(g_res.vga + y * GAME_SCREEN_W + x,
					g_res.background + (y < ORIG_H ? y : ORIG_H - 1) * ORIG_W,
					MIN(ORIG_W, GAME_SCREEN_W - x));
			}
		}
	}
}

void video_draw_panel(const uint8_t *src) {
	const int h = TILEMAP_SCREEN_H;
	const int x = (GAME_SCREEN_W - ORIG_W) / 2;
	decode_planar(src, g_res.vga + h * GAME_SCREEN_W + x, GAME_SCREEN_W, ORIG_W, PANEL_H - 1, 0xFF);
}

void video_draw_tile(const uint8_t *src, int x_offset, int y_offset) {
	int tile_w = 16;
	if (x_offset < 0) {
		tile_w += x_offset;
		src -= x_offset / 2;
		x_offset = 0;
	}
	if (x_offset + tile_w > TILEMAP_SCREEN_W) {
		tile_w = TILEMAP_SCREEN_W - x_offset;
	}
	if (tile_w <= 0) {
		return;
	}
	int tile_h = 16;
	if (y_offset < 0) {
		tile_h += y_offset;
		src -= y_offset * 8;
		y_offset = 0;
	}
	if (y_offset + tile_h > TILEMAP_SCREEN_H) {
		tile_h = TILEMAP_SCREEN_H - y_offset;
	}
	if (tile_h <= 0) {
		return;
	}
	uint8_t *dst = g_res.vga + y_offset * TILEMAP_SCREEN_W + x_offset;
	for (int y = 0; y < tile_h; ++y) {
		for (int x = 0; x < tile_w / 2; ++x) {
			const uint8_t color = src[x];
			const uint8_t c1 = color >> 4;
			if (c1 != 0) {
				dst[x * 2] = c1;
			}
			const uint8_t c2 = color & 15;
			if (c2 != 0) {
				dst[x * 2 + 1] = c2;
			}
		}
		src += 8;
		dst += TILEMAP_SCREEN_W;
	}
}

void video_convert_tiles(uint8_t *data, int len) {
	for (int offset = 0; offset < (len & ~127); offset += 128) {
		uint8_t buffer[16 * 8];
		convert_planar_tile_4bpp(data + offset, buffer, 8);
		memcpy(data + offset, buffer, 16 * 8);
	}
}

void video_load_front_tiles() {
	g_sys.render_unload_sprites(RENDER_SPR_FG);
	assert((g_res.frontlen & 127) == 0);
	const int count = g_res.frontlen / (16 * 8);
	assert(count <= MAX_FRONT_TILES);
	struct sys_rect_t r[MAX_FRONT_TILES];
	const int w = 256;
	const int h = 192;
	memset(g_res.vga, 0, w * h);
	int tile = 0;
	for (int y = 0; y < h; y += 16) {
		for (int x = 0; x < w; x += 16) {
			r[tile].x = x;
			r[tile].y = y;
			r[tile].w = 16;
			r[tile].h = 16;
			decode_planar(g_res.frontdat + tile * 16 * 8, g_res.vga + y * w + x, w, 16, 16, 0);
			++tile;
			if (tile == count) {
				g_sys.render_load_sprites(RENDER_SPR_FG, count, r, g_res.vga, w, h, 0x0, true);
				return;
			}
		}
	}
}

void video_transition_close() {
	print_debug(DBG_SYSTEM, "video_transition_close");
	struct sys_rect_t s;
	s.h = TILEMAP_SCREEN_H;
	s.w = TILEMAP_SCREEN_W;
	g_sys.update_screen(g_res.vga, 0);
	g_sys.transition_screen(&s, TRANSITION_CURTAIN, false);
}

void video_transition_open() {
	print_debug(DBG_SYSTEM, "video_transition_open");
	struct sys_rect_t s;
	s.h = g_sys.resize ? GAME_SCREEN_H : TILEMAP_SCREEN_H;
	s.w = TILEMAP_SCREEN_W;
	g_sys.update_screen(g_res.vga, 0);
	g_sys.transition_screen(&s, TRANSITION_CURTAIN, true);
}

void video_load_sprites() {
	struct sys_rect_t r[MAX_SPRITES];
	uint8_t *data = (uint8_t *)calloc(MAX_SPRITESHEET_W * MAX_SPRITESHEET_H, 1);
	if (data) {
		int current_x = 0;
		int max_w = 0;
		int current_y = 0;
		int max_h = 0;

		int offset = 0;
		int count = 0;
		uint8_t value;
		for (int i = 0; count < g_res.spr_monsters_count; ++i) {

			if (g_res.dos_demo && (i >= 305 && i < 312)) {
				/* demo is missing 7 (monster) sprites compared to full game */
				continue;
			}
			const int j = i * 2;

			value = spr_size_tbl[j];
			value = (value >> 3) | ((value & 7) << 5);
			if ((value & 0xE0) != 0) {
				value &= ~0xE0;
				++value;
			}
			const int h = spr_size_tbl[j + 1];
			const int w = value * 8;
			assert(spr_size_tbl[j] == w);
			const int size = (h * value) * 4;

			if (current_x + w > MAX_SPRITESHEET_W) {
				current_y += max_h;
				if (current_x > max_w) {
					max_w = current_x;
				}
				current_x = 0;
				max_h = h;
			} else {
				if (h > max_h) {
					max_h = h;
				}
			}
			decode_planar(g_res.sprites + offset, data + current_y * MAX_SPRITESHEET_W + current_x, MAX_SPRITESHEET_W, w, h, 0xFF);
			offset += size;

			r[i].x = current_x;
			r[i].y = current_y;
			r[i].w = w;
			r[i].h = h;
			current_x += w;

			++count;
		}
		assert(count <= MAX_SPRITES);
		assert(max_w <= MAX_SPRITESHEET_W);
		assert(current_y + max_h <= MAX_SPRITESHEET_H);
		g_sys.render_unload_sprites(RENDER_SPR_GAME);
		g_sys.render_load_sprites(RENDER_SPR_GAME, count, r, data, MAX_SPRITESHEET_W, current_y + max_h, 0x0, true);
		free(data);
		print_debug(DBG_SCREEN, "sprites total_size %d count %d", offset, count);
	}
}

void video_draw_sprite(int num, int x, int y, int flag, bool centred) {
	g_sys.render_add_sprite(RENDER_SPR_GAME, num, x, y, flag != 0, centred);
}

void video_put_pixel(int x, int y, uint8_t color) {
	g_res.vga[y * GAME_SCREEN_W + x] = color;
}
