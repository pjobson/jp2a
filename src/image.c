/*
 * Copyright (C) 2006 Christian Stigen Larsen, http://csl.sublevel3.org
 * Distributed under the GNU General Public License (GPL) v2 or later.
 *
 * Project homepage on http://jp2a.sf.net
 *
 * $Id$
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#undef HAVE_STDLIB_H
#endif

#include <stdio.h>

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "jpeglib.h"
#include "jp2a.h"
#include "options.h"

#define ROUND(x) (int) ( 0.5f + x )

typedef struct Image_ {
	int width;
	int height;
	float *pixel;
	int *yadds;
	float resize_y;
	float resize_x;
	int *lookup_resx;
} Image;

// Calculate width or height, but not both
void aspect_ratio(const int jpeg_width, const int jpeg_height) {

	// the 2.0f and 0.5f factors are used for text displays that (usually) have characters
	// that are taller than they are wide.

	#define CALC_WIDTH ROUND(2.0f * (float) height * (float) jpeg_width / (float) jpeg_height)
	#define CALC_HEIGHT ROUND(0.5f * (float) width * (float) jpeg_height / (float) jpeg_width)

	// calc width
	if ( auto_width && !auto_height ) {
		width = CALC_WIDTH;

		// adjust for too small dimensions	
		while ( width==0 ) {
			++height;
			aspect_ratio(jpeg_width, jpeg_height);
		}
		
		while ( termfit==TERM_FIT_AUTO && (width + use_border*2)>term_width ) {
			width = term_width - use_border*2;
			height = 0;
			auto_height = 1;
			auto_width = 0;
			aspect_ratio(jpeg_width, jpeg_height);
		}

	}

	// calc height
	if ( !auto_width && auto_height ) {
		height = CALC_HEIGHT;

		// adjust for too small dimensions
		while ( height==0 ) {
			++width;
			aspect_ratio(jpeg_width, jpeg_height);
		}
	}
}

void print_border(const int width) {
	#ifdef WIN32
	char *bord = (char*) malloc(width+3);
	#else
	char bord[width + 3];
	#endif

	memset(bord, '-', width+2);
	bord[0] = bord[width+1] = '+';
	bord[width+2] = 0;
	puts(bord);

	#ifdef WIN32
	free(bord);
	#endif
}

void print_image(const Image* const i, const int chars, FILE *f) {
	#ifdef WIN32
	char *line = (char*) malloc(i->width + 1);
	#else
	char line[i->width + 1];
	#endif

	line[i->width] = 0;

	int x, y;
	for ( y=0; y < i->height; ++y ) {

		for ( x=0; x < i->width; ++x ) {

			const float lum = i->pixel[x + (flipy? i->height - y - 1 : y) * i->width];
			const int pos = ROUND((float)chars * lum);

			line[flipx? i->width - x - 1 : x] = ascii_palette[invert? pos : chars - pos];
		}

		fprintf(f, !use_border? "%s\n" : "|%s|\n", line);
	}

	#ifdef WIN32
	free(line);
	#endif
}

void clear(Image* i) {
	memset(i->pixel, 0, i->width * i->height * sizeof(float));
	memset(i->yadds, 0, i->height * sizeof(int) );
}

void normalize(Image* i) {

	float *pixel = i->pixel;
	int x, y;

	for ( y=0; y < i->height; ++y, pixel += i->width ) {

		if ( i->yadds[y] <= 1 )
			continue;

		for ( x=0; x < i->width; ++x )
			pixel[x] /= i->yadds[y];
	}
}

void print_progress(const struct jpeg_decompress_struct* jpg) {
	#define BARLEN 56

	static char s[BARLEN];
	s[BARLEN-1] = 0;

 	float progress = (float) (jpg->output_scanline + 1.0f) / (float) jpg->output_height;
	int pos = ROUND( (float) (BARLEN-2) * progress );

	memset(s, '.', BARLEN-2);
	memset(s, '#', pos);

	fprintf(stderr, "Decompressing image [%s]\r", s);
	fflush(stderr);
}

void print_info(const struct jpeg_decompress_struct* jpg) {
	fprintf(stderr, "Source width: %d\n", jpg->output_width);
	fprintf(stderr, "Source height: %d\n", jpg->output_height);
	fprintf(stderr, "Source color components: %d\n", jpg->output_components);
	fprintf(stderr, "Output width: %d\n", width);
	fprintf(stderr, "Output height: %d\n", height);
	fprintf(stderr, "Output palette (%d chars): '%s'\n", (int)strlen(ascii_palette), ascii_palette);
}

void process_scanline(const struct jpeg_decompress_struct *jpg, const JSAMPLE* scanline, Image* i) {
	static int lasty = 0;
	const int y = ROUND( i->resize_y * (float) (jpg->output_scanline-1) );
	int x=0;

	// include all scanlines since last call

	float *pixel = &i->pixel[lasty * i->width];

	while ( lasty <= y ) {

		// we always assume 8-bits per channel
		// there is no test for this, so stuff will look incorrect with
		// other bit-depths.

		int w = i->width;

		float *pRED = RED;
		float *pGREEN = GREEN;
		float *pBLUE = BLUE;

		if ( jpg->out_color_components != 3 ) {
			// assume grayscale, only use first component
			pRED = GRAY;
			pGREEN = pBLUE = ZERO_CHN;
			w -= 2; // last two components
		}

		for ( x=0; x < w; ++x ) {
			const JSAMPLE *src = &scanline[i->lookup_resx[x]];
			const JSAMPLE *src_end = &scanline[i->lookup_resx[x+1]];

			int adds = 0;
			float v = 0.0f;
			while ( src <= src_end ) {
/*
				v += jpg->out_color_components==3 ?
					  RED[src[0]] + GREEN[src[1]] + BLUE[src[2]]
					: GRAY[src[0]];
*/
				v += pRED[src[0]] + pGREEN[src[1]] + pBLUE[src[2]];

				++adds;
				src += jpg->out_color_components;
			}

			pixel[x] += adds>1 ? v / (float) adds : v;
		}

		// add lost grayscale pixels
		for ( ; x < i->width; ++x ) {
			const JSAMPLE *src = &scanline[i->lookup_resx[x]];
			const JSAMPLE *src_end = &scanline[i->lookup_resx[x+1]];

			int adds = 0;
			float v = 0.0f;
			while ( src <= src_end ) {
				v += GRAY[src[0]];
				++adds;
				src += jpg->out_color_components;
			}

			pixel[x] += adds>1 ? v / (float) adds : v;
		}

		++i->yadds[lasty++];
		pixel += i->width;
	}

	lasty = y;
}

