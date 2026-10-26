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
	return 13
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
	Pages = new string[7]
	Pages[0] = T("General", "Geral")
	Pages[1] = T("Lock-On", "Lock-On")
	Pages[2] = T("On-screen dot", "Ponto no ecrã")
	Pages[3] = T("Targeting", "Alvo")
	Pages[4] = T("Combos and turning", "Combos e giro")
	Pages[5] = T("Camera and movement", "Câmera e movimento")
	Pages[6] = T("Magic aim", "Mira da magia")
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
		r = new string[2]
		r[0] = T("Normal (vanilla movement / Coeus Unsheathed off)", "Normal (movimento do jogo / Coeus Unsheathed desligado)")
		r[1] = T("FreeCam / Coeus (Unsheathed on)", "FreeCam / Coeus (Unsheathed ligado)")
	elseIf menuId == 3
		r = new string[4]
		r[0] = T("Off", "Desligado")
		r[1] = T("Left button (M2)", "Botão esquerdo (M2)")
		r[2] = T("Right button (M1)", "Botão direito (M1)")
		r[3] = T("Both", "Ambos")
	elseIf menuId == 8
		r = new string[4]
		r[0] = T("Dot", "Ponto")
		r[1] = T("Ring and dot", "Anel e ponto")
		r[2] = T("Simple Lock-On reticle (.swf)", "Retícula do Simple Lock-On (.swf)")
		r[3] = T("Coeus-style reticle (.swf)", "Retícula estilo Coeus (.swf)")
	elseIf menuId == 9
		r = new string[6]
		r[0] = T("White", "Branco")
		r[1] = T("Red", "Vermelho")
		r[2] = T("Yellow", "Amarelo")
		r[3] = T("Cyan", "Ciano")
		r[4] = T("Green", "Verde")
		r[5] = T("Orange", "Laranja")
	elseIf menuId == 5
		r = new string[2]
		r[0] = T("Off", "Desligado")
		r[1] = T("Tilt the body (arms only)", "Inclinar o corpo (só os braços)")
	else
		r = new string[3]
		r[0] = T("Pause SoftTarget", "Pausar o SoftTarget")
		r[1] = T("Body only (camera untouched)", "Só o corpo (câmera intacta)")
		r[2] = T("Pause + switch lock-on to nearest", "Pausar + trocar o lock-on para o mais próximo")
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
		PageLock()
	elseIf p == 2
		PageDot()
	elseIf p == 3
		PageTargeting()
	elseIf p == 4
		PageBody()
	elseIf p == 5
		PageCamera()
	else
		PageMagic()
	endIf
EndEvent

