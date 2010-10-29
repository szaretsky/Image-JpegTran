/*
 * transupp.h
 *
 * Copyright (C) 1997-2009, Thomas G. Lane, Guido Vollbeding.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains declarations for image transformation routines and
 * other utility code used by the jpegtran sample application.  These are
 * NOT part of the core JPEG library.  But we keep these routines separate
 * from jpegtran.c to ease the task of maintaining jpegtran-like programs
 * that have other user interfaces.
 *
 * NOTE: all the routines declared here have very specific requirements
 * about when they are to be executed during the reading and writing of the
 * source and destination files.  See the comments in transupp.c, or see
 * jpegtran.c for an example of correct usage.
 */

/* If you happen not to want the image transform support, disable it here */
#ifndef TRANSFORMS_SUPPORTED
#define TRANSFORMS_SUPPORTED 1		/* 0 disables transform code */
#endif

/*
 * Although rotating and flipping data expressed as DCT coefficients is not
 * hard, there is an asymmetry in the JPEG format specification for images
 * whose dimensions aren't multiples of the iMCU size.  The right and bottom
 * image edges are padded out to the next iMCU boundary with junk data; but
 * no padding is possible at the top and left edges.  If we were to flip
 * the whole image including the pad data, then pad garbage would become
 * visible at the top and/or left, and real pixels would disappear into the
 * pad margins --- perhaps permanently, since encoders & decoders may not
 * bother to preserve DCT blocks that appear to be completely outside the
 * nominal image area.  So, we have to exclude any partial iMCUs from the
 * basic transformation.
 *
 * Transpose is the only transformation that can handle partial iMCUs at the
 * right and bottom edges completely cleanly.  flip_h can flip partial iMCUs
 * at the bottom, but leaves any partial iMCUs at the right edge untouched.
 * Similarly flip_v leaves any partial iMCUs at the bottom edge untouched.
 * The other transforms are defined as combinations of these basic transforms
 * and process edge blocks in a way that preserves the equivalence.
 *
 * The "trim" option causes untransformable partial iMCUs to be dropped;
 * this is not strictly lossless, but it usually gives the best-looking
 * result for odd-size images.  Note that when this option is active,
 * the expected mathematical equivalences between the transforms may not hold.
 * (For example, -rot 270 -trim trims only the bottom edge, but -rot 90 -trim
 * followed by -rot 180 -trim trims both edges.)
 *
 * We also offer a lossless-crop option, which discards data outside a given
 * image region but losslessly preserves what is inside.  Like the rotate and
 * flip transforms, lossless crop is restricted by the JPEG format: the upper
 * left corner of the selected region must fall on an iMCU boundary.  If this
 * does not hold for the given crop parameters, we silently move the upper left
 * corner up and/or left to make it so, simultaneously increasing the region
 * dimensions to keep the lower right crop corner unchanged.  (Thus, the
 * output image covers at least the requested region, but may cover more.)
 *
 * We also provide a lossless-resize option, which is kind of a lossless-crop
 * operation in the DCT coefficient block domain - it discards higher-order
 * coefficients and losslessly preserves lower-order coefficients of a
 * sub-block.
 *
 * Rotate/flip transform, resize, and crop can be requested together in a
 * single invocation.  The crop is applied last --- that is, the crop region
 * is specified in terms of the destination image after transform/resize.
 *
 * We also offer a "force to grayscale" option, which simply discards the
 * chrominance channels of a YCbCr image.  This is lossless in the sense that
 * the luminance channel is preserved exactly.  It's not the same kind of
 * thing as the rotate/flip transformations, but it's convenient to handle it
 * as part of this package, mainly because the transformation routines have to
 * be aware of the option to know how many components to work on.
 */


/* Short forms of external names for systems with brain-damaged linkers. */

#ifdef NEED_SHORT_EXTERNAL_NAMES
#define jtransform_parse_crop_spec	jTrParCrop
#define jtransform_request_workspace	jTrRequest
#define jtransform_adjust_parameters	jTrAdjust
#define jtransform_execute_transform	jTrExec
#define jtransform_perfect_transform	jTrPerfect
#define jcopy_markers_setup		jCMrkSetup
#define jcopy_markers_execute		jCMrkExec
#endif /* NEED_SHORT_EXTERNAL_NAMES */


