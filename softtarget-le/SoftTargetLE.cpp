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
#include "skse/GameObjects.h"
#include "skse/GameMenus.h"
#include "skse/NiNodes.h"
#include "skse/PapyrusVM.h"
#include "skse/PapyrusNativeFunctions.h"
#include "skse/Hooks_UI.h"
#include "skse/ScaleformMovie.h"
#include "skse/ScaleformCallbacks.h"
#include "Log.h"
#include "Engine.h"

#include <shlobj.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define PLUGIN_NAME			"SoftTargetLE"
#define PLUGIN_VERSION_STR	"0.12.3 (FreeCam sem yield, rotacao do input, sprint mantem a mira, previsao com interceptacao, ajuste do ponto de mira)"
#define PLUGIN_LOG_FILE		"\\My Games\\Skyrim\\SKSE\\SoftTargetLE.log"
#define PLUGIN_INI_NAME		"SoftTargetLE.ini"
#define PLUGIN_INI_FALLBACK	"Data\\SKSE\\Plugins\\SoftTargetLE.ini"

static PluginHandle				g_pluginHandle	= kPluginHandle_Invalid;
static SKSEMessagingInterface *	g_messaging		= NULL;
static SKSEPapyrusInterface *	g_papyrus		= NULL;
static SKSETaskInterface *		g_taskIface		= NULL;

