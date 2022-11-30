#include "stdafx.h"
#include <ShlObj.h>
#include <KnownFolders.h>
#include <iostream>
#include <fstream>

using namespace winrt;
using namespace winrt::Windows::System;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using namespace winrt::Windows::Graphics::Capture;

#ifdef DEV_TRACE
void logCamPin(std::string line) {
	std::ofstream ofs("c:\\dev\\vcam.log", std::ios::app);
	if (!ofs) return;
	ofs << line << std::endl;
}

void logCamPinW(std::wstring line) {
	std::wofstream ofs("c:\\dev\\vcam_w.log", std::ios::app);
	if (!ofs) return;
	ofs << line << std::endl;
}
#endif

NMVCamPin::NMVCamPin(HRESULT *phr, NMVCamSource *pFilter) : CSourceStream(NAME("NMVCamPin"), phr, pFilter, OUTPUT_PIN_NAME)
	,m_pFilter(pFilter)
	,m_deviceCtx(nullptr), m_bufferTexture(nullptr)//, m_attatchedWindow(NULL)
	,m_graphicsCaptureItem(nullptr), m_framePool(nullptr)
	,m_captureSession(nullptr), m_frameBits(nullptr), m_pixelPosX(nullptr), m_pixelPosY(nullptr)
	,m_rtFrameLength(666666)
	,m_BmpData(NULL), m_Hdc(NULL), m_Bitmap(NULL)
	,m_lastFrameArrival(0)
{
	GetMediaType(&m_mt);
	m_brush = CreateSolidBrush(RGB(0, 0, 0));

	//CPU読み出し可能なバッファをGPU上に作成
	m_capWinSize.Width = 1;
	m_capWinSize.Height = 1;
	m_capWinSizeInTexture.left = 0;
	m_capWinSizeInTexture.right = 1;
	m_capWinSizeInTexture.top = 0;
	m_capWinSizeInTexture.bottom = 1;
	m_capWinSizeInTexture.front = 0;
	m_capWinSizeInTexture.back = 1;
	m_frameBits = new unsigned char[WINDOW_WIDTH * WINDOW_HEIGHT * ((PIXEL_BIT - 1) / 8 + 1)];
	m_pixelPosX = new double[WINDOW_WIDTH * WINDOW_HEIGHT];
	m_pixelPosY = new double[WINDOW_WIDTH * WINDOW_HEIGHT];
	for (int idx = 0; idx < WINDOW_WIDTH * WINDOW_HEIGHT; idx++) {
		m_pixelPosX[idx] = -1;
		m_pixelPosY[idx] = -1;
	}
	createDirect3DDevice();
}

NMVCamPin::~NMVCamPin() {
	if (m_bufferTexture) {
		m_bufferTexture->Release();
		m_bufferTexture = nullptr;
	}
	if (m_Bitmap) {
		DeleteObject(m_Bitmap);
		m_Bitmap = NULL;
	}
	if (m_Hdc) {
		DeleteDC(m_Hdc);
		m_Hdc = NULL;
	}
	if (m_BmpData) {
		delete m_BmpData;
		m_BmpData = NULL;
	}
	if (m_frameBits) {
		delete[] m_frameBits;
		m_frameBits = nullptr;
	}
	if (m_pixelPosX) {
		delete[] m_pixelPosX;
		m_pixelPosX = nullptr;
	}
	if (m_pixelPosY) {
		delete[] m_pixelPosY;
		m_pixelPosY = nullptr;
	}
}

HRESULT	NMVCamPin::OnThreadDestroy() {
	stopCapture();
	return NOERROR;
}

/****************************************************************/
/*  winRT GraphicsCapture Function Start                        */
/****************************************************************/
template<typename T>
auto getDXGIInterfaceFromObject(winrt::Windows::Foundation::IInspectable const &object) {
	auto access = object.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
	com_ptr<T> result;
	check_hresult(access->GetInterface(guid_of<T>(), result.put_void()));
	return result;
}

