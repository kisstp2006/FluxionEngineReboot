#include <Fluxion/Application/Window/Clipboard.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

bool Fluxion_Clipboard_SetText(const char* text)
{
    if (!text)
    {
        return false;
    }

    int wideLength = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (wideLength <= 0)
    {
        return false;
    }

    if (!OpenClipboard(NULL))
    {
        return false;
    }
    EmptyClipboard();

    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)wideLength * sizeof(wchar_t));
    if (!handle)
    {
        CloseClipboard();
        return false;
    }

    wchar_t* locked = (wchar_t*)GlobalLock(handle);
    MultiByteToWideChar(CP_UTF8, 0, text, -1, locked, wideLength);
    GlobalUnlock(handle);

    SetClipboardData(CF_UNICODETEXT, handle);
    CloseClipboard();
    // `handle` ownership transfers to the system on success — do not GlobalFree it.
    return true;
}

bool Fluxion_Clipboard_GetText(char* outBuffer, usize bufferSize)
{
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
    {
        return false;
    }
    if (!OpenClipboard(NULL))
    {
        return false;
    }

    HGLOBAL handle = GetClipboardData(CF_UNICODETEXT);
    if (!handle)
    {
        CloseClipboard();
        return false;
    }

    const wchar_t* wideText = (const wchar_t*)GlobalLock(handle);
    if (!wideText)
    {
        CloseClipboard();
        return false;
    }

    int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wideText, -1, outBuffer, (int)bufferSize, NULL, NULL);
    GlobalUnlock(handle);
    CloseClipboard();

    return utf8Length > 0;
}
