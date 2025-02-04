/*
SageThumbs - Thumbnail image shell extension.

Copyright (C) Nikolay Raspopov, 2004-2013.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "stdafx.h"
#include "SageThumbs.h"
#include "Thumb.h"
#include "SQLite.h"
#include <InitGuid.h>

DEFINE_GUID(CLSID_Thumb,0x4A34B3E3,0xF50E,0x4FF6,0x89,0x79,0x7E,0x41,0x76,0x46,0x6F,0xF2);

CThumb::CThumb() :
	m_uOurItemID( 0 ),
	m_cx( THUMB_STORE_SIZE ),
	m_cy( THUMB_STORE_SIZE ),
	m_bCleanup( FALSE )
{
}

HRESULT CThumb::FinalConstruct()
{
	ATLTRACE( "CThumb - FinalConstruct()\n" );

	return CoCreateFreeThreadedMarshaler( GetControllingUnknown(), &m_pUnkMarshaler.p );
}

void CThumb::FinalRelease()
{
	m_Filenames.RemoveAll();

	m_sFilename.Empty();

#ifdef ISTREAM_ENABLED
	m_pStream.Release();
#endif // ISTREAM_ENABLED

	m_pSite.Release();

	m_pUnkMarshaler.Release();

	ATLTRACE( "CThumb - FinalRelease()\n" );
}

// IShellExtInit

STDMETHODIMP CThumb::Initialize(LPCITEMIDLIST, IDataObject* pDO, HKEY)
{
	if ( ! pDO )
	{
		ATLTRACE( "CThumb - IShellExtInit::Initialize() : E_INVALIDARG (No data)\n" );
		return E_INVALIDARG;
	}

	bool bEnableMenu = GetRegValue( _T("EnableMenu"), 1ul ) != 0;
	if ( ! bEnableMenu )
	{
		ATLTRACE( "CThumb - IShellExtInit::Initialize() : E_INVALIDARG (Menu disabled)\n" );
		return E_INVALIDARG;
	}

	// ��������� ������ � ���������� ���������
	FORMATETC fe = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
	STGMEDIUM med = { TYMED_HGLOBAL, NULL, NULL };
	HRESULT hr = pDO->GetData( &fe, &med );
	if ( FAILED( hr ) )
	{
		ATLTRACE( "CThumb - IShellExtInit::Initialize() : E_INVALIDARG (No data)\n" );
		return E_INVALIDARG;
	}

    HDROP hDrop = (HDROP)GlobalLock( med.hGlobal );
    if ( ! hDrop )
	{
		ReleaseStgMedium (&med);
		ATLTRACE( "CThumb - IShellExtInit::Initialize() : E_INVALIDARG (No data)\n" );
		return E_INVALIDARG;
	}

	// ����� ���������� ���� ��������
	UINT count = DragQueryFile( hDrop, 0xFFFFFFFF, 0, 0 );
	for ( UINT i = 0; i < count; i++ )
	{
		CString filename;
		LPTSTR buf = filename.GetBuffer( MAX_LONG_PATH );
		DWORD len = DragQueryFile( hDrop, i, buf, MAX_LONG_PATH - 1 );
		buf[ len ] = _T('\0');
		filename.ReleaseBuffer();

		if ( _Module.IsGoodFile( filename ) )
		{
			m_Filenames.AddTail( filename );
		}
		else
		{
			ATLTRACE( "CThumb - IShellExtInit::Initialize() : Ignored file \"%s\"\n", (LPCSTR)CT2A( filename ) );
		}
	}
    GlobalUnlock( med.hGlobal );
	ReleaseStgMedium( &med );

	if ( m_Filenames.IsEmpty() )
	{
		ATLTRACE( "CThumb - IShellExtInit::Initialize() : E_INVALIDARG (No files selected)\n" );
		return E_INVALIDARG;
	}

	ATLTRACE( "CThumb - IShellExtInit::Initialize() : S_OK (%d files, first: \"%s\")\n", m_Filenames.GetCount(), (LPCSTR)CT2A( m_Filenames.GetHead() ) );
	return S_OK;
}

// IContextMenu

#define ID_SUBMENU_ITEM				0
#define ID_CLIPBOARD_ITEM			1
#define ID_THUMBNAIL_ITEM			2
#define ID_OPTIONS_ITEM				3
#define ID_WALLPAPER_STRETCH_ITEM	4
#define ID_WALLPAPER_TILE_ITEM		5
#define ID_WALLPAPER_CENTER_ITEM	6
#define ID_MAIL_IMAGE_ITEM			7
#define ID_MAIL_THUMBNAIL_ITEM		8
#define ID_CONVERT_JPG_ITEM			9
#define ID_CONVERT_GIF_ITEM			10
#define ID_CONVERT_BMP_ITEM			11
#define ID_CONVERT_PNG_ITEM			12
#define ID_THUMBNAIL_INFO_ITEM		13
#define ID_END_ITEM					14

static const LPCTSTR szVerbs[ ID_END_ITEM ] =
{
	_T("submenu"),
	_T("clipboard_image"),
	_T("thumbnail"),
	_T("options"),
	_T("wallpaper_stretch"),
	_T("wallpaper_tile"),
	_T("wallpaper_center"),
	_T("mail_image"),
	_T("mail_thumbnail"),
	_T("convert_jpg"),
	_T("convert_gif"),
	_T("convert_bmp"),
	_T("convert_png"),
	_T("info")
};

STDMETHODIMP CThumb::QueryContextMenu(HMENU hMenu, UINT uIndex, UINT uidCmdFirst, UINT uidCmdLast, UINT uFlags)
{
	// If the flags include CMF_DEFAULTONLY then we shouldn't do anything.
	if ( uFlags & CMF_DEFAULTONLY )
	{
		ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : S_OK (Bypass)\n" );
		return MAKE_HRESULT( SEVERITY_SUCCESS, FACILITY_NULL, 0 );
	}

	bool bEnableMenu = GetRegValue( _T("EnableMenu"), 1ul ) != 0;
	if ( ! bEnableMenu )
	{
		ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Menu disabled)\n" );
		// ���� ���������
		return E_FAIL;
	}

	// �������� �� �������� ���������������
	if ( uidCmdFirst + ID_END_ITEM > uidCmdLast )
	{
		ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (No free IDs)\n" );
		return E_FAIL;
	}

/*#ifdef _DEBUG
	ATLTRACE( "Menu:\n" );
	UINT count = GetMenuItemCount( hMenu );
	for ( UINT i = 0; i < count; ++i )
	{
		TCHAR szItem[ 256 ] = {};
		MENUITEMINFO mii = { sizeof( MENUITEMINFO ), MIIM_FTYPE | MIIM_ID | MIIM_STRING | MIIM_BITMAP | MIIM_CHECKMARKS };
		mii.dwTypeData = szItem;
		mii.cch = _countof( szItem );
		GetMenuItemInfo( hMenu, i, TRUE, &mii );
		ATLTRACE( "%2u. wID=%5d fType=0x%08x %s%s%s", i, mii.wID, mii.fType,
			( mii.hbmpItem ? "{bitmap} " : "" ),
			( mii.hbmpChecked ? "{check} " : "" ),
			( mii.hbmpUnchecked ? "{uncheck} " : "" )  );
		if ( mii.fType & MFT_BITMAP )
			ATLTRACE( "MFT_BITMAP " );
		else if ( mii.fType & MFT_SEPARATOR )
			ATLTRACE( "MFT_SEPARATOR " );
		else if ( mii.fMask & MIIM_STRING )
			ATLTRACE( "MFT_STRING \"%s\" ", (LPCSTR)CT2A( (LPCTSTR)mii.dwTypeData ) );
		ATLTRACE( "\n" );
	}
#endif // _DEBUG*/

	bool bPreviewInSubMenu = GetRegValue( _T("SubMenu"), 1ul ) != 0;
	bool bSingleFile = ( m_Filenames.GetCount () == 1 );

	int nPos = 0;

	// Creating submenu items
	HMENU hSubMenu = CreateMenu ();

	// Clipboard operation items
	if ( bSingleFile )
	{
		if ( ! InsertMenu( hSubMenu, nPos++, MF_BYPOSITION | MF_STRING, uidCmdFirst + ID_CLIPBOARD_ITEM, _Module.m_oLangs.LoadString( IDS_CLIPBOARD ) ) )
		{
			ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert menu item %d)\n", nPos );
			return E_FAIL;
		}
		if ( ! InsertMenu( hSubMenu, nPos++, MF_BYPOSITION | MF_SEPARATOR, 0, 0))
		{
			ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert menu item %d)\n", nPos );
			return E_FAIL;
		}
	}

	// Wallpaper operation items
	if ( bSingleFile )
	{
		if ( ! InsertMenu( hSubMenu, nPos++, MF_BYPOSITION | MF_STRING, uidCmdFirst + ID_WALLPAPER_STRETCH_ITEM, _Module.m_oLangs.LoadString( IDS_WALLPAPER_STRETCH ) ) )
		{
			ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert menu item %d)\n", nPos );
			return E_FAIL;
		}
		if ( ! InsertMenu( hSubMenu, nPos++, MF_BYPOSITION | MF_STRING, uidCmdFirst + ID_WALLPAPER_TILE_ITEM, _Module.m_oLangs.LoadString( IDS_WALLPAPER_TILE ) ) )
		{
			ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert menu item %d)\n", nPos );
			return E_FAIL;
		}
		if ( ! InsertMenu( hSubMenu, nPos++, MF_BYPOSITION | MF_STRING, uidCmdFirst + ID_WALLPAPER_CENTER_ITEM, _Module.m_oLangs.LoadString( IDS_WALLPAPER_CENTER ) ) )
		{
			ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert menu item %d)\n", nPos );
			return E_FAIL;
		}
		if ( ! InsertMenu( hSubMenu, nPos++, MF_BYPOSITION | MF_SEPARATOR, 0, 0))
		{
			ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert menu item %d)\n", nPos );
			return E_FAIL;
		}
	}

	if ( ! InsertMenu( hSubMenu, nPos++, MF_BYPOSITION | MF_STRING, uidCmdFirst + ID_MAIL_IMAGE_ITEM, _Module.m_oLangs.LoadString( IDS_MAIL_IMAGE ) ) )
	{
		ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert menu item %d)\n", nPos );
		return E_FAIL;
	}
	if ( ! InsertMenu( hSubMenu, nPos++, MF_BYPOSITION | MF_STRING, uidCmdFirst + ID_MAIL_THUMBNAIL_ITEM, _Module.m_oLangs.LoadString( IDS_MAIL_THUMBNAIL ) ) )
	{
		ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert menu item %d)\n", nPos );
		return E_FAIL;
	}
	if ( ! InsertMenu( hSubMenu, nPos++, MF_BYPOSITION | MF_SEPARATOR, 0, 0))
	{
		ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert menu item %d)\n", nPos );
		return E_FAIL;
	}
	if ( ! InsertMenu( hSubMenu, nPos++, MF_BYPOSITION | MF_STRING, uidCmdFirst + ID_CONVERT_JPG_ITEM, _Module.m_oLangs.LoadString( IDS_CONVERT_JPG ) ) )
	{
		ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert menu item %d)\n", nPos );
		return E_FAIL;
	}
	if ( ! InsertMenu( hSubMenu, nPos++, MF_BYPOSITION | MF_STRING,uidCmdFirst + ID_CONVERT_GIF_ITEM, _Module.m_oLangs.LoadString( IDS_CONVERT_GIF ) ) )
	{
		ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert menu item %d)\n", nPos );
		return E_FAIL;
	}
	if ( ! InsertMenu( hSubMenu, nPos++, MF_BYPOSITION | MF_STRING, uidCmdFirst + ID_CONVERT_BMP_ITEM, _Module.m_oLangs.LoadString( IDS_CONVERT_BMP ) ) )
	{
		ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert menu item %d)\n", nPos );
		return E_FAIL;
	}
	if ( ! InsertMenu( hSubMenu, nPos++, MF_BYPOSITION | MF_STRING, uidCmdFirst + ID_CONVERT_PNG_ITEM, _Module.m_oLangs.LoadString( IDS_CONVERT_PNG ) ) )
	{
		ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert menu item %d)\n", nPos );
		return E_FAIL;
	}
	if ( ! InsertMenu( hSubMenu, nPos++, MF_BYPOSITION | MF_SEPARATOR, 0, 0))
	{
		ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert menu item %d)\n", nPos );
		return E_FAIL;
	}
	if ( ! InsertMenu( hSubMenu, nPos++, MF_BYPOSITION | MF_STRING, uidCmdFirst + ID_OPTIONS_ITEM, _Module.m_oLangs.LoadString( IDS_OPTIONS ) ) )
	{
		ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert menu item %d)\n", nPos );
		return E_FAIL;
	}

	// Creating main menu items
	if ( ! InsertMenu( hMenu, uIndex++, MF_BYPOSITION | MF_SEPARATOR, 0, 0 ) )
	{
		ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert menu separator)\n" );
		return E_FAIL;
	}

	// Preview menu item
	if ( bSingleFile )
	{
		// �������� �����
		DWORD width = GetRegValue( _T("Width"), THUMB_STORE_SIZE );
		DWORD height = GetRegValue( _T("Height"), THUMB_STORE_SIZE );
		m_Preview.LoadImage( m_Filenames.GetHead(), width, height );

		if ( m_Preview )
		{
			// Store the menu item's ID so we can check against it later when
			// WM_MEASUREITEM/WM_DRAWITEM are sent.
			m_uOurItemID = uidCmdFirst + ID_THUMBNAIL_ITEM;

			if ( IsThemeActive() )
			{
				HBITMAP hPreview = m_Preview.GetImage( width, height );

				BITMAP bm = {};
				GetObject( hPreview, sizeof( BITMAP ), &bm );
				ATLTRACE( "Bitmap %dx%d bytes=%d bits=%d planes=%d type=%d\n", bm.bmHeight, bm.bmWidth, bm.bmWidthBytes, bm.bmBitsPixel, bm.bmPlanes, bm.bmType );

				if ( ! InsertMenu( ( bPreviewInSubMenu ? hSubMenu : hMenu ), ( bPreviewInSubMenu ? 0 : uIndex++ ), MF_BYPOSITION | MF_BITMAP, m_uOurItemID, (LPCTSTR)hPreview ) )
				{
					ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert image menu item)\n" );
					return E_FAIL;
				}
			}
			else
			{
				MENUITEMINFO mii = {};
				mii.cbSize = sizeof (MENUITEMINFO);
				mii.fMask  = MIIM_ID | MIIM_TYPE;
				mii.fType  = MFT_OWNERDRAW;
				mii.wID    = m_uOurItemID;
				if ( ! InsertMenuItem( ( bPreviewInSubMenu ? hSubMenu : hMenu ), ( bPreviewInSubMenu ? 0 : uIndex++ ), TRUE, &mii ) )
				{
					ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert image menu item)\n" );
					return E_FAIL;
				}
			}

			if ( ! InsertMenu( ( bPreviewInSubMenu ? hSubMenu : hMenu ), ( bPreviewInSubMenu ? 1:  uIndex++ ),
				MF_BYPOSITION | MF_STRING | MF_DISABLED, uidCmdFirst + ID_THUMBNAIL_INFO_ITEM, _T("( ") + m_Preview.GetTitleString() + _T(" )") ) )
			{
				ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert image menu item)\n" );
				return E_FAIL;
			}
		}
	}

	CString sAppName = _Module.m_oLangs.LoadString( IDS_PROJNAME );
	MENUITEMINFO mii = { sizeof( MENUITEMINFO ) };
	mii.fMask  = MIIM_STRING | MIIM_SUBMENU | MIIM_ID | MIIM_CHECKMARKS;
	mii.wID = uidCmdFirst + ID_SUBMENU_ITEM;
	mii.hSubMenu = hSubMenu;
	mii.dwTypeData = (LPTSTR)(LPCTSTR)sAppName;
	mii.cch = (UINT)sAppName.GetLength();
	mii.hbmpChecked = mii.hbmpUnchecked = (HBITMAP)LoadImage( _AtlBaseModule.GetResourceInstance(), MAKEINTRESOURCE( IDR_SAGETHUMBS ), IMAGE_BITMAP, 16, 16, LR_SHARED );
	if ( ! InsertMenuItem ( hMenu, uIndex++, TRUE, &mii ) )
	{
		ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert main menu)\n" );
		return E_FAIL;
	}

	if ( ! InsertMenu ( hMenu, uIndex++, MF_BYPOSITION | MF_SEPARATOR, 0, 0 ) )
	{
		ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : E_FAIL (Failed to insert menu separator)\n" );
		return E_FAIL;
	}

	ATLTRACE( "CThumb - IContextMenu::QueryContextMenu() : S_OK\n" );
	return MAKE_HRESULT (SEVERITY_SUCCESS, FACILITY_NULL, ID_END_ITEM);
}