void NMVCamPin::createDirect3DDevice() {
	UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CPU_ACCESS_READ;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
	if (m_dxDevice != nullptr) {
		m_dxDevice.Close();
	}
	com_ptr<ID3D11Device> d3dDevice = nullptr;
	check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE,
		nullptr, createDeviceFlags, nullptr, 0, D3D11_SDK_VERSION,
		d3dDevice.put(), nullptr, nullptr));
	com_ptr<IDXGIDevice> dxgiDevice = d3dDevice.as<IDXGIDevice>();
	com_ptr<::IInspectable> device = nullptr;
	check_hresult(::CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), device.put()));
	m_dxDevice = device.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
	d3dDevice->GetImmediateContext(m_deviceCtx.put());

	//CPUから読みだすためのバッファテクスチャ
	m_bufferTextureDesc.Width = MAX_CAP_WIDTH;
	m_bufferTextureDesc.Height = MAX_CAP_HEIGHT;
	m_bufferTextureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	m_bufferTextureDesc.ArraySize = 1;
	m_bufferTextureDesc.BindFlags = 0;
	m_bufferTextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	m_bufferTextureDesc.MipLevels = 1;
	m_bufferTextureDesc.MiscFlags = 0;
	m_bufferTextureDesc.SampleDesc.Count = 1;
	m_bufferTextureDesc.SampleDesc.Quality = 0;
	m_bufferTextureDesc.Usage = D3D11_USAGE_STAGING;
	d3dDevice->CreateTexture2D(&m_bufferTextureDesc, 0, &m_bufferTexture);
}

void NMVCamPin::stopCapture() {
	if (isCapturing()) {
		m_frameArrived.revoke();
		m_captureSession = nullptr;
		m_framePool.Close();
		m_framePool = nullptr;
		m_graphicsCaptureItem = nullptr;
	}
}

void NMVCamPin::changeWindow(GraphicsCaptureItem targetCaptureItem) {
	if (targetCaptureItem == nullptr) {
		return;
	}
	stopCapture();

	m_graphicsCaptureItem = targetCaptureItem;
	m_capWinSize = m_graphicsCaptureItem.Size();
	m_capWinSizeInTexture.right = m_capWinSize.Width;
	m_capWinSizeInTexture.bottom = m_capWinSize.Height;
	changePixelPos();
	m_framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(m_dxDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, m_capWinSize);
	m_frameArrived = m_framePool.FrameArrived(auto_revoke, { this, &NMVCamPin::onFrameArrived });
	m_captureSession = m_framePool.CreateCaptureSession(m_graphicsCaptureItem);
	//IsCursorCaptureEnabledでカーソルもキャプチャするか指定できる。
	m_captureSession.IsCursorCaptureEnabled(false);
	m_captureSession.StartCapture();
}

BOOL CALLBACK EnumWndProc(HWND hWnd, LPARAM lParam) {
	WCHAR buff[MAX_PATH] = L"";
	SendMessageTimeoutW(hWnd, WM_GETTEXT, sizeof(buff), (LPARAM)buff, SMTO_BLOCK, 100, NULL);
	const WCHAR* pName = ((WindowCell*)lParam)->name;
	const size_t lenName = wcsnlen_s(pName, MAX_PATH);
	if (wcsncmp(buff, pName, lenName) == 0) {
		((WindowCell*)lParam)->window = hWnd;
		return FALSE;
	}
	return TRUE;
}

/// <summary>
/// WinRT APIでのキャプチャに必要なGraphicsCaptureItem構造体を、ウインドウ名（定数TARGET_WINDOW_NAME）をもとに取得する。
/// </summary>
/// <returns>所定の名前のウインドウがあればそのGraphicsCaptureItem、ない場合はnullptr</returns>
GraphicsCaptureItem NMVCamPin::CreateCaptureItemForWindow()
{
	// 下記記事のコードをもとに部分改変（abi定義は削除し、stdafx.hに必要なヘッダ（Windows.Graphics.Capture.Interop.h）を追加
	// https://qiita.com/HexagramNM/items/8493350d40608433421c
	// 下記も参照した。
	// https://qiita.com/i_saint/items/ad5b0545873d0cff4604

	WindowCell wc;
	wc.window = NULL;
	wcscpy(wc.name, TARGET_WINDOW_NAME);

	//デスクトップ上にあるウィンドウを走査し、特定ウィンドウ名のウィンドウハンドルを取得します。
	EnumWindows(EnumWndProc, (LPARAM)&wc);

	auto factory = get_activation_factory<GraphicsCaptureItem>();
	auto interop = factory.as<IGraphicsCaptureItemInterop>();
	GraphicsCaptureItem item{ nullptr };
	if (wc.window != NULL) {
		check_hresult(interop->CreateForWindow(wc.window, guid_of<IGraphicsCaptureItem>(),
			reinterpret_cast<void**>(put_abi(item))));
	}
	return item;
}

