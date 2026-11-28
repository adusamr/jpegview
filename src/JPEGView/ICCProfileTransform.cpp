#include "stdafx.h"

#include "ICCProfileTransform.h"
#include "SettingsProvider.h"
#include "ProcessingThreadPool.h"
#include <vector>
#include <mutex>
#include <math.h>


#ifndef WINXP

// This define is necessary for 32-bit builds to work, for some reason
#define CMS_DLL
#include "lcms2.h"
#define TYPE_LabA_8 (COLORSPACE_SH(PT_Lab)|EXTRA_SH(1)|CHANNELS_SH(3)|BYTES_SH(1))


void* ICCProfileTransform::sRGBProfile = NULL;

namespace {
	// Defined below: true only for the standard sRGB profiles (checked by exact description, not by a text search,
	// as e.g. "Linear sRGB" profiles share the sRGB primaries but have another tone curve and must be converted)
	bool IsSRGBProfile(cmsHPROFILE hProfile, const CString& sPath);
}

void* ICCProfileTransform::CreateTransform(const void* profile, unsigned int size, PixelFormat format)
{
	if (profile == NULL || size == 0) {
		return NULL; // No ICC Profile
	}

	if ((format == FORMAT_BGRA || format == FORMAT_RGBA || format == FORMAT_BGR || format == FORMAT_RGB) && CaptureProfile(profile, size)) {
		return NULL; // pixels stay in the embedded color space, they are converted to the display when painted
	}

	if (sRGBProfile == NULL) {
		try {
			sRGBProfile = cmsCreate_sRGBProfile();
		} catch (...) {}
	}

	if (sRGBProfile == NULL) {
		return NULL; // lcms2.dll not found or out of memory
	}

	cmsHPROFILE hInProfile = cmsOpenProfileFromMem(profile, size);
	if (hInProfile == NULL) {
		return NULL; // Invalid profile
	}

    // Already sRGB?
    if (::IsSRGBProfile(hInProfile, CString())) {
        cmsCloseProfile(hInProfile);
        return nullptr;
    }

	// Create transform from embedded profile to sRGB
	cmsUInt32Number flags = cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_COPY_ALPHA;
	cmsUInt32Number inFormat, outFormat;
	switch (format) {
		case FORMAT_BGRA:
			inFormat = TYPE_BGRA_8;
			outFormat = TYPE_BGRA_8;
			break;
		case FORMAT_RGBA:
			inFormat = TYPE_RGBA_8;
			outFormat = TYPE_BGRA_8;
			break;
		case FORMAT_BGR:
			inFormat = TYPE_BGR_8;
			outFormat = TYPE_BGR_8;
			break;
		case FORMAT_RGB:
			inFormat = TYPE_RGB_8;
			outFormat = TYPE_BGR_8;
			break;
		default:
			cmsCloseProfile(hInProfile);
			return NULL;
	}
	cmsHTRANSFORM transform = cmsCreateTransform(hInProfile, inFormat, sRGBProfile, outFormat, INTENT_RELATIVE_COLORIMETRIC, flags);
	cmsCloseProfile(hInProfile);
	return transform;
}

bool ICCProfileTransform::DoTransform(void* transform, const void* inputBuffer, void* outputBuffer, unsigned int width, unsigned int height, unsigned int stride)
{
	unsigned int numPixels = width * height;
	if (transform == NULL || inputBuffer == NULL || outputBuffer == NULL || numPixels == 0 || !CSettingsProvider::This().UseEmbeddedColorProfiles())
		return false;
	cmsUInt32Number inFormat = cmsGetTransformInputFormat(transform);
	int nchannels;
	switch (inFormat) {
		case TYPE_BGRA_8:
		case TYPE_RGBA_8:
		case TYPE_LabA_8:
		case TYPE_CMYK_8:
		case TYPE_CMYK_8_REV:
			nchannels = 4;
			break;
		case TYPE_BGR_8:
		case TYPE_RGB_8:
		case TYPE_Lab_8:
			nchannels = 3;
			break;
		default:
			return false;
	}
	// Only differs from the input for CMYK to BGR transforms (see CreateWideGamutTransform())
	cmsUInt32Number outFormat = cmsGetTransformOutputFormat(transform);
	int nOutChannels = T_CHANNELS(outFormat) + T_EXTRA(outFormat);
	if (T_BYTES(outFormat) != 1 || (nOutChannels != 3 && nOutChannels != 4))
		return false;
	if (stride == 0)
		stride = width * nchannels;
	unsigned int nOutStride = Helpers::DoPadding(width * nOutChannels, 4);
	cmsDoTransformLineStride(transform, inputBuffer, outputBuffer, width, height, stride, nOutStride, stride * height, nOutStride * height);
	return true;
}

void ICCProfileTransform::DeleteTransform(void* transform)
{
	if (transform != NULL)
		cmsDeleteTransform(transform);
}

void* ICCProfileTransform::CreateLabTransform(PixelFormat format) {
	cmsHTRANSFORM transform = NULL;
	cmsHPROFILE hLabProfile = NULL;
	try {
		hLabProfile = cmsCreateLab4Profile(cmsD50_xyY());
		if (sRGBProfile == NULL) {
			sRGBProfile = cmsCreate_sRGBProfile();
		}
	} catch (...) {}

	if (hLabProfile == NULL || sRGBProfile == NULL)
		return NULL; // Could not create profile

	// Create transform from CIELAB D50 (Photoshop "Lab mode") to sRGB
	cmsUInt32Number flags = cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_COPY_ALPHA;
	cmsUInt32Number inFormat, outFormat;
	switch (format) {
		case FORMAT_Lab:
			inFormat = TYPE_Lab_8;
			outFormat = TYPE_BGR_8;
			break;
		case FORMAT_LabA:
			inFormat = TYPE_LabA_8;
			outFormat = TYPE_BGRA_8;
			break;
		default:
			cmsCloseProfile(hLabProfile);
			return NULL;
	}
	transform = cmsCreateTransform(hLabProfile, inFormat, sRGBProfile, outFormat, INTENT_RELATIVE_COLORIMETRIC, flags);
	cmsCloseProfile(hLabProfile);
	return transform;
}

///////////////////////////////////////////////////////////////////////////////////
// Display color profile
///////////////////////////////////////////////////////////////////////////////////

namespace {

	// Applies the display transform to a strip of a 32 bpp DIB, executed in parallel on the processing thread pool
	class CDisplayTransformRequest : public CProcessingRequest {
	public:
		CDisplayTransformRequest(cmsHTRANSFORM transform, const void* pSource, void* pTarget, CSize size)
			: CProcessingRequest(pSource, size, pTarget, size, CPoint(0, 0), size) {
			Transform = transform;
		}

		virtual bool ProcessStrip(int offsetY, int sizeY) {
			// 32 bpp DIBs have no row padding, so a strip is one contiguous run of pixels
			size_t nOffset = (size_t)offsetY * SourceSize.cx * 4;
			cmsDoTransform(Transform, (const unsigned char*)SourcePixels + nOffset, (unsigned char*)TargetPixels + nOffset, (cmsUInt32Number)SourceSize.cx * sizeY);
			return true;
		}

	private:
		cmsHTRANSFORM Transform;
	};