STDMETHODIMP CThumb::GetCommandString (
	UINT_PTR uCmd, UINT uFlags, UINT* /*puReserved*/,
	LPSTR pszName, UINT cchMax )
{
	CString tmp;
	switch ( uFlags )
	{
	case GCS_VERBA:
	case GCS_VERBW:
		if ( uCmd < ID_END_ITEM )
		{
			tmp = CString( _T("SageThumbs.") ) + szVerbs[ uCmd ];
		}
		break;

	case GCS_HELPTEXTA:
	case GCS_HELPTEXTW:
		switch ( uCmd )
		{
		case ID_SUBMENU_ITEM:
			tmp = _Module.GetAppName();
			break;
		case ID_THUMBNAIL_ITEM:
		case ID_THUMBNAIL_INFO_ITEM:
			tmp = m_Preview.GetMenuTipString ();
			break;
		case ID_OPTIONS_ITEM:
			tmp = _Module.m_oLangs.LoadString (IDS_OPTIONS_HELP);
			break;
		case ID_CLIPBOARD_ITEM:
			tmp = _Module.m_oLangs.LoadString (IDS_CLIPBOARD);
			break;
		case ID_WALLPAPER_STRETCH_ITEM:
			tmp = _Module.m_oLangs.LoadString (IDS_WALLPAPER_STRETCH);
			break;
		case ID_WALLPAPER_TILE_ITEM:
			tmp = _Module.m_oLangs.LoadString (IDS_WALLPAPER_TILE);
			break;
		case ID_WALLPAPER_CENTER_ITEM:
			tmp = _Module.m_oLangs.LoadString (IDS_WALLPAPER_CENTER);
			break;
		case ID_MAIL_IMAGE_ITEM:
			tmp = _Module.m_oLangs.LoadString (IDS_MAIL_IMAGE);
			break;
		case ID_MAIL_THUMBNAIL_ITEM:
			tmp = _Module.m_oLangs.LoadString (IDS_MAIL_THUMBNAIL);
			break;
		case ID_CONVERT_JPG_ITEM:
			tmp = _Module.m_oLangs.LoadString (IDS_CONVERT_JPG);
			break;
		case ID_CONVERT_GIF_ITEM:
			tmp = _Module.m_oLangs.LoadString (IDS_CONVERT_GIF);
			break;
		case ID_CONVERT_BMP_ITEM:
			tmp = _Module.m_oLangs.LoadString (IDS_CONVERT_BMP);
			break;
		case ID_CONVERT_PNG_ITEM:
			tmp = _Module.m_oLangs.LoadString (IDS_CONVERT_PNG);
			break;
		default:
			ATLTRACE( "0x%08x::IContextMenu::GetCommandString (%d, %d, 0x%08x \"%s\", %d) E_INVALIDARG\n", this, uCmd, uFlags, pszName, pszName, cchMax);
			return E_INVALIDARG;
		}
		break;

	case GCS_VALIDATEA:
	case GCS_VALIDATEW:
		return S_OK;

	default:
		ATLTRACE( "0x%08x::IContextMenu::GetCommandString (%d, %d, 0x%08x \"%s\", %d) E_INVALIDARG\n", this, uCmd, uFlags, pszName, pszName, cchMax);
		return E_INVALIDARG;
	}

	if ( uFlags & GCS_UNICODE )
		wcsncpy_s( (LPWSTR)pszName, cchMax, (LPCWSTR)CT2W( tmp ), cchMax );
	else
		strncpy_s( (LPSTR)pszName, cchMax, (LPCSTR)CT2A( tmp ), cchMax );

	return S_OK;
}

void CThumb::ConvertTo(HWND hWnd, int ext)
{
	CComPtr< IProgressDialog > pProgress;
	HRESULT hr = pProgress.CoCreateInstance( CLSID_ProgressDialog );
	if ( SUCCEEDED( hr ) )
	{
		pProgress->SetTitle( _Module.GetAppName() );
		pProgress->SetLine( 1, _Module.m_oLangs.LoadString( IDS_CONVERTING ), FALSE, NULL );
		pProgress->StartProgressDialog( hWnd, NULL, PROGDLG_NORMAL | PROGDLG_AUTOTIME, NULL );
	}
	DWORD total = (DWORD)m_Filenames.GetCount(), counter = 0;

	LPCSTR szExt = NULL;
	switch ( ext )
	{
	case ID_CONVERT_JPG_ITEM:
		szExt = "jpeg";
		break;
	case ID_CONVERT_GIF_ITEM:
		szExt = "gif";
		break;
	case ID_CONVERT_BMP_ITEM:
		szExt = "bmp";
		break;
	case ID_CONVERT_PNG_ITEM:
		szExt = "png";
		break;
	default:
		ATLASSERT( FALSE );
	}
	int index = gflGetFormatIndexByName( szExt );

	for ( POSITION pos = m_Filenames.GetHeadPosition (); pos ; ++counter )
	{
		CString filename( m_Filenames.GetNext( pos ) );

		if ( pProgress )
		{
			pProgress->SetLine( 2, filename, TRUE, NULL );
			pProgress->SetProgress( counter, total );
			Sleep( 10 );
			if ( pProgress->HasUserCancelled() )
			{
				pProgress->StopProgressDialog();
				return;
			}
		}

		GFL_BITMAP* hBitmap = NULL;
		if ( SUCCEEDED( _Module.LoadGFLBitmap( filename, &hBitmap ) ) )
		{
			GFL_SAVE_PARAMS params = {};
			gflGetDefaultSaveParams( &params );
			params.Flags = GFL_SAVE_REPLACE_EXTENSION | GFL_SAVE_ANYWAY;
			params.FormatIndex = index;
			if ( ext == ID_CONVERT_JPG_ITEM )
			{
				params.Quality = (GFL_INT16)GetRegValue( _T("JPEG"), JPEG_DEFAULT );
				params.Progressive = GFL_TRUE;
				params.OptimizeHuffmanTable = GFL_TRUE;
			}
			else if ( ext == ID_CONVERT_GIF_ITEM )
			{
				params.Interlaced = GFL_TRUE;
			}
			else if ( ext == ID_CONVERT_PNG_ITEM )
			{
				params.CompressionLevel = (GFL_INT16)GetRegValue( _T("PNG"), PNG_DEFAULT );
			}

			if ( gflSaveBitmapT( (LPTSTR)(LPCTSTR)filename, hBitmap, &params ) != GFL_NO_ERROR )
			{
				_Module.MsgBox( hWnd, IDS_ERR_SAVE );
				break;
			}
			DeleteObject( hBitmap );
		}
		else
		{
			_Module.MsgBox( hWnd, IDS_ERR_OPEN );
			break;
		}
	}

	if ( pProgress )
	{
		pProgress->SetLine( 2, _T(""), TRUE, NULL );
		pProgress->SetProgress( total, total );
		Sleep( 1000 );
		pProgress->StopProgressDialog();
	}
}

void CThumb::SetWallpaper(HWND hWnd, WORD reason)
{
	CString filename( m_Filenames.GetHead() );

	GFL_BITMAP* hBitmap = NULL;
	if ( SUCCEEDED( _Module.LoadGFLBitmap( filename, &hBitmap ) ) )
	{
		GFL_SAVE_PARAMS params = {};
		gflGetDefaultSaveParams( &params );
		params.Flags = GFL_SAVE_ANYWAY;
		params.Compression = GFL_NO_COMPRESSION;
		params.FormatIndex = gflGetFormatIndexByName( "bmp" );
		CString save_path = GetSpecialFolderPath( CSIDL_APPDATA ).TrimRight( _T("\\") ) +
			_T("\\SageThumbs wallpaper.bmp");
		if ( gflSaveBitmapT( (LPTSTR)(LPCTSTR)save_path, hBitmap, &params) == GFL_NO_ERROR)
		{
			SetRegValue( _T("TileWallpaper"),
				((reason == ID_WALLPAPER_TILE_ITEM) ? _T("1") : _T("0")),
				_T("Control Panel\\Desktop"), HKEY_CURRENT_USER );
			SetRegValue( _T("WallpaperStyle"),
				((reason == ID_WALLPAPER_STRETCH_ITEM) ? _T("2") : _T("0")),
				_T("Control Panel\\Desktop"), HKEY_CURRENT_USER );
			SystemParametersInfo (SPI_SETDESKWALLPAPER, 0,
				(LPVOID) (LPCTSTR) save_path, SPIF_SENDCHANGE | SPIF_UPDATEINIFILE);
		}
		else
			_Module.MsgBox( hWnd, IDS_ERR_SAVE );
		DeleteObject( hBitmap );
	}
	else
		_Module.MsgBox( hWnd, IDS_ERR_OPEN );
}

