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
#include "skse/GameThreads.h"
#include "skse/GameData.h"
#include "skse/GameExtraData.h"
#include "skse/SafeWrite.h"
#include "skse/ScaleformMovie.h"
#include "skse/ScaleformCallbacks.h"
#include "Log.h"
#include "Engine.h"

#include <shlobj.h>
#include <math.h>
#include <float.h>
#include <string.h>
#include <stdlib.h>

#define PLUGIN_NAME			"SoftTargetLE"
#define PLUGIN_VERSION_STR	"0.26.1 (paredes sem atraso, objetos finos, zona de borda, salto na viga, Edge acima da viga)"
#define PLUGIN_LOG_FILE		"\\My Games\\Skyrim\\SKSE\\SoftTargetLE.log"
#define PLUGIN_INI_NAME		"SoftTargetLE.ini"
#define PLUGIN_INI_FALLBACK	"Data\\SKSE\\Plugins\\SoftTargetLE.ini"

static PluginHandle				g_pluginHandle	= kPluginHandle_Invalid;
static SKSEMessagingInterface *	g_messaging		= NULL;
static SKSEPapyrusInterface *	g_papyrus		= NULL;
static SKSETaskInterface *		g_taskIface		= NULL;
static DWORD					g_mainTid		= 0;		// thread principal do jogo (a que carrega o plugin)

