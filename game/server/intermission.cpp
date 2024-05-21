//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "entitylist.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//=========================================================
// Multiplayer intermission spots.
//=========================================================
class CInfoIntermission:public CPointEntity
{
public:
	DECLARE_CLASS( CInfoIntermission, CPointEntity );

	void Spawn( void );
	void Think( void );
};

void CInfoIntermission::Spawn( void )
{
	GetEngineObject()->SetSolid( SOLID_NONE );
	AddEffects( EF_NODRAW );
	GetEngineObject()->SetLocalAngles( vec3_angle );
	SetNextThink( gpGlobals->curtime + 2 );// let targets spawn !
}

void CInfoIntermission::Think ( void )
{
	CBaseEntity *pTarget;

	// find my target
	pTarget = gEntList.FindEntityByName( NULL, m_target );

	if ( pTarget )
	{
		Vector dir = pTarget->GetEngineObject()->GetLocalOrigin() - GetEngineObject()->GetLocalOrigin();
		VectorNormalize( dir );
		QAngle angles;
		VectorAngles( dir, angles );
		GetEngineObject()->SetLocalAngles( angles );
	}
}

LINK_ENTITY_TO_CLASS( info_intermission, CInfoIntermission );
