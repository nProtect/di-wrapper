#include "di_wrap.h"

//
// Globals
//

HMODULE								WrapperSystem::dinputDll;
WrapperList							WrapperSystem::wrappers;
HHOOK								WrapperSystem::hookHandle = 0;
DIRECTINPUTCREATEAPROC				WrapperSystem::DirectInputCreateA;
bool								WrapperSystem::hidInitialized = false;
HANDLE								WrapperSystem::wrapperModule;
MouseEventList						WrapperSystem::eventList;

// with these guid defs, we can skip dxguid.lib
GUID GUID_DI_SysMouse	= { 0x6F1D2B60, 0xD5A0, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };
GUID GUID_DI_XAxis		= { 0xA36D02E0, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };
GUID GUID_DI_YAxis		= { 0xA36D02E1, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };
GUID GUID_DI_ZAxis		= { 0xA36D02E2, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };
GUID GUID_DI_Button		= { 0xA36D02F0, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 };

//
// DLL exports
//

extern "C" {

BOOL WINAPI DllMain( HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved ) {

	switch( ul_reason_for_call ) {

		case DLL_PROCESS_ATTACH:

			if( !WrapperSystem::Init( hModule ) ) return FALSE;
			break;

		case DLL_PROCESS_DETACH:

			WrapperSystem::Shutdown( );
			break;
	}

	return TRUE;
}

HRESULT WINAPI DirectInputCreateA( HINSTANCE inst_handle, DWORD version, DI_HID_WrapperBase ** out_wrapper, LPUNKNOWN p_unk ) {

	*out_wrapper = WrapperSystem::AllocWrapper( );
	return WrapperSystem::DirectInputCreateA( inst_handle, version, ( LPDIRECTINPUTA * )( *out_wrapper )->GetDIPtr( ), p_unk );
};

}

//
// WrapperSystem
//

bool WrapperSystem::Init( HANDLE mod_hnd ) {

	char dinputDllName[ MAX_PATH ];

	// returns with system32 even on win64 32bit mode, but image loader solves it
	GetSystemDirectoryA( dinputDllName, MAX_PATH );
	strcat( dinputDllName, "\\dinput.dll" );

	dinputDll = LoadLibraryA( dinputDllName );

	// MSDN: If the function succeeds, the return value is greater than 31.
	if( dinputDll > ( HMODULE )31 ) {

		DirectInputCreateA = ( DIRECTINPUTCREATEAPROC )GetProcAddress( dinputDll, "DirectInputCreateA" );

		if( !DirectInputCreateA ) {

			Shutdown( );
			return false;
		}

		wrapperModule = mod_hnd;
		return true;
	}

	return false;
}

void WrapperSystem::Shutdown( ) {

	for( WrapperList::iterator wrpItr = wrappers.begin( ); wrpItr != wrappers.end( ); wrpItr++ ) {

		delete *wrpItr;
	}

	if( hookHandle ) {
		
		UnhookWindowsHookEx( hookHandle );
		hookHandle = 0;
	}

	FreeLibrary( dinputDll );
}

void WrapperSystem::InitHID( HWND h_wnd ) {

	if( hidInitialized ) return;

	// jk2 sends a null hwnd, and RegisterRawInputDevices fails without it, so grab one with GetForegroundWindow
	if( !h_wnd ) {

		h_wnd = GetForegroundWindow( );
	}

	RAWINPUTDEVICE Rid;
	Rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
	Rid.usUsage = HID_USAGE_GENERIC_MOUSE;
	Rid.dwFlags = RIDEV_INPUTSINK;
	Rid.hwndTarget = h_wnd;

	if( !RegisterRawInputDevices( &Rid, 1, sizeof( RAWINPUTDEVICE ) ) ) return;

	// register our hook into the game main message loop
	hookHandle = SetWindowsHookExA( WH_GETMESSAGE, WindowHookFunc, ( HINSTANCE )wrapperModule, GetCurrentThreadId( ) );

	if( !hookHandle ) return;

	hidInitialized = true;
}

LRESULT WrapperSystem::WindowHookFunc( int nCode, WPARAM wParam, LPARAM lParam ) {

	MSG * cwpData = ( MSG * )lParam;

	if( cwpData->message == WM_INPUT ) {

		byte lpb[ 48 ];
		RAWINPUT * raw;
		DWORD dwRet;
		UINT dwSize = 48;
    
		dwRet = GetRawInputData( ( HRAWINPUT )cwpData->lParam, RID_INPUT, lpb, &dwSize, sizeof( RAWINPUTHEADER ) );

		raw = ( RAWINPUT * )lpb;

		if( raw->header.dwType == RIM_TYPEMOUSE && !raw->data.mouse.usFlags ) {

			eventList.push_back( raw->data.mouse );
			return TRUE;
		}
	}

	return CallNextHookEx( 0, nCode, wParam, lParam );
}