void NMVCamPin::convertFrameToBits() {
	D3D11_MAPPED_SUBRESOURCE mapd;
	HRESULT hr;
	hr = m_deviceCtx->Map(m_bufferTexture, 0, D3D11_MAP_READ, 0, &mapd);
	const unsigned char *source = static_cast<const unsigned char *>(mapd.pData);
	int texBitPos = 0;

	//取得したピクセル情報からビットマップを作る処理
	int pixelPosition = 0;
	int bitPosition = 0;
	int texPixelXInt = 0;
	int texPixelYInt = 0;
	double texPixelXDecimal = 0.0;
	double texPixelYDecimal = 0.0;
	double pixelColor[3] = { 0.0, 0.0, 0.0 };
	double pixelRate = 0.0;
	for (int y = 0; y < WINDOW_HEIGHT; y++) {
		for (int x = 0; x < WINDOW_WIDTH; x++) {
			//ピンに送られるビットはBGRで、上下反転するっぽい
			for (int cIdx = 0; cIdx < 3; cIdx++) {
				pixelColor[cIdx] = 0;
			}
			if (m_pixelPosX[pixelPosition] >= 0 && m_pixelPosY[pixelPosition] >= 0) {
#ifdef REFINED_PROCESS
				//周囲4ピクセルの情報を使用して、位置で重みづけ平均化したピクセルカラーを適用する。
				texPixelXInt = (int)m_pixelPosX[pixelPosition];
				texPixelYInt = (int)m_pixelPosY[pixelPosition];
				texPixelXDecimal = m_pixelPosX[pixelPosition] - texPixelXInt;
				texPixelYDecimal = m_pixelPosY[pixelPosition] - texPixelYInt;
				for (int px = 0; px < 2; px++) {
					for (int py = 0; py < 2; py++) {
						texBitPos = mapd.RowPitch * (texPixelYInt + py) + 4 * (texPixelXInt + px);
						pixelRate = (px == 0 ? (1.0 - texPixelXDecimal) : texPixelXDecimal) *  (py == 0 ? (1.0 - texPixelYDecimal) : texPixelYDecimal);
						for (int cIdx = 0; cIdx < 3; cIdx++) {
							pixelColor[cIdx] += source[texBitPos + cIdx] * pixelRate;
						}
					}
				}
#else
				//Nearest Neighbor
				texPixelXInt = (int)(m_pixelPosX[pixelPosition] + 0.5);
				texPixelYInt = (int)(m_pixelPosY[pixelPosition] + 0.5);
				texBitPos = mapd.RowPitch * texPixelYInt + 4 * texPixelXInt;
				for (int cIdx = 0; cIdx < 3; cIdx++) {
					pixelColor[cIdx] += source[texBitPos + cIdx];
				}
#endif
			}
			for (int cIdx = 0; cIdx < 3; cIdx++) {
				m_frameBits[bitPosition + cIdx] = (unsigned char)(pixelColor[cIdx] + 0.5);
			}
			pixelPosition++;
			bitPosition += 3;
		}
	}
	m_deviceCtx->Unmap(m_bufferTexture, 0);
}

void NMVCamPin::changePixelPos() {
	double widthZoomRate = (double)WINDOW_WIDTH / (double)m_capWinSize.Width;
	double heightZoomRate = (double)WINDOW_HEIGHT / (double)m_capWinSize.Height;
	double zoomRate = (widthZoomRate < heightZoomRate ? widthZoomRate : heightZoomRate);
	double invZoomRate = 1.0 / zoomRate;
	double halfWinWidth = (double)WINDOW_WIDTH * 0.5;
	double halfWinHeight = (double)WINDOW_HEIGHT * 0.5;

	double winSizeWidthDouble = (double)m_capWinSize.Width;
	double winSizeHeightDouble = (double)m_capWinSize.Height;
	double halfCapWidth = winSizeWidthDouble * 0.5;
	double halfCapHeight = winSizeHeightDouble * 0.5;

	int pixeldx = 0;
	double capPosX = 0;
	double capPosY = 0;
	for (int y = 0; y < WINDOW_HEIGHT; y++) {
		for (int x = 0; x < WINDOW_WIDTH; x++) {
			capPosX = (x - halfWinWidth) * invZoomRate + halfCapWidth;
			capPosY = winSizeHeightDouble - 1.0 - ((y - halfWinHeight) * invZoomRate + halfCapHeight);

			if (capPosX >= 0.0 && capPosX < winSizeWidthDouble && capPosY >= 0 && capPosY < winSizeHeightDouble) {
				m_pixelPosX[pixeldx] = capPosX;
				m_pixelPosY[pixeldx] = capPosY;
			}
			else {
				m_pixelPosX[pixeldx] = -1.0;
				m_pixelPosY[pixeldx] = -1.0;
			}
			pixeldx++;
		}
	}
}

