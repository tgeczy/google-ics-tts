/* ics_util.c -- fopen for UTF-8 paths, so voices and the dictionary load from
   folders whose names fall outside the ANSI code page. */
#include "ics_util.h"

#ifdef _WIN32
#include <stdlib.h>
#include <windows.h>

FILE *ics_fopen(const char *path, const char *mode)
{
	wchar_t wmode[8];
	wchar_t *wpath;
	FILE *f;
	int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
	if (n <= 0 || !MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 8))
		return NULL;
	wpath = (wchar_t *) malloc(sizeof(wchar_t) * (size_t) n);
	if (!wpath)
		return NULL;
	MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, n);
	f = _wfopen(wpath, wmode);
	free(wpath);
	return f;
}

int ics_remove(const char *path)
{
	wchar_t *wpath;
	int ok, n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
	if (n <= 0 || !(wpath = (wchar_t *) malloc(sizeof(wchar_t) * (size_t) n)))
		return -1;
	MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, n);
	ok = DeleteFileW(wpath);
	free(wpath);
	return ok ? 0 : -1;
}
#else
FILE *ics_fopen(const char *path, const char *mode)
{
	return fopen(path, mode);
}

int ics_remove(const char *path)
{
	return remove(path);
}
#endif
