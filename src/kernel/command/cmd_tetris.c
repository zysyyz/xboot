/*
 * kernel/command/cmd_tetris.c
 *
 * Copyright (c) 2007-2009  jianjun jiang <jerryjianjun@gmail.com>
 * official site: http://xboot.org
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

#include <configs.h>
#include <default.h>
#include <types.h>
#include <string.h>
#include <malloc.h>
#include <rand.h>
#include <time/tick.h>
#include <time/timer.h>
#include <time/delay.h>
#include <xboot/log.h>
#include <xboot/list.h>
#include <xboot/printk.h>
#include <xboot/scank.h>
#include <xboot/initcall.h>
#include <console/console.h>
#include <command/command.h>


#if	defined(CONFIG_COMMAND_TETRIS) && (CONFIG_COMMAND_TETRIS > 0)

/* dimensions of playing area */
#define	GAME_AREA_WIDTH				(12)
#define	GAME_AREA_HEIGHT			(22)

/*
 * define tetris shape
 */
struct shape
{
	/* pointer to shape rotated +/- 90 degrees */
	x_s32 plus90, minus90;

	/* shape color */
	enum tcolor color;

	/* drawing instructions for this shape */
	struct vector {
		x_s32 x;
		x_s32 y;
	} direction[4];
};

static const struct shape shapes[] = {
	/* o shape */
    { 0,	0,	TCOLOR_BULE,		{{  0, -1 }, { +1,  0 }, {  0, +1 }, { -1,  0 }} },
    /* i shape */
    { 2,	2,	TCOLOR_GREEN,		{{ -1,  0 }, { +1,  0 }, { +1,  0 }, { +1,  0 }} },
    { 1,	1,	TCOLOR_GREEN,		{{  0, -1 }, {  0, +1 }, {  0, +1 }, {  0, +1 }} },
    /* z shape */
    { 4,	4,	TCOLOR_CYAN,		{{ -1,  0 }, { +1,  0 }, {  0, +1 }, { +1,  0 }} },
    { 3,	3,	TCOLOR_CYAN,		{{  0, -1 }, {  0, +1 }, { -1,  0 }, {  0, +1 }} },
    /* s shape */
    { 6,	6,	TCOLOR_RED,			{{ +1,  0 }, { -1,  0 }, {  0, +1 }, { -1,  0 }} },
    { 5,	5,	TCOLOR_RED,			{{  0, -1 }, {  0, +1 }, { +1,  0 }, {  0, +1 }} },
    /* j shape */
    { 8,	10,	TCOLOR_MAGENTA,		{{ +1,  0 }, { -1,  0 }, { -1,  0 }, {  0, -1 }} },
    { 9,	7,	TCOLOR_MAGENTA,		{{  0, -1 }, {  0, +1 }, {  0, +1 }, { -1,  0 }} },
    { 10,	8,	TCOLOR_MAGENTA,		{{ -1,  0 }, { +1,  0 }, { +1,  0 }, {  0, +1 }} },
    { 7,	9,	TCOLOR_MAGENTA,		{{  0, +1 }, {  0, -1 }, {  0, -1 }, { +1,  0 }} },
    /* l shape */
    { 12,	14,	TCOLOR_YELLOW,		{{ +1,  0 }, { -1,  0 }, { -1,  0 }, {  0, +1 }} },
    { 13,	11, TCOLOR_YELLOW,		{{  0, -1 }, {  0, +1 }, {  0, +1 }, { +1,  0 }} },
    { 14,	12, TCOLOR_YELLOW,		{{ -1,  0 }, { +1,  0 }, { +1,  0 }, {  0, -1 }} },
    { 11,	13, TCOLOR_YELLOW,		{{  0, +1 }, {  0, -1 }, {  0, -1 }, { -1,  0 }} },
    /* t shape */
    { 16,	18,	TCOLOR_WHITE,		{{  0, -1 }, {  0, +1 }, { -1,  0 }, { +2,  0 }} },
    { 17,	15,	TCOLOR_WHITE,		{{ -1,  0 }, { +1,  0 }, {  0, -1 }, {  0, +2 }} },
    { 18,	16,	TCOLOR_WHITE,		{{  0, +1 }, {  0, -1 }, { +1,  0 }, { -2,  0 }} },
    { 15,	17,	TCOLOR_WHITE,		{{ +1,  0 }, { -1,  0 }, {  0, +1 }, {  0, -2 }} }
};

