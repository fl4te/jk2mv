// console.c

#include "client.h"
#include "../strings/con_text.h"
#include "../qcommon/strip.h"
#include <mv_setup.h>


console_t	con;

cvar_t		*con_height;
cvar_t		*con_notifytime;
cvar_t		*con_scale;
cvar_t		*con_speed;
cvar_t		*con_timestamps;

//EternalJK2MV
cvar_t		*con_opacity;

#define	DEFAULT_CONSOLE_WIDTH	78
#define CON_BLANK_CHAR			' '
#define CON_SCROLL_L_CHAR		'$'
#define CON_SCROLL_R_CHAR		'$'
#define CON_TIMESTAMP_LEN		11 // "[13:37:00] "
#define CON_MIN_WIDTH			20

static const conChar_t CON_WRAP = { { ColorIndex_Extended(COLOR_LT_TRANSPARENT), '\\' } };
static const conChar_t CON_BLANK = { { ColorIndex(COLOR_WHITE), CON_BLANK_CHAR } };

vec4_t	console_color = {1.0, 1.0, 1.0, 1.0};

/*
================
Con_ToggleConsole_f
================
*/
void Con_ToggleConsole_f (void) {
	// closing a full screen console restarts the demo loop
	if ( cls.state == CA_DISCONNECTED && cls.keyCatchers == KEYCATCH_CONSOLE ) {
		CL_StartDemoLoop();
		return;
	}

	Field_Clear( &kg.g_consoleField );

	Con_ClearNotify ();
	cls.keyCatchers ^= KEYCATCH_CONSOLE;
}

/*
================
Con_MessageMode_f
================
*/
void Con_MessageMode_f (void) {		//yell
	chat_playerNum = -1;
	chat_team = qfalse;
	Field_Clear( &chatField );
	chatField.widthInChars = 30 / cls.cgxadj;

	cls.keyCatchers ^= KEYCATCH_MESSAGE;
}

/*
================
Con_MessageMode2_f
================
*/
void Con_MessageMode2_f (void) {	//team chat
	chat_playerNum = -1;
	chat_team = qtrue;
	Field_Clear( &chatField );
	chatField.widthInChars = 25 / cls.cgxadj;
	cls.keyCatchers ^= KEYCATCH_MESSAGE;
}

/*
================
Con_MessageMode3_f
================
*/
void Con_MessageMode3_f (void) {	//target chat
	if (cl.snap.ps.pm_flags & PMF_FOLLOW) { //Send to the person we are spectating instead
		chat_playerNum = cl.snap.ps.clientNum;
	}
	else {
		chat_playerNum = VM_Call(cgvm, CG_CROSSHAIR_PLAYER);
	}
	if ( chat_playerNum < 0 || chat_playerNum >= MAX_CLIENTS ) {
		chat_playerNum = -1;
		return;
	}
	chat_team = qfalse;
	Field_Clear( &chatField );
	chatField.widthInChars = 26 / cls.cgxadj;
	cls.keyCatchers ^= KEYCATCH_MESSAGE;
}

/*
================
Con_MessageMode4_f
================
*/
void Con_MessageMode4_f (void) {	//attacker
	chat_playerNum = VM_Call( cgvm, CG_LAST_ATTACKER );
	if ( chat_playerNum < 0 || chat_playerNum >= MAX_CLIENTS ) {
		chat_playerNum = -1;
		return;
	}
	chat_team = qfalse;
	Field_Clear( &chatField );
	chatField.widthInChars = 30 / cls.cgxadj;
	cls.keyCatchers ^= KEYCATCH_MESSAGE;
}

/*
================
Con_Clear_f
================
*/
void Con_Clear_f (void) {
	int		i;

	for ( i = 0 ; i < CON_TEXTSIZE ; i++ ) {
		con.text[i] = CON_BLANK;
	}

	Con_Bottom();		// go to end
}

