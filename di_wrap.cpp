/*
 * di-wrapper - A reimplemented version of dinput.dll with the raw input api
 * Copyright (C) 2011 Robert Balint
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "di_wrap.h"

//
// Globals
//

#ifdef DI_WRAPPER8
DIRECTINPUT8CREATEPROC				WrapperSystem::DirectInput8Create;
#else
DIRECTINPUTCREATEAPROC				WrapperSystem::DirectInputCreateA;
#endif

HMODULE								WrapperSystem::dinputDll;
WrapperList							WrapperSystem::wrappers;
HHOOK								WrapperSystem::hookHandleGetMessage = 0;
HHOOK								WrapperSystem::hookHandleCallWnd = 0;
bool								WrapperSystem::hidInitialized = false;
HANDLE								WrapperSystem::wrapperModule;
MouseEventList						WrapperSystem::eventList;
DI_HID_MouseEvent					WrapperSystem::button6ShutdownEvent;
DI_HID_MouseEvent					WrapperSystem::button7ShutdownEvent;
DWORD								WrapperSystem::lastEventTime;
LONG								WrapperSystem::immediateBuffer[ type_list_size ];
bool								WrapperSystem::bufferedMode = false;

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

#ifdef DI_WRAPPER8

HRESULT WINAPI DirectInput8Create( HINSTANCE inst_handle, DWORD version, const IID & r_iid, DI_HID_WrapperBase ** out_wrapper, LPUNKNOWN p_unk ) {

	*out_wrapper = WrapperSystem::AllocWrapper( );
	return WrapperSystem::DirectInput8Create( inst_handle, version, r_iid, ( LPVOID * )( *out_wrapper )->GetDIPtr( ), p_unk );
}

#else

HRESULT WINAPI DirectInputCreateA( HINSTANCE inst_handle, DWORD version, DI_HID_WrapperBase ** out_wrapper, LPUNKNOWN p_unk ) {

	*out_wrapper = WrapperSystem::AllocWrapper( );
	return WrapperSystem::DirectInputCreateA( inst_handle, version, ( LPDIRECTINPUTA * )( *out_wrapper )->GetDIPtr( ), p_unk );
};

#endif

}

//
// WrapperSystem
//

bool WrapperSystem::Init( HANDLE mod_hnd ) {

	char dinputDllName[ MAX_PATH ];

	// returns with system32 even on win64 32bit mode, but image loader solves it
	GetSystemDirectoryA( dinputDllName, MAX_PATH );

	button6ShutdownEvent.Clear( );
	button6ShutdownEvent.SetType( type_button_6 );
	button6ShutdownEvent.usButtonFlags = 0x800;
	button6ShutdownEvent.usButtonData = 0;
	button7ShutdownEvent.Clear( );
	button7ShutdownEvent.SetType( type_button_7 );
	button7ShutdownEvent.usButtonFlags = 0x800;
	button7ShutdownEvent.usButtonData = 0;

	memset( immediateBuffer, 0, sizeof( LONG ) * type_list_size );
	bufferedMode = false;

#ifdef DI_WRAPPER8
	strcat( dinputDllName, "\\dinput8.dll" );
#else
	strcat( dinputDllName, "\\dinput.dll" );
#endif

	dinputDll = LoadLibraryA( dinputDllName );

	// MSDN: If the function succeeds, the return value is greater than 31.
	if( dinputDll > ( HMODULE )31 ) {

#ifdef DI_WRAPPER8
		DirectInput8Create = ( DIRECTINPUT8CREATEPROC )GetProcAddress( dinputDll, "DirectInput8Create" );
		if( !DirectInput8Create ) {
#else
		DirectInputCreateA = ( DIRECTINPUTCREATEAPROC )GetProcAddress( dinputDll, "DirectInputCreateA" );
		if( !DirectInputCreateA ) {
#endif

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

	bufferedMode = false;

	if( hookHandleGetMessage ) {
		
		UnhookWindowsHookEx( hookHandleGetMessage );
		hookHandleGetMessage = 0;
	}

	if( hookHandleCallWnd ) {
		
		UnhookWindowsHookEx( hookHandleCallWnd );
		hookHandleCallWnd = 0;
	}

	FreeLibrary( dinputDll );
}

void WrapperSystem::InitHID( HWND & h_wnd ) {

	RECT windowRect = { 0 };

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
	if( !( hookHandleGetMessage = SetWindowsHookExA( WH_GETMESSAGE, WindowHookFuncGetMessage, ( HINSTANCE )wrapperModule, GetCurrentThreadId( ) ) ) ) return;
	if( !( hookHandleCallWnd = SetWindowsHookExA( WH_CALLWNDPROC, WindowHookFuncCallWnd, ( HINSTANCE )wrapperModule, GetCurrentThreadId( ) ) ) ) return;

	hidInitialized = true;
}

LRESULT WrapperSystem::WindowHookFuncCallWnd( int nCode, WPARAM wParam, LPARAM lParam ) {

	PCWPSTRUCT cwpData = ( PCWPSTRUCT )lParam;

	if( cwpData->message == WM_ACTIVATEAPP ) {

		for( WrapperList::iterator wrpItr = wrappers.begin( ); wrpItr != wrappers.end( ); wrpItr++ ) {

			( *wrpItr )->SendAdjustExclusiveMode( cwpData->wParam ? false : true );
		}

	} else if( cwpData->message == WM_SIZE ) {

		for( WrapperList::iterator wrpItr = wrappers.begin( ); wrpItr != wrappers.end( ); wrpItr++ ) {

			( *wrpItr )->SendAdjustExclusiveMode( false );
		}

	} else if( cwpData->message == WM_ENTERSIZEMOVE || cwpData->message == WM_ENTERMENULOOP ) {

		for( WrapperList::iterator wrpItr = wrappers.begin( ); wrpItr != wrappers.end( ); wrpItr++ ) {

			( *wrpItr )->SendAdjustExclusiveMode( true );
		}
	}

	if( nCode < 0 ) return CallNextHookEx( 0, nCode, wParam, lParam );
	return TRUE;
}

LRESULT WrapperSystem::WindowHookFuncGetMessage( int nCode, WPARAM wParam, LPARAM lParam ) {

	MSG * cwpData = ( MSG * )lParam;

	if( cwpData->message == WM_INPUT ) {

		byte lpb[ 48 ];
		RAWINPUT * raw;
		DWORD dwRet;
		UINT dwSize = 48;
    
		dwRet = GetRawInputData( ( HRAWINPUT )cwpData->lParam, RID_INPUT, lpb, &dwSize, sizeof( RAWINPUTHEADER ) );

		raw = ( RAWINPUT * )lpb;

		if( raw->header.dwType == RIM_TYPEMOUSE && !raw->data.mouse.usFlags ) {

			DI_HID_MouseEvent mouseEvent = raw->data.mouse;
			lastEventTime = GetTickCount( );
			mouseEvent.SetTime( lastEventTime );
			DI_HID_MouseEvent mouseEventWrap;

			if( mouseEvent.Wrap( mouseEventWrap, type_axis_x ) ) PushEvent( mouseEventWrap );
			if( mouseEvent.Wrap( mouseEventWrap, type_axis_y ) ) PushEvent( mouseEventWrap );
			if( mouseEvent.Wrap( mouseEventWrap, type_axis_z ) ) PushEvent( mouseEventWrap );
			if( mouseEvent.Wrap( mouseEventWrap, type_button_1 ) ) PushEvent( mouseEventWrap );
			if( mouseEvent.Wrap( mouseEventWrap, type_button_2 ) ) PushEvent( mouseEventWrap );
			if( mouseEvent.Wrap( mouseEventWrap, type_button_3 ) ) PushEvent( mouseEventWrap );
			if( mouseEvent.Wrap( mouseEventWrap, type_button_4 ) ) PushEvent( mouseEventWrap );
			if( mouseEvent.Wrap( mouseEventWrap, type_button_5 ) ) PushEvent( mouseEventWrap );
			if( mouseEvent.Wrap( mouseEventWrap, type_button_6 ) ) { PushEvent( mouseEventWrap ); PushEvent( button6ShutdownEvent ); }
			if( mouseEvent.Wrap( mouseEventWrap, type_button_7 ) ) { PushEvent( mouseEventWrap ); PushEvent( button7ShutdownEvent ); }

			return TRUE;
		}
	}

	if( nCode < 0 ) return CallNextHookEx( 0, nCode, wParam, lParam );
	return TRUE;
}

DI_HID_WrapperBase * WrapperSystem::AllocWrapper( ) {

	DI_HID_WrapperBase * wrapper = new DI_HID_WrapperBase( );
	wrappers.push_back( wrapper );
	return wrapper;
}

//
// DI_HID_MouseEvent
//

bool DI_HID_MouseEvent::Wrap( DI_HID_MouseEvent & obj_wrap, int obj_type ) {
	
	switch( obj_type ) {

		case type_axis_x:

			if( !this->lLastX ) return false;
				
			WrapperSystem::ImmediateBuffer( type_axis_x ) += this->lLastX;
			obj_wrap.Clear( );
			obj_wrap.lLastX = this->lLastX;
			break;

		case type_axis_y:

			if( !this->lLastY ) return false;
				
			WrapperSystem::ImmediateBuffer( type_axis_y ) += this->lLastY;
			obj_wrap.Clear( );
			obj_wrap.lLastY = this->lLastY;
			break;

		case type_axis_z:

			if( !( this->usButtonFlags & RI_MOUSE_WHEEL ) ) return false;
				
			WrapperSystem::ImmediateBuffer( type_axis_z ) += ( *( short * )&this->usButtonData ) > 0 ? 1 : -1;
			obj_wrap.Clear( );
			obj_wrap.usButtonFlags = RI_MOUSE_WHEEL;
			obj_wrap.usButtonData = this->usButtonData;
			break;

		case type_button_1:

			if( this->usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN ) {

				WrapperSystem::ImmediateBuffer( type_button_1 ) = 0x80;
				obj_wrap.Clear( );
				obj_wrap.usButtonFlags = RI_MOUSE_LEFT_BUTTON_DOWN;

			} else if( this->usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP ) {

				WrapperSystem::ImmediateBuffer( type_button_1 ) = 0;
				obj_wrap.Clear( );
				obj_wrap.usButtonFlags = RI_MOUSE_LEFT_BUTTON_UP;

			} else return false;

			break;

		case type_button_2:

			if( this->usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN ) {

				WrapperSystem::ImmediateBuffer( type_button_2 ) = 0x80;
				obj_wrap.Clear( );
				obj_wrap.usButtonFlags = RI_MOUSE_RIGHT_BUTTON_DOWN;

			} else if( this->usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP ) {

				WrapperSystem::ImmediateBuffer( type_button_2 ) = 0;
				obj_wrap.Clear( );
				obj_wrap.usButtonFlags = RI_MOUSE_RIGHT_BUTTON_UP;

			} else return false;

			break;

		case type_button_3:

			if( this->usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN ) {

				WrapperSystem::ImmediateBuffer( type_button_3 ) = 0x80;
				obj_wrap.Clear( );
				obj_wrap.usButtonFlags = RI_MOUSE_MIDDLE_BUTTON_DOWN;

			} else if( this->usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP ) {

				WrapperSystem::ImmediateBuffer( type_button_3 ) = 0;
				obj_wrap.Clear( );
				obj_wrap.usButtonFlags = RI_MOUSE_MIDDLE_BUTTON_UP;

			} else return false;

			break;

		case type_button_4:

			if( this->usButtonFlags & RI_MOUSE_BUTTON_4_DOWN ) {

				WrapperSystem::ImmediateBuffer( type_button_4 ) = 0x80;
				obj_wrap.Clear( );
				obj_wrap.usButtonFlags = RI_MOUSE_BUTTON_4_DOWN;

			} else if( this->usButtonFlags & RI_MOUSE_BUTTON_4_UP ) {

				WrapperSystem::ImmediateBuffer( type_button_4 ) = 0;
				obj_wrap.Clear( );
				obj_wrap.usButtonFlags = RI_MOUSE_BUTTON_4_UP;

			} else return false;

			break;

		case type_button_5:

			if( this->usButtonFlags & RI_MOUSE_BUTTON_5_DOWN ) {

				WrapperSystem::ImmediateBuffer( type_button_5 ) = 0x80;
				obj_wrap.Clear( );
				obj_wrap.usButtonFlags = RI_MOUSE_BUTTON_5_DOWN;

			} else if( this->usButtonFlags & RI_MOUSE_BUTTON_5_UP ) {

				WrapperSystem::ImmediateBuffer( type_button_5 ) = 0;
				obj_wrap.Clear( );
				obj_wrap.usButtonFlags = RI_MOUSE_BUTTON_5_UP;

			} else return false;

			break;

		case type_button_6:

			if( !( this->usButtonFlags & 0x800 ) || ( *( short * )&this->usButtonData ) > 0 ) return false;
				
			WrapperSystem::ImmediateBuffer( type_button_6 ) = this->usButtonData ? 0x80 : 0;
			obj_wrap.Clear( );
			obj_wrap.usButtonFlags = 0x800;
			obj_wrap.usButtonData = this->usButtonData;
			break;

		case type_button_7:

			if( !( this->usButtonFlags & 0x800 ) || ( *( short * )&this->usButtonData ) < 0 ) return false;
				
			WrapperSystem::ImmediateBuffer( type_button_7 ) = this->usButtonData ? 0x80 : 0;
			obj_wrap.Clear( );
			obj_wrap.usButtonFlags = 0x800;
			obj_wrap.usButtonData = this->usButtonData;
			break;
	}

	assert( obj_type != -1 );
	obj_wrap.SetType( obj_type );
	obj_wrap.eventTime = this->eventTime;

	return true;
}

//
// DI_HID_WrapperBase
//

DI_HID_WrapperBase::DI_HID_WrapperBase( ) {

	this->refCount = 0;
	this->diInterface = 0;
}

DI_HID_WrapperBase::~DI_HID_WrapperBase( ) {

	for( DeviceList::iterator devItr = this->devices.begin( ); devItr != this->devices.end( ); devItr++ ) {

		delete *devItr;
	}

	this->diInterface->Release( );
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

void DI_HID_WrapperBase::SendAdjustExclusiveMode( bool disable_capture ) {

	for( DeviceList::iterator devItr = this->devices.begin( ); devItr != this->devices.end( ); devItr++ ) {

		( *devItr )->AdjustExclusiveMode( disable_capture );
	}
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

	HRESULT res = diInterface->CreateDevice( r_guid, ( DIDeviceStructPtr * )( *di_device )->GetDIPtr( ), p_unk );

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

#ifdef DI_WRAPPER8

HRESULT DI_HID_WrapperBase::EnumDevicesBySemantics( LPCSTR user_name, LPDIACTIONFORMATA action_format, LPDIENUMDEVICESBYSEMANTICSCBA callback, LPVOID cb_userdata, DWORD flags ) {

	return S_OK;
}

HRESULT DI_HID_WrapperBase::ConfigureDevices( LPDICONFIGUREDEVICESCALLBACK callback, LPDICONFIGUREDEVICESPARAMSA device_config, DWORD flags, LPVOID ref_data ) {

	return S_OK;
}

#endif

//
// DI_HID_DeviceBase
//

DI_HID_DeviceBase::DI_HID_DeviceBase( ) {

	this->refCount = 0;
	this->diDevice = NULL;
	this->seqNumber = 0;
	this->exclusiveMode = false;
	this->foregroundMode = false;
	this->captureDisabled = true;
	this->isAcquired = false;
	this->absoluteAxis = false;
	memset( &this->windowRect, 0, sizeof( RECT ) );

	DI_HID_Object obj;

	obj.guidType = GUID_DI_XAxis;
	obj.dwType = DIDFT_AXIS | DIDFT_MAKEINSTANCE( 0 );
	obj.dwOfs = DIMOFS_X;
	strcpy( obj.tszName, "X-Axis" );
	obj.SetSize( 4 );
	obj.SetType( type_axis_x );
	objects.push_back( obj );

	obj.guidType = GUID_DI_YAxis;
	obj.dwType = DIDFT_AXIS | DIDFT_MAKEINSTANCE( 1 );
	obj.dwOfs = DIMOFS_Y;
	strcpy( obj.tszName, "Y-Axis" );
	obj.SetSize( 4 );
	obj.SetType( type_axis_y );
	objects.push_back( obj );

	obj.guidType = GUID_DI_ZAxis;
	obj.dwType = DIDFT_AXIS | DIDFT_MAKEINSTANCE( 2 );
	obj.dwOfs = DIMOFS_Z;
	strcpy( obj.tszName, "Z-Axis" );
	obj.SetSize( 4 );
	obj.SetType( type_axis_z );
	objects.push_back( obj );

	obj.guidType = GUID_DI_Button;
	obj.dwType = DIDFT_BUTTON | DIDFT_MAKEINSTANCE( 0 );
	obj.dwOfs = DIMOFS_BUTTON0;
	strcpy( obj.tszName, "Left Button" );
	obj.SetSize( 1 );
	obj.SetType( type_button_1 );
	objects.push_back( obj );

	obj.guidType = GUID_DI_Button;
	obj.dwType = DIDFT_BUTTON | DIDFT_MAKEINSTANCE( 1 );
	obj.dwOfs = DIMOFS_BUTTON1;
	strcpy( obj.tszName, "Right Button" );
	obj.SetSize( 1 );
	obj.SetType( type_button_2 );
	objects.push_back( obj );

	obj.guidType = GUID_DI_Button;
	obj.dwType = DIDFT_BUTTON | DIDFT_MAKEINSTANCE( 2 );
	obj.dwOfs = DIMOFS_BUTTON2;
	strcpy( obj.tszName, "Middle Button" );
	obj.SetSize( 1 );
	obj.SetType( type_button_3 );
	objects.push_back( obj );

	obj.guidType = GUID_DI_Button;
	obj.dwType = DIDFT_BUTTON | DIDFT_MAKEINSTANCE( 3 );
	obj.dwOfs = DIMOFS_BUTTON3;
	strcpy( obj.tszName, "Button 4" );
	obj.SetSize( 1 );
	obj.SetType( type_button_4 );
	objects.push_back( obj );

	obj.guidType = GUID_DI_Button;
	obj.dwType = DIDFT_BUTTON | DIDFT_MAKEINSTANCE( 4 );
	obj.dwOfs = DIMOFS_BUTTON4;
	strcpy( obj.tszName, "Button 5" );
	obj.SetSize( 1 );
	obj.SetType( type_button_5 );
	objects.push_back( obj );

	obj.guidType = GUID_DI_Button;
	obj.dwType = DIDFT_BUTTON | DIDFT_MAKEINSTANCE( 5 );
	obj.dwOfs = DIMOFS_BUTTON5;
	strcpy( obj.tszName, "Wheel Left" );
	obj.SetSize( 1 );
	obj.SetType( type_button_6 );
	objects.push_back( obj );

	obj.guidType = GUID_DI_Button;
	obj.dwType = DIDFT_BUTTON | DIDFT_MAKEINSTANCE( 6 );
	obj.dwOfs = DIMOFS_BUTTON6;
	strcpy( obj.tszName, "Wheel Right" );
	obj.SetSize( 1 );
	obj.SetType( type_button_7 );
	objects.push_back( obj );

	initTime = GetTickCount( );
}

DI_HID_DeviceBase::~DI_HID_DeviceBase( ) {
}

DI_HID_Object * DI_HID_DeviceBase::FindType( int obj_type ) {

	for( ObjectList::iterator objItr = dataFormat.begin( ); objItr != dataFormat.end( ); objItr++ ) {

		if( objItr->GetType( ) == obj_type ) return &( *objItr );
	}

	return NULL;
}

DI_HID_Object * DI_HID_DeviceBase::FetchEvent( ) {

	DI_HID_Object * objPtr;
	DI_HID_MouseEvent mouseEvent;

	while( 1 ) {

		if( !WrapperSystem::eventList.size( ) ) return NULL;

		mouseEvent = WrapperSystem::eventList.front( );
		WrapperSystem::eventList.pop_front( );

		if( objPtr = FindType( mouseEvent.GetType( ) ) ) break;
	}

	switch( objPtr->GetType( ) ) {

		case type_axis_x:	objPtr->SetData( mouseEvent.lLastX );														break;
		case type_axis_y:	objPtr->SetData( mouseEvent.lLastY );														break;
		case type_axis_z:	objPtr->SetData( ( *( short * )&mouseEvent.usButtonData ) > 0 ? 1 : -1 );					break;
		case type_button_1:	objPtr->SetData( ( mouseEvent.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN ) ? 0x80 : 0 );		break;
		case type_button_2:	objPtr->SetData( ( mouseEvent.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN ) ? 0x80 : 0 );	break;
		case type_button_3:	objPtr->SetData( ( mouseEvent.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN ) ? 0x80 : 0 );	break;
		case type_button_4:	objPtr->SetData( ( mouseEvent.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN ) ? 0x80 : 0 );		break;
		case type_button_5:	objPtr->SetData( ( mouseEvent.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN ) ? 0x80 : 0 );		break;
		case type_button_6:	objPtr->SetData( mouseEvent.usButtonData ? 0x80 : 0 );										break;
		case type_button_7:	objPtr->SetData( mouseEvent.usButtonData ? 0x80 : 0 );										break;
	}

	objPtr->SetTime( mouseEvent.GetTime( ) - initTime );

	return objPtr;
}

bool DI_HID_DeviceBase::AlreadyInFormatList( const DI_HID_Object & obj ) {

	for( ObjectList::iterator objItr = dataFormat.begin( ); objItr != dataFormat.end( ); objItr++ ) {

		if( obj.GetType( ) == objItr->GetType( ) ) return true;
	}

	return false;
}

void DI_HID_DeviceBase::AddToFormatList( const DI_HID_Object & obj, const DIOBJECTDATAFORMAT & obj_format ) {

	DI_HID_Object object = obj;
	object.dwOfs = obj_format.dwOfs;
	dataFormat.push_back( object );
}

void DI_HID_DeviceBase::AdjustExclusiveMode( bool disable_capture ) {

	if( disable_capture && !this->captureDisabled ) {

		ClipCursor( NULL );
		if( this->exclusiveMode ) ShowCursor( TRUE );
		this->captureDisabled = true;
	}
	
	if( !disable_capture ) {

		if( this->exclusiveMode ) {

			GetClientRect( this->windowHandle, &this->windowRect );
			ClipCursor( &this->windowRect );

			if( this->captureDisabled ) {

				ShowCursor( FALSE );
			}
		}

		this->captureDisabled = false;
	}
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

			WrapperSystem::SetBufferedMode( ( ( LPDIPROPDWORD )dip )->dwData ? true : false );
			return S_OK;
	}

	return DIERR_UNSUPPORTED;
}

HRESULT DI_HID_DeviceBase::Acquire( ) {

	if( this->isAcquired ) return S_FALSE;
	AdjustExclusiveMode( false );
	if( this->foregroundMode && this->captureDisabled ) return DIERR_INPUTLOST;
	this->isAcquired = true;
	return S_OK;
}

HRESULT DI_HID_DeviceBase::Unacquire( ) {

	if( !this->isAcquired ) return DI_NOEFFECT;
	AdjustExclusiveMode( true );
	this->isAcquired = false;
	return S_OK;
}

HRESULT DI_HID_DeviceBase::GetDeviceState( DWORD buf_size, LPVOID buf_data ) {

	if( !this->isAcquired ) return DIERR_NOTACQUIRED;

	for( ObjectList::iterator objItr = dataFormat.begin( ); objItr != dataFormat.end( ); objItr++ ) {

		if( ( objItr->dwOfs + objItr->GetSize( ) ) > buf_size ) {
			
			return DIERR_INVALIDPARAM;
		}

		if( objItr->GetSize( ) == 1 ) {

			( ( unsigned char * )buf_data )[ objItr->dwOfs ] = ( unsigned char )WrapperSystem::ImmediateBuffer( objItr->GetType( ) );

		} else if( objItr->GetSize( ) == 4 ) {

			( ( LONG * )buf_data )[ objItr->dwOfs >> 2 ] = WrapperSystem::ImmediateBuffer( objItr->GetType( ) );
		}
	}

	if( !this->absoluteAxis ) {

		WrapperSystem::ImmediateBuffer( type_axis_x ) = 0;
		WrapperSystem::ImmediateBuffer( type_axis_y ) = 0;
		WrapperSystem::ImmediateBuffer( type_axis_z ) = 0;
	}

	return S_OK;
}

HRESULT DI_HID_DeviceBase::GetDeviceData( DWORD buf_size, LPDIDEVICEOBJECTDATA buf_data, LPDWORD out_size, DWORD flags ) {

	DWORD outIndex;
	DI_HID_Object * deviceEvent;

	assert( !flags ); // noremove not implemented yet

	// quake3 needs this
	if( !this->isAcquired ) return DIERR_NOTACQUIRED;

	for( outIndex = 0; outIndex < *out_size; outIndex++, buf_data++ ) {

		if( !( deviceEvent = FetchEvent( ) ) ) break;

		buf_data->dwData = deviceEvent->GetData( );
		buf_data->dwOfs = deviceEvent->dwOfs;
		buf_data->dwTimeStamp = deviceEvent->GetTime( );
		buf_data->dwSequence = this->seqNumber++;
	}

	*out_size = outIndex;
	return WrapperSystem::eventList.size( ) ? DI_BUFFEROVERFLOW : S_OK;
}

HRESULT DI_HID_DeviceBase::SetDataFormat( LPCDIDATAFORMAT data_format ) {

	this->absoluteAxis = data_format->dwFlags == DIDF_ABSAXIS;

	dataFormat.clear( );

	for( DWORD i = 0; i < data_format->dwNumObjs; i++ ) {

		bool found = false;

		for( ObjectList::iterator objItr = objects.begin( ); objItr != objects.end( ); objItr++ ) {

			if( AlreadyInFormatList( *objItr ) ) continue;

			if( ( data_format->rgodf[ i ].dwType & DIDFT_ANYINSTANCE ) != DIDFT_ANYINSTANCE ) {

				if( data_format->rgodf[ i ].dwType == objItr->dwType ) {

					AddToFormatList( *objItr, data_format->rgodf[ i ] );
					found = true;
					break;
				}
			}
		}

		for( ObjectList::iterator objItr = objects.begin( ); !found && objItr != objects.end( ); objItr++ ) {

			if( AlreadyInFormatList( *objItr ) ) continue;

			if( data_format->rgodf[ i ].pguid ) {
				
				if( ( *data_format->rgodf[ i ].pguid == objItr->guidType ) ) {

					AddToFormatList( *objItr, data_format->rgodf[ i ] );
					found = true;
					break;
				}
			}

		}

		for( ObjectList::iterator objItr = objects.begin( ); !found && objItr != objects.end( ); objItr++ ) {

			if( AlreadyInFormatList( *objItr ) ) continue;

			if( ( data_format->rgodf[ i ].dwType & DIDFT_ANYINSTANCE ) == DIDFT_ANYINSTANCE ) {

				if( DIDFT_GETTYPE( data_format->rgodf[ i ].dwType ) == DIDFT_GETTYPE( objItr->dwType ) ) {

					AddToFormatList( *objItr, data_format->rgodf[ i ] );
					break;
				}
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

	this->windowHandle = h_wnd;
	WrapperSystem::InitHID( this->windowHandle );

	this->exclusiveMode = level & DISCL_EXCLUSIVE ? true : false;
	this->foregroundMode = level & DISCL_FOREGROUND ? true : false;

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

	if( !this->isAcquired ) return DIERR_NOTACQUIRED;
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

#ifdef DI_WRAPPER8

HRESULT DI_HID_DeviceBase::BuildActionMap( LPDIACTIONFORMATA action_info, LPCSTR user_name, DWORD flags ) {

	return S_OK;
}

HRESULT DI_HID_DeviceBase::SetActionMap( LPDIACTIONFORMATA action_info, LPCSTR user_name, DWORD flags ) {

	return S_OK;
}

HRESULT DI_HID_DeviceBase::GetImageInfo( LPDIDEVICEIMAGEINFOHEADERA image_info ) {

	return S_OK;
}

#endif
