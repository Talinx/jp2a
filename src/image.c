/*
 * Copyright 2006-2016 Christian Stigen Larsen
 * Copyright 2020-2024 Christoph Raitzig
 * Distributed under the GNU General Public License (GPL) v2.
 */

#include "config.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include <stdio.h>
#include <limits.h>

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <stdint.h>

#include "jpeglib.h"
#include "png.h"
#include "webp/decode.h"
#include <libexif/exif-data.h>
#include <libexif/exif-loader.h>
#include <setjmp.h>

#include "aspect_ratio.h"
#include "image.h"
#include "jp2a.h"
#include "options.h"
#include "html.h"
#include <math.h>

#define ROUND(x) (int) ( 0.5f + x )

static char DIRECTIONAL_CHARS[4] = "=/|\\";

void print_margin_top(const Image *image, FILE *f) {
	if ( centery && !( html || xhtml ) ) {
		int total_margin = term_height - image->height;
		if ( use_border ) {
			total_margin -= 2;
		}
		if ( total_margin <= 0 )
			return;
		int margin_top = (int) floor(total_margin / 2);
		for ( size_t i = 0; i < margin_top; i++ ) {
			fprintf(f, "\n");
		}
	}
}

void print_margin_bottom(const Image *image, FILE *f) {
	if ( centery && !( html || xhtml ) ) {
		int total_margin = term_height - image->height;
		if ( use_border ) {
			total_margin -= 2;
		}
		if ( total_margin <= 0 )
			return;
		int margin_bottom = (int) ceil(total_margin / 2);
		for ( size_t i = 0; i < margin_bottom; i++ ) {
			fprintf(f, "\n");
		}
	}
}

void print_margin_start(const Image *image, FILE *f) {
	if ( centerx && !( html || xhtml ) ) {
		int total_margin = term_width - image->width;
		if ( use_border ) {
			total_margin -= 2;
		}
		if ( total_margin <= 0 )
			return;
		int margin_start = (int) floor(total_margin / 2);
		for ( size_t i = 0; i < margin_start; i++ ) {
			fprintf(f, " ");
		}
	}
}

void print_border(const int width) {
	#ifndef HAVE_MEMSET
	int n;
	#endif

	#ifdef WIN32
	char *bord = (char*) malloc(width+3);
	#else
	char bord[width + 3];
	#endif

	#ifdef HAVE_MEMSET
	memset(bord, '-', width+2);
	#else
	for ( n=0; n<width+2; ++n ) bord[n] = '-';
	#endif

	bord[0] = bord[width+1] = '+';
	bord[width+2] = 0;
	puts(bord);

	#ifdef WIN32
	free(bord);
	#endif
}

void print_image(Image *image, FILE *f) {
	if ( verbose ) {
		fprintf(stderr, "\n");
		fflush(stderr);
	}

	normalize(image);

	if ( clearscr ) {
		fprintf(f, "%c[2J", 27); // ansi code for clear
		fprintf(f, "%c[0;0H", 27); // move to upper left
	}

	if ( html && !html_rawoutput ) print_html_image_start(f);
	else if ( xhtml && !html_rawoutput ) print_xhtml_image_start(f);
	print_margin_top(image, f);
	if ( use_border ) {
		print_margin_start(image, f);
		print_border(image->width);
	}

	(!usecolors? print_image_no_colors : print_image_colors) (image, ascii_palette_length - 1, f);

	if ( use_border ) {
		print_margin_start(image, f);
		print_border(image->width);
	}
	print_margin_bottom(image, f);
	if ( html && !html_rawoutput ) print_html_image_end(f);
	else if ( xhtml && !html_rawoutput ) print_xhtml_image_end(f);
}

int get_pixel_index(const Image* const image, const int x, const int y) {
	float fx;
	float fy;
	switch (image->orientation) {
		case HORIZONTAL:
		case MIRROR_HORIZONTAL_ROTATE_90:
			fx = flipx ? image->width - x - 1 : x;
			fy = flipy ? image->height - y - 1 : y;
			break;
		case MIRROR_HORIZONTAL:
		case ROTATE_270:
			fx = flipx ? x : image->width - x - 1;
			fy = flipy ? image->height - y - 1 : y;
			break;
		case ROTATE_180:
		case MIRROR_HORIZONTAL_ROTATE_270:
			fx = flipx ? x : image->width - x - 1;
			fy = flipy ? y : image->height - y - 1;
			break;
		case MIRROR_VERTICAL:
		case ROTATE_90:
			fx = flipx ? image->width - x - 1 : x;
			fy = flipy ? y : image->height - y - 1;
			break;

	}
	const int cx = fx < 0 ? 0 : fx > image->width - 1 ? image->width - 1 : fx;
	const int cy = fy < 0 ? 0 : fy > image->height - 1 ? image->height - 1 : fy;
	if ( image->switch_x_y ) {
		return cy + cx * image->src_width;
	}
	return cx + cy * image->src_width;
}

typedef struct {
	float x;
	float y;
} vec2;

vec2 get_image_gradient(const Image* const image, const int x, const int y) {
	const float kernel_x[4] = {
		-1., 0.,
		 1., 0.
	};
	const float kernel_y[4] = {
		-1., 1.,
		 0., 0.
	};
	const float patch[4] = {
		image->pixel[get_pixel_index(image, x  , y  )],
		image->pixel[get_pixel_index(image, x+1, y  )],
		image->pixel[get_pixel_index(image, x  , y+1)],
		image->pixel[get_pixel_index(image, x+1, y+1)],
	};
	vec2 grad = {0., 0.};
	for( int i = 0; i < 4; ++i ) {
		grad.x += kernel_x[i] * patch[i];
		grad.y += kernel_y[i] * patch[i];
	}
	return grad;
}