void Con_Copy(void) {
	qboolean		empty;
	int				l, i, j, x;
	int				line;
	int				lineLen;
	char			buffer[CON_TIMESTAMP_LEN + MAXPRINTMSG + 1];
	int				bufferlen, savebufferlen;
	char			*savebuffer;

	// skip empty lines
	for (l = 1, empty = qtrue; l < con.totallines && empty; l++)
	{
		line = ((con.current + l) % con.totallines) * con.rowwidth;

		for (j = CON_TIMESTAMP_LEN; j < con.rowwidth - 1; j++)
			if (con.text[line + j].f.character != CON_BLANK_CHAR)
				empty = qfalse;
	}

#ifdef _WIN32
	bufferlen = con.linewidth + 3;
#else
	bufferlen = con.linewidth + 2;
#endif

	savebufferlen = bufferlen*(con.current - l);
	savebuffer = (char *)Hunk_AllocateTempMemory(savebufferlen);
	memset(savebuffer, 0, savebufferlen);

	for (; l < con.totallines; l++)
	{
		lineLen = 0;
		i = 0;
		x = 0;

		// Print timestamp
		if (con_timestamps->integer) {
			line = ((con.current + l) % con.totallines) * con.rowwidth;

			for (i = 0; i < CON_TIMESTAMP_LEN; i++)
				buffer[i] = con.text[line + i].f.character;

			lineLen = CON_TIMESTAMP_LEN;
		}

		// Concatenate wrapped lines
		for (; l < con.totallines; l++)
		{
			line = ((con.current + l) % con.totallines) * con.rowwidth;

			for (j = CON_TIMESTAMP_LEN; j < con.rowwidth - 1 && i < (int)sizeof(buffer) - 1; j++, i++) {
				buffer[i] = con.text[line + j].f.character;

				if (con.text[line + j].f.character != CON_BLANK_CHAR)
					lineLen = i + 1;
			}

			if (i == sizeof(buffer) - 1)
				break;

			if (con.text[line + j].compare != CON_WRAP.compare)
				break;
		}

		for (x = con.linewidth - 1; x >= 0; x--)
		{
			if (buffer[x] == CON_BLANK_CHAR)
				buffer[x] = 0;
			else
				break;
		}

		buffer[lineLen] = '\n';

		Q_strcat(savebuffer, savebufferlen, buffer);
	}


	Sys_SetClipboardData(savebuffer);
	Com_Printf("^2Console successfully copied to clipboard!\n");
	Hunk_FreeTempMemory(savebuffer);
}

void Con_CopyLink(void) {
	int l, x, i, pointDiff;
	//short *line;
	conChar_t *line;
	char *buffer, n[] = "\0";
	const char *link, *point1, *point2, *point3;
	qboolean containsNum = qfalse, containsPoint = qfalse;

	buffer = (char *)Hunk_AllocateTempMemory(con.linewidth);

	for (l = con.current; l >= con.current - 32; l--)
	{
		line = con.text + (l%con.totallines)*con.linewidth;
		for (i = 0; i < con.linewidth; i++) {
			buffer[i] = (char)(line[i].f.character);// & 0xff);
			if (!containsNum && Q_isanumber(&buffer[i])) containsNum = qtrue;
			if (!containsPoint && buffer[i] == '.') containsPoint = qtrue;
		}
		// Clear spaces at end of buffer
		for (x = con.linewidth - 1; x >= 0; x--) {
			if (buffer[x] == ' ')
				buffer[x] = 0;
			else
				break;
		}
		Q_StripColor(buffer);
		if ((link = Q_stristr(buffer, "://")) || (link = Q_stristr(buffer, "www."))) {
			// Move link ptr back until it hits a space or first char of string
			while (link != &buffer[0] && *(link - 1) != ' ') link--;
			for (i = 0; buffer[i] != 0; i++) {
				buffer[i] = *link++;
				if (*link == ' ' || *link == '"') buffer[i + 1] = 0;
			}
			Sys_SetClipboardData(buffer);
			Com_Printf("^2Link ^7\"%s\" ^2Copied!\n", buffer);
			break;
		}
		if (containsNum && containsPoint) {
			containsNum = qfalse, containsPoint = qfalse;
			if (!(point1 = Q_stristr(buffer, ".")) || // Set address of first point
													  // Check if points exist after point1 and set their addresses
				!(point2 = Q_stristr(point1 + 1, ".")) ||
				!(point3 = Q_stristr(point2 + 1, "."))) continue;
			for (i = 0; buffer[i] != 0; i++) {
				if (point1 == &buffer[i]) { // If addresses match, set point1 to next point
											// Check if points exist and set point addresses
					if (
						!(point1 = Q_stristr(&buffer[i + 1], ".")) ||
						!(point2 = Q_stristr(point1 + 1, ".")) ||
						!(point3 = Q_stristr(point2 + 1, "."))
						) break;
				}
				*n = buffer[i]; // Force Q_isanumber to look at a single char
				if (Q_isanumber(n)) {
					// Check if chars exist between points and the amount of chars is > 0 & <=3
					// <xxx>.<xxx>.<xxx>. Can't reliably check for chars after last point
					if ((pointDiff = point1 - &buffer[i]) <= 3 &&
						pointDiff > 0 &&
						(pointDiff = point2 - (point1 + 1)) <= 3 &&
						pointDiff > 0 &&
						(pointDiff = point3 - (point2 + 1)) <= 3 &&
						pointDiff > 0
						) {
						link = &buffer[i];
						break;
					}
				}
			}
			if (link) {
				for (i = 0; buffer[i] != 0; i++) {
					buffer[i] = *link++;
					if (*link == ' ' || *link == '"') buffer[i + 1] = 0;
				}
				Sys_SetClipboardData(buffer);
				Com_Printf("^2IP ^7\"%s\" ^2Copied!\n", buffer);
				break;
			}
		}
	}
	if (!link) {
		Com_Printf("^1No Links or IPs found!\n", buffer);
	}
	Hunk_FreeTempMemory(buffer);
}


