//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Game & Client shared functions moved from physics.cpp
//
//=============================================================================//
//#include "cbase.h"
#include <stdio.h>
#include <stdlib.h>
#include "string_t.h"
#include "bspfile.h"
#include "vcollide_parse.h"
#include "filesystem.h"
//#include "movevars_shared.h"
#include "engine/ivmodelinfo.h"
#include "physics_shared.h"
#include "model_types.h"
#include "bone_setup.h"
#include "vphysics/object_hash.h"
#include "vphysics/friction.h"
#include "coordsize.h"
#include <KeyValues.h>
#include "decals.h"
#include "IEffects.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"
#include "util_shared.h"
#include "physics_saverestore.h"
#ifdef CLIENT_DLL
#include "cliententitylist.h"
#endif // CLIENT_DLL
#ifdef GAME_DLL
#include "entitylist.h"
#endif // GAME_DLL


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//
//IPhysics			*physics = NULL;
//IPhysicsObject		*g_PhysWorldObject = NULL;
//IPhysicsCollision	*physcollision = NULL;
//IPhysicsEnvironment	*physenv = NULL;
//#ifdef PORTAL
//IPhysicsEnvironment	*physenv_main = NULL;
//#endif
//IPhysicsSurfaceProps *physprops = NULL;
// UNDONE: This hash holds both entity & IPhysicsObject pointer pairs
// UNDONE: Split into separate hashes?
//IPhysicsObjectPairHash *g_EntityCollisionHash = NULL;
#ifdef CLIENT_DLL
extern IVModelInfoClient* modelinfo;
#endif // CLIENT_DLL
#ifdef GAME_DLL
extern IVModelInfo* modelinfo;
#endif // GAME_DLL
extern ISoundEmitterSystem* g_pSoundEmitterSystem;

const char *SURFACEPROP_MANIFEST_FILE = "scripts/surfaceproperties_manifest.txt";

const objectparams_t g_PhysDefaultObjectParams =
{
	NULL,
	1.0, //mass
	1.0, // inertia
	0.1f, // damping
	0.1f, // rotdamping
	0.05f, // rotIntertiaLimit
	"DEFAULT",
	NULL,// game data
	0.f, // volume (leave 0 if you don't have one or call physcollision->CollideVolume() to compute it)
	1.0f, // drag coefficient
	true,// enable collisions?
};

// solid_t parsing
class CSolidSetDefaults : public IVPhysicsKeyHandler
{
public:
	virtual void ParseKeyValue(void* pData, const char* pKey, const char* pValue);
	virtual void SetDefaults(void* pData);

	unsigned int GetContentsMask() { return m_contentsMask; }

private:
	unsigned int m_contentsMask;
};


void CSolidSetDefaults::ParseKeyValue( void *pData, const char *pKey, const char *pValue )
{
	if ( !Q_stricmp( pKey, "contents" ) )
	{
		m_contentsMask = atoi( pValue );
	}
}

void CSolidSetDefaults::SetDefaults( void *pData )
{
	solid_t *pSolid = (solid_t *)pData;
	pSolid->params = g_PhysDefaultObjectParams;
	m_contentsMask = CONTENTS_SOLID;
}