//
// DI_HID_WrapperBase
//

DI_HID_WrapperBase * WrapperSystem::AllocWrapper( ) {

	DI_HID_WrapperBase * wrapper = new DI_HID_WrapperBase( );
	wrappers.push_back( wrapper );
	return wrapper;
}

DI_HID_WrapperBase::DI_HID_WrapperBase( ) {

	this->refCount = 0;
	this->diInterface = 0;
}

BOOL DI_HID_WrapperBase::EnumDevicesCallback( LPCDIDEVICEINSTANCEA dev_inst, LPVOID cb_userdata ) {

	DeviceInstanceList * devices = ( DeviceInstanceList * )cb_userdata;
	devices->push_back( *dev_inst );
	return TRUE;
}

DI_HID_DeviceBase * DI_HID_WrapperBase::AllocDevice( ) {

	DI_HID_DeviceBase * device = new DI_HID_DeviceBase( );
	devices.push_back( device );
	return device;
}

HRESULT DI_HID_WrapperBase::QueryInterface( const IID & r_iid, LPVOID * p_obj ) {

	*p_obj = this;
	return S_OK;
}

ULONG DI_HID_WrapperBase::AddRef( ) {

	return this->refCount++;
}

ULONG DI_HID_WrapperBase::Release( ) {

	return this->refCount--;
}

HRESULT DI_HID_WrapperBase::CreateDevice( const GUID & r_guid, DI_HID_DeviceBase ** di_device, LPUNKNOWN p_unk ) {

	*di_device = AllocDevice( );

	// found a mouse, use vtbl of DI_HID_DeviceBase
	if( r_guid == GUID_DI_SysMouse ) return S_OK;

	HRESULT res = diInterface->CreateDevice( r_guid, ( LPDIRECTINPUTDEVICEA * )( *di_device )->GetDIPtr( ), p_unk );
	( *di_device )->PassTruhMode( true );

	// use the di's virtual table
	*di_device = reinterpret_cast< DI_HID_DeviceBase * >( *( *di_device )->GetDIPtr( ) );
	return res;
}

HRESULT DI_HID_WrapperBase::EnumDevices( DWORD dev_type, LPDIENUMDEVICESCALLBACKA callback, LPVOID cb_userdata, DWORD flags ) {

	DeviceInstanceList devices;
	diInterface->EnumDevices( dev_type, EnumDevicesCallback, &devices, flags );

	for( DeviceInstanceList::iterator devItr = devices.begin( ); devItr != devices.end( ); devItr++ ) {

		callback( ( LPCDIDEVICEINSTANCEA )&( *devItr ), cb_userdata );
	}

	return S_OK;
}

HRESULT DI_HID_WrapperBase::GetDeviceStatus( const GUID & r_guid ) {

	return S_OK;
}

HRESULT DI_HID_WrapperBase::RunControlPanel( HWND win_handle, DWORD flags ) {

	return S_OK;
}

HRESULT DI_HID_WrapperBase::Initialize( HINSTANCE inst_handle, DWORD version ) {

	return S_OK;
}

HRESULT DI_HID_WrapperBase::FindDevice( const GUID & r_guid, LPCSTR dev_name, LPGUID dev_guid ) {

	return S_OK;
}

//
// DI_HID_DeviceBase
//

