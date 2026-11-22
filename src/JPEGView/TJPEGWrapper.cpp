
#include "stdafx.h"
#include "TJPEGWrapper.h"
#include "libjpeg-turbo\include\turbojpeg.h"
#include "MaxImageDef.h"
#include "ICCProfileTransform.h"

void * TurboJpeg::ReadImage(int &width,
					   int &height,
					   int &nchannels,
					   TJSAMP &chromoSubsampling,
					   bool &outOfMemory,
					   const void *buffer,
					   int sizebytes)
{
	outOfMemory = false;
	width = height = 0;
	nchannels = 3;
	chromoSubsampling = TJSAMP_420;

	tjhandle hDecoder = tj3Init(TJINIT_DECOMPRESS);
	if (hDecoder == NULL) {
		return NULL;
	}

	unsigned char* pPixelData = NULL;
	int nResult = tj3DecompressHeader(hDecoder, (unsigned char*)buffer, sizebytes);
	if (nResult == 0) {
		width = tj3Get(hDecoder, TJPARAM_JPEGWIDTH);
		height = tj3Get(hDecoder, TJPARAM_JPEGHEIGHT);
		chromoSubsampling = (TJSAMP)tj3Get(hDecoder, TJPARAM_SUBSAMP);
		if (abs((double)width * height) > MAX_IMAGE_PIXELS) {
			outOfMemory = true;
		} else if (width <= MAX_IMAGE_DIMENSION && height <= MAX_IMAGE_DIMENSION && chromoSubsampling != TJSAMP_UNKNOWN) {
			pPixelData = new(std::nothrow) unsigned char[TJPAD(static_cast<std::size_t>(width) * 3) * height];
			if (pPixelData != NULL) {
				nResult = tj3Decompress8(hDecoder, (unsigned char*)buffer, sizebytes, pPixelData, TJPAD(width * 3), TJPF_BGR);
				if (nResult != 0) {
					delete[] pPixelData;
					pPixelData = NULL;
				} else {
					unsigned char *iccBuf = NULL;
					size_t iccSize = 0;
					nResult = tj3GetICCProfile(hDecoder, &iccBuf, &iccSize);
					if (nResult == 0) {
						void* transform = ICCProfileTransform::CreateTransform(iccBuf, iccSize, ICCProfileTransform::FORMAT_BGR);
						if (transform != NULL) {
							ICCProfileTransform::DoTransform(transform, pPixelData, pPixelData, width, height, TJPAD(width * 3));
							ICCProfileTransform::DeleteTransform(transform);
						}
					tj3Free(iccBuf);
					}
				}
			} else {
				outOfMemory = true;
			}
		}
	}

	tj3Destroy(hDecoder);

	return pPixelData;
}

void * TurboJpeg::ReadImageCMYK(int &width,
					   int &height,
					   TJSAMP &chromoSubsampling,
					   bool &outOfMemory,
					   const void *buffer,
					   int sizebytes,
					   void *transformCMYKToBGR)
{
	outOfMemory = false;
	width = height = 0;
	chromoSubsampling = TJSAMP_420;

	if (transformCMYKToBGR == NULL) {
		return NULL;
	}
	tjhandle hDecoder = tj3Init(TJINIT_DECOMPRESS);
	if (hDecoder == NULL) {
		return NULL;
	}

	unsigned char* pPixelData = NULL;
	if (tj3DecompressHeader(hDecoder, (unsigned char*)buffer, sizebytes) == 0) {
		int nColorSpace = tj3Get(hDecoder, TJPARAM_COLORSPACE);
		width = tj3Get(hDecoder, TJPARAM_JPEGWIDTH);
		height = tj3Get(hDecoder, TJPARAM_JPEGHEIGHT);
		chromoSubsampling = (TJSAMP)tj3Get(hDecoder, TJPARAM_SUBSAMP);
		if (nColorSpace != TJCS_CMYK && nColorSpace != TJCS_YCCK) {
			// not a CMYK JPEG
		} else if (abs((double)width * height) > MAX_IMAGE_PIXELS) {
			outOfMemory = true;
		} else if (width > 0 && height > 0 && width <= MAX_IMAGE_DIMENSION && height <= MAX_IMAGE_DIMENSION && chromoSubsampling != TJSAMP_UNKNOWN) {
			// CMYK pixels as stored in the JPEG (YCCK is converted to CMYK by the decoder), the transform knows if they are inverted
			unsigned char* pCMYK = new(std::nothrow) unsigned char[static_cast<std::size_t>(width) * 4 * height];
			pPixelData = new(std::nothrow) unsigned char[TJPAD(static_cast<std::size_t>(width) * 3) * height];
			if (pCMYK == NULL || pPixelData == NULL) {
				outOfMemory = true;
				delete[] pPixelData;
				pPixelData = NULL;
			} else if (tj3Decompress8(hDecoder, (unsigned char*)buffer, sizebytes, pCMYK, width * 4, TJPF_CMYK) != 0 ||
				!ICCProfileTransform::DoTransform(transformCMYKToBGR, pCMYK, pPixelData, width, height, width * 4)) {
				delete[] pPixelData;
				pPixelData = NULL;
			}
			delete[] pCMYK;
		}
	}

	tj3Destroy(hDecoder);

	return pPixelData;
}

void * TurboJpeg::Compress(const void *source,
					  int width,
					  int height,
					  int &len,
					  bool &outOfMemory,
					  int quality)
{
	outOfMemory = false;
	len = 0;
	tjhandle hEncoder = tj3Init(TJINIT_COMPRESS);
	if (hEncoder == NULL) {
		return NULL;
	}

	unsigned char* pJPEGCompressed = NULL;
	size_t nCompressedLen = 0;
	tj3Set(hEncoder, TJPARAM_SUBSAMP, TJSAMP_420);
	tj3Set(hEncoder, TJPARAM_QUALITY, quality);
	int nResult = tj3Compress8(hEncoder, (unsigned char*)source, width, TJPAD(width * 3), height, TJPF_BGR,
		&pJPEGCompressed, &nCompressedLen);
	if (nResult != 0 || nCompressedLen > INT_MAX) {
		if (pJPEGCompressed == NULL) {
			outOfMemory = true;
		}
		Free(pJPEGCompressed);
		pJPEGCompressed = NULL;
	}

	len = nCompressedLen;

	tj3Destroy(hEncoder);

	return pJPEGCompressed;
}

void TurboJpeg::Free(void* buffer) {
	tj3Free(buffer);
}
