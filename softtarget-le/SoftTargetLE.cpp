// SoftTarget LE - golpes auto-direcionados (sem travar a mira), estilo Witcher 3.
//
// Ao carregar no botao de ataque (ou ao SEGURAR o botao de bloqueio), o plugin procura o inimigo
// mais proximo (cone a frente ou circulo a volta) e vira o CORPO do personagem para ele, de forma
// suave. Nao usa scripts em loop. Todas as afinacoes estao no SoftTargetLE.ini, que e relido
// sozinho quando e guardado e pode ser mudado pelo menu (MCM do SkyUI), por funcoes nativas.
//
// ESTADO: versao 0.5.0 (experimental). Novidades: funcoes para o menu, camera pelo deslocamento
// (CameraMode=3), CoeusYield, detecao real do lock-on (LockOn.dll), botoes extras (ExtraTapKeys /
// ExtraHoldKeys) e idioma do menu (Language).
// Licenca GPL-3.0. A base de compilacao e os enderecos vem do FreeClimb LE (GPL-3.0).

#include "skse/PluginAPI.h"
#include "skse/skse_version.h"
#include "skse/GameRTTI.h"
#include "skse/GameCamera.h"
#include "skse/PapyrusVM.h"
#include "skse/PapyrusNativeFunctions.h"
#include "Log.h"
#include "Engine.h"

#include <shlobj.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define PLUGIN_NAME			"SoftTargetLE"
#define PLUGIN_VERSION_STR	"0.6.0 (perfis Vanilla/Coeus, combo sticky, menu)"
#define PLUGIN_LOG_FILE		"\\My Games\\Skyrim\\SKSE\\SoftTargetLE.log"
#define PLUGIN_INI_NAME		"SoftTargetLE.ini"
#define PLUGIN_INI_FALLBACK	"Data\\SKSE\\Plugins\\SoftTargetLE.ini"

static PluginHandle				g_pluginHandle	= kPluginHandle_Invalid;
static SKSEMessagingInterface *	g_messaging		= NULL;
static SKSEPapyrusInterface *	g_papyrus		= NULL;

// O SKSE declara LookupREFRByHandle mas so a define em GameReferences.cpp, que nao compilamos.
// Definimos aqui com o endereco da LE 1.9.32.0 (confirmado no FreeClimb LE).
const _LookupREFRByHandle LookupREFRByHandle = (_LookupREFRByHandle)0x004A9180;

namespace
{
	const float kPi = 3.14159265f;

	// ------------------------------------------------------------------ afinacoes (.ini)
	struct Config
	{
		int		enabled;		// 1 = ligado
		int		logOnly;		// 1 = so escreve no log, nao vira o personagem
		int		verbose;		// 1 = log detalhado
		int		probe;			// 1 = diagnostico (so leitura)
		int		requireCombat;	// 1 = so inimigos ja em combate
		int		useHostileList;	// 1 = tambem aceita quem o jogo ja lista como hostil ao jogador
		int		shape;			// 0 = cone a frente, 1 = circulo a volta (360 graus)
		float	range;			// alcance maximo (140 = ~2 m)
		float	coneDeg;		// angulo TOTAL do cone (so no modo cone)
		float	maxHeightDiff;	// diferenca maxima de altura ate ao alvo (relevo)
		float	switchMargin;	// % que um novo alvo tem de ser mais proximo para trocar
		float	turnSpeedDeg;	// velocidade maxima de rotacao, graus por segundo
		float	smoothing;		// 0 = velocidade constante; >0 = abranda ao chegar
		int		updateMs;		// intervalo da rotacao, em ms
		int		tapAssist;		// 1 = vira ao carregar no ataque
		int		holdMode;		// bloqueio continuo: 0 = off, 1 = botao esquerdo (M2), 2 = direito (M1), 3 = ambos
		int		retargetMs;		// de quanto em quanto tempo re-escolhe o alvo durante o giro
		int		holdMs;			// quanto tempo o corpo encara o alvo depois de um toque
		int		returnToHeading;// 1 = depois do golpe o corpo volta ao rumo anterior
		float	returnSpeedDeg;	// velocidade do regresso, graus por segundo
		int		returnMaxMs;	// tempo maximo do regresso
		int		cameraMode;		// 0 = nao mexe; 1 = contra-rotacao; 2 = repoe angulo; 3 = deslocamento (recomendado)
		int		lockOnKey;		// codigo da tecla do lock-on (reserva se a LockOn.dll nao existir)
		int		lockOnMode;		// com lock-on ativo: 0 = pausa o SoftTarget; 1 = so corpo, sem mexer na camera
		int		coeusYield;		// 1 = enquanto vira, pede ao Coeus para largar a rotacao do corpo
		int		verticalMode;	// mira vertical: 0 = off, 1 = corpo, 2 = camera, 3 = ambas
		float	aimHeight;		// altura do ponto de mira no alvo, acima dos pes
		float	playerAimHeight;// altura de onde o jogador "mira", acima dos pes
		int		verticalInvert;	// 1 = inverte o sentido da inclinacao
		float	verticalLimitDeg;// inclinacao maxima, em graus
		int		language;		// idioma do menu: 0 = ingles, 1 = portugues
		int		profile;		// 0 = automatico (Coeus se o movimento direcional esta ativo), 1 = sem TDM, 2 = Coeus (TDM)
		int		cameraModeCoeus;// camera no perfil Coeus (0 = nao mexe, recomendado)
		int		stickyTarget;	// 1 = durante uma sequencia de golpes mantem o alvo enquanto for valido
		int		allowReanimated;// 1 = aceita inimigos reanimados / sem estado de vida normal
	};
	Config g_cfg;

