#ifndef _VALUE_H
#define _VALUE_H

#include "_global.h"

//functions
bool valuesignedcalc();
void valuesetsignedcalc(bool a);
bool valapifromstring(const char* name, duint* value, bool silent);
bool convertNumber(const char* str, duint & result, int radix);
bool convertLongLongNumber(const char* str, unsigned long long & result, int radix);
int valfromstring_noexpr_isvar(const char* string);
bool valfromstring_noexpr(const char* string, duint* value, bool silent = true, bool baseonly = false, int* value_size = nullptr, bool* isvar = nullptr, bool* hexonly = nullptr);
bool valfromstring(const char* string, duint* value, bool silent = true, bool baseonly = false, int* value_size = nullptr, bool* isvar = nullptr, bool* hexonly = nullptr, bool allowassign = false);
bool valflagfromstring(duint eflags, const char* string);
bool valsetbuffer(const char* string, const void* data, size_t size, bool silent);
bool valsetscalar(const char* string, duint value, bool silent);
bool valrequiresbuffer(const char* string);
bool valmxcsrflagfromstring(duint mxcsrflags, const char* string);
bool valx87statuswordflagfromstring(duint statusword, const char* string);
bool valx87controlwordflagfromstring(duint controlword, const char* string);
unsigned short valmxcsrfieldfromstring(duint mxcsrflags, const char* string);
unsigned short valx87statuswordfieldfromstring(duint statusword, const char* string);
unsigned short valx87controlwordfieldfromstring(duint controlword, const char* string);
duint valfileoffsettova(const char* modname, duint offset);
duint valvatofileoffset(duint va);
bool setregister(const char* string, duint value);
bool setflag(const char* string, bool set);
duint getregister(int* size, const char* string);

#endif // _VALUE_H