// O SKSE declara LookupREFRByHandle mas so a define em GameReferences.cpp, que nao compilamos.
// Definimos aqui com o endereco da LE 1.9.32.0 (confirmado no FreeClimb LE).
const _LookupREFRByHandle LookupREFRByHandle = (_LookupREFRByHandle)0x004A9180;
// Cria o "handle" de uma referencia (visto na LockOn.dll; assinatura do SKSE: saida, referencia).
const _CreateRefHandleByREFR CreateRefHandleByREFR = (_CreateRefHandleByREFR)0x0065CC00;

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
		float	returnSmoothing;// suavizacao do regresso (menor = mais lento)
		int		attackPulseMs;	// perfil TDM: ao atacar a andar, vira para o alvo durante este tempo
		int		lockOnKey;		// codigo da tecla do lock-on (reserva se a LockOn.dll nao existir)
		int		lockOnMode;		// com lock-on ativo: 0 = pausa o SoftTarget; 1 = so corpo, sem mexer na camera; 2 = pausa e troca o lock-on para o inimigo mais proximo
		int		coeusYield;		// 1 = enquanto vira, pede ao Coeus para largar a rotacao do corpo
		int		verticalMode;	// mira vertical: 0 = off, 1 = so o corpo (bracos); a camera nunca e tocada
		float	aimHeight;		// altura do ponto de mira no alvo, acima dos pes
		float	playerAimHeight;// altura de onde o jogador "mira", acima dos pes
		int		verticalInvert;	// 1 = inverte o sentido da inclinacao
		float	verticalLimitDeg;// inclinacao maxima, em graus
		int		language;		// idioma do menu: 0 = ingles, 1 = portugues
		int		profileMode;	// 0 = normal (sem TDM, ou Coeus com EnableUnsheathed=0), 1 = TDM (movimento direcional)
		int		lockOnSwitchKey;// tecla para trocar de alvo no lock-on (0 = a que a LockOn.dll indica)
		int		allowReanimated;// 1 = aceita inimigos reanimados / sem estado de vida normal
		// ---- lock-on proprio
		int		lockEnabled;	// 1 = lock-on proprio ligado
		int		lockKey;		// tecla que liga/desliga (33 = F; 256 + botao do rato: 258 = M3)
		int		lockSwitchKey;	// tecla que troca de alvo (258 = M3)
		float	lockRange;		// alcance maximo do lock-on
		float	lockAcquireDeg;	// angulo maximo entre a mira e o corpo do alvo para o poder prender
		int		lockHostileOnly;// 1 = so prende inimigos
		int		lockLoseSightMs;// tempo sem ver o alvo ate o lock-on largar
		float	lockLoseAngleDeg;// angulo a partir do qual o alvo conta como fora da vista
		int		lockAutoNext;	// 1 = quando o alvo morre, passa para o inimigo mais proximo
		int		lockCameraFollow;// 1 = (perfil normal) a camera acompanha o alvo
		int		lockCameraPitch;// 1 = a camera tambem sobe/desce para o alvo
		float	lockCameraSmooth;// velocidade da camera a seguir o alvo (maior = mais rapido)
		int		predictEnabled;	// 1 = previsao do disparo com o lock-on ativo
		float	projectileSpeed;// velocidade do projetil, unidades por segundo
		float	projectileGravity;// queda do projetil (unidades por segundo ao quadrado; 0 = sem compensacao)
		float	predictStrength;// 0..100: quanto da previsao se aplica
		float	predictMinDist;	// abaixo desta distancia nao ha previsao
		// ---- v0.12.0
		int		lockCameraAxis;	// eixo da camera que aponta para a frente (0..11); 1 = coluna Y da matriz (confirmado nos logs)
		float	lockAutoCircle;	// escolha automatica: qualquer direcao ate esta distancia
		float	lockAutoCone;	// escolha automatica: alem do circulo, so dentro deste angulo (cada lado) a frente da camera
		int		cameraHoldTdm;	// perfil TDM: quando o corpo vira, a camera fica parada (deslocamento compensado)
		int		createHandles;	// 1 = cria o handle de alvos que o jogo nao lista como hostis (o Coeus precisa dele)
		int		dotLock;		// 1 = mostra o ponto no alvo do lock-on
		int		dotSoft;		// 1 = mostra o ponto no alvo do SoftTarget
		int		dotSkin;		// 0 = ponto, 1 = anel e ponto, 2 = reticula do Simple Lock-On (.swf), 3 = reticula do TDM (.swf)
		float	dotSize;		// tamanho, % (100 = normal)
		int		dotColor;		// 0 branco, 1 vermelho, 2 amarelo, 3 ciano, 4 verde, 5 laranja
		float	dotOpacity;		// opacidade, %
		int		dotTdmType;		// tipo da reticula do TDM (0..2)
		// ---- v0.12.3
		int		moveRemap;		// perfil FreeCam: o corpo encara o alvo e o input de andar e rodado para continuar relativo ao ecra
		int		sprintKeepsAim;	// perfil FreeCam: correr nao larga a mira nem o lock-on
		int		mvRegion;		// onde esta o vetor de movimento (-1 = descobrir sozinho; valor do registo)
		int		mvOffset;		// deslocamento em bytes dentro da regiao
		float	aimOffX;		// graus: o alvo fica a direita (+) ou a esquerda (-) do centro (camera do lock, perfil normal)
		float	aimOffY;		// graus: o alvo fica acima (+) ou abaixo (-) do centro
		float	spellSpeed;		// velocidade de reserva dos feiticos, unidades por segundo
		float	speedScale;		// % aplicada a velocidade do projetil (ajuste fino da previsao)
		int		useFormSpeed;	// 1 = le a velocidade do feitico nos dados do jogo
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
		{ "ReturnToHeading",		'i', &g_cfg.returnToHeading,	0.0f },
		{ "ReturnSpeedDegPerSec",	'f', &g_cfg.returnSpeedDeg,		360.0f },
		{ "ReturnSmoothing",		'f', &g_cfg.returnSmoothing,	3.0f },
		{ "AttackPulseMs",			'i', &g_cfg.attackPulseMs,		350.0f },
		{ "LockOnKey",				'i', &g_cfg.lockOnKey,			0.0f },
		{ "LockOnMode",				'i', &g_cfg.lockOnMode,			0.0f },
		{ "CoeusYield",				'i', &g_cfg.coeusYield,			0.0f },
		{ "VerticalMode",			'i', &g_cfg.verticalMode,		0.0f },
		{ "AimHeight",				'f', &g_cfg.aimHeight,			100.0f },
		{ "PlayerAimHeight",		'f', &g_cfg.playerAimHeight,	110.0f },
		{ "VerticalInvert",			'i', &g_cfg.verticalInvert,		0.0f },
		{ "VerticalLimitDegrees",	'f', &g_cfg.verticalLimitDeg,	45.0f },
		{ "Language",				'i', &g_cfg.language,			0.0f },
		{ "ProfileMode",			'i', &g_cfg.profileMode,		0.0f },
		{ "LockOnSwitchKey",		'i', &g_cfg.lockOnSwitchKey,	0.0f },
		{ "AllowReanimated",		'i', &g_cfg.allowReanimated,	1.0f },
		{ "LockEnabled",			'i', &g_cfg.lockEnabled,		1.0f },
		{ "LockKey",				'i', &g_cfg.lockKey,			33.0f },
		{ "LockSwitchKey",			'i', &g_cfg.lockSwitchKey,		258.0f },
		{ "LockRange",				'f', &g_cfg.lockRange,			4000.0f },
		{ "LockAcquireDegrees",		'f', &g_cfg.lockAcquireDeg,		30.0f },
		{ "LockHostileOnly",		'i', &g_cfg.lockHostileOnly,	1.0f },
		{ "LockLoseSightMs",		'i', &g_cfg.lockLoseSightMs,	1500.0f },
		{ "LockLoseAngleDegrees",	'f', &g_cfg.lockLoseAngleDeg,	75.0f },
		{ "LockAutoNext",			'i', &g_cfg.lockAutoNext,		1.0f },
		{ "LockCameraFollow",		'i', &g_cfg.lockCameraFollow,	1.0f },
		{ "LockCameraPitch",		'i', &g_cfg.lockCameraPitch,	1.0f },
		{ "LockCameraSmooth",		'f', &g_cfg.lockCameraSmooth,	8.0f },
		{ "PredictEnabled",			'i', &g_cfg.predictEnabled,		1.0f },
		{ "ProjectileSpeed",		'f', &g_cfg.projectileSpeed,	4500.0f },
		{ "ProjectileGravity",		'f', &g_cfg.projectileGravity,	450.0f },
		{ "PredictStrength",		'f', &g_cfg.predictStrength,	100.0f },
		{ "PredictMinDistance",		'f', &g_cfg.predictMinDist,		150.0f },
		{ "LockCameraAxis",			'i', &g_cfg.lockCameraAxis,		1.0f },
		{ "LockAutoCircleRange",	'f', &g_cfg.lockAutoCircle,		900.0f },
		{ "LockAutoConeDegrees",	'f', &g_cfg.lockAutoCone,		60.0f },
		{ "CameraHoldTdm",			'i', &g_cfg.cameraHoldTdm,		1.0f },
		{ "CreateHandles",			'i', &g_cfg.createHandles,		1.0f },
		{ "DotLock",				'i', &g_cfg.dotLock,			1.0f },
		{ "DotSoftTarget",			'i', &g_cfg.dotSoft,			0.0f },
		{ "DotSkin",				'i', &g_cfg.dotSkin,			1.0f },
		{ "DotSize",				'f', &g_cfg.dotSize,			100.0f },
		{ "DotColor",				'i', &g_cfg.dotColor,			0.0f },
		{ "DotOpacity",				'f', &g_cfg.dotOpacity,			100.0f },
		{ "DotTdmType",				'i', &g_cfg.dotTdmType,			0.0f },
		{ "MoveRemap",				'i', &g_cfg.moveRemap,			1.0f },
		{ "SprintKeepsAim",			'i', &g_cfg.sprintKeepsAim,		1.0f },
		{ "MoveVecRegion",			'i', &g_cfg.mvRegion,			-1.0f },
		{ "MoveVecOffset",			'i', &g_cfg.mvOffset,			0.0f },
		{ "AimOffsetX",				'f', &g_cfg.aimOffX,			0.0f },
		{ "AimOffsetY",				'f', &g_cfg.aimOffY,			0.0f },
		{ "SpellProjectileSpeed",	'f', &g_cfg.spellSpeed,			2500.0f },
		{ "ProjectileSpeedScale",	'f', &g_cfg.speedScale,			100.0f },
		{ "UseSpellDataSpeed",		'i', &g_cfg.useFormSpeed,		1.0f },
	};
	const int kNumOpts = sizeof(g_opts) / sizeof(g_opts[0]);

	// Teclas extra (codigos de teclado separados por virgula): toque e segurar.
	const int kMaxKeys = 8;
	int g_tapKeys[kMaxKeys];	int g_numTapKeys = 0;
	int g_holdKeys[kMaxKeys];	int g_numHoldKeys = 0;
	int g_shiftKeys[kMaxKeys];	int g_numShiftKeys = 0;

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
		g_numShiftKeys = ParseKeys("ShiftKeys", g_shiftKeys, kMaxKeys);
		if(g_numShiftKeys == 0) { g_shiftKeys[0] = 42; g_shiftKeys[1] = 54; g_numShiftKeys = 2; }	// Shift esquerdo e direito
		g_cfg.verticalMode &= 1;
		if(g_cfg.lockRange < 300.0f)		g_cfg.lockRange = 300.0f;
		if(g_cfg.lockAcquireDeg < 2.0f)		g_cfg.lockAcquireDeg = 2.0f;
		if(g_cfg.lockAcquireDeg > 90.0f)	g_cfg.lockAcquireDeg = 90.0f;
		if(g_cfg.lockLoseAngleDeg < 20.0f)	g_cfg.lockLoseAngleDeg = 20.0f;
		if(g_cfg.lockLoseAngleDeg > 170.0f)	g_cfg.lockLoseAngleDeg = 170.0f;
		if(g_cfg.lockLoseSightMs < 0)		g_cfg.lockLoseSightMs = 0;
		if(g_cfg.lockCameraSmooth < 1.0f)	g_cfg.lockCameraSmooth = 1.0f;
		if(g_cfg.projectileSpeed < 500.0f)	g_cfg.projectileSpeed = 500.0f;
		if(g_cfg.spellSpeed < 300.0f)		g_cfg.spellSpeed = 300.0f;
		if(g_cfg.speedScale < 20.0f)		g_cfg.speedScale = 20.0f;
		if(g_cfg.speedScale > 300.0f)		g_cfg.speedScale = 300.0f;
		if(g_cfg.aimOffX < -20.0f)			g_cfg.aimOffX = -20.0f;
		if(g_cfg.aimOffX >  20.0f)			g_cfg.aimOffX =  20.0f;
		if(g_cfg.aimOffY < -20.0f)			g_cfg.aimOffY = -20.0f;
		if(g_cfg.aimOffY >  20.0f)			g_cfg.aimOffY =  20.0f;
		if(g_cfg.predictStrength < 0.0f)	g_cfg.predictStrength = 0.0f;
		if(g_cfg.predictStrength > 150.0f)	g_cfg.predictStrength = 150.0f;
		if(g_cfg.lockCameraAxis < 0 || g_cfg.lockCameraAxis > 11) g_cfg.lockCameraAxis = 1;
		if(g_cfg.lockAutoCircle < 100.0f)	g_cfg.lockAutoCircle = 100.0f;
		if(g_cfg.lockAutoCone < 5.0f)		g_cfg.lockAutoCone = 5.0f;
		if(g_cfg.lockAutoCone > 180.0f)		g_cfg.lockAutoCone = 180.0f;
		if(g_cfg.dotSkin < 0 || g_cfg.dotSkin > 3) g_cfg.dotSkin = 0;
		if(g_cfg.dotColor < 0 || g_cfg.dotColor > 5) g_cfg.dotColor = 0;
		if(g_cfg.dotSize < 30.0f)			g_cfg.dotSize = 30.0f;
		if(g_cfg.dotSize > 400.0f)			g_cfg.dotSize = 400.0f;
		if(g_cfg.dotOpacity < 10.0f)		g_cfg.dotOpacity = 10.0f;
		if(g_cfg.dotOpacity > 100.0f)		g_cfg.dotOpacity = 100.0f;
		if(g_cfg.dotTdmType < 0 || g_cfg.dotTdmType > 2) g_cfg.dotTdmType = 0;

		if(g_cfg.updateMs < 1)		g_cfg.updateMs = 1;
		if(g_cfg.updateMs > 50)		g_cfg.updateMs = 50;
		if(g_cfg.retargetMs < 16)	g_cfg.retargetMs = 16;

		_MESSAGE("config (%s%s): enabled=%d logOnly=%d shape=%s range=%.0f cone=%.0f turn=%.0f smooth=%.1f update=%dms tap=%d hold=%d retarget=%dms holdMs=%d combat=%d lock=%d lockKey=%d lockMode=%d yield=%d hostileList=%d return=%d vertical=%d switchMargin=%.0f tapKeys=%d holdKeys=%d lang=%d",
				 found ? "ini: " : "NO ini found, defaults; looked for: ", g_iniPath[0] ? g_iniPath : PLUGIN_INI_NAME,
				 g_cfg.enabled, g_cfg.logOnly, g_cfg.shape ? "circle" : "cone", g_cfg.range, g_cfg.coneDeg,
				 g_cfg.turnSpeedDeg, g_cfg.smoothing, g_cfg.updateMs, g_cfg.tapAssist, g_cfg.holdMode,
				 g_cfg.retargetMs, g_cfg.holdMs, g_cfg.requireCombat, g_cfg.lockEnabled, g_cfg.lockOnKey, g_cfg.lockOnMode,
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
	FnVoidBool		g_coeusIsLock = NULL;		// Coeus.dll: Coeus_IsLockOnActive (so diagnostico)
	FnVoidBool		g_coeusIsExt = NULL;		// Coeus.dll: Coeus_IsExternalLockOnActive (so diagnostico)
	typedef UInt32 (*FnVoidU32)(void);
	typedef int    (*FnVoidInt)(void);
	FnVoidU32		g_sloGetTarget	= NULL;		// LockOn.dll: SLO_GetTargetRefHandle
	FnVoidInt		g_sloSwitchKey	= NULL;		// LockOn.dll: SLO_GetSwitchTargetKeyCode
	volatile LONG	g_assistOff		= 0;		// a ajuda do lock-on desliga-se sozinha se a tecla desligar o lock-on

	void ResolveExternal()
	{
		HMODULE h = GetModuleHandleA("LockOn.dll");
		if(h)
		{
			g_sloIsLockOn = (FnVoidBool)GetProcAddress(h, "SLO_IsLockOnActive");
			g_sloGetTarget = (FnVoidU32)GetProcAddress(h, "SLO_GetTargetRefHandle");
			g_sloSwitchKey = (FnVoidInt)GetProcAddress(h, "SLO_GetSwitchTargetKeyCode");
		}
		h = GetModuleHandleA("Coeus.dll");
		if(h) g_coeusExternal = (FnSetExternal)GetProcAddress(h, "Coeus_SetExternalLockOnActive");
		if(h)
		{
			g_coeusDirActive = (FnVoidBool)GetProcAddress(h, "Coeus_IsDirectionalMovementActive");
			g_coeusIsLock = (FnVoidBool)GetProcAddress(h, "Coeus_IsLockOnActive");
			g_coeusIsExt = (FnVoidBool)GetProcAddress(h, "Coeus_IsExternalLockOnActive");
		}
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
	volatile LONG	g_lockOwn = 0;			// 1 = o lock-on PROPRIO do mod esta ligado
	// Com lock-on ativo o SoftTarget fica em pausa (modos 0 e 2); o modo 1 so vira o corpo.
	bool LockPauses() { return g_lockActive && (g_cfg.lockOnMode == 0 || g_cfg.lockOnMode == 2); }

	bool QueryLockOn()
	{
		if(g_sloIsLockOn)
		{
			__try { return g_sloIsLockOn(); }
			__except(EXCEPTION_EXECUTE_HANDLER) { g_sloIsLockOn = NULL; return false; }
		}
		return g_lockKeyState;
	}

	// Perfil escolhido no menu: true = TDM (movimento direcional do Coeus com armas na mao).
	bool InCoeusProfile() { return g_cfg.profileMode == 1; }

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

	// Perfil do utilizador: copia das opcoes em SoftTargetLE_User.ini (ao lado do .ini principal).
	bool UserIniPath(char * out, size_t n)
	{
		if(!g_iniPath[0]) return false;
		_snprintf_s(out, n, _TRUNCATE, "%s", g_iniPath);
		char * dot = strrchr(out, '.');
		if(!dot) return false;
		_snprintf_s(dot, n - (size_t)(dot - out), _TRUNCATE, "_User.ini");
		return true;
	}

	void N_SaveProfile(StaticFunctionTag *)
	{
		char path[MAX_PATH];
		if(!UserIniPath(path, sizeof(path))) { _MESSAGE("menu: cannot save the user profile (no ini path)"); return; }
		for(int i = 0; i < kNumOpts; ++i)
		{
			char buf[48];
			if(g_opts[i].type == 'i') _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%d", *(int *)g_opts[i].ptr);
			else					  _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.3f", *(float *)g_opts[i].ptr);
			WritePrivateProfileStringA("SoftTarget", g_opts[i].key, buf, path);
		}
		_MESSAGE("menu: user profile saved (%s)", path);
	}

	// Carrega o perfil do utilizador por cima das opcoes atuais. Devolve 1 se existia, 0 se nao.
	SInt32 N_LoadProfile(StaticFunctionTag *)
	{
		char path[MAX_PATH];
		if(!UserIniPath(path, sizeof(path)) || !FileExists(path)) { _MESSAGE("menu: no saved user profile"); return 0; }
		for(int i = 0; i < kNumOpts; ++i)
		{
			char cur[48], buf[48];
			if(g_opts[i].type == 'i') _snprintf_s(cur, sizeof(cur), _TRUNCATE, "%d", *(int *)g_opts[i].ptr);
			else					  _snprintf_s(cur, sizeof(cur), _TRUNCATE, "%.3f", *(float *)g_opts[i].ptr);
			GetPrivateProfileStringA("SoftTarget", g_opts[i].key, cur, buf, sizeof(buf), path);
			const float v = (float)atof(buf);
			if(g_opts[i].type == 'i') *(int *)g_opts[i].ptr = (int)(v + (v < 0 ? -0.5f : 0.5f));
			else					  *(float *)g_opts[i].ptr = v;
		}
		N_SaveAll(NULL);
		_MESSAGE("menu: user profile loaded");
		return 1;
	}

	int DotFilesFound();

	// Estado para o menu: 0 = perfil efetivo (1 sem TDM, 2 Coeus), 1 = LockOn.dll, 2 = Coeus.dll (0/1), 3 = lock-on ativo
	SInt32 N_Status(StaticFunctionTag *, SInt32 what)
	{
		switch(what)
		{
			case 0: return InCoeusProfile() ? 2 : 1;
			case 1: return g_sloIsLockOn ? 1 : 0;
			case 2: return (g_coeusExternal || g_coeusDirActive) ? 1 : 0;
			case 3: return (g_lockActive || g_lockOwn) ? 1 : 0;
			case 4: return DotFilesFound();
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
		registry->RegisterFunction(new NativeFunction0 <StaticFunctionTag, void>("SaveProfile", "SoftTargetNative", N_SaveProfile, registry));
		registry->RegisterFunction(new NativeFunction0 <StaticFunctionTag, SInt32>("LoadProfile", "SoftTargetNative", N_LoadProfile, registry));
		registry->RegisterFunction(new NativeFunction0 <StaticFunctionTag, void>("ResetDefaults", "SoftTargetNative", N_ResetDefaults, registry));
		registry->RegisterFunction(new NativeFunction1 <StaticFunctionTag, SInt32, SInt32>("Status", "SoftTargetNative", N_Status, registry));
		_MESSAGE("papyrus functions registered (SoftTargetNative)");
		return true;
	}

	// ------------------------------------------------------------------ escolha do alvo
	UInt32 g_lastTargetId = 0;
	volatile LONG	g_endReason = 0;		// por que a ultima sessao acabou: 0 tempo, 1 correr, 2 lock-on externo, 3 sem alvo, 4 lock-on proprio desligado
	volatile LONG	g_forceCircle = 0;	// 1 = ignora o cone (usado pela ajuda do lock-on)
	volatile LONG	g_active	= 0;		// 1 = sessao de combate em curso (o corpo encara o alvo mais proximo)
	volatile LONG	g_shiftHeld = 0;		// Shift (ou outra tecla de ShiftKeys) carregado
	volatile DWORD	g_lastAttackTick = 0;	// ultimo toque de ataque
	volatile LONG	g_yieldNow = 0;			// 1 = o pedido ao Coeus (lock externo) esta ligado
	float			g_findCenter[3] = { 0.0f, 0.0f, 0.0f };		// centro do corpo do alvo do SoftTarget (para o ponto no ecra)
	float			g_lockCenter[3] = { 0.0f, 0.0f, 0.0f };		// centro do corpo do alvo do lock-on

	// Handle de uma referencia (0 se falhar). O jogo so lista os hostis; para os outros cria-se aqui.
	UInt32 HandleFor(Actor * a)
	{
		if(!a || !g_cfg.createHandles) return 0;
		__try
		{
			UInt32 h = 0;
			CreateRefHandleByREFR(&h, (TESObjectREFR *)a);
			return h;
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
	}

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

	// Posicao de cada no do esqueleto do ator (o "corpo"): devolve quantos pontos escreveu em pts (x,y,z).
	int CollectPoints(Actor * actor, float * pts, int maxPts)
	{
		NiNode * root = actor->GetNiNode();
		if(!root) return 0;
		const NiPoint3 pos = Engine::Position(actor);
		NiNode * stack[96];
		int sp = 0, n = 0, visited = 0;
		stack[sp++] = root;
		while(sp > 0 && n < maxPts && visited < 700)
		{
			NiNode * nd = stack[--sp];
			++visited;
			const NiPoint3 & p = nd->m_worldTransform.pos;
			const float ex = p.x - pos.x, ey = p.y - pos.y, ez = p.z - pos.z;
			if(ex * ex + ey * ey + ez * ez < 2000.0f * 2000.0f)			// ignora nos perdidos na origem do mundo
			{
				pts[n * 3] = p.x; pts[n * 3 + 1] = p.y; pts[n * 3 + 2] = p.z;
				++n;
			}
			const NiTArray<NiAVObject *> & ch = nd->m_children;
			for(UInt16 i = 0; i < ch.m_emptyRunStart; ++i)
			{
				NiAVObject * c = ch.m_data[i];
				if(!c) continue;
				NiNode * cn = c->GetAsNiNode();
				if(cn && sp < 96) stack[sp++] = cn;
			}
		}
		return n;
	}

	DWORD g_lastActorLog = 0;

	// Devolve true se encontrou um alvo; escreve a guinada, a inclinacao (positiva = para cima),
	// o formID e o handle. O alvo e SEMPRE o inimigo mais proximo, medido pela parte do corpo mais perto
	// de ti (qualquer osso: cabeca, pernas, asas, cauda) e nao pelo centro; a guinada aponta para essa parte.
	bool FindTarget(Actor * player, float * outYaw, float * outPitchUp, UInt32 * outId, UInt32 * outHandle)
	{
		TESObjectCELL * cell = Engine::ParentCell(player);
		if(!cell) return false;

		const NiPoint3 me		= Engine::Position(player);
		const float facing		= Engine::Rotation(player).z;
		const float halfCone	= (g_cfg.coneDeg * 0.5f) * kPi / 180.0f;
		const bool  cone		= (g_cfg.shape == 0) && !g_forceCircle;
		const float limitRad	= g_cfg.verticalLimitDeg * kPi / 180.0f;

		UInt32 hostileIds[64], hostileHandles[64];
		const int hostileCount = CollectHostileIds((PlayerCharacter *)player, hostileIds, hostileHandles, 64);

		bool dbg = false;
		int dbgLines = 0;
		if(g_cfg.probe && GetTickCount() - g_lastActorLog > 1000) { dbg = true; g_lastActorLog = GetTickCount(); }

		float bestScore = 1e30f;
		Actor * bestActor = NULL;
		int bestListed = -1;
		float bestYaw = 0.0f, bestC[3] = { 0.0f, 0.0f, 0.0f }, bestNear = 0.0f;

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
			const float cdx = pos.x - me.x;
			const float cdy = pos.y - me.y;
			const float cdist = sqrtf(cdx * cdx + cdy * cdy);
			if(cdist > g_cfg.range + 800.0f) continue;						// o corpo pode ser enorme (dragoes): margem larga

			const UInt32 life = Engine::LifeState(actor);

			int listedAt = -1;
			if(g_cfg.useHostileList)
				for(int j = 0; j < hostileCount; ++j) if(hostileIds[j] == actor->formID) { listedAt = j; break; }
			const bool listed = (listedAt >= 0);

			// Hostil ao jogador: funcao do proprio jogo OU presente na lista de hostis do jogador.
			const bool hostile = listed || CALL_MEMBER_FN(actor, IsHostileToActor)(player);

			// Vivos (0) ou, se permitido, em "reanimacao" (4); o resto (morto, a morrer, inconsciente...) nao.
			if(life != 0 && !(g_cfg.allowReanimated && life == 4)) continue;
			if(!hostile) continue;
			if(g_cfg.requireCombat && !listed && !actor->IsInCombat()) continue;

			// Corpo: ponto mais perto (na horizontal) e centro do corpo.
			float pts[3 * 128];
			const int n = CollectPoints(actor, pts, 128);
			float nd = cdist, nx = pos.x, ny = pos.y, nz = pos.z;
			float c[3] = { pos.x, pos.y, pos.z + g_cfg.aimHeight };
			if(n >= 3)
			{
				nd = 1e30f;
				float sx = 0.0f, sy = 0.0f, zmin = 1e30f, zmax = -1e30f;
				for(int q = 0; q < n; ++q)
				{
					const float qx = pts[q * 3], qy = pts[q * 3 + 1], qz = pts[q * 3 + 2];
					const float ddx = qx - me.x, ddy = qy - me.y;
					const float dd = sqrtf(ddx * ddx + ddy * ddy);
					if(dd < nd) { nd = dd; nx = qx; ny = qy; nz = qz; }
					sx += qx; sy += qy;
					if(qz < zmin) zmin = qz;
					if(qz > zmax) zmax = qz;
				}
				c[0] = sx / n; c[1] = sy / n; c[2] = zmin + 0.65f * (zmax - zmin);		// tronco, nao a media dos ossos
			}

			if(dbg && dbgLines < 8)
			{
				++dbgLines;
				_MESSAGE("probe-actor: formID %08X center-dist %.0f body-dist %.0f points %d life=%u hostile=%d listed=%d", actor->formID, cdist, nd, n, (unsigned)life, hostile ? 1 : 0, listed ? 1 : 0);
			}

			if(nd > g_cfg.range) continue;
			if(fabsf(nz - me.z) > g_cfg.maxHeightDiff && fabsf(c[2] - me.z) > g_cfg.maxHeightDiff) continue;

			// Guinada para a parte mais perto (se estiver colada ao jogador, para o centro, para nao tremer).
			float yaw = (nd > 25.0f) ? atan2f(nx - me.x, ny - me.y) : atan2f(c[0] - me.x, c[1] - me.y);
			const float off = fabsf(WrapPi(yaw - facing));

			float score;
			if(cone)
			{
				if(off > halfCone) continue;
				score = nd * (1.0f + off / (kPi * 0.5f));
			}
			else
			{
				score = nd;													// circulo: o mais proximo, em qualquer direcao
			}

			// Margem opcional de troca (0 = sempre o mais proximo).
			if(actor->formID == g_lastTargetId && g_cfg.switchMargin > 0.0f) score *= (1.0f - g_cfg.switchMargin / 100.0f);

			if(score < bestScore)
			{
				bestScore = score;
				bestActor = actor;
				bestListed = listedAt;
				bestYaw = yaw;
				bestNear = nd;
				bestC[0] = c[0]; bestC[1] = c[1]; bestC[2] = c[2];
			}
		}

		if(!bestActor) return false;

		*outYaw = bestYaw;
		*outId = bestActor->formID;
		*outHandle = (bestListed >= 0) ? hostileHandles[bestListed] : HandleFor(bestActor);

		const float hx = bestC[0] - me.x, hy = bestC[1] - me.y;
		const float hd = sqrtf(hx * hx + hy * hy);
		float up = atan2f(bestC[2] - (me.z + g_cfg.playerAimHeight), hd > 1.0f ? hd : 1.0f);
		if(up >  limitRad) up =  limitRad;
		if(up < -limitRad) up = -limitRad;
		*outPitchUp = up;
		g_findCenter[0] = bestC[0]; g_findCenter[1] = bestC[1]; g_findCenter[2] = bestC[2];
		(void)bestNear;
		return true;
	}

	// Chamada tambem pela thread de rotacao: protegida contra leituras invalidas.
	bool SafeFindTarget(Actor * player, float * outYaw, float * outPitchUp, UInt32 * outId, UInt32 * outHandle)
	{
		__try { return FindTarget(player, outYaw, outPitchUp, outId, outHandle); }
		__except(EXCEPTION_EXECUTE_HANDLER) { return false; }
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
	float StepToward(float cur, float target, bool wrap, float dt, float maxSpeedRad, float smooth)
	{
		const float delta = wrap ? WrapPi(target - cur) : (target - cur);
		const float maxStep = maxSpeedRad * dt;
		float want = delta;
		if(smooth > 0.0f) want = delta * (1.0f - expf(-smooth * dt));
		if(want >  maxStep) want =  maxStep;
		if(want < -maxStep) want = -maxStep;
		return want;
	}


	// Camera de deslocamento (perfil TDM): com a rotacao livre ligada (byte 0xB4, ligado pelo Coeus) a camera fica em
	// corpo + deslocamento (0xAC). Quando NOS giramos o corpo 'step', tira-se o mesmo ao deslocamento: a camera fica
	// parada e o rato continua a mexer so a camera. Nao forcamos o 0xB4 (o jogo/Coeus reescrevem-no); so usamos
	// o modo quando ele ja esta ligado.
	void CameraHold(UInt8 * cs, float step)
	{
		if(!cs) return;
		float * off = (float *)(cs + 0xAC);
		*off = WrapPi(*off - step);
	}

	// ------------------------------------------------------------------ botoes
	bool	g_holdLeft = false, g_holdRight = false, g_holdExtra = false;
	bool	g_kFwd = false, g_kBack = false, g_kLeft = false, g_kRight = false;
	DWORD	g_lastHoldEvent = 0;
	DWORD	g_lastScan = 0;

	// ------------------------------------------------------------------ lock-on PROPRIO (F liga/desliga, M3 troca de alvo)
	// Prende o alvo cujo CORPO INTEIRO (qualquer osso do esqueleto: pernas, asas, cauda, cabeca...) fica mais perto da
	// mira, dentro ou fora do combate. Nao desenha reticula. Substitui o Simple Lock-On (nao usar os dois ao mesmo tempo).
	volatile LONG	g_lockId = 0;				// formID do alvo preso
	Actor *			g_lockActor = NULL;			// ponteiro do alvo (revalidado por varrimento a cada 250 ms)
	volatile float	g_aimX = 0.0f, g_aimY = 0.0f, g_aimZ = 0.0f;	// ponto de mira (centro do corpo + previsao)
	volatile LONG	g_assist = 0;				// 1 = a prever o disparo (arco / magia a ser usado)
	DWORD			g_assistUntil = 0;
	DWORD			g_lockLostSince = 0, g_lastResolve = 0, g_lastNodes = 0, g_lockPrevTick = 0;
	float			g_lockOff[3] = { 0.0f, 0.0f, 80.0f };	// centro do corpo menos a posicao do ator
	float			g_lockVel[3] = { 0.0f, 0.0f, 0.0f };	// velocidade estimada do alvo (unidades por segundo)
	float			g_lockPrev[3] = { 0.0f, 0.0f, 0.0f };
	int				g_camAxis = 1;				// eixo da camera que aponta para a frente (0..11): fixo, vem de LockCameraAxis (1 = coluna Y)

	bool IsFirstPerson()
	{
		PlayerCamera * cam = PlayerCamera::GetSingleton();
		return cam && cam->cameraState && cam->cameraState->stateId == PlayerCamera::kCameraState_FirstPerson;
	}

	// Raio da mira: origem na camera, direcao para a frente. O eixo "frente" da matriz e fixo (LockCameraAxis, padrao 1 =
	// coluna Y; nos registos coincidia a 100% com a direcao camera -> personagem). Sem calibracao continua: era ela que oscilava.
	bool CameraRay(PlayerCharacter * player, float * o, float * d)
	{
		__try
		{
			PlayerCamera * cam = PlayerCamera::GetSingleton();
			if(!cam || !cam->cameraNode) return false;
			const NiTransform & t = cam->cameraNode->m_worldTransform;
			o[0] = t.pos.x; o[1] = t.pos.y; o[2] = t.pos.z;

			float cand[12][3];
			for(int a = 0; a < 3; ++a)
				for(int r = 0; r < 3; ++r)
				{
					cand[a][r]     =  t.rot.data[r][a];		// colunas
					cand[a + 3][r] = -t.rot.data[r][a];
					cand[a + 6][r] =  t.rot.data[a][r];		// linhas
					cand[a + 9][r] = -t.rot.data[a][r];
				}

			const int ax = g_cfg.lockCameraAxis;
			g_camAxis = (ax >= 0 && ax < 12) ? ax : 1;

			const float dx = cand[g_camAxis][0], dy = cand[g_camAxis][1], dz = cand[g_camAxis][2];
			const float dl = sqrtf(dx * dx + dy * dy + dz * dz);
			if(dl < 0.5f) return false;
			d[0] = dx / dl; d[1] = dy / dl; d[2] = dz / dl;
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// Angulo (radianos) entre o raio da mira e o ponto p.
	float AngleToRay(const float * o, const float * d, float px, float py, float pz)
	{
		const float vx = px - o[0], vy = py - o[1], vz = pz - o[2];
		const float len = sqrtf(vx * vx + vy * vy + vz * vz);
		if(len < 1.0f) return 0.0f;
		float c = (vx * d[0] + vy * d[1] + vz * d[2]) / len;
		if(c >  1.0f) c =  1.0f;
		if(c < -1.0f) c = -1.0f;
		return acosf(c);
	}

	// Avalia o corpo do ator: angulo minimo entre a mira e qualquer ponto do corpo, e o centro do corpo.
	bool EvalBody(Actor * actor, const float * o, const float * d, float * outAngle, float * outCenter)
	{
		float pts[3 * 256];
		const NiPoint3 pos = Engine::Position(actor);
		int n = CollectPoints(actor, pts, 256);
		if(n < 3)
		{
			// Sem esqueleto utilizavel: usa so os pes e a altura de mira.
			pts[0] = pos.x; pts[1] = pos.y; pts[2] = pos.z;
			pts[3] = pos.x; pts[4] = pos.y; pts[5] = pos.z + g_cfg.aimHeight;
			n = 2;
		}
		float minA = 10.0f, cx = 0.0f, cy = 0.0f, minZ = 1e30f, maxZ = -1e30f;
		for(int i = 0; i < n; ++i)
		{
			const float a = AngleToRay(o, d, pts[i * 3], pts[i * 3 + 1], pts[i * 3 + 2]);
			if(a < minA) minA = a;
			cx += pts[i * 3]; cy += pts[i * 3 + 1];
			if(pts[i * 3 + 2] < minZ) minZ = pts[i * 3 + 2];
			if(pts[i * 3 + 2] > maxZ) maxZ = pts[i * 3 + 2];
		}
		*outAngle = minA;
		// Ponto de mira: centro horizontal do corpo, a 65% da altura (peito de um humano; para criaturas grandes, o tronco).
		outCenter[0] = cx / n; outCenter[1] = cy / n; outCenter[2] = minZ + 0.65f * (maxZ - minZ);
		return true;
	}

	// Atores a considerar: os da celula atual e os que o jogo lista como hostis (mesmo noutra celula).
	int GatherActors(PlayerCharacter * player, Actor ** out, int maxN, UInt32 * hostileIds, UInt32 * hostileHandles, int * hostileCount)
	{
		int n = 0;
		const int hc = CollectHostileIds(player, hostileIds, hostileHandles, 64);
		if(hostileCount) *hostileCount = hc;

		TESObjectCELL * cell = Engine::ParentCell(player);
		if(cell)
		{
			const UInt32 * raw = (const UInt32 *)&cell->objectList;
			TESObjectREFR ** items = (TESObjectREFR **)raw[0];
			UInt32 count = (raw[1] < raw[2]) ? raw[1] : raw[2];
			if(items && count > 0)
			{
				if(count > 20000) count = 20000;
				for(UInt32 i = 0; i < count && n < maxN; ++i)
				{
					TESObjectREFR * ref = items[i];
					if(!ref || ref == (TESObjectREFR *)player) continue;
					if(ref->formType != kFormType_Character) continue;
					Actor * a = DYNAMIC_CAST(ref, TESObjectREFR, Actor);
					if(a) out[n++] = a;
				}
			}
		}

		for(int j = 0; j < hc && n < maxN; ++j)
		{
			UInt32 h = hostileHandles[j];
			TESObjectREFR * ref = NULL;
			if(h && LookupREFRByHandle(&h, &ref) && ref)
			{
				Actor * a = DYNAMIC_CAST(ref, TESObjectREFR, Actor);
				ref->handleRefObject.DecRefHandle();
				if(a && a != (Actor *)player)
				{
					bool dup = false;
					for(int k = 0; k < n; ++k) if(out[k] == a) { dup = true; break; }
					if(!dup) out[n++] = a;
				}
			}
		}
		return n;
	}

	bool LockEligible(Actor * a, PlayerCharacter * player, const UInt32 * hid, int hc)
	{
		const UInt32 life = Engine::LifeState(a);
		if(life != 0 && !(g_cfg.allowReanimated && life == 4)) return false;
		if(!a->GetNiNode()) return false;							// sem corpo carregado
		if(g_cfg.lockHostileOnly)
		{
			bool listed = false;
			for(int j = 0; j < hc; ++j) if(hid[j] == a->formID) { listed = true; break; }
			if(!listed && !CALL_MEMBER_FN(a, IsHostileToActor)((Actor *)player)) return false;
		}
		return true;
	}

	// mode 0 = o corpo mais perto da mira (centro do ecra); mode 1 = o mais perto do jogador. excludeId: ignora esse alvo.
	Actor * PickTarget(PlayerCharacter * player, int mode, UInt32 excludeId, float * outAngle)
	{
		float o[3], d[3];
		if(!CameraRay(player, o, d)) return NULL;

		Actor * list[512];
		UInt32 hid[64], hh[64];
		int hc = 0;
		const int n = GatherActors(player, list, 512, hid, hh, &hc);

		const NiPoint3 me = Engine::Position(player);
		const float acquireRad = g_cfg.lockAcquireDeg * kPi / 180.0f;
		Actor * best = NULL;
		float bestScore = 1e30f, bestAng = 0.0f;

		for(int i = 0; i < n; ++i)
		{
			Actor * a = list[i];
			if(!a || a->formID == excludeId) continue;
			const NiPoint3 pos = Engine::Position(a);
			const float dx = pos.x - me.x, dy = pos.y - me.y, dz = pos.z - me.z;
			const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
			if(dist > g_cfg.lockRange) continue;
			if(!LockEligible(a, player, hid, hc)) continue;

			float score, ang = 0.0f;
			if(mode == 0)
			{
				// Pre-filtro barato (o corpo pode ser grande: margem larga) antes de percorrer o esqueleto.
				if(AngleToRay(o, d, pos.x, pos.y, pos.z + 80.0f) > acquireRad + 1.2f) continue;
				float c[3];
				if(!EvalBody(a, o, d, &ang, c)) continue;
				if(ang > acquireRad) continue;
				score = ang + dist * 0.00001f;						// empate: o mais perto
			}
			else
			{
				score = dist;
				// Escolha automatica: ate ao alcance circular qualquer direcao (tambem atras); mais longe so a frente da camera.
				if(dist > g_cfg.lockAutoCircle)
				{
					const float yaw = atan2f(dx, dy), cyaw = atan2f(d[0], d[1]);
					if(fabsf(WrapPi(yaw - cyaw)) > g_cfg.lockAutoCone * kPi / 180.0f) continue;
				}
			}

			if(score < bestScore) { bestScore = score; best = a; bestAng = ang; }
		}
		if(outAngle) *outAngle = bestAng;
		return best;
	}

	Actor * SafePickTarget(PlayerCharacter * player, int mode, UInt32 excludeId, float * outAngle)
	{
		__try { return PickTarget(player, mode, excludeId, outAngle); }
		__except(EXCEPTION_EXECUTE_HANDLER) { return NULL; }
	}

	void LockSetTarget(Actor * a, const char * what, float angleDeg)
	{
		const DWORD now = GetTickCount();
		g_lockActor = a;
		g_lockId = (LONG)a->formID;
		g_lockOwn = 1;
		g_lockLostSince = 0;
		g_lastResolve = now - 1000;		// revalida ja no 1o passo (precisa do handle para o Coeus)
		g_lastNodes = 0;
		g_lockPrevTick = 0;
		g_lockVel[0] = g_lockVel[1] = g_lockVel[2] = 0.0f;
		g_lastTargetId = a->formID;
		g_targetHandle = 0;
		g_endTick = now + 1000;
		g_active = 1;
		if(angleDeg >= 0.0f) _MESSAGE("lock: %s formID %08X (%.1f deg from the crosshair)", what, a->formID, angleDeg);
		else				 _MESSAGE("lock: %s formID %08X", what, a->formID);
	}

	void LockOff(const char * why)
	{
		if(!g_lockOwn) return;
		g_lockOwn = 0;
		g_lockActor = NULL;
		g_lockId = 0;
		g_assist = 0;
		g_targetHandle = 0;
		g_endReason = 4;
		g_active = 0;
		_MESSAGE("lock: OFF (%s)", why);
	}

	bool MenuBlocksInput()
	{
		__try
		{
			MenuManager * mm = MenuManager::GetSingleton();
			UIStringHolder * ui = UIStringHolder::GetSingleton();
			if(!mm || !ui) return false;
			BSFixedString * names[] =
			{
				&ui->inventoryMenu, &ui->console, &ui->dialogueMenu, &ui->mainMenu, &ui->magicMenu, &ui->tweenMenu,
				&ui->barterMenu, &ui->giftMenu, &ui->mapMenu, &ui->lockpickingMenu, &ui->statsMenu, &ui->containerMenu,
				&ui->sleepWaitMenu, &ui->journalMenu, &ui->bookMenu, &ui->favoritesMenu, &ui->loadingMenu
			};
			for(int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); ++i)
				if(mm->IsMenuOpen(names[i])) return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { }
		return false;
	}

	// Tecla F (liga/desliga) - chamada pelo recetor de teclas.
	void LockToggle()
	{
		PlayerCharacter * player = Engine::Player();
		if(!player) return;
		if(g_lockOwn) { LockOff("key pressed"); return; }
		if(g_lockActive) { _MESSAGE("lock: another lock-on (LockOn.dll) is active - remove it, this mod replaces it"); return; }

		float ang = 0.0f;
		Actor * a = SafePickTarget(player, 0, 0, &ang);
		if(!a) { _MESSAGE("lock: no target near the crosshair (range %.0f, area %.0f deg)", g_cfg.lockRange, g_cfg.lockAcquireDeg); return; }
		LockSetTarget(a, "ON, target", ang * 180.0f / kPi);
	}

	// Tecla M3 (troca de alvo): o corpo mais perto do centro do ecra, sem contar o atual.
	void LockSwitch()
	{
		if(!g_lockOwn) return;
		PlayerCharacter * player = Engine::Player();
		if(!player) return;
		float ang = 0.0f;
		Actor * a = SafePickTarget(player, 0, (UInt32)g_lockId, &ang);
		if(!a) { _MESSAGE("lock: no other target near the crosshair"); return; }
		LockSetTarget(a, "switched to", ang * 180.0f / kPi);
	}

	void LockLoseOrNext(PlayerCharacter * player, const char * why)
	{
		if(g_cfg.lockAutoNext)
		{
			Actor * a = SafePickTarget(player, 1, (UInt32)g_lockId, NULL);
			if(a) { _MESSAGE("lock: %s - going to the nearest enemy", why); LockSetTarget(a, "auto-next", -1.0f); return; }
		}
		LockOff(why);
	}

	// Arco ou besta numa das maos?
	bool BowEquipped(PlayerCharacter * player)
	{
		ActorProcessManager * pm = player->processManager;
		if(!pm) return false;
		for(int h = 0; h < 2; ++h)
		{
			TESForm * f = pm->equippedObject[h];
			if(f && f->formType == kFormType_Weapon)
			{
				const UInt8 type = ((TESObjectWEAP *)f)->gameData.type;
				if(type == TESObjectWEAP::GameData::kType_Bow || type == TESObjectWEAP::GameData::kType_CrossBow) return true;
			}
		}
		return false;
	}

	// A usar arco / besta / magia com um botao de ataque carregado? (sem funcoes do SKSE: leitura direta)
	bool RangedHeld(PlayerCharacter * player)
	{
		ActorProcessManager * pm = player->processManager;
		if(!pm) return false;
		const bool anyHeld = g_holdLeft || g_holdRight;
		for(int h = 0; h < 2; ++h)
		{
			TESForm * f = pm->equippedObject[h];			// 0 = mao esquerda, 1 = direita
			if(!f) continue;
			if(f->formType == kFormType_Weapon)
			{
				const UInt8 type = ((TESObjectWEAP *)f)->gameData.type;
				if((type == TESObjectWEAP::GameData::kType_Bow || type == TESObjectWEAP::GameData::kType_CrossBow) && anyHeld) return true;
			}
			else if(f->formType == kFormType_Spell)
			{
				if((h == 0 && g_holdLeft) || (h == 1 && g_holdRight)) return true;
			}
		}
		return false;
	}


	// ------------------------------------------------------------------ velocidade do projetil (previsao)
	// Feiticos: a velocidade vem do projetil do primeiro efeito (dados do jogo); se nao houver, a de reserva do ini.
	float SpellSpeedFromData(PlayerCharacter * player)
	{
		__try
		{
			ActorProcessManager * pm = player->processManager;
			if(!pm) return 0.0f;
			TESForm * f = pm->equippedObject[g_holdRight ? 1 : 0];
			if(!f || f->formType != kFormType_Spell) return 0.0f;
			MagicItem * mi = (SpellItem *)f;
			for(UInt32 i = 0; i < mi->effectItemList.count && i < 8; ++i)
			{
				MagicItem::EffectItem * ei = mi->effectItemList.arr.entries[i];
				if(!ei || !ei->mgef) continue;
				BGSProjectile * pr = ei->mgef->properties.projectile;
				if(!pr) continue;
				const float sp = pr->data.unk08;
				if(sp > 200.0f && sp < 30000.0f) return sp;
			}
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { }
		return 0.0f;
	}

	float ProjectileSpeedNow(PlayerCharacter * player)
	{
		float sp = 0.0f;
		if(BowEquipped(player)) sp = g_cfg.projectileSpeed;
		else
		{
			if(g_cfg.useFormSpeed) sp = SpellSpeedFromData(player);
			if(sp <= 0.0f) sp = g_cfg.spellSpeed;
		}
		sp *= g_cfg.speedScale / 100.0f;
		return sp < 300.0f ? 300.0f : sp;
	}

	// ------------------------------------------------------------------ rotacao do input (perfil FreeCam / Coeus)
	// Com o corpo preso ao alvo, o jogo le as teclas relativas AO CORPO. Para o movimento continuar relativo ao ECRA
	// (W = para cima no ecra, A = esquerda...), roda-se o vetor de movimento pela diferenca camera - corpo.
	// O vetor (dois floats seguidos) nao esta nos cabecalhos do SKSE: descobre-se sozinho comparando a memoria dos
	// controlos do jogador com as teclas carregadas (parado = 0, a andar = comprimento 1, e a direcao certa).
	const int kMvRegions = 14;				// 0 = o proprio PlayerControls; 1..13 = os seus manipuladores de input
	const int kMvWords0 = 128;				// 0x200 bytes
	const int kMvWordsH = 24;				// 0x60 bytes
	struct MvCand { UInt8 dir[4]; UInt16 move; UInt16 idle; };
	MvCand			g_mvCand[kMvRegions][kMvWords0];
	int				g_mvMoveSamples = 0, g_mvIdleSamples = 0;
	int				g_mvRegion = -1, g_mvWord = -1;
	volatile LONG	g_mvFound = 0, g_mvWant = 0;
	volatile DWORD	g_mvWantTick = 0;
	DWORD			g_mvLastLearn = 0;
	long			g_mvWrites = 0, g_mvSeen = 0, g_mvKept = 0;
	float			g_mvLastX = 0.0f, g_mvLastY = 0.0f;
	volatile LONG	g_mvHaveLast = 0;

	inline float W2F(UInt32 w) { float f; memcpy(&f, &w, 4); return f; }

	UInt32 * MvBase(int region, int * words)
	{
		PlayerControls * pc = PlayerControls::GetSingleton();
		if(!pc) return NULL;
		if(region == 0) { *words = kMvWords0; return (UInt32 *)pc; }
		PlayerInputHandler * h = pc->inputHandlers[region - 1];
		if(!h) return NULL;
		*words = kMvWordsH;
		return (UInt32 *)h;
	}

	bool MvReadRegion(int region, UInt32 * dst, int * words)
	{
		__try
		{
			UInt32 * b = MvBase(region, words);
			if(!b) return false;
			for(int i = 0; i < *words; ++i) dst[i] = b[i];
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	void MvLearn(DWORD now)
	{
		if(g_mvFound || now - g_mvLastLearn < 12) return;
		g_mvLastLearn = now;
		const int nd = (g_kFwd ? 1 : 0) + (g_kBack ? 1 : 0) + (g_kLeft ? 1 : 0) + (g_kRight ? 1 : 0);
		if(nd > 2) return;
		const bool idle = (nd == 0);
		int dir = -1; float ex = 0.0f, ey = 0.0f;
		if(nd == 1)
		{
			if(g_kFwd) { dir = 0; ey = 1.0f; }
			else if(g_kBack) { dir = 1; ey = -1.0f; }
			else if(g_kLeft) { dir = 2; ex = -1.0f; }
			else { dir = 3; ex = 1.0f; }
		}
		if(idle) ++g_mvIdleSamples; else ++g_mvMoveSamples;

		for(int r = 0; r < kMvRegions; ++r)
		{
			UInt32 w[kMvWords0];
			int n = 0;
			if(!MvReadRegion(r, w, &n)) continue;
			for(int i = 0; i + 1 < n; ++i)
			{
				const float x = W2F(w[i]), y = W2F(w[i + 1]);
				if(!(x == x) || !(y == y)) continue;
				if(fabsf(x) > 1.05f || fabsf(y) > 1.05f) continue;
				MvCand & c = g_mvCand[r][i];
				const float len = sqrtf(x * x + y * y);
				if(idle) { if(len < 0.02f && c.idle < 60000) ++c.idle; }
				else
				{
					if(len > 0.9f && len < 1.1f && c.move < 60000) ++c.move;
					if(dir >= 0 && fabsf(x - ex) < 0.12f && fabsf(y - ey) < 0.12f && c.dir[dir] < 250) ++c.dir[dir];
				}
			}
		}
		if(g_mvMoveSamples < 40 || g_mvIdleSamples < 40) return;

		int bestR = -1, bestI = -1, bestScore = -1, ties = 0, bestDirs = 0;
		for(int r = 0; r < kMvRegions; ++r)
		{
			const int nw = (r == 0) ? kMvWords0 : kMvWordsH;
			for(int i = 0; i + 1 < nw; ++i)
			{
				const MvCand & c = g_mvCand[r][i];
				if(c.move < g_mvMoveSamples * 8 / 10 || c.idle < g_mvIdleSamples * 8 / 10) continue;
				int dirs = 0;
				for(int d = 0; d < 4; ++d) if(c.dir[d] >= 5) ++dirs;
				const int score = dirs * 100000 + c.move + c.idle;
				if(score > bestScore) { bestScore = score; bestR = r; bestI = i; bestDirs = dirs; ties = 0; }
				else if(score == bestScore) ++ties;
			}
		}
		if(bestR < 0)
		{
			// nada com a forma certa: recomeca a recolher (as contagens antigas ficam, mas as amostras duplicam-se)
			if(g_mvMoveSamples > 600)
			{
				_MESSAGE("input: no movement vector found in the player controls (%d moving / %d idle samples) - the input rotation stays off", g_mvMoveSamples, g_mvIdleSamples);
				g_mvMoveSamples = -1000000;		// desiste (nao volta a tentar nesta sessao de jogo)
			}
			return;
		}
		g_mvRegion = bestR; g_mvWord = bestI; g_mvFound = 1;
		_MESSAGE("input: movement vector found: region %d, byte offset 0x%X (directions matched %d of 4, other equal candidates %d) - to skip the search put MoveVecRegion=%d and MoveVecOffset=%d in the ini",
				 bestR, bestI * 4, bestDirs, ties, bestR, bestI * 4);
	}

	// Escreve o vetor ja rodado. Chamado muitas vezes por segundo pela thread do input (o jogo e o Coeus tambem o escrevem).
	void MvApply(float camYaw, float bodyYaw)
	{
		if(!g_mvFound) return;
		float sx = (g_kRight ? 1.0f : 0.0f) - (g_kLeft ? 1.0f : 0.0f);
		float sy = (g_kFwd ? 1.0f : 0.0f) - (g_kBack ? 1.0f : 0.0f);
		if(sx == 0.0f && sy == 0.0f) { g_mvHaveLast = 0; return; }
		const float l = sqrtf(sx * sx + sy * sy);
		sx /= l; sy /= l;
		const float d = WrapPi(camYaw - bodyYaw);
		const float cs = cosf(d), sn = sinf(d);
		const float ix = sx * cs + sy * sn;
		const float iy = -sx * sn + sy * cs;
		__try
		{
			int n = 0;
			UInt32 * b = MvBase(g_mvRegion, &n);
			if(!b || g_mvWord + 1 >= n) return;
			float * v = (float *)(b + g_mvWord);
			if(g_mvHaveLast) { ++g_mvSeen; if(fabsf(v[0] - g_mvLastX) < 0.001f && fabsf(v[1] - g_mvLastY) < 0.001f) ++g_mvKept; }
			v[0] = ix; v[1] = iy;
			g_mvLastX = ix; g_mvLastY = iy; g_mvHaveLast = 1;
			++g_mvWrites;
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { g_mvFound = 0; g_mvRegion = -1; g_mvWord = -1; g_mvMoveSamples = -1000000; }
	}

	DWORD WINAPI MvThread(LPVOID)
	{
		for(;;)
		{
			Sleep(1);
			if(!g_mvFound || !g_mvWant || GetTickCount() - g_mvWantTick > 120) { g_mvHaveLast = 0; continue; }
			__try
			{
				PlayerCharacter * player = Engine::Player();
				if(!player) continue;
				float o[3], d[3];
				if(!CameraRay(player, o, d)) continue;
				MvApply(atan2f(d[0], d[1]), Engine::Rotation(player).z);
			}
			__except(EXCEPTION_EXECUTE_HANDLER) { }
		}
		return 0;
	}

	// Um passo do lock-on proprio (corre na thread de rotacao): valida o alvo, calcula o ponto de mira e a previsao.
	void LockTick(PlayerCharacter * player, DWORD now)
	{
		if(!g_lockOwn) return;

		// 1) revalidar o ponteiro do alvo (varrimento a cada 250 ms)
		if(!g_lockActor || (LONG)(now - g_lastResolve) > 250)
		{
			g_lastResolve = now;
			Actor * list[512];
			UInt32 hid[64], hh[64];
			int hc = 0;
			const int n = GatherActors(player, list, 512, hid, hh, &hc);
			Actor * f = NULL;
			for(int i = 0; i < n; ++i) if(list[i] && list[i]->formID == (UInt32)g_lockId) { f = list[i]; break; }
			if(!f) { LockOff("target left the area"); return; }
			g_lockActor = f;
			LONG newHandle = 0;
			for(int j = 0; j < hc; ++j) if(hid[j] == f->formID) { newHandle = (LONG)hh[j]; break; }
			if(!newHandle) newHandle = (LONG)HandleFor(f);
			if(newHandle) g_targetHandle = newHandle;			// so se muda quando ha um valor valido
		}

		Actor * a = g_lockActor;
		if(!a || a->formID != (UInt32)g_lockId) { g_lockActor = NULL; return; }

		const UInt32 life = Engine::LifeState(a);
		if(life != 0 && !(g_cfg.allowReanimated && life == 4)) { LockLoseOrNext(player, "target died"); return; }

		const NiPoint3 pos = Engine::Position(a);
		const NiPoint3 me = Engine::Position(player);
		const float ex = pos.x - me.x, ey = pos.y - me.y, ez = pos.z - me.z;
		const float dist3 = sqrtf(ex * ex + ey * ey + ez * ez);
		if(dist3 > g_cfg.lockRange * 1.25f) { LockOff("target too far"); return; }

		// 2) corpo do alvo e visibilidade (a cada 60 ms)
		if((LONG)(now - g_lastNodes) >= 60)
		{
			g_lastNodes = now;
			float o[3], d[3];
			if(CameraRay(player, o, d))
			{
				float ang = kPi, c[3];
				if(EvalBody(a, o, d, &ang, c))
				{
					g_lockOff[0] = c[0] - pos.x; g_lockOff[1] = c[1] - pos.y; g_lockOff[2] = c[2] - pos.z;
				}
				const bool visible = ang <= g_cfg.lockLoseAngleDeg * kPi / 180.0f;
				if(visible) g_lockLostSince = 0;
				else if(!g_lockLostSince) g_lockLostSince = now;
				else if((LONG)(now - g_lockLostSince) > g_cfg.lockLoseSightMs)
				{
					_MESSAGE("lock: target %.0f deg away from the view for %d ms", ang * 180.0f / kPi, g_cfg.lockLoseSightMs);
					LockOff("target out of sight");
					return;
				}
			}
		}

		// 3) velocidade do alvo (a cada 100 ms, suavizada)
		if(!g_lockPrevTick)
		{
			g_lockPrev[0] = pos.x; g_lockPrev[1] = pos.y; g_lockPrev[2] = pos.z;
			g_lockPrevTick = now;
		}
		else if((LONG)(now - g_lockPrevTick) >= 100)
		{
			const float s = (now - g_lockPrevTick) / 1000.0f;
			float nv[3] = { (pos.x - g_lockPrev[0]) / s, (pos.y - g_lockPrev[1]) / s, (pos.z - g_lockPrev[2]) / s };
			if(sqrtf(nv[0] * nv[0] + nv[1] * nv[1] + nv[2] * nv[2]) > 4000.0f) nv[0] = nv[1] = nv[2] = 0.0f;	// salto/teleporte
			for(int k = 0; k < 3; ++k) g_lockVel[k] = g_lockVel[k] * 0.5f + nv[k] * 0.5f;
			g_lockPrev[0] = pos.x; g_lockPrev[1] = pos.y; g_lockPrev[2] = pos.z;
			g_lockPrevTick = now;
		}

		// 4) ponto de mira e previsao do disparo
		float ax = pos.x + g_lockOff[0], ay = pos.y + g_lockOff[1], az = pos.z + g_lockOff[2];

		if(RangedHeld(player)) g_assistUntil = now + 350;		// o disparo sai ao SOLTAR: mantem a ajuda um instante
		const bool assist = ((LONG)(g_assistUntil - now) > 0);
		g_assist = assist ? 1 : 0;
		if(assist && g_cfg.predictEnabled)
		{
			const float sxp = me.x, syp = me.y, szp = me.z + g_cfg.playerAimHeight;
			const float d0x = ax - sxp, d0y = ay - syp, d0z = az - szp;
			const float dist = sqrtf(d0x * d0x + d0y * d0y + d0z * d0z);
			if(dist > g_cfg.predictMinDist)
			{
				// Interceptacao: o tempo de voo depende de onde o alvo ESTARA; repete-se ate convergir.
				const float speed = ProjectileSpeedNow(player);
				const float k = g_cfg.predictStrength / 100.0f;
				const bool bow = BowEquipped(player);
				float t = dist / speed;
				float px = ax, py = ay, pz = az;
				for(int it = 0; it < 6; ++it)
				{
					px = ax + g_lockVel[0] * t * k;
					py = ay + g_lockVel[1] * t * k;
					pz = az + g_lockVel[2] * t * k * 0.5f;
					const float dx = px - sxp, dy = py - syp, dz = pz - szp;
					t = sqrtf(dx * dx + dy * dy + dz * dz) / speed;
					if(t > 3.0f) { t = 3.0f; break; }
				}
				ax = px; ay = py;
				az = pz + (bow ? 0.5f * g_cfg.projectileGravity * t * t : 0.0f);
				if(g_cfg.verbose)
					_MESSAGE("predict: dist %.0f speed %.0f t %.2fs lead (%.0f, %.0f, %.0f) %s", dist, speed, t, px - (pos.x + g_lockOff[0]), py - (pos.y + g_lockOff[1]), pz - (pos.z + g_lockOff[2]), bow ? "bow" : "spell");
			}
		}
		g_aimX = ax; g_aimY = ay; g_aimZ = az;
		g_lockCenter[0] = pos.x + g_lockOff[0]; g_lockCenter[1] = pos.y + g_lockOff[1]; g_lockCenter[2] = pos.z + g_lockOff[2];

		// 5) guinada e inclinacao que o corpo (e a camera) devem tomar
		const float hx = ax - me.x, hy = ay - me.y;
		const float hd = sqrtf(hx * hx + hy * hy);
		const float limitRad = g_cfg.verticalLimitDeg * kPi / 180.0f;
		float up = atan2f(az - (me.z + g_cfg.playerAimHeight), hd > 1.0f ? hd : 1.0f);
		if(up >  limitRad) up =  limitRad;
		if(up < -limitRad) up = -limitRad;
		g_targetYaw = atan2f(hx, hy);
		g_targetPitchUp = up;
		g_hasPitch = 1;
		g_lastTargetId = a->formID;
		g_active = 1;
		g_endTick = now + 1000;
	}

	void SafeLockTick(PlayerCharacter * player, DWORD now)
	{
		__try { LockTick(player, now); }
		__except(EXCEPTION_EXECUTE_HANDLER) { g_lockActor = NULL; }
	}


	// ------------------------------------------------------------------ ponto / reticula no ecra
	// Desenhado dentro do HUD do jogo (menu "HUD Menu") por um clip criado aqui. Tudo o que toca na interface corre na
	// thread do jogo (tarefa de UI do SKSE), nunca na nossa thread de rotacao.
	//  - dotSkin 0 / 1: desenhado pelo proprio plugin (sem ficheiros);
	//  - dotSkin 2: reticula do Simple Lock-On (LockOnMenu.swf: LockOnWidget.SetTargetPos / Hide);
	//  - dotSkin 3: reticula do TDM (TDM_Widgets.swf + TDM_Assets0.swf: Widgets.TDM_TargetLockReticle).
	volatile LONG	g_dotShow = 0;
	volatile float	g_dotWX = 0.0f, g_dotWY = 0.0f, g_dotWZ = 0.0f;
	volatile LONG	g_dotPending = 0;
	volatile DWORD	g_dotQueuedAt = 0;
	volatile LONG	g_dotShown = 0;
	volatile LONG	g_dotDisabled = 0;
	LONG			g_dotFaults = 0;

	struct DotCtx
	{
		int		state;			// 0 = sem clip, 1 = a carregar um .swf, 2 = pronto
		int		skin;			// skin com que o clip foi criado
		int		fallback;		// 1 = o .swf nao carregou: desenha o ponto simples
		int		cand;			// indice do caminho em tentativa
		DWORD	t0;				// inicio da tentativa
		DWORD	createdAt;
		void *	view;
		bool	shown;
		bool	playPending;	// TDM: falta tocar a animacao de entrada
		DWORD	hideAt;			// TDM: depois disto o clip fica invisivel
		DWORD	lastCheck;
		int		drawnColor;
		int		drawnSkin;
		int		reqSkin;		// ultimo aspeto pedido no menu
		bool	diag;			// ja registou o estado do carregamento desta tentativa
		bool	loggedFrustum;
	};
	DotCtx g_dot;
	float g_viewAspect = 16.0f / 9.0f;

	// O widget do Simple Lock-On poe Stage.scaleMode = "noScale" e Stage.align = "TL" no palco INTEIRO do HUD.
	// Guarda-se o que la estava antes de o carregar e repoe-se depois (para nao deslocar o resto do HUD).
	char	g_stageScale[40] = { 0 };
	char	g_stageAlign[40] = { 0 };
	bool	g_stageSaved = false;

	void SaveStage(GFxMovieView * view)
	{
		g_stageSaved = false;
		GFxValue a, b;
		if(view->GetVariable(&a, "Stage.scaleMode") && a.GetType() == GFxValue::kType_String && a.GetString())
		{
			strncpy_s(g_stageScale, sizeof(g_stageScale), a.GetString(), _TRUNCATE);
			if(view->GetVariable(&b, "Stage.align") && b.GetType() == GFxValue::kType_String && b.GetString())
			{
				strncpy_s(g_stageAlign, sizeof(g_stageAlign), b.GetString(), _TRUNCATE);
				g_stageSaved = true;
			}
		}
		_MESSAGE("dot: HUD stage was scaleMode='%s' align='%s'%s", g_stageSaved ? g_stageScale : "?", g_stageSaved ? g_stageAlign : "?", g_stageSaved ? "" : " (could not read, will not restore)");
	}

	void RestoreStage(GFxMovieView * view)
	{
		if(!g_stageSaved) return;
		GFxValue a, b;
		a.SetString(g_stageScale);
		b.SetString(g_stageAlign);
		view->SetVariable("Stage.scaleMode", &a, 0);
		view->SetVariable("Stage.align", &b, 0);
		_MESSAGE("dot: HUD stage restored to scaleMode='%s' align='%s'", g_stageScale, g_stageAlign);
	}

	const int kDotColors[6] = { 0xFFFFFF, 0xFF3030, 0xFFE040, 0x40E0FF, 0x50FF50, 0xFF9020 };
	// Ficheiros .swf por aspeto (relativos a Data\\Interface) e os caminhos que o loadMovie recebe.
	const char * kRelSimple[] = { "SoftTarget\\LockOnReticle.swf", "LockOnReticle.swf", "LockOnMenu.swf" };
	const char * kRelTdm[]    = { "TDM_Widgets.swf", "TrueDirectionalMovement\\TDM_Widgets.swf" };
	char	g_urls[24][110];
	int		g_nUrls = 0;

	bool InterfaceFile(const char * rel)
	{
		char path[260];
		_snprintf_s(path, sizeof(path), _TRUNCATE, "Data\\Interface\\%s", rel);
		return FileExists(path);
	}

	// Bitmask: 1 = reticula do Simple Lock-On encontrada, 2 = reticula do TDM (Widgets + Assets0) encontrada.
	int DotFilesFound()
	{
		int m = 0;
		for(int i = 0; i < 3; ++i) if(InterfaceFile(kRelSimple[i])) { m |= 1; break; }
		bool w = false;
		for(int i = 0; i < 2; ++i) if(InterfaceFile(kRelTdm[i])) { w = true; break; }
		if(w && (InterfaceFile("TDM_Assets0.swf") || InterfaceFile("TrueDirectionalMovement\\TDM_Assets0.swf"))) m |= 2;
		return m;
	}

	// Constroi a lista de caminhos a tentar: so os ficheiros que existem, cada um com 3 formas de escrever o caminho.
	void BuildUrls(int skin)
	{
		g_nUrls = 0;
		const char ** rel = (skin == 2) ? kRelSimple : kRelTdm;
		const int n = (skin == 2) ? 3 : 2;
		for(int i = 0; i < n; ++i)
		{
			const bool ex = InterfaceFile(rel[i]);
			_MESSAGE("dot: file Data\\Interface\\%s %s", rel[i], ex ? "FOUND" : "not found");
			if(!ex) continue;
			char fwd[100];
			_snprintf_s(fwd, sizeof(fwd), _TRUNCATE, "%s", rel[i]);
			for(char * c = fwd; *c; ++c) if(*c == '\\') *c = '/';
			static const char * pre[3] = { "", "Interface/", "Data/Interface/" };
			for(int k = 0; k < 3 && g_nUrls < 24; ++k)
				_snprintf_s(g_urls[g_nUrls++], sizeof(g_urls[0]), _TRUNCATE, "%s%s", pre[k], fwd);
		}
	}

	bool IsObj(const GFxValue & v) { return v.GetType() >= GFxValue::kType_Object; }

	void CallN(GFxValue & o, const char * fn, int n, double a, double b, double c, double d)
	{
		GFxValue args[4], res;
		args[0].SetNumber(a); args[1].SetNumber(b); args[2].SetNumber(c); args[3].SetNumber(d);
		o.Invoke(fn, &res, args, (UInt32)n);
	}

	void CallNone(GFxValue & o, const char * fn)
	{
		GFxValue dummy[1], res;
		o.Invoke(fn, &res, dummy, 0);
	}

	void DrawCircle(GFxValue & clip, double r)
	{
		const double kc = r / cos(22.5 * kPi / 180.0);
		CallN(clip, "moveTo", 2, r, 0.0, 0.0, 0.0);
		for(int k = 0; k < 8; ++k)
		{
			const double a1 = (k * 45.0 + 22.5) * kPi / 180.0, a2 = (k + 1) * 45.0 * kPi / 180.0;
			CallN(clip, "curveTo", 4, kc * cos(a1), kc * sin(a1), r * cos(a2), r * sin(a2));
		}
	}

	void DrawDotShape(GFxValue & clip, int skin, int rgb)
	{
		CallNone(clip, "clear");
		if(skin == 1)
		{
			CallN(clip, "lineStyle", 3, 2.5, (double)rgb, 100.0, 0.0);
			DrawCircle(clip, 15.0);
			CallNone(clip, "lineStyle");
			CallN(clip, "beginFill", 2, (double)rgb, 100.0, 0.0, 0.0);
			DrawCircle(clip, 3.0);
			CallNone(clip, "endFill");
		}
		else
		{
			CallN(clip, "lineStyle", 3, 1.5, 0.0, 60.0, 0.0);
			CallN(clip, "beginFill", 2, (double)rgb, 100.0, 0.0, 0.0);
			DrawCircle(clip, 4.5);
			CallNone(clip, "endFill");
		}
	}

	// Le o frustum (offset 0xE8 da NiCamera) diretamente de um objeto e valida se parece um frustum.
	bool FrustumRaw(void * obj, float * h, float * v)
	{
		if(!obj) return false;
		const float * f = (const float *)((UInt8 *)obj + 0xE8);
		const float l = f[0], r = f[1], t = f[2], b = f[3], n = f[4];
		if(!(n > 0.01f && n < 200.0f && r > l && t > b)) return false;
		const float hh = (r - l) * 0.5f / n, vv = (t - b) * 0.5f / n;
		if(!(hh > 0.2f && hh < 4.0f && vv > 0.1f && vv < 4.0f)) return false;
		*h = hh; *v = vv;
		return true;
	}

	// Procura o frustum no objeto e, ate 4 niveis, nos descendentes.
	bool FrustumSearch(NiAVObject * o, int depth, float * h, float * v)
	{
		if(!o) return false;
		if(FrustumRaw(o, h, v)) return true;
		if(depth >= 4) return false;
		NiNode * nd = o->GetAsNiNode();
		if(!nd) return false;
		const NiTArray<NiAVObject *> & ch = nd->m_children;
		for(UInt16 i = 0; i < ch.m_emptyRunStart && i < 12; ++i)
			if(ch.m_data[i] && FrustumSearch(ch.m_data[i], depth + 1, h, v)) return true;
		return false;
	}

	// Tangentes do meio-campo de visao (horizontal e vertical), do frustum da NiCamera; reserva: worldFOV.
	bool CameraTan(float * tanH, float * tanV)
	{
		PlayerCamera * cam = PlayerCamera::GetSingleton();
		if(!cam || !cam->cameraNode) return false;
		NiNode * n = cam->cameraNode;
		{
			// 1) leitura direta: o proprio no e cada filho (a NiCamera costuma ser o 1o filho)
			float h = 0.0f, v = 0.0f;
			int where = FrustumSearch((NiAVObject *)n, 0, &h, &v) ? 0 : -2;
			if(where != -2)
			{
				*tanH = h; *tanV = v;
				if(!g_dot.loggedFrustum) { g_dot.loggedFrustum = true; _MESSAGE("dot: camera frustum found (depth search): tanH %.3f tanV %.3f (h-fov %.1f deg, v-fov %.1f deg)", h, v, 2.0f * atanf(h) * 180.0f / kPi, 2.0f * atanf(v) * 180.0f / kPi); }
				return true;
			}
		}
		NiCamera * nc = DYNAMIC_CAST(n, NiAVObject, NiCamera);
		if(!nc)
		{
			const NiTArray<NiAVObject *> & ch = n->m_children;
			for(UInt16 i = 0; i < ch.m_emptyRunStart && !nc; ++i)
			{
				NiAVObject * c = ch.m_data[i];
				if(c) nc = DYNAMIC_CAST(c, NiAVObject, NiCamera);
			}
		}
		if(nc)
		{
			const NiFrustum & f = nc->m_frustum;
			if(f.m_fNear > 0.0001f && f.m_fRight > f.m_fLeft && f.m_fTop > f.m_fBottom)
			{
				const float h = (f.m_fRight - f.m_fLeft) * 0.5f / f.m_fNear, v = (f.m_fTop - f.m_fBottom) * 0.5f / f.m_fNear;
				if(h > 0.2f && h < 4.0f && v > 0.1f && v < 4.0f)
				{
					*tanH = h; *tanV = v;
					if(!g_dot.loggedFrustum) { g_dot.loggedFrustum = true; _MESSAGE("dot: camera frustum tanH %.3f tanV %.3f (h-fov %.1f deg)", h, v, 2.0f * atanf(h) * 180.0f / kPi); }
					return true;
				}
			}
		}
		const float fov = cam->worldFOV;
		if(fov > 10.0f && fov < 150.0f)
		{
			// O worldFOV do Skyrim e o campo horizontal para 4:3; o vertical sai disso e o horizontal real, do formato do ecra.
			*tanV = tanf(fov * 0.5f * kPi / 180.0f) * 0.75f;
			*tanH = *tanV * g_viewAspect;
			if(!g_dot.loggedFrustum) { g_dot.loggedFrustum = true; _MESSAGE("dot: NiCamera frustum not found, using worldFOV %.1f as horizontal-4:3 (aspect %.3f): tanH %.3f tanV %.3f", fov, g_viewAspect, *tanH, *tanV); }
			return true;
		}
		return false;
	}

	// Ponto do mundo -> coordenadas normalizadas (-1..1) no ecra. Falha se estiver atras da camera.
	bool ProjectToScreen(PlayerCharacter * player, float wx, float wy, float wz, float * ndcX, float * ndcY, float * dist)
	{
		float o[3], d[3];
		if(!CameraRay(player, o, d)) return false;
		float rx = d[1], ry = -d[0];						// direita = frente x cima(0,0,1)
		const float rl = sqrtf(rx * rx + ry * ry);
		if(rl < 1e-4f) return false;
		rx /= rl; ry /= rl;
		const float ux = ry * d[2], uy = -rx * d[2], uz = rx * d[1] - ry * d[0];	// cima = direita x frente
		const float vx = wx - o[0], vy = wy - o[1], vz = wz - o[2];
		const float x = vx * rx + vy * ry;
		const float y = vx * ux + vy * uy + vz * uz;
		const float z = vx * d[0] + vy * d[1] + vz * d[2];
		if(z < 10.0f) return false;
		float tH = 1.0f, tV = 0.5625f;
		if(!CameraTan(&tH, &tV)) return false;
		*ndcX = x / (z * tH);
		*ndcY = y / (z * tV);
		*dist = sqrtf(vx * vx + vy * vy + vz * vz);
		return true;
	}

	void DotSetVisible(GFxMovieView * view, const char * path, bool vis)
	{
		GFxValue c;
		if(!view->GetVariable(&c, path) || !IsObj(c)) return;
		GFxValue::DisplayInfo di;
		di.SetVisible(vis);
		c.SetDisplayInfo(&di);
	}

	void DotFail(const char * why)
	{
		_MESSAGE("dot: %s", why);
	}

	void DotUpdateInner()
	{
		DotCtx & s = g_dot;
		const DWORD now = GetTickCount();
		const bool want = (g_dotShow != 0);

		if(!want && s.state == 0) { g_dotShown = 0; return; }			// nada a fazer: nem se cria o clip

		MenuManager * mm = MenuManager::GetSingleton();
		UIStringHolder * ui = UIStringHolder::GetSingleton();
		if(!mm || !ui) return;
		GFxMovieView * view = mm->GetMovieView(&ui->hudMenu);
		if(!view) { s.state = 0; s.shown = false; g_dotShown = 0; return; }
		if(view != (GFxMovieView *)s.view) { s.view = view; s.state = 0; s.shown = false; s.fallback = 0; s.cand = 0; }

		if(s.reqSkin != g_cfg.dotSkin) { s.reqSkin = g_cfg.dotSkin; s.fallback = 0; s.cand = 0; }	// mudar o aspeto no menu tenta de novo
		int skin = g_cfg.dotSkin;
		if(s.fallback) skin = 0;

		// O clip pode ter desaparecido (carregar um save recria o HUD) ou a skin pode ter mudado.
		if(s.state != 0 && (now - s.lastCheck) > 700)
		{
			s.lastCheck = now;
			GFxValue c;
			if(!view->GetVariable(&c, "_root.SoftTargetDot") || !IsObj(c)) { s.state = 0; s.shown = false; }
			else if(s.skin != skin)
			{
				CallNone(c, "removeMovieClip");
				s.state = 0; s.shown = false;
			}
		}

		// Criar o clip.
		if(s.state == 0)
		{
			if(!want) { g_dotShown = 0; return; }
			GFxValue root, clip;
			if(!view->GetVariable(&root, "_root") || !IsObj(root)) { DotFail("cannot reach _root of the HUD"); return; }
			{
				GFxValue old;
				if(view->GetVariable(&old, "_root.SoftTargetDot") && IsObj(old)) CallNone(old, "removeMovieClip");
			}
			GFxValue args[2];
			args[0].SetString("SoftTargetDot");
			args[1].SetNumber(24000.0);
			if(!root.Invoke("createEmptyMovieClip", &clip, args, 2) || !IsObj(clip)) { DotFail("createEmptyMovieClip failed"); return; }

			s.skin = skin;
			s.t0 = now; s.createdAt = now; s.lastCheck = now; s.shown = false; s.playPending = false; s.hideAt = 0;
			s.drawnColor = -1; s.drawnSkin = -1;

			if(skin <= 1)
			{
				s.state = 2;
				_MESSAGE("dot: ready (drawn by the plugin, skin %d)", skin);
			}
			else
			{
				BuildUrls(skin);
				if(g_nUrls == 0)
				{
					DotFail("the .swf files of this appearance were not found in Data\\Interface - using the plain dot instead");
					CallNone(clip, "removeMovieClip");
					s.fallback = 1; s.state = 0; s.cand = 0;
					return;
				}
				s.cand = 0;
				if(skin == 2) SaveStage(view);
				GFxValue a1[1], r;
				a1[0].SetString(g_urls[s.cand]);
				clip.Invoke("loadMovie", &r, a1, 1);
				s.state = 1; s.diag = false;
				_MESSAGE("dot: loading '%s' (skin %d, %d paths to try)", g_urls[s.cand], skin, g_nUrls);
			}
			return;
		}

		// A carregar um .swf: espera que o conteudo apareca; se nao aparecer, tenta o caminho seguinte.
		if(s.state == 1)
		{
			GFxValue clip;
			if(!view->GetVariable(&clip, "_root.SoftTargetDot") || !IsObj(clip)) { s.state = 0; return; }
			bool ready = false;
			if(s.skin == 2)
			{
				GFxValue w;
				if(view->GetVariable(&w, "_root.SoftTargetDot.widget") && IsObj(w))
				{
					GFxValue r;
					view->Invoke("_root.SoftTargetDot.widget.Hide", &r, (GFxValue *)NULL, 0);
					RestoreStage(view);
					ready = true;
				}
			}
			else
			{
				GFxValue a3[3], ret;
				a3[0].SetString("TDM_TargetLockReticle");
				a3[1].SetString("ret");
				a3[2].SetNumber(1.0);
				if(clip.Invoke("attachMovie", &ret, a3, 3) && IsObj(ret))
				{
					GFxValue a2[2], r;
					a2[0].SetNumber((double)g_cfg.dotTdmType);
					a2[1].SetNumber((double)g_cfg.dotOpacity);
					ret.Invoke("loadConfig", &r, a2, 2);
					GFxValue::DisplayInfo di;
					di.SetVisible(false);
					ret.SetDisplayInfo(&di);
					ready = true;
				}
			}
			if(ready)
			{
				s.state = 2;
				_MESSAGE("dot: .swf ready after %lu ms (try %d)", (unsigned long)(now - s.t0), s.cand + 1);
			}
			else
			{
				if(!s.diag && now - s.t0 > 800)
				{
					// Diagnostico: o que o clip tem 0,8 s depois do loadMovie.
					s.diag = true;
					GFxValue bt, bl, fl, a0[1];
					clip.Invoke("getBytesTotal", &bt, a0, 0);
					clip.Invoke("getBytesLoaded", &bl, a0, 0);
					clip.GetMember("_framesloaded", &fl);
					_MESSAGE("dot: after 0.8 s: bytesTotal=%.0f bytesLoaded=%.0f framesLoaded=%.0f", bt.GetType() == GFxValue::kType_Number ? bt.GetNumber() : -1.0, bl.GetType() == GFxValue::kType_Number ? bl.GetNumber() : -1.0, fl.GetType() == GFxValue::kType_Number ? fl.GetNumber() : -1.0);
				}
				if(now - s.t0 > 1500)
				{
					++s.cand;
					if(s.cand >= g_nUrls)
					{
						DotFail("the .swf was found on disk but could not be loaded into the HUD - using the plain dot instead");
						CallNone(clip, "removeMovieClip");
						s.fallback = 1; s.state = 0; s.cand = 0;
					}
					else
					{
						GFxValue a1[1], r;
						a1[0].SetString(g_urls[s.cand]);
						clip.Invoke("loadMovie", &r, a1, 1);
						s.t0 = now; s.diag = false;
						_MESSAGE("dot: trying '%s'", g_urls[s.cand]);
					}
				}
			}
			return;
		}

		// Pronto: mostrar / esconder / posicionar.
		float ndcX = 0.0f, ndcY = 0.0f, dist = 0.0f;
		bool visible = false;
		if(want)
		{
			PlayerCharacter * player = Engine::Player();
			if(player) visible = ProjectToScreen(player, g_dotWX, g_dotWY, g_dotWZ, &ndcX, &ndcY, &dist);
			if(visible && (fabsf(ndcX) > 1.15f || fabsf(ndcY) > 1.15f)) visible = false;		// fora do ecra
		}

		GRectF vr = view->GetVisibleFrameRect();
		float vx0 = vr.left, vy0 = vr.top, vw = vr.right - vr.left, vh = vr.bottom - vr.top;
		if(!(vw > 100.0f) || !(vh > 100.0f)) { vx0 = 0.0f; vy0 = 0.0f; vw = 1280.0f; vh = 720.0f; }
		g_viewAspect = vw / vh;
		const float xs = vx0 + (ndcX * 0.5f + 0.5f) * vw;
		const float ys = vy0 + (0.5f - ndcY * 0.5f) * vh;
		const double scale = g_cfg.dotSize;
		const double alpha = g_cfg.dotOpacity;

		if(s.skin <= 1)
		{
			GFxValue clip;
			if(!view->GetVariable(&clip, "_root.SoftTargetDot") || !IsObj(clip)) { s.state = 0; return; }
			const int rgb = kDotColors[(g_cfg.dotColor >= 0 && g_cfg.dotColor < 6) ? g_cfg.dotColor : 0];
			if(s.drawnColor != rgb || s.drawnSkin != s.skin) { DrawDotShape(clip, s.skin, rgb); s.drawnColor = rgb; s.drawnSkin = s.skin; }
			GFxValue::DisplayInfo di;
			di.SetVisible(visible);
			if(visible) { di.SetPosition(xs, ys); di.SetScale(scale, scale); di.SetAlpha(alpha); }
			clip.SetDisplayInfo(&di);
		}
		else if(s.skin == 2)
		{
			GFxValue r;
			if(visible)
			{
				float sw = vw, sh = vh;
				GFxValue v;
				if(view->GetVariable(&v, "Stage.width") && v.GetType() == GFxValue::kType_Number && v.GetNumber() > 100.0) sw = (float)v.GetNumber();
				if(view->GetVariable(&v, "Stage.height") && v.GetType() == GFxValue::kType_Number && v.GetNumber() > 100.0) sh = (float)v.GetNumber();
				GFxValue a3[3];
				a3[0].SetNumber(xs - sw * 0.5f);
				a3[1].SetNumber(ys - sh * 0.5f);
				a3[2].SetNumber(dist);
				view->Invoke("_root.SoftTargetDot.widget.SetTargetPos", &r, a3, 3);
			}
			else view->Invoke("_root.SoftTargetDot.widget.Hide", &r, (GFxValue *)NULL, 0);
		}
		else
		{
			GFxValue ret;
			if(!view->GetVariable(&ret, "_root.SoftTargetDot.ret") || !IsObj(ret)) { s.state = 0; return; }
			if(visible)
			{
				if(!s.shown) { s.playPending = true; s.hideAt = 0; }
				GFxValue::DisplayInfo di;
				di.SetVisible(true); di.SetPosition(xs, ys); di.SetScale(scale, scale);
				ret.SetDisplayInfo(&di);
				if(s.playPending && now - s.createdAt > 150)
				{
					GFxValue t[1], f[1], r;
					t[0].SetBool(true); f[0].SetBool(false);
					ret.Invoke("updateData", &r, t, 1);
					ret.Invoke("updateData", &r, f, 1);
					s.playPending = false;
				}
			}
			else if(s.shown)
			{
				GFxValue t[1], r;
				t[0].SetBool(true);
				ret.Invoke("updateData", &r, t, 1);			// animacao de saida
				s.hideAt = now + 450;
			}
			if(!visible && s.hideAt && now >= s.hideAt)
			{
				GFxValue::DisplayInfo di;
				di.SetVisible(false);
				ret.SetDisplayInfo(&di);
				s.hideAt = 0;
			}
		}
		s.shown = visible;
		g_dotShown = visible ? 1 : 0;
	}

	void DotUpdate()
	{
		g_dotPending = 0;
		if(g_dotDisabled) return;
		__try { DotUpdateInner(); }
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			++g_dotFaults;
			_MESSAGE("dot: fault #%ld in the interface update", g_dotFaults);
			g_dot.state = 0; g_dot.shown = false;
			if(g_dotFaults >= 5) { g_dotDisabled = 1; _MESSAGE("dot: disabled after repeated faults"); }
		}
	}

	class DotTask : public UIDelegate_v1
	{
	public:
		virtual void Run(void) { DotUpdate(); }
		virtual void Dispose(void) { delete this; }
	};

	// Pede (no maximo a cada ~16 ms, ou a cada 400 ms em repouso) uma atualizacao do ponto na thread do jogo.
	void DotPump(DWORD now)
	{
		if(!g_taskIface || g_dotDisabled) return;
		const bool busy = g_dotShow || g_dotShown;
		static DWORD lastQ = 0;
		if(now - lastQ < (busy ? 16u : 400u)) return;
		if(g_dotPending && (now - g_dotQueuedAt) < 500) return;
		lastQ = now;
		g_dotPending = 1;
		g_dotQueuedAt = now;
		g_taskIface->AddUITask(new DotTask());
	}

	bool MatchKey(UInt32 device, int code, int cfgCode)
	{
		if(cfgCode <= 0) return false;
		if(cfgCode >= 256) return device == kDeviceType_Mouse && (code + 256 == cfgCode || code == cfgCode);
		return device == kDeviceType_Keyboard && code == cfgCode;
	}

	// ---- ajuda do lock-on: trocar para o inimigo mais proximo
	UInt32 SafeCallU32(FnVoidU32 f)
	{
		__try { return f ? f() : 0; }
		__except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
	}

	int SafeCallInt(FnVoidInt f)
	{
		__try { return f ? f() : 0; }
		__except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
	}

	UInt32 SafeFormIdFromHandle(UInt32 handle)
	{
		__try
		{
			UInt32 h = handle;
			TESObjectREFR * ref = NULL;
			if(LookupREFRByHandle(&h, &ref) && ref)
			{
				const UInt32 id = ref->formID;
				ref->handleRefObject.DecRefHandle();
				return id;
			}
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { }
		return 0;
	}

	bool GameHasFocus()
	{
		HWND w = GetForegroundWindow();
		if(!w) return false;
		DWORD pid = 0;
		GetWindowThreadProcessId(w, &pid);
		return pid == GetCurrentProcessId();
	}

	void TapKey(int scan)
	{
		INPUT in[2];
		memset(in, 0, sizeof(in));
		in[0].type = INPUT_KEYBOARD; in[0].ki.wScan = (WORD)scan; in[0].ki.dwFlags = KEYEVENTF_SCANCODE;
		in[1].type = INPUT_KEYBOARD; in[1].ki.wScan = (WORD)scan; in[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
		SendInput(1, &in[0], sizeof(INPUT));
		Sleep(40);
		SendInput(1, &in[1], sizeof(INPUT));
	}

	DWORD WINAPI TurnThread(LPVOID)
	{
		DWORD last = GetTickCount();
		int   phase = 0;					// 0 = parado, 1 = a seguir o alvo, 2 = a regressar (rumo e/ou inclinacao)
		float homeYaw = 0.0f, homePitch = 0.0f;
		DWORD returnStart = 0, lastYield = 0, lastRetarget = 0, noTargetSince = 0;
		bool  yielding = false, returnYaw = false, prevActive = false, tiltUsed = false;
		int   lastProfile = -1;
		DWORD lastAssist = 0, lastSwitch = 0, keyAt = 0;
		int   attempts = 0;
		bool  wasLock = false;

		for(;;)
		{
			Sleep((DWORD)g_cfg.updateMs);
			const DWORD now = GetTickCount();
			float dt = (now - last) / 1000.0f;
			last = now;
			if(dt > 0.1f) dt = 0.1f;

			// A sessao acaba quando passa o tempo sem novos botoes (o lock-on proprio nao tem tempo limite).
			if(g_active && !g_lockOwn && (LONG)(now - g_endTick) >= 0) { g_endReason = 0; g_active = 0; }

			PlayerCharacter * player = Engine::Player();
			if(!player) { phase = 0; continue; }

			const bool coeusProfile = InCoeusProfile();
			if((int)coeusProfile != lastProfile)
			{
				lastProfile = (int)coeusProfile;
				_MESSAGE("profile: %s", coeusProfile ? "FreeCam / Coeus (directional movement)" : "normal (no directional movement)");
			}

			// Correr (Shift + andar) = fugir: a sessao acaba e o corpo volta a ser do jogador / do Coeus.
			// Com o lock-on proprio o lock continua, mas o corpo e a camera deixam de ser puxados enquanto corre.
			bool sprintHold = false;
			// FreeCam / Coeus: correr nao larga a mira (ataque em corrida). No perfil normal correr continua a ser fugir.
			const bool sprintKeeps = coeusProfile && g_cfg.sprintKeepsAim;
			if(g_active && g_shiftHeld && g_moveHeld && !sprintKeeps)
			{
				if(g_lockOwn) sprintHold = true;
				else { g_endReason = 1; g_active = 0; phase = 0; }
			}

			// Outro lock-on (LockOn.dll) assumiu: o nosso larga.
			if(g_lockOwn && g_lockActive) LockOff("another lock-on took over");
			if(!g_cfg.lockEnabled && g_lockOwn) LockOff("lock-on disabled in the menu");

			// Lock-on externo ativo (SoftTarget em pausa): larga tudo, sem regressar ao rumo antigo.
			if(LockPauses()) { phase = 0; if(g_active) g_endReason = 2; g_active = 0; }

			// Lock-on assistido (LockOnMode=2): se o lock-on esta num inimigo que nao e o mais proximo,
			// carrega na tecla de trocar alvo da LockOn.dll. Desliga-se sozinho se a tecla desligar o lock-on.
			if(wasLock && !g_lockActive && keyAt && now - keyAt < 700 && !g_assistOff)
			{
				g_assistOff = 1;
				_MESSAGE("lock-on assist: the switch key turned the lock-on OFF - assist disabled (set LockOnSwitchKey to another key or use LockOnMode 0/1)");
			}
			wasLock = g_lockActive != 0;
			if(!g_lockActive) { attempts = 0; }
			else if(g_cfg.lockOnMode == 2 && g_sloGetTarget && !g_assistOff && !g_cfg.logOnly && now - lastAssist >= 250)
			{
				lastAssist = now;
				const UInt32 lh = SafeCallU32(g_sloGetTarget);
				const UInt32 lockId = (lh && lh != 0xFFFFFFFF) ? SafeFormIdFromHandle(lh) : 0;
				float ay = 0.0f, ap = 0.0f;
				UInt32 nid = 0, nh = 0;
				g_forceCircle = 1;
				const bool ok = SafeFindTarget(player, &ay, &ap, &nid, &nh);
				g_forceCircle = 0;
				if(ok && lockId && nid != lockId && attempts < 3 && now - lastSwitch >= 900 && GameHasFocus())
				{
					int key = g_cfg.lockOnSwitchKey ? g_cfg.lockOnSwitchKey : SafeCallInt(g_sloSwitchKey);
					if(key > 0 && key < 256)
					{
						_MESSAGE("lock-on assist: locked on %08X, nearest is %08X - pressing key %d (attempt %d)", lockId, nid, key, attempts + 1);
						TapKey(key);
						lastSwitch = now; keyAt = now; ++attempts;
					}
				}
				else if(ok && nid == lockId) attempts = 0;
			}

			// Lock-on proprio: valida o alvo e calcula o ponto de mira.
			if(g_lockOwn) SafeLockTick(player, now);

			// Durante a sessao o alvo mais proximo e reavaliado aqui, mesmo sem carregar em nada.
			if(g_active && !g_lockOwn && !g_cfg.logOnly && now - lastRetarget >= (DWORD)g_cfg.retargetMs)
			{
				lastRetarget = now;
				float ty = 0.0f, tp = 0.0f;
				UInt32 tid = 0, th = 0;
				if(SafeFindTarget(player, &ty, &tp, &tid, &th))
				{
					noTargetSince = 0;
					if(g_cfg.verbose || tid != g_lastTargetId)
						_MESSAGE("target: formID %08X, yaw %.0f deg, pitch %.0f deg", tid, ty * 180.0f / kPi, tp * 180.0f / kPi);
					g_lastTargetId = tid;
					g_targetYaw = ty; g_targetPitchUp = tp; g_targetHandle = (LONG)th;
				}
				else
				{
					if(!noTargetSince) noTargetSince = now;
					else if(now - noTargetSince > 300) { g_endReason = 3; g_active = 0; noTargetSince = 0; }	// alvo saiu da area
				}
			}
			if(!g_active) noTargetSince = 0;

			// Ponto no ecra: diz-se onde esta o alvo; o desenho e feito pela thread da interface (DotPump).
			{
				bool show = false;
				if(g_lockOwn && g_cfg.dotLock && g_lockActor)
				{
					g_dotWX = g_lockCenter[0]; g_dotWY = g_lockCenter[1]; g_dotWZ = g_lockCenter[2];
					show = true;
				}
				else if(!g_lockOwn && g_active && g_cfg.dotSoft && !g_cfg.logOnly)
				{
					g_dotWX = g_findCenter[0]; g_dotWY = g_findCenter[1]; g_dotWZ = g_findCenter[2];
					show = true;
				}
				g_dotShow = show ? 1 : 0;
				DotPump(now);
			}

			// Registo: inicio e fim de cada sessao de combate (e o motivo do fim).
			if(g_active && !prevActive)
			{
				_MESSAGE("session start: profile=%s target=%08X handle=%08X%s", coeusProfile ? "FreeCam" : "normal", g_lastTargetId, (UInt32)g_targetHandle, g_lockOwn ? " (own lock-on)" : "");
			}
			else if(!g_active && prevActive)
			{
				static const char * why[] = { "timeout", "sprint", "external lock-on active", "no target in range", "own lock-on off" };
				_MESSAGE("session end: %s", why[(g_endReason >= 0 && g_endReason < 5) ? g_endReason : 0]);
			}
			prevActive = (g_active != 0);

			// Perfil TDM sem "encarar o alvo": enquanto anda o corpo e do Coeus; so ha um "pulso" depois de cada ataque.
			// (O lock-on proprio nao usa o pulso: o corpo encara sempre o alvo preso.)
			// Com a rotacao do input ativa o corpo encara o alvo mesmo a andar: o movimento continua a ser relativo ao ecra.
			const bool remapOn = coeusProfile && g_cfg.moveRemap && g_mvFound;
			const bool suppress = !g_lockOwn && coeusProfile && !remapOn && g_moveHeld && (now - g_lastAttackTick) >= (DWORD)g_cfg.attackPulseMs;

			// Perfil TDM a encarar o alvo: pede ao Coeus para largar o corpo (so com handle valido, para nao
			// deixar o Coeus/LockOn num estado de lock-on "fantasma").
			// Com o lock proprio o Coeus larga SEMPRE o corpo (senao os dois rodam o corpo em sentidos opostos e o personagem anda em circulos);
			// para o SoftTarget isso depende de CoeusYield. Sem sprint: correr devolve o corpo ao Coeus.
			// 0.12.3: no perfil FreeCam o Coeus NUNCA larga o corpo (largar solta tambem a camera livre e o movimento relativo ao ecra).
			const bool wantYield = false;
			if(wantYield)
			{
				if(!yielding || now - lastYield > 150)
				{
					__try { g_coeusExternal(true, (UInt32)g_targetHandle); }
					__except(EXCEPTION_EXECUTE_HANDLER) { g_coeusExternal = NULL; }
					if(!yielding) _MESSAGE("coeus: external lock-on ON (handle %08X)", (UInt32)g_targetHandle);
					yielding = true; lastYield = now; g_yieldNow = 1;
				}
			}
			else if(yielding)
			{
				if(g_coeusExternal)
				{
					__try { g_coeusExternal(false, 0); }
					__except(EXCEPTION_EXECUTE_HANDLER) { g_coeusExternal = NULL; }
				}
				_MESSAGE("coeus: external lock-on OFF");
				yielding = false; g_yieldNow = 0;
			}

			// Rotacao do input: aprende onde esta o vetor de movimento e pede a thread do input para o rodar.
			if(g_cfg.moveRemap && !g_mvFound)
			{
				if(g_cfg.mvRegion >= 0 && g_cfg.mvRegion < kMvRegions && g_cfg.mvOffset >= 0 && g_cfg.mvOffset < 0x200)
				{
					g_mvRegion = g_cfg.mvRegion; g_mvWord = g_cfg.mvOffset / 4; g_mvFound = 1;
					_MESSAGE("input: movement vector from the ini: region %d, byte offset 0x%X", g_mvRegion, g_cfg.mvOffset);
				}
				else if(g_mvMoveSamples >= -100000 && GameHasFocus()) MvLearn(now);
			}
			g_mvWant = (remapOn && g_active && !suppress && g_moveHeld) ? 1 : 0;
			g_mvWantTick = now;

			NiPoint3 & rot = Engine::Rotation(player);
			const float maxRad = g_cfg.turnSpeedDeg * kPi / 180.0f;
			const float limitRad = g_cfg.verticalLimitDeg * kPi / 180.0f;
			const float vSign = g_cfg.verticalInvert ? -1.0f : 1.0f;
			const bool returnAllowed = g_cfg.returnToHeading && !coeusProfile;
			const bool firstPerson = IsFirstPerson();
			// Inclinar o corpo (bracos): pela opcao de mira vertical, ou com o lock-on proprio a prever um disparo / em primeira pessoa.
			const bool useTilt = g_hasPitch && ((g_cfg.verticalMode & 1) || (g_lockOwn && (g_assist || firstPerson)));

			if(g_active && !suppress && !sprintHold)
			{
				if(phase == 0)
				{
					// Inicio da sessao: guarda de onde partimos (rumo e inclinacao do corpo).
					homeYaw = rot.z;
					homePitch = rot.x;
					tiltUsed = false;
					phase = 1;
				}
				else if(phase == 2) phase = 1;

				const float stepYaw = StepToward(rot.z, g_targetYaw, true, dt, maxRad, g_cfg.smoothing);
				if(coeusProfile && g_cfg.cameraHoldTdm)
				{
					UInt8 * hcs = ThirdPersonStatePtr();
					// Camera livre (0xB4 ligado), fora da mira com arco/magia (la o Coeus faz a sua propria compensacao): o deslocamento perde o que o corpo ganha.
					if(hcs && *(hcs + 0xB4) && !RangedHeld(player))
					{
						CameraHold(hcs, stepYaw);
						if(g_cfg.verbose && fabsf(stepYaw) > 0.001f) _MESSAGE("camhold: step %.3f free=%d offset=%.3f", stepYaw, (int)*(hcs + 0xB4), *(float *)(hcs + 0xAC));
					}
				}
				rot.z = WrapPi(rot.z + stepYaw);

				// Mira vertical: so o corpo (bracos).
				if(useTilt)
				{
					float up = g_targetPitchUp;
					if(up >  limitRad) up =  limitRad;
					if(up < -limitRad) up = -limitRad;
					rot.x += StepToward(rot.x, -up * vSign, false, dt, maxRad, g_cfg.smoothing);	// positivo = olhar para baixo
					if(!firstPerson) tiltUsed = true;		// em primeira pessoa rot.x e a propria vista: nao se repoe
				}

				// Lock-on proprio: a camera acompanha o alvo (SO no perfil normal; no FreeCam a camera e sempre do rato).
				//  - camera presa ao corpo (0xB4 = 0): escreve-se o angulo da propria camera;
				//  - camera de deslocamento (0xB4 = 1, TDM): o deslocamento caminha para (alvo - corpo).
				if(g_lockOwn && g_cfg.lockCameraFollow && !coeusProfile)
				{
					UInt8 * cs = ThirdPersonStatePtr();
					if(cs)
					{
						if(!*(cs + 0xB4))
						{
							float cy = *(float *)(cs + 0x5C);
							float wantYaw = g_targetYaw;
							{
								// Erro entre a direcao REAL da camera e o alvo, medido a partir da posicao da camera: compensa
								// qualquer deslocamento lateral/atras da camera (mods tipo AIM FIX).
								float o2[3], d2[3];
								if(CameraRay(player, o2, d2))
									wantYaw = WrapPi(cy + WrapPi(atan2f(g_aimX - o2[0], g_aimY - o2[1]) - atan2f(d2[0], d2[1])));
							}
							wantYaw = WrapPi(wantYaw - g_cfg.aimOffX * kPi / 180.0f);		// alvo a direita do centro = camera um pouco mais a esquerda
							cy = WrapPi(cy + StepToward(cy, wantYaw, true, dt, maxRad, g_cfg.lockCameraSmooth));
							*(float *)(cs + 0x5C) = cy;
							*(float *)(cs + 0x60) = cy;
						}
						else
						{
							float * off = (float *)(cs + 0xAC);
							const float want = WrapPi(g_targetYaw - rot.z);
							*off = WrapPi(*off + StepToward(*off, want, true, dt, maxRad, g_cfg.lockCameraSmooth));
						}

						float o[3], dd[3];
						if(g_cfg.lockCameraPitch && CameraRay(player, o, dd))
						{
							const float hx = g_aimX - o[0], hy = g_aimY - o[1];
							float want = -atan2f(g_aimZ - o[2], sqrtf(hx * hx + hy * hy) > 1.0f ? sqrtf(hx * hx + hy * hy) : 1.0f);
							want += g_cfg.aimOffY * kPi / 180.0f;		// alvo acima do centro = camera um pouco mais para baixo
							if(want >  1.1f) want =  1.1f;
							if(want < -1.1f) want = -1.1f;
							float cp = *(float *)(cs + 0x54);
							cp += StepToward(cp, want, false, dt, maxRad, g_cfg.lockCameraSmooth);
							*(float *)(cs + 0x54) = cp;
							*(float *)(cs + 0x58) = cp;
						}
					}
				}
			}
			else if(!g_active && phase == 1)
			{
				// A sessao acabou: o corpo repoe a inclinacao (sempre) e, se pedido, o rumo (so perfil normal,
				// e nunca contra as teclas de andar).
				const bool needYaw = returnAllowed && !g_moveHeld;
				const bool needPitch = tiltUsed && fabsf(homePitch - rot.x) > 0.02f;
				if(needYaw || needPitch) { phase = 2; returnStart = now; returnYaw = needYaw; }
				else phase = 0;
			}

			if(phase == 2 && !g_active)
			{
				const float returnRad = g_cfg.returnSpeedDeg * kPi / 180.0f;
				bool done = true;

				if(returnYaw && g_moveHeld) returnYaw = false;
				if(returnYaw)
				{
					const float stepYaw = StepToward(rot.z, homeYaw, true, dt, returnRad, g_cfg.returnSmoothing);
					rot.z = WrapPi(rot.z + stepYaw);
					if(fabsf(WrapPi(homeYaw - rot.z)) >= 0.01f) done = false;
				}
				if(tiltUsed)
				{
					rot.x += StepToward(rot.x, homePitch, false, dt, returnRad, g_cfg.returnSmoothing);
					if(fabsf(homePitch - rot.x) >= 0.01f) done = false;
				}

				if(done || (LONG)(now - returnStart) >= 6000) { phase = 0; tiltUsed = false; }
			}
		}
		return 0;
	}

	// ------------------------------------------------------------------ escolha + arranque
	// Escolhe o alvo. Com 'extend' o giro e mantido durante 'windowMs'; sem 'extend' so se atualiza
	// o alvo de um giro que ja esta em curso.
	// Um toque de botao: se ja ha sessao so renova o temporizador (o corpo nao muda de alvo por causa
	// do clique); senao escolhe o alvo mais proximo e abre a sessao.
	bool AcquireAndTurn(int windowMs, bool /*extend*/)
	{
		// Lock-on ativo: LockOnMode=0 deixa o SoftTarget em pausa.
		if(LockPauses()) return false;

		if(g_active && !g_cfg.logOnly)
		{
			g_endTick = GetTickCount() + (DWORD)windowMs;
			return true;
		}

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
		g_endTick = GetTickCount() + (DWORD)windowMs;
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

		float camDist = -1.0f, fov = 0.0f;
		__try
		{
			if(cam->cameraNode)
			{
				const NiPoint3 pp = Engine::Position(player);
				const NiPoint3 cp = cam->cameraNode->m_worldTransform.pos;
				camDist = sqrtf((pp.x - cp.x) * (pp.x - cp.x) + (pp.y - cp.y) * (pp.y - cp.y) + (pp.z - cp.z) * (pp.z - cp.z));
			}
			fov = cam->worldFOV;
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { }
		int sloNow = -1, cLock = -1, cExt = -1;
		__try { if(g_sloIsLockOn) sloNow = g_sloIsLockOn() ? 1 : 0; } __except(EXCEPTION_EXECUTE_HANDLER) { }
		__try { if(g_coeusIsLock) cLock = g_coeusIsLock() ? 1 : 0; } __except(EXCEPTION_EXECUTE_HANDLER) { }
		__try { if(g_coeusIsExt) cExt = g_coeusIsExt() ? 1 : 0; } __except(EXCEPTION_EXECUTE_HANDLER) { }

		int frFlag = -1; float frOff = 0.0f, camYawV = 0.0f;
		{
			UInt8 * c1 = ThirdPersonStatePtr();
			if(c1) { frFlag = (int)*(c1 + 0xB4); frOff = *(float *)(c1 + 0xAC); camYawV = *(float *)(c1 + 0x5C); }
		}
		_MESSAGE("probe: state=%u camDist=%.1f fov=%.1f | player rotZ=%.3f rotX=%.3f | camYaw=%.3f freeRot=%d offset=%.3f | lock: ours=%d slo=%d coeusLock=%d coeusExt=%d | active=%d move=%d shift=%d own=%d",
				 st ? (unsigned)st->stateId : 0xFFFFu, camDist, fov, rot.z, rot.x, camYawV, frFlag, frOff,
				 (int)g_lockActive, sloNow, cLock, cExt, (int)g_active, (int)g_moveHeld, (int)g_shiftHeld, (int)g_lockOwn);

		{
			float o[3], d[3];
			if(ThirdPersonStatePtr() && CameraRay(player, o, d))
			{
				const NiPoint3 pp = Engine::Position(player);
				const float v[3] = { pp.x - o[0], pp.y - o[1], (pp.z + g_cfg.playerAimHeight) - o[2] };
				const float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
				if(len > 1.0f) _MESSAGE("probe-cam: axis %d match with camera->player %.2f | dot show=%d shown=%d state=%d skin=%d fallback=%d", g_camAxis, (d[0] * v[0] + d[1] * v[1] + d[2] * v[2]) / len, (int)g_dotShow, (int)g_dotShown, g_dot.state, g_dot.skin, g_dot.fallback);
			}
		}

		{
			int cdir = -1;
			__try { if(g_coeusDirActive) cdir = g_coeusDirActive() ? 1 : 0; } __except(EXCEPTION_EXECUTE_HANDLER) { }
			_MESSAGE("probe-move: coeusDirectional=%d yieldRequested=%d ownLock=%d keys F%d B%d L%d R%d shift=%d | body-to-target %.1f deg | profile=%d CoeusYield=%d",
					 cdir, (int)g_yieldNow, (int)g_lockOwn, (int)g_kFwd, (int)g_kBack, (int)g_kLeft, (int)g_kRight, (int)g_shiftHeld,
					 WrapPi(g_targetYaw - rot.z) * 180.0f / kPi, g_cfg.profileMode, g_cfg.coeusYield);
			_MESSAGE("probe-input: remap=%d found=%d region %d offset 0x%X | writes %ld, game left our value %ld of %ld checks (low = the game/Coeus rewrite the vector)",
					 g_cfg.moveRemap, (int)g_mvFound, g_mvRegion, g_mvWord * 4, g_mvWrites, g_mvKept, g_mvSeen);
		}

		if(st)
		{
			UInt32 w[48];
			if(g_cfg.verbose && SafeReadWords(st, w, 48))
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


	bool InList(const int * list, int n, int code)
	{
		for(int i = 0; i < n; ++i) if(list[i] == code) return true;
		return false;
	}

	// Bloqueio segurado: mantem a sessao viva (e abre-a se ainda nao existir).
	void HoldTick()
	{
		const DWORD now = GetTickCount();
		if(now - g_lastHoldEvent > 250) return;					// botao deixou de chegar: considera solto
		if(now - g_lastScan < (DWORD)g_cfg.retargetMs) return;
		g_lastScan = now;
		AcquireAndTurn(g_cfg.holdMs, true);
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
			if(LockPauses()) { if(g_active) g_endReason = 2; g_active = 0; }

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

				// Lock-on proprio: tecla de ligar/desligar e de trocar de alvo (so fora de menus).
				if(pressedNow && g_cfg.lockEnabled && !MenuBlocksInput())
				{
					if(MatchKey((UInt32)e->deviceType, (int)button->keyMask, g_cfg.lockKey)) LockToggle();
					else if(MatchKey((UInt32)e->deviceType, (int)button->keyMask, g_cfg.lockSwitchKey)) LockSwitch();
				}

				if(e->deviceType == kDeviceType_Keyboard)
				{
					const int code = (int)button->keyMask;
					if(InList(g_shiftKeys, g_numShiftKeys, code)) g_shiftHeld = held ? 1 : 0;

					// Tecla de reserva do lock-on (so quando nao ha LockOn.dll): cada toque liga/desliga.
					if(g_cfg.lockOnKey && !g_sloIsLockOn && pressedNow && code == g_cfg.lockOnKey)
					{
						g_lockKeyState = !g_lockKeyState;
						g_lockNoEnemySince = 0;
						g_lockActive = g_lockKeyState ? 1 : 0;
						if(LockPauses()) g_active = 0;
						_MESSAGE("lock-on key: lock-on assumed %s", g_lockKeyState ? "ON" : "OFF");
					}

					// Botoes extra: toque (ataque carregado, etc.) e segurar (bloqueio de armas duplas, etc.).
					if(pressedNow && g_cfg.tapAssist && InList(g_tapKeys, g_numTapKeys, code))
					{
						g_lastAttackTick = GetTickCount();
						AcquireAndTurn(g_cfg.holdMs, true);
					}
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
				{
					g_lastAttackTick = GetTickCount();
					AcquireAndTurn(g_cfg.holdMs, true);
				}
			}

			const bool holding = ((g_cfg.holdMode & 1) && g_holdLeft) || ((g_cfg.holdMode & 2) && g_holdRight) || g_holdExtra;
			if(holding && (holdBtnSeen || extraHeldNow || GetTickCount() - g_lastHoldEvent <= 250)) HoldTick();

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
				CreateThread(NULL, 0, MvThread, NULL, 0, NULL);
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

	g_taskIface = (SKSETaskInterface *)skse->QueryInterface(kInterface_Task);
	_MESSAGE("task interface: %s", g_taskIface ? "available (the on-screen dot can be drawn)" : "NOT available (no on-screen dot)");

	g_papyrus = (SKSEPapyrusInterface *)skse->QueryInterface(kInterface_Papyrus);
	if(g_papyrus && g_papyrus->Register(RegisterPapyrus))
		_MESSAGE("papyrus interface: registration queued");
	else
		_MESSAGE("papyrus interface not available - the in-game menu will not work (the .ini still does)");

	_MESSAGE("running");
	return true;
}

};