float magnitude( const vec2 v ) {
	return sqrtf( v.x*v.x + v.y*v.y );
}

float direction( const vec2 v ) {
	return atan(v.y / v.x);
}

void print_image_colors(const Image* const image, const int chars, FILE* f) {

	for ( int y=0;  y < image->height; ++y ) {
		float prev_Y = -1.0;
		float prev_R = -1.0;
		float prev_G = -1.0;
		float prev_B = -1.0;
		float prev_A = -1.0;

		if ( !html && !xhtml && usecolors )
			fprintf(f, "\e[0m"); // reset colors

		print_margin_start(image, f);

		if ( use_border ) fprintf(f, "|");

		for ( int x=0; x < image->width; x += 1 ) {

			const int pixel_index = get_pixel_index(image, x, y);
			float Y = image->pixel[pixel_index];
			float R = image->red  [pixel_index];
			float G = image->green[pixel_index];
			float B = image->blue [pixel_index];
			float A = image->alpha[pixel_index];
			R *= A;
			G *= A;
			B *= A;
			const vec2 gradient = get_image_gradient(image, x, y);
			int pos = ROUND((float)chars * Y);

			if( edges_only && magnitude(gradient) < edge_threshold ) {
				pos = 0;
			}

			int i = invert? pos : chars - pos;
			i = ROUND((float)i * A);

			char ch[MB_LEN_MAX + 1];
#define PRINTF_FORMAT_TYPE "%s"

#if ASCII
			char* char_start = &ascii_palette[i];
			size_t char_len = 1;
#else
			char* char_start = &ascii_palette[ascii_palette_indizes[i]];
			size_t char_len = ascii_palette_lengths[i];
#endif
			if( magnitude(gradient) > edge_threshold ) {
				// scale the gradient direction in the range -2 to 2, then add .5 to offset direction bins to match character directions
				float direction_scaled = direction(gradient) / M_PI * 4. + .5;
				// use +4 and fmod to bring the direction into the range 0-4, then use (int) to get an index 0-3 into DIRECTIONAL_CHARS array
				char_start = &DIRECTIONAL_CHARS[ (int) fmod(direction_scaled + 4., 4.) ];
				char_len = 1;
			}

			memcpy(ch, char_start, char_len);
			ch[char_len] = '\0';

			if ( !html && !xhtml && Y == prev_Y && R == prev_R && G == prev_G && B == prev_B && A == prev_A) {
				fprintf(f, PRINTF_FORMAT_TYPE, ch);
				continue;
			}
			prev_Y = Y;
			prev_R = R;
			prev_G = G;
			prev_B = B;
			prev_A = A;

			const float min = 1.0f / 255.0f;

			if ( !html && !xhtml ) {
				if ( colorDepth==4 ) {
					const float t = 0.1f; // threshold
					const float i = 1.0f - t;

					int colr = 0;
					int highl = 0;

					// ANSI highlite, only use in grayscale
				        if ( Y>=0.95f && R<min && G<min && B<min ) highl = 1; // ANSI highlite

					if ( !convert_grayscale ) {
					     if ( R-t>G && R-t>B )            colr = 31; // red
					else if ( G-t>R && G-t>B )            colr = 32; // green
					else if ( R-t>B && G-t>B && R+G>i )   colr = 33; // yellow
					else if ( B-t>R && B-t>G && Y<0.95f ) colr = 34; // blue
					else if ( R-t>G && B-t>G && R+B>i )   colr = 35; // magenta
					else if ( G-t>R && B-t>R && B+G>i )   colr = 36; // cyan
					else if ( R+G+B>=3.0f*Y )             colr = 37; // white
					} else {
						if ( Y>=0.7f ) { highl=1; colr = 37; }
					}

					if ( !colr ) {
						if ( !highl ) fprintf(f, PRINTF_FORMAT_TYPE, ch);
						else          fprintf(f, "\e[1m" PRINTF_FORMAT_TYPE "\e[0m", ch);
					} else {
						if ( colorfill ) colr += 10;          // set to ANSI background color
						fprintf(f, "\e[%dm" PRINTF_FORMAT_TYPE, colr, ch); // ANSI color
					}
				} else
				if ( colorDepth==8 ) {
					int type = 38;                        // 38 = foreground; 48 = background
					if ( colorfill ) type += 10;          // set to background color
					if ( convert_grayscale || (R<min && G<min && B<min && Y>min) ) {
						if ( Y < 0.15 ) {
							if ( colorfill )
								fprintf(f, "\e[38;5;%dm", 0);
							fprintf(f, "\e[%d;5;0%dm" PRINTF_FORMAT_TYPE, type, 0, ch);
						} else
						if ( Y > 0.965 ) {
							if ( colorfill )
								fprintf(f, "\e[38;5;%dm", 244);
							fprintf(f, "\e[%d;5;%dm" PRINTF_FORMAT_TYPE, type, 231, ch);
						} else {
							if ( colorfill )
								fprintf(f, "\e[38;5;%dm", ROUND(24.0f*Y*0.5f) + 232);
							fprintf(f, "\e[%d;5;%dm" PRINTF_FORMAT_TYPE, type, ROUND(24.0f*Y) + 232, ch);
						}
					} else {
						if ( colorfill )
							fprintf(f, "\e[38;5;%dm", 16 + 36 * ROUND(5.0f*Y*R) + 6 * ROUND(5.0f*Y*G) + ROUND(5.0f*Y*B)); // foreground color
						fprintf(f, "\e[%d;5;%dm" PRINTF_FORMAT_TYPE, type, 16 + 36 * ROUND(5.0f*R) + 6 * ROUND(5.0f*G) + ROUND(5.0f*B), ch); // color
					}
				} else
				if ( colorDepth==24 ) {
					int type = 38;                        // 38 = foreground; 48 = background
					if ( colorfill ) type += 10;          // set to background color
					if ( convert_grayscale || (R<min && G<min && B<min && Y>min) ) {
						if ( colorfill )
							fprintf(f, "\x1b[38;2;%d;%d;%dm", ROUND(255.0f*Y*0.5f), ROUND(255.0f*Y*0.5f), ROUND(255.0f*Y*0.5f));
						fprintf(f, "\x1b[%d;2;%d;%d;%dm" PRINTF_FORMAT_TYPE, type, ROUND(255.0f*Y), ROUND(255.0f*Y), ROUND(255.0f*Y), ch);
					} else {
						if ( colorfill )
							fprintf(f, "\x1b[38;2;%d;%d;%dm", ROUND(255.0f*Y*R), ROUND(255.0f*Y*G), ROUND(255.0f*Y*B)); // foreground color
						fprintf(f, "\x1b[%d;2;%d;%d;%dm" PRINTF_FORMAT_TYPE, type, ROUND(255.0f*R), ROUND(255.0f*G), ROUND(255.0f*B), ch); // color
					}
				}

			} else
			if ( html ) {  // HTML output
			
				// either --grayscale is specified (convert_grayscale)
				// or we can see that the image is inherently a grayscale image	
				if ( convert_grayscale || (R<min && G<min && B<min && Y>min) ) {
					// Grayscale image
					if ( colorfill )
						print_html_char(f, ch,
							ROUND(255.0f*Y*0.5f), ROUND(255.0f*Y*0.5f), ROUND(255.0f*Y*0.5f),
							ROUND(255.0f*Y),      ROUND(255.0f*Y),      ROUND(255.0f*Y));
					else
						print_html_char(f, ch,
							ROUND(255.0f*Y), ROUND(255.0f*Y), ROUND(255.0f*Y),
							255, 255, 255);
				} else {
					if ( colorfill )
						print_html_char(f, ch,
							ROUND(255.0f*Y*R), ROUND(255.0f*Y*G), ROUND(255.0f*Y*B),
							ROUND(255.0f*R),   ROUND(255.0f*G),   ROUND(255.0f*B));
					else
						print_html_char(f, ch,
							ROUND(255.0f*R), ROUND(255.0f*G), ROUND(255.0f*B),
							255, 255, 255);
				}
			} else 
			if ( xhtml ) {  // XHTML output
			
				// either --grayscale is specified (convert_grayscale)
				// or we can see that the image is inherently a grayscale image	
				if ( convert_grayscale || (R<min && G<min && B<min && Y>min) ) {
					// Grayscale image
					if ( colorfill )
						print_xhtml_char(f, ch,
							ROUND(255.0f*Y*0.5f), ROUND(255.0f*Y*0.5f), ROUND(255.0f*Y*0.5f),
							ROUND(255.0f*Y),      ROUND(255.0f*Y),      ROUND(255.0f*Y));
					else
						print_xhtml_char(f, ch,
							ROUND(255.0f*Y), ROUND(255.0f*Y), ROUND(255.0f*Y),
							255, 255, 255);
				} else {
					if ( colorfill )
						print_xhtml_char(f, ch,
							ROUND(255.0f*Y*R), ROUND(255.0f*Y*G), ROUND(255.0f*Y*B),
							ROUND(255.0f*R),   ROUND(255.0f*G),   ROUND(255.0f*B));
					else
						print_xhtml_char(f, ch,
							ROUND(255.0f*R), ROUND(255.0f*G), ROUND(255.0f*B),
							255, 255, 255);
				}
			}
		}

		if ( usecolors && !html && !xhtml )
			fprintf(f, "\e[0m");

		if ( use_border )
			fputc('|', f);

		if ( html )
			print_html_newline(f);
		else
		if ( xhtml )
			print_xhtml_newline(f);
		else
			fputc('\n', f);
	}
}