void CThumb::SendByMail(HWND hWnd, WORD reason)
{
	CComPtr< IProgressDialog > pProgress;
	HRESULT hr = pProgress.CoCreateInstance( CLSID_ProgressDialog );
	if ( SUCCEEDED( hr ) )
	{
		pProgress->SetTitle( _Module.GetAppName() );
		pProgress->SetLine( 1, _Module.m_oLangs.LoadString( IDS_SENDING ), FALSE, NULL );
		pProgress->StartProgressDialog( hWnd, NULL, PROGDLG_NORMAL | PROGDLG_AUTOTIME, NULL );
	}
	DWORD total = (DWORD)m_Filenames.GetCount(), counter = 0;

	// �������� �������� �� �������
	DWORD width = GetRegValue( _T("Width"), THUMB_STORE_SIZE );
	DWORD height = GetRegValue( _T("Height"), THUMB_STORE_SIZE );

	// ������������� MAPI
	if ( HMODULE hLibrary = LoadLibrary( _T("MAPI32.DLL") ) )
	{
		tMAPISendMail pMAPISendMail = (tMAPISendMail)GetProcAddress( hLibrary, "MAPISendMail" );
		if ( pMAPISendMail )
		{
			// ���������� ����������� � �������
			CAtlArray< CStringA > save_names;
			CAtlArray< CStringA > save_filenames;
			for ( POSITION pos = m_Filenames.GetHeadPosition () ; pos ; ++counter )
			{
				CString filename( m_Filenames.GetNext( pos ) );

				if ( pProgress )
				{
					pProgress->SetLine( 2, filename, TRUE, NULL );
					pProgress->SetProgress( counter, total );
					Sleep( 10 );
					if ( pProgress->HasUserCancelled() )
					{
						pProgress->StopProgressDialog();
						FreeLibrary (hLibrary);
						return;
					}
				}

				if ( reason == ID_MAIL_IMAGE_ITEM )
				{
					save_names.Add( CT2CA( PathFindFileName( filename ) ) );
					save_filenames.Add( CT2CA( filename ) );
				}
				else
				{
					GFL_BITMAP* hGflBitmap = NULL;
					hr = _Module.LoadThumbnail( filename, width, height, &hGflBitmap );
					if ( SUCCEEDED( hr ) )
					{
						GFL_SAVE_PARAMS params = {};
						gflGetDefaultSaveParams (&params);
						params.Flags = GFL_SAVE_ANYWAY;
						params.FormatIndex = gflGetFormatIndexByName( "jpeg" );
						TCHAR tmp [ MAX_PATH ] = {};
						GetTempPath( MAX_PATH, tmp );
						GetTempFileName( tmp, _T("tmb"), 0, tmp );
						if ( gflSaveBitmapT( tmp, hGflBitmap, &params ) == GFL_NO_ERROR )
						{
							save_names.Add( CT2CA( PathFindFileName( filename ) ) );
							save_filenames.Add( CT2CA( tmp ) );
						}
					}
				}
			}
			if ( size_t count = save_names.GetCount() )
			{
				// ������� ������
				MapiFileDesc* mfd = new MapiFileDesc[ count ];
				MapiMessage mm = {};
				mm.nFileCount = (ULONG)count;
				mm.lpFiles = mfd;
				if ( mfd )
				{
					ZeroMemory( mfd, sizeof( MapiFileDesc ) * count );

					for ( size_t i = 0; i < count; ++i )
					{
						mfd [i].nPosition = (ULONG)-1;
						mfd [i].lpszPathName = const_cast< LPSTR >(
							(LPCSTR)save_filenames[ i ] );
						mfd [i].lpszFileName = const_cast< LPSTR >(
							(LPCSTR)save_names[ i ] );
						mfd [i].lpFileType = NULL;
					}
					ULONG err = pMAPISendMail (0, (ULONG_PTR)hWnd, &mm,
						MAPI_DIALOG | MAPI_LOGON_UI | MAPI_NEW_SESSION, 0);
					if (MAPI_E_USER_ABORT != err && SUCCESS_SUCCESS != err)
						_Module.MsgBox( hWnd, IDS_ERR_MAIL );
					delete [] mfd;
				}
				else
					_Module.MsgBox( hWnd, IDS_ERR_MAIL );

				// �������� ��������� �����������
				if ( reason != ID_MAIL_IMAGE_ITEM )
				{
					for ( size_t i = 0; i < count; ++i )
					{
						ATLVERIFY( DeleteFileA( save_filenames[ i ] ) );
					}
				}
			}
			else
				_Module.MsgBox( hWnd, IDS_ERR_NOTHING );
		}
		else
			_Module.MsgBox( hWnd, IDS_ERR_MAIL );

		FreeLibrary (hLibrary);
	}
	else
		_Module.MsgBox( hWnd, IDS_ERR_MAIL );

	if ( pProgress )
	{
		pProgress->SetLine( 2, _T(""), TRUE, NULL );
		pProgress->SetProgress( total, total );
		Sleep( 1000 );
		pProgress->StopProgressDialog();
	}
}

void CThumb::CopyToClipboard(HWND hwnd)
{
	GFL_BITMAP* pBitmap = NULL;
	HBITMAP hBitmap = NULL;
	if ( SUCCEEDED( _Module.LoadGFLBitmap( m_Filenames.GetHead(), &pBitmap ) ) &&
		 SUCCEEDED( _Module.ConvertBitmap( pBitmap, &hBitmap ) ) )
	{
		if ( OpenClipboard ( hwnd ) )
		{
			EmptyClipboard();
			
			if ( SetClipboardData( CF_BITMAP, hBitmap ) )
			{
				// OK
			}
			else
				_Module.MsgBox( hwnd, IDS_ERR_CLIPBOARD );

			CloseClipboard();
		}
		else
			_Module.MsgBox( hwnd, IDS_ERR_CLIPBOARD );

		DeleteObject( hBitmap );
	}
	else
		_Module.MsgBox(hwnd, IDS_ERR_OPEN );
}

STDMETHODIMP CThumb::InvokeCommand(LPCMINVOKECOMMANDINFO pInfo)
{
	ATLTRACE ( "0x%08x::IContextMenu::InvokeCommand\n", this);

	// If lpVerb really points to a string, ignore this function call and bail out.
	if (0 != HIWORD (pInfo->lpVerb))
		return E_INVALIDARG;

	// The command ID must be 0 since we only have one menu item.
	switch (LOWORD (pInfo->lpVerb))
	{
	case ID_THUMBNAIL_ITEM:
		// Open the bitmap in the default paint program.
		SHAddToRecentDocs( SHARD_PATH, m_Filenames.GetHead() );
		ShellExecute( pInfo->hwnd, _T("open"), m_Filenames.GetHead(), NULL, NULL, SW_SHOWNORMAL );
		break;

	case ID_THUMBNAIL_INFO_ITEM:
		// Disabled
		break;

	case ID_OPTIONS_ITEM:
		// Options
		Options (pInfo->hwnd);
		break;

	case ID_CONVERT_JPG_ITEM:
		ConvertTo( pInfo->hwnd, ID_CONVERT_JPG_ITEM );
		break;
	case ID_CONVERT_GIF_ITEM:
		ConvertTo( pInfo->hwnd, ID_CONVERT_GIF_ITEM );
		break;
	case ID_CONVERT_BMP_ITEM:
		ConvertTo( pInfo->hwnd, ID_CONVERT_BMP_ITEM );
		break;
	case ID_CONVERT_PNG_ITEM:
		ConvertTo( pInfo->hwnd, ID_CONVERT_PNG_ITEM );
		break;

	case ID_WALLPAPER_STRETCH_ITEM:
	case ID_WALLPAPER_TILE_ITEM:
	case ID_WALLPAPER_CENTER_ITEM:
		SetWallpaper( pInfo->hwnd, LOWORD( pInfo->lpVerb ) );
		break;

	case ID_MAIL_IMAGE_ITEM:
	case ID_MAIL_THUMBNAIL_ITEM:
		SendByMail( pInfo->hwnd, LOWORD( pInfo->lpVerb ) );
		break;

	case ID_CLIPBOARD_ITEM:
		CopyToClipboard( pInfo->hwnd );
		break;

	default:
		return E_INVALIDARG;
	}

	return S_OK;
}

// IContextMenu2

STDMETHODIMP CThumb::HandleMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	ATLTRACE( "0x%08x::IContextMenu2::HandleMenuMsg\n", this);
	LRESULT res = 0;
	return MenuMessageHandler (uMsg, wParam, lParam, &res);
}

// IContextMenu3

STDMETHODIMP CThumb::HandleMenuMsg2(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
	ATLTRACE( "0x%08x::IContextMenu3::HandleMenuMsg2\n", this);
	LRESULT res = 0;
	return MenuMessageHandler (uMsg, wParam, lParam, (pResult ? pResult : &res));
}

STDMETHODIMP CThumb::MenuMessageHandler(UINT uMsg, WPARAM /*wParam*/, LPARAM lParam, LRESULT* pResult)
{
	switch (uMsg)
	{
	case WM_INITMENUPOPUP:
		ATLTRACE ( "0x%08x::WM_INITMENUPOPUP\n", this);
		break;

	case WM_MEASUREITEM:
		ATLTRACE ( "0x%08x::WM_MEASUREITEM\n", this);
		return OnMeasureItem( (MEASUREITEMSTRUCT*)lParam, pResult );

	case WM_DRAWITEM:
		ATLTRACE ( "0x%08x::WM_DRAWITEM\n", this);
		return OnDrawItem( (DRAWITEMSTRUCT*)lParam, pResult );

	case WM_MENUCHAR:
		ATLTRACE ( "0x%08x::WM_MENUCHAR\n", this);
		break;

	default:
		ATLTRACE ( "0x%08x::Unknown message %u\n", this, uMsg );
	}
	return S_OK;
}

STDMETHODIMP CThumb::OnMeasureItem(MEASUREITEMSTRUCT* pmis, LRESULT* pResult)
{
	// Check that we're getting called for our own menu item.
	if ( m_uOurItemID != pmis->itemID )
		return S_OK;

	if ( ! m_Preview )
		return S_OK;

	pmis->itemWidth = m_Preview.Width();
	pmis->itemHeight = m_Preview.Height();

	*pResult = TRUE;

	return S_OK;
}

STDMETHODIMP CThumb::OnDrawItem(DRAWITEMSTRUCT* pdis, LRESULT* pResult)
{
	// Check that we're getting called for our own menu item.
	if ( m_uOurItemID != pdis->itemID )
		return S_OK;

	if ( ! m_Preview )
		return S_OK;

	const int width = pdis->rcItem.right - pdis->rcItem.left;
	const int height = pdis->rcItem.bottom - pdis->rcItem.top;
	const int x = pdis->rcItem.left + ( width - m_Preview.Width() ) / 2;
	const int y = pdis->rcItem.top + ( height - m_Preview.Height() ) / 2;
	const int cx = min( width, (int)m_Preview.Width() );
	const int cy = min( height, (int)m_Preview.Height() );

	if ( ( pdis->itemState & ODS_SELECTED ) )
		FillRect( pdis->hDC, &pdis->rcItem, GetSysColorBrush( COLOR_HIGHLIGHT ) );
	else
		FillRect( pdis->hDC, &pdis->rcItem, GetSysColorBrush( COLOR_MENU ) );

	// ��������� �����������
	HBITMAP hBitmap = m_Preview.GetImage( width, height );
	HDC hBitmapDC = CreateCompatibleDC( pdis->hDC );
	HBITMAP hOldBitmap = (HBITMAP)SelectObject( hBitmapDC, hBitmap );
	HBRUSH hPattern = CreateHatchBrush( HS_BDIAGONAL, GetSysColor( COLOR_HIGHLIGHT ) );
	HBRUSH hOldBrush = (HBRUSH)SelectObject( pdis->hDC, hPattern );
	StretchBlt( pdis->hDC, x, y, cx, cy, hBitmapDC, 0, 0, m_Preview.Width(), m_Preview.Height(), ( pdis->itemState & ODS_SELECTED ) ?  MERGECOPY : SRCCOPY );
	SelectObject( hBitmapDC, hOldBitmap );
	DeleteDC( hBitmapDC );
	SelectObject( pdis->hDC, hOldBrush );
	DeleteObject( hPattern );

	*pResult = TRUE;

	return S_OK;
}

// IPersistFile

STDMETHODIMP CThumb::Load(LPCOLESTR wszFile, DWORD /*dwMode*/)
{
	if ( ! wszFile )
	{
		ATLTRACE( "0x%08x::IPersistFile::Load() : E_POINTER\n" );
		return E_POINTER;
	}

	if ( ! _Module.IsGoodFile( (LPCTSTR)CW2CT( wszFile ) ) )
	{
		ATLTRACE( "0x%08x::IPersistFile::Load(\"%s\") : E_FAIL (Bad File)\n", this, (LPCSTR)CW2A( wszFile ) );
		return E_FAIL;
	}

	m_sFilename = wszFile;

	ATLTRACE( "0x%08x::IPersistFile::Load(\"%s\") : S_OK\n", this, (LPCSTR)CW2A( wszFile ) );
	return S_OK;
}

STDMETHODIMP CThumb::GetClassID(LPCLSID pclsid)
{
	ATLTRACE( "0x%08x::IPersist::GetClassID : ", this );

	if ( ! pclsid )
	{
		ATLTRACE ("E_POINTER\n");
		return E_POINTER;
	}

	*pclsid = CLSID_Thumb;

	ATLTRACE ("S_OK\n");
	return S_OK;
}

STDMETHODIMP CThumb::IsDirty()
{
	ATLTRACENOTIMPL( _T("IPersistFile::IsDirty") );
}

STDMETHODIMP CThumb::Save(LPCOLESTR, BOOL)
{
	ATLTRACENOTIMPL( _T("IPersistFile::Save") );
}

STDMETHODIMP CThumb::SaveCompleted(LPCOLESTR)
{
	ATLTRACENOTIMPL( _T("IPersistFile::SaveCompleted") );
}

STDMETHODIMP CThumb::GetCurFile(LPOLESTR*)
{
	ATLTRACENOTIMPL( _T("IPersistFile::GetCurFile") );
}

// IInitializeWithStream

