// dinput8_proxy.h
// Declares the 48 DirectInput8 export stubs that forward to the real dinput8.dll.
// This allows Windows to load our DLL in place of dinput8.dll (ASI loader pattern).
#pragma once
void InitProxy();   // Load real dinput8.dll and resolve all exports
void FreeProxy();
