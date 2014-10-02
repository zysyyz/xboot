/*
 * drivers/console/console-fb.c
 *
 * Copyright(c) 2007-2014 Jianjun Jiang <8192542@qq.com>
 * Official site: http://xboot.org
 * Mobile phone: +86-18665388956
 * QQ: 8192542
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <xboot.h>
#include <console/console.h>
#include <console/console-fb-font.h>
#include <console/console-fb.h>

enum tcolor_t {
	TCOLOR_BLACK			= 0x00,
	TCOLOR_RED				= 0x01,
	TCOLOR_GREEN			= 0x02,
	TCOLOR_YELLOW			= 0x03,
	TCOLOR_BULE				= 0x04,
	TCOLOR_MAGENTA			= 0x05,
	TCOLOR_CYAN				= 0x06,
	TCOLOR_WHITE			= 0x07,

	TCOLOR_BRIGHT_BLACK		= 0x08,
	TCOLOR_BRIGHT_RED		= 0x09,
	TCOLOR_BRIGHT_GREEN		= 0x0a,
	TCOLOR_BRIGHT_YELLOW	= 0x0b,
	TCOLOR_BRIGHT_BULE		= 0x0c,
	TCOLOR_BRIGHT_MAGENTA	= 0x0d,
	TCOLOR_BRIGHT_CYAN		= 0x0e,
	TCOLOR_BRIGHT_WHITE		= 0x0f,
};

enum esc_state_t {
	ESC_STATE_NORMAL,
	ESC_STATE_ESC,
	ESC_STATE_CSI,
};

struct cell_t
{
	u32_t cp;
	struct color_t fc, bc;
};

struct console_fb_data_t {
	struct fb_t * fb;

	int fw, fh;
	int w, h;
	int x, y;
	int sx, sy;
	int cursor;
	enum tcolor_t f, b;
	struct color_t fc, bc;

	struct cell_t * cell;
	int clen;

	enum esc_state_t state;
	int params[8];
	int num_params;
	char utf8[32];
	int usize;
};

static const u8_t tcolor_to_rgba_table[16][3] = {
	/* 0x00 */	{ 0x00, 0x00, 0x00 },
	/* 0x01 */	{ 0xcd, 0x00, 0x00 },
	/* 0x02 */	{ 0x00, 0xcd, 0x00 },
	/* 0x03 */	{ 0xcd, 0xcd, 0x00 },
	/* 0x04 */	{ 0x00, 0x00, 0xee },
	/* 0x05 */	{ 0xcd, 0x00, 0xcd },
	/* 0x06 */	{ 0x00, 0xcd, 0xcd },
	/* 0x07 */	{ 0xe5, 0xe5, 0xe5 },
	/* 0x08 */	{ 0x7f, 0x7f, 0x7f },
	/* 0x09 */	{ 0xff, 0x00, 0x00 },
	/* 0x0a */	{ 0x00, 0xff, 0x00 },
	/* 0x0b */	{ 0xff, 0xff, 0x00 },
	/* 0x0c */	{ 0x5c, 0x5c, 0xff },
	/* 0x0d */	{ 0xff, 0x00, 0xff },
	/* 0x0e */	{ 0x00, 0xff, 0xff },
	/* 0x0f */	{ 0xff, 0xff, 0xff },
};

static bool_t tcolor_to_color(enum tcolor_t c, struct color_t * col)
{
	u8_t index = c & 0x0f;

	col->r = tcolor_to_rgba_table[index][0];
	col->g = tcolor_to_rgba_table[index][1];
	col->b = tcolor_to_rgba_table[index][2];
	col->a = 0xff;

	return TRUE;
}

static void fb_helper_fill_rect(struct fb_t * fb, struct color_t * c, u32_t x, u32_t y, u32_t w, u32_t h)
{
	struct rect_t rect;

	rect.x = x;
	rect.y = y;
	rect.w = w;
	rect.h = h;

	render_clear(fb->alone, &rect, c);
}

static void fb_helper_blit(struct fb_t * fb, struct texture_t * texture, u32_t x, u32_t y, u32_t w, u32_t h, u32_t ox, u32_t oy)
{
	struct rect_t drect, srect;

	drect.x = x;
	drect.y = y;
	drect.w = w;
	drect.h = h;

	srect.x = ox;
	srect.y = oy;
	srect.w = w;
	srect.h = h;

	render_blit_texture(fb->alone, &drect, texture, &srect);
}