#ifdef ISTREAM_ENABLED
STDMETHODIMP CThumb::Initialize(
	/* [in] */ IStream * pstream,
	/* [in] */ DWORD /*grfMode*/)
{
	if ( ! pstream )
	{
		ATLTRACE( "CThumb - IInitializeWithStream::Initialize() : E_POINTER\n" );
		return E_POINTER;
	}

	TCHAR szPath[ MAX_PATH ];
	GetModuleFileName( NULL, szPath, MAX_PATH );
	ATLTRACE( "CThumb - IInitializeWithStream::Initialize() : We are inside \"%s\"\n", (LPCSTR)CT2A( szPath ) );

	LPCTSTR szFilename = PathFindFileName( szPath );
	//if ( _tcsicmp( szFilename, _T("dllhost.exe") ) == 0 )
	{
		m_pStream = pstream;
		ATLTRACE( "CThumb - IInitializeWithStream::Initialize() : S_OK\n" );
		return S_OK;
	}

	ATLTRACE( "CThumb - IInitializeWithStream::Initialize() : E_NOTIMPL\n" );
	return E_NOTIMPL;
}
#endif // ISTREAM_ENABLED

// IInitializeWithItem

STDMETHODIMP CThumb::Initialize(
  /* [in] */ __RPC__in_opt IShellItem* psi,
  /* [in] */ DWORD /* grfMode */)
{
	if ( ! psi  )
	{
		ATLTRACE( "CThumb - IInitializeWithItem::Initialize() : E_POINTER\n" );
		return E_POINTER;
	}

	LPWSTR wszFile = NULL;
	HRESULT hr = psi->GetDisplayName( SIGDN_FILESYSPATH, &wszFile );
	if ( FAILED( hr ) )
	{
		ATLTRACE( "CThumb - IInitializeWithItem::Initialize() : E_FAIL (Unknown path)\n" );
		return E_FAIL;
	}

	if ( ! _Module.IsGoodFile( (LPCTSTR)CW2T( wszFile ) ) )
	{
		ATLTRACE( "0x%08x::IInitializeWithItem::Initialize(\"%s\") : E_FAIL (Bad File)\n", this, (LPCSTR)CW2A( wszFile ) );
		return E_FAIL;
	}

	m_sFilename = wszFile;

	ATLTRACE( "CThumb - IInitializeWithItem::Initialize(\"%s\") : S_OK\n", (LPCSTR)CW2A( wszFile ) );
	CoTaskMemFree( wszFile );
	return S_OK;
}

// IInitializeWithFile

STDMETHODIMP CThumb::Initialize(
	/* [in] */ LPCWSTR wszFile,
	/* [in] */ DWORD /* grfMode */)
{
	if ( ! wszFile  )
	{
		ATLTRACE( "0x%08x::IInitializeWithFile::Initialize() : E_POINTER\n", this );
		return E_POINTER;
	}

	if ( ! _Module.IsGoodFile( (LPCTSTR)CW2T( wszFile ) ) )
	{
		ATLTRACE( "0x%08x::IInitializeWithFile::Initialize(\"%s\") : E_FAIL (Bad File)\n", this, (LPCSTR)CW2A( wszFile ) );
		return E_FAIL;
	}

	m_sFilename = wszFile;

	ATLTRACE( "0x%08x::IInitializeWithFile::Initialize(\"%s\") : S_OK\n", this, (LPCSTR)CW2A( wszFile ) );
	return S_OK;
}

// IThumbnailProvider

STDMETHODIMP CThumb::GetThumbnail(UINT cx, HBITMAP *phbmp, WTS_ALPHATYPE *pdwAlpha)
{
	if ( ! phbmp )
	{
		ATLTRACE( "CThumb - IThumbnailProvider::GetThumbnail(%d) : E_POINTER\n", cx );
		return E_POINTER;
	}
	*phbmp = NULL;

	if ( pdwAlpha )
		*pdwAlpha = WTSAT_UNKNOWN;

	if ( ! GetRegValue( _T("EnableThumbs"), 1ul ) )
	{
		ATLTRACE( "CThumb - IThumbnailProvider::GetThumbnail(%d) : E_FAIL (Disabled)\n", cx );
		return E_FAIL;
	}

	if ( ! m_Preview )
	{
		if ( ! m_sFilename.IsEmpty() )
		{
			m_Preview.LoadImage( m_sFilename, cx, cx );
		}
#ifdef ISTREAM_ENABLED
		else if ( m_pStream )
		{
			m_Preview.LoadImage( m_pStream, cx, cx );
		}
#endif // ISTREAM_ENABLED
	}

	*phbmp = m_Preview.GetImage( cx, cx );
	if ( ! *phbmp )
	{
		ATLTRACE( "CThumb - IThumbnailProvider::GetThumbnail(%d) : E_FAIL (Load failed)\n", cx );
		return E_FAIL;
	}

#ifdef _DEBUG
	BITMAP bm = {};
	GetObject( *phbmp, sizeof( BITMAP ), &bm );
	ATLTRACE( "CThumb - IThumbnailProvider::GetThumbnail(%d) : S_OK (%d planes, %d bits)\n", cx, bm.bmPlanes, bm.bmBitsPixel );
#endif
	return S_OK;
}

// IPropertyStoreCapabilities

STDMETHODIMP CThumb::IsPropertyWritable(
	/* [in] */ __RPC__in REFPROPERTYKEY key)
{
	key;
#ifdef _DEBUG
	CStringA sPropName;
	CComPtr< IPropertyDescription > pDesc;
	if ( SUCCEEDED( PSGetPropertyDescription( key, IID_PPV_ARGS( &pDesc ) ) ) )
	{
		LPWSTR szPropName = NULL;
		if ( SUCCEEDED( pDesc->GetCanonicalName( &szPropName ) ) )
		{
			sPropName = szPropName;
			CoTaskMemFree( szPropName );
		}
		pDesc.Release();
	}
	ATLTRACE( "CThumb - IPropertyStoreCapabilities::IsPropertyWritable(\"%s\") : S_FALSE\n", (LPCSTR)sPropName );
#endif
	return S_FALSE;
}

// IPropertyStore

const PROPERTYKEY props[] =
{
	PKEY_ItemTypeText,
	PKEY_FileDescription,
	PKEY_DRM_IsProtected,
	PKEY_Image_Dimensions,
	PKEY_Image_HorizontalSize,
	PKEY_Image_VerticalSize,
	PKEY_Image_ResolutionUnit,
	PKEY_Image_HorizontalResolution,
	PKEY_Image_VerticalResolution,
	PKEY_Image_BitDepth,
	PKEY_Image_Compression,
	PKEY_Image_CompressionText,
	PKEY_PerceivedType
//	PKEY_Kind
};

STDMETHODIMP CThumb::GetCount(
	/* [out] */ __RPC__out DWORD* cProps)
{
	if ( ! cProps )
	{
		ATLTRACE( "CThumb - IPropertyStore::GetCount() : E_POINTER\n" );
		return E_POINTER;
	}

	*cProps = _countof( props );

	ATLTRACE( "CThumb - IPropertyStore::GetCount() : S_OK (%u)\n", *cProps );
	return S_OK;
}

STDMETHODIMP CThumb::GetAt(
	/* [in] */ DWORD iProp,
	/* [out] */ __RPC__out PROPERTYKEY* pkey)
{
	if ( ! pkey )
	{
		ATLTRACE( "CThumb - IPropertyStore::GetAt(%u) : E_POINTER\n", iProp );
		return E_POINTER;
	}

	if ( iProp >= _countof( props ) )
	{
		ATLTRACE( "CThumb - IPropertyStore::GetAt(%u) : E_INVALIDARG\n", iProp );
		return E_INVALIDARG;
	}

	*pkey = props[ iProp ];

	ATLTRACE( "CThumb - IPropertyStore::GetAt(%u) : S_OK\n", iProp );
	return S_OK;
}

STDMETHODIMP CThumb::GetValue(
	/* [in] */ __RPC__in REFPROPERTYKEY key,
	/* [out] */ __RPC__out PROPVARIANT* pv)
{
	if ( ! pv )
	{
		ATLTRACE( "CThumb - IPropertyStore::GetValue() : E_POINTER\n" );
		return E_POINTER;
	}
	PropVariantInit( pv );

#ifdef _DEBUG
	CStringA sPropName;
	CComPtr< IPropertyDescription > pDesc;
	if ( SUCCEEDED( PSGetPropertyDescription( key, IID_PPV_ARGS( &pDesc ) ) ) )
	{
		LPWSTR szPropName = NULL;
		if ( SUCCEEDED( pDesc->GetCanonicalName( &szPropName ) ) )
		{
			sPropName = szPropName;
			CoTaskMemFree( szPropName );
		}
		pDesc.Release();
	}
	ATLTRACE( "CThumb - IPropertyStore::GetValue(\"%s\") : S_OK\n", (LPCSTR)sPropName );
#endif

	if ( ! m_sFilename.IsEmpty() )
	{
		m_Preview.LoadInfo( m_sFilename );
	}
#ifdef ISTREAM_ENABLED
	else if ( m_pStream )
	{
		m_Preview.LoadInfo( m_pStream );
	}
#endif // ISTREAM_ENABLED

	if ( ! m_Preview.IsInfoAvailable() )
	{
		ATLTRACE( "CThumb - IPropertyStore::GetValue() : S_OK (No info)\n" );
		return S_OK;
	}

	if ( IsEqualPropertyKey( key, PKEY_ItemTypeText ) || IsEqualPropertyKey( key, PKEY_FileDescription ) )
	{
		pv->vt = VT_BSTR;
		pv->bstrVal = m_Preview.ImageDescription().AllocSysString();
	}
	else if ( IsEqualPropertyKey( key, PKEY_DRM_IsProtected ) )
	{
		pv->vt = VT_BOOL;
		pv->boolVal = VARIANT_FALSE;
	}
	else if ( IsEqualPropertyKey( key, PKEY_Image_Dimensions ) )
	{
		CString sDimensions;
		sDimensions.Format( _T("%d x %d"), m_Preview.ImageWidth(), m_Preview.ImageHeight() );
		pv->vt = VT_BSTR;
		pv->bstrVal = sDimensions.AllocSysString();
	}
	else if ( IsEqualPropertyKey( key, PKEY_Image_HorizontalSize ) )
	{
		pv->vt = VT_UI4;
		pv->ulVal = m_Preview.ImageWidth();
	}
	else if ( IsEqualPropertyKey( key, PKEY_Image_VerticalSize ) )
	{
		pv->vt = VT_UI4;
		pv->ulVal = m_Preview.ImageHeight();
	}
	else if ( IsEqualPropertyKey( key, PKEY_Image_ResolutionUnit ) )
	{
		pv->vt = VT_I2;
		pv->iVal = 2;	// inches
	}
	else if ( IsEqualPropertyKey( key, PKEY_Image_HorizontalResolution ) )
	{
		if ( m_Preview.ImageXdpi() )
		{
			pv->vt = VT_R8;
			pv->dblVal = (double)m_Preview.ImageXdpi();
		}
	}
	else if ( IsEqualPropertyKey( key, PKEY_Image_VerticalResolution ) )
	{
		if ( m_Preview.ImageYdpi() || m_Preview.ImageXdpi() )
		{
			pv->vt = VT_R8;
			pv->dblVal = (double) ( m_Preview.ImageYdpi() ? m_Preview.ImageYdpi() : m_Preview.ImageXdpi() );
		}
	}
	else if ( IsEqualPropertyKey( key, PKEY_Image_BitDepth ) )
	{
		pv->vt = VT_UI4;
		pv->ulVal = m_Preview.ImageBitDepth();
	}
	else if ( IsEqualPropertyKey( key, PKEY_Image_Compression ) )
	{
		pv->vt = VT_UI2;
		switch ( m_Preview.ImageCompression() )
		{
		case GFL_NO_COMPRESSION:
			pv->uiVal = IMAGE_COMPRESSION_UNCOMPRESSED;
			break;
		case GFL_LZW:
		case GFL_LZW_PREDICTOR:
			pv->uiVal = IMAGE_COMPRESSION_LZW;
			break;
		case GFL_JPEG:
			pv->uiVal = IMAGE_COMPRESSION_JPEG;
			break;
		case GFL_CCITT_FAX3:
		case GFL_CCITT_FAX3_2D:
			pv->uiVal = IMAGE_COMPRESSION_CCITT_T3;
			break;
		case GFL_CCITT_FAX4:
			pv->uiVal = IMAGE_COMPRESSION_CCITT_T4;
			break;
		case GFL_ZIP:
		case GFL_RLE:
		case GFL_SGI_RLE:
		case GFL_CCITT_RLE:
		case GFL_WAVELET:
		default:
			pv->uiVal = IMAGE_COMPRESSION_PACKBITS;
		}
	}
	else if ( IsEqualPropertyKey( key, PKEY_Image_CompressionText ) )
	{
		pv->vt = VT_BSTR;
		pv->bstrVal = m_Preview.ImageCompressionDescription().AllocSysString();
	}
	else if ( IsEqualPropertyKey( key, PKEY_PerceivedType ) )
	{
		pv->vt = VT_I4;
		pv->lVal = PERCEIVED_TYPE_IMAGE;
	}
	//else if ( IsEqualPropertyKey( key, PKEY_Kind ) )
	//{
	//	pv->vt = VT_ARRAY | VT_BSTR;
	//	pv-> = CString( KIND_PICTURE ).AllocSysString();
	//}

	return S_OK;
}

