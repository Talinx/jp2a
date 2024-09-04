/*! \file
 * \noop Copyright 2006-2016 Christian Stigen Larsen
 * \noop Copyright 2020 Christoph Raitzig
 * 
 * \brief Function for calculating the output width or height.
 *
 * \author Christian Stigen Larsen
 * \author Christoph Raitzig
 * \copyright Distributed under the GNU General Public License (GPL) v2.
 */

#ifndef INC_JP2A_ASPECT_RATIO_H
#define INC_JP2A_ASPECT_RATIO_H

/*!
 * \brief Calculate the output width or height, but not both.
 *
 * \param jpeg_width,jpeg_height dimensions of the input image
 * \param switch_x_y whether to switch x and y dimensions, this is the case when the stored pixels differ from the displayed pixels due to a rotation
 */
void aspect_ratio(const int jpeg_width, const int jpeg_height, const int switch_x_y);

#endif