/*
 * Codes for supported types of image transformations.
 */

static const unsigned char JExifHeader[]   = {0x45, 0x78, 0x69, 0x66, 0x00, 0x00};
static const unsigned char JFIFHeader[]    = {0x4a, 0x46, 0x49, 0x46, 0x00};
static const unsigned char JAdobeHeader[]  = {0x41, 0x64, 0x6f, 0x62, 0x65};

typedef enum {
	JXFORM_NONE,		/* no transformation */
	JXFORM_FLIP_H,		/* horizontal flip */
	JXFORM_FLIP_V,		/* vertical flip */
	JXFORM_TRANSPOSE,	/* transpose across UL-to-LR axis */
	JXFORM_TRANSVERSE,	/* transpose across UR-to-LL axis */
	JXFORM_ROT_90,		/* 90-degree clockwise rotation */
	JXFORM_ROT_180,		/* 180-degree rotation */
	JXFORM_ROT_270		/* 270-degree clockwise (or 90 ccw) */
} JXFORM_CODE;

static const char* JXFORM_NAME[] = {
	 "None"
	,"Flip horizontal"
	,"Flip vertical"
	,"Transpose"
	,"Transverse"
	,"Rotate 90 CW"
	,"Rotate 180 CW"
	,"Rotate 270 CW"
};

/*
 * Codes for crop parameters, which can individually be unspecified,
 * positive, or negative.  (Negative width or height makes no sense, though.)
 */

typedef enum {
	JCROP_UNSET,
	JCROP_POS,
	JCROP_NEG
} JCROP_CODE;

/*
 * Transform parameters struct.
 * NB: application must not change any elements of this struct after
 * calling jtransform_request_workspace.
 */

typedef struct {
  /* Options: set by caller */
  JXFORM_CODE transform;	/* image transform operator */
  boolean perfect;		/* if TRUE, fail if partial MCUs are requested */
  boolean trim;			/* if TRUE, trim partial MCUs as needed */
  boolean force_grayscale;	/* if TRUE, convert color image to grayscale */
  boolean crop;			/* if TRUE, crop source image */

  /* Crop parameters: application need not set these unless crop is TRUE.
   * These can be filled in by jtransform_parse_crop_spec().
   */
  JDIMENSION crop_width;	/* Width of selected region */
  JCROP_CODE crop_width_set;
  JDIMENSION crop_height;	/* Height of selected region */
  JCROP_CODE crop_height_set;
  JDIMENSION crop_xoffset;	/* X offset of selected region */
  JCROP_CODE crop_xoffset_set;	/* (negative measures from right edge) */
  JDIMENSION crop_yoffset;	/* Y offset of selected region */
  JCROP_CODE crop_yoffset_set;	/* (negative measures from bottom edge) */

  /* Internal workspace: caller should not touch these */
  int num_components;		/* # of components in workspace */
  jvirt_barray_ptr * workspace_coef_arrays; /* workspace for transformations */
  JDIMENSION output_width;	/* cropped destination dimensions */
  JDIMENSION output_height;
  JDIMENSION x_crop_offset;	/* destination crop offsets measured in iMCUs */
  JDIMENSION y_crop_offset;
  int iMCU_sample_width;	/* destination iMCU size */
  int iMCU_sample_height;
  
  boolean discard_thumbnail;
} jpeg_transform_info;


#if TRANSFORMS_SUPPORTED

/* Parse a crop specification (written in X11 geometry style) */
EXTERN(boolean) jtransform_parse_crop_spec
	JPP((jpeg_transform_info *info, const char *spec));
/* Request any required workspace */
EXTERN(boolean) jtransform_request_workspace
	JPP((j_decompress_ptr srcinfo, jpeg_transform_info *info));
