// SoftTarget LE - golpes auto-direcionados (sem travar a mira), estilo Witcher 3.
//
// Ao carregar no botao de ataque (ou ao SEGURAR o botao de bloqueio), o plugin procura o inimigo
// mais proximo (cone a frente ou circulo a volta) e vira o CORPO do personagem para ele, de forma
// suave. Nao usa Papyrus. Todas as afinacoes estao no SoftTargetLE.ini, que e relido sozinho
// quando e guardado (nao precisa de reiniciar o jogo).
//
// ESTADO: versao 0.3.0 (experimental). Novidades: inimigos reanimados, compensacao da camera
// (CameraMode), pausa com a tecla do lock-on (LockOnKey) e mais diagnostico (Probe=1).
// Licenca GPL-3.0. A base de compilacao e os enderecos vem do FreeClimb LE (GPL-3.0).

#include "skse/PluginAPI.h"
#include "skse/skse_version.h"
#include "skse/GameRTTI.h"
#include "skse/GameCamera.h"
#include "Log.h"
#include "Engine.h"

#include <shlobj.h>
#include <math.h>
#include <string.h>

#define PLUGIN_NAME			"SoftTargetLE"
#define PLUGIN_VERSION_STR	"0.3.0 (reanimados, camera, lock-on, diagnostico)"
#define PLUGIN_LOG_FILE		"\\My Games\\Skyrim\\SKSE\\SoftTargetLE.log"
#define PLUGIN_INI_NAME		"SoftTargetLE.ini"
#define PLUGIN_INI_FALLBACK	"Data\\SKSE\\Plugins\\SoftTargetLE.ini"