	// Minimal declarations of the DisplayConfig API (Windows 7+). The SDK only declares them for WINVER >= 0x0601
	// and JPEGView must still start on systems without these functions, so they are loaded dynamically.
	struct JV_DISPLAYCONFIG_PATH_SOURCE_INFO { LUID adapterId; UINT32 id; UINT32 modeInfoIdx; UINT32 statusFlags; };
	struct JV_DISPLAYCONFIG_PATH_TARGET_INFO {
		LUID adapterId; UINT32 id; UINT32 modeInfoIdx; UINT32 outputTechnology; UINT32 rotation; UINT32 scaling;
		UINT32 refreshRateNumerator; UINT32 refreshRateDenominator; UINT32 scanLineOrdering; BOOL targetAvailable; UINT32 statusFlags;
	};
	struct JV_DISPLAYCONFIG_PATH_INFO { JV_DISPLAYCONFIG_PATH_SOURCE_INFO sourceInfo; JV_DISPLAYCONFIG_PATH_TARGET_INFO targetInfo; UINT32 flags; };
	struct JV_DISPLAYCONFIG_DEVICE_INFO_HEADER { UINT32 type; UINT32 size; LUID adapterId; UINT32 id; };
	struct JV_DISPLAYCONFIG_SOURCE_DEVICE_NAME { JV_DISPLAYCONFIG_DEVICE_INFO_HEADER header; WCHAR viewGdiDeviceName[32]; };
	struct JV_DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO { JV_DISPLAYCONFIG_DEVICE_INFO_HEADER header; UINT32 value; UINT32 colorEncoding; UINT32 bitsPerColorChannel; };
	static_assert(sizeof(JV_DISPLAYCONFIG_PATH_INFO) == 72, "DISPLAYCONFIG_PATH_INFO layout mismatch");
	const int JV_DISPLAYCONFIG_MODE_INFO_SIZE = 64; // sizeof(DISPLAYCONFIG_MODE_INFO), modes are not interpreted
	const UINT32 JV_QDC_ONLY_ACTIVE_PATHS = 0x00000002;
	const UINT32 JV_DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME = 1;
	const UINT32 JV_DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO = 9;
	const UINT32 JV_ADVANCED_COLOR_ENABLED = 0x2;

	typedef LONG (WINAPI *GetDisplayConfigBufferSizesFn)(UINT32, UINT32*, UINT32*);
	typedef LONG (WINAPI *QueryDisplayConfigFn)(UINT32, UINT32*, void*, UINT32*, void*, void*);
	typedef LONG (WINAPI *DisplayConfigGetDeviceInfoFn)(JV_DISPLAYCONFIG_DEVICE_INFO_HEADER*);

	// Returns true if Windows itself color manages this monitor (HDR or Windows 11 "Automatically manage color for apps").
	// In this case the desktop compositor already converts sRGB content to the display, an additional
	// transform by JPEGView would apply the correction twice.
	bool IsAdvancedColorActive(LPCTSTR sGdiDeviceName) {
		HMODULE hUser32 = ::GetModuleHandle(_T("user32.dll"));
		if (hUser32 == NULL) return false;
		GetDisplayConfigBufferSizesFn pGetBufferSizes = (GetDisplayConfigBufferSizesFn)::GetProcAddress(hUser32, "GetDisplayConfigBufferSizes");
		QueryDisplayConfigFn pQueryDisplayConfig = (QueryDisplayConfigFn)::GetProcAddress(hUser32, "QueryDisplayConfig");
		DisplayConfigGetDeviceInfoFn pGetDeviceInfo = (DisplayConfigGetDeviceInfoFn)::GetProcAddress(hUser32, "DisplayConfigGetDeviceInfo");
		if (pGetBufferSizes == NULL || pQueryDisplayConfig == NULL || pGetDeviceInfo == NULL) return false;

		std::vector<JV_DISPLAYCONFIG_PATH_INFO> paths;
		std::vector<UINT64> modes; // UINT64 for correct alignment of the mode info structures
		UINT32 nPaths = 0, nModes = 0;
		LONG result;
		do {
			if (pGetBufferSizes(JV_QDC_ONLY_ACTIVE_PATHS, &nPaths, &nModes) != ERROR_SUCCESS) return false;
			paths.resize(max(nPaths, 1u));
			modes.resize(max(nModes, 1u) * JV_DISPLAYCONFIG_MODE_INFO_SIZE / sizeof(UINT64));
			result = pQueryDisplayConfig(JV_QDC_ONLY_ACTIVE_PATHS, &nPaths, paths.data(), &nModes, modes.data(), NULL);
		} while (result == ERROR_INSUFFICIENT_BUFFER);
		if (result != ERROR_SUCCESS) return false;

		CT2CW sDeviceName(sGdiDeviceName);
		for (UINT32 i = 0; i < nPaths; i++) {
			JV_DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName;
			memset(&sourceName, 0, sizeof(sourceName));
			sourceName.header.type = JV_DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
			sourceName.header.size = sizeof(sourceName);
			sourceName.header.adapterId = paths[i].sourceInfo.adapterId;
			sourceName.header.id = paths[i].sourceInfo.id;
			if (pGetDeviceInfo(&sourceName.header) != ERROR_SUCCESS || _wcsicmp(sourceName.viewGdiDeviceName, sDeviceName) != 0) {
				continue;
			}
			JV_DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo;
			memset(&colorInfo, 0, sizeof(colorInfo));
			colorInfo.header.type = JV_DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
			colorInfo.header.size = sizeof(colorInfo);
			colorInfo.header.adapterId = paths[i].targetInfo.adapterId;
			colorInfo.header.id = paths[i].targetInfo.id;
			if (pGetDeviceInfo(&colorInfo.header) == ERROR_SUCCESS && (colorInfo.value & JV_ADVANCED_COLOR_ENABLED) != 0) {
				return true;
			}
		}
		return false;
	}

	// Gets the file name of the ICC profile assigned to the monitor in the Windows color management settings
	bool GetSystemMonitorProfile(LPCTSTR sGdiDeviceName, CString& sProfilePath) {
		HDC hDC = ::CreateDC(_T("DISPLAY"), sGdiDeviceName, NULL, NULL);
		if (hDC == NULL) return false;
		std::vector<TCHAR> buffer(MAX_PATH + 1);
		DWORD nLen = MAX_PATH;
		BOOL bOk = ::GetICMProfile(hDC, &nLen, buffer.data());
		if (!bOk && nLen > MAX_PATH) {
			buffer.resize(nLen + 1);
			bOk = ::GetICMProfile(hDC, &nLen, buffer.data());
		}
		::DeleteDC(hDC);
		if (!bOk) return false;
		sProfilePath = buffer.data();
		return !sProfilePath.IsEmpty();
	}

	// Reads the profile file (Unicode file names are supported, unlike cmsOpenProfileFromFile())
	bool ReadProfileFile(LPCTSTR sFileName, std::vector<unsigned char>& data) {
		data.clear();
		HANDLE hFile = ::CreateFile(sFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile == INVALID_HANDLE_VALUE) return false;
		LARGE_INTEGER fileSize;
		if (::GetFileSizeEx(hFile, &fileSize) && fileSize.QuadPart > 128 && fileSize.QuadPart < 64 * 1024 * 1024) {
			data.resize((size_t)fileSize.QuadPart);
			DWORD nRead = 0;
			if (!::ReadFile(hFile, data.data(), (DWORD)data.size(), &nRead, NULL) || nRead != data.size()) {
				data.clear();
			}
		}
		::CloseHandle(hFile);
		return !data.empty();
	}

	bool IsDisplayColorManagementEnabled() {
		CString sSetting = CSettingsProvider::This().DisplayColorProfile();
		sSetting.Trim();
		return !(sSetting.IsEmpty() || sSetting.CompareNoCase(_T("none")) == 0 || sSetting.CompareNoCase(_T("false")) == 0);
	}

	// Profile of the image currently being loaded on this thread, see ICCProfileTransform::CaptureProfile()
	thread_local ICCProfilePtr t_capturedProfile;

	// True if the profile is (most likely) a standard sRGB profile - then no transform is needed
	bool IsSRGBProfile(cmsHPROFILE hProfile, const CString& sPath) {
		CString sFileName = sPath.Mid(sPath.ReverseFind(_T('\\')) + 1);
		if (sFileName.CompareNoCase(_T("sRGB Color Space Profile.icm")) == 0) {
			return true; // Windows default when no profile is assigned
		}
		char description[256] = { 0 };
		if (cmsGetProfileInfoASCII(hProfile, cmsInfoDescription, "en", "US", description, sizeof(description)) > 0) {
			if (_stricmp(description, "sRGB IEC61966-2.1") == 0 || _stricmp(description, "sRGB IEC61966-2-1 black scaled") == 0 ||
				_stricmp(description, "sRGB built-in") == 0) {
				return true;
			}
		}
		return false;
	}

	CString GetProfileDisplayName(cmsHPROFILE hProfile, const CString& sPath) {
		wchar_t description[256] = { 0 };
		if (cmsGetProfileInfo(hProfile, cmsInfoDescription, "en", "US", description, sizeof(description)) > 0 /* size in bytes */ && description[0] != 0) {
			return CString(description);
		}
		return sPath.Mid(sPath.ReverseFind(_T('\\')) + 1);
	}