/*
================
Con_Dump_f

Save the console contents out to a file
================
*/
void Con_Dump_f (void)
{
	char			fileName[MAX_QPATH];
	qboolean		empty;
	int				l, i, j;
	int				line;
	int				lineLen;
	fileHandle_t	f;
	char			buffer[CON_TIMESTAMP_LEN + MAXPRINTMSG + 1];

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("%s\n", SP_GetStringText(CON_TEXT_DUMP_USAGE));
		return;
	}

	Q_strncpyz( fileName, Cmd_Argv( 1 ), sizeof( fileName ) );
	COM_SanitizeExtension( fileName, sizeof( fileName ), ".txt" );

	f = FS_FOpenFileWrite( fileName );
	if (!f)
	{
		Com_Printf (S_COLOR_RED"ERROR: couldn't open %s.\n", fileName);
		return;
	}

	// skip empty lines
	for (l = 1, empty = qtrue ; l < con.totallines && empty ; l++)
	{
		line = ((con.current + l) % con.totallines) * con.rowwidth;

		for (j = CON_TIMESTAMP_LEN ; j < con.rowwidth - 1 ; j++)
			if (con.text[line + j].f.character != CON_BLANK_CHAR)
				empty = qfalse;
	}

	for ( ; l < con.totallines ; l++)
	{
		lineLen = 0;
		i = 0;

		// Print timestamp
		if (con_timestamps->integer) {
			line = ((con.current + l) % con.totallines) * con.rowwidth;

			for (i = 0; i < CON_TIMESTAMP_LEN; i++)
				buffer[i] = con.text[line + i].f.character;

			lineLen = CON_TIMESTAMP_LEN;
		}

		// Concatenate wrapped lines
		for ( ; l < con.totallines ; l++)
		{
			line = ((con.current + l) % con.totallines) * con.rowwidth;

			for (j = CON_TIMESTAMP_LEN; j < con.rowwidth - 1 && i < (int)sizeof(buffer) - 1; j++, i++) {
				buffer[i] = con.text[line + j].f.character;

				if (con.text[line + j].f.character != CON_BLANK_CHAR)
					lineLen = i + 1;
			}

			if (i == sizeof(buffer) - 1)
				break;

			if (con.text[line + j].compare != CON_WRAP.compare)
				break;
		}

		buffer[lineLen] = '\n';
		FS_Write(buffer, lineLen + 1, f);
	}

	FS_FCloseFile( f );

	Com_Printf ("Dumped console text to %s.\n", fileName );
}


/*
================
Con_ClearNotify
================
*/
void Con_ClearNotify( void ) {
	int		i;

	for ( i = 0 ; i < NUM_CON_TIMES ; i++ ) {
		con.times[i] = 0;
	}
}