STDMETHODIMP CThumb::SetValue(
	/* [in] */ __RPC__in REFPROPERTYKEY key,
	/* [in] */ __RPC__in REFPROPVARIANT /* propvar */)
{
	key;

#ifdef _DEBUG
	CStringA sPropName;
	CComPtr< IPropertyDescription > pDesc;
	if ( SUCCEEDED( PSGetPropertyDescription( key, IID_PPV_ARGS( &pDesc ) ) ) )
	{
		LPWSTR szPropName = NULL;
		if ( SUCCEEDED( pDesc->GetCanonicalName( &szPropName ) ) )
		{
			sPropName = szPropName;
			CoTaskMemFree( szPropName );
		}
		pDesc.Release();
	}
	ATLTRACE( "CThumb - IPropertyStore::SetValue(\"%s\") : E_INVALIDARG\n", (LPCSTR)sPropName );
#endif

	return E_INVALIDARG;
}

STDMETHODIMP CThumb::Commit()
{
	ATLTRACE( "CThumb - IPropertyStore::Commit() : E_FAIL\n" );

	return E_FAIL;
}

// IPropertySetStorage

//STDMETHODIMP CThumb::Create(
//	/* [in] */ __RPC__in REFFMTID rfmtid,
//	/* [unique][in] */ __RPC__in_opt const CLSID* /* pclsid */,
//	/* [in] */ DWORD /* grfFlags */,
//	/* [in] */ DWORD /* grfMode */,
//	/* [out] */ __RPC__deref_out_opt IPropertyStorage** /* ppprstg */)
//{
//	rfmtid;
//
//#ifdef _DEBUG
//	LPOLESTR szGUID = NULL;
//	StringFromIID( rfmtid, &szGUID );
//	ATLTRACE( "CThumb - IPropertySetStorage::Create(\"%s\") : STG_E_ACCESSDENIED\n", (LPCSTR)CW2A( szGUID ) );
//	CoTaskMemFree( szGUID );
//#endif
//
//	return STG_E_ACCESSDENIED;
//}
//
//STDMETHODIMP CThumb::Open(
//	/* [in] */ __RPC__in REFFMTID rfmtid,
//	/* [in] */ DWORD /* grfMode */,
//	/* [out] */ __RPC__deref_out_opt IPropertyStorage** ppprstg)
//{
//	if ( ! ppprstg )
//	{
//		ATLTRACE( "CThumb - IPropertySetStorage::Open() : E_POINTER\n" );
//		return E_POINTER;
//	}
//	*ppprstg = NULL;
//
//	if ( IsEqualIID( rfmtid, FMTID_ImageProperties ) )
//	{
//		ATLTRACE( "CThumb - IPropertySetStorage::Open(\"FMTID_ImageProperties\") : S_OK\n" );
//		return QueryInterface( IID_IPropertyStorage, (void**)ppprstg );
//	}
//
//#ifdef _DEBUG
//	LPOLESTR szGUID = NULL;
//	StringFromIID( rfmtid, &szGUID );
//	ATLTRACE( "CThumb - IPropertySetStorage::Open(\"%s\") : STG_E_INVALIDPARAMETER\n", (LPCSTR)CW2A( szGUID ) );
//	CoTaskMemFree( szGUID );
//#endif
//
//	return STG_E_INVALIDPARAMETER;
//}
//
//STDMETHODIMP CThumb::Delete(
//	/* [in] */ __RPC__in REFFMTID rfmtid)
//{
//	rfmtid;
//
//#ifdef _DEBUG
//	LPOLESTR szGUID = NULL;
//	StringFromIID( rfmtid, &szGUID );
//	ATLTRACE( "CThumb - IPropertySetStorage::Delete(\"%s\") : STG_E_ACCESSDENIED\n", (LPCSTR)CW2A( szGUID ) );
//	CoTaskMemFree( szGUID );
//#endif
//
//	return STG_E_ACCESSDENIED;
//}
//
//STDMETHODIMP CThumb::Enum(
//	/* [out] */ __RPC__deref_out_opt IEnumSTATPROPSETSTG** /* ppenum */)
//{
//	ATLTRACENOTIMPL( _T("CThumb - IPropertySetStorage::Enum()") );
//}

// IPropertyStorage

//STDMETHODIMP CThumb::ReadMultiple(
//	/* [in] */ ULONG cpspec,
//	/* [size_is][in] */ __RPC__in_ecount_full(cpspec) const PROPSPEC rgpspec[ ],
//	/* [size_is][out] */ __RPC__out_ecount_full(cpspec) PROPVARIANT rgpropvar[])
//{
//	if ( ! rgpspec || ! rgpropvar )
//	{
//		ATLTRACE( "CThumb - IPropertySetStorage::ReadMultiple() : E_POINTER\n" );
//		return E_POINTER;
//	}
//
//	for ( ULONG i = 0; i < cpspec; ++i )
//	{
//		if ( rgpspec[ i ].ulKind == PRSPEC_PROPID )
//		{
//			ATLTRACE( "CThumb - IPropertySetStorage::ReadMultiple() : %d. ID=%d\n", i + 1, rgpspec[ i ].propid );
//		}
//		else
//		{
//			ATLTRACE( "CThumb - IPropertySetStorage::ReadMultiple() : %d. STR=\"%s\"\n", i + 1, (LPCSTR)CW2A( rgpspec[ i ].lpwstr ) );
//		}
//	}
//
//	return S_OK;
//}
//
//STDMETHODIMP CThumb::WriteMultiple(
//	/* [in] */ ULONG /* cpspec */,
//	/* [size_is][in] */ __RPC__in_ecount_full(cpspec) const PROPSPEC /* rgpspec */ [ ],
//	/* [size_is][in] */ __RPC__in_ecount_full(cpspec) const PROPVARIANT /* rgpropvar */ [ ],
//	/* [in] */ PROPID /* propidNameFirst */)
//{
//	ATLTRACENOTIMPL( _T("CThumb - IPropertyStorage::WriteMultiple()") );
//}
//
//STDMETHODIMP CThumb::DeleteMultiple(
//	/* [in] */ ULONG /* cpspec */,
//	/* [size_is][in] */ __RPC__in_ecount_full(cpspec) const PROPSPEC /* rgpspec */ [])
//{
//	ATLTRACENOTIMPL( _T("CThumb - IPropertyStorage::DeleteMultiple()") );
//}
//
//STDMETHODIMP CThumb::ReadPropertyNames(
//	/* [in] */ ULONG /* cpropid */,
//	/* [size_is][in] */ __RPC__in_ecount_full(cpropid) const PROPID /* rgpropid */ [],
//	/* [size_is][out] */ __RPC__out_ecount_full(cpropid) LPOLESTR /* rglpwstrName */ [])
//{
//	ATLTRACENOTIMPL( _T("CThumb - IPropertyStorage::ReadPropertyNames()") );
//}
//
//STDMETHODIMP CThumb::WritePropertyNames(
//	/* [in] */ ULONG /* cpropid */,
//	/* [size_is][in] */ __RPC__in_ecount_full(cpropid) const PROPID /* rgpropid */ [],
//	/* [size_is][in] */ __RPC__in_ecount_full(cpropid) const LPOLESTR /* rglpwstrName */ [])
//{
//	ATLTRACENOTIMPL( _T("CThumb - IPropertyStorage::WritePropertyNames()") );
//}
//
//STDMETHODIMP CThumb::DeletePropertyNames(
//	/* [in] */ ULONG /* cpropid */,
//	/* [size_is][in] */ __RPC__in_ecount_full(cpropid) const PROPID /* rgpropid */ [])
//{
//	ATLTRACENOTIMPL( _T("CThumb - IPropertyStorage::DeletePropertyNames()") );
//}
//
//STDMETHODIMP CThumb::Commit(
//	/* [in] */ DWORD /* grfCommitFlags */)
//{
//	ATLTRACENOTIMPL( _T("CThumb - IPropertyStorage::Commit()") );
//}
//
//STDMETHODIMP CThumb::Revert()
//{
//	ATLTRACENOTIMPL( _T("CThumb - IPropertyStorage::Revert()") );
//}
//
//STDMETHODIMP CThumb::Enum(
//	/* [out] */ __RPC__deref_out_opt IEnumSTATPROPSTG** /* ppenum */)
//{
//	ATLTRACENOTIMPL( _T("CThumb - IPropertyStorage::Enum()") );
//}
//
//STDMETHODIMP CThumb::SetTimes(
//	/* [in] */ __RPC__in const FILETIME* /* pctime */,
//	/* [in] */ __RPC__in const FILETIME* /* patime */,
//	/* [in] */ __RPC__in const FILETIME* /* pmtime */)
//{
//	ATLTRACENOTIMPL( _T("CThumb - IPropertyStorage::SetTimes()") );
//}
//
//STDMETHODIMP CThumb::SetClass(
//	/* [in] */ __RPC__in REFCLSID /* clsid */)
//{
//	ATLTRACENOTIMPL( _T("CThumb - IPropertyStorage::SetClass()") );
//}
//
//STDMETHODIMP CThumb::Stat(
//	/* [out] */ __RPC__out STATPROPSETSTG* /* pstatpsstg */)
//{
//	ATLTRACENOTIMPL( _T("CThumb - IPropertyStorage::Stat()") );
//}

// INamedPropertyStore

//STDMETHODIMP CThumb::GetNamedValue(
//	/* [string][in] */ __RPC__in_string LPCWSTR pszName,
//	/* [out] */ __RPC__out PROPVARIANT* ppropvar)
//{
//	if ( ! ppropvar )
//	{
//		ATLTRACE( "CThumb - IPropertyStore::GetValue() : E_POINTER\n" );
//		return E_POINTER;
//	}
//	PropVariantInit( ppropvar );
//
//	ATLTRACE( "CThumb - INamedPropertyStore::GetNamedValue(\"%s\") : S_OK\n", (LPCSTR)CW2A( pszName ) );
//	return S_OK;
//}
//
//STDMETHODIMP CThumb::SetNamedValue(
//	/* [string][in] */ __RPC__in_string LPCWSTR pszName,
//	 /* [in] */ __RPC__in REFPROPVARIANT /* propvar */)
//{
//	ATLTRACE( "CThumb - INamedPropertyStore::SetNamedValue(\"%s\") : STG_E_ACCESSDENIED\n", (LPCSTR)CW2A( pszName ) );
//	return STG_E_ACCESSDENIED;
//}
//
//STDMETHODIMP CThumb::GetNameCount(
//	/* [out] */ __RPC__out DWORD* pdwCount)
//{
//	if ( ! pdwCount )
//	{
//		ATLTRACE( "CThumb - INamedPropertyStore::GetNameCount() : E_POINTER\n" );
//		return E_POINTER;
//	}
//	*pdwCount = 0;
//
//	ATLTRACE( "CThumb - INamedPropertyStore::GetNameCount : S_OK (%u)\n", *pdwCount );
//	return S_OK;
//}
//
//STDMETHODIMP CThumb::GetNameAt(
//	/* [in] */ DWORD /* iProp */,
//	/* [out] */ __RPC__deref_out_opt BSTR* /* pbstrName */)
//{
//	ATLTRACE( "CThumb - INamedPropertyStore::GetNameAt() : STG_E_ACCESSDENIED\n" );
//	return E_NOTIMPL;
//}

// IPreviewHandler

//STDMETHODIMP CThumb::SetWindow(HWND hwnd, const RECT *prc)
//{
//	ATLTRACENOTIMPL( _T("IPreviewHandler::SetWindow") );
//}
//
//STDMETHODIMP CThumb::SetRect(const RECT *prc)
//{
//	ATLTRACENOTIMPL( _T("IPreviewHandler::SetRect") );
//}
//
//STDMETHODIMP CThumb::DoPreview(void)
//{
//	ATLTRACENOTIMPL( _T("IPreviewHandler::DoPreview") );
//}
//
//STDMETHODIMP CThumb::Unload(void)
//{
//	ATLTRACENOTIMPL( _T("IPreviewHandler::Unload") );
//}
//
//STDMETHODIMP CThumb::SetFocus(void)
//{
//	ATLTRACENOTIMPL( _T("IPreviewHandler::SetFocus") );
//}
//
//STDMETHODIMP CThumb::QueryFocus(HWND *phwnd)
//{
//	ATLTRACENOTIMPL( _T("IPreviewHandler::QueryFocus") );
//}
//
//STDMETHODIMP CThumb::TranslateAccelerator(MSG *pmsg)
//{
//	ATLTRACENOTIMPL( _T("IPreviewHandler::TranslateAccelerator") );
//}

// IOleWindow

//STDMETHODIMP CThumb::GetWindow(HWND *phwnd)
//{
//	ATLTRACENOTIMPL( _T("IOleWindow::GetWindow") );
//}
//
//STDMETHODIMP CThumb::ContextSensitiveHelp(BOOL /*fEnterMode*/)
//{
//	ATLTRACENOTIMPL( _T("IOleWindow::ContextSensitiveHelp") );
//}

// IExtractImage