void print_image_no_colors(const Image* const image, const int chars, FILE *f) {
#if ASCII
	#ifdef WIN32
	char *line = (char*) malloc(image->width + 1);
	#else
	char line[image->width + 1];
	#endif
	line[image->width] = 0;
#else
	#ifdef WIN32
	char *line = (char*) malloc(image->width * MB_LEN_MAX + 1);
	#else
	char line[image->width * MB_LEN_MAX + 1];
	#endif
	line[image->width * MB_LEN_MAX] = 0;
#endif

	for ( int y=0; y < image->height; ++y ) {
		print_margin_start(image, f);

		int curLinePos = 0;
		for ( int x=0; x < image->width; ++x ) {

			const int pixel_index = get_pixel_index(image, x, y);
			const float lum = image->pixel[pixel_index];
			const float opacity = image->alpha[pixel_index];
			const vec2 gradient = get_image_gradient(image, x, y);
			int pos = ROUND((float)chars * lum);

			if( edges_only && magnitude(gradient) < edge_threshold ) {
				pos = 0;
			}

			int i = invert? pos : chars - pos;
			i = ROUND((float)i * opacity);

			char* char_dest = &line[curLinePos];
#if ASCII
			char* char_start = &ascii_palette[i];
			size_t char_len = 1;
#else
			char* char_start = &ascii_palette[ascii_palette_indizes[i]];
			size_t char_len = ascii_palette_lengths[i];
#endif
			if( magnitude(gradient) > edge_threshold ) {
				// scale the gradient direction in the range -2 to 2, then add .5 to offset direction bins to match character directions
				float direction_scaled = direction(gradient) / M_PI * 4. + .5;
				// use +4 and fmod to bring the direction into the range 0-4, then use (int) to get an index 0-3 into DIRECTIONAL_CHARS array
				char_start = &DIRECTIONAL_CHARS[ (int) fmod(direction_scaled + 4., 4.) ];
				char_len = 1;
			}
			memcpy(char_dest, char_start, char_len);
			curLinePos += char_len;
			line[curLinePos] = '\0';
		}
		fprintf(f, !use_border? "%s\n" : "|%s|\n", line);
	}

#ifdef WIN32
	free(line);
#endif
}