	// Creates a matrix/shaper RGB profile and returns it serialized as ICC profile, empty on failure.
	// Takes ownership of curve.
	std::vector<unsigned char> CreateRGBProfileData(const cmsCIExyY& whitePoint, const cmsCIExyYTRIPLE& primaries, cmsToneCurve* curve, const wchar_t* sDescription) {
		std::vector<unsigned char> data;
		if (curve == NULL) {
			return data;
		}
		cmsToneCurve* curves[3] = { curve, curve, curve };
		cmsHPROFILE hProfile = cmsCreateRGBProfile(&whitePoint, &primaries, curves);
		cmsFreeToneCurve(curve);
		if (hProfile == NULL) {
			return data;
		}
		cmsMLU* pDescription = cmsMLUalloc(NULL, 1);
		if (pDescription != NULL) {
			cmsMLUsetWide(pDescription, "en", "US", sDescription);
			cmsWriteTag(hProfile, cmsSigProfileDescriptionTag, pDescription);
			cmsMLUfree(pDescription);
		}
		cmsUInt32Number nSize = 0;
		if (cmsSaveProfileToMem(hProfile, NULL, &nSize) && nSize > 128) {
			data.resize(nSize);
			if (!cmsSaveProfileToMem(hProfile, data.data(), &nSize)) {
				data.clear();
			}
		}
		cmsCloseProfile(hProfile);
		return data;
	}

	cmsToneCurve* CreateSRGBToneCurve() {
		const cmsFloat64Number params[5] = { 2.4, 1.0 / 1.055, 0.055 / 1.055, 1.0 / 12.92, 0.04045 };
		return cmsBuildParametricToneCurve(NULL, 4, params);
	}

	cmsToneCurve* CreateGammaToneCurve(double dGamma) {
		const cmsFloat64Number params[1] = { dGamma };
		return cmsBuildParametricToneCurve(NULL, 1, params);
	}

	const cmsCIExyY cD65 = { 0.3127, 0.3290, 1.0 };

	// Working space for CMYK and Lab images (see ICCProfileTransform::CreateWideGamutTransform()): Rec. 2020 primaries,
	// that contain the gamut of print processes and practically all real surface colors, with the sRGB tone curve
	// to keep 8 bit quantization steps small. Created once, the same pointer is shared by all images using it.
	ICCProfilePtr GetWideGamutProfile() {
		static const ICCProfilePtr s_profile = []() -> ICCProfilePtr {
			try {
				const cmsCIExyYTRIPLE rec2020 = { { 0.708, 0.292, 1.0 }, { 0.170, 0.797, 1.0 }, { 0.131, 0.046, 1.0 } };
				std::vector<unsigned char> data = CreateRGBProfileData(cD65, rec2020, CreateSRGBToneCurve(), L"JPEGView working space (Rec. 2020 primaries, sRGB tone curve)");
				if (!data.empty()) {
					return std::make_shared<const std::vector<unsigned char>>(std::move(data));
				}
			} catch (...) {} // lcms2.dll not found
			return ICCProfilePtr();
		}();
		return s_profile;
	}

	// True if the tone curve gives the same linear values as the sRGB tone curve for all 8 bit input values, within
	// one step of the 12 bit linear values the resampling code works with (see LookupTables.h)
	bool IsSRGBToneCurve(const cmsToneCurve* curve) {
		for (int i = 0; i < 256; i++) {
			double v = i / 255.0;
			double dSRGB = (v <= 0.04045) ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
			if (fabs(cmsEvalToneCurveFloat(curve, (cmsFloat32Number)v) - dSRGB) > 1.0 / 4095) {
				return false;
			}
		}
		return true;
	}

	// Checks if the profile is a matrix/TRC RGB profile (colorants and one tone curve per channel, no lookup tables).
	// Returns 0 if not, 1 if all its tone curves are the sRGB tone curve, 2 if one of them differs from the sRGB tone curve.
	int GetMatrixShaperToneCurveType(cmsHPROFILE hProfile) {
		if (cmsGetColorSpace(hProfile) != cmsSigRgbData || !cmsIsMatrixShaper(hProfile)) {
			return 0;
		}
		// lcms uses the lookup tables instead of the matrix if the profile has both, they may describe other colors
		if (cmsIsTag(hProfile, cmsSigAToB0Tag) || cmsIsTag(hProfile, cmsSigAToB1Tag) || cmsIsTag(hProfile, cmsSigAToB2Tag) ||
			cmsIsTag(hProfile, cmsSigDToB0Tag) || cmsIsTag(hProfile, cmsSigDToB1Tag) || cmsIsTag(hProfile, cmsSigDToB2Tag)) {
			return 0;
		}
		const cmsTagSignature trcs[3] = { cmsSigRedTRCTag, cmsSigGreenTRCTag, cmsSigBlueTRCTag };
		int nType = 1;
		for (int i = 0; i < 3; i++) {
			const cmsToneCurve* curve = (const cmsToneCurve*)cmsReadTag(hProfile, trcs[i]);
			if (curve == NULL) {
				return 0;
			}
			if (!IsSRGBToneCurve(curve)) {
				nType = 2;
			}
		}
		return nType;
	}

	// True if the profile is a matrix/TRC RGB profile whose tone curves differ from the sRGB tone curve
	// (see ICCProfileTransform::ReencodeToSRGBToneCurve())
	bool NeedsSRGBToneCurveVariant(cmsHPROFILE hProfile) {
		return GetMatrixShaperToneCurveType(hProfile) == 2;
	}

	// Creates a matrix/TRC profile with the colorants, white point, chromatic adaptation, version and device class of
	// the given matrix/TRC profile, but with the given tone curve (takes ownership of curve) for all channels. So it has
	// the same gamut, only the encoding differs. sSuffix is appended to the description.
	// Returns it serialized as ICC profile, empty on failure.
	std::vector<unsigned char> CreateToneCurveVariant(cmsHPROFILE hProfile, cmsToneCurve* curve, LPCTSTR sSuffix) {
		std::vector<unsigned char> data;
		cmsHPROFILE hNew = cmsCreateProfilePlaceholder(NULL);
		if (curve != NULL && hNew != NULL) {
			cmsSetProfileVersion(hNew, cmsGetProfileVersion(hProfile));
			cmsSetDeviceClass(hNew, cmsGetDeviceClass(hProfile));
			cmsSetColorSpace(hNew, cmsSigRgbData);
			cmsSetPCS(hNew, cmsSigXYZData);
			cmsSetHeaderRenderingIntent(hNew, INTENT_RELATIVE_COLORIMETRIC);
			bool bOk = true;
			const cmsTagSignature colorants[3] = { cmsSigRedColorantTag, cmsSigGreenColorantTag, cmsSigBlueColorantTag };
			const cmsTagSignature trcs[3] = { cmsSigRedTRCTag, cmsSigGreenTRCTag, cmsSigBlueTRCTag };
			for (int i = 0; i < 3 && bOk; i++) {
				void* pColorant = cmsReadTag(hProfile, colorants[i]);
				bOk = pColorant != NULL && cmsWriteTag(hNew, colorants[i], pColorant) && cmsWriteTag(hNew, trcs[i], curve);
			}
			void* pWhitePoint = cmsReadTag(hProfile, cmsSigMediaWhitePointTag);
			if (bOk) {
				bOk = cmsWriteTag(hNew, cmsSigMediaWhitePointTag, (pWhitePoint != NULL) ? pWhitePoint : (void*)cmsD50_XYZ());
			}
			void* pAdaptation = cmsReadTag(hProfile, cmsSigChromaticAdaptationTag);
			if (bOk && pAdaptation != NULL) {
				bOk = cmsWriteTag(hNew, cmsSigChromaticAdaptationTag, pAdaptation);
			}
			if (bOk) {
				CString sDescription = GetProfileDisplayName(hProfile, CString());
				sDescription += sSuffix;
				cmsMLU* pDescription = cmsMLUalloc(NULL, 1);
				if (pDescription != NULL) {
					cmsMLUsetWide(pDescription, "en", "US", CT2CW(sDescription));
					cmsWriteTag(hNew, cmsSigProfileDescriptionTag, pDescription);
					cmsMLUfree(pDescription);
				}
				cmsUInt32Number nSize = 0;
				if (cmsSaveProfileToMem(hNew, NULL, &nSize) && nSize > 128) {
					data.resize(nSize);
					if (!cmsSaveProfileToMem(hNew, data.data(), &nSize)) {
						data.clear();
					}
				}
			}
		}
		if (curve != NULL) cmsFreeToneCurve(curve);
		if (hNew != NULL) cmsCloseProfile(hNew);
		return data;
	}

