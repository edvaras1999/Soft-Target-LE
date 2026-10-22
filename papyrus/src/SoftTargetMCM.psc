Scriptname SoftTargetMCM extends SKI_ConfigBase
{ SoftTarget LE - in-game menu (SkyUI MCM). Nothing is stored in this script:
  every option reads and writes straight to the SoftTarget DLL, which saves the .ini file. }

; ---- table of the options shown on the current page (rebuilt on every page reset)
string[] okey
int[] oid
int[] okind		; 1 = toggle, 2 = slider, 3 = menu, 4 = key
int[] oisint	; slider: 1 = integer setting, 0 = float setting
int[] omenu		; menu: which list of choices (see MenuChoices)
float[] omin
float[] omax
float[] ostep
float[] odef
float[] oscale	; shown value = real value * scale (e.g. milliseconds shown as seconds)
string[] ofmt
string[] oinfo
int ocount
int lang		; 0 = English, 1 = Portugues

int Function GetVersion()
	return 3
EndFunction

Function InitTables()
	okey = new string[48]
	oid = new int[48]
	okind = new int[48]
	oisint = new int[48]
	omenu = new int[48]
	omin = new float[48]
	omax = new float[48]
	ostep = new float[48]
	odef = new float[48]
	oscale = new float[48]
	ofmt = new string[48]
	oinfo = new string[48]
	ocount = 0
EndFunction

Event OnConfigInit()
	ModName = "SoftTarget"
	InitTables()
	SetPageNames()
EndEvent

Event OnVersionUpdate(int a_version)
	InitTables()
	SetPageNames()
EndEvent

Event OnConfigOpen()
	InitTables()
	SetPageNames()
EndEvent

; Page names follow the chosen language (they refresh the next time the menu is opened).
Function SetPageNames()
	lang = SoftTargetNative.GetInt("Language")
	Pages = new string[5]
	Pages[0] = T("General", "Geral")
	Pages[1] = T("Targeting", "Alvo")
	Pages[2] = T("Combos and turning", "Combos e giro")
	Pages[3] = T("Camera and movement", "Camera e movimento")
	Pages[4] = T("Magic aim", "Mira da magia")
EndFunction

string Function T(string en, string pt)
	if lang == 1 && pt != ""
		return pt
	endIf
	return en
EndFunction

; ---- lists of choices for the menu options
string[] Function MenuChoices(int menuId)
	string[] r
	if menuId == 1
		r = new string[2]
		r[0] = "English"
		r[1] = "Português"
	elseIf menuId == 2
		r = new string[2]
		r[0] = T("Cone in front", "Cone à frente")
		r[1] = T("Circle around (360)", "Círculo à volta (360)")
	elseIf menuId == 7
		r = new string[3]
		r[0] = T("Automatic", "Automático")
		r[1] = T("Without TDM", "Sem TDM")
		r[2] = T("Coeus (TDM)", "Coeus (TDM)")
	elseIf menuId == 3
		r = new string[4]
		r[0] = T("Off", "Desligado")
		r[1] = T("Left button (M2)", "Botão esquerdo (M2)")
		r[2] = T("Right button (M1)", "Botão direito (M1)")
		r[3] = T("Both", "Ambos")
	elseIf menuId == 4
		r = new string[4]
		r[0] = T("Off (do not touch the camera)", "Desligado (não mexe na câmera)")
		r[1] = T("Counter-rotate", "Contra-rotação")
		r[2] = T("Restore angle", "Repor o ângulo")
		r[3] = T("Offset (recommended)", "Deslocamento (recomendado)")
	elseIf menuId == 5
		r = new string[4]
		r[0] = T("Off", "Desligado")
		r[1] = T("Tilt the body", "Inclinar o corpo")
		r[2] = T("Tilt the camera", "Inclinar a câmera")
		r[3] = T("Both", "Ambos")
	else
		r = new string[2]
		r[0] = T("Pause SoftTarget", "Pausar o SoftTarget")
		r[1] = T("Body only (camera untouched)", "Só o corpo (câmera intacta)")
	endIf
	return r
EndFunction

string Function ChoiceText(int menuId, int index)
	string[] r = MenuChoices(menuId)
	if index < 0 || index >= r.Length
		index = 0
	endIf
	return r[index]
EndFunction

