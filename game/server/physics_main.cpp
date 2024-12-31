//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Physics simulation for non-havok/ipion objects
//
// $NoKeywords: $
//=============================================================================//


#include "cbase.h"


#include "player.h"
#include "ai_basenpc.h"
#include "gamerules.h"
#include "mempool.h"
#include "engine/IEngineSound.h"
#include "datacache/imdlcache.h"
#include "ispatialpartition.h"
#include "tier0/vprof.h"
#include "movevars_shared.h"
#include "hierarchy.h"
#include "trains.h"
#include "vphysicsupdateai.h"
#include "tier0/vcrmode.h"
#include "pushentity.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef _XBOX
ConVar vprof_think_limit( "vprof_think_limit", "0" );
#endif

ConVar vprof_scope_entity_thinks( "vprof_scope_entity_thinks", "0" );
ConVar vprof_scope_entity_gamephys( "vprof_scope_entity_gamephys", "0" );

ConVar	npc_vphysics	( "npc_vphysics","0");
//-----------------------------------------------------------------------------
// helper method for trace hull as used by physics...
//-----------------------------------------------------------------------------
static void Physics_TraceEntity( CBaseEntity* pBaseEntity, const Vector &vecAbsStart,
	const Vector &vecAbsEnd, unsigned int mask, trace_t *ptr )
{
	// FIXME: I really am not sure the best way of doing this
	// The TraceHull code below for shots will make sure the object passes
	// through shields which do not block that damage type. It will also 
	// send messages to the shields that they've been hit.
	if (pBaseEntity->GetDamageType() != DMG_GENERIC)
	{
		GameRules()->WeaponTraceEntity( pBaseEntity, vecAbsStart, vecAbsEnd, mask, ptr );
	}
	else
	{
		EntityList()->GetEngineWorld()->TraceEntity( pBaseEntity->GetEngineObject(), vecAbsStart, vecAbsEnd, mask, ptr);
	}
}


//-----------------------------------------------------------------------------
// Purpose: Does not change the entities velocity at all
// Input  : push - 
// Output : trace_t
//-----------------------------------------------------------------------------
static void PhysicsCheckSweep( CBaseEntity *pEntity, const Vector& vecAbsStart, const Vector &vecAbsDelta, trace_t *pTrace )
{
	unsigned int mask = pEntity->PhysicsSolidMaskForEntity();

	Vector vecAbsEnd;
	VectorAdd( vecAbsStart, vecAbsDelta, vecAbsEnd );

	// Set collision type
	if ( !pEntity->GetEngineObject()->IsSolid() || pEntity->GetEngineObject()->IsSolidFlagSet( FSOLID_VOLUME_CONTENTS) )
	{
		if ( pEntity->GetEngineObject()->GetMoveParent() )
		{
			UTIL_ClearTrace( *pTrace );
			return;
		}

		// don't collide with monsters
		mask &= ~CONTENTS_MONSTER;
	}

	Physics_TraceEntity( pEntity, vecAbsStart, vecAbsEnd, mask, pTrace );
}

CPhysicsPushedEntities s_PushedEntities;
#ifndef TF_DLL
CPhysicsPushedEntities *g_pPushedEntities = &s_PushedEntities;
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CPhysicsPushedEntities::CPhysicsPushedEntities( void ) : m_rgPusher(8, 8), m_rgMoved(32, 32)
{
	m_flMoveTime = -1.0f;
}

//-----------------------------------------------------------------------------
// Purpose: Store off entity and copy original origin to temporary array
//-----------------------------------------------------------------------------
void CPhysicsPushedEntities::AddEntity( CBaseEntity *ent )
{
	int i = m_rgMoved.AddToTail();
	m_rgMoved[i].m_pEntity = ent;
	m_rgMoved[i].m_vecStartAbsOrigin = ent->GetEngineObject()->GetAbsOrigin();
}


//-----------------------------------------------------------------------------
// Unlink + relink the pusher list so we can actually do the push
//-----------------------------------------------------------------------------
void CPhysicsPushedEntities::UnlinkPusherList( int *pPusherHandles )
{
	for ( int i = m_rgPusher.Count(); --i >= 0; )
	{
		pPusherHandles[i] = partition->HideElement( m_rgPusher[i].m_pEntity->GetEngineObject()->GetPartitionHandle() );
	}
}

void CPhysicsPushedEntities::RelinkPusherList( int *pPusherHandles )
{
	for ( int i = m_rgPusher.Count(); --i >= 0; )
	{
		partition->UnhideElement( m_rgPusher[i].m_pEntity->GetEngineObject()->GetPartitionHandle(), pPusherHandles[i] );
	}
}


//-----------------------------------------------------------------------------
// Compute the direction to move the rotation blocker
//-----------------------------------------------------------------------------
void CPhysicsPushedEntities::ComputeRotationalPushDirection( CBaseEntity *pBlocker, const RotatingPushMove_t &rotPushMove, Vector *pMove, CBaseEntity *pRoot )
{
	// calculate destination position
	// "start" is relative to the *root* pusher, world orientation
	Vector start = pBlocker->GetEngineObject()->GetCollisionOrigin();
	if ( pRoot->GetEngineObject()->GetSolid() == SOLID_VPHYSICS )
	{
		// HACKHACK: Use move dir to guess which corner of the box determines contact and rotate the box so
		// that corner remains in the same local position.
		// BUGBUG: This will break, but not as badly as the previous solution!!!
		Vector vecAbsMins, vecAbsMaxs;
		pBlocker->GetEngineObject()->WorldSpaceAABB( &vecAbsMins, &vecAbsMaxs );
		start.x = (pMove->x < 0) ? vecAbsMaxs.x : vecAbsMins.x;
		start.y = (pMove->y < 0) ? vecAbsMaxs.y : vecAbsMins.y;
		start.z = (pMove->z < 0) ? vecAbsMaxs.z : vecAbsMins.z;
		
		CBasePlayer *pPlayer = ToBasePlayer(pBlocker);
		if ( pPlayer )
		{
			// notify the player physics code so it can use vphysics to keep players from getting stuck
			pPlayer->SetPhysicsFlag( PFLAG_GAMEPHYSICS_ROTPUSH, true );
		}
	}

	// org is pusher local coordinate of start
	Vector local;
	// transform starting point into local space
	VectorITransform( start, rotPushMove.startLocalToWorld, local );
	// rotate local org into world space at end of rotation
	Vector end;
	VectorTransform( local, rotPushMove.endLocalToWorld, end );

	// move is the difference (in world space) that the move will push this object
	VectorSubtract( end, start, *pMove );
}

class CTraceFilterPushFinal : public CTraceFilterSimple
{
	DECLARE_CLASS( CTraceFilterPushFinal, CTraceFilterSimple );

public:
	CTraceFilterPushFinal( CBaseEntity *pEntity, int nCollisionGroup ) 
		: CTraceFilterSimple( pEntity, nCollisionGroup )
	{
	
	}

	bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask )
	{
		Assert( dynamic_cast<CBaseEntity*>(pHandleEntity) );
		CBaseEntity *pTestEntity = static_cast<CBaseEntity*>(pHandleEntity);

		// UNDONE: This should really filter to just the pushing entities
		if ( pTestEntity->GetEngineObject()->GetMoveType() == MOVETYPE_VPHYSICS &&
			pTestEntity->GetEngineObject()->VPhysicsGetObject() && pTestEntity->GetEngineObject()->VPhysicsGetObject()->IsMoveable() )
			return false;

		return BaseClass::ShouldHitEntity( pHandleEntity, contentsMask );
	}

};

bool CPhysicsPushedEntities::IsPushedPositionValid( CBaseEntity *pBlocker )
{
	CTraceFilterPushFinal pushFilter(pBlocker, pBlocker->GetEngineObject()->GetCollisionGroup() );

	trace_t trace;
	EntityList()->GetEngineWorld()->TraceEntity( pBlocker->GetEngineObject(), pBlocker->GetEngineObject()->GetAbsOrigin(), pBlocker->GetEngineObject()->GetAbsOrigin(), pBlocker->PhysicsSolidMaskForEntity(), &pushFilter, &trace);

	return !trace.startsolid;
}