/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize (void)
{
	int		i, j;
	int		width;
	int		oldrowwidth;
	int		oldtotallines;
	static conChar_t tbuf[CON_TEXTSIZE];

	if (cls.glconfig.vidWidth <= 0.0f)			// video hasn't been initialized yet
	{
		cls.xadjust = 1;
		cls.yadjust = 1;
		con.charWidth = SMALLCHAR_WIDTH;
		con.charHeight = SMALLCHAR_HEIGHT;
		con.linewidth = DEFAULT_CONSOLE_WIDTH;
		con.rowwidth = CON_TIMESTAMP_LEN + con.linewidth + 1;
		con.totallines = CON_TEXTSIZE / con.rowwidth;
		con.current = con.totallines - 1;
		for(i=0; i<CON_TEXTSIZE; i++)
		{
			con.text[i] = CON_BLANK;
		}
	}
	else
	{
		float	scale = cls.glconfig.displayDPI / 96.0f *
			((con_scale && con_scale->value > 0.0f) ? con_scale->value : 1.0f);
		int		charWidth = scale * SMALLCHAR_WIDTH;

		if (charWidth < 1) {
			charWidth = 1;
			scale = 1.0f / SMALLCHAR_WIDTH;
		}

		width = (cls.glconfig.vidWidth / charWidth) - 2;

		if (width < 20) {
			width = 20;
			charWidth = cls.glconfig.vidWidth / 22;
			scale = charWidth / SMALLCHAR_WIDTH;
		}

		if (con_timestamps->integer) {
			if (width == con.rowwidth - 1)
				return;
		} else {
			if (width == con.rowwidth - CON_TIMESTAMP_LEN - 1)
				return;
		}

		con.charWidth = charWidth;
		con.charHeight = scale * SMALLCHAR_HEIGHT;

		kg.g_consoleField.widthInChars = width - 1; // Command prompt

		con.linewidth = width;
		oldrowwidth = con.rowwidth;
		con.rowwidth = width + 1;
		if (!con_timestamps->integer)
			con.rowwidth += CON_TIMESTAMP_LEN;
		oldtotallines = con.totallines;
		con.totallines = CON_TEXTSIZE / con.rowwidth;

		Com_Memcpy (tbuf, con.text, sizeof(tbuf));
		for(i=0; i<CON_TEXTSIZE; i++)
			con.text[i] = CON_BLANK;

		int oi = 0;
		int ni = 0;

		while (oi < oldtotallines)
		{
			conChar_t	line[MAXPRINTMSG];
			conChar_t	timestamp[CON_TIMESTAMP_LEN];
			int		lineLen = 0;
			int		oldline = ((con.current + oi) % oldtotallines) * oldrowwidth;
			int		newline = (ni % con.totallines) * con.rowwidth;

			// Store timestamp
			for (i = 0; i < CON_TIMESTAMP_LEN; i++)
				timestamp[i] = tbuf[oldline + i];

			// Store whole line concatenating on CON_WRAP
			for (i = 0; oi < oldtotallines; oi++)
			{
				oldline = ((con.current + oi) % oldtotallines) * oldrowwidth;

				for (j = CON_TIMESTAMP_LEN; j < oldrowwidth - 1 && i < (int)ARRAY_LEN(line); j++, i++) {
					line[i] = tbuf[oldline + j];

					if (line[i].f.character != CON_BLANK_CHAR)
						lineLen = i + 1;
				}

				if (i == ARRAY_LEN(line))
					break;

				if (tbuf[oldline + j].compare != CON_WRAP.compare)
					break;
			}

			oi++;

			// Print stored line to a new text buffer
			for (i = 0; ; ni++) {
				newline = (ni % con.totallines) * con.rowwidth;

				// Print timestamp at the begining of each line
				for (j = 0; j < CON_TIMESTAMP_LEN; j++)
					con.text[newline + j] = timestamp[j];

				for (j = CON_TIMESTAMP_LEN; j < con.rowwidth - 1 && i < lineLen; j++, i++)
					con.text[newline + j] = line[i];

				if (i == lineLen) {
					// Erase remaining chars in case newline wrapped
					for (; j < con.rowwidth - 1; j++)
						con.text[newline + j] = CON_BLANK;

					ni++;
					break;
				}

				con.text[newline + j] = CON_WRAP;
			}
		}

		con.current = ni;

		// Erase con.current line for next CL_ConsolePrint
		int newline = (con.current % con.totallines) * con.rowwidth;
		for (j = 0; j < con.rowwidth; j++)
			con.text[newline + j] = CON_BLANK;

		Con_ClearNotify ();
	}

	con.display = con.current;
}


/*
================
Con_Init
================
*/
void Con_Init (void) {
	con_height = Cvar_Get ("con_height", "0.5", CVAR_GLOBAL | CVAR_ARCHIVE);
	con_notifytime = Cvar_Get ("con_notifytime", "3", CVAR_GLOBAL | CVAR_ARCHIVE);
	con_speed = Cvar_Get ("con_speed", "3", CVAR_GLOBAL | CVAR_ARCHIVE);
	con_scale = Cvar_Get ("con_scale", "1", CVAR_GLOBAL | CVAR_ARCHIVE);
	con_timestamps = Cvar_Get ("con_timestamps", "0", CVAR_GLOBAL | CVAR_ARCHIVE);
	//EternalJK2MV
	con_opacity = Cvar_Get("con_opacity", "1.0", CVAR_GLOBAL|CVAR_ARCHIVE);

	Field_Clear( &kg.g_consoleField );
	kg.g_consoleField.widthInChars = DEFAULT_CONSOLE_WIDTH - 1; // Command prompt

	Cmd_AddCommand ("toggleconsole", Con_ToggleConsole_f);
	Cmd_AddCommand ("messagemode", Con_MessageMode_f);
	Cmd_AddCommand ("messagemode2", Con_MessageMode2_f);
	Cmd_AddCommand ("messagemode3", Con_MessageMode3_f);
	Cmd_AddCommand ("messagemode4", Con_MessageMode4_f);
	Cmd_AddCommand ("clear", Con_Clear_f);
	Cmd_AddCommand ("condump", Con_Dump_f);
	Cmd_SetCommandCompletionFunc( "condump", Cmd_CompleteTxtName );

	//Initialize values on first print
	con.initialized = qfalse;
}