	// Cache for GetSRGBToneCurveVariant(), so that the frames of an animation and images with the same profile share
	// the variant (and its display transform). The last entry is the most recently used one.
	std::mutex s_variantCacheMutex;
	std::vector<std::pair<ICCProfilePtr, ICCProfilePtr>> s_variantCache; // embedded profile -> variant, empty if not needed
	const size_t VARIANT_CACHE_SIZE = 8;

	// Gets the sRGB tone curve variant of the embedded profile (see CreateSRGBToneCurveVariant()),
	// or an empty pointer if the profile does not need one or it cannot be created
	ICCProfilePtr GetSRGBToneCurveVariant(const ICCProfilePtr& profile) {
		{
			std::lock_guard<std::mutex> lock(s_variantCacheMutex);
			for (size_t i = 0; i < s_variantCache.size(); i++) {
				if (s_variantCache[i].first == profile || *s_variantCache[i].first == *profile) {
					std::pair<ICCProfilePtr, ICCProfilePtr> entry = s_variantCache[i];
					s_variantCache.erase(s_variantCache.begin() + i);
					s_variantCache.push_back(entry);
					return entry.second;
				}
			}
		}
		ICCProfilePtr variant;
		try {
			cmsHPROFILE hProfile = cmsOpenProfileFromMem(profile->data(), (cmsUInt32Number)profile->size());
			if (hProfile != NULL) {
				if (NeedsSRGBToneCurveVariant(hProfile)) {
					std::vector<unsigned char> data = CreateToneCurveVariant(hProfile, CreateSRGBToneCurve(), _T(" (sRGB tone curve)"));
					if (!data.empty()) {
						variant = std::make_shared<const std::vector<unsigned char>>(std::move(data));
					}
				}
				cmsCloseProfile(hProfile);
			}
		} catch (...) {
			variant.reset();
		}
		std::lock_guard<std::mutex> lock(s_variantCacheMutex);
		if (s_variantCache.size() >= VARIANT_CACHE_SIZE) {
			s_variantCache.erase(s_variantCache.begin());
		}
		s_variantCache.push_back(std::make_pair(profile, variant));
		return variant;
	}
}

///////////////////////////////////////////////////////////////////////////////////
// Embedded profiles kept with the image (not converted to sRGB when loading)
///////////////////////////////////////////////////////////////////////////////////

bool ICCProfileTransform::KeepEmbeddedColorSpace() {
	static volatile int s_nKeep = -1; // evaluated once, the settings do not change at runtime
	if (s_nKeep < 0) {
		bool bKeep = CSettingsProvider::This().UseEmbeddedColorProfiles() && IsDisplayColorManagementEnabled() && IsAvailable();
		s_nKeep = bKeep ? 1 : 0;
	}
	return s_nKeep == 1;
}

void ICCProfileTransform::ResetCapturedProfile() {
	t_capturedProfile.reset();
}

ICCProfilePtr ICCProfileTransform::GetCapturedProfile() {
	return t_capturedProfile;
}

void ICCProfileTransform::SetCapturedProfile(const ICCProfilePtr& profile) {
	t_capturedProfile = profile;
}

bool ICCProfileTransform::CaptureProfile(const void* profile, unsigned int size) {
	if (!KeepEmbeddedColorSpace() || profile == NULL || size < 128) {
		return false;
	}
	bool bCapture = false;
	try {
		cmsHPROFILE hProfile = cmsOpenProfileFromMem(profile, size);
		if (hProfile != NULL) {
			// sRGB profiles are not kept - these images are handled like images without profile
			bCapture = cmsGetColorSpace(hProfile) == cmsSigRgbData && !::IsSRGBProfile(hProfile, CString());
			if (bCapture) {
				// make sure lcms can use the profile, else convert as before (or show unconverted)
				cmsHPROFILE hSRGB = cmsCreate_sRGBProfile();
				cmsHTRANSFORM hTest = (hSRGB == NULL) ? NULL : cmsCreateTransform(hProfile, TYPE_BGRA_8, hSRGB, TYPE_BGRA_8, INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_BLACKPOINTCOMPENSATION);
				bCapture = hTest != NULL;
				if (hTest != NULL) cmsDeleteTransform(hTest);
				if (hSRGB != NULL) cmsCloseProfile(hSRGB);
			}
			cmsCloseProfile(hProfile);
		}
	} catch (...) {
		bCapture = false;
	}
	if (bCapture) {
		const unsigned char* pBytes = (const unsigned char*)profile;
		t_capturedProfile = std::make_shared<const std::vector<unsigned char>>(pBytes, pBytes + size);
	}
	return bCapture;
}

bool ICCProfileTransform::IsRGBProfile(const void* profile, unsigned int size) {
	if (profile == NULL || size < 128) return false;
	bool bRGB = false;
	try {
		cmsHPROFILE hProfile = cmsOpenProfileFromMem(profile, size);
		if (hProfile != NULL) {
			bRGB = cmsGetColorSpace(hProfile) == cmsSigRgbData;
			cmsCloseProfile(hProfile);
		}
	} catch (...) {}
	return bRGB;
}

bool ICCProfileTransform::IsSRGBProfile(const void* profile, unsigned int size) {
	if (profile == NULL || size < 128) return false;
	bool bSRGB = false;
	try {
		cmsHPROFILE hProfile = cmsOpenProfileFromMem(profile, size);
		if (hProfile != NULL) {
			bSRGB = cmsGetColorSpace(hProfile) == cmsSigRgbData && ::IsSRGBProfile(hProfile, CString());
			cmsCloseProfile(hProfile);
		}
	} catch (...) {}
	return bSRGB;
}

bool ICCProfileTransform::IsAvailable() {
	static volatile int s_nAvailable = -1; // lcms2.dll is checked once
	if (s_nAvailable < 0) {
		bool bAvailable = false;
		try {
			cmsHPROFILE hTest = cmsCreate_sRGBProfile();
			bAvailable = hTest != NULL;
			if (hTest != NULL) cmsCloseProfile(hTest);
		} catch (...) {
			bAvailable = false;
		}
		s_nAvailable = bAvailable ? 1 : 0;
	}
	return s_nAvailable == 1;
}

bool ICCProfileTransform::ConvertToSRGB(const ICCProfilePtr& profile, void* pixels, int width, int height, int bytesPerPixel) {
	if (profile == NULL || profile->empty() || pixels == NULL || width <= 0 || height <= 0 || (bytesPerPixel != 3 && bytesPerPixel != 4)) {
		return false;
	}
	bool bOk = false;
	try {
		cmsHPROFILE hIn = cmsOpenProfileFromMem(profile->data(), (cmsUInt32Number)profile->size());
		cmsHPROFILE hOut = cmsCreate_sRGBProfile();
		if (hIn != NULL && hOut != NULL) {
			cmsUInt32Number format = (bytesPerPixel == 4) ? TYPE_BGRA_8 : TYPE_BGR_8;
			cmsUInt32Number flags = cmsFLAGS_BLACKPOINTCOMPENSATION | ((bytesPerPixel == 4) ? cmsFLAGS_COPY_ALPHA : 0);
			cmsHTRANSFORM transform = cmsCreateTransform(hIn, format, hOut, format, INTENT_RELATIVE_COLORIMETRIC, flags);
			if (transform != NULL) {
				cmsUInt32Number stride = (bytesPerPixel == 4) ? width * 4 : Helpers::DoPadding(width * 3, 4);
				cmsDoTransformLineStride(transform, pixels, pixels, width, height, stride, stride, stride * height, stride * height);
				cmsDeleteTransform(transform);
				bOk = true;
			}
		}
		if (hIn != NULL) cmsCloseProfile(hIn);
		if (hOut != NULL) cmsCloseProfile(hOut);
	} catch (...) {}
	return bOk;
}