CSolidSetDefaults g_SolidSetup;
IVPhysicsKeyHandler* g_pSolidSetup = &g_SolidSetup;

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &mins - 
//			&maxs - 
// Output : CPhysCollide
//-----------------------------------------------------------------------------
CPhysCollide *PhysCreateBbox( const Vector &minsIn, const Vector &maxsIn )
{
	// UNDONE: Track down why this causes errors for the player controller and adjust/enable
	//float radius = 0.5 - DIST_EPSILON;
	Vector mins = minsIn;// + Vector(radius, radius, radius);
	Vector maxs = maxsIn;// - Vector(radius, radius, radius);

	// VPHYSICS caches/cleans up these
	CPhysCollide *pResult = EntityList()->PhysGetCollision()->BBoxToCollide(mins, maxs);

	g_pPhysSaveRestoreManager->NoteBBox( mins, maxs, pResult );
	
	return pResult;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEntity - 
//			&mins - 
//			&maxs - 
//			&origin - 
//			isStatic - 
// Output : static IPhysicsObject
//-----------------------------------------------------------------------------
IPhysicsObject *PhysModelCreateBox( IHandleEntity *pEntity, const Vector &mins, const Vector &maxs, const Vector &origin, bool isStatic )
{
	int modelIndex = pEntity->GetEngineObject()->GetModelIndex();
	const char *pSurfaceProps = "flesh";
	solid_t solid;
	PhysGetDefaultAABBSolid( solid );
	Vector dims = maxs - mins;
	solid.params.volume = dims.x * dims.y * dims.z;

	if ( modelIndex )
	{
		const model_t *model = modelinfo->GetModel( modelIndex );
		if ( model )
		{
			IStudioHdr* studioHdr = modelinfo->GetStudiomodel( model );
			if (!studioHdr) {
				studioHdr = modelinfo->GetStudiomodel(model);
			}
			if (studioHdr && studioHdr->IsValid())
			{
				pSurfaceProps = studioHdr->Studio_GetDefaultSurfaceProps(  );
			}
		}
	}
	Q_strncpy( solid.surfaceprop, pSurfaceProps, sizeof( solid.surfaceprop ) );

	CPhysCollide *pCollide = PhysCreateBbox( mins, maxs );
	if ( !pCollide )
		return NULL;
	
	return PhysModelCreateCustom( pEntity, pCollide, origin, vec3_angle, STRING(pEntity->GetEngineObject()->GetModelName()), isStatic, &solid );
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEntity - 
//			&mins - 
//			&maxs - 
//			&origin - 
//			isStatic - 
// Output : static IPhysicsObject
//-----------------------------------------------------------------------------
IPhysicsObject *PhysModelCreateOBB( IHandleEntity *pEntity, const Vector &mins, const Vector &maxs, const Vector &origin, const QAngle &angle, bool isStatic )
{
	int modelIndex = pEntity->GetEngineObject()->GetModelIndex();
	const char *pSurfaceProps = "flesh";
	solid_t solid;
	PhysGetDefaultAABBSolid( solid );
	Vector dims = maxs - mins;
	solid.params.volume = dims.x * dims.y * dims.z;

	if ( modelIndex )
	{
		const model_t *model = modelinfo->GetModel( modelIndex );
		if ( model )
		{
			IStudioHdr* studioHdr = modelinfo->GetStudiomodel( model );
			if (studioHdr->IsValid()) 
			{
				pSurfaceProps = studioHdr->Studio_GetDefaultSurfaceProps(  );
			}
		}
	}
	Q_strncpy( solid.surfaceprop, pSurfaceProps, sizeof( solid.surfaceprop ) );

	CPhysCollide *pCollide = PhysCreateBbox( mins, maxs );
	if ( !pCollide )
		return NULL;
	
	return PhysModelCreateCustom( pEntity, pCollide, origin, angle, STRING(pEntity->GetEngineObject()->GetModelName()), isStatic, &solid );
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &solid - 
//			*pEntity - 
//			modelIndex - 
//			solidIndex - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool PhysModelParseSolidByIndex( solid_t &solid, IHandleEntity *pEntity, int modelIndex, int solidIndex )
{
	vcollide_t *pCollide = modelinfo->GetVCollide( modelIndex );
	if ( !pCollide )
		return false;

	bool parsed = false;

	memset( &solid, 0, sizeof(solid) );
	solid.params = g_PhysDefaultObjectParams;

	IVPhysicsKeyParser *pParse = EntityList()->PhysGetCollision()->VPhysicsKeyParserCreate( pCollide->pKeyValues );
	while ( !pParse->Finished() )
	{
		const char *pBlock = pParse->GetCurrentBlockName();
		if ( !strcmpi( pBlock, "solid" ) )
		{
			solid_t tmpSolid;
			memset( &tmpSolid, 0, sizeof(tmpSolid) );
			tmpSolid.params = g_PhysDefaultObjectParams;

			pParse->ParseSolid( &tmpSolid, &g_SolidSetup );

			if ( solidIndex < 0 || tmpSolid.index == solidIndex )
			{
				parsed = true;
				solid = tmpSolid;
				// just to be sure we aren't ever getting a non-zero solid by accident
				Assert( solidIndex >= 0 || solid.index == 0 );
				break;
			}
		}
		else
		{
			pParse->SkipBlock();
		}
	}
	EntityList()->PhysGetCollision()->VPhysicsKeyParserDestroy( pParse );

	// collisions are off by default
	solid.params.enableCollisions = true;

	solid.params.pGameData = static_cast<void *>(pEntity);
	solid.params.pName = STRING(pEntity->GetEngineObject()->GetModelName());
	return parsed;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &solid - 
//			*pEntity - 
//			modelIndex - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool PhysModelParseSolid( solid_t &solid, IHandleEntity *pEntity, int modelIndex )
{
	return PhysModelParseSolidByIndex( solid, pEntity, modelIndex, -1 );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &solid - 
//			*pEntity - 
//			*pCollide - 
//			solidIndex - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool PhysModelParseSolidByIndex( solid_t &solid, IHandleEntity *pEntity, vcollide_t *pCollide, int solidIndex )
{
	bool parsed = false;

	memset( &solid, 0, sizeof(solid) );
	solid.params = g_PhysDefaultObjectParams;

	IVPhysicsKeyParser *pParse = EntityList()->PhysGetCollision()->VPhysicsKeyParserCreate( pCollide->pKeyValues );
	while ( !pParse->Finished() )
	{
		const char *pBlock = pParse->GetCurrentBlockName();
		if ( !strcmpi( pBlock, "solid" ) )
		{
			solid_t tmpSolid;
			memset( &tmpSolid, 0, sizeof(tmpSolid) );
			tmpSolid.params = g_PhysDefaultObjectParams;

			pParse->ParseSolid( &tmpSolid, &g_SolidSetup );

			if ( solidIndex < 0 || tmpSolid.index == solidIndex )
			{
				parsed = true;
				solid = tmpSolid;
				// just to be sure we aren't ever getting a non-zero solid by accident
				Assert( solidIndex >= 0 || solid.index == 0 );
				break;
			}
		}
		else
		{
			pParse->SkipBlock();
		}
	}
	EntityList()->PhysGetCollision()->VPhysicsKeyParserDestroy( pParse );

	// collisions are off by default
	solid.params.enableCollisions = true;

	solid.params.pGameData = static_cast<void *>(pEntity);
	solid.params.pName = STRING(pEntity->GetEngineObject()->GetModelName());
	return parsed;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEntity - 
//			modelIndex - 
//			&origin - 
//			&angles - 
//			*pSolid - 
// Output : IPhysicsObject
//-----------------------------------------------------------------------------
IPhysicsObject *PhysModelCreate( IHandleEntity *pEntity, int modelIndex, const Vector &origin, const QAngle &angles, solid_t *pSolid )
{
	if ( !EntityList()->PhysGetEnv())
		return NULL;

	vcollide_t *pCollide = modelinfo->GetVCollide( modelIndex );
	if ( !pCollide || !pCollide->solidCount )
		return NULL;
	
	solid_t tmpSolid;
	if ( !pSolid )
	{
		pSolid = &tmpSolid;
		if ( !PhysModelParseSolidByIndex( tmpSolid, pEntity, pCollide, -1 ) )
			return NULL;
	}

	int surfaceProp = -1;
	if ( pSolid->surfaceprop[0] )
	{
		surfaceProp = EntityList()->PhysGetProps()->GetSurfaceIndex( pSolid->surfaceprop );
	}
	IPhysicsObject *pObject = EntityList()->PhysGetEnv()->CreatePolyObject( pCollide->solids[pSolid->index], surfaceProp, origin, angles, &pSolid->params );
	//PhysCheckAdd( pObject, STRING(pEntity->m_iClassname) );

	if ( pObject )
	{
		if ( modelinfo->GetModelType(modelinfo->GetModel(modelIndex)) == mod_brush )
		{
			unsigned int contents = modelinfo->GetModelContents( modelIndex );
			Assert(contents!=0);
			// HACKHACK: contents is used to filter collisions
			// HACKHACK: So keep solid on for water brushes since they should pass collision rules (as triggers)
			if ( contents & MASK_WATER )
			{
				contents |= CONTENTS_SOLID;
			}
			if ( contents != pObject->GetContents() && contents != 0 )
			{
				pObject->SetContents( contents );
				pObject->RecheckCollisionFilter();
			}
		}

		g_pPhysSaveRestoreManager->AssociateModel( pObject, modelIndex);
	}

	return pObject;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEntity - 
//			modelIndex - 
//			&origin - 
//			&angles - 
// Output : IPhysicsObject
//-----------------------------------------------------------------------------
IPhysicsObject *PhysModelCreateUnmoveable( IHandleEntity *pEntity, int modelIndex, const Vector &origin, const QAngle &angles )
{
	if ( !EntityList()->PhysGetEnv())
		return NULL;

	vcollide_t *pCollide = modelinfo->GetVCollide( modelIndex );
	if ( !pCollide || !pCollide->solidCount )
		return NULL;

	solid_t solid;

	if ( !PhysModelParseSolidByIndex( solid, pEntity, pCollide, -1 ) )
		return NULL;

	// collisions are off by default
	solid.params.enableCollisions = true;
	//solid.params.mass = 1.0;
	int surfaceProp = -1;
	if ( solid.surfaceprop[0] )
	{
		surfaceProp = EntityList()->PhysGetProps()->GetSurfaceIndex( solid.surfaceprop );
	}
	solid.params.pGameData = static_cast<void *>(pEntity);
	solid.params.pName = STRING(pEntity->GetEngineObject()->GetModelName());
	IPhysicsObject *pObject = EntityList()->PhysGetEnv()->CreatePolyObjectStatic( pCollide->solids[0], surfaceProp, origin, angles, &solid.params );

	//PhysCheckAdd( pObject, STRING(pEntity->m_iClassname) );
	if ( pObject )
	{
		if ( modelinfo->GetModelType(modelinfo->GetModel(modelIndex)) == mod_brush )
		{
			unsigned int contents = modelinfo->GetModelContents( modelIndex );
			Assert(contents!=0);
			if ( contents != pObject->GetContents() && contents != 0 )
			{
				pObject->SetContents( contents );
				pObject->RecheckCollisionFilter();
			}
		}
		g_pPhysSaveRestoreManager->AssociateModel( pObject, modelIndex);
	}

	return pObject;
}


//-----------------------------------------------------------------------------
// Purpose: Create a vphysics object based on an existing collision model
// Input  : *pEntity - 
//			*pModel - 
//			&origin - 
//			&angles - 
//			*pName - 
//			isStatic - 
//			*pSolid - 
// Output : IPhysicsObject
//-----------------------------------------------------------------------------
IPhysicsObject *PhysModelCreateCustom( IHandleEntity *pEntity, const CPhysCollide *pModel, const Vector &origin, const QAngle &angles, const char *pName, bool isStatic, solid_t *pSolid )
{
	if ( !EntityList()->PhysGetEnv())
		return NULL;

	solid_t tmpSolid;
	if ( !pSolid )
	{
		PhysGetDefaultAABBSolid( tmpSolid );
		pSolid = &tmpSolid;
	}
	int surfaceProp = EntityList()->PhysGetProps()->GetSurfaceIndex( pSolid->surfaceprop );
	pSolid->params.pGameData = static_cast<void *>(pEntity);
	pSolid->params.pName = pName;
	IPhysicsObject *pObject = NULL;
	if ( isStatic )
	{
		pObject = EntityList()->PhysGetEnv()->CreatePolyObjectStatic( pModel, surfaceProp, origin, angles, &pSolid->params );
	}
	else
	{
		pObject = EntityList()->PhysGetEnv()->CreatePolyObject( pModel, surfaceProp, origin, angles, &pSolid->params );
	}

	if ( pObject )
		g_pPhysSaveRestoreManager->AssociateModel( pObject, pModel);

	return pObject;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEntity - 
//			radius - 
//			&origin - 
//			&solid - 
// Output : IPhysicsObject
//-----------------------------------------------------------------------------
IPhysicsObject *PhysSphereCreate( IHandleEntity *pEntity, float radius, const Vector &origin, solid_t &solid )
{
	if ( !EntityList()->PhysGetEnv())
		return NULL;

	int surfaceProp = -1;
	if ( solid.surfaceprop[0] )
	{
		surfaceProp = EntityList()->PhysGetProps()->GetSurfaceIndex( solid.surfaceprop );
	}

	solid.params.pGameData = static_cast<void *>(pEntity);
	IPhysicsObject *pObject = EntityList()->PhysGetEnv()->CreateSphereObject( radius, surfaceProp, origin, vec3_angle, &solid.params, false );

	return pObject;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void PhysGetDefaultAABBSolid( solid_t &solid )
{
	solid.params = g_PhysDefaultObjectParams;
	solid.params.mass = 85.0f;
	solid.params.inertia = 1e24f;
	Q_strncpy( solid.surfaceprop, "default", sizeof( solid.surfaceprop ) );
}

//-----------------------------------------------------------------------------
// Purpose: Destroy a physics object
// Input  : *pObject - 
//-----------------------------------------------------------------------------
void PhysDestroyObject( IPhysicsObject *pObject, IHandleEntity *pEntity )
{
	g_pPhysSaveRestoreManager->ForgetModel( pObject );

	
	if ( pObject )
		pObject->SetGameData( NULL );

	EntityList()->PhysGetEntityCollisionHash()->RemoveAllPairsForObject( pObject );
	if ( pEntity && pEntity->GetEngineObject()->IsMarkedForDeletion() )
	{
		EntityList()->PhysGetEntityCollisionHash()->RemoveAllPairsForObject( pEntity );
	}

	if (EntityList()->PhysGetEnv())
	{
		EntityList()->PhysGetEnv()->DestroyObject( pObject );
	}
}

void AddSurfacepropFile( const char *pFileName, IPhysicsSurfaceProps *pProps, IFileSystem *pFileSystem )
{
	// Load file into memory
	FileHandle_t file = pFileSystem->Open( pFileName, "rb", "GAME" );

	if ( file )
	{
		int len = pFileSystem->Size( file );

		// read the file
		int nBufSize = len+1;
		if ( IsXbox() )
		{
			nBufSize = AlignValue( nBufSize , 512 );
		}
		char *buffer = (char *)stackalloc( nBufSize );
		pFileSystem->ReadEx( buffer, nBufSize, len, file );
		pFileSystem->Close( file );
		buffer[len] = 0;
		pProps->ParseSurfaceData( pFileName, buffer );
		// buffer is on the stack, no need to free
	}
	else
	{
		Error( "Unable to load surface prop file '%s' (referenced by manifest file '%s')\n", pFileName, SURFACEPROP_MANIFEST_FILE );
	}
}

void PhysParseSurfaceData( IPhysicsSurfaceProps *pProps, IFileSystem *pFileSystem )
{
	KeyValues *manifest = new KeyValues( SURFACEPROP_MANIFEST_FILE );
	if ( manifest->LoadFromFile( pFileSystem, SURFACEPROP_MANIFEST_FILE, "GAME" ) )
	{
		for ( KeyValues *sub = manifest->GetFirstSubKey(); sub != NULL; sub = sub->GetNextKey() )
		{
			if ( !Q_stricmp( sub->GetName(), "file" ) )
			{
				// Add
				AddSurfacepropFile( sub->GetString(), pProps, pFileSystem );
				continue;
			}

			Warning( "surfaceprops::Init:  Manifest '%s' with bogus file type '%s', expecting 'file'\n", 
				SURFACEPROP_MANIFEST_FILE, sub->GetName() );
		}
	}
	else
	{
		Error( "Unable to load manifest file '%s'\n", SURFACEPROP_MANIFEST_FILE );
	}

	manifest->deleteThis();
}

void PhysCreateVirtualTerrain( IHandleEntity *pWorld, const objectparams_t &defaultParams )
{
	if ( !EntityList()->PhysGetEnv())
		return;

	char nameBuf[1024];
	for ( int i = 0; i < MAX_MAP_DISPINFO; i++ )
	{
		CPhysCollide *pCollide = modelinfo->GetCollideForVirtualTerrain( i );
		if ( pCollide )
		{
			solid_t solid;
			solid.params = defaultParams;
			solid.params.enableCollisions = true;
			solid.params.pGameData = static_cast<void *>(pWorld);
			Q_snprintf(nameBuf, sizeof(nameBuf), "vdisp_%04d", i );
			solid.params.pName = nameBuf;
			int surfaceData = EntityList()->PhysGetProps()->GetSurfaceIndex( "default" );
			// create this as part of the world
			IPhysicsObject *pObject = EntityList()->PhysGetEnv()->CreatePolyObjectStatic( pCollide, surfaceData, vec3_origin, vec3_angle, &solid.params );
			pObject->SetCallbackFlags( pObject->GetCallbackFlags() | CALLBACK_NEVER_DELETED );
		}
	}
}

IPhysicsObject *PhysCreateWorld_Shared( IHandleEntity *pWorld, vcollide_t *pWorldCollide, const objectparams_t &defaultParams )
{
	solid_t solid;
	fluid_t fluid;

	if ( !EntityList()->PhysGetEnv())
		return NULL;

	int surfaceData = EntityList()->PhysGetProps()->GetSurfaceIndex( "default" );

	objectparams_t params = defaultParams;
	params.pGameData = static_cast<void *>(pWorld);
	params.pName = "world";

	IPhysicsObject *pWorldPhysics = EntityList()->PhysGetEnv()->CreatePolyObjectStatic(
		pWorldCollide->solids[0], surfaceData, vec3_origin, vec3_angle, &params );

	// hint - saves vphysics some work
	pWorldPhysics->SetCallbackFlags( pWorldPhysics->GetCallbackFlags() | CALLBACK_NEVER_DELETED );

	//PhysCheckAdd( world, "World" );
	// walk the world keys in case there are some fluid volumes to create
	IVPhysicsKeyParser *pParse = EntityList()->PhysGetCollision()->VPhysicsKeyParserCreate( pWorldCollide->pKeyValues );

	bool bCreateVirtualTerrain = false;
	while ( !pParse->Finished() )
	{
		const char *pBlock = pParse->GetCurrentBlockName();

		if ( !strcmpi( pBlock, "solid" ) || !strcmpi( pBlock, "staticsolid" ) )
		{
			solid.params = defaultParams;
			pParse->ParseSolid( &solid, &g_SolidSetup );
			solid.params.enableCollisions = true;
			solid.params.pGameData = static_cast<void *>(pWorld);
			solid.params.pName = "world";
			int surfaceData = EntityList()->PhysGetProps()->GetSurfaceIndex( "default" );

			// already created world above
			if ( solid.index == 0 )
				continue;

			if ( !pWorldCollide->solids[solid.index] )
			{
				// this implies that the collision model is a mopp and the physics DLL doesn't support that.
				bCreateVirtualTerrain = true;
				continue;
			}
			// create this as part of the world
			IPhysicsObject *pObject = EntityList()->PhysGetEnv()->CreatePolyObjectStatic( pWorldCollide->solids[solid.index],
				surfaceData, vec3_origin, vec3_angle, &solid.params );

			// invalid collision model or can't create, ignore
			if (!pObject)
				continue;

			pObject->SetCallbackFlags( pObject->GetCallbackFlags() | CALLBACK_NEVER_DELETED );
			Assert( g_SolidSetup.GetContentsMask() != 0 );
			pObject->SetContents( g_SolidSetup.GetContentsMask() );

			if ( !pWorldPhysics )
			{
				pWorldPhysics = pObject;
			}
		}
		else if ( !strcmpi( pBlock, "fluid" ) )
		{
			pParse->ParseFluid( &fluid, NULL );

			// create a fluid for floating
			if ( fluid.index > 0 )
			{
				solid.params = defaultParams;	// copy world's params
				solid.params.enableCollisions = true;
				solid.params.pName = "fluid";
				solid.params.pGameData = static_cast<void *>(pWorld);
				fluid.params.pGameData = static_cast<void *>(pWorld);
				int surfaceData = EntityList()->PhysGetProps()->GetSurfaceIndex( fluid.surfaceprop );
				// create this as part of the world
				IPhysicsObject *pWater = EntityList()->PhysGetEnv()->CreatePolyObjectStatic( pWorldCollide->solids[fluid.index],
					surfaceData, vec3_origin, vec3_angle, &solid.params );

				pWater->SetCallbackFlags( pWater->GetCallbackFlags() | CALLBACK_NEVER_DELETED );
				EntityList()->PhysGetEnv()->CreateFluidController( pWater, &fluid.params );
			}
		}
		else if ( !strcmpi( pBlock, "materialtable" ) )
		{
			int surfaceTable[128];
			memset( surfaceTable, 0, sizeof(surfaceTable) );

			pParse->ParseSurfaceTable( surfaceTable, NULL );
			EntityList()->PhysGetProps()->SetWorldMaterialIndexTable( surfaceTable, 128 );
		}
		else if ( !strcmpi(pBlock, "virtualterrain" ) )
		{
			bCreateVirtualTerrain = true;
			pParse->SkipBlock();
		}
		else
		{
			// unknown chunk???
			pParse->SkipBlock();
		}
	}
	EntityList()->PhysGetCollision()->VPhysicsKeyParserDestroy( pParse );

	if ( bCreateVirtualTerrain && EntityList()->PhysGetCollision()->SupportsVirtualMesh() )
	{
		PhysCreateVirtualTerrain( pWorld, defaultParams );
	}
	return pWorldPhysics;
}


//=============================================================================
//
// Physics Game Trace
//
class CPhysicsGameTrace : public IPhysicsGameTrace
{
public:

	void VehicleTraceRay( const Ray_t &ray, void *pVehicle, trace_t *pTrace );
	void VehicleTraceRayWithWater( const Ray_t &ray, void *pVehicle, trace_t *pTrace );
	bool VehiclePointInWater( const Vector &vecPoint );
};

CPhysicsGameTrace g_PhysGameTrace;
IPhysicsGameTrace *physgametrace = &g_PhysGameTrace;

//-----------------------------------------------------------------------------
// Purpose: Game ray-traces in vphysics.
//-----------------------------------------------------------------------------
void CPhysicsGameTrace::VehicleTraceRay( const Ray_t &ray, void *pVehicle, trace_t *pTrace )
{
	IHandleEntity *pBaseEntity = static_cast<IHandleEntity*>( pVehicle );
	UTIL_TraceRay( ray, MASK_SOLID, pBaseEntity, COLLISION_GROUP_NONE, pTrace );
}

//-----------------------------------------------------------------------------
// Purpose: Game ray-traces in vphysics.
//-----------------------------------------------------------------------------
void CPhysicsGameTrace::VehicleTraceRayWithWater( const Ray_t &ray, void *pVehicle, trace_t *pTrace )
{
	IHandleEntity *pBaseEntity = static_cast<IHandleEntity*>( pVehicle );
	UTIL_TraceRay( ray, MASK_SOLID|MASK_WATER, pBaseEntity, COLLISION_GROUP_NONE, pTrace );
}

//-----------------------------------------------------------------------------
// Purpose: Test to see if a vehicle point is in water.
//-----------------------------------------------------------------------------
bool CPhysicsGameTrace::VehiclePointInWater( const Vector &vecPoint )
{
	return ( ( UTIL_PointContents( vecPoint ) & MASK_WATER ) != 0 );
}


void PhysComputeSlideDirection( IPhysicsObject *pPhysics, const Vector &inputVelocity, const AngularImpulse &inputAngularVelocity, 
							   Vector *pOutputVelocity, Vector *pOutputAngularVelocity, float minMass )
{
	Vector velocity = inputVelocity;
	AngularImpulse angVel = inputAngularVelocity;
	Vector pos;

	IPhysicsFrictionSnapshot *pSnapshot = pPhysics->CreateFrictionSnapshot();
	while ( pSnapshot->IsValid() )
	{
		IPhysicsObject *pOther = pSnapshot->GetObject( 1 );
		if ( !pOther->IsMoveable() || pOther->GetMass() > minMass )
		{
			Vector normal;
			pSnapshot->GetSurfaceNormal( normal );

			// BUGBUG: Figure out the correct rotation clipping equation
			if ( pOutputAngularVelocity )
			{
				angVel = normal * DotProduct( angVel, normal );
#if 0
				pSnapshot->GetContactPoint( point );
				Vector point, dummy;
				AngularImpulse angularClip, clip2;

				pPhysics->CalculateVelocityOffset( normal, point, dummy, angularClip );
				VectorNormalize( angularClip );
				float proj = DotProduct( angVel, angularClip );
				if ( proj > 0 )
				{
					angVel -= angularClip * proj;
				}
				CrossProduct( angularClip, normal, clip2 );
				proj = DotProduct( angVel, clip2 );
				if ( proj > 0 )
				{
					angVel -= clip2 * proj;
				}
				//NDebugOverlay::Line( point, point - normal * 20, 255, 0, 0, true, 0.1 );
#endif
			}

			// Determine how far along plane to slide based on incoming direction.
			// NOTE: Normal points away from this object
			float proj = DotProduct( velocity, normal );
			if ( proj > 0.0f )
			{
				velocity -= normal * proj;
			}
		}
		pSnapshot->NextFrictionData();
	}
	pPhysics->DestroyFrictionSnapshot( pSnapshot );

	//NDebugOverlay::Line( pos, pos + unitVel * 20, 0, 0, 255, true, 0.1 );
	
	if ( pOutputVelocity )
	{
		*pOutputVelocity = velocity;
	}
	if ( pOutputAngularVelocity )
	{
		*pOutputAngularVelocity = angVel;
	}
}

bool PhysHasContactWithOtherInDirection( IPhysicsObject *pPhysics, const Vector &dir )
{
	bool hit = false;
	void *pGameData = pPhysics->GetGameData();
	IPhysicsFrictionSnapshot *pSnapshot = pPhysics->CreateFrictionSnapshot();
	while ( pSnapshot->IsValid() )
	{
		IPhysicsObject *pOther = pSnapshot->GetObject( 1 );
		if ( pOther->GetGameData() != pGameData )
		{
			Vector normal;
			pSnapshot->GetSurfaceNormal( normal );
			if ( DotProduct(normal,dir) > 0 )
			{
				hit = true;
				break;
			}
		}
		pSnapshot->NextFrictionData();
	}
	pPhysics->DestroyFrictionSnapshot( pSnapshot );

	return hit;
}


void PhysForceClearVelocity( IPhysicsObject *pPhys )
{
	IPhysicsFrictionSnapshot *pSnapshot = pPhys->CreateFrictionSnapshot();
	// clear the velocity of the rigid body
	Vector vel;
	AngularImpulse angVel;
	vel.Init();
	angVel.Init();
	pPhys->SetVelocity( &vel, &angVel );
	// now clear the "strain" stored in the contact points
	while ( pSnapshot->IsValid() )
	{
		pSnapshot->ClearFrictionForce();
		pSnapshot->RecomputeFriction();
		pSnapshot->NextFrictionData();
	}
	pPhys->DestroyFrictionSnapshot( pSnapshot );
}


void PhysFrictionEffect( Vector &vecPos, Vector vecVel, float energy, int surfaceProps, int surfacePropsHit )
{
	Vector invVecVel = -vecVel;
	VectorNormalize( invVecVel );

	surfacedata_t *psurf = EntityList()->PhysGetProps()->GetSurfaceData( surfaceProps );
	surfacedata_t *phit = EntityList()->PhysGetProps()->GetSurfaceData( surfacePropsHit );

	switch ( phit->game.material )
	{
	case CHAR_TEX_DIRT:
		
		if ( energy < MASS10_SPEED2ENERGY(15) )
			break;
		
		g_pEffects->Dust( vecPos, invVecVel, 1, 16 );
		break;

	case CHAR_TEX_CONCRETE:
		
		if ( energy < MASS10_SPEED2ENERGY(28) )
			break;
		
		g_pEffects->Dust( vecPos, invVecVel, 1, 16 );
		break;
	}
	
	//Metal sparks
	if ( energy > MASS10_SPEED2ENERGY(50) )
	{
		// make sparks for metal/concrete scrapes with enough energy
		if ( psurf->game.material == CHAR_TEX_METAL || psurf->game.material == CHAR_TEX_GRATE )
		{	
			switch ( phit->game.material )
			{
			case CHAR_TEX_CONCRETE:
			case CHAR_TEX_METAL:

				g_pEffects->MetalSparks( vecPos, invVecVel );
				break;									
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Precaches a surfaceproperties string name if it's set.
// Input  : idx - 
// Output : static void
//-----------------------------------------------------------------------------
static HSOUNDSCRIPTHANDLE PrecachePhysicsSoundByStringIndex( int idx )
{
	// Only precache if a value was set in the script file...
	if ( idx != 0 )
	{
		return g_pSoundEmitterSystem->PrecacheScriptSound(EntityList()->PhysGetProps()->GetString( idx ) );
	}

	return SOUNDEMITTER_INVALID_HANDLE;
}

//-----------------------------------------------------------------------------
// Purpose: Iterates all surfacedata sounds and precaches them
// Output : static void
//-----------------------------------------------------------------------------
void PrecachePhysicsSounds()
{
	// precache the surface prop sounds
	for ( int i = 0; i < EntityList()->PhysGetProps()->SurfacePropCount(); i++ )
	{
		surfacedata_t *pprop = EntityList()->PhysGetProps()->GetSurfaceData( i );
		Assert( pprop );

		pprop->soundhandles.stepleft = PrecachePhysicsSoundByStringIndex( pprop->sounds.stepleft );
		pprop->soundhandles.stepright = PrecachePhysicsSoundByStringIndex( pprop->sounds.stepright );
		pprop->soundhandles.impactSoft = PrecachePhysicsSoundByStringIndex( pprop->sounds.impactSoft );
		pprop->soundhandles.impactHard = PrecachePhysicsSoundByStringIndex( pprop->sounds.impactHard );
		pprop->soundhandles.scrapeSmooth = PrecachePhysicsSoundByStringIndex( pprop->sounds.scrapeSmooth );
		pprop->soundhandles.scrapeRough = PrecachePhysicsSoundByStringIndex( pprop->sounds.scrapeRough );
		pprop->soundhandles.bulletImpact = PrecachePhysicsSoundByStringIndex( pprop->sounds.bulletImpact );
		pprop->soundhandles.rolling = PrecachePhysicsSoundByStringIndex( pprop->sounds.rolling );
		pprop->soundhandles.breakSound = PrecachePhysicsSoundByStringIndex( pprop->sounds.breakSound );
		pprop->soundhandles.strainSound = PrecachePhysicsSoundByStringIndex( pprop->sounds.strainSound );
	}
}


