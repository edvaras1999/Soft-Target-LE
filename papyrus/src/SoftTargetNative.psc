Scriptname SoftTargetNative Native Hidden
{ Native functions implemented by the SoftTargetLE DLL. The menu reads and writes options through
  these, and the DLL saves the .ini on every change, so the menu and the .ini never disagree. }

bool Function GetBool(string key) global native
Function SetBool(string key, bool value) global native
int Function GetInt(string key) global native
Function SetInt(string key, int value) global native
float Function GetFloat(string key) global native
Function SetFloat(string key, float value) global native

; Re-reads the .ini (after editing it by hand).
Function Reload() global native

; Writes every option to the .ini.
Function SaveAll() global native

; Restores the factory values (keeps the language) and saves.
Function ResetDefaults() global native

; 0 = active profile (1 without TDM, 2 Coeus), 1 = LockOn.dll found, 2 = Coeus found, 3 = lock-on active
int Function Status(int what) global native
