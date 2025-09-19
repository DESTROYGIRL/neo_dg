#include "cbase.h"
#include "weapon_smac.h"

#ifdef GAME_DLL
#include "explode.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

IMPLEMENT_NETWORKCLASS_ALIASED(WeaponSMAC, DT_WeaponSMAC)

BEGIN_NETWORK_TABLE(CWeaponSMAC, DT_WeaponSMAC)
#ifdef CLIENT_DLL
	RecvPropBool(RECVINFO(m_bExplosiveMode)),
#else
	SendPropBool(SENDINFO(m_bExplosiveMode)),
#endif
END_NETWORK_TABLE()

#ifdef CLIENT_DLL
BEGIN_PREDICTION_DATA(CWeaponSMAC)
	DEFINE_PRED_FIELD(m_bExplosiveMode, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE),
END_PREDICTION_DATA()
#endif

NEO_IMPLEMENT_ACTTABLE(CWeaponSMAC)

LINK_ENTITY_TO_CLASS(weapon_smac, CWeaponSMAC);

PRECACHE_WEAPON_REGISTER(weapon_smac);

#define FUSE_TIME 3.0f

CWeaponSMAC::CWeaponSMAC()
{
	m_bExplosiveMode = false;

	m_flSoonestAttack = gpGlobals->curtime;
	m_flAccuracyPenalty = 0;

	m_nNumShotsFired = 0;

	m_weaponSeeds = {
		"smacpx",
		"smacpy",
		"smacrx",
		"smacry",
	};
}

void CWeaponSMAC::Equip(CBaseCombatCharacter *pNewOwner)
{
#ifdef GAME_DLL
	m_pLastOwner = pNewOwner;
#endif
	BaseClass::Equip(pNewOwner);
}

void CWeaponSMAC::ItemPostFrame(void)
{
	if (!m_bExplosiveMode)
	{
		BaseClass::ItemPostFrame();
	}
	else
	{
		auto pOwner = ToNEOPlayer(GetOwner());

		static bool startLowIdle = true;
		if (startLowIdle && pOwner && pOwner->m_flNextAttack < gpGlobals->curtime)
		{
			startLowIdle = false;
			SendWeaponAnim(ACT_VM_IDLE_LOWERED);
		}

		if (GetActivity() == ACT_VM_THROW)
		{
			if (m_flNextPrimaryAttack < gpGlobals->curtime)
			{
				pOwner->Weapon_Drop(this);
				RemoveSolidFlags(FSOLID_TRIGGER);
				SetThink(&CWeaponSMAC::Explode);
				SetNextThink(gpGlobals->curtime + FUSE_TIME);
			}
		}

		if (pOwner->m_nButtons & IN_ATTACK && m_flNextPrimaryAttack <= gpGlobals->curtime)
		{
			PrimaryAttack();
#ifdef CLIENT_DLL
			pOwner->SetFiredWeapon(true);
#endif
		}
	}
}

void CWeaponSMAC::PrimaryAttack(void)
{
	if (!m_bExplosiveMode)
	{
		BaseClass::PrimaryAttack();
	}
	else
	{
		SendWeaponAnim(ACT_VM_THROW);

		ToNEOPlayer(GetOwner())->DoAnimationEvent(PLAYERANIMEVENT_ATTACK_GRENADE);

		m_flNextPrimaryAttack = gpGlobals->curtime + SequenceDuration();
	}
}

void CWeaponSMAC::SecondaryAttack(void)
{
	if (ShootingIsPrevented() || m_bExplosiveMode)
	{
		return;
	}

	//WeaponSound(SPECIAL1);
	SendWeaponAnim(ACT_VM_CHANGEMODE);

	auto pOwner = ToNEOPlayer(GetOwner());
	//pOwner->DoAnimationEvent(PLAYERANIMEVENT_RELOAD);
	pOwner->m_flNextAttack = gpGlobals->curtime + SequenceDuration();

	m_bExplosiveMode = true;
}

bool CWeaponSMAC::CanBePickedUpByClass(int classId)
{
	return classId != NEO_CLASS_VIP;
}

void CWeaponSMAC::Explode()
{
	SetMoveType(MOVETYPE_NONE);
	SetModelName(NULL_STRING);
	AddSolidFlags(FSOLID_NOT_SOLID);
	SetSolid(SOLID_NONE);
	m_takedamage = DAMAGE_NO;

	EmitSound("BaseGrenade.Explode");
#ifdef GAME_DLL
	ExplosionCreate(WorldSpaceCenter(), vec3_angle, m_pLastOwner, 128, 128, NULL, 12, m_pLastOwner);
#endif

	SUB_Remove();
}