void NMVCamPin::onFrameArrived(Direct3D11CaptureFramePool const &sender,
	winrt::Windows::Foundation::IInspectable const &args)
{
	auto frame = sender.TryGetNextFrame();

	SizeInt32 itemSize = frame.ContentSize();
	if (itemSize.Width <= 0) {
		itemSize.Width = 1;
	}
	if (itemSize.Height <= 0) {
		itemSize.Height = 1;
	}
	if (itemSize.Width != m_capWinSize.Width || itemSize.Height != m_capWinSize.Height) {
		m_capWinSize.Width = itemSize.Width;
		m_capWinSize.Height = itemSize.Height;
		m_capWinSizeInTexture.right = m_capWinSize.Width;
		m_capWinSizeInTexture.bottom = m_capWinSize.Height;
		changePixelPos();
		m_framePool.Recreate(m_dxDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, m_capWinSize);
	}

	com_ptr<ID3D11Texture2D> texture2D = getDXGIInterfaceFromObject<::ID3D11Texture2D>(frame.Surface());

	//CPU読み込み可能なバッファテクスチャにGPU上でデータコピー
	m_deviceCtx->CopySubresourceRegion(m_bufferTexture, 0, 0, 0, 0,
		texture2D.get(), 0, &m_capWinSizeInTexture);

	// 更新時刻を更新
	m_lastFrameArrival = GetTickCount64();
}

/****************************************************************/
/*  winRT GraphicsCapture Function End                          */
/****************************************************************/


STDMETHODIMP NMVCamPin::Notify(IBaseFilter *pSelf, Quality q) {
	return E_NOTIMPL;
}

HRESULT NMVCamPin::GetMediaType(CMediaType *pMediaType) {
	HRESULT hr=NOERROR;
	VIDEOINFO *pvi=(VIDEOINFO *)pMediaType->AllocFormatBuffer(sizeof(VIDEOINFO));
	ZeroMemory(pvi, sizeof(VIDEOINFO));

	pvi->AvgTimePerFrame=m_rtFrameLength;

	BITMAPINFOHEADER *pBmi=&(pvi->bmiHeader);
	pBmi->biSize=sizeof(BITMAPINFOHEADER);
	pBmi->biWidth = WINDOW_WIDTH;
	pBmi->biHeight = WINDOW_HEIGHT;
	pBmi->biPlanes=1;
	pBmi->biBitCount=PIXEL_BIT;
	pBmi->biCompression=BI_RGB;
	pvi->bmiHeader.biSizeImage=DIBSIZE(pvi->bmiHeader);

	SetRectEmpty(&(pvi->rcSource));
	SetRectEmpty(&(pvi->rcTarget));

	pMediaType->SetType(&MEDIATYPE_Video);
	pMediaType->SetFormatType(&FORMAT_VideoInfo);

	const GUID subtype=GetBitmapSubtype(&pvi->bmiHeader);
	pMediaType->SetSubtype(&subtype);

	pMediaType->SetTemporalCompression(FALSE);
	const int bmpsize=DIBSIZE(*pBmi);
	pMediaType->SetSampleSize(bmpsize);
	if(m_BmpData) delete m_BmpData;
	m_BmpData=new DWORD[pBmi->biWidth * pBmi->biHeight];
	memset(m_BmpData,0,pMediaType->GetSampleSize());
	
	HDC dwhdc=GetDC(GetDesktopWindow());
	m_Bitmap=
		CreateDIBitmap(dwhdc, pBmi, CBM_INIT, m_BmpData, (BITMAPINFO*)pBmi, DIB_RGB_COLORS);
	
	if (m_Hdc) {
		DeleteDC(m_Hdc);
		m_Hdc = NULL;
	}
	m_Hdc=CreateCompatibleDC(dwhdc);
	SelectObject(m_Hdc, m_Bitmap);
	ReleaseDC(GetDesktopWindow(), dwhdc);
	
	return hr;
}

HRESULT NMVCamPin::CheckMediaType(const CMediaType *pMediaType) {
	HRESULT hr=NOERROR;
	CheckPointer(pMediaType,E_POINTER);
	CMediaType mt;
	GetMediaType(&mt);
	if(mt==*pMediaType) {
		FreeMediaType(mt);
		return S_OK;
	}
	FreeMediaType(mt);
	return E_FAIL;
}