ICCProfilePtr ICCProfileTransform::ReencodeToSRGBToneCurve(const ICCProfilePtr& profile, void* pixels, int width, int height, int bytesPerPixel) {
	if (profile == NULL || profile->empty() || pixels == NULL || width <= 0 || height <= 0 || (bytesPerPixel != 3 && bytesPerPixel != 4)) {
		return profile;
	}
	ICCProfilePtr variant = GetSRGBToneCurveVariant(profile);
	if (variant == NULL) {
		return profile; // sRGB tone curve already, lookup table profile, or lcms2 failed: pixels stay as they are
	}
	bool bOk = false;
	try {
		cmsHPROFILE hIn = cmsOpenProfileFromMem(profile->data(), (cmsUInt32Number)profile->size());
		cmsHPROFILE hOut = cmsOpenProfileFromMem(variant->data(), (cmsUInt32Number)variant->size());
		if (hIn != NULL && hOut != NULL) {
			cmsUInt32Number format = (bytesPerPixel == 4) ? TYPE_BGRA_8 : TYPE_BGR_8;
			// Same colorants and white point: only the tone curves are exchanged, no gamut mapping happens
			cmsUInt32Number flags = (bytesPerPixel == 4) ? cmsFLAGS_COPY_ALPHA : 0;
			cmsHTRANSFORM transform = cmsCreateTransform(hIn, format, hOut, format, INTENT_RELATIVE_COLORIMETRIC, flags);
			if (transform != NULL) {
				cmsUInt32Number stride = (bytesPerPixel == 4) ? width * 4 : Helpers::DoPadding(width * 3, 4);
				cmsDoTransformLineStride(transform, pixels, pixels, width, height, stride, stride, stride * height, stride * height);
				cmsDeleteTransform(transform);
				bOk = true;
			}
		}
		if (hIn != NULL) cmsCloseProfile(hIn);
		if (hOut != NULL) cmsCloseProfile(hOut);
	} catch (...) {
		bOk = false;
	}
	return bOk ? variant : profile;
}

CString ICCProfileTransform::GetProfileName(const ICCProfilePtr& profile) {
	CString sName;
	if (profile == NULL || profile->empty()) return sName;
	try {
		cmsHPROFILE hProfile = cmsOpenProfileFromMem(profile->data(), (cmsUInt32Number)profile->size());
		if (hProfile != NULL) {
			sName = GetProfileDisplayName(hProfile, CString());
			cmsCloseProfile(hProfile);
		}
	} catch (...) {}
	return sName;
}

bool ICCProfileTransform::IsCMYKProfile(const void* profile, unsigned int size) {
	if (profile == NULL || size < 128) return false;
	bool bCMYK = false;
	try {
		cmsHPROFILE hProfile = cmsOpenProfileFromMem(profile, size);
		if (hProfile != NULL) {
			bCMYK = cmsGetColorSpace(hProfile) == cmsSigCmykData;
			cmsCloseProfile(hProfile);
		}
	} catch (...) {}
	return bCMYK;
}

void* ICCProfileTransform::CreateWideGamutTransform(const void* profile, unsigned int size, PixelFormat format) {
	if (!KeepEmbeddedColorSpace()) {
		return NULL;
	}
	cmsHTRANSFORM transform = NULL;
	try {
		ICCProfilePtr wideGamutProfile = GetWideGamutProfile();
		if (wideGamutProfile == NULL) {
			return NULL;
		}
		cmsUInt32Number inFormat, outFormat;
		cmsUInt32Number flags = cmsFLAGS_BLACKPOINTCOMPENSATION;
		cmsHPROFILE hIn = NULL;
		switch (format) {
			case FORMAT_CMYK:
			case FORMAT_CMYK_INVERTED:
				if (profile == NULL || size < 128) return NULL;
				hIn = cmsOpenProfileFromMem(profile, size);
				if (hIn != NULL && cmsGetColorSpace(hIn) != cmsSigCmykData) {
					cmsCloseProfile(hIn);
					return NULL;
				}
				inFormat = (format == FORMAT_CMYK) ? TYPE_CMYK_8 : TYPE_CMYK_8_REV;
				outFormat = TYPE_BGR_8;
				break;
			case FORMAT_Lab:
				hIn = cmsCreateLab4Profile(cmsD50_xyY());
				inFormat = TYPE_Lab_8;
				outFormat = TYPE_BGR_8;
				break;
			case FORMAT_LabA:
				hIn = cmsCreateLab4Profile(cmsD50_xyY());
				inFormat = TYPE_LabA_8;
				outFormat = TYPE_BGRA_8;
				flags |= cmsFLAGS_COPY_ALPHA;
				break;
			default:
				return NULL;
		}
		cmsHPROFILE hOut = cmsOpenProfileFromMem(wideGamutProfile->data(), (cmsUInt32Number)wideGamutProfile->size());
		if (hIn != NULL && hOut != NULL) {
			transform = cmsCreateTransform(hIn, inFormat, hOut, outFormat, INTENT_RELATIVE_COLORIMETRIC, flags);
		}
		if (hIn != NULL) cmsCloseProfile(hIn);
		if (hOut != NULL) cmsCloseProfile(hOut);
	} catch (...) {
		transform = NULL;
	}
	return transform;
}

bool ICCProfileTransform::CaptureWideGamutProfile() {
	if (!KeepEmbeddedColorSpace()) {
		return false;
	}
	ICCProfilePtr wideGamutProfile = GetWideGamutProfile();
	if (wideGamutProfile == NULL) {
		return false;
	}
	t_capturedProfile = wideGamutProfile;
	return true;
}

std::vector<unsigned char> ICCProfileTransform::CreateProfileFromCICP(int colorPrimaries, int transferCharacteristics) {
	std::vector<unsigned char> data;

	// Transfer characteristics. The BT.709 family (camera OETFs) is treated as sRGB, as other image viewers and
	// browsers do in practice, so the common 1/1/x (BT.709) tagged images look exactly as before.
	enum { CURVE_SRGB, CURVE_GAMMA22, CURVE_GAMMA28, CURVE_LINEAR } eCurve;
	const wchar_t* sCurveName;
	switch (transferCharacteristics) {
		case 0: // unknown
		case 1: // BT.709
		case 2: // unspecified
		case 6: // BT.601
		case 13: // sRGB (IEC 61966-2-1)
		case 14: // BT.2020 10 bit
		case 15: // BT.2020 12 bit
			eCurve = CURVE_SRGB; sCurveName = L"sRGB";
			break;
		case 4: // BT.470 System M
			eCurve = CURVE_GAMMA22; sCurveName = L"gamma 2.2";
			break;
		case 5: // BT.470 System B, G
			eCurve = CURVE_GAMMA28; sCurveName = L"gamma 2.8";
			break;
		case 8:
			eCurve = CURVE_LINEAR; sCurveName = L"linear";
			break;
		default:
			return data; // HDR (PQ, HLG) and rarely used transfer functions: not handled, shown as before
	}

	// Color primaries and white point
	cmsCIExyY whitePoint = cD65;
	cmsCIExyYTRIPLE primaries;
	const wchar_t* sPrimariesName;
	switch (colorPrimaries) {
		case 0: // unknown
		case 1: // BT.709, sRGB
		case 2: // unspecified
			if (eCurve == CURVE_SRGB) {
				return data; // sRGB, nothing to do
			}
			primaries = { { 0.640, 0.330, 1.0 }, { 0.300, 0.600, 1.0 }, { 0.150, 0.060, 1.0 } };
			sPrimariesName = L"BT.709";
			break;
		case 4: // BT.470 System M
			primaries = { { 0.670, 0.330, 1.0 }, { 0.210, 0.710, 1.0 }, { 0.140, 0.080, 1.0 } };
			whitePoint = { 0.310, 0.316, 1.0 }; // illuminant C
			sPrimariesName = L"BT.470 M";
			break;
		case 5: // BT.470 System B, G
			primaries = { { 0.640, 0.330, 1.0 }, { 0.290, 0.600, 1.0 }, { 0.150, 0.060, 1.0 } };
			sPrimariesName = L"BT.470 BG";
			break;
		case 6: // BT.601
		case 7: // SMPTE 240M
			primaries = { { 0.630, 0.340, 1.0 }, { 0.310, 0.595, 1.0 }, { 0.155, 0.070, 1.0 } };
			sPrimariesName = L"BT.601";
			break;
		case 9: // BT.2020, BT.2100
			primaries = { { 0.708, 0.292, 1.0 }, { 0.170, 0.797, 1.0 }, { 0.131, 0.046, 1.0 } };
			sPrimariesName = L"BT.2020";
			break;
		case 11: // SMPTE RP 431-2 (DCI-P3)
			primaries = { { 0.680, 0.320, 1.0 }, { 0.265, 0.690, 1.0 }, { 0.150, 0.060, 1.0 } };
			whitePoint = { 0.314, 0.351, 1.0 };
			sPrimariesName = L"DCI-P3";
			break;
		case 12: // SMPTE EG 432-1 (Display P3)
			primaries = { { 0.680, 0.320, 1.0 }, { 0.265, 0.690, 1.0 }, { 0.150, 0.060, 1.0 } };
			sPrimariesName = L"Display P3";
			break;
		case 22: // EBU Tech. 3213-E
			primaries = { { 0.630, 0.340, 1.0 }, { 0.295, 0.605, 1.0 }, { 0.155, 0.077, 1.0 } };
			sPrimariesName = L"EBU 3213-E";
			break;
		default:
			return data; // generic film, XYZ and unknown values: not handled, shown as before
	}

	try {
		cmsToneCurve* curve = NULL;
		switch (eCurve) {
			case CURVE_SRGB: curve = CreateSRGBToneCurve(); break;
			case CURVE_GAMMA22: curve = CreateGammaToneCurve(2.2); break;
			case CURVE_GAMMA28: curve = CreateGammaToneCurve(2.8); break;
			case CURVE_LINEAR: curve = CreateGammaToneCurve(1.0); break;
		}
		CString sDescription;
		sDescription.Format(_T("%ls, %ls tone curve (CICP %d/%d)"), sPrimariesName, sCurveName, colorPrimaries, transferCharacteristics);
		data = CreateRGBProfileData(whitePoint, primaries, curve, CT2CW(sDescription));
	} catch (...) {
		data.clear(); // lcms2.dll not found
	}
	return data;
}