static PluginHandle				g_pluginHandle	= kPluginHandle_Invalid;
static SKSEMessagingInterface *	g_messaging		= NULL;

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
		int		probe;			// 1 = diagnostico: escreve dados da camera/jogador/teclas no log (so leitura)
		int		useHostileList;	// 1 = tambem aceita quem o jogo ja lista como hostil ao jogador (ajuda com reanimados)
		int		cameraMode;		// 0 = nao mexe; 1 = contra-rotacao da camera; 2 = camera fica parada durante o giro
		int		lockOnKey;		// codigo da tecla do lock-on (F = 33); 0 = desligado
		int		requireCombat;	// 1 = so inimigos ja em combate
		int		shape;			// 0 = cone a frente, 1 = circulo a volta (360 graus)
		float	range;			// alcance maximo (140 = ~2 m)
		float	coneDeg;		// angulo TOTAL do cone (so no modo cone)
		float	turnSpeedDeg;	// velocidade maxima de rotacao, graus por segundo
		float	smoothing;		// 0 = velocidade constante; >0 = abranda ao chegar (maior = mais rapido)
		int		updateMs;		// intervalo da rotacao, em ms (menor = mais fluido)
		int		tapAssist;		// 1 = vira ao carregar no ataque
		int		holdMode;		// bloqueio continuo: 0 = off, 1 = botao esquerdo (M2), 2 = botao direito, 3 = ambos
		int		retargetMs;		// de quanto em quanto tempo re-escolhe o alvo no modo continuo
		int		holdMs;			// quanto tempo a rotacao e mantida depois de um toque no ataque
	};
	Config g_cfg;

	char		g_iniPath[MAX_PATH];
	FILETIME	g_iniStamp;
	DWORD		g_lastIniCheck = 0;

	bool FileExists(const char * path)
	{
		const DWORD a = GetFileAttributesA(path);
		return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
	}

	// Procura o .ini ao lado da DLL (funciona com Mod Organizer 2 e Vortex); se nao houver, usa
	// o caminho classico Data\SKSE\Plugins.
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

	void LoadConfig()
	{
		const bool found = g_iniPath[0] && FileExists(g_iniPath);
		if(found)
		{
			WIN32_FILE_ATTRIBUTE_DATA d;
			if(GetFileAttributesExA(g_iniPath, GetFileExInfoStandard, &d)) g_iniStamp = d.ftLastWriteTime;
		}

		g_cfg.enabled		= ReadInt("Enabled", 1);
		g_cfg.logOnly		= ReadInt("LogOnly", 0);
		g_cfg.verbose		= ReadInt("Verbose", 0);
		g_cfg.probe			= ReadInt("Probe", 0);
		g_cfg.useHostileList = ReadInt("UseHostileList", 1);
		g_cfg.cameraMode	= ReadInt("CameraMode", 0);
		g_cfg.lockOnKey		= ReadInt("LockOnKey", 0);
		g_cfg.requireCombat	= ReadInt("RequireCombat", 1);
		g_cfg.shape			= ReadInt("Shape", 0);
		g_cfg.range			= ReadFloat("Range", 450.0f);
		g_cfg.coneDeg		= ReadFloat("ConeDegrees", 140.0f);
		g_cfg.turnSpeedDeg	= ReadFloat("TurnSpeedDegPerSec", 900.0f);
		g_cfg.smoothing		= ReadFloat("Smoothing", 0.0f);
		g_cfg.updateMs		= ReadInt("UpdateMilliseconds", 8);
		g_cfg.tapAssist		= ReadInt("TapAssist", 1);
		g_cfg.holdMode		= ReadInt("HoldFaceButton", 0);
		g_cfg.retargetMs	= ReadInt("HoldRetargetMilliseconds", 100);
		g_cfg.holdMs		= ReadInt("HoldMilliseconds", 400);

		if(g_cfg.updateMs < 1)		g_cfg.updateMs = 1;
		if(g_cfg.updateMs > 50)		g_cfg.updateMs = 50;
		if(g_cfg.retargetMs < 16)	g_cfg.retargetMs = 16;

		_MESSAGE("config (%s%s): enabled=%d logOnly=%d shape=%s range=%.0f cone=%.0f turn=%.0f smooth=%.1f update=%dms tap=%d hold=%d retarget=%dms combat=%d camera=%d lockKey=%d hostileList=%d",
				 found ? "ini: " : "NO ini found, defaults; looked for: ", g_iniPath[0] ? g_iniPath : PLUGIN_INI_NAME,
				 g_cfg.enabled, g_cfg.logOnly, g_cfg.shape ? "circle" : "cone", g_cfg.range, g_cfg.coneDeg,
				 g_cfg.turnSpeedDeg, g_cfg.smoothing, g_cfg.updateMs, g_cfg.tapAssist, g_cfg.holdMode,
				 g_cfg.retargetMs, g_cfg.requireCombat, g_cfg.cameraMode, g_cfg.lockOnKey, g_cfg.useHostileList);
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

	// ------------------------------------------------------------------ matematica de angulos
	// No Skyrim a rotacao Z (guinada) e medida a partir de +Y, no sentido horario:
	// a direcao (dx, dy) corresponde a atan2(dx, dy).
	float WrapPi(float a)
	{
		while(a >  kPi) a -= 2.0f * kPi;
		while(a < -kPi) a += 2.0f * kPi;
		return a;
	}

	// ------------------------------------------------------------------ escolha do alvo
	// Inimigos que o proprio jogo ja tem em combate com o jogador (PlayerCharacter::hostileHandles,
	// offset 0x630: ponteiro + capacidade + tamanho). Ajuda com reanimados e invocados, para os
	// quais a pergunta "e hostil?" as vezes responde que nao.
	int CollectHostileIds(PlayerCharacter * player, UInt32 * ids, int maxIds)
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
			TESObjectREFR * ref = NULL;
			if(LookupREFRByHandle(&handle, &ref) && ref)
			{
				ids[n++] = ref->formID;
				ref->handleRefObject.DecRefHandle();
			}
		}
		return n;
	}

	DWORD g_lastActorLog = 0;

	// Devolve true se encontrou um alvo; escreve a guinada desejada e o formID.
	bool FindTarget(Actor * player, float * outYaw, UInt32 * outId)
	{
		TESObjectCELL * cell = Engine::ParentCell(player);
		if(!cell) return false;

		const NiPoint3 me		= Engine::Position(player);
		const float facing		= Engine::Rotation(player).z;
		const float halfCone	= (g_cfg.coneDeg * 0.5f) * kPi / 180.0f;
		const bool  cone		= (g_cfg.shape == 0);

		UInt32 hostileIds[64];
		const int hostileCount = g_cfg.useHostileList ? CollectHostileIds((PlayerCharacter *)player, hostileIds, 64) : 0;

		bool dbg = false;
		int dbgLines = 0;
		if(g_cfg.probe && GetTickCount() - g_lastActorLog > 1000) { dbg = true; g_lastActorLog = GetTickCount(); }

		float bestScore = 1e30f;
		bool found = false;

		// A lista de referencias da celula e cell->objectList (GameForms.h do SKSE 1.7.3, offset 0x4C):
		// ponteiro para os dados + dois contadores (capacidade e tamanho). Le-se em bruto e usa-se o
		// MENOR dos dois contadores, que e sempre o tamanho, qualquer que seja a ordem deles.
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
			if(dist > g_cfg.range || fabsf(dz) > 250.0f) continue;

			const UInt32 life = Engine::LifeState(actor);

			bool listed = false;
			for(int j = 0; j < hostileCount; ++j) if(hostileIds[j] == actor->formID) { listed = true; break; }

			// Hostil ao jogador: funcao do proprio jogo OU presente na lista de hostis do jogador.
			const bool hostile = listed || CALL_MEMBER_FN(actor, IsHostileToActor)(player);

			if(dbg && dbgLines < 8)
			{
				++dbgLines;
				_MESSAGE("probe-actor: formID %08X dist %.0f life=%u hostile=%d listed=%d", actor->formID, dist, (unsigned)life, hostile ? 1 : 0, listed ? 1 : 0);
			}

			if(life != 0) continue;									// so vivos
			if(!hostile) continue;
			if(g_cfg.requireCombat && !actor->IsInCombat()) continue;

			const float yaw = atan2f(dx, dy);
			const float off = fabsf(WrapPi(yaw - facing));

			float score;
			if(cone)
			{
				if(off > halfCone) continue;
				// Mais perto e mais a frente = melhor. O desvio angular pesa como distancia extra.
				score = dist * (1.0f + off / (kPi * 0.5f));
			}
			else
			{
				score = dist;											// circulo: o mais proximo, em qualquer direcao
			}

			if(score < bestScore)
			{
				bestScore = score;
				*outYaw = yaw;
				*outId = actor->formID;
				found = true;
			}
		}
		return found;
	}

	// ------------------------------------------------------------------ camera (experimental)
	// No log, em terceira pessoa (estado 8 ou 9), a guinada da camera esta em 0x5C e 0x60 do estado
	// da camera (acompanha a rotacao do jogador quando ha arma na mao).
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
	volatile LONG	g_active	= 0;
	volatile DWORD	g_endTick	= 0;
	volatile float	g_targetYaw	= 0.0f;

	DWORD WINAPI TurnThread(LPVOID)
	{
		DWORD last = GetTickCount();
		bool  session = false;			// ha um giro em curso (para o modo de camera 2)
		float camYaw0 = 0.0f;
		for(;;)
		{
			Sleep((DWORD)g_cfg.updateMs);
			const DWORD now = GetTickCount();
			float dt = (now - last) / 1000.0f;
			last = now;
			if(dt > 0.1f) dt = 0.1f;

			if(!g_active) { session = false; continue; }
			if((LONG)(now - g_endTick) >= 0) { g_active = 0; session = false; continue; }

			PlayerCharacter * player = Engine::Player();
			if(!player) continue;

			UInt8 * camState = g_cfg.cameraMode ? ThirdPersonStatePtr() : NULL;
			if(camState && !session) { camYaw0 = *(float *)(camState + 0x5C); session = true; }

			NiPoint3 & rot = Engine::Rotation(player);
			const float delta = WrapPi(g_targetYaw - rot.z);
			const float maxStep = g_cfg.turnSpeedDeg * kPi / 180.0f * dt;

			// Suavizacao: com Smoothing > 0 o passo e proporcional a distancia que falta
			// (movimento que abranda ao chegar), sempre limitado pela velocidade maxima.
			float want = delta;
			if(g_cfg.smoothing > 0.0f) want = delta * (1.0f - expf(-g_cfg.smoothing * dt));

			float step = want;
			if(step >  maxStep) step =  maxStep;
			if(step < -maxStep) step = -maxStep;
			rot.z = WrapPi(rot.z + step);

			// Camera: 1 = tira a camera o mesmo giro que demos ao corpo; 2 = repoe a camera onde estava.
			if(camState)
			{
				float * y1 = (float *)(camState + 0x5C);
				float * y2 = (float *)(camState + 0x60);
				if(g_cfg.cameraMode == 1)		{ *y1 -= step; *y2 -= step; }
				else if(g_cfg.cameraMode == 2)	{ *y1 = camYaw0; *y2 = camYaw0; }
			}
		}
	}

	// ------------------------------------------------------------------ escolha + arranque
	UInt32 g_lastTargetId = 0;

	// Lock-on (Simple Lock-On / Coeus): o plugin nao sabe se o lock esta ativo, entao acompanha a
	// tecla configurada em LockOnKey: cada toque liga/desliga. Enquanto "travado", fica em pausa.
	bool	g_lockOn = false;
	DWORD	g_lockNoEnemySince = 0;

	// Escolhe o alvo e manda a thread vira-lo durante 'windowMs'.
	bool AcquireAndTurn(int windowMs)
	{
		if(g_lockOn) return false;

		PlayerCharacter * player = Engine::Player();
		if(!player) return false;

		float yaw = 0.0f;
		UInt32 id = 0;
		if(!FindTarget(player, &yaw, &id)) return false;

		if(g_cfg.verbose || id != g_lastTargetId)
			_MESSAGE("target: formID %08X, turning toward yaw %.0f deg%s", id, yaw * 180.0f / kPi, g_cfg.logOnly ? " (LogOnly)" : "");
		g_lastTargetId = id;

		if(g_cfg.logOnly) return true;

		g_targetYaw	= yaw;
		g_endTick	= GetTickCount() + (DWORD)windowMs;
		g_active	= 1;
		return true;
	}

	// ------------------------------------------------------------------ diagnostico (so leitura)
	// Com Probe=1 escreve no log, de meio em meio segundo: estado da camera, rotacao da camera e do
	// jogador, alvo do jogo e alvo da mira. Serve para descobrir (a) qual valor da camera muda quando
	// se roda a camera e (b) qual valor muda quando o Simple Lock-On trava um alvo.
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

		UInt32 crosshair = 0;
		CrosshairRefHandleHolder * ch = CrosshairRefHandleHolder::GetSingleton();
		if(ch) crosshair = ch->CrosshairRefHandle();

		const NiPoint3 rot = Engine::Rotation(player);
		TESCameraState * st = cam->cameraState;

		_MESSAGE("probe: state=%u camRotZ=%.3f camRotX=%.3f zoom=%.2f | player rotZ=%.3f rotX=%.3f targetHandle=%08X crosshair=%08X",
				 st ? (unsigned)st->stateId : 0xFFFFu, cam->rotZ, cam->rotX, cam->zoom,
				 rot.z, rot.x, (unsigned)player->targetHandle, (unsigned)crosshair);

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
	bool	g_holdLeft = false, g_holdRight = false;
	DWORD	g_lastHoldEvent = 0;
	DWORD	g_lastScan = 0;

	void HoldTick()
	{
		const DWORD now = GetTickCount();
		if(now - g_lastHoldEvent > 250) return;					// botao deixou de chegar: considera solto
		if(now - g_lastScan < (DWORD)g_cfg.retargetMs) return;
		g_lastScan = now;

		const int window = (2 * g_cfg.retargetMs > 120) ? 2 * g_cfg.retargetMs : 120;
		AcquireAndTurn(window);
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

			// O lock-on desliga-se sozinho quando ja nao ha inimigos em combate (2 s seguidos).
			if(g_lockOn)
			{
				PlayerCharacter * p = Engine::Player();
				UInt32 tmp[8];
				if(p && CollectHostileIds(p, tmp, 8) == 0)
				{
					if(!g_lockNoEnemySince) g_lockNoEnemySince = GetTickCount();
					else if(GetTickCount() - g_lockNoEnemySince > 2000) { g_lockOn = false; g_lockNoEnemySince = 0; _MESSAGE("lock-on assumed OFF (no enemies in combat)"); }
				}
				else g_lockNoEnemySince = 0;
			}

			bool holdBtnSeen = false;
			for(InputEvent * e = *events; e; e = e->next)
			{
				if(e->eventType != InputEvent::kEventType_Button) continue;

				ButtonEvent * button = (ButtonEvent *)e;
				const float value = *(float *)&button->flags;
				const bool held = (value != 0.0f);
				const bool pressedNow = held && (button->timer == 0.0f);

				// Diagnostico: regista cada toque de tecla (para descobrir o codigo da tecla do lock-on).
				if(g_cfg.probe && pressedNow)
				{
					const BSFixedString * c = button->GetControlID();
					_MESSAGE("probe-key: device=%u code=%u control=%s", (unsigned)e->deviceType, (unsigned)button->keyMask, (c && c->data) ? c->data : "-");
				}

				// Tecla do lock-on: cada toque liga/desliga o estado "travado".
				if(g_cfg.lockOnKey && pressedNow && e->deviceType == kDeviceType_Keyboard && button->keyMask == (UInt32)g_cfg.lockOnKey)
				{
					g_lockOn = !g_lockOn;
					g_lockNoEnemySince = 0;
					if(g_lockOn) g_active = 0;
					_MESSAGE("lock-on key: assist %s", g_lockOn ? "PAUSED (lock-on assumed ON)" : "RESUMED (lock-on assumed OFF)");
				}

				const BSFixedString * control = button->GetControlID();
				if(!control) continue;

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
					AcquireAndTurn(g_cfg.holdMs);
			}

			// Bloqueio continuo: enquanto o botao esta seguro, re-escolhe o alvo de tempos a tempos.
			const bool holding = ((g_cfg.holdMode & 1) && g_holdLeft) || ((g_cfg.holdMode & 2) && g_holdRight);
			if(holding && (holdBtnSeen || GetTickCount() - g_lastHoldEvent <= 250)) HoldTick();

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
	_MESSAGE("running");
	return true;
}

};