static void fb_helper_putcode(struct fb_t * fb, u32_t code, struct color_t * fc, struct color_t * bc, u32_t x, u32_t y)
{
	struct texture_t * face = lookup_console_font_face(fb->alone, code, fc, bc);

	if(face)
		fb_helper_blit(fb, face, x, y, face->width, face->height, 0, 0);
	render_free_texture(fb->alone, face);
}

static void console_fb_cursor_gotoxy(struct console_fb_data_t * dat, int x, int y)
{
	struct cell_t * cell;
	int pos, px, py;

	if(x < 0)
		x = 0;
	if(y < 0)
		y = 0;

	if(x > dat->w - 1)
		x = dat->w - 1;
	if(y > dat->h - 1)
		y = dat->h - 1;

	if(dat->cursor != 0)
	{
		pos = dat->w * dat->y + dat->x;
		cell = &(dat->cell[pos]);
		px = (pos % dat->w) * dat->fw;
		py = (pos / dat->w) * dat->fh;
		fb_helper_putcode(dat->fb, cell->cp, &(cell->fc), &(cell->bc), px, py);

		pos = dat->w * y + x;
		cell = &(dat->cell[pos]);
		px = (pos % dat->w) * dat->fw;
		py = (pos / dat->w) * dat->fh;
		fb_helper_putcode(dat->fb, cell->cp, &(dat->bc), &(dat->fc), px, py);
	}

	dat->x = x;
	dat->y = y;
}

static void console_fb_save_cursor(struct console_fb_data_t * dat)
{
	dat->sx = dat->x;
	dat->sy = dat->y;
}

static void console_fb_restore_cursor(struct console_fb_data_t * dat)
{
	console_fb_cursor_gotoxy(dat, dat->sx, dat->sy);
}

static void console_fb_show_cursor(struct console_fb_data_t * dat, int show)
{
	struct cell_t * cell;
	int pos, px, py;

	dat->cursor = (show != 0) ? 1 : 0;

	pos = dat->w * dat->y + dat->x;
	cell = &(dat->cell[pos]);
	px = (pos % dat->w) * dat->fw;
	py = (pos / dat->w) * dat->fh;

	if(dat->cursor != 0)
		fb_helper_putcode(dat->fb, cell->cp, &(dat->bc), &(dat->fc), px, py);
	else
		fb_helper_putcode(dat->fb, cell->cp, &(cell->fc), &(cell->bc), px, py);
}

static void console_fb_set_color(struct console_fb_data_t * dat, enum tcolor_t f, enum tcolor_t b)
{
	dat->f = f;
	dat->b = b;
	tcolor_to_color(f, &(dat->fc));
	tcolor_to_color(b, &(dat->bc));
}

static void console_fb_clear_screen(struct console_fb_data_t * dat)
{
	struct cell_t * cell = &(dat->cell[0]);
	int i;

	for(i = 0; i < dat->clen; i++)
	{
		cell->cp = ' ';
		memcpy(&(cell->fc), &(dat->fc), sizeof(struct color_t));
		memcpy(&(cell->bc), &(dat->bc), sizeof(struct color_t));
		cell++;
	}

	fb_helper_fill_rect(dat->fb, &(dat->bc), 0, 0, (dat->w * dat->fw), (dat->h * dat->fh));
	console_fb_cursor_gotoxy(dat, 0, 0);
}

static void console_fb_scrollup(struct console_fb_data_t * dat)
{
	struct cell_t * p, * q;
	int m, l;
	int i;

	l = dat->w;
	m = dat->clen - l;
	p = &(dat->cell[0]);
	q = &(dat->cell[l]);

	for(i = 0; i < m; i++)
	{
		p->cp = q->cp;
		p->fc = q->fc;
		p->bc = q->bc;

		p++;
		q++;
	}

	while( (l--) > 0 )
	{
		p->cp = ' ';
		p->fc = dat->fc;
		p->bc = dat->bc;
		p++;
	}

	struct texture_t * t = render_snapshot(dat->fb->alone);
	fb_helper_blit(dat->fb, t, 0, 0, (dat->w * dat->fw), ((dat->h - 1) * dat->fh), 0, dat->fh);
	render_free_texture(dat->fb->alone, t);
	fb_helper_fill_rect(dat->fb, &(dat->bc), 0, ((dat->h - 1) * dat->fh), (dat->w * dat->fw), dat->fh);
	console_fb_cursor_gotoxy(dat, dat->x, dat->y - 1);
}