///////////////////////////////////////////////////////////////////////////////////
// CDisplayColorProfile
///////////////////////////////////////////////////////////////////////////////////

void* CDisplayColorProfile::sm_transform = NULL;
std::vector<unsigned char> CDisplayColorProfile::sm_displayProfile;
bool CDisplayColorProfile::sm_bEnabled = false;
std::vector<CDisplayColorProfile::EmbeddedTransform> CDisplayColorProfile::sm_embeddedTransforms;
HMONITOR CDisplayColorProfile::sm_hMonitor = NULL;
bool CDisplayColorProfile::sm_bValid = false;
unsigned int CDisplayColorProfile::sm_nTransformGeneration = 1;
CString CDisplayColorProfile::sm_sProfileName;

bool CDisplayColorProfile::Update(HWND hWnd) {
	HMONITOR hMonitor = ::MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
	if (sm_bValid && hMonitor == sm_hMonitor) {
		return false;
	}
	bool bWasActive = sm_transform != NULL;
	CString sOldProfileName = sm_sProfileName;

	DeleteDisplayTransform();
	sm_hMonitor = hMonitor;
	sm_bValid = true;
	CreateDisplayTransform(hMonitor);
	sm_nTransformGeneration++;
	UpdateRenderState();

	return bWasActive != (sm_transform != NULL) || sOldProfileName != sm_sProfileName;
}

void CDisplayColorProfile::Invalidate() {
	sm_bValid = false;
}

bool CDisplayColorProfile::IsActive() {
	return sm_transform != NULL;
}

void* CDisplayColorProfile::GetTransform(const ICCProfilePtr& profile) {
	if (profile == NULL || profile->empty()) {
		return sm_transform; // image pixels are sRGB
	}
	if (!sm_bEnabled) {
		return NULL;
	}
	for (size_t i = 0; i < sm_embeddedTransforms.size(); i++) {
		const ICCProfilePtr& cached = sm_embeddedTransforms[i].profile;
		if (cached == profile || *cached == *profile) {
			return sm_embeddedTransforms[i].transform;
		}
	}

	// Embedded profile to display profile, or to sRGB if the display is sRGB (or color managed by Windows)
	cmsHTRANSFORM transform = NULL;
	try {
		cmsHPROFILE hIn = cmsOpenProfileFromMem(profile->data(), (cmsUInt32Number)profile->size());
		cmsHPROFILE hOut = sm_displayProfile.empty() ? cmsCreate_sRGBProfile() :
			cmsOpenProfileFromMem(sm_displayProfile.data(), (cmsUInt32Number)sm_displayProfile.size());
		if (hIn != NULL && hOut != NULL) {
			// cmsFLAGS_NOCACHE: the transform is used on several threads in parallel
			cmsUInt32Number flags = cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_COPY_ALPHA | cmsFLAGS_NOCACHE;
			transform = cmsCreateTransform(hIn, TYPE_BGRA_8, hOut, TYPE_BGRA_8, INTENT_RELATIVE_COLORIMETRIC, flags);
		}
		if (hIn != NULL) cmsCloseProfile(hIn);
		if (hOut != NULL) cmsCloseProfile(hOut);
	} catch (...) {
		transform = NULL;
	}

	const size_t cnMaxCachedTransforms = 8;
	if (sm_embeddedTransforms.size() >= cnMaxCachedTransforms) {
		if (sm_embeddedTransforms.front().transform != NULL) {
			cmsDeleteTransform(sm_embeddedTransforms.front().transform);
		}
		sm_embeddedTransforms.erase(sm_embeddedTransforms.begin());
		sm_nTransformGeneration++; // a deleted transform must not be mistaken for a new one at the same address
	}
	EmbeddedTransform entry;
	entry.profile = profile;
	entry.transform = transform; // also cache failures to not retry on every paint
	sm_embeddedTransforms.push_back(entry);
	return transform;
}

bool CDisplayColorProfile::Apply(void* transform, const void* pSource, void* pTarget, int nWidth, int nHeight) {
	if (transform == NULL || pSource == NULL || pTarget == NULL || nWidth <= 0 || nHeight <= 0) {
		return false;
	}
	CDisplayTransformRequest request(transform, pSource, pTarget, CSize(nWidth, nHeight));
	return CProcessingThreadPool::This().Process(&request);
}

void CDisplayColorProfile::DeleteDisplayTransform() {
	if (sm_transform != NULL) {
		cmsDeleteTransform(sm_transform);
		sm_transform = NULL;
	}
	for (size_t i = 0; i < sm_embeddedTransforms.size(); i++) {
		if (sm_embeddedTransforms[i].transform != NULL) {
			cmsDeleteTransform(sm_embeddedTransforms[i].transform);
		}
	}
	sm_embeddedTransforms.clear();
	sm_displayProfile.clear();
	sm_bEnabled = false;
	sm_sProfileName.Empty();
}