DI_HID_DeviceBase::DI_HID_DeviceBase( ) {

	this->refCount = 0;
	this->passTruh = false;
	this->diDevice = NULL;
	this->seqNumber = 0;

	// clear axis states
	Accumlate( );

	DI_HID_Object obj;

	obj.guidType = GUID_DI_XAxis;
	obj.dwType = DIDFT_AXIS | DIDFT_MAKEINSTANCE( 0 );
	obj.dwOfs = DIMOFS_X;
	strcpy( obj.tszName, "X-Axis" );
	obj.SetDataPtr( &this->accumlateX );
	obj.SetSize( 4 );
	objects.push_back( obj );

	obj.guidType = GUID_DI_YAxis;
	obj.dwType = DIDFT_AXIS | DIDFT_MAKEINSTANCE( 1 );
	obj.dwOfs = DIMOFS_Y;
	strcpy( obj.tszName, "Y-Axis" );
	obj.SetDataPtr( &this->accumlateY );
	obj.SetSize( 4 );
	objects.push_back( obj );

	obj.guidType = GUID_DI_ZAxis;
	obj.dwType = DIDFT_AXIS | DIDFT_MAKEINSTANCE( 2 );
	obj.dwOfs = DIMOFS_Z;
	strcpy( obj.tszName, "Z-Axis" );
	obj.SetDataPtr( &this->accumlateZ );
	obj.SetSize( 4 );
	objects.push_back( obj );

	obj.guidType = GUID_DI_Button;
	obj.dwType = DIDFT_BUTTON | DIDFT_MAKEINSTANCE( 0 );
	obj.dwOfs = DIMOFS_BUTTON0;
	strcpy( obj.tszName, "Left Button" );
	obj.SetDataPtr( &this->accumlateButton1 );
	obj.SetSize( 1 );
	objects.push_back( obj );

	obj.guidType = GUID_DI_Button;
	obj.dwType = DIDFT_BUTTON | DIDFT_MAKEINSTANCE( 1 );
	obj.dwOfs = DIMOFS_BUTTON1;
	strcpy( obj.tszName, "Right Button" );
	obj.SetDataPtr( &this->accumlateButton2 );
	obj.SetSize( 1 );
	objects.push_back( obj );

	obj.guidType = GUID_DI_Button;
	obj.dwType = DIDFT_BUTTON | DIDFT_MAKEINSTANCE( 2 );
	obj.dwOfs = DIMOFS_BUTTON2;
	strcpy( obj.tszName, "Middle Button" );
	obj.SetDataPtr( &this->accumlateButton3 );
	obj.SetSize( 1 );
	objects.push_back( obj );

	obj.guidType = GUID_DI_Button;
	obj.dwType = DIDFT_BUTTON | DIDFT_MAKEINSTANCE( 3 );
	obj.dwOfs = DIMOFS_BUTTON3;
	strcpy( obj.tszName, "Button 4" );
	obj.SetDataPtr( &this->accumlateButton4 );
	obj.SetSize( 1 );
	objects.push_back( obj );

	obj.guidType = GUID_DI_Button;
	obj.dwType = DIDFT_BUTTON | DIDFT_MAKEINSTANCE( 4 );
	obj.dwOfs = DIMOFS_BUTTON4;
	strcpy( obj.tszName, "Button 5" );
	obj.SetDataPtr( &this->accumlateButton5 );
	obj.SetSize( 1 );
	objects.push_back( obj );

	obj.guidType = GUID_DI_Button;
	obj.dwType = DIDFT_BUTTON | DIDFT_MAKEINSTANCE( 5 );
	obj.dwOfs = DIMOFS_BUTTON5;
	strcpy( obj.tszName, "Wheel Left" );
	obj.SetDataPtr( &this->accumlateButton6 );
	obj.SetSize( 1 );
	objects.push_back( obj );

	obj.guidType = GUID_DI_Button;
	obj.dwType = DIDFT_BUTTON | DIDFT_MAKEINSTANCE( 6 );
	obj.dwOfs = DIMOFS_BUTTON6;
	strcpy( obj.tszName, "Wheel Right" );
	obj.SetDataPtr( &this->accumlateButton7 );
	obj.SetSize( 1 );
	objects.push_back( obj );

	initTime = GetTickCount( );

	this->accumlateButton1 = 0;
	this->accumlateButton2 = 0;
	this->accumlateButton3 = 0;
	this->accumlateButton4 = 0;
	this->accumlateButton5 = 0;
}