string Function ProfileName(int v)
	if v == 2
		return T("FreeCam / Coeus (Unsheathed on)", "FreeCam / Coeus (Unsheathed ligado)")
	endIf
	return T("Normal (vanilla movement)", "Normal (movimento do jogo)")
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
	AddMenuRow("ProfileMode", T("Profile", "Perfil"), 7, 0.0, T("Normal: no directional movement (Coeus off, or Coeus with EnableUnsheathed=0, so weapons-drawn movement is vanilla). FreeCam / Coeus: Coeus directional movement is active with weapons drawn (EnableUnsheathed=1). Pick the one that matches your Coeus setting.", "Normal: sem movimento direcional (Coeus desligado, ou Coeus com EnableUnsheathed=0, então com armas na mão o movimento é o do jogo). FreeCam / Coeus: o movimento direcional do Coeus está ativo com armas na mão (EnableUnsheathed=1). Escolha o que corresponde à sua configuração do Coeus."))
	AddEmptyOption()
	AddHeaderOption(T("Status", "Estado"))
	AddEmptyOption()
	AddStatusRow(T("Active profile", "Perfil ativo"), ProfileName(SoftTargetNative.Status(0)), T("The profile you chose above.", "O perfil escolhido acima."))
	AddStatusRow(T("Lock-on active", "Lock-on ativo"), YesNo(SoftTargetNative.Status(3)), T("Whether the lock-on (this mod's or LockOn.dll) is on right now.", "Se o lock-on (deste mod ou da LockOn.dll) está ligado agora."))
	AddStatusRow(T("Coeus found", "Coeus encontrado"), YesNo(SoftTargetNative.Status(2)), "")
	AddStatusRow(T("LockOn.dll found", "LockOn.dll encontrada"), YesNo(SoftTargetNative.Status(1)), "")
	AddHeaderOption(T("Settings file", "Ficheiro de configuração"))
	AddEmptyOption()
	AddButtonRow("@save", T("Save my profile", "Guardar o meu perfil"), T("[Save]", "[Guardar]"), T("Saves all current options as your own profile (SoftTargetLE_User.ini). Every change is already written to SoftTargetLE.ini as you make it.", "Guarda todas as opções atuais como o seu perfil (SoftTargetLE_User.ini). Cada mudança já é gravada no SoftTargetLE.ini na hora."))
	AddButtonRow("@load", T("Load my profile", "Carregar o meu perfil"), T("[Load]", "[Carregar]"), T("Replaces all current options with the ones saved in your profile.", "Substitui todas as opções atuais pelas que guardou no seu perfil."))
	AddButtonRow("@reset", T("Reset to defaults", "Repor valores de fábrica"), T("[Reset]", "[Repor]"), T("Restores all options to the factory values (the language is kept).", "Repõe todas as opções para os valores de fábrica (o idioma é mantido)."))
	AddEmptyOption()
	AddHeaderOption(T("Diagnostics", "Diagnóstico"))
	AddEmptyOption()
	AddToggleRow("LogOnly", T("Log only (do not turn)", "Só registar (não virar)"), 0.0, T("Only writes the chosen target to the log, without turning the character.", "Só escreve o alvo escolhido no registo, sem virar o personagem."))
	AddToggleRow("Probe", T("Detailed log", "Registo detalhado"), 0.0, T("Writes camera and enemy data to SoftTargetLE.log. Makes the log very large; keep it off normally.", "Escreve dados da câmera e dos inimigos no SoftTargetLE.log. Deixa o registo muito grande; normalmente deve ficar desligado."))
EndFunction

Function PageTargeting()
	AddHeaderOption(T("Detection area (the nearest enemy is always chosen)", "Área de deteção (escolhe sempre o inimigo mais próximo)"))
	AddEmptyOption()
	AddMenuRow("Shape", T("Shape", "Forma"), 2, 0.0, T("Cone in front of the player, or a full circle around (best with directional or 360 movement).", "Cone à frente do jogador, ou um círculo completo à volta (melhor com movimento direcional ou de 360 graus)."))
	AddSliderRow("Range", T("Range", "Alcance"), 100.0, 1500.0, 10.0, 450.0, 1.0, 0, "{0}", T("How far an enemy can be and still be picked. It is measured to the part of the body closest to you (head, legs, wings, tail), not to its center. 140 is about 2 meters.", "A que distância um inimigo ainda pode ser escolhido. Mede-se até à parte do corpo mais próxima de si (cabeça, pernas, asas, cauda), não até ao centro. 140 é cerca de 2 metros."))
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
	AddSliderRow("SwitchMarginPercent", T("Switch margin", "Margem de troca"), 0.0, 50.0, 1.0, 0.0, 1.0, 0, "{0} %", T("0 = the body always follows the nearest enemy. Higher values keep the current target until another is this much closer.", "0 = o corpo segue sempre o inimigo mais próximo. Valores maiores mantêm o alvo atual até outro estar tanto mais perto."))
	AddSliderRow("RetargetMilliseconds", T("Re-check every", "Reavaliar a cada"), 16.0, 300.0, 4.0, 80.0, 1.0, 1, "{0} ms", T("How often the nearest enemy is re-evaluated while turning. Lower switches faster.", "De quanto em quanto tempo o inimigo mais próximo é reavaliado durante o giro. Menos troca mais depressa."))
