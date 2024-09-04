/*
 * Copyright 2006-2016 Christian Stigen Larsen
 * Copyright 2020 Christoph Raitzig
 * Distributed under the GNU General Public License (GPL) v2.
 */

#include "options.h"
#include "round.h"

void aspect_ratio(const int jpeg_width, const int jpeg_height, const int switch_x_y) {

	// the 2.0f and 0.5f factors are used for text displays that (usually) have characters
	// that are taller than they are wide.

	const int stored_width = switch_x_y ? jpeg_height : jpeg_width;
	const int stored_height = switch_x_y ? jpeg_width : jpeg_height;

	#define CALC_WIDTH ROUND(2.0f * (float) height * (float) stored_width / (float) stored_height)
	#define CALC_HEIGHT ROUND(0.5f * (float) width * (float) stored_height / (float) stored_width)

	// calc width
	if ( auto_width && !auto_height ) {
		width = CALC_WIDTH;

		// adjust for too small dimensions	
		while ( width==0 ) {
			++height;
			aspect_ratio(stored_width, stored_height, 0);
		}
		
		while ( termfit==TERM_FIT_AUTO && (width + use_border*2)>term_width ) {
			width = term_width - use_border*2;
			height = 0;
			auto_height = 1;
			auto_width = 0;
			aspect_ratio(stored_width, stored_height, 0);
		}

	}

	// calc height
	if ( !auto_width && auto_height ) {
		height = CALC_HEIGHT;

		// adjust for too small dimensions
		if ( height==0 ) {
			height = ( stored_height == 1 )? 0 : 1;
		}
	}
}