	// Tabela de opcoes: usada para ler o .ini e pelas funcoes nativas do menu.
	struct Opt { const char * key; char type; void * ptr; float def; };
	Opt g_opts[] =
	{
		{ "Enabled",				'i', &g_cfg.enabled,			1.0f },
		{ "LogOnly",				'i', &g_cfg.logOnly,			0.0f },
		{ "Verbose",				'i', &g_cfg.verbose,			0.0f },
		{ "Probe",					'i', &g_cfg.probe,				0.0f },
		{ "RequireCombat",			'i', &g_cfg.requireCombat,		1.0f },
		{ "UseHostileList",			'i', &g_cfg.useHostileList,		1.0f },
		{ "Shape",					'i', &g_cfg.shape,				0.0f },
		{ "Range",					'f', &g_cfg.range,				450.0f },
		{ "ConeDegrees",			'f', &g_cfg.coneDeg,			140.0f },
		{ "MaxHeightDifference",	'f', &g_cfg.maxHeightDiff,		600.0f },
		{ "SwitchMarginPercent",	'f', &g_cfg.switchMargin,		0.0f },
		{ "TurnSpeedDegPerSec",		'f', &g_cfg.turnSpeedDeg,		900.0f },
		{ "Smoothing",				'f', &g_cfg.smoothing,			0.0f },
		{ "UpdateMilliseconds",		'i', &g_cfg.updateMs,			8.0f },
		{ "TapAssist",				'i', &g_cfg.tapAssist,			1.0f },
		{ "HoldFaceButton",			'i', &g_cfg.holdMode,			3.0f },
		{ "RetargetMilliseconds",	'i', &g_cfg.retargetMs,			80.0f },
		{ "HoldMilliseconds",		'i', &g_cfg.holdMs,				3000.0f },
		{ "ReturnToHeading",		'i', &g_cfg.returnToHeading,	1.0f },
		{ "ReturnSpeedDegPerSec",	'f', &g_cfg.returnSpeedDeg,		360.0f },
		{ "ReturnMaxMilliseconds",	'i', &g_cfg.returnMaxMs,		1200.0f },
		{ "CameraMode",				'i', &g_cfg.cameraMode,			0.0f },
		{ "LockOnKey",				'i', &g_cfg.lockOnKey,			0.0f },
		{ "LockOnMode",				'i', &g_cfg.lockOnMode,			0.0f },
		{ "CoeusYield",				'i', &g_cfg.coeusYield,			1.0f },
		{ "VerticalMode",			'i', &g_cfg.verticalMode,		0.0f },
		{ "AimHeight",				'f', &g_cfg.aimHeight,			100.0f },
		{ "PlayerAimHeight",		'f', &g_cfg.playerAimHeight,	110.0f },
		{ "VerticalInvert",			'i', &g_cfg.verticalInvert,		0.0f },
		{ "VerticalLimitDegrees",	'f', &g_cfg.verticalLimitDeg,	45.0f },
		{ "Language",				'i', &g_cfg.language,			0.0f },
		{ "Profile",				'i', &g_cfg.profile,			0.0f },
		{ "CameraModeCoeus",		'i', &g_cfg.cameraModeCoeus,	0.0f },
		{ "StickyTarget",			'i', &g_cfg.stickyTarget,		1.0f },
		{ "AllowReanimated",		'i', &g_cfg.allowReanimated,	1.0f },
	};
	const int kNumOpts = sizeof(g_opts) / sizeof(g_opts[0]);

	// Teclas extra (codigos de teclado separados por virgula): toque e segurar.
	const int kMaxKeys = 8;
	int g_tapKeys[kMaxKeys];	int g_numTapKeys = 0;
	int g_holdKeys[kMaxKeys];	int g_numHoldKeys = 0;

	char		g_iniPath[MAX_PATH];
	FILETIME	g_iniStamp;
	DWORD		g_lastIniCheck = 0;

	bool FileExists(const char * path)
	{
		const DWORD a = GetFileAttributesA(path);
		return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
	}

