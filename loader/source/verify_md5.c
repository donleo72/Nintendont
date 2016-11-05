/*

Nintendont (Loader) - Playing Gamecubes in Wii mode on a Wii U
MD5sum verifier.

Copyright (C) 2013  crediar

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation version 2.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/

#include "verify_md5.h"
#include "global.h"
#include "md5.h"
#include "FPad.h"
#include "font.h"

#include <stdio.h>
#include <sys/time.h>
#include <ogc/lwp_watchdog.h>

#include "ff_utf8.h"

/**
 * Get the time of day with correct microseconds.
 * @param tv struct timeval
 * @param tz struct timezone
 */
static int gettimeofday_rvlfix(struct timeval *tv, struct timezone *tz)
{
	int ret = gettimeofday(tv, tz);
	if (ret != 0)
		return ret;

	// devkitPPC's gettimeofday() is completely broken.
	// tv_sec is correct, but tv_usec always returns a value less than 400.
	// Reference: http://devkitpro.org/viewtopic.php?t=3056&p=15322
#ifdef tick_microsecs
#undef tick_microsecs
#endif
#ifdef tick_nanosecs
#undef tick_nanosecs
#endif
#define tick_microsecs(ticks) ((((u64)(ticks)*8)/(u64)(TB_TIMER_CLOCK/125))%1000000)
#define tick_nanosecs(ticks) ((((u64)(ticks)*8000)/(u64)(TB_TIMER_CLOCK/125))%1000000000)
	tv->tv_usec = tick_microsecs(gettick());
	return ret;
}

/**
 * Verify a game's MD5.
 * @param gameinfo Game to verify.
 */
