/*	quantizer.h

	See quantizer.c for more information
*/

#define schar	signed char
#define sshort	signed short
#define slong	signed long
#define uchar	unsigned char
#define ushort	unsigned short
#define ulong	unsigned long

int dl1quant(uchar *inbuf, int width, int height,
			int quant_to, uchar userpal[3][256]);

int dl3quant(uchar *inbuf, int width, int height,
			int quant_to, uchar userpal[3][256]);

int dl3floste(uchar *inbuf, uchar *outbuf, int width, int height,
			int quant_to, int dither, uchar userpal[3][256]);