/*
===============
Con_Linefeed
===============
*/
static void Con_Linefeed (qboolean skipnotify)
{
	int		i;
	int		line = (con.current % con.totallines) * con.rowwidth;

	// mark time for transparent overlay
	if (con.current >= 0)
	{
		if (skipnotify)
			con.times[con.current & NUM_CON_TIMES] = 0;
		else
			con.times[con.current % NUM_CON_TIMES] = cls.realtime;
	}

	// print timestamp on the PREVIOUS line
	{
		qtime_t	time;
		char	timestamp[CON_TIMESTAMP_LEN + 1];
		const unsigned char color = ColorIndex_Extended(COLOR_LT_TRANSPARENT);

		Com_RealTime(&time);
		Com_sprintf(timestamp, sizeof(timestamp), "[%02d:%02d:%02d] ",
			time.tm_hour, time.tm_min, time.tm_sec);

		for ( i = 0; i < CON_TIMESTAMP_LEN; i++ ) {
			con.text[line + i].f = { color, timestamp[i] };
		}
	}

	con.x = 0;

	if (con.display == con.current)
		con.display++;
	con.current++;

	line = (con.current % con.totallines) * con.rowwidth;

	for ( i = 0; i < con.rowwidth; i++ )
		con.text[line + i] = CON_BLANK;
}

/*
================
CL_ConsolePrint

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be logged to disk
If no console is visible, the text will appear at the top of the game window
================
*/
void CL_ConsolePrint( const char *txt, qboolean extendedColors ) {
	unsigned char	color;
	char			c;
	int				y;
	qboolean		skipnotify = qfalse;

	// for some demos we don't want to ever show anything on the console
	if (cl_noprint && cl_noprint->integer) {
		return;
	}

	// TTimo - prefix for text that shows up in console but not in notify
	// backported from RTCW
	if (!Q_strncmp(txt, "[skipnotify]", 12)) {
		skipnotify = qtrue;
		txt += 12;
	}

	if (!con.initialized) {
		con.color[0] =
		con.color[1] =
		con.color[2] =
		con.color[3] = 1.0f;
		con.linewidth = -1;
		con.rowwidth = -1;
		Con_CheckResize ();
		con.initialized = qtrue;
	}

	const bool use102color = MV_USE102COLOR;

	color = ColorIndex(COLOR_WHITE);

	while ( (c = *txt) != 0 ) {
		if ( Q_IsColorString( txt ) ||
			(extendedColors && Q_IsColorString_Extended( txt )) ||
			( use102color && Q_IsColorString_1_02( txt ) ) )
		{
			if (extendedColors) color = ColorIndex_Extended( *(txt+1) );
			else color = ColorIndex( *(txt+1) );
			txt += 2;
			continue;
		}

		txt++;

		switch (c)
		{
		case '\n':
			Con_Linefeed(skipnotify);
			break;
		case '\r':
			con.x = 0;
			break;
		default:	// display character and advance
			y = con.current % con.totallines;

			if (con.x == con.rowwidth - CON_TIMESTAMP_LEN - 1) {
				con.text[y * con.rowwidth + CON_TIMESTAMP_LEN + con.x] = CON_WRAP;
				Con_Linefeed(skipnotify);
				y = con.current % con.totallines;
			}

			con.text[y * con.rowwidth + CON_TIMESTAMP_LEN + con.x].f = { color, c };
			con.x++;
			break;
		}
	}
}


/*
==============================================================================

DRAWING

==============================================================================
*/


