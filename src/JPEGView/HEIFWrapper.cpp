#include "stdafx.h"

#include "HEIFWrapper.h"
#include "MaxImageDef.h"
#include "ICCProfileTransform.h"

// ICC profile created from the nclx color profile (color primaries and transfer characteristics, e.g. Display P3)
// of an image without ICC profile, empty if the image has no nclx profile or is sRGB
static std::vector<unsigned char> GetCICPProfile(heif::ImageHandle& handle, const heif::Image& image) {
	int nPrimaries = -1, nTransfer = -1;
	heif_color_profile_nclx* nclx = NULL;
	heif_error error = heif_image_handle_get_nclx_color_profile(handle.get_raw_image_handle(), &nclx);
	if (error.code == heif_error_Ok && nclx != NULL) {
		nPrimaries = nclx->color_primaries;
		nTransfer = nclx->transfer_characteristics;
	}
	if (nclx != NULL) {
		heif_nclx_color_profile_free(nclx);
	}
	if (nPrimaries < 0) {
		// nclx profile stored in the bitstream instead of a colr box
		try {
			heif::ColorProfile_nclx imageNclx = image.get_nclx_color_profile();
			nPrimaries = imageNclx.get_color_primaries();
			nTransfer = imageNclx.get_transfer_characteristics();
		} catch (...) {}
	}
	if (nPrimaries < 0) {
		return std::vector<unsigned char>();
	}
	return ICCProfileTransform::CreateProfileFromCICP(nPrimaries, nTransfer);
}

void * HeifReader::ReadImage(int &width,
					   int &height,
					   int &nchannels,
					   int &frame_count,
					   void* &exif_chunk,
					   bool &outOfMemory,
					   int frame_index,
					   const void *buffer,
					   int sizebytes)
{
	outOfMemory = false;
	width = height = 0;
	nchannels = 4;

	unsigned char* pPixelData = NULL;
	exif_chunk = NULL;

	heif::Context context;
	context.read_from_memory_without_copy(buffer, sizebytes);
	frame_count = context.get_number_of_top_level_images();
	heif_item_id item_id = context.get_list_of_top_level_image_IDs().at(frame_index);
	heif::ImageHandle handle = context.get_image_handle(item_id);
	// height = handle.get_height();
	// width = handle.get_width();
	heif::Image image = handle.decode_image(heif_colorspace_RGB, heif_chroma_interleaved_RGBA);
	int stride;
	uint8_t* data = image.get_plane(heif_channel_interleaved, &stride);
	width = image.get_width(heif_channel_interleaved);
	height = image.get_height(heif_channel_interleaved);

	if (width > MAX_IMAGE_DIMENSION || height > MAX_IMAGE_DIMENSION)
		return NULL;
	if (abs((double)width * height) > MAX_IMAGE_PIXELS) {
		outOfMemory = true;
		return NULL;
	}
	if (width < 1 || height < 1 || width * nchannels > stride)
		return NULL;

	size_t size = (size_t)width * nchannels * height;
	pPixelData = new(std::nothrow) unsigned char[size];
	if (pPixelData == NULL) {
		outOfMemory = true;
		return NULL;
	}
	std::vector<uint8_t> iccp = image.get_raw_color_profile();
	if (iccp.empty()) {
		iccp = GetCICPProfile(handle, image);
	}
	void* transform = ICCProfileTransform::CreateTransform(iccp.data(), iccp.size(), ICCProfileTransform::FORMAT_RGBA);
	size_t i, j;
	if (!ICCProfileTransform::DoTransform(transform, data, pPixelData, width, height, stride=stride)) {
		unsigned int* o = (unsigned int*)pPixelData;
		for (i = 0; i < height; i++) {
			unsigned int* p = (unsigned int*)(data + i * stride);
			for (j = 0; j < width; j++) {
				// RGBA -> BGRA conversion
				*o = _rotr(_byteswap_ulong(*p), 8);
				p++;
				o++;
			}
		}
	}
	ICCProfileTransform::DeleteTransform(transform);

	std::vector<heif_item_id> exif_blocks = handle.get_list_of_metadata_block_IDs("Exif");

	if (!exif_blocks.empty()) {
		std::vector<uint8_t> exif = handle.get_metadata(exif_blocks[0]);
		// 65538 magic number comes from investigations by qbnu
		// see https://github.com/sylikc/jpegview/pull/213#pullrequestreview-1494451359 for more details
		/*
		* These libraries all have their own ideas about where to start the Exif data from.
		* JPEG Exif blocks are in the format
		  FF E1 SS SS 45 78 69 66 00 00 [data]

		  The SS SS is a big-endian unsigned short representing the size of everything after FF E1.

		  libjxl gives 00 00 00 00 [data]
		  libheif gives 00 00 00 00 45 78 69 66 00 00 [data]
		  libavif, libwebp and libpng give [data], so they have different limits for size.

		  If you want I can change it to 65536 + an offset and add notes explaining why.
		*/
		if (exif.size() > 8 && exif.size() < 65538) {
			exif_chunk = malloc(exif.size());
			if (exif_chunk != NULL) {
				memcpy(exif_chunk, exif.data(), exif.size());
				*((unsigned short*)exif_chunk) = _byteswap_ushort(0xFFE1);
				*((unsigned short*)exif_chunk + 1) = _byteswap_ushort(exif.size() - 2);
			}
		}
	}

	return (void*)pPixelData;
}