void CDisplayColorProfile::CreateDisplayTransform(HMONITOR hMonitor) {
	if (!IsDisplayColorManagementEnabled()) {
		return; // display color management disabled
	}
	// From here on the display is treated as sRGB unless a usable display profile is found:
	// images with embedded profiles are then converted to sRGB when painted
	sm_bEnabled = true;
	CString sSetting = CSettingsProvider::This().DisplayColorProfile();
	sSetting.Trim();

	MONITORINFOEX monitorInfo;
	memset(&monitorInfo, 0, sizeof(monitorInfo));
	monitorInfo.cbSize = sizeof(monitorInfo);
	if (hMonitor == NULL || !::GetMonitorInfo(hMonitor, &monitorInfo)) {
		return;
	}

	CString sProfilePath;
	bool bAuto = sSetting.CompareNoCase(_T("auto")) == 0;
	if (bAuto) {
		if (IsAdvancedColorActive(monitorInfo.szDevice)) {
			return; // Windows color manages this display (HDR or Auto Color Management)
		}
		if (!GetSystemMonitorProfile(monitorInfo.szDevice, sProfilePath)) {
			return;
		}
	} else {
		sProfilePath = sSetting;
		bool bAbsolutePath = (sProfilePath.GetLength() >= 2 && sProfilePath[1] == _T(':')) || sProfilePath[0] == _T('\\');
		if (!bAbsolutePath) {
			sProfilePath = CSettingsProvider::This().GetEXEPath() + sProfilePath;
		}
	}

	std::vector<unsigned char> profileData;
	if (!ReadProfileFile(sProfilePath, profileData)) {
		return; // profile not found
	}
	cmsHPROFILE hDisplayProfile = NULL;
	cmsHPROFILE hSRGBProfile = NULL;
	try {
		hDisplayProfile = cmsOpenProfileFromMem(profileData.data(), (cmsUInt32Number)profileData.size());
		if (hDisplayProfile == NULL) {
			return; // invalid profile
		}
		if (cmsGetColorSpace(hDisplayProfile) != cmsSigRgbData || IsSRGBProfile(hDisplayProfile, sProfilePath)) {
			// not usable as display profile or identity transform for sRGB images
			cmsCloseProfile(hDisplayProfile);
			return;
		}
		hSRGBProfile = cmsCreate_sRGBProfile();
		if (hSRGBProfile != NULL) {
			// cmsFLAGS_NOCACHE: the transform is used on several threads in parallel
			cmsUInt32Number flags = cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_COPY_ALPHA | cmsFLAGS_NOCACHE;
			sm_transform = cmsCreateTransform(hSRGBProfile, TYPE_BGRA_8, hDisplayProfile, TYPE_BGRA_8, INTENT_RELATIVE_COLORIMETRIC, flags);
			if (sm_transform != NULL) {
				sm_sProfileName = GetProfileDisplayName(hDisplayProfile, sProfilePath);
				sm_displayProfile.swap(profileData);
			}
			cmsCloseProfile(hSRGBProfile);
		}
		cmsCloseProfile(hDisplayProfile);
	} catch (...) {
		// lcms2.dll not found
		sm_transform = NULL;
		sm_bEnabled = false;
	}
}

///////////////////////////////////////////////////////////////////////////////////
// CDisplayRenderTransform, CDisplayColorProfile::GetRenderTransform()
///////////////////////////////////////////////////////////////////////////////////

CDisplayRenderTransform::~CDisplayRenderTransform() {
	if (m_transform != NULL) {
		cmsDeleteTransform(m_transform);
	}
}

void CDisplayRenderTransform::Convert(float* pSource, unsigned char* pTarget, int nPixels) const {
	if (nPixels <= 0) {
		return;
	}
	if (m_bEncodeSRGB) {
		for (int i = 0; i < nPixels * 3; i++) {
			float v = pSource[i];
			pSource[i] = (v <= 0.0031308f) ? 12.92f * v : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
		}
	}
	cmsDoTransform(m_transform, pSource, pTarget, (cmsUInt32Number)nPixels);
	// lcms does not write the alpha channel of the output, it has no alpha channel in the input
	unsigned int* pPixel = (unsigned int*)pTarget;
	for (int i = 0; i < nPixels; i++) {
		pPixel[i] |= 0xFF000000;
	}
}

namespace {
	// State of the display for CDisplayColorProfile::GetRenderTransform(), which can be called from any thread.
	// Written by CDisplayColorProfile::UpdateRenderState() on the UI thread, everything protected by s_renderMutex.
	std::mutex s_renderMutex;
	bool s_bRenderStateValid = false;
	bool s_bRenderEnabled = false; // display color management is on (CDisplayColorProfile::sm_bEnabled)
	bool s_bRenderSRGBToDisplay = false; // sRGB images need a conversion (CDisplayColorProfile::sm_transform != NULL)
	std::shared_ptr<const std::vector<unsigned char>> s_renderDisplayProfile; // empty if the display is treated as sRGB
	// Cache of the created transforms (also failed attempts, as empty pointer), cleared when the display changes
	std::vector<std::pair<ICCProfilePtr, DisplayRenderTransformPtr>> s_renderTransforms;
	const size_t RENDER_TRANSFORM_CACHE_SIZE = 8;

	bool IsSameProfile(const ICCProfilePtr& profile1, const ICCProfilePtr& profile2) {
		bool bEmpty1 = profile1 == NULL || profile1->empty();
		bool bEmpty2 = profile2 == NULL || profile2->empty();
		if (bEmpty1 || bEmpty2) {
			return bEmpty1 == bEmpty2;
		}
		return profile1 == profile2 || *profile1 == *profile2;
	}

	// Creates the transform from the linear light output of the resampler (in the color space of the given profile,
	// sRGB if empty) to the given display profile (sRGB if empty)
	DisplayRenderTransformPtr CreateRenderTransform(const ICCProfilePtr& profile, const std::shared_ptr<const std::vector<unsigned char>>& displayProfile) {
		cmsHPROFILE hSource = NULL;
		cmsHPROFILE hIn = NULL;
		cmsHPROFILE hOut = NULL;
		cmsHTRANSFORM transform = NULL;
		bool bEncodeSRGB = false;
		try {
			if (profile == NULL || profile->empty()) {
				hSource = cmsCreate_sRGBProfile();
			} else {
				hSource = cmsOpenProfileFromMem(profile->data(), (cmsUInt32Number)profile->size());
			}
			if (hSource != NULL) {
				// The resampler decodes all pixels with the sRGB tone curve. For matrix/TRC profiles with the sRGB tone curve
				// (all of them after ICCProfileTransform::ReencodeToSRGBToneCurve()) its output is converted with the same
				// profile with a linear tone curve.
				if (GetMatrixShaperToneCurveType(hSource) == 1) {
					std::vector<unsigned char> linearProfile = CreateToneCurveVariant(hSource, CreateGammaToneCurve(1.0), _T(" (linear)"));
					if (!linearProfile.empty()) {
						hIn = cmsOpenProfileFromMem(linearProfile.data(), (cmsUInt32Number)linearProfile.size());
					}
				}
				if (hIn == NULL) {
					// Profile with lookup tables: the output is encoded with the sRGB tone curve again (in floating point)
					// and converted with the profile itself
					hIn = hSource;
					hSource = NULL;
					bEncodeSRGB = true;
				}
			}
			hOut = (displayProfile == NULL || displayProfile->empty()) ? cmsCreate_sRGBProfile() :
				cmsOpenProfileFromMem(displayProfile->data(), (cmsUInt32Number)displayProfile->size());
			if (hIn != NULL && hOut != NULL) {
				// Same intent and flags as the transforms applied when painting (see CDisplayColorProfile::GetTransform()).
				// cmsFLAGS_NOCACHE: the transform is used on several threads in parallel
				cmsUInt32Number flags = cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_NOCACHE;
				transform = cmsCreateTransform(hIn, TYPE_BGR_FLT, hOut, TYPE_BGRA_8, INTENT_RELATIVE_COLORIMETRIC, flags);
			}
		} catch (...) {
			transform = NULL;
		}
		if (hSource != NULL) cmsCloseProfile(hSource);
		if (hIn != NULL) cmsCloseProfile(hIn);
		if (hOut != NULL) cmsCloseProfile(hOut);
		if (transform == NULL) {
			return DisplayRenderTransformPtr();
		}
		return DisplayRenderTransformPtr(new CDisplayRenderTransform(transform, bEncodeSRGB));
	}
}

void CDisplayColorProfile::UpdateRenderState() {
	std::lock_guard<std::mutex> lock(s_renderMutex);
	bool bSRGBToDisplay = sm_transform != NULL;
	bool bSameDisplayProfile = sm_displayProfile.empty() ? (s_renderDisplayProfile == NULL) :
		(s_renderDisplayProfile != NULL && *s_renderDisplayProfile == sm_displayProfile);
	if (s_bRenderStateValid && s_bRenderEnabled == sm_bEnabled && s_bRenderSRGBToDisplay == bSRGBToDisplay && bSameDisplayProfile) {
		return; // nothing changed (e.g. reactivation of the application), keep the transforms - the images need not be resampled again
	}
	s_bRenderStateValid = true;
	s_bRenderEnabled = sm_bEnabled;
	s_bRenderSRGBToDisplay = bSRGBToDisplay;
	if (sm_displayProfile.empty()) {
		s_renderDisplayProfile.reset();
	} else {
		s_renderDisplayProfile = std::make_shared<const std::vector<unsigned char>>(sm_displayProfile);
	}
	s_renderTransforms.clear(); // transforms still used by an image stay alive until it is resampled
}