/*
================
Con_DrawInput

Draw the editline after a ] prompt
================
*/
void Con_DrawInput (void) {
	int		y, x = 0;

	if ( cls.state != CA_DISCONNECTED && !(cls.keyCatchers & KEYCATCH_CONSOLE ) ) {
		return;
	}

	y = con.vislines - ( con.charHeight * (re.Language_IsAsian() ? 1.5 : 2) );

	if (con_timestamps->integer)
	{
		qtime_t	time;
		char	timestamp[CON_TIMESTAMP_LEN + 1];
		const unsigned char color = ColorIndex(CT_GREEN);

		Com_RealTime(&time);
		Com_sprintf(timestamp, sizeof(timestamp), "[%02d:%02d:%02d] ",
			time.tm_hour, time.tm_min, time.tm_sec);

		re.SetColor(colorGreen);
		for (x = 0; x < CON_TIMESTAMP_LEN; x++) {
			SCR_DrawSmallChar(con.xadjust + (x + 1) * con.charWidth, y, timestamp[x]);
		}

		x = CON_TIMESTAMP_LEN + 1;
	}

	re.SetColor( con.color );

	if (con_timestamps->integer) {
		Field_Draw(&kg.g_consoleField, x * con.charWidth, y, qtrue);
	}
	else {
		Field_Draw(&kg.g_consoleField, 2 * con.charWidth, y, qtrue);
		SCR_DrawSmallChar(con.charWidth, y, CONSOLE_PROMPT_CHAR);
	}

	re.SetColor( g_color_table[ColorIndex_Extended(COLOR_LT_TRANSPARENT)] );

	if ( kg.g_consoleField.scroll > 0 )
		SCR_DrawSmallChar( 0, y, CON_SCROLL_L_CHAR );

	int len = Q_PrintStrlen( kg.g_consoleField.buffer, MV_USE102COLOR );
	int pos = Q_PrintStrLenTo( kg.g_consoleField.buffer, kg.g_consoleField.scroll, NULL, MV_USE102COLOR);
	if ( pos + kg.g_consoleField.widthInChars < len )
		SCR_DrawSmallChar( cls.glconfig.vidWidth - con.charWidth, y, CON_SCROLL_R_CHAR );
}




/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
void Con_DrawNotify (void)
{
	unsigned char	currentColor;
	conChar_t		*text;
	int		x, v;
	int		i;
	int		time;
	int		skip;
	const char* chattext;

	currentColor = 7;
	re.SetColor( g_color_table[currentColor] );

	static int iFontIndex = re.RegisterFont("ocr_a");
	float fFontScale = 1.0f;
	int iPixelHeightToAdvance = 0;
	if (re.Language_IsAsian())
	{
		fFontScale = con.charWidth * 10.0f /
			re.Font_StrLenPixels("aaaaaaaaaa", iFontIndex, 1.0f, cls.xadjust, cls.yadjust);
		iPixelHeightToAdvance = 1.3 * re.Font_HeightPixels(iFontIndex, fFontScale, cls.xadjust, cls.yadjust);
	}

	v = 0;
	for (i= con.current-NUM_CON_TIMES+1 ; i<=con.current ; i++)
	{
		if (i < 0)
			continue;
		time = con.times[i % NUM_CON_TIMES];
		if (time == 0)
			continue;
		time = cls.realtime - time;
		if (time >= con_notifytime->value*1000)
			continue;
		text = con.text + (i % con.totallines)*con.rowwidth;
		if (!con_timestamps->integer || con_timestamps->integer == 1)
			text += CON_TIMESTAMP_LEN;

		if (cl.snap.ps.pm_type != PM_INTERMISSION && cls.keyCatchers & (KEYCATCH_UI | KEYCATCH_CGAME) ) {
			continue;
		}


		if (!cl_conXOffset)
		{
			cl_conXOffset = Cvar_Get ("cl_conXOffset", "0", 0);
		}

		// asian language needs to use the new font system to print glyphs...
		//
		// (ignore colours since we're going to print the whole thing as one string)
		//
		if (re.Language_IsAsian())
		{
			// concat the text to be printed...
			//
			char sTemp[4096];	// ott
			sTemp[0] = '\0';
			for (x = 0 ; x < con.linewidth ; x++)
			{
				if ( text[x].f.color != currentColor ) {
					currentColor = text[x].f.color;
					strcat(sTemp,va("^%i", (currentColor > 7 ? COLOR_JK2MV_FALLBACK : currentColor) ));
				}
				strcat(sTemp,va("%c",text[x].f.character));
			}
			//
			// and print...
			//
			re.Font_DrawString(cl_conXOffset->integer + con.charWidth, v, sTemp,
				g_color_table[currentColor], iFontIndex, -1, fFontScale, cls.xadjust, cls.yadjust);

			v +=  iPixelHeightToAdvance;
		}
		else
		{
			for (x = 0 ; x < con.linewidth ; x++) {
				if ( text[x].f.character == ' ' ) {
					continue;
				}
				if ( text[x].f.color != currentColor ) {
					currentColor = text[x].f.color;
					re.SetColor( g_color_table[currentColor] );
				}
				if (!cl_conXOffset)
				{
					cl_conXOffset = Cvar_Get ("cl_conXOffset", "0", 0);
				}
				SCR_DrawSmallChar( (int)(cl_conXOffset->integer + (x+1)*con.charWidth), v, text[x].f.character );
			}

			v += con.charHeight;
		}
	}

	re.SetColor( NULL );

	if (cls.keyCatchers & (KEYCATCH_UI | KEYCATCH_CGAME) ) {
		return;
	}

	// draw the chat line
	if ( cls.keyCatchers & KEYCATCH_MESSAGE )
	{
		if (chat_playerNum != -1) {
			chattext = "Whisper:";
			skip = 9;
		}
		else if (chat_team) {
			chattext = "Say Team:";
			skip = 10;
		}
		else
		{
			chattext = "Say:";
			skip = 5;
		}

		SCR_DrawBigString(8, v, chattext, 1.0f);
		Field_BigDraw( &chatField, skip * BIGCHAR_WIDTH, v, qtrue );

		v += BIGCHAR_HEIGHT;
	}

}

