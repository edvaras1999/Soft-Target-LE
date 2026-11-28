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
int npos		; position in the page (to keep the two columns aligned)
int lang		; 0 = English, 1 = Portugues

int Function GetVersion()
	return 22
EndFunction

Function InitTables()
	okey = new string[64]
	oid = new int[64]
	okind = new int[64]
	oisint = new int[64]
	omenu = new int[64]
	omin = new float[64]
	omax = new float[64]
	ostep = new float[64]
	odef = new float[64]
	oscale = new float[64]
	ofmt = new string[64]
	oinfo = new string[64]
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
	Pages[1] = T("Lock-On", "Lock-On")
	Pages[2] = T("Targeting and combos", "Alvo e combos")
	Pages[3] = T("Ranged (magic, bow)", "À distância (magia, arco)")
	Pages[4] = T("Environment (RaySense)", "Ambiente (RaySense)")
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
	elseIf menuId == 11
		r = new string[3]
		r[0] = T("Off", "Desligado")
		r[1] = T("Always", "Sempre")
		r[2] = T("Only in combat", "Só em combate")
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
	elseIf menuId == 10
		r = new string[3]
		r[0] = T("Off (older behaviour)", "Desligado (comportamento antigo)")
		r[1] = T("Soft (free mouse, lock takes over when you attack)", "Suave (rato livre, o lock toma o controlo ao atacar)")
		r[2] = T("Hard (view stays on the target, mouse blocked)", "Duro (vista presa ao alvo, rato bloqueado)")
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

; start a section: pad to the left column, then a header (left) and an empty cell (right)
Function Sec(string title)
	if (npos % 2) == 1
		AddEmptyOption()
		npos += 1
	endIf
	AddHeaderOption(title)
	AddEmptyOption()
	npos += 2
EndFunction

Function Pad()
	AddEmptyOption()
	npos += 1
EndFunction

; options of one profile are only shown when that profile can be in use: with the automatic profile both are
bool Function ProfNormal()
	return SoftTargetNative.GetInt("ProfileAuto") == 1 || SoftTargetNative.GetInt("ProfileMode") != 1
EndFunction

bool Function ProfFree()
	return SoftTargetNative.GetInt("ProfileAuto") == 1 || SoftTargetNative.GetInt("ProfileMode") == 1
EndFunction

; ---- helpers to add one row
Function AddToggleRow(string key, string label, float def, string info)
	if ocount >= 64
		return
	endIf
	int i = ocount
	ocount += 1
	npos += 1
	okey[i] = key
	okind[i] = 1
	odef[i] = def
	oinfo[i] = info
	oid[i] = AddToggleOption(label, SoftTargetNative.GetBool(key))
EndFunction

Function AddSliderRow(string key, string label, float minV, float maxV, float stepV, float def, float scale, int isInt, string fmt, string info)
	if ocount >= 64
		return
	endIf
	int i = ocount
	ocount += 1
	npos += 1
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
	if ocount >= 64
		return
	endIf
	int i = ocount
	ocount += 1
	npos += 1
	okey[i] = key
	okind[i] = 3
	omenu[i] = menuId
	odef[i] = def
	oinfo[i] = info
	oid[i] = AddMenuOption(label, ChoiceText(menuId, SoftTargetNative.GetInt(key)))
EndFunction

Function AddKeyRow(string key, string label, string info)
	if ocount >= 64
		return
	endIf
	int i = ocount
	ocount += 1
	npos += 1
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

	npos = 0
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
		PageTargeting()
	elseIf p == 3
		PageRanged()
	else
		PageEnv()
	endIf
EndEvent

string Function ProfileName(int v)
	if v == 2
		return T("FreeCam / Coeus (Unsheathed on)", "FreeCam / Coeus (Unsheathed ligado)")
	endIf
	return T("Normal (vanilla movement)", "Normal (movimento do jogo)")
EndFunction

Function AddButtonRow(string key, string label, string btn, string info)
	if ocount >= 64
		return
	endIf
	int i = ocount
	ocount += 1
	npos += 1
	okey[i] = key
	okind[i] = 5
	oinfo[i] = info
	oid[i] = AddTextOption(label, btn)
EndFunction

Function AddStatusRow(string label, string value, string info)
	if ocount >= 64
		return
	endIf
	int i = ocount
	ocount += 1
	npos += 1
	okey[i] = ""
	okind[i] = 0
	oinfo[i] = info
	oid[i] = AddTextOption(label, value, OPTION_FLAG_DISABLED)
EndFunction

string Function LearnText(int s)
	if s == 1
		return T("waiting for W/A/S/D", "à espera de W/A/S/D")
	elseIf s == 2
		return T("learning...", "a aprender...")
	elseIf s == 3
		return T("found (offset ", "encontrado (offset ") + SoftTargetNative.Status(6) + ")"
	elseIf s == 4
		return T("failed - try again", "falhou - tente de novo")
	endIf
	return T("not learned", "não aprendido")
EndFunction

string Function YesNo(int v)
	if v != 0
		return T("yes", "sim")
	endIf
	return T("no", "não")
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