void DI_HID_DeviceBase::Accumlate( ) {

	this->accumlateX = 0;
	this->accumlateY = 0;
	this->accumlateZ = 0;
	this->accumlateButton6 = 0;
	this->accumlateButton7 = 0;

	for( MouseEventList::iterator evItr = WrapperSystem::eventList.begin( ); evItr != WrapperSystem::eventList.end( ); evItr++ ) {

		// raw mouse input handles axis data seperately
		if( !evItr->usButtonFlags ) {

			this->accumlateX += evItr->lLastX;
			this->accumlateY += evItr->lLastY;

		} else {

			// standard buttons need to be saved per call, or only a down message sent
			this->accumlateButton1 = ( evItr->usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN )	? 0x80 : ( ( evItr->usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP )	? 0 : this->accumlateButton1 );
			this->accumlateButton2 = ( evItr->usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN )	? 0x80 : ( ( evItr->usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP )	? 0 : this->accumlateButton1 );
			this->accumlateButton3 = ( evItr->usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN )	? 0x80 : ( ( evItr->usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP )	? 0 : this->accumlateButton1 );
			this->accumlateButton4 = ( evItr->usButtonFlags & RI_MOUSE_BUTTON_4_DOWN )		? 0x80 : ( ( evItr->usButtonFlags & RI_MOUSE_BUTTON_4_UP )		? 0 : this->accumlateButton4 );
			this->accumlateButton5 = ( evItr->usButtonFlags & RI_MOUSE_BUTTON_5_DOWN )		? 0x80 : ( ( evItr->usButtonFlags & RI_MOUSE_BUTTON_5_UP )		? 0 : this->accumlateButton5 );
			
			// we need to convert whell data into the di's z axis
			if( evItr->usButtonFlags & RI_MOUSE_WHEEL ) {

				this->accumlateZ += ( *( short * )&evItr->usButtonData ) > 0 ? 1 : -1;
			}

			// this is undocumented as usual, but works well with my scroll wheel vertical switch
			if( evItr->usButtonFlags & 0x800 ) {

				if( ( *( short * )&evItr->usButtonData ) < 0 ) this->accumlateButton6 = 0x80;
				if( ( *( short * )&evItr->usButtonData ) > 0 ) this->accumlateButton7 = 0x80;
			}
		}
	}

	WrapperSystem::eventList.clear( );
}

HRESULT DI_HID_DeviceBase::QueryInterface( const IID & r_iid, LPVOID * p_obj ) {

	*p_obj = this;
	return S_OK;
}

ULONG DI_HID_DeviceBase::AddRef( ) {

	return this->refCount++;
}

ULONG DI_HID_DeviceBase::Release( ) {

	return this->refCount--;
}

HRESULT DI_HID_DeviceBase::GetCapabilities( LPDIDEVCAPS dev_caps ) {

	return S_OK;
}

HRESULT DI_HID_DeviceBase::EnumObjects( LPDIENUMDEVICEOBJECTSCALLBACKA callback, LPVOID cb_userdata, DWORD flags ) {

	for( ObjectList::iterator objItr = objects.begin( ); objItr != objects.end( ); objItr++ ) {

		callback( ( LPDIDEVICEOBJECTINSTANCEA )&( *objItr ), cb_userdata );
	}

	return S_OK;
}

HRESULT DI_HID_DeviceBase::GetProperty( const IID & r_iid, LPDIPROPHEADER dip ) {

	switch( ( int )&r_iid ) {

		case 1: // DIPROP_BUFFERSIZE

			( ( LPDIPROPDWORD )dip )->dwData = 0xFFFFFFFF; // evil laugh
			return S_OK;

		case 4: // DIPROP_RANGE

			( ( LPDIPROPRANGE )dip )->lMin = DIPROPRANGE_NOMIN;
			( ( LPDIPROPRANGE )dip )->lMax = DIPROPRANGE_NOMAX;
			return S_OK;
	}

	return DIERR_UNSUPPORTED;
}

HRESULT DI_HID_DeviceBase::SetProperty( const IID & r_iid, LPCDIPROPHEADER dip ) {

	switch( ( int )&r_iid ) {

		case 1: // DIPROP_BUFFERSIZE

			return DI_PROPNOEFFECT;
	}

	return DIERR_UNSUPPORTED;
}

HRESULT DI_HID_DeviceBase::Acquire( ) {

	return S_OK;
}

HRESULT DI_HID_DeviceBase::Unacquire( ) {

	return S_OK;
}

HRESULT DI_HID_DeviceBase::GetDeviceState( DWORD buf_size, LPVOID buf_data ) {

	Accumlate( );
	DWORD ofs = 0;

	for( ObjectList::iterator objItr = dataFormat.begin( ); objItr != dataFormat.end( ); objItr++ ) {

		assert( ( ofs + 4 ) <= buf_size );
		*( LONG * )&( ( ( char * )buf_data )[ ofs ] ) = objItr->GetData( );
		ofs += 4;
	}

	return S_OK;
}

HRESULT DI_HID_DeviceBase::GetDeviceData( DWORD buf_size, LPDIDEVICEOBJECTDATA buf_data, LPDWORD out_size, DWORD flags ) {

	DWORD seqRemainder = this->seqNumber % ( dataFormat.size( ) + 1 );

	if( !seqRemainder ) {

		Accumlate( );
		seqIterator = dataFormat.begin( );
	}

	if( seqRemainder < dataFormat.size( ) ) {

		buf_data->dwData = seqIterator->GetData( );
		buf_data->dwOfs = seqIterator->dwOfs;
		seqIterator++;

	} else {

		*out_size = 0;
	}

	buf_data->dwTimeStamp = GetTickCount( ) - initTime;
	buf_data->dwSequence = this->seqNumber++;

	return S_OK;
}