void clear(Image* i) {
	memset(i->yadds, 0, i->src_height * sizeof(int) );
	memset(i->pixel, 0, i->width * i->height * sizeof(float));
	for ( int j = 0; j < i->width * i->height; ++j ) {
		i->alpha[j] = 1.0;
	}
	memset(i->lookup_resx, 0, (1 + i->src_width) * sizeof(int) );

	if ( usecolors ) {
		memset(i->red,   0, i->width * i->height * sizeof(float));
		memset(i->green, 0, i->width * i->height * sizeof(float));
		memset(i->blue,  0, i->width * i->height * sizeof(float));
	}
}

void normalize(Image* i) {

	float *pixel = i->pixel;
	float *red   = i->red;
	float *green = i->green;
	float *blue  = i->blue;

	int x, y;

	for ( y=0; y < i->src_height; ++y ) {

		if ( i->yadds[y] > 1 ) {

			for ( x=0; x < i->src_width; ++x ) {
				pixel[x] /= i->yadds[y];

				if ( usecolors ) {
					red  [x] /= i->yadds[y];
					green[x] /= i->yadds[y];
					blue [x] /= i->yadds[y];
				}
			}
		}

		pixel += i->src_width;

		if ( usecolors ) {
			red   += i->src_width;
			green += i->src_width;
			blue  += i->src_width;
		}
	}
}

void print_progress(float progress) {
	int pos;
	#define BARLEN 56

	static char s[BARLEN];
	s[BARLEN-1] = 0;

	pos = ROUND( (float) (BARLEN-2) * progress );

	memset(s, '.', BARLEN-2);
	memset(s, '#', pos);

	fprintf(stderr, "Decompressing image [%s]\r", s);
	fflush(stderr);
}

void print_info_jpeg(const struct jpeg_decompress_struct* jpg, const Orientation orientation) {
	fprintf(stderr, "Source width: %d\n", jpg->output_width);
	fprintf(stderr, "Source height: %d\n", jpg->output_height);
	fprintf(stderr, "Source color components: %d\n", jpg->output_components);
	switch ( orientation ) {
		case HORIZONTAL:
			fprintf(stderr, "Orientation: 1 (Horizontal/normal)\n");
			break;
		case MIRROR_HORIZONTAL:
			fprintf(stderr, "Orientation: 2 (Mirror horizontal)\n");
			break;
		case ROTATE_180:
			fprintf(stderr, "Orientation: 3 (Rotate 180)\n");
			break;
		case MIRROR_VERTICAL:
			fprintf(stderr, "Orientation: 4 (Mirror vertical)\n");
			break;
		case MIRROR_HORIZONTAL_ROTATE_90:
			fprintf(stderr, "Orientation: 5 (Mirror horizontal and rotate 90)\n");
			break;
		case ROTATE_270:
			fprintf(stderr, "Orientation: 6 (Rotate 270)\n");
			break;
		case MIRROR_HORIZONTAL_ROTATE_270:
			fprintf(stderr, "Orientation: 7 (Mirror horizontal and rotate 270)\n");
			break;
		case ROTATE_90:
			fprintf(stderr, "Orientation: 8 (Rotate 90)\n");
			break;
	}
	fprintf(stderr, "Output width: %d\n", width);
	fprintf(stderr, "Output height: %d\n", height);
	fprintf(stderr, "Output palette (%d chars): '%s'\n", ascii_palette_length, ascii_palette);
}