// *pdwPriority:
// const DWORD ITSAT_MAX_PRIORITY		= 0x7fffffff;
// const DWORD ITSAT_MIN_PRIORITY		= 0x00000000;
// const DWORD ITSAT_DEFAULT_PRIORITY	= 0x10000000;
// const DWORD IEI_PRIORITY_MAX			= ITSAT_MAX_PRIORITY;
// const DWORD IEI_PRIORITY_MIN			= ITSAT_MIN_PRIORITY;
// const DWORD IEIT_PRIORITY_NORMAL		= ITSAT_DEFAULT_PRIORITY;
// *pdwFlags:
// const DWORD IEIFLAG_ASYNC      = 0x0001;	// ask the extractor if it supports ASYNC extract (free threaded)
// const DWORD IEIFLAG_CACHE      = 0x0002;	// returned from the extractor if it does NOT cache the thumbnail
// const DWORD IEIFLAG_ASPECT     = 0x0004;	// passed to the extractor to beg it to render to the aspect ratio of the supplied rect
// const DWORD IEIFLAG_OFFLINE    = 0x0008;	// if the extractor shouldn't hit the net to get any content neede for the rendering
// const DWORD IEIFLAG_GLEAM      = 0x0010;	// does the image have a gleam ? this will be returned if it does
// const DWORD IEIFLAG_SCREEN     = 0x0020;	// render as if for the screen  (this is exlusive with IEIFLAG_ASPECT )
// const DWORD IEIFLAG_ORIGSIZE   = 0x0040;	// render to the approx size passed, but crop if neccessary
// const DWORD IEIFLAG_NOSTAMP    = 0x0080;	// returned from the extractor if it does NOT want an icon stamp on the thumbnail
// const DWORD IEIFLAG_NOBORDER   = 0x0100;	// returned from the extractor if it does NOT want an a border around the thumbnail
// const DWORD IEIFLAG_QUALITY    = 0x0200;	// passed to the Extract method to indicate that a slower, higher quality image is desired, re-compute the thumbnail
// const DWORD IEIFLAG_REFRESH    = 0x0400;	// returned from the extractor if it would like to have Refresh Thumbnail available

STDMETHODIMP CThumb::GetLocation (
    /* [size_is][out] */ LPWSTR pszPathBuffer,
    /* [in] */ DWORD cch,
    /* [unique][out][in] */ DWORD* /* pdwPriority */,
    /* [in] */ const SIZE* prgSize,
    /* [in] */ DWORD /* dwRecClrDepth */,
    /* [in] */ DWORD* pdwFlags)
{
	ATLTRACE( "CThumb - IExtractImage::GetLocation(%dx%d) : ", (prgSize ? prgSize->cx : 0), (prgSize ? prgSize->cy : 0) );

	if ( ! GetRegValue( _T("EnableThumbs"), 1ul ) )
	{
		ATLTRACE( "E_FAIL (Disabled)\n" );
		return E_FAIL;
	}

	if ( pszPathBuffer )
	{
		*pszPathBuffer = 0;

		if ( ! m_sFilename.IsEmpty() )
		{
			CT2CW szFilenameW( m_sFilename );
			DWORD len = min( (DWORD)( m_sFilename.GetLength() + 1 ), cch );
			wcsncpy_s( pszPathBuffer, cch, (LPCWSTR)szFilenameW, len );
		}
#ifdef ISTREAM_ENABLED
		else if ( m_pStream )
		{
			STATSTG stat = {};
			if ( SUCCEEDED( m_pStream->Stat( &stat,  STATFLAG_DEFAULT ) ) && stat.pwcsName )
			{
				wcscpy_s( pszPathBuffer, cch, stat.pwcsName );
				if ( stat.pwcsName ) CoTaskMemFree( stat.pwcsName );
			}
		}
#endif // ISTREAM_ENABLED
	}

	// ��������� �������� �� �������
	if ( prgSize )
	{
		m_cx = prgSize->cx;
		m_cy = prgSize->cy;
	}

	if ( pdwFlags )
	{
		if ( GetRegValue( _T("WinCache"), 1ul ) != 0 )
			*pdwFlags = IEIFLAG_CACHE;
		else
			*pdwFlags = 0;
	}

	if ( ! m_sFilename.IsEmpty() )
	{
		m_Preview.LoadImage( m_sFilename, m_cx, m_cy );
	}
#ifdef ISTREAM_ENABLED
	else if ( m_pStream )
	{
		m_Preview.LoadImage( m_pStream, m_cx, m_cy );
	}
#endif // ISTREAM_ENABLED

	return m_Preview ? S_OK : E_FAIL;
}

STDMETHODIMP CThumb::Extract (
	/* [out] */ HBITMAP *phBmpThumbnail)
{
	if ( ! phBmpThumbnail )
	{
		ATLTRACE( "CThumb - IExtractImage::Extract() : E_POINTER\n" );
		return E_POINTER;
	}
	*phBmpThumbnail = NULL;

	if ( ! GetRegValue( _T("EnableThumbs"), 1ul ) )
	{
		ATLTRACE( "CThumb - IExtractImage::Extract() : E_FAIL (Disabled)\n" );
		return E_FAIL;
	}

	*phBmpThumbnail = m_Preview.GetImage( m_cx, m_cy );
	if ( ! *phBmpThumbnail )
	{
		ATLTRACE( "CThumb - IExtractImage::Extract() : E_FAIL (Load failed)\n" );
		return E_FAIL;
	}

	ATLTRACE( "CThumb - IExtractImage::Extract() : S_OK\n" );
	return S_OK;
}

// IExtractImage2

STDMETHODIMP CThumb::GetDateStamp (
	/* [out] */ FILETIME *pDateStamp)
{
	if ( ! GetRegValue( _T("EnableThumbs"), 1ul ) )
	{
		ATLTRACE( "CThumb - IExtractImage2:GetDateStamp() : E_FAIL (Disabled)\n" );
		return E_FAIL;
	}

	if ( ! pDateStamp )
	{
		ATLTRACE( "CThumb - IExtractImage2:GetDateStamp() : E_POINTER\n" );
		return E_POINTER;
	}

	m_Preview.GetLastWriteTime( pDateStamp );

	ATLTRACE( "CThumb - IExtractImage2:GetDateStamp() : S_OK\n" );
	return S_OK;
}

// IRunnableTask
//
//STDMETHODIMP CThumb::Run ()
//{
//	ATLTRACENOTIMPL ("IRunnableTask::Run");
//}
//
//STDMETHODIMP CThumb::Kill (BOOL /*fWait*/)
//{
//	ATLTRACENOTIMPL ("IRunnableTask::Kill");
//}
//
//STDMETHODIMP CThumb::Suspend ()
//{
//	ATLTRACENOTIMPL ("IRunnableTask::Suspend");
//}
//
//STDMETHODIMP CThumb::Resume ()
//{
//	ATLTRACENOTIMPL("IRunnableTask::Resume");
//}
//
//STDMETHODIMP_(ULONG) CThumb::IsRunning ()
//{
//	ATLTRACE ("IRunnableTask::IsRunning\n");
//	return IRTIR_TASK_FINISHED;
//}

// IQueryInfo

STDMETHODIMP CThumb::GetInfoFlags(DWORD* pdwFlags)
{
	if ( pdwFlags )
		*pdwFlags = 0;

	ATLTRACE( "0x%08x::IQueryInfo::GetInfoFlags() : S_OK\n", this );
	return S_OK;
}

STDMETHODIMP CThumb::GetInfoTip(DWORD, LPWSTR* ppwszTip)
{
	if ( ! ppwszTip )
	{
		ATLTRACE( "0x%08x::IQueryInfo::GetInfoTip() : E_POINTER\n", this );
		return E_POINTER;
	}
	*ppwszTip = NULL;

	CComPtr<IMalloc> pIMalloc;
	if ( FAILED( SHGetMalloc ( &pIMalloc ) ) )
	{
		ATLTRACE( "0x%08x::IQueryInfo::GetInfoTip() : E_OUTOFMEMORY\n", this );
		return E_OUTOFMEMORY;
	}

	if ( ! m_sFilename.IsEmpty() )
	{
		m_Preview.LoadInfo( m_sFilename );
	}
#ifdef ISTREAM_ENABLED
	else if ( m_pStream )
	{
		m_Preview.LoadInfo( m_pStream );
	}
#endif // ISTREAM_ENABLED

	if ( ! m_Preview.IsInfoAvailable() )
	{
		ATLTRACE( "0x%08x::IQueryInfo::GetInfoTip() : E_FAIL (Load failed)\n", this );
		return E_FAIL;
	}

	CT2W info( m_Preview.GetInfoTipString() );
	size_t len = wcslen( (LPCWSTR)info ) + 1;
	*ppwszTip = (LPWSTR) pIMalloc->Alloc( len * sizeof( WCHAR ) );
	if ( ! *ppwszTip )
	{
		ATLTRACE( "0x%08x::IQueryInfo::GetInfoTip() : E_OUTOFMEMORY\n", this );
		return E_OUTOFMEMORY;
	}
	wcscpy_s( *ppwszTip, len, (LPCWSTR)info );

	ATLTRACE( "0x%08x::IQueryInfo::GetInfoTip() : S_OK\n", this );
	return S_OK;
}

// IExtractIconA

STDMETHODIMP CThumb::GetIconLocation(UINT uFlags, LPSTR szIconFile, UINT cch, int* piIndex, UINT* pwFlags)
{
	WCHAR szIconFileW[ MAX_LONG_PATH ] = {};
	HRESULT hr = GetIconLocation( uFlags, szIconFileW, MAX_LONG_PATH, piIndex, pwFlags );
	strcpy_s( szIconFile, cch, (LPCSTR)CW2A( szIconFileW ) );
	return hr;
}

STDMETHODIMP CThumb::Extract(LPCSTR pszFile, UINT nIconIndex, HICON* phiconLarge, HICON* phiconSmall, UINT nIconSize)
{
	return Extract( (LPCWSTR)CA2W( pszFile ), nIconIndex, phiconLarge, phiconSmall,nIconSize );
}

// IExtractIconW

STDMETHODIMP CThumb::GetIconLocation(UINT /* uFlags */, LPWSTR szIconFile, UINT cch, int* piIndex, UINT* pwFlags)
{
	if ( ! pwFlags || ! piIndex )
	{
		ATLTRACE( "CThumb - IExtractIcon::GetIconLocation() : E_POINTER\n" );
		return E_POINTER;
	}

	// Make it unique
	LARGE_INTEGER count;
	QueryPerformanceCounter( &count );
	*piIndex = (int)count.LowPart;

	if ( szIconFile )
	{
		*szIconFile = 0;

		if ( ! m_sFilename.IsEmpty() )
		{
			CT2CW szFilenameW( m_sFilename );
			DWORD len = min( (DWORD)( m_sFilename.GetLength() + 1 ), cch );
			wcsncpy_s( szIconFile, cch, (LPCWSTR)szFilenameW, len );
		}
#ifdef ISTREAM_ENABLED
		else if ( m_pStream )
		{
			STATSTG stat = {};
			if ( SUCCEEDED( m_pStream->Stat( &stat,  STATFLAG_DEFAULT ) ) && stat.pwcsName )
			{
				wcscpy_s( szIconFile, cch, stat.pwcsName );
				if ( stat.pwcsName ) CoTaskMemFree( stat.pwcsName );
			}
		}
#endif // ISTREAM_ENABLED

		if ( *szIconFile )
		{
			*piIndex = (int)CRC32( (const char*)szIconFile, (int)( wcslen( szIconFile ) * sizeof( TCHAR ) ) );
		}
	}

	*pwFlags = GIL_NOTFILENAME | GIL_PERINSTANCE;

	ATLTRACE( "CThumb - IExtractIcon::GetIconLocation(\"%s\",0x%08x,0x%08x) : S_OK\n", (LPCSTR)CW2A( szIconFile ), (DWORD)*piIndex, *pwFlags );
	return S_OK;
}