//-----------------------------------------------------------------------------
// Speculatively checks to see if all entities in this list can be pushed
//-----------------------------------------------------------------------------
bool CPhysicsPushedEntities::SpeculativelyCheckPush( PhysicsPushedInfo_t &info, const Vector &vecAbsPush, bool bRotationalPush )
{
	CBaseEntity *pBlocker = info.m_pEntity;

	// See if it's possible to move the entity, but disable all pushers in the hierarchy first
	int *pPusherHandles = (int*)stackalloc( m_rgPusher.Count() * sizeof(int) );
	UnlinkPusherList( pPusherHandles );
	CTraceFilterPushMove pushFilter(pBlocker, pBlocker->GetEngineObject()->GetCollisionGroup() );

	Vector pushDestPosition = pBlocker->GetEngineObject()->GetAbsOrigin() + vecAbsPush;
	EntityList()->GetEngineWorld()->TraceEntity( pBlocker->GetEngineObject(), pBlocker->GetEngineObject()->GetAbsOrigin(), pushDestPosition,
		pBlocker->PhysicsSolidMaskForEntity(), &pushFilter, &info.m_Trace );

	RelinkPusherList(pPusherHandles);
	info.m_bPusherIsGround = false;
	if ( pBlocker->GetEngineObject()->GetGroundEntity() && pBlocker->GetEngineObject()->GetGroundEntity()->GetRootMoveParent()->GetOuter() == m_rgPusher[0].m_pEntity)
	{
		info.m_bPusherIsGround = true;
	}

	bool bIsUnblockable = (m_bIsUnblockableByPlayer && (pBlocker->IsPlayer() || pBlocker->MyNPCPointer())) ? true : false;
	if ( bIsUnblockable )
	{
		pBlocker->GetEngineObject()->SetAbsOrigin( pushDestPosition );
	}
	else
	{
		// Move the blocker into its new position
		if ( info.m_Trace.fraction )
		{
			pBlocker->GetEngineObject()->SetAbsOrigin( info.m_Trace.endpos );
		}

		// We're not blocked if the blocker is point-sized or non-solid
		if ( pBlocker->GetEngineObject()->IsPointSized() || !pBlocker->GetEngineObject()->IsSolid() ||
			pBlocker->GetEngineObject()->IsSolidFlagSet( FSOLID_VOLUME_CONTENTS ) )
		{
			return true;
		}

		if ( (!bRotationalPush) && (info.m_Trace.fraction == 1.0) )
		{
			//Assert( pBlocker->PhysicsTestEntityPosition() == false );
			if ( !IsPushedPositionValid(pBlocker) )
			{
				Warning("Interpenetrating entities! (%s and %s)\n",
					pBlocker->GetClassname(), m_rgPusher[0].m_pEntity->GetClassname() );
			}

			return true;
		}
	}

	// Check to see if we're still blocked by the pushers
	// FIXME: If the trace fraction == 0 can we early out also?
	info.m_bBlocked = !IsPushedPositionValid(pBlocker);

	if ( !info.m_bBlocked )
		return true;

	// if the player is blocking the train try nudging him around to fix accumulated error
	if ( bIsUnblockable )
	{
		Vector org = pBlocker->GetEngineObject()->GetAbsOrigin();
		for ( int checkCount = 0; checkCount < 4; checkCount++ )
		{
			Vector move;
			MatrixGetColumn( m_rgPusher[0].m_pEntity->GetEngineObject()->EntityToWorldTransform(), checkCount>>1, move );
			
			// alternate movements 1/2" in each direction
			float factor = ( checkCount & 1 ) ? -0.5f : 0.5f;
			pBlocker->GetEngineObject()->SetAbsOrigin( org + move * factor );
			info.m_bBlocked = !IsPushedPositionValid(pBlocker);
			if ( !info.m_bBlocked )
				return true;
		}
		pBlocker->GetEngineObject()->SetAbsOrigin( pushDestPosition );

#ifndef TF_DLL
		DevMsg(1, "Ignoring player blocking train!\n");
#endif
		return true;
	}
	return false;
}


//-----------------------------------------------------------------------------
// Speculatively checks to see if all entities in this list can be pushed
//-----------------------------------------------------------------------------
bool CPhysicsPushedEntities::SpeculativelyCheckRotPush( const RotatingPushMove_t &rotPushMove, CBaseEntity *pRoot )
{
	Vector vecAbsPush;
	m_nBlocker = -1;
	for (int i = m_rgMoved.Count(); --i >= 0; )
	{
		ComputeRotationalPushDirection( m_rgMoved[i].m_pEntity, rotPushMove, &vecAbsPush, pRoot );
		if (!SpeculativelyCheckPush( m_rgMoved[i], vecAbsPush, true ))
		{
			m_nBlocker = i;
			return false;
		}
	}

	return true;
}


//-----------------------------------------------------------------------------
// Speculatively checks to see if all entities in this list can be pushed
//-----------------------------------------------------------------------------
bool CPhysicsPushedEntities::SpeculativelyCheckLinearPush( const Vector &vecAbsPush )
{
	m_nBlocker = -1;
	for (int i = m_rgMoved.Count(); --i >= 0; )
	{
		if (!SpeculativelyCheckPush( m_rgMoved[i], vecAbsPush, false ))
		{
			m_nBlocker = i;
			return false;
		}
	}

	return true;
}


//-----------------------------------------------------------------------------
// Causes all entities in the list to touch triggers from their prev position
//-----------------------------------------------------------------------------
void CPhysicsPushedEntities::FinishPushers()
{
	// We succeeded! Now that we know the final location of all entities,
	// touch triggers + update physics objects + do other fixup
	for ( int i = m_rgPusher.Count(); --i >= 0; )
	{
		PhysicsPusherInfo_t &info = m_rgPusher[i];

		// Cause touch functions to be called
		// FIXME: Need to make moved entities not touch triggers until we know we're ok
		// FIXME: it'd be better for the engine to just have a touch method
		info.m_pEntity->GetEngineObject()->PhysicsTouchTriggers( &info.m_vecStartAbsOrigin );

		info.m_pEntity->UpdatePhysicsShadowToCurrentPosition( gpGlobals->frametime );
	}
}


//-----------------------------------------------------------------------------
// Causes all entities in the list to touch triggers from their prev position
//-----------------------------------------------------------------------------
void CPhysicsPushedEntities::FinishRotPushedEntity( CBaseEntity *pPushedEntity, const RotatingPushMove_t &rotPushMove )
{
	// Impart angular velocity of push onto pushed objects
	if ( pPushedEntity->IsPlayer() )
	{
		QAngle angVel = pPushedEntity->GetLocalAngularVelocity();
		angVel[1] = rotPushMove.amove[1];
		pPushedEntity->SetLocalAngularVelocity(angVel);

		// Look up associated client
		CBasePlayer *player = ( CBasePlayer * )pPushedEntity;
		player->pl.fixangle = FIXANGLE_RELATIVE;
		// Because we can run multiple ticks per server frame, accumulate a total offset here instead of straight
		//  setting it.  The engine will reset anglechange to 0 when the message is actually sent to the client
		player->pl.anglechange += rotPushMove.amove;
	}
	else
	{
		QAngle angles = pPushedEntity->GetEngineObject()->GetAbsAngles();
		
		// only rotate YAW with pushing.  Freely rotateable entities should either use VPHYSICS
		// or be set up as children
		angles.y += rotPushMove.amove.y;
		pPushedEntity->GetEngineObject()->SetAbsAngles( angles );
	}
}


//-----------------------------------------------------------------------------
// Causes all entities in the list to touch triggers from their prev position
//-----------------------------------------------------------------------------
void CPhysicsPushedEntities::FinishPush( bool bIsRotPush, const RotatingPushMove_t *pRotPushMove )
{
	FinishPushers();

	for ( int i = m_rgMoved.Count(); --i >= 0; )
	{
		PhysicsPushedInfo_t &info = m_rgMoved[i];
		CBaseEntity *pPushedEntity = info.m_pEntity;

		// Cause touch functions to be called
		// FIXME: it'd be better for the engine to just have a touch method
		info.m_pEntity->GetEngineObject()->PhysicsTouchTriggers( &info.m_vecStartAbsOrigin );
		info.m_pEntity->UpdatePhysicsShadowToCurrentPosition( gpGlobals->frametime );
		CAI_BaseNPC *pNPC = info.m_pEntity->MyNPCPointer();
		if ( info.m_bPusherIsGround && pNPC )
		{
			pNPC->NotifyPushMove();
		}


		// Register physics impacts...
		if (info.m_Trace.m_pEnt)
		{
			pPushedEntity->GetEngineObject()->PhysicsImpact(((CBaseEntity*)info.m_Trace.m_pEnt)->GetEngineObject(), info.m_Trace );
		}

		if (bIsRotPush)
		{
			FinishRotPushedEntity( pPushedEntity, *pRotPushMove );
		}
	}
}

// save initial state when beginning a push sequence
void CPhysicsPushedEntities::BeginPush( CBaseEntity *pRoot )
{
	m_rgMoved.RemoveAll();
	m_rgPusher.RemoveAll();

	m_rootPusherStartLocalOrigin = pRoot->GetEngineObject()->GetLocalOrigin();
	m_rootPusherStartLocalAngles = pRoot->GetEngineObject()->GetLocalAngles();
	m_rootPusherStartLocaltime = pRoot->GetLocalTime();
}

// store off a list of what has changed - so vphysicsUpdate can undo this if the object gets blocked
void CPhysicsPushedEntities::StoreMovedEntities( physicspushlist_t &list )
{
	list.localMoveTime = m_rootPusherStartLocaltime;
	list.localOrigin = m_rootPusherStartLocalOrigin;
	list.localAngles = m_rootPusherStartLocalAngles;
	list.pushedCount = CountMovedEntities();
	Assert(list.pushedCount < ARRAYSIZE(list.pushedEnts));
	if ( list.pushedCount > ARRAYSIZE(list.pushedEnts) )
	{
		list.pushedCount = ARRAYSIZE(list.pushedEnts);
	}
	for ( int i = 0; i < list.pushedCount; i++ )
	{
		list.pushedEnts[i] = m_rgMoved[i].m_pEntity;
		list.pushVec[i] = m_rgMoved[i].m_pEntity->GetEngineObject()->GetAbsOrigin() - m_rgMoved[i].m_vecStartAbsOrigin;
	}
}

