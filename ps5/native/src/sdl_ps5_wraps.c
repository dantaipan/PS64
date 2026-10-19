/*
 * SDL's PS5 driver sets up USB keyboards and the system text-input dialog.
 * Those system libraries aren't connected for a native app (the first call
 * jumped to address 0), and PS64 needs neither, so these stand-ins report
 * "not available"; SDL carries on without them. Selected with
 * APP_WRAP_SYMBOLS (each sceX resolves to __wrap_sceX).
 */
#include <stdint.h>

#define NOT_AVAILABLE ((int)0x80020002)

int __wrap_sceKeyboardInit(void);
int __wrap_sceKeyboardOpen(int user, int type, int index, void *param);
int __wrap_sceKeyboardClose(int handle);
int __wrap_sceKeyboardReadState(int handle, void *data);
int __wrap_sceImeDialogInit(const void *param, void *extended);
int __wrap_sceImeDialogGetStatus(void);
int __wrap_sceImeDialogGetResult(void *result);
int __wrap_sceImeDialogTerm(void);

int __wrap_sceKeyboardInit(void) { return NOT_AVAILABLE; }
int __wrap_sceKeyboardOpen(int user, int type, int index, void *param)
{
    (void)user; (void)type; (void)index; (void)param;
    return NOT_AVAILABLE;
}
int __wrap_sceKeyboardClose(int handle) { (void)handle; return 0; }
int __wrap_sceKeyboardReadState(int handle, void *data) { (void)handle; (void)data; return NOT_AVAILABLE; }
int __wrap_sceImeDialogInit(const void *param, void *extended) { (void)param; (void)extended; return NOT_AVAILABLE; }
int __wrap_sceImeDialogGetStatus(void) { return 0; }        /* SCE_IME_DIALOG_STATUS_NONE */
int __wrap_sceImeDialogGetResult(void *result) { (void)result; return NOT_AVAILABLE; }
int __wrap_sceImeDialogTerm(void) { return 0; }