/*
 * define tetris map
 */
struct map {
	x_bool dirty[GAME_AREA_HEIGHT];
	enum tcolor screen[GAME_AREA_WIDTH][GAME_AREA_HEIGHT];
};

static struct map map;

/*
 * refresh game area
 */
static void refresh(void)
{
	struct console * stdout = get_stdout();
	x_s32 w, h;
	x_s32 x, y, xp, yp;

	if(!stdout)
		return;

	for(y=0; y < GAME_AREA_HEIGHT; y++)
	{
		if(map.dirty[y] == FALSE)
			continue;

		for(x=0; x < GAME_AREA_WIDTH; x++)
		{
			console_getwh(stdout, &w, &h);
			xp = (w - GAME_AREA_WIDTH) / 2;
			yp = (h - GAME_AREA_HEIGHT) / 2;
			console_gotoxy(stdout, xp + x, yp + y);
			if(map.screen[x][y] != TCOLOR_BLACK)
			{
				console_setcolor(stdout, map.screen[x][y], TCOLOR_BLACK);
				console_putcode(stdout, UNICODE_CUBE);
			}
			else
			{
				console_setcolor(stdout, TCOLOR_WHITE, TCOLOR_BLACK);
				console_putcode(stdout, UNICODE_SPACE);
			}
		}
		map.dirty[y] = FALSE;
    }
}

static void block_draw(x_s32 x, x_s32 y, enum tcolor c)
{
    if(x >= GAME_AREA_WIDTH)
        x = GAME_AREA_WIDTH - 1;
    if(y >= GAME_AREA_HEIGHT)
        y = GAME_AREA_HEIGHT - 1;

    map.screen[x][y] = c;
    map.dirty[y] = TRUE;
}

static x_bool block_hit(x_s32 x, x_s32 y)
{
	return (map.screen[x][y] != TCOLOR_BLACK);
}

static void shape_draw(x_s32 x, x_s32 y, x_u32 index)
{
    x_u32 i;

    for(i = 0; i < 4; i++)
    {
    	block_draw(x, y, shapes[index].color);
        x += shapes[index].direction[i].x;
        y += shapes[index].direction[i].y;
    }
    block_draw(x, y, shapes[index].color);
}

static void shape_erase(x_s32 x, x_s32 y, x_u32 index)
{
    x_u32 i;

    for(i = 0; i < 4; i++)
    {
    	block_draw(x, y, TCOLOR_BLACK);
        x += shapes[index].direction[i].x;
        y += shapes[index].direction[i].y;
    }
    block_draw(x, y, TCOLOR_BLACK);
}

static x_bool shape_hit(x_s32 x, x_s32 y, x_u32 index)
{
	x_u32 i;

    for(i = 0; i < 4; i++)
    {
    	if(block_hit(x, y))
            return TRUE;
        x += shapes[index].direction[i].x;
        y += shapes[index].direction[i].y;
    }

    if(block_hit(x, y))
        return TRUE;

    return FALSE;
}

static void collapse(void)
{
	x_s32 solidrow[GAME_AREA_HEIGHT], solidrows;
	x_s32 row, col, temp;

	/* determine which rows are solidly filled */
	solidrows = 0;
	for(row = 1; row < GAME_AREA_HEIGHT - 1; row++)
	{
		temp = 0;
		for(col = 1; col < GAME_AREA_WIDTH - 1; col++)
		{
			if(map.screen[col][row] != TCOLOR_BLACK)
				temp++;
		}

		if(temp == GAME_AREA_WIDTH - 2)
		{
			solidrow[row] = 1;
			solidrows++;
		}
		else
			solidrow[row] = 0;
	}

	if(solidrows == 0)
		return;

	/* collapse them */
	for(temp = row = GAME_AREA_HEIGHT - 2; row > 0; row--, temp--)
	{
		while(solidrow[temp])
			temp--;

		if(temp < 1)
		{
			for(col = 1; col < GAME_AREA_WIDTH - 1; col++)
				map.screen[col][row] = TCOLOR_BLACK;
		}
		else
		{
			for (col = 1; col < GAME_AREA_WIDTH - 1; col++)
				map.screen[col][row] = map.screen[col][temp];
		}
		map.dirty[row] = TRUE;
	}

	refresh();
}