void print_info_png(const png_structp png_ptr, const png_infop info_ptr) {
	fprintf(stderr, "Source width: %d\n", png_get_image_width(png_ptr, info_ptr));
	fprintf(stderr, "Source height: %d\n", png_get_image_height(png_ptr, info_ptr));
	fprintf(stderr, "Source channel count: %d ", png_get_channels(png_ptr, info_ptr));
	switch ( png_get_color_type(png_ptr, info_ptr) ) {
		case PNG_COLOR_TYPE_GRAY:
			fprintf(stderr, "(G)\n");
			break;
		case PNG_COLOR_TYPE_GRAY_ALPHA:
			fprintf(stderr, "(GA)\n");
			break;
		case PNG_COLOR_TYPE_PALETTE:
			fprintf(stderr, "(Palette)\n");
			break;
		case PNG_COLOR_TYPE_RGB:
			fprintf(stderr, "(RGB)\n");
			break;
		case PNG_COLOR_TYPE_RGB_ALPHA:
			fprintf(stderr, "(RGBA)\n");
			break;
	}
	switch ( png_get_interlace_type(png_ptr, info_ptr) ) {
		case PNG_INTERLACE_NONE:
			fprintf(stderr, "Source interlacing: None\n");
			break;
		case PNG_INTERLACE_ADAM7:
			fprintf(stderr, "Source interlacing: Adam7\n");
			break;
	}
	fprintf(stderr, "Source bit depth: %d\n", png_get_bit_depth(png_ptr, info_ptr));
	fprintf(stderr, "Output width: %d\n", width);
	fprintf(stderr, "Output height: %d\n", height);
	fprintf(stderr, "Output palette (%d chars): '%s'\n", ascii_palette_length, ascii_palette);
}

void print_info_webp(WebPDecoderConfig* config) {
	fprintf(stderr, "Source width: %d\n", config->input.width);
	fprintf(stderr, "Source height: %d\n", config->input.height);
	if ( config->input.has_alpha ) {
		fprintf(stderr, "Has alpha channel: Yes\n");
	} else {
		fprintf(stderr, "Has alpha channel: No\n");
	}
	if ( config->input.has_animation ) {
		fprintf(stderr, "Has animation: Yes\n");
	} else {
		fprintf(stderr, "Has animation: No\n");
	}
	fprintf(stderr, "Output width: %d\n", width);
	fprintf(stderr, "Output height: %d\n", height);
	fprintf(stderr, "Output palette (%d chars): '%s'\n", ascii_palette_length, ascii_palette);
}

void process_scanline_jpeg(const struct jpeg_decompress_struct *jpg, const JSAMPLE* scanline, Image* i) {
	static int lasty = 0;
	const int y = ROUND( i->resize_y * (float) (jpg->output_scanline-1) );

	// include all scanlines since last call

	float *pixel, *red, *green, *blue, *alpha;

	pixel  = &i->pixel[lasty * i->src_width];
	red = green = blue = NULL;
	alpha  = &i->alpha[lasty * i->src_width];

	if ( usecolors ) {
		int offset = lasty * i->src_width;
		red   = &i->red  [offset];
		green = &i->green[offset];
		blue  = &i->blue [offset];
	}

	while ( lasty <= y ) {

		const int components = jpg->out_color_components;

		int x;
		for ( x=0; x < i->src_width; ++x ) {
			const JSAMPLE *src     = &scanline[i->lookup_resx[x] * jpg->out_color_components];
			const JSAMPLE *src_end = &scanline[i->lookup_resx[x+1] * jpg->out_color_components];

			int adds = 0;

			float v, r, g, b;
			v = r = g = b = 0.0f;

			while ( src <= src_end ) {

				if ( components != 3 )
					v += GRAY[src[0]];
				else {
					v += RED[src[0]] + GREEN[src[1]] + BLUE[src[2]];

					if ( usecolors ) {
						r += (float) src[0]/255.0f;
						g += (float) src[1]/255.0f;
						b += (float) src[2]/255.0f;
					}
				}

				++adds;
				src += components;
			}

			pixel[x] += adds>1 ? v / (float) adds : v;
			alpha[x] = 1.0;

			if ( usecolors ) {
				red  [x] += adds>1 ? r / (float) adds : r;
				green[x] += adds>1 ? g / (float) adds : g;
				blue [x] += adds>1 ? b / (float) adds : b;
			}
		}

		++i->yadds[lasty++];

		pixel += i->src_width;
		alpha += i->src_width;

		if ( usecolors ) {
			red   += i->src_width;
			green += i->src_width;
			blue  += i->src_width;
		}
	}

	lasty = y;
}

void process_scanline_png(const png_bytep row, const int current_y, const int color_components, Image* i) {
	static int lasty = 0;
	const int y = ROUND( i->resize_y * (float) current_y );

	// include all scanlines since last call

	float *pixel, *red, *green, *blue, *alpha;

	pixel  = &i->pixel[lasty * i->src_width];
	red = green = blue = NULL;
	alpha = &i->alpha[lasty * i->src_width];

	if ( usecolors ) {
		int offset = lasty * i->src_width;
		red   = &i->red  [offset];
		green = &i->green[offset];
		blue  = &i->blue [offset];
	}

	while ( lasty <= y ) {

		int x;
		for ( x=0; x < i->src_width; ++x ) {

			int adds = 0;

			float v, r, g, b, a;
			v = r = g = b = a = 0.0f;

			for ( int j = i->lookup_resx[x] ; j < i->lookup_resx[x+1]; ++j ) {
				png_byte* src_pixel = &(row[j * color_components]);

				if ( color_components < 3 ) {
					v += GRAY[src_pixel[0]];
					if ( color_components == 2 )
						a += ALPHA[src_pixel[1]];
				} else {
					v += RED[src_pixel[0]] + GREEN[src_pixel[1]] + BLUE[src_pixel[2]];

					if ( usecolors ) {
						r += (float) src_pixel[0]/255.0f;
						g += (float) src_pixel[1]/255.0f;
						b += (float) src_pixel[2]/255.0f;
					}
					if ( color_components == 4 )
						a += ALPHA[src_pixel[3]];
				}

				++adds;
			}

			pixel[x] += adds>1 ? v / (float) adds : v;

			if ( usecolors ) {
				red  [x] += adds>1 ? r / (float) adds : r;
				green[x] += adds>1 ? g / (float) adds : g;
				blue [x] += adds>1 ? b / (float) adds : b;
			}
			if ( color_components == 1 || color_components == 3 ) {
				alpha[x] = 1.0;
			} else {
				if ( a == 0.0 )
					alpha[x] = 0.0;
				else
					alpha[x] = adds>1 ? a / (float) adds : a;
			}
		}

		++i->yadds[lasty++];

		pixel += i->src_width;
		alpha += i->src_width;

		if ( usecolors ) {
			red   += i->src_width;
			green += i->src_width;
			blue  += i->src_width;
		}
	}

	lasty = y;
}