void VerifyMD5(const gameinfo *gi)
{
	md5_state_t state;
	md5_byte_t digest[16];
	md5_init(&state);

	ClearScreen();
	PrintInfo();

	// Initial status.
	#define STR_X(len) ((640 - ((len)*10)) / 2)
	#define STR_CONST_X(str) STR_X(sizeof(str)-1)
	#define STR_PTR_X(str) STR_X(strlen(str))
	static const char opening_str[] = "Opening disc image for verification:";
	PrintFormat(DEFAULT_SIZE, BLACK, STR_CONST_X(opening_str), 232-40, opening_str);
	PrintFormat(DEFAULT_SIZE, BLACK, STR_PTR_X(gi->Path), 232, "%s", gi->Path);
	GRRLIB_Render();
	ClearScreen();

	// Open the file.
	FIL in;
	if (f_open_char(&in, gi->Path, FA_READ|FA_OPEN_EXISTING) != FR_OK)
	{
		// Could not open the disc image.
		// TODO: Show an error.
		return;
	}

#ifdef _USE_FASTSEEK
	// Set up a FatFS link map for faster operation.
	u32 tblsize = 4; //minimum default size
	in.cltbl = malloc(tblsize * sizeof(DWORD));
	in.cltbl[0] = tblsize;
	int ret = f_lseek(&in, CREATE_LINKMAP);
	if (ret == FR_NOT_ENOUGH_CORE)
	{
		// Need to allocate more memory for the link map.
		tblsize = in.cltbl[0];
		free(in.cltbl);
		in.cltbl = malloc(tblsize * sizeof(DWORD));
		in.cltbl[0] = tblsize;
		ret = f_lseek(&in, CREATE_LINKMAP);
		if (ret != FR_OK)
		{
			// Error creating the link map.
			// We'll continue without it.
			free(in.cltbl);
			in.cltbl = NULL;
		}
	} else if (ret != FR_OK)
	{
		// Error creating the link map.
		// We'll continue without it.
		free(in.cltbl);
		in.cltbl = NULL;
	}
#endif /* _USE_FASTSEEK */

	// snprintf() buffer for the status message.
	char status_msg[128];
	int len;

	// Read 64 KB at a time.
	static const u32 buf_sz = 1024*1024;
	u8 *buf = (u8*)malloc(buf_sz);
	if (!buf)
	{
		// Memory allocation failed.
		// TODO: Show an error.
		f_close(&in);
		return;
	}

	// Start time.
	struct timeval tv;
	gettimeofday_rvlfix(&tv, NULL);
	float time_start = tv.tv_sec + ((float)tv.tv_usec / 1000000.0f);

	u32 total_read = 0;
	u32 total_read_mb = 0;
	const u32 total_size = f_size(&in);
	bool cancel = false;
	while (!f_eof(&in))
	{
		FPAD_Update();
		if (FPAD_Cancel(0))
		{
			// User cancelled the operation.
			cancel = true;
			break;
		}

		UINT read;
		FRESULT res = f_read(&in, buf, buf_sz, &read);
		if (res != FR_OK)
		{
			// TODO: Show an error.
			free(buf);
			f_close(&in);
			return;
		}

		// Process the data.
		md5_append(&state, (const md5_byte_t*)buf, read);
		total_read += read;

		// Only update the screen if the total read MB has changed.
		u32 new_total_read_mb = total_read / (1024*1024);
		if (new_total_read_mb != total_read_mb)
		{
			new_total_read_mb = total_read_mb;

			// Status update.
			ClearScreen();
			PrintInfo();
			PrintFormat(DEFAULT_SIZE, BLACK, MENU_POS_X + 430, MENU_POS_Y + 20*2, "B   : Cancel");

			static const char md5_calculating[] = "Calculating MD5...";
			PrintFormat(DEFAULT_SIZE, BLACK, STR_CONST_X(md5_calculating), 232-40, md5_calculating);
			PrintFormat(DEFAULT_SIZE, BLACK, STR_PTR_X(gi->Path), 232, "%s", gi->Path);

			// Print the status, in MiB.
			len = snprintf(status_msg, sizeof(status_msg),
				"%u of %u MiB processed",
				total_read / (1024*1024), total_size / (1024*1024));
			PrintFormat(DEFAULT_SIZE, BLACK, STR_X(len), 232+40, "%s", status_msg);

			// Render the text.
			GRRLIB_Render();
			ClearScreen();
		}
	}

	free(buf);
	f_close(&in);
#ifdef _USE_FASTSEEK
	free(in.cltbl);
#endif

	if (cancel)
	{
		// User cancelled the operation.
		return;
	}

	// Finish the MD5 calculations.
	md5_finish(&state, digest);

	// End time.
	gettimeofday_rvlfix(&tv, NULL);
	float time_end = tv.tv_sec + ((float)tv.tv_usec / 1000000.0f);

	// Finished processing the MD5.
	// TODO: Verify against a list of MD5s.
	ClearScreen();
	PrintInfo();
	len = snprintf(status_msg, sizeof(status_msg), "MD5 calculated in %0.1f seconds.", time_end - time_start);
	static const char press_a_btn[] = "Press the A button to continue.";
	PrintFormat(DEFAULT_SIZE, BLACK, STR_X(len), 232-40, status_msg);
	PrintFormat(DEFAULT_SIZE, BLACK, STR_PTR_X(gi->Path), 232, "%s", gi->Path);
	PrintFormat(DEFAULT_SIZE, BLACK, STR_CONST_X(press_a_btn), 232+80, press_a_btn);

	// Convert the MD5 to a string.
	static const char hex_lookup[16] = {
		'0','1','2','3','4','5','6','7',
		'8','9','A','B','C','D','E','F'
	};
	char md5_str[33];
	int i;
	for (i = 0; i < 16; i++) {
		md5_str[(i*2)+0] = hex_lookup[digest[i] >> 4];
		md5_str[(i*2)+1] = hex_lookup[digest[i] & 0x0F];
	}
	md5_str[32] = 0;

	// Print the MD5.
	len = snprintf(status_msg, sizeof(status_msg), "MD5: %s", md5_str);
	PrintFormat(DEFAULT_SIZE, BLACK, STR_X(len), 232+40, "%s", status_msg);

	// Render the text.
	GRRLIB_Render();
	ClearScreen();

	// Wait for the user to press the A button.
	while (1)
	{
		VIDEO_WaitVSync();
		FPAD_Update();

		if (FPAD_OK(0))
			break;
	}
}