EndFunction

Function PageBody()
	AddHeaderOption(T("Combos and clicking", "Combos e cliques"))
	AddEmptyOption()
	AddToggleRow("TapAssist", T("Turn when attacking", "Virar ao atacar"), 1.0, T("The body turns toward the nearest enemy when you press an attack button (spells included).", "O corpo vira para o inimigo mais próximo quando carrega num botão de ataque (magia incluída)."))
	AddSliderRow("HoldMilliseconds", T("Combat window", "Janela de combate"), 100.0, 10000.0, 100.0, 3000.0, 0.001, 1, "{1} s", T("After an attack or block, the body keeps following the nearest enemy for this long (no click needed). Every new attack or block restarts the timer without changing the target. Default 3 seconds.", "Depois de um ataque ou bloqueio, o corpo continua a seguir o inimigo mais próximo por este tempo (sem precisar clicar). Cada novo ataque ou bloqueio reinicia o temporizador sem mudar o alvo. Padrão: 3 segundos."))
	AddMenuRow("HoldFaceButton", T("Keep facing while holding", "Encarar enquanto segura"), 3, 3.0, T("While you hold this button (block), the body keeps following the nearest enemy.", "Enquanto segura este botão (bloqueio), o corpo continua a seguir o inimigo mais próximo."))
	AddEmptyOption()
	AddHeaderOption(T("Turning", "Giro"))
	AddEmptyOption()
	AddSliderRow("TurnSpeedDegPerSec", T("Max turn speed", "Velocidade máxima do giro"), 90.0, 1800.0, 10.0, 900.0, 1.0, 0, "{0} deg/s", T("Maximum turn speed in degrees per second. Lower is slower.", "Velocidade máxima do giro, em graus por segundo. Menos é mais lento."))
	AddSliderRow("Smoothing", T("Smoothing", "Suavização"), 0.0, 30.0, 0.5, 0.0, 1.0, 0, "{1}", T("0 = constant speed. Higher values slow down smoothly when arriving at the target. Try 8 to 15.", "0 = velocidade constante. Valores maiores abrandam suavemente ao chegar ao alvo. Experimente 8 a 15."))
	AddSliderRow("UpdateMilliseconds", T("Update interval", "Intervalo de atualização"), 1.0, 30.0, 1.0, 8.0, 1.0, 1, "{0} ms", T("How often the turn is updated. Lower is smoother.", "De quanto em quanto tempo o giro é atualizado. Menos é mais fluido."))
	AddEmptyOption()
	int prof = SoftTargetNative.GetInt("ProfileMode")
	if prof == 0
		AddHeaderOption(T("Normal profile", "Perfil normal"))
		AddEmptyOption()
		AddToggleRow("ReturnToHeading", T("Return to previous heading", "Voltar ao rumo anterior"), 0.0, T("After the combat window ends, the body turns slowly back to where it was facing. Off by default. Does not return while you hold the walk keys.", "Depois que a janela de combate termina, o corpo volta devagar para onde estava virado. Desligado por padrão. Não volta enquanto segura as teclas de andar."))
		AddSliderRow("ReturnSpeedDegPerSec", T("Return max speed", "Velocidade máxima do regresso"), 90.0, 1000.0, 10.0, 360.0, 1.0, 0, "{0} deg/s", T("Upper limit of the return speed, in degrees per second.", "Limite máximo da velocidade do regresso, em graus por segundo."))
		AddSliderRow("ReturnSmoothing", T("Return smoothing", "Suavização do regresso"), 0.5, 15.0, 0.5, 3.0, 1.0, 0, "{1}", T("Lower is slower and softer. Default 3.", "Menos é mais lento e suave. Padrão: 3."))
		AddEmptyOption()
	endIf
EndFunction