; ---- helpers to add one row
Function AddToggleRow(string key, string label, float def, string info)
	if ocount >= 48
		return
	endIf
	int i = ocount
	ocount += 1
	okey[i] = key
	okind[i] = 1
	odef[i] = def
	oinfo[i] = info
	oid[i] = AddToggleOption(label, SoftTargetNative.GetBool(key))
EndFunction

Function AddSliderRow(string key, string label, float minV, float maxV, float stepV, float def, float scale, int isInt, string fmt, string info)
	if ocount >= 48
		return
	endIf
	int i = ocount
	ocount += 1
	okey[i] = key
	okind[i] = 2
	oisint[i] = isInt
	omin[i] = minV
	omax[i] = maxV
	ostep[i] = stepV
	odef[i] = def
	oscale[i] = scale
	ofmt[i] = fmt
	oinfo[i] = info
	oid[i] = AddSliderOption(label, ShownValue(i), fmt)
EndFunction

Function AddMenuRow(string key, string label, int menuId, float def, string info)
	if ocount >= 48
		return
	endIf
	int i = ocount
	ocount += 1
	okey[i] = key
	okind[i] = 3
	omenu[i] = menuId
	odef[i] = def
	oinfo[i] = info
	oid[i] = AddMenuOption(label, ChoiceText(menuId, SoftTargetNative.GetInt(key)))
EndFunction

Function AddKeyRow(string key, string label, string info)
	if ocount >= 48
		return
	endIf
	int i = ocount
	ocount += 1
	okey[i] = key
	okind[i] = 4
	odef[i] = 0.0
	oinfo[i] = info
	oid[i] = AddKeyMapOption(label, SoftTargetNative.GetInt(key))
EndFunction

float Function ShownValue(int i)
	float v
	if oisint[i] == 1
		v = SoftTargetNative.GetInt(okey[i]) as float
	else
		v = SoftTargetNative.GetFloat(okey[i])
	endIf
	return v * oscale[i]
EndFunction

int Function FindRow(int a_option)
	int i = 0
	while i < ocount
		if oid[i] == a_option
			return i
		endIf
		i += 1
	endWhile
	return -1
EndFunction

Function StoreSlider(int i, float shown)
	float raw = shown / oscale[i]
	if oisint[i] == 1
		SoftTargetNative.SetInt(okey[i], (raw + 0.5) as int)
	else
		SoftTargetNative.SetFloat(okey[i], raw)
	endIf
EndFunction

; ---- pages
Event OnPageReset(string a_page)
	InitTables()
	lang = SoftTargetNative.GetInt("Language")

	int p = 0
	if a_page != ""
		p = Pages.Find(a_page)
		if p < 0
			p = 0
		endIf
	endIf

	if p == 0
		PageGeneral()
	elseIf p == 1
		PageTargeting()
	elseIf p == 2
		PageBody()
	elseIf p == 3
		PageCamera()
	else
		PageMagic()
	endIf
EndEvent

string Function ProfileName(int v)
	if v == 2
		return T("Coeus (TDM)", "Coeus (TDM)")
	endIf
	return T("Without TDM", "Sem TDM")
EndFunction

Function AddButtonRow(string key, string label, string btn, string info)
	if ocount >= 48
		return
	endIf
	int i = ocount
	ocount += 1
	okey[i] = key
	okind[i] = 5
	oinfo[i] = info
	oid[i] = AddTextOption(label, btn)
EndFunction

Function AddStatusRow(string label, string value, string info)
	if ocount >= 48
		return
	endIf
	int i = ocount
	ocount += 1
	okey[i] = ""
	okind[i] = 0
	oinfo[i] = info
	oid[i] = AddTextOption(label, value, OPTION_FLAG_DISABLED)
EndFunction

string Function YesNo(int v)
	if v != 0
		return T("yes", "sim")
	endIf
	return T("no", "não")
EndFunction

