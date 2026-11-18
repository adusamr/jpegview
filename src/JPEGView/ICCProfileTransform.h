#pragma once

#include <memory>
#include <vector>

// Raw bytes of an RGB ICC profile embedded in an image. Shared between an image, its thumbnails and the loader.
typedef std::shared_ptr<const std::vector<unsigned char>> ICCProfilePtr;

class ICCProfileTransform
{
public:
	enum PixelFormat {
		FORMAT_BGRA,
		FORMAT_RGBA,
		FORMAT_BGR,
		FORMAT_RGB,
		FORMAT_Lab,
		FORMAT_LabA,
	};

	// Create a transform from given ICC Profile to standard sRGB color space.
	// If KeepEmbeddedColorSpace() is true and the profile is a (non sRGB) RGB profile, no transform is created:
	// the profile is stored in the per thread capture slot (see GetCapturedProfile()) and NULL is returned,
	// so the pixels stay in the color space of the embedded profile.
	static void* CreateTransform(
		const void* profile, // pointer to ICC profile
		unsigned int size, // size of ICC profile in bytes
		PixelFormat format // format of input pixels
	);

	// Apply color transform to image. Returns true on success, false otherwise.
	static bool DoTransform(
		void* transform, // ICCP transform
		const void* inputBuffer, // 4-byte BGRA or RGBA input depending on transform pixel format
		void* outputBuffer, // 4-byte BGRA output
		unsigned int width, // width of image in pixels
		unsigned int height, // height of image in pixels
		unsigned int stride=0 // number of bytes per row of pixels in the input, only needed if not equal to width * 4
	);

	// Free memory associated with the given transform
	static void DeleteTransform(void* transform);

	static void* CreateLabTransform(PixelFormat format);

	// True if images with an embedded RGB profile keep their pixels in the color space of that profile instead of
	// being converted to sRGB when loaded. They are converted directly to the display profile when painted, so colors
	// outside of sRGB are not clipped on wide gamut monitors, and to sRGB when saved, copied or printed.
	// This is the case when UseEmbeddedColorProfiles=true and display color management (DisplayColorProfile) is on.
	static bool KeepEmbeddedColorSpace();

	// Per thread slot for the embedded profile of the image being loaded (see CreateTransform()).
	// The image loader clears it before decoding an image and attaches the captured profile to the image afterwards.
	static void ResetCapturedProfile();
	static ICCProfilePtr GetCapturedProfile();
	static void SetCapturedProfile(const ICCProfilePtr& profile);

	// Stores the profile in the capture slot if KeepEmbeddedColorSpace() is true and it is a usable, non sRGB RGB profile.
	// Returns true if the profile was captured, i.e. the pixels must not be converted to sRGB.
	static bool CaptureProfile(const void* profile, unsigned int size);

	// True if the profile is usable and an RGB profile (not CMYK, gray or Lab)
	static bool IsRGBProfile(const void* profile, unsigned int size);

	// True if the profile is usable and a standard sRGB profile (converting from it to sRGB changes nothing)
	static bool IsSRGBProfile(const void* profile, unsigned int size);

	// True if lcms2 (lcms2.dll) can be used. Embedded profiles are then applied by JPEGView itself instead of GDI+.
	static bool IsAvailable();

	// Converts pixels in place from the given embedded profile to sRGB, used when pixels leave JPEGView
	// (saving, clipboard, printing). bytesPerPixel is 3 (BGR, rows padded to 4 bytes) or 4 (BGRA).
	// Does nothing and returns false if profile is empty.
	static bool ConvertToSRGB(const ICCProfilePtr& profile, void* pixels, int width, int height, int bytesPerPixel);

	// Name (description) of the profile, empty if none
	static CString GetProfileName(const ICCProfilePtr& profile);

private:
	static void* sRGBProfile;
};


// Color management for the display: converts the final, sRGB encoded 32 bpp BGRA DIBs that are painted
// to the screen into the color space of the monitor the window is currently shown on.
// All decoded and processed image data stays in sRGB, only the pixels blitted to the screen are converted,
// so saving, copying to the clipboard and printing are not affected.
// The monitor profile is either taken from the Windows color management settings (INI: DisplayColorProfile=auto)
// or from an ICC profile file given in the INI file.
// All methods must be called from the UI thread.
class CDisplayColorProfile
{
public:
	// Checks the monitor the window is on and (re)creates the display transform if needed.
	// Returns true if the window moved to a monitor with a different color transform since the last call,
	// i.e. if the image must be repainted.
	static bool Update(HWND hWnd);

	// Forces re-reading of the monitor profiles on next Update(), e.g. after display settings changed
	static void Invalidate();

	// True if a (non identity) display transform for sRGB images is currently active
	static bool IsActive();

	// Gets the transform from the given embedded image profile to the display, or the sRGB to display transform
	// if profile is empty. Returns NULL if no conversion is needed or possible. The transform stays valid until
	// TransformGeneration() changes.
	static void* GetTransform(const ICCProfilePtr& profile);

	// Changes each time the display transform changes
	static unsigned int TransformGeneration() { return sm_nTransformGeneration; }

	// File name or description of the currently used display profile, empty if none
	static const CString& ActiveProfileName() { return sm_sProfileName; }

	// Converts width x height 32 bpp BGRA pixels with the given transform (see GetTransform()), using the processing thread pool.
	// pSource and pTarget may be identical. Returns false if transform is NULL.
	static bool Apply(void* transform, const void* pSource, void* pTarget, int nWidth, int nHeight);

private:
	static void DeleteDisplayTransform();
	static void CreateDisplayTransform(HMONITOR hMonitor);

	static void* sm_transform; // sRGB to display, NULL if not needed
	static std::vector<unsigned char> sm_displayProfile; // empty if the display is sRGB (or treated as sRGB)
	static bool sm_bEnabled; // DisplayColorProfile is not "none"
	struct EmbeddedTransform { ICCProfilePtr profile; void* transform; };
	static std::vector<EmbeddedTransform> sm_embeddedTransforms; // cache of embedded profile to display transforms
	static HMONITOR sm_hMonitor;
	static bool sm_bValid;
	static unsigned int sm_nTransformGeneration;
	static CString sm_sProfileName;
};

// Holds the display color converted copy of a DIB, so that the (costly) conversion is only done
// when the DIB content, its size or the display transform changes - not on every repaint.
// Use one instance per painted DIB source (e.g. main image, zoom navigator thumbnail).
class CDisplayColorDIBCache
{
public:
	CDisplayColorDIBCache();
	~CDisplayColorDIBCache();

	// Returns the DIB to paint: pDIB itself if no display transform is needed, else the converted copy.
	// nDIBGeneration must change whenever the content of pDIB changes (see CJPEGImage::DIBGeneration()).
	// profile is the embedded profile of the image if its pixels are not sRGB (see CJPEGImage::GetICCProfile()).
	void* Convert(void* pDIB, CSize dibSize, unsigned int nDIBGeneration, const ICCProfilePtr& profile);

	// Frees the cached copy
	void Clear();

private:
	void* m_pSourceDIB;
	CSize m_size;
	unsigned int m_nDIBGeneration;
	unsigned int m_nTransformGeneration;
	void* m_pTransform;
	unsigned int* m_pConverted;
	int m_nAllocatedPixels;
};