Function PageGeneral()
	Sec(T("SoftTarget LE", "SoftTarget LE"))
	AddToggleRow("Enabled", T("Enabled", "Ativado"), 1.0, T("Turns the whole mod on or off.", "Liga ou desliga o mod inteiro."))
	AddMenuRow("Language", T("Menu language", "Idioma do menu"), 1, 0.0, T("Language of this menu. Page names update the next time you open the menu.", "Idioma deste menu. Os nomes das páginas mudam na próxima vez que abrir o menu."))
	Sec(T("Profile", "Perfil"))
	AddToggleRow("ProfileAuto", T("Automatic profile", "Perfil automático"), 1.0, T("On: weapon or spell in hand = FreeCam / Coeus profile (directional movement), sheathed = Normal profile. Off: you choose the profile below.", "Ligado: arma ou magia na mão = perfil FreeCam / Coeus (movimento direcional), guardada = perfil Normal. Desligado: escolhes o perfil abaixo."))
	if SoftTargetNative.GetInt("ProfileAuto") == 0
		AddMenuRow("ProfileMode", T("Profile", "Perfil"), 7, 1.0, T("Normal: no directional movement (Coeus off, or Coeus with EnableUnsheathed=0, so weapons-drawn movement is vanilla). FreeCam / Coeus: Coeus directional movement is active with weapons drawn (EnableUnsheathed=1). Pick the one that matches your Coeus setting.", "Normal: sem movimento direcional (Coeus desligado, ou Coeus com EnableUnsheathed=0, então com armas na mão o movimento é o do jogo). FreeCam / Coeus: o movimento direcional do Coeus está ativo com armas na mão (EnableUnsheathed=1). Escolha o que corresponde à sua configuração do Coeus."))
	endIf
	AddStatusRow(T("Active profile", "Perfil ativo"), ProfileName(SoftTargetNative.Status(0)), T("The profile in use right now (with the automatic profile it changes when you draw or sheathe).", "O perfil em uso agora (com o perfil automático muda ao sacar ou guardar a arma)."))
	AddStatusRow(T("Lock-on active", "Lock-on ativo"), YesNo(SoftTargetNative.Status(3)), T("Whether the lock-on (this mod's or LockOn.dll) is on right now.", "Se o lock-on (deste mod ou da LockOn.dll) está ligado agora."))
	AddStatusRow(T("Coeus found", "Coeus encontrado"), YesNo(SoftTargetNative.Status(2)), "")
	AddStatusRow(T("LockOn.dll found", "LockOn.dll encontrada"), YesNo(SoftTargetNative.Status(1)), "")
	Sec(T("Movement learning (off until you ask)", "Aprender o movimento (só quando pedires)"))
	AddButtonRow("@learn", T("Learn movement keys", "Aprender as teclas de movimento"), T("[Start]", "[Iniciar]"), T("Close the menu, then press W, A, S, D one at a time (hold each 1-2 s, repeat for about 15 s). Same as Shift + F10. Needed for the FreeCam movement and the edge guard. Run it again if the movement is wrong.", "Feche o menu e carregue W, A, S, D um de cada vez (segure cada um 1-2 s, repita durante cerca de 15 s). O mesmo que Shift + F10. Necessário para o movimento FreeCam e a guarda da borda. Repita se o movimento ficar errado."))
	AddStatusRow(T("Learning status", "Estado da aprendizagem"), LearnText(SoftTargetNative.Status(5)), T("Reopen the menu to refresh. The result is saved in the ini.", "Reabra o menu para atualizar. O resultado fica guardado no ini."))
	Sec(T("Animation mods", "Mods de animação"))
	AddMenuRow("KeytraceMode", T("Keytrace mask", "Máscara Keytrace"), 11, 2.0, T("For DAR/OAR packs that test HasMagicEffect on Keytrace.esp. Off, always, or only in combat (weapon drawn with the lock on or SoftTarget active). Going from Off to on needs a game restart.", "Para pacotes DAR/OAR que testam HasMagicEffect do Keytrace.esp. Desligada, sempre, ou só em combate (arma sacada com o lock ligado ou o SoftTarget ativo). Passar de Desligada a ligada precisa de reiniciar o jogo."))
	AddMenuRow("DmkMode", T("DMK animation variables", "Variáveis de animação do DMK"), 11, 0.0, T("For animation packs made for Directional Movement Keys. Off, always, or only in combat. Needs SoftTarget_BDI.json in SKSE/Plugins/BehaviorDataInjector. Do not use together with another DMK.", "Para pacotes de animação feitos para o Directional Movement Keys. Desligadas, sempre, ou só em combate. Precisa do SoftTarget_BDI.json em SKSE/Plugins/BehaviorDataInjector. Não use junto com outro DMK."))
	Sec(T("Settings file", "Ficheiro de configuração"))
	AddButtonRow("@save", T("Save my profile", "Guardar o meu perfil"), T("[Save]", "[Guardar]"), T("Saves all current options as your own profile (SoftTargetLE_User.ini). Every change is already written to SoftTargetLE.ini as you make it.", "Guarda todas as opções atuais como o seu perfil (SoftTargetLE_User.ini). Cada mudança já é gravada no SoftTargetLE.ini na hora."))
	AddButtonRow("@load", T("Load my profile", "Carregar o meu perfil"), T("[Load]", "[Carregar]"), T("Replaces all current options with the ones saved in your profile.", "Substitui todas as opções atuais pelas que guardou no seu perfil."))
	AddButtonRow("@reset", T("Reset to defaults", "Repor valores de fábrica"), T("[Reset]", "[Repor]"), T("Restores all options to the factory values (the language is kept).", "Repõe todas as opções para os valores de fábrica (o idioma é mantido)."))
	Sec(T("Diagnostics", "Diagnóstico"))
	AddToggleRow("LogOnly", T("Log only (do not turn)", "Só registar (não virar)"), 0.0, T("Only writes the chosen target to the log, without turning the character.", "Só escreve o alvo escolhido no registo, sem virar o personagem."))
	AddToggleRow("Probe", T("Detailed log", "Registo detalhado"), 0.0, T("Writes camera and enemy data to SoftTargetLE.log. Makes the log very large; keep it off normally.", "Escreve dados da câmera e dos inimigos no SoftTargetLE.log. Deixa o registo muito grande; normalmente deve ficar desligado."))