//-----------------------------------------------------------------------------
// Registers a blockage
//-----------------------------------------------------------------------------
CBaseEntity *CPhysicsPushedEntities::RegisterBlockage()
{
	Assert(	m_nBlocker >= 0 );

	// Generate a PhysicsImpact against the blocker...
	PhysicsPushedInfo_t &info = m_rgMoved[m_nBlocker];
	if ( info.m_Trace.m_pEnt )
	{
		info.m_pEntity->GetEngineObject()->PhysicsImpact(((CBaseEntity*)info.m_Trace.m_pEnt)->GetEngineObject(), info.m_Trace );
	}

	// This is the dude 
	return info.m_pEntity;
}


//-----------------------------------------------------------------------------
// Purpose: Restore entities that might have been moved
// Input  : fromrotation - if the move is from a rotation, then angular move must also be reverted
//			*amove - 
//-----------------------------------------------------------------------------
void CPhysicsPushedEntities::RestoreEntities( )
{
	// Reset all of the pushed entities to get them back into place also
	for ( int i = m_rgMoved.Count(); --i >= 0; )
	{
		m_rgMoved[ i ].m_pEntity->GetEngineObject()->SetAbsOrigin( m_rgMoved[ i ].m_vecStartAbsOrigin );
	}
}




//-----------------------------------------------------------------------------
// Purpose: This is a trace filter that only hits an exclusive list of entities
//-----------------------------------------------------------------------------
class CTraceFilterAgainstEntityList : public ITraceFilter
{
public:
	virtual bool ShouldHitEntity( IHandleEntity *pEntity, int contentsMask )
	{
		for ( int i = m_entityList.Count()-1; i >= 0; --i )
		{
			if ( m_entityList[i] == pEntity )
				return true;
		}

		return false;
	}

	virtual TraceType_t	GetTraceType() const
	{
		return TRACE_ENTITIES_ONLY;
	}

	void AddEntityToHit( IHandleEntity *pEntity )
	{
		m_entityList.AddToTail(pEntity);
	}

	CUtlVector<IHandleEntity *>	m_entityList;
};

//-----------------------------------------------------------------------------
// Generates a list of potential blocking entities
//-----------------------------------------------------------------------------
class CPushBlockerEnum : public IPartitionEnumerator
{
public:
	CPushBlockerEnum( CPhysicsPushedEntities *pPushedEntities ) : m_pPushedEntities(pPushedEntities)
	{
		// All elements are part of the same hierarchy, so they all have
		// the same root, so it doesn't matter which one we grab
		m_pRootHighestParent = m_pPushedEntities->m_rgPusher[0].m_pEntity->GetEngineObject()->GetRootMoveParent()->GetOuter();
		++s_nEnumCount;

		m_collisionGroupCount = 0;
		for ( int i = m_pPushedEntities->m_rgPusher.Count(); --i >= 0; )
		{
			if ( !m_pPushedEntities->m_rgPusher[i].m_pEntity->GetEngineObject()->IsSolid() )
				continue;

			m_pushersOnly.AddEntityToHit( m_pPushedEntities->m_rgPusher[i].m_pEntity );
			int collisionGroup = m_pPushedEntities->m_rgPusher[i].m_pEntity->GetEngineObject()->GetCollisionGroup();
			AddCollisionGroup(collisionGroup);
		}

	}

	virtual IterationRetval_t EnumElement( IHandleEntity *pHandleEntity )
	{
		CBaseEntity *pCheck = GetPushableEntity( pHandleEntity );
		if ( !pCheck )
			return ITERATION_CONTINUE;

		// Mark it as seen
		pCheck->m_nPushEnumCount = s_nEnumCount;
		m_pPushedEntities->AddEntity( pCheck );

		return ITERATION_CONTINUE;
	}

private:

	inline void AddCollisionGroup(int collisionGroup)
	{
		for ( int i = 0; i < m_collisionGroupCount; i++ )
		{
			if ( m_collisionGroups[i] == collisionGroup )
				return;
		}
		if ( m_collisionGroupCount < ARRAYSIZE(m_collisionGroups) )
		{
			m_collisionGroups[m_collisionGroupCount] = collisionGroup;
			m_collisionGroupCount++;
		}
	}

	bool IsStandingOnPusher( CBaseEntity *pCheck )
	{
		CBaseEntity* pGroundEnt = pCheck->GetEngineObject()->GetGroundEntity() ? pCheck->GetEngineObject()->GetGroundEntity()->GetOuter() : NULL;
		if ( pCheck->GetEngineObject()->GetFlags() & FL_ONGROUND || pGroundEnt )
		{
			for ( int i = m_pPushedEntities->m_rgPusher.Count(); --i >= 0; )
			{
				if (m_pPushedEntities->m_rgPusher[i].m_pEntity == pGroundEnt)
				{
					return true;
				}
			}
		}
		return false;
	}

	bool IntersectsPushers( CBaseEntity *pTest )
	{
		trace_t tr;

		ICollideable *pCollision = pTest->GetCollideable();
		enginetrace->SweepCollideable( pCollision, pTest->GetEngineObject()->GetAbsOrigin(), pTest->GetEngineObject()->GetAbsOrigin(), pCollision->GetCollisionAngles(),
			pTest->PhysicsSolidMaskForEntity(), &m_pushersOnly, &tr );

		return tr.startsolid;
	}

	CBaseEntity *GetPushableEntity( IHandleEntity *pHandleEntity )
	{
		CBaseEntity *pCheck = EntityList()->GetBaseEntityFromHandle( pHandleEntity->GetRefEHandle() );
		if ( !pCheck )
			return NULL;

		// Don't bother if we've already seen this one...
		if (pCheck->m_nPushEnumCount == s_nEnumCount)
			return NULL;

		if ( !pCheck->GetEngineObject()->IsSolid() )
			return NULL;

		if ( pCheck->GetEngineObject()->GetMoveType() == MOVETYPE_PUSH ||
			 pCheck->GetEngineObject()->GetMoveType() == MOVETYPE_NONE ||
			 pCheck->GetEngineObject()->GetMoveType() == MOVETYPE_VPHYSICS ||
			 pCheck->GetEngineObject()->GetMoveType() == MOVETYPE_NOCLIP )
		{
			return NULL;
		}

		bool bCollide = false;
		for ( int i = 0; i < m_collisionGroupCount; i++ )
		{
			if ( g_pGameRules->ShouldCollide( pCheck->GetEngineObject()->GetCollisionGroup(), m_collisionGroups[i] ) )
			{
				bCollide = true;
				break;
			}
		}
		if ( !bCollide )
			return NULL;
		// We're not pushing stuff we're hierarchically attached to
		IEngineObjectServer *pCheckHighestParent = pCheck->GetEngineObject()->GetRootMoveParent();
		if (pCheckHighestParent->GetOuter() == m_pRootHighestParent)
			return NULL;

		// If we're standing on the pusher or any rigidly attached child
		// of the pusher, we don't need to bother checking for interpenetration
		if ( !IsStandingOnPusher(pCheck) )
		{
			// Our surrounding boxes are touching. But we may well not be colliding....
			// see if the ent's bbox is inside the pusher's final position
			if ( !IntersectsPushers( pCheck ) )
				return NULL;
		}

		// NOTE: This is pretty tricky here. If a rigidly attached child comes into
		// contact with a pusher, we *cannot* push the child. Instead, we must push
		// the highest parent of that child.
		return pCheckHighestParent->GetOuter();
	}

private:
	static int s_nEnumCount;
	CPhysicsPushedEntities *m_pPushedEntities;
	CBaseEntity *m_pRootHighestParent;
	CTraceFilterAgainstEntityList	m_pushersOnly;
	int m_collisionGroups[8];
	int m_collisionGroupCount;
};

int CPushBlockerEnum::s_nEnumCount = 0;

//-----------------------------------------------------------------------------
// Generates a list of potential blocking entities
//-----------------------------------------------------------------------------
void CPhysicsPushedEntities::GenerateBlockingEntityList()
{
	VPROF("CPhysicsPushedEntities::GenerateBlockingEntityList");

	m_rgMoved.RemoveAll();
	CPushBlockerEnum blockerEnum( this );

	for ( int i = m_rgPusher.Count(); --i >= 0;  )
	{
		CBaseEntity *pPusher = m_rgPusher[i].m_pEntity;

		// Don't bother if the pusher isn't solid
		if ( !pPusher->GetEngineObject()->IsSolid() || pPusher->GetEngineObject()->IsSolidFlagSet( FSOLID_VOLUME_CONTENTS ) )
		{
			continue;
		}

		Vector vecAbsMins, vecAbsMaxs;
		pPusher->GetEngineObject()->WorldSpaceAABB( &vecAbsMins, &vecAbsMaxs );
		partition->EnumerateElementsInBox( PARTITION_ENGINE_NON_STATIC_EDICTS, vecAbsMins, vecAbsMaxs, false, &blockerEnum );

		//Go back throught the generated list.
	}
}