Function PageGeneral()
	AddHeaderOption("SoftTarget LE")
	AddEmptyOption()
	AddToggleRow("Enabled", T("Enabled", "Ativado"), 1.0, T("Turns the whole mod on or off.", "Liga ou desliga o mod inteiro."))
	AddMenuRow("Language", T("Menu language", "Idioma do menu"), 1, 0.0, T("Language of this menu. Page names update the next time you open the menu.", "Idioma deste menu. Os nomes das páginas mudam na próxima vez que abrir o menu."))
	AddMenuRow("Profile", T("Profile", "Perfil"), 7, 0.0, T("Automatic: Coeus profile while Coeus directional movement is active, otherwise Without TDM. You can also force one of them.", "Automático: perfil Coeus enquanto o movimento direcional do Coeus está ativo, senão Sem TDM. Também pode forçar um deles."))
	AddEmptyOption()
	AddHeaderOption(T("Status", "Estado"))
	AddEmptyOption()
	AddStatusRow(T("Active profile", "Perfil ativo"), ProfileName(SoftTargetNative.Status(0)), T("The profile in use right now.", "O perfil em uso neste momento."))
	AddStatusRow(T("Lock-on active", "Lock-on ativo"), YesNo(SoftTargetNative.Status(3)), "")
	AddStatusRow(T("Coeus found", "Coeus encontrado"), YesNo(SoftTargetNative.Status(2)), "")
	AddStatusRow(T("LockOn.dll found", "LockOn.dll encontrada"), YesNo(SoftTargetNative.Status(1)), "")
	AddHeaderOption(T("Settings file", "Ficheiro de configuração"))
	AddEmptyOption()
	AddButtonRow("@save", T("Save now", "Guardar agora"), T("[Save]", "[Guardar]"), T("Writes every option to SoftTargetLE.ini. Every change is already saved as you make it; this is a safety copy.", "Grava todas as opções no SoftTargetLE.ini. Cada mudança já é gravada na hora; isto é uma garantia extra."))
	AddButtonRow("@reset", T("Reset to defaults", "Repor valores de fábrica"), T("[Reset]", "[Repor]"), T("Restores all options to the factory values (the language is kept).", "Repõe todas as opções para os valores de fábrica (o idioma é mantido)."))
	AddHeaderOption(T("Diagnostics", "Diagnóstico"))
	AddEmptyOption()
	AddToggleRow("LogOnly", T("Log only (do not turn)", "Só registar (não virar)"), 0.0, T("Only writes the chosen target to the log, without turning the character.", "Só escreve o alvo escolhido no registo, sem virar o personagem."))
	AddToggleRow("Probe", T("Detailed log", "Registo detalhado"), 0.0, T("Writes camera and enemy data to SoftTargetLE.log. Makes the log very large; keep it off normally.", "Escreve dados da câmera e dos inimigos no SoftTargetLE.log. Deixa o registo muito grande; normalmente deve ficar desligado."))
EndFunction

Function PageTargeting()
	AddHeaderOption(T("Detection area (both profiles)", "Área de deteção (ambos os perfis)"))
	AddEmptyOption()
	AddMenuRow("Shape", T("Shape", "Forma"), 2, 0.0, T("Cone in front of the player, or a full circle around (best with directional or 360 movement).", "Cone à frente do jogador, ou um círculo completo à volta (melhor com movimento direcional ou de 360 graus)."))
	AddSliderRow("Range", T("Range", "Alcance"), 100.0, 1500.0, 10.0, 450.0, 1.0, 0, "{0}", T("How far an enemy can be and still be picked. 140 is about 2 meters.", "A que distância um inimigo ainda pode ser escolhido. 140 é cerca de 2 metros."))
	AddSliderRow("ConeDegrees", T("Cone angle", "Ângulo do cone"), 30.0, 360.0, 5.0, 140.0, 1.0, 0, "{0}", T("Total width of the cone, in degrees (cone shape only).", "Largura total do cone, em graus (só na forma de cone)."))
	AddSliderRow("MaxHeightDifference", T("Max height difference", "Diferença máxima de altura"), 100.0, 1500.0, 10.0, 600.0, 1.0, 0, "{0}", T("Enemies higher or lower than this are ignored (stairs, rocks, cliffs).", "Inimigos mais altos ou mais baixos que isto são ignorados (escadas, rochas, penhascos)."))
	AddHeaderOption(T("Who counts as an enemy", "Quem conta como inimigo"))
	AddEmptyOption()
	AddToggleRow("RequireCombat", T("Only enemies in combat", "Só inimigos em combate"), 1.0, T("If on, only enemies already fighting you are picked.", "Se ligado, só são escolhidos inimigos que já estão a lutar contra si."))
	AddToggleRow("UseHostileList", T("Use the game's hostile list", "Usar a lista de hostis do jogo"), 1.0, T("Also accepts anyone the game already lists as hostile. Helps with reanimated and summoned enemies.", "Aceita também quem o jogo já lista como hostil. Ajuda com inimigos reanimados e invocados."))
	AddToggleRow("AllowReanimated", T("Accept reanimated enemies", "Aceitar inimigos reanimados"), 1.0, T("Also accepts enemies in the game's 'reanimate' life state. Turn off if corpses get targeted.", "Aceita também inimigos no estado de vida 'reanimar' do jogo. Desligue se cadáveres forem escolhidos."))
	AddEmptyOption()
	AddHeaderOption(T("Switching targets", "Troca de alvo"))
	AddEmptyOption()
	AddToggleRow("StickyTarget", T("Keep target during a combo", "Manter o alvo durante o combo"), 1.0, T("While you keep attacking, the body stays on the same enemy until it dies or leaves the area, instead of jumping between enemies.", "Enquanto continua a atacar, o corpo fica no mesmo inimigo até ele morrer ou sair da área, em vez de saltar entre inimigos."))
	AddSliderRow("SwitchMarginPercent", T("Switch margin", "Margem de troca"), 0.0, 50.0, 1.0, 0.0, 1.0, 0, "{0} %", T("When not in a combo: how much closer another enemy must be before the target changes.", "Fora de um combo: quanto mais perto outro inimigo tem de estar para o alvo mudar."))
	AddSliderRow("RetargetMilliseconds", T("Re-check every", "Reavaliar a cada"), 16.0, 300.0, 4.0, 80.0, 1.0, 1, "{0} ms", T("How often the nearest enemy is re-evaluated while turning. Lower switches faster.", "De quanto em quanto tempo o inimigo mais próximo é reavaliado durante o giro. Menos troca mais depressa."))