	// Procura o .ini ao lado da DLL (funciona com Mod Organizer 2 e Vortex); se nao houver, usa
	// o caminho classico Data\SKSE\Plugins. Se nenhum existir, guarda o caminho ao lado da DLL
	// (e o menu cria o ficheiro ao gravar).
	void ResolveIniPath()
	{
		g_iniPath[0] = 0;

		HMODULE self = NULL;
		if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
							  (LPCSTR)&ResolveIniPath, &self))
		{
			char dll[MAX_PATH] = { 0 };
			const DWORD n = GetModuleFileNameA(self, dll, MAX_PATH);
			if(n > 0 && n < MAX_PATH)
			{
				char * slash = strrchr(dll, '\\');
				if(slash)
				{
					slash[1] = 0;
					_snprintf_s(g_iniPath, sizeof(g_iniPath), _TRUNCATE, "%s%s", dll, PLUGIN_INI_NAME);
				}
			}
		}

		if(!g_iniPath[0] || !FileExists(g_iniPath))
		{
			if(FileExists(PLUGIN_INI_FALLBACK))
				_snprintf_s(g_iniPath, sizeof(g_iniPath), _TRUNCATE, "%s", PLUGIN_INI_FALLBACK);
		}
	}

	float ReadFloat(const char * key, float def)
	{
		char def_s[32], buf[48];
		_snprintf_s(def_s, sizeof(def_s), _TRUNCATE, "%f", def);
		GetPrivateProfileStringA("SoftTarget", key, def_s, buf, sizeof(buf), g_iniPath);
		return (float)atof(buf);
	}

	int ReadInt(const char * key, int def)
	{
		return (int)(ReadFloat(key, (float)def) + (def < 0 ? -0.5f : 0.5f));
	}

	int ParseKeys(const char * key, int * out, int maxN)
	{
		char buf[128] = { 0 };
		GetPrivateProfileStringA("SoftTarget", key, "", buf, sizeof(buf), g_iniPath);
		int n = 0;
		const char * p = buf;
		while(*p && n < maxN)
		{
			while(*p == ' ' || *p == ',' || *p == ';') ++p;
			if(!*p) break;
			char * end = NULL;
			const long v = strtol(p, &end, 10);
			if(end == p) { ++p; continue; }
			if(v > 0 && v < 512) out[n++] = (int)v;
			p = end;
		}
		return n;
	}

	void RefreshIniStamp()
	{
		WIN32_FILE_ATTRIBUTE_DATA d;
		if(g_iniPath[0] && GetFileAttributesExA(g_iniPath, GetFileExInfoStandard, &d)) g_iniStamp = d.ftLastWriteTime;
	}

	void LoadConfig()
	{
		const bool found = g_iniPath[0] && FileExists(g_iniPath);
		if(found) RefreshIniStamp();

		for(int i = 0; i < kNumOpts; ++i)
		{
			if(g_opts[i].type == 'i') *(int *)g_opts[i].ptr = ReadInt(g_opts[i].key, (int)g_opts[i].def);
			else					  *(float *)g_opts[i].ptr = ReadFloat(g_opts[i].key, g_opts[i].def);
		}
		g_numTapKeys  = ParseKeys("ExtraTapKeys",  g_tapKeys,  kMaxKeys);
		g_numHoldKeys = ParseKeys("ExtraHoldKeys", g_holdKeys, kMaxKeys);

		if(g_cfg.updateMs < 1)		g_cfg.updateMs = 1;
		if(g_cfg.updateMs > 50)		g_cfg.updateMs = 50;
		if(g_cfg.retargetMs < 16)	g_cfg.retargetMs = 16;

		_MESSAGE("config (%s%s): enabled=%d logOnly=%d shape=%s range=%.0f cone=%.0f turn=%.0f smooth=%.1f update=%dms tap=%d hold=%d retarget=%dms holdMs=%d combat=%d camera=%d lockKey=%d lockMode=%d yield=%d hostileList=%d return=%d vertical=%d switchMargin=%.0f tapKeys=%d holdKeys=%d lang=%d",
				 found ? "ini: " : "NO ini found, defaults; looked for: ", g_iniPath[0] ? g_iniPath : PLUGIN_INI_NAME,
				 g_cfg.enabled, g_cfg.logOnly, g_cfg.shape ? "circle" : "cone", g_cfg.range, g_cfg.coneDeg,
				 g_cfg.turnSpeedDeg, g_cfg.smoothing, g_cfg.updateMs, g_cfg.tapAssist, g_cfg.holdMode,
				 g_cfg.retargetMs, g_cfg.holdMs, g_cfg.requireCombat, g_cfg.cameraMode, g_cfg.lockOnKey, g_cfg.lockOnMode,
				 g_cfg.coeusYield, g_cfg.useHostileList, g_cfg.returnToHeading, g_cfg.verticalMode, g_cfg.switchMargin,
				 g_numTapKeys, g_numHoldKeys, g_cfg.language);
	}

	// Relê o .ini se ele mudou (verifica no maximo uma vez por segundo).
	void MaybeReloadConfig()
	{
		const DWORD now = GetTickCount();
		if(now - g_lastIniCheck < 1000) return;
		g_lastIniCheck = now;

		if(!g_iniPath[0]) return;
		WIN32_FILE_ATTRIBUTE_DATA d;
		if(!GetFileAttributesExA(g_iniPath, GetFileExInfoStandard, &d)) return;
		if(CompareFileTime(&d.ftLastWriteTime, &g_iniStamp) != 0)
		{
			_MESSAGE("ini changed - reloading");
			LoadConfig();
		}
	}

	// ------------------------------------------------------------------ DLLs opcionais (Coeus, LockOn)
	typedef bool (*FnVoidBool)(void);
	typedef void (*FnSetExternal)(bool active, UInt32 handle);

	FnVoidBool		g_sloIsLockOn	= NULL;		// LockOn.dll: SLO_IsLockOnActive
	FnSetExternal	g_coeusExternal	= NULL;		// Coeus.dll: Coeus_SetExternalLockOnActive
	FnVoidBool		g_coeusDirActive = NULL;	// Coeus.dll: Coeus_IsDirectionalMovementActive

	void ResolveExternal()
	{
		HMODULE h = GetModuleHandleA("LockOn.dll");
		if(h) g_sloIsLockOn = (FnVoidBool)GetProcAddress(h, "SLO_IsLockOnActive");
		h = GetModuleHandleA("Coeus.dll");
		if(h) g_coeusExternal = (FnSetExternal)GetProcAddress(h, "Coeus_SetExternalLockOnActive");
		if(h) g_coeusDirActive = (FnVoidBool)GetProcAddress(h, "Coeus_IsDirectionalMovementActive");
		_MESSAGE("external: Coeus directional-movement query %s", g_coeusDirActive ? "FOUND" : "not found");
		_MESSAGE("external: LockOn.dll lock-on query %s, Coeus.dll external lock %s",
				 g_sloIsLockOn ? "FOUND" : "not found (F key fallback)", g_coeusExternal ? "FOUND" : "not found");
	}

	// ------------------------------------------------------------------ matematica de angulos
	// No Skyrim a rotacao Z (guinada) e medida a partir de +Y, no sentido horario:
	// a direcao (dx, dy) corresponde a atan2(dx, dy).
	float WrapPi(float a)
	{
		while(a >  kPi) a -= 2.0f * kPi;
		while(a < -kPi) a += 2.0f * kPi;
		return a;
	}

	// ------------------------------------------------------------------ lock-on
	// Estado real vindo da LockOn.dll, se existir; senao, o ultimo toque na tecla LockOnKey.
	bool			g_lockKeyState = false;
	DWORD			g_lockNoEnemySince = 0;
	volatile LONG	g_lockActive = 0;

	bool QueryLockOn()
	{
		if(g_sloIsLockOn)
		{
			__try { return g_sloIsLockOn(); }
			__except(EXCEPTION_EXECUTE_HANDLER) { g_sloIsLockOn = NULL; return false; }
		}
		return g_lockKeyState;
	}

	// Perfil efetivo: true = Coeus (TDM) ativo. Em automatico pergunta ao Coeus a cada chamada.
	bool InCoeusProfile()
	{
		if(g_cfg.profile == 1) return false;
		if(g_cfg.profile == 2) return true;
		if(g_coeusDirActive)
		{
			__try { return g_coeusDirActive(); }
			__except(EXCEPTION_EXECUTE_HANDLER) { g_coeusDirActive = NULL; return false; }
		}
		return false;
	}

	int EffCameraMode() { return InCoeusProfile() ? g_cfg.cameraModeCoeus : g_cfg.cameraMode; }

	// ------------------------------------------------------------------ funcoes nativas (menu)
	const Opt * FindOpt(const char * key)
	{
		if(!key) return NULL;
		for(int i = 0; i < kNumOpts; ++i)
			if(_stricmp(g_opts[i].key, key) == 0) return &g_opts[i];
		return NULL;
	}

	float OptValue(const Opt * o)
	{
		return (o->type == 'i') ? (float)*(int *)o->ptr : *(float *)o->ptr;
	}

	void SetOpt(const char * key, float value)
	{
		const Opt * o = FindOpt(key);
		if(!o) { _MESSAGE("menu: unknown option '%s'", key ? key : "(null)"); return; }

		char buf[48];
		if(o->type == 'i')
		{
			const int v = (int)(value + (value < 0 ? -0.5f : 0.5f));
			*(int *)o->ptr = v;
			_snprintf_s(buf, sizeof(buf), _TRUNCATE, "%d", v);
		}
		else
		{
			*(float *)o->ptr = value;
			_snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.3f", value);
		}

		if(g_cfg.updateMs < 1)		g_cfg.updateMs = 1;
		if(g_cfg.updateMs > 50)		g_cfg.updateMs = 50;
		if(g_cfg.retargetMs < 16)	g_cfg.retargetMs = 16;

		_MESSAGE("menu: %s = %s", o->key, buf);
		if(g_iniPath[0])
		{
			WritePrivateProfileStringA("SoftTarget", o->key, buf, g_iniPath);
			RefreshIniStamp();		// para nao reler o que acabamos de gravar
		}
	}

	bool	N_GetBool(StaticFunctionTag *, BSFixedString key)	{ const Opt * o = FindOpt(key.data); return o ? (OptValue(o) != 0.0f) : false; }
	SInt32	N_GetInt(StaticFunctionTag *, BSFixedString key)	{ const Opt * o = FindOpt(key.data); return o ? (SInt32)OptValue(o) : 0; }
	float	N_GetFloat(StaticFunctionTag *, BSFixedString key)	{ const Opt * o = FindOpt(key.data); return o ? OptValue(o) : 0.0f; }
	void	N_SetBool(StaticFunctionTag *, BSFixedString key, bool value)		{ SetOpt(key.data, value ? 1.0f : 0.0f); }
	void	N_SetInt(StaticFunctionTag *, BSFixedString key, SInt32 value)		{ SetOpt(key.data, (float)value); }
	void	N_SetFloat(StaticFunctionTag *, BSFixedString key, float value)		{ SetOpt(key.data, value); }
	void	N_Reload(StaticFunctionTag *)										{ LoadConfig(); }

	void WriteOptToIni(const Opt * o)
	{
		char buf[48];
		if(o->type == 'i') _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%d", *(int *)o->ptr);
		else			   _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.3f", *(float *)o->ptr);
		WritePrivateProfileStringA("SoftTarget", o->key, buf, g_iniPath);
	}

	// Grava TODAS as opcoes no .ini (botao "Save now" do menu).
	void N_SaveAll(StaticFunctionTag *)
	{
		if(g_iniPath[0])
		{
			for(int i = 0; i < kNumOpts; ++i) WriteOptToIni(&g_opts[i]);
			RefreshIniStamp();
		}
		_MESSAGE("menu: all options saved to the ini");
	}

	// Repoe os valores de fabrica (botao "Reset" do menu) e grava. Mantem o idioma.
	void N_ResetDefaults(StaticFunctionTag *)
	{
		for(int i = 0; i < kNumOpts; ++i)
		{
			if(_stricmp(g_opts[i].key, "Language") == 0) continue;
			if(g_opts[i].type == 'i') *(int *)g_opts[i].ptr = (int)g_opts[i].def;
			else					  *(float *)g_opts[i].ptr = g_opts[i].def;
		}
		N_SaveAll(NULL);
		_MESSAGE("menu: defaults restored");
	}

	// Estado para o menu: 0 = perfil efetivo (1 sem TDM, 2 Coeus), 1 = LockOn.dll, 2 = Coeus.dll (0/1), 3 = lock-on ativo
	SInt32 N_Status(StaticFunctionTag *, SInt32 what)
	{
		switch(what)
		{
			case 0: return InCoeusProfile() ? 2 : 1;
			case 1: return g_sloIsLockOn ? 1 : 0;
			case 2: return (g_coeusExternal || g_coeusDirActive) ? 1 : 0;
			case 3: return g_lockActive ? 1 : 0;
		}
		return 0;
	}

	bool RegisterPapyrus(VMClassRegistry * registry)
	{
		registry->RegisterFunction(new NativeFunction1 <StaticFunctionTag, bool, BSFixedString>("GetBool", "SoftTargetNative", N_GetBool, registry));
		registry->RegisterFunction(new NativeFunction1 <StaticFunctionTag, SInt32, BSFixedString>("GetInt", "SoftTargetNative", N_GetInt, registry));
		registry->RegisterFunction(new NativeFunction1 <StaticFunctionTag, float, BSFixedString>("GetFloat", "SoftTargetNative", N_GetFloat, registry));
		registry->RegisterFunction(new NativeFunction2 <StaticFunctionTag, void, BSFixedString, bool>("SetBool", "SoftTargetNative", N_SetBool, registry));
		registry->RegisterFunction(new NativeFunction2 <StaticFunctionTag, void, BSFixedString, SInt32>("SetInt", "SoftTargetNative", N_SetInt, registry));
		registry->RegisterFunction(new NativeFunction2 <StaticFunctionTag, void, BSFixedString, float>("SetFloat", "SoftTargetNative", N_SetFloat, registry));
		registry->RegisterFunction(new NativeFunction0 <StaticFunctionTag, void>("Reload", "SoftTargetNative", N_Reload, registry));
		registry->RegisterFunction(new NativeFunction0 <StaticFunctionTag, void>("SaveAll", "SoftTargetNative", N_SaveAll, registry));
		registry->RegisterFunction(new NativeFunction0 <StaticFunctionTag, void>("ResetDefaults", "SoftTargetNative", N_ResetDefaults, registry));
		registry->RegisterFunction(new NativeFunction1 <StaticFunctionTag, SInt32, SInt32>("Status", "SoftTargetNative", N_Status, registry));
		_MESSAGE("papyrus functions registered (SoftTargetNative)");
		return true;
	}

	// ------------------------------------------------------------------ escolha do alvo
	UInt32 g_lastTargetId = 0;
	volatile LONG	g_active	= 0;		// 1 = sequencia de golpes em curso (o corpo encara o alvo)

	// Inimigos que o proprio jogo ja tem em combate com o jogador (PlayerCharacter::hostileHandles,
	// offset 0x630: ponteiro + capacidade + tamanho). Ajuda com reanimados e invocados.
	int CollectHostileIds(PlayerCharacter * player, UInt32 * ids, UInt32 * handles, int maxIds)
	{
		const UInt32 * raw = (const UInt32 *)((UInt8 *)player + 0x630);
		const UInt32 * data = (const UInt32 *)raw[0];
		UInt32 count = (raw[1] < raw[2]) ? raw[1] : raw[2];
		if(!data || count == 0) return 0;
		if(count > (UInt32)maxIds) count = (UInt32)maxIds;

		int n = 0;
		for(UInt32 i = 0; i < count; ++i)
		{
			UInt32 handle = data[i];
			if(!handle) continue;
			UInt32 h = handle;
			TESObjectREFR * ref = NULL;
			if(LookupREFRByHandle(&h, &ref) && ref)
			{
				ids[n] = ref->formID;
				if(handles) handles[n] = handle;
				++n;
				ref->handleRefObject.DecRefHandle();
			}
		}
		return n;
	}

	DWORD g_lastActorLog = 0;

	// Devolve true se encontrou um alvo; escreve a guinada, a inclinacao (positiva = para cima),
	// o formID e o handle (0 se o jogo nao o lista).
	bool FindTarget(Actor * player, float * outYaw, float * outPitchUp, UInt32 * outId, UInt32 * outHandle)
	{
		TESObjectCELL * cell = Engine::ParentCell(player);
		if(!cell) return false;

		const NiPoint3 me		= Engine::Position(player);
		const float facing		= Engine::Rotation(player).z;
		const float halfCone	= (g_cfg.coneDeg * 0.5f) * kPi / 180.0f;
		const bool  cone		= (g_cfg.shape == 0);
		const float limitRad	= g_cfg.verticalLimitDeg * kPi / 180.0f;

		UInt32 hostileIds[64], hostileHandles[64];
		const int hostileCount = CollectHostileIds((PlayerCharacter *)player, hostileIds, hostileHandles, 64);

		bool dbg = false;
		int dbgLines = 0;
		if(g_cfg.probe && GetTickCount() - g_lastActorLog > 1000) { dbg = true; g_lastActorLog = GetTickCount(); }

		float bestScore = 1e30f;
		bool found = false;

		// A lista de referencias da celula e cell->objectList (GameForms.h do SKSE 1.7.3, offset 0x4C):
		// ponteiro para os dados + dois contadores. Usa-se o MENOR dos dois, que e sempre o tamanho.
		const UInt32 * raw = (const UInt32 *)&cell->objectList;
		TESObjectREFR ** items = (TESObjectREFR **)raw[0];
		UInt32 count = (raw[1] < raw[2]) ? raw[1] : raw[2];
		if(!items || count == 0) return false;
		if(count > 20000) count = 20000;										// seguranca

		for(UInt32 i = 0; i < count; ++i)
		{
			TESObjectREFR * ref = items[i];
			if(!ref || ref == (TESObjectREFR *)player) continue;
			if(ref->formType != kFormType_Character) continue;

			Actor * actor = DYNAMIC_CAST(ref, TESObjectREFR, Actor);
			if(!actor) continue;

			const NiPoint3 pos = Engine::Position(actor);
			const float dx = pos.x - me.x;
			const float dy = pos.y - me.y;
			const float dz = pos.z - me.z;
			const float dist = sqrtf(dx * dx + dy * dy);
			if(dist > g_cfg.range || fabsf(dz) > g_cfg.maxHeightDiff) continue;

			const UInt32 life = Engine::LifeState(actor);

			int listedAt = -1;
			if(g_cfg.useHostileList)
				for(int j = 0; j < hostileCount; ++j) if(hostileIds[j] == actor->formID) { listedAt = j; break; }
			const bool listed = (listedAt >= 0);

			// Hostil ao jogador: funcao do proprio jogo OU presente na lista de hostis do jogador.
			const bool hostile = listed || CALL_MEMBER_FN(actor, IsHostileToActor)(player);

			if(dbg && dbgLines < 8)
			{
				++dbgLines;
				_MESSAGE("probe-actor: formID %08X dist %.0f dz %.0f life=%u hostile=%d listed=%d", actor->formID, dist, dz, (unsigned)life, hostile ? 1 : 0, listed ? 1 : 0);
			}

			// Vivos (0) ou, se permitido, em "reanimacao" (4); o resto (morto, a morrer, inconsciente...) nao.
			if(life != 0 && !(g_cfg.allowReanimated && life == 4)) continue;
			if(!hostile) continue;
			if(g_cfg.requireCombat && !listed && !actor->IsInCombat()) continue;

			const float yaw = atan2f(dx, dy);
			const float off = fabsf(WrapPi(yaw - facing));

			float score;
			if(cone)
			{
				if(off > halfCone) continue;
				score = dist * (1.0f + off / (kPi * 0.5f));
			}
			else
			{
				score = dist;											// circulo: o mais proximo, em qualquer direcao
			}

			// Histerese opcional: o alvo atual so e trocado se outro for claramente mais perto.
			if(actor->formID == g_lastTargetId)
			{
				if(g_cfg.stickyTarget && g_active) score = -1.0f;		// sequencia de golpes: mantem o alvo enquanto for valido
				else if(g_cfg.switchMargin > 0.0f) score *= (1.0f - g_cfg.switchMargin / 100.0f);
			}

			if(score < bestScore)
			{
				bestScore = score;
				*outYaw = yaw;
				*outId = actor->formID;
				*outHandle = (listedAt >= 0) ? hostileHandles[listedAt] : 0;

				const float aimZ = (pos.z + g_cfg.aimHeight) - (me.z + g_cfg.playerAimHeight);
				float up = atan2f(aimZ, dist > 1.0f ? dist : 1.0f);
				if(up >  limitRad) up =  limitRad;
				if(up < -limitRad) up = -limitRad;
				*outPitchUp = up;

				found = true;
			}
		}
		return found;
	}

	// ------------------------------------------------------------------ camera
	// Em terceira pessoa (estado 8 ou 9): guinada da camera em 0x5C e 0x60, inclinacao em 0x54 e 0x58
	// e o "deslocamento" camera-corpo em 0xAC (camera = corpo + deslocamento; confirmado nos logs).
	UInt8 * ThirdPersonStatePtr()
	{
		PlayerCamera * cam = PlayerCamera::GetSingleton();
		if(!cam) return NULL;
		TESCameraState * st = cam->cameraState;
		if(!st) return NULL;
		if(st->stateId != PlayerCamera::kCameraState_ThirdPerson1 && st->stateId != PlayerCamera::kCameraState_ThirdPerson2) return NULL;
		return (UInt8 *)st;
	}

	// ------------------------------------------------------------------ rotacao suave (thread)
	volatile LONG	g_hasPitch	= 0;
	volatile LONG	g_moveHeld	= 0;		// teclas de movimento carregadas (o regresso nao corre contra o TDM)
	volatile DWORD	g_endTick	= 0;
	volatile float	g_targetYaw	= 0.0f;
	volatile float	g_targetPitchUp = 0.0f;
	volatile LONG	g_targetHandle = 0;

	// Passo (com sinal) para aproximar 'cur' de 'target', com velocidade maxima e suavizacao.
	float StepToward(float cur, float target, bool wrap, float dt, float maxSpeedRad)
	{
		const float delta = wrap ? WrapPi(target - cur) : (target - cur);
		const float maxStep = maxSpeedRad * dt;
		float want = delta;
		if(g_cfg.smoothing > 0.0f) want = delta * (1.0f - expf(-g_cfg.smoothing * dt));
		if(want >  maxStep) want =  maxStep;
		if(want < -maxStep) want = -maxStep;
		return want;
	}

	void CameraYawCompensate(UInt8 * cs, float step, float camYaw0, int mode)
	{
		if(!cs) return;
		if(mode == 3)
		{
			// O jogo calcula camera = corpo + deslocamento a cada quadro. Tiramos ao deslocamento o
			// mesmo giro dado ao corpo, e a propria conta do jogo deixa a camera parada.
			float * off = (float *)(cs + 0xAC);
			*off = WrapPi(*off - step);
			return;
		}
		float * y1 = (float *)(cs + 0x5C);
		float * y2 = (float *)(cs + 0x60);
		if(mode == 1)		{ *y1 -= step; *y2 -= step; }
		else if(mode == 2)	{ *y1 = camYaw0; *y2 = camYaw0; }
	}

	DWORD WINAPI TurnThread(LPVOID)
	{
		DWORD last = GetTickCount();
		int   phase = 0;					// 0 = parado, 1 = a virar para o alvo, 2 = a regressar ao rumo original
		float homeYaw = 0.0f, homePitch = 0.0f, camYaw0 = 0.0f, camPitch0 = 0.0f;
		DWORD returnStart = 0;
		bool  yielding = false;
		DWORD lastYield = 0;

		for(;;)
		{
			Sleep((DWORD)g_cfg.updateMs);
			const DWORD now = GetTickCount();
			float dt = (now - last) / 1000.0f;
			last = now;
			if(dt > 0.1f) dt = 0.1f;

			if(g_active && (LONG)(now - g_endTick) >= 0) g_active = 0;

			PlayerCharacter * player = Engine::Player();
			if(!player) { phase = 0; continue; }

			// Pede ao Coeus para largar a rotacao do corpo enquanto giramos (expira em 500 ms).
			const bool coeusProfile = InCoeusProfile();
			const int  camMode = EffCameraMode();

			// Lock-on ativo (e SoftTarget em pausa): larga tudo, sem regressar ao rumo antigo
			// (era isso que brigava com o lock-on e agitava a camera).
			if(g_lockActive && g_cfg.lockOnMode == 0) { phase = 0; g_active = 0; }

			const bool wantYield = g_cfg.coeusYield && coeusProfile && g_coeusExternal && (g_active || phase == 2) && !g_lockActive;
			if(wantYield)
			{
				if(!yielding || now - lastYield > 150)
				{
					__try { g_coeusExternal(true, (UInt32)g_targetHandle); }
					__except(EXCEPTION_EXECUTE_HANDLER) { g_coeusExternal = NULL; }
					yielding = true; lastYield = now;
				}
			}
			else if(yielding)
			{
				if(g_coeusExternal)
				{
					__try { g_coeusExternal(false, 0); }
					__except(EXCEPTION_EXECUTE_HANDLER) { g_coeusExternal = NULL; }
				}
				yielding = false;
			}

			// Camera: com o lock-on ativo (LockOnMode=1) so o corpo gira; a camera fica com o lock-on.
			const bool wantCam = (camMode != 0) || (g_cfg.verticalMode & 2);
			UInt8 * cs = (wantCam && !g_lockActive) ? ThirdPersonStatePtr() : NULL;
			NiPoint3 & rot = Engine::Rotation(player);
			const float maxRad = g_cfg.turnSpeedDeg * kPi / 180.0f;
			const float limitRad = g_cfg.verticalLimitDeg * kPi / 180.0f;
			const float vSign = g_cfg.verticalInvert ? -1.0f : 1.0f;

			if(g_active)
			{
				if(phase == 0)
				{
					// Inicio de um giro: guarda de onde partimos, para podermos regressar.
					homeYaw = rot.z;
					homePitch = rot.x;
					if(cs) { camYaw0 = *(float *)(cs + 0x5C); camPitch0 = *(float *)(cs + 0x54); }
					phase = 1;
				}
				else if(phase == 2) phase = 1;		// novo golpe durante o regresso: continua a partir do mesmo rumo original

				// Guinada do corpo.
				const float stepYaw = StepToward(rot.z, g_targetYaw, true, dt, maxRad);
				rot.z = WrapPi(rot.z + stepYaw);
				CameraYawCompensate(cs, stepYaw, camYaw0, camMode);

				// Mira vertical (magia).
				if(g_hasPitch && g_cfg.verticalMode)
				{
					float up = g_targetPitchUp;
					if(up >  limitRad) up =  limitRad;
					if(up < -limitRad) up = -limitRad;

					if(g_cfg.verticalMode & 1)
						rot.x += StepToward(rot.x, -up * vSign, false, dt, maxRad);	// positivo = olhar para baixo
					if((g_cfg.verticalMode & 2) && cs)
					{
						float * p1 = (float *)(cs + 0x54);
						float * p2 = (float *)(cs + 0x58);
						const float s = StepToward(*p1, camPitch0 - up * vSign, false, dt, maxRad);
						*p1 += s; *p2 = *p1;
					}
				}
			}
			else if(phase == 1)
			{
				// O golpe acabou: regressa ao rumo original (a menos que o jogador esteja a andar).
				if(g_cfg.returnToHeading && !g_moveHeld) { phase = 2; returnStart = now; }
				else phase = 0;
			}

			if(phase == 2 && !g_active)
			{
				const float returnRad = g_cfg.returnSpeedDeg * kPi / 180.0f;

				const float stepYaw = StepToward(rot.z, homeYaw, true, dt, returnRad);
				rot.z = WrapPi(rot.z + stepYaw);
				CameraYawCompensate(cs, stepYaw, camYaw0, camMode);
				bool done = fabsf(WrapPi(homeYaw - rot.z)) < 0.01f;

				if(g_cfg.verticalMode & 1)
				{
					rot.x += StepToward(rot.x, homePitch, false, dt, returnRad);
					if(fabsf(homePitch - rot.x) >= 0.01f) done = false;
				}
				if((g_cfg.verticalMode & 2) && cs)
				{
					float * p1 = (float *)(cs + 0x54);
					float * p2 = (float *)(cs + 0x58);
					*p1 += StepToward(*p1, camPitch0, false, dt, returnRad);
					*p2 = *p1;
					if(fabsf(camPitch0 - *p1) >= 0.01f) done = false;
				}

				if(done || g_moveHeld || (LONG)(now - returnStart) >= g_cfg.returnMaxMs) phase = 0;
			}
		}
		return 0;
	}

	// ------------------------------------------------------------------ escolha + arranque
	// Escolhe o alvo. Com 'extend' o giro e mantido durante 'windowMs'; sem 'extend' so se atualiza
	// o alvo de um giro que ja esta em curso.
	bool AcquireAndTurn(int windowMs, bool extend)
	{
		// Lock-on ativo: LockOnMode=0 deixa o SoftTarget em pausa.
		if(g_lockActive && g_cfg.lockOnMode == 0) return false;

		PlayerCharacter * player = Engine::Player();
		if(!player) return false;

		float yaw = 0.0f, pitchUp = 0.0f;
		UInt32 id = 0, handle = 0;
		if(!FindTarget(player, &yaw, &pitchUp, &id, &handle)) return false;

		if(g_cfg.verbose || id != g_lastTargetId)
			_MESSAGE("target: formID %08X, yaw %.0f deg, pitch %.0f deg%s", id, yaw * 180.0f / kPi, pitchUp * 180.0f / kPi, g_cfg.logOnly ? " (LogOnly)" : "");
		g_lastTargetId = id;

		if(g_cfg.logOnly) return true;

		g_targetYaw = yaw;
		g_targetPitchUp = pitchUp;
		g_targetHandle = (LONG)handle;
		g_hasPitch = g_cfg.verticalMode ? 1 : 0;
		if(extend || !g_active) g_endTick = GetTickCount() + (DWORD)windowMs;
		g_active = 1;
		return true;
	}

	// ------------------------------------------------------------------ diagnostico (so leitura)
	bool SafeReadWords(const void * src, UInt32 * dst, int count)
	{
		__try
		{
			for(int i = 0; i < count; ++i) dst[i] = ((const UInt32 *)src)[i];
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	DWORD g_lastProbe = 0;

	void ProbeTick()
	{
		const DWORD now = GetTickCount();
		if(now - g_lastProbe < 500) return;
		g_lastProbe = now;

		PlayerCharacter * player = Engine::Player();
		PlayerCamera * cam = PlayerCamera::GetSingleton();
		if(!player || !cam) return;

		const NiPoint3 rot = Engine::Rotation(player);
		TESCameraState * st = cam->cameraState;

		_MESSAGE("probe: state=%u camRotZ=%.3f camRotX=%.3f zoom=%.2f | player rotZ=%.3f rotX=%.3f lockActive=%d",
				 st ? (unsigned)st->stateId : 0xFFFFu, cam->rotZ, cam->rotX, cam->zoom,
				 rot.z, rot.x, (int)g_lockActive);

		if(st)
		{
			UInt32 w[48];
			if(SafeReadWords(st, w, 48))
			{
				char line[1400];
				int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "probe-state:");
				for(int i = 8; i < 48 && n > 0 && n < (int)sizeof(line) - 24; ++i)	// de 0x20 a 0xBC
				{
					float f;
					memcpy(&f, &w[i], 4);
					n += _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, " %02X=%.3f", i * 4, f);
				}
				_MESSAGE("%s", line);
			}
		}
	}

	// ------------------------------------------------------------------ botoes
	bool	g_holdLeft = false, g_holdRight = false, g_holdExtra = false;
	bool	g_kFwd = false, g_kBack = false, g_kLeft = false, g_kRight = false;
	DWORD	g_lastHoldEvent = 0;
	DWORD	g_lastScan = 0;

	bool InList(const int * list, int n, int code)
	{
		for(int i = 0; i < n; ++i) if(list[i] == code) return true;
		return false;
	}

	// Bloqueio continuo: re-escolhe o alvo de tempos a tempos enquanto o botao esta seguro.
	void HoldTick()
	{
		const DWORD now = GetTickCount();
		if(now - g_lastHoldEvent > 250) return;					// botao deixou de chegar: considera solto
		if(now - g_lastScan < (DWORD)g_cfg.retargetMs) return;
		g_lastScan = now;

		const int window = (2 * g_cfg.retargetMs > 120) ? 2 * g_cfg.retargetMs : 120;
		AcquireAndTurn(window, true);
	}

	// Durante um giro de toque: continua a reavaliar quem e o inimigo mais proximo.
	void RetargetTick()
	{
		const DWORD now = GetTickCount();
		if(now - g_lastScan < (DWORD)g_cfg.retargetMs) return;
		g_lastScan = now;
		AcquireAndTurn(0, false);
	}

	class InputSink : public BSTEventSink<InputEvent>
	{
	public:
		virtual EventResult ReceiveEvent(InputEvent ** events, InputEventDispatcher * dispatcher)
		{
			InputStringHolder * strings = Engine::UserEvents();
			if(!events || !*events || !strings) return kEvent_Continue;
			if(!g_cfg.enabled) return kEvent_Continue;

			MaybeReloadConfig();
			if(g_cfg.probe) ProbeTick();

			// Estado do lock-on (real, pela LockOn.dll, ou estimado pela tecla).
			g_lockActive = QueryLockOn() ? 1 : 0;
			if(g_lockActive && !g_sloIsLockOn)
			{
				// Sem a LockOn.dll: desliga-se sozinho quando ja nao ha inimigos em combate (2 s seguidos).
				PlayerCharacter * p = Engine::Player();
				UInt32 ids[8];
				if(p && CollectHostileIds(p, ids, NULL, 8) == 0)
				{
					if(!g_lockNoEnemySince) g_lockNoEnemySince = GetTickCount();
					else if(GetTickCount() - g_lockNoEnemySince > 2000) { g_lockKeyState = false; g_lockActive = 0; g_lockNoEnemySince = 0; _MESSAGE("lock-on assumed OFF (no enemies in combat)"); }
				}
				else g_lockNoEnemySince = 0;
			}
			if(g_lockActive && g_cfg.lockOnMode == 0) g_active = 0;

			bool holdBtnSeen = false;
			bool extraHeldNow = false;
			for(InputEvent * e = *events; e; e = e->next)
			{
				if(e->eventType != InputEvent::kEventType_Button) continue;

				ButtonEvent * button = (ButtonEvent *)e;
				const float value = *(float *)&button->flags;
				const bool held = (value != 0.0f);
				const bool pressedNow = held && (button->timer == 0.0f);

				const BSFixedString * control = button->GetControlID();

				// Diagnostico: regista cada toque de tecla (para descobrir codigos de tecla).
				if(g_cfg.probe && pressedNow)
					_MESSAGE("probe-key: device=%u code=%u control=%s", (unsigned)e->deviceType, (unsigned)button->keyMask, (control && control->data) ? control->data : "-");

				if(e->deviceType == kDeviceType_Keyboard)
				{
					const int code = (int)button->keyMask;

					// Tecla de reserva do lock-on (so quando nao ha LockOn.dll): cada toque liga/desliga.
					if(g_cfg.lockOnKey && !g_sloIsLockOn && pressedNow && code == g_cfg.lockOnKey)
					{
						g_lockKeyState = !g_lockKeyState;
						g_lockNoEnemySince = 0;
						g_lockActive = g_lockKeyState ? 1 : 0;
						if(g_lockActive && g_cfg.lockOnMode == 0) g_active = 0;
						_MESSAGE("lock-on key: lock-on assumed %s", g_lockKeyState ? "ON" : "OFF");
					}

					// Botoes extra: toque (ataque carregado, etc.) e segurar (bloqueio de armas duplas, etc.).
					if(pressedNow && g_cfg.tapAssist && InList(g_tapKeys, g_numTapKeys, code))
						AcquireAndTurn(g_cfg.holdMs, true);
					if(InList(g_holdKeys, g_numHoldKeys, code))
					{
						g_holdExtra = held;
						if(held) { g_lastHoldEvent = GetTickCount(); holdBtnSeen = true; extraHeldNow = true; }
					}
				}

				if(!control) continue;

				// Teclas de movimento (o regresso ao rumo nao corre contra o TDM enquanto andas).
				if(*control == strings->forward)			g_kFwd = held;
				else if(*control == strings->back)			g_kBack = held;
				else if(*control == strings->strafeLeft)	g_kLeft = held;
				else if(*control == strings->strafeRight)	g_kRight = held;
				g_moveHeld = (g_kFwd || g_kBack || g_kLeft || g_kRight) ? 1 : 0;

				const bool isRight = (*control == strings->rightAttack);
				const bool isLeft  = (*control == strings->leftAttack);
				if(!isRight && !isLeft) continue;

				if(isRight) g_holdRight = held; else g_holdLeft = held;

				if(held && (((g_cfg.holdMode & 2) && isRight) || ((g_cfg.holdMode & 1) && isLeft)))
				{
					g_lastHoldEvent = GetTickCount();
					holdBtnSeen = true;
				}

				if(pressedNow && g_cfg.tapAssist)
					AcquireAndTurn(g_cfg.holdMs, true);
			}

			const bool holding = ((g_cfg.holdMode & 1) && g_holdLeft) || ((g_cfg.holdMode & 2) && g_holdRight) || g_holdExtra;
			if(holding && (holdBtnSeen || extraHeldNow || GetTickCount() - g_lastHoldEvent <= 250)) HoldTick();
			else if(g_active) RetargetTick();

			return kEvent_Continue;
		}
	};
	InputSink g_sink;

	void OnMessage(SKSEMessagingInterface::Message * message)
	{
		if(!message) return;

		switch(message->type)
		{
			case SKSEMessagingInterface::kMessage_DataLoaded:
				ResolveIniPath();
				LoadConfig();
				ResolveExternal();
				break;

			case SKSEMessagingInterface::kMessage_InputLoaded:
			{
				InputEventDispatcher * dispatcher = *g_inputEventDispatcher;
				if(!dispatcher) { _MESSAGE("input dispatcher not available - the mod will not work"); break; }
				dispatcher->AddEventSink(&g_sink);
				CreateThread(NULL, 0, TurnThread, NULL, 0, NULL);
				_MESSAGE("input receiver registered, turn thread started");
				break;
			}

			default:
				break;
		}
	}
}