/*
================
Con_DrawSolidConsole

Draws the console with the solid background
================
*/
void Con_DrawSolidConsole( float frac ) {
	unsigned char	currentColor;
	conChar_t		*text;
	int				i, x, y;
	int				rows;
	int				row;
	int				lines;
//	qhandle_t		conShader;
	char *vertext;

	struct tm		*newtime;
	char			am_pm[] = "AM";
	time_t			rawtime;
	char			ts[24];
	const int padding = (int)(0.5f + (con_scale && con_scale->value > 0.0f) ? 2 * con_scale->value : 2.0f);

	lines = (int) (cls.glconfig.vidHeight * frac);
	if (lines <= 0)
		return;

	if (lines > cls.glconfig.vidHeight )
		lines = cls.glconfig.vidHeight;

	// draw the background
	y = (int) (frac * SCREEN_HEIGHT - 2);
	if ( y < 1 ) {
		y = 0;
	}
	else {
		// draw the background at full opacity only if fullscreen
		if (frac < 1.0f)
		{
			vec4_t con_color;
			MAKERGBA(con_color, 1.0f, 1.0f, 1.0f, Com_Clamp(0.0f, 1.0f, con_opacity->value));
			re.SetColor(con_color);
		}
		else
		{
			re.SetColor(NULL);
		}
		SCR_DrawPic( 0, 0, SCREEN_WIDTH, (float) y, cls.consoleShader );
	}

	// draw the bottom bar and version number
	re.SetColor( g_color_table[ColorIndex_Extended(COLOR_JK2MV)] );
	SCR_DrawPic( 0, y, SCREEN_WIDTH, 2, cls.whiteShader );

	vertext = Q3_VERSION;
	i = (int)strlen(vertext);
	for (x = 0; x<i; x++) {
		SCR_DrawSmallChar(cls.glconfig.vidWidth - (i - x + 1) * con.charWidth,
			(lines - (con.charHeight * 2 + con.charHeight / 2)) + padding, vertext[x]);
	}

	// Draw time and date
	time(&rawtime);
	newtime = localtime(&rawtime);
	if (newtime->tm_hour >= 12) strcpy(am_pm, "PM");
	if (newtime->tm_hour > 12) newtime->tm_hour -= 12;
	if (newtime->tm_hour == 0) newtime->tm_hour = 12;
	Com_sprintf(ts, sizeof(ts), "%.19s %s ", asctime(newtime), am_pm);
	i = strlen(ts);

	for (x = 0; x<i; x++) {
		SCR_DrawSmallChar(cls.glconfig.vidWidth - (i - x) * con.charWidth, lines - (con.charHeight + con.charHeight / 2) + padding, ts[x]);
	}


	// draw the text
	con.vislines = lines;
	rows = (lines-con.charHeight)/con.charHeight;		// rows of text to draw

	y = lines - (con.charHeight*3);

	// draw from the bottom up
	if (con.display != con.current)
	{
		// draw arrows to show the buffer is backscrolled
		re.SetColor( g_color_table[ColorIndex(COLOR_RED)] );
		for (x=0 ; x<con.linewidth ; x+=4)
			SCR_DrawSmallChar( (x+1)*con.charWidth, y, '^' );
		y -= con.charHeight;
		rows--;
	}

	row = con.display;

	if ( con.x == 0 ) {
		row--;
	}

	currentColor = 7;
	re.SetColor( g_color_table[currentColor] );

	static int iFontIndex = re.RegisterFont("ocr_a");
	float fFontScale = 1.0f;
	int iPixelHeightToAdvance = con.charHeight;
	if (re.Language_IsAsian())
	{
		fFontScale = con.charWidth * 10.0f /
			re.Font_StrLenPixels("aaaaaaaaaa", iFontIndex, 1.0f, cls.xadjust, cls.yadjust);
		iPixelHeightToAdvance = 1.3 * re.Font_HeightPixels(iFontIndex, fFontScale, cls.xadjust, cls.yadjust);
	}

	for (i=0 ; i<rows ; i++, y -= iPixelHeightToAdvance, row--)
	{
		if (row < 0)
			break;
		if (con.current - row >= con.totallines) {
			// past scrollback wrap point
			continue;
		}

		text = con.text + (row % con.totallines)*con.rowwidth;
		if (!con_timestamps->integer)
			text += CON_TIMESTAMP_LEN;

		// asian language needs to use the new font system to print glyphs...
		//
		// (ignore colours since we're going to print the whole thing as one string)
		//
		if (re.Language_IsAsian())
		{
			// concat the text to be printed...
			//
			char sTemp[4096];	// ott
			sTemp[0] = '\0';
			for (x = 0 ; x < con.linewidth + 1 ; x++)
			{
				if ( text[x].f.color != currentColor ) {
					currentColor = text[x].f.color;
					strcat(sTemp,va("^%i", (currentColor > 7 ? COLOR_JK2MV_FALLBACK : currentColor) ));
				}
				strcat(sTemp,va("%c",text[x].f.character));
			}
			//
			// and print...
			//
			re.Font_DrawString(con.charWidth, y, sTemp, g_color_table[currentColor],
				iFontIndex, -1, fFontScale, cls.xadjust, cls.yadjust);
		}
		else
		{
			for (x = 0; x < con.linewidth + 1 ; x++) {
				if ( text[x].f.character == ' ' ) {
					continue;
				}

				if ( text[x].f.color != currentColor ) {
					currentColor = text[x].f.color;
					re.SetColor( g_color_table[currentColor] );
				}
				SCR_DrawSmallChar( (x+1)*con.charWidth, y, text[x].f.character );
			}
		}
	}

	// draw the input prompt, user text, and cursor if desired
	Con_DrawInput ();

	re.SetColor( NULL );
}



