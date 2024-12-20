//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

//#include "cbase.h"
#include "baseentity_shared.h"
#include "sendproxy.h"
#include "entitylist.h"
#ifdef _WIN32
#include "typeinfo"
// BUGBUG: typeinfo stomps some of the warning settings (in yvals.h)
#pragma warning(disable:4244)
#elif POSIX
#include <typeinfo>
#else
#error "need typeinfo defined"
#endif
#include "utlvector.h"
#include "igamesystem.h"
#include "collisionutils.h"
#include "UtlSortVector.h"
#include "tier0/vprof.h"
#include "mapentities.h"

#include "datacache/imdlcache.h"
#include "positionwatcher.h"
#include "mapentities_shared.h"
#include "tier1/mempool.h"
#include "saverestore_utlvector.h"
#include "tier0/vcrmode.h"
#include "coordsize.h"
#include "physics_saverestore.h"
#include "animation.h"
#include "vphysics/constraints.h"
#include "mathlib/polyhedron.h"
#include "model_types.h"
#include "te_effect_dispatch.h"
#include "movevars_shared.h"
#include "vehicle_base.h"
#include "in_buttons.h"
#include "rope_shared.h"
#include "rope_helpers.h"
#include "bone_setup.h"
#include "ragdoll_shared.h"
#include "physics_shared.h"
#include "player.h"
#include "vphysics/collision_set.h"
#include "env_debughistory.h"
#include "physics_prop_ragdoll.h"
#include "hl2_gamerules.h"
#include "portal_util_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar ent_debugkeys;
extern ConVar think_limit;

//extern bool ParseKeyvalue(void* pObject, typedescription_t* pFields, int iNumFields, const char* szKeyName, const char* szValue);
//extern bool ExtractKeyvalue(void* pObject, typedescription_t* pFields, int iNumFields, const char* szKeyName, char* szValue, int iMaxLen);

CGlobalEntityList<CBaseEntity> gEntList;
CBaseEntityList<CBaseEntity>* g_pEntityList = &gEntList;

// Expose list to engine
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CGlobalEntityList, IServerEntityList, VSERVERENTITYLIST_INTERFACE_VERSION, gEntList);

// Store local pointer to interface for rest of client .dll only 
//  (CClientEntityList instead of IClientEntityList )
CGlobalEntityList<CBaseEntity>* sv_entitylist = &gEntList;

//static servertouchlink_t *g_pNextLink = NULL;
int linksallocated = 0;
int groundlinksallocated = 0;
// memory pool for storing links between entities
static CUtlMemoryPool g_EdictTouchLinks(sizeof(servertouchlink_t), MAX_EDICTS, CUtlMemoryPool::GROW_NONE, "g_EdictTouchLinks");
static CUtlMemoryPool g_EntityGroundLinks(sizeof(servergroundlink_t), MAX_EDICTS, CUtlMemoryPool::GROW_NONE, "g_EntityGroundLinks");
#ifndef CLIENT_DLL
ConVar debug_touchlinks("debug_touchlinks", "0", 0, "Spew touch link activity");
#define DebugTouchlinks() debug_touchlinks.GetBool()
#else
#define DebugTouchlinks() false
#endif
#if !defined( CLIENT_DLL )
static ConVar sv_thinktimecheck("sv_thinktimecheck", "0", 0, "Check for thinktimes all on same timestamp.");
#endif

bool g_bTestMoveTypeStepSimulation = true;
ConVar sv_teststepsimulation("sv_teststepsimulation", "1", 0);
ConVar step_spline("step_spline", "0");

static ConVar sv_portal_collision_sim_bounds_x("sv_portal_collision_sim_bounds_x", "200", FCVAR_REPLICATED, "Size of box used to grab collision geometry around placed portals. These should be at the default size or larger only!");
static ConVar sv_portal_collision_sim_bounds_y("sv_portal_collision_sim_bounds_y", "200", FCVAR_REPLICATED, "Size of box used to grab collision geometry around placed portals. These should be at the default size or larger only!");
static ConVar sv_portal_collision_sim_bounds_z("sv_portal_collision_sim_bounds_z", "252", FCVAR_REPLICATED, "Size of box used to grab collision geometry around placed portals. These should be at the default size or larger only!");
ConVar sv_portal_trace_vs_world("sv_portal_trace_vs_world", "1", FCVAR_REPLICATED | FCVAR_CHEAT, "Use traces against portal environment world geometry");
ConVar sv_portal_trace_vs_displacements("sv_portal_trace_vs_displacements", "1", FCVAR_REPLICATED | FCVAR_CHEAT, "Use traces against portal environment displacement geometry");
ConVar sv_portal_trace_vs_holywall("sv_portal_trace_vs_holywall", "1", FCVAR_REPLICATED | FCVAR_CHEAT, "Use traces against portal environment carved wall");
ConVar sv_portal_trace_vs_staticprops("sv_portal_trace_vs_staticprops", "1", FCVAR_REPLICATED | FCVAR_CHEAT, "Use traces against portal environment static prop geometry");
ConVar sv_use_transformed_collideables("sv_use_transformed_collideables", "1", FCVAR_REPLICATED | FCVAR_CHEAT, "Disables traces against remote portal moving entities using transforms to bring them into local space.");
ConVar sv_debug_physicsshadowclones("sv_debug_physicsshadowclones", "0", FCVAR_REPLICATED);
ConVar r_vehicleBrakeRate("r_vehicleBrakeRate", "1.5", FCVAR_CHEAT);
ConVar xbox_throttlebias("xbox_throttlebias", "100", FCVAR_ARCHIVE);
ConVar xbox_throttlespoof("xbox_throttlespoof", "200", FCVAR_ARCHIVE);
ConVar xbox_autothrottle("xbox_autothrottle", "1", FCVAR_ARCHIVE);
ConVar xbox_steering_deadzone("xbox_steering_deadzone", "0.0");
ConVar ai_setupbones_debug("ai_setupbones_debug", "0", 0, "Shows that bones that are setup every think");
ConVar	sv_alternateticks("sv_alternateticks", (IsX360()) ? "1" : "0", FCVAR_SPONLY, "If set, server only simulates entities on even numbered ticks.\n");
ConVar g_ragdoll_important_maxcount("g_ragdoll_important_maxcount", "2", FCVAR_REPLICATED);
//-----------------------------------------------------------------------------
// LRU
//-----------------------------------------------------------------------------
#ifdef _XBOX
// xbox defaults to 4 ragdolls max
ConVar g_ragdoll_maxcount("g_ragdoll_maxcount", "4", FCVAR_REPLICATED);
#else
ConVar g_ragdoll_maxcount("g_ragdoll_maxcount", "8", FCVAR_REPLICATED);
#endif
ConVar g_debug_ragdoll_removal("g_debug_ragdoll_removal", "0", FCVAR_REPLICATED | FCVAR_CHEAT);
ConVar sv_fullsyncclones("sv_fullsyncclones", "1", FCVAR_CHEAT);
void TimescaleChanged(IConVar* var, const char* pOldString, float flOldValue)
{
	if (gEntList.PhysGetEnv())
	{
		gEntList.PhysGetEnv()->ResetSimulationClock();
	}
}
ConVar phys_timescale("phys_timescale", "1", 0, "Scale time for physics", TimescaleChanged);
ConVar phys_speeds("phys_speeds", "0");
#if _DEBUG
ConVar phys_dontprintint("phys_dontprintint", "1", FCVAR_NONE, "Don't print inter-penetration warnings.");
#endif
static ConVar phys_penetration_error_time("phys_penetration_error_time", "10", 0, "Controls the duration of vphysics penetration error boxes.");
static int g_iShadowCloneCount = 0;
ConVar sv_use_shadow_clones("sv_use_shadow_clones", "1", FCVAR_REPLICATED | FCVAR_CHEAT); //should we create shadow clones?


//-----------------------------------------------------------------------------
// Call these in pre-save + post save
//-----------------------------------------------------------------------------
void CEntitySaveUtils::PreSave()
{
	Assert(!m_pLevelAdjacencyDependencyHash);
	MEM_ALLOC_CREDIT();
	m_pLevelAdjacencyDependencyHash = gEntList.Physics()->CreateObjectPairHash();
}

void CEntitySaveUtils::PostSave()
{
	gEntList.Physics()->DestroyObjectPairHash(m_pLevelAdjacencyDependencyHash);
	m_pLevelAdjacencyDependencyHash = NULL;
}


//-----------------------------------------------------------------------------
// Gets the # of dependencies for a particular entity
//-----------------------------------------------------------------------------
int CEntitySaveUtils::GetEntityDependencyCount(CBaseEntity* pEntity)
{
	return m_pLevelAdjacencyDependencyHash->GetPairCountForObject(pEntity);
}


//-----------------------------------------------------------------------------
// Gets all dependencies for a particular entity
//-----------------------------------------------------------------------------
int CEntitySaveUtils::GetEntityDependencies(CBaseEntity* pEntity, int nCount, CBaseEntity** ppEntList)
{
	return m_pLevelAdjacencyDependencyHash->GetPairListForObject(pEntity, nCount, (void**)ppEntList);
}


//-----------------------------------------------------------------------------
// Methods of IEntitySaveUtils
//-----------------------------------------------------------------------------
void CEntitySaveUtils::AddLevelTransitionSaveDependency(CBaseEntity* pEntity1, CBaseEntity* pEntity2)
{
	if (pEntity1 != pEntity2)
	{
		m_pLevelAdjacencyDependencyHash->AddObjectPair(pEntity1, pEntity2);
	}
}

static bool IsDebris(int collisionGroup)
{
	switch (collisionGroup)
	{
	case COLLISION_GROUP_DEBRIS:
	case COLLISION_GROUP_INTERACTIVE_DEBRIS:
	case COLLISION_GROUP_DEBRIS_TRIGGER:
		return true;
	default:
		break;
	}
	return false;
}

// a little debug wrapper to help fix bugs when entity pointers get trashed
#if 0
struct physcheck_t
{
	IPhysicsObject* pPhys;
	char			string[512];
};

CUtlVector< physcheck_t > physCheck;

void PhysCheckAdd(IPhysicsObject* pPhys, const char* pString)
{
	physcheck_t tmp;
	tmp.pPhys = pPhys;
	Q_strncpy(tmp.string, pString, sizeof(tmp.string));
	physCheck.AddToTail(tmp);
}

const char* PhysCheck(IPhysicsObject* pPhys)
{
	for (int i = 0; i < physCheck.Size(); i++)
	{
		if (physCheck[i].pPhys == pPhys)
			return physCheck[i].string;
	}

	return "unknown";
}
#endif

// vehicle wheels can only collide with things that can't get stuck in them during game physics
// because they aren't in the game physics world at present
static bool WheelCollidesWith(IPhysicsObject* pObj, CBaseEntity* pEntity)
{
#if defined( INVASION_DLL )
	if (pEntity->GetEngineObject()->GetCollisionGroup() == TFCOLLISION_GROUP_OBJECT)
		return false;
#endif

	// Cull against interactive debris
	if (pEntity->GetEngineObject()->GetCollisionGroup() == COLLISION_GROUP_INTERACTIVE_DEBRIS)
		return false;

	// Hit physics ents
	if (pEntity->GetEngineObject()->GetMoveType() == MOVETYPE_PUSH || pEntity->GetEngineObject()->GetMoveType() == MOVETYPE_VPHYSICS || pObj->IsStatic())
		return true;

	return false;
}

CCollisionEvent::CCollisionEvent()
{
	m_inCallback = 0;
	m_bBufferTouchEvents = false;
	m_lastTickFrictionError = 0;
}

int CCollisionEvent::ShouldCollide(IPhysicsObject* pObj0, IPhysicsObject* pObj1, void* pGameData0, void* pGameData1)
#if _DEBUG
{
	int x0 = ShouldCollide_2(pObj0, pObj1, pGameData0, pGameData1);
	int x1 = ShouldCollide_2(pObj1, pObj0, pGameData1, pGameData0);
	Assert(x0 == x1);
	return x0;
}
int CCollisionEvent::ShouldCollide_2(IPhysicsObject* pObj0, IPhysicsObject* pObj1, void* pGameData0, void* pGameData1)
#endif
{
	CallbackContext check(this);

	CBaseEntity* pEntity0 = static_cast<CBaseEntity*>(pGameData0);
	CBaseEntity* pEntity1 = static_cast<CBaseEntity*>(pGameData1);

	if (!pEntity0 || !pEntity1)
		return 1;

	unsigned short gameFlags0 = pObj0->GetGameFlags();
	unsigned short gameFlags1 = pObj1->GetGameFlags();

	if (pEntity0 == pEntity1)
	{
		// allow all-or-nothing per-entity disable
		if ((gameFlags0 | gameFlags1) & FVPHYSICS_NO_SELF_COLLISIONS)
			return 0;

		IPhysicsCollisionSet* pSet = gEntList.Physics()->FindCollisionSet(pEntity0->GetEngineObject()->GetModelIndex());
		if (pSet)
			return pSet->ShouldCollide(pObj0->GetGameIndex(), pObj1->GetGameIndex());

		return 1;
	}

	// objects that are both constrained to the world don't collide with each other
	if ((gameFlags0 & gameFlags1) & FVPHYSICS_CONSTRAINT_STATIC)
	{
		return 0;
	}

	// Special collision rules for vehicle wheels
	// Their entity collides with stuff using the normal rules, but they
	// have different rules than the vehicle body for various reasons.
	// sort of a hack because we don't have spheres to represent them in the game
	// world for speculative collisions.
	if (pObj0->GetCallbackFlags() & CALLBACK_IS_VEHICLE_WHEEL)
	{
		if (!WheelCollidesWith(pObj1, pEntity1))
			return false;
	}
	if (pObj1->GetCallbackFlags() & CALLBACK_IS_VEHICLE_WHEEL)
	{
		if (!WheelCollidesWith(pObj0, pEntity0))
			return false;
	}

	if (pEntity0->ForceVPhysicsCollide(pEntity1) || pEntity1->ForceVPhysicsCollide(pEntity0))
		return 1;

	if (pEntity0->entindex() != -1 && pEntity1->entindex() != -1)
	{
		// don't collide with your owner
		if (pEntity0->GetOwnerEntity() == pEntity1 || pEntity1->GetOwnerEntity() == pEntity0)
			return 0;
	}

	if (pEntity0->GetEngineObject()->GetMoveParent() || pEntity1->GetEngineObject()->GetMoveParent())
	{
		CBaseEntity* pParent0 = pEntity0->GetEngineObject()->GetRootMoveParent()->GetOuter();
		CBaseEntity* pParent1 = pEntity1->GetEngineObject()->GetRootMoveParent()->GetOuter();

		// NOTE: Don't let siblings/parents collide.  If you want this behavior, do it
		// with constraints, not hierarchy!
		if (pParent0 == pParent1)
			return 0;

		if (gEntList.PhysGetEntityCollisionHash()->IsObjectPairInHash(pParent0, pParent1))
			return 0;

		IPhysicsObject* p0 = pParent0->GetEngineObject()->VPhysicsGetObject();
		IPhysicsObject* p1 = pParent1->GetEngineObject()->VPhysicsGetObject();
		if (p0 && p1)
		{
			if (gEntList.PhysGetEntityCollisionHash()->IsObjectPairInHash(p0, p1))
				return 0;
		}
	}

	int solid0 = pEntity0->GetEngineObject()->GetSolid();
	int solid1 = pEntity1->GetEngineObject()->GetSolid();
	int nSolidFlags0 = pEntity0->GetEngineObject()->GetSolidFlags();
	int nSolidFlags1 = pEntity1->GetEngineObject()->GetSolidFlags();

	int movetype0 = pEntity0->GetEngineObject()->GetMoveType();
	int movetype1 = pEntity1->GetEngineObject()->GetMoveType();

	// entities with non-physical move parents or entities with MOVETYPE_PUSH
	// are considered as "AI movers".  They are unchanged by collision; they exert
	// physics forces on the rest of the system.
	bool aiMove0 = (movetype0 == MOVETYPE_PUSH) ? true : false;
	bool aiMove1 = (movetype1 == MOVETYPE_PUSH) ? true : false;

	if (pEntity0->GetEngineObject()->GetMoveParent())
	{
		// if the object & its parent are both MOVETYPE_VPHYSICS, then this must be a special case
		// like a prop_ragdoll_attached
		if (!(movetype0 == MOVETYPE_VPHYSICS && pEntity0->GetEngineObject()->GetRootMoveParent()->GetMoveType() == MOVETYPE_VPHYSICS))
		{
			aiMove0 = true;
		}
	}
	if (pEntity1->GetEngineObject()->GetMoveParent())
	{
		// if the object & its parent are both MOVETYPE_VPHYSICS, then this must be a special case.
		if (!(movetype1 == MOVETYPE_VPHYSICS && pEntity1->GetEngineObject()->GetRootMoveParent()->GetMoveType() == MOVETYPE_VPHYSICS))
		{
			aiMove1 = true;
		}
	}

	// AI movers don't collide with the world/static/pinned objects or other AI movers
	if ((aiMove0 && !pObj1->IsMoveable()) ||
		(aiMove1 && !pObj0->IsMoveable()) ||
		(aiMove0 && aiMove1))
		return 0;

	// two objects under shadow control should not collide.  The AI will figure it out
	if (pObj0->GetShadowController() && pObj1->GetShadowController())
		return 0;

	// BRJ 1/24/03
	// You can remove the assert if it's problematic; I *believe* this condition
	// should be met, but I'm not sure.
	//Assert ( (solid0 != SOLID_NONE) && (solid1 != SOLID_NONE) );
	if ((solid0 == SOLID_NONE) || (solid1 == SOLID_NONE))
		return 0;

	// not solid doesn't collide with anything
	if ((nSolidFlags0 | nSolidFlags1) & FSOLID_NOT_SOLID)
	{
		// might be a vphysics trigger, collide with everything but "not solid"
		if (pObj0->IsTrigger() && !(nSolidFlags1 & FSOLID_NOT_SOLID))
			return 1;
		if (pObj1->IsTrigger() && !(nSolidFlags0 & FSOLID_NOT_SOLID))
			return 1;

		return 0;
	}

	if ((nSolidFlags0 & FSOLID_TRIGGER) &&
		!(solid1 == SOLID_VPHYSICS || solid1 == SOLID_BSP || movetype1 == MOVETYPE_VPHYSICS))
		return 0;

	if ((nSolidFlags1 & FSOLID_TRIGGER) &&
		!(solid0 == SOLID_VPHYSICS || solid0 == SOLID_BSP || movetype0 == MOVETYPE_VPHYSICS))
		return 0;

	if (!g_pGameRules->ShouldCollide(pEntity0->GetEngineObject()->GetCollisionGroup(), pEntity1->GetEngineObject()->GetCollisionGroup()))
		return 0;

	// check contents
	if (!(pObj0->GetContents() & pEntity1->PhysicsSolidMaskForEntity()) || !(pObj1->GetContents() & pEntity0->PhysicsSolidMaskForEntity()))
		return 0;

	if (gEntList.PhysGetEntityCollisionHash()->IsObjectPairInHash(pGameData0, pGameData1))
		return 0;

	if (gEntList.PhysGetEntityCollisionHash()->IsObjectPairInHash(pObj0, pObj1))
		return 0;

	return 1;
}

bool FindMaxContact(IPhysicsObject* pObject, float minForce, IPhysicsObject** pOtherObject, Vector* contactPos, Vector* pForce)
{
	float mass = pObject->GetMass();
	float maxForce = minForce;
	*pOtherObject = NULL;
	IPhysicsFrictionSnapshot* pSnapshot = pObject->CreateFrictionSnapshot();
	while (pSnapshot->IsValid())
	{
		IPhysicsObject* pOther = pSnapshot->GetObject(1);
		if (pOther->IsMoveable() && pOther->GetMass() > mass)
		{
			float force = pSnapshot->GetNormalForce();
			if (force > maxForce)
			{
				*pOtherObject = pOther;
				pSnapshot->GetContactPoint(*contactPos);
				pSnapshot->GetSurfaceNormal(*pForce);
				*pForce *= force;
			}
		}
		pSnapshot->NextFrictionData();
	}
	pObject->DestroyFrictionSnapshot(pSnapshot);
	if (*pOtherObject)
		return true;

	return false;
}

CBaseEntity* Physics_CreateSolver(CBaseEntity* pMovingEntity, CBaseEntity* pPhysicsObject, bool disableCollisions, float separationDuration)
{
	return pMovingEntity->EntityPhysics_CreateSolver(pPhysicsObject, disableCollisions, separationDuration);
}

bool CCollisionEvent::ShouldFreezeObject(IPhysicsObject* pObject)
{
	// for now, don't apply a per-object limit to ai MOVETYPE_PUSH objects
	// NOTE: If this becomes a problem (too many collision checks this tick) we should add a path
	// to inform the logic in VPhysicsUpdatePusher() about the limit being applied so 
	// that it doesn't falsely block the object when it's simply been temporarily frozen
	// for performance reasons
	CBaseEntity* pEntity = static_cast<CBaseEntity*>(pObject->GetGameData());
	if (pEntity)
	{
		if (pEntity->GetEngineObject()->GetMoveType() == MOVETYPE_PUSH)
			return false;

		// don't limit vehicle collisions either, limit can make breaking through a pile of breakable
		// props very hitchy
		if (pEntity->GetServerVehicle() && !(pObject->GetCallbackFlags() & CALLBACK_IS_VEHICLE_WHEEL))
			return false;
	}

	// if we're freezing a debris object, then it's probably due to some kind of solver issue
	// usually this is a large object resting on the debris object in question which is not
	// very stable.
	// After doing the experiment of constraining the dynamic range of mass while solving friction
	// contacts, I like the results of this tradeoff better.  So damage or remove the debris object
	// wherever possible once we hit this case:
	if (pEntity && IsDebris(pEntity->GetEngineObject()->GetCollisionGroup()) && !pEntity->IsNPC())
	{
		IPhysicsObject* pOtherObject = NULL;
		Vector contactPos;
		Vector force;
		// find the contact with the moveable object applying the most contact force
		if (FindMaxContact(pObject, pObject->GetMass() * 10, &pOtherObject, &contactPos, &force))
		{
			CBaseEntity* pOther = static_cast<CBaseEntity*>(pOtherObject->GetGameData());
			// this object can take damage, crush it
			if (pEntity->m_takedamage > DAMAGE_EVENTS_ONLY)
			{
				CTakeDamageInfo dmgInfo(pOther, pOther, force, contactPos, force.Length() * 0.1f, DMG_CRUSH);
				gEntList.PhysCallbackDamage(pEntity, dmgInfo);
			}
			else
			{
				// can't be damaged, so do something else:
				if (pEntity->IsGib())
				{
					// it's always safe to delete gibs, so kill this one to avoid simulation problems
					gEntList.PhysCallbackRemove(pEntity);
				}
				else
				{
					// not a gib, create a solver:
					// UNDONE: Add a property to override this in gameplay critical scenarios?
					gEntList.PhysGetPostSimulationQueue().QueueCall(Physics_CreateSolver, pOther, pEntity, true, 1.0f);
				}
			}
		}
	}
	return true;
}

bool CCollisionEvent::ShouldFreezeContacts(IPhysicsObject** pObjectList, int objectCount)
{
	if (m_lastTickFrictionError > gpGlobals->tickcount || m_lastTickFrictionError < (gpGlobals->tickcount - 1))
	{
		DevWarning("Performance Warning: large friction system (%d objects)!!!\n", objectCount);
#if _DEBUG
		for (int i = 0; i < objectCount; i++)
		{
			CBaseEntity* pEntity = static_cast<CBaseEntity*>(pObjectList[i]->GetGameData());
			pEntity->m_debugOverlays |= OVERLAY_ABSBOX_BIT | OVERLAY_PIVOT_BIT;
		}
#endif
	}
	m_lastTickFrictionError = gpGlobals->tickcount;
	return false;
}

// NOTE: these are fully edge triggered events 
// called when an object wakes up (starts simulating)
void CCollisionEvent::ObjectWake(IPhysicsObject* pObject)
{
	CBaseEntity* pEntity = static_cast<CBaseEntity*>(pObject->GetGameData());
	if (pEntity && pEntity->GetEngineObject()->HasDataObjectType(VPHYSICSWATCHER))
	{
		//ReportVPhysicsStateChanged( pObject, pEntity, true );
		pEntity->NotifyVPhysicsStateChanged(pObject, true);
	}
}
// called when an object goes to sleep (no longer simulating)
void CCollisionEvent::ObjectSleep(IPhysicsObject* pObject)
{
	CBaseEntity* pEntity = static_cast<CBaseEntity*>(pObject->GetGameData());
	if (pEntity && pEntity->GetEngineObject()->HasDataObjectType(VPHYSICSWATCHER))
	{
		//ReportVPhysicsStateChanged( pObject, pEntity, false );
		pEntity->NotifyVPhysicsStateChanged(pObject, false);
	}
}


extern const ConVar* g_pDeveloper;
static void ReportPenetration(CBaseEntity* pEntity, float duration)
{
	if (pEntity->GetEngineObject()->GetMoveType() == MOVETYPE_VPHYSICS)
	{
		if (g_pDeveloper->GetInt() > 1)
		{
			pEntity->m_debugOverlays |= OVERLAY_ABSBOX_BIT;
		}

		pEntity->AddTimedOverlay(UTIL_VarArgs("VPhysics Penetration Error (%s)!", pEntity->GetDebugName()), duration);
	}
}

static void UpdateEntityPenetrationFlag(CBaseEntity* pEntity, bool isPenetrating)
{
	if (!pEntity)
		return;
	IPhysicsObject* pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	int count = pEntity->GetEngineObject()->VPhysicsGetObjectList(pList, ARRAYSIZE(pList));
	for (int i = 0; i < count; i++)
	{
		if (!pList[i]->IsStatic())
		{
			if (isPenetrating)
			{
				PhysSetGameFlags(pList[i], FVPHYSICS_PENETRATING);
			}
			else
			{
				PhysClearGameFlags(pList[i], FVPHYSICS_PENETRATING);
			}
		}
	}
}

void CCollisionEvent::GetListOfPenetratingEntities(CBaseEntity* pSearch, CUtlVector<CBaseEntity*>& list)
{
	for (int i = m_penetrateEvents.Count() - 1; i >= 0; --i)
	{
		if (m_penetrateEvents[i].hEntity0 == pSearch && m_penetrateEvents[i].hEntity1.Get() != NULL)
		{
			list.AddToTail(m_penetrateEvents[i].hEntity1);
		}
		else if (m_penetrateEvents[i].hEntity1 == pSearch && m_penetrateEvents[i].hEntity0.Get() != NULL)
		{
			list.AddToTail(m_penetrateEvents[i].hEntity0);
		}
	}
}

void CCollisionEvent::UpdatePenetrateEvents(void)
{
	for (int i = m_penetrateEvents.Count() - 1; i >= 0; --i)
	{
		CBaseEntity* pEntity0 = m_penetrateEvents[i].hEntity0;
		CBaseEntity* pEntity1 = m_penetrateEvents[i].hEntity1;

		if (m_penetrateEvents[i].collisionState == COLLSTATE_TRYDISABLE)
		{
			if (pEntity0 && pEntity1)
			{
				IPhysicsObject* pObj0 = pEntity0->GetEngineObject()->VPhysicsGetObject();
				if (pObj0)
				{
					gEntList.PhysForceEntityToSleep(pEntity0, pObj0);
				}
				IPhysicsObject* pObj1 = pEntity1->GetEngineObject()->VPhysicsGetObject();
				if (pObj1)
				{
					gEntList.PhysForceEntityToSleep(pEntity1, pObj1);
				}
				m_penetrateEvents[i].collisionState = COLLSTATE_DISABLED;
				continue;
			}
			// missing entity or object, clear event
		}
		else if (m_penetrateEvents[i].collisionState == COLLSTATE_TRYNPCSOLVER)
		{
			if (pEntity0 && pEntity1)
			{
				bool IsNPC = pEntity0->IsNPC();
				CBaseEntity* pNPC = pEntity0;
				CBaseEntity* pBlocker = pEntity1;
				if (!IsNPC)
				{
					IsNPC = pEntity1->IsNPC();
					Assert(IsNPC);
					pNPC = pEntity1;
					pBlocker = pEntity0;
				}
				pNPC->NPCPhysics_CreateSolver(pBlocker, true, 1.0f);
			}
			// transferred to solver, clear event
		}
		else if (m_penetrateEvents[i].collisionState == COLLSTATE_TRYENTITYSOLVER)
		{
			if (pEntity0 && pEntity1)
			{
				if (!IsDebris(pEntity1->GetEngineObject()->GetCollisionGroup()) || pEntity1->GetEngineObject()->GetMoveType() != MOVETYPE_VPHYSICS)
				{
					CBaseEntity* pTmp = pEntity0;
					pEntity0 = pEntity1;
					pEntity1 = pTmp;
				}
				Physics_CreateSolver(pEntity0, pEntity1, true, 1.0f);
			}
			// transferred to solver, clear event
		}
		else if (gpGlobals->curtime - m_penetrateEvents[i].timeStamp > 1.0)
		{
			if (m_penetrateEvents[i].collisionState == COLLSTATE_DISABLED)
			{
				if (pEntity0 && pEntity1)
				{
					IPhysicsObject* pObj0 = pEntity0->GetEngineObject()->VPhysicsGetObject();
					IPhysicsObject* pObj1 = pEntity1->GetEngineObject()->VPhysicsGetObject();
					if (pObj0 && pObj1)
					{
						m_penetrateEvents[i].collisionState = COLLSTATE_ENABLED;
						continue;
					}
				}
			}
			// haven't penetrated for 1 second, so remove
		}
		else
		{
			// recent timestamp, don't remove the event yet
			continue;
		}
		// done, clear event
		m_penetrateEvents.FastRemove(i);
		UpdateEntityPenetrationFlag(pEntity0, false);
		UpdateEntityPenetrationFlag(pEntity1, false);
	}
}

penetrateevent_t& CCollisionEvent::FindOrAddPenetrateEvent(CBaseEntity* pEntity0, CBaseEntity* pEntity1)
{
	int index = -1;
	for (int i = m_penetrateEvents.Count() - 1; i >= 0; --i)
	{
		if (m_penetrateEvents[i].hEntity0.Get() == pEntity0 && m_penetrateEvents[i].hEntity1.Get() == pEntity1)
		{
			index = i;
			break;
		}
	}
	if (index < 0)
	{
		index = m_penetrateEvents.AddToTail();
		penetrateevent_t& event = m_penetrateEvents[index];
		event.hEntity0 = pEntity0;
		event.hEntity1 = pEntity1;
		event.startTime = gpGlobals->curtime;
		event.collisionState = COLLSTATE_ENABLED;
		UpdateEntityPenetrationFlag(pEntity0, true);
		UpdateEntityPenetrationFlag(pEntity1, true);
	}
	penetrateevent_t& event = m_penetrateEvents[index];
	event.timeStamp = gpGlobals->curtime;
	return event;
}

static bool CanResolvePenetrationWithNPC(CBaseEntity* pEntity, IPhysicsObject* pObject)
{
	if (pEntity->GetEngineObject()->GetMoveType() == MOVETYPE_VPHYSICS)
	{
		// hinged objects won't be able to be pushed out anyway, so don't try the npc solver
		if (!pObject->IsHinged() && !pObject->IsAttachedToConstraint(true))
		{
			if (pObject->IsMoveable() || pEntity->GetServerVehicle())
				return true;
		}
	}
	return false;
}

int CCollisionEvent::ShouldSolvePenetration(IPhysicsObject* pObj0, IPhysicsObject* pObj1, void* pGameData0, void* pGameData1, float dt)
{
	CallbackContext check(this);

	// Pointers to the entity for each physics object
	CBaseEntity* pEntity0 = static_cast<CBaseEntity*>(pGameData0);
	CBaseEntity* pEntity1 = static_cast<CBaseEntity*>(pGameData1);

	// this can get called as entities are being constructed on the other side of a game load or level transition
	// Some entities may not be fully constructed, so don't call into their code until the level is running
	if (gEntList.PhysIsPaused())
		return true;

	// solve it yourself here and return 0, or have the default implementation do it
	if (pEntity0 > pEntity1)
	{
		// swap sort
		CBaseEntity* pTmp = pEntity0;
		pEntity0 = pEntity1;
		pEntity1 = pTmp;
		IPhysicsObject* pTmpObj = pObj0;
		pObj0 = pObj1;
		pObj1 = pTmpObj;
	}
	if (pEntity0 == pEntity1)
	{
		if (pObj0->GetGameFlags() & FVPHYSICS_PART_OF_RAGDOLL)
		{
			DevMsg(2, "Solving ragdoll self penetration! %s (%s) (%d v %d)\n", pObj0->GetName(), pEntity0->GetDebugName(), pObj0->GetGameIndex(), pObj1->GetGameIndex());
			ragdoll_t* pRagdoll = pEntity0->GetEngineObject()->GetRagdoll();
			pRagdoll->pGroup->SolvePenetration(pObj0, pObj1);
			return false;
		}
	}
	penetrateevent_t& event = FindOrAddPenetrateEvent(pEntity0, pEntity1);
	float eventTime = gpGlobals->curtime - event.startTime;

	// NPC vs. physics object.  Create a game DLL solver and remove this event
	if ((pEntity0->MyNPCPointer() && CanResolvePenetrationWithNPC(pEntity1, pObj1)) ||
		(pEntity1->MyNPCPointer() && CanResolvePenetrationWithNPC(pEntity0, pObj0)))
	{
		event.collisionState = COLLSTATE_TRYNPCSOLVER;
	}

	if ((IsDebris(pEntity0->GetEngineObject()->GetCollisionGroup()) && !pObj1->IsStatic()) || (IsDebris(pEntity1->GetEngineObject()->GetCollisionGroup()) && !pObj0->IsStatic()))
	{
		if (eventTime > 0.5f)
		{
			//Msg("Debris stuck in non-static!\n");
			event.collisionState = COLLSTATE_TRYENTITYSOLVER;
		}
	}
#if _DEBUG
	if (phys_dontprintint.GetBool() == false)
	{
		const char* pName1 = STRING(pEntity0->GetEngineObject()->GetModelName());
		const char* pName2 = STRING(pEntity1->GetEngineObject()->GetModelName());
		if (pEntity0 == pEntity1)
		{
			int index0 = gEntList.PhysGetCollision()->CollideIndex(pObj0->GetCollide());
			int index1 = gEntList.PhysGetCollision()->CollideIndex(pObj1->GetCollide());
			DevMsg(1, "***Inter-penetration on %s (%d & %d) (%.0f, %.0f)\n", pName1 ? pName1 : "(null)", index0, index1, gpGlobals->curtime, eventTime);
		}
		else
		{
			DevMsg(1, "***Inter-penetration between %s(%s) AND %s(%s) (%.0f, %.0f)\n", pName1 ? pName1 : "(null)", pEntity0->GetDebugName(), pName2 ? pName2 : "(null)", pEntity1->GetDebugName(), gpGlobals->curtime, eventTime);
		}
	}
#endif

	if (eventTime > 3)
	{
		// don't report penetrations on ragdolls with themselves, or outside of developer mode
		if (g_pDeveloper->GetInt() && pEntity0 != pEntity1)
		{
			ReportPenetration(pEntity0, phys_penetration_error_time.GetFloat());
			ReportPenetration(pEntity1, phys_penetration_error_time.GetFloat());
		}
		event.startTime = gpGlobals->curtime;
		// don't put players or game physics controlled objects to sleep
		if (!pEntity0->IsPlayer() && !pEntity1->IsPlayer() && !pObj0->GetShadowController() && !pObj1->GetShadowController())
		{
			// two objects have been stuck for more than 3 seconds, try disabling simulation
			event.collisionState = COLLSTATE_TRYDISABLE;
			return false;
		}
	}


	return true;
}

static int BestAxisMatchingNormal(const matrix3x4_t& matrix, const Vector& normal)
{
	float bestDot = -1;
	int best = 0;
	for (int i = 0; i < 3; i++)
	{
		Vector tmp;
		MatrixGetColumn(matrix, i, tmp);
		float dot = fabs(DotProduct(tmp, normal));
		if (dot > bestDot)
		{
			bestDot = dot;
			best = i;
		}
	}

	return best;
}

void PhysicsSplash(IPhysicsFluidController* pFluid, IPhysicsObject* pObject, CBaseEntity* pEntity)
{
	Vector normal;
	float dist;
	pFluid->GetSurfacePlane(&normal, &dist);

	const matrix3x4_t& matrix = pEntity->GetEngineObject()->EntityToWorldTransform();

	// Find the local axis that best matches the water surface normal
	int bestAxis = BestAxisMatchingNormal(matrix, normal);

	Vector tangent, binormal;
	MatrixGetColumn(matrix, (bestAxis + 1) % 3, tangent);
	binormal = CrossProduct(normal, tangent);
	VectorNormalize(binormal);
	tangent = CrossProduct(binormal, normal);
	VectorNormalize(tangent);

	// Now we have a basis tangent to the surface that matches the object's local orientation as well as possible
	// compute an OBB using this basis

	// Get object extents in basis
	Vector tanPts[2], binPts[2];
	tanPts[0] = EntityList()->PhysGetCollision()->CollideGetExtent(pObject->GetCollide(), pEntity->GetEngineObject()->GetAbsOrigin(), pEntity->GetEngineObject()->GetAbsAngles(), -tangent);
	tanPts[1] = EntityList()->PhysGetCollision()->CollideGetExtent(pObject->GetCollide(), pEntity->GetEngineObject()->GetAbsOrigin(), pEntity->GetEngineObject()->GetAbsAngles(), tangent);
	binPts[0] = EntityList()->PhysGetCollision()->CollideGetExtent(pObject->GetCollide(), pEntity->GetEngineObject()->GetAbsOrigin(), pEntity->GetEngineObject()->GetAbsAngles(), -binormal);
	binPts[1] = EntityList()->PhysGetCollision()->CollideGetExtent(pObject->GetCollide(), pEntity->GetEngineObject()->GetAbsOrigin(), pEntity->GetEngineObject()->GetAbsAngles(), binormal);

	// now compute the centered bbox
	float mins[2], maxs[2], center[2], extents[2];
	mins[0] = DotProduct(tanPts[0], tangent);
	maxs[0] = DotProduct(tanPts[1], tangent);

	mins[1] = DotProduct(binPts[0], binormal);
	maxs[1] = DotProduct(binPts[1], binormal);

	center[0] = 0.5 * (mins[0] + maxs[0]);
	center[1] = 0.5 * (mins[1] + maxs[1]);

	extents[0] = maxs[0] - center[0];
	extents[1] = maxs[1] - center[1];

	Vector centerPoint = center[0] * tangent + center[1] * binormal + dist * normal;

	Vector axes[2];
	axes[0] = (maxs[0] - center[0]) * tangent;
	axes[1] = (maxs[1] - center[1]) * binormal;

	// visualize OBB hit
	/*
	Vector corner1 = centerPoint - axes[0] - axes[1];
	Vector corner2 = centerPoint + axes[0] - axes[1];
	Vector corner3 = centerPoint + axes[0] + axes[1];
	Vector corner4 = centerPoint - axes[0] + axes[1];
	NDebugOverlay::Line( corner1, corner2, 0, 0, 255, false, 10 );
	NDebugOverlay::Line( corner2, corner3, 0, 0, 255, false, 10 );
	NDebugOverlay::Line( corner3, corner4, 0, 0, 255, false, 10 );
	NDebugOverlay::Line( corner4, corner1, 0, 0, 255, false, 10 );
	*/

	Vector	corner[4];

	corner[0] = centerPoint - axes[0] - axes[1];
	corner[1] = centerPoint + axes[0] - axes[1];
	corner[2] = centerPoint + axes[0] + axes[1];
	corner[3] = centerPoint - axes[0] + axes[1];

	CEffectData	data;

	if (pObject->GetGameFlags() & FVPHYSICS_PART_OF_RAGDOLL)
	{
		/*
		data.m_vOrigin = centerPoint;
		data.m_vNormal = normal;
		VectorAngles( normal, data.m_vAngles );
		data.m_flScale = random->RandomFloat( 8, 10 );

		DispatchEffect( "watersplash", data );

		int		splashes = 4;
		Vector	point;

		for ( int i = 0; i < splashes; i++ )
		{
			point = RandomVector( -32.0f, 32.0f );
			point[2] = 0.0f;

			point += corner[i];

			data.m_vOrigin = point;
			data.m_vNormal = normal;
			VectorAngles( normal, data.m_vAngles );
			data.m_flScale = random->RandomFloat( 4, 6 );

			DispatchEffect( "watersplash", data );
		}
		*/

		//FIXME: This code will not work correctly given how the ragdoll/fluid collision is acting currently
		return;
	}

	Vector vel;
	pObject->GetVelocity(&vel, NULL);
	float rawSpeed = -DotProduct(normal, vel);

	// proportional to cross-sectional area times velocity squared (fluid pressure)
	float speed = rawSpeed * rawSpeed * extents[0] * extents[1] * (1.0f / 2500000.0f) * pObject->GetMass() * (0.01f);

	speed = clamp(speed, 0.f, 50.f);

	bool bRippleOnly = false;

	// allow the entity to perform a custom splash effect
	if (pEntity->PhysicsSplash(centerPoint, normal, rawSpeed, speed))
		return;

	//Deny really weak hits
	//FIXME: We still need to ripple the surface in this case
	if (speed <= 0.35f)
	{
		if (speed <= 0.1f)
			return;

		bRippleOnly = true;
	}

	float size = RemapVal(speed, 0.35, 50, 8, 18);

	//Find the surface area
	float	radius = extents[0] * extents[1];
	//int	splashes = clamp ( radius / 128.0f, 1, 2 );	//One splash for every three square feet of area

	//Msg( "Speed: %.2f, Size: %.2f\n, Radius: %.2f, Splashes: %d", speed, size, radius, splashes );

	Vector point;

	data.m_fFlags = 0;
	data.m_vOrigin = centerPoint;
	data.m_vNormal = normal;
	VectorAngles(normal, data.m_vAngles);
	data.m_flScale = size + random->RandomFloat(0, 2);
	if (pEntity->GetWaterType() & CONTENTS_SLIME)
	{
		data.m_fFlags |= FX_WATER_IN_SLIME;
	}

	if (bRippleOnly)
	{
		DispatchEffect("waterripple", data);
	}
	else
	{
		DispatchEffect("watersplash", data);
	}

	if (radius > 500.0f)
	{
		int splashes = random->RandomInt(1, 4);

		for (int i = 0; i < splashes; i++)
		{
			point = RandomVector(-4.0f, 4.0f);
			point[2] = 0.0f;

			point += corner[i];

			data.m_fFlags = 0;
			data.m_vOrigin = point;
			data.m_vNormal = normal;
			VectorAngles(normal, data.m_vAngles);
			data.m_flScale = size + random->RandomFloat(-3, 1);
			if (pEntity->GetWaterType() & CONTENTS_SLIME)
			{
				data.m_fFlags |= FX_WATER_IN_SLIME;
			}

			if (bRippleOnly)
			{
				DispatchEffect("waterripple", data);
			}
			else
			{
				DispatchEffect("watersplash", data);
			}
		}
	}

	/*
	for ( i = 0; i < splashes; i++ )
	{
		point = RandomVector( -8.0f, 8.0f );
		point[2] = 0.0f;

		point += centerPoint + axes[0] * random->RandomFloat( -1, 1 ) + axes[1] * random->RandomFloat( -1, 1 );

		data.m_vOrigin = point;
		data.m_vNormal = normal;
		VectorAngles( normal, data.m_vAngles );
		data.m_flScale = size + random->RandomFloat( -2, 4 );

		DispatchEffect( "watersplash", data );
	}
	*/
}

void CCollisionEvent::FluidStartTouch(IPhysicsObject* pObject, IPhysicsFluidController* pFluid)
{
	CallbackContext check(this);
	if ((pObject == NULL) || (pFluid == NULL))
		return;

	CBaseEntity* pEntity = static_cast<CBaseEntity*>(pObject->GetGameData());
	if (!pEntity)
		return;

	pEntity->GetEngineObject()->AddEFlags(EFL_TOUCHING_FLUID);
	pEntity->OnEntityEvent(ENTITY_EVENT_WATER_TOUCH, (void*)(intp)pFluid->GetContents());

	float timeSinceLastCollision = DeltaTimeSinceLastFluid(pEntity);
	if (timeSinceLastCollision < 0.5f)
		return;

	// UNDONE: Use this for splash logic instead?
	// UNDONE: Use angular term too - push splashes in rotAxs cross normal direction?
	Vector normal;
	float dist;
	pFluid->GetSurfacePlane(&normal, &dist);
	Vector vel;
	AngularImpulse angVel;
	pObject->GetVelocity(&vel, &angVel);
	Vector unitVel = vel;
	VectorNormalize(unitVel);

	// normal points out of the surface, we want the direction that points in
	float dragScale = pFluid->GetDensity() * gEntList.PhysGetEnv()->GetSimulationTimestep();
	normal = -normal;
	float linearScale = 0.5f * DotProduct(unitVel, normal) * pObject->CalculateLinearDrag(normal) * dragScale;
	linearScale = clamp(linearScale, 0.0f, 1.0f);
	vel *= -linearScale;

	// UNDONE: Figure out how much of the surface area has crossed the water surface and scale angScale by that
	// For now assume 25%
	Vector rotAxis = angVel;
	VectorNormalize(rotAxis);
	float angScale = 0.25f * pObject->CalculateAngularDrag(angVel) * dragScale;
	angScale = clamp(angScale, 0.0f, 1.0f);
	angVel *= -angScale;

	// compute the splash before we modify the velocity
	PhysicsSplash(pFluid, pObject, pEntity);

	// now damp out some motion toward the surface
	pObject->AddVelocity(&vel, &angVel);
}

void CCollisionEvent::FluidEndTouch(IPhysicsObject* pObject, IPhysicsFluidController* pFluid)
{
	CallbackContext check(this);
	if ((pObject == NULL) || (pFluid == NULL))
		return;

	CBaseEntity* pEntity = static_cast<CBaseEntity*>(pObject->GetGameData());
	if (!pEntity)
		return;

	float timeSinceLastCollision = DeltaTimeSinceLastFluid(pEntity);
	if (timeSinceLastCollision >= 0.5f)
	{
		PhysicsSplash(pFluid, pObject, pEntity);
	}

	pEntity->GetEngineObject()->RemoveEFlags(EFL_TOUCHING_FLUID);
	pEntity->OnEntityEvent(ENTITY_EVENT_WATER_UNTOUCH, (void*)(intp)pFluid->GetContents());
}

//-----------------------------------------------------------------------------
// CollisionEvent system 
//-----------------------------------------------------------------------------
// NOTE: PreCollision/PostCollision ALWAYS come in matched pairs!!!
void CCollisionEvent::PreCollision(vcollisionevent_t* pEvent)
{
	CallbackContext check(this);
	m_gameEvent.Init(pEvent);

	// gather the pre-collision data that the game needs to track
	for (int i = 0; i < 2; i++)
	{
		IPhysicsObject* pObject = pEvent->pObjects[i];
		if (pObject)
		{
			if (pObject->GetGameFlags() & FVPHYSICS_PLAYER_HELD)
			{
				CBaseEntity* pOtherEntity = reinterpret_cast<CBaseEntity*>(pEvent->pObjects[!i]->GetGameData());
				if (pOtherEntity && !pOtherEntity->IsPlayer())
				{
					Vector velocity;
					AngularImpulse angVel;
					// HACKHACK: If we totally clear this out, then Havok will think the objects
					// are penetrating and generate forces to separate them
					// so make it fairly small and have a tiny collision instead.
					pObject->GetVelocity(&velocity, &angVel);
					float len = VectorNormalize(velocity);
					len = MAX(len, 10);
					velocity *= len;
					len = VectorNormalize(angVel);
					len = MAX(len, 1);
					angVel *= len;
					pObject->SetVelocity(&velocity, &angVel);
				}
			}
			pObject->GetVelocity(&m_gameEvent.preVelocity[i], &m_gameEvent.preAngularVelocity[i]);
		}
	}
}

void CCollisionEvent::PostCollision(vcollisionevent_t* pEvent)
{
	CallbackContext check(this);
	bool isShadow[2] = { false,false };
	int i;

	for (i = 0; i < 2; i++)
	{
		IPhysicsObject* pObject = pEvent->pObjects[i];
		if (pObject)
		{
			CBaseEntity* pEntity = reinterpret_cast<CBaseEntity*>(pObject->GetGameData());
			if (!pEntity)
				return;

			// UNDONE: This is here to trap crashes due to NULLing out the game data on delete
			m_gameEvent.pEntities[i] = pEntity;
			unsigned int flags = pObject->GetCallbackFlags();
			pObject->GetVelocity(&m_gameEvent.postVelocity[i], NULL);
			if (flags & CALLBACK_SHADOW_COLLISION)
			{
				isShadow[i] = true;
			}

			// Shouldn't get impacts with triggers
			Assert(!pObject->IsTrigger());
		}
	}

	// copy off the post-collision variable data
	m_gameEvent.collisionSpeed = pEvent->collisionSpeed;
	m_gameEvent.pInternalData = pEvent->pInternalData;

	// special case for hitting self, only make one non-shadow call
	if (m_gameEvent.pEntities[0] == m_gameEvent.pEntities[1])
	{
		if (pEvent->isCollision && m_gameEvent.pEntities[0])
		{
			m_gameEvent.pEntities[0]->VPhysicsCollision(0, &m_gameEvent);
		}
		return;
	}

	if (isShadow[0] && isShadow[1])
	{
		pEvent->isCollision = false;
	}

	for (i = 0; i < 2; i++)
	{
		if (pEvent->isCollision)
		{
			m_gameEvent.pEntities[i]->VPhysicsCollision(i, &m_gameEvent);
		}
		if (pEvent->isShadowCollision && isShadow[i])
		{
			m_gameEvent.pEntities[i]->VPhysicsShadowCollision(i, &m_gameEvent);
		}
	}
}



void CCollisionEvent::Friction(IPhysicsObject* pObject, float energy, int surfaceProps, int surfacePropsHit, IPhysicsCollisionData* pData)
{
	CallbackContext check(this);
	//Get our friction information
	Vector vecPos, vecVel;
	pData->GetContactPoint(vecPos);
	pObject->GetVelocityAtPoint(vecPos, &vecVel);

	CBaseEntity* pEntity = reinterpret_cast<CBaseEntity*>(pObject->GetGameData());

	if (pEntity)
	{
		friction_t* pFriction = FindFriction(pEntity);

		if (pFriction && pFriction->pObject)
		{
			// in MP mode play sound and effects once every 500 msecs,
			// no ongoing updates, takes too much bandwidth
			if ((pFriction->flLastEffectTime + 0.5f) > gpGlobals->curtime)
			{
				pFriction->flLastUpdateTime = gpGlobals->curtime;
				return;
			}
		}

		pEntity->VPhysicsFriction(pObject, energy, surfaceProps, surfacePropsHit);
	}

	PhysFrictionEffect(vecPos, vecVel, energy, surfaceProps, surfacePropsHit);
}


friction_t* CCollisionEvent::FindFriction(CBaseEntity* pObject)
{
	friction_t* pFree = NULL;

	for (int i = 0; i < ARRAYSIZE(m_current); i++)
	{
		if (!m_current[i].pObject && !pFree)
			pFree = &m_current[i];

		if (m_current[i].pObject == pObject)
			return &m_current[i];
	}

	return pFree;
}

void CCollisionEvent::ShutdownFriction(friction_t& friction)
{
	//	Msg( "Scrape Stop %s \n", STRING(friction.pObject->m_iClassname) );
	CSoundEnvelopeController::GetController().SoundDestroy(friction.patch);
	friction.patch = NULL;
	friction.pObject = NULL;
}

void CCollisionEvent::UpdateRemoveObjects()
{
	Assert(!gEntList.PhysIsInCallback());
	for (int i = 0; i < m_removeObjects.Count(); i++)
	{
		gEntList.DestroyEntity(m_removeObjects[i]);
	}
	m_removeObjects.RemoveAll();
}

void CCollisionEvent::PostSimulationFrame()
{
	UpdateDamageEvents();
	gEntList.PhysGetPostSimulationQueue().CallQueued();
	UpdateRemoveObjects();
}

void CCollisionEvent::FlushQueuedOperations()
{
	int loopCount = 0;
	while (loopCount < 20)
	{
		int count = m_triggerEvents.Count() + m_touchEvents.Count() + m_damageEvents.Count() + m_removeObjects.Count() + gEntList.PhysGetPostSimulationQueue().Count();
		if (!count)
			break;
		// testing, if this assert fires it proves we've fixed the crash
		// after that the assert + warning can safely be removed
		Assert(0);
		Warning("Physics queue not empty, error!\n");
		loopCount++;
		UpdateTouchEvents();
		UpdateDamageEvents();
		gEntList.PhysGetPostSimulationQueue().CallQueued();
		UpdateRemoveObjects();
	}
}

void CCollisionEvent::FrameUpdate(void)
{
	UpdateFrictionSounds();
	UpdateTouchEvents();
	UpdatePenetrateEvents();
	UpdateFluidEvents();
	UpdateDamageEvents(); // if there was no PSI in physics, we'll still need to do some of these because collisions are solved in between PSIs
	gEntList.PhysGetPostSimulationQueue().CallQueued();
	UpdateRemoveObjects();

	// There are some queued operations that must complete each frame, iterate until these are done
	FlushQueuedOperations();
}



void CCollisionEvent::UpdateFluidEvents(void)
{
	for (int i = m_fluidEvents.Count() - 1; i >= 0; --i)
	{
		if ((gpGlobals->curtime - m_fluidEvents[i].impactTime) > FLUID_TIME_MAX)
		{
			m_fluidEvents.FastRemove(i);
		}
	}
}


float CCollisionEvent::DeltaTimeSinceLastFluid(CBaseEntity* pEntity)
{
	for (int i = m_fluidEvents.Count() - 1; i >= 0; --i)
	{
		if (gEntList.GetBaseEntity(m_fluidEvents[i].hEntity) == pEntity)
		{
			return gpGlobals->curtime - m_fluidEvents[i].impactTime;
		}
	}

	int index = m_fluidEvents.AddToTail();
	m_fluidEvents[index].hEntity = pEntity;
	m_fluidEvents[index].impactTime = gpGlobals->curtime;
	return FLUID_TIME_MAX;
}

void CCollisionEvent::UpdateFrictionSounds(void)
{
	for (int i = 0; i < ARRAYSIZE(m_current); i++)
	{
		if (m_current[i].patch)
		{
			if (m_current[i].flLastUpdateTime < (gpGlobals->curtime - 0.1f))
			{
				// friction wasn't updated the last 100msec, assume fiction finished
				ShutdownFriction(m_current[i]);
			}
		}
	}
}


void CCollisionEvent::DispatchStartTouch(CBaseEntity* pEntity0, CBaseEntity* pEntity1, const Vector& point, const Vector& normal)
{
	trace_t trace;
	memset(&trace, 0, sizeof(trace));
	trace.endpos = point;
	trace.plane.dist = DotProduct(point, normal);
	trace.plane.normal = normal;

	// NOTE: This sets up the touch list for both entities, no call to pEntity1 is needed
	pEntity0->GetEngineObject()->PhysicsMarkEntitiesAsTouchingEventDriven(pEntity1->GetEngineObject(), trace);
}

void CCollisionEvent::DispatchEndTouch(CBaseEntity* pEntity0, CBaseEntity* pEntity1)
{
	// frees the event-driven touchlinks
	pEntity1->GetEngineObject()->PhysicsNotifyOtherOfUntouch(pEntity0->GetEngineObject());
	pEntity0->GetEngineObject()->PhysicsNotifyOtherOfUntouch(pEntity1->GetEngineObject());
}

void CCollisionEvent::UpdateTouchEvents(void)
{
	int i;
	// Turn on buffering in case new touch events occur during processing
	bool bOldTouchEvents = m_bBufferTouchEvents;
	m_bBufferTouchEvents = true;
	for (i = 0; i < m_touchEvents.Count(); i++)
	{
		const touchevent_t& event = m_touchEvents[i];
		if (event.touchType == TOUCH_START)
		{
			DispatchStartTouch((CBaseEntity*)event.pEntity0, (CBaseEntity*)event.pEntity1, event.endPoint, event.normal);
		}
		else
		{
			// TOUCH_END
			DispatchEndTouch((CBaseEntity*)event.pEntity0, (CBaseEntity*)event.pEntity1);
		}
	}
	m_touchEvents.RemoveAll();

	for (i = 0; i < m_triggerEvents.Count(); i++)
	{
		m_currentTriggerEvent = m_triggerEvents[i];
		if (m_currentTriggerEvent.bStart)
		{
			m_currentTriggerEvent.pTriggerEntity->StartTouch(m_currentTriggerEvent.pEntity);
		}
		else
		{
			m_currentTriggerEvent.pTriggerEntity->EndTouch(m_currentTriggerEvent.pEntity);
		}
	}
	m_triggerEvents.RemoveAll();
	m_currentTriggerEvent.Clear();
	m_bBufferTouchEvents = bOldTouchEvents;
}

void CCollisionEvent::UpdateDamageEvents(void)
{
	for (int i = 0; i < m_damageEvents.Count(); i++)
	{
		damageevent_t& event = m_damageEvents[i];

		// Track changes in the entity's life state
		int iEntBits = event.pEntity->IsAlive() ? 0x0001 : 0;
		iEntBits |= event.pEntity->GetEngineObject()->IsMarkedForDeletion() ? 0x0002 : 0;
		iEntBits |= (event.pEntity->GetEngineObject()->GetSolidFlags() & FSOLID_NOT_SOLID) ? 0x0004 : 0;
#if 0
		// Go ahead and compute the current static stress when hit by a large object (with a force high enough to do damage).  
		// That way you die from the impact rather than the stress of the object resting on you whenever possible. 
		// This makes the damage effects cleaner.
		if (event.pInflictorPhysics && event.pInflictorPhysics->GetMass() > VPHYSICS_LARGE_OBJECT_MASS)
		{
			CBaseCombatCharacter* pCombat = event.pEntity->MyCombatCharacterPointer();
			if (pCombat)
			{
				vphysics_objectstress_t stressOut;
				event.info.AddDamage(pCombat->CalculatePhysicsStressDamage(&stressOut, pCombat->GetEngineObject()->VPhysicsGetObject()));
			}
		}
#endif

		event.pEntity->TakeDamage(event.info);
		int iEntBits2 = event.pEntity->IsAlive() ? 0x0001 : 0;
		iEntBits2 |= event.pEntity->GetEngineObject()->IsMarkedForDeletion() ? 0x0002 : 0;
		iEntBits2 |= (event.pEntity->GetEngineObject()->GetSolidFlags() & FSOLID_NOT_SOLID) ? 0x0004 : 0;

		if (event.bRestoreVelocity && iEntBits != iEntBits2)
		{
			// UNDONE: Use ratio of masses to blend in a little of the collision response?
			// UNDONE: Damage for future events is already computed - it would be nice to
			//			go back and recompute it now that the values have
			//			been adjusted
			RestoreDamageInflictorState(event.pInflictorPhysics);
		}
	}
	m_damageEvents.RemoveAll();
	m_damageInflictors.RemoveAll();
}

void CCollisionEvent::RestoreDamageInflictorState(int inflictorStateIndex, float velocityBlend)
{
	inflictorstate_t& state = m_damageInflictors[inflictorStateIndex];
	if (state.restored)
		return;

	// so we only restore this guy once
	state.restored = true;

	if (velocityBlend > 0)
	{
		Vector velocity;
		AngularImpulse angVel;
		state.pInflictorPhysics->GetVelocity(&velocity, &angVel);
		state.savedVelocity = state.savedVelocity * velocityBlend + velocity * (1 - velocityBlend);
		state.savedAngularVelocity = state.savedAngularVelocity * velocityBlend + angVel * (1 - velocityBlend);
		state.pInflictorPhysics->SetVelocity(&state.savedVelocity, &state.savedAngularVelocity);
	}

	if (state.nextIndex >= 0)
	{
		RestoreDamageInflictorState(state.nextIndex, velocityBlend);
	}
}

void CCollisionEvent::RestoreDamageInflictorState(IPhysicsObject* pInflictor)
{
	if (!pInflictor)
		return;

	int index = FindDamageInflictor(pInflictor);
	if (index >= 0)
	{
		inflictorstate_t& state = m_damageInflictors[index];
		if (!state.restored)
		{
			float velocityBlend = 1.0;
			float inflictorMass = state.pInflictorPhysics->GetMass();
			if (inflictorMass < VPHYSICS_LARGE_OBJECT_MASS && !(state.pInflictorPhysics->GetGameFlags()& FVPHYSICS_DMG_SLICE))
			{
				float otherMass = state.otherMassMax > 0 ? state.otherMassMax : 1;
				float massRatio = inflictorMass / otherMass;
				massRatio = clamp(massRatio, 0.1f, 10.0f);
				if (massRatio < 1)
				{
					velocityBlend = RemapVal(massRatio, 0.1, 1, 0, 0.5);
				}
				else
				{
					velocityBlend = RemapVal(massRatio, 1.0, 10, 0.5, 1);
				}
			}
			RestoreDamageInflictorState(index, velocityBlend);
		}
	}
}

bool CCollisionEvent::GetInflictorVelocity(IPhysicsObject* pInflictor, Vector& velocity, AngularImpulse& angVelocity)
{
	int index = FindDamageInflictor(pInflictor);
	if (index >= 0)
	{
		inflictorstate_t& state = m_damageInflictors[index];
		velocity = state.savedVelocity;
		angVelocity = state.savedAngularVelocity;
		return true;
	}

	return false;
}



void CCollisionEvent::AddTouchEvent(CBaseEntity* pEntity0, CBaseEntity* pEntity1, int touchType, const Vector& point, const Vector& normal)
{
	if (!pEntity0 || !pEntity1)
		return;

	int index = m_touchEvents.AddToTail();
	touchevent_t& event = m_touchEvents[index];
	event.pEntity0 = pEntity0;
	event.pEntity1 = pEntity1;
	event.touchType = touchType;
	event.endPoint = point;
	event.normal = normal;
}

void CCollisionEvent::AddDamageEvent(CBaseEntity* pEntity, const CTakeDamageInfo& info, IPhysicsObject* pInflictorPhysics, bool bRestoreVelocity, const Vector& savedVel, const AngularImpulse& savedAngVel)
{
	if (pEntity->GetEngineObject()->IsMarkedForDeletion())
		return;

	int iTimeBasedDamage = g_pGameRules->Damage_GetTimeBased();
	if (!(info.GetDamageType() & (DMG_BURN | DMG_DROWN | iTimeBasedDamage | DMG_PREVENT_PHYSICS_FORCE)))
	{
		Assert(info.GetDamageForce() != vec3_origin && info.GetDamagePosition() != vec3_origin);
	}

	int index = m_damageEvents.AddToTail();
	damageevent_t& event = m_damageEvents[index];
	event.pEntity = pEntity;
	event.info = info;
	event.pInflictorPhysics = pInflictorPhysics;
	event.bRestoreVelocity = bRestoreVelocity;
	if (!pInflictorPhysics || !pInflictorPhysics->IsMoveable())
	{
		event.bRestoreVelocity = false;
	}

	if (event.bRestoreVelocity)
	{
		float otherMass = pEntity->GetEngineObject()->VPhysicsGetObject()->GetMass();
		int inflictorIndex = FindDamageInflictor(pInflictorPhysics);
		if (inflictorIndex >= 0)
		{
			// if this is a bigger mass, save that info
			inflictorstate_t& state = m_damageInflictors[inflictorIndex];
			if (otherMass > state.otherMassMax)
			{
				state.otherMassMax = otherMass;
			}

		}
		else
		{
			AddDamageInflictor(pInflictorPhysics, otherMass, savedVel, savedAngVel, true);
		}
	}

}

//-----------------------------------------------------------------------------
// Impulse events
//-----------------------------------------------------------------------------
void PostSimulation_ImpulseEvent(IPhysicsObject* pObject, const Vector& centerForce, const AngularImpulse& centerTorque)
{
	pObject->ApplyForceCenter(centerForce);
	pObject->ApplyTorqueCenter(centerTorque);
}

void PostSimulation_SetVelocityEvent(IPhysicsObject* pPhysicsObject, const Vector& vecVelocity)
{
	pPhysicsObject->SetVelocity(&vecVelocity, NULL);
}

void CCollisionEvent::AddRemoveObject(CBaseEntity* pRemove)
{
	if (pRemove && m_removeObjects.Find(pRemove) == -1)
	{
		m_removeObjects.AddToTail(pRemove);
	}
}
int CCollisionEvent::FindDamageInflictor(IPhysicsObject* pInflictorPhysics)
{
	// UNDONE: Linear search?  Probably ok with a low count here
	for (int i = m_damageInflictors.Count() - 1; i >= 0; --i)
	{
		const inflictorstate_t& state = m_damageInflictors[i];
		if (state.pInflictorPhysics == pInflictorPhysics)
			return i;
	}

	return -1;
}


int CCollisionEvent::AddDamageInflictor(IPhysicsObject* pInflictorPhysics, float otherMass, const Vector& savedVel, const AngularImpulse& savedAngVel, bool addList)
{
	// NOTE: Save off the state of the object before collision
	// restore if the impact is a kill
	// UNDONE: Should we absorb some energy here?
	// NOTE: we can't save a delta because there could be subsequent post-fatal collisions

	int addIndex = m_damageInflictors.AddToTail();
	{
		inflictorstate_t& state = m_damageInflictors[addIndex];
		state.pInflictorPhysics = pInflictorPhysics;
		state.savedVelocity = savedVel;
		state.savedAngularVelocity = savedAngVel;
		state.otherMassMax = otherMass;
		state.restored = false;
		state.nextIndex = -1;
	}

	if (addList)
	{
		CBaseEntity* pEntity = static_cast<CBaseEntity*>(pInflictorPhysics->GetGameData());
		if (pEntity)
		{
			IPhysicsObject* pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
			int physCount = pEntity->GetEngineObject()->VPhysicsGetObjectList(pList, ARRAYSIZE(pList));
			if (physCount > 1)
			{
				int currentIndex = addIndex;
				for (int i = 0; i < physCount; i++)
				{
					if (pList[i] != pInflictorPhysics)
					{
						Vector vel;
						AngularImpulse angVel;
						pList[i]->GetVelocity(&vel, &angVel);
						int next = AddDamageInflictor(pList[i], otherMass, vel, angVel, false);
						m_damageInflictors[currentIndex].nextIndex = next;
						currentIndex = next;
					}
				}
			}
		}
	}
	return addIndex;
}


void CCollisionEvent::LevelShutdown(void)
{
	for (int i = 0; i < ARRAYSIZE(m_current); i++)
	{
		if (m_current[i].patch)
		{
			ShutdownFriction(m_current[i]);
		}
	}
}


void CCollisionEvent::StartTouch(IPhysicsObject* pObject1, IPhysicsObject* pObject2, IPhysicsCollisionData* pTouchData)
{
	CallbackContext check(this);
	CBaseEntity* pEntity1 = static_cast<CBaseEntity*>(pObject1->GetGameData());
	CBaseEntity* pEntity2 = static_cast<CBaseEntity*>(pObject2->GetGameData());

	if (!pEntity1 || !pEntity2)
		return;

	Vector endPoint, normal;
	pTouchData->GetContactPoint(endPoint);
	pTouchData->GetSurfaceNormal(normal);
	if (!m_bBufferTouchEvents)
	{
		DispatchStartTouch(pEntity1, pEntity2, endPoint, normal);
	}
	else
	{
		AddTouchEvent(pEntity1, pEntity2, TOUCH_START, endPoint, normal);
	}
}

static int CountPhysicsObjectEntityContacts(IPhysicsObject* pObject, CBaseEntity* pEntity)
{
	IPhysicsFrictionSnapshot* pSnapshot = pObject->CreateFrictionSnapshot();
	int count = 0;
	while (pSnapshot->IsValid())
	{
		IPhysicsObject* pOther = pSnapshot->GetObject(1);
		CBaseEntity* pOtherEntity = static_cast<CBaseEntity*>(pOther->GetGameData());
		if (pOtherEntity == pEntity)
			count++;
		pSnapshot->NextFrictionData();
	}
	pObject->DestroyFrictionSnapshot(pSnapshot);
	return count;
}

void CCollisionEvent::EndTouch(IPhysicsObject* pObject1, IPhysicsObject* pObject2, IPhysicsCollisionData* pTouchData)
{
	CallbackContext check(this);
	CBaseEntity* pEntity1 = static_cast<CBaseEntity*>(pObject1->GetGameData());
	CBaseEntity* pEntity2 = static_cast<CBaseEntity*>(pObject2->GetGameData());

	if (!pEntity1 || !pEntity2)
		return;

	// contact point deleted, but entities are still touching?
	IPhysicsObject* list[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	int count = pEntity1->GetEngineObject()->VPhysicsGetObjectList(list, ARRAYSIZE(list));

	int contactCount = 0;
	for (int i = 0; i < count; i++)
	{
		contactCount += CountPhysicsObjectEntityContacts(list[i], pEntity2);

		// still touching
		if (contactCount > 1)
			return;
	}

	// should have exactly one contact point (the one getting deleted here)
	//Assert( contactCount == 1 );

	Vector endPoint, normal;
	pTouchData->GetContactPoint(endPoint);
	pTouchData->GetSurfaceNormal(normal);

	if (!m_bBufferTouchEvents)
	{
		DispatchEndTouch(pEntity1, pEntity2);
	}
	else
	{
		AddTouchEvent(pEntity1, pEntity2, TOUCH_END, vec3_origin, vec3_origin);
	}
}

// UNDONE: This is functional, but minimally.
void CCollisionEvent::ObjectEnterTrigger(IPhysicsObject* pTrigger, IPhysicsObject* pObject)
{
	CBaseEntity* pTriggerEntity = static_cast<CBaseEntity*>(pTrigger->GetGameData());
	CBaseEntity* pEntity = static_cast<CBaseEntity*>(pObject->GetGameData());
	if (pTriggerEntity && pEntity)
	{
		// UNDONE: Don't buffer these until we can solve generating touches at object creation time
		if (0 && m_bBufferTouchEvents)
		{
			int index = m_triggerEvents.AddToTail();
			m_triggerEvents[index].Init(pTriggerEntity, pTrigger, pEntity, pObject, true);
		}
		else
		{
			CallbackContext check(this);
			m_currentTriggerEvent.Init(pTriggerEntity, pTrigger, pEntity, pObject, true);
			pTriggerEntity->StartTouch(pEntity);
			m_currentTriggerEvent.Clear();
		}
	}
}

void CCollisionEvent::ObjectLeaveTrigger(IPhysicsObject* pTrigger, IPhysicsObject* pObject)
{
	CBaseEntity* pTriggerEntity = static_cast<CBaseEntity*>(pTrigger->GetGameData());
	CBaseEntity* pEntity = static_cast<CBaseEntity*>(pObject->GetGameData());
	if (pTriggerEntity && pEntity)
	{
		// UNDONE: Don't buffer these until we can solve generating touches at object creation time
		if (0 && m_bBufferTouchEvents)
		{
			int index = m_triggerEvents.AddToTail();
			m_triggerEvents[index].Init(pTriggerEntity, pTrigger, pEntity, pObject, false);
		}
		else
		{
			CallbackContext check(this);
			m_currentTriggerEvent.Init(pTriggerEntity, pTrigger, pEntity, pObject, false);
			pTriggerEntity->EndTouch(pEntity);
			m_currentTriggerEvent.Clear();
		}
	}
}

bool CCollisionEvent::GetTriggerEvent(triggerevent_t* pEvent, CBaseEntity* pTriggerEntity)
{
	if (pEvent && pTriggerEntity == m_currentTriggerEvent.pTriggerEntity)
	{
		*pEvent = m_currentTriggerEvent;
		return true;
	}

	return false;
}

int CPortal_CollisionEvent::ShouldCollide(IPhysicsObject* pObj0, IPhysicsObject* pObj1, void* pGameData0, void* pGameData1)
{
	if (gEntList.m_ActivePortals.Count() > 0) {
		if (!pGameData0 || !pGameData1)
			return 1;

		AssertOnce(pObj0 && pObj1);
		bool bShadowClonesInvolved = ((pObj0->GetGameFlags() | pObj1->GetGameFlags()) & FVPHYSICS_IS_SHADOWCLONE) != 0;

		if (bShadowClonesInvolved)
		{
			//at least one shadow clone

			if ((pObj0->GetGameFlags() & pObj1->GetGameFlags()) & FVPHYSICS_IS_SHADOWCLONE)
				return 0; //both are shadow clones

			if ((pObj0->GetGameFlags() | pObj1->GetGameFlags()) & FVPHYSICS_PLAYER_HELD)
			{
				//at least one is held

				//don't let players collide with objects they're holding, they get kinda messed up sometimes
				if (pGameData0 && ((CBaseEntity*)pGameData0)->IsPlayer() && (((CBasePlayer*)pGameData0)->GetPlayerHeldEntity() == (CBaseEntity*)pGameData1))
					return 0;

				if (pGameData1 && ((CBaseEntity*)pGameData1)->IsPlayer() && (((CBasePlayer*)pGameData1)->GetPlayerHeldEntity() == (CBaseEntity*)pGameData0))
					return 0;
			}
		}



		//everything is in one environment. This means we must tightly control what collides with what
		if (pGameData0 != pGameData1)
		{
			//this code only decides what CAN'T collide due to portal environment differences, things that should collide will pass through here to deeper ShouldCollide() code
			CBaseEntity* pEntities[2] = { (CBaseEntity*)pGameData0, (CBaseEntity*)pGameData1 };
			IPhysicsObject* pPhysObjects[2] = { pObj0, pObj1 };
			bool bStatic[2] = { pObj0->IsStatic(), pObj1->IsStatic() };
			CEnginePortalInternal* pSimulators[2];
			for (int i = 0; i != 2; ++i)
				pSimulators[i] = (CEnginePortalInternal*)pEntities[i]->GetEngineObject()->GetSimulatorThatOwnsEntity();

			AssertOnce((bStatic[0] && bStatic[1]) == false); //hopefully the system doesn't even call in for this, they're both static and can't collide
			if (bStatic[0] && bStatic[1])
				return 0;

#ifdef _DEBUG
			for (int i = 0; i != 2; ++i)
			{
				if ((pSimulators[i] != NULL) && pEntities[i]->GetEngineObject()->IsShadowClone())
				{
					CBaseEntity* pSource = pEntities[i]->GetEngineShadowClone()->GetClonedEntity();

					CEnginePortalInternal* pSourceSimulator = (CEnginePortalInternal*)pSource->GetEngineObject()->GetSimulatorThatOwnsEntity();
					Assert((pSimulators[i]->m_EntFlags[pEntities[i]->entindex()] & PSEF_IS_IN_PORTAL_HOLE) == (pSourceSimulator->m_EntFlags[pSource->entindex()] & PSEF_IS_IN_PORTAL_HOLE));
				}
			}
#endif

			if (pSimulators[0] == pSimulators[1]) //same simulator
			{
				if (pSimulators[0] != NULL) //and not main world
				{
					if (bStatic[0] || bStatic[1])
					{
						for (int i = 0; i != 2; ++i)
						{
							if (bStatic[i])
							{
								if (pEntities[i]->GetEngineObject()->IsPortalSimulatorCollisionEntity())
								{
									PS_PhysicsObjectSourceType_t objectSource;
									if (pSimulators[i]->CreatedPhysicsObject(pPhysObjects[i], &objectSource) &&
										((objectSource == PSPOST_REMOTE_BRUSHES) || (objectSource == PSPOST_REMOTE_STATICPROPS)))
									{
										if ((pSimulators[1 - i]->m_EntFlags[pEntities[1 - i]->entindex()] & PSEF_IS_IN_PORTAL_HOLE) == 0)
											return 0; //require that the entity be in the portal hole before colliding with transformed geometry
										//FIXME: The above requirement might fail horribly for transformed collision blocking the portal from the other side and fast moving objects
									}
								}
								break;
							}
						}
					}
					else if (bShadowClonesInvolved)
					{
						if (((pSimulators[0]->m_EntFlags[pEntities[0]->entindex()] |
							pSimulators[1]->m_EntFlags[pEntities[1]->entindex()]) &
							PSEF_IS_IN_PORTAL_HOLE) == 0)
						{
							return 0; //neither entity was actually in the portal hole
						}
					}
				}
			}
			else //different simulators
			{
				if (bShadowClonesInvolved) //entities can only collide with shadow clones "owned" by the same simulator.
					return 0;

				if (bStatic[0] || bStatic[1])
				{
					for (int i = 0; i != 2; ++i)
					{
						if (bStatic[i])
						{
							int j = 1 - i;
							CEnginePortalInternal* pSimulator_Entity = pSimulators[j];

							if (pEntities[i]->IsWorld())
							{
								Assert(gEntList.GetSimulatorThatCreatedPhysicsObject(pPhysObjects[i]) == NULL);
								if (pSimulator_Entity)
									return 0;
							}
							else
							{
								CEnginePortalInternal* pSimulator_Static = gEntList.GetSimulatorThatCreatedPhysicsObject(pPhysObjects[i]); //might have been a static prop which would yield a new simulator

								if (pSimulator_Static && (pSimulator_Static != pSimulator_Entity))
									return 0; //static collideable is from a different simulator
							}
							break;
						}
					}
				}
				else
				{
					Assert(pEntities[0]->GetEngineObject()->IsPortalSimulatorCollisionEntity() == false);
					Assert(pEntities[1]->GetEngineObject()->IsPortalSimulatorCollisionEntity() == false);

					for (int i = 0; i != 2; ++i)
					{
						if (pSimulators[i])
						{
							//entities in the physics environment only collide with statics created by the environment (handled above), entities in the same environment (also above), or entities that should be cloned from main to the same environment
							if ((pSimulators[i]->m_EntFlags[pEntities[1 - i]->entindex()] & PSEF_CLONES_ENTITY_FROM_MAIN) == 0) //not cloned from main
								return 0;
						}
					}
				}

			}
		}
	}
	return BaseClass::ShouldCollide(pObj0, pObj1, pGameData0, pGameData1);
}

int CPortal_CollisionEvent::ShouldSolvePenetration(IPhysicsObject* pObj0, IPhysicsObject* pObj1, void* pGameData0, void* pGameData1, float dt)
{
	if (gEntList.m_ActivePortals.Count() > 0) {
		if ((pGameData0 == NULL) || (pGameData1 == NULL))
			return 0;

		if (((CBaseEntity*)pGameData0)->GetEngineObject()->IsPortalSimulatorCollisionEntity() ||
			((CBaseEntity*)pGameData1)->GetEngineObject()->IsPortalSimulatorCollisionEntity())
			return 0;

		// For portal, don't solve penetrations on combine balls
		if (FClassnameIs((CBaseEntity*)pGameData0, "prop_energy_ball") ||
			FClassnameIs((CBaseEntity*)pGameData1, "prop_energy_ball"))
			return 0;

		if ((pObj0->GetGameFlags() | pObj1->GetGameFlags()) & FVPHYSICS_PLAYER_HELD)
		{
			//at least one is held
			CBaseEntity* pHeld;
			CBaseEntity* pOther;
			IPhysicsObject* pPhysHeld;
			IPhysicsObject* pPhysOther;
			if (pObj0->GetGameFlags() & FVPHYSICS_PLAYER_HELD)
			{
				pHeld = (CBaseEntity*)pGameData0;
				pPhysHeld = pObj0;
				pOther = (CBaseEntity*)pGameData1;
				pPhysOther = pObj1;
			}
			else
			{
				pHeld = (CBaseEntity*)pGameData1;
				pPhysHeld = pObj1;
				pOther = (CBaseEntity*)pGameData0;
				pPhysOther = pObj0;
			}

			//don't let players collide with objects they're holding, they get kinda messed up sometimes
			if (pOther->IsPlayer() && (((CBasePlayer*)pOther)->GetPlayerHeldEntity() == pHeld))
				return 0;

			//held objects are clipping into other objects when travelling across a portal. We're close to ship, so this seems to be the
			//most localized way to make a fix.
			//Note that we're not actually going to change whether it should solve, we're just going to tack on some hacks
			CBasePlayer* pHoldingPlayer = gEntList.GetPlayerHoldingEntity(pHeld);
			if (!pHoldingPlayer && pHeld->GetEngineObject()->IsShadowClone())
				pHoldingPlayer = gEntList.GetPlayerHoldingEntity(pHeld->GetEngineShadowClone()->GetClonedEntity());

			Assert(pHoldingPlayer);
			if (pHoldingPlayer)
			{
				IGrabControllerServer* pGrabController = pHoldingPlayer->GetGrabController();

				if (!pGrabController)
					pGrabController = pHoldingPlayer->GetActiveWeapon()->GetGrabController();

				Assert(pGrabController);
				if (pGrabController)
				{
					pGrabController->SetPortalPenetratingEntity(pOther);
				}

				//NDebugOverlay::EntityBounds( pHeld, 0, 0, 255, 16, 1.0f );
				//NDebugOverlay::EntityBounds( pOther, 255, 0, 0, 16, 1.0f );
				//pPhysOther->Wake();
				//FindClosestPassableSpace( pOther, Vector( 0.0f, 0.0f, 1.0f ) );
			}
		}


		if ((pObj0->GetGameFlags() | pObj1->GetGameFlags()) & FVPHYSICS_IS_SHADOWCLONE)
		{
			//at least one shadowclone is involved

			if ((pObj0->GetGameFlags() & pObj1->GetGameFlags()) & FVPHYSICS_IS_SHADOWCLONE) //don't solve between two shadowclones, they're just going to resync in a frame anyways
				return 0;



			IPhysicsObject* const pObjects[2] = { pObj0, pObj1 };

			for (int i = 0; i != 2; ++i)
			{
				if (pObjects[i]->GetGameFlags() & FVPHYSICS_IS_SHADOWCLONE)
				{
					int j = 1 - i;
					if (!pObjects[j]->IsMoveable())
						return 0; //don't solve between shadow clones and statics

					if (((CBaseEntity*)(pObjects[i]->GetGameData()))->GetEngineShadowClone()->GetClonedEntity() == (pObjects[j]->GetGameData()))
						return 0; //don't solve between a shadow clone and its source entity
				}
			}
		}
	}
	return BaseClass::ShouldSolvePenetration(pObj0, pObj1, pGameData0, pGameData1, dt);
}



// Data for energy ball vs held item mass swapping hack
static float s_fSavedMass[2];
static bool s_bChangedMass[2] = { false, false };
static bool s_bUseUnshadowed[2] = { false, false };
static IPhysicsObject* s_pUnshadowed[2] = { NULL, NULL };


static void ModifyWeight_PreCollision(vcollisionevent_t* pEvent)
{
	Assert((pEvent->pObjects[0] != NULL) && (pEvent->pObjects[1] != NULL));

	CBaseEntity* pUnshadowedEntities[2];
	IPhysicsObject* pUnshadowedObjects[2];

	for (int i = 0; i != 2; ++i)
	{
		if (pEvent->pObjects[i]->GetGameFlags() & FVPHYSICS_IS_SHADOWCLONE)
		{
			CBaseEntity* pClone = ((CBaseEntity*)pEvent->pObjects[i]->GetGameData());
			pUnshadowedEntities[i] = pClone->GetEngineShadowClone()->GetClonedEntity();

			if (pUnshadowedEntities[i] == NULL)
				return;

			pUnshadowedObjects[i] = pClone->GetEngineShadowClone()->TranslatePhysicsToClonedEnt(pEvent->pObjects[i]);

			if (pUnshadowedObjects[i] == NULL)
				return;
		}
		else
		{
			pUnshadowedEntities[i] = (CBaseEntity*)pEvent->pObjects[i]->GetGameData();
			pUnshadowedObjects[i] = pEvent->pObjects[i];
		}
	}

	// HACKHACK: Reduce mass for combine ball vs movable brushes so the collision
	// appears fully elastic regardless of mass ratios
	for (int i = 0; i != 2; ++i)
	{
		int j = 1 - i;

		// One is a combine ball, if the other is a movable brush, reduce the combine ball mass
		if (pUnshadowedEntities[j]->IsCombineBall() && pUnshadowedEntities[i] != NULL)
		{
			if (pUnshadowedEntities[i]->GetEngineObject()->GetMoveType() == MOVETYPE_PUSH)
			{
				s_bChangedMass[j] = true;
				s_fSavedMass[j] = pUnshadowedObjects[j]->GetMass();
				pEvent->pObjects[j]->SetMass(VPHYSICS_MIN_MASS);
				if (pUnshadowedObjects[j] != pEvent->pObjects[j])
				{
					s_bUseUnshadowed[j] = true;
					s_pUnshadowed[j] = pUnshadowedObjects[j];

					pUnshadowedObjects[j]->SetMass(VPHYSICS_MIN_MASS);
				}
			}

			//HACKHACK: last minute problem knocking over turrets with energy balls, up the mass of the ball by a lot
			if (FClassnameIs(pUnshadowedEntities[i], "npc_portal_turret_floor"))
			{
				pUnshadowedObjects[j]->SetMass(pUnshadowedEntities[i]->GetEngineObject()->VPhysicsGetObject()->GetMass());
			}
		}
	}

	for (int i = 0; i != 2; ++i)
	{
		if ((pUnshadowedObjects[i] && pUnshadowedObjects[i]->GetGameFlags() & FVPHYSICS_PLAYER_HELD))
		{
			int j = 1 - i;
			if (pUnshadowedEntities[j]->IsCombineBall())
			{
				// [j] is the combine ball, set mass low
				// if the above ball vs brush entity check didn't already change the mass, change the mass
				if (!s_bChangedMass[j])
				{
					s_bChangedMass[j] = true;
					s_fSavedMass[j] = pUnshadowedObjects[j]->GetMass();
					pEvent->pObjects[j]->SetMass(VPHYSICS_MIN_MASS);
					if (pUnshadowedObjects[j] != pEvent->pObjects[j])
					{
						s_bUseUnshadowed[j] = true;
						s_pUnshadowed[j] = pUnshadowedObjects[j];

						pUnshadowedObjects[j]->SetMass(VPHYSICS_MIN_MASS);
					}
				}

				// [i] is the held object, set mass high
				s_bChangedMass[i] = true;
				s_fSavedMass[i] = pUnshadowedObjects[i]->GetMass();
				pEvent->pObjects[i]->SetMass(VPHYSICS_MAX_MASS);
				if (pUnshadowedObjects[i] != pEvent->pObjects[i])
				{
					s_bUseUnshadowed[i] = true;
					s_pUnshadowed[i] = pUnshadowedObjects[i];

					pUnshadowedObjects[i]->SetMass(VPHYSICS_MAX_MASS);
				}
			}
			else if (pEvent->pObjects[j]->GetGameFlags() & FVPHYSICS_IS_SHADOWCLONE)
			{
				//held object vs shadow clone, set held object mass back to grab controller saved mass

				// [i] is the held object
				s_bChangedMass[i] = true;
				s_fSavedMass[i] = pUnshadowedObjects[i]->GetMass();

				IGrabControllerServer* pGrabController = NULL;
				CBaseEntity* pLookingForEntity = (CBaseEntity*)pEvent->pObjects[i]->GetGameData();
				CBasePlayer* pHoldingPlayer = gEntList.GetPlayerHoldingEntity(pLookingForEntity);
				if (pHoldingPlayer)
					pGrabController = pHoldingPlayer->GetGrabController();

				float fSavedMass, fSavedRotationalDamping;

				AssertMsg(pGrabController, "Physics object is held, but we can't find the holding controller.");
				pGrabController->GetSavedParamsForCarriedPhysObject(pUnshadowedObjects[i], &fSavedMass, &fSavedRotationalDamping);

				pEvent->pObjects[i]->SetMass(fSavedMass);
				if (pUnshadowedObjects[i] != pEvent->pObjects[i])
				{
					s_bUseUnshadowed[i] = true;
					s_pUnshadowed[i] = pUnshadowedObjects[i];

					pUnshadowedObjects[i]->SetMass(fSavedMass);
				}
			}
		}
	}
}



void CPortal_CollisionEvent::PreCollision(vcollisionevent_t* pEvent)
{
	if (gEntList.m_ActivePortals.Count() > 0) {
		ModifyWeight_PreCollision(pEvent);
	}
	BaseClass::PreCollision(pEvent);
}


static void ModifyWeight_PostCollision(vcollisionevent_t* pEvent)
{
	for (int i = 0; i != 2; ++i)
	{
		if (s_bChangedMass[i])
		{
			pEvent->pObjects[i]->SetMass(s_fSavedMass[i]);
			if (s_bUseUnshadowed[i])
			{
				s_pUnshadowed[i]->SetMass(s_fSavedMass[i]);
				s_bUseUnshadowed[i] = false;
			}
			s_bChangedMass[i] = false;
		}
	}
}

void CPortal_CollisionEvent::PostCollision(vcollisionevent_t* pEvent)
{
	if (gEntList.m_ActivePortals.Count() > 0) {
		ModifyWeight_PostCollision(pEvent);
	}
	BaseClass::PostCollision(pEvent);
}

void CPortal_CollisionEvent::PostSimulationFrame()
{
	//this actually happens once per physics environment simulation, and we don't want that, so do nothing and we'll get a different version manually called
	if (gEntList.m_ActivePortals.Count() == 0) {
		BaseClass::PostSimulationFrame();
	}
}

void CPortal_CollisionEvent::PortalPostSimulationFrame(void)
{
	if (gEntList.m_ActivePortals.Count() > 0) {
		BaseClass::PostSimulationFrame();
	}
}


void CPortal_CollisionEvent::AddDamageEvent(CBaseEntity* pEntity, const CTakeDamageInfo& info, IPhysicsObject* pInflictorPhysics, bool bRestoreVelocity, const Vector& savedVel, const AngularImpulse& savedAngVel)
{
	if (gEntList.m_ActivePortals.Count() > 0) {
		const CTakeDamageInfo* pPassDownInfo = &info;
		CTakeDamageInfo ReplacementDamageInfo; //only used some of the time

		if ((info.GetDamageType() & DMG_CRUSH) &&
			(pInflictorPhysics->GetGameFlags() & FVPHYSICS_IS_SHADOWCLONE) &&
			(!info.BaseDamageIsValid()) &&
			(info.GetDamageForce().LengthSqr() > (20000.0f * 20000.0f))
			)
		{
			//VERY likely this was caused by the penetration solver. Since a shadow clone is involved we're going to ignore it becuase it causes more problems than it solves in this case
			ReplacementDamageInfo = info;
			ReplacementDamageInfo.SetDamage(0.0f);
			pPassDownInfo = &ReplacementDamageInfo;
		}

		BaseClass::AddDamageEvent(pEntity, *pPassDownInfo, pInflictorPhysics, bRestoreVelocity, savedVel, savedAngVel);
	}
	else {
		BaseClass::AddDamageEvent(pEntity, info, pInflictorPhysics, bRestoreVelocity, savedVel, savedAngVel);
	}
}


//-----------------------------------------------------------------------------
// Portal-specific hack designed to eliminate re-entrancy in touch functions
//-----------------------------------------------------------------------------
class CPortalTouchScope
{
public:
	CPortalTouchScope::CPortalTouchScope()
	{
		++gEntList.m_nTouchDepth;
	}

	CPortalTouchScope::~CPortalTouchScope()
	{
		Assert(gEntList.m_nTouchDepth >= 1);
		if (--gEntList.m_nTouchDepth == 0)
		{
			gEntList.m_PostTouchQueue.CallQueued();
		}
	}
};

//-----------------------------------------------------------------------------
// Purpose: 
// Output : inline servertouchlink_t
//-----------------------------------------------------------------------------
inline servertouchlink_t* AllocTouchLink(void)
{
	servertouchlink_t* link = (servertouchlink_t*)g_EdictTouchLinks.Alloc(sizeof(servertouchlink_t));
	if (link)
	{
		++linksallocated;
	}
	else
	{
		DevWarning("AllocTouchLink: failed to allocate servertouchlink_t.\n");
	}

	return link;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *link - 
// Output : inline void
//-----------------------------------------------------------------------------
inline void FreeTouchLink(servertouchlink_t* link)
{
	if (link)
	{
		//if ( link == g_pNextLink )
		//{
		//	g_pNextLink = link->nextLink;
		//}
		--linksallocated;
		link->prevLink = link->nextLink = NULL;
	}

	// Necessary to catch crashes
	g_EdictTouchLinks.Free(link);
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : inline groundlink_t
//-----------------------------------------------------------------------------
inline servergroundlink_t* AllocGroundLink(void)
{
	servergroundlink_t* link = (servergroundlink_t*)g_EntityGroundLinks.Alloc(sizeof(servergroundlink_t));
	if (link)
	{
		++groundlinksallocated;
	}
	else
	{
		DevMsg("AllocGroundLink: failed to allocate groundlink_t.!!!  groundlinksallocated=%d g_EntityGroundLinks.Count()=%d\n", groundlinksallocated, g_EntityGroundLinks.Count());
	}

#ifdef STAGING_ONLY
#ifndef CLIENT_DLL
	if (sv_groundlink_debug.GetBool())
	{
		UTIL_LogPrintf("Groundlink Alloc: %p at %d\n", link, groundlinksallocated);
	}
#endif
#endif // STAGING_ONLY

	return link;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *link - 
// Output : inline void
//-----------------------------------------------------------------------------
inline void FreeGroundLink(servergroundlink_t* link)
{
#ifdef STAGING_ONLY
#ifndef CLIENT_DLL
	if (sv_groundlink_debug.GetBool())
	{
		UTIL_LogPrintf("Groundlink Free: %p at %d\n", link, groundlinksallocated);
	}
#endif
#endif // STAGING_ONLY

	if (link)
	{
		--groundlinksallocated;
	}

	g_EntityGroundLinks.Free(link);
}

BEGIN_SIMPLE_DATADESC(game_shadowcontrol_params_t)

	DEFINE_FIELD(targetPosition, FIELD_POSITION_VECTOR),
	DEFINE_FIELD(targetRotation, FIELD_VECTOR),
	DEFINE_FIELD(maxAngular, FIELD_FLOAT),
	DEFINE_FIELD(maxDampAngular, FIELD_FLOAT),
	DEFINE_FIELD(maxSpeed, FIELD_FLOAT),
	DEFINE_FIELD(maxDampSpeed, FIELD_FLOAT),
	DEFINE_FIELD(dampFactor, FIELD_FLOAT),
	DEFINE_FIELD(teleportDistance, FIELD_FLOAT),

END_DATADESC()

BEGIN_SIMPLE_DATADESC(CGrabControllerInternal)

	DEFINE_EMBEDDED(m_shadow),

	DEFINE_FIELD(m_timeToArrive, FIELD_FLOAT),
	DEFINE_FIELD(m_errorTime, FIELD_FLOAT),
	DEFINE_FIELD(m_error, FIELD_FLOAT),
	DEFINE_FIELD(m_contactAmount, FIELD_FLOAT),
	DEFINE_AUTO_ARRAY(m_savedRotDamping, FIELD_FLOAT),
	DEFINE_AUTO_ARRAY(m_savedMass, FIELD_FLOAT),
	DEFINE_FIELD(m_flLoadWeight, FIELD_FLOAT),
	DEFINE_FIELD(m_bCarriedEntityBlocksLOS, FIELD_BOOLEAN),
	DEFINE_FIELD(m_bIgnoreRelativePitch, FIELD_BOOLEAN),
	DEFINE_FIELD(m_attachedEntity, FIELD_EHANDLE),
	DEFINE_FIELD(m_angleAlignment, FIELD_FLOAT),
	DEFINE_FIELD(m_vecPreferredCarryAngles, FIELD_VECTOR),
	DEFINE_FIELD(m_bHasPreferredCarryAngles, FIELD_BOOLEAN),
	DEFINE_FIELD(m_flDistanceOffset, FIELD_FLOAT),
	DEFINE_FIELD(m_attachedAnglesPlayerSpace, FIELD_VECTOR),
	DEFINE_FIELD(m_attachedPositionObjectSpace, FIELD_VECTOR),
	DEFINE_FIELD(m_bAllowObjectOverhead, FIELD_BOOLEAN),

	// Physptrs can't be inside embedded classes
	// DEFINE_PHYSPTR( m_controller ),

END_DATADESC()

const float DEFAULT_MAX_ANGULAR = 360.0f * 10.0f;
const float REDUCED_CARRY_MASS = 1.0f;

CGrabControllerInternal::CGrabControllerInternal(void)
{
	m_shadow.dampFactor = 1.0;
	m_shadow.teleportDistance = 0;
	m_errorTime = 0;
	m_error = 0;
	// make this controller really stiff!
	m_shadow.maxSpeed = 1000;
	m_shadow.maxAngular = DEFAULT_MAX_ANGULAR;
	m_shadow.maxDampSpeed = m_shadow.maxSpeed * 2;
	m_shadow.maxDampAngular = m_shadow.maxAngular;
	m_attachedEntity = NULL;
	m_vecPreferredCarryAngles = vec3_angle;
	m_bHasPreferredCarryAngles = false;
	m_flDistanceOffset = 0;
	// NVNT constructing m_pControllingPlayer to NULL
	m_pControllingPlayer = NULL;
}

CGrabControllerInternal::~CGrabControllerInternal(void)
{
	DetachEntity(false);
}

void CGrabControllerInternal::OnRestore()
{
	if (m_controller)
	{
		m_controller->SetEventHandler(this);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Computes a local matrix for the player clamped to valid carry ranges
//-----------------------------------------------------------------------------
// when looking level, hold bottom of object 8 inches below eye level
#define PLAYER_HOLD_LEVEL_EYES	-8

// when looking down, hold bottom of object 0 inches from feet
#define PLAYER_HOLD_DOWN_FEET	2

// when looking up, hold bottom of object 24 inches above eye level
#define PLAYER_HOLD_UP_EYES		24

// use a +/-30 degree range for the entire range of motion of pitch
#define PLAYER_LOOK_PITCH_RANGE	30

// player can reach down 2ft below his feet (otherwise he'll hold the object above the bottom)
#define PLAYER_REACH_DOWN_DISTANCE	24

static void ComputePlayerMatrix(CBasePlayer* pPlayer, matrix3x4_t& out)
{
	if (!pPlayer)
		return;

	QAngle angles = pPlayer->EyeAngles();
	Vector origin = pPlayer->EyePosition();

	// 0-360 / -180-180
	//angles.x = init ? 0 : AngleDistance( angles.x, 0 );
	//angles.x = clamp( angles.x, -PLAYER_LOOK_PITCH_RANGE, PLAYER_LOOK_PITCH_RANGE );
	angles.x = 0;

	float feet = pPlayer->GetEngineObject()->GetAbsOrigin().z + pPlayer->GetEngineObject()->WorldAlignMins().z;
	float eyes = origin.z;
	float zoffset = 0;
	// moving up (negative pitch is up)
	if (angles.x < 0)
	{
		zoffset = RemapVal(angles.x, 0, -PLAYER_LOOK_PITCH_RANGE, PLAYER_HOLD_LEVEL_EYES, PLAYER_HOLD_UP_EYES);
	}
	else
	{
		zoffset = RemapVal(angles.x, 0, PLAYER_LOOK_PITCH_RANGE, PLAYER_HOLD_LEVEL_EYES, PLAYER_HOLD_DOWN_FEET + (feet - eyes));
	}
	origin.z += zoffset;
	angles.x = 0;
	AngleMatrix(angles, origin, out);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CGrabControllerInternal::UpdateObject(CBasePlayer* pPlayer, float flError)
{
	CBaseEntity* pPenetratedEntity = m_PenetratedEntity.Get();
	if (pPenetratedEntity)
	{
		//FindClosestPassableSpace( pPenetratedEntity, Vector( 0.0f, 0.0f, 1.0f ) );
		IPhysicsObject* pPhysObject = pPenetratedEntity->GetEngineObject()->VPhysicsGetObject();
		if (pPhysObject)
			pPhysObject->Wake();

		m_PenetratedEntity = NULL; //assume we won
	}

	CBaseEntity* pEntity = GetAttached();
	if (!pEntity || ComputeError() > flError || (pPlayer->GetEngineObject()->GetGroundEntity() ? pPlayer->GetEngineObject()->GetGroundEntity()->GetOuter() : NULL) == pEntity || !pEntity->GetEngineObject()->VPhysicsGetObject())
	{
		return false;
	}
	if (!pEntity->GetEngineObject()->VPhysicsGetObject())
		return false;
	if (m_frameCount == gpGlobals->framecount)
	{
		return true;
	}
	m_frameCount = gpGlobals->framecount;
	//Adrian: Oops, our object became motion disabled, let go!
	IPhysicsObject* pPhys = pEntity->GetEngineObject()->VPhysicsGetObject();
	if (pPhys && pPhys->IsMoveable() == false)
	{
		return false;
	}

	Vector forward, right, up;
	QAngle playerAngles = pPlayer->EyeAngles();
	float pitch = AngleDistance(playerAngles.x, 0);
	if (!m_bAllowObjectOverhead)
	{
		playerAngles.x = clamp(pitch, -75, 75);
	}
	else
	{
		playerAngles.x = clamp(pitch, -90, 75);
	}
	AngleVectors(playerAngles, &forward, &right, &up);

	if (HL2GameRules()->MegaPhyscannonActive())
	{
		Vector los = (pEntity->WorldSpaceCenter() - pPlayer->Weapon_ShootPosition());
		VectorNormalize(los);

		float flDot = DotProduct(los, forward);

		//Let go of the item if we turn around too fast.
		if (flDot <= 0.35f)
			return false;
	}

	Vector start = pPlayer->Weapon_ShootPosition();

	// If the player is upside down then we need to hold the box closer to their feet.
	if (up.z < 0.0f)
		start += pPlayer->GetViewOffset() * up.z;
	if (right.z < 0.0f)
		start += pPlayer->GetViewOffset() * right.z;


	// Find out if it's being held across a portal
	bool bLookingAtHeldPortal = true;
	IEnginePortalServer* pPortal = pPlayer->GetEnginePlayer()->GetHeldObjectPortal();

	if (!pPortal)
	{
		// If the portal is invalid make sure we don't try to hold it across the portal
		pPlayer->GetEnginePlayer()->SetHeldObjectOnOppositeSideOfPortal(false);
	}

	if (pPlayer->GetEnginePlayer()->IsHeldObjectOnOppositeSideOfPortal())
	{
		Ray_t rayPortalTest;
		rayPortalTest.Init(start, start + forward * 1024.0f);

		// Check if we're looking at the portal we're holding through
		if (pPortal)
		{
			if (UTIL_IntersectRayWithPortal(rayPortalTest, pPortal) < 0.0f)
			{
				bLookingAtHeldPortal = false;
			}
		}
		// If our end point hasn't gone into the portal yet we at least need to know what portal is in front of us
		else
		{
			int iPortalCount = gEntList.m_ActivePortals.Count();
			if (iPortalCount != 0)
			{
				CEnginePortalInternal** pPortals = gEntList.m_ActivePortals.Base();
				float fMinDist = 2.0f;
				for (int i = 0; i != iPortalCount; ++i)
				{
					CEnginePortalInternal* pTempPortal = pPortals[i];
					if (pTempPortal->IsActivated() &&
						(pTempPortal->GetLinkedPortal() != NULL))
					{
						float fDist = UTIL_IntersectRayWithPortal(rayPortalTest, pTempPortal);
						if ((fDist >= 0.0f) && (fDist < fMinDist))
						{
							fMinDist = fDist;
							pPortal = pTempPortal;
						}
					}
				}
			}
		}
	}
	else
	{
		pPortal = NULL;
	}

	QAngle qEntityAngles = pEntity->GetEngineObject()->GetAbsAngles();

	if (pPortal)
	{
		// If the portal isn't linked we need to drop the object
		if (!pPortal->GetLinkedPortal())
		{
			pPlayer->ForceDropOfCarriedPhysObjects();
			return false;
		}

		UTIL_Portal_AngleTransform(pPortal->GetLinkedPortal()->MatrixThisToLinked(), qEntityAngles, qEntityAngles);
	}
	// Now clamp a sphere of object radius at end to the player's bbox
	Vector radial = EntityList()->PhysGetCollision()->CollideGetExtent(pPhys->GetCollide(), vec3_origin, qEntityAngles, -forward);
	Vector player2d = pPlayer->GetEngineObject()->OBBMaxs();
	float playerRadius = player2d.Length2D();

	float radius = playerRadius + radial.Length();//float radius = playerRadius + fabs(DotProduct( forward, radial ));

	float distance = 24 + (radius * 2.0f);

	// Add the prop's distance offset
	distance += m_flDistanceOffset;

	Vector end = start + (forward * distance);

	trace_t	tr;
	CTraceFilterSkipTwoEntities traceFilter(pPlayer, pEntity, COLLISION_GROUP_NONE);
	Ray_t ray;
	ray.Init(start, end);
	//enginetrace->TraceRay( ray, MASK_SOLID_BRUSHONLY, &traceFilter, &tr );
	UTIL_Portal_TraceRay(ray, MASK_SOLID_BRUSHONLY, &traceFilter, &tr);//enginetrace->TraceRay( ray, MASK_SOLID_BRUSHONLY, &traceFilter, &tr );

	if (tr.fraction < 0.5)
	{
		end = start + forward * (radius * 0.5f);
	}
	else if (tr.fraction <= 1.0f)
	{
		end = start + forward * (distance - radius);
	}
	Vector playerMins, playerMaxs, nearest;
	pPlayer->GetEngineObject()->WorldSpaceAABB(&playerMins, &playerMaxs);
	Vector playerLine = pPlayer->GetEngineObject()->WorldSpaceCenter();
	CalcClosestPointOnLine(end, playerLine + Vector(0, 0, playerMins.z), playerLine + Vector(0, 0, playerMaxs.z), nearest, NULL);

	if (!m_bAllowObjectOverhead)
	{
		Vector delta = end - nearest;
		float len = VectorNormalize(delta);
		if (len < radius)
		{
			end = nearest + radius * delta;
		}
	}

	//Show overlays of radius
	ConVarRef	g_debug_physcannon("g_debug_physcannon");
	if (g_debug_physcannon.GetBool())
	{
		NDebugOverlay::Box(end, -Vector(2, 2, 2), Vector(2, 2, 2), 0, 255, 0, true, 0);

		NDebugOverlay::Box(GetAttached()->WorldSpaceCenter(),
			-Vector(radius, radius, radius),
			Vector(radius, radius, radius),
			255, 0, 0,
			true,
			0.0f);
	}

	QAngle angles = TransformAnglesFromPlayerSpace(m_attachedAnglesPlayerSpace, pPlayer);

	// If it has a preferred orientation, update to ensure we're still oriented correctly.
	Pickup_GetPreferredCarryAngles(pEntity, pPlayer, pPlayer->GetEngineObject()->EntityToWorldTransform(), angles);

	// We may be holding a prop that has preferred carry angles
	if (m_bHasPreferredCarryAngles)
	{
		matrix3x4_t tmp;
		ComputePlayerMatrix(pPlayer, tmp);
		angles = TransformAnglesToWorldSpace(m_vecPreferredCarryAngles, tmp);
	}

	matrix3x4_t attachedToWorld;
	Vector offset;
	AngleMatrix(angles, attachedToWorld);
	VectorRotate(m_attachedPositionObjectSpace, attachedToWorld, offset);

	// Translate hold position and angles across portal
	if (pPlayer->GetEnginePlayer()->IsHeldObjectOnOppositeSideOfPortal())
	{
		IEnginePortalServer* pPortalLinked = pPortal->GetLinkedPortal();
		if (pPortal && pPortal->IsActivated() && pPortalLinked != NULL)
		{
			Vector vTeleportedPosition;
			QAngle qTeleportedAngles;

			if (!bLookingAtHeldPortal && (start - pPortal->AsEngineObject()->GetAbsOrigin()).Length() > distance - radius)
			{
				// Pull the object through the portal
				Vector vPortalLinkedForward;
				pPortalLinked->AsEngineObject()->GetVectors(&vPortalLinkedForward, NULL, NULL);
				vTeleportedPosition = pPortalLinked->AsEngineObject()->GetAbsOrigin() - vPortalLinkedForward * (1.0f + offset.Length());
				qTeleportedAngles = pPortalLinked->AsEngineObject()->GetAbsAngles();
			}
			else
			{
				// Translate hold position and angles across the portal
				VMatrix matThisToLinked = pPortal->MatrixThisToLinked();
				UTIL_Portal_PointTransform(matThisToLinked, end - offset, vTeleportedPosition);
				UTIL_Portal_AngleTransform(matThisToLinked, angles, qTeleportedAngles);
			}

			SetTargetPosition(vTeleportedPosition, qTeleportedAngles);
			pPlayer->GetEnginePlayer()->SetHeldObjectPortal(pPortal);
		}
		else
		{
			pPlayer->ForceDropOfCarriedPhysObjects();
		}
	}
	else
	{
		SetTargetPosition(end - offset, angles);
		pPlayer->GetEnginePlayer()->SetHeldObjectPortal(NULL);
	}

	return true;
}

void CGrabControllerInternal::SetTargetPosition(const Vector& target, const QAngle& targetOrientation)
{
	m_shadow.targetPosition = target;
	m_shadow.targetRotation = targetOrientation;

	m_timeToArrive = gpGlobals->frametime;

	CBaseEntity* pAttached = GetAttached();
	if (pAttached)
	{
		IPhysicsObject* pObj = pAttached->GetEngineObject()->VPhysicsGetObject();

		if (pObj != NULL)
		{
			pObj->Wake();
		}
		else
		{
			DetachEntity(false);//DetachEntity();
		}
	}
}

void CGrabControllerInternal::GetTargetPosition(Vector* target, QAngle* targetOrientation)
{
	if (target)
		*target = m_shadow.targetPosition;

	if (targetOrientation)
		*targetOrientation = m_shadow.targetRotation;
}
//-----------------------------------------------------------------------------
// Purpose: 
// Output : float
//-----------------------------------------------------------------------------
float CGrabControllerInternal::ComputeError()
{
	if (m_errorTime <= 0)
		return 0;

	CBaseEntity* pAttached = GetAttached();
	if (pAttached)
	{
		Vector pos;
		IPhysicsObject* pObj = pAttached->GetEngineObject()->VPhysicsGetObject();

		if (pObj)
		{
			pObj->GetShadowPosition(&pos, NULL);

			float error = (m_shadow.targetPosition - pos).Length();
			if (m_errorTime > 0)
			{
				if (m_errorTime > 1)
				{
					m_errorTime = 1;
				}
				float speed = error / m_errorTime;
				if (speed > m_shadow.maxSpeed)
				{
					error *= 0.5;
				}
				m_error = (1 - m_errorTime) * m_error + error * m_errorTime;
			}
		}
		else
		{
			DevMsg("Object attached to Physcannon has no physics object\n");
			DetachEntity(false);//DetachEntity();
			return 9999; // force detach
		}
	}

	if (pAttached->GetEngineObject()->IsEFlagSet(EFL_IS_BEING_LIFTED_BY_BARNACLE))
	{
		m_error *= 3.0f;
	}

	// If held across a portal but not looking at the portal multiply error
	CBasePlayer* pPortalPlayer = gEntList.GetPlayerHoldingEntity(pAttached);
	Assert(pPortalPlayer);
	if (pPortalPlayer->GetEnginePlayer()->IsHeldObjectOnOppositeSideOfPortal())
	{
		Vector forward, right, up;
		QAngle playerAngles = pPortalPlayer->EyeAngles();

		float pitch = AngleDistance(playerAngles.x, 0);
		playerAngles.x = clamp(pitch, -75, 75);
		AngleVectors(playerAngles, &forward, &right, &up);

		Vector start = pPortalPlayer->Weapon_ShootPosition();

		// If the player is upside down then we need to hold the box closer to their feet.
		if (up.z < 0.0f)
			start += pPortalPlayer->GetViewOffset() * up.z;
		if (right.z < 0.0f)
			start += pPortalPlayer->GetViewOffset() * right.z;

		Ray_t rayPortalTest;
		rayPortalTest.Init(start, start + forward * 256.0f);

		if (UTIL_IntersectRayWithPortal(rayPortalTest, pPortalPlayer->GetEnginePlayer()->GetHeldObjectPortal()) < 0.0f)
		{
			m_error *= 2.5f;
		}
	}

	m_errorTime = 0;

	return m_error;
}


#define MASS_SPEED_SCALE	60
#define MAX_MASS			40

void CGrabControllerInternal::ComputeMaxSpeed(CBaseEntity* pEntity, IPhysicsObject* pPhysics)
{
	m_shadow.maxSpeed = 1000;
	m_shadow.maxAngular = DEFAULT_MAX_ANGULAR;

	// Compute total mass...
	float flMass = PhysGetEntityMass(pEntity);
	ConVarRef physcannon_maxmass("physcannon_maxmass");
	float flMaxMass = physcannon_maxmass.GetFloat();
	if (flMass <= flMaxMass)
		return;

	float flLerpFactor = clamp(flMass, flMaxMass, 500.0f);
	flLerpFactor = SimpleSplineRemapVal(flLerpFactor, flMaxMass, 500.0f, 0.0f, 1.0f);

	float invMass = pPhysics->GetInvMass();
	float invInertia = pPhysics->GetInvInertia().Length();

	float invMaxMass = 1.0f / MAX_MASS;
	float ratio = invMaxMass / invMass;
	invMass = invMaxMass;
	invInertia *= ratio;

	float maxSpeed = invMass * MASS_SPEED_SCALE * 200;
	float maxAngular = invInertia * MASS_SPEED_SCALE * 360;

	m_shadow.maxSpeed = Lerp(flLerpFactor, m_shadow.maxSpeed, maxSpeed);
	m_shadow.maxAngular = Lerp(flLerpFactor, m_shadow.maxAngular, maxAngular);
}


QAngle CGrabControllerInternal::TransformAnglesToPlayerSpace(const QAngle& anglesIn, CBasePlayer* pPlayer)
{
	if (m_bIgnoreRelativePitch)
	{
		matrix3x4_t test;
		QAngle angleTest = pPlayer->EyeAngles();
		angleTest.x = 0;
		AngleMatrix(angleTest, test);
		return TransformAnglesToLocalSpace(anglesIn, test);
	}
	return TransformAnglesToLocalSpace(anglesIn, pPlayer->GetEngineObject()->EntityToWorldTransform());
}

QAngle CGrabControllerInternal::TransformAnglesFromPlayerSpace(const QAngle& anglesIn, CBasePlayer* pPlayer)
{
	if (m_bIgnoreRelativePitch)
	{
		matrix3x4_t test;
		QAngle angleTest = pPlayer->EyeAngles();
		angleTest.x = 0;
		AngleMatrix(angleTest, test);
		return TransformAnglesToWorldSpace(anglesIn, test);
	}
	return TransformAnglesToWorldSpace(anglesIn, pPlayer->GetEngineObject()->EntityToWorldTransform());
}

//-----------------------------------------------------------------------------
// Purpose: Finds the nearest ragdoll sub-piece to a location and returns it
// Input  : *pTarget - entity that is the potential ragdoll
//			&position - position we're testing against
// Output : IPhysicsObject - sub-object (if any)
//-----------------------------------------------------------------------------
IPhysicsObject* GetRagdollChildAtPosition(CBaseEntity* pTarget, const Vector& position)
{
	// Check for a ragdoll
	if (dynamic_cast<CRagdollProp*>(pTarget) == NULL)
		return NULL;

	// Get the root
	IPhysicsObject* pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	int count = pTarget->GetEngineObject()->VPhysicsGetObjectList(pList, ARRAYSIZE(pList));

	IPhysicsObject* pBestChild = NULL;
	float			flBestDist = 99999999.0f;
	float			flDist;
	Vector			vPos;

	// Find the nearest child to where we're looking
	for (int i = 0; i < count; i++)
	{
		pList[i]->GetPosition(&vPos, NULL);

		flDist = (position - vPos).LengthSqr();

		if (flDist < flBestDist)
		{
			pBestChild = pList[i];
			flBestDist = flDist;
		}
	}

	// Make this our base now
	pTarget->GetEngineObject()->VPhysicsSwapObject(pBestChild);

	return pTarget->GetEngineObject()->VPhysicsGetObject();
}

#define SIGN(x) ( (x) < 0 ? -1 : 1 )

static void MatrixOrthogonalize(matrix3x4_t& matrix, int column)
{
	Vector columns[3];
	int i;

	for (i = 0; i < 3; i++)
	{
		MatrixGetColumn(matrix, i, columns[i]);
	}

	int index0 = column;
	int index1 = (column + 1) % 3;
	int index2 = (column + 2) % 3;

	columns[index2] = CrossProduct(columns[index0], columns[index1]);
	columns[index1] = CrossProduct(columns[index2], columns[index0]);
	VectorNormalize(columns[index2]);
	VectorNormalize(columns[index1]);
	MatrixSetColumn(columns[index1], index1, matrix);
	MatrixSetColumn(columns[index2], index2, matrix);
}

static QAngle AlignAngles(const QAngle& angles, float cosineAlignAngle)
{
	matrix3x4_t alignMatrix;
	AngleMatrix(angles, alignMatrix);

	// NOTE: Must align z first
	for (int j = 3; --j >= 0; )
	{
		Vector vec;
		MatrixGetColumn(alignMatrix, j, vec);
		for (int i = 0; i < 3; i++)
		{
			if (fabs(vec[i]) > cosineAlignAngle)
			{
				vec[i] = SIGN(vec[i]);
				vec[(i + 1) % 3] = 0;
				vec[(i + 2) % 3] = 0;
				MatrixSetColumn(vec, j, alignMatrix);
				MatrixOrthogonalize(alignMatrix, j);
				break;
			}
		}
	}

	QAngle out;
	MatrixAngles(alignMatrix, out);
	return out;
}

void CGrabControllerInternal::AttachEntity(CBasePlayer* pPlayer, CBaseEntity* pEntity, IPhysicsObject* pPhys, bool bIsMegaPhysCannon, const Vector& vGrabPosition, bool bUseGrabPosition)
{
	// play the impact sound of the object hitting the player
	// used as feedback to let the player know he picked up the object
	if (!pPlayer->GetEnginePlayer()->IsSilentDropAndPickup())
	{
		int hitMaterial = pPhys->GetMaterialIndex();
		int playerMaterial = pPlayer->GetEngineObject()->VPhysicsGetObject() ? pPlayer->GetEngineObject()->VPhysicsGetObject()->GetMaterialIndex() : hitMaterial;
		gEntList.PhysicsImpactSound(pPlayer, pPhys, CHAN_STATIC, hitMaterial, playerMaterial, 1.0, 64);
	}
	Vector position;
	QAngle angles;
	pPhys->GetPosition(&position, &angles);
	// If it has a preferred orientation, use that instead.
	Pickup_GetPreferredCarryAngles(pEntity, pPlayer, pPlayer->GetEngineObject()->EntityToWorldTransform(), angles);

	//Fix attachment orientation weirdness
	if (pPlayer->GetEnginePlayer()->IsHeldObjectOnOppositeSideOfPortal())
	{
		Vector vPlayerForward;
		pPlayer->EyeVectors(&vPlayerForward);

		Vector radial = EntityList()->PhysGetCollision()->CollideGetExtent(pPhys->GetCollide(), vec3_origin, pEntity->GetEngineObject()->GetAbsAngles(), -vPlayerForward);
		Vector player2d = pPlayer->GetEngineObject()->OBBMaxs();
		float playerRadius = player2d.Length2D();
		float flDot = DotProduct(vPlayerForward, radial);

		float radius = playerRadius + fabs(flDot);

		float distance = 24 + (radius * 2.0f);

		//find out which portal the object is on the other side of....
		Vector start = pPlayer->Weapon_ShootPosition();
		Vector end = start + (vPlayerForward * distance);

		IEnginePortalServer* pObjectPortal = NULL;
		pObjectPortal = pPlayer->GetEnginePlayer()->GetHeldObjectPortal();

		// If our end point hasn't gone into the portal yet we at least need to know what portal is in front of us
		if (!pObjectPortal)
		{
			Ray_t rayPortalTest;
			rayPortalTest.Init(start, start + vPlayerForward * 1024.0f);

			int iPortalCount = gEntList.m_ActivePortals.Count();
			if (iPortalCount != 0)
			{
				CEnginePortalInternal** pPortals = gEntList.m_ActivePortals.Base();
				float fMinDist = 2.0f;
				for (int i = 0; i != iPortalCount; ++i)
				{
					CEnginePortalInternal* pTempPortal = pPortals[i];
					if (pTempPortal->m_bActivated &&
						(pTempPortal->GetLinkedPortal() != NULL))
					{
						float fDist = UTIL_IntersectRayWithPortal(rayPortalTest, pTempPortal);
						if ((fDist >= 0.0f) && (fDist < fMinDist))
						{
							fMinDist = fDist;
							pObjectPortal = pTempPortal;
						}
					}
				}
			}
		}

		if (pObjectPortal)
		{
			UTIL_Portal_AngleTransform(pObjectPortal->GetLinkedPortal()->MatrixThisToLinked(), angles, angles);
		}
	}

	VectorITransform(pEntity->WorldSpaceCenter(), pEntity->GetEngineObject()->EntityToWorldTransform(), m_attachedPositionObjectSpace);
	//	ComputeMaxSpeed( pEntity, pPhys );

		// If we haven't been killed by a grab, we allow the gun to grab the nearest part of a ragdoll
	if (bUseGrabPosition)
	{
		IPhysicsObject* pChild = GetRagdollChildAtPosition(pEntity, vGrabPosition);

		if (pChild)
		{
			pPhys = pChild;
		}
	}

	// Carried entities can never block LOS
	m_bCarriedEntityBlocksLOS = pEntity->BlocksLOS();
	pEntity->SetBlocksLOS(false);
	m_controller = EntityList()->PhysGetEnv()->CreateMotionController(this);
	m_controller->AttachObject(pPhys, true);
	// Don't do this, it's causing trouble with constraint solvers.
	//m_controller->SetPriority( IPhysicsMotionController::HIGH_PRIORITY );

	pPhys->Wake();
	PhysSetGameFlags(pPhys, FVPHYSICS_PLAYER_HELD);
	SetTargetPosition(position, angles);
	m_attachedEntity = pEntity;
	IPhysicsObject* pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	int count = pEntity->GetEngineObject()->VPhysicsGetObjectList(pList, ARRAYSIZE(pList));
	m_flLoadWeight = 0;
	float damping = 10;
	float flFactor = count / 7.5f;
	if (flFactor < 1.0f)
	{
		flFactor = 1.0f;
	}
	for (int i = 0; i < count; i++)
	{
		float mass = pList[i]->GetMass();
		pList[i]->GetDamping(NULL, &m_savedRotDamping[i]);
		m_flLoadWeight += mass;
		m_savedMass[i] = mass;

		// reduce the mass to prevent the player from adding crazy amounts of energy to the system
		pList[i]->SetMass(REDUCED_CARRY_MASS / flFactor);
		pList[i]->SetDamping(NULL, &damping);
	}

	// NVNT setting m_pControllingPlayer to the player attached
	m_pControllingPlayer = pPlayer;

	// Give extra mass to the phys object we're actually picking up
	pPhys->SetMass(REDUCED_CARRY_MASS);
	pPhys->EnableDrag(false);

	m_errorTime = bIsMegaPhysCannon ? -1.5f : -1.0f; // 1 seconds until error starts accumulating
	m_error = 0;
	m_contactAmount = 0;

	m_attachedAnglesPlayerSpace = TransformAnglesToPlayerSpace(angles, pPlayer);
	if (m_angleAlignment != 0)
	{
		m_attachedAnglesPlayerSpace = AlignAngles(m_attachedAnglesPlayerSpace, m_angleAlignment);
	}

	// Ragdolls don't offset this way
	if (dynamic_cast<CRagdollProp*>(pEntity))
	{
		m_attachedPositionObjectSpace.Init();
	}
	else
	{
		VectorITransform(pEntity->WorldSpaceCenter(), pEntity->GetEngineObject()->EntityToWorldTransform(), m_attachedPositionObjectSpace);
	}

	// If it's a prop, see if it has desired carry angles
	CPhysicsProp* pProp = dynamic_cast<CPhysicsProp*>(pEntity);
	if (pProp)
	{
		m_bHasPreferredCarryAngles = pProp->GetPropDataAngles("preferred_carryangles", m_vecPreferredCarryAngles);
		m_flDistanceOffset = pProp->GetCarryDistanceOffset();
	}
	else
	{
		m_bHasPreferredCarryAngles = false;
		m_flDistanceOffset = 0;
	}

	m_bAllowObjectOverhead = IsObjectAllowedOverhead(pEntity);
}

static void ClampPhysicsVelocity(IPhysicsObject* pPhys, float linearLimit, float angularLimit)
{
	Vector vel;
	AngularImpulse angVel;
	pPhys->GetVelocity(&vel, &angVel);
	float speed = VectorNormalize(vel) - linearLimit;
	float angSpeed = VectorNormalize(angVel) - angularLimit;
	speed = speed < 0 ? 0 : -speed;
	angSpeed = angSpeed < 0 ? 0 : -angSpeed;
	vel *= speed;
	angVel *= angSpeed;
	pPhys->AddVelocity(&vel, &angVel);
}

void CGrabControllerInternal::DetachEntity(bool bClearVelocity)
{
	Assert(!gEntList.PhysIsInCallback());
	CBaseEntity* pEntity = GetAttached();
	if (pEntity)
	{
		// Restore the LS blocking state
		pEntity->SetBlocksLOS(m_bCarriedEntityBlocksLOS);
		IPhysicsObject* pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
		int count = pEntity->GetEngineObject()->VPhysicsGetObjectList(pList, ARRAYSIZE(pList));
		for (int i = 0; i < count; i++)
		{
			IPhysicsObject* pPhys = pList[i];
			if (!pPhys)
				continue;

			// on the odd chance that it's gone to sleep while under anti-gravity
			pPhys->EnableDrag(true);
			pPhys->Wake();
			pPhys->SetMass(m_savedMass[i]);
			pPhys->SetDamping(NULL, &m_savedRotDamping[i]);
			PhysClearGameFlags(pPhys, FVPHYSICS_PLAYER_HELD);
			if (bClearVelocity)// pPhys->GetContactPoint( NULL, NULL ) 
			{
				pEntity->GetEngineObject()->PhysForceClearVelocity(pPhys);
			}
			else
			{
				ConVarRef hl2_normspeed("hl2_normspeed");
				ClampPhysicsVelocity(pPhys, hl2_normspeed.GetFloat() * 1.5f, 2.0f * 360.0f);
			}

		}
	}

	m_attachedEntity = NULL;
	if (m_controller) {
		EntityList()->PhysGetEnv()->DestroyMotionController(m_controller);
		m_controller = NULL;
	}
}

static bool InContactWithHeavyObject(IPhysicsObject* pObject, float heavyMass)
{
	bool contact = false;
	IPhysicsFrictionSnapshot* pSnapshot = pObject->CreateFrictionSnapshot();
	while (pSnapshot->IsValid())
	{
		IPhysicsObject* pOther = pSnapshot->GetObject(1);
		if (!pOther->IsMoveable() || pOther->GetMass() > heavyMass)
		{
			contact = true;
			break;
		}
		pSnapshot->NextFrictionData();
	}
	pObject->DestroyFrictionSnapshot(pSnapshot);
	return contact;
}

IMotionEvent::simresult_e CGrabControllerInternal::Simulate(IPhysicsMotionController* pController, IPhysicsObject* pObject, float deltaTime, Vector& linear, AngularImpulse& angular)
{
	game_shadowcontrol_params_t shadowParams = m_shadow;
	if (InContactWithHeavyObject(pObject, GetLoadWeight()))
	{
		m_contactAmount = Approach(0.1f, m_contactAmount, deltaTime * 2.0f);
	}
	else
	{
		m_contactAmount = Approach(1.0f, m_contactAmount, deltaTime * 2.0f);
	}
	shadowParams.maxAngular = m_shadow.maxAngular * m_contactAmount * m_contactAmount * m_contactAmount;
	m_timeToArrive = pObject->ComputeShadowControl(shadowParams, m_timeToArrive, deltaTime);

	// Slide along the current contact points to fix bouncing problems
	Vector velocity;
	AngularImpulse angVel;
	pObject->GetVelocity(&velocity, &angVel);
	PhysComputeSlideDirection(pObject, velocity, angVel, &velocity, &angVel, GetLoadWeight());
	pObject->SetVelocityInstantaneous(&velocity, NULL);

	linear.Init();
	angular.Init();
	m_errorTime += deltaTime;

	return SIM_LOCAL_ACCELERATION;
}

float CGrabControllerInternal::GetSavedMass(IPhysicsObject* pObject)
{
	CBaseEntity* pHeld = m_attachedEntity;
	if (pHeld)
	{
		if (pObject->GetGameData() == (void*)pHeld)
		{
			IPhysicsObject* pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
			int count = pHeld->GetEngineObject()->VPhysicsGetObjectList(pList, ARRAYSIZE(pList));
			for (int i = 0; i < count; i++)
			{
				if (pList[i] == pObject)
					return m_savedMass[i];
			}
		}
	}
	return 0.0f;
}

void CGrabControllerInternal::GetSavedParamsForCarriedPhysObject(IPhysicsObject* pObject, float* pSavedMassOut, float* pSavedRotationalDampingOut)
{
	CBaseEntity* pHeld = m_attachedEntity;
	if (pHeld)
	{
		if (pObject->GetGameData() == (void*)pHeld)
		{
			IPhysicsObject* pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
			int count = pHeld->GetEngineObject()->VPhysicsGetObjectList(pList, ARRAYSIZE(pList));
			for (int i = 0; i < count; i++)
			{
				if (pList[i] == pObject)
				{
					if (pSavedMassOut)
						*pSavedMassOut = m_savedMass[i];

					if (pSavedRotationalDampingOut)
						*pSavedRotationalDampingOut = m_savedRotDamping[i];

					return;
				}
			}
		}
	}

	if (pSavedMassOut)
		*pSavedMassOut = 0.0f;

	if (pSavedRotationalDampingOut)
		*pSavedRotationalDampingOut = 0.0f;

	return;
}

//-----------------------------------------------------------------------------
// Is this an object that the player is allowed to lift to a position 
// directly overhead? The default behavior prevents lifting objects directly
// overhead, but there are exceptions for gameplay purposes.
//-----------------------------------------------------------------------------
bool CGrabControllerInternal::IsObjectAllowedOverhead(CBaseEntity* pEntity)
{
	// Allow combine balls overhead 
	if (pEntity->IsCombineBall())
		return true;

	// Allow props that are specifically flagged as such
	CPhysicsProp* pPhysProp = dynamic_cast<CPhysicsProp*>(pEntity);
	if (pPhysProp != NULL && pPhysProp->HasInteraction(PROPINTER_PHYSGUN_ALLOW_OVERHEAD))
		return true;

	// String checks are fine here, we only run this code one time- when the object is picked up.
	if (pEntity->ClassMatches("grenade_helicopter"))
		return true;

	if (pEntity->ClassMatches("weapon_striderbuster"))
		return true;

	return false;
}

void CGrabControllerInternal::SetPortalPenetratingEntity(CBaseEntity* pPenetrated)
{
	m_PenetratedEntity = pPenetrated;
}

class CThinkContextsSaveDataOps : public CDefSaveRestoreOps
{
	virtual void Save(const SaveRestoreFieldInfo_t& fieldInfo, ISave* pSave)
	{
		AssertMsg(fieldInfo.pTypeDesc->fieldSize == 1, "CThinkContextsSaveDataOps does not support arrays");

		// Write out the vector
		CUtlVector< thinkfunc_t >* pUtlVector = (CUtlVector< thinkfunc_t > *)fieldInfo.pField;
		SaveUtlVector(pSave, pUtlVector, FIELD_EMBEDDED);

		// Get our owner
		CEngineObjectInternal* pOwner = (CEngineObjectInternal*)fieldInfo.pOwner;

		pSave->StartBlock();
		// Now write out all the functions
		for (int i = 0; i < pUtlVector->Size(); i++)
		{
			thinkfunc_t* thinkfun = &pUtlVector->Element(i);
#ifdef WIN32
			void** ppV = (void**)&((*pUtlVector)[i].m_pfnThink);
#else
			THINKPTR* ppV = &((*pUtlVector)[i].m_pfnThink);
#endif
			bool bHasFunc = (*ppV != NULL);
			pSave->WriteBool(&bHasFunc, 1);
			if (bHasFunc)
			{
				pSave->WriteFunction(pOwner->GetOuter()->GetDataDescMap(), "m_pfnThink", (inputfunc_t**)ppV, 1);
			}
		}
		pSave->EndBlock();
	}

	virtual void Restore(const SaveRestoreFieldInfo_t& fieldInfo, IRestore* pRestore)
	{
		AssertMsg(fieldInfo.pTypeDesc->fieldSize == 1, "CThinkContextsSaveDataOps does not support arrays");

		// Read in the vector
		CUtlVector< thinkfunc_t >* pUtlVector = (CUtlVector< thinkfunc_t > *)fieldInfo.pField;
		RestoreUtlVector(pRestore, pUtlVector, FIELD_EMBEDDED);

		// Get our owner
		CEngineObjectInternal* pOwner = (CEngineObjectInternal*)fieldInfo.pOwner;

		pRestore->StartBlock();
		// Now read in all the functions
		for (int i = 0; i < pUtlVector->Size(); i++)
		{
			thinkfunc_t* thinkfun = &pUtlVector->Element(i);
			bool bHasFunc;
			pRestore->ReadBool(&bHasFunc, 1);
#ifdef WIN32
			void** ppV = (void**)&((*pUtlVector)[i].m_pfnThink);
#else
			THINKPTR* ppV = &((*pUtlVector)[i].m_pfnThink);
			Q_memset((void*)ppV, 0x0, sizeof(inputfunc_t));
#endif
			if (bHasFunc)
			{
				SaveRestoreRecordHeader_t header;
				pRestore->ReadHeader(&header);
				pRestore->ReadFunction(pOwner->GetOuter()->GetDataDescMap(), (inputfunc_t**)ppV, 1, header.size);
			}
			else
			{
				*ppV = NULL;
			}
		}
		pRestore->EndBlock();
	}

	virtual bool IsEmpty(const SaveRestoreFieldInfo_t& fieldInfo)
	{
		CUtlVector< thinkfunc_t >* pUtlVector = (CUtlVector< thinkfunc_t > *)fieldInfo.pField;
		return (pUtlVector->Count() == 0);
	}

	virtual void MakeEmpty(const SaveRestoreFieldInfo_t& fieldInfo)
	{
		THINKPTR pFunc = *((THINKPTR*)fieldInfo.pField);
		pFunc = NULL;
	}
};

CThinkContextsSaveDataOps g_ThinkContextsSaveDataOps;
ISaveRestoreOps* thinkcontextFuncs = &g_ThinkContextsSaveDataOps;

class CIKSaveRestoreOps : public CClassPtrSaveRestoreOps
{
	// save data type interface
	void Save(const SaveRestoreFieldInfo_t& fieldInfo, ISave* pSave)
	{
		Assert(fieldInfo.pTypeDesc->fieldSize == 1);
		CIKContext** pIK = (CIKContext**)fieldInfo.pField;
		bool bHasIK = (*pIK) != 0;
		pSave->WriteBool(&bHasIK);
	}

	void Restore(const SaveRestoreFieldInfo_t& fieldInfo, IRestore* pRestore)
	{
		Assert(fieldInfo.pTypeDesc->fieldSize == 1);
		CIKContext** pIK = (CIKContext**)fieldInfo.pField;

		bool bHasIK;
		pRestore->ReadBool(&bHasIK);
		*pIK = (bHasIK) ? new CIKContext : NULL;
	}
};

static CIKSaveRestoreOps s_IKSaveRestoreOp;

BEGIN_SIMPLE_DATADESC(thinkfunc_t)
	DEFINE_FIELD(m_iszContext, FIELD_STRING),
	// DEFINE_FIELD( m_pfnThink,		FIELD_FUNCTION ),		// Manually written
	DEFINE_FIELD(m_nNextThinkTick, FIELD_TICK),
	DEFINE_FIELD(m_nLastThinkTick, FIELD_TICK),
END_DATADESC()

#define DEFINE_RAGDOLL_ELEMENT( i ) \
	DEFINE_FIELD( m_ragdoll.list[i].originParentSpace, FIELD_VECTOR ), \
	DEFINE_PHYSPTR( m_ragdoll.list[i].pObject ), \
	DEFINE_PHYSPTR( m_ragdoll.list[i].pConstraint ), \
	DEFINE_FIELD( m_ragdoll.list[i].parentIndex, FIELD_INTEGER )

BEGIN_DATADESC_NO_BASE(CEngineObjectInternal)
	DEFINE_FIELD(m_vecOrigin, FIELD_VECTOR),			// NOTE: MUST BE IN LOCAL SPACE, NOT POSITION_VECTOR!!! (see CBaseEntity::Restore)
	DEFINE_FIELD(m_angRotation, FIELD_VECTOR),
	DEFINE_KEYFIELD(m_vecVelocity, FIELD_VECTOR, "velocity"),
	DEFINE_FIELD(m_vecAbsOrigin, FIELD_POSITION_VECTOR),
	DEFINE_FIELD(m_angAbsRotation, FIELD_VECTOR),
	DEFINE_FIELD(m_vecAbsVelocity, FIELD_VECTOR),
	DEFINE_ARRAY( m_rgflCoordinateFrame, FIELD_FLOAT, 12 ), // NOTE: MUST BE IN LOCAL SPACE, NOT POSITION_VECTOR!!! (see CBaseEntity::Restore)
	DEFINE_GLOBAL_FIELD(m_hMoveParent, FIELD_EHANDLE),
	DEFINE_GLOBAL_FIELD(m_hMoveChild, FIELD_EHANDLE),
	DEFINE_GLOBAL_FIELD(m_hMovePeer, FIELD_EHANDLE),
	DEFINE_KEYFIELD(m_iClassname, FIELD_STRING, "classname"),
	DEFINE_GLOBAL_KEYFIELD(m_iGlobalname, FIELD_STRING, "globalname"),
	DEFINE_KEYFIELD(m_iParent, FIELD_STRING, "parentname"),
	DEFINE_FIELD(m_iName, FIELD_STRING),
	DEFINE_FIELD(m_iParentAttachment, FIELD_CHARACTER),
	DEFINE_FIELD(m_fFlags, FIELD_INTEGER),
	DEFINE_FIELD(m_iEFlags, FIELD_INTEGER),
	DEFINE_FIELD(touchStamp, FIELD_INTEGER),
	DEFINE_FIELD(m_hGroundEntity, FIELD_EHANDLE),
	DEFINE_FIELD(m_flGroundChangeTime, FIELD_TIME),
	DEFINE_GLOBAL_KEYFIELD(m_ModelName, FIELD_MODELNAME, "model"),
	DEFINE_GLOBAL_KEYFIELD(m_nModelIndex, FIELD_SHORT, "modelindex"),
	DEFINE_KEYFIELD(m_spawnflags, FIELD_INTEGER, "spawnflags"),
	DEFINE_EMBEDDED(m_Collision),
	DEFINE_FIELD(m_CollisionGroup, FIELD_INTEGER),
	DEFINE_KEYFIELD(m_fEffects, FIELD_INTEGER, "effects"),
	DEFINE_KEYFIELD(m_flGravity, FIELD_FLOAT, "gravity"),
	DEFINE_KEYFIELD(m_flFriction, FIELD_FLOAT, "friction"),
	DEFINE_FIELD(m_flElasticity, FIELD_FLOAT),
	DEFINE_FIELD(m_pfnThink, FIELD_FUNCTION),
	DEFINE_KEYFIELD(m_nNextThinkTick, FIELD_TICK, "nextthink"),
	DEFINE_FIELD(m_nLastThinkTick, FIELD_TICK),
	DEFINE_CUSTOM_FIELD(m_aThinkFunctions, thinkcontextFuncs),
	DEFINE_FIELD(m_MoveType, FIELD_CHARACTER),
	DEFINE_FIELD(m_MoveCollide, FIELD_CHARACTER),
	DEFINE_FIELD(m_bSimulatedEveryTick, FIELD_BOOLEAN),
	DEFINE_FIELD(m_bAnimatedEveryTick, FIELD_BOOLEAN),
	DEFINE_FIELD(m_flAnimTime, FIELD_TIME),
	DEFINE_FIELD(m_flSimulationTime, FIELD_TIME),
	DEFINE_FIELD(m_bClientSideAnimation, FIELD_BOOLEAN),
	DEFINE_INPUT(m_nSkin, FIELD_INTEGER, "skin"),
	DEFINE_KEYFIELD(m_nBody, FIELD_INTEGER, "body"),
	DEFINE_INPUT(m_nBody, FIELD_INTEGER, "SetBodyGroup"),
	DEFINE_KEYFIELD(m_nHitboxSet, FIELD_INTEGER, "hitboxset"),
	DEFINE_FIELD(m_flModelScale, FIELD_FLOAT),
	DEFINE_KEYFIELD(m_flModelScale, FIELD_FLOAT, "modelscale"),
	DEFINE_ARRAY(m_flEncodedController, FIELD_FLOAT, NUM_BONECTRLS),
	DEFINE_FIELD(m_bClientSideFrameReset, FIELD_BOOLEAN),
	DEFINE_KEYFIELD(m_nSequence, FIELD_INTEGER, "sequence"),
	DEFINE_ARRAY(m_flPoseParameter, FIELD_FLOAT, NUM_POSEPAREMETERS),
	DEFINE_KEYFIELD(m_flPlaybackRate, FIELD_FLOAT, "playbackrate"),
	DEFINE_KEYFIELD(m_flCycle, FIELD_FLOAT, "cycle"),
	DEFINE_FIELD(m_nNewSequenceParity, FIELD_INTEGER),
	DEFINE_FIELD(m_nResetEventsParity, FIELD_INTEGER),
	DEFINE_FIELD(m_nMuzzleFlashParity, FIELD_CHARACTER),
	DEFINE_FIELD(m_flGroundSpeed, FIELD_FLOAT),
	DEFINE_FIELD(m_flLastEventCheck, FIELD_TIME),
	DEFINE_FIELD(m_bSequenceFinished, FIELD_BOOLEAN),
	DEFINE_FIELD(m_bSequenceLoops, FIELD_BOOLEAN),
	DEFINE_FIELD(m_flSpeedScale, FIELD_FLOAT),
	DEFINE_PHYSPTR(m_pPhysicsObject),
	DEFINE_AUTO_ARRAY(m_ragdoll.boneIndex, FIELD_INTEGER),
	DEFINE_AUTO_ARRAY(m_ragPos, FIELD_POSITION_VECTOR),
	DEFINE_AUTO_ARRAY(m_ragAngles, FIELD_VECTOR),
	DEFINE_FIELD(m_ragdoll.listCount, FIELD_INTEGER),
	DEFINE_FIELD(m_ragdoll.allowStretch, FIELD_BOOLEAN),
	DEFINE_PHYSPTR(m_ragdoll.pGroup),
	DEFINE_FIELD(m_lastUpdateTickCount, FIELD_INTEGER),
	DEFINE_FIELD(m_allAsleep, FIELD_BOOLEAN),
	DEFINE_AUTO_ARRAY(m_ragdollMins, FIELD_VECTOR),
	DEFINE_AUTO_ARRAY(m_ragdollMaxs, FIELD_VECTOR),
	DEFINE_KEYFIELD(m_anglesOverrideString, FIELD_STRING, "angleOverride"),
	DEFINE_RAGDOLL_ELEMENT(1),
	DEFINE_RAGDOLL_ELEMENT(2),
	DEFINE_RAGDOLL_ELEMENT(3),
	DEFINE_RAGDOLL_ELEMENT(4),
	DEFINE_RAGDOLL_ELEMENT(5),
	DEFINE_RAGDOLL_ELEMENT(6),
	DEFINE_RAGDOLL_ELEMENT(7),
	DEFINE_RAGDOLL_ELEMENT(8),
	DEFINE_RAGDOLL_ELEMENT(9),
	DEFINE_RAGDOLL_ELEMENT(10),
	DEFINE_RAGDOLL_ELEMENT(11),
	DEFINE_RAGDOLL_ELEMENT(12),
	DEFINE_RAGDOLL_ELEMENT(13),
	DEFINE_RAGDOLL_ELEMENT(14),
	DEFINE_RAGDOLL_ELEMENT(15),
	DEFINE_RAGDOLL_ELEMENT(16),
	DEFINE_RAGDOLL_ELEMENT(17),
	DEFINE_RAGDOLL_ELEMENT(18),
	DEFINE_RAGDOLL_ELEMENT(19),
	DEFINE_RAGDOLL_ELEMENT(20),
	DEFINE_RAGDOLL_ELEMENT(21),
	DEFINE_RAGDOLL_ELEMENT(22),
	DEFINE_RAGDOLL_ELEMENT(23),
	DEFINE_KEYFIELD(m_nRenderFX, FIELD_CHARACTER, "renderfx"),
	DEFINE_FIELD(m_nOverlaySequence, FIELD_INTEGER),
	DEFINE_CUSTOM_FIELD(m_pIk, &s_IKSaveRestoreOp),
	DEFINE_FIELD(m_iIKCounter, FIELD_INTEGER),
	DEFINE_FIELD(m_fBoneCacheFlags, FIELD_SHORT),
	DEFINE_FIELD(m_bAlternateSorting, FIELD_BOOLEAN),
	DEFINE_EMBEDDED(m_grabController),
	// Physptrs can't be inside embedded classes
	DEFINE_PHYSPTR(m_grabController.m_controller),
END_DATADESC()

void SendProxy_Origin(const SendProp* pProp, const void* pStruct, const void* pData, DVariant* pOut, int iElement, int objectID)
{
	CEngineObjectInternal* entity = (CEngineObjectInternal*)pStruct;
	Assert(entity);

	const Vector* v;
	if (entity->entindex() == 1) {
		int aaa = 0;
	}
	if (!entity->UseStepSimulationNetworkOrigin(&v))
	{
		v = &entity->GetLocalOrigin();
	}

	pOut->m_Vector[0] = v->x;
	pOut->m_Vector[1] = v->y;
	pOut->m_Vector[2] = v->z;
}

//--------------------------------------------------------------------------------------------------------
// Used when breaking up origin, note we still have to deal with StepSimulation
//--------------------------------------------------------------------------------------------------------
void SendProxy_OriginXY(const SendProp* pProp, const void* pStruct, const void* pData, DVariant* pOut, int iElement, int objectID)
{
	CEngineObjectInternal* entity = (CEngineObjectInternal*)pStruct;
	Assert(entity);

	const Vector* v;

	if (!entity->UseStepSimulationNetworkOrigin(&v))
	{
		v = &entity->GetLocalOrigin();
	}

	pOut->m_Vector[0] = v->x;
	pOut->m_Vector[1] = v->y;
}

//--------------------------------------------------------------------------------------------------------
// Used when breaking up origin, note we still have to deal with StepSimulation
//--------------------------------------------------------------------------------------------------------
void SendProxy_OriginZ(const SendProp* pProp, const void* pStruct, const void* pData, DVariant* pOut, int iElement, int objectID)
{
	CEngineObjectInternal* entity = (CEngineObjectInternal*)pStruct;
	Assert(entity);

	const Vector* v;

	if (!entity->UseStepSimulationNetworkOrigin(&v))
	{
		v = &entity->GetLocalOrigin();
	}

	pOut->m_Float = v->z;
}

void SendProxy_Angles(const SendProp* pProp, const void* pStruct, const void* pData, DVariant* pOut, int iElement, int objectID)
{
	CEngineObjectInternal* entity = (CEngineObjectInternal*)pStruct;
	Assert(entity);

	const QAngle* a;

	if (!entity->UseStepSimulationNetworkAngles(&a))
	{
		a = &entity->GetLocalAngles();
	}

	pOut->m_Vector[0] = anglemod(a->x);
	pOut->m_Vector[1] = anglemod(a->y);
	pOut->m_Vector[2] = anglemod(a->z);
}

void SendProxy_LocalVelocity(const SendProp* pProp, const void* pStruct, const void* pData, DVariant* pOut, int iElement, int objectID)
{
	CEngineObjectInternal* entity = (CEngineObjectInternal*)pStruct;
	Assert(entity);

	const Vector* a = &entity->GetLocalVelocity();;

	pOut->m_Vector[0] = a->x;
	pOut->m_Vector[1] = a->y;
	pOut->m_Vector[2] = a->z;
}

void SendProxy_MoveParentToInt(const SendProp* pProp, const void* pStruct, const void* pData, DVariant* pOut, int iElement, int objectID)
{
	CEngineObjectInternal* entity = (CEngineObjectInternal*)pStruct;
	Assert(entity);

	CBaseEntity* pMoveParent = entity->GetMoveParent() ? entity->GetMoveParent()->GetOuter() : NULL;
	if (pMoveParent&& pMoveParent->entindex()==1) {
		int aaa = 0;
	}
	CBaseHandle pHandle = pMoveParent ? pMoveParent->GetRefEHandle() : NULL;;
	SendProxy_EHandleToInt(pProp, pStruct, &pHandle, pOut, iElement, objectID);
}

void SendProxy_CropFlagsToPlayerFlagBitsLength(const SendProp* pProp, const void* pStruct, const void* pVarData, DVariant* pOut, int iElement, int objectID)
{
	int mask = (1 << PLAYER_FLAG_BITS) - 1;
	int data = *(int*)pVarData;

	pOut->m_Int = (data & mask);
}

// This table encodes edict data.
void SendProxy_AnimTime(const SendProp* pProp, const void* pStruct, const void* pVarData, DVariant* pOut, int iElement, int objectID)
{
	CEngineObjectInternal* pEntity = (CEngineObjectInternal*)pStruct;

#if defined( _DEBUG )
	Assert(!pEntity->IsUsingClientSideAnimation());
#endif

	int ticknumber = TIME_TO_TICKS(pEntity->m_flAnimTime);
	// Tickbase is current tick rounded down to closes 100 ticks
	int tickbase = gpGlobals->GetNetworkBase(gpGlobals->tickcount, pEntity->entindex());
	int addt = 0;
	// If it's within the last tick interval through the current one, then we can encode it
	if (ticknumber >= (tickbase - 100))
	{
		addt = (ticknumber - tickbase) & 0xFF;
	}

	pOut->m_Int = addt;
}

// This table encodes edict data.
void SendProxy_SimulationTime(const SendProp* pProp, const void* pStruct, const void* pVarData, DVariant* pOut, int iElement, int objectID)
{
	CEngineObjectInternal* pEntity = (CEngineObjectInternal*)pStruct;

	int ticknumber = TIME_TO_TICKS(pEntity->m_flSimulationTime);
	// tickbase is current tick rounded down to closest 100 ticks
	int tickbase = gpGlobals->GetNetworkBase(gpGlobals->tickcount, pEntity->entindex());
	int addt = 0;
	if (ticknumber >= tickbase)
	{
		addt = (ticknumber - tickbase) & 0xff;
	}

	pOut->m_Int = addt;
}

void* SendProxy_ClientSideAnimation(const SendProp* pProp, const void* pStruct, const void* pVarData, CSendProxyRecipients* pRecipients, int objectID)
{
	CEngineObjectInternal* pEntity = (CEngineObjectInternal*)pStruct;

	if (!pEntity->IsUsingClientSideAnimation() && !pEntity->GetOuter()->IsViewModel())
		return (void*)pVarData;
	else
		return NULL;	// Don't send animtime unless the client needs it.
}
REGISTER_SEND_PROXY_NON_MODIFIED_POINTER(SendProxy_ClientSideAnimation);


void* SendProxy_ClientSideSimulation(const SendProp* pProp, const void* pStruct, const void* pVarData, CSendProxyRecipients* pRecipients, int objectID)
{
	CEngineObjectInternal* pEntity = (CEngineObjectInternal*)pStruct;

	if (!pEntity->GetOuter()->IsViewModel())
		return (void*)pVarData;
	else
		return NULL;	// Don't send animtime unless the client needs it.
}
REGISTER_SEND_PROXY_NON_MODIFIED_POINTER(SendProxy_ClientSideSimulation);

BEGIN_SEND_TABLE_NOBASE(CEngineObjectInternal, DT_AnimTimeMustBeFirst)
// NOTE:  Animtime must be sent before origin and angles ( from pev ) because it has a 
//  proxy on the client that stores off the old values before writing in the new values and
//  if it is sent after the new values, then it will only have the new origin and studio model, etc.
//  interpolation will be busted
	SendPropInt(SENDINFO(m_flAnimTime), 8, SPROP_UNSIGNED | SPROP_CHANGES_OFTEN | SPROP_ENCODED_AGAINST_TICKCOUNT, SendProxy_AnimTime),
END_SEND_TABLE()

BEGIN_SEND_TABLE_NOBASE(CEngineObjectInternal, DT_SimulationTimeMustBeFirst)
	SendPropInt(SENDINFO(m_flSimulationTime), SIMULATION_TIME_WINDOW_BITS, SPROP_UNSIGNED | SPROP_CHANGES_OFTEN | SPROP_ENCODED_AGAINST_TICKCOUNT, SendProxy_SimulationTime),
END_SEND_TABLE()

// Sendtable for fields we don't want to send to clientside animating entities
BEGIN_SEND_TABLE_NOBASE(CEngineObjectInternal, DT_ServerAnimationData)
	// ANIMATION_CYCLE_BITS is defined in shareddefs.h
	SendPropFloat(SENDINFO(m_flCycle), ANIMATION_CYCLE_BITS, SPROP_CHANGES_OFTEN | SPROP_ROUNDDOWN, 0.0f, 1.0f),
	SendPropArray3(SENDINFO_ARRAY3(m_flPoseParameter), SendPropFloat(SENDINFO_ARRAY(m_flPoseParameter), ANIMATION_POSEPARAMETER_BITS, 0, 0.0f, 1.0f)),
	SendPropFloat(SENDINFO(m_flPlaybackRate), ANIMATION_PLAYBACKRATE_BITS, SPROP_ROUNDUP, -4.0, 12.0f), // NOTE: if this isn't a power of 2 than "1.0" can't be encoded correctly
	SendPropInt(SENDINFO(m_nSequence), ANIMATION_SEQUENCE_BITS, SPROP_UNSIGNED),
	SendPropInt(SENDINFO(m_nNewSequenceParity), EF_PARITY_BITS, SPROP_UNSIGNED),
	SendPropInt(SENDINFO(m_nResetEventsParity), EF_PARITY_BITS, SPROP_UNSIGNED),
	SendPropInt(SENDINFO(m_nMuzzleFlashParity), EF_MUZZLEFLASH_BITS, SPROP_UNSIGNED),
END_SEND_TABLE()

void* SendProxy_ClientSideAnimationE(const SendProp* pProp, const void* pStruct, const void* pVarData, CSendProxyRecipients* pRecipients, int objectID)
{
	CEngineObjectInternal* pEntity = (CEngineObjectInternal*)pStruct;

	if (!pEntity->IsUsingClientSideAnimation())
		return (void*)pVarData;
	else
		return NULL;	// Don't send animtime unless the client needs it.
}
REGISTER_SEND_PROXY_NON_MODIFIED_POINTER(SendProxy_ClientSideAnimationE);

BEGIN_SEND_TABLE_NOBASE(CEngineObjectInternal, DT_EngineObject)
	SendPropDataTable("AnimTimeMustBeFirst", 0, &REFERENCE_SEND_TABLE(DT_AnimTimeMustBeFirst), SendProxy_ClientSideAnimation),
	SendPropDataTable("SimulationTimeMustBeFirst", 0, &REFERENCE_SEND_TABLE(DT_SimulationTimeMustBeFirst), SendProxy_ClientSideSimulation),
	SendPropInt(SENDINFO(testNetwork), 32, SPROP_UNSIGNED),
#if PREDICTION_ERROR_CHECK_LEVEL > 1 
	SendPropVector(SENDINFO(m_vecOrigin), -1, SPROP_NOSCALE | SPROP_CHANGES_OFTEN, 0.0f, HIGH_DEFAULT, SendProxy_Origin),
#else
	SendPropVector(SENDINFO(m_vecOrigin), -1, SPROP_COORD | SPROP_CHANGES_OFTEN, 0.0f, HIGH_DEFAULT, SendProxy_Origin),
#endif
#if PREDICTION_ERROR_CHECK_LEVEL > 1 
	SendPropVector(SENDINFO(m_angRotation), -1, SPROP_NOSCALE | SPROP_CHANGES_OFTEN, 0, HIGH_DEFAULT, SendProxy_Angles),
#else
	SendPropQAngles(SENDINFO(m_angRotation), 13, SPROP_CHANGES_OFTEN, SendProxy_Angles),
#endif
	SendPropVector(SENDINFO(m_vecVelocity), 0, SPROP_NOSCALE, 0.0f, HIGH_DEFAULT, SendProxy_LocalVelocity),
	SendPropEHandle(SENDINFO_NAME(m_hMoveParent, moveparent), 0, SendProxy_MoveParentToInt),
	SendPropInt(SENDINFO(m_iParentAttachment), NUM_PARENTATTACHMENT_BITS, SPROP_UNSIGNED),
	SendPropEHandle(SENDINFO(m_hGroundEntity), SPROP_CHANGES_OFTEN),
	SendPropModelIndex(SENDINFO(m_nModelIndex)),
	SendPropDataTable(SENDINFO_DT(m_Collision), &REFERENCE_SEND_TABLE(DT_CollisionProperty)),
	SendPropInt(SENDINFO(m_CollisionGroup), 5, SPROP_UNSIGNED),
	SendPropInt(SENDINFO(m_fFlags), PLAYER_FLAG_BITS, SPROP_UNSIGNED | SPROP_CHANGES_OFTEN, SendProxy_CropFlagsToPlayerFlagBitsLength),
	SendPropInt(SENDINFO(m_fEffects), EF_MAX_BITS, SPROP_UNSIGNED),
	SendPropFloat(SENDINFO(m_flFriction), 8, SPROP_ROUNDDOWN, 0.0f, 4.0f),
	SendPropFloat(SENDINFO(m_flElasticity), 0, SPROP_COORD),
	SendPropInt(SENDINFO(m_nNextThinkTick)),
	SendPropInt(SENDINFO_NAME(m_MoveType, movetype), MOVETYPE_MAX_BITS, SPROP_UNSIGNED),
	SendPropInt(SENDINFO_NAME(m_MoveCollide, movecollide), MOVECOLLIDE_MAX_BITS, SPROP_UNSIGNED),
	SendPropInt(SENDINFO(m_bSimulatedEveryTick), 1, SPROP_UNSIGNED),
	SendPropInt(SENDINFO(m_bAnimatedEveryTick), 1, SPROP_UNSIGNED),
	SendPropInt(SENDINFO(m_bClientSideAnimation), 1, SPROP_UNSIGNED),
	SendPropInt(SENDINFO(m_nForceBone), 8, 0),
	SendPropVector(SENDINFO(m_vecForce), -1, SPROP_NOSCALE),
	SendPropInt(SENDINFO(m_nSkin), ANIMATION_SKIN_BITS),
	SendPropInt(SENDINFO(m_nBody), ANIMATION_BODY_BITS),

	SendPropInt(SENDINFO(m_nHitboxSet), ANIMATION_HITBOXSET_BITS, SPROP_UNSIGNED),

	SendPropFloat(SENDINFO(m_flModelScale)),
	SendPropArray3(SENDINFO_ARRAY3(m_flEncodedController), SendPropFloat(SENDINFO_ARRAY(m_flEncodedController), 11, SPROP_ROUNDDOWN, 0.0f, 1.0f)),
	SendPropInt(SENDINFO(m_bClientSideFrameReset), 1, SPROP_UNSIGNED),
	SendPropDataTable("serveranimdata", 0, &REFERENCE_SEND_TABLE(DT_ServerAnimationData), SendProxy_ClientSideAnimationE),
	SendPropInt(SENDINFO_NAME(m_ragdoll.listCount, m_ragdollListCount), 32, SPROP_UNSIGNED),
	SendPropArray(SendPropQAngles(SENDINFO_ARRAY(m_ragAngles), 13, 0), m_ragAngles),
	SendPropArray(SendPropVector(SENDINFO_ARRAY(m_ragPos), -1, SPROP_COORD), m_ragPos),
	SendPropInt(SENDINFO(m_nRenderFX), 8, SPROP_UNSIGNED),
	SendPropInt(SENDINFO(m_nOverlaySequence), 11),
	SendPropBool(SENDINFO(m_bAlternateSorting)),
	SendPropInt(SENDINFO(m_ubInterpolationFrame), NOINTERP_PARITY_MAX_BITS, SPROP_UNSIGNED),

END_SEND_TABLE()

IMPLEMENT_SERVERCLASS(CEngineObjectInternal, DT_EngineObject)

#include "tier0/memdbgoff.h"
//-----------------------------------------------------------------------------
// CBaseEntity new/delete
// allocates and frees memory for itself from the engine->
// All fields in the object are all initialized to 0.
//-----------------------------------------------------------------------------
void* CEngineObjectInternal::operator new(size_t stAllocateBlock)
{
	// call into engine to get memory
	Assert(stAllocateBlock != 0);
	return engine->PvAllocEntPrivateData(stAllocateBlock);
};

void* CEngineObjectInternal::operator new(size_t stAllocateBlock, int nBlockUse, const char* pFileName, int nLine)
{
	// call into engine to get memory
	Assert(stAllocateBlock != 0);
	return engine->PvAllocEntPrivateData(stAllocateBlock);
}

void CEngineObjectInternal::operator delete(void* pMem)
{
	// get the engine to free the memory
	engine->FreeEntPrivateData(pMem);
}

#include "tier0/memdbgon.h"

FORCEINLINE bool NamesMatch(const char* pszQuery, string_t nameToMatch)
{
	if (nameToMatch == NULL_STRING)
		return (!pszQuery || *pszQuery == 0 || *pszQuery == '*');

	const char* pszNameToMatch = STRING(nameToMatch);

	// If the pointers are identical, we're identical
	if (pszNameToMatch == pszQuery)
		return true;

	while (*pszNameToMatch && *pszQuery)
	{
		unsigned char cName = *pszNameToMatch;
		unsigned char cQuery = *pszQuery;
		// simple ascii case conversion
		if (cName == cQuery)
			;
		else if (cName - 'A' <= (unsigned char)'Z' - 'A' && cName - 'A' + 'a' == cQuery)
			;
		else if (cName - 'a' <= (unsigned char)'z' - 'a' && cName - 'a' + 'A' == cQuery)
			;
		else
			break;
		++pszNameToMatch;
		++pszQuery;
	}

	if (*pszQuery == 0 && *pszNameToMatch == 0)
		return true;

	// @TODO (toml 03-18-03): Perhaps support real wildcards. Right now, only thing supported is trailing *
	if (*pszQuery == '*')
		return true;

	return false;
}

bool CEngineObjectInternal::NameMatchesComplex(const char* pszNameOrWildcard)
{
	if (!Q_stricmp("!player", pszNameOrWildcard))
		return m_pOuter->IsPlayer();

	return NamesMatch(pszNameOrWildcard, m_iName);
}

bool CEngineObjectInternal::ClassMatchesComplex(const char* pszClassOrWildcard)
{
	return NamesMatch(pszClassOrWildcard, m_iClassname);
}

inline bool CEngineObjectInternal::NameMatches(const char* pszNameOrWildcard)
{
	if (IDENT_STRINGS(m_iName, pszNameOrWildcard))
		return true;
	return NameMatchesComplex(pszNameOrWildcard);
}

inline bool CEngineObjectInternal::NameMatches(string_t nameStr)
{
	if (IDENT_STRINGS(m_iName, nameStr))
		return true;
	return NameMatchesComplex(STRING(nameStr));
}

inline bool CEngineObjectInternal::ClassMatches(const char* pszClassOrWildcard)
{
	if (IDENT_STRINGS(m_iClassname, pszClassOrWildcard))
		return true;
	return ClassMatchesComplex(pszClassOrWildcard);
}

inline bool CEngineObjectInternal::ClassMatches(string_t nameStr)
{
	if (IDENT_STRINGS(m_iClassname, nameStr))
		return true;
	return ClassMatchesComplex(STRING(nameStr));
}

//-----------------------------------------------------------------------------
// Purpose: Verifies that this entity's data description is valid in debug builds.
//-----------------------------------------------------------------------------
#ifdef _DEBUG
typedef CUtlVector< const char* >	KeyValueNameList_t;

static void AddDataMapFieldNamesToList(KeyValueNameList_t& list, datamap_t* pDataMap)
{
	while (pDataMap != NULL)
	{
		for (int i = 0; i < pDataMap->dataNumFields; i++)
		{
			typedescription_t* pField = &pDataMap->dataDesc[i];

			if (pField->fieldType == FIELD_EMBEDDED)
			{
				AddDataMapFieldNamesToList(list, pField->td);
				continue;
			}

			if (pField->flags & FTYPEDESC_KEY)
			{
				list.AddToTail(pField->externalName);
			}
		}

		pDataMap = pDataMap->baseMap;
	}
}

void CEngineObjectInternal::ValidateDataDescription(void)
{
	// Multiple key fields that have the same name are not allowed - it creates an
	// ambiguity when trying to parse keyvalues and outputs.
	datamap_t* pDataMap = GetDataDescMap();
	if ((pDataMap == NULL) || pDataMap->bValidityChecked)
		return;

	pDataMap->bValidityChecked = true;

	// Let's generate a list of all keyvalue strings in the entire hierarchy...
	KeyValueNameList_t	names(128);
	AddDataMapFieldNamesToList(names, pDataMap);

	for (int i = names.Count(); --i > 0; )
	{
		for (int j = i - 1; --j >= 0; )
		{
			if (!Q_stricmp(names[i], names[j]))
			{
				DevMsg("%s has multiple data description entries for \"%s\"\n", STRING(m_iClassname), names[i]);
				break;
			}
		}
	}
}
#endif // _DEBUG



//-----------------------------------------------------------------------------
// Purpose: Handles keys and outputs from the BSP.
// Input  : mapData - Text block of keys and values from the BSP.
//-----------------------------------------------------------------------------
void CEngineObjectInternal::ParseMapData(IEntityMapData* mapData)
{
	char keyName[MAPKEY_MAXLENGTH];
	char value[MAPKEY_MAXLENGTH];

#ifdef _DEBUG
	ValidateDataDescription();
#endif // _DEBUG

	// loop through all keys in the data block and pass the info back into the object
	if (mapData->GetFirstKey(keyName, value))
	{
		do
		{
			if (m_pOuter->KeyValue(keyName, value)) 
			{
				if (KeyValue(keyName, value)) {
					Msg("Entity %s has multiparsed key: %s!\n", GetClassname(), keyName);
				}
			}
			else 
			{
				if (!KeyValue(keyName, value)) {
					Msg("Entity %s has unparsed key: %s!\n", GetClassname(), keyName);
				}
			}
		} while (mapData->GetNextKey(keyName, value));
	}
}

//-----------------------------------------------------------------------------
// Parse data from a map file
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::KeyValue(const char* szKeyName, const char* szValue)
{
	//!! temp hack, until worldcraft is fixed
	// strip the # tokens from (duplicate) key names
	char* s = (char*)strchr(szKeyName, '#');
	if (s)
	{
		*s = '\0';
	}

	//if (FStrEq(szKeyName, "rendercolor") || FStrEq(szKeyName, "rendercolor32"))
	//{
	//	color32 tmp;
	//	UTIL_StringToColor32(&tmp, szValue);
	//	SetRenderColor(tmp.r, tmp.g, tmp.b);
	//	// don't copy alpha, legacy support uses renderamt
	//	return true;
	//}

	//if (FStrEq(szKeyName, "renderamt"))
	//{
	//	SetRenderColorA(atoi(szValue));
	//	return true;
	//}

	//if (FStrEq(szKeyName, "disableshadows"))
	//{
	//	int val = atoi(szValue);
	//	if (val)
	//	{
	//		AddEffects(EF_NOSHADOW);
	//	}
	//	return true;
	//}

	//if (FStrEq(szKeyName, "mins"))
	//{
	//	Vector mins;
	//	UTIL_StringToVector(mins.Base(), szValue);
	//	m_Collision.SetCollisionBounds(mins, OBBMaxs());
	//	return true;
	//}

	//if (FStrEq(szKeyName, "maxs"))
	//{
	//	Vector maxs;
	//	UTIL_StringToVector(maxs.Base(), szValue);
	//	m_Collision.SetCollisionBounds(OBBMins(), maxs);
	//	return true;
	//}

	//if (FStrEq(szKeyName, "disablereceiveshadows"))
	//{
	//	int val = atoi(szValue);
	//	if (val)
	//	{
	//		AddEffects(EF_NORECEIVESHADOW);
	//	}
	//	return true;
	//}

	//if (FStrEq(szKeyName, "nodamageforces"))
	//{
	//	int val = atoi(szValue);
	//	if (val)
	//	{
	//		AddEFlags(EFL_NO_DAMAGE_FORCES);
	//	}
	//	return true;
	//}

	// Fix up single angles
	if (FStrEq(szKeyName, "angle"))
	{
		static char szBuf[64];

		float y = atof(szValue);
		if (y >= 0)
		{
			Q_snprintf(szBuf, sizeof(szBuf), "%f %f %f", GetLocalAngles()[0], y, GetLocalAngles()[2]);
		}
		else if ((int)y == -1)
		{
			Q_strncpy(szBuf, "-90 0 0", sizeof(szBuf));
		}
		else
		{
			Q_strncpy(szBuf, "90 0 0", sizeof(szBuf));
		}

		// Do this so inherited classes looking for 'angles' don't have to bother with 'angle'
		return KeyValue("angles", szBuf);
	}

	// NOTE: Have to do these separate because they set two values instead of one
	if (FStrEq(szKeyName, "angles"))
	{
		QAngle angles;
		UTIL_StringToVector(angles.Base(), szValue);

		// If you're hitting this assert, it's probably because you're
		// calling SetLocalAngles from within a KeyValues method.. use SetAbsAngles instead!
		Assert((GetMoveParent() == NULL) && !IsEFlagSet(EFL_DIRTY_ABSTRANSFORM));
		SetAbsAngles(angles);
		return true;
	}

	if (FStrEq(szKeyName, "origin"))
	{
		Vector vecOrigin;
		UTIL_StringToVector(vecOrigin.Base(), szValue);

		// If you're hitting this assert, it's probably because you're
		// calling SetLocalOrigin from within a KeyValues method.. use SetAbsOrigin instead!
		Assert((GetMoveParent() == NULL) && !IsEFlagSet(EFL_DIRTY_ABSTRANSFORM));
		SetAbsOrigin(vecOrigin);
		return true;
	}

	if (FStrEq(szKeyName, "targetname"))
	{
		m_iName = AllocPooledString(szValue);
		return true;
	}

	// loop through the data description, and try and place the keys in
	if (!*ent_debugkeys.GetString())
	{
		for (datamap_t* dmap = GetDataDescMap(); dmap != NULL; dmap = dmap->baseMap)
		{
			if (dmap->ParseKeyvalue(this, szKeyName, szValue, &AllocPooledString))
				return true;
		}
	}
	else
	{
		// debug version - can be used to see what keys have been parsed in
		bool printKeyHits = false;
		const char* debugName = "";

		if (*ent_debugkeys.GetString() && !Q_stricmp(ent_debugkeys.GetString(), STRING(m_iClassname)))
		{
			// Msg( "-- found entity of type %s\n", STRING(m_iClassname) );
			printKeyHits = true;
			debugName = STRING(m_iClassname);
		}

		// loop through the data description, and try and place the keys in
		for (datamap_t* dmap = GetDataDescMap(); dmap != NULL; dmap = dmap->baseMap)
		{
			if (!printKeyHits && *ent_debugkeys.GetString() && !Q_stricmp(dmap->dataClassName, ent_debugkeys.GetString()))
			{
				// Msg( "-- found class of type %s\n", dmap->dataClassName );
				printKeyHits = true;
				debugName = dmap->dataClassName;
			}

			if (dmap->ParseKeyvalue(this, szKeyName, szValue, &AllocPooledString))
			{
				if (printKeyHits)
					Msg("(%s) key: %-16s value: %s\n", debugName, szKeyName, szValue);

				return true;
			}
		}

		if (printKeyHits)
			Msg("!! (%s) key not handled: \"%s\" \"%s\"\n", STRING(m_iClassname), szKeyName, szValue);
	}

	// key hasn't been handled
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Saves the current object out to disk, by iterating through the objects
//			data description hierarchy
// Input  : &save - save buffer which the class data is written to
// Output : int	- 0 if the save failed, 1 on success
//-----------------------------------------------------------------------------
int CEngineObjectInternal::Save(ISave& save)
{
	// loop through the data description list, saving each data desc block
	int status = save.WriteEntity(this->m_pOuter);

	return status;
}

//-----------------------------------------------------------------------------
// Purpose: Recursively saves all the classes in an object, in reverse order (top down)
// Output : int 0 on failure, 1 on success
//-----------------------------------------------------------------------------
//int CBaseEntity::SaveDataDescBlock( ISave &save, datamap_t *dmap )
//{
//	return save.WriteAll( this, dmap );
//}

//-----------------------------------------------------------------------------
// Purpose: Restores the current object from disk, by iterating through the objects
//			data description hierarchy
// Input  : &restore - restore buffer which the class data is read from
// Output : int	- 0 if the restore failed, 1 on success
//-----------------------------------------------------------------------------
int CEngineObjectInternal::Restore(IRestore& restore)
{
	// This is essential to getting the spatial partition info correct
	DestroyPartitionHandle();
	
	// loops through the data description list, restoring each data desc block in order
	int status = restore.ReadEntity(this->m_pOuter);;

	// ---------------------------------------------------------------
	// HACKHACK: We don't know the space of these vectors until now
	// if they are worldspace, fix them up.
	// ---------------------------------------------------------------
	{
		CGameSaveRestoreInfo* pGameInfo = restore.GetGameSaveRestoreInfo();
		Vector parentSpaceOffset = pGameInfo->modelSpaceOffset;
		if (!GetMoveParent())
		{
			// parent is the world, so parent space is worldspace
			// so update with the worldspace leveltransition transform
			parentSpaceOffset += pGameInfo->GetLandmark();
		}

		// NOTE: Do *not* use GetAbsOrigin() here because it will
		// try to recompute m_rgflCoordinateFrame!
		//MatrixSetColumn(GetEngineObject()->m_vecAbsOrigin, 3, GetEngineObject()->m_rgflCoordinateFrame );
		ResetRgflCoordinateFrame();

		m_vecOrigin += parentSpaceOffset;
	}

	// Gotta do this after the coordframe is set up as it depends on it.

	// By definition, the surrounding bounds are dirty
	// Also, twiddling with the flags here ensures it gets added to the KD tree dirty list
	// (We don't want to use the saved version of this flag)
	RemoveEFlags(EFL_DIRTY_SPATIAL_PARTITION);
	MarkSurroundingBoundsDirty();
	
	if (m_pOuter->IsNetworkable() && entindex() != -1 && GetModelIndex() != 0 && GetModelName() != NULL_STRING && restore.GetPrecacheMode())
	{
		engine->PrecacheModel(STRING(GetModelName()));

		//Adrian: We should only need to do this after we precache. No point in setting the model again.
		SetModelIndex(modelinfo->GetModelIndex(STRING(GetModelName())));
	}

	// Restablish ground entity
	if (GetGroundEntity() != NULL)
	{
		GetGroundEntity()->AddEntityToGroundList(this);
	}
	if (GetModelScale() <= 0.0f)
		SetModelScale(1.0f);
	LockStudioHdr();
	return status;
}


//-----------------------------------------------------------------------------
// handler to do stuff before you are saved
//-----------------------------------------------------------------------------
void CEngineObjectInternal::OnSave(IEntitySaveUtils* pUtils)
{
	// Here, we must force recomputation of all abs data so it gets saved correctly
	// We can't leave the dirty bits set because the loader can't cope with it.
	CalcAbsolutePosition();
	CalcAbsoluteVelocity();
	if (m_ragdoll.listCount) {
		// Don't save ragdoll element 0, base class saves the pointer in 
		// m_pPhysicsObject
		Assert(m_ragdoll.list[0].parentIndex == -1);
		Assert(m_ragdoll.list[0].pConstraint == NULL);
		Assert(m_ragdoll.list[0].originParentSpace == vec3_origin);
		Assert(m_ragdoll.list[0].pObject != NULL);
		VPhysicsSetObject(NULL);	// squelch a warning message
		VPhysicsSetObject(m_ragdoll.list[0].pObject);	// make sure object zero is saved by CBaseEntity
	}
	m_pOuter->OnSave(pUtils);
}

//-----------------------------------------------------------------------------
// handler to do stuff after you are restored
//-----------------------------------------------------------------------------
void CEngineObjectInternal::OnRestore()
{
	SimThink_EntityChanged(this->m_pOuter);

	// touchlinks get recomputed
	if (IsEFlagSet(EFL_CHECK_UNTOUCH))
	{
		RemoveEFlags(EFL_CHECK_UNTOUCH);
		SetCheckUntouch(true);
	}

	if (GetMoveParent())
	{
		CEngineObjectInternal* pChild = GetMoveParent()->FirstMoveChild();
		while (pChild)
		{
			if (pChild == this)
				break;
			pChild = pChild->NextMovePeer();
		}
		if (pChild != this)
		{
#if _DEBUG
			// generally this means you've got something marked FCAP_DONT_SAVE
			// in a hierarchy.  That's probably ok given this fixup, but the hierarhcy
			// linked list is just saved/loaded in-place
			Warning("Fixing up parent on %s\n", GetClassname());
#endif
			// We only need to be back in the parent's list because we're already in the right place and with the right data
			GetMoveParent()->LinkChild(this);
			this->m_pOuter->AfterParentChanged(NULL);
		}
	}

	// We're not save/loading the PVS dirty state. Assume everything is dirty after a restore
	MarkPVSInformationDirty();
	NetworkStateChanged();
	if (m_ragdoll.listCount) {
		// rebuild element 0 since it isn't saved
		// NOTE: This breaks the rules - the pointer needs to get fixed in Restore()
		m_ragdoll.list[0].pObject = VPhysicsGetObject();
		m_ragdoll.list[0].parentIndex = -1;
		m_ragdoll.list[0].originParentSpace.Init();
		// JAY: Reset collision relationships
		RagdollSetupCollisions(m_ragdoll, modelinfo->GetVCollide(GetModelIndex()), GetModelIndex());
	}
	m_flEstIkFloor = GetLocalOrigin().z;
	m_grabController.OnRestore();
	m_pOuter->OnRestore();
	m_pOuter->NetworkStateChanged();
}

//-----------------------------------------------------------------------------
// PVS rules
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::IsInPVS(const CBaseEntity* pRecipient, const void* pvs, int pvssize)
{
	RecomputePVSInformation();

	// ignore if not touching a PV leaf
	// negative leaf count is a node number
	// If no pvs, add any entity

	Assert(pvs && (GetOuter() != pRecipient));

	unsigned char* pPVS = (unsigned char*)pvs;

	if (m_PVSInfo.m_nClusterCount < 0)   // too many clusters, use headnode
	{
		return (engine->CheckHeadnodeVisible(m_PVSInfo.m_nHeadNode, pPVS, pvssize) != 0);
	}

	for (int i = m_PVSInfo.m_nClusterCount; --i >= 0; )
	{
		if (pPVS[m_PVSInfo.m_pClusters[i] >> 3] & (1 << (m_PVSInfo.m_pClusters[i] & 7)))
			return true;
	}

	return false;		// not visible
}


//-----------------------------------------------------------------------------
// PVS: this function is called a lot, so it avoids function calls
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::IsInPVS(const CCheckTransmitInfo* pInfo)
{
	// PVS data must be up to date
	//Assert( !m_pPev || ( ( m_pPev->m_fStateFlags & FL_EDICT_DIRTY_PVS_INFORMATION ) == 0 ) );

	int i;

	// Early out if the areas are connected
	if (!m_PVSInfo.m_nAreaNum2)
	{
		for (i = 0; i < pInfo->m_AreasNetworked; i++)
		{
			int clientArea = pInfo->m_Areas[i];
			if (clientArea == m_PVSInfo.m_nAreaNum || engine->CheckAreasConnected(clientArea, m_PVSInfo.m_nAreaNum))
				break;
		}
	}
	else
	{
		// doors can legally straddle two areas, so
		// we may need to check another one
		for (i = 0; i < pInfo->m_AreasNetworked; i++)
		{
			int clientArea = pInfo->m_Areas[i];
			if (clientArea == m_PVSInfo.m_nAreaNum || clientArea == m_PVSInfo.m_nAreaNum2)
				break;

			if (engine->CheckAreasConnected(clientArea, m_PVSInfo.m_nAreaNum))
				break;

			if (engine->CheckAreasConnected(clientArea, m_PVSInfo.m_nAreaNum2))
				break;
		}
	}

	if (i == pInfo->m_AreasNetworked)
	{
		// areas not connected
		return false;
	}

	// ignore if not touching a PV leaf
	// negative leaf count is a node number
	// If no pvs, add any entity

	Assert(entindex() != pInfo->m_pClientEnt);

	unsigned char* pPVS = (unsigned char*)pInfo->m_PVS;

	if (m_PVSInfo.m_nClusterCount < 0)   // too many clusters, use headnode
	{
		return (engine->CheckHeadnodeVisible(m_PVSInfo.m_nHeadNode, pPVS, pInfo->m_nPVSSize) != 0);
	}

	for (i = m_PVSInfo.m_nClusterCount; --i >= 0; )
	{
		int nCluster = m_PVSInfo.m_pClusters[i];
		if (((int)(pPVS[nCluster >> 3])) & BitVec_BitInByte(nCluster))
			return true;
	}

	return false;		// not visible

}

//-----------------------------------------------------------------------------
// PVS information
//-----------------------------------------------------------------------------
void CEngineObjectInternal::RecomputePVSInformation()
{
	if (m_bPVSInfoDirty/*((GetTransmitState() & FL_EDICT_DIRTY_PVS_INFORMATION) != 0)*/)// m_entindex!=-1 && 
	{
		//GetTransmitState() &= ~FL_EDICT_DIRTY_PVS_INFORMATION;
		m_bPVSInfoDirty = false;
		engine->BuildEntityClusterList(m_pOuter, &m_PVSInfo);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Sets the movement parent of this entity. This entity will be moved
//			to a local coordinate calculated from its current absolute offset
//			from the parent entity and will then follow the parent entity.
// Input  : pParentEntity - This entity's new parent in the movement hierarchy.
//-----------------------------------------------------------------------------
void CEngineObjectInternal::SetParent(IEngineObjectServer* pParentEntity, int iAttachment)
{
	if (pParentEntity == this)
	{
		// should never set parent to 'this' - makes no sense
		Assert(0);
		pParentEntity = NULL;
	}
	// If they didn't specify an attachment, use our current
	if (iAttachment == -1)
	{
		iAttachment = m_iParentAttachment;
	}

	//bool bWasNotParented = (GetMoveParent() == NULL);
	CEngineObjectInternal* pOldParent = GetMoveParent();
	unsigned char iOldParentAttachment = m_iParentAttachment;

	this->m_pOuter->BeforeParentChanged(pParentEntity ? pParentEntity->GetOuter() : NULL, iAttachment);
	// notify the old parent of the loss
	this->UnlinkFromParent();
	m_iParent = NULL_STRING;
	m_iParentAttachment = 0;

	if (pParentEntity == NULL)
	{
		this->m_pOuter->AfterParentChanged(pOldParent ? pOldParent->m_pOuter : NULL, iOldParentAttachment);
		return;
	}

	RemoveSolidFlags(FSOLID_ROOT_PARENT_ALIGNED);
	if (const_cast<IEngineObjectServer*>(pParentEntity)->GetRootMoveParent()->GetSolid() == SOLID_BSP)
	{
		AddSolidFlags(FSOLID_ROOT_PARENT_ALIGNED);
	}
	else
	{
		if (GetSolid() == SOLID_BSP)
		{
			// Must be SOLID_VPHYSICS because parent might rotate
			SetSolid(SOLID_VPHYSICS);
		}
	}

	// set the move parent if we have one

	// add ourselves to the list
	pParentEntity->LinkChild(this);
	// set the new name
//m_pParent = pParentEntity;
	m_iParent = pParentEntity->GetEntityName();
	m_iParentAttachment = (char)iAttachment;
	this->m_pOuter->AfterParentChanged(pOldParent ? pOldParent->m_pOuter : NULL, iOldParentAttachment);

	EntityMatrix matrix, childMatrix;
	matrix.InitFromEntity(pParentEntity->GetOuter(), m_iParentAttachment); // parent->world
	childMatrix.InitFromEntityLocal(this->m_pOuter); // child->world
	Vector localOrigin = matrix.WorldToLocal(this->GetLocalOrigin());

	// I have the axes of local space in world space. (childMatrix)
	// I want to compute those world space axes in the parent's local space
	// and set that transform (as angles) on the child's object so the net
	// result is that the child is now in parent space, but still oriented the same way
	VMatrix tmp = matrix.Transpose(); // world->parent
	tmp.MatrixMul(childMatrix, matrix); // child->parent
	QAngle angles;
	MatrixToAngles(matrix, angles);
	this->SetLocalAngles(angles);
	//UTIL_SetOrigin(this->m_pOuter, localOrigin);
	this->SetLocalOrigin(localOrigin);

	if (VPhysicsGetObject())
	{
		if (VPhysicsGetObject()->IsStatic())
		{
			if (VPhysicsGetObject()->IsAttachedToConstraint(false))
			{
				Warning("SetParent on static object, all constraints attached to %s (%s)will now be broken!\n", m_pOuter->GetDebugName(), m_pOuter->GetClassname());
			}
			VPhysicsDestroyObject();
			VPhysicsInitShadow(false, false);
		}
	}
	CollisionRulesChanged();
}


//-----------------------------------------------------------------------------
// Purpose: Does the linked list work of removing a child object from the hierarchy.
// Input  : pParent - 
//			pChild - 
//-----------------------------------------------------------------------------
void CEngineObjectInternal::UnlinkChild(IEngineObjectServer* pChild)
{
	CEngineObjectInternal* pList = this->FirstMoveChild();
	CEngineObjectInternal* pPrev = NULL;

	while (pList)
	{
		CEngineObjectInternal* pNext = pList->NextMovePeer();
		if (pList == pChild)
		{
			// patch up the list
			if (!pPrev) {
				this->SetFirstMoveChild(pNext);
			}
			else {
				pPrev->SetNextMovePeer(pNext);
			}

			// Clear hierarchy bits for this guy
			((CEngineObjectInternal*)pChild)->SetMoveParent(NULL);
			((CEngineObjectInternal*)pChild)->SetNextMovePeer(NULL);
			//pList->GetOuter()->NetworkProp()->SetNetworkParent( CBaseHandle() );
			pChild->GetOuter()->DispatchUpdateTransmitState();
			pChild->GetOuter()->OnEntityEvent(ENTITY_EVENT_PARENT_CHANGED, NULL);

			this->RecalcHasPlayerChildBit();
			return;
		}
		else
		{
			pPrev = pList;
			pList = pNext;
		}
	}

	// This only happens if the child wasn't found in the parent's child list
	Assert(0);
}

void CEngineObjectInternal::LinkChild(IEngineObjectServer* pChild)
{
	//EHANDLE hParent;
	//hParent.Set( pParent->GetOuter() );
	((CEngineObjectInternal*)pChild)->SetNextMovePeer(this->FirstMoveChild());
	this->SetFirstMoveChild(pChild);
	((CEngineObjectInternal*)pChild)->SetMoveParent(this);
	//pChild->GetOuter()->NetworkProp()->SetNetworkParent(pParent->GetOuter());
	pChild->GetOuter()->DispatchUpdateTransmitState();
	pChild->GetOuter()->OnEntityEvent(ENTITY_EVENT_PARENT_CHANGED, NULL);
	this->RecalcHasPlayerChildBit();
}

void CEngineObjectInternal::TransferChildren(IEngineObjectServer* pNewParent)
{
	CEngineObjectInternal* pChild = this->FirstMoveChild();
	while (pChild)
	{
		// NOTE: Have to do this before the unlink to ensure local coords are valid
		Vector vecAbsOrigin = pChild->GetAbsOrigin();
		QAngle angAbsRotation = pChild->GetAbsAngles();
		Vector vecAbsVelocity = pChild->GetAbsVelocity();
		//		QAngle vecAbsAngVelocity = pChild->GetAbsAngularVelocity();
		pChild->GetOuter()->BeforeParentChanged(pNewParent->GetOuter());
		UnlinkChild(pChild);
		pNewParent->LinkChild(pChild);
		pChild->GetOuter()->AfterParentChanged(this->GetOuter());

		// FIXME: This is a hack to guarantee update of the local origin, angles, etc.
		pChild->m_vecAbsOrigin.Init(FLT_MAX, FLT_MAX, FLT_MAX);
		pChild->m_angAbsRotation.Init(FLT_MAX, FLT_MAX, FLT_MAX);
		pChild->m_vecAbsVelocity.Init(FLT_MAX, FLT_MAX, FLT_MAX);

		pChild->SetAbsOrigin(vecAbsOrigin);
		pChild->SetAbsAngles(angAbsRotation);
		pChild->SetAbsVelocity(vecAbsVelocity);
		//		pChild->SetAbsAngularVelocity(vecAbsAngVelocity);

		pChild = this->FirstMoveChild();
	}
}

void CEngineObjectInternal::UnlinkFromParent()
{
	if (this->GetMoveParent())
	{
		// NOTE: Have to do this before the unlink to ensure local coords are valid
		Vector vecAbsOrigin = this->GetAbsOrigin();
		QAngle angAbsRotation = this->GetAbsAngles();
		Vector vecAbsVelocity = this->GetAbsVelocity();
		//		QAngle vecAbsAngVelocity = pRemove->GetAbsAngularVelocity();

		this->GetMoveParent()->UnlinkChild(this);

		this->SetLocalOrigin(vecAbsOrigin);
		this->SetLocalAngles(angAbsRotation);
		this->SetLocalVelocity(vecAbsVelocity);
		//		pRemove->SetLocalAngularVelocity(vecAbsAngVelocity);
		this->GetOuter()->UpdateWaterState();
	}
}


//-----------------------------------------------------------------------------
// Purpose: Clears the parent of all the children of the given object.
//-----------------------------------------------------------------------------
void CEngineObjectInternal::UnlinkAllChildren()
{
	CEngineObjectInternal* pChild = this->FirstMoveChild();
	while (pChild)
	{
		CEngineObjectInternal* pNext = pChild->NextMovePeer();
		pChild->UnlinkFromParent();
		pChild = pNext;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Calculates the absolute position of an edict in the world
//			assumes the parent's absolute origin has already been calculated
//-----------------------------------------------------------------------------
void CEngineObjectInternal::CalcAbsolutePosition(void)
{
	if (!IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
		return;

	RemoveEFlags(EFL_DIRTY_ABSTRANSFORM);

	// Plop the entity->parent matrix into m_rgflCoordinateFrame
	AngleMatrix(m_angRotation, m_vecOrigin, m_rgflCoordinateFrame);

	CEngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		// no move parent, so just copy existing values
		m_vecAbsOrigin = m_vecOrigin;
		m_angAbsRotation = m_angRotation;
		if (HasDataObjectType(POSITIONWATCHER))
		{
			//ReportPositionChanged(this->m_pOuter);
			this->m_pOuter->NotifyPositionChanged();
		}
		return;
	}

	// concatenate with our parent's transform
	matrix3x4_t tmpMatrix, scratchSpace;
	ConcatTransforms(GetParentToWorldTransform(scratchSpace), m_rgflCoordinateFrame, tmpMatrix);
	MatrixCopy(tmpMatrix, m_rgflCoordinateFrame);

	// pull our absolute position out of the matrix
	MatrixGetColumn(m_rgflCoordinateFrame, 3, m_vecAbsOrigin);

	// if we have any angles, we have to extract our absolute angles from our matrix
	if ((m_angRotation == vec3_angle) && (m_iParentAttachment == 0))
	{
		// just copy our parent's absolute angles
		VectorCopy(pMoveParent->GetAbsAngles(), m_angAbsRotation);
	}
	else
	{
		MatrixAngles(m_rgflCoordinateFrame, m_angAbsRotation);
	}
	if (HasDataObjectType(POSITIONWATCHER))
	{
		//ReportPositionChanged(this->m_pOuter);
		this->m_pOuter->NotifyPositionChanged();
	}
}

void CEngineObjectInternal::CalcAbsoluteVelocity()
{
	if (!IsEFlagSet(EFL_DIRTY_ABSVELOCITY))
		return;

	RemoveEFlags(EFL_DIRTY_ABSVELOCITY);

	CEngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		m_vecAbsVelocity = m_vecVelocity;
		return;
	}

	// This transforms the local velocity into world space
	VectorRotate(m_vecVelocity, pMoveParent->EntityToWorldTransform(), m_vecAbsVelocity);

	// Now add in the parent abs velocity
	m_vecAbsVelocity += pMoveParent->GetAbsVelocity();
}

void CEngineObjectInternal::ComputeAbsPosition(const Vector& vecLocalPosition, Vector* pAbsPosition)
{
	CEngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		*pAbsPosition = vecLocalPosition;
	}
	else
	{
		VectorTransform(vecLocalPosition, pMoveParent->EntityToWorldTransform(), *pAbsPosition);
	}
}


//-----------------------------------------------------------------------------
// Computes the abs position of a point specified in local space
//-----------------------------------------------------------------------------
void CEngineObjectInternal::ComputeAbsDirection(const Vector& vecLocalDirection, Vector* pAbsDirection)
{
	CEngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		*pAbsDirection = vecLocalDirection;
	}
	else
	{
		VectorRotate(vecLocalDirection, pMoveParent->EntityToWorldTransform(), *pAbsDirection);
	}
}

void CEngineObjectInternal::GetVectors(Vector* forward, Vector* right, Vector* up) const {
	m_pOuter->GetVectors(forward, right, up);
}

const matrix3x4_t& CEngineObjectInternal::GetParentToWorldTransform(matrix3x4_t& tempMatrix)
{
	CEngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		Assert(false);
		SetIdentityMatrix(tempMatrix);
		return tempMatrix;
	}

	if (m_iParentAttachment != 0)
	{
		MDLCACHE_CRITICAL_SECTION();

		CBaseAnimating* pAnimating = pMoveParent->m_pOuter->GetBaseAnimating();
		if (pAnimating && pAnimating->GetEngineObject()->GetAttachment(m_iParentAttachment, tempMatrix))
		{
			return tempMatrix;
		}
	}

	// If we fall through to here, then just use the move parent's abs origin and angles.
	return pMoveParent->EntityToWorldTransform();
}


//-----------------------------------------------------------------------------
// These methods recompute local versions as well as set abs versions
//-----------------------------------------------------------------------------
void CEngineObjectInternal::SetAbsOrigin(const Vector& absOrigin)
{
	AssertMsg(absOrigin.IsValid(), "Invalid origin set");

	// This is necessary to get the other fields of m_rgflCoordinateFrame ok
	CalcAbsolutePosition();

	if (m_vecAbsOrigin == absOrigin)
		return;
	//m_pOuter->NetworkStateChanged(55551);

	// All children are invalid, but we are not
	InvalidatePhysicsRecursive(POSITION_CHANGED);
	RemoveEFlags(EFL_DIRTY_ABSTRANSFORM);

	m_vecAbsOrigin = absOrigin;

	MatrixSetColumn(absOrigin, 3, m_rgflCoordinateFrame);

	Vector vecNewOrigin;
	CEngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		vecNewOrigin = absOrigin;
	}
	else
	{
		matrix3x4_t tempMat;
		const matrix3x4_t& parentTransform = GetParentToWorldTransform(tempMat);

		// Moveparent case: transform the abs position into local space
		VectorITransform(absOrigin, parentTransform, vecNewOrigin);
	}

	if (m_vecOrigin != vecNewOrigin)
	{
		//m_pOuter->NetworkStateChanged(55554);
		m_vecOrigin = vecNewOrigin;
		SetSimulationTime(gpGlobals->curtime);
	}
}

void CEngineObjectInternal::SetAbsAngles(const QAngle& absAngles)
{
	// This is necessary to get the other fields of m_rgflCoordinateFrame ok
	CalcAbsolutePosition();

	// FIXME: The normalize caused problems in server code like momentary_rot_button that isn't
	//        handling things like +/-180 degrees properly. This should be revisited.
	//QAngle angleNormalize( AngleNormalize( absAngles.x ), AngleNormalize( absAngles.y ), AngleNormalize( absAngles.z ) );

	if (m_angAbsRotation == absAngles)
		return;
	//m_pOuter->NetworkStateChanged(55552);

	// All children are invalid, but we are not
	InvalidatePhysicsRecursive(ANGLES_CHANGED);
	RemoveEFlags(EFL_DIRTY_ABSTRANSFORM);

	m_angAbsRotation = absAngles;
	AngleMatrix(absAngles, m_rgflCoordinateFrame);
	MatrixSetColumn(m_vecAbsOrigin, 3, m_rgflCoordinateFrame);

	QAngle angNewRotation;
	CEngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		angNewRotation = absAngles;
	}
	else
	{
		if (m_angAbsRotation == pMoveParent->GetAbsAngles())
		{
			angNewRotation.Init();
		}
		else
		{
			// Moveparent case: transform the abs transform into local space
			matrix3x4_t worldToParent, localMatrix;
			MatrixInvert(pMoveParent->EntityToWorldTransform(), worldToParent);
			ConcatTransforms(worldToParent, m_rgflCoordinateFrame, localMatrix);
			MatrixAngles(localMatrix, angNewRotation);
		}
	}

	if (m_angRotation != angNewRotation)
	{
		//m_pOuter->NetworkStateChanged(55555);
		m_angRotation = angNewRotation;
		SetSimulationTime(gpGlobals->curtime);
	}
}

void CEngineObjectInternal::SetAbsVelocity(const Vector& vecAbsVelocity)
{
	if (m_vecAbsVelocity == vecAbsVelocity)
		return;
	//m_pOuter->NetworkStateChanged(55556);
	//m_pOuter->NetworkStateChanged(55553);
	// The abs velocity won't be dirty since we're setting it here
	// All children are invalid, but we are not
	InvalidatePhysicsRecursive(VELOCITY_CHANGED);
	RemoveEFlags(EFL_DIRTY_ABSVELOCITY);

	m_vecAbsVelocity = vecAbsVelocity;

	// NOTE: Do *not* do a network state change in this case.
	// m_vecVelocity is only networked for the player, which is not manual mode
	CEngineObjectInternal* pMoveParent = GetMoveParent();
	if (!pMoveParent)
	{
		m_vecVelocity = vecAbsVelocity;
		return;
	}

	// First subtract out the parent's abs velocity to get a relative
	// velocity measured in world space
	Vector relVelocity;
	VectorSubtract(vecAbsVelocity, pMoveParent->GetAbsVelocity(), relVelocity);

	// Transform relative velocity into parent space
	Vector vNew;
	VectorIRotate(relVelocity, pMoveParent->EntityToWorldTransform(), vNew);
	m_vecVelocity = vNew;
}

static double s_LastEntityReasonableEmitTime;
bool EmitReasonablePhysicsSpew()
{

	// Reported recently?
	double now = Plat_FloatTime();
	if (now >= s_LastEntityReasonableEmitTime && now < s_LastEntityReasonableEmitTime + 5.0)
	{
		// Already reported recently
		return false;
	}

	// Not reported recently.  Report it now
	s_LastEntityReasonableEmitTime = now;
	return true;
}

inline bool IsPositionReasonable(const Vector& v)
{
	float r = MAX_COORD_FLOAT;
	return
		v.x > -r && v.x < r &&
		v.y > -r && v.y < r &&
		v.z > -r && v.z < r;
}

//-----------------------------------------------------------------------------
// Methods that modify local physics state, and let us know to compute abs state later
//-----------------------------------------------------------------------------
void CEngineObjectInternal::SetLocalOrigin(const Vector& origin)
{
	// Safety check against NaN's or really huge numbers
	if (!IsPositionReasonable(origin))
	{
		if (EmitReasonablePhysicsSpew())
		{
			Warning("Bad SetLocalOrigin(%f,%f,%f) on %s\n", origin.x, origin.y, origin.z, m_pOuter->GetDebugName());
		}
		Assert(false);
		return;
	}

	//	if ( !origin.IsValid() )
	//	{
	//		AssertMsg( 0, "Bad origin set" );
	//		return;
	//	}

	if (m_vecOrigin != origin)
	{
		// Sanity check to make sure the origin is valid.
#ifdef _DEBUG
		float largeVal = 1024 * 128;
		Assert(origin.x >= -largeVal && origin.x <= largeVal);
		Assert(origin.y >= -largeVal && origin.y <= largeVal);
		Assert(origin.z >= -largeVal && origin.z <= largeVal);
#endif
		//m_pOuter->NetworkStateChanged(55554);
		InvalidatePhysicsRecursive(POSITION_CHANGED);
		m_vecOrigin = origin;
		SetSimulationTime(gpGlobals->curtime);
	}
}

inline bool IsQAngleReasonable(const QAngle& q)
{
	float r = 360.0 * 1000.0f;
	return
		q.x > -r && q.x < r &&
		q.y > -r && q.y < r &&
		q.z > -r && q.z < r;
}

void CEngineObjectInternal::SetLocalAngles(const QAngle& angles)
{
	// NOTE: The angle normalize is a little expensive, but we can save
	// a bunch of time in interpolation if we don't have to invalidate everything
	// and sometimes it's off by a normalization amount

	// FIXME: The normalize caused problems in server code like momentary_rot_button that isn't
	//        handling things like +/-180 degrees properly. This should be revisited.
	//QAngle angleNormalize( AngleNormalize( angles.x ), AngleNormalize( angles.y ), AngleNormalize( angles.z ) );

	// Safety check against NaN's or really huge numbers
	if (!IsQAngleReasonable(angles))
	{
		if (EmitReasonablePhysicsSpew())
		{
			Warning("Bad SetLocalAngles(%f,%f,%f) on %s\n", angles.x, angles.y, angles.z, m_pOuter->GetDebugName());
		}
		Assert(false);
		return;
	}

	if (m_angRotation != angles)
	{
		//m_pOuter->NetworkStateChanged(55555);
		InvalidatePhysicsRecursive(ANGLES_CHANGED);
		m_angRotation = angles;
		SetSimulationTime(gpGlobals->curtime);
	}
}

int CheckVelocity(Vector& v)
{
	float r = 2000.0f * 2.0f;
	if (
		v.x > -r && v.x < r &&
		v.y > -r && v.y < r &&
		v.z > -r && v.z < r)
	{
		// The usual case.  It's totally reasonable
		return 1;
	}
	float speed = v.Length();
	if (speed < 2000.0f * 2.0f * 100.0f)
	{
		// Sort of suspicious.  Clamp it
		v *= 2000.0f * 2.0f / speed;
		return 0;
	}

	// A terrible, horrible, no good, very bad velocity.
	return -1;
}

void CEngineObjectInternal::SetLocalVelocity(const Vector& inVecVelocity)
{
	Vector vecVelocity = inVecVelocity;

	// Safety check against receive a huge impulse, which can explode physics
	switch (CheckVelocity(vecVelocity))
	{
	case -1:
		Warning("Discarding SetLocalVelocity(%f,%f,%f) on %s\n", vecVelocity.x, vecVelocity.y, vecVelocity.z, m_pOuter->GetDebugName());
		Assert(false);
		return;
	case 0:
		if (EmitReasonablePhysicsSpew())
		{
			Warning("Clamping SetLocalVelocity(%f,%f,%f) on %s\n", inVecVelocity.x, inVecVelocity.y, inVecVelocity.z, m_pOuter->GetDebugName());
		}
		break;
	}

	if (m_vecVelocity != vecVelocity)
	{
		//m_pOuter->NetworkStateChanged(55556);
		InvalidatePhysicsRecursive(VELOCITY_CHANGED);
		m_vecVelocity = vecVelocity;
	}
}

const Vector& CEngineObjectInternal::GetLocalVelocity() const
{
	return m_vecVelocity;
}

const Vector& CEngineObjectInternal::GetAbsVelocity()
{
	Assert(gEntList.IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSVELOCITY))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsoluteVelocity();
	}
	return m_vecAbsVelocity;
}

const Vector& CEngineObjectInternal::GetAbsVelocity() const
{
	Assert(gEntList.IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSVELOCITY))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsoluteVelocity();
	}
	return m_vecAbsVelocity;
}

//-----------------------------------------------------------------------------
// Physics state accessor methods
//-----------------------------------------------------------------------------

const Vector& CEngineObjectInternal::GetLocalOrigin(void) const
{
	return m_vecOrigin;
}

const QAngle& CEngineObjectInternal::GetLocalAngles(void) const
{
	return m_angRotation;
}

const Vector& CEngineObjectInternal::GetAbsOrigin(void)
{
	Assert(gEntList.IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsolutePosition();
	}
	return m_vecAbsOrigin;
}

const Vector& CEngineObjectInternal::GetAbsOrigin(void) const
{
	Assert(gEntList.IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsolutePosition();
	}
	return m_vecAbsOrigin;
}

const QAngle& CEngineObjectInternal::GetAbsAngles(void)
{
	Assert(gEntList.IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsolutePosition();
	}
	return m_angAbsRotation;
}

const QAngle& CEngineObjectInternal::GetAbsAngles(void) const
{
	Assert(gEntList.IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsolutePosition();
	}
	return m_angAbsRotation;
}

//-----------------------------------------------------------------------------
// Methods relating to traversing hierarchy
//-----------------------------------------------------------------------------
CEngineObjectInternal* CEngineObjectInternal::GetMoveParent(void)
{
	return m_hMoveParent.Get() ? (CEngineObjectInternal*)(m_hMoveParent.Get()->GetEngineObject()) : NULL;
}

void CEngineObjectInternal::SetMoveParent(IEngineObjectServer* hMoveParent) {
	m_hMoveParent = hMoveParent? hMoveParent->GetOuter():NULL;
	//this->NetworkStateChanged();
}

CEngineObjectInternal* CEngineObjectInternal::FirstMoveChild(void)
{
	return gEntList.GetBaseEntity(m_hMoveChild) ? (CEngineObjectInternal*)gEntList.GetBaseEntity(m_hMoveChild)->GetEngineObject() : NULL;
}

void CEngineObjectInternal::SetFirstMoveChild(IEngineObjectServer* hMoveChild) {
	m_hMoveChild = hMoveChild? hMoveChild->GetOuter():NULL;
}

CEngineObjectInternal* CEngineObjectInternal::NextMovePeer(void)
{
	return gEntList.GetBaseEntity(m_hMovePeer) ? (CEngineObjectInternal*)gEntList.GetBaseEntity(m_hMovePeer)->GetEngineObject() : NULL;
}

void CEngineObjectInternal::SetNextMovePeer(IEngineObjectServer* hMovePeer) {
	m_hMovePeer = hMovePeer? hMovePeer->GetOuter():NULL;
}

CEngineObjectInternal* CEngineObjectInternal::GetRootMoveParent()
{
	CEngineObjectInternal* pEntity = this;
	CEngineObjectInternal* pParent = this->GetMoveParent();
	while (pParent)
	{
		pEntity = pParent;
		pParent = pEntity->GetMoveParent();
	}

	return pEntity;
}

void CEngineObjectInternal::ResetRgflCoordinateFrame() {
	MatrixSetColumn(this->m_vecAbsOrigin, 3, this->m_rgflCoordinateFrame);
}

//-----------------------------------------------------------------------------
// Returns the entity-to-world transform
//-----------------------------------------------------------------------------
matrix3x4_t& CEngineObjectInternal::EntityToWorldTransform()
{
	Assert(gEntList.IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
	{
		CalcAbsolutePosition();
	}
	return m_rgflCoordinateFrame;
}

const matrix3x4_t& CEngineObjectInternal::EntityToWorldTransform() const
{
	Assert(gEntList.IsAbsQueriesValid());

	if (IsEFlagSet(EFL_DIRTY_ABSTRANSFORM))
	{
		const_cast<CEngineObjectInternal*>(this)->CalcAbsolutePosition();
	}
	return m_rgflCoordinateFrame;
}

//-----------------------------------------------------------------------------
// Some helper methods that transform a point from entity space to world space + back
//-----------------------------------------------------------------------------
void CEngineObjectInternal::EntityToWorldSpace(const Vector& in, Vector* pOut) const
{
	if (const_cast<CEngineObjectInternal*>(this)->GetAbsAngles() == vec3_angle)
	{
		VectorAdd(in, const_cast<CEngineObjectInternal*>(this)->GetAbsOrigin(), *pOut);
	}
	else
	{
		VectorTransform(in, EntityToWorldTransform(), *pOut);
	}
}

void CEngineObjectInternal::WorldToEntitySpace(const Vector& in, Vector* pOut) const
{
	if (const_cast<CEngineObjectInternal*>(this)->GetAbsAngles() == vec3_angle)
	{
		VectorSubtract(in, const_cast<CEngineObjectInternal*>(this)->GetAbsOrigin(), *pOut);
	}
	else
	{
		VectorITransform(in, EntityToWorldTransform(), *pOut);
	}
}

void EntityTouch_Add(CBaseEntity* pEntity);

void CEngineObjectInternal::SetCheckUntouch(bool check)
{
	// Invalidate touchstamp
	if (check)
	{
		touchStamp++;
		if (!IsEFlagSet(EFL_CHECK_UNTOUCH))
		{
			AddEFlags(EFL_CHECK_UNTOUCH);
			EntityTouch_Add(this->GetOuter());
		}
	}
	else
	{
		RemoveEFlags(EFL_CHECK_UNTOUCH);
	}
}

bool CEngineObjectInternal::HasDataObjectType(int type) const
{
	Assert(type >= 0 && type < NUM_DATAOBJECT_TYPES);
	return (m_fDataObjectTypes & (1 << type)) ? true : false;
}

void CEngineObjectInternal::AddDataObjectType(int type)
{
	Assert(type >= 0 && type < NUM_DATAOBJECT_TYPES);
	m_fDataObjectTypes |= (1 << type);
}

void CEngineObjectInternal::RemoveDataObjectType(int type)
{
	Assert(type >= 0 && type < NUM_DATAOBJECT_TYPES);
	m_fDataObjectTypes &= ~(1 << type);
}

void* CEngineObjectInternal::GetDataObject(int type)
{
	Assert(type >= 0 && type < NUM_DATAOBJECT_TYPES);
	if (!HasDataObjectType(type))
		return NULL;
	return gEntList.GetDataObject(type, m_pOuter);
}

void* CEngineObjectInternal::CreateDataObject(int type)
{
	Assert(type >= 0 && type < NUM_DATAOBJECT_TYPES);
	AddDataObjectType(type);
	return gEntList.CreateDataObject(type, m_pOuter);
}

void CEngineObjectInternal::DestroyDataObject(int type)
{
	Assert(type >= 0 && type < NUM_DATAOBJECT_TYPES);
	if (!HasDataObjectType(type))
		return;
	gEntList.DestroyDataObject(type, m_pOuter);
	RemoveDataObjectType(type);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CEngineObjectInternal::DestroyAllDataObjects(void)
{
	int i;
	for (i = 0; i < NUM_DATAOBJECT_TYPES; i++)
	{
		if (HasDataObjectType(i))
		{
			DestroyDataObject(i);
		}
	}
}

void CEngineObjectInternal::OnPositionChanged()
{
	m_pOuter->OnPositionChanged();
}

void CEngineObjectInternal::OnAnglesChanged()
{
	m_pOuter->OnAnglesChanged();
}

void CEngineObjectInternal::OnAnimationChanged()
{
	m_pOuter->OnAnimationChanged();
}

//-----------------------------------------------------------------------------
// Invalidates the abs state of all children
//-----------------------------------------------------------------------------
void CEngineObjectInternal::InvalidatePhysicsRecursive(int nChangeFlags)
{
	// Main entry point for dirty flag setting for the 90% case
	// 1) If the origin changes, then we have to update abstransform, Shadow projection, PVS, KD-tree, 
	//    client-leaf system.
	// 2) If the angles change, then we have to update abstransform, Shadow projection,
	//    shadow render-to-texture, client-leaf system, and surrounding bounds. 
	//	  Children have to additionally update absvelocity, KD-tree, and PVS.
	//	  If the surrounding bounds actually update, when we also need to update the KD-tree and the PVS.
	// 3) If it's due to attachment, then all children who are attached to an attachment point
	//    are assumed to have dirty origin + angles.

	// Other stuff:
	// 1) Marking the surrounding bounds dirty will automatically mark KD tree + PVS dirty.

	int nDirtyFlags = 0;

	if (nChangeFlags & VELOCITY_CHANGED)
	{
		nDirtyFlags |= EFL_DIRTY_ABSVELOCITY;
	}

	if (nChangeFlags & POSITION_CHANGED)
	{
		nDirtyFlags |= EFL_DIRTY_ABSTRANSFORM;

//#ifndef CLIENT_DLL
		MarkPVSInformationDirty();
//#endif
		OnPositionChanged();
	}

	// NOTE: This has to be done after velocity + position are changed
	// because we change the nChangeFlags for the child entities
	if (nChangeFlags & ANGLES_CHANGED)
	{
		nDirtyFlags |= EFL_DIRTY_ABSTRANSFORM;
		OnAnglesChanged();

		// This is going to be used for all children: children
		// have position + velocity changed
		nChangeFlags |= POSITION_CHANGED | VELOCITY_CHANGED;
	}

	AddEFlags(nDirtyFlags);

	// Set flags for children
	bool bOnlyDueToAttachment = false;
	if (nChangeFlags & ANIMATION_CHANGED)
	{
		OnAnimationChanged();

		// Only set this flag if the only thing that changed us was the animation.
		// If position or something else changed us, then we must tell all children.
		if (!(nChangeFlags & (POSITION_CHANGED | VELOCITY_CHANGED | ANGLES_CHANGED)))
		{
			bOnlyDueToAttachment = true;
		}

		nChangeFlags = POSITION_CHANGED | ANGLES_CHANGED | VELOCITY_CHANGED;
	}

	for (CEngineObjectInternal* pChild = FirstMoveChild(); pChild; pChild = pChild->NextMovePeer())
	{
		// If this is due to the parent animating, only invalidate children that are parented to an attachment
		// Entities that are following also access attachments points on parents and must be invalidated.
		if (bOnlyDueToAttachment)
		{
			if (pChild->GetParentAttachment() == 0)
				continue;
		}
		pChild->InvalidatePhysicsRecursive(nChangeFlags);
	}

	//
	// This code should really be in here, or the bone cache should not be in world space.
	// Since the bone transforms are in world space, if we move or rotate the entity, its
	// bones should be marked invalid.
	//
	// As it is, we're near ship, and don't have time to setup a good A/B test of how much
	// overhead this fix would add. We've also only got one known case where the lack of
	// this fix is screwing us, and I just fixed it, so I'm leaving this commented out for now.
	//
	// Hopefully, we'll put the bone cache in entity space and remove the need for this fix.
	//
	//#ifdef CLIENT_DLL
	//	if ( nChangeFlags & (POSITION_CHANGED | ANGLES_CHANGED | ANIMATION_CHANGED) )
	//	{
	//		C_BaseAnimating *pAnim = GetBaseAnimating();
	//		if ( pAnim )
	//			pAnim->InvalidateBoneCache();		
	//	}
	//#endif
}

static trace_t g_TouchTrace;
static bool g_bCleanupDatObject = true;

const trace_t& CEngineObjectInternal::GetTouchTrace(void)
{
	return g_TouchTrace;
}

//-----------------------------------------------------------------------------
// Purpose: Two entities have touched, so run their touch functions
// Input  : *other - 
//			*ptrace - 
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsImpact(IEngineObjectServer* other, trace_t& trace)
{
	if (!other)
	{
		return;
	}

	// If either of the entities is flagged to be deleted, 
	//  don't call the touch functions
	if ((GetFlags() | other->GetFlags()) & FL_KILLME)
	{
		return;
	}

	PhysicsMarkEntitiesAsTouching(other, trace);
}

void CEngineObjectInternal::PhysicsTouchTriggers(const Vector* pPrevAbsOrigin)
{
	if (m_pOuter->IsNetworkable() && entindex() != -1 && !m_pOuter->IsWorld())
	{
		bool isTriggerCheckSolids = IsSolidFlagSet(FSOLID_TRIGGER);
		bool isSolidCheckTriggers = IsSolid() && !isTriggerCheckSolids;		// NOTE: Moving triggers (items, ammo etc) are not 
		// checked against other triggers to reduce the number of touchlinks created
		if (!(isSolidCheckTriggers || isTriggerCheckSolids))
			return;

		if (GetSolid() == SOLID_BSP)
		{
			if (!GetModel() && Q_strlen(STRING(GetModelName())) == 0)
			{
				Warning("Inserted %s with no model\n", GetClassname());
				return;
			}
		}

		SetCheckUntouch(true);
		if (isSolidCheckTriggers)
		{
			engine->SolidMoved(this->m_pOuter, &m_Collision, pPrevAbsOrigin, gEntList.IsAccurateTriggerBboxChecks());
		}
		if (isTriggerCheckSolids)
		{
			engine->TriggerMoved(this->m_pOuter, gEntList.IsAccurateTriggerBboxChecks());
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Marks the fact that two edicts are in contact
// Input  : *other - other entity
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsMarkEntitiesAsTouching(IEngineObjectServer* other, trace_t& trace)
{
	g_TouchTrace = trace;
	PhysicsMarkEntityAsTouched(other);
	other->PhysicsMarkEntityAsTouched(this);
}


void CEngineObjectInternal::PhysicsMarkEntitiesAsTouchingEventDriven(IEngineObjectServer* other, trace_t& trace)
{
	g_TouchTrace = trace;
	g_TouchTrace.m_pEnt = other->GetOuter();

	servertouchlink_t* link;
	link = this->PhysicsMarkEntityAsTouched(other);
	if (link)
	{
		// mark these links as event driven so they aren't untouched the next frame
		// when the physics doesn't refresh them
		link->touchStamp = TOUCHSTAMP_EVENT_DRIVEN;
	}
	g_TouchTrace.m_pEnt = this->GetOuter();
	link = other->PhysicsMarkEntityAsTouched(this);
	if (link)
	{
		link->touchStamp = TOUCHSTAMP_EVENT_DRIVEN;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Marks in an entity that it is touching another entity, and calls
//			it's Touch() function if it is a new touch.
//			Stamps the touch link with the new time so that when we check for
//			untouch we know things haven't changed.
// Input  : *other - entity that it is in contact with
//-----------------------------------------------------------------------------
servertouchlink_t* CEngineObjectInternal::PhysicsMarkEntityAsTouched(IEngineObjectServer* other)
{
	servertouchlink_t* link;

	if (this == other)
		return NULL;

	// Entities in hierarchy should not interact
	if ((this->GetMoveParent() == other) || (this == other->GetMoveParent()))
		return NULL;

	// check if either entity doesn't generate touch functions
	if ((GetFlags() | other->GetFlags()) & FL_DONTTOUCH)
		return NULL;

	// Pure triggers should not touch each other
	if (IsSolidFlagSet(FSOLID_TRIGGER) && other->IsSolidFlagSet(FSOLID_TRIGGER))
	{
		if (!IsSolid() && !other->IsSolid())
			return NULL;
	}

	// Don't do touching if marked for deletion
	if (other->IsMarkedForDeletion())
	{
		return NULL;
	}

	if (IsMarkedForDeletion())
	{
		return NULL;
	}

	if (gEntList.m_ActivePortals.Count() > 0) {
		CPortalTouchScope scope;
	}

	// check if the edict is already in the list
	servertouchlink_t* root = (servertouchlink_t*)GetDataObject(TOUCHLINK);
	if (root)
	{
		for (link = root->nextLink; link != root; link = link->nextLink)
		{
			if (link->entityTouched == other->GetOuter())
			{
				// update stamp
				link->touchStamp = GetTouchStamp();

				if (!gEntList.IsDisableTouchFuncs())
				{
					PhysicsTouch(other);
				}

				// no more to do
				return link;
			}
		}
	}
	else
	{
		// Allocate the root object
		root = (servertouchlink_t*)CreateDataObject(TOUCHLINK);
		root->nextLink = root->prevLink = root;
	}

	// entity is not in list, so it's a new touch
	// add it to the touched list and then call the touch function

	// build new link
	link = AllocTouchLink();
	if (DebugTouchlinks())
		Msg("add 0x%p: %s-%s (%d-%d) [%d in play, %d max]\n", link, m_pOuter->GetDebugName(), other->GetOuter()->GetDebugName(), entindex(), other->GetOuter()->entindex(), linksallocated, g_EdictTouchLinks.PeakCount());
	if (!link)
		return NULL;

	link->touchStamp = GetTouchStamp();
	link->entityTouched = other->GetOuter();
	link->flags = 0;
	// add it to the list
	link->nextLink = root->nextLink;
	link->prevLink = root;
	link->prevLink->nextLink = link;
	link->nextLink->prevLink = link;

	// non-solid entities don't get touched
	bool bShouldTouch = (IsSolid() && !IsSolidFlagSet(FSOLID_VOLUME_CONTENTS)) || IsSolidFlagSet(FSOLID_TRIGGER);
	if (bShouldTouch && !other->IsSolidFlagSet(FSOLID_TRIGGER))
	{
		link->flags |= FTOUCHLINK_START_TOUCH;
		if (!gEntList.IsDisableTouchFuncs())
		{
			PhysicsStartTouch(other);
		}
	}

	return link;
}


//-----------------------------------------------------------------------------
// Purpose: Called every frame that two entities are touching
// Input  : *pentOther - the entity who it has touched
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsTouch(IEngineObjectServer* pentOther)
{
	if (pentOther)
	{
		if (!(IsMarkedForDeletion() || pentOther->IsMarkedForDeletion()))
		{
			m_pOuter->Touch(pentOther->GetOuter());
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Called whenever two entities come in contact
// Input  : *pentOther - the entity who it has touched
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsStartTouch(IEngineObjectServer* pentOther)
{
	if (pentOther)
	{
		if (!(IsMarkedForDeletion() || pentOther->IsMarkedForDeletion()))
		{
			m_pOuter->StartTouch(pentOther->GetOuter());
			m_pOuter->Touch(pentOther->GetOuter());
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::IsCurrentlyTouching(void) const
{
	if (HasDataObjectType(TOUCHLINK))
	{
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Checks to see if any entities that have been touching this one
//			have stopped touching it, and notify the entity if so.
//			Called at the end of a frame, after all the entities have run
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsCheckForEntityUntouch(void)
{
	//Assert( g_pNextLink == NULL );

	servertouchlink_t* link, * nextLink;

	servertouchlink_t* root = (servertouchlink_t*)this->GetDataObject(TOUCHLINK);
	if (root)
	{
		if (gEntList.m_ActivePortals.Count() > 0) {
			CPortalTouchScope scope;
		}
		bool saveCleanup = g_bCleanupDatObject;
		g_bCleanupDatObject = false;

		link = root->nextLink;
		while (link && link != root)
		{
			nextLink = link->nextLink;

			// these touchlinks are not polled.  The ents are touching due to an outside
			// system that will add/delete them as necessary (vphysics in this case)
			if (link->touchStamp == TOUCHSTAMP_EVENT_DRIVEN)
			{
				// refresh the touch call
				PhysicsTouch(((CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched))->GetEngineObject());
			}
			else
			{
				// check to see if the touch stamp is up to date
				if (link->touchStamp != this->GetTouchStamp())
				{
					// stamp is out of data, so entities are no longer touching
					// remove self from other entities touch list
					((CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched))->GetEngineObject()->PhysicsNotifyOtherOfUntouch(this);

					// remove other entity from this list
					this->PhysicsRemoveToucher(link);
				}
			}

			link = nextLink;
		}

		g_bCleanupDatObject = saveCleanup;

		// Nothing left in list, destroy root
		if (root->nextLink == root &&
			root->prevLink == root)
		{
			DestroyDataObject(TOUCHLINK);
		}
	}

	//g_pNextLink = NULL;

	SetCheckUntouch(false);
}

//-----------------------------------------------------------------------------
// Purpose: notifies an entity than another touching entity has moved out of contact.
// Input  : *other - the entity to be acted upon
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsNotifyOtherOfUntouch(IEngineObjectServer* ent)
{
	// loop through ed's touch list, looking for the notifier
	// remove and call untouch if found
	servertouchlink_t* root = (servertouchlink_t*)this->GetDataObject(TOUCHLINK);
	if (root)
	{
		servertouchlink_t* link = root->nextLink;
		while (link && link != root)
		{
			if (link->entityTouched == ent->GetOuter())
			{
				this->PhysicsRemoveToucher(link);

				// Check for complete removal
				if (g_bCleanupDatObject &&
					root->nextLink == root &&
					root->prevLink == root)
				{
					this->DestroyDataObject(TOUCHLINK);
				}
				return;
			}

			link = link->nextLink;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Clears all touches from the list
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsRemoveTouchedList()
{
	if (gEntList.m_ActivePortals.Count() > 0) {
		CPortalTouchScope scope;
	}

	servertouchlink_t* link, * nextLink;

	servertouchlink_t* root = (servertouchlink_t*)this->GetDataObject(TOUCHLINK);
	if (root)
	{
		link = root->nextLink;
		bool saveCleanup = g_bCleanupDatObject;
		g_bCleanupDatObject = false;
		while (link && link != root)
		{
			nextLink = link->nextLink;

			// notify the other entity that this ent has gone away
			if (gEntList.GetServerEntityFromHandle(link->entityTouched)) {
				((CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched))->GetEngineObject()->PhysicsNotifyOtherOfUntouch(this);
			}

			// kill it
			if (DebugTouchlinks())
				Msg("remove 0x%p: %s-%s (%d-%d) [%d in play, %d max]\n", link, this->m_pOuter->GetDebugName(), ((CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched))->GetDebugName(), this->entindex(), ((CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched))->entindex(), linksallocated, g_EdictTouchLinks.PeakCount());
			FreeTouchLink(link);
			link = nextLink;
		}

		g_bCleanupDatObject = saveCleanup;
		this->DestroyDataObject(TOUCHLINK);
	}

	this->ClearTouchStamp();
}

//-----------------------------------------------------------------------------
// Purpose: removes a toucher from the list
// Input  : *link - the link to remove
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsRemoveToucher(servertouchlink_t* link)
{
	// Every start Touch gets a corresponding end touch
	if ((link->flags & FTOUCHLINK_START_TOUCH) &&
		link->entityTouched != INVALID_EHANDLE_INDEX)
	{
		CBaseEntity* pEntity = (CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched);
		this->m_pOuter->EndTouch(pEntity);
	}

	link->nextLink->prevLink = link->prevLink;
	link->prevLink->nextLink = link->nextLink;

	if (DebugTouchlinks())
		Msg("remove 0x%p: %s-%s (%d-%d) [%d in play, %d max]\n", link, ((CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched))->GetDebugName(), this->m_pOuter->GetDebugName(), ((CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched))->entindex(), this->entindex(), linksallocated, g_EdictTouchLinks.PeakCount());
	FreeTouchLink(link);
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *other - 
// Output : groundlink_t
//-----------------------------------------------------------------------------
servergroundlink_t* CEngineObjectInternal::AddEntityToGroundList(IEngineObjectServer* other)
{
	servergroundlink_t* link;

	if (this == other)
		return NULL;

	// check if the edict is already in the list
	servergroundlink_t* root = (servergroundlink_t*)GetDataObject(GROUNDLINK);
	if (root)
	{
		for (link = root->nextLink; link != root; link = link->nextLink)
		{
			if (link->entity == other->GetOuter())
			{
				// no more to do
				return link;
			}
		}
	}
	else
	{
		root = (servergroundlink_t*)CreateDataObject(GROUNDLINK);
		root->prevLink = root->nextLink = root;
	}

	// entity is not in list, so it's a new touch
	// add it to the touched list and then call the touch function

	// build new link
	link = AllocGroundLink();
	if (!link)
		return NULL;

	link->entity = other->GetOuter();
	// add it to the list
	link->nextLink = root->nextLink;
	link->prevLink = root;
	link->prevLink->nextLink = link;
	link->nextLink->prevLink = link;

	PhysicsStartGroundContact(other);

	return link;
}

//-----------------------------------------------------------------------------
// Purpose: Called whenever two entities come in contact
// Input  : *pentOther - the entity who it has touched
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsStartGroundContact(IEngineObjectServer* pentOther)
{
	if (!pentOther)
		return;

	if (!(IsMarkedForDeletion() || pentOther->IsMarkedForDeletion()))
	{
		pentOther->GetOuter()->StartGroundContact(this->m_pOuter);
	}
}

//-----------------------------------------------------------------------------
// Purpose: notifies an entity than another touching entity has moved out of contact.
// Input  : *other - the entity to be acted upon
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsNotifyOtherOfGroundRemoval(IEngineObjectServer* ent)
{
	// loop through ed's touch list, looking for the notifier
	// remove and call untouch if found
	servergroundlink_t* root = (servergroundlink_t*)this->GetDataObject(GROUNDLINK);
	if (root)
	{
		servergroundlink_t* link = root->nextLink;
		while (link != root)
		{
			if (link->entity == ent->GetOuter())
			{
				PhysicsRemoveGround(link);

				if (root->nextLink == root &&
					root->prevLink == root)
				{
					this->DestroyDataObject(GROUNDLINK);
				}
				return;
			}

			link = link->nextLink;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: removes a toucher from the list
// Input  : *link - the link to remove
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsRemoveGround(servergroundlink_t* link)
{
	// Every start Touch gets a corresponding end touch
	if (link->entity != INVALID_EHANDLE_INDEX)
	{
		CBaseEntity* linkEntity = (CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entity);
		CBaseEntity* otherEntity = this->m_pOuter;
		if (linkEntity && otherEntity)
		{
			linkEntity->EndGroundContact(otherEntity);
		}
	}

	link->nextLink->prevLink = link->prevLink;
	link->prevLink->nextLink = link->nextLink;
	FreeGroundLink(link);
}

//-----------------------------------------------------------------------------
// Purpose: static method to remove ground list for an entity
// Input  : *ent - 
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsRemoveGroundList()
{
	servergroundlink_t* link, * nextLink;

	servergroundlink_t* root = (servergroundlink_t*)this->GetDataObject(GROUNDLINK);
	if (root)
	{
		link = root->nextLink;
		while (link && link != root)
		{
			nextLink = link->nextLink;

			// notify the other entity that this ent has gone away
			((CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entity))->GetEngineObject()->PhysicsNotifyOtherOfGroundRemoval(this);

			// kill it
			FreeGroundLink(link);

			link = nextLink;
		}

		this->DestroyDataObject(GROUNDLINK);
	}
}

void CEngineObjectInternal::SetGroundEntity(IEngineObjectServer* ground)
{
	if ((m_hGroundEntity.Get() ? m_hGroundEntity.Get()->GetEngineObject() : NULL) == ground)
		return;

	// this can happen in-between updates to the held object controller (physcannon, +USE)
	// so trap it here and release held objects when they become player ground
	if (ground && m_pOuter->IsPlayer() && ground->GetMoveType() == MOVETYPE_VPHYSICS)
	{
		CBasePlayer* pPlayer = static_cast<CBasePlayer*>(this->m_pOuter);
		IPhysicsObject* pPhysGround = ground->VPhysicsGetObject();
		if (pPhysGround && pPlayer)
		{
			if (pPhysGround->GetGameFlags() & FVPHYSICS_PLAYER_HELD)
			{
				pPlayer->ForceDropOfCarriedPhysObjects(ground->GetOuter());
			}
		}
	}

	CBaseEntity* oldGround = m_hGroundEntity;
	m_hGroundEntity = ground ? ground->GetOuter() : NULL;

	// Just starting to touch
	if (!oldGround && ground)
	{
		ground->AddEntityToGroundList(this);
	}
	// Just stopping touching
	else if (oldGround && !ground)
	{
		oldGround->GetEngineObject()->PhysicsNotifyOtherOfGroundRemoval(this);
	}
	// Changing out to new ground entity
	else
	{
		oldGround->GetEngineObject()->PhysicsNotifyOtherOfGroundRemoval(this);
		ground->AddEntityToGroundList(this);
	}

	// HACK/PARANOID:  This is redundant with the code above, but in case we get out of sync groundlist entries ever, 
	//  this will force the appropriate flags
	if (ground)
	{
		AddFlag(FL_ONGROUND);
	}
	else
	{
		RemoveFlag(FL_ONGROUND);
	}
}

CEngineObjectInternal* CEngineObjectInternal::GetGroundEntity(void)
{
	return m_hGroundEntity.Get() ? (CEngineObjectInternal*)m_hGroundEntity.Get()->GetEngineObject() : NULL;
}

void CEngineObjectInternal::SetModelIndex(int index)
{
	//if ( IsDynamicModelIndex( index ) && !(GetBaseAnimating() && m_bDynamicModelAllowed) )
	//{
	//	AssertMsg( false, "dynamic model support not enabled on server entity" );
	//	index = -1;
	//}
		// delete exiting studio model container
	if (!m_pModel || index != m_nModelIndex)
	{
		/*if ( m_bDynamicModelPending )
		{
			sg_DynamicLoadHandlers.Remove( this );
		}*/
		UnlockStudioHdr();
		m_pStudioHdr = NULL;
		//modelinfo->ReleaseDynamicModel( m_nModelIndex );
		//modelinfo->AddRefDynamicModel( index );
		m_nModelIndex = index;

		//m_bDynamicModelSetBounds = false;

		//if ( IsDynamicModelIndex( index ) )
		//{
		//	m_bDynamicModelPending = true;
		//	sg_DynamicLoadHandlers[ sg_DynamicLoadHandlers.Insert( this ) ].Register( index );
		//}
		//else
		//{
		//	m_bDynamicModelPending = false;
		//m_pOuter->OnNewModel();
		const model_t* pModel = modelinfo->GetModel(m_nModelIndex);
		SetModelPointer(pModel);
		//}
	}
	m_pOuter->DispatchUpdateTransmitState();
}

bool CEngineObjectInternal::Intersects(IEngineObjectServer* pOther)
{
	//if (entindex() == -1 || pOther->entindex() == -1)
	//	return false;
	return IsOBBIntersectingOBB(
		this->GetCollisionOrigin(), this->GetCollisionAngles(), this->OBBMins(), this->OBBMaxs(),
		pOther->GetCollisionOrigin(), pOther->GetCollisionAngles(), pOther->OBBMins(), pOther->OBBMaxs());
}

void CEngineObjectInternal::SetCollisionGroup(int collisionGroup)
{
	if ((int)m_CollisionGroup != collisionGroup)
	{
		m_CollisionGroup = collisionGroup;
		CollisionRulesChanged();
	}
}


void CEngineObjectInternal::CollisionRulesChanged()
{
	// ivp maintains state based on recent return values from the collision filter, so anything
	// that can change the state that a collision filter will return (like m_Solid) needs to call RecheckCollisionFilter.
	if (VPhysicsGetObject())
	{
		if (gEntList.PhysIsInCallback())
		{
			Warning("Changing collision rules within a callback is likely to cause crashes!\n");
			Assert(0);
		}
		IPhysicsObject* pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
		int count = VPhysicsGetObjectList(pList, ARRAYSIZE(pList));
		for (int i = 0; i < count; i++)
		{
			if (pList[i] != NULL) //this really shouldn't happen, but it does >_<
				pList[i]->RecheckCollisionFilter();
		}
	}
}

#if !defined( CLIENT_DLL )
#define CHANGE_FLAGS(flags,newFlags) { unsigned int old = flags; flags = (newFlags); gEntList.ReportEntityFlagsChanged( this->m_pOuter, old, flags ); }
#else
#define CHANGE_FLAGS(flags,newFlags) (flags = (newFlags))
#endif

void CEngineObjectInternal::AddFlag(int flags)
{
	CHANGE_FLAGS(m_fFlags, m_fFlags | flags);
}

void CEngineObjectInternal::RemoveFlag(int flagsToRemove)
{
	CHANGE_FLAGS(m_fFlags, m_fFlags & ~flagsToRemove);
}

void CEngineObjectInternal::ClearFlags(void)
{
	CHANGE_FLAGS(m_fFlags, 0);
}

void CEngineObjectInternal::ToggleFlag(int flagToToggle)
{
	CHANGE_FLAGS(m_fFlags, m_fFlags ^ flagToToggle);
}

void CEngineObjectInternal::SetEffects(int nEffects)
{
	if (nEffects != m_fEffects)
	{
		m_pOuter->OnSetEffects(nEffects);
		m_fEffects = nEffects;
		m_pOuter->DispatchUpdateTransmitState();
	}
}

void CEngineObjectInternal::AddEffects(int nEffects)
{
	m_pOuter->OnAddEffects(nEffects);
	m_fEffects |= nEffects;
	if (nEffects & EF_NODRAW)
	{
		m_pOuter->DispatchUpdateTransmitState();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int	CEngineObjectInternal::GetIndexForThinkContext(const char* pszContext)
{
	for (int i = 0; i < m_aThinkFunctions.Size(); i++)
	{
		if (!Q_strncmp(STRING(m_aThinkFunctions[i].m_iszContext), pszContext, MAX_CONTEXT_LENGTH))
			return i;
	}

	return NO_THINK_CONTEXT;
}

//-----------------------------------------------------------------------------
// Purpose: Get a fresh think context for this entity
//-----------------------------------------------------------------------------
int CEngineObjectInternal::RegisterThinkContext(const char* szContext)
{
	int iIndex = GetIndexForThinkContext(szContext);
	if (iIndex != NO_THINK_CONTEXT)
		return iIndex;

	// Make a new think func
	thinkfunc_t sNewFunc;
	Q_memset(&sNewFunc, 0, sizeof(sNewFunc));
	sNewFunc.m_pfnThink = NULL;
	sNewFunc.m_nNextThinkTick = 0;
	sNewFunc.m_iszContext = AllocPooledString(szContext);

	// Insert it into our list
	return m_aThinkFunctions.AddToTail(sNewFunc);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
THINKPTR CEngineObjectInternal::ThinkSet(THINKPTR func, float thinkTime, const char* szContext)
{
#if !defined( CLIENT_DLL )
#ifdef _DEBUG
#ifdef PLATFORM_64BITS
#ifdef GNUC
	COMPILE_TIME_ASSERT(sizeof(func) == 16);
#else
	COMPILE_TIME_ASSERT(sizeof(func) == 8);
#endif
#else
#ifdef GNUC
	COMPILE_TIME_ASSERT(sizeof(func) == 8);
#else
	COMPILE_TIME_ASSERT(sizeof(func) == 4);
#endif
#endif
#endif
#endif

	// Old system?
	if (!szContext)
	{
		m_pfnThink = func;
#if !defined( CLIENT_DLL )
#ifdef _DEBUG
		m_pOuter->FunctionCheck(*(reinterpret_cast<void**>(&m_pfnThink)), "BaseThinkFunc");
#endif
#endif
		return m_pfnThink;
	}

	// Find the think function in our list, and if we couldn't find it, register it
	int iIndex = GetIndexForThinkContext(szContext);
	if (iIndex == NO_THINK_CONTEXT)
	{
		iIndex = RegisterThinkContext(szContext);
	}

	m_aThinkFunctions[iIndex].m_pfnThink = func;
#if !defined( CLIENT_DLL )
#ifdef _DEBUG
	m_pOuter->FunctionCheck(*(reinterpret_cast<void**>(&m_aThinkFunctions[iIndex].m_pfnThink)), szContext);
#endif
#endif

	if (thinkTime != 0)
	{
		int thinkTick = (thinkTime == TICK_NEVER_THINK) ? TICK_NEVER_THINK : TIME_TO_TICKS(thinkTime);
		m_aThinkFunctions[iIndex].m_nNextThinkTick = thinkTick;
		CheckHasThinkFunction(thinkTick == TICK_NEVER_THINK ? false : true);
	}
	return func;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CEngineObjectInternal::SetNextThink(float thinkTime, const char* szContext)
{
	int thinkTick = (thinkTime == TICK_NEVER_THINK) ? TICK_NEVER_THINK : TIME_TO_TICKS(thinkTime);

	// Are we currently in a think function with a context?
	int iIndex = 0;
	if (!szContext)
	{
#ifdef _DEBUG
		if (m_iCurrentThinkContext != NO_THINK_CONTEXT)
		{
			Msg("Warning: Setting base think function within think context %s\n", STRING(m_aThinkFunctions[m_iCurrentThinkContext].m_iszContext));
		}
#endif

		// Old system
		m_nNextThinkTick = thinkTick;
		CheckHasThinkFunction(thinkTick == TICK_NEVER_THINK ? false : true);
		return;
	}
	else
	{
		// Find the think function in our list, and if we couldn't find it, register it
		iIndex = GetIndexForThinkContext(szContext);
		if (iIndex == NO_THINK_CONTEXT)
		{
			iIndex = RegisterThinkContext(szContext);
		}
	}

	// Old system
	m_aThinkFunctions[iIndex].m_nNextThinkTick = thinkTick;
	CheckHasThinkFunction(thinkTick == TICK_NEVER_THINK ? false : true);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
float CEngineObjectInternal::GetNextThink(const char* szContext)
{
	// Are we currently in a think function with a context?
	int iIndex = 0;
	if (!szContext)
	{
#ifdef _DEBUG
		if (m_iCurrentThinkContext != NO_THINK_CONTEXT)
		{
			Msg("Warning: Getting base nextthink time within think context %s\n", STRING(m_aThinkFunctions[m_iCurrentThinkContext].m_iszContext));
		}
#endif

		if (m_nNextThinkTick == TICK_NEVER_THINK)
			return TICK_NEVER_THINK;

		// Old system
		return TICK_INTERVAL * (m_nNextThinkTick);
	}
	else
	{
		// Find the think function in our list
		iIndex = GetIndexForThinkContext(szContext);
	}

	if (iIndex == m_aThinkFunctions.InvalidIndex())
		return TICK_NEVER_THINK;

	if (m_aThinkFunctions[iIndex].m_nNextThinkTick == TICK_NEVER_THINK)
	{
		return TICK_NEVER_THINK;
	}
	return TICK_INTERVAL * (m_aThinkFunctions[iIndex].m_nNextThinkTick);
}

int	CEngineObjectInternal::GetNextThinkTick(const char* szContext /*= NULL*/)
{
	// Are we currently in a think function with a context?
	int iIndex = 0;
	if (!szContext)
	{
#ifdef _DEBUG
		if (m_iCurrentThinkContext != NO_THINK_CONTEXT)
		{
			Msg("Warning: Getting base nextthink time within think context %s\n", STRING(m_aThinkFunctions[m_iCurrentThinkContext].m_iszContext));
		}
#endif

		if (m_nNextThinkTick == TICK_NEVER_THINK)
			return TICK_NEVER_THINK;

		// Old system
		return m_nNextThinkTick;
	}
	else
	{
		// Find the think function in our list
		iIndex = GetIndexForThinkContext(szContext);

		// Looking up an invalid think context!
		Assert(iIndex != -1);
	}

	if ((iIndex == -1) || (m_aThinkFunctions[iIndex].m_nNextThinkTick == TICK_NEVER_THINK))
	{
		return TICK_NEVER_THINK;
	}

	return m_aThinkFunctions[iIndex].m_nNextThinkTick;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
float CEngineObjectInternal::GetLastThink(const char* szContext)
{
	// Are we currently in a think function with a context?
	int iIndex = 0;
	if (!szContext)
	{
#ifdef _DEBUG
		if (m_iCurrentThinkContext != NO_THINK_CONTEXT)
		{
			Msg("Warning: Getting base lastthink time within think context %s\n", STRING(m_aThinkFunctions[m_iCurrentThinkContext].m_iszContext));
		}
#endif
		// Old system
		return m_nLastThinkTick * TICK_INTERVAL;
	}
	else
	{
		// Find the think function in our list
		iIndex = GetIndexForThinkContext(szContext);
	}

	return m_aThinkFunctions[iIndex].m_nLastThinkTick * TICK_INTERVAL;
}

int CEngineObjectInternal::GetLastThinkTick(const char* szContext /*= NULL*/)
{
	// Are we currently in a think function with a context?
	int iIndex = 0;
	if (!szContext)
	{
#ifdef _DEBUG
		if (m_iCurrentThinkContext != NO_THINK_CONTEXT)
		{
			Msg("Warning: Getting base lastthink time within think context %s\n", STRING(m_aThinkFunctions[m_iCurrentThinkContext].m_iszContext));
		}
#endif
		// Old system
		return m_nLastThinkTick;
	}
	else
	{
		// Find the think function in our list
		iIndex = GetIndexForThinkContext(szContext);
	}

	return m_aThinkFunctions[iIndex].m_nLastThinkTick;
}

void CEngineObjectInternal::SetLastThinkTick(int iThinkTick)
{
	m_nLastThinkTick = iThinkTick;
}

bool CEngineObjectInternal::WillThink()
{
	if (m_nNextThinkTick > 0)
		return true;

	for (int i = 0; i < m_aThinkFunctions.Count(); i++)
	{
		if (m_aThinkFunctions[i].m_nNextThinkTick > 0)
			return true;
	}

	return false;
}

// returns the first tick the entity will run any think function
// returns TICK_NEVER_THINK if no think functions are scheduled
int CEngineObjectInternal::GetFirstThinkTick()
{
	int minTick = TICK_NEVER_THINK;
	if (m_nNextThinkTick > 0)
	{
		minTick = m_nNextThinkTick;
	}

	for (int i = 0; i < m_aThinkFunctions.Count(); i++)
	{
		int next = m_aThinkFunctions[i].m_nNextThinkTick;
		if (next > 0)
		{
			if (next < minTick || minTick == TICK_NEVER_THINK)
			{
				minTick = next;
			}
		}
	}
	return minTick;
}

//-----------------------------------------------------------------------------
// Sets/Gets the next think based on context index
//-----------------------------------------------------------------------------
void CEngineObjectInternal::SetNextThink(int nContextIndex, float thinkTime)
{
	int thinkTick = (thinkTime == TICK_NEVER_THINK) ? TICK_NEVER_THINK : TIME_TO_TICKS(thinkTime);

	if (nContextIndex < 0)
	{
		SetNextThink(thinkTime);
	}
	else
	{
		m_aThinkFunctions[nContextIndex].m_nNextThinkTick = thinkTick;
	}
	CheckHasThinkFunction(thinkTick == TICK_NEVER_THINK ? false : true);
}

void CEngineObjectInternal::SetLastThink(int nContextIndex, float thinkTime)
{
	int thinkTick = (thinkTime == TICK_NEVER_THINK) ? TICK_NEVER_THINK : TIME_TO_TICKS(thinkTime);

	if (nContextIndex < 0)
	{
		m_nLastThinkTick = thinkTick;
	}
	else
	{
		m_aThinkFunctions[nContextIndex].m_nLastThinkTick = thinkTick;
	}
}

float CEngineObjectInternal::GetNextThink(int nContextIndex) const
{
	if (nContextIndex < 0)
		return m_nNextThinkTick * TICK_INTERVAL;

	return m_aThinkFunctions[nContextIndex].m_nNextThinkTick * TICK_INTERVAL;
}

int	CEngineObjectInternal::GetNextThinkTick(int nContextIndex) const
{
	if (nContextIndex < 0)
		return m_nNextThinkTick;

	return m_aThinkFunctions[nContextIndex].m_nNextThinkTick;
}

// NOTE: pass in the isThinking hint so we have to search the think functions less
void CEngineObjectInternal::CheckHasThinkFunction(bool isThinking)
{
	if (IsEFlagSet(EFL_NO_THINK_FUNCTION) && isThinking)
	{
		RemoveEFlags(EFL_NO_THINK_FUNCTION);
	}
	else if (!isThinking && !IsEFlagSet(EFL_NO_THINK_FUNCTION) && !WillThink())
	{
		AddEFlags(EFL_NO_THINK_FUNCTION);
	}
#if !defined( CLIENT_DLL )
	SimThink_EntityChanged(this->m_pOuter);
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Runs thinking code if time.  There is some play in the exact time the think
//  function will be called, because it is called before any movement is done
//  in a frame.  Not used for pushmove objects, because they must be exact.
//  Returns false if the entity removed itself.
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::PhysicsRunThink(thinkmethods_t thinkMethod)
{
	if (IsEFlagSet(EFL_NO_THINK_FUNCTION))
		return true;

	bool bAlive = true;

	// Don't fire the base if we're avoiding it
	if (thinkMethod != THINK_FIRE_ALL_BUT_BASE)
	{
		bAlive = PhysicsRunSpecificThink(-1, (THINKPTR) & CBaseEntity::Think);
		if (!bAlive)
			return false;
	}

	// Are we just firing the base think?
	if (thinkMethod == THINK_FIRE_BASE_ONLY)
		return bAlive;

	// Fire the rest of 'em
	for (int i = 0; i < m_aThinkFunctions.Count(); i++)
	{
#ifdef _DEBUG
		// Set the context
		m_iCurrentThinkContext = i;
#endif

		bAlive = PhysicsRunSpecificThink(i, m_aThinkFunctions[i].m_pfnThink);

#ifdef _DEBUG
		// Clear our context
		m_iCurrentThinkContext = NO_THINK_CONTEXT;
#endif

		if (!bAlive)
			return false;
	}

	return bAlive;
}

//-----------------------------------------------------------------------------
// Purpose: For testing if all thinks are occuring at the same time
//-----------------------------------------------------------------------------
struct ThinkSync
{
	float					thinktime;
	int						thinktick;
	CUtlVector< EHANDLE >	entities;

	ThinkSync()
	{
		thinktime = 0;
	}

	ThinkSync(const ThinkSync& src)
	{
		thinktime = src.thinktime;
		thinktick = src.thinktick;
		int c = src.entities.Count();
		for (int i = 0; i < c; i++)
		{
			entities.AddToTail(src.entities[i]);
		}
	}
};

//-----------------------------------------------------------------------------
// Purpose: For testing if all thinks are occuring at the same time
//-----------------------------------------------------------------------------
class CThinkSyncTester
{
public:
	CThinkSyncTester() :
		m_Thinkers(0, 0, ThinkLessFunc)
	{
		m_nLastFrameCount = -1;
		m_bShouldCheck = false;
	}

	void EntityThinking(int framecount, CBaseEntity* ent, float thinktime, int thinktick)
	{
#if !defined( CLIENT_DLL )
		if (m_nLastFrameCount != framecount)
		{
			if (m_bShouldCheck)
			{
				// Report
				Report();
				m_Thinkers.RemoveAll();
				m_nLastFrameCount = framecount;
			}

			m_bShouldCheck = sv_thinktimecheck.GetBool();
		}

		if (!m_bShouldCheck)
			return;

		ThinkSync* p = FindOrAddItem(ent, thinktime);
		if (!p)
		{
			Assert(0);
		}

		p->thinktime = thinktime;
		p->thinktick = thinktick;
		EHANDLE h;
		h = ent;
		p->entities.AddToTail(h);
#endif
	}

private:

	static bool ThinkLessFunc(const ThinkSync& item1, const ThinkSync& item2)
	{
		return item1.thinktime < item2.thinktime;
	}

	ThinkSync* FindOrAddItem(CBaseEntity* ent, float thinktime)
	{
		ThinkSync item;
		item.thinktime = thinktime;

		int idx = m_Thinkers.Find(item);
		if (idx == m_Thinkers.InvalidIndex())
		{
			idx = m_Thinkers.Insert(item);
		}

		return &m_Thinkers[idx];
	}

	void Report()
	{
		if (m_Thinkers.Count() == 0)
			return;

		Msg("-----------------\nThink report frame %i\n", gpGlobals->tickcount);

		for (int i = m_Thinkers.FirstInorder();
			i != m_Thinkers.InvalidIndex();
			i = m_Thinkers.NextInorder(i))
		{
			ThinkSync* p = &m_Thinkers[i];
			Assert(p);
			if (!p)
				continue;

			int ecount = p->entities.Count();
			if (!ecount)
			{
				continue;
			}

			Msg("thinktime %f, %i entities\n", p->thinktime, ecount);
			for (int j = 0; j < ecount; j++)
			{
				EHANDLE h = p->entities[j];
				int lastthinktick = 0;
				int nextthinktick = 0;
				CBaseEntity* e = h.Get();
				if (e)
				{
					lastthinktick = e->GetEngineObject()->GetLastThinkTick();
					nextthinktick = e->GetEngineObject()->GetNextThinkTick();
				}

				Msg("  %p : %30s (last %5i/next %5i)\n", h.Get(), h.Get() ? h->GetClassname() : "NULL",
					lastthinktick, nextthinktick);
			}
		}
	}

	CUtlRBTree< ThinkSync >	m_Thinkers;
	int			m_nLastFrameCount;
	bool		m_bShouldCheck;
};

static CThinkSyncTester g_ThinkChecker;

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::PhysicsRunSpecificThink(int nContextIndex, THINKPTR thinkFunc)
{
	int thinktick = GetNextThinkTick(nContextIndex);

	if (thinktick <= 0 || thinktick > gpGlobals->tickcount)
		return true;

	float thinktime = thinktick * TICK_INTERVAL;

	// Don't let things stay in the past.
	//  it is possible to start that way
	//  by a trigger with a local time.
	if (thinktime < gpGlobals->curtime)
	{
		thinktime = gpGlobals->curtime;
	}

	// Only do this on the game server
#if !defined( CLIENT_DLL )
	g_ThinkChecker.EntityThinking(gpGlobals->tickcount, this->m_pOuter, thinktime, m_nNextThinkTick);
#endif

	SetNextThink(nContextIndex, TICK_NEVER_THINK);

	PhysicsDispatchThink(thinkFunc);

	SetLastThink(nContextIndex, gpGlobals->curtime);

	// Return whether entity is still valid
	return (!IsMarkedForDeletion());
}

//-----------------------------------------------------------------------------
// Purpose: Called when it's time for a physically moved objects (plats, doors, etc)
//			to run it's game code.
//			All other entity thinking is done during worldspawn's think
//-----------------------------------------------------------------------------
void CEngineObjectInternal::PhysicsDispatchThink(THINKPTR thinkFunc)
{
	VPROF_ENTER_SCOPE((!vprof_scope_entity_thinks.GetBool()) ?
		"CBaseEntity::PhysicsDispatchThink" :
		gEntList.GetCannonicalName(GetClassname()));

	float thinkLimit = think_limit.GetFloat();

	// The thinkLimit stuff makes a LOT of calls to Sys_FloatTime, which winds up calling into
	// VCR mode so much that the framerate becomes unusable.
	if (VCRGetMode() != VCR_Disabled)
		thinkLimit = 0;

	float startTime = 0.0;

	if (m_pOuter->IsDormant())
	{
		Warning("Dormant entity %s (%s) is thinking!!\n", GetClassname(), m_pOuter->GetDebugName());
		Assert(0);
	}

	if (thinkLimit)
	{
		startTime = engine->Time();
	}

	if (thinkFunc)
	{
		MDLCACHE_CRITICAL_SECTION();
		(m_pOuter->*thinkFunc)();
	}

	if (thinkLimit)
	{
		// calculate running time of the AI in milliseconds
		float time = (engine->Time() - startTime) * 1000.0f;
		if (time > thinkLimit)
		{
#if defined( _XBOX ) && !defined( _RETAIL )
			if (vprof_think_limit.GetBool())
			{
				extern bool g_VProfSignalSpike;
				g_VProfSignalSpike = true;
			}
#endif
			// If its an NPC print out the shedule/task that took so long
			if (m_pOuter->ShouldReportOverThinkLimit())
			{
				m_pOuter->ReportOverThinkLimit(time);
			}
			else
			{
#ifdef _WIN32
				Msg("%s(%s) thinking for %.02f ms!!!\n", STRING(GetClassname()), typeid(m_pOuter).raw_name(), time);
#elif POSIX
				Msg("%s(%s) thinking for %.02f ms!!!\n", STRING(GetClassname()), typeid(m_pOuter).name(), time);
#else
#error "typeinfo"
#endif
			}
		}
	}

	VPROF_EXIT_SCOPE();
}

void CEngineObjectInternal::SetMoveType(MoveType_t val, MoveCollide_t moveCollide)
{
#ifdef _DEBUG
	// Make sure the move type + move collide are compatible...
	if ((val != MOVETYPE_FLY) && (val != MOVETYPE_FLYGRAVITY))
	{
		Assert(moveCollide == MOVECOLLIDE_DEFAULT);
	}

	if (m_MoveType == MOVETYPE_VPHYSICS && val != m_MoveType)
	{
		if (VPhysicsGetObject() && val != MOVETYPE_NONE)
		{
			// What am I supposed to do with the physics object if
			// you're changing away from MOVETYPE_VPHYSICS without making the object 
			// shadow?  This isn't likely to work, assert.
			// You probably meant to call VPhysicsInitShadow() instead of VPhysicsInitNormal()!
			Assert(VPhysicsGetObject()->GetShadowController());
		}
	}
#endif

	if (m_MoveType == val)
	{
		m_MoveCollide = moveCollide;
		return;
	}

	// This is needed to the removal of MOVETYPE_FOLLOW:
	// We can't transition from follow to a different movetype directly
	// or the leaf code will break.
	Assert(!IsEffectActive(EF_BONEMERGE));
	m_MoveType = val;
	m_MoveCollide = moveCollide;

	CollisionRulesChanged();

	switch (m_MoveType)
	{
	case MOVETYPE_WALK:
	{
		SetSimulatedEveryTick(true);
		SetAnimatedEveryTick(true);
	}
	break;
	case MOVETYPE_STEP:
	{
		// This will probably go away once I remove the cvar that controls the test code
		SetSimulatedEveryTick(g_bTestMoveTypeStepSimulation ? true : false);
		SetAnimatedEveryTick(false);
	}
	break;
	case MOVETYPE_FLY:
	case MOVETYPE_FLYGRAVITY:
	{
		// Initialize our water state, because these movetypes care about transitions in/out of water
		m_pOuter->UpdateWaterState();
	}
	break;
	default:
	{
		SetSimulatedEveryTick(true);
		SetAnimatedEveryTick(false);
	}
	}

	// This will probably go away or be handled in a better way once I remove the cvar that controls the test code
	CheckStepSimulationChanged();
	CheckHasGamePhysicsSimulation();
}

//-----------------------------------------------------------------------------
// Purpose: Until we remove the above cvar, we need to have the entities able
//  to dynamically deal with changing their simulation stuff here.
//-----------------------------------------------------------------------------
void CEngineObjectInternal::CheckStepSimulationChanged()
{
	if (g_bTestMoveTypeStepSimulation != IsSimulatedEveryTick())
	{
		SetSimulatedEveryTick(g_bTestMoveTypeStepSimulation);
	}

	bool hadobject = HasDataObjectType(STEPSIMULATION);

	if (g_bTestMoveTypeStepSimulation)
	{
		if (!hadobject)
		{
			CreateDataObject(STEPSIMULATION);
		}
	}
	else
	{
		if (hadobject)
		{
			DestroyDataObject(STEPSIMULATION);
		}
	}
}

bool CEngineObjectInternal::WillSimulateGamePhysics()
{
	// players always simulate game physics
	if (!m_pOuter->IsPlayer())
	{
		MoveType_t movetype = GetMoveType();

		if (movetype == MOVETYPE_NONE || movetype == MOVETYPE_VPHYSICS)
			return false;

#if !defined( CLIENT_DLL )
		// MOVETYPE_PUSH not supported on the client
		if (movetype == MOVETYPE_PUSH && m_pOuter->GetMoveDoneTime() <= 0)
			return false;
#endif
	}

	return true;
}

void CEngineObjectInternal::CheckHasGamePhysicsSimulation()
{
	bool isSimulating = WillSimulateGamePhysics();
	if (isSimulating != IsEFlagSet(EFL_NO_GAME_PHYSICS_SIMULATION))
		return;
	if (isSimulating)
	{
		RemoveEFlags(EFL_NO_GAME_PHYSICS_SIMULATION);
	}
	else
	{
		AddEFlags(EFL_NO_GAME_PHYSICS_SIMULATION);
	}
#if !defined( CLIENT_DLL )
	SimThink_EntityChanged(this->m_pOuter);
#endif
}

//-----------------------------------------------------------------------------
bool CEngineObjectInternal::UseStepSimulationNetworkOrigin(const Vector** out_v)
{
	Assert(out_v);


	if (g_bTestMoveTypeStepSimulation &&
		GetMoveType() == MOVETYPE_STEP &&
		HasDataObjectType(STEPSIMULATION))
	{
		StepSimulationData* step = (StepSimulationData*)GetDataObject(STEPSIMULATION);
		ComputeStepSimulationNetwork(step);
		*out_v = &step->m_vecNetworkOrigin;

		return step->m_bOriginActive;
	}

	return false;
}

//-----------------------------------------------------------------------------
bool CEngineObjectInternal::UseStepSimulationNetworkAngles(const QAngle** out_a)
{
	Assert(out_a);

	if (g_bTestMoveTypeStepSimulation &&
		GetMoveType() == MOVETYPE_STEP &&
		HasDataObjectType(STEPSIMULATION))
	{
		StepSimulationData* step = (StepSimulationData*)GetDataObject(STEPSIMULATION);
		ComputeStepSimulationNetwork(step);
		*out_a = &step->m_angNetworkAngles;
		return step->m_bAnglesActive;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Run one tick's worth of faked simulation
// Input  : *step - 
//-----------------------------------------------------------------------------
void CEngineObjectInternal::ComputeStepSimulationNetwork(StepSimulationData* step)
{
	if (!step)
	{
		Assert(!"ComputeStepSimulationNetworkOriginAndAngles with NULL step\n");
		return;
	}

	// Don't run again if we've already calculated this tick
	if (step->m_nLastProcessTickCount == gpGlobals->tickcount)
	{
		return;
	}

	step->m_nLastProcessTickCount = gpGlobals->tickcount;

	// Origin
	// It's inactive
	if (step->m_bOriginActive)
	{
		// First see if any external code moved the entity
		if (m_pOuter->GetStepOrigin() != step->m_Next.vecOrigin)
		{
			step->m_bOriginActive = false;
		}
		else
		{
			// Compute interpolated info based on tick interval
			float frac = 1.0f;
			int tickdelta = step->m_Next.nTickCount - step->m_Previous.nTickCount;
			if (tickdelta > 0)
			{
				frac = (float)(gpGlobals->tickcount - step->m_Previous.nTickCount) / (float)tickdelta;
				frac = clamp(frac, 0.0f, 1.0f);
			}

			if (step->m_Previous2.nTickCount == 0 || step->m_Previous2.nTickCount >= step->m_Previous.nTickCount)
			{
				Vector delta = step->m_Next.vecOrigin - step->m_Previous.vecOrigin;
				VectorMA(step->m_Previous.vecOrigin, frac, delta, step->m_vecNetworkOrigin);
			}
			else if (!step_spline.GetBool())
			{
				StepSimulationStep* pOlder = &step->m_Previous;
				StepSimulationStep* pNewer = &step->m_Next;

				if (step->m_Discontinuity.nTickCount > step->m_Previous.nTickCount)
				{
					if (gpGlobals->tickcount > step->m_Discontinuity.nTickCount)
					{
						pOlder = &step->m_Discontinuity;
					}
					else
					{
						pNewer = &step->m_Discontinuity;
					}

					tickdelta = pNewer->nTickCount - pOlder->nTickCount;
					if (tickdelta > 0)
					{
						frac = (float)(gpGlobals->tickcount - pOlder->nTickCount) / (float)tickdelta;
						frac = clamp(frac, 0.0f, 1.0f);
					}
				}

				Vector delta = pNewer->vecOrigin - pOlder->vecOrigin;
				VectorMA(pOlder->vecOrigin, frac, delta, step->m_vecNetworkOrigin);
			}
			else
			{
				Hermite_Spline(step->m_Previous2.vecOrigin, step->m_Previous.vecOrigin, step->m_Next.vecOrigin, frac, step->m_vecNetworkOrigin);
			}
		}
	}

	// Angles
	if (step->m_bAnglesActive)
	{
		// See if external code changed the orientation of the entity
		if (m_pOuter->GetStepAngles() != step->m_angNextRotation)
		{
			step->m_bAnglesActive = false;
		}
		else
		{
			// Compute interpolated info based on tick interval
			float frac = 1.0f;
			int tickdelta = step->m_Next.nTickCount - step->m_Previous.nTickCount;
			if (tickdelta > 0)
			{
				frac = (float)(gpGlobals->tickcount - step->m_Previous.nTickCount) / (float)tickdelta;
				frac = clamp(frac, 0.0f, 1.0f);
			}

			if (step->m_Previous2.nTickCount == 0 || step->m_Previous2.nTickCount >= step->m_Previous.nTickCount)
			{
				// Pure blend between start/end orientations
				Quaternion outangles;
				QuaternionBlend(step->m_Previous.qRotation, step->m_Next.qRotation, frac, outangles);
				QuaternionAngles(outangles, step->m_angNetworkAngles);
			}
			else if (!step_spline.GetBool())
			{
				StepSimulationStep* pOlder = &step->m_Previous;
				StepSimulationStep* pNewer = &step->m_Next;

				if (step->m_Discontinuity.nTickCount > step->m_Previous.nTickCount)
				{
					if (gpGlobals->tickcount > step->m_Discontinuity.nTickCount)
					{
						pOlder = &step->m_Discontinuity;
					}
					else
					{
						pNewer = &step->m_Discontinuity;
					}

					tickdelta = pNewer->nTickCount - pOlder->nTickCount;
					if (tickdelta > 0)
					{
						frac = (float)(gpGlobals->tickcount - pOlder->nTickCount) / (float)tickdelta;
						frac = clamp(frac, 0.0f, 1.0f);
					}
				}

				// Pure blend between start/end orientations
				Quaternion outangles;
				QuaternionBlend(pOlder->qRotation, pNewer->qRotation, frac, outangles);
				QuaternionAngles(outangles, step->m_angNetworkAngles);
			}
			else
			{
				// FIXME: enable spline interpolation when turning is debounced.
				Quaternion outangles;
				Hermite_Spline(step->m_Previous2.qRotation, step->m_Previous.qRotation, step->m_Next.qRotation, frac, outangles);
				QuaternionAngles(outangles, step->m_angNetworkAngles);
			}
		}
	}

}

inline bool AnyPlayersInHierarchy_R(IEngineObjectServer* pEnt)
{
	if (pEnt->GetOuter()->IsPlayer())
		return true;

	for (IEngineObjectServer* pCur = pEnt->FirstMoveChild(); pCur; pCur = pCur->NextMovePeer())
	{
		if (AnyPlayersInHierarchy_R(pCur))
			return true;
	}

	return false;
}

void CEngineObjectInternal::RecalcHasPlayerChildBit()
{
	if (AnyPlayersInHierarchy_R(this))
		AddEFlags(EFL_HAS_PLAYER_CHILD);
	else
		RemoveEFlags(EFL_HAS_PLAYER_CHILD);
}

bool CEngineObjectInternal::DoesHavePlayerChild()
{
	return IsEFlagSet(EFL_HAS_PLAYER_CHILD);
}

//-----------------------------------------------------------------------------
// These methods encapsulate MOVETYPE_FOLLOW, which became obsolete
//-----------------------------------------------------------------------------
void CEngineObjectInternal::FollowEntity(IEngineObjectServer* pBaseEntity, bool bBoneMerge)
{
	if (pBaseEntity)
	{
		SetParent(pBaseEntity);
		SetMoveType(MOVETYPE_NONE);

		if (bBoneMerge)
			AddEffects(EF_BONEMERGE);

		AddSolidFlags(FSOLID_NOT_SOLID);
		SetLocalOrigin(vec3_origin);
		SetLocalAngles(vec3_angle);
	}
	else
	{
		StopFollowingEntity();
	}
}

void CEngineObjectInternal::StopFollowingEntity()
{
	if (!IsFollowingEntity())
	{
		//		Assert( GetEngineObject()->IsEffectActive( EF_BONEMERGE ) == 0 );
		return;
	}

	SetParent(NULL);
	RemoveEffects(EF_BONEMERGE);
	RemoveSolidFlags(FSOLID_NOT_SOLID);
	SetMoveType(MOVETYPE_NONE);
	CollisionRulesChanged();
}

bool CEngineObjectInternal::IsFollowingEntity()
{
	return IsEffectActive(EF_BONEMERGE) && (GetMoveType() == MOVETYPE_NONE) && GetMoveParent();
}

IEngineObjectServer* CEngineObjectInternal::GetFollowedEntity()
{
	if (!IsFollowingEntity())
		return NULL;
	return GetMoveParent();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CEngineObjectInternal::UseClientSideAnimation()
{
	m_bClientSideAnimation = true;
}

void CEngineObjectInternal::SimulationChanged() {
	NetworkStateChanged(&m_vecOrigin);
	NetworkStateChanged(&m_angRotation);
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : scale - 
//-----------------------------------------------------------------------------
void CEngineObjectInternal::SetModelScale(float scale, float change_duration /*= 0.0f*/)
{
	if (change_duration > 0.0f)
	{
		ModelScale* mvs = (ModelScale*)CreateDataObject(MODELSCALE);
		mvs->m_flModelScaleStart = m_flModelScale;
		mvs->m_flModelScaleGoal = scale;
		mvs->m_flModelScaleStartTime = gpGlobals->curtime;
		mvs->m_flModelScaleFinishTime = mvs->m_flModelScaleStartTime + change_duration;
	}
	else
	{
		m_flModelScale = scale;
		m_pOuter->RefreshCollisionBounds();

		if (HasDataObjectType(MODELSCALE))
		{
			DestroyDataObject(MODELSCALE);
		}
	}
}

void CEngineObjectInternal::UpdateModelScale()
{
	ModelScale* mvs = (ModelScale*)GetDataObject(MODELSCALE);
	if (!mvs)
	{
		return;
	}

	float dt = mvs->m_flModelScaleFinishTime - mvs->m_flModelScaleStartTime;
	Assert(dt > 0.0f);

	float frac = (gpGlobals->curtime - mvs->m_flModelScaleStartTime) / dt;
	frac = clamp(frac, 0.0f, 1.0f);

	if (gpGlobals->curtime >= mvs->m_flModelScaleFinishTime)
	{
		m_flModelScale = mvs->m_flModelScaleGoal;
		DestroyDataObject(MODELSCALE);
	}
	else
	{
		m_flModelScale = Lerp(frac, mvs->m_flModelScaleStart, mvs->m_flModelScaleGoal);
	}

	m_pOuter->RefreshCollisionBounds();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  :
//-----------------------------------------------------------------------------
void CEngineObjectInternal::LockStudioHdr()
{
	AUTO_LOCK(m_StudioHdrInitLock);
	const model_t* mdl = GetModel();
	if (mdl)
	{
		MDLHandle_t hStudioHdr = modelinfo->GetCacheHandle(mdl);
		if (hStudioHdr != MDLHANDLE_INVALID)
		{
			IStudioHdr* pStudioHdr = mdlcache->LockStudioHdr(hStudioHdr);
			IStudioHdr* pStudioHdrContainer = NULL;
			if (!m_pStudioHdr)
			{
				if (pStudioHdr)
				{
					pStudioHdrContainer = pStudioHdr;// mdlcache->GetIStudioHdr(pStudioHdr);
					//pStudioHdrContainer->Init( pStudioHdr, mdlcache );
				}
			}
			else
			{
				pStudioHdrContainer = m_pStudioHdr;
			}

			Assert((pStudioHdr == NULL && pStudioHdrContainer == NULL) || pStudioHdrContainer->GetRenderHdr() == pStudioHdr->GetRenderHdr());

			//if ( pStudioHdrContainer && pStudioHdrContainer->GetVirtualModel() )
			//{
			//	MDLHandle_t hVirtualModel = VoidPtrToMDLHandle( pStudioHdrContainer->GetRenderHdr()->VirtualModel() );
			//	mdlcache->LockStudioHdr( hVirtualModel );
			//}
			m_pStudioHdr = pStudioHdrContainer; // must be last to ensure virtual model correctly set up
		}
	}
}

void CEngineObjectInternal::UnlockStudioHdr()
{
	if (m_pStudioHdr)
	{
		const model_t* mdl = GetModel();
		if (mdl)
		{
			mdlcache->UnlockStudioHdr(modelinfo->GetCacheHandle(mdl));
			//if ( m_pStudioHdr->GetVirtualModel() )
			//{
			//	MDLHandle_t hVirtualModel = VoidPtrToMDLHandle( m_pStudioHdr->GetRenderHdr()->VirtualModel() );
			//	mdlcache->UnlockStudioHdr( hVirtualModel );
			//}
		}
	}
}

void CEngineObjectInternal::SetModelPointer(const model_t* pModel)
{
	if (m_pModel != pModel)
	{
		m_pModel = pModel;
		if (GetModelPtr()) {
			// Make sure m_CachedBones has space.
			if (m_CachedBoneData.Count() != GetModelPtr()->numbones())
			{
				m_CachedBoneData.SetSize(GetModelPtr()->numbones());
				for (int i = 0; i < GetModelPtr()->numbones(); i++)
				{
					SetIdentityMatrix(m_CachedBoneData[i]);
				}
			}
			m_BoneAccessor.Init(this, m_CachedBoneData.Base()); // Always call this in case the IStudioHdr has changed.
			m_BoneAccessor.SetReadableBones(0);
			m_BoneAccessor.SetWritableBones(0);
			m_flLastBoneSetupTime = 0;
		}
		m_pOuter->OnNewModel();
	}
}

const model_t* CEngineObjectInternal::GetModel(void) const
{
	return m_pModel;
}

//-----------------------------------------------------------------------------
// Purpose: Force a clientside-animating entity to reset it's frame
//-----------------------------------------------------------------------------
void CEngineObjectInternal::ResetClientsideFrame(void)
{
	// TODO: Once we can chain MSG_ENTITY messages, use one of them
	m_bClientSideFrameReset = !(bool)m_bClientSideFrameReset;
}

//=========================================================
//=========================================================
void CEngineObjectInternal::SetSequence(int nSequence)
{
	Assert(nSequence == 0 /* || IsDynamicModelLoading()*/ || (GetModelPtr() && (nSequence < GetModelPtr()->GetNumSeq()) && (GetModelPtr()->GetNumSeq() < (1 << ANIMATION_SEQUENCE_BITS))));
	m_nSequence = nSequence;
}

void CEngineObjectInternal::ResetSequence(int nSequence)
{
	m_pOuter->OnResetSequence(nSequence);

	if (!SequenceLoops())
	{
		SetCycle(0);
	}

	// Tracker 17868:  If the sequence number didn't actually change, but you call resetsequence info, it changes
	//  the newsequenceparity bit which causes the client to call m_flCycle.Reset() which causes a very slight 
	//  discontinuity in looping animations as they reset around to cycle 0.0.  This was causing the parentattached
	//  helmet on barney to hitch every time barney's idle cycled back around to its start.
	bool changed = nSequence != GetSequence() ? true : false;

	SetSequence(nSequence);
	if (changed || !SequenceLoops())
	{
		ResetSequenceInfo();
	}
}

//=========================================================
//=========================================================
void CEngineObjectInternal::ResetSequenceInfo()
{
	if (GetSequence() == -1)
	{
		// This shouldn't happen.  Setting m_nSequence blindly is a horrible coding practice.
		SetSequence(0);
	}

	//if ( IsDynamicModelLoading() )
	//{
	//	m_bResetSequenceInfoOnLoad = true;
	//	return;
	//}

	IStudioHdr* pStudioHdr = GetModelPtr();
	m_flGroundSpeed = GetSequenceGroundSpeed(pStudioHdr, GetSequence()) * GetModelScale();
	m_bSequenceLoops = ((pStudioHdr->GetSequenceFlags(GetSequence()) & STUDIO_LOOPING) != 0);
	// m_flAnimTime = gpGlobals->time;
	m_flPlaybackRate = 1.0;
	m_bSequenceFinished = false;
	m_flLastEventCheck = 0;

	m_nNewSequenceParity = (m_nNewSequenceParity + 1) & EF_PARITY_MASK;
	m_nResetEventsParity = (m_nResetEventsParity + 1) & EF_PARITY_MASK;

	// FIXME: why is this called here?  Nothing should have changed to make this nessesary
	if (pStudioHdr)
	{
		mdlcache->SetEventIndexForSequence(pStudioHdr->pSeqdesc(GetSequence()));
	}
}

void CEngineObjectInternal::DoMuzzleFlash()
{
	m_nMuzzleFlashParity = (m_nMuzzleFlashParity + 1) & ((1 << EF_MUZZLEFLASH_BITS) - 1);
}

//=========================================================
//=========================================================
int CEngineObjectInternal::LookupPoseParameter(IStudioHdr* pStudioHdr, const char* szName)
{
	if (!pStudioHdr)
		return 0;

	if (!pStudioHdr->SequencesAvailable())
	{
		return 0;
	}

	for (int i = 0; i < pStudioHdr->GetNumPoseParameters(); i++)
	{
		if (Q_stricmp(pStudioHdr->pPoseParameter(i).pszName(), szName) == 0)
		{
			return i;
		}
	}

	// AssertMsg( 0, UTIL_VarArgs( "poseparameter %s couldn't be mapped!!!\n", szName ) );
	return -1; // Error
}

//=========================================================
//=========================================================
float CEngineObjectInternal::GetPoseParameter(const char* szName)
{
	return GetPoseParameter(LookupPoseParameter(szName));
}

float CEngineObjectInternal::GetPoseParameter(int iParameter)
{
	IStudioHdr* pstudiohdr = GetModelPtr();

	if (!pstudiohdr)
	{
		Assert(!"CBaseAnimating::GetPoseParameter: model missing");
		return 0.0;
	}

	if (!pstudiohdr->SequencesAvailable())
	{
		return 0;
	}

	if (iParameter >= 0)
	{
		return pstudiohdr->Studio_GetPoseParameter(iParameter, m_flPoseParameter[iParameter]);
	}

	return 0.0;
}

//=========================================================
//=========================================================
float CEngineObjectInternal::SetPoseParameter(IStudioHdr* pStudioHdr, const char* szName, float flValue)
{
	int poseParam = LookupPoseParameter(pStudioHdr, szName);
	AssertMsg2(poseParam >= 0, "SetPoseParameter called with invalid argument %s by %s", szName, m_pOuter->GetDebugName());
	return SetPoseParameter(pStudioHdr, poseParam, flValue);
}

float CEngineObjectInternal::SetPoseParameter(IStudioHdr* pStudioHdr, int iParameter, float flValue)
{
	if (!pStudioHdr)
	{
		return flValue;
	}

	if (iParameter >= 0)
	{
		float flNewValue;
		flValue = pStudioHdr->Studio_SetPoseParameter(iParameter, flValue, flNewValue);
		m_flPoseParameter.Set(iParameter, flNewValue);
	}

	return flValue;
}

//=========================================================
//=========================================================
float CEngineObjectInternal::SetBoneController(int iController, float flValue)
{
	Assert(GetModelPtr());

	IStudioHdr* pmodel = (IStudioHdr*)GetModelPtr();

	Assert(iController >= 0 && iController < NUM_BONECTRLS);

	float newValue;
	float retVal = pmodel->Studio_SetController(iController, flValue, newValue);

	float& val = m_flEncodedController.GetForModify(iController);
	val = newValue;
	return retVal;
}

//=========================================================
//=========================================================
float CEngineObjectInternal::GetBoneController(int iController)
{
	Assert(GetModelPtr());

	IStudioHdr* pmodel = (IStudioHdr*)GetModelPtr();

	return pmodel->Studio_GetController(iController, m_flEncodedController[iController]);
}

//=========================================================
//=========================================================
float CEngineObjectInternal::SequenceDuration(IStudioHdr* pStudioHdr, int iSequence)
{
	if (!pStudioHdr)
	{
		DevWarning(2, "CBaseAnimating::SequenceDuration( %d ) NULL pstudiohdr on %s!\n", iSequence, GetClassname());
		return 0.1;
	}
	if (!pStudioHdr->SequencesAvailable())
	{
		return 0.1;
	}
	if (iSequence >= pStudioHdr->GetNumSeq() || iSequence < 0)
	{
		DevWarning(2, "CBaseAnimating::SequenceDuration( %d ) out of range\n", iSequence);
		return 0.1;
	}

	return pStudioHdr->Studio_Duration(iSequence, GetPoseParameterArray());
}

float CEngineObjectInternal::GetSequenceCycleRate(IStudioHdr* pStudioHdr, int iSequence)
{
	float t = SequenceDuration(pStudioHdr, iSequence);

	if (t > 0.0f)
	{
		return 1.0f / t;
	}
	else
	{
		return 1.0f / 0.1f;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//
// Input  : iSequence - 
//
// Output : float
//-----------------------------------------------------------------------------
float CEngineObjectInternal::GetSequenceMoveDist(IStudioHdr* pStudioHdr, int iSequence)
{
	Vector				vecReturn;

	pStudioHdr->GetSequenceLinearMotion(iSequence, GetPoseParameterArray(), &vecReturn);

	return vecReturn.Length();
}

//-----------------------------------------------------------------------------
// Purpose: 
//
// Input  : iSequence - 
//
// Output : float - 
//-----------------------------------------------------------------------------
float CEngineObjectInternal::GetSequenceMoveYaw(int iSequence)
{
	Vector				vecReturn;

	Assert(GetModelPtr());
	GetModelPtr()->GetSequenceLinearMotion(iSequence, GetPoseParameterArray(), &vecReturn);

	if (vecReturn.Length() > 0)
	{
		return UTIL_VecToYaw(vecReturn);
	}

	return NOMOTION;
}

//-----------------------------------------------------------------------------
// Purpose: 
//
// Input  : iSequence - 
//			*pVec - 
//
//-----------------------------------------------------------------------------
void CEngineObjectInternal::GetSequenceLinearMotion(int iSequence, Vector* pVec)
{
	Assert(GetModelPtr());
	GetModelPtr()->GetSequenceLinearMotion(iSequence, GetPoseParameterArray(), pVec);
}

//-----------------------------------------------------------------------------
// Purpose: does a specific sequence have movement?
// Output :
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::HasMovement(int iSequence)
{
	IStudioHdr* pstudiohdr = GetModelPtr();
	if (!pstudiohdr)
		return false;

	// FIXME: this needs to check to see if there are keys, and the object is walking
	Vector deltaPos;
	QAngle deltaAngles;
	if (pstudiohdr->Studio_SeqMovement(iSequence, 0.0f, 1.0f, GetPoseParameterArray(), deltaPos, deltaAngles))
	{
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: find frame where they animation has moved a given distance.
// Output :
//-----------------------------------------------------------------------------
float CEngineObjectInternal::GetMovementFrame(float flDist)
{
	IStudioHdr* pstudiohdr = GetModelPtr();
	if (!pstudiohdr)
		return 0;

	float t = pstudiohdr->Studio_FindSeqDistance(GetSequence(), GetPoseParameterArray(), flDist);

	return t;
}

//-----------------------------------------------------------------------------
// Purpose:
// Output :
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::GetSequenceMovement(int nSequence, float fromCycle, float toCycle, Vector& deltaPosition, QAngle& deltaAngles)
{
	IStudioHdr* pstudiohdr = GetModelPtr();
	if (!pstudiohdr)
		return false;

	return pstudiohdr->Studio_SeqMovement(nSequence, fromCycle, toCycle, GetPoseParameterArray(), deltaPosition, deltaAngles);
}

//-----------------------------------------------------------------------------
// Purpose:
// Output :
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::GetIntervalMovement(float flIntervalUsed, bool& bMoveSeqFinished, Vector& newPosition, QAngle& newAngles)
{
	IStudioHdr* pstudiohdr = GetModelPtr();
	if (!pstudiohdr || !pstudiohdr->SequencesAvailable())
		return false;

	float flComputedCycleRate = GetSequenceCycleRate(GetSequence());

	float flNextCycle = GetCycle() + flIntervalUsed * flComputedCycleRate * GetPlaybackRate();

	if ((!SequenceLoops()) && flNextCycle > 1.0)
	{
		flIntervalUsed = GetCycle() / (flComputedCycleRate * GetPlaybackRate());
		flNextCycle = 1.0;
		bMoveSeqFinished = true;
	}
	else
	{
		bMoveSeqFinished = false;
	}

	Vector deltaPos;
	QAngle deltaAngles;

	if (pstudiohdr->Studio_SeqMovement(GetSequence(), GetCycle(), flNextCycle, GetPoseParameterArray(), deltaPos, deltaAngles))
	{
		VectorYawRotate(deltaPos, GetLocalAngles().y, deltaPos);
		newPosition = GetLocalOrigin() + deltaPos;
		newAngles.Init();
		newAngles.y = GetLocalAngles().y + deltaAngles.y;
		return true;
	}
	else
	{
		newPosition = GetLocalOrigin();
		newAngles = GetLocalAngles();
		return false;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
// Output :
//-----------------------------------------------------------------------------
float CEngineObjectInternal::GetEntryVelocity(int iSequence)
{
	IStudioHdr* pstudiohdr = GetModelPtr();
	if (!pstudiohdr)
		return 0;

	Vector vecVelocity;
	pstudiohdr->Studio_SeqVelocity(iSequence, 0.0, GetPoseParameterArray(), vecVelocity);

	return vecVelocity.Length();
}

float CEngineObjectInternal::GetExitVelocity(int iSequence)
{
	IStudioHdr* pstudiohdr = GetModelPtr();
	if (!pstudiohdr)
		return 0;

	Vector vecVelocity;
	pstudiohdr->Studio_SeqVelocity(iSequence, 1.0, GetPoseParameterArray(), vecVelocity);

	return vecVelocity.Length();
}

//-----------------------------------------------------------------------------
// Purpose:
// Output :
//-----------------------------------------------------------------------------
float CEngineObjectInternal::GetInstantaneousVelocity(float flInterval)
{
	IStudioHdr* pstudiohdr = GetModelPtr();
	if (!pstudiohdr)
		return 0;

	// FIXME: someone needs to check for last frame, etc.
	float flNextCycle = GetCycle() + flInterval * GetSequenceCycleRate(GetSequence()) * GetPlaybackRate();

	Vector vecVelocity;
	pstudiohdr->Studio_SeqVelocity(GetSequence(), flNextCycle, GetPoseParameterArray(), vecVelocity);
	vecVelocity *= GetPlaybackRate();

	return vecVelocity.Length();
}

float CEngineObjectInternal::GetSequenceGroundSpeed(IStudioHdr* pStudioHdr, int iSequence)
{
	float t = SequenceDuration(pStudioHdr, iSequence);

	if (t > 0)
	{
		return (GetSequenceMoveDist(pStudioHdr, iSequence) / t) * m_flSpeedScale;
	}
	else
	{
		return 0;
	}
}

bool CEngineObjectInternal::GetPoseParameterRange(int index, float& minValue, float& maxValue)
{
	IStudioHdr* pStudioHdr = GetModelPtr();

	if (pStudioHdr)
	{
		if (index >= 0 && index < pStudioHdr->GetNumPoseParameters())
		{
			const mstudioposeparamdesc_t& pose = pStudioHdr->pPoseParameter(index);
			minValue = pose.start;
			maxValue = pose.end;
			return true;
		}
	}
	minValue = 0.0f;
	maxValue = 1.0f;
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CEngineObjectInternal::VPhysicsDestroyObject(void)
{
	if (m_pPhysicsObject && !m_ragdoll.listCount)
	{
		gEntList.PhysRemoveShadow(this->m_pOuter);
		PhysDestroyObject(m_pPhysicsObject, this->m_pOuter);
		m_pPhysicsObject = NULL;
	}
}

int CEngineObjectInternal::VPhysicsGetObjectList(IPhysicsObject** pList, int listMax)
{
	if (RagdollBoneCount()) {
		for (int i = 0; i < RagdollBoneCount(); i++)
		{
			if (i < listMax)
			{
				pList[i] = GetElement(i);
			}
		}

		return RagdollBoneCount();
	}

	IPhysicsObject* pPhys = VPhysicsGetObject();
	if (pPhys)
	{
		// multi-object entities must implement this function
		Assert(!(pPhys->GetGameFlags() & FVPHYSICS_MULTIOBJECT_ENTITY));
		if (listMax > 0)
		{
			pList[0] = pPhys;
			return 1;
		}
	}
	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPhysics - 
//-----------------------------------------------------------------------------
void CEngineObjectInternal::VPhysicsSetObject(IPhysicsObject* pPhysics)
{
	if (m_pPhysicsObject && pPhysics)
	{
		Warning("Overwriting physics object for %s\n", GetClassname());
	}
	m_pPhysicsObject = pPhysics;
	if (pPhysics && !m_pPhysicsObject)
	{
		CollisionRulesChanged();
	}
}

void CEngineObjectInternal::VPhysicsSwapObject(IPhysicsObject* pSwap)
{
	if (!pSwap)
	{
		gEntList.PhysRemoveShadow(this->m_pOuter);
	}

	if (!m_pPhysicsObject)
	{
		Warning("Bad vphysics swap for %s\n", STRING(GetClassname()));
	}
	m_pPhysicsObject = pSwap;
}

//-----------------------------------------------------------------------------
// Purpose: Init this object's physics as a static
//-----------------------------------------------------------------------------
IPhysicsObject* CEngineObjectInternal::VPhysicsInitStatic(void)
{
	if (!VPhysicsInitSetup())
		return NULL;

#ifndef CLIENT_DLL
	// If this entity has a move parent, it needs to be shadow, not static
	if (GetMoveParent())
	{
		// must be SOLID_VPHYSICS if in hierarchy to solve collisions correctly
		if (GetSolid() == SOLID_BSP && GetRootMoveParent()->GetSolid() != SOLID_BSP)
		{
			SetSolid(SOLID_VPHYSICS);
		}

		return VPhysicsInitShadow(false, false);
	}
#endif

	// No physics
	if (GetSolid() == SOLID_NONE)
		return NULL;

	// create a static physics objct
	IPhysicsObject* pPhysicsObject = NULL;
	if (GetSolid() == SOLID_BBOX)
	{
		pPhysicsObject = PhysModelCreateBox(this->m_pOuter, WorldAlignMins(), WorldAlignMaxs(), GetAbsOrigin(), true);
	}
	else
	{
		pPhysicsObject = PhysModelCreateUnmoveable(this->m_pOuter, GetModelIndex(), GetAbsOrigin(), GetAbsAngles());
	}
	VPhysicsSetObject(pPhysicsObject);
	return pPhysicsObject;
}

//-----------------------------------------------------------------------------
// Purpose: This creates a normal vphysics simulated object
//			physics alone determines where it goes (gravity, friction, etc)
//			and the entity receives updates from vphysics.  SetAbsOrigin(), etc do not affect the object!
//-----------------------------------------------------------------------------
IPhysicsObject* CEngineObjectInternal::VPhysicsInitNormal(SolidType_t solidType, int nSolidFlags, bool createAsleep, solid_t* pSolid)
{
	if (!VPhysicsInitSetup())
		return NULL;

	// NOTE: This has to occur before PhysModelCreate because that call will
	// call back into ShouldCollide(), which uses solidtype for rules.
	SetSolid(solidType);
	SetSolidFlags(nSolidFlags);

	// No physics
	if (solidType == SOLID_NONE)
	{
		return NULL;
	}

	// create a normal physics object
	IPhysicsObject* pPhysicsObject = PhysModelCreate(this->m_pOuter, GetModelIndex(), GetAbsOrigin(), GetAbsAngles(), pSolid);
	if (pPhysicsObject)
	{
		VPhysicsSetObject(pPhysicsObject);
		SetMoveType(MOVETYPE_VPHYSICS);

		if (!createAsleep)
		{
			pPhysicsObject->Wake();
		}
	}

	return pPhysicsObject;
}

// This creates a vphysics object with a shadow controller that follows the AI
IPhysicsObject* CEngineObjectInternal::VPhysicsInitShadow(bool allowPhysicsMovement, bool allowPhysicsRotation, solid_t* pSolid)
{
	if (!VPhysicsInitSetup())
		return NULL;

	// No physics
	if (GetSolid() == SOLID_NONE)
		return NULL;

	const Vector& origin = GetAbsOrigin();
	QAngle angles = GetAbsAngles();
	IPhysicsObject* pPhysicsObject = NULL;

	if (GetSolid() == SOLID_BBOX)
	{
		// adjust these so the game tracing epsilons match the physics minimum separation distance
		// this will shrink the vphysics version of the model by the difference in epsilons
		float radius = 0.25f - DIST_EPSILON;
		Vector mins = WorldAlignMins() + Vector(radius, radius, radius);
		Vector maxs = WorldAlignMaxs() - Vector(radius, radius, radius);
		pPhysicsObject = PhysModelCreateBox(this->m_pOuter, mins, maxs, origin, false);
		angles = vec3_angle;
	}
	else if (GetSolid() == SOLID_OBB)
	{
		pPhysicsObject = PhysModelCreateOBB(this->m_pOuter, OBBMins(), OBBMaxs(), origin, angles, false);
	}
	else
	{
		pPhysicsObject = PhysModelCreate(this->m_pOuter, GetModelIndex(), origin, angles, pSolid);
	}
	if (!pPhysicsObject)
		return NULL;

	VPhysicsSetObject(pPhysicsObject);
	// UNDONE: Tune these speeds!!!
	pPhysicsObject->SetShadow(1e4, 1e4, allowPhysicsMovement, allowPhysicsRotation);
	pPhysicsObject->UpdateShadow(origin, angles, false, 0);
	return pPhysicsObject;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::VPhysicsInitSetup()
{
#ifndef CLIENT_DLL
	// don't support logical ents
	if (entindex() == -1 || IsMarkedForDeletion())
		return false;
#endif

	// If this entity already has a physics object, then it should have been deleted prior to making this call.
	Assert(!m_pPhysicsObject);
	VPhysicsDestroyObject();

	// make sure absorigin / absangles are correct
	return true;
}

IPhysicsObject* CEngineObjectInternal::GetGroundVPhysics()
{
	CEngineObjectInternal* pGroundEntity = GetGroundEntity() ? GetGroundEntity() : NULL;
	;	if (pGroundEntity && pGroundEntity->GetMoveType() == MOVETYPE_VPHYSICS)
	{
		IPhysicsObject* pPhysGround = pGroundEntity->VPhysicsGetObject();
		if (pPhysGround && pPhysGround->IsMoveable())
			return pPhysGround;
	}
	return NULL;
}

// UNDONE: Look and see if the ground entity is in hierarchy with a MOVETYPE_VPHYSICS?
// Behavior in that case is not as good currently when the parent is rideable
bool CEngineObjectInternal::IsRideablePhysics(IPhysicsObject* pPhysics)
{
	if (pPhysics)
	{
		if (pPhysics->GetMass() > (VPhysicsGetObject()->GetMass() * 2))
			return true;
	}

	return false;
}

//=========================================================
// SelectWeightedSequence
//=========================================================
int CEngineObjectInternal::SelectWeightedSequence(int activity)
{
	Assert(activity != ACT_INVALID);
	Assert(GetModelPtr());
	return GetModelPtr()->SelectWeightedSequence(activity, GetSequence(), SharedRandomSelect);
}


int CEngineObjectInternal::SelectWeightedSequence(int activity, int curSequence)
{
	Assert(activity != ACT_INVALID);
	Assert(GetModelPtr());
	return GetModelPtr()->SelectWeightedSequence(activity, curSequence, SharedRandomSelect);
}

//=========================================================
// LookupHeaviestSequence
//
// Get sequence with highest 'weight' for this activity
//
//=========================================================
int CEngineObjectInternal::SelectHeaviestSequence(int activity)
{
	Assert(GetModelPtr());
	return GetModelPtr()->SelectHeaviestSequence(activity);
}

void CEngineObjectInternal::ClearRagdoll() {
	if (m_ragdoll.listCount) {
		for (int i = 0; i < m_ragdoll.listCount; i++)
		{
			if (m_ragdoll.list[i].pObject)
			{
				g_pPhysSaveRestoreManager->ForgetModel(m_ragdoll.list[i].pObject);
				m_ragdoll.list[i].pObject->EnableCollisions(false);
			}
		}

		// Set to null so that the destructor's call to DestroyObject won't destroy
		//  m_pObjects[ 0 ] twice since that's the physics object for the prop
		VPhysicsSetObject(NULL);

		RagdollDestroy(m_ragdoll);
	}
}

void CEngineObjectInternal::RagdollSolveSeparation(ragdoll_t& ragdoll, IHandleEntity* pEntity)
{
	byte needsFix[256];
	int fixCount = 0;
	Assert(ragdoll.listCount <= ARRAYSIZE(needsFix));
	for (int i = 0; i < ragdoll.listCount; i++)
	{
		needsFix[i] = 0;
		const ragdollelement_t& element = ragdoll.list[i];
		if (element.pConstraint && element.parentIndex >= 0)
		{
			Vector start, target;
			element.pObject->GetPosition(&start, NULL);
			ragdoll.list[element.parentIndex].pObject->LocalToWorld(&target, element.originParentSpace);
			if (needsFix[element.parentIndex])
			{
				needsFix[i] = 1;
				++fixCount;
				continue;
			}
			Vector dir = target - start;
			if (dir.LengthSqr() > 1.0f)
			{
				// this fixes a bug in ep2 with antlion grubs, but causes problems in TF2 - revisit, but disable for TF now
#if !defined(TF_CLIENT_DLL)
				// heuristic: guess that anything separated and small mass ratio is in some state that's 
				// keeping the solver from fixing it
				float mass = element.pObject->GetMass();
				float massParent = ragdoll.list[element.parentIndex].pObject->GetMass();

				if (mass * 2.0f < massParent)
				{
					// if this is <0.5 mass of parent and still separated it's attached to something heavy or 
					// in a bad state
					needsFix[i] = 1;
					++fixCount;
					continue;
				}
#endif

				if (PhysHasContactWithOtherInDirection(element.pObject, dir))
				{
					Ray_t ray;
					trace_t tr;
					ray.Init(target, start);
					UTIL_TraceRay(ray, MASK_SOLID, pEntity, COLLISION_GROUP_NONE, &tr);
					if (tr.DidHit())
					{
						needsFix[i] = 1;
						++fixCount;
					}
				}
			}
		}
	}

	if (fixCount)
	{
		for (int i = 0; i < ragdoll.listCount; i++)
		{
			if (!needsFix[i])
				continue;

			const ragdollelement_t& element = ragdoll.list[i];
			Vector target, velocity;
			ragdoll.list[element.parentIndex].pObject->LocalToWorld(&target, element.originParentSpace);
			ragdoll.list[element.parentIndex].pObject->GetVelocityAtPoint(target, &velocity);
			matrix3x4_t xform;
			element.pObject->GetPositionMatrix(&xform);
			MatrixSetColumn(target, 3, xform);
			element.pObject->SetPositionMatrix(xform, true);
			element.pObject->SetVelocity(&velocity, &vec3_origin);
		}
		DevMsg(2, "TICK:%5d:Ragdoll separation count: %d\n", gpGlobals->tickcount, fixCount);
	}
	else
	{
		ragdoll.pGroup->ClearErrorState();
	}
}

void CEngineObjectInternal::VPhysicsUpdate(IPhysicsObject* pPhysics)
{
	bool bIsRagdoll = false;
	for (int i = 0; i < m_ragdoll.listCount; i++)
	{
		if (m_ragdoll.list[0].pObject == pPhysics)
		{
			bIsRagdoll = true;
			break;
		}
	}
	if (!bIsRagdoll) {
		m_pOuter->VPhysicsUpdate(pPhysics);
		return;
	}
	if (pPhysics == VPhysicsGetObject()) {
		m_pOuter->VPhysicsUpdate(pPhysics);
	}
	if (m_lastUpdateTickCount == (unsigned int)gpGlobals->tickcount)
		return;

	m_lastUpdateTickCount = gpGlobals->tickcount;
	//NetworkStateChanged();

	matrix3x4_t boneToWorld[MAXSTUDIOBONES];
	QAngle angles;
	Vector surroundingMins, surroundingMaxs;

	int i;
	for (i = 0; i < m_ragdoll.listCount; i++)
	{
		CBoneAccessor boneaccessor(boneToWorld);
		if (RagdollGetBoneMatrix(m_ragdoll, boneaccessor, i))
		{
			Vector vNewPos;
			MatrixAngles(boneToWorld[m_ragdoll.boneIndex[i]], angles, vNewPos);
			m_ragPos.Set(i, vNewPos);
			m_ragAngles.Set(i, angles);
		}
		else
		{
			m_ragPos.GetForModify(i).Init();
			m_ragAngles.GetForModify(i).Init();
		}
	}

	// BUGBUG: Use the ragdollmins/maxs to do this instead of the collides
	m_allAsleep = RagdollIsAsleep(m_ragdoll);

	if (m_allAsleep)
	{

	}
	else
	{
		if (m_ragdoll.pGroup->IsInErrorState())
		{
			RagdollSolveSeparation(m_ragdoll, this->m_pOuter);
		}
	}

	Vector vecFullMins, vecFullMaxs;
	vecFullMins = m_ragPos[0];
	vecFullMaxs = m_ragPos[0];
	for (i = 0; i < m_ragdoll.listCount; i++)
	{
		Vector mins, maxs;
		matrix3x4_t update;
		if (!m_ragdoll.list[i].pObject)
		{
			m_ragdollMins[i].Init();
			m_ragdollMaxs[i].Init();
			continue;
		}
		m_ragdoll.list[i].pObject->GetPositionMatrix(&update);
		TransformAABB(update, m_ragdollMins[i], m_ragdollMaxs[i], mins, maxs);
		for (int j = 0; j < 3; j++)
		{
			if (mins[j] < vecFullMins[j])
			{
				vecFullMins[j] = mins[j];
			}
			if (maxs[j] > vecFullMaxs[j])
			{
				vecFullMaxs[j] = maxs[j];
			}
		}
	}

	SetAbsOrigin(m_ragPos[0]);
	SetAbsAngles(vec3_angle);
	const Vector& vecOrigin = GetCollisionOrigin();
	AddSolidFlags(FSOLID_FORCE_WORLD_ALIGNED);
	SetSurroundingBoundsType(USE_COLLISION_BOUNDS_NEVER_VPHYSICS);
	SetCollisionBounds(vecFullMins - vecOrigin, vecFullMaxs - vecOrigin);
	MarkSurroundingBoundsDirty();

	PhysicsTouchTriggers();

	return;
}

void CEngineObjectInternal::InitRagdoll(const Vector& forceVector, int forceBone, const Vector& forcePos, matrix3x4_t* pPrevBones, matrix3x4_t* pBoneToWorld, float dt, int collisionGroup, bool activateRagdoll, bool bWakeRagdoll)
{
	if (m_ragdoll.listCount) {
		return;
	}
	SetCollisionGroup(collisionGroup);
	SetMoveType(MOVETYPE_VPHYSICS);
	SetSolid(SOLID_VPHYSICS);
	AddSolidFlags(FSOLID_CUSTOMRAYTEST | FSOLID_CUSTOMBOXTEST);
	m_pOuter->m_takedamage = DAMAGE_EVENTS_ONLY;

	ragdollparams_t params;
	params.pGameData = static_cast<void*>(static_cast<CBaseEntity*>(this->m_pOuter));
	params.modelIndex = GetModelIndex();
	params.pCollide = modelinfo->GetVCollide(params.modelIndex);
	params.pStudioHdr = GetModelPtr();
	params.forceVector = forceVector;
	params.forceBoneIndex = forceBone;
	params.forcePosition = forcePos;
	params.pCurrentBones = pBoneToWorld;
	params.jointFrictionScale = 1.0;
	params.allowStretch = HasSpawnFlags(SF_RAGDOLLPROP_ALLOW_STRETCH);
	params.fixedConstraints = false;
	RagdollCreate(m_ragdoll, params, gEntList.PhysGetEnv());
	RagdollApplyAnimationAsVelocity(m_ragdoll, pPrevBones, pBoneToWorld, dt);
	if (m_anglesOverrideString != NULL_STRING && Q_strlen(m_anglesOverrideString.ToCStr()) > 0)
	{
		char szToken[2048];
		const char* pStr = nexttoken(szToken, STRING(m_anglesOverrideString), ',');
		// anglesOverride is index,angles,index,angles (e.g. "1, 22.5 123.0 0.0, 2, 0 0 0, 3, 0 0 180.0")
		while (szToken[0] != 0)
		{
			int objectIndex = atoi(szToken);
			// sanity check to make sure this token is an integer
			Assert(atof(szToken) == ((float)objectIndex));
			pStr = nexttoken(szToken, pStr, ',');
			Assert(szToken[0]);
			if (objectIndex >= m_ragdoll.listCount)
			{
				Warning("Bad ragdoll pose in entity %s, model (%s) at %s, model changed?\n", m_pOuter->GetDebugName(), GetModelName().ToCStr(), VecToString(GetAbsOrigin()));
			}
			else if (szToken[0] != 0)
			{
				QAngle angles;
				Assert(objectIndex >= 0 && objectIndex < RAGDOLL_MAX_ELEMENTS);
				UTIL_StringToVector(angles.Base(), szToken);
				int boneIndex = m_ragdoll.boneIndex[objectIndex];
				AngleMatrix(angles, pBoneToWorld[boneIndex]);
				const ragdollelement_t& element = m_ragdoll.list[objectIndex];
				Vector out;
				if (element.parentIndex >= 0)
				{
					int parentBoneIndex = m_ragdoll.boneIndex[element.parentIndex];
					VectorTransform(element.originParentSpace, pBoneToWorld[parentBoneIndex], out);
				}
				else
				{
					out = GetAbsOrigin();
				}
				MatrixSetColumn(out, 3, pBoneToWorld[boneIndex]);
				element.pObject->SetPositionMatrix(pBoneToWorld[boneIndex], true);
			}
			pStr = nexttoken(szToken, pStr, ',');
		}
	}

	if (activateRagdoll)
	{
		MEM_ALLOC_CREDIT();
		RagdollActivate(m_ragdoll, params.pCollide, GetModelIndex(), bWakeRagdoll);
	}

	for (int i = 0; i < m_ragdoll.listCount; i++)
	{
		UpdateNetworkDataFromVPhysics(i);
		g_pPhysSaveRestoreManager->AssociateModel(m_ragdoll.list[i].pObject, GetModelIndex());
		gEntList.PhysGetCollision()->CollideGetAABB(&m_ragdollMins[i], &m_ragdollMaxs[i], m_ragdoll.list[i].pObject->GetCollide(), vec3_origin, vec3_angle);
	}
	VPhysicsSetObject(m_ragdoll.list[0].pObject);

	CalcRagdollSize();

	for (int i = 0; i < GetModelPtr()->numbones(); i++)
	{
		SetIdentityMatrix(m_CachedBoneData[i]);
	}
	m_BoneAccessor.SetReadableBones(0);
	m_BoneAccessor.SetWritableBones(0);
	m_flLastBoneSetupTime = 0;
}

void CEngineObjectInternal::CalcRagdollSize(void)
{
	SetSurroundingBoundsType(USE_HITBOXES);
	RemoveSolidFlags(FSOLID_FORCE_WORLD_ALIGNED);
}

void CEngineObjectInternal::UpdateNetworkDataFromVPhysics(int index)
{
	Assert(index < m_ragdoll.listCount);

	QAngle angles;
	Vector vPos;
	m_ragdoll.list[index].pObject->GetPosition(&vPos, &angles);
	m_ragPos.Set(index, vPos);
	m_ragAngles.Set(index, angles);

	// move/relink if root moved
	if (index == 0)
	{
		SetAbsOrigin(m_ragPos[0]);
		PhysicsTouchTriggers();
	}
}

IPhysicsObject* CEngineObjectInternal::GetElement(int elementNum)
{
	return m_ragdoll.list[elementNum].pObject;
}

//-----------------------------------------------------------------------------
// Purpose: Force all the ragdoll's bone's physics objects to recheck their collision filters
//-----------------------------------------------------------------------------
void CEngineObjectInternal::RecheckCollisionFilter(void)
{
	for (int i = 0; i < m_ragdoll.listCount; i++)
	{
		m_ragdoll.list[i].pObject->RecheckCollisionFilter();
	}
}

void CEngineObjectInternal::GetAngleOverrideFromCurrentState(char* pOut, int size)
{
	pOut[0] = 0;
	for (int i = 0; i < m_ragdoll.listCount; i++)
	{
		if (i != 0)
		{
			Q_strncat(pOut, ",", size, COPY_ALL_CHARACTERS);

		}
		CFmtStr str("%d,%.2f %.2f %.2f", i, m_ragAngles[i].x, m_ragAngles[i].y, m_ragAngles[i].z);
		Q_strncat(pOut, str, size, COPY_ALL_CHARACTERS);
	}
}

void CEngineObjectInternal::RagdollBone(bool* boneSimulated, CBoneAccessor& pBoneToWorld)
{
	for (int i = 0; i < m_ragdoll.listCount; i++)
	{
		// during restore this may be NULL
		if (!GetElement(i))
			continue;

		if (RagdollGetBoneMatrix(m_ragdoll, pBoneToWorld, i))
		{
			boneSimulated[m_ragdoll.boneIndex[i]] = true;
		}
	}
}

bool CEngineObjectInternal::IsRagdoll() const
{
	if (GetFlags() & FL_TRANSRAGDOLL)
		return true;
	if (RagdollBoneCount()) {
		return true;
	}
	return false;
}

void CEngineObjectInternal::ActiveRagdoll()
{
	//RagdollActivate(*GetEngineObject()->GetRagdoll(), modelinfo->GetVCollide(GetEngineObject()->GetModelIndex()), GetEngineObject()->GetModelIndex());
	RagdollActivate(m_ragdoll, modelinfo->GetVCollide(GetModelIndex()), GetModelIndex());
}

void CEngineObjectInternal::ApplyAnimationAsVelocityToRagdoll(const matrix3x4_t* pPrevBones, const matrix3x4_t* pCurrentBones, float dt)
{
	RagdollApplyAnimationAsVelocity(m_ragdoll, pPrevBones, pCurrentBones, dt);
}

void CEngineObjectInternal::DrawRawSkeleton(matrix3x4_t boneToWorld[], int boneMask, bool noDepthTest, float duration, bool monocolor)
{
	IStudioHdr* pStudioHdr = GetModelPtr();
	if (!pStudioHdr)
		return;

	int i;
	int r = 255;
	int g = 255;
	int b = monocolor ? 255 : 0;


	for (i = 0; i < pStudioHdr->numbones(); i++)
	{
		if (pStudioHdr->pBone(i)->flags & boneMask)
		{
			Vector p1;
			MatrixPosition(boneToWorld[i], p1);
			if (pStudioHdr->pBone(i)->parent != -1)
			{
				Vector p2;
				MatrixPosition(boneToWorld[pStudioHdr->pBone(i)->parent], p2);
				NDebugOverlay::Line(p1, p2, r, g, b, noDepthTest, duration);
			}
		}
	}
}

void CEngineObjectInternal::SetupBones(matrix3x4_t* pBoneToWorldOut, int nMaxBones, int boneMask, float currentTime)
{
	AUTO_LOCK(m_BoneSetupMutex);

	VPROF_BUDGET("CBaseAnimating::SetupBones", VPROF_BUDGETGROUP_SERVER_ANIM);

	MDLCACHE_CRITICAL_SECTION();

	Assert(GetModelPtr());

	IStudioHdr* pStudioHdr = GetModelPtr();

	if (!pStudioHdr)
	{
		Assert(!"CBaseAnimating::GetSkeleton() without a model");
		return;
	}

	Assert(!IsEFlagSet(EFL_SETTING_UP_BONES));

	AddEFlags(EFL_SETTING_UP_BONES);

	// no ragdoll, fall through to base class 
	if (RagdollBoneCount())
	{
		// Not really ideal, but it'll work for now
		UpdateModelScale();

		bool sim[MAXSTUDIOBONES];
		memset(sim, 0, pStudioHdr->numbones());

		int i;

		RagdollBone(sim, m_BoneAccessor);

		mstudiobone_t* pbones = pStudioHdr->pBone(0);
		for (i = 0; i < pStudioHdr->numbones(); i++)
		{
			if (sim[i])
				continue;

			if (!(pStudioHdr->boneFlags(i) & boneMask))
				continue;

			matrix3x4_t matBoneLocal;
			AngleMatrix(pbones[i].rot, pbones[i].pos, matBoneLocal);
			ConcatTransforms(GetBone(pbones[i].parent), matBoneLocal, GetBoneForWrite(i));
		}
	}
	else {

		Vector pos[MAXSTUDIOBONES];
		Quaternion q[MAXSTUDIOBONES];

		// adjust hit boxes based on IK driven offset
		Vector adjOrigin = GetAbsOrigin() + Vector(0, 0, m_flEstIkOffset);

		if (m_pOuter->CanSkipAnimation())
		{
			IBoneSetup boneSetup(pStudioHdr, boneMask, GetPoseParameterArray());
			boneSetup.InitPose(pos, q);
			// Msg( "%.03f : %s:%s not in pvs\n", gpGlobals->curtime, GetClassname(), GetEntityName().ToCStr() );
		}
		else
		{
			if (m_pIk)
			{
				// FIXME: pass this into Studio_BuildMatrices to skip transforms
				CBoneBitList boneComputed;
				m_iIKCounter++;
				m_pIk->Init(pStudioHdr, GetAbsAngles(), adjOrigin, gpGlobals->curtime, m_iIKCounter, boneMask);
				m_pOuter->GetSkeleton(pStudioHdr, pos, q, boneMask);

				m_pIk->UpdateTargets(pos, q, m_BoneAccessor.GetBoneArrayForWrite(), boneComputed);
				m_pOuter->CalculateIKLocks(gpGlobals->curtime);
				m_pIk->SolveDependencies(pos, q, m_BoneAccessor.GetBoneArrayForWrite(), boneComputed);
			}
			else
			{
				// Msg( "%.03f : %s:%s\n", gpGlobals->curtime, GetClassname(), GetEntityName().ToCStr() );
				m_pOuter->GetSkeleton(pStudioHdr, pos, q, boneMask);
			}
		}

		CEngineObjectInternal* pParent = GetMoveParent();
		if (pParent && pParent->GetModelPtr())
		{
			IStudioHdr* fhdr = pParent->GetModelPtr();
			mstudiobone_t* pbones = pStudioHdr->pBone(0);

			matrix3x4_t rotationmatrix; // model to world transformation
			AngleMatrix(GetAbsAngles(), adjOrigin, rotationmatrix);

			for (int i = 0; i < pStudioHdr->numbones(); i++)
			{
				// Now find the bone in the parent entity.
				bool merged = false;
				int parentBoneIndex = fhdr->Studio_BoneIndexByName(pbones[i].pszName());
				if (parentBoneIndex >= 0)
				{
					const matrix3x4_t& pMat = pParent->GetBone(parentBoneIndex);
					//if (pMat)
					{
						MatrixCopy(pMat, GetBoneForWrite(i));
						merged = true;
					}
				}

				if (!merged)
				{
					// If we get down here, then the bone wasn't merged.
					matrix3x4_t bonematrix;
					QuaternionMatrix(q[i], pos[i], bonematrix);

					if (pbones[i].parent == -1)
					{
						ConcatTransforms(rotationmatrix, bonematrix, GetBoneForWrite(i));
					}
					else
					{
						ConcatTransforms(GetBone(pbones[i].parent), bonematrix, GetBoneForWrite(i));
					}
				}
			}
		}
		else {

			int i, j;
			int					chain[MAXSTUDIOBONES] = {};
			int					chainlength = pStudioHdr->numbones();
			for (i = 0; i < pStudioHdr->numbones(); i++)
			{
				chain[chainlength - i - 1] = i;
			}

			matrix3x4_t bonematrix;
			matrix3x4_t rotationmatrix; // model to world transformation
			AngleMatrix(GetAbsAngles(), adjOrigin, rotationmatrix);

			// Account for a change in scale
			if (GetModelScale() < 1.0f - FLT_EPSILON || GetModelScale() > 1.0f + FLT_EPSILON)
			{
				Vector vecOffset;
				MatrixGetColumn(rotationmatrix, 3, vecOffset);
				vecOffset -= adjOrigin;
				vecOffset *= GetModelScale();
				vecOffset += adjOrigin;
				MatrixSetColumn(vecOffset, 3, rotationmatrix);

				// Scale it uniformly
				VectorScale(rotationmatrix[0], GetModelScale(), rotationmatrix[0]);
				VectorScale(rotationmatrix[1], GetModelScale(), rotationmatrix[1]);
				VectorScale(rotationmatrix[2], GetModelScale(), rotationmatrix[2]);
			}

			for (j = chainlength - 1; j >= 0; j--)
			{
				i = chain[j];
				if (pStudioHdr->boneFlags(i) & boneMask)
				{
					QuaternionMatrix(q[i], pos[i], bonematrix);

					if (pStudioHdr->boneParent(i) == -1)
					{
						ConcatTransforms(rotationmatrix, bonematrix, GetBoneForWrite(i));
					}
					else
					{
						ConcatTransforms(GetBone(pStudioHdr->boneParent(i)), bonematrix, GetBoneForWrite(i));
					}
				}
			}
		}
	}

	if (ai_setupbones_debug.GetBool())
	{
		// Msg("%s:%s:%s (%x)\n", GetClassname(), GetDebugName(), STRING(GetModelName()), boneMask );
		DrawRawSkeleton(m_BoneAccessor.GetBoneArrayForWrite(), boneMask, true, 0.11);
	}
	RemoveEFlags(EFL_SETTING_UP_BONES);
	m_BoneAccessor.SetReadableBones(boneMask);
	m_flLastBoneSetupTime = currentTime;
		// Do they want to get at the bone transforms? If it's just making sure an aiment has 
	// its bones setup, it doesn't need the transforms yet.
	if (pBoneToWorldOut)
	{
		if (nMaxBones >= m_CachedBoneData.Count())
		{
			memcpy(pBoneToWorldOut, m_CachedBoneData.Base(), sizeof(matrix3x4_t) * m_CachedBoneData.Count());
		}
		else
		{
			Warning("SetupBones: invalid bone array size (%d - needs %d)\n", nMaxBones, m_CachedBoneData.Count());
			return;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: return the index to the shared bone cache
// Output :
//-----------------------------------------------------------------------------
void CEngineObjectInternal::GetBoneCache(void)
{
	IStudioHdr* pStudioHdr = GetModelPtr();
	Assert(pStudioHdr);

	//CBoneCache* pcache = Studio_GetBoneCache(m_boneCacheHandle);
	int boneMask = BONE_USED_BY_HITBOX | BONE_USED_BY_ATTACHMENT;

	// TF queries these bones to position weapons when players are killed
#if defined( TF_DLL )
	boneMask |= BONE_USED_BY_BONE_MERGE;
#endif
	//if (pcache)
	//{
		if (gpGlobals->curtime <= m_flLastBoneSetupTime && (m_BoneAccessor.GetReadableBones() & boneMask) == boneMask)
		{
			// Msg("%s:%s:%s (%x:%x:%8.4f) cache\n", GetClassname(), GetDebugName(), STRING(GetModelName()), boneMask, pcache->m_boneMask, pcache->m_timeValid );
			// in memory and still valid, use it!
			return;
		}
		// in memory, but missing some of the bone masks
		//if ((pcache->m_boneMask & boneMask) != boneMask)
		//{
			//Studio_DestroyBoneCache(m_boneCacheHandle);
			//m_boneCacheHandle = 0;
			//pcache = NULL;
		//}
	//}

	SetupBones(NULL, -1, boneMask, gpGlobals->curtime);

	//if (pcache)
	//{
		// still in memory but out of date, refresh the bones.
	//	pcache->UpdateBones(bonetoworld, pStudioHdr->numbones(), gpGlobals->curtime);
	//}
	//else
	//{
	//	bonecacheparams_t params;
	//	params.pStudioHdr = pStudioHdr;
	//	params.pBoneToWorld = bonetoworld;
	//	params.curtime = gpGlobals->curtime;
	//	params.boneMask = boneMask;

	//	m_boneCacheHandle = Studio_CreateBoneCache(params);
	//	pcache = Studio_GetBoneCache(m_boneCacheHandle);
	//}
	//Assert(pcache);
	//return pcache;
}


void CEngineObjectInternal::InvalidateBoneCache(void)
{
	m_BoneAccessor.SetReadableBones(0);
}

void CEngineObjectInternal::InvalidateBoneCacheIfOlderThan(float deltaTime)
{
	if (gpGlobals->curtime - m_flLastBoneSetupTime <=  deltaTime)
	{
		InvalidateBoneCache();
	}
}

//=========================================================
//=========================================================

void CEngineObjectInternal::GetHitboxBoneTransform(int iBone, matrix3x4_t& pBoneToWorld)
{
	IStudioHdr* pStudioHdr = GetModelPtr();

	if (!pStudioHdr)
	{
		Assert(!"CBaseAnimating::GetHitboxBoneTransform: model missing");
		return;
	}

	if (iBone < 0 || iBone >= pStudioHdr->numbones())
	{
		Assert(!"CBaseAnimating::GetHitboxBoneTransform: invalid bone index");
		return;
	}

	GetBoneCache();

	const matrix3x4_t& pmatrix = GetBone(iBone);

	//if (!pmatrix)
	//{
	//	MatrixCopy(EntityToWorldTransform(), pBoneToWorld);
	//	return;
	//}

	//Assert(pmatrix);

	// FIXME
	MatrixCopy(pmatrix, pBoneToWorld);
}

void CEngineObjectInternal::GetHitboxBoneTransforms(const matrix3x4_t* hitboxbones[MAXSTUDIOBONES])
{
	IStudioHdr* pStudioHdr = GetModelPtr();

	if (!pStudioHdr)
	{
		Assert(!"CBaseAnimating::GetHitboxBoneTransform: model missing");
		return;
	}

	GetBoneCache();

	memset(hitboxbones, 0, sizeof(matrix3x4_t*) * MAXSTUDIOBONES);
	for (int i = 0; i < MAXSTUDIOBONES; i++)
	{
		hitboxbones[i] = &m_BoneAccessor.GetBone(i);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Returns index number of a given named bone
// Input  : name of a bone
// Output :	Bone index number or -1 if bone not found
//-----------------------------------------------------------------------------
int CEngineObjectInternal::LookupBone(const char* szName)
{
	const IStudioHdr* pStudioHdr = GetModelPtr();
	Assert(pStudioHdr);
	if (!pStudioHdr)
		return -1;
	return pStudioHdr->Studio_BoneIndexByName(szName);
}


//=========================================================
//=========================================================
void CEngineObjectInternal::GetHitboxBonePosition(int iBone, Vector& origin, QAngle& angles)
{
	IStudioHdr* pStudioHdr = GetModelPtr();
	if (!pStudioHdr)
	{
		Assert(!"CBaseAnimating::GetHitboxBonePosition: model missing");
		return;
	}

	if (iBone < 0 || iBone >= pStudioHdr->numbones())
	{
		Assert(!"CBaseAnimating::GetHitboxBonePosition: invalid bone index");
		return;
	}

	matrix3x4_t bonetoworld;
	GetHitboxBoneTransform(iBone, bonetoworld);

	MatrixAngles(bonetoworld, angles, origin);
}

int CEngineObjectInternal::GetPhysicsBone(int boneIndex)
{
	IStudioHdr* pStudioHdr = GetModelPtr();
	if (pStudioHdr)
	{
		if (boneIndex >= 0 && boneIndex < pStudioHdr->numbones())
			return pStudioHdr->pBone(boneIndex)->physicsbone;
	}
	return 0;
}

void CEngineObjectInternal::EnableServerIK()
{
	if (!m_pIk)
	{
		m_pIk = new CIKContext;
		m_iIKCounter = 0;
	}
}

void CEngineObjectInternal::DisableServerIK()
{
	delete m_pIk;
	m_pIk = NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Receives the clients IK floor position
//-----------------------------------------------------------------------------

void CEngineObjectInternal::SetIKGroundContactInfo(float minHeight, float maxHeight)
{
	m_flIKGroundContactTime = gpGlobals->curtime;
	m_flIKGroundMinHeight = minHeight;
	m_flIKGroundMaxHeight = maxHeight;
}

//-----------------------------------------------------------------------------
// Purpose: Initializes IK floor position
//-----------------------------------------------------------------------------

void CEngineObjectInternal::InitStepHeightAdjust(void)
{
	m_flIKGroundContactTime = 0;
	m_flIKGroundMinHeight = 0;
	m_flIKGroundMaxHeight = 0;

	// FIXME: not safe to call GetAbsOrigin here. Hierarchy might not be set up!
	m_flEstIkFloor = GetAbsOrigin().z;
	m_flEstIkOffset = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Interpolates client IK floor position and drops entity down so that the feet will reach
//-----------------------------------------------------------------------------

ConVar npc_height_adjust("npc_height_adjust", "1", FCVAR_ARCHIVE, "Enable test mode for ik height adjustment");

void CEngineObjectInternal::UpdateStepOrigin()
{
	if (!npc_height_adjust.GetBool())
	{
		m_flEstIkOffset = 0;
		m_flEstIkFloor = GetLocalOrigin().z;
		return;
	}

	/*
	if (m_debugOverlays & OVERLAY_NPC_SELECTED_BIT)
	{
		Msg("%x : %x\n", GetMoveParent(), GetGroundEntity() );
	}
	*/

	if (m_flIKGroundContactTime > 0.2 && m_flIKGroundContactTime > gpGlobals->curtime - 0.2)
	{
		if ((GetFlags() & (FL_FLY | FL_SWIM)) == 0 && GetMoveParent() == NULL && GetGroundEntity() != NULL && !GetGroundEntity()->GetOuter()->IsMoving())
		{
			Vector toAbs = GetAbsOrigin() - GetLocalOrigin();
			if (toAbs.z == 0.0)
			{
				// FIXME:  There needs to be a default step height somewhere
				float height = 18.0f;
				if (m_pOuter->IsNPC())
				{
					height = m_pOuter->GetStepHeight();
				}

				// debounce floor location
				m_flEstIkFloor = m_flEstIkFloor * 0.2 + m_flIKGroundMinHeight * 0.8;

				// don't let heigth difference between min and max exceed step height
				float bias = clamp((m_flIKGroundMaxHeight - m_flIKGroundMinHeight) - height, 0.f, height);
				// save off reasonable offset
				m_flEstIkOffset = clamp(m_flEstIkFloor - GetAbsOrigin().z, -height + bias, 0.0f);
				return;
			}
		}
	}

	// don't use floor offset, decay the value
	m_flEstIkOffset *= 0.5;
	m_flEstIkFloor = GetLocalOrigin().z;
}

// gets the bone for an attachment
int CEngineObjectInternal::GetAttachmentBone( int iAttachment )
{
	IStudioHdr *pStudioHdr = GetModelPtr( );
	if (!pStudioHdr || iAttachment < 1 || iAttachment > pStudioHdr->GetNumAttachments() )
	{
		AssertOnce(pStudioHdr && "CBaseAnimating::GetAttachment: model missing");
		return 0;
	}

	return pStudioHdr->GetAttachmentBone( iAttachment-1 );
}

//-----------------------------------------------------------------------------
// Purpose: Returns index number of a given named attachment
// Input  : name of attachment
// Output :	attachment index number or -1 if attachment not found
//-----------------------------------------------------------------------------
int CEngineObjectInternal::LookupAttachment(const char* szName)
{
	IStudioHdr* pStudioHdr = GetModelPtr();
	if (!pStudioHdr)
	{
		Assert(!"CBaseAnimating::LookupAttachment: model missing");
		return 0;
	}

	// The +1 is to make attachment indices be 1-based (namely 0 == invalid or unused attachment)
	return pStudioHdr->Studio_FindAttachment(szName) + 1;
}

//-----------------------------------------------------------------------------
// Purpose: Returns the world location and world angles of an attachment
// Input  : attachment index
// Output :	location and angles
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::GetAttachment(int iAttachment, matrix3x4_t& attachmentToWorld)
{
	IStudioHdr* pStudioHdr = GetModelPtr();
	if (!pStudioHdr)
	{
		MatrixCopy(EntityToWorldTransform(), attachmentToWorld);
		AssertOnce(!"CBaseAnimating::GetAttachment: model missing");
		return false;
	}

	if (iAttachment < 1 || iAttachment > pStudioHdr->GetNumAttachments())
	{
		MatrixCopy(EntityToWorldTransform(), attachmentToWorld);
		//		Assert(!"CBaseAnimating::GetAttachment: invalid attachment index");
		return false;
	}

	const mstudioattachment_t& pattachment = pStudioHdr->pAttachment(iAttachment - 1);
	int iBone = pStudioHdr->GetAttachmentBone(iAttachment - 1);

	matrix3x4_t bonetoworld;
	GetHitboxBoneTransform(iBone, bonetoworld);
	if ((pattachment.flags & ATTACHMENT_FLAG_WORLD_ALIGN) == 0)
	{
		ConcatTransforms(bonetoworld, pattachment.local, attachmentToWorld);
	}
	else
	{
		Vector vecLocalBonePos, vecWorldBonePos;
		MatrixGetColumn(pattachment.local, 3, vecLocalBonePos);
		VectorTransform(vecLocalBonePos, bonetoworld, vecWorldBonePos);

		SetIdentityMatrix(attachmentToWorld);
		MatrixSetColumn(vecWorldBonePos, 3, attachmentToWorld);
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Returns the world location and world angles of an attachment
// Input  : attachment index
// Output :	location and angles
//-----------------------------------------------------------------------------
bool CEngineObjectInternal::GetAttachment(int iAttachment, Vector& absOrigin, QAngle& absAngles)
{
	matrix3x4_t attachmentToWorld;

	bool bRet = GetAttachment(iAttachment, attachmentToWorld);
	MatrixAngles(attachmentToWorld, absAngles, absOrigin);
	return bRet;
}

//------------------------------------------------------------------------------
void CEngineObjectInternal::IncrementInterpolationFrame()
{
	m_ubInterpolationFrame = (m_ubInterpolationFrame + 1) % NOINTERP_PARITY_MAX;
}

bool CEngineObjectInternal::PhysModelParseSolid(solid_t& solid) 
{
	return ::PhysModelParseSolid(solid, m_pOuter, GetModelIndex());
}

bool CEngineObjectInternal::PhysModelParseSolidByIndex(solid_t& solid, int solidIndex)
{
	return ::PhysModelParseSolidByIndex(solid, m_pOuter, GetModelIndex(), solidIndex);
}

void CEngineObjectInternal::PhysForceClearVelocity(IPhysicsObject* pPhys)
{
	::PhysForceClearVelocity(pPhys);
}

CEngineObjectInternal* CEngineObjectInternal::GetClonesOfEntity() const
{
	if (gEntList.m_EntityClones[this->entindex()]) {
		return gEntList.m_EntityClones[this->entindex()]->pClone;
	}
	return NULL;
}

IEnginePortalServer* CEngineObjectInternal::GetSimulatorThatOwnsEntity()
{
	if (!this->m_pOuter->IsNetworkable() || this->entindex() == -1) {
		return NULL;
	}
#ifdef _DEBUG
	int iEntIndex = this->entindex();
	CEnginePortalInternal* pOwningSimulatorCheck = NULL;

	for (int i = gEntList.m_ActivePortals.Count(); --i >= 0; )
	{
		if (gEntList.m_ActivePortals[i]->m_EntFlags[iEntIndex] & PSEF_OWNS_ENTITY)
		{
			AssertMsg(pOwningSimulatorCheck == NULL, "More than one portal simulator found owning the same entity.");
			pOwningSimulatorCheck = gEntList.m_ActivePortals[i];
		}
	}

	AssertMsg(pOwningSimulatorCheck == gEntList.m_OwnedEntityMap[iEntIndex], "Owned entity mapping out of sync with individual simulator ownership flags.");
#endif

	return gEntList.m_OwnedEntityMap[this->entindex()];
}

bool CEngineObjectInternal::EntityIsParentOf(IEngineObjectServer* pEntity)
{
	while (pEntity->GetMoveParent())
	{
		pEntity = pEntity->GetMoveParent();
		if (this == pEntity)
			return true;
	}

	return false;
}

static void GetAllChildren_r(IEngineObjectServer* pEntity, CUtlVector<IEngineObjectServer*>& list)
{
	for (; pEntity != NULL; pEntity = pEntity->NextMovePeer())
	{
		list.AddToTail(pEntity);
		GetAllChildren_r(pEntity->FirstMoveChild(), list);
	}
}

int CEngineObjectInternal::GetAllChildren(CUtlVector<IEngineObjectServer*>& list)
{
	GetAllChildren_r(this->FirstMoveChild(), list);
	return list.Count();
}

int	CEngineObjectInternal::GetAllInHierarchy(CUtlVector<IEngineObjectServer*>& list)
{
	list.AddToTail(this);
	return GetAllChildren(list) + 1;
}

BEGIN_SEND_TABLE(CEnginePlayerInternal, DT_EnginePlayer)
	SendPropEHandle(SENDINFO(m_hPortalEnvironment)),
	SendPropEHandle(SENDINFO(m_pHeldObjectPortal)),
	SendPropBool(SENDINFO(m_bHeldObjectOnOppositeSideOfPortal)),
END_SEND_TABLE()

BEGIN_DATADESC(CEnginePlayerInternal)
	DEFINE_FIELD(m_hPortalEnvironment, FIELD_EHANDLE),
	DEFINE_FIELD(m_pHeldObjectPortal, FIELD_EHANDLE),
	DEFINE_FIELD(m_bHeldObjectOnOppositeSideOfPortal, FIELD_BOOLEAN),
	DEFINE_FIELD(m_bSilentDropAndPickup, FIELD_BOOLEAN),
END_DATADESC()

IMPLEMENT_SERVERCLASS(CEnginePlayerInternal, DT_EnginePlayer)

CEnginePlayerInternal::CEnginePlayerInternal(IServerEntityList* pServerEntityList, int iForceEdictIndex, int iSerialNum)
	:CEngineObjectInternal(pServerEntityList, iForceEdictIndex, iSerialNum)
{
	m_hPortalEnvironment = NULL;
	m_pHeldObjectPortal = NULL;
	m_bHeldObjectOnOppositeSideOfPortal = false;
	m_bSilentDropAndPickup = false;
	gEntList.m_ActivePlayers.AddToTail(this);
}

CEnginePlayerInternal::~CEnginePlayerInternal() 
{
	gEntList.m_ActivePlayers.FindAndRemove(this);
}

void CEnginePlayerInternal::VPhysicsDestroyObject()
{
	// Since CBasePlayer aliases its pointer to the physics object, tell CBaseEntity to 
	// clear out its physics object pointer so we don't wind up deleting one of
	// the aliased objects twice.
	VPhysicsSetObject(NULL);

	gEntList.PhysRemoveShadow(this->m_pOuter);

	if (m_pPhysicsController)
	{
		gEntList.PhysGetEnv()->DestroyPlayerController(m_pPhysicsController);
		m_pPhysicsController = NULL;
	}

	if (m_pShadowStand)
	{
		m_pShadowStand->EnableCollisions(false);
		PhysDestroyObject(m_pShadowStand);
		m_pShadowStand = NULL;
	}
	if (m_pShadowCrouch)
	{
		m_pShadowCrouch->EnableCollisions(false);
		PhysDestroyObject(m_pShadowCrouch);
		m_pShadowCrouch = NULL;
	}

	CEngineObjectInternal::VPhysicsDestroyObject();
}

void CEnginePlayerInternal::SetupVPhysicsShadow(const Vector& vHullMin, const Vector& vHullMax, const Vector& vDuckHullMin, const Vector& vDuckHullMax)
{
	solid_t solid;
	Q_strncpy(solid.surfaceprop, "player", sizeof(solid.surfaceprop));
	solid.params = gEntList.PhysGetDefaultObjectParams();
	solid.params.mass = 85.0f;
	solid.params.inertia = 1e24f;
	solid.params.enableCollisions = false;
	//disable drag
	solid.params.dragCoefficient = 0;
	// create standing hull
	CPhysCollide* pStandModel = PhysCreateBbox(vHullMin, vHullMax);
	m_pShadowStand = PhysModelCreateCustom(this->m_pOuter, pStandModel, GetLocalOrigin(), GetLocalAngles(), "player_stand", false, &solid);
	m_pShadowStand->SetCallbackFlags(CALLBACK_GLOBAL_COLLISION | CALLBACK_SHADOW_COLLISION);

	// create crouchig hull
	CPhysCollide* pCrouchModel = PhysCreateBbox(vDuckHullMin, vDuckHullMax);
	m_pShadowCrouch = PhysModelCreateCustom(this->m_pOuter, pCrouchModel, GetLocalOrigin(), GetLocalAngles(), "player_crouch", false, &solid);
	m_pShadowCrouch->SetCallbackFlags(CALLBACK_GLOBAL_COLLISION | CALLBACK_SHADOW_COLLISION);

	// default to stand
	VPhysicsSetObject(m_pShadowStand);

	// tell physics lists I'm a shadow controller object
	gEntList.PhysAddShadow(this->m_pOuter);
	m_pPhysicsController = gEntList.PhysGetEnv()->CreatePlayerController(m_pShadowStand);
	m_pPhysicsController->SetPushMassLimit(350.0f);
	m_pPhysicsController->SetPushSpeedLimit(50.0f);

	// Give the controller a valid position so it doesn't do anything rash.
	UpdatePhysicsShadowToPosition(m_vecAbsOrigin);

	// init state
	if (GetFlags() & FL_DUCKING)
	{
		SetVCollisionState(m_vecAbsOrigin, m_vecAbsVelocity, VPHYS_CROUCH);
	}
	else
	{
		SetVCollisionState(m_vecAbsOrigin, m_vecAbsVelocity, VPHYS_WALK);
	}
}

void CEnginePlayerInternal::UpdatePhysicsShadowToPosition(const Vector& vecAbsOrigin)
{
	UpdateVPhysicsPosition(vecAbsOrigin, vec3_origin, gpGlobals->frametime);
}

void CEnginePlayerInternal::UpdateVPhysicsPosition(const Vector& position, const Vector& velocity, float secondsToArrival)
{
	bool onground = (GetFlags() & FL_ONGROUND) ? true : false;
	IPhysicsObject* pPhysGround = GetGroundVPhysics();

	// if the object is much heavier than the player, treat it as a local coordinate system
	// the player controller will solve movement differently in this case.
	if (!IsRideablePhysics(pPhysGround))
	{
		pPhysGround = NULL;
	}

	m_pPhysicsController->Update(position, velocity, secondsToArrival, onground, pPhysGround);
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEnginePlayerInternal::SetVCollisionState(const Vector& vecAbsOrigin, const Vector& vecAbsVelocity, int collisionState)
{
	m_vphysicsCollisionState = collisionState;
	switch (collisionState)
	{
	case VPHYS_WALK:
		m_pShadowStand->SetPosition(vecAbsOrigin, vec3_angle, true);
		m_pShadowStand->SetVelocity(&vecAbsVelocity, NULL);
		m_pShadowCrouch->EnableCollisions(false);
		m_pPhysicsController->SetObject(m_pShadowStand);
		VPhysicsSwapObject(m_pShadowStand);
		m_pShadowStand->EnableCollisions(true);
		break;

	case VPHYS_CROUCH:
		m_pShadowCrouch->SetPosition(vecAbsOrigin, vec3_angle, true);
		m_pShadowCrouch->SetVelocity(&vecAbsVelocity, NULL);
		m_pShadowStand->EnableCollisions(false);
		m_pPhysicsController->SetObject(m_pShadowCrouch);
		VPhysicsSwapObject(m_pShadowCrouch);
		m_pShadowCrouch->EnableCollisions(true);
		break;

	case VPHYS_NOCLIP:
		m_pShadowCrouch->EnableCollisions(false);
		m_pShadowStand->EnableCollisions(false);
		break;
	}
}

BEGIN_SEND_TABLE(CEnginePortalInternal, DT_EnginePortal)
	SendPropEHandle(SENDINFO(m_hLinkedPortal)),
	SendPropBool(SENDINFO(m_bActivated)),
	SendPropBool(SENDINFO(m_bIsPortal2)),
END_SEND_TABLE()

BEGIN_DATADESC(CEnginePortalInternal)
	DEFINE_FIELD(m_hLinkedPortal, FIELD_EHANDLE),
	DEFINE_KEYFIELD(m_bActivated, FIELD_BOOLEAN, "Activated"),
	DEFINE_KEYFIELD(m_bIsPortal2, FIELD_BOOLEAN, "PortalTwo"),
	DEFINE_ARRAY(m_vPortalCorners, FIELD_POSITION_VECTOR, 4),
END_DATADESC()

IMPLEMENT_SERVERCLASS(CEnginePortalInternal, DT_EnginePortal)

CEnginePortalInternal::CEnginePortalInternal(IServerEntityList* pServerEntityList, int iForceEdictIndex, int iSerialNum)
: CEngineObjectInternal(pServerEntityList, iForceEdictIndex, iSerialNum), m_DataAccess(m_InternalData), m_bSimulateVPhysics(true), m_bLocalDataIsReady(false)
{
	static int s_iPortalSimulatorGUIDAllocator = 0;
	m_iPortalSimulatorGUID = s_iPortalSimulatorGUIDAllocator++;
	memset(m_EntFlags, 0, sizeof(m_EntFlags));
	m_bActivated = false;
	m_bIsPortal2 = false;
	// Init to something safe
	for (int i = 0; i < 4; ++i)
	{
		m_vPortalCorners[i] = Vector(0, 0, 0);
	}
	gEntList.m_ActivePortals.AddToTail(this);
}

CEnginePortalInternal::~CEnginePortalInternal()
{
	gEntList.m_ActivePortals.FindAndRemove(this); //also removed in UpdateOnRemove()	
}

IPhysicsObject* CEnginePortalInternal::VPhysicsGetObject(void) const
{
	if (GetWorldBrushesPhysicsObject() != NULL)
		return GetWorldBrushesPhysicsObject();
	else if (GetWallBrushesPhysicsObject() != NULL)
		return GetWallBrushesPhysicsObject();
	else if (GetWallTubePhysicsObject() != NULL)
		return GetWallTubePhysicsObject();
	else if (GetRemoteWallBrushesPhysicsObject() != NULL)
		return GetRemoteWallBrushesPhysicsObject();
	else
		return NULL;
}

int CEnginePortalInternal::VPhysicsGetObjectList(IPhysicsObject** pList, int listMax)
{
	if ((pList == NULL) || (listMax == 0))
		return 0;

	int iRetVal = 0;

	if (GetWorldBrushesPhysicsObject() != NULL)
	{
		pList[iRetVal] = GetWorldBrushesPhysicsObject();
		++iRetVal;
		if (iRetVal == listMax)
			return iRetVal;
	}

	if (GetWallBrushesPhysicsObject() != NULL)
	{
		pList[iRetVal] = GetWallBrushesPhysicsObject();
		++iRetVal;
		if (iRetVal == listMax)
			return iRetVal;
	}

	if (GetWallTubePhysicsObject() != NULL)
	{
		pList[iRetVal] = GetWallTubePhysicsObject();
		++iRetVal;
		if (iRetVal == listMax)
			return iRetVal;
	}

	if (GetRemoteWallBrushesPhysicsObject() != NULL)
	{
		pList[iRetVal] = GetRemoteWallBrushesPhysicsObject();
		++iRetVal;
		if (iRetVal == listMax)
			return iRetVal;
	}

	return iRetVal;
}

void CEnginePortalInternal::VPhysicsDestroyObject(void)
{
	VPhysicsSetObject(NULL);
}

void CEnginePortalInternal::OnRestore(void)
{
	BaseClass::OnRestore();
}

void CEnginePortalInternal::SetVPhysicsSimulationEnabled(bool bEnabled)
{
	m_bSimulateVPhysics = bEnabled;
}

bool CEnginePortalInternal::IsSimulatingVPhysics(void) const
{
	return m_bSimulateVPhysics;
}

void CEnginePortalInternal::MoveTo(const Vector& ptCenter, const QAngle& angles)
{
	{
		SetAbsOrigin(ptCenter);
		SetAbsAngles(angles);
		//m_InternalData.Placement.ptCenter = ptCenter;
		//m_InternalData.Placement.qAngles = angles;
		AngleVectors(angles, &m_InternalData.Placement.vForward, &m_InternalData.Placement.vRight, &m_InternalData.Placement.vUp);
		m_InternalData.Placement.PortalPlane.normal = m_InternalData.Placement.vForward;
		m_InternalData.Placement.PortalPlane.dist = m_InternalData.Placement.PortalPlane.normal.Dot(GetAbsOrigin());
		m_InternalData.Placement.PortalPlane.signbits = SignbitsForPlane(&m_InternalData.Placement.PortalPlane);
		//m_InternalData.Placement.PortalPlane.Init(m_InternalData.Placement.vForward, m_InternalData.Placement.vForward.Dot(GetEngineObject()->GetAbsOrigin()));
		Vector vAbsNormal;
		vAbsNormal.x = fabs(m_InternalData.Placement.PortalPlane.normal.x);
		vAbsNormal.y = fabs(m_InternalData.Placement.PortalPlane.normal.y);
		vAbsNormal.z = fabs(m_InternalData.Placement.PortalPlane.normal.z);

		if (vAbsNormal.x > vAbsNormal.y)
		{
			if (vAbsNormal.x > vAbsNormal.z)
			{
				if (vAbsNormal.x > 0.999f)
					m_InternalData.Placement.PortalPlane.type = PLANE_X;
				else
					m_InternalData.Placement.PortalPlane.type = PLANE_ANYX;
			}
			else
			{
				if (vAbsNormal.z > 0.999f)
					m_InternalData.Placement.PortalPlane.type = PLANE_Z;
				else
					m_InternalData.Placement.PortalPlane.type = PLANE_ANYZ;
			}
		}
		else
		{
			if (vAbsNormal.y > vAbsNormal.z)
			{
				if (vAbsNormal.y > 0.999f)
					m_InternalData.Placement.PortalPlane.type = PLANE_Y;
				else
					m_InternalData.Placement.PortalPlane.type = PLANE_ANYY;
			}
			else
			{
				if (vAbsNormal.z > 0.999f)
					m_InternalData.Placement.PortalPlane.type = PLANE_Z;
				else
					m_InternalData.Placement.PortalPlane.type = PLANE_ANYZ;
			}
		}
	}
}

void CEnginePortalInternal::AttachTo(IEnginePortalServer* pLinkedPortal) 
{
	m_hLinkedPortal = pLinkedPortal->AsEngineObject()->GetOuter();
	GetLinkedPortal()->m_hLinkedPortal = this->AsEngineObject()->GetOuter();
}

void CEnginePortalInternal::DetachFromLinked(void) {
	if (GetLinkedPortal()) {
		GetLinkedPortal()->m_hLinkedPortal = NULL;
		m_hLinkedPortal = NULL;
	}
}

void CEnginePortalInternal::UpdateLinkMatrix(IEnginePortalServer* pRemoteCollisionEntity)
{
	if (pRemoteCollisionEntity) {
		CEnginePortalInternal* pRemotePortalInternal = dynamic_cast<CEnginePortalInternal*>(pRemoteCollisionEntity);
		Vector vLocalLeft = -m_InternalData.Placement.vRight;
		VMatrix matLocalToWorld(m_InternalData.Placement.vForward, vLocalLeft, m_InternalData.Placement.vUp);
		matLocalToWorld.SetTranslation(GetAbsOrigin());

		VMatrix matLocalToWorldInverse;
		MatrixInverseTR(matLocalToWorld, matLocalToWorldInverse);

		//180 degree rotation about up
		VMatrix matRotation;
		matRotation.Identity();
		matRotation.m[0][0] = -1.0f;
		matRotation.m[1][1] = -1.0f;

		Vector vRemoteLeft = -pRemotePortalInternal->m_InternalData.Placement.vRight;
		VMatrix matRemoteToWorld(pRemotePortalInternal->m_InternalData.Placement.vForward, vRemoteLeft, pRemotePortalInternal->m_InternalData.Placement.vUp);
		matRemoteToWorld.SetTranslation(pRemotePortalInternal->GetAbsOrigin());

		//final
		m_InternalData.Placement.matThisToLinked = matRemoteToWorld * matRotation * matLocalToWorldInverse;
	}
	else {
		m_InternalData.Placement.matThisToLinked.Identity();
	}

	m_InternalData.Placement.matThisToLinked.InverseTR(m_InternalData.Placement.matLinkedToThis);

	MatrixAngles(m_InternalData.Placement.matThisToLinked.As3x4(), m_InternalData.Placement.ptaap_ThisToLinked.qAngleTransform, m_InternalData.Placement.ptaap_ThisToLinked.ptOriginTransform);
	MatrixAngles(m_InternalData.Placement.matLinkedToThis.As3x4(), m_InternalData.Placement.ptaap_LinkedToThis.qAngleTransform, m_InternalData.Placement.ptaap_LinkedToThis.ptOriginTransform);

}

bool CEnginePortalInternal::EntityIsInPortalHole(IEngineObjectServer* pEntity) const //true if the entity is within the portal cutout bounds and crossing the plane. Not just *near* the portal
{
	Assert(m_InternalData.Placement.pHoleShapeCollideable != NULL);

#ifdef DEBUG_PORTAL_COLLISION_ENVIRONMENTS
	const char* szDumpFileName = "ps_entholecheck.txt";
	if (sv_debug_dumpportalhole_nextcheck.GetBool())
	{
		filesystem->RemoveFile(szDumpFileName);

		DumpActiveCollision(this, szDumpFileName);
		PortalSimulatorDumps_DumpCollideToGlView(m_InternalData.Placement.pHoleShapeCollideable, vec3_origin, vec3_angle, 1.0f, szDumpFileName);
	}
#endif

	trace_t Trace;

	switch (pEntity->GetSolid())
	{
	case SOLID_VPHYSICS:
	{
		ICollideable* pCollideable = pEntity->GetCollideable();
		vcollide_t* pVCollide = modelinfo->GetVCollide(pCollideable->GetCollisionModel());

		//Assert( pVCollide != NULL ); //brush models?
		if (pVCollide != NULL)
		{
			Vector ptEntityPosition = pCollideable->GetCollisionOrigin();
			QAngle qEntityAngles = pCollideable->GetCollisionAngles();

#ifdef DEBUG_PORTAL_COLLISION_ENVIRONMENTS
			if (sv_debug_dumpportalhole_nextcheck.GetBool())
			{
				for (int i = 0; i != pVCollide->solidCount; ++i)
					PortalSimulatorDumps_DumpCollideToGlView(m_InternalData.Placement.pHoleShapeCollideable, vec3_origin, vec3_angle, 0.4f, szDumpFileName);

				sv_debug_dumpportalhole_nextcheck.SetValue(false);
			}
#endif

			for (int i = 0; i != pVCollide->solidCount; ++i)
			{
				gEntList.PhysGetCollision()->TraceCollide(ptEntityPosition, ptEntityPosition, pVCollide->solids[i], qEntityAngles, m_InternalData.Placement.pHoleShapeCollideable, vec3_origin, vec3_angle, &Trace);

				if (Trace.startsolid)
					return true;
			}
		}
		else
		{
			//energy balls lack a vcollide
			Vector vMins, vMaxs, ptCenter;
			pCollideable->WorldSpaceSurroundingBounds(&vMins, &vMaxs);
			ptCenter = (vMins + vMaxs) * 0.5f;
			vMins -= ptCenter;
			vMaxs -= ptCenter;
			gEntList.PhysGetCollision()->TraceBox(ptCenter, ptCenter, vMins, vMaxs, m_InternalData.Placement.pHoleShapeCollideable, vec3_origin, vec3_angle, &Trace);

			return Trace.startsolid;
		}
		break;
	}

	case SOLID_BBOX:
	{
		gEntList.PhysGetCollision()->TraceBox(pEntity->GetAbsOrigin(), pEntity->GetAbsOrigin(),
			pEntity->OBBMins(), pEntity->OBBMaxs(),
			m_InternalData.Placement.pHoleShapeCollideable, vec3_origin, vec3_angle, &Trace);

#ifdef DEBUG_PORTAL_COLLISION_ENVIRONMENTS
		if (sv_debug_dumpportalhole_nextcheck.GetBool())
		{
			Vector vMins = pEntity->GetAbsOrigin() + pEntity->OBBMins();
			Vector vMaxs = pEntity->GetAbsOrigin() + pEntity->OBBMaxs();
			PortalSimulatorDumps_DumpBoxToGlView(vMins, vMaxs, 1.0f, 1.0f, 1.0f, szDumpFileName);

			sv_debug_dumpportalhole_nextcheck.SetValue(false);
		}
#endif

		if (Trace.startsolid)
			return true;

		break;
	}
	case SOLID_NONE:
#ifdef DEBUG_PORTAL_COLLISION_ENVIRONMENTS
		if (sv_debug_dumpportalhole_nextcheck.GetBool())
			sv_debug_dumpportalhole_nextcheck.SetValue(false);
#endif

		return false;

	default:
		Assert(false); //make a handler
	};

#ifdef DEBUG_PORTAL_COLLISION_ENVIRONMENTS
	if (sv_debug_dumpportalhole_nextcheck.GetBool())
		sv_debug_dumpportalhole_nextcheck.SetValue(false);
#endif

	return false;
}
bool CEnginePortalInternal::EntityHitBoxExtentIsInPortalHole(IEngineObjectServer* pBaseAnimating) const //true if the entity is within the portal cutout bounds and crossing the plane. Not just *near* the portal
{
	bool bFirstVert = true;
	Vector vMinExtent;
	Vector vMaxExtent;

	IStudioHdr* pStudioHdr = pBaseAnimating->GetModelPtr();
	if (!pStudioHdr)
		return false;

	mstudiohitboxset_t* set = pStudioHdr->pHitboxSet(pBaseAnimating->GetHitboxSet());
	if (!set)
		return false;

	Vector position;
	QAngle angles;

	for (int i = 0; i < set->numhitboxes; i++)
	{
		mstudiobbox_t* pbox = set->pHitbox(i);

		pBaseAnimating->GetHitboxBonePosition(pbox->bone, position, angles);

		// Build a rotation matrix from orientation
		matrix3x4_t fRotateMatrix;
		AngleMatrix(angles, fRotateMatrix);

		//Vector pVerts[8];
		Vector vecPos;
		for (int i = 0; i < 8; ++i)
		{
			vecPos[0] = (i & 0x1) ? pbox->bbmax[0] : pbox->bbmin[0];
			vecPos[1] = (i & 0x2) ? pbox->bbmax[1] : pbox->bbmin[1];
			vecPos[2] = (i & 0x4) ? pbox->bbmax[2] : pbox->bbmin[2];

			Vector vRotVec;

			VectorRotate(vecPos, fRotateMatrix, vRotVec);
			vRotVec += position;

			if (bFirstVert)
			{
				vMinExtent = vRotVec;
				vMaxExtent = vRotVec;
				bFirstVert = false;
			}
			else
			{
				vMinExtent = vMinExtent.Min(vRotVec);
				vMaxExtent = vMaxExtent.Max(vRotVec);
			}
		}
	}

	Vector ptCenter = (vMinExtent + vMaxExtent) * 0.5f;
	vMinExtent -= ptCenter;
	vMaxExtent -= ptCenter;

	trace_t Trace;
	gEntList.PhysGetCollision()->TraceBox(ptCenter, ptCenter, vMinExtent, vMaxExtent, m_InternalData.Placement.pHoleShapeCollideable, vec3_origin, vec3_angle, &Trace);

	if (Trace.startsolid)
		return true;

	return false;
}

bool CEnginePortalInternal::RayIsInPortalHole(const Ray_t& ray) const //traces a ray against the same detector for EntityIsInPortalHole(), bias is towards false positives
{
	trace_t Trace;
	gEntList.PhysGetCollision()->TraceBox(ray, m_InternalData.Placement.pHoleShapeCollideable, vec3_origin, vec3_angle, &Trace);
	return Trace.DidHit();
}

bool CEnginePortalInternal::TraceWorldBrushes(const Ray_t& ray, trace_t* pTrace) const
{
	if (m_DataAccess.Simulation.Static.World.Brushes.pCollideable && sv_portal_trace_vs_world.GetBool())
	{
		gEntList.PhysGetCollision()->TraceBox(ray, m_DataAccess.Simulation.Static.World.Brushes.pCollideable, vec3_origin, vec3_angle, pTrace);
		return true;
	}
	return false;
}

bool CEnginePortalInternal::TraceWallTube(const Ray_t& ray, trace_t* pTrace) const
{
	if (m_DataAccess.Simulation.Static.Wall.Local.Tube.pCollideable && sv_portal_trace_vs_holywall.GetBool())
	{
		gEntList.PhysGetCollision()->TraceBox(ray, m_DataAccess.Simulation.Static.Wall.Local.Tube.pCollideable, vec3_origin, vec3_angle, pTrace);
		return true;
	}
	return false;
}

bool CEnginePortalInternal::TraceWallBrushes(const Ray_t& ray, trace_t* pTrace) const
{
	if (m_DataAccess.Simulation.Static.Wall.Local.Brushes.pCollideable && sv_portal_trace_vs_holywall.GetBool())
	{
		gEntList.PhysGetCollision()->TraceBox(ray, m_DataAccess.Simulation.Static.Wall.Local.Brushes.pCollideable, vec3_origin, vec3_angle, pTrace);
		return true;
	}
	return false;
}

bool CEnginePortalInternal::TraceTransformedWorldBrushes(const IEnginePortalServer* pRemoteCollisionEntity, const Ray_t& ray, trace_t* pTrace) const
{
	const CEnginePortalInternal* pRemotePortalInternal = dynamic_cast<const CEnginePortalInternal*>(pRemoteCollisionEntity);
	if (pRemotePortalInternal->m_DataAccess.Simulation.Static.World.Brushes.pCollideable && sv_portal_trace_vs_world.GetBool())
	{
		gEntList.PhysGetCollision()->TraceBox(ray, pRemotePortalInternal->m_DataAccess.Simulation.Static.World.Brushes.pCollideable, m_DataAccess.Placement.ptaap_LinkedToThis.ptOriginTransform, m_DataAccess.Placement.ptaap_LinkedToThis.qAngleTransform, pTrace);
		return true;
	}
	return false;
}

//only enumerates entities in front of the associated portal and are solid (as in a player would get stuck in them)
class CPortalCollideableEnumerator : public IPartitionEnumerator
{
private:
	EHANDLE m_hTestPortal; //the associated portal that we only want objects in front of
	Vector m_vPlaneNormal; //portal plane normal
	float m_fPlaneDist; //plane equation distance
	Vector m_ptForward1000; //a point exactly 1000 units from the portal center along its forward vector
public:
	IHandleEntity* m_pHandles[1024];
	int m_iHandleCount;
	CPortalCollideableEnumerator(const CEnginePortalInternal* pAssociatedPortal);
	virtual IterationRetval_t EnumElement(IHandleEntity* pHandleEntity);
};

#define PORTAL_TELEPORTATION_PLANE_OFFSET 7.0f

CPortalCollideableEnumerator::CPortalCollideableEnumerator(const CEnginePortalInternal* pAssociatedPortal)
{
	Assert(pAssociatedPortal);
	m_hTestPortal = pAssociatedPortal->m_pOuter;

	pAssociatedPortal->GetVectors(&m_vPlaneNormal, NULL, NULL);

	m_ptForward1000 = pAssociatedPortal->GetAbsOrigin();
	m_ptForward1000 += m_vPlaneNormal * PORTAL_TELEPORTATION_PLANE_OFFSET;
	m_fPlaneDist = m_vPlaneNormal.Dot(m_ptForward1000);

	m_ptForward1000 += m_vPlaneNormal * 1000.0f;

	m_iHandleCount = 0;
}

IterationRetval_t CPortalCollideableEnumerator::EnumElement(IHandleEntity* pHandleEntity)
{
	EHANDLE hEnt = pHandleEntity->GetRefEHandle();

	CBaseEntity* pEnt = hEnt.Get();
	if (pEnt == NULL) //I really never thought this would be necessary
		return ITERATION_CONTINUE;

	if (hEnt == m_hTestPortal)
		return ITERATION_CONTINUE; //ignore this portal

	/*if( staticpropmgr->IsStaticProp( pHandleEntity ) )
	{
		//we're dealing with a static prop, which unfortunately doesn't have everything I want to use for checking

		ICollideable *pCollideable = pEnt->GetCollideable();

		Vector vMins, vMaxs;
		pCollideable->WorldSpaceSurroundingBounds( &vMins, &vMaxs );

		Vector ptTest( (m_vPlaneNormal.x > 0.0f)?(vMaxs.x):(vMins.x),
						(m_vPlaneNormal.y > 0.0f)?(vMaxs.y):(vMins.y),
						(m_vPlaneNormal.z > 0.0f)?(vMaxs.z):(vMins.z) );

		float fPtPlaneDist = m_vPlaneNormal.Dot( ptTest ) - m_fPlaneDist;
		if( fPtPlaneDist <= 0.0f )
			return ITERATION_CONTINUE;
	}
	else*/
	{
		//not a static prop, w00t

		if (!pEnt->GetEngineObject()->IsSolid())
			return ITERATION_CONTINUE; //not solid

		Vector ptEntCenter = pEnt->GetEngineObject()->WorldSpaceCenter();

		float fBoundRadius = pEnt->GetEngineObject()->BoundingRadius();
		float fPtPlaneDist = m_vPlaneNormal.Dot(ptEntCenter) - m_fPlaneDist;

		if (fPtPlaneDist < -fBoundRadius)
			return ITERATION_CONTINUE; //object wholly behind the portal

		if (!(fPtPlaneDist > fBoundRadius) && (fPtPlaneDist > -fBoundRadius)) //object is not wholly in front of the portal, but could be partially in front, do more checks
		{
			Vector ptNearest;
			pEnt->GetEngineObject()->CalcNearestPoint(m_ptForward1000, &ptNearest);
			fPtPlaneDist = m_vPlaneNormal.Dot(ptNearest) - m_fPlaneDist;
			if (fPtPlaneDist < 0.0f)
				return ITERATION_CONTINUE; //closest point was behind the portal plane, we don't want it
		}


	}

	//if we're down here, this entity needs to be added to our enumeration
	Assert(m_iHandleCount < 1024);
	if (m_iHandleCount < 1024)
		m_pHandles[m_iHandleCount] = pHandleEntity;
	++m_iHandleCount;

	return ITERATION_CONTINUE;
}

void CEnginePortalInternal::TraceRay(const Ray_t& ray, unsigned int fMask, ITraceFilter* pTraceFilter, trace_t* pTrace, bool bTraceHolyWall) const//traces against a specific portal's environment, does no *real* tracing
{
#ifdef CLIENT_DLL
	Assert((GameRules() == NULL) || GameRules()->IsMultiplayer());
#endif
	Assert(IsReadyToSimulate()); //a trace shouldn't make it down this far if the portal is incapable of changing the results of the trace

	CTraceFilterHitAll traceFilterHitAll;
	if (!pTraceFilter)
	{
		pTraceFilter = &traceFilterHitAll;
	}

	pTrace->fraction = 2.0f;
	pTrace->startsolid = true;
	pTrace->allsolid = true;

	trace_t TempTrace;
	int counter;

	const CEnginePortalInternal* pLinkedPortalSimulator = GetLinkedPortal();

	//bool bTraceDisplacements = sv_portal_trace_vs_displacements.GetBool();
	bool bTraceStaticProps = sv_portal_trace_vs_staticprops.GetBool();
	if (sv_portal_trace_vs_holywall.GetBool() == false)
		bTraceHolyWall = false;

	bool bTraceTransformedGeometry = ((pLinkedPortalSimulator != NULL) && bTraceHolyWall && RayIsInPortalHole(ray));

	bool bCopyBackBrushTraceData = false;



	// Traces vs world
	if (pTraceFilter->GetTraceType() != TRACE_ENTITIES_ONLY)
	{
		//trace_t RealTrace;
		//enginetrace->TraceRay( ray, fMask, pTraceFilter, &RealTrace );
		if (TraceWorldBrushes(ray, pTrace))
		{
			bCopyBackBrushTraceData = true;
		}

		if (bTraceHolyWall)
		{
			if (TraceWallTube(ray, &TempTrace))
			{
				if ((TempTrace.startsolid == false) && (TempTrace.fraction < pTrace->fraction)) //never allow something to be stuck in the tube, it's more of a last-resort guide than a real collideable
				{
					*pTrace = TempTrace;
					bCopyBackBrushTraceData = true;
				}
			}

			if (TraceWallBrushes(ray, &TempTrace))
			{
				if ((TempTrace.fraction < pTrace->fraction))
				{
					*pTrace = TempTrace;
					bCopyBackBrushTraceData = true;
				}
			}

			//if( portalSimulator->m_DataAccess.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pCollideable && sv_portal_trace_vs_world.GetBool() )
			if (bTraceTransformedGeometry && TraceTransformedWorldBrushes(pLinkedPortalSimulator, ray, &TempTrace))
			{
				if ((TempTrace.fraction < pTrace->fraction))
				{
					*pTrace = TempTrace;
					bCopyBackBrushTraceData = true;
				}
			}
		}

		if (bCopyBackBrushTraceData)
		{
			pTrace->surface = GetSurfaceProperties().surface;
			pTrace->contents = GetSurfaceProperties().contents;
			pTrace->m_pEnt = GetSurfaceProperties().pEntity;

			bCopyBackBrushTraceData = false;
		}
	}

	// Traces vs entities
	if (pTraceFilter->GetTraceType() != TRACE_WORLD_ONLY)
	{
		bool bFilterStaticProps = (pTraceFilter->GetTraceType() == TRACE_EVERYTHING_FILTER_PROPS);

		//solid entities
		CPortalCollideableEnumerator enumerator(this);
		partition->EnumerateElementsAlongRay(PARTITION_ENGINE_SOLID_EDICTS | PARTITION_ENGINE_STATIC_PROPS, ray, false, &enumerator);
		for (counter = 0; counter != enumerator.m_iHandleCount; ++counter)
		{
			if (staticpropmgr->IsStaticProp(enumerator.m_pHandles[counter]))
			{
				//if( bFilterStaticProps && !pTraceFilter->ShouldHitEntity( enumerator.m_pHandles[counter], fMask ) )
				continue; //static props are handled separately, with clipped versions
			}
			else if (!pTraceFilter->ShouldHitEntity(enumerator.m_pHandles[counter], fMask))
			{
				continue;
			}

			enginetrace->ClipRayToEntity(ray, fMask, enumerator.m_pHandles[counter], &TempTrace);
			if ((TempTrace.fraction < pTrace->fraction))
				*pTrace = TempTrace;
		}




		if (bTraceStaticProps)
		{
			//local clipped static props
			{
				int iLocalStaticCount = GetStaticPropsCount();
				if (iLocalStaticCount != 0 && StaticPropsCollisionExists())
				{
					int iIndex = 0;
					Vector vTransform = vec3_origin;
					QAngle qTransform = vec3_angle;

					do
					{
						const PS_SD_Static_World_StaticProps_ClippedProp_t* pCurrentProp = GetStaticProps(iIndex);
						if ((!bFilterStaticProps) || pTraceFilter->ShouldHitEntity(pCurrentProp->pSourceProp, fMask))
						{
							EntityList()->PhysGetCollision()->TraceBox(ray, pCurrentProp->pCollide, vTransform, qTransform, &TempTrace);
							if ((TempTrace.fraction < pTrace->fraction))
							{
								*pTrace = TempTrace;
								pTrace->surface.flags = pCurrentProp->iTraceSurfaceFlags;
								pTrace->surface.surfaceProps = pCurrentProp->iTraceSurfaceProps;
								pTrace->surface.name = pCurrentProp->szTraceSurfaceName;
								pTrace->contents = pCurrentProp->iTraceContents;
								pTrace->m_pEnt = pCurrentProp->pTraceEntity;
							}
						}

						++iIndex;
					} while (iIndex != iLocalStaticCount);
				}
			}

			if (bTraceHolyWall)
			{
				//remote clipped static props transformed into our wall space
				if (bTraceTransformedGeometry && (pTraceFilter->GetTraceType() != TRACE_WORLD_ONLY) && sv_portal_trace_vs_staticprops.GetBool())
				{
					int iLocalStaticCount = pLinkedPortalSimulator->GetStaticPropsCount();
					if (iLocalStaticCount != 0)
					{
						int iIndex = 0;
						Vector vTransform = GetTransformedOrigin();
						QAngle qTransform = GetTransformedAngles();

						do
						{
							const PS_SD_Static_World_StaticProps_ClippedProp_t* pCurrentProp = pLinkedPortalSimulator->GetStaticProps(iIndex);
							if ((!bFilterStaticProps) || pTraceFilter->ShouldHitEntity(pCurrentProp->pSourceProp, fMask))
							{
								EntityList()->PhysGetCollision()->TraceBox(ray, pCurrentProp->pCollide, vTransform, qTransform, &TempTrace);
								if ((TempTrace.fraction < pTrace->fraction))
								{
									*pTrace = TempTrace;
									pTrace->surface.flags = pCurrentProp->iTraceSurfaceFlags;
									pTrace->surface.surfaceProps = pCurrentProp->iTraceSurfaceProps;
									pTrace->surface.name = pCurrentProp->szTraceSurfaceName;
									pTrace->contents = pCurrentProp->iTraceContents;
									pTrace->m_pEnt = pCurrentProp->pTraceEntity;
								}
							}

							++iIndex;
						} while (iIndex != iLocalStaticCount);
					}
				}
			}
		}
	}

	if (pTrace->fraction > 1.0f) //this should only happen if there was absolutely nothing to trace against
	{
		//AssertMsg( 0, "Nothing to trace against" );
		memset(pTrace, 0, sizeof(trace_t));
		pTrace->fraction = 1.0f;
		pTrace->startpos = ray.m_Start - ray.m_StartOffset;
		pTrace->endpos = pTrace->startpos + ray.m_Delta;
	}
	else if (pTrace->fraction < 0)
	{
		// For all brush traces, use the 'portal backbrush' surface surface contents
		// BUGBUG: Doing this is a great solution because brushes near a portal
		// will have their contents and surface properties homogenized to the brush the portal ray hit.
		pTrace->contents = GetSurfaceProperties().contents;
		pTrace->surface = GetSurfaceProperties().surface;
		pTrace->m_pEnt = GetSurfaceProperties().pEntity;
	}
}

class CTransformedCollideable : public ICollideable //wraps an existing collideable, but transforms everything that pertains to world space by another transform
{
public:
	VMatrix m_matTransform; //the transformation we apply to the wrapped collideable
	VMatrix m_matInvTransform; //cached inverse of m_matTransform

	ICollideable* m_pWrappedCollideable; //the collideable we're transforming without it knowing

	struct CTC_ReferenceVars_t
	{
		Vector m_vCollisionOrigin;
		QAngle m_qCollisionAngles;
		matrix3x4_t m_matCollisionToWorldTransform;
		matrix3x4_t m_matRootParentToWorldTransform;
	};

	mutable CTC_ReferenceVars_t m_ReferencedVars; //when returning a const reference, it needs to point to something, so here we go

	//abstract functions which require no transforms, just pass them along to the wrapped collideable
	virtual IHandleEntity* GetEntityHandle() { return m_pWrappedCollideable->GetEntityHandle(); }
	virtual const Vector& OBBMinsPreScaled() const { return m_pWrappedCollideable->OBBMinsPreScaled(); }
	virtual const Vector& OBBMaxsPreScaled() const { return m_pWrappedCollideable->OBBMaxsPreScaled(); }
	virtual const Vector& OBBMins() const { return m_pWrappedCollideable->OBBMins(); }
	virtual const Vector& OBBMaxs() const { return m_pWrappedCollideable->OBBMaxs(); }
	virtual int				GetCollisionModelIndex() { return m_pWrappedCollideable->GetCollisionModelIndex(); }
	virtual const model_t* GetCollisionModel() { return m_pWrappedCollideable->GetCollisionModel(); }
	virtual SolidType_t		GetSolid() const { return m_pWrappedCollideable->GetSolid(); }
	virtual int				GetSolidFlags() const { return m_pWrappedCollideable->GetSolidFlags(); }
	//virtual IClientUnknown*	GetIClientUnknown() { return m_pWrappedCollideable->GetIClientUnknown(); }
	virtual int				GetCollisionGroup() const { return m_pWrappedCollideable->GetCollisionGroup(); }
	virtual bool			ShouldTouchTrigger(int triggerSolidFlags) const { return m_pWrappedCollideable->ShouldTouchTrigger(triggerSolidFlags); }

	//slightly trickier functions
	virtual void			WorldSpaceTriggerBounds(Vector* pVecWorldMins, Vector* pVecWorldMaxs) const;
	virtual bool			TestCollision(const Ray_t& ray, unsigned int fContentsMask, trace_t& tr);
	virtual bool			TestHitboxes(const Ray_t& ray, unsigned int fContentsMask, trace_t& tr);
	virtual const Vector& GetCollisionOrigin() const;
	virtual const QAngle& GetCollisionAngles() const;
	virtual const matrix3x4_t& CollisionToWorldTransform() const;
	virtual void			WorldSpaceSurroundingBounds(Vector* pVecMins, Vector* pVecMaxs);
	virtual const matrix3x4_t* GetRootParentToWorldTransform() const;
};

void CTransformedCollideable::WorldSpaceTriggerBounds(Vector* pVecWorldMins, Vector* pVecWorldMaxs) const
{
	m_pWrappedCollideable->WorldSpaceTriggerBounds(pVecWorldMins, pVecWorldMaxs);

	if (pVecWorldMins)
		*pVecWorldMins = m_matTransform * (*pVecWorldMins);

	if (pVecWorldMaxs)
		*pVecWorldMaxs = m_matTransform * (*pVecWorldMaxs);
}

bool CTransformedCollideable::TestCollision(const Ray_t& ray, unsigned int fContentsMask, trace_t& tr)
{
	//TODO: Transform the ray by inverse matTransform and transform the trace results by matTransform? AABB Errors arise by transforming the ray.
	return m_pWrappedCollideable->TestCollision(ray, fContentsMask, tr);
}

bool CTransformedCollideable::TestHitboxes(const Ray_t& ray, unsigned int fContentsMask, trace_t& tr)
{
	//TODO: Transform the ray by inverse matTransform and transform the trace results by matTransform? AABB Errors arise by transforming the ray.
	return m_pWrappedCollideable->TestHitboxes(ray, fContentsMask, tr);
}

const Vector& CTransformedCollideable::GetCollisionOrigin() const
{
	m_ReferencedVars.m_vCollisionOrigin = m_matTransform * m_pWrappedCollideable->GetCollisionOrigin();
	return m_ReferencedVars.m_vCollisionOrigin;
}

const QAngle& CTransformedCollideable::GetCollisionAngles() const
{
	m_ReferencedVars.m_qCollisionAngles = TransformAnglesToWorldSpace(m_pWrappedCollideable->GetCollisionAngles(), m_matTransform.As3x4());
	return m_ReferencedVars.m_qCollisionAngles;
}

const matrix3x4_t& CTransformedCollideable::CollisionToWorldTransform() const
{
	//1-2 order correct?
	ConcatTransforms(m_matTransform.As3x4(), m_pWrappedCollideable->CollisionToWorldTransform(), m_ReferencedVars.m_matCollisionToWorldTransform);
	return m_ReferencedVars.m_matCollisionToWorldTransform;
}

void CTransformedCollideable::WorldSpaceSurroundingBounds(Vector* pVecMins, Vector* pVecMaxs)
{
	if ((pVecMins == NULL) && (pVecMaxs == NULL))
		return;

	Vector vMins, vMaxs;
	m_pWrappedCollideable->WorldSpaceSurroundingBounds(&vMins, &vMaxs);

	TransformAABB(m_matTransform.As3x4(), vMins, vMaxs, vMins, vMaxs);

	if (pVecMins)
		*pVecMins = vMins;
	if (pVecMaxs)
		*pVecMaxs = vMaxs;
}

const matrix3x4_t* CTransformedCollideable::GetRootParentToWorldTransform() const
{
	const matrix3x4_t* pWrappedVersion = m_pWrappedCollideable->GetRootParentToWorldTransform();
	if (pWrappedVersion == NULL)
		return NULL;

	ConcatTransforms(m_matTransform.As3x4(), *pWrappedVersion, m_ReferencedVars.m_matRootParentToWorldTransform);
	return &m_ReferencedVars.m_matRootParentToWorldTransform;
}

void CEnginePortalInternal::TraceEntity(CBaseEntity* pEntity, const Vector& vecAbsStart, const Vector& vecAbsEnd, unsigned int mask, ITraceFilter* pFilter, trace_t* pTrace) const
{

	const CEnginePortalInternal* pLinkedPortalSimulator = this->GetLinkedPortal();
	ICollideable* pCollision = enginetrace->GetCollideable(pEntity);

	Ray_t entRay;
	entRay.Init(vecAbsStart, vecAbsEnd, pCollision->OBBMins(), pCollision->OBBMaxs());

#if 0 // this trace for brush ents made sense at one time, but it's 'overcolliding' during portal transitions (bugzilla#25)
	if (realTrace.m_pEnt && (realTrace.m_pEnt->GetEngineObject()->GetMoveType() != MOVETYPE_NONE)) //started by hitting something moving which wouldn't be detected in the following traces
	{
		float fFirstPortalFraction = 2.0f;
		CProp_Portal* pFirstPortal = UTIL_Portal_FirstAlongRay(entRay, fFirstPortalFraction);

		if (!pFirstPortal)
			*pTrace = realTrace;
		else
		{
			Vector vFirstPortalForward;
			pFirstPortal->GetVectors(&vFirstPortalForward, NULL, NULL);
			if (vFirstPortalForward.Dot(realTrace.endpos - pFirstPortal->GetAbsOrigin()) > 0.0f)
				*pTrace = realTrace;
		}
	}
#endif

	// We require both environments to be active in order to trace against them
	Assert(pCollision);
	if (!pCollision)
	{
		return;
	}

	// World, displacements and holy wall are stored in separate collideables
	// Traces against each and keep the closest intersection (if any)
	trace_t tempTrace;

	// Hit the world
	if (pFilter->GetTraceType() != TRACE_ENTITIES_ONLY)
	{
		if (TraceWorldBrushes(entRay, &tempTrace))
		{
			//physcollision->TraceCollide( vecAbsStart, vecAbsEnd, pCollision, qCollisionAngles, 
			//							pPortalSimulator->m_DataAccess.Simulation.Static.World.Brushes.pCollideable, vec3_origin, vec3_angle, &tempTrace );
			if (tempTrace.startsolid || (tempTrace.fraction < pTrace->fraction))
			{
				*pTrace = tempTrace;
			}
		}

		//if( pPortalSimulator->m_DataAccess.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pCollideable &&
		if (pLinkedPortalSimulator && TraceTransformedWorldBrushes(pLinkedPortalSimulator, entRay, &tempTrace))
		{
			//physcollision->TraceCollide( vecAbsStart, vecAbsEnd, pCollision, qCollisionAngles,
			//							pLinkedPortalSimulator->m_DataAccess.Simulation.Static.World.Brushes.pCollideable, pPortalSimulator->m_DataAccess.Placement.ptaap_LinkedToThis.ptOriginTransform, pPortalSimulator->m_DataAccess.Placement.ptaap_LinkedToThis.qAngleTransform, &tempTrace );

			if (tempTrace.startsolid || (tempTrace.fraction < pTrace->fraction))
			{
				*pTrace = tempTrace;
			}
		}

		if (TraceWallBrushes(entRay, &tempTrace))
		{
			//physcollision->TraceCollide( vecAbsStart, vecAbsEnd, pCollision, qCollisionAngles,
			//							pPortalSimulator->m_DataAccess.Simulation.Static.Wall.Local.Brushes.pCollideable, vec3_origin, vec3_angle, &tempTrace );

			if (tempTrace.startsolid || (tempTrace.fraction < pTrace->fraction))
			{
				if (tempTrace.fraction == 0.0f)
					tempTrace.startsolid = true;

				if (tempTrace.fractionleftsolid == 1.0f)
					tempTrace.allsolid = true;

				*pTrace = tempTrace;
			}
		}

		if (TraceWallTube(entRay, &tempTrace))
		{
			//physcollision->TraceCollide( vecAbsStart, vecAbsEnd, pCollision, qCollisionAngles,
			//							pPortalSimulator->m_DataAccess.Simulation.Static.Wall.Local.Tube.pCollideable, vec3_origin, vec3_angle, &tempTrace );

			if ((tempTrace.startsolid == false) && (tempTrace.fraction < pTrace->fraction)) //never allow something to be stuck in the tube, it's more of a last-resort guide than a real collideable
			{
				*pTrace = tempTrace;
			}
		}

		// For all brush traces, use the 'portal backbrush' surface surface contents
		// BUGBUG: Doing this is a great solution because brushes near a portal
		// will have their contents and surface properties homogenized to the brush the portal ray hit.
		if (pTrace->startsolid || (pTrace->fraction < 1.0f))
		{
			pTrace->surface = GetSurfaceProperties().surface;
			pTrace->contents = GetSurfaceProperties().contents;
			pTrace->m_pEnt = GetSurfaceProperties().pEntity;
		}
	}

	// Trace vs entities
	if (pFilter->GetTraceType() != TRACE_WORLD_ONLY)
	{
		if (sv_portal_trace_vs_staticprops.GetBool() && (pFilter->GetTraceType() != TRACE_ENTITIES_ONLY))
		{
			bool bFilterStaticProps = (pFilter->GetTraceType() == TRACE_EVERYTHING_FILTER_PROPS);

			//local clipped static props
			{
				int iLocalStaticCount = GetStaticPropsCount();
				if (iLocalStaticCount != 0 && StaticPropsCollisionExists())
				{
					int iIndex = 0;
					Vector vTransform = vec3_origin;
					QAngle qTransform = vec3_angle;

					do
					{
						const PS_SD_Static_World_StaticProps_ClippedProp_t* pCurrentProp = GetStaticProps(iIndex);
						if ((!bFilterStaticProps) || pFilter->ShouldHitEntity(pCurrentProp->pSourceProp, mask))
						{
							//physcollision->TraceCollide( vecAbsStart, vecAbsEnd, pCollision, qCollisionAngles,
							//							pCurrentProp->pCollide, vTransform, qTransform, &tempTrace );

							EntityList()->PhysGetCollision()->TraceBox(entRay, MASK_ALL, NULL, pCurrentProp->pCollide, vTransform, qTransform, &tempTrace);

							if (tempTrace.startsolid || (tempTrace.fraction < pTrace->fraction))
							{
								*pTrace = tempTrace;
								pTrace->surface.flags = pCurrentProp->iTraceSurfaceFlags;
								pTrace->surface.surfaceProps = pCurrentProp->iTraceSurfaceProps;
								pTrace->surface.name = pCurrentProp->szTraceSurfaceName;
								pTrace->contents = pCurrentProp->iTraceContents;
								pTrace->m_pEnt = pCurrentProp->pTraceEntity;
							}
						}

						++iIndex;
					} while (iIndex != iLocalStaticCount);
				}
			}

			if (pLinkedPortalSimulator && EntityIsInPortalHole(pEntity->GetEngineObject()))
			{

#ifndef CLIENT_DLL
				if (sv_use_transformed_collideables.GetBool()) //if this never gets turned off, it should be removed before release
				{
					//moving entities near the remote portal
					CBaseEntity* pEnts[1024];
					int iEntCount = pLinkedPortalSimulator->GetMoveableOwnedEntities(pEnts, 1024);

					CTransformedCollideable transformedCollideable;
					transformedCollideable.m_matTransform = pLinkedPortalSimulator->MatrixThisToLinked();
					transformedCollideable.m_matInvTransform = pLinkedPortalSimulator->MatrixLinkedToThis();
					for (int i = 0; i != iEntCount; ++i)
					{
						CBaseEntity* pRemoteEntity = pEnts[i];
						if (pRemoteEntity->GetEngineObject()->GetSolid() == SOLID_NONE)
							continue;

						transformedCollideable.m_pWrappedCollideable = pRemoteEntity->GetCollideable();
						Assert(transformedCollideable.m_pWrappedCollideable != NULL);

						//enginetrace->ClipRayToCollideable( entRay, mask, &transformedCollideable, pTrace );

						enginetrace->ClipRayToCollideable(entRay, mask, &transformedCollideable, &tempTrace);
						if (tempTrace.startsolid || (tempTrace.fraction < pTrace->fraction))
						{
							*pTrace = tempTrace;
						}
					}
				}
#endif //#ifndef CLIENT_DLL
			}
		}
	}

	if (pTrace->fraction == 1.0f)
	{
		memset(pTrace, 0, sizeof(trace_t));
		pTrace->fraction = 1.0f;
		pTrace->startpos = vecAbsStart;
		pTrace->endpos = vecAbsEnd;
	}
	//#endif

}

int CEnginePortalInternal::GetStaticPropsCount() const
{
	return m_DataAccess.Simulation.Static.World.StaticProps.ClippedRepresentations.Count();
}

const PS_SD_Static_World_StaticProps_ClippedProp_t* CEnginePortalInternal::GetStaticProps(int index) const
{
	return &m_DataAccess.Simulation.Static.World.StaticProps.ClippedRepresentations[index];
}

bool CEnginePortalInternal::StaticPropsCollisionExists() const
{
	return m_DataAccess.Simulation.Static.World.StaticProps.bCollisionExists;
}

//const Vector& CPSCollisionEntity::GetOrigin() const
//{
//	return m_DataAccess.Placement.ptCenter;
//}

//const QAngle& CPSCollisionEntity::GetAngles() const
//{
//	return m_DataAccess.Placement.qAngles;
//}

const Vector& CEnginePortalInternal::GetTransformedOrigin() const
{
	return m_DataAccess.Placement.ptaap_LinkedToThis.ptOriginTransform;
}

const QAngle& CEnginePortalInternal::GetTransformedAngles() const
{
	return m_DataAccess.Placement.ptaap_LinkedToThis.qAngleTransform;
}

const VMatrix& CEnginePortalInternal::MatrixThisToLinked() const
{
	return m_InternalData.Placement.matThisToLinked;
}
const VMatrix& CEnginePortalInternal::MatrixLinkedToThis() const
{
	return m_InternalData.Placement.matLinkedToThis;
}

const cplane_t& CEnginePortalInternal::GetPortalPlane() const
{
	return m_DataAccess.Placement.PortalPlane;
}

const Vector& CEnginePortalInternal::GetVectorForward() const
{
	return m_DataAccess.Placement.vForward;
}
const Vector& CEnginePortalInternal::GetVectorUp() const
{
	return m_DataAccess.Placement.vUp;
}
const Vector& CEnginePortalInternal::GetVectorRight() const
{
	return m_DataAccess.Placement.vRight;
}

const PS_SD_Static_SurfaceProperties_t& CEnginePortalInternal::GetSurfaceProperties() const
{
	return m_DataAccess.Simulation.Static.SurfaceProperties;
}

IPhysicsObject* CEnginePortalInternal::GetWorldBrushesPhysicsObject() const
{
	return m_DataAccess.Simulation.Static.World.Brushes.pPhysicsObject;
}

IPhysicsObject* CEnginePortalInternal::GetWallBrushesPhysicsObject() const
{
	return m_DataAccess.Simulation.Static.Wall.Local.Brushes.pPhysicsObject;
}

IPhysicsObject* CEnginePortalInternal::GetWallTubePhysicsObject() const
{
	return m_DataAccess.Simulation.Static.Wall.Local.Tube.pPhysicsObject;
}

IPhysicsObject* CEnginePortalInternal::GetRemoteWallBrushesPhysicsObject() const
{
	return m_DataAccess.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject;
}

IPhysicsEnvironment* CEnginePortalInternal::GetPhysicsEnvironment()
{
	return m_pPhysicsEnvironment;
}

void CEnginePortalInternal::CreatePhysicsEnvironment()
{
	m_pPhysicsEnvironment = gEntList.PhysGetEnv();
//#ifdef PORTAL
//	pPhysicsEnvironment = physenv_main;
//#endif
}

void CEnginePortalInternal::ClearPhysicsEnvironment()
{
	m_pPhysicsEnvironment = NULL;
}

class CStaticCollisionPolyhedronCache : public CAutoGameSystem
{
public:
	CStaticCollisionPolyhedronCache(void);
	~CStaticCollisionPolyhedronCache(void);

	void LevelInitPreEntity(void);
	void Shutdown(void);

	const CPolyhedron* GetBrushPolyhedron(int iBrushNumber);
	int GetStaticPropPolyhedrons(ICollideable* pStaticProp, CPolyhedron** pOutputPolyhedronArray, int iOutputArraySize);

private:
	// See comments in LevelInitPreEntity for why these members are commented out
//	CUtlString	m_CachedMap;

	CUtlVector<CPolyhedron*> m_BrushPolyhedrons;

	struct StaticPropPolyhedronCacheInfo_t
	{
		int iStartIndex;
		int iNumPolyhedrons;
		int iStaticPropIndex; //helps us remap ICollideable pointers when the map is restarted
	};

	CUtlVector<CPolyhedron*> m_StaticPropPolyhedrons;
	CUtlMap<ICollideable*, StaticPropPolyhedronCacheInfo_t> m_CollideableIndicesMap;


	void Clear(void);
	void Update(void);
};

class CPolyhedron_LumpedMemory : public CPolyhedron //we'll be allocating one big chunk of memory for all our polyhedrons. No individual will own any memory.
{
public:
	virtual void Release(void) { };
	static CPolyhedron_LumpedMemory* AllocateAt(void* pMemory, int iVertices, int iLines, int iIndices, int iPolygons)
	{
#include "tier0/memdbgoff.h" //the following placement new doesn't compile with memory debugging
		CPolyhedron_LumpedMemory* pAllocated = new (pMemory) CPolyhedron_LumpedMemory;
#include "tier0/memdbgon.h"

		pAllocated->iVertexCount = iVertices;
		pAllocated->iLineCount = iLines;
		pAllocated->iIndexCount = iIndices;
		pAllocated->iPolygonCount = iPolygons;
		pAllocated->pVertices = (Vector*)(pAllocated + 1); //start vertex memory at the end of the class
		pAllocated->pLines = (Polyhedron_IndexedLine_t*)(pAllocated->pVertices + iVertices);
		pAllocated->pIndices = (Polyhedron_IndexedLineReference_t*)(pAllocated->pLines + iLines);
		pAllocated->pPolygons = (Polyhedron_IndexedPolygon_t*)(pAllocated->pIndices + iIndices);

		return pAllocated;
	}
};

static uint8* s_BrushPolyhedronMemory = NULL;
static uint8* s_StaticPropPolyhedronMemory = NULL;


typedef ICollideable* ICollideablePtr; //needed for key comparison function syntax
static bool CollideablePtr_KeyCompareFunc(const ICollideablePtr& a, const ICollideablePtr& b)
{
	return a < b;
};

CStaticCollisionPolyhedronCache::CStaticCollisionPolyhedronCache(void)
	: m_CollideableIndicesMap(CollideablePtr_KeyCompareFunc)
{

}

CStaticCollisionPolyhedronCache::~CStaticCollisionPolyhedronCache(void)
{
	Clear();
}

void CStaticCollisionPolyhedronCache::LevelInitPreEntity(void)
{

	// FIXME: Fast updates would be nice but this method doesn't work with the recent changes to standard containers.
	// For now we're going with the quick fix of always doing a full update. -Jeep

//	if( Q_stricmp( m_CachedMap, MapName() ) != 0 )
//	{
//		// New map or the last load was a transition, fully update the cache
//		m_CachedMap.Set( MapName() );

	Update();
	//	}
	//	else
	//	{
	//		// No need for a full update, but we need to remap static prop ICollideable's in the old system to the new system
	//		for( int i = m_CollideableIndicesMap.Count(); --i >= 0; )
	//		{
	//#ifdef _DEBUG
	//			StaticPropPolyhedronCacheInfo_t cacheInfo = m_CollideableIndicesMap.Element(i);
	//#endif
	//			m_CollideableIndicesMap.Reinsert( staticpropmgr->GetStaticPropByIndex( m_CollideableIndicesMap.Element(i).iStaticPropIndex ), i );
	//
	//			Assert( (m_CollideableIndicesMap.Element(i).iStartIndex == cacheInfo.iStartIndex) &&
	//					(m_CollideableIndicesMap.Element(i).iNumPolyhedrons == cacheInfo.iNumPolyhedrons) &&
	//					(m_CollideableIndicesMap.Element(i).iStaticPropIndex == cacheInfo.iStaticPropIndex) ); //I'm assuming this doesn't cause a reindex of the unordered list, if it does then this needs to be rewritten
	//		}
	//	}
}

void CStaticCollisionPolyhedronCache::Shutdown(void)
{
	Clear();
}


void CStaticCollisionPolyhedronCache::Clear(void)
{
	//The uses one big lump of memory to store polyhedrons. No need to Release() the polyhedrons.

	//Brushes
	{
		m_BrushPolyhedrons.RemoveAll();
		if (s_BrushPolyhedronMemory != NULL)
		{
			delete[]s_BrushPolyhedronMemory;
			s_BrushPolyhedronMemory = NULL;
		}
	}

	//Static props
	{
		m_CollideableIndicesMap.RemoveAll();
		m_StaticPropPolyhedrons.RemoveAll();
		if (s_StaticPropPolyhedronMemory != NULL)
		{
			delete[]s_StaticPropPolyhedronMemory;
			s_StaticPropPolyhedronMemory = NULL;
		}
	}
}

void CStaticCollisionPolyhedronCache::Update(void)
{
	Clear();

	//There's no efficient way to know exactly how much memory we'll need to cache off all these polyhedrons.
	//So we're going to allocated temporary workspaces as we need them and consolidate into one allocation at the end.
	const size_t workSpaceSize = 1024 * 1024; //1MB. Fairly arbitrary size for a workspace. Brushes usually use 1-3MB in the end. Static props usually use about half as much as brushes.

	uint8* workSpaceAllocations[256];
	size_t usedSpaceInWorkspace[256];
	unsigned int workSpacesAllocated = 0;
	uint8* pCurrentWorkSpace = new uint8[workSpaceSize];
	size_t roomLeftInWorkSpace = workSpaceSize;
	workSpaceAllocations[workSpacesAllocated] = pCurrentWorkSpace;
	usedSpaceInWorkspace[workSpacesAllocated] = 0;
	++workSpacesAllocated;


	//brushes
	{
		int iBrush = 0;
		CUtlVector<Vector4D> Planes;

		float fStackPlanes[4 * 400]; //400 is a crapload of planes in my opinion

		while (enginetrace->GetBrushInfo(iBrush, &Planes, NULL))
		{
			int iPlaneCount = Planes.Count();
			AssertMsg(iPlaneCount != 0, "A brush with no planes???????");

			const Vector4D* pReturnedPlanes = Planes.Base();

			CPolyhedron* pTempPolyhedron;

			if (iPlaneCount > 400)
			{
				// o_O, we'll have to get more memory to transform this brush
				float* pNonstackPlanes = new float[4 * iPlaneCount];

				for (int i = 0; i != iPlaneCount; ++i)
				{
					pNonstackPlanes[(i * 4) + 0] = pReturnedPlanes[i].x;
					pNonstackPlanes[(i * 4) + 1] = pReturnedPlanes[i].y;
					pNonstackPlanes[(i * 4) + 2] = pReturnedPlanes[i].z;
					pNonstackPlanes[(i * 4) + 3] = pReturnedPlanes[i].w;
				}

				pTempPolyhedron = GeneratePolyhedronFromPlanes(pNonstackPlanes, iPlaneCount, 0.01f, true);

				delete[]pNonstackPlanes;
			}
			else
			{
				for (int i = 0; i != iPlaneCount; ++i)
				{
					fStackPlanes[(i * 4) + 0] = pReturnedPlanes[i].x;
					fStackPlanes[(i * 4) + 1] = pReturnedPlanes[i].y;
					fStackPlanes[(i * 4) + 2] = pReturnedPlanes[i].z;
					fStackPlanes[(i * 4) + 3] = pReturnedPlanes[i].w;
				}

				pTempPolyhedron = GeneratePolyhedronFromPlanes(fStackPlanes, iPlaneCount, 0.01f, true);
			}

			if (pTempPolyhedron)
			{
				size_t memRequired = (sizeof(CPolyhedron_LumpedMemory)) +
					(sizeof(Vector) * pTempPolyhedron->iVertexCount) +
					(sizeof(Polyhedron_IndexedLine_t) * pTempPolyhedron->iLineCount) +
					(sizeof(Polyhedron_IndexedLineReference_t) * pTempPolyhedron->iIndexCount) +
					(sizeof(Polyhedron_IndexedPolygon_t) * pTempPolyhedron->iPolygonCount);

				Assert(memRequired < workSpaceSize);

				if (roomLeftInWorkSpace < memRequired)
				{
					usedSpaceInWorkspace[workSpacesAllocated - 1] = workSpaceSize - roomLeftInWorkSpace;

					pCurrentWorkSpace = new uint8[workSpaceSize];
					roomLeftInWorkSpace = workSpaceSize;
					workSpaceAllocations[workSpacesAllocated] = pCurrentWorkSpace;
					usedSpaceInWorkspace[workSpacesAllocated] = 0;
					++workSpacesAllocated;
				}

				CPolyhedron* pWorkSpacePolyhedron = CPolyhedron_LumpedMemory::AllocateAt(pCurrentWorkSpace,
					pTempPolyhedron->iVertexCount,
					pTempPolyhedron->iLineCount,
					pTempPolyhedron->iIndexCount,
					pTempPolyhedron->iPolygonCount);

				pCurrentWorkSpace += memRequired;
				roomLeftInWorkSpace -= memRequired;

				memcpy(pWorkSpacePolyhedron->pVertices, pTempPolyhedron->pVertices, pTempPolyhedron->iVertexCount * sizeof(Vector));
				memcpy(pWorkSpacePolyhedron->pLines, pTempPolyhedron->pLines, pTempPolyhedron->iLineCount * sizeof(Polyhedron_IndexedLine_t));
				memcpy(pWorkSpacePolyhedron->pIndices, pTempPolyhedron->pIndices, pTempPolyhedron->iIndexCount * sizeof(Polyhedron_IndexedLineReference_t));
				memcpy(pWorkSpacePolyhedron->pPolygons, pTempPolyhedron->pPolygons, pTempPolyhedron->iPolygonCount * sizeof(Polyhedron_IndexedPolygon_t));

				m_BrushPolyhedrons.AddToTail(pWorkSpacePolyhedron);

				pTempPolyhedron->Release();
			}
			else
			{
				m_BrushPolyhedrons.AddToTail(NULL);
			}

			++iBrush;
		}

		usedSpaceInWorkspace[workSpacesAllocated - 1] = workSpaceSize - roomLeftInWorkSpace;

		if (usedSpaceInWorkspace[0] != 0) //At least a little bit of memory was used.
		{
			//consolidate workspaces into a single memory chunk
			size_t totalMemoryNeeded = 0;
			for (unsigned int i = 0; i != workSpacesAllocated; ++i)
			{
				totalMemoryNeeded += usedSpaceInWorkspace[i];
			}

			uint8* pFinalDest = new uint8[totalMemoryNeeded];
			s_BrushPolyhedronMemory = pFinalDest;

			DevMsg(2, "CStaticCollisionPolyhedronCache: Used %.2f KB to cache %d brush polyhedrons.\n", ((float)totalMemoryNeeded) / 1024.0f, m_BrushPolyhedrons.Count());

			int iCount = m_BrushPolyhedrons.Count();
			for (int i = 0; i != iCount; ++i)
			{
				CPolyhedron_LumpedMemory* pSource = (CPolyhedron_LumpedMemory*)m_BrushPolyhedrons[i];

				if (pSource == NULL)
					continue;

				size_t memRequired = (sizeof(CPolyhedron_LumpedMemory)) +
					(sizeof(Vector) * pSource->iVertexCount) +
					(sizeof(Polyhedron_IndexedLine_t) * pSource->iLineCount) +
					(sizeof(Polyhedron_IndexedLineReference_t) * pSource->iIndexCount) +
					(sizeof(Polyhedron_IndexedPolygon_t) * pSource->iPolygonCount);

				CPolyhedron_LumpedMemory* pDest = (CPolyhedron_LumpedMemory*)pFinalDest;
				m_BrushPolyhedrons[i] = pDest;
				pFinalDest += memRequired;

				intp memoryOffset = ((uint8*)pDest) - ((uint8*)pSource);

				memcpy(pDest, pSource, memRequired);
				//move all the pointers to their new location.
				pDest->pVertices = (Vector*)(((uint8*)(pDest->pVertices)) + memoryOffset);
				pDest->pLines = (Polyhedron_IndexedLine_t*)(((uint8*)(pDest->pLines)) + memoryOffset);
				pDest->pIndices = (Polyhedron_IndexedLineReference_t*)(((uint8*)(pDest->pIndices)) + memoryOffset);
				pDest->pPolygons = (Polyhedron_IndexedPolygon_t*)(((uint8*)(pDest->pPolygons)) + memoryOffset);
			}
		}
	}

	unsigned int iBrushWorkSpaces = workSpacesAllocated;
	workSpacesAllocated = 1;
	pCurrentWorkSpace = workSpaceAllocations[0];
	usedSpaceInWorkspace[0] = 0;
	roomLeftInWorkSpace = workSpaceSize;

	//static props
	{
		CUtlVector<ICollideable*> StaticPropCollideables;
		staticpropmgr->GetAllStaticProps(&StaticPropCollideables);

		if (StaticPropCollideables.Count() != 0)
		{
			ICollideable** pCollideables = StaticPropCollideables.Base();
			ICollideable** pStop = pCollideables + StaticPropCollideables.Count();

			int iStaticPropIndex = 0;
			do
			{
				ICollideable* pProp = *pCollideables;
				vcollide_t* pCollide = modelinfo->GetVCollide(pProp->GetCollisionModel());
				StaticPropPolyhedronCacheInfo_t cacheInfo;
				cacheInfo.iStartIndex = m_StaticPropPolyhedrons.Count();

				if (pCollide != NULL)
				{
					VMatrix matToWorldPosition = pProp->CollisionToWorldTransform();

					for (int i = 0; i != pCollide->solidCount; ++i)
					{
						CPhysConvex* ConvexesArray[1024];
						int iConvexes = gEntList.PhysGetCollision()->GetConvexesUsedInCollideable(pCollide->solids[i], ConvexesArray, 1024);

						for (int j = 0; j != iConvexes; ++j)
						{
							CPolyhedron* pTempPolyhedron = gEntList.PhysGetCollision()->PolyhedronFromConvex(ConvexesArray[j], true);
							if (pTempPolyhedron)
							{
								for (int iPointCounter = 0; iPointCounter != pTempPolyhedron->iVertexCount; ++iPointCounter)
									pTempPolyhedron->pVertices[iPointCounter] = matToWorldPosition * pTempPolyhedron->pVertices[iPointCounter];

								for (int iPolyCounter = 0; iPolyCounter != pTempPolyhedron->iPolygonCount; ++iPolyCounter)
									pTempPolyhedron->pPolygons[iPolyCounter].polyNormal = matToWorldPosition.ApplyRotation(pTempPolyhedron->pPolygons[iPolyCounter].polyNormal);


								size_t memRequired = (sizeof(CPolyhedron_LumpedMemory)) +
									(sizeof(Vector) * pTempPolyhedron->iVertexCount) +
									(sizeof(Polyhedron_IndexedLine_t) * pTempPolyhedron->iLineCount) +
									(sizeof(Polyhedron_IndexedLineReference_t) * pTempPolyhedron->iIndexCount) +
									(sizeof(Polyhedron_IndexedPolygon_t) * pTempPolyhedron->iPolygonCount);

								Assert(memRequired < workSpaceSize);

								if (roomLeftInWorkSpace < memRequired)
								{
									usedSpaceInWorkspace[workSpacesAllocated - 1] = workSpaceSize - roomLeftInWorkSpace;

									if (workSpacesAllocated < iBrushWorkSpaces)
									{
										//re-use a workspace already allocated during brush polyhedron conversion
										pCurrentWorkSpace = workSpaceAllocations[workSpacesAllocated];
										usedSpaceInWorkspace[workSpacesAllocated] = 0;
									}
									else
									{
										//allocate a new workspace
										pCurrentWorkSpace = new uint8[workSpaceSize];
										workSpaceAllocations[workSpacesAllocated] = pCurrentWorkSpace;
										usedSpaceInWorkspace[workSpacesAllocated] = 0;
									}

									roomLeftInWorkSpace = workSpaceSize;
									++workSpacesAllocated;
								}

								CPolyhedron* pWorkSpacePolyhedron = CPolyhedron_LumpedMemory::AllocateAt(pCurrentWorkSpace,
									pTempPolyhedron->iVertexCount,
									pTempPolyhedron->iLineCount,
									pTempPolyhedron->iIndexCount,
									pTempPolyhedron->iPolygonCount);

								pCurrentWorkSpace += memRequired;
								roomLeftInWorkSpace -= memRequired;

								memcpy(pWorkSpacePolyhedron->pVertices, pTempPolyhedron->pVertices, pTempPolyhedron->iVertexCount * sizeof(Vector));
								memcpy(pWorkSpacePolyhedron->pLines, pTempPolyhedron->pLines, pTempPolyhedron->iLineCount * sizeof(Polyhedron_IndexedLine_t));
								memcpy(pWorkSpacePolyhedron->pIndices, pTempPolyhedron->pIndices, pTempPolyhedron->iIndexCount * sizeof(Polyhedron_IndexedLineReference_t));
								memcpy(pWorkSpacePolyhedron->pPolygons, pTempPolyhedron->pPolygons, pTempPolyhedron->iPolygonCount * sizeof(Polyhedron_IndexedPolygon_t));

								m_StaticPropPolyhedrons.AddToTail(pWorkSpacePolyhedron);

#ifdef _DEBUG
								CPhysConvex* pConvex = gEntList.PhysGetCollision()->ConvexFromConvexPolyhedron(*pTempPolyhedron);
								AssertMsg(pConvex != NULL, "Conversion from Convex to Polyhedron was unreversable");
								if (pConvex)
								{
									gEntList.PhysGetCollision()->ConvexFree(pConvex);
								}
#endif

								pTempPolyhedron->Release();
							}
						}
					}

					cacheInfo.iNumPolyhedrons = m_StaticPropPolyhedrons.Count() - cacheInfo.iStartIndex;
					cacheInfo.iStaticPropIndex = iStaticPropIndex;
					Assert(staticpropmgr->GetStaticPropByIndex(iStaticPropIndex) == pProp);

					m_CollideableIndicesMap.InsertOrReplace(pProp, cacheInfo);
				}

				++iStaticPropIndex;
				++pCollideables;
			} while (pCollideables != pStop);


			usedSpaceInWorkspace[workSpacesAllocated - 1] = workSpaceSize - roomLeftInWorkSpace;

			if (usedSpaceInWorkspace[0] != 0) //At least a little bit of memory was used.
			{
				//consolidate workspaces into a single memory chunk
				size_t totalMemoryNeeded = 0;
				for (unsigned int i = 0; i != workSpacesAllocated; ++i)
				{
					totalMemoryNeeded += usedSpaceInWorkspace[i];
				}

				uint8* pFinalDest = new uint8[totalMemoryNeeded];
				s_StaticPropPolyhedronMemory = pFinalDest;

				DevMsg(2, "CStaticCollisionPolyhedronCache: Used %.2f KB to cache %d static prop polyhedrons.\n", ((float)totalMemoryNeeded) / 1024.0f, m_StaticPropPolyhedrons.Count());

				int iCount = m_StaticPropPolyhedrons.Count();
				for (int i = 0; i != iCount; ++i)
				{
					CPolyhedron_LumpedMemory* pSource = (CPolyhedron_LumpedMemory*)m_StaticPropPolyhedrons[i];

					size_t memRequired = (sizeof(CPolyhedron_LumpedMemory)) +
						(sizeof(Vector) * pSource->iVertexCount) +
						(sizeof(Polyhedron_IndexedLine_t) * pSource->iLineCount) +
						(sizeof(Polyhedron_IndexedLineReference_t) * pSource->iIndexCount) +
						(sizeof(Polyhedron_IndexedPolygon_t) * pSource->iPolygonCount);

					CPolyhedron_LumpedMemory* pDest = (CPolyhedron_LumpedMemory*)pFinalDest;
					m_StaticPropPolyhedrons[i] = pDest;
					pFinalDest += memRequired;

					intp memoryOffset = ((uint8*)pDest) - ((uint8*)pSource);

					memcpy(pDest, pSource, memRequired);
					//move all the pointers to their new location.
					pDest->pVertices = (Vector*)(((uint8*)(pDest->pVertices)) + memoryOffset);
					pDest->pLines = (Polyhedron_IndexedLine_t*)(((uint8*)(pDest->pLines)) + memoryOffset);
					pDest->pIndices = (Polyhedron_IndexedLineReference_t*)(((uint8*)(pDest->pIndices)) + memoryOffset);
					pDest->pPolygons = (Polyhedron_IndexedPolygon_t*)(((uint8*)(pDest->pPolygons)) + memoryOffset);
				}
			}
		}
	}

	if (iBrushWorkSpaces > workSpacesAllocated)
		workSpacesAllocated = iBrushWorkSpaces;

	for (unsigned int i = 0; i != workSpacesAllocated; ++i)
	{
		delete[]workSpaceAllocations[i];
	}
}



const CPolyhedron* CStaticCollisionPolyhedronCache::GetBrushPolyhedron(int iBrushNumber)
{
	Assert(iBrushNumber < m_BrushPolyhedrons.Count());

	if ((iBrushNumber < 0) || (iBrushNumber >= m_BrushPolyhedrons.Count()))
		return NULL;

	return m_BrushPolyhedrons[iBrushNumber];
}

int CStaticCollisionPolyhedronCache::GetStaticPropPolyhedrons(ICollideable* pStaticProp, CPolyhedron** pOutputPolyhedronArray, int iOutputArraySize)
{
	unsigned short iPropIndex = m_CollideableIndicesMap.Find(pStaticProp);
	if (!m_CollideableIndicesMap.IsValidIndex(iPropIndex)) //static prop never made it into the cache for some reason (specifically no collision data when this workaround was written)
		return 0;

	StaticPropPolyhedronCacheInfo_t cacheInfo = m_CollideableIndicesMap.Element(iPropIndex);

	if (cacheInfo.iNumPolyhedrons < iOutputArraySize)
		iOutputArraySize = cacheInfo.iNumPolyhedrons;

	for (int i = cacheInfo.iStartIndex, iWriteIndex = 0; iWriteIndex != iOutputArraySize; ++i, ++iWriteIndex)
	{
		pOutputPolyhedronArray[iWriteIndex] = m_StaticPropPolyhedrons[i];
	}

	return iOutputArraySize;
}

CStaticCollisionPolyhedronCache g_StaticCollisionPolyhedronCache;



static void ConvertBrushListToClippedPolyhedronList(const int* pBrushes, int iBrushCount, const float* pOutwardFacingClipPlanes, int iClipPlaneCount, float fClipEpsilon, CUtlVector<CPolyhedron*>* pPolyhedronList)
{
	if (pPolyhedronList == NULL)
		return;

	if ((pBrushes == NULL) || (iBrushCount == 0))
		return;

	for (int i = 0; i != iBrushCount; ++i)
	{
		CPolyhedron* pPolyhedron = ClipPolyhedron(g_StaticCollisionPolyhedronCache.GetBrushPolyhedron(pBrushes[i]), pOutwardFacingClipPlanes, iClipPlaneCount, fClipEpsilon);
		if (pPolyhedron)
			pPolyhedronList->AddToTail(pPolyhedron);
	}
}

static void ClipPolyhedrons(CPolyhedron* const* pExistingPolyhedrons, int iPolyhedronCount, const float* pOutwardFacingClipPlanes, int iClipPlaneCount, float fClipEpsilon, CUtlVector<CPolyhedron*>* pPolyhedronList)
{
	if (pPolyhedronList == NULL)
		return;

	if ((pExistingPolyhedrons == NULL) || (iPolyhedronCount == 0))
		return;

	for (int i = 0; i != iPolyhedronCount; ++i)
	{
		CPolyhedron* pPolyhedron = ClipPolyhedron(pExistingPolyhedrons[i], pOutwardFacingClipPlanes, iClipPlaneCount, fClipEpsilon);
		if (pPolyhedron)
			pPolyhedronList->AddToTail(pPolyhedron);
	}
}

void CEnginePortalInternal::CreatePolyhedrons(void)
{
	//forward reverse conventions signify whether the normal is the same direction as m_InternalData.Placement.PortalPlane.m_Normal
//World and wall conventions signify whether it's been shifted in front of the portal plane or behind it

	float fWorldClipPlane_Forward[4] = { m_InternalData.Placement.PortalPlane.normal.x,
											m_InternalData.Placement.PortalPlane.normal.y,
											m_InternalData.Placement.PortalPlane.normal.z,
											m_InternalData.Placement.PortalPlane.dist + PORTAL_WORLD_WALL_HALF_SEPARATION_AMOUNT };

	float fWorldClipPlane_Reverse[4] = { -fWorldClipPlane_Forward[0],
											-fWorldClipPlane_Forward[1],
											-fWorldClipPlane_Forward[2],
											-fWorldClipPlane_Forward[3] };

	float fWallClipPlane_Forward[4] = { m_InternalData.Placement.PortalPlane.normal.x,
											m_InternalData.Placement.PortalPlane.normal.y,
											m_InternalData.Placement.PortalPlane.normal.z,
											m_InternalData.Placement.PortalPlane.dist }; // - PORTAL_WORLD_WALL_HALF_SEPARATION_AMOUNT

	//float fWallClipPlane_Reverse[4] = {		-fWallClipPlane_Forward[0],
	//										-fWallClipPlane_Forward[1],
	//										-fWallClipPlane_Forward[2],
	//										-fWallClipPlane_Forward[3] };


	//World
	{
		Vector vOBBForward = m_InternalData.Placement.vForward;
		Vector vOBBRight = m_InternalData.Placement.vRight;
		Vector vOBBUp = m_InternalData.Placement.vUp;


		//scale the extents to usable sizes
		float flScaleX = sv_portal_collision_sim_bounds_x.GetFloat();
		if (flScaleX < 200.0f)
			flScaleX = 200.0f;
		float flScaleY = sv_portal_collision_sim_bounds_y.GetFloat();
		if (flScaleY < 200.0f)
			flScaleY = 200.0f;
		float flScaleZ = sv_portal_collision_sim_bounds_z.GetFloat();
		if (flScaleZ < 252.0f)
			flScaleZ = 252.0f;

		vOBBForward *= flScaleX;
		vOBBRight *= flScaleY;
		vOBBUp *= flScaleZ;	// default size for scale z (252) is player (height + portal half height) * 2. Any smaller than this will allow for players to 
		// reach unsimulated geometry before an end touch with teh portal.

		Vector ptOBBOrigin = GetAbsOrigin();
		ptOBBOrigin -= vOBBRight / 2.0f;
		ptOBBOrigin -= vOBBUp / 2.0f;

		Vector vAABBMins, vAABBMaxs;
		vAABBMins = vAABBMaxs = ptOBBOrigin;

		for (int i = 1; i != 8; ++i)
		{
			Vector ptTest = ptOBBOrigin;
			if (i & (1 << 0)) ptTest += vOBBForward;
			if (i & (1 << 1)) ptTest += vOBBRight;
			if (i & (1 << 2)) ptTest += vOBBUp;

			if (ptTest.x < vAABBMins.x) vAABBMins.x = ptTest.x;
			if (ptTest.y < vAABBMins.y) vAABBMins.y = ptTest.y;
			if (ptTest.z < vAABBMins.z) vAABBMins.z = ptTest.z;
			if (ptTest.x > vAABBMaxs.x) vAABBMaxs.x = ptTest.x;
			if (ptTest.y > vAABBMaxs.y) vAABBMaxs.y = ptTest.y;
			if (ptTest.z > vAABBMaxs.z) vAABBMaxs.z = ptTest.z;
		}

		//Brushes
		{
			Assert(m_InternalData.Simulation.Static.World.Brushes.Polyhedrons.Count() == 0);

			CUtlVector<int> WorldBrushes;
			enginetrace->GetBrushesInAABB(vAABBMins, vAABBMaxs, &WorldBrushes, MASK_SOLID_BRUSHONLY | CONTENTS_PLAYERCLIP | CONTENTS_MONSTERCLIP);

			//create locally clipped polyhedrons for the world
			{
				int* pBrushList = WorldBrushes.Base();
				int iBrushCount = WorldBrushes.Count();
				ConvertBrushListToClippedPolyhedronList(pBrushList, iBrushCount, fWorldClipPlane_Reverse, 1, PORTAL_POLYHEDRON_CUT_EPSILON, &m_InternalData.Simulation.Static.World.Brushes.Polyhedrons);
			}
		}

		//static props
		{
			Assert(m_InternalData.Simulation.Static.World.StaticProps.Polyhedrons.Count() == 0);

			CUtlVector<ICollideable*> StaticProps;
			staticpropmgr->GetAllStaticPropsInAABB(vAABBMins, vAABBMaxs, &StaticProps);

			for (int i = StaticProps.Count(); --i >= 0; )
			{
				ICollideable* pProp = StaticProps[i];

				CPolyhedron* PolyhedronArray[1024];
				int iPolyhedronCount = g_StaticCollisionPolyhedronCache.GetStaticPropPolyhedrons(pProp, PolyhedronArray, 1024);

				StaticPropPolyhedronGroups_t indices;
				indices.iStartIndex = m_InternalData.Simulation.Static.World.StaticProps.Polyhedrons.Count();

				for (int j = 0; j != iPolyhedronCount; ++j)
				{
					CPolyhedron* pPropPolyhedronPiece = PolyhedronArray[j];
					if (pPropPolyhedronPiece)
					{
						CPolyhedron* pClippedPropPolyhedron = ClipPolyhedron(pPropPolyhedronPiece, fWorldClipPlane_Reverse, 1, 0.01f, false);
						if (pClippedPropPolyhedron)
							m_InternalData.Simulation.Static.World.StaticProps.Polyhedrons.AddToTail(pClippedPropPolyhedron);
					}
				}

				indices.iNumPolyhedrons = m_InternalData.Simulation.Static.World.StaticProps.Polyhedrons.Count() - indices.iStartIndex;
				if (indices.iNumPolyhedrons != 0)
				{
					int index = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.AddToTail();
					PS_SD_Static_World_StaticProps_ClippedProp_t& NewEntry = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[index];

					NewEntry.PolyhedronGroup = indices;
					NewEntry.pCollide = NULL;
#ifndef CLIENT_DLL
					NewEntry.pPhysicsObject = NULL;
#endif
					NewEntry.pSourceProp = pProp->GetEntityHandle();

					const model_t* pModel = pProp->GetCollisionModel();
					bool bIsStudioModel = pModel && (modelinfo->GetModelType(pModel) == mod_studio);
					AssertOnce(bIsStudioModel);
					if (bIsStudioModel)
					{
						IStudioHdr* pStudioHdr = modelinfo->GetStudiomodel(pModel);
						Assert(pStudioHdr != NULL);
						NewEntry.iTraceContents = pStudioHdr->contents();
						NewEntry.iTraceSurfaceProps = gEntList.PhysGetProps()->GetSurfaceIndex(pStudioHdr->pszSurfaceProp());
					}
					else
					{
						NewEntry.iTraceContents = m_InternalData.Simulation.Static.SurfaceProperties.contents;
						NewEntry.iTraceSurfaceProps = m_InternalData.Simulation.Static.SurfaceProperties.surface.surfaceProps;
					}
				}
			}
		}
	}



	//(Holy) Wall
	{
		Assert(m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.Count() == 0);
		Assert(m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons.Count() == 0);

		Vector vBackward = -m_InternalData.Placement.vForward;
		Vector vLeft = -m_InternalData.Placement.vRight;
		Vector vDown = -m_InternalData.Placement.vUp;

		Vector vOBBForward = -m_InternalData.Placement.vForward;
		Vector vOBBRight = -m_InternalData.Placement.vRight;
		Vector vOBBUp = m_InternalData.Placement.vUp;

		//scale the extents to usable sizes
		vOBBForward *= PORTAL_WALL_FARDIST / 2.0f;
		vOBBRight *= PORTAL_WALL_FARDIST * 2.0f;
		vOBBUp *= PORTAL_WALL_FARDIST * 2.0f;

		Vector ptOBBOrigin = GetAbsOrigin();
		ptOBBOrigin -= vOBBRight / 2.0f;
		ptOBBOrigin -= vOBBUp / 2.0f;

		Vector vAABBMins, vAABBMaxs;
		vAABBMins = vAABBMaxs = ptOBBOrigin;

		for (int i = 1; i != 8; ++i)
		{
			Vector ptTest = ptOBBOrigin;
			if (i & (1 << 0)) ptTest += vOBBForward;
			if (i & (1 << 1)) ptTest += vOBBRight;
			if (i & (1 << 2)) ptTest += vOBBUp;

			if (ptTest.x < vAABBMins.x) vAABBMins.x = ptTest.x;
			if (ptTest.y < vAABBMins.y) vAABBMins.y = ptTest.y;
			if (ptTest.z < vAABBMins.z) vAABBMins.z = ptTest.z;
			if (ptTest.x > vAABBMaxs.x) vAABBMaxs.x = ptTest.x;
			if (ptTest.y > vAABBMaxs.y) vAABBMaxs.y = ptTest.y;
			if (ptTest.z > vAABBMaxs.z) vAABBMaxs.z = ptTest.z;
		}


		float fPlanes[6 * 4];

		//first and second planes are always forward and backward planes
		fPlanes[(0 * 4) + 0] = fWallClipPlane_Forward[0];
		fPlanes[(0 * 4) + 1] = fWallClipPlane_Forward[1];
		fPlanes[(0 * 4) + 2] = fWallClipPlane_Forward[2];
		fPlanes[(0 * 4) + 3] = fWallClipPlane_Forward[3] - PORTAL_WALL_TUBE_OFFSET;

		fPlanes[(1 * 4) + 0] = vBackward.x;
		fPlanes[(1 * 4) + 1] = vBackward.y;
		fPlanes[(1 * 4) + 2] = vBackward.z;
		float fTubeDepthDist = vBackward.Dot(GetAbsOrigin() + (vBackward * (PORTAL_WALL_TUBE_DEPTH + PORTAL_WALL_TUBE_OFFSET)));
		fPlanes[(1 * 4) + 3] = fTubeDepthDist;


		//the remaining planes will always have the same ordering of normals, with different distances plugged in for each convex we're creating
		//normal order is up, down, left, right

		fPlanes[(2 * 4) + 0] = m_InternalData.Placement.vUp.x;
		fPlanes[(2 * 4) + 1] = m_InternalData.Placement.vUp.y;
		fPlanes[(2 * 4) + 2] = m_InternalData.Placement.vUp.z;
		fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(GetAbsOrigin() + (m_InternalData.Placement.vUp * PORTAL_HOLE_HALF_HEIGHT));

		fPlanes[(3 * 4) + 0] = vDown.x;
		fPlanes[(3 * 4) + 1] = vDown.y;
		fPlanes[(3 * 4) + 2] = vDown.z;
		fPlanes[(3 * 4) + 3] = vDown.Dot(GetAbsOrigin() + (vDown * PORTAL_HOLE_HALF_HEIGHT));

		fPlanes[(4 * 4) + 0] = vLeft.x;
		fPlanes[(4 * 4) + 1] = vLeft.y;
		fPlanes[(4 * 4) + 2] = vLeft.z;
		fPlanes[(4 * 4) + 3] = vLeft.Dot(GetAbsOrigin() + (vLeft * PORTAL_HOLE_HALF_WIDTH));

		fPlanes[(5 * 4) + 0] = m_InternalData.Placement.vRight.x;
		fPlanes[(5 * 4) + 1] = m_InternalData.Placement.vRight.y;
		fPlanes[(5 * 4) + 2] = m_InternalData.Placement.vRight.z;
		fPlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(GetAbsOrigin() + (m_InternalData.Placement.vRight * PORTAL_HOLE_HALF_WIDTH));

		float* fSidePlanesOnly = &fPlanes[(2 * 4)];

		//these 2 get re-used a bit
		float fFarRightPlaneDistance = m_InternalData.Placement.vRight.Dot(GetAbsOrigin() + m_InternalData.Placement.vRight * (PORTAL_WALL_FARDIST * 10.0f));
		float fFarLeftPlaneDistance = vLeft.Dot(GetAbsOrigin() + vLeft * (PORTAL_WALL_FARDIST * 10.0f));


		CUtlVector<int> WallBrushes;
		CUtlVector<CPolyhedron*> WallBrushPolyhedrons_ClippedToWall;
		CPolyhedron** pWallClippedPolyhedrons = NULL;
		int iWallClippedPolyhedronCount = 0;
		//if (m_pOwningSimulator->IsSimulatingVPhysics()) //if not simulating vphysics, we skip making the entire wall, and just create the minimal tube instead
		{
			enginetrace->GetBrushesInAABB(vAABBMins, vAABBMaxs, &WallBrushes, MASK_SOLID_BRUSHONLY);

			if (WallBrushes.Count() != 0)
				ConvertBrushListToClippedPolyhedronList(WallBrushes.Base(), WallBrushes.Count(), fPlanes, 1, PORTAL_POLYHEDRON_CUT_EPSILON, &WallBrushPolyhedrons_ClippedToWall);

			if (WallBrushPolyhedrons_ClippedToWall.Count() != 0)
			{
				for (int i = WallBrushPolyhedrons_ClippedToWall.Count(); --i >= 0; )
				{
					CPolyhedron* pPolyhedron = ClipPolyhedron(WallBrushPolyhedrons_ClippedToWall[i], fSidePlanesOnly, 4, PORTAL_POLYHEDRON_CUT_EPSILON, true);
					if (pPolyhedron)
					{
						//a chunk of this brush passes through the hole, not eligible to be removed from cutting
						pPolyhedron->Release();
					}
					else
					{
						//no part of this brush interacts with the hole, no point in cutting the brush any later
						m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons.AddToTail(WallBrushPolyhedrons_ClippedToWall[i]);
						WallBrushPolyhedrons_ClippedToWall.FastRemove(i);
					}
				}

				if (WallBrushPolyhedrons_ClippedToWall.Count() != 0) //might have become 0 while removing uncut brushes
				{
					pWallClippedPolyhedrons = WallBrushPolyhedrons_ClippedToWall.Base();
					iWallClippedPolyhedronCount = WallBrushPolyhedrons_ClippedToWall.Count();
				}
			}
		}


		//upper wall
		{
			//minimal portion that extends into the hole space
			//fPlanes[(1*4) + 3] = fTubeDepthDist;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(GetAbsOrigin() + m_InternalData.Placement.vUp * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS));
			fPlanes[(3 * 4) + 3] = vDown.Dot(GetAbsOrigin() + m_InternalData.Placement.vUp * PORTAL_HOLE_HALF_HEIGHT);
			fPlanes[(4 * 4) + 3] = vLeft.Dot(GetAbsOrigin() + vLeft * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS));
			fPlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(GetAbsOrigin() + m_InternalData.Placement.vRight * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS));

			CPolyhedron* pTubePolyhedron = GeneratePolyhedronFromPlanes(fPlanes, 6, PORTAL_POLYHEDRON_CUT_EPSILON);
			if (pTubePolyhedron)
				m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.AddToTail(pTubePolyhedron);

			//general hole cut
			//fPlanes[(1*4) + 3] += 2000.0f;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(GetAbsOrigin() + m_InternalData.Placement.vUp * (PORTAL_WALL_FARDIST * 10.0f));
			fPlanes[(3 * 4) + 3] = vDown.Dot(GetAbsOrigin() + m_InternalData.Placement.vUp * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS));
			fPlanes[(4 * 4) + 3] = fFarLeftPlaneDistance;
			fPlanes[(5 * 4) + 3] = fFarRightPlaneDistance;



			ClipPolyhedrons(pWallClippedPolyhedrons, iWallClippedPolyhedronCount, fSidePlanesOnly, 4, PORTAL_POLYHEDRON_CUT_EPSILON, &m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons);
		}

		//lower wall
		{
			//minimal portion that extends into the hole space
			//fPlanes[(1*4) + 3] = fTubeDepthDist;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(GetAbsOrigin() + (vDown * PORTAL_HOLE_HALF_HEIGHT));
			fPlanes[(3 * 4) + 3] = vDown.Dot(GetAbsOrigin() + vDown * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS));
			fPlanes[(4 * 4) + 3] = vLeft.Dot(GetAbsOrigin() + vLeft * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS));
			fPlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(GetAbsOrigin() + m_InternalData.Placement.vRight * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS));

			CPolyhedron* pTubePolyhedron = GeneratePolyhedronFromPlanes(fPlanes, 6, PORTAL_POLYHEDRON_CUT_EPSILON);
			if (pTubePolyhedron)
				m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.AddToTail(pTubePolyhedron);

			//general hole cut
			//fPlanes[(1*4) + 3] += 2000.0f;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(GetAbsOrigin() + (vDown * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS)));
			fPlanes[(3 * 4) + 3] = vDown.Dot(GetAbsOrigin() + (vDown * (PORTAL_WALL_FARDIST * 10.0f)));
			fPlanes[(4 * 4) + 3] = fFarLeftPlaneDistance;
			fPlanes[(5 * 4) + 3] = fFarRightPlaneDistance;

			ClipPolyhedrons(pWallClippedPolyhedrons, iWallClippedPolyhedronCount, fSidePlanesOnly, 4, PORTAL_POLYHEDRON_CUT_EPSILON, &m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons);
		}

		//left wall
		{
			//minimal portion that extends into the hole space
			//fPlanes[(1*4) + 3] = fTubeDepthDist;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(GetAbsOrigin() + (m_InternalData.Placement.vUp * PORTAL_HOLE_HALF_HEIGHT));
			fPlanes[(3 * 4) + 3] = vDown.Dot(GetAbsOrigin() + (vDown * PORTAL_HOLE_HALF_HEIGHT));
			fPlanes[(4 * 4) + 3] = vLeft.Dot(GetAbsOrigin() + (vLeft * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS)));
			fPlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(GetAbsOrigin() + (vLeft * PORTAL_HOLE_HALF_WIDTH));

			CPolyhedron* pTubePolyhedron = GeneratePolyhedronFromPlanes(fPlanes, 6, PORTAL_POLYHEDRON_CUT_EPSILON);
			if (pTubePolyhedron)
				m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.AddToTail(pTubePolyhedron);

			//general hole cut
			//fPlanes[(1*4) + 3] += 2000.0f;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(GetAbsOrigin() + (m_InternalData.Placement.vUp * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS)));
			fPlanes[(3 * 4) + 3] = vDown.Dot(GetAbsOrigin() - (m_InternalData.Placement.vUp * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS)));
			fPlanes[(4 * 4) + 3] = fFarLeftPlaneDistance;
			fPlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(GetAbsOrigin() + (vLeft * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS)));

			ClipPolyhedrons(pWallClippedPolyhedrons, iWallClippedPolyhedronCount, fSidePlanesOnly, 4, PORTAL_POLYHEDRON_CUT_EPSILON, &m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons);
		}

		//right wall
		{
			//minimal portion that extends into the hole space
			//fPlanes[(1*4) + 3] = fTubeDepthDist;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(GetAbsOrigin() + (m_InternalData.Placement.vUp * (PORTAL_HOLE_HALF_HEIGHT)));
			fPlanes[(3 * 4) + 3] = vDown.Dot(GetAbsOrigin() + (vDown * (PORTAL_HOLE_HALF_HEIGHT)));
			fPlanes[(4 * 4) + 3] = vLeft.Dot(GetAbsOrigin() + m_InternalData.Placement.vRight * PORTAL_HOLE_HALF_WIDTH);
			fPlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(GetAbsOrigin() + m_InternalData.Placement.vRight * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS));

			CPolyhedron* pTubePolyhedron = GeneratePolyhedronFromPlanes(fPlanes, 6, PORTAL_POLYHEDRON_CUT_EPSILON);
			if (pTubePolyhedron)
				m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.AddToTail(pTubePolyhedron);

			//general hole cut
			//fPlanes[(1*4) + 3] += 2000.0f;
			fPlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(GetAbsOrigin() + (m_InternalData.Placement.vUp * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS)));
			fPlanes[(3 * 4) + 3] = vDown.Dot(GetAbsOrigin() + (vDown * (PORTAL_HOLE_HALF_HEIGHT + PORTAL_WALL_MIN_THICKNESS)));
			fPlanes[(4 * 4) + 3] = vLeft.Dot(GetAbsOrigin() + m_InternalData.Placement.vRight * (PORTAL_HOLE_HALF_WIDTH + PORTAL_WALL_MIN_THICKNESS));
			fPlanes[(5 * 4) + 3] = fFarRightPlaneDistance;

			ClipPolyhedrons(pWallClippedPolyhedrons, iWallClippedPolyhedronCount, fSidePlanesOnly, 4, PORTAL_POLYHEDRON_CUT_EPSILON, &m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons);
		}

		for (int i = WallBrushPolyhedrons_ClippedToWall.Count(); --i >= 0; )
			WallBrushPolyhedrons_ClippedToWall[i]->Release();

		WallBrushPolyhedrons_ClippedToWall.RemoveAll();
	}
}

void CEnginePortalInternal::ClearPolyhedrons(void)
{
	if (m_InternalData.Simulation.Static.World.Brushes.Polyhedrons.Count() != 0)
	{
		for (int i = m_InternalData.Simulation.Static.World.Brushes.Polyhedrons.Count(); --i >= 0; )
			m_InternalData.Simulation.Static.World.Brushes.Polyhedrons[i]->Release();

		m_InternalData.Simulation.Static.World.Brushes.Polyhedrons.RemoveAll();
	}

	if (m_InternalData.Simulation.Static.World.StaticProps.Polyhedrons.Count() != 0)
	{
		for (int i = m_InternalData.Simulation.Static.World.StaticProps.Polyhedrons.Count(); --i >= 0; )
			m_InternalData.Simulation.Static.World.StaticProps.Polyhedrons[i]->Release();

		m_InternalData.Simulation.Static.World.StaticProps.Polyhedrons.RemoveAll();
	}
#ifdef _DEBUG
	for (int i = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
	{
#ifndef CLIENT_DLL
		Assert(m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[i].pPhysicsObject == NULL);
#endif
		Assert(m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[i].pCollide == NULL);
	}
#endif
	m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.RemoveAll();

	if (m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons.Count() != 0)
	{
		for (int i = m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons.Count(); --i >= 0; )
			m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons[i]->Release();

		m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons.RemoveAll();
	}

	if (m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.Count() != 0)
	{
		for (int i = m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.Count(); --i >= 0; )
			m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons[i]->Release();

		m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.RemoveAll();
	}
}

static CPhysCollide* ConvertPolyhedronsToCollideable(CPolyhedron** pPolyhedrons, int iPolyhedronCount)
{
	if ((pPolyhedrons == NULL) || (iPolyhedronCount == 0))
		return NULL;

	CREATEDEBUGTIMER(functionTimer);

	STARTDEBUGTIMER(functionTimer);
	DEBUGTIMERONLY(DevMsg(2, "[PSDT:%d] %sConvertPolyhedronsToCollideable() START\n", s_iPortalSimulatorGUID, TABSPACING); );
	INCREMENTTABSPACING();

	CPhysConvex** pConvexes = (CPhysConvex**)stackalloc(iPolyhedronCount * sizeof(CPhysConvex*));
	int iConvexCount = 0;

	CREATEDEBUGTIMER(convexTimer);
	STARTDEBUGTIMER(convexTimer);
	for (int i = 0; i != iPolyhedronCount; ++i)
	{
		pConvexes[iConvexCount] = gEntList.PhysGetCollision()->ConvexFromConvexPolyhedron(*pPolyhedrons[i]);

		Assert(pConvexes[iConvexCount] != NULL);

		if (pConvexes[iConvexCount])
			++iConvexCount;
	}
	STOPDEBUGTIMER(convexTimer);
	DEBUGTIMERONLY(DevMsg(2, "[PSDT:%d] %sConvex Generation:%fms\n", s_iPortalSimulatorGUID, TABSPACING, convexTimer.GetDuration().GetMillisecondsF()); );


	CPhysCollide* pReturn;
	if (iConvexCount != 0)
	{
		CREATEDEBUGTIMER(collideTimer);
		STARTDEBUGTIMER(collideTimer);
		pReturn = gEntList.PhysGetCollision()->ConvertConvexToCollide(pConvexes, iConvexCount);
		STOPDEBUGTIMER(collideTimer);
		DEBUGTIMERONLY(DevMsg(2, "[PSDT:%d] %sCollideable Generation:%fms\n", s_iPortalSimulatorGUID, TABSPACING, collideTimer.GetDuration().GetMillisecondsF()); );
	}
	else
	{
		pReturn = NULL;
	}

	STOPDEBUGTIMER(functionTimer);
	DECREMENTTABSPACING();
	DEBUGTIMERONLY(DevMsg(2, "[PSDT:%d] %sConvertPolyhedronsToCollideable() FINISH: %fms\n", s_iPortalSimulatorGUID, TABSPACING, functionTimer.GetDuration().GetMillisecondsF()); );

	return pReturn;
}

void CEnginePortalInternal::CreateLocalCollision(void)
{
	CREATEDEBUGTIMER(worldBrushTimer);
	STARTDEBUGTIMER(worldBrushTimer);
	Assert(m_InternalData.Simulation.Static.World.Brushes.pCollideable == NULL); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
	if (m_InternalData.Simulation.Static.World.Brushes.Polyhedrons.Count() != 0)
		m_InternalData.Simulation.Static.World.Brushes.pCollideable = ConvertPolyhedronsToCollideable(m_InternalData.Simulation.Static.World.Brushes.Polyhedrons.Base(), m_InternalData.Simulation.Static.World.Brushes.Polyhedrons.Count());
	STOPDEBUGTIMER(worldBrushTimer);
	DEBUGTIMERONLY(DevMsg(2, "[PSDT:%d] %sWorld Brushes=%fms\n", GetPortalSimulatorGUID(), TABSPACING, worldBrushTimer.GetDuration().GetMillisecondsF()); );

	CREATEDEBUGTIMER(worldPropTimer);
	STARTDEBUGTIMER(worldPropTimer);
#ifdef _DEBUG
	for (int i = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
	{
		Assert(m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[i].pCollide == NULL);
	}
#endif
	Assert(m_InternalData.Simulation.Static.World.StaticProps.bCollisionExists == false); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
	if (m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count() != 0)
	{
		Assert(m_InternalData.Simulation.Static.World.StaticProps.Polyhedrons.Count() != 0);
		CPolyhedron** pPolyhedronsBase = m_InternalData.Simulation.Static.World.StaticProps.Polyhedrons.Base();
		for (int i = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
		{
			PS_SD_Static_World_StaticProps_ClippedProp_t& Representation = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[i];

			Assert(Representation.pCollide == NULL);
			Representation.pCollide = ConvertPolyhedronsToCollideable(&pPolyhedronsBase[Representation.PolyhedronGroup.iStartIndex], Representation.PolyhedronGroup.iNumPolyhedrons);
			Assert(Representation.pCollide != NULL);
		}
	}
	m_InternalData.Simulation.Static.World.StaticProps.bCollisionExists = true;
	STOPDEBUGTIMER(worldPropTimer);
	DEBUGTIMERONLY(DevMsg(2, "[PSDT:%d] %sWorld Props=%fms\n", GetPortalSimulatorGUID(), TABSPACING, worldPropTimer.GetDuration().GetMillisecondsF()); );

	//if (m_pOwningSimulator->IsSimulatingVPhysics())
	{
		//only need the tube when simulating player movement

		//TODO: replace the complete wall with the wall shell
		CREATEDEBUGTIMER(wallBrushTimer);
		STARTDEBUGTIMER(wallBrushTimer);
		Assert(m_InternalData.Simulation.Static.Wall.Local.Brushes.pCollideable == NULL); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
		if (m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons.Count() != 0)
			m_InternalData.Simulation.Static.Wall.Local.Brushes.pCollideable = ConvertPolyhedronsToCollideable(m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons.Base(), m_InternalData.Simulation.Static.Wall.Local.Brushes.Polyhedrons.Count());
		STOPDEBUGTIMER(wallBrushTimer);
		DEBUGTIMERONLY(DevMsg(2, "[PSDT:%d] %sWall Brushes=%fms\n", GetPortalSimulatorGUID(), TABSPACING, wallBrushTimer.GetDuration().GetMillisecondsF()); );
	}

	CREATEDEBUGTIMER(wallTubeTimer);
	STARTDEBUGTIMER(wallTubeTimer);
	Assert(m_InternalData.Simulation.Static.Wall.Local.Tube.pCollideable == NULL); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
	if (m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.Count() != 0)
		m_InternalData.Simulation.Static.Wall.Local.Tube.pCollideable = ConvertPolyhedronsToCollideable(m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.Base(), m_InternalData.Simulation.Static.Wall.Local.Tube.Polyhedrons.Count());
	STOPDEBUGTIMER(wallTubeTimer);
	DEBUGTIMERONLY(DevMsg(2, "[PSDT:%d] %sWall Tube=%fms\n", GetPortalSimulatorGUID(), TABSPACING, wallTubeTimer.GetDuration().GetMillisecondsF()); );

	//grab surface properties to use for the portal environment
	{
		CTraceFilterWorldAndPropsOnly filter;
		trace_t Trace;
		UTIL_TraceLine(GetAbsOrigin() + m_InternalData.Placement.vForward, GetAbsOrigin() - (m_InternalData.Placement.vForward * 500.0f), MASK_SOLID_BRUSHONLY, &filter, &Trace);

		if (Trace.fraction != 1.0f)
		{
			m_InternalData.Simulation.Static.SurfaceProperties.contents = Trace.contents;
			m_InternalData.Simulation.Static.SurfaceProperties.surface = Trace.surface;
			m_InternalData.Simulation.Static.SurfaceProperties.pEntity = (CBaseEntity*)Trace.m_pEnt;
		}
		else
		{
			m_InternalData.Simulation.Static.SurfaceProperties.contents = CONTENTS_SOLID;
			m_InternalData.Simulation.Static.SurfaceProperties.surface.name = "**empty**";
			m_InternalData.Simulation.Static.SurfaceProperties.surface.flags = 0;
			m_InternalData.Simulation.Static.SurfaceProperties.surface.surfaceProps = 0;
#ifndef CLIENT_DLL
			m_InternalData.Simulation.Static.SurfaceProperties.pEntity = gEntList.GetBaseEntity(0);
#else
			m_InternalData.Simulation.Static.SurfaceProperties.pEntity = ClientEntityList().GetBaseEntity(0);
#endif
		}

#ifndef CLIENT_DLL
		//if( pCollisionEntity )
		m_InternalData.Simulation.Static.SurfaceProperties.pEntity = this->m_pOuter;
#endif		
	}
}

void CEnginePortalInternal::ClearLocalCollision(void)
{
	if (m_InternalData.Simulation.Static.Wall.Local.Brushes.pCollideable)
	{
		gEntList.PhysGetCollision()->DestroyCollide(m_InternalData.Simulation.Static.Wall.Local.Brushes.pCollideable);
		m_InternalData.Simulation.Static.Wall.Local.Brushes.pCollideable = NULL;
	}

	if (m_InternalData.Simulation.Static.Wall.Local.Tube.pCollideable)
	{
		gEntList.PhysGetCollision()->DestroyCollide(m_InternalData.Simulation.Static.Wall.Local.Tube.pCollideable);
		m_InternalData.Simulation.Static.Wall.Local.Tube.pCollideable = NULL;
	}

	if (m_InternalData.Simulation.Static.World.Brushes.pCollideable)
	{
		gEntList.PhysGetCollision()->DestroyCollide(m_InternalData.Simulation.Static.World.Brushes.pCollideable);
		m_InternalData.Simulation.Static.World.Brushes.pCollideable = NULL;
	}

	if (m_InternalData.Simulation.Static.World.StaticProps.bCollisionExists &&
		(m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count() != 0))
	{
		for (int i = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
		{
			PS_SD_Static_World_StaticProps_ClippedProp_t& Representation = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[i];
			if (Representation.pCollide)
			{
				gEntList.PhysGetCollision()->DestroyCollide(Representation.pCollide);
				Representation.pCollide = NULL;
			}
		}
	}
	m_InternalData.Simulation.Static.World.StaticProps.bCollisionExists = false;
}

void CEnginePortalInternal::CreateLocalPhysics(void)
{
	//int iDefaultSurfaceIndex = physprops->GetSurfaceIndex( "default" );
	objectparams_t params = gEntList.PhysGetDefaultObjectParams();

	// Any non-moving object can point to world safely-- Make sure we dont use 'params' for something other than that beyond this point.
	//if( m_InternalData.Simulation.pCollisionEntity )
	params.pGameData = this->m_pOuter;
	//else
	//	GetWorldEntity();

	//World
	{
		Assert(m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject == NULL); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
		if (m_InternalData.Simulation.Static.World.Brushes.pCollideable != NULL)
		{
			m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject = m_pPhysicsEnvironment->CreatePolyObjectStatic(m_InternalData.Simulation.Static.World.Brushes.pCollideable, m_InternalData.Simulation.Static.SurfaceProperties.surface.surfaceProps, vec3_origin, vec3_angle, &params);

			if (VPhysicsGetObject() == NULL)
				VPhysicsSetObject(m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject);

			m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject->RecheckCollisionFilter(); //some filters only work after the variable is stored in the class
		}

		//Assert( m_InternalData.Simulation.Static.World.StaticProps.PhysicsObjects.Count() == 0 ); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
#ifdef _DEBUG
		for (int i = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
		{
			Assert(m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[i].pPhysicsObject == NULL); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
		}
#endif

		if (m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count() != 0)
		{
			Assert(m_InternalData.Simulation.Static.World.StaticProps.bCollisionExists);
			for (int i = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
			{
				PS_SD_Static_World_StaticProps_ClippedProp_t& Representation = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[i];
				Assert(Representation.pCollide != NULL);
				Assert(Representation.pPhysicsObject == NULL);

				Representation.pPhysicsObject = m_pPhysicsEnvironment->CreatePolyObjectStatic(Representation.pCollide, Representation.iTraceSurfaceProps, vec3_origin, vec3_angle, &params);
				Assert(Representation.pPhysicsObject != NULL);
				Representation.pPhysicsObject->RecheckCollisionFilter(); //some filters only work after the variable is stored in the class
			}
		}
		m_InternalData.Simulation.Static.World.StaticProps.bPhysicsExists = true;
	}

	//Wall
	{
		Assert(m_InternalData.Simulation.Static.Wall.Local.Brushes.pPhysicsObject == NULL); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
		if (m_InternalData.Simulation.Static.Wall.Local.Brushes.pCollideable != NULL)
		{
			m_InternalData.Simulation.Static.Wall.Local.Brushes.pPhysicsObject = m_pPhysicsEnvironment->CreatePolyObjectStatic(m_InternalData.Simulation.Static.Wall.Local.Brushes.pCollideable, m_InternalData.Simulation.Static.SurfaceProperties.surface.surfaceProps, vec3_origin, vec3_angle, &params);

			if (VPhysicsGetObject() == NULL)
				VPhysicsSetObject(m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject);

			m_InternalData.Simulation.Static.Wall.Local.Brushes.pPhysicsObject->RecheckCollisionFilter(); //some filters only work after the variable is stored in the class
		}

		Assert(m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject == NULL); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
		if (m_InternalData.Simulation.Static.Wall.Local.Tube.pCollideable != NULL)
		{
			m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject = m_pPhysicsEnvironment->CreatePolyObjectStatic(m_InternalData.Simulation.Static.Wall.Local.Tube.pCollideable, m_InternalData.Simulation.Static.SurfaceProperties.surface.surfaceProps, vec3_origin, vec3_angle, &params);

			if (VPhysicsGetObject() == NULL)
				VPhysicsSetObject(m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject);

			m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject->RecheckCollisionFilter(); //some filters only work after the variable is stored in the class
		}
	}
}

void CEnginePortalInternal::CreateLinkedPhysics(IEnginePortalServer* pRemoteCollisionEntity)
{
	CEnginePortalInternal* pRemotePortalInternal = dynamic_cast<CEnginePortalInternal*>(pRemoteCollisionEntity);
	//int iDefaultSurfaceIndex = physprops->GetSurfaceIndex( "default" );
	objectparams_t params = gEntList.PhysGetDefaultObjectParams();

	//if( pCollisionEntity )
	params.pGameData = this->m_pOuter;
	//else
	//	params.pGameData = GetWorldEntity();

	//everything in our linked collision should be based on the linked portal's world collision
	PS_SD_Static_World_t& RemoteSimulationStaticWorld = pRemotePortalInternal->m_InternalData.Simulation.Static.World;

	Assert(m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject == NULL); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
	if (RemoteSimulationStaticWorld.Brushes.pCollideable != NULL)
	{
		m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject = m_pPhysicsEnvironment->CreatePolyObjectStatic(RemoteSimulationStaticWorld.Brushes.pCollideable, pRemotePortalInternal->m_InternalData.Simulation.Static.SurfaceProperties.surface.surfaceProps, m_InternalData.Placement.ptaap_LinkedToThis.ptOriginTransform, m_InternalData.Placement.ptaap_LinkedToThis.qAngleTransform, &params);
		m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject->RecheckCollisionFilter(); //some filters only work after the variable is stored in the class
	}


	Assert(m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects.Count() == 0); //Be sure to find graceful fixes for asserts, performance is a big concern with portal simulation
	if (RemoteSimulationStaticWorld.StaticProps.ClippedRepresentations.Count() != 0)
	{
		for (int i = RemoteSimulationStaticWorld.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
		{
			PS_SD_Static_World_StaticProps_ClippedProp_t& Representation = RemoteSimulationStaticWorld.StaticProps.ClippedRepresentations[i];
			IPhysicsObject* pPhysObject = m_pPhysicsEnvironment->CreatePolyObjectStatic(Representation.pCollide, Representation.iTraceSurfaceProps, m_InternalData.Placement.ptaap_LinkedToThis.ptOriginTransform, m_InternalData.Placement.ptaap_LinkedToThis.qAngleTransform, &params);
			if (pPhysObject)
			{
				m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects.AddToTail(pPhysObject);
				pPhysObject->RecheckCollisionFilter(); //some filters only work after the variable is stored in the class
			}
		}
	}
}

void CEnginePortalInternal::ClearLocalPhysics(void)
{
	if (m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject)
	{
		m_pPhysicsEnvironment->DestroyObject(m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject);
		m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject = NULL;
	}

	if (m_InternalData.Simulation.Static.World.StaticProps.bPhysicsExists &&
		(m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count() != 0))
	{
		for (int i = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
		{
			PS_SD_Static_World_StaticProps_ClippedProp_t& Representation = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[i];
			if (Representation.pPhysicsObject)
			{
				m_pPhysicsEnvironment->DestroyObject(Representation.pPhysicsObject);
				Representation.pPhysicsObject = NULL;
			}
		}
	}
	m_InternalData.Simulation.Static.World.StaticProps.bPhysicsExists = false;

	if (m_InternalData.Simulation.Static.Wall.Local.Brushes.pPhysicsObject)
	{
		m_pPhysicsEnvironment->DestroyObject(m_InternalData.Simulation.Static.Wall.Local.Brushes.pPhysicsObject);
		m_InternalData.Simulation.Static.Wall.Local.Brushes.pPhysicsObject = NULL;
	}

	if (m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject)
	{
		m_pPhysicsEnvironment->DestroyObject(m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject);
		m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject = NULL;
	}
	VPhysicsSetObject(NULL);
}

void CEnginePortalInternal::ClearLinkedPhysics(void)
{
	//static collideables
	{
		if (m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject)
		{
			m_pPhysicsEnvironment->DestroyObject(m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject);
			m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject = NULL;
		}

		if (m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects.Count())
		{
			for (int i = m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects.Count(); --i >= 0; )
				m_pPhysicsEnvironment->DestroyObject(m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects[i]);

			m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects.RemoveAll();
		}
	}
}

void CEnginePortalInternal::CreateHoleShapeCollideable()
{
	//update hole shape - used to detect if an entity is within the portal hole bounds
	{
		if (m_InternalData.Placement.pHoleShapeCollideable)
			gEntList.PhysGetCollision()->DestroyCollide(m_InternalData.Placement.pHoleShapeCollideable);

		float fHolePlanes[6 * 4];

		//first and second planes are always forward and backward planes
		fHolePlanes[(0 * 4) + 0] = m_InternalData.Placement.PortalPlane.normal.x;
		fHolePlanes[(0 * 4) + 1] = m_InternalData.Placement.PortalPlane.normal.y;
		fHolePlanes[(0 * 4) + 2] = m_InternalData.Placement.PortalPlane.normal.z;
		fHolePlanes[(0 * 4) + 3] = m_InternalData.Placement.PortalPlane.dist - 0.5f;

		fHolePlanes[(1 * 4) + 0] = -m_InternalData.Placement.PortalPlane.normal.x;
		fHolePlanes[(1 * 4) + 1] = -m_InternalData.Placement.PortalPlane.normal.y;
		fHolePlanes[(1 * 4) + 2] = -m_InternalData.Placement.PortalPlane.normal.z;
		fHolePlanes[(1 * 4) + 3] = (-m_InternalData.Placement.PortalPlane.dist) + 500.0f;


		//the remaining planes will always have the same ordering of normals, with different distances plugged in for each convex we're creating
		//normal order is up, down, left, right

		fHolePlanes[(2 * 4) + 0] = m_InternalData.Placement.vUp.x;
		fHolePlanes[(2 * 4) + 1] = m_InternalData.Placement.vUp.y;
		fHolePlanes[(2 * 4) + 2] = m_InternalData.Placement.vUp.z;
		fHolePlanes[(2 * 4) + 3] = m_InternalData.Placement.vUp.Dot(GetAbsOrigin() + (m_InternalData.Placement.vUp * (PORTAL_HALF_HEIGHT * 0.98f)));

		fHolePlanes[(3 * 4) + 0] = -m_InternalData.Placement.vUp.x;
		fHolePlanes[(3 * 4) + 1] = -m_InternalData.Placement.vUp.y;
		fHolePlanes[(3 * 4) + 2] = -m_InternalData.Placement.vUp.z;
		fHolePlanes[(3 * 4) + 3] = -m_InternalData.Placement.vUp.Dot(GetAbsOrigin() - (m_InternalData.Placement.vUp * (PORTAL_HALF_HEIGHT * 0.98f)));

		fHolePlanes[(4 * 4) + 0] = -m_InternalData.Placement.vRight.x;
		fHolePlanes[(4 * 4) + 1] = -m_InternalData.Placement.vRight.y;
		fHolePlanes[(4 * 4) + 2] = -m_InternalData.Placement.vRight.z;
		fHolePlanes[(4 * 4) + 3] = -m_InternalData.Placement.vRight.Dot(GetAbsOrigin() - (m_InternalData.Placement.vRight * (PORTAL_HALF_WIDTH * 0.98f)));

		fHolePlanes[(5 * 4) + 0] = m_InternalData.Placement.vRight.x;
		fHolePlanes[(5 * 4) + 1] = m_InternalData.Placement.vRight.y;
		fHolePlanes[(5 * 4) + 2] = m_InternalData.Placement.vRight.z;
		fHolePlanes[(5 * 4) + 3] = m_InternalData.Placement.vRight.Dot(GetAbsOrigin() + (m_InternalData.Placement.vRight * (PORTAL_HALF_WIDTH * 0.98f)));

		CPolyhedron* pPolyhedron = GeneratePolyhedronFromPlanes(fHolePlanes, 6, PORTAL_POLYHEDRON_CUT_EPSILON, true);
		Assert(pPolyhedron != NULL);
		CPhysConvex* pConvex = gEntList.PhysGetCollision()->ConvexFromConvexPolyhedron(*pPolyhedron);
		pPolyhedron->Release();
		Assert(pConvex != NULL);
		m_InternalData.Placement.pHoleShapeCollideable = gEntList.PhysGetCollision()->ConvertConvexToCollide(&pConvex, 1);
	}
}

void CEnginePortalInternal::ClearHoleShapeCollideable()
{
	if (m_InternalData.Placement.pHoleShapeCollideable) {
		gEntList.PhysGetCollision()->DestroyCollide(m_InternalData.Placement.pHoleShapeCollideable);
		m_InternalData.Placement.pHoleShapeCollideable = NULL;
	}
}

bool CEnginePortalInternal::CreatedPhysicsObject(const IPhysicsObject* pObject, PS_PhysicsObjectSourceType_t* pOut_SourceType) const
{
	if ((pObject == m_InternalData.Simulation.Static.World.Brushes.pPhysicsObject) || (pObject == m_InternalData.Simulation.Static.Wall.Local.Brushes.pPhysicsObject))
	{
		if (pOut_SourceType)
			*pOut_SourceType = PSPOST_LOCAL_BRUSHES;

		return true;
	}

	if (pObject == m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.Brushes.pPhysicsObject)
	{
		if (pOut_SourceType)
			*pOut_SourceType = PSPOST_REMOTE_BRUSHES;

		return true;
	}

	for (int i = m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations.Count(); --i >= 0; )
	{
		if (m_InternalData.Simulation.Static.World.StaticProps.ClippedRepresentations[i].pPhysicsObject == pObject)
		{
			if (pOut_SourceType)
				*pOut_SourceType = PSPOST_LOCAL_STATICPROPS;
			return true;
		}
	}

	for (int i = m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects.Count(); --i >= 0; )
	{
		if (m_InternalData.Simulation.Static.Wall.RemoteTransformedToLocal.StaticProps.PhysicsObjects[i] == pObject)
		{
			if (pOut_SourceType)
				*pOut_SourceType = PSPOST_REMOTE_STATICPROPS;

			return true;
		}
	}

	if (pObject == m_InternalData.Simulation.Static.Wall.Local.Tube.pPhysicsObject)
	{
		if (pOut_SourceType)
			*pOut_SourceType = PSPOST_HOLYWALL_TUBE;

		return true;
	}

	return false;
}

void CEnginePortalInternal::MarkAsOwned(CBaseEntity* pEntity)
{
	if (!pEntity->IsNetworkable() || pEntity->entindex() == -1) {
		return;
	}

	Assert(pEntity != NULL);
	int iEntIndex = pEntity->entindex();
	Assert(gEntList.m_OwnedEntityMap[iEntIndex] == NULL);
#ifdef _DEBUG
	for (int i = m_OwnedEntities.Count(); --i >= 0; )
		Assert(m_OwnedEntities[i] != pEntity);
#endif
	Assert((m_EntFlags[iEntIndex] & PSEF_OWNS_ENTITY) == 0);

	m_EntFlags[iEntIndex] |= PSEF_OWNS_ENTITY;
	gEntList.m_OwnedEntityMap[iEntIndex] = this;
	m_OwnedEntities.AddToTail(pEntity);

	if (pEntity->IsPlayer())
	{
		((CEnginePlayerInternal*)pEntity->GetEnginePlayer())->m_bPlayerIsInSimulator = true;
	}
}

void CEnginePortalInternal::MarkAsReleased(CBaseEntity* pEntity)
{
	if (!pEntity->IsNetworkable() || pEntity->entindex() == -1) {
		return;
	}

	Assert(pEntity != NULL);
	int iEntIndex = pEntity->entindex();
	Assert(gEntList.m_OwnedEntityMap[iEntIndex] == this);
	Assert(((m_EntFlags[iEntIndex] & PSEF_OWNS_ENTITY) != 0) || pEntity->GetEngineObject()->IsPortalSimulatorCollisionEntity());

	gEntList.m_OwnedEntityMap[iEntIndex] = NULL;
	m_EntFlags[iEntIndex] &= ~PSEF_OWNS_ENTITY;
	int i;
	for (i = m_OwnedEntities.Count(); --i >= 0; )
	{
		if (m_OwnedEntities[i] == pEntity)
		{
			m_OwnedEntities.FastRemove(i);
			break;
		}
	}
	Assert(i >= 0);

	if (pEntity->IsPlayer())
	{
		((CEnginePlayerInternal*)pEntity->GetEnginePlayer())->m_bPlayerIsInSimulator = false;
	}
}

void UpdateShadowClonesPortalSimulationFlags(const CBaseEntity* pSourceEntity, unsigned int iFlags, int iSourceFlags)
{
	unsigned int iOrFlags = iSourceFlags & iFlags;

	IEngineObjectServer* pClones = pSourceEntity->GetEngineObject()->GetClonesOfEntity();
	while (pClones)
	{
		CBaseEntity* pClone = pClones->GetOuter();
		CEnginePortalInternal* pCloneSimulator = (CEnginePortalInternal*)pClone->GetEngineObject()->GetSimulatorThatOwnsEntity();

		unsigned int* pFlags = (unsigned int*)&pCloneSimulator->m_EntFlags[pClone->entindex()];
		*pFlags &= ~iFlags;
		*pFlags |= iOrFlags;

		Assert(((iSourceFlags ^ *pFlags) & iFlags) == 0);

		pClones = pClones->AsEngineShadowClone()->GetNext() ? pClones->AsEngineShadowClone()->GetNext()->AsEngineObject() : NULL;
	}
}

void CEnginePortalInternal::TakeOwnershipOfEntity(CBaseEntity* pEntity)
{
	AssertMsg(m_bLocalDataIsReady, "Tell the portal simulator where it is with MoveTo() before using it in any other way.");

	Assert(pEntity != NULL);
	if (pEntity == NULL)
		return;

	if (!pEntity->IsNetworkable() || pEntity->entindex() == -1) {
		return;
	}

	if (pEntity->IsWorld())
		return;

	if (pEntity->GetEngineObject()->IsShadowClone())
		return;

	if (pEntity->GetServerVehicle() != NULL) //we don't take kindly to vehicles in these here parts. Their physics controllers currently don't migrate properly and cause a crash
		return;

	if (OwnsEntity(pEntity))
		return;

	Assert(pEntity->GetEngineObject()->GetSimulatorThatOwnsEntity() == NULL);
	MarkAsOwned(pEntity);
	Assert(pEntity->GetEngineObject()->GetSimulatorThatOwnsEntity() == this);

	if (EntityIsInPortalHole(pEntity->GetEngineObject()))
		m_EntFlags[pEntity->entindex()] |= PSEF_IS_IN_PORTAL_HOLE;
	else
		m_EntFlags[pEntity->entindex()] &= ~PSEF_IS_IN_PORTAL_HOLE;

	UpdateShadowClonesPortalSimulationFlags(pEntity, PSEF_IS_IN_PORTAL_HOLE, m_EntFlags[pEntity->entindex()]);

	pEntity->PortalSimulator_TookOwnershipOfEntity(this);

	if (IsSimulatingVPhysics())
		TakePhysicsOwnership(pEntity);

	pEntity->GetEngineObject()->CollisionRulesChanged(); //absolutely necessary in single-environment mode, possibly expendable in multi-environment moder
	//pEntity->SetGroundEntity( NULL );
	IPhysicsObject* pObject = pEntity->GetEngineObject()->VPhysicsGetObject();
	if (pObject)
	{
		pObject->Wake();
		pObject->RecheckContactPoints();
	}

	CUtlVector<IEngineObjectServer*> childrenList;
	pEntity->GetEngineObject()->GetAllChildren( childrenList);
	for (int i = childrenList.Count(); --i >= 0; )
	{
		CBaseEntity* pEnt = childrenList[i]->GetOuter();
		IEnginePortalServer* pOwningSimulator = pEnt->GetEngineObject()->GetSimulatorThatOwnsEntity();
		if (pOwningSimulator != this)
		{
			if (pOwningSimulator != NULL)
				pOwningSimulator->ReleaseOwnershipOfEntity(pEnt, (pOwningSimulator == GetLinkedPortal()));

			TakeOwnershipOfEntity(childrenList[i]->GetOuter());
		}
	}
}

void RecheckEntityCollision(CBaseEntity* pEntity)
{
	CCallQueue* pCallQueue;
	if ((pCallQueue = gEntList.GetPostTouchQueue()) != NULL)
	{
		pCallQueue->QueueCall(RecheckEntityCollision, pEntity);
		return;
	}

	pEntity->GetEngineObject()->CollisionRulesChanged(); //absolutely necessary in single-environment mode, possibly expendable in multi-environment mode
	//pEntity->SetGroundEntity( NULL );
	IPhysicsObject* pObject = pEntity->GetEngineObject()->VPhysicsGetObject();
	if (pObject)
	{
		pObject->Wake();
		pObject->RecheckContactPoints();
	}
}

void CEnginePortalInternal::ReleaseOwnershipOfEntity(CBaseEntity* pEntity, bool bMovingToLinkedSimulator /*= false*/)
{
	if (pEntity == NULL)
		return;

	if (!pEntity->IsNetworkable() || pEntity->entindex() == -1) {
		return;
	}

	if (pEntity->IsWorld())
		return;

	if (!OwnsEntity(pEntity))
		return;

	if (GetPhysicsEnvironment())
		ReleasePhysicsOwnership(pEntity, true, bMovingToLinkedSimulator);

	m_EntFlags[pEntity->entindex()] &= ~PSEF_IS_IN_PORTAL_HOLE;
	UpdateShadowClonesPortalSimulationFlags(pEntity, PSEF_IS_IN_PORTAL_HOLE, m_EntFlags[pEntity->entindex()]);

	Assert(pEntity->GetEngineObject()->GetSimulatorThatOwnsEntity() == this);
	MarkAsReleased(pEntity);
	Assert(pEntity->GetEngineObject()->GetSimulatorThatOwnsEntity() == NULL);

	if (bMovingToLinkedSimulator == false)
	{
		RecheckEntityCollision(pEntity);
	}

	pEntity->PortalSimulator_ReleasedOwnershipOfEntity(this);

	CUtlVector<IEngineObjectServer*> childrenList;
	pEntity->GetEngineObject()->GetAllChildren( childrenList);
	for (int i = childrenList.Count(); --i >= 0; )
		ReleaseOwnershipOfEntity(childrenList[i]->GetOuter());
}

void CEnginePortalInternal::ReleaseAllEntityOwnership(void)
{
	//Assert( m_bLocalDataIsReady || (m_InternalData.Simulation.Dynamic.m_OwnedEntities.Count() == 0) );
	int iSkippedObjects = 0;
	while (m_OwnedEntities.Count() != iSkippedObjects) //the release function changes m_OwnedEntities
	{
		CBaseEntity* pEntity = m_OwnedEntities[iSkippedObjects];
		if (pEntity->GetEngineObject()->IsShadowClone() ||
			pEntity->GetEngineObject()->IsPortalSimulatorCollisionEntity())
		{
			++iSkippedObjects;
			continue;
		}
		if (EntityIsInPortalHole(pEntity->GetEngineObject()))
		{
			pEntity->FindClosestPassableSpace(GetPortalPlane().normal);
		}
		ReleaseOwnershipOfEntity(pEntity);
	}

	Assert(OwnsEntity(m_pOuter));
}

void CEnginePortalInternal::TakePhysicsOwnership(CBaseEntity* pEntity)
{
	if (!pEntity->IsNetworkable() || pEntity->entindex() == -1) {
		return;
	}

	if (GetPhysicsEnvironment() == NULL)
		return;

	if (pEntity->GetEngineObject()->IsPortalSimulatorCollisionEntity())
		return;

	Assert(pEntity->GetEngineObject()->IsShadowClone() == false);
	Assert(OwnsEntity(pEntity)); //taking physics ownership happens AFTER general ownership

	if (OwnsPhysicsForEntity(pEntity))
		return;

	int iEntIndex = pEntity->entindex();
	m_EntFlags[iEntIndex] |= PSEF_OWNS_PHYSICS;


	//physics cloning
	{
#ifdef _DEBUG
		{
			int iDebugIndex;
			for (iDebugIndex = m_ShadowClones.FromLinkedPortal.Count(); --iDebugIndex >= 0; )
			{
				if (m_ShadowClones.FromLinkedPortal[iDebugIndex]->GetClonedEntity() == pEntity)
					break;
			}
			AssertMsg(iDebugIndex < 0, "Trying to own an entity, when a clone from the linked portal already exists");

			if (GetLinkedPortal())
			{
				for (iDebugIndex = GetLinkedPortal()->m_ShadowClones.FromLinkedPortal.Count(); --iDebugIndex >= 0; )
				{
					if (GetLinkedPortal()->m_ShadowClones.FromLinkedPortal[iDebugIndex]->GetClonedEntity() == pEntity)
						break;
				}
				AssertMsg(iDebugIndex < 0, "Trying to own an entity, when we're already exporting a clone to the linked portal");
			}

			//Don't require a copy from main to already exist
		}
#endif

		EHANDLE hEnt = pEntity;

		//To linked portal
		if (GetLinkedPortal() && GetLinkedPortal()->GetPhysicsEnvironment())
		{

			DBG_CODE(
				for (int i = GetLinkedPortal()->m_ShadowClones.FromLinkedPortal.Count(); --i >= 0; )
					AssertMsg(GetLinkedPortal()->m_ShadowClones.FromLinkedPortal[i]->GetClonedEntity() != pEntity, "Already cloning to linked portal.");
					);

			CEngineShadowCloneInternal* pClone = CEngineShadowCloneInternal::CreateShadowClone(GetLinkedPortal()->GetPhysicsEnvironment(), hEnt, "CPortalSimulator::TakePhysicsOwnership(): To Linked Portal", &MatrixThisToLinked().As3x4());
			if (pClone)
			{
				//bool bHeldByPhyscannon = false;
				CBaseEntity* pHeldEntity = NULL;
				CBasePlayer* pPlayer = gEntList.GetPlayerHoldingEntity(pEntity);

				if (!pPlayer && pEntity->IsPlayer())
				{
					pPlayer = (CBasePlayer*)pEntity;
				}

				if (pPlayer)
				{
					pHeldEntity = pPlayer->GetPlayerHeldEntity();
					/*if ( !pHeldEntity )
					{
						pHeldEntity = PhysCannonGetHeldEntity( pPlayer->GetActiveWeapon() );
						bHeldByPhyscannon = true;
					}*/
				}

				if (pHeldEntity)
				{
					//player is holding the entity, force them to pick it back up again
					bool bIsHeldObjectOnOppositeSideOfPortal = pPlayer->GetEnginePlayer()->IsHeldObjectOnOppositeSideOfPortal();
					pPlayer->GetEnginePlayer()->SetSilentDropAndPickup(true);
					pPlayer->ForceDropOfCarriedPhysObjects(pHeldEntity);
					pPlayer->GetEnginePlayer()->SetHeldObjectOnOppositeSideOfPortal(bIsHeldObjectOnOppositeSideOfPortal);
				}

				GetLinkedPortal()->m_ShadowClones.FromLinkedPortal.AddToTail(pClone);
				GetLinkedPortal()->MarkAsOwned(pClone->AsEngineObject()->GetOuter());
				GetLinkedPortal()->m_EntFlags[pClone->entindex()] |= PSEF_OWNS_PHYSICS;
				GetLinkedPortal()->m_EntFlags[pClone->entindex()] |= m_EntFlags[pEntity->entindex()] & PSEF_IS_IN_PORTAL_HOLE;
				pClone->AsEngineObject()->CollisionRulesChanged(); //adding the clone to the portal simulator changes how it collides

				if (pHeldEntity)
				{
					/*if ( bHeldByPhyscannon )
					{
						PhysCannonPickupObject( pPlayer, pHeldEntity );
					}
					else*/
					{
						pPlayer->PickupObject(pHeldEntity);
					}
					pPlayer->GetEnginePlayer()->SetSilentDropAndPickup(false);
				}
			}
		}
	}

	//PortalSimulator_TookPhysicsOwnershipOfEntity(pEntity);
}

void CEnginePortalInternal::ReleasePhysicsOwnership(CBaseEntity* pEntity, bool bContinuePhysicsCloning /*= true*/, bool bMovingToLinkedSimulator /*= false*/)
{
	if (!pEntity->IsNetworkable() || pEntity->entindex() == -1) {
		return;
	}

	if (pEntity->GetEngineObject()->IsPortalSimulatorCollisionEntity())
		return;

	Assert(OwnsEntity(pEntity)); //releasing physics ownership happens BEFORE releasing general ownership
	Assert(pEntity->GetEngineObject()->IsShadowClone() == false);

	if (GetPhysicsEnvironment() == NULL)
		return;

	if (!OwnsPhysicsForEntity(pEntity))
		return;

	if (IsSimulatingVPhysics() == false)
		bContinuePhysicsCloning = false;

	int iEntIndex = pEntity->entindex();
	m_EntFlags[iEntIndex] &= ~PSEF_OWNS_PHYSICS;

	//physics cloning
	{
#ifdef _DEBUG
		{
			int iDebugIndex;
			for (iDebugIndex = m_ShadowClones.FromLinkedPortal.Count(); --iDebugIndex >= 0; )
			{
				if (m_ShadowClones.FromLinkedPortal[iDebugIndex]->GetClonedEntity() == pEntity)
					break;
			}
			AssertMsg(iDebugIndex < 0, "Trying to release an entity, when a clone from the linked portal already exists.");
		}
#endif

		//clear exported clones
		{
			DBG_CODE_NOSCOPE(bool bFoundAlready = false; );
			DBG_CODE_NOSCOPE(const char* szLastFoundMarker = NULL; );

			//to linked portal
			if (GetLinkedPortal())
			{
				DBG_CODE_NOSCOPE(bFoundAlready = false; );
				for (int i = GetLinkedPortal()->m_ShadowClones.FromLinkedPortal.Count(); --i >= 0; )
				{
					if (GetLinkedPortal()->m_ShadowClones.FromLinkedPortal[i]->GetClonedEntity() == pEntity)
					{
						CEngineShadowCloneInternal* pClone = GetLinkedPortal()->m_ShadowClones.FromLinkedPortal[i];
						AssertMsg(bFoundAlready == false, "Multiple clones to linked portal found.");
						DBG_CODE_NOSCOPE(bFoundAlready = true; );
						DBG_CODE_NOSCOPE(szLastFoundMarker = pClone->m_szDebugMarker);

						//bool bHeldByPhyscannon = false;
						CBaseEntity* pHeldEntity = NULL;
						CBasePlayer* pPlayer = gEntList.GetPlayerHoldingEntity(pEntity);

						if (!pPlayer && pEntity->IsPlayer())
						{
							pPlayer = (CBasePlayer*)pEntity;
						}

						if (pPlayer)
						{
							pHeldEntity = pPlayer->GetPlayerHeldEntity();
							/*if ( !pHeldEntity )
							{
								pHeldEntity = PhysCannonGetHeldEntity( pPlayer->GetActiveWeapon() );
								bHeldByPhyscannon = true;
							}*/
						}

						if (pHeldEntity)
						{
							//player is holding the entity, force them to pick it back up again
							bool bIsHeldObjectOnOppositeSideOfPortal = pPlayer->GetEnginePlayer()->IsHeldObjectOnOppositeSideOfPortal();
							pPlayer->GetEnginePlayer()->SetSilentDropAndPickup(true);
							pPlayer->ForceDropOfCarriedPhysObjects(pHeldEntity);
							pPlayer->GetEnginePlayer()->SetHeldObjectOnOppositeSideOfPortal(bIsHeldObjectOnOppositeSideOfPortal);
						}
						else
						{
							pHeldEntity = NULL;
						}

						GetLinkedPortal()->m_EntFlags[pClone->entindex()] &= ~PSEF_OWNS_PHYSICS;
						GetLinkedPortal()->MarkAsReleased(pClone->AsEngineObject()->GetOuter());
						CEngineShadowCloneInternal::ReleaseShadowClone(pClone);
						GetLinkedPortal()->m_ShadowClones.FromLinkedPortal.FastRemove(i);

						if (pHeldEntity)
						{
							/*if ( bHeldByPhyscannon )
							{
								PhysCannonPickupObject( pPlayer, pHeldEntity );
							}
							else*/
							{
								pPlayer->PickupObject(pHeldEntity);
							}
							pPlayer->GetEnginePlayer()->SetSilentDropAndPickup(false);
						}

						DBG_CODE_NOSCOPE(continue; );
						break;
					}
				}
			}
		}
	}

	//PortalSimulator_ReleasedPhysicsOwnershipOfEntity(pEntity);
}

int CEnginePortalInternal::GetMoveableOwnedEntities(CBaseEntity** pEntsOut, int iEntOutLimit) const
{
	int iOwnedEntCount = m_OwnedEntities.Count();
	int iOutputCount = 0;

	for (int i = 0; i != iOwnedEntCount; ++i)
	{
		CBaseEntity* pEnt = m_OwnedEntities[i];
		Assert(pEnt != NULL);

		if (pEnt->GetEngineObject()->IsShadowClone())
			continue;

		if (pEnt->GetEngineObject()->IsPortalSimulatorCollisionEntity())
			continue;

		if (pEnt->GetEngineObject()->GetMoveType() == MOVETYPE_NONE)
			continue;

		pEntsOut[iOutputCount] = pEnt;
		++iOutputCount;

		if (iOutputCount == iEntOutLimit)
			break;
	}

	return iOutputCount;
}

void CEnginePortalInternal::BeforeMove()
{
	//create a list of all entities that are actually within the portal hole, they will likely need to be moved out of solid space when the portal moves
	m_pFixEntities = new CBaseEntity * [m_OwnedEntities.Count()]; //(CBaseEntity**)stackalloc(sizeof(CBaseEntity*) * m_OwnedEntities.Count());
	m_iFixEntityCount = 0;
	for (int i = m_OwnedEntities.Count(); --i >= 0; )
	{
		CBaseEntity* pEntity = m_OwnedEntities[i];
		if (pEntity->GetEngineObject()->IsShadowClone() ||
			pEntity->GetEngineObject()->IsPortalSimulatorCollisionEntity())
			continue;

		if (EntityIsInPortalHole(pEntity->GetEngineObject()))
		{
			m_pFixEntities[m_iFixEntityCount] = pEntity;
			++m_iFixEntityCount;
		}
	}
	m_OldPlane = GetPortalPlane(); //used in fixing code
}

void CEnginePortalInternal::AfterMove()
{
	for (int i = 0; i != m_iFixEntityCount; ++i)
	{
		if (!EntityIsInPortalHole(m_pFixEntities[i]->GetEngineObject()))
		{
			//this entity is most definitely stuck in a solid wall right now
			//pFixEntities[i]->SetAbsOrigin( pFixEntities[i]->GetAbsOrigin() + (OldPlane.m_Normal * 50.0f) );
			m_pFixEntities[i]->FindClosestPassableSpace( m_OldPlane.normal);
			continue;
		}

		//entity is still in the hole, but it's possible the hole moved enough where they're in part of the wall
		{
			//TODO: figure out if that's the case and fix it
		}
	}
	delete[] m_pFixEntities;
	m_pFixEntities = NULL;

	Assert( OwnsEntity(m_pOuter));
}

void CEnginePortalInternal::BeforeLocalPhysicsClear()
{
	//all physics clones
	{
		for (int i = m_ShadowClones.FromLinkedPortal.Count(); --i >= 0; )
		{
			CEngineShadowCloneInternal* pClone = m_ShadowClones.FromLinkedPortal[i];
			Assert(pClone->AsEngineObject()->GetSimulatorThatOwnsEntity() == this);
			m_EntFlags[pClone->entindex()] &= ~PSEF_OWNS_PHYSICS;
			MarkAsReleased(pClone->AsEngineObject()->GetOuter());
			Assert(pClone->AsEngineObject()->GetSimulatorThatOwnsEntity() == NULL);
			CEngineShadowCloneInternal::ReleaseShadowClone(pClone);
		}

		m_ShadowClones.FromLinkedPortal.RemoveAll();
	}

	Assert(m_ShadowClones.FromLinkedPortal.Count() == 0);

	//release physics ownership of owned entities
	for (int i = m_OwnedEntities.Count(); --i >= 0; )
		ReleasePhysicsOwnership(m_OwnedEntities[i], false);

	Assert(m_ShadowClones.FromLinkedPortal.Count() == 0);
}

void CEnginePortalInternal::AfterLocalPhysicsCreated()
{
	//re-acquire environment physics for owned entities
	for (int i = m_OwnedEntities.Count(); --i >= 0; )
		TakePhysicsOwnership(m_OwnedEntities[i]);
}


void CEnginePortalInternal::BeforeLinkedPhysicsClear()
{
	//clones from the linked portal
	{
		for (int i = m_ShadowClones.FromLinkedPortal.Count(); --i >= 0; )
		{
			CEngineShadowCloneInternal* pClone = m_ShadowClones.FromLinkedPortal[i];
			m_EntFlags[pClone->entindex()] &= ~PSEF_OWNS_PHYSICS;
			MarkAsReleased(pClone->AsEngineObject()->GetOuter());
			CEngineShadowCloneInternal::ReleaseShadowClone(pClone);
		}

		m_ShadowClones.FromLinkedPortal.RemoveAll();
	}
}

void CEnginePortalInternal::AfterLinkedPhysicsCreated()
{
	//re-clone physicsshadowclones from the remote environment
	CUtlVector<CBaseEntity*>& RemoteOwnedEntities = GetLinkedPortal()->m_OwnedEntities;
	for (int i = RemoteOwnedEntities.Count(); --i >= 0; )
	{
		if (RemoteOwnedEntities[i]->GetEngineObject()->IsShadowClone() ||
			RemoteOwnedEntities[i]->GetEngineObject()->IsPortalSimulatorCollisionEntity())
			continue;

		int j;
		for (j = m_ShadowClones.FromLinkedPortal.Count(); --j >= 0; )
		{
			if (m_ShadowClones.FromLinkedPortal[j]->GetClonedEntity() == RemoteOwnedEntities[i])
				break;
		}

		if (j >= 0) //already cloning
			continue;



		EHANDLE hEnt = RemoteOwnedEntities[i];
		CEngineShadowCloneInternal* pClone = CEngineShadowCloneInternal::CreateShadowClone(GetPhysicsEnvironment(), hEnt, "CPortalSimulator::CreateLinkedPhysics(): From Linked Portal", &MatrixLinkedToThis().As3x4());
		if (pClone)
		{
			MarkAsOwned(pClone->AsEngineObject()->GetOuter());
			m_EntFlags[pClone->entindex()] |= PSEF_OWNS_PHYSICS;
			m_ShadowClones.FromLinkedPortal.AddToTail(pClone);
			pClone->AsEngineObject()->CollisionRulesChanged(); //adding the clone to the portal simulator changes how it collides
		}
	}
}

void CEnginePortalInternal::AfterCollisionEntityCreated()
{
	MarkAsOwned(m_pOuter);
	m_EntFlags[m_pOuter->entindex()] |= PSEF_OWNS_PHYSICS;
}


void CEnginePortalInternal::BeforeCollisionEntityDestroy()
{
	m_EntFlags[m_pOuter->entindex()] &= ~PSEF_OWNS_PHYSICS;
	MarkAsReleased(m_pOuter);
}

void CEnginePortalInternal::StartCloningEntity(CBaseEntity* pEntity)
{
	if (!pEntity->IsNetworkable() || pEntity->entindex() == -1) {
		return;
	}

	if (pEntity->GetEngineObject()->IsShadowClone() || pEntity->GetEngineObject()->IsPortalSimulatorCollisionEntity())
		return;

	if ((m_EntFlags[pEntity->entindex()] & PSEF_CLONES_ENTITY_FROM_MAIN) != 0)
		return; //already cloned, no work to do

#ifdef _DEBUG
	for (int i = m_ShadowClones.ShouldCloneFromMain.Count(); --i >= 0; )
		Assert(m_ShadowClones.ShouldCloneFromMain[i] != pEntity);
#endif

	//NDebugOverlay::EntityBounds( pEntity, 0, 255, 0, 50, 5.0f );

	m_ShadowClones.ShouldCloneFromMain.AddToTail(pEntity);
	m_EntFlags[pEntity->entindex()] |= PSEF_CLONES_ENTITY_FROM_MAIN;
}

void CEnginePortalInternal::StopCloningEntity(CBaseEntity* pEntity)
{
	if (!pEntity->IsNetworkable() || pEntity->entindex() == -1) {
		return;
	}

	if ((m_EntFlags[pEntity->entindex()] & PSEF_CLONES_ENTITY_FROM_MAIN) == 0)
	{
		Assert(m_ShadowClones.ShouldCloneFromMain.Find(pEntity) == -1);
		return; //not cloned, no work to do
	}

	//NDebugOverlay::EntityBounds( pEntity, 255, 0, 0, 50, 5.0f );

	m_ShadowClones.ShouldCloneFromMain.FastRemove(m_ShadowClones.ShouldCloneFromMain.Find(pEntity));
	m_EntFlags[pEntity->entindex()] &= ~PSEF_CLONES_ENTITY_FROM_MAIN;
}

void CEnginePortalInternal::ClearLinkedEntities(void)
{
	//clones from the linked portal
	{
		for (int i = m_ShadowClones.FromLinkedPortal.Count(); --i >= 0; )
		{
			CEngineShadowCloneInternal* pClone = m_ShadowClones.FromLinkedPortal[i];
			m_EntFlags[pClone->entindex()] &= ~PSEF_OWNS_PHYSICS;
			MarkAsReleased(pClone->AsEngineObject()->GetOuter());
			CEngineShadowCloneInternal::ReleaseShadowClone(pClone);
		}

		m_ShadowClones.FromLinkedPortal.RemoveAll();
	}
}

void CEnginePortalInternal::UpdateCorners()
{
	Vector vOrigin = GetAbsOrigin();
	Vector vUp, vRight;
	GetVectors(NULL, &vRight, &vUp);

	for (int i = 0; i < 4; ++i)
	{
		Vector vAddPoint = vOrigin;

		vAddPoint += vRight * ((i & (1 << 0)) ? (PORTAL_HALF_WIDTH) : (-PORTAL_HALF_WIDTH));
		vAddPoint += vUp * ((i & (1 << 1)) ? (PORTAL_HALF_HEIGHT) : (-PORTAL_HALF_HEIGHT));

		m_vPortalCorners[i] = vAddPoint;
	}
}

CEngineShadowCloneInternal::CEngineShadowCloneInternal(IServerEntityList* pServerEntityList, int iForceEdictIndex, int iSerialNum)
	:CEngineObjectInternal(pServerEntityList, iForceEdictIndex, iSerialNum)
{
	m_matrixShadowTransform.Identity();
	m_matrixShadowTransform_Inverse.Identity();
	m_bShadowTransformIsIdentity = true;
	gEntList.m_ActiveShadowClones.AddToTail(this);
}

CEngineShadowCloneInternal::~CEngineShadowCloneInternal() 
{
	SetClonedEntity(NULL);
	gEntList.m_ActiveShadowClones.FindAndRemove(this); //also removed in UpdateOnRemove()
}

void CEngineShadowCloneInternal::SetClonedEntity(CBaseEntity* pEntToClone)
{
	VPhysicsDestroyObject();
	CBaseEntity* pSource = m_hClonedEntity.Get();
	if (pSource)
	{
		CPhysicsShadowCloneLL* pCloneListHead = gEntList.m_EntityClones[pSource->entindex()];
		Assert(pCloneListHead != NULL);

		CPhysicsShadowCloneLL* pFind = pCloneListHead;
		CPhysicsShadowCloneLL* pLast = pFind;
		while (pFind->pClone != this)
		{
			pLast = pFind;
			Assert(pFind->pNext != NULL);
			pFind = pFind->pNext;
		}

		if (pFind == pCloneListHead)
		{
			gEntList.m_EntityClones[pSource->entindex()] = pFind->pNext;
		}
		else
		{
			pLast->pNext = pFind->pNext;
			pLast->pClone->m_pNext = pFind->pNext->pClone;
		}
		m_pNext = NULL;
		gEntList.m_SCLLManager.Free(pFind);
	}
#ifdef _DEBUG
	else
	{
		//verify that it didn't weasel into a list somewhere and get left behind
		for (int i = 0; i != MAX_SHADOW_CLONE_COUNT; ++i)
		{
			CPhysicsShadowCloneLL* pCloneSearch = gEntList.m_EntityClones[i];
			while (pCloneSearch)
			{
				Assert(pCloneSearch->pClone != this);
				pCloneSearch = pCloneSearch->pNext;
			}
		}
	}
#endif
	m_hClonedEntity = pEntToClone;
	if (m_hClonedEntity.Get()) {
		CPhysicsShadowCloneLL* pCloneLLEntry = gEntList.m_SCLLManager.Alloc();
		pCloneLLEntry->pClone = this;
		pCloneLLEntry->pNext = gEntList.m_EntityClones[pEntToClone->entindex()];
		if (gEntList.m_EntityClones[pEntToClone->entindex()]) {
			m_pNext = gEntList.m_EntityClones[pEntToClone->entindex()]->pClone;
		}
		gEntList.m_EntityClones[pEntToClone->entindex()] = pCloneLLEntry;
	}
	//FullSyncClonedPhysicsObjects();
}

CBaseEntity* CEngineShadowCloneInternal::GetClonedEntity(void)
{
	return m_hClonedEntity.Get();
}

void CEngineShadowCloneInternal::VPhysicsDestroyObject(void)
{
	VPhysicsSetObject(NULL);

	for (int i = m_CloneLinks.Count(); --i >= 0; )
	{
		Assert(m_CloneLinks[i].pClone != NULL);
		m_pOwnerPhysEnvironment->DestroyObject(m_CloneLinks[i].pClone);
	}
	m_CloneLinks.RemoveAll();

	//BaseClass::VPhysicsDestroyObject();
}

int CEngineShadowCloneInternal::VPhysicsGetObjectList(IPhysicsObject** pList, int listMax)
{
	int iCountStop = m_CloneLinks.Count();
	if (iCountStop > listMax)
		iCountStop = listMax;

	for (int i = 0; i != iCountStop; ++i, ++pList)
		*pList = m_CloneLinks[i].pClone;

	return iCountStop;
}

static void DrawDebugOverlayForShadowClone(CEngineShadowCloneInternal* pClone)
{
	unsigned char iColorIntensity = (pClone->IsInAssumedSyncState()) ? (127) : (255);

	int iRed = (pClone->IsUntransformedClone()) ? (0) : (iColorIntensity);
	int iGreen = iColorIntensity;
	int iBlue = iColorIntensity;

	NDebugOverlay::EntityBounds(pClone->m_pOuter, iRed, iGreen, iBlue, (iColorIntensity >> 2), 0.05f);
}

void CEngineShadowCloneInternal::FullSync(bool bAllowAssumedSync)
{
	Assert(IsMarkedForDeletion() == false);

	IEngineObjectServer* pClonedEntity = m_hClonedEntity.Get() ? m_hClonedEntity.Get()->GetEngineObject() : NULL;

	if (pClonedEntity == NULL)
	{
		AssertMsg(VPhysicsGetObject() != NULL, "Been linkless for more than this update, something should have killed this clone.");
		SetMoveType(MOVETYPE_NONE);
		SetSolid(SOLID_NONE);
		SetSolidFlags(0);
		SetCollisionGroup(COLLISION_GROUP_NONE);
		VPhysicsDestroyObject();
		return;
	}

	SetGroundEntity(NULL);

	bool bIsSynced = bAllowAssumedSync;
	bool bBigChanges = true; //assume there are, and be proven wrong

	if (bAllowAssumedSync)
	{
		IPhysicsObject* pSourceObjects[1024];
		int iObjectCount = pClonedEntity->VPhysicsGetObjectList(pSourceObjects, 1024);

		//scan for really big differences that would definitely require a full sync
		bBigChanges = (iObjectCount != m_CloneLinks.Count());
		if (!bBigChanges)
		{
			for (int i = 0; i != iObjectCount; ++i)
			{
				IPhysicsObject* pSourcePhysics = pSourceObjects[i];
				IPhysicsObject* pClonedPhysics = m_CloneLinks[i].pClone;

				if ((pSourcePhysics != m_CloneLinks[i].pSource) ||
					(pSourcePhysics->IsCollisionEnabled() != pClonedPhysics->IsCollisionEnabled()))
				{
					bBigChanges = true;
					bIsSynced = false;
					break;
				}

				Vector ptSourcePosition, ptClonePosition;
				pSourcePhysics->GetPosition(&ptSourcePosition, NULL);
				if (!m_bShadowTransformIsIdentity)
					ptSourcePosition = m_matrixShadowTransform * ptSourcePosition;

				pClonedPhysics->GetPosition(&ptClonePosition, NULL);

				if ((ptClonePosition - ptSourcePosition).LengthSqr() > 2500.0f)
				{
					bBigChanges = true;
					bIsSynced = false;
					break;
				}

				//Vector vSourceVelocity, vCloneVelocity;


				if (!pSourcePhysics->IsAsleep()) //only allow full syncrosity if the source entity is entirely asleep
					bIsSynced = false;

				if (m_bInAssumedSyncState && !pClonedPhysics->IsAsleep())
					bIsSynced = false;
			}
		}
		else
		{
			bIsSynced = false;
		}

		bIsSynced = false;

		if (bIsSynced)
		{
			//good enough to skip a full update
			if (!m_bInAssumedSyncState)
			{
				//do one last sync
				PartialSync(true);

				//if we don't do this, objects just fall out of the world (it happens, I swear)

				for (int i = m_CloneLinks.Count(); --i >= 0; )
				{
					if ((m_CloneLinks[i].pSource->GetShadowController() == NULL) && m_CloneLinks[i].pClone->IsMotionEnabled())
					{
						//m_CloneLinks[i].pClone->SetVelocityInstantaneous( &vec3_origin, &vec3_origin );
						//m_CloneLinks[i].pClone->SetVelocity( &vec3_origin, &vec3_origin );
						m_CloneLinks[i].pClone->EnableGravity(false);
						m_CloneLinks[i].pClone->EnableMotion(false);
						m_CloneLinks[i].pClone->Sleep();
					}
				}

				m_bInAssumedSyncState = true;
			}

			if (sv_debug_physicsshadowclones.GetBool())
				DrawDebugOverlayForShadowClone(this);

			return;
		}
	}

	m_bInAssumedSyncState = false;

	//past this point, we're committed to a broad update

	if (bBigChanges)
	{
		MoveType_t sourceMoveType = pClonedEntity->GetMoveType();


		IPhysicsObject* pPhysObject = pClonedEntity->VPhysicsGetObject();
		if ((sourceMoveType == MOVETYPE_CUSTOM) ||
			(sourceMoveType == MOVETYPE_STEP) ||
			(sourceMoveType == MOVETYPE_WALK) ||
			(pPhysObject &&
				(
					(pPhysObject->GetGameFlags() & FVPHYSICS_PLAYER_HELD) ||
					(pPhysObject->GetShadowController() != NULL)
					)
				)
			)
		{
			//#ifdef _DEBUG
			SetMoveType(MOVETYPE_NONE); //to kill an assert
			//#endif
						//PUSH should be used sparingly, you can't stand on a MOVETYPE_PUSH object :/
			SetMoveType(MOVETYPE_VPHYSICS, pClonedEntity->GetMoveCollide()); //either an unclonable movetype, or a shadow/held object
		}
		/*else if(sourceMoveType == MOVETYPE_STEP)
		{
			//GetEngineObject()->SetMoveType( MOVETYPE_NONE ); //to kill an assert
			GetEngineObject()->SetMoveType( MOVETYPE_VPHYSICS, pClonedEntity->GetMoveCollide() );
		}*/
		else
		{
			//if( m_bShadowTransformIsIdentity )
			SetMoveType(sourceMoveType, pClonedEntity->GetMoveCollide());
			//else
			//{
			//	GetEngineObject()->SetMoveType( MOVETYPE_NONE ); //to kill an assert
			//	GetEngineObject()->SetMoveType( MOVETYPE_PUSH, pClonedEntity->GetMoveCollide() );
			//}
		}

		SolidType_t sourceSolidType = pClonedEntity->GetSolid();
		if (sourceSolidType == SOLID_BBOX)
			SetSolid(SOLID_VPHYSICS);
		else
			SetSolid(sourceSolidType);
		//SetSolid( SOLID_VPHYSICS );

		SetElasticity(pClonedEntity->GetElasticity());
		SetFriction(pClonedEntity->GetFriction());



		int iSolidFlags = pClonedEntity->GetSolidFlags() | FSOLID_CUSTOMRAYTEST;
		if (m_bShadowTransformIsIdentity)
			iSolidFlags |= FSOLID_CUSTOMBOXTEST; //need this at least for the player or they get stuck in themselves
		else
			iSolidFlags &= ~FSOLID_FORCE_WORLD_ALIGNED;
		/*if( pClonedEntity->IsPlayer() )
		{
			iSolidFlags |= FSOLID_CUSTOMRAYTEST | FSOLID_CUSTOMBOXTEST;
		}*/

		SetSolidFlags(iSolidFlags);



		SetEffects(pClonedEntity->GetEffects() | (EF_NODRAW | EF_NOSHADOW | EF_NORECEIVESHADOW));

		SetCollisionGroup(pClonedEntity->GetCollisionGroup());

		SetModelIndex(pClonedEntity->GetModelIndex());
		SetModelName(pClonedEntity->GetModelName());

		if (modelinfo->GetModelType(pClonedEntity->GetModel()) == mod_studio)
			m_pOuter->SetModel(STRING(pClonedEntity->GetModelName()));

		m_pOuter->SetSize(pClonedEntity->OBBMins(), pClonedEntity->OBBMaxs());
	}

	FullSyncClonedPhysicsObjects(bBigChanges);
	SyncEntity(true);

	if (bBigChanges)
		CollisionRulesChanged();

	if (sv_debug_physicsshadowclones.GetBool())
		DrawDebugOverlayForShadowClone(this);
}

void CEngineShadowCloneInternal::SyncEntity(bool bPullChanges)
{
	m_bShouldUpSync = false;

	CBaseEntity* pSource, * pDest;
	VMatrix* pTransform;
	if (bPullChanges)
	{
		pSource = m_hClonedEntity.Get();
		pDest = this->m_pOuter;
		pTransform = &m_matrixShadowTransform;

		if (pSource == NULL)
			return;
	}
	else
	{
		pSource = this->m_pOuter;
		pDest = m_hClonedEntity.Get();
		pTransform = &m_matrixShadowTransform_Inverse;

		if (pDest == NULL)
			return;
	}


	Vector ptOrigin, vVelocity;
	QAngle qAngles;

	ptOrigin = pSource->GetEngineObject()->GetAbsOrigin();
	qAngles = pSource->GetEngineObject()->GetAbsAngles();
	vVelocity = pSource->GetEngineObject()->GetAbsVelocity();

	if (!m_bShadowTransformIsIdentity)
	{
		ptOrigin = (*pTransform) * ptOrigin;
		qAngles = TransformAnglesToWorldSpace(qAngles, pTransform->As3x4());
		vVelocity = pTransform->ApplyRotation(vVelocity);
	}
	//else
	//{
	//	pDest->SetGroundEntity( pSource->GetGroundEntity() );
	//}

	if ((ptOrigin != pDest->GetEngineObject()->GetAbsOrigin()) || (qAngles != pDest->GetEngineObject()->GetAbsAngles()))
	{
		pDest->Teleport(&ptOrigin, &qAngles, NULL);
	}

	if (vVelocity != pDest->GetEngineObject()->GetAbsVelocity())
	{
		//pDest->IncrementInterpolationFrame();
		pDest->GetEngineObject()->SetAbsVelocity(vec3_origin); //the two step process helps, I don't know why, but it does
		pDest->ApplyAbsVelocityImpulse(vVelocity);
	}
}

static void FullSyncPhysicsObject(IPhysicsObject* pSource, IPhysicsObject* pDest, const VMatrix* pTransform, bool bTeleport)
{
	IGrabControllerServer* pGrabController = NULL;

	if (!pSource->IsAsleep())
		pDest->Wake();

	float fSavedMass = 0.0f, fSavedRotationalDamping; //setting mass to 0.0f purely to kill a warning that I can't seem to kill with pragmas
	if (gEntList.m_ActivePortals.Count() > 0) {
		if (pSource->GetGameFlags() & FVPHYSICS_PLAYER_HELD)
		{
			//CBasePlayer *pPlayer = UTIL_PlayerByIndex( 1 );
			//Assert( pPlayer );

			CBaseEntity* pLookingForEntity = (CBaseEntity*)pSource->GetGameData();

			CBasePlayer* pHoldingPlayer = gEntList.GetPlayerHoldingEntity(pLookingForEntity);
			if (pHoldingPlayer)
			{
				pGrabController = pHoldingPlayer->GetGrabController();

				if (!pGrabController)
					pGrabController = pHoldingPlayer->GetActiveWeapon()->GetGrabController();
			}

			AssertMsg(pGrabController, "Physics object is held, but we can't find the holding controller.");
			pGrabController->GetSavedParamsForCarriedPhysObject(pSource, &fSavedMass, &fSavedRotationalDamping);
		}
	}

	//Boiler plate
	{
		pDest->SetGameIndex(pSource->GetGameIndex()); //what's it do?
		pDest->SetCallbackFlags(pSource->GetCallbackFlags()); //wise?
		pDest->SetGameFlags(pSource->GetGameFlags() | FVPHYSICS_NO_SELF_COLLISIONS | FVPHYSICS_IS_SHADOWCLONE);
		pDest->SetMaterialIndex(pSource->GetMaterialIndex());
		pDest->SetContents(pSource->GetContents());

		pDest->EnableCollisions(pSource->IsCollisionEnabled());
		pDest->EnableGravity(pSource->IsGravityEnabled());
		pDest->EnableDrag(pSource->IsDragEnabled());
		pDest->EnableMotion(pSource->IsMotionEnabled());
	}

	//Damping
	{
		float fSpeedDamp, fRotDamp;
		if (pGrabController)
		{
			pSource->GetDamping(&fSpeedDamp, NULL);
			pDest->SetDamping(&fSpeedDamp, &fSavedRotationalDamping);
		}
		else
		{
			pSource->GetDamping(&fSpeedDamp, &fRotDamp);
			pDest->SetDamping(&fSpeedDamp, &fRotDamp);
		}
	}

	//stuff that we really care about
	{
		if (pGrabController)
			pDest->SetMass(fSavedMass);
		else
			pDest->SetMass(pSource->GetMass());

		Vector ptOrigin, vVelocity, vAngularVelocity, vInertia;
		QAngle qAngles;

		pSource->GetPosition(&ptOrigin, &qAngles);
		pSource->GetVelocity(&vVelocity, &vAngularVelocity);
		vInertia = pSource->GetInertia();

		if (pTransform)
		{
#if 0
			pDest->SetPositionMatrix(pTransform->As3x4(), true); //works like we think?
#else		
			ptOrigin = (*pTransform) * ptOrigin;
			qAngles = TransformAnglesToWorldSpace(qAngles, pTransform->As3x4());
			vVelocity = pTransform->ApplyRotation(vVelocity);
			vAngularVelocity = pTransform->ApplyRotation(vAngularVelocity);
#endif
		}

		//avoid oversetting variables (I think that even setting them to the same value they already are disrupts the delicate physics balance)
		if (vInertia != pDest->GetInertia())
			pDest->SetInertia(vInertia);

		Vector ptDestOrigin, vDestVelocity, vDestAngularVelocity;
		QAngle qDestAngles;
		pDest->GetPosition(&ptDestOrigin, &qDestAngles);

		if ((ptOrigin != ptDestOrigin) || (qAngles != qDestAngles))
			pDest->SetPosition(ptOrigin, qAngles, bTeleport);

		//pDest->SetVelocityInstantaneous( &vec3_origin, &vec3_origin );
		//pDest->Sleep();

		pDest->GetVelocity(&vDestVelocity, &vDestAngularVelocity);

		if ((vVelocity != vDestVelocity) || (vAngularVelocity != vDestAngularVelocity))
			pDest->SetVelocityInstantaneous(&vVelocity, &vAngularVelocity);

		IPhysicsShadowController* pSourceController = pSource->GetShadowController();
		if (pSourceController == NULL)
		{
			if (pDest->GetShadowController() != NULL)
			{
				//we don't need a shadow controller anymore
				pDest->RemoveShadowController();
			}
		}
		else
		{
			IPhysicsShadowController* pDestController = pDest->GetShadowController();
			if (pDestController == NULL)
			{
				//we need a shadow controller
				float fMaxSpeed, fMaxAngularSpeed;
				pSourceController->GetMaxSpeed(&fMaxSpeed, &fMaxAngularSpeed);

				pDest->SetShadow(fMaxSpeed, fMaxAngularSpeed, pSourceController->AllowsTranslation(), pSourceController->AllowsRotation());
				pDestController = pDest->GetShadowController();
				pDestController->SetTeleportDistance(pSourceController->GetTeleportDistance());
				pDestController->SetPhysicallyControlled(pSourceController->IsPhysicallyControlled());
			}

			//sync shadow controllers
			float fTimeOffset;
			Vector ptTargetPosition;
			QAngle qTargetAngles;
			fTimeOffset = pSourceController->GetTargetPosition(&ptTargetPosition, &qTargetAngles);

			if (pTransform)
			{
				ptTargetPosition = (*pTransform) * ptTargetPosition;
				qTargetAngles = TransformAnglesToWorldSpace(qTargetAngles, pTransform->As3x4());
			}

			pDestController->Update(ptTargetPosition, qTargetAngles, fTimeOffset);
		}


	}

	//pDest->RecheckContactPoints();
}

static void PartialSyncPhysicsObject(IPhysicsObject* pSource, IPhysicsObject* pDest, const VMatrix* pTransform)
{
	Vector ptOrigin, vVelocity, vAngularVelocity, vInertia;
	QAngle qAngles;

	pSource->GetPosition(&ptOrigin, &qAngles);
	pSource->GetVelocity(&vVelocity, &vAngularVelocity);
	vInertia = pSource->GetInertia();

	if (pTransform)
	{
#if 0
		//pDest->SetPositionMatrix( matTransform.As3x4(), true ); //works like we think?
#else	
		ptOrigin = (*pTransform) * ptOrigin;
		qAngles = TransformAnglesToWorldSpace(qAngles, pTransform->As3x4());
		vVelocity = pTransform->ApplyRotation(vVelocity);
		vAngularVelocity = pTransform->ApplyRotation(vAngularVelocity);
#endif
	}

	//avoid oversetting variables (I think that even setting them to the same value they already are disrupts the delicate physics balance)
	if (vInertia != pDest->GetInertia())
		pDest->SetInertia(vInertia);

	Vector ptDestOrigin, vDestVelocity, vDestAngularVelocity;
	QAngle qDestAngles;
	pDest->GetPosition(&ptDestOrigin, &qDestAngles);
	pDest->GetVelocity(&vDestVelocity, &vDestAngularVelocity);


	if ((ptOrigin != ptDestOrigin) || (qAngles != qDestAngles))
		pDest->SetPosition(ptOrigin, qAngles, false);

	if ((vVelocity != vDestVelocity) || (vAngularVelocity != vDestAngularVelocity))
		pDest->SetVelocity(&vVelocity, &vAngularVelocity);

	pDest->EnableCollisions(pSource->IsCollisionEnabled());
}


void CEngineShadowCloneInternal::FullSyncClonedPhysicsObjects(bool bTeleport)
{
	IEngineObjectServer* pClonedEntity = m_hClonedEntity.Get() ? m_hClonedEntity.Get()->GetEngineObject() : NULL;
	if (pClonedEntity == NULL)
	{
		VPhysicsDestroyObject();
		return;
	}

	VMatrix* pTransform;
	if (m_bShadowTransformIsIdentity)
		pTransform = NULL;
	else
		pTransform = &m_matrixShadowTransform;

	IPhysicsObject* (pSourceObjects[1024]);
	int iObjectCount = pClonedEntity->VPhysicsGetObjectList(pSourceObjects, 1024);

	//easy out if nothing has changed
	if (iObjectCount == m_CloneLinks.Count())
	{
		int i;
		for (i = 0; i != iObjectCount; ++i)
		{
			if (pSourceObjects[i] == NULL)
				break;

			if (pSourceObjects[i] != m_CloneLinks[i].pSource)
				break;
		}

		if (i == iObjectCount) //no changes
		{
			for (i = 0; i != iObjectCount; ++i)
				FullSyncPhysicsObject(m_CloneLinks[i].pSource, m_CloneLinks[i].pClone, pTransform, bTeleport);

			return;
		}
	}



	//copy the existing list of clone links to a temp array, we're going to be starting from scratch and copying links as we need them
	PhysicsObjectCloneLink_t* pExistingLinks = NULL;
	int iExistingLinkCount = m_CloneLinks.Count();
	if (iExistingLinkCount != 0)
	{
		pExistingLinks = (PhysicsObjectCloneLink_t*)stackalloc(sizeof(PhysicsObjectCloneLink_t) * m_CloneLinks.Count());
		memcpy(pExistingLinks, m_CloneLinks.Base(), sizeof(PhysicsObjectCloneLink_t) * m_CloneLinks.Count());
	}
	m_CloneLinks.RemoveAll();

	//now, go over the object list we just got from the source entity, and either copy or create links as necessary
	int i;
	for (i = 0; i != iObjectCount; ++i)
	{
		IPhysicsObject* pSource = pSourceObjects[i];

		if (pSource == NULL) //this really shouldn't happen, but it does >_<
			continue;

		PhysicsObjectCloneLink_t cloneLink;

		int j;
		for (j = 0; j != iExistingLinkCount; ++j)
		{
			if (pExistingLinks[j].pSource == pSource)
				break;
		}

		if (j != iExistingLinkCount)
		{
			//copyable link found
			cloneLink = pExistingLinks[j];
			memset(&pExistingLinks[j], 0, sizeof(PhysicsObjectCloneLink_t)); //zero out this slot so we don't destroy it in cleanup
		}
		else
		{
			//no link found to copy, create a new one
			cloneLink.pSource = pSource;

			//apparently some collision code gets called on creation before we've set extra game flags, so we're going to cheat a bit and temporarily set our extra flags on the source
			unsigned int iOldGameFlags = pSource->GetGameFlags();
			pSource->SetGameFlags(iOldGameFlags | FVPHYSICS_IS_SHADOWCLONE);

			unsigned int size = gEntList.PhysGetEnv()->GetObjectSerializeSize(pSource);
			byte* pBuffer = (byte*)stackalloc(size);
			memset(pBuffer, 0, size);

			gEntList.PhysGetEnv()->SerializeObjectToBuffer(pSource, pBuffer, size); //this should work across physics environments because the serializer doesn't write anything about itself to the template
			pSource->SetGameFlags(iOldGameFlags);
			cloneLink.pClone = m_pOwnerPhysEnvironment->UnserializeObjectFromBuffer(this->m_pOuter, pBuffer, size, false); //unserializer has to be in the target environment
			assert(cloneLink.pClone); //there should be absolutely no case where we can't clone a valid existing physics object

			stackfree(pBuffer);
		}

		FullSyncPhysicsObject(cloneLink.pSource, cloneLink.pClone, pTransform, bTeleport);

		//cloneLink.pClone->Wake();

		m_CloneLinks.AddToTail(cloneLink);
	}


	//now go over the existing links, if any of them haven't been nullified, they need to be deleted
	for (i = 0; i != iExistingLinkCount; ++i)
	{
		if (pExistingLinks[i].pClone)
			m_pOwnerPhysEnvironment->DestroyObject(pExistingLinks[i].pClone); //also destroys shadow controller
	}


	VPhysicsSetObject(NULL);

	IPhysicsObject* pSource = pClonedEntity->VPhysicsGetObject();

	for (i = m_CloneLinks.Count(); --i >= 0; )
	{
		if (m_CloneLinks[i].pSource == pSource)
		{
			//m_CloneLinks[i].pClone->Wake();
			VPhysicsSetObject(m_CloneLinks[i].pClone);
			break;
		}
	}

	if ((i < 0) && (m_CloneLinks.Count() != 0))
	{
		VPhysicsSetObject(m_CloneLinks[0].pClone);
	}

	stackfree(pExistingLinks);

	//CollisionRulesChanged();
}



void CEngineShadowCloneInternal::PartialSync(bool bPullChanges)
{
	VMatrix* pTransform;

	if (bPullChanges)
	{
		if (m_bShadowTransformIsIdentity)
			pTransform = NULL;
		else
			pTransform = &m_matrixShadowTransform;

		for (int i = m_CloneLinks.Count(); --i >= 0; )
			PartialSyncPhysicsObject(m_CloneLinks[i].pSource, m_CloneLinks[i].pClone, pTransform);
	}
	else
	{
		if (m_bShadowTransformIsIdentity)
			pTransform = NULL;
		else
			pTransform = &m_matrixShadowTransform_Inverse;

		for (int i = m_CloneLinks.Count(); --i >= 0; )
			PartialSyncPhysicsObject(m_CloneLinks[i].pClone, m_CloneLinks[i].pSource, pTransform);
	}

	SyncEntity(bPullChanges);
}

void CEngineShadowCloneInternal::SetCloneTransformationMatrix(const matrix3x4_t& sourceMatrix)
{
	m_matrixShadowTransform = sourceMatrix;
	m_bShadowTransformIsIdentity = m_matrixShadowTransform.IsIdentity();

	if (!m_bShadowTransformIsIdentity)
	{
		if (m_matrixShadowTransform.InverseGeneral(m_matrixShadowTransform_Inverse) == false)
		{
			m_matrixShadowTransform.InverseTR(m_matrixShadowTransform_Inverse); //probably not the right matrix, but we're out of options
		}
	}

	FullSync();
	//PartialSync( true );
}

IPhysicsObject* CEngineShadowCloneInternal::TranslatePhysicsToClonedEnt(const IPhysicsObject* pPhysics)
{
	if (m_hClonedEntity.Get() != NULL)
	{
		for (int i = m_CloneLinks.Count(); --i >= 0; )
		{
			if (m_CloneLinks[i].pClone == pPhysics)
				return m_CloneLinks[i].pSource;
		}
	}

	return NULL;
}

CEngineShadowCloneInternal* CEngineShadowCloneInternal::CreateShadowClone(IPhysicsEnvironment* pInPhysicsEnvironment, EHANDLE hEntToClone, const char* szDebugMarker, const matrix3x4_t* pTransformationMatrix /*= NULL*/)
{
	AssertMsg(szDebugMarker != NULL, "All shadow clones must have a debug marker for where it came from in debug builds.");

	if (!sv_use_shadow_clones.GetBool())
		return NULL;

	CBaseEntity* pClonedEntity = hEntToClone.Get();
	if (pClonedEntity == NULL)
		return NULL;

	AssertMsg(pClonedEntity->GetEngineObject()->IsShadowClone() == false, "Shouldn't attempt to clone clones");

	if (pClonedEntity->GetEngineObject()->IsMarkedForDeletion())
		return NULL;

	//if( pClonedEntity->IsPlayer() )
	//	return NULL;

	IPhysicsObject* pPhysics = pClonedEntity->GetEngineObject()->VPhysicsGetObject();

	if (pPhysics == NULL)
		return NULL;

	if (pPhysics->IsStatic())
		return NULL;

	if (pClonedEntity->GetEngineObject()->GetSolid() == SOLID_BSP)
		return NULL;

	if (pClonedEntity->GetEngineObject()->GetSolidFlags() & (FSOLID_NOT_SOLID | FSOLID_TRIGGER))
		return NULL;

	if (pClonedEntity->GetEngineObject()->GetFlags() & (FL_WORLDBRUSH | FL_STATICPROP))
		return NULL;

	/*if( FClassnameIs( pClonedEntity, "func_door" ) )
	{
		//only clone func_door's that are in front of the portal

		return NULL;
	}*/

	// Too many shadow clones breaks the game (too many entities)
	if (g_iShadowCloneCount >= MAX_SHADOW_CLONE_COUNT)
	{
		AssertMsg(false, "Too many shadow clones, consider upping the limit or reducing the level's physics props");
		return NULL;
	}
	++g_iShadowCloneCount;

	CEngineShadowCloneInternal* pClone = (CEngineShadowCloneInternal*)gEntList.CreateEntityByName("physicsshadowclone")->GetEngineShadowClone();
	//s_IsShadowClone[pClone->entindex()] = true;
	pClone->SetOwnerEnvironment(pInPhysicsEnvironment);
	pClone->SetClonedEntity(hEntToClone);
	DBG_CODE_NOSCOPE(pClone->m_szDebugMarker = szDebugMarker; );



	if (pTransformationMatrix)
	{
		pClone->SetCloneTransformationMatrix(*pTransformationMatrix);
	}

	DispatchSpawn(pClone->AsEngineObject()->GetOuter());

	return pClone;
}

void CEngineShadowCloneInternal::ReleaseShadowClone(CEngineShadowCloneInternal* pShadowClone)
{
	gEntList.DestroyEntity(pShadowClone->AsEngineObject()->GetOuter());

	//Too many shadow clones breaks the game (too many entities)
	--g_iShadowCloneCount;
}

// remaps an angular variable to a 3 band function:
// 0 <= t < start :		f(t) = 0
// start <= t <= end :	f(t) = end * spline(( t-start) / (end-start) )  // s curve between clamped and linear
// end < t :			f(t) = t
float RemapAngleRange(float startInterval, float endInterval, float value)
{
	// Fixup the roll
	value = AngleNormalize(value);
	float absAngle = fabs(value);

	// beneath cutoff?
	if (absAngle < startInterval)
	{
		value = 0;
	}
	// in spline range?
	else if (absAngle <= endInterval)
	{
		float newAngle = SimpleSpline((absAngle - startInterval) / (endInterval - startInterval)) * endInterval;
		// grab the sign from the initial value
		if (value < 0)
		{
			newAngle *= -1;
		}
		value = newAngle;
	}
	// else leave it alone, in linear range

	return value;
}

BEGIN_DATADESC(CEngineVehicleInternal)

// These two are reset every time 
//	DEFINE_FIELD( m_pOuter, FIELD_EHANDLE ),
//											m_pOuterServerVehicle;

	// Quiet down classcheck
	// DEFINE_FIELD( m_controls, vehicle_controlparams_t ),

	// Controls
	DEFINE_FIELD(m_controls.throttle, FIELD_FLOAT),
	DEFINE_FIELD(m_controls.steering, FIELD_FLOAT),
	DEFINE_FIELD(m_controls.brake, FIELD_FLOAT),
	DEFINE_FIELD(m_controls.boost, FIELD_FLOAT),
	DEFINE_FIELD(m_controls.handbrake, FIELD_BOOLEAN),
	DEFINE_FIELD(m_controls.handbrakeLeft, FIELD_BOOLEAN),
	DEFINE_FIELD(m_controls.handbrakeRight, FIELD_BOOLEAN),
	DEFINE_FIELD(m_controls.brakepedal, FIELD_BOOLEAN),
	DEFINE_FIELD(m_controls.bHasBrakePedal, FIELD_BOOLEAN),

	// This has to be handled by the containing class owing to 'owner' issues
	//	DEFINE_PHYSPTR( m_pVehicle ),
	DEFINE_PHYSPTR(m_pVehicle),

	DEFINE_FIELD(m_nSpeed, FIELD_INTEGER),
	DEFINE_FIELD(m_nLastSpeed, FIELD_INTEGER),
	DEFINE_FIELD(m_nRPM, FIELD_INTEGER),
	DEFINE_FIELD(m_fLastBoost, FIELD_FLOAT),
	DEFINE_FIELD(m_nBoostTimeLeft, FIELD_INTEGER),
	DEFINE_FIELD(m_nHasBoost, FIELD_INTEGER),

	DEFINE_FIELD(m_maxThrottle, FIELD_FLOAT),
	DEFINE_FIELD(m_flMaxRevThrottle, FIELD_FLOAT),
	DEFINE_FIELD(m_flMaxSpeed, FIELD_FLOAT),
	DEFINE_FIELD(m_actionSpeed, FIELD_FLOAT),

	// This has to be handled by the containing class owing to 'owner' issues
	//	DEFINE_PHYSPTR_ARRAY( m_pWheels ),
	DEFINE_PHYSPTR_ARRAY(m_pWheels),

	DEFINE_FIELD(m_wheelCount, FIELD_INTEGER),

	DEFINE_ARRAY(m_wheelPosition, FIELD_VECTOR, 4),
	DEFINE_ARRAY(m_wheelRotation, FIELD_VECTOR, 4),
	DEFINE_ARRAY(m_wheelBaseHeight, FIELD_FLOAT, 4),
	DEFINE_ARRAY(m_wheelTotalHeight, FIELD_FLOAT, 4),
	DEFINE_ARRAY(m_poseParameters, FIELD_INTEGER, 12),
	DEFINE_FIELD(m_actionValue, FIELD_FLOAT),
	DEFINE_KEYFIELD(m_actionScale, FIELD_FLOAT, "actionScale"),
	DEFINE_FIELD(m_debugRadius, FIELD_FLOAT),
	DEFINE_FIELD(m_throttleRate, FIELD_FLOAT),
	DEFINE_FIELD(m_throttleStartTime, FIELD_FLOAT),
	DEFINE_FIELD(m_throttleActiveTime, FIELD_FLOAT),
	DEFINE_FIELD(m_turboTimer, FIELD_FLOAT),

	DEFINE_FIELD(m_flVehicleVolume, FIELD_FLOAT),
	DEFINE_FIELD(m_bIsOn, FIELD_BOOLEAN),
	DEFINE_FIELD(m_bLastThrottle, FIELD_BOOLEAN),
	DEFINE_FIELD(m_bLastBoost, FIELD_BOOLEAN),
	DEFINE_FIELD(m_bLastSkid, FIELD_BOOLEAN),
END_DATADESC()

//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CEngineVehicleInternal::CEngineVehicleInternal(IServerEntityList* pServerEntityList, int iForceEdictIndex, int iSerialNum)
:CEngineObjectInternal(pServerEntityList, iForceEdictIndex, iSerialNum)
{
	m_flVehicleVolume = 0.5;
	//m_pOuter = NULL;
	//m_pOuterServerVehicle = NULL;
	m_flMaxSpeed = 30;
}

//-----------------------------------------------------------------------------
// Destructor
//-----------------------------------------------------------------------------
CEngineVehicleInternal::~CEngineVehicleInternal()
{
	gEntList.PhysGetEnv()->DestroyVehicleController(m_pVehicle);
}

//-----------------------------------------------------------------------------
// A couple wrapper methods to perform common operations
//-----------------------------------------------------------------------------
//inline int CEngineVehicleInternal::LookupPoseParameter(const char* szName)
//{
//	return LookupPoseParameter(szName);
//}
//
//inline float CEngineVehicleInternal::GetPoseParameter(int iParameter)
//{
//	return GetPoseParameter(iParameter);
//}
//
//inline float CEngineVehicleInternal::SetPoseParameter(int iParameter, float flValue)
//{
//	Assert(IsFinite(flValue));
//	return SetPoseParameter(iParameter, flValue);
//}

//inline bool CEngineVehicleInternal::GetAttachment(const char* szName, Vector& origin, QAngle& angles)
//{
//	return m_pOuter->GetAttachment(szName, origin, angles);
//}

//-----------------------------------------------------------------------------
// Methods related to spawn
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::InitializePoseParameters()
{
	m_poseParameters[VEH_FL_WHEEL_HEIGHT] = LookupPoseParameter("vehicle_wheel_fl_height");
	m_poseParameters[VEH_FR_WHEEL_HEIGHT] = LookupPoseParameter("vehicle_wheel_fr_height");
	m_poseParameters[VEH_RL_WHEEL_HEIGHT] = LookupPoseParameter("vehicle_wheel_rl_height");
	m_poseParameters[VEH_RR_WHEEL_HEIGHT] = LookupPoseParameter("vehicle_wheel_rr_height");
	m_poseParameters[VEH_FL_WHEEL_SPIN] = LookupPoseParameter("vehicle_wheel_fl_spin");
	m_poseParameters[VEH_FR_WHEEL_SPIN] = LookupPoseParameter("vehicle_wheel_fr_spin");
	m_poseParameters[VEH_RL_WHEEL_SPIN] = LookupPoseParameter("vehicle_wheel_rl_spin");
	m_poseParameters[VEH_RR_WHEEL_SPIN] = LookupPoseParameter("vehicle_wheel_rr_spin");
	m_poseParameters[VEH_STEER] = LookupPoseParameter("vehicle_steer");
	m_poseParameters[VEH_ACTION] = LookupPoseParameter("vehicle_action");
	m_poseParameters[VEH_SPEEDO] = LookupPoseParameter("vehicle_guage");


	// move the wheels to a neutral position
	SetPoseParameter(m_poseParameters[VEH_SPEEDO], 0);
	SetPoseParameter(m_poseParameters[VEH_STEER], 0);
	SetPoseParameter(m_poseParameters[VEH_FL_WHEEL_HEIGHT], 0);
	SetPoseParameter(m_poseParameters[VEH_FR_WHEEL_HEIGHT], 0);
	SetPoseParameter(m_poseParameters[VEH_RL_WHEEL_HEIGHT], 0);
	SetPoseParameter(m_poseParameters[VEH_RR_WHEEL_HEIGHT], 0);
	InvalidateBoneCache();
}

//-----------------------------------------------------------------------------
// Purpose: Parses the vehicle's script
//-----------------------------------------------------------------------------
bool CEngineVehicleInternal::ParseVehicleScript(const char* pScriptName, solid_t& solid, vehicleparams_t& vehicle)
{
	// Physics keeps a cache of these to share among spawns of vehicles or flush for debugging
	gEntList.FindOrAddVehicleScript(pScriptName, &vehicle, NULL);

	m_debugRadius = vehicle.axles[0].wheels.radius;
	CalcWheelData(vehicle);

	PhysModelParseSolid(solid);

	// Allow the script to shift the center of mass
	if (vehicle.body.massCenterOverride != vec3_origin)
	{
		solid.massCenterOverride = vehicle.body.massCenterOverride;
		solid.params.massCenterOverride = &solid.massCenterOverride;
	}

	// allow script to change the mass of the vehicle body
	if (vehicle.body.massOverride > 0)
	{
		solid.params.mass = vehicle.body.massOverride;
	}

	return true;
}

void CEngineVehicleInternal::CalcWheelData(vehicleparams_t& vehicle)
{
	const char* pWheelAttachments[4] = { "wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr" };
	Vector left, right;
	QAngle dummy;
	SetPoseParameter(m_poseParameters[VEH_FL_WHEEL_HEIGHT], 0);
	SetPoseParameter(m_poseParameters[VEH_FR_WHEEL_HEIGHT], 0);
	SetPoseParameter(m_poseParameters[VEH_RL_WHEEL_HEIGHT], 0);
	SetPoseParameter(m_poseParameters[VEH_RR_WHEEL_HEIGHT], 0);
	InvalidateBoneCache();
	if (GetAttachment("wheel_fl", left, dummy) && GetAttachment("wheel_fr", right, dummy))
	{
		VectorITransform(left, EntityToWorldTransform(), left);
		VectorITransform(right, EntityToWorldTransform(), right);
		Vector center = (left + right) * 0.5;
		vehicle.axles[0].offset = center;
		vehicle.axles[0].wheelOffset = right - center;
		// Cache the base height of the wheels in body space
		m_wheelBaseHeight[0] = left.z;
		m_wheelBaseHeight[1] = right.z;
	}

	if (GetAttachment("wheel_rl", left, dummy) && GetAttachment("wheel_rr", right, dummy))
	{
		VectorITransform(left, EntityToWorldTransform(), left);
		VectorITransform(right, EntityToWorldTransform(), right);
		Vector center = (left + right) * 0.5;
		vehicle.axles[1].offset = center;
		vehicle.axles[1].wheelOffset = right - center;
		// Cache the base height of the wheels in body space
		m_wheelBaseHeight[2] = left.z;
		m_wheelBaseHeight[3] = right.z;
	}
	SetPoseParameter(m_poseParameters[VEH_FL_WHEEL_HEIGHT], 1);
	SetPoseParameter(m_poseParameters[VEH_FR_WHEEL_HEIGHT], 1);
	SetPoseParameter(m_poseParameters[VEH_RL_WHEEL_HEIGHT], 1);
	SetPoseParameter(m_poseParameters[VEH_RR_WHEEL_HEIGHT], 1);
	InvalidateBoneCache();
	if (GetAttachment("wheel_fl", left, dummy) && GetAttachment("wheel_fr", right, dummy))
	{
		VectorITransform(left, EntityToWorldTransform(), left);
		VectorITransform(right, EntityToWorldTransform(), right);
		// Cache the height range of the wheels in body space
		m_wheelTotalHeight[0] = m_wheelBaseHeight[0] - left.z;
		m_wheelTotalHeight[1] = m_wheelBaseHeight[1] - right.z;
		vehicle.axles[0].wheels.springAdditionalLength = m_wheelTotalHeight[0];
	}

	if (GetAttachment("wheel_rl", left, dummy) && GetAttachment("wheel_rr", right, dummy))
	{
		VectorITransform(left, EntityToWorldTransform(), left);
		VectorITransform(right, EntityToWorldTransform(), right);
		// Cache the height range of the wheels in body space
		m_wheelTotalHeight[2] = m_wheelBaseHeight[0] - left.z;
		m_wheelTotalHeight[3] = m_wheelBaseHeight[1] - right.z;
		vehicle.axles[1].wheels.springAdditionalLength = m_wheelTotalHeight[2];
	}
	for (int i = 0; i < 4; i++)
	{
		if (m_wheelTotalHeight[i] == 0.0f)
		{
			DevWarning("Vehicle %s has invalid wheel attachment for %s - no movement\n", STRING(GetModelName()), pWheelAttachments[i]);
			m_wheelTotalHeight[i] = 1.0f;
		}
	}

	SetPoseParameter(m_poseParameters[VEH_FL_WHEEL_HEIGHT], 0);
	SetPoseParameter(m_poseParameters[VEH_FR_WHEEL_HEIGHT], 0);
	SetPoseParameter(m_poseParameters[VEH_RL_WHEEL_HEIGHT], 0);
	SetPoseParameter(m_poseParameters[VEH_RR_WHEEL_HEIGHT], 0);
	InvalidateBoneCache();

	// Get raytrace offsets if they exist.
	if (GetAttachment("raytrace_fl", left, dummy) && GetAttachment("raytrace_fr", right, dummy))
	{
		VectorITransform(left, EntityToWorldTransform(), left);
		VectorITransform(right, EntityToWorldTransform(), right);
		Vector center = (left + right) * 0.5;
		vehicle.axles[0].raytraceCenterOffset = center;
		vehicle.axles[0].raytraceOffset = right - center;
	}

	if (GetAttachment("raytrace_rl", left, dummy) && GetAttachment("raytrace_rr", right, dummy))
	{
		VectorITransform(left, EntityToWorldTransform(), left);
		VectorITransform(right, EntityToWorldTransform(), right);
		Vector center = (left + right) * 0.5;
		vehicle.axles[1].raytraceCenterOffset = center;
		vehicle.axles[1].raytraceOffset = right - center;
	}
}


//-----------------------------------------------------------------------------
// Spawns the vehicle
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::Spawn()
{
	Assert(m_pOuter);

	m_actionValue = 0;
	m_actionSpeed = 0;

	m_bIsOn = false;
	m_controls.handbrake = false;
	m_controls.handbrakeLeft = false;
	m_controls.handbrakeRight = false;
	m_controls.bHasBrakePedal = true;
	m_controls.bAnalogSteering = false;

	SetMaxThrottle(1.0);
	SetMaxReverseThrottle(-1.0f);

	InitializePoseParameters();
}

extern IPhysicsGameTrace* physgametrace;

//-----------------------------------------------------------------------------
// Purpose: Initializes the vehicle physics
//			Called by our outer vehicle in it's Spawn()
//-----------------------------------------------------------------------------
bool CEngineVehicleInternal::Initialize(const char* pVehicleScript, unsigned int nVehicleType)
{
	// Ok, turn on the simulation now
	// FIXME: Disabling collisions here is necessary because we seem to be
	// getting a one-frame collision between the old + new collision models
	if (VPhysicsGetObject())
	{
		VPhysicsGetObject()->EnableCollisions(false);
	}
	VPhysicsDestroyObject();

	// Create the vphysics model + teleport it into position
	solid_t solid;
	vehicleparams_t vehicle;
	if (!ParseVehicleScript(pVehicleScript, solid, vehicle))
	{
		gEntList.DestroyEntity(m_pOuter);
		return false;
	}

	// NOTE: this needs to be greater than your max framerate (so zero is still instant)
	m_throttleRate = 10000.0;
	if (vehicle.engine.throttleTime > 0)
	{
		m_throttleRate = 1.0 / vehicle.engine.throttleTime;
	}

	m_flMaxSpeed = vehicle.engine.maxSpeed;

	IPhysicsObject* pBody = VPhysicsInitNormal(SOLID_VPHYSICS, 0, false, &solid);
	PhysSetGameFlags(pBody, FVPHYSICS_NO_SELF_COLLISIONS | FVPHYSICS_MULTIOBJECT_ENTITY);
	m_pVehicle = gEntList.PhysGetEnv()->CreateVehicleController(pBody, vehicle, nVehicleType, physgametrace);
	m_wheelCount = m_pVehicle->GetWheelCount();
	for (int i = 0; i < m_wheelCount; i++)
	{
		m_pWheels[i] = m_pVehicle->GetWheel(i);
	}
	return true;
}


//-----------------------------------------------------------------------------
// Various steering parameters
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SetThrottle(float flThrottle)
{
	m_controls.throttle = flThrottle;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SetMaxThrottle(float flMaxThrottle)
{
	m_maxThrottle = flMaxThrottle;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SetMaxReverseThrottle(float flMaxThrottle)
{
	m_flMaxRevThrottle = flMaxThrottle;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SetSteering(float flSteering, float flSteeringRate)
{
	if (!flSteeringRate)
	{
		m_controls.steering = flSteering;
	}
	else
	{
		m_controls.steering = Approach(flSteering, m_controls.steering, flSteeringRate);
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SetSteeringDegrees(float flDegrees)
{
	vehicleparams_t& vehicleParams = m_pVehicle->GetVehicleParamsForChange();
	vehicleParams.steering.degreesSlow = flDegrees;
	vehicleParams.steering.degreesFast = flDegrees;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SetAction(float flAction)
{
	m_actionSpeed = flAction;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::TurnOn()
{
	if (IsEngineDisabled())
		return;

	if (!m_bIsOn)
	{
		GetOuterServerVehicle()->SoundStart();
		m_bIsOn = true;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::TurnOff()
{
	ResetControls();

	if (m_bIsOn)
	{
		GetOuterServerVehicle()->SoundShutdown();
		m_bIsOn = false;
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SetBoost(float flBoost)
{
	if (!IsEngineDisabled())
	{
		m_controls.boost = flBoost;
	}
}

//------------------------------------------------------
// UpdateBooster - Calls UpdateBooster() in the vphysics
// code to allow the timer to be updated
//
// Returns: false if timer has expired (can use again and
//			can stop think
//			true if timer still running
//------------------------------------------------------
bool CEngineVehicleInternal::UpdateBooster(void)
{
	float retval = m_pVehicle->UpdateBooster(gpGlobals->frametime);
	return (retval > 0);
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SetHasBrakePedal(bool bHasBrakePedal)
{
	m_controls.bHasBrakePedal = bHasBrakePedal;
}

//-----------------------------------------------------------------------------
// Teleport
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::Teleport(matrix3x4_t& relativeTransform)
{
	// We basically just have to make sure the wheels are in the right place
	// after teleportation occurs

	for (int i = 0; i < m_wheelCount; i++)
	{
		matrix3x4_t matrix, newMatrix;
		m_pWheels[i]->GetPositionMatrix(&matrix);
		ConcatTransforms(relativeTransform, matrix, newMatrix);
		m_pWheels[i]->SetPositionMatrix(newMatrix, true);
	}

	// Wake the vehicle back up after a teleport
	//if (GetOuterServerVehicle() && GetOuterServerVehicle()->GetFourWheelVehicle())
	//{
		IPhysicsObject* pObj = VPhysicsGetObject();
		if (pObj)
		{
			pObj->Wake();
		}
	//}
}

//----------------------------------------------------
// Place dust at vector passed in
//----------------------------------------------------
void CEngineVehicleInternal::PlaceWheelDust(int wheelIndex, bool ignoreSpeed)
{
	// New vehicles handle this deeper into the base class
	if (hl2_episodic.GetBool())
		return;

	// Old dust
	Vector	vecPos, vecVel;
	m_pVehicle->GetWheelContactPoint(wheelIndex, &vecPos, NULL);

	vecVel.Random(-1.0f, 1.0f);
	vecVel.z = random->RandomFloat(0.3f, 1.0f);

	VectorNormalize(vecVel);

	// Higher speeds make larger dust clouds
	float flSize;
	if (ignoreSpeed)
	{
		flSize = 1.0f;
	}
	else
	{
		flSize = RemapValClamped(m_nSpeed, DUST_SPEED, m_flMaxSpeed, 0.0f, 1.0f);
	}

	if (flSize)
	{
		CEffectData	data;

		data.m_vOrigin = vecPos;
		data.m_vNormal = vecVel;
		data.m_flScale = flSize;

		DispatchEffect("WheelDust", data);
	}
}

//-----------------------------------------------------------------------------
// Frame-based updating 
//-----------------------------------------------------------------------------
bool CEngineVehicleInternal::Think()
{
	if (!m_pVehicle)
		return false;

	// Update sound + physics state
	const vehicle_operatingparams_t& carState = m_pVehicle->GetOperatingParams();
	const vehicleparams_t& vehicleData = m_pVehicle->GetVehicleParams();

	// Set save data.
	float carSpeed = fabs(INS2MPH(carState.speed));
	m_nLastSpeed = m_nSpeed;
	m_nSpeed = (int)carSpeed;
	m_nRPM = (int)carState.engineRPM;
	m_nHasBoost = vehicleData.engine.boostDelay;	// if we have any boost delay, vehicle has boost ability

	m_pVehicle->Update(gpGlobals->frametime, m_controls);

	// boost sounds
	if (IsBoosting() && !m_bLastBoost)
	{
		m_bLastBoost = true;
		m_turboTimer = gpGlobals->curtime + 2.75f;		// min duration for turbo sound
	}
	else if (!IsBoosting() && m_bLastBoost)
	{
		if (gpGlobals->curtime >= m_turboTimer)
		{
			m_bLastBoost = false;
		}
	}

	m_fLastBoost = carState.boostDelay;
	m_nBoostTimeLeft = carState.boostTimeLeft;

	// UNDONE: Use skid info from the physics system?
	// Only check wheels if we're not being carried by a dropship
	if (VPhysicsGetObject() && !VPhysicsGetObject()->GetShadowController())
	{
		const float skidFactor = 0.15f;
		const float minSpeed = DEFAULT_SKID_THRESHOLD / skidFactor;
		// we have to slide at least 15% of our speed at higher speeds to make the skid sound (otherwise it can be too frequent)
		float skidThreshold = m_bLastSkid ? DEFAULT_SKID_THRESHOLD : (carState.speed * 0.15f);
		if (skidThreshold < DEFAULT_SKID_THRESHOLD)
		{
			// otherwise, ramp in the skid threshold to avoid the sound at really low speeds unless really skidding
			skidThreshold = RemapValClamped(fabs(carState.speed), 0, minSpeed, DEFAULT_SKID_THRESHOLD * 8, DEFAULT_SKID_THRESHOLD);
		}
		// check for skidding, if we're skidding, need to play the sound
		if (carState.skidSpeed > skidThreshold && m_bIsOn)
		{
			if (!m_bLastSkid)	// only play sound once
			{
				m_bLastSkid = true;
				CPASAttenuationFilter filter(m_pOuter);
				GetOuterServerVehicle()->PlaySound(VS_SKID_FRICTION_NORMAL);
			}

			// kick up dust from the wheels while skidding
			for (int i = 0; i < 4; i++)
			{
				PlaceWheelDust(i, true);
			}
		}
		else if (m_bLastSkid == true)
		{
			m_bLastSkid = false;
			GetOuterServerVehicle()->StopSound(VS_SKID_FRICTION_NORMAL);
		}

		// toss dust up from the wheels of the vehicle if we're moving fast enough
		if (m_nSpeed >= DUST_SPEED && vehicleData.steering.dustCloud && m_bIsOn)
		{
			for (int i = 0; i < 4; i++)
			{
				PlaceWheelDust(i);
			}
		}
	}

	// Make the steering wheel match the input, with a little dampening.
#define STEER_DAMPING	0.8
	float flSteer = GetPoseParameter(m_poseParameters[VEH_STEER]);
	float flPhysicsSteer = carState.steeringAngle / vehicleData.steering.degreesSlow;
	SetPoseParameter(m_poseParameters[VEH_STEER], (STEER_DAMPING * flSteer) + ((1 - STEER_DAMPING) * flPhysicsSteer));

	m_actionValue += m_actionSpeed * m_actionScale * gpGlobals->frametime;
	SetPoseParameter(m_poseParameters[VEH_ACTION], m_actionValue);

	// setup speedometer
	if (m_bIsOn == true)
	{
		float displaySpeed = m_nSpeed / MAX_GUAGE_SPEED;
		SetPoseParameter(m_poseParameters[VEH_SPEEDO], displaySpeed);
	}

	return m_bIsOn;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::VPhysicsUpdate(IPhysicsObject* pPhysics)
{
	// must be a wheel
	//if (pPhysics == VPhysicsGetObject())
	//	return true;

	// This is here so we can make the pose parameters of the wheels
	// reflect their current physics state
	for (int i = 0; i < m_wheelCount; i++)
	{
		if (pPhysics == m_pWheels[i])
		{
			Vector tmp;
			pPhysics->GetPosition(&m_wheelPosition[i], &m_wheelRotation[i]);

			// transform the wheel into body space
			VectorITransform(m_wheelPosition[i], EntityToWorldTransform(), tmp);
			SetPoseParameter(m_poseParameters[VEH_FL_WHEEL_HEIGHT + i], (m_wheelBaseHeight[i] - tmp.z) / m_wheelTotalHeight[i]);
			SetPoseParameter(m_poseParameters[VEH_FL_WHEEL_SPIN + i], -m_wheelRotation[i].z);
			return;
		}
	}

	BaseClass::VPhysicsUpdate(pPhysics);
}


//-----------------------------------------------------------------------------
// Shared code to compute the vehicle view position
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::GetVehicleViewPosition(const char* pViewAttachment, float flPitchFactor, Vector* pAbsOrigin, QAngle* pAbsAngles)
{
	matrix3x4_t vehicleEyePosToWorld;
	Vector vehicleEyeOrigin;
	QAngle vehicleEyeAngles;
	GetAttachment(pViewAttachment, vehicleEyeOrigin, vehicleEyeAngles);
	AngleMatrix(vehicleEyeAngles, vehicleEyePosToWorld);

#ifdef HL2_DLL
	// View dampening.
	if (r_VehicleViewDampen.GetInt())
	{
		GetOuterServerVehicle()->DampenEyePosition(vehicleEyeOrigin, vehicleEyeAngles);
	}
#endif

	// Compute the relative rotation between the unperterbed eye attachment + the eye angles
	matrix3x4_t cameraToWorld;
	AngleMatrix(*pAbsAngles, cameraToWorld);

	matrix3x4_t worldToEyePos;
	MatrixInvert(vehicleEyePosToWorld, worldToEyePos);

	matrix3x4_t vehicleCameraToEyePos;
	ConcatTransforms(worldToEyePos, cameraToWorld, vehicleCameraToEyePos);

	// Now perterb the attachment point
	vehicleEyeAngles.x = RemapAngleRange(PITCH_CURVE_ZERO * flPitchFactor, PITCH_CURVE_LINEAR, vehicleEyeAngles.x);
	vehicleEyeAngles.z = RemapAngleRange(ROLL_CURVE_ZERO * flPitchFactor, ROLL_CURVE_LINEAR, vehicleEyeAngles.z);
	AngleMatrix(vehicleEyeAngles, vehicleEyeOrigin, vehicleEyePosToWorld);

	// Now treat the relative eye angles as being relative to this new, perterbed view position...
	matrix3x4_t newCameraToWorld;
	ConcatTransforms(vehicleEyePosToWorld, vehicleCameraToEyePos, newCameraToWorld);

	// output new view abs angles
	MatrixAngles(newCameraToWorld, *pAbsAngles);

	// UNDONE: *pOrigin would already be correct in single player if the HandleView() on the server ran after vphysics
	MatrixGetColumn(newCameraToWorld, 3, *pAbsOrigin);
}


//-----------------------------------------------------------------------------
// Control initialization
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::ResetControls()
{
	m_controls.handbrake = true;
	m_controls.handbrakeLeft = false;
	m_controls.handbrakeRight = false;
	m_controls.boost = 0;
	m_controls.brake = 0.0f;
	m_controls.throttle = 0;
	m_controls.steering = 0;
}

void CEngineVehicleInternal::ReleaseHandbrake()
{
	m_controls.handbrake = false;
}

void CEngineVehicleInternal::SetHandbrake(bool bBrake)
{
	m_controls.handbrake = bBrake;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::EnableMotion(void)
{
	for (int iWheel = 0; iWheel < m_wheelCount; ++iWheel)
	{
		m_pWheels[iWheel]->EnableMotion(true);
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::DisableMotion(void)
{
	Vector vecZero(0.0f, 0.0f, 0.0f);
	AngularImpulse angNone(0.0f, 0.0f, 0.0f);

	for (int iWheel = 0; iWheel < m_wheelCount; ++iWheel)
	{
		m_pWheels[iWheel]->SetVelocity(&vecZero, &angNone);
		m_pWheels[iWheel]->EnableMotion(false);
	}
}

float CEngineVehicleInternal::GetHLSpeed() const
{
	const vehicle_operatingparams_t& carState = m_pVehicle->GetOperatingParams();
	return carState.speed;
}

float CEngineVehicleInternal::GetSteering() const
{
	return m_controls.steering;
}

float CEngineVehicleInternal::GetSteeringDegrees() const
{
	const vehicleparams_t vehicleParams = m_pVehicle->GetVehicleParams();
	return vehicleParams.steering.degreesSlow;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SteeringRest(float carSpeed, const vehicleparams_t& vehicleData)
{
	float flSteeringRate = RemapValClamped(carSpeed, vehicleData.steering.speedSlow, vehicleData.steering.speedFast,
		vehicleData.steering.steeringRestRateSlow, vehicleData.steering.steeringRestRateFast);
	m_controls.steering = Approach(0, m_controls.steering, flSteeringRate * gpGlobals->frametime);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SteeringTurn(float carSpeed, const vehicleparams_t& vehicleData, bool bTurnLeft, bool bBrake, bool bThrottle)
{
	float flTargetSteering = bTurnLeft ? -1.0f : 1.0f;
	// steering speeds are stored in MPH
	float flSteeringRestRate = RemapValClamped(carSpeed, vehicleData.steering.speedSlow, vehicleData.steering.speedFast,
		vehicleData.steering.steeringRestRateSlow, vehicleData.steering.steeringRestRateFast);

	float carSpeedIns = MPH2INS(carSpeed);
	// engine speeds are stored in in/s
	if (carSpeedIns > vehicleData.engine.maxSpeed)
	{
		flSteeringRestRate = RemapValClamped(carSpeedIns, vehicleData.engine.maxSpeed, vehicleData.engine.boostMaxSpeed, vehicleData.steering.steeringRestRateFast, vehicleData.steering.steeringRestRateFast * 0.5f);
	}

	const vehicle_operatingparams_t& carState = m_pVehicle->GetOperatingParams();
	bool bIsBoosting = carState.isTorqueBoosting;

	// if you're recovering from a boost and still going faster than max, use the boost steering values
	bool bIsBoostRecover = (carState.boostTimeLeft == 100 || carState.boostTimeLeft == 0) ? false : true;
	float boostMinSpeed = vehicleData.engine.maxSpeed * vehicleData.engine.autobrakeSpeedGain;
	if (!bIsBoosting && bIsBoostRecover && carSpeedIns > boostMinSpeed)
	{
		bIsBoosting = true;
	}

	if (bIsBoosting)
	{
		flSteeringRestRate *= vehicleData.steering.boostSteeringRestRateFactor;
	}
	else if (bThrottle)
	{
		flSteeringRestRate *= vehicleData.steering.throttleSteeringRestRateFactor;
	}

	float flSteeringRate = RemapValClamped(carSpeed, vehicleData.steering.speedSlow, vehicleData.steering.speedFast,
		vehicleData.steering.steeringRateSlow, vehicleData.steering.steeringRateFast);

	if (fabs(flSteeringRate) < flSteeringRestRate)
	{
		if (Sign(flTargetSteering) != Sign(m_controls.steering))
		{
			flSteeringRate = flSteeringRestRate;
		}
	}
	if (bIsBoosting)
	{
		flSteeringRate *= vehicleData.steering.boostSteeringRateFactor;
	}
	else if (bBrake)
	{
		flSteeringRate *= vehicleData.steering.brakeSteeringRateFactor;
	}
	flSteeringRate *= gpGlobals->frametime;
	m_controls.steering = Approach(flTargetSteering, m_controls.steering, flSteeringRate);
	m_controls.bAnalogSteering = false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SteeringTurnAnalog(float carSpeed, const vehicleparams_t& vehicleData, float sidemove)
{

	// OLD Code
#if 0
	float flSteeringRate = STEERING_BASE_RATE;

	float factor = clamp(fabs(sidemove) / STICK_EXTENTS, 0.0f, 1.0f);

	factor *= 30;
	flSteeringRate *= log(factor);
	flSteeringRate *= gpGlobals->frametime;

	SetSteering(sidemove < 0.0f ? -1 : 1, flSteeringRate);
#else
	// This is tested with gamepads with analog sticks.  It gives full analog control allowing the player to hold shallow turns.
	float steering = (sidemove / STICK_EXTENTS);

	float flSign = (steering > 0) ? 1.0f : -1.0f;
	float flSteerAdj = RemapValClamped(fabs(steering), xbox_steering_deadzone.GetFloat(), 1.0f, 0.0f, 1.0f);

	float flSteeringRate = RemapValClamped(carSpeed, vehicleData.steering.speedSlow, vehicleData.steering.speedFast,
		vehicleData.steering.steeringRateSlow, vehicleData.steering.steeringRateFast);
	flSteeringRate *= vehicleData.steering.throttleSteeringRestRateFactor;

	m_controls.bAnalogSteering = true;
	SetSteering(flSign * flSteerAdj, flSteeringRate * gpGlobals->frametime);
#endif
}

//-----------------------------------------------------------------------------
// Methods related to actually driving the vehicle
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::UpdateDriverControls(CUserCmd* cmd, float flFrameTime)
{
	const float SPEED_THROTTLE_AS_BRAKE = 2.0f;
	int nButtons = cmd->buttons;

	// Get vehicle data.
	const vehicle_operatingparams_t& carState = m_pVehicle->GetOperatingParams();
	const vehicleparams_t& vehicleData = m_pVehicle->GetVehicleParams();

	// Get current speed in miles/hour.
	float flCarSign = 0.0f;
	if (carState.speed >= SPEED_THROTTLE_AS_BRAKE)
	{
		flCarSign = 1.0f;
	}
	else if (carState.speed <= -SPEED_THROTTLE_AS_BRAKE)
	{
		flCarSign = -1.0f;
	}
	float carSpeed = fabs(INS2MPH(carState.speed));

	// If going forward and turning hard, keep the throttle applied.
	if (xbox_autothrottle.GetBool() && cmd->forwardmove > 0.0f)
	{
		if (carSpeed > GetMaxSpeed() * 0.75)
		{
			if (fabs(cmd->sidemove) > cmd->forwardmove)
			{
				cmd->forwardmove = STICK_EXTENTS;
			}
		}
	}

	//Msg("F: %4.1f \tS: %4.1f!\tSTEER: %3.1f\n", cmd->forwardmove, cmd->sidemove, carState.steeringAngle);
	// If changing direction, use default "return to zero" speed to more quickly transition.
	if ((nButtons & IN_MOVELEFT) || (nButtons & IN_MOVERIGHT))
	{
		bool bTurnLeft = ((nButtons & IN_MOVELEFT) != 0);
		bool bBrake = ((nButtons & IN_BACK) != 0);
		bool bThrottleDown = ((nButtons & IN_FORWARD) != 0) && !bBrake;
		SteeringTurn(carSpeed, vehicleData, bTurnLeft, bBrake, bThrottleDown);
	}
	else if (cmd->sidemove != 0.0f)
	{
		SteeringTurnAnalog(carSpeed, vehicleData, cmd->sidemove);
	}
	else
	{
		SteeringRest(carSpeed, vehicleData);
	}

	// Set vehicle control inputs.
	m_controls.boost = 0;
	m_controls.handbrake = false;
	m_controls.handbrakeLeft = false;
	m_controls.handbrakeRight = false;
	m_controls.brakepedal = false;
	bool bThrottle;

	//-------------------------------------------------------------------------
	// Analog throttle biasing - This code gives the player a bit of control stick
	// 'slop' in the opposite direction that they are driving. If a player is 
	// driving forward and makes a hard turn in which the stick actually goes
	// below neutral (toward reverse), this code continues to propel the car 
	// forward unless the player makes a significant motion towards reverse.
	// (The inverse is true when driving in reverse and the stick is moved slightly forward)
	//-------------------------------------------------------------------------
	IDrivableVehicle* pDrivableVehicle = dynamic_cast<IDrivableVehicle*>(GetOuterServerVehicle());
	CBaseEntity* pDriver = pDrivableVehicle ? pDrivableVehicle->GetDriver() : NULL;
	CBasePlayer* pPlayerDriver;
	float flBiasThreshold = xbox_throttlebias.GetFloat();

	if (pDriver && pDriver->IsPlayer())
	{
		pPlayerDriver = dynamic_cast<CBasePlayer*>(pDriver);

		if (cmd->forwardmove == 0.0f && (fabs(cmd->sidemove) < 200.0f))
		{
			// If the stick goes neutral, clear out the bias. When the bias is neutral, it will begin biasing
			// in whichever direction the user next presses the analog stick.
			pPlayerDriver->SetVehicleAnalogControlBias(VEHICLE_ANALOG_BIAS_NONE);
		}
		else if (cmd->forwardmove > 0.0f)
		{
			if (pPlayerDriver->GetVehicleAnalogControlBias() == VEHICLE_ANALOG_BIAS_REVERSE)
			{
				// Player is pushing forward, but the controller is currently biased for reverse driving.
				// Must pass a threshold to be accepted as forward input. Otherwise we just spoof a reduced reverse input 
				// to keep the car moving in the direction the player probably expects.
				if (cmd->forwardmove < flBiasThreshold)
				{
					cmd->forwardmove = -xbox_throttlespoof.GetFloat();
				}
				else
				{
					// Passed the threshold. Allow the direction change to occur.
					pPlayerDriver->SetVehicleAnalogControlBias(VEHICLE_ANALOG_BIAS_FORWARD);
				}
			}
			else if (pPlayerDriver->GetVehicleAnalogControlBias() == VEHICLE_ANALOG_BIAS_NONE)
			{
				pPlayerDriver->SetVehicleAnalogControlBias(VEHICLE_ANALOG_BIAS_FORWARD);
			}
		}
		else if (cmd->forwardmove < 0.0f)
		{
			if (pPlayerDriver->GetVehicleAnalogControlBias() == VEHICLE_ANALOG_BIAS_FORWARD)
			{
				// Inverse of above logic
				if (cmd->forwardmove > -flBiasThreshold)
				{
					cmd->forwardmove = xbox_throttlespoof.GetFloat();
				}
				else
				{
					pPlayerDriver->SetVehicleAnalogControlBias(VEHICLE_ANALOG_BIAS_REVERSE);
				}
			}
			else if (pPlayerDriver->GetVehicleAnalogControlBias() == VEHICLE_ANALOG_BIAS_NONE)
			{
				pPlayerDriver->SetVehicleAnalogControlBias(VEHICLE_ANALOG_BIAS_REVERSE);
			}
		}
	}

	//=========================
	// analog control
	//=========================
	if (cmd->forwardmove > 0.0f)
	{
		float flAnalogThrottle = cmd->forwardmove / STICK_EXTENTS;

		flAnalogThrottle = clamp(flAnalogThrottle, 0.25f, 1.0f);

		bThrottle = true;
		if (m_controls.throttle < 0)
		{
			m_controls.throttle = 0;
		}

		float flMaxThrottle = MAX(0.1, m_maxThrottle);
		if (m_controls.steering != 0)
		{
			float flThrottleReduce = 0;

			// ramp this in, don't just start at the slow speed reduction (helps accelerate from a stop)
			if (carSpeed < vehicleData.steering.speedSlow)
			{
				flThrottleReduce = RemapValClamped(carSpeed, 0, vehicleData.steering.speedSlow,
					0, vehicleData.steering.turnThrottleReduceSlow);
			}
			else
			{
				flThrottleReduce = RemapValClamped(carSpeed, vehicleData.steering.speedSlow, vehicleData.steering.speedFast,
					vehicleData.steering.turnThrottleReduceSlow, vehicleData.steering.turnThrottleReduceFast);
			}

			float limit = 1.0f - (flThrottleReduce * fabs(m_controls.steering));
			if (limit < 0)
				limit = 0;
			flMaxThrottle = MIN(flMaxThrottle, limit);
		}

		m_controls.throttle = Approach(flMaxThrottle * flAnalogThrottle, m_controls.throttle, flFrameTime * m_throttleRate);

		// Apply the brake.
		if ((flCarSign < 0.0f) && m_controls.bHasBrakePedal)
		{
			m_controls.brake = Approach(BRAKE_MAX_VALUE, m_controls.brake, flFrameTime * r_vehicleBrakeRate.GetFloat() * BRAKE_BACK_FORWARD_SCALAR);
			m_controls.brakepedal = true;
			m_controls.throttle = 0.0f;
			bThrottle = false;
		}
		else
		{
			m_controls.brake = 0.0f;
		}
	}
	else if (cmd->forwardmove < 0.0f)
	{
		float flAnalogBrake = fabs(cmd->forwardmove / STICK_EXTENTS);

		flAnalogBrake = clamp(flAnalogBrake, 0.25f, 1.0f);

		bThrottle = true;
		if (m_controls.throttle > 0)
		{
			m_controls.throttle = 0;
		}

		float flMaxThrottle = MIN(-0.1, m_flMaxRevThrottle);
		m_controls.throttle = Approach(flMaxThrottle * flAnalogBrake, m_controls.throttle, flFrameTime * m_throttleRate);

		// Apply the brake.
		if ((flCarSign > 0.0f) && m_controls.bHasBrakePedal)
		{
			m_controls.brake = Approach(BRAKE_MAX_VALUE, m_controls.brake, flFrameTime * r_vehicleBrakeRate.GetFloat());
			m_controls.brakepedal = true;
			m_controls.throttle = 0.0f;
			bThrottle = false;
		}
		else
		{
			m_controls.brake = 0.0f;
		}
	}
	// digital control
	else if (nButtons & IN_FORWARD)
	{
		bThrottle = true;
		if (m_controls.throttle < 0)
		{
			m_controls.throttle = 0;
		}

		float flMaxThrottle = MAX(0.1, m_maxThrottle);

		if (m_controls.steering != 0)
		{
			float flThrottleReduce = 0;

			// ramp this in, don't just start at the slow speed reduction (helps accelerate from a stop)
			if (carSpeed < vehicleData.steering.speedSlow)
			{
				flThrottleReduce = RemapValClamped(carSpeed, 0, vehicleData.steering.speedSlow,
					0, vehicleData.steering.turnThrottleReduceSlow);
			}
			else
			{
				flThrottleReduce = RemapValClamped(carSpeed, vehicleData.steering.speedSlow, vehicleData.steering.speedFast,
					vehicleData.steering.turnThrottleReduceSlow, vehicleData.steering.turnThrottleReduceFast);
			}

			float limit = 1.0f - (flThrottleReduce * fabs(m_controls.steering));
			if (limit < 0)
				limit = 0;
			flMaxThrottle = MIN(flMaxThrottle, limit);
		}

		m_controls.throttle = Approach(flMaxThrottle, m_controls.throttle, flFrameTime * m_throttleRate);

		// Apply the brake.
		if ((flCarSign < 0.0f) && m_controls.bHasBrakePedal)
		{
			m_controls.brake = Approach(BRAKE_MAX_VALUE, m_controls.brake, flFrameTime * r_vehicleBrakeRate.GetFloat() * BRAKE_BACK_FORWARD_SCALAR);
			m_controls.brakepedal = true;
			m_controls.throttle = 0.0f;
			bThrottle = false;
		}
		else
		{
			m_controls.brake = 0.0f;
		}
	}
	else if (nButtons & IN_BACK)
	{
		bThrottle = true;
		if (m_controls.throttle > 0)
		{
			m_controls.throttle = 0;
		}

		float flMaxThrottle = MIN(-0.1, m_flMaxRevThrottle);
		m_controls.throttle = Approach(flMaxThrottle, m_controls.throttle, flFrameTime * m_throttleRate);

		// Apply the brake.
		if ((flCarSign > 0.0f) && m_controls.bHasBrakePedal)
		{
			m_controls.brake = Approach(BRAKE_MAX_VALUE, m_controls.brake, flFrameTime * r_vehicleBrakeRate.GetFloat());
			m_controls.brakepedal = true;
			m_controls.throttle = 0.0f;
			bThrottle = false;
		}
		else
		{
			m_controls.brake = 0.0f;
		}
	}
	else
	{
		bThrottle = false;
		m_controls.throttle = 0;
		m_controls.brake = 0.0f;
	}

	if ((nButtons & IN_SPEED) && !IsEngineDisabled() && bThrottle)
	{
		m_controls.boost = 1.0f;
	}

	// Using has brakepedal for handbrake as well.
	if ((nButtons & IN_JUMP) && m_controls.bHasBrakePedal)
	{
		m_controls.handbrake = true;

		if (cmd->sidemove < -100)
		{
			m_controls.handbrakeLeft = true;
		}
		else if (cmd->sidemove > 100)
		{
			m_controls.handbrakeRight = true;
		}

		// Prevent playing of the engine revup when we're braking
		bThrottle = false;
	}

	if (IsEngineDisabled())
	{
		m_controls.throttle = 0.0f;
		m_controls.handbrake = true;
		bThrottle = false;
	}

	// throttle sounds
	// If we dropped a bunch of speed, restart the throttle
	if (bThrottle && (m_nLastSpeed > m_nSpeed && (m_nLastSpeed - m_nSpeed > 10)))
	{
		m_bLastThrottle = false;
	}

	// throttle down now but not before??? (or we're braking)
	if (!m_controls.handbrake && !m_controls.brakepedal && bThrottle && !m_bLastThrottle)
	{
		m_throttleStartTime = gpGlobals->curtime;		// need to track how long throttle is down
		m_bLastThrottle = true;
	}
	// throttle up now but not before??
	else if (!bThrottle && m_bLastThrottle && IsEngineDisabled() == false)
	{
		m_throttleActiveTime = gpGlobals->curtime - m_throttleStartTime;
		m_bLastThrottle = false;
	}

	float flSpeedPercentage = clamp(m_nSpeed / m_flMaxSpeed, 0.f, 1.f);
	vbs_sound_update_t params;
	params.Defaults();
	params.bReverse = (m_controls.throttle < 0);
	params.bThrottleDown = bThrottle;
	params.bTurbo = IsBoosting();
	params.bVehicleInWater = GetOuterServerVehicle()->IsVehicleBodyInWater();
	params.flCurrentSpeedFraction = flSpeedPercentage;
	params.flFrameTime = flFrameTime;
	params.flWorldSpaceSpeed = carState.speed;
	GetOuterServerVehicle()->SoundUpdate(params);
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CEngineVehicleInternal::IsBoosting(void)
{
	const vehicleparams_t* pVehicleParams = &m_pVehicle->GetVehicleParams();
	const vehicle_operatingparams_t* pVehicleOperating = &m_pVehicle->GetOperatingParams();
	if (pVehicleParams && pVehicleOperating)
	{
		if ((pVehicleOperating->boostDelay - pVehicleParams->engine.boostDelay) > 0.0f)
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CEngineVehicleInternal::SetDisableEngine(bool bDisable)
{
	// Set the engine state.
	m_pVehicle->SetEngineDisabled(bDisable);
}

static int AddPhysToList(IPhysicsObject** pList, int listMax, int count, IPhysicsObject* pPhys)
{
	if (pPhys)
	{
		if (count < listMax)
		{
			pList[count] = pPhys;
			count++;
		}
	}
	return count;
}

int CEngineVehicleInternal::VPhysicsGetObjectList(IPhysicsObject** pList, int listMax)
{
	int count = 0;
	// add the body
	count = AddPhysToList(pList, listMax, count, VPhysicsGetObject());
	for (int i = 0; i < 4; i++)
	{
		count = AddPhysToList(pList, listMax, count, m_pWheels[i]);
	}
	return count;
}

BEGIN_SEND_TABLE(CEngineRopeInternal, DT_EngineRope)
	SendPropEHandle(SENDINFO(m_hStartPoint)),
	SendPropEHandle(SENDINFO(m_hEndPoint)),
	SendPropInt(SENDINFO(m_iStartAttachment), 5, 0),
	SendPropInt(SENDINFO(m_iEndAttachment), 5, 0),

	SendPropInt(SENDINFO(m_Slack), 12),
	SendPropInt(SENDINFO(m_RopeLength), 15),
	SendPropInt(SENDINFO(m_fLockedPoints), 4, SPROP_UNSIGNED),
	SendPropInt(SENDINFO(m_RopeFlags), ROPE_NUMFLAGS, SPROP_UNSIGNED),
	SendPropInt(SENDINFO(m_nSegments), 4, SPROP_UNSIGNED),
	SendPropBool(SENDINFO(m_bConstrainBetweenEndpoints)),
	SendPropInt(SENDINFO(m_iRopeMaterialModelIndex), 16, SPROP_UNSIGNED),
	SendPropInt(SENDINFO(m_Subdiv), 4, SPROP_UNSIGNED),

	SendPropFloat(SENDINFO(m_TextureScale), 10, 0, 0.1f, 10.0f),
	SendPropFloat(SENDINFO(m_Width), 0, SPROP_NOSCALE),
	SendPropFloat(SENDINFO(m_flScrollSpeed), 0, SPROP_NOSCALE),
END_SEND_TABLE()

BEGIN_DATADESC(CEngineRopeInternal)
	DEFINE_FIELD(m_RopeFlags, FIELD_INTEGER),
	DEFINE_KEYFIELD(m_Slack, FIELD_INTEGER, "Slack"),
	DEFINE_KEYFIELD(m_Width, FIELD_FLOAT, "Width"),
	DEFINE_KEYFIELD(m_TextureScale, FIELD_FLOAT, "TextureScale"),
	DEFINE_FIELD(m_nSegments, FIELD_INTEGER),
	DEFINE_FIELD(m_bConstrainBetweenEndpoints, FIELD_BOOLEAN),
	DEFINE_FIELD(m_strRopeMaterialModel, FIELD_STRING),
	DEFINE_FIELD(m_iRopeMaterialModelIndex, FIELD_MODELINDEX),
	DEFINE_KEYFIELD(m_Subdiv, FIELD_INTEGER, "Subdiv"),
	DEFINE_FIELD(m_RopeLength, FIELD_INTEGER),
	DEFINE_FIELD(m_fLockedPoints, FIELD_INTEGER),
	DEFINE_KEYFIELD(m_flScrollSpeed, FIELD_FLOAT, "ScrollSpeed"),
	DEFINE_FIELD(m_hStartPoint, FIELD_EHANDLE),
	DEFINE_FIELD(m_hEndPoint, FIELD_EHANDLE),
	DEFINE_FIELD(m_iStartAttachment, FIELD_SHORT),
	DEFINE_FIELD(m_iEndAttachment, FIELD_SHORT),
	DEFINE_FIELD(m_bStartPointValid, FIELD_BOOLEAN),
	DEFINE_FIELD(m_bEndPointValid, FIELD_BOOLEAN),
END_DATADESC()

IMPLEMENT_SERVERCLASS(CEngineRopeInternal, DT_EngineRope)

CEngineRopeInternal::CEngineRopeInternal(IServerEntityList* pServerEntityList, int iForceEdictIndex, int iSerialNum)
:CEngineObjectInternal(pServerEntityList, iForceEdictIndex, iSerialNum)
{
	m_iStartAttachment = m_iEndAttachment = 0;

	m_Slack = 0;
	m_Width = 2;
	m_TextureScale = 4;	// 4:1
	m_nSegments = 5;
	m_RopeLength = 20;
	m_fLockedPoints = (int)(ROPE_LOCK_START_POINT | ROPE_LOCK_END_POINT); // by default, both points are locked
	m_flScrollSpeed = 0;
	m_RopeFlags = ROPE_SIMULATE | ROPE_INITIAL_HANG;
	m_iRopeMaterialModelIndex = -1;
	m_Subdiv = 2;
}

CEngineRopeInternal::~CEngineRopeInternal() {
	
}

void CEngineRopeInternal::EndpointsChanged()
{
	CBaseEntity* pStartEnt = m_hStartPoint.Get();
	if (pStartEnt)
	{
		if ((pStartEnt != this->m_pOuter) || GetMoveParent())
		{
			//WatchPositionChanges( this, pStartEnt );
			pStartEnt->AddWatcherToEntity(this->m_pOuter, POSITIONWATCHER);
		}
	}
	CBaseEntity* pEndEnt = m_hEndPoint.Get();
	if (pEndEnt)
	{
		if ((pEndEnt != this->m_pOuter) || GetMoveParent())
		{
			//WatchPositionChanges( this, pEndEnt );
			pEndEnt->AddWatcherToEntity(this->m_pOuter, POSITIONWATCHER);
		}
	}
}

void CEngineRopeInternal::SetAttachmentPoint(CBaseHandle& hOutEnt, short& iOutAttachment, CBaseEntity* pEnt, int iAttachment)
{
	// Unforce our previously attached entity from transmitting.
	CBaseEntity* pCurEnt = gEntList.GetBaseEntity(hOutEnt);
	if (pCurEnt && pCurEnt->entindex() != -1)
	{
		pCurEnt->DecrementTransmitStateOwnedCounter();
		pCurEnt->DispatchUpdateTransmitState();
	}

	hOutEnt = pEnt;
	iOutAttachment = iAttachment;

	// Force this entity to transmit.
	if (pEnt)
	{
		pEnt->SetTransmitState(FL_EDICT_ALWAYS);
		pEnt->IncrementTransmitStateOwnedCounter();
	}

	EndpointsChanged();
}

void CEngineRopeInternal::SetStartPoint(CBaseEntity* pStartPoint, int attachment)
{
	SetAttachmentPoint(m_hStartPoint.GetForModify(), m_iStartAttachment.GetForModify(), pStartPoint, attachment);
}

void CEngineRopeInternal::SetEndPoint(CBaseEntity* pEndPoint, int attachment)
{
	SetAttachmentPoint(m_hEndPoint.GetForModify(), m_iEndAttachment.GetForModify(), pEndPoint, attachment);
}

void CEngineRopeInternal::ActivateStartDirectionConstraints(bool bEnable)
{
	if (bEnable)
	{
		m_fLockedPoints.Set(m_fLockedPoints | ROPE_LOCK_START_DIRECTION);
	}
	else
	{
		m_fLockedPoints &= ~((int)ROPE_LOCK_START_DIRECTION);
	}
}


void CEngineRopeInternal::ActivateEndDirectionConstraints(bool bEnable)
{
	if (bEnable)
	{
		m_fLockedPoints.Set(m_fLockedPoints | ROPE_LOCK_END_DIRECTION);
	}
	else
	{
		m_fLockedPoints &= ~((int)ROPE_LOCK_END_DIRECTION);
	}
}

bool CEngineRopeInternal::SetupHangDistance(float flHangDist)
{
	CBaseEntity* pEnt1 = m_hStartPoint.Get();
	CBaseEntity* pEnt2 = m_hEndPoint.Get();
	if (!pEnt1 || !pEnt2)
		return false;

	// Calculate starting conditions so we can force it to hang down N inches.
	Vector v1 = pEnt1->GetEngineObject()->GetAbsOrigin();
	if (pEnt1->GetBaseAnimating())
		pEnt1->GetBaseAnimating()->GetAttachment(m_iStartAttachment, v1);

	Vector v2 = pEnt2->GetEngineObject()->GetAbsOrigin();
	if (pEnt2->GetBaseAnimating())
		pEnt2->GetBaseAnimating()->GetAttachment(m_iEndAttachment, v2);

	float flSlack, flLen;
	CalcRopeStartingConditions(v1, v2, ROPE_MAX_SEGMENTS, flHangDist, &flLen, &flSlack);

	m_RopeLength = (int)flLen;
	m_Slack = (int)flSlack;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Calculate the length of the rope
//-----------------------------------------------------------------------------
void CEngineRopeInternal::RecalculateLength(void)
{
	// Get my entities
	if (m_hEndPoint.Get())
	{
		CBaseEntity* pStartEnt = m_hStartPoint.Get();
		CBaseEntity* pEndEnt = m_hEndPoint.Get();

		// Set the length
		m_RopeLength = (int)(pStartEnt->GetEngineObject()->GetAbsOrigin() - pEndEnt->GetEngineObject()->GetAbsOrigin()).Length();
	}
	else
	{
		m_RopeLength = 0;
	}
}

bool CEngineRopeInternal::GetEndPointPos2(CBaseEntity* pAttached, int iAttachment, Vector& vPos)
{
	if (!pAttached)
		return false;

	if (iAttachment > 0)
	{
		CBaseAnimating* pAnim = pAttached->GetBaseAnimating();
		if (pAnim)
		{
			if (!pAnim->GetAttachment(iAttachment, vPos))
				return false;
		}
		else
		{
			return false;
		}
	}
	else
	{
		vPos = pAttached->GetEngineObject()->GetAbsOrigin();
	}

	return true;
}


bool CEngineRopeInternal::GetEndPointPos(int iPt, Vector& v)
{
	if (iPt == 0)
		return GetEndPointPos2(m_hStartPoint, m_iStartAttachment, v);
	else
		return GetEndPointPos2(m_hEndPoint, m_iEndAttachment, v);
}

void CEngineRopeInternal::UpdateBBox(bool bForceRelink)
{
	Vector v1, v2;
	Vector vMin, vMax;
	if (GetEndPointPos(0, v1))
	{
		if (GetEndPointPos(1, v2))
		{
			VectorMin(v1, v2, vMin);
			VectorMax(v1, v2, vMax);

			// Set our bounds to enclose both endpoints and relink.
			vMin -= GetAbsOrigin();
			vMax -= GetAbsOrigin();
		}
		else
		{
			vMin = vMax = v1 - GetAbsOrigin();
		}
	}
	else
	{
		vMin = vMax = Vector(0, 0, 0);
	}

	if (WorldAlignMins() != vMin || WorldAlignMaxs() != vMax)
	{
		UTIL_SetSize(this->m_pOuter, vMin, vMax);
	}
}

void CEngineRopeInternal::Init()
{
	SetLocalAngles(vec3_angle);
	RecalculateLength();

	SetSegments(clamp((int)GetSegments(), 2, ROPE_MAX_SEGMENTS));

	UpdateBBox(true);

	m_bStartPointValid = (GetStartPoint() != NULL);
	m_bEndPointValid = (GetEndPoint() != NULL);
}

void CEngineRopeInternal::NotifyPositionChanged()
{
	// Update our bbox?
	UpdateBBox(false);

	CBaseEntity* ents[2] = { m_hStartPoint.Get(), m_hEndPoint.Get() };
	if ((m_RopeFlags & ROPE_RESIZE) && ents[0] && ents[0]->entindex() != -1 && ents[1] && ents[1]->entindex() != -1)
	{
		int len = (int)(ents[0]->GetEngineObject()->GetAbsOrigin() - ents[1]->GetEngineObject()->GetAbsOrigin()).Length() + m_Slack;
		if (len != m_RopeLength)
		{
			m_RopeLength = len;
		}
	}

	// Figure out if our attachment points have gone away and make sure to update the client if they have.
	bool* pValid[2] = { &m_bStartPointValid, &m_bEndPointValid };
	for (int i = 0; i < 2; i++)
	{
		bool bCurrentlyValid = (ents[i] != NULL);
		if (*pValid[i] != bCurrentlyValid)
		{
			*pValid[i] = bCurrentlyValid;
		}
	}
}

void CEngineRopeInternal::SetMaterial(const char* pName)
{
	m_strRopeMaterialModel = AllocPooledString(pName);
	m_iRopeMaterialModelIndex = engine->PrecacheModel(STRING(m_strRopeMaterialModel));
}

void CEngineRopeInternal::DetachPoint(int iPoint)
{
	Assert(iPoint == 0 || iPoint == 1);

	m_fLockedPoints &= ~(1 << iPoint);
}

void CEngineRopeInternal::EnableCollision()
{
	if (!(m_RopeFlags & ROPE_COLLIDE))
	{
		m_RopeFlags |= ROPE_COLLIDE;
	}
}

void CEngineRopeInternal::EnableWind(bool bEnable)
{
	int flag = 0;
	if (!bEnable)
		flag |= ROPE_NO_WIND;

	if ((m_RopeFlags & ROPE_NO_WIND) != flag)
	{
		m_RopeFlags |= flag;
	}
}


struct collidelist_t
{
	const CPhysCollide* pCollide;
	Vector			origin;
	QAngle			angles;
};

// NOTE: This routine is relatively slow.  If you need to use it for per-frame work, consider that fact.
// UNDONE: Expand this to the full matrix of solid types on each side and move into enginetrace
bool TestEntityTriggerIntersection_Accurate(IEngineObjectServer* pTrigger, IEngineObjectServer* pEntity)
{
	Assert(pTrigger->GetSolid() == SOLID_BSP);

	if (pTrigger->Intersects(pEntity))	// It touches one, it's in the volume
	{
		switch (pEntity->GetSolid())
		{
		case SOLID_BBOX:
		{
			ICollideable* pCollide = pTrigger->GetCollideable();
			Ray_t ray;
			trace_t tr;
			ray.Init(pEntity->GetAbsOrigin(), pEntity->GetAbsOrigin(), pEntity->WorldAlignMins(), pEntity->WorldAlignMaxs());
			enginetrace->ClipRayToCollideable(ray, MASK_ALL, pCollide, &tr);

			if (tr.startsolid)
				return true;
		}
		break;
		case SOLID_BSP:
		case SOLID_VPHYSICS:
		{
			CPhysCollide* pTriggerCollide = modelinfo->GetVCollide(pTrigger->GetModelIndex())->solids[0];
			Assert(pTriggerCollide);

			CUtlVector<collidelist_t> collideList;
			IPhysicsObject* pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
			int physicsCount = pEntity->VPhysicsGetObjectList(pList, ARRAYSIZE(pList));
			if (physicsCount)
			{
				for (int i = 0; i < physicsCount; i++)
				{
					const CPhysCollide* pCollide = pList[i]->GetCollide();
					if (pCollide)
					{
						collidelist_t element;
						element.pCollide = pCollide;
						pList[i]->GetPosition(&element.origin, &element.angles);
						collideList.AddToTail(element);
					}
				}
			}
			else
			{
				vcollide_t* pVCollide = modelinfo->GetVCollide(pEntity->GetModelIndex());
				if (pVCollide && pVCollide->solidCount)
				{
					collidelist_t element;
					element.pCollide = pVCollide->solids[0];
					element.origin = pEntity->GetAbsOrigin();
					element.angles = pEntity->GetAbsAngles();
					collideList.AddToTail(element);
				}
			}
			for (int i = collideList.Count() - 1; i >= 0; --i)
			{
				const collidelist_t& element = collideList[i];
				trace_t tr;
				gEntList.PhysGetCollision()->TraceCollide(element.origin, element.origin, element.pCollide, element.angles, pTriggerCollide, pTrigger->GetAbsOrigin(), pTrigger->GetAbsAngles(), &tr);
				if (tr.startsolid)
					return true;
			}
		}
		break;

		default:
			return true;
		}
	}
	return false;
}

bool ShouldRemoveThisRagdoll(CBaseEntity* pRagdoll)
{
	if (g_RagdollLVManager.IsLowViolence())
	{
		return true;
	}

#ifdef CLIENT_DLL

	/* we no longer ignore enemies just because they are on fire -- a ragdoll in front of me
	   is always a higher priority for retention than a flaming zombie behind me. At the
	   time I put this in, the ragdolls do clean up their own effects if culled via SUB_Remove().
	   If you're encountering trouble with ragdolls leaving effects behind, try renabling the code below.
	/////////////////////
	//Just ignore it until we're done burning/dissolving.
	if ( pRagdoll->GetEffectEntity() )
		return false;
	*/

	Vector vMins, vMaxs;

	Vector origin = pRagdoll->GetEngineObject()->GetRagdollOrigin();
	pRagdoll->GetEngineObject()->GetRagdollBounds(vMins, vMaxs);

	if (engine->IsBoxInViewCluster(vMins + origin, vMaxs + origin) == false)
	{
		if (g_debug_ragdoll_removal.GetBool())
		{
			debugoverlay->AddBoxOverlay(origin, vMins, vMaxs, QAngle(0, 0, 0), 0, 255, 0, 16, 5);
			debugoverlay->AddLineOverlay(origin, origin + Vector(0, 0, 64), 0, 255, 0, true, 5);
		}

		return true;
	}
	else if (engine->CullBox(vMins + origin, vMaxs + origin) == true)
	{
		if (g_debug_ragdoll_removal.GetBool())
		{
			debugoverlay->AddBoxOverlay(origin, vMins, vMaxs, QAngle(0, 0, 0), 0, 0, 255, 16, 5);
			debugoverlay->AddLineOverlay(origin, origin + Vector(0, 0, 64), 0, 0, 255, true, 5);
		}

		return true;
	}

#else
	CBasePlayer* pPlayer = UTIL_GetLocalPlayer();

	if (!UTIL_FindClientInPVS(pRagdoll))
	{
		if (g_debug_ragdoll_removal.GetBool())
			NDebugOverlay::Line(pRagdoll->GetEngineObject()->GetAbsOrigin(), pRagdoll->GetEngineObject()->GetAbsOrigin() + Vector(0, 0, 64), 0, 255, 0, true, 5);

		return true;
	}
	else if (pPlayer && !pPlayer->FInViewCone(pRagdoll))
	{
		if (g_debug_ragdoll_removal.GetBool())
			NDebugOverlay::Line(pRagdoll->GetEngineObject()->GetAbsOrigin(), pRagdoll->GetEngineObject()->GetAbsOrigin() + Vector(0, 0, 64), 0, 0, 255, true, 5);

		return true;
	}

#endif

	return false;
}

// Called by CEntityListSystem
void CAimTargetManager::LevelInitPreEntity()
{
	gEntList.AddListenerEntity(this);
	Clear();
}
void CAimTargetManager::LevelShutdownPostEntity()
{
	gEntList.RemoveListenerEntity(this);
	Clear();
}

void CAimTargetManager::Clear()
{
	m_targetList.Purge();
}

void CAimTargetManager::ForceRepopulateList()
{
	Clear();

	CBaseEntity* pEnt = gEntList.FirstEnt();

	while (pEnt)
	{
		if (ShouldAddEntity(pEnt))
			AddEntity(pEnt);

		pEnt = gEntList.NextEnt(pEnt);
	}
}

bool CAimTargetManager::ShouldAddEntity(CBaseEntity* pEntity)
{
	return ((pEntity->GetEngineObject()->GetFlags() & FL_AIMTARGET) != 0);
}

// IEntityListener
void CAimTargetManager::OnEntityCreated(CBaseEntity* pEntity) {}
void CAimTargetManager::OnEntityDeleted(CBaseEntity* pEntity)
{
	if (!(pEntity->GetEngineObject()->GetFlags() & FL_AIMTARGET))
		return;
	RemoveEntity(pEntity);
}
void CAimTargetManager::AddEntity(CBaseEntity* pEntity)
{
	if (pEntity->GetEngineObject()->IsMarkedForDeletion())
		return;
	m_targetList.AddToTail(pEntity);
}
void CAimTargetManager::RemoveEntity(CBaseEntity* pEntity)
{
	int index = m_targetList.Find(pEntity);
	if (m_targetList.IsValidIndex(index))
	{
		m_targetList.FastRemove(index);
	}
}
int CAimTargetManager::ListCount() { return m_targetList.Count(); }
int CAimTargetManager::ListCopy(CBaseEntity* pList[], int listMax)
{
	int count = MIN(listMax, ListCount());
	memcpy(pList, m_targetList.Base(), sizeof(CBaseEntity*) * count);
	return count;
}

CAimTargetManager g_AimManager;

int AimTarget_ListCount()
{
	return g_AimManager.ListCount();
}
int AimTarget_ListCopy(CBaseEntity* pList[], int listMax)
{
	return g_AimManager.ListCopy(pList, listMax);
}
void AimTarget_ForceRepopulateList()
{
	g_AimManager.ForceRepopulateList();
}


// Manages a list of all entities currently doing game simulation or thinking
// NOTE: This is usually a small subset of the global entity list, so it's
// an optimization to maintain this list incrementally rather than polling each
// frame.
struct simthinkentry_t
{
	unsigned short	entEntry;
	unsigned short	unused0;
	int				nextThinkTick;
};
class CSimThinkManager : public IEntityListener<CBaseEntity>
{
public:
	CSimThinkManager()
	{
		Clear();
	}
	void Clear()
	{
		m_simThinkList.Purge();
		for (int i = 0; i < ARRAYSIZE(m_entinfoIndex); i++)
		{
			m_entinfoIndex[i] = 0xFFFF;
		}
	}
	void LevelInitPreEntity()
	{
		gEntList.AddListenerEntity(this);
	}

	void LevelShutdownPostEntity()
	{
		gEntList.RemoveListenerEntity(this);
		Clear();
	}

	void OnEntityCreated(CBaseEntity* pEntity)
	{
		Assert(m_entinfoIndex[pEntity->GetRefEHandle().GetEntryIndex()] == 0xFFFF);
	}
	void OnEntityDeleted(CBaseEntity* pEntity)
	{
		RemoveEntinfoIndex(pEntity->GetRefEHandle().GetEntryIndex());
	}

	void RemoveEntinfoIndex(int index)
	{
		int listHandle = m_entinfoIndex[index];
		// If this guy is in the active list, remove him
		if (listHandle != 0xFFFF)
		{
			Assert(m_simThinkList[listHandle].entEntry == index);
			m_simThinkList.FastRemove(listHandle);
			m_entinfoIndex[index] = 0xFFFF;

			// fast remove shifted someone, update that someone
			if (listHandle < m_simThinkList.Count())
			{
				m_entinfoIndex[m_simThinkList[listHandle].entEntry] = listHandle;
			}
		}
	}
	int ListCount()
	{
		return m_simThinkList.Count();
	}

	int ListCopy(CBaseEntity* pList[], int listMax)
	{
		int count = MIN(listMax, ListCount());
		int out = 0;
		for (int i = 0; i < count; i++)
		{
			// only copy out entities that will simulate or think this frame
			if (m_simThinkList[i].nextThinkTick <= gpGlobals->tickcount)
			{
				Assert(m_simThinkList[i].nextThinkTick >= 0);
				int entinfoIndex = m_simThinkList[i].entEntry;
				const CEntInfo<CBaseEntity>* pInfo = gEntList.GetEntInfoPtrByIndex(entinfoIndex);
				pList[out] = (CBaseEntity*)pInfo->m_pEntity;
				Assert(m_simThinkList[i].nextThinkTick == 0 || pList[out]->GetEngineObject()->GetFirstThinkTick() == m_simThinkList[i].nextThinkTick);
				Assert(gEntList.IsEntityPtr(pList[out]));
				out++;
			}
		}

		return out;
	}

	void EntityChanged(CBaseEntity* pEntity)
	{
		// might change after deletion, don't put back into the list
		if (pEntity->GetEngineObject()->IsMarkedForDeletion())
			return;

		const CBaseHandle& eh = pEntity->GetRefEHandle();
		if (!eh.IsValid())
			return;

		int index = eh.GetEntryIndex();
		if (pEntity->GetEngineObject()->IsEFlagSet(EFL_NO_THINK_FUNCTION) && pEntity->GetEngineObject()->IsEFlagSet(EFL_NO_GAME_PHYSICS_SIMULATION))
		{
			RemoveEntinfoIndex(index);
		}
		else
		{
			// already in the list? (had think or sim last time, now has both - or had both last time, now just one)
			if (m_entinfoIndex[index] == 0xFFFF)
			{
				MEM_ALLOC_CREDIT();
				m_entinfoIndex[index] = m_simThinkList.AddToTail();
				m_simThinkList[m_entinfoIndex[index]].entEntry = (unsigned short)index;
				m_simThinkList[m_entinfoIndex[index]].nextThinkTick = 0;
				if (pEntity->GetEngineObject()->IsEFlagSet(EFL_NO_GAME_PHYSICS_SIMULATION))
				{
					m_simThinkList[m_entinfoIndex[index]].nextThinkTick = pEntity->GetEngineObject()->GetFirstThinkTick();
					Assert(m_simThinkList[m_entinfoIndex[index]].nextThinkTick >= 0);
				}
			}
			else
			{
				// updating existing entry - if no sim, reset think time
				if (pEntity->GetEngineObject()->IsEFlagSet(EFL_NO_GAME_PHYSICS_SIMULATION))
				{
					m_simThinkList[m_entinfoIndex[index]].nextThinkTick = pEntity->GetEngineObject()->GetFirstThinkTick();
					Assert(m_simThinkList[m_entinfoIndex[index]].nextThinkTick >= 0);
				}
				else
				{
					m_simThinkList[m_entinfoIndex[index]].nextThinkTick = 0;
				}
			}
		}
	}

private:
	unsigned short m_entinfoIndex[NUM_ENT_ENTRIES];
	CUtlVector<simthinkentry_t>	m_simThinkList;
};

CSimThinkManager g_SimThinkManager;

int SimThink_ListCount()
{
	return g_SimThinkManager.ListCount();
}

int SimThink_ListCopy(CBaseEntity* pList[], int listMax)
{
	return g_SimThinkManager.ListCopy(pList, listMax);
}

void SimThink_EntityChanged(CBaseEntity* pEntity)
{
	g_SimThinkManager.EntityChanged(pEntity);
}

CEntityList::CEntityList()
{
	m_pItemList = NULL;
	m_iNumItems = 0;
}

CEntityList::~CEntityList()
{
	// remove all items from the list
	entitem_t* next, * e = m_pItemList;
	while (e != NULL)
	{
		next = e->pNext;
		delete e;
		e = next;
	}
	m_pItemList = NULL;
}

void CEntityList::AddEntity(CBaseEntity* pEnt)
{
	// check if it's already in the list; if not, add it
	entitem_t* e = m_pItemList;
	while (e != NULL)
	{
		if (e->hEnt == pEnt)
		{
			// it's already in the list
			return;
		}

		if (e->pNext == NULL)
		{
			// we've hit the end of the list, so tack it on
			e->pNext = new entitem_t;
			e->pNext->hEnt = pEnt;
			e->pNext->pNext = NULL;
			m_iNumItems++;
			return;
		}

		e = e->pNext;
	}

	// empty list
	m_pItemList = new entitem_t;
	m_pItemList->hEnt = pEnt;
	m_pItemList->pNext = NULL;
	m_iNumItems = 1;
}

void CEntityList::DeleteEntity(CBaseEntity* pEnt)
{
	// find the entry in the list and delete it
	entitem_t* prev = NULL, * e = m_pItemList;
	while (e != NULL)
	{
		// delete the link if it's the matching entity OR if the link is NULL
		if (e->hEnt == pEnt || e->hEnt == NULL)
		{
			if (prev)
			{
				prev->pNext = e->pNext;
			}
			else
			{
				m_pItemList = e->pNext;
			}

			delete e;
			m_iNumItems--;

			// REVISIT: Is this correct?  Is this just here to clean out dead EHANDLEs?
			// restart the loop
			e = m_pItemList;
			prev = NULL;
			continue;
		}

		prev = e;
		e = e->pNext;
	}
}

//-----------------------------------------------------------------------------
// NOTIFY LIST
// 
// Allows entities to get events fired when another entity changes
//-----------------------------------------------------------------------------
struct entitynotify_t
{
	CBaseEntity* pNotify;
	CBaseEntity* pWatched;
};
class CNotifyList : public INotify, public IEntityListener<CBaseEntity>
{
public:
	// INotify
	void AddEntity(CBaseEntity* pNotify, CBaseEntity* pWatched);
	void RemoveEntity(CBaseEntity* pNotify, CBaseEntity* pWatched);
	void ReportNamedEvent(CBaseEntity* pEntity, const char* pEventName);
	void ClearEntity(CBaseEntity* pNotify);
	void ReportSystemEvent(CBaseEntity* pEntity, notify_system_event_t eventType, const notify_system_event_params_t& params);

	// IEntityListener
	virtual void OnEntityCreated(CBaseEntity* pEntity);
	virtual void OnEntityDeleted(CBaseEntity* pEntity);

	// Called from CEntityListSystem
	void LevelInitPreEntity();
	void LevelShutdownPreEntity();

private:
	CUtlVector<entitynotify_t>	m_notifyList;
};

void CNotifyList::AddEntity(CBaseEntity* pNotify, CBaseEntity* pWatched)
{
	// OPTIMIZE: Also flag pNotify for faster "RemoveAllNotify" ?
	pWatched->GetEngineObject()->AddEFlags(EFL_NOTIFY);
	int index = m_notifyList.AddToTail();
	entitynotify_t& notify = m_notifyList[index];
	notify.pNotify = pNotify;
	notify.pWatched = pWatched;
}

// Remove noitfication for an entity
void CNotifyList::RemoveEntity(CBaseEntity* pNotify, CBaseEntity* pWatched)
{
	for (int i = m_notifyList.Count(); --i >= 0; )
	{
		if (m_notifyList[i].pNotify == pNotify && m_notifyList[i].pWatched == pWatched)
		{
			m_notifyList.FastRemove(i);
		}
	}
}


void CNotifyList::ReportNamedEvent(CBaseEntity* pEntity, const char* pInputName)
{
	variant_t emptyVariant;

	if (!pEntity->GetEngineObject()->IsEFlagSet(EFL_NOTIFY))
		return;

	for (int i = 0; i < m_notifyList.Count(); i++)
	{
		if (m_notifyList[i].pWatched == pEntity)
		{
			m_notifyList[i].pNotify->AcceptInput(pInputName, pEntity, pEntity, emptyVariant, 0);
		}
	}
}

void CNotifyList::LevelInitPreEntity()
{
	gEntList.AddListenerEntity(this);
}

void CNotifyList::LevelShutdownPreEntity(void)
{
	gEntList.RemoveListenerEntity(this);
	m_notifyList.Purge();
}

void CNotifyList::OnEntityCreated(CBaseEntity* pEntity)
{
}

void CNotifyList::OnEntityDeleted(CBaseEntity* pEntity)
{
	ReportDestroyEvent(pEntity);
	ClearEntity(pEntity);
}


// UNDONE: Slow linear search?
void CNotifyList::ClearEntity(CBaseEntity* pNotify)
{
	for (int i = m_notifyList.Count(); --i >= 0; )
	{
		if (m_notifyList[i].pNotify == pNotify || m_notifyList[i].pWatched == pNotify)
		{
			m_notifyList.FastRemove(i);
		}
	}
}

void CNotifyList::ReportSystemEvent(CBaseEntity* pEntity, notify_system_event_t eventType, const notify_system_event_params_t& params)
{
	if (!pEntity->GetEngineObject()->IsEFlagSet(EFL_NOTIFY))
		return;

	for (int i = 0; i < m_notifyList.Count(); i++)
	{
		if (m_notifyList[i].pWatched == pEntity)
		{
			m_notifyList[i].pNotify->NotifySystemEvent(pEntity, eventType, params);
		}
	}
}

static CNotifyList g_NotifyList;
INotify* g_pNotify = &g_NotifyList;

class CEntityTouchManager : public IEntityListener<CBaseEntity>
{
public:
	// called by CEntityListSystem
	void LevelInitPreEntity()
	{
		gEntList.AddListenerEntity(this);
		Clear();
	}
	void LevelShutdownPostEntity()
	{
		gEntList.RemoveListenerEntity(this);
		Clear();
	}
	void FrameUpdatePostEntityThink();

	void Clear()
	{
		m_updateList.Purge();
	}

	// IEntityListener
	virtual void OnEntityCreated(CBaseEntity* pEntity) {}
	virtual void OnEntityDeleted(CBaseEntity* pEntity)
	{
		if (!pEntity->GetEngineObject()->GetCheckUntouch())
			return;
		int index = m_updateList.Find(pEntity);
		if (m_updateList.IsValidIndex(index))
		{
			m_updateList.FastRemove(index);
		}
	}
	void AddEntity(CBaseEntity* pEntity)
	{
		if (pEntity->GetEngineObject()->IsMarkedForDeletion())
			return;
		m_updateList.AddToTail(pEntity);
	}

private:
	CUtlVector<CBaseEntity*>	m_updateList;
};

static CEntityTouchManager g_TouchManager;

void EntityTouch_Add(CBaseEntity* pEntity)
{
	g_TouchManager.AddEntity(pEntity);
}


void CEntityTouchManager::FrameUpdatePostEntityThink()
{
	VPROF("CEntityTouchManager::FrameUpdatePostEntityThink");
	// Loop through all entities again, checking their untouch if flagged to do so

	int count = m_updateList.Count();
	if (count)
	{
		// copy off the list
		CBaseEntity** ents = (CBaseEntity**)stackalloc(sizeof(CBaseEntity*) * count);
		memcpy(ents, m_updateList.Base(), sizeof(CBaseEntity*) * count);
		// clear it
		m_updateList.RemoveAll();

		// now update those ents
		for (int i = 0; i < count; i++)
		{
			//Assert( ents[i]->GetCheckUntouch() );
			if (ents[i]->GetEngineObject()->GetCheckUntouch())
			{
				ents[i]->GetEngineObject()->PhysicsCheckForEntityUntouch();
			}
		}
		stackfree(ents);
	}
}



// One hook to rule them all...
// Since most of the little list managers in here only need one or two of the game
// system callbacks, this hook is a game system that passes them the appropriate callbacks
class CEntityListSystem : public CAutoGameSystemPerFrame
{
public:
	CEntityListSystem(char const* name) : CAutoGameSystemPerFrame(name)
	{

	}
	void LevelInitPreEntity()
	{
		g_NotifyList.LevelInitPreEntity();
		g_TouchManager.LevelInitPreEntity();
		g_AimManager.LevelInitPreEntity();
		g_SimThinkManager.LevelInitPreEntity();
	}
	void LevelShutdownPreEntity()
	{
		g_NotifyList.LevelShutdownPreEntity();
	}
	void LevelShutdownPostEntity()
	{
		g_TouchManager.LevelShutdownPostEntity();
		g_AimManager.LevelShutdownPostEntity();
		g_SimThinkManager.LevelShutdownPostEntity();
	}

	void Update(float frametime)
	{
		gEntList.UpdateRagdolls(frametime);
	}

	void FrameUpdatePostEntityThink()
	{
		//This is pretty hacky, it's only called on the server so it just calls the update method.
		gEntList.UpdateRagdolls(0);
		g_TouchManager.FrameUpdatePostEntityThink();
	}
};

static CEntityListSystem g_EntityListSystem("CEntityListSystem");

class CSortedEntityList
{
public:
	CSortedEntityList() : m_sortedList(), m_emptyCount(0) {}

	typedef CBaseEntity* ENTITYPTR;
	class CEntityReportLess
	{
	public:
		bool Less(const ENTITYPTR& src1, const ENTITYPTR& src2, void* pCtx)
		{
			if (stricmp(src1->GetClassname(), src2->GetClassname()) < 0)
				return true;

			return false;
		}
	};

	void AddEntityToList(CBaseEntity* pEntity)
	{
		if (!pEntity)
		{
			m_emptyCount++;
		}
		else
		{
			m_sortedList.Insert(pEntity);
		}
	}
	void ReportEntityList()
	{
		const char* pLastClass = "";
		int count = 0;
		int edicts = 0;
		for (int i = 0; i < m_sortedList.Count(); i++)
		{
			CBaseEntity* pEntity = m_sortedList[i];
			if (!pEntity)
				continue;

			if (pEntity->entindex() != -1)
				edicts++;

			const char* pClassname = pEntity->GetClassname();
			if (!FStrEq(pClassname, pLastClass))
			{
				if (count)
				{
					Msg("Class: %s (%d)\n", pLastClass, count);
				}

				pLastClass = pClassname;
				count = 1;
			}
			else
				count++;
		}
		if (pLastClass[0] != 0 && count)
		{
			Msg("Class: %s (%d)\n", pLastClass, count);
		}
		if (m_sortedList.Count())
		{
			Msg("Total %d entities (%d empty, %d edicts)\n", m_sortedList.Count(), m_emptyCount, edicts);
		}
	}
private:
	CUtlSortVector< CBaseEntity*, CEntityReportLess > m_sortedList;
	int		m_emptyCount;
};



CON_COMMAND(report_entities, "Lists all entities")
{
	if (!UTIL_IsCommandIssuedByServerAdmin())
		return;

	CSortedEntityList list;
	CBaseEntity* pEntity = gEntList.FirstEnt();
	while (pEntity)
	{
		list.AddEntityToList(pEntity);
		pEntity = gEntList.NextEnt(pEntity);
	}
	list.ReportEntityList();
}


CON_COMMAND(report_touchlinks, "Lists all touchlinks")
{
	if (!UTIL_IsCommandIssuedByServerAdmin())
		return;

	CSortedEntityList list;
	CBaseEntity* pEntity = gEntList.FirstEnt();
	const char* pClassname = NULL;
	if (args.ArgC() > 1)
	{
		pClassname = args.Arg(1);
	}
	while (pEntity)
	{
		if (!pClassname || FClassnameIs(pEntity, pClassname))
		{
			servertouchlink_t* root = (servertouchlink_t*)pEntity->GetEngineObject()->GetDataObject(TOUCHLINK);
			if (root)
			{
				servertouchlink_t* link = root->nextLink;
				while (link && link != root)
				{
					list.AddEntityToList((CBaseEntity*)gEntList.GetServerEntityFromHandle(link->entityTouched));
					link = link->nextLink;
				}
			}
		}
		pEntity = gEntList.NextEnt(pEntity);
	}
	list.ReportEntityList();
}

CON_COMMAND(report_simthinklist, "Lists all simulating/thinking entities")
{
	if (!UTIL_IsCommandIssuedByServerAdmin())
		return;

	CBaseEntity* pTmp[NUM_ENT_ENTRIES];
	int count = SimThink_ListCopy(pTmp, ARRAYSIZE(pTmp));

	CSortedEntityList list;
	for (int i = 0; i < count; i++)
	{
		if (!pTmp[i])
			continue;

		list.AddEntityToList(pTmp[i]);
	}
	list.ReportEntityList();
}