EndFunction

Function PageBody()
	AddHeaderOption(T("Combos and clicking", "Combos e cliques"))
	AddEmptyOption()
	AddToggleRow("TapAssist", T("Turn when attacking", "Virar ao atacar"), 1.0, T("The body turns toward the nearest enemy when you press an attack button (spells included).", "O corpo vira para o inimigo mais próximo quando carrega num botão de ataque (magia incluída)."))
	AddSliderRow("HoldMilliseconds", T("Combo window", "Janela do combo"), 100.0, 10000.0, 100.0, 3000.0, 0.001, 1, "{1} s", T("Every click keeps the body on the target for this long. The sequence only ends, and the body only returns, after you stop clicking for this time. Default 3 seconds.", "Cada clique mantém o corpo no alvo por este tempo. A sequência só termina, e o corpo só volta, depois de parar de clicar por este tempo. Padrão: 3 segundos."))
	AddMenuRow("HoldFaceButton", T("Keep facing while holding", "Encarar enquanto segura"), 3, 3.0, T("While you hold this button (block), the body keeps following the nearest enemy.", "Enquanto segura este botão (bloqueio), o corpo continua a seguir o inimigo mais próximo."))
	AddEmptyOption()
	AddHeaderOption(T("Turning", "Giro"))
	AddEmptyOption()
	AddSliderRow("TurnSpeedDegPerSec", T("Max turn speed", "Velocidade máxima do giro"), 90.0, 1800.0, 10.0, 900.0, 1.0, 0, "{0} deg/s", T("Maximum turn speed in degrees per second. Lower is slower.", "Velocidade máxima do giro, em graus por segundo. Menos é mais lento."))
	AddSliderRow("Smoothing", T("Smoothing", "Suavização"), 0.0, 30.0, 0.5, 0.0, 1.0, 0, "{1}", T("0 = constant speed. Higher values slow down smoothly when arriving at the target. Try 8 to 15.", "0 = velocidade constante. Valores maiores abrandam suavemente ao chegar ao alvo. Experimente 8 a 15."))
	AddSliderRow("UpdateMilliseconds", T("Update interval", "Intervalo de atualização"), 1.0, 30.0, 1.0, 8.0, 1.0, 1, "{0} ms", T("How often the turn is updated. Lower is smoother.", "De quanto em quanto tempo o giro é atualizado. Menos é mais fluido."))
	AddEmptyOption()
	AddHeaderOption(T("After the combo", "Depois do combo"))
	AddEmptyOption()
	AddToggleRow("ReturnToHeading", T("Return to previous heading", "Voltar ao rumo anterior"), 1.0, T("After the combo window ends, the body turns back to where it was facing. Does not return while you hold the walk keys.", "Depois que a janela do combo termina, o corpo volta para onde estava virado. Não volta enquanto segura as teclas de andar."))
	AddSliderRow("ReturnSpeedDegPerSec", T("Return speed", "Velocidade do regresso"), 90.0, 1000.0, 10.0, 360.0, 1.0, 0, "{0} deg/s", T("How fast the body turns back, in degrees per second.", "Com que velocidade o corpo volta, em graus por segundo."))
	AddSliderRow("ReturnMaxMilliseconds", T("Max return time", "Tempo máximo do regresso"), 200.0, 3000.0, 50.0, 1200.0, 0.001, 1, "{1} s", T("The return gives up after this time.", "O regresso desiste depois deste tempo."))