// LookupREFRByHandle e CreateRefHandleByREFR vem agora do GameReferences.cpp do SKSE (compilado no projeto).

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
		int		predictAlways;	// 1 = com o arco equipado e o lock ligado o ponto de mira ja inclui a previsao (nao so com a corda puxada)
		int		lockTiltAlways;	// 1 = em 3a pessoa com o lock ligado a inclinacao do corpo/cabeca vai sempre para o alvo (qualquer arma)
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
		int		requireLos;			// 1 = nao escolhe alvos sem linha de visao (atras de paredes)
		int		keepBleedout;		// 1 = o alvo ja preso mantem-se enquanto estiver em bleedout / caido (ate morrer de verdade)
		int		dotExact;			// 1 = o ponto usa a projeccao exacta da camera do jogo (NiCamera), senao a estimativa pelo FOV
		int		aiGuard;			// 1 = nao mexe no corpo/camera quando o motor controla o jogador (cenas, scripts)
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
		// ---- v0.12.4
		float	aimFilterMs;	// constante de tempo do filtro do ponto de mira (tira o balanco dos golpes do alvo)
		float	camDeadZoneDeg;	// zona morta da camera do lock, graus
		// ---- v0.12.5
		float	aimAdjust;		// ajuste manual da altura do ponto de mira, unidades (Shift + roda do rato)
		int		aimAdjustWheel;	// 1 = Shift + roda do rato ajusta a altura
		int		aimUpKey;		// tecla alternativa: sobe o ponto de mira (201 = Page Up, 0 = nenhuma)
		int		aimDownKey;		// tecla alternativa: desce o ponto de mira (209 = Page Down)
		// ---- v0.13.0: camera ao ombro com o arco
		int		bowShoulder;	// 1 = ao puxar o arco a camera passa para o ombro (nos dois perfis)
		int		bowRight;		// 1 = ombro direito, 0 = esquerdo
		float	bowShX;			// desvio lateral da camera, unidades
		float	bowShY;			// aproximacao (+) ou afastamento (-) da camera, somado ao valor normal
		float	bowShZ;			// altura da camera, somada ao valor normal
		int		bowSwapKey;		// tecla que troca de ombro enquanto se puxa o arco (18 = E)
		// ---- v0.13.1
		int		bowLockCamera;	// 1 = (perfil FreeCam) com o lock e o arco puxado a camera centra o alvo
		int		lockAutoCombatOnly;	// 1 = a escolha automatica so apanha inimigos em combate
		int		mvLearnKey;		// tecla (com Shift) que inicia a aprendizagem do vetor de movimento (68 = F10)
		// ---- v0.13.2
		int		noSoftFirstPerson;	// 1 = em primeira pessoa o SoftTarget automatico nao atua (so o lock proprio)
		int		noSoftBow;			// 1 = com arco/besta (a puxar) o SoftTarget automatico nao atua
		// ---- v0.14.0: lock suave (perfil FreeCam, primeira pessoa e arco)
		int		lockViewMode;		// vista com o lock em 1a pessoa / arco (FreeCam): 0 = antigo, 1 = suave (rato livre; ao atacar o lock toma o controlo), 2 = duro (vista presa ao alvo, rato bloqueado)
		int		lockFreeLookKey;	// tecla que, enquanto premida, liberta a vista (42 = Shift; 0 = nenhuma)
		int		lockMouseYieldMs;	// depois de mexer o rato o puxao fica a zero durante este tempo
		int		lockMouseRampMs;	// e volta a subir ate ao normal durante este tempo
		float	lockSoftSmooth;		// suavidade do puxao (maior = mais depressa)
		float	lockSoftSpeedDeg;	// velocidade maxima do puxao, graus por segundo
		float	lockSoftDeadZone;	// zona morta, graus
		// ---- v0.15.0: compatibilidade DMK (variaveis de animacao do jogador para pacotes OAR)
		int		dmkKeytrace;		// 1 = adiciona uma vez as 4 habilidades do Keytrace.esp (LE) ao jogador (pacotes que usam HasMagicEffect Keytrace)
		int		levelPosture;		// 1 = ao sair da 1a pessoa (sem lock/arco) a inclinacao do corpo volta ao nivel
		int		dmkCompat;			// 1 = publica DirecionalCycleMoveset (direcao do movimento) e CameraMovementCMF (direcao da camera)
		// ---- v0.18.2
		int		bowReleaseTailMs;	// depois de soltar o arco mantem o comportamento de "puxando" durante este tempo (0 = desligado)
		int		losWaitMs;			// o lock espera ate este tempo pelo primeiro resultado da linha de visao
		int		losGraceMs;			// um alvo visto ha menos deste tempo nao e dado como bloqueado
		int		lockVertForgive;	// 1 = para prender/manter o lock conta so a diferenca horizontal quando o alvo esta muito acima/abaixo (encostas)
		int		sprintFreeBody;		// 1 = com o lock, correr (Shift) deixa o corpo seguir a direcao das teclas; ao largar volta ao alvo
		int		noAmmoGuard;		// 1 = arco sem flechas equipadas nao conta como "a mirar"
		int		lockCamLimit;		// 1 = limita a inclinacao da camera de 3a pessoa com o lock (evita chao / 1a pessoa)
		float	lockCamUpDeg;		// maximo a olhar para cima, graus
		int		lockBowInstant;		// v0.25.0: 1 = com o arco puxado a camera do lock segue o alvo sem atraso (a previsao so serve assim)
		int		lockArrowTilt;		// v0.25.0: 1 = com o lock e um arco, a flecha sai mais reta (INI f1P/f3PArrowTiltUpAngle), reposto ao largar
		int		beamRail;			// v0.25.0: 1 = em cima da viga o movimento lateral e removido (trilho) e o jogador e puxado ao centro
		int		edgeZone;			// v0.26.1: 1 = a menos de ~35 unidades de uma queda >=100 em chao plano, BeamWalkOnBeam=1 (animacoes da viga na ponta)
		int		edgeGuard;			// v0.26.1: 1 = na zona de borda (queda a <20) o movimento em direcao a queda e removido (saltar solta-te)
		int		envThin;			// v0.26.1: 1 = leque extra de raios (+-10/20 graus) para paredes finas, pilares e troncos
		int		frameLog;			// v0.25.0: 1 = regista o tempo de frame no log (para comparar com os sensores ligados/desligados)
		int		lockLatencyComp;	// v0.25.1: 1 = a mira do lock adianta-se pela velocidade do alvo x ~1,5 frames (compensa o atraso de leitura da camera)
		int		singleWriter;		// v0.25.0: 1 = as escritas da camera do lock fazem-se uma vez por frame, na thread do jogo
		// ---- v0.19.0
		int		envProbe;			// 1 = sensores do ambiente (chao, inclinacao, obstaculos a frente, saliencia): log + globais do SoftTargetEnv.esp
		// ---- v0.20.0
		int		envJump;			// 1 = ST_JumpObstacle: trava enquanto dura um salto iniciado com obstaculo alto a frente
		int		envInCombat;		// 1 = os sensores tambem correm em combate (por omissao so fora de combate)
		float	jumpObstacleMin;	// altura minima do obstaculo para ST_JumpObstacle (RaySense usa 66)
		float	envWalkDist;		// distancia de deteccao a andar
		float	envSprintDist;		// distancia de deteccao a correr
		// ---- v0.22.0
		int		envPerf;			// 0 = leve (PC fraco, por omissao), 1 = normal, 2 = potente: ritmo dos sensores
		int		envDemand;			// 1 = so corre os sensores que as animacoes (OAR) usam de facto
		float	envHoldMs;			// tempo que o valor mais alto do obstaculo se mantem (ms)
		// ---- v0.23.0
		float	obsMinDist;			// distancia minima ao obstaculo para o salto de obstaculo (mais perto fica para o FreeRun)
		int		jumpBonus;			// 1 = aumenta a altura do salto so nos saltos de obstaculo
		float	jumpClearance;		// folga acima do topo do obstaculo (unidades)
		float	jumpMaxMult;		// multiplicador maximo da altura do salto
		int		keytraceCombatOnly;	// 1 = a mascara Keytrace so se calcula com arma sacada e lock/alvo travado
		// ---- v0.24.0
		int		lockIgnoreEssential;// 1 = (com "so inimigos" desligado) nao prende essenciais, seguidores nem o cavalo do jogador
		int		lockMounted;		// 1 = no cavalo o lock tambem prende a camera e a mira (camera do cavalo)
		int		lockFreePitch;		// 1 = com a camera livre (Coeus) a inclinacao vertical da camera segue o alvo (campo 0xB0)
		int		beamAssist;			// 1 = em cima de uma viga a vista/corpo alinha-se com o eixo da viga e e puxada de volta ao centro
		float	beamTau;			// constante de tempo do alinhamento (s)
		float	beamMaxRate;		// velocidade maxima do giro (rad/s)
		float	beamLatGain;		// giro extra pelo erro lateral (rad ao erro maximo)
		float	beamDead;			// zona morta lateral (fracao da meia-largura)
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
		{ "Shape",					'i', &g_cfg.shape,				1.0f },
		{ "Range",					'f', &g_cfg.range,				450.0f },
		{ "ConeDegrees",			'f', &g_cfg.coneDeg,			140.0f },
		{ "MaxHeightDifference",	'f', &g_cfg.maxHeightDiff,		600.0f },
		{ "SwitchMarginPercent",	'f', &g_cfg.switchMargin,		20.0f },
		{ "TurnSpeedDegPerSec",		'f', &g_cfg.turnSpeedDeg,		900.0f },
		{ "Smoothing",				'f', &g_cfg.smoothing,			15.0f },
		{ "UpdateMilliseconds",		'i', &g_cfg.updateMs,			1.0f },
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
		{ "ProfileMode",			'i', &g_cfg.profileMode,		1.0f },
		{ "LockOnSwitchKey",		'i', &g_cfg.lockOnSwitchKey,	0.0f },
		{ "AllowReanimated",		'i', &g_cfg.allowReanimated,	1.0f },
		{ "LockEnabled",			'i', &g_cfg.lockEnabled,		1.0f },
		{ "LockKey",				'i', &g_cfg.lockKey,			33.0f },
		{ "LockSwitchKey",			'i', &g_cfg.lockSwitchKey,		258.0f },
		{ "LockRange",				'f', &g_cfg.lockRange,			2000.0f },
		{ "LockAcquireDegrees",		'f', &g_cfg.lockAcquireDeg,		30.0f },
		{ "LockHostileOnly",		'i', &g_cfg.lockHostileOnly,	1.0f },
		{ "LockLoseSightMs",		'i', &g_cfg.lockLoseSightMs,	3000.0f },
		{ "LockLoseAngleDegrees",	'f', &g_cfg.lockLoseAngleDeg,	95.0f },
		{ "LockAutoNext",			'i', &g_cfg.lockAutoNext,		1.0f },
		{ "LockCameraFollow",		'i', &g_cfg.lockCameraFollow,	0.0f },
		{ "LockCameraPitch",		'i', &g_cfg.lockCameraPitch,	1.0f },
		{ "LockCameraSmooth",		'f', &g_cfg.lockCameraSmooth,	5.0f },
		{ "PredictEnabled",			'i', &g_cfg.predictEnabled,		1.0f },
		{ "PredictAlways",			'i', &g_cfg.predictAlways,		1.0f },
		{ "LockTiltAlways",			'i', &g_cfg.lockTiltAlways,		1.0f },
		{ "ProjectileSpeed",		'f', &g_cfg.projectileSpeed,	9000.0f },
		{ "ProjectileGravity",		'f', &g_cfg.projectileGravity,	0.0f },
		{ "PredictStrength",		'f', &g_cfg.predictStrength,	100.0f },
		{ "PredictMinDistance",		'f', &g_cfg.predictMinDist,		150.0f },
		{ "LockCameraAxis",			'i', &g_cfg.lockCameraAxis,		1.0f },
		{ "LockAutoCircleRange",	'f', &g_cfg.lockAutoCircle,		900.0f },
		{ "LockAutoConeDegrees",	'f', &g_cfg.lockAutoCone,		60.0f },
		{ "CameraHoldTdm",			'i', &g_cfg.cameraHoldTdm,		1.0f },
		{ "CreateHandles",			'i', &g_cfg.createHandles,		1.0f },
		{ "DotLock",				'i', &g_cfg.dotLock,			1.0f },
		{ "DotSoftTarget",			'i', &g_cfg.dotSoft,			1.0f },
		{ "DotSkin",				'i', &g_cfg.dotSkin,			3.0f },
		{ "DotSize",				'f', &g_cfg.dotSize,			100.0f },
		{ "DotColor",				'i', &g_cfg.dotColor,			0.0f },
		{ "DotOpacity",				'f', &g_cfg.dotOpacity,			100.0f },
		{ "RequireLineOfSight",		'i', &g_cfg.requireLos,		1.0f },
		{ "KeepBleedoutTarget",		'i', &g_cfg.keepBleedout,		1.0f },
		{ "DotExactProject",		'i', &g_cfg.dotExact,			1.0f },
		{ "AiDrivenGuard",			'i', &g_cfg.aiGuard,			1.0f },
		{ "DotTdmType",				'i', &g_cfg.dotTdmType,			1.0f },
		{ "MoveRemap",				'i', &g_cfg.moveRemap,			1.0f },
		{ "SprintKeepsAim",			'i', &g_cfg.sprintKeepsAim,		1.0f },
		{ "MoveVecRegion",			'i', &g_cfg.mvRegion,			-1.0f },
		{ "MoveVecOffset",			'i', &g_cfg.mvOffset,			0.0f },
		{ "AimOffsetX",				'f', &g_cfg.aimOffX,			0.0f },
		{ "AimOffsetY",				'f', &g_cfg.aimOffY,			0.0f },
		{ "SpellProjectileSpeed",	'f', &g_cfg.spellSpeed,			2500.0f },
		{ "ProjectileSpeedScale",	'f', &g_cfg.speedScale,			100.0f },
		{ "UseSpellDataSpeed",		'i', &g_cfg.useFormSpeed,		1.0f },
		{ "AimFilterMs",			'f', &g_cfg.aimFilterMs,		600.0f },
		{ "CameraDeadZoneDegrees",	'f', &g_cfg.camDeadZoneDeg,		3.5f },
		{ "AimHeightAdjust",		'f', &g_cfg.aimAdjust,			0.0f },
		{ "AimAdjustWheel",			'i', &g_cfg.aimAdjustWheel,		1.0f },
		{ "AimAdjustUpKey",			'i', &g_cfg.aimUpKey,			201.0f },
		{ "AimAdjustDownKey",		'i', &g_cfg.aimDownKey,			209.0f },
		{ "BowShoulder",			'i', &g_cfg.bowShoulder,		0.0f },
		{ "BowShoulderRight",		'i', &g_cfg.bowRight,			1.0f },
		{ "BowShoulderX",			'f', &g_cfg.bowShX,				40.0f },
		{ "BowShoulderY",			'f', &g_cfg.bowShY,				0.0f },
		{ "BowShoulderZ",			'f', &g_cfg.bowShZ,				10.0f },
		{ "BowShoulderSwapKey",		'i', &g_cfg.bowSwapKey,			18.0f },
		{ "BowLockCamera",			'i', &g_cfg.bowLockCamera,		1.0f },
		{ "LockAutoCombatOnly",		'i', &g_cfg.lockAutoCombatOnly,	1.0f },
		{ "MoveLearnKey",			'i', &g_cfg.mvLearnKey,			68.0f },
		{ "NoSoftTargetFirstPerson",'i', &g_cfg.noSoftFirstPerson,	1.0f },
		{ "NoSoftTargetBow",		'i', &g_cfg.noSoftBow,			1.0f },
		{ "LockViewMode",			'i', &g_cfg.lockViewMode,		2.0f },
		{ "LockFreeLookKey",		'i', &g_cfg.lockFreeLookKey,	42.0f },
		{ "LockMouseYieldMs",		'i', &g_cfg.lockMouseYieldMs,	150.0f },
		{ "LockMouseRampMs",		'i', &g_cfg.lockMouseRampMs,	400.0f },
		{ "LockSoftSmooth",			'f', &g_cfg.lockSoftSmooth,		6.0f },
		{ "LockSoftSpeedDegPerSec",	'f', &g_cfg.lockSoftSpeedDeg,	240.0f },
		{ "LockSoftDeadZoneDegrees",'f', &g_cfg.lockSoftDeadZone,	0.6f },
		{ "LevelPosture",			'i', &g_cfg.levelPosture,		1.0f },
		{ "DmkCompat",				'i', &g_cfg.dmkCompat,			0.0f },
		{ "DmkKeytrace",			'i', &g_cfg.dmkKeytrace,		1.0f },
		{ "BowReleaseTailMs",		'i', &g_cfg.bowReleaseTailMs,	400.0f },
		{ "LosWaitMs",				'i', &g_cfg.losWaitMs,			40.0f },
		{ "LosGraceMs",				'i', &g_cfg.losGraceMs,			1500.0f },
		{ "LockVerticalForgive",	'i', &g_cfg.lockVertForgive,	1.0f },
		{ "SprintFreeBody",			'i', &g_cfg.sprintFreeBody,		1.0f },
		{ "NoAmmoGuard",			'i', &g_cfg.noAmmoGuard,		1.0f },
		{ "LockCamLimit",			'i', &g_cfg.lockCamLimit,		1.0f },
		{ "LockCamUpDegrees",		'f', &g_cfg.lockCamUpDeg,		40.0f },
		{ "LockBowInstant",		'i', &g_cfg.lockBowInstant,	1.0f },
		{ "LockArrowTilt",			'i', &g_cfg.lockArrowTilt,		1.0f },
		{ "BeamRail",				'i', &g_cfg.beamRail,			1.0f },
		{ "EdgeZone",				'i', &g_cfg.edgeZone,			1.0f },
		{ "EdgeGuard",				'i', &g_cfg.edgeGuard,		1.0f },
		{ "EnvThinFan",			'i', &g_cfg.envThin,			1.0f },
		{ "FrameTimeLog",			'i', &g_cfg.frameLog,			1.0f },
		{ "LockLatencyComp",		'i', &g_cfg.lockLatencyComp,	1.0f },
		{ "SingleRotationWriter",	'i', &g_cfg.singleWriter,		0.0f },
		{ "EnvProbe",				'i', &g_cfg.envProbe,			1.0f },
		{ "EnvJumpObstacle",		'i', &g_cfg.envJump,			1.0f },
		{ "EnvInCombat",			'i', &g_cfg.envInCombat,		0.0f },
		{ "JumpObstacleMin",		'f', &g_cfg.jumpObstacleMin,	44.0f },
		{ "EnvWalkDistance",		'f', &g_cfg.envWalkDist,		230.0f },
		{ "EnvPerformance",			'i', &g_cfg.envPerf,			2.0f },
		{ "EnvDemandScan",			'i', &g_cfg.envDemand,			1.0f },
		{ "EnvHoldMs",				'f', &g_cfg.envHoldMs,			300.0f },
		{ "ObstacleJumpMinDistance",'f', &g_cfg.obsMinDist,			120.0f },
		{ "ObstacleJumpBonus",		'i', &g_cfg.jumpBonus,			1.0f },
		{ "ObstacleJumpClearance",	'f', &g_cfg.jumpClearance,		40.0f },
		{ "ObstacleJumpMaxMult",	'f', &g_cfg.jumpMaxMult,		3.3f },
		{ "KeytraceCombatOnly",		'i', &g_cfg.keytraceCombatOnly,	1.0f },
		{ "EnvSprintDistance",		'f', &g_cfg.envSprintDist,		330.0f },
		{ "LockIgnoreEssential",	'i', &g_cfg.lockIgnoreEssential,1.0f },
		{ "LockMounted",			'i', &g_cfg.lockMounted,		1.0f },
		{ "LockFreeCamPitch",		'i', &g_cfg.lockFreePitch,		1.0f },
		{ "BeamAssist",				'i', &g_cfg.beamAssist,			1.0f },
		{ "BeamTau",				'f', &g_cfg.beamTau,			0.15f },
		{ "BeamMaxYawRate",			'f', &g_cfg.beamMaxRate,		2.0f },
		{ "BeamLateralGain",		'f', &g_cfg.beamLatGain,		0.6f },
		{ "BeamDeadzone",			'f', &g_cfg.beamDead,			0.30f },
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
		if(g_cfg.beamTau < 0.03f)			g_cfg.beamTau = 0.03f;
		if(g_cfg.beamTau > 1.0f)			g_cfg.beamTau = 1.0f;
		if(g_cfg.beamMaxRate < 0.3f)		g_cfg.beamMaxRate = 0.3f;
		if(g_cfg.beamMaxRate > 6.0f)		g_cfg.beamMaxRate = 6.0f;
		if(g_cfg.beamLatGain < 0.0f)		g_cfg.beamLatGain = 0.0f;
		if(g_cfg.beamLatGain > 1.5f)		g_cfg.beamLatGain = 1.5f;
		if(g_cfg.beamDead < 0.0f)			g_cfg.beamDead = 0.0f;
		if(g_cfg.beamDead > 0.9f)			g_cfg.beamDead = 0.9f;
		if(g_cfg.lockCameraSmooth < 1.0f)	g_cfg.lockCameraSmooth = 1.0f;
		if(g_cfg.projectileSpeed < 500.0f)	g_cfg.projectileSpeed = 500.0f;
		if(g_cfg.predictAlways < 0)			g_cfg.predictAlways = 0;
		if(g_cfg.predictAlways > 1)			g_cfg.predictAlways = 1;
		if(g_cfg.lockTiltAlways < 0)		g_cfg.lockTiltAlways = 0;
		if(g_cfg.lockTiltAlways > 1)		g_cfg.lockTiltAlways = 1;
		if(g_cfg.spellSpeed < 300.0f)		g_cfg.spellSpeed = 300.0f;
		if(g_cfg.bowShX < 0.0f)				g_cfg.bowShX = 0.0f;
		if(g_cfg.bowShX > 120.0f)			g_cfg.bowShX = 120.0f;
		if(g_cfg.bowShY < -100.0f)			g_cfg.bowShY = -100.0f;
		if(g_cfg.bowShY >  100.0f)			g_cfg.bowShY =  100.0f;
		if(g_cfg.bowShZ < -60.0f)			g_cfg.bowShZ = -60.0f;
		if(g_cfg.bowShZ >  80.0f)			g_cfg.bowShZ =  80.0f;
		if(g_cfg.aimAdjust < -60.0f)		g_cfg.aimAdjust = -60.0f;
		if(g_cfg.aimAdjust >  60.0f)		g_cfg.aimAdjust =  60.0f;
		if(g_cfg.aimFilterMs < 0.0f)		g_cfg.aimFilterMs = 0.0f;
		if(g_cfg.aimFilterMs > 1500.0f)		g_cfg.aimFilterMs = 1500.0f;
		if(g_cfg.camDeadZoneDeg < 0.0f)		g_cfg.camDeadZoneDeg = 0.0f;
		if(g_cfg.camDeadZoneDeg > 8.0f)		g_cfg.camDeadZoneDeg = 8.0f;
		if(g_cfg.lockMouseYieldMs < 0)		g_cfg.lockMouseYieldMs = 0;
		if(g_cfg.lockMouseYieldMs > 1000)	g_cfg.lockMouseYieldMs = 1000;
		if(g_cfg.lockMouseRampMs < 1)		g_cfg.lockMouseRampMs = 1;
		if(g_cfg.lockMouseRampMs > 3000)	g_cfg.lockMouseRampMs = 3000;
		if(g_cfg.lockSoftSmooth < 0.5f)		g_cfg.lockSoftSmooth = 0.5f;
		if(g_cfg.lockSoftSmooth > 40.0f)	g_cfg.lockSoftSmooth = 40.0f;
		if(g_cfg.lockSoftSpeedDeg < 10.0f)	g_cfg.lockSoftSpeedDeg = 10.0f;
		if(g_cfg.lockSoftSpeedDeg > 1440.0f)g_cfg.lockSoftSpeedDeg = 1440.0f;
		if(g_cfg.lockSoftDeadZone < 0.0f)	g_cfg.lockSoftDeadZone = 0.0f;
		if(g_cfg.lockSoftDeadZone > 5.0f)	g_cfg.lockSoftDeadZone = 5.0f;
		if(g_cfg.levelPosture < 0)			g_cfg.levelPosture = 0;
		if(g_cfg.levelPosture > 1)			g_cfg.levelPosture = 1;
		if(g_cfg.dmkCompat < 0)				g_cfg.dmkCompat = 0;
		if(g_cfg.dmkCompat > 1)				g_cfg.dmkCompat = 1;
		if(g_cfg.dmkKeytrace < 0)			g_cfg.dmkKeytrace = 0;
		if(g_cfg.dmkKeytrace > 1)			g_cfg.dmkKeytrace = 1;
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
		if(g_cfg.bowReleaseTailMs < 0) g_cfg.bowReleaseTailMs = 0;
		if(g_cfg.bowReleaseTailMs > 2000) g_cfg.bowReleaseTailMs = 2000;
		if(g_cfg.losWaitMs < 0) g_cfg.losWaitMs = 0;
		if(g_cfg.losWaitMs > 200) g_cfg.losWaitMs = 200;
		if(g_cfg.losGraceMs < 0) g_cfg.losGraceMs = 0;
		if(g_cfg.losGraceMs > 10000) g_cfg.losGraceMs = 10000;
		if(g_cfg.lockVertForgive < 0) g_cfg.lockVertForgive = 0;
		if(g_cfg.lockVertForgive > 1) g_cfg.lockVertForgive = 1;
		if(g_cfg.sprintFreeBody < 0) g_cfg.sprintFreeBody = 0;
		if(g_cfg.sprintFreeBody > 1) g_cfg.sprintFreeBody = 1;
		if(g_cfg.noAmmoGuard < 0) g_cfg.noAmmoGuard = 0;
		if(g_cfg.noAmmoGuard > 1) g_cfg.noAmmoGuard = 1;
		if(g_cfg.lockCamLimit < 0) g_cfg.lockCamLimit = 0;
		if(g_cfg.lockCamLimit > 1) g_cfg.lockCamLimit = 1;
		if(g_cfg.lockCamUpDeg < 10.0f) g_cfg.lockCamUpDeg = 10.0f;
		if(g_cfg.lockCamUpDeg > 80.0f) g_cfg.lockCamUpDeg = 80.0f;
		if(g_cfg.envProbe < 0) g_cfg.envProbe = 0;
		if(g_cfg.envProbe > 1) g_cfg.envProbe = 1;
		if(g_cfg.envJump < 0) g_cfg.envJump = 0;
		if(g_cfg.envJump > 1) g_cfg.envJump = 1;
		if(g_cfg.envInCombat < 0) g_cfg.envInCombat = 0;
		if(g_cfg.envInCombat > 1) g_cfg.envInCombat = 1;
		if(g_cfg.jumpObstacleMin < 20.0f) g_cfg.jumpObstacleMin = 20.0f;
		if(g_cfg.jumpObstacleMin > 200.0f) g_cfg.jumpObstacleMin = 200.0f;
		if(g_cfg.envWalkDist < 80.0f) g_cfg.envWalkDist = 80.0f;
		if(g_cfg.envWalkDist > 600.0f) g_cfg.envWalkDist = 600.0f;
		if(g_cfg.envSprintDist < 80.0f) g_cfg.envSprintDist = 80.0f;
		if(g_cfg.envSprintDist > 600.0f) g_cfg.envSprintDist = 600.0f;
		if(g_cfg.envPerf < 0) g_cfg.envPerf = 0;
		if(g_cfg.envPerf > 2) g_cfg.envPerf = 2;
		if(g_cfg.envDemand < 0) g_cfg.envDemand = 0;
		if(g_cfg.envDemand > 1) g_cfg.envDemand = 1;
		if(g_cfg.envHoldMs < 0.0f) g_cfg.envHoldMs = 0.0f;
		if(g_cfg.envHoldMs > 1000.0f) g_cfg.envHoldMs = 1000.0f;
		if(g_cfg.obsMinDist < 0.0f) g_cfg.obsMinDist = 0.0f;
		if(g_cfg.obsMinDist > 300.0f) g_cfg.obsMinDist = 300.0f;
		if(g_cfg.jumpBonus < 0) g_cfg.jumpBonus = 0;
		if(g_cfg.jumpBonus > 1) g_cfg.jumpBonus = 1;
		if(g_cfg.jumpClearance < 0.0f) g_cfg.jumpClearance = 0.0f;
		if(g_cfg.jumpClearance > 100.0f) g_cfg.jumpClearance = 100.0f;
		if(g_cfg.jumpMaxMult < 1.0f) g_cfg.jumpMaxMult = 1.0f;
		if(g_cfg.jumpMaxMult > 4.0f) g_cfg.jumpMaxMult = 4.0f;
		if(g_cfg.keytraceCombatOnly < 0) g_cfg.keytraceCombatOnly = 0;
		if(g_cfg.keytraceCombatOnly > 1) g_cfg.keytraceCombatOnly = 1;
		if(g_cfg.requireLos < 0) g_cfg.requireLos = 0;
		if(g_cfg.requireLos > 1) g_cfg.requireLos = 1;
		if(g_cfg.keepBleedout < 0) g_cfg.keepBleedout = 0;
		if(g_cfg.keepBleedout > 1) g_cfg.keepBleedout = 1;
		if(g_cfg.dotExact < 0) g_cfg.dotExact = 0;
		if(g_cfg.dotExact > 1) g_cfg.dotExact = 1;
		if(g_cfg.aiGuard < 0) g_cfg.aiGuard = 0;
		if(g_cfg.aiGuard > 1) g_cfg.aiGuard = 1;
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
	void MvArm();
	int MvLearnState();
	int MvFoundOffset();

	// Estado para o menu: 0 = perfil efetivo (1 sem TDM, 2 Coeus), 1 = LockOn.dll, 2 = Coeus.dll (0/1), 3 = lock-on ativo
	void N_LearnMovement(StaticFunctionTag *) { MvArm(); }

	SInt32 N_Status(StaticFunctionTag *, SInt32 what)
	{
		switch(what)
		{
			case 0: return InCoeusProfile() ? 2 : 1;
			case 1: return g_sloIsLockOn ? 1 : 0;
			case 2: return (g_coeusExternal || g_coeusDirActive) ? 1 : 0;
			case 3: return (g_lockActive || g_lockOwn) ? 1 : 0;
			case 4: return DotFilesFound();
			case 5: return MvLearnState();
			case 6: return MvFoundOffset();
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
		registry->RegisterFunction(new NativeFunction0 <StaticFunctionTag, void>("LearnMovement", "SoftTargetNative", N_LearnMovement, registry));
		registry->RegisterFunction(new NativeFunction0 <StaticFunctionTag, void>("SaveProfile", "SoftTargetNative", N_SaveProfile, registry));
		registry->RegisterFunction(new NativeFunction0 <StaticFunctionTag, SInt32>("LoadProfile", "SoftTargetNative", N_LoadProfile, registry));
		registry->RegisterFunction(new NativeFunction0 <StaticFunctionTag, void>("ResetDefaults", "SoftTargetNative", N_ResetDefaults, registry));
		registry->RegisterFunction(new NativeFunction1 <StaticFunctionTag, SInt32, SInt32>("Status", "SoftTargetNative", N_Status, registry));
		_MESSAGE("papyrus functions registered (SoftTargetNative)");
		return true;
	}

	// ------------------------------------------------------------------ escolha do alvo
	UInt32 g_lastTargetId = 0;
	volatile LONG	g_endReason = 0;		// por que a ultima sessao acabou: 0 tempo, 1 correr, 2 lock-on externo, 3 sem alvo, 4 lock-on proprio desligado, 5 primeira pessoa/arco
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
	int CollectPoints(Actor * actor, float * pts, int maxPts, float * chest = NULL, bool * hasChest = NULL)
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
				// Osso do peito (humanos): ponto de mira estavel, nao muda com a arma nem com os bracos.
				if(chest && hasChest && !*hasChest && nd->m_name && (strstr(nd->m_name, "Spn2") || strstr(nd->m_name, "Spine2")))
				{
					chest[0] = p.x; chest[1] = p.y; chest[2] = p.z;
					*hasChest = true;
				}
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

	// ------------------------------------------------------------------ linha de visao (v0.18.1)
	// Rotina do proprio jogo (a que o Papyrus Actor.HasLOS usa): 0x008C0FA0, cdecl (viewer, alvo, bool* resultado), devolve true se correu.
	// Faz testes de raios: so corre na thread principal. Nas outras threads responde-se pela cache, que uma tarefa da thread do jogo preenche.
	struct LosEntry { UInt32 id; DWORD tick; int res; DWORD seen; int logged; };
	LosEntry		g_losCache[64];
	UInt32			g_losPend[16];
	volatile LONG	g_losTaskQueued = 0;
	int				g_losLogN = 0;
	bool			g_snapReset = false;				// pedido para refazer as leituras de camera do arco (inicio da puxada)
	DWORD			g_drawTraceUntil = 0;				// diagnostico: regista o erro de mira logo apos o inicio da puxada
	typedef bool (__cdecl * HasLosFn)(Actor * viewer, TESObjectREFR * target, bool * result);

	int LosDirect(Actor * a)
	{
		int r = -1;
		__try
		{
			PlayerCharacter * p = Engine::Player();
			if(p && a)
			{
				bool res = false;
				HasLosFn fn = (HasLosFn)0x008C0FA0;
				if(fn((Actor *)p, (TESObjectREFR *)a, &res)) r = res ? 1 : 0;
			}
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { r = -1; }
		return r;
	}

	// Devolve true se o resultado mudou desde o ultimo registado (so se escreve no log quando muda).
	bool LosStore(UInt32 id, int res)
	{
		const DWORD now = GetTickCount();
		int slot = -1; DWORD oldest = 0; int oldestAt = 0;
		for(int i = 0; i < 64; ++i)
		{
			if(g_losCache[i].id == id) { slot = i; break; }
			const DWORD age = now - g_losCache[i].tick;
			if(g_losCache[i].id == 0) { slot = i; break; }
			if(age >= oldest) { oldest = age; oldestAt = i; }
		}
		if(slot < 0) slot = oldestAt;
		if(g_losCache[slot].id != id) { g_losCache[slot].seen = 0; g_losCache[slot].logged = 0; }
		g_losCache[slot].id = id; g_losCache[slot].tick = now; g_losCache[slot].res = res;
		if(res == 1) g_losCache[slot].seen = now;
		const bool changed = (g_losCache[slot].logged != res + 1);
		g_losCache[slot].logged = res + 1;
		return changed;
	}

	class LosTask : public TaskDelegate
	{
	public:
		virtual void Run(void)
		{
			UInt32 ids[16]; int n = 0;
			for(int i = 0; i < 16; ++i) { const UInt32 v = (UInt32)InterlockedExchange((volatile LONG *)&g_losPend[i], 0); if(v) ids[n++] = v; }
			for(int i = 0; i < n && i < 8; ++i)
			{
				TESForm * f = LookupFormByID(ids[i]);
				if(!f || f->formType != kFormType_Character) continue;
				Actor * a = DYNAMIC_CAST(f, TESForm, Actor);
				if(!a) continue;
				const int r = LosDirect(a);
				if(r >= 0)
				{
					if(LosStore(ids[i], r) && g_losLogN < 150) { ++g_losLogN; _MESSAGE("los: %08X -> %s (game thread task)", (unsigned)ids[i], r ? "visible" : "BLOCKED"); }
				}
			}
			g_losTaskQueued = 0;
		}
		virtual void Dispose(void) { delete this; }
	};

	// 1 = ha linha de visao (ou desconhecido: nao se bloqueia), 0 = sem linha de visao.
	// v0.18.2: um alvo visto ha menos de LosGraceMs nao e dado como bloqueado (encostas, arvores, curvas do terreno);
	// wait = o lock (tecla) espera ate LosWaitMs pelo primeiro resultado, para nao escapar para tras de uma parede.
	int LosApplyGrace(int found, const LosEntry & e, DWORD now)
	{
		if(found == 0 && g_cfg.losGraceMs > 0 && e.seen && (DWORD)(now - e.seen) < (DWORD)g_cfg.losGraceMs) return 1;
		return found;
	}

	int LosState(Actor * a, bool wait = false)
	{
		if(!g_cfg.requireLos || !a) return 1;
		const DWORD now = GetTickCount();
		if(g_mainTid && GetCurrentThreadId() == g_mainTid)
		{
			const int r = LosDirect(a);
			if(r < 0) return 1;
			if(LosStore(a->formID, r) && g_losLogN < 150) { ++g_losLogN; _MESSAGE("los: %08X -> %s (main thread)", (unsigned)a->formID, r ? "visible" : "BLOCKED"); }
			for(int i = 0; i < 64; ++i) if(g_losCache[i].id == a->formID) return LosApplyGrace(r, g_losCache[i], now);
			return r;
		}
		int found = -1; bool stale = true; int slotAt = -1;
		for(int i = 0; i < 64; ++i)
			if(g_losCache[i].id == a->formID) { found = g_losCache[i].res; stale = (now - g_losCache[i].tick) > 400; slotAt = i; break; }
		if(stale && g_taskIface)
		{
			bool queued = false;
			for(int i = 0; i < 16 && !queued; ++i)
			{
				if(g_losPend[i] == a->formID) { queued = true; break; }
				if(InterlockedCompareExchange((volatile LONG *)&g_losPend[i], (LONG)a->formID, 0) == 0) queued = true;
			}
			if(queued && InterlockedExchange(&g_losTaskQueued, 1) == 0) g_taskIface->AddTask(new LosTask());
			// primeira vez (sem resultado): espera um instante pela tarefa da thread do jogo
			if(found < 0 && wait && g_cfg.losWaitMs > 0)
			{
				const DWORD t0 = GetTickCount();
				while((DWORD)(GetTickCount() - t0) < (DWORD)g_cfg.losWaitMs)
				{
					Sleep(2);
					bool got = false;
					for(int i = 0; i < 64; ++i)
						if(g_losCache[i].id == a->formID && (DWORD)(g_losCache[i].tick - t0) < 100000u) { found = g_losCache[i].res; slotAt = i; got = true; break; }
					if(got) break;
				}
			}
		}
		if(found < 0) return 1;
		if(slotAt >= 0 && g_losCache[slotAt].id == a->formID) return LosApplyGrace(found, g_losCache[slotAt], now);
		return found;
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
			if(life != 0 && !(g_cfg.allowReanimated && life == 4) && !(g_cfg.keepBleedout && (life == 7 || life == 8) && actor->formID == g_lastTargetId)) continue;
			if(!hostile) continue;
			if(g_cfg.requireCombat && !listed && !actor->IsInCombat()) continue;

			// Corpo: ponto mais perto (na horizontal) e centro do corpo.
			float pts[3 * 128];
			bool hasChest = false;
			float chestPos[3] = { 0.0f, 0.0f, 0.0f };
			const int n = CollectPoints(actor, pts, 128, chestPos, &hasChest);
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
				if(hasChest) { c[0] = chestPos[0]; c[1] = chestPos[1]; c[2] = chestPos[2]; }
				c[2] += g_cfg.aimAdjust;
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
				if(LosState(actor) == 0) continue;			// atras de uma parede
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

	// v0.24.0: camera do cavalo. HorseCameraState herda de ThirdPersonState: mesmos campos (0xAC giro livre, 0xB0 inclinacao livre, 0xB4 ativo).
	UInt8 * HorseStatePtr()
	{
		PlayerCamera * cam = PlayerCamera::GetSingleton();
		if(!cam || !cam->cameraState) return NULL;
		return (cam->cameraState->stateId == PlayerCamera::kCameraState_Horse) ? (UInt8 *)cam->cameraState : NULL;
	}
	bool IsMountedCam() { return HorseStatePtr() != NULL; }

	// O cavalo que o jogador montou por ultimo (manipulador em PlayerCharacter+0x5AC).
	bool IsPlayerMount(Actor * a, PlayerCharacter * player)
	{
		__try
		{
			UInt32 h = *(UInt32 *)((UInt8 *)player + 0x5AC);
			if(!h) return false;
			TESObjectREFR * ref = NULL;
			bool same = false;
			if(LookupREFRByHandle(&h, &ref) && ref) { same = (ref == (TESObjectREFR *)a); ref->handleRefObject.DecRefHandle(); }
			return same;
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	// Essencial: bit 18 de Actor+0x138 (TESV 0x008DCB30). Aliado do jogador (seguidor): bit 26 de Actor+0x7C (TESV 0x008DC030).
	bool IsEssentialActor(Actor * a) { return ((*(UInt32 *)((UInt8 *)a + 0x138)) >> 18) & 1; }
	bool IsTeammateActor(Actor * a) { return ((*(UInt32 *)((UInt8 *)a + 0x7C)) >> 26) & 1; }

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

	// ------------------------------------------------------------------ tarefa por frame (v0.25.0)
	// Corre na thread do jogo, uma vez por frame: (1) aplica as escritas adiadas (SingleRotationWriter), (2) mede o tempo de frame.
	volatile LONG	g_frameQueued = 0;
	volatile LONG	g_deferLock = 0;
	struct DeferSlot { float * a; float v; };
	DeferSlot		g_defer[16];
	int				g_deferN = 0;
	volatile float	g_frameMs = 20.0f;			// v0.25.1: tempo de frame medio (EMA)
	volatile float	g_frameLastMs = 16.0f;		// v0.26.0: ultimo frame (para saltar solucos)
	volatile float	g_railLat = 0.0f, g_railHead = 0.0f;
	volatile LONG	g_railOn = 0;				// v0.25.0: trilho da viga ativo
	volatile DWORD	g_railTick = 0;
	volatile LONG	g_guardMask = 0;			// v0.26.1: bits (0-7) das direcoes com queda a <20 (cada bit = base + k*45 graus)
	volatile float	g_guardBase = 0.0f;
	volatile DWORD	g_guardTick = 0;
	volatile LONG	g_leapOn = 0;				// v0.26.1: BeamWalkLeap (ligado pela tecla de salto ou pelo estado de ar)
	volatile DWORD	g_leapTick = 0;
	volatile LONG	g_envEdgeZone = 0;			// v0.26.1: zona de borda (publica BeamWalkOnBeam)
	volatile LONG	g_envNeedMirror = 0;		// espelho de g_envNeed so para o log do frame

	class FrameTask : public TaskDelegate
	{
	public:
		virtual void Run(void);
		virtual void Dispose(void) { delete this; }
	};

	void FrameQueue()
	{
		if(g_taskIface && InterlockedExchange(&g_frameQueued, 1) == 0) g_taskIface->AddTask(new FrameTask());
	}

	// Com SingleRotationWriter ligado a escrita fica guardada (a ultima por endereco) e e aplicada no proximo frame; senao e imediata.
	void DeferF(float * addr, float val)
	{
		if(!g_cfg.singleWriter || !g_taskIface) { *addr = val; return; }
		while(InterlockedExchange(&g_deferLock, 1)) Sleep(0);
		int i = 0;
		for(; i < g_deferN; ++i) if(g_defer[i].a == addr) break;
		if(i == g_deferN && g_deferN < 16) { g_defer[i].a = addr; ++g_deferN; }
		if(i < 16) g_defer[i].v = val;
		InterlockedExchange(&g_deferLock, 0);
		FrameQueue();
	}

	void FrameTask::Run(void)
	{
		DeferSlot loc[16]; int n = 0;
		while(InterlockedExchange(&g_deferLock, 1)) Sleep(0);
		for(int i = 0; i < g_deferN; ++i) loc[n++] = g_defer[i];
		g_deferN = 0;
		InterlockedExchange(&g_deferLock, 0);
		__try { for(int i = 0; i < n; ++i) *loc[i].a = loc[i].v; }
		__except(EXCEPTION_EXECUTE_HANDLER) { }
		{
			static LARGE_INTEGER s_last = {0}, s_freq = {0};
			static double s_sum = 0.0, s_max = 0.0; static int s_cnt = 0, s_o33 = 0, s_o50 = 0, s_lines = 0; static DWORD s_t0 = 0;
			LARGE_INTEGER t; QueryPerformanceCounter(&t);
			if(!s_freq.QuadPart) QueryPerformanceFrequency(&s_freq);
			if(s_last.QuadPart && s_freq.QuadPart)
			{
				const double ms = (double)(t.QuadPart - s_last.QuadPart) * 1000.0 / (double)s_freq.QuadPart;
				g_frameLastMs = (float)ms;
				if(ms < 200.0) g_frameMs = g_frameMs * 0.9f + (float)ms * 0.1f;
				if(ms < 1000.0) { s_sum += ms; ++s_cnt; if(ms > s_max) s_max = ms; if(ms > 33.4) ++s_o33; if(ms > 50.0) ++s_o50; }
			}
			s_last = t;
			const DWORD now = GetTickCount();
			if(!s_t0) s_t0 = now;
			if((DWORD)(now - s_t0) >= 10000)
			{
				if(g_cfg.frameLog && s_cnt > 0 && s_lines < 200)
				{
					++s_lines;
					_MESSAGE("frametime: 10 s: avg %.1f ms (%.0f fps), worst %.0f ms, frames over 33 ms: %d, over 50 ms: %d | sensors mask 0x%X, lock %d, beam rail %d",
							 s_sum / s_cnt, 1000.0 / (s_sum / s_cnt), s_max, s_o33, s_o50, (unsigned)g_envNeedMirror, (int)g_lockOwn, (int)g_railOn);
				}
				s_sum = 0.0; s_max = 0.0; s_cnt = 0; s_o33 = 0; s_o50 = 0; s_t0 = now;
			}
		}
		InterlockedExchange(&g_frameQueued, 0);
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
	UInt32			g_lockOffId = 0;						// alvo a que o filtro pertence
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
		bool hasChest = false;
		float chestPos[3] = { 0.0f, 0.0f, 0.0f };
		int n = CollectPoints(actor, pts, 256, chestPos, &hasChest);
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
		if(hasChest) { outCenter[0] = chestPos[0]; outCenter[1] = chestPos[1]; outCenter[2] = chestPos[2]; }
		outCenter[2] += g_cfg.aimAdjust;		// ajuste manual (Shift + roda do rato)
		return true;
	}

	// v0.18.2: encostas. O alvo conta como "perto da mira" se a diferenca HORIZONTAL cabe no angulo (a vertical ate ~80 graus):
	// em cima ou em baixo de uma subida o angulo total passava o limite e o lock soltava ou nao voltava a prender.
	bool YawForgive(const float * o, const float * d, const float * c, float limitRad)
	{
		const float vx = c[0] - o[0], vy = c[1] - o[1], vz = c[2] - o[2];
		const float hd = sqrtf(vx * vx + vy * vy);
		if(hd < 1.0f) return true;
		const float dyaw = fabsf(WrapPi(atan2f(vx, vy) - atan2f(d[0], d[1])));
		float dz = d[2]; if(dz > 1.0f) dz = 1.0f; if(dz < -1.0f) dz = -1.0f;
		const float dEl = fabsf(atan2f(vz, hd) - asinf(dz));
		return dyaw <= limitRad && dEl <= 1.4f;
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
		if(IsMountedCam() && IsPlayerMount(a, player)) return false;	// nunca o cavalo que se esta a montar
		bool listed = false;
		for(int j = 0; j < hc; ++j) if(hid[j] == a->formID) { listed = true; break; }
		const bool hostile = listed || CALL_MEMBER_FN(a, IsHostileToActor)((Actor *)player);
		if(g_cfg.lockHostileOnly)
		{
			if(!hostile) return false;
		}
		else if(g_cfg.lockIgnoreEssential && !hostile)
		{
			// v0.24.0: sem "so inimigos": nao prende essenciais, seguidores nem o cavalo do jogador (um inimigo continua a poder ser preso).
			if(IsEssentialActor(a) || IsTeammateActor(a) || IsPlayerMount(a, player)) return false;
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
				if(ang > acquireRad && !(g_cfg.lockVertForgive && YawForgive(o, d, c, acquireRad))) continue;
				score = ang + dist * 0.00001f;						// empate: o mais perto
			}
			else
			{
				score = dist;
				if(g_cfg.lockAutoCombatOnly)
				{
					bool fight = a->IsInCombat();
					for(int j = 0; !fight && j < hc; ++j) if(hid[j] == a->formID) fight = true;
					if(!fight) continue;
				}
				// Escolha automatica: ate ao alcance circular qualquer direcao (tambem atras); mais longe so a frente da camera.
				if(dist > g_cfg.lockAutoCircle)
				{
					const float yaw = atan2f(dx, dy), cyaw = atan2f(d[0], d[1]);
					if(fabsf(WrapPi(yaw - cyaw)) > g_cfg.lockAutoCone * kPi / 180.0f) continue;
				}
			}

			if(score < bestScore)
			{
				if(LosState(a, true) == 0) continue;							// atras de uma parede (so testa quem ganharia)
				bestScore = score; best = a; bestAng = ang;
			}
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

	// v0.18.0: o motor controla o jogador (cenas, pacotes de script, mobilia...): o mod nao escreve rotacao nem camera.
	// Mapeamento exacto do LE 1.9.32 (o mesmo que o TDM/Coeus usam): jogador+0x726 bit 0x08, ou controlador de movimento (+0xD0) com o byte +0xE9 a zero.
	bool PlayerAIDriven()
	{
		bool r = false;
		__try
		{
			PlayerCharacter * p = Engine::Player();
			if(p)
			{
				if((*(const UInt8 *)((const UInt8 *)p + 0x726)) & 0x08) r = true;
				else
				{
					const void * mc = *(void * const *)((const UInt8 *)p + 0xD0);
					if(mc && *(const UInt8 *)((const UInt8 *)mc + 0xE9) == 0) r = true;
				}
			}
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { r = false; }
		return r;
	}
	bool AIDrivenCached()
	{
		if(!g_cfg.aiGuard) return false;
		static DWORD t = 0; static bool v = false; static int logN = 0;
		const DWORD n = GetTickCount();
		if(n - t >= 100)
		{
			t = n;
			const bool nv = PlayerAIDriven();
			if(nv != v && logN < 12) { ++logN; _MESSAGE("guard: player %s by the game engine", nv ? "is now controlled" : "is back under player control"); }
			v = nv;
		}
		return v;
	}

	// v0.16.2: o mod so age com um jogo carregado (save ou jogo novo) e nunca mexe na camera/corpo com um menu aberto.
	volatile LONG	g_gameLive = 0;
	DWORD			g_perfLast = 0;

	// v0.23.0: ritmo minimo (ms) das threads rapidas por modo de desempenho (0 leve, 1 normal, 2 potente) e medicao de CPU por thread
	// v0.23.1: em combate (lock proprio/externo ou sessao de combate ativa) as threads correm sempre ao ritmo maximo (1 ms); o modo so afecta o resto do tempo
	inline bool PerfCombat() { return g_lockOwn || g_lockActive || g_active; }
	inline DWORD PerfTurnMin() { if(PerfCombat()) return 1; static const DWORD k[3] = { 4, 2, 1 }; int m = g_cfg.envPerf; return k[m < 0 ? 0 : (m > 2 ? 2 : m)]; }
	inline DWORD PerfFastMin() { if(PerfCombat()) return 1; static const DWORD k[3] = { 3, 2, 1 }; int m = g_cfg.envPerf; return k[m < 0 ? 0 : (m > 2 ? 2 : m)]; }
	enum { kThrTurn, kThrMv, kThrHold, kThrDmk, kThrEnv, kThrJump, kThrN };
	HANDLE			g_thr[kThrN];
	const char * const kThrNames[kThrN] = { "turn", "move", "hold", "dmk", "env", "jump" };
	void PerfLog()
	{
		static ULONGLONG s_prev[kThrN]; static DWORD s_last = 0; static int s_lines = 0;
		const DWORD now = GetTickCount();
		if(!s_last) { s_last = now; for(int i = 0; i < kThrN; ++i) s_prev[i] = 0; }
		if((DWORD)(now - s_last) < 10000 || s_lines >= 60) return;
		const double wall = (double)(DWORD)(now - s_last) * 10000.0;			// em unidades de 100 ns
		char buf[160]; int n = 0; double sum = 0.0;
		for(int i = 0; i < kThrN; ++i)
		{
			FILETIME c, e, k, u; ULONGLONG t = 0;
			if(g_thr[i] && GetThreadTimes(g_thr[i], &c, &e, &k, &u))
				t = (((ULONGLONG)k.dwHighDateTime << 32) | k.dwLowDateTime) + (((ULONGLONG)u.dwHighDateTime << 32) | u.dwLowDateTime);
			const double pc = s_prev[i] ? (double)(t - s_prev[i]) * 100.0 / wall : 0.0;
			s_prev[i] = t; sum += pc;
			n += _snprintf_s(buf + n, sizeof(buf) - n, _TRUNCATE, "%s %.1f%% ", kThrNames[i], pc);
		}
		s_last = now;
		if(sum > 0.05) { ++s_lines; _MESSAGE("perf: CPU of one core in 10 s: %stotal %.1f%% (mode %d)", buf, sum, g_cfg.envPerf); }
	}
	DWORD			g_camGuardUntil = 0;
	int				g_camChkN = 0;
	volatile DWORD	g_liveTick = 0;
	bool MenuBlockedCached()
	{
		static DWORD t = 0; static bool v = false;
		const DWORD n = GetTickCount();
		if(n - t >= 100) { t = n; v = MenuBlocksInput(); }
		return v;
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

	// v0.18.2: ha flechas / virotes equipados? (so conta como "nao" se o inventario foi lido e nao tem municao equipada)
	bool AmmoReady(PlayerCharacter * player)
	{
		if(!g_cfg.noAmmoGuard) return true;
		static DWORD s_t = 0; static bool s_v = true; static int s_last = -1;
		const DWORD now = GetTickCount();
		if((DWORD)(now - s_t) < 200) return s_v;
		s_t = now;
		bool res = true;
		__try
		{
			BSExtraData * x = ((TESObjectREFR *)player)->extraData.GetByType(kExtraData_ContainerChanges);
			ExtraContainerChanges * ecc = x ? (ExtraContainerChanges *)x : NULL;
			if(ecc && ecc->data && ecc->data->objList)
			{
				int entries = 0; bool worn = false;
				for(EntryDataList::Iterator it = ecc->data->objList->Begin(); !it.End() && entries < 2000; ++it)
				{
					InventoryEntryData * e = it.Get();
					++entries;
					if(!e || !e->type || e->type->formType != kFormType_Ammo || !e->extendDataList) continue;
					for(ExtendDataList::Iterator j = e->extendDataList->Begin(); !j.End(); ++j)
					{
						BaseExtraList * b = j.Get();
						if(b && b->HasType(kExtraData_Worn)) { worn = true; break; }
					}
					if(worn) break;
				}
				res = worn || entries == 0;			// lista vazia: nao se sabe, assume que ha
			}
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { res = true; }
		s_v = res;
		if(s_last != (res ? 1 : 0)) { s_last = res ? 1 : 0; _MESSAGE("ammo: %s", res ? "ammo equipped (or unknown)" : "NO ammo equipped - the bow does not count as aiming"); }
		return res;
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
	struct MvCand { UInt8 dir[4]; UInt8 sw[4]; UInt16 move; UInt16 idle; };
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

	// ---- v0.13.1: aprendizagem a pedido (Shift+F10 ou botao do menu).
	// Carrega-se W, A, S, D um de cada vez durante ~15 s; para cada posicao de memoria conta-se quantas vezes o par de floats
	// apontou para a direcao certa enquanto SO essa tecla estava premida. Sao aceites as que acertam em 3-4 direcoes.
	volatile LONG	g_mvLearnState = 0;			// 0 nada, 1 a espera da primeira tecla, 2 a aprender, 3 encontrado, 4 falhou
	DWORD			g_mvArmTick = 0, g_mvLearnStart = 0, g_mvLastVecLog = 0, g_mvLastEval = 0;
	int				g_mvLR[6], g_mvLW[6], g_mvLS[6], g_mvLN = 0;

	int MvLearnState() { return (g_mvLearnState == 1 || g_mvLearnState == 2 || g_mvLearnState == 4) ? (int)g_mvLearnState : (g_mvFound ? 3 : 0); }
	int MvFoundOffset() { return g_mvFound ? g_mvWord * 4 : -1; }

	void MvArm()
	{
		g_mvFound = 0; g_mvWant = 0; g_mvLN = 0;
		memset(g_mvCand, 0, sizeof(g_mvCand));
		g_mvMoveSamples = 0; g_mvIdleSamples = 0;
		g_mvRegion = -1; g_mvWord = -1;
		g_cfg.mvRegion = -1;
		g_mvArmTick = GetTickCount(); g_mvLearnStart = 0;
		g_mvLearnState = 1;
		_MESSAGE("input: movement learning armed - now press W, A, S, D one at a time (hold each for 1-2 s, repeat a few times, about 15 s)");
	}

	int MvScoreOf(const MvCand & c, int minHits, int * sw, int * dirs)
	{
		int dn = 0, ds = 0, sn = 0, ss = 0;
		for(int d = 0; d < 4; ++d)
		{
			if(c.dir[d] >= minHits) { ++dn; sn += c.dir[d]; }
			if(c.sw[d]  >= minHits) { ++ds; ss += c.sw[d]; }
		}
		if(dn >= ds) { *sw = 0; *dirs = dn; return dn * 100000 + sn; }
		*sw = 1; *dirs = ds; return ds * 100000 + ss;
	}

	void MvLogVec()
	{
		UInt32 w[kMvWords0];
		int n = 0;
		if(!MvReadRegion(0, w, &n)) return;
		char line[320];
		int len = 0;
		for(int i = 7; i <= 17; ++i)
			len += _snprintf_s(line + len, sizeof(line) - len, _TRUNCATE, " %X=%.2f", i * 4, W2F(w[i]));
		_MESSAGE("probe-vec: keys W%d S%d A%d D%d |%s", (int)g_kFwd, (int)g_kBack, (int)g_kLeft, (int)g_kRight, line);
	}

	bool MvEvaluate(bool final)
	{
		const int minHits = final ? 5 : 10, needDirs = final ? 3 : 4;
		int bestScore = -1;
		for(int r = 0; r < kMvRegions; ++r)
		{
			const int nw = (r == 0) ? kMvWords0 : kMvWordsH;
			for(int i = 0; i + 1 < nw; ++i)
			{
				int sw, dirs;
				const int sc = MvScoreOf(g_mvCand[r][i], minHits, &sw, &dirs);
				if(dirs >= needDirs && sc > bestScore) bestScore = sc;
			}
		}
		if(bestScore < 0) return false;
		int lr[6], lw[6], ls[6], n = 0, more = 0;
		for(int r = 0; r < kMvRegions; ++r)
		{
			const int nw = (r == 0) ? kMvWords0 : kMvWordsH;
			for(int i = 0; i + 1 < nw; ++i)
			{
				int sw, dirs;
				const int sc = MvScoreOf(g_mvCand[r][i], minHits, &sw, &dirs);
				if(dirs < needDirs || sc != bestScore) continue;
				if(n < 6) { lr[n] = r; lw[n] = i; ls[n] = sw; ++n; } else ++more;
			}
		}
		for(int k = 0; k < n; ++k) { g_mvLR[k] = lr[k]; g_mvLW[k] = lw[k]; g_mvLS[k] = ls[k]; }
		g_mvLN = n;
		g_mvRegion = lr[0]; g_mvWord = lw[0];
		g_cfg.mvRegion = lr[0]; g_cfg.mvOffset = lw[0] * 4;
		g_mvFound = 1;
		g_mvLearnState = 3;
		{
			char list[96]; int len = 0; list[0] = 0;
			for(int k = 0; k < n; ++k)
				len += _snprintf_s(list + len, sizeof(list) - len, _TRUNCATE, "%s%d:%d:%d", k ? "," : "", lr[k], lw[k] * 4, ls[k]);
			char b1[16], b2[16];
			_snprintf_s(b1, sizeof(b1), _TRUNCATE, "%d", lr[0]);
			_snprintf_s(b2, sizeof(b2), _TRUNCATE, "%d", lw[0] * 4);
			WritePrivateProfileStringA("SoftTarget", "MoveVecRegion", b1, g_iniPath);
			WritePrivateProfileStringA("SoftTarget", "MoveVecOffset", b2, g_iniPath);
			WritePrivateProfileStringA("SoftTarget", "MoveVecList", list, g_iniPath);
			_MESSAGE("input: movement vector found (%s): %d place(s) [region:offset:swapped] %s%s - saved to the ini",
					 final ? "end of window" : "early", n, list, more ? " (+ more not used)" : "");
		}
		return true;
	}

	void MvLearnFail()
	{
		g_mvLearnState = 4;
		_MESSAGE("input: learning FAILED (%d single-key samples) - the input rotation stays off. Best candidates:", g_mvMoveSamples);
		int pr[5], pi[5], n = 0;
		for(int k = 0; k < 5; ++k)
		{
			int bs = 0, br = -1, bi = -1;
			for(int r = 0; r < kMvRegions; ++r)
			{
				const int nw = (r == 0) ? kMvWords0 : kMvWordsH;
				for(int i = 0; i + 1 < nw; ++i)
				{
					bool used = false;
					for(int j = 0; j < n; ++j) if(pr[j] == r && pi[j] == i) used = true;
					if(used) continue;
					int sw, dirs;
					const int sc = MvScoreOf(g_mvCand[r][i], 1, &sw, &dirs);
					if(sc > bs) { bs = sc; br = r; bi = i; }
				}
			}
			if(br < 0) break;
			pr[n] = br; pi[n] = bi; ++n;
			const MvCand & c = g_mvCand[br][bi];
			_MESSAGE("   region %d offset 0x%X: normal W%d S%d A%d D%d | swapped W%d S%d A%d D%d", br, bi * 4,
					 (int)c.dir[0], (int)c.dir[1], (int)c.dir[2], (int)c.dir[3], (int)c.sw[0], (int)c.sw[1], (int)c.sw[2], (int)c.sw[3]);
		}
	}

	// Chamada pela MvThread (cada ~2 ms) enquanto a aprendizagem esta armada.
	void MvLearn(DWORD now)
	{
		if(g_mvLearnState != 1 && g_mvLearnState != 2) return;
		if(now - g_mvLastLearn < 2) return;
		g_mvLastLearn = now;
		const int nd = (g_kFwd ? 1 : 0) + (g_kBack ? 1 : 0) + (g_kLeft ? 1 : 0) + (g_kRight ? 1 : 0);
		if(g_mvLearnState == 1)
		{
			if(now - g_mvArmTick > 60000)
			{
				g_mvLearnState = 4;
				_MESSAGE("input: learning cancelled - no movement key was pressed within 60 s");
				return;
			}
			if(nd == 0) return;
			g_mvLearnStart = now; g_mvLastEval = now; g_mvLearnState = 2;
			_MESSAGE("input: learning started - W, A, S, D one at a time for the next 15 s");
		}
		if(now - g_mvLearnStart > 15000)
		{
			if(!MvEvaluate(true)) MvLearnFail();
			return;
		}
		if(nd >= 1 && now - g_mvLastVecLog >= 150) { g_mvLastVecLog = now; MvLogVec(); }
		if(nd != 1) return;
		int dir; float ex = 0.0f, ey = 0.0f;
		if(g_kFwd) { dir = 0; ey = 1.0f; }
		else if(g_kBack) { dir = 1; ey = -1.0f; }
		else if(g_kLeft) { dir = 2; ex = -1.0f; }
		else { dir = 3; ex = 1.0f; }
		++g_mvMoveSamples;
		for(int r = 0; r < kMvRegions; ++r)
		{
			UInt32 w[kMvWords0];
			int n = 0;
			if(!MvReadRegion(r, w, &n)) continue;
			for(int i = 0; i + 1 < n; ++i)
			{
				const float p = W2F(w[i]), q = W2F(w[i + 1]);
				if(!(p == p) || !(q == q)) continue;
				if(fabsf(p) > 1.6f || fabsf(q) > 1.6f) continue;
				const float len = sqrtf(p * p + q * q);
				if(len < 0.3f) continue;
				MvCand & c = g_mvCand[r][i];
				if((p * ex + q * ey) / len > 0.97f && c.dir[dir] < 250) ++c.dir[dir];
				if((q * ex + p * ey) / len > 0.97f && c.sw[dir]  < 250) ++c.sw[dir];
			}
		}
		if(now - g_mvLastEval >= 100) { g_mvLastEval = now; MvEvaluate(false); }
	}

	// Le a lista guardada no ini (MoveVecList) ou, em alternativa, MoveVecRegion/MoveVecOffset.
	bool MvLoadSaved()
	{
		char buf[128]; buf[0] = 0;
		if(g_iniPath[0]) GetPrivateProfileStringA("SoftTarget", "MoveVecList", "", buf, sizeof(buf), g_iniPath);
		int lr[6], lw[6], ls[6], n = 0;
		char * p = buf;
		while(*p && n < 6)
		{
			char * e;
			const long r = strtol(p, &e, 10); if(e == p || *e != ':') break; p = e + 1;
			const long o = strtol(p, &e, 10); if(e == p || *e != ':') break; p = e + 1;
			const long sw = strtol(p, &e, 10); if(e == p) break; p = e;
			while(*p == ',' || *p == ' ') ++p;
			if(r < 0 || r >= kMvRegions || o < 0 || o >= 0x200 || (o & 3)) continue;
			lr[n] = (int)r; lw[n] = (int)(o / 4); ls[n] = sw ? 1 : 0; ++n;
		}
		if(n == 0 && g_cfg.mvRegion >= 0 && g_cfg.mvRegion < kMvRegions && g_cfg.mvOffset >= 0 && g_cfg.mvOffset < 0x200)
		{ lr[0] = g_cfg.mvRegion; lw[0] = g_cfg.mvOffset / 4; ls[0] = 0; n = 1; }
		if(n == 0) return false;
		for(int k = 0; k < n; ++k) { g_mvLR[k] = lr[k]; g_mvLW[k] = lw[k]; g_mvLS[k] = ls[k]; }
		g_mvLN = n;
		g_mvRegion = lr[0]; g_mvWord = lw[0];
		g_mvFound = 1;
		_MESSAGE("input: movement vector loaded from the ini: %d place(s), first region %d offset 0x%X", n, lr[0], lw[0] * 4);
		return true;
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
			const int cnt = g_mvLN;
			for(int k = 0; k < cnt; ++k)
			{
				int n = 0;
				UInt32 * b = MvBase(g_mvLR[k], &n);
				if(!b || g_mvLW[k] + 1 >= n) continue;
				float * v = (float *)(b + g_mvLW[k]);
				const float wx = g_mvLS[k] ? iy : ix, wy = g_mvLS[k] ? ix : iy;
				if(k == 0 && g_mvHaveLast) { ++g_mvSeen; if(fabsf(v[0] - g_mvLastX) < 0.001f && fabsf(v[1] - g_mvLastY) < 0.001f) ++g_mvKept; }
				v[0] = wx; v[1] = wy;
				if(k == 0) { g_mvLastX = wx; g_mvLastY = wy; g_mvHaveLast = 1; }
			}
			++g_mvWrites;
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { g_mvFound = 0; g_mvLN = 0; g_mvRegion = -1; g_mvWord = -1; }
	}

	void MvRail(float camYaw, float bodyYaw);
	void MvGuard(float camYaw, float bodyYaw);
	DWORD WINAPI MvThread(LPVOID)
	{
		for(;;)
		{
			Sleep((g_mvLearnState || g_mvWant || g_railOn || g_guardMask) ? PerfFastMin() : 10);
			if(g_mvLearnState == 1 || g_mvLearnState == 2) { MvLearn(GetTickCount()); continue; }
			const bool railNow = g_railOn && (DWORD)(GetTickCount() - g_railTick) <= 120;
			const bool remapNow = g_mvWant && (DWORD)(GetTickCount() - g_mvWantTick) <= 120;
			const bool guardNow = g_cfg.edgeGuard && g_guardMask && (DWORD)(GetTickCount() - g_guardTick) <= 800;
			if(!g_mvFound || (!remapNow && !railNow && !guardNow)) { g_mvHaveLast = 0; continue; }
			__try
			{
				PlayerCharacter * player = Engine::Player();
				if(!player) continue;
				float o[3], d[3];
				if(!CameraRay(player, o, d)) continue;
				if(remapNow) MvApply(atan2f(d[0], d[1]), Engine::Rotation(player).z);
				if(railNow) MvRail(atan2f(d[0], d[1]), Engine::Rotation(player).z);
				if(guardNow) MvGuard(atan2f(d[0], d[1]), Engine::Rotation(player).z);
			}
			__except(EXCEPTION_EXECUTE_HANDLER) { }
		}
		return 0;
	}

	// ------------------------------------------------------------------ compatibilidade DMK (v0.15.0)
	// Publica no grafo de animacao do jogador as variaveis que os pacotes OAR feitos para o "Directional Movement Keys" leem:
	//   DirecionalCycleMoveset: 0 parado, 1 frente, 2 frente-direita, 3 direita, 4 tras-direita, 5 tras, 6 tras-esquerda,
	//                           7 esquerda, 8 frente-esquerda, 9 frente+tras, 10 esquerda+direita, 11-14 tres teclas
	//   CameraMovementCMF:      mesma ideia (1-8) para o movimento da camera (rato)
	// A direcao e a do corpo: com a rotacao do input (perfil FreeCam) usa o vetor ja rodado, senao as teclas.
	// A escrita usa a rotina interna do jogo (a mesma do SetAnimationVariableInt do Papyrus), sem Papyrus e na thread do jogo.
	// Os enderecos sao os do Skyrim LE 1.9.32; antes de os usar confere-se os bytes da rotina.
	typedef bool (__fastcall * DmkSetIntFn)(void * holder, void * edxUnused, const BSFixedString * name, SInt32 value);
	const UInt32	kDmkSetIntAddr	= 0x00650C80;
	const UInt32	kDmkInnerAddr	= 0x00650960;
	volatile LONG	g_dmkReady		= 0;		// 1 = rotina confirmada
	volatile LONG	g_dmkFaults		= 0;
	volatile LONG	g_ktState		= 0;		// 0 por fazer, 1 feito, 2 desistiu (ver o log)
	volatile LONG	g_ktGraphOk		= 0;		// o grafo do jogador ja aceitou as variaveis
	volatile LONG	g_dmkMx			= 0;		// deslocamento acumulado do rato (para CameraMovementCMF)
	volatile LONG	g_dmkMy			= 0;
	volatile LONG	g_dmkDir		= 0;		// ultimo valor calculado
	volatile LONG	g_dmkCam		= 0;
	volatile LONG	g_dmkPending	= 0;
	volatile DWORD	g_dmkQueuedAt	= 0;
	BSFixedString *	g_dmkNameDir	= NULL;
	BSFixedString *	g_dmkNameCam	= NULL;

	void DmkInit()
	{
		static const UInt8 kSig[]   = { 0x8B,0x54,0x24,0x04, 0x8D,0x44,0x24,0x08, 0x50, 0x52, 0xE8,0xD1,0xFC,0xFF,0xFF, 0xC2,0x08,0x00 };
		static const UInt8 kInner[] = { 0x83,0xEC,0x10, 0x8B,0x01, 0x8B,0x40,0x08 };
		g_dmkReady = 0;
		__try
		{
			const bool a = memcmp((const void *)kDmkSetIntAddr, kSig, sizeof(kSig)) == 0;
			const bool b = memcmp((const void *)kDmkInnerAddr, kInner, sizeof(kInner)) == 0;
			if(a && b) { g_dmkReady = 1; _MESSAGE("dmk: engine routine SetAnimationVariableInt confirmed at 0x%08X (this build is compatible)", (unsigned)kDmkSetIntAddr); }
			else _MESSAGE("dmk: engine routine NOT recognised (%d/%d) - the DMK compatibility stays OFF (is this Skyrim LE 1.9.32?)", (int)a, (int)b);
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { _MESSAGE("dmk: could not read the engine routine - the DMK compatibility stays OFF"); }
	}

	void DmkMakeNames()		// fora de qualquer __try (o new precisa de desenrolar)
	{
		if(!g_dmkNameDir) g_dmkNameDir = new BSFixedString("DirecionalCycleMoveset");
		if(!g_dmkNameCam) g_dmkNameCam = new BSFixedString("CameraMovementCMF");
	}

	// Corre na thread do jogo.
	void DmkWriteNow(int dir, int cam)
	{
		if(!g_dmkReady || g_dmkFaults >= 3) return;
		DmkMakeNames();
		__try
		{
			PlayerCharacter * player = Engine::Player();
			if(!player) return;
			void * holder = (void *)((UInt8 *)player + 0x20);
			DmkSetIntFn fn = (DmkSetIntFn)kDmkSetIntAddr;
			const bool okD = fn(holder, NULL, g_dmkNameDir, (SInt32)dir);
			const bool okC = fn(holder, NULL, g_dmkNameCam, (SInt32)cam);
			static int logged = 0;
			static DWORD lastFailLog = 0;
			if(okD && okC) g_ktGraphOk = 1;
			static int lastLoggedDir = -1;
			static int camLogged = 0;
			if(dir != lastLoggedDir && logged < 200) { ++logged; lastLoggedDir = dir; _MESSAGE("dmk: direction=%d (graph write: direction %s, camera %s)", dir, okD ? "ok" : "failed", okC ? "ok" : "failed"); }
			else if(cam != 0 && camLogged < 8) { ++camLogged; _MESSAGE("dmk: camera=%d (mouse direction, first %d samples)", cam, camLogged); }
			if((!okD || !okC) && GetTickCount() - lastFailLog > 30000)
			{ lastFailLog = GetTickCount(); _MESSAGE("dmk: the graph did not accept the variable (%s%s) - is SoftTarget_BDI.json installed and BDI loaded?", okD ? "" : "DirecionalCycleMoveset ", okC ? "" : "CameraMovementCMF"); }
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			++g_dmkFaults;
			_MESSAGE("dmk: fault #%ld while writing the variables%s", (long)g_dmkFaults, g_dmkFaults >= 3 ? " - DMK compatibility disabled" : "");
		}
	}

	class DmkTask : public TaskDelegate
	{
	public:
		DmkTask(int d, int c) : m_d(d), m_c(c) { }
		virtual void Run(void) { DmkWriteNow(m_d, m_c); g_dmkPending = 0; }
		virtual void Dispose(void) { delete this; }
	private:
		int m_d, m_c;
	};


	// ------------------------------------------------------------------ Keytrace como mascara (v0.17.0)
	// Pacotes DAR/OAR feitos para o DtryKeyUtil testam HasMagicEffect("Keytrace.esp"|0x801..0x804).
	// O Keytrace.esp do SoftTarget so tem 4 efeitos vazios (sem habilidades, sem condicoes). A funcao do motor que avalia a condicao
	// HasMagicEffect e embrulhada: para o jogador e para esses 4 efeitos responde pela direcao do movimento (g_dmkDir);
	// para tudo o resto chama a funcao original. Nada e adicionado ao jogador nem ao save.
	//   0x801 frente: 1,2,8,9,11,12,14   0x802 esquerda: 6,7,8,10,11,13,14   0x803 tras: 4,5,6,9,12,13,14   0x804 direita: 2,3,4,10,11,12,13
	typedef bool (__cdecl * KtEvalFn)(void * thisObj, void * arg1, void * arg2, double * result);
	const UInt32	kKtTableEntry	= 0x012509F0;		// entrada "HasMagicEffect" da tabela de funcoes de condicao
	const UInt32	kKtEvalSlot		= 0x01250A10;		// ponteiro da funcao de avaliacao nessa entrada
	const UInt32	kKtEvalOrig		= 0x0050AEA0;
	KtEvalFn		g_ktOrig		= NULL;
	volatile LONG	g_ktModIndex	= -1;				// indice do Keytrace.esp na ordem de carga (-1 = nao carregado)
	volatile LONG	g_ktInstalled	= 0;
	volatile LONG	g_ktLogN		= 0;
	volatile LONG	g_ktAnyLogN		= 0;

	inline bool KtMaskFor(int idx, int dir)	// idx 0..3 = frente, esquerda, tras, direita
	{
		static const unsigned short kM[4] = {
			(1<<1)|(1<<2)|(1<<8)|(1<<9)|(1<<11)|(1<<12)|(1<<14),
			(1<<6)|(1<<7)|(1<<8)|(1<<10)|(1<<11)|(1<<13)|(1<<14),
			(1<<4)|(1<<5)|(1<<6)|(1<<9)|(1<<12)|(1<<13)|(1<<14),
			(1<<2)|(1<<3)|(1<<4)|(1<<10)|(1<<11)|(1<<12)|(1<<13) };
		return dir > 0 && dir < 16 && ((kM[idx] >> dir) & 1);
	}

	bool __cdecl KtEvalWrap(void * thisObj, void * arg1, void * arg2, double * result)
	{
		if(g_cfg.dmkKeytrace && g_ktModIndex >= 0 && thisObj && arg1 && result)
		{
			UInt32 fid = 0; bool isMgef = false;
			__try
			{
				TESForm * f = (TESForm *)arg1;
				fid = f->formID; isMgef = (f->formType == 0x12);
			}
			__except(EXCEPTION_EXECUTE_HANDLER) { isMgef = false; }
			if(g_ktAnyLogN < 6) { ++g_ktAnyLogN; _MESSAGE("keytrace-mask: HasMagicEffect evaluated by the engine for form %08X (actor %p, player %p)", (unsigned)fid, thisObj, (void *)Engine::Player()); }
			if(isMgef && (LONG)(fid >> 24) == g_ktModIndex && (fid & 0x00FFFFFF) >= 0x801 && (fid & 0x00FFFFFF) <= 0x804 && thisObj == (void *)Engine::Player())
			{
				const int idx = (int)(fid & 0x00FFFFFF) - 0x801;
				const int dir = (int)g_dmkDir;
				const bool on = KtMaskFor(idx, dir);
				*result = on ? 1.0 : 0.0;
				if(g_ktLogN < 20) { ++g_ktLogN; _MESSAGE("keytrace-mask: HasMagicEffect asked for %08X (dir=%d) -> %s", (unsigned)fid, dir, on ? "true" : "false"); }
				return true;
			}
		}
		return g_ktOrig(thisObj, arg1, arg2, result);
	}

	void KtInstall()
	{
		if(g_ktInstalled) return;
		static const UInt8 kPro[] = { 0x8B,0x44,0x24,0x04, 0x56, 0x8B,0x74,0x24,0x14, 0xC7,0x06,0,0,0,0, 0xC7,0x46,0x04,0,0,0,0, 0x85,0xC0, 0x74,0x3B };
		bool ok = false;
		__try
		{
			const char * nm = *(const char **)(kKtTableEntry + 0x00);
			const UInt32 opc = *(const UInt32 *)(kKtTableEntry + 0x08);
			const UInt32 ev  = *(const UInt32 *)kKtEvalSlot;
			ok = nm && strcmp(nm, "HasMagicEffect") == 0 && opc == 0x10D6 && ev == kKtEvalOrig && memcmp((const void *)kKtEvalOrig, kPro, sizeof(kPro)) == 0;
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { ok = false; }
		if(!ok) { _MESSAGE("keytrace-mask: condition function HasMagicEffect NOT recognised - Keytrace mask stays OFF"); return; }
		g_ktOrig = (KtEvalFn)kKtEvalOrig;
		SafeWrite32(kKtEvalSlot, (UInt32)&KtEvalWrap);
		g_ktInstalled = 1;
		_MESSAGE("keytrace-mask: HasMagicEffect condition hooked");
	}

	// v0.17.1: o Themis_OAR (e provavelmente o DAR) nao passa pela funcao de condicao: chama directamente a rotina interna do jogo
	// 0x00662E80 (MagicTarget::HasMagicEffect, thiscall, 1 argumento = EffectSetting*, devolve bool em al, ret 4).
	// Poe-se um desvio de 5 bytes no inicio dessa rotina; para o jogador e para os 4 efeitos Keytrace responde pela direcao,
	// para todo o resto corre a rotina original (copiada para um trampolim).
	typedef bool (__fastcall * KtHasMeFn)(void * self, void * edxUnused, void * mgef);
	const UInt32	kKtHasMeAddr	= 0x00662E80;
	KtHasMeFn		g_ktTramp		= NULL;
	volatile LONG	g_ktHasMeLogN	= 0;
	volatile LONG	g_ktHasMeOn		= 0;

	bool __fastcall KtHasMeWrap(void * self, void * edxUnused, void * mgef)
	{
		if(g_cfg.dmkKeytrace && g_ktModIndex >= 0 && self && mgef)
		{
			PlayerCharacter * p = Engine::Player();
			if(p && self == (void *)((UInt8 *)p + 0x54))
			{
				UInt32 fid = 0; bool isMgef = false;
				__try { TESForm * f = (TESForm *)mgef; fid = f->formID; isMgef = (f->formType == 0x12); }
				__except(EXCEPTION_EXECUTE_HANDLER) { isMgef = false; }
				if(isMgef && (LONG)(fid >> 24) == g_ktModIndex && (fid & 0x00FFFFFF) >= 0x801 && (fid & 0x00FFFFFF) <= 0x804)
				{
					const int idx = (int)(fid & 0x00FFFFFF) - 0x801;
					const int dir = (int)g_dmkDir;
					const bool on = KtMaskFor(idx, dir);
					if(g_ktHasMeLogN < 20) { ++g_ktHasMeLogN; _MESSAGE("keytrace-mask: [MagicTarget] HasMagicEffect asked for %08X (dir=%d) -> %s", (unsigned)fid, dir, on ? "true" : "false"); }
					return on;
				}
			}
		}
		return g_ktTramp(self, edxUnused, mgef);
	}

	void KtInstallHasMe()
	{
		if(g_ktHasMeOn) return;
		static const UInt8 kPro[] = { 0x8B,0x01, 0x8B,0x50,0x1C, 0x53, 0x56, 0xFF,0xD2, 0x8B,0xF0 };
		bool ok = false;
		__try { ok = memcmp((const void *)kKtHasMeAddr, kPro, sizeof(kPro)) == 0; }
		__except(EXCEPTION_EXECUTE_HANDLER) { ok = false; }
		if(!ok) { _MESSAGE("keytrace-mask: engine routine MagicTarget::HasMagicEffect NOT recognised - OAR/DAR mask stays OFF"); return; }
		UInt8 * tr = (UInt8 *)VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if(!tr) { _MESSAGE("keytrace-mask: could not allocate the trampoline"); return; }
		memcpy(tr, (const void *)kKtHasMeAddr, 5);
		tr[5] = 0xE9;
		*(UInt32 *)(tr + 6) = (kKtHasMeAddr + 5) - ((UInt32)tr + 10);
		g_ktTramp = (KtHasMeFn)tr;
		UInt8 patch[5]; patch[0] = 0xE9;
		*(UInt32 *)(patch + 1) = (UInt32)&KtHasMeWrap - (kKtHasMeAddr + 5);
		DWORD oldProt = 0;
		if(!VirtualProtect((void *)kKtHasMeAddr, 5, PAGE_EXECUTE_READWRITE, &oldProt)) { _MESSAGE("keytrace-mask: VirtualProtect failed"); return; }
		memcpy((void *)kKtHasMeAddr, patch, 5);
		VirtualProtect((void *)kKtHasMeAddr, 5, oldProt, &oldProt);
		FlushInstructionCache(GetCurrentProcess(), (void *)kKtHasMeAddr, 5);
		g_ktHasMeOn = 1;
		_MESSAGE("keytrace-mask: MagicTarget::HasMagicEffect hooked (for OAR/DAR packs)");
	}

	void KtResolveMod()
	{
		DataHandler * dh = DataHandler::GetSingleton();
		const ModInfo * mi = dh ? dh->LookupModByName("Keytrace.esp") : NULL;
		if(mi && mi->modIndex != 0xFF) { g_ktModIndex = mi->modIndex; _MESSAGE("keytrace-mask: Keytrace.esp found at load index %02X", (unsigned)mi->modIndex); }
		else { g_ktModIndex = -1; _MESSAGE("keytrace-mask: Keytrace.esp is not loaded (install the SoftTarget Keytrace.esp for the mask to work)"); }
	}

	inline int DmkSectorFromVec(float x, float y)	// x = direita, y = frente
	{
		float deg = atan2f(x, y) * 180.0f / kPi;
		if(deg < 0.0f) deg += 360.0f;
		return ((int)((deg + 22.5f) / 45.0f)) % 8 + 1;
	}

	void DmkCompute(DWORD now, int * pdir, int * pcam, int * pcamVal, DWORD * pcamTick)
	{
		int dir = 0, cam = 0, camVal = *pcamVal;
		DWORD camTick = *pcamTick;
		__try
		{
			if(!MenuBlocksInput())
			{
				const bool f = g_kFwd, b = g_kBack, l = g_kLeft, r = g_kRight;
				if(f || b || l || r)
				{
					if(l && f && r) dir = 11;
					else if(b && f && r) dir = 12;
					else if(l && b && r) dir = 13;
					else if(b && f && l) dir = 14;
					else if(f && b) dir = 9;
					else if(l && r) dir = 10;
					else
					{
						float x = (r ? 1.0f : 0.0f) - (l ? 1.0f : 0.0f), y = (f ? 1.0f : 0.0f) - (b ? 1.0f : 0.0f);
						PlayerCharacter * player = Engine::Player();
						float o[3], d[3];
						if(player && g_mvFound && g_mvWant && (now - g_mvWantTick) <= 120 && CameraRay(player, o, d))
						{
							const float dd = WrapPi(atan2f(d[0], d[1]) - Engine::Rotation(player).z);
							const float cs = cosf(dd), sn = sinf(dd);
							const float nx = x * cs + y * sn, ny = -x * sn + y * cs;
							x = nx; y = ny;
						}
						dir = DmkSectorFromVec(x, y);
					}
				}
				// camera: movimento do rato (mantem-se 120 ms depois do ultimo movimento)
				const LONG dx = InterlockedExchange(&g_dmkMx, 0), dy = InterlockedExchange(&g_dmkMy, 0);
				const bool h = (dx >= 2 || dx <= -2), v = (dy >= 2 || dy <= -2);
				if(h || v)
				{
					const bool up = v && dy < 0, down = v && dy > 0, left = h && dx < 0, right = h && dx > 0;
					if(up && left) camVal = 8; else if(up && right) camVal = 2; else if(down && left) camVal = 6; else if(down && right) camVal = 4;
					else if(up) camVal = 1; else if(left) camVal = 7; else if(down) camVal = 5; else camVal = 3;
					camTick = now;
				}
				else if(now - camTick > 120) camVal = 0;
				cam = camVal;
			}
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { dir = 0; cam = 0; }
		*pdir = dir; *pcam = cam; *pcamVal = camVal; *pcamTick = camTick;
	}

	// =====================================================================================================================
	// v0.19.0: sonda de raios do ambiente (so log). Raio do Havok pelo mesmo caminho da rotina vanilla de raios (0x006BF6E0):
	// bhkWorld da celula (0x004C5430), PickObject no slot 0x2F da vtable, estrutura de pedido com origem/destino em escala Havok.
	// Corre numa tarefa da thread do jogo, 10x por segundo, fora de combate. Qualquer erro desliga-a (g_envDead).
	// =====================================================================================================================
	volatile LONG	g_envDead = 0;
	volatile LONG	g_envQueued = 0;
	volatile DWORD	g_envRetryAt = 0, g_envLastFail = 0;	// v0.26.0: os sensores religam-se sozinhos depois de um erro (nunca ficam desligados para sempre)
	volatile LONG	g_envFailN = 0, g_envGroupNow = -1, g_envAirState = 0;
	volatile float	g_envFallDist = 0.0f;
	DWORD			g_exCode = 0; void * g_exAddr = NULL;
	static int EnvFilter(DWORD code, EXCEPTION_POINTERS * ep) { g_exCode = code; g_exAddr = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : NULL; return EXCEPTION_EXECUTE_HANDLER; }
	int				g_envLogN = 0;

	// v0.20.0: globais do SoftTargetEnv.esp (formularios 0x800..0x80B). Os nomes seguem os do RaySense para os pacotes convertidos.
	enum { kEnvN = 36 };
	enum { kEnvJump = 11, kEnvWallFL = 12, kEnvWallFR = 13, kEnvTypeL = 14, kEnvTypeR = 15, kEnvBeamLeap = 16, kEnvOnBeam = 17 };
	const char * const kEnvNames[kEnvN] = {
		"Obstacle_Type_Front", "Verticality_Obstacle", "Verticality_Front", "Verticality_Player", "RaySense_SurfaceSlope", "RaySense_MacroSlope_Front",
		"RaySense_Wall_Front", "RaySense_Wall_Left", "RaySense_Wall_Right", "RaySense_LedgeHeight", "RaySense_LedgeDistance", "ST_JumpObstacle",
		"RaySense_Wall_FrontLeft", "RaySense_Wall_FrontRight", "Obstacle_Type_Left", "Obstacle_Type_Right", "BeamWalkLeap", "BeamWalkOnBeam",
		"Cover_F_to_F", "Cover_F_to_B", "Cover_F_to_L", "Cover_F_to_R", "Cover_B_to_F", "Cover_B_to_B", "Cover_B_to_L", "Cover_B_to_R",
		"Cover_L_to_F", "Cover_L_to_B", "Cover_L_to_L", "Cover_L_to_R", "Cover_R_to_F", "Cover_R_to_B", "Cover_R_to_L", "Cover_R_to_R",
		"Verticality_Cliff", "RaySense_LedgeDepth" };
	TESGlobal * 	g_envG[kEnvN];
	volatile LONG	g_envModIdx = -1;
	volatile LONG	g_envResolved = 0;
	volatile float	g_envRawH = 0.0f;		// altura bruta da ultima leitura rapida
	volatile float	g_envFwdD = -1.0f;		// distancia ao obstaculo a frente (-1 = nenhum)
	volatile float	g_envHeldType = 0.0f;
	volatile LONG	g_envNeed = 0x3F;		// grupos de sensores necessarios (tabela de demanda)
	enum { kNeedFwd = 1, kNeedGround = 2, kNeedFront = 4, kNeedWall = 8, kNeedLedge = 16, kNeedBeam = 32 };
	volatile LONG	g_envIdle = 0;			// 1 = parado e sem obstaculo: a leitura abranda
	volatile LONG	g_envOnBeam = 0;		// 1 = em cima de uma viga/passadico estreito (para BeamWalkLeap)
	volatile float	g_envObsH = 0.0f;		// ultima altura do obstaculo a frente (para o ST_JumpObstacle)

	void EnvResolveMod()
	{
		DataHandler * dh = DataHandler::GetSingleton();
		const ModInfo * mi = dh ? dh->LookupModByName("SoftTargetEnv.esp") : NULL;
		if(mi && mi->modIndex != 0xFF && mi->modIndex < 0xFE) { g_envModIdx = mi->modIndex; _MESSAGE("env: SoftTargetEnv.esp found at load index %02X", (unsigned)mi->modIndex); }
		else { g_envModIdx = -1; _MESSAGE("env: SoftTargetEnv.esp is not loaded (the environment globals are not written; install it for the RaySense-style packs)"); }
	}

	void EnvResolveForms()
	{
		if(g_envResolved || g_envModIdx < 0) return;
		static int s_tries = 0;
		int ok = 0;
		for(int i = 0; i < kEnvN; ++i)
		{
			TESForm * f = LookupFormByID(((UInt32)g_envModIdx << 24) | (UInt32)(0x800 + i));
			if(f && f->formType == kFormType_Global) { g_envG[i] = (TESGlobal *)f; ++ok; }
		}
		++s_tries;
		if(ok > 0 || s_tries >= 50) g_envResolved = 1;
		if(ok > 0 || s_tries == 1 || s_tries == 50) _MESSAGE("env: %d of %d globals found in SoftTargetEnv.esp (attempt %d)", ok, (int)kEnvN, s_tries);
	}

	inline void EnvSet(int i, float v)
	{
		TESGlobal * g = g_envG[i];
		if(g) *(volatile float *)((UInt8 *)g + 0x20) = v;
	}

	struct EnvRay { bool hit; float dist; float nx, ny, nz; UInt32 layer; const void * root; };

	typedef void * (__fastcall * _EnvCellWorld)(void * cell);
	typedef bool (__thiscall * _EnvPick)(void * world, void * pick);

	// Lanca um raio de (sx,sy,sz) na direcao unitaria (dx,dy,dz) com comprimento len (unidades do jogo).
	// Devolve false se nao foi possivel (sem mundo fisico, etc.); out->hit diz se acertou.
	bool EnvCast(PlayerCharacter * player, float sx, float sy, float sz, float dx, float dy, float dz, float len, UInt32 castLayer, EnvRay * out)
	{
		out->hit = false; out->dist = len; out->nx = out->ny = out->nz = 0.0f; out->layer = 0; out->root = NULL;
		const float scale = *(float *)0x01B11570;
		if(!(scale > 0.001f && scale < 0.1f)) return false;
		UInt8 * pl = (UInt8 *)player;
		void * cell = *(void **)(pl + 0x40);
		if(!cell) return false;
		void * world = ((_EnvCellWorld)0x004C5430)(cell);
		if(!world) return false;
		// filtro de colisao do personagem (grupo do sistema: assim o raio nao acerta no proprio jogador)
		UInt32 filter = 0;
		void * proc = *(void **)(pl + 0x88);
		void * mid = proc ? *(void **)((UInt8 *)proc + 0x04) : NULL;
		void * ctrl = mid ? *(void **)((UInt8 *)mid + 0x15C) : NULL;
		if(ctrl)
		{
			typedef UInt32 * (__thiscall * _Flt)(void *, UInt32 *);
			UInt32 tmp = 0;
			(*(_Flt **)ctrl)[8](ctrl, &tmp);
			filter = tmp;
		}
		__declspec(align(16)) UInt8 pd[0xC0];
		memset(pd, 0, sizeof(pd));
		float * from = (float *)(pd + 0x00);
		float * to = (float *)(pd + 0x10);
		from[0] = sx * scale; from[1] = sy * scale; from[2] = sz * scale; from[3] = 0.0f;
		to[0] = (sx + dx * len) * scale; to[1] = (sy + dy * len) * scale; to[2] = (sz + dz * len) * scale; to[3] = 0.0f;
		*(UInt32 *)(pd + 0x24) = (filter & ~0x7Fu) | (castLayer & 0x7Fu);
		*(float *)(pd + 0x40) = 1.0f;
		*(UInt32 *)(pd + 0x44) = 0xFFFFFFFF;
		*(UInt32 *)(pd + 0x48) = 0xFFFFFFFF;
		*(UInt32 *)(pd + 0x50) = 0xFFFFFFFF;
		memcpy(pd + 0x90, (const void *)0x01162820, 16);			// constante que a rotina vanilla copia para o pedido
		(*(_EnvPick **)world)[0x2F](world, pd);
		const float fr = *(float *)(pd + 0x40);
		if(fr < 1.0f && fr >= 0.0f)
		{
			out->hit = true; out->dist = len * fr;
			const float * n = (const float *)(pd + 0x30);
			out->nx = n[0]; out->ny = n[1]; out->nz = n[2];
			const UInt8 * root = *(const UInt8 **)(pd + 0x80);
			if(root) { out->layer = *(const UInt32 *)(root + 0x1C) & 0x7F; out->root = root; }
		}
		return true;
	}

	typedef TESObjectREFR * (__cdecl * _EnvFindRef)(const void * collidable);

	// altura do chao (relativa a z0) no ponto (x,y): raio de z0+120 para baixo; false se nao acertou
	bool EnvGroundH(PlayerCharacter * player, float x, float y, float z0, float * h, float * nz)
	{
		EnvRay r;
		if(!EnvCast(player, x, y, z0 + 120.0f, 0, 0, -1.0f, 420.0f, 3, &r) || !r.hit) return false;
		*h = 120.0f - r.dist; if(nz) *nz = r.nz;
		return true;
	}

	static void EnvRestoreJump();
	void EnvZero()
	{
		EnvRestoreJump();
		for(int i = 0; i <= 10; ++i) EnvSet(i, 0.0f);
		EnvSet(kEnvWallFL, 999.0f); EnvSet(kEnvWallFR, 999.0f); EnvSet(6, 999.0f); EnvSet(7, 999.0f); EnvSet(8, 999.0f);
		EnvSet(kEnvTypeL, 0.0f); EnvSet(kEnvTypeR, 0.0f); EnvSet(kEnvOnBeam, 0.0f);
		EnvSet(34, 0.0f); EnvSet(35, 0.0f);
		g_envObsH = 0.0f; g_envOnBeam = 0; g_envEdgeZone = 0; g_leapOn = 0; g_guardMask = 0; g_envRawH = 0.0f; g_envFwdD = -1.0f;
	}

	// classifica o que um raio acertou: 43 = personagem (camada 30), 34 = estatico; refina com o formulario real quando valido
	float EnvClassify(const EnvRay & r, bool refOk)
	{
		if(!r.hit) return 0.0f;
		float t = (float)kFormType_Static;
		if(r.layer == 30) t = (float)kFormType_NPC;
		if(refOk && r.root)
		{
			__try
			{
				TESObjectREFR * ref = ((_EnvFindRef)0x004726B0)(r.root);
				if(ref && (ref->formType == kFormType_Reference || ref->formType == kFormType_Character) && ref->baseForm)
				{
					const UInt8 bt = ref->baseForm->formType;
					if(bt == kFormType_Static || bt == kFormType_Tree || bt == kFormType_Furniture || bt == kFormType_Door || bt == kFormType_Activator || bt == kFormType_MovableStatic || bt == kFormType_NPC)
						t = (float)bt;
				}
			}
			__except(EXCEPTION_EXECUTE_HANDLER) { }
		}
		return t;
	}

	// ---- v0.22.0: tabela de demanda. Le uma vez as config.json do OAR e ve que globais do SoftTargetEnv.esp as animacoes usam; so esses grupos de sensores correm.
	static UInt32 EnvNeedOf(int idx)
	{
		switch(idx)
		{
		case 0: case 1: case 11: return kNeedFwd;
		case 2: case 5: case 34: return kNeedFront;
		case 3: case 4: return kNeedGround;
		case 6: case 7: case 8: case 12: case 13: case 14: case 15: return kNeedWall;
		case 9: case 10: case 35: return kNeedLedge;
		case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31: case 32: case 33: return kNeedWall;
		case 16: case 17: return kNeedBeam | kNeedGround;
		default: return 0;
		}
	}

	static void EnvScanFile(const char * path, UInt32 * mask, int * hits)
	{
		HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if(h == INVALID_HANDLE_VALUE) return;
		DWORD sz = GetFileSize(h, NULL);
		if(sz == INVALID_FILE_SIZE || sz > 262144) { CloseHandle(h); return; }
		char * buf = (char *)malloc(sz + 1);
		DWORD rd = 0;
		if(buf && ReadFile(h, buf, sz, &rd, NULL) && rd > 0)
		{
			buf[rd] = 0;
			const char * p = buf;
			while((p = strstr(p, "SoftTargetEnv.esp")) != NULL)
			{
				p += 17;
				const char * f = strstr(p, "formID");
				if(!f || f - p > 160) continue;
				f += 6;
				while(*f && *f != '"') ++f;			// fecha a chave
				if(*f) ++f;
				while(*f && *f != '"') ++f;			// abre o valor
				if(!*f) break;
				++f;
				const long v = strtol(f, NULL, 16);
				const int idx = (int)v - 0x800;
				if(idx >= 0 && idx < kEnvN) { *mask |= EnvNeedOf(idx); ++*hits; }
			}
		}
		if(buf) free(buf);
		CloseHandle(h);
	}

	static void EnvScanDir(const char * dir, UInt32 * mask, int * hits, int * files, int depth)
	{
		if(depth > 8 || *files > 6000) return;
		char pat[MAX_PATH];
		_snprintf_s(pat, sizeof(pat), _TRUNCATE, "%s\\*", dir);
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA(pat, &fd);
		if(h == INVALID_HANDLE_VALUE) return;
		do
		{
			if(!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
			char full[MAX_PATH];
			_snprintf_s(full, sizeof(full), _TRUNCATE, "%s\\%s", dir, fd.cFileName);
			if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) EnvScanDir(full, mask, hits, files, depth + 1);
			else if(!_stricmp(fd.cFileName, "config.json")) { ++*files; EnvScanFile(full, mask, hits); }
		} while(FindNextFileA(h, &fd));
		FindClose(h);
	}

	void EnvScanDemand()
	{
		if(!g_cfg.envDemand) { g_envNeed = 0x3F; _MESSAGE("env: demand scan off - every sensor group runs"); return; }
		char exe[MAX_PATH]; exe[0] = 0;
		GetModuleFileNameA(NULL, exe, MAX_PATH);
		char * sl = strrchr(exe, '\\');
		if(sl) *sl = 0;
		UInt32 mask = 0; int hits = 0, files = 0;
		char d[MAX_PATH];
		_snprintf_s(d, sizeof(d), _TRUNCATE, "%s\\Data\\Meshes\\Actors\\Character\\animations\\OpenAnimationReplacer", exe);
		EnvScanDir(d, &mask, &hits, &files, 0);
		_snprintf_s(d, sizeof(d), _TRUNCATE, "%s\\Data\\Meshes\\Actors\\Character\\_1stperson\\animations\\OpenAnimationReplacer", exe);
		EnvScanDir(d, &mask, &hits, &files, 0);
		if(hits == 0) { g_envNeed = 0x3F; _MESSAGE("env: demand scan found no use of SoftTargetEnv.esp in %d config files - every sensor group runs", files); return; }
		if(g_cfg.envJump) mask |= kNeedFwd;
		if(g_cfg.beamAssist) mask |= kNeedBeam;
		if(mask & kNeedBeam) mask |= kNeedGround;
		g_envNeed = (LONG)mask;
		_MESSAGE("env: demand scan: %d config files, %d uses -> forward %d ground %d front %d walls %d ledge %d beam %d",
				 files, hits, (mask & kNeedFwd) ? 1 : 0, (mask & kNeedGround) ? 1 : 0, (mask & kNeedFront) ? 1 : 0, (mask & kNeedWall) ? 1 : 0, (mask & kNeedLedge) ? 1 : 0, (mask & kNeedBeam) ? 1 : 0);
	}

	struct EnvState
	{
		float h0, nz0, slope, vertPlayer, vertFront, macro, wall[5], wtype[5], ledgeH, ledgeD, obsType, obsRaw, heldH;
		float fd[3]; UInt32 fl[3]; bool ok0; DWORD heldTick;
	};
	static EnvState g_es;

	struct EnvPos { NiPoint3 pos; float fx, fy, rx, ry; bool moving; float det; };

	// v0.23.0: bonus de altura so nos saltos de obstaculo. O jogo calcula a altura do salto como fJumpHeightMin * (factor do ator) no momento do salto
	// (rotina em 0x00781550); por isso, enquanto ha um obstaculo elegivel a frente, a definicao e elevada para (topo + folga) e volta ao valor original
	// logo que deixa de haver. Se outro mod mudar a definicao entretanto, o valor dele e respeitado.
	static float s_jbBase = 0.0f, s_jbApplied = 0.0f; static int s_jbOk = -1;
	static void EnvRestoreJump()
	{
		if(s_jbOk != 1 || s_jbApplied <= 0.0f) return;
		volatile float * v = (volatile float *)0x01B16218;
		if(*v == s_jbApplied) *v = s_jbBase;
		s_jbApplied = 0.0f;
	}
	static void EnvApplyJumpBonus(float h)
	{
		if(!g_cfg.jumpBonus) { EnvRestoreJump(); return; }
		if(s_jbOk < 0)
		{
			s_jbOk = 0;
			const char * nm = *(const char * const *)0x01B1621C;
			const float cur = *(const float *)0x01B16218;
			if(nm && !strcmp(nm, "fJumpHeightMin") && cur > 10.0f && cur < 500.0f) s_jbOk = 1;
			_MESSAGE("env: jump height setting at 0x01B16214 %s (value %.1f)", s_jbOk ? "verified" : "NOT verified - the obstacle jump bonus is off", cur);
		}
		if(s_jbOk != 1) return;
		volatile float * v = (volatile float *)0x01B16218;
		if(h >= g_cfg.jumpObstacleMin)
		{
			if(s_jbApplied <= 0.0f || *v != s_jbApplied) { s_jbBase = *v; s_jbApplied = 0.0f; }
			float mult = (h + g_cfg.jumpClearance) / s_jbBase;
			if(mult < 1.0f) mult = 1.0f;
			if(mult > g_cfg.jumpMaxMult) mult = g_cfg.jumpMaxMult;
			const float nv = s_jbBase * mult;
			*v = nv; s_jbApplied = nv;
		}
		else EnvRestoreJump();
	}

	// grupo rapido: raios a frente (joelho, cintura, cabeca) e topo do obstaculo; mantem o valor mais alto durante EnvHoldMs
	static void EnvGroupFwd(PlayerCharacter * player, const EnvPos & e, bool refOk)
	{
		EnvRay r;
		EnvRay fr[3];
		static const float kH[3] = { 20.0f, 60.0f, 110.0f };
		for(int i = 0; i < 3; ++i)
		{
			EnvCast(player, e.pos.x, e.pos.y, e.pos.z + kH[i], e.fx, e.fy, 0.0f, e.det, 3, &fr[i]);
			g_es.fd[i] = fr[i].hit ? fr[i].dist : -1.0f; g_es.fl[i] = fr[i].hit ? fr[i].layer : 0;
		}
		int nr = -1;
		if(fr[0].hit) nr = 0;
		if(fr[1].hit && (nr < 0 || fr[1].dist < fr[0].dist)) nr = 1;
		float type = 0.0f, h = 0.0f;
		g_envFwdD = nr >= 0 ? fr[nr].dist : -1.0f;
		// perto demais da superficie: fica para o FreeRun (escalada), o salto de obstaculo nao se aplica
		const bool tooClose = nr >= 0 && fr[nr].dist < g_cfg.obsMinDist;
		if(tooClose) { g_es.heldH = 0.0f; g_es.heldTick = GetTickCount(); }
		if(nr >= 0 && !tooClose)
		{
			type = EnvClassify(fr[nr], refOk);
			const float D = fr[nr].dist;
			if(fr[2].hit && fr[2].dist <= D + 8.0f) h = 200.0f;
			else
			{
				static const float kOff[4] = { 2.0f, 8.0f, 16.0f, 30.0f };
				for(int i = 0; i < 4; ++i)
				{
					if(!EnvCast(player, e.pos.x + e.fx * (D + kOff[i]), e.pos.y + e.fy * (D + kOff[i]), e.pos.z + 200.0f, 0, 0, -1.0f, 260.0f, 3, &r) || !r.hit) continue;
					const float hh = 200.0f - r.dist;
					if(hh > h) h = hh;
				}
				if(h < 0.0f) h = 0.0f;
			}
		}
		const DWORD now = GetTickCount();
		g_es.obsRaw = h; g_envRawH = h;
		if(h >= g_es.heldH || (DWORD)(now - g_es.heldTick) > (DWORD)g_cfg.envHoldMs) { g_es.heldH = h; g_es.heldTick = now; g_es.obsType = type; }
		const float pub = tooClose ? 0.0f : g_es.heldH;
		g_envObsH = pub; g_envHeldType = tooClose ? 0.0f : g_es.obsType;
		// v0.25.1: o TIPO do obstaculo a frente publica-se sempre (a Wall Leaning exige tipo estatico/porta/arvore junto a uma parede); so a altura fica a 0 quando esta perto demais
		const float typeNear = (tooClose && nr >= 0) ? EnvClassify(fr[nr], refOk) : 0.0f;
		float pubType = tooClose ? typeNear : g_es.obsType;
		if(pubType == 0.0f && g_es.wall[0] <= 60.0f && g_es.wtype[0] != 0.0f) pubType = g_es.wtype[0];
		EnvSet(0, pubType); EnvSet(1, pub);
		EnvApplyJumpBonus(pub);
	}

	// v0.24.0: geometria da viga. Em cada distancia a frente amostra o chao a 13 desvios laterais (-48..+48 passo 8): "na viga" = chao a +-25 do chao do jogador.
	// Devolve o centro lateral (positivo = direita do jogador) e a meia-largura; false se nao ha viga (ou e uma superficie larga).
	volatile float	g_dbgCliff = 0.0f, g_dbgDepth = 0.0f;
	volatile LONG	g_beamOk = 0;
	volatile DWORD	g_beamTick = 0;
	volatile float	g_beamHead = 0.0f, g_beamLat = 0.0f, g_beamHalf = 30.0f;

	// v0.25.0: TRILHO da viga. Le o vetor de movimento que o motor vai usar (o mesmo que o MvApply escreve), tira-lhe a componente
	// perpendicular ao eixo da viga e acrescenta uma pequena correcao para o centro. E idempotente (pode correr a cada ms).
	// O vetor esta no referencial do corpo (camera livre / remap) ou da camera (jogo normal).
	void MvRail(float camYaw, float bodyYaw)
	{
		if(!g_mvFound || g_mvLN < 1) return;
		static int s_logs = 0; static DWORD s_lt = 0;
		UInt8 * cs = ThirdPersonStatePtr();
		const bool camRel = (g_mvWant != 0) ? false : (cs && !*(cs + 0xB4));
		const float ref = camRel ? camYaw : bodyYaw;
		__try
		{
			int n = 0;
			UInt32 * b0 = MvBase(g_mvLR[0], &n);
			if(!b0 || g_mvLW[0] + 1 >= n) return;
			const float * v0 = (const float *)(b0 + g_mvLW[0]);
			const float ix = g_mvLS[0] ? v0[1] : v0[0], iy = g_mvLS[0] ? v0[0] : v0[1];
			const float len = sqrtf(ix * ix + iy * iy);
			if(!(len == len) || len < 0.2f || len > 1.6f) return;
			const float a = WrapPi(g_railHead - ref);
			const float sa = sinf(a), ca = cosf(a);
			const float along = ix * sa + iy * ca;
			const float lat = g_railLat;
			const float dz = 3.0f;
			float corr = fabsf(lat) <= dz ? 0.0f : (lat - (lat > 0.0f ? dz : -dz)) / 16.0f;
			if(corr > 1.0f) corr = 1.0f; if(corr < -1.0f) corr = -1.0f;
			corr *= 0.8f * (len > 1.0f ? 1.0f : len);
			float nx = along * sa + corr * ca, ny = along * ca - corr * sa;
			const float nl2 = sqrtf(nx * nx + ny * ny);
			if(nl2 > 1.0f) { nx /= nl2; ny /= nl2; }
			const float lateralIn = ix * ca - iy * sa;
			const DWORD now = GetTickCount();
			if(s_logs < 40 && fabsf(lateralIn) > 0.2f && (DWORD)(now - s_lt) >= 600) { s_lt = now; ++s_logs; _MESSAGE("beam rail: sideways input %.2f removed, pull to centre %.2f (frame %s)", lateralIn, corr, camRel ? "camera" : "body"); }
			const int cnt = g_mvLN;
			for(int k = 0; k < cnt; ++k)
			{
				int nn = 0;
				UInt32 * b = MvBase(g_mvLR[k], &nn);
				if(!b || g_mvLW[k] + 1 >= nn) continue;
				float * v = (float *)(b + g_mvLW[k]);
				v[0] = g_mvLS[k] ? ny : nx; v[1] = g_mvLS[k] ? nx : ny;
			}
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { }
	}
	// v0.26.1: EdgeGuard - junto a uma queda (<20 unidades) remove-se a parte do movimento que vai em direcao a queda (saltar solta-te)
	void MvGuard(float camYaw, float bodyYaw)
	{
		if(!g_mvFound || g_mvLN < 1) return;
		static int s_logs = 0; static DWORD s_lt = 0;
		UInt8 * cs = ThirdPersonStatePtr();
		const bool camRel = (g_mvWant != 0) ? false : (cs && !*(cs + 0xB4));
		const float ref = camRel ? camYaw : bodyYaw;
		__try
		{
			int n = 0;
			UInt32 * b0 = MvBase(g_mvLR[0], &n);
			if(!b0 || g_mvLW[0] + 1 >= n) return;
			const float * v0 = (const float *)(b0 + g_mvLW[0]);
			const float ix = g_mvLS[0] ? v0[1] : v0[0], iy = g_mvLS[0] ? v0[0] : v0[1];
			const float len = sqrtf(ix * ix + iy * iy);
			if(!(len == len) || len < 0.2f || len > 1.6f) return;
			float nx = ix, ny = iy; bool ch = false;
			const unsigned m = (unsigned)g_guardMask;
			for(int k = 0; k < 8; ++k)
			{
				if(!(m & (1u << k))) continue;
				const float a = WrapPi(g_guardBase + (float)k * 0.7853982f - ref);
				const float sa = sinf(a), ca = cosf(a);
				const float toward = nx * sa + ny * ca;
				if(toward > 0.05f) { nx -= toward * sa; ny -= toward * ca; ch = true; }
			}
			if(!ch) return;
			const DWORD now = GetTickCount();
			if(s_logs < 30 && (DWORD)(now - s_lt) >= 1000) { s_lt = now; ++s_logs; _MESSAGE("edge guard: movement towards the drop removed (mask %02X)", m); }
			for(int k = 0; k < g_mvLN; ++k)
			{
				int nn = 0;
				UInt32 * b = MvBase(g_mvLR[k], &nn);
				if(!b || g_mvLW[k] + 1 >= nn) continue;
				float * v = (float *)(b + g_mvLW[k]);
				v[0] = g_mvLS[k] ? ny : nx; v[1] = g_mvLS[k] ? nx : ny;
			}
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { }
	}
	static bool BeamCenterAt(PlayerCharacter * player, const EnvPos & e, float fwd, float h0, float * c, float * half)
	{
		const float cx = e.pos.x + e.fx * fwd, cy = e.pos.y + e.fy * fwd;
		bool on[13];
		for(int i = 0; i < 13; ++i)
		{
			const float L = (float)(i - 6) * 10.0f;
			float hh = 0.0f;
			const bool ok = EnvGroundH(player, cx + e.rx * L, cy + e.ry * L, e.pos.z, &hh, NULL);
			on[i] = ok && fabsf(hh - h0) <= 25.0f;
		}
		int ci = -1;
		for(int k = 0; k <= 6 && ci < 0; ++k) { if(on[6 + k]) ci = 6 + k; else if(on[6 - k]) ci = 6 - k; }
		if(ci < 0) return false;
		int lo = ci, hi = ci;
		while(lo > 0 && on[lo - 1]) --lo;
		while(hi < 12 && on[hi + 1]) ++hi;
		if(lo == 0 || hi == 12) return false;
		*c = (float)((lo - 6) + (hi - 6)) * 5.0f;
		*half = (float)(hi - lo) * 5.0f + 5.0f;
		return true;
	}

	// chao sob o jogador, inclinacao e (se ha animacoes de viga) deteccao da viga
	static void EnvGroupGround(PlayerCharacter * player, const EnvPos & e)
	{
		float h0 = 0.0f, nz0 = 1.0f;
		g_es.ok0 = EnvGroundH(player, e.pos.x, e.pos.y, e.pos.z, &h0, &nz0);
		if(!g_es.ok0) { h0 = 0.0f; nz0 = 1.0f; }
		g_es.h0 = h0;
		g_es.vertPlayer = g_es.ok0 ? (h0 < 0.0f ? -h0 : 0.0f) : 300.0f;
		g_es.slope = acosf(nz0 > 1.0f ? 1.0f : (nz0 < -1.0f ? -1.0f : nz0)) * 57.2958f;
		EnvSet(3, g_es.vertPlayer); EnvSet(4, g_es.slope);
		if(!(g_envNeed & kNeedBeam)) return;
		// v0.26.1: ZONA DE BORDA - em chao plano, queda >=100 a menos de 35 unidades (8 direcoes) = BeamWalkOnBeam=1 (animacoes da viga na ponta);
		// queda >=100 a menos de 20 unidades = EdgeGuard (tira o movimento em direcao a queda). Debounce 150 ms / 300 ms.
		{
			static DWORD s_ezSince = 0, s_ezLost = 0;
			const DWORD tz = GetTickCount();
			bool ez = false; unsigned gm = 0;
			float yawb = atan2f(e.fx, e.fy);
			if(g_cfg.edgeZone && g_es.ok0 && g_es.slope < 30.0f && g_es.vertPlayer < 15.0f && !g_envAirState)
			{
				unsigned zm = 0;
				for(int k = 0; k < 8; ++k)
				{
					const float ya = yawb + (float)k * 0.7853982f, sx = sinf(ya), sy = cosf(ya);
					float hp = 0.0f;
					const bool ok = EnvGroundH(player, e.pos.x + sx * 35.0f, e.pos.y + sy * 35.0f, e.pos.z, &hp, NULL);
					if(!ok || (h0 - hp) >= 100.0f)
					{
						zm |= 1u << k;
						float hq = 0.0f;
						const bool okq = EnvGroundH(player, e.pos.x + sx * 20.0f, e.pos.y + sy * 20.0f, e.pos.z, &hq, NULL);
						if(!okq || (h0 - hq) >= 100.0f) gm |= 1u << k;
					}
				}
				ez = zm != 0;
			}
			if(ez) { if(!s_ezSince) s_ezSince = tz; s_ezLost = 0; }
			else { s_ezSince = 0; if(!s_ezLost) s_ezLost = tz; }
			if(ez && (DWORD)(tz - s_ezSince) >= 150 && !g_envEdgeZone) { g_envEdgeZone = 1; _MESSAGE("env: edge zone ON (drop within 35 units)"); }
			else if(!ez && g_envEdgeZone && s_ezLost && (DWORD)(tz - s_ezLost) >= 300) { g_envEdgeZone = 0; _MESSAGE("env: edge zone OFF"); }
			if(g_envEdgeZone && gm) { g_guardBase = yawb; g_guardMask = (LONG)gm; g_guardTick = tz; }
			else g_guardMask = 0;
		}
		// viga: chao pouco inclinado e queda >= 50 a pelo menos um dos lados (a 45 do centro), mantido 0.2 s
		static DWORD s_beamSince = 0, s_beamLost = 0;
		const DWORD tn = GetTickCount();
		float hl = 0.0f, hr = 0.0f;
		// v0.26.0: UMA VIGA tem queda dos DOIS lados (a 70 do centro), e uma faixa estreita (largura ate ~130) entre elas. Queda so de um lado = borda (Edge), nao viga.
		// Se o eixo ainda nao e conhecido, testa 4 orientacoes (0, 45, 90, 135 graus em relacao ao corpo) e fica com a faixa mais estreita.
		bool dropL = false, dropR = false, strip = false; float bestHalf = 1000.0f, bestYaw = 0.0f;
		if(g_es.ok0 && g_es.slope < 30.0f && g_es.vertPlayer < 15.0f)
		{
			const float yaw0 = atan2f(e.fx, e.fy);
			const bool known = g_beamOk && (DWORD)(tn - g_beamTick) < 1000;
			const int nAng = known ? 1 : 4;
			for(int ai = 0; ai < nAng; ++ai)
			{
				const float ya = known ? g_beamHead : yaw0 + (float)ai * 0.7853982f;
				EnvPos f = e; f.fx = sinf(ya); f.fy = cosf(ya); f.rx = f.fy; f.ry = -f.fx;
				const bool okl = EnvGroundH(player, e.pos.x - f.rx * 70.0f, e.pos.y - f.ry * 70.0f, e.pos.z, &hl, NULL);
				const bool okr = EnvGroundH(player, e.pos.x + f.rx * 70.0f, e.pos.y + f.ry * 70.0f, e.pos.z, &hr, NULL);
				const bool dl = !okl || (h0 - hl) >= 50.0f, dr = !okr || (h0 - hr) >= 50.0f;
				if(!(dl && dr)) continue;
				float sc = 0.0f, sw = 0.0f;
				if(BeamCenterAt(player, f, 0.0f, h0, &sc, &sw) && sw <= 66.0f && sw < bestHalf) { bestHalf = sw; bestYaw = ya; strip = true; dropL = dl; dropR = dr; }
			}
		}
		const bool cand = strip;
		if(cand && !(g_beamOk && (DWORD)(tn - g_beamTick) < 1000)) { g_beamHead = bestYaw; g_beamTick = tn; g_beamOk = 1; g_beamLat = 0.0f; g_beamHalf = bestHalf; }
		if(cand) { if(!s_beamSince) s_beamSince = tn; s_beamLost = 0; }
		else { s_beamSince = 0; if(!s_beamLost) s_beamLost = tn; }
		if(cand && (DWORD)(tn - s_beamSince) >= 200 && !g_envOnBeam) { g_envOnBeam = 1; _MESSAGE("env: BeamWalkOnBeam ON (drop left %s / right %s)", dropL ? "yes" : "no", dropR ? "yes" : "no"); }
		else if(!cand && g_envOnBeam && s_beamLost && (DWORD)(tn - s_beamLost) >= 300) { g_envOnBeam = 0; _MESSAGE("env: BeamWalkOnBeam OFF"); }
		EnvSet(kEnvOnBeam, (g_envOnBeam || g_envEdgeZone) ? 1.0f : 0.0f);
		if(g_cfg.beamAssist && (g_envOnBeam || cand))
		{
			// v0.24.1: as sondas seguem o eixo da viga ja conhecido (e nao a frente do corpo), por isso continuam a ver a viga mesmo
			// quando o jogador esta torto em relacao a ela. Sonda para a frente e para tras; o eixo vem da linha dos centros.
			EnvPos f = e;
			const bool haveAx = g_beamOk && (DWORD)(tn - g_beamTick) < 1000;
			if(haveAx) { const float sh = sinf(g_beamHead), ch = cosf(g_beamHead); f.fx = sh; f.fy = ch; f.rx = ch; f.ry = -sh; }
			float c0 = 0.0f, w0 = 0.0f, cp = 0.0f, wp = 0.0f, cm = 0.0f, wm = 0.0f;
			if(BeamCenterAt(player, f, 0.0f, h0, &c0, &w0))
			{
				const bool okp = BeamCenterAt(player, f, 70.0f, h0, &cp, &wp);
				const bool okm = BeamCenterAt(player, f, -70.0f, h0, &cm, &wm);
				if(okp || okm)
				{
					const float lo = okm ? -70.0f : 0.0f, loC = okm ? cm : c0, hi = okp ? 70.0f : 0.0f, hiC = okp ? cp : c0;
					const float vx = f.fx * (hi - lo) + f.rx * (hiC - loC), vy = f.fy * (hi - lo) + f.ry * (hiC - loC);
					float head = atan2f(vx, vy);
					if(haveAx) head = WrapPi(g_beamHead + WrapPi(head - g_beamHead) * 0.5f);
					// lateral em relacao ao eixo: positivo = o centro da viga esta a direita de quem anda no sentido do eixo
					const float lat = c0 * (f.rx * cosf(head) - f.ry * sinf(head));
					g_beamHead = head; g_beamLat = lat; g_beamHalf = w0; g_beamTick = tn; g_beamOk = 1;
					static DWORD s_bl = 0; static int s_bn = 0;
					if(s_bn < 80 && (DWORD)(tn - s_bl) >= 700) { s_bl = tn; ++s_bn; _MESSAGE("beam: axis %.0f deg, centre %.0f to the %s of the axis, half-width %.0f (probes %s%s)", head * 57.2958f, fabsf(lat), lat >= 0.0f ? "right" : "left", w0, okm ? "back " : "", okp ? "front" : ""); }
				}
			}
		}
		if(!g_envOnBeam) g_beamOk = 0;
	}

	// terreno a frente: 80 (diferenca de elevacao) e 150 (inclinacao em ponto mais largo)
	static void EnvGroupFront(PlayerCharacter * player, const EnvPos & e)
	{
		float h80 = 0.0f, h150 = 0.0f;
		const bool ok80 = EnvGroundH(player, e.pos.x + e.fx * 80.0f, e.pos.y + e.fy * 80.0f, e.pos.z, &h80, NULL);
		const bool ok150 = EnvGroundH(player, e.pos.x + e.fx * 150.0f, e.pos.y + e.fy * 150.0f, e.pos.z, &h150, NULL);
		g_es.vertFront = ok80 ? (h80 - g_es.h0) : -300.0f;
		g_es.macro = ok150 ? atan2f(h150 - g_es.h0, 150.0f) * 57.2958f : 0.0f;
		EnvSet(2, g_es.vertFront); EnvSet(5, g_es.macro);
		// v0.24.0: Verticality_Cliff = a maior queda (ate 1500) a 50 ou 90 a frente, em relacao ao chao do jogador (RaySense Verticality "F": olhar para baixo da borda)
		float cliff = 0.0f;
		for(int i = 0; i < 2; ++i)
		{
			const float dd = i ? 90.0f : 50.0f;
			EnvRay r;
			float drop = 0.0f;
			if(EnvCast(player, e.pos.x + e.fx * dd, e.pos.y + e.fy * dd, e.pos.z + 120.0f, 0, 0, -1.0f, 1600.0f, 3, &r) && r.hit) drop = g_es.h0 - (120.0f - r.dist);
			else
			{
				// v0.25.1: falha do raio = queda so se o caminho ate la estiver livre (junto a uma parede o raio nasce dentro da geometria e falhava: falso precipicio)
				EnvRay hr;
				const bool blockedLow = EnvCast(player, e.pos.x, e.pos.y, e.pos.z + 40.0f, e.fx, e.fy, 0.0f, dd + 40.0f, 3, &hr) && hr.hit;
				const bool blockedHigh = EnvCast(player, e.pos.x, e.pos.y, e.pos.z + 110.0f, e.fx, e.fy, 0.0f, dd + 40.0f, 3, &hr) && hr.hit;
				drop = (blockedLow || blockedHigh || g_es.wall[0] < dd + 60.0f) ? 0.0f : 1500.0f;
			}
			if(drop > cliff) cliff = drop;
		}
		if(cliff > 1500.0f) cliff = 1500.0f;
		if(g_envOnBeam) cliff = 0.0f;		// v0.26.0: em cima de uma viga nao ha "borda" (Edge) - evita as animacoes pisarem-se
		EnvSet(34, cliff); g_dbgCliff = cliff;
	}

	// paredes em 5 direcoes (frente, diagonais, lados) a 80 de altura, ate 100 de distancia; so superficies quase verticais; 999 = sem parede
	static void EnvGroupWalls(PlayerCharacter * player, const EnvPos & e, bool refOk)
	{
		static const float kWA[5][2] = { {0.0f, 1.0f}, {-0.7071f, 0.7071f}, {0.7071f, 0.7071f}, {-1.0f, 0.0f}, {1.0f, 0.0f} };	// (direita, frente)
		EnvRay r;
		for(int i = 0; i < 5; ++i)
		{
			const float dx = e.rx * kWA[i][0] + e.fx * kWA[i][1], dy = e.ry * kWA[i][0] + e.fy * kWA[i][1];
			g_es.wall[i] = 999.0f; g_es.wtype[i] = 0.0f;
			if(EnvCast(player, e.pos.x, e.pos.y, e.pos.z + 80.0f, dx, dy, 0.0f, 100.0f, 3, &r) && r.hit && fabsf(r.nz) < 0.75f)
			{ g_es.wall[i] = r.dist; g_es.wtype[i] = EnvClassify(r, refOk); }
		}
		// v0.26.1: objetos finos (pilares, troncos, paredes estreitas): leque extra de +-10/20 graus, so parado e sem nada no raio central
		if(g_cfg.envThin && !e.moving && g_es.wall[0] > 100.0f)
		{
			static const float kTA[4][2] = { {-0.3420f, 0.9397f}, {0.3420f, 0.9397f}, {-0.1736f, 0.9848f}, {0.1736f, 0.9848f} };
			for(int i = 0; i < 4; ++i)
			{
				const float dx = e.rx * kTA[i][0] + e.fx * kTA[i][1], dy = e.ry * kTA[i][0] + e.fy * kTA[i][1];
				if(EnvCast(player, e.pos.x, e.pos.y, e.pos.z + 80.0f, dx, dy, 0.0f, 100.0f, 3, &r) && r.hit && fabsf(r.nz) < 0.75f && r.dist < g_es.wall[0])
				{ g_es.wall[0] = r.dist; g_es.wtype[0] = EnvClassify(r, refOk); }
			}
		}
		EnvSet(6, g_es.wall[0]); EnvSet(kEnvWallFL, g_es.wall[1]); EnvSet(kEnvWallFR, g_es.wall[2]); EnvSet(7, g_es.wall[3]); EnvSet(8, g_es.wall[4]);
		EnvSet(kEnvTypeL, g_es.wtype[3]); EnvSet(kEnvTypeR, g_es.wtype[4]);
		// v0.26.0: junto a uma parede o tipo do obstaculo a frente vem da propria parede (a Wall Leaning exige estatico/porta/arvore)
		if(g_es.wall[0] <= 60.0f && g_es.wtype[0] != 0.0f && g_envHeldType == 0.0f) EnvSet(0, g_es.wtype[0]);
	}

	// saliencia: raios para baixo a partir de 200 unidades acima dos pes, de 10 em 10 unidades para a frente (ate 75); so com obstaculo a frente
	static void EnvGroupLedge(PlayerCharacter * player, const EnvPos & e)
	{
		float lh = 0.0f, ld = 0.0f, ldep = 0.0f;
		if(g_envFwdD >= 0.0f)
		{
			EnvRay r;
			for(float d = 10.0f; d <= 75.0f; d += 10.0f)
			{
				EnvCast(player, e.pos.x + e.fx * d, e.pos.y + e.fy * d, e.pos.z + 200.0f, 0, 0, -1.0f, 260.0f, 3, &r);
				if(!r.hit) continue;
				const float h = 200.0f - r.dist;
				if(h >= 30.0f)
				{
					lh = h; ld = d;
					// v0.24.0: profundidade = quanto o topo da saliencia continua para a frente (passos de 2, ate 16): distingue grades finas de paredes/muros
					EnvRay r2;
					for(float sdep = 2.0f; sdep <= 16.0f; sdep += 2.0f)
					{
						if(!EnvCast(player, e.pos.x + e.fx * (d + sdep), e.pos.y + e.fy * (d + sdep), e.pos.z + 200.0f, 0, 0, -1.0f, 260.0f, 3, &r2) || !r2.hit) break;
						if(fabsf((200.0f - r2.dist) - h) > 8.0f) break;
						ldep = sdep;
					}
					break;
				}
			}
		}
		g_es.ledgeH = lh; g_es.ledgeD = ld;
		EnvSet(9, lh); EnvSet(10, ld); EnvSet(35, ldep); g_dbgDepth = ldep;
	}

	void EnvMeasure()
	{
		if(g_envDead || !g_cfg.envProbe || !g_gameLive) return;
		if(g_frameLastMs > 150.0f) { g_frameLastMs = 16.0f; return; }		// v0.26.0: soluco / carregamento - salta este ciclo e retoma
		PlayerCharacter * player = Engine::Player();
		if(!player) return;
		static bool s_zeroed = false, s_init = false;
		if(player->IsInCombat() && !g_cfg.envInCombat) { if(!s_zeroed) { s_zeroed = true; EnvResolveForms(); EnvZero(); g_es.heldH = 0.0f; } return; }
		s_zeroed = false;
		static bool s_checked = false, s_refOk = false;
		if(!s_checked)
		{
			s_checked = true;
			const UInt8 * fn = (const UInt8 *)0x004C5430;
			if(!(fn[0] == 0xF6 && fn[1] == 0x41 && fn[2] == 0x2C && fn[3] == 0x01))
			{
				InterlockedExchange(&g_envDead, 1);
				_MESSAGE("env: the engine routine at 0x004C5430 is not the expected one - the sensors are off");
				return;
			}
			const UInt8 * fr = (const UInt8 *)0x004726B0;
			s_refOk = (fr[0] == 0x8B && fr[1] == 0x4C && fr[2] == 0x24 && fr[3] == 0x04 && fr[4] == 0x83 && fr[5] == 0xEC && fr[6] == 0x0C);
			if(!s_refOk) _MESSAGE("env: FindCollidableRef is not the expected routine - obstacle types fall back to the collision layer");
		}
		EnvResolveForms();
		if(!s_init) { s_init = true; EnvZero(); }
		LARGE_INTEGER t0, t1, qf; QueryPerformanceCounter(&t0);
		__try
		{
			EnvPos e;
			e.pos = Engine::Position(player);
			const float yaw = Engine::Rotation(player).z;
			e.fx = sinf(yaw); e.fy = cosf(yaw); e.rx = e.fy; e.ry = -e.fx;
			e.moving = g_moveHeld != 0;
			e.det = (g_shiftHeld && g_moveHeld) ? g_cfg.envSprintDist : g_cfg.envWalkDist;
			static DWORD s_next[4]; static unsigned s_tick = 0;
			static const DWORD kInt[3][4] = { { 200, 150, 300, 300 }, { 150, 100, 200, 200 }, { 100, 60, 100, 100 } };	// chao/viga, paredes, terreno a frente, saliencia (ms)
			const DWORD now = GetTickCount();
			// v0.26.0: a leitura acompanha o tempo de frame - com frames lentos (>33 ms) os grupos leem mais devagar (ate 4x), nunca param
			const float fmx = g_frameMs;
			const DWORD mulF = fmx > 33.0f ? (DWORD)(fmx / 33.0f > 4.0f ? 4.0f : fmx / 33.0f) : 1;
			const DWORD mul = (g_envIdle ? 2 : 1) * (mulF < 1 ? 1 : mulF);
			if(g_envAirState && (g_envNeed & (kNeedGround | kNeedBeam))) s_next[0] = now;		// no ar o chao le-se a cada ciclo (altura da queda)
			const LONG need = g_envNeed;
			++s_tick;
			// rapido: so em movimento, ou de 4 em 4 ciclos parado
			g_envGroupNow = 0;
			if((need & kNeedFwd) && (e.moving || (s_tick & 3) == 0) && (mulF < 2 || (s_tick & 1) == 0)) EnvGroupFwd(player, e, s_refOk);
			// lento: no maximo um grupo por ciclo, o mais atrasado
			const DWORD * iv = kInt[g_cfg.envPerf];
			const LONG want[4] = { (LONG)(need & (kNeedGround | kNeedBeam)), (LONG)(need & kNeedWall), (LONG)(need & kNeedFront), (LONG)(need & kNeedLedge) };
			int pick = -1; DWORD worst = 0;
			for(int g = 0; g < 4; ++g)
			{
				if(!want[g]) continue;
				const DWORD late = (DWORD)(now - s_next[g]);
				if((int)(now - s_next[g]) >= 0 && (pick < 0 || late >= worst)) { pick = g; worst = late; }
			}
			if(pick >= 0)
			{
				DWORD ivp = iv[pick], mm = mul;
				if(pick == 1)
				{
					// v0.26.1: paredes - sem duplicar parado; 100 ms parado ou com parede a <=120; o alargamento por FPS fica em 2x no maximo
					mm = mulF < 1 ? 1 : (mulF > 2 ? 2 : mulF);
					const bool nearW = g_es.wall[0] <= 120.0f || g_es.wall[1] <= 120.0f || g_es.wall[2] <= 120.0f || g_es.wall[3] <= 120.0f || g_es.wall[4] <= 120.0f;
					if((!e.moving || nearW) && ivp > 100) ivp = 100;
				}
				s_next[pick] = now + ivp * mm;
				g_envGroupNow = 1 + pick;
				if(pick == 0) EnvGroupGround(player, e);
				else if(pick == 1) EnvGroupWalls(player, e, s_refOk);
				else if(pick == 2) EnvGroupFront(player, e);
				else EnvGroupLedge(player, e);
			}
			g_envGroupNow = 5;
			{
				// v0.26.0: distancia de queda real (altura maxima desde que saiu do chao - altura atual). O FreeRun/FreeClimb levantam o personagem, por isso
				// o raio ao chao sozinho nao serve: Verticality_Player = o maior dos dois.
				static float s_zPeak = 0.0f; static bool s_airPrev = false;
				const bool air = g_envAirState != 0;
				if(!air) { s_zPeak = e.pos.z; if(s_airPrev && (need & kNeedGround)) EnvGroupGround(player, e); g_envFallDist = 0.0f; }
				else
				{
					if(!s_airPrev || e.pos.z > s_zPeak) s_zPeak = e.pos.z;
					const float fd = s_zPeak - e.pos.z; g_envFallDist = fd;
					if((need & kNeedGround) && fd > g_es.vertPlayer) { g_es.vertPlayer = fd; EnvSet(3, fd); }
				}
				s_airPrev = air;
			}
			g_envIdle = (!e.moving && g_es.heldH <= 0.0f) ? 1 : 0;
			// log so quando algo mudou de forma apreciavel
			static int s_k[14]; int k[14];
			k[0] = (int)g_es.obsType; k[1] = (int)(g_es.heldH / 10.0f); k[2] = (int)(g_es.vertFront / 20.0f); k[3] = (int)(g_es.vertPlayer / 20.0f); k[4] = (int)(g_es.slope / 5.0f); k[5] = (int)(g_es.macro / 5.0f);
			k[6] = (int)(g_es.wall[0] / 20.0f); k[7] = (int)(g_es.wall[3] / 20.0f); k[8] = (int)(g_es.wall[4] / 20.0f); k[9] = (int)(g_es.ledgeH / 10.0f); k[10] = (int)(g_es.ledgeD / 10.0f);
			k[11] = (int)(g_es.wall[1] / 20.0f) * 64 + (int)(g_es.wall[2] / 20.0f); k[12] = (int)g_es.wtype[3] * 256 + (int)g_es.wtype[4]; k[13] = g_envOnBeam * 100 + (int)(g_dbgCliff / 100.0f) * 10 + (int)(g_dbgDepth / 2.0f);
			bool changed = false;
			for(int i = 0; i < 14; ++i) if(k[i] != s_k[i]) { changed = true; s_k[i] = k[i]; }
			if(changed && g_envLogN < 500)
			{
				++g_envLogN;
				_MESSAGE("env: type %.0f obstacle %.0f (raw %.0f) | front %.0f player %.0f slope %.0f macro %.0f | walls F/FL/FR/L/R %.0f/%.0f/%.0f/%.0f/%.0f (types L/R %.0f/%.0f) | ledge %.0f at %.0f | beam %d | cliff %.0f depth %.0f | knee/waist/head %.0f/%.0f/%.0f (layers %u/%u/%u)",
						 g_es.obsType, g_es.heldH, g_es.obsRaw, g_es.vertFront, g_es.vertPlayer, g_es.slope, g_es.macro, g_es.wall[0], g_es.wall[1], g_es.wall[2], g_es.wall[3], g_es.wall[4], g_es.wtype[3], g_es.wtype[4],
						 g_es.ledgeH, g_es.ledgeD, (int)g_envOnBeam, (float)g_dbgCliff, (float)g_dbgDepth, g_es.fd[0], g_es.fd[1], g_es.fd[2], (unsigned)g_es.fl[0], (unsigned)g_es.fl[1], (unsigned)g_es.fl[2]);
			}
		}
		__except(EnvFilter(GetExceptionCode(), GetExceptionInformation()))
		{
			const DWORD tf = GetTickCount();
			if((DWORD)(tf - g_envLastFail) > 120000) g_envFailN = 0;
			g_envLastFail = tf;
			const LONG n = InterlockedIncrement(&g_envFailN);
			const DWORD wait = 3000u << (n > 4 ? 4 : (n - 1));
			g_envRetryAt = tf + (wait > 60000 ? 60000 : wait);
			InterlockedExchange(&g_envDead, 1);
			_MESSAGE("env: exception 0x%08X at %p in sensor group %d (failure %d) - the sensors pause and restart by themselves in %u s", (unsigned)g_exCode, g_exAddr, (int)g_envGroupNow, (int)n, (unsigned)((wait > 60000 ? 60000 : wait) / 1000));
		}
		// medicao de tempo: media e maximo (ms) a cada 10 s, ate 40 linhas
		QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&qf);
		{
			static double s_sum = 0.0, s_max = 0.0; static int s_n = 0, s_lines = 0; static DWORD s_last = 0;
			const double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)qf.QuadPart;
			s_sum += ms; ++s_n; if(ms > s_max) s_max = ms;
			const DWORD tn = GetTickCount();
			if(!s_last) s_last = tn;
			if((DWORD)(tn - s_last) >= 10000 && s_lines < 40)
			{
				_MESSAGE("env: timing: %d cycles in 10 s, average %.3f ms, worst %.3f ms (mode %d)", s_n, s_sum / (s_n ? s_n : 1), s_max, g_cfg.envPerf);
				++s_lines; s_sum = 0.0; s_max = 0.0; s_n = 0; s_last = tn;
			}
		}
	}

	class EnvTask : public TaskDelegate
	{
	public:
		virtual void Run(void) { EnvMeasure(); g_envQueued = 0; }
		virtual void Dispose(void) { delete this; }
	};

	DWORD WINAPI EnvThread(LPVOID)
	{
		bool scanned = false;
		for(;;)
		{
			static const DWORD kTick[3] = { 50, 33, 20 };
			Sleep(kTick[g_cfg.envPerf]);
			PerfLog();
			if(!scanned && g_gameLive) { scanned = true; EnvScanDemand(); }
			if(g_gameLive) FrameQueue();
			if(g_envDead && g_envRetryAt && (LONG)(GetTickCount() - g_envRetryAt) >= 0 && g_gameLive)
			{
				g_envRetryAt = 0; InterlockedExchange(&g_envDead, 0);
				_MESSAGE("env: the sensors restarted (attempt %d)", (int)g_envFailN);
			}
			if(g_envDead || !g_cfg.envProbe || !g_gameLive || !g_taskIface) continue;
			if(InterlockedExchange(&g_envQueued, 1) == 0) g_taskIface->AddTask(new EnvTask());
		}
		return 0;
	}

	// v0.20.0: ST_JumpObstacle. Estado do controlador do personagem (0 = no chao, 1 = a saltar, 2 = no ar): ao sair do chao com obstaculo
	// alto a frente (Verticality_Obstacle >= JumpObstacleMin) fica a 1 (ou 2 se >= 200 em movimento) ate aterrar. As animacoes de queda/aterragem do pacote testam-no.
	// v0.24.0: Cover_X_to_Y (F,B,L,R). Como o CoverAnimation.dll: junto a uma parede, cada mudanca da direcao de entrada (teclas de andar) de X para Y
	// liga o global Cover_X_to_Y e desliga os outros; fica ate nova mudanca e e limpo 2 s depois de afastar-se das paredes.
	static void CoverTrack(DWORD now)
	{
		static int s_cur = -1; static DWORD s_nowall = 0; static bool s_any = false;
		if(!g_envG[18] || !(g_envNeed & kNeedWall)) return;
		bool wall = false;
		for(int i = 0; i < 5; ++i) if(g_es.wall[i] <= 100.0f) wall = true;
		if(!wall)
		{
			if(!s_nowall) s_nowall = now;
			if(s_any && (DWORD)(now - s_nowall) > 2000)
			{
				for(int i = 0; i < 16; ++i) EnvSet(18 + i, 0.0f);
				s_any = false; s_cur = -1;
			}
			return;
		}
		s_nowall = 0;
		int dir = -1;
		if(g_kFwd && !g_kBack) dir = 0; else if(g_kBack && !g_kFwd) dir = 1; else if(g_kLeft && !g_kRight) dir = 2; else if(g_kRight && !g_kLeft) dir = 3;
		if(dir < 0 || dir == s_cur) return;
		const int from = s_cur >= 0 ? s_cur : dir;
		s_cur = dir;
		for(int i = 0; i < 16; ++i) EnvSet(18 + i, i == from * 4 + dir ? 1.0f : 0.0f);
		s_any = true;
		static int s_cl = 0;
		if(s_cl < 60) { ++s_cl; static const char k[4] = { 'F', 'B', 'L', 'R' }; _MESSAGE("env: Cover_%c_to_%c = 1 (walls F/FL/FR/L/R %.0f/%.0f/%.0f/%.0f/%.0f)", k[from], k[dir], g_es.wall[0], g_es.wall[1], g_es.wall[2], g_es.wall[3], g_es.wall[4]); }
	}

	DWORD WINAPI EnvJumpThread(LPVOID)
	{
		int prev = 0; bool latched = false, leap = false; DWORD latchTick = 0;
		for(;;)
		{
			Sleep(30);
			if(!g_gameLive || g_envDead || !g_envG[kEnvJump]) continue;
			if(g_cfg.envProbe) CoverTrack(GetTickCount());
			if(!g_cfg.envJump || !g_cfg.envProbe) { if(latched) { latched = false; EnvSet(kEnvJump, 0.0f); } continue; }
			int st = -1;
			__try
			{
				PlayerCharacter * player = Engine::Player();
				if(player)
				{
					UInt8 * proc = *(UInt8 **)((UInt8 *)player + 0x88);
					UInt8 * mid = proc ? *(UInt8 **)(proc + 0x04) : NULL;
					UInt8 * ctrl = mid ? *(UInt8 **)(mid + 0x15C) : NULL;
					if(ctrl) st = *(int *)(ctrl + 0x1A0);
				}
			}
			__except(EXCEPTION_EXECUTE_HANDLER) { st = -1; }
			if(st < 0 || st > 8) { prev = 0; if(latched) { latched = false; EnvSet(kEnvJump, 0.0f); } continue; }
			g_envAirState = st;
			const DWORD now = GetTickCount();
			{
				static int s_jl = 0;
				if(st != 0 && prev == 0 && s_jl < 200)
				{
					++s_jl;
					_MESSAGE("env: jump: obstacle held %.0f (raw %.0f, in front at %.0f, type %.0f) threshold %.0f -> %s (state %d)",
							 (double)g_envObsH, (double)g_envRawH, (double)g_envFwdD, (double)g_envHeldType, (double)g_cfg.jumpObstacleMin, g_envObsH >= g_cfg.jumpObstacleMin ? "obstacle jump" : "normal jump", st);
				}
			}
			if(st != 0 && prev == 0 && !latched && g_envObsH >= g_cfg.jumpObstacleMin)
			{
				// 1 = obstaculo >= JumpObstacleMin (salto "Front Flip"); 2 = obstaculo >= 200 a correr/mover (salto "Cat Pass")
				const float lv = (g_envObsH >= 200.0f && g_moveHeld) ? 2.0f : 1.0f;
				latched = true; latchTick = now; EnvSet(kEnvJump, lv);
				_MESSAGE("env: ST_JumpObstacle = %.0f (jump with an obstacle %.0f high in front, state %d)", lv, (double)g_envObsH, st);
			}
			else if(latched && (st == 0 || (DWORD)(now - latchTick) > 8000))
			{
				latched = false; EnvSet(kEnvJump, 0.0f);
				_MESSAGE("env: ST_JumpObstacle OFF (landed)");
			}
			// BeamWalkLeap: salto iniciado em cima de uma viga; fica a 1 ate aterrar
			if(st != 0 && prev == 0 && !g_leapOn && (g_envOnBeam || g_envEdgeZone)) { g_leapOn = 1; g_leapTick = now; EnvSet(kEnvBeamLeap, 1.0f); _MESSAGE("env: BeamWalkLeap ON (air state)"); }
			else if(g_leapOn && st == 0 && (DWORD)(now - g_leapTick) > 600) { g_leapOn = 0; EnvSet(kEnvBeamLeap, 0.0f); _MESSAGE("env: BeamWalkLeap OFF"); }
			prev = st;
		}
		return 0;
	}

	// v0.23.0: a mascara Keytrace so e necessaria em combate: arma sacada e lock proprio/externo ligado
	static bool KtCombatActive()
	{
		if(!g_cfg.keytraceCombatOnly) return true;
		if(!(g_lockOwn || g_lockActive)) return false;
		bool drawn = false;
		__try
		{
			PlayerCharacter * pl = Engine::Player();
			drawn = pl && pl->actorState.IsWeaponDrawn();
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { drawn = false; }
		return drawn;
	}

	DWORD WINAPI DmkThread(LPVOID)
	{
		int lastDir = -1, lastCam = -1;
		DWORD lastWrite = 0, camTick = 0;
		int camVal = 0;
		for(;;)
		{
			const bool ktOn = g_cfg.dmkKeytrace && g_cfg.enabled && g_gameLive && KtCombatActive();
			Sleep((g_cfg.dmkCompat || ktOn) ? 8 : 50);
			const DWORD now = GetTickCount();
			int dir = 0, cam = 0;
			if(g_cfg.enabled && g_gameLive && ((g_cfg.dmkCompat && g_dmkReady && g_taskIface) || ktOn))
			{
				DmkCompute(now, &dir, &cam, &camVal, &camTick);
			}
			else if(lastDir <= 0 && lastCam <= 0) { lastDir = lastCam = 0; continue; }	// desligado e ja a zero: nada a fazer
			{
				// v0.16.1: largar a direcao so depois de 90 ms sem ela (evita 1,0,1,0 ao andar e escritas desnecessarias no grafo).
				static int s_hDir = 0; static DWORD s_hTick = 0;
				if(dir != 0) { s_hDir = dir; s_hTick = now; }
				else if(s_hDir != 0 && (now - s_hTick) < 90) dir = s_hDir;
				else s_hDir = 0;
			}
			g_dmkDir = dir; g_dmkCam = cam;
			if(!g_cfg.dmkCompat) { lastDir = dir; lastCam = cam; continue; }		// so a mascara Keytrace: nao se escreve no grafo
			const bool changed = (dir != lastDir || cam != lastCam);
			if(!changed && now - lastWrite < 500) continue;		// refresca de meio em meio segundo (o grafo reinicia ao mudar de area)
			if(g_dmkPending && now - g_dmkQueuedAt < 500) continue;
			if(!g_taskIface) continue;
			lastDir = dir; lastCam = cam; lastWrite = now;
			g_dmkPending = 1; g_dmkQueuedAt = now;
			g_taskIface->AddTask(new DmkTask(dir, cam));
		}
		return 0;
	}


	bool BowAiming(PlayerCharacter * player);
	bool SoftTargetBlocked();

	// ------------------------------------------------------------------ corpo preso ao alvo enquanto anda (perfil FreeCam / Coeus)
	// O Coeus roda o corpo para a direcao das teclas a cada frame; com a rotacao do input ativa o corpo tem de ficar no alvo.
	// Esta thread repoe o corpo no alvo logo (sem limite de velocidade) e, com camera livre, compensa o deslocamento da camera
	// para ela nao mexer.
	volatile LONG	g_fastHold = 0;
	volatile LONG	g_sprintOn = 0;			// v0.18.2: correr com o lock: o corpo segue a direcao das teclas (a HoldThread trata disso)
	volatile DWORD	g_sprintTick = 0;
	DWORD			g_relTraceUntil = 0;	// v0.18.2: diagnostico depois de soltar o arco
	float			g_ctrErrYaw = 0.0f, g_ctrErrPitch = 0.0f;	// erro (rad) entre o ponto de mira e o centro do ecra, so para o log
	volatile DWORD	g_mouseTick = 0;		// ultimo movimento do rato (para o lock suave ceder)
	volatile LONG	g_freeLookHeld = 0;		// tecla de olhar livremente premida
	volatile DWORD	g_freeLookTick = 0;		// ultima vez que esteve premida
	volatile DWORD	g_fastHoldTick = 0;
	long			g_holdTicks = 0, g_holdFixes = 0;

	DWORD WINAPI HoldThread(LPVOID)
	{
		for(;;)
		{
			Sleep(g_fastHold ? PerfFastMin() : 10);
			if(!g_fastHold || GetTickCount() - g_fastHoldTick > 120) continue;
			__try
			{
				PlayerCharacter * player = Engine::Player();
				if(!player) continue;
				NiPoint3 & rot = Engine::Rotation(player);
				// v0.18.2: correr (Shift) com o lock: o corpo vira para a direcao das teclas relativa a camera (W = em frente na vista,
				// A = esquerda, S = costas...); a rotacao do input passa a dar "em frente" e o personagem corre nessa direcao.
				// Ao largar o Shift o corpo volta ao alvo, a uma velocidade limitada.
				static bool s_wasSprint = false; static DWORD s_relUntil = 0, s_lastT = 0;
				const DWORD tn = GetTickCount();
				const bool sprintNow = g_sprintOn && (DWORD)(tn - g_sprintTick) <= 120;
				if(s_wasSprint && !sprintNow) s_relUntil = tn + 350;
				s_wasSprint = sprintNow;
				float wantYaw = g_targetYaw;
				if(sprintNow)
				{
					const float skx = (g_kRight ? 1.0f : 0.0f) - (g_kLeft ? 1.0f : 0.0f), sky = (g_kFwd ? 1.0f : 0.0f) - (g_kBack ? 1.0f : 0.0f);
					float so[3], sd[3];
					if((skx != 0.0f || sky != 0.0f) && CameraRay(player, so, sd)) wantYaw = WrapPi(atan2f(sd[0], sd[1]) + atan2f(skx, sky));
				}
				const float diff = WrapPi(wantYaw - rot.z);
				++g_holdTicks;
				float lim = 0.7f;
				if(sprintNow || (LONG)(s_relUntil - tn) > 0)
				{
					float dts = (s_lastT && tn > s_lastT) ? (tn - s_lastT) / 1000.0f : 0.002f;
					if(dts > 0.05f) dts = 0.05f;
					lim = 12.0f * dts; if(lim < 0.012f) lim = 0.012f;
				}
				s_lastT = tn;
				if(fabsf(diff) < 0.002f) continue;
				++g_holdFixes;
				float step = diff;
				if(step >  lim) step =  lim;
				if(step < -lim) step = -lim;
				if(g_cfg.cameraHoldTdm)
				{
					UInt8 * hcs = ThirdPersonStatePtr();
					if(hcs && *(hcs + 0xB4) && !BowAiming(player)) CameraHold(hcs, step);
				}
				rot.z = WrapPi(rot.z + step);
			}
			__except(EXCEPTION_EXECUTE_HANDLER) { }
		}
		return 0;
	}

	// Arco / besta a ser puxado (so ai o Coeus compensa a camera por si; com magia a compensacao e nossa).
	bool BowAiming(PlayerCharacter * player)
	{
		return (g_holdLeft || g_holdRight) && BowEquipped(player) && AmmoReady(player);
	}

	// v0.18.2: camera de 3a pessoa com o lock. Limita o olhar para cima (camera ao chao / a entrar na 1a pessoa por colisao)
	// e regista 3 s de diagnostico depois de ligar o lock (estado da camera, distancia, inclinacao).
	// v0.25.0: com o lock e um arco a flecha sai mais reta (valores do mod antigo: arco 0.5/0.0, o jogo usa maiores), reposto ao largar.
	// Procura no processo o objeto "Setting" de uma definicao INI pelo nome (vtable, valor, ponteiro do nome) e devolve o endereco do valor.
	static float * FindIniSettingValue(const char * name)
	{
		const size_t len = strlen(name) + 1;
		UInt32 strAt[4]; int ns = 0;
		const UInt32 lo = 0x00400000, hi = 0x02400000;
		__try
		{
			MEMORY_BASIC_INFORMATION mi;
			for(UInt32 a = lo; a < hi && ns < 4; )
			{
				if(!VirtualQuery((void *)a, &mi, sizeof(mi))) break;
				const UInt32 b = (UInt32)mi.BaseAddress, e = b + (UInt32)mi.RegionSize;
				if(mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && e - b >= len)
				{
					for(UInt32 q = b; q + len <= e && ns < 4; ++q)
						if(*(const char *)q == name[0] && memcmp((const void *)q, name, len) == 0) strAt[ns++] = q;
				}
				a = e > a ? e : a + 0x1000;
			}
			for(int k = 0; k < ns; ++k)
			{
				for(UInt32 a = lo; a < hi; )
				{
					if(!VirtualQuery((void *)a, &mi, sizeof(mi))) break;
					const UInt32 b = (UInt32)mi.BaseAddress, e = b + (UInt32)mi.RegionSize;
					if(mi.State == MEM_COMMIT && (mi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) && !(mi.Protect & PAGE_GUARD))
					{
						for(UInt32 q = (b + 3) & ~3u; q + 4 <= e; q += 4)
						{
							if(*(const UInt32 *)q == strAt[k] && q >= b + 8)
							{
								const UInt32 vt = *(const UInt32 *)(q - 8);
								if(vt >= lo && vt < hi) return (float *)(q - 4);
							}
						}
					}
					a = e > a ? e : a + 0x1000;
				}
			}
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { }
		return NULL;
	}

	void ArrowTiltTick(PlayerCharacter * player)
	{
		static bool s_on = false, s_fail = false; static float s_o1 = 0.0f, s_o3 = 0.0f; static float * s1 = NULL, * s3 = NULL;
		if(s_fail) return;
		const bool want = g_cfg.lockArrowTilt && g_lockOwn && BowEquipped(player);
		if(want == s_on) return;
		__try
		{
			if(want)
			{
				if(!s1) s1 = FindIniSettingValue("f1PArrowTiltUpAngle:Combat"); if(!s3) s3 = FindIniSettingValue("f3PArrowTiltUpAngle:Combat");
				if(!s1 || !s3) { s_fail = true; _MESSAGE("arrowtilt: the game settings were not found - the option stays off"); return; }
				s_o1 = *s1; s_o3 = *s3;
				*s1 = 0.5f; *s3 = 0.0f;
				s_on = true;
				_MESSAGE("arrowtilt: ON (1st person %.2f -> 0.50, 3rd person %.2f -> 0.00)", s_o1, s_o3);
			}
			else
			{
				if(s1) *s1 = s_o1;
				if(s3) *s3 = s_o3;
				s_on = false;
				_MESSAGE("arrowtilt: restored (%.2f / %.2f)", s_o1, s_o3);
			}
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { s_fail = true; s_on = false; }
	}

	void LockCamGuard(PlayerCharacter * player, DWORD now, bool apply)
	{
		ArrowTiltTick(player);
		static bool s_prev = false; static DWORD s_t0 = 0, s_last = 0; static int s_logs = 0; static int s_clampLogs = 0;
		const bool on = g_lockOwn != 0;
		if(on && !s_prev) { s_t0 = now; s_last = 0; }
		s_prev = on;
		if(!on) return;
		__try
		{
			UInt8 * cs = ThirdPersonStatePtr();
			if(cs && apply && g_cfg.lockCamLimit)
			{
				const float up = g_cfg.lockCamUpDeg * kPi / 180.0f;
				float cp = *(float *)(cs + 0x54);
				if(cp < -up)
				{
					if(s_clampLogs < 20) { ++s_clampLogs; _MESSAGE("lockcam: camera pitch %.1f deg looks too far up - limited to %.0f deg", cp * 57.2958f, -g_cfg.lockCamUpDeg); }
					*(float *)(cs + 0x54) = -up; *(float *)(cs + 0x58) = -up;
				}
			}
			{
				// v0.25.0: com o arco puxado regista os campos da camera de 3a pessoa (0x3C..0x6C, 0xAC, 0xB0) e a distancia real:
				// mostra qual campo se mexe quando a camera "se afasta" ao puxar o arco.
				static DWORD s_zl = 0; static int s_zn = 0;
				if(cs && s_zn < 150 && BowAiming(player) && (DWORD)(now - s_zl) >= 150)
				{
					s_zl = now; ++s_zn;
					PlayerCamera * zc = PlayerCamera::GetSingleton();
					float zd = -1.0f;
					if(zc && zc->cameraNode)
					{
						const NiPoint3 pp = Engine::Position(player); const NiPoint3 cp3 = zc->cameraNode->m_worldTransform.pos;
						zd = sqrtf((pp.x - cp3.x) * (pp.x - cp3.x) + (pp.y - cp3.y) * (pp.y - cp3.y) + (pp.z - cp3.z) * (pp.z - cp3.z));
					}
					_MESSAGE("zoomdump: camDist %.1f | 3C %.2f 40 %.2f 44 %.2f 48 %.2f 4C %.2f 50 %.2f 54 %.3f 58 %.3f 5C %.3f 60 %.3f 64 %.2f 68 %.2f 6C %.2f | AC %.3f B0 %.3f B4 %d",
							 zd, *(float *)(cs + 0x3C), *(float *)(cs + 0x40), *(float *)(cs + 0x44), *(float *)(cs + 0x48), *(float *)(cs + 0x4C), *(float *)(cs + 0x50),
							 *(float *)(cs + 0x54), *(float *)(cs + 0x58), *(float *)(cs + 0x5C), *(float *)(cs + 0x60), *(float *)(cs + 0x64), *(float *)(cs + 0x68), *(float *)(cs + 0x6C),
							 *(float *)(cs + 0xAC), *(float *)(cs + 0xB0), (int)*(cs + 0xB4));
				}
			}
			if((DWORD)(now - s_t0) < 3000 && (DWORD)(now - s_last) >= 100 && s_logs < 300)
			{
				s_last = now; ++s_logs;
				PlayerCamera * cam = PlayerCamera::GetSingleton();
				float camDist = -1.0f;
				if(cam && cam->cameraNode)
				{
					const NiPoint3 pp = Engine::Position(player);
					const NiPoint3 cp2 = cam->cameraNode->m_worldTransform.pos;
					camDist = sqrtf((pp.x - cp2.x) * (pp.x - cp2.x) + (pp.y - cp2.y) * (pp.y - cp2.y) + (pp.z - cp2.z) * (pp.z - cp2.z));
				}
				_MESSAGE("lockcam: +%lu ms state=%u camDist=%.0f | camera pitch %.1f deg offset %.2f | body pitch %.1f deg",
						 (unsigned long)(now - s_t0), (cam && cam->cameraState) ? (unsigned)cam->cameraState->stateId : 0xFFFFu, camDist,
						 cs ? *(float *)(cs + 0x54) * 57.2958f : 0.0f, cs ? *(float *)(cs + 0xAC) : 0.0f, Engine::Rotation(player).x * 57.2958f);
			}
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { }
	}

	// Ajuste manual da altura do ponto de mira (unidades); fica gravado no ini.
	void AimAdjust(float delta)
	{
		g_cfg.aimAdjust += delta;
		if(g_cfg.aimAdjust < -60.0f) g_cfg.aimAdjust = -60.0f;
		if(g_cfg.aimAdjust >  60.0f) g_cfg.aimAdjust =  60.0f;
		char buf[32];
		_snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.1f", g_cfg.aimAdjust);
		WritePrivateProfileStringA("SoftTarget", "AimHeightAdjust", buf, g_iniPath);
		_MESSAGE("aim point height adjust: %.1f", g_cfg.aimAdjust);
	}

	// ------------------------------------------------------------------ camera ao ombro com o arco
	// A camera de terceira pessoa tem o deslocamento "por cima do ombro" (x lateral, y a frente, z altura) em 0x3C..0x44 e o valor
	// de destino em 0x48..0x50 (confirmado nos logs: os dois grupos sao iguais). Enquanto se puxa o arco levamo-lo para o ombro
	// escolhido, com transicao suave, e ao largar repomos os valores de antes.
	bool			g_shOn = false;
	bool			g_shTgt = false;			// o grupo 0x48 e uma copia do 0x3C (so entao o escrevemos)
	float			g_shOrig[3] = { 0.0f, 0.0f, 0.0f };
	float			g_shCur[3] = { 0.0f, 0.0f, 0.0f };
	DWORD			g_shHold = 0;

	void ShoulderTick(PlayerCharacter * player, float dt, DWORD now)
	{
		UInt8 * cs = ThirdPersonStatePtr();
		if(!cs) { g_shOn = false; return; }
		const bool want = g_cfg.bowShoulder && !IsFirstPerson() && BowAiming(player);
		if(want) g_shHold = now + 250;
		const bool aiming = want || (g_shOn && (LONG)(g_shHold - now) > 0);
		if(!aiming && !g_shOn) return;

		float * cur = (float *)(cs + 0x3C);
		float * tgt = (float *)(cs + 0x48);
		const float a = 1.0f - expf(-dt * 14.0f);

		if(aiming)
		{
			if(!g_shOn)
			{
				g_shOn = true;
				for(int k = 0; k < 3; ++k) { g_shOrig[k] = cur[k]; g_shCur[k] = cur[k]; }
				g_shTgt = fabsf(tgt[0] - cur[0]) < 0.01f && fabsf(tgt[1] - cur[1]) < 0.01f && fabsf(tgt[2] - cur[2]) < 0.01f;
				_MESSAGE("bow camera: shoulder view ON (%s shoulder), normal offset (%.1f, %.1f, %.1f), target copy=%d, free-rotation=%d",
						 g_cfg.bowRight ? "right" : "left", cur[0], cur[1], cur[2], g_shTgt ? 1 : 0, (int)*(cs + 0xB4));
			}
			const float w[3] = { (g_cfg.bowRight ? 1.0f : -1.0f) * g_cfg.bowShX, g_shOrig[1] + g_cfg.bowShY, g_shOrig[2] + g_cfg.bowShZ };
			for(int k = 0; k < 3; ++k)
			{
				g_shCur[k] += (w[k] - g_shCur[k]) * a;
				cur[k] = g_shCur[k];
				if(g_shTgt) tgt[k] = g_shCur[k];
			}
		}
		else
		{
			float err = 0.0f;
			for(int k = 0; k < 3; ++k)
			{
				g_shCur[k] += (g_shOrig[k] - g_shCur[k]) * a;
				cur[k] = g_shCur[k];
				if(g_shTgt) tgt[k] = g_shCur[k];
				err += fabsf(g_shOrig[k] - g_shCur[k]);
			}
			if(err < 0.3f)
			{
				for(int k = 0; k < 3; ++k) { cur[k] = g_shOrig[k]; if(g_shTgt) tgt[k] = g_shOrig[k]; }
				g_shOn = false;
				_MESSAGE("bow camera: shoulder view OFF");
			}
		}
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
			if(!f)
			{
				// v0.24.0: a lista da celula nao tem alvos de celulas vizinhas (campo aberto): procura-se pelo formID.
				TESForm * fm = LookupFormByID((UInt32)g_lockId);
				Actor * fa = fm ? DYNAMIC_CAST(fm, TESForm, Actor) : NULL;
				if(fa && fa->GetNiNode() && Engine::ParentCell(fa)) f = fa;
			}
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
		{
			static UInt32 s_lastLife = 0xFFFF; static int s_lifeLogN = 0;
			if(life != s_lastLife) { s_lastLife = life; if(s_lifeLogN < 30) { ++s_lifeLogN; _MESSAGE("lock: target %08X life state is now %u", (unsigned)a->formID, (unsigned)life); } }
		}
		// v0.18.1: em bleedout / caido (estados 7 e 8) o alvo mantem-se: ainda esta vivo e pode levantar-se.
		const bool downedKeep = g_cfg.keepBleedout && (life == 7 || life == 8);
		if(life != 0 && !(g_cfg.allowReanimated && life == 4) && !downedKeep) { LockLoseOrNext(player, "target died"); return; }

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
				bool evOk = false;
				if(EvalBody(a, o, d, &ang, c))
				{
					evOk = true;
					// Filtro: braços e arma do alvo mexem a cada golpe; a camera nao deve ir atras disso.
					const float nx = c[0] - pos.x, ny = c[1] - pos.y, nz = c[2] - pos.z;
					if(g_lockOffId != a->formID || g_cfg.aimFilterMs < 1.0f)
					{
						g_lockOff[0] = nx; g_lockOff[1] = ny; g_lockOff[2] = nz;
						g_lockOffId = a->formID;
					}
					else
					{
						const float al = 1.0f - expf(-60.0f / g_cfg.aimFilterMs);
						g_lockOff[0] += (nx - g_lockOff[0]) * al;
						g_lockOff[1] += (ny - g_lockOff[1]) * al;
						g_lockOff[2] += (nz - g_lockOff[2]) * al;
					}
				}
				const float loseRad = g_cfg.lockLoseAngleDeg * kPi / 180.0f;
				const bool visible = (ang <= loseRad) || (evOk && g_cfg.lockVertForgive && YawForgive(o, d, c, loseRad));
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
		else if((LONG)(now - g_lockPrevTick) >= 50)
		{
			const float s = (now - g_lockPrevTick) / 1000.0f;
			float nv[3] = { (pos.x - g_lockPrev[0]) / s, (pos.y - g_lockPrev[1]) / s, (pos.z - g_lockPrev[2]) / s };
			if(sqrtf(nv[0] * nv[0] + nv[1] * nv[1] + nv[2] * nv[2]) > 4000.0f) nv[0] = nv[1] = nv[2] = 0.0f;	// salto/teleporte
			for(int k = 0; k < 3; ++k) g_lockVel[k] = g_lockVel[k] * 0.4f + nv[k] * 0.6f;		// v0.16.7: leitura a cada 50 ms (antes 100), menos atraso
			g_lockPrev[0] = pos.x; g_lockPrev[1] = pos.y; g_lockPrev[2] = pos.z;
			g_lockPrevTick = now;
		}

		// 4) ponto de mira e previsao do disparo
		float ax = pos.x + g_lockOff[0], ay = pos.y + g_lockOff[1], az = pos.z + g_lockOff[2];
		if(g_cfg.lockLatencyComp)
		{
			// v0.25.1: a camera e lida com cerca de um frame de atraso; adianta-se o ponto pela velocidade do alvo x 1,5 frames (max 120 ms)
			float lat = g_frameMs * 1.5f * 0.001f; if(lat > 0.12f) lat = 0.12f; if(lat < 0.0f) lat = 0.0f;
			ax += g_lockVel[0] * lat; ay += g_lockVel[1] * lat; az += g_lockVel[2] * lat * 0.5f;
		}

		if(RangedHeld(player)) g_assistUntil = now + 350;		// o disparo sai ao SOLTAR: mantem a ajuda um instante
		// v0.16.7: com o arco equipado a previsao ja esta ativa antes de puxar; o ponto de mira nao salta ao comecar a puxar.
		const bool bowAlways = g_cfg.predictAlways && BowEquipped(player);
		const bool assist = ((LONG)(g_assistUntil - now) > 0) || bowAlways;
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
				static DWORD lastPredLog = 0;
				if(g_cfg.verbose && now - lastPredLog > 400)
				{
					lastPredLog = now;
					_MESSAGE("predict: %s dist %.0f speed %.0f flight %.2fs | target speed %.0f u/s | lead (%.0f, %.0f, %.0f) drop +%.0f",
							 bow ? "bow" : "spell", dist, speed, t, sqrtf(g_lockVel[0] * g_lockVel[0] + g_lockVel[1] * g_lockVel[1] + g_lockVel[2] * g_lockVel[2]),
							 px - (pos.x + g_lockOff[0]), py - (pos.y + g_lockOff[1]), pz - (pos.z + g_lockOff[2]),
							 bow ? 0.5f * g_cfg.projectileGravity * t * t : 0.0f);
				}
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

	// v0.18.0: a NiCamera do jogo reconhece-se pela tabela virtual (0x01117CFC), sem depender do RTTI do SKSE (que falhava).
	const UInt32 kNiCameraVtbl = 0x01117CFC;
	NiAVObject * FindNiCameraIn(NiAVObject * o, int depth)
	{
		if(!o) return NULL;
		if(*(const UInt32 *)o == kNiCameraVtbl) return o;
		if(depth >= 3) return NULL;
		NiNode * nd = o->GetAsNiNode();
		if(!nd) return NULL;
		const NiTArray<NiAVObject *> & ch = nd->m_children;
		for(UInt16 i = 0; i < ch.m_emptyRunStart && i < 16; ++i)
		{
			NiAVObject * f = FindNiCameraIn(ch.m_data[i], depth + 1);
			if(f) return f;
		}
		return NULL;
	}

	void DotCameraProbeOnce(PlayerCamera * cam)
	{
		static bool done = false;
		if(done) return;
		done = true;
		__try
		{
			NiNode * n = cam ? cam->cameraNode : NULL;
			if(!n) { _MESSAGE("dot-probe: cameraNode is NULL"); return; }
			const NiTArray<NiAVObject *> & ch = n->m_children;
			_MESSAGE("dot-probe: cameraNode=%p vtbl=%08X children=%d", (void *)n, (unsigned)*(const UInt32 *)n, (int)ch.m_emptyRunStart);
			for(UInt16 i = 0; i < ch.m_emptyRunStart && i < 8; ++i)
			{
				NiAVObject * c = ch.m_data[i];
				if(!c) { _MESSAGE("dot-probe: child %d = NULL", (int)i); continue; }
				const float * f = (const float *)((const UInt8 *)c + 0xE8);
				_MESSAGE("dot-probe: child %d = %p vtbl=%08X frustum(L,R,T,B,N)=(%.3f,%.3f,%.3f,%.3f,%.3f)", (int)i, (void *)c, (unsigned)*(const UInt32 *)c, f[0], f[1], f[2], f[3], f[4]);
			}
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { _MESSAGE("dot-probe: fault while probing the camera node"); }
	}

	// Projeccao exacta com a rotina do proprio jogo (0x00AB84C0): matriz mundo->camara (+0xA8) e viewport (+0x10C) da NiCamera.
	// Devolve coordenadas normalizadas (-1..1), x para a direita e y para cima.
	typedef bool (__cdecl * NiWorldToScreenFn)(const float * worldToCam, const float * port, const float * in, float * xOut, float * yOut, float * zOut, float tol);
	const UInt32 kNiWorldToScreenAddr = 0x00AB84C0;
	bool ExactNdc(float wx, float wy, float wz, float * ndcX, float * ndcY)
	{
		bool ok = false;
		__try
		{
			PlayerCamera * cam = PlayerCamera::GetSingleton();
			if(cam && cam->cameraNode)
			{
				DotCameraProbeOnce(cam);
				NiAVObject * nc = FindNiCameraIn((NiAVObject *)cam->cameraNode, 0);
				if(nc)
				{
					const float * m = (const float *)((const UInt8 *)nc + 0xA8);
					const float * p = (const float *)((const UInt8 *)nc + 0x10C);		// left, right, top, bottom
					bool sane = (p[1] > p[0] && p[2] > p[3]);
					for(int i = 0; i < 16 && sane; ++i) if(!_finite(m[i])) sane = false;
					if(sane)
					{
						const float in[3] = { wx, wy, wz };
						float x = 0.0f, y = 0.0f, z = 0.0f;
						NiWorldToScreenFn fn = (NiWorldToScreenFn)kNiWorldToScreenAddr;
						if(fn(m, p, in, &x, &y, &z, 1e-5f) && _finite(x) && _finite(y))
						{
							*ndcX = (x - 0.5f * (p[0] + p[1])) / (0.5f * (p[1] - p[0]));
							*ndcY = (y - 0.5f * (p[2] + p[3])) / (0.5f * (p[2] - p[3]));
							ok = true;
						}
					}
				}
			}
		}
		__except(EXCEPTION_EXECUTE_HANDLER) { ok = false; }
		return ok;
	}

	// Tangentes do meio-campo de visao (horizontal e vertical), do frustum da NiCamera; reserva: worldFOV.
	bool CameraTan(float * tanH, float * tanV)
	{
		PlayerCamera * cam = PlayerCamera::GetSingleton();
		if(!cam || !cam->cameraNode) return false;
		NiNode * n = cam->cameraNode;
		{
			NiAVObject * vc = FindNiCameraIn((NiAVObject *)n, 0);
			float h = 0.0f, v = 0.0f;
			if(vc && FrustumRaw(vc, &h, &v))
			{
				*tanH = h; *tanV = v;
				if(!g_dot.loggedFrustum) { g_dot.loggedFrustum = true; _MESSAGE("dot: NiCamera found by vtable, frustum tanH %.3f tanV %.3f (h-fov %.1f deg, v-fov %.1f deg)", h, v, 2.0f * atanf(h) * 180.0f / kPi, 2.0f * atanf(v) * 180.0f / kPi); }
				return true;
			}
		}
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
		if(g_cfg.dotExact)
		{
			float ex = 0.0f, ey = 0.0f;
			if(ExactNdc(wx, wy, wz, &ex, &ey))
			{
				// Confere o sentido dos eixos contra a estimativa (so quando o ponto esta longe do centro) e corrige se vier invertido.
				static int sAgreeX = 0, sDisX = 0, sAgreeY = 0, sDisY = 0;
				static bool sFlipX = false, sFlipY = false;
				if(fabsf(*ndcX) > 0.25f && fabsf(ex) > 0.25f) { if((*ndcX > 0.0f) == (ex > 0.0f)) ++sAgreeX; else ++sDisX; }
				if(fabsf(*ndcY) > 0.25f && fabsf(ey) > 0.25f) { if((*ndcY > 0.0f) == (ey > 0.0f)) ++sAgreeY; else ++sDisY; }
				const bool fx = (sAgreeX + sDisX >= 4) && sDisX > sAgreeX, fy = (sAgreeY + sDisY >= 4) && sDisY > sAgreeY;
				if(fx != sFlipX) { sFlipX = fx; _MESSAGE("dot: exact projection X axis %s", fx ? "inverted - flipping" : "ok"); }
				if(fy != sFlipY) { sFlipY = fy; _MESSAGE("dot: exact projection Y axis %s", fy ? "inverted - flipping" : "ok"); }
				static int sLogN = 0;
				if(sLogN < 6) { ++sLogN; _MESSAGE("dot: exact ndc=(%.3f,%.3f) estimate=(%.3f,%.3f)", ex, ey, *ndcX, *ndcY); }
				*ndcX = sFlipX ? -ex : ex;
				*ndcY = sFlipY ? -ey : ey;
			}
		}
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

	// ------------------------------------------------------------------ v0.24.0: camera livre e camera do cavalo
	// Com a camera livre (byte 0xB4 ligado: Coeus / TDM / cavalo) o jogo usa deslocamentos em relacao ao corpo: 0xAC = giro, 0xB0 = inclinacao
	// (positivo = olhar para baixo). Escrever em 0x54/0x58 nao mexia na vista vertical nesse modo. A correcao usa a direcao REAL da camera.
	// v0.24.1: o sinal de 0xB0 nao e o que se supunha (no log, diffRotX negativo = olhar para baixo na camera livre de 3a pessoa). Em vez de
	// supor, o sinal e calibrado: se a camera se mover ao contrario do comando 3 leituras seguidas, o sinal inverte-se (fica no log).
	struct PitchCal { float sign; float lastRay; float lastCmd; int bad; bool have; DWORD t0; };
	static void PitchCalStep(float * dx, PitchCal & c, float rayUp, float err, float camMax, float camSmooth, float dz, const char * tag)
	{
		const DWORD now = GetTickCount();
		float dtc = c.have ? (float)(DWORD)(now - c.t0) * 0.001f : 0.016f;
		if(dtc < 0.001f) dtc = 0.001f; if(dtc > 0.05f) dtc = 0.05f;
		if(c.have && fabsf(c.lastCmd) > 0.0035f)
		{
			const float obs = rayUp - c.lastRay;
			if(obs * c.lastCmd < 0.0f && fabsf(obs) > 0.002f) { if(++c.bad >= 3) { c.sign = -c.sign; c.bad = 0; _MESSAGE("%s: pitch sign set to %+.0f (the camera moved against the command)", tag, c.sign); } }
			else if(obs * c.lastCmd > 0.0f) c.bad = 0;
		}
		c.lastRay = rayUp; c.t0 = now; c.have = true;
		const float eff = fabsf(err) <= dz ? 0.0f : err - (err > 0.0f ? dz : -dz);
		const float step = StepToward(0.0f, eff, false, dtc, camMax, camSmooth);
		c.lastCmd = step;
		float nv = *dx + c.sign * step;
		if(nv >  1.2f) nv =  1.2f;
		if(nv < -1.2f) nv = -1.2f;
		DeferF(dx, nv);
	}

	void FreeCamPitchStep(UInt8 * cs, PlayerCharacter * player, float dt, float camMax, float camSmooth, float dz)
	{
		float o[3], dd[3];
		if(!CameraRay(player, o, dd)) return;
		float * dx = (float *)(cs + 0xB0);
		const float hr = sqrtf(dd[0] * dd[0] + dd[1] * dd[1]);
		const float rayUp = atan2f(dd[2], hr > 0.0001f ? hr : 0.0001f);
		static float s_ray = 1000.0f; static DWORD s_t = 0;
		const DWORD now = GetTickCount();
		if(fabsf(rayUp - s_ray) < 0.00002f && (DWORD)(now - s_t) < 33) return;		// so uma correcao por leitura nova da camera
		s_ray = rayUp; s_t = now;
		const float hx = g_aimX - o[0], hy = g_aimY - o[1];
		const float hh = sqrtf(hx * hx + hy * hy);
		const float aimUp = atan2f(g_aimZ - o[2], hh > 1.0f ? hh : 1.0f);
		const float err = aimUp - rayUp;
		g_ctrErrPitch = err;
		static PitchCal s_cal = { 1.0f, 0.0f, 0.0f, 0, false, 0 };
		PitchCalStep(dx, s_cal, rayUp, err, camMax, camSmooth, dz, "freepitch");
		static DWORD s_log = 0; static int s_n = 0;
		if(s_n < 80 && (DWORD)(now - s_log) >= 500) { s_log = now; ++s_n; _MESSAGE("freepitch: aim vs centre %.2f deg | ray elevation %.1f deg, aim elevation %.1f deg, diffRotX %.1f deg, sign %+.0f", err * 57.2958f, rayUp * 57.2958f, aimUp * 57.2958f, *dx * 57.2958f, s_cal.sign); }
	}

	// Cavalo: a camera do cavalo e livre (0xB4). Guia-se giro (0xAC) e inclinacao (0xB0) pela direcao real da camera; o corpo e o cavalo nao giram.
	void MountedCameraTick(PlayerCharacter * player, UInt8 * hs, float dt, DWORD now, float camMax, float camSmooth)
	{
		*(hs + 0xB4) = 1;
		float o[3], d[3];
		if(!CameraRay(player, o, d)) return;
		static float s_ray = 1000.0f, s_rayU = 1000.0f; static DWORD s_t0 = 0;
		const float rayYaw = atan2f(d[0], d[1]);
		const float hr = sqrtf(d[0] * d[0] + d[1] * d[1]);
		const float rayUp = atan2f(d[2], hr > 0.0001f ? hr : 0.0001f);
		if(fabsf(WrapPi(rayYaw - s_ray)) < 0.00002f && fabsf(rayUp - s_rayU) < 0.00002f && (DWORD)(now - s_t0) < 100) return;	// sem leitura nova da camera
		float dtc = (float)(DWORD)(now - s_t0) * 0.001f; if(dtc < 0.001f) dtc = 0.001f; if(dtc > 0.05f) dtc = 0.05f;
		s_ray = rayYaw; s_rayU = rayUp; s_t0 = now;
		const float hx = g_aimX - o[0], hy = g_aimY - o[1];
		const float hh = sqrtf(hx * hx + hy * hy);
		const float errYaw = WrapPi(atan2f(hx, hy) - rayYaw);
		const float errPitch = atan2f(g_aimZ - o[2], hh > 1.0f ? hh : 1.0f) - rayUp;
		g_ctrErrYaw = errYaw; g_ctrErrPitch = errPitch;
		const float dz = 0.3f * kPi / 180.0f;
		const float ey = fabsf(errYaw) <= dz ? 0.0f : errYaw - (errYaw > 0.0f ? dz : -dz);
		const float ep = fabsf(errPitch) <= dz ? 0.0f : errPitch - (errPitch > 0.0f ? dz : -dz);
		float * offZ = (float *)(hs + 0xAC);
		float * offX = (float *)(hs + 0xB0);
		*offZ = WrapPi(*offZ + StepToward(0.0f, ey, true, dtc, camMax, camSmooth));
		static PitchCal s_calH = { -1.0f, 0.0f, 0.0f, 0, false, 0 };
		PitchCalStep(offX, s_calH, rayUp, errPitch, camMax, camSmooth, dz, "mounted");
		static DWORD s_log = 0; static int s_n = 0;
		if(s_n < 200 && (DWORD)(now - s_log) >= 400)
		{
			s_log = now; ++s_n;
			_MESSAGE("mounted: aim vs centre yaw %.2f deg, pitch %.2f deg | offsets %.1f / %.1f deg | assist %d", errYaw * 57.2958f, errPitch * 57.2958f, *offZ * 57.2958f, *offX * 57.2958f, (int)g_assist);
		}
	}

	// v0.24.0: assistencia na viga. Alinha a direcao da vista (a que o movimento usa) com o eixo da viga e puxa-a de volta ao centro, como o mod
	// original faz com a DLL. Metodo 0: gira o corpo (a camera acompanha); metodo 1: gira a camera de 3a pessoa livre de corpo. Se o metodo atual
	// nao mexe de facto a vista em 0,5 s, troca para o outro (fica registado).
	void BeamSteerTick(PlayerCharacter * player, float dt, DWORD now)
	{
		static bool s_on = false; static int s_method = -1; static float s_cmd = 0.0f, s_ray0 = 0.0f; static DWORD s_t0 = 0; static int s_sw = 0, s_lg = 0;
		static DWORD s_lastOk = 0, s_dg = 0; static int s_dn = 0;
		static float s_ppx = 0.0f, s_ppy = 0.0f; static DWORD s_pt = 0, s_mvT = 0;
		g_envNeedMirror = g_envNeed;
		{
			// v0.25.0: "andar" tambem se mede pelo deslocamento real (as teclas nem sempre chegam ao nosso leitor)
			const NiPoint3 pp = Engine::Position(player);
			if(!s_pt) { s_ppx = pp.x; s_ppy = pp.y; s_pt = now; }
			else if((DWORD)(now - s_pt) >= 100)
			{
				const float dx = pp.x - s_ppx, dy = pp.y - s_ppy;
				if(sqrtf(dx * dx + dy * dy) / ((float)(DWORD)(now - s_pt) * 0.001f) > 20.0f) s_mvT = now;
				s_ppx = pp.x; s_ppy = pp.y; s_pt = now;
			}
		}
		const bool movingNow = g_moveHeld || (DWORD)(now - s_mvT) < 300;
		{
			// v0.25.1: o trilho guarda a LINHA da viga (centro + direcao) quando a deteta e mede o desvio pela posicao do jogador a cada ciclo.
			// Fica ligado ate saltares / caires (sem chao ~120 ms) ou te afastares da linha mais de 45 unidades.
			static bool s_rl = false; static float s_cx = 0.0f, s_cy = 0.0f, s_ch = 0.0f; static DWORD s_airSince = 0, s_lastFresh = 0, s_lastAir = 0;
			const NiPoint3 rp = Engine::Position(player);
			const bool grounded = g_es.ok0 && g_es.vertPlayer < 15.0f;
			if(!grounded) { if(!s_airSince) s_airSince = now; s_lastAir = now; } else s_airSince = 0;
			const bool fresh = g_cfg.beamRail && g_envOnBeam && g_beamOk && (DWORD)(now - g_beamTick) < 1000 && g_es.ok0 && !IsMountedCam();
			if(fresh && grounded && g_beamTick != s_lastFresh && (s_rl || (DWORD)(now - s_lastAir) > 400))
			{
				s_lastFresh = g_beamTick;
				const float hh = g_beamHead, ll = g_beamLat;
				const float ncx = rp.x + cosf(hh) * ll, ncy = rp.y - sinf(hh) * ll;
				if(!s_rl) { s_rl = true; s_cx = ncx; s_cy = ncy; s_ch = hh; _MESSAGE("beam rail: ON (line axis %.0f deg, offset %.0f)", hh * 57.2958f, ll); }
				else if(fabsf(ll) < 20.0f) { s_cx = ncx; s_cy = ncy; s_ch = hh; }
			}
			if(s_rl)
			{
				const float lat = (s_cx - rp.x) * cosf(s_ch) + (s_cy - rp.y) * (-sinf(s_ch));
				if(!g_cfg.beamRail || IsMountedCam()) { s_rl = false; _MESSAGE("beam rail: OFF (disabled)"); }
				else if(s_airSince && (DWORD)(now - s_airSince) > 120) { s_rl = false; _MESSAGE("beam rail: OFF (left the ground)"); }
				else if(fabsf(lat) > 45.0f) { s_rl = false; _MESSAGE("beam rail: OFF (%.0f units from the line)", lat); }
				else { g_railLat = lat; g_railHead = s_ch; g_railTick = now; g_railOn = 1; }
			}
			if(!s_rl) g_railOn = 0;
		}
		const bool ok = g_cfg.beamAssist && g_envOnBeam && g_beamOk && (DWORD)(now - g_beamTick) < 1000 && movingNow && !g_lockOwn && !g_active && !IsMountedCam();
		if(!ok)
		{
			// so desliga (e regista) depois de 400 ms seguidos sem condicoes: evita o liga/desliga a cada passo
			if(s_on && (DWORD)(now - s_lastOk) > 400) { s_on = false; _MESSAGE("beam: assist OFF"); }
			if(g_envOnBeam && !s_on && s_dn < 40 && (DWORD)(now - s_dg) >= 1500) { s_dg = now; ++s_dn; _MESSAGE("beam: on a beam but not steering: assist %d beamOk %d age %u ms, keys held %d, lock %d, session %d, mounted %d", g_cfg.beamAssist, (int)g_beamOk, (unsigned)(now - g_beamTick), (int)g_moveHeld, (int)g_lockOwn, (int)g_active, (int)IsMountedCam()); }
			s_t0 = 0; s_cmd = 0.0f;
			return;
		}
		s_lastOk = now;
		float o[3], d[3];
		if(!CameraRay(player, o, d)) return;
		const float cy = atan2f(d[0], d[1]);
		float head = g_beamHead;
		float latSign = 1.0f;
		if(fabsf(WrapPi(cy - head)) > kPi * 0.5f) { head = WrapPi(head + kPi); latSign = -1.0f; }
		float ln = latSign * g_beamLat / (g_beamHalf > 8.0f ? g_beamHalf : 8.0f);
		if(ln > 1.0f) ln = 1.0f; if(ln < -1.0f) ln = -1.0f;
		const float dzn = g_cfg.beamDead;
		ln = fabsf(ln) <= dzn ? 0.0f : (ln - (ln > 0.0f ? dzn : -dzn)) / (1.0f - dzn);
		const float want = WrapPi(head + g_cfg.beamLatGain * ln);
		const float err = WrapPi(want - cy);
		float step = err * (1.0f - expf(-dt / g_cfg.beamTau));
		const float lim = g_cfg.beamMaxRate * dt;
		if(step > lim) step = lim; if(step < -lim) step = -lim;
		if(!s_on)
		{
			s_on = true; s_t0 = 0; s_cmd = 0.0f;
			_MESSAGE("beam: assist ON (view %.0f deg, beam axis %.0f deg, lateral %.0f)", cy * 57.2958f, head * 57.2958f, g_beamLat);
		}
		UInt8 * cs = ThirdPersonStatePtr();
		if(s_method < 0) s_method = (cs && !*(cs + 0xB4)) ? 1 : 0;
		if(s_method == 1 && cs)
		{
			float c = WrapPi(*(float *)(cs + 0x5C) + step);
			DeferF((float *)(cs + 0x5C), c); DeferF((float *)(cs + 0x60), c);
		}
		else
		{
			NiPoint3 & rot = Engine::Rotation(player);
			rot.z = WrapPi(rot.z + step);
		}
		if(!s_t0) { s_t0 = now; s_cmd = 0.0f; s_ray0 = cy; }
		s_cmd += step;
		if((DWORD)(now - s_t0) >= 500)
		{
			const float obs = WrapPi(cy - s_ray0);
			if(s_lg < 40) { ++s_lg; _MESSAGE("beam: method %d, commanded %.1f deg, view moved %.1f deg, error %.1f deg", s_method, s_cmd * 57.2958f, obs * 57.2958f, err * 57.2958f); }
			if(fabsf(s_cmd) > 0.15f && obs * s_cmd < 0.25f * s_cmd * s_cmd && s_sw < 6)
			{
				++s_sw; s_method = s_method ? 0 : 1;
				_MESSAGE("beam: the view did not follow - switching steering method to %d", s_method);
			}
			s_t0 = 0;
		}
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
			Sleep((g_active || g_lockOwn || g_fastHold) ? (DWORD)(g_cfg.updateMs > (int)PerfTurnMin() ? g_cfg.updateMs : (int)PerfTurnMin()) : 12);
			const DWORD now = GetTickCount();
			float dt = (now - last) / 1000.0f;
			last = now;
			if(dt > 0.1f) dt = 0.1f;
			if(g_gameLive) FrameQueue();

			// v0.16.2: mod desligado no MCM, jogo ainda a carregar ou menu aberto: nao se mexe em nada.
			if(!g_cfg.enabled || !g_gameLive || (now - g_liveTick) < 1500)
			{
				if(g_lockOwn) LockOff(g_cfg.enabled ? "game not ready" : "mod turned off");
				g_active = 0; g_fastHold = 0; g_mvWant = 0; phase = 0; g_perfLast = 0;
				Sleep(50);
				continue;
			}
			if(MenuBlockedCached()) { g_fastHold = 0; g_mvWant = 0; g_perfLast = 0; continue; }
			if(AIDrivenCached())
			{
				if(g_lockOwn) LockOff("the game took control of the player");
				g_active = 0; g_fastHold = 0; g_mvWant = 0; phase = 0; g_perfLast = 0;
				Sleep(20);
				continue;
			}

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

			// Primeira pessoa / arco: o SoftTarget automatico larga (o lock proprio continua).
			if(g_active && !g_lockOwn && SoftTargetBlocked()) { g_endReason = 5; g_active = 0; phase = 0; }

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
				static const char * why[] = { "timeout", "sprint", "external lock-on active", "no target in range", "own lock-on off", "first person / bow (no SoftTarget there)" };
				_MESSAGE("session end: %s", why[(g_endReason >= 0 && g_endReason < 6) ? g_endReason : 0]);
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

			// Vista com o lock (FreeCam, primeira pessoa e arco na mao). Modo 2 = duro: a vista fica sempre no alvo e o rato e ignorado;
			// modo 1 = suave: o rato e livre e o lock toma o controlo total enquanto se ataca; tecla de olhar livremente liberta a vista.
			const bool softFP = IsFirstPerson();
			// v0.18.2: cauda de soltura. Ao soltar a flecha o corpo/camera mantem o comportamento de "puxando" durante BowReleaseTailMs.
			const bool bowRaw = g_cfg.bowLockCamera && BowAiming(player);
			static bool s_prevRaw = false; static DWORD s_tailUntil = 0;
			if(bowRaw) s_tailUntil = 0;
			else if(s_prevRaw && g_lockOwn)
			{
				if(g_cfg.bowReleaseTailMs > 0) s_tailUntil = now + (DWORD)g_cfg.bowReleaseTailMs;
				g_relTraceUntil = now + 800;
				_MESSAGE("bowrelease: arrow released (tail %d ms)", g_cfg.bowReleaseTailMs);
			}
			s_prevRaw = bowRaw;
			const bool bowDrawing = bowRaw || (!bowRaw && (LONG)(s_tailUntil - now) > 0);
			const bool softBow = g_cfg.bowLockCamera && !softFP && (bowDrawing || BowEquipped(player));
			const bool viewCtl = coeusProfile && g_cfg.lockViewMode > 0 && g_lockOwn && (softFP || softBow);
			bool viewHard = false;
			float yieldF = 1.0f;
			if(viewCtl)
			{
				float fk = 1.0f;
				if(g_freeLookHeld) { g_freeLookTick = now; fk = 0.0f; }
				else
				{
					const float since = (float)(DWORD)(now - g_freeLookTick);
					const float rm = (float)g_cfg.lockMouseRampMs;
					fk = (since < rm) ? since / rm : 1.0f;
				}
				float mf = 1.0f;
				if(g_cfg.lockViewMode == 1 && !(g_holdLeft || g_holdRight))
				{
					const float dtm = (float)(DWORD)(now - g_mouseTick);
					const float ym = (float)g_cfg.lockMouseYieldMs, rm2 = (float)g_cfg.lockMouseRampMs;
					mf = (dtm < ym) ? 0.0f : ((dtm < ym + rm2) ? (dtm - ym) / rm2 : 1.0f);
				}
				yieldF = (fk < mf) ? fk : mf;
				viewHard = (yieldF >= 1.0f) && (g_cfg.lockViewMode == 2 || (g_holdLeft || g_holdRight));
			}
			const float softMax = viewHard ? 50.0f : g_cfg.lockSoftSpeedDeg * kPi / 180.0f * yieldF;
			const float softSm = viewHard ? 0.0f : g_cfg.lockSoftSmooth;
			const float softDz = viewHard ? 0.0f : g_cfg.lockSoftDeadZone * kPi / 180.0f;

			// Rotacao do input: aprende onde esta o vetor de movimento e pede a thread do input para o rodar.
			if(g_cfg.moveRemap && !g_mvFound && g_mvLearnState == 0)
			{
				static DWORD lastIniTry = 0;
				if(now - lastIniTry > 2000) { lastIniTry = now; MvLoadSaved(); }
			}
			g_mvWant = (remapOn && !(viewCtl && (softFP || bowDrawing)) && g_active && !suppress && g_moveHeld) ? 1 : 0;
			g_mvWantTick = now;
			// Camera ao ombro com o arco (os dois perfis).
			{
				// v0.16.1 diagnostico: a thread parou? (lags) e quanto o ponto de mira esta longe do centro do ecra (arco / primeira pessoa).
				static DWORD s_lastCtr = 0; static int s_stallLogs = 0, s_ctrLogs = 0;
				if(g_perfLast && (now - g_perfLast) > 150 && s_stallLogs < 40) { ++s_stallLogs; _MESSAGE("perf: SoftTarget thread stalled %lu ms", (unsigned long)(now - g_perfLast)); }
				g_perfLast = now;
				if(g_lockOwn && (softFP || bowDrawing || (viewCtl && softBow)) && (now - s_lastCtr) > 500 && s_ctrLogs < 600)
				{
					s_lastCtr = now; ++s_ctrLogs;
					_MESSAGE("center: %s | aim point vs screen centre: yaw %.2f deg, pitch %.2f deg", softFP ? "first person" : (bowDrawing ? "bow" : "bow-ready (not drawing)"), g_ctrErrYaw * 57.2958f, g_ctrErrPitch * 57.2958f);
				}
			}
			{
				static DWORD s_trRel = 0;
				if((LONG)(g_relTraceUntil - now) > 0 && (DWORD)(now - s_trRel) >= 60)
				{
					s_trRel = now;
					UInt8 * rcs = ThirdPersonStatePtr();
					_MESSAGE("releasetrace: %ld ms left | aim vs centre yaw %.2f deg | body yaw %.2f | camera yaw %.2f offset %.2f | remap=%d tail=%d",
							 (long)(g_relTraceUntil - now), g_ctrErrYaw * 57.2958f, Engine::Rotation(player).z * 57.2958f,
							 rcs ? *(float *)(rcs + 0x5C) * 57.2958f : 0.0f, rcs ? *(float *)(rcs + 0xAC) * 57.2958f : 0.0f, (int)g_mvWant, (!bowRaw && bowDrawing) ? 1 : 0);
				}
			}
			__try { ShoulderTick(player, dt, now); }
			__except(EXCEPTION_EXECUTE_HANDLER) { g_shOn = false; }

			// Com a rotacao do input a andar, quem prende o corpo ao alvo e a HoldThread (rapida); esta thread so trata do resto.
			const bool fastHold = (g_mvWant != 0) && !sprintHold;
			g_fastHold = fastHold ? 1 : 0;
			{
				// v0.18.2: sprint livre com o lock (perfil FreeCam): so com a rotacao do input ativa, fora da mira com arco / 1a pessoa.
				const bool sprintFree = g_cfg.sprintFreeBody && coeusProfile && g_lockOwn && g_shiftHeld && g_moveHeld && g_mvWant && !(viewCtl && (softFP || bowDrawing));
				static bool s_prevSprint = false;
				if(sprintFree != s_prevSprint) { s_prevSprint = sprintFree; _MESSAGE("sprint: free body %s (lock on, Shift %d, move %d)", sprintFree ? "ON" : "OFF", (int)g_shiftHeld, (int)g_moveHeld); }
				g_sprintOn = sprintFree ? 1 : 0; g_sprintTick = now;
			}
			g_fastHoldTick = now;

			NiPoint3 & rot = Engine::Rotation(player);
			const float maxRad = g_cfg.turnSpeedDeg * kPi / 180.0f;
			const float limitRad = g_cfg.verticalLimitDeg * kPi / 180.0f;
			const float vSign = g_cfg.verticalInvert ? -1.0f : 1.0f;
			const bool returnAllowed = g_cfg.returnToHeading && !coeusProfile;
			const bool firstPerson = IsFirstPerson();
			LockCamGuard(player, now, !firstPerson && !(viewCtl && (softFP || bowDrawing)));		// v0.20.0: com arco equipado (sem puxar) o limite tambem vale
			{
				// v0.17.0: ao sair da 1a pessoa sem lock nem arco, a inclinacao do corpo (vista para cima/baixo) volta ao nivel em ~350 ms.
				static bool s_prevFP = false; static DWORD s_levelUntil = 0;
				if(s_prevFP && !firstPerson) s_levelUntil = GetTickCount() + 350;
				s_prevFP = firstPerson;
				if(g_cfg.levelPosture && !firstPerson && !g_lockOwn && !(RangedHeld(player) || g_assist != 0) && (LONG)(s_levelUntil - GetTickCount()) > 0 && fabsf(rot.x) > 0.01f)
					rot.x += StepToward(rot.x, 0.0f, false, dt, maxRad, 12.0f);
			}
			// Inclinar o corpo (bracos): pela opcao de mira vertical, ou com o lock-on proprio a prever um disparo / em primeira pessoa.
			const bool rangedNow = RangedHeld(player) || g_assist != 0;
			// v0.16.6: com o lock e o arco equipado (3a pessoa) a inclinacao do corpo ja vai para o alvo ANTES da puxada; assim o corpo nao arranca
			// com a inclinacao antiga (por exemplo a vista para o chao herdada da 1a pessoa) e a camera nao tem de "viajar" ao puxar.
			const bool bowReady3 = g_lockOwn && !firstPerson && g_cfg.bowLockCamera && BowEquipped(player);
			// v0.16.7: LockTiltAlways: em 3a pessoa, com o lock ligado, o corpo/cabeca olha sempre para o alvo (nao so com o arco).
			const bool tiltAlways3 = g_cfg.lockTiltAlways && g_lockOwn && !firstPerson;
			const bool useTilt = g_hasPitch && (((g_cfg.verticalMode & 1) && rangedNow) || (g_lockOwn && (g_assist || firstPerson)) || bowReady3 || tiltAlways3);

				// v0.24.0: no cavalo o lock prende a camera (e com ela a mira e o disparo) ao alvo; ao largar o lock a camera volta ao modo de antes.
				{
					static bool s_hOn = false; static UInt8 s_hPrev = 0; static DWORD s_hEase = 0;
					UInt8 * hs = HorseStatePtr();
					const bool wantH = g_lockOwn && g_cfg.lockMounted && hs;
					if(wantH)
					{
						if(!s_hOn) { s_hOn = true; s_hEase = 0; s_hPrev = *(hs + 0xB4); _MESSAGE("mounted: lock camera ON (free camera was %d, horse camera state)", (int)s_hPrev); }
						const float hsm = BowAiming(player) ? 14.0f : (g_cfg.lockCameraSmooth > 8.0f ? g_cfg.lockCameraSmooth : 8.0f);
						MountedCameraTick(player, hs, dt, now, maxRad, hsm);
					}
					else if(s_hOn)
					{
						if(!hs || s_hPrev) { s_hOn = false; _MESSAGE("mounted: lock camera OFF"); }
						else
						{
							// a camera estava presa ao cavalo: o deslocamento volta a zero em ~300 ms e so entao se repoe o modo.
							if(!s_hEase) s_hEase = now + 300;
							float * oz = (float *)(hs + 0xAC); float * ox = (float *)(hs + 0xB0);
							const float f = 1.0f - expf(-12.0f * dt);
							*oz = WrapPi(*oz - *oz * f); *ox -= *ox * f;
							if((LONG)(now - s_hEase) >= 0 || (fabsf(*oz) < 0.01f && fabsf(*ox) < 0.01f)) { *oz = 0.0f; *ox = 0.0f; *(hs + 0xB4) = 0; s_hOn = false; _MESSAGE("mounted: lock camera OFF (camera back to the horse)"); }
						}
					}
				}

			__try { BeamSteerTick(player, dt, now); }
			__except(EXCEPTION_EXECUTE_HANDLER) { }
			if(g_active && !suppress && !sprintHold)
			{
				if(phase == 0)
				{
					// Inicio da sessao: guarda de onde partimos (rumo e inclinacao do corpo).
					homeYaw = rot.z;
					homePitch = (firstPerson || fabsf(rot.x) > 0.6f) ? 0.0f : rot.x;		// a inclinacao herdada da 1a pessoa nao e para repor
					tiltUsed = false;
					phase = 1;
				}
				else if(phase == 2) phase = 1;

				float stepYaw;
				if(fastHold) stepYaw = 0.0f;
				else if(viewCtl)
				{
					if(!softFP)
					{
						// v0.20.0: com lock proprio e arco equipado o corpo usa SEMPRE a direcao a partir da camera (nao so ao puxar), para nao haver salto lateral (paralaxe) ao iniciar/soltar.
						// v0.16.5: com o arco puxado o corpo SEGUE o alvo continuamente (como no perfil normal), virando para onde a camera
						// (centrada no alvo) aponta. Antes o corpo ficava preso na direcao do inicio da puxada e so alcancava o alvo ao soltar.
						float wyb = g_targetYaw; float ob[3], db[3];
						if(CameraRay(player, ob, db)) wyb = atan2f(g_aimX - ob[0], g_aimY - ob[1]);
						stepYaw = StepToward(rot.z, wyb, true, dt, maxRad, g_cfg.smoothing);
					}
					else
					{
						// Primeira pessoa: a vista e o corpo. Erro medido pela direcao REAL da vista, zona morta pequena, puxao suave.
						// v0.16.2: o ponto de mira tem de ficar no CENTRO do ecra. O erro medido pela direcao REAL da vista e aplicado
						// de forma suave, distribuido pelos frames (nao aos saltos); com a vista presa (Hard) corrige logo, uma vez por leitura.
						static float s_rf = 1000.0f, s_pendY = 0.0f; static DWORD s_tf = 0;
						float o2[3], d2[3];
						if(CameraRay(player, o2, d2))
						{
							const float rayYaw = atan2f(d2[0], d2[1]);
							const float errYaw = WrapPi(atan2f(g_aimX - o2[0], g_aimY - o2[1]) - rayYaw);
							g_ctrErrYaw = errYaw;
							if(fabsf(WrapPi(rayYaw - s_rf)) >= 0.00002f || (DWORD)(now - s_tf) >= 100)
							{ s_rf = rayYaw; s_tf = now; s_pendY = viewHard ? 0.5f * errYaw : errYaw; }
						}
						else s_pendY = 0.0f;
						{
							const float eff = fabsf(s_pendY) <= softDz ? 0.0f : s_pendY - (s_pendY > 0.0f ? softDz : -softDz);
							const float frac = viewHard ? 1.0f : 1.0f - expf(-dt * (softSm > 1.0f ? softSm : 15.0f));
							float st = eff * frac; const float lim = softMax * dt;
							if(st > lim) st = lim; if(st < -lim) st = -lim;
							s_pendY -= st; stepYaw = st;
						}
					}
				}
				else stepYaw = StepToward(rot.z, g_targetYaw, true, dt, maxRad, g_cfg.smoothing);
				if(coeusProfile && g_cfg.cameraHoldTdm)
				{
					UInt8 * hcs = ThirdPersonStatePtr();
					// Camera livre (0xB4 ligado), fora da mira com arco (la o Coeus faz a sua propria compensacao; com magia a compensacao e nossa): o deslocamento perde o que o corpo ganha.
					if(hcs && *(hcs + 0xB4) && !BowAiming(player))
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
					if(viewCtl && softFP)
					{
						// v0.16.2: inclinacao medida pela direcao REAL da vista, aplicada de forma suave pelos frames.
						static float s_rp = 1000.0f, s_pendP = 0.0f; static DWORD s_tp = 0;
						float o4[3], d4[3];
						if(CameraRay(player, o4, d4))
						{
							const float hr = sqrtf(d4[0] * d4[0] + d4[1] * d4[1]);
							const float rayUp = atan2f(d4[2], hr > 0.0001f ? hr : 0.0001f);
							const float hx = g_aimX - o4[0], hy = g_aimY - o4[1];
							const float aimUp = atan2f(g_aimZ - o4[2], sqrtf(hx * hx + hy * hy) > 1.0f ? sqrtf(hx * hx + hy * hy) : 1.0f);
							const float errUp = aimUp - rayUp;
							g_ctrErrPitch = errUp;
							if(fabsf(rayUp - s_rp) >= 0.00002f || (DWORD)(now - s_tp) >= 100)
							{ s_rp = rayUp; s_tp = now; s_pendP = viewHard ? 0.5f * errUp : errUp; }
						}
						else s_pendP = 0.0f;
						{
							const float eff = fabsf(s_pendP) <= softDz ? 0.0f : s_pendP - (s_pendP > 0.0f ? softDz : -softDz);
							const float frac = viewHard ? 1.0f : 1.0f - expf(-dt * (softSm > 1.0f ? softSm : 15.0f));
							float st = eff * frac; const float lim = softMax * dt;
							if(st > lim) st = lim; if(st < -lim) st = -lim;
							s_pendP -= st; rot.x -= st;		// positivo = olhar para baixo
						}
					}
					else rot.x += StepToward(rot.x, -up * vSign, false, dt, maxRad, g_cfg.smoothing);	// positivo = olhar para baixo
					if(!firstPerson) tiltUsed = true;		// em primeira pessoa rot.x e a propria vista: nao se repoe
				}
				else if(tiltUsed)
				{
					// Deixou de mirar com arco/magia: a inclinacao do corpo volta ao que era (nao fica presa a olhar para baixo).
					rot.x += StepToward(rot.x, homePitch, false, dt, maxRad, g_cfg.smoothing);
					if(fabsf(homePitch - rot.x) < 0.02f) { rot.x = homePitch; tiltUsed = false; }
				}

				// Lock-on proprio: a camera acompanha o alvo (SO no perfil normal; no FreeCam a camera e sempre do rato).
				//  - camera presa ao corpo (0xB4 = 0): escreve-se o angulo da propria camera;
				//  - camera de deslocamento (0xB4 = 1, TDM): o deslocamento caminha para (alvo - corpo).
				const bool bowLockCam = coeusProfile && g_cfg.bowLockCamera && BowAiming(player);
				if(g_lockOwn && ((g_cfg.lockCameraFollow && !coeusProfile) || bowLockCam || viewCtl))
				{
					// Com o arco puxado (FreeCam) a camera centra o alvo a partir da posicao REAL da camera, mais depressa e sem zona morta.
					// v0.16.3: com o arco (puxado, ou equipado no modo de vista presa) a correcao e sempre suave, nunca o salto do Hard.
					const bool gentle = bowLockCam || (viewCtl && softBow);
					// v0.25.0: com o arco puxado a camera segue o alvo SEM atraso (so assim a previsao da flecha acerta alvos em movimento);
					// nos primeiros 200 ms depois de puxar ha um "encaixe" imediato no alvo (em vez de a camera o alcancar aos poucos).
					static DWORD s_bowSnapUntil = 0; static bool s_bowPrev = false;
					const bool instBow = bowLockCam && g_cfg.lockBowInstant;
					if(instBow && !s_bowPrev) s_bowSnapUntil = now + 200;
					s_bowPrev = instBow;
					const bool snapNow = instBow && (LONG)(s_bowSnapUntil - now) > 0;
					const float gainC = instBow ? 1.0f : (gentle ? 0.8f : 0.5f);
					const float camSmooth = instBow ? (snapNow ? 400.0f : 120.0f) : (gentle ? (g_cfg.lockCameraSmooth > 14.0f ? g_cfg.lockCameraSmooth : 14.0f) : (viewCtl ? softSm : g_cfg.lockCameraSmooth));
					const float dzRad = viewCtl ? softDz : (bowLockCam ? 0.0f : g_cfg.camDeadZoneDeg * kPi / 180.0f);
					const float camMax = instBow ? 25.0f : (gentle ? maxRad : (viewCtl ? softMax : maxRad));
					UInt8 * cs = ThirdPersonStatePtr();
					if(cs)
					{
						if(!*(cs + 0xB4))
						{
							float cy = *(float *)(cs + 0x5C);
							float wantYaw = g_targetYaw;
							// v0.15.1: o erro medido pela direcao REAL da camera so muda uma vez por frame; corrigi-lo a cada 1 ms
							// aplicava a mesma correcao ~16 vezes e a camera oscilava. So se corrige quando ha leitura nova.
							bool camHold = false;
							float dtc = dt;
							{
								// Erro entre a direcao REAL da camera e o alvo, medido a partir da posicao da camera: compensa
								// qualquer deslocamento lateral/atras da camera (mods tipo AIM FIX).
								float o2[3], d2[3];
								if(CameraRay(player, o2, d2))
								{
									static float s_ray0 = 1000.0f; static DWORD s_t0 = 0;
									const float rayYaw = atan2f(d2[0], d2[1]);
									if(fabsf(WrapPi(rayYaw - s_ray0)) < 0.00002f && (DWORD)(now - s_t0) < 100) camHold = true;
									else
									{
										dtc = (float)(DWORD)(now - s_t0) * 0.001f; if(dtc < 0.001f) dtc = 0.001f; if(dtc > 0.05f) dtc = 0.05f;
										s_ray0 = rayYaw; s_t0 = now;
										g_ctrErrYaw = WrapPi(atan2f(g_aimX - o2[0], g_aimY - o2[1]) - rayYaw);
										wantYaw = WrapPi(cy + gainC * WrapPi(atan2f(g_aimX - o2[0], g_aimY - o2[1]) - rayYaw));
									}
								}
							}
							wantYaw = WrapPi(wantYaw - g_cfg.aimOffX * kPi / 180.0f);		// alvo a direita do centro = camera um pouco mais a esquerda
							{
								// Zona morta: erros pequenos nao mexem a camera (os golpes do alvo nao a abanam); fora dela segue sem salto.
								const float dz = dzRad;
								const float dl = WrapPi(wantYaw - cy);
								if(fabsf(dl) <= dz) wantYaw = cy;
								else wantYaw = WrapPi(cy + dl - (dl > 0.0f ? dz : -dz));
							}
							if(!camHold)
							{
								cy = WrapPi(cy + StepToward(cy, wantYaw, true, dtc, camMax, camSmooth));
								DeferF((float *)(cs + 0x5C), cy);
								DeferF((float *)(cs + 0x60), cy);
							}
						}
						else
						{
							float * off = (float *)(cs + 0xAC);
							float want = WrapPi(g_targetYaw - rot.z);
							const float dzO = viewCtl ? softDz : dzRad;
							if(gentle)
							{
								// v0.16.4: a camera e corrigida pelo ANGULO PREVISTO NO MUNDO. A cada leitura nova da camera guarda-se (direcao, corpo, deslocamento);
								// entre leituras a direcao prevista = leitura + (o que o corpo girou) + (o que o deslocamento mudou). Assim o corpo a girar
								// ao puxar o arco (jogo/Coeus) nao faz a correcao correr atras de si propria, e pode corrigir-se a cada ms sem pausas.
								static float s_ray = 1000.0f, s_r0 = 0.0f, s_rot0 = 0.0f, s_off0 = 0.0f; static bool s_have = false;
								static bool s_prevDraw = false;
								if(bowDrawing && !s_prevDraw) { _MESSAGE("bowdraw: before drawing the aim point was %.1f deg (yaw) / %.1f deg (pitch) from the screen centre", g_ctrErrYaw * 57.2958f, g_ctrErrPitch * 57.2958f); g_drawTraceUntil = now + 700; g_snapReset = true; s_have = false; }
								s_prevDraw = bowDrawing;
								float o3[3], d3[3];
								if(CameraRay(player, o3, d3))
								{
									const float rayYaw = atan2f(d3[0], d3[1]);
									if(!s_have || fabsf(WrapPi(rayYaw - s_ray)) >= 0.00002f) { s_ray = rayYaw; s_r0 = rayYaw; s_rot0 = rot.z; s_off0 = *off; s_have = true; }
									const float predicted = WrapPi(s_r0 + WrapPi(rot.z - s_rot0) + WrapPi(*off - s_off0));
									const float err = WrapPi(atan2f(g_aimX - o3[0], g_aimY - o3[1]) - predicted);
									g_ctrErrYaw = err;
									if(fabsf(err) <= dzO) want = *off;
									else want = WrapPi(*off + err - (err > 0.0f ? dzO : -dzO));
								}
								else want = *off;
								DeferF(off, WrapPi(*off + StepToward(*off, want, true, dt, camMax, camSmooth)));
							}
							else
							{
								*off = WrapPi(*off + StepToward(*off, want, true, dt, camMax, camSmooth));
							}
						}

						float o[3], dd[3];
						if(g_cfg.lockFreePitch && g_cfg.lockCameraPitch && *(cs + 0xB4))
						{
							// v0.24.0: camera livre: a inclinacao vertical e o deslocamento 0xB0, nao 0x54.
							FreeCamPitchStep(cs, player, dt, camMax, camSmooth, dzRad);
						}
						else if(g_cfg.lockCameraPitch && CameraRay(player, o, dd))
						{
							const float hx = g_aimX - o[0], hy = g_aimY - o[1];
							float cp = *(float *)(cs + 0x54);
							// v0.16.1: com o arco puxado a inclinacao e corrigida pelo erro REAL (ponto de mira vs centro do ecra), uma vez por leitura.
							float want = -atan2f(g_aimZ - o[2], sqrtf(hx * hx + hy * hy) > 1.0f ? sqrtf(hx * hx + hy * hy) : 1.0f);
							if(gentle)
							{
								// v0.16.4: mesma previsao no eixo vertical (cp positivo = olhar para baixo).
								static float s_rayU = 1000.0f, s_ru0 = 0.0f, s_cp0 = 0.0f; static bool s_haveP = false;
								const float hr = sqrtf(dd[0] * dd[0] + dd[1] * dd[1]);
								const float rayUp = atan2f(dd[2], hr > 0.0001f ? hr : 0.0001f);
								if(g_snapReset) { s_haveP = false; g_snapReset = false; }
								if(!s_haveP || fabsf(rayUp - s_rayU) >= 0.00002f) { s_rayU = rayUp; s_ru0 = rayUp; s_cp0 = cp; s_haveP = true; }
								const float predUp = s_ru0 - (cp - s_cp0);
								const float aimUp = -want;
								g_ctrErrPitch = aimUp - predUp;
								{
									static DWORD s_trLast = 0;
									if((LONG)(g_drawTraceUntil - now) > 0 && (DWORD)(now - s_trLast) >= 60)
									{
										s_trLast = now;
										_MESSAGE("drawtrace: %ld ms before the end | aim vs centre: pitch %.2f deg (ray elevation %.2f, camera pitch %.2f, aim elevation %.2f)", (long)(g_drawTraceUntil - now), g_ctrErrPitch * 57.2958f, rayUp * 57.2958f, cp * 57.2958f, aimUp * 57.2958f);
									}
								}
								want = cp - (aimUp - predUp);
							}
							else want += g_cfg.aimOffY * kPi / 180.0f;		// alvo acima do centro = camera um pouco mais para baixo
							if(want >  1.1f) want =  1.1f;
							if(want < -1.1f) want = -1.1f;
							const float dtb = dt;
							{
								const float dzp = dzRad;
								const float dp = want - cp;
								if(fabsf(dp) <= dzp) want = cp;
								else want = cp + dp - (dp > 0.0f ? dzp : -dzp);
							}
							cp += StepToward(cp, want, false, dtb, camMax, camSmooth);
							DeferF((float *)(cs + 0x54), cp);
							DeferF((float *)(cs + 0x58), cp);
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
	// v0.13.2: SoftTarget so em terceira pessoa e sem arco/besta (o lock proprio nao e afetado).
	bool SoftTargetBlocked()
	{
		if(g_lockOwn) return false;
		if(g_cfg.noSoftFirstPerson && IsFirstPerson()) return true;
		if(g_cfg.noSoftBow)
		{
			PlayerCharacter * p = Engine::Player();
			if(p && (BowEquipped(p) || BowAiming(p))) return true;
		}
		return false;
	}

	bool AcquireAndTurn(int windowMs, bool /*extend*/)
	{
		// Lock-on ativo: LockOnMode=0 deixa o SoftTarget em pausa.
		if(LockPauses()) return false;
		if(SoftTargetBlocked()) return false;

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
			_MESSAGE("probe-hold: fast=%d ticks %ld, body had to be put back on the target %ld times (high = Coeus keeps turning it)", (int)g_fastHold, g_holdTicks, g_holdFixes);
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
			MaybeReloadConfig();
			if(!g_cfg.enabled || !g_gameLive) return kEvent_Continue;

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
				if(e->eventType == InputEvent::kEventType_MouseMove)
				{
					// Movimento do rato (o lock suave cede). Os deslocamentos estao em +0x18 / +0x1C (como no ThumbstickEvent).
					const SInt32 mx = *(SInt32 *)((UInt8 *)e + 0x18), my = *(SInt32 *)((UInt8 *)e + 0x1C);
					static int mouseLogged = 0;
					if(mouseLogged < 6 && (mx || my)) { ++mouseLogged; _MESSAGE("mouse move event: x=%d y=%d (layout check)", (int)mx, (int)my); }
					if((mx || my) && mx > -100000 && mx < 100000 && my > -100000 && my < 100000)
					{
						g_mouseTick = GetTickCount();
						InterlockedExchangeAdd(&g_dmkMx, mx); InterlockedExchangeAdd(&g_dmkMy, my);
					}
					continue;
				}
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

				// Shift + roda do rato: ajusta a altura do ponto de mira (em combate / com lock). Tenta tirar o evento ao zoom.
				if(pressedNow && g_cfg.aimAdjustWheel && e->deviceType == kDeviceType_Mouse && g_shiftHeld && (g_active || g_lockOwn) && !MenuBlocksInput())
				{
					const int wc = (int)button->keyMask;
					if(wc == 8 || wc == 9)
					{
						AimAdjust(wc == 8 ? 3.0f : -3.0f);
						*(float *)&button->flags = 0.0f;
						continue;
					}
				}

				if(e->deviceType == kDeviceType_Keyboard)
				{
					const int code = (int)button->keyMask;
					if(InList(g_shiftKeys, g_numShiftKeys, code)) g_shiftHeld = held ? 1 : 0;
					if(g_cfg.lockFreeLookKey && (code == g_cfg.lockFreeLookKey || ((g_cfg.lockFreeLookKey == 42 || g_cfg.lockFreeLookKey == 54) && (code == 42 || code == 54))))
					{ g_freeLookHeld = held ? 1 : 0; if(held) g_freeLookTick = GetTickCount(); }
					if(pressedNow && g_shiftHeld && g_cfg.mvLearnKey && code == g_cfg.mvLearnKey && !MenuBlocksInput()) MvArm();
					if(pressedNow && g_shOn && g_cfg.bowSwapKey && code == g_cfg.bowSwapKey)
					{
						g_cfg.bowRight = g_cfg.bowRight ? 0 : 1;
						WritePrivateProfileStringA("SoftTarget", "BowShoulderRight", g_cfg.bowRight ? "1" : "0", g_iniPath);
						_MESSAGE("bow camera: %s shoulder", g_cfg.bowRight ? "right" : "left");
					}
					if(pressedNow && (g_active || g_lockOwn) && !MenuBlocksInput())
					{
						if(g_cfg.aimUpKey && code == g_cfg.aimUpKey) AimAdjust(3.0f);
						else if(g_cfg.aimDownKey && code == g_cfg.aimDownKey) AimAdjust(-3.0f);
					}

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
				// v0.26.1: BeamWalkLeap liga logo ao premir o salto em cima da viga / na zona de borda (antes de o jogo mudar de estado)
				if(held && *control == strings->jump && !g_leapOn && (g_envOnBeam || g_envEdgeZone) && g_envG[kEnvBeamLeap] && g_cfg.envProbe)
				{ g_leapOn = 1; g_leapTick = GetTickCount(); EnvSet(kEnvBeamLeap, 1.0f); _MESSAGE("env: BeamWalkLeap ON (jump key)"); }

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
				KtResolveMod();
				EnvResolveMod();
				KtInstall();
				_MESSAGE("keytrace-mask: DmkKeytrace=%d", g_cfg.dmkKeytrace);
				if(g_cfg.dmkKeytrace) KtInstallHasMe();
				break;

			case SKSEMessagingInterface::kMessage_PreLoadGame:
				g_gameLive = 0;
				if(g_lockOwn) LockOff("loading a game");
				break;

			case SKSEMessagingInterface::kMessage_PostLoadGame:
			case SKSEMessagingInterface::kMessage_NewGame:
				g_liveTick = GetTickCount();
				g_gameLive = 1;
				_MESSAGE("game loaded - the mod is now active");
				break;

			case SKSEMessagingInterface::kMessage_InputLoaded:
			{
				InputEventDispatcher * dispatcher = *g_inputEventDispatcher;
				if(!dispatcher) { _MESSAGE("input dispatcher not available - the mod will not work"); break; }
				dispatcher->AddEventSink(&g_sink);
				g_thr[kThrTurn] = CreateThread(NULL, 0, TurnThread, NULL, 0, NULL);
				g_thr[kThrMv] = CreateThread(NULL, 0, MvThread, NULL, 0, NULL);
				g_thr[kThrHold] = CreateThread(NULL, 0, HoldThread, NULL, 0, NULL);
				DmkInit();
				g_thr[kThrDmk] = CreateThread(NULL, 0, DmkThread, NULL, 0, NULL);
				g_thr[kThrEnv] = CreateThread(NULL, 0, EnvThread, NULL, 0, NULL);
				g_thr[kThrJump] = CreateThread(NULL, 0, EnvJumpThread, NULL, 0, NULL);
				// as threads que nao sao criticas correm com prioridade abaixo do normal
				if(g_thr[kThrDmk]) SetThreadPriority(g_thr[kThrDmk], THREAD_PRIORITY_BELOW_NORMAL);
				if(g_thr[kThrEnv]) SetThreadPriority(g_thr[kThrEnv], THREAD_PRIORITY_BELOW_NORMAL);
				if(g_thr[kThrJump]) SetThreadPriority(g_thr[kThrJump], THREAD_PRIORITY_BELOW_NORMAL);
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

// API publica para outros plugins. Retorna 0 quando nao existe alvo valido.
UInt32 SoftTarget_GetLockedFormID()
{
	if(!InterlockedCompareExchange(&g_lockOwn, 0, 0)) return 0;
	return (UInt32)InterlockedCompareExchange(&g_lockId, 0, 0);
}

UInt32 SoftTarget_GetAssistFormID()
{
	if(!InterlockedCompareExchange(&g_assist, 0, 0)) return 0;
	if(!InterlockedCompareExchange(&g_lockOwn, 0, 0)) return 0;
	return (UInt32)InterlockedCompareExchange(&g_lockId, 0, 0);
}

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

	g_mainTid = GetCurrentThreadId();
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
