Scriptname SoftTargetMCM extends SKI_ConfigBase
{ Menu de configuracao do SoftTarget LE (SkyUI MCM). Nada e guardado aqui:
  cada opcao le e grava direto na DLL, que grava o .ini. }

int oEnabled
int oRange
int oHoldSeconds

int Function GetVersion()
	return 1
EndFunction

Event OnConfigInit()
	ModName = "SoftTarget"
	Pages = new string[1]
	Pages[0] = "General"
EndEvent

Event OnPageReset(string a_page)
	AddHeaderOption("SoftTarget LE")
	oEnabled = AddToggleOption("Enabled", SoftTargetNative.GetBool("Enabled"))
	oRange = AddSliderOption("Range", SoftTargetNative.GetFloat("Range"), "{0}")
	oHoldSeconds = AddSliderOption("Face the target after attacking", SoftTargetNative.GetInt("HoldMilliseconds") / 1000.0, "{1} s")
EndEvent

Event OnOptionSelect(int a_option)
	if a_option == oEnabled
		bool v = !SoftTargetNative.GetBool("Enabled")
		SoftTargetNative.SetBool("Enabled", v)
		SetToggleOptionValue(a_option, v)
	endIf
EndEvent

Event OnOptionSliderOpen(int a_option)
	if a_option == oRange
		SetSliderDialogStartValue(SoftTargetNative.GetFloat("Range"))
		SetSliderDialogDefaultValue(450.0)
		SetSliderDialogRange(100.0, 1500.0)
		SetSliderDialogInterval(10.0)
	elseIf a_option == oHoldSeconds
		SetSliderDialogStartValue(SoftTargetNative.GetInt("HoldMilliseconds") / 1000.0)
		SetSliderDialogDefaultValue(3.0)
		SetSliderDialogRange(0.1, 10.0)
		SetSliderDialogInterval(0.1)
	endIf
EndEvent

Event OnOptionSliderAccept(int a_option, float a_value)
	if a_option == oRange
		SoftTargetNative.SetFloat("Range", a_value)
		SetSliderOptionValue(a_option, a_value, "{0}")
	elseIf a_option == oHoldSeconds
		SoftTargetNative.SetInt("HoldMilliseconds", (a_value * 1000.0) as int)
		SetSliderOptionValue(a_option, a_value, "{1} s")
	endIf
EndEvent

Event OnOptionHighlight(int a_option)
	if a_option == oEnabled
		SetInfoText("Turns the whole mod on or off.")
	elseIf a_option == oRange
		SetInfoText("How far away an enemy can be and still be picked. 140 is about 2 meters.")
	elseIf a_option == oHoldSeconds
		SetInfoText("How long the body keeps facing the nearest enemy after an attack.")
	endIf
EndEvent
