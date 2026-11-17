#include "stdafx.h"

#include "ICCProfileTransform.h"
#include "SettingsProvider.h"
#include "ProcessingThreadPool.h"
#include <vector>


#ifndef WINXP

// This define is necessary for 32-bit builds to work, for some reason
#define CMS_DLL
#include "lcms2.h"
#define TYPE_LabA_8 (COLORSPACE_SH(PT_Lab)|EXTRA_SH(1)|CHANNELS_SH(3)|BYTES_SH(1))


void* ICCProfileTransform::sRGBProfile = NULL;

void* ICCProfileTransform::CreateTransform(const void* profile, unsigned int size, PixelFormat format)
{
	if (profile == NULL || size == 0)
		return NULL; // No ICC Profile
	if (sRGBProfile == NULL) {
		try {
			sRGBProfile = cmsCreate_sRGBProfile();
		} catch (...) {}
	}
	if (sRGBProfile == NULL)
		return NULL; // lcms2.dll not found or out of memory
	cmsHPROFILE hInProfile = cmsOpenProfileFromMem(profile, size);
	if (hInProfile == NULL)
		return NULL; // Invalid profile

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
	if (stride == 0)
		stride = width * nchannels;
	cmsDoTransformLineStride(transform, inputBuffer, outputBuffer, width, height, stride, Helpers::DoPadding(width * nchannels, 4), stride * height, Helpers::DoPadding(width * nchannels, 4) * height);
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
	cmsHPROFILE OpenProfileFromFile(LPCTSTR sFileName) {
		HANDLE hFile = ::CreateFile(sFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile == INVALID_HANDLE_VALUE) return NULL;
		cmsHPROFILE hProfile = NULL;
		LARGE_INTEGER fileSize;
		if (::GetFileSizeEx(hFile, &fileSize) && fileSize.QuadPart > 128 && fileSize.QuadPart < 64 * 1024 * 1024) {
			std::vector<char> data((size_t)fileSize.QuadPart);
			DWORD nRead = 0;
			if (::ReadFile(hFile, data.data(), (DWORD)data.size(), &nRead, NULL) && nRead == data.size()) {
				hProfile = cmsOpenProfileFromMem(data.data(), nRead);
			}
		}
		::CloseHandle(hFile);
		return hProfile;
	}

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
}

void* CDisplayColorProfile::sm_transform = NULL;
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

	return bWasActive != (sm_transform != NULL) || sOldProfileName != sm_sProfileName;
}

void CDisplayColorProfile::Invalidate() {
	sm_bValid = false;
}

bool CDisplayColorProfile::IsActive() {
	return sm_transform != NULL;
}

bool CDisplayColorProfile::Apply(const void* pSource, void* pTarget, int nWidth, int nHeight) {
	if (sm_transform == NULL || pSource == NULL || pTarget == NULL || nWidth <= 0 || nHeight <= 0) {
		return false;
	}
	CDisplayTransformRequest request(sm_transform, pSource, pTarget, CSize(nWidth, nHeight));
	return CProcessingThreadPool::This().Process(&request);
}

void CDisplayColorProfile::DeleteDisplayTransform() {
	if (sm_transform != NULL) {
		cmsDeleteTransform(sm_transform);
		sm_transform = NULL;
	}
	sm_sProfileName.Empty();
}

void CDisplayColorProfile::CreateDisplayTransform(HMONITOR hMonitor) {
	CString sSetting = CSettingsProvider::This().DisplayColorProfile();
	sSetting.Trim();
	if (sSetting.IsEmpty() || sSetting.CompareNoCase(_T("none")) == 0 || sSetting.CompareNoCase(_T("false")) == 0) {
		return; // display color management disabled
	}

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

	cmsHPROFILE hDisplayProfile = NULL;
	cmsHPROFILE hSRGBProfile = NULL;
	try {
		hDisplayProfile = OpenProfileFromFile(sProfilePath);
		if (hDisplayProfile == NULL) {
			return; // profile not found or invalid
		}
		if (cmsGetColorSpace(hDisplayProfile) != cmsSigRgbData || IsSRGBProfile(hDisplayProfile, sProfilePath)) {
			// not usable as display profile or identity transform - nothing to do
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
			}
			cmsCloseProfile(hSRGBProfile);
		}
		cmsCloseProfile(hDisplayProfile);
	} catch (...) {
		// lcms2.dll not found
		sm_transform = NULL;
	}
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

void* CDisplayColorProfile::sm_transform = NULL;
HMONITOR CDisplayColorProfile::sm_hMonitor = NULL;
bool CDisplayColorProfile::sm_bValid = false;
unsigned int CDisplayColorProfile::sm_nTransformGeneration = 1;
CString CDisplayColorProfile::sm_sProfileName;

bool CDisplayColorProfile::Update(HWND /* hWnd */) { return false; }
void CDisplayColorProfile::Invalidate() { }
bool CDisplayColorProfile::IsActive() { return false; }
bool CDisplayColorProfile::Apply(const void* /* pSource */, void* /* pTarget */, int /* nWidth */, int /* nHeight */) { return false; }
void CDisplayColorProfile::DeleteDisplayTransform() { }
void CDisplayColorProfile::CreateDisplayTransform(HMONITOR /* hMonitor */) { }

#endif

///////////////////////////////////////////////////////////////////////////////////
// CDisplayColorDIBCache
///////////////////////////////////////////////////////////////////////////////////

CDisplayColorDIBCache::CDisplayColorDIBCache() {
	m_pSourceDIB = NULL;
	m_size = CSize(0, 0);
	m_nDIBGeneration = 0;
	m_nTransformGeneration = 0;
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

void* CDisplayColorDIBCache::Convert(void* pDIB, CSize dibSize, unsigned int nDIBGeneration) {
	if (pDIB == NULL || !CDisplayColorProfile::IsActive() || dibSize.cx <= 0 || dibSize.cy <= 0) {
		if (m_pConverted != NULL) Clear();
		return pDIB;
	}
	unsigned int nTransformGeneration = CDisplayColorProfile::TransformGeneration();
	if (m_pConverted != NULL && pDIB == m_pSourceDIB && dibSize == m_size &&
		nDIBGeneration == m_nDIBGeneration && nTransformGeneration == m_nTransformGeneration) {
		return m_pConverted; // up to date
	}

	int nPixels = dibSize.cx * dibSize.cy;
	if (m_pConverted == NULL || nPixels > m_nAllocatedPixels) {
		delete[] m_pConverted;
		m_pConverted = new(std::nothrow) unsigned int[nPixels];
		m_nAllocatedPixels = (m_pConverted == NULL) ? 0 : nPixels;
	}
	if (m_pConverted == NULL || !CDisplayColorProfile::Apply(pDIB, m_pConverted, dibSize.cx, dibSize.cy)) {
		Clear();
		return pDIB; // out of memory or transform failed - show unconverted pixels
	}
	m_pSourceDIB = pDIB;
	m_size = dibSize;
	m_nDIBGeneration = nDIBGeneration;
	m_nTransformGeneration = nTransformGeneration;
	return m_pConverted;
}