extern "C"
{

bool SKSEPlugin_Query(const SKSEInterface * skse, PluginInfo * info)
{
	gLog.OpenRelative(CSIDL_MYDOCUMENTS, PLUGIN_LOG_FILE);
	_MESSAGE("SoftTarget LE " PLUGIN_VERSION_STR " (built " __DATE__ " " __TIME__ ")");

	info->infoVersion	= PluginInfo::kInfoVersion;
	info->name			= PLUGIN_NAME;
	info->version		= 1;

	g_pluginHandle = skse->GetPluginHandle();

	if(skse->isEditor) { _MESSAGE("ABORTING: loaded in the editor"); return false; }

	_MESSAGE("game runtime %08X (1.9.32.0 is %08X)", skse->runtimeVersion, RUNTIME_VERSION_1_9_32_0);
	if(skse->runtimeVersion != RUNTIME_VERSION_1_9_32_0)
	{
		_MESSAGE("ABORTING: unsupported runtime");
		return false;
	}
	return true;
}

bool SKSEPlugin_Load(const SKSEInterface * skse)
{
	g_messaging = (SKSEMessagingInterface *)skse->QueryInterface(kInterface_Messaging);
	if(!g_messaging || !g_messaging->RegisterListener(g_pluginHandle, "SKSE", OnMessage))
	{
		_MESSAGE("ABORTING: the messaging interface is not available");
		return false;
	}

	g_papyrus = (SKSEPapyrusInterface *)skse->QueryInterface(kInterface_Papyrus);
	if(g_papyrus && g_papyrus->Register(RegisterPapyrus))
		_MESSAGE("papyrus interface: registration queued");
	else
		_MESSAGE("papyrus interface not available - the in-game menu will not work (the .ini still does)");

	_MESSAGE("running");
	return true;
}

};