EndFunction

Function PageLock()
	Sec(T("Lock-On (replaces Simple Lock-On)", "Lock-On (substitui o Simple Lock-On)"))
	AddToggleRow("LockEnabled", T("Lock-on enabled", "Lock-on ligado"), 1.0, T("Press the lock key to lock the enemy under the crosshair. Works in and out of combat. Do not use together with LockOn.dll / Simple Lock-On: remove that mod.", "Carregue na tecla do lock para prender o inimigo que está na mira. Funciona dentro e fora do combate. Não use junto com a LockOn.dll / Simple Lock-On: retire esse mod."))
	Pad()
	AddKeyRow("LockKey", T("Lock on/off key", "Tecla de ligar/desligar"), T("Default F. Mouse buttons work too (pick one in the dialog).", "Padrão: F. Botões do rato também servem (escolha no diálogo)."))
	AddKeyRow("LockSwitchKey", T("Switch-target key", "Tecla de trocar de alvo"), T("Default M3 (middle mouse button). Switches to the enemy closest to the center of the screen.", "Padrão: M3 (botão do meio do rato). Troca para o inimigo mais perto do centro do ecrã."))
	Sec(T("Choosing the target", "Escolha do alvo"))
	AddSliderRow("LockRange", T("Range", "Alcance"), 500.0, 8000.0, 100.0, 2000.0, 1.0, 0, "{0}", T("How far away a target can be locked and followed. 70 is about 1 meter.", "A que distância um alvo pode ser preso e seguido. 70 é cerca de 1 metro."))
	AddSliderRow("LockAcquireDegrees", T("Aim area", "Área da mira"), 5.0, 90.0, 1.0, 30.0, 1.0, 0, "{0} deg", T("How far from the crosshair, in degrees, any part of the enemy's body (head, legs, wings, tail) may be and still be locked. The one closest to the crosshair wins.", "A que distância da mira, em graus, qualquer parte do corpo do inimigo (cabeça, pernas, asas, cauda) pode estar para ser presa. Ganha a que estiver mais perto da mira."))
	AddToggleRow("LockHostileOnly", T("Only enemies", "Só inimigos"), 1.0, T("If off, any living creature or person can be locked (for example animals when hunting).", "Se desligado, qualquer criatura ou pessoa viva pode ser presa (por exemplo animais na caça)."))
	AddToggleRow("LockAutoNext", T("Next enemy when target dies", "Próximo inimigo quando o alvo morre"), 1.0, T("When the target dies the lock goes to the nearest enemy instead of turning off.", "Quando o alvo morre o lock passa para o inimigo mais próximo em vez de desligar."))
	if ProfNormal()
	Sec(T("Normal profile (weapon sheathed)", "Perfil normal (arma guardada)"))
	AddToggleRow("LockCameraFollow", T("Camera follows the target", "Câmera acompanha o alvo"), 0.0, T("Normal profile only: with the lock on, the camera turns to the target. In the FreeCam / Coeus profile the camera is always yours.", "Só no perfil normal: com o lock ligado, a câmera vira para o alvo. No perfil FreeCam / Coeus a câmera é sempre sua."))
	AddSliderRow("LockCameraSmooth", T("Camera speed", "Velocidade da câmera"), 2.0, 20.0, 0.5, 5.0, 1.0, 0, "{1}", T("How fast the camera turns to the target. Higher is faster and less smooth.", "A velocidade com que a câmera vira para o alvo. Mais é mais rápido e menos suave."))
	AddSliderRow("AimFilterMs", T("Camera stability", "Estabilidade da câmera"), 0.0, 1000.0, 50.0, 600.0, 0.001, 1, "{2} s", T("Averages the aim point over this time so the target's weapon swings and arm movements do not shake the camera. 0 = off. Higher is steadier but slower to follow.", "Faz a média do ponto de mira durante este tempo, para os golpes e movimentos de braço do alvo não abanarem a câmera. 0 = desligado. Mais é mais estável mas segue mais devagar."))
	endIf
	if ProfFree()
	Sec(T("FreeCam / Coeus profile (weapon in hand)", "Perfil FreeCam / Coeus (arma na mão)"))
	AddToggleRow("MoveRemap", T("Face the target, move by screen", "Encarar o alvo, andar pelo ecrã"), 1.0, T("The body keeps facing the target while you walk and your keys stay relative to the SCREEN (W = up on the screen, A = left, D = right, S = down). Keyboard only. The plugin finds the game's movement vector by itself the first time you walk (see the log). Off = Coeus turns the body to where you walk.", "O corpo continua virado para o alvo enquanto anda e as teclas continuam relativas ao ECRÃ (W = para cima no ecrã, A = esquerda, D = direita, S = para baixo). Só teclado. O plugin descobre sozinho o vetor de movimento do jogo na primeira vez que andar (veja o registo). Desligado = o Coeus vira o corpo para onde anda."))
	AddToggleRow("SprintKeepsAim", T("Sprint keeps the aim", "Correr mantém a mira"), 1.0, T("Sprinting does not release the SoftTarget or the lock-on (sprint attacks). Off = sprinting ends the SoftTarget.", "Correr não larga o SoftTarget nem o lock-on (ataques em corrida). Desligado = correr termina o SoftTarget."))
	AddToggleRow("CameraHoldTdm", T("Keep the camera still when the body turns", "Câmera parada quando o corpo vira"), 1.0, T("When this mod turns the body toward an enemy, the camera stays where you put it and the mouse keeps moving only the camera (free camera style). Off = the camera follows the turning body.", "Quando este mod vira o corpo para um inimigo, a câmera fica onde a pôs e o rato continua a mexer só a câmera (câmera livre). Desligado = a câmera acompanha o corpo que vira."))
	AddMenuRow("LockViewMode", T("View with the lock", "Vista com o lock"), 10, 2.0, T("First person and bow (FreeCam profile, lock on). Hard: the camera stays on the target and the mouse is blocked, you move freely and M1/M2 turn the body; no fight with the camera. Soft: the mouse is free, but when you attack the lock takes full control. Off: older behaviour.", "Primeira pessoa e arco (perfil FreeCam, com o lock ligado). Duro: a câmera fica no alvo e o rato fica bloqueado, anda livremente e M1/M2 viram o corpo; sem luta com a câmera. Suave: o rato é livre, mas ao atacar o lock toma o controlo total. Desligado: comportamento antigo."))
	AddKeyRow("LockFreeLookKey", T("Free look key", "Tecla de olhar livremente"), T("While you hold this key the view is free (the lock stays on); when you let go the view eases back to the target. Default Shift (it is also sprint). 0 = none.", "Enquanto segurar esta tecla a vista fica livre (o lock continua ligado); ao soltar a vista volta ao alvo com suavidade. Padrão: Shift (que também é correr). 0 = nenhuma."))
	endIf
	Sec(T("On-screen indicator", "Indicador no ecrã"))
	AddToggleRow("DotLock", T("Show on the locked target", "Mostrar no alvo do lock-on"), 1.0, T("Draws the indicator on the enemy you have locked.", "Desenha o indicador no inimigo que prendeu."))
	AddToggleRow("DotSoftTarget", T("Show on the SoftTarget enemy", "Mostrar no inimigo do SoftTarget"), 1.0, T("Also draws the indicator on the enemy the body is turning toward while you fight.", "Desenha também o indicador no inimigo para onde o corpo está a virar enquanto luta."))
	AddMenuRow("DotSkin", T("Appearance", "Aspeto"), 8, 3.0, T("Dot and Ring-and-dot are drawn by the plugin (no files). The Simple Lock-On reticle needs LockOnReticle.swf (or LockOnMenu.swf) in Data\\Interface. The Coeus-style reticle needs TDM_Widgets.swf and TDM_Assets0.swf in Data\\Interface (or Data\\Interface\\TrueDirectionalMovement); the retexture variants of that widget change its look. If a file is missing the plain dot is used and the log says so.", "Ponto e Anel e ponto são desenhados pelo plugin (sem ficheiros). A retícula do Simple Lock-On precisa do LockOnReticle.swf (ou LockOnMenu.swf) em Data\\Interface. A retícula estilo Coeus precisa do TDM_Widgets.swf e TDM_Assets0.swf em Data\\Interface (ou Data\\Interface\\TrueDirectionalMovement); as variantes de retextura desse widget mudam o aspeto. Se faltar um ficheiro usa-se o ponto simples e o registo diz isso."))
	AddMenuRow("DotColor", T("Color (drawn skins)", "Cor (aspetos desenhados)"), 9, 0.0, T("Color of the dot and ring.", "Cor do ponto e do anel."))
	AddSliderRow("DotSize", T("Size", "Tamanho"), 30.0, 400.0, 10.0, 100.0, 1.0, 0, "{0} %", T("Size of the indicator.", "Tamanho do indicador."))
	AddSliderRow("DotOpacity", T("Opacity", "Opacidade"), 10.0, 100.0, 5.0, 100.0, 1.0, 0, "{0} %", T("How solid the indicator is.", "Quão opaco é o indicador."))