void free_image(Image* i) {
	if ( i->pixel ) free(i->pixel);
	if ( i->yadds ) free(i->yadds);
	if ( i->lookup_resx ) free(i->lookup_resx);
}

void malloc_image(Image* i) {
	i->pixel = NULL;
	i->yadds = NULL;
	i->lookup_resx = NULL;

	i->width = width;
	i->height = height;

	if ( (i->pixel = (float*) malloc(width * height * sizeof(float))) == NULL ) {
		fprintf(stderr, "Not enough memory for given output dimension\n");
		exit(1);
	}

	if ( (i->yadds = (int*) malloc(height * sizeof(int))) == NULL ) {
		fprintf(stderr, "Not enough memory for given output dimension (for yadds)\n");
		free_image(i);
		exit(1);
	}

	// we allocate one extra pixel for resx because of the src .. src_end stuff in process_scanline
	if ( (i->lookup_resx = (int*) malloc( (1 + width) * sizeof(int))) == NULL ) {
		fprintf(stderr, "Not enough memory for given output dimension (lookup_resx)\n");
		free_image(i);
		exit(1);
	}
}

void init_image(Image *i, const struct jpeg_decompress_struct *jpg) {
	i->resize_y = (float) (i->height - 1) / (float) (jpg->output_height - 1);
	i->resize_x = (float) (jpg->output_width - 1) / (float) (i->width );

	int dst_x;
	for ( dst_x=0; dst_x <= i->width; ++dst_x ) {
		i->lookup_resx[dst_x] = ROUND( (float) dst_x * i->resize_x );
		i->lookup_resx[dst_x] *= jpg->out_color_components;
	}
}

void decompress(FILE *fp, FILE *fout) {
	struct jpeg_error_mgr jerr;
	struct jpeg_decompress_struct jpg;

	jpg.err = jpeg_std_error(&jerr);
	jpeg_create_decompress(&jpg);
	jpeg_stdio_src(&jpg, fp);
	jpeg_read_header(&jpg, TRUE);
	jpeg_start_decompress(&jpg);

	if ( jpg.data_precision != 8 ) {
		fprintf(stderr,
			"Image has %d bits color channels, we only support 8-bit.\n",
			jpg.data_precision);
		exit(1);
	}

	int row_stride = jpg.output_width * jpg.output_components;

	JSAMPARRAY buffer = (*jpg.mem->alloc_sarray)
		((j_common_ptr) &jpg, JPOOL_IMAGE, row_stride, 1);

	aspect_ratio(jpg.output_width, jpg.output_height);

	Image image;
	malloc_image(&image);
	clear(&image);

	if ( verbose ) print_info(&jpg);

	init_image(&image, &jpg);

	while ( jpg.output_scanline < jpg.output_height ) {
		jpeg_read_scanlines(&jpg, buffer, 1);
		process_scanline(&jpg, buffer[0], &image);
		if ( verbose ) print_progress(&jpg);
	}

	if ( verbose ) {
		fprintf(stderr, "\n");
		fflush(stderr);
	}

	normalize(&image);

	if ( clearscr ) {
		fprintf(fout, "%c[2J", 27); // ansi code for clear
		fprintf(fout, "%c[0;0H", 27); // move to upper left
	}

	if ( html ) print_html_start(html_fontsize);
	if ( use_border ) print_border(image.width);

	print_image(&image, (int) strlen(ascii_palette) - 1, fout);

	if ( use_border ) print_border(image.width);
	if ( html ) print_html_end();

	free_image(&image);

	jpeg_finish_decompress(&jpg);
	jpeg_destroy_decompress(&jpg);
}
