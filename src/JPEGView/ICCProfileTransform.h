#pragma once

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

	// True if a (non identity) display transform is currently active
	static bool IsActive();

	// Changes each time the display transform changes
	static unsigned int TransformGeneration() { return sm_nTransformGeneration; }

	// File name or description of the currently used display profile, empty if none
	static const CString& ActiveProfileName() { return sm_sProfileName; }

	// Converts width x height 32 bpp BGRA pixels from sRGB to the display color space.
	// pSource and pTarget may be identical. Returns false if no transform is active.
	static bool Apply(const void* pSource, void* pTarget, int nWidth, int nHeight);

private:
	static void DeleteDisplayTransform();
	static void CreateDisplayTransform(HMONITOR hMonitor);

	static void* sm_transform;
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

	// Returns the DIB to paint: pDIB itself if no display transform is active, else the converted copy.
	// nDIBGeneration must change whenever the content of pDIB changes (see CJPEGImage::DIBGeneration()).
	void* Convert(void* pDIB, CSize dibSize, unsigned int nDIBGeneration);

	// Frees the cached copy
	void Clear();

private:
	void* m_pSourceDIB;
	CSize m_size;
	unsigned int m_nDIBGeneration;
	unsigned int m_nTransformGeneration;
	unsigned int* m_pConverted;
	int m_nAllocatedPixels;
};
