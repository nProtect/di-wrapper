#ifndef DI_WRAP_H
#define DI_WRAP_H

// without this define, sadly compiler wants to use non-portable (ms only) library funcs
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <assert.h>
#include <list>

// with 0x0700, compiler throws a weird "second C linkage for DirectInputCreateA" message
#define DIRECTINPUT_VERSION 0x0800
#include "dinput.h"

#ifndef HID_USAGE_PAGE_GENERIC
#define HID_USAGE_PAGE_GENERIC 1
#endif
#ifndef HID_USAGE_GENERIC_MOUSE
#define HID_USAGE_GENERIC_MOUSE 2
#endif

class DI_HID_Object;
class DI_HID_DeviceBase;
class DI_HID_WrapperBase;

typedef std::list< DI_HID_Object >			ObjectList;
typedef std::list< DI_HID_DeviceBase * >	DeviceList;
typedef std::list< DIDEVICEINSTANCEA >		DeviceInstanceList;
typedef std::list< DI_HID_WrapperBase * >	WrapperList;
typedef std::list< RAWMOUSE >				MouseEventList;

// neded for the original dll func
typedef HRESULT ( WINAPI * DIRECTINPUTCREATEAPROC )( HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUTA * ppDI, LPUNKNOWN punkOuter );

// func prototype for our hook
typedef LRESULT ( CALLBACK * HOOKWNDPROC )( int nCode, WPARAM wParam, LPARAM lParam );

class DI_HID_Object : public DIDEVICEOBJECTINSTANCEA {

private:

	LONG *										dataPtr;
	int											dataSize;

public:

												DI_HID_Object( ) { memset( this, 0, sizeof( DI_HID_Object ) ); this->dwSize = sizeof( DIDEVICEOBJECTINSTANCEA ); }
	
	void										SetDataPtr( LONG * ptr ) { dataPtr = ptr; }
	LONG										GetData( ) { return *dataPtr; }

	void										SetSize( int size ) { this->dataSize = size; }
	int											GetSize( ) const { return dataSize; }
};

class DI_HID_DeviceBase : public IDirectInputDevice7A {

private:

	ULONG										refCount;
	IDirectInputDevice7A *						diDevice;
	bool										passTruh;
	DWORD										seqNumber;
	DWORD										initTime;
	ObjectList									objects;
	ObjectList::iterator						seqIterator;
	ObjectList									dataFormat;
	LONG										accumlateX;
	LONG										accumlateY;
	LONG										accumlateZ;
	LONG										accumlateButton1;
	LONG										accumlateButton2;
	LONG										accumlateButton3;
	LONG										accumlateButton4;
	LONG										accumlateButton5;
	LONG										accumlateButton6;
	LONG										accumlateButton7;

public:

												DI_HID_DeviceBase( );

	IDirectInputDevice7A **						GetDIPtr( ) { return &diDevice; }
	void										PassTruhMode( bool state ) { passTruh = state; }
	void										Accumlate( );

	// IUnknown
	virtual HRESULT WINAPI						QueryInterface( const IID & r_iid, LPVOID * p_obj );
	virtual ULONG WINAPI						AddRef( );
	virtual ULONG WINAPI						Release( );