void free_image(Image* i) {
	if ( i->pixel ) free(i->pixel);
	if ( i->alpha ) free(i->alpha);
	if ( i->red ) free(i->red);
	if ( i->green ) free(i->green);
	if ( i->blue ) free(i->blue);
	if ( i->yadds ) free(i->yadds);
	if ( i->lookup_resx ) free(i->lookup_resx);
}

void malloc_image(Image* i, int switch_x_y) {
	i->orientation = HORIZONTAL;
	i->switch_x_y = switch_x_y;
	i->pixel = i->red = i->green = i->blue = i->alpha = NULL;
	i->yadds = NULL;
	i->lookup_resx = NULL;

	i->width = width;
	i->height = height;
	i->src_width = switch_x_y ? height : width;
	i->src_height = switch_x_y ? width : height;

	i->yadds = (int*) malloc(i->src_height * sizeof(int));
	i->pixel = (float*) malloc(width*height*sizeof(float));
	i->alpha = (float*) malloc(width*height*sizeof(float));

	if ( usecolors ) {
		i->red   = (float*) malloc(width*height*sizeof(float));
		i->green = (float*) malloc(width*height*sizeof(float));
		i->blue  = (float*) malloc(width*height*sizeof(float));
	}

	// we allocate one extra pixel for resx because of the src .. src_end stuff in process_scanline_jpeg and the equivalent in for PNG
	i->lookup_resx = (int*) malloc( (1 + i->src_width) * sizeof(int));

	if ( !(i->pixel && i->alpha && i->yadds && i->lookup_resx) ||
	     (usecolors && !(i->red && i->green && i->blue)) )
	{
		fprintf(stderr, "Not enough memory for given output dimension\n");
		free_image(i);
		exit(1);
	}
}

void init_image(Image *i, int src_width, int src_height) {
	int dst_x;

	if ( src_height > 1 )
		i->resize_y = (float) (i->src_height - 1) / (float) (src_height - 1);
	else
		i->resize_y = 1;
	i->resize_x = (float) (src_width - 1) / (float) (i->src_width );


	for ( dst_x=0; dst_x <= i->src_width; ++dst_x ) {
		i->lookup_resx[dst_x] = ROUND( (float) dst_x * i->resize_x );
	}
}


Orientation get_orientation(FILE *imageFP) {
	char orientationTag[13] = "Top-left";  // default to horizontal/normal

	size_t size;
	unsigned char data[1024];
	ExifData *edata;
	ExifLoader *loader;
	loader = exif_loader_new();
	while ( 1 ) {
		size = fread(data, 1, sizeof(data), imageFP);
		if (size <= 0) {
			break;
		}
		if (!exif_loader_write(loader, data, size)) {
			break;
		}
	}
	rewind(imageFP);
	edata = exif_loader_get_data(loader);
	exif_loader_unref(loader);
	if ( edata ) {
		ExifEntry *entry = exif_content_get_entry(edata->ifd[EXIF_IFD_0], EXIF_TAG_ORIENTATION);
		if ( entry ) {
			exif_entry_get_value(entry, orientationTag, sizeof(orientationTag));
		}
		exif_data_unref(edata);
	}

	if ( strcmp(orientationTag, "Top-left" ) == 0) {
		return HORIZONTAL;
	} else if ( strcmp(orientationTag, "Top-right" ) == 0) {
		return MIRROR_HORIZONTAL;
	} else if ( strcmp(orientationTag, "Bottom-right" ) == 0) {
		return ROTATE_180;
	} else if ( strcmp(orientationTag, "Bottom-left" ) == 0) {
		return MIRROR_VERTICAL;
	} else if ( strcmp(orientationTag, "Left-top" ) == 0) {
		return MIRROR_HORIZONTAL_ROTATE_90;
	} else if ( strcmp(orientationTag, "Right-top" ) == 0) {
		return ROTATE_270;
	} else if ( strcmp(orientationTag, "Right-bottom" ) == 0) {
		return MIRROR_HORIZONTAL_ROTATE_270;
	} else if ( strcmp(orientationTag, "Left-bottom" ) == 0) {
		return ROTATE_90;
	}
	return HORIZONTAL;
}