/*
==================
Con_DrawConsole
==================
*/
void Con_DrawConsole( void ) {
	// check for console width changes from a vid mode change
	Con_CheckResize ();

	// if disconnected, render console full screen
	if ( cls.state == CA_DISCONNECTED ) {
		if ( !( cls.keyCatchers & (KEYCATCH_UI | KEYCATCH_CGAME)) ) {
			Con_DrawSolidConsole( 1.0 );
			return;
		}
	}

	if ( con.displayFrac ) {
		Con_DrawSolidConsole( con.displayFrac );
	} else {
		// draw notify lines
		if ( cls.state == CA_ACTIVE ) {
			Con_DrawNotify ();
		}
	}
}

//================================================================

/*
==================
Con_RunConsole

Scroll it up or down
==================
*/
void Con_RunConsole (void) {
	// decide on the destination height of the console
	if ( cls.keyCatchers & KEYCATCH_CONSOLE )
		con.finalFrac = con_height->value;
	else
		con.finalFrac = 0;				// none visible

	if ( clc.demoplaying && (com_timescale->value < 1 || cl_paused->integer) ) {
		con.displayFrac = con.finalFrac;	// set console height instantly if timescale is very low or 0 in demo playback (happens when menu is up)
	} else {
		// scroll towards the destination height
		if (con.finalFrac < con.displayFrac)
		{
			con.displayFrac -= con_speed->value*(float)(cls.realFrametime*0.001);
			if (con.finalFrac > con.displayFrac)
				con.displayFrac = con.finalFrac;

		}
		else if (con.finalFrac > con.displayFrac)
		{
			con.displayFrac += con_speed->value*(float)(cls.realFrametime*0.001);
			if (con.finalFrac < con.displayFrac)
				con.displayFrac = con.finalFrac;
		}
	}
}


void Con_PageUp( int lines  ) {
	con.display -= lines;
	if ( con.current - con.display >= con.totallines ) {
		con.display = con.current - con.totallines + 1;
	}
}

void Con_PageDown( int lines  ) {
	con.display += lines;
	if (con.display > con.current) {
		con.display = con.current;
	}
}

void Con_Top( void ) {
	con.display = con.totallines;
	if ( con.current - con.display >= con.totallines ) {
		con.display = con.current - con.totallines + 1;
	}
}

void Con_Bottom( void ) {
	con.display = con.current;
}


void Con_Close( void ) {
	if ( !com_cl_running->integer ) {
		return;
	}
	Field_Clear( &kg.g_consoleField );
	Con_ClearNotify ();
	cls.keyCatchers &= ~KEYCATCH_CONSOLE;
	con.finalFrac = 0;				// none visible
	con.displayFrac = 0;
}