//-----------------------------------------------------------------------------
// Generates a list of potential blocking entities
//-----------------------------------------------------------------------------
void CPhysicsPushedEntities::GenerateBlockingEntityListAddBox( const Vector &vecMoved )
{
	VPROF("CPhysicsPushedEntities::GenerateBlockingEntityListAddBox");

	m_rgMoved.RemoveAll();
	CPushBlockerEnum blockerEnum( this );

	for ( int i = m_rgPusher.Count(); --i >= 0;  )
	{
		CBaseEntity *pPusher = m_rgPusher[i].m_pEntity;

		// Don't bother if the pusher isn't solid
		if ( !pPusher->GetEngineObject()->IsSolid() || pPusher->GetEngineObject()->IsSolidFlagSet( FSOLID_VOLUME_CONTENTS ) )
		{
			continue;
		}

		Vector vecAbsMins, vecAbsMaxs;
		pPusher->GetEngineObject()->WorldSpaceAABB( &vecAbsMins, &vecAbsMaxs );
		for ( int iAxis = 0; iAxis < 3; ++iAxis )
		{
			if ( vecMoved[iAxis] >= 0.0f )
			{
				vecAbsMins[iAxis] -= vecMoved[iAxis];
			}
			else
			{
				vecAbsMaxs[iAxis] -= vecMoved[iAxis];
			}
		}

		partition->EnumerateElementsInBox( PARTITION_ENGINE_NON_STATIC_EDICTS, vecAbsMins, vecAbsMaxs, false, &blockerEnum );

		//Go back throught the generated list.
	}
}


//-----------------------------------------------------------------------------
// Purpose: Gets a list of all entities hierarchically attached to the root 
//-----------------------------------------------------------------------------
void CPhysicsPushedEntities::SetupAllInHierarchy( CBaseEntity *pParent )
{
	if (!pParent)
		return;

	VPROF("CPhysicsPushedEntities::SetupAllInHierarchy");

	// Make sure to snack the position +before+ relink because applying the
	// rotation (which occurs in relink) will put it at the final location
	// NOTE: The root object at this point is actually at its final position.
	// We'll fix that up later
	int i = m_rgPusher.AddToTail();
	m_rgPusher[i].m_pEntity = pParent;
	m_rgPusher[i].m_vecStartAbsOrigin = pParent->GetEngineObject()->GetAbsOrigin();

	IEngineObjectServer *pChild;
	for ( pChild = pParent->GetEngineObject()->FirstMoveChild(); pChild != NULL; pChild = pChild->NextMovePeer() )
	{
		SetupAllInHierarchy( pChild->GetOuter() );
	}
}


//-----------------------------------------------------------------------------
// Purpose: Rotates the root entity, fills in the pushmove structure
//-----------------------------------------------------------------------------
void CPhysicsPushedEntities::RotateRootEntity( CBaseEntity *pRoot, float movetime, RotatingPushMove_t &rotation )
{
	VPROF("CPhysicsPushedEntities::RotateRootEntity");

	rotation.amove = pRoot->GetLocalAngularVelocity() * movetime;
	rotation.origin = pRoot->GetEngineObject()->GetAbsOrigin();

	// Knowing the initial + ending basis is needed for determining
	// which corner we're pushing 
	MatrixCopy( pRoot->GetEngineObject()->EntityToWorldTransform(), rotation.startLocalToWorld );

	// rotate the pusher to it's final position
	QAngle angles = pRoot->GetEngineObject()->GetLocalAngles();
	angles += pRoot->GetLocalAngularVelocity() * movetime;

	pRoot->GetEngineObject()->SetLocalAngles( angles );
	
	// Compute the change in absangles
	MatrixCopy( pRoot->GetEngineObject()->EntityToWorldTransform(), rotation.endLocalToWorld );
}


