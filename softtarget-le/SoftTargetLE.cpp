// SoftTarget LE - golpes auto-direcionados (sem travar a mira), estilo Witcher 3.
//
// Ao carregar no botao de ataque, o plugin procura o inimigo mais proximo dentro de um cone a
// frente do jogador e vira o personagem para ele durante o golpe. Nao usa Papyrus: so reage ao
// botao, e uma pequena thread (so ativa durante o golpe) faz a rotacao suave.
//
// ESTADO: versao 0.1, nunca testada no jogo. As partes menos certas estao marcadas com "VERIFICAR".
// Licenca GPL-3.0. A base de compilacao e os enderecos vem do FreeClimb LE (GPL-3.0).

#include "skse/PluginAPI.h"
#include "skse/skse_version.h"
#include "skse/GameRTTI.h"
#include "Log.h"
#include "Engine.h"

#include <shlobj.h>
#include <math.h>

#define PLUGIN_NAME			"SoftTargetLE"
#define PLUGIN_VERSION_STR	"0.1.0 (primeira versao completa, por testar)"
#define PLUGIN_LOG_FILE		"\\My Games\\Skyrim\\SKSE\\SoftTargetLE.log"
#define PLUGIN_INI_FILE		"Data\\SKSE\\Plugins\\SoftTargetLE.ini"

static PluginHandle				g_pluginHandle	= kPluginHandle_Invalid;
static SKSEMessagingInterface *	g_messaging		= NULL;

namespace
{
	const float kPi = 3.14159265f;

	// ------------------------------------------------------------------ afinacoes (.ini)
	struct Config
	{
		float	range;			// alcance maximo, em unidades do jogo (140 = ~2 m)
		float	coneDeg;		// angulo TOTAL do cone a frente do jogador, em graus
		float	turnSpeedDeg;	// velocidade de rotacao, graus por segundo
		int		holdMs;			// quanto tempo o plugin segura a rotacao por golpe
		int		enabled;
		int		logOnly;		// 1 = so escreve no log, nao vira o personagem
	};
	Config g_cfg;

	float ReadFloat(const char * key, float def)
	{
		char def_s[32], buf[32];
		_snprintf_s(def_s, sizeof(def_s), _TRUNCATE, "%f", def);
		GetPrivateProfileStringA("SoftTarget", key, def_s, buf, sizeof(buf), PLUGIN_INI_FILE);
		return (float)atof(buf);
	}