Function PageCamera()
	int prof = SoftTargetNative.GetInt("ProfileMode")
	if prof == 1
		AddHeaderOption(T("FreeCam / Coeus profile (directional movement)", "Perfil FreeCam / Coeus (movimento direcional)"))
		AddEmptyOption()
		AddToggleRow("MoveRemap", T("Face the target, move by screen", "Encarar o alvo, andar pelo ecrã"), 1.0, T("The body keeps facing the target while you walk and your keys stay relative to the SCREEN (W = up on the screen, A = left, D = right, S = down). Keyboard only. The plugin finds the game's movement vector by itself the first time you walk (see the log). Off = Coeus turns the body to where you walk.", "O corpo continua virado para o alvo enquanto anda e as teclas continuam relativas ao ECRÃ (W = para cima no ecrã, A = esquerda, D = direita, S = para baixo). Só teclado. O plugin descobre sozinho o vetor de movimento do jogo na primeira vez que andar (veja o registo). Desligado = o Coeus vira o corpo para onde anda."))
		AddToggleRow("SprintKeepsAim", T("Sprint keeps the aim", "Correr mantém a mira"), 1.0, T("Sprinting does not release the SoftTarget or the lock-on (sprint attacks). Off = sprinting ends the SoftTarget.", "Correr não larga o SoftTarget nem o lock-on (ataques em corrida). Desligado = correr termina o SoftTarget."))
		AddToggleRow("CameraHoldTdm", T("Keep the camera still when the body turns", "Câmera parada quando o corpo vira"), 1.0, T("When this mod turns the body toward an enemy, the camera stays where you put it and the mouse keeps moving only the camera (free camera style). Off = the camera follows the turning body.", "Quando este mod vira o corpo para um inimigo, a câmera fica onde a pôs e o rato continua a mexer só a câmera (câmera livre). Desligado = a câmera acompanha o corpo que vira."))
		AddSliderRow("AttackPulseMs", T("Turn while walking", "Virar enquanto anda"), 0.0, 1500.0, 50.0, 350.0, 0.001, 1, "{2} s", T("Only when 'Face the target, move by screen' is off (or not working): each attack turns the walking body toward the enemy for this long. 0 = never.", "Só quando 'Encarar o alvo, andar pelo ecrã' está desligado (ou não funciona): cada ataque vira o corpo em movimento para o inimigo durante este tempo. 0 = nunca."))
	else
		AddHeaderOption(T("Normal profile", "Perfil normal"))
		AddEmptyOption()
		AddStatusRow(T("Camera options", "Opções da câmera"), T("see Lock-On page", "veja a página Lock-On"), T("The camera options for the lock-on are on the Lock-On page. Outside the lock-on the camera is never touched.", "As opções da câmera do lock-on estão na página Lock-On. Fora do lock-on a câmera nunca é tocada."))
	endIf
EndFunction

