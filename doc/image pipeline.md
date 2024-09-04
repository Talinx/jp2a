# Image pipeline

This page outlines the image pipeline of jp2a from the image being read to characters displayed.

## Reading images

jp2a can process images in different file formats like [JPEG](https://en.wikipedia.org/wiki/JPEG) and [PNG](https://en.wikipedia.org/wiki/PNG).

jp2a has one method for every supported image type:

 - [decompress_jpeg](@ref decompress_jpeg)
 - [decompress_png](@ref decompress_png)

These methods read the image from a file stream and store their results in a [Image](@ref Image_) data structure.

The original pixels of the image are not stored in this [Image](@ref Image_). Instead the pixels are generated while reading the image to account for adjusted display dimensions.

The [Image](@ref Image_) saves the pixels in buffers that store the pixel line by line, i. e. the pixels of the first read line come first, followed by the pixels of the second line and so on.

## Adjusting image dimensions

jp2a adjust the dimensions of the pixel based on the terminal dimensions are user input. This [Image](@ref Image_) structure contains the image data in the (adjusted) display dimensions.

jp2a also has to account for different image orientations. The [Exif](https://en.wikipedia.org/wiki/Exif) data of an image may specify an orientation/rotation/flip that is different from the pixel layout in the image file.

### Determining the display aspect ratio and dimensions

The display aspect ratio and dimension is computed by the [aspect_ratio](@ref aspect_ratio) function. This function takes the terminal size or user specified dimensions into account as well as whether _x_ and _y_ dimensions should be switched.

The displayed aspect ratio may differ from the original image aspect ration because terminal "pixels" (characters) are not perfect squares.

_x_ and _y_ dimensions are switched if the image has to be rotated by 90° or 270° based on e. g. [Exif](https://en.wikipedia.org/wiki/Exif) metadata.

The [Image](@ref Image_) structure fields `width` and `height` save the display dimensions without taking switching _x_ and _y_ dimensions into account. The `src_width` and `src_height` fields do take switching dimensions into account.

### Normalizing

Before printing the image is normalized using the [normalize](@ref normalize) function. Multiple scanlines of the source image might contribute to one line in the displayed output. The displayed pixels accumulate colors during scanning. These accumulated values are divided by the number of scanlines used for that pixel to normalize them (which results in a displayed pixel having an average value from multiple source pixels).

### Reading images scanline by scanline

Images are read scanline by scanline. A scanline is a horizontal line of pixels. In other words, the pixel are organized in a 2-dimensional grid which requires two loops to iterate. The outer loop loops over scanlines (_y_ or height dimension) and the inner loop over the pixels in that scanline (_x_ or width dimension).

The inner loop is performed in one of these functions (depending on file type):

 - [process_scanline_jpeg](@ref process_scanline_jpeg)
 - [process_scanline_png](@ref process_scanline_png)

This function needs a mapping from original image pixel coordinates to output pixel coordinates because generally the input and display dimensions differ. This is accomplished by the `resize_x`, `resize_y` and `lookup_resx` fields of the [Image](@ref Image_) struct.

jp2a averages pixel along the input _y_ dimension while only considering one pixel along the _x_ dimension. Therefore `resize_y` rounds to the last scanline that should be taken into account for an output line while `lookup_resx` provides a direct mapping. These two fields are set beforehand in the [init_image](@ref init_image) function.

`resize_x` and `resize_y` contain the scaling factor and are calculated as follows:

- `resize_y = (display_height - 1) / (source_height - 1)`
- `resize_x = (display_width - 1) / (source_width - 1)`

`lookup_resx` is an array with as many pixels as the displayed image is wide. Its entries are:

- `lookup_resx[i] = i * resize_x`

`resize_x`, `resize_y` and `lookup_resx` work in original dimensions because they are utilized during reading of the image file. When the output _x_ and _y_ dimensions are switched, the source dimensions remain the same while the display dimensions need to be switched. Then the calculations are:

- `resize_y = (display_width - 1) / (source_height - 1)`
- `resize_x = (display_height - 1) / (source_width - 1)`
- `lookup_resx[i] = i * resize_x`

Note that `lookup_resx` with the same formula. However with a different value for `resize_x` and a different array length. It is now as long as the displayed image high.

## Displaying images

After reading and normalizing the [Image](@ref Image_) struct contains the pixel data in the following fields:

 - `pixel`: luminosity
 - `red`: red channel
 - `green`: green channel
 - `blue`: blue channel
 - `alpha`: opacity

These fields contain the pixel scanline by scanline. I. e. at index 0 is the pixel at the top left, at index src_width - 1 the pixel at the top right and so on. The ordering depends on the how the image was read from the image file. This differs from the displayed ordering if _x_ and _y_ dimensions need to be switched.

The function [get_pixel](@ref get_pixel) converts the displayed _x_ _y_ coordinates to the index in these pixel buffers. The index depends upon the image orientation and user options (whether the output should be flipped along the _x_ or _y_ dimension).

The image is printed to the console line by line. Each pixel of the [Image](@ref Image_) struct is converted to a character.

The character is determined from the luminosity and opacity from the pixel. A linear function maps these to indexes in the character palette. The character is then appended to the current line.

While ASCII characters are always one byte long in memory this is not the case for every UTF-8 character. To support UTF-8, two arrays are used that map the palette index to the start of the character and its length respectively. This information is then used to append the whole character to the current line.

For color output the red, green and blue channels are encoded with terminal color escape sequences.