HRESULT NMVCamPin::DecideBufferSize(IMemAllocator *pAlloc,ALLOCATOR_PROPERTIES *pRequest) {
	HRESULT hr=NOERROR;
	VIDEOINFO *pvi=reinterpret_cast<VIDEOINFO*>(m_mt.Format());
	ASSERT(pvi != NULL);
	pRequest->cBuffers=1;
	// バッファサイズはビットマップ1枚分以上である必要がある
	if(pvi->bmiHeader.biSizeImage > (DWORD)pRequest->cbBuffer) {
		pRequest->cbBuffer=pvi->bmiHeader.biSizeImage;
	}
	// アロケータプロパティを設定しなおす
	ALLOCATOR_PROPERTIES Actual;
	hr=pAlloc->SetProperties(pRequest, &Actual);
	if(FAILED(hr)) {
		return hr;
	}
	if(Actual.cbBuffer < pRequest->cbBuffer) {
		return E_FAIL;
	}

	return S_OK;
}

HRESULT NMVCamPin::FillBuffer(IMediaSample *pSample) {
	HRESULT hr=E_FAIL;
	CheckPointer(pSample,E_POINTER);

	// ダウンストリームフィルタが
	// フォーマットを動的に変えていないかチェック
	ASSERT(m_mt.formattype == FORMAT_VideoInfo);
	ASSERT(m_mt.cbFormat >= sizeof(VIDEOINFOHEADER));

	// フレームに書き込み
	LPBYTE pSampleData=NULL;
	const long size=pSample->GetSize();
	pSample->GetPointer(&pSampleData);

	TCHAR buffer1[50];
	TCHAR buffer2[20];
	CRefTime ref;
	m_pFilter->StreamTime(ref);
	
	if (m_graphicsCaptureItem == nullptr) {
		auto item = this->CreateCaptureItemForWindow();
		if (item != nullptr) {
			this->changeWindow(item);
			m_lastFrameArrival = GetTickCount64();
		}
	}
	else {
		ULONGLONG now = GetTickCount64();
		// 5秒キャプチャフレームが来ない場合はウインドウが消えたものとして扱う。
		if ((now - m_lastFrameArrival) > 5 * 1000) {
			auto item = this->CreateCaptureItemForWindow();
			if (item == nullptr) {
				stopCapture();
			}
		}
	}

	//キャプチャされた画像のビット列をpSampleDataにコピー
	if (isCapturing()) {
		convertFrameToBits();
#pragma warning(push)
#pragma warning(disable:6386)
		memcpy(pSampleData, m_frameBits, WINDOW_WIDTH * WINDOW_HEIGHT * ((PIXEL_BIT - 1) / 8 + 1));
#pragma warning(pop)
	}
	else {
		SelectObject(m_Hdc, m_brush);
		PatBlt(m_Hdc, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, PATCOPY); // 黒で塗りつぶす
		SetTextColor(m_Hdc, RGB(255, 255, 128)); // 文字色は明るい黄色
		SetBkColor(m_Hdc, RGB(0, 0, 0)); // 背景色は黒
		int fontHeight = 120;
		HFONT hFont = CreateFont(
			-fontHeight, FW_DONTCARE, 
			0, 0, FW_BOLD, 
			FALSE, FALSE, FALSE, 
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 
			VARIABLE_PITCH | FF_SWISS, 
			NULL); // 大きく、プロポーショナルで、Sans-serifで太字なフォントを指定
		HGDIOBJ originalFont = SelectObject(m_Hdc, hFont);
		_snwprintf_s(buffer1, _countof(buffer1), _TRUNCATE, TARGET_WINDOW_NAME);
		TextOut(m_Hdc, 30, fontHeight, buffer1, lstrlen(buffer1));
		_snwprintf_s(buffer2, _countof(buffer2), _TRUNCATE, TEXT("Not Found"));
		TextOut(m_Hdc, 30, 3*fontHeight, buffer2, lstrlen(buffer2));
		SelectObject(m_Hdc, originalFont);
		DeleteObject(hFont);
		// ウインドウがない場合の表示を仮想カメラ映像として設定
		VIDEOINFO *pvi = (VIDEOINFO *)m_mt.Format();
		GetDIBits(m_Hdc, m_Bitmap, 0, WINDOW_HEIGHT,
			pSampleData, (BITMAPINFO*)&pvi->bmiHeader, DIB_RGB_COLORS);
	}

	const REFERENCE_TIME delta=m_rtFrameLength;
	REFERENCE_TIME start_time=ref;
	FILTER_STATE state;
	m_pFilter->GetState(0, &state);
	if(state==State_Paused)
		start_time=0;
	REFERENCE_TIME end_time=(start_time+delta);
	pSample->SetTime(&start_time, &end_time);
	pSample->SetActualDataLength(size);
	pSample->SetSyncPoint(TRUE);

	//CPU使用率を抑えて、ZoomなどのUIの反応をしやすくするために適度にSleepする。
	std::this_thread::sleep_for(std::chrono::milliseconds(10));

	return S_OK;
}