EndFunction

Function PageCamera()
	int prof = SoftTargetNative.GetInt("Profile")
	if prof == 0 || prof == 2
		AddHeaderOption(T("Coeus (TDM) profile", "Perfil Coeus (TDM)"))
		AddEmptyOption()
		AddMenuRow("CameraModeCoeus", T("Camera while turning", "Câmera durante o giro"), 4, 0.0, T("With Coeus the camera is free, so 'Off' (do not touch the camera) is usually right. If the camera spins by itself, keep it on Off.", "Com o Coeus a câmera é livre, então 'Desligado' (não mexer na câmera) costuma ser o certo. Se a câmera girar sozinha, deixe em Desligado."))
		AddToggleRow("CoeusYield", T("Ask Coeus to let go of the body", "Pedir ao Coeus para largar o corpo"), 1.0, T("While SoftTarget turns the body, Coeus stops rotating it. Walking becomes relative to the body, like in a lock-on.", "Enquanto o SoftTarget vira o corpo, o Coeus deixa de o rodar. Andar passa a ser relativo ao corpo, como num lock-on."))
	endIf
	if prof == 0 || prof == 1
		AddHeaderOption(T("Without TDM profile", "Perfil Sem TDM"))
		AddEmptyOption()
		AddMenuRow("CameraMode", T("Camera while turning", "Câmera durante o giro"), 4, 3.0, T("Keeps the camera still while the body turns. Offset is the recommended mode.", "Mantém a câmera parada enquanto o corpo vira. Deslocamento é o modo recomendado."))
		AddEmptyOption()
	endIf
	AddHeaderOption(T("Lock-on (Simple Lock-On / Coeus)", "Lock-on (Simple Lock-On / Coeus)"))
	AddEmptyOption()
	AddMenuRow("LockOnMode", T("While locked on", "Com o lock-on ativo"), 6, 0.0, T("Pause SoftTarget while the lock-on is active, or let it turn only the body without touching the camera.", "Pausar o SoftTarget enquanto o lock-on está ativo, ou deixá-lo virar só o corpo sem mexer na câmera."))
	AddKeyRow("LockOnKey", T("Lock-on key (fallback)", "Tecla do lock-on (reserva)"), T("Only used when the lock-on state cannot be read from LockOn.dll. Each press toggles the pause.", "Só é usada quando o estado do lock-on não pode ser lido da LockOn.dll. Cada toque liga ou desliga a pausa."))
EndFunction

Function PageMagic()
	AddHeaderOption(T("Vertical aim (spells)", "Mira vertical (magia)"))
	AddEmptyOption()
	AddMenuRow("VerticalMode", T("Vertical aim", "Mira vertical"), 5, 0.0, T("Tilts the body or the camera so spells hit enemies above or below you.", "Inclina o corpo ou a câmera para a magia acertar em inimigos acima ou abaixo de si."))
	AddToggleRow("VerticalInvert", T("Invert tilt", "Inverter a inclinação"), 0.0, T("Turn on if spells go up when they should go down.", "Ligue se a magia sobe quando devia descer."))
	AddSliderRow("AimHeight", T("Aim height on enemy", "Altura da mira no inimigo"), 0.0, 200.0, 5.0, 100.0, 1.0, 0, "{0}", T("Height of the aim point above the enemy's feet. 100 is the chest of a human.", "Altura do ponto de mira acima dos pés do inimigo. 100 é o peito de um humano."))
	AddSliderRow("PlayerAimHeight", T("Aim height on player", "Altura da mira no jogador"), 0.0, 200.0, 5.0, 110.0, 1.0, 0, "{0}", T("Height the spell leaves from, above your feet.", "Altura de onde a magia sai, acima dos seus pés."))
	AddSliderRow("VerticalLimitDegrees", T("Max tilt", "Inclinação máxima"), 5.0, 80.0, 1.0, 45.0, 1.0, 0, "{0} deg", T("Maximum tilt, in degrees.", "Inclinação máxima, em graus."))