Function PageLock()
	AddHeaderOption(T("Lock-On (replaces Simple Lock-On)", "Lock-On (substitui o Simple Lock-On)"))
	AddEmptyOption()
	AddToggleRow("LockEnabled", T("Lock-on enabled", "Lock-on ligado"), 1.0, T("Press the lock key to lock the enemy under the crosshair. Works in and out of combat. Do not use together with LockOn.dll / Simple Lock-On: remove that mod.", "Carregue na tecla do lock para prender o inimigo que está na mira. Funciona dentro e fora do combate. Não use junto com a LockOn.dll / Simple Lock-On: retire esse mod."))
	AddEmptyOption()
	AddKeyRow("LockKey", T("Lock on/off key", "Tecla de ligar/desligar"), T("Default F. Mouse buttons work too (pick one in the dialog).", "Padrão: F. Botões do rato também servem (escolha no diálogo)."))
	AddKeyRow("LockSwitchKey", T("Switch-target key", "Tecla de trocar de alvo"), T("Default M3 (middle mouse button). Switches to the enemy closest to the center of the screen.", "Padrão: M3 (botão do meio do rato). Troca para o inimigo mais perto do centro do ecrã."))
	AddHeaderOption(T("Choosing the target", "Escolha do alvo"))
	AddEmptyOption()
	AddSliderRow("LockRange", T("Range", "Alcance"), 500.0, 8000.0, 100.0, 4000.0, 1.0, 0, "{0}", T("How far away a target can be locked and followed. 70 is about 1 meter.", "A que distância um alvo pode ser preso e seguido. 70 é cerca de 1 metro."))
	AddSliderRow("LockAcquireDegrees", T("Aim area", "Área da mira"), 5.0, 90.0, 1.0, 30.0, 1.0, 0, "{0} deg", T("How far from the crosshair, in degrees, any part of the enemy's body (head, legs, wings, tail) may be and still be locked. The one closest to the crosshair wins.", "A que distância da mira, em graus, qualquer parte do corpo do inimigo (cabeça, pernas, asas, cauda) pode estar para ser presa. Ganha a que estiver mais perto da mira."))
	AddToggleRow("LockHostileOnly", T("Only enemies", "Só inimigos"), 1.0, T("If off, any living creature or person can be locked (for example animals when hunting).", "Se desligado, qualquer criatura ou pessoa viva pode ser presa (por exemplo animais na caça)."))
	AddToggleRow("LockAutoNext", T("Next enemy when target dies", "Próximo inimigo quando o alvo morre"), 1.0, T("When the target dies the lock goes to the nearest enemy instead of turning off.", "Quando o alvo morre o lock passa para o inimigo mais próximo em vez de desligar."))
	AddSliderRow("LockAutoCircleRange", T("Automatic choice: all-around range", "Escolha automática: alcance em volta"), 200.0, 4000.0, 100.0, 900.0, 1.0, 0, "{0}", T("When the lock picks an enemy by itself, anyone within this distance counts, in any direction (even behind you).", "Quando o lock escolhe um inimigo sozinho, quem estiver dentro desta distância conta, em qualquer direção (até atrás de si)."))
	AddSliderRow("LockAutoConeDegrees", T("Automatic choice: forward cone", "Escolha automática: cone à frente"), 10.0, 180.0, 5.0, 60.0, 1.0, 0, "{0} deg", T("Beyond the all-around range (up to the lock range) only enemies inside this cone in front of the camera count. The angle is measured to each side of the screen center.", "Além do alcance em volta (até ao alcance do lock) só contam inimigos dentro deste cone à frente da câmera. O ângulo é medido para cada lado do centro do ecrã."))
	AddHeaderOption(T("Losing the target", "Perder o alvo"))
	AddEmptyOption()
	AddSliderRow("LockLoseSightMs", T("Time out of view", "Tempo fora da vista"), 0.0, 5000.0, 100.0, 1500.0, 0.001, 1, "{1} s", T("The lock turns off if the target stays out of view for this long.", "O lock desliga se o alvo ficar fora da vista durante este tempo."))
	AddSliderRow("LockLoseAngleDegrees", T("Out-of-view angle", "Ângulo de fora da vista"), 30.0, 170.0, 5.0, 75.0, 1.0, 0, "{0} deg", T("The target counts as out of view when its whole body is further than this from the center of the screen. Walls and rocks in between are not detected yet.", "O alvo conta como fora da vista quando o corpo inteiro está mais longe do que isto do centro do ecrã. Paredes e rochas pelo meio ainda não são detetadas."))
	AddHeaderOption(T("Camera", "Câmera"))
	AddEmptyOption()
	AddToggleRow("LockCameraFollow", T("Camera follows the target", "Câmera acompanha o alvo"), 1.0, T("Normal profile only: with the lock on, the camera turns to the target. In the FreeCam / Coeus profile the camera is always yours.", "Só no perfil normal: com o lock ligado, a câmera vira para o alvo. No perfil FreeCam / Coeus a câmera é sempre sua."))
	AddToggleRow("LockCameraPitch", T("Camera also looks up/down", "Câmera também sobe/desce"), 1.0, T("The camera tilts up or down toward the target (dragons, high ground). Turn off if the camera behaves strangely.", "A câmera inclina para cima ou para baixo para o alvo (dragões, terreno alto). Desligue se a câmera se portar de forma estranha."))
	AddSliderRow("AimOffsetX", T("Aim point: left / right", "Ponto de mira: esquerda / direita"), -20.0, 20.0, 0.5, 0.0, 1.0, 0, "{1} deg", T("Normal profile. For mods that move the aim point (AIM FIX, camera offset): where the locked target sits relative to the crosshair. Positive = to the right of the centre. If the crosshair is left of the target, raise it.", "Perfil normal. Para mods que mudam o ponto de mira (AIM FIX, câmera deslocada): onde o alvo preso fica em relação à mira. Positivo = à direita do centro. Se a mira ficar à esquerda do alvo, aumente."))
	AddSliderRow("AimOffsetY", T("Aim point: up / down", "Ponto de mira: cima / baixo"), -20.0, 20.0, 0.5, 0.0, 1.0, 0, "{1} deg", T("Normal profile. Positive = the target sits above the centre of the screen.", "Perfil normal. Positivo = o alvo fica acima do centro do ecrã."))
	AddSliderRow("LockCameraSmooth", T("Camera speed", "Velocidade da câmera"), 2.0, 20.0, 0.5, 8.0, 1.0, 0, "{1}", T("How fast the camera turns to the target. Higher is faster and less smooth.", "A velocidade com que a câmera vira para o alvo. Mais é mais rápido e menos suave."))
	AddSliderRow("AimFilterMs", T("Camera stability", "Estabilidade da câmera"), 0.0, 1000.0, 50.0, 350.0, 0.001, 1, "{2} s", T("Averages the aim point over this time so the target's weapon swings and arm movements do not shake the camera. 0 = off. Higher is steadier but slower to follow.", "Faz a média do ponto de mira durante este tempo, para os golpes e movimentos de braço do alvo não abanarem a câmera. 0 = desligado. Mais é mais estável mas segue mais devagar."))
	AddSliderRow("CameraDeadZoneDegrees", T("Camera dead zone", "Zona morta da câmera"), 0.0, 8.0, 0.5, 1.5, 1.0, 0, "{1} deg", T("The camera ignores aiming errors smaller than this. 0 = off.", "A câmera ignora erros de mira menores do que isto. 0 = desligado."))
	AddHeaderOption(T("Aim point height", "Altura do ponto de mira"))
	AddEmptyOption()
	AddSliderRow("AimHeightAdjust", T("Height adjust", "Ajuste de altura"), -60.0, 60.0, 1.0, 0.0, 1.0, 0, "{0}", T("Humans are aimed at the chest, other creatures at the middle of the body. This moves the point up (+) or down (-), in game units. In combat you can also change it with Shift + mouse wheel (or Page Up / Page Down).", "Os humanos são mirados no peito, as outras criaturas no meio do corpo. Isto sobe (+) ou desce (-) o ponto, em unidades do jogo. Em combate também pode mudar com Shift + roda do rato (ou Page Up / Page Down)."))
	AddToggleRow("AimAdjustWheel", T("Shift + wheel adjusts it", "Shift + roda ajusta"), 1.0, T("In combat, Shift + mouse wheel changes the aim point height instead of zooming. Turn off if the wheel still zooms; Page Up / Page Down keep working.", "Em combate, Shift + roda do rato muda a altura do ponto de mira em vez de dar zoom. Desligue se a roda ainda der zoom; Page Up / Page Down continuam a funcionar."))
	AddHeaderOption(T("Shot prediction (only with lock-on)", "Previsão do disparo (só com lock-on)"))
	AddEmptyOption()
	AddToggleRow("PredictEnabled", T("Predict the shot", "Prever o disparo"), 1.0, T("While you draw a bow / crossbow or hold a spell button with the lock on, the aim leads a moving target. Without lock-on the shot is the normal one.", "Enquanto puxa o arco / besta ou segura o botão de uma magia com o lock ligado, a mira antecipa o alvo em movimento. Sem lock-on o disparo é o normal."))
	AddSliderRow("PredictStrength", T("Prediction strength", "Força da previsão"), 0.0, 150.0, 5.0, 100.0, 1.0, 0, "{0} %", T("How much of the computed lead is applied. Lower if shots go too far ahead of the target, raise if they fall behind.", "Quanto da antecipação calculada é aplicada. Baixe se os tiros vão muito à frente do alvo, suba se ficam atrás."))
	AddSliderRow("ProjectileSpeed", T("Projectile speed", "Velocidade do projétil"), 1000.0, 12000.0, 100.0, 4500.0, 1.0, 0, "{0}", T("Arrow speed of bows and crossbows, units per second. Spells use the speed in the game data (or SpellProjectileSpeed in the ini).", "Velocidade da flecha de arcos e bestas, em unidades por segundo. As magias usam a velocidade dos dados do jogo (ou SpellProjectileSpeed no ini)."))
	AddSliderRow("ProjectileSpeedScale", T("Projectile speed fine tuning", "Ajuste fino da velocidade"), 20.0, 300.0, 5.0, 100.0, 1.0, 0, "{0} %", T("Shots land BEHIND a moving target: lower this. Shots land IN FRONT: raise it. Applies to arrows and spells.", "Os tiros caem ATRÁS do alvo em movimento: baixe. Caem À FRENTE: suba. Vale para flechas e magias."))
	AddSliderRow("ProjectileGravity", T("Projectile drop", "Queda do projétil"), 0.0, 2000.0, 50.0, 470.0, 1.0, 0, "{0}", T("Bow and crossbow only: raises the aim to compensate for the arrow drop. 0 = no compensation.", "Só arco e besta: levanta a mira para compensar a queda da flecha. 0 = sem compensação."))
	AddSliderRow("PredictMinDistance", T("Minimum distance", "Distância mínima"), 100.0, 2000.0, 50.0, 150.0, 1.0, 0, "{0}", T("Closer than this there is no prediction.", "Mais perto do que isto não há previsão."))