void decompress_jpeg(FILE *fp, FILE *fout, error_collector *errors) {
	if ( errors->jpeg_status ) {
		print_errors(errors);
		return;
	}

	Orientation orientation = get_orientation(fp);
	int switch_x_y = 0;
	if (
			orientation == MIRROR_HORIZONTAL_ROTATE_90 ||
			orientation == ROTATE_270 ||
			orientation == MIRROR_HORIZONTAL_ROTATE_270 ||
			orientation == ROTATE_90
	) {
		switch_x_y = 1;
	}

	int row_stride;
	my_jpeg_error_mgr jerr;
	struct jpeg_decompress_struct jpg;
	JSAMPARRAY buffer;
	Image image;

	jpg.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = jpeg_error_exit;
	if ( setjmp(jerr.setjmp_buffer) ) {
		errors->jpeg_error = &jerr;
		errors->jpeg_status = 1;
		jpeg_destroy_decompress(&jpg);
		rewind(fp);
		decompress_webp(fp, fout, errors);
		return;
	}
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

	row_stride = jpg.output_width * jpg.output_components;

	buffer = (*jpg.mem->alloc_sarray)((j_common_ptr) &jpg, JPOOL_IMAGE, row_stride, 1);

	aspect_ratio(jpg.output_width, jpg.output_height, switch_x_y);

	if ( verbose ) print_info_jpeg(&jpg, orientation);

	if ( height != 0 && width != 0 ) {
		malloc_image(&image, switch_x_y);
		clear(&image);

		init_image(&image, jpg.output_width, jpg.output_height);
		image.orientation = orientation;

		while ( jpg.output_scanline < jpg.output_height ) {
			jpeg_read_scanlines(&jpg, buffer, 1);
			process_scanline_jpeg(&jpg, buffer[0], &image);
			if ( verbose ) print_progress((float) (jpg.output_scanline + 1.0f) / (float) jpg.output_height);
		}

		print_image(&image, fout);

		free_image(&image);
		jpeg_finish_decompress(&jpg);
	}

	jpeg_destroy_decompress(&jpg);
}

void jpeg_error_exit(j_common_ptr jerr) {
	my_jpeg_error_ptr myerr = (my_jpeg_error_ptr)jerr->err;
	longjmp(myerr->setjmp_buffer, 1);
}

void decompress_png(FILE *fp, FILE *fout, error_collector *errors) {
	if ( errors->png_status ) {
		print_errors(errors);
		return;
	}
	Image image;
	int number_bytes_to_check = 8;
	char header[number_bytes_to_check];
	if ( fread(&header, 1, number_bytes_to_check, fp) != number_bytes_to_check || png_sig_cmp(header, 0, number_bytes_to_check) ) {
		errors->png_error_msg = "Not a PNG file: Wrong signature";
		errors->png_status = 1;
		rewind(fp);
		decompress_jpeg(fp, fout, errors);
		return;
	}
	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if ( !png_ptr ) {
		fprintf(stderr, "Unable to setup PNG reading, skipping file.\n");
		return;
	}
	png_infop info_ptr = png_create_info_struct(png_ptr);
	if ( !info_ptr ) {
		fprintf(stderr, "Unable to setup PNG reading, skipping file.\n");
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		return;
	}
	if ( setjmp(png_jmpbuf(png_ptr)) ) {
		errors->png_error_msg = "Not a valid PNG file.";
		errors->png_status = 1;
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		rewind(fp);
		decompress_webp(fp, fout, errors);
		return;
	}
	png_init_io(png_ptr, fp);
	png_set_sig_bytes(png_ptr, number_bytes_to_check);
	png_read_info(png_ptr, info_ptr);

	int png_width = png_get_image_width(png_ptr, info_ptr);
	int png_height = png_get_image_height(png_ptr, info_ptr);

	aspect_ratio(png_width, png_height, 0);

	if ( verbose ) print_info_png(png_ptr, info_ptr);

	if ( height != 0 && width != 0 ) {
		malloc_image(&image, 0);
		clear(&image);


		// peform transformations (after printing the info):
		if ( png_get_color_type(png_ptr, info_ptr) == PNG_COLOR_TYPE_PALETTE )
			png_set_palette_to_rgb(png_ptr);
		if ( png_get_bit_depth(png_ptr, info_ptr) < 8 ) {
			if ( png_get_channels(png_ptr, info_ptr) < 3 ) {
				png_set_expand_gray_1_2_4_to_8(png_ptr);
			} else {
				png_set_expand(png_ptr);
			}
		}
		if ( png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS) )
			png_set_tRNS_to_alpha(png_ptr);
		if ( png_get_bit_depth(png_ptr, info_ptr) == 16 )
			png_set_strip_16(png_ptr);
		int number_of_passes = png_set_interlace_handling(png_ptr);
		png_read_update_info(png_ptr, info_ptr);

		init_image(&image, png_width, png_height);

		if ( verbose )
			print_progress(0.0);
		if ( png_get_interlace_type(png_ptr, info_ptr) == PNG_INTERLACE_NONE ) {
			png_bytep row_pointer = png_malloc(png_ptr, png_width * png_get_channels(png_ptr, info_ptr) * 1);
			for ( int y = 0; y < png_height; y++ ) {
				png_read_row(png_ptr, row_pointer, NULL);
				process_scanline_png(row_pointer, y, png_get_channels(png_ptr, info_ptr), &image);
				if ( verbose )
					print_progress((float) y/png_height);
			}
			png_free(png_ptr, row_pointer);
		} else {
			png_bytepp row_pointers = png_malloc(png_ptr, png_height * sizeof(png_bytep));
			for ( int i = 0; i < png_height; ++i )
				row_pointers[i] = NULL;
			for ( int i = 0; i < png_height; ++i )
				row_pointers[i] = png_malloc(png_ptr, png_width * png_get_channels(png_ptr, info_ptr) * 1);
			// png_read_image would do the same thing, but progress could not be displayed
			for ( int passes = 0; passes < number_of_passes; ++passes ) {
				png_read_rows(png_ptr, row_pointers, NULL, png_height);
				if ( verbose )
					print_progress((float) (passes + 1)/number_of_passes);
			}
			for ( int y = 0; y < png_height; y++ ) {
				process_scanline_png(row_pointers[y], y, png_get_channels(png_ptr, info_ptr), &image);
			}
			for ( int i = 0; i < png_height; ++i )
				png_free(png_ptr, row_pointers[i]);
			png_free(png_ptr, row_pointers);
		}
		if ( verbose )
			print_progress(1.0);
		png_read_end(png_ptr, NULL);

		print_image(&image, fout);

		free_image(&image);
	}

	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
}