EndFunction

; ---- events
Event OnOptionSelect(int a_option)
	int i = FindRow(a_option)
	if i < 0
		return
	endIf
	if okind[i] == 1
		bool v = !SoftTargetNative.GetBool(okey[i])
		SoftTargetNative.SetBool(okey[i], v)
		SetToggleOptionValue(a_option, v)
	elseIf okind[i] == 5
		if okey[i] == "@save"
			SoftTargetNative.SaveAll()
			SetTextOptionValue(a_option, T("[Saved]", "[Guardado]"))
		elseIf okey[i] == "@reset"
			if ShowMessage(T("Restore all SoftTarget options to the factory values?", "Repor todas as opções do SoftTarget para os valores de fábrica?"), true, T("Reset", "Repor"), T("Cancel", "Cancelar"))
				SoftTargetNative.ResetDefaults()
				ForcePageReset()
			endIf
		endIf
	endIf
EndEvent

Event OnOptionDefault(int a_option)
	int i = FindRow(a_option)
	if i < 0
		return
	endIf
	if okind[i] == 1
		bool v = odef[i] != 0.0
		SoftTargetNative.SetBool(okey[i], v)
		SetToggleOptionValue(a_option, v)
	elseIf okind[i] == 2
		StoreSlider(i, odef[i] * oscale[i])
		SetSliderOptionValue(a_option, ShownValue(i), ofmt[i])
	elseIf okind[i] == 3
		SoftTargetNative.SetInt(okey[i], odef[i] as int)
		SetMenuOptionValue(a_option, ChoiceText(omenu[i], odef[i] as int))
	elseIf okind[i] == 4
		SoftTargetNative.SetInt(okey[i], 0)
		SetKeyMapOptionValue(a_option, 0)
	endIf
EndEvent

Event OnOptionSliderOpen(int a_option)
	int i = FindRow(a_option)
	if i < 0
		return
	endIf
	SetSliderDialogStartValue(ShownValue(i))
	SetSliderDialogDefaultValue(odef[i] * oscale[i])
	SetSliderDialogRange(omin[i] * oscale[i], omax[i] * oscale[i])
	SetSliderDialogInterval(ostep[i] * oscale[i])
EndEvent

Event OnOptionSliderAccept(int a_option, float a_value)
	int i = FindRow(a_option)
	if i < 0
		return
	endIf
	StoreSlider(i, a_value)
	SetSliderOptionValue(a_option, ShownValue(i), ofmt[i])
EndEvent

Event OnOptionMenuOpen(int a_option)
	int i = FindRow(a_option)
	if i < 0
		return
	endIf
	SetMenuDialogOptions(MenuChoices(omenu[i]))
	SetMenuDialogStartIndex(SoftTargetNative.GetInt(okey[i]))
	SetMenuDialogDefaultIndex(odef[i] as int)
EndEvent

Event OnOptionMenuAccept(int a_option, int a_index)
	int i = FindRow(a_option)
	if i < 0
		return
	endIf
	SoftTargetNative.SetInt(okey[i], a_index)
	if okey[i] == "Language"
		lang = a_index
		ForcePageReset()
	elseIf okey[i] == "Profile"
		ForcePageReset()
	else
		SetMenuOptionValue(a_option, ChoiceText(omenu[i], a_index))
	endIf
EndEvent

Event OnOptionKeyMapChange(int a_option, int a_keyCode, string a_conflictControl, string a_conflictName)
	int i = FindRow(a_option)
	if i < 0
		return
	endIf
	SoftTargetNative.SetInt(okey[i], a_keyCode)
	SetKeyMapOptionValue(a_option, a_keyCode)
EndEvent

Event OnOptionHighlight(int a_option)
	int i = FindRow(a_option)
	if i >= 0
		SetInfoText(oinfo[i])
	endIf
EndEvent