EndFunction

bool Function HasBit(int mask, int bit)
	if bit == 1
		return (mask % 2) == 1
	endIf
	return ((mask / 2) % 2) == 1
EndFunction

string Function FoundText(int mask, int bit)
	if HasBit(mask, bit)
		return T("found", "encontrado")
	endIf
	return T("not found", "não encontrado")
EndFunction

Function PageDot()
	AddHeaderOption(T("On-screen indicator", "Indicador no ecrã"))
	AddEmptyOption()
	AddToggleRow("DotLock", T("Show on the locked target", "Mostrar no alvo do lock-on"), 1.0, T("Draws the indicator on the enemy you have locked.", "Desenha o indicador no inimigo que prendeu."))
	AddToggleRow("DotSoftTarget", T("Show on the SoftTarget enemy", "Mostrar no inimigo do SoftTarget"), 0.0, T("Also draws the indicator on the enemy the body is turning toward while you fight.", "Desenha também o indicador no inimigo para onde o corpo está a virar enquanto luta."))
	int found = SoftTargetNative.Status(4)
	AddStatusRow(T("Simple Lock-On reticle file", "Ficheiro da retícula do Simple Lock-On"), FoundText(found, 1), T("Looks for LockOnReticle.swf or LockOnMenu.swf in Data\\Interface.", "Procura LockOnReticle.swf ou LockOnMenu.swf em Data\\Interface."))
	AddStatusRow(T("Coeus-style reticle files", "Ficheiros da retícula estilo Coeus"), FoundText(found, 2), T("Looks for TDM_Widgets.swf and TDM_Assets0.swf in Data\\Interface.", "Procura TDM_Widgets.swf e TDM_Assets0.swf em Data\\Interface."))
	AddMenuRow("DotSkin", T("Appearance", "Aspeto"), 8, 1.0, T("Dot and Ring-and-dot are drawn by the plugin (no files). The Simple Lock-On reticle needs LockOnReticle.swf (or LockOnMenu.swf) in Data\\Interface. The Coeus-style reticle needs TDM_Widgets.swf and TDM_Assets0.swf in Data\\Interface (or Data\\Interface\\TrueDirectionalMovement); the retexture variants of that widget change its look. If a file is missing the plain dot is used and the log says so.", "Ponto e Anel e ponto são desenhados pelo plugin (sem ficheiros). A retícula do Simple Lock-On precisa do LockOnReticle.swf (ou LockOnMenu.swf) em Data\\Interface. A retícula estilo Coeus precisa do TDM_Widgets.swf e TDM_Assets0.swf em Data\\Interface (ou Data\\Interface\\TrueDirectionalMovement); as variantes de retextura desse widget mudam o aspeto. Se faltar um ficheiro usa-se o ponto simples e o registo diz isso."))
	AddMenuRow("DotColor", T("Color (drawn skins)", "Cor (aspetos desenhados)"), 9, 0.0, T("Color of the dot and ring.", "Cor do ponto e do anel."))
	AddSliderRow("DotSize", T("Size", "Tamanho"), 30.0, 400.0, 10.0, 100.0, 1.0, 0, "{0} %", T("Size of the indicator.", "Tamanho do indicador."))
	AddSliderRow("DotOpacity", T("Opacity", "Opacidade"), 10.0, 100.0, 5.0, 100.0, 1.0, 0, "{0} %", T("How solid the indicator is.", "Quão opaco é o indicador."))
	AddSliderRow("DotTdmType", T("Coeus-style reticle type", "Tipo da retícula estilo Coeus"), 0.0, 2.0, 1.0, 0.0, 1.0, 1, "{0}", T("Reticle type passed to the reticle widget (0, 1 or 2). Only used with the Coeus-style reticle.", "Tipo de retícula passado ao widget da retícula (0, 1 ou 2). Só é usado com a retícula estilo Coeus."))