//-----------------------------------------------------------------------------
// Purpose: Tries to rotate an entity hierarchy, returns the blocker if any
//-----------------------------------------------------------------------------
CBaseEntity *CPhysicsPushedEntities::PerformRotatePush( CBaseEntity *pRoot, float movetime )
{
	VPROF("CPhysicsPushedEntities::PerformRotatePush");

	m_bIsUnblockableByPlayer = (pRoot->GetEngineObject()->GetFlags() & FL_UNBLOCKABLE_BY_PLAYER) ? true : false;
	// Build a list of this entity + all its children because we're going to try to move them all
	// This will also make sure each entity is linked in the appropriate place
	// with correct absboxes
	m_rgPusher.RemoveAll();
	SetupAllInHierarchy( pRoot );

	// save where we rotated from, in case we're blocked
	QAngle angPrevAngles = pRoot->GetEngineObject()->GetLocalAngles();

	// Apply the rotation
	RotatingPushMove_t	rotPushMove;
	RotateRootEntity( pRoot, movetime, rotPushMove );

	// Next generate a list of all entities that could potentially be intersecting with
	// any of the children in their new locations...
	GenerateBlockingEntityList( );

	// Now we have a unique list of things that could potentially block our push
	// and need to be pushed out of the way. Lets try to push them all out of the way.
	// If we fail, undo it all
	if (!SpeculativelyCheckRotPush( rotPushMove, pRoot ))
	{
		CBaseEntity *pBlocker = RegisterBlockage();
		pRoot->GetEngineObject()->SetLocalAngles( angPrevAngles );
		RestoreEntities( );
		return pBlocker;
	}

	FinishPush( true, &rotPushMove );
	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Linearly moves the root entity
//-----------------------------------------------------------------------------
void CPhysicsPushedEntities::LinearlyMoveRootEntity( CBaseEntity *pRoot, float movetime, Vector *pAbsPushVector )
{
	VPROF("CPhysicsPushedEntities::LinearlyMoveRootEntity");

	// move the pusher to it's final position
	Vector move = pRoot->GetEngineObject()->GetLocalVelocity() * movetime;
	Vector origin = pRoot->GetEngineObject()->GetLocalOrigin();
	origin += move;		
	pRoot->GetEngineObject()->SetLocalOrigin( origin );

	// Store off the abs push vector
	*pAbsPushVector = pRoot->GetEngineObject()->GetAbsVelocity() * movetime;
}


//-----------------------------------------------------------------------------
// Purpose: Tries to linearly push an entity hierarchy, returns the blocker if any
//-----------------------------------------------------------------------------
CBaseEntity *CPhysicsPushedEntities::PerformLinearPush( CBaseEntity *pRoot, float movetime )
{
	VPROF("CPhysicsPushedEntities::PerformLinearPush");

	m_flMoveTime = movetime;

	m_bIsUnblockableByPlayer = (pRoot->GetEngineObject()->GetFlags() & FL_UNBLOCKABLE_BY_PLAYER) ? true : false;
	// Build a list of this entity + all its children because we're going to try to move them all
	// This will also make sure each entity is linked in the appropriate place
	// with correct absboxes
	m_rgPusher.RemoveAll();
	SetupAllInHierarchy( pRoot );

	// save where we started from, in case we're blocked
	Vector vecPrevOrigin = pRoot->GetEngineObject()->GetLocalOrigin();

	// Move the root (and all children) into its new position
	Vector vecAbsPush;
	LinearlyMoveRootEntity( pRoot, movetime, &vecAbsPush );

	// Next generate a list of all entities that could potentially be intersecting with
	// any of the children in their new locations...
	GenerateBlockingEntityListAddBox( vecAbsPush );

	// Now we have a unique list of things that could potentially block our push
	// and need to be pushed out of the way. Lets try to push them all out of the way.
	// If we fail, undo it all
	if (!SpeculativelyCheckLinearPush( vecAbsPush ))
	{
		CBaseEntity *pBlocker = RegisterBlockage();
		pRoot->GetEngineObject()->SetLocalOrigin( vecPrevOrigin );
		RestoreEntities();
		return pBlocker;
	}

	FinishPush( );
	return NULL;
}



//-----------------------------------------------------------------------------
//
// CBaseEntity methods
//
//-----------------------------------------------------------------------------



//-----------------------------------------------------------------------------
// Purpose: Does not change the entities velocity at all
// Input  : push - 
// Output : trace_t
//-----------------------------------------------------------------------------
void CBaseEntity::PhysicsCheckSweep( const Vector& vecAbsStart, const Vector &vecAbsDelta, trace_t *pTrace )
{
	::PhysicsCheckSweep( this, vecAbsStart, vecAbsDelta, pTrace );
}



#define	MAX_CLIP_PLANES	5
//-----------------------------------------------------------------------------
// Purpose: The basic solid body movement attempt/clip that slides along multiple planes
// Input  : time - Amount of time to try moving for
//			*steptrace - if not NULL, the trace results of any vertical wall hit will be stored
// Output : int - the clipflags if the velocity was modified (hit something solid)
//   1 = floor
//   2 = wall / step
//   4 = dead stop
//-----------------------------------------------------------------------------
int CBaseEntity::PhysicsTryMove( float flTime, trace_t *steptrace )
{
	VPROF("CBaseEntity::PhysicsTryMove");

	int			bumpcount, numbumps;
	Vector		dir;
	float		d;
	int			numplanes;
	Vector		planes[MAX_CLIP_PLANES];
	Vector		primal_velocity, original_velocity, new_velocity;
	int			i, j;
	trace_t		trace;
	Vector		end;
	float		time_left;
	int			blocked;
	
	unsigned int mask = PhysicsSolidMaskForEntity();

	new_velocity.Init();

	numbumps = 4;

	Vector vecAbsVelocity = GetEngineObject()->GetAbsVelocity();

	blocked = 0;
	VectorCopy (vecAbsVelocity, original_velocity);
	VectorCopy (vecAbsVelocity, primal_velocity);
	numplanes = 0;
	
	time_left = flTime;

	for (bumpcount=0 ; bumpcount<numbumps ; bumpcount++)
	{
		if (vecAbsVelocity == vec3_origin)
			break;

		VectorMA(GetEngineObject()->GetAbsOrigin(), time_left, vecAbsVelocity, end );

		Physics_TraceEntity( this, GetEngineObject()->GetAbsOrigin(), end, mask, &trace );

		if (trace.startsolid)
		{	// entity is trapped in another solid
			GetEngineObject()->SetAbsVelocity(vec3_origin);
			return 4;
		}

		if (trace.fraction > 0)
		{	// actually covered some distance
			GetEngineObject()->SetAbsOrigin( trace.endpos );
			VectorCopy (vecAbsVelocity, original_velocity);
			numplanes = 0;
		}

		if (trace.fraction == 1)
			 break;		// moved the entire distance

		if (!trace.m_pEnt)
		{
			GetEngineObject()->SetAbsVelocity( vecAbsVelocity );
			Warning( "PhysicsTryMove: !trace.u.ent" );
			Assert(0);
			return 4;
		}

		if (trace.plane.normal[2] > 0.7)
		{
			blocked |= 1;		// floor
			if (CanStandOn((CBaseEntity*)trace.m_pEnt ))
			{
				// keep track of time when changing ground entity
				if (!GetEngineObject()->GetGroundEntity()|| GetEngineObject()->GetGroundEntity()->GetOuter() != trace.m_pEnt)
				{
					GetEngineObject()->SetGroundChangeTime( gpGlobals->curtime + (flTime - (1 - trace.fraction) * time_left) );
				}

				GetEngineObject()->SetGroundEntity(((CBaseEntity*)trace.m_pEnt)->GetEngineObject() );
			}
		}
		if (!trace.plane.normal[2])
		{
			blocked |= 2;		// step
			if (steptrace)
				*steptrace = trace;	// save for player extrafriction
		}

		// run the impact function
		GetEngineObject()->PhysicsImpact(((CBaseEntity*)trace.m_pEnt)->GetEngineObject(), trace );
		// Removed by the impact function
		if (GetEngineObject()->IsMarkedForDeletion())// || engine->IsEdictFree(entindex()) 
			break;		
	
		time_left -= time_left * trace.fraction;
		
		// clipped to another plane
		if (numplanes >= MAX_CLIP_PLANES)
		{	// this shouldn't really happen
			GetEngineObject()->SetAbsVelocity(vec3_origin);
			return blocked;
		}

		VectorCopy (trace.plane.normal, planes[numplanes]);
		numplanes++;

		// modify original_velocity so it parallels all of the clip planes
		if (GetEngineObject()->GetMoveType() == MOVETYPE_WALK && (!(GetEngineObject()->GetFlags() & FL_ONGROUND) || GetEngineObject()->GetFriction()!=1) )	// relfect player velocity
		{
			for ( i = 0; i < numplanes; i++ )
			{
				if ( planes[i][2] > 0.7  )
				{// floor or slope
					PhysicsClipVelocity( original_velocity, planes[i], new_velocity, 1 );
					VectorCopy( new_velocity, original_velocity );
				}
				else
				{
					PhysicsClipVelocity( original_velocity, planes[i], new_velocity, 1.0 + sv_bounce.GetFloat() * (1- GetEngineObject()->GetFriction()) );
				}
			}

			VectorCopy( new_velocity, vecAbsVelocity );
			VectorCopy( new_velocity, original_velocity );
		}
		else
		{
			for (i=0 ; i<numplanes ; i++)
			{
				PhysicsClipVelocity (original_velocity, planes[i], new_velocity, 1);
				for (j=0 ; j<numplanes ; j++)
					if (j != i)
					{
						if (DotProduct (new_velocity, planes[j]) < 0)
							break;	// not ok
					}
				if (j == numplanes)
					break;
			}
			
			if (i != numplanes)
			{	
				// go along this plane
				VectorCopy (new_velocity, vecAbsVelocity);
			}
			else
			{	
				// go along the crease
				if (numplanes != 2)
				{
	//				Msg( "clip velocity, numplanes == %i\n",numplanes);
					GetEngineObject()->SetAbsVelocity( vecAbsVelocity );
					return blocked;
				}
				CrossProduct (planes[0], planes[1], dir);
				d = DotProduct (dir, vecAbsVelocity);
				VectorScale (dir, d, vecAbsVelocity);
			}

			//
			// if original velocity is against the original velocity, stop dead
			// to avoid tiny oscillations in sloping corners
			//
			if (DotProduct (vecAbsVelocity, primal_velocity) <= 0)
			{
				GetEngineObject()->SetAbsVelocity(vec3_origin);
				return blocked;
			}
		}
	}

	GetEngineObject()->SetAbsVelocity( vecAbsVelocity );
	return blocked;
}

//-----------------------------------------------------------------------------
// Purpose: Applies 1/2 gravity to falling movetype step objects
//			Simulation should be done assuming average velocity over the time 
//			interval.  Since that would effect a lot of code, and since most of 
//			that code is going away, it's easier to just add in the average effect 
//			of gravity on the velocity over the interval at the beginning of similation, 
//			then add it in again at the end of simulation so that the final velocity is
//			correct for the entire interval.
//-----------------------------------------------------------------------------
void CBaseEntity::PhysicsAddHalfGravity( float timestep )
{
	VPROF("CBaseEntity::PhysicsAddHalfGravity");
	float	ent_gravity;

	if (GetEngineObject()->GetGravity() )
	{
		ent_gravity = GetEngineObject()->GetGravity();
	}
	else
	{
		ent_gravity = 1.0;
	}

	// Add 1/2 of the total gravitational effects over this timestep
	Vector vecAbsVelocity = GetEngineObject()->GetAbsVelocity();
	vecAbsVelocity[2] -= ( 0.5 * ent_gravity * GetCurrentGravity() * timestep );
	vecAbsVelocity[2] += GetBaseVelocity()[2] * gpGlobals->frametime;
	GetEngineObject()->SetAbsVelocity( vecAbsVelocity );

	Vector vecNewBaseVelocity = GetBaseVelocity();
	vecNewBaseVelocity[2] = 0;
	SetBaseVelocity( vecNewBaseVelocity );
	
	// Bound velocity
	PhysicsCheckVelocity();
}


//-----------------------------------------------------------------------------
// Purpose: Does not change the entities velocity at all
// Input  : push - 
// Output : trace_t
//-----------------------------------------------------------------------------
void CBaseEntity::PhysicsPushEntity( const Vector& push, trace_t *pTrace )
{
	VPROF("CBaseEntity::PhysicsPushEntity");

	if (GetEngineObject()->GetMoveParent() )
	{
		Warning( "pushing entity (%s) that has parent (%s)!\n", GetDebugName(), GetEngineObject()->GetMoveParent()->GetOuter()->GetDebugName());
		Assert(0);
	}

	// NOTE: absorigin and origin must be equal because there is no moveparent
	Vector prevOrigin;
	VectorCopy(GetEngineObject()->GetAbsOrigin(), prevOrigin );

	::PhysicsCheckSweep( this, prevOrigin, push, pTrace );

	if ( pTrace->fraction )
	{
		GetEngineObject()->SetAbsOrigin( pTrace->endpos );

		// FIXME(ywb):  Should we try to enable this here
		// WakeRestingObjects();
	}

	// Passing in the previous abs origin here will cause the relinker
	// to test the swept ray from previous to current location for trigger intersections
	GetEngineObject()->PhysicsTouchTriggers( &prevOrigin );

	if ( pTrace->m_pEnt )
	{
		GetEngineObject()->PhysicsImpact(((CBaseEntity*)pTrace->m_pEnt)->GetEngineObject(), *pTrace );
	}
}


//-----------------------------------------------------------------------------
// Purpose:  See if entity is inside another entity, if so, returns true if so, fills in *ppEntity if ppEntity is not NULL
// Input  : **ppEntity - optional return pointer to entity we are inside of
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CBaseEntity::PhysicsTestEntityPosition( CBaseEntity **ppEntity /*=NULL*/ )
{
	VPROF("CBaseEntity::PhysicsTestEntityPosition");

	trace_t	trace;
	
	unsigned int mask = PhysicsSolidMaskForEntity();

	Physics_TraceEntity( this, GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsOrigin(), mask, &trace );
	
	if ( trace.startsolid )
	{
		if ( ppEntity )
		{
			*ppEntity = (CBaseEntity*)trace.m_pEnt;
		}
		return true;
	}
		
	return false;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CBaseEntity *CBaseEntity::PhysicsPushMove( float movetime )
{
	VPROF("CBaseEntity::PhysicsPushMove");

	// If this entity isn't moving, just update the time.
	IncrementLocalTime( movetime );

	if (GetEngineObject()->GetLocalVelocity() == vec3_origin )
	{
		return NULL;
	}

	// Now check that the entire hierarchy can rotate into the new location
	CBaseEntity *pBlocker = g_pPushedEntities->PerformLinearPush( this, movetime );
	if ( pBlocker )
	{
		IncrementLocalTime( -movetime );
	}
	return pBlocker;
}


//-----------------------------------------------------------------------------
// Purpose: Tries to rotate, returns success or failure
// Input  : movetime - 
// Output : bool
//-----------------------------------------------------------------------------
CBaseEntity *CBaseEntity::PhysicsPushRotate( float movetime )
{
	VPROF("CBaseEntity::PhysicsPushRotate");

	IncrementLocalTime( movetime );

	// Not rotating
	if ( GetLocalAngularVelocity() == vec3_angle )
	{
		return NULL;
	}

	// Now check that the entire hierarchy can rotate into the new location
	CBaseEntity *pBlocker = g_pPushedEntities->PerformRotatePush( this, movetime );
	if ( pBlocker )
	{
		IncrementLocalTime( -movetime );
	}

	return pBlocker;
}


//-----------------------------------------------------------------------------
// Block of icky shared code from PhysicsParent + PhysicsPusher
//-----------------------------------------------------------------------------
void CBaseEntity::PerformPush( float movetime )
{
	VPROF("CBaseEntity::PerformPush");
	// NOTE: Use handle index because the previous blocker could have been deleted
	int hPrevBlocker = m_pBlocker.ToInt();
	CBaseEntity *pBlocker;
	g_pPushedEntities->BeginPush( this );
	if (movetime > 0)
	{
		if ( GetLocalAngularVelocity() != vec3_angle )
		{
			if (GetEngineObject()->GetLocalVelocity() != vec3_origin )
			{
				// NOTE: Both PhysicsPushRotate + PhysicsPushMove
				// will attempt to advance local time. Choose the one that's
				// the greater of the two from push + move

				// FIXME: Should we really be doing them both simultaneously??
				// FIXME: Choose the *greater* of the two?!? That's strange...
				float flInitialLocalTime = m_flLocalTime;

				// moving and rotating, so rotate first, then move
				pBlocker = PhysicsPushRotate( movetime );
				if ( !pBlocker )
				{
					float flRotateLocalTime = m_flLocalTime;

					// Reset the local time to what it was before we rotated
					m_flLocalTime = flInitialLocalTime;
					pBlocker = PhysicsPushMove( movetime );
					if ( m_flLocalTime < flRotateLocalTime )
					{
						m_flLocalTime = flRotateLocalTime;
					}
				}
			}
			else
			{
				// only rotating
				pBlocker = PhysicsPushRotate( movetime );
			}
		}
		else
		{
			// only moving
			pBlocker = PhysicsPushMove( movetime );
		}

		m_pBlocker = pBlocker;
		if (m_pBlocker.ToInt() != hPrevBlocker)
		{
			if (hPrevBlocker != INVALID_EHANDLE_INDEX)
			{
				EndBlocked();
			}
			if (m_pBlocker)
			{
				StartBlocked( pBlocker );
			}
		}
		if (m_pBlocker)
		{
			Blocked( m_pBlocker );
		}

		// NOTE NOTE: This is here for brutal reasons.
		// For MOVETYPE_PUSH objects with VPhysics shadow objects, the move done time
		// is handled by CBaseEntity::VPhyicsUpdatePusher, which only gets called if
		// the physics system thinks the entity is awake. That will happen if the
		// shadow gets updated, but the push code above doesn't update unless the
		// move is successful or non-zero. So we must make sure it's awake
		if (GetEngineObject()->VPhysicsGetObject() )
		{
			GetEngineObject()->VPhysicsGetObject()->Wake();
		}
	}

	// move done is handled by physics if it has any
	if (GetEngineObject()->VPhysicsGetObject() )
	{
		// store the list of moved entities for later
		// if you actually did an unblocked push that moved entities, and you're using physics (which may block later)
		if ( movetime > 0 && !m_pBlocker && GetEngineObject()->GetSolid() == SOLID_VPHYSICS && g_pPushedEntities->CountMovedEntities() > 0 )
		{
			// UNDONE: Any reason to want to call this twice before physics runs?
			// If so, maybe just append to the list?
			Assert( !GetEngineObject()->GetDataObject( PHYSICSPUSHLIST ) );
			physicspushlist_t *pList = (physicspushlist_t *)GetEngineObject()->CreateDataObject( PHYSICSPUSHLIST );
			if ( pList )
			{
				g_pPushedEntities->StoreMovedEntities( *pList );
			}
		}
	}
	else
	{
		if ( m_flMoveDoneTime <= m_flLocalTime && m_flMoveDoneTime > 0 )
		{
			SetMoveDoneTime( -1 );
			MoveDone();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: UNDONE: This is only different from PhysicsParent because of the callback to PhysicsVelocity()
// Can we support that callback in push objects as well?
//-----------------------------------------------------------------------------
void CBaseEntity::PhysicsPusher( void )
{
	VPROF("CBaseEntity::PhysicsPusher");

	// regular thinking
	if ( !GetEngineObject()->PhysicsRunThink() )
		return;

	m_flVPhysicsUpdateLocalTime = m_flLocalTime;

	float movetime = GetMoveDoneTime();
	if (movetime > gpGlobals->frametime)
	{
		movetime = gpGlobals->frametime;
	}

	PerformPush( movetime );
}



//-----------------------------------------------------------------------------
// Purpose: Non moving objects can only think
//-----------------------------------------------------------------------------
void CBaseEntity::PhysicsNone( void )
{
	VPROF("CBaseEntity::PhysicsNone");

	// regular thinking
	GetEngineObject()->PhysicsRunThink();
}


//-----------------------------------------------------------------------------
// Purpose: A moving object that doesn't obey physics
//-----------------------------------------------------------------------------
void CBaseEntity::PhysicsNoclip( void )
{
	VPROF("CBaseEntity::PhysicsNoclip");

	// regular thinking
	if ( !GetEngineObject()->PhysicsRunThink() )
	{
		return;
	}
	
	// Apply angular velocity
	SimulateAngles( gpGlobals->frametime );

	Vector origin;
	VectorMA(GetEngineObject()->GetLocalOrigin(), gpGlobals->frametime, GetEngineObject()->GetLocalVelocity(), origin );
	GetEngineObject()->SetLocalOrigin( origin );
}


void CBaseEntity::PerformCustomPhysics( Vector *pNewPosition, Vector *pNewVelocity, QAngle *pNewAngles, QAngle *pNewAngVelocity )
{
	// If you're going to use custom physics, you need to implement this!
	Assert(0);
}


//-----------------------------------------------------------------------------
// Allows entities to describe their own physics
//-----------------------------------------------------------------------------
void CBaseEntity::PhysicsCustom()
{
	VPROF("CBaseEntity::PhysicsCustom");
	PhysicsCheckWater();

	// regular thinking
	if ( !GetEngineObject()->PhysicsRunThink() )
		return;

	// Moving upward, off the ground, or  resting on a client/monster, remove FL_ONGROUND
	if (GetEngineObject()->GetLocalVelocity()[2] > 0 || !GetEngineObject()->GetGroundEntity() || !GetEngineObject()->GetGroundEntity()->GetOuter()->IsStandable())
	{
		GetEngineObject()->SetGroundEntity( NULL );
	}

	// NOTE: The entity must set the position, angles, velocity in its custom movement
	Vector vecNewPosition = GetEngineObject()->GetAbsOrigin();
	Vector vecNewVelocity = GetEngineObject()->GetAbsVelocity();
	QAngle angNewAngles = GetEngineObject()->GetAbsAngles();
	QAngle angNewAngVelocity = GetLocalAngularVelocity();

	PerformCustomPhysics( &vecNewPosition, &vecNewVelocity, &angNewAngles, &angNewAngVelocity );

	// Store off all of the new state information...
	GetEngineObject()->SetAbsVelocity( vecNewVelocity );
	GetEngineObject()->SetAbsAngles( angNewAngles );
	SetLocalAngularVelocity( angNewAngVelocity );

	Vector move;
	VectorSubtract( vecNewPosition, GetEngineObject()->GetAbsOrigin(), move );

	// move origin
	trace_t trace;
	PhysicsPushEntity( move, &trace );

	PhysicsCheckVelocity();

	if (trace.allsolid)
	{	
		// entity is trapped in another solid
		// UNDONE: does this entity needs to be removed?
		GetEngineObject()->SetAbsVelocity(vec3_origin);
		SetLocalAngularVelocity(vec3_angle);
		return;
	}
	
	if (GetEngineObject()->IsMarkedForDeletion())//engine->IsEdictFree(entindex())
		return;

	// check for in water
	PhysicsCheckWaterTransition();
}






#define STEP_TELPORTATION_VEL_SQ	( 4096.0f * 4096.0f )
//-----------------------------------------------------------------------------
// Purpose: Run regular think and latch off angle/origin changes so we can interpolate them on the server to fake simulation
// Input  : *step - 
//-----------------------------------------------------------------------------
void CBaseEntity::StepSimulationThink( float dt )
{
	// See if we need to allocate, deallocate step simulation object
	GetEngineObject()->CheckStepSimulationChanged();

	StepSimulationData *step = ( StepSimulationData * )GetEngineObject()->GetDataObject( STEPSIMULATION );
	if ( !step )
	{
		PhysicsStepRunTimestep( dt );

		// Just call the think function directly
		GetEngineObject()->PhysicsRunThink( THINK_FIRE_BASE_ONLY );
	}
	else
	{
		// Assume that it's in use
		step->m_bOriginActive = true;
		step->m_bAnglesActive = true;

		// Reset networked versions of origin and angles
		step->m_nLastProcessTickCount = -1;
		step->m_vecNetworkOrigin.Init();
		step->m_angNetworkAngles.Init();

		// Remember old old values
		step->m_Previous2 = step->m_Previous;

		// Remember old values
		step->m_Previous.nTickCount = gpGlobals->tickcount;
		step->m_Previous.vecOrigin = GetStepOrigin();
		QAngle stepAngles = GetStepAngles();
		AngleQuaternion( stepAngles, step->m_Previous.qRotation );

		// Run simulation
		PhysicsStepRunTimestep( dt );

		// Call the actual think function...
		GetEngineObject()->PhysicsRunThink( THINK_FIRE_BASE_ONLY );

		// do any local processing that's needed
		if (GetBaseAnimating() != NULL)
		{
			GetBaseAnimating()->GetEngineObject()->UpdateStepOrigin();
		}

		// Latch new values to see if external code modifies our position/orientation
		step->m_Next.vecOrigin = GetStepOrigin();
		stepAngles = GetStepAngles();
		AngleQuaternion( stepAngles, step->m_Next.qRotation );
		// Also store of non-Quaternion version for simple comparisons
		step->m_angNextRotation = GetStepAngles();
		step->m_Next.nTickCount = GetEngineObject()->GetNextThinkTick();

		// Hack:  Add a tick if we are simulating every other tick
		if (EntityList()->IsSimulatingOnAlternateTicks() )
		{
			++step->m_Next.nTickCount;
		}

		// Check for teleportation/snapping of the origin
		if ( dt > 0.0f )
		{
			Vector deltaorigin = step->m_Next.vecOrigin - step->m_Previous.vecOrigin;
			float velSq = deltaorigin.LengthSqr() / ( dt * dt );
			if ( velSq >= STEP_TELPORTATION_VEL_SQ )
			{
				// Deactivate it due to large origin change
				step->m_bOriginActive = false;
				step->m_bAnglesActive = false;
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Monsters freefall when they don't have a ground entity, otherwise
//   all movement is done with discrete steps.
// This is also used for objects that have become still on the ground, but
//  will fall if the floor is pulled out from under them.
// JAY: Extended this to synchronize movement and thinking wherever possible.
// This allows the client-side interpolation to interpolate animation and simulation
// data at the same time.
// UNDONE:	Remove all other cases from this loop - only use MOVETYPE_STEP to simulate
//			entities that are currently animating/thinking.
//-----------------------------------------------------------------------------
void CBaseEntity::PhysicsStep()
{
	// EVIL HACK: Force these to appear as if they've changed!!!
	// The underlying values don't actually change, but we need the network sendproxy on origin/angles
	//  to get triggered, and that only happens if NetworkStateChanged() appears to have occured.
	// Getting them for modify marks them as changed automagically.
	//m_vecOrigin.GetForModify();
	//m_angRotation.GetForModify();
	GetEngineObject()->SimulationChanged();
	
	// HACK:  Make sure that the client latches the networked origin/orientation changes with the current server tick count
	//  so that we don't get jittery interpolation.  All of this is necessary to mimic actual continuous simulation of the underlying
	//  variables.
	GetEngineObject()->SetSimulationTime( gpGlobals->curtime );
	
	// Run all but the base think function
	GetEngineObject()->PhysicsRunThink( THINK_FIRE_ALL_BUT_BASE );

	int thinktick = GetEngineObject()->GetNextThinkTick();
	float thinktime = thinktick * TICK_INTERVAL;

	// Is the next think too far out, or non-existent?
	// BUGBUG: Interpolation is going to look bad in here.  But it should only
	// be for dead things - and those should be ragdolls (client-side sim) anyway.
	// UNDONE: Remove this and assert?  Force MOVETYPE_STEP objs to become MOVETYPE_TOSS when
	// they aren't thinking?
	// UNDONE: this happens as the first frame for a bunch of things like dynamically created ents.
	// can't remove until initial conditions are resolved
	float deltaThink = thinktime - gpGlobals->curtime;
	if ( thinktime <= 0 || deltaThink > 0.5 )
	{
		PhysicsStepRunTimestep( gpGlobals->frametime );
		PhysicsCheckWaterTransition();
		GetEngineObject()->SetLastThinkTick( TIME_TO_TICKS(gpGlobals->curtime) );
		UpdatePhysicsShadowToCurrentPosition(gpGlobals->frametime);
		PhysicsRelinkChildren(gpGlobals->frametime);
		return;
	}

	Vector oldOrigin = GetEngineObject()->GetAbsOrigin();

	// Feed the position delta back from vphysics if enabled
	bool updateFromVPhysics = npc_vphysics.GetBool();
	if (GetEngineObject()->HasDataObjectType(VPHYSICSUPDATEAI) )
	{
		vphysicsupdateai_t *pUpdate = static_cast<vphysicsupdateai_t *>(GetEngineObject()->GetDataObject( VPHYSICSUPDATEAI ));
		if ( pUpdate->stopUpdateTime > gpGlobals->curtime )
		{
			updateFromVPhysics = true;
		}
		else
		{
			float maxAngular;
			GetEngineObject()->VPhysicsGetObject()->GetShadowController()->GetMaxSpeed( NULL, &maxAngular );
			GetEngineObject()->VPhysicsGetObject()->GetShadowController()->MaxSpeed( pUpdate->savedShadowControllerMaxSpeed, maxAngular );
			GetEngineObject()->DestroyDataObject(VPHYSICSUPDATEAI);
		}
	}

	if ( updateFromVPhysics && GetEngineObject()->VPhysicsGetObject() && !GetEngineObject()->GetMoveParent() )
	{
		Vector position;
		GetEngineObject()->VPhysicsGetObject()->GetShadowPosition( &position, NULL );
		float delta = (GetEngineObject()->GetAbsOrigin() - position).LengthSqr();
		// for now, use a tolerance of 1 inch for these tests
		if ( delta < 1 )
		{
			// physics is really close, check to see if my current position is valid.
			// If so, ignore the physics result.
			trace_t tr;
			Physics_TraceEntity( this, GetEngineObject()->GetAbsOrigin(), GetEngineObject()->GetAbsOrigin(), PhysicsSolidMaskForEntity(), &tr );
			updateFromVPhysics = tr.startsolid;
		}
		if ( updateFromVPhysics )
		{
			GetEngineObject()->SetAbsOrigin( position );
			GetEngineObject()->PhysicsTouchTriggers();
		}
		//NDebugOverlay::Box( position, WorldAlignMins(), WorldAlignMaxs(), 255, 255, 0, 0, 0.0 );
	}

	// not going to think, don't run game physics either
	if ( thinktick > gpGlobals->tickcount )
		return;
	
	// Don't let things stay in the past.
	//  it is possible to start that way
	//  by a trigger with a local time.
	if ( thinktime < gpGlobals->curtime )
	{
		thinktime = gpGlobals->curtime;	
	}

	// simulate over the timestep
	float dt = thinktime - GetEngineObject()->GetLastThink();

	// Now run step simulator
	StepSimulationThink( dt );

	PhysicsCheckWaterTransition();

	if (GetEngineObject()->VPhysicsGetObject() )
	{
		if ( !VectorCompare( oldOrigin, GetEngineObject()->GetAbsOrigin() ) )
		{
			GetEngineObject()->VPhysicsGetObject()->UpdateShadow(GetEngineObject()->GetAbsOrigin(), vec3_angle, (GetEngineObject()->GetFlags() & FL_FLY) ? true : false, dt );
		}
	}
	PhysicsRelinkChildren(dt);
}

// Check to see what (if anything) this MOVETYPE_STEP entity is standing on
void CBaseEntity::PhysicsStepRecheckGround()
{
	unsigned int mask = PhysicsSolidMaskForEntity();
	// determine if it's on solid ground at all
	Vector	mins, maxs, point;
	int		x, y;
	trace_t trace;
	
	VectorAdd (GetEngineObject()->GetAbsOrigin(), GetEngineObject()->WorldAlignMins(), mins);
	VectorAdd (GetEngineObject()->GetAbsOrigin(), GetEngineObject()->WorldAlignMaxs(), maxs);
	point[2] = mins[2] - 1;
	for	(x=0 ; x<=1 ; x++)
	{
		for	(y=0 ; y<=1 ; y++)
		{
			point[0] = x ? maxs[0] : mins[0];
			point[1] = y ? maxs[1] : mins[1];

			ICollideable *pCollision = GetCollideable();

			if ( pCollision && IsNPC() )
			{
				EntityList()->GetEngineWorld()->TraceLineFilterEntity( this->GetEngineObject(), point, point, mask, COLLISION_GROUP_NONE, &trace);
			}
			else
			{
				UTIL_TraceLine( point, point, mask, this, COLLISION_GROUP_NONE, &trace );
			}

			if ( trace.startsolid )
			{
				GetEngineObject()->SetGroundEntity((CBaseEntity*)trace.m_pEnt ? ((CBaseEntity*)trace.m_pEnt)->GetEngineObject() : NULL);
				return;
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : timestep - 
//-----------------------------------------------------------------------------
void CBaseEntity::PhysicsStepRunTimestep( float timestep )
{
	bool	wasonground;
	bool	inwater;
	bool	hitsound = false;
	float	speed, newspeed, control;
	float	friction;

	PhysicsCheckVelocity();

	wasonground = (GetEngineObject()->GetFlags() & FL_ONGROUND ) ? true : false;

	// add gravity except:
	//   flying monsters
	//   swimming monsters who are in the water
	inwater = PhysicsCheckWater();

	bool isfalling = false;

	if ( !wasonground )
	{
		if ( !(GetEngineObject()->GetFlags() & FL_FLY ) )
		{
			if ( !( (GetEngineObject()->GetFlags() & FL_SWIM ) && ( GetWaterLevel() > 0 ) ) )
			{
				if (GetEngineObject()->GetAbsVelocity()[2] < ( GetCurrentGravity() * -0.1 ) )
				{
					hitsound = true;
				}

				if ( !inwater )
				{
					PhysicsAddHalfGravity( timestep );
					isfalling = true;
				}
			}
		}
	}

	if ( !(GetEngineObject()->GetFlags() & FL_STEPMOVEMENT) &&
		(!VectorCompare(GetEngineObject()->GetAbsVelocity(), vec3_origin) ||
		 !VectorCompare(GetBaseVelocity(), vec3_origin)))
	{
		Vector vecAbsVelocity = GetEngineObject()->GetAbsVelocity();

		GetEngineObject()->SetGroundEntity( NULL );
		// apply friction
		// let dead monsters who aren't completely onground slide
		if ( wasonground )
		{
			speed = VectorLength( vecAbsVelocity );
			if (speed)
			{
				friction = sv_friction.GetFloat() * GetEngineObject()->GetFriction();

				control = speed < sv_stopspeed.GetFloat() ? sv_stopspeed.GetFloat() : speed;
				newspeed = speed - timestep*control*friction;

				if (newspeed < 0)
					newspeed = 0;
				newspeed /= speed;

				vecAbsVelocity[0] *= newspeed;
				vecAbsVelocity[1] *= newspeed;
			}
		}

		vecAbsVelocity += GetBaseVelocity();
		GetEngineObject()->SetAbsVelocity( vecAbsVelocity );

		// Apply angular velocity
		SimulateAngles( timestep );

		PhysicsCheckVelocity();

		PhysicsTryMove( timestep, NULL );

		PhysicsCheckVelocity();

		vecAbsVelocity = GetEngineObject()->GetAbsVelocity();
		vecAbsVelocity -= GetBaseVelocity();
		GetEngineObject()->SetAbsVelocity( vecAbsVelocity );

		PhysicsCheckVelocity();

		if ( !(GetEngineObject()->GetFlags() & FL_ONGROUND) )
		{
			PhysicsStepRecheckGround();
		}

		GetEngineObject()->PhysicsTouchTriggers();
	}

	if (!(GetEngineObject()->GetFlags() & FL_ONGROUND ) && isfalling)
	{
		PhysicsAddHalfGravity( timestep );
	}
}

// After this long, if a player isn't updating, then return it's projectiles to server control
#define PLAYER_PACKETS_STOPPED_SO_RETURN_TO_PHYSICS_TIME 1.0f

void Physics_SimulateEntity( CBaseEntity *pEntity )
{
	VPROF( ( !vprof_scope_entity_gamephys.GetBool() ) ? 
			"Physics_SimulateEntity" : 
		EntityList()->GetCannonicalName( pEntity->GetClassname() ) );

	if (pEntity->IsNetworkable() && pEntity->entindex()!=-1 )
	{
//#if !defined( NO_ENTITY_PREDICTION )
//		// Player drives simulation of this entity
//		if ( pEntity->IsPlayerSimulated() )
//		{
//			// If the player is gone, dropped, crashed, then return
//			//  control to the game code.
//			CBasePlayer *simulatingPlayer = pEntity->GetSimulatingPlayer();
//			if ( simulatingPlayer &&
//				( simulatingPlayer->GetTimeBase() > gpGlobals->curtime - PLAYER_PACKETS_STOPPED_SO_RETURN_TO_PHYSICS_TIME ) )
//			{
//				// Okay, the guy is still around
//				return;
//			}
//
//			pEntity->UnsetPlayerSimulated();
//		}
//#endif

		MDLCACHE_CRITICAL_SECTION();

//#if !defined( NO_ENTITY_PREDICTION )
//		// If an object was at one point player simulated, but had that status revoked (as just
//		//  above when no packets have arrived in a while ), then we still will assume that the
//		//  owner/player will be predicting the entity locally (even if the game is playing like butt)
//		//  and so we won't spam that player with additional network data such as effects/sounds 
//		//  that are theoretically being predicted by the player anyway.
//		if ( pEntity->m_PredictableID->IsActive() )
//		{
//			CBasePlayer *playerowner = ToBasePlayer( pEntity->GetOwnerEntity() );
//			if ( playerowner )
//			{
//				CBasePlayer *pl = ToBasePlayer( EntityList()->GetPlayerByIndex( pEntity->m_PredictableID->GetPlayer() + 1 ) );
//				// Is the player who created it still the owner?
//				if ( pl == playerowner )
//				{
//					// Set up to suppress sending events to owner player
//					if ( pl->IsPredictingWeapons() )
//					{
//						IPredictionSystem::SuppressHostEvents( playerowner );
//					}
//				}
//			}	
//			{
//				VPROF( ( !vprof_scope_entity_gamephys.GetBool() ) ? 
//						"pEntity->PhysicsSimulate" : 
//						EntityFactoryDictionary()->GetCannonicalName( pEntity->GetClassname() ) );
//
//				// Run entity physics
//				pEntity->PhysicsSimulate();
//			}
//
//			// Restore suppression filter
//			IPredictionSystem::SuppressHostEvents( NULL );
//		}
//		else
//#endif
		//{
			// Run entity physics
			pEntity->PhysicsSimulate();
		//}
	}
	else
	{
		pEntity->GetEngineObject()->PhysicsRunThink();
	}
}

extern bool g_bTestMoveTypeStepSimulation;
extern ConVar sv_teststepsimulation;
//-----------------------------------------------------------------------------
// Purpose: Runs the main physics simulation loop against all entities ( except players )
//-----------------------------------------------------------------------------
void Physics_RunThinkFunctions( bool simulating )
{
	VPROF( "Physics_RunThinkFunctions");

	g_bTestMoveTypeStepSimulation = sv_teststepsimulation.GetBool();

	float starttime = gpGlobals->curtime;
	// clear all entites freed outside of this loop
	EntityList()->CleanupDeleteList();

	if ( !simulating )
	{
		// only simulate players
		for ( int i = 1; i <= gpGlobals->maxClients; i++ )
		{
			CBasePlayer *pPlayer = ToBasePlayer(EntityList()->GetPlayerByIndex( i ));
			if ( pPlayer )
			{
				// Always reset clock to real sv.time
				gpGlobals->curtime = starttime;
				// Force usercmd processing even though gpGlobals->tickcount isn't incrementing
				pPlayer->ForceSimulation();
				Physics_SimulateEntity( pPlayer );
			}
		}
	}
	else
	{
		EntityList()->DisableDestroyImmediate();
		int listMax = EntityList()->SimThink_ListCount();
		listMax = MAX(listMax,1);
		CBaseEntity **list = (CBaseEntity **)stackalloc( sizeof(CBaseEntity *) * listMax );
		// iterate through all entities and have them think or simulate
		
		// UNDONE: This has problems with UTIL_RemoveImmediate() (now disabled during this loop).  
		// Do we really need UTIL_RemoveImmediate()?
		int count = EntityList()->SimThink_ListCopy( list, listMax );

		//DevMsg(1, "Count: %d\n", count );
		for ( int i = 0; i < count; i++ )
		{
			if ( !list[i] )
				continue;
			// Always reset clock to real sv.time
			gpGlobals->curtime = starttime;
			Physics_SimulateEntity( list[i] );
		}

		stackfree( list );
		EntityList()->EnableDestroyImmediate();
	}

	gpGlobals->curtime = starttime;
}

void CBaseEntity::StartGroundContact(CBaseEntity* ground)
{
	GetEngineObject()->AddFlag(FL_ONGROUND);
	//	Msg( "+++ %s starting contact with ground %s\n", GetClassname(), ground->GetClassname() );
}

void CBaseEntity::EndGroundContact(CBaseEntity* ground)
{
	GetEngineObject()->RemoveFlag(FL_ONGROUND);
	//	Msg( "--- %s ending contact with ground %s\n", GetClassname(), ground->GetClassname() );
}