EndFunction

Function PageTargeting()
	Sec(T("Detection area (the nearest enemy is chosen)", "Área de deteção (escolhe sempre o inimigo mais próximo)"))
	AddMenuRow("Shape", T("Shape", "Forma"), 2, 1.0, T("Cone in front of the player, or a full circle around (best with directional or 360 movement).", "Cone à frente do jogador, ou um círculo completo à volta (melhor com movimento direcional ou de 360 graus)."))
	AddSliderRow("Range", T("Range", "Alcance"), 100.0, 1500.0, 10.0, 450.0, 1.0, 0, "{0}", T("How far an enemy can be and still be picked. It is measured to the part of the body closest to you (head, legs, wings, tail), not to its center. 140 is about 2 meters.", "A que distância um inimigo ainda pode ser escolhido. Mede-se até à parte do corpo mais próxima de si (cabeça, pernas, asas, cauda), não até ao centro. 140 é cerca de 2 metros."))
	AddSliderRow("ConeDegrees", T("Cone angle", "Ângulo do cone"), 30.0, 360.0, 5.0, 140.0, 1.0, 0, "{0}", T("Total width of the cone, in degrees (cone shape only).", "Largura total do cone, em graus (só na forma de cone)."))
	AddSliderRow("MaxHeightDifference", T("Max height difference", "Diferença máxima de altura"), 100.0, 1500.0, 10.0, 600.0, 1.0, 0, "{0}", T("Enemies higher or lower than this are ignored (stairs, rocks, cliffs).", "Inimigos mais altos ou mais baixos que isto são ignorados (escadas, rochas, penhascos)."))
	Sec(T("Who counts as an enemy", "Quem conta como inimigo"))
	AddToggleRow("RequireCombat", T("Only enemies in combat", "Só inimigos em combate"), 1.0, T("If on, only enemies already fighting you are picked.", "Se ligado, só são escolhidos inimigos que já estão a lutar contra si."))
	AddToggleRow("UseHostileList", T("Use the game's hostile list", "Usar a lista de hostis do jogo"), 1.0, T("Also accepts anyone the game already lists as hostile. Helps with reanimated and summoned enemies.", "Aceita também quem o jogo já lista como hostil. Ajuda com inimigos reanimados e invocados."))
	AddToggleRow("AllowReanimated", T("Accept reanimated enemies", "Aceitar inimigos reanimados"), 1.0, T("Also accepts enemies in the game's 'reanimate' life state. Turn off if corpses get targeted.", "Aceita também inimigos no estado de vida 'reanimar' do jogo. Desligue se cadáveres forem escolhidos."))
	AddSliderRow("SwitchMarginPercent", T("Switch margin", "Margem de troca"), 0.0, 50.0, 1.0, 20.0, 1.0, 0, "{0} %", T("0 = the body always follows the nearest enemy. Higher values keep the current target until another is this much closer.", "0 = o corpo segue sempre o inimigo mais próximo. Valores maiores mantêm o alvo atual até outro estar tanto mais perto."))
	Sec(T("Where SoftTarget works", "Onde o SoftTarget atua"))
	AddToggleRow("NoSoftTargetFirstPerson", T("No SoftTarget in first person", "Sem SoftTarget em primeira pessoa"), 1.0, T("In first person the automatic turning is off and your aim is fully manual. The lock-on still works if you turn it on.", "Em primeira pessoa o giro automático fica desligado e a mira é totalmente manual. O lock-on continua a funcionar se o ligar."))
	AddToggleRow("NoSoftTargetBow", T("No SoftTarget with a bow or crossbow", "Sem SoftTarget com arco ou besta"), 1.0, T("With a bow or crossbow the automatic turning is off and your aim is manual (the game camera is used). The lock-on still works if you turn it on.", "Com arco ou besta o giro automático fica desligado e a mira é manual (usa-se a câmera do jogo). O lock-on continua a funcionar se o ligar."))
	Sec(T("Combos and clicking", "Combos e cliques"))
	AddToggleRow("TapAssist", T("Turn when attacking", "Virar ao atacar"), 1.0, T("The body turns toward the nearest enemy when you press an attack button (spells included).", "O corpo vira para o inimigo mais próximo quando carrega num botão de ataque (magia incluída)."))
	AddSliderRow("HoldMilliseconds", T("Combat window", "Janela de combate"), 100.0, 10000.0, 100.0, 3000.0, 0.001, 1, "{1} s", T("After an attack or block, the body keeps following the nearest enemy for this long (no click needed). Every new attack or block restarts the timer without changing the target. Default 3 seconds.", "Depois de um ataque ou bloqueio, o corpo continua a seguir o inimigo mais próximo por este tempo (sem precisar clicar). Cada novo ataque ou bloqueio reinicia o temporizador sem mudar o alvo. Padrão: 3 segundos."))
	AddMenuRow("HoldFaceButton", T("Keep facing while holding", "Encarar enquanto segura"), 3, 3.0, T("While you hold this button (block), the body keeps following the nearest enemy.", "Enquanto segura este botão (bloqueio), o corpo continua a seguir o inimigo mais próximo."))
	Sec(T("Turning", "Giro"))
	AddSliderRow("TurnSpeedDegPerSec", T("Max turn speed", "Velocidade máxima do giro"), 90.0, 1800.0, 10.0, 900.0, 1.0, 0, "{0} deg/s", T("Maximum turn speed in degrees per second. Lower is slower.", "Velocidade máxima do giro, em graus por segundo. Menos é mais lento."))
	AddSliderRow("Smoothing", T("Smoothing", "Suavização"), 0.0, 30.0, 0.5, 15.0, 1.0, 0, "{1}", T("0 = constant speed. Higher values slow down smoothly when arriving at the target. Try 8 to 15.", "0 = velocidade constante. Valores maiores abrandam suavemente ao chegar ao alvo. Experimente 8 a 15."))
	if ProfNormal()
	Sec(T("Normal profile", "Perfil normal"))
	AddToggleRow("ReturnToHeading", T("Return to previous heading", "Voltar ao rumo anterior"), 0.0, T("After the combat window ends, the body turns slowly back to where it was facing. Off by default. Does not return while you hold the walk keys.", "Depois que a janela de combate termina, o corpo volta devagar para onde estava virado. Desligado por padrão. Não volta enquanto segura as teclas de andar."))
	endIf
	if ProfFree()
	Sec(T("FreeCam / Coeus profile", "Perfil FreeCam / Coeus"))
	AddSliderRow("AttackPulseMs", T("Turn while walking", "Virar enquanto anda"), 0.0, 1500.0, 50.0, 350.0, 0.001, 1, "{2} s", T("Only when 'Face the target, move by screen' is off (or not working): each attack turns the walking body toward the enemy for this long. 0 = never.", "Só quando 'Encarar o alvo, andar pelo ecrã' está desligado (ou não funciona): cada ataque vira o corpo em movimento para o inimigo durante este tempo. 0 = nunca."))
	endIf