DisplayRenderTransformPtr CDisplayColorProfile::GetRenderTransform(const ICCProfilePtr& profile) {
	std::shared_ptr<const std::vector<unsigned char>> displayProfile;
	{
		std::lock_guard<std::mutex> lock(s_renderMutex);
		if (!s_bRenderStateValid || !s_bRenderEnabled) {
			return DisplayRenderTransformPtr();
		}
		bool bHasProfile = profile != NULL && !profile->empty();
		if (!bHasProfile && !s_bRenderSRGBToDisplay) {
			return DisplayRenderTransformPtr(); // sRGB image on a display treated as sRGB
		}
		for (size_t i = 0; i < s_renderTransforms.size(); i++) {
			if (IsSameProfile(s_renderTransforms[i].first, profile)) {
				return s_renderTransforms[i].second;
			}
		}
		displayProfile = s_renderDisplayProfile;
	}

	// Creating the transform takes some time, it is done without holding the lock
	DisplayRenderTransformPtr transform = CreateRenderTransform(profile, displayProfile);

	std::lock_guard<std::mutex> lock(s_renderMutex);
	if (s_renderDisplayProfile != displayProfile) {
		return transform; // the display changed meanwhile, the transform is not cached
	}
	for (size_t i = 0; i < s_renderTransforms.size(); i++) {
		if (IsSameProfile(s_renderTransforms[i].first, profile)) {
			return s_renderTransforms[i].second; // created by another thread meanwhile, all images shall use the same one
		}
	}
	if (s_renderTransforms.size() >= RENDER_TRANSFORM_CACHE_SIZE) {
		s_renderTransforms.erase(s_renderTransforms.begin());
	}
	s_renderTransforms.push_back(std::make_pair(profile, transform));
	return transform;
}

#else

// stub out lcms2 methods in an elegant way in XP build, as per suggestion https://github.com/sylikc/jpegview/commit/4b62f07e2a147a04a5014a5711d159670162e799#commitcomment-102738193

void* ICCProfileTransform::CreateTransform(const void* /* profile */, unsigned int /* size */, PixelFormat /* format */) {
	return NULL;
}

bool ICCProfileTransform::DoTransform(void* /* transform */, const void* /* inputBuffer */, void* /* outputBuffer */, unsigned int /* width */, unsigned int /* height */, unsigned int /* stride */) {
	return false;
}

void ICCProfileTransform::DeleteTransform(void* /* transform */) { }

void* ICCProfileTransform::CreateLabTransform(PixelFormat /* format */) {
	return NULL;
}

bool ICCProfileTransform::KeepEmbeddedColorSpace() { return false; }
void ICCProfileTransform::ResetCapturedProfile() { }
ICCProfilePtr ICCProfileTransform::GetCapturedProfile() { return ICCProfilePtr(); }
void ICCProfileTransform::SetCapturedProfile(const ICCProfilePtr& /* profile */) { }
bool ICCProfileTransform::CaptureProfile(const void* /* profile */, unsigned int /* size */) { return false; }
bool ICCProfileTransform::IsRGBProfile(const void* /* profile */, unsigned int /* size */) { return false; }
bool ICCProfileTransform::IsSRGBProfile(const void* /* profile */, unsigned int /* size */) { return false; }
bool ICCProfileTransform::IsAvailable() { return false; }
bool ICCProfileTransform::ConvertToSRGB(const ICCProfilePtr& /* profile */, void* /* pixels */, int /* width */, int /* height */, int /* bytesPerPixel */) { return false; }
ICCProfilePtr ICCProfileTransform::ReencodeToSRGBToneCurve(const ICCProfilePtr& profile, void* /* pixels */, int /* width */, int /* height */, int /* bytesPerPixel */) { return profile; }
CString ICCProfileTransform::GetProfileName(const ICCProfilePtr& /* profile */) { return CString(); }
bool ICCProfileTransform::IsCMYKProfile(const void* /* profile */, unsigned int /* size */) { return false; }
void* ICCProfileTransform::CreateWideGamutTransform(const void* /* profile */, unsigned int /* size */, PixelFormat /* format */) { return NULL; }
bool ICCProfileTransform::CaptureWideGamutProfile() { return false; }
std::vector<unsigned char> ICCProfileTransform::CreateProfileFromCICP(int /* colorPrimaries */, int /* transferCharacteristics */) { return std::vector<unsigned char>(); }

void* CDisplayColorProfile::sm_transform = NULL;
std::vector<unsigned char> CDisplayColorProfile::sm_displayProfile;
bool CDisplayColorProfile::sm_bEnabled = false;
std::vector<CDisplayColorProfile::EmbeddedTransform> CDisplayColorProfile::sm_embeddedTransforms;
HMONITOR CDisplayColorProfile::sm_hMonitor = NULL;
bool CDisplayColorProfile::sm_bValid = false;
unsigned int CDisplayColorProfile::sm_nTransformGeneration = 1;
CString CDisplayColorProfile::sm_sProfileName;

bool CDisplayColorProfile::Update(HWND /* hWnd */) { return false; }
void CDisplayColorProfile::Invalidate() { }
bool CDisplayColorProfile::IsActive() { return false; }
void* CDisplayColorProfile::GetTransform(const ICCProfilePtr& /* profile */) { return NULL; }
bool CDisplayColorProfile::Apply(void* /* transform */, const void* /* pSource */, void* /* pTarget */, int /* nWidth */, int /* nHeight */) { return false; }
void CDisplayColorProfile::DeleteDisplayTransform() { }
void CDisplayColorProfile::CreateDisplayTransform(HMONITOR /* hMonitor */) { }
void CDisplayColorProfile::UpdateRenderState() { }
DisplayRenderTransformPtr CDisplayColorProfile::GetRenderTransform(const ICCProfilePtr& /* profile */) { return DisplayRenderTransformPtr(); }
CDisplayRenderTransform::~CDisplayRenderTransform() { }
void CDisplayRenderTransform::Convert(float* /* pSource */, unsigned char* /* pTarget */, int /* nPixels */) const { }

#endif

///////////////////////////////////////////////////////////////////////////////////
// CDisplayColorDIBCache
///////////////////////////////////////////////////////////////////////////////////

CDisplayColorDIBCache::CDisplayColorDIBCache() {
	m_pSourceDIB = NULL;
	m_size = CSize(0, 0);
	m_nDIBGeneration = 0;
	m_nTransformGeneration = 0;
	m_pTransform = NULL;
	m_pConverted = NULL;
	m_nAllocatedPixels = 0;
}

CDisplayColorDIBCache::~CDisplayColorDIBCache() {
	Clear();
}

void CDisplayColorDIBCache::Clear() {
	delete[] m_pConverted;
	m_pConverted = NULL;
	m_nAllocatedPixels = 0;
	m_pSourceDIB = NULL;
}

void* CDisplayColorDIBCache::Convert(void* pDIB, CSize dibSize, unsigned int nDIBGeneration, const ICCProfilePtr& profile) {
	void* transform = (pDIB == NULL) ? NULL : CDisplayColorProfile::GetTransform(profile);
	if (transform == NULL || dibSize.cx <= 0 || dibSize.cy <= 0) {
		if (m_pConverted != NULL) Clear();
		return pDIB;
	}
	unsigned int nTransformGeneration = CDisplayColorProfile::TransformGeneration();
	if (m_pConverted != NULL && pDIB == m_pSourceDIB && dibSize == m_size && transform == m_pTransform &&
		nDIBGeneration == m_nDIBGeneration && nTransformGeneration == m_nTransformGeneration) {
		return m_pConverted; // up to date
	}

	int nPixels = dibSize.cx * dibSize.cy;
	if (m_pConverted == NULL || nPixels > m_nAllocatedPixels) {
		delete[] m_pConverted;
		m_pConverted = new(std::nothrow) unsigned int[nPixels];
		m_nAllocatedPixels = (m_pConverted == NULL) ? 0 : nPixels;
	}
	if (m_pConverted == NULL || !CDisplayColorProfile::Apply(transform, pDIB, m_pConverted, dibSize.cx, dibSize.cy)) {
		Clear();
		return pDIB; // out of memory or transform failed - show unconverted pixels
	}
	m_pSourceDIB = pDIB;
	m_size = dibSize;
	m_nDIBGeneration = nDIBGeneration;
	m_nTransformGeneration = nTransformGeneration;
	m_pTransform = transform;
	return m_pConverted;
}