STDMETHODIMP CThumb::Extract(LPCWSTR pszFile, UINT nIconIndex, HICON* phiconLarge, HICON* phiconSmall, UINT nIconSize)
{
	pszFile;
	nIconIndex;

	if ( phiconLarge ) *phiconLarge = NULL;
	if ( phiconSmall ) *phiconSmall = NULL;

	UINT cxLarge = ( phiconLarge ? LOWORD( nIconSize ) : 0 );
	UINT cxSmall = ( phiconSmall ? HIWORD( nIconSize ) : 0 );
	m_cx = m_cy = max( cxLarge, cxSmall );
	if ( ! m_cx )
	{
		ATLTRACE( "CThumb - IExtractIcon::Extract(\"%s\") : E_FAIL (No size)\n", (LPCSTR)CW2A( pszFile ) );
		return E_FAIL;
	}

	if ( ! m_Preview )
	{
		if ( ! m_sFilename.IsEmpty() )
		{
			m_Preview.LoadImage( m_sFilename, m_cx, m_cy );
		}
#ifdef ISTREAM_ENABLED
		else if ( m_pStream )
		{
			m_Preview.LoadImage( m_pStream, m_cx, m_cy );
		}
#endif // ISTREAM_ENABLED
	}

	if ( ! m_Preview && ! m_sFilename.IsEmpty() )
	{
		// Attempt to load default icon
		CString sExt = PathFindExtension( m_sFilename );
		if ( sExt.IsEmpty() )
		{
			// No extension
			ATLTRACE( "CThumb - IExtractIcon::Extract(\"%s\") : E_FAIL (No extension)\n", (LPCSTR)CW2A( pszFile ) );
			return E_FAIL;
		}

		CString sDefaultIcon;
		CString sDefaultKey = GetRegValue( _T(""), CString(), sExt, HKEY_CLASSES_ROOT );
		if ( sDefaultKey.IsEmpty() )
		{
			sDefaultIcon = GetRegValue( _T(""), CString(), sExt + _T("\\DefaultIcon"), HKEY_CLASSES_ROOT );
		}
		else
		{
			sDefaultIcon = GetRegValue( _T(""), CString(), sDefaultKey + _T("\\DefaultIcon"), HKEY_CLASSES_ROOT );
		}
		if ( sDefaultIcon.IsEmpty() )
		{
			// No icon
			ATLTRACE( "CThumb - IExtractIcon::Extract(\"%s\") : E_FAIL (No icon)\n", (LPCSTR)CW2A( pszFile ) );
			return E_FAIL;
		}

		if ( ! LoadIcon( sDefaultIcon,
			( cxSmall == 16 ) ? phiconSmall : ( ( cxLarge == 16 ) ? phiconLarge : NULL ),
			( cxSmall == 32 ) ? phiconSmall : ( ( cxLarge == 32 ) ? phiconLarge : NULL ),
			( cxSmall == 48 ) ? phiconSmall : ( ( cxLarge == 48 ) ? phiconLarge : NULL ) ) )
		{
			// Found no icon
			ATLTRACE( "CThumb - IExtractIcon::Extract(\"%s\") : E_FAIL (Found no icon)\n", (LPCSTR)CW2A( pszFile ) );
			return E_FAIL;
		}

		ATLTRACE( "CThumb - IExtractIcon::Extract(\"%s\") : S_OK (Default)\n", (LPCSTR)CW2A( pszFile ) );
		return S_OK;
	}

	if ( cxLarge )
	{
		*phiconLarge = m_Preview.GetIcon( cxLarge );
	}

	if ( cxSmall )
	{
		*phiconSmall = m_Preview.GetIcon( cxSmall );
	}

	ATLTRACE( "CThumb - IExtractIcon::Extract(\"%s\",0x%08x,%d,%d) : S_OK\n", (LPCSTR)CW2A( pszFile ), nIconIndex, cxLarge, cxSmall );
	return S_OK;
}

// IDataObject

//#define PRINT_FORMAT1(fmt) if(pformatetcIn->cfFormat==RegisterClipboardFormat(fmt)) \
//	{ ATLTRACE ("%s\n", #fmt); } else
//#define PRINT_FORMAT2(fmt) if(pformatetcIn->cfFormat==(fmt)) \
//	{ ATLTRACE ("%s\n", #fmt); } else
//#define PRINT_FORMAT_END \
//	{ ATLTRACE ("no CF_\n"); }
//#define PRINT_FORMAT_ALL \
//	{ \
//		TCHAR fm [128]; fm [0] = _T('\0');\
//		GetClipboardFormatName (pformatetcIn->cfFormat, fm, sizeof (fm)); \
//		ATLTRACE ("0x%08x \"%s\" ", pformatetcIn->cfFormat, fm);\
//	} \
//	PRINT_FORMAT1(CFSTR_SHELLIDLIST) \
//	PRINT_FORMAT1(CFSTR_SHELLIDLISTOFFSET) \
//	PRINT_FORMAT1(CFSTR_NETRESOURCES) \
//	PRINT_FORMAT1(CFSTR_FILEDESCRIPTORA) \
//	PRINT_FORMAT1(CFSTR_FILEDESCRIPTORW) \
//	PRINT_FORMAT1(CFSTR_FILECONTENTS) \
//	PRINT_FORMAT1(CFSTR_FILENAMEA) \
//	PRINT_FORMAT1(CFSTR_FILENAMEW) \
//	PRINT_FORMAT1(CFSTR_PRINTERGROUP) \
//	PRINT_FORMAT1(CFSTR_FILENAMEMAPA) \
//	PRINT_FORMAT1(CFSTR_FILENAMEMAPW) \
//	PRINT_FORMAT1(CFSTR_SHELLURL) \
//	PRINT_FORMAT1(CFSTR_INETURLA) \
//	PRINT_FORMAT1(CFSTR_INETURLW) \
//	PRINT_FORMAT1(CFSTR_PREFERREDDROPEFFECT) \
//	PRINT_FORMAT1(CFSTR_PERFORMEDDROPEFFECT) \
//	PRINT_FORMAT1(CFSTR_PASTESUCCEEDED) \
//	PRINT_FORMAT1(CFSTR_INDRAGLOOP) \
//	PRINT_FORMAT1(CFSTR_DRAGCONTEXT) \
//	PRINT_FORMAT1(CFSTR_MOUNTEDVOLUME) \
//	PRINT_FORMAT1(CFSTR_PERSISTEDDATAOBJECT) \
//	PRINT_FORMAT1(CFSTR_TARGETCLSID) \
//	PRINT_FORMAT1(CFSTR_LOGICALPERFORMEDDROPEFFECT) \
//	PRINT_FORMAT1(CFSTR_AUTOPLAY_SHELLIDLISTS) \
//	PRINT_FORMAT1(CF_RTF) \
//	PRINT_FORMAT1(CF_RTFNOOBJS) \
//	PRINT_FORMAT1(CF_RETEXTOBJ) \
//	PRINT_FORMAT2(CF_TEXT) \
//	PRINT_FORMAT2(CF_BITMAP) \
//	PRINT_FORMAT2(CF_METAFILEPICT) \
//	PRINT_FORMAT2(CF_SYLK) \
//	PRINT_FORMAT2(CF_DIF) \
//	PRINT_FORMAT2(CF_TIFF) \
//	PRINT_FORMAT2(CF_OEMTEXT) \
//	PRINT_FORMAT2(CF_DIB) \
//	PRINT_FORMAT2(CF_PALETTE) \
//	PRINT_FORMAT2(CF_PENDATA) \
//	PRINT_FORMAT2(CF_RIFF) \
//	PRINT_FORMAT2(CF_WAVE) \
//	PRINT_FORMAT2(CF_UNICODETEXT) \
//	PRINT_FORMAT2(CF_ENHMETAFILE) \
//	PRINT_FORMAT2(CF_HDROP) \
//	PRINT_FORMAT2(CF_LOCALE) \
//	PRINT_FORMAT2(CF_DIBV5) \
//	PRINT_FORMAT2(CF_MAX) \
//	PRINT_FORMAT2(CF_OWNERDISPLAY) \
//	PRINT_FORMAT2(CF_DSPTEXT) \
//	PRINT_FORMAT2(CF_DSPBITMAP) \
//	PRINT_FORMAT2(CF_DSPMETAFILEPICT) \
//	PRINT_FORMAT2(CF_DSPENHMETAFILE) \
//	PRINT_FORMAT2(CF_PRIVATEFIRST) \
//	PRINT_FORMAT2(CF_PRIVATELAST) \
//	PRINT_FORMAT2(CF_GDIOBJFIRST) \
//	PRINT_FORMAT2(CF_GDIOBJLAST) \
//	PRINT_FORMAT_END
//
//STDMETHODIMP CThumb::GetData (
//	/* [unique][in] */ FORMATETC* pformatetcIn,
//	/* [out] */ STGMEDIUM* /*pmedium*/)
//{
//	ATLTRACE ("IDataObject::GetData() : ");
//	PRINT_FORMAT_ALL
//	return E_INVALIDARG;
//	/*startActualLoad ();
//	stopActualLoad ();
//	if (m_Status == LS_LOADED) {
//	pmedium->tymed = TYMED_HGLOBAL;
//	pmedium->hGlobal = GlobalAlloc (GHND, sizeof (CLSID));
//	pmedium->pUnkForRelease = NULL;
//	char* dst = (char*) GlobalLock (pmedium->hGlobal);
//	CLSID clsid = { 0x4A34B3E3,0xF50E,0x4FF6,0x89,0x79,0x7E,0x41,0x76,0x46,0x6F,0xF2 };
//	CopyMemory (dst, &clsid, sizeof (CLSID));
//	GlobalUnlock (pmedium->hGlobal);
//	return S_OK;
//	}
//	return E_FAIL;*/
//}
//
//STDMETHODIMP CThumb::GetDataHere (
//	/* [unique][in] */ FORMATETC* /* pformatetc */,
//	/* [out][in] */ STGMEDIUM* /* pmedium */)
//{
//	ATLTRACENOTIMPL ("IDataObject::GetDataHere");
//}
//
//STDMETHODIMP CThumb::QueryGetData (
//	/* [unique][in] */ FORMATETC* pformatetcIn)
//{
//	ATLTRACE ("IDataObject::QueryGetData() : ");
//	PRINT_FORMAT_ALL
//	return E_INVALIDARG;
//}
//
//STDMETHODIMP CThumb::GetCanonicalFormatEtc (
//	/* [unique][in] */ FORMATETC* /* pformatectIn */,
//	/* [out] */ FORMATETC* /* pformatetcOut */)
//{
//	ATLTRACENOTIMPL ("IDataObject::GetCanonicalFormatEtc");
//}
//
//STDMETHODIMP CThumb::SetData (
//	/* [unique][in] */ FORMATETC* pformatetcIn,
//	/* [unique][in] */ STGMEDIUM* /*pmedium*/,
//	/* [in] */ BOOL fRelease)
//{
//	ATLTRACE ("IDataObject::SetData(fRelease=%d) : ", fRelease);
//	PRINT_FORMAT_ALL
//	/*FILEDESCRIPTOR* src = (FILEDESCRIPTOR*) GlobalLock (pmedium->hGlobal);
//	SIZE_T len = GlobalSize (pmedium->hGlobal);
//	GlobalUnlock (pmedium->hGlobal);
//	if (fRelease)
//		GlobalFree (pmedium->hGlobal);*/
//	return E_NOTIMPL;
//}
//
//static FORMATETC fmt [1] = {
//	{ CF_BITMAP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL }
//};
//
//class FORMATETCCopy
//{
//public:
//	static void init (FORMATETC* /*p*/)
//	{
//	}
//	static HRESULT copy (FORMATETC* pTo, const FORMATETC* pFrom)
//	{
//		CopyMemory (pTo, pFrom, sizeof (FORMATETC));
//		return S_OK;
//	}
//	static void destroy (FORMATETC* /*p*/)
//	{
//	}
//};
//
//STDMETHODIMP CThumb::EnumFormatEtc (
//	/* [in] */ DWORD dwDirection,
//	/* [out] */ IEnumFORMATETC** ppenumFormatEtc)
//{
//	ATLTRACE ("IDataObject::EnumFormatEtc(dwDirection=%d) : ", dwDirection);
//	if (!ppenumFormatEtc) {
//		ATLTRACE("E_POINTER\n");
//		return E_POINTER;
//	}
//	// �������� �������-�������������
//	typedef CComEnum < IEnumFORMATETC, &IID_IEnumFORMATETC,
//		FORMATETC, FORMATETCCopy > EnumFORMATETCType;
//	typedef CComObject < EnumFORMATETCType > EnumFORMATETC;
//	EnumFORMATETC* pEnum = NULL;
//	EnumFORMATETC::CreateInstance (&pEnum);
//	pEnum->Init ((FORMATETC*) (&fmt[0]), (FORMATETC*) (&fmt[1]), NULL);
//	ATLTRACE("S_OK\n");
//	return pEnum->QueryInterface (IID_IEnumFORMATETC, (void**) ppenumFormatEtc);
//}
//
//STDMETHODIMP CThumb::DAdvise (
//	/* [in] */ FORMATETC* /* pformatetc */,
//	/* [in] */ DWORD /* advf */,
//	/* [unique][in] */ IAdviseSink* /* pAdvSink */,
//	/* [out] */ DWORD* /* pdwConnection */)
//{
//	ATLTRACENOTIMPL ("IDataObject::DAdvise");
//}
//
//STDMETHODIMP CThumb::DUnadvise (
//	/* [in] */ DWORD /* dwConnection */)
//{
//	ATLTRACENOTIMPL ("IDataObject::DUnadvise");
//}
//
//STDMETHODIMP CThumb::EnumDAdvise (
//	/* [out] */ IEnumSTATDATA** /* ppenumAdvise */)
//{
//	ATLTRACENOTIMPL ("IDataObject::EnumDAdvise");
//}

// IImageDecodeFilter

