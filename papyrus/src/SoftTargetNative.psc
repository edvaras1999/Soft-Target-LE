Scriptname SoftTargetNative Native Hidden
{ Funcoes implementadas pela DLL SoftTargetLE. O menu le e grava as opcoes por aqui,
  e a DLL grava o .ini a cada mudanca, entao menu e .ini nunca divergem.
  Sem a DLL carregada, estas funcoes devolvem valores vazios. }

bool Function GetBool(string key) global native
Function SetBool(string key, bool value) global native
int Function GetInt(string key) global native
Function SetInt(string key, int value) global native
float Function GetFloat(string key) global native
Function SetFloat(string key, float value) global native

; Relê o .ini (para quando foi editado a mao).
Function Reload() global native