/* Adjust output image parameters */
EXTERN(jvirt_barray_ptr *) jtransform_adjust_parameters
	JPP((j_decompress_ptr srcinfo, j_compress_ptr dstinfo,
	     jvirt_barray_ptr *src_coef_arrays,
	     jpeg_transform_info *info));
/* Execute the actual transformation, if any */
EXTERN(void) jtransform_execute_transform
	JPP((j_decompress_ptr srcinfo, j_compress_ptr dstinfo,
	     jvirt_barray_ptr *src_coef_arrays,
	     jpeg_transform_info *info));
/* Determine whether lossless transformation is perfectly
 * possible for a specified image and transformation.
 */
EXTERN(boolean) jtransform_perfect_transform
	JPP((JDIMENSION image_width, JDIMENSION image_height,
	     int MCU_width, int MCU_height,
	     JXFORM_CODE transform));

/* jtransform_execute_transform used to be called
 * jtransform_execute_transformation, but some compilers complain about
 * routine names that long.  This macro is here to avoid breaking any
 * old source code that uses the original name...
 */
#define jtransform_execute_transformation	jtransform_execute_transform

#endif /* TRANSFORMS_SUPPORTED */


/*
 * Support for copying optional markers from source to destination file.
 */

/* 

my %jpegMarker = (
    0x01 => 'TEM',
    0xc0 => 'SOF0', # to SOF15, with a few exceptions below
    0xc4 => 'DHT',
    0xc8 => 'JPGA',
    0xcc => 'DAC',
    0xd0 => 'RST0',
    0xd8 => 'SOI',
    0xd9 => 'EOI',
    0xda => 'SOS',
    0xdb => 'DQT',
    0xdc => 'DNL',
    0xdd => 'DRI',
    0xde => 'DHP',
    0xdf => 'EXP',
    0xe0 => 'APP0', # to APP15
    0xf0 => 'JPG0',
    0xfe => 'COM',
);

*/

typedef unsigned long JCOPY_MASK;

#define JCOPY_NONE     0x00000000
#define JCOPY_EXIF     0x00000001 // APP1
#define JCOPY_ICC      0x00000002 // APP2
#define JCOPY_META     0x00000004 // APP3
#define JCOPY_SCALADO  0x00000008 // APP4
#define JCOPY_RMETA    0x00000010 // APP5
#define JCOPY_EPPIM    0x00000020 // APP6
#define JCOPY_APP7     0x00000040 // ????
#define JCOPY_SPIFF    0x00000080 // APP8
#define JCOPY_ACOM     0x00000100 // APP10 | Comment
#define JCOPY_APP11    0x00000200 // ????
#define JCOPY_PINFO    0x00000400 // APP12 | PictureInfo
#define JCOPY_PSHOP    0x00000800 // APP13 | Photoshop
#define JCOPY_ADOBE    0x00001000 // APP14
#define JCOPY_APP15    0x00002000 // ????

#define JCOPY_COM      0x20000000 // JPEG_COM (0xfe) APP0 + 30

#define JCOPY_ALL      0x20002fff
#define JCOPY_DEFAULT  (JCOPY_ALL - JCOPY_ADOBE - JCOPY_PSHOP - JCOPY_APP7 - JCOPY_APP11 - JCOPY_APP15)

/* Setup decompression object to save desired markers in memory */
EXTERN(void) jcopy_markers_setup JPP((j_decompress_ptr srcinfo, JCOPY_MASK option));
/* Copy markers saved in the given source object to the destination object */
EXTERN(void) jcopy_markers_execute JPP((j_decompress_ptr srcinfo, j_compress_ptr dstinfo, JCOPY_MASK option));

/* miscellaneous useful macros */
#ifdef DONT_USE_B_MODE		/* define mode parameters for fopen() */
#define READ_BINARY	"r"
#define WRITE_BINARY	"w"
#else
#ifdef VMS			/* VMS is very nonstandard */
#define READ_BINARY	"rb", "ctx=stm"
#define WRITE_BINARY	"wb", "ctx=stm"
#else				/* standard ANSI-compliant case */
#define READ_BINARY	"rb"
#define WRITE_BINARY	"wb"
#endif
#endif