	// DirectInputDevice2A
	virtual HRESULT WINAPI						GetCapabilities( LPDIDEVCAPS dev_caps );
	virtual HRESULT WINAPI						EnumObjects( LPDIENUMDEVICEOBJECTSCALLBACKA callback, LPVOID cb_userdata, DWORD flags );
	virtual HRESULT WINAPI						GetProperty( const IID & r_iid, LPDIPROPHEADER dip );
	virtual HRESULT WINAPI						SetProperty( const IID & r_iid, LPCDIPROPHEADER dip );
	virtual HRESULT WINAPI						Acquire( );
	virtual HRESULT WINAPI						Unacquire( );
	virtual HRESULT WINAPI						GetDeviceState( DWORD buf_size, LPVOID buf_data );
	virtual HRESULT WINAPI						GetDeviceData( DWORD buf_size, LPDIDEVICEOBJECTDATA buf_data, LPDWORD out_size, DWORD flags );
	virtual HRESULT WINAPI						SetDataFormat( LPCDIDATAFORMAT data_format );
	virtual HRESULT WINAPI						SetEventNotification( HANDLE h_event );
	virtual HRESULT WINAPI						SetCooperativeLevel( HWND h_wnd, DWORD level );
	virtual HRESULT WINAPI						GetObjectInfo( LPDIDEVICEOBJECTINSTANCEA obj_inst, DWORD obj, DWORD how );
	virtual HRESULT WINAPI						GetDeviceInfo( LPDIDEVICEINSTANCEA info );
	virtual HRESULT WINAPI						RunControlPanel( HWND h_wnd, DWORD flags );
	virtual HRESULT WINAPI						Initialize( HINSTANCE inst, DWORD version, const IID & r_iid );
	virtual HRESULT WINAPI						CreateEffect( const IID & r_iid, LPCDIEFFECT fx, LPDIRECTINPUTEFFECT * di_fx, LPUNKNOWN p_unk );
	virtual HRESULT WINAPI						EnumEffects( LPDIENUMEFFECTSCALLBACKA callback, LPVOID cb_userdata, DWORD fx_type );
	virtual HRESULT WINAPI						GetEffectInfo( LPDIEFFECTINFOA fx, const IID & r_iid );
	virtual HRESULT WINAPI						GetForceFeedbackState( LPDWORD out );
	virtual HRESULT WINAPI						SendForceFeedbackCommand( DWORD flags );
	virtual HRESULT WINAPI						EnumCreatedEffectObjects( LPDIENUMCREATEDEFFECTOBJECTSCALLBACK callback, LPVOID cb_userdata, DWORD flags );
	virtual HRESULT WINAPI						Escape( LPDIEFFESCAPE esc );
	virtual HRESULT WINAPI						Poll( );
	virtual HRESULT WINAPI						SendDeviceData( DWORD buf_size, LPCDIDEVICEOBJECTDATA buf_data, LPDWORD out_size, DWORD flags );

	// IDirectInputDevice7A
	virtual HRESULT WINAPI						EnumEffectsInFile( LPCSTR file_name, LPDIENUMEFFECTSINFILECALLBACK callback, LPVOID cb_userdata, DWORD flags );
	virtual HRESULT WINAPI						WriteEffectToFile( LPCSTR file_name, DWORD count, LPDIFILEEFFECT data, DWORD flags );
};

class DI_HID_WrapperBase {

private:

	ULONG										refCount;
	IDirectInput7A *							diInterface;
	DeviceList									devices;

public:

												DI_HID_WrapperBase( );
												//~DI_HID_WrapperBase( );

	static BOOL	CALLBACK						EnumDevicesCallback( LPCDIDEVICEINSTANCEA dev_inst, LPVOID cb_userdata );

	IDirectInput7A **							GetDIPtr( ) { return &diInterface; }
	IDirectInput7A &							GetDI( ) { return *diInterface; }
	DI_HID_DeviceBase *							AllocDevice( );

	// IUnknown
	virtual HRESULT WINAPI						QueryInterface( const IID & r_iid, LPVOID * p_obj );
	virtual ULONG WINAPI						AddRef( );
	virtual ULONG WINAPI						Release( );

	// DirectInput
	virtual HRESULT WINAPI						CreateDevice( const GUID & r_guid, DI_HID_DeviceBase ** di_device, LPUNKNOWN p_unk );
	virtual HRESULT WINAPI						EnumDevices( DWORD dev_type, LPDIENUMDEVICESCALLBACKA callback, LPVOID cb_userdata, DWORD flags );
	virtual HRESULT WINAPI						GetDeviceStatus( const GUID & r_guid );
	virtual HRESULT WINAPI						RunControlPanel( HWND win_handle, DWORD flags );
	virtual HRESULT WINAPI						Initialize( HINSTANCE inst_handle, DWORD version );
	virtual HRESULT WINAPI						FindDevice( const GUID & r_guid, LPCSTR dev_name, LPGUID dev_guid );
};

class WrapperSystem {

private:

	static HMODULE								dinputDll;
	static WrapperList							wrappers;
	static HHOOK								hookHandle;
	static bool									hidInitialized;
	static HANDLE								wrapperModule;

public:

	static MouseEventList						eventList;

	static DIRECTINPUTCREATEAPROC				DirectInputCreateA;
	static LRESULT CALLBACK						WindowHookFunc( int nCode, WPARAM wParam, LPARAM lParam );

	static bool									Init( HANDLE mod_hnd );
	static void									Shutdown( );
	static void									InitHID( HWND h_wnd );

	static DI_HID_WrapperBase *					AllocWrapper( );
};

#endif