static void screen_init(void)
{
	x_s32 x, y;

	for(y = 0; y < GAME_AREA_HEIGHT; y++)
	{
		map.dirty[y] = TRUE;
		for(x = 1; x < (GAME_AREA_WIDTH - 1); x++)
			map.screen[x][y] = TCOLOR_BLACK;
		map.screen[0][y] = map.screen[GAME_AREA_WIDTH - 1][y] = TCOLOR_BULE;
	}
	for(x = 0; x < GAME_AREA_WIDTH; x++)
		map.screen[x][0] = map.screen[x][GAME_AREA_HEIGHT - 1] = TCOLOR_BULE;

	collapse();
}

static x_s32 tetris(x_s32 argc, const x_s8 **argv)
{
	struct console * stdout = get_stdout();
	x_u32 x, y, shape;
	x_u32 newx, newy, newshape;
	x_bool fell = FALSE;
	x_bool try_again = FALSE;
	x_u32 code;

	if(!stdout)
		return -1;

	console_setcursor(stdout, FALSE);
	console_cls(stdout);

	srand(jiffies + rand());

	do {
		screen_init();

		y = 3;
		x = GAME_AREA_WIDTH / 2;
		shape = rand() % ARRAY_SIZE(shapes);
        shape_draw(x, y, shape);
        refresh();

        while(1)
        {
            newx = x;
            newy = y;
            newshape = shape;

            if(getcode_with_timeout(&code, 250))
            {
    			switch(code)
    			{
    			case 0x10:	/* up */
    				newshape = shapes[shape].plus90;
    				fell = FALSE;
    				break;

    			case 0xe:	/* down */
                    if(y < GAME_AREA_HEIGHT - 1)
                        newy = y + 1;
                    fell = TRUE;
    				break;

    			case 0x2:	/* left */
    				if(x > 0)
    					newx = x - 1;
    				fell = FALSE;
    				break;

    			case 0x6:	/* right */
    				if(x < GAME_AREA_WIDTH - 1)
    					newx = x + 1;
    				fell = FALSE;
    				break;

    			default:
    				newy++;
    				fell = TRUE;
    				break;
    			}

            }
            else
            {
				newy++;
				fell = TRUE;
            }

	        if((newx == x) && (newy == y) && (newshape == shape))
	            continue;

	        shape_erase(x, y, shape);
	        if(shape_hit(newx, newy, newshape) == FALSE)
	        {
				x = newx;
	            y = newy;
	            shape = newshape;
	        }
	        else if(fell == TRUE)
	        {
	            shape_draw(x, y, shape);

	    		y = 3;
	    		x = GAME_AREA_WIDTH / 2;
	    		shape = rand() % ARRAY_SIZE(shapes);
	    		collapse();

	            if(shape_hit(x, y, shape))
	            {
	            	try_again = FALSE;
	            	break;
	            }
	        }

	        shape_draw(x, y, shape);
	        refresh();
        }
	}while(try_again);

	console_setcursor(stdout, TRUE);
	console_setcolor(stdout, TCOLOR_WHITE, TCOLOR_BLACK);
	console_cls(stdout);

	return 0;
}

static struct command tetris_cmd = {
	.name		= "tetris",
	.func		= tetris,
	.desc		= "classic video game tetris\r\n",
	.usage		= "tetris\r\n",
	.help		= "    tetris is a puzzle video game.\r\n"
};

static __init void tetris_cmd_init(void)
{
	if(!command_register(&tetris_cmd))
		LOG_E("register 'tetris' command fail");
}

static __exit void tetris_cmd_exit(void)
{
	if(!command_unregister(&tetris_cmd))
		LOG_E("unregister 'tetris' command fail");
}

module_init(tetris_cmd_init, LEVEL_COMMAND);
module_exit(tetris_cmd_exit, LEVEL_COMMAND);

#endif