HRESULT DI_HID_DeviceBase::SetDataFormat( LPCDIDATAFORMAT data_format ) {

	assert( data_format->dwFlags == DIDF_RELAXIS );

	dataFormat.clear( );

	for( DWORD i = 0; i < data_format->dwNumObjs; i++ ) {

		for( ObjectList::iterator objItr = objects.begin( ); objItr != objects.end( ); objItr++ ) {

			if( data_format->rgodf[ i ].pguid && *data_format->rgodf[ i ].pguid == objItr->guidType &&
				( ( data_format->rgodf[ i ].dwType & DIDFT_ANYINSTANCE ) == DIDFT_ANYINSTANCE ) ?
				( DIDFT_GETTYPE( data_format->rgodf[ i ].dwType ) == DIDFT_GETTYPE( objItr->dwType ) ) :
				( data_format->rgodf[ i ].dwType == objItr->dwType ) ) {

				DI_HID_Object object = *objItr;
				object.dwOfs = data_format->rgodf[ i ].dwOfs;
				dataFormat.push_back( object );
				break;
			}
		}
	}

	// avoid GetDeviceData fails with a sequence reset
	this->seqNumber = 0;
	
	if( data_format->dwNumObjs != dataFormat.size( ) ) return DIERR_INVALIDPARAM;

	return S_OK;
}

HRESULT DI_HID_DeviceBase::SetEventNotification( HANDLE h_event ) {

	return S_OK;
}

HRESULT DI_HID_DeviceBase::SetCooperativeLevel( HWND h_wnd, DWORD level ) {

	WrapperSystem::InitHID( h_wnd );

	return S_OK;
}

HRESULT DI_HID_DeviceBase::GetObjectInfo( LPDIDEVICEOBJECTINSTANCEA obj_inst, DWORD obj, DWORD how ) {

	return S_OK;
}

HRESULT DI_HID_DeviceBase::GetDeviceInfo( LPDIDEVICEINSTANCEA info ) {

	return S_OK;
}

HRESULT DI_HID_DeviceBase::RunControlPanel( HWND h_wnd, DWORD flags ) {

	return S_OK;
}

HRESULT DI_HID_DeviceBase::Initialize( HINSTANCE inst, DWORD version, const IID & r_iid ) {

	return S_OK;
}

HRESULT DI_HID_DeviceBase::CreateEffect( const IID & r_iid, LPCDIEFFECT fx, LPDIRECTINPUTEFFECT * di_fx, LPUNKNOWN p_unk ) {

	return S_OK;
}

HRESULT DI_HID_DeviceBase::EnumEffects( LPDIENUMEFFECTSCALLBACKA callback, LPVOID cb_userdata, DWORD fx_type ) {

	return S_OK;
}

HRESULT DI_HID_DeviceBase::GetEffectInfo( LPDIEFFECTINFOA fx, const IID & r_iid ) {

	return S_OK;
}

HRESULT DI_HID_DeviceBase::GetForceFeedbackState( LPDWORD out ) {

	return S_OK;
}

HRESULT DI_HID_DeviceBase::SendForceFeedbackCommand( DWORD flags ) {

	return S_OK;
}

HRESULT DI_HID_DeviceBase::EnumCreatedEffectObjects( LPDIENUMCREATEDEFFECTOBJECTSCALLBACK callback, LPVOID cb_userdata, DWORD flags ) {

	return S_OK;
}

HRESULT DI_HID_DeviceBase::Escape( LPDIEFFESCAPE esc ) {

	return S_OK;
}

HRESULT DI_HID_DeviceBase::Poll( ) {

	return S_OK;
}

HRESULT DI_HID_DeviceBase::SendDeviceData( DWORD buf_size, LPCDIDEVICEOBJECTDATA buf_data, LPDWORD out_size, DWORD flags ) {

	return S_OK;
}

HRESULT DI_HID_DeviceBase::EnumEffectsInFile( LPCSTR file_name, LPDIENUMEFFECTSINFILECALLBACK callback, LPVOID cb_userdata, DWORD flags ) {

	return S_OK;
}

HRESULT DI_HID_DeviceBase::WriteEffectToFile( LPCSTR file_name, DWORD count, LPDIFILEEFFECT data, DWORD flags ) {

	return S_OK;
}