static void console_fb_putcode(struct console_fb_data_t * dat, u32_t code)
{
	struct cell_t * cell;
	int pos, px, py;
	int w, i;

	switch(code)
	{
	case '\b':
		return;

	case '\t':
		i = 8 - (dat->x % 8);
		if(i + dat->x >= dat->w)
			i = dat->w - dat->x - 1;

		while(i--)
		{
			pos = dat->w * dat->y + dat->x;
			cell = &(dat->cell[pos]);

			cell->cp = ' ';
			memcpy(&(cell->fc), &(dat->fc), sizeof(struct color_t));
			memcpy(&(cell->bc), &(dat->bc), sizeof(struct color_t));

			px = (pos % dat->w) * dat->fw;
			py = (pos / dat->w) * dat->fh;
			fb_helper_putcode(dat->fb, cell->cp, &(cell->fc), &(cell->bc), px, py);
			dat->x = dat->x + 1;
		}
		console_fb_cursor_gotoxy(dat, dat->x, dat->y);
		break;

	case '\n':
		if(dat->y + 1 >= dat->h)
			console_fb_scrollup(dat);
		console_fb_cursor_gotoxy(dat, 0, dat->y + 1);
		break;

	case '\r':
		console_fb_cursor_gotoxy(dat, 0, dat->y);
		break;

	default:
		w = ucs4_width(code);
		if(w <= 0)
			return;

		pos = dat->w * dat->y + dat->x;
		cell = &(dat->cell[pos]);

		cell->cp = code;
		memcpy(&(cell->fc), &(dat->fc), sizeof(struct color_t));
		memcpy(&(cell->bc), &(dat->bc), sizeof(struct color_t));

		for(i = 1; i < w; i++)
		{
			((struct cell_t *)(cell + i))->cp = ' ';
			((struct cell_t *)(cell + i))->fc = dat->fc;
			((struct cell_t *)(cell + i))->bc = dat->bc;
		}

		px = (pos % dat->w) * dat->fw;
		py = (pos / dat->w) * dat->fh;
		fb_helper_putcode(dat->fb, cell->cp, &(cell->fc), &(cell->bc), px, py);

		if(dat->x + w < dat->w)
			console_fb_cursor_gotoxy(dat, dat->x + w, dat->y);
		else
		{
			if(dat->y + 1 >= dat->h)
				console_fb_scrollup(dat);
			console_fb_cursor_gotoxy(dat, 0, dat->y + 1);
		}
		break;
	}
}