EndFunction

Function PageMagic()
	AddHeaderOption(T("Vertical aim (spells)", "Mira vertical (magia)"))
	AddEmptyOption()
	AddMenuRow("VerticalMode", T("Vertical aim", "Mira vertical"), 5, 0.0, T("Tilts the body (arms) so attacks and spells hit enemies above or below you. The camera is never touched. After the combat window the body returns to level.", "Inclina o corpo (os braços) para os ataques e magias acertarem em inimigos acima ou abaixo de si. A câmera nunca é tocada. Depois da janela de combate o corpo volta ao nível."))
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
			SoftTargetNative.SaveProfile()
			SetTextOptionValue(a_option, T("[Saved]", "[Guardado]"))
		elseIf okey[i] == "@load"
			if SoftTargetNative.LoadProfile() == 1
				ForcePageReset()
			else
				ShowMessage(T("No saved profile found. Use 'Save my profile' first.", "Nenhum perfil guardado. Use primeiro 'Guardar o meu perfil'."), false)
			endIf
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
	if okey[i] == "DotSkin"
		int have = SoftTargetNative.Status(4)
		if (a_index == 2 && !HasBit(have, 1)) || (a_index == 3 && !HasBit(have, 2))
			ShowMessage(T("Those files were not found in Data\\Interface, so this appearance stays unavailable. Keeping the previous one.", "Esses ficheiros não foram encontrados em Data\\Interface, por isso este aspeto fica indisponível. Mantém-se o anterior."), false)
			return
		endIf
	endIf
	SoftTargetNative.SetInt(okey[i], a_index)
	if okey[i] == "Language"
		lang = a_index
		ForcePageReset()
	elseIf okey[i] == "ProfileMode"
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
