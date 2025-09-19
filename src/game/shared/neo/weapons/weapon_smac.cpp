#include "cbase.h"
#include "weapon_smac.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

IMPLEMENT_NETWORKCLASS_ALIASED(WeaponSMAC, DT_WeaponSMAC)

BEGIN_NETWORK_TABLE(CWeaponSMAC, DT_WeaponSMAC)
#ifdef CLIENT_DLL
	RecvPropBool(RECVINFO(m_bExplosiveMode))
#else
	SendPropBool(SENDINFO(m_bExplosiveMode))
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

void CWeaponSMAC::ItemPostFrame(void)
{
	BaseClass::ItemPostFrame();
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

		auto pOwner = ToNEOPlayer(GetOwner());
		pOwner->DoAnimationEvent(PLAYERANIMEVENT_ATTACK_GRENADE);

		m_flNextPrimaryAttack = gpGlobals->curtime + SequenceDuration();

		Drop(vec3_origin);
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
	pOwner->DoAnimationEvent(PLAYERANIMEVENT_RELOAD);

	pOwner->m_flNextAttack = gpGlobals->curtime + SequenceDuration();

	m_bExplosiveMode = true;
}

bool CWeaponSMAC::CanBePickedUpByClass(int classId)
{
	return classId == NEO_CLASS_VIP;
}