//STDMETHODIMP CThumb::Initialize(IImageDecodeEventSink* pEventSink)
//{
//	if ( ! pEventSink )
//	{
//		ATLTRACE( "CThumb - IImageDecodeFilter::Initialize() : E_POINTER\n" );
//		return E_POINTER;
//	}
//
//	m_pEventSink = pEventSink;
//
//	DWORD dwEvents = 0;
//	ULONG nFormats = 0;
//    BFID *pFormats = NULL;
//	HRESULT hr = m_pEventSink->OnBeginDecode( &dwEvents, &nFormats, &pFormats );
//	if (FAILED (hr))
//	{
//		ATLTRACE( "CThumb - IImageDecodeFilter : OnBeginDecode error 0x%08x\n", hr);
//		m_pEventSink.Release();
//		return hr;
//	}
//	ATLTRACE( "CThumb - IImageDecodeFilter : OnBeginDecode returns: events=0x%08x, formats=%d\n", dwEvents, nFormats);
//	ULONG i = 0;
//	bool bOk = false;
//	for ( ; i < nFormats; ++i )
//	{
//		if ( IsEqualGUID( BFID_MONOCHROME, pFormats[ i ] ) )
//		{
//			ATLTRACE( "CThumb - IImageDecodeFilter : Found format BFID_MONOCHROME\n" );
//		}
//		else if ( IsEqualGUID( BFID_RGB_4, pFormats[ i ] ) )
//		{
//			ATLTRACE( "CThumb - IImageDecodeFilter : Found format BFID_RGB_4\n" );
//		}
//		else if ( IsEqualGUID( BFID_RGB_8, pFormats[ i ] ) )
//		{
//			ATLTRACE( "CThumb - IImageDecodeFilter : Found format BFID_RGB_8\n" );
//		}
//		else if ( IsEqualGUID( BFID_RGB_555, pFormats[ i ] ) )
//		{
//			ATLTRACE( "CThumb - IImageDecodeFilter : Found format BFID_RGB_555\n" );
//		}
//		else if ( IsEqualGUID( BFID_RGB_565, pFormats[ i ] ) )
//		{
//			ATLTRACE( "CThumb - IImageDecodeFilter : Found format BFID_RGB_565\n" );
//		}
//		else if ( IsEqualGUID( BFID_RGB_24, pFormats[ i ] ) )
//		{
//			ATLTRACE( "CThumb - IImageDecodeFilter : Found format BFID_RGB_24\n" );
//			bOk = true;
//		}
//		else if ( IsEqualGUID( BFID_RGB_32, pFormats[ i ] ) )
//		{
//			ATLTRACE( "CThumb - IImageDecodeFilter : Found format BFID_RGB_32\n" );
//		}
//		else if ( IsEqualGUID( BFID_RGBA_32, pFormats[ i ] ) )
//		{
//			ATLTRACE( "CThumb - IImageDecodeFilter : Found format BFID_RGBA_32\n" );
//		}
//		else if ( IsEqualGUID( BFID_GRAY_8, pFormats[ i ] ) )
//		{
//			ATLTRACE( "CThumb - IImageDecodeFilter : Found format BFID_GRAY_8\n" );
//		}
//		else if ( IsEqualGUID( BFID_GRAY_16, pFormats[ i ] ) )
//		{
//			ATLTRACE( "CThumb - IImageDecodeFilter : Found format BFID_GRAY_16\n" );
//		}
//		else
//		{
//			ATLTRACE( "CThumb - IImageDecodeFilter : Found unknown format\n" );
//		}
//	}
//	CoTaskMemFree( pFormats );
//
//	if ( ! bOk )
//	{
//		ATLTRACE( "CThumb - IImageDecodeFilter : OnBeginDecode cannot find RGB_24 format\n");
//		return E_FAIL;
//	}
//
//	return S_OK;
//}
//
//STDMETHODIMP CThumb::Process(IStream* pStream)
//{
//	HRESULT hr;
//
//	const ULONG chunk = 1024;
//	ULONG total = 0;
//	CAtlArray< unsigned char > data;
//	for (;;)
//	{
//		if ( ! data.SetCount( total + chunk ) )
//		{
//			ATLTRACE( "CThumb - IImageDecodeFilter::Process() : Out of memory\n" );
//			return E_OUTOFMEMORY;
//		}
//
//		ULONG readed = 0;
//		hr = pStream->Read( data.GetData() + total, chunk, &readed );
//		total += readed;
//		if ( FAILED( hr ) )
//		{
//			ATLTRACE( "CThumb - IImageDecodeFilter::Process() : Read error 0x%08x\n", hr );
//			return hr;
//		}
//		if ( hr == S_FALSE )
//			break;
//	}
//
//	hr = m_pEventSink->OnBitsComplete();
//	ATLTRACE( "CThumb - IImageDecodeFilter::Process() : Readed %u bytes\n", total );
//
//	GFL_BITMAP* hGflBitmap = NULL;
//	hr = _Module.LoadBitmapFromMemory( data.GetData (), total, &hGflBitmap );
//	if ( FAILED( hr ) )
//	{
//		ATLTRACE( "CThumb - IImageDecodeFilter::Process() : Load error 0x%08x\n", hr );
//		return hr;
//	}
//	ATLTRACE( "CThumb - IImageDecodeFilter::Process() : Loaded as %dx%d bitmap (%d bpl)\n", hGflBitmap->Width, hGflBitmap->Height, hGflBitmap->BytesPerLine );
//
//	CComPtr< IDirectDrawSurface > pIDirectDrawSurface;
//	hr = m_pEventSink->GetSurface( hGflBitmap->Width, hGflBitmap->Height,
//		BFID_RGB_24, 1, IMGDECODE_HINT_TOPDOWN | IMGDECODE_HINT_FULLWIDTH,
//		(IUnknown**) &pIDirectDrawSurface );
//	if (FAILED (hr))
//	{
//		ATLTRACE ("CThumb - IImageDecodeFilter::Process() : m_spEventSink->GetSurface error 0x%08x\n", hr );
//		_Module.FreeBitmap( hGflBitmap );
//		return hr;
//	}
//
//	DDSURFACEDESC desc = { sizeof( DDSURFACEDESC ) };
//	RECT rc = { 0, 0, hGflBitmap->Width, hGflBitmap->Height };
//	hr = pIDirectDrawSurface->Lock( &rc, &desc, DDLOCK_WAIT, NULL );
//	if (FAILED (hr))
//	{
//		ATLTRACE ("CThumb - IImageDecodeFilter::Process() : pIDirectDrawSurface->Lock error 0x%08x\n", hr);
//		_Module.FreeBitmap( hGflBitmap );
//		return hr;
//	}
//
//	for ( int line = 0; line < hGflBitmap->Height; ++line )
//	{
//		char* dst = (char*)desc.lpSurface + line * desc.lPitch;
//		char* src = (char*)hGflBitmap->Data + line * hGflBitmap->BytesPerLine;
//		for ( int p = 0; p < hGflBitmap->Width; ++p, dst += 3, src += hGflBitmap->BytesPerPixel )
//		{
//			// RGB -> BGR
//			dst[0] = src[2];
//			dst[1] = src[1];
//			dst[2] = src[0];
//		}
//	}
//
//	hr = pIDirectDrawSurface->Unlock( &desc );
//	if (FAILED (hr))
//	{
//		ATLTRACE ("CThumb - IImageDecodeFilter::Process() : pIDirectDrawSurface->Unlock error 0x%08x\n", hr);
//		_Module.FreeBitmap( hGflBitmap );
//		return hr;
//	}
//
//	m_pEventSink->OnDecodeComplete( S_OK );
//
//	ATLTRACE( "CThumb - IImageDecodeFilter::Process() : OK\n" );
//	_Module.FreeBitmap( hGflBitmap );
//	return hr;
//}
//
//STDMETHODIMP CThumb::Terminate(HRESULT hrStatus)
//{
//	if ( m_pEventSink )
//	{
//		m_pEventSink->OnDecodeComplete( hrStatus );
//		m_pEventSink.Release();
//	}
//
//	return S_OK;
//}

// IObjectWithSite

STDMETHODIMP CThumb::SetSite(IUnknown *pUnkSite)
{
	ATLTRACE( "CThumb - IObjectWithSite::SetSite(0x%08x)\n", pUnkSite );

	if ( pUnkSite )
		m_pSite = pUnkSite;
	else
		m_pSite.Release();

	return S_OK;
}

STDMETHODIMP CThumb::GetSite(REFIID riid, void **ppvSite)
{
	ATLTRACE ( "CThumb - IObjectWithSite::GetSite()\n" );

	if ( ! ppvSite )
		return E_POINTER;

	*ppvSite = NULL;

	if ( ! m_pSite )
		return E_FAIL;

	return m_pSite->QueryInterface( riid, ppvSite );
}

// IColumnProvider
//
//STDMETHODIMP CThumb::Initialize (LPCSHCOLUMNINIT /* psci */)
//{
//	ATLTRACENOTIMPL ("IColumnProvider::Initialize");
//}
//
//STDMETHODIMP CThumb::GetColumnInfo (DWORD /* dwIndex */, SHCOLUMNINFO* /* psci */)
//{
//	ATLTRACENOTIMPL ("IColumnProvider::GetColumnInfo");
//}
//
//STDMETHODIMP CThumb::GetItemData (LPCSHCOLUMNID /* pscid */, LPCSHCOLUMNDATA /* pscd */, VARIANT* /* pvarData */)
//{
//	ATLTRACENOTIMPL ("IColumnProvider::GetItemData");
//}

// IParentAndItem

//STDMETHODIMP CThumb::SetParentAndItem(
//	/* [unique][in] */ __RPC__in_opt PCIDLIST_ABSOLUTE /*pidlParent*/,
//	/* [unique][in] */ __RPC__in_opt IShellFolder * /*psf*/,
//	/* [in] */ __RPC__in PCUITEMID_CHILD /*pidlChild*/)
//{
//	ATLTRACENOTIMPL( _T("IParentAndItem::SetParentAndItem") );
//}
//
//STDMETHODIMP CThumb::GetParentAndItem(
//	/* [out] */ __RPC__deref_out_opt PIDLIST_ABSOLUTE * /*ppidlParent*/,
//	/* [out] */ __RPC__deref_out_opt IShellFolder ** /*ppsf*/,
//	/* [out] */ __RPC__deref_out_opt PITEMID_CHILD * /*ppidlChild*/)
//{
//	ATLTRACENOTIMPL( _T("IParentAndItem::GetParentAndItem") );
//}

// IEmptyVolumeCache

STDMETHODIMP CThumb::Initialize(
	/* [in] */ HKEY /*hkRegKey*/,
	/* [in] */ LPCWSTR pcwszVolume,
	/* [out] */ LPWSTR *ppwszDisplayName,
	/* [out] */ LPWSTR *ppwszDescription,
	/* [out] */ DWORD *pdwFlags)
{
	if ( ppwszDisplayName )
	{
		CString foo = _Module.m_oLangs.LoadString( IDS_CACHE );
		size_t len = ( foo.GetLength() + 1 ) * sizeof( TCHAR );
		*ppwszDisplayName = (LPWSTR)CoTaskMemAlloc( len );
		CopyMemory( *ppwszDisplayName, (LPCTSTR)foo, len );
	}

	if ( ppwszDescription )
	{
		CString foo = _Module.m_oLangs.LoadString( IDS_DESCRIPTION );
		size_t len = ( foo.GetLength() + 1 ) * sizeof( TCHAR );
		*ppwszDescription = (LPWSTR)CoTaskMemAlloc( len );
		CopyMemory( *ppwszDescription, (LPCTSTR)foo, len );
	}

	m_bCleanup = ( _Module.m_sDatabase.GetAt( 0 ) == *pcwszVolume );

	if ( m_bCleanup )
	{
		return S_OK;
	}

	if ( pdwFlags )
	{
		*pdwFlags |= EVCF_DONTSHOWIFZERO;
	}

	return S_FALSE;
}

STDMETHODIMP CThumb::GetSpaceUsed(
	/* [out] */ __RPC__out DWORDLONG *pdwlSpaceUsed,
	/* [in] */ __RPC__in_opt IEmptyVolumeCacheCallBack* /*picb*/)
{
	if ( ! m_bCleanup )
	{
		if ( pdwlSpaceUsed )
		{
			*pdwlSpaceUsed = 0;
		}
		return S_OK;
	}

	WIN32_FILE_ATTRIBUTE_DATA wfadDatabase = {};
	GetFileAttributesEx( _Module.m_sDatabase, GetFileExInfoStandard, &wfadDatabase );
	if ( pdwlSpaceUsed )
	{
		*pdwlSpaceUsed = MAKEQWORD( wfadDatabase.nFileSizeLow, wfadDatabase.nFileSizeHigh );
	}

	return S_OK;
}

STDMETHODIMP CThumb::Purge(
	/* [in] */ DWORDLONG /*dwlSpaceToFree*/,
	/* [in] */ __RPC__in_opt IEmptyVolumeCacheCallBack * /*picb*/)
{
	CDatabase db( _Module.m_sDatabase );
	if ( db )
	{
		db.Exec( DROP_DATABASE );
		db.Exec( RECREATE_DATABASE );
	}

	return S_OK;
}

STDMETHODIMP CThumb::ShowProperties(
	/* [in] */ __RPC__in HWND /*hwnd*/)
{
	ATLTRACENOTIMPL( _T("IEmptyVolumeCache::ShowProperties") );
}

STDMETHODIMP CThumb::Deactivate(
	/* [out] */ __RPC__out DWORD* /*pdwFlags*/)
{
	return S_OK;
}

// IEmptyVolumeCache2

STDMETHODIMP CThumb::InitializeEx(
	/* [in] */ HKEY hkRegKey,
	/* [in] */ LPCWSTR pcwszVolume,
	/* [in] */ LPCWSTR /*pcwszKeyName*/,
	/* [out] */ LPWSTR *ppwszDisplayName,
	/* [out] */ LPWSTR *ppwszDescription,
	/* [out] */ LPWSTR *ppwszBtnText,
	/* [out] */ DWORD *pdwFlags)
{
	if ( ppwszBtnText )
	{
		*ppwszBtnText = NULL;
	}

	return Initialize( hkRegKey, pcwszVolume, ppwszDisplayName, ppwszDescription, pdwFlags );
}