static void console_fb_putchar(struct console_fb_data_t * dat, unsigned char c)
{
	char * rest;
	u32_t cp;

	switch(dat->state)
	{
	case ESC_STATE_NORMAL:
		switch(c)
		{
		case 27:
			dat->state = ESC_STATE_ESC;
			break;

		default:
			dat->utf8[dat->usize++] = c;
			if(utf8_to_ucs4(&cp, 1, (const char *)dat->utf8, dat->usize, (const char **)&rest) > 0)
			{
				dat->usize -= rest - dat->utf8;
				memmove(dat->utf8, rest, dat->usize);
				console_fb_putcode(dat, cp);
			}
			break;
		}
		break;

	case ESC_STATE_ESC:
		if(c == '[')
		{
			dat->num_params = 0;
			dat->state = ESC_STATE_CSI;
		}
		else
		{
			dat->state = ESC_STATE_NORMAL;
		}
		break;

	case ESC_STATE_CSI:
		dat->params[dat->num_params++] = c;

		if(dat->num_params == 1)
		{
			switch(dat->params[0])
			{
			case 's':
				console_fb_save_cursor(dat);
				dat->state = ESC_STATE_NORMAL;
				break;

			case 'u':
				console_fb_restore_cursor(dat);
				dat->state = ESC_STATE_NORMAL;
				break;

			case 'A':
				console_fb_cursor_gotoxy(dat, dat->x, dat->y - 1);
				dat->state = ESC_STATE_NORMAL;
				break;

			case 'B':
				console_fb_cursor_gotoxy(dat, dat->x, dat->y + 1);
				dat->state = ESC_STATE_NORMAL;
				break;

			case 'C':
				console_fb_cursor_gotoxy(dat, dat->x + 1, dat->y);
				dat->state = ESC_STATE_NORMAL;
				break;

			case 'D':
				console_fb_cursor_gotoxy(dat, dat->x - 1, dat->y);
				dat->state = ESC_STATE_NORMAL;
				break;

			default:
				break;
			}
		}
		else if(dat->num_params == 2)
		{
			switch(dat->params[1])
			{
			case 'A':
				console_fb_cursor_gotoxy(dat, dat->x, dat->y - dat->params[0]);
				dat->state = ESC_STATE_NORMAL;
				break;

			case 'B':
				console_fb_cursor_gotoxy(dat, dat->x, dat->y + dat->params[0]);
				dat->state = ESC_STATE_NORMAL;
				break;

			case 'C':
				console_fb_cursor_gotoxy(dat, dat->x + dat->params[0], dat->y);
				dat->state = ESC_STATE_NORMAL;
				break;

			case 'D':
				console_fb_cursor_gotoxy(dat, dat->x - dat->params[0], dat->y);
				dat->state = ESC_STATE_NORMAL;
				break;

			case 'J':
				console_fb_clear_screen(dat);
				dat->state = ESC_STATE_NORMAL;
				break;

			default:
				break;
			}
		}
		else
		{
			dat->state = ESC_STATE_NORMAL;
		}
		break;

	default:
		dat->state = ESC_STATE_NORMAL;
		break;
	}
}

static ssize_t console_fb_read(struct console_t * console, unsigned char * buf, size_t count)
{
	return 0;
}

static ssize_t console_fb_write(struct console_t * console, const unsigned char * buf, size_t count)
{
	struct console_fb_data_t * dat = (struct console_fb_data_t *)console->priv;
	size_t i;

	for(i = 0; i < count; i++)
		console_fb_putchar(dat, buf[i]);
	return count;
}

static void console_fb_suspend(struct console_t * console)
{
}

static void console_fb_resume(struct console_t * console)
{
}

bool_t register_console_framebuffer(struct fb_t * fb)
{
	struct console_fb_data_t * dat;
	struct console_t * console;

	if(!fb || !fb->name)
		return FALSE;

	dat = malloc(sizeof(struct console_fb_data_t));
	if(!dat)
		return FALSE;

	console = malloc(sizeof(struct console_t));
	if(!console)
	{
		free(dat);
		return FALSE;
	}

	dat->fb = fb;
	dat->fw = 8;
	dat->fh = 16;
	dat->w = fb->alone->width / dat->fw;
	dat->h = fb->alone->height / dat->fh;
	dat->x = 0;
	dat->y = 0;
	dat->sx = dat->x;
	dat->sy = dat->y;
	dat->cursor = 1;
	dat->f = TCOLOR_WHITE;
	dat->b = TCOLOR_BLACK;
	tcolor_to_color(dat->f, &(dat->fc));
	tcolor_to_color(dat->b, &(dat->bc));
	dat->clen = dat->w * dat->h;
	dat->cell = malloc(dat->clen * sizeof(struct cell_t));
	if(!dat->cell)
	{
		free(console);
		free(dat);
		return FALSE;
	}
	memset(dat->cell, 0, dat->clen * sizeof(struct cell_t));
	dat->state = ESC_STATE_NORMAL;
	dat->num_params = 0;
	dat->usize = 0;

	console->name = strdup(fb->name);
	console->read = console_fb_read,
	console->write = console_fb_write,
	console->suspend = console_fb_suspend,
	console->resume	= console_fb_resume,
	console->priv = dat;

	if(register_console(console))
		return TRUE;

	free(dat->cell);
	free(console->priv);
	free(console->name);
	free(console);
	return FALSE;
}

bool_t unregister_console_framebuffer(struct fb_t * fb)
{
	struct console_t * console;
	struct console_fb_data_t * dat;

	console = search_console(fb->name);
	if(!console)
		return FALSE;
	dat = (struct console_fb_data_t *)console->priv;

	if(!unregister_console(console))
		return FALSE;

	free(dat->cell);
	free(dat);
	free(console->name);
	free(console);
	return TRUE;
}