EndFunction

Function PageRanged()
	Sec(T("Vertical aim (spells and attacks)", "Mira vertical (magia e ataques)"))
	AddMenuRow("VerticalMode", T("Vertical aim", "Mira vertical"), 5, 1.0, T("Tilts the body (arms) so attacks and spells hit enemies above or below you. The camera is never touched. After the combat window the body returns to level.", "Inclina o corpo (os braços) para os ataques e magias acertarem em inimigos acima ou abaixo de si. A câmera nunca é tocada. Depois da janela de combate o corpo volta ao nível."))
	AddToggleRow("VerticalInvert", T("Invert tilt", "Inverter a inclinação"), 0.0, T("Turn on if spells go up when they should go down.", "Ligue se a magia sobe quando devia descer."))
	AddSliderRow("VerticalLimitDegrees", T("Max tilt", "Inclinação máxima"), 5.0, 80.0, 1.0, 45.0, 1.0, 0, "{0} deg", T("Maximum tilt, in degrees.", "Inclinação máxima, em graus."))
	Sec(T("Shot prediction (only with lock-on)", "Previsão do disparo (só com lock-on)"))
	AddToggleRow("PredictEnabled", T("Predict the shot", "Prever o disparo"), 1.0, T("While you draw a bow / crossbow or hold a spell button with the lock on, the aim leads a moving target. Without lock-on the shot is the normal one.", "Enquanto puxa o arco / besta ou segura o botão de uma magia com o lock ligado, a mira antecipa o alvo em movimento. Sem lock-on o disparo é o normal."))
	AddToggleRow("PredictAlways", T("Predict with the bow equipped", "Prever com o arco equipado"), 1.0, T("With the lock on and a bow equipped, the aim point already includes the lead before you draw, so the camera does not jump when you start drawing.", "Com o lock ligado e o arco equipado, o ponto de mira já inclui a antecipação antes de puxar, para a câmera não saltar ao começar a puxar."))
	AddToggleRow("LockTiltAlways", T("Head looks at the target (third person)", "Cabeça olha para o alvo (terceira pessoa)"), 1.0, T("In third person with the lock on, the body/head tilt follows the target with any weapon, not only with the bow.", "Em terceira pessoa com o lock ligado, a inclinação do corpo/cabeça segue o alvo com qualquer arma, não só com o arco."))
	AddSliderRow("PredictStrength", T("Prediction strength", "Força da previsão"), 0.0, 150.0, 5.0, 100.0, 1.0, 0, "{0} %", T("How much of the computed lead is applied. Lower if shots go too far ahead of the target, raise if they fall behind.", "Quanto da antecipação calculada é aplicada. Baixe se os tiros vão muito à frente do alvo, suba se ficam atrás."))
	AddSliderRow("ProjectileSpeed", T("Projectile speed", "Velocidade do projétil"), 1000.0, 12000.0, 100.0, 9000.0, 1.0, 0, "{0}", T("Arrow speed of bows and crossbows, units per second. Spells use the speed in the game data (or SpellProjectileSpeed in the ini).", "Velocidade da flecha de arcos e bestas, em unidades por segundo. As magias usam a velocidade dos dados do jogo (ou SpellProjectileSpeed no ini)."))
	AddSliderRow("ProjectileSpeedScale", T("Projectile speed fine tuning", "Ajuste fino da velocidade"), 20.0, 300.0, 5.0, 100.0, 1.0, 0, "{0} %", T("Shots land BEHIND a moving target: lower this. Shots land IN FRONT: raise it. Applies to arrows and spells.", "Os tiros caem ATRÁS do alvo em movimento: baixe. Caem À FRENTE: suba. Vale para flechas e magias."))
	AddSliderRow("ProjectileGravity", T("Projectile drop", "Queda do projétil"), 0.0, 2000.0, 50.0, 0.0, 1.0, 0, "{0}", T("Bow and crossbow only: raises the aim to compensate for the arrow drop. 0 = no compensation.", "Só arco e besta: levanta a mira para compensar a queda da flecha. 0 = sem compensação."))
	Sec(T("Bow and crossbow with the lock", "Arco e besta com o lock"))
	AddToggleRow("BowLockCamera", T("Bow: camera centers the target (FreeCam)", "Arco: câmera centra o alvo (FreeCam)"), 1.0, T("FreeCam / Coeus profile: while you draw a bow or crossbow with the lock on, the camera centers the target (from the real camera position, also with the shoulder view) so AIM Fix arrows land on it. Normal profile does this already.", "Perfil FreeCam / Coeus: enquanto puxa o arco ou a besta com o lock ligado, a câmera centra o alvo (a partir da posição real da câmera, também com a vista ao ombro) para as flechas do AIM Fix acertarem. O perfil normal já faz isto."))
	AddToggleRow("LockArrowTilt", T("Straighter arrows with the lock", "Flechas mais retas com o lock"), 1.0, T("While the lock is on with a bow the arrows fly straighter. The game values are restored when you let go.", "Com o lock ligado e o arco as flechas saem mais retas. Os valores do jogo são repostos ao largar."))
	AddSliderRow("BowTiltRate", T("Bow: body tilt speed limit", "Arco: limite da velocidade de inclinação"), 0, 360, 10, 60, 1.0, 0, "{0} deg/s", T("Limits how fast the body tilts with bow and lock (smooths the camera pull-back). 0 = no limit.", "Limita a rapidez com que o corpo inclina com arco e lock (suaviza o afastamento da câmera). 0 = sem limite."))
	Sec(T("Bow shoulder camera (off by default)", "Câmera ao ombro com o arco (desligada por omissão)"))
	AddToggleRow("BowShoulder", T("Shoulder view with the bow", "Vista ao ombro com o arco"), 0.0, T("While you draw a bow or crossbow the camera moves to the shoulder (third-person shooter style) and goes back when you release.", "Enquanto puxa o arco ou a besta a câmera passa para o ombro (estilo jogo de tiro) e volta ao largar."))
	AddToggleRow("BowShoulderRight", T("Right shoulder", "Ombro direito"), 1.0, T("On = right shoulder, off = left. The swap key changes it while you draw.", "Ligado = ombro direito, desligado = esquerdo. A tecla de trocar muda-o enquanto puxa."))
	AddSliderRow("BowShoulderX", T("Shoulder: sideways", "Ombro: lateral"), 0.0, 120.0, 2.0, 40.0, 1.0, 0, "{0}", T("How far the camera moves sideways.", "Quanto a câmera vai para o lado."))
	AddSliderRow("BowShoulderY", T("Shoulder: closer / farther", "Ombro: mais perto / mais longe"), -100.0, 100.0, 2.0, 0.0, 1.0, 0, "{0}", T("Added to the normal camera offset. Positive = camera forward (closer), negative = farther.", "Somado ao deslocamento normal da câmera. Positivo = câmera para a frente (mais perto), negativo = mais longe."))
	AddSliderRow("BowShoulderZ", T("Shoulder: height", "Ombro: altura"), -60.0, 80.0, 2.0, 10.0, 1.0, 0, "{0}", T("Added to the normal camera height.", "Somado à altura normal da câmera."))
	AddKeyRow("BowShoulderSwapKey", T("Swap shoulder key", "Tecla de trocar de ombro"), T("Pressed while the bow is drawn it swaps shoulder. Default E (also the Activate key).", "Carregada com o arco puxado troca de ombro. Padrão: E (também é a tecla de ativar)."))