	void LoadConfig()
	{
		g_cfg.range			= ReadFloat("Range", 450.0f);
		g_cfg.coneDeg		= ReadFloat("ConeDegrees", 140.0f);
		g_cfg.turnSpeedDeg	= ReadFloat("TurnSpeedDegPerSec", 900.0f);
		g_cfg.holdMs		= (int)ReadFloat("HoldMilliseconds", 400.0f);
		g_cfg.enabled		= (int)ReadFloat("Enabled", 1.0f);
		g_cfg.logOnly		= (int)ReadFloat("LogOnly", 0.0f);
		_MESSAGE("config: range=%.0f cone=%.0f turn=%.0f deg/s hold=%d ms enabled=%d logOnly=%d",
				 g_cfg.range, g_cfg.coneDeg, g_cfg.turnSpeedDeg, g_cfg.holdMs, g_cfg.enabled, g_cfg.logOnly);
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
	// Devolve true se encontrou um alvo e escreve a guinada desejada em *outYaw.
	bool FindTarget(Actor * player, float * outYaw)
	{
		TESObjectCELL * cell = Engine::ParentCell(player);
		if(!cell) return false;

		const NiPoint3 me		= Engine::Position(player);
		const float facing		= Engine::Rotation(player).z;
		const float halfCone	= (g_cfg.coneDeg * 0.5f) * kPi / 180.0f;

		float bestScore = 1e30f;
		bool found = false;

		// VERIFICAR: a lista de referencias da celula. Nas fontes do SKSE 1.7.3 e esperado
		// cell->refData.maxSlots e cell->refData.refArray[i].ref. Se a compilacao reclamar
		// destes nomes, e aqui que se corrige.
		const UInt32 count = cell->refData.maxSlots;
		for(UInt32 i = 0; i < count; ++i)
		{
			TESObjectREFR * ref = cell->refData.refArray[i].ref;
			if(!ref || ref == (TESObjectREFR *)player) continue;
			if(ref->formType != kFormType_Character) continue;

			Actor * actor = DYNAMIC_CAST(ref, TESObjectREFR, Actor);
			if(!actor) continue;
			if(Engine::LifeState(actor) != 0) continue;			// so vivos

			// VERIFICAR: ainda nao ha teste de "hostil". Esta versao considera qualquer
			// ator vivo. Para o teste, use LogOnly=1 e veja quem entra na lista.
			const NiPoint3 pos = Engine::Position(actor);
			const float dx = pos.x - me.x;
			const float dy = pos.y - me.y;
			const float dz = pos.z - me.z;
			const float dist = sqrtf(dx * dx + dy * dy);
			if(dist > g_cfg.range || fabsf(dz) > 250.0f) continue;

			const float yaw = atan2f(dx, dy);
			const float off = fabsf(WrapPi(yaw - facing));
			if(off > halfCone) continue;

			// Mais perto e mais a frente = melhor. O desvio angular pesa como distancia extra.
			const float score = dist * (1.0f + off / (kPi * 0.5f));
			if(score < bestScore)
			{
				bestScore = score;
				*outYaw = yaw;
				found = true;
				_MESSAGE("target candidate: formID %08X dist %.0f off %.0f deg", actor->formID, dist, off * 180.0f / kPi);
			}
		}
		return found;
	}

	// ------------------------------------------------------------------ rotacao suave (thread)
	volatile LONG	g_active	= 0;
	volatile DWORD	g_endTick	= 0;
	volatile float	g_targetYaw	= 0.0f;

	DWORD WINAPI TurnThread(LPVOID)
	{
		DWORD last = GetTickCount();
		for(;;)
		{
			Sleep(8);
			const DWORD now = GetTickCount();
			const float dt = (now - last) / 1000.0f;
			last = now;

			if(!g_active) continue;
			if((LONG)(now - g_endTick) >= 0) { g_active = 0; continue; }

			PlayerCharacter * player = Engine::Player();
			if(!player) continue;

			NiPoint3 & rot = Engine::Rotation(player);
			const float delta = WrapPi(g_targetYaw - rot.z);
			const float maxStep = g_cfg.turnSpeedDeg * kPi / 180.0f * dt;
			const float step = (fabsf(delta) <= maxStep) ? delta : (delta > 0 ? maxStep : -maxStep);
			rot.z = WrapPi(rot.z + step);
		}
	}

	// ------------------------------------------------------------------ botao de ataque
	void OnAttackPressed()
	{
		if(!g_cfg.enabled) return;

		PlayerCharacter * player = Engine::Player();
		if(!player) return;

		float yaw = 0.0f;
		if(!FindTarget(player, &yaw)) return;

		_MESSAGE("attack: turning toward yaw %.0f deg%s", yaw * 180.0f / kPi, g_cfg.logOnly ? " (LogOnly)" : "");
		if(g_cfg.logOnly) return;

		g_targetYaw	= yaw;
		g_endTick	= GetTickCount() + (DWORD)g_cfg.holdMs;
		g_active	= 1;
	}

	class InputSink : public BSTEventSink<InputEvent>
	{
	public:
		virtual EventResult ReceiveEvent(InputEvent ** events, InputEventDispatcher * dispatcher)
		{
			InputStringHolder * strings = Engine::UserEvents();
			if(!events || !*events || !strings) return kEvent_Continue;

			for(InputEvent * e = *events; e; e = e->next)
			{
				if(e->eventType != InputEvent::kEventType_Button) continue;

				ButtonEvent * button = (ButtonEvent *)e;
				const BSFixedString * control = button->GetControlID();
				if(!control) continue;

				const float value = *(float *)&button->flags;
				const bool pressedNow = (value != 0.0f) && (button->timer == 0.0f);
				if(!pressedNow) continue;

				// VERIFICAR: nomes dos campos de InputStringHolder para os botoes de ataque.
				if(*control == strings->rightAttack || *control == strings->leftAttack)
					OnAttackPressed();
			}
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
