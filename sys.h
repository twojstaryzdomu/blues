
#ifndef SYS_H__
#define SYS_H__

#include "intern.h"

#define INPUT_DIRECTION_LEFT  (1 << 0)
#define INPUT_DIRECTION_RIGHT (1 << 1)
#define INPUT_DIRECTION_UP    (1 << 2)
#define INPUT_DIRECTION_DOWN  (1 << 3)

#define SYS_AUDIO_FREQ 22050

#define RENDER_SPR_GAME  0 /* player sprites */
#define RENDER_SPR_LEVEL 1 /* level sprites */
#define RENDER_SPR_FG    2 /* foreground tiles */

#define ORIG_W 320
#define ORIG_H 200

#define FULLSCREEN_W 640
#define FULLSCREEN_H 360

#define GAME_SCREEN_W g_sys.w
#define GAME_SCREEN_H g_sys.h

#define MESSAGE_MAX 30
#define MESSAGE_TIMELIMIT 1000
#define MAX_MESSAGES 5

struct input_t {
	uint8_t direction;
	bool quit;
	bool space;
	bool digit1, digit2, digit3;
	char jump_button[1];
};

typedef void (*sys_audio_cb)(void *, uint8_t *data, int len);

struct sys_rect_t {
	int x, y;
	int w, h;
};

enum sys_transition_e {
	TRANSITION_SQUARE,
	TRANSITION_CURTAIN
};

enum sys_slide_e {
	SLIDE_BOTTOM = 1
};

struct sys_t {
	struct input_t	input;
	int	(*init)();
	void	(*fini)();
	void	(*set_screen_size)(int w, int h, const char *caption, int scale, const char *filter, bool fullscreen, bool hybrid_color);
	void	(*set_screen_palette)(const uint8_t *colors, int offset, int count, int depth);
	void	(*set_palette_amiga)(const uint16_t *colors, int offset);
	void	(*set_copper_bars)(const uint16_t *data);
	void	(*set_palette_color)(int i, const uint8_t *colors);
	void	(*fade_in_palette)();
	void	(*fade_out_palette)();
	void	(*resize_screen)();
	void	(*update_screen)(const uint8_t *p, int present);
	void	(*update_screen_cached)(const uint8_t *p, int present, bool cache_redraw);
	void	(*shake_screen)(int dx, int dy);
	void	(*transition_screen)(const struct sys_rect_t *s, enum sys_transition_e type, bool open);
	void	(*process_events)();
	void	(*sleep)(int duration);
	uint32_t	(*get_timestamp)();
	void	(*start_audio)(sys_audio_cb callback, void *param);
	void	(*stop_audio)();
	void	(*lock_audio)();
	void	(*unlock_audio)();
	void	(*render_load_sprites)(int spr_type, int count, const struct sys_rect_t *r, const uint8_t *data, int w, int h, uint8_t color_key, bool update_pal);
	void	(*render_unload_sprites)(int spr_type);
	void	(*render_add_sprite)(int spr_type, int frame, int x, int y, int xflip, bool centred);
	void	(*render_clear_sprites)();
	void	(*render_set_sprites_clipping_rect)(int x, int y, int w, int h);
	void	(*add_message)(char *m);
	char*	(*get_message)();
	void	(*clear_message)(const char *m);
	void	(*clear_messages)();
	void	(*clear_slide)();
	struct	sys_rect_t slide_rect;
	enum	sys_slide_e slide_type;
	uint16_t	slide_end;
	char	*message_queue[MAX_MESSAGES];
	bool	paused;
	bool	audio;
	bool	resize;
	bool	rehint;
	bool	cycle_palette;
	bool	hybrid_color;
	bool	centred;
	bool	reset_cache_counters;
	int8_t	palette_offset;
	int	w, h;
};

extern struct sys_t g_sys;

#endif /* SYS_H__ */