EndFunction

Function PageEnv()
	Sec(T("Environment sensors (RaySense style)", "Sensores do ambiente (estilo RaySense)"))
	AddToggleRow("EnvProbe", T("Environment sensors", "Sensores do ambiente"), 1.0, T("Reads obstacles, walls, ledges, slope and beams around you and publishes them to SoftTargetEnv.esp for the jump / cover / beam animation packs. Off = nothing is read and those packs do nothing.", "Lê obstáculos, paredes, saliências, inclinação e vigas à tua volta e publica-os no SoftTargetEnv.esp para os pacotes de animação de salto, cobertura e viga. Desligado = nada é lido e esses pacotes não fazem nada."))
	AddSliderRow("EnvPerformance", T("Sensor mode (0 light, 1 normal, 2 powerful)", "Modo dos sensores (0 leve, 1 normal, 2 potente)"), 0.0, 2.0, 1.0, 2.0, 1.0, 1, "{0}", T("How often the sensors read. Light is for weak PCs. In combat the turning threads always run at full speed.", "Com que frequência os sensores leem. Leve é para PCs fracos. Em combate as threads de giro correm sempre à velocidade máxima."))
	AddToggleRow("EnvDemandScan", T("Run only the sensors the animations use", "Só correr os sensores que as animações usam"), 1.0, T("At load, reads the OAR configs and skips sensor groups no animation needs. Applies on the next game start.", "Ao carregar, lê as configs do OAR e salta os grupos de sensores que nenhuma animação usa. Aplica-se no próximo arranque do jogo."))
	AddToggleRow("EnvInCombat", T("Also read in combat", "Ler também em combate"), 0.0, T("Off by default to keep combat light. Turn on if you want obstacle jumps and cover during fights.", "Desligado por omissão para manter o combate leve. Liga se quiseres saltos de obstáculo e cobertura durante lutas."))
	Sec(T("Obstacle jump", "Salto de obstáculo"))
	AddToggleRow("EnvJumpObstacle", T("Obstacle jump state", "Estado de salto de obstáculo"), 1.0, T("Keeps ST_JumpObstacle on during a jump started in front of an obstacle (used by the fall and land animations).", "Mantém ST_JumpObstacle ligado durante um salto iniciado diante de um obstáculo (usado pelas animações de queda e aterragem)."))
	AddToggleRow("ObstacleJumpBonus", T("Higher jump over obstacles", "Salto mais alto sobre obstáculos"), 1.0, T("Raises the jump height only when an obstacle is in front. Normal jumps are not changed.", "Aumenta a altura do salto só quando há um obstáculo à frente. Os saltos normais não mudam."))
	AddSliderRow("JumpObstacleMin", T("Minimum obstacle height", "Altura mínima do obstáculo"), 20.0, 200.0, 2.0, 44.0, 1.0, 0, "{0}", T("Obstacles lower than this are jumped normally. RaySense uses 66.", "Obstáculos mais baixos que isto são saltados normalmente. O RaySense usa 66."))
	AddSliderRow("ObstacleJumpMinDistance", T("Minimum distance to the obstacle", "Distância mínima ao obstáculo"), 0.0, 300.0, 5.0, 120.0, 1.0, 0, "{0}", T("Closer than this the obstacle jump does not apply (FreeRun climbs instead). Raise it if FreeRun and the jump collide.", "Mais perto que isto o salto de obstáculo não se aplica (o FreeRun escala). Aumenta se o FreeRun e o salto colidirem."))
	AddSliderRow("ObstacleJumpClearance", T("Clearance above the top", "Folga acima do topo"), 0.0, 100.0, 5.0, 40.0, 1.0, 0, "{0}", T("Extra height above the obstacle top the jump aims for.", "Altura extra acima do topo do obstáculo que o salto procura."))
	AddSliderRow("ObstacleJumpMaxMult", T("Largest jump multiplier", "Maior multiplicador do salto"), 1.0, 4.0, 0.1, 3.3, 1.0, 0, "{1}", T("Upper limit of the jump height multiplier.", "Limite máximo do multiplicador da altura do salto."))
	Sec(T("Beams and narrow walkways", "Vigas e passadiços estreitos"))
	AddToggleRow("BeamAssist", T("Beam assist", "Assistência na viga"), 1.0, T("On a beam the view lines up with the beam and is pulled back to the centre, so you do not slip off or run past it. Jumping in another direction leaves the beam.", "Numa viga a vista alinha-se com a viga e é puxada de volta ao centro, para não escorregares nem passares ao lado. Saltar noutra direção sai da viga."))
	AddToggleRow("BeamRail", T("Beam rail", "Trilho na viga"), 1.0, T("On a beam W and S walk along the line and you are gently pulled to the centre. Jumping releases you.", "Numa viga o W e o S andam ao longo da linha e és puxado de leve para o centro. Saltar solta-te."))
	AddToggleRow("BeamEndSlow", T("Stop at the end of the beam", "Parar no fim da viga"), 1.0, T("The movement slows down and stops at the end of the beam. You leave it only by jumping.", "O movimento abranda e para no fim da viga. Só se sai saltando."))
	AddToggleRow("BeamJumpAlign", T("Jump onto a beam: align the body", "Saltar para a viga: alinhar o corpo"), 1.0, T("When you jump toward a narrow beam about 90 units ahead the body lines up with its axis.", "Ao saltar para uma viga estreita a cerca de 90 unidades o corpo alinha-se com o eixo dela."))
	AddSliderRow("BeamSpeed", T("Beam: walking speed", "Viga: velocidade ao andar"), 0.3, 1.0, 0.05, 0.85, 1.0, 0, "{2}", T("Speed along the beam (with Shift there is no reduction).", "Velocidade ao longo da viga (com Shift não há redução)."))
	AddSliderRow("BeamTau", T("Beam: alignment time", "Viga: tempo de alinhamento"), 0.03, 1.0, 0.01, 0.03, 1.0, 0, "{2} s", T("Lower = lines up faster, higher = smoother.", "Menor = alinha mais depressa, maior = mais suave."))
	AddSliderRow("BeamMaxYawRate", T("Beam: maximum turn speed", "Viga: velocidade máxima do giro"), 0.3, 6.0, 0.1, 6.0, 1.0, 0, "{1} rad/s", T("Limits how fast the view may turn.", "Limita a rapidez com que a vista pode girar."))
	AddSliderRow("BeamLateralGain", T("Beam: pull to the centre", "Viga: força para o centro"), 0.0, 1.5, 0.05, 1.5, 1.0, 0, "{2}", T("How strongly you are pulled back when off the centre.", "Quão forte és puxado de volta quando sais do centro."))
	Sec(T("Edges", "Bordas"))
	AddToggleRow("EdgeZone", T("Edge zone", "Zona de borda"), 1.0, T("A drop near your feet turns on the edge animations.", "Uma queda junto dos pés liga as animações de borda."))
	AddToggleRow("EdgeGuard", T("Edge guard", "Guarda da borda"), 1.0, T("Near a drop the movement toward it is reduced gradually; pushing on for 1.5 s or jumping releases you. Off while on a beam. Needs the learned movement.", "Perto de uma queda o movimento em direção a ela reduz-se aos poucos; empurrar 1,5 s ou saltar solta-te. Desligada na viga. Precisa do movimento aprendido."))
	AddSliderRow("EdgeDropMin", T("Edge: minimum drop", "Borda: queda mínima"), 20, 300, 5, 60, 1.0, 1, "{0}", T("Smaller drops (steps) are ignored by the edge zone and the guard.", "Quedas menores (degraus) são ignoradas pela zona de borda e pela guarda."))
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
		if okey[i] == "ProfileAuto"
			ForcePageReset()
		endIf
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
		elseIf okey[i] == "@learn"
			SoftTargetNative.LearnMovement()
			SetTextOptionValue(a_option, T("[Waiting...]", "[A aguardar...]"))
			ShowMessage(T("Close the menu now. In the game press W, A, S, D one at a time (hold each for 1-2 seconds, repeat for about 15 seconds). The result is saved in the ini. Run it again if the movement is wrong.", "Feche o menu agora. No jogo carregue W, A, S, D um de cada vez (segure cada um 1-2 segundos, repita durante cerca de 15 segundos). O resultado fica guardado no ini. Repita se o movimento ficar errado."), false)
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
