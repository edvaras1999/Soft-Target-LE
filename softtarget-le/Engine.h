// Engine.h - os poucos enderecos e deslocamentos da LE 1.9.32.0 que o SoftTarget usa.
// Todos foram CONFIRMADOS no projeto FreeClimb LE (GPL-3.0, Eelta e colaboradores), que ja
// funciona nesse executavel. Nao acrescentar endereco novo sem confirmar no jogo.
#pragma once

#include "skse/GameForms.h"
#include "skse/GameReferences.h"
#include "skse/GameInput.h"
#include "skse/NiTypes.h"

namespace Engine
{
	inline PlayerCharacter *	Player()		{ return *(PlayerCharacter **)0x01310588; }	// g_thePlayer
	inline InputStringHolder *	UserEvents()	{ return *(InputStringHolder **)0x01B2E388; }

	// TESObjectREFR: rot 0x28, pos 0x34, parentCell 0x40.
	inline NiPoint3 &		Position(TESObjectREFR * ref)	{ return *(NiPoint3 *)((UInt8 *)ref + 0x34); }
	inline NiPoint3 &		Rotation(TESObjectREFR * ref)	{ return *(NiPoint3 *)((UInt8 *)ref + 0x28); }
	inline TESObjectCELL *	ParentCell(TESObjectREFR * ref)	{ return *(TESObjectCELL **)((UInt8 *)ref + 0x40); }

	// Actor: primeira palavra de flags em 0x68; estado de vida nos bits 21..24 (0 = vivo).
	inline UInt32 &	ActorFlags1(Actor * actor)	{ return *(UInt32 *)((UInt8 *)actor + 0x68); }
	inline UInt32	LifeState(Actor * actor)	{ return (ActorFlags1(actor) >> 21) & 0x0F; }
}