webp_data* get_webp_data(FILE *fp) {
	webp_data* data_struct = malloc(sizeof(webp_data));
	uint8_t buffer[1024];
	size_t bytes_read;
	size_t data_size = 0;
	size_t data_allocated_size = 16384;
	uint8_t* data = malloc(data_allocated_size);
	
	while ( 1 ) {
		bytes_read = fread(buffer, 1, sizeof(buffer), fp);
		if ( bytes_read <= 0 ) {
			break;
		}
		size_t new_data_size = data_size + bytes_read;
		if ( new_data_size > data_allocated_size ) {
			data_allocated_size *= 2;
			data = realloc(data, data_allocated_size);
		}
		uint8_t* dest = data + data_size;
		memcpy(dest, buffer, bytes_read);
		data_size = new_data_size;
	}

	data_struct->data = data;
	data_struct->size = data_size;

	return data_struct;
}

void free_webp_data(webp_data* data) {
	free(data->data);
	free(data);
}

void decompress_webp(FILE *fp, FILE *fout, error_collector *errors) {
	if ( errors->webp_status ) {
		print_errors(errors);
		return;
	}

	Orientation orientation = get_orientation(fp);
	int switch_x_y = 0;
	if (
			orientation == MIRROR_HORIZONTAL_ROTATE_90 ||
			orientation == ROTATE_270 ||
			orientation == MIRROR_HORIZONTAL_ROTATE_270 ||
			orientation == ROTATE_90
	) {
		switch_x_y = 1;
	}

	Image image;

	WebPDecoderConfig config;
	WebPInitDecoderConfig(&config);

	webp_data* img_data = get_webp_data(fp);

	if ( WebPGetFeatures(img_data->data, img_data->size, &config.input) != VP8_STATUS_OK ) {
		errors->webp_error_msg = "Unable to determine WebP features, possibly not a WebP";
		errors->webp_status = 1;
		free_webp_data(img_data);
		rewind(fp);
		decompress_png(fp, fout, errors);
		return;
	}

	aspect_ratio(config.input.width, config.input.height, switch_x_y);
	
	if ( verbose ) print_info_webp(&config);

	if ( height != 0 && width != 0 ) {
		malloc_image(&image, switch_x_y);
		clear(&image);

		init_image(&image, image.src_width, image.src_height);
		image.orientation = orientation;
		if ( image.src_width != config.input.width || image.src_height != config.input.height ) {
			// scale the image using the webp library instead of by jp2a
			// this should provide smoother and faster scaling
			config.options.use_scaling = 1;
			config.options.scaled_width = image.src_width;
			config.options.scaled_height = image.src_height;
		}
		config.output.colorspace = MODE_RGBA;
		image.resize_x = 1.0f;
		image.resize_y = 1.0f;

		if ( WebPDecode(img_data->data, img_data->size, &config) != VP8_STATUS_OK) {

			errors->webp_error_msg = "Error decoding WebP image";
			errors->webp_status = 1;
			free_image(&image);
			free_webp_data(img_data);
			rewind(fp);
			decompress_png(fp, fout, errors);
			return;
		}

		WebPRGBABuffer* u = (WebPRGBABuffer*) &config.output.u;
		uint8_t* rgba = u->rgba;

		for ( size_t i = 0; i < image.width * image.height; i++ ) {
			if ( usecolors ) {
				image.red[i] = rgba[i * 4] / 255.0f;
				image.green[i] = rgba[i * 4 + 1] / 255.0f;
				image.blue[i] = rgba[i * 4 + 2] / 255.0f;
			}
			image.pixel[i] = RED[rgba[i * 4]] + GREEN[rgba[i * 4 + 1]] + BLUE[rgba[i * 4 + 2]];
			image.alpha[i] = rgba[i * 4 + 3] / 255.0f;
		}
		for ( size_t i = 0; i < image.src_height; i++ ) {
			image.yadds[i] = 1;
		}

		print_image(&image, fout);

		free_image(&image);
		WebPFreeDecBuffer(&config.output);
	}

	free_webp_data(img_data);
}

void print_errors(error_collector *errors) {
	if ( errors->jpeg_status ) {
		my_jpeg_error_mgr *jerr = errors->jpeg_error;
		struct jpeg_common_struct cinfo;
		cinfo.err = &jerr->pub;
		(jerr->pub.output_message) ((j_common_ptr)&cinfo);
	}
	if ( errors->png_status ) {
		fprintf(stderr, "%s\n", errors->png_error_msg);
	}
	if ( errors->webp_status ) {
		fprintf(stderr, "%s\n", errors->webp_error_msg);
	}
}
