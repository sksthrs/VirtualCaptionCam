#include "stdafx.h"

using namespace winrt;
using namespace winrt::Windows::System;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using namespace winrt::Windows::Graphics::Capture;

// 参考元のNMUniversalVCamや、そのまま流用したものと衝突しないようにGUIDは改変
// {54222542-93B6-477D-B240-D2E847C7F702}
DEFINE_GUID(CLSID_NMUniversalVCamM,
	0x54222542, 0x93b6, 0x477d, 0xb2, 0x40, 0xd2, 0xe8, 0x47, 0xc7, 0xf7, 0x02);

// ピンタイプの定義
const AMOVIESETUP_MEDIATYPE sudPinTypes =
{
	&MEDIATYPE_Video,
	&MEDIASUBTYPE_RGB24
};
// 入力用、出力用ピンの情報
const AMOVIESETUP_PIN sudPins =
{
	OUTPUT_PIN_NAME,
	FALSE,
	TRUE,
	FALSE,
	FALSE,
	&CLSID_NULL,
	NULL,
	1,
	&sudPinTypes
};

// フィルタ情報
const AMOVIESETUP_FILTER afFilterInfo =
{
	&CLSID_NMUniversalVCamM,
	FILTER_NAME,
	MERIT_NORMAL,
	1,
	&sudPins
};

class NMVCamPin;

//ソースフィルタクラス
class NMVCamSource : public CSource
{
private:
	NMVCamPin *m_pin;
public:

	NMVCamSource(LPUNKNOWN pUnk, HRESULT *phr);
	virtual ~NMVCamSource();
	static CUnknown * WINAPI CreateInstance(LPUNKNOWN pUnk, HRESULT *phr);
	
	STDMETHODIMP QueryInterface(REFIID riid, void **ppv);
	IFilterGraph *GetGraph() { return m_pGraph; }
};

// ウインドウ名の最大長
const size_t MAX_NAME_LENGTH = 256;

// ウインドウ一覧を取得する際に用いる構造体
struct WindowCell {
	HWND window;
	WCHAR name[MAX_NAME_LENGTH];
};

// プッシュピンクラス
class NMVCamPin : public CSourceStream, public IAMStreamConfig , public IKsPropertySet, public IAMFilterMiscFlags{
public:
	NMVCamPin(HRESULT *phr, NMVCamSource *pFilter);
	virtual			~NMVCamPin();
	STDMETHODIMP	Notify(IBaseFilter *pSelf, Quality q) override;

	// CSourceStreamの実装
	HRESULT			GetMediaType(CMediaType *pMediaType) override;
	HRESULT			CheckMediaType(const CMediaType *pMediaType) override;
	HRESULT			DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pRequest) override;
	HRESULT			FillBuffer(IMediaSample *pSample) override;
	HRESULT			OnThreadDestroy(void) override;

	//IUnknown
	STDMETHODIMP QueryInterface(REFIID   riid, LPVOID * ppvObj) override;

	STDMETHODIMP_(ULONG) AddRef() override;

	STDMETHODIMP_(ULONG) Release() override;

	//IKsPropertySet
	HRESULT STDMETHODCALLTYPE Get(
		REFGUID PropSet,
		ULONG   Id,
		LPVOID  InstanceData,
		ULONG   InstanceLength,
		LPVOID  PropertyData,
		ULONG   DataLength,
		ULONG   *BytesReturned
	) override;

	HRESULT STDMETHODCALLTYPE Set(
		REFGUID PropSet,
		ULONG   Id,
		LPVOID  InstanceData,
		ULONG   InstanceLength,
		LPVOID  PropertyData,
		ULONG   DataLength
	) override;

	HRESULT STDMETHODCALLTYPE QuerySupported(
		REFGUID PropSet,
		ULONG   Id,
		ULONG   *TypeSupport
	) override;

	//IAMStreamConfig
	HRESULT STDMETHODCALLTYPE GetFormat(
		AM_MEDIA_TYPE **ppmt
	) override;

	HRESULT STDMETHODCALLTYPE GetNumberOfCapabilities(
		int *piCount,
		int *piSize
	) override;

	HRESULT STDMETHODCALLTYPE GetStreamCaps(
		int           iIndex,
		AM_MEDIA_TYPE **ppmt,
		BYTE          *pSCC
	) override;

	HRESULT STDMETHODCALLTYPE SetFormat(
		AM_MEDIA_TYPE *pmt
	) override;

	//IAMFilterMiscFlags
	ULONG STDMETHODCALLTYPE GetMiscFlags() override;

protected:
private:
	VIDEOINFOHEADER videoInfo {
		RECT{0, 0, WINDOW_WIDTH, WINDOW_HEIGHT},
		RECT{0, 0, WINDOW_WIDTH, WINDOW_HEIGHT},
		30 * WINDOW_WIDTH * WINDOW_HEIGHT * PIXEL_BIT,
		0,
		160000,
		BITMAPINFOHEADER{
			sizeof(BITMAPINFOHEADER),
			WINDOW_WIDTH,
			WINDOW_HEIGHT,
			1,
			PIXEL_BIT,
			BI_RGB,
			0,
			2500,
			2500,
			0,
			0
		}
	};

	/****************************************************************/
	/*  winRT GraphicsCapture Function                              */
	/****************************************************************/
	void createDirect3DDevice();
	bool isCapturing() { return m_framePool != nullptr; }
	void stopCapture();
	void changeWindow(GraphicsCaptureItem targetCaptureItem);
	GraphicsCaptureItem CreateCaptureItemForWindow();
	void convertFrameToBits();
	void changePixelPos();
	void onFrameArrived(Direct3D11CaptureFramePool const &sender,
						winrt::Windows::Foundation::IInspectable const &args);

	NMVCamSource*		m_pFilter;			//このピンが所属しているフィルタへのポインタ
	IDirect3DDevice m_dxDevice;
	com_ptr<ID3D11DeviceContext> m_deviceCtx;
	ID3D11Texture2D *m_bufferTexture;
	D3D11_TEXTURE2D_DESC m_bufferTextureDesc;
	//HWND m_attatchedWindow;
	//winrt::Windows::Foundation::IAsyncOperation<GraphicsCaptureItem> m_graphicsCaptureAsyncResult;
	GraphicsCaptureItem m_graphicsCaptureItem;
	Direct3D11CaptureFramePool m_framePool;
	event_revoker<IDirect3D11CaptureFramePool> m_frameArrived;
	GraphicsCaptureSession m_captureSession;
	SizeInt32 m_capWinSize;
	D3D11_BOX m_capWinSizeInTexture;
	unsigned char* m_frameBits;
	double *m_pixelPosX;
	double *m_pixelPosY;

	REFERENCE_TIME	m_rtFrameLength;	//1フレームあたりの時間

	ULONGLONG m_lastFrameArrival; // 最後にキャプチャを取得した時点

	HBITMAP m_Bitmap;
	LPDWORD m_BmpData;
	HDC     m_Hdc;
	HBRUSH  m_brush;